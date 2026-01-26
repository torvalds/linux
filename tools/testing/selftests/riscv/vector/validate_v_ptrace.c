// SPDX-License-Identifier: GPL-2.0-only
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <unistd.h>
#include <errno.h>

#include <linux/ptrace.h>
#include <linux/elf.h>

#include "kselftest_harness.h"
#include "v_helpers.h"

#define SR_FS_DIRTY	0x00006000UL
#define CSR_VXRM_SHIFT	1

volatile unsigned long chld_lock;

TEST(ptrace_v_not_enabled)
{
	pid_t pid;

	if (!(is_vector_supported() || is_xtheadvector_supported()))
		SKIP(return, "Vector not supported");

	chld_lock = 1;
	pid = fork();
	ASSERT_LE(0, pid)
		TH_LOG("fork: %m");

	if (pid == 0) {
		while (chld_lock == 1)
			asm volatile("" : : "g"(chld_lock) : "memory");

		asm volatile ("ebreak" : : : );
	} else {
		struct __riscv_v_regset_state *regset_data;
		unsigned long vlenb = get_vr_len();
		size_t regset_size;
		struct iovec iov;
		int status;
		int ret;

		/* attach */

		ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* unlock */

		ASSERT_EQ(0, ptrace(PTRACE_POKEDATA, pid, &chld_lock, 0));

		/* resume and wait for ebreak */

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* try to read vector registers from the tracee */

		regset_size = sizeof(*regset_data) + vlenb * 32;
		regset_data = calloc(1, regset_size);

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		/* V extension is available, but not yet enabled for the tracee */

		errno = 0;
		ret = ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov);
		ASSERT_EQ(ENODATA, errno);
		ASSERT_EQ(-1, ret);

		/* cleanup */

		ASSERT_EQ(0, kill(pid, SIGKILL));
	}
}

TEST(ptrace_v_early_debug)
{
	static volatile unsigned long vstart;
	static volatile unsigned long vtype;
	static volatile unsigned long vlenb;
	static volatile unsigned long vcsr;
	static volatile unsigned long vl;
	bool xtheadvector;
	pid_t pid;

	if (!(is_vector_supported() || is_xtheadvector_supported()))
		SKIP(return, "Vector not supported");

	xtheadvector = is_xtheadvector_supported();

	chld_lock = 1;
	pid = fork();
	ASSERT_LE(0, pid)
		TH_LOG("fork: %m");

	if (pid == 0) {
		unsigned long vxsat, vxrm;

		vlenb = get_vr_len();

		while (chld_lock == 1)
			asm volatile ("" : : "g"(chld_lock) : "memory");

		asm volatile (
			"csrr %[vstart], vstart\n"
			"csrr %[vtype], vtype\n"
			"csrr %[vl], vl\n"
			: [vtype] "=r"(vtype), [vstart] "=r"(vstart), [vl] "=r"(vl)
			:
			: "memory");

		/* no 'is_xtheadvector_supported()' here to avoid clobbering v-state by syscall */
		if (xtheadvector) {
			asm volatile (
				"csrs sstatus, %[bit]\n"
				"csrr %[vxsat], vxsat\n"
				"csrr %[vxrm], vxrm\n"
				: [vxsat] "=r"(vxsat), [vxrm] "=r"(vxrm)
				: [bit] "r" (SR_FS_DIRTY)
				: "memory");
			vcsr = vxsat | vxrm << CSR_VXRM_SHIFT;
		} else {
			asm volatile (
				"csrr %[vcsr], vcsr\n"
				: [vcsr] "=r"(vcsr)
				:
				: "memory");
		}

		asm volatile (
			".option push\n"
			".option norvc\n"
			"ebreak\n"
			".option pop\n");
	} else {
		struct __riscv_v_regset_state *regset_data;
		unsigned long vstart_csr;
		unsigned long vlenb_csr;
		unsigned long vtype_csr;
		unsigned long vcsr_csr;
		unsigned long vl_csr;
		size_t regset_size;
		struct iovec iov;
		int status;

		/* attach */

		ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* unlock */

		ASSERT_EQ(0, ptrace(PTRACE_POKEDATA, pid, &chld_lock, 0));

		/* resume and wait for ebreak */

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace PEEKDATA */

		errno = 0;
		vstart_csr = ptrace(PTRACE_PEEKDATA, pid, &vstart, NULL);
		ASSERT_FALSE((errno != 0) && (vstart_csr == -1));

		errno = 0;
		vl_csr = ptrace(PTRACE_PEEKDATA, pid, &vl, NULL);
		ASSERT_FALSE((errno != 0) && (vl_csr == -1));

		errno = 0;
		vtype_csr = ptrace(PTRACE_PEEKDATA, pid, &vtype, NULL);
		ASSERT_FALSE((errno != 0) && (vtype_csr == -1));

		errno = 0;
		vcsr_csr = ptrace(PTRACE_PEEKDATA, pid, &vcsr, NULL);
		ASSERT_FALSE((errno != 0) && (vcsr_csr == -1));

		errno = 0;
		vlenb_csr = ptrace(PTRACE_PEEKDATA, pid, &vlenb, NULL);
		ASSERT_FALSE((errno != 0) && (vlenb_csr == -1));

		/* read tracee csr regs using ptrace GETREGSET */

		regset_size = sizeof(*regset_data) + vlenb_csr * 32;
		regset_data = calloc(1, regset_size);

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* compare */

		EXPECT_EQ(vstart_csr, regset_data->vstart);
		EXPECT_EQ(vtype_csr, regset_data->vtype);
		EXPECT_EQ(vlenb_csr, regset_data->vlenb);
		EXPECT_EQ(vcsr_csr, regset_data->vcsr);
		EXPECT_EQ(vl_csr, regset_data->vl);

		/* cleanup */

		ASSERT_EQ(0, kill(pid, SIGKILL));
	}
}

