// File: user/slabtest.c

#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"

#define RUN_NUM (5)
#define ITERATIONS (1000)
#define shared_size (7)
#define MASK (0xA5)
#define BLOCK_SIZE 4096
struct data_s {
    int id;
    void *shared;
    int iterations;
};
const char * const CACHE_NAMES[] = {"tc_0", "tc_1", "tc_2", "tc_3", "tc_4"};

static int i = 1;

int check(void *data, int size) {
    int ret = 1;
    for (int i = 0; i < size; i++) {
        if (((unsigned char *)data)[i] != MASK) {
            ret = 0;
        }
    }
    return ret;
}

struct objects_s {
    void *cache;
    void *data;
};

void work(void* pdata) {
    struct data_s data = *(struct data_s*) pdata;

    int size = 0;
    int object_size = data.id + 1;
    
    void *cache = kmem_cache_create(CACHE_NAMES[data.id], object_size, 0, 0);
    struct objects_s *objs = (struct objects_s*)(kmalloc(sizeof(struct objects_s) * data.iterations));
    
    for (int i = 0; i < data.iterations; i++) {
        if (i % 100 == 0) {
            objs[size].data = kmem_cache_alloc(data.shared);
			memset(objs[size].data, MASK, shared_size);
            objs[size].cache = data.shared;
            if (!check(objs[size].data, shared_size)) {
                printf("Value not correct!\n");
            }
        }
        else {
            objs[size].data = kmem_cache_alloc(cache);
            objs[size].cache = cache;
            memset(objs[size].data, MASK, object_size);
        }
        size++;
    }
    
    kmem_cache_info(cache);
    kmem_cache_info(data.shared);
    
    for (int i = 0; i < size; i++) {
        if (!check(objs[i].data, (cache == objs[i].cache) ? object_size : shared_size)) {
            printf("Value not correct!\n");
        }
        kmem_cache_free(objs[i].cache, objs[i].data);
    }
    
    kkfree(objs);
    kmem_cache_destroy(cache);
}

void runs(void(*work)(void*), struct data_s* data, int num) {
    for (int i = 0; i < num; i++) {
        struct data_s private_data;
        private_data = *(struct data_s*) data;
        private_data.id = i;
        work(&private_data);
    }
}

int main(void) {
    int num_of_blocks = 1024;

	printf("CONSTRUCT %p\n", construct);
	printf("MAIN %p\n", main);
	printf("WORK %p\n", work);
    void* space = malloc(num_of_blocks * BLOCK_SIZE);
    kmem_init(space, num_of_blocks);
    void *shared = kmem_cache_create("shared object", shared_size,  construct, 0);
	struct data_s data;

    data.shared = shared;
    data.iterations = ITERATIONS;
    runs(work, &data, RUN_NUM);
    
    kmem_cache_destroy(shared);

    exit(0);
}
__attribute__((noinline, used))
void construct(void *data) {

    printf("%d Shared object constructed.\n", i++);
    memset(data, MASK, shared_size);
}