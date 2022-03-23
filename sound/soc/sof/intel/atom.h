/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017-2021 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOF_INTEL_ATOM_H
#define __SOF_INTEL_ATOM_H

/* DSP memories */
#define IRAM_OFFSET		0x0C0000
#define IRAM_SIZE		(80 * 1024)
#define DRAM_OFFSET		0x100000
#define DRAM_SIZE		(160 * 1024)
#define SHIM_OFFSET		0x140000
#define SHIM_SIZE_BYT		0x100
#define SHIM_SIZE_CHT		0x118
#define MBOX_OFFSET		0x144000
#define MBOX_SIZE		0x1000
#define EXCEPT_OFFSET		0x800
#define EXCEPT_MAX_HDR_SIZE	0x400

/* DSP peripherals */
#define DMAC0_OFFSET		0x098000
#define DMAC1_OFFSET		0x09c000
#define DMAC2_OFFSET		0x094000
#define DMAC_SIZE		0x420
#define SSP0_OFFSET		0x0a0000
#define SSP1_OFFSET		0x0a1000
#define SSP2_OFFSET		0x0a2000
#define SSP3_OFFSET		0x0a4000
#define SSP4_OFFSET		0x0a5000
#define SSP5_OFFSET		0x0a6000
#define SSP_SIZE		0x100

#define STACK_DUMP_SIZE		32

#define PCI_BAR_SIZE		0x200000

#define PANIC_OFFSET(x)	(((x) & GENMASK_ULL(47, 32)) >> 32)

/*
 * Debug
 */

#define MBOX_DUMP_SIZE	0x30

/* BARs */
#define DSP_BAR		0
#define PCI_BAR		1
#define IMR_BAR		2

irqreturn_t atom_irq_handler(int irq, void *context);
irqreturn_t atom_irq_thread(int irq, void *context);

int atom_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg);
int atom_get_mailbox_offset(struct snd_sof_dev *sdev);
int atom_get_window_offset(struct snd_sof_dev *sdev, u32 id);

int atom_run(struct snd_sof_dev *sdev);
int atom_reset(struct snd_sof_dev *sdev);
void atom_dump(struct snd_sof_dev *sdev, u32 flags);

struct snd_soc_acpi_mach *atom_machine_select(struct snd_sof_dev *sdev);
void atom_set_mach_params(struct snd_soc_acpi_mach *mach,
			  struct snd_sof_dev *sdev);

extern struct snd_soc_dai_driver atom_dai[];

#endif
