// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / proc interface routines
 *
 *  Copyright (c) by James Courtier-Dutton <James@superbug.co.uk>
 *  	Added EMU 1010 support.
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include "p16v.h"

static void snd_emu10k1_proc_spdif_status(struct snd_emu10k1 * emu,
					  struct snd_info_buffer *buffer,
					  char *title,
					  int status_reg,
					  int rate_reg)
{
	static char *clkaccy[4] = { "1000ppm", "50ppm", "variable", "unknown" };
	static int samplerate[16] = { 44100, 1, 48000, 32000, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static char *channel[16] = { "unspec", "left", "right", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" };
	static char *emphasis[8] = { "none", "50/15 usec 2 channel", "2", "3", "4", "5", "6", "7" };
	unsigned int status, rate = 0;
	
	status = snd_emu10k1_ptr_read(emu, status_reg, 0);

	snd_iprintf(buffer, "\n%s\n", title);

	if (status != 0xffffffff) {
		snd_iprintf(buffer, "Professional Mode     : %s\n", (status & SPCS_PROFESSIONAL) ? "yes" : "no");
		snd_iprintf(buffer, "Not Audio Data        : %s\n", (status & SPCS_NOTAUDIODATA) ? "yes" : "no");
		snd_iprintf(buffer, "Copyright             : %s\n", (status & SPCS_COPYRIGHT) ? "yes" : "no");
		snd_iprintf(buffer, "Emphasis              : %s\n", emphasis[(status & SPCS_EMPHASISMASK) >> 3]);
		snd_iprintf(buffer, "Mode                  : %i\n", (status & SPCS_MODEMASK) >> 6);
		snd_iprintf(buffer, "Category Code         : 0x%x\n", (status & SPCS_CATEGORYCODEMASK) >> 8);
		snd_iprintf(buffer, "Generation Status     : %s\n", status & SPCS_GENERATIONSTATUS ? "original" : "copy");
		snd_iprintf(buffer, "Source Mask           : %i\n", (status & SPCS_SOURCENUMMASK) >> 16);
		snd_iprintf(buffer, "Channel Number        : %s\n", channel[(status & SPCS_CHANNELNUMMASK) >> 20]);
		snd_iprintf(buffer, "Sample Rate           : %iHz\n", samplerate[(status & SPCS_SAMPLERATEMASK) >> 24]);
		snd_iprintf(buffer, "Clock Accuracy        : %s\n", clkaccy[(status & SPCS_CLKACCYMASK) >> 28]);

		if (rate_reg > 0) {
			rate = snd_emu10k1_ptr_read(emu, rate_reg, 0);
			snd_iprintf(buffer, "S/PDIF Valid          : %s\n", rate & SRCS_SPDIFVALID ? "on" : "off");
			snd_iprintf(buffer, "S/PDIF Locked         : %s\n", rate & SRCS_SPDIFLOCKED ? "on" : "off");
			snd_iprintf(buffer, "Rate Locked           : %s\n", rate & SRCS_RATELOCKED ? "on" : "off");
			/* From ((Rate * 48000 ) / 262144); */
			snd_iprintf(buffer, "Estimated Sample Rate : %d\n", ((rate & 0xFFFFF ) * 375) >> 11); 
		}
	} else {
		snd_iprintf(buffer, "No signal detected.\n");
	}

}

static void snd_emu10k1_proc_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	/* FIXME - output names are in emufx.c too */
	static char *creative_outs[32] = {
		/* 00 */ "AC97 Left",
		/* 01 */ "AC97 Right",
		/* 02 */ "Optical IEC958 Left",
		/* 03 */ "Optical IEC958 Right",
		/* 04 */ "Center",
		/* 05 */ "LFE",
		/* 06 */ "Headphone Left",
		/* 07 */ "Headphone Right",
		/* 08 */ "Surround Left",
		/* 09 */ "Surround Right",
		/* 10 */ "PCM Capture Left",
		/* 11 */ "PCM Capture Right",
		/* 12 */ "MIC Capture",
		/* 13 */ "AC97 Surround Left",
		/* 14 */ "AC97 Surround Right",
		/* 15 */ "???",
		/* 16 */ "???",
		/* 17 */ "Analog Center",
		/* 18 */ "Analog LFE",
		/* 19 */ "???",
		/* 20 */ "???",
		/* 21 */ "???",
		/* 22 */ "???",
		/* 23 */ "???",
		/* 24 */ "???",
		/* 25 */ "???",
		/* 26 */ "???",
		/* 27 */ "???",
		/* 28 */ "???",
		/* 29 */ "???",
		/* 30 */ "???",
		/* 31 */ "???"
	};

	static char *audigy_outs[64] = {
		/* 00 */ "Digital Front Left",
		/* 01 */ "Digital Front Right",
		/* 02 */ "Digital Center",
		/* 03 */ "Digital LEF",
		/* 04 */ "Headphone Left",
		/* 05 */ "Headphone Right",
		/* 06 */ "Digital Rear Left",
		/* 07 */ "Digital Rear Right",
		/* 08 */ "Front Left",
		/* 09 */ "Front Right",
		/* 10 */ "Center",
		/* 11 */ "LFE",
		/* 12 */ "???",
		/* 13 */ "???",
		/* 14 */ "Rear Left",
		/* 15 */ "Rear Right",
		/* 16 */ "AC97 Front Left",
		/* 17 */ "AC97 Front Right",
		/* 18 */ "ADC Capture Left",
		/* 19 */ "ADC Capture Right",
		/* 20 */ "???",
		/* 21 */ "???",
		/* 22 */ "???",
		/* 23 */ "???",
		/* 24 */ "???",
		/* 25 */ "???",
		/* 26 */ "???",
		/* 27 */ "???",
		/* 28 */ "???",
		/* 29 */ "???",
		/* 30 */ "???",
		/* 31 */ "???",
		/* 32 */ "FXBUS2_0",
		/* 33 */ "FXBUS2_1",
		/* 34 */ "FXBUS2_2",
		/* 35 */ "FXBUS2_3",
		/* 36 */ "FXBUS2_4",
		/* 37 */ "FXBUS2_5",
		/* 38 */ "FXBUS2_6",
		/* 39 */ "FXBUS2_7",
		/* 40 */ "FXBUS2_8",
		/* 41 */ "FXBUS2_9",
		/* 42 */ "FXBUS2_10",
		/* 43 */ "FXBUS2_11",
		/* 44 */ "FXBUS2_12",
		/* 45 */ "FXBUS2_13",
		/* 46 */ "FXBUS2_14",
		/* 47 */ "FXBUS2_15",
		/* 48 */ "FXBUS2_16",
		/* 49 */ "FXBUS2_17",
		/* 50 */ "FXBUS2_18",
		/* 51 */ "FXBUS2_19",
		/* 52 */ "FXBUS2_20",
		/* 53 */ "FXBUS2_21",
		/* 54 */ "FXBUS2_22",
		/* 55 */ "FXBUS2_23",
		/* 56 */ "FXBUS2_24",
		/* 57 */ "FXBUS2_25",
		/* 58 */ "FXBUS2_26",
		/* 59 */ "FXBUS2_27",
		/* 60 */ "FXBUS2_28",
		/* 61 */ "FXBUS2_29",
		/* 62 */ "FXBUS2_30",
		/* 63 */ "FXBUS2_31"
	};

	struct snd_emu10k1 *emu = entry->private_data;
	unsigned int val, val1;
	int nefx = emu->audigy ? 64 : 32;
	char **outputs = emu->audigy ? audigy_outs : creative_outs;
	int idx;
	
	snd_iprintf(buffer, "EMU10K1\n\n");
	snd_iprintf(buffer, "Card                  : %s\n",
		    emu->audigy ? "Audigy" : (emu->card_capabilities->ecard ? "EMU APS" : "Creative"));
	snd_iprintf(buffer, "Internal TRAM (words) : 0x%x\n", emu->fx8010.itram_size);
	snd_iprintf(buffer, "External TRAM (words) : 0x%x\n", (int)emu->fx8010.etram_pages.bytes / 2);
	snd_iprintf(buffer, "\n");
	snd_iprintf(buffer, "Effect Send Routing   :\n");
	for (idx = 0; idx < NUM_G; idx++) {
		val = emu->audigy ?
			snd_emu10k1_ptr_read(emu, A_FXRT1, idx) :
			snd_emu10k1_ptr_read(emu, FXRT, idx);
		val1 = emu->audigy ?
			snd_emu10k1_ptr_read(emu, A_FXRT2, idx) :
			0;
		if (emu->audigy) {
			snd_iprintf(buffer, "Ch%i: A=%i, B=%i, C=%i, D=%i, ",
				idx,
				val & 0x3f,
				(val >> 8) & 0x3f,
				(val >> 16) & 0x3f,
				(val >> 24) & 0x3f);
			snd_iprintf(buffer, "E=%i, F=%i, G=%i, H=%i\n",
				val1 & 0x3f,
				(val1 >> 8) & 0x3f,
				(val1 >> 16) & 0x3f,
				(val1 >> 24) & 0x3f);
		} else {
			snd_iprintf(buffer, "Ch%i: A=%i, B=%i, C=%i, D=%i\n",
				idx,
				(val >> 16) & 0x0f,
				(val >> 20) & 0x0f,
				(val >> 24) & 0x0f,
				(val >> 28) & 0x0f);
		}
	}
	snd_iprintf(buffer, "\nCaptured FX Outputs   :\n");
	for (idx = 0; idx < nefx; idx++) {
		if (emu->efx_voices_mask[idx/32] & (1 << (idx%32)))
			snd_iprintf(buffer, "  Output %02i [%s]\n", idx, outputs[idx]);
	}
	snd_iprintf(buffer, "\nAll FX Outputs        :\n");
	for (idx = 0; idx < (emu->audigy ? 64 : 32); idx++)
		snd_iprintf(buffer, "  Output %02i [%s]\n", idx, outputs[idx]);
}

static void snd_emu10k1_proc_spdif_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	u32 value;
	u32 value2;
	u32 rate;

	if (emu->card_capabilities->emu_model) {
		snd_emu1010_fpga_read(emu, 0x38, &value);
		if ((value & 0x1) == 0) {
			snd_emu1010_fpga_read(emu, 0x2a, &value);
			snd_emu1010_fpga_read(emu, 0x2b, &value2);
			rate = 0x1770000 / (((value << 5) | value2)+1);	
			snd_iprintf(buffer, "ADAT Locked : %u\n", rate);
		} else {
			snd_iprintf(buffer, "ADAT Unlocked\n");
		}
		snd_emu1010_fpga_read(emu, 0x20, &value);
		if ((value & 0x4) == 0) {
			snd_emu1010_fpga_read(emu, 0x28, &value);
			snd_emu1010_fpga_read(emu, 0x29, &value2);
			rate = 0x1770000 / (((value << 5) | value2)+1);	
			snd_iprintf(buffer, "SPDIF Locked : %d\n", rate);
		} else {
			snd_iprintf(buffer, "SPDIF Unlocked\n");
		}
	} else {
		snd_emu10k1_proc_spdif_status(emu, buffer, "CD-ROM S/PDIF In", CDCS, CDSRCS);
		snd_emu10k1_proc_spdif_status(emu, buffer, "Optical or Coax S/PDIF In", GPSCS, GPSRCS);
	}
#if 0
	val = snd_emu10k1_ptr_read(emu, ZVSRCS, 0);
	snd_iprintf(buffer, "\nZoomed Video\n");
	snd_iprintf(buffer, "Rate Locked           : %s\n", val & SRCS_RATELOCKED ? "on" : "off");
	snd_iprintf(buffer, "Estimated Sample Rate : 0x%x\n", val & SRCS_ESTSAMPLERATE);
#endif
}

static void snd_emu10k1_proc_rates_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	static int samplerate[8] = { 44100, 48000, 96000, 192000, 4, 5, 6, 7 };
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned int val, tmp, n;
	val = snd_emu10k1_ptr20_read(emu, CAPTURE_RATE_STATUS, 0);
	for (n = 0; n < 4; n++) {
		tmp = val >> (16 + (n*4));
		if (tmp & 0x8) snd_iprintf(buffer, "Channel %d: Rate=%d\n", n, samplerate[tmp & 0x7]);
		else snd_iprintf(buffer, "Channel %d: No input\n", n);
	}
}

static void snd_emu10k1_proc_acode_read(struct snd_info_entry *entry, 
				        struct snd_info_buffer *buffer)
{
	u32 pc;
	struct snd_emu10k1 *emu = entry->private_data;

	snd_iprintf(buffer, "FX8010 Instruction List '%s'\n", emu->fx8010.name);
	snd_iprintf(buffer, "  Code dump      :\n");
	for (pc = 0; pc < (emu->audigy ? 1024 : 512); pc++) {
		u32 low, high;
			
		low = snd_emu10k1_efx_read(emu, pc * 2);
		high = snd_emu10k1_efx_read(emu, pc * 2 + 1);
		if (emu->audigy)
			snd_iprintf(buffer, "    OP(0x%02x, 0x%03x, 0x%03x, 0x%03x, 0x%03x) /* 0x%04x: 0x%08x%08x */\n",
				    (high >> 24) & 0x0f,
				    (high >> 12) & 0x7ff,
				    (high >> 0) & 0x7ff,
				    (low >> 12) & 0x7ff,
				    (low >> 0) & 0x7ff,
				    pc,
				    high, low);
		else
			snd_iprintf(buffer, "    OP(0x%02x, 0x%03x, 0x%03x, 0x%03x, 0x%03x) /* 0x%04x: 0x%08x%08x */\n",
				    (high >> 20) & 0x0f,
				    (high >> 10) & 0x3ff,
				    (high >> 0) & 0x3ff,
				    (low >> 10) & 0x3ff,
				    (low >> 0) & 0x3ff,
				    pc,
				    high, low);
	}
}

#define TOTAL_SIZE_GPR		(0x100*4)
#define A_TOTAL_SIZE_GPR	(0x200*4)
#define TOTAL_SIZE_TANKMEM_DATA	(0xa0*4)
#define TOTAL_SIZE_TANKMEM_ADDR (0xa0*4)
#define A_TOTAL_SIZE_TANKMEM_DATA (0x100*4)
#define A_TOTAL_SIZE_TANKMEM_ADDR (0x100*4)
#define TOTAL_SIZE_CODE		(0x200*8)
#define A_TOTAL_SIZE_CODE	(0x400*8)

static ssize_t snd_emu10k1_fx8010_read(struct snd_info_entry *entry,
				       void *file_private_data,
				       struct file *file, char __user *buf,
				       size_t count, loff_t pos)
{
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned int offset;
	int tram_addr = 0;
	unsigned int *tmp;
	long res;
	unsigned int idx;
	
	if (!strcmp(entry->name, "fx8010_tram_addr")) {
		offset = TANKMEMADDRREGBASE;
		tram_addr = 1;
	} else if (!strcmp(entry->name, "fx8010_tram_data")) {
		offset = TANKMEMDATAREGBASE;
	} else if (!strcmp(entry->name, "fx8010_code")) {
		offset = emu->audigy ? A_MICROCODEBASE : MICROCODEBASE;
	} else {
		offset = emu->audigy ? A_FXGPREGBASE : FXGPREGBASE;
	}

	tmp = kmalloc(count + 8, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	for (idx = 0; idx < ((pos & 3) + count + 3) >> 2; idx++) {
		unsigned int val;
		val = snd_emu10k1_ptr_read(emu, offset + idx + (pos >> 2), 0);
		if (tram_addr && emu->audigy) {
			val >>= 11;
			val |= snd_emu10k1_ptr_read(emu, 0x100 + idx + (pos >> 2), 0) << 20;
		}
		tmp[idx] = val;
	}
	if (copy_to_user(buf, ((char *)tmp) + (pos & 3), count))
		res = -EFAULT;
	else
		res = count;
	kfree(tmp);
	return res;
}

static void snd_emu10k1_proc_voices_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	struct snd_emu10k1_voice *voice;
	int idx;
	
	snd_iprintf(buffer, "ch\tuse\tpcm\tefx\tsynth\tmidi\n");
	for (idx = 0; idx < NUM_G; idx++) {
		voice = &emu->voices[idx];
		snd_iprintf(buffer, "%i\t%i\t%i\t%i\t%i\t%i\n",
			idx,
			voice->use,
			voice->pcm,
			voice->efx,
			voice->synth,
			voice->midi);
	}
}

#ifdef CONFIG_SND_DEBUG
static void snd_emu_proc_emu1010_reg_read(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	u32 value;
	int i;
	snd_iprintf(buffer, "EMU1010 Registers:\n\n");

	for(i = 0; i < 0x40; i+=1) {
		snd_emu1010_fpga_read(emu, i, &value);
		snd_iprintf(buffer, "%02X: %08X, %02X\n", i, value, (value >> 8) & 0x7f);
	}
}

static void snd_emu_proc_io_reg_read(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned long value;
	unsigned long flags;
	int i;
	snd_iprintf(buffer, "IO Registers:\n\n");
	for(i = 0; i < 0x40; i+=4) {
		spin_lock_irqsave(&emu->emu_lock, flags);
		value = inl(emu->port + i);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		snd_iprintf(buffer, "%02X: %08lX\n", i, value);
	}
}

static void snd_emu_proc_io_reg_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned long flags;
	char line[64];
	u32 reg, val;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		if (reg < 0x40 && val <= 0xffffffff) {
			spin_lock_irqsave(&emu->emu_lock, flags);
			outl(val, emu->port + (reg & 0xfffffffc));
			spin_unlock_irqrestore(&emu->emu_lock, flags);
		}
	}
}

static unsigned int snd_ptr_read(struct snd_emu10k1 * emu,
				 unsigned int iobase,
				 unsigned int reg,
				 unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + iobase + PTR);
	val = inl(emu->port + iobase + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

static void snd_ptr_write(struct snd_emu10k1 *emu,
			  unsigned int iobase,
			  unsigned int reg,
			  unsigned int chn,
			  unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + iobase + PTR);
	outl(data, emu->port + iobase + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}


static void snd_emu_proc_ptr_reg_read(struct snd_info_entry *entry,
				      struct snd_info_buffer *buffer, int iobase, int offset, int length, int voices)
{
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned long value;
	int i,j;
	if (offset+length > 0xa0) {
		snd_iprintf(buffer, "Input values out of range\n");
		return;
	}
	snd_iprintf(buffer, "Registers 0x%x\n", iobase);
	for(i = offset; i < offset+length; i++) {
		snd_iprintf(buffer, "%02X: ",i);
		for (j = 0; j < voices; j++) {
			if(iobase == 0)
                		value = snd_ptr_read(emu, 0, i, j);
			else
                		value = snd_ptr_read(emu, 0x20, i, j);
			snd_iprintf(buffer, "%08lX ", value);
		}
		snd_iprintf(buffer, "\n");
	}
}

static void snd_emu_proc_ptr_reg_write(struct snd_info_entry *entry,
				       struct snd_info_buffer *buffer, int iobase)
{
	struct snd_emu10k1 *emu = entry->private_data;
	char line[64];
	unsigned int reg, channel_id , val;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x %x", &reg, &channel_id, &val) != 3)
			continue;
		if (reg < 0xa0 && val <= 0xffffffff && channel_id <= 3)
			snd_ptr_write(emu, iobase, reg, channel_id, val);
	}
}

