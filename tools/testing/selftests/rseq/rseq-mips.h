/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Author: Paul Burton <paul.burton@mips.com>
 * (C) Copyright 2018 MIPS Tech LLC
 * (C) Copyright 2016-2022 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

/*
 * RSEQ_SIG uses the break instruction. The instruction pattern is:
 *
 * On MIPS:
 *	0350000d        break     0x350
 *
 * On nanoMIPS:
 *      00100350        break     0x350
 *
 * On microMIPS:
 *      0000d407        break     0x350
 *
 * For nanoMIPS32 and microMIPS, the instruction stream is encoded as 16-bit
 * halfwords, so the signature halfwords need to be swapped accordingly for
 * little-endian.
 */
#if defined(__nanomips__)
# ifdef __MIPSEL__
#  define RSEQ_SIG	0x03500010
# else
#  define RSEQ_SIG	0x00100350
# endif
#elif defined(__mips_micromips)
# ifdef __MIPSEL__
#  define RSEQ_SIG	0xd4070000
# else
#  define RSEQ_SIG	0x0000d407
# endif
#elif defined(__mips__)
# define RSEQ_SIG	0x0350000d
#else
/* Unknown MIPS architecture. */
#endif

#define rseq_smp_mb()	__asm__ __volatile__ ("sync" ::: "memory")
#define rseq_smp_rmb()	rseq_smp_mb()
#define rseq_smp_wmb()	rseq_smp_mb()

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	__typeof(*p) ____p1 = RSEQ_READ_ONCE(*p);			\
	rseq_smp_mb();							\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	rseq_smp_mb();							\
	RSEQ_WRITE_ONCE(*p, v);						\
} while (0)

#if _MIPS_SZLONG == 64
# define LONG			".dword"
# define LONG_LA		"dla"
# define LONG_L			"ld"
# define LONG_S			"sd"
# define LONG_ADDI		"daddiu"
# define U32_U64_PAD(x)		x
#elif _MIPS_SZLONG == 32
# define LONG			".word"
# define LONG_LA		"la"
# define LONG_L			"lw"
# define LONG_S			"sw"
# define LONG_ADDI		"addiu"
# ifdef __BIG_ENDIAN
#  define U32_U64_PAD(x)	"0x0, " x
# else
#  define U32_U64_PAD(x)	x ", 0x0"
# endif
#else
# error unsupported _MIPS_SZLONG
#endif

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip, \
				post_commit_offset, abort_ip) \
		".pushsection __rseq_cs, \"aw\"\n\t" \
		".balign 32\n\t" \
		__rseq_str(label) ":\n\t"					\
		".word " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(start_ip)) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(post_commit_offset)) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(abort_ip)) "\n\t" \
		".popsection\n\t" \
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(label) "b") "\n\t" \
		".popsection\n\t"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip, \
				(post_commit_ip - start_ip), abort_ip)

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip) \
		".pushsection __rseq_exit_point_array, \"aw\"\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(start_ip)) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(exit_ip)) "\n\t" \
		".popsection\n\t"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs) \
		RSEQ_INJECT_ASM(1) \
		LONG_LA " $4, " __rseq_str(cs_label) "\n\t" \
		LONG_S  " $4, %[" __rseq_str(rseq_cs) "]\n\t" \
		__rseq_str(label) ":\n\t"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label) \
		RSEQ_INJECT_ASM(2) \
		"lw  $4, %[" __rseq_str(current_cpu_id) "]\n\t" \
		"bne $4, %[" __rseq_str(cpu_id) "], " __rseq_str(label) "\n\t"

#define __RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown, \
				abort_label, version, flags, \
				start_ip, post_commit_offset, abort_ip) \
		".balign 32\n\t" \
		__rseq_str(table_label) ":\n\t" \
		".word " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(start_ip)) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(post_commit_offset)) "\n\t" \
		LONG " " U32_U64_PAD(__rseq_str(abort_ip)) "\n\t" \
		".word " __rseq_str(RSEQ_SIG) "\n\t" \
		__rseq_str(label) ":\n\t" \
		teardown \
		"b %l[" __rseq_str(abort_label) "]\n\t"

#define RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown, abort_label, \
			      start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown, \
				abort_label, 0x0, 0x0, start_ip, \
				(post_commit_ip - start_ip), abort_ip)

#define RSEQ_ASM_DEFINE_CMPFAIL(label, teardown, cmpfail_label) \
		__rseq_str(label) ":\n\t" \
		teardown \
		"b %l[" __rseq_str(cmpfail_label) "]\n\t"

/* Per-cpu-id indexing. */

#define RSEQ_TEMPLATE_CPU_ID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-mips-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-mips-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_CPU_ID

/* Per-mm-cid indexing. */

#define RSEQ_TEMPLATE_MM_CID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-mips-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-mips-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_MM_CID

/* APIs which are not based on cpu ids. */

#define RSEQ_TEMPLATE_CPU_ID_NONE
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-mips-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED
#undef RSEQ_TEMPLATE_CPU_ID_NONE
