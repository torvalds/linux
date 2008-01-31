/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of YMF724/740/744/754 chips
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
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/tlv.h>
#include <sound/ymfpci.h>
#include <sound/asoundef.h>
#include <sound/mpu401.h>

#include <asm/io.h>
#include <asm/byteorder.h>

/*
 *  common I/O routines
 */

static void snd_ymfpci_irq_wait(struct snd_ymfpci *chip);

static inline u8 snd_ymfpci_readb(struct snd_ymfpci *chip, u32 offset)
{
	return readb(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writeb(struct snd_ymfpci *chip, u32 offset, u8 val)
{
	writeb(val, chip->reg_area_virt + offset);
}

static inline u16 snd_ymfpci_readw(struct snd_ymfpci *chip, u32 offset)
{
	return readw(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writew(struct snd_ymfpci *chip, u32 offset, u16 val)
{
	writew(val, chip->reg_area_virt + offset);
}

static inline u32 snd_ymfpci_readl(struct snd_ymfpci *chip, u32 offset)
{
	return readl(chip->reg_area_virt + offset);
}

static inline void snd_ymfpci_writel(struct snd_ymfpci *chip, u32 offset, u32 val)
{
	writel(val, chip->reg_area_virt + offset);
}

static int snd_ymfpci_codec_ready(struct snd_ymfpci *chip, int secondary)
{
	unsigned long end_time;
	u32 reg = secondary ? YDSXGR_SECSTATUSADR : YDSXGR_PRISTATUSADR;
	
	end_time = jiffies + msecs_to_jiffies(750);
	do {
		if ((snd_ymfpci_readw(chip, reg) & 0x8000) == 0)
			return 0;
		schedule_timeout_uninterruptible(1);
	} while (time_before(jiffies, end_time));
	snd_printk(KERN_ERR "codec_ready: codec %i is not ready [0x%x]\n", secondary, snd_ymfpci_readw(chip, reg));
	return -EBUSY;
}

static void snd_ymfpci_codec_write(struct snd_ac97 *ac97, u16 reg, u16 val)
{
	struct snd_ymfpci *chip = ac97->private_data;
	u32 cmd;
	
	snd_ymfpci_codec_ready(chip, 0);
	cmd = ((YDSXG_AC97WRITECMD | reg) << 16) | val;
	snd_ymfpci_writel(chip, YDSXGR_AC97CMDDATA, cmd);
}

static u16 snd_ymfpci_codec_read(struct snd_ac97 *ac97, u16 reg)
{
	struct snd_ymfpci *chip = ac97->private_data;

	if (snd_ymfpci_codec_ready(chip, 0))
		return ~0;
	snd_ymfpci_writew(chip, YDSXGR_AC97CMDADR, YDSXG_AC97READCMD | reg);
	if (snd_ymfpci_codec_ready(chip, 0))
		return ~0;
	if (chip->device_id == PCI_DEVICE_ID_YAMAHA_744 && chip->rev < 2) {
		int i;
		for (i = 0; i < 600; i++)
			snd_ymfpci_readw(chip, YDSXGR_PRISTATUSDATA);
	}
	return snd_ymfpci_readw(chip, YDSXGR_PRISTATUSDATA);
}

/*
 *  Misc routines
 */

static u32 snd_ymfpci_calc_delta(u32 rate)
{
	switch (rate) {
	case 8000:	return 0x02aaab00;
	case 11025:	return 0x03accd00;
	case 16000:	return 0x05555500;
	case 22050:	return 0x07599a00;
	case 32000:	return 0x0aaaab00;
	case 44100:	return 0x0eb33300;
	default:	return ((rate << 16) / 375) << 5;
	}
}

static u32 def_rate[8] = {
	100, 2000, 8000, 11025, 16000, 22050, 32000, 48000
};

static u32 snd_ymfpci_calc_lpfK(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x00570000, 0x06AA0000, 0x18B20000, 0x20930000,
		0x2B9A0000, 0x35A10000, 0x3EAA0000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x40000000;	/* FIXME: What's the right value? */
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

static u32 snd_ymfpci_calc_lpfQ(u32 rate)
{
	u32 i;
	static u32 val[8] = {
		0x35280000, 0x34A70000, 0x32020000, 0x31770000,
		0x31390000, 0x31C90000, 0x33D00000, 0x40000000
	};
	
	if (rate == 44100)
		return 0x370A0000;
	for (i = 0; i < 8; i++)
		if (rate <= def_rate[i])
			return val[i];
	return val[0];
}

/*
 *  Hardware start management
 */

static void snd_ymfpci_hw_start(struct snd_ymfpci *chip)
{
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->start_count++ > 0)
		goto __end;
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) | 3);
	chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT) & 1;
      __end:
      	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

static void snd_ymfpci_hw_stop(struct snd_ymfpci *chip)
{
	unsigned long flags;
	long timeout = 1000;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (--chip->start_count > 0)
		goto __end;
	snd_ymfpci_writel(chip, YDSXGR_MODE,
			  snd_ymfpci_readl(chip, YDSXGR_MODE) & ~3);
	while (timeout-- > 0) {
		if ((snd_ymfpci_readl(chip, YDSXGR_STATUS) & 2) == 0)
			break;
	}
	if (atomic_read(&chip->interrupt_sleep_count)) {
		atomic_set(&chip->interrupt_sleep_count, 0);
		wake_up(&chip->interrupt_sleep);
	}
      __end:
      	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

/*
 *  Playback voice management
 */

static int voice_alloc(struct snd_ymfpci *chip,
		       enum snd_ymfpci_voice_type type, int pair,
		       struct snd_ymfpci_voice **rvoice)
{
	struct snd_ymfpci_voice *voice, *voice2;
	int idx;
	
	*rvoice = NULL;
	for (idx = 0; idx < YDSXG_PLAYBACK_VOICES; idx += pair ? 2 : 1) {
		voice = &chip->voices[idx];
		voice2 = pair ? &chip->voices[idx+1] : NULL;
		if (voice->use || (voice2 && voice2->use))
			continue;
		voice->use = 1;
		if (voice2)
			voice2->use = 1;
		switch (type) {
		case YMFPCI_PCM:
			voice->pcm = 1;
			if (voice2)
				voice2->pcm = 1;
			break;
		case YMFPCI_SYNTH:
			voice->synth = 1;
			break;
		case YMFPCI_MIDI:
			voice->midi = 1;
			break;
		}
		snd_ymfpci_hw_start(chip);
		if (voice2)
			snd_ymfpci_hw_start(chip);
		*rvoice = voice;
		return 0;
	}
	return -ENOMEM;
}

static int snd_ymfpci_voice_alloc(struct snd_ymfpci *chip,
				  enum snd_ymfpci_voice_type type, int pair,
				  struct snd_ymfpci_voice **rvoice)
{
	unsigned long flags;
	int result;
	
	snd_assert(rvoice != NULL, return -EINVAL);
	snd_assert(!pair || type == YMFPCI_PCM, return -EINVAL);
	
	spin_lock_irqsave(&chip->voice_lock, flags);
	for (;;) {
		result = voice_alloc(chip, type, pair, rvoice);
		if (result == 0 || type != YMFPCI_PCM)
			break;
		/* TODO: synth/midi voice deallocation */
		break;
	}
	spin_unlock_irqrestore(&chip->voice_lock, flags);	
	return result;		
}

static int snd_ymfpci_voice_free(struct snd_ymfpci *chip, struct snd_ymfpci_voice *pvoice)
{
	unsigned long flags;
	
	snd_assert(pvoice != NULL, return -EINVAL);
	snd_ymfpci_hw_stop(chip);
	spin_lock_irqsave(&chip->voice_lock, flags);
	if (pvoice->number == chip->src441_used) {
		chip->src441_used = -1;
		pvoice->ypcm->use_441_slot = 0;
	}
	pvoice->use = pvoice->pcm = pvoice->synth = pvoice->midi = 0;
	pvoice->ypcm = NULL;
	pvoice->interrupt = NULL;
	spin_unlock_irqrestore(&chip->voice_lock, flags);
	return 0;
}

/*
 *  PCM part
 */

static void snd_ymfpci_pcm_interrupt(struct snd_ymfpci *chip, struct snd_ymfpci_voice *voice)
{
	struct snd_ymfpci_pcm *ypcm;
	u32 pos, delta;
	
	if ((ypcm = voice->ypcm) == NULL)
		return;
	if (ypcm->substream == NULL)
		return;
	spin_lock(&chip->reg_lock);
	if (ypcm->running) {
		pos = le32_to_cpu(voice->bank[chip->active_bank].start);
		if (pos < ypcm->last_pos)
			delta = pos + (ypcm->buffer_size - ypcm->last_pos);
		else
			delta = pos - ypcm->last_pos;
		ypcm->period_pos += delta;
		ypcm->last_pos = pos;
		if (ypcm->period_pos >= ypcm->period_size) {
			// printk("done - active_bank = 0x%x, start = 0x%x\n", chip->active_bank, voice->bank[chip->active_bank].start);
			ypcm->period_pos %= ypcm->period_size;
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(ypcm->substream);
			spin_lock(&chip->reg_lock);
		}

		if (unlikely(ypcm->update_pcm_vol)) {
			unsigned int subs = ypcm->substream->number;
			unsigned int next_bank = 1 - chip->active_bank;
			struct snd_ymfpci_playback_bank *bank;
			u32 volume;
			
			bank = &voice->bank[next_bank];
			volume = cpu_to_le32(chip->pcm_mixer[subs].left << 15);
			bank->left_gain_end = volume;
			if (ypcm->output_rear)
				bank->eff2_gain_end = volume;
			if (ypcm->voices[1])
				bank = &ypcm->voices[1]->bank[next_bank];
			volume = cpu_to_le32(chip->pcm_mixer[subs].right << 15);
			bank->right_gain_end = volume;
			if (ypcm->output_rear)
				bank->eff3_gain_end = volume;
			ypcm->update_pcm_vol--;
		}
	}
	spin_unlock(&chip->reg_lock);
}

static void snd_ymfpci_pcm_capture_interrupt(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;
	struct snd_ymfpci *chip = ypcm->chip;
	u32 pos, delta;
	
	spin_lock(&chip->reg_lock);
	if (ypcm->running) {
		pos = le32_to_cpu(chip->bank_capture[ypcm->capture_bank_number][chip->active_bank]->start) >> ypcm->shift;
		if (pos < ypcm->last_pos)
			delta = pos + (ypcm->buffer_size - ypcm->last_pos);
		else
			delta = pos - ypcm->last_pos;
		ypcm->period_pos += delta;
		ypcm->last_pos = pos;
		if (ypcm->period_pos >= ypcm->period_size) {
			ypcm->period_pos %= ypcm->period_size;
			// printk("done - active_bank = 0x%x, start = 0x%x\n", chip->active_bank, voice->bank[chip->active_bank].start);
			spin_unlock(&chip->reg_lock);
			snd_pcm_period_elapsed(substream);
			spin_lock(&chip->reg_lock);
		}
	}
	spin_unlock(&chip->reg_lock);
}

static int snd_ymfpci_playback_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_ymfpci_pcm *ypcm = substream->runtime->private_data;
	struct snd_kcontrol *kctl = NULL;
	int result = 0;

	spin_lock(&chip->reg_lock);
	if (ypcm->voices[0] == NULL) {
		result = -EINVAL;
		goto __unlock;
	}
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		chip->ctrl_playback[ypcm->voices[0]->number + 1] = cpu_to_le32(ypcm->voices[0]->bank_addr);
		if (ypcm->voices[1] != NULL && !ypcm->use_441_slot)
			chip->ctrl_playback[ypcm->voices[1]->number + 1] = cpu_to_le32(ypcm->voices[1]->bank_addr);
		ypcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream->pcm == chip->pcm && !ypcm->use_441_slot) {
			kctl = chip->pcm_mixer[substream->number].ctl;
			kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		}
		/* fall through */
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		chip->ctrl_playback[ypcm->voices[0]->number + 1] = 0;
		if (ypcm->voices[1] != NULL && !ypcm->use_441_slot)
			chip->ctrl_playback[ypcm->voices[1]->number + 1] = 0;
		ypcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
      __unlock:
	spin_unlock(&chip->reg_lock);
	if (kctl)
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_INFO, &kctl->id);
	return result;
}
static int snd_ymfpci_capture_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_ymfpci_pcm *ypcm = substream->runtime->private_data;
	int result = 0;
	u32 tmp;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		tmp = snd_ymfpci_readl(chip, YDSXGR_MAPOFREC) | (1 << ypcm->capture_bank_number);
		snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tmp = snd_ymfpci_readl(chip, YDSXGR_MAPOFREC) & ~(1 << ypcm->capture_bank_number);
		snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, tmp);
		ypcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return result;
}

