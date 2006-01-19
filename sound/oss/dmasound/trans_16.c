/*
 *  linux/sound/oss/dmasound/trans_16.c
 *
 *  16 bit translation routines.  Only used by Power mac at present.
 *
 *  See linux/sound/oss/dmasound/dmasound_core.c for copyright and
 *  history prior to 08/02/2001.
 *
 *  08/02/2001 Iain Sandoe
 *		split from dmasound_awacs.c
 *  11/29/2003 Renzo Davoli (King Enzo)
 *  	- input resampling (for soft rate < hard rate)
 *  	- software line in gain control
 */

#include <linux/soundcard.h>
#include <asm/uaccess.h>
#include "dmasound.h"

extern int expand_bal;	/* Balance factor for expanding (not volume!) */
static short dmasound_alaw2dma16[] ;
static short dmasound_ulaw2dma16[] ;

static ssize_t pmac_ct_law(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_s8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t pmac_ct_u8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t pmac_ct_s16(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_u16(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);

static ssize_t pmac_ctx_law(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t pmac_ctx_s8(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ctx_u8(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ctx_s16(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t pmac_ctx_u16(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);

static ssize_t pmac_ct_s16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t pmac_ct_u16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);

/*** Translations ************************************************************/

static int expand_data;	/* Data for expanding */

static ssize_t pmac_ct_law(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	short *table = dmasound.soft.format == AFMT_MU_LAW
		? dmasound_ulaw2dma16 : dmasound_alaw2dma16;
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = table[data];
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = table[data];
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_s8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = data << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = data << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_u8(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = (data ^ 0x80) << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = (data ^ 0x80) << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_s16(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min_t(unsigned long, userCount, frameLeft);
	if (!stereo) {
		short __user *up = (short __user *) userPtr;
		while (count > 0) {
			short data;
			if (get_user(data, up++))
				return -EFAULT;
			*fp++ = data;
			*fp++ = data;
			count--;
		}
	} else {
		if (copy_from_user(fp, userPtr, count * 4))
			return -EFAULT;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t pmac_ct_u16(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short __user *up = (short __user *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		short data;
		if (get_user(data, up++))
			return -EFAULT;
		data ^= mask;
		*fp++ = data;
		if (stereo) {
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
		}
		*fp++ = data;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}


static ssize_t pmac_ctx_law(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned short *table = (unsigned short *)
		(dmasound.soft.format == AFMT_MU_LAW
		 ? dmasound_ulaw2dma16 : dmasound_alaw2dma16);
	unsigned int data = expand_data;
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;
	int stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = table[c];
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + table[c];
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}

static ssize_t pmac_ctx_s8(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = c << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + (c << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_u8(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = (c ^ 0x80) << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + ((c ^ 0x80) << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_s16(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short __user *up = (unsigned short __user *) userPtr;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + c;
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}


static ssize_t pmac_ctx_u16(const u_char __user *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short __user *up = (unsigned short __user *) userPtr;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int stereo = dmasound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + (c ^ mask);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}

/* data in routines... */

static ssize_t pmac_ct_s8_read(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		val = (val * software_input_volume) >> 7;
		data = val >> 8;
		if (put_user(data, (u_char __user *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			val = (val * software_input_volume) >> 7;
			data = val >> 8;
			if (put_user(data, (u_char __user *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t pmac_ct_u8_read(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = dmasound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		val = (val * software_input_volume) >> 7;
		data = (val >> 8) ^ 0x80;
		if (put_user(data, (u_char __user *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			val = (val * software_input_volume) >> 7;
			data = (val >> 8) ^ 0x80;
			if (put_user(data, (u_char __user *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}

static ssize_t pmac_ct_s16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short __user *up = (short __user *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		short data;

		data = *fp++;
		data = (data * software_input_volume) >> 7;
		if (put_user(data, up++))
			return -EFAULT;
		if (stereo) {
			data = *fp;
			data = (data * software_input_volume) >> 7;
			if (put_user(data, up++))
				return -EFAULT;
		}
		fp++;
		count--;
 	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t pmac_ct_u16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = dmasound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short __user *up = (short __user *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min_t(unsigned long, userCount, frameLeft);
	while (count > 0) {
		int data;

		data = *fp++;
		data = (data * software_input_volume) >> 7;
		data ^= mask;
		if (put_user(data, up++))
			return -EFAULT;
		if (stereo) {
			data = *fp;
			data = (data * software_input_volume) >> 7;
			data ^= mask;
			if (put_user(data, up++))
				return -EFAULT;
		}
		fp++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

/* data in routines (reducing speed)... */

static ssize_t pmac_ctx_s8_read(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	short *p = (short *) &frame[*frameUsed];
	int bal = expand_read_bal;
	int vall,valr, stereo = dmasound.soft.stereo;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char data;

		if (bal<0 && userCount == 0)
			break;
		vall = *p++;
		vall = (vall * software_input_volume) >> 7;
		if (stereo) {
			valr = *p;
			valr = (valr * software_input_volume) >> 7;
		}
		p++;
		if (bal < 0) {
			data = vall >> 8;
			if (put_user(data, (u_char __user *)userPtr++))
				return -EFAULT;
			if (stereo) {
				data = valr >> 8;
				if (put_user(data, (u_char __user *)userPtr++))
					return -EFAULT;
			}
			userCount--;
			bal += hSpeed;
		}
		frameLeft--;
		bal -= sSpeed;
	}
	expand_read_bal=bal;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t pmac_ctx_u8_read(const u_char __user *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	short *p = (short *) &frame[*frameUsed];
	int bal = expand_read_bal;
	int vall,valr, stereo = dmasound.soft.stereo;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char data;

		if (bal<0 && userCount == 0)
			break;

		vall = *p++;
		vall = (vall * software_input_volume) >> 7;
		if (stereo) {
			valr = *p;
			valr = (valr * software_input_volume) >> 7;
		}
		p++;
		if (bal < 0) {
			data = (vall >> 8) ^ 0x80;
			if (put_user(data, (u_char __user *)userPtr++))
				return -EFAULT;
			if (stereo) {
				data = (valr >> 8) ^ 0x80;
				if (put_user(data, (u_char __user *)userPtr++))
					return -EFAULT;
			}
			userCount--;
			bal += hSpeed;
		}
		frameLeft--;
		bal -= sSpeed;
	}
	expand_read_bal=bal;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}

static ssize_t pmac_ctx_s16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	int bal = expand_read_bal;
	short *fp = (short *) &frame[*frameUsed];
	short __user *up = (short __user *) userPtr;
	int stereo = dmasound.soft.stereo;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		int datal,datar;

		if (bal<0 && userCount == 0)
			break;

		datal = *fp++;
		datal = (datal * software_input_volume) >> 7;
		if (stereo) {
			datar = *fp;
			datar = (datar * software_input_volume) >> 7;
		}
		fp++;
		if (bal < 0) {
			if (put_user(datal, up++))
				return -EFAULT;
			if (stereo) {
				if (put_user(datar, up++))
					return -EFAULT;
			}
			userCount--;
			bal += hSpeed;
		}
		frameLeft--;
		bal -= sSpeed;
	}
	expand_read_bal=bal;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}

static ssize_t pmac_ctx_u16_read(const u_char __user *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	int bal = expand_read_bal;
	int mask = (dmasound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	short *fp = (short *) &frame[*frameUsed];
	short __user *up = (short __user *) userPtr;
	int stereo = dmasound.soft.stereo;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		int datal,datar;

		if (bal<0 && userCount == 0)
			break;

		datal = *fp++;
		datal = (datal * software_input_volume) >> 7;
		datal ^= mask;
		if (stereo) {
			datar = *fp;
			datar = (datar * software_input_volume) >> 7;
			datar ^= mask;
		}
		fp++;
		if (bal < 0) {
			if (put_user(datal, up++))
				return -EFAULT;
			if (stereo) {
				if (put_user(datar, up++))
					return -EFAULT;
			}
			userCount--;
			bal += hSpeed;
		}
		frameLeft--;
		bal -= sSpeed;
	}
	expand_read_bal=bal;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}


TRANS transAwacsNormal = {
	.ct_ulaw=	pmac_ct_law,
	.ct_alaw=	pmac_ct_law,
	.ct_s8=		pmac_ct_s8,
	.ct_u8=		pmac_ct_u8,
	.ct_s16be=	pmac_ct_s16,
	.ct_u16be=	pmac_ct_u16,
	.ct_s16le=	pmac_ct_s16,
	.ct_u16le=	pmac_ct_u16,
};

TRANS transAwacsExpand = {
	.ct_ulaw=	pmac_ctx_law,
	.ct_alaw=	pmac_ctx_law,
	.ct_s8=		pmac_ctx_s8,
	.ct_u8=		pmac_ctx_u8,
	.ct_s16be=	pmac_ctx_s16,
	.ct_u16be=	pmac_ctx_u16,
	.ct_s16le=	pmac_ctx_s16,
	.ct_u16le=	pmac_ctx_u16,
};

TRANS transAwacsNormalRead = {
	.ct_s8=		pmac_ct_s8_read,
	.ct_u8=		pmac_ct_u8_read,
	.ct_s16be=	pmac_ct_s16_read,
	.ct_u16be=	pmac_ct_u16_read,
	.ct_s16le=	pmac_ct_s16_read,
	.ct_u16le=	pmac_ct_u16_read,
};

TRANS transAwacsExpandRead = {
	.ct_s8=		pmac_ctx_s8_read,
	.ct_u8=		pmac_ctx_u8_read,
	.ct_s16be=	pmac_ctx_s16_read,
	.ct_u16be=	pmac_ctx_u16_read,
	.ct_s16le=	pmac_ctx_s16_read,
	.ct_u16le=	pmac_ctx_u16_read,
};

/* translation tables */
/* 16 bit mu-law */

static short dmasound_ulaw2dma16[] = {
	-32124,	-31100,	-30076,	-29052,	-28028,	-27004,	-25980,	-24956,
	-23932,	-22908,	-21884,	-20860,	-19836,	-18812,	-17788,	-16764,
	-15996,	-15484,	-14972,	-14460,	-13948,	-13436,	-12924,	-12412,
	-11900,	-11388,	-10876,	-10364,	-9852,	-9340,	-8828,	-8316,
	-7932,	-7676,	-7420,	-7164,	-6908,	-6652,	-6396,	-6140,
	-5884,	-5628,	-5372,	-5116,	-4860,	-4604,	-4348,	-4092,
	-3900,	-3772,	-3644,	-3516,	-3388,	-3260,	-3132,	-3004,
	-2876,	-2748,	-2620,	-2492,	-2364,	-2236,	-2108,	-1980,
	-1884,	-1820,	-1756,	-1692,	-1628,	-1564,	-1500,	-1436,
	-1372,	-1308,	-1244,	-1180,	-1116,	-1052,	-988,	-924,
	-876,	-844,	-812,	-780,	-748,	-716,	-684,	-652,
	-620,	-588,	-556,	-524,	-492,	-460,	-428,	-396,
	-372,	-356,	-340,	-324,	-308,	-292,	-276,	-260,
	-244,	-228,	-212,	-196,	-180,	-164,	-148,	-132,
	-120,	-112,	-104,	-96,	-88,	-80,	-72,	-64,
	-56,	-48,	-40,	-32,	-24,	-16,	-8,	0,
	32124,	31100,	30076,	29052,	28028,	27004,	25980,	24956,
	23932,	22908,	21884,	20860,	19836,	18812,	17788,	16764,
	15996,	15484,	14972,	14460,	13948,	13436,	12924,	12412,
	11900,	11388,	10876,	10364,	9852,	9340,	8828,	8316,
	7932,	7676,	7420,	7164,	6908,	6652,	6396,	6140,
	5884,	5628,	5372,	5116,	4860,	4604,	4348,	4092,
	3900,	3772,	3644,	3516,	3388,	3260,	3132,	3004,
	2876,	2748,	2620,	2492,	2364,	2236,	2108,	1980,
	1884,	1820,	1756,	1692,	1628,	1564,	1500,	1436,
	1372,	1308,	1244,	1180,	1116,	1052,	988,	924,
	876,	844,	812,	780,	748,	716,	684,	652,
	620,	588,	556,	524,	492,	460,	428,	396,
	372,	356,	340,	324,	308,	292,	276,	260,
	244,	228,	212,	196,	180,	164,	148,	132,
	120,	112,	104,	96,	88,	80,	72,	64,
	56,	48,	40,	32,	24,	16,	8,	0,
};

/* 16 bit A-law */

static short dmasound_alaw2dma16[] = {
	-5504,	-5248,	-6016,	-5760,	-4480,	-4224,	-4992,	-4736,
	-7552,	-7296,	-8064,	-7808,	-6528,	-6272,	-7040,	-6784,
	-2752,	-2624,	-3008,	-2880,	-2240,	-2112,	-2496,	-2368,
	-3776,	-3648,	-4032,	-3904,	-3264,	-3136,	-3520,	-3392,
	-22016,	-20992,	-24064,	-23040,	-17920,	-16896,	-19968,	-18944,
	-30208,	-29184,	-32256,	-31232,	-26112,	-25088,	-28160,	-27136,
	-11008,	-10496,	-12032,	-11520,	-8960,	-8448,	-9984,	-9472,
	-15104,	-14592,	-16128,	-15616,	-13056,	-12544,	-14080,	-13568,
	-344,	-328,	-376,	-360,	-280,	-264,	-312,	-296,
	-472,	-456,	-504,	-488,	-408,	-392,	-440,	-424,
	-88,	-72,	-120,	-104,	-24,	-8,	-56,	-40,
	-216,	-200,	-248,	-232,	-152,	-136,	-184,	-168,
	-1376,	-1312,	-1504,	-1440,	-1120,	-1056,	-1248,	-1184,
	-1888,	-1824,	-2016,	-1952,	-1632,	-1568,	-1760,	-1696,
	-688,	-656,	-752,	-720,	-560,	-528,	-624,	-592,
	-944,	-912,	-1008,	-976,	-816,	-784,	-880,	-848,
	5504,	5248,	6016,	5760,	4480,	4224,	4992,	4736,
	7552,	7296,	8064,	7808,	6528,	6272,	7040,	6784,
	2752,	2624,	3008,	2880,	2240,	2112,	2496,	2368,
	3776,	3648,	4032,	3904,	3264,	3136,	3520,	3392,
	22016,	20992,	24064,	23040,	17920,	16896,	19968,	18944,
	30208,	29184,	32256,	31232,	26112,	25088,	28160,	27136,
	11008,	10496,	12032,	11520,	8960,	8448,	9984,	9472,
	15104,	14592,	16128,	15616,	13056,	12544,	14080,	13568,
	344,	328,	376,	360,	280,	264,	312,	296,
	472,	456,	504,	488,	408,	392,	440,	424,
	88,	72,	120,	104,	24,	8,	56,	40,
	216,	200,	248,	232,	152,	136,	184,	168,
	1376,	1312,	1504,	1440,	1120,	1056,	1248,	1184,
	1888,	1824,	2016,	1952,	1632,	1568,	1760,	1696,
	688,	656,	752,	720,	560,	528,	624,	592,
	944,	912,	1008,	976,	816,	784,	880,	848,
};
