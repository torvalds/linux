/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.17
 *
 *  FEATURES currently supported:
 *    See ca0106_main.c for features.
 * 
 *  Changelog:
 *    Support interrupts per period.
 *    Removed noise from Center/LFE channel when in Analog mode.
 *    Rename and remove mixer controls.
 *  0.0.6
 *    Use separate card based DMA buffer for periods table list.
 *  0.0.7
 *    Change remove and rename ctrls into lists.
 *  0.0.8
 *    Try to fix capture sources.
 *  0.0.9
 *    Fix AC3 output.
 *    Enable S32_LE format support.
 *  0.0.10
 *    Enable playback 48000 and 96000 rates. (Rates other that these do not work, even with "plug:front".)
 *  0.0.11
 *    Add Model name recognition.
 *  0.0.12
 *    Correct interrupt timing. interrupt at end of period, instead of in the middle of a playback period.
 *    Remove redundent "voice" handling.
 *  0.0.13
 *    Single trigger call for multi channels.
 *  0.0.14
 *    Set limits based on what the sound card hardware can do.
 *    playback periods_min=2, periods_max=8
 *    capture hw constraints require period_size = n * 64 bytes.
 *    playback hw constraints require period_size = n * 64 bytes.
 *  0.0.15
 *    Separate ca0106.c into separate functional .c files.
 *  0.0.16
 *    Modified Copyright message.
 *  0.0.17
 *    Add iec958 file in proc file system to show status of SPDIF in.
 *    
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
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
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/asoundef.h>

#include "ca0106.h"


struct snd_ca0106_category_str {
	int val;
	const char *name;
};

static struct snd_ca0106_category_str snd_ca0106_con_category[] = {
	{ IEC958_AES1_CON_DAT, "DAT" },
	{ IEC958_AES1_CON_VCR, "VCR" },
	{ IEC958_AES1_CON_MICROPHONE, "microphone" },
	{ IEC958_AES1_CON_SYNTHESIZER, "synthesizer" },
	{ IEC958_AES1_CON_RATE_CONVERTER, "rate converter" },
	{ IEC958_AES1_CON_MIXER, "mixer" },
	{ IEC958_AES1_CON_SAMPLER, "sampler" },
	{ IEC958_AES1_CON_PCM_CODER, "PCM coder" },
	{ IEC958_AES1_CON_IEC908_CD, "CD" },
	{ IEC958_AES1_CON_NON_IEC908_CD, "non-IEC908 CD" },
	{ IEC958_AES1_CON_GENERAL, "general" },
};


void snd_ca0106_proc_dump_iec958( snd_info_buffer_t *buffer, u32 value)
{
	int i;
	u32 status[4];
	status[0] = value & 0xff;
	status[1] = (value >> 8) & 0xff;
	status[2] = (value >> 16)  & 0xff;
	status[3] = (value >> 24)  & 0xff;
	
	if (! (status[0] & IEC958_AES0_PROFESSIONAL)) {
		/* consumer */
		snd_iprintf(buffer, "Mode: consumer\n");
		snd_iprintf(buffer, "Data: ");
		if (!(status[0] & IEC958_AES0_NONAUDIO)) {
			snd_iprintf(buffer, "audio\n");
		} else {
			snd_iprintf(buffer, "non-audio\n");
		}
		snd_iprintf(buffer, "Rate: ");
		switch (status[3] & IEC958_AES3_CON_FS) {
		case IEC958_AES3_CON_FS_44100:
			snd_iprintf(buffer, "44100 Hz\n");
			break;
		case IEC958_AES3_CON_FS_48000:
			snd_iprintf(buffer, "48000 Hz\n");
			break;
		case IEC958_AES3_CON_FS_32000:
			snd_iprintf(buffer, "32000 Hz\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
		snd_iprintf(buffer, "Copyright: ");
		if (status[0] & IEC958_AES0_CON_NOT_COPYRIGHT) {
			snd_iprintf(buffer, "permitted\n");
		} else {
			snd_iprintf(buffer, "protected\n");
		}
		snd_iprintf(buffer, "Emphasis: ");
		if ((status[0] & IEC958_AES0_CON_EMPHASIS) != IEC958_AES0_CON_EMPHASIS_5015) {
			snd_iprintf(buffer, "none\n");
		} else {
			snd_iprintf(buffer, "50/15us\n");
		}
		snd_iprintf(buffer, "Category: ");
		for (i = 0; i < ARRAY_SIZE(snd_ca0106_con_category); i++) {
			if ((status[1] & IEC958_AES1_CON_CATEGORY) == snd_ca0106_con_category[i].val) {
				snd_iprintf(buffer, "%s\n", snd_ca0106_con_category[i].name);
				break;
			}
		}
		if (i >= ARRAY_SIZE(snd_ca0106_con_category)) {
			snd_iprintf(buffer, "unknown 0x%x\n", status[1] & IEC958_AES1_CON_CATEGORY);
		}
		snd_iprintf(buffer, "Original: ");
		if (status[1] & IEC958_AES1_CON_ORIGINAL) {
			snd_iprintf(buffer, "original\n");
		} else {
			snd_iprintf(buffer, "1st generation\n");
		}
		snd_iprintf(buffer, "Clock: ");
		switch (status[3] & IEC958_AES3_CON_CLOCK) {
		case IEC958_AES3_CON_CLOCK_1000PPM:
			snd_iprintf(buffer, "1000 ppm\n");
			break;
		case IEC958_AES3_CON_CLOCK_50PPM:
			snd_iprintf(buffer, "50 ppm\n");
			break;
		case IEC958_AES3_CON_CLOCK_VARIABLE:
			snd_iprintf(buffer, "variable pitch\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
	} else {
		snd_iprintf(buffer, "Mode: professional\n");
		snd_iprintf(buffer, "Data: ");
		if (!(status[0] & IEC958_AES0_NONAUDIO)) {
			snd_iprintf(buffer, "audio\n");
		} else {
			snd_iprintf(buffer, "non-audio\n");
		}
		snd_iprintf(buffer, "Rate: ");
		switch (status[0] & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_44100:
			snd_iprintf(buffer, "44100 Hz\n");
			break;
		case IEC958_AES0_PRO_FS_48000:
			snd_iprintf(buffer, "48000 Hz\n");
			break;
		case IEC958_AES0_PRO_FS_32000:
			snd_iprintf(buffer, "32000 Hz\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
		snd_iprintf(buffer, "Rate Locked: ");
		if (status[0] & IEC958_AES0_PRO_FREQ_UNLOCKED)
			snd_iprintf(buffer, "no\n");
		else
			snd_iprintf(buffer, "yes\n");
		snd_iprintf(buffer, "Emphasis: ");
		switch (status[0] & IEC958_AES0_PRO_EMPHASIS) {
		case IEC958_AES0_PRO_EMPHASIS_CCITT:
			snd_iprintf(buffer, "CCITT J.17\n");
			break;
		case IEC958_AES0_PRO_EMPHASIS_NONE:
			snd_iprintf(buffer, "none\n");
			break;
		case IEC958_AES0_PRO_EMPHASIS_5015:
			snd_iprintf(buffer, "50/15us\n");
			break;
		case IEC958_AES0_PRO_EMPHASIS_NOTID:
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
		snd_iprintf(buffer, "Stereophonic: ");
		if ((status[1] & IEC958_AES1_PRO_MODE) == IEC958_AES1_PRO_MODE_STEREOPHONIC) {
			snd_iprintf(buffer, "stereo\n");
		} else {
			snd_iprintf(buffer, "not indicated\n");
		}
		snd_iprintf(buffer, "Userbits: ");
		switch (status[1] & IEC958_AES1_PRO_USERBITS) {
		case IEC958_AES1_PRO_USERBITS_192:
			snd_iprintf(buffer, "192bit\n");
			break;
		case IEC958_AES1_PRO_USERBITS_UDEF:
			snd_iprintf(buffer, "user-defined\n");
			break;
		default:
			snd_iprintf(buffer, "unkown\n");
			break;
		}
		snd_iprintf(buffer, "Sample Bits: ");
		switch (status[2] & IEC958_AES2_PRO_SBITS) {
		case IEC958_AES2_PRO_SBITS_20:
			snd_iprintf(buffer, "20 bit\n");
			break;
		case IEC958_AES2_PRO_SBITS_24:
			snd_iprintf(buffer, "24 bit\n");
			break;
		case IEC958_AES2_PRO_SBITS_UDEF:
			snd_iprintf(buffer, "user defined\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
		snd_iprintf(buffer, "Word Length: ");
		switch (status[2] & IEC958_AES2_PRO_WORDLEN) {
		case IEC958_AES2_PRO_WORDLEN_22_18:
			snd_iprintf(buffer, "22 bit or 18 bit\n");
			break;
		case IEC958_AES2_PRO_WORDLEN_23_19:
			snd_iprintf(buffer, "23 bit or 19 bit\n");
			break;
		case IEC958_AES2_PRO_WORDLEN_24_20:
			snd_iprintf(buffer, "24 bit or 20 bit\n");
			break;
		case IEC958_AES2_PRO_WORDLEN_20_16:
			snd_iprintf(buffer, "20 bit or 16 bit\n");
			break;
		default:
			snd_iprintf(buffer, "unknown\n");
			break;
		}
	}
}

static void snd_ca0106_proc_iec958(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	u32 value;

        value = snd_ca0106_ptr_read(emu, SAMPLE_RATE_TRACKER_STATUS, 0);
	snd_iprintf(buffer, "Status: %s, %s, %s\n",
		  (value & 0x100000) ? "Rate Locked" : "Not Rate Locked",
		  (value & 0x200000) ? "SPDIF Locked" : "No SPDIF Lock",
		  (value & 0x400000) ? "Audio Valid" : "No valid audio" );
	snd_iprintf(buffer, "Estimated sample rate: %u\n", 
		  ((value & 0xfffff) * 48000) / 0x8000 );
	if (value & 0x200000) {
		snd_iprintf(buffer, "IEC958/SPDIF input status:\n");
        	value = snd_ca0106_ptr_read(emu, SPDIF_INPUT_STATUS, 0);
		snd_ca0106_proc_dump_iec958(buffer, value);
	}

	snd_iprintf(buffer, "\n");
}

static void snd_ca0106_proc_reg_write32(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	unsigned long flags;
        char line[64];
        u32 reg, val;
        while (!snd_info_get_line(buffer, line, sizeof(line))) {
                if (sscanf(line, "%x %x", &reg, &val) != 2)
                        continue;
                if ((reg < 0x40) && (reg >=0) && (val <= 0xffffffff) ) {
			spin_lock_irqsave(&emu->emu_lock, flags);
			outl(val, emu->port + (reg & 0xfffffffc));
			spin_unlock_irqrestore(&emu->emu_lock, flags);
		}
        }
}

static void snd_ca0106_proc_reg_read32(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	unsigned long value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=4) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inl(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %08lX\n", i, value);
	}
}

static void snd_ca0106_proc_reg_read16(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
        unsigned int value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=2) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inw(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %04X\n", i, value);
	}
}

static void snd_ca0106_proc_reg_read8(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	unsigned int value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "Registers:\n\n");
	for(i = 0; i < 0x20; i+=1) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inb(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "Register %02X: %02X\n", i, value);
	}
}

static void snd_ca0106_proc_reg_read1(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	unsigned long value;
	int i,j;

	snd_iprintf(buffer, "Registers\n");
	for(i = 0; i < 0x40; i++) {
		snd_iprintf(buffer, "%02X: ",i);
		for (j = 0; j < 4; j++) {
                  value = snd_ca0106_ptr_read(emu, i, j);
		  snd_iprintf(buffer, "%08lX ", value);
                }
	        snd_iprintf(buffer, "\n");
	}
}

static void snd_ca0106_proc_reg_read2(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
	unsigned long value;
	int i,j;

	snd_iprintf(buffer, "Registers\n");
	for(i = 0x40; i < 0x80; i++) {
		snd_iprintf(buffer, "%02X: ",i);
		for (j = 0; j < 4; j++) {
                  value = snd_ca0106_ptr_read(emu, i, j);
		  snd_iprintf(buffer, "%08lX ", value);
                }
	        snd_iprintf(buffer, "\n");
	}
}

static void snd_ca0106_proc_reg_write(snd_info_entry_t *entry, 
				       snd_info_buffer_t * buffer)
{
	ca0106_t *emu = entry->private_data;
        char line[64];
        unsigned int reg, channel_id , val;
        while (!snd_info_get_line(buffer, line, sizeof(line))) {
                if (sscanf(line, "%x %x %x", &reg, &channel_id, &val) != 3)
                        continue;
                if ((reg < 0x80) && (reg >=0) && (val <= 0xffffffff) && (channel_id >=0) && (channel_id <= 3) )
                        snd_ca0106_ptr_write(emu, reg, channel_id, val);
        }
}


int __devinit snd_ca0106_proc_init(ca0106_t * emu)
{
	snd_info_entry_t *entry;
	
	if(! snd_card_proc_new(emu->card, "iec958", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_iec958);
	if(! snd_card_proc_new(emu->card, "ca0106_reg32", &entry)) {
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_reg_read32);
		entry->c.text.write_size = 64;
		entry->c.text.write = snd_ca0106_proc_reg_write32;
	}
	if(! snd_card_proc_new(emu->card, "ca0106_reg16", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_reg_read16);
	if(! snd_card_proc_new(emu->card, "ca0106_reg8", &entry))
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_reg_read8);
	if(! snd_card_proc_new(emu->card, "ca0106_regs1", &entry)) {
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_reg_read1);
		entry->c.text.write_size = 64;
		entry->c.text.write = snd_ca0106_proc_reg_write;
//		entry->private_data = emu;
	}
	if(! snd_card_proc_new(emu->card, "ca0106_regs2", &entry)) 
		snd_info_set_text_ops(entry, emu, 1024, snd_ca0106_proc_reg_read2);
	return 0;
}