static int snd_ymfpci_pcm_voice_alloc(struct snd_ymfpci_pcm *ypcm, int voices)
{
	int err;

	if (ypcm->voices[1] != NULL && voices < 2) {
		snd_ymfpci_voice_free(ypcm->chip, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (voices == 1 && ypcm->voices[0] != NULL)
		return 0;		/* already allocated */
	if (voices == 2 && ypcm->voices[0] != NULL && ypcm->voices[1] != NULL)
		return 0;		/* already allocated */
	if (voices > 1) {
		if (ypcm->voices[0] != NULL && ypcm->voices[1] == NULL) {
			snd_ymfpci_voice_free(ypcm->chip, ypcm->voices[0]);
			ypcm->voices[0] = NULL;
		}		
	}
	err = snd_ymfpci_voice_alloc(ypcm->chip, YMFPCI_PCM, voices > 1, &ypcm->voices[0]);
	if (err < 0)
		return err;
	ypcm->voices[0]->ypcm = ypcm;
	ypcm->voices[0]->interrupt = snd_ymfpci_pcm_interrupt;
	if (voices > 1) {
		ypcm->voices[1] = &ypcm->chip->voices[ypcm->voices[0]->number + 1];
		ypcm->voices[1]->ypcm = ypcm;
	}
	return 0;
}

static void snd_ymfpci_pcm_init_voice(struct snd_ymfpci_pcm *ypcm, unsigned int voiceidx,
				      struct snd_pcm_runtime *runtime,
				      int has_pcm_volume)
{
	struct snd_ymfpci_voice *voice = ypcm->voices[voiceidx];
	u32 format;
	u32 delta = snd_ymfpci_calc_delta(runtime->rate);
	u32 lpfQ = snd_ymfpci_calc_lpfQ(runtime->rate);
	u32 lpfK = snd_ymfpci_calc_lpfK(runtime->rate);
	struct snd_ymfpci_playback_bank *bank;
	unsigned int nbank;
	u32 vol_left, vol_right;
	u8 use_left, use_right;
	unsigned long flags;

	snd_assert(voice != NULL, return);
	if (runtime->channels == 1) {
		use_left = 1;
		use_right = 1;
	} else {
		use_left = (voiceidx & 1) == 0;
		use_right = !use_left;
	}
	if (has_pcm_volume) {
		vol_left = cpu_to_le32(ypcm->chip->pcm_mixer
				       [ypcm->substream->number].left << 15);
		vol_right = cpu_to_le32(ypcm->chip->pcm_mixer
					[ypcm->substream->number].right << 15);
	} else {
		vol_left = cpu_to_le32(0x40000000);
		vol_right = cpu_to_le32(0x40000000);
	}
	spin_lock_irqsave(&ypcm->chip->voice_lock, flags);
	format = runtime->channels == 2 ? 0x00010000 : 0;
	if (snd_pcm_format_width(runtime->format) == 8)
		format |= 0x80000000;
	else if (ypcm->chip->device_id == PCI_DEVICE_ID_YAMAHA_754 &&
		 runtime->rate == 44100 && runtime->channels == 2 &&
		 voiceidx == 0 && (ypcm->chip->src441_used == -1 ||
				   ypcm->chip->src441_used == voice->number)) {
		ypcm->chip->src441_used = voice->number;
		ypcm->use_441_slot = 1;
		format |= 0x10000000;
	}
	if (ypcm->chip->src441_used == voice->number &&
	    (format & 0x10000000) == 0) {
		ypcm->chip->src441_used = -1;
		ypcm->use_441_slot = 0;
	}
	if (runtime->channels == 2 && (voiceidx & 1) != 0)
		format |= 1;
	spin_unlock_irqrestore(&ypcm->chip->voice_lock, flags);
	for (nbank = 0; nbank < 2; nbank++) {
		bank = &voice->bank[nbank];
		memset(bank, 0, sizeof(*bank));
		bank->format = cpu_to_le32(format);
		bank->base = cpu_to_le32(runtime->dma_addr);
		bank->loop_end = cpu_to_le32(ypcm->buffer_size);
		bank->lpfQ = cpu_to_le32(lpfQ);
		bank->delta =
		bank->delta_end = cpu_to_le32(delta);
		bank->lpfK =
		bank->lpfK_end = cpu_to_le32(lpfK);
		bank->eg_gain =
		bank->eg_gain_end = cpu_to_le32(0x40000000);

		if (ypcm->output_front) {
			if (use_left) {
				bank->left_gain =
				bank->left_gain_end = vol_left;
			}
			if (use_right) {
				bank->right_gain =
				bank->right_gain_end = vol_right;
			}
		}
		if (ypcm->output_rear) {
		        if (!ypcm->swap_rear) {
        			if (use_left) {
        				bank->eff2_gain =
        				bank->eff2_gain_end = vol_left;
        			}
        			if (use_right) {
        				bank->eff3_gain =
        				bank->eff3_gain_end = vol_right;
        			}
		        } else {
        			/* The SPDIF out channels seem to be swapped, so we have
        			 * to swap them here, too.  The rear analog out channels
        			 * will be wrong, but otherwise AC3 would not work.
        			 */
        			if (use_left) {
        				bank->eff3_gain =
        				bank->eff3_gain_end = vol_left;
        			}
        			if (use_right) {
        				bank->eff2_gain =
        				bank->eff2_gain_end = vol_right;
        			}
        		}
                }
	}
}

static int __devinit snd_ymfpci_ac3_init(struct snd_ymfpci *chip)
{
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				4096, &chip->ac3_tmp_base) < 0)
		return -ENOMEM;

	chip->bank_effect[3][0]->base =
	chip->bank_effect[3][1]->base = cpu_to_le32(chip->ac3_tmp_base.addr);
	chip->bank_effect[3][0]->loop_end =
	chip->bank_effect[3][1]->loop_end = cpu_to_le32(1024);
	chip->bank_effect[4][0]->base =
	chip->bank_effect[4][1]->base = cpu_to_le32(chip->ac3_tmp_base.addr + 2048);
	chip->bank_effect[4][0]->loop_end =
	chip->bank_effect[4][1]->loop_end = cpu_to_le32(1024);

	spin_lock_irq(&chip->reg_lock);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT,
			  snd_ymfpci_readl(chip, YDSXGR_MAPOFEFFECT) | 3 << 3);
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_ac3_done(struct snd_ymfpci *chip)
{
	spin_lock_irq(&chip->reg_lock);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT,
			  snd_ymfpci_readl(chip, YDSXGR_MAPOFEFFECT) & ~(3 << 3));
	spin_unlock_irq(&chip->reg_lock);
	// snd_ymfpci_irq_wait(chip);
	if (chip->ac3_tmp_base.area) {
		snd_dma_free_pages(&chip->ac3_tmp_base);
		chip->ac3_tmp_base.area = NULL;
	}
	return 0;
}

