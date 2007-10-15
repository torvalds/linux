/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com) and to datasheets
 *  for specific codecs.
 *
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

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/control.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include "ac97_id.h"
#include "ac97_local.h"

/*
 *  PCM support
 */

static unsigned char rate_reg_tables[2][4][9] = {
{
  /* standard rates */
  {
  	/* 3&4 front, 7&8 rear, 6&9 center/lfe */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 3 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 6 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 7 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 8 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
  	/* 7&8 front, 6&9 rear, 10&11 center/lfe */
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 6 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 7 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 8 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 9 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 10 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 6&9 front, 10&11 rear, 3&4 center/lfe */
	AC97_PCM_LFE_DAC_RATE,		/* slot 3 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 9 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 10 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 11 */
  },
  {
  	/* 10&11 front, 3&4 rear, 7&8 center/lfe */
	AC97_PCM_SURR_DAC_RATE,		/* slot 3 */
	AC97_PCM_SURR_DAC_RATE,		/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 7 */
	AC97_PCM_LFE_DAC_RATE,		/* slot 8 */
	0xff,				/* slot 9 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 10 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 11 */
  },
},
{
  /* double rates */
  {
  	/* 3&4 front, 7&8 front (t+1) */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 3 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 7 */
	AC97_PCM_FRONT_DAC_RATE,	/* slot 8 */
	0xff,				/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
	/* not specified in the specification */
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	0xff,				/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	0xff,				/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  },
  {
	0xff,				/* slot 3 */
	0xff,				/* slot 4 */
	0xff,				/* slot 5 */
	0xff,				/* slot 6 */
	0xff,				/* slot 7 */
	0xff,				/* slot 8 */
	0xff,				/* slot 9 */
	0xff,				/* slot 10 */
	0xff,				/* slot 11 */
  }
}};

/* FIXME: more various mappings for ADC? */
static unsigned char rate_cregs[9] = {
	AC97_PCM_LR_ADC_RATE,	/* 3 */
	AC97_PCM_LR_ADC_RATE,	/* 4 */
	0xff,			/* 5 */
	AC97_PCM_MIC_ADC_RATE,	/* 6 */
	0xff,			/* 7 */
	0xff,			/* 8 */
	0xff,			/* 9 */
	0xff,			/* 10 */
	0xff,			/* 11 */
};

