/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RISC-V SBI specific definitions
 *
 * Copyright (C) 2024 Rivos Inc.
 */

#ifndef SELFTEST_KVM_SBI_H
#define SELFTEST_KVM_SBI_H

/* SBI spec version fields */
#define SBI_SPEC_VERSION_DEFAULT	0x1
#define SBI_SPEC_VERSION_MAJOR_SHIFT	24
#define SBI_SPEC_VERSION_MAJOR_MASK	0x7f
#define SBI_SPEC_VERSION_MINOR_MASK	0xffffff

/* SBI return error codes */
#define SBI_SUCCESS				 0
#define SBI_ERR_FAILURE				-1
#define SBI_ERR_NOT_SUPPORTED			-2
#define SBI_ERR_INVALID_PARAM			-3
#define SBI_ERR_DENIED				-4
#define SBI_ERR_INVALID_ADDRESS			-5
#define SBI_ERR_ALREADY_AVAILABLE		-6
#define SBI_ERR_ALREADY_STARTED			-7
#define SBI_ERR_ALREADY_STOPPED			-8

#define SBI_EXT_EXPERIMENTAL_START		0x08000000
#define SBI_EXT_EXPERIMENTAL_END		0x08FFFFFF

#define KVM_RISCV_SELFTESTS_SBI_EXT		SBI_EXT_EXPERIMENTAL_END
#define KVM_RISCV_SELFTESTS_SBI_UCALL		0
#define KVM_RISCV_SELFTESTS_SBI_UNEXP		1

enum sbi_ext_id {
	SBI_EXT_BASE = 0x10,
	SBI_EXT_STA = 0x535441,
	SBI_EXT_PMU = 0x504D55,
};

enum sbi_ext_base_fid {
	SBI_EXT_BASE_GET_SPEC_VERSION = 0,
	SBI_EXT_BASE_GET_IMP_ID,
	SBI_EXT_BASE_GET_IMP_VERSION,
	SBI_EXT_BASE_PROBE_EXT = 3,
};
enum sbi_ext_pmu_fid {
	SBI_EXT_PMU_NUM_COUNTERS = 0,
	SBI_EXT_PMU_COUNTER_GET_INFO,
	SBI_EXT_PMU_COUNTER_CFG_MATCH,
	SBI_EXT_PMU_COUNTER_START,
	SBI_EXT_PMU_COUNTER_STOP,
	SBI_EXT_PMU_COUNTER_FW_READ,
	SBI_EXT_PMU_COUNTER_FW_READ_HI,
	SBI_EXT_PMU_SNAPSHOT_SET_SHMEM,
};

union sbi_pmu_ctr_info {
	unsigned long value;
	struct {
		unsigned long csr:12;
		unsigned long width:6;
#if __riscv_xlen == 32
		unsigned long reserved:13;
#else
		unsigned long reserved:45;
#endif
		unsigned long type:1;
	};
};

struct riscv_pmu_snapshot_data {
	u64 ctr_overflow_mask;
	u64 ctr_values[64];
	u64 reserved[447];
};

struct sbiret {
	long error;
	long value;
};

/** General pmu event codes specified in SBI PMU extension */
enum sbi_pmu_hw_generic_events_t {
	SBI_PMU_HW_NO_EVENT			= 0,
	SBI_PMU_HW_CPU_CYCLES			= 1,
	SBI_PMU_HW_INSTRUCTIONS			= 2,
	SBI_PMU_HW_CACHE_REFERENCES		= 3,
	SBI_PMU_HW_CACHE_MISSES			= 4,
	SBI_PMU_HW_BRANCH_INSTRUCTIONS		= 5,
	SBI_PMU_HW_BRANCH_MISSES		= 6,
	SBI_PMU_HW_BUS_CYCLES			= 7,
	SBI_PMU_HW_STALLED_CYCLES_FRONTEND	= 8,
	SBI_PMU_HW_STALLED_CYCLES_BACKEND	= 9,
	SBI_PMU_HW_REF_CPU_CYCLES		= 10,

	SBI_PMU_HW_GENERAL_MAX,
};

/* SBI PMU counter types */
enum sbi_pmu_ctr_type {
	SBI_PMU_CTR_TYPE_HW = 0x0,
	SBI_PMU_CTR_TYPE_FW,
};

/* Flags defined for config matching function */
#define SBI_PMU_CFG_FLAG_SKIP_MATCH	BIT(0)
#define SBI_PMU_CFG_FLAG_CLEAR_VALUE	BIT(1)
#define SBI_PMU_CFG_FLAG_AUTO_START	BIT(2)
#define SBI_PMU_CFG_FLAG_SET_VUINH	BIT(3)
#define SBI_PMU_CFG_FLAG_SET_VSINH	BIT(4)
#define SBI_PMU_CFG_FLAG_SET_UINH	BIT(5)
#define SBI_PMU_CFG_FLAG_SET_SINH	BIT(6)
#define SBI_PMU_CFG_FLAG_SET_MINH	BIT(7)

/* Flags defined for counter start function */
#define SBI_PMU_START_FLAG_SET_INIT_VALUE BIT(0)
#define SBI_PMU_START_FLAG_INIT_SNAPSHOT BIT(1)

/* Flags defined for counter stop function */
#define SBI_PMU_STOP_FLAG_RESET BIT(0)
#define SBI_PMU_STOP_FLAG_TAKE_SNAPSHOT BIT(1)

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5);

bool guest_sbi_probe_extension(int extid, long *out_val);

/* Make SBI version */
static inline unsigned long sbi_mk_version(unsigned long major,
					    unsigned long minor)
{
	return ((major & SBI_SPEC_VERSION_MAJOR_MASK) << SBI_SPEC_VERSION_MAJOR_SHIFT)
		| (minor & SBI_SPEC_VERSION_MINOR_MASK);
}

unsigned long get_host_sbi_spec_version(void);

#endif /* SELFTEST_KVM_SBI_H */
