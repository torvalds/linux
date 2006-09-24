/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.2
 *  by Intel Corporation (http://developer.intel.com).
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
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include "ac97_local.h"
#include "ac97_id.h"

/*
 * proc interface
 */

static void snd_ac97_proc_read_functions(struct snd_ac97 *ac97, struct snd_info_buffer *buffer)
{
	int header = 0, function;
	unsigned short info, sense_info;
	static const char *function_names[12] = {
		"Master Out", "AUX Out", "Center/LFE Out", "SPDIF Out",
		"Phone In", "Mic 1", "Mic 2", "Line In", "CD In", "Video In",
		"Aux In", "Mono Out"
	};
	static const char *locations[8] = {
		"Rear I/O Panel", "Front Panel", "Motherboard", "Dock/External",
		"reserved", "reserved", "reserved", "NC/unused"
	};

	for (function = 0; function < 12; ++function) {
		snd_ac97_write(ac97, AC97_FUNC_SELECT, function << 1);
		info = snd_ac97_read(ac97, AC97_FUNC_INFO);
		if (!(info & 0x0001))
			continue;
		if (!header) {
			snd_iprintf(buffer, "\n                    Gain     Inverted  Buffer delay  Location\n");
			header = 1;
		}
		sense_info = snd_ac97_read(ac97, AC97_SENSE_INFO);
		snd_iprintf(buffer, "%-17s: %3d.%d dBV    %c      %2d/fs         %s\n",
			    function_names[function],
			    (info & 0x8000 ? -1 : 1) * ((info & 0x7000) >> 12) * 3 / 2,
			    ((info & 0x0800) >> 11) * 5,
			    info & 0x0400 ? 'X' : '-',
			    (info & 0x03e0) >> 5,
			    locations[sense_info >> 13]);
	}
}

static const char *snd_ac97_stereo_enhancements[] =
{
  /*   0 */ "No 3D Stereo Enhancement",
  /*   1 */ "Analog Devices Phat Stereo",
  /*   2 */ "Creative Stereo Enhancement",
  /*   3 */ "National Semi 3D Stereo Enhancement",
  /*   4 */ "YAMAHA Ymersion",
  /*   5 */ "BBE 3D Stereo Enhancement",
  /*   6 */ "Crystal Semi 3D Stereo Enhancement",
  /*   7 */ "Qsound QXpander",
  /*   8 */ "Spatializer 3D Stereo Enhancement",
  /*   9 */ "SRS 3D Stereo Enhancement",
  /*  10 */ "Platform Tech 3D Stereo Enhancement",
  /*  11 */ "AKM 3D Audio",
  /*  12 */ "Aureal Stereo Enhancement",
  /*  13 */ "Aztech 3D Enhancement",
  /*  14 */ "Binaura 3D Audio Enhancement",
  /*  15 */ "ESS Technology Stereo Enhancement",
  /*  16 */ "Harman International VMAx",
  /*  17 */ "Nvidea/IC Ensemble/KS Waves 3D Stereo Enhancement",
  /*  18 */ "Philips Incredible Sound",
  /*  19 */ "Texas Instruments 3D Stereo Enhancement",
  /*  20 */ "VLSI Technology 3D Stereo Enhancement",
  /*  21 */ "TriTech 3D Stereo Enhancement",
  /*  22 */ "Realtek 3D Stereo Enhancement",
  /*  23 */ "Samsung 3D Stereo Enhancement",
  /*  24 */ "Wolfson Microelectronics 3D Enhancement",
  /*  25 */ "Delta Integration 3D Enhancement",
  /*  26 */ "SigmaTel 3D Enhancement",
  /*  27 */ "IC Ensemble/KS Waves",
  /*  28 */ "Rockwell 3D Stereo Enhancement",
  /*  29 */ "Reserved 29",
  /*  30 */ "Reserved 30",
  /*  31 */ "Reserved 31"
};

static void snd_ac97_proc_read_main(struct snd_ac97 *ac97, struct snd_info_buffer *buffer, int subidx)
{
	char name[64];
	unsigned short val, tmp, ext, mext;
	static const char *spdif_slots[4] = { " SPDIF=3/4", " SPDIF=7/8", " SPDIF=6/9", " SPDIF=10/11" };
	static const char *spdif_rates[4] = { " Rate=44.1kHz", " Rate=res", " Rate=48kHz", " Rate=32kHz" };
	static const char *spdif_rates_cs4205[4] = { " Rate=48kHz", " Rate=44.1kHz", " Rate=res", " Rate=res" };
	static const char *double_rate_slots[4] = { "10/11", "7/8", "reserved", "reserved" };

	snd_ac97_get_name(NULL, ac97->id, name, 0);
	snd_iprintf(buffer, "%d-%d/%d: %s\n\n", ac97->addr, ac97->num, subidx, name);

	if ((ac97->scaps & AC97_SCAP_AUDIO) == 0)
		goto __modem;

        snd_iprintf(buffer, "PCI Subsys Vendor: 0x%04x\n",
	            ac97->subsystem_vendor);
        snd_iprintf(buffer, "PCI Subsys Device: 0x%04x\n\n",
                    ac97->subsystem_device);

	if ((ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_23) {
		val = snd_ac97_read(ac97, AC97_INT_PAGING);
		snd_ac97_update_bits(ac97, AC97_INT_PAGING,
				     AC97_PAGE_MASK, AC97_PAGE_1);
		tmp = snd_ac97_read(ac97, AC97_CODEC_CLASS_REV);
		snd_iprintf(buffer, "Revision         : 0x%02x\n", tmp & 0xff);
		snd_iprintf(buffer, "Compat. Class    : 0x%02x\n", (tmp >> 8) & 0x1f);
		snd_iprintf(buffer, "Subsys. Vendor ID: 0x%04x\n",
			    snd_ac97_read(ac97, AC97_PCI_SVID));
		snd_iprintf(buffer, "Subsys. ID       : 0x%04x\n\n",
			    snd_ac97_read(ac97, AC97_PCI_SID));
		snd_ac97_update_bits(ac97, AC97_INT_PAGING,
				     AC97_PAGE_MASK, val & AC97_PAGE_MASK);
	}

	// val = snd_ac97_read(ac97, AC97_RESET);
	val = ac97->caps;
	snd_iprintf(buffer, "Capabilities     :%s%s%s%s%s%s\n",
	    	    val & AC97_BC_DEDICATED_MIC ? " -dedicated MIC PCM IN channel-" : "",
		    val & AC97_BC_RESERVED1 ? " -reserved1-" : "",
		    val & AC97_BC_BASS_TREBLE ? " -bass & treble-" : "",
		    val & AC97_BC_SIM_STEREO ? " -simulated stereo-" : "",
		    val & AC97_BC_HEADPHONE ? " -headphone out-" : "",
		    val & AC97_BC_LOUDNESS ? " -loudness-" : "");
	tmp = ac97->caps & AC97_BC_DAC_MASK;
	snd_iprintf(buffer, "DAC resolution   : %s%s%s%s\n",
		    tmp == AC97_BC_16BIT_DAC ? "16-bit" : "",
		    tmp == AC97_BC_18BIT_DAC ? "18-bit" : "",
		    tmp == AC97_BC_20BIT_DAC ? "20-bit" : "",
		    tmp == AC97_BC_DAC_MASK ? "???" : "");
	tmp = ac97->caps & AC97_BC_ADC_MASK;
	snd_iprintf(buffer, "ADC resolution   : %s%s%s%s\n",
		    tmp == AC97_BC_16BIT_ADC ? "16-bit" : "",
		    tmp == AC97_BC_18BIT_ADC ? "18-bit" : "",
		    tmp == AC97_BC_20BIT_ADC ? "20-bit" : "",
		    tmp == AC97_BC_ADC_MASK ? "???" : "");
	snd_iprintf(buffer, "3D enhancement   : %s\n",
		snd_ac97_stereo_enhancements[(val >> 10) & 0x1f]);
	snd_iprintf(buffer, "\nCurrent setup\n");
	val = snd_ac97_read(ac97, AC97_MIC);
	snd_iprintf(buffer, "Mic gain         : %s [%s]\n", val & 0x0040 ? "+20dB" : "+0dB", ac97->regs[AC97_MIC] & 0x0040 ? "+20dB" : "+0dB");
	val = snd_ac97_read(ac97, AC97_GENERAL_PURPOSE);
	snd_iprintf(buffer, "POP path         : %s 3D\n"
		    "Sim. stereo      : %s\n"
		    "3D enhancement   : %s\n"
		    "Loudness         : %s\n"
		    "Mono output      : %s\n"
		    "Mic select       : %s\n"
		    "ADC/DAC loopback : %s\n",
		    val & 0x8000 ? "post" : "pre",
		    val & 0x4000 ? "on" : "off",
		    val & 0x2000 ? "on" : "off",
		    val & 0x1000 ? "on" : "off",
		    val & 0x0200 ? "Mic" : "MIX",
		    val & 0x0100 ? "Mic2" : "Mic1",
		    val & 0x0080 ? "on" : "off");
	if (ac97->ext_id & AC97_EI_DRA)
		snd_iprintf(buffer, "Double rate slots: %s\n",
			    double_rate_slots[(val >> 10) & 3]);

	ext = snd_ac97_read(ac97, AC97_EXTENDED_ID);
	if (ext == 0)
		goto __modem;
		
	snd_iprintf(buffer, "Extended ID      : codec=%i rev=%i%s%s%s%s DSA=%i%s%s%s%s\n",
			(ext & AC97_EI_ADDR_MASK) >> AC97_EI_ADDR_SHIFT,
			(ext & AC97_EI_REV_MASK) >> AC97_EI_REV_SHIFT,
			ext & AC97_EI_AMAP ? " AMAP" : "",
			ext & AC97_EI_LDAC ? " LDAC" : "",
			ext & AC97_EI_SDAC ? " SDAC" : "",
			ext & AC97_EI_CDAC ? " CDAC" : "",
			(ext & AC97_EI_DACS_SLOT_MASK) >> AC97_EI_DACS_SLOT_SHIFT,
			ext & AC97_EI_VRM ? " VRM" : "",
			ext & AC97_EI_SPDIF ? " SPDIF" : "",
			ext & AC97_EI_DRA ? " DRA" : "",
			ext & AC97_EI_VRA ? " VRA" : "");
	val = snd_ac97_read(ac97, AC97_EXTENDED_STATUS);
	snd_iprintf(buffer, "Extended status  :%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			val & AC97_EA_PRL ? " PRL" : "",
			val & AC97_EA_PRK ? " PRK" : "",
			val & AC97_EA_PRJ ? " PRJ" : "",
			val & AC97_EA_PRI ? " PRI" : "",
			val & AC97_EA_SPCV ? " SPCV" : "",
			val & AC97_EA_MDAC ? " MADC" : "",
			val & AC97_EA_LDAC ? " LDAC" : "",
			val & AC97_EA_SDAC ? " SDAC" : "",
			val & AC97_EA_CDAC ? " CDAC" : "",
			ext & AC97_EI_SPDIF ? spdif_slots[(val & AC97_EA_SPSA_SLOT_MASK) >> AC97_EA_SPSA_SLOT_SHIFT] : "",
			val & AC97_EA_VRM ? " VRM" : "",
			val & AC97_EA_SPDIF ? " SPDIF" : "",
			val & AC97_EA_DRA ? " DRA" : "",
			val & AC97_EA_VRA ? " VRA" : "");
	if (ext & AC97_EI_VRA) {	/* VRA */
		val = snd_ac97_read(ac97, AC97_PCM_FRONT_DAC_RATE);
		snd_iprintf(buffer, "PCM front DAC    : %iHz\n", val);
		if (ext & AC97_EI_SDAC) {
			val = snd_ac97_read(ac97, AC97_PCM_SURR_DAC_RATE);
			snd_iprintf(buffer, "PCM Surr DAC     : %iHz\n", val);
		}
		if (ext & AC97_EI_LDAC) {
			val = snd_ac97_read(ac97, AC97_PCM_LFE_DAC_RATE);
			snd_iprintf(buffer, "PCM LFE DAC      : %iHz\n", val);
		}
		val = snd_ac97_read(ac97, AC97_PCM_LR_ADC_RATE);
		snd_iprintf(buffer, "PCM ADC          : %iHz\n", val);
	}
	if (ext & AC97_EI_VRM) {
		val = snd_ac97_read(ac97, AC97_PCM_MIC_ADC_RATE);
		snd_iprintf(buffer, "PCM MIC ADC      : %iHz\n", val);
	}
	if ((ext & AC97_EI_SPDIF) || (ac97->flags & AC97_CS_SPDIF)) {
	        if (ac97->flags & AC97_CS_SPDIF)
			val = snd_ac97_read(ac97, AC97_CSR_SPDIF);
		else
			val = snd_ac97_read(ac97, AC97_SPDIF);

		snd_iprintf(buffer, "SPDIF Control    :%s%s%s%s Category=0x%x Generation=%i%s%s%s\n",
			val & AC97_SC_PRO ? " PRO" : " Consumer",
			val & AC97_SC_NAUDIO ? " Non-audio" : " PCM",
			val & AC97_SC_COPY ? "" : " Copyright",
			val & AC97_SC_PRE ? " Preemph50/15" : "",
			(val & AC97_SC_CC_MASK) >> AC97_SC_CC_SHIFT,
			(val & AC97_SC_L) >> 11,
			(ac97->flags & AC97_CS_SPDIF) ?
			    spdif_rates_cs4205[(val & AC97_SC_SPSR_MASK) >> AC97_SC_SPSR_SHIFT] :
			    spdif_rates[(val & AC97_SC_SPSR_MASK) >> AC97_SC_SPSR_SHIFT],
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & AC97_SC_DRS ? " Validity" : "") :
			    (val & AC97_SC_DRS ? " DRS" : ""),
			(ac97->flags & AC97_CS_SPDIF) ?
			    (val & AC97_SC_V ? " Enabled" : "") :
			    (val & AC97_SC_V ? " Validity" : ""));
		/* ALC650 specific*/
		if ((ac97->id & 0xfffffff0) == 0x414c4720 &&
		    (snd_ac97_read(ac97, AC97_ALC650_CLOCK) & 0x01)) {
			val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS2);
			if (val & AC97_ALC650_CLOCK_LOCK) {
				val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS1);
				snd_iprintf(buffer, "SPDIF In Status  :%s%s%s%s Category=0x%x Generation=%i",
					    val & AC97_ALC650_PRO ? " PRO" : " Consumer",
					    val & AC97_ALC650_NAUDIO ? " Non-audio" : " PCM",
					    val & AC97_ALC650_COPY ? "" : " Copyright",
					    val & AC97_ALC650_PRE ? " Preemph50/15" : "",
					    (val & AC97_ALC650_CC_MASK) >> AC97_ALC650_CC_SHIFT,
					    (val & AC97_ALC650_L) >> 15);
				val = snd_ac97_read(ac97, AC97_ALC650_SPDIF_INPUT_STATUS2);
				snd_iprintf(buffer, "%s Accuracy=%i%s%s\n",
					    spdif_rates[(val & AC97_ALC650_SPSR_MASK) >> AC97_ALC650_SPSR_SHIFT],
					    (val & AC97_ALC650_CLOCK_ACCURACY) >> AC97_ALC650_CLOCK_SHIFT,
					    (val & AC97_ALC650_CLOCK_LOCK ? " Locked" : " Unlocked"),
					    (val & AC97_ALC650_V ? " Validity?" : ""));
			} else {
				snd_iprintf(buffer, "SPDIF In Status  : Not Locked\n");
			}
		}
	}
	if ((ac97->ext_id & AC97_EI_REV_MASK) >= AC97_EI_REV_23) {
		val = snd_ac97_read(ac97, AC97_INT_PAGING);
		snd_ac97_update_bits(ac97, AC97_INT_PAGING,
				     AC97_PAGE_MASK, AC97_PAGE_1);
		snd_ac97_proc_read_functions(ac97, buffer);
		snd_ac97_update_bits(ac97, AC97_INT_PAGING,
				     AC97_PAGE_MASK, val & AC97_PAGE_MASK);
	}


      __modem:
	mext = snd_ac97_read(ac97, AC97_EXTENDED_MID);
	if (mext == 0)
		return;
	
	snd_iprintf(buffer, "Extended modem ID: codec=%i%s%s%s%s%s\n",
			(mext & AC97_MEI_ADDR_MASK) >> AC97_MEI_ADDR_SHIFT,
			mext & AC97_MEI_CID2 ? " CID2" : "",
			mext & AC97_MEI_CID1 ? " CID1" : "",
			mext & AC97_MEI_HANDSET ? " HSET" : "",
			mext & AC97_MEI_LINE2 ? " LIN2" : "",
			mext & AC97_MEI_LINE1 ? " LIN1" : "");
	val = snd_ac97_read(ac97, AC97_EXTENDED_MSTATUS);
	snd_iprintf(buffer, "Modem status     :%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
			val & AC97_MEA_GPIO ? " GPIO" : "",
			val & AC97_MEA_MREF ? " MREF" : "",
			val & AC97_MEA_ADC1 ? " ADC1" : "",
			val & AC97_MEA_DAC1 ? " DAC1" : "",
			val & AC97_MEA_ADC2 ? " ADC2" : "",
			val & AC97_MEA_DAC2 ? " DAC2" : "",
			val & AC97_MEA_HADC ? " HADC" : "",
			val & AC97_MEA_HDAC ? " HDAC" : "",
			val & AC97_MEA_PRA ? " PRA(GPIO)" : "",
			val & AC97_MEA_PRB ? " PRB(res)" : "",
			val & AC97_MEA_PRC ? " PRC(ADC1)" : "",
			val & AC97_MEA_PRD ? " PRD(DAC1)" : "",
			val & AC97_MEA_PRE ? " PRE(ADC2)" : "",
			val & AC97_MEA_PRF ? " PRF(DAC2)" : "",
			val & AC97_MEA_PRG ? " PRG(HADC)" : "",
			val & AC97_MEA_PRH ? " PRH(HDAC)" : "");
	if (mext & AC97_MEI_LINE1) {
		val = snd_ac97_read(ac97, AC97_LINE1_RATE);
		snd_iprintf(buffer, "Line1 rate       : %iHz\n", val);
	}
	if (mext & AC97_MEI_LINE2) {
		val = snd_ac97_read(ac97, AC97_LINE2_RATE);
		snd_iprintf(buffer, "Line2 rate       : %iHz\n", val);
	}
	if (mext & AC97_MEI_HANDSET) {
		val = snd_ac97_read(ac97, AC97_HANDSET_RATE);
		snd_iprintf(buffer, "Headset rate     : %iHz\n", val);
	}
}

