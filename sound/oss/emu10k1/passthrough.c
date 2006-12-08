/*
 **********************************************************************
 *     passthrough.c -- Emu10k1 digital passthrough
 *     Copyright (C) 2001  Juha Yrjölä <jyrjola@cc.hut.fi>
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     May 15, 2001	    Juha Yrjölä	    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */
                       
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>

#include "hwaccess.h"
#include "cardwo.h"
#include "cardwi.h"
#include "recmgr.h"
#include "irqmgr.h"
#include "audio.h"
#include "8010.h"

static void pt_putsamples(struct pt_data *pt, u16 *ptr, u16 left, u16 right)
{
	unsigned int idx;

	ptr[pt->copyptr] = left;
	idx = pt->copyptr + PT_SAMPLES/2;
	idx %= PT_SAMPLES;
	ptr[idx] = right;
}

static inline int pt_can_write(struct pt_data *pt)
{
	return pt->blocks_copied < pt->blocks_played + 8;
}

static int pt_wait_for_write(struct emu10k1_wavedevice *wavedev, int nonblock)
{
	struct emu10k1_card *card = wavedev->card;
	struct pt_data *pt = &card->pt;

	if (nonblock && !pt_can_write(pt))
		return -EAGAIN;
	while (!pt_can_write(pt) && pt->state != PT_STATE_INACTIVE) {
		interruptible_sleep_on(&pt->wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}
	if (pt->state == PT_STATE_INACTIVE)
		return -EAGAIN;
	
	return 0;
}

static int pt_putblock(struct emu10k1_wavedevice *wave_dev, u16 *block, int nonblock)
{
	struct woinst *woinst = wave_dev->woinst;
	struct emu10k1_card *card = wave_dev->card;
	struct pt_data *pt = &card->pt;
	u16 *ptr = (u16 *) card->tankmem.addr;
	int i = 0, r;
	unsigned long flags;

	r = pt_wait_for_write(wave_dev, nonblock);
	if (r < 0)
		return r;
	spin_lock_irqsave(&card->pt.lock, flags);
	while (i < PT_BLOCKSAMPLES) {
		pt_putsamples(pt, ptr, block[2*i], block[2*i+1]);
		if (pt->copyptr == 0)
			pt->copyptr = PT_SAMPLES;
		pt->copyptr--;
		i++;
	}
	woinst->total_copied += PT_BLOCKSIZE;
	pt->blocks_copied++;
	if (pt->blocks_copied >= 4 && pt->state != PT_STATE_PLAYING) {
		DPF(2, "activating digital pass-through playback\n");
		sblive_writeptr(card, GPR_BASE + pt->enable_gpr, 0, 1);
		pt->state = PT_STATE_PLAYING;
	}
	spin_unlock_irqrestore(&card->pt.lock, flags);
	return 0;
}

int emu10k1_pt_setup(struct emu10k1_wavedevice *wave_dev)
{
	u32 bits;
	struct emu10k1_card *card = wave_dev->card;
	struct pt_data *pt = &card->pt;
	int i;

	for (i = 0; i < 3; i++) {
		pt->old_spcs[i] = sblive_readptr(card, SPCS0 + i, 0);
		if (pt->spcs_to_use & (1 << i)) {
			DPD(2, "using S/PDIF port %d\n", i);
			bits = SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
				SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS |
				0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT;
			if (pt->ac3data)
				bits |= SPCS_NOTAUDIODATA;
			sblive_writeptr(card, SPCS0 + i, 0, bits);
		}
	}
	return 0;
}

ssize_t emu10k1_pt_write(struct file *file, const char __user *buffer, size_t count)
{
	struct emu10k1_wavedevice *wave_dev = (struct emu10k1_wavedevice *) file->private_data;
	struct emu10k1_card *card = wave_dev->card;
	struct pt_data *pt = &card->pt;
	int nonblock, i, r, blocks, blocks_copied, bytes_copied = 0;

	DPD(3, "emu10k1_pt_write(): %d bytes\n", count);
	
	nonblock = file->f_flags & O_NONBLOCK;
	
	if (card->tankmem.size < PT_SAMPLES*2)
		return -EFAULT;
	if (pt->state == PT_STATE_INACTIVE) {
		DPF(2, "bufptr init\n");
		pt->playptr = PT_SAMPLES-1;
		pt->copyptr = PT_INITPTR;
		pt->blocks_played = pt->blocks_copied = 0;
		memset(card->tankmem.addr, 0, card->tankmem.size);
		pt->state = PT_STATE_ACTIVATED;
		pt->buf = kmalloc(PT_BLOCKSIZE, GFP_KERNEL);
		pt->prepend_size = 0;
		if (pt->buf == NULL)
			return -ENOMEM;
		emu10k1_pt_setup(wave_dev);
	}
	if (pt->prepend_size) {
		int needed = PT_BLOCKSIZE - pt->prepend_size;

		DPD(3, "prepend size %d, prepending %d bytes\n", pt->prepend_size, needed);
		if (count < needed) {
			if (copy_from_user(pt->buf + pt->prepend_size,
					   buffer, count))
				return -EFAULT;
			pt->prepend_size += count;
			DPD(3, "prepend size now %d\n", pt->prepend_size);
			return count;
		}
		if (copy_from_user(pt->buf + pt->prepend_size, buffer, needed))
			return -EFAULT;
		r = pt_putblock(wave_dev, (u16 *) pt->buf, nonblock);
		if (r)
			return r;
		bytes_copied += needed;
		pt->prepend_size = 0;
	}
	blocks = (count-bytes_copied)/PT_BLOCKSIZE;
	blocks_copied = 0;
	while (blocks > 0) {
		u16 __user *bufptr = (u16 __user *) buffer + (bytes_copied/2);
		if (copy_from_user(pt->buf, bufptr, PT_BLOCKSIZE))
			return -EFAULT;
		r = pt_putblock(wave_dev, (u16 *)pt->buf, nonblock);
		if (r) {
			if (bytes_copied)
				return bytes_copied;
			else
				return r;
		}
		bytes_copied += PT_BLOCKSIZE;
		blocks--;
		blocks_copied++;
	}
	i = count - bytes_copied;
	if (i) {
		pt->prepend_size = i;
		if (copy_from_user(pt->buf, buffer + bytes_copied, i))
			return -EFAULT;
		bytes_copied += i;
		DPD(3, "filling prepend buffer with %d bytes", i);
	}
	return bytes_copied;
}

void emu10k1_pt_stop(struct emu10k1_card *card)
{
	struct pt_data *pt = &card->pt;
	int i;

	if (pt->state != PT_STATE_INACTIVE) {
		DPF(2, "digital pass-through stopped\n");
		sblive_writeptr(card, (card->is_audigy ? A_GPR_BASE : GPR_BASE) + pt->enable_gpr, 0, 0);
		for (i = 0; i < 3; i++) {
                        if (pt->spcs_to_use & (1 << i))
				sblive_writeptr(card, SPCS0 + i, 0, pt->old_spcs[i]);
		}
		pt->state = PT_STATE_INACTIVE;
		kfree(pt->buf);
	}
}

void emu10k1_pt_waveout_update(struct emu10k1_wavedevice *wave_dev)
{
	struct woinst *woinst = wave_dev->woinst;
	struct pt_data *pt = &wave_dev->card->pt;
	u32 pos;

	if (pt->state == PT_STATE_PLAYING && pt->pos_gpr >= 0) {
		pos = sblive_readptr(wave_dev->card, GPR_BASE + pt->pos_gpr, 0);
		if (pos > PT_BLOCKSAMPLES)
			pos = PT_BLOCKSAMPLES;
		pos = 4 * (PT_BLOCKSAMPLES - pos);
	} else
		pos = 0;
	woinst->total_played = pt->blocks_played * woinst->buffer.fragment_size + pos;
	woinst->buffer.hw_pos = pos;
}
