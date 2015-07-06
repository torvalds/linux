/*
 *  Maintained by Jaroslav Kysela <perex@perex.cz>
 *  Originated by audio@tridentmicro.com
 *  Fri Feb 19 15:55:28 MST 1999
 *  Routines for control of Trident 4DWave (DX and NX) chip
 *
 *  BUGS:
 *
 *  TODO:
 *    ---
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *
 *  SiS7018 S/PDIF support by Thomas Winischhofer <thomas@winischhofer.net>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gameport.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include "trident.h"
#include <sound/asoundef.h>

static int snd_trident_pcm_mixer_build(struct snd_trident *trident,
				       struct snd_trident_voice * voice,
				       struct snd_pcm_substream *substream);
static int snd_trident_pcm_mixer_free(struct snd_trident *trident,
				      struct snd_trident_voice * voice,
				      struct snd_pcm_substream *substream);
static irqreturn_t snd_trident_interrupt(int irq, void *dev_id);
static int snd_trident_sis_reset(struct snd_trident *trident);

static void snd_trident_clear_voices(struct snd_trident * trident,
				     unsigned short v_min, unsigned short v_max);
static int snd_trident_free(struct snd_trident *trident);

/*
 *  common I/O routines
 */


#if 0
static void snd_trident_print_voice_regs(struct snd_trident *trident, int voice)
{
	unsigned int val, tmp;

	dev_dbg(trident->card->dev, "Trident voice %i:\n", voice);
	outb(voice, TRID_REG(trident, T4D_LFO_GC_CIR));
	val = inl(TRID_REG(trident, CH_LBA));
	dev_dbg(trident->card->dev, "LBA: 0x%x\n", val);
	val = inl(TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC));
	dev_dbg(trident->card->dev, "GVSel: %i\n", val >> 31);
	dev_dbg(trident->card->dev, "Pan: 0x%x\n", (val >> 24) & 0x7f);
	dev_dbg(trident->card->dev, "Vol: 0x%x\n", (val >> 16) & 0xff);
	dev_dbg(trident->card->dev, "CTRL: 0x%x\n", (val >> 12) & 0x0f);
	dev_dbg(trident->card->dev, "EC: 0x%x\n", val & 0x0fff);
	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		val = inl(TRID_REG(trident, CH_DX_CSO_ALPHA_FMS));
		dev_dbg(trident->card->dev, "CSO: 0x%x\n", val >> 16);
		dev_dbg(trident->card->dev, "Alpha: 0x%x\n", (val >> 4) & 0x0fff);
		dev_dbg(trident->card->dev, "FMS: 0x%x\n", val & 0x0f);
		val = inl(TRID_REG(trident, CH_DX_ESO_DELTA));
		dev_dbg(trident->card->dev, "ESO: 0x%x\n", val >> 16);
		dev_dbg(trident->card->dev, "Delta: 0x%x\n", val & 0xffff);
		val = inl(TRID_REG(trident, CH_DX_FMC_RVOL_CVOL));
	} else {		// TRIDENT_DEVICE_ID_NX
		val = inl(TRID_REG(trident, CH_NX_DELTA_CSO));
		tmp = (val >> 24) & 0xff;
		dev_dbg(trident->card->dev, "CSO: 0x%x\n", val & 0x00ffffff);
		val = inl(TRID_REG(trident, CH_NX_DELTA_ESO));
		tmp |= (val >> 16) & 0xff00;
		dev_dbg(trident->card->dev, "Delta: 0x%x\n", tmp);
		dev_dbg(trident->card->dev, "ESO: 0x%x\n", val & 0x00ffffff);
		val = inl(TRID_REG(trident, CH_NX_ALPHA_FMS_FMC_RVOL_CVOL));
		dev_dbg(trident->card->dev, "Alpha: 0x%x\n", val >> 20);
		dev_dbg(trident->card->dev, "FMS: 0x%x\n", (val >> 16) & 0x0f);
	}
	dev_dbg(trident->card->dev, "FMC: 0x%x\n", (val >> 14) & 3);
	dev_dbg(trident->card->dev, "RVol: 0x%x\n", (val >> 7) & 0x7f);
	dev_dbg(trident->card->dev, "CVol: 0x%x\n", val & 0x7f);
}
#endif

/*---------------------------------------------------------------------------
   unsigned short snd_trident_codec_read(struct snd_ac97 *ac97, unsigned short reg)
  
   Description: This routine will do all of the reading from the external
                CODEC (AC97).
  
   Parameters:  ac97 - ac97 codec structure
                reg - CODEC register index, from AC97 Hal.
 
   returns:     16 bit value read from the AC97.
  
  ---------------------------------------------------------------------------*/
static unsigned short snd_trident_codec_read(struct snd_ac97 *ac97, unsigned short reg)
{
	unsigned int data = 0, treg;
	unsigned short count = 0xffff;
	unsigned long flags;
	struct snd_trident *trident = ac97->private_data;

	spin_lock_irqsave(&trident->reg_lock, flags);
	if (trident->device == TRIDENT_DEVICE_ID_DX) {
		data = (DX_AC97_BUSY_READ | (reg & 0x000000ff));
		outl(data, TRID_REG(trident, DX_ACR1_AC97_R));
		do {
			data = inl(TRID_REG(trident, DX_ACR1_AC97_R));
			if ((data & DX_AC97_BUSY_READ) == 0)
				break;
		} while (--count);
	} else if (trident->device == TRIDENT_DEVICE_ID_NX) {
		data = (NX_AC97_BUSY_READ | (reg & 0x000000ff));
		treg = ac97->num == 0 ? NX_ACR2_AC97_R_PRIMARY : NX_ACR3_AC97_R_SECONDARY;
		outl(data, TRID_REG(trident, treg));
		do {
			data = inl(TRID_REG(trident, treg));
			if ((data & 0x00000C00) == 0)
				break;
		} while (--count);
	} else if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		data = SI_AC97_BUSY_READ | SI_AC97_AUDIO_BUSY | (reg & 0x000000ff);
		if (ac97->num == 1)
			data |= SI_AC97_SECONDARY;
		outl(data, TRID_REG(trident, SI_AC97_READ));
		do {
			data = inl(TRID_REG(trident, SI_AC97_READ));
			if ((data & (SI_AC97_BUSY_READ)) == 0)
				break;
		} while (--count);
	}

	if (count == 0 && !trident->ac97_detect) {
		dev_err(trident->card->dev,
			"ac97 codec read TIMEOUT [0x%x/0x%x]!!!\n",
			   reg, data);
		data = 0;
	}

	spin_unlock_irqrestore(&trident->reg_lock, flags);
	return ((unsigned short) (data >> 16));
}

/*---------------------------------------------------------------------------
   void snd_trident_codec_write(struct snd_ac97 *ac97, unsigned short reg,
   unsigned short wdata)
  
   Description: This routine will do all of the writing to the external
                CODEC (AC97).
  
   Parameters:	ac97 - ac97 codec structure
   	        reg - CODEC register index, from AC97 Hal.
                data  - Lower 16 bits are the data to write to CODEC.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/
static void snd_trident_codec_write(struct snd_ac97 *ac97, unsigned short reg,
				    unsigned short wdata)
{
	unsigned int address, data;
	unsigned short count = 0xffff;
	unsigned long flags;
	struct snd_trident *trident = ac97->private_data;

	data = ((unsigned long) wdata) << 16;

	spin_lock_irqsave(&trident->reg_lock, flags);
	if (trident->device == TRIDENT_DEVICE_ID_DX) {
		address = DX_ACR0_AC97_W;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & DX_AC97_BUSY_WRITE) == 0)
				break;
		} while (--count);

		data |= (DX_AC97_BUSY_WRITE | (reg & 0x000000ff));
	} else if (trident->device == TRIDENT_DEVICE_ID_NX) {
		address = NX_ACR1_AC97_W;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & NX_AC97_BUSY_WRITE) == 0)
				break;
		} while (--count);

		data |= (NX_AC97_BUSY_WRITE | (ac97->num << 8) | (reg & 0x000000ff));
	} else if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		address = SI_AC97_WRITE;

		/* read AC-97 write register status */
		do {
			if ((inw(TRID_REG(trident, address)) & (SI_AC97_BUSY_WRITE)) == 0)
				break;
		} while (--count);

		data |= SI_AC97_BUSY_WRITE | SI_AC97_AUDIO_BUSY | (reg & 0x000000ff);
		if (ac97->num == 1)
			data |= SI_AC97_SECONDARY;
	} else {
		address = 0;	/* keep GCC happy */
		count = 0;	/* return */
	}

	if (count == 0) {
		spin_unlock_irqrestore(&trident->reg_lock, flags);
		return;
	}
	outl(data, TRID_REG(trident, address));
	spin_unlock_irqrestore(&trident->reg_lock, flags);
}

/*---------------------------------------------------------------------------
   void snd_trident_enable_eso(struct snd_trident *trident)
  
   Description: This routine will enable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
                Also enables middle of loop interrupts.
  
   Parameters:  trident - pointer to target device class for 4DWave.
  
  ---------------------------------------------------------------------------*/

static void snd_trident_enable_eso(struct snd_trident * trident)
{
	unsigned int val;

	val = inl(TRID_REG(trident, T4D_LFO_GC_CIR));
	val |= ENDLP_IE;
	val |= MIDLP_IE;
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		val |= BANK_B_EN;
	outl(val, TRID_REG(trident, T4D_LFO_GC_CIR));
}

/*---------------------------------------------------------------------------
   void snd_trident_disable_eso(struct snd_trident *trident)
  
   Description: This routine will disable end of loop interrupts.
                End of loop interrupts will occur when a running
                channel reaches ESO.
                Also disables middle of loop interrupts.
  
   Parameters:  
                trident - pointer to target device class for 4DWave.
  
   returns:     TRUE if everything went ok, else FALSE.
  
  ---------------------------------------------------------------------------*/

static void snd_trident_disable_eso(struct snd_trident * trident)
{
	unsigned int tmp;

	tmp = inl(TRID_REG(trident, T4D_LFO_GC_CIR));
	tmp &= ~ENDLP_IE;
	tmp &= ~MIDLP_IE;
	outl(tmp, TRID_REG(trident, T4D_LFO_GC_CIR));
}

/*---------------------------------------------------------------------------
   void snd_trident_start_voice(struct snd_trident * trident, unsigned int voice)

    Description: Start a voice, any channel 0 thru 63.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.

    Parameters : voice - Voice number 0 thru n.
                 trident - pointer to target device class for 4DWave.

    Return Value: None.

  ---------------------------------------------------------------------------*/

void snd_trident_start_voice(struct snd_trident * trident, unsigned int voice)
{
	unsigned int mask = 1 << (voice & 0x1f);
	unsigned int reg = (voice & 0x20) ? T4D_START_B : T4D_START_A;

	outl(mask, TRID_REG(trident, reg));
}

EXPORT_SYMBOL(snd_trident_start_voice);

/*---------------------------------------------------------------------------
   void snd_trident_stop_voice(struct snd_trident * trident, unsigned int voice)

    Description: Stop a voice, any channel 0 thru 63.
                 This routine automatically handles the fact that there are
                 more than 32 channels available.

    Parameters : voice - Voice number 0 thru n.
                 trident - pointer to target device class for 4DWave.

    Return Value: None.

  ---------------------------------------------------------------------------*/

void snd_trident_stop_voice(struct snd_trident * trident, unsigned int voice)
{
	unsigned int mask = 1 << (voice & 0x1f);
	unsigned int reg = (voice & 0x20) ? T4D_STOP_B : T4D_STOP_A;

	outl(mask, TRID_REG(trident, reg));
}

EXPORT_SYMBOL(snd_trident_stop_voice);

/*---------------------------------------------------------------------------
    int snd_trident_allocate_pcm_channel(struct snd_trident *trident)
  
    Description: Allocate hardware channel in Bank B (32-63).
  
    Parameters :  trident - pointer to target device class for 4DWave.
  
    Return Value: hardware channel - 32-63 or -1 when no channel is available
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_pcm_channel(struct snd_trident * trident)
{
	int idx;

	if (trident->ChanPCMcnt >= trident->ChanPCM)
		return -1;
	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[T4D_BANK_B] & (1 << idx))) {
			trident->ChanMap[T4D_BANK_B] |= 1 << idx;
			trident->ChanPCMcnt++;
			return idx + 32;
		}
	}
	return -1;
}

/*---------------------------------------------------------------------------
    void snd_trident_free_pcm_channel(int channel)
  
    Description: Free hardware channel in Bank B (32-63)
  
    Parameters :  trident - pointer to target device class for 4DWave.
	          channel - hardware channel number 0-63
  
    Return Value: none
  
  ---------------------------------------------------------------------------*/

static void snd_trident_free_pcm_channel(struct snd_trident *trident, int channel)
{
	if (channel < 32 || channel > 63)
		return;
	channel &= 0x1f;
	if (trident->ChanMap[T4D_BANK_B] & (1 << channel)) {
		trident->ChanMap[T4D_BANK_B] &= ~(1 << channel);
		trident->ChanPCMcnt--;
	}
}

