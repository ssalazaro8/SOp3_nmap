#define _GNU_SOURCE
#include <bpf/libbpf.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void on_signal(int signo)
{
    (void)signo;
    stopping = 1;
}

int main(int argc, char **argv)
{
    const char *object_path = argc > 1 ? argv[1] : "build/mmap_guard.bpf.o";
    struct bpf_object *object;
    struct bpf_program *program;
    struct bpf_link *link;
    int error;

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    object = bpf_object__open_file(object_path, NULL);
    error = (int)libbpf_get_error(object);
    if (error) {
        fprintf(stderr, "cannot open BPF object %s: %s\n", object_path,
                strerror(-error));
        return EXIT_FAILURE;
    }

    error = bpf_object__load(object);
    if (error) {
        fprintf(stderr, "cannot load BPF object: %s\n", strerror(-error));
        bpf_object__close(object);
        return EXIT_FAILURE;
    }

    program = bpf_object__find_program_by_name(object, "deny_shared_writable_mmap");
    if (!program) {
        fprintf(stderr, "BPF program not found in object\n");
        bpf_object__close(object);
        return EXIT_FAILURE;
    }

    link = bpf_program__attach_lsm(program);
    error = (int)libbpf_get_error(link);
    if (error) {
        fprintf(stderr, "cannot attach mmap_file LSM hook: %s\n",
                strerror(-error));
        bpf_object__close(object);
        return EXIT_FAILURE;
    }

    if (signal(SIGINT, on_signal) == SIG_ERR || signal(SIGTERM, on_signal) == SIG_ERR) {
        perror("signal");
        bpf_link__destroy(link);
        bpf_object__close(object);
        return EXIT_FAILURE;
    }

    (void)argc;
    printf("mmap_guard active; shared writable file mappings denied. Press Ctrl-C to stop.\n");
    fflush(stdout);
    while (!stopping)
        pause();

    bpf_link__destroy(link);
    bpf_object__close(object);
    return EXIT_SUCCESS;
}

