#include "buddy.h"
#include "types.h"

void            acquire(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void panic(char *s);

buddy_allocator_t* buddy_allocator;

buddy_block_t* get_buddy_block(int order)
{
    buddy_block_t* block = buddy_allocator->free_list[order];
    if (!block) return 0;
    buddy_allocator->free_list[order] = block->next;  //unlink the block
    return block;
}

void split_buddy(int order)
{
    buddy_block_t* bb = buddy_allocator->free_list[order]; //take the first buddy_block of that order from the linked list
    if(!bb) return;
    buddy_allocator->free_list[order] = bb->next; //relink the linked list

    //split the block
    buddy_block_t* left = bb;
    buddy_block_t* right = (buddy_block_t*)((char*)bb + (1 << (order - 1)) * BLOCK_SIZE); // move the right one exactly half bytes away

    //put them into the free list of order - 1
    left->next = right;
    right->next = buddy_allocator->free_list[order - 1];
    buddy_allocator->free_list[order - 1] = left;
}

buddy_allocator_t* buddy_init(void* start_address, int num_of_blocks)
{
    buddy_allocator_t* buddy_allocator = (buddy_allocator_t*)start_address;
    int size_of_buddy_in_blocks = (sizeof(buddy_allocator_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int order = 0;
    while((1 << order) < num_of_blocks - size_of_buddy_in_blocks) order++;
    if (order > MAX_ORDER_LIMIT) return 0;
    //put buddy allocator on the start of starting_address as meta data
    char* managed_address = (char*)start_address +  size_of_buddy_in_blocks * BLOCK_SIZE;
    buddy_allocator->base = (void*)managed_address;
    for(int i = 0; i < MAX_ORDER_LIMIT + 1; i++)
    {
        buddy_allocator->free_list[i] = 0;
    }

    buddy_allocator->max_order = order;
    buddy_block_t* bb = (void*)managed_address;
    bb->next = 0;
    buddy_allocator->free_list[order] = bb;

    initlock(&buddy_allocator->lock, "buddy_lock");
    return buddy_allocator;
}

void* buddy_alloc(int num_of_blocks)
{
    if (buddy_allocator == 0)
        panic("buddy_allocator is NULL");
    void* buddy_block;
    //align up num_of_blocks to 2^n
    int order = 0;
    while((1 << order) < num_of_blocks) order++;
    if (order > MAX_ORDER_LIMIT) return 0;
    int i = order;
    //find free_block of 2^n
    acquire(&buddy_allocator->lock);
    while (i <= buddy_allocator->max_order)
    {
        if (!buddy_allocator->free_list[i]) i++; // block of order[i] is not free, look for the one that is
        else if (buddy_allocator->free_list[i] && i < order) i++; // block of order[i] is free but not big enough
        else if (buddy_allocator->free_list[i] && i == order) //block of order[i] is found!
        {
            buddy_block = (void*)get_buddy_block(i);
            release(&buddy_allocator->lock);
            return buddy_block ;
        }
        else if (buddy_allocator->free_list[i] && i > order) //block of order[i] is free but too big, need to split it
        {
            while (i != order)
            {
                split_buddy(i); //splits the buddy to i-1 order blocks
                i--; //go to the i-1 order blocks
            }
            //block of order[i] is splitted and ready to return;
            buddy_block = (void*)get_buddy_block(i);
            release(&buddy_allocator->lock);
            return buddy_block ;
        }
    }
    release(&buddy_allocator->lock);
    return 0; //no blocks can be allocated
}

int buddy_free(void* address, int num_of_blocks)
{
    buddy_block_t* free_block = address;

    //find the order of the block
    int order = 0;
    while((1 << order) < num_of_blocks) order++;

    acquire(&buddy_allocator->lock);
    //find its buddy and merge if possible
    while (order < MAX_ORDER_LIMIT)
    {
        int merged = 0; //condition to break from the loop if no merges happened
        //find its buddy
        uint64 offset = (uint64)address - (uint64)buddy_allocator->base;
        uint64 buddy_offset = offset ^ (uint64)((1 << order) * BLOCK_SIZE);
        buddy_block_t* buddy = (buddy_block_t*)(buddy_allocator->base + buddy_offset);

        //find the buddy in the free list
        buddy_block_t* prev = 0;
        buddy_block_t* cur = buddy_allocator->free_list[order];

        while (cur)
        {
            if (cur == buddy) //buddy is not allocated, they can be merged
            {
                if (prev)
                    prev->next = cur->next;
                else
                    buddy_allocator->free_list[order] = cur->next;

                cur->next = 0;
                merged = 1;
                break;
            }
            prev = cur;
            cur = cur->next;
        }

        if (merged == 0) break; //no more blocks can be merged

        //set the address variable to the left one
        if ((uint64)address > (uint64)buddy) address = (void*)buddy;

        order++;
    }
    //no more merges put the new buddy_block into the free list
    free_block = (buddy_block_t*)address;
    free_block->next = buddy_allocator->free_list[order];
    buddy_allocator->free_list[order] = free_block;

    release(&buddy_allocator->lock);

    return (1 << order); //num of blocks freed
}

