/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 *
 * Author: Cezary Rojewski <cezary.rojewski@intel.com>
 */

#ifndef __SND_SOC_INTEL_CATPT_CORE_H
#define __SND_SOC_INTEL_CATPT_CORE_H

#include <linux/dma/dw.h>
#include <linux/irqreturn.h>
#include "messages.h"
#include "registers.h"

struct catpt_dev;

extern const struct attribute_group *catpt_attr_groups[];

void catpt_sram_init(struct resource *sram, u32 start, u32 size);
void catpt_sram_free(struct resource *sram);
struct resource *
catpt_request_region(struct resource *root, resource_size_t size);

static inline bool catpt_resource_overlapping(struct resource *r1,
					      struct resource *r2,
					      struct resource *ret)
{
	if (!resource_overlaps(r1, r2))
		return false;
	ret->start = max(r1->start, r2->start);
	ret->end = min(r1->end, r2->end);
	return true;
}

struct catpt_ipc_msg {
	union {
		u32 header;
		union catpt_global_msg rsp;
	};
	void *data;
	size_t size;
};

struct catpt_ipc {
	struct device *dev;

	struct catpt_ipc_msg rx;
	struct catpt_fw_ready config;
	u32 default_timeout;
	bool ready;

	spinlock_t lock;
	struct mutex mutex;
	struct completion done_completion;
	struct completion busy_completion;
};

void catpt_ipc_init(struct catpt_ipc *ipc, struct device *dev);

struct catpt_module_type {
	bool loaded;
	u32 entry_point;
	u32 persistent_size;
	u32 scratch_size;
	/* DRAM, initial module state */
	u32 state_offset;
	u32 state_size;

	struct list_head node;
};

struct catpt_spec {
	struct snd_soc_acpi_mach *machines;
	u8 core_id;
	u32 host_dram_offset;
	u32 host_iram_offset;
	u32 host_shim_offset;
	u32 host_dma_offset[CATPT_DMA_COUNT];
	u32 host_ssp_offset[CATPT_SSP_COUNT];
	u32 dram_mask;
	u32 iram_mask;
	void (*pll_shutdown)(struct catpt_dev *cdev, bool enable);
	int (*power_up)(struct catpt_dev *cdev);
	int (*power_down)(struct catpt_dev *cdev);
};

struct catpt_dev {
	struct device *dev;
	struct dw_dma_chip *dmac;
	struct catpt_ipc ipc;

	void __iomem *pci_ba;
	void __iomem *lpe_ba;
	u32 lpe_base;
	int irq;

	const struct catpt_spec *spec;
	struct completion fw_ready;

	struct resource dram;
	struct resource iram;
	struct resource *scratch;

	struct catpt_mixer_stream_info mixer;
	struct catpt_module_type modules[CATPT_MODULE_COUNT];
	struct catpt_ssp_device_format devfmt[CATPT_SSP_COUNT];
	struct list_head stream_list;
	spinlock_t list_lock;
	struct mutex clk_mutex;

	struct catpt_dx_context dx_ctx;
	void *dxbuf_vaddr;
	dma_addr_t dxbuf_paddr;
};

int catpt_dmac_probe(struct catpt_dev *cdev);
void catpt_dmac_remove(struct catpt_dev *cdev);
struct dma_chan *catpt_dma_request_config_chan(struct catpt_dev *cdev);
int catpt_dma_memcpy_todsp(struct catpt_dev *cdev, struct dma_chan *chan,
			   dma_addr_t dst_addr, dma_addr_t src_addr,
			   size_t size);
int catpt_dma_memcpy_fromdsp(struct catpt_dev *cdev, struct dma_chan *chan,
			     dma_addr_t dst_addr, dma_addr_t src_addr,
			     size_t size);

void lpt_dsp_pll_shutdown(struct catpt_dev *cdev, bool enable);
void wpt_dsp_pll_shutdown(struct catpt_dev *cdev, bool enable);
int lpt_dsp_power_up(struct catpt_dev *cdev);
int lpt_dsp_power_down(struct catpt_dev *cdev);
int wpt_dsp_power_up(struct catpt_dev *cdev);
int wpt_dsp_power_down(struct catpt_dev *cdev);
int catpt_dsp_stall(struct catpt_dev *cdev, bool stall);
void catpt_dsp_update_srampge(struct catpt_dev *cdev, struct resource *sram,
			      unsigned long mask);
int catpt_dsp_update_lpclock(struct catpt_dev *cdev);
irqreturn_t catpt_dsp_irq_handler(int irq, void *dev_id);
irqreturn_t catpt_dsp_irq_thread(int irq, void *dev_id);

/*
 * IPC handlers may return positive values which denote successful
 * HOST <-> DSP communication yet failure to process specific request.
 * Use below macro to convert returned non-zero values appropriately
 */
#define CATPT_IPC_ERROR(err) (((err) < 0) ? (err) : -EREMOTEIO)

int catpt_dsp_send_msg_timeout(struct catpt_dev *cdev,
			       struct catpt_ipc_msg request,
			       struct catpt_ipc_msg *reply, int timeout);
int catpt_dsp_send_msg(struct catpt_dev *cdev, struct catpt_ipc_msg request,
		       struct catpt_ipc_msg *reply);

int catpt_first_boot_firmware(struct catpt_dev *cdev);
int catpt_boot_firmware(struct catpt_dev *cdev, bool restore);
int catpt_store_streams_context(struct catpt_dev *cdev, struct dma_chan *chan);
int catpt_store_module_states(struct catpt_dev *cdev, struct dma_chan *chan);
int catpt_store_memdumps(struct catpt_dev *cdev, struct dma_chan *chan);
int catpt_coredump(struct catpt_dev *cdev);

#include <sound/memalloc.h>
#include <uapi/sound/asound.h>

struct snd_pcm_substream;
struct catpt_stream_template;

struct catpt_stream_runtime {
	struct snd_pcm_substream *substream;

	struct catpt_stream_template *template;
	struct catpt_stream_info info;
	struct resource *persistent;
	struct snd_dma_buffer pgtbl;

	bool allocated;
	bool prepared;

	struct list_head node;
};

int catpt_register_plat_component(struct catpt_dev *cdev);
void catpt_stream_update_position(struct catpt_dev *cdev,
				  struct catpt_stream_runtime *stream,
				  struct catpt_notify_position *pos);
struct catpt_stream_runtime *
catpt_stream_find(struct catpt_dev *cdev, u8 stream_hw_id);
int catpt_arm_stream_templates(struct catpt_dev *cdev);

#endif
