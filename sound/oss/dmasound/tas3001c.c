/*
 * Driver for the i2c/i2s based TA3004 sound chip used
 * on some Apple hardware. Also known as "snapper".
 *
 * Tobias Sargeant <tobias.sargeant@bigpond.com>
 * Based upon, tas3001c.c by Christopher C. Chimelis <chris@debian.org>:
 *
 *   TODO:
 *   -----
 *   * Enable control over input line 2 (is this connected?)
 *   * Implement sleep support (at least mute everything and
 *   * set gains to minimum during sleep)
 *   * Look into some of Darwin's tweaks regarding the mute
 *   * lines (delays & different behaviour on some HW)
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
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/prom.h>

#include "dmasound.h"
#include "tas_common.h"
#include "tas3001c.h"

#include "tas_ioctl.h"

#define TAS3001C_BIQUAD_FILTER_COUNT  6
#define TAS3001C_BIQUAD_CHANNEL_COUNT 2

#define VOL_DEFAULT	(100 * 4 / 5)
#define INPUT_DEFAULT	(100 * 4 / 5)
#define BASS_DEFAULT	(100 / 2)
#define TREBLE_DEFAULT	(100 / 2)

struct tas3001c_data_t {
	struct tas_data_t super;
	int device_id;
	int output_id;
	int speaker_id;
	struct tas_drce_t drce_state;
	struct work_struct change;
};


static const union tas_biquad_t
tas3001c_eq_unity={
	.buf = { 0x100000, 0x000000, 0x000000, 0x000000, 0x000000 }
};


static inline unsigned char db_to_regval(short db) {
	int r=0;

	r=(db+0x59a0) / 0x60;

	if (r < 0x91) return 0x91;
	if (r > 0xef) return 0xef;
	return r;
}

static inline short quantize_db(short db) {
	return db_to_regval(db) * 0x60 - 0x59a0;
}


static inline int
register_width(enum tas3001c_reg_t r)
{
	switch(r) {
	case TAS3001C_REG_MCR:
 	case TAS3001C_REG_TREBLE:
	case TAS3001C_REG_BASS:
		return 1;

	case TAS3001C_REG_DRC:
		return 2;

	case TAS3001C_REG_MIXER1:
	case TAS3001C_REG_MIXER2:
		return 3;

	case TAS3001C_REG_VOLUME:
		return 6;

	case TAS3001C_REG_LEFT_BIQUAD0:
	case TAS3001C_REG_LEFT_BIQUAD1:
	case TAS3001C_REG_LEFT_BIQUAD2:
	case TAS3001C_REG_LEFT_BIQUAD3:
	case TAS3001C_REG_LEFT_BIQUAD4:
	case TAS3001C_REG_LEFT_BIQUAD5:
	case TAS3001C_REG_LEFT_BIQUAD6:

	case TAS3001C_REG_RIGHT_BIQUAD0:
	case TAS3001C_REG_RIGHT_BIQUAD1:
	case TAS3001C_REG_RIGHT_BIQUAD2:
	case TAS3001C_REG_RIGHT_BIQUAD3:
	case TAS3001C_REG_RIGHT_BIQUAD4:
	case TAS3001C_REG_RIGHT_BIQUAD5:
	case TAS3001C_REG_RIGHT_BIQUAD6:
		return 15;

	default:
		return 0;
	}
}

static int
tas3001c_write_register(	struct tas3001c_data_t *self,
				enum tas3001c_reg_t reg_num,
				char *data,
				uint write_mode)
{
	if (reg_num==TAS3001C_REG_MCR ||
	    reg_num==TAS3001C_REG_BASS ||
	    reg_num==TAS3001C_REG_TREBLE) {
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
tas3001c_sync_register(	struct tas3001c_data_t *self,
			enum tas3001c_reg_t reg_num)
{
	if (reg_num==TAS3001C_REG_MCR ||
	    reg_num==TAS3001C_REG_BASS ||
	    reg_num==TAS3001C_REG_TREBLE) {
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
tas3001c_read_register(	struct tas3001c_data_t *self,
			enum tas3001c_reg_t reg_num,
			char *data,
			uint write_mode)
{
	return tas_read_register(&self->super,
				 (uint)reg_num,
				 register_width(reg_num),
				 data);
}

static inline int
tas3001c_fast_load(struct tas3001c_data_t *self, int fast)
{
	if (fast)
		self->super.shadow[TAS3001C_REG_MCR][0] |= 0x80;
	else
		self->super.shadow[TAS3001C_REG_MCR][0] &= 0x7f;
	return tas3001c_sync_register(self,TAS3001C_REG_MCR);
}

static uint
tas3001c_supported_mixers(struct tas3001c_data_t *self)
{
	return SOUND_MASK_VOLUME |
		SOUND_MASK_PCM |
		SOUND_MASK_ALTPCM |
		SOUND_MASK_TREBLE |
		SOUND_MASK_BASS;
}

static int
tas3001c_mixer_is_stereo(struct tas3001c_data_t *self,int mixer)
{
	switch(mixer) {
	case SOUND_MIXER_VOLUME:
		return 1;
	default:
		return 0;
	}
}

static uint
tas3001c_stereo_mixers(struct tas3001c_data_t *self)
{
	uint r=tas3001c_supported_mixers(self);
	uint i;
	
	for (i=1; i<SOUND_MIXER_NRDEVICES; i++)
		if (r&(1<<i) && !tas3001c_mixer_is_stereo(self,i))
			r &= ~(1<<i);
	return r;
}

static int
tas3001c_get_mixer_level(struct tas3001c_data_t *self,int mixer,uint *level)
{
	if (!self)
		return -1;
		
	*level=self->super.mixer[mixer];
	
	return 0;
}

static int
tas3001c_set_mixer_level(struct tas3001c_data_t *self,int mixer,uint level)
{
	int rc;
	tas_shadow_t *shadow;

	uint temp;
	uint offset=0;

	if (!self)
		return -1;
		
	shadow=self->super.shadow;

	if (!tas3001c_mixer_is_stereo(self,mixer))
		level = tas_mono_to_stereo(level);

	switch(mixer) {
	case SOUND_MIXER_VOLUME:
		temp = tas3001c_gain.master[level&0xff];
		shadow[TAS3001C_REG_VOLUME][0] = (temp >> 16) & 0xff;
		shadow[TAS3001C_REG_VOLUME][1] = (temp >> 8)  & 0xff;
		shadow[TAS3001C_REG_VOLUME][2] = (temp >> 0)  & 0xff;
		temp = tas3001c_gain.master[(level>>8)&0xff];
		shadow[TAS3001C_REG_VOLUME][3] = (temp >> 16) & 0xff;
		shadow[TAS3001C_REG_VOLUME][4] = (temp >> 8)  & 0xff;
		shadow[TAS3001C_REG_VOLUME][5] = (temp >> 0)  & 0xff;
		rc = tas3001c_sync_register(self,TAS3001C_REG_VOLUME);
		break;
	case SOUND_MIXER_ALTPCM:
		/* tas3001c_fast_load(self, 1); */
		level = tas_mono_to_stereo(level);
		temp = tas3001c_gain.mixer[level&0xff];
		shadow[TAS3001C_REG_MIXER2][offset+0] = (temp >> 16) & 0xff;
		shadow[TAS3001C_REG_MIXER2][offset+1] = (temp >> 8)  & 0xff;
		shadow[TAS3001C_REG_MIXER2][offset+2] = (temp >> 0)  & 0xff;
		rc = tas3001c_sync_register(self,TAS3001C_REG_MIXER2);
		/* tas3001c_fast_load(self, 0); */
		break;
	case SOUND_MIXER_PCM:
		/* tas3001c_fast_load(self, 1); */
		level = tas_mono_to_stereo(level);
		temp = tas3001c_gain.mixer[level&0xff];
		shadow[TAS3001C_REG_MIXER1][offset+0] = (temp >> 16) & 0xff;
		shadow[TAS3001C_REG_MIXER1][offset+1] = (temp >> 8)  & 0xff;
		shadow[TAS3001C_REG_MIXER1][offset+2] = (temp >> 0)  & 0xff;
		rc = tas3001c_sync_register(self,TAS3001C_REG_MIXER1);
		/* tas3001c_fast_load(self, 0); */
		break;
	case SOUND_MIXER_TREBLE:
		temp = tas3001c_gain.treble[level&0xff];
		shadow[TAS3001C_REG_TREBLE][0]=temp&0xff;
		rc = tas3001c_sync_register(self,TAS3001C_REG_TREBLE);
		break;
	case SOUND_MIXER_BASS:
		temp = tas3001c_gain.bass[level&0xff];
		shadow[TAS3001C_REG_BASS][0]=temp&0xff;
		rc = tas3001c_sync_register(self,TAS3001C_REG_BASS);
		break;
	default:
		rc = -1;
		break;
	}
	if (rc < 0)
		return rc;
	self->super.mixer[mixer]=level;
	return 0;
}

static int
tas3001c_leave_sleep(struct tas3001c_data_t *self)
{
	unsigned char mcr = (1<<6)+(2<<4)+(2<<2);

	if (!self)
		return -1;

	/* Make sure something answers on the i2c bus */
	if (tas3001c_write_register(self, TAS3001C_REG_MCR, &mcr,
	    WRITE_NORMAL|FORCE_WRITE) < 0)
	    	return -1;

	tas3001c_fast_load(self, 1);

	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD0);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD1);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD2);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD3);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD4);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD5);

	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD0);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD1);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD2);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD3);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD4);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD5);

	tas3001c_fast_load(self, 0);

	(void)tas3001c_sync_register(self,TAS3001C_REG_BASS);
	(void)tas3001c_sync_register(self,TAS3001C_REG_TREBLE);
	(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER1);
	(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER2);
	(void)tas3001c_sync_register(self,TAS3001C_REG_VOLUME);

	return 0;
}

static int
tas3001c_enter_sleep(struct tas3001c_data_t *self)
{
	/* Stub for now, but I have the details on low-power mode */
	if (!self)
		return -1; 
	return 0;
}

static int
tas3001c_sync_biquad(	struct tas3001c_data_t *self,
			u_int channel,
			u_int filter)
{
	enum tas3001c_reg_t reg;

	if (channel >= TAS3001C_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3001C_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3001C_REG_RIGHT_BIQUAD0 : TAS3001C_REG_LEFT_BIQUAD0 ) + filter;

	return tas3001c_sync_register(self,reg);
}

static int
tas3001c_write_biquad_shadow(	struct tas3001c_data_t *self,
				u_int channel,
				u_int filter,
				const union tas_biquad_t *biquad)
{
	tas_shadow_t *shadow=self->super.shadow;
	enum tas3001c_reg_t reg;

	if (channel >= TAS3001C_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3001C_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3001C_REG_RIGHT_BIQUAD0 : TAS3001C_REG_LEFT_BIQUAD0 ) + filter;

	SET_4_20(shadow[reg], 0,biquad->coeff.b0);
	SET_4_20(shadow[reg], 3,biquad->coeff.b1);
	SET_4_20(shadow[reg], 6,biquad->coeff.b2);
	SET_4_20(shadow[reg], 9,biquad->coeff.a1);
	SET_4_20(shadow[reg],12,biquad->coeff.a2);

	return 0;
}

static int
tas3001c_write_biquad(	struct tas3001c_data_t *self,
			u_int channel,
			u_int filter,
			const union tas_biquad_t *biquad)
{
	int rc;

	rc=tas3001c_write_biquad_shadow(self, channel, filter, biquad);
	if (rc < 0) return rc;

	return tas3001c_sync_biquad(self, channel, filter);
}

static int
tas3001c_write_biquad_list(	struct tas3001c_data_t *self,
				u_int filter_count,
				u_int flags,
				struct tas_biquad_ctrl_t *biquads)
{
	int i;
	int rc;

	if (flags & TAS_BIQUAD_FAST_LOAD) tas3001c_fast_load(self,1);

	for (i=0; i<filter_count; i++) {
		rc=tas3001c_write_biquad(self,
					 biquads[i].channel,
					 biquads[i].filter,
					 &biquads[i].data);
		if (rc < 0) break;
	}

	if (flags & TAS_BIQUAD_FAST_LOAD) {
		tas3001c_fast_load(self,0);

		(void)tas3001c_sync_register(self,TAS3001C_REG_BASS);
		(void)tas3001c_sync_register(self,TAS3001C_REG_TREBLE);
		(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER1);
		(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER2);
		(void)tas3001c_sync_register(self,TAS3001C_REG_VOLUME);
	}

	return rc;
}

static int
tas3001c_read_biquad(	struct tas3001c_data_t *self,
			u_int channel,
			u_int filter,
			union tas_biquad_t *biquad)
{
	tas_shadow_t *shadow=self->super.shadow;
	enum tas3001c_reg_t reg;

	if (channel >= TAS3001C_BIQUAD_CHANNEL_COUNT ||
	    filter  >= TAS3001C_BIQUAD_FILTER_COUNT) return -EINVAL;

	reg=( channel ? TAS3001C_REG_RIGHT_BIQUAD0 : TAS3001C_REG_LEFT_BIQUAD0 ) + filter;

	biquad->coeff.b0=GET_4_20(shadow[reg], 0);
	biquad->coeff.b1=GET_4_20(shadow[reg], 3);
	biquad->coeff.b2=GET_4_20(shadow[reg], 6);
	biquad->coeff.a1=GET_4_20(shadow[reg], 9);
	biquad->coeff.a2=GET_4_20(shadow[reg],12);
	
	return 0;	
}

static int
tas3001c_eq_rw(	struct tas3001c_data_t *self,
		u_int cmd,
		u_long arg)
{
	int rc;
	struct tas_biquad_ctrl_t biquad;
	void __user *argp = (void __user *)arg;

	if (copy_from_user(&biquad, argp, sizeof(struct tas_biquad_ctrl_t))) {
		return -EFAULT;
	}

	if (cmd & SIOC_IN) {
		rc=tas3001c_write_biquad(self, biquad.channel, biquad.filter, &biquad.data);
		if (rc != 0) return rc;
	}

	if (cmd & SIOC_OUT) {
		rc=tas3001c_read_biquad(self, biquad.channel, biquad.filter, &biquad.data);
		if (rc != 0) return rc;

		if (copy_to_user(argp, &biquad, sizeof(struct tas_biquad_ctrl_t))) {
			return -EFAULT;
		}

	}
	return 0;
}

