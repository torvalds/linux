/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-arm64.h
 *
 * (C) Copyright 2016-2022 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * (C) Copyright 2018 - Will Deacon <will.deacon@arm.com>
 */

/*
 * aarch64 -mbig-endian generates mixed endianness code vs data:
 * little-endian code and big-endian data. Ensure the RSEQ_SIG signature
 * matches code endianness.
 */
#define RSEQ_SIG_CODE	0xd428bc00	/* BRK #0x45E0.  */

#ifdef __AARCH64EB__
#define RSEQ_SIG_DATA	0x00bc28d4	/* BRK #0x45E0.  */
#else
#define RSEQ_SIG_DATA	RSEQ_SIG_CODE
#endif

#define RSEQ_SIG	RSEQ_SIG_DATA

#define rseq_smp_mb()	__asm__ __volatile__ ("dmb ish" ::: "memory")
#define rseq_smp_rmb()	__asm__ __volatile__ ("dmb ishld" ::: "memory")
#define rseq_smp_wmb()	__asm__ __volatile__ ("dmb ishst" ::: "memory")

#define rseq_smp_load_acquire(p)						\
__extension__ ({								\
	__typeof(*p) ____p1;							\
	switch (sizeof(*p)) {							\
	case 1:									\
		asm volatile ("ldarb %w0, %1"					\
			: "=r" (*(__u8 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 2:									\
		asm volatile ("ldarh %w0, %1"					\
			: "=r" (*(__u16 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 4:									\
		asm volatile ("ldar %w0, %1"					\
			: "=r" (*(__u32 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 8:									\
		asm volatile ("ldar %0, %1"					\
			: "=r" (*(__u64 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	}									\
	____p1;									\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)						\
do {										\
	switch (sizeof(*p)) {							\
	case 1:									\
		asm volatile ("stlrb %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u8)v)					\
				: "memory");					\
		break;								\
	case 2:									\
		asm volatile ("stlrh %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u16)v)				\
				: "memory");					\
		break;								\
	case 4:									\
		asm volatile ("stlr %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u32)v)				\
				: "memory");					\
		break;								\
	case 8:									\
		asm volatile ("stlr %1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u64)v)				\
				: "memory");					\
		break;								\
	}									\
} while (0)

#define RSEQ_ASM_TMP_REG32	"w15"
#define RSEQ_ASM_TMP_REG	"x15"
#define RSEQ_ASM_TMP_REG_2	"x14"

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,		\
				post_commit_offset, abort_ip)			\
	"	.pushsection	__rseq_cs, \"aw\"\n"				\
	"	.balign	32\n"							\
	__rseq_str(label) ":\n"							\
	"	.long	" __rseq_str(version) ", " __rseq_str(flags) "\n"	\
	"	.quad	" __rseq_str(start_ip) ", "				\
			  __rseq_str(post_commit_offset) ", "			\
			  __rseq_str(abort_ip) "\n"				\
	"	.popsection\n\t"						\
	"	.pushsection __rseq_cs_ptr_array, \"aw\"\n"				\
	"	.quad " __rseq_str(label) "b\n"					\
	"	.popsection\n"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip)	\
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,			\
				(post_commit_ip - start_ip), abort_ip)

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)				\
	"	.pushsection __rseq_exit_point_array, \"aw\"\n"			\
	"	.quad " __rseq_str(start_ip) ", " __rseq_str(exit_ip) "\n"	\
	"	.popsection\n"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)			\
	RSEQ_INJECT_ASM(1)							\
	"	adrp	" RSEQ_ASM_TMP_REG ", " __rseq_str(cs_label) "\n"	\
	"	add	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", :lo12:" __rseq_str(cs_label) "\n"			\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(rseq_cs) "]\n"	\
	__rseq_str(label) ":\n"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)				\
	"	b	222f\n"							\
	"	.inst 	"	__rseq_str(RSEQ_SIG_CODE) "\n"			\
	__rseq_str(label) ":\n"							\
	"	b	%l[" __rseq_str(abort_label) "]\n"			\
	"222:\n"

#define RSEQ_ASM_OP_STORE(value, var)						\
	"	str	%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_STORE_RELEASE(value, var)					\
	"	stlr	%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_FINAL_STORE(value, var, post_commit_label)			\
	RSEQ_ASM_OP_STORE(value, var)						\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_FINAL_STORE_RELEASE(value, var, post_commit_label)		\
	RSEQ_ASM_OP_STORE_RELEASE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_CMPEQ(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	"	sub	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(expect) "]\n"				\
	"	cbnz	" RSEQ_ASM_TMP_REG ", " __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPEQ32(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG32 ", %[" __rseq_str(var) "]\n"	\
	"	sub	" RSEQ_ASM_TMP_REG32 ", " RSEQ_ASM_TMP_REG32		\
			", %w[" __rseq_str(expect) "]\n"			\
	"	cbnz	" RSEQ_ASM_TMP_REG32 ", " __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPNE(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	"	sub	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(expect) "]\n"				\
	"	cbz	" RSEQ_ASM_TMP_REG ", " __rseq_str(label) "\n"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)			\
	RSEQ_INJECT_ASM(2)							\
	RSEQ_ASM_OP_CMPEQ32(current_cpu_id, cpu_id, label)

#define RSEQ_ASM_OP_R_LOAD(var)							\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_STORE(var)						\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_LOAD_OFF(offset)						\
	"	ldr	" RSEQ_ASM_TMP_REG ", [" RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(offset) "]]\n"

#define RSEQ_ASM_OP_R_ADD(count)						\
	"	add	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(count) "]\n"

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)			\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)					\
	"	cbz	%[" __rseq_str(len) "], 333f\n"				\
	"	mov	" RSEQ_ASM_TMP_REG_2 ", %[" __rseq_str(len) "]\n"	\
	"222:	sub	" RSEQ_ASM_TMP_REG_2 ", " RSEQ_ASM_TMP_REG_2 ", #1\n"	\
	"	ldrb	" RSEQ_ASM_TMP_REG32 ", [%[" __rseq_str(src) "]"	\
			", " RSEQ_ASM_TMP_REG_2 "]\n"				\
	"	strb	" RSEQ_ASM_TMP_REG32 ", [%[" __rseq_str(dst) "]"	\
			", " RSEQ_ASM_TMP_REG_2 "]\n"				\
	"	cbnz	" RSEQ_ASM_TMP_REG_2 ", 222b\n"				\
	"333:\n"

/* Per-cpu-id indexing. */

#define RSEQ_TEMPLATE_CPU_ID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-arm64-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-arm64-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_CPU_ID

/* Per-mm-cid indexing. */

#define RSEQ_TEMPLATE_MM_CID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-arm64-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-arm64-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_MM_CID

/* APIs which are not based on cpu ids. */

#define RSEQ_TEMPLATE_CPU_ID_NONE
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-arm64-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED
#undef RSEQ_TEMPLATE_CPU_ID_NONE
