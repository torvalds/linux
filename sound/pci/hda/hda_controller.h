/*
 *  Common functionality for the alsa driver code base for HD Audio.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 */

#ifndef __SOUND_HDA_CONTROLLER_H
#define __SOUND_HDA_CONTROLLER_H

#include <linux/timecounter.h>
#include <linux/interrupt.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include "hda_codec.h"
#include <sound/hda_register.h>

#define AZX_MAX_CODECS		HDA_MAX_CODECS
#define AZX_DEFAULT_CODECS	4

/* driver quirks (capabilities) */
/* bits 0-7 are used for indicating driver type */
#define AZX_DCAPS_NO_TCSEL	(1 << 8)	/* No Intel TCSEL bit */
#define AZX_DCAPS_NO_MSI	(1 << 9)	/* No MSI support */
#define AZX_DCAPS_SNOOP_MASK	(3 << 10)	/* snoop type mask */
#define AZX_DCAPS_SNOOP_OFF	(1 << 12)	/* snoop default off */
#ifdef CONFIG_SND_HDA_I915
#define AZX_DCAPS_I915_COMPONENT (1 << 13)	/* bind with i915 gfx */
#else
#define AZX_DCAPS_I915_COMPONENT 0		/* NOP */
#endif
/* 14 unused */
#define AZX_DCAPS_CTX_WORKAROUND (1 << 15)	/* X-Fi workaround */
#define AZX_DCAPS_POSFIX_LPIB	(1 << 16)	/* Use LPIB as default */
#define AZX_DCAPS_AMD_WORKAROUND (1 << 17)	/* AMD-specific workaround */
#define AZX_DCAPS_NO_64BIT	(1 << 18)	/* No 64bit address */
#define AZX_DCAPS_SYNC_WRITE	(1 << 19)	/* sync each cmd write */
#define AZX_DCAPS_OLD_SSYNC	(1 << 20)	/* Old SSYNC reg for ICH */
#define AZX_DCAPS_NO_ALIGN_BUFSIZE (1 << 21)	/* no buffer size alignment */
/* 22 unused */
#define AZX_DCAPS_4K_BDLE_BOUNDARY (1 << 23)	/* BDLE in 4k boundary */
/* 24 unused */
#define AZX_DCAPS_COUNT_LPIB_DELAY  (1 << 25)	/* Take LPIB as delay */
#define AZX_DCAPS_PM_RUNTIME	(1 << 26)	/* runtime PM support */
#ifdef CONFIG_SND_HDA_I915
#define AZX_DCAPS_I915_POWERWELL (1 << 27)	/* HSW i915 powerwell support */
#else
#define AZX_DCAPS_I915_POWERWELL 0		/* NOP */
#endif
#define AZX_DCAPS_CORBRP_SELF_CLEAR (1 << 28)	/* CORBRP clears itself after reset */
#define AZX_DCAPS_NO_MSI64      (1 << 29)	/* Stick to 32-bit MSIs */
#define AZX_DCAPS_SEPARATE_STREAM_TAG	(1 << 30) /* capture and playback use separate stream tag */

enum {
	AZX_SNOOP_TYPE_NONE,
	AZX_SNOOP_TYPE_SCH,
	AZX_SNOOP_TYPE_ATI,
	AZX_SNOOP_TYPE_NVIDIA,
};

struct azx_dev {
	struct hdac_stream core;

	unsigned int irq_pending:1;
	/*
	 * For VIA:
	 *  A flag to ensure DMA position is 0
	 *  when link position is not greater than FIFO size
	 */
	unsigned int insufficient:1;
	unsigned int wc_marked:1;
};

#define azx_stream(dev)		(&(dev)->core)
#define stream_to_azx_dev(s)	container_of(s, struct azx_dev, core)

struct azx;

/* Functions to read/write to hda registers. */
struct hda_controller_ops {
	/* Disable msi if supported, PCI only */
	int (*disable_msi_reset_irq)(struct azx *);
	int (*substream_alloc_pages)(struct azx *chip,
				     struct snd_pcm_substream *substream,
				     size_t size);
	int (*substream_free_pages)(struct azx *chip,
				    struct snd_pcm_substream *substream);
	void (*pcm_mmap_prepare)(struct snd_pcm_substream *substream,
				 struct vm_area_struct *area);
	/* Check if current position is acceptable */
	int (*position_check)(struct azx *chip, struct azx_dev *azx_dev);
	/* enable/disable the link power */
	int (*link_power)(struct azx *chip, bool enable);
};

struct azx_pcm {
	struct azx *chip;
	struct snd_pcm *pcm;
	struct hda_codec *codec;
	struct hda_pcm *info;
	struct list_head list;
};