static void snd_ac97_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ac97 *ac97 = entry->private_data;
	
	mutex_lock(&ac97->page_mutex);
	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86
		int idx;
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
						     ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_read_main(ac97, buffer, idx);
				snd_iprintf(buffer, "\n\n");
			}
		/* select all codecs */
		snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
		
		snd_iprintf(buffer, "\nAD18XX configuration\n");
		snd_iprintf(buffer, "Unchained        : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.unchained[0],
			ac97->spec.ad18xx.unchained[1],
			ac97->spec.ad18xx.unchained[2]);
		snd_iprintf(buffer, "Chained          : 0x%04x,0x%04x,0x%04x\n",
			ac97->spec.ad18xx.chained[0],
			ac97->spec.ad18xx.chained[1],
			ac97->spec.ad18xx.chained[2]);
	} else {
		snd_ac97_proc_read_main(ac97, buffer, 0);
	}
	mutex_unlock(&ac97->page_mutex);
}

#ifdef CONFIG_SND_DEBUG
/* direct register write for debugging */
static void snd_ac97_proc_regs_write(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ac97 *ac97 = entry->private_data;
	char line[64];
	unsigned int reg, val;
	mutex_lock(&ac97->page_mutex);
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		/* register must be even */
		if (reg < 0x80 && (reg & 1) == 0 && val <= 0xffff)
			snd_ac97_write_cache(ac97, reg, val);
	}
	mutex_unlock(&ac97->page_mutex);
}
#endif

