/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef AMSTREAM_H
#define AMSTREAM_H

//#include <linux/interrupt.h>
#include "ve.h"

#ifdef __KERNEL__
#define PORT_FLAG_IN_USE    0x0001
#define PORT_FLAG_VFORMAT   0x0002
#define PORT_FLAG_AFORMAT   0x0004
#define PORT_FLAG_FORMAT    (PORT_FLAG_VFORMAT | PORT_FLAG_AFORMAT)
#define PORT_FLAG_VID       0x0008
#define PORT_FLAG_AID       0x0010
#define PORT_FLAG_SID       0x0020
#define PORT_FLAG_UD       0x0040
#define PORT_FLAG_ID        (PORT_FLAG_VID | PORT_FLAG_AID | PORT_FLAG_SID | PORT_FLAG_UD)
#define PORT_FLAG_INITED    0x100

#define PORT_TYPE_VIDEO     0x01
#define PORT_TYPE_AUDIO     0x02
#define PORT_TYPE_MPTS      0x04
#define PORT_TYPE_MPPS      0x08
#define PORT_TYPE_ES        0x10
#define PORT_TYPE_RM        0x20
#define PORT_TYPE_SUB       0x40
#define PORT_TYPE_SUB_RD    0x80
#define PORT_TYPE_USERDATA	0x200
#endif

#define AMSTREAM_IOC_MAGIC  'S'

#define AMSTREAM_IOC_VB_START   _IOW(AMSTREAM_IOC_MAGIC, 0x00, int)
#define AMSTREAM_IOC_VB_SIZE    _IOW(AMSTREAM_IOC_MAGIC, 0x01, int)
#define AMSTREAM_IOC_AB_START   _IOW(AMSTREAM_IOC_MAGIC, 0x02, int)
#define AMSTREAM_IOC_AB_SIZE    _IOW(AMSTREAM_IOC_MAGIC, 0x03, int)
#define AMSTREAM_IOC_VFORMAT    _IOW(AMSTREAM_IOC_MAGIC, 0x04, int)
#define AMSTREAM_IOC_AFORMAT    _IOW(AMSTREAM_IOC_MAGIC, 0x05, int)
#define AMSTREAM_IOC_VID        _IOW(AMSTREAM_IOC_MAGIC, 0x06, int)
#define AMSTREAM_IOC_AID        _IOW(AMSTREAM_IOC_MAGIC, 0x07, int)
#define AMSTREAM_IOC_VB_STATUS  _IOR(AMSTREAM_IOC_MAGIC, 0x08, unsigned long)
#define AMSTREAM_IOC_AB_STATUS  _IOR(AMSTREAM_IOC_MAGIC, 0x09, unsigned long)
#define AMSTREAM_IOC_SYSINFO    _IOW(AMSTREAM_IOC_MAGIC, 0x0a, int)
#define AMSTREAM_IOC_ACHANNEL   _IOW(AMSTREAM_IOC_MAGIC, 0x0b, int)
#define AMSTREAM_IOC_SAMPLERATE _IOW(AMSTREAM_IOC_MAGIC, 0x0c, int)
#define AMSTREAM_IOC_DATAWIDTH  _IOW(AMSTREAM_IOC_MAGIC, 0x0d, int)
#define AMSTREAM_IOC_TSTAMP     _IOW(AMSTREAM_IOC_MAGIC, 0x0e, unsigned long)
#define AMSTREAM_IOC_VDECSTAT   _IOR(AMSTREAM_IOC_MAGIC, 0x0f, unsigned long)
#define AMSTREAM_IOC_ADECSTAT   _IOR(AMSTREAM_IOC_MAGIC, 0x10, unsigned long)

#define AMSTREAM_IOC_PORT_INIT   _IO(AMSTREAM_IOC_MAGIC, 0x11)
#define AMSTREAM_IOC_TRICKMODE   _IOW(AMSTREAM_IOC_MAGIC, 0x12, unsigned long)

