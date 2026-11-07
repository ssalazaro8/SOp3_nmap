#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define WRITE_ITERATIONS 200000U

struct race_context {
    unsigned char *mapping;
    size_t length;
    atomic_int done;
    atomic_int madvise_error;
};

static void *discard_worker(void *argument)
{
    struct race_context *context = argument;

    while (!atomic_load_explicit(&context->done, memory_order_relaxed)) {
        if (madvise(context->mapping, context->length, MADV_DONTNEED) < 0) {
            atomic_store(&context->madvise_error, errno);
            break;
        }
    }
    return NULL;
}

int main(void)
{
    char temporary_path[] = "/tmp/mmap-race-XXXXXX";
    long page_size = sysconf(_SC_PAGESIZE);
    struct race_context context;
    pthread_t worker;
    unsigned char *verification = NULL;
    int fd = -1;
    int result = EXIT_FAILURE;
    size_t offset;

    if (page_size <= 0) {
        fprintf(stderr, "could not determine page size\n");
        return EXIT_FAILURE;
    }
    context.length = (size_t)page_size * 16U;
    context.done = 0;
    context.madvise_error = 0;
    fd = mkstemp(temporary_path);
    if (fd < 0) {
        perror("mkstemp");
        goto cleanup;
    }
    if (ftruncate(fd, (off_t)context.length) < 0) {
        perror("ftruncate");
        goto cleanup;
    }

    verification = malloc(context.length);
    if (!verification) {
        perror("malloc");
        goto cleanup;
    }
    memset(verification, 'A', context.length);
    if (pwrite(fd, verification, context.length, 0) != (ssize_t)context.length) {
        perror("pwrite");
        goto cleanup;
    }

    context.mapping = mmap(NULL, context.length, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE, fd, 0);
    if (context.mapping == MAP_FAILED) {
        perror("mmap MAP_PRIVATE");
        goto cleanup;
    }
    if (pthread_create(&worker, NULL, discard_worker, &context) != 0) {
        perror("pthread_create");
        munmap(context.mapping, context.length);
        goto cleanup;
    }

    for (unsigned int i = 0; i < WRITE_ITERATIONS; i++) {
        offset = ((size_t)i * 7919U) % context.length;
        context.mapping[offset] = 'B';
    }
    atomic_store_explicit(&context.done, 1, memory_order_relaxed);
    if (pthread_join(worker, NULL) != 0) {
        perror("pthread_join");
        munmap(context.mapping, context.length);
        goto cleanup;
    }
    if (atomic_load(&context.madvise_error) != 0) {
        errno = atomic_load(&context.madvise_error);
        perror("madvise");
        munmap(context.mapping, context.length);
        goto cleanup;
    }
    if (munmap(context.mapping, context.length) < 0) {
        perror("munmap");
        goto cleanup;
    }

    memset(verification, 0, context.length);
    if (pread(fd, verification, context.length, 0) != (ssize_t)context.length) {
        perror("pread");
        goto cleanup;
    }
    for (offset = 0; offset < context.length; offset++) {
        if (verification[offset] != 'A') {
            fprintf(stderr, "FAIL: private mapping changed backing file at byte %zu\n", offset);
            goto cleanup;
        }
    }

    puts("PASS: concurrent MADV_DONTNEED/private writes left the backing file unchanged");
    result = EXIT_SUCCESS;

cleanup:
    free(verification);
    if (fd >= 0)
        close(fd);
    unlink(temporary_path);
    return result;
}