static void snd_ac97_proc_regs_read_main(struct snd_ac97 *ac97, struct snd_info_buffer *buffer, int subidx)
{
	int reg, val;

	for (reg = 0; reg < 0x80; reg += 2) {
		val = snd_ac97_read(ac97, reg);
		snd_iprintf(buffer, "%i:%02x = %04x\n", subidx, reg, val);
	}
}

static void snd_ac97_proc_regs_read(struct snd_info_entry *entry, 
				    struct snd_info_buffer *buffer)
{
	struct snd_ac97 *ac97 = entry->private_data;

	mutex_lock(&ac97->page_mutex);
	if ((ac97->id & 0xffffff40) == AC97_ID_AD1881) {	// Analog Devices AD1881/85/86

		int idx;
		for (idx = 0; idx < 3; idx++)
			if (ac97->spec.ad18xx.id[idx]) {
				/* select single codec */
				snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000,
						     ac97->spec.ad18xx.unchained[idx] | ac97->spec.ad18xx.chained[idx]);
				snd_ac97_proc_regs_read_main(ac97, buffer, idx);
			}
		/* select all codecs */
		snd_ac97_update_bits(ac97, AC97_AD_SERIAL_CFG, 0x7000, 0x7000);
	} else {
		snd_ac97_proc_regs_read_main(ac97, buffer, 0);
	}	
	mutex_unlock(&ac97->page_mutex);
}