#define AMSTREAM_IOC_AUDIO_INFO	 _IOW(AMSTREAM_IOC_MAGIC, 0x13, unsigned long)
#define AMSTREAM_IOC_TRICK_STAT  _IOR(AMSTREAM_IOC_MAGIC, 0x14, unsigned long)
#define AMSTREAM_IOC_AUDIO_RESET _IO(AMSTREAM_IOC_MAGIC, 0x15)
#define AMSTREAM_IOC_SID         _IOW(AMSTREAM_IOC_MAGIC, 0x16, int)
#define AMSTREAM_IOC_VPAUSE      _IOW(AMSTREAM_IOC_MAGIC, 0x17, int)
#define AMSTREAM_IOC_AVTHRESH    _IOW(AMSTREAM_IOC_MAGIC, 0x18, int)
#define AMSTREAM_IOC_SYNCTHRESH  _IOW(AMSTREAM_IOC_MAGIC, 0x19, int)
#define AMSTREAM_IOC_SUB_RESET   _IOW(AMSTREAM_IOC_MAGIC, 0x1a, int)
#define AMSTREAM_IOC_SUB_LENGTH  _IOR(AMSTREAM_IOC_MAGIC, 0x1b, unsigned long)
#define AMSTREAM_IOC_SET_DEC_RESET _IOW(AMSTREAM_IOC_MAGIC, 0x1c, int)
#define AMSTREAM_IOC_TS_SKIPBYTE _IOW(AMSTREAM_IOC_MAGIC, 0x1d, int)
#define AMSTREAM_IOC_SUB_TYPE    _IOW(AMSTREAM_IOC_MAGIC, 0x1e, int)
#define AMSTREAM_IOC_CLEAR_VIDEO _IOW(AMSTREAM_IOC_MAGIC, 0x1f, int)

#define AMSTREAM_IOC_APTS               _IOR(AMSTREAM_IOC_MAGIC, 0x40, unsigned long)
#define AMSTREAM_IOC_VPTS               _IOR(AMSTREAM_IOC_MAGIC, 0x41, unsigned long)
#define AMSTREAM_IOC_PCRSCR             _IOR(AMSTREAM_IOC_MAGIC, 0x42, unsigned long)
#define AMSTREAM_IOC_SYNCENABLE         _IOW(AMSTREAM_IOC_MAGIC, 0x43, unsigned long)
#define AMSTREAM_IOC_GET_SYNC_ADISCON   _IOR(AMSTREAM_IOC_MAGIC, 0x44, unsigned long)
#define AMSTREAM_IOC_SET_SYNC_ADISCON   _IOW(AMSTREAM_IOC_MAGIC, 0x45, unsigned long)
#define AMSTREAM_IOC_GET_SYNC_VDISCON   _IOR(AMSTREAM_IOC_MAGIC, 0x46, unsigned long)
#define AMSTREAM_IOC_SET_SYNC_VDISCON   _IOW(AMSTREAM_IOC_MAGIC, 0x47, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_DISABLE  _IOR(AMSTREAM_IOC_MAGIC, 0x48, unsigned long)
#define AMSTREAM_IOC_SET_VIDEO_DISABLE  _IOW(AMSTREAM_IOC_MAGIC, 0x49, unsigned long)
#define AMSTREAM_IOC_SET_PCRSCR         _IOW(AMSTREAM_IOC_MAGIC, 0x4a, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_AXIS     _IOR(AMSTREAM_IOC_MAGIC, 0x4b, unsigned long)
#define AMSTREAM_IOC_SET_VIDEO_AXIS     _IOW(AMSTREAM_IOC_MAGIC, 0x4c, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_CROP     _IOR(AMSTREAM_IOC_MAGIC, 0x4d, unsigned long)
#define AMSTREAM_IOC_SET_VIDEO_CROP     _IOW(AMSTREAM_IOC_MAGIC, 0x4e, unsigned long)

