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

#include "../skylake/skl-sst-dsp.h"

struct sst_mem_block;
struct sst_module;
struct sst_fw;

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
	u32 dsp_iram_offset;
	u32 dsp_dram_offset;
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
 * Audio DSP Generic Module Template.
 *
 * Used to define and register a new FW module. This data is extracted from
 * FW module header information.
 */
struct sst_module_template {
	u32 id;
	u32 entry;			/* entry point */
	u32 scratch_size;
	u32 persistent_size;
};

/*
 * Block Allocator - Used to allocate blocks of DSP memory.
 */
struct sst_block_allocator {
	u32 id;
	u32 offset;
	int size;
	enum sst_mem_type type;
};

/*
 * Runtime Module Instance - A module object can be instanciated multiple
 * times within the DSP FW.
 */
struct sst_module_runtime {
	struct sst_dsp *dsp;
	int id;
	struct sst_module *module;	/* parent module we belong too */

	u32 persistent_offset;		/* private memory offset */
	void *private;

	struct list_head list;
	struct list_head block_list;	/* list of blocks used */
};

/*
 * Runtime Module Context - The runtime context must be manually stored by the
 * driver prior to enter S3 and restored after leaving S3. This should really be
 * part of the memory context saved by the enter D3 message IPC ???
 */
struct sst_module_runtime_context {
	dma_addr_t dma_buffer;
	u32 *buffer;
};

/*
 * Audio DSP Module State
 */
enum sst_module_state {
	SST_MODULE_STATE_UNLOADED = 0,	/* default state */
	SST_MODULE_STATE_LOADED,
	SST_MODULE_STATE_INITIALIZED,	/* and inactive */
	SST_MODULE_STATE_ACTIVE,
};

/*
 * Audio DSP Generic Module.
 *
 * Each Firmware file can consist of 1..N modules. A module can span multiple
 * ADSP memory blocks. The simplest FW will be a file with 1 module. A module
 * can be instanciated multiple times in the DSP.
 */
struct sst_module {
	struct sst_dsp *dsp;
	struct sst_fw *sst_fw;		/* parent FW we belong too */

	/* module configuration */
	u32 id;
	u32 entry;			/* module entry point */
	s32 offset;			/* module offset in firmware file */
	u32 size;			/* module size */
	u32 scratch_size;		/* global scratch memory required */
	u32 persistent_size;		/* private memory required */
	enum sst_mem_type type;		/* destination memory type */
	u32 data_offset;		/* offset in ADSP memory space */
	void *data;			/* module data */

	/* runtime */
	u32 usage_count;		/* can be unloaded if count == 0 */
	void *private;			/* core doesn't touch this */

	/* lists */
	struct list_head block_list;	/* Module list of blocks in use */
	struct list_head list;		/* DSP list of modules */
	struct list_head list_fw;	/* FW list of modules */
	struct list_head runtime_list;	/* list of runtime module objects*/

	/* state */
	enum sst_module_state state;
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
	const struct sst_block_ops *ops;/* block operations, if any */

	/* block status */
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

/* Create/Free FW files - can contain multiple modules */
struct sst_fw *sst_fw_new(struct sst_dsp *dsp,
	const struct firmware *fw, void *private);
void sst_fw_free(struct sst_fw *sst_fw);
void sst_fw_free_all(struct sst_dsp *dsp);
int sst_fw_reload(struct sst_fw *sst_fw);
void sst_fw_unload(struct sst_fw *sst_fw);

/* Create/Free firmware modules */
struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private);
void sst_module_free(struct sst_module *module);
struct sst_module *sst_module_get_from_id(struct sst_dsp *dsp, u32 id);
int sst_module_alloc_blocks(struct sst_module *module);
int sst_module_free_blocks(struct sst_module *module);

/* Create/Free firmware module runtime instances */
struct sst_module_runtime *sst_module_runtime_new(struct sst_module *module,
	int id, void *private);
void sst_module_runtime_free(struct sst_module_runtime *runtime);
struct sst_module_runtime *sst_module_runtime_get_from_id(
	struct sst_module *module, u32 id);
int sst_module_runtime_alloc_blocks(struct sst_module_runtime *runtime,
	int offset);
int sst_module_runtime_free_blocks(struct sst_module_runtime *runtime);
int sst_module_runtime_save(struct sst_module_runtime *runtime,
	struct sst_module_runtime_context *context);
int sst_module_runtime_restore(struct sst_module_runtime *runtime,
	struct sst_module_runtime_context *context);

/* generic block allocation */
int sst_alloc_blocks(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list);
int sst_free_blocks(struct sst_dsp *dsp, struct list_head *block_list);

/* scratch allocation */
int sst_block_alloc_scratch(struct sst_dsp *dsp);
void sst_block_free_scratch(struct sst_dsp *dsp);

/* Register the DSPs memory blocks - would be nice to read from ACPI */
struct sst_mem_block *sst_mem_block_register(struct sst_dsp *dsp, u32 offset,
	u32 size, enum sst_mem_type type, const struct sst_block_ops *ops,
	u32 index, void *private);
void sst_mem_block_unregister_all(struct sst_dsp *dsp);

/* Create/Free DMA resources */
int sst_dma_new(struct sst_dsp *sst);
void sst_dma_free(struct sst_dma *dma);

u32 sst_dsp_get_offset(struct sst_dsp *dsp, u32 offset,
	enum sst_mem_type type);
#endif
