/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
 * All rights reserved.
 *
 * Portions of this source are based on cwcealdr.cpp and dhwiface.cpp in
 * cwcealdr1.zip, the sample sources by Crystal Semiconductor.
 * Copyright (c) 1996-1998 Crystal Semiconductor Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/chip.h>
#include <dev/sound/pci/csareg.h>
#include <dev/sound/pci/csavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

SND_DECLARE_FILE("$FreeBSD$");

/* Buffer size on dma transfer. Fixed for CS416x. */
#define CS461x_BUFFSIZE   (4 * 1024)

#define GOF_PER_SEC 200

/* device private data */
struct csa_info;

struct csa_chinfo {
	struct csa_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir;
	u_int32_t fmt, spd;
	int dma;
};

struct csa_info {
	csa_res		res; /* resource */
	void		*ih; /* Interrupt cookie */
	bus_dma_tag_t	parent_dmat; /* DMA tag */
	struct csa_bridgeinfo *binfo; /* The state of the parent. */
	struct csa_card *card;

	int active;
	/* Contents of board's registers */
	u_long		pfie;
	u_long		pctl;
	u_long		cctl;
	struct csa_chinfo pch, rch;
	u_int32_t	ac97[CS461x_AC97_NUMBER_RESTORE_REGS];
	u_int32_t	ac97_powerdown;
	u_int32_t	ac97_general_purpose;
};

/* -------------------------------------------------------------------- */

/* prototypes */
static int      csa_init(struct csa_info *);
static void     csa_intr(void *);
static void	csa_setplaysamplerate(csa_res *resp, u_long ulInRate);
static void	csa_setcapturesamplerate(csa_res *resp, u_long ulOutRate);
static void	csa_startplaydma(struct csa_info *csa);
static void	csa_startcapturedma(struct csa_info *csa);
static void	csa_stopplaydma(struct csa_info *csa);
static void	csa_stopcapturedma(struct csa_info *csa);
static int	csa_startdsp(csa_res *resp);
static int	csa_stopdsp(csa_res *resp);
static int	csa_allocres(struct csa_info *scp, device_t dev);
static void	csa_releaseres(struct csa_info *scp, device_t dev);
static void	csa_ac97_suspend(struct csa_info *csa);
static void	csa_ac97_resume(struct csa_info *csa);

static u_int32_t csa_playfmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S8, 1, 0),
	SND_FORMAT(AFMT_S8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S16_BE, 1, 0),
	SND_FORMAT(AFMT_S16_BE, 2, 0),
	0
};
static struct pcmchan_caps csa_playcaps = {8000, 48000, csa_playfmt, 0};

static u_int32_t csa_recfmt[] = {
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};
static struct pcmchan_caps csa_reccaps = {11025, 48000, csa_recfmt, 0};

/* -------------------------------------------------------------------- */

static int
csa_active(struct csa_info *csa, int run)
{
	int old;

	old = csa->active;
	csa->active += run;

	if ((csa->active > 1) || (csa->active < -1))
		csa->active = 0;
	if (csa->card->active)
		return (csa->card->active(!(csa->active && old)));

	return 0;
}

/* -------------------------------------------------------------------- */
/* ac97 codec */

static int
csa_rdcd(kobj_t obj, void *devinfo, int regno)
{
	u_int32_t data;
	struct csa_info *csa = (struct csa_info *)devinfo;

	csa_active(csa, 1);
	if (csa_readcodec(&csa->res, regno + BA0_AC97_RESET, &data))
		data = 0;
	csa_active(csa, -1);

	return data;
}

static int
csa_wrcd(kobj_t obj, void *devinfo, int regno, u_int32_t data)
{
	struct csa_info *csa = (struct csa_info *)devinfo;

	csa_active(csa, 1);
	csa_writecodec(&csa->res, regno + BA0_AC97_RESET, data);
	csa_active(csa, -1);

	return 0;
}

