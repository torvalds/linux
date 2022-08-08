// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bt87x.c - Brooktree Bt878/Bt879 driver for ALSA
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 * based on btaudio.c by Gerd Knorr <kraxel@bytesex.org>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("Brooktree Bt87x audio driver");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -2}; /* Exclude the first card */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int digital_rate[SNDRV_CARDS];	/* digital input rate */
static bool load_all;	/* allow to load cards not the allowlist */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Bt87x soundcard");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Bt87x soundcard");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Bt87x soundcard");
module_param_array(digital_rate, int, NULL, 0444);
MODULE_PARM_DESC(digital_rate, "Digital input rate for Bt87x soundcard");
module_param(load_all, bool, 0444);
MODULE_PARM_DESC(load_all, "Allow to load cards not on the allowlist");


/* register offsets */
#define REG_INT_STAT		0x100	/* interrupt status */
#define REG_INT_MASK		0x104	/* interrupt mask */
#define REG_GPIO_DMA_CTL	0x10c	/* audio control */
#define REG_PACKET_LEN		0x110	/* audio packet lengths */
#define REG_RISC_STRT_ADD	0x114	/* RISC program start address */
#define REG_RISC_COUNT		0x120	/* RISC program counter */

/* interrupt bits */
#define INT_OFLOW	(1 <<  3)	/* audio A/D overflow */
#define INT_RISCI	(1 << 11)	/* RISC instruction IRQ bit set */
#define INT_FBUS	(1 << 12)	/* FIFO overrun due to bus access latency */
#define INT_FTRGT	(1 << 13)	/* FIFO overrun due to target latency */
#define INT_FDSR	(1 << 14)	/* FIFO data stream resynchronization */
#define INT_PPERR	(1 << 15)	/* PCI parity error */
#define INT_RIPERR	(1 << 16)	/* RISC instruction parity error */
#define INT_PABORT	(1 << 17)	/* PCI master or target abort */
#define INT_OCERR	(1 << 18)	/* invalid opcode */
#define INT_SCERR	(1 << 19)	/* sync counter overflow */
#define INT_RISC_EN	(1 << 27)	/* DMA controller running */
#define INT_RISCS_SHIFT	      28	/* RISC status bits */

/* audio control bits */
#define CTL_FIFO_ENABLE		(1 <<  0)	/* enable audio data FIFO */
#define CTL_RISC_ENABLE		(1 <<  1)	/* enable audio DMA controller */
#define CTL_PKTP_4		(0 <<  2)	/* packet mode FIFO trigger point - 4 DWORDs */
#define CTL_PKTP_8		(1 <<  2)	/* 8 DWORDs */
#define CTL_PKTP_16		(2 <<  2)	/* 16 DWORDs */
#define CTL_ACAP_EN		(1 <<  4)	/* enable audio capture */
#define CTL_DA_APP		(1 <<  5)	/* GPIO input */
#define CTL_DA_IOM_AFE		(0 <<  6)	/* audio A/D input */
#define CTL_DA_IOM_DA		(1 <<  6)	/* digital audio input */
#define CTL_DA_SDR_SHIFT	       8	/* DDF first stage decimation rate */
#define CTL_DA_SDR_MASK		(0xf<< 8)
#define CTL_DA_LMT		(1 << 12)	/* limit audio data values */
#define CTL_DA_ES2		(1 << 13)	/* enable DDF stage 2 */
#define CTL_DA_SBR		(1 << 14)	/* samples rounded to 8 bits */
#define CTL_DA_DPM		(1 << 15)	/* data packet mode */
#define CTL_DA_LRD_SHIFT	      16	/* ALRCK delay */
#define CTL_DA_MLB		(1 << 21)	/* MSB/LSB format */
#define CTL_DA_LRI		(1 << 22)	/* left/right indication */
#define CTL_DA_SCE		(1 << 23)	/* sample clock edge */
#define CTL_A_SEL_STV		(0 << 24)	/* TV tuner audio input */
#define CTL_A_SEL_SFM		(1 << 24)	/* FM audio input */
#define CTL_A_SEL_SML		(2 << 24)	/* mic/line audio input */
#define CTL_A_SEL_SMXC		(3 << 24)	/* MUX bypass */
#define CTL_A_SEL_SHIFT		      24
#define CTL_A_SEL_MASK		(3 << 24)
#define CTL_A_PWRDN		(1 << 26)	/* analog audio power-down */
#define CTL_A_G2X		(1 << 27)	/* audio gain boost */
#define CTL_A_GAIN_SHIFT	      28	/* audio input gain */
#define CTL_A_GAIN_MASK		(0xf<<28)

/* RISC instruction opcodes */
#define RISC_WRITE	(0x1 << 28)	/* write FIFO data to memory at address */
#define RISC_WRITEC	(0x5 << 28)	/* write FIFO data to memory at current address */
#define RISC_SKIP	(0x2 << 28)	/* skip FIFO data */
#define RISC_JUMP	(0x7 << 28)	/* jump to address */
#define RISC_SYNC	(0x8 << 28)	/* synchronize with FIFO */

/* RISC instruction bits */
#define RISC_BYTES_ENABLE	(0xf << 12)	/* byte enable bits */
#define RISC_RESYNC		(  1 << 15)	/* disable FDSR errors */
#define RISC_SET_STATUS_SHIFT	        16	/* set status bits */
#define RISC_RESET_STATUS_SHIFT	        20	/* clear status bits */
#define RISC_IRQ		(  1 << 24)	/* interrupt */
#define RISC_EOL		(  1 << 26)	/* end of line */
#define RISC_SOL		(  1 << 27)	/* start of line */

/* SYNC status bits values */
#define RISC_SYNC_FM1	0x6
#define RISC_SYNC_VRO	0xc

#define ANALOG_CLOCK 1792000
#ifdef CONFIG_SND_BT87X_OVERCLOCK
#define CLOCK_DIV_MIN 1
#else
#define CLOCK_DIV_MIN 4
#endif
#define CLOCK_DIV_MAX 15

#define ERROR_INTERRUPTS (INT_FBUS | INT_FTRGT | INT_PPERR | \
			  INT_RIPERR | INT_PABORT | INT_OCERR)
#define MY_INTERRUPTS (INT_RISCI | ERROR_INTERRUPTS)

/* SYNC, one WRITE per line, one extra WRITE per page boundary, SYNC, JUMP */
#define MAX_RISC_SIZE ((1 + 255 + (PAGE_ALIGN(255 * 4092) / PAGE_SIZE - 1) + 1 + 1) * 8)

/* Cards with configuration information */
enum snd_bt87x_boardid {
	SND_BT87X_BOARD_UNKNOWN,
	SND_BT87X_BOARD_GENERIC,	/* both an & dig interfaces, 32kHz */
	SND_BT87X_BOARD_ANALOG,		/* board with no external A/D */
	SND_BT87X_BOARD_OSPREY2x0,
	SND_BT87X_BOARD_OSPREY440,
	SND_BT87X_BOARD_AVPHONE98,
};

/* Card configuration */
struct snd_bt87x_board {
	int dig_rate;		/* Digital input sampling rate */
	u32 digital_fmt;	/* Register settings for digital input */
	unsigned no_analog:1;	/* No analog input */
	unsigned no_digital:1;	/* No digital input */
};

static const struct snd_bt87x_board snd_bt87x_boards[] = {
	[SND_BT87X_BOARD_UNKNOWN] = {
		.dig_rate = 32000, /* just a guess */
	},
	[SND_BT87X_BOARD_GENERIC] = {
		.dig_rate = 32000,
	},
	[SND_BT87X_BOARD_ANALOG] = {
		.no_digital = 1,
	},
	[SND_BT87X_BOARD_OSPREY2x0] = {
		.dig_rate = 44100,
		.digital_fmt = CTL_DA_LRI | (1 << CTL_DA_LRD_SHIFT),
	},
	[SND_BT87X_BOARD_OSPREY440] = {
		.dig_rate = 32000,
		.digital_fmt = CTL_DA_LRI | (1 << CTL_DA_LRD_SHIFT),
		.no_analog = 1,
	},
	[SND_BT87X_BOARD_AVPHONE98] = {
		.dig_rate = 48000,
	},
};

struct snd_bt87x {
	struct snd_card *card;
	struct pci_dev *pci;
	struct snd_bt87x_board board;

	void __iomem *mmio;
	int irq;

	spinlock_t reg_lock;
	unsigned long opened;
	struct snd_pcm_substream *substream;

	struct snd_dma_buffer dma_risc;
	unsigned int line_bytes;
	unsigned int lines;

	u32 reg_control;
	u32 interrupt_mask;

	int current_line;

	int pci_parity_errors;
};

enum { DEVICE_DIGITAL, DEVICE_ANALOG };

static inline u32 snd_bt87x_readl(struct snd_bt87x *chip, u32 reg)
{
	return readl(chip->mmio + reg);
}

static inline void snd_bt87x_writel(struct snd_bt87x *chip, u32 reg, u32 value)
{
	writel(value, chip->mmio + reg);
}

static int snd_bt87x_create_risc(struct snd_bt87x *chip, struct snd_pcm_substream *substream,
			       	 unsigned int periods, unsigned int period_bytes)
{
	unsigned int i, offset;
	__le32 *risc;

	if (chip->dma_risc.area == NULL) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, &chip->pci->dev,
					PAGE_ALIGN(MAX_RISC_SIZE), &chip->dma_risc) < 0)
			return -ENOMEM;
	}
	risc = (__le32 *)chip->dma_risc.area;
	offset = 0;
	*risc++ = cpu_to_le32(RISC_SYNC | RISC_SYNC_FM1);
	*risc++ = cpu_to_le32(0);
	for (i = 0; i < periods; ++i) {
		u32 rest;

		rest = period_bytes;
		do {
			u32 cmd, len;
			unsigned int addr;

			len = PAGE_SIZE - (offset % PAGE_SIZE);
			if (len > rest)
				len = rest;
			cmd = RISC_WRITE | len;
			if (rest == period_bytes) {
				u32 block = i * 16 / periods;
				cmd |= RISC_SOL;
				cmd |= block << RISC_SET_STATUS_SHIFT;
				cmd |= (~block & 0xf) << RISC_RESET_STATUS_SHIFT;
			}
			if (len == rest)
				cmd |= RISC_EOL | RISC_IRQ;
			*risc++ = cpu_to_le32(cmd);
			addr = snd_pcm_sgbuf_get_addr(substream, offset);
			*risc++ = cpu_to_le32(addr);
			offset += len;
			rest -= len;
		} while (rest > 0);
	}
	*risc++ = cpu_to_le32(RISC_SYNC | RISC_SYNC_VRO);
	*risc++ = cpu_to_le32(0);
	*risc++ = cpu_to_le32(RISC_JUMP);
	*risc++ = cpu_to_le32(chip->dma_risc.addr);
	chip->line_bytes = period_bytes;
	chip->lines = periods;
	return 0;
}

static void snd_bt87x_free_risc(struct snd_bt87x *chip)
{
	if (chip->dma_risc.area) {
		snd_dma_free_pages(&chip->dma_risc);
		chip->dma_risc.area = NULL;
	}
}

static void snd_bt87x_pci_error(struct snd_bt87x *chip, unsigned int status)
{
	int pci_status = pci_status_get_and_clear_errors(chip->pci);

	if (pci_status != PCI_STATUS_DETECTED_PARITY)
		dev_err(chip->card->dev,
			"Aieee - PCI error! status %#08x, PCI status %#04x\n",
			   status & ERROR_INTERRUPTS, pci_status);
	else {
		dev_err(chip->card->dev,
			"Aieee - PCI parity error detected!\n");
		/* error 'handling' similar to aic7xxx_pci.c: */
		chip->pci_parity_errors++;
		if (chip->pci_parity_errors > 20) {
			dev_err(chip->card->dev,
				"Too many PCI parity errors observed.\n");
			dev_err(chip->card->dev,
				"Some device on this bus is generating bad parity.\n");
			dev_err(chip->card->dev,
				"This is an error *observed by*, not *generated by*, this card.\n");
			dev_err(chip->card->dev,
				"PCI parity error checking has been disabled.\n");
			chip->interrupt_mask &= ~(INT_PPERR | INT_RIPERR);
			snd_bt87x_writel(chip, REG_INT_MASK, chip->interrupt_mask);
		}
	}
}

static irqreturn_t snd_bt87x_interrupt(int irq, void *dev_id)
{
	struct snd_bt87x *chip = dev_id;
	unsigned int status, irq_status;

	status = snd_bt87x_readl(chip, REG_INT_STAT);
	irq_status = status & chip->interrupt_mask;
	if (!irq_status)
		return IRQ_NONE;
	snd_bt87x_writel(chip, REG_INT_STAT, irq_status);

	if (irq_status & ERROR_INTERRUPTS) {
		if (irq_status & (INT_FBUS | INT_FTRGT))
			dev_warn(chip->card->dev,
				 "FIFO overrun, status %#08x\n", status);
		if (irq_status & INT_OCERR)
			dev_err(chip->card->dev,
				"internal RISC error, status %#08x\n", status);
		if (irq_status & (INT_PPERR | INT_RIPERR | INT_PABORT))
			snd_bt87x_pci_error(chip, irq_status);
	}
	if ((irq_status & INT_RISCI) && (chip->reg_control & CTL_ACAP_EN)) {
		int current_block, irq_block;

		/* assume that exactly one line has been recorded */
		chip->current_line = (chip->current_line + 1) % chip->lines;
		/* but check if some interrupts have been skipped */
		current_block = chip->current_line * 16 / chip->lines;
		irq_block = status >> INT_RISCS_SHIFT;
		if (current_block != irq_block)
			chip->current_line = DIV_ROUND_UP(irq_block * chip->lines,
							  16);

		snd_pcm_period_elapsed(chip->substream);
	}
	return IRQ_HANDLED;
}

static const struct snd_pcm_hardware snd_bt87x_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = 0, /* set at runtime */
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 255 * 4092,
	.period_bytes_min = 32,
	.period_bytes_max = 4092,
	.periods_min = 2,
	.periods_max = 255,
};

static const struct snd_pcm_hardware snd_bt87x_analog_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,
	.rates = SNDRV_PCM_RATE_KNOT,
	.rate_min = ANALOG_CLOCK / CLOCK_DIV_MAX,
	.rate_max = ANALOG_CLOCK / CLOCK_DIV_MIN,
	.channels_min = 1,
	.channels_max = 1,
	.buffer_bytes_max = 255 * 4092,
	.period_bytes_min = 32,
	.period_bytes_max = 4092,
	.periods_min = 2,
	.periods_max = 255,
};

static int snd_bt87x_set_digital_hw(struct snd_bt87x *chip, struct snd_pcm_runtime *runtime)
{
	chip->reg_control |= CTL_DA_IOM_DA | CTL_A_PWRDN;
	runtime->hw = snd_bt87x_digital_hw;
	runtime->hw.rates = snd_pcm_rate_to_rate_bit(chip->board.dig_rate);
	runtime->hw.rate_min = chip->board.dig_rate;
	runtime->hw.rate_max = chip->board.dig_rate;
	return 0;
}

static int snd_bt87x_set_analog_hw(struct snd_bt87x *chip, struct snd_pcm_runtime *runtime)
{
	static const struct snd_ratnum analog_clock = {
		.num = ANALOG_CLOCK,
		.den_min = CLOCK_DIV_MIN,
		.den_max = CLOCK_DIV_MAX,
		.den_step = 1
	};
	static const struct snd_pcm_hw_constraint_ratnums constraint_rates = {
		.nrats = 1,
		.rats = &analog_clock
	};

	chip->reg_control &= ~(CTL_DA_IOM_DA | CTL_A_PWRDN);
	runtime->hw = snd_bt87x_analog_hw;
	return snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					     &constraint_rates);
}

static int snd_bt87x_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	if (test_and_set_bit(0, &chip->opened))
		return -EBUSY;

	if (substream->pcm->device == DEVICE_DIGITAL)
		err = snd_bt87x_set_digital_hw(chip, runtime);
	else
		err = snd_bt87x_set_analog_hw(chip, runtime);
	if (err < 0)
		goto _error;

	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		goto _error;

	chip->substream = substream;
	return 0;

_error:
	clear_bit(0, &chip->opened);
	smp_mb__after_atomic();
	return err;
}

static int snd_bt87x_close(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	chip->reg_control |= CTL_A_PWRDN;
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	spin_unlock_irq(&chip->reg_lock);

	chip->substream = NULL;
	clear_bit(0, &chip->opened);
	smp_mb__after_atomic();
	return 0;
}

static int snd_bt87x_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	return snd_bt87x_create_risc(chip, substream,
				     params_periods(hw_params),
				     params_period_bytes(hw_params));
}

static int snd_bt87x_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	snd_bt87x_free_risc(chip);
	return 0;
}

static int snd_bt87x_prepare(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int decimation;

	spin_lock_irq(&chip->reg_lock);
	chip->reg_control &= ~(CTL_DA_SDR_MASK | CTL_DA_SBR);
	decimation = (ANALOG_CLOCK + runtime->rate / 4) / runtime->rate;
	chip->reg_control |= decimation << CTL_DA_SDR_SHIFT;
	if (runtime->format == SNDRV_PCM_FORMAT_S8)
		chip->reg_control |= CTL_DA_SBR;
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_bt87x_start(struct snd_bt87x *chip)
{
	spin_lock(&chip->reg_lock);
	chip->current_line = 0;
	chip->reg_control |= CTL_FIFO_ENABLE | CTL_RISC_ENABLE | CTL_ACAP_EN;
	snd_bt87x_writel(chip, REG_RISC_STRT_ADD, chip->dma_risc.addr);
	snd_bt87x_writel(chip, REG_PACKET_LEN,
			 chip->line_bytes | (chip->lines << 16));
	snd_bt87x_writel(chip, REG_INT_MASK, chip->interrupt_mask);
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	spin_unlock(&chip->reg_lock);
	return 0;
}

static int snd_bt87x_stop(struct snd_bt87x *chip)
{
	spin_lock(&chip->reg_lock);
	chip->reg_control &= ~(CTL_FIFO_ENABLE | CTL_RISC_ENABLE | CTL_ACAP_EN);
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	snd_bt87x_writel(chip, REG_INT_MASK, 0);
	snd_bt87x_writel(chip, REG_INT_STAT, MY_INTERRUPTS);
	spin_unlock(&chip->reg_lock);
	return 0;
}

static int snd_bt87x_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		return snd_bt87x_start(chip);
	case SNDRV_PCM_TRIGGER_STOP:
		return snd_bt87x_stop(chip);
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t snd_bt87x_pointer(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	return (snd_pcm_uframes_t)bytes_to_frames(runtime, chip->current_line * chip->line_bytes);
}

static const struct snd_pcm_ops snd_bt87x_pcm_ops = {
	.open = snd_bt87x_pcm_open,
	.close = snd_bt87x_close,
	.hw_params = snd_bt87x_hw_params,
	.hw_free = snd_bt87x_hw_free,
	.prepare = snd_bt87x_prepare,
	.trigger = snd_bt87x_trigger,
	.pointer = snd_bt87x_pointer,
};

static int snd_bt87x_capture_volume_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 15;
	return 0;
}

static int snd_bt87x_capture_volume_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);

	value->value.integer.value[0] = (chip->reg_control & CTL_A_GAIN_MASK) >> CTL_A_GAIN_SHIFT;
	return 0;
}

