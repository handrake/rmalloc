#ifndef RMALLOC_RMALLOC_H
#define RMALLOC_RMALLOC_H

#include <stddef.h>

#define RMALLOC_OK      0
#define RMALLOC_ERROR   1
#define RMALLOC_NOMEM   7
#define RMALLOC_RANGE   25

void *rmalloc(size_t size);
void rfree(void *ptr);

#endif //RMALLOC_RMALLOC_H