TEST(ptrace_v_syscall_clobbering)
{
	pid_t pid;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vector not supported");

	chld_lock = 1;
	pid = fork();
	ASSERT_LE(0, pid)
		TH_LOG("fork: %m");

	if (pid == 0) {
		unsigned long vl;

		while (chld_lock == 1)
			asm volatile("" : : "g"(chld_lock) : "memory");

		if (is_xtheadvector_supported()) {
			asm volatile (
				// 0 | zimm[10:0] | rs1 | 1 1 1 | rd |1010111| vsetvli
				// vsetvli	t4, x0, e16, m2, d1
				".4byte		0b00000000010100000111111011010111\n"
				"mv		%[new_vl], t4\n"
				: [new_vl] "=r" (vl) : : "t4");
		} else {
			asm volatile (
				".option push\n"
				".option arch, +zve32x\n"
				"vsetvli %[new_vl], x0, e16, m2, tu, mu\n"
				".option pop\n"
				: [new_vl] "=r"(vl) : : );
		}

		while (1) {
			asm volatile (
				".option push\n"
				".option norvc\n"
				"ebreak\n"
				".option pop\n");

			sleep(0);
		}
	} else {
		struct __riscv_v_regset_state *regset_data;
		unsigned long vlenb = get_vr_len();
		struct user_regs_struct regs;
		size_t regset_size;
		struct iovec iov;
		int status;

		/* attach */

		ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* unlock */

		ASSERT_EQ(0, ptrace(PTRACE_POKEDATA, pid, &chld_lock, 0));

		/* resume and wait for the 1st ebreak */

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace GETREGSET */

		regset_size = sizeof(*regset_data) + vlenb * 32;
		regset_data = calloc(1, regset_size);

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify initial vsetvli settings */

		if (is_xtheadvector_supported())
			EXPECT_EQ(5UL, regset_data->vtype);
		else
			EXPECT_EQ(9UL, regset_data->vtype);

		EXPECT_EQ(regset_data->vlenb, regset_data->vl);
		EXPECT_EQ(vlenb, regset_data->vlenb);
		EXPECT_EQ(0UL, regset_data->vstart);
		EXPECT_EQ(0UL, regset_data->vcsr);

		/* skip 1st ebreak, then resume and wait for the 2nd ebreak */

		iov.iov_base = &regs;
		iov.iov_len = sizeof(regs);

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov));
		regs.pc += 4;
		ASSERT_EQ(0, ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov));

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vtype using ptrace GETREGSET */

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify that V state is illegal after syscall */

		EXPECT_EQ((1UL << (__riscv_xlen - 1)), regset_data->vtype);
		EXPECT_EQ(vlenb, regset_data->vlenb);
		EXPECT_EQ(0UL, regset_data->vstart);
		EXPECT_EQ(0UL, regset_data->vcsr);
		EXPECT_EQ(0UL, regset_data->vl);

		/* cleanup */

		ASSERT_EQ(0, kill(pid, SIGKILL));
	}
}