static int snd_bt87x_capture_volume_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);
	u32 old_control;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	old_control = chip->reg_control;
	chip->reg_control = (chip->reg_control & ~CTL_A_GAIN_MASK)
		| (value->value.integer.value[0] << CTL_A_GAIN_SHIFT);
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	changed = old_control != chip->reg_control;
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}

static const struct snd_kcontrol_new snd_bt87x_capture_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.info = snd_bt87x_capture_volume_info,
	.get = snd_bt87x_capture_volume_get,
	.put = snd_bt87x_capture_volume_put,
};

#define snd_bt87x_capture_boost_info	snd_ctl_boolean_mono_info

static int snd_bt87x_capture_boost_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);

	value->value.integer.value[0] = !! (chip->reg_control & CTL_A_G2X);
	return 0;
}

static int snd_bt87x_capture_boost_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);
	u32 old_control;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	old_control = chip->reg_control;
	chip->reg_control = (chip->reg_control & ~CTL_A_G2X)
		| (value->value.integer.value[0] ? CTL_A_G2X : 0);
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	changed = chip->reg_control != old_control;
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}

static const struct snd_kcontrol_new snd_bt87x_capture_boost = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Boost",
	.info = snd_bt87x_capture_boost_info,
	.get = snd_bt87x_capture_boost_get,
	.put = snd_bt87x_capture_boost_put,
};

static int snd_bt87x_capture_source_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *info)
{
	static const char *const texts[3] = {"TV Tuner", "FM", "Mic/Line"};

	return snd_ctl_enum_info(info, 1, 3, texts);
}

static int snd_bt87x_capture_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);

	value->value.enumerated.item[0] = (chip->reg_control & CTL_A_SEL_MASK) >> CTL_A_SEL_SHIFT;
	return 0;
}

static int snd_bt87x_capture_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *value)
{
	struct snd_bt87x *chip = snd_kcontrol_chip(kcontrol);
	u32 old_control;
	int changed;

	spin_lock_irq(&chip->reg_lock);
	old_control = chip->reg_control;
	chip->reg_control = (chip->reg_control & ~CTL_A_SEL_MASK)
		| (value->value.enumerated.item[0] << CTL_A_SEL_SHIFT);
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	changed = chip->reg_control != old_control;
	spin_unlock_irq(&chip->reg_lock);
	return changed;
}

static const struct snd_kcontrol_new snd_bt87x_capture_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_bt87x_capture_source_info,
	.get = snd_bt87x_capture_source_get,
	.put = snd_bt87x_capture_source_put,
};

static int snd_bt87x_free(struct snd_bt87x *chip)
{
	if (chip->mmio)
		snd_bt87x_stop(chip);
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	iounmap(chip->mmio);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

static int snd_bt87x_dev_free(struct snd_device *device)
{
	struct snd_bt87x *chip = device->device_data;
	return snd_bt87x_free(chip);
}

static int snd_bt87x_pcm(struct snd_bt87x *chip, int device, char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_bt87x_pcm_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV_SG,
				       &chip->pci->dev,
				       128 * 1024,
				       ALIGN(255 * 4092, 1024));
	return 0;
}

static int snd_bt87x_create(struct snd_card *card,
			    struct pci_dev *pci,
			    struct snd_bt87x **rchip)
{
	struct snd_bt87x *chip;
	int err;
	static const struct snd_device_ops ops = {
		.dev_free = snd_bt87x_dev_free
	};

	*rchip = NULL;

	err = pci_enable_device(pci);
	if (err < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	spin_lock_init(&chip->reg_lock);

	if ((err = pci_request_regions(pci, "Bt87x audio")) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}
	chip->mmio = pci_ioremap_bar(pci, 0);
	if (!chip->mmio) {
		dev_err(card->dev, "cannot remap io memory\n");
		err = -ENOMEM;
		goto fail;
	}

	chip->reg_control = CTL_A_PWRDN | CTL_DA_ES2 |
			    CTL_PKTP_16 | (15 << CTL_DA_SDR_SHIFT);
	chip->interrupt_mask = MY_INTERRUPTS;
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	snd_bt87x_writel(chip, REG_INT_MASK, 0);
	snd_bt87x_writel(chip, REG_INT_STAT, MY_INTERRUPTS);

	err = request_irq(pci->irq, snd_bt87x_interrupt, IRQF_SHARED,
			  KBUILD_MODNAME, chip);
	if (err < 0) {
		dev_err(card->dev, "cannot grab irq %d\n", pci->irq);
		goto fail;
	}
	chip->irq = pci->irq;
	card->sync_irq = chip->irq;
	pci_set_master(pci);

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0)
		goto fail;

	*rchip = chip;
	return 0;

fail:
	snd_bt87x_free(chip);
	return err;
}

#define BT_DEVICE(chip, subvend, subdev, id) \
	{ .vendor = PCI_VENDOR_ID_BROOKTREE, \
	  .device = chip, \
	  .subvendor = subvend, .subdevice = subdev, \
	  .driver_data = SND_BT87X_BOARD_ ## id }
/* driver_data is the card id for that device */

static const struct pci_device_id snd_bt87x_ids[] = {
	/* Hauppauge WinTV series */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x0070, 0x13eb, GENERIC),
	/* Hauppauge WinTV series */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_879, 0x0070, 0x13eb, GENERIC),
	/* Viewcast Osprey 200 */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x0070, 0xff01, OSPREY2x0),
	/* Viewcast Osprey 440 (rate is configurable via gpio) */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x0070, 0xff07, OSPREY440),
	/* ATI TV-Wonder */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x1002, 0x0001, GENERIC),
	/* Leadtek Winfast tv 2000xp delux */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x107d, 0x6606, GENERIC),
	/* Pinnacle PCTV */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x11bd, 0x0012, GENERIC),
	/* Voodoo TV 200 */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x121a, 0x3000, GENERIC),
	/* Askey Computer Corp. MagicTView'99 */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x144f, 0x3000, GENERIC),
	/* AVerMedia Studio No. 103, 203, ...? */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x1461, 0x0003, AVPHONE98),
	/* Prolink PixelView PV-M4900 */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x1554, 0x4011, GENERIC),
	/* Pinnacle  Studio PCTV rave */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0xbd11, 0x1200, GENERIC),
	{ }
};
MODULE_DEVICE_TABLE(pci, snd_bt87x_ids);

/* cards known not to have audio
 * (DVB cards use the audio function to transfer MPEG data) */
static struct {
	unsigned short subvendor, subdevice;
} denylist[] = {
	{0x0071, 0x0101}, /* Nebula Electronics DigiTV */
	{0x11bd, 0x001c}, /* Pinnacle PCTV Sat */
	{0x11bd, 0x0026}, /* Pinnacle PCTV SAT CI */
	{0x1461, 0x0761}, /* AVermedia AverTV DVB-T */
	{0x1461, 0x0771}, /* AVermedia DVB-T 771 */
	{0x1822, 0x0001}, /* Twinhan VisionPlus DVB-T */
	{0x18ac, 0xd500}, /* DVICO FusionHDTV 5 Lite */
	{0x18ac, 0xdb10}, /* DVICO FusionHDTV DVB-T Lite */
	{0x18ac, 0xdb11}, /* Ultraview DVB-T Lite */
	{0x270f, 0xfc00}, /* Chaintech Digitop DST-1000 DVB-S */
	{0x7063, 0x2000}, /* pcHDTV HD-2000 TV */
};

static struct pci_driver driver;