static unsigned char get_slot_reg(struct ac97_pcm *pcm, unsigned short cidx,
				  unsigned short slot, int dbl)
{
	if (slot < 3)
		return 0xff;
	if (slot > 11)
		return 0xff;
	if (pcm->spdif)
		return AC97_SPDIF; /* pseudo register */
	if (pcm->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return rate_reg_tables[dbl][pcm->r[dbl].rate_table[cidx]][slot - 3];
	else
		return rate_cregs[slot - 3];
}

static int set_spdif_rate(struct snd_ac97 *ac97, unsigned short rate)
{
	unsigned short old, bits, reg, mask;
	unsigned int sbits;

	if (! (ac97->ext_id & AC97_EI_SPDIF))
		return -ENODEV;

	/* TODO: double rate support */
	if (ac97->flags & AC97_CS_SPDIF) {
		switch (rate) {
		case 48000: bits = 0; break;
		case 44100: bits = 1 << AC97_SC_SPSR_SHIFT; break;
		default: /* invalid - disable output */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		reg = AC97_CSR_SPDIF;
		mask = 1 << AC97_SC_SPSR_SHIFT;
	} else {
		if (ac97->id == AC97_ID_CM9739 && rate != 48000) {
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		switch (rate) {
		case 44100: bits = AC97_SC_SPSR_44K; break;
		case 48000: bits = AC97_SC_SPSR_48K; break;
		case 32000: bits = AC97_SC_SPSR_32K; break;
		default: /* invalid - disable output */
			snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
			return -EINVAL;
		}
		reg = AC97_SPDIF;
		mask = AC97_SC_SPSR_MASK;
	}

	mutex_lock(&ac97->reg_mutex);
	old = snd_ac97_read(ac97, reg) & mask;
	if (old != bits) {
		snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, 0);
		snd_ac97_update_bits_nolock(ac97, reg, mask, bits);
		/* update the internal spdif bits */
		sbits = ac97->spdif_status;
		if (sbits & IEC958_AES0_PROFESSIONAL) {
			sbits &= ~IEC958_AES0_PRO_FS;
			switch (rate) {
			case 44100: sbits |= IEC958_AES0_PRO_FS_44100; break;
			case 48000: sbits |= IEC958_AES0_PRO_FS_48000; break;
			case 32000: sbits |= IEC958_AES0_PRO_FS_32000; break;
			}
		} else {
			sbits &= ~(IEC958_AES3_CON_FS << 24);
			switch (rate) {
			case 44100: sbits |= IEC958_AES3_CON_FS_44100<<24; break;
			case 48000: sbits |= IEC958_AES3_CON_FS_48000<<24; break;
			case 32000: sbits |= IEC958_AES3_CON_FS_32000<<24; break;
			}
		}
		ac97->spdif_status = sbits;
	}
	snd_ac97_update_bits_nolock(ac97, AC97_EXTENDED_STATUS, AC97_EA_SPDIF, AC97_EA_SPDIF);
	mutex_unlock(&ac97->reg_mutex);
	return 0;
}

/**
 * snd_ac97_set_rate - change the rate of the given input/output.
 * @ac97: the ac97 instance
 * @reg: the register to change
 * @rate: the sample rate to set
 *
 * Changes the rate of the given input/output on the codec.
 * If the codec doesn't support VAR, the rate must be 48000 (except
 * for SPDIF).
 *
 * The valid registers are AC97_PMC_MIC_ADC_RATE,
 * AC97_PCM_FRONT_DAC_RATE, AC97_PCM_LR_ADC_RATE.
 * AC97_PCM_SURR_DAC_RATE and AC97_PCM_LFE_DAC_RATE are accepted
 * if the codec supports them.
 * AC97_SPDIF is accepted as a pseudo register to modify the SPDIF
 * status bits.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ac97_set_rate(struct snd_ac97 *ac97, int reg, unsigned int rate)
{
	int dbl;
	unsigned int tmp;
	
	dbl = rate > 48000;
	if (dbl) {
		if (!(ac97->flags & AC97_DOUBLE_RATE))
			return -EINVAL;
		if (reg != AC97_PCM_FRONT_DAC_RATE)
			return -EINVAL;
	}

	snd_ac97_update_power(ac97, reg, 1);
	switch (reg) {
	case AC97_PCM_MIC_ADC_RATE:
		if ((ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_VRM) == 0)	/* MIC VRA */
			if (rate != 48000)
				return -EINVAL;
		break;
	case AC97_PCM_FRONT_DAC_RATE:
	case AC97_PCM_LR_ADC_RATE:
		if ((ac97->regs[AC97_EXTENDED_STATUS] & AC97_EA_VRA) == 0)	/* VRA */
			if (rate != 48000 && rate != 96000)
				return -EINVAL;
		break;
	case AC97_PCM_SURR_DAC_RATE:
		if (! (ac97->scaps & AC97_SCAP_SURROUND_DAC))
			return -EINVAL;
		break;
	case AC97_PCM_LFE_DAC_RATE:
		if (! (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
			return -EINVAL;
		break;
	case AC97_SPDIF:
		/* special case */
		return set_spdif_rate(ac97, rate);
	default:
		return -EINVAL;
	}
	if (dbl)
		rate /= 2;
	tmp = (rate * ac97->bus->clock) / 48000;
	if (tmp > 65535)
		return -EINVAL;
	if ((ac97->ext_id & AC97_EI_DRA) && reg == AC97_PCM_FRONT_DAC_RATE)
		snd_ac97_update_bits(ac97, AC97_EXTENDED_STATUS,
				     AC97_EA_DRA, dbl ? AC97_EA_DRA : 0);
	snd_ac97_update(ac97, reg, tmp & 0xffff);
	snd_ac97_read(ac97, reg);
	if ((ac97->ext_id & AC97_EI_DRA) && reg == AC97_PCM_FRONT_DAC_RATE) {
		/* Intel controllers require double rate data to be put in
		 * slots 7+8
		 */
		snd_ac97_update_bits(ac97, AC97_GENERAL_PURPOSE,
				     AC97_GP_DRSS_MASK,
				     dbl ? AC97_GP_DRSS_78 : 0);
		snd_ac97_read(ac97, AC97_GENERAL_PURPOSE);
	}
	return 0;
}

EXPORT_SYMBOL(snd_ac97_set_rate);

static unsigned short get_pslots(struct snd_ac97 *ac97, unsigned char *rate_table, unsigned short *spdif_slots)
{
	if (!ac97_is_audio(ac97))
		return 0;
	if (ac97_is_rev22(ac97) || ac97_can_amap(ac97)) {
		unsigned short slots = 0;
		if (ac97_is_rev22(ac97)) {
			/* Note: it's simply emulation of AMAP behaviour */
			u16 es;
			es = ac97->regs[AC97_EXTENDED_ID] &= ~AC97_EI_DACS_SLOT_MASK;
			switch (ac97->addr) {
			case 1:
			case 2: es |= (1<<AC97_EI_DACS_SLOT_SHIFT); break;
			case 3: es |= (2<<AC97_EI_DACS_SLOT_SHIFT); break;
			}
			snd_ac97_write_cache(ac97, AC97_EXTENDED_ID, es);
		}
		switch (ac97->addr) {
		case 0:
			slots |= (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
			if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
				slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
			if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
				slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF) {
				if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT)|(1<<AC97_SLOT_SPDIF_RIGHT);
				else if (!(ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
				else
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			}
			*rate_table = 0;
			break;
		case 1:
		case 2:
			slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
			if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
				slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF) {
				if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
				else
					*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			}
			*rate_table = 1;
			break;
		case 3:
			slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
			if (ac97->ext_id & AC97_EI_SPDIF)
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
			*rate_table = 2;
			break;
		}
		return slots;
	} else {
		unsigned short slots;
		slots = (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
		if (ac97->scaps & AC97_SCAP_SURROUND_DAC)
			slots |= (1<<AC97_SLOT_PCM_SLEFT)|(1<<AC97_SLOT_PCM_SRIGHT);
		if (ac97->scaps & AC97_SCAP_CENTER_LFE_DAC)
			slots |= (1<<AC97_SLOT_PCM_CENTER)|(1<<AC97_SLOT_LFE);
		if (ac97->ext_id & AC97_EI_SPDIF) {
			if (!(ac97->scaps & AC97_SCAP_SURROUND_DAC))
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT)|(1<<AC97_SLOT_SPDIF_RIGHT);
			else if (!(ac97->scaps & AC97_SCAP_CENTER_LFE_DAC))
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT1)|(1<<AC97_SLOT_SPDIF_RIGHT1);
			else
				*spdif_slots = (1<<AC97_SLOT_SPDIF_LEFT2)|(1<<AC97_SLOT_SPDIF_RIGHT2);
		}
		*rate_table = 0;
		return slots;
	}
}

static unsigned short get_cslots(struct snd_ac97 *ac97)
{
	unsigned short slots;

	if (!ac97_is_audio(ac97))
		return 0;
	slots = (1<<AC97_SLOT_PCM_LEFT)|(1<<AC97_SLOT_PCM_RIGHT);
	slots |= (1<<AC97_SLOT_MIC);
	return slots;
}

static unsigned int get_rates(struct ac97_pcm *pcm, unsigned int cidx, unsigned short slots, int dbl)
{
	int i, idx;
	unsigned int rates = ~0;
	unsigned char reg;

	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		reg = get_slot_reg(pcm, cidx, i, dbl);
		switch (reg) {
		case AC97_PCM_FRONT_DAC_RATE:	idx = AC97_RATES_FRONT_DAC; break;
		case AC97_PCM_SURR_DAC_RATE:	idx = AC97_RATES_SURR_DAC; break;
		case AC97_PCM_LFE_DAC_RATE:	idx = AC97_RATES_LFE_DAC; break;
		case AC97_PCM_LR_ADC_RATE:	idx = AC97_RATES_ADC; break;
		case AC97_PCM_MIC_ADC_RATE:	idx = AC97_RATES_MIC_ADC; break;
		default:			idx = AC97_RATES_SPDIF; break;
		}
		rates &= pcm->r[dbl].codec[cidx]->rates[idx];
	}
	if (!dbl)
		rates &= ~(SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200 |
			   SNDRV_PCM_RATE_96000);
	return rates;
}