/*---------------------------------------------------------------------------
    unsigned int snd_trident_allocate_synth_channel(void)
  
    Description: Allocate hardware channel in Bank A (0-31).
  
    Parameters :  trident - pointer to target device class for 4DWave.
  
    Return Value: hardware channel - 0-31 or -1 when no channel is available
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_synth_channel(struct snd_trident * trident)
{
	int idx;

	for (idx = 31; idx >= 0; idx--) {
		if (!(trident->ChanMap[T4D_BANK_A] & (1 << idx))) {
			trident->ChanMap[T4D_BANK_A] |= 1 << idx;
			trident->synth.ChanSynthCount++;
			return idx;
		}
	}
	return -1;
}

/*---------------------------------------------------------------------------
    void snd_trident_free_synth_channel( int channel )
  
    Description: Free hardware channel in Bank B (0-31).
  
    Parameters :  trident - pointer to target device class for 4DWave.
	          channel - hardware channel number 0-63
  
    Return Value: none
  
  ---------------------------------------------------------------------------*/

static void snd_trident_free_synth_channel(struct snd_trident *trident, int channel)
{
	if (channel < 0 || channel > 31)
		return;
	channel &= 0x1f;
	if (trident->ChanMap[T4D_BANK_A] & (1 << channel)) {
		trident->ChanMap[T4D_BANK_A] &= ~(1 << channel);
		trident->synth.ChanSynthCount--;
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_voice_regs
  
   Description: This routine will complete and write the 5 hardware channel
                registers to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Each register field.
  
  ---------------------------------------------------------------------------*/

void snd_trident_write_voice_regs(struct snd_trident * trident,
				  struct snd_trident_voice * voice)
{
	unsigned int FmcRvolCvol;
	unsigned int regs[5];

	regs[1] = voice->LBA;
	regs[4] = (voice->GVSel << 31) |
		  ((voice->Pan & 0x0000007f) << 24) |
		  ((voice->CTRL & 0x0000000f) << 12);
	FmcRvolCvol = ((voice->FMC & 3) << 14) |
	              ((voice->RVol & 0x7f) << 7) |
	              (voice->CVol & 0x7f);

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_SI7018:
		regs[4] |= voice->number > 31 ?
				(voice->Vol & 0x000003ff) :
				((voice->Vol & 0x00003fc) << (16-2)) |
				(voice->EC & 0x00000fff);
		regs[0] = (voice->CSO << 16) | ((voice->Alpha & 0x00000fff) << 4) |
			(voice->FMS & 0x0000000f);
		regs[2] = (voice->ESO << 16) | (voice->Delta & 0x0ffff);
		regs[3] = (voice->Attribute << 16) | FmcRvolCvol;
		break;
	case TRIDENT_DEVICE_ID_DX:
		regs[4] |= ((voice->Vol & 0x000003fc) << (16-2)) |
			   (voice->EC & 0x00000fff);
		regs[0] = (voice->CSO << 16) | ((voice->Alpha & 0x00000fff) << 4) |
			(voice->FMS & 0x0000000f);
		regs[2] = (voice->ESO << 16) | (voice->Delta & 0x0ffff);
		regs[3] = FmcRvolCvol;
		break;
	case TRIDENT_DEVICE_ID_NX:
		regs[4] |= ((voice->Vol & 0x000003fc) << (16-2)) |
			   (voice->EC & 0x00000fff);
		regs[0] = (voice->Delta << 24) | (voice->CSO & 0x00ffffff);
		regs[2] = ((voice->Delta << 16) & 0xff000000) |
			(voice->ESO & 0x00ffffff);
		regs[3] = (voice->Alpha << 20) |
			((voice->FMS & 0x0000000f) << 16) | FmcRvolCvol;
		break;
	default:
		snd_BUG();
		return;
	}

	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outl(regs[0], TRID_REG(trident, CH_START + 0));
	outl(regs[1], TRID_REG(trident, CH_START + 4));
	outl(regs[2], TRID_REG(trident, CH_START + 8));
	outl(regs[3], TRID_REG(trident, CH_START + 12));
	outl(regs[4], TRID_REG(trident, CH_START + 16));

#if 0
	dev_dbg(trident->card->dev, "written %i channel:\n", voice->number);
	dev_dbg(trident->card->dev, "  regs[0] = 0x%x/0x%x\n",
	       regs[0], inl(TRID_REG(trident, CH_START + 0)));
	dev_dbg(trident->card->dev, "  regs[1] = 0x%x/0x%x\n",
	       regs[1], inl(TRID_REG(trident, CH_START + 4)));
	dev_dbg(trident->card->dev, "  regs[2] = 0x%x/0x%x\n",
	       regs[2], inl(TRID_REG(trident, CH_START + 8)));
	dev_dbg(trident->card->dev, "  regs[3] = 0x%x/0x%x\n",
	       regs[3], inl(TRID_REG(trident, CH_START + 12)));
	dev_dbg(trident->card->dev, "  regs[4] = 0x%x/0x%x\n",
	       regs[4], inl(TRID_REG(trident, CH_START + 16)));
#endif
}

EXPORT_SYMBOL(snd_trident_write_voice_regs);

/*---------------------------------------------------------------------------
   snd_trident_write_cso_reg
  
   Description: This routine will write the new CSO offset
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                CSO - new CSO value
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_cso_reg(struct snd_trident * trident,
				      struct snd_trident_voice * voice,
				      unsigned int CSO)
{
	voice->CSO = CSO;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		outw(voice->CSO, TRID_REG(trident, CH_DX_CSO_ALPHA_FMS) + 2);
	} else {
		outl((voice->Delta << 24) |
		     (voice->CSO & 0x00ffffff), TRID_REG(trident, CH_NX_DELTA_CSO));
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_eso_reg
  
   Description: This routine will write the new ESO offset
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                ESO - new ESO value
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_eso_reg(struct snd_trident * trident,
				      struct snd_trident_voice * voice,
				      unsigned int ESO)
{
	voice->ESO = ESO;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		outw(voice->ESO, TRID_REG(trident, CH_DX_ESO_DELTA) + 2);
	} else {
		outl(((voice->Delta << 16) & 0xff000000) | (voice->ESO & 0x00ffffff),
		     TRID_REG(trident, CH_NX_DELTA_ESO));
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_vol_reg
  
   Description: This routine will write the new voice volume
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Vol - new voice volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_vol_reg(struct snd_trident * trident,
				      struct snd_trident_voice * voice,
				      unsigned int Vol)
{
	voice->Vol = Vol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
	case TRIDENT_DEVICE_ID_NX:
		outb(voice->Vol >> 2, TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC + 2));
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		/* dev_dbg(trident->card->dev, "voice->Vol = 0x%x\n", voice->Vol); */
		outw((voice->CTRL << 12) | voice->Vol,
		     TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC));
		break;
	}
}

/*---------------------------------------------------------------------------
   snd_trident_write_pan_reg
  
   Description: This routine will write the new voice pan
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                Pan - new pan value
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_pan_reg(struct snd_trident * trident,
				      struct snd_trident_voice * voice,
				      unsigned int Pan)
{
	voice->Pan = Pan;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outb(((voice->GVSel & 0x01) << 7) | (voice->Pan & 0x7f),
	     TRID_REG(trident, CH_GVSEL_PAN_VOL_CTRL_EC + 3));
}

/*---------------------------------------------------------------------------
   snd_trident_write_rvol_reg
  
   Description: This routine will write the new reverb volume
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                RVol - new reverb volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_rvol_reg(struct snd_trident * trident,
				       struct snd_trident_voice * voice,
				       unsigned int RVol)
{
	voice->RVol = RVol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outw(((voice->FMC & 0x0003) << 14) | ((voice->RVol & 0x007f) << 7) |
	     (voice->CVol & 0x007f),
	     TRID_REG(trident, trident->device == TRIDENT_DEVICE_ID_NX ?
		      CH_NX_ALPHA_FMS_FMC_RVOL_CVOL : CH_DX_FMC_RVOL_CVOL));
}

/*---------------------------------------------------------------------------
   snd_trident_write_cvol_reg
  
   Description: This routine will write the new chorus volume
                register to hardware.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                voice - synthesizer voice structure
                CVol - new chorus volume
  
  ---------------------------------------------------------------------------*/

static void snd_trident_write_cvol_reg(struct snd_trident * trident,
				       struct snd_trident_voice * voice,
				       unsigned int CVol)
{
	voice->CVol = CVol;
	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));
	outw(((voice->FMC & 0x0003) << 14) | ((voice->RVol & 0x007f) << 7) |
	     (voice->CVol & 0x007f),
	     TRID_REG(trident, trident->device == TRIDENT_DEVICE_ID_NX ?
		      CH_NX_ALPHA_FMS_FMC_RVOL_CVOL : CH_DX_FMC_RVOL_CVOL));
}

/*---------------------------------------------------------------------------
   snd_trident_convert_rate

   Description: This routine converts rate in HZ to hardware delta value.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                rate - Real or Virtual channel number.
  
   Returns:     Delta value.
  
  ---------------------------------------------------------------------------*/
static unsigned int snd_trident_convert_rate(unsigned int rate)
{
	unsigned int delta;

	// We special case 44100 and 8000 since rounding with the equation
	// does not give us an accurate enough value. For 11025 and 22050
	// the equation gives us the best answer. All other frequencies will
	// also use the equation. JDW
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + 24000) / 48000) & 0x0000ffff;
	return delta;
}

/*---------------------------------------------------------------------------
   snd_trident_convert_adc_rate

   Description: This routine converts rate in HZ to hardware delta value.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                rate - Real or Virtual channel number.
  
   Returns:     Delta value.
  
  ---------------------------------------------------------------------------*/
static unsigned int snd_trident_convert_adc_rate(unsigned int rate)
{
	unsigned int delta;

	// We special case 44100 and 8000 since rounding with the equation
	// does not give us an accurate enough value. For 11025 and 22050
	// the equation gives us the best answer. All other frequencies will
	// also use the equation. JDW
	if (rate == 44100)
		delta = 0x116a;
	else if (rate == 8000)
		delta = 0x6000;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = ((48000 << 12) / rate) & 0x0000ffff;
	return delta;
}

/*---------------------------------------------------------------------------
   snd_trident_spurious_threshold

   Description: This routine converts rate in HZ to spurious threshold.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                rate - Real or Virtual channel number.
  
   Returns:     Delta value.
  
  ---------------------------------------------------------------------------*/
static unsigned int snd_trident_spurious_threshold(unsigned int rate,
						   unsigned int period_size)
{
	unsigned int res = (rate * period_size) / 48000;
	if (res < 64)
		res = res / 2;
	else
		res -= 32;
	return res;
}

/*---------------------------------------------------------------------------
   snd_trident_control_mode

   Description: This routine returns a control mode for a PCM channel.
  
   Parameters:  trident - pointer to target device class for 4DWave.
                substream  - PCM substream
  
   Returns:     Control value.
  
  ---------------------------------------------------------------------------*/
static unsigned int snd_trident_control_mode(struct snd_pcm_substream *substream)
{
	unsigned int CTRL;
	struct snd_pcm_runtime *runtime = substream->runtime;

	/* set ctrl mode
	   CTRL default: 8-bit (unsigned) mono, loop mode enabled
	 */
	CTRL = 0x00000001;
	if (snd_pcm_format_width(runtime->format) == 16)
		CTRL |= 0x00000008;	// 16-bit data
	if (snd_pcm_format_signed(runtime->format))
		CTRL |= 0x00000002;	// signed data
	if (runtime->channels > 1)
		CTRL |= 0x00000004;	// stereo data
	return CTRL;
}

/*
 *  PCM part
 */

/*---------------------------------------------------------------------------
   snd_trident_ioctl
  
   Description: Device I/O control handler for playback/capture parameters.
  
   Parameters:   substream  - PCM substream class
                cmd     - what ioctl message to process
                arg     - additional message infoarg     
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_ioctl(struct snd_pcm_substream *substream,
			     unsigned int cmd,
			     void *arg)
{
	/* FIXME: it seems that with small periods the behaviour of
	          trident hardware is unpredictable and interrupt generator
	          is broken */
	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

