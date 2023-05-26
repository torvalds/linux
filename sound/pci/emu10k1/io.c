// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1.h>
#include <linux/delay.h>
#include <linux/export.h>
#include "p17v.h"

static inline bool check_ptr_reg(struct snd_emu10k1 *emu, unsigned int reg)
{
	if (snd_BUG_ON(!emu))
		return false;
	if (snd_BUG_ON(reg & (emu->audigy ? (0xffff0000 & ~A_PTR_ADDRESS_MASK)
					  : (0xffff0000 & ~PTR_ADDRESS_MASK))))
		return false;
	if (snd_BUG_ON(reg & 0x0000ffff & ~PTR_CHANNELNUM_MASK))
		return false;
	return true;
}

unsigned int snd_emu10k1_ptr_read(struct snd_emu10k1 * emu, unsigned int reg, unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
	unsigned int mask;

	regptr = (reg << 16) | chn;
	if (!check_ptr_reg(emu, regptr))
		return 0;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR);
	val = inl(emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);

	if (reg & 0xff000000) {
		unsigned char size, offset;
		
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = (1 << size) - 1;
		
		return (val >> offset) & mask;
	} else {
		return val;
	}
}

EXPORT_SYMBOL(snd_emu10k1_ptr_read);

void snd_emu10k1_ptr_write(struct snd_emu10k1 *emu, unsigned int reg, unsigned int chn, unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;
	unsigned int mask;

	regptr = (reg << 16) | chn;
	if (!check_ptr_reg(emu, regptr))
		return;

	if (reg & 0xff000000) {
		unsigned char size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = (1 << size) - 1;
		if (snd_BUG_ON(data & ~mask))
			return;
		mask <<= offset;
		data <<= offset;

		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
		data |= inl(emu->port + DATA) & ~mask;
	} else {
		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
	}
	outl(data, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

EXPORT_SYMBOL(snd_emu10k1_ptr_write);

void snd_emu10k1_ptr_write_multiple(struct snd_emu10k1 *emu, unsigned int chn, ...)
{
	va_list va;
	u32 addr_mask;
	unsigned long flags;

	if (snd_BUG_ON(!emu))
		return;
	if (snd_BUG_ON(chn & ~PTR_CHANNELNUM_MASK))
		return;
	addr_mask = ~((emu->audigy ? A_PTR_ADDRESS_MASK : PTR_ADDRESS_MASK) >> 16);

	va_start(va, chn);
	spin_lock_irqsave(&emu->emu_lock, flags);
	for (;;) {
		u32 data;
		u32 reg = va_arg(va, u32);
		if (reg == REGLIST_END)
			break;
		data = va_arg(va, u32);
		if (snd_BUG_ON(reg & addr_mask))  // Only raw registers supported here
			continue;
		outl((reg << 16) | chn, emu->port + PTR);
		outl(data, emu->port + DATA);
	}
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	va_end(va);
}

EXPORT_SYMBOL(snd_emu10k1_ptr_write_multiple);

unsigned int snd_emu10k1_ptr20_read(struct snd_emu10k1 * emu, 
					  unsigned int reg, 
					  unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
  
	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR2);
	val = inl(emu->port + DATA2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

void snd_emu10k1_ptr20_write(struct snd_emu10k1 *emu, 
				   unsigned int reg, 
				   unsigned int chn, 
				   unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;

	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + PTR2);
	outl(data, emu->port + DATA2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

int snd_emu10k1_spi_write(struct snd_emu10k1 * emu,
				   unsigned int data)
{
	unsigned int reset, set;
	unsigned int reg, tmp;
	int n, result;
	int err = 0;

	/* This function is not re-entrant, so protect against it. */
	spin_lock(&emu->spi_lock);
	if (emu->card_capabilities->ca0108_chip)
		reg = P17V_SPI;
	else {
		/* For other chip types the SPI register
		 * is currently unknown. */
		err = 1;
		goto spi_write_exit;
	}
	if (data > 0xffff) {
		/* Only 16bit values allowed */
		err = 1;
		goto spi_write_exit;
	}

	tmp = snd_emu10k1_ptr20_read(emu, reg, 0);
	reset = (tmp & ~0x3ffff) | 0x20000; /* Set xxx20000 */
	set = reset | 0x10000; /* Set xxx1xxxx */
	snd_emu10k1_ptr20_write(emu, reg, 0, reset | data);
	tmp = snd_emu10k1_ptr20_read(emu, reg, 0); /* write post */
	snd_emu10k1_ptr20_write(emu, reg, 0, set | data);
	result = 1;
	/* Wait for status bit to return to 0 */
	for (n = 0; n < 100; n++) {
		udelay(10);
		tmp = snd_emu10k1_ptr20_read(emu, reg, 0);
		if (!(tmp & 0x10000)) {
			result = 0;
			break;
		}
	}
	if (result) {
		/* Timed out */
		err = 1;
		goto spi_write_exit;
	}
	snd_emu10k1_ptr20_write(emu, reg, 0, reset | data);
	tmp = snd_emu10k1_ptr20_read(emu, reg, 0); /* Write post */
	err = 0;
spi_write_exit:
	spin_unlock(&emu->spi_lock);
	return err;
}

/* The ADC does not support i2c read, so only write is implemented */
int snd_emu10k1_i2c_write(struct snd_emu10k1 *emu,
				u32 reg,
				u32 value)
{
	u32 tmp;
	int timeout = 0;
	int status;
	int retry;
	int err = 0;

	if ((reg > 0x7f) || (value > 0x1ff)) {
		dev_err(emu->card->dev, "i2c_write: invalid values.\n");
		return -EINVAL;
	}

	/* This function is not re-entrant, so protect against it. */
	spin_lock(&emu->i2c_lock);

	tmp = reg << 25 | value << 16;

	/* This controls the I2C connected to the WM8775 ADC Codec */
	snd_emu10k1_ptr20_write(emu, P17V_I2C_1, 0, tmp);
	tmp = snd_emu10k1_ptr20_read(emu, P17V_I2C_1, 0); /* write post */

	for (retry = 0; retry < 10; retry++) {
		/* Send the data to i2c */
		tmp = 0;
		tmp = tmp | (I2C_A_ADC_LAST|I2C_A_ADC_START|I2C_A_ADC_ADD);
		snd_emu10k1_ptr20_write(emu, P17V_I2C_ADDR, 0, tmp);

		/* Wait till the transaction ends */
		while (1) {
			mdelay(1);
			status = snd_emu10k1_ptr20_read(emu, P17V_I2C_ADDR, 0);
			timeout++;
			if ((status & I2C_A_ADC_START) == 0)
				break;

			if (timeout > 1000) {
				dev_warn(emu->card->dev,
					   "emu10k1:I2C:timeout status=0x%x\n",
					   status);
				break;
			}
		}
		//Read back and see if the transaction is successful
		if ((status & I2C_A_ADC_ABORT) == 0)
			break;
	}

	if (retry == 10) {
		dev_err(emu->card->dev, "Writing to ADC failed!\n");
		dev_err(emu->card->dev, "status=0x%x, reg=%d, value=%d\n",
			status, reg, value);
		/* dump_stack(); */
		err = -EINVAL;
	}
    
	spin_unlock(&emu->i2c_lock);
	return err;
}

static void snd_emu1010_fpga_write_locked(struct snd_emu10k1 *emu, u32 reg, u32 value)
{
	if (snd_BUG_ON(reg > 0x3f))
		return;
	reg += 0x40; /* 0x40 upwards are registers. */
	if (snd_BUG_ON(value > 0x3f)) /* 0 to 0x3f are values */
		return;
	outw(reg, emu->port + A_GPIO);
	udelay(10);
	outw(reg | 0x80, emu->port + A_GPIO);  /* High bit clocks the value into the fpga. */
	udelay(10);
	outw(value, emu->port + A_GPIO);
	udelay(10);
	outw(value | 0x80 , emu->port + A_GPIO);  /* High bit clocks the value into the fpga. */
}

void snd_emu1010_fpga_write(struct snd_emu10k1 *emu, u32 reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	snd_emu1010_fpga_write_locked(emu, reg, value);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_emu1010_fpga_read_locked(struct snd_emu10k1 *emu, u32 reg, u32 *value)
{
	// The higest input pin is used as the designated interrupt trigger,
	// so it needs to be masked out.
	u32 mask = emu->card_capabilities->ca0108_chip ? 0x1f : 0x7f;
	if (snd_BUG_ON(reg > 0x3f))
		return;
	reg += 0x40; /* 0x40 upwards are registers. */
	outw(reg, emu->port + A_GPIO);
	udelay(10);
	outw(reg | 0x80, emu->port + A_GPIO);  /* High bit clocks the value into the fpga. */
	udelay(10);
	*value = ((inw(emu->port + A_GPIO) >> 8) & mask);
}

void snd_emu1010_fpga_read(struct snd_emu10k1 *emu, u32 reg, u32 *value)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	snd_emu1010_fpga_read_locked(emu, reg, value);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

/* Each Destination has one and only one Source,
 * but one Source can feed any number of Destinations simultaneously.
 */
void snd_emu1010_fpga_link_dst_src_write(struct snd_emu10k1 *emu, u32 dst, u32 src)
{
	unsigned long flags;

	if (snd_BUG_ON(dst & ~0x71f))
		return;
	if (snd_BUG_ON(src & ~0x71f))
		return;
	spin_lock_irqsave(&emu->emu_lock, flags);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_DESTHI, dst >> 8);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_DESTLO, dst & 0x1f);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_SRCHI, src >> 8);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_SRCLO, src & 0x1f);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

u32 snd_emu1010_fpga_link_dst_src_read(struct snd_emu10k1 *emu, u32 dst)
{
	unsigned long flags;
	u32 hi, lo;

	if (snd_BUG_ON(dst & ~0x71f))
		return 0;
	spin_lock_irqsave(&emu->emu_lock, flags);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_DESTHI, dst >> 8);
	snd_emu1010_fpga_write_locked(emu, EMU_HANA_DESTLO, dst & 0x1f);
	snd_emu1010_fpga_read_locked(emu, EMU_HANA_SRCHI, &hi);
	snd_emu1010_fpga_read_locked(emu, EMU_HANA_SRCLO, &lo);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return (hi << 8) | lo;
}

void snd_emu10k1_intr_enable(struct snd_emu10k1 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) | intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_intr_disable(struct snd_emu10k1 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE) & ~intrenb;
	outl(enable, emu->port + INTE);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_intr_enable(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(CLIEH << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val |= 1 << (voicenum - 32);
	} else {
		outl(CLIEL << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val |= 1 << voicenum;
	}
	outl(val, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_intr_disable(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(CLIEH << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val &= ~(1 << (voicenum - 32));
	} else {
		outl(CLIEL << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val &= ~(1 << voicenum);
	}
	outl(val, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_intr_ack(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(CLIPH << 16, emu->port + PTR);
		voicenum = 1 << (voicenum - 32);
	} else {
		outl(CLIPL << 16, emu->port + PTR);
		voicenum = 1 << voicenum;
	}
	outl(voicenum, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_half_loop_intr_enable(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(HLIEH << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val |= 1 << (voicenum - 32);
	} else {
		outl(HLIEL << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val |= 1 << voicenum;
	}
	outl(val, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_half_loop_intr_disable(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(HLIEH << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val &= ~(1 << (voicenum - 32));
	} else {
		outl(HLIEL << 16, emu->port + PTR);
		val = inl(emu->port + DATA);
		val &= ~(1 << voicenum);
	}
	outl(val, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_half_loop_intr_ack(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(HLIPH << 16, emu->port + PTR);
		voicenum = 1 << (voicenum - 32);
	} else {
		outl(HLIPL << 16, emu->port + PTR);
		voicenum = 1 << voicenum;
	}
	outl(voicenum, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

#if 0
void snd_emu10k1_voice_set_loop_stop(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int sol;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(SOLEH << 16, emu->port + PTR);
		sol = inl(emu->port + DATA);
		sol |= 1 << (voicenum - 32);
	} else {
		outl(SOLEL << 16, emu->port + PTR);
		sol = inl(emu->port + DATA);
		sol |= 1 << voicenum;
	}
	outl(sol, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_clear_loop_stop(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int sol;

	spin_lock_irqsave(&emu->emu_lock, flags);
	if (voicenum >= 32) {
		outl(SOLEH << 16, emu->port + PTR);
		sol = inl(emu->port + DATA);
		sol &= ~(1 << (voicenum - 32));
	} else {
		outl(SOLEL << 16, emu->port + PTR);
		sol = inl(emu->port + DATA);
		sol &= ~(1 << voicenum);
	}
	outl(sol, emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}
#endif

void snd_emu10k1_voice_set_loop_stop_multiple(struct snd_emu10k1 *emu, u64 voices)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(SOLEL << 16, emu->port + PTR);
	outl(inl(emu->port + DATA) | (u32)voices, emu->port + DATA);
	outl(SOLEH << 16, emu->port + PTR);
	outl(inl(emu->port + DATA) | (u32)(voices >> 32), emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

void snd_emu10k1_voice_clear_loop_stop_multiple(struct snd_emu10k1 *emu, u64 voices)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(SOLEL << 16, emu->port + PTR);
	outl(inl(emu->port + DATA) & (u32)~voices, emu->port + DATA);
	outl(SOLEH << 16, emu->port + PTR);
	outl(inl(emu->port + DATA) & (u32)(~voices >> 32), emu->port + DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

int snd_emu10k1_voice_clear_loop_stop_multiple_atomic(struct snd_emu10k1 *emu, u64 voices)
{
	unsigned long flags;
	u32 soll, solh;
	int ret = -EIO;

	spin_lock_irqsave(&emu->emu_lock, flags);

	outl(SOLEL << 16, emu->port + PTR);
	soll = inl(emu->port + DATA);
	outl(SOLEH << 16, emu->port + PTR);
	solh = inl(emu->port + DATA);

	soll &= (u32)~voices;
	solh &= (u32)(~voices >> 32);

	for (int tries = 0; tries < 1000; tries++) {
		const u32 quart = 1U << (REG_SIZE(WC_CURRENTCHANNEL) - 2);
		// First we wait for the third quarter of the sample cycle ...
		u32 wc = inl(emu->port + WC);
		u32 cc = REG_VAL_GET(WC_CURRENTCHANNEL, wc);
		if (cc >= quart * 2 && cc < quart * 3) {
			// ... and release the low voices, while the high ones are serviced.
			outl(SOLEL << 16, emu->port + PTR);
			outl(soll, emu->port + DATA);
			// Then we wait for the first quarter of the next sample cycle ...
			for (; tries < 1000; tries++) {
				cc = REG_VAL_GET(WC_CURRENTCHANNEL, inl(emu->port + WC));
				if (cc < quart)
					goto good;
				// We will block for 10+ us with interrupts disabled. This is
				// not nice at all, but necessary for reasonable reliability.
				udelay(1);
			}
			break;
		good:
			// ... and release the high voices, while the low ones are serviced.
			outl(SOLEH << 16, emu->port + PTR);
			outl(solh, emu->port + DATA);
			// Finally we verify that nothing interfered in fact.
			if (REG_VAL_GET(WC_SAMPLECOUNTER, inl(emu->port + WC)) ==
			    ((REG_VAL_GET(WC_SAMPLECOUNTER, wc) + 1) & REG_MASK0(WC_SAMPLECOUNTER))) {
				ret = 0;
			} else {
				ret = -EAGAIN;
			}
			break;
		}
		// Don't block for too long
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		udelay(1);
		spin_lock_irqsave(&emu->emu_lock, flags);
	}

	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return ret;
}

void snd_emu10k1_wait(struct snd_emu10k1 *emu, unsigned int wait)
{
	volatile unsigned count;
	unsigned int newtime = 0, curtime;

	curtime = inl(emu->port + WC) >> 6;
	while (wait-- > 0) {
		count = 0;
		while (count++ < 16384) {
			newtime = inl(emu->port + WC) >> 6;
			if (newtime != curtime)
				break;
		}
		if (count > 16384)
			break;
		curtime = newtime;
	}
}

unsigned short snd_emu10k1_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct snd_emu10k1 *emu = ac97->private_data;
	unsigned long flags;
	unsigned short val;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	val = inw(emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
	return val;
}

void snd_emu10k1_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short data)
{
	struct snd_emu10k1 *emu = ac97->private_data;
	unsigned long flags;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outb(reg, emu->port + AC97ADDRESS);
	outw(data, emu->port + AC97DATA);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}
