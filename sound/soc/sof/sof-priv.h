/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 */

#ifndef __SOUND_SOC_SOF_PRIV_H
#define __SOUND_SOC_SOF_PRIV_H

#include <linux/device.h>

#include <sound/hdaudio.h>
#include <sound/soc.h>
#include <sound/control.h>

#include <sound/sof.h>
#include <sound/sof/stream.h> /* needs to be included before control.h */
#include <sound/sof/control.h>
#include <sound/sof/dai.h>
#include <sound/sof/info.h>
#include <sound/sof/pm.h>
#include <sound/sof/topology.h>
#include <sound/sof/trace.h>

#include <uapi/sound/sof/fw.h>

/* debug flags */
#define SOF_DBG_ENABLE_TRACE	BIT(0)
#define SOF_DBG_REGS		BIT(1)
#define SOF_DBG_MBOX		BIT(2)
#define SOF_DBG_TEXT		BIT(3)
#define SOF_DBG_PCI		BIT(4)
#define SOF_DBG_RETAIN_CTX	BIT(5)	/* prevent DSP D3 on FW exception */

/* global debug state set by SOF_DBG_ flags */
extern int sof_core_debug;

/* max BARs mmaped devices can use */
#define SND_SOF_BARS	8

/* time in ms for runtime suspend delay */
#define SND_SOF_SUSPEND_DELAY_MS	2000

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

#define ENABLE_DEBUGFS_CACHEBUF \
	(IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_DEBUGFS_CACHE) || \
	 IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST))

#define DMA_CHAN_INVALID	0xFFFFFFFF

/* DSP D0ix sub-state */
enum sof_d0_substate {
	SOF_DSP_D0I0 = 0,	/* DSP default D0 substate */
	SOF_DSP_D0I3,		/* DSP D0i3(low power) substate*/
};

struct snd_sof_dev;
struct snd_sof_ipc_msg;
struct snd_sof_ipc;
struct snd_sof_debugfs_map;
struct snd_soc_tplg_ops;
struct snd_soc_component;
struct snd_sof_pdata;

/*
 * SOF DSP HW abstraction operations.
 * Used to abstract DSP HW architecture and any IO busses between host CPU
 * and DSP device(s).
 */
struct snd_sof_dsp_ops {

	/* probe and remove */
	int (*probe)(struct snd_sof_dev *sof_dev); /* mandatory */
	int (*remove)(struct snd_sof_dev *sof_dev); /* optional */

	/* DSP core boot / reset */
	int (*run)(struct snd_sof_dev *sof_dev); /* mandatory */
	int (*stall)(struct snd_sof_dev *sof_dev); /* optional */
	int (*reset)(struct snd_sof_dev *sof_dev); /* optional */
	int (*core_power_up)(struct snd_sof_dev *sof_dev,
			     unsigned int core_mask); /* optional */
	int (*core_power_down)(struct snd_sof_dev *sof_dev,
			       unsigned int core_mask); /* optional */

	/*
	 * Register IO: only used by respective drivers themselves,
	 * TODO: consider removing these operations and calling respective
	 * implementations directly
	 */
	void (*write)(struct snd_sof_dev *sof_dev, void __iomem *addr,
		      u32 value); /* optional */
	u32 (*read)(struct snd_sof_dev *sof_dev,
		    void __iomem *addr); /* optional */
	void (*write64)(struct snd_sof_dev *sof_dev, void __iomem *addr,
			u64 value); /* optional */
	u64 (*read64)(struct snd_sof_dev *sof_dev,
		      void __iomem *addr); /* optional */

	/* memcpy IO */
	void (*block_read)(struct snd_sof_dev *sof_dev, u32 bar,
			   u32 offset, void *dest,
			   size_t size); /* mandatory */
	void (*block_write)(struct snd_sof_dev *sof_dev, u32 bar,
			    u32 offset, void *src,
			    size_t size); /* mandatory */