static kobj_method_t csa_ac97_methods[] = {
    	KOBJMETHOD(ac97_read,		csa_rdcd),
    	KOBJMETHOD(ac97_write,		csa_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(csa_ac97);

static void
csa_setplaysamplerate(csa_res *resp, u_long ulInRate)
{
	u_long ulTemp1, ulTemp2;
	u_long ulPhiIncr;
	u_long ulCorrectionPerGOF, ulCorrectionPerSec;
	u_long ulOutRate;

	ulOutRate = 48000;

	/*
	 * Compute the values used to drive the actual sample rate conversion.
	 * The following formulas are being computed, using inline assembly
	 * since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     ulPhiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     ulCorrectionPerGOF = floor((Fs,in * 2^26 - Fs,out * ulPhiIncr) /
	 *                                GOF_PER_SEC)
	 *     ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * ulCorrectionPerGOF
	 *
	 * i.e.
	 *
	 *     ulPhiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     ulCorrectionPerGOF:ulCorrectionPerSec =
	 *         dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	ulTemp1 = ulInRate << 16;
	ulPhiIncr = ulTemp1 / ulOutRate;
	ulTemp1 -= ulPhiIncr * ulOutRate;
	ulTemp1 <<= 10;
	ulPhiIncr <<= 10;
	ulTemp2 = ulTemp1 / ulOutRate;
	ulPhiIncr += ulTemp2;
	ulTemp1 -= ulTemp2 * ulOutRate;
	ulCorrectionPerGOF = ulTemp1 / GOF_PER_SEC;
	ulTemp1 -= ulCorrectionPerGOF * GOF_PER_SEC;
	ulCorrectionPerSec = ulTemp1;

	/*
	 * Fill in the SampleRateConverter control block.
	 */
	csa_writemem(resp, BA1_PSRC, ((ulCorrectionPerSec << 16) & 0xFFFF0000) | (ulCorrectionPerGOF & 0xFFFF));
	csa_writemem(resp, BA1_PPI, ulPhiIncr);
}

static void
csa_setcapturesamplerate(csa_res *resp, u_long ulOutRate)
{
	u_long ulPhiIncr, ulCoeffIncr, ulTemp1, ulTemp2;
	u_long ulCorrectionPerGOF, ulCorrectionPerSec, ulInitialDelay;
	u_long dwFrameGroupLength, dwCnt;
	u_long ulInRate;

	ulInRate = 48000;

	/*
	 * We can only decimate by up to a factor of 1/9th the hardware rate.
	 * Return an error if an attempt is made to stray outside that limit.
	 */
	if((ulOutRate * 9) < ulInRate)
		return;

	/*
	 * We can not capture at at rate greater than the Input Rate (48000).
	 * Return an error if an attempt is made to stray outside that limit.
	 */
	if(ulOutRate > ulInRate)
		return;

	/*
	 * Compute the values used to drive the actual sample rate conversion.
	 * The following formulas are being computed, using inline assembly
	 * since we need to use 64 bit arithmetic to compute the values:
	 *
	 *     ulCoeffIncr = -floor((Fs,out * 2^23) / Fs,in)
	 *     ulPhiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *     ulCorrectionPerGOF = floor((Fs,in * 2^26 - Fs,out * ulPhiIncr) /
	 *                                GOF_PER_SEC)
	 *     ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -
	 *                          GOF_PER_SEC * ulCorrectionPerGOF
	 *     ulInitialDelay = ceil((24 * Fs,in) / Fs,out)
	 *
	 * i.e.
	 *
	 *     ulCoeffIncr = neg(dividend((Fs,out * 2^23) / Fs,in))
	 *     ulPhiIncr:ulOther = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *     ulCorrectionPerGOF:ulCorrectionPerSec =
	 *         dividend:remainder(ulOther / GOF_PER_SEC)
	 *     ulInitialDelay = dividend(((24 * Fs,in) + Fs,out - 1) / Fs,out)
	 */
	ulTemp1 = ulOutRate << 16;
	ulCoeffIncr = ulTemp1 / ulInRate;
	ulTemp1 -= ulCoeffIncr * ulInRate;
	ulTemp1 <<= 7;
	ulCoeffIncr <<= 7;
	ulCoeffIncr += ulTemp1 / ulInRate;
	ulCoeffIncr ^= 0xFFFFFFFF;
	ulCoeffIncr++;
	ulTemp1 = ulInRate << 16;
	ulPhiIncr = ulTemp1 / ulOutRate;
	ulTemp1 -= ulPhiIncr * ulOutRate;
	ulTemp1 <<= 10;
	ulPhiIncr <<= 10;
	ulTemp2 = ulTemp1 / ulOutRate;
	ulPhiIncr += ulTemp2;
	ulTemp1 -= ulTemp2 * ulOutRate;
	ulCorrectionPerGOF = ulTemp1 / GOF_PER_SEC;
	ulTemp1 -= ulCorrectionPerGOF * GOF_PER_SEC;
	ulCorrectionPerSec = ulTemp1;
	ulInitialDelay = ((ulInRate * 24) + ulOutRate - 1) / ulOutRate;

	/*
	 * Fill in the VariDecimate control block.
	 */
	csa_writemem(resp, BA1_CSRC,
		     ((ulCorrectionPerSec << 16) & 0xFFFF0000) | (ulCorrectionPerGOF & 0xFFFF));
	csa_writemem(resp, BA1_CCI, ulCoeffIncr);
	csa_writemem(resp, BA1_CD,
	     (((BA1_VARIDEC_BUF_1 + (ulInitialDelay << 2)) << 16) & 0xFFFF0000) | 0x80);
	csa_writemem(resp, BA1_CPI, ulPhiIncr);

	/*
	 * Figure out the frame group length for the write back task.  Basically,
	 * this is just the factors of 24000 (2^6*3*5^3) that are not present in
	 * the output sample rate.
	 */
	dwFrameGroupLength = 1;
	for(dwCnt = 2; dwCnt <= 64; dwCnt *= 2)
	{
		if(((ulOutRate / dwCnt) * dwCnt) !=
		   ulOutRate)
		{
			dwFrameGroupLength *= 2;
		}
	}
	if(((ulOutRate / 3) * 3) !=
	   ulOutRate)
	{
		dwFrameGroupLength *= 3;
	}
	for(dwCnt = 5; dwCnt <= 125; dwCnt *= 5)
	{
		if(((ulOutRate / dwCnt) * dwCnt) !=
		   ulOutRate)
		{
			dwFrameGroupLength *= 5;
		}
	}

	/*
	 * Fill in the WriteBack control block.
	 */
	csa_writemem(resp, BA1_CFG1, dwFrameGroupLength);
	csa_writemem(resp, BA1_CFG2, (0x00800000 | dwFrameGroupLength));
	csa_writemem(resp, BA1_CCST, 0x0000FFFF);
	csa_writemem(resp, BA1_CSPB, ((65536 * ulOutRate) / 24000));
	csa_writemem(resp, (BA1_CSPB + 4), 0x0000FFFF);
}

static void
csa_startplaydma(struct csa_info *csa)
{
	csa_res *resp;
	u_long ul;

	if (!csa->pch.dma) {
		resp = &csa->res;
		ul = csa_readmem(resp, BA1_PCTL);
		ul &= 0x0000ffff;
		csa_writemem(resp, BA1_PCTL, ul | csa->pctl);
		csa_writemem(resp, BA1_PVOL, 0x80008000);
		csa->pch.dma = 1;
	}
}

static void
csa_startcapturedma(struct csa_info *csa)
{
	csa_res *resp;
	u_long ul;

	if (!csa->rch.dma) {
		resp = &csa->res;
		ul = csa_readmem(resp, BA1_CCTL);
		ul &= 0xffff0000;
		csa_writemem(resp, BA1_CCTL, ul | csa->cctl);
		csa_writemem(resp, BA1_CVOL, 0x80008000);
		csa->rch.dma = 1;
	}
}

static void
csa_stopplaydma(struct csa_info *csa)
{
	csa_res *resp;
	u_long ul;

	if (csa->pch.dma) {
		resp = &csa->res;
		ul = csa_readmem(resp, BA1_PCTL);
		csa->pctl = ul & 0xffff0000;
		csa_writemem(resp, BA1_PCTL, ul & 0x0000ffff);
		csa_writemem(resp, BA1_PVOL, 0xffffffff);
		csa->pch.dma = 0;

		/*
		 * The bitwise pointer of the serial FIFO in the DSP
		 * seems to make an error upon starting or stopping the
		 * DSP. Clear the FIFO and correct the pointer if we
		 * are not capturing.
		 */
		if (!csa->rch.dma) {
			csa_clearserialfifos(resp);
			csa_writeio(resp, BA0_SERBSP, 0);
		}
	}
}

static void
csa_stopcapturedma(struct csa_info *csa)
{
	csa_res *resp;
	u_long ul;

	if (csa->rch.dma) {
		resp = &csa->res;
		ul = csa_readmem(resp, BA1_CCTL);
		csa->cctl = ul & 0x0000ffff;
		csa_writemem(resp, BA1_CCTL, ul & 0xffff0000);
		csa_writemem(resp, BA1_CVOL, 0xffffffff);
		csa->rch.dma = 0;

		/*
		 * The bitwise pointer of the serial FIFO in the DSP
		 * seems to make an error upon starting or stopping the
		 * DSP. Clear the FIFO and correct the pointer if we
		 * are not playing.
		 */
		if (!csa->pch.dma) {
			csa_clearserialfifos(resp);
			csa_writeio(resp, BA0_SERBSP, 0);
		}
	}
}

static int
csa_startdsp(csa_res *resp)
{
	int i;
	u_long ul;

	/*
	 * Set the frame timer to reflect the number of cycles per frame.
	 */
	csa_writemem(resp, BA1_FRMT, 0xadf);

	/*
	 * Turn on the run, run at frame, and DMA enable bits in the local copy of
	 * the SP control register.
	 */
	csa_writemem(resp, BA1_SPCR, SPCR_RUN | SPCR_RUNFR | SPCR_DRQEN);

	/*
	 * Wait until the run at frame bit resets itself in the SP control
	 * register.
	 */
	ul = 0;
	for (i = 0 ; i < 25 ; i++) {
		/*
		 * Wait a little bit, so we don't issue PCI reads too frequently.
		 */
		DELAY(50);
		/*
		 * Fetch the current value of the SP status register.
		 */
		ul = csa_readmem(resp, BA1_SPCR);

		/*
		 * If the run at frame bit has reset, then stop waiting.
		 */
		if((ul & SPCR_RUNFR) == 0)
			break;
	}
	/*
	 * If the run at frame bit never reset, then return an error.
	 */
	if((ul & SPCR_RUNFR) != 0)
		return (EAGAIN);

	return (0);
}

static int
csa_stopdsp(csa_res *resp)
{
	/*
	 * Turn off the run, run at frame, and DMA enable bits in
	 * the local copy of the SP control register.
	 */
	csa_writemem(resp, BA1_SPCR, 0);

	return (0);
}

static int
csa_setupchan(struct csa_chinfo *ch)
{
	struct csa_info *csa = ch->parent;
	csa_res *resp = &csa->res;
	u_long pdtc, tmp;

	if (ch->dir == PCMDIR_PLAY) {
		/* direction */
		csa_writemem(resp, BA1_PBA, sndbuf_getbufaddr(ch->buffer));

		/* format */
		csa->pfie = csa_readmem(resp, BA1_PFIE) & ~0x0000f03f;
		if (!(ch->fmt & AFMT_SIGNED))
			csa->pfie |= 0x8000;
		if (ch->fmt & AFMT_BIGENDIAN)
			csa->pfie |= 0x4000;
		if (AFMT_CHANNEL(ch->fmt) < 2)
			csa->pfie |= 0x2000;
		if (ch->fmt & AFMT_8BIT)
			csa->pfie |= 0x1000;
		csa_writemem(resp, BA1_PFIE, csa->pfie);

		tmp = 4;
		if (ch->fmt & AFMT_16BIT)
			tmp <<= 1;
		if (AFMT_CHANNEL(ch->fmt) > 1)
			tmp <<= 1;
		tmp--;

		pdtc = csa_readmem(resp, BA1_PDTC) & ~0x000001ff;
		pdtc |= tmp;
		csa_writemem(resp, BA1_PDTC, pdtc);

		/* rate */
		csa_setplaysamplerate(resp, ch->spd);
	} else if (ch->dir == PCMDIR_REC) {
		/* direction */
		csa_writemem(resp, BA1_CBA, sndbuf_getbufaddr(ch->buffer));

		/* format */
		csa_writemem(resp, BA1_CIE, (csa_readmem(resp, BA1_CIE) & ~0x0000003f) | 0x00000001);

		/* rate */
		csa_setcapturesamplerate(resp, ch->spd);
	}
	return 0;
}

/* -------------------------------------------------------------------- */
/* channel interface */

static void *
csachan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct csa_info *csa = devinfo;
	struct csa_chinfo *ch = (dir == PCMDIR_PLAY)? &csa->pch : &csa->rch;

	ch->parent = csa;
	ch->channel = c;
	ch->buffer = b;
	ch->dir = dir;
	if (sndbuf_alloc(ch->buffer, csa->parent_dmat, 0, CS461x_BUFFSIZE) != 0)
		return NULL;
	return ch;
}

static int
csachan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct csa_chinfo *ch = data;

	ch->fmt = format;
	return 0;
}

