/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_LEGACY__
#define __BPF_LEGACY__

#define BPF_ANNOTATE_KV_PAIR(name, type_key, type_val)		\
	struct ____btf_map_##name {				\
		type_key key;					\
		type_val value;					\
	};							\
	struct ____btf_map_##name				\
	__attribute__ ((section(".maps." #name), used))		\
		____btf_map_##name = { }

/* llvm builtin functions that eBPF C program may use to
 * emit BPF_LD_ABS and BPF_LD_IND instructions
 */
unsigned long long load_byte(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.byte");
unsigned long long load_half(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.half");
unsigned long long load_word(void *skb,
			     unsigned long long off) asm("llvm.bpf.load.word");

#endif