	/* doorbell */
	irqreturn_t (*irq_handler)(int irq, void *context); /* optional */
	irqreturn_t (*irq_thread)(int irq, void *context); /* optional */

	/* ipc */
	int (*send_msg)(struct snd_sof_dev *sof_dev,
			struct snd_sof_ipc_msg *msg); /* mandatory */

	/* FW loading */
	int (*load_firmware)(struct snd_sof_dev *sof_dev); /* mandatory */
	int (*load_module)(struct snd_sof_dev *sof_dev,
			   struct snd_sof_mod_hdr *hdr); /* optional */
	/*
	 * FW ready checks for ABI compatibility and creates
	 * memory windows at first boot
	 */
	int (*fw_ready)(struct snd_sof_dev *sdev, u32 msg_id); /* mandatory */

	/* connect pcm substream to a host stream */
	int (*pcm_open)(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream); /* optional */
	/* disconnect pcm substream to a host stream */
	int (*pcm_close)(struct snd_sof_dev *sdev,
			 struct snd_pcm_substream *substream); /* optional */

	/* host stream hw params */
	int (*pcm_hw_params)(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct sof_ipc_stream_params *ipc_params); /* optional */

	/* host stream hw_free */
	int (*pcm_hw_free)(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream); /* optional */

	/* host stream trigger */
	int (*pcm_trigger)(struct snd_sof_dev *sdev,
			   struct snd_pcm_substream *substream,
			   int cmd); /* optional */

	/* host stream pointer */
	snd_pcm_uframes_t (*pcm_pointer)(struct snd_sof_dev *sdev,
					 struct snd_pcm_substream *substream); /* optional */

	/* host read DSP stream data */
	void (*ipc_msg_data)(struct snd_sof_dev *sdev,
			     struct snd_pcm_substream *substream,
			     void *p, size_t sz); /* mandatory */

	/* host configure DSP HW parameters */
	int (*ipc_pcm_params)(struct snd_sof_dev *sdev,
			      struct snd_pcm_substream *substream,
			      const struct sof_ipc_pcm_params_reply *reply); /* mandatory */

	/* pre/post firmware run */
	int (*pre_fw_run)(struct snd_sof_dev *sof_dev); /* optional */
	int (*post_fw_run)(struct snd_sof_dev *sof_dev); /* optional */

	/* DSP PM */
	int (*suspend)(struct snd_sof_dev *sof_dev); /* optional */
	int (*resume)(struct snd_sof_dev *sof_dev); /* optional */
	int (*runtime_suspend)(struct snd_sof_dev *sof_dev); /* optional */
	int (*runtime_resume)(struct snd_sof_dev *sof_dev); /* optional */
	int (*runtime_idle)(struct snd_sof_dev *sof_dev); /* optional */
	int (*set_hw_params_upon_resume)(struct snd_sof_dev *sdev); /* optional */
	int (*set_power_state)(struct snd_sof_dev *sdev,
			       enum sof_d0_substate d0_substate); /* optional */

	/* DSP clocking */
	int (*set_clk)(struct snd_sof_dev *sof_dev, u32 freq); /* optional */

	/* debug */
	const struct snd_sof_debugfs_map *debug_map; /* optional */
	int debug_map_count; /* optional */
	void (*dbg_dump)(struct snd_sof_dev *sof_dev,
			 u32 flags); /* optional */
	void (*ipc_dump)(struct snd_sof_dev *sof_dev); /* optional */

	/* host DMA trace initialization */
	int (*trace_init)(struct snd_sof_dev *sdev,
			  u32 *stream_tag); /* optional */
	int (*trace_release)(struct snd_sof_dev *sdev); /* optional */
	int (*trace_trigger)(struct snd_sof_dev *sdev,
			     int cmd); /* optional */

	/* misc */
	int (*get_bar_index)(struct snd_sof_dev *sdev,
			     u32 type); /* optional */
	int (*get_mailbox_offset)(struct snd_sof_dev *sdev);/* mandatory for common loader code */
	int (*get_window_offset)(struct snd_sof_dev *sdev,
				 u32 id);/* mandatory for common loader code */

