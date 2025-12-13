/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Select the instruction "csrw mhartid, x0" as the RSEQ_SIG. Unlike
 * other architectures, the ebreak instruction has no immediate field for
 * distinguishing purposes. Hence, ebreak is not suitable as RSEQ_SIG.
 * "csrw mhartid, x0" can also satisfy the RSEQ requirement because it
 * is an uncommon instruction and will raise an illegal instruction
 * exception when executed in all modes.
 */
#include <endian.h>
#include <asm/fence.h>

#if defined(__BYTE_ORDER) ? (__BYTE_ORDER == __LITTLE_ENDIAN) : defined(__LITTLE_ENDIAN)
#define RSEQ_SIG   0xf1401073  /* csrr mhartid, x0 */
#else
#error "Currently, RSEQ only supports Little-Endian version"
#endif

#if __riscv_xlen == 64
#define __REG_SEL(a, b)	a
#elif __riscv_xlen == 32
#define __REG_SEL(a, b)	b
#endif

#define REG_L	__REG_SEL("ld ", "lw ")
#define REG_S	__REG_SEL("sd ", "sw ")

#define rseq_smp_mb()	RISCV_FENCE(rw, rw)
#define rseq_smp_rmb()	RISCV_FENCE(r, r)
#define rseq_smp_wmb()	RISCV_FENCE(w, w)
#define RSEQ_ASM_TMP_REG_1	"t6"
#define RSEQ_ASM_TMP_REG_2	"t5"
#define RSEQ_ASM_TMP_REG_3	"t4"
#define RSEQ_ASM_TMP_REG_4	"t3"

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	rseq_unqual_scalar_typeof(*(p)) ____p1 = RSEQ_READ_ONCE(*(p));	\
	RISCV_FENCE(r, rw);						\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	RISCV_FENCE(rw, w);						\
	RSEQ_WRITE_ONCE(*(p), v);					\
} while (0)

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,	\
				post_commit_offset, abort_ip)		\
	".pushsection	__rseq_cs, \"aw\"\n"				\
	".balign	32\n"						\
	__rseq_str(label) ":\n"						\
	".long	" __rseq_str(version) ", " __rseq_str(flags) "\n"	\
	".quad	" __rseq_str(start_ip) ", "				\
			  __rseq_str(post_commit_offset) ", "		\
			  __rseq_str(abort_ip) "\n"			\
	".popsection\n\t"						\
	".pushsection __rseq_cs_ptr_array, \"aw\"\n"			\
	".quad " __rseq_str(label) "b\n"				\
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
	".quad " __rseq_str(start_ip) ", " __rseq_str(exit_ip) "\n"	\
	".popsection\n"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)		\
	RSEQ_INJECT_ASM(1)						\
	"la	" RSEQ_ASM_TMP_REG_1 ", " __rseq_str(cs_label) "\n"	\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(rseq_cs) "]\n"	\
	__rseq_str(label) ":\n"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)			\
	"j	222f\n"							\
	".balign	4\n"						\
	".long "	__rseq_str(RSEQ_SIG) "\n"			\
	__rseq_str(label) ":\n"						\
	"j	%l[" __rseq_str(abort_label) "]\n"			\
	"222:\n"

#define RSEQ_ASM_OP_STORE(value, var)					\
	REG_S	"%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_CMPEQ(var, expect, label)				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	"bne	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPEQ32(var, expect, label)				\
	"lw	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"	\
	"bne	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPNE(var, expect, label)				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	"beq	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)		\
	RSEQ_INJECT_ASM(2)						\
	RSEQ_ASM_OP_CMPEQ32(current_cpu_id, cpu_id, label)

#define RSEQ_ASM_OP_R_LOAD(var)						\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_STORE(var)					\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_LOAD_OFF(offset)					\
	"add	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(offset) "], "	\
		 RSEQ_ASM_TMP_REG_1 "\n"				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", (" RSEQ_ASM_TMP_REG_1 ")\n"

#define RSEQ_ASM_OP_R_ADD(count)					\
	"add	" RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1		\
		", %[" __rseq_str(count) "]\n"

#define RSEQ_ASM_OP_FINAL_STORE(value, var, post_commit_label)		\
	RSEQ_ASM_OP_STORE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_FINAL_STORE_RELEASE(value, var, post_commit_label)	\
	"fence	rw, w\n"						\
	RSEQ_ASM_OP_STORE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)		\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)				\
	"beqz	%[" __rseq_str(len) "], 333f\n"				\
	"mv	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(len) "]\n"	\
	"mv	" RSEQ_ASM_TMP_REG_2 ", %[" __rseq_str(src) "]\n"	\
	"mv	" RSEQ_ASM_TMP_REG_3 ", %[" __rseq_str(dst) "]\n"	\
	"222:\n"							\
	"lb	" RSEQ_ASM_TMP_REG_4 ", 0(" RSEQ_ASM_TMP_REG_2 ")\n"	\
	"sb	" RSEQ_ASM_TMP_REG_4 ", 0(" RSEQ_ASM_TMP_REG_3 ")\n"	\
	"addi	" RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1 ", -1\n"	\
	"addi	" RSEQ_ASM_TMP_REG_2 ", " RSEQ_ASM_TMP_REG_2 ", 1\n"	\
	"addi	" RSEQ_ASM_TMP_REG_3 ", " RSEQ_ASM_TMP_REG_3 ", 1\n"	\
	"bnez	" RSEQ_ASM_TMP_REG_1 ", 222b\n"				\
	"333:\n"

#define RSEQ_ASM_OP_R_DEREF_ADDV(ptr, off, inc, post_commit_label)	\
	"mv	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(ptr) "]\n"	\
	RSEQ_ASM_OP_R_ADD(off)						\
	REG_L	  RSEQ_ASM_TMP_REG_1 ", 0(" RSEQ_ASM_TMP_REG_1 ")\n"	\
	RSEQ_ASM_OP_R_ADD(inc)						\
	__rseq_str(post_commit_label) ":\n"

/* Per-cpu-id indexing. */

#define RSEQ_TEMPLATE_CPU_ID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-riscv-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-riscv-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_CPU_ID

/* Per-mm-cid indexing. */

#define RSEQ_TEMPLATE_MM_CID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-riscv-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-riscv-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_MM_CID

/* APIs which are not based on cpu ids. */

#define RSEQ_TEMPLATE_CPU_ID_NONE
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-riscv-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED
#undef RSEQ_TEMPLATE_CPU_ID_NONE