static int snd_ymfpci_playback_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if ((err = snd_ymfpci_pcm_voice_alloc(ypcm, params_channels(hw_params))) < 0)
		return err;
	return 0;
}

static int snd_ymfpci_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;
	
	if (runtime->private_data == NULL)
		return 0;
	ypcm = runtime->private_data;

	/* wait, until the PCI operations are not finished */
	snd_ymfpci_irq_wait(chip);
	snd_pcm_lib_free_pages(substream);
	if (ypcm->voices[1]) {
		snd_ymfpci_voice_free(chip, ypcm->voices[1]);
		ypcm->voices[1] = NULL;
	}
	if (ypcm->voices[0]) {
		snd_ymfpci_voice_free(chip, ypcm->voices[0]);
		ypcm->voices[0] = NULL;
	}
	return 0;
}

static int snd_ymfpci_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;
	struct snd_kcontrol *kctl;
	unsigned int nvoice;

	ypcm->period_size = runtime->period_size;
	ypcm->buffer_size = runtime->buffer_size;
	ypcm->period_pos = 0;
	ypcm->last_pos = 0;
	for (nvoice = 0; nvoice < runtime->channels; nvoice++)
		snd_ymfpci_pcm_init_voice(ypcm, nvoice, runtime,
					  substream->pcm == chip->pcm);

	if (substream->pcm == chip->pcm && !ypcm->use_441_slot) {
		kctl = chip->pcm_mixer[substream->number].ctl;
		kctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_INFO, &kctl->id);
	}
	return 0;
}

static int snd_ymfpci_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_ymfpci_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);

	/* wait, until the PCI operations are not finished */
	snd_ymfpci_irq_wait(chip);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_ymfpci_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;
	struct snd_ymfpci_capture_bank * bank;
	int nbank;
	u32 rate, format;

	ypcm->period_size = runtime->period_size;
	ypcm->buffer_size = runtime->buffer_size;
	ypcm->period_pos = 0;
	ypcm->last_pos = 0;
	ypcm->shift = 0;
	rate = ((48000 * 4096) / runtime->rate) - 1;
	format = 0;
	if (runtime->channels == 2) {
		format |= 2;
		ypcm->shift++;
	}
	if (snd_pcm_format_width(runtime->format) == 8)
		format |= 1;
	else
		ypcm->shift++;
	switch (ypcm->capture_bank_number) {
	case 0:
		snd_ymfpci_writel(chip, YDSXGR_RECFORMAT, format);
		snd_ymfpci_writel(chip, YDSXGR_RECSLOTSR, rate);
		break;
	case 1:
		snd_ymfpci_writel(chip, YDSXGR_ADCFORMAT, format);
		snd_ymfpci_writel(chip, YDSXGR_ADCSLOTSR, rate);
		break;
	}
	for (nbank = 0; nbank < 2; nbank++) {
		bank = chip->bank_capture[ypcm->capture_bank_number][nbank];
		bank->base = cpu_to_le32(runtime->dma_addr);
		bank->loop_end = cpu_to_le32(ypcm->buffer_size << ypcm->shift);
		bank->start = 0;
		bank->num_of_loops = 0;
	}
	return 0;
}

static snd_pcm_uframes_t snd_ymfpci_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;
	struct snd_ymfpci_voice *voice = ypcm->voices[0];

	if (!(ypcm->running && voice))
		return 0;
	return le32_to_cpu(voice->bank[chip->active_bank].start);
}

static snd_pcm_uframes_t snd_ymfpci_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;

	if (!ypcm->running)
		return 0;
	return le32_to_cpu(chip->bank_capture[ypcm->capture_bank_number][chip->active_bank]->start) >> ypcm->shift;
}

static void snd_ymfpci_irq_wait(struct snd_ymfpci *chip)
{
	wait_queue_t wait;
	int loops = 4;

	while (loops-- > 0) {
		if ((snd_ymfpci_readl(chip, YDSXGR_MODE) & 3) == 0)
		 	continue;
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&chip->interrupt_sleep, &wait);
		atomic_inc(&chip->interrupt_sleep_count);
		schedule_timeout_uninterruptible(msecs_to_jiffies(50));
		remove_wait_queue(&chip->interrupt_sleep, &wait);
	}
}

static irqreturn_t snd_ymfpci_interrupt(int irq, void *dev_id)
{
	struct snd_ymfpci *chip = dev_id;
	u32 status, nvoice, mode;
	struct snd_ymfpci_voice *voice;

	status = snd_ymfpci_readl(chip, YDSXGR_STATUS);
	if (status & 0x80000000) {
		chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT) & 1;
		spin_lock(&chip->voice_lock);
		for (nvoice = 0; nvoice < YDSXG_PLAYBACK_VOICES; nvoice++) {
			voice = &chip->voices[nvoice];
			if (voice->interrupt)
				voice->interrupt(chip, voice);
		}
		for (nvoice = 0; nvoice < YDSXG_CAPTURE_VOICES; nvoice++) {
			if (chip->capture_substream[nvoice])
				snd_ymfpci_pcm_capture_interrupt(chip->capture_substream[nvoice]);
		}
#if 0
		for (nvoice = 0; nvoice < YDSXG_EFFECT_VOICES; nvoice++) {
			if (chip->effect_substream[nvoice])
				snd_ymfpci_pcm_effect_interrupt(chip->effect_substream[nvoice]);
		}
#endif
		spin_unlock(&chip->voice_lock);
		spin_lock(&chip->reg_lock);
		snd_ymfpci_writel(chip, YDSXGR_STATUS, 0x80000000);
		mode = snd_ymfpci_readl(chip, YDSXGR_MODE) | 2;
		snd_ymfpci_writel(chip, YDSXGR_MODE, mode);
		spin_unlock(&chip->reg_lock);

		if (atomic_read(&chip->interrupt_sleep_count)) {
			atomic_set(&chip->interrupt_sleep_count, 0);
			wake_up(&chip->interrupt_sleep);
		}
	}

	status = snd_ymfpci_readw(chip, YDSXGR_INTFLAG);
	if (status & 1) {
		if (chip->timer)
			snd_timer_interrupt(chip->timer, chip->timer->sticks);
	}
	snd_ymfpci_writew(chip, YDSXGR_INTFLAG, status);

	if (chip->rawmidi)
		snd_mpu401_uart_interrupt(irq, chip->rawmidi->private_data);
	return IRQ_HANDLED;
}

static struct snd_pcm_hardware snd_ymfpci_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID | 
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	256 * 1024, /* FIXME: enough? */
	.period_bytes_min =	64,
	.period_bytes_max =	256 * 1024, /* FIXME: enough? */
	.periods_min =		3,
	.periods_max =		1024,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_ymfpci_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	256 * 1024, /* FIXME: enough? */
	.period_bytes_min =	64,
	.period_bytes_max =	256 * 1024, /* FIXME: enough? */
	.periods_min =		3,
	.periods_max =		1024,
	.fifo_size =		0,
};

static void snd_ymfpci_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static int snd_ymfpci_playback_open_1(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;

	ypcm = kzalloc(sizeof(*ypcm), GFP_KERNEL);
	if (ypcm == NULL)
		return -ENOMEM;
	ypcm->chip = chip;
	ypcm->type = PLAYBACK_VOICE;
	ypcm->substream = substream;
	runtime->hw = snd_ymfpci_playback;
	runtime->private_data = ypcm;
	runtime->private_free = snd_ymfpci_pcm_free_substream;
	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5333, UINT_MAX);
	return 0;
}

/* call with spinlock held */
static void ymfpci_open_extension(struct snd_ymfpci *chip)
{
	if (! chip->rear_opened) {
		if (! chip->spdif_opened) /* set AC3 */
			snd_ymfpci_writel(chip, YDSXGR_MODE,
					  snd_ymfpci_readl(chip, YDSXGR_MODE) | (1 << 30));
		/* enable second codec (4CHEN) */
		snd_ymfpci_writew(chip, YDSXGR_SECCONFIG,
				  (snd_ymfpci_readw(chip, YDSXGR_SECCONFIG) & ~0x0330) | 0x0010);
	}
}

/* call with spinlock held */
static void ymfpci_close_extension(struct snd_ymfpci *chip)
{
	if (! chip->rear_opened) {
		if (! chip->spdif_opened)
			snd_ymfpci_writel(chip, YDSXGR_MODE,
					  snd_ymfpci_readl(chip, YDSXGR_MODE) & ~(1 << 30));
		snd_ymfpci_writew(chip, YDSXGR_SECCONFIG,
				  (snd_ymfpci_readw(chip, YDSXGR_SECCONFIG) & ~0x0330) & ~0x0010);
	}
}

static int snd_ymfpci_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;
	int err;
	
	if ((err = snd_ymfpci_playback_open_1(substream)) < 0)
		return err;
	ypcm = runtime->private_data;
	ypcm->output_front = 1;
	ypcm->output_rear = chip->mode_dup4ch ? 1 : 0;
	ypcm->swap_rear = 0;
	spin_lock_irq(&chip->reg_lock);
	if (ypcm->output_rear) {
		ymfpci_open_extension(chip);
		chip->rear_opened++;
	}
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_playback_spdif_open(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;
	int err;
	
	if ((err = snd_ymfpci_playback_open_1(substream)) < 0)
		return err;
	ypcm = runtime->private_data;
	ypcm->output_front = 0;
	ypcm->output_rear = 1;
	ypcm->swap_rear = 1;
	spin_lock_irq(&chip->reg_lock);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL,
			  snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) | 2);
	ymfpci_open_extension(chip);
	chip->spdif_pcm_bits = chip->spdif_bits;
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_pcm_bits);
	chip->spdif_opened++;
	spin_unlock_irq(&chip->reg_lock);

	chip->spdif_pcm_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &chip->spdif_pcm_ctl->id);
	return 0;
}

