// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#ifndef __SOUND_SOC_SOF_PRIV_H
#define __SOUND_SOC_SOF_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <uapi/sound/sof-ipc.h>
#include <uapi/sound/sof-fw.h>
#include <uapi/sound/asoc.h>
#include <sound/hdaudio.h>
#include <sound/compress_driver.h>

/* debug flags */
#define SOF_DBG_REGS	BIT(1)
#define SOF_DBG_MBOX	BIT(2)
#define SOF_DBG_TEXT	BIT(3)
#define SOF_DBG_PCI	BIT(4)

/* max BARs mmaped devices can use */
#define SND_SOF_BARS	8

/* time in ms for runtime suspend delay */
#define SND_SOF_SUSPEND_DELAY	2000

/* DMA buffer size for trace */
#define DMA_BUF_SIZE_FOR_TRACE (PAGE_SIZE * 16)

/* max number of FE PCMs before BEs */
#define SOF_BE_PCM_BASE		16

#define SOF_IPC_DSP_REPLY		0
#define SOF_IPC_HOST_REPLY		1

/* convenience constructor for DAI driver streams */
#define SOF_DAI_STREAM(sname, scmin, scmax, srates, sfmt) \
	{.stream_name = sname, .channels_min = scmin, .channels_max = scmax, \
	 .rates = srates, .formats = sfmt}

#define SOF_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_FLOAT)

struct snd_sof_dev;
struct snd_sof_ipc_msg;
struct snd_sof_ipc;
struct snd_sof_debugfs_map;
struct snd_soc_tplg_ops;
struct snd_soc_component;
struct sof_intel_hda_dev;
struct snd_sof_pdata;

/*
 * SOF DSP HW abstraction operations.
 * Used to abstract DSP HW architecture and any IO busses between host CPU
 * and DSP device(s).
 */
struct snd_sof_dsp_ops {
	/* probe and remove */
	int (*remove)(struct snd_sof_dev *sof_dev);
	int (*probe)(struct snd_sof_dev *sof_dev);

	/* DSP core boot / reset */
	int (*run)(struct snd_sof_dev *sof_dev);
	int (*stall)(struct snd_sof_dev *sof_dev);
	int (*reset)(struct snd_sof_dev *sof_dev);

	/* DSP PM */
	int (*suspend)(struct snd_sof_dev *sof_dev, int state);
	int (*resume)(struct snd_sof_dev *sof_dev);
	int (*runtime_suspend)(struct snd_sof_dev *sof_dev, int state);
	int (*runtime_resume)(struct snd_sof_dev *sof_dev);

	/* DSP clocking */
	int (*set_clk)(struct snd_sof_dev *sof_dev, u32 freq);

	/* Register IO */
	void (*write)(struct snd_sof_dev *sof_dev, void __iomem *addr,
		      u32 value);
	u32 (*read)(struct snd_sof_dev *sof_dev, void __iomem *addr);
	void (*write64)(struct snd_sof_dev *sof_dev, void __iomem *addr,
			u64 value);
	u64 (*read64)(struct snd_sof_dev *sof_dev, void __iomem *addr);

	/* memcpy IO */
	void (*block_read)(struct snd_sof_dev *sof_dev,
			   u32 offset, void *dest, size_t size);
	void (*block_write)(struct snd_sof_dev *sof_dev,
			    u32 offset, void *src, size_t size);

	/* doorbell */
	irqreturn_t (*irq_handler)(int irq, void *context);
	irqreturn_t (*irq_thread)(int irq, void *context);

	/* mailbox */
	void (*mailbox_read)(struct snd_sof_dev *sof_dev, u32 offset,
			     void *addr, size_t bytes);
	void (*mailbox_write)(struct snd_sof_dev *sof_dev, u32 offset,
			      void *addr, size_t bytes);

	/* ipc */
	int (*send_msg)(struct snd_sof_dev *sof_dev,
			struct snd_sof_ipc_msg *msg);
	int (*get_reply)(struct snd_sof_dev *sof_dev,
			 struct snd_sof_ipc_msg *msg);
	int (*is_ready)(struct snd_sof_dev *sof_dev);
	int (*cmd_done)(struct snd_sof_dev *sof_dev, int dir);

	/* debug */
	const struct snd_sof_debugfs_map *debug_map;
	int debug_map_count;
	void (*dbg_dump)(struct snd_sof_dev *sof_dev, u32 flags);

	/* connect pcm substream to a host stream */
	int (*pcm_open)(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream);
	/* disconnect pcm substream to a host stream */
	int (*pcm_close)(struct snd_sof_dev *sdev,
			 struct snd_pcm_substream *substream);