/*---------------------------------------------------------------------------
   snd_trident_allocate_pcm_mem
  
   Description: Allocate PCM ring buffer for given substream
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_pcm_mem(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (trident->tlb.entries) {
		if (err > 0) { /* change */
			if (voice->memblk)
				snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = snd_trident_alloc_pages(trident, substream);
			if (voice->memblk == NULL)
				return -ENOMEM;
		}
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_allocate_evoice
  
   Description: Allocate extra voice as interrupt generator
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_allocate_evoice(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice->extra;

	/* voice management */

	if (params_buffer_size(hw_params) / 2 != params_period_size(hw_params)) {
		if (evoice == NULL) {
			evoice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
			if (evoice == NULL)
				return -ENOMEM;
			voice->extra = evoice;
			evoice->substream = substream;
		}
	} else {
		if (evoice != NULL) {
			snd_trident_free_voice(trident, evoice);
			voice->extra = evoice = NULL;
		}
	}

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_hw_params
  
   Description: Set the hardware parameters for the playback device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	int err;

	err = snd_trident_allocate_pcm_mem(substream, hw_params);
	if (err >= 0)
		err = snd_trident_allocate_evoice(substream, hw_params);
	return err;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_hw_free
  
   Description: Release the hardware resources for the playback device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice ? voice->extra : NULL;

	if (trident->tlb.entries) {
		if (voice && voice->memblk) {
			snd_trident_free_pages(trident, voice->memblk);
			voice->memblk = NULL;
		}
	}
	snd_pcm_lib_free_pages(substream);
	if (evoice != NULL) {
		snd_trident_free_voice(trident, evoice);
		voice->extra = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_prepare
  
   Description: Prepare playback device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice->extra;
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[substream->number];

	spin_lock_irq(&trident->reg_lock);	

	/* set delta (rate) value */
	voice->Delta = snd_trident_convert_rate(runtime->rate);
	voice->spurious_threshold = snd_trident_spurious_threshold(runtime->rate, runtime->period_size);

	/* set Loop Begin Address */
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;
	else
		voice->LBA = runtime->dma_addr;
 
	voice->CSO = 0;
	voice->ESO = runtime->buffer_size - 1;	/* in samples */
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->GVSel = 1;
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Vol = mix->vol;
	voice->RVol = mix->rvol;
	voice->CVol = mix->cvol;
	voice->Pan = mix->pan;
	voice->Attribute = 0;
#if 0
	voice->Attribute = (1<<(30-16))|(2<<(26-16))|
			   (0<<(24-16))|(0x1f<<(19-16));
#else
	voice->Attribute = 0;
#endif

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = voice->Delta;
		evoice->spurious_threshold = voice->spurious_threshold;
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) + 4 - 1; /* in samples */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = trident->device == TRIDENT_DEVICE_ID_SI7018 ? 0 : 1;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
#if 0
		evoice->Attribute = (1<<(30-16))|(2<<(26-16))|
				    (0<<(24-16))|(0x1f<<(19-16));
#else
		evoice->Attribute = 0;
#endif
		snd_trident_write_voice_regs(trident, evoice);
		evoice->isync2 = 1;
		evoice->isync_mark = runtime->period_size;
		evoice->ESO = (runtime->period_size * 2) - 1;
	}

	spin_unlock_irq(&trident->reg_lock);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_hw_params
  
   Description: Set the hardware parameters for the capture device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_capture_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	return snd_trident_allocate_pcm_mem(substream, hw_params);
}

/*---------------------------------------------------------------------------
   snd_trident_capture_prepare
  
   Description: Prepare capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	unsigned int val, ESO_bytes;

	spin_lock_irq(&trident->reg_lock);

	// Initialize the channel and set channel Mode
	outb(0, TRID_REG(trident, LEGACY_DMAR15));

	// Set DMA channel operation mode register
	outb(0x54, TRID_REG(trident, LEGACY_DMAR11));

	// Set channel buffer Address, DMAR0 expects contiguous PCI memory area	
	voice->LBA = runtime->dma_addr;
	outl(voice->LBA, TRID_REG(trident, LEGACY_DMAR0));
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;

	// set ESO
	ESO_bytes = snd_pcm_lib_buffer_bytes(substream) - 1;
	outb((ESO_bytes & 0x00ff0000) >> 16, TRID_REG(trident, LEGACY_DMAR6));
	outw((ESO_bytes & 0x0000ffff), TRID_REG(trident, LEGACY_DMAR4));
	ESO_bytes++;

	// Set channel sample rate, 4.12 format
	val = (((unsigned int) 48000L << 12) + (runtime->rate/2)) / runtime->rate;
	outw(val, TRID_REG(trident, T4D_SBDELTA_DELTA_R));

	// Set channel interrupt blk length
	if (snd_pcm_format_width(runtime->format) == 16) {
		val = (unsigned short) ((ESO_bytes >> 1) - 1);
	} else {
		val = (unsigned short) (ESO_bytes - 1);
	}

	outl((val << 16) | val, TRID_REG(trident, T4D_SBBL_SBCL));

	// Right now, set format and start to run captureing, 
	// continuous run loop enable.
	trident->bDMAStart = 0x19;	// 0001 1001b

	if (snd_pcm_format_width(runtime->format) == 16)
		trident->bDMAStart |= 0x80;
	if (snd_pcm_format_signed(runtime->format))
		trident->bDMAStart |= 0x20;
	if (runtime->channels > 1)
		trident->bDMAStart |= 0x40;

	// Prepare capture intr channel

	voice->Delta = snd_trident_convert_rate(runtime->rate);
	voice->spurious_threshold = snd_trident_spurious_threshold(runtime->rate, runtime->period_size);
	voice->isync = 1;
	voice->isync_mark = runtime->period_size;
	voice->isync_max = runtime->buffer_size;

	// Set voice parameters
	voice->CSO = 0;
	voice->ESO = voice->isync_ESO = (runtime->period_size * 2) + 6 - 1;
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->RVol = 0x7f;
	voice->CVol = 0x7f;
	voice->GVSel = 1;
	voice->Pan = 0x7f;		/* mute */
	voice->Vol = 0x3ff;		/* mute */
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Attribute = 0;

	snd_trident_write_voice_regs(trident, voice);

	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_hw_params
  
   Description: Set the hardware parameters for the capture device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_hw_params(struct snd_pcm_substream *substream,
						struct snd_pcm_hw_params *hw_params)
{
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;

	return snd_trident_allocate_evoice(substream, hw_params);
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_hw_free
  
   Description: Release the hardware resources for the capture device.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice ? voice->extra : NULL;