// VPP.VE IOCTL command list
#define AMSTREAM_IOC_VE_BEXT     _IOW(AMSTREAM_IOC_MAGIC, 0x20, struct ve_bext_s  )
#define AMSTREAM_IOC_VE_DNLP     _IOW(AMSTREAM_IOC_MAGIC, 0x21, struct ve_dnlp_s  )
#define AMSTREAM_IOC_VE_HSVS     _IOW(AMSTREAM_IOC_MAGIC, 0x22, struct ve_hsvs_s  )
#define AMSTREAM_IOC_VE_CCOR     _IOW(AMSTREAM_IOC_MAGIC, 0x23, struct ve_ccor_s  )
#define AMSTREAM_IOC_VE_BENH     _IOW(AMSTREAM_IOC_MAGIC, 0x24, struct ve_benh_s  )
#define AMSTREAM_IOC_VE_DEMO     _IOW(AMSTREAM_IOC_MAGIC, 0x25, struct ve_demo_s  )
#define AMSTREAM_IOC_VE_VDO_MEAS _IOW(AMSTREAM_IOC_MAGIC, 0x27, struct vdo_meas_s )
#define AMSTREAM_IOC_VE_DEBUG    _IOWR(AMSTREAM_IOC_MAGIC, 0x28, unsigned long long)
#define AMSTREAM_IOC_VE_REGMAP   _IOW(AMSTREAM_IOC_MAGIC, 0x29, struct ve_regmap_s)

// VPP.CM IOCTL command list
#define AMSTREAM_IOC_CM_REGION  _IOW(AMSTREAM_IOC_MAGIC, 0x30, struct cm_region_s)
#define AMSTREAM_IOC_CM_TOP     _IOW(AMSTREAM_IOC_MAGIC, 0x31, struct cm_top_s   )
#define AMSTREAM_IOC_CM_DEMO    _IOW(AMSTREAM_IOC_MAGIC, 0x32, struct cm_demo_s  )
#define AMSTREAM_IOC_CM_DEBUG   _IOWR(AMSTREAM_IOC_MAGIC, 0x33, unsigned long long)
#define AMSTREAM_IOC_CM_REGMAP  _IOW(AMSTREAM_IOC_MAGIC, 0x34, struct cm_regmap_s)

//VPP.3D IOCTL command list
#define  AMSTREAM_IOC_SET_3D_TYPE  _IOW(AMSTREAM_IOC_MAGIC, 0x3c, unsigned int)
#define  AMSTREAM_IOC_GET_3D_TYPE  _IOW(AMSTREAM_IOC_MAGIC, 0x3d, unsigned int)

#define AMSTREAM_IOC_SUB_NUM	_IOR(AMSTREAM_IOC_MAGIC, 0x50, unsigned long)
#define AMSTREAM_IOC_SUB_INFO	_IOR(AMSTREAM_IOC_MAGIC, 0x51, unsigned long)
#define AMSTREAM_IOC_GET_BLACKOUT_POLICY   _IOR(AMSTREAM_IOC_MAGIC, 0x52, unsigned long)
#define AMSTREAM_IOC_SET_BLACKOUT_POLICY   _IOW(AMSTREAM_IOC_MAGIC, 0x53, unsigned long)
#define AMSTREAM_IOC_GET_SCREEN_MODE _IOR(AMSTREAM_IOC_MAGIC, 0x58, int)
#define AMSTREAM_IOC_SET_SCREEN_MODE _IOW(AMSTREAM_IOC_MAGIC, 0x59, int)
#define AMSTREAM_IOC_GET_VIDEO_DISCONTINUE_REPORT _IOR(AMSTREAM_IOC_MAGIC, 0x5a, int)
#define AMSTREAM_IOC_SET_VIDEO_DISCONTINUE_REPORT _IOW(AMSTREAM_IOC_MAGIC, 0x5b, int)
#define AMSTREAM_IOC_VF_STATUS  _IOR(AMSTREAM_IOC_MAGIC, 0x60, unsigned long)
#define AMSTREAM_IOC_CLEAR_VBUF _IO(AMSTREAM_IOC_MAGIC, 0x80)

