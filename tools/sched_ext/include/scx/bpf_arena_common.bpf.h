/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#pragma once

#ifndef PAGE_SIZE
#define PAGE_SIZE __PAGE_SIZE
/*
 * for older kernels try sizeof(struct genradix_node)
 * or flexible:
 * static inline long __bpf_page_size(void) {
 *   return bpf_core_enum_value(enum page_size_enum___l, __PAGE_SIZE___l) ?: sizeof(struct genradix_node);
 * }
 * but generated code is not great.
 */
#endif

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) && !defined(BPF_ARENA_FORCE_ASM)
#define __arena __attribute__((address_space(1)))
#define __arena_global __attribute__((address_space(1)))
#define cast_kern(ptr) /* nop for bpf prog. emitted by LLVM */
#define cast_user(ptr) /* nop for bpf prog. emitted by LLVM */
#else

/* emit instruction:
 * rX = rX .off = BPF_ADDR_SPACE_CAST .imm32 = (dst_as << 16) | src_as
 *
 * This is a workaround for LLVM compiler versions without
 * __BPF_FEATURE_ADDR_SPACE_CAST that do not automatically cast between arena
 * pointers and native kernel/userspace ones. In this case we explicitly do so
 * with cast_kern() and cast_user(). E.g., in the Linux kernel tree,
 * tools/testing/selftests/bpf includes tests that use these macros to implement
 * linked lists and hashtables backed by arena memory. In sched_ext, we use
 * cast_kern() and cast_user() for compatibility with older LLVM toolchains.
 */
#ifndef bpf_addr_space_cast
#define bpf_addr_space_cast(var, dst_as, src_as)\
	asm volatile(".byte 0xBF;		\
		     .ifc %[reg], r0;		\
		     .byte 0x00;		\
		     .endif;			\
		     .ifc %[reg], r1;		\
		     .byte 0x11;		\
		     .endif;			\
		     .ifc %[reg], r2;		\
		     .byte 0x22;		\
		     .endif;			\
		     .ifc %[reg], r3;		\
		     .byte 0x33;		\
		     .endif;			\
		     .ifc %[reg], r4;		\
		     .byte 0x44;		\
		     .endif;			\
		     .ifc %[reg], r5;		\
		     .byte 0x55;		\
		     .endif;			\
		     .ifc %[reg], r6;		\
		     .byte 0x66;		\
		     .endif;			\
		     .ifc %[reg], r7;		\
		     .byte 0x77;		\
		     .endif;			\
		     .ifc %[reg], r8;		\
		     .byte 0x88;		\
		     .endif;			\
		     .ifc %[reg], r9;		\
		     .byte 0x99;		\
		     .endif;			\
		     .short %[off];		\
		     .long %[as]"		\
		     : [reg]"+r"(var)		\
		     : [off]"i"(BPF_ADDR_SPACE_CAST) \
		     , [as]"i"((dst_as << 16) | src_as));
#endif

#define __arena
#define __arena_global SEC(".addr_space.1")
#define cast_kern(ptr) bpf_addr_space_cast(ptr, 0, 1)
#define cast_user(ptr) bpf_addr_space_cast(ptr, 1, 0)
#endif

void __arena* bpf_arena_alloc_pages(void *map, void __arena *addr, __u32 page_cnt,
				    int node_id, __u64 flags) __ksym __weak;
void bpf_arena_free_pages(void *map, void __arena *ptr, __u32 page_cnt) __ksym __weak;

/*
 * Note that cond_break can only be portably used in the body of a breakable
 * construct, whereas can_loop can be used anywhere.
 */
#ifdef TEST
#define can_loop true
#define __cond_break(expr) expr
#else
#ifdef __BPF_FEATURE_MAY_GOTO
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#endif /* __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ */
#endif /* __BPF_FEATURE_MAY_GOTO */
#endif /* TEST */

#define cond_break __cond_break(break)
#define cond_break_label(label) __cond_break(goto label)


void bpf_preempt_disable(void) __weak __ksym;
void bpf_preempt_enable(void) __weak __ksym;
