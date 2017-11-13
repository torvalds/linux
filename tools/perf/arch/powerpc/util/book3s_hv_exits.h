/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_PERF_BOOK3S_HV_EXITS_H
#define ARCH_PERF_BOOK3S_HV_EXITS_H

/*
 * PowerPC Interrupt vectors : exit code to name mapping
 */

#define kvm_trace_symbol_exit \
	{0x0,	"RETURN_TO_HOST"}, \
	{0x100, "SYSTEM_RESET"}, \
	{0x200, "MACHINE_CHECK"}, \
	{0x300, "DATA_STORAGE"}, \
	{0x380, "DATA_SEGMENT"}, \
	{0x400, "INST_STORAGE"}, \
	{0x480, "INST_SEGMENT"}, \
	{0x500, "EXTERNAL"}, \
	{0x501, "EXTERNAL_LEVEL"}, \
	{0x502, "EXTERNAL_HV"}, \
	{0x600, "ALIGNMENT"}, \
	{0x700, "PROGRAM"}, \
	{0x800, "FP_UNAVAIL"}, \
	{0x900, "DECREMENTER"}, \
	{0x980, "HV_DECREMENTER"}, \
	{0xc00, "SYSCALL"}, \
	{0xd00, "TRACE"}, \
	{0xe00, "H_DATA_STORAGE"}, \
	{0xe20, "H_INST_STORAGE"}, \
	{0xe40, "H_EMUL_ASSIST"}, \
	{0xf00, "PERFMON"}, \
	{0xf20, "ALTIVEC"}, \
	{0xf40, "VSX"}

#endif
