/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_LEGACY__
#define __BPF_LEGACY__

#if __GNUC__ && !__clang__
/* Functions to emit BPF_LD_ABS and BPF_LD_IND instructions.  We
 * provide the "standard" names as synonyms of the corresponding GCC
 * builtins. Note how the SKB argument is ignored.
 */
#define load_byte(skb, off) __builtin_bpf_load_byte(off)
#define load_half(skb, off) __builtin_bpf_load_half(off)
#define load_word(skb, off) __builtin_bpf_load_word(off)
#else
/* llvm builtin functions that eBPF C program may use to
 * emit BPF_LD_ABS and BPF_LD_IND instructions
 */
unsigned long long load_byte(void *skb, unsigned long long off) asm("llvm.bpf.load.byte");
unsigned long long load_half(void *skb, unsigned long long off) asm("llvm.bpf.load.half");
unsigned long long load_word(void *skb, unsigned long long off) asm("llvm.bpf.load.word");
#endif

#endif