/**
 * snd_ac97_pcm_assign - assign AC97 slots to given PCM streams
 * @bus: the ac97 bus instance
 * @pcms_count: count of PCMs to be assigned
 * @pcms: PCMs to be assigned
 *
 * It assigns available AC97 slots for given PCMs. If none or only
 * some slots are available, pcm->xxx.slots and pcm->xxx.rslots[] members
 * are reduced and might be zero.
 */
int snd_ac97_pcm_assign(struct snd_ac97_bus *bus,
			unsigned short pcms_count,
			const struct ac97_pcm *pcms)
{
	int i, j, k;
	const struct ac97_pcm *pcm;
	struct ac97_pcm *rpcms, *rpcm;
	unsigned short avail_slots[2][4];
	unsigned char rate_table[2][4];
	unsigned short tmp, slots;
	unsigned short spdif_slots[4];
	unsigned int rates;
	struct snd_ac97 *codec;

	rpcms = kcalloc(pcms_count, sizeof(struct ac97_pcm), GFP_KERNEL);
	if (rpcms == NULL)
		return -ENOMEM;
	memset(avail_slots, 0, sizeof(avail_slots));
	memset(rate_table, 0, sizeof(rate_table));
	memset(spdif_slots, 0, sizeof(spdif_slots));
	for (i = 0; i < 4; i++) {
		codec = bus->codec[i];
		if (!codec)
			continue;
		avail_slots[0][i] = get_pslots(codec, &rate_table[0][i], &spdif_slots[i]);
		avail_slots[1][i] = get_cslots(codec);
		if (!(codec->scaps & AC97_SCAP_INDEP_SDIN)) {
			for (j = 0; j < i; j++) {
				if (bus->codec[j])
					avail_slots[1][i] &= ~avail_slots[1][j];
			}
		}
	}
	/* first step - exclusive devices */
	for (i = 0; i < pcms_count; i++) {
		pcm = &pcms[i];
		rpcm = &rpcms[i];
		/* low-level driver thinks that it's more clever */
		if (pcm->copy_flag) {
			*rpcm = *pcm;
			continue;
		}
		rpcm->stream = pcm->stream;
		rpcm->exclusive = pcm->exclusive;
		rpcm->spdif = pcm->spdif;
		rpcm->private_value = pcm->private_value;
		rpcm->bus = bus;
		rpcm->rates = ~0;
		slots = pcm->r[0].slots;
		for (j = 0; j < 4 && slots; j++) {
			if (!bus->codec[j])
				continue;
			rates = ~0;
			if (pcm->spdif && pcm->stream == 0)
				tmp = spdif_slots[j];
			else
				tmp = avail_slots[pcm->stream][j];
			if (pcm->exclusive) {
				/* exclusive access */
				tmp &= slots;
				for (k = 0; k < i; k++) {
					if (rpcm->stream == rpcms[k].stream)
						tmp &= ~rpcms[k].r[0].rslots[j];
				}
			} else {
				/* non-exclusive access */
				tmp &= pcm->r[0].slots;
			}
			if (tmp) {
				rpcm->r[0].rslots[j] = tmp;
				rpcm->r[0].codec[j] = bus->codec[j];
				rpcm->r[0].rate_table[j] = rate_table[pcm->stream][j];
				if (bus->no_vra)
					rates = SNDRV_PCM_RATE_48000;
				else
					rates = get_rates(rpcm, j, tmp, 0);
				if (pcm->exclusive)
					avail_slots[pcm->stream][j] &= ~tmp;
			}
			slots &= ~tmp;
			rpcm->r[0].slots |= tmp;
			rpcm->rates &= rates;
		}
		/* for double rate, we check the first codec only */
		if (pcm->stream == SNDRV_PCM_STREAM_PLAYBACK &&
		    bus->codec[0] && (bus->codec[0]->flags & AC97_DOUBLE_RATE) &&
		    rate_table[pcm->stream][0] == 0) {
			tmp = (1<<AC97_SLOT_PCM_LEFT) | (1<<AC97_SLOT_PCM_RIGHT) |
			      (1<<AC97_SLOT_PCM_LEFT_0) | (1<<AC97_SLOT_PCM_RIGHT_0);
			if ((tmp & pcm->r[1].slots) == tmp) {
				rpcm->r[1].slots = tmp;
				rpcm->r[1].rslots[0] = tmp;
				rpcm->r[1].rate_table[0] = 0;
				rpcm->r[1].codec[0] = bus->codec[0];
				if (pcm->exclusive)
					avail_slots[pcm->stream][0] &= ~tmp;
				if (bus->no_vra)
					rates = SNDRV_PCM_RATE_96000;
				else
					rates = get_rates(rpcm, 0, tmp, 1);
				rpcm->rates |= rates;
			}
		}
		if (rpcm->rates == ~0)
			rpcm->rates = 0; /* not used */
	}
	bus->pcms_count = pcms_count;
	bus->pcms = rpcms;
	return 0;
}

