// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Lee Revell <rlrevell@joe-job.com>
 *                   James Courtier-Dutton <James@superbug.co.uk>
 *                   Oswald Buddenhagen <oswald.buddenhagen@gmx.de>
 *                   Creative Labs, Inc.
 *
 *  Routines for control of EMU10K1 chips / proc interface routines
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/string_choices.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include "p16v.h"

static void snd_emu10k1_proc_spdif_status(struct snd_emu10k1 * emu,
					  struct snd_info_buffer *buffer,
					  char *title,
					  int status_reg,
					  int rate_reg)
{
	static const char * const clkaccy[4] = { "1000ppm", "50ppm", "variable", "unknown" };
	static const int samplerate[16] = { 44100, 1, 48000, 32000, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	static const char * const channel[16] = { "unspec", "left", "right", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" };
	static const char * const emphasis[8] = { "none", "50/15 usec 2 channel", "2", "3", "4", "5", "6", "7" };
	unsigned int status, rate = 0;
	
	status = snd_emu10k1_ptr_read(emu, status_reg, 0);

	snd_iprintf(buffer, "\n%s\n", title);

	if (status != 0xffffffff) {
		snd_iprintf(buffer, "Professional Mode     : %s\n", str_yes_no(status & SPCS_PROFESSIONAL));
		snd_iprintf(buffer, "Not Audio Data        : %s\n", str_yes_no(status & SPCS_NOTAUDIODATA));
		snd_iprintf(buffer, "Copyright             : %s\n", str_yes_no(status & SPCS_COPYRIGHT));
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
			snd_iprintf(buffer, "S/PDIF Valid          : %s\n", str_on_off(rate & SRCS_SPDIFVALID));
			snd_iprintf(buffer, "S/PDIF Locked         : %s\n", str_on_off(rate & SRCS_SPDIFLOCKED));
			snd_iprintf(buffer, "Rate Locked           : %s\n", str_on_off(rate & SRCS_RATELOCKED));
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
	struct snd_emu10k1 *emu = entry->private_data;
	const char * const *inputs = emu->audigy ?
		snd_emu10k1_audigy_ins : snd_emu10k1_sblive_ins;
	const char * const *outputs = emu->audigy ?
		snd_emu10k1_audigy_outs : snd_emu10k1_sblive_outs;
	unsigned short extin_mask = emu->audigy ? ~0 : emu->fx8010.extin_mask;
	unsigned short extout_mask = emu->audigy ? ~0 : emu->fx8010.extout_mask;
	unsigned int val, val1, ptrx, psst, dsl, snda;
	int nefx = emu->audigy ? 32 : 16;
	int idx;
	
	snd_iprintf(buffer, "EMU10K1\n\n");
	snd_iprintf(buffer, "Card                  : %s\n",
		    emu->card_capabilities->emu_model ? "E-MU D.A.S." :
		    emu->card_capabilities->ecard ? "E-MU A.P.S." :
		    emu->audigy ? "SB Audigy" : "SB Live!");
	snd_iprintf(buffer, "Internal TRAM (words) : 0x%x\n", emu->fx8010.itram_size);
	snd_iprintf(buffer, "External TRAM (words) : 0x%x\n", (int)emu->fx8010.etram_pages.bytes / 2);

	snd_iprintf(buffer, "\nEffect Send Routing & Amounts:\n");
	for (idx = 0; idx < NUM_G; idx++) {
		ptrx = snd_emu10k1_ptr_read(emu, PTRX, idx);
		psst = snd_emu10k1_ptr_read(emu, PSST, idx);
		dsl = snd_emu10k1_ptr_read(emu, DSL, idx);
		if (emu->audigy) {
			val = snd_emu10k1_ptr_read(emu, A_FXRT1, idx);
			val1 = snd_emu10k1_ptr_read(emu, A_FXRT2, idx);
			snda = snd_emu10k1_ptr_read(emu, A_SENDAMOUNTS, idx);
			snd_iprintf(buffer, "Ch%-2i: A=%2i:%02x, B=%2i:%02x, C=%2i:%02x, D=%2i:%02x, ",
				idx,
				val & 0x3f, REG_VAL_GET(PTRX_FXSENDAMOUNT_A, ptrx),
				(val >> 8) & 0x3f, REG_VAL_GET(PTRX_FXSENDAMOUNT_B, ptrx),
				(val >> 16) & 0x3f, REG_VAL_GET(PSST_FXSENDAMOUNT_C, psst),
				(val >> 24) & 0x3f, REG_VAL_GET(DSL_FXSENDAMOUNT_D, dsl));
			snd_iprintf(buffer, "E=%2i:%02x, F=%2i:%02x, G=%2i:%02x, H=%2i:%02x\n",
				val1 & 0x3f, (snda >> 24) & 0xff,
				(val1 >> 8) & 0x3f, (snda >> 16) & 0xff,
				(val1 >> 16) & 0x3f, (snda >> 8) & 0xff,
				(val1 >> 24) & 0x3f, snda & 0xff);
		} else {
			val = snd_emu10k1_ptr_read(emu, FXRT, idx);
			snd_iprintf(buffer, "Ch%-2i: A=%2i:%02x, B=%2i:%02x, C=%2i:%02x, D=%2i:%02x\n",
				idx,
				(val >> 16) & 0x0f, REG_VAL_GET(PTRX_FXSENDAMOUNT_A, ptrx),
				(val >> 20) & 0x0f, REG_VAL_GET(PTRX_FXSENDAMOUNT_B, ptrx),
				(val >> 24) & 0x0f, REG_VAL_GET(PSST_FXSENDAMOUNT_C, psst),
				(val >> 28) & 0x0f, REG_VAL_GET(DSL_FXSENDAMOUNT_D, dsl));
		}
	}
	snd_iprintf(buffer, "\nEffect Send Targets:\n");
	// Audigy actually has 64, but we don't use them all.
	for (idx = 0; idx < 32; idx++) {
		const char *c = snd_emu10k1_fxbus[idx];
		if (c)
			snd_iprintf(buffer, "  Channel %02i [%s]\n", idx, c);
	}
	if (!emu->card_capabilities->emu_model) {
		snd_iprintf(buffer, "\nOutput Channels:\n");
		for (idx = 0; idx < 32; idx++)
			if (outputs[idx] && (extout_mask & (1 << idx)))
				snd_iprintf(buffer, "  Channel %02i [%s]\n", idx, outputs[idx]);
		snd_iprintf(buffer, "\nInput Channels:\n");
		for (idx = 0; idx < 16; idx++)
			if (inputs[idx] && (extin_mask & (1 << idx)))
				snd_iprintf(buffer, "  Channel %02i [%s]\n", idx, inputs[idx]);
		snd_iprintf(buffer, "\nMultichannel Capture Sources:\n");
		for (idx = 0; idx < nefx; idx++)
			if (emu->efx_voices_mask[0] & (1 << idx))
				snd_iprintf(buffer, "  Channel %02i [Output: %s]\n",
					    idx, outputs[idx] ? outputs[idx] : "???");
		if (emu->audigy) {
			for (idx = 0; idx < 32; idx++)
				if (emu->efx_voices_mask[1] & (1 << idx))
					snd_iprintf(buffer, "  Channel %02i [Input: %s]\n",
						    idx + 32, inputs[idx] ? inputs[idx] : "???");
		} else {
			for (idx = 0; idx < 16; idx++) {
				if (emu->efx_voices_mask[0] & ((1 << 16) << idx)) {
					if (emu->card_capabilities->sblive51) {
						s8 c = snd_emu10k1_sblive51_fxbus2_map[idx];
						if (c == -1)
							snd_iprintf(buffer, "  Channel %02i [Output: %s]\n",
								    idx + 16, outputs[idx + 16]);
						else
							snd_iprintf(buffer, "  Channel %02i [Input: %s]\n",
								    idx + 16, inputs[c]);
					} else {
						snd_iprintf(buffer, "  Channel %02i [Input: %s]\n",
							    idx + 16, inputs[idx] ? inputs[idx] : "???");
					}
				}
			}
		}
	}
}

static void snd_emu10k1_proc_spdif_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	u32 value;
	u32 value2;

	if (emu->card_capabilities->emu_model) {
		snd_emu1010_fpga_lock(emu);

		// This represents the S/PDIF lock status on 0404b, which is
		// kinda weird and unhelpful, because monitoring it via IRQ is
		// impractical (one gets an IRQ flood as long as it is desynced).
		snd_emu1010_fpga_read(emu, EMU_HANA_IRQ_STATUS, &value);
		snd_iprintf(buffer, "Lock status 1: %#x\n", value & 0x10);

		// Bit 0x1 in LO being 0 is supposedly for ADAT lock.
		// The registers are always all zero on 0404b.
		snd_emu1010_fpga_read(emu, EMU_HANA_LOCK_STS_LO, &value);
		snd_emu1010_fpga_read(emu, EMU_HANA_LOCK_STS_HI, &value2);
		snd_iprintf(buffer, "Lock status 2: %#x %#x\n", value, value2);

		snd_iprintf(buffer, "S/PDIF rate: %dHz\n",
			    snd_emu1010_get_raw_rate(emu, EMU_HANA_WCLOCK_HANA_SPDIF_IN));
		if (emu->card_capabilities->emu_model != EMU_MODEL_EMU0404) {
			snd_iprintf(buffer, "ADAT rate: %dHz\n",
				    snd_emu1010_get_raw_rate(emu, EMU_HANA_WCLOCK_HANA_ADAT_IN));
			snd_iprintf(buffer, "Dock rate: %dHz\n",
				    snd_emu1010_get_raw_rate(emu, EMU_HANA_WCLOCK_2ND_HANA));
		}
		if (emu->card_capabilities->emu_model == EMU_MODEL_EMU0404 ||
		    emu->card_capabilities->emu_model == EMU_MODEL_EMU1010)
			snd_iprintf(buffer, "BNC rate: %dHz\n",
				    snd_emu1010_get_raw_rate(emu, EMU_HANA_WCLOCK_SYNC_BNC));

		snd_emu1010_fpga_read(emu, EMU_HANA_SPDIF_MODE, &value);
		if (value & EMU_HANA_SPDIF_MODE_RX_INVALID)
			snd_iprintf(buffer, "\nS/PDIF input invalid\n");
		else
			snd_iprintf(buffer, "\nS/PDIF mode: %s%s\n",
				    value & EMU_HANA_SPDIF_MODE_RX_PRO ? "professional" : "consumer",
				    value & EMU_HANA_SPDIF_MODE_RX_NOCOPY ? ", no copy" : "");

		snd_emu1010_fpga_unlock(emu);
	} else {
		snd_emu10k1_proc_spdif_status(emu, buffer, "CD-ROM S/PDIF In", CDCS, CDSRCS);
		snd_emu10k1_proc_spdif_status(emu, buffer, "Optical or Coax S/PDIF In", GPSCS, GPSRCS);
	}
#if 0
	val = snd_emu10k1_ptr_read(emu, ZVSRCS, 0);
	snd_iprintf(buffer, "\nZoomed Video\n");
	snd_iprintf(buffer, "Rate Locked           : %s\n", str_on_off(val & SRCS_RATELOCKED));
	snd_iprintf(buffer, "Estimated Sample Rate : 0x%x\n", val & SRCS_ESTSAMPLERATE);
#endif
}

static void snd_emu10k1_proc_rates_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	static const int samplerate[8] = { 44100, 48000, 96000, 192000, 4, 5, 6, 7 };
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned int val, tmp, n;
	val = snd_emu10k1_ptr20_read(emu, CAPTURE_RATE_STATUS, 0);
	for (n = 0; n < 4; n++) {
		tmp = val >> (16 + (n*4));
		if (tmp & 0x8) snd_iprintf(buffer, "Channel %d: Rate=%d\n", n, samplerate[tmp & 0x7]);
		else snd_iprintf(buffer, "Channel %d: No input\n", n);
	}
}

struct emu10k1_reg_entry {
	unsigned short base, size;
	const char *name;
};

static const struct emu10k1_reg_entry sblive_reg_entries[] = {
	{    0, 0x10, "FXBUS" },
	{ 0x10, 0x10, "EXTIN" },
	{ 0x20, 0x10, "EXTOUT" },
	{ 0x30, 0x10, "FXBUS2" },
	{ 0x40, 0x20, NULL },  // Constants
	{ 0x100, 0x100, "GPR" },
	{ 0x200, 0x80, "ITRAM_DATA" },
	{ 0x280, 0x20, "ETRAM_DATA" },
	{ 0x300, 0x80, "ITRAM_ADDR" },
	{ 0x380, 0x20, "ETRAM_ADDR" },
	{ 0x400, 0, NULL }
};

static const struct emu10k1_reg_entry audigy_reg_entries[] = {
	{    0, 0x40, "FXBUS" },
	{ 0x40, 0x10, "EXTIN" },
	{ 0x50, 0x10, "P16VIN" },
	{ 0x60, 0x20, "EXTOUT" },
	{ 0x80, 0x20, "FXBUS2" },
	{ 0xa0, 0x10, "EMU32OUTH" },
	{ 0xb0, 0x10, "EMU32OUTL" },
	{ 0xc0, 0x20, NULL },  // Constants
	// This can't be quite right - overlap.
	//{ 0x100, 0xc0, "ITRAM_CTL" },
	//{ 0x1c0, 0x40, "ETRAM_CTL" },
	{ 0x160, 0x20, "A3_EMU32IN" },
	{ 0x1e0, 0x20, "A3_EMU32OUT" },
	{ 0x200, 0xc0, "ITRAM_DATA" },
	{ 0x2c0, 0x40, "ETRAM_DATA" },
	{ 0x300, 0xc0, "ITRAM_ADDR" },
	{ 0x3c0, 0x40, "ETRAM_ADDR" },
	{ 0x400, 0x200, "GPR" },
	{ 0x600, 0, NULL }
};

static const char * const emu10k1_const_entries[] = {
	"C_00000000",
	"C_00000001",
	"C_00000002",
	"C_00000003",
	"C_00000004",
	"C_00000008",
	"C_00000010",
	"C_00000020",
	"C_00000100",
	"C_00010000",
	"C_00000800",
	"C_10000000",
	"C_20000000",
	"C_40000000",
	"C_80000000",
	"C_7fffffff",
	"C_ffffffff",
	"C_fffffffe",
	"C_c0000000",
	"C_4f1bbcdc",
	"C_5a7ef9db",
	"C_00100000",
	"GPR_ACCU",
	"GPR_COND",
	"GPR_NOISE0",
	"GPR_NOISE1",
	"GPR_IRQ",
	"GPR_DBAC",
	"GPR_DBACE",
	"???",
};

static int disasm_emu10k1_reg(char *buffer,
			      const struct emu10k1_reg_entry *entries,
			      unsigned reg, const char *pfx)
{
	for (int i = 0; ; i++) {
		unsigned base = entries[i].base;
		unsigned size = entries[i].size;
		if (!size)
			return sprintf(buffer, "%s0x%03x", pfx, reg);
		if (reg >= base && reg < base + size) {
			const char *name = entries[i].name;
			reg -= base;
			if (name)
				return sprintf(buffer, "%s%s(%u)", pfx, name, reg);
			return sprintf(buffer, "%s%s", pfx, emu10k1_const_entries[reg]);
		}
	}
}

static int disasm_sblive_reg(char *buffer, unsigned reg, const char *pfx)
{
	return disasm_emu10k1_reg(buffer, sblive_reg_entries, reg, pfx);
}

static int disasm_audigy_reg(char *buffer, unsigned reg, const char *pfx)
{
	return disasm_emu10k1_reg(buffer, audigy_reg_entries, reg, pfx);
}

static void snd_emu10k1_proc_acode_read(struct snd_info_entry *entry,
				        struct snd_info_buffer *buffer)
{
	u32 pc;
	struct snd_emu10k1 *emu = entry->private_data;
	static const char * const insns[16] = {
		"MAC0", "MAC1", "MAC2", "MAC3", "MACINT0", "MACINT1", "ACC3", "MACMV",
		"ANDXOR", "TSTNEG", "LIMITGE", "LIMITLT", "LOG", "EXP", "INTERP", "SKIP",
	};
	static const char spaces[] = "                              ";
	const int nspaces = sizeof(spaces) - 1;

	snd_iprintf(buffer, "FX8010 Instruction List '%s'\n", emu->fx8010.name);
	snd_iprintf(buffer, "  Code dump      :\n");
	for (pc = 0; pc < (emu->audigy ? 1024 : 512); pc++) {
		u32 low, high;
		int len;
		char buf[100];
		char *bufp = buf;
			
		low = snd_emu10k1_efx_read(emu, pc * 2);
		high = snd_emu10k1_efx_read(emu, pc * 2 + 1);
		if (emu->audigy) {
			bufp += sprintf(bufp, "    %-7s  ", insns[(high >> 24) & 0x0f]);
			bufp += disasm_audigy_reg(bufp, (high >> 12) & 0x7ff, "");
			bufp += disasm_audigy_reg(bufp, (high >> 0) & 0x7ff, ", ");
			bufp += disasm_audigy_reg(bufp, (low >> 12) & 0x7ff, ", ");
			bufp += disasm_audigy_reg(bufp, (low >> 0) & 0x7ff, ", ");
		} else {
			bufp += sprintf(bufp, "    %-7s  ", insns[(high >> 20) & 0x0f]);
			bufp += disasm_sblive_reg(bufp, (high >> 10) & 0x3ff, "");
			bufp += disasm_sblive_reg(bufp, (high >> 0) & 0x3ff, ", ");
			bufp += disasm_sblive_reg(bufp, (low >> 10) & 0x3ff, ", ");
			bufp += disasm_sblive_reg(bufp, (low >> 0) & 0x3ff, ", ");
		}
		len = (int)(ptrdiff_t)(bufp - buf);
		snd_iprintf(buffer, "%s %s /* 0x%04x: 0x%08x%08x */\n",
			    buf, &spaces[nspaces - clamp(65 - len, 0, nspaces)],
			    pc, high, low);
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
	static const char * const types[] = {
		"Unused", "EFX", "EFX IRQ", "PCM", "PCM IRQ", "Synth"
	};
	static_assert(ARRAY_SIZE(types) == EMU10K1_NUM_TYPES);

	snd_iprintf(buffer, "ch\tdirty\tlast\tuse\n");
	for (idx = 0; idx < NUM_G; idx++) {
		voice = &emu->voices[idx];
		snd_iprintf(buffer, "%i\t%u\t%u\t%s\n",
			idx,
			voice->dirty,
			voice->last,
			types[voice->use]);
	}
}

#ifdef CONFIG_SND_DEBUG

static void snd_emu_proc_emu1010_link_read(struct snd_emu10k1 *emu,
					   struct snd_info_buffer *buffer,
					   u32 dst)
{
	u32 src = snd_emu1010_fpga_link_dst_src_read(emu, dst);
	snd_iprintf(buffer, "%04x: %04x\n", dst, src);
}

static void snd_emu_proc_emu1010_reg_read(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	u32 value;
	int i;

	snd_emu1010_fpga_lock(emu);

	snd_iprintf(buffer, "EMU1010 Registers:\n\n");

	for(i = 0; i < 0x40; i+=1) {
		snd_emu1010_fpga_read(emu, i, &value);
		snd_iprintf(buffer, "%02x: %02x\n", i, value);
	}

	snd_iprintf(buffer, "\nEMU1010 Routes:\n\n");

	for (i = 0; i < 16; i++)  // To Alice2/Tina[2] via EMU32
		snd_emu_proc_emu1010_link_read(emu, buffer, i);
	if (emu->card_capabilities->emu_model != EMU_MODEL_EMU0404)
		for (i = 0; i < 32; i++)  // To Dock via EDI
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x100 + i);
	if (emu->card_capabilities->emu_model != EMU_MODEL_EMU1616)
		for (i = 0; i < 8; i++)  // To Hamoa/local
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x200 + i);
	for (i = 0; i < 8; i++)  // To Hamoa/Mana/local
		snd_emu_proc_emu1010_link_read(emu, buffer, 0x300 + i);
	if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1616) {
		for (i = 0; i < 16; i++)  // To Tina2 via EMU32
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x400 + i);
	} else if (emu->card_capabilities->emu_model != EMU_MODEL_EMU0404) {
		for (i = 0; i < 8; i++)  // To Hana ADAT
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x400 + i);
		if (emu->card_capabilities->emu_model == EMU_MODEL_EMU1010B) {
			for (i = 0; i < 16; i++)  // To Tina via EMU32
				snd_emu_proc_emu1010_link_read(emu, buffer, 0x500 + i);
		} else {
			// To Alice2 via I2S
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x500);
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x501);
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x600);
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x601);
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x700);
			snd_emu_proc_emu1010_link_read(emu, buffer, 0x701);
		}
	}

	snd_emu1010_fpga_unlock(emu);
}