	/* host stream hw params */
	int (*pcm_hw_params)(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct sof_ipc_stream_params *ipc_params);

	/* host stream trigger */
	int (*pcm_trigger)(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream, int cmd);

	/* host stream pointer */
	snd_pcm_uframes_t (*pcm_pointer)(struct snd_sof_dev *sdev,
					 struct snd_pcm_substream *substream);

	/* FW loading */
	int (*load_firmware)(struct snd_sof_dev *sof_dev,
			     bool first_boot);
	int (*load_module)(struct snd_sof_dev *sof_dev,
			   struct snd_sof_mod_hdr *hdr);
	int (*fw_ready)(struct snd_sof_dev *sdev, u32 msg_id);

	/* host DMA trace initialization */
	int (*trace_init)(struct snd_sof_dev *sdev, u32 *stream_tag);
	int (*trace_release)(struct snd_sof_dev *sdev);
	int (*trace_trigger)(struct snd_sof_dev *sdev, int cmd);

	/* DAI ops */
	struct snd_soc_dai_driver *drv;
	int num_drv;
};

/* DSP architecture specific callbacks for oops and stack dumps */
struct sof_arch_ops {
	void (*dsp_oops)(struct snd_sof_dev *sdev, void *oops);
	void (*dsp_stack)(struct snd_sof_dev *sdev, void *oops,
			  u32 *stack, u32 stack_words);
};

/* DSP device HW descriptor mapping between bus ID and ops */
struct sof_ops_table {
	const struct sof_dev_desc *desc;
	struct snd_sof_dsp_ops *ops;
	struct platform_device *(*new_data)(struct snd_sof_pdata *pdata);
};

/* FS entry for debug files that can expose DSP memories, registers */
struct snd_sof_dfsentry_io {
	struct dentry *dfsentry;
	size_t size;
	void __iomem *buf;
	struct snd_sof_dev *sdev;
};

struct snd_sof_dfsentry_buf {
	struct dentry *dfsentry;
	size_t size;
	void *buf;
	struct snd_sof_dev *sdev;
};

/* Debug mapping for any DSP memory or registers that can used for debug */
struct snd_sof_debugfs_map {
	const char *name;
	u32 bar;
	u32 offset;
	u32 size;
};

/* mailbox descriptor, used for host <-> DSP IPC */
struct snd_sof_mailbox {
	u32 offset;
	size_t size;
};

/* IPC message descriptor for host <-> DSP IO */
struct snd_sof_ipc_msg {
	struct list_head list;

	/* message data */
	u32 header;
	void *msg_data;
	void *reply_data;
	size_t msg_size;
	size_t reply_size;

	wait_queue_head_t waitq;
	bool complete;
};

/* PCM stream, mapped to FW component  */
struct snd_sof_pcm_stream {
	u32 comp_id;
	struct snd_dma_buffer page_table;
	struct sof_ipc_stream_posn posn;
	struct snd_pcm_substream *substream;
};

/* ASLA SOF PCM device */
struct snd_sof_pcm {
	struct snd_sof_dev *sdev;
	struct snd_soc_tplg_pcm pcm;
	struct snd_sof_pcm_stream stream[2];
	u32 posn_offset[2];
	struct mutex mutex;	/* access mutex */
	struct list_head list;	/* list in sdev pcm list */
	struct snd_pcm_hw_params params[2];
	int restore_stream[2]; /* restore hw_params for paused stream */
};

/* ALSA SOF Kcontrol device */
struct snd_sof_control {
	struct snd_sof_dev *sdev;
	int comp_id;
	int num_channels;
	u32 readback_offset; /* offset to mmaped data if used */
	struct sof_ipc_ctrl_data *control_data;
	u32 size;	/* cdata size */
	enum sof_ipc_ctrl_cmd cmd;
	u32 *volume_table; /* volume table computed from tlv data*/

	struct mutex mutex;	/* access mutex */
	struct list_head list;	/* list in sdev control list */
};

/* ASoC SOF DAPM widget */
struct snd_sof_widget {
	struct snd_sof_dev *sdev;
	int comp_id;
	int pipeline_id;
	int complete;
	int id;

	struct snd_soc_dapm_widget *widget;
	struct mutex mutex;	/* access mutex */
	struct list_head list;	/* list in sdev widget list */

	void *private;		/* core does not touch this */
};

/* ASoC SOF DAPM route */
struct snd_sof_route {
	struct snd_sof_dev *sdev;

	struct snd_soc_dapm_route route;
	struct list_head list;	/* list in sdev route list */

	void *private;
};

/* ASoC DAI device */
struct snd_sof_dai {
	struct snd_sof_dev *sdev;
	const char *name;

