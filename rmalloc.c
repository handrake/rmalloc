#include <sys/mman.h>

#include "rmalloc.h"

/* all sizes in bytes */
#define DEFAULT_ARENA_SIZE              1048576
#define MIN_BLOCK_SIZE                  32
#define MAX_BLOCK_SIZE                  4096
#define BLOCK_META_SIZE                 (2*sizeof(unsigned int))
#define GET_BLOCK_SIZE(p)               ((*(unsigned int *)p) >> 3)
#define SET_BLOCK_SIZE(p, size)         (*(unsigned int *)p = size << 3)
#define GET_BLOCK_TAIL(head, size)      (head + size - sizeof(unsigned int))
#define GET_PREV_BLOCK_TAIL(head)       (head - sizeof(unsigned int))
#define IS_FREE_BLOCK(p)                ((*(unsigned int *)p & 1) == 0)
#define SET_BLOCK_USED(p)               (*(unsigned int *)p |= 1)
#define SET_BLOCK_FREE(p)               (*(unsigned int *)p &= 0xFFFFFFF8)
#define GET_BLOCK_META(p)               (*(unsigned int *)p)
#define SET_BLOCK_META(p1, p2)          (*(unsigned int *)p1 = GET_BLOCK_META(p2))
#define GET_RET_ADDR(head)              ((unsigned int *)(head + sizeof(unsigned int)))
#define MARK_BLOCK_BOUNDARY(p)          (*(unsigned int *)p |= 2)
#define IS_BLOCK_BOUNDARY(p)            (*(unsigned int *)p & 2)
#define GET_NEXT_BLOCK(head)            (head + GET_BLOCK_SIZE(p))
#define GET_PREV_BLOCK(head)            (head - GET_BLOCK_SIZE(GET_PREV_BLOCK_TAIL(head)))

typedef enum COALESCE_DIRECTION {
    NEXT,
    PREV,
    BOTH,
} COALESCE_DIRECTION;

static unsigned char *p_arena_start = NULL;
static unsigned char *p_arena_end = NULL;
static size_t arena_size = 0;

static unsigned char *p_last_allocated_block_start = NULL;

unsigned int next_power_of_2(unsigned int v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    return v;
}

int block_init(unsigned char *p_block_head, size_t size, int mark_boundary) {
    if (p_block_head == NULL || size < MIN_BLOCK_SIZE || size > MAX_BLOCK_SIZE || p_block_head + size > p_arena_end) {
        return RMALLOC_RANGE;
    }

    unsigned char *p_block_tail = GET_BLOCK_TAIL(p_block_head, size);

    SET_BLOCK_SIZE(p_block_head, size);
    SET_BLOCK_SIZE(p_block_tail, size);

    if (mark_boundary) {
        MARK_BLOCK_BOUNDARY(p_block_head);
        MARK_BLOCK_BOUNDARY(p_block_tail);
    }

    return RMALLOC_OK;
}

int allocate_block_if_available(unsigned int **p, unsigned char *p_block_head, size_t size) {
    unsigned char *p_block_tail;
    size_t block_size = GET_BLOCK_SIZE(p_block_head);

    if (IS_FREE_BLOCK(p_block_head) && block_size >= size) {
        if (block_size - size > MIN_BLOCK_SIZE) {
            block_init(p_block_head + size, block_size - size, 0);
            SET_BLOCK_SIZE(p_block_head, size);
        }
        SET_BLOCK_USED(p_block_head);
        *p = GET_RET_ADDR(p_block_head);

        p_block_tail = GET_BLOCK_TAIL(p_block_head, GET_BLOCK_SIZE(p_block_head));
        SET_BLOCK_META(p_block_tail, p_block_head);

        p_last_allocated_block_start = p_block_head;

        return RMALLOC_OK;
    }

    return RMALLOC_ERROR;
}

int find_free_block(unsigned int **p, size_t size) {
    unsigned char *p_block_head = p_last_allocated_block_start != NULL ? p_last_allocated_block_start : p_arena_start;
    unsigned char *p_block_tail;
    size_t new_size = next_power_of_2(size + BLOCK_META_SIZE);

    for (int i = 0; p_block_head < p_arena_end; i++) {
        if (allocate_block_if_available(p, p_block_head, new_size) == RMALLOC_OK) {
            return RMALLOC_OK;
        }

        p_block_head += GET_BLOCK_SIZE(p_block_head);
    }

    if (p_last_allocated_block_start == NULL) {
        return RMALLOC_NOMEM;
    }

    p_block_tail = GET_PREV_BLOCK_TAIL(p_last_allocated_block_start);
    p_block_head = p_last_allocated_block_start - GET_BLOCK_SIZE(p_block_tail);

    for (int i = 0; p_arena_start < p_block_head; i++) {
        if (allocate_block_if_available(p, p_block_head, new_size) == RMALLOC_OK) {
            return RMALLOC_OK;
        }

        p_block_tail = GET_PREV_BLOCK_TAIL(p_block_head);
        p_block_head -= GET_BLOCK_SIZE(p_block_tail);
    }

    return RMALLOC_NOMEM;
}

void mm_init() {
    size_t block_size = MAX_BLOCK_SIZE;
    arena_size = DEFAULT_ARENA_SIZE;

    p_arena_start = mmap(NULL, arena_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    p_arena_end = p_arena_start + arena_size;

    for (int i = 0; i < arena_size / block_size; i++) {
        block_init(p_arena_start + i * block_size, block_size, 1);
    }
}

void mm_exit() {
    if (p_arena_start != NULL) {
        munmap(p_arena_start, arena_size);
    }
}

void mm_coalesce(unsigned char *p, COALESCE_DIRECTION direction) {
    int rc;

    if (p < p_arena_start || p >= p_arena_end || !IS_FREE_BLOCK(p)) {
        return;
    }

    unsigned char *p_next_block_head = NULL;
    unsigned char *p_prev_block_head = NULL;

    switch (direction) {
        case NEXT:
            p_next_block_head = GET_NEXT_BLOCK(p);
            break;
        case PREV:
            p_prev_block_head = GET_PREV_BLOCK(p);
            break;
        case BOTH:
            p_next_block_head = GET_NEXT_BLOCK(p);
            p_prev_block_head = GET_PREV_BLOCK(p);
            break;
    }

    if (p_arena_end <= p_next_block_head || (p_next_block_head && IS_BLOCK_BOUNDARY(p_next_block_head)) || p == p_next_block_head) {
        p_next_block_head = NULL;
    }

    if (p_prev_block_head < p_arena_start || (p_prev_block_head && IS_BLOCK_BOUNDARY(p_prev_block_head)) || p == p_prev_block_head) {
        p_prev_block_head = NULL;
    }

    if (p_next_block_head) {
        rc = block_init(p, GET_BLOCK_SIZE(p) + GET_BLOCK_SIZE(p_next_block_head), 0);
        if (rc == RMALLOC_OK) {
            mm_coalesce(p, NEXT);
        }
    }

    if (p_prev_block_head) {
        block_init(p_prev_block_head, GET_BLOCK_SIZE(p_prev_block_head) + GET_BLOCK_SIZE(p), 0);
        if (rc == RMALLOC_OK) {
            mm_coalesce(p_prev_block_head, PREV);
        }
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

void mm_free(unsigned char *ptr) {
    unsigned char *p_block_head = ptr - sizeof(unsigned int);

    if (IS_FREE_BLOCK(p_block_head)) {
        return;
    }

    SET_BLOCK_FREE(p_block_head);
    mm_coalesce(p_block_head, BOTH);
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

    mm_free((unsigned char *)ptr);
}
