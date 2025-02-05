// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Patch transfer callback for Emu10k1
 *
 *  Copyright (C) 2000 Takashi iwai <tiwai@suse.de>
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
#define BLANK_HEAD_SIZE		3

/*
 * allocate a sample block and copy data from userspace
 */
int
snd_emu10k1_sample_new(struct snd_emux *rec, struct snd_sf_sample *sp,
		       struct snd_util_memhdr *hdr,
		       const void __user *data, long count)
{
	u8 fill;
	u32 xor;
	int shift;
	int offset;
	int truesize, size, blocksize;
	int loop_start, loop_end, loop_size, data_end, unroll;
	struct snd_emu10k1 *emu;

	emu = rec->hw;
	if (snd_BUG_ON(!sp || !hdr))
		return -EINVAL;

	if (sp->v.mode_flags & (SNDRV_SFNT_SAMPLE_BIDIR_LOOP | SNDRV_SFNT_SAMPLE_REVERSE_LOOP)) {
		/* should instead return -ENOTSUPP; but compatibility */
		dev_warn(emu->card->dev,
			 "Emu10k1 wavetable patch %d with unsupported loop feature\n",
			 sp->v.sample);
	}

	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_8BITS) {
		shift = 0;
		fill = 0x80;
		xor = (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_UNSIGNED) ? 0 : 0x80808080;
	} else {
		shift = 1;
		fill = 0;
		xor = (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_UNSIGNED) ? 0x80008000 : 0;
	}

	/* compute true data size to be loaded */
	truesize = sp->v.size + BLANK_HEAD_SIZE;
	if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_NO_BLANK) {
		truesize += BLANK_LOOP_SIZE;
		/* if no blank loop is attached in the sample, add it */
		if (sp->v.mode_flags & SNDRV_SFNT_SAMPLE_SINGLESHOT) {
			sp->v.loopstart = sp->v.end + BLANK_LOOP_START;
			sp->v.loopend = sp->v.end + BLANK_LOOP_END;
		}
	}

	loop_start = sp->v.loopstart;
	loop_end = sp->v.loopend;
	loop_size = loop_end - loop_start;
	if (!loop_size)
		return -EINVAL;
	data_end = sp->v.end;

	/* recalculate offset */
	sp->v.start += BLANK_HEAD_SIZE;
	sp->v.end += BLANK_HEAD_SIZE;
	sp->v.loopstart += BLANK_HEAD_SIZE;
	sp->v.loopend += BLANK_HEAD_SIZE;

	// Automatic pre-filling of the cache does not work in the presence
	// of loops (*), and we don't want to fill it manually, as that is
	// fiddly and slow. So we unroll the loop until the loop end is
	// beyond the cache size.
	// (*) Strictly speaking, a single iteration is supported (that's
	// how it works when the playback engine runs), but handling this
	// special case is not worth it.
	unroll = 0;
	while (sp->v.loopend < 64) {
		truesize += loop_size;
		sp->v.loopstart += loop_size;
		sp->v.loopend += loop_size;
		sp->v.end += loop_size;
		unroll++;
	}

	/* try to allocate a memory block */
	blocksize = truesize << shift;
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
	size = BLANK_HEAD_SIZE << shift;
	snd_emu10k1_synth_memset(emu, sp->block, offset, size, fill);
	offset += size;

	/* copy provided samples */
	if (unroll && loop_end <= data_end) {
		size = loop_end << shift;
		if (snd_emu10k1_synth_copy_from_user(emu, sp->block, offset, data, size, xor))
			goto faulty;
		offset += size;

		data += loop_start << shift;
		while (--unroll > 0) {
			size = loop_size << shift;
			if (snd_emu10k1_synth_copy_from_user(emu, sp->block, offset, data, size, xor))
				goto faulty;
			offset += size;
		}

		size = (data_end - loop_start) << shift;
	} else {
		size = data_end << shift;
	}
	if (snd_emu10k1_synth_copy_from_user(emu, sp->block, offset, data, size, xor))
		goto faulty;
	offset += size;

	/* clear rest of samples (if any) */
	if (offset < blocksize)
		snd_emu10k1_synth_memset(emu, sp->block, offset, blocksize - offset, fill);

	return 0;

faulty:
	snd_emu10k1_synth_free(emu, sp->block);
	sp->block = NULL;
	return -EFAULT;
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