EXPORT_SYMBOL(snd_ac97_pcm_assign);

/**
 * snd_ac97_pcm_open - opens the given AC97 pcm
 * @pcm: the ac97 pcm instance
 * @rate: rate in Hz, if codec does not support VRA, this value must be 48000Hz
 * @cfg: output stream characteristics
 * @slots: a subset of allocated slots (snd_ac97_pcm_assign) for this pcm
 *
 * It locks the specified slots and sets the given rate to AC97 registers.
 */
int snd_ac97_pcm_open(struct ac97_pcm *pcm, unsigned int rate,
		      enum ac97_pcm_cfg cfg, unsigned short slots)
{
	struct snd_ac97_bus *bus;
	int i, cidx, r, ok_flag;
	unsigned int reg_ok[4] = {0,0,0,0};
	unsigned char reg;
	int err = 0;

	r = rate > 48000;
	bus = pcm->bus;
	if (cfg == AC97_PCM_CFG_SPDIF) {
		int err;
		for (cidx = 0; cidx < 4; cidx++)
			if (bus->codec[cidx] && (bus->codec[cidx]->ext_id & AC97_EI_SPDIF)) {
				err = set_spdif_rate(bus->codec[cidx], rate);
				if (err < 0)
					return err;
			}
	}
	spin_lock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		ok_flag = 0;
		for (cidx = 0; cidx < 4; cidx++) {
			if (bus->used_slots[pcm->stream][cidx] & (1 << i)) {
				spin_unlock_irq(&pcm->bus->bus_lock);
				err = -EBUSY;
				goto error;
			}
			if (pcm->r[r].rslots[cidx] & (1 << i)) {
				bus->used_slots[pcm->stream][cidx] |= (1 << i);
				ok_flag++;
			}
		}
		if (!ok_flag) {
			spin_unlock_irq(&pcm->bus->bus_lock);
			snd_printk(KERN_ERR "cannot find configuration for AC97 slot %i\n", i);
			err = -EAGAIN;
			goto error;
		}
	}
	pcm->cur_dbl = r;
	spin_unlock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		for (cidx = 0; cidx < 4; cidx++) {
			if (pcm->r[r].rslots[cidx] & (1 << i)) {
				reg = get_slot_reg(pcm, cidx, i, r);
				if (reg == 0xff) {
					snd_printk(KERN_ERR "invalid AC97 slot %i?\n", i);
					continue;
				}
				if (reg_ok[cidx] & (1 << (reg - AC97_PCM_FRONT_DAC_RATE)))
					continue;
				//printk(KERN_DEBUG "setting ac97 reg 0x%x to rate %d\n", reg, rate);
				err = snd_ac97_set_rate(pcm->r[r].codec[cidx], reg, rate);
				if (err < 0)
					snd_printk(KERN_ERR "error in snd_ac97_set_rate: cidx=%d, reg=0x%x, rate=%d, err=%d\n", cidx, reg, rate, err);
				else
					reg_ok[cidx] |= (1 << (reg - AC97_PCM_FRONT_DAC_RATE));
			}
		}
	}
	pcm->aslots = slots;
	return 0;

 error:
	pcm->aslots = slots;
	snd_ac97_pcm_close(pcm);
	return err;
}