	/* DAI ops */
	struct snd_soc_dai_driver *drv;
	int num_drv;

	/* ALSA HW info flags, will be stored in snd_pcm_runtime.hw.info */
	u32 hw_info;
};

/* DSP architecture specific callbacks for oops and stack dumps */
struct sof_arch_ops {
	void (*dsp_oops)(struct snd_sof_dev *sdev, void *oops);
	void (*dsp_stack)(struct snd_sof_dev *sdev, void *oops,
			  u32 *stack, u32 stack_words);
};

#define sof_arch_ops(sdev) ((sdev)->pdata->desc->arch_ops)

/* DSP device HW descriptor mapping between bus ID and ops */
struct sof_ops_table {
	const struct sof_dev_desc *desc;
	const struct snd_sof_dsp_ops *ops;
};

enum sof_dfsentry_type {
	SOF_DFSENTRY_TYPE_IOMEM = 0,
	SOF_DFSENTRY_TYPE_BUF,
};

enum sof_debugfs_access_type {
	SOF_DEBUGFS_ACCESS_ALWAYS = 0,
	SOF_DEBUGFS_ACCESS_D0_ONLY,
};

/* FS entry for debug files that can expose DSP memories, registers */
struct snd_sof_dfsentry {
	size_t size;
	enum sof_dfsentry_type type;
	/*
	 * access_type specifies if the
	 * memory -> DSP resource (memory, register etc) is always accessible
	 * or if it is accessible only when the DSP is in D0.
	 */
	enum sof_debugfs_access_type access_type;
#if ENABLE_DEBUGFS_CACHEBUF
	char *cache_buf; /* buffer to cache the contents of debugfs memory */
#endif
	struct snd_sof_dev *sdev;
	struct list_head list;  /* list in sdev dfsentry list */
	union {
		void __iomem *io_mem;
		void *buf;
	};
};

/* Debug mapping for any DSP memory or registers that can used for debug */
struct snd_sof_debugfs_map {
	const char *name;
	u32 bar;
	u32 offset;
	u32 size;
	/*
	 * access_type specifies if the memory is always accessible
	 * or if it is accessible only when the DSP is in D0.
	 */
	enum sof_debugfs_access_type access_type;
};

/* mailbox descriptor, used for host <-> DSP IPC */
struct snd_sof_mailbox {
	u32 offset;
	size_t size;
};

/* IPC message descriptor for host <-> DSP IO */
struct snd_sof_ipc_msg {
	/* message data */
	u32 header;
	void *msg_data;
	void *reply_data;
	size_t msg_size;
	size_t reply_size;
	int reply_error;

	wait_queue_head_t waitq;
	bool ipc_complete;
};

/* PCM stream, mapped to FW component  */
struct snd_sof_pcm_stream {
	u32 comp_id;
	struct snd_dma_buffer page_table;
	struct sof_ipc_stream_posn posn;
	struct snd_pcm_substream *substream;
	struct work_struct period_elapsed_work;
	bool d0i3_compatible; /* DSP can be in D0I3 when this pcm is opened */
};

/* ALSA SOF PCM device */
struct snd_sof_pcm {
	struct snd_sof_dev *sdev;
	struct snd_soc_tplg_pcm pcm;
	struct snd_sof_pcm_stream stream[2];
	struct list_head list;	/* list in sdev pcm list */
	struct snd_pcm_hw_params params[2];
	bool prepared[2]; /* PCM_PARAMS set successfully */
};

struct snd_sof_led_control {
	unsigned int use_led;
	unsigned int direction;
	unsigned int led_value;
};

