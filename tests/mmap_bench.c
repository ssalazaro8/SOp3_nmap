#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t nanoseconds(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec * UINT64_C(1000000000) + (uint64_t)ts->tv_nsec;
}

static int parse_positive(const char *text, unsigned long *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0)
        return -1;
    *value = parsed;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long iterations = 10000;
    unsigned long mib = 64;
    long page_size = sysconf(_SC_PAGESIZE);
    struct stat st;
    struct timespec start, end;
    struct rusage before, after;
    uint64_t checksum = 0;
    size_t length;
    int fd;
    volatile unsigned char sink = 0;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s FILE [ITERATIONS [MIB]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if ((argc >= 3 && parse_positive(argv[2], &iterations) < 0) ||
        (argc == 4 && parse_positive(argv[3], &mib) < 0) || page_size <= 0 ||
        mib > SIZE_MAX / (1024U * 1024U)) {
        fprintf(stderr, "invalid iteration count, size, or page size\n");
        return EXIT_FAILURE;
    }
    length = (size_t)mib * 1024U * 1024U;
    fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }
    if (fstat(fd, &st) < 0 || st.st_size < 0 || (uint64_t)st.st_size < length) {
        fprintf(stderr, "benchmark file must be at least %lu MiB\n", mib);
        close(fd);
        return EXIT_FAILURE;
    }
    if (getrusage(RUSAGE_SELF, &before) < 0 || clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        perror("measurement start");
        close(fd);
        return EXIT_FAILURE;
    }

    for (unsigned long iteration = 0; iteration < iterations; iteration++) {
        unsigned char *map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return EXIT_FAILURE;
        }
        for (size_t offset = 0; offset < length; offset += (size_t)page_size)
            sink ^= map[offset];
        checksum += sink;
        if (munmap(map, length) < 0) {
            perror("munmap");
            close(fd);
            return EXIT_FAILURE;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end) < 0 || getrusage(RUSAGE_SELF, &after) < 0) {
        perror("measurement end");
        close(fd);
        return EXIT_FAILURE;
    }
    close(fd);

    uint64_t elapsed = nanoseconds(&end) - nanoseconds(&start);
    printf("iterations=%lu file_mib=%lu ns_per_map_touch_unmap=%.2f "
           "minor_faults=%ld major_faults=%ld checksum=%" PRIu64 "\n",
           iterations, mib, (double)elapsed / (double)iterations,
           after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt,
           checksum + (uint64_t)sink);
    return EXIT_SUCCESS;
}

