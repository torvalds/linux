/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rivos, Inc */

.text
.global riscv_hwprobe
riscv_hwprobe:
	# Put __NR_riscv_hwprobe in the syscall number register, then just shim
	# back the kernel's return.  This doesn't do any sort of errno
	# handling, the caller can deal with it.
	li a7, 258
	ecall
	ret
