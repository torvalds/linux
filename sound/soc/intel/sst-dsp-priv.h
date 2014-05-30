/*
 * Intel Smart Sound Technology
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SOUND_SOC_SST_DSP_PRIV_H
#define __SOUND_SOC_SST_DSP_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

struct sst_mem_block;
struct sst_module;
struct sst_fw;

/*
 * DSP Operations exported by platform Audio DSP driver.
 */
struct sst_ops {
	/* DSP core boot / reset */
	void (*boot)(struct sst_dsp *);
	void (*reset)(struct sst_dsp *);

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

	/* FW module parser/loader */
	int (*parse_fw)(struct sst_fw *sst_fw);
};

/*
 * Audio DSP memory offsets and addresses.
 */
struct sst_addr {
	u32 lpe_base;
	u32 shim_offset;
	u32 iram_offset;
	u32 dram_offset;
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
 * Audio DSP Firmware data types.
 */
enum sst_data_type {
	SST_DATA_M	= 0, /* module block data */
	SST_DATA_P	= 1, /* peristant data (text, data) */
	SST_DATA_S	= 2, /* scratch data (usually buffers) */
};

/*
 * Audio DSP memory block types.
 */
enum sst_mem_type {
	SST_MEM_IRAM = 0,
	SST_MEM_DRAM = 1,
	SST_MEM_ANY  = 2,
	SST_MEM_CACHE= 3,
};

/*
 * Audio DSP Generic Firmware File.
 *
 * SST Firmware files can consist of 1..N modules. This generic structure is
 * used to manage each firmware file and it's modules regardless of SST firmware
 * type. A SST driver may load multiple FW files.
 */
struct sst_fw {
	struct sst_dsp *dsp;

	/* base addresses of FW file data */
	dma_addr_t dmable_fw_paddr;	/* physical address of fw data */
	void *dma_buf;			/* virtual address of fw data */
	u32 size;			/* size of fw data */

	/* lists */
	struct list_head list;		/* DSP list of FW */
	struct list_head module_list;	/* FW list of modules */

	void *private;			/* core doesn't touch this */
};

/*
 * Audio DSP Generic Module data.
 *
 * This is used to dsecribe any sections of persistent (text and data) and
 * scratch (buffers) of module data in ADSP memory space.
 */
struct sst_module_data {

	enum sst_mem_type type;		/* destination memory type */
	enum sst_data_type data_type;	/* type of module data */

	u32 size;		/* size in bytes */
	int32_t offset;		/* offset in FW file */
	u32 data_offset;	/* offset in ADSP memory space */
	void *data;		/* module data */
};

/*
 * Audio DSP Generic Module Template.
 *
 * Used to define and register a new FW module. This data is extracted from
 * FW module header information.
 */
struct sst_module_template {
	u32 id;
	u32 entry;			/* entry point */
	struct sst_module_data s;	/* scratch data */
	struct sst_module_data p;	/* peristant data */
};

/*
 * Audio DSP Generic Module.
 *
 * Each Firmware file can consist of 1..N modules. A module can span multiple
 * ADSP memory blocks. The simplest FW will be a file with 1 module.
 */
struct sst_module {
	struct sst_dsp *dsp;
	struct sst_fw *sst_fw;		/* parent FW we belong too */

	/* module configuration */
	u32 id;
	u32 entry;			/* module entry point */
	u32 offset;			/* module offset in firmware file */
	u32 size;			/* module size */
	struct sst_module_data s;	/* scratch data */
	struct sst_module_data p;	/* peristant data */

	/* runtime */
	u32 usage_count;		/* can be unloaded if count == 0 */
	void *private;			/* core doesn't touch this */

	/* lists */
	struct list_head block_list;	/* Module list of blocks in use */
	struct list_head list;		/* DSP list of modules */
	struct list_head list_fw;	/* FW list of modules */
};

/*
 * SST Memory Block operations.
 */
struct sst_block_ops {
	int (*enable)(struct sst_mem_block *block);
	int (*disable)(struct sst_mem_block *block);
};

/*
 * SST Generic Memory Block.
 *
 * SST ADP  memory has multiple IRAM and DRAM blocks. Some ADSP blocks can be
 * power gated.
 */
struct sst_mem_block {
	struct sst_dsp *dsp;
	struct sst_module *module;	/* module that uses this block */

	/* block config */
	u32 offset;			/* offset from base */
	u32 size;			/* block size */
	u32 index;			/* block index 0..N */
	enum sst_mem_type type;		/* block memory type IRAM/DRAM */
	struct sst_block_ops *ops;	/* block operations, if any */

	/* block status */
	enum sst_data_type data_type;	/* data type held in this block */
	u32 bytes_used;			/* bytes in use by modules */
	void *private;			/* generic core does not touch this */
	int users;			/* number of modules using this block */

	/* block lists */
	struct list_head module_list;	/* Module list of blocks */
	struct list_head list;		/* Map list of free/used blocks */
};

/*
 * Generic SST Shim Interface.
 */
struct sst_dsp {

	/* runtime */
	struct sst_dsp_device *sst_dev;
	spinlock_t spinlock;	/* IPC locking */
	struct mutex mutex;	/* DSP FW lock */
	struct device *dev;
	struct device *dma_dev;
	void *thread_context;
	int irq;
	u32 id;

	/* list of free and used ADSP memory blocks */
	struct list_head used_block_list;
	struct list_head free_block_list;

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
	struct list_head fw_list;

	/* platform data */
	struct sst_pdata *pdata;

	/* DMA FW loading */
	struct sst_dma *dma;
	bool fw_use_dma;
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

/* Create/Free FW files - can contain multiple modules */
struct sst_fw *sst_fw_new(struct sst_dsp *dsp,
	const struct firmware *fw, void *private);
void sst_fw_free(struct sst_fw *sst_fw);
void sst_fw_free_all(struct sst_dsp *dsp);

/* Create/Free firmware modules */
struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private);
void sst_module_free(struct sst_module *sst_module);
int sst_module_insert(struct sst_module *sst_module);
int sst_module_remove(struct sst_module *sst_module);
int sst_module_insert_fixed_block(struct sst_module *module,
	struct sst_module_data *data);
struct sst_module *sst_module_get_from_id(struct sst_dsp *dsp, u32 id);

/* allocate/free pesistent/scratch memory regions managed by drv */
struct sst_module *sst_mem_block_alloc_scratch(struct sst_dsp *dsp);
void sst_mem_block_free_scratch(struct sst_dsp *dsp,
	struct sst_module *scratch);
int sst_block_module_remove(struct sst_module *module);

/* Register the DSPs memory blocks - would be nice to read from ACPI */
struct sst_mem_block *sst_mem_block_register(struct sst_dsp *dsp, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops, u32 index,
	void *private);
void sst_mem_block_unregister_all(struct sst_dsp *dsp);

#endif
