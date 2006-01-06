/*
 *  Patch routines for the emu8000 (AWE32/64)
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
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
 */

#include "emu8000_local.h"
#include <asm/uaccess.h>
#include <linux/moduleparam.h>

static int emu8000_reset_addr = 0;
module_param(emu8000_reset_addr, int, 0444);
MODULE_PARM_DESC(emu8000_reset_addr, "reset write address at each time (makes slowdown)");


/*
 * Open up channels.
 */
static int
snd_emu8000_open_dma(struct snd_emu8000 *emu, int write)
{
	int i;

	/* reserve all 30 voices for loading */
	for (i = 0; i < EMU8000_DRAM_VOICES; i++) {
		snd_emux_lock_voice(emu->emu, i);
		snd_emu8000_dma_chan(emu, i, write);
	}

	/* assign voice 31 and 32 to ROM */
	EMU8000_VTFT_WRITE(emu, 30, 0);
	EMU8000_PSST_WRITE(emu, 30, 0x1d8);
	EMU8000_CSL_WRITE(emu, 30, 0x1e0);
	EMU8000_CCCA_WRITE(emu, 30, 0x1d8);
	EMU8000_VTFT_WRITE(emu, 31, 0);
	EMU8000_PSST_WRITE(emu, 31, 0x1d8);
	EMU8000_CSL_WRITE(emu, 31, 0x1e0);
	EMU8000_CCCA_WRITE(emu, 31, 0x1d8);

	return 0;
}

/*
 * Close all dram channels.
 */
static void
snd_emu8000_close_dma(struct snd_emu8000 *emu)
{
	int i;

	for (i = 0; i < EMU8000_DRAM_VOICES; i++) {
		snd_emu8000_dma_chan(emu, i, EMU8000_RAM_CLOSE);
		snd_emux_unlock_voice(emu->emu, i);
	}
}

/*
 */

#define BLANK_LOOP_START	4
#define BLANK_LOOP_END		8
#define BLANK_LOOP_SIZE		12
#define BLANK_HEAD_SIZE		48

/*
 * Read a word from userland, taking care of conversions from
 * 8bit samples etc.
 */
static unsigned short
read_word(const void __user *buf, int offset, int mode)
{
	unsigned short c;
	if (mode & SNDRV_SFNT_SAMPLE_8BITS) {
		unsigned char cc;
		get_user(cc, (unsigned char __user *)buf + offset);
		c = cc << 8; /* convert 8bit -> 16bit */
	} else {
#ifdef SNDRV_LITTLE_ENDIAN
		get_user(c, (unsigned short __user *)buf + offset);
#else
		unsigned short cc;
		get_user(cc, (unsigned short __user *)buf + offset);
		c = swab16(cc);
#endif
	}
	if (mode & SNDRV_SFNT_SAMPLE_UNSIGNED)
		c ^= 0x8000; /* unsigned -> signed */
	return c;
}

/*
 */
static void
snd_emu8000_write_wait(struct snd_emu8000 *emu)
{
	while ((EMU8000_SMALW_READ(emu) & 0x80000000) != 0) {
		schedule_timeout_interruptible(1);
		if (signal_pending(current))
			break;
	}
}

/*
 * write sample word data
 *
 * You should not have to keep resetting the address each time
 * as the chip is supposed to step on the next address automatically.
 * It mostly does, but during writes of some samples at random it
 * completely loses words (every one in 16 roughly but with no
 * obvious pattern).
 *
 * This is therefore much slower than need be, but is at least
 * working.
 */
static inline void
write_word(struct snd_emu8000 *emu, int *offset, unsigned short data)
{
	if (emu8000_reset_addr) {
		if (emu8000_reset_addr > 1)
			snd_emu8000_write_wait(emu);
		EMU8000_SMALW_WRITE(emu, *offset);
	}
	EMU8000_SMLD_WRITE(emu, data);
	*offset += 1;
}

/*
 * Write the sample to EMU800 memory.  This routine is invoked out of
 * the generic soundfont routines as a callback.
 */