static u_int32_t
csachan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct csa_chinfo *ch = data;

	ch->spd = speed;
	return ch->spd; /* XXX calc real speed */
}

static u_int32_t
csachan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	return CS461x_BUFFSIZE / 2;
}

static int
csachan_trigger(kobj_t obj, void *data, int go)
{
	struct csa_chinfo *ch = data;
	struct csa_info *csa = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return 0;

	if (go == PCMTRIG_START) {
		csa_active(csa, 1);
		csa_setupchan(ch);
		if (ch->dir == PCMDIR_PLAY)
			csa_startplaydma(csa);
		else
			csa_startcapturedma(csa);
	} else {
		if (ch->dir == PCMDIR_PLAY)
			csa_stopplaydma(csa);
		else
			csa_stopcapturedma(csa);
		csa_active(csa, -1);
	}
	return 0;
}

static u_int32_t
csachan_getptr(kobj_t obj, void *data)
{
	struct csa_chinfo *ch = data;
	struct csa_info *csa = ch->parent;
	csa_res *resp;
	u_int32_t ptr;

	resp = &csa->res;

	if (ch->dir == PCMDIR_PLAY) {
		ptr = csa_readmem(resp, BA1_PBA) - sndbuf_getbufaddr(ch->buffer);
		if ((ch->fmt & AFMT_U8) != 0 || (ch->fmt & AFMT_S8) != 0)
			ptr >>= 1;
	} else {
		ptr = csa_readmem(resp, BA1_CBA) - sndbuf_getbufaddr(ch->buffer);
		if ((ch->fmt & AFMT_U8) != 0 || (ch->fmt & AFMT_S8) != 0)
			ptr >>= 1;
	}

	return (ptr);
}

