#ifndef _TAS_COMMON_H_
#define _TAS_COMMON_H_

#include <linux/i2c.h>
#include <linux/soundcard.h>
#include <asm/string.h>

#define I2C_DRIVERID_TAS_BASE   (0xFEBA)

#define SET_4_20(shadow, offset, val)                        \
	do {                                                 \
		(shadow)[(offset)+0] = ((val) >> 16) & 0xff; \
		(shadow)[(offset)+1] = ((val) >> 8)  & 0xff; \
		(shadow)[(offset)+2] = ((val) >> 0)  & 0xff; \
	} while (0)

#define GET_4_20(shadow, offset)                             \
	(((u_int)((shadow)[(offset)+0]) << 16) |             \
	 ((u_int)((shadow)[(offset)+1]) <<  8) |             \
	 ((u_int)((shadow)[(offset)+2]) <<  0))


#define TAS_BIQUAD_FAST_LOAD 0x01

#define TAS_DRCE_ENABLE           0x01
#define TAS_DRCE_ABOVE_RATIO      0x02
#define TAS_DRCE_BELOW_RATIO      0x04
#define TAS_DRCE_THRESHOLD        0x08
#define TAS_DRCE_ENERGY           0x10
#define TAS_DRCE_ATTACK           0x20
#define TAS_DRCE_DECAY            0x40

#define TAS_DRCE_ALL              0x7f


#define TAS_OUTPUT_HEADPHONES     0x00
#define TAS_OUTPUT_INTERNAL_SPKR  0x01
#define TAS_OUTPUT_EXTERNAL_SPKR  0x02


union tas_biquad_t {
	struct {
		int b0,b1,b2,a1,a2;
	} coeff;
	int buf[5];
};

struct tas_biquad_ctrl_t {
	u_int channel:4;
	u_int filter:4;

	union tas_biquad_t data;
};

struct tas_biquad_ctrl_list_t {
	int flags;
	int filter_count;
	struct tas_biquad_ctrl_t biquads[0];
};

struct tas_ratio_t {
	unsigned short val;    /* 8.8                        */
	unsigned short expand; /* 0 = compress, !0 = expand. */
};

struct tas_drce_t {
	unsigned short enable;
	struct tas_ratio_t above;
	struct tas_ratio_t below;
	short threshold;       /* dB,       8.8 signed    */
	unsigned short energy; /* seconds,  4.12 unsigned */
	unsigned short attack; /* seconds,  4.12 unsigned */
	unsigned short decay;  /* seconds,  4.12 unsigned */
};

struct tas_drce_ctrl_t {
	uint flags;

	struct tas_drce_t data;
};

struct tas_gain_t
{
  unsigned int *master;
  unsigned int *treble;
  unsigned int *bass;
  unsigned int *mixer;
};

typedef char tas_shadow_t[0x45];

struct tas_data_t
{
	struct i2c_client *client;
	tas_shadow_t *shadow;
	uint mixer[SOUND_MIXER_NRDEVICES];
};

typedef int (*tas_hook_init_t)(struct i2c_client *);
typedef int (*tas_hook_post_init_t)(struct tas_data_t *);
typedef void (*tas_hook_uninit_t)(struct tas_data_t *);

typedef int (*tas_hook_get_mixer_level_t)(struct tas_data_t *,int,uint *);
typedef int (*tas_hook_set_mixer_level_t)(struct tas_data_t *,int,uint);

typedef int (*tas_hook_enter_sleep_t)(struct tas_data_t *);
typedef int (*tas_hook_leave_sleep_t)(struct tas_data_t *);

typedef int (*tas_hook_supported_mixers_t)(struct tas_data_t *);
typedef int (*tas_hook_mixer_is_stereo_t)(struct tas_data_t *,int);
typedef int (*tas_hook_stereo_mixers_t)(struct tas_data_t *);

typedef int (*tas_hook_output_device_change_t)(struct tas_data_t *,int,int,int);
typedef int (*tas_hook_device_ioctl_t)(struct tas_data_t *,u_int,u_long);

struct tas_driver_hooks_t {
	/*
	 * All hardware initialisation must be performed in
	 * post_init(), as tas_dmasound_init() does a hardware reset.
	 *
	 * init() is called before tas_dmasound_init() so that
	 * ouput_device_change() is always called after i2c driver
	 * initialisation. The implication is that
	 * output_device_change() must cope with the fact that it
	 * may be called before post_init().
	 */

	tas_hook_init_t                   init;
	tas_hook_post_init_t              post_init;
	tas_hook_uninit_t                 uninit;

	tas_hook_get_mixer_level_t        get_mixer_level;
	tas_hook_set_mixer_level_t        set_mixer_level;

	tas_hook_enter_sleep_t            enter_sleep;
	tas_hook_leave_sleep_t            leave_sleep;

	tas_hook_supported_mixers_t       supported_mixers;
	tas_hook_mixer_is_stereo_t        mixer_is_stereo;
	tas_hook_stereo_mixers_t          stereo_mixers;

	tas_hook_output_device_change_t   output_device_change;
	tas_hook_device_ioctl_t           device_ioctl;
};

enum tas_write_mode_t {
	WRITE_HW     = 0x01,
	WRITE_SHADOW = 0x02,
	WRITE_NORMAL = 0x03,
	FORCE_WRITE  = 0x04
};

static inline uint
tas_mono_to_stereo(uint mono)
{
	mono &=0xff;
	return mono | (mono<<8);
}

/*
 * Todo: make these functions a bit more efficient !
 */
static inline int
tas_write_register(	struct tas_data_t *self,
			uint reg_num,
			uint reg_width,
			char *data,
			uint write_mode)
{
	int rc;

	if (reg_width==0 || data==NULL || self==NULL)
		return -EINVAL;
	if (!(write_mode & FORCE_WRITE) &&
	    !memcmp(data,self->shadow[reg_num],reg_width))
	    	return 0;

	if (write_mode & WRITE_SHADOW)
		memcpy(self->shadow[reg_num],data,reg_width);
	if (write_mode & WRITE_HW) {
		rc=i2c_smbus_write_i2c_block_data(self->client,
						  reg_num,
						  reg_width,
						  data);
		if (rc < 0) {
			printk("tas: I2C block write failed \n");  
			return rc; 
		}
	}
	return 0;
}

static inline int
tas_sync_register(	struct tas_data_t *self,
			uint reg_num,
			uint reg_width)
{
	int rc;

	if (reg_width==0 || self==NULL)
		return -EINVAL;
	rc=i2c_smbus_write_i2c_block_data(self->client,
					  reg_num,
					  reg_width,
					  self->shadow[reg_num]);
	if (rc < 0) {
		printk("tas: I2C block write failed \n");
		return rc;
	}
	return 0;
}

static inline int
tas_write_byte_register(	struct tas_data_t *self,
				uint reg_num,
				char data,
				uint write_mode)
{
	if (self==NULL)
		return -1;
	if (!(write_mode & FORCE_WRITE) && data != self->shadow[reg_num][0])
		return 0;
	if (write_mode & WRITE_SHADOW)
		self->shadow[reg_num][0]=data;
	if (write_mode & WRITE_HW) {
		if (i2c_smbus_write_byte_data(self->client, reg_num, data) < 0) {
			printk("tas: I2C byte write failed \n");  
			return -1; 
		}
	}
	return 0;
}

static inline int
tas_sync_byte_register(	struct tas_data_t *self,
			uint reg_num,
			uint reg_width)
{
	if (reg_width==0 || self==NULL)
		return -1;
	if (i2c_smbus_write_byte_data(
	    self->client, reg_num, self->shadow[reg_num][0]) < 0) {
		printk("tas: I2C byte write failed \n");
		return -1;
	}
	return 0;
}

static inline int
tas_read_register(	struct tas_data_t *self,
			uint reg_num,
			uint reg_width,
			char *data)
{
	if (reg_width==0 || data==NULL || self==NULL)
		return -1;
	memcpy(data,self->shadow[reg_num],reg_width);
	return 0;
}

extern int tas_register_driver(struct tas_driver_hooks_t *hooks);

extern int tas_get_mixer_level(int mixer,uint *level);
extern int tas_set_mixer_level(int mixer,uint level);
extern int tas_enter_sleep(void);
extern int tas_leave_sleep(void);
extern int tas_supported_mixers(void);
extern int tas_mixer_is_stereo(int mixer);
extern int tas_stereo_mixers(void);
extern int tas_output_device_change(int,int,int);
extern int tas_device_ioctl(u_int, u_long);

extern void tas_cleanup(void);
extern int tas_init(int driver_id,const char *driver_name);
extern int tas_post_init(void);

#endif /* _TAS_COMMON_H_ */
/*
 * Local Variables:
 * tab-width: 8
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * End:
 */