/* ALSA SOF Kcontrol device */
struct snd_sof_control {
	struct snd_sof_dev *sdev;
	int comp_id;
	int min_volume_step; /* min volume step for volume_table */
	int max_volume_step; /* max volume step for volume_table */
	int num_channels;
	u32 readback_offset; /* offset to mmaped data if used */
	struct sof_ipc_ctrl_data *control_data;
	u32 size;	/* cdata size */
	enum sof_ipc_ctrl_cmd cmd;
	u32 *volume_table; /* volume table computed from tlv data*/

	struct list_head list;	/* list in sdev control list */

	struct snd_sof_led_control led_ctl;
};

/* ASoC SOF DAPM widget */
struct snd_sof_widget {
	struct snd_sof_dev *sdev;
	int comp_id;
	int pipeline_id;
	int complete;
	int id;

	struct snd_soc_dapm_widget *widget;
	struct list_head list;	/* list in sdev widget list */

	void *private;		/* core does not touch this */
};

/* ASoC SOF DAPM route */
struct snd_sof_route {
	struct snd_sof_dev *sdev;

	struct snd_soc_dapm_route *route;
	struct list_head list;	/* list in sdev route list */

	void *private;
};

/* ASoC DAI device */
struct snd_sof_dai {
	struct snd_sof_dev *sdev;
	const char *name;
	const char *cpu_dai_name;

	struct sof_ipc_comp_dai comp_dai;
	struct sof_ipc_dai_config *dai_config;
	struct list_head list;	/* list in sdev dai list */
};

/*
 * SOF Device Level.
 */
struct snd_sof_dev {
	struct device *dev;
	spinlock_t ipc_lock;	/* lock for IPC users */
	spinlock_t hw_lock;	/* lock for HW IO access */

	/*
	 * ASoC components. plat_drv fields are set dynamically so
	 * can't use const
	 */
	struct snd_soc_component_driver plat_drv;

	/* power states related */
	enum sof_d0_substate d0_substate;
	/* flag to track if the intended power target of suspend is S0ix */
	bool s0_suspend;

	/* DSP firmware boot */
	wait_queue_head_t boot_wait;
	u32 boot_complete;
	u32 first_boot;

	/* work queue in case the probe is implemented in two steps */
	struct work_struct probe_work;

	/* DSP HW differentiation */
	struct snd_sof_pdata *pdata;

	/* IPC */
	struct snd_sof_ipc *ipc;
	struct snd_sof_mailbox dsp_box;		/* DSP initiated IPC */
	struct snd_sof_mailbox host_box;	/* Host initiated IPC */
	struct snd_sof_mailbox stream_box;	/* Stream position update */
	struct snd_sof_ipc_msg *msg;
	int ipc_irq;
	u32 next_comp_id; /* monotonic - reset during S3 */

	/* memory bases for mmaped DSPs - set by dsp_init() */
	void __iomem *bar[SND_SOF_BARS];	/* DSP base address */
	int mmio_bar;
	int mailbox_bar;
	size_t dsp_oops_offset;

	/* debug */
	struct dentry *debugfs_root;
	struct list_head dfsentry_list;

	/* firmware loader */
	struct snd_dma_buffer dmab;
	struct snd_dma_buffer dmab_bdl;
	struct sof_ipc_fw_ready fw_ready;
	struct sof_ipc_fw_version fw_version;

	/* topology */
	struct snd_soc_tplg_ops *tplg_ops;
	struct list_head pcm_list;
	struct list_head kcontrol_list;
	struct list_head widget_list;
	struct list_head dai_list;
	struct list_head route_list;
	struct snd_soc_component *component;
	u32 enabled_cores_mask; /* keep track of enabled cores */

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
	u32 dtrace_is_supported; /* set with Kconfig or module parameter */
	u32 dtrace_is_enabled;
	u32 dtrace_error;
	u32 dtrace_draining;

	bool msi_enabled;

	void *private;			/* core does not touch this */
};

/*
 * Device Level.
 */

int snd_sof_device_probe(struct device *dev, struct snd_sof_pdata *plat_data);
int snd_sof_device_remove(struct device *dev);