void snd_ac97_proc_init(struct snd_ac97 * ac97)
{
	struct snd_info_entry *entry;
	char name[32];
	const char *prefix;

	if (ac97->bus->proc == NULL)
		return;
	prefix = ac97_is_audio(ac97) ? "ac97" : "mc97";
	sprintf(name, "%s#%d-%d", prefix, ac97->addr, ac97->num);
	if ((entry = snd_info_create_card_entry(ac97->bus->card, name, ac97->bus->proc)) != NULL) {
		snd_info_set_text_ops(entry, ac97, snd_ac97_proc_read);
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ac97->proc = entry;
	sprintf(name, "%s#%d-%d+regs", prefix, ac97->addr, ac97->num);
	if ((entry = snd_info_create_card_entry(ac97->bus->card, name, ac97->bus->proc)) != NULL) {
		snd_info_set_text_ops(entry, ac97, snd_ac97_proc_regs_read);
#ifdef CONFIG_SND_DEBUG
		entry->mode |= S_IWUSR;
		entry->c.text.write = snd_ac97_proc_regs_write;
#endif
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	ac97->proc_regs = entry;
}

void snd_ac97_proc_done(struct snd_ac97 * ac97)
{
	snd_info_free_entry(ac97->proc_regs);
	ac97->proc_regs = NULL;
	snd_info_free_entry(ac97->proc);
	ac97->proc = NULL;
}

void snd_ac97_bus_proc_init(struct snd_ac97_bus * bus)
{
	struct snd_info_entry *entry;
	char name[32];

	sprintf(name, "codec97#%d", bus->num);
	if ((entry = snd_info_create_card_entry(bus->card, name, bus->card->proc_root)) != NULL) {
		entry->mode = S_IFDIR | S_IRUGO | S_IXUGO;
		if (snd_info_register(entry) < 0) {
			snd_info_free_entry(entry);
			entry = NULL;
		}
	}
	bus->proc = entry;
}

void snd_ac97_bus_proc_done(struct snd_ac97_bus * bus)
{
	snd_info_free_entry(bus->proc);
	bus->proc = NULL;
}