static int snd_ymfpci_playback_4ch_open(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;
	int err;
	
	if ((err = snd_ymfpci_playback_open_1(substream)) < 0)
		return err;
	ypcm = runtime->private_data;
	ypcm->output_front = 0;
	ypcm->output_rear = 1;
	ypcm->swap_rear = 0;
	spin_lock_irq(&chip->reg_lock);
	ymfpci_open_extension(chip);
	chip->rear_opened++;
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_capture_open(struct snd_pcm_substream *substream,
				   u32 capture_bank_number)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm;

	ypcm = kzalloc(sizeof(*ypcm), GFP_KERNEL);
	if (ypcm == NULL)
		return -ENOMEM;
	ypcm->chip = chip;
	ypcm->type = capture_bank_number + CAPTURE_REC;
	ypcm->substream = substream;	
	ypcm->capture_bank_number = capture_bank_number;
	chip->capture_substream[capture_bank_number] = substream;
	runtime->hw = snd_ymfpci_capture;
	/* FIXME? True value is 256/48 = 5.33333 ms */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME, 5333, UINT_MAX);
	runtime->private_data = ypcm;
	runtime->private_free = snd_ymfpci_pcm_free_substream;
	snd_ymfpci_hw_start(chip);
	return 0;
}

static int snd_ymfpci_capture_rec_open(struct snd_pcm_substream *substream)
{
	return snd_ymfpci_capture_open(substream, 0);
}

static int snd_ymfpci_capture_ac97_open(struct snd_pcm_substream *substream)
{
	return snd_ymfpci_capture_open(substream, 1);
}

static int snd_ymfpci_playback_close_1(struct snd_pcm_substream *substream)
{
	return 0;
}

static int snd_ymfpci_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_ymfpci_pcm *ypcm = substream->runtime->private_data;

	spin_lock_irq(&chip->reg_lock);
	if (ypcm->output_rear && chip->rear_opened > 0) {
		chip->rear_opened--;
		ymfpci_close_extension(chip);
	}
	spin_unlock_irq(&chip->reg_lock);
	return snd_ymfpci_playback_close_1(substream);
}

static int snd_ymfpci_playback_spdif_close(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	chip->spdif_opened = 0;
	ymfpci_close_extension(chip);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL,
			  snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & ~2);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);
	spin_unlock_irq(&chip->reg_lock);
	chip->spdif_pcm_ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &chip->spdif_pcm_ctl->id);
	return snd_ymfpci_playback_close_1(substream);
}

static int snd_ymfpci_playback_4ch_close(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);

	spin_lock_irq(&chip->reg_lock);
	if (chip->rear_opened > 0) {
		chip->rear_opened--;
		ymfpci_close_extension(chip);
	}
	spin_unlock_irq(&chip->reg_lock);
	return snd_ymfpci_playback_close_1(substream);
}

static int snd_ymfpci_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_ymfpci *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_ymfpci_pcm *ypcm = runtime->private_data;

	if (ypcm != NULL) {
		chip->capture_substream[ypcm->capture_bank_number] = NULL;
		snd_ymfpci_hw_stop(chip);
	}
	return 0;
}

static struct snd_pcm_ops snd_ymfpci_playback_ops = {
	.open =			snd_ymfpci_playback_open,
	.close =		snd_ymfpci_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_ymfpci_playback_hw_params,
	.hw_free =		snd_ymfpci_playback_hw_free,
	.prepare =		snd_ymfpci_playback_prepare,
	.trigger =		snd_ymfpci_playback_trigger,
	.pointer =		snd_ymfpci_playback_pointer,
};

static struct snd_pcm_ops snd_ymfpci_capture_rec_ops = {
	.open =			snd_ymfpci_capture_rec_open,
	.close =		snd_ymfpci_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_ymfpci_capture_hw_params,
	.hw_free =		snd_ymfpci_capture_hw_free,
	.prepare =		snd_ymfpci_capture_prepare,
	.trigger =		snd_ymfpci_capture_trigger,
	.pointer =		snd_ymfpci_capture_pointer,
};

int __devinit snd_ymfpci_pcm(struct snd_ymfpci *chip, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI", device, 32, 1, &pcm)) < 0)
		return err;
	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ymfpci_capture_rec_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI");
	chip->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static struct snd_pcm_ops snd_ymfpci_capture_ac97_ops = {
	.open =			snd_ymfpci_capture_ac97_open,
	.close =		snd_ymfpci_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_ymfpci_capture_hw_params,
	.hw_free =		snd_ymfpci_capture_hw_free,
	.prepare =		snd_ymfpci_capture_prepare,
	.trigger =		snd_ymfpci_capture_trigger,
	.pointer =		snd_ymfpci_capture_pointer,
};

int __devinit snd_ymfpci_pcm2(struct snd_ymfpci *chip, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - PCM2", device, 0, 1, &pcm)) < 0)
		return err;
	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_ymfpci_capture_ac97_ops);

	/* global setup */
	pcm->info_flags = 0;
	sprintf(pcm->name, "YMFPCI - %s",
		chip->device_id == PCI_DEVICE_ID_YAMAHA_754 ? "Direct Recording" : "AC'97");
	chip->pcm2 = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static struct snd_pcm_ops snd_ymfpci_playback_spdif_ops = {
	.open =			snd_ymfpci_playback_spdif_open,
	.close =		snd_ymfpci_playback_spdif_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_ymfpci_playback_hw_params,
	.hw_free =		snd_ymfpci_playback_hw_free,
	.prepare =		snd_ymfpci_playback_prepare,
	.trigger =		snd_ymfpci_playback_trigger,
	.pointer =		snd_ymfpci_playback_pointer,
};

int __devinit snd_ymfpci_pcm_spdif(struct snd_ymfpci *chip, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - IEC958", device, 1, 0, &pcm)) < 0)
		return err;
	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_spdif_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI - IEC958");
	chip->pcm_spdif = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static struct snd_pcm_ops snd_ymfpci_playback_4ch_ops = {
	.open =			snd_ymfpci_playback_4ch_open,
	.close =		snd_ymfpci_playback_4ch_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_ymfpci_playback_hw_params,
	.hw_free =		snd_ymfpci_playback_hw_free,
	.prepare =		snd_ymfpci_playback_prepare,
	.trigger =		snd_ymfpci_playback_trigger,
	.pointer =		snd_ymfpci_playback_pointer,
};

int __devinit snd_ymfpci_pcm_4ch(struct snd_ymfpci *chip, int device, struct snd_pcm ** rpcm)
{
	struct snd_pcm *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;
	if ((err = snd_pcm_new(chip->card, "YMFPCI - Rear", device, 1, 0, &pcm)) < 0)
		return err;
	pcm->private_data = chip;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_ymfpci_playback_4ch_ops);

	/* global setup */
	pcm->info_flags = 0;
	strcpy(pcm->name, "YMFPCI - Rear PCM");
	chip->pcm_4ch = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci), 64*1024, 256*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static int snd_ymfpci_spdif_default_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_default_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&chip->reg_lock);
	ucontrol->value.iec958.status[0] = (chip->spdif_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (chip->spdif_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_48000;
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_spdif_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change;

	val = ((ucontrol->value.iec958.status[0] & 0x3e) << 0) |
	      (ucontrol->value.iec958.status[1] << 8);
	spin_lock_irq(&chip->reg_lock);
	change = chip->spdif_bits != val;
	chip->spdif_bits = val;
	if ((snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & 1) && chip->pcm_spdif == NULL)
		snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);
	spin_unlock_irq(&chip->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ymfpci_spdif_default __devinitdata =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_ymfpci_spdif_default_info,
	.get =		snd_ymfpci_spdif_default_get,
	.put =		snd_ymfpci_spdif_default_put
};

static int snd_ymfpci_spdif_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&chip->reg_lock);
	ucontrol->value.iec958.status[0] = 0x3e;
	ucontrol->value.iec958.status[1] = 0xff;
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static struct snd_kcontrol_new snd_ymfpci_spdif_mask __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_ymfpci_spdif_mask_info,
	.get =		snd_ymfpci_spdif_mask_get,
};

static int snd_ymfpci_spdif_stream_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_ymfpci_spdif_stream_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&chip->reg_lock);
	ucontrol->value.iec958.status[0] = (chip->spdif_pcm_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (chip->spdif_pcm_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[3] = IEC958_AES3_CON_FS_48000;
	spin_unlock_irq(&chip->reg_lock);
	return 0;
}

static int snd_ymfpci_spdif_stream_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change;

	val = ((ucontrol->value.iec958.status[0] & 0x3e) << 0) |
	      (ucontrol->value.iec958.status[1] << 8);
	spin_lock_irq(&chip->reg_lock);
	change = chip->spdif_pcm_bits != val;
	chip->spdif_pcm_bits = val;
	if ((snd_ymfpci_readw(chip, YDSXGR_SPDIFOUTCTRL) & 2))
		snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_pcm_bits);
	spin_unlock_irq(&chip->reg_lock);
	return change;
}

static struct snd_kcontrol_new snd_ymfpci_spdif_stream __devinitdata =
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_ymfpci_spdif_stream_info,
	.get =		snd_ymfpci_spdif_stream_get,
	.put =		snd_ymfpci_spdif_stream_put
};