static int
tas3001c_eq_list_rw(	struct tas3001c_data_t *self,
			u_int cmd,
			u_long arg)
{
	int rc;
	int filter_count;
	int flags;
	int i,j;
	char sync_required[2][6];
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
			rc=tas3001c_write_biquad_shadow(self, biquad.channel, biquad.filter, &biquad.data);
			if (rc != 0) return rc;
		}

		if (cmd & SIOC_OUT) {
			rc=tas3001c_read_biquad(self, biquad.channel, biquad.filter, &biquad.data);
			if (rc != 0) return rc;

			if (copy_to_user(&argp->biquads[i], &biquad,
					 sizeof(struct tas_biquad_ctrl_t))) {
				return -EFAULT;
			}
		}
	}

	if (cmd & SIOC_IN) {
		if (flags & TAS_BIQUAD_FAST_LOAD) tas3001c_fast_load(self,1);
		for (i=0; i<2; i++) {
			for (j=0; j<6; j++) {
				if (sync_required[i][j]) {
					rc=tas3001c_sync_biquad(self, i, j);
					if (rc < 0) return rc;
				}
			}
		}
		if (flags & TAS_BIQUAD_FAST_LOAD) {
			tas3001c_fast_load(self,0);
			/* now we need to set up the mixers again,
			   because leaving fast mode resets them. */
			(void)tas3001c_sync_register(self,TAS3001C_REG_BASS);
			(void)tas3001c_sync_register(self,TAS3001C_REG_TREBLE);
			(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER1);
			(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER2);
			(void)tas3001c_sync_register(self,TAS3001C_REG_VOLUME);
		}
	}

	return 0;
}

static int
tas3001c_update_drce(	struct tas3001c_data_t *self,
			int flags,
			struct tas_drce_t *drce)
{
	tas_shadow_t *shadow;
	shadow=self->super.shadow;

	shadow[TAS3001C_REG_DRC][1] = 0xc1;

	if (flags & TAS_DRCE_THRESHOLD) {
		self->drce_state.threshold=quantize_db(drce->threshold);
		shadow[TAS3001C_REG_DRC][2] = db_to_regval(self->drce_state.threshold);
	}

	if (flags & TAS_DRCE_ENABLE) {
		self->drce_state.enable = drce->enable;
	}

	if (!self->drce_state.enable) {
		shadow[TAS3001C_REG_DRC][0] = 0xf0;
	}

#ifdef DEBUG_DRCE
	printk("DRCE IOCTL: set [ ENABLE:%x THRESH:%x\n",
	       self->drce_state.enable,
	       self->drce_state.threshold);

	printk("DRCE IOCTL: reg [ %02x %02x ]\n",
	       (unsigned char)shadow[TAS3001C_REG_DRC][0],
	       (unsigned char)shadow[TAS3001C_REG_DRC][1]);
#endif

	return tas3001c_sync_register(self, TAS3001C_REG_DRC);
}

static int
tas3001c_drce_rw(	struct tas3001c_data_t *self,
			u_int cmd,
			u_long arg)
{
	int rc;
	struct tas_drce_ctrl_t drce_ctrl;
	void __user *argp = (void __user *)arg;

	if (copy_from_user(&drce_ctrl, argp, sizeof(struct tas_drce_ctrl_t)))
		return -EFAULT;

#ifdef DEBUG_DRCE
	printk("DRCE IOCTL: input [ FLAGS:%x ENABLE:%x THRESH:%x\n",
	       drce_ctrl.flags,
	       drce_ctrl.data.enable,
	       drce_ctrl.data.threshold);
#endif

	if (cmd & SIOC_IN) {
		rc = tas3001c_update_drce(self, drce_ctrl.flags, &drce_ctrl.data);
		if (rc < 0)
			return rc;
	}

	if (cmd & SIOC_OUT) {
		if (drce_ctrl.flags & TAS_DRCE_ENABLE)
			drce_ctrl.data.enable = self->drce_state.enable;

		if (drce_ctrl.flags & TAS_DRCE_THRESHOLD)
			drce_ctrl.data.threshold = self->drce_state.threshold;

		if (copy_to_user(argp, &drce_ctrl,
				 sizeof(struct tas_drce_ctrl_t))) {
			return -EFAULT;
		}
	}

	return 0;
}

static void
tas3001c_update_device_parameters(struct tas3001c_data_t *self)
{
	int i,j;

	if (!self) return;

	if (self->output_id == TAS_OUTPUT_HEADPHONES) {
		tas3001c_fast_load(self, 1);

		for (i=0; i<TAS3001C_BIQUAD_CHANNEL_COUNT; i++) {
			for (j=0; j<TAS3001C_BIQUAD_FILTER_COUNT; j++) {
				tas3001c_write_biquad(self, i, j, &tas3001c_eq_unity);
			}
		}

		tas3001c_fast_load(self, 0);

		(void)tas3001c_sync_register(self,TAS3001C_REG_BASS);
		(void)tas3001c_sync_register(self,TAS3001C_REG_TREBLE);
		(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER1);
		(void)tas3001c_sync_register(self,TAS3001C_REG_MIXER2);
		(void)tas3001c_sync_register(self,TAS3001C_REG_VOLUME);

		return;
	}

	for (i=0; tas3001c_eq_prefs[i]; i++) {
		struct tas_eq_pref_t *eq = tas3001c_eq_prefs[i];

		if (eq->device_id == self->device_id &&
		    (eq->output_id == 0 || eq->output_id == self->output_id) &&
		    (eq->speaker_id == 0 || eq->speaker_id == self->speaker_id)) {

			tas3001c_update_drce(self, TAS_DRCE_ALL, eq->drce);
			tas3001c_write_biquad_list(self, eq->filter_count, TAS_BIQUAD_FAST_LOAD, eq->biquads);

			break;
		}
	}
}

static void
tas3001c_device_change_handler(struct work_struct *work)
{
	struct tas3001c_data_t *self;
	self = container_of(work, struct tas3001c_data_t, change);
	tas3001c_update_device_parameters(self);
}

static int
tas3001c_output_device_change(	struct tas3001c_data_t *self,
				int device_id,
				int output_id,
				int speaker_id)
{
	self->device_id=device_id;
	self->output_id=output_id;
	self->speaker_id=speaker_id;

	schedule_work(&self->change);
	return 0;
}

static int
tas3001c_device_ioctl(	struct tas3001c_data_t *self,
			u_int cmd,
			u_long arg)
{
	uint __user *argp = (void __user *)arg;
	switch (cmd) {
	case TAS_READ_EQ:
	case TAS_WRITE_EQ:
		return tas3001c_eq_rw(self, cmd, arg);

	case TAS_READ_EQ_LIST:
	case TAS_WRITE_EQ_LIST:
		return tas3001c_eq_list_rw(self, cmd, arg);

	case TAS_READ_EQ_FILTER_COUNT:
		put_user(TAS3001C_BIQUAD_FILTER_COUNT, argp);
		return 0;

	case TAS_READ_EQ_CHANNEL_COUNT:
		put_user(TAS3001C_BIQUAD_CHANNEL_COUNT, argp);
		return 0;

	case TAS_READ_DRCE:
	case TAS_WRITE_DRCE:
		return tas3001c_drce_rw(self, cmd, arg);

	case TAS_READ_DRCE_CAPS:
		put_user(TAS_DRCE_ENABLE | TAS_DRCE_THRESHOLD, argp);
		return 0;

	case TAS_READ_DRCE_MIN:
	case TAS_READ_DRCE_MAX: {
		struct tas_drce_ctrl_t drce_ctrl;

		if (copy_from_user(&drce_ctrl, argp,
				   sizeof(struct tas_drce_ctrl_t))) {
			return -EFAULT;
		}

		if (drce_ctrl.flags & TAS_DRCE_THRESHOLD) {
			if (cmd == TAS_READ_DRCE_MIN) {
				drce_ctrl.data.threshold=-36<<8;
			} else {
				drce_ctrl.data.threshold=-6<<8;
			}
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
tas3001c_init_mixer(struct tas3001c_data_t *self)
{
	unsigned char mcr = (1<<6)+(2<<4)+(2<<2);

	/* Make sure something answers on the i2c bus */
	if (tas3001c_write_register(self, TAS3001C_REG_MCR, &mcr,
	    WRITE_NORMAL|FORCE_WRITE) < 0)
		return -1;

	tas3001c_fast_load(self, 1);

	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD0);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD1);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD2);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD3);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD4);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD5);
	(void)tas3001c_sync_register(self,TAS3001C_REG_RIGHT_BIQUAD6);

	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD0);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD1);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD2);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD3);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD4);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD5);
	(void)tas3001c_sync_register(self,TAS3001C_REG_LEFT_BIQUAD6);

	tas3001c_fast_load(self, 0);

	tas3001c_set_mixer_level(self, SOUND_MIXER_VOLUME, VOL_DEFAULT<<8 | VOL_DEFAULT);
	tas3001c_set_mixer_level(self, SOUND_MIXER_PCM, INPUT_DEFAULT<<8 | INPUT_DEFAULT);
	tas3001c_set_mixer_level(self, SOUND_MIXER_ALTPCM, 0);

	tas3001c_set_mixer_level(self, SOUND_MIXER_BASS, BASS_DEFAULT);
	tas3001c_set_mixer_level(self, SOUND_MIXER_TREBLE, TREBLE_DEFAULT);

	return 0;
}

static int
tas3001c_uninit_mixer(struct tas3001c_data_t *self)
{
	tas3001c_set_mixer_level(self, SOUND_MIXER_VOLUME, 0);
	tas3001c_set_mixer_level(self, SOUND_MIXER_PCM,    0);
	tas3001c_set_mixer_level(self, SOUND_MIXER_ALTPCM, 0);

	tas3001c_set_mixer_level(self, SOUND_MIXER_BASS,   0);
	tas3001c_set_mixer_level(self, SOUND_MIXER_TREBLE, 0);

	return 0;
}

static int
tas3001c_init(struct i2c_client *client)
{
	struct tas3001c_data_t *self;
	size_t sz = sizeof(*self) + (TAS3001C_REG_MAX*sizeof(tas_shadow_t));
	int i, j;

	self = kmalloc(sz, GFP_KERNEL);
	if (!self)
		return -ENOMEM;
	memset(self, 0, sz);

	self->super.client = client;
	self->super.shadow = (tas_shadow_t *)(self+1);
	self->output_id = TAS_OUTPUT_HEADPHONES;

	dev_set_drvdata(&client->dev, self);

	for (i = 0; i < TAS3001C_BIQUAD_CHANNEL_COUNT; i++)
		for (j = 0; j < TAS3001C_BIQUAD_FILTER_COUNT; j++)
			tas3001c_write_biquad_shadow(self, i, j,
				&tas3001c_eq_unity);

	INIT_WORK(&self->change, tas3001c_device_change_handler);
	return 0;
}

static void
tas3001c_uninit(struct tas3001c_data_t *self)
{
	tas3001c_uninit_mixer(self);
	kfree(self);
}

struct tas_driver_hooks_t tas3001c_hooks = {
	.init			= (tas_hook_init_t)tas3001c_init,
	.post_init		= (tas_hook_post_init_t)tas3001c_init_mixer,
	.uninit			= (tas_hook_uninit_t)tas3001c_uninit,
	.get_mixer_level	= (tas_hook_get_mixer_level_t)tas3001c_get_mixer_level,
	.set_mixer_level	= (tas_hook_set_mixer_level_t)tas3001c_set_mixer_level,
	.enter_sleep		= (tas_hook_enter_sleep_t)tas3001c_enter_sleep,
	.leave_sleep		= (tas_hook_leave_sleep_t)tas3001c_leave_sleep,
	.supported_mixers	= (tas_hook_supported_mixers_t)tas3001c_supported_mixers,
	.mixer_is_stereo	= (tas_hook_mixer_is_stereo_t)tas3001c_mixer_is_stereo,
	.stereo_mixers		= (tas_hook_stereo_mixers_t)tas3001c_stereo_mixers,
	.output_device_change	= (tas_hook_output_device_change_t)tas3001c_output_device_change,
	.device_ioctl		= (tas_hook_device_ioctl_t)tas3001c_device_ioctl
};
