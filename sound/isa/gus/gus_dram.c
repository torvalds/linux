// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  DRAM access routines
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/info.h>


static int snd_gus_dram_poke(struct snd_gus_card *gus, char __user *_buffer,
			     unsigned int address, unsigned int size)
{
	unsigned int size1, size2;
	char buffer[256], *pbuffer;

	while (size > 0) {
		size1 = size > sizeof(buffer) ? sizeof(buffer) : size;
		if (copy_from_user(buffer, _buffer, size1))
			return -EFAULT;
		if (gus->interwave) {
			guard(spinlock_irqsave)(&gus->reg_lock);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
			snd_gf1_dram_addr(gus, address);
			outsb(GUSP(gus, DRAM), buffer, size1);
			address += size1;
		} else {
			pbuffer = buffer;
			size2 = size1;
			while (size2--)
				snd_gf1_poke(gus, address++, *pbuffer++);
		}
		size -= size1;
		_buffer += size1;
	}
	return 0;
}


int snd_gus_dram_write(struct snd_gus_card *gus, char __user *buffer,
		       unsigned int address, unsigned int size)
{
	return snd_gus_dram_poke(gus, buffer, address, size);
}

static int snd_gus_dram_peek(struct snd_gus_card *gus, char __user *_buffer,
			     unsigned int address, unsigned int size,
			     int rom)
{
	unsigned int size1, size2;
	char buffer[256], *pbuffer;

	while (size > 0) {
		size1 = size > sizeof(buffer) ? sizeof(buffer) : size;
		if (gus->interwave) {
			guard(spinlock_irqsave)(&gus->reg_lock);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, rom ? 0x03 : 0x01);
			snd_gf1_dram_addr(gus, address);
			insb(GUSP(gus, DRAM), buffer, size1);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
			address += size1;
		} else {
			pbuffer = buffer;
			size2 = size1;
			while (size2--)
				*pbuffer++ = snd_gf1_peek(gus, address++);
		}
		if (copy_to_user(_buffer, buffer, size1))
			return -EFAULT;
		size -= size1;
		_buffer += size1;
	}
	return 0;
}

int snd_gus_dram_read(struct snd_gus_card *gus, char __user *buffer,
		      unsigned int address, unsigned int size,
		      int rom)
{
	return snd_gus_dram_peek(gus, buffer, address, size, rom);
}