/* return the id of the card, or a negative value if it's on the denylist */
static int snd_bt87x_detect_card(struct pci_dev *pci)
{
	int i;
	const struct pci_device_id *supported;

	supported = pci_match_id(snd_bt87x_ids, pci);
	if (supported && supported->driver_data > 0)
		return supported->driver_data;

	for (i = 0; i < ARRAY_SIZE(denylist); ++i)
		if (denylist[i].subvendor == pci->subsystem_vendor &&
		    denylist[i].subdevice == pci->subsystem_device) {
			dev_dbg(&pci->dev,
				"card %#04x-%#04x:%#04x has no audio\n",
				    pci->device, pci->subsystem_vendor, pci->subsystem_device);
			return -EBUSY;
		}

	dev_info(&pci->dev, "unknown card %#04x-%#04x:%#04x\n",
		   pci->device, pci->subsystem_vendor, pci->subsystem_device);
	dev_info(&pci->dev, "please mail id, board name, and, "
		   "if it works, the correct digital_rate option to "
		   "<alsa-devel@alsa-project.org>\n");
	return SND_BT87X_BOARD_UNKNOWN;
}

static int snd_bt87x_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_bt87x *chip;
	int err;
	enum snd_bt87x_boardid boardid;

	if (!pci_id->driver_data) {
		err = snd_bt87x_detect_card(pci);
		if (err < 0)
			return -ENODEV;
		boardid = err;
	} else
		boardid = pci_id->driver_data;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		++dev;
		return -ENOENT;
	}

	err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
			   0, &card);
	if (err < 0)
		return err;

	err = snd_bt87x_create(card, pci, &chip);
	if (err < 0)
		goto _error;

	memcpy(&chip->board, &snd_bt87x_boards[boardid], sizeof(chip->board));

	if (!chip->board.no_digital) {
		if (digital_rate[dev] > 0)
			chip->board.dig_rate = digital_rate[dev];

		chip->reg_control |= chip->board.digital_fmt;

		err = snd_bt87x_pcm(chip, DEVICE_DIGITAL, "Bt87x Digital");
		if (err < 0)
			goto _error;
	}
	if (!chip->board.no_analog) {
		err = snd_bt87x_pcm(chip, DEVICE_ANALOG, "Bt87x Analog");
		if (err < 0)
			goto _error;
		err = snd_ctl_add(card, snd_ctl_new1(
				  &snd_bt87x_capture_volume, chip));
		if (err < 0)
			goto _error;
		err = snd_ctl_add(card, snd_ctl_new1(
				  &snd_bt87x_capture_boost, chip));
		if (err < 0)
			goto _error;
		err = snd_ctl_add(card, snd_ctl_new1(
				  &snd_bt87x_capture_source, chip));
		if (err < 0)
			goto _error;
	}
	dev_info(card->dev, "bt87x%d: Using board %d, %sanalog, %sdigital "
		   "(rate %d Hz)\n", dev, boardid,
		   chip->board.no_analog ? "no " : "",
		   chip->board.no_digital ? "no " : "", chip->board.dig_rate);

	strcpy(card->driver, "Bt87x");
	sprintf(card->shortname, "Brooktree Bt%x", pci->device);
	sprintf(card->longname, "%s at %#llx, irq %i",
		card->shortname, (unsigned long long)pci_resource_start(pci, 0),
		chip->irq);
	strcpy(card->mixername, "Bt87x");

	err = snd_card_register(card);
	if (err < 0)
		goto _error;

	pci_set_drvdata(pci, card);
	++dev;
	return 0;

_error:
	snd_card_free(card);
	return err;
}

static void snd_bt87x_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

/* default entries for all Bt87x cards - it's not exported */
/* driver_data is set to 0 to call detection */
static const struct pci_device_id snd_bt87x_default_ids[] = {
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, PCI_ANY_ID, PCI_ANY_ID, UNKNOWN),
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_879, PCI_ANY_ID, PCI_ANY_ID, UNKNOWN),
	{ }
};

static struct pci_driver driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_bt87x_ids,
	.probe = snd_bt87x_probe,
	.remove = snd_bt87x_remove,
};

static int __init alsa_card_bt87x_init(void)
{
	if (load_all)
		driver.id_table = snd_bt87x_default_ids;
	return pci_register_driver(&driver);
}

static void __exit alsa_card_bt87x_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_bt87x_init)
module_exit(alsa_card_bt87x_exit)