static int snd_ymfpci_drec_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *info)
{
	static char *texts[3] = {"AC'97", "IEC958", "ZV Port"};

	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = 1;
	info->value.enumerated.items = 3;
	if (info->value.enumerated.item > 2)
		info->value.enumerated.item = 2;
	strcpy(info->value.enumerated.name, texts[info->value.enumerated.item]);
	return 0;
}

static int snd_ymfpci_drec_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *value)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	u16 reg;

	spin_lock_irq(&chip->reg_lock);
	reg = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
	spin_unlock_irq(&chip->reg_lock);
	if (!(reg & 0x100))
		value->value.enumerated.item[0] = 0;
	else
		value->value.enumerated.item[0] = 1 + ((reg & 0x200) != 0);
	return 0;
}

static int snd_ymfpci_drec_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *value)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	u16 reg, old_reg;

	spin_lock_irq(&chip->reg_lock);
	old_reg = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
	if (value->value.enumerated.item[0] == 0)
		reg = old_reg & ~0x100;
	else
		reg = (old_reg & ~0x300) | 0x100 | ((value->value.enumerated.item[0] == 2) << 9);
	snd_ymfpci_writew(chip, YDSXGR_GLOBALCTRL, reg);
	spin_unlock_irq(&chip->reg_lock);
	return reg != old_reg;
}

static struct snd_kcontrol_new snd_ymfpci_drec_source __devinitdata = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Direct Recording Source",
	.info =		snd_ymfpci_drec_source_info,
	.get =		snd_ymfpci_drec_source_get,
	.put =		snd_ymfpci_drec_source_put
};

/*
 *  Mixer controls
 */

#define YMFPCI_SINGLE(xname, xindex, reg, shift) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_ymfpci_info_single, \
  .get = snd_ymfpci_get_single, .put = snd_ymfpci_put_single, \
  .private_value = ((reg) | ((shift) << 16)) }

#define snd_ymfpci_info_single		snd_ctl_boolean_mono_info

static int snd_ymfpci_get_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xffff;
	unsigned int shift = (kcontrol->private_value >> 16) & 0xff;
	unsigned int mask = 1;
	
	switch (reg) {
	case YDSXGR_SPDIFOUTCTRL: break;
	case YDSXGR_SPDIFINCTRL: break;
	default: return -EINVAL;
	}
	ucontrol->value.integer.value[0] =
		(snd_ymfpci_readl(chip, reg) >> shift) & mask;
	return 0;
}

static int snd_ymfpci_put_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xffff;
	unsigned int shift = (kcontrol->private_value >> 16) & 0xff;
 	unsigned int mask = 1;
	int change;
	unsigned int val, oval;
	
	switch (reg) {
	case YDSXGR_SPDIFOUTCTRL: break;
	case YDSXGR_SPDIFINCTRL: break;
	default: return -EINVAL;
	}
	val = (ucontrol->value.integer.value[0] & mask);
	val <<= shift;
	spin_lock_irq(&chip->reg_lock);
	oval = snd_ymfpci_readl(chip, reg);
	val = (oval & ~(mask << shift)) | val;
	change = val != oval;
	snd_ymfpci_writel(chip, reg, val);
	spin_unlock_irq(&chip->reg_lock);
	return change;
}

static const DECLARE_TLV_DB_LINEAR(db_scale_native, TLV_DB_GAIN_MUTE, 0);

#define YMFPCI_DOUBLE(xname, xindex, reg) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
  .info = snd_ymfpci_info_double, \
  .get = snd_ymfpci_get_double, .put = snd_ymfpci_put_double, \
  .private_value = reg, \
  .tlv = { .p = db_scale_native } }

static int snd_ymfpci_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	unsigned int reg = kcontrol->private_value;

	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 16383;
	return 0;
}

static int snd_ymfpci_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int reg = kcontrol->private_value;
	unsigned int shift_left = 0, shift_right = 16, mask = 16383;
	unsigned int val;
	
	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	spin_lock_irq(&chip->reg_lock);
	val = snd_ymfpci_readl(chip, reg);
	spin_unlock_irq(&chip->reg_lock);
	ucontrol->value.integer.value[0] = (val >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (val >> shift_right) & mask;
	return 0;
}

static int snd_ymfpci_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int reg = kcontrol->private_value;
	unsigned int shift_left = 0, shift_right = 16, mask = 16383;
	int change;
	unsigned int val1, val2, oval;
	
	if (reg < 0x80 || reg >= 0xc0)
		return -EINVAL;
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	val1 <<= shift_left;
	val2 <<= shift_right;
	spin_lock_irq(&chip->reg_lock);
	oval = snd_ymfpci_readl(chip, reg);
	val1 = (oval & ~((mask << shift_left) | (mask << shift_right))) | val1 | val2;
	change = val1 != oval;
	snd_ymfpci_writel(chip, reg, val1);
	spin_unlock_irq(&chip->reg_lock);
	return change;
}

static int snd_ymfpci_put_nativedacvol(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int reg = YDSXGR_NATIVEDACOUTVOL;
	unsigned int reg2 = YDSXGR_BUF441OUTVOL;
	int change;
	unsigned int value, oval;
	
	value = ucontrol->value.integer.value[0] & 0x3fff;
	value |= (ucontrol->value.integer.value[1] & 0x3fff) << 16;
	spin_lock_irq(&chip->reg_lock);
	oval = snd_ymfpci_readl(chip, reg);
	change = value != oval;
	snd_ymfpci_writel(chip, reg, value);
	snd_ymfpci_writel(chip, reg2, value);
	spin_unlock_irq(&chip->reg_lock);
	return change;
}

/*
 * 4ch duplication
 */
#define snd_ymfpci_info_dup4ch		snd_ctl_boolean_mono_info

static int snd_ymfpci_get_dup4ch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = chip->mode_dup4ch;
	return 0;
}

static int snd_ymfpci_put_dup4ch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	int change;
	change = (ucontrol->value.integer.value[0] != chip->mode_dup4ch);
	if (change)
		chip->mode_dup4ch = !!ucontrol->value.integer.value[0];
	return change;
}


static struct snd_kcontrol_new snd_ymfpci_controls[] __devinitdata = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Wave Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_ymfpci_info_double,
	.get = snd_ymfpci_get_double,
	.put = snd_ymfpci_put_nativedacvol,
	.private_value = YDSXGR_NATIVEDACOUTVOL,
	.tlv = { .p = db_scale_native },
},
YMFPCI_DOUBLE("Wave Capture Volume", 0, YDSXGR_NATIVEDACLOOPVOL),
YMFPCI_DOUBLE("Digital Capture Volume", 0, YDSXGR_NATIVEDACINVOL),
YMFPCI_DOUBLE("Digital Capture Volume", 1, YDSXGR_NATIVEADCINVOL),
YMFPCI_DOUBLE("ADC Playback Volume", 0, YDSXGR_PRIADCOUTVOL),
YMFPCI_DOUBLE("ADC Capture Volume", 0, YDSXGR_PRIADCLOOPVOL),
YMFPCI_DOUBLE("ADC Playback Volume", 1, YDSXGR_SECADCOUTVOL),
YMFPCI_DOUBLE("ADC Capture Volume", 1, YDSXGR_SECADCLOOPVOL),
YMFPCI_DOUBLE("FM Legacy Volume", 0, YDSXGR_LEGACYOUTVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("AC97 ", PLAYBACK,VOLUME), 0, YDSXGR_ZVOUTVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("", CAPTURE,VOLUME), 0, YDSXGR_ZVLOOPVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("AC97 ",PLAYBACK,VOLUME), 1, YDSXGR_SPDIFOUTVOL),
YMFPCI_DOUBLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,VOLUME), 1, YDSXGR_SPDIFLOOPVOL),
YMFPCI_SINGLE(SNDRV_CTL_NAME_IEC958("",PLAYBACK,SWITCH), 0, YDSXGR_SPDIFOUTCTRL, 0),
YMFPCI_SINGLE(SNDRV_CTL_NAME_IEC958("",CAPTURE,SWITCH), 0, YDSXGR_SPDIFINCTRL, 0),
YMFPCI_SINGLE(SNDRV_CTL_NAME_IEC958("Loop",NONE,NONE), 0, YDSXGR_SPDIFINCTRL, 4),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "4ch Duplication",
	.info = snd_ymfpci_info_dup4ch,
	.get = snd_ymfpci_get_dup4ch,
	.put = snd_ymfpci_put_dup4ch,
},
};


/*
 * GPIO
 */

static int snd_ymfpci_get_gpio_out(struct snd_ymfpci *chip, int pin)
{
	u16 reg, mode;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	reg = snd_ymfpci_readw(chip, YDSXGR_GPIOFUNCENABLE);
	reg &= ~(1 << (pin + 8));
	reg |= (1 << pin);
	snd_ymfpci_writew(chip, YDSXGR_GPIOFUNCENABLE, reg);
	/* set the level mode for input line */
	mode = snd_ymfpci_readw(chip, YDSXGR_GPIOTYPECONFIG);
	mode &= ~(3 << (pin * 2));
	snd_ymfpci_writew(chip, YDSXGR_GPIOTYPECONFIG, mode);
	snd_ymfpci_writew(chip, YDSXGR_GPIOFUNCENABLE, reg | (1 << (pin + 8)));
	mode = snd_ymfpci_readw(chip, YDSXGR_GPIOINSTATUS);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return (mode >> pin) & 1;
}

