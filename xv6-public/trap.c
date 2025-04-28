#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"
#include "pageswap.h"

// Declarations for external functions
pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
void rsect(uint sec, void *buf);
int swap_out_page(void);


// Interrupt descriptor table (shared by all CPUs)
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

extern struct swap_slot swap_slots[NSLOTS];
extern struct spinlock swtchlock;
extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

void clear_pte_a_for_10_percent() {
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) { // Iterate all processes
        if(p->state == UNUSED) continue;
        pte_t *pte;
        uint va;
        // Walk the page table and clear PTE_A for ~10% of pages
        // acquire(&tickslock);
        // uint count = ticks;
        // release(&tickslock);
        // uint start = count%p->sz;
        // start = PGSIZE * (start / PGSIZE); // Align to page size
        for(va = 0; va < p->sz; va += PGSIZE) {
            // count++;
            pte = walkpgdir(p->pgdir, (char *) va, 0); // Get PTE
            if(pte && (*pte & PTE_A) ) { 
                *pte &= ~PTE_A; // Clear Accessed bit
            }
        }
    }
}

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_PGFLT: {
    uint fault_va = rcr2();
    struct proc *p = myproc();
    if(p == 0 || p->pgdir == 0){
      cprintf("Page fault in kernel or no pgdir\n");
      p->killed = 1;
      break;
    }
    cprintf("%d: page fault at va %d with sz %d\n", p->pid, fault_va, p->sz);

    if(fault_va >= p->sz){
      cprintf("Page fault: va %d out of bounds\n", fault_va);
      p->killed = 1;
      break;
    }

    pte_t *pte = walkpgdir(p->pgdir, (char*)fault_va, 0);
    if(pte == 0){
      cprintf("Page fault: no PTE for addr 0x%x\n", fault_va);
      p->killed = 1;
      break;
    }
    if(*pte & PTE_P){
      cprintf("Unexpected page fault on present page at 0x%x\n", fault_va);
      p->killed = 1;
      break;
    }
    acquire(&swtchlock);

    int slot = (*pte) >> 12;
    

    if(slot >= NSLOTS){
      cprintf("Invalid swap slot index: %d\n", slot);
      p->killed = 1;
      release(&swtchlock);
      break;
    }

    if(swap_slots[slot].is_free){
      cprintf("PTE points to slot %d, but slot is marked free\n", slot);
      release(&swtchlock);
      p->killed = 1;
      break;
    }
    swap_slots[slot].is_free = 1;
    release(&swtchlock);

    char *mem = kalloc();
    if(mem == 0){
      cprintf("kalloc failed in page fault handler\n");
      p->killed = 1;
      break;
    }

    cprintf("Swapped in page at 0x%x from slot %d\n", fault_va, slot);
    for(int i = 0; i < 8; i++){
      begin_op();
      rsect(swap_slots[slot].start_block + i, mem + i * 512);
      end_op();
    }

    if(mappages(p->pgdir, (void*)PGROUNDDOWN(fault_va), PGSIZE,
                V2P(mem), swap_slots[slot].page_perm | PTE_P) < 0){
      cprintf("mappages failed in page fault handler\n");
      kfree(mem);
      release(&swtchlock);
      p->killed = 1;
      break;
    }
    *pte &= ~PTE_A;
  
    p->rss++;

    break;
  }

  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    clear_pte_a_for_10_percent();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
