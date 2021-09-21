// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Pan Xiuli <xiuli.pan@linux.intel.com>
//

#include <linux/module.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../sof-priv.h"

struct xtensa_exception_cause {
	u32 id;
	const char *msg;
	const char *description;
};

/*
 * From 4.4.1.5 table 4-64 Exception Causes of Xtensa
 * Instruction Set Architecture (ISA) Reference Manual
 */
static const struct xtensa_exception_cause xtensa_exception_causes[] = {
	{0, "IllegalInstructionCause", "Illegal instruction"},
	{1, "SyscallCause", "SYSCALL instruction"},
	{2, "InstructionFetchErrorCause",
	"Processor internal physical address or data error during instruction fetch"},
	{3, "LoadStoreErrorCause",
	"Processor internal physical address or data error during load or store"},
	{4, "Level1InterruptCause",
	"Level-1 interrupt as indicated by set level-1 bits in the INTERRUPT register"},
	{5, "AllocaCause",
	"MOVSP instruction, if caller’s registers are not in the register file"},
	{6, "IntegerDivideByZeroCause",
	"QUOS, QUOU, REMS, or REMU divisor operand is zero"},
	{8, "PrivilegedCause",
	"Attempt to execute a privileged operation when CRING ? 0"},
	{9, "LoadStoreAlignmentCause", "Load or store to an unaligned address"},
	{12, "InstrPIFDataErrorCause",
	"PIF data error during instruction fetch"},
	{13, "LoadStorePIFDataErrorCause",
	"Synchronous PIF data error during LoadStore access"},
	{14, "InstrPIFAddrErrorCause",
	"PIF address error during instruction fetch"},
	{15, "LoadStorePIFAddrErrorCause",
	"Synchronous PIF address error during LoadStore access"},
	{16, "InstTLBMissCause", "Error during Instruction TLB refill"},
	{17, "InstTLBMultiHitCause",
	"Multiple instruction TLB entries matched"},
	{18, "InstFetchPrivilegeCause",
	"An instruction fetch referenced a virtual address at a ring level less than CRING"},
	{20, "InstFetchProhibitedCause",
	"An instruction fetch referenced a page mapped with an attribute that does not permit instruction fetch"},
	{24, "LoadStoreTLBMissCause",
	"Error during TLB refill for a load or store"},
	{25, "LoadStoreTLBMultiHitCause",
	"Multiple TLB entries matched for a load or store"},
	{26, "LoadStorePrivilegeCause",
	"A load or store referenced a virtual address at a ring level less than CRING"},
	{28, "LoadProhibitedCause",
	"A load referenced a page mapped with an attribute that does not permit loads"},
	{32, "Coprocessor0Disabled",
	"Coprocessor 0 instruction when cp0 disabled"},
	{33, "Coprocessor1Disabled",
	"Coprocessor 1 instruction when cp1 disabled"},
	{34, "Coprocessor2Disabled",
	"Coprocessor 2 instruction when cp2 disabled"},
	{35, "Coprocessor3Disabled",
	"Coprocessor 3 instruction when cp3 disabled"},
	{36, "Coprocessor4Disabled",
	"Coprocessor 4 instruction when cp4 disabled"},
	{37, "Coprocessor5Disabled",
	"Coprocessor 5 instruction when cp5 disabled"},
	{38, "Coprocessor6Disabled",
	"Coprocessor 6 instruction when cp6 disabled"},
	{39, "Coprocessor7Disabled",
	"Coprocessor 7 instruction when cp7 disabled"},
};

/* only need xtensa atm */
static void xtensa_dsp_oops(struct snd_sof_dev *sdev, void *oops)
{
	struct sof_ipc_dsp_oops_xtensa *xoops = oops;
	int i;

	dev_err(sdev->dev, "error: DSP Firmware Oops\n");
	for (i = 0; i < ARRAY_SIZE(xtensa_exception_causes); i++) {
		if (xtensa_exception_causes[i].id == xoops->exccause) {
			dev_err(sdev->dev, "error: Exception Cause: %s, %s\n",
				xtensa_exception_causes[i].msg,
				xtensa_exception_causes[i].description);
		}
	}
	dev_err(sdev->dev, "EXCCAUSE 0x%8.8x EXCVADDR 0x%8.8x PS       0x%8.8x SAR     0x%8.8x\n",
		xoops->exccause, xoops->excvaddr, xoops->ps, xoops->sar);
	dev_err(sdev->dev, "EPC1     0x%8.8x EPC2     0x%8.8x EPC3     0x%8.8x EPC4    0x%8.8x",
		xoops->epc1, xoops->epc2, xoops->epc3, xoops->epc4);
	dev_err(sdev->dev, "EPC5     0x%8.8x EPC6     0x%8.8x EPC7     0x%8.8x DEPC    0x%8.8x",
		xoops->epc5, xoops->epc6, xoops->epc7, xoops->depc);
	dev_err(sdev->dev, "EPS2     0x%8.8x EPS3     0x%8.8x EPS4     0x%8.8x EPS5    0x%8.8x",
		xoops->eps2, xoops->eps3, xoops->eps4, xoops->eps5);
	dev_err(sdev->dev, "EPS6     0x%8.8x EPS7     0x%8.8x INTENABL 0x%8.8x INTERRU 0x%8.8x",
		xoops->eps6, xoops->eps7, xoops->intenable, xoops->interrupt);
}

static void xtensa_stack(struct snd_sof_dev *sdev, void *oops, u32 *stack,
			 u32 stack_words)
{
	struct sof_ipc_dsp_oops_xtensa *xoops = oops;
	u32 stack_ptr = xoops->plat_hdr.stackptr;
	/* 4 * 8chars + 3 ws + 1 terminating NUL */
	unsigned char buf[4 * 8 + 3 + 1];
	int i;

	dev_err(sdev->dev, "stack dump from 0x%8.8x\n", stack_ptr);

	/*
	 * example output:
	 * 0x0049fbb0: 8000f2d0 0049fc00 6f6c6c61 00632e63
	 */
	for (i = 0; i < stack_words; i += 4) {
		hex_dump_to_buffer(stack + i, 16, 16, 4,
				   buf, sizeof(buf), false);
		dev_err(sdev->dev, "0x%08x: %s\n", stack_ptr + i * 4, buf);
	}
}

const struct sof_arch_ops sof_xtensa_arch_ops = {
	.dsp_oops = xtensa_dsp_oops,
	.dsp_stack = xtensa_stack,
};
EXPORT_SYMBOL_NS(sof_xtensa_arch_ops, SND_SOC_SOF_XTENSA);

MODULE_DESCRIPTION("SOF Xtensa DSP support");
MODULE_LICENSE("Dual BSD/GPL");
