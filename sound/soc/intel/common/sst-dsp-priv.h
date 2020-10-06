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

/* do we need to remove or keep */
#define DSP_DRAM_ADDR_OFFSET		0x400000

/*
 * DSP Operations exported by platform Audio DSP driver.
 */
struct sst_ops {
	/* DSP core boot / reset */
	void (*boot)(struct sst_dsp *);
	void (*reset)(struct sst_dsp *);
	int (*wake)(struct sst_dsp *);
	void (*sleep)(struct sst_dsp *);
	void (*stall)(struct sst_dsp *);

	/* Shim IO */
	void (*write)(void __iomem *addr, u32 offset, u32 value);
	u32 (*read)(void __iomem *addr, u32 offset);
	void (*write64)(void __iomem *addr, u32 offset, u64 value);
	u64 (*read64)(void __iomem *addr, u32 offset);

	/* DSP I/DRAM IO */
	void (*ram_read)(struct sst_dsp *sst, void  *dest, void __iomem *src,
		size_t bytes);
	void (*ram_write)(struct sst_dsp *sst, void __iomem *dest, void *src,
		size_t bytes);

	void (*dump)(struct sst_dsp *);

	/* IRQ handlers */
	irqreturn_t (*irq_handler)(int irq, void *context);

	/* SST init and free */
	int (*init)(struct sst_dsp *sst, struct sst_pdata *pdata);
	void (*free)(struct sst_dsp *sst);
};

/*
 * Audio DSP memory offsets and addresses.
 */
struct sst_addr {
	u32 lpe_base;
	u32 shim_offset;
	u32 iram_offset;
	u32 dram_offset;
	u32 dsp_iram_offset;
	u32 dsp_dram_offset;
	u32 sram0_base;
	u32 sram1_base;
	u32 w0_stat_sz;
	u32 w0_up_sz;
	void __iomem *lpe;
	void __iomem *shim;
	void __iomem *pci_cfg;
	void __iomem *fw_ext;
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
	struct device *dma_dev;
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

	/* HSW/Byt data */

	/* list of free and used ADSP memory blocks */
	struct list_head used_block_list;
	struct list_head free_block_list;

	/* SST FW files loaded and their modules */
	struct list_head module_list;
	struct list_head fw_list;

	/* scratch buffer */
	struct list_head scratch_block_list;
	u32 scratch_offset;
	u32 scratch_size;

	/* platform data */
	struct sst_pdata *pdata;

	/* DMA FW loading */
	struct sst_dma *dma;
	bool fw_use_dma;

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

/* Size optimised DRAM/IRAM memcpy */
static inline void sst_dsp_write(struct sst_dsp *sst, void *src,
	u32 dest_offset, size_t bytes)
{
	sst->ops->ram_write(sst, sst->addr.lpe + dest_offset, src, bytes);
}

static inline void sst_dsp_read(struct sst_dsp *sst, void *dest,
	u32 src_offset, size_t bytes)
{
	sst->ops->ram_read(sst, dest, sst->addr.lpe + src_offset, bytes);
}

static inline void *sst_dsp_get_thread_context(struct sst_dsp *sst)
{
	return sst->thread_context;
}

#endif
