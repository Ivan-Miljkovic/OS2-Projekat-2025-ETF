#ifndef XV6_RISCV_RISCV_BUDDY_H
#define XV6_RISCV_RISCV_BUDDY_H
#include "spinlock.h"
#define BLOCK_SIZE 4096
#define MAX_ORDER_LIMIT 12

//Buddy is inited only once, at the start of the os and it takes up a fixed amount of memory
typedef struct buddy{
    struct buddy *next;
}buddy_block_t;

typedef struct {
    buddy_block_t *free_list[MAX_ORDER_LIMIT + 1];
    int max_order;
    void* base;
    struct spinlock lock;
}buddy_allocator_t;

extern buddy_allocator_t* buddy_allocator;

buddy_allocator_t* buddy_init(void* start_address, int num_of_blocks);
void* buddy_alloc(int num_of_blocks);
int buddy_free( void* address, int num_of_blocks); //returns amount of blocks freed
#endif //XV6_RISCV_RISCV_BUDDY_H