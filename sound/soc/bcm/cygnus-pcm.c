/*
 * Copyright (C) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "cygnus-ssp.h"

/* Register offset needed for ASoC PCM module */

#define INTH_R5F_STATUS_OFFSET     0x040
#define INTH_R5F_CLEAR_OFFSET      0x048
#define INTH_R5F_MASK_SET_OFFSET   0x050
#define INTH_R5F_MASK_CLEAR_OFFSET 0x054

#define BF_REARM_FREE_MARK_OFFSET 0x344
#define BF_REARM_FULL_MARK_OFFSET 0x348

/* Ring Buffer Ctrl Regs --- Start */
/* AUD_FMM_BF_CTRL_SOURCECH_RINGBUF_X_RDADDR_REG_BASE */
#define SRC_RBUF_0_RDADDR_OFFSET 0x500
#define SRC_RBUF_1_RDADDR_OFFSET 0x518
#define SRC_RBUF_2_RDADDR_OFFSET 0x530
#define SRC_RBUF_3_RDADDR_OFFSET 0x548
#define SRC_RBUF_4_RDADDR_OFFSET 0x560
#define SRC_RBUF_5_RDADDR_OFFSET 0x578
#define SRC_RBUF_6_RDADDR_OFFSET 0x590

/* AUD_FMM_BF_CTRL_SOURCECH_RINGBUF_X_WRADDR_REG_BASE */
#define SRC_RBUF_0_WRADDR_OFFSET 0x504
#define SRC_RBUF_1_WRADDR_OFFSET 0x51c
#define SRC_RBUF_2_WRADDR_OFFSET 0x534
#define SRC_RBUF_3_WRADDR_OFFSET 0x54c
#define SRC_RBUF_4_WRADDR_OFFSET 0x564
#define SRC_RBUF_5_WRADDR_OFFSET 0x57c
#define SRC_RBUF_6_WRADDR_OFFSET 0x594

/* AUD_FMM_BF_CTRL_SOURCECH_RINGBUF_X_BASEADDR_REG_BASE */
#define SRC_RBUF_0_BASEADDR_OFFSET 0x508
#define SRC_RBUF_1_BASEADDR_OFFSET 0x520
#define SRC_RBUF_2_BASEADDR_OFFSET 0x538
#define SRC_RBUF_3_BASEADDR_OFFSET 0x550
#define SRC_RBUF_4_BASEADDR_OFFSET 0x568
#define SRC_RBUF_5_BASEADDR_OFFSET 0x580
#define SRC_RBUF_6_BASEADDR_OFFSET 0x598

/* AUD_FMM_BF_CTRL_SOURCECH_RINGBUF_X_ENDADDR_REG_BASE */
#define SRC_RBUF_0_ENDADDR_OFFSET 0x50c
#define SRC_RBUF_1_ENDADDR_OFFSET 0x524
#define SRC_RBUF_2_ENDADDR_OFFSET 0x53c
#define SRC_RBUF_3_ENDADDR_OFFSET 0x554
#define SRC_RBUF_4_ENDADDR_OFFSET 0x56c
#define SRC_RBUF_5_ENDADDR_OFFSET 0x584
#define SRC_RBUF_6_ENDADDR_OFFSET 0x59c

/* AUD_FMM_BF_CTRL_SOURCECH_RINGBUF_X_FREE_MARK_REG_BASE */
#define SRC_RBUF_0_FREE_MARK_OFFSET 0x510
#define SRC_RBUF_1_FREE_MARK_OFFSET 0x528
#define SRC_RBUF_2_FREE_MARK_OFFSET 0x540
#define SRC_RBUF_3_FREE_MARK_OFFSET 0x558
#define SRC_RBUF_4_FREE_MARK_OFFSET 0x570
#define SRC_RBUF_5_FREE_MARK_OFFSET 0x588
#define SRC_RBUF_6_FREE_MARK_OFFSET 0x5a0

/* AUD_FMM_BF_CTRL_DESTCH_RINGBUF_X_RDADDR_REG_BASE */
#define DST_RBUF_0_RDADDR_OFFSET 0x5c0
#define DST_RBUF_1_RDADDR_OFFSET 0x5d8
#define DST_RBUF_2_RDADDR_OFFSET 0x5f0
#define DST_RBUF_3_RDADDR_OFFSET 0x608
#define DST_RBUF_4_RDADDR_OFFSET 0x620
#define DST_RBUF_5_RDADDR_OFFSET 0x638

/* AUD_FMM_BF_CTRL_DESTCH_RINGBUF_X_WRADDR_REG_BASE */
#define DST_RBUF_0_WRADDR_OFFSET 0x5c4
#define DST_RBUF_1_WRADDR_OFFSET 0x5dc
#define DST_RBUF_2_WRADDR_OFFSET 0x5f4
#define DST_RBUF_3_WRADDR_OFFSET 0x60c
#define DST_RBUF_4_WRADDR_OFFSET 0x624
#define DST_RBUF_5_WRADDR_OFFSET 0x63c

/* AUD_FMM_BF_CTRL_DESTCH_RINGBUF_X_BASEADDR_REG_BASE */
#define DST_RBUF_0_BASEADDR_OFFSET 0x5c8
#define DST_RBUF_1_BASEADDR_OFFSET 0x5e0
#define DST_RBUF_2_BASEADDR_OFFSET 0x5f8
#define DST_RBUF_3_BASEADDR_OFFSET 0x610
#define DST_RBUF_4_BASEADDR_OFFSET 0x628
#define DST_RBUF_5_BASEADDR_OFFSET 0x640

/* AUD_FMM_BF_CTRL_DESTCH_RINGBUF_X_ENDADDR_REG_BASE */
#define DST_RBUF_0_ENDADDR_OFFSET 0x5cc
#define DST_RBUF_1_ENDADDR_OFFSET 0x5e4
#define DST_RBUF_2_ENDADDR_OFFSET 0x5fc
#define DST_RBUF_3_ENDADDR_OFFSET 0x614
#define DST_RBUF_4_ENDADDR_OFFSET 0x62c
#define DST_RBUF_5_ENDADDR_OFFSET 0x644

/* AUD_FMM_BF_CTRL_DESTCH_RINGBUF_X_FULL_MARK_REG_BASE */
#define DST_RBUF_0_FULL_MARK_OFFSET 0x5d0
#define DST_RBUF_1_FULL_MARK_OFFSET 0x5e8
#define DST_RBUF_2_FULL_MARK_OFFSET 0x600
#define DST_RBUF_3_FULL_MARK_OFFSET 0x618
#define DST_RBUF_4_FULL_MARK_OFFSET 0x630
#define DST_RBUF_5_FULL_MARK_OFFSET 0x648
/* Ring Buffer Ctrl Regs --- End */

/* Error Status Regs --- Start */
/* AUD_FMM_BF_ESR_ESRX_STATUS_REG_BASE */
#define ESR0_STATUS_OFFSET 0x900
#define ESR1_STATUS_OFFSET 0x918
#define ESR2_STATUS_OFFSET 0x930
#define ESR3_STATUS_OFFSET 0x948
#define ESR4_STATUS_OFFSET 0x960

/* AUD_FMM_BF_ESR_ESRX_STATUS_CLEAR_REG_BASE */
#define ESR0_STATUS_CLR_OFFSET 0x908
#define ESR1_STATUS_CLR_OFFSET 0x920
#define ESR2_STATUS_CLR_OFFSET 0x938
#define ESR3_STATUS_CLR_OFFSET 0x950
#define ESR4_STATUS_CLR_OFFSET 0x968

/* AUD_FMM_BF_ESR_ESRX_MASK_REG_BASE */
#define ESR0_MASK_STATUS_OFFSET 0x90c
#define ESR1_MASK_STATUS_OFFSET 0x924
#define ESR2_MASK_STATUS_OFFSET 0x93c
#define ESR3_MASK_STATUS_OFFSET 0x954
#define ESR4_MASK_STATUS_OFFSET 0x96c

/* AUD_FMM_BF_ESR_ESRX_MASK_SET_REG_BASE */
#define ESR0_MASK_SET_OFFSET 0x910
#define ESR1_MASK_SET_OFFSET 0x928
#define ESR2_MASK_SET_OFFSET 0x940
#define ESR3_MASK_SET_OFFSET 0x958
#define ESR4_MASK_SET_OFFSET 0x970

/* AUD_FMM_BF_ESR_ESRX_MASK_CLEAR_REG_BASE */
#define ESR0_MASK_CLR_OFFSET 0x914
#define ESR1_MASK_CLR_OFFSET 0x92c
#define ESR2_MASK_CLR_OFFSET 0x944
#define ESR3_MASK_CLR_OFFSET 0x95c
#define ESR4_MASK_CLR_OFFSET 0x974
/* Error Status Regs --- End */

#define R5F_ESR0_SHIFT  0    /* esr0 = fifo underflow */
#define R5F_ESR1_SHIFT  1    /* esr1 = ringbuf underflow */
#define R5F_ESR2_SHIFT  2    /* esr2 = ringbuf overflow */
#define R5F_ESR3_SHIFT  3    /* esr3 = freemark */
#define R5F_ESR4_SHIFT  4    /* esr4 = fullmark */


/* Mask for R5F register.  Set all relevant interrupt for playback handler */
#define ANY_PLAYBACK_IRQ  (BIT(R5F_ESR0_SHIFT) | \
			   BIT(R5F_ESR1_SHIFT) | \
			   BIT(R5F_ESR3_SHIFT))

/* Mask for R5F register.  Set all relevant interrupt for capture handler */
#define ANY_CAPTURE_IRQ   (BIT(R5F_ESR2_SHIFT) | BIT(R5F_ESR4_SHIFT))

/*
 * PERIOD_BYTES_MIN is the number of bytes to at which the interrupt will tick.
 * This number should be a multiple of 256. Minimum value is 256
 */
#define PERIOD_BYTES_MIN 0x100

static const struct snd_pcm_hardware cygnus_pcm_hw = {
	.info = SNDRV_PCM_INFO_MMAP |
			SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S32_LE,

	/* A period is basically an interrupt */
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = 0x10000,

	/* period_min/max gives range of approx interrupts per buffer */
	.periods_min = 2,
	.periods_max = 8,

	/*
	 * maximum buffer size in bytes = period_bytes_max * periods_max
	 * We allocate this amount of data for each enabled channel
	 */
	.buffer_bytes_max = 4 * 0x8000,
};

static u64 cygnus_dma_dmamask = DMA_BIT_MASK(32);

static struct cygnus_aio_port *cygnus_dai_get_dma_data(
				struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;

	return snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);
}

static void ringbuf_set_initial(void __iomem *audio_io,
		struct ringbuf_regs *p_rbuf,
		bool is_playback,
		u32 start,
		u32 periodsize,
		u32 bufsize)
{
	u32 initial_rd;
	u32 initial_wr;
	u32 end;
	u32 fmark_val; /* free or full mark */

	p_rbuf->period_bytes = periodsize;
	p_rbuf->buf_size = bufsize;

	if (is_playback) {
		/* Set the pointers to indicate full (flip uppermost bit) */
		initial_rd = start;
		initial_wr = initial_rd ^ BIT(31);
	} else {
		/* Set the pointers to indicate empty */
		initial_wr = start;
		initial_rd = initial_wr;
	}

	end = start + bufsize - 1;

	/*
	 * The interrupt will fire when free/full mark is *exceeded*
	 * The fmark value must be multiple of PERIOD_BYTES_MIN so set fmark
	 * to be PERIOD_BYTES_MIN less than the period size.
	 */
	fmark_val = periodsize - PERIOD_BYTES_MIN;

	writel(start, audio_io + p_rbuf->baseaddr);
	writel(end, audio_io + p_rbuf->endaddr);
	writel(fmark_val, audio_io + p_rbuf->fmark);
	writel(initial_rd, audio_io + p_rbuf->rdaddr);
	writel(initial_wr, audio_io + p_rbuf->wraddr);
}

static int configure_ringbuf_regs(struct snd_pcm_substream *substream)
{
	struct cygnus_aio_port *aio;
	struct ringbuf_regs *p_rbuf;
	int status = 0;

	aio = cygnus_dai_get_dma_data(substream);

	/* Map the ssp portnum to a set of ring buffers. */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		p_rbuf = &aio->play_rb_regs;

		switch (aio->portnum) {
		case 0:
			*p_rbuf = RINGBUF_REG_PLAYBACK(0);
			break;
		case 1:
			*p_rbuf = RINGBUF_REG_PLAYBACK(2);
			break;
		case 2:
			*p_rbuf = RINGBUF_REG_PLAYBACK(4);
			break;
		case 3: /* SPDIF */
			*p_rbuf = RINGBUF_REG_PLAYBACK(6);
			break;
		default:
			status = -EINVAL;
		}
	} else {
		p_rbuf = &aio->capture_rb_regs;

		switch (aio->portnum) {
		case 0:
			*p_rbuf = RINGBUF_REG_CAPTURE(0);
			break;
		case 1:
			*p_rbuf = RINGBUF_REG_CAPTURE(2);
			break;
		case 2:
			*p_rbuf = RINGBUF_REG_CAPTURE(4);
			break;
		default:
			status = -EINVAL;
		}
	}

	return status;
}

static struct ringbuf_regs *get_ringbuf(struct snd_pcm_substream *substream)
{
	struct cygnus_aio_port *aio;
	struct ringbuf_regs *p_rbuf = NULL;

	aio = cygnus_dai_get_dma_data(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		p_rbuf = &aio->play_rb_regs;
	else
		p_rbuf = &aio->capture_rb_regs;

	return p_rbuf;
}

static void enable_intr(struct snd_pcm_substream *substream)
{
	struct cygnus_aio_port *aio;
	u32 clear_mask;

	aio = cygnus_dai_get_dma_data(substream);

	/* The port number maps to the bit position to be cleared */
	clear_mask = BIT(aio->portnum);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Clear interrupt status before enabling them */
		writel(clear_mask, aio->cygaud->audio + ESR0_STATUS_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR1_STATUS_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR3_STATUS_CLR_OFFSET);
		/* Unmask the interrupts of the given port*/
		writel(clear_mask, aio->cygaud->audio + ESR0_MASK_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR1_MASK_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR3_MASK_CLR_OFFSET);

		writel(ANY_PLAYBACK_IRQ,
			aio->cygaud->audio + INTH_R5F_MASK_CLEAR_OFFSET);
	} else {
		writel(clear_mask, aio->cygaud->audio + ESR2_STATUS_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR4_STATUS_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR2_MASK_CLR_OFFSET);
		writel(clear_mask, aio->cygaud->audio + ESR4_MASK_CLR_OFFSET);

		writel(ANY_CAPTURE_IRQ,
			aio->cygaud->audio + INTH_R5F_MASK_CLEAR_OFFSET);
	}

}

static void disable_intr(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct cygnus_aio_port *aio;
	u32 set_mask;

	aio = cygnus_dai_get_dma_data(substream);

	dev_dbg(rtd->cpu_dai->dev, "%s on port %d\n", __func__, aio->portnum);

	/* The port number maps to the bit position to be set */
	set_mask = BIT(aio->portnum);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Mask the interrupts of the given port*/
		writel(set_mask, aio->cygaud->audio + ESR0_MASK_SET_OFFSET);
		writel(set_mask, aio->cygaud->audio + ESR1_MASK_SET_OFFSET);
		writel(set_mask, aio->cygaud->audio + ESR3_MASK_SET_OFFSET);
	} else {
		writel(set_mask, aio->cygaud->audio + ESR2_MASK_SET_OFFSET);
		writel(set_mask, aio->cygaud->audio + ESR4_MASK_SET_OFFSET);
	}

}

static int cygnus_pcm_trigger(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		enable_intr(substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		disable_intr(substream);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void cygnus_pcm_period_elapsed(struct snd_pcm_substream *substream)
{
	struct cygnus_aio_port *aio;
	struct ringbuf_regs *p_rbuf = NULL;
	u32 regval;

	aio = cygnus_dai_get_dma_data(substream);

	p_rbuf = get_ringbuf(substream);

	/*
	 * If free/full mark interrupt occurs, provide timestamp
	 * to ALSA and update appropriate idx by period_bytes
	 */
	snd_pcm_period_elapsed(substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Set the ring buffer to full */
		regval = readl(aio->cygaud->audio + p_rbuf->rdaddr);
		regval = regval ^ BIT(31);
		writel(regval, aio->cygaud->audio + p_rbuf->wraddr);
	} else {
		/* Set the ring buffer to empty */
		regval = readl(aio->cygaud->audio + p_rbuf->wraddr);
		writel(regval, aio->cygaud->audio + p_rbuf->rdaddr);
	}
}

/*
 * ESR0/1/3 status  Description
 *  0x1	I2S0_out port caused interrupt
 *  0x2	I2S1_out port caused interrupt
 *  0x4	I2S2_out port caused interrupt
 *  0x8	SPDIF_out port caused interrupt
 */
static void handle_playback_irq(struct cygnus_audio *cygaud)
{
	void __iomem *audio_io;
	u32 port;
	u32 esr_status0, esr_status1, esr_status3;

	audio_io = cygaud->audio;

	/*
	 * ESR status gets updates with/without interrupts enabled.
	 * So, check the ESR mask, which provides interrupt enable/
	 * disable status and use it to determine which ESR status
	 * should be serviced.
	 */
	esr_status0 = readl(audio_io + ESR0_STATUS_OFFSET);
	esr_status0 &= ~readl(audio_io + ESR0_MASK_STATUS_OFFSET);
	esr_status1 = readl(audio_io + ESR1_STATUS_OFFSET);
	esr_status1 &= ~readl(audio_io + ESR1_MASK_STATUS_OFFSET);
	esr_status3 = readl(audio_io + ESR3_STATUS_OFFSET);
	esr_status3 &= ~readl(audio_io + ESR3_MASK_STATUS_OFFSET);

	for (port = 0; port < CYGNUS_MAX_PLAYBACK_PORTS; port++) {
		u32 esrmask = BIT(port);

		/*
		 * Ringbuffer or FIFO underflow
		 * If we get this interrupt then, it is also true that we have
		 * not yet responded to the freemark interrupt.
		 * Log a debug message.  The freemark handler below will
		 * handle getting everything going again.
		 */
		if ((esrmask & esr_status1) || (esrmask & esr_status0)) {
			dev_dbg(cygaud->dev,
				"Underrun: esr0=0x%x, esr1=0x%x esr3=0x%x\n",
				esr_status0, esr_status1, esr_status3);
		}

		/*
		 * Freemark is hit. This is the normal interrupt.
		 * In typical operation the read and write regs will be equal
		 */
		if (esrmask & esr_status3) {
			struct snd_pcm_substream *playstr;

			playstr = cygaud->portinfo[port].play_stream;
			cygnus_pcm_period_elapsed(playstr);
		}
	}

	/* Clear ESR interrupt */
	writel(esr_status0, audio_io + ESR0_STATUS_CLR_OFFSET);
	writel(esr_status1, audio_io + ESR1_STATUS_CLR_OFFSET);
	writel(esr_status3, audio_io + ESR3_STATUS_CLR_OFFSET);
	/* Rearm freemark logic by writing 1 to the correct bit */
	writel(esr_status3, audio_io + BF_REARM_FREE_MARK_OFFSET);
}

/*
 * ESR2/4 status  Description
 *  0x1	I2S0_in port caused interrupt
 *  0x2	I2S1_in port caused interrupt
 *  0x4	I2S2_in port caused interrupt
 */
static void handle_capture_irq(struct cygnus_audio *cygaud)
{
	void __iomem *audio_io;
	u32 port;
	u32 esr_status2, esr_status4;

	audio_io = cygaud->audio;

	/*
	 * ESR status gets updates with/without interrupts enabled.
	 * So, check the ESR mask, which provides interrupt enable/
	 * disable status and use it to determine which ESR status
	 * should be serviced.
	 */
	esr_status2 = readl(audio_io + ESR2_STATUS_OFFSET);
	esr_status2 &= ~readl(audio_io + ESR2_MASK_STATUS_OFFSET);
	esr_status4 = readl(audio_io + ESR4_STATUS_OFFSET);
	esr_status4 &= ~readl(audio_io + ESR4_MASK_STATUS_OFFSET);

	for (port = 0; port < CYGNUS_MAX_CAPTURE_PORTS; port++) {
		u32 esrmask = BIT(port);

		/*
		 * Ringbuffer or FIFO overflow
		 * If we get this interrupt then, it is also true that we have
		 * not yet responded to the fullmark interrupt.
		 * Log a debug message.  The fullmark handler below will
		 * handle getting everything going again.
		 */
		if (esrmask & esr_status2)
			dev_dbg(cygaud->dev,
				"Overflow: esr2=0x%x\n", esr_status2);

		if (esrmask & esr_status4) {
			struct snd_pcm_substream *capstr;

			capstr = cygaud->portinfo[port].capture_stream;
			cygnus_pcm_period_elapsed(capstr);
		}
	}

	writel(esr_status2, audio_io + ESR2_STATUS_CLR_OFFSET);
	writel(esr_status4, audio_io + ESR4_STATUS_CLR_OFFSET);
	/* Rearm fullmark logic by writing 1 to the correct bit */
	writel(esr_status4, audio_io + BF_REARM_FULL_MARK_OFFSET);
}

static irqreturn_t cygnus_dma_irq(int irq, void *data)
{
	u32 r5_status;
	struct cygnus_audio *cygaud = data;

	/*
	 * R5 status bits	Description
	 *  0		ESR0 (playback FIFO interrupt)
	 *  1		ESR1 (playback rbuf interrupt)
	 *  2		ESR2 (capture rbuf interrupt)
	 *  3		ESR3 (Freemark play. interrupt)
	 *  4		ESR4 (Fullmark capt. interrupt)
	 */
	r5_status = readl(cygaud->audio + INTH_R5F_STATUS_OFFSET);

	if (!(r5_status & (ANY_PLAYBACK_IRQ | ANY_CAPTURE_IRQ)))
		return IRQ_NONE;

	/* If playback interrupt happened */
	if (ANY_PLAYBACK_IRQ & r5_status) {
		handle_playback_irq(cygaud);
		writel(ANY_PLAYBACK_IRQ & r5_status,
			cygaud->audio + INTH_R5F_CLEAR_OFFSET);
	}

	/* If  capture interrupt happened */
	if (ANY_CAPTURE_IRQ & r5_status) {
		handle_capture_irq(cygaud);
		writel(ANY_CAPTURE_IRQ & r5_status,
			cygaud->audio + INTH_R5F_CLEAR_OFFSET);
	}

	return IRQ_HANDLED;
}

static int cygnus_pcm_open(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct cygnus_aio_port *aio;
	int ret;

	aio = cygnus_dai_get_dma_data(substream);
	if (!aio)
		return -ENODEV;

	dev_dbg(rtd->cpu_dai->dev, "%s port %d\n", __func__, aio->portnum);

	snd_soc_set_runtime_hwparams(substream, &cygnus_pcm_hw);

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, PERIOD_BYTES_MIN);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, PERIOD_BYTES_MIN);
	if (ret < 0)
		return ret;
	/*
	 * Keep track of which substream belongs to which port.
	 * This info is needed by snd_pcm_period_elapsed() in irq_handler
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aio->play_stream = substream;
	else
		aio->capture_stream = substream;

	return 0;
}

static int cygnus_pcm_close(struct snd_soc_component *component,
			    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct cygnus_aio_port *aio;

	aio = cygnus_dai_get_dma_data(substream);

	dev_dbg(rtd->cpu_dai->dev, "%s  port %d\n", __func__, aio->portnum);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aio->play_stream = NULL;
	else
		aio->capture_stream = NULL;

	if (!aio->play_stream && !aio->capture_stream)
		dev_dbg(rtd->cpu_dai->dev, "freed  port %d\n", aio->portnum);

	return 0;
}

static int cygnus_pcm_hw_params(struct snd_soc_component *component,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct cygnus_aio_port *aio;

	aio = cygnus_dai_get_dma_data(substream);
	dev_dbg(rtd->cpu_dai->dev, "%s  port %d\n", __func__, aio->portnum);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	return 0;
}

static int cygnus_pcm_hw_free(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct cygnus_aio_port *aio;

	aio = cygnus_dai_get_dma_data(substream);
	dev_dbg(rtd->cpu_dai->dev, "%s  port %d\n", __func__, aio->portnum);

	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int cygnus_pcm_prepare(struct snd_soc_component *component,
			      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct cygnus_aio_port *aio;
	unsigned long bufsize, periodsize;
	bool is_play;
	u32 start;
	struct ringbuf_regs *p_rbuf = NULL;

	aio = cygnus_dai_get_dma_data(substream);
	dev_dbg(rtd->cpu_dai->dev, "%s port %d\n", __func__, aio->portnum);

	bufsize = snd_pcm_lib_buffer_bytes(substream);
	periodsize = snd_pcm_lib_period_bytes(substream);

	dev_dbg(rtd->cpu_dai->dev, "%s (buf_size %lu) (period_size %lu)\n",
			__func__, bufsize, periodsize);

	configure_ringbuf_regs(substream);

	p_rbuf = get_ringbuf(substream);

	start = runtime->dma_addr;

	is_play = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? 1 : 0;

	ringbuf_set_initial(aio->cygaud->audio, p_rbuf, is_play, start,
				periodsize, bufsize);

	return 0;
}

static snd_pcm_uframes_t cygnus_pcm_pointer(struct snd_soc_component *component,
					    struct snd_pcm_substream *substream)
{
	struct cygnus_aio_port *aio;
	unsigned int res = 0, cur = 0, base = 0;
	struct ringbuf_regs *p_rbuf = NULL;

	aio = cygnus_dai_get_dma_data(substream);

	/*
	 * Get the offset of the current read (for playack) or write
	 * index (for capture).  Report this value back to the asoc framework.
	 */
	p_rbuf = get_ringbuf(substream);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		cur = readl(aio->cygaud->audio + p_rbuf->rdaddr);
	else
		cur = readl(aio->cygaud->audio + p_rbuf->wraddr);

	base = readl(aio->cygaud->audio + p_rbuf->baseaddr);

	/*
	 * Mask off the MSB of the rdaddr,wraddr and baseaddr
	 * since MSB is not part of the address
	 */
	res = (cur & 0x7fffffff) - (base & 0x7fffffff);

	return bytes_to_frames(substream->runtime, res);
}

