CLANG ?= clang
CC ?= cc
PKG_CONFIG ?= pkg-config
BUILD := build
VMLINUX := /sys/kernel/btf/vmlinux
BPF_CFLAGS := -O2 -g -target bpf -D__TARGET_ARCH_x86
LIBBPF_CFLAGS := $(shell $(PKG_CONFIG) --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell $(PKG_CONFIG) --libs libbpf 2>/dev/null)

.PHONY: all clean test check-env
all: $(BUILD)/mmap_guard.bpf.o $(BUILD)/mmap_guard $(BUILD)/mmap_probe $(BUILD)/mmap_race $(BUILD)/mmap_bench

$(BUILD):
	mkdir -p $@

$(BUILD)/mmap_guard.bpf.o: src/mmap_guard.bpf.c | check-env $(BUILD)
	@bpftool btf dump file $(VMLINUX) format c > vmlinux.h || { rm -f vmlinux.h; echo "Could not generate vmlinux.h from kernel BTF" >&2; exit 1; }
	$(CLANG) $(BPF_CFLAGS) $(LIBBPF_CFLAGS) -I. -c $< -o $@

$(BUILD)/mmap_guard: src/mmap_guard.c | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $(LIBBPF_CFLAGS) $< -o $@ $(LIBBPF_LIBS)

$(BUILD)/mmap_probe: tests/mmap_probe.c | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $< -o $@

$(BUILD)/mmap_race: tests/mmap_race.c | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror -pthread $< -o $@

$(BUILD)/mmap_bench: tests/mmap_bench.c | $(BUILD)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $< -o $@

check-env:
	@test -r $(VMLINUX) || (echo "Missing kernel BTF: $(VMLINUX)" >&2; exit 1)
	@$(PKG_CONFIG) --exists libbpf || (echo "Missing libbpf development package" >&2; exit 1)
	@command -v bpftool >/dev/null || (echo "Missing bpftool" >&2; exit 1)

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BUILD) vmlinux.h

