CLANG ?= clang
BPFTOOL ?= $(or $(firstword $(wildcard /usr/lib/linux-tools/*/bpftool)),bpftool)
CC ?= cc

ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')
BPF_CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_$(ARCH) -I.
USER_CFLAGS := -g -O2 -Wall -Wextra

all: exec_guard

vmlinux.h: FORCE
	@if [ ! -s "$@" ]; then \
		$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@; \
	fi

exec_guard.bpf.o: exec_guard.bpf.c vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

exec_guard.skel.h: exec_guard.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

exec_guard: exec_guard.c exec_guard.skel.h
	$(CC) $(USER_CFLAGS) $< -o $@ $$(pkg-config --cflags --libs libbpf) -lelf -lz

clean:
	rm -f exec_guard exec_guard.bpf.o exec_guard.skel.h vmlinux.h

.PHONY: all clean
.PHONY: FORCE
FORCE: