/*
 **********************************************************************
 *     hwaccess.c -- Hardware access layer
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     December 9, 1999     Jon Taylor      rewrote the I/O subsystem
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

#include <asm/io.h>

#include "hwaccess.h"
#include "8010.h"
#include "icardmid.h"

/*************************************************************************
* Function : srToPitch                                                   *
* Input    : sampleRate - sampling rate                                  *
* Return   : pitch value                                                 *
* About    : convert sampling rate to pitch                              *
* Note     : for 8010, sampling rate is at 48kHz, this function should   *
*            be changed.                                                 *
*************************************************************************/
u32 srToPitch(u32 sampleRate)
{
	int i;

	/* FIXME: These tables should be defined in a headerfile */
	static u32 logMagTable[128] = {
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

	static char logSlopeTable[128] = {
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

	if (sampleRate == 0)
		return 0;	/* Bail out if no leading "1" */

	sampleRate *= 11185;	/* Scale 48000 to 0x20002380 */

	for (i = 31; i > 0; i--) {
		if (sampleRate & 0x80000000) {	/* Detect leading "1" */
			return (u32) (((s32) (i - 15) << 20) +
				      logMagTable[0x7f & (sampleRate >> 24)] +
				      (0x7f & (sampleRate >> 17)) * logSlopeTable[0x7f & (sampleRate >> 24)]);
		}
		sampleRate = sampleRate << 1;
	}

	DPF(2, "srToPitch: BUG!\n");
	return 0;		/* Should never reach this point */
}

/*******************************************
* write/read PCI function 0 registers      *
********************************************/
void emu10k1_writefn0(struct emu10k1_card *card, u32 reg, u32 data)
{
	unsigned long flags;

	if (reg & 0xff000000) {
		u32 mask;
		u8 size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		data = (data << offset) & mask;
		reg &= 0x7f;

		spin_lock_irqsave(&card->lock, flags);
		data |= inl(card->iobase + reg) & ~mask;
		outl(data, card->iobase + reg);
		spin_unlock_irqrestore(&card->lock, flags);
	} else {
		spin_lock_irqsave(&card->lock, flags);
		outl(data, card->iobase + reg);
		spin_unlock_irqrestore(&card->lock, flags);
	}

	return;
}

#ifdef DBGEMU
void emu10k1_writefn0_2(struct emu10k1_card *card, u32 reg, u32 data, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	if (size == 32)
		outl(data, card->iobase + (reg & 0x1F));
	else if (size == 16)
		outw(data, card->iobase + (reg & 0x1F));
	else
		outb(data, card->iobase + (reg & 0x1F));

	spin_unlock_irqrestore(&card->lock, flags);

	return;
}
#endif  /*  DBGEMU  */

u32 emu10k1_readfn0(struct emu10k1_card * card, u32 reg)
{
	u32 val;
	unsigned long flags;

	if (reg & 0xff000000) {
		u32 mask;
		u8 size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		reg &= 0x7f;

		spin_lock_irqsave(&card->lock, flags);
		val = inl(card->iobase + reg);
		spin_unlock_irqrestore(&card->lock, flags);

		return (val & mask) >> offset;
        } else {
		spin_lock_irqsave(&card->lock, flags);
		val = inl(card->iobase + reg);
		spin_unlock_irqrestore(&card->lock, flags);
		return val;
	}
}

void emu10k1_timer_set(struct emu10k1_card * card, u16 data)
{
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	outw(data & TIMER_RATE_MASK, card->iobase + TIMER);
	spin_unlock_irqrestore(&card->lock, flags);
}

/************************************************************************
* write/read Emu10k1 pointer-offset register set, accessed through      *
*  the PTR and DATA registers                                           *
*************************************************************************/
#define A_PTR_ADDRESS_MASK 0x0fff0000
void sblive_writeptr(struct emu10k1_card *card, u32 reg, u32 channel, u32 data)
{
	u32 regptr;
	unsigned long flags;

	regptr = ((reg << 16) & A_PTR_ADDRESS_MASK) | (channel & PTR_CHANNELNUM_MASK);

	if (reg & 0xff000000) {
		u32 mask;
		u8 size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;
		data = (data << offset) & mask;

		spin_lock_irqsave(&card->lock, flags);
		outl(regptr, card->iobase + PTR);
		data |= inl(card->iobase + DATA) & ~mask;
		outl(data, card->iobase + DATA);
		spin_unlock_irqrestore(&card->lock, flags);
	} else {
		spin_lock_irqsave(&card->lock, flags);
		outl(regptr, card->iobase + PTR);
		outl(data, card->iobase + DATA);
		spin_unlock_irqrestore(&card->lock, flags);
	}
}

/* ... :  data, reg, ... , TAGLIST_END */
void sblive_writeptr_tag(struct emu10k1_card *card, u32 channel, ...)
{
	va_list args;

	unsigned long flags;
        u32 reg;

	va_start(args, channel);

	spin_lock_irqsave(&card->lock, flags);
	while ((reg = va_arg(args, u32)) != TAGLIST_END) {
		u32 data = va_arg(args, u32);
		u32 regptr = (((reg << 16) & A_PTR_ADDRESS_MASK)
			      | (channel & PTR_CHANNELNUM_MASK));
		outl(regptr, card->iobase + PTR);
		if (reg & 0xff000000) {
			int size = (reg >> 24) & 0x3f;
                        int offset = (reg >> 16) & 0x1f;
			u32 mask = ((1 << size) - 1) << offset;
			data = (data << offset) & mask;

			data |= inl(card->iobase + DATA) & ~mask;
		}
		outl(data, card->iobase + DATA);
	}
	spin_unlock_irqrestore(&card->lock, flags);

	va_end(args);

	return;
}

u32 sblive_readptr(struct emu10k1_card * card, u32 reg, u32 channel)
{
	u32 regptr, val;
	unsigned long flags;

	regptr = ((reg << 16) & A_PTR_ADDRESS_MASK) | (channel & PTR_CHANNELNUM_MASK);

	if (reg & 0xff000000) {
		u32 mask;
		u8 size, offset;

		size = (reg >> 24) & 0x3f;
		offset = (reg >> 16) & 0x1f;
		mask = ((1 << size) - 1) << offset;

		spin_lock_irqsave(&card->lock, flags);
		outl(regptr, card->iobase + PTR);
		val = inl(card->iobase + DATA);
		spin_unlock_irqrestore(&card->lock, flags);

		return (val & mask) >> offset;
	} else {
		spin_lock_irqsave(&card->lock, flags);
		outl(regptr, card->iobase + PTR);
		val = inl(card->iobase + DATA);
		spin_unlock_irqrestore(&card->lock, flags);

		return val;
	}
}

void emu10k1_irq_enable(struct emu10k1_card *card, u32 irq_mask)
{
	u32 val;
	unsigned long flags;

	DPF(2,"emu10k1_irq_enable()\n");

	spin_lock_irqsave(&card->lock, flags);
        val = inl(card->iobase + INTE) | irq_mask;
        outl(val, card->iobase + INTE);
	spin_unlock_irqrestore(&card->lock, flags);
	return;
}

void emu10k1_irq_disable(struct emu10k1_card *card, u32 irq_mask)
{
        u32 val;
        unsigned long flags;

        DPF(2,"emu10k1_irq_disable()\n");

        spin_lock_irqsave(&card->lock, flags);
        val = inl(card->iobase + INTE) & ~irq_mask;
        outl(val, card->iobase + INTE);
        spin_unlock_irqrestore(&card->lock, flags);
        return;
}

void emu10k1_clear_stop_on_loop(struct emu10k1_card *card, u32 voicenum)
{
	/* Voice interrupt */
	if (voicenum >= 32)
		sblive_writeptr(card, SOLEH | ((0x0100 | (voicenum - 32)) << 16), 0, 0);
	else
		sblive_writeptr(card, SOLEL | ((0x0100 | voicenum) << 16), 0, 0);

	return;
}

static void sblive_wcwait(struct emu10k1_card *card, u32 wait)
{
	volatile unsigned uCount;
	u32 newtime = 0, curtime;

	curtime = emu10k1_readfn0(card, WC_SAMPLECOUNTER);
	while (wait--) {
		uCount = 0;
		while (uCount++ < TIMEOUT) {
			newtime = emu10k1_readfn0(card, WC_SAMPLECOUNTER);
			if (newtime != curtime)
				break;
		}

		if (uCount >= TIMEOUT)
			break;

		curtime = newtime;
	}
}

u16 emu10k1_ac97_read(struct ac97_codec *codec, u8 reg)
{
	struct emu10k1_card *card = codec->private_data;
	u16 data;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	outb(reg, card->iobase + AC97ADDRESS);
	data = inw(card->iobase + AC97DATA);

	spin_unlock_irqrestore(&card->lock, flags);

	return data;
}

void emu10k1_ac97_write(struct ac97_codec *codec, u8 reg, u16 value)
{
	struct emu10k1_card *card = codec->private_data;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	outb(reg, card->iobase + AC97ADDRESS);
	outw(value, card->iobase + AC97DATA);
	outb( AC97_EXTENDED_ID, card->iobase + AC97ADDRESS); 
	spin_unlock_irqrestore(&card->lock, flags);
}

/*********************************************************
*            MPU access functions                        *
**********************************************************/

int emu10k1_mpu_write_data(struct emu10k1_card *card, u8 data)
{
	unsigned long flags;
	int ret;

	if (card->is_audigy) {
		if ((sblive_readptr(card, A_MUSTAT,0) & MUSTAT_ORDYN) == 0) {
			sblive_writeptr(card, A_MUDATA, 0, data);
			ret = 0;
		} else
			ret = -1;
	} else {
		spin_lock_irqsave(&card->lock, flags);

		if ((inb(card->iobase + MUSTAT) & MUSTAT_ORDYN) == 0) {
			outb(data, card->iobase + MUDATA);
			ret = 0;
		} else
			ret = -1;

		spin_unlock_irqrestore(&card->lock, flags);
	}

	return ret;
}

int emu10k1_mpu_read_data(struct emu10k1_card *card, u8 * data)
{
	unsigned long flags;
	int ret;

	if (card->is_audigy) {
		if ((sblive_readptr(card, A_MUSTAT,0) & MUSTAT_IRDYN) == 0) {
			*data = sblive_readptr(card, A_MUDATA,0);
			ret = 0;
		} else
			ret = -1;
	} else {
		spin_lock_irqsave(&card->lock, flags);

		if ((inb(card->iobase + MUSTAT) & MUSTAT_IRDYN) == 0) {
			*data = inb(card->iobase + MUDATA);
			ret = 0;
		} else
			ret = -1;

		spin_unlock_irqrestore(&card->lock, flags);
	}

	return ret;
}

int emu10k1_mpu_reset(struct emu10k1_card *card)
{
	u8 status;
	unsigned long flags;

	DPF(2, "emu10k1_mpu_reset()\n");
	if (card->is_audigy) {
		if (card->mpuacqcount == 0) {
			sblive_writeptr(card, A_MUCMD, 0, MUCMD_RESET);
			sblive_wcwait(card, 8);
			sblive_writeptr(card, A_MUCMD, 0, MUCMD_RESET);
			sblive_wcwait(card, 8);
			sblive_writeptr(card, A_MUCMD, 0, MUCMD_ENTERUARTMODE);
			sblive_wcwait(card, 8);
			status = sblive_readptr(card, A_MUDATA, 0);
			if (status == 0xfe)
				return 0;
			else
				return -1;
		}

		return 0;
	} else {
		if (card->mpuacqcount == 0) {
			spin_lock_irqsave(&card->lock, flags);
			outb(MUCMD_RESET, card->iobase + MUCMD);
			spin_unlock_irqrestore(&card->lock, flags);

			sblive_wcwait(card, 8);

			spin_lock_irqsave(&card->lock, flags);
			outb(MUCMD_RESET, card->iobase + MUCMD);
			spin_unlock_irqrestore(&card->lock, flags);

			sblive_wcwait(card, 8);

			spin_lock_irqsave(&card->lock, flags);
			outb(MUCMD_ENTERUARTMODE, card->iobase + MUCMD);
			spin_unlock_irqrestore(&card->lock, flags);

			sblive_wcwait(card, 8);

			spin_lock_irqsave(&card->lock, flags);
			status = inb(card->iobase + MUDATA);
			spin_unlock_irqrestore(&card->lock, flags);

			if (status == 0xfe)
				return 0;
			else
				return -1;
		}

		return 0;
	}
}

int emu10k1_mpu_acquire(struct emu10k1_card *card)
{
	/* FIXME: This should be a macro */
	++card->mpuacqcount;

	return 0;
}

int emu10k1_mpu_release(struct emu10k1_card *card)
{
	/* FIXME: this should be a macro */
	--card->mpuacqcount;

	return 0;
}