	snd_pcm_lib_free_pages(substream);
	if (evoice != NULL) {
		snd_trident_free_voice(trident, evoice);
		voice->extra = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_si7018_capture_prepare
  
   Description: Prepare capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_si7018_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice->extra;

	spin_lock_irq(&trident->reg_lock);

	voice->LBA = runtime->dma_addr;
	voice->Delta = snd_trident_convert_adc_rate(runtime->rate);
	voice->spurious_threshold = snd_trident_spurious_threshold(runtime->rate, runtime->period_size);

	// Set voice parameters
	voice->CSO = 0;
	voice->ESO = runtime->buffer_size - 1;		/* in samples */
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 0;
	voice->RVol = 0;
	voice->CVol = 0;
	voice->GVSel = 1;
	voice->Pan = T4D_DEFAULT_PCM_PAN;
	voice->Vol = 0;
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;

	voice->Attribute = (2 << (30-16)) |
			   (2 << (26-16)) |
			   (2 << (24-16)) |
			   (1 << (23-16));

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = snd_trident_convert_rate(runtime->rate);
		evoice->spurious_threshold = voice->spurious_threshold;
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) + 20 - 1; /* in samples, 20 means correction */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = 0;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
		evoice->Attribute = 0;
		snd_trident_write_voice_regs(trident, evoice);
		evoice->isync2 = 1;
		evoice->isync_mark = runtime->period_size;
		evoice->ESO = (runtime->period_size * 2) - 1;
	}
	
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_prepare
  
   Description: Prepare foldback capture device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice->extra;

	spin_lock_irq(&trident->reg_lock);

	/* Set channel buffer Address */
	if (voice->memblk)
		voice->LBA = voice->memblk->offset;
	else
		voice->LBA = runtime->dma_addr;

	/* set target ESO for channel */
	voice->ESO = runtime->buffer_size - 1;	/* in samples */

	/* set sample rate */
	voice->Delta = 0x1000;
	voice->spurious_threshold = snd_trident_spurious_threshold(48000, runtime->period_size);

	voice->CSO = 0;
	voice->CTRL = snd_trident_control_mode(substream);
	voice->FMC = 3;
	voice->RVol = 0x7f;
	voice->CVol = 0x7f;
	voice->GVSel = 1;
	voice->Pan = 0x7f;	/* mute */
	voice->Vol = 0x3ff;	/* mute */
	voice->EC = 0;
	voice->Alpha = 0;
	voice->FMS = 0;
	voice->Attribute = 0;

	/* set up capture channel */
	outb(((voice->number & 0x3f) | 0x80), TRID_REG(trident, T4D_RCI + voice->foldback_chan));

	snd_trident_write_voice_regs(trident, voice);

	if (evoice != NULL) {
		evoice->Delta = voice->Delta;
		evoice->spurious_threshold = voice->spurious_threshold;
		evoice->LBA = voice->LBA;
		evoice->CSO = 0;
		evoice->ESO = (runtime->period_size * 2) + 4 - 1; /* in samples */
		evoice->CTRL = voice->CTRL;
		evoice->FMC = 3;
		evoice->GVSel = trident->device == TRIDENT_DEVICE_ID_SI7018 ? 0 : 1;
		evoice->EC = 0;
		evoice->Alpha = 0;
		evoice->FMS = 0;
		evoice->Vol = 0x3ff;			/* mute */
		evoice->RVol = evoice->CVol = 0x7f;	/* mute */
		evoice->Pan = 0x7f;			/* mute */
		evoice->Attribute = 0;
		snd_trident_write_voice_regs(trident, evoice);
		evoice->isync2 = 1;
		evoice->isync_mark = runtime->period_size;
		evoice->ESO = (runtime->period_size * 2) - 1;
	}

	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_hw_params
  
   Description: Set the hardware parameters for the spdif device.
  
   Parameters:  substream  - PCM substream class
		hw_params  - hardware parameters
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *hw_params)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	unsigned int old_bits = 0, change = 0;
	int err;

	err = snd_trident_allocate_pcm_mem(substream, hw_params);
	if (err < 0)
		return err;

	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		err = snd_trident_allocate_evoice(substream, hw_params);
		if (err < 0)
			return err;
	}

	/* prepare SPDIF channel */
	spin_lock_irq(&trident->reg_lock);
	old_bits = trident->spdif_pcm_bits;
	if (old_bits & IEC958_AES0_PROFESSIONAL)
		trident->spdif_pcm_bits &= ~IEC958_AES0_PRO_FS;
	else
		trident->spdif_pcm_bits &= ~(IEC958_AES3_CON_FS << 24);
	if (params_rate(hw_params) >= 48000) {
		trident->spdif_pcm_ctrl = 0x3c;	// 48000 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_48000 :
				(IEC958_AES3_CON_FS_48000 << 24);
	}
	else if (params_rate(hw_params) >= 44100) {
		trident->spdif_pcm_ctrl = 0x3e;	// 44100 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_44100 :
				(IEC958_AES3_CON_FS_44100 << 24);
	}
	else {
		trident->spdif_pcm_ctrl = 0x3d;	// 32000 Hz
		trident->spdif_pcm_bits |=
			trident->spdif_bits & IEC958_AES0_PROFESSIONAL ?
				IEC958_AES0_PRO_FS_32000 :
				(IEC958_AES3_CON_FS_32000 << 24);
	}
	change = old_bits != trident->spdif_pcm_bits;
	spin_unlock_irq(&trident->reg_lock);

	if (change)
		snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE, &trident->spdif_pcm_ctl->id);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_prepare
  
   Description: Prepare SPDIF device for playback.
  
   Parameters:  substream  - PCM substream class
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_prepare(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident_voice *evoice = voice->extra;
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[substream->number];
	unsigned int RESO, LBAO;
	unsigned int temp;

	spin_lock_irq(&trident->reg_lock);

	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {

		/* set delta (rate) value */
		voice->Delta = snd_trident_convert_rate(runtime->rate);
		voice->spurious_threshold = snd_trident_spurious_threshold(runtime->rate, runtime->period_size);

		/* set Loop Back Address */
		LBAO = runtime->dma_addr;
		if (voice->memblk)
			voice->LBA = voice->memblk->offset;
		else
			voice->LBA = LBAO;

		voice->isync = 1;
		voice->isync3 = 1;
		voice->isync_mark = runtime->period_size;
		voice->isync_max = runtime->buffer_size;

		/* set target ESO for channel */
		RESO = runtime->buffer_size - 1;
		voice->ESO = voice->isync_ESO = (runtime->period_size * 2) + 6 - 1;

		/* set ctrl mode */
		voice->CTRL = snd_trident_control_mode(substream);

		voice->FMC = 3;
		voice->RVol = 0x7f;
		voice->CVol = 0x7f;
		voice->GVSel = 1;
		voice->Pan = 0x7f;
		voice->Vol = 0x3ff;
		voice->EC = 0;
		voice->CSO = 0;
		voice->Alpha = 0;
		voice->FMS = 0;
		voice->Attribute = 0;

		/* prepare surrogate IRQ channel */
		snd_trident_write_voice_regs(trident, voice);

		outw((RESO & 0xffff), TRID_REG(trident, NX_SPESO));
		outb((RESO >> 16), TRID_REG(trident, NX_SPESO + 2));
		outl((LBAO & 0xfffffffc), TRID_REG(trident, NX_SPLBA));
		outw((voice->CSO & 0xffff), TRID_REG(trident, NX_SPCTRL_SPCSO));
		outb((voice->CSO >> 16), TRID_REG(trident, NX_SPCTRL_SPCSO + 2));

		/* set SPDIF setting */
		outb(trident->spdif_pcm_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
		outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));

	} else {	/* SiS */
	
		/* set delta (rate) value */
		voice->Delta = 0x800;
		voice->spurious_threshold = snd_trident_spurious_threshold(48000, runtime->period_size);

		/* set Loop Begin Address */
		if (voice->memblk)
			voice->LBA = voice->memblk->offset;
		else
			voice->LBA = runtime->dma_addr;

		voice->CSO = 0;
		voice->ESO = runtime->buffer_size - 1;	/* in samples */
		voice->CTRL = snd_trident_control_mode(substream);
		voice->FMC = 3;
		voice->GVSel = 1;
		voice->EC = 0;
		voice->Alpha = 0;
		voice->FMS = 0;
		voice->Vol = mix->vol;
		voice->RVol = mix->rvol;
		voice->CVol = mix->cvol;
		voice->Pan = mix->pan;
		voice->Attribute = (1<<(30-16))|(7<<(26-16))|
				   (0<<(24-16))|(0<<(19-16));

		snd_trident_write_voice_regs(trident, voice);

		if (evoice != NULL) {
			evoice->Delta = voice->Delta;
			evoice->spurious_threshold = voice->spurious_threshold;
			evoice->LBA = voice->LBA;
			evoice->CSO = 0;
			evoice->ESO = (runtime->period_size * 2) + 4 - 1; /* in samples */
			evoice->CTRL = voice->CTRL;
			evoice->FMC = 3;
			evoice->GVSel = trident->device == TRIDENT_DEVICE_ID_SI7018 ? 0 : 1;
			evoice->EC = 0;
			evoice->Alpha = 0;
			evoice->FMS = 0;
			evoice->Vol = 0x3ff;			/* mute */
			evoice->RVol = evoice->CVol = 0x7f;	/* mute */
			evoice->Pan = 0x7f;			/* mute */
			evoice->Attribute = 0;
			snd_trident_write_voice_regs(trident, evoice);
			evoice->isync2 = 1;
			evoice->isync_mark = runtime->period_size;
			evoice->ESO = (runtime->period_size * 2) - 1;
		}

		outl(trident->spdif_pcm_bits, TRID_REG(trident, SI_SPDIF_CS));
		temp = inl(TRID_REG(trident, T4D_LFO_GC_CIR));
		temp &= ~(1<<19);
		outl(temp, TRID_REG(trident, T4D_LFO_GC_CIR));
		temp = inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		temp |= SPDIF_EN;
		outl(temp, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
	}

	spin_unlock_irq(&trident->reg_lock);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_trigger
  
   Description: Start/stop devices
  
   Parameters:  substream  - PCM substream class
   		cmd	- trigger command (STOP, GO)
  
   Returns:     Error status
  
  ---------------------------------------------------------------------------*/

static int snd_trident_trigger(struct snd_pcm_substream *substream,
			       int cmd)
				    
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *s;
	unsigned int what, whati, capture_flag, spdif_flag;
	struct snd_trident_voice *voice, *evoice;
	unsigned int val, go;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		go = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		go = 0;
		break;
	default:
		return -EINVAL;
	}
	what = whati = capture_flag = spdif_flag = 0;
	spin_lock(&trident->reg_lock);
	val = inl(TRID_REG(trident, T4D_STIMER)) & 0x00ffffff;
	snd_pcm_group_for_each_entry(s, substream) {
		if ((struct snd_trident *) snd_pcm_substream_chip(s) == trident) {
			voice = s->runtime->private_data;
			evoice = voice->extra;
			what |= 1 << (voice->number & 0x1f);
			if (evoice == NULL) {
				whati |= 1 << (voice->number & 0x1f);
			} else {
				what |= 1 << (evoice->number & 0x1f);
				whati |= 1 << (evoice->number & 0x1f);
				if (go)
					evoice->stimer = val;
			}
			if (go) {
				voice->running = 1;
				voice->stimer = val;
			} else {
				voice->running = 0;
			}
			snd_pcm_trigger_done(s, substream);
			if (voice->capture)
				capture_flag = 1;
			if (voice->spdif)
				spdif_flag = 1;
		}
	}
	if (spdif_flag) {
		if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
			outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));
			val = trident->spdif_pcm_ctrl;
			if (!go)
				val &= ~(0x28);
			outb(val, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
		} else {
			outl(trident->spdif_pcm_bits, TRID_REG(trident, SI_SPDIF_CS));
			val = inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)) | SPDIF_EN;
			outl(val, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		}
	}
	if (!go)
		outl(what, TRID_REG(trident, T4D_STOP_B));
	val = inl(TRID_REG(trident, T4D_AINTEN_B));
	if (go) {
		val |= whati;
	} else {
		val &= ~whati;
	}
	outl(val, TRID_REG(trident, T4D_AINTEN_B));
	if (go) {
		outl(what, TRID_REG(trident, T4D_START_B));

		if (capture_flag && trident->device != TRIDENT_DEVICE_ID_SI7018)
			outb(trident->bDMAStart, TRID_REG(trident, T4D_SBCTRL_SBE2R_SBDD));
	} else {
		if (capture_flag && trident->device != TRIDENT_DEVICE_ID_SI7018)
			outb(0x00, TRID_REG(trident, T4D_SBCTRL_SBE2R_SBDD));
	}
	spin_unlock(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_pointer
  
   Description: This routine return the playback position
                
   Parameters:	substream  - PCM substream class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	unsigned int cso;

	if (!voice->running)
		return 0;

	spin_lock(&trident->reg_lock);

	outb(voice->number, TRID_REG(trident, T4D_LFO_GC_CIR));

	if (trident->device != TRIDENT_DEVICE_ID_NX) {
		cso = inw(TRID_REG(trident, CH_DX_CSO_ALPHA_FMS + 2));
	} else {		// ID_4DWAVE_NX
		cso = (unsigned int) inl(TRID_REG(trident, CH_NX_DELTA_CSO)) & 0x00ffffff;
	}

	spin_unlock(&trident->reg_lock);

	if (cso >= runtime->buffer_size)
		cso = 0;

	return cso;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_pointer
  
   Description: This routine return the capture position
                
   Parameters:   pcm1    - PCM device class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	unsigned int result;

	if (!voice->running)
		return 0;

	result = inw(TRID_REG(trident, T4D_SBBL_SBCL));
	if (runtime->channels > 1)
		result >>= 1;
	if (result > 0)
		result = runtime->buffer_size - result;

	return result;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_pointer
  
   Description: This routine return the SPDIF playback position
                
   Parameters:	substream  - PCM substream class

   Returns:     position of buffer
  
  ---------------------------------------------------------------------------*/

static snd_pcm_uframes_t snd_trident_spdif_pointer(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;
	unsigned int result;

	if (!voice->running)
		return 0;

	result = inl(TRID_REG(trident, NX_SPCTRL_SPCSO)) & 0x00ffffff;

	return result;
}

/*
 *  Playback support device description
 */

static struct snd_pcm_hardware snd_trident_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_PAUSE /* | SNDRV_PCM_INFO_RESUME */),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(256*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(256*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  Capture support device description
 */

static struct snd_pcm_hardware snd_trident_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_PAUSE /* | SNDRV_PCM_INFO_RESUME */),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  Foldback capture support device description
 */

static struct snd_pcm_hardware snd_trident_foldback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_PAUSE /* | SNDRV_PCM_INFO_RESUME */),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  SPDIF playback support device description
 */

static struct snd_pcm_hardware snd_trident_spdif =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_PAUSE /* | SNDRV_PCM_INFO_RESUME */),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				 SNDRV_PCM_RATE_48000),
	.rate_min =		32000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_trident_spdif_7018 =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_PAUSE /* | SNDRV_PCM_INFO_RESUME */),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static void snd_trident_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	struct snd_trident_voice *voice = runtime->private_data;
	struct snd_trident *trident;

	if (voice) {
		trident = voice->trident;
		snd_trident_free_voice(trident, voice);
	}
}

static int snd_trident_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice;

	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL)
		return -EAGAIN;
	snd_trident_pcm_mixer_build(trident, voice, substream);
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_playback;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_playback_close
  
   Description: This routine will close the 4DWave playback device. For now 
                we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_trident_voice *voice = runtime->private_data;

	snd_trident_pcm_mixer_free(trident, voice, substream);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif_open
  
   Description: This routine will open the 4DWave SPDIF device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag
  
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_open(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_trident_voice *voice;
	struct snd_pcm_runtime *runtime = substream->runtime;
	
	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL)
		return -EAGAIN;
	voice->spdif = 1;
	voice->substream = substream;
	spin_lock_irq(&trident->reg_lock);
	trident->spdif_pcm_bits = trident->spdif_bits;
	spin_unlock_irq(&trident->reg_lock);

	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		runtime->hw = snd_trident_spdif;
	} else {
		runtime->hw = snd_trident_spdif_7018;
	}

	trident->spdif_pcm_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &trident->spdif_pcm_ctl->id);

	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}


/*---------------------------------------------------------------------------
   snd_trident_spdif_close
  
   Description: This routine will close the 4DWave SPDIF device.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_close(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	unsigned int temp;

	spin_lock_irq(&trident->reg_lock);
	// restore default SPDIF setting
	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
		outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
	} else {
		outl(trident->spdif_bits, TRID_REG(trident, SI_SPDIF_CS));
		temp = inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		if (trident->spdif_ctrl) {
			temp |= SPDIF_EN;
		} else {
			temp &= ~SPDIF_EN;
		}
		outl(temp, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
	}
	spin_unlock_irq(&trident->reg_lock);
	trident->spdif_pcm_ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(trident->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &trident->spdif_pcm_ctl->id);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_open
  
   Description: This routine will open the 4DWave capture device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag

  ---------------------------------------------------------------------------*/