static void snd_emu_proc_io_reg_read(struct snd_info_entry *entry,
				     struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	unsigned long value;
	int i;
	snd_iprintf(buffer, "IO Registers:\n\n");
	for(i = 0; i < 0x40; i+=4) {
		value = inl(emu->port + i);
		snd_iprintf(buffer, "%02X: %08lX\n", i, value);
	}
}

static void snd_emu_proc_io_reg_write(struct snd_info_entry *entry,
                                      struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	char line[64];
	u32 reg, val;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		if (reg < 0x40 && val <= 0xffffffff) {
			outl(val, emu->port + (reg & 0xfffffffc));
		}
	}
}

static unsigned int snd_ptr_read(struct snd_emu10k1 * emu,
				 unsigned int iobase,
				 unsigned int reg,
				 unsigned int chn)
{
	unsigned int regptr, val;

	regptr = (reg << 16) | chn;

	spin_lock_irq(&emu->emu_lock);
	outl(regptr, emu->port + iobase + PTR);
	val = inl(emu->port + iobase + DATA);
	spin_unlock_irq(&emu->emu_lock);
	return val;
}

static void snd_ptr_write(struct snd_emu10k1 *emu,
			  unsigned int iobase,
			  unsigned int reg,
			  unsigned int chn,
			  unsigned int data)
{
	unsigned int regptr;

	regptr = (reg << 16) | chn;

	spin_lock_irq(&emu->emu_lock);
	outl(regptr, emu->port + iobase + PTR);
	outl(data, emu->port + iobase + DATA);
	spin_unlock_irq(&emu->emu_lock);
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
			value = snd_ptr_read(emu, iobase, i, j);
			snd_iprintf(buffer, "%08lX ", value);
		}
		snd_iprintf(buffer, "\n");
	}
}