#define AMSTREAM_IOC_APTS_LOOKUP    _IOR(AMSTREAM_IOC_MAGIC, 0x81, unsigned long)
#define GET_FIRST_APTS_FLAG         _IOR(AMSTREAM_IOC_MAGIC, 0x82, long)

#define AMSTREAM_IOC_GET_SYNC_ADISCON_DIFF  _IOR(AMSTREAM_IOC_MAGIC, 0x83, unsigned long)
#define AMSTREAM_IOC_GET_SYNC_VDISCON_DIFF  _IOR(AMSTREAM_IOC_MAGIC, 0x84, unsigned long)
#define AMSTREAM_IOC_SET_SYNC_ADISCON_DIFF  _IOW(AMSTREAM_IOC_MAGIC, 0x85, unsigned long)
#define AMSTREAM_IOC_SET_SYNC_VDISCON_DIFF  _IOW(AMSTREAM_IOC_MAGIC, 0x86, unsigned long)
#define AMSTREAM_IOC_GET_FREERUN_MODE  _IOR(AMSTREAM_IOC_MAGIC, 0x87, unsigned long)
#define AMSTREAM_IOC_SET_FREERUN_MODE  _IOW(AMSTREAM_IOC_MAGIC, 0x88, unsigned long)
#define AMSTREAM_IOC_SET_DEMUX         _IOW(AMSTREAM_IOC_MAGIC, 0x90, unsigned long)

#define AMSTREAM_IOC_SET_VIDEO_DELAY_LIMIT_MS _IOW(AMSTREAM_IOC_MAGIC, 0xa0, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_DELAY_LIMIT_MS _IOR(AMSTREAM_IOC_MAGIC, 0xa1, unsigned long)
#define AMSTREAM_IOC_SET_AUDIO_DELAY_LIMIT_MS _IOW(AMSTREAM_IOC_MAGIC, 0xa2, unsigned long)
#define AMSTREAM_IOC_GET_AUDIO_DELAY_LIMIT_MS _IOR(AMSTREAM_IOC_MAGIC, 0xa3, unsigned long)
#define AMSTREAM_IOC_GET_AUDIO_CUR_DELAY_MS _IOR(AMSTREAM_IOC_MAGIC, 0xa4, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_CUR_DELAY_MS _IOR(AMSTREAM_IOC_MAGIC, 0xa5, unsigned long)
#define AMSTREAM_IOC_GET_AUDIO_AVG_BITRATE_BPS _IOR(AMSTREAM_IOC_MAGIC, 0xa6, unsigned long)
#define AMSTREAM_IOC_GET_VIDEO_AVG_BITRATE_BPS _IOR(AMSTREAM_IOC_MAGIC, 0xa7, unsigned long)

#define TRICKMODE_NONE       0x00
#define TRICKMODE_I          0x01
#define TRICKMODE_FFFB       0x02

#define TRICK_STAT_DONE      0x01
#define TRICK_STAT_WAIT      0x00

#define AUDIO_EXTRA_DATA_SIZE   (4096)
#define MAX_SUB_NUM		32

enum VIDEO_DEC_TYPE
{
        VIDEO_DEC_FORMAT_UNKNOW,
        VIDEO_DEC_FORMAT_MPEG4_3,
        VIDEO_DEC_FORMAT_MPEG4_4,
        VIDEO_DEC_FORMAT_MPEG4_5,
        VIDEO_DEC_FORMAT_H264,
        VIDEO_DEC_FORMAT_MJPEG,
        VIDEO_DEC_FORMAT_MP4,
        VIDEO_DEC_FORMAT_H263,
        VIDEO_DEC_FORMAT_REAL_8,
        VIDEO_DEC_FORMAT_REAL_9,
        VIDEO_DEC_FORMAT_WMV3,
        VIDEO_DEC_FORMAT_WVC1,
        VIDEO_DEC_FORMAT_SW,
        VIDEO_DEC_FORMAT_MAX
};