static int snd_trident_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_trident_voice *voice;
	struct snd_pcm_runtime *runtime = substream->runtime;

	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL)
		return -EAGAIN;
	voice->capture = 1;
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_capture;
	snd_pcm_set_sync(substream);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_capture_close
  
   Description: This routine will close the 4DWave capture device. For now 
                we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_capture_close(struct snd_pcm_substream *substream)
{
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_open
  
   Description: This routine will open the 4DWave foldback capture device.

   Parameters:	substream  - PCM substream class

   Returns:     status  - success or failure flag

  ---------------------------------------------------------------------------*/

static int snd_trident_foldback_open(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_trident_voice *voice;
	struct snd_pcm_runtime *runtime = substream->runtime;

	voice = snd_trident_alloc_voice(trident, SNDRV_TRIDENT_VOICE_TYPE_PCM, 0, 0);
	if (voice == NULL)
		return -EAGAIN;
	voice->foldback_chan = substream->number;
	voice->substream = substream;
	runtime->private_data = voice;
	runtime->private_free = snd_trident_pcm_free_substream;
	runtime->hw = snd_trident_foldback;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE, 0, 64*1024);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_close
  
   Description: This routine will close the 4DWave foldback capture device. 
		For now we will simply free the dma transfer buffer.
                
   Parameters:	substream  - PCM substream class

  ---------------------------------------------------------------------------*/
static int snd_trident_foldback_close(struct snd_pcm_substream *substream)
{
	struct snd_trident *trident = snd_pcm_substream_chip(substream);
	struct snd_trident_voice *voice;
	struct snd_pcm_runtime *runtime = substream->runtime;
	voice = runtime->private_data;
	
	/* stop capture channel */
	spin_lock_irq(&trident->reg_lock);
	outb(0x00, TRID_REG(trident, T4D_RCI + voice->foldback_chan));
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

/*---------------------------------------------------------------------------
   PCM operations
  ---------------------------------------------------------------------------*/

static struct snd_pcm_ops snd_trident_playback_ops = {
	.open =		snd_trident_playback_open,
	.close =	snd_trident_playback_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_playback_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
};

static struct snd_pcm_ops snd_trident_nx_playback_ops = {
	.open =		snd_trident_playback_open,
	.close =	snd_trident_playback_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_playback_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

static struct snd_pcm_ops snd_trident_capture_ops = {
	.open =		snd_trident_capture_open,
	.close =	snd_trident_capture_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_capture_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_capture_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_capture_pointer,
};

static struct snd_pcm_ops snd_trident_si7018_capture_ops = {
	.open =		snd_trident_capture_open,
	.close =	snd_trident_capture_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_si7018_capture_hw_params,
	.hw_free =	snd_trident_si7018_capture_hw_free,
	.prepare =	snd_trident_si7018_capture_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
};

static struct snd_pcm_ops snd_trident_foldback_ops = {
	.open =		snd_trident_foldback_open,
	.close =	snd_trident_foldback_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_foldback_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
};

static struct snd_pcm_ops snd_trident_nx_foldback_ops = {
	.open =		snd_trident_foldback_open,
	.close =	snd_trident_foldback_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_foldback_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
	.page =		snd_pcm_sgbuf_ops_page,
};

static struct snd_pcm_ops snd_trident_spdif_ops = {
	.open =		snd_trident_spdif_open,
	.close =	snd_trident_spdif_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_spdif_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_spdif_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_spdif_pointer,
};

static struct snd_pcm_ops snd_trident_spdif_7018_ops = {
	.open =		snd_trident_spdif_open,
	.close =	snd_trident_spdif_close,
	.ioctl =	snd_trident_ioctl,
	.hw_params =	snd_trident_spdif_hw_params,
	.hw_free =	snd_trident_hw_free,
	.prepare =	snd_trident_spdif_prepare,
	.trigger =	snd_trident_trigger,
	.pointer =	snd_trident_playback_pointer,
};

/*---------------------------------------------------------------------------
   snd_trident_pcm
  
   Description: This routine registers the 4DWave device for PCM support.
                
   Parameters:  trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_pcm(struct snd_trident *trident, int device)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(trident->card, "trident_dx_nx", device, trident->ChanPCM, 1, &pcm)) < 0)
		return err;

	pcm->private_data = trident;

	if (trident->tlb.entries) {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_nx_playback_ops);
	} else {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_playback_ops);
	}
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			trident->device != TRIDENT_DEVICE_ID_SI7018 ?
			&snd_trident_capture_ops :
			&snd_trident_si7018_capture_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "Trident 4DWave");
	trident->pcm = pcm;

	if (trident->tlb.entries) {
		struct snd_pcm_substream *substream;
		for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
			snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV_SG,
						      snd_dma_pci_data(trident->pci),
						      64*1024, 128*1024);
		snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
					      SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(trident->pci),
					      64*1024, 128*1024);
	} else {
		snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(trident->pci), 64*1024, 128*1024);
	}

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_foldback_pcm
  
   Description: This routine registers the 4DWave device for foldback PCM support.
                
   Parameters:  trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_foldback_pcm(struct snd_trident *trident, int device)
{
	struct snd_pcm *foldback;
	int err;
	int num_chan = 3;
	struct snd_pcm_substream *substream;

	if (trident->device == TRIDENT_DEVICE_ID_NX)
		num_chan = 4;
	if ((err = snd_pcm_new(trident->card, "trident_dx_nx", device, 0, num_chan, &foldback)) < 0)
		return err;

	foldback->private_data = trident;
	if (trident->tlb.entries)
		snd_pcm_set_ops(foldback, SNDRV_PCM_STREAM_CAPTURE, &snd_trident_nx_foldback_ops);
	else
		snd_pcm_set_ops(foldback, SNDRV_PCM_STREAM_CAPTURE, &snd_trident_foldback_ops);
	foldback->info_flags = 0;
	strcpy(foldback->name, "Trident 4DWave");
	substream = foldback->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
	strcpy(substream->name, "Front Mixer");
	substream = substream->next;
	strcpy(substream->name, "Reverb Mixer");
	substream = substream->next;
	strcpy(substream->name, "Chorus Mixer");
	if (num_chan == 4) {
		substream = substream->next;
		strcpy(substream->name, "Second AC'97 ADC");
	}
	trident->foldback = foldback;

	if (trident->tlb.entries)
		snd_pcm_lib_preallocate_pages_for_all(foldback, SNDRV_DMA_TYPE_DEV_SG,
						      snd_dma_pci_data(trident->pci), 0, 128*1024);
	else
		snd_pcm_lib_preallocate_pages_for_all(foldback, SNDRV_DMA_TYPE_DEV,
						      snd_dma_pci_data(trident->pci), 64*1024, 128*1024);

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_spdif
  
   Description: This routine registers the 4DWave-NX device for SPDIF support.
                
   Parameters:  trident - pointer to target device class for 4DWave-NX.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

int snd_trident_spdif_pcm(struct snd_trident *trident, int device)
{
	struct snd_pcm *spdif;
	int err;

	if ((err = snd_pcm_new(trident->card, "trident_dx_nx IEC958", device, 1, 0, &spdif)) < 0)
		return err;

	spdif->private_data = trident;
	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		snd_pcm_set_ops(spdif, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_spdif_ops);
	} else {
		snd_pcm_set_ops(spdif, SNDRV_PCM_STREAM_PLAYBACK, &snd_trident_spdif_7018_ops);
	}
	spdif->info_flags = 0;
	strcpy(spdif->name, "Trident 4DWave IEC958");
	trident->spdif = spdif;

	snd_pcm_lib_preallocate_pages_for_all(spdif, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(trident->pci), 64*1024, 128*1024);

	return 0;
}

/*
 *  Mixer part
 */


/*---------------------------------------------------------------------------
    snd_trident_spdif_control

    Description: enable/disable S/PDIF out from ac97 mixer
  ---------------------------------------------------------------------------*/

#define snd_trident_spdif_control_info	snd_ctl_boolean_mono_info

static int snd_trident_spdif_control_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned char val;

	spin_lock_irq(&trident->reg_lock);
	val = trident->spdif_ctrl;
	ucontrol->value.integer.value[0] = val == kcontrol->private_value;
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

static int snd_trident_spdif_control_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int change;

	val = ucontrol->value.integer.value[0] ? (unsigned char) kcontrol->private_value : 0x00;
	spin_lock_irq(&trident->reg_lock);
	/* S/PDIF C Channel bits 0-31 : 48khz, SCMS disabled */
	change = trident->spdif_ctrl != val;
	trident->spdif_ctrl = val;
	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		if ((inb(TRID_REG(trident, NX_SPCTRL_SPCSO + 3)) & 0x10) == 0) {
			outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
			outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
		}
	} else {
		if (trident->spdif == NULL) {
			unsigned int temp;
			outl(trident->spdif_bits, TRID_REG(trident, SI_SPDIF_CS));
			temp = inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)) & ~SPDIF_EN;
			if (val)
				temp |= SPDIF_EN;
			outl(temp, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
		}
	}
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_spdif_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH),
	.info =		snd_trident_spdif_control_info,
	.get =		snd_trident_spdif_control_get,
	.put =		snd_trident_spdif_control_put,
	.private_value = 0x28,
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_default

    Description: put/get the S/PDIF default settings
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_default_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_default_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&trident->reg_lock);
	ucontrol->value.iec958.status[0] = (trident->spdif_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (trident->spdif_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (trident->spdif_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (trident->spdif_bits >> 24) & 0xff;
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

static int snd_trident_spdif_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irq(&trident->reg_lock);
	change = trident->spdif_bits != val;
	trident->spdif_bits = val;
	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		if ((inb(TRID_REG(trident, NX_SPCTRL_SPCSO + 3)) & 0x10) == 0)
			outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
	} else {
		if (trident->spdif == NULL)
			outl(trident->spdif_bits, TRID_REG(trident, SI_SPDIF_CS));
	}
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_spdif_default =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_trident_spdif_default_info,
	.get =		snd_trident_spdif_default_get,
	.put =		snd_trident_spdif_default_put
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_mask

    Description: put/get the S/PDIF mask
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_mask_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static struct snd_kcontrol_new snd_trident_spdif_mask =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.info =		snd_trident_spdif_mask_info,
	.get =		snd_trident_spdif_mask_get,
};

/*---------------------------------------------------------------------------
    snd_trident_spdif_stream

    Description: put/get the S/PDIF stream settings
  ---------------------------------------------------------------------------*/

static int snd_trident_spdif_stream_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_trident_spdif_stream_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&trident->reg_lock);
	ucontrol->value.iec958.status[0] = (trident->spdif_pcm_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (trident->spdif_pcm_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (trident->spdif_pcm_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (trident->spdif_pcm_bits >> 24) & 0xff;
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

static int snd_trident_spdif_stream_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	spin_lock_irq(&trident->reg_lock);
	change = trident->spdif_pcm_bits != val;
	trident->spdif_pcm_bits = val;
	if (trident->spdif != NULL) {
		if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
			outl(trident->spdif_pcm_bits, TRID_REG(trident, NX_SPCSTATUS));
		} else {
			outl(trident->spdif_bits, TRID_REG(trident, SI_SPDIF_CS));
		}
	}
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_spdif_stream =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_trident_spdif_stream_info,
	.get =		snd_trident_spdif_stream_get,
	.put =		snd_trident_spdif_stream_put
};

/*---------------------------------------------------------------------------
    snd_trident_ac97_control

    Description: enable/disable rear path for ac97
  ---------------------------------------------------------------------------*/

#define snd_trident_ac97_control_info	snd_ctl_boolean_mono_info

static int snd_trident_ac97_control_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned char val;

	spin_lock_irq(&trident->reg_lock);
	val = trident->ac97_ctrl = inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	ucontrol->value.integer.value[0] = (val & (1 << kcontrol->private_value)) ? 1 : 0;
	spin_unlock_irq(&trident->reg_lock);
	return 0;
}

static int snd_trident_ac97_control_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int change = 0;

	spin_lock_irq(&trident->reg_lock);
	val = trident->ac97_ctrl = inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	val &= ~(1 << kcontrol->private_value);
	if (ucontrol->value.integer.value[0])
		val |= 1 << kcontrol->private_value;
	change = val != trident->ac97_ctrl;
	trident->ac97_ctrl = val;
	outl(trident->ac97_ctrl = val, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_ac97_rear_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Rear Path",
	.info =		snd_trident_ac97_control_info,
	.get =		snd_trident_ac97_control_get,
	.put =		snd_trident_ac97_control_put,
	.private_value = 4,
};

/*---------------------------------------------------------------------------
    snd_trident_vol_control

    Description: wave & music volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_vol_control_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	return 0;
}

static int snd_trident_vol_control_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	val = trident->musicvol_wavevol;
	ucontrol->value.integer.value[0] = 255 - ((val >> kcontrol->private_value) & 0xff);
	ucontrol->value.integer.value[1] = 255 - ((val >> (kcontrol->private_value + 8)) & 0xff);
	return 0;
}

static const DECLARE_TLV_DB_SCALE(db_scale_gvol, -6375, 25, 0);

static int snd_trident_vol_control_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	spin_lock_irq(&trident->reg_lock);
	val = trident->musicvol_wavevol;
	val &= ~(0xffff << kcontrol->private_value);
	val |= ((255 - (ucontrol->value.integer.value[0] & 0xff)) |
	        ((255 - (ucontrol->value.integer.value[1] & 0xff)) << 8)) << kcontrol->private_value;
	change = val != trident->musicvol_wavevol;
	outl(trident->musicvol_wavevol = val, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_vol_music_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Music Playback Volume",
	.info =		snd_trident_vol_control_info,
	.get =		snd_trident_vol_control_get,
	.put =		snd_trident_vol_control_put,
	.private_value = 16,
	.tlv = { .p = db_scale_gvol },
};

static struct snd_kcontrol_new snd_trident_vol_wave_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Wave Playback Volume",
	.info =		snd_trident_vol_control_info,
	.get =		snd_trident_vol_control_get,
	.put =		snd_trident_vol_control_put,
	.private_value = 0,
	.tlv = { .p = db_scale_gvol },
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_vol_control

    Description: PCM front volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_vol_control_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		uinfo->value.integer.max = 1023;
	return 0;
}

static int snd_trident_pcm_vol_control_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];

	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		ucontrol->value.integer.value[0] = 1023 - mix->vol;
	} else {
		ucontrol->value.integer.value[0] = 255 - (mix->vol>>2);
	}
	return 0;
}

static int snd_trident_pcm_vol_control_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];
	unsigned int val;
	int change = 0;

	if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		val = 1023 - (ucontrol->value.integer.value[0] & 1023);
	} else {
		val = (255 - (ucontrol->value.integer.value[0] & 255)) << 2;
	}
	spin_lock_irq(&trident->reg_lock);
	change = val != mix->vol;
	mix->vol = val;
	if (mix->voice != NULL)
		snd_trident_write_vol_reg(trident, mix->voice, val);
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_pcm_vol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "PCM Front Playback Volume",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.count =	32,
	.info =		snd_trident_pcm_vol_control_info,
	.get =		snd_trident_pcm_vol_control_get,
	.put =		snd_trident_pcm_vol_control_put,
	/* FIXME: no tlv yet */
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_pan_control

    Description: PCM front pan control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_pan_control_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_pan_control_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];

	ucontrol->value.integer.value[0] = mix->pan;
	if (ucontrol->value.integer.value[0] & 0x40) {
		ucontrol->value.integer.value[0] = (0x3f - (ucontrol->value.integer.value[0] & 0x3f));
	} else {
		ucontrol->value.integer.value[0] |= 0x40;
	}
	return 0;
}

static int snd_trident_pcm_pan_control_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];
	unsigned char val;
	int change = 0;

	if (ucontrol->value.integer.value[0] & 0x40)
		val = ucontrol->value.integer.value[0] & 0x3f;
	else
		val = (0x3f - (ucontrol->value.integer.value[0] & 0x3f)) | 0x40;
	spin_lock_irq(&trident->reg_lock);
	change = val != mix->pan;
	mix->pan = val;
	if (mix->voice != NULL)
		snd_trident_write_pan_reg(trident, mix->voice, val);
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_pcm_pan_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "PCM Pan Playback Control",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.count =	32,
	.info =		snd_trident_pcm_pan_control_info,
	.get =		snd_trident_pcm_pan_control_get,
	.put =		snd_trident_pcm_pan_control_put,
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_rvol_control

    Description: PCM reverb volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_rvol_control_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_rvol_control_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];

	ucontrol->value.integer.value[0] = 127 - mix->rvol;
	return 0;
}