static struct pcmchan_caps *
csachan_getcaps(kobj_t obj, void *data)
{
	struct csa_chinfo *ch = data;
	return (ch->dir == PCMDIR_PLAY)? &csa_playcaps : &csa_reccaps;
}

static kobj_method_t csachan_methods[] = {
    	KOBJMETHOD(channel_init,		csachan_init),
    	KOBJMETHOD(channel_setformat,		csachan_setformat),
    	KOBJMETHOD(channel_setspeed,		csachan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	csachan_setblocksize),
    	KOBJMETHOD(channel_trigger,		csachan_trigger),
    	KOBJMETHOD(channel_getptr,		csachan_getptr),
    	KOBJMETHOD(channel_getcaps,		csachan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(csachan);

/* -------------------------------------------------------------------- */
/* The interrupt handler */
static void
csa_intr(void *p)
{
	struct csa_info *csa = p;

	if ((csa->binfo->hisr & HISR_VC0) != 0)
		chn_intr(csa->pch.channel);
	if ((csa->binfo->hisr & HISR_VC1) != 0)
		chn_intr(csa->rch.channel);
}

/* -------------------------------------------------------------------- */

/*
 * Probe and attach the card
 */

static int
csa_init(struct csa_info *csa)
{
	csa_res *resp;

	resp = &csa->res;

	csa->pfie = 0;
	csa_stopplaydma(csa);
	csa_stopcapturedma(csa);

	if (csa_startdsp(resp))
		return (1);

	/* Crank up the power on the DAC and ADC. */
	csa_setplaysamplerate(resp, 8000);
	csa_setcapturesamplerate(resp, 8000);
	/* Set defaults */
	csa_writeio(resp, BA0_EGPIODR, EGPIODR_GPOE0);
	csa_writeio(resp, BA0_EGPIOPTR, EGPIOPTR_GPPT0);
	/* Power up amplifier */
	csa_writeio(resp, BA0_EGPIODR, csa_readio(resp, BA0_EGPIODR) |
		EGPIODR_GPOE2);
	csa_writeio(resp, BA0_EGPIOPTR, csa_readio(resp, BA0_EGPIOPTR) | 
		EGPIOPTR_GPPT2);

	return 0;
}

/* Allocates resources. */
static int
csa_allocres(struct csa_info *csa, device_t dev)
{
	csa_res *resp;

	resp = &csa->res;
	if (resp->io == NULL) {
		resp->io = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
			&resp->io_rid, RF_ACTIVE);
		if (resp->io == NULL)
			return (1);
	}
	if (resp->mem == NULL) {
		resp->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
			&resp->mem_rid, RF_ACTIVE);
		if (resp->mem == NULL)
			return (1);
	}
	if (resp->irq == NULL) {
		resp->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
			&resp->irq_rid, RF_ACTIVE | RF_SHAREABLE);
		if (resp->irq == NULL)
			return (1);
	}
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev),
			       /*alignment*/CS461x_BUFFSIZE,
			       /*boundary*/CS461x_BUFFSIZE,
			       /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			       /*highaddr*/BUS_SPACE_MAXADDR,
			       /*filter*/NULL, /*filterarg*/NULL,
			       /*maxsize*/CS461x_BUFFSIZE, /*nsegments*/1, /*maxsegz*/0x3ffff,
			       /*flags*/0, /*lockfunc*/busdma_lock_mutex,
			       /*lockarg*/&Giant, &csa->parent_dmat) != 0)
		return (1);

	return (0);
}