FIXTURE(v_csr_invalid)
{
};

FIXTURE_SETUP(v_csr_invalid)
{
}

FIXTURE_TEARDOWN(v_csr_invalid)
{
}

#define VECTOR_1_0		BIT(0)
#define XTHEAD_VECTOR_0_7	BIT(1)

#define vector_test(x)		((x) & VECTOR_1_0)
#define xthead_test(x)		((x) & XTHEAD_VECTOR_0_7)

/* modifications of the initial vsetvli settings */
FIXTURE_VARIANT(v_csr_invalid)
{
	unsigned long vstart;
	unsigned long vl;
	unsigned long vtype;
	unsigned long vcsr;
	unsigned long vlenb_mul;
	unsigned long vlenb_min;
	unsigned long vlenb_max;
	unsigned long spec;
};

/* unexpected vlenb value */
FIXTURE_VARIANT_ADD(v_csr_invalid, new_vlenb)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x3,
	.vcsr = 0x0,
	.vlenb_mul = 0x2,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0 | XTHEAD_VECTOR_0_7,
};

/* invalid reserved bits in vcsr */
FIXTURE_VARIANT_ADD(v_csr_invalid, vcsr_invalid_reserved_bits)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x3,
	.vcsr = 0x1UL << 8,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0 | XTHEAD_VECTOR_0_7,
};

/* invalid reserved bits in vtype */
FIXTURE_VARIANT_ADD(v_csr_invalid, vtype_invalid_reserved_bits)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = (0x1UL << 8) | 0x3,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0 | XTHEAD_VECTOR_0_7,
};

/* set vill bit */
FIXTURE_VARIANT_ADD(v_csr_invalid, invalid_vill_bit)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = (0x1UL << (__riscv_xlen - 1)) | 0x3,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0 | XTHEAD_VECTOR_0_7,
};

/* reserved vsew value: vsew > 3 */
FIXTURE_VARIANT_ADD(v_csr_invalid, reserved_vsew)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x4UL << 3,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0,
};

/* XTheadVector: unsupported non-zero VEDIV value */
FIXTURE_VARIANT_ADD(v_csr_invalid, reserved_vediv)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x3UL << 5,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = XTHEAD_VECTOR_0_7,
};

/* reserved vlmul value: vlmul == 4 */
FIXTURE_VARIANT_ADD(v_csr_invalid, reserved_vlmul)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x4,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0,
};

/* invalid fractional LMUL for VLEN <= 256: LMUL= 1/8, SEW = 64 */
FIXTURE_VARIANT_ADD(v_csr_invalid, frac_lmul1)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x1d,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x20,
	.spec = VECTOR_1_0,
};

/* invalid integral LMUL for VLEN <= 16: LMUL= 2, SEW = 64 */
FIXTURE_VARIANT_ADD(v_csr_invalid, int_lmul1)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x19,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x2,
	.spec = VECTOR_1_0,
};

