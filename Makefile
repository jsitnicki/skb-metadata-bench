# Build tc-BPF objects (CO-RE) for the loopback benchmark.
#
# Usage: make            - build all bpf/*.bpf.c objects
#        make bpf/cnt.bpf.o
#        make vmlinux_h   - regenerate vmlinux.h from the running kernel's BTF
#
# NOTE: run inside the guest (or on the target kernel), vmlinux.h is
# regenerated from /sys/kernel/btf/vmlinux.

KDIR      ?= $(HOME)/src/linux
SELFTESTS := $(KDIR)/tools/testing/selftests/bpf
BPFTOOL   := $(SELFTESTS)/tools/sbin/bpftool
LIBBPF_INC := $(SELFTESTS)/tools/include
VMLINUX_H := $(SELFTESTS)/tools/include/vmlinux.h

CLANG  ?= clang
CFLAGS := -g -O2 -target bpf -D__TARGET_ARCH_x86 \
          -I$(LIBBPF_INC) -I$(KDIR)/tools/lib

SRCS := $(wildcard bpf/*.bpf.c)
OBJS := $(SRCS:.c=.o)

LIBBPF_A := $(SELFTESTS)/tools/build/libbpf/libbpf.a

all: $(OBJS) attach

# Userspace loader: tc-attach + tracing auto-attach for any variant.
attach: attach.c
	$(CC) -g -O2 -Wall -I$(LIBBPF_INC) -I$(KDIR)/tools/lib \
	      attach.c $(LIBBPF_A) -lelf -lz -lzstd -o $@

# Regenerate vmlinux.h from the BUILT kernel's BTF (the patched
# kernel tree, which carries RHASH, bpf_cast_to_kern_ctx and the
# gated tracepoints). Run after rebuilding the kernel.
vmlinux_h:
	$(BPFTOOL) btf dump file $(KDIR)/vmlinux format c > $(VMLINUX_H)

bpf/%.bpf.o: bpf/%.bpf.c
	$(CLANG) $(CFLAGS) -c $< -o $@
	llvm-objdump -h $@ | grep -E "tc/ingress|tc/egress" || \
		{ echo "warning: no tc sections in $@"; }

clean:
	rm -f $(OBJS)

.PHONY: all vmlinux_h clean