static int cygnus_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size;

	size = cygnus_pcm_hw.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
			&buf->addr, GFP_KERNEL);

	dev_dbg(rtd->cpu_dai->dev, "%s: size 0x%zx @ %pK\n",
				__func__, size, buf->area);

	if (!buf->area) {
		dev_err(rtd->cpu_dai->dev, "%s: dma_alloc failed\n", __func__);
		return -ENOMEM;
	}
	buf->bytes = size;

	return 0;
}

static void cygnus_dma_free_dma_buffers(struct snd_soc_component *component,
					struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;

	substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	if (substream) {
		buf = &substream->dma_buffer;
		if (buf->area) {
			dma_free_coherent(pcm->card->dev, buf->bytes,
				buf->area, buf->addr);
			buf->area = NULL;
		}
	}

	substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	if (substream) {
		buf = &substream->dma_buffer;
		if (buf->area) {
			dma_free_coherent(pcm->card->dev, buf->bytes,
				buf->area, buf->addr);
			buf->area = NULL;
		}
	}
}

static int cygnus_dma_new(struct snd_soc_component *component,
			  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &cygnus_dma_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = cygnus_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			return ret;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = cygnus_pcm_preallocate_dma_buffer(pcm,
				SNDRV_PCM_STREAM_CAPTURE);
		if (ret) {
			cygnus_dma_free_dma_buffers(component, pcm);
			return ret;
		}
	}

	return 0;
}