/* XTheadVector: invalid integral LMUL for VLEN <= 16: LMUL= 2, SEW = 64 */
FIXTURE_VARIANT_ADD(v_csr_invalid, int_lmul2)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0xd,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x2,
	.spec = XTHEAD_VECTOR_0_7,
};

/* invalid VL for VLEN <= 128: LMUL= 2, SEW = 64, VL = 8 */
FIXTURE_VARIANT_ADD(v_csr_invalid, vl1)
{
	.vstart = 0x0,
	.vl = 0x8,
	.vtype = 0x19,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x10,
	.spec = VECTOR_1_0,
};

/* XTheadVector: invalid VL for VLEN <= 128: LMUL= 2, SEW = 64, VL = 8 */
FIXTURE_VARIANT_ADD(v_csr_invalid, vl2)
{
	.vstart = 0x0,
	.vl = 0x8,
	.vtype = 0xd,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x0,
	.vlenb_max = 0x10,
	.spec = XTHEAD_VECTOR_0_7,
};

TEST_F(v_csr_invalid, ptrace_v_invalid_values)
{
	unsigned long vlenb;
	pid_t pid;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vectors not supported");

	if (is_vector_supported() && !vector_test(variant->spec))
		SKIP(return, "Test not supported for Vector");

	if (is_xtheadvector_supported() && !xthead_test(variant->spec))
		SKIP(return, "Test not supported for XTheadVector");

	vlenb = get_vr_len();

	if (variant->vlenb_min) {
		if (vlenb < variant->vlenb_min)
			SKIP(return, "This test does not support VLEN < %lu\n",
			     variant->vlenb_min * 8);
	}

	if (variant->vlenb_max) {
		if (vlenb > variant->vlenb_max)
			SKIP(return, "This test does not support VLEN > %lu\n",
			     variant->vlenb_max * 8);
	}

	chld_lock = 1;
	pid = fork();
	ASSERT_LE(0, pid)
		TH_LOG("fork: %m");

	if (pid == 0) {
		unsigned long vl;

		while (chld_lock == 1)
			asm volatile("" : : "g"(chld_lock) : "memory");

		if (is_xtheadvector_supported()) {
			asm volatile (
				// 0 | zimm[10:0] | rs1 | 1 1 1 | rd |1010111| vsetvli
				// vsetvli	t4, x0, e16, m2, d1
				".4byte		0b00000000010100000111111011010111\n"
				"mv		%[new_vl], t4\n"
				: [new_vl] "=r" (vl) : : "t4");
		} else {
			asm volatile (
				".option push\n"
				".option arch, +zve32x\n"
				"vsetvli %[new_vl], x0, e16, m2, tu, mu\n"
				".option pop\n"
				: [new_vl] "=r"(vl) : : );
		}

		while (1) {
			asm volatile (
				".option push\n"
				".option norvc\n"
				"ebreak\n"
				"nop\n"
				".option pop\n");
		}
	} else {
		struct __riscv_v_regset_state *regset_data;
		size_t regset_size;
		struct iovec iov;
		int status;
		int ret;

		/* attach */

		ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* unlock */

		ASSERT_EQ(0, ptrace(PTRACE_POKEDATA, pid, &chld_lock, 0));

		/* resume and wait for the 1st ebreak */

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace GETREGSET */

		regset_size = sizeof(*regset_data) + vlenb * 32;
		regset_data = calloc(1, regset_size);

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify initial vsetvli settings */

		if (is_xtheadvector_supported())
			EXPECT_EQ(5UL, regset_data->vtype);
		else
			EXPECT_EQ(9UL, regset_data->vtype);

		EXPECT_EQ(regset_data->vlenb, regset_data->vl);
		EXPECT_EQ(vlenb, regset_data->vlenb);
		EXPECT_EQ(0UL, regset_data->vstart);
		EXPECT_EQ(0UL, regset_data->vcsr);

		/* apply invalid settings from fixture variants */

		regset_data->vlenb *= variant->vlenb_mul;
		regset_data->vstart = variant->vstart;
		regset_data->vtype = variant->vtype;
		regset_data->vcsr = variant->vcsr;
		regset_data->vl = variant->vl;

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		errno = 0;
		ret = ptrace(PTRACE_SETREGSET, pid, NT_RISCV_VECTOR, &iov);
		ASSERT_EQ(errno, EINVAL);
		ASSERT_EQ(ret, -1);

		/* cleanup */

		ASSERT_EQ(0, kill(pid, SIGKILL));
	}
}