/* Releases resources. */
static void
csa_releaseres(struct csa_info *csa, device_t dev)
{
	csa_res *resp;

	KASSERT(csa != NULL, ("called with bogus resource structure"));

	resp = &csa->res;
	if (resp->irq != NULL) {
		if (csa->ih)
			bus_teardown_intr(dev, resp->irq, csa->ih);
		bus_release_resource(dev, SYS_RES_IRQ, resp->irq_rid, resp->irq);
		resp->irq = NULL;
	}
	if (resp->io != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, resp->io_rid, resp->io);
		resp->io = NULL;
	}
	if (resp->mem != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, resp->mem_rid, resp->mem);
		resp->mem = NULL;
	}
	if (csa->parent_dmat != NULL) {
		bus_dma_tag_destroy(csa->parent_dmat);
		csa->parent_dmat = NULL;
	}

	free(csa, M_DEVBUF);
}

static int
pcmcsa_probe(device_t dev)
{
	char *s;
	struct sndcard_func *func;

	/* The parent device has already been probed. */

	func = device_get_ivars(dev);
	if (func == NULL || func->func != SCF_PCM)
		return (ENXIO);

	s = "CS461x PCM Audio";

	device_set_desc(dev, s);
	return (0);
}

static int
pcmcsa_attach(device_t dev)
{
	struct csa_info *csa;
	csa_res *resp;
	int unit;
	char status[SND_STATUSLEN];
	struct ac97_info *codec;
	struct sndcard_func *func;

	csa = malloc(sizeof(*csa), M_DEVBUF, M_WAITOK | M_ZERO);
	unit = device_get_unit(dev);
	func = device_get_ivars(dev);
	csa->binfo = func->varinfo;
	/*
	 * Fake the status of DMA so that the initial value of
	 * PCTL and CCTL can be stored into csa->pctl and csa->cctl,
	 * respectively.
	 */
	csa->pch.dma = csa->rch.dma = 1;
	csa->active = 0;
	csa->card = csa->binfo->card;

	/* Allocate the resources. */
	resp = &csa->res;
	resp->io_rid = PCIR_BAR(0);
	resp->mem_rid = PCIR_BAR(1);
	resp->irq_rid = 0;
	if (csa_allocres(csa, dev)) {
		csa_releaseres(csa, dev);
		return (ENXIO);
	}

	csa_active(csa, 1);
	if (csa_init(csa)) {
		csa_releaseres(csa, dev);
		return (ENXIO);
	}
	codec = AC97_CREATE(dev, csa, csa_ac97);
	if (codec == NULL) {
		csa_releaseres(csa, dev);
		return (ENXIO);
	}
	if (csa->card->inv_eapd)
		ac97_setflags(codec, AC97_F_EAPD_INV);
	if (mixer_init(dev, ac97_getmixerclass(), codec) == -1) {
		ac97_destroy(codec);
		csa_releaseres(csa, dev);
		return (ENXIO);
	}

	snprintf(status, SND_STATUSLEN, "at irq %jd %s",
			rman_get_start(resp->irq),PCM_KLDSTRING(snd_csa));

	/* Enable interrupt. */
	if (snd_setup_intr(dev, resp->irq, 0, csa_intr, csa, &csa->ih)) {
		ac97_destroy(codec);
		csa_releaseres(csa, dev);
		return (ENXIO);
	}
	csa_writemem(resp, BA1_PFIE, csa_readmem(resp, BA1_PFIE) & ~0x0000f03f);
	csa_writemem(resp, BA1_CIE, (csa_readmem(resp, BA1_CIE) & ~0x0000003f) | 0x00000001);
	csa_active(csa, -1);

	if (pcm_register(dev, csa, 1, 1)) {
		ac97_destroy(codec);
		csa_releaseres(csa, dev);
		return (ENXIO);
	}
	pcm_addchan(dev, PCMDIR_REC, &csachan_class, csa);
	pcm_addchan(dev, PCMDIR_PLAY, &csachan_class, csa);
	pcm_setstatus(dev, status);

	return (0);
}

static int
pcmcsa_detach(device_t dev)
{
	int r;
	struct csa_info *csa;

	r = pcm_unregister(dev);
	if (r)
		return r;

	csa = pcm_getdevinfo(dev);
	csa_releaseres(csa, dev);

	return 0;
}

static void
csa_ac97_suspend(struct csa_info *csa)
{
	int count, i;
	uint32_t tmp;

	for (count = 0x2, i=0;
	    (count <= CS461x_AC97_HIGHESTREGTORESTORE) &&
	    (i < CS461x_AC97_NUMBER_RESTORE_REGS);
	    count += 2, i++)
		csa_readcodec(&csa->res, BA0_AC97_RESET + count, &csa->ac97[i]);

	/* mute the outputs */
	csa_writecodec(&csa->res, BA0_AC97_MASTER_VOLUME, 0x8000);
	csa_writecodec(&csa->res, BA0_AC97_HEADPHONE_VOLUME, 0x8000);
	csa_writecodec(&csa->res, BA0_AC97_MASTER_VOLUME_MONO, 0x8000);
	csa_writecodec(&csa->res, BA0_AC97_PCM_OUT_VOLUME, 0x8000);
	/* save the registers that cause pops */
	csa_readcodec(&csa->res, BA0_AC97_POWERDOWN, &csa->ac97_powerdown);
	csa_readcodec(&csa->res, BA0_AC97_GENERAL_PURPOSE,
	    &csa->ac97_general_purpose);

	/*
	 * And power down everything on the AC97 codec. Well, for now,
	 * only power down the DAC/ADC and MIXER VREFON components.
	 * trouble with removing VREF.
	 */

	/* MIXVON */
	csa_readcodec(&csa->res, BA0_AC97_POWERDOWN, &tmp);
	csa_writecodec(&csa->res, BA0_AC97_POWERDOWN,
	    tmp | CS_AC97_POWER_CONTROL_MIXVON);
	/* ADC */
	csa_readcodec(&csa->res, BA0_AC97_POWERDOWN, &tmp);
	csa_writecodec(&csa->res, BA0_AC97_POWERDOWN,
	    tmp | CS_AC97_POWER_CONTROL_ADC);
	/* DAC */
	csa_readcodec(&csa->res, BA0_AC97_POWERDOWN, &tmp);
	csa_writecodec(&csa->res, BA0_AC97_POWERDOWN,
	    tmp | CS_AC97_POWER_CONTROL_DAC);
}

