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

unsigned int snd_emu10k1_ptr_read(struct snd_emu10k1 * emu, unsigned int reg, unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
	unsigned int mask;

	mask = emu->audigy ? A_PTR_ADDRESS_MASK : PTR_ADDRESS_MASK;
	regptr = ((reg << 16) & mask) | (chn & PTR_CHANNELNUM_MASK);

	if (reg & 0xff000000) {
		unsigned char size, offset;
		
		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		
		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
		val = inl(emu->port + DATA);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		
		return (val & mask) >> offset;
	} else {
		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
		val = inl(emu->port + DATA);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
		return val;
	}
}

EXPORT_SYMBOL(snd_emu10k1_ptr_read);

void snd_emu10k1_ptr_write(struct snd_emu10k1 *emu, unsigned int reg, unsigned int chn, unsigned int data)
{
	unsigned int regptr;
	unsigned long flags;
	unsigned int mask;

	if (snd_BUG_ON(!emu))
		return;
	mask = emu->audigy ? A_PTR_ADDRESS_MASK : PTR_ADDRESS_MASK;
	regptr = ((reg << 16) & mask) | (chn & PTR_CHANNELNUM_MASK);

	if (reg & 0xff000000) {
		unsigned char size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		data = (data << offset) & mask;

		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
		data |= inl(emu->port + DATA) & ~mask;
		outl(data, emu->port + DATA);
		spin_unlock_irqrestore(&emu->emu_lock, flags);		
	} else {
		spin_lock_irqsave(&emu->emu_lock, flags);
		outl(regptr, emu->port + PTR);
		outl(data, emu->port + DATA);
		spin_unlock_irqrestore(&emu->emu_lock, flags);
	}
}

EXPORT_SYMBOL(snd_emu10k1_ptr_write);