static int snd_trident_pcm_rvol_control_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];
	unsigned short val;
	int change = 0;

	val = 0x7f - (ucontrol->value.integer.value[0] & 0x7f);
	spin_lock_irq(&trident->reg_lock);
	change = val != mix->rvol;
	mix->rvol = val;
	if (mix->voice != NULL)
		snd_trident_write_rvol_reg(trident, mix->voice, val);
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_crvol, -3175, 25, 1);

static struct snd_kcontrol_new snd_trident_pcm_rvol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "PCM Reverb Playback Volume",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.count = 	32,
	.info =		snd_trident_pcm_rvol_control_info,
	.get =		snd_trident_pcm_rvol_control_get,
	.put =		snd_trident_pcm_rvol_control_put,
	.tlv = { .p = db_scale_crvol },
};

/*---------------------------------------------------------------------------
    snd_trident_pcm_cvol_control

    Description: PCM chorus volume control
  ---------------------------------------------------------------------------*/

static int snd_trident_pcm_cvol_control_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_trident_pcm_cvol_control_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];

	ucontrol->value.integer.value[0] = 127 - mix->cvol;
	return 0;
}

static int snd_trident_pcm_cvol_control_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_trident *trident = snd_kcontrol_chip(kcontrol);
	struct snd_trident_pcm_mixer *mix = &trident->pcm_mixer[snd_ctl_get_ioffnum(kcontrol, &ucontrol->id)];
	unsigned short val;
	int change = 0;

	val = 0x7f - (ucontrol->value.integer.value[0] & 0x7f);
	spin_lock_irq(&trident->reg_lock);
	change = val != mix->cvol;
	mix->cvol = val;
	if (mix->voice != NULL)
		snd_trident_write_cvol_reg(trident, mix->voice, val);
	spin_unlock_irq(&trident->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_trident_pcm_cvol_control =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "PCM Chorus Playback Volume",
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.count =	32,
	.info =		snd_trident_pcm_cvol_control_info,
	.get =		snd_trident_pcm_cvol_control_get,
	.put =		snd_trident_pcm_cvol_control_put,
	.tlv = { .p = db_scale_crvol },
};

static void snd_trident_notify_pcm_change1(struct snd_card *card,
					   struct snd_kcontrol *kctl,
					   int num, int activate)
{
	struct snd_ctl_elem_id id;

	if (! kctl)
		return;
	if (activate)
		kctl->vd[num].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	else
		kctl->vd[num].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO,
		       snd_ctl_build_ioff(&id, kctl, num));
}

static void snd_trident_notify_pcm_change(struct snd_trident *trident,
					  struct snd_trident_pcm_mixer *tmix,
					  int num, int activate)
{
	snd_trident_notify_pcm_change1(trident->card, trident->ctl_vol, num, activate);
	snd_trident_notify_pcm_change1(trident->card, trident->ctl_pan, num, activate);
	snd_trident_notify_pcm_change1(trident->card, trident->ctl_rvol, num, activate);
	snd_trident_notify_pcm_change1(trident->card, trident->ctl_cvol, num, activate);
}

static int snd_trident_pcm_mixer_build(struct snd_trident *trident,
				       struct snd_trident_voice *voice,
				       struct snd_pcm_substream *substream)
{
	struct snd_trident_pcm_mixer *tmix;

	if (snd_BUG_ON(!trident || !voice || !substream))
		return -EINVAL;
	tmix = &trident->pcm_mixer[substream->number];
	tmix->voice = voice;
	tmix->vol = T4D_DEFAULT_PCM_VOL;
	tmix->pan = T4D_DEFAULT_PCM_PAN;
	tmix->rvol = T4D_DEFAULT_PCM_RVOL;
	tmix->cvol = T4D_DEFAULT_PCM_CVOL;
	snd_trident_notify_pcm_change(trident, tmix, substream->number, 1);
	return 0;
}

static int snd_trident_pcm_mixer_free(struct snd_trident *trident, struct snd_trident_voice *voice, struct snd_pcm_substream *substream)
{
	struct snd_trident_pcm_mixer *tmix;

	if (snd_BUG_ON(!trident || !substream))
		return -EINVAL;
	tmix = &trident->pcm_mixer[substream->number];
	tmix->voice = NULL;
	snd_trident_notify_pcm_change(trident, tmix, substream->number, 0);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_mixer
  
   Description: This routine registers the 4DWave device for mixer support.
                
   Parameters:  trident - pointer to target device class for 4DWave.

   Returns:     None
  
  ---------------------------------------------------------------------------*/

static int snd_trident_mixer(struct snd_trident *trident, int pcm_spdif_device)
{
	struct snd_ac97_template _ac97;
	struct snd_card *card = trident->card;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_value *uctl;
	int idx, err, retries = 2;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_trident_codec_write,
		.read = snd_trident_codec_read,
	};

	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return -ENOMEM;

	if ((err = snd_ac97_bus(trident->card, 0, &ops, NULL, &trident->ac97_bus)) < 0)
		goto __out;

	memset(&_ac97, 0, sizeof(_ac97));
	_ac97.private_data = trident;
	trident->ac97_detect = 1;

      __again:
	if ((err = snd_ac97_mixer(trident->ac97_bus, &_ac97, &trident->ac97)) < 0) {
		if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
			if ((err = snd_trident_sis_reset(trident)) < 0)
				goto __out;
			if (retries-- > 0)
				goto __again;
			err = -EIO;
		}
		goto __out;
	}
	
	/* secondary codec? */
	if (trident->device == TRIDENT_DEVICE_ID_SI7018 &&
	    (inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)) & SI_AC97_PRIMARY_READY) != 0) {
		_ac97.num = 1;
		err = snd_ac97_mixer(trident->ac97_bus, &_ac97, &trident->ac97_sec);
		if (err < 0)
			dev_err(trident->card->dev,
				"SI7018: the secondary codec - invalid access\n");
#if 0	// only for my testing purpose --jk
		{
			struct snd_ac97 *mc97;
			err = snd_ac97_modem(trident->card, &_ac97, &mc97);
			if (err < 0)
				dev_err(trident->card->dev,
					"snd_ac97_modem returned error %i\n", err);
		}
#endif
	}
	
	trident->ac97_detect = 0;

	if (trident->device != TRIDENT_DEVICE_ID_SI7018) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_vol_wave_control, trident))) < 0)
			goto __out;
		kctl->put(kctl, uctl);
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_vol_music_control, trident))) < 0)
			goto __out;
		kctl->put(kctl, uctl);
		outl(trident->musicvol_wavevol = 0x00000000, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	} else {
		outl(trident->musicvol_wavevol = 0xffff0000, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));
	}

	for (idx = 0; idx < 32; idx++) {
		struct snd_trident_pcm_mixer *tmix;
		
		tmix = &trident->pcm_mixer[idx];
		tmix->voice = NULL;
	}
	if ((trident->ctl_vol = snd_ctl_new1(&snd_trident_pcm_vol_control, trident)) == NULL)
		goto __nomem;
	if ((err = snd_ctl_add(card, trident->ctl_vol)))
		goto __out;
		
	if ((trident->ctl_pan = snd_ctl_new1(&snd_trident_pcm_pan_control, trident)) == NULL)
		goto __nomem;
	if ((err = snd_ctl_add(card, trident->ctl_pan)))
		goto __out;

	if ((trident->ctl_rvol = snd_ctl_new1(&snd_trident_pcm_rvol_control, trident)) == NULL)
		goto __nomem;
	if ((err = snd_ctl_add(card, trident->ctl_rvol)))
		goto __out;

	if ((trident->ctl_cvol = snd_ctl_new1(&snd_trident_pcm_cvol_control, trident)) == NULL)
		goto __nomem;
	if ((err = snd_ctl_add(card, trident->ctl_cvol)))
		goto __out;

	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_trident_ac97_rear_control, trident))) < 0)
			goto __out;
		kctl->put(kctl, uctl);
	}
	if (trident->device == TRIDENT_DEVICE_ID_NX || trident->device == TRIDENT_DEVICE_ID_SI7018) {

		kctl = snd_ctl_new1(&snd_trident_spdif_control, trident);
		if (kctl == NULL) {
			err = -ENOMEM;
			goto __out;
		}
		if (trident->ac97->ext_id & AC97_EI_SPDIF)
			kctl->id.index++;
		if (trident->ac97_sec && (trident->ac97_sec->ext_id & AC97_EI_SPDIF))
			kctl->id.index++;
		idx = kctl->id.index;
		if ((err = snd_ctl_add(card, kctl)) < 0)
			goto __out;
		kctl->put(kctl, uctl);

		kctl = snd_ctl_new1(&snd_trident_spdif_default, trident);
		if (kctl == NULL) {
			err = -ENOMEM;
			goto __out;
		}
		kctl->id.index = idx;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl)) < 0)
			goto __out;

		kctl = snd_ctl_new1(&snd_trident_spdif_mask, trident);
		if (kctl == NULL) {
			err = -ENOMEM;
			goto __out;
		}
		kctl->id.index = idx;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl)) < 0)
			goto __out;

		kctl = snd_ctl_new1(&snd_trident_spdif_stream, trident);
		if (kctl == NULL) {
			err = -ENOMEM;
			goto __out;
		}
		kctl->id.index = idx;
		kctl->id.device = pcm_spdif_device;
		if ((err = snd_ctl_add(card, kctl)) < 0)
			goto __out;
		trident->spdif_pcm_ctl = kctl;
	}

	err = 0;
	goto __out;

 __nomem:
	err = -ENOMEM;

 __out:
	kfree(uctl);

	return err;
}

/*
 * gameport interface
 */

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))

static unsigned char snd_trident_gameport_read(struct gameport *gameport)
{
	struct snd_trident *chip = gameport_get_port_data(gameport);

	if (snd_BUG_ON(!chip))
		return 0;
	return inb(TRID_REG(chip, GAMEPORT_LEGACY));
}

static void snd_trident_gameport_trigger(struct gameport *gameport)
{
	struct snd_trident *chip = gameport_get_port_data(gameport);

	if (snd_BUG_ON(!chip))
		return;
	outb(0xff, TRID_REG(chip, GAMEPORT_LEGACY));
}

static int snd_trident_gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	struct snd_trident *chip = gameport_get_port_data(gameport);
	int i;

	if (snd_BUG_ON(!chip))
		return 0;

	*buttons = (~inb(TRID_REG(chip, GAMEPORT_LEGACY)) >> 4) & 0xf;

	for (i = 0; i < 4; i++) {
		axes[i] = inw(TRID_REG(chip, GAMEPORT_AXES + i * 2));
		if (axes[i] == 0xffff) axes[i] = -1;
	}
        
        return 0;
}

static int snd_trident_gameport_open(struct gameport *gameport, int mode)
{
	struct snd_trident *chip = gameport_get_port_data(gameport);

	if (snd_BUG_ON(!chip))
		return 0;

	switch (mode) {
		case GAMEPORT_MODE_COOKED:
			outb(GAMEPORT_MODE_ADC, TRID_REG(chip, GAMEPORT_GCR));
			msleep(20);
			return 0;
		case GAMEPORT_MODE_RAW:
			outb(0, TRID_REG(chip, GAMEPORT_GCR));
			return 0;
		default:
			return -1;
	}
}

int snd_trident_create_gameport(struct snd_trident *chip)
{
	struct gameport *gp;

	chip->gameport = gp = gameport_allocate_port();
	if (!gp) {
		dev_err(chip->card->dev,
			"cannot allocate memory for gameport\n");
		return -ENOMEM;
	}

	gameport_set_name(gp, "Trident 4DWave");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(chip->pci));
	gameport_set_dev_parent(gp, &chip->pci->dev);

	gameport_set_port_data(gp, chip);
	gp->fuzz = 64;
	gp->read = snd_trident_gameport_read;
	gp->trigger = snd_trident_gameport_trigger;
	gp->cooked_read = snd_trident_gameport_cooked_read;
	gp->open = snd_trident_gameport_open;

	gameport_register_port(gp);

	return 0;
}

static inline void snd_trident_free_gameport(struct snd_trident *chip)
{
	if (chip->gameport) {
		gameport_unregister_port(chip->gameport);
		chip->gameport = NULL;
	}
}
#else
int snd_trident_create_gameport(struct snd_trident *chip) { return -ENOSYS; }
static inline void snd_trident_free_gameport(struct snd_trident *chip) { }
#endif /* CONFIG_GAMEPORT */

/*
 * delay for 1 tick
 */
static inline void do_delay(struct snd_trident *chip)
{
	schedule_timeout_uninterruptible(1);
}

/*
 *  SiS reset routine
 */

