/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  DRAM access routines
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
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/info.h>


static int snd_gus_dram_poke(struct snd_gus_card *gus, char __user *_buffer,
			     unsigned int address, unsigned int size)
{
	unsigned long flags;
	unsigned int size1, size2;
	char buffer[256], *pbuffer;

	while (size > 0) {
		size1 = size > sizeof(buffer) ? sizeof(buffer) : size;
		if (copy_from_user(buffer, _buffer, size1))
			return -EFAULT;
		if (gus->interwave) {
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
			snd_gf1_dram_addr(gus, address);
			outsb(GUSP(gus, DRAM), buffer, size1);
			spin_unlock_irqrestore(&gus->reg_lock, flags);
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
	unsigned long flags;
	unsigned int size1, size2;
	char buffer[256], *pbuffer;

	while (size > 0) {
		size1 = size > sizeof(buffer) ? sizeof(buffer) : size;
		if (gus->interwave) {
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, rom ? 0x03 : 0x01);
			snd_gf1_dram_addr(gus, address);
			insb(GUSP(gus, DRAM), buffer, size1);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
			spin_unlock_irqrestore(&gus->reg_lock, flags);
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
