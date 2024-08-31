#include <sys/mman.h>

#include "rmalloc.h"

/* all sizes in bytes */
#define DEFAULT_ARENA_SIZE  1048576
#define MIN_BLOCK_SIZE      32
#define MAX_BLOCK_SIZE      4096

static unsigned char *p_arena_start = NULL;
static unsigned char *p_arena_end = NULL;

int block_init(unsigned char *p_block_head, size_t size) {
    if (p_block_head == NULL || size < MIN_BLOCK_SIZE || size > MAX_BLOCK_SIZE) {
        return RMALLOC_RANGE;
    }

    unsigned char *p_block_tail = p_block_head + size - sizeof(unsigned int);

    *(unsigned int *)p_block_head = size << 3;
    *(unsigned int *)p_block_tail = size << 3;

    return RMALLOC_OK;
}

int find_free_block(unsigned int **p, size_t size) {
    unsigned char *p_block_head = p_arena_start;
    unsigned char *p_block_tail = NULL;
    size_t block_size;
    size_t new_size = size + 2 * sizeof(unsigned int);

    for (int i = 0; p_block_head < p_arena_end; i++) {
        block_size = (*(unsigned int *)p_block_head) >> 3;

        if ((*(unsigned int *)p_block_head & 1) == 0 && block_size - 2 * sizeof(unsigned int) >= size) {
            if (block_size - new_size > MIN_BLOCK_SIZE) {
                block_init(p_block_head + block_size, block_size - new_size);
                *(unsigned int *)p_block_head = new_size << 3;
            }
            *(unsigned int *)p_block_head |= 1;
            *p = (unsigned int *)(p_block_head + sizeof(unsigned int));

            p_block_tail = p_block_head + (*(unsigned int *)p_block_head >> 3) - sizeof(unsigned int);
            *(unsigned int *)p_block_tail = *(unsigned int *)p_block_head;

            return RMALLOC_OK;
        }

        p_block_head += block_size;
    }

    return RMALLOC_NOMEM;
}

void mm_init() {
    size_t block_size = MAX_BLOCK_SIZE;
    size_t arena_size = DEFAULT_ARENA_SIZE;

    p_arena_start = mmap(NULL, arena_size * sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    p_arena_end = p_arena_start + arena_size;

    for (int i = 0; i < arena_size / block_size - 1; i++) {
        block_init(p_arena_start + i * block_size, block_size);
    }
}

void *mm_malloc(size_t size) {
    unsigned long addr;
    int rc = find_free_block((unsigned int **) &addr, size);

    if (rc == RMALLOC_OK) {
        return (void *) addr;
    }

    return NULL;
}

void mm_free(unsigned int *ptr) {
    unsigned int *p_block_head = ptr - 1;

    if ((*(unsigned int *)p_block_head & 1) == 0) {
        return;
    }

    *p_block_head &= 0xFFFFFFF8;
}

void *rmalloc(size_t size) {
    if (p_arena_start == NULL) {
        mm_init();
    }

    return mm_malloc(size);
}

void rfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    mm_free((unsigned int *)ptr);
}