static int snd_trident_sis_reset(struct snd_trident *trident)
{
	unsigned long end_time;
	unsigned int i;
	int r;

	r = trident->in_suspend ? 0 : 2;	/* count of retries */
      __si7018_retry:
	pci_write_config_byte(trident->pci, 0x46, 0x04);	/* SOFTWARE RESET */
	udelay(100);
	pci_write_config_byte(trident->pci, 0x46, 0x00);
	udelay(100);
	/* disable AC97 GPIO interrupt */
	outb(0x00, TRID_REG(trident, SI_AC97_GPIO));
	/* initialize serial interface, force cold reset */
	i = PCMOUT|SURROUT|CENTEROUT|LFEOUT|SECONDARY_ID|COLD_RESET;
	outl(i, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
	udelay(1000);
	/* remove cold reset */
	i &= ~COLD_RESET;
	outl(i, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
	udelay(2000);
	/* wait, until the codec is ready */
	end_time = (jiffies + (HZ * 3) / 4) + 1;
	do {
		if ((inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)) & SI_AC97_PRIMARY_READY) != 0)
			goto __si7018_ok;
		do_delay(trident);
	} while (time_after_eq(end_time, jiffies));
	dev_err(trident->card->dev, "AC'97 codec ready error [0x%x]\n",
		inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)));
	if (r-- > 0) {
		end_time = jiffies + HZ;
		do {
			do_delay(trident);
		} while (time_after_eq(end_time, jiffies));
		goto __si7018_retry;
	}
      __si7018_ok:
	/* wait for the second codec */
	do {
		if ((inl(TRID_REG(trident, SI_SERIAL_INTF_CTRL)) & SI_AC97_SECONDARY_READY) != 0)
			break;
		do_delay(trident);
	} while (time_after_eq(end_time, jiffies));
	/* enable 64 channel mode */
	outl(BANK_B_EN, TRID_REG(trident, T4D_LFO_GC_CIR));
	return 0;
}

/*  
 *  /proc interface
 */

static void snd_trident_proc_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_trident *trident = entry->private_data;
	char *s;

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_SI7018:
		s = "SiS 7018 Audio";
		break;
	case TRIDENT_DEVICE_ID_DX:
		s = "Trident 4DWave PCI DX";
		break;
	case TRIDENT_DEVICE_ID_NX:
		s = "Trident 4DWave PCI NX";
		break;
	default:
		s = "???";
	}
	snd_iprintf(buffer, "%s\n\n", s);
	snd_iprintf(buffer, "Spurious IRQs    : %d\n", trident->spurious_irq_count);
	snd_iprintf(buffer, "Spurious IRQ dlta: %d\n", trident->spurious_irq_max_delta);
	if (trident->device == TRIDENT_DEVICE_ID_NX || trident->device == TRIDENT_DEVICE_ID_SI7018)
		snd_iprintf(buffer, "IEC958 Mixer Out : %s\n", trident->spdif_ctrl == 0x28 ? "on" : "off");
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		snd_iprintf(buffer, "Rear Speakers    : %s\n", trident->ac97_ctrl & 0x00000010 ? "on" : "off");
		if (trident->tlb.entries) {
			snd_iprintf(buffer,"\nVirtual Memory\n");
			snd_iprintf(buffer, "Memory Maximum : %d\n", trident->tlb.memhdr->size);
			snd_iprintf(buffer, "Memory Used    : %d\n", trident->tlb.memhdr->used);
			snd_iprintf(buffer, "Memory Free    : %d\n", snd_util_mem_avail(trident->tlb.memhdr));
		}
	}
}

static void snd_trident_proc_init(struct snd_trident *trident)
{
	struct snd_info_entry *entry;
	const char *s = "trident";
	
	if (trident->device == TRIDENT_DEVICE_ID_SI7018)
		s = "sis7018";
	if (! snd_card_proc_new(trident->card, s, &entry))
		snd_info_set_text_ops(entry, trident, snd_trident_proc_read);
}

static int snd_trident_dev_free(struct snd_device *device)
{
	struct snd_trident *trident = device->device_data;
	return snd_trident_free(trident);
}

/*---------------------------------------------------------------------------
   snd_trident_tlb_alloc
  
   Description: Allocate and set up the TLB page table on 4D NX.
		Each entry has 4 bytes (physical PCI address).
                
   Parameters:  trident - pointer to target device class for 4DWave.

   Returns:     0 or negative error code
  
  ---------------------------------------------------------------------------*/

static int snd_trident_tlb_alloc(struct snd_trident *trident)
{
	int i;

	/* TLB array must be aligned to 16kB !!! so we allocate
	   32kB region and correct offset when necessary */

	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(trident->pci),
				2 * SNDRV_TRIDENT_MAX_PAGES * 4, &trident->tlb.buffer) < 0) {
		dev_err(trident->card->dev, "unable to allocate TLB buffer\n");
		return -ENOMEM;
	}
	trident->tlb.entries = (unsigned int*)ALIGN((unsigned long)trident->tlb.buffer.area, SNDRV_TRIDENT_MAX_PAGES * 4);
	trident->tlb.entries_dmaaddr = ALIGN(trident->tlb.buffer.addr, SNDRV_TRIDENT_MAX_PAGES * 4);
	/* allocate shadow TLB page table (virtual addresses) */
	trident->tlb.shadow_entries = vmalloc(SNDRV_TRIDENT_MAX_PAGES*sizeof(unsigned long));
	if (trident->tlb.shadow_entries == NULL) {
		dev_err(trident->card->dev,
			"unable to allocate shadow TLB entries\n");
		return -ENOMEM;
	}
	/* allocate and setup silent page and initialise TLB entries */
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(trident->pci),
				SNDRV_TRIDENT_PAGE_SIZE, &trident->tlb.silent_page) < 0) {
		dev_err(trident->card->dev, "unable to allocate silent page\n");
		return -ENOMEM;
	}
	memset(trident->tlb.silent_page.area, 0, SNDRV_TRIDENT_PAGE_SIZE);
	for (i = 0; i < SNDRV_TRIDENT_MAX_PAGES; i++) {
		trident->tlb.entries[i] = cpu_to_le32(trident->tlb.silent_page.addr & ~(SNDRV_TRIDENT_PAGE_SIZE-1));
		trident->tlb.shadow_entries[i] = (unsigned long)trident->tlb.silent_page.area;
	}

	/* use emu memory block manager code to manage tlb page allocation */
	trident->tlb.memhdr = snd_util_memhdr_new(SNDRV_TRIDENT_PAGE_SIZE * SNDRV_TRIDENT_MAX_PAGES);
	if (trident->tlb.memhdr == NULL)
		return -ENOMEM;

	trident->tlb.memhdr->block_extra_size = sizeof(struct snd_trident_memblk_arg);
	return 0;
}

/*
 * initialize 4D DX chip
 */

static void snd_trident_stop_all_voices(struct snd_trident *trident)
{
	outl(0xffffffff, TRID_REG(trident, T4D_STOP_A));
	outl(0xffffffff, TRID_REG(trident, T4D_STOP_B));
	outl(0, TRID_REG(trident, T4D_AINTEN_A));
	outl(0, TRID_REG(trident, T4D_AINTEN_B));
}

static int snd_trident_4d_dx_init(struct snd_trident *trident)
{
	struct pci_dev *pci = trident->pci;
	unsigned long end_time;

	/* reset the legacy configuration and whole audio/wavetable block */
	pci_write_config_dword(pci, 0x40, 0);	/* DDMA */
	pci_write_config_byte(pci, 0x44, 0);	/* ports */
	pci_write_config_byte(pci, 0x45, 0);	/* Legacy DMA */
	pci_write_config_byte(pci, 0x46, 4); /* reset */
	udelay(100);
	pci_write_config_byte(pci, 0x46, 0); /* release reset */
	udelay(100);
	
	/* warm reset of the AC'97 codec */
	outl(0x00000001, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
	udelay(100);
	outl(0x00000000, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
	/* DAC on, disable SB IRQ and try to force ADC valid signal */
	trident->ac97_ctrl = 0x0000004a;
	outl(trident->ac97_ctrl, TRID_REG(trident, DX_ACR2_AC97_COM_STAT));
	/* wait, until the codec is ready */
	end_time = (jiffies + (HZ * 3) / 4) + 1;
	do {
		if ((inl(TRID_REG(trident, DX_ACR2_AC97_COM_STAT)) & 0x0010) != 0)
			goto __dx_ok;
		do_delay(trident);
	} while (time_after_eq(end_time, jiffies));
	dev_err(trident->card->dev, "AC'97 codec ready error\n");
	return -EIO;

 __dx_ok:
	snd_trident_stop_all_voices(trident);

	return 0;
}

/*
 * initialize 4D NX chip
 */
static int snd_trident_4d_nx_init(struct snd_trident *trident)
{
	struct pci_dev *pci = trident->pci;
	unsigned long end_time;

	/* reset the legacy configuration and whole audio/wavetable block */
	pci_write_config_dword(pci, 0x40, 0);	/* DDMA */
	pci_write_config_byte(pci, 0x44, 0);	/* ports */
	pci_write_config_byte(pci, 0x45, 0);	/* Legacy DMA */

	pci_write_config_byte(pci, 0x46, 1); /* reset */
	udelay(100);
	pci_write_config_byte(pci, 0x46, 0); /* release reset */
	udelay(100);

	/* warm reset of the AC'97 codec */
	outl(0x00000001, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	udelay(100);
	outl(0x00000000, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	/* wait, until the codec is ready */
	end_time = (jiffies + (HZ * 3) / 4) + 1;
	do {
		if ((inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT)) & 0x0008) != 0)
			goto __nx_ok;
		do_delay(trident);
	} while (time_after_eq(end_time, jiffies));
	dev_err(trident->card->dev, "AC'97 codec ready error [0x%x]\n",
		inl(TRID_REG(trident, NX_ACR0_AC97_COM_STAT)));
	return -EIO;

 __nx_ok:
	/* DAC on */
	trident->ac97_ctrl = 0x00000002;
	outl(trident->ac97_ctrl, TRID_REG(trident, NX_ACR0_AC97_COM_STAT));
	/* disable SB IRQ */
	outl(NX_SB_IRQ_DISABLE, TRID_REG(trident, T4D_MISCINT));

	snd_trident_stop_all_voices(trident);

	if (trident->tlb.entries != NULL) {
		unsigned int i;
		/* enable virtual addressing via TLB */
		i = trident->tlb.entries_dmaaddr;
		i |= 0x00000001;
		outl(i, TRID_REG(trident, NX_TLBC));
	} else {
		outl(0, TRID_REG(trident, NX_TLBC));
	}
	/* initialize S/PDIF */
	outl(trident->spdif_bits, TRID_REG(trident, NX_SPCSTATUS));
	outb(trident->spdif_ctrl, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));

	return 0;
}

/*
 * initialize sis7018 chip
 */
static int snd_trident_sis_init(struct snd_trident *trident)
{
	int err;

	if ((err = snd_trident_sis_reset(trident)) < 0)
		return err;

	snd_trident_stop_all_voices(trident);

	/* initialize S/PDIF */
	outl(trident->spdif_bits, TRID_REG(trident, SI_SPDIF_CS));

	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_create
  
   Description: This routine will create the device specific class for
                the 4DWave card. It will also perform basic initialization.
                
   Parameters:  card  - which card to create
                pci   - interface to PCI bus resource info
                dma1ptr - playback dma buffer
                dma2ptr - capture dma buffer
                irqptr  -  interrupt resource info

   Returns:     4DWave device class private data
  
  ---------------------------------------------------------------------------*/

int snd_trident_create(struct snd_card *card,
		       struct pci_dev *pci,
		       int pcm_streams,
		       int pcm_spdif_device,
		       int max_wavetable_size,
		       struct snd_trident ** rtrident)
{
	struct snd_trident *trident;
	int i, err;
	struct snd_trident_voice *voice;
	struct snd_trident_pcm_mixer *tmix;
	static struct snd_device_ops ops = {
		.dev_free =	snd_trident_dev_free,
	};

	*rtrident = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;
	/* check, if we can restrict PCI DMA transfers to 30 bits */
	if (dma_set_mask(&pci->dev, DMA_BIT_MASK(30)) < 0 ||
	    dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(30)) < 0) {
		dev_err(card->dev,
			"architecture does not support 30bit PCI busmaster DMA\n");
		pci_disable_device(pci);
		return -ENXIO;
	}
	
	trident = kzalloc(sizeof(*trident), GFP_KERNEL);
	if (trident == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	trident->device = (pci->vendor << 16) | pci->device;
	trident->card = card;
	trident->pci = pci;
	spin_lock_init(&trident->reg_lock);
	spin_lock_init(&trident->event_lock);
	spin_lock_init(&trident->voice_alloc);
	if (pcm_streams < 1)
		pcm_streams = 1;
	if (pcm_streams > 32)
		pcm_streams = 32;
	trident->ChanPCM = pcm_streams;
	if (max_wavetable_size < 0 )
		max_wavetable_size = 0;
	trident->synth.max_size = max_wavetable_size * 1024;
	trident->irq = -1;

	trident->midi_port = TRID_REG(trident, T4D_MPU401_BASE);
	pci_set_master(pci);

	if ((err = pci_request_regions(pci, "Trident Audio")) < 0) {
		kfree(trident);
		pci_disable_device(pci);
		return err;
	}
	trident->port = pci_resource_start(pci, 0);

	if (request_irq(pci->irq, snd_trident_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, trident)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_trident_free(trident);
		return -EBUSY;
	}
	trident->irq = pci->irq;

	/* allocate 16k-aligned TLB for NX cards */
	trident->tlb.entries = NULL;
	trident->tlb.buffer.area = NULL;
	if (trident->device == TRIDENT_DEVICE_ID_NX) {
		if ((err = snd_trident_tlb_alloc(trident)) < 0) {
			snd_trident_free(trident);
			return err;
		}
	}

	trident->spdif_bits = trident->spdif_pcm_bits = SNDRV_PCM_DEFAULT_CON_SPDIF;

	/* initialize chip */
	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
		err = snd_trident_4d_dx_init(trident);
		break;
	case TRIDENT_DEVICE_ID_NX:
		err = snd_trident_4d_nx_init(trident);
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		err = snd_trident_sis_init(trident);
		break;
	default:
		snd_BUG();
		break;
	}
	if (err < 0) {
		snd_trident_free(trident);
		return err;
	}

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, trident, &ops)) < 0) {
		snd_trident_free(trident);
		return err;
	}

	if ((err = snd_trident_mixer(trident, pcm_spdif_device)) < 0)
		return err;
	
	/* initialise synth voices */
	for (i = 0; i < 64; i++) {
		voice = &trident->synth.voices[i];
		voice->number = i;
		voice->trident = trident;
	}
	/* initialize pcm mixer entries */
	for (i = 0; i < 32; i++) {
		tmix = &trident->pcm_mixer[i];
		tmix->vol = T4D_DEFAULT_PCM_VOL;
		tmix->pan = T4D_DEFAULT_PCM_PAN;
		tmix->rvol = T4D_DEFAULT_PCM_RVOL;
		tmix->cvol = T4D_DEFAULT_PCM_CVOL;
	}

	snd_trident_enable_eso(trident);

	snd_trident_proc_init(trident);
	*rtrident = trident;
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_free
  
   Description: This routine will free the device specific class for
                the 4DWave card. 
                
   Parameters:  trident  - device specific private data for 4DWave card

   Returns:     None.
  
  ---------------------------------------------------------------------------*/