int
snd_emu8000_sample_new(struct snd_emux *rec, struct snd_sf_sample *sp,
		       struct snd_util_memhdr *hdr,
		       const void __user *data, long count)
{
	int  i;
	int  rc;
	int  offset;
	int  truesize;
	int  dram_offset, dram_start;
	struct snd_emu8000 *emu;

	emu = rec->hw;
	snd_assert(sp != NULL, return -EINVAL);

	if (sp->v.size == 0)
		return 0;

	/* be sure loop points start < end */
	if (sp->v.loopstart > sp->v.loopend) {
		int tmp = sp->v.loopstart;
		sp->v.loopstart = sp->v.loopend;
		sp->v.loopend = tmp;
	}

	/* compute true data size to be loaded */
	truesize = sp->v.size;
	if (sp->v.mode_flags & (SNDRV_SFNT_SAMPLE_BIDIR_LOOP|SNDRV_SFNT_SAMPLE_REVERSE_LOOP))
		truesize += sp->v.loopend - sp->v.loopstart;
	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_NO_BLANK)
		truesize += BLANK_LOOP_SIZE;

	sp->block = snd_util_mem_alloc(hdr, truesize * 2);
	if (sp->block == NULL) {
		/*snd_printd("EMU8000: out of memory\n");*/
		/* not ENOMEM (for compatibility) */
		return -ENOSPC;
	}

	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS) {
		if (!access_ok(VERIFY_READ, data, sp->v.size))
			return -EFAULT;
	} else {
		if (!access_ok(VERIFY_READ, data, sp->v.size * 2))
			return -EFAULT;
	}

	/* recalculate address offset */
	sp->v.end -= sp->v.start;
	sp->v.loopstart -= sp->v.start;
	sp->v.loopend -= sp->v.start;
	sp->v.start = 0;

	/* dram position (in word) -- mem_offset is byte */
	dram_offset = EMU8000_DRAM_OFFSET + (sp->block->offset >> 1);
	dram_start = dram_offset;

	/* set the total size (store onto obsolete checksum value) */
	sp->v.truesize = truesize * 2; /* in bytes */

	snd_emux_terminate_all(emu->emu);
	if ((rc = snd_emu8000_open_dma(emu, EMU8000_RAM_WRITE)) != 0)
		return rc;

	/* Set the address to start writing at */
	snd_emu8000_write_wait(emu);
	EMU8000_SMALW_WRITE(emu, dram_offset);

	/*snd_emu8000_init_fm(emu);*/

#if 0
	/* first block - write 48 samples for silence */
	if (! sp->block->offset) {
		for (i = 0; i < BLANK_HEAD_SIZE; i++) {
			write_word(emu, &dram_offset, 0);
		}
	}
#endif

	offset = 0;
	for (i = 0; i < sp->v.size; i++) {
		unsigned short s;

		s = read_word(data, offset, sp->v.mode_flags);
		offset++;
		write_word(emu, &dram_offset, s);

		/* we may take too long time in this loop.
		 * so give controls back to kernel if needed.
		 */
		cond_resched();

		if (i == sp->v.loopend &&
		    (sp->v.mode_flags & (SNDRV_SFNT_SAMPLE_BIDIR_LOOP|SNDRV_SFNT_SAMPLE_REVERSE_LOOP)))
		{
			int looplen = sp->v.loopend - sp->v.loopstart;
			int k;

			/* copy reverse loop */
			for (k = 1; k <= looplen; k++) {
				s = read_word(data, offset - k, sp->v.mode_flags);
				write_word(emu, &dram_offset, s);
			}
			if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_BIDIR_LOOP) {
				sp->v.loopend += looplen;
			} else {
				sp->v.loopstart += looplen;
				sp->v.loopend += looplen;
			}
			sp->v.end += looplen;
		}
	}

	/* if no blank loop is attached in the sample, add it */
	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_NO_BLANK) {
		for (i = 0; i < BLANK_LOOP_SIZE; i++) {
			write_word(emu, &dram_offset, 0);
		}
		if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_SINGLESHOT) {
			sp->v.loopstart = sp->v.end + BLANK_LOOP_START;
			sp->v.loopend = sp->v.end + BLANK_LOOP_END;
		}
	}

	/* add dram offset */
	sp->v.start += dram_start;
	sp->v.end += dram_start;
	sp->v.loopstart += dram_start;
	sp->v.loopend += dram_start;

	snd_emu8000_close_dma(emu);
	snd_emu8000_init_fm(emu);

	return 0;
}

/*
 * free a sample block
 */
int
snd_emu8000_sample_free(struct snd_emux *rec, struct snd_sf_sample *sp,
			struct snd_util_memhdr *hdr)
{
	if (sp->block) {
		snd_util_mem_free(hdr, sp->block);
		sp->block = NULL;
	}
	return 0;
}


/*
 * sample_reset callback - terminate voices
 */
void
snd_emu8000_sample_reset(struct snd_emux *rec)
{
	snd_emux_terminate_all(rec);
}
