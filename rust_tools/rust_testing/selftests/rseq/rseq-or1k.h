/* SPDX-License-Identifier: LGPL-2.1 OR MIT */

/*
 * Select the instruction "l.nop 0x35" as the RSEQ_SIG.
 */
#define RSEQ_SIG   0x15000035

#define rseq_smp_mb()	__asm__ __volatile__ ("l.msync" ::: "memory")
#define rseq_smp_rmb()	rseq_smp_mb()
#define rseq_smp_wmb()	rseq_smp_mb()
#define RSEQ_ASM_TMP_REG_1	"r31"
#define RSEQ_ASM_TMP_REG_2	"r29"
#define RSEQ_ASM_TMP_REG_3	"r27"
#define RSEQ_ASM_TMP_REG_4	"r25"

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	rseq_unqual_scalar_typeof(*(p)) ____p1 = RSEQ_READ_ONCE(*(p));	\
	rseq_smp_mb();							\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	rseq_smp_mb();							\
	RSEQ_WRITE_ONCE(*(p), v);					\
} while (0)

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,	\
				post_commit_offset, abort_ip)		\
	".pushsection	__rseq_cs, \"aw\"\n"				\
	".balign	32\n"						\
	__rseq_str(label) ":\n"						\
	".long " __rseq_str(version) ", " __rseq_str(flags) "\n"	\
	".long 0x0, " __rseq_str(start_ip) ", "				\
		"0x0, " __rseq_str(post_commit_offset) ", "		\
		"0x0, " __rseq_str(abort_ip) "\n"			\
	".popsection\n\t"						\
	".pushsection __rseq_cs_ptr_array, \"aw\"\n"			\
	".long 0x0, " __rseq_str(label) "b\n"				\
	".popsection\n"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,		 \
				((post_commit_ip) - (start_ip)), abort_ip)

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)			\
	".pushsection __rseq_exit_point_array, \"aw\"\n"		\
	".long 0x0, " __rseq_str(start_ip) ", 0x0, " __rseq_str(exit_ip) "\n"	\
	".popsection\n"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)		\
	RSEQ_INJECT_ASM(1)						\
	"l.movhi " RSEQ_ASM_TMP_REG_1 ", hi(" __rseq_str(cs_label) ")\n"\
	"l.ori   " RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1		\
		", lo(" __rseq_str(cs_label) ")\n"\
	"l.sw  %[" __rseq_str(rseq_cs) "], " RSEQ_ASM_TMP_REG_1 "\n"	\
	__rseq_str(label) ":\n"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)			\
	"l.j 222f\n"							\
	" l.nop\n"							\
	".balign	4\n"						\
	".long "	__rseq_str(RSEQ_SIG) "\n"			\
	__rseq_str(label) ":\n"						\
	"l.j %l[" __rseq_str(abort_label) "]\n"				\
	" l.nop\n"							\
	"222:\n"

#define RSEQ_ASM_OP_STORE(var, value)					\
	"l.sw %[" __rseq_str(var) "], %[" __rseq_str(value) "]\n"

#define RSEQ_ASM_OP_CMPEQ(var, expect, label)				\
	"l.lwz  " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"	\
	"l.sfne " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "]\n"	\
	"l.bf   " __rseq_str(label) "\n"				\
	" l.nop\n"

#define RSEQ_ASM_OP_CMPNE(var, expect, label)				\
	"l.lwz  " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"	\
	"l.sfeq " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "]\n"	\
	"l.bf   " __rseq_str(label) "\n"				\
	" l.nop\n"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)		\
	RSEQ_INJECT_ASM(2)						\
	RSEQ_ASM_OP_CMPEQ(current_cpu_id, cpu_id, label)

#define RSEQ_ASM_OP_R_LOAD(var)						\
	"l.lwz " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_STORE(var)					\
	"l.sw %[" __rseq_str(var) "], " RSEQ_ASM_TMP_REG_1 "\n"

#define RSEQ_ASM_OP_R_LOAD_OFF(offset)					\
	"l.lwz " RSEQ_ASM_TMP_REG_1 ", "				\
		"%[" __rseq_str(offset) "](" RSEQ_ASM_TMP_REG_1 ")\n"

#define RSEQ_ASM_OP_R_ADD(count)					\
	"l.add " RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1		\
		", %[" __rseq_str(count) "]\n"

#define RSEQ_ASM_OP_FINAL_STORE(var, value, post_commit_label)		\
	RSEQ_ASM_OP_STORE(var, value)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_FINAL_STORE_RELEASE(var, value, post_commit_label)	\
	"l.msync\n"							\
	RSEQ_ASM_OP_STORE(var, value)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)		\
	"l.sw %[" __rseq_str(var) "], " RSEQ_ASM_TMP_REG_1 "\n"		\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)				\
	"l.sfeq	%[" __rseq_str(len) "], r0\n"				\
	"l.bf 333f\n"							\
	" l.nop\n"							\
	"l.ori  " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(len) "], 0\n"	\
	"l.ori  " RSEQ_ASM_TMP_REG_2 ", %[" __rseq_str(src) "], 0\n"	\
	"l.ori  " RSEQ_ASM_TMP_REG_3 ", %[" __rseq_str(dst) "], 0\n"	\
	"222:\n"							\
	"l.lbz  " RSEQ_ASM_TMP_REG_4 ", 0(" RSEQ_ASM_TMP_REG_2 ")\n"	\
	"l.sb   0(" RSEQ_ASM_TMP_REG_3 "), " RSEQ_ASM_TMP_REG_4 "\n"	\
	"l.addi " RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1 ", -1\n"	\
	"l.addi " RSEQ_ASM_TMP_REG_2 ", " RSEQ_ASM_TMP_REG_2 ", 1\n"	\
	"l.addi " RSEQ_ASM_TMP_REG_3 ", " RSEQ_ASM_TMP_REG_3 ", 1\n"	\
	"l.sfne " RSEQ_ASM_TMP_REG_1 ", r0\n"				\
	"l.bf 222b\n"							\
	" l.nop\n"							\
	"333:\n"

#define RSEQ_ASM_OP_R_DEREF_ADDV(ptr, off, inc, post_commit_label)	\
	"l.ori  " RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(ptr) "], 0\n"	\
	RSEQ_ASM_OP_R_ADD(off)						\
	"l.lwz  " RSEQ_ASM_TMP_REG_1 ", 0(" RSEQ_ASM_TMP_REG_1 ")\n"	\
	RSEQ_ASM_OP_R_ADD(inc)						\
	__rseq_str(post_commit_label) ":\n"

/* Per-cpu-id indexing. */

#define RSEQ_TEMPLATE_CPU_ID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-or1k-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-or1k-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_CPU_ID

/* Per-mm-cid indexing. */

#define RSEQ_TEMPLATE_MM_CID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-or1k-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-or1k-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_MM_CID

/* APIs which are not based on cpu ids. */

#define RSEQ_TEMPLATE_CPU_ID_NONE
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-or1k-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED
#undef RSEQ_TEMPLATE_CPU_ID_NONE
