/*
 * include/asm-sh/dma.h
 *
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_DMA_H
#define __ASM_SH_DMA_H
#ifdef __KERNEL__

#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sysdev.h>
#include <asm/cpu/dma.h>

/* The maximum address that we can perform a DMA transfer to on this platform */
/* Don't define MAX_DMA_ADDRESS; it's useless on the SuperH and any
   occurrence should be flagged as an error.  */
/* But... */
/* XXX: This is not applicable to SuperH, just needed for alloc_bootmem */
#define MAX_DMA_ADDRESS		(PAGE_OFFSET+0x10000000)

#ifdef CONFIG_NR_DMA_CHANNELS
#  define MAX_DMA_CHANNELS	(CONFIG_NR_DMA_CHANNELS)
#else
#  define MAX_DMA_CHANNELS	(CONFIG_NR_ONCHIP_DMA_CHANNELS)
#endif

/*
 * Read and write modes can mean drastically different things depending on the
 * channel configuration. Consult your DMAC documentation and module
 * implementation for further clues.
 */
#define DMA_MODE_READ		0x00
#define DMA_MODE_WRITE		0x01
#define DMA_MODE_MASK		0x01

#define DMA_AUTOINIT		0x10

/*
 * DMAC (dma_info) flags
 */
enum {
	DMAC_CHANNELS_CONFIGURED	= 0x01,
	DMAC_CHANNELS_TEI_CAPABLE	= 0x02,	/* Transfer end interrupt */
};

/*
 * DMA channel capabilities / flags
 */
enum {
	DMA_CONFIGURED			= 0x01,

	/*
	 * Transfer end interrupt, inherited from DMAC.
	 * wait_queue used in dma_wait_for_completion.
	 */
	DMA_TEI_CAPABLE			= 0x02,
};

extern spinlock_t dma_spin_lock;

struct dma_channel;

struct dma_ops {
	int (*request)(struct dma_channel *chan);
	void (*free)(struct dma_channel *chan);

	int (*get_residue)(struct dma_channel *chan);
	int (*xfer)(struct dma_channel *chan);
	int (*configure)(struct dma_channel *chan, unsigned long flags);
	int (*extend)(struct dma_channel *chan, unsigned long op, void *param);
};

struct dma_channel {
	char dev_id[16];		/* unique name per DMAC of channel */

	unsigned int chan;		/* DMAC channel number */
	unsigned int vchan;		/* Virtual channel number */

	unsigned int mode;
	unsigned int count;

	unsigned long sar;
	unsigned long dar;

	const char **caps;

	unsigned long flags;
	atomic_t busy;

	wait_queue_head_t wait_queue;

	struct sys_device dev;
	void *priv_data;
};

struct dma_info {
	struct platform_device *pdev;

	const char *name;
	unsigned int nr_channels;
	unsigned long flags;

	struct dma_ops *ops;
	struct dma_channel *channels;

	struct list_head list;
	int first_channel_nr;
	int first_vchannel_nr;
};

struct dma_chan_caps {
	int ch_num;
	const char **caplist;
};

#define to_dma_channel(channel) container_of(channel, struct dma_channel, dev)

/* arch/sh/drivers/dma/dma-api.c */
extern int dma_xfer(unsigned int chan, unsigned long from,
		    unsigned long to, size_t size, unsigned int mode);

#define dma_write(chan, from, to, size)	\
	dma_xfer(chan, from, to, size, DMA_MODE_WRITE)
#define dma_write_page(chan, from, to)	\
	dma_write(chan, from, to, PAGE_SIZE)

#define dma_read(chan, from, to, size)	\
	dma_xfer(chan, from, to, size, DMA_MODE_READ)
#define dma_read_page(chan, from, to)	\
	dma_read(chan, from, to, PAGE_SIZE)

extern int request_dma_bycap(const char **dmac, const char **caps,
			     const char *dev_id);
extern int request_dma(unsigned int chan, const char *dev_id);
extern void free_dma(unsigned int chan);
extern int get_dma_residue(unsigned int chan);
extern struct dma_info *get_dma_info(unsigned int chan);
extern struct dma_channel *get_dma_channel(unsigned int chan);
extern void dma_wait_for_completion(unsigned int chan);
extern void dma_configure_channel(unsigned int chan, unsigned long flags);

extern int register_dmac(struct dma_info *info);
extern void unregister_dmac(struct dma_info *info);
extern struct dma_info *get_dma_info_by_name(const char *dmac_name);

extern int dma_extend(unsigned int chan, unsigned long op, void *param);
extern int register_chan_caps(const char *dmac, struct dma_chan_caps *capslist);

#ifdef CONFIG_SYSFS
/* arch/sh/drivers/dma/dma-sysfs.c */
extern int dma_create_sysfs_files(struct dma_channel *, struct dma_info *);
extern void dma_remove_sysfs_files(struct dma_channel *, struct dma_info *);
#else
#define dma_create_sysfs_file(channel, info)		do { } while (0)
#define dma_remove_sysfs_file(channel, info)		do { } while (0)
#endif

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy	(0)
#endif

#endif /* __KERNEL__ */
#endif /* __ASM_SH_DMA_H */
