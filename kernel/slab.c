
#include "slab.h"
#include "buddy.h"
#include "printf.h"
#define MIN_BUFFER_SIZE (1 << 5)
#define MAX_BUFFER_SIZE (1 << 17)

static kmem_cache_t *kmem_cache_list;
static struct spinlock kmem_list_lock;

void            acquire(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
int printf(char *fmt, ...);
void concat(char* dest, const char* s1, const char* s2);
char* itoa(int num, char* str);
char* strncpy(char *s, const char *t, int n);
int strncmp(const char *p, const char *q, uint n);
int strlen(const char *s);
void* memset(void *dst, int c, uint n);
static inline size_t align(size_t x)
{
	return (x + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
}
static inline void bitmap_set(uint8 *bm, int i)
{
	bm[i / 8] |= (1 << (i % 8));
}

static inline void bitmap_clear(uint8 *bm, int i)
{
	bm[i / 8] &= ~(1 << (i % 8));
}

static inline int bitmap_test(uint8 *bm, int i)
{
	return (bm[i / 8] >> (i % 8)) & 1;
}
void kmem_init(void* space, int num_blocks)
{
	buddy_allocator = buddy_init(space, num_blocks);
	initlock(&kmem_list_lock,"kmem_list_lock");
}

slab_t* create_slab(kmem_cache_t* kmem)
{
	slab_t* slab = buddy_alloc(1 << kmem->order);
	if (!slab) return 0;

	slab->next = 0;
	slab->mem = (void*)align((uint64)slab + sizeof(slab_t));
	slab->in_use = 0;
	int bm_bytes = (kmem->objects_per_slab + 7) / 8;

	slab->bitmap = (uint8*)((char*)slab->mem + kmem->objects_per_slab * kmem->size_of_object);

	memset(slab->bitmap, 0, bm_bytes); //set all bits of bitmap to 0

	//create the needed number of objects
	for (int i = 0; i < kmem->objects_per_slab; i++)
	{
		void *obj = (char*)slab->mem + i * kmem->size_of_object;
		// if constructor exists, call it
		if (kmem->ctor)
		{
			kmem->ctor(obj);
		}
	}
	return slab;
}
int destroy_slab(kmem_cache_t* cachep, slab_t* slab_list) //completly deallocates the slab_list with all objects
{
	if (!slab_list) return 0;
	int blocks_freed = 0;
	slab_t* slab;
	while (slab_list)
	{
		slab = slab_list;
		slab_list = slab_list->next;
		// if destructor exists, call it for every object in the slab
		if (cachep->dtor)
		{
			for (int i = 0; i < cachep->objects_per_slab; i++)
			{
				void *obj = (char*)slab->mem + i * cachep->size_of_object;
				cachep->dtor(obj);
			}
		}
		slab->next = 0;
		cachep->slab_num--;
		cachep->used_objects -= slab->in_use;
		blocks_freed += buddy_free(slab, 1 << cachep->order);
	}
	return blocks_freed;
}

void* get_obj_from_slab(slab_t* slab, kmem_cache_t* cachep) //gets a free object from slab
{
	if (slab == 0) return 0;
	//find index of free object in the bitmap
	int i =0;
	for (i = 0; i < cachep->objects_per_slab; i++)
	{
		if (!bitmap_test(slab->bitmap, i)) //check if the bit is 0
			break;
	}
	if (i >= cachep->objects_per_slab)
	{
		return 0;
	}
	void* obj = (char*)slab->mem + i * cachep->size_of_object;
	bitmap_set(slab->bitmap, i); //mark the bit as used
	//update stats
	slab->in_use++;
	cachep->used_objects++;
	//check if the slab is full, if so put it into full list
	if (slab->in_use == cachep->objects_per_slab)
	{
		cachep->partial = cachep->partial->next;
		slab->next = cachep->full;
		cachep->full = slab;
	}
	return obj;
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *))
{
	size_t size_needed_in_blocks = (sizeof(kmem_cache_t) - 1 + BLOCK_SIZE) / BLOCK_SIZE;
	kmem_cache_t* kmem = (kmem_cache_t*)buddy_alloc(size_needed_in_blocks);
	if (!kmem) return 0;

	kmem->size_of_object = align(size < sizeof(void*) ? sizeof(void*) : size);
	int slab_size = (kmem->size_of_object * NUM_OF_OBJECTS_IN_SLAB + sizeof(slab_t) - 1 + BLOCK_SIZE) / BLOCK_SIZE;

	int order = 0;
	while((1 << order) < slab_size) order++;

	int name_length = strlen(name);
	strncpy(kmem->name, name, name_length);
	kmem->name[name_length] = '\0';

	kmem->order = order;

	//calculate the size of the bitmap and adjust the number of objects per slab accordingly
	int obj_size = kmem->size_of_object;
	int slab_bytes = (1 << order) * BLOCK_SIZE;
	int obj_count = (slab_bytes - sizeof(slab_t)) / obj_size;

	int bm_bytes = (obj_count + 7) / 8;

	while (obj_count * obj_size + bm_bytes > slab_bytes - sizeof(slab_t))
	{
		obj_count--;
		bm_bytes = (obj_count + 7) / 8;
	}

	kmem->objects_per_slab = obj_count;
	kmem->ctor = ctor;
	kmem->dtor = dtor;
	kmem->slab_size = 1 << order;
	kmem->slab_num = 0;
	kmem->used_objects = 0;

	kmem->free = 0;
	kmem->partial = 0;
	kmem->full = 0;

	initlock(&kmem->lock, "kmem");
	//put into kmem_cache_list
	acquire(&kmem_list_lock);
	kmem->next = kmem_cache_list;
	kmem_cache_list = kmem;
	release(&kmem_list_lock);

	return kmem;
}
int kmem_cache_shrink(kmem_cache_t *cachep)
{
	acquire(&cachep->lock);
	//deallocate all the free lists
	if (cachep->needed_more_slabs == 1) //if there was a need for more slabs since the last shrink
	{
		cachep->needed_more_slabs = 0;
		release(&cachep->lock);
		return 0;
	}
	//deallocate all the free lists in cache
	int blocks_freed = destroy_slab(cachep, cachep->free);
	cachep->free = 0;

	release(&cachep->lock);
	return blocks_freed;
}
void* kmem_cache_alloc(kmem_cache_t *cachep)
{
	if (!cachep) return 0;

	slab_t* slab = 0;
	acquire(&cachep->lock);

	if (cachep->partial)
	{
		slab = cachep->partial;
	}
	else if (cachep->free)
	{
		//object is allocated free list becomes partial
		slab = cachep->free;
		cachep->free = cachep->free->next;
		slab->next = cachep->partial;
		cachep->partial = slab;
	}
	else //create a new slab
	{
		slab = create_slab(cachep);
		if (!slab)
		{
			release(&cachep->lock);
			return 0;
		}
		//add it to partial list
		slab->next = cachep->partial;
		cachep->partial = slab;
		cachep->slab_num++;
		cachep->needed_more_slabs = 1;
	}

	void *obj = get_obj_from_slab(slab, cachep);
	release(&cachep->lock);
	return obj;
}

void kmem_cache_free(kmem_cache_t *cachep, void *objp)
{
	if (!cachep) return;

	slab_t* slab = 0, *prev = 0;

	acquire(&cachep->lock);
	slab_t* curr = cachep->full;

	while (curr)
	{
		if (curr->mem <= objp && ((char*)curr->mem + cachep->size_of_object * cachep->objects_per_slab) > (char*)objp)
		{
			//this is the slab
			slab = curr;
			//delink the slab
			if (prev)
			{
				prev->next = slab->next;
			}
			else
			{
				cachep->full = slab->next;
			}
			slab->next = 0;
			break;
		}
		prev = curr;
		curr = curr->next;
	}
	//then check partial if slab not found
	if (slab == 0)
	{
		curr = cachep->partial;
		prev = 0;
		while (curr)
		{
			if (curr->mem <= objp && ((char*)curr->mem + cachep->size_of_object * cachep->objects_per_slab) > (char*)objp)
			{
				//this is the slab
				slab = curr;
				//delink the slab, if its not empty just link it back
				if (prev)
				{
					prev->next = slab->next;
				}
				else
				{
					cachep->partial = slab->next;
				}
				slab->next = 0;
				break;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	if (slab == 0) //there is no allocated object in the cache
	{
		release(&cachep->lock);
		return;
	}

	//find the index of the object in the bitmap
	int i = (int)(((char*)objp - (char*)slab->mem) / cachep->size_of_object);

	if (i < 0 || i >= cachep->objects_per_slab) //something went wrong
	{
		release(&cachep->lock);
		return;
	}
	bitmap_clear(slab->bitmap, i);
	slab->in_use--;
	cachep->used_objects--;

	//check if the slab is empty, if so put it into free list
	if (slab->in_use == 0)
	{
		slab->next = cachep->free;
		cachep->free = slab;
		//try to shrink the cache
		release(&cachep->lock);
		if (cachep->free && (cachep->partial || cachep->full))
		{
			kmem_cache_shrink(cachep);
		}
	}
	else //slab was full or slab stays in partial, put it in partial head, since its most likely it will need to be accessed again
	{
		slab->next = cachep->partial;
		cachep->partial = slab;
		release(&cachep->lock);
	}
}
//implicitly creates a cache if it doesnt exist
void *kmalloc(size_t size)
{
	if (size < MIN_BUFFER_SIZE || size > MAX_BUFFER_SIZE) return 0;

	char ssize[16];
	char name[24];
	concat(name, "size-", itoa(size, ssize));
	//find the cache that matches the object size and name "size-N"
	acquire(&kmem_list_lock);
	kmem_cache_t* cachep = kmem_cache_list;

	while (cachep)
	{
		//cache found, allocate one small memory buffer
		if (strncmp(cachep->name, name, strlen(cachep->name)) == 0)
		{
			release(&kmem_list_lock);
			return kmem_cache_alloc(cachep);
		}
		cachep = cachep->next;
	}
	//if it got to this point, there is no cache found, make a new one
	release(&kmem_list_lock);
	cachep = kmem_cache_create(name, size, 0, 0);
	if (cachep == 0) return 0; //could not allocate a cache

	return kmem_cache_alloc(cachep);
}

void kkfree(const void *objp)
{
	if (objp == 0) return;
	void *obj = (void*)objp;
	//find the cache that has the address of objp
	kmem_cache_t* cachep = kmem_cache_list;
	acquire(&kmem_list_lock);
	while (cachep)
	{
			acquire(&cachep->lock);
			//first check full list
			slab_t* curr = cachep->full;
			while (curr)
			{
				if (curr->mem <= objp && (void*)((char*)curr->mem + cachep->size_of_object * cachep->objects_per_slab) > objp)
				{
					//this is the cache
					release(&cachep->lock);
					release(&kmem_list_lock);
					kmem_cache_free(cachep, obj);

					return;
				}
				curr = curr->next;
			}
			//then check partial
			curr = cachep->partial;
			while (curr)
			{
				if (curr->mem <= objp && (void*)((char*)curr->mem + cachep->size_of_object * cachep->objects_per_slab) > objp)
				{
					//this is the cache
					release(&cachep->lock);
					release(&kmem_list_lock);
					kmem_cache_free(cachep, obj);
					return;
				}
				curr = curr->next;
			}
	    release(&cachep->lock);
		cachep = cachep->next;
	}
	//cache of the object not found
	release(&kmem_list_lock);
}

void kmem_cache_destroy(kmem_cache_t *cachep)
{
	acquire(&kmem_list_lock);
	acquire(&cachep->lock);
	//remove cache from the linked list
	kmem_cache_t* curr, *prev = 0;
	curr = kmem_cache_list;
	while (curr)
	{
		if (curr == cachep)
		{
			if (prev)
    			prev->next = curr->next;
			else
    			kmem_cache_list = curr->next;

			break;
		}
		prev = curr;
		curr = curr->next;
	}
	release(&kmem_list_lock);
	if (!curr)
	{
		release(&cachep->lock);
		return; //cache not found
	}
	//deallocate all the slabs
	slab_t* slab = cachep->full;
	destroy_slab(cachep, slab);
	slab = cachep->partial;
	destroy_slab(cachep, slab);
	slab = cachep->free;
	destroy_slab(cachep, slab);

	//deallocate the cache
	release(&cachep->lock);
	buddy_free(cachep,  (sizeof(kmem_cache_t) + BLOCK_SIZE - 1) / BLOCK_SIZE);

}

void kmem_cache_info(kmem_cache_t *cachep)
{
	//prints name, size_of_object, size_of_cache, slab_num, object_in_slab and cache fill percentage
	if (cachep == 0) return;

	acquire(&cachep->lock);
	int cache_size = (cachep->slab_num * cachep->slab_size * BLOCK_SIZE + sizeof(kmem_cache_t) - 1 + BLOCK_SIZE) / BLOCK_SIZE;
	int percentage = 0;
	if (cachep->slab_num != 0) {
		int total = cachep->objects_per_slab * cachep->slab_num;
		percentage = (cachep->used_objects * 100) / total;
	}

	printf("kmem_cache_info: cache %s\n", cachep->name);
	printf("Size of object: %d bytes\n", cachep->size_of_object);
	printf("Size of the cache: %d blocks\n", cache_size);
	printf("Number of slabs: %d\n", cachep->slab_num);
	printf("Objects per slab: %d\n", cachep->objects_per_slab);
	printf("Used objects: %d\n", cachep->used_objects);
	printf("Cache fill percentage: %d%%\n", percentage);

	release(&cachep->lock);
}
int kmem_cache_error(kmem_cache_t *cachep)
{
	if (!cachep)
	{
		printf("kmem_cache_error: cache pointer is NULL\n");
		return 1;
	}
	acquire(&cachep->lock);
	if (cachep->size_of_object == 0)
	{
		printf("kmem_cache_error (%s): object size is 0\n", cachep->name);
		release(&cachep->lock);
		return 1;
	}

	if (cachep->objects_per_slab == 0)
	{
		printf("kmem_cache_error (%s): objects_per_slab is 0\n", cachep->name);
		release(&cachep->lock);
		return 1;
	}

	if (cachep->slab_size == 0)
	{
		printf("kmem_cache_error (%s): slab_size is 0\n", cachep->name);
		release(&cachep->lock);
		return 1;
	}

	slab_t* slab;

	//check partial slabs
	slab = cachep->partial;
	while (slab)
	{
		if (slab->in_use == 0 || slab->in_use >= cachep->objects_per_slab)
		{
			printf("kmem_cache_error (%s): invalid partial slab (in_use=%u)\n", cachep->name, slab->in_use);
			release(&cachep->lock);
			return 1;
		}
		slab = slab->next;
	}

	// check full slabs
	slab = cachep->full;
	while (slab)
	{
		if (slab->in_use != cachep->objects_per_slab)
		{
			printf("kmem_cache_error (%s): full slab not fully used (in_use=%u)\n", cachep->name, slab->in_use);
			release(&cachep->lock);
			return 1;
		}
		slab = slab->next;
	}

	// check free slabs
	slab = cachep->free;
	while (slab)
	{
		if (slab->in_use != 0)
		{
			printf("kmem_cache_error (%s): free slab has in_use=%u\n", cachep->name, slab->in_use);
			release(&cachep->lock);
			return 1;
		}
		slab = slab->next;
	}
	release(&cachep->lock);
	return 0;
}