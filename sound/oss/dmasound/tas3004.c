/*
 * Driver for the i2c/i2s based TA3004 sound chip used
 * on some Apple hardware. Also known as "snapper".
 *
 * Tobias Sargeant <tobias.sargeant@bigpond.com>
 * Based upon tas3001c.c by Christopher C. Chimelis <chris@debian.org>:
 *
 * Input support by Renzo Davoli <renzo@cs.unibo.it>
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/soundcard.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/prom.h>

#include "dmasound.h"
#include "tas_common.h"
#include "tas3004.h"

#include "tas_ioctl.h"

/* #define DEBUG_DRCE */

#define TAS3004_BIQUAD_FILTER_COUNT  7
#define TAS3004_BIQUAD_CHANNEL_COUNT 2

#define VOL_DEFAULT	(100 * 4 / 5)
#define INPUT_DEFAULT	(100 * 4 / 5)
#define BASS_DEFAULT	(100 / 2)
#define TREBLE_DEFAULT	(100 / 2)

struct tas3004_data_t {
	struct tas_data_t super;
	int device_id;
	int output_id;
	int speaker_id;
	struct tas_drce_t drce_state;
};

#define MAKE_TIME(sec,usec) (((sec)<<12) + (50000+(usec/10)*(1<<12))/100000)

#define MAKE_RATIO(i,f) (((i)<<8) + ((500+(f)*(1<<8))/1000))


static const union tas_biquad_t tas3004_eq_unity = {
	.buf		 = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 },
};


static const struct tas_drce_t tas3004_drce_min = {
	.enable		= 1,
	.above		= { .val = MAKE_RATIO(16,0), .expand = 0 },
	.below		= { .val = MAKE_RATIO(2,0), .expand = 0 },
	.threshold	= -0x59a0,
	.energy		= MAKE_TIME(0,  1700),
	.attack		= MAKE_TIME(0,  1700),
	.decay		= MAKE_TIME(0,  1700),
};


static const struct tas_drce_t tas3004_drce_max = {
	.enable		= 1,
	.above		= { .val = MAKE_RATIO(1,500), .expand = 1 },
	.below		= { .val = MAKE_RATIO(2,0), .expand = 1 },
	.threshold	= -0x0,
	.energy		= MAKE_TIME(2,400000),
	.attack		= MAKE_TIME(2,400000),
	.decay		= MAKE_TIME(2,400000),
};


static const unsigned short time_constants[]={
	MAKE_TIME(0,  1700),
	MAKE_TIME(0,  3500),
	MAKE_TIME(0,  6700),
	MAKE_TIME(0, 13000),
	MAKE_TIME(0, 26000),
	MAKE_TIME(0, 53000),
	MAKE_TIME(0,106000),
	MAKE_TIME(0,212000),
	MAKE_TIME(0,425000),
	MAKE_TIME(0,850000),
	MAKE_TIME(1,700000),
	MAKE_TIME(2,400000),
};

static const unsigned short above_threshold_compression_ratio[]={
	MAKE_RATIO( 1, 70),
	MAKE_RATIO( 1,140),
	MAKE_RATIO( 1,230),
	MAKE_RATIO( 1,330),
	MAKE_RATIO( 1,450),
	MAKE_RATIO( 1,600),
	MAKE_RATIO( 1,780),
	MAKE_RATIO( 2,  0),
	MAKE_RATIO( 2,290),
	MAKE_RATIO( 2,670),
	MAKE_RATIO( 3,200),
	MAKE_RATIO( 4,  0),
	MAKE_RATIO( 5,330),
	MAKE_RATIO( 8,  0),
	MAKE_RATIO(16,  0),
};

static const unsigned short above_threshold_expansion_ratio[]={
	MAKE_RATIO(1, 60),
	MAKE_RATIO(1,130),
	MAKE_RATIO(1,190),
	MAKE_RATIO(1,250),
	MAKE_RATIO(1,310),
	MAKE_RATIO(1,380),
	MAKE_RATIO(1,440),
	MAKE_RATIO(1,500)
};

static const unsigned short below_threshold_compression_ratio[]={
	MAKE_RATIO(1, 70),
	MAKE_RATIO(1,140),
	MAKE_RATIO(1,230),
	MAKE_RATIO(1,330),
	MAKE_RATIO(1,450),
	MAKE_RATIO(1,600),
	MAKE_RATIO(1,780),
	MAKE_RATIO(2,  0)
};

static const unsigned short below_threshold_expansion_ratio[]={
	MAKE_RATIO(1, 60),
	MAKE_RATIO(1,130),
	MAKE_RATIO(1,190),
	MAKE_RATIO(1,250),
	MAKE_RATIO(1,310),
	MAKE_RATIO(1,380),
	MAKE_RATIO(1,440),
	MAKE_RATIO(1,500),
	MAKE_RATIO(1,560),
	MAKE_RATIO(1,630),
	MAKE_RATIO(1,690),
	MAKE_RATIO(1,750),
	MAKE_RATIO(1,810),
	MAKE_RATIO(1,880),
	MAKE_RATIO(1,940),
	MAKE_RATIO(2,  0)
};

static inline int
search(	unsigned short val,
	const unsigned short *arr,
	const int arrsize) {
	/*
	 * This could be a binary search, but for small tables,
	 * a linear search is likely to be faster
	 */

	int i;

	for (i=0; i < arrsize; i++)
		if (arr[i] >= val)
			goto _1;
	return arrsize-1;
 _1:
	if (i == 0)
		return 0;
	return (arr[i]-val < val-arr[i-1]) ? i : i-1;
}

#define SEARCH(a, b) search(a, b, ARRAY_SIZE(b))

static inline int
time_index(unsigned short time)
{
	return SEARCH(time, time_constants);
}


static inline int
above_threshold_compression_index(unsigned short ratio)
{
	return SEARCH(ratio, above_threshold_compression_ratio);
}


static inline int
above_threshold_expansion_index(unsigned short ratio)
{
	return SEARCH(ratio, above_threshold_expansion_ratio);
}


static inline int
below_threshold_compression_index(unsigned short ratio)
{
	return SEARCH(ratio, below_threshold_compression_ratio);
}


static inline int
below_threshold_expansion_index(unsigned short ratio)
{
	return SEARCH(ratio, below_threshold_expansion_ratio);
}

static inline unsigned char db_to_regval(short db) {
	int r=0;

	r=(db+0x59a0) / 0x60;

	if (r < 0x91) return 0x91;
	if (r > 0xef) return 0xef;
	return r;
}

static inline short quantize_db(short db)
{
	return db_to_regval(db) * 0x60 - 0x59a0;
}

static inline int
register_width(enum tas3004_reg_t r)
{
	switch(r) {
	case TAS3004_REG_MCR:
 	case TAS3004_REG_TREBLE:
	case TAS3004_REG_BASS:
	case TAS3004_REG_ANALOG_CTRL:
	case TAS3004_REG_TEST1:
	case TAS3004_REG_TEST2:
	case TAS3004_REG_MCR2:
		return 1;

	case TAS3004_REG_LEFT_LOUD_BIQUAD_GAIN:
	case TAS3004_REG_RIGHT_LOUD_BIQUAD_GAIN:
		return 3;

	case TAS3004_REG_DRC:
	case TAS3004_REG_VOLUME:
		return 6;

	case TAS3004_REG_LEFT_MIXER:
	case TAS3004_REG_RIGHT_MIXER:
		return 9;

	case TAS3004_REG_TEST:
		return 10;

	case TAS3004_REG_LEFT_BIQUAD0:
	case TAS3004_REG_LEFT_BIQUAD1:
	case TAS3004_REG_LEFT_BIQUAD2:
	case TAS3004_REG_LEFT_BIQUAD3:
	case TAS3004_REG_LEFT_BIQUAD4:
	case TAS3004_REG_LEFT_BIQUAD5:
	case TAS3004_REG_LEFT_BIQUAD6:

	case TAS3004_REG_RIGHT_BIQUAD0:
	case TAS3004_REG_RIGHT_BIQUAD1:
	case TAS3004_REG_RIGHT_BIQUAD2:
	case TAS3004_REG_RIGHT_BIQUAD3:
	case TAS3004_REG_RIGHT_BIQUAD4:
	case TAS3004_REG_RIGHT_BIQUAD5:
	case TAS3004_REG_RIGHT_BIQUAD6:

	case TAS3004_REG_LEFT_LOUD_BIQUAD:
	case TAS3004_REG_RIGHT_LOUD_BIQUAD:
		return 15;

	default:
		return 0;
	}
}

static int
tas3004_write_register(	struct tas3004_data_t *self,
			enum tas3004_reg_t reg_num,
			char *data,
			uint write_mode)
{
	if (reg_num==TAS3004_REG_MCR ||
	    reg_num==TAS3004_REG_BASS ||
	    reg_num==TAS3004_REG_TREBLE ||
	    reg_num==TAS3004_REG_ANALOG_CTRL) {
		return tas_write_byte_register(&self->super,
					       (uint)reg_num,
					       *data,
					       write_mode);
	} else {
		return tas_write_register(&self->super,
					  (uint)reg_num,
					  register_width(reg_num),
					  data,
					  write_mode);
	}
}

static int
tas3004_sync_register(	struct tas3004_data_t *self,
			enum tas3004_reg_t reg_num)
{
	if (reg_num==TAS3004_REG_MCR ||
	    reg_num==TAS3004_REG_BASS ||
	    reg_num==TAS3004_REG_TREBLE ||
	    reg_num==TAS3004_REG_ANALOG_CTRL) {
		return tas_sync_byte_register(&self->super,
					      (uint)reg_num,
					      register_width(reg_num));
	} else {
		return tas_sync_register(&self->super,
					 (uint)reg_num,
					 register_width(reg_num));
	}
}

static int
tas3004_read_register(	struct tas3004_data_t *self,
			enum tas3004_reg_t reg_num,
			char *data,
			uint write_mode)
{
	return tas_read_register(&self->super,
				 (uint)reg_num,
				 register_width(reg_num),
				 data);
}

static inline int
tas3004_fast_load(struct tas3004_data_t *self, int fast)
{
	if (fast)
		self->super.shadow[TAS3004_REG_MCR][0] |= 0x80;
	else
		self->super.shadow[TAS3004_REG_MCR][0] &= 0x7f;
	return tas3004_sync_register(self,TAS3004_REG_MCR);
}

static uint
tas3004_supported_mixers(struct tas3004_data_t *self)
{
	return SOUND_MASK_VOLUME |
		SOUND_MASK_PCM |
		SOUND_MASK_ALTPCM |
		SOUND_MASK_IMIX |
		SOUND_MASK_TREBLE |
		SOUND_MASK_BASS |
		SOUND_MASK_MIC |
		SOUND_MASK_LINE;
}

static int
tas3004_mixer_is_stereo(struct tas3004_data_t *self, int mixer)
{
	switch(mixer) {
	case SOUND_MIXER_VOLUME:
	case SOUND_MIXER_PCM:
	case SOUND_MIXER_ALTPCM:
	case SOUND_MIXER_IMIX:
		return 1;
	default:
		return 0;
	}
}

static uint
tas3004_stereo_mixers(struct tas3004_data_t *self)
{
	uint r = tas3004_supported_mixers(self);
	uint i;
	
	for (i=1; i<SOUND_MIXER_NRDEVICES; i++)
		if (r&(1<<i) && !tas3004_mixer_is_stereo(self,i))
			r &= ~(1<<i);
	return r;
}

static int
tas3004_get_mixer_level(struct tas3004_data_t *self, int mixer, uint *level)
{
	if (!self)
		return -1;

	*level = self->super.mixer[mixer];

	return 0;
}

static int
tas3004_set_mixer_level(struct tas3004_data_t *self, int mixer, uint level)
{
	int rc;
	tas_shadow_t *shadow;
	uint temp;
	uint offset=0;

	if (!self)
		return -1;

	shadow = self->super.shadow;

	if (!tas3004_mixer_is_stereo(self,mixer))
		level = tas_mono_to_stereo(level);
	switch(mixer) {
	case SOUND_MIXER_VOLUME:
		temp = tas3004_gain.master[level&0xff];
		SET_4_20(shadow[TAS3004_REG_VOLUME], 0, temp);
		temp = tas3004_gain.master[(level>>8)&0xff];
		SET_4_20(shadow[TAS3004_REG_VOLUME], 3, temp);
		rc = tas3004_sync_register(self,TAS3004_REG_VOLUME);
		break;
	case SOUND_MIXER_IMIX:
		offset += 3;
	case SOUND_MIXER_ALTPCM:
		offset += 3;
	case SOUND_MIXER_PCM:
		/*
		 * Don't load these in fast mode. The documentation
		 * says it can be done in either mode, but testing it
		 * shows that fast mode produces ugly clicking.
		*/
		/* tas3004_fast_load(self,1); */
		temp = tas3004_gain.mixer[level&0xff];
		SET_4_20(shadow[TAS3004_REG_LEFT_MIXER], offset, temp);
		temp = tas3004_gain.mixer[(level>>8)&0xff];
		SET_4_20(shadow[TAS3004_REG_RIGHT_MIXER], offset, temp);
		rc = tas3004_sync_register(self,TAS3004_REG_LEFT_MIXER);
		if (rc == 0)
			rc=tas3004_sync_register(self,TAS3004_REG_RIGHT_MIXER);
		/* tas3004_fast_load(self,0); */
		break;
	case SOUND_MIXER_TREBLE:
		temp = tas3004_gain.treble[level&0xff];
		shadow[TAS3004_REG_TREBLE][0]=temp&0xff;
		rc = tas3004_sync_register(self,TAS3004_REG_TREBLE);
		break;
	case SOUND_MIXER_BASS:
		temp = tas3004_gain.bass[level&0xff];
		shadow[TAS3004_REG_BASS][0]=temp&0xff;
		rc = tas3004_sync_register(self,TAS3004_REG_BASS);
		break;
	case SOUND_MIXER_MIC:
		if ((level&0xff)>0) {
			software_input_volume = SW_INPUT_VOLUME_SCALE * (level&0xff);
			if (self->super.mixer[mixer] == 0) {
				self->super.mixer[SOUND_MIXER_LINE] = 0;
				shadow[TAS3004_REG_ANALOG_CTRL][0]=0xc2;
				rc = tas3004_sync_register(self,TAS3004_REG_ANALOG_CTRL);
			} else rc=0;
		} else {
			self->super.mixer[SOUND_MIXER_LINE] = SW_INPUT_VOLUME_DEFAULT;
			software_input_volume = SW_INPUT_VOLUME_SCALE *
				(self->super.mixer[SOUND_MIXER_LINE]&0xff);
			shadow[TAS3004_REG_ANALOG_CTRL][0]=0x00;
			rc = tas3004_sync_register(self,TAS3004_REG_ANALOG_CTRL);
		}
		break;
	case SOUND_MIXER_LINE:
		if (self->super.mixer[SOUND_MIXER_MIC] == 0) {
			software_input_volume = SW_INPUT_VOLUME_SCALE * (level&0xff);
			rc=0;
		}
		break;
	default:
		rc = -1;
		break;
	}
	if (rc < 0)
		return rc;
	self->super.mixer[mixer] = level;
	
	return 0;
}

static int
tas3004_leave_sleep(struct tas3004_data_t *self)
{
	unsigned char mcr = (1<<6)+(2<<4)+(2<<2);

	if (!self)
		return -1;

	/* Make sure something answers on the i2c bus */
	if (tas3004_write_register(self, TAS3004_REG_MCR, &mcr,
	    WRITE_NORMAL | FORCE_WRITE) < 0)
		return -1;

	tas3004_fast_load(self, 1);

	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD0);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD1);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD2);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD3);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD4);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD5);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD6);

	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD0);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD1);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD2);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD3);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD4);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD5);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD6);

	tas3004_fast_load(self, 0);

	(void)tas3004_sync_register(self,TAS3004_REG_VOLUME);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_MIXER);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_MIXER);
	(void)tas3004_sync_register(self,TAS3004_REG_TREBLE);
	(void)tas3004_sync_register(self,TAS3004_REG_BASS);
	(void)tas3004_sync_register(self,TAS3004_REG_ANALOG_CTRL);

	return 0;
}

static int
tas3004_enter_sleep(struct tas3004_data_t *self)
{
	if (!self)
		return -1; 
	return 0;
}

static int
tas3004_sync_biquad(	struct tas3004_data_t *self,
			u_int channel,
			u_int filter)
{
	enum tas3004_reg_t reg;

	if (channel >= TAS3004_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3004_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3004_REG_RIGHT_BIQUAD0 : TAS3004_REG_LEFT_BIQUAD0 ) + filter;

	return tas3004_sync_register(self,reg);
}

static int
tas3004_write_biquad_shadow(	struct tas3004_data_t *self,
				u_int channel,
				u_int filter,
				const union tas_biquad_t *biquad)
{
	tas_shadow_t *shadow=self->super.shadow;
	enum tas3004_reg_t reg;

	if (channel >= TAS3004_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3004_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3004_REG_RIGHT_BIQUAD0 : TAS3004_REG_LEFT_BIQUAD0 ) + filter;

	SET_4_20(shadow[reg], 0,biquad->coeff.b0);
	SET_4_20(shadow[reg], 3,biquad->coeff.b1);
	SET_4_20(shadow[reg], 6,biquad->coeff.b2);
	SET_4_20(shadow[reg], 9,biquad->coeff.a1);
	SET_4_20(shadow[reg],12,biquad->coeff.a2);

	return 0;
}

static int
tas3004_write_biquad(	struct tas3004_data_t *self,
			u_int channel,
			u_int filter,
			const union tas_biquad_t *biquad)
{
	int rc;

	rc=tas3004_write_biquad_shadow(self, channel, filter, biquad);
	if (rc < 0) return rc;

	return tas3004_sync_biquad(self, channel, filter);
}

static int
tas3004_write_biquad_list(	struct tas3004_data_t *self,
				u_int filter_count,
				u_int flags,
				struct tas_biquad_ctrl_t *biquads)
{
	int i;
	int rc;

	if (flags & TAS_BIQUAD_FAST_LOAD) tas3004_fast_load(self,1);

	for (i=0; i<filter_count; i++) {
		rc=tas3004_write_biquad(self,
					biquads[i].channel,
					biquads[i].filter,
					&biquads[i].data);
		if (rc < 0) break;
	}

	if (flags & TAS_BIQUAD_FAST_LOAD) tas3004_fast_load(self,0);

	return rc;
}

static int
tas3004_read_biquad(	struct tas3004_data_t *self,
			u_int channel,
			u_int filter,
			union tas_biquad_t *biquad)
{
	tas_shadow_t *shadow=self->super.shadow;
	enum tas3004_reg_t reg;

	if (channel >= TAS3004_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3004_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3004_REG_RIGHT_BIQUAD0 : TAS3004_REG_LEFT_BIQUAD0 ) + filter;

	biquad->coeff.b0=GET_4_20(shadow[reg], 0);
	biquad->coeff.b1=GET_4_20(shadow[reg], 3);
	biquad->coeff.b2=GET_4_20(shadow[reg], 6);
	biquad->coeff.a1=GET_4_20(shadow[reg], 9);
	biquad->coeff.a2=GET_4_20(shadow[reg],12);
	
	return 0;	
}

static int
tas3004_eq_rw(	struct tas3004_data_t *self,
		u_int cmd,
		u_long arg)
{
	void __user *argp = (void __user *)arg;
	int rc;
	struct tas_biquad_ctrl_t biquad;

	if (copy_from_user((void *)&biquad, argp, sizeof(struct tas_biquad_ctrl_t))) {
		return -EFAULT;
	}

	if (cmd & SIOC_IN) {
		rc=tas3004_write_biquad(self, biquad.channel, biquad.filter, &biquad.data);
		if (rc != 0) return rc;
	}

	if (cmd & SIOC_OUT) {
		rc=tas3004_read_biquad(self, biquad.channel, biquad.filter, &biquad.data);
		if (rc != 0) return rc;

		if (copy_to_user(argp, &biquad, sizeof(struct tas_biquad_ctrl_t))) {
			return -EFAULT;
		}

	}
	return 0;
}

static int
tas3004_eq_list_rw(	struct tas3004_data_t *self,
			u_int cmd,
			u_long arg)
{
	int rc = 0;
	int filter_count;
	int flags;
	int i,j;
	char sync_required[TAS3004_BIQUAD_CHANNEL_COUNT][TAS3004_BIQUAD_FILTER_COUNT];
	struct tas_biquad_ctrl_t biquad;
	struct tas_biquad_ctrl_list_t __user *argp = (void __user *)arg;

	memset(sync_required,0,sizeof(sync_required));

	if (copy_from_user(&filter_count, &argp->filter_count, sizeof(int)))
		return -EFAULT;

	if (copy_from_user(&flags, &argp->flags, sizeof(int)))
		return -EFAULT;

	if (cmd & SIOC_IN) {
	}

	for (i=0; i < filter_count; i++) {
		if (copy_from_user(&biquad, &argp->biquads[i],
				   sizeof(struct tas_biquad_ctrl_t))) {
			return -EFAULT;
		}

		if (cmd & SIOC_IN) {
			sync_required[biquad.channel][biquad.filter]=1;
			rc=tas3004_write_biquad_shadow(self, biquad.channel, biquad.filter, &biquad.data);
			if (rc != 0) return rc;
		}

		if (cmd & SIOC_OUT) {
			rc=tas3004_read_biquad(self, biquad.channel, biquad.filter, &biquad.data);
			if (rc != 0) return rc;

			if (copy_to_user(&argp->biquads[i], &biquad,
					 sizeof(struct tas_biquad_ctrl_t))) {
				return -EFAULT;
			}
		}
	}

	if (cmd & SIOC_IN) {
		/*
		 * This is OK for the tas3004. For the
		 * tas3001c, going into fast load mode causes
		 * the treble and bass to be reset to 0dB, and
		 * volume controls to be muted.
		 */
		if (flags & TAS_BIQUAD_FAST_LOAD) tas3004_fast_load(self,1);
		for (i=0; i<TAS3004_BIQUAD_CHANNEL_COUNT; i++) {
			for (j=0; j<TAS3004_BIQUAD_FILTER_COUNT; j++) {
				if (sync_required[i][j]) {
					rc=tas3004_sync_biquad(self, i, j);
					if (rc < 0) goto out;
				}
			}
		}
	out:
		if (flags & TAS_BIQUAD_FAST_LOAD)
			tas3004_fast_load(self,0);
	}

	return rc;
}

static int
tas3004_update_drce(	struct tas3004_data_t *self,
			int flags,
			struct tas_drce_t *drce)
{
	tas_shadow_t *shadow;
	int i;
	shadow=self->super.shadow;

	if (flags & TAS_DRCE_ABOVE_RATIO) {
		self->drce_state.above.expand = drce->above.expand;
		if (drce->above.val == (1<<8)) {
			self->drce_state.above.val = 1<<8;
			shadow[TAS3004_REG_DRC][0] = 0x02;
					
		} else if (drce->above.expand) {
			i=above_threshold_expansion_index(drce->above.val);
			self->drce_state.above.val=above_threshold_expansion_ratio[i];
			shadow[TAS3004_REG_DRC][0] = 0x0a + (i<<3);
		} else {
			i=above_threshold_compression_index(drce->above.val);
			self->drce_state.above.val=above_threshold_compression_ratio[i];
			shadow[TAS3004_REG_DRC][0] = 0x08 + (i<<3);
		}
	}

	if (flags & TAS_DRCE_BELOW_RATIO) {
		self->drce_state.below.expand = drce->below.expand;
		if (drce->below.val == (1<<8)) {
			self->drce_state.below.val = 1<<8;
			shadow[TAS3004_REG_DRC][1] = 0x02;
					
		} else if (drce->below.expand) {
			i=below_threshold_expansion_index(drce->below.val);
			self->drce_state.below.val=below_threshold_expansion_ratio[i];
			shadow[TAS3004_REG_DRC][1] = 0x08 + (i<<3);
		} else {
			i=below_threshold_compression_index(drce->below.val);
			self->drce_state.below.val=below_threshold_compression_ratio[i];
			shadow[TAS3004_REG_DRC][1] = 0x0a + (i<<3);
		}
	}

	if (flags & TAS_DRCE_THRESHOLD) {
		self->drce_state.threshold=quantize_db(drce->threshold);
		shadow[TAS3004_REG_DRC][2] = db_to_regval(self->drce_state.threshold);
	}

	if (flags & TAS_DRCE_ENERGY) {
		i=time_index(drce->energy);
		self->drce_state.energy=time_constants[i];
		shadow[TAS3004_REG_DRC][3] = 0x40 + (i<<4);
	}

	if (flags & TAS_DRCE_ATTACK) {
		i=time_index(drce->attack);
		self->drce_state.attack=time_constants[i];
		shadow[TAS3004_REG_DRC][4] = 0x40 + (i<<4);
	}

	if (flags & TAS_DRCE_DECAY) {
		i=time_index(drce->decay);
		self->drce_state.decay=time_constants[i];
		shadow[TAS3004_REG_DRC][5] = 0x40 + (i<<4);
	}

	if (flags & TAS_DRCE_ENABLE) {
		self->drce_state.enable = drce->enable;
	}

	if (!self->drce_state.enable) {
		shadow[TAS3004_REG_DRC][0] |= 0x01;
	}

#ifdef DEBUG_DRCE
	printk("DRCE: set [ ENABLE:%x ABOVE:%x/%x BELOW:%x/%x THRESH:%x ENERGY:%x ATTACK:%x DECAY:%x\n",
	       self->drce_state.enable,
	       self->drce_state.above.expand,self->drce_state.above.val,
	       self->drce_state.below.expand,self->drce_state.below.val,
	       self->drce_state.threshold,
	       self->drce_state.energy,
	       self->drce_state.attack,
	       self->drce_state.decay);

	printk("DRCE: reg [ %02x %02x %02x %02x %02x %02x ]\n",
	       (unsigned char)shadow[TAS3004_REG_DRC][0],
	       (unsigned char)shadow[TAS3004_REG_DRC][1],
	       (unsigned char)shadow[TAS3004_REG_DRC][2],
	       (unsigned char)shadow[TAS3004_REG_DRC][3],
	       (unsigned char)shadow[TAS3004_REG_DRC][4],
	       (unsigned char)shadow[TAS3004_REG_DRC][5]);
#endif

	return tas3004_sync_register(self, TAS3004_REG_DRC);
}

static int
tas3004_drce_rw(	struct tas3004_data_t *self,
			u_int cmd,
			u_long arg)
{
	int rc;
	struct tas_drce_ctrl_t drce_ctrl;
	void __user *argp = (void __user *)arg;

	if (copy_from_user(&drce_ctrl, argp, sizeof(struct tas_drce_ctrl_t)))
		return -EFAULT;

#ifdef DEBUG_DRCE
	printk("DRCE: input [ FLAGS:%x ENABLE:%x ABOVE:%x/%x BELOW:%x/%x THRESH:%x ENERGY:%x ATTACK:%x DECAY:%x\n",
	       drce_ctrl.flags,
	       drce_ctrl.data.enable,
	       drce_ctrl.data.above.expand,drce_ctrl.data.above.val,
	       drce_ctrl.data.below.expand,drce_ctrl.data.below.val,
	       drce_ctrl.data.threshold,
	       drce_ctrl.data.energy,
	       drce_ctrl.data.attack,
	       drce_ctrl.data.decay);
#endif

	if (cmd & SIOC_IN) {
		rc = tas3004_update_drce(self, drce_ctrl.flags, &drce_ctrl.data);
		if (rc < 0) return rc;
	}

	if (cmd & SIOC_OUT) {
		if (drce_ctrl.flags & TAS_DRCE_ENABLE)
			drce_ctrl.data.enable = self->drce_state.enable;
		if (drce_ctrl.flags & TAS_DRCE_ABOVE_RATIO)
			drce_ctrl.data.above = self->drce_state.above;
		if (drce_ctrl.flags & TAS_DRCE_BELOW_RATIO)
			drce_ctrl.data.below = self->drce_state.below;
		if (drce_ctrl.flags & TAS_DRCE_THRESHOLD)
			drce_ctrl.data.threshold = self->drce_state.threshold;
		if (drce_ctrl.flags & TAS_DRCE_ENERGY)
			drce_ctrl.data.energy = self->drce_state.energy;
		if (drce_ctrl.flags & TAS_DRCE_ATTACK)
			drce_ctrl.data.attack = self->drce_state.attack;
		if (drce_ctrl.flags & TAS_DRCE_DECAY)
			drce_ctrl.data.decay = self->drce_state.decay;

		if (copy_to_user(argp, &drce_ctrl,
				 sizeof(struct tas_drce_ctrl_t))) {
			return -EFAULT;
		}
	}

	return 0;
}

static void
tas3004_update_device_parameters(struct tas3004_data_t *self)
{
	char data;
	int i;

	if (!self) return;

	if (self->output_id == TAS_OUTPUT_HEADPHONES) {
		/* turn on allPass when headphones are plugged in */
		data = 0x02;
	} else {
		data = 0x00;
	}

	tas3004_write_register(self, TAS3004_REG_MCR2, &data, WRITE_NORMAL | FORCE_WRITE);

	for (i=0; tas3004_eq_prefs[i]; i++) {
		struct tas_eq_pref_t *eq = tas3004_eq_prefs[i];

		if (eq->device_id == self->device_id &&
		    (eq->output_id == 0 || eq->output_id == self->output_id) &&
		    (eq->speaker_id == 0 || eq->speaker_id == self->speaker_id)) {

			tas3004_update_drce(self, TAS_DRCE_ALL, eq->drce);
			tas3004_write_biquad_list(self, eq->filter_count, TAS_BIQUAD_FAST_LOAD, eq->biquads);

			break;
		}
	}
}

static void
tas3004_device_change_handler(void *self)
{
	if (!self) return;

	tas3004_update_device_parameters((struct tas3004_data_t *)self);
}

static struct work_struct device_change;

static int
tas3004_output_device_change(	struct tas3004_data_t *self,
				int device_id,
				int output_id,
				int speaker_id)
{
	self->device_id=device_id;
	self->output_id=output_id;
	self->speaker_id=speaker_id;

	schedule_work(&device_change);

	return 0;
}

static int
tas3004_device_ioctl(	struct tas3004_data_t *self,
			u_int cmd,
			u_long arg)
{
	uint __user *argp = (void __user *)arg;
	switch (cmd) {
	case TAS_READ_EQ:
	case TAS_WRITE_EQ:
		return tas3004_eq_rw(self, cmd, arg);

	case TAS_READ_EQ_LIST:
	case TAS_WRITE_EQ_LIST:
		return tas3004_eq_list_rw(self, cmd, arg);

	case TAS_READ_EQ_FILTER_COUNT:
		put_user(TAS3004_BIQUAD_FILTER_COUNT, argp);
		return 0;

	case TAS_READ_EQ_CHANNEL_COUNT:
		put_user(TAS3004_BIQUAD_CHANNEL_COUNT, argp);
		return 0;

	case TAS_READ_DRCE:
	case TAS_WRITE_DRCE:
		return tas3004_drce_rw(self, cmd, arg);

	case TAS_READ_DRCE_CAPS:
		put_user(TAS_DRCE_ENABLE         |
			 TAS_DRCE_ABOVE_RATIO    |
			 TAS_DRCE_BELOW_RATIO    |
			 TAS_DRCE_THRESHOLD      |
			 TAS_DRCE_ENERGY         |
			 TAS_DRCE_ATTACK         |
			 TAS_DRCE_DECAY,
			 argp);
		return 0;

	case TAS_READ_DRCE_MIN:
	case TAS_READ_DRCE_MAX: {
		struct tas_drce_ctrl_t drce_ctrl;
		const struct tas_drce_t *drce_copy;

		if (copy_from_user(&drce_ctrl, argp,
				   sizeof(struct tas_drce_ctrl_t))) {
			return -EFAULT;
		}

		if (cmd == TAS_READ_DRCE_MIN) {
			drce_copy=&tas3004_drce_min;
		} else {
			drce_copy=&tas3004_drce_max;
		}

		if (drce_ctrl.flags & TAS_DRCE_ABOVE_RATIO) {
			drce_ctrl.data.above=drce_copy->above;
		}
		if (drce_ctrl.flags & TAS_DRCE_BELOW_RATIO) {
			drce_ctrl.data.below=drce_copy->below;
		}
		if (drce_ctrl.flags & TAS_DRCE_THRESHOLD) {
			drce_ctrl.data.threshold=drce_copy->threshold;
		}
		if (drce_ctrl.flags & TAS_DRCE_ENERGY) {
			drce_ctrl.data.energy=drce_copy->energy;
		}
		if (drce_ctrl.flags & TAS_DRCE_ATTACK) {
			drce_ctrl.data.attack=drce_copy->attack;
		}
		if (drce_ctrl.flags & TAS_DRCE_DECAY) {
			drce_ctrl.data.decay=drce_copy->decay;
		}

		if (copy_to_user(argp, &drce_ctrl,
				 sizeof(struct tas_drce_ctrl_t))) {
			return -EFAULT;
		}
	}
	}

	return -EINVAL;
}

static int
tas3004_init_mixer(struct tas3004_data_t *self)
{
	unsigned char mcr = (1<<6)+(2<<4)+(2<<2);

	/* Make sure something answers on the i2c bus */
	if (tas3004_write_register(self, TAS3004_REG_MCR, &mcr,
	    WRITE_NORMAL | FORCE_WRITE) < 0)
		return -1;

	tas3004_fast_load(self, 1);

	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD0);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD1);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD2);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD3);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD4);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD5);
	(void)tas3004_sync_register(self,TAS3004_REG_RIGHT_BIQUAD6);

	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD0);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD1);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD2);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD3);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD4);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD5);
	(void)tas3004_sync_register(self,TAS3004_REG_LEFT_BIQUAD6);

	tas3004_sync_register(self, TAS3004_REG_DRC);

	tas3004_sync_register(self, TAS3004_REG_MCR2);

	tas3004_fast_load(self, 0);

	tas3004_set_mixer_level(self, SOUND_MIXER_VOLUME, VOL_DEFAULT<<8 | VOL_DEFAULT);
	tas3004_set_mixer_level(self, SOUND_MIXER_PCM, INPUT_DEFAULT<<8 | INPUT_DEFAULT);
	tas3004_set_mixer_level(self, SOUND_MIXER_ALTPCM, 0);
	tas3004_set_mixer_level(self, SOUND_MIXER_IMIX, 0);

	tas3004_set_mixer_level(self, SOUND_MIXER_BASS, BASS_DEFAULT);
	tas3004_set_mixer_level(self, SOUND_MIXER_TREBLE, TREBLE_DEFAULT);

	tas3004_set_mixer_level(self, SOUND_MIXER_LINE,SW_INPUT_VOLUME_DEFAULT);

	return 0;
}

static int
tas3004_uninit_mixer(struct tas3004_data_t *self)
{
	tas3004_set_mixer_level(self, SOUND_MIXER_VOLUME, 0);
	tas3004_set_mixer_level(self, SOUND_MIXER_PCM, 0);
	tas3004_set_mixer_level(self, SOUND_MIXER_ALTPCM, 0);
	tas3004_set_mixer_level(self, SOUND_MIXER_IMIX, 0);

	tas3004_set_mixer_level(self, SOUND_MIXER_BASS, 0);
	tas3004_set_mixer_level(self, SOUND_MIXER_TREBLE, 0);

	tas3004_set_mixer_level(self, SOUND_MIXER_LINE, 0);

	return 0;
}

static int
tas3004_init(struct i2c_client *client)
{
	struct tas3004_data_t *self;
	size_t sz = sizeof(*self) + (TAS3004_REG_MAX*sizeof(tas_shadow_t));
	char drce_init[] = { 0x69, 0x22, 0x9f, 0xb0, 0x60, 0xa0 };
	char mcr2 = 0;
	int i, j;

	self = kmalloc(sz, GFP_KERNEL);
	if (!self)
		return -ENOMEM;
	memset(self, 0, sz);

	self->super.client = client;
	self->super.shadow = (tas_shadow_t *)(self+1);
	self->output_id = TAS_OUTPUT_HEADPHONES;

	dev_set_drvdata(&client->dev, self);

	for (i = 0; i < TAS3004_BIQUAD_CHANNEL_COUNT; i++)
		for (j = 0; j<TAS3004_BIQUAD_FILTER_COUNT; j++)
			tas3004_write_biquad_shadow(self, i, j,
					&tas3004_eq_unity);

	tas3004_write_register(self, TAS3004_REG_MCR2, &mcr2, WRITE_SHADOW);
	tas3004_write_register(self, TAS3004_REG_DRC, drce_init, WRITE_SHADOW);

	INIT_WORK(&device_change, tas3004_device_change_handler, self);
	return 0;
}

static void 
tas3004_uninit(struct tas3004_data_t *self)
{
	tas3004_uninit_mixer(self);
	kfree(self);
}


struct tas_driver_hooks_t tas3004_hooks = {
	.init			= (tas_hook_init_t)tas3004_init,
	.post_init		= (tas_hook_post_init_t)tas3004_init_mixer,
	.uninit			= (tas_hook_uninit_t)tas3004_uninit,
	.get_mixer_level	= (tas_hook_get_mixer_level_t)tas3004_get_mixer_level,
	.set_mixer_level	= (tas_hook_set_mixer_level_t)tas3004_set_mixer_level,
	.enter_sleep		= (tas_hook_enter_sleep_t)tas3004_enter_sleep,
	.leave_sleep		= (tas_hook_leave_sleep_t)tas3004_leave_sleep,
	.supported_mixers	= (tas_hook_supported_mixers_t)tas3004_supported_mixers,
	.mixer_is_stereo	= (tas_hook_mixer_is_stereo_t)tas3004_mixer_is_stereo,
	.stereo_mixers		= (tas_hook_stereo_mixers_t)tas3004_stereo_mixers,
	.output_device_change	= (tas_hook_output_device_change_t)tas3004_output_device_change,
	.device_ioctl		= (tas_hook_device_ioctl_t)tas3004_device_ioctl
};