FIXTURE(v_csr_valid)
{
};

FIXTURE_SETUP(v_csr_valid)
{
}

FIXTURE_TEARDOWN(v_csr_valid)
{
}

/* modifications of the initial vsetvli settings */
FIXTURE_VARIANT(v_csr_valid)
{
	unsigned long vstart;
	unsigned long vl;
	unsigned long vtype;
	unsigned long vcsr;
	unsigned long vlenb_mul;
	unsigned long vlenb_min;
	unsigned long vlenb_max;
	unsigned long spec;
};

/* valid for VLEN >= 128: LMUL= 1/4, SEW = 32 */
FIXTURE_VARIANT_ADD(v_csr_valid, frac_lmul1)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x16,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x10,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0,
};

/* valid for VLEN >= 16: LMUL= 2, SEW = 32 */
FIXTURE_VARIANT_ADD(v_csr_valid, int_lmul1)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x11,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x2,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0,
};

/* valid for XTheadVector VLEN >= 16: LMUL= 2, SEW = 32 */
FIXTURE_VARIANT_ADD(v_csr_valid, int_lmul2)
{
	.vstart = 0x0,
	.vl = 0x0,
	.vtype = 0x9,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x2,
	.vlenb_max = 0x0,
	.spec = XTHEAD_VECTOR_0_7,
};

/* valid for VLEN >= 32: LMUL= 2, SEW = 32, VL = 2 */
FIXTURE_VARIANT_ADD(v_csr_valid, int_lmul3)
{
	.vstart = 0x0,
	.vl = 0x2,
	.vtype = 0x11,
	.vcsr = 0x0,
	.vlenb_mul = 0x1,
	.vlenb_min = 0x4,
	.vlenb_max = 0x0,
	.spec = VECTOR_1_0,
};