typedef unsigned int (*azx_get_pos_callback_t)(struct azx *, struct azx_dev *);
typedef int (*azx_get_delay_callback_t)(struct azx *, struct azx_dev *, unsigned int pos);

struct azx {
	struct hda_bus bus;

	struct snd_card *card;
	struct pci_dev *pci;
	int dev_index;

	/* chip type specific */
	int driver_type;
	unsigned int driver_caps;
	int playback_streams;
	int playback_index_offset;
	int capture_streams;
	int capture_index_offset;
	int num_streams;
	const int *jackpoll_ms; /* per-card jack poll interval */

	/* Register interaction. */
	const struct hda_controller_ops *ops;

	/* position adjustment callbacks */
	azx_get_pos_callback_t get_position[2];
	azx_get_delay_callback_t get_delay[2];

	/* locks */
	struct mutex open_mutex; /* Prevents concurrent open/close operations */

	/* PCM */
	struct list_head pcm_list; /* azx_pcm list */

	/* HD codec */
	int  codec_probe_mask; /* copied from probe_mask option */
	unsigned int beep_mode;

#ifdef CONFIG_SND_HDA_PATCH_LOADER
	const struct firmware *fw;
#endif

	/* flags */
	int bdl_pos_adj;
	int poll_count;
	unsigned int running:1;
	unsigned int fallback_to_single_cmd:1;
	unsigned int single_cmd:1;
	unsigned int polling_mode:1;
	unsigned int msi:1;
	unsigned int probing:1; /* codec probing phase */
	unsigned int snoop:1;
	unsigned int uc_buffer:1; /* non-cached pages for stream buffers */
	unsigned int align_buffer_size:1;
	unsigned int region_requested:1;
	unsigned int disabled:1; /* disabled by vga_switcheroo */

	/* GTS present */
	unsigned int gts_present:1;

#ifdef CONFIG_SND_HDA_DSP_LOADER
	struct azx_dev saved_azx_dev;
#endif
};

#define azx_bus(chip)	(&(chip)->bus.core)
#define bus_to_azx(_bus)	container_of(_bus, struct azx, bus.core)

#ifdef CONFIG_X86
#define azx_snoop(chip)		((chip)->snoop)
#else
#define azx_snoop(chip)		true
#endif

/*
 * macros for easy use
 */

#define azx_writel(chip, reg, value) \
	snd_hdac_chip_writel(azx_bus(chip), reg, value)
#define azx_readl(chip, reg) \
	snd_hdac_chip_readl(azx_bus(chip), reg)
#define azx_writew(chip, reg, value) \
	snd_hdac_chip_writew(azx_bus(chip), reg, value)
#define azx_readw(chip, reg) \
	snd_hdac_chip_readw(azx_bus(chip), reg)
#define azx_writeb(chip, reg, value) \
	snd_hdac_chip_writeb(azx_bus(chip), reg, value)
#define azx_readb(chip, reg) \
	snd_hdac_chip_readb(azx_bus(chip), reg)

#define azx_has_pm_runtime(chip) \
	((chip)->driver_caps & AZX_DCAPS_PM_RUNTIME)

/* PCM setup */
static inline struct azx_dev *get_azx_dev(struct snd_pcm_substream *substream)
{
	return substream->runtime->private_data;
}
unsigned int azx_get_position(struct azx *chip, struct azx_dev *azx_dev);
unsigned int azx_get_pos_lpib(struct azx *chip, struct azx_dev *azx_dev);
unsigned int azx_get_pos_posbuf(struct azx *chip, struct azx_dev *azx_dev);

/* Stream control. */
void azx_stop_all_streams(struct azx *chip);

/* Allocation functions. */
#define azx_alloc_stream_pages(chip) \
	snd_hdac_bus_alloc_stream_pages(azx_bus(chip))
#define azx_free_stream_pages(chip) \
	snd_hdac_bus_free_stream_pages(azx_bus(chip))

/* Low level azx interface */
void azx_init_chip(struct azx *chip, bool full_reset);
void azx_stop_chip(struct azx *chip);
#define azx_enter_link_reset(chip) \
	snd_hdac_bus_enter_link_reset(azx_bus(chip))
irqreturn_t azx_interrupt(int irq, void *dev_id);

/* Codec interface */
int azx_bus_init(struct azx *chip, const char *model,
		 const struct hdac_io_ops *io_ops);
int azx_probe_codecs(struct azx *chip, unsigned int max_slots);
int azx_codec_configure(struct azx *chip);
int azx_init_streams(struct azx *chip);
void azx_free_streams(struct azx *chip);

#endif /* __SOUND_HDA_CONTROLLER_H */