unsigned int snd_emu10k1_ptr20_read(struct snd_emu10k1 * emu, 
					  unsigned int reg, 
					  unsigned int chn)
{
	unsigned long flags;
	unsigned int regptr, val;
  
	regptr = (reg << 16) | chn;

	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(regptr, emu->port + 0x20 + PTR);
	val = inl(emu->port + 0x20 + DATA);
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
	outl(regptr, emu->port + 0x20 + PTR);
	outl(data, emu->port + 0x20 + DATA);
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
		reg = 0x3c; /* PTR20, reg 0x3c */
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

int snd_emu1010_fpga_write(struct snd_emu10k1 * emu, u32 reg, u32 value)
{
	unsigned long flags;

	if (reg > 0x3f)
		return 1;
	reg += 0x40; /* 0x40 upwards are registers. */
	if (value > 0x3f) /* 0 to 0x3f are values */
		return 1;
	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(reg, emu->port + A_IOCFG);
	udelay(10);
	outl(reg | 0x80, emu->port + A_IOCFG);  /* High bit clocks the value into the fpga. */
	udelay(10);
	outl(value, emu->port + A_IOCFG);
	udelay(10);
	outl(value | 0x80 , emu->port + A_IOCFG);  /* High bit clocks the value into the fpga. */
	spin_unlock_irqrestore(&emu->emu_lock, flags);

	return 0;
}

int snd_emu1010_fpga_read(struct snd_emu10k1 * emu, u32 reg, u32 *value)
{
	unsigned long flags;
	if (reg > 0x3f)
		return 1;
	reg += 0x40; /* 0x40 upwards are registers. */
	spin_lock_irqsave(&emu->emu_lock, flags);
	outl(reg, emu->port + A_IOCFG);
	udelay(10);
	outl(reg | 0x80, emu->port + A_IOCFG);  /* High bit clocks the value into the fpga. */
	udelay(10);
	*value = ((inl(emu->port + A_IOCFG) >> 8) & 0x7f);
	spin_unlock_irqrestore(&emu->emu_lock, flags);

	return 0;
}

/* Each Destination has one and only one Source,
 * but one Source can feed any number of Destinations simultaneously.
 */
int snd_emu1010_fpga_link_dst_src_write(struct snd_emu10k1 * emu, u32 dst, u32 src)
{
	snd_emu1010_fpga_write(emu, 0x00, ((dst >> 8) & 0x3f) );
	snd_emu1010_fpga_write(emu, 0x01, (dst & 0x3f) );
	snd_emu1010_fpga_write(emu, 0x02, ((src >> 8) & 0x3f) );
	snd_emu1010_fpga_write(emu, 0x03, (src & 0x3f) );

	return 0;
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
	/* voice interrupt */
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
	/* voice interrupt */
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
	/* voice interrupt */
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
	/* voice interrupt */
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
	/* voice interrupt */
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
	/* voice interrupt */
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

void snd_emu10k1_voice_set_loop_stop(struct snd_emu10k1 *emu, unsigned int voicenum)
{
	unsigned long flags;
	unsigned int sol;

	spin_lock_irqsave(&emu->emu_lock, flags);
	/* voice interrupt */
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
	/* voice interrupt */
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

/*
 *  convert rate to pitch
 */

unsigned int snd_emu10k1_rate_to_pitch(unsigned int rate)
{
	static const u32 logMagTable[128] = {
		0x00000, 0x02dfc, 0x05b9e, 0x088e6, 0x0b5d6, 0x0e26f, 0x10eb3, 0x13aa2,
		0x1663f, 0x1918a, 0x1bc84, 0x1e72e, 0x2118b, 0x23b9a, 0x2655d, 0x28ed5,
		0x2b803, 0x2e0e8, 0x30985, 0x331db, 0x359eb, 0x381b6, 0x3a93d, 0x3d081,
		0x3f782, 0x41e42, 0x444c1, 0x46b01, 0x49101, 0x4b6c4, 0x4dc49, 0x50191,
		0x5269e, 0x54b6f, 0x57006, 0x59463, 0x5b888, 0x5dc74, 0x60029, 0x623a7,
		0x646ee, 0x66a00, 0x68cdd, 0x6af86, 0x6d1fa, 0x6f43c, 0x7164b, 0x73829,
		0x759d4, 0x77b4f, 0x79c9a, 0x7bdb5, 0x7dea1, 0x7ff5e, 0x81fed, 0x8404e,
		0x86082, 0x88089, 0x8a064, 0x8c014, 0x8df98, 0x8fef1, 0x91e20, 0x93d26,
		0x95c01, 0x97ab4, 0x9993e, 0x9b79f, 0x9d5d9, 0x9f3ec, 0xa11d8, 0xa2f9d,
		0xa4d3c, 0xa6ab5, 0xa8808, 0xaa537, 0xac241, 0xadf26, 0xafbe7, 0xb1885,
		0xb3500, 0xb5157, 0xb6d8c, 0xb899f, 0xba58f, 0xbc15e, 0xbdd0c, 0xbf899,
		0xc1404, 0xc2f50, 0xc4a7b, 0xc6587, 0xc8073, 0xc9b3f, 0xcb5ed, 0xcd07c,
		0xceaec, 0xd053f, 0xd1f73, 0xd398a, 0xd5384, 0xd6d60, 0xd8720, 0xda0c3,
		0xdba4a, 0xdd3b4, 0xded03, 0xe0636, 0xe1f4e, 0xe384a, 0xe512c, 0xe69f3,
		0xe829f, 0xe9b31, 0xeb3a9, 0xecc08, 0xee44c, 0xefc78, 0xf148a, 0xf2c83,
		0xf4463, 0xf5c2a, 0xf73da, 0xf8b71, 0xfa2f0, 0xfba57, 0xfd1a7, 0xfe8df
	};
	static const char logSlopeTable[128] = {
		0x5c, 0x5c, 0x5b, 0x5a, 0x5a, 0x59, 0x58, 0x58,
		0x57, 0x56, 0x56, 0x55, 0x55, 0x54, 0x53, 0x53,
		0x52, 0x52, 0x51, 0x51, 0x50, 0x50, 0x4f, 0x4f,
		0x4e, 0x4d, 0x4d, 0x4d, 0x4c, 0x4c, 0x4b, 0x4b,
		0x4a, 0x4a, 0x49, 0x49, 0x48, 0x48, 0x47, 0x47,
		0x47, 0x46, 0x46, 0x45, 0x45, 0x45, 0x44, 0x44,
		0x43, 0x43, 0x43, 0x42, 0x42, 0x42, 0x41, 0x41,
		0x41, 0x40, 0x40, 0x40, 0x3f, 0x3f, 0x3f, 0x3e,
		0x3e, 0x3e, 0x3d, 0x3d, 0x3d, 0x3c, 0x3c, 0x3c,
		0x3b, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x3a, 0x39,
		0x39, 0x39, 0x39, 0x38, 0x38, 0x38, 0x38, 0x37,
		0x37, 0x37, 0x37, 0x36, 0x36, 0x36, 0x36, 0x35,
		0x35, 0x35, 0x35, 0x34, 0x34, 0x34, 0x34, 0x34,
		0x33, 0x33, 0x33, 0x33, 0x32, 0x32, 0x32, 0x32,
		0x32, 0x31, 0x31, 0x31, 0x31, 0x31, 0x30, 0x30,
		0x30, 0x30, 0x30, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f
	};
	int i;

	if (rate == 0)
		return 0;	/* Bail out if no leading "1" */
	rate *= 11185;		/* Scale 48000 to 0x20002380 */
	for (i = 31; i > 0; i--) {
		if (rate & 0x80000000) {	/* Detect leading "1" */
			return (((unsigned int) (i - 15) << 20) +
			       logMagTable[0x7f & (rate >> 24)] +
					(0x7f & (rate >> 17)) *
					logSlopeTable[0x7f & (rate >> 24)]);
		}
		rate <<= 1;
	}

	return 0;		/* Should never reach this point */
}