static void snd_emu_proc_ptr_reg_write00(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_write(entry, buffer, 0);
}

static void snd_emu_proc_ptr_reg_write20(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_write(entry, buffer, 0x20);
}
	

static void snd_emu_proc_ptr_reg_read00a(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_read(entry, buffer, 0, 0, 0x40, 64);
}

static void snd_emu_proc_ptr_reg_read00b(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_read(entry, buffer, 0, 0x40, 0x40, 64);
}

static void snd_emu_proc_ptr_reg_read20a(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_read(entry, buffer, 0x20, 0, 0x40, 4);
}

static void snd_emu_proc_ptr_reg_read20b(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_read(entry, buffer, 0x20, 0x40, 0x40, 4);
}

static void snd_emu_proc_ptr_reg_read20c(struct snd_info_entry *entry,
					 struct snd_info_buffer * buffer)
{
	snd_emu_proc_ptr_reg_read(entry, buffer, 0x20, 0x80, 0x20, 4);
}
#endif

static struct snd_info_entry_ops snd_emu10k1_proc_ops_fx8010 = {
	.read = snd_emu10k1_fx8010_read,
};

int snd_emu10k1_proc_init(struct snd_emu10k1 *emu)
{
	struct snd_info_entry *entry;
#ifdef CONFIG_SND_DEBUG
	if (emu->card_capabilities->emu_model) {
		snd_card_ro_proc_new(emu->card, "emu1010_regs",
				     emu, snd_emu_proc_emu1010_reg_read);
	}
	snd_card_rw_proc_new(emu->card, "io_regs", emu,
			     snd_emu_proc_io_reg_read,
			     snd_emu_proc_io_reg_write);
	snd_card_rw_proc_new(emu->card, "ptr_regs00a", emu,
			     snd_emu_proc_ptr_reg_read00a,
			     snd_emu_proc_ptr_reg_write00);
	snd_card_rw_proc_new(emu->card, "ptr_regs00b", emu,
			     snd_emu_proc_ptr_reg_read00b,
			     snd_emu_proc_ptr_reg_write00);
	snd_card_rw_proc_new(emu->card, "ptr_regs20a", emu,
			     snd_emu_proc_ptr_reg_read20a,
			     snd_emu_proc_ptr_reg_write20);
	snd_card_rw_proc_new(emu->card, "ptr_regs20b", emu,
			     snd_emu_proc_ptr_reg_read20b,
			     snd_emu_proc_ptr_reg_write20);
	snd_card_rw_proc_new(emu->card, "ptr_regs20c", emu,
			     snd_emu_proc_ptr_reg_read20c,
			     snd_emu_proc_ptr_reg_write20);
#endif
	
	snd_card_ro_proc_new(emu->card, "emu10k1", emu, snd_emu10k1_proc_read);

	if (emu->card_capabilities->emu10k2_chip)
		snd_card_ro_proc_new(emu->card, "spdif-in", emu,
				     snd_emu10k1_proc_spdif_read);
	if (emu->card_capabilities->ca0151_chip)
		snd_card_ro_proc_new(emu->card, "capture-rates", emu,
				     snd_emu10k1_proc_rates_read);

	snd_card_ro_proc_new(emu->card, "voices", emu,
			     snd_emu10k1_proc_voices_read);

	if (! snd_card_proc_new(emu->card, "fx8010_gpr", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | 0444 /*| S_IWUSR*/;
		entry->size = emu->audigy ? A_TOTAL_SIZE_GPR : TOTAL_SIZE_GPR;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
	}
	if (! snd_card_proc_new(emu->card, "fx8010_tram_data", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | 0444 /*| S_IWUSR*/;
		entry->size = emu->audigy ? A_TOTAL_SIZE_TANKMEM_DATA : TOTAL_SIZE_TANKMEM_DATA ;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
	}
	if (! snd_card_proc_new(emu->card, "fx8010_tram_addr", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | 0444 /*| S_IWUSR*/;
		entry->size = emu->audigy ? A_TOTAL_SIZE_TANKMEM_ADDR : TOTAL_SIZE_TANKMEM_ADDR ;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
	}
	if (! snd_card_proc_new(emu->card, "fx8010_code", &entry)) {
		entry->content = SNDRV_INFO_CONTENT_DATA;
		entry->private_data = emu;
		entry->mode = S_IFREG | 0444 /*| S_IWUSR*/;
		entry->size = emu->audigy ? A_TOTAL_SIZE_CODE : TOTAL_SIZE_CODE;
		entry->c.ops = &snd_emu10k1_proc_ops_fx8010;
	}
	snd_card_ro_proc_new(emu->card, "fx8010_acode", emu,
			     snd_emu10k1_proc_acode_read);
	return 0;
}
