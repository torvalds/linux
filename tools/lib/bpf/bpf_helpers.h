/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_HELPERS__
#define __BPF_HELPERS__

#include "bpf_helper_defs.h"

#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

/* Helper macro to print out debug messages */
#define bpf_printk(fmt, ...)				\
({							\
	char ____fmt[] = fmt;				\
	bpf_trace_printk(____fmt, sizeof(____fmt),	\
			 ##__VA_ARGS__);		\
})

/*
 * Helper macro to place programs, maps, license in
 * different sections in elf_bpf file. Section names
 * are interpreted by elf_bpf loader
 */
#define SEC(NAME) __attribute__((section(NAME), used))

#ifndef __always_inline
#define __always_inline __attribute__((always_inline))
#endif

/*
 * Helper structure used by eBPF C program
 * to describe BPF map attributes to libbpf loader
 */
struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
};

enum libbpf_pin_type {
	LIBBPF_PIN_NONE,
	/* PIN_BY_NAME: pin maps by name (in /sys/fs/bpf by default) */
	LIBBPF_PIN_BY_NAME,
};

/* The following types should be used by BPF_PROG_TYPE_TRACING program to
 * access kernel function arguments. BPF trampoline and raw tracepoints
 * typecast arguments to 'unsigned long long'.
 */
typedef int __attribute__((aligned(8))) ks32;
typedef char __attribute__((aligned(8))) ks8;
typedef short __attribute__((aligned(8))) ks16;
typedef long long __attribute__((aligned(8))) ks64;
typedef unsigned int __attribute__((aligned(8))) ku32;
typedef unsigned char __attribute__((aligned(8))) ku8;
typedef unsigned short __attribute__((aligned(8))) ku16;
typedef unsigned long long __attribute__((aligned(8))) ku64;

#endif