struct buf_status {
        int size;
        int data_len;
        int free_len;
        unsigned int read_pointer;
        unsigned int write_pointer;
};


struct vdec_status {
        unsigned int width;
        unsigned int height;
        unsigned int fps;
        unsigned int error_count;
        unsigned int status;
};

struct adec_status {
        unsigned int channels;
        unsigned int sample_rate;
        unsigned int resolution;
        unsigned int error_count;
        unsigned int status;
};

struct am_io_param {
        union {
                int data;
                int id;//get bufstatus? //or others
        };

        int len; //buffer size;

        union {
                char buf[1];
                struct buf_status status;
                struct vdec_status vstatus;
                struct adec_status astatus;
        };
};
struct audio_info {
        int valid;
        int sample_rate;
        int channels;
        int bitrate;
        int codec_id;
        int block_align;
        int extradata_size;
        char extradata[AUDIO_EXTRA_DATA_SIZE];
};

struct dec_sysinfo {
        unsigned int    format;
        unsigned int    width;
        unsigned int    height;
        unsigned int    rate;
        unsigned int    extra;
        unsigned int    status;
        unsigned int    ratio;
        void *          param;
        unsigned long long    ratio64;
};

struct subtitle_info
{
        unsigned char id;
        unsigned char width;
        unsigned char height;
        unsigned char type;
};

struct codec_profile_t
{
        char *name;		// video codec short name
        char *profile;	// Attributes,seperated by commas
};
#define SUPPORT_VDEC_NUM	(8)

int vcodec_profile_register(const struct codec_profile_t *vdec_profile);
ssize_t vcodec_profile_read(char *buf);

#ifdef __KERNEL__
#ifdef ENABLE_DEMUX_DRIVER
/*TS demux operation interface*/
struct tsdemux_ops {
        int (*reset)(void);
        int (*set_reset_flag)(void);
        int (*request_irq)(irq_handler_t handler, void *data);
        int (*free_irq)(void);
        int (*set_vid)(int vpid);
        int (*set_aid)(int apid);
        int (*set_sid)(int spid);
        int (*set_skipbyte)(int skipbyte);
        int (*set_demux)(int dev);
};

void tsdemux_set_ops(struct tsdemux_ops *ops);
int  tsdemux_set_reset_flag(void);

#endif /*ENABLE_DEMUX_DRIVER*/
void set_vdec_func(int (*vdec_func)(struct vdec_status *));
void set_adec_func(int (*adec_func)(struct adec_status *));
void set_trickmode_func(int (*trickmode_func)(unsigned long trickmode));
void wakeup_sub_poll(void);
int  wakeup_userdata_poll(int wp, int start_phyaddr, int buf_size);
int  get_sub_type(void);
#endif

typedef struct tcon_gamma_table_s {
        u16 data[256];
} tcon_gamma_table_t;

typedef struct tcon_rgb_ogo_s {
        unsigned int en;
        int r_pre_offset;  // s11.0, range -1024~+1023, default is 0
        int g_pre_offset;  // s11.0, range -1024~+1023, default is 0
        int b_pre_offset;  // s11.0, range -1024~+1023, default is 0
        unsigned int r_gain;        // u1.10, range 0~2047, default is 1024 (1.0x)
        unsigned int g_gain;        // u1.10, range 0~2047, default is 1024 (1.0x)
        unsigned int b_gain;        // u1.10, range 0~2047, default is 1024 (1.0x)
        int r_post_offset; // s11.0, range -1024~+1023, default is 0
        int g_post_offset; // s11.0, range -1024~+1023, default is 0
        int b_post_offset; // s11.0, range -1024~+1023, default is 0
} tcon_rgb_ogo_t;

#endif /* AMSTREAM_H */

