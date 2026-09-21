
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    printf("payload: real ELF running (pid=%d uid=%d argc=%d argv0=%s)\n",
           (int)getpid(), (int)getuid(), argc, argc > 0 ? argv[0] : "?");
    const char *p = getenv("PATH");
    printf("payload: PATH=%s\n", p ? p : "(unset)");
    void *h = malloc(64);
    if (h) {
        strcpy((char *)h, "heap works");
        printf("payload: %s\n", (char *)h);
        free(h);
    }
    return 42;
}