static int snd_ymfpci_set_gpio_out(struct snd_ymfpci *chip, int pin, int enable)
{
	u16 reg;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	reg = snd_ymfpci_readw(chip, YDSXGR_GPIOFUNCENABLE);
	reg &= ~(1 << pin);
	reg &= ~(1 << (pin + 8));
	snd_ymfpci_writew(chip, YDSXGR_GPIOFUNCENABLE, reg);
	snd_ymfpci_writew(chip, YDSXGR_GPIOOUTCTRL, enable << pin);
	snd_ymfpci_writew(chip, YDSXGR_GPIOFUNCENABLE, reg | (1 << (pin + 8)));
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return 0;
}

#define snd_ymfpci_gpio_sw_info		snd_ctl_boolean_mono_info

static int snd_ymfpci_gpio_sw_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	int pin = (int)kcontrol->private_value;
	ucontrol->value.integer.value[0] = snd_ymfpci_get_gpio_out(chip, pin);
	return 0;
}

static int snd_ymfpci_gpio_sw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	int pin = (int)kcontrol->private_value;

	if (snd_ymfpci_get_gpio_out(chip, pin) != ucontrol->value.integer.value[0]) {
		snd_ymfpci_set_gpio_out(chip, pin, !!ucontrol->value.integer.value[0]);
		ucontrol->value.integer.value[0] = snd_ymfpci_get_gpio_out(chip, pin);
		return 1;
	}
	return 0;
}

static struct snd_kcontrol_new snd_ymfpci_rear_shared __devinitdata = {
	.name = "Shared Rear/Line-In Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_ymfpci_gpio_sw_info,
	.get = snd_ymfpci_gpio_sw_get,
	.put = snd_ymfpci_gpio_sw_put,
	.private_value = 2,
};

/*
 * PCM voice volume
 */

static int snd_ymfpci_pcm_vol_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0x8000;
	return 0;
}

static int snd_ymfpci_pcm_vol_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int subs = kcontrol->id.subdevice;

	ucontrol->value.integer.value[0] = chip->pcm_mixer[subs].left;
	ucontrol->value.integer.value[1] = chip->pcm_mixer[subs].right;
	return 0;
}

static int snd_ymfpci_pcm_vol_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ymfpci *chip = snd_kcontrol_chip(kcontrol);
	unsigned int subs = kcontrol->id.subdevice;
	struct snd_pcm_substream *substream;
	unsigned long flags;

	if (ucontrol->value.integer.value[0] != chip->pcm_mixer[subs].left ||
	    ucontrol->value.integer.value[1] != chip->pcm_mixer[subs].right) {
		chip->pcm_mixer[subs].left = ucontrol->value.integer.value[0];
		chip->pcm_mixer[subs].right = ucontrol->value.integer.value[1];
		if (chip->pcm_mixer[subs].left > 0x8000)
			chip->pcm_mixer[subs].left = 0x8000;
		if (chip->pcm_mixer[subs].right > 0x8000)
			chip->pcm_mixer[subs].right = 0x8000;

		substream = (struct snd_pcm_substream *)kcontrol->private_value;
		spin_lock_irqsave(&chip->voice_lock, flags);
		if (substream->runtime && substream->runtime->private_data) {
			struct snd_ymfpci_pcm *ypcm = substream->runtime->private_data;
			if (!ypcm->use_441_slot)
				ypcm->update_pcm_vol = 2;
		}
		spin_unlock_irqrestore(&chip->voice_lock, flags);
		return 1;
	}
	return 0;
}

static struct snd_kcontrol_new snd_ymfpci_pcm_volume __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "PCM Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.info = snd_ymfpci_pcm_vol_info,
	.get = snd_ymfpci_pcm_vol_get,
	.put = snd_ymfpci_pcm_vol_put,
};


/*
 *  Mixer routines
 */

static void snd_ymfpci_mixer_free_ac97_bus(struct snd_ac97_bus *bus)
{
	struct snd_ymfpci *chip = bus->private_data;
	chip->ac97_bus = NULL;
}

static void snd_ymfpci_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct snd_ymfpci *chip = ac97->private_data;
	chip->ac97 = NULL;
}

int __devinit snd_ymfpci_mixer(struct snd_ymfpci *chip, int rear_switch)
{
	struct snd_ac97_template ac97;
	struct snd_kcontrol *kctl;
	struct snd_pcm_substream *substream;
	unsigned int idx;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.write = snd_ymfpci_codec_write,
		.read = snd_ymfpci_codec_read,
	};

	if ((err = snd_ac97_bus(chip->card, 0, &ops, chip, &chip->ac97_bus)) < 0)
		return err;
	chip->ac97_bus->private_free = snd_ymfpci_mixer_free_ac97_bus;
	chip->ac97_bus->no_vra = 1; /* YMFPCI doesn't need VRA */

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_ymfpci_mixer_free_ac97;
	if ((err = snd_ac97_mixer(chip->ac97_bus, &ac97, &chip->ac97)) < 0)
		return err;

	/* to be sure */
	snd_ac97_update_bits(chip->ac97, AC97_EXTENDED_STATUS,
			     AC97_EA_VRA|AC97_EA_VRM, 0);

	for (idx = 0; idx < ARRAY_SIZE(snd_ymfpci_controls); idx++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_ymfpci_controls[idx], chip))) < 0)
			return err;
	}

	/* add S/PDIF control */
	snd_assert(chip->pcm_spdif != NULL, return -EIO);
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_default, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_mask, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	if ((err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_spdif_stream, chip))) < 0)
		return err;
	kctl->id.device = chip->pcm_spdif->device;
	chip->spdif_pcm_ctl = kctl;

	/* direct recording source */
	if (chip->device_id == PCI_DEVICE_ID_YAMAHA_754 &&
	    (err = snd_ctl_add(chip->card, kctl = snd_ctl_new1(&snd_ymfpci_drec_source, chip))) < 0)
		return err;

	/*
	 * shared rear/line-in
	 */
	if (rear_switch) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_ymfpci_rear_shared, chip))) < 0)
			return err;
	}

	/* per-voice volume */
	substream = chip->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	for (idx = 0; idx < 32; ++idx) {
		kctl = snd_ctl_new1(&snd_ymfpci_pcm_volume, chip);
		if (!kctl)
			return -ENOMEM;
		kctl->id.device = chip->pcm->device;
		kctl->id.subdevice = idx;
		kctl->private_value = (unsigned long)substream;
		if ((err = snd_ctl_add(chip->card, kctl)) < 0)
			return err;
		chip->pcm_mixer[idx].left = 0x8000;
		chip->pcm_mixer[idx].right = 0x8000;
		chip->pcm_mixer[idx].ctl = kctl;
		substream = substream->next;
	}

	return 0;
}


/*
 * timer
 */

