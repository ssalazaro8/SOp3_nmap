#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *what)
{
    perror(what);
    return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    int fd;
    struct stat st;
    unsigned char *mapping;
    const char *path;

    if (argc != 2) {
        fprintf(stderr, "usage: %s FILE\n", argv[0]);
        return EXIT_FAILURE;
    }
    path = argv[1];
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return fail("open");
    if (fstat(fd, &st) < 0)
        return fail("fstat");
    if (st.st_size < 1) {
        fprintf(stderr, "test file must contain at least one byte\n");
        close(fd);
        return EXIT_FAILURE;
    }

    errno = 0;
    mapping = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping != MAP_FAILED) {
        munmap(mapping, 1);
        fprintf(stderr, "FAIL: MAP_SHARED writable mapping unexpectedly succeeded\n");
        close(fd);
        return EXIT_FAILURE;
    }
    if (errno != EACCES && errno != EPERM) {
        fprintf(stderr, "FAIL: shared mapping failed for unexpected reason: %s\n",
                strerror(errno));
        close(fd);
        return EXIT_FAILURE;
    }
    puts("PASS: writable MAP_SHARED mapping denied");

    mapping = mmap(NULL, 1, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) {
        close(fd);
        return fail("mmap MAP_PRIVATE");
    }
    {
        unsigned char original = mapping[0];
        mapping[0] ^= 0x5aU;
        if (mapping[0] == original) {
            munmap(mapping, 1);
            close(fd);
            fprintf(stderr, "FAIL: private mapping write was not visible locally\n");
            return EXIT_FAILURE;
        }
        mapping[0] = original;
    }
    if (munmap(mapping, 1) < 0) {
        close(fd);
        return fail("munmap");
    }
    close(fd);
    puts("PASS: writable MAP_PRIVATE mapping remains available");
    return EXIT_SUCCESS;
}

