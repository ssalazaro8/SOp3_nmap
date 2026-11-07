// Deny writable shared file mappings at the kernel's mmap_file LSM hook.
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// Linux UAPI values, kept local to avoid pulling glibc headers into BPF target.
#define MMAP_FLAG_TYPE_MASK 0x0fUL
#define MMAP_FLAG_SHARED 0x01UL
#define MMAP_PROT_WRITE 0x02UL
#define MMAP_EACCES 13

char LICENSE[] SEC("license") = "GPL";

SEC("lsm/mmap_file")
int BPF_PROG(deny_shared_writable_mmap, struct file *file,
             unsigned long reqprot, unsigned long prot,
             unsigned long flags, int previous_ret)
{
    (void)file;
    (void)reqprot;

    if (previous_ret != 0)
        return previous_ret;

    if ((flags & MMAP_FLAG_TYPE_MASK) == MMAP_FLAG_SHARED &&
        (prot & MMAP_PROT_WRITE) != 0)
        return -MMAP_EACCES;

    return 0;
}