static int snd_trident_free(struct snd_trident *trident)
{
	snd_trident_free_gameport(trident);
	snd_trident_disable_eso(trident);
	// Disable S/PDIF out
	if (trident->device == TRIDENT_DEVICE_ID_NX)
		outb(0x00, TRID_REG(trident, NX_SPCTRL_SPCSO + 3));
	else if (trident->device == TRIDENT_DEVICE_ID_SI7018) {
		outl(0, TRID_REG(trident, SI_SERIAL_INTF_CTRL));
	}
	if (trident->irq >= 0)
		free_irq(trident->irq, trident);
	if (trident->tlb.buffer.area) {
		outl(0, TRID_REG(trident, NX_TLBC));
		snd_util_memhdr_free(trident->tlb.memhdr);
		if (trident->tlb.silent_page.area)
			snd_dma_free_pages(&trident->tlb.silent_page);
		vfree(trident->tlb.shadow_entries);
		snd_dma_free_pages(&trident->tlb.buffer);
	}
	pci_release_regions(trident->pci);
	pci_disable_device(trident->pci);
	kfree(trident);
	return 0;
}

/*---------------------------------------------------------------------------
   snd_trident_interrupt
  
   Description: ISR for Trident 4DWave device
                
   Parameters:  trident  - device specific private data for 4DWave card

   Problems:    It seems that Trident chips generates interrupts more than
                one time in special cases. The spurious interrupts are
                detected via sample timer (T4D_STIMER) and computing
                corresponding delta value. The limits are detected with
                the method try & fail so it is possible that it won't
                work on all computers. [jaroslav]

   Returns:     None.
  
  ---------------------------------------------------------------------------*/

static irqreturn_t snd_trident_interrupt(int irq, void *dev_id)
{
	struct snd_trident *trident = dev_id;
	unsigned int audio_int, chn_int, stimer, channel, mask, tmp;
	int delta;
	struct snd_trident_voice *voice;

	audio_int = inl(TRID_REG(trident, T4D_MISCINT));
	if ((audio_int & (ADDRESS_IRQ|MPU401_IRQ)) == 0)
		return IRQ_NONE;
	if (audio_int & ADDRESS_IRQ) {
		// get interrupt status for all channels
		spin_lock(&trident->reg_lock);
		stimer = inl(TRID_REG(trident, T4D_STIMER)) & 0x00ffffff;
		chn_int = inl(TRID_REG(trident, T4D_AINT_A));
		if (chn_int == 0)
			goto __skip1;
		outl(chn_int, TRID_REG(trident, T4D_AINT_A));	/* ack */
	      __skip1:
		chn_int = inl(TRID_REG(trident, T4D_AINT_B));
		if (chn_int == 0)
			goto __skip2;
		for (channel = 63; channel >= 32; channel--) {
			mask = 1 << (channel&0x1f);
			if ((chn_int & mask) == 0)
				continue;
			voice = &trident->synth.voices[channel];
			if (!voice->pcm || voice->substream == NULL) {
				outl(mask, TRID_REG(trident, T4D_STOP_B));
				continue;
			}
			delta = (int)stimer - (int)voice->stimer;
			if (delta < 0)
				delta = -delta;
			if ((unsigned int)delta < voice->spurious_threshold) {
				/* do some statistics here */
				trident->spurious_irq_count++;
				if (trident->spurious_irq_max_delta < (unsigned int)delta)
					trident->spurious_irq_max_delta = delta;
				continue;
			}
			voice->stimer = stimer;
			if (voice->isync) {
				if (!voice->isync3) {
					tmp = inw(TRID_REG(trident, T4D_SBBL_SBCL));
					if (trident->bDMAStart & 0x40)
						tmp >>= 1;
					if (tmp > 0)
						tmp = voice->isync_max - tmp;
				} else {
					tmp = inl(TRID_REG(trident, NX_SPCTRL_SPCSO)) & 0x00ffffff;
				}
				if (tmp < voice->isync_mark) {
					if (tmp > 0x10)
						tmp = voice->isync_ESO - 7;
					else
						tmp = voice->isync_ESO + 2;
					/* update ESO for IRQ voice to preserve sync */
					snd_trident_stop_voice(trident, voice->number);
					snd_trident_write_eso_reg(trident, voice, tmp);
					snd_trident_start_voice(trident, voice->number);
				}
			} else if (voice->isync2) {
				voice->isync2 = 0;
				/* write original ESO and update CSO for IRQ voice to preserve sync */
				snd_trident_stop_voice(trident, voice->number);
				snd_trident_write_cso_reg(trident, voice, voice->isync_mark);
				snd_trident_write_eso_reg(trident, voice, voice->ESO);
				snd_trident_start_voice(trident, voice->number);
			}
#if 0
			if (voice->extra) {
				/* update CSO for extra voice to preserve sync */
				snd_trident_stop_voice(trident, voice->extra->number);
				snd_trident_write_cso_reg(trident, voice->extra, 0);
				snd_trident_start_voice(trident, voice->extra->number);
			}
#endif
			spin_unlock(&trident->reg_lock);
			snd_pcm_period_elapsed(voice->substream);
			spin_lock(&trident->reg_lock);
		}
		outl(chn_int, TRID_REG(trident, T4D_AINT_B));	/* ack */
	      __skip2:
		spin_unlock(&trident->reg_lock);
	}
	if (audio_int & MPU401_IRQ) {
		if (trident->rmidi) {
			snd_mpu401_uart_interrupt(irq, trident->rmidi->private_data);
		} else {
			inb(TRID_REG(trident, T4D_MPUR0));
		}
	}
	// outl((ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW), TRID_REG(trident, T4D_MISCINT));
	return IRQ_HANDLED;
}

struct snd_trident_voice *snd_trident_alloc_voice(struct snd_trident * trident, int type, int client, int port)
{
	struct snd_trident_voice *pvoice;
	unsigned long flags;
	int idx;

	spin_lock_irqsave(&trident->voice_alloc, flags);
	if (type == SNDRV_TRIDENT_VOICE_TYPE_PCM) {
		idx = snd_trident_allocate_pcm_channel(trident);
		if(idx < 0) {
			spin_unlock_irqrestore(&trident->voice_alloc, flags);
			return NULL;
		}
		pvoice = &trident->synth.voices[idx];
		pvoice->use = 1;
		pvoice->pcm = 1;
		pvoice->capture = 0;
		pvoice->spdif = 0;
		pvoice->memblk = NULL;
		pvoice->substream = NULL;
		spin_unlock_irqrestore(&trident->voice_alloc, flags);
		return pvoice;
	}
	if (type == SNDRV_TRIDENT_VOICE_TYPE_SYNTH) {
		idx = snd_trident_allocate_synth_channel(trident);
		if(idx < 0) {
			spin_unlock_irqrestore(&trident->voice_alloc, flags);
			return NULL;
		}
		pvoice = &trident->synth.voices[idx];
		pvoice->use = 1;
		pvoice->synth = 1;
		pvoice->client = client;
		pvoice->port = port;
		pvoice->memblk = NULL;
		spin_unlock_irqrestore(&trident->voice_alloc, flags);
		return pvoice;
	}
	if (type == SNDRV_TRIDENT_VOICE_TYPE_MIDI) {
	}
	spin_unlock_irqrestore(&trident->voice_alloc, flags);
	return NULL;
}

EXPORT_SYMBOL(snd_trident_alloc_voice);

void snd_trident_free_voice(struct snd_trident * trident, struct snd_trident_voice *voice)
{
	unsigned long flags;
	void (*private_free)(struct snd_trident_voice *);

	if (voice == NULL || !voice->use)
		return;
	snd_trident_clear_voices(trident, voice->number, voice->number);
	spin_lock_irqsave(&trident->voice_alloc, flags);
	private_free = voice->private_free;
	voice->private_free = NULL;
	voice->private_data = NULL;
	if (voice->pcm)
		snd_trident_free_pcm_channel(trident, voice->number);
	if (voice->synth)
		snd_trident_free_synth_channel(trident, voice->number);
	voice->use = voice->pcm = voice->synth = voice->midi = 0;
	voice->capture = voice->spdif = 0;
	voice->sample_ops = NULL;
	voice->substream = NULL;
	voice->extra = NULL;
	spin_unlock_irqrestore(&trident->voice_alloc, flags);
	if (private_free)
		private_free(voice);
}

EXPORT_SYMBOL(snd_trident_free_voice);

static void snd_trident_clear_voices(struct snd_trident * trident, unsigned short v_min, unsigned short v_max)
{
	unsigned int i, val, mask[2] = { 0, 0 };

	if (snd_BUG_ON(v_min > 63 || v_max > 63))
		return;
	for (i = v_min; i <= v_max; i++)
		mask[i >> 5] |= 1 << (i & 0x1f);
	if (mask[0]) {
		outl(mask[0], TRID_REG(trident, T4D_STOP_A));
		val = inl(TRID_REG(trident, T4D_AINTEN_A));
		outl(val & ~mask[0], TRID_REG(trident, T4D_AINTEN_A));
	}
	if (mask[1]) {
		outl(mask[1], TRID_REG(trident, T4D_STOP_B));
		val = inl(TRID_REG(trident, T4D_AINTEN_B));
		outl(val & ~mask[1], TRID_REG(trident, T4D_AINTEN_B));
	}
}

#ifdef CONFIG_PM_SLEEP
static int snd_trident_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_trident *trident = card->private_data;

	trident->in_suspend = 1;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(trident->pcm);
	snd_pcm_suspend_all(trident->foldback);
	snd_pcm_suspend_all(trident->spdif);

	snd_ac97_suspend(trident->ac97);
	snd_ac97_suspend(trident->ac97_sec);
	return 0;
}

static int snd_trident_resume(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct snd_trident *trident = card->private_data;

	switch (trident->device) {
	case TRIDENT_DEVICE_ID_DX:
		snd_trident_4d_dx_init(trident);
		break;
	case TRIDENT_DEVICE_ID_NX:
		snd_trident_4d_nx_init(trident);
		break;
	case TRIDENT_DEVICE_ID_SI7018:
		snd_trident_sis_init(trident);
		break;
	}

	snd_ac97_resume(trident->ac97);
	snd_ac97_resume(trident->ac97_sec);

	/* restore some registers */
	outl(trident->musicvol_wavevol, TRID_REG(trident, T4D_MUSICVOL_WAVEVOL));

	snd_trident_enable_eso(trident);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	trident->in_suspend = 0;
	return 0;
}

SIMPLE_DEV_PM_OPS(snd_trident_pm, snd_trident_suspend, snd_trident_resume);
#endif /* CONFIG_PM_SLEEP */