static int snd_ymfpci_timer_start(struct snd_timer *timer)
{
	struct snd_ymfpci *chip;
	unsigned long flags;
	unsigned int count;

	chip = snd_timer_chip(timer);
	count = (timer->sticks << 1) - 1;
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writew(chip, YDSXGR_TIMERCOUNT, count);
	snd_ymfpci_writeb(chip, YDSXGR_TIMERCTRL, 0x03);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ymfpci_timer_stop(struct snd_timer *timer)
{
	struct snd_ymfpci *chip;
	unsigned long flags;

	chip = snd_timer_chip(timer);
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_ymfpci_writeb(chip, YDSXGR_TIMERCTRL, 0x00);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

static int snd_ymfpci_timer_precise_resolution(struct snd_timer *timer,
					       unsigned long *num, unsigned long *den)
{
	*num = 1;
	*den = 48000;
	return 0;
}

static struct snd_timer_hardware snd_ymfpci_timer_hw = {
	.flags = SNDRV_TIMER_HW_AUTO,
	.resolution = 20833, /* 1/fs = 20.8333...us */
	.ticks = 0x8000,
	.start = snd_ymfpci_timer_start,
	.stop = snd_ymfpci_timer_stop,
	.precise_resolution = snd_ymfpci_timer_precise_resolution,
};

int __devinit snd_ymfpci_timer(struct snd_ymfpci *chip, int device)
{
	struct snd_timer *timer = NULL;
	struct snd_timer_id tid;
	int err;

	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = chip->card->number;
	tid.device = device;
	tid.subdevice = 0;
	if ((err = snd_timer_new(chip->card, "YMFPCI", &tid, &timer)) >= 0) {
		strcpy(timer->name, "YMFPCI timer");
		timer->private_data = chip;
		timer->hw = snd_ymfpci_timer_hw;
	}
	chip->timer = timer;
	return err;
}


/*
 *  proc interface
 */

static void snd_ymfpci_proc_read(struct snd_info_entry *entry, 
				 struct snd_info_buffer *buffer)
{
	struct snd_ymfpci *chip = entry->private_data;
	int i;
	
	snd_iprintf(buffer, "YMFPCI\n\n");
	for (i = 0; i <= YDSXGR_WORKBASE; i += 4)
		snd_iprintf(buffer, "%04x: %04x\n", i, snd_ymfpci_readl(chip, i));
}

static int __devinit snd_ymfpci_proc_init(struct snd_card *card, struct snd_ymfpci *chip)
{
	struct snd_info_entry *entry;
	
	if (! snd_card_proc_new(card, "ymfpci", &entry))
		snd_info_set_text_ops(entry, chip, snd_ymfpci_proc_read);
	return 0;
}

/*
 *  initialization routines
 */

static void snd_ymfpci_aclink_reset(struct pci_dev * pci)
{
	u8 cmd;

	pci_read_config_byte(pci, PCIR_DSXG_CTRL, &cmd);
#if 0 // force to reset
	if (cmd & 0x03) {
#endif
		pci_write_config_byte(pci, PCIR_DSXG_CTRL, cmd & 0xfc);
		pci_write_config_byte(pci, PCIR_DSXG_CTRL, cmd | 0x03);
		pci_write_config_byte(pci, PCIR_DSXG_CTRL, cmd & 0xfc);
		pci_write_config_word(pci, PCIR_DSXG_PWRCTRL1, 0);
		pci_write_config_word(pci, PCIR_DSXG_PWRCTRL2, 0);
#if 0
	}
#endif
}

static void snd_ymfpci_enable_dsp(struct snd_ymfpci *chip)
{
	snd_ymfpci_writel(chip, YDSXGR_CONFIG, 0x00000001);
}

static void snd_ymfpci_disable_dsp(struct snd_ymfpci *chip)
{
	u32 val;
	int timeout = 1000;

	val = snd_ymfpci_readl(chip, YDSXGR_CONFIG);
	if (val)
		snd_ymfpci_writel(chip, YDSXGR_CONFIG, 0x00000000);
	while (timeout-- > 0) {
		val = snd_ymfpci_readl(chip, YDSXGR_STATUS);
		if ((val & 0x00000002) == 0)
			break;
	}
}

#ifdef CONFIG_SND_YMFPCI_FIRMWARE_IN_KERNEL

#include "ymfpci_image.h"

static struct firmware snd_ymfpci_dsp_microcode = {
	.size = YDSXG_DSPLENGTH,
	.data = (u8 *)DspInst,
};
static struct firmware snd_ymfpci_controller_microcode = {
	.size = YDSXG_CTRLLENGTH,
	.data = (u8 *)CntrlInst,
};
static struct firmware snd_ymfpci_controller_1e_microcode = {
	.size = YDSXG_CTRLLENGTH,
	.data = (u8 *)CntrlInst1E,
};
#endif

#ifdef CONFIG_SND_YMFPCI_FIRMWARE_IN_KERNEL
static int snd_ymfpci_request_firmware(struct snd_ymfpci *chip)
{
	chip->dsp_microcode = &snd_ymfpci_dsp_microcode;
	if (chip->device_id == PCI_DEVICE_ID_YAMAHA_724F ||
	    chip->device_id == PCI_DEVICE_ID_YAMAHA_740C ||
	    chip->device_id == PCI_DEVICE_ID_YAMAHA_744 ||
	    chip->device_id == PCI_DEVICE_ID_YAMAHA_754)
		chip->controller_microcode =
			&snd_ymfpci_controller_1e_microcode;
	else
		chip->controller_microcode =
			&snd_ymfpci_controller_microcode;
	return 0;
}

#else /* use fw_loader */

#ifdef __LITTLE_ENDIAN
static inline void snd_ymfpci_convert_from_le(const struct firmware *fw) { }
#else
static void snd_ymfpci_convert_from_le(const struct firmware *fw)
{
	int i;
	u32 *data = (u32 *)fw->data;

	for (i = 0; i < fw->size / 4; ++i)
		le32_to_cpus(&data[i]);
}
#endif

static int snd_ymfpci_request_firmware(struct snd_ymfpci *chip)
{
	int err, is_1e;
	const char *name;

	err = request_firmware(&chip->dsp_microcode, "yamaha/ds1_dsp.fw",
			       &chip->pci->dev);
	if (err >= 0) {
		if (chip->dsp_microcode->size == YDSXG_DSPLENGTH)
			snd_ymfpci_convert_from_le(chip->dsp_microcode);
		else {
			snd_printk(KERN_ERR "DSP microcode has wrong size\n");
			err = -EINVAL;
		}
	}
	if (err < 0)
		return err;
	is_1e = chip->device_id == PCI_DEVICE_ID_YAMAHA_724F ||
		chip->device_id == PCI_DEVICE_ID_YAMAHA_740C ||
		chip->device_id == PCI_DEVICE_ID_YAMAHA_744 ||
		chip->device_id == PCI_DEVICE_ID_YAMAHA_754;
	name = is_1e ? "yamaha/ds1e_ctrl.fw" : "yamaha/ds1_ctrl.fw";
	err = request_firmware(&chip->controller_microcode, name,
			       &chip->pci->dev);
	if (err >= 0) {
		if (chip->controller_microcode->size == YDSXG_CTRLLENGTH)
			snd_ymfpci_convert_from_le(chip->controller_microcode);
		else {
			snd_printk(KERN_ERR "controller microcode"
				   " has wrong size\n");
			err = -EINVAL;
		}
	}
	if (err < 0)
		return err;
	return 0;
}

MODULE_FIRMWARE("yamaha/ds1_dsp.fw");
MODULE_FIRMWARE("yamaha/ds1_ctrl.fw");
MODULE_FIRMWARE("yamaha/ds1e_ctrl.fw");

#endif

static void snd_ymfpci_download_image(struct snd_ymfpci *chip)
{
	int i;
	u16 ctrl;
	u32 *inst;

	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0x00000000);
	snd_ymfpci_disable_dsp(chip);
	snd_ymfpci_writel(chip, YDSXGR_MODE, 0x00010000);
	snd_ymfpci_writel(chip, YDSXGR_MODE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFREC, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_MAPOFEFFECT, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, 0x00000000);
	snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, 0x00000000);
	ctrl = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
	snd_ymfpci_writew(chip, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);

	/* setup DSP instruction code */
	inst = (u32 *)chip->dsp_microcode->data;
	for (i = 0; i < YDSXG_DSPLENGTH / 4; i++)
		snd_ymfpci_writel(chip, YDSXGR_DSPINSTRAM + (i << 2), inst[i]);

	/* setup control instruction code */
	inst = (u32 *)chip->controller_microcode->data;
	for (i = 0; i < YDSXG_CTRLLENGTH / 4; i++)
		snd_ymfpci_writel(chip, YDSXGR_CTRLINSTRAM + (i << 2), inst[i]);

	snd_ymfpci_enable_dsp(chip);
}

static int __devinit snd_ymfpci_memalloc(struct snd_ymfpci *chip)
{
	long size, playback_ctrl_size;
	int voice, bank, reg;
	u8 *ptr;
	dma_addr_t ptr_addr;

	playback_ctrl_size = 4 + 4 * YDSXG_PLAYBACK_VOICES;
	chip->bank_size_playback = snd_ymfpci_readl(chip, YDSXGR_PLAYCTRLSIZE) << 2;
	chip->bank_size_capture = snd_ymfpci_readl(chip, YDSXGR_RECCTRLSIZE) << 2;
	chip->bank_size_effect = snd_ymfpci_readl(chip, YDSXGR_EFFCTRLSIZE) << 2;
	chip->work_size = YDSXG_DEFAULT_WORK_SIZE;
	
	size = ALIGN(playback_ctrl_size, 0x100) +
	       ALIGN(chip->bank_size_playback * 2 * YDSXG_PLAYBACK_VOICES, 0x100) +
	       ALIGN(chip->bank_size_capture * 2 * YDSXG_CAPTURE_VOICES, 0x100) +
	       ALIGN(chip->bank_size_effect * 2 * YDSXG_EFFECT_VOICES, 0x100) +
	       chip->work_size;
	/* work_ptr must be aligned to 256 bytes, but it's already
	   covered with the kernel page allocation mechanism */
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(chip->pci),
				size, &chip->work_ptr) < 0) 
		return -ENOMEM;
	ptr = chip->work_ptr.area;
	ptr_addr = chip->work_ptr.addr;
	memset(ptr, 0, size);	/* for sure */

	chip->bank_base_playback = ptr;
	chip->bank_base_playback_addr = ptr_addr;
	chip->ctrl_playback = (u32 *)ptr;
	chip->ctrl_playback[0] = cpu_to_le32(YDSXG_PLAYBACK_VOICES);
	ptr += ALIGN(playback_ctrl_size, 0x100);
	ptr_addr += ALIGN(playback_ctrl_size, 0x100);
	for (voice = 0; voice < YDSXG_PLAYBACK_VOICES; voice++) {
		chip->voices[voice].number = voice;
		chip->voices[voice].bank = (struct snd_ymfpci_playback_bank *)ptr;
		chip->voices[voice].bank_addr = ptr_addr;
		for (bank = 0; bank < 2; bank++) {
			chip->bank_playback[voice][bank] = (struct snd_ymfpci_playback_bank *)ptr;
			ptr += chip->bank_size_playback;
			ptr_addr += chip->bank_size_playback;
		}
	}
	ptr = (char *)ALIGN((unsigned long)ptr, 0x100);
	ptr_addr = ALIGN(ptr_addr, 0x100);
	chip->bank_base_capture = ptr;
	chip->bank_base_capture_addr = ptr_addr;
	for (voice = 0; voice < YDSXG_CAPTURE_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			chip->bank_capture[voice][bank] = (struct snd_ymfpci_capture_bank *)ptr;
			ptr += chip->bank_size_capture;
			ptr_addr += chip->bank_size_capture;
		}
	ptr = (char *)ALIGN((unsigned long)ptr, 0x100);
	ptr_addr = ALIGN(ptr_addr, 0x100);
	chip->bank_base_effect = ptr;
	chip->bank_base_effect_addr = ptr_addr;
	for (voice = 0; voice < YDSXG_EFFECT_VOICES; voice++)
		for (bank = 0; bank < 2; bank++) {
			chip->bank_effect[voice][bank] = (struct snd_ymfpci_effect_bank *)ptr;
			ptr += chip->bank_size_effect;
			ptr_addr += chip->bank_size_effect;
		}
	ptr = (char *)ALIGN((unsigned long)ptr, 0x100);
	ptr_addr = ALIGN(ptr_addr, 0x100);
	chip->work_base = ptr;
	chip->work_base_addr = ptr_addr;
	
	snd_assert(ptr + chip->work_size == chip->work_ptr.area + chip->work_ptr.bytes, );

	snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, chip->bank_base_playback_addr);
	snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, chip->bank_base_capture_addr);
	snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, chip->bank_base_effect_addr);
	snd_ymfpci_writel(chip, YDSXGR_WORKBASE, chip->work_base_addr);
	snd_ymfpci_writel(chip, YDSXGR_WORKSIZE, chip->work_size >> 2);

	/* S/PDIF output initialization */
	chip->spdif_bits = chip->spdif_pcm_bits = SNDRV_PCM_DEFAULT_CON_SPDIF & 0xffff;
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTCTRL, 0);
	snd_ymfpci_writew(chip, YDSXGR_SPDIFOUTSTATUS, chip->spdif_bits);

	/* S/PDIF input initialization */
	snd_ymfpci_writew(chip, YDSXGR_SPDIFINCTRL, 0);

	/* digital mixer setup */
	for (reg = 0x80; reg < 0xc0; reg += 4)
		snd_ymfpci_writel(chip, reg, 0);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_ZVOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_SPDIFOUTVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEADCINVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACINVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_PRIADCLOOPVOL, 0x3fff3fff);
	snd_ymfpci_writel(chip, YDSXGR_LEGACYOUTVOL, 0x3fff3fff);
	
	return 0;
}