static void
csa_ac97_resume(struct csa_info *csa)
{
	int count, i;

	/*
	 * First, we restore the state of the general purpose register.  This
	 * contains the mic select (mic1 or mic2) and if we restore this after
	 * we restore the mic volume/boost state and mic2 was selected at
	 * suspend time, we will end up with a brief period of time where mic1
	 * is selected with the volume/boost settings for mic2, causing
	 * acoustic feedback.  So we restore the general purpose register
	 * first, thereby getting the correct mic selected before we restore
	 * the mic volume/boost.
	 */
	csa_writecodec(&csa->res, BA0_AC97_GENERAL_PURPOSE,
	    csa->ac97_general_purpose);
	/*
	 * Now, while the outputs are still muted, restore the state of power
	 * on the AC97 part.
	 */
	csa_writecodec(&csa->res, BA0_AC97_POWERDOWN, csa->ac97_powerdown);
	/*
	 * Restore just the first set of registers, from register number
	 * 0x02 to the register number that ulHighestRegToRestore specifies.
	 */
	for (count = 0x2, i=0;
	    (count <= CS461x_AC97_HIGHESTREGTORESTORE) &&
	    (i < CS461x_AC97_NUMBER_RESTORE_REGS);
	    count += 2, i++)
		csa_writecodec(&csa->res, BA0_AC97_RESET + count, csa->ac97[i]);
}

