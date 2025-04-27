#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "pageswap.h"
#include "fs.h"
#include "sleeplock.h"
#include "buf.h"

// external function prototypes
pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);

struct swap_slot swap_slots[NSLOTS];
struct spinlock swtchlock;

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

const int LIMIT = 100; // maximum allowed Npg
int Th = 100;    // initial threshold
int Npg = 4;     // initial number of pages to swap out

void initswap() {

    cprintf("Initializing swap slots...\n");
    initlock(&swtchlock, "swtchlock");

    int i;
    for (i = 0; i < NSLOTS; i++) {
        swap_slots[i].is_free = 1;
        swap_slots[i].page_perm = 0;
        swap_slots[i].start_block = 2 + i * 8; // From SWAPSTART
        swap_slots[i].pid = -1; // No process assigned
        swap_slots[i].va = 0; // No virtual address assigned
    }
}


struct proc *select_victim_process(void) {
  struct proc *victim = 0;
  acquire(&ptable.lock);
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    // Consider only processes in RUNNING, RUNNABLE, or SLEEPING states and with pid >= 1.
    if(p->state == UNUSED || p->pid < 1)
      continue;
    if(victim == 0 || p->rss > victim->rss ||
       (p->rss == victim->rss && p->pid < victim->pid)) {
      victim = p;
    }
  }
  release(&ptable.lock);
  return victim;
}

// Returns the virtual address of the victim page; also, you may return its pte pointer through an argument.
uint select_victim_page(struct proc *p, pte_t **victim_pte) {
  uint va;
  pte_t *pte;
  // You can search user space in steps of PGSIZE.
  for(va = 0; va < p->sz; va += PGSIZE) {
    pte = walkpgdir(p->pgdir, (char*)va, 0);
    if(pte && (*pte & PTE_P) && !(*pte & PTE_A)) {
      if(victim_pte)
        *victim_pte = pte;
      return va;
    }
  }

  // If no page is found where PTE_A is cleared, you might choose a fallback strategy
  // For example, try another pass or return 0.
  return va;
}

int swap_out_page(void) {
  // 1. Select victim process.
  struct proc *vp = select_victim_process();
  if(vp == 0) {
    cprintf("swap_out_page: No eligible process found\n");
    return -1;
  }
  
  // 2. Select victim page from that process.
  pte_t *victim_pte;
  uint victim_va = select_victim_page(vp, &victim_pte);
  if(victim_va >= vp->sz) {
    cprintf("swap_out_page: No eligible page found in process %d\n", vp->pid);
    return -1;
  }
//   cprintf("RSS of process %d is %d\n", vp->pid, vp->rss);
  // 3. Find a free swap slot.
  if (!(*victim_pte & PTE_P)) {
    cprintf("Victim PTE not present for process %d va 0x%x\n", vp->pid, victim_va);
    return -1;
  }

  int slot;
  acquire(&swtchlock); // use a lock to protect swap_slots (you may need to create this lock)
  for(slot = 0; slot < NSLOTS; slot++) {
    if(swap_slots[slot].is_free)
      break;
  }
  if(slot == NSLOTS) {
    release(&swtchlock);
    cprintf("swap_out_page: No free swap slots available\n");
    return -1;
  }
  
  // Mark the slot as in-use (temporarily) and record page permissions.
  swap_slots[slot].is_free = 0;
  // You may extract page permissions from *victim_pte, e.g., user, read/write bits.
  swap_slots[slot].page_perm = (*victim_pte) & 0xFFF;  // lower 12 bits could store permission bits
  swap_slots[slot].pid = vp->pid;
  swap_slots[slot].va = victim_va;
  cprintf("Swapping out page from process %d, va %d\n", vp->pid, victim_va);
  // 4. Write the page content into the swap slot.
  // Each swap slot represents 8 disk blocks.
  // Calculate starting block from swap_slots[slot].start_block.
  release(&swtchlock);
    
  
  char *v = P2V(PTE_ADDR(*victim_pte));
  for (int i = 0; i < 8; i++) {
    // Write each block (512 bytes) from the page into disk.
    // You will need to use wsect() or similar function to write to disk.
    // Example:
    begin_op();
    wsect(swap_slots[slot].start_block + i, v + i * 512);
    end_op();
  }
  
  // 5. Update the page table entry: clear the PTE_P flag.
  *victim_pte &= ~PTE_P;
  
  // 6. Adjust the process's resident page count.
  // Using myproc() might not be applicable here since we're in the context of the victim process.
  // So, do:
  vp->rss--;
  
//   release(&swtchlock);
  
  // Return some indicator (e.g. the swap slot used) or 0 on success.
  return slot;
}