static struct snd_soc_component_driver cygnus_soc_platform = {
	.open		= cygnus_pcm_open,
	.close		= cygnus_pcm_close,
	.ioctl		= snd_soc_pcm_lib_ioctl,
	.hw_params	= cygnus_pcm_hw_params,
	.hw_free	= cygnus_pcm_hw_free,
	.prepare	= cygnus_pcm_prepare,
	.trigger	= cygnus_pcm_trigger,
	.pointer	= cygnus_pcm_pointer,
	.pcm_construct	= cygnus_dma_new,
	.pcm_destruct	= cygnus_dma_free_dma_buffers,
};

int cygnus_soc_platform_register(struct device *dev,
				 struct cygnus_audio *cygaud)
{
	int rc = 0;

	dev_dbg(dev, "%s Enter\n", __func__);

	rc = devm_request_irq(dev, cygaud->irq_num, cygnus_dma_irq,
				IRQF_SHARED, "cygnus-audio", cygaud);
	if (rc) {
		dev_err(dev, "%s request_irq error %d\n", __func__, rc);
		return rc;
	}

	rc = devm_snd_soc_register_component(dev, &cygnus_soc_platform,
					     NULL, 0);
	if (rc) {
		dev_err(dev, "%s failed\n", __func__);
		return rc;
	}

	return 0;
}

int cygnus_soc_platform_unregister(struct device *dev)
{
	return 0;
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Cygnus ASoC PCM module");