static int snd_ymfpci_free(struct snd_ymfpci *chip)
{
	u16 ctrl;

	snd_assert(chip != NULL, return -EINVAL);

	if (chip->res_reg_area) {	/* don't touch busy hardware */
		snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0);
		snd_ymfpci_writel(chip, YDSXGR_BUF441OUTVOL, 0);
		snd_ymfpci_writel(chip, YDSXGR_LEGACYOUTVOL, 0);
		snd_ymfpci_writel(chip, YDSXGR_STATUS, ~0);
		snd_ymfpci_disable_dsp(chip);
		snd_ymfpci_writel(chip, YDSXGR_PLAYCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_RECCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_EFFCTRLBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_WORKBASE, 0);
		snd_ymfpci_writel(chip, YDSXGR_WORKSIZE, 0);
		ctrl = snd_ymfpci_readw(chip, YDSXGR_GLOBALCTRL);
		snd_ymfpci_writew(chip, YDSXGR_GLOBALCTRL, ctrl & ~0x0007);
	}

	snd_ymfpci_ac3_done(chip);

	/* Set PCI device to D3 state */
#if 0
	/* FIXME: temporarily disabled, otherwise we cannot fire up
	 * the chip again unless reboot.  ACPI bug?
	 */
	pci_set_power_state(chip->pci, 3);
#endif

#ifdef CONFIG_PM
	vfree(chip->saved_regs);
#endif
	release_and_free_resource(chip->mpu_res);
	release_and_free_resource(chip->fm_res);
	snd_ymfpci_free_gameport(chip);
	if (chip->reg_area_virt)
		iounmap(chip->reg_area_virt);
	if (chip->work_ptr.area)
		snd_dma_free_pages(&chip->work_ptr);
	
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	release_and_free_resource(chip->res_reg_area);

	pci_write_config_word(chip->pci, 0x40, chip->old_legacy_ctrl);
	
	pci_disable_device(chip->pci);
#ifndef CONFIG_SND_YMFPCI_FIRMWARE_IN_KERNEL
	release_firmware(chip->dsp_microcode);
	release_firmware(chip->controller_microcode);
#endif
	kfree(chip);
	return 0;
}

static int snd_ymfpci_dev_free(struct snd_device *device)
{
	struct snd_ymfpci *chip = device->device_data;
	return snd_ymfpci_free(chip);
}

#ifdef CONFIG_PM
static int saved_regs_index[] = {
	/* spdif */
	YDSXGR_SPDIFOUTCTRL,
	YDSXGR_SPDIFOUTSTATUS,
	YDSXGR_SPDIFINCTRL,
	/* volumes */
	YDSXGR_PRIADCLOOPVOL,
	YDSXGR_NATIVEDACINVOL,
	YDSXGR_NATIVEDACOUTVOL,
	YDSXGR_BUF441OUTVOL,
	YDSXGR_NATIVEADCINVOL,
	YDSXGR_SPDIFLOOPVOL,
	YDSXGR_SPDIFOUTVOL,
	YDSXGR_ZVOUTVOL,
	YDSXGR_LEGACYOUTVOL,
	/* address bases */
	YDSXGR_PLAYCTRLBASE,
	YDSXGR_RECCTRLBASE,
	YDSXGR_EFFCTRLBASE,
	YDSXGR_WORKBASE,
	/* capture set up */
	YDSXGR_MAPOFREC,
	YDSXGR_RECFORMAT,
	YDSXGR_RECSLOTSR,
	YDSXGR_ADCFORMAT,
	YDSXGR_ADCSLOTSR,
};
#define YDSXGR_NUM_SAVED_REGS	ARRAY_SIZE(saved_regs_index)

int snd_ymfpci_suspend(struct pci_dev *pci, pm_message_t state)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_ymfpci *chip = card->private_data;
	unsigned int i;
	
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_pcm_suspend_all(chip->pcm2);
	snd_pcm_suspend_all(chip->pcm_spdif);
	snd_pcm_suspend_all(chip->pcm_4ch);
	snd_ac97_suspend(chip->ac97);
	for (i = 0; i < YDSXGR_NUM_SAVED_REGS; i++)
		chip->saved_regs[i] = snd_ymfpci_readl(chip, saved_regs_index[i]);
	chip->saved_ydsxgr_mode = snd_ymfpci_readl(chip, YDSXGR_MODE);
	snd_ymfpci_writel(chip, YDSXGR_NATIVEDACOUTVOL, 0);
	snd_ymfpci_disable_dsp(chip);
	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, pci_choose_state(pci, state));
	return 0;
}

int snd_ymfpci_resume(struct pci_dev *pci)
{
	struct snd_card *card = pci_get_drvdata(pci);
	struct snd_ymfpci *chip = card->private_data;
	unsigned int i;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "ymfpci: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);
	snd_ymfpci_aclink_reset(pci);
	snd_ymfpci_codec_ready(chip, 0);
	snd_ymfpci_download_image(chip);
	udelay(100);

	for (i = 0; i < YDSXGR_NUM_SAVED_REGS; i++)
		snd_ymfpci_writel(chip, saved_regs_index[i], chip->saved_regs[i]);

	snd_ac97_resume(chip->ac97);

	/* start hw again */
	if (chip->start_count > 0) {
		spin_lock_irq(&chip->reg_lock);
		snd_ymfpci_writel(chip, YDSXGR_MODE, chip->saved_ydsxgr_mode);
		chip->active_bank = snd_ymfpci_readl(chip, YDSXGR_CTRLSELECT);
		spin_unlock_irq(&chip->reg_lock);
	}
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM */

int __devinit snd_ymfpci_create(struct snd_card *card,
				struct pci_dev * pci,
				unsigned short old_legacy_ctrl,
				struct snd_ymfpci ** rchip)
{
	struct snd_ymfpci *chip;
	int err;
	static struct snd_device_ops ops = {
		.dev_free =	snd_ymfpci_dev_free,
	};
	
	*rchip = NULL;

	/* enable PCI device */
	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	chip->old_legacy_ctrl = old_legacy_ctrl;
	spin_lock_init(&chip->reg_lock);
	spin_lock_init(&chip->voice_lock);
	init_waitqueue_head(&chip->interrupt_sleep);
	atomic_set(&chip->interrupt_sleep_count, 0);
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;
	chip->device_id = pci->device;
	chip->rev = pci->revision;
	chip->reg_area_phys = pci_resource_start(pci, 0);
	chip->reg_area_virt = ioremap_nocache(chip->reg_area_phys, 0x8000);
	pci_set_master(pci);
	chip->src441_used = -1;

	if ((chip->res_reg_area = request_mem_region(chip->reg_area_phys, 0x8000, "YMFPCI")) == NULL) {
		snd_printk(KERN_ERR "unable to grab memory region 0x%lx-0x%lx\n", chip->reg_area_phys, chip->reg_area_phys + 0x8000 - 1);
		snd_ymfpci_free(chip);
		return -EBUSY;
	}
	if (request_irq(pci->irq, snd_ymfpci_interrupt, IRQF_SHARED,
			"YMFPCI", chip)) {
		snd_printk(KERN_ERR "unable to grab IRQ %d\n", pci->irq);
		snd_ymfpci_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	snd_ymfpci_aclink_reset(pci);
	if (snd_ymfpci_codec_ready(chip, 0) < 0) {
		snd_ymfpci_free(chip);
		return -EIO;
	}

	err = snd_ymfpci_request_firmware(chip);
	if (err < 0) {
		snd_printk(KERN_ERR "firmware request failed: %d\n", err);
		snd_ymfpci_free(chip);
		return err;
	}
	snd_ymfpci_download_image(chip);

	udelay(100); /* seems we need a delay after downloading image.. */

	if (snd_ymfpci_memalloc(chip) < 0) {
		snd_ymfpci_free(chip);
		return -EIO;
	}

	if ((err = snd_ymfpci_ac3_init(chip)) < 0) {
		snd_ymfpci_free(chip);
		return err;
	}

#ifdef CONFIG_PM
	chip->saved_regs = vmalloc(YDSXGR_NUM_SAVED_REGS * sizeof(u32));
	if (chip->saved_regs == NULL) {
		snd_ymfpci_free(chip);
		return -ENOMEM;
	}
#endif

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_ymfpci_free(chip);
		return err;
	}

	snd_ymfpci_proc_init(card, chip);

	snd_card_set_dev(card, &pci->dev);

	*rchip = chip;
	return 0;
}
