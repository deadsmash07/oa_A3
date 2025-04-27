
#include "param.h"
#include "types.h"

#ifndef PAGESWAP_H
#define PAGESWAP_H

#define NSLOTS 800

struct swap_slot {
    int is_free;
    int page_perm;
    int start_block;
    int pid;
    uint va;
};

extern int Th;
extern int Npg;
extern const int LIMIT;
extern struct spinlock swtchlock;
extern struct swap_slot swap_slots[NSLOTS];

void initswap();
int swap_out_page(void);


#endif