static void snd_emu_proc_ptr_reg_write(struct snd_info_entry *entry,
				       struct snd_info_buffer *buffer,
				       int iobase, int length, int voices)
{
	struct snd_emu10k1 *emu = entry->private_data;
	char line[64];
	unsigned int reg, channel_id , val;
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x %x", &reg, &channel_id, &val) != 3)
			continue;
		if (reg < length && channel_id < voices)
			snd_ptr_write(emu, iobase, reg, channel_id, val);
	}
}

static void snd_emu_proc_ptr_reg_write00(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	snd_emu_proc_ptr_reg_write(entry, buffer, 0, 0x80, 64);
}

static void snd_emu_proc_ptr_reg_write20(struct snd_info_entry *entry,
					 struct snd_info_buffer *buffer)
{
	struct snd_emu10k1 *emu = entry->private_data;
	snd_emu_proc_ptr_reg_write(entry, buffer, 0x20,
				   emu->card_capabilities->ca0108_chip ? 0xa0 : 0x80, 4);
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

static const struct snd_info_entry_ops snd_emu10k1_proc_ops_fx8010 = {
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
	if (!emu->card_capabilities->emu_model &&
	    (emu->card_capabilities->ca0151_chip || emu->card_capabilities->ca0108_chip)) {
		snd_card_rw_proc_new(emu->card, "ptr_regs20a", emu,
				     snd_emu_proc_ptr_reg_read20a,
				     snd_emu_proc_ptr_reg_write20);
		snd_card_rw_proc_new(emu->card, "ptr_regs20b", emu,
				     snd_emu_proc_ptr_reg_read20b,
				     snd_emu_proc_ptr_reg_write20);
		if (emu->card_capabilities->ca0108_chip)
			snd_card_rw_proc_new(emu->card, "ptr_regs20c", emu,
					     snd_emu_proc_ptr_reg_read20c,
					     snd_emu_proc_ptr_reg_write20);
	}
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
