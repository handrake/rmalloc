#include <stdio.h>
#include "rmalloc.h"

int main() {
    unsigned char *p = (unsigned char *) 100;

    for (int i = 0; p != NULL; i++) {
        p = rmalloc(4000);

        printf("%u: %p\n", i, p);

        rfree(p);
    }
}
