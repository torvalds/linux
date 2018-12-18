/*
 *  Patch transfer callback for Emu10k1
 *
 *  Copyright (C) 2000 Takashi iwai <tiwai@suse.de>
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
/*
 * All the code for loading in a patch.  There is very little that is
 * chip specific here.  Just the actual writing to the board.
 */

#include "emu10k1_synth_local.h"

/*
 */
#define BLANK_LOOP_START	4
#define BLANK_LOOP_END		8
#define BLANK_LOOP_SIZE		12
#define BLANK_HEAD_SIZE		32

/*
 * allocate a sample block and copy data from userspace
 */
int
snd_emu10k1_sample_new(struct snd_emux *rec, struct snd_sf_sample *sp,
		       struct snd_util_memhdr *hdr,
		       const void __user *data, long count)
{
	int offset;
	int truesize, size, loopsize, blocksize;
	int loopend, sampleend;
	unsigned int start_addr;
	struct snd_emu10k1 *emu;

	emu = rec->hw;
	if (snd_BUG_ON(!sp || !hdr))
		return -EINVAL;

	if (sp->v.size == 0) {
		dev_dbg(emu->card->dev,
			"emu: rom font for sample %d\n", sp->v.sample);
		return 0;
	}

	/* recalculate address offset */
	sp->v.end -= sp->v.start;
	sp->v.loopstart -= sp->v.start;
	sp->v.loopend -= sp->v.start;
	sp->v.start = 0;

	/* some samples have invalid data.  the addresses are corrected in voice info */
	sampleend = sp->v.end;
	if (sampleend > sp->v.size)
		sampleend = sp->v.size;
	loopend = sp->v.loopend;
	if (loopend > sampleend)
		loopend = sampleend;

	/* be sure loop points start < end */
	if (sp->v.loopstart >= sp->v.loopend)
		swap(sp->v.loopstart, sp->v.loopend);

	/* compute true data size to be loaded */
	truesize = sp->v.size + BLANK_HEAD_SIZE;
	loopsize = 0;
#if 0 /* not supported */
	if (sp->v.mode_flags & (SNDRV_SFNT_SAMPLE_BIDIR_LOOP|SNDRV_SFNT_SAMPLE_REVERSE_LOOP))
		loopsize = sp->v.loopend - sp->v.loopstart;
	truesize += loopsize;
#endif
	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_NO_BLANK)
		truesize += BLANK_LOOP_SIZE;

	/* try to allocate a memory block */
	blocksize = truesize;
	if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS))
		blocksize *= 2;
	sp->block = snd_emu10k1_synth_alloc(emu, blocksize);
	if (sp->block == NULL) {
		dev_dbg(emu->card->dev,
			"synth malloc failed (size=%d)\n", blocksize);
		/* not ENOMEM (for compatibility with OSS) */
		return -ENOSPC;
	}
	/* set the total size */
	sp->v.truesize = blocksize;

	/* write blank samples at head */
	offset = 0;
	size = BLANK_HEAD_SIZE;
	if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS))
		size *= 2;
	if (offset + size > blocksize)
		return -EINVAL;
	snd_emu10k1_synth_bzero(emu, sp->block, offset, size);
	offset += size;

	/* copy start->loopend */
	size = loopend;
	if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS))
		size *= 2;
	if (offset + size > blocksize)
		return -EINVAL;
	if (snd_emu10k1_synth_copy_from_user(emu, sp->block, offset, data, size)) {
		snd_emu10k1_synth_free(emu, sp->block);
		sp->block = NULL;
		return -EFAULT;
	}
	offset += size;
	data += size;

#if 0 /* not supported yet */
	/* handle reverse (or bidirectional) loop */
	if (sp->v.mode_flags & (SNDRV_SFNT_SAMPLE_BIDIR_LOOP|SNDRV_SFNT_SAMPLE_REVERSE_LOOP)) {
		/* copy loop in reverse */
		if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS)) {
			int woffset;
			unsigned short *wblock = (unsigned short*)block;
			woffset = offset / 2;
			if (offset + loopsize * 2 > blocksize)
				return -EINVAL;
			for (i = 0; i < loopsize; i++)
				wblock[woffset + i] = wblock[woffset - i -1];
			offset += loopsize * 2;
		} else {
			if (offset + loopsize > blocksize)
				return -EINVAL;
			for (i = 0; i < loopsize; i++)
				block[offset + i] = block[offset - i -1];
			offset += loopsize;
		}

		/* modify loop pointers */
		if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_BIDIR_LOOP) {
			sp->v.loopend += loopsize;
		} else {
			sp->v.loopstart += loopsize;
			sp->v.loopend += loopsize;
		}
		/* add sample pointer */
		sp->v.end += loopsize;
	}
#endif

	/* loopend -> sample end */
	size = sp->v.size - loopend;
	if (size < 0)
		return -EINVAL;
	if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS))
		size *= 2;
	if (snd_emu10k1_synth_copy_from_user(emu, sp->block, offset, data, size)) {
		snd_emu10k1_synth_free(emu, sp->block);
		sp->block = NULL;
		return -EFAULT;
	}
	offset += size;

	/* clear rest of samples (if any) */
	if (offset < blocksize)
		snd_emu10k1_synth_bzero(emu, sp->block, offset, blocksize - offset);

	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_NO_BLANK) {
		/* if no blank loop is attached in the sample, add it */
		if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_SINGLESHOT) {
			sp->v.loopstart = sp->v.end + BLANK_LOOP_START;
			sp->v.loopend = sp->v.end + BLANK_LOOP_END;
		}
	}

#if 0 /* not supported yet */
	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_UNSIGNED) {
		/* unsigned -> signed */
		if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS)) {
			unsigned short *wblock = (unsigned short*)block;
			for (i = 0; i < truesize; i++)
				wblock[i] ^= 0x8000;
		} else {
			for (i = 0; i < truesize; i++)
				block[i] ^= 0x80;
		}
	}
#endif

	/* recalculate offset */
	start_addr = BLANK_HEAD_SIZE * 2;
	if (! (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS))
		start_addr >>= 1;
	sp->v.start += start_addr;
	sp->v.end += start_addr;
	sp->v.loopstart += start_addr;
	sp->v.loopend += start_addr;

	return 0;
}

/*
 * free a sample block
 */
int
snd_emu10k1_sample_free(struct snd_emux *rec, struct snd_sf_sample *sp,
			struct snd_util_memhdr *hdr)
{
	struct snd_emu10k1 *emu;

	emu = rec->hw;
	if (snd_BUG_ON(!sp || !hdr))
		return -EINVAL;

	if (sp->block) {
		snd_emu10k1_synth_free(emu, sp->block);
		sp->block = NULL;
	}
	return 0;
}