int snd_sof_runtime_suspend(struct device *dev);
int snd_sof_runtime_resume(struct device *dev);
int snd_sof_runtime_idle(struct device *dev);
int snd_sof_resume(struct device *dev);
int snd_sof_suspend(struct device *dev);
int snd_sof_set_d0_substate(struct snd_sof_dev *sdev,
			    enum sof_d0_substate d0_substate);

void snd_sof_new_platform_drv(struct snd_sof_dev *sdev);

int snd_sof_create_page_table(struct snd_sof_dev *sdev,
			      struct snd_dma_buffer *dmab,
			      unsigned char *page_table, size_t size);

/*
 * Firmware loading.
 */
int snd_sof_load_firmware(struct snd_sof_dev *sdev);
int snd_sof_load_firmware_raw(struct snd_sof_dev *sdev);
int snd_sof_load_firmware_memcpy(struct snd_sof_dev *sdev);
int snd_sof_run_firmware(struct snd_sof_dev *sdev);
int snd_sof_parse_module_memcpy(struct snd_sof_dev *sdev,
				struct snd_sof_mod_hdr *module);
void snd_sof_fw_unload(struct snd_sof_dev *sdev);
int snd_sof_fw_parse_ext_data(struct snd_sof_dev *sdev, u32 bar, u32 offset);

/*
 * IPC low level APIs.
 */
struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev);
void snd_sof_ipc_free(struct snd_sof_dev *sdev);
int snd_sof_ipc_reply(struct snd_sof_dev *sdev, u32 msg_id);
void snd_sof_ipc_msgs_rx(struct snd_sof_dev *sdev);
int snd_sof_ipc_stream_pcm_params(struct snd_sof_dev *sdev,
				  struct sof_ipc_pcm_params *params);
int snd_sof_dsp_mailbox_init(struct snd_sof_dev *sdev, u32 dspbox,
			     size_t dspbox_size, u32 hostbox,
			     size_t hostbox_size);
int snd_sof_ipc_valid(struct snd_sof_dev *sdev);
int sof_ipc_tx_message(struct snd_sof_ipc *ipc, u32 header,
		       void *msg_data, size_t msg_bytes, void *reply_data,
		       size_t reply_bytes);
struct snd_sof_widget *snd_sof_find_swidget(struct snd_sof_dev *sdev,
					    const char *name);
struct snd_sof_widget *snd_sof_find_swidget_sname(struct snd_sof_dev *sdev,
						  const char *pcm_name,
						  int dir);
struct snd_sof_dai *snd_sof_find_dai(struct snd_sof_dev *sdev,
				     const char *name);

static inline
struct snd_sof_pcm *snd_sof_find_spcm_dai(struct snd_sof_dev *sdev,
					  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (le32_to_cpu(spcm->pcm.dai_id) == rtd->dai_link->id)
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_sof_dev *sdev,
					   const char *name);
struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_sof_dev *sdev,
					   unsigned int comp_id,
					   int *direction);
struct snd_sof_pcm *snd_sof_find_spcm_pcm_id(struct snd_sof_dev *sdev,
					     unsigned int pcm_id);
void snd_sof_pcm_period_elapsed(struct snd_pcm_substream *substream);

/*
 * Stream IPC
 */
int snd_sof_ipc_stream_posn(struct snd_sof_dev *sdev,
			    struct snd_sof_pcm *spcm, int direction,
			    struct sof_ipc_stream_posn *posn);

/*
 * Mixer IPC
 */
int snd_sof_ipc_set_get_comp_data(struct snd_sof_ipc *ipc,
				  struct snd_sof_control *scontrol, u32 ipc_cmd,
				  enum sof_ipc_ctrl_type ctrl_type,
				  enum sof_ipc_ctrl_cmd ctrl_cmd,
				  bool send);

/*
 * Topology.
 * There is no snd_sof_free_topology since topology components will
 * be freed by snd_soc_unregister_component,
 */