EXPORT_SYMBOL(snd_ac97_pcm_open);

/**
 * snd_ac97_pcm_close - closes the given AC97 pcm
 * @pcm: the ac97 pcm instance
 *
 * It frees the locked AC97 slots.
 */
int snd_ac97_pcm_close(struct ac97_pcm *pcm)
{
	struct snd_ac97_bus *bus;
	unsigned short slots = pcm->aslots;
	int i, cidx;

#ifdef CONFIG_SND_AC97_POWER_SAVE
	int r = pcm->cur_dbl;
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		for (cidx = 0; cidx < 4; cidx++) {
			if (pcm->r[r].rslots[cidx] & (1 << i)) {
				int reg = get_slot_reg(pcm, cidx, i, r);
				snd_ac97_update_power(pcm->r[r].codec[cidx],
						      reg, 0);
			}
		}
	}
#endif

	bus = pcm->bus;
	spin_lock_irq(&pcm->bus->bus_lock);
	for (i = 3; i < 12; i++) {
		if (!(slots & (1 << i)))
			continue;
		for (cidx = 0; cidx < 4; cidx++)
			bus->used_slots[pcm->stream][cidx] &= ~(1 << i);
	}
	pcm->aslots = 0;
	pcm->cur_dbl = 0;
	spin_unlock_irq(&pcm->bus->bus_lock);
	return 0;
}

