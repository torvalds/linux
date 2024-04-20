/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RISC-V SBI specific definitions
 *
 * Copyright (C) 2024 Rivos Inc.
 */

#ifndef SELFTEST_KVM_SBI_H
#define SELFTEST_KVM_SBI_H

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
};

enum sbi_ext_base_fid {
	SBI_EXT_BASE_PROBE_EXT = 3,
};

struct sbiret {
	long error;
	long value;
};

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5);

bool guest_sbi_probe_extension(int extid, long *out_val);

#endif /* SELFTEST_KVM_SBI_H */
