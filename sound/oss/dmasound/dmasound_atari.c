// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/sound/oss/dmasound/dmasound_atari.c
 *
 *  Atari TT and Falcon DMA Sound Driver
 *
 *  See linux/sound/oss/dmasound/dmasound_core.c for copyright and credits
 *  prior to 28/01/2001
 *
 *  28/01/2001 [0.1] Iain Sandoe
 *		     - added versioning
 *		     - put in and populated the hardware_afmts field.
 *             [0.2] - put in SNDCTL_DSP_GETCAPS value.
 *  01/02/2001 [0.3] - put in default hard/soft settings.
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/soundcard.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <linux/uaccess.h>
#include <asm/atariints.h>
#include <asm/atari_stram.h>

#include "dmasound.h"

#define DMASOUND_ATARI_REVISION 0
#define DMASOUND_ATARI_EDITION 3

extern void atari_microwire_cmd(int cmd);

static int is_falcon;
static int write_sq_ignore_int;	/* ++TeSche: used for Falcon */

static int expand_bal;	/* Balance factor for expanding (not volume!) */
static int expand_data;	/* Data for expanding */


/*** Translations ************************************************************/


/* ++TeSche: radically changed for new expanding purposes...
 *
 * These two routines now deal with copying/expanding/translating the samples
 * from user space into our buffer at the right frequency. They take care about
 * how much data there's actually to read, how much buffer space there is and
 * to convert samples into the right frequency/encoding. They will only work on
 * complete samples so it may happen they leave some bytes in the input stream
 * if the user didn't write a multiple of the current sample size. They both
 * return the number of bytes they've used from both streams so you may detect
 * such a situation. Luckily all programs should be able to cope with that.
 *
 * I think I've optimized anything as far as one can do in plain C, all
 * variables should fit in registers and the loops are really short. There's
 * one loop for every possible situation. Writing a more generalized and thus
 * parameterized loop would only produce slower code. Feel free to optimize
 * this in assembler if you like. :)
 *
 * I think these routines belong here because they're not yet really hardware
 * independent, especially the fact that the Falcon can play 16bit samples
 * only in stereo is hardcoded in both of them!
 *
 * ++geert: split in even more functions (one per format)
 */

static ssize_t ata_ct_law(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t ata_ct_s8(const u_char __user *userPtr, size_t userCount,
			 u_char frame[], ssize_t *frameUsed,
			 ssize_t frameLeft);
static ssize_t ata_ct_u8(const u_char __user *userPtr, size_t userCount,
			 u_char frame[], ssize_t *frameUsed,
			 ssize_t frameLeft);
static ssize_t ata_ct_s16be(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t ata_ct_u16be(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t ata_ct_s16le(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t ata_ct_u16le(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t ata_ctx_law(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t ata_ctx_s8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t ata_ctx_u8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t ata_ctx_s16be(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft);
static ssize_t ata_ctx_u16be(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft);
static ssize_t ata_ctx_s16le(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft);
static ssize_t ata_ctx_u16le(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft);


/*** Low level stuff *********************************************************/


static void *AtaAlloc(unsigned int size, gfp_t flags);
static void AtaFree(void *, unsigned int size);
static int AtaIrqInit(void);
#ifdef MODULE
static void AtaIrqCleanUp(void);
#endif /* MODULE */
static int AtaSetBass(int bass);
static int AtaSetTreble(int treble);
static void TTSilence(void);
static void TTInit(void);
static int TTSetFormat(int format);
static int TTSetVolume(int volume);
static int TTSetGain(int gain);
static void FalconSilence(void);
static void FalconInit(void);
static int FalconSetFormat(int format);
static int FalconSetVolume(int volume);
static void AtaPlayNextFrame(int index);
static void AtaPlay(void);
static irqreturn_t AtaInterrupt(int irq, void *dummy);

/*** Mid level stuff *********************************************************/

static void TTMixerInit(void);
static void FalconMixerInit(void);
static int AtaMixerIoctl(u_int cmd, u_long arg);
static int TTMixerIoctl(u_int cmd, u_long arg);
static int FalconMixerIoctl(u_int cmd, u_long arg);
static int AtaWriteSqSetup(void);
static int AtaSqOpen(fmode_t mode);
static int TTStateInfo(char *buffer, size_t space);
static int FalconStateInfo(char *buffer, size_t space);


/*** Translations ************************************************************/


static ssize_t ata_ct_law(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	char *table = dmasound.soft.format == AFMT_MU_LAW ? dmasound_ulaw2dma8
							  : dmasound_alaw2dma8;
	ssize_t count, used;
	u_char *p = &frame[*frameUsed];

	count = min_t(unsigned long, userCount, frameLeft);
	if (dmasound.soft.stereo)
		count &= ~1;
	used = count;
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		*p++ = table[data];
		count--;
	}
	*frameUsed += used;
	return used;
}


static ssize_t ata_ct_s8(const u_char __user *userPtr, size_t userCount,
			 u_char frame[], ssize_t *frameUsed,
			 ssize_t frameLeft)
{
	ssize_t count, used;
	void *p = &frame[*frameUsed];

	count = min_t(unsigned long, userCount, frameLeft);
	if (dmasound.soft.stereo)
		count &= ~1;
	used = count;
	if (copy_from_user(p, userPtr, count))
		return -EFAULT;
	*frameUsed += used;
	return used;
}


static ssize_t ata_ct_u8(const u_char __user *userPtr, size_t userCount,
			 u_char frame[], ssize_t *frameUsed,
			 ssize_t frameLeft)
{
	ssize_t count, used;

	if (!dmasound.soft.stereo) {
		u_char *p = &frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft);
		used = count;
		while (count > 0) {
			u_char data;
			if (get_user(data, userPtr++))
				return -EFAULT;
			*p++ = data ^ 0x80;
			count--;
		}
	} else {
		u_short *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>1;
		used = count*2;
		while (count > 0) {
			u_short data;
			if (get_user(data, (u_short __user *)userPtr))
				return -EFAULT;
			userPtr += 2;
			*p++ = data ^ 0x8080;
			count--;
		}
	}
	*frameUsed += used;
	return used;
}


static ssize_t ata_ct_s16be(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	ssize_t count, used;

	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>1;
		used = count*2;
		while (count > 0) {
			u_short data;
			if (get_user(data, (u_short __user *)userPtr))
				return -EFAULT;
			userPtr += 2;
			*p++ = data;
			*p++ = data;
			count--;
		}
		*frameUsed += used*2;
	} else {
		void *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft) & ~3;
		used = count;
		if (copy_from_user(p, userPtr, count))
			return -EFAULT;
		*frameUsed += used;
	}
	return used;
}


static ssize_t ata_ct_u16be(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	ssize_t count, used;

	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>1;
		used = count*2;
		while (count > 0) {
			u_short data;
			if (get_user(data, (u_short __user *)userPtr))
				return -EFAULT;
			userPtr += 2;
			data ^= 0x8000;
			*p++ = data;
			*p++ = data;
			count--;
		}
		*frameUsed += used*2;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>2;
		used = count*4;
		while (count > 0) {
			u_int data;
			if (get_user(data, (u_int __user *)userPtr))
				return -EFAULT;
			userPtr += 4;
			*p++ = data ^ 0x80008000;
			count--;
		}
		*frameUsed += used;
	}
	return used;
}


static ssize_t ata_ct_s16le(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	ssize_t count, used;

	count = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>1;
		used = count*2;
		while (count > 0) {
			u_short data;
			if (get_user(data, (u_short __user *)userPtr))
				return -EFAULT;
			userPtr += 2;
			data = le2be16(data);
			*p++ = data;
			*p++ = data;
			count--;
		}
		*frameUsed += used*2;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>2;
		used = count*4;
		while (count > 0) {
			u_long data;
			if (get_user(data, (u_int __user *)userPtr))
				return -EFAULT;
			userPtr += 4;
			data = le2be16dbl(data);
			*p++ = data;
			count--;
		}
		*frameUsed += used;
	}
	return used;
}


static ssize_t ata_ct_u16le(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	ssize_t count, used;

	count = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>1;
		used = count*2;
		while (count > 0) {
			u_short data;
			if (get_user(data, (u_short __user *)userPtr))
				return -EFAULT;
			userPtr += 2;
			data = le2be16(data) ^ 0x8000;
			*p++ = data;
			*p++ = data;
		}
		*frameUsed += used*2;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		count = min_t(unsigned long, userCount, frameLeft)>>2;
		used = count;
		while (count > 0) {
			u_long data;
			if (get_user(data, (u_int __user *)userPtr))
				return -EFAULT;
			userPtr += 4;
			data = le2be16dbl(data) ^ 0x80008000;
			*p++ = data;
			count--;
		}
		*frameUsed += used;
	}
	return used;
}


static ssize_t ata_ctx_law(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	char *table = dmasound.soft.format == AFMT_MU_LAW ? dmasound_ulaw2dma8
							  : dmasound_alaw2dma8;
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_char *p = &frame[*frameUsed];
		u_char data = expand_data;
		while (frameLeft) {
			u_char c;
			if (bal < 0) {
				if (!userCount)
					break;
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = table[c];
				userCount--;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft--;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 2) {
			u_char c;
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = table[c] << 8;
				if (get_user(c, userPtr++))
					return -EFAULT;
				data |= table[c];
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 2;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_s8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_char *p = &frame[*frameUsed];
		u_char data = expand_data;
		while (frameLeft) {
			if (bal < 0) {
				if (!userCount)
					break;
				if (get_user(data, userPtr++))
					return -EFAULT;
				userCount--;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft--;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 2) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 2;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_u8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_char *p = &frame[*frameUsed];
		u_char data = expand_data;
		while (frameLeft) {
			if (bal < 0) {
				if (!userCount)
					break;
				if (get_user(data, userPtr++))
					return -EFAULT;
				data ^= 0x80;
				userCount--;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft--;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 2) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				data ^= 0x8080;
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 2;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_s16be(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		u_long data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 4)
					break;
				if (get_user(data, (u_int __user *)userPtr))
					return -EFAULT;
				userPtr += 4;
				userCount -= 4;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_u16be(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				data ^= 0x8000;
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		u_long data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 4)
					break;
				if (get_user(data, (u_int __user *)userPtr))
					return -EFAULT;
				userPtr += 4;
				data ^= 0x80008000;
				userCount -= 4;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_s16le(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				data = le2be16(data);
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		u_long data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 4)
					break;
				if (get_user(data, (u_int __user *)userPtr))
					return -EFAULT;
				userPtr += 4;
				data = le2be16dbl(data);
				userCount -= 4;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static ssize_t ata_ctx_u16le(const u_char __user *userPtr, size_t userCount,
			     u_char frame[], ssize_t *frameUsed,
			     ssize_t frameLeft)
{
	/* this should help gcc to stuff everything into registers */
	long bal = expand_bal;
	long hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	ssize_t used, usedf;

	used = userCount;
	usedf = frameLeft;
	if (!dmasound.soft.stereo) {
		u_short *p = (u_short *)&frame[*frameUsed];
		u_short data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 2)
					break;
				if (get_user(data, (u_short __user *)userPtr))
					return -EFAULT;
				userPtr += 2;
				data = le2be16(data) ^ 0x8000;
				userCount -= 2;
				bal += hSpeed;
			}
			*p++ = data;
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	} else {
		u_long *p = (u_long *)&frame[*frameUsed];
		u_long data = expand_data;
		while (frameLeft >= 4) {
			if (bal < 0) {
				if (userCount < 4)
					break;
				if (get_user(data, (u_int __user *)userPtr))
					return -EFAULT;
				userPtr += 4;
				data = le2be16dbl(data) ^ 0x80008000;
				userCount -= 4;
				bal += hSpeed;
			}
			*p++ = data;
			frameLeft -= 4;
			bal -= sSpeed;
		}
		expand_data = data;
	}
	expand_bal = bal;
	used -= userCount;
	*frameUsed += usedf-frameLeft;
	return used;
}


static TRANS transTTNormal = {
	.ct_ulaw	= ata_ct_law,
	.ct_alaw	= ata_ct_law,
	.ct_s8		= ata_ct_s8,
	.ct_u8		= ata_ct_u8,
};

static TRANS transTTExpanding = {
	.ct_ulaw	= ata_ctx_law,
	.ct_alaw	= ata_ctx_law,
	.ct_s8		= ata_ctx_s8,
	.ct_u8		= ata_ctx_u8,
};

static TRANS transFalconNormal = {
	.ct_ulaw	= ata_ct_law,
	.ct_alaw	= ata_ct_law,
	.ct_s8		= ata_ct_s8,
	.ct_u8		= ata_ct_u8,
	.ct_s16be	= ata_ct_s16be,
	.ct_u16be	= ata_ct_u16be,
	.ct_s16le	= ata_ct_s16le,
	.ct_u16le	= ata_ct_u16le
};

static TRANS transFalconExpanding = {
	.ct_ulaw	= ata_ctx_law,
	.ct_alaw	= ata_ctx_law,
	.ct_s8		= ata_ctx_s8,
	.ct_u8		= ata_ctx_u8,
	.ct_s16be	= ata_ctx_s16be,
	.ct_u16be	= ata_ctx_u16be,
	.ct_s16le	= ata_ctx_s16le,
	.ct_u16le	= ata_ctx_u16le,
};


/*** Low level stuff *********************************************************/



/*
 * Atari (TT/Falcon)
 */

static void *AtaAlloc(unsigned int size, gfp_t flags)
{
	return atari_stram_alloc(size, "dmasound");
}

static void AtaFree(void *obj, unsigned int size)
{
	atari_stram_free( obj );
}

static int __init AtaIrqInit(void)
{
	/* Set up timer A. Timer A
	   will receive a signal upon end of playing from the sound
	   hardware. Furthermore Timer A is able to count events
	   and will cause an interrupt after a programmed number
	   of events. So all we need to keep the music playing is
	   to provide the sound hardware with new data upon
	   an interrupt from timer A. */
	st_mfp.tim_ct_a = 0;	/* ++roman: Stop timer before programming! */
	st_mfp.tim_dt_a = 1;	/* Cause interrupt after first event. */
	st_mfp.tim_ct_a = 8;	/* Turn on event counting. */
	/* Register interrupt handler. */
	if (request_irq(IRQ_MFP_TIMA, AtaInterrupt, 0, "DMA sound",
			AtaInterrupt))
		return 0;
	st_mfp.int_en_a |= 0x20;	/* Turn interrupt on. */
	st_mfp.int_mk_a |= 0x20;
	return 1;
}

#ifdef MODULE
static void AtaIrqCleanUp(void)
{
	st_mfp.tim_ct_a = 0;		/* stop timer */
	st_mfp.int_en_a &= ~0x20;	/* turn interrupt off */
	free_irq(IRQ_MFP_TIMA, AtaInterrupt);
}
#endif /* MODULE */


#define TONE_VOXWARE_TO_DB(v) \
	(((v) < 0) ? -12 : ((v) > 100) ? 12 : ((v) - 50) * 6 / 25)
#define TONE_DB_TO_VOXWARE(v) (((v) * 25 + ((v) > 0 ? 5 : -5)) / 6 + 50)


static int AtaSetBass(int bass)
{
	dmasound.bass = TONE_VOXWARE_TO_DB(bass);
	atari_microwire_cmd(MW_LM1992_BASS(dmasound.bass));
	return TONE_DB_TO_VOXWARE(dmasound.bass);
}


static int AtaSetTreble(int treble)
{
	dmasound.treble = TONE_VOXWARE_TO_DB(treble);
	atari_microwire_cmd(MW_LM1992_TREBLE(dmasound.treble));
	return TONE_DB_TO_VOXWARE(dmasound.treble);
}



/*
 * TT
 */


static void TTSilence(void)
{
	tt_dmasnd.ctrl = DMASND_CTRL_OFF;
	atari_microwire_cmd(MW_LM1992_PSG_HIGH); /* mix in PSG signal 1:1 */
}


static void TTInit(void)
{
	int mode, i, idx;
	const int freq[4] = {50066, 25033, 12517, 6258};

	/* search a frequency that fits into the allowed error range */

	idx = -1;
	for (i = 0; i < ARRAY_SIZE(freq); i++)
		/* this isn't as much useful for a TT than for a Falcon, but
		 * then it doesn't hurt very much to implement it for a TT too.
		 */
		if ((100 * abs(dmasound.soft.speed - freq[i]) / freq[i]) < catchRadius)
			idx = i;
	if (idx > -1) {
		dmasound.soft.speed = freq[idx];
		dmasound.trans_write = &transTTNormal;
	} else
		dmasound.trans_write = &transTTExpanding;

	TTSilence();
	dmasound.hard = dmasound.soft;

	if (dmasound.hard.speed > 50066) {
		/* we would need to squeeze the sound, but we won't do that */
		dmasound.hard.speed = 50066;
		mode = DMASND_MODE_50KHZ;
		dmasound.trans_write = &transTTNormal;
	} else if (dmasound.hard.speed > 25033) {
		dmasound.hard.speed = 50066;
		mode = DMASND_MODE_50KHZ;
	} else if (dmasound.hard.speed > 12517) {
		dmasound.hard.speed = 25033;
		mode = DMASND_MODE_25KHZ;
	} else if (dmasound.hard.speed > 6258) {
		dmasound.hard.speed = 12517;
		mode = DMASND_MODE_12KHZ;
	} else {
		dmasound.hard.speed = 6258;
		mode = DMASND_MODE_6KHZ;
	}

	tt_dmasnd.mode = (dmasound.hard.stereo ?
			  DMASND_MODE_STEREO : DMASND_MODE_MONO) |
		DMASND_MODE_8BIT | mode;

	expand_bal = -dmasound.soft.speed;
}


static int TTSetFormat(int format)
{
	/* TT sound DMA supports only 8bit modes */

	switch (format) {
	case AFMT_QUERY:
		return dmasound.soft.format;
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_S8:
	case AFMT_U8:
		break;
	default:
		format = AFMT_S8;
	}

	dmasound.soft.format = format;
	dmasound.soft.size = 8;
	if (dmasound.minDev == SND_DEV_DSP) {
		dmasound.dsp.format = format;
		dmasound.dsp.size = 8;
	}
	TTInit();

	return format;
}


#define VOLUME_VOXWARE_TO_DB(v) \
	(((v) < 0) ? -40 : ((v) > 100) ? 0 : ((v) * 2) / 5 - 40)
#define VOLUME_DB_TO_VOXWARE(v) ((((v) + 40) * 5 + 1) / 2)


static int TTSetVolume(int volume)
{
	dmasound.volume_left = VOLUME_VOXWARE_TO_DB(volume & 0xff);
	atari_microwire_cmd(MW_LM1992_BALLEFT(dmasound.volume_left));
	dmasound.volume_right = VOLUME_VOXWARE_TO_DB((volume & 0xff00) >> 8);
	atari_microwire_cmd(MW_LM1992_BALRIGHT(dmasound.volume_right));
	return VOLUME_DB_TO_VOXWARE(dmasound.volume_left) |
	       (VOLUME_DB_TO_VOXWARE(dmasound.volume_right) << 8);
}


#define GAIN_VOXWARE_TO_DB(v) \
	(((v) < 0) ? -80 : ((v) > 100) ? 0 : ((v) * 4) / 5 - 80)
#define GAIN_DB_TO_VOXWARE(v) ((((v) + 80) * 5 + 1) / 4)

static int TTSetGain(int gain)
{
	dmasound.gain = GAIN_VOXWARE_TO_DB(gain);
	atari_microwire_cmd(MW_LM1992_VOLUME(dmasound.gain));
	return GAIN_DB_TO_VOXWARE(dmasound.gain);
}



/*
 * Falcon
 */


static void FalconSilence(void)
{
	/* stop playback, set sample rate 50kHz for PSG sound */
	tt_dmasnd.ctrl = DMASND_CTRL_OFF;
	tt_dmasnd.mode = DMASND_MODE_50KHZ | DMASND_MODE_STEREO | DMASND_MODE_8BIT;
	tt_dmasnd.int_div = 0; /* STE compatible divider */
	tt_dmasnd.int_ctrl = 0x0;
	tt_dmasnd.cbar_src = 0x0000; /* no matrix inputs */
	tt_dmasnd.cbar_dst = 0x0000; /* no matrix outputs */
	tt_dmasnd.dac_src = 1; /* connect ADC to DAC, disconnect matrix */
	tt_dmasnd.adc_src = 3; /* ADC Input = PSG */
}


static void FalconInit(void)
{
	int divider, i, idx;
	const int freq[8] = {49170, 32780, 24585, 19668, 16390, 12292, 9834, 8195};

	/* search a frequency that fits into the allowed error range */

	idx = -1;
	for (i = 0; i < ARRAY_SIZE(freq); i++)
		/* if we will tolerate 3% error 8000Hz->8195Hz (2.38%) would
		 * be playable without expanding, but that now a kernel runtime
		 * option
		 */
		if ((100 * abs(dmasound.soft.speed - freq[i]) / freq[i]) < catchRadius)
			idx = i;
	if (idx > -1) {
		dmasound.soft.speed = freq[idx];
		dmasound.trans_write = &transFalconNormal;
	} else
		dmasound.trans_write = &transFalconExpanding;

	FalconSilence();
	dmasound.hard = dmasound.soft;

	if (dmasound.hard.size == 16) {
		/* the Falcon can play 16bit samples only in stereo */
		dmasound.hard.stereo = 1;
	}

	if (dmasound.hard.speed > 49170) {
		/* we would need to squeeze the sound, but we won't do that */
		dmasound.hard.speed = 49170;
		divider = 1;
		dmasound.trans_write = &transFalconNormal;
	} else if (dmasound.hard.speed > 32780) {
		dmasound.hard.speed = 49170;
		divider = 1;
	} else if (dmasound.hard.speed > 24585) {
		dmasound.hard.speed = 32780;
		divider = 2;
	} else if (dmasound.hard.speed > 19668) {
		dmasound.hard.speed = 24585;
		divider = 3;
	} else if (dmasound.hard.speed > 16390) {
		dmasound.hard.speed = 19668;
		divider = 4;
	} else if (dmasound.hard.speed > 12292) {
		dmasound.hard.speed = 16390;
		divider = 5;
	} else if (dmasound.hard.speed > 9834) {
		dmasound.hard.speed = 12292;
		divider = 7;
	} else if (dmasound.hard.speed > 8195) {
		dmasound.hard.speed = 9834;
		divider = 9;
	} else {
		dmasound.hard.speed = 8195;
		divider = 11;
	}
	tt_dmasnd.int_div = divider;

	/* Setup Falcon sound DMA for playback */
	tt_dmasnd.int_ctrl = 0x4; /* Timer A int at play end */
	tt_dmasnd.track_select = 0x0; /* play 1 track, track 1 */
	tt_dmasnd.cbar_src = 0x0001; /* DMA(25MHz) --> DAC */
	tt_dmasnd.cbar_dst = 0x0000;
	tt_dmasnd.rec_track_select = 0;
	tt_dmasnd.dac_src = 2; /* connect matrix to DAC */
	tt_dmasnd.adc_src = 0; /* ADC Input = Mic */

	tt_dmasnd.mode = (dmasound.hard.stereo ?
			  DMASND_MODE_STEREO : DMASND_MODE_MONO) |
		((dmasound.hard.size == 8) ?
		 DMASND_MODE_8BIT : DMASND_MODE_16BIT) |
		DMASND_MODE_6KHZ;

	expand_bal = -dmasound.soft.speed;
}


static int FalconSetFormat(int format)
{
	int size;
	/* Falcon sound DMA supports 8bit and 16bit modes */

	switch (format) {
	case AFMT_QUERY:
		return dmasound.soft.format;
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
		size = 8;
		break;
	case AFMT_S16_BE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_U16_LE:
		size = 16;
		break;
	default: /* :-) */
		size = 8;
		format = AFMT_S8;
	}

	dmasound.soft.format = format;
	dmasound.soft.size = size;
	if (dmasound.minDev == SND_DEV_DSP) {
		dmasound.dsp.format = format;
		dmasound.dsp.size = dmasound.soft.size;
	}

	FalconInit();

	return format;
}


/* This is for the Falcon output *attenuation* in 1.5dB steps,
 * i.e. output level from 0 to -22.5dB in -1.5dB steps.
 */
#define VOLUME_VOXWARE_TO_ATT(v) \
	((v) < 0 ? 15 : (v) > 100 ? 0 : 15 - (v) * 3 / 20)
#define VOLUME_ATT_TO_VOXWARE(v) (100 - (v) * 20 / 3)


static int FalconSetVolume(int volume)
{
	dmasound.volume_left = VOLUME_VOXWARE_TO_ATT(volume & 0xff);
	dmasound.volume_right = VOLUME_VOXWARE_TO_ATT((volume & 0xff00) >> 8);
	tt_dmasnd.output_atten = dmasound.volume_left << 8 | dmasound.volume_right << 4;
	return VOLUME_ATT_TO_VOXWARE(dmasound.volume_left) |
	       VOLUME_ATT_TO_VOXWARE(dmasound.volume_right) << 8;
}


static void AtaPlayNextFrame(int index)
{
	char *start, *end;

	/* used by AtaPlay() if all doubts whether there really is something
	 * to be played are already wiped out.
	 */
	start = write_sq.buffers[write_sq.front];
	end = start+((write_sq.count == index) ? write_sq.rear_size
					       : write_sq.block_size);
	/* end might not be a legal virtual address. */
	DMASNDSetEnd(virt_to_phys(end - 1) + 1);
	DMASNDSetBase(virt_to_phys(start));
	/* Since only an even number of samples per frame can
	   be played, we might lose one byte here. (TO DO) */
	write_sq.front = (write_sq.front+1) % write_sq.max_count;
	write_sq.active++;
	tt_dmasnd.ctrl = DMASND_CTRL_ON | DMASND_CTRL_REPEAT;
}


static void AtaPlay(void)
{
	/* ++TeSche: Note that write_sq.active is no longer just a flag but
	 * holds the number of frames the DMA is currently programmed for
	 * instead, may be 0, 1 (currently being played) or 2 (pre-programmed).
	 *
	 * Changes done to write_sq.count and write_sq.active are a bit more
	 * subtle again so now I must admit I also prefer disabling the irq
	 * here rather than considering all possible situations. But the point
	 * is that disabling the irq doesn't have any bad influence on this
	 * version of the driver as we benefit from having pre-programmed the
	 * DMA wherever possible: There's no need to reload the DMA at the
	 * exact time of an interrupt but only at some time while the
	 * pre-programmed frame is playing!
	 */
	atari_disable_irq(IRQ_MFP_TIMA);

	if (write_sq.active == 2 ||	/* DMA is 'full' */
	    write_sq.count <= 0) {	/* nothing to do */
		atari_enable_irq(IRQ_MFP_TIMA);
		return;
	}

	if (write_sq.active == 0) {
		/* looks like there's nothing 'in' the DMA yet, so try
		 * to put two frames into it (at least one is available).
		 */
		if (write_sq.count == 1 &&
		    write_sq.rear_size < write_sq.block_size &&
		    !write_sq.syncing) {
			/* hmmm, the only existing frame is not
			 * yet filled and we're not syncing?
			 */
			atari_enable_irq(IRQ_MFP_TIMA);
			return;
		}
		AtaPlayNextFrame(1);
		if (write_sq.count == 1) {
			/* no more frames */
			atari_enable_irq(IRQ_MFP_TIMA);
			return;
		}
		if (write_sq.count == 2 &&
		    write_sq.rear_size < write_sq.block_size &&
		    !write_sq.syncing) {
			/* hmmm, there were two frames, but the second
			 * one is not yet filled and we're not syncing?
			 */
			atari_enable_irq(IRQ_MFP_TIMA);
			return;
		}
		AtaPlayNextFrame(2);
	} else {
		/* there's already a frame being played so we may only stuff
		 * one new into the DMA, but even if this may be the last
		 * frame existing the previous one is still on write_sq.count.
		 */
		if (write_sq.count == 2 &&
		    write_sq.rear_size < write_sq.block_size &&
		    !write_sq.syncing) {
			/* hmmm, the only existing frame is not
			 * yet filled and we're not syncing?
			 */
			atari_enable_irq(IRQ_MFP_TIMA);
			return;
		}
		AtaPlayNextFrame(2);
	}
	atari_enable_irq(IRQ_MFP_TIMA);
}


static irqreturn_t AtaInterrupt(int irq, void *dummy)
{
#if 0
	/* ++TeSche: if you should want to test this... */
	static int cnt;
	if (write_sq.active == 2)
		if (++cnt == 10) {
			/* simulate losing an interrupt */
			cnt = 0;
			return IRQ_HANDLED;
		}
#endif
	spin_lock(&dmasound.lock);
	if (write_sq_ignore_int && is_falcon) {
		/* ++TeSche: Falcon only: ignore first irq because it comes
		 * immediately after starting a frame. after that, irqs come
		 * (almost) like on the TT.
		 */
		write_sq_ignore_int = 0;
		goto out;
	}

	if (!write_sq.active) {
		/* playing was interrupted and sq_reset() has already cleared
		 * the sq variables, so better don't do anything here.
		 */
		WAKE_UP(write_sq.sync_queue);
		goto out;
	}

	/* Probably ;) one frame is finished. Well, in fact it may be that a
	 * pre-programmed one is also finished because there has been a long
	 * delay in interrupt delivery and we've completely lost one, but
	 * there's no way to detect such a situation. In such a case the last
	 * frame will be played more than once and the situation will recover
	 * as soon as the irq gets through.
	 */
	write_sq.count--;
	write_sq.active--;

	if (!write_sq.active) {
		tt_dmasnd.ctrl = DMASND_CTRL_OFF;
		write_sq_ignore_int = 1;
	}

	WAKE_UP(write_sq.action_queue);
	/* At least one block of the queue is free now
	   so wake up a writing process blocked because
	   of a full queue. */

	if ((write_sq.active != 1) || (write_sq.count != 1))
		/* We must be a bit carefully here: write_sq.count indicates the
		 * number of buffers used and not the number of frames to be
		 * played. If write_sq.count==1 and write_sq.active==1 that
		 * means the only remaining frame was already programmed
		 * earlier (and is currently running) so we mustn't call
		 * AtaPlay() here, otherwise we'll play one frame too much.
		 */
		AtaPlay();

	if (!write_sq.active) WAKE_UP(write_sq.sync_queue);
	/* We are not playing after AtaPlay(), so there
	   is nothing to play any more. Wake up a process
	   waiting for audio output to drain. */
out:
	spin_unlock(&dmasound.lock);
	return IRQ_HANDLED;
}


/*** Mid level stuff *********************************************************/


/*
 * /dev/mixer abstraction
 */

#define RECLEVEL_VOXWARE_TO_GAIN(v)	\
	((v) < 0 ? 0 : (v) > 100 ? 15 : (v) * 3 / 20)
#define RECLEVEL_GAIN_TO_VOXWARE(v)	(((v) * 20 + 2) / 3)


static void __init TTMixerInit(void)
{
	atari_microwire_cmd(MW_LM1992_VOLUME(0));
	dmasound.volume_left = 0;
	atari_microwire_cmd(MW_LM1992_BALLEFT(0));
	dmasound.volume_right = 0;
	atari_microwire_cmd(MW_LM1992_BALRIGHT(0));
	atari_microwire_cmd(MW_LM1992_TREBLE(0));
	atari_microwire_cmd(MW_LM1992_BASS(0));
}

static void __init FalconMixerInit(void)
{
	dmasound.volume_left = (tt_dmasnd.output_atten & 0xf00) >> 8;
	dmasound.volume_right = (tt_dmasnd.output_atten & 0xf0) >> 4;
}

static int AtaMixerIoctl(u_int cmd, u_long arg)
{
	int data;
	unsigned long flags;
	switch (cmd) {
	    case SOUND_MIXER_READ_SPEAKER:
		    if (is_falcon || MACH_IS_TT) {
			    int porta;
			    spin_lock_irqsave(&dmasound.lock, flags);
			    sound_ym.rd_data_reg_sel = 14;
			    porta = sound_ym.rd_data_reg_sel;
			    spin_unlock_irqrestore(&dmasound.lock, flags);
			    return IOCTL_OUT(arg, porta & 0x40 ? 0 : 100);
		    }
		    break;
	    case SOUND_MIXER_WRITE_VOLUME:
		    IOCTL_IN(arg, data);
		    return IOCTL_OUT(arg, dmasound_set_volume(data));
	    case SOUND_MIXER_WRITE_SPEAKER:
		    if (is_falcon || MACH_IS_TT) {
			    int porta;
			    IOCTL_IN(arg, data);
			    spin_lock_irqsave(&dmasound.lock, flags);
			    sound_ym.rd_data_reg_sel = 14;
			    porta = (sound_ym.rd_data_reg_sel & ~0x40) |
				    (data < 50 ? 0x40 : 0);
			    sound_ym.wd_data = porta;
			    spin_unlock_irqrestore(&dmasound.lock, flags);
			    return IOCTL_OUT(arg, porta & 0x40 ? 0 : 100);
		    }
	}
	return -EINVAL;
}


static int TTMixerIoctl(u_int cmd, u_long arg)
{
	int data;
	switch (cmd) {
	    case SOUND_MIXER_READ_RECMASK:
		return IOCTL_OUT(arg, 0);
	    case SOUND_MIXER_READ_DEVMASK:
		return IOCTL_OUT(arg,
				 SOUND_MASK_VOLUME | SOUND_MASK_TREBLE | SOUND_MASK_BASS |
				 (MACH_IS_TT ? SOUND_MASK_SPEAKER : 0));
	    case SOUND_MIXER_READ_STEREODEVS:
		return IOCTL_OUT(arg, SOUND_MASK_VOLUME);
	    case SOUND_MIXER_READ_VOLUME:
		return IOCTL_OUT(arg,
				 VOLUME_DB_TO_VOXWARE(dmasound.volume_left) |
				 (VOLUME_DB_TO_VOXWARE(dmasound.volume_right) << 8));
	    case SOUND_MIXER_READ_BASS:
		return IOCTL_OUT(arg, TONE_DB_TO_VOXWARE(dmasound.bass));
	    case SOUND_MIXER_READ_TREBLE:
		return IOCTL_OUT(arg, TONE_DB_TO_VOXWARE(dmasound.treble));
	    case SOUND_MIXER_READ_OGAIN:
		return IOCTL_OUT(arg, GAIN_DB_TO_VOXWARE(dmasound.gain));
	    case SOUND_MIXER_WRITE_BASS:
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, dmasound_set_bass(data));
	    case SOUND_MIXER_WRITE_TREBLE:
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, dmasound_set_treble(data));
	    case SOUND_MIXER_WRITE_OGAIN:
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, dmasound_set_gain(data));
	}
	return AtaMixerIoctl(cmd, arg);
}

static int FalconMixerIoctl(u_int cmd, u_long arg)
{
	int data;
	switch (cmd) {
	case SOUND_MIXER_READ_RECMASK:
		return IOCTL_OUT(arg, SOUND_MASK_MIC);
	case SOUND_MIXER_READ_DEVMASK:
		return IOCTL_OUT(arg, SOUND_MASK_VOLUME | SOUND_MASK_MIC | SOUND_MASK_SPEAKER);
	case SOUND_MIXER_READ_STEREODEVS:
		return IOCTL_OUT(arg, SOUND_MASK_VOLUME | SOUND_MASK_MIC);
	case SOUND_MIXER_READ_VOLUME:
		return IOCTL_OUT(arg,
			VOLUME_ATT_TO_VOXWARE(dmasound.volume_left) |
			VOLUME_ATT_TO_VOXWARE(dmasound.volume_right) << 8);
	case SOUND_MIXER_READ_CAPS:
		return IOCTL_OUT(arg, SOUND_CAP_EXCL_INPUT);
	case SOUND_MIXER_WRITE_MIC:
		IOCTL_IN(arg, data);
		tt_dmasnd.input_gain =
			RECLEVEL_VOXWARE_TO_GAIN(data & 0xff) << 4 |
			RECLEVEL_VOXWARE_TO_GAIN(data >> 8 & 0xff);
		fallthrough;	/* return set value */
	case SOUND_MIXER_READ_MIC:
		return IOCTL_OUT(arg,
			RECLEVEL_GAIN_TO_VOXWARE(tt_dmasnd.input_gain >> 4 & 0xf) |
			RECLEVEL_GAIN_TO_VOXWARE(tt_dmasnd.input_gain & 0xf) << 8);
	}
	return AtaMixerIoctl(cmd, arg);
}

static int AtaWriteSqSetup(void)
{
	write_sq_ignore_int = 0;
	return 0 ;
}

static int AtaSqOpen(fmode_t mode)
{
	write_sq_ignore_int = 1;
	return 0 ;
}

static int TTStateInfo(char *buffer, size_t space)
{
	int len = 0;
	len += sprintf(buffer+len, "\tvol left  %ddB [-40...  0]\n",
		       dmasound.volume_left);
	len += sprintf(buffer+len, "\tvol right %ddB [-40...  0]\n",
		       dmasound.volume_right);
	len += sprintf(buffer+len, "\tbass      %ddB [-12...+12]\n",
		       dmasound.bass);
	len += sprintf(buffer+len, "\ttreble    %ddB [-12...+12]\n",
		       dmasound.treble);
	if (len >= space) {
		printk(KERN_ERR "dmasound_atari: overflowed state buffer alloc.\n") ;
		len = space ;
	}
	return len;
}

static int FalconStateInfo(char *buffer, size_t space)
{
	int len = 0;
	len += sprintf(buffer+len, "\tvol left  %ddB [-22.5 ... 0]\n",
		       dmasound.volume_left);
	len += sprintf(buffer+len, "\tvol right %ddB [-22.5 ... 0]\n",
		       dmasound.volume_right);
	if (len >= space) {
		printk(KERN_ERR "dmasound_atari: overflowed state buffer alloc.\n") ;
		len = space ;
	}
	return len;
}


/*** Machine definitions *****************************************************/

static SETTINGS def_hard_falcon = {
	.format		= AFMT_S8,
	.stereo		= 0,
	.size		= 8,
	.speed		= 8195
} ;

static SETTINGS def_hard_tt = {
	.format	= AFMT_S8,
	.stereo	= 0,
	.size	= 8,
	.speed	= 12517
} ;

static SETTINGS def_soft = {
	.format	= AFMT_U8,
	.stereo	= 0,
	.size	= 8,
	.speed	= 8000
} ;

static __initdata MACHINE machTT = {
	.name		= "Atari",
	.name2		= "TT",
	.owner		= THIS_MODULE,
	.dma_alloc	= AtaAlloc,
	.dma_free	= AtaFree,
	.irqinit	= AtaIrqInit,
#ifdef MODULE
	.irqcleanup	= AtaIrqCleanUp,
#endif /* MODULE */
	.init		= TTInit,
	.silence	= TTSilence,
	.setFormat	= TTSetFormat,
	.setVolume	= TTSetVolume,
	.setBass	= AtaSetBass,
	.setTreble	= AtaSetTreble,
	.setGain	= TTSetGain,
	.play		= AtaPlay,
	.mixer_init	= TTMixerInit,
	.mixer_ioctl	= TTMixerIoctl,
	.write_sq_setup	= AtaWriteSqSetup,
	.sq_open	= AtaSqOpen,
	.state_info	= TTStateInfo,
	.min_dsp_speed	= 6258,
	.version	= ((DMASOUND_ATARI_REVISION<<8) | DMASOUND_ATARI_EDITION),
	.hardware_afmts	= AFMT_S8,  /* h'ware-supported formats *only* here */
	.capabilities	=  DSP_CAP_BATCH	/* As per SNDCTL_DSP_GETCAPS */
};

static __initdata MACHINE machFalcon = {
	.name		= "Atari",
	.name2		= "FALCON",
	.dma_alloc	= AtaAlloc,
	.dma_free	= AtaFree,
	.irqinit	= AtaIrqInit,
#ifdef MODULE
	.irqcleanup	= AtaIrqCleanUp,
#endif /* MODULE */
	.init		= FalconInit,
	.silence	= FalconSilence,
	.setFormat	= FalconSetFormat,
	.setVolume	= FalconSetVolume,
	.setBass	= AtaSetBass,
	.setTreble	= AtaSetTreble,
	.play		= AtaPlay,
	.mixer_init	= FalconMixerInit,
	.mixer_ioctl	= FalconMixerIoctl,
	.write_sq_setup	= AtaWriteSqSetup,
	.sq_open	= AtaSqOpen,
	.state_info	= FalconStateInfo,
	.min_dsp_speed	= 8195,
	.version	= ((DMASOUND_ATARI_REVISION<<8) | DMASOUND_ATARI_EDITION),
	.hardware_afmts	= (AFMT_S8 | AFMT_S16_BE), /* h'ware-supported formats *only* here */
	.capabilities	=  DSP_CAP_BATCH	/* As per SNDCTL_DSP_GETCAPS */
};


/*** Config & Setup **********************************************************/


static int __init dmasound_atari_init(void)
{
	if (MACH_IS_ATARI && ATARIHW_PRESENT(PCM_8BIT)) {
	    if (ATARIHW_PRESENT(CODEC)) {
		dmasound.mach = machFalcon;
		dmasound.mach.default_soft = def_soft ;
		dmasound.mach.default_hard = def_hard_falcon ;
		is_falcon = 1;
	    } else if (ATARIHW_PRESENT(MICROWIRE)) {
		dmasound.mach = machTT;
		dmasound.mach.default_soft = def_soft ;
		dmasound.mach.default_hard = def_hard_tt ;
		is_falcon = 0;
	    } else
		return -ENODEV;
	    if ((st_mfp.int_en_a & st_mfp.int_mk_a & 0x20) == 0)
		return dmasound_init();
	    else {
		printk("DMA sound driver: Timer A interrupt already in use\n");
		return -EBUSY;
	    }
	}
	return -ENODEV;
}

static void __exit dmasound_atari_cleanup(void)
{
	dmasound_deinit();
}

module_init(dmasound_atari_init);
module_exit(dmasound_atari_cleanup);
MODULE_LICENSE("GPL");
