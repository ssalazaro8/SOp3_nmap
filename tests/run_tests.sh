#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
guard_pid=
cleanup() {
    if [ -n "$guard_pid" ]; then
        kill -TERM "$guard_pid" 2>/dev/null || true
        wait "$guard_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT HUP INT TERM

printf 'X' > "$tmp/sample.dat"
if [ "$(id -u)" -eq 0 ]; then
    "$root/build/mmap_guard" "$root/build/mmap_guard.bpf.o" >"$tmp/guard.log" 2>&1 &
else
    if ! sudo -v; then
        echo "SKIP: sudo authorization is required to load the BPF LSM policy" >&2
        exit 77
    fi
    sudo -n "$root/build/mmap_guard" "$root/build/mmap_guard.bpf.o" >"$tmp/guard.log" 2>&1 &
fi
guard_pid=$!

attempt=0
while [ ! -s "$tmp/guard.log" ] && [ "$attempt" -lt 50 ]; do
    sleep 0.1
    attempt=$((attempt + 1))
done
if ! kill -0 "$guard_pid" 2>/dev/null; then
    cat "$tmp/guard.log" >&2
    echo "SKIP: could not activate BPF LSM policy (check kernel config and privileges)" >&2
    exit 77
fi

"$root/build/mmap_probe" "$tmp/sample.dat"
"$root/build/mmap_race"

