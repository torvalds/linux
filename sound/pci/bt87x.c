/*
 * bt87x.c - Brooktree Bt878/Bt879 driver for ALSA
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 * based on btaudio.c by Gerd Knorr <kraxel@bytesex.org>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("Brooktree Bt87x audio driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Brooktree,Bt878},"
		"{Brooktree,Bt879}}");

static int index[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -2}; /* Exclude the first card */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int digital_rate[SNDRV_CARDS] = { [0 ... (SNDRV_CARDS-1)] = 0 }; /* digital input rate */
static int load_all;	/* allow to load the non-whitelisted cards */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Bt87x soundcard");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Bt87x soundcard");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Bt87x soundcard");
module_param_array(digital_rate, int, NULL, 0444);
MODULE_PARM_DESC(digital_rate, "Digital input rate for Bt87x soundcard");
module_param(load_all, bool, 0444);
MODULE_PARM_DESC(load_all, "Allow to load the non-whitelisted cards");


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

struct snd_bt87x {
	struct snd_card *card;
	struct pci_dev *pci;

	void __iomem *mmio;
	int irq;

	int dig_rate;

	spinlock_t reg_lock;
	long opened;
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
	struct snd_sg_buf *sgbuf = snd_pcm_substream_sgbuf(substream);
	unsigned int i, offset;
	u32 *risc;

	if (chip->dma_risc.area == NULL) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
					PAGE_ALIGN(MAX_RISC_SIZE), &chip->dma_risc) < 0)
			return -ENOMEM;
	}
	risc = (u32 *)chip->dma_risc.area;
	offset = 0;
	*risc++ = cpu_to_le32(RISC_SYNC | RISC_SYNC_FM1);
	*risc++ = cpu_to_le32(0);
	for (i = 0; i < periods; ++i) {
		u32 rest;

		rest = period_bytes;
		do {
			u32 cmd, len;

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
			*risc++ = cpu_to_le32((u32)snd_pcm_sgbuf_get_addr(sgbuf, offset));
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
	u16 pci_status;

	pci_read_config_word(chip->pci, PCI_STATUS, &pci_status);
	pci_status &= PCI_STATUS_PARITY | PCI_STATUS_SIG_TARGET_ABORT |
		PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_REC_MASTER_ABORT |
		PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY;
	pci_write_config_word(chip->pci, PCI_STATUS, pci_status);
	if (pci_status != PCI_STATUS_DETECTED_PARITY)
		snd_printk(KERN_ERR "Aieee - PCI error! status %#08x, PCI status %#04x\n",
			   status & ERROR_INTERRUPTS, pci_status);
	else {
		snd_printk(KERN_ERR "Aieee - PCI parity error detected!\n");
		/* error 'handling' similar to aic7xxx_pci.c: */
		chip->pci_parity_errors++;
		if (chip->pci_parity_errors > 20) {
			snd_printk(KERN_ERR "Too many PCI parity errors observed.\n");
			snd_printk(KERN_ERR "Some device on this bus is generating bad parity.\n");
			snd_printk(KERN_ERR "This is an error *observed by*, not *generated by*, this card.\n");
			snd_printk(KERN_ERR "PCI parity error checking has been disabled.\n");
			chip->interrupt_mask &= ~(INT_PPERR | INT_RIPERR);
			snd_bt87x_writel(chip, REG_INT_MASK, chip->interrupt_mask);
		}
	}
}

static irqreturn_t snd_bt87x_interrupt(int irq, void *dev_id, struct pt_regs *regs)
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
			snd_printk(KERN_WARNING "FIFO overrun, status %#08x\n", status);
		if (irq_status & INT_OCERR)
			snd_printk(KERN_ERR "internal RISC error, status %#08x\n", status);
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
			chip->current_line = (irq_block * chip->lines + 15) / 16;

		snd_pcm_period_elapsed(chip->substream);
	}
	return IRQ_HANDLED;
}

static struct snd_pcm_hardware snd_bt87x_digital_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
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

static struct snd_pcm_hardware snd_bt87x_analog_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID,
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
	static struct {
		int rate;
		unsigned int bit;
	} ratebits[] = {
		{8000, SNDRV_PCM_RATE_8000},
		{11025, SNDRV_PCM_RATE_11025},
		{16000, SNDRV_PCM_RATE_16000},
		{22050, SNDRV_PCM_RATE_22050},
		{32000, SNDRV_PCM_RATE_32000},
		{44100, SNDRV_PCM_RATE_44100},
		{48000, SNDRV_PCM_RATE_48000}
	};
	int i;

	chip->reg_control |= CTL_DA_IOM_DA;
	runtime->hw = snd_bt87x_digital_hw;
	runtime->hw.rates = SNDRV_PCM_RATE_KNOT;
	for (i = 0; i < ARRAY_SIZE(ratebits); ++i)
		if (chip->dig_rate == ratebits[i].rate) {
			runtime->hw.rates = ratebits[i].bit;
			break;
		}
	runtime->hw.rate_min = chip->dig_rate;
	runtime->hw.rate_max = chip->dig_rate;
	return 0;
}

static int snd_bt87x_set_analog_hw(struct snd_bt87x *chip, struct snd_pcm_runtime *runtime)
{
	static struct snd_ratnum analog_clock = {
		.num = ANALOG_CLOCK,
		.den_min = CLOCK_DIV_MIN,
		.den_max = CLOCK_DIV_MAX,
		.den_step = 1
	};
	static struct snd_pcm_hw_constraint_ratnums constraint_rates = {
		.nrats = 1,
		.rats = &analog_clock
	};

	chip->reg_control &= ~CTL_DA_IOM_DA;
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
	smp_mb__after_clear_bit();
	return err;
}

static int snd_bt87x_close(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	chip->substream = NULL;
	clear_bit(0, &chip->opened);
	smp_mb__after_clear_bit();
	return 0;
}

static int snd_bt87x_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);
	int err;

	err = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	return snd_bt87x_create_risc(chip, substream,
				     params_periods(hw_params),
				     params_period_bytes(hw_params));
}

static int snd_bt87x_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_bt87x *chip = snd_pcm_substream_chip(substream);

	snd_bt87x_free_risc(chip);
	snd_pcm_lib_free_pages(substream);
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

static struct snd_pcm_ops snd_bt87x_pcm_ops = {
	.open = snd_bt87x_pcm_open,
	.close = snd_bt87x_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_bt87x_hw_params,
	.hw_free = snd_bt87x_hw_free,
	.prepare = snd_bt87x_prepare,
	.trigger = snd_bt87x_trigger,
	.pointer = snd_bt87x_pointer,
	.page = snd_pcm_sgbuf_ops_page,
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

static struct snd_kcontrol_new snd_bt87x_capture_volume = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.info = snd_bt87x_capture_volume_info,
	.get = snd_bt87x_capture_volume_get,
	.put = snd_bt87x_capture_volume_put,
};

static int snd_bt87x_capture_boost_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	info->count = 1;
	info->value.integer.min = 0;
	info->value.integer.max = 1;
	return 0;
}

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

static struct snd_kcontrol_new snd_bt87x_capture_boost = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Boost",
	.info = snd_bt87x_capture_boost_info,
	.get = snd_bt87x_capture_boost_get,
	.put = snd_bt87x_capture_boost_put,
};

static int snd_bt87x_capture_source_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *info)
{
	static char *texts[3] = {"TV Tuner", "FM", "Mic/Line"};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 3;
	if (info->value.enumerated.item > 2)
		info->value.enumerated.item = 2;
	strcpy(info->value.enumerated.name, texts[info->value.enumerated.item]);
	return 0;
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

static struct snd_kcontrol_new snd_bt87x_capture_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_bt87x_capture_source_info,
	.get = snd_bt87x_capture_source_get,
	.put = snd_bt87x_capture_source_put,
};

static int snd_bt87x_free(struct snd_bt87x *chip)
{
	if (chip->mmio) {
		snd_bt87x_stop(chip);
		if (chip->irq >= 0)
			synchronize_irq(chip->irq);

		iounmap(chip->mmio);
	}
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
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

static int __devinit snd_bt87x_pcm(struct snd_bt87x *chip, int device, char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;
	pcm->private_data = chip;
	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_bt87x_pcm_ops);
	return snd_pcm_lib_preallocate_pages_for_all(pcm,
						     SNDRV_DMA_TYPE_DEV_SG,
						     snd_dma_pci_data(chip->pci),
							128 * 1024,
							(255 * 4092 + 1023) & ~1023);
}

static int __devinit snd_bt87x_create(struct snd_card *card,
				      struct pci_dev *pci,
				      struct snd_bt87x **rchip)
{
	struct snd_bt87x *chip;
	int err;
	static struct snd_device_ops ops = {
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
	chip->mmio = ioremap_nocache(pci_resource_start(pci, 0),
				     pci_resource_len(pci, 0));
	if (!chip->mmio) {
		snd_bt87x_free(chip);
		snd_printk(KERN_ERR "cannot remap io memory\n");
		return -ENOMEM;
	}

	chip->reg_control = CTL_DA_ES2 | CTL_PKTP_16 | (15 << CTL_DA_SDR_SHIFT);
	chip->interrupt_mask = MY_INTERRUPTS;
	snd_bt87x_writel(chip, REG_GPIO_DMA_CTL, chip->reg_control);
	snd_bt87x_writel(chip, REG_INT_MASK, 0);
	snd_bt87x_writel(chip, REG_INT_STAT, MY_INTERRUPTS);

	if (request_irq(pci->irq, snd_bt87x_interrupt, SA_INTERRUPT | SA_SHIRQ,
			"Bt87x audio", chip)) {
		snd_bt87x_free(chip);
		snd_printk(KERN_ERR "cannot grab irq\n");
		return -EBUSY;
	}
	chip->irq = pci->irq;
	pci_set_master(pci);
	synchronize_irq(chip->irq);

	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
	if (err < 0) {
		snd_bt87x_free(chip);
		return err;
	}
	snd_card_set_dev(card, &pci->dev);
	*rchip = chip;
	return 0;
}

#define BT_DEVICE(chip, subvend, subdev, rate) \
	{ .vendor = PCI_VENDOR_ID_BROOKTREE, \
	  .device = chip, \
	  .subvendor = subvend, .subdevice = subdev, \
	  .driver_data = rate }

/* driver_data is the default digital_rate value for that device */
static struct pci_device_id snd_bt87x_ids[] = {
	/* Hauppauge WinTV series */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x0070, 0x13eb, 32000),
	/* Hauppauge WinTV series */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_879, 0x0070, 0x13eb, 32000),
	/* Viewcast Osprey 200 */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x0070, 0xff01, 44100),
	/* AVerMedia Studio No. 103, 203, ...? */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x1461, 0x0003, 48000),
	/* Leadtek Winfast tv 2000xp delux */
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, 0x107d, 0x6606, 32000),
	{ }
};
MODULE_DEVICE_TABLE(pci, snd_bt87x_ids);

/* cards known not to have audio
 * (DVB cards use the audio function to transfer MPEG data) */
static struct {
	unsigned short subvendor, subdevice;
} blacklist[] __devinitdata = {
	{0x0071, 0x0101}, /* Nebula Electronics DigiTV */
	{0x11bd, 0x001c}, /* Pinnacle PCTV Sat */
	{0x11bd, 0x0026}, /* Pinnacle PCTV SAT CI */
	{0x1461, 0x0761}, /* AVermedia AverTV DVB-T */
	{0x1461, 0x0771}, /* AVermedia DVB-T 771 */
	{0x1822, 0x0001}, /* Twinhan VisionPlus DVB-T */
	{0x18ac, 0xd500}, /* DVICO FusionHDTV 5 Lite */
	{0x18ac, 0xdb10}, /* DVICO FusionHDTV DVB-T Lite */
	{0x270f, 0xfc00}, /* Chaintech Digitop DST-1000 DVB-S */
	{0x7063, 0x2000}, /* pcHDTV HD-2000 TV */
};

static struct pci_driver driver;

/* return the rate of the card, or a negative value if it's blacklisted */
static int __devinit snd_bt87x_detect_card(struct pci_dev *pci)
{
	int i;
	const struct pci_device_id *supported;

	supported = pci_match_device(&driver, pci);
	if (supported && supported->driver_data > 0)
		return supported->driver_data;

	for (i = 0; i < ARRAY_SIZE(blacklist); ++i)
		if (blacklist[i].subvendor == pci->subsystem_vendor &&
		    blacklist[i].subdevice == pci->subsystem_device) {
			snd_printdd(KERN_INFO "card %#04x-%#04x:%#04x has no audio\n",
				    pci->device, pci->subsystem_vendor, pci->subsystem_device);
			return -EBUSY;
		}

	snd_printk(KERN_INFO "unknown card %#04x-%#04x:%#04x, using default rate 32000\n",
	           pci->device, pci->subsystem_vendor, pci->subsystem_device);
	snd_printk(KERN_DEBUG "please mail id, board name, and, "
		   "if it works, the correct digital_rate option to "
		   "<alsa-devel@lists.sf.net>\n");
	return 32000; /* default rate */
}

static int __devinit snd_bt87x_probe(struct pci_dev *pci,
				     const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_card *card;
	struct snd_bt87x *chip;
	int err, rate;

	rate = pci_id->driver_data;
	if (! rate)
		if ((rate = snd_bt87x_detect_card(pci)) <= 0)
			return -ENODEV;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		++dev;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (!card)
		return -ENOMEM;

	err = snd_bt87x_create(card, pci, &chip);
	if (err < 0)
		goto _error;

	if (digital_rate[dev] > 0)
		chip->dig_rate = digital_rate[dev];
	else
		chip->dig_rate = rate;

	err = snd_bt87x_pcm(chip, DEVICE_DIGITAL, "Bt87x Digital");
	if (err < 0)
		goto _error;
	err = snd_bt87x_pcm(chip, DEVICE_ANALOG, "Bt87x Analog");
	if (err < 0)
		goto _error;

	err = snd_ctl_add(card, snd_ctl_new1(&snd_bt87x_capture_volume, chip));
	if (err < 0)
		goto _error;
	err = snd_ctl_add(card, snd_ctl_new1(&snd_bt87x_capture_boost, chip));
	if (err < 0)
		goto _error;
	err = snd_ctl_add(card, snd_ctl_new1(&snd_bt87x_capture_source, chip));
	if (err < 0)
		goto _error;

	strcpy(card->driver, "Bt87x");
	sprintf(card->shortname, "Brooktree Bt%x", pci->device);
	sprintf(card->longname, "%s at %#lx, irq %i",
		card->shortname, pci_resource_start(pci, 0), chip->irq);
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

static void __devexit snd_bt87x_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

/* default entries for all Bt87x cards - it's not exported */
/* driver_data is set to 0 to call detection */
static struct pci_device_id snd_bt87x_default_ids[] = {
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_878, PCI_ANY_ID, PCI_ANY_ID, 0),
	BT_DEVICE(PCI_DEVICE_ID_BROOKTREE_879, PCI_ANY_ID, PCI_ANY_ID, 0),
	{ }
};

static struct pci_driver driver = {
	.name = "Bt87x",
	.id_table = snd_bt87x_ids,
	.probe = snd_bt87x_probe,
	.remove = __devexit_p(snd_bt87x_remove),
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
