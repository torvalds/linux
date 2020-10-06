/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Smart Sound Technology
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 */

#ifndef __SOUND_SOC_SST_DSP_PRIV_H
#define __SOUND_SOC_SST_DSP_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

#include "../skylake/skl-sst-dsp.h"

/*
 * DSP Operations exported by platform Audio DSP driver.
 */
struct sst_ops {
	/* Shim IO */
	void (*write)(void __iomem *addr, u32 offset, u32 value);
	u32 (*read)(void __iomem *addr, u32 offset);

	/* IRQ handlers */
	irqreturn_t (*irq_handler)(int irq, void *context);

	/* SST init and free */
	int (*init)(struct sst_dsp *sst);
	void (*free)(struct sst_dsp *sst);
};

/*
 * Audio DSP memory offsets and addresses.
 */
struct sst_addr {
	u32 sram0_base;
	u32 sram1_base;
	u32 w0_stat_sz;
	u32 w0_up_sz;
	void __iomem *lpe;
	void __iomem *shim;
};

/*
 * Audio DSP Mailbox configuration.
 */
struct sst_mailbox {
	void __iomem *in_base;
	void __iomem *out_base;
	size_t in_size;
	size_t out_size;
};

/*
 * Generic SST Shim Interface.
 */
struct sst_dsp {

	/* Shared for all platforms */

	/* runtime */
	struct sst_dsp_device *sst_dev;
	spinlock_t spinlock;	/* IPC locking */
	struct mutex mutex;	/* DSP FW lock */
	struct device *dev;
	void *thread_context;
	int irq;
	u32 id;

	/* operations */
	struct sst_ops *ops;

	/* debug FS */
	struct dentry *debugfs_root;

	/* base addresses */
	struct sst_addr addr;

	/* mailbox */
	struct sst_mailbox mailbox;

	/* SST FW files loaded and their modules */
	struct list_head module_list;

	/* SKL data */

	const char *fw_name;

	/* To allocate CL dma buffers */
	struct skl_dsp_loader_ops dsp_ops;
	struct skl_dsp_fw_ops fw_ops;
	int sst_state;
	struct skl_cl_dev cl_dev;
	u32 intr_status;
	const struct firmware *fw;
	struct snd_dma_buffer dmab;
};

static inline void *sst_dsp_get_thread_context(struct sst_dsp *sst)
{
	return sst->thread_context;
}

#endif