EXPORT_SYMBOL(snd_ac97_pcm_close);

static int double_rate_hw_constraint_rate(struct snd_pcm_hw_params *params,
					  struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	if (channels->min > 2) {
		static const struct snd_interval single_rates = {
			.min = 1,
			.max = 48000,
		};
		struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
		return snd_interval_refine(rate, &single_rates);
	}
	return 0;
}

static int double_rate_hw_constraint_channels(struct snd_pcm_hw_params *params,
					      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (rate->min > 48000) {
		static const struct snd_interval double_rate_channels = {
			.min = 2,
			.max = 2,
		};
		struct snd_interval *channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
		return snd_interval_refine(channels, &double_rate_channels);
	}
	return 0;
}

/**
 * snd_ac97_pcm_double_rate_rules - set double rate constraints
 * @runtime: the runtime of the ac97 front playback pcm
 *
 * Installs the hardware constraint rules to prevent using double rates and
 * more than two channels at the same time.
 */
int snd_ac97_pcm_double_rate_rules(struct snd_pcm_runtime *runtime)
{
	int err;

	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  double_rate_hw_constraint_rate, NULL,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (err < 0)
		return err;
	err = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  double_rate_hw_constraint_channels, NULL,
				  SNDRV_PCM_HW_PARAM_RATE, -1);
	return err;
}

EXPORT_SYMBOL(snd_ac97_pcm_double_rate_rules);