static int
pcmcsa_suspend(device_t dev)
{
	struct csa_info *csa;
	csa_res *resp;

	csa = pcm_getdevinfo(dev);
	resp = &csa->res;

	csa_active(csa, 1);

	/* playback interrupt disable */
	csa_writemem(resp, BA1_PFIE,
	    (csa_readmem(resp, BA1_PFIE) & ~0x0000f03f) | 0x00000010);
	/* capture interrupt disable */
	csa_writemem(resp, BA1_CIE,
	    (csa_readmem(resp, BA1_CIE) & ~0x0000003f) | 0x00000011);
	csa_stopplaydma(csa);
	csa_stopcapturedma(csa);

	csa_ac97_suspend(csa);

	csa_resetdsp(resp);

	csa_stopdsp(resp);
	/*
	 *  Power down the DAC and ADC.  For now leave the other areas on.
	 */
	csa_writecodec(&csa->res, BA0_AC97_POWERDOWN, 0x300);
	/*
	 *  Power down the PLL.
	 */
	csa_writemem(resp, BA0_CLKCR1, 0);
	/*
	 * Turn off the Processor by turning off the software clock
	 * enable flag in the clock control register.
	 */
	csa_writemem(resp, BA0_CLKCR1,
	    csa_readmem(resp, BA0_CLKCR1) & ~CLKCR1_SWCE);

	csa_active(csa, -1);

	return 0;
}

static int
pcmcsa_resume(device_t dev)
{
	struct csa_info *csa;
	csa_res *resp;

	csa = pcm_getdevinfo(dev);
	resp = &csa->res;

	csa_active(csa, 1);

	/* cs_hardware_init */
	csa_stopplaydma(csa);
	csa_stopcapturedma(csa);
	csa_ac97_resume(csa);
	if (csa_startdsp(resp))
		return (ENXIO);
	/* Enable interrupts on the part. */
	if ((csa_readio(resp, BA0_HISR) & HISR_INTENA) == 0)
		csa_writeio(resp, BA0_HICR, HICR_IEV | HICR_CHGM);
	/* playback interrupt enable */
	csa_writemem(resp, BA1_PFIE, csa_readmem(resp, BA1_PFIE) & ~0x0000f03f);
	/* capture interrupt enable */
	csa_writemem(resp, BA1_CIE,
	    (csa_readmem(resp, BA1_CIE) & ~0x0000003f) | 0x00000001);
	/* cs_restart_part */
	csa_setupchan(&csa->pch);
	csa_startplaydma(csa);
	csa_setupchan(&csa->rch);
	csa_startcapturedma(csa);

	csa_active(csa, -1);

	return 0;
}

static device_method_t pcmcsa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe , pcmcsa_probe ),
	DEVMETHOD(device_attach, pcmcsa_attach),
	DEVMETHOD(device_detach, pcmcsa_detach),
	DEVMETHOD(device_suspend, pcmcsa_suspend),
	DEVMETHOD(device_resume, pcmcsa_resume),

	{ 0, 0 },
};

static driver_t pcmcsa_driver = {
	"pcm",
	pcmcsa_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_csapcm, csa, pcmcsa_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_csapcm, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_csapcm, snd_csa, 1, 1, 1);
MODULE_VERSION(snd_csapcm, 1);