int snd_sof_init_topology(struct snd_sof_dev *sdev,
			  struct snd_soc_tplg_ops *ops);
int snd_sof_load_topology(struct snd_sof_dev *sdev, const char *file);
int snd_sof_complete_pipeline(struct snd_sof_dev *sdev,
			      struct snd_sof_widget *swidget);

int sof_load_pipeline_ipc(struct snd_sof_dev *sdev,
			  struct sof_ipc_pipe_new *pipeline,
			  struct sof_ipc_comp_reply *r);

/*
 * Trace/debug
 */
int snd_sof_init_trace(struct snd_sof_dev *sdev);
void snd_sof_release_trace(struct snd_sof_dev *sdev);
void snd_sof_free_trace(struct snd_sof_dev *sdev);
int snd_sof_dbg_init(struct snd_sof_dev *sdev);
void snd_sof_free_debug(struct snd_sof_dev *sdev);
int snd_sof_debugfs_io_item(struct snd_sof_dev *sdev,
			    void __iomem *base, size_t size,
			    const char *name,
			    enum sof_debugfs_access_type access_type);
int snd_sof_debugfs_buf_item(struct snd_sof_dev *sdev,
			     void *base, size_t size,
			     const char *name, mode_t mode);
int snd_sof_trace_update_pos(struct snd_sof_dev *sdev,
			     struct sof_ipc_dma_trace_posn *posn);
void snd_sof_trace_notify_for_error(struct snd_sof_dev *sdev);
void snd_sof_get_status(struct snd_sof_dev *sdev, u32 panic_code,
			u32 tracep_code, void *oops,
			struct sof_ipc_panic_info *panic_info,
			void *stack, size_t stack_words);
int snd_sof_init_trace_ipc(struct snd_sof_dev *sdev);
void snd_sof_handle_fw_exception(struct snd_sof_dev *sdev);

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
int snd_sof_switch_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol);
int snd_sof_switch_put(struct snd_kcontrol *kcontrol,
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
			  const unsigned int __user *binary_data,
			  unsigned int size);
int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *binary_data,
			  unsigned int size);

/*
 * DSP Architectures.
 */
static inline void sof_stack(struct snd_sof_dev *sdev, void *oops, u32 *stack,
			     u32 stack_words)
{
	if (sof_arch_ops(sdev)->dsp_stack)
		sof_arch_ops(sdev)->dsp_stack(sdev, oops, stack, stack_words);
}

static inline void sof_oops(struct snd_sof_dev *sdev, void *oops)
{
	if (sof_arch_ops(sdev)->dsp_oops)
		sof_arch_ops(sdev)->dsp_oops(sdev, oops);
}

extern const struct sof_arch_ops sof_xtensa_arch_ops;

/*
 * Utilities
 */
void sof_io_write(struct snd_sof_dev *sdev, void __iomem *addr, u32 value);
void sof_io_write64(struct snd_sof_dev *sdev, void __iomem *addr, u64 value);
u32 sof_io_read(struct snd_sof_dev *sdev, void __iomem *addr);
u64 sof_io_read64(struct snd_sof_dev *sdev, void __iomem *addr);
void sof_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
		       void *message, size_t bytes);
void sof_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
		      void *message, size_t bytes);
void sof_block_write(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *src,
		     size_t size);
void sof_block_read(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *dest,
		    size_t size);

int sof_fw_ready(struct snd_sof_dev *sdev, u32 msg_id);

void intel_ipc_msg_data(struct snd_sof_dev *sdev,
			struct snd_pcm_substream *substream,
			void *p, size_t sz);
int intel_ipc_pcm_params(struct snd_sof_dev *sdev,
			 struct snd_pcm_substream *substream,
			 const struct sof_ipc_pcm_params_reply *reply);

int intel_pcm_open(struct snd_sof_dev *sdev,
		   struct snd_pcm_substream *substream);
int intel_pcm_close(struct snd_sof_dev *sdev,
		    struct snd_pcm_substream *substream);

#endif