	struct sof_ipc_comp_dai comp_dai;
	struct sof_ipc_dai_config dai_config;
	struct list_head list;	/* list in sdev dai list */
};

/*
 * SOF Device Level.
 */
struct snd_sof_dev {
	struct device *dev;
	struct device *parent;
	spinlock_t ipc_lock;	/* lock for IPC users */
	spinlock_t hw_lock;	/* lock for HW IO access */
	struct pci_dev *pci;

	/* ASoC components */
	struct snd_soc_component_driver plat_drv;

	/* DSP firmware boot */
	wait_queue_head_t boot_wait;
	bool boot_complete;

	/* DSP HW differentiation */
	struct snd_sof_pdata *pdata;
	const struct snd_sof_dsp_ops *ops;
	struct sof_intel_hda_dev *hda;	/* for HDA based DSP HW */
	const struct sof_arch_ops *arch_ops;

	/* IPC */
	struct snd_sof_ipc *ipc;
	struct snd_sof_mailbox dsp_box;		/* DSP initiated IPC */
	struct snd_sof_mailbox host_box;	/* Host initiated IPC */
	struct snd_sof_mailbox stream_box;	/* Stream position update */
	u64 irq_status;
	int ipc_irq;
	u32 next_comp_id; /* monotonic - reset during S3 */

	/* memory bases for mmaped DSPs - set by dsp_init() */
	void __iomem *bar[SND_SOF_BARS];	/* DSP base address */
	int mmio_bar;
	int mailbox_bar;
	size_t dsp_oops_offset;

	/* debug */
	struct dentry *debugfs_root;

	/* firmware loader */
	int cl_bar;
	struct snd_dma_buffer dmab;
	struct snd_dma_buffer dmab_bdl;
	struct sof_ipc_fw_ready fw_ready;

	/* topology */
	struct snd_soc_tplg_ops *tplg_ops;
	struct list_head pcm_list;
	struct list_head kcontrol_list;
	struct list_head widget_list;
	struct list_head dai_list;
	struct list_head route_list;
	struct snd_soc_component *component;

	/* FW configuration */
	struct sof_ipc_dma_buffer_data *info_buffer;
	struct sof_ipc_window *info_window;

	/* IPC timeouts in ms */
	int ipc_timeout;
	int boot_timeout;

	/* Wait queue for code loading */
	wait_queue_head_t waitq;
	int code_loading;

	/* DMA for Trace */
	struct snd_dma_buffer dmatb;
	struct snd_dma_buffer dmatp;
	int dma_trace_pages;
	wait_queue_head_t trace_sleep;
	u32 host_offset;
	bool dtrace_is_enabled;
	bool dtrace_error;

	/* PM */
	bool restore_kcontrols; /* restore kcontrols upon resume */

	void *private;			/* core does not touch this */
};

#define sof_to_bus(s)  (&(s)->hda->hbus.core)
#define sof_to_hbus(s) (&(s)->hda->hbus)

/*
 * SOF platform private struct used as drvdata of
 * platform dev (e.g. pci/acpi/spi...) drvdata.
 */
struct sof_platform_priv {
	struct snd_sof_pdata *sof_pdata;
	struct platform_device *pdev_pcm;
};

/*
 * Device Level.
 */
void snd_sof_shutdown(struct device *dev);
int snd_sof_runtime_suspend(struct device *dev);
int snd_sof_runtime_resume(struct device *dev);
int snd_sof_resume(struct device *dev);
int snd_sof_suspend(struct device *dev);
int snd_sof_prepare(struct device *dev);
int snd_sof_suspend_late(struct device *dev);

void snd_sof_new_platform_drv(struct snd_sof_dev *sdev);

int snd_sof_create_page_table(struct snd_sof_dev *sdev,
			      struct snd_dma_buffer *dmab,
			      unsigned char *page_table, size_t size);

/*
 * Firmware loading.
 */
int snd_sof_load_firmware(struct snd_sof_dev *sdev,
			  bool first_boot);
int snd_sof_load_firmware_memcpy(struct snd_sof_dev *sdev,
				 bool first_boot);
int snd_sof_run_firmware(struct snd_sof_dev *sdev);
int snd_sof_parse_module_memcpy(struct snd_sof_dev *sdev,
				struct snd_sof_mod_hdr *module);
void snd_sof_fw_unload(struct snd_sof_dev *sdev);
int snd_sof_fw_parse_ext_data(struct snd_sof_dev *sdev, u32 offset);

/*
 * IPC low level APIs.
 */
struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev);
void snd_sof_ipc_free(struct snd_sof_dev *sdev);
int snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id);
void snd_sof_ipc_msgs_rx(struct snd_sof_dev *sdev);
void snd_sof_ipc_msgs_tx(struct snd_sof_dev *sdev);
int snd_sof_ipc_stream_pcm_params(struct snd_sof_dev *sdev,
				  struct sof_ipc_pcm_params *params);
int snd_sof_dsp_mailbox_init(struct snd_sof_dev *sdev, u32 dspbox,
			     size_t dspbox_size, u32 hostbox,
			     size_t hostbox_size);
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, u32 header, void *tx_data,
		       size_t tx_bytes, void *rx_data, size_t rx_bytes);
struct snd_sof_widget *snd_sof_find_swidget(struct snd_sof_dev *sdev,
					    char *name);
struct snd_sof_dai *snd_sof_find_dai(struct snd_sof_dev *sdev,
				     char *name);
struct snd_sof_pcm *snd_sof_find_spcm_dai(struct snd_sof_dev *sdev,
					  struct snd_soc_pcm_runtime *rtd);
struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_sof_dev *sdev,
					   char *name);
struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_sof_dev *sdev,
					   unsigned int comp_id,
					   int *direction);
struct snd_sof_pcm *snd_sof_find_spcm_pcm_id(struct snd_sof_dev *sdev,
					     unsigned int pcm_id);
void sof_ipc_drop_all(struct snd_sof_ipc *ipc);

/*
 * Stream IPC
 */
int snd_sof_ipc_stream_posn(struct snd_sof_dev *sdev,
			    struct snd_sof_pcm *spcm, int direction,
			    struct sof_ipc_stream_posn *posn);

/*
 * Mixer IPC
 */
int snd_sof_ipc_set_comp_data(struct snd_sof_ipc *ipc,
			      struct snd_sof_control *scontrol, u32 ipc_cmd,
			      enum sof_ipc_ctrl_type ctrl_type,
			      enum sof_ipc_ctrl_cmd ctrl_cmd);
int snd_sof_ipc_get_comp_data(struct snd_sof_ipc *ipc,
			      struct snd_sof_control *scontrol, u32 ipc_cmd,
			      enum sof_ipc_ctrl_type ctrl_type,
			      enum sof_ipc_ctrl_cmd ctrl_cmd);

/*
 * Topology.
 */
int snd_sof_init_topology(struct snd_sof_dev *sdev,
			  struct snd_soc_tplg_ops *ops);
int snd_sof_load_topology(struct snd_sof_dev *sdev, const char *file);
void snd_sof_free_topology(struct snd_sof_dev *sdev);
int snd_sof_complete_pipeline(struct snd_sof_dev *sdev,
			      struct snd_sof_widget *swidget);

/*
 * Trace/debug
 */
int snd_sof_init_trace(struct snd_sof_dev *sdev);
void snd_sof_release_trace(struct snd_sof_dev *sdev);
int snd_sof_dbg_init(struct snd_sof_dev *sdev);
void snd_sof_free_debug(struct snd_sof_dev *sdev);
int snd_sof_debugfs_create_item(struct snd_sof_dev *sdev,
				void __iomem *base, size_t size,
				const char *name);
int snd_sof_trace_update_pos(struct snd_sof_dev *sdev,
			     struct sof_ipc_dma_trace_posn *posn);
void snd_sof_trace_notify_for_error(struct snd_sof_dev *sdev);
int snd_sof_get_status(struct snd_sof_dev *sdev, u32 panic_code,
		       u32 tracep_code, void *oops,
		       struct sof_ipc_panic_info *panic_info,
		       void *stack, size_t stack_size);
int snd_sof_init_trace_ipc(struct snd_sof_dev *sdev);

/*
 * Platform specific ops.
 */
extern struct snd_compr_ops sof_compressed_ops;

/*
 * Kcontrols.
 */

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_enum_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);
int snd_sof_enum_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
int snd_sof_bytes_ext_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *bytes,
			  unsigned int size);
int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *bytes,
			  unsigned int size);

/*
 * DSP Architectures.
 */
static inline void sof_stack(struct snd_sof_dev *sdev, void *oops, u32 *stack,
			     u32 stack_words)
{
	if (sdev->arch_ops->dsp_stack)
		sdev->arch_ops->dsp_stack(sdev, oops, stack, stack_words);
}

static inline void sof_oops(struct snd_sof_dev *sdev, void *oops)
{
	if (sdev->arch_ops->dsp_oops)
		sdev->arch_ops->dsp_oops(sdev, oops);
}

extern const struct sof_arch_ops sof_xtensa_arch_ops;

/*
 * Utilities
 */
int sof_create_platform_device(struct sof_platform_priv *priv);
#endif
