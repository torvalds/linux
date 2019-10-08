/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_HELPERS__
#define __BPF_HELPERS__

#include "bpf_helper_defs.h"

#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

/* helper macro to print out debug messages */
#define bpf_printk(fmt, ...)				\
({							\
	char ____fmt[] = fmt;				\
	bpf_trace_printk(____fmt, sizeof(____fmt),	\
			 ##__VA_ARGS__);		\
})

/* helper macro to place programs, maps, license in
 * different sections in elf_bpf file. Section names
 * are interpreted by elf_bpf loader
 */
#define SEC(NAME) __attribute__((section(NAME), used))

/* a helper structure used by eBPF C program
 * to describe BPF map attributes to libbpf loader
 */
struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
};

/*
 * bpf_core_read() abstracts away bpf_probe_read() call and captures offset
 * relocation for source address using __builtin_preserve_access_index()
 * built-in, provided by Clang.
 *
 * __builtin_preserve_access_index() takes as an argument an expression of
 * taking an address of a field within struct/union. It makes compiler emit
 * a relocation, which records BTF type ID describing root struct/union and an
 * accessor string which describes exact embedded field that was used to take
 * an address. See detailed description of this relocation format and
 * semantics in comments to struct bpf_offset_reloc in libbpf_internal.h.
 *
 * This relocation allows libbpf to adjust BPF instruction to use correct
 * actual field offset, based on target kernel BTF type that matches original
 * (local) BTF, used to record relocation.
 */
#define bpf_core_read(dst, sz, src)					    \
	bpf_probe_read(dst, sz,						    \
		       (const void *)__builtin_preserve_access_index(src))

#endif
