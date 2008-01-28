/*
 * C-Media CMI8788 driver - helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/delay.h>
#include <linux/sched.h>
#include <sound/core.h>
#include <asm/io.h>
#include "oxygen.h"

u8 oxygen_read8(struct oxygen *chip, unsigned int reg)
{
	return inb(chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_read8);

u16 oxygen_read16(struct oxygen *chip, unsigned int reg)
{
	return inw(chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_read16);

u32 oxygen_read32(struct oxygen *chip, unsigned int reg)
{
	return inl(chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_read32);

void oxygen_write8(struct oxygen *chip, unsigned int reg, u8 value)
{
	outb(value, chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write8);

void oxygen_write16(struct oxygen *chip, unsigned int reg, u16 value)
{
	outw(value, chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write16);

void oxygen_write32(struct oxygen *chip, unsigned int reg, u32 value)
{
	outl(value, chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write32);

void oxygen_write8_masked(struct oxygen *chip, unsigned int reg,
			  u8 value, u8 mask)
{
	u8 tmp = inb(chip->addr + reg);
	outb((tmp & ~mask) | (value & mask), chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write8_masked);

void oxygen_write16_masked(struct oxygen *chip, unsigned int reg,
			   u16 value, u16 mask)
{
	u16 tmp = inw(chip->addr + reg);
	outw((tmp & ~mask) | (value & mask), chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write16_masked);

void oxygen_write32_masked(struct oxygen *chip, unsigned int reg,
			   u32 value, u32 mask)
{
	u32 tmp = inl(chip->addr + reg);
	outl((tmp & ~mask) | (value & mask), chip->addr + reg);
}
EXPORT_SYMBOL(oxygen_write32_masked);

static int oxygen_ac97_wait(struct oxygen *chip, unsigned int mask)
{
	u8 status = 0;

	/*
	 * Reading the status register also clears the bits, so we have to save
	 * the read bits in status.
	 */
	wait_event_timeout(chip->ac97_waitqueue,
			   ({ status |= oxygen_read8(chip, OXYGEN_AC97_INTERRUPT_STATUS);
			      status & mask; }),
			   msecs_to_jiffies(1) + 1);
	/*
	 * Check even after a timeout because this function should not require
	 * the AC'97 interrupt to be enabled.
	 */
	status |= oxygen_read8(chip, OXYGEN_AC97_INTERRUPT_STATUS);
	return status & mask ? 0 : -EIO;
}

/*
 * About 10% of AC'97 register reads or writes fail to complete, but even those
 * where the controller indicates completion aren't guaranteed to have actually
 * happened.
 *
 * It's hard to assign blame to either the controller or the codec because both
 * were made by C-Media ...
 */

void oxygen_write_ac97(struct oxygen *chip, unsigned int codec,
		       unsigned int index, u16 data)
{
	unsigned int count, succeeded;
	u32 reg;

	reg = data;
	reg |= index << OXYGEN_AC97_REG_ADDR_SHIFT;
	reg |= OXYGEN_AC97_REG_DIR_WRITE;
	reg |= codec << OXYGEN_AC97_REG_CODEC_SHIFT;
	succeeded = 0;
	for (count = 5; count > 0; --count) {
		udelay(5);
		oxygen_write32(chip, OXYGEN_AC97_REGS, reg);
		/* require two "completed" writes, just to be sure */
		if (oxygen_ac97_wait(chip, OXYGEN_AC97_INT_WRITE_DONE) >= 0 &&
		    ++succeeded >= 2)
			return;
	}
	snd_printk(KERN_ERR "AC'97 write timeout\n");
}
EXPORT_SYMBOL(oxygen_write_ac97);

u16 oxygen_read_ac97(struct oxygen *chip, unsigned int codec,
		     unsigned int index)
{
	unsigned int count;
	unsigned int last_read = UINT_MAX;
	u32 reg;

	reg = index << OXYGEN_AC97_REG_ADDR_SHIFT;
	reg |= OXYGEN_AC97_REG_DIR_READ;
	reg |= codec << OXYGEN_AC97_REG_CODEC_SHIFT;
	for (count = 5; count > 0; --count) {
		udelay(5);
		oxygen_write32(chip, OXYGEN_AC97_REGS, reg);
		udelay(10);
		if (oxygen_ac97_wait(chip, OXYGEN_AC97_INT_READ_DONE) >= 0) {
			u16 value = oxygen_read16(chip, OXYGEN_AC97_REGS);
			/* we require two consecutive reads of the same value */
			if (value == last_read)
				return value;
			last_read = value;
			/*
			 * Invert the register value bits to make sure that two
			 * consecutive unsuccessful reads do not return the same
			 * value.
			 */
			reg ^= 0xffff;
		}
	}
	snd_printk(KERN_ERR "AC'97 read timeout on codec %u\n", codec);
	return 0;
}
EXPORT_SYMBOL(oxygen_read_ac97);

void oxygen_write_ac97_masked(struct oxygen *chip, unsigned int codec,
			      unsigned int index, u16 data, u16 mask)
{
	u16 value = oxygen_read_ac97(chip, codec, index);
	value &= ~mask;
	value |= data & mask;
	oxygen_write_ac97(chip, codec, index, value);
}
EXPORT_SYMBOL(oxygen_write_ac97_masked);

void oxygen_write_spi(struct oxygen *chip, u8 control, unsigned int data)
{
	unsigned int count;

	/* should not need more than 7.68 us (24 * 320 ns) */
	count = 10;
	while ((oxygen_read8(chip, OXYGEN_SPI_CONTROL) & OXYGEN_SPI_BUSY)
	       && count > 0) {
		udelay(1);
		--count;
	}

	spin_lock_irq(&chip->reg_lock);
	oxygen_write8(chip, OXYGEN_SPI_DATA1, data);
	oxygen_write8(chip, OXYGEN_SPI_DATA2, data >> 8);
	if (control & OXYGEN_SPI_DATA_LENGTH_3)
		oxygen_write8(chip, OXYGEN_SPI_DATA3, data >> 16);
	oxygen_write8(chip, OXYGEN_SPI_CONTROL, control);
	spin_unlock_irq(&chip->reg_lock);
}
EXPORT_SYMBOL(oxygen_write_spi);
