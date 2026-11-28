#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    void *p;
    write(1, "write() from glibc ok\n", 22);
    printf("printf from glibc ok\n");
    p = malloc(64);
    printf("malloc -> %p\n", p);
    free(p);
    return 0;
}