TEST_F(v_csr_valid, ptrace_v_valid_values)
{
	unsigned long vlenb;
	pid_t pid;

	if (!is_vector_supported() && !is_xtheadvector_supported())
		SKIP(return, "Vectors not supported");

	if (is_vector_supported() && !vector_test(variant->spec))
		SKIP(return, "Test not supported for Vector");

	if (is_xtheadvector_supported() && !xthead_test(variant->spec))
		SKIP(return, "Test not supported for XTheadVector");

	vlenb = get_vr_len();

	if (variant->vlenb_min) {
		if (vlenb < variant->vlenb_min)
			SKIP(return, "This test does not support VLEN < %lu\n",
			     variant->vlenb_min * 8);
	}
	if (variant->vlenb_max) {
		if (vlenb > variant->vlenb_max)
			SKIP(return, "This test does not support VLEN > %lu\n",
			     variant->vlenb_max * 8);
	}

	chld_lock = 1;
	pid = fork();
	ASSERT_LE(0, pid)
		TH_LOG("fork: %m");

	if (pid == 0) {
		unsigned long vl;

		while (chld_lock == 1)
			asm volatile("" : : "g"(chld_lock) : "memory");

		if (is_xtheadvector_supported()) {
			asm volatile (
				// 0 | zimm[10:0] | rs1 | 1 1 1 | rd |1010111| vsetvli
				// vsetvli	t4, x0, e16, m2, d1
				".4byte		0b00000000010100000111111011010111\n"
				"mv		%[new_vl], t4\n"
				: [new_vl] "=r" (vl) : : "t4");
		} else {
			asm volatile (
				".option push\n"
				".option arch, +zve32x\n"
				"vsetvli %[new_vl], x0, e16, m2, tu, mu\n"
				".option pop\n"
				: [new_vl] "=r"(vl) : : );
		}

		asm volatile (
			".option push\n"
			".option norvc\n"
			".option arch, +zve32x\n"
			"ebreak\n" /* breakpoint 1: apply new V state using ptrace */
			"nop\n"
			"ebreak\n" /* breakpoint 2: V state clean - context will not be saved */
			"vmv.v.i v0, -1\n"
			"ebreak\n" /* breakpoint 3: V state dirty - context will be saved */
			".option pop\n");
	} else {
		struct __riscv_v_regset_state *regset_data;
		struct user_regs_struct regs;
		size_t regset_size;
		struct iovec iov;
		int status;

		/* attach */

		ASSERT_EQ(0, ptrace(PTRACE_ATTACH, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* unlock */

		ASSERT_EQ(0, ptrace(PTRACE_POKEDATA, pid, &chld_lock, 0));

		/* resume and wait for the 1st ebreak */

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace GETREGSET */

		regset_size = sizeof(*regset_data) + vlenb * 32;
		regset_data = calloc(1, regset_size);

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify initial vsetvli settings */

		if (is_xtheadvector_supported())
			EXPECT_EQ(5UL, regset_data->vtype);
		else
			EXPECT_EQ(9UL, regset_data->vtype);

		EXPECT_EQ(regset_data->vlenb, regset_data->vl);
		EXPECT_EQ(vlenb, regset_data->vlenb);
		EXPECT_EQ(0UL, regset_data->vstart);
		EXPECT_EQ(0UL, regset_data->vcsr);

		/* apply valid settings from fixture variants */

		regset_data->vlenb *= variant->vlenb_mul;
		regset_data->vstart = variant->vstart;
		regset_data->vtype = variant->vtype;
		regset_data->vcsr = variant->vcsr;
		regset_data->vl = variant->vl;

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_SETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* skip 1st ebreak, then resume and wait for the 2nd ebreak */

		iov.iov_base = &regs;
		iov.iov_len = sizeof(regs);

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov));
		regs.pc += 4;
		ASSERT_EQ(0, ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov));

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace GETREGSET */

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify vector csr regs from tracee context */

		EXPECT_EQ(regset_data->vstart, variant->vstart);
		EXPECT_EQ(regset_data->vtype, variant->vtype);
		EXPECT_EQ(regset_data->vcsr, variant->vcsr);
		EXPECT_EQ(regset_data->vl, variant->vl);
		EXPECT_EQ(regset_data->vlenb, vlenb);

		/* skip 2nd ebreak, then resume and wait for the 3rd ebreak */

		iov.iov_base = &regs;
		iov.iov_len = sizeof(regs);

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov));
		regs.pc += 4;
		ASSERT_EQ(0, ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov));

		ASSERT_EQ(0, ptrace(PTRACE_CONT, pid, NULL, NULL));
		ASSERT_EQ(pid, waitpid(pid, &status, 0));
		ASSERT_TRUE(WIFSTOPPED(status));

		/* read tracee vector csr regs using ptrace GETREGSET */

		iov.iov_base = regset_data;
		iov.iov_len = regset_size;

		ASSERT_EQ(0, ptrace(PTRACE_GETREGSET, pid, NT_RISCV_VECTOR, &iov));

		/* verify vector csr regs from tracee context */

		EXPECT_EQ(regset_data->vstart, variant->vstart);
		EXPECT_EQ(regset_data->vtype, variant->vtype);
		EXPECT_EQ(regset_data->vcsr, variant->vcsr);
		EXPECT_EQ(regset_data->vl, variant->vl);
		EXPECT_EQ(regset_data->vlenb, vlenb);

		/* cleanup */

		ASSERT_EQ(0, kill(pid, SIGKILL));
	}
}

TEST_HARNESS_MAIN
