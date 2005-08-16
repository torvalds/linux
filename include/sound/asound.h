/*
 *  Advanced Linux Sound Architecture - ALSA - Driver
 *  Copyright (c) 1994-2003 by Jaroslav Kysela <perex@suse.cz>,
 *                             Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __SOUND_ASOUND_H
#define __SOUND_ASOUND_H

#if defined(LINUX) || defined(__LINUX__) || defined(__linux__)

#include <linux/ioctl.h>

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/time.h>
#include <asm/byteorder.h>

#ifdef  __LITTLE_ENDIAN
#define SNDRV_LITTLE_ENDIAN
#else
#ifdef __BIG_ENDIAN
#define SNDRV_BIG_ENDIAN
#else
#error "Unsupported endian..."
#endif
#endif

#else /* !__KERNEL__ */

#include <endian.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SNDRV_LITTLE_ENDIAN
#elif __BYTE_ORDER == __BIG_ENDIAN
#define SNDRV_BIG_ENDIAN
#else
#error "Unsupported endian..."
#endif

#endif /* __KERNEL **/

#endif /* LINUX */

#ifndef __KERNEL__
#include <sys/time.h>
#include <sys/types.h>
#endif

/*
 *  protocol version
 */

#define SNDRV_PROTOCOL_VERSION(major, minor, subminor) (((major)<<16)|((minor)<<8)|(subminor))
#define SNDRV_PROTOCOL_MAJOR(version) (((version)>>16)&0xffff)
#define SNDRV_PROTOCOL_MINOR(version) (((version)>>8)&0xff)
#define SNDRV_PROTOCOL_MICRO(version) ((version)&0xff)
#define SNDRV_PROTOCOL_INCOMPATIBLE(kversion, uversion) \
	(SNDRV_PROTOCOL_MAJOR(kversion) != SNDRV_PROTOCOL_MAJOR(uversion) || \
	 (SNDRV_PROTOCOL_MAJOR(kversion) == SNDRV_PROTOCOL_MAJOR(uversion) && \
	   SNDRV_PROTOCOL_MINOR(kversion) != SNDRV_PROTOCOL_MINOR(uversion)))

/****************************************************************************
 *                                                                          *
 *        Digital audio interface					    *
 *                                                                          *
 ****************************************************************************/

struct sndrv_aes_iec958 {
	unsigned char status[24];	/* AES/IEC958 channel status bits */
	unsigned char subcode[147];	/* AES/IEC958 subcode bits */
	unsigned char pad;		/* nothing */
	unsigned char dig_subframe[4];	/* AES/IEC958 subframe bits */
};

/****************************************************************************
 *                                                                          *
 *      Section for driver hardware dependent interface - /dev/snd/hw?      *
 *                                                                          *
 ****************************************************************************/

#define SNDRV_HWDEP_VERSION		SNDRV_PROTOCOL_VERSION(1, 0, 1)

enum sndrv_hwdep_iface {
	SNDRV_HWDEP_IFACE_OPL2 = 0,
	SNDRV_HWDEP_IFACE_OPL3,
	SNDRV_HWDEP_IFACE_OPL4,
	SNDRV_HWDEP_IFACE_SB16CSP,	/* Creative Signal Processor */
	SNDRV_HWDEP_IFACE_EMU10K1,	/* FX8010 processor in EMU10K1 chip */
	SNDRV_HWDEP_IFACE_YSS225,	/* Yamaha FX processor */
	SNDRV_HWDEP_IFACE_ICS2115,	/* Wavetable synth */
	SNDRV_HWDEP_IFACE_SSCAPE,	/* Ensoniq SoundScape ISA card (MC68EC000) */
	SNDRV_HWDEP_IFACE_VX,		/* Digigram VX cards */
	SNDRV_HWDEP_IFACE_MIXART,	/* Digigram miXart cards */
	SNDRV_HWDEP_IFACE_USX2Y,	/* Tascam US122, US224 & US428 usb */
	SNDRV_HWDEP_IFACE_EMUX_WAVETABLE, /* EmuX wavetable */	
	SNDRV_HWDEP_IFACE_BLUETOOTH,	/* Bluetooth audio */
	SNDRV_HWDEP_IFACE_USX2Y_PCM,	/* Tascam US122, US224 & US428 rawusb pcm */
	SNDRV_HWDEP_IFACE_PCXHR,	/* Digigram PCXHR */
	SNDRV_HWDEP_IFACE_SB_RC,	/* SB Extigy/Audigy2NX remote control */

	/* Don't forget to change the following: */
	SNDRV_HWDEP_IFACE_LAST = SNDRV_HWDEP_IFACE_SB_RC
};

struct sndrv_hwdep_info {
	unsigned int device;		/* WR: device number */
	int card;			/* R: card number */
	unsigned char id[64];		/* ID (user selectable) */
	unsigned char name[80];		/* hwdep name */
	enum sndrv_hwdep_iface iface;	/* hwdep interface */
	unsigned char reserved[64];	/* reserved for future */
};

/* generic DSP loader */
struct sndrv_hwdep_dsp_status {
	unsigned int version;		/* R: driver-specific version */
	unsigned char id[32];		/* R: driver-specific ID string */
	unsigned int num_dsps;		/* R: number of DSP images to transfer */
	unsigned int dsp_loaded;	/* R: bit flags indicating the loaded DSPs */
	unsigned int chip_ready;	/* R: 1 = initialization finished */
	unsigned char reserved[16];	/* reserved for future use */
};

struct sndrv_hwdep_dsp_image {
	unsigned int index;		/* W: DSP index */
	unsigned char name[64];		/* W: ID (e.g. file name) */
	unsigned char __user *image;	/* W: binary image */
	size_t length;			/* W: size of image in bytes */
	unsigned long driver_data;	/* W: driver-specific data */
};

enum {
	SNDRV_HWDEP_IOCTL_PVERSION = _IOR ('H', 0x00, int),
	SNDRV_HWDEP_IOCTL_INFO = _IOR ('H', 0x01, struct sndrv_hwdep_info),
	SNDRV_HWDEP_IOCTL_DSP_STATUS = _IOR('H', 0x02, struct sndrv_hwdep_dsp_status),
	SNDRV_HWDEP_IOCTL_DSP_LOAD   = _IOW('H', 0x03, struct sndrv_hwdep_dsp_image)
};

/*****************************************************************************
 *                                                                           *
 *             Digital Audio (PCM) interface - /dev/snd/pcm??                *
 *                                                                           *
 *****************************************************************************/

#define SNDRV_PCM_VERSION		SNDRV_PROTOCOL_VERSION(2, 0, 7)

typedef unsigned long sndrv_pcm_uframes_t;
typedef long sndrv_pcm_sframes_t;

enum sndrv_pcm_class {
	SNDRV_PCM_CLASS_GENERIC = 0,	/* standard mono or stereo device */
	SNDRV_PCM_CLASS_MULTI,		/* multichannel device */
	SNDRV_PCM_CLASS_MODEM,		/* software modem class */
	SNDRV_PCM_CLASS_DIGITIZER,	/* digitizer class */
	/* Don't forget to change the following: */
	SNDRV_PCM_CLASS_LAST = SNDRV_PCM_CLASS_DIGITIZER,
};

enum sndrv_pcm_subclass {
	SNDRV_PCM_SUBCLASS_GENERIC_MIX = 0, /* mono or stereo subdevices are mixed together */
	SNDRV_PCM_SUBCLASS_MULTI_MIX,	/* multichannel subdevices are mixed together */
	/* Don't forget to change the following: */
	SNDRV_PCM_SUBCLASS_LAST = SNDRV_PCM_SUBCLASS_MULTI_MIX,
};

enum sndrv_pcm_stream {
	SNDRV_PCM_STREAM_PLAYBACK = 0,
	SNDRV_PCM_STREAM_CAPTURE,
	SNDRV_PCM_STREAM_LAST = SNDRV_PCM_STREAM_CAPTURE,
};

enum sndrv_pcm_access {
	SNDRV_PCM_ACCESS_MMAP_INTERLEAVED = 0,	/* interleaved mmap */
	SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED, 	/* noninterleaved mmap */
	SNDRV_PCM_ACCESS_MMAP_COMPLEX,		/* complex mmap */
	SNDRV_PCM_ACCESS_RW_INTERLEAVED,	/* readi/writei */
	SNDRV_PCM_ACCESS_RW_NONINTERLEAVED,	/* readn/writen */
	SNDRV_PCM_ACCESS_LAST = SNDRV_PCM_ACCESS_RW_NONINTERLEAVED,
};

enum sndrv_pcm_format {
	SNDRV_PCM_FORMAT_S8 = 0,
	SNDRV_PCM_FORMAT_U8,
	SNDRV_PCM_FORMAT_S16_LE,
	SNDRV_PCM_FORMAT_S16_BE,
	SNDRV_PCM_FORMAT_U16_LE,
	SNDRV_PCM_FORMAT_U16_BE,
	SNDRV_PCM_FORMAT_S24_LE,	/* low three bytes */
	SNDRV_PCM_FORMAT_S24_BE,	/* low three bytes */
	SNDRV_PCM_FORMAT_U24_LE,	/* low three bytes */
	SNDRV_PCM_FORMAT_U24_BE,	/* low three bytes */
	SNDRV_PCM_FORMAT_S32_LE,
	SNDRV_PCM_FORMAT_S32_BE,
	SNDRV_PCM_FORMAT_U32_LE,
	SNDRV_PCM_FORMAT_U32_BE,
	SNDRV_PCM_FORMAT_FLOAT_LE,	/* 4-byte float, IEEE-754 32-bit, range -1.0 to 1.0 */
	SNDRV_PCM_FORMAT_FLOAT_BE,	/* 4-byte float, IEEE-754 32-bit, range -1.0 to 1.0 */
	SNDRV_PCM_FORMAT_FLOAT64_LE,	/* 8-byte float, IEEE-754 64-bit, range -1.0 to 1.0 */
	SNDRV_PCM_FORMAT_FLOAT64_BE,	/* 8-byte float, IEEE-754 64-bit, range -1.0 to 1.0 */
	SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE,	/* IEC-958 subframe, Little Endian */
	SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE,	/* IEC-958 subframe, Big Endian */
	SNDRV_PCM_FORMAT_MU_LAW,
	SNDRV_PCM_FORMAT_A_LAW,
	SNDRV_PCM_FORMAT_IMA_ADPCM,
	SNDRV_PCM_FORMAT_MPEG,
	SNDRV_PCM_FORMAT_GSM,
	SNDRV_PCM_FORMAT_SPECIAL = 31,
	SNDRV_PCM_FORMAT_S24_3LE = 32,	/* in three bytes */
	SNDRV_PCM_FORMAT_S24_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U24_3LE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U24_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_S20_3LE,	/* in three bytes */
	SNDRV_PCM_FORMAT_S20_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U20_3LE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U20_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_S18_3LE,	/* in three bytes */
	SNDRV_PCM_FORMAT_S18_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U18_3LE,	/* in three bytes */
	SNDRV_PCM_FORMAT_U18_3BE,	/* in three bytes */
	SNDRV_PCM_FORMAT_LAST = SNDRV_PCM_FORMAT_U18_3BE,

#ifdef SNDRV_LITTLE_ENDIAN
	SNDRV_PCM_FORMAT_S16 = SNDRV_PCM_FORMAT_S16_LE,
	SNDRV_PCM_FORMAT_U16 = SNDRV_PCM_FORMAT_U16_LE,
	SNDRV_PCM_FORMAT_S24 = SNDRV_PCM_FORMAT_S24_LE,
	SNDRV_PCM_FORMAT_U24 = SNDRV_PCM_FORMAT_U24_LE,
	SNDRV_PCM_FORMAT_S32 = SNDRV_PCM_FORMAT_S32_LE,
	SNDRV_PCM_FORMAT_U32 = SNDRV_PCM_FORMAT_U32_LE,
	SNDRV_PCM_FORMAT_FLOAT = SNDRV_PCM_FORMAT_FLOAT_LE,
	SNDRV_PCM_FORMAT_FLOAT64 = SNDRV_PCM_FORMAT_FLOAT64_LE,
	SNDRV_PCM_FORMAT_IEC958_SUBFRAME = SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE,
#endif
#ifdef SNDRV_BIG_ENDIAN
	SNDRV_PCM_FORMAT_S16 = SNDRV_PCM_FORMAT_S16_BE,
	SNDRV_PCM_FORMAT_U16 = SNDRV_PCM_FORMAT_U16_BE,
	SNDRV_PCM_FORMAT_S24 = SNDRV_PCM_FORMAT_S24_BE,
	SNDRV_PCM_FORMAT_U24 = SNDRV_PCM_FORMAT_U24_BE,
	SNDRV_PCM_FORMAT_S32 = SNDRV_PCM_FORMAT_S32_BE,
	SNDRV_PCM_FORMAT_U32 = SNDRV_PCM_FORMAT_U32_BE,
	SNDRV_PCM_FORMAT_FLOAT = SNDRV_PCM_FORMAT_FLOAT_BE,
	SNDRV_PCM_FORMAT_FLOAT64 = SNDRV_PCM_FORMAT_FLOAT64_BE,
	SNDRV_PCM_FORMAT_IEC958_SUBFRAME = SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE,
#endif
};

enum sndrv_pcm_subformat {
	SNDRV_PCM_SUBFORMAT_STD = 0,
	SNDRV_PCM_SUBFORMAT_LAST = SNDRV_PCM_SUBFORMAT_STD,
};

#define SNDRV_PCM_INFO_MMAP		0x00000001	/* hardware supports mmap */
#define SNDRV_PCM_INFO_MMAP_VALID	0x00000002	/* period data are valid during transfer */
#define SNDRV_PCM_INFO_DOUBLE		0x00000004	/* Double buffering needed for PCM start/stop */
#define SNDRV_PCM_INFO_BATCH		0x00000010	/* double buffering */
#define SNDRV_PCM_INFO_INTERLEAVED	0x00000100	/* channels are interleaved */
#define SNDRV_PCM_INFO_NONINTERLEAVED	0x00000200	/* channels are not interleaved */
#define SNDRV_PCM_INFO_COMPLEX		0x00000400	/* complex frame organization (mmap only) */
#define SNDRV_PCM_INFO_BLOCK_TRANSFER	0x00010000	/* hardware transfer block of samples */
#define SNDRV_PCM_INFO_OVERRANGE	0x00020000	/* hardware supports ADC (capture) overrange detection */
#define SNDRV_PCM_INFO_RESUME		0x00040000	/* hardware supports stream resume after suspend */
#define SNDRV_PCM_INFO_PAUSE		0x00080000	/* pause ioctl is supported */
#define SNDRV_PCM_INFO_HALF_DUPLEX	0x00100000	/* only half duplex */
#define SNDRV_PCM_INFO_JOINT_DUPLEX	0x00200000	/* playback and capture stream are somewhat correlated */
#define SNDRV_PCM_INFO_SYNC_START	0x00400000	/* pcm support some kind of sync go */

enum sndrv_pcm_state {
	SNDRV_PCM_STATE_OPEN = 0,	/* stream is open */
	SNDRV_PCM_STATE_SETUP,		/* stream has a setup */
	SNDRV_PCM_STATE_PREPARED,	/* stream is ready to start */
	SNDRV_PCM_STATE_RUNNING,	/* stream is running */
	SNDRV_PCM_STATE_XRUN,		/* stream reached an xrun */
	SNDRV_PCM_STATE_DRAINING,	/* stream is draining */
	SNDRV_PCM_STATE_PAUSED,		/* stream is paused */
	SNDRV_PCM_STATE_SUSPENDED,	/* hardware is suspended */
	SNDRV_PCM_STATE_DISCONNECTED,	/* hardware is disconnected */
	SNDRV_PCM_STATE_LAST = SNDRV_PCM_STATE_DISCONNECTED,
};

enum {
	SNDRV_PCM_MMAP_OFFSET_DATA = 0x00000000,
	SNDRV_PCM_MMAP_OFFSET_STATUS = 0x80000000,
	SNDRV_PCM_MMAP_OFFSET_CONTROL = 0x81000000,
};

union sndrv_pcm_sync_id {
	unsigned char id[16];
	unsigned short id16[8];
	unsigned int id32[4];
};

struct sndrv_pcm_info {
	unsigned int device;		/* RO/WR (control): device number */
	unsigned int subdevice;		/* RO/WR (control): subdevice number */
	enum sndrv_pcm_stream stream;	/* RO/WR (control): stream number */
	int card;			/* R: card number */
	unsigned char id[64];		/* ID (user selectable) */
	unsigned char name[80];		/* name of this device */
	unsigned char subname[32];	/* subdevice name */
	enum sndrv_pcm_class dev_class;	/* SNDRV_PCM_CLASS_* */
	enum sndrv_pcm_subclass dev_subclass; /* SNDRV_PCM_SUBCLASS_* */
	unsigned int subdevices_count;
	unsigned int subdevices_avail;
	union sndrv_pcm_sync_id sync;	/* hardware synchronization ID */
	unsigned char reserved[64];	/* reserved for future... */
};

enum sndrv_pcm_hw_param {
	SNDRV_PCM_HW_PARAM_ACCESS = 0,	/* Access type */
	SNDRV_PCM_HW_PARAM_FIRST_MASK = SNDRV_PCM_HW_PARAM_ACCESS,
	SNDRV_PCM_HW_PARAM_FORMAT,	/* Format */
	SNDRV_PCM_HW_PARAM_SUBFORMAT,	/* Subformat */
	SNDRV_PCM_HW_PARAM_LAST_MASK = SNDRV_PCM_HW_PARAM_SUBFORMAT,

	SNDRV_PCM_HW_PARAM_SAMPLE_BITS = 8, /* Bits per sample */
	SNDRV_PCM_HW_PARAM_FIRST_INTERVAL = SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
	SNDRV_PCM_HW_PARAM_FRAME_BITS,	/* Bits per frame */
	SNDRV_PCM_HW_PARAM_CHANNELS,	/* Channels */
	SNDRV_PCM_HW_PARAM_RATE,	/* Approx rate */
	SNDRV_PCM_HW_PARAM_PERIOD_TIME,	/* Approx distance between interrupts
					   in us */
	SNDRV_PCM_HW_PARAM_PERIOD_SIZE,	/* Approx frames between interrupts */
	SNDRV_PCM_HW_PARAM_PERIOD_BYTES, /* Approx bytes between interrupts */
	SNDRV_PCM_HW_PARAM_PERIODS,	/* Approx interrupts per buffer */
	SNDRV_PCM_HW_PARAM_BUFFER_TIME,	/* Approx duration of buffer in us */
	SNDRV_PCM_HW_PARAM_BUFFER_SIZE,	/* Size of buffer in frames */
	SNDRV_PCM_HW_PARAM_BUFFER_BYTES, /* Size of buffer in bytes */
	SNDRV_PCM_HW_PARAM_TICK_TIME,	/* Approx tick duration in us */
	SNDRV_PCM_HW_PARAM_LAST_INTERVAL = SNDRV_PCM_HW_PARAM_TICK_TIME
};

#define SNDRV_PCM_HW_PARAMS_NORESAMPLE		(1<<0)	/* avoid rate resampling */

struct sndrv_interval {
	unsigned int min, max;
	unsigned int openmin:1,
		     openmax:1,
		     integer:1,
		     empty:1;
};

#define SNDRV_MASK_MAX	256

struct sndrv_mask {
	u_int32_t bits[(SNDRV_MASK_MAX+31)/32];
};

struct sndrv_pcm_hw_params {
	unsigned int flags;
	struct sndrv_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK - 
			       SNDRV_PCM_HW_PARAM_FIRST_MASK + 1];
	struct sndrv_mask mres[5];	/* reserved masks */
	struct sndrv_interval intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
				        SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
	struct sndrv_interval ires[9];	/* reserved intervals */
	unsigned int rmask;		/* W: requested masks */
	unsigned int cmask;		/* R: changed masks */
	unsigned int info;		/* R: Info flags for returned setup */
	unsigned int msbits;		/* R: used most significant bits */
	unsigned int rate_num;		/* R: rate numerator */
	unsigned int rate_den;		/* R: rate denominator */
	sndrv_pcm_uframes_t fifo_size;	/* R: chip FIFO size in frames */
	unsigned char reserved[64];	/* reserved for future */
};

enum sndrv_pcm_tstamp {
	SNDRV_PCM_TSTAMP_NONE = 0,
	SNDRV_PCM_TSTAMP_MMAP,
	SNDRV_PCM_TSTAMP_LAST = SNDRV_PCM_TSTAMP_MMAP,
};

struct sndrv_pcm_sw_params {
	enum sndrv_pcm_tstamp tstamp_mode;	/* timestamp mode */
	unsigned int period_step;
	unsigned int sleep_min;			/* min ticks to sleep */
	sndrv_pcm_uframes_t avail_min;		/* min avail frames for wakeup */
	sndrv_pcm_uframes_t xfer_align;		/* xfer size need to be a multiple */
	sndrv_pcm_uframes_t start_threshold;	/* min hw_avail frames for automatic start */
	sndrv_pcm_uframes_t stop_threshold;	/* min avail frames for automatic stop */
	sndrv_pcm_uframes_t silence_threshold;	/* min distance from noise for silence filling */
	sndrv_pcm_uframes_t silence_size;	/* silence block size */
	sndrv_pcm_uframes_t boundary;		/* pointers wrap point */
	unsigned char reserved[64];		/* reserved for future */
};

struct sndrv_pcm_channel_info {
	unsigned int channel;
	off_t offset;			/* mmap offset */
	unsigned int first;		/* offset to first sample in bits */
	unsigned int step;		/* samples distance in bits */
};

struct sndrv_pcm_status {
	enum sndrv_pcm_state state;	/* stream state */
	struct timespec trigger_tstamp;	/* time when stream was started/stopped/paused */
	struct timespec tstamp;		/* reference timestamp */
	sndrv_pcm_uframes_t appl_ptr;	/* appl ptr */
	sndrv_pcm_uframes_t hw_ptr;	/* hw ptr */
	sndrv_pcm_sframes_t delay;	/* current delay in frames */
	sndrv_pcm_uframes_t avail;	/* number of frames available */
	sndrv_pcm_uframes_t avail_max;	/* max frames available on hw since last status */
	sndrv_pcm_uframes_t overrange;	/* count of ADC (capture) overrange detections from last status */
	enum sndrv_pcm_state suspended_state; /* suspended stream state */
	unsigned char reserved[60];	/* must be filled with zero */
};

struct sndrv_pcm_mmap_status {
	enum sndrv_pcm_state state;	/* RO: state - SNDRV_PCM_STATE_XXXX */
	int pad1;			/* Needed for 64 bit alignment */
	sndrv_pcm_uframes_t hw_ptr;	/* RO: hw ptr (0...boundary-1) */
	struct timespec tstamp;		/* Timestamp */
	enum sndrv_pcm_state suspended_state; /* RO: suspended stream state */
};

struct sndrv_pcm_mmap_control {
	sndrv_pcm_uframes_t appl_ptr;	/* RW: appl ptr (0...boundary-1) */
	sndrv_pcm_uframes_t avail_min;	/* RW: min available frames for wakeup */
};

#define SNDRV_PCM_SYNC_PTR_HWSYNC	(1<<0)	/* execute hwsync */
#define SNDRV_PCM_SYNC_PTR_APPL		(1<<1)	/* get appl_ptr from driver (r/w op) */
#define SNDRV_PCM_SYNC_PTR_AVAIL_MIN	(1<<2)	/* get avail_min from driver */

struct sndrv_pcm_sync_ptr {
	unsigned int flags;
	union {
		struct sndrv_pcm_mmap_status status;
		unsigned char reserved[64];
	} s;
	union {
		struct sndrv_pcm_mmap_control control;
		unsigned char reserved[64];
	} c;
};

struct sndrv_xferi {
	sndrv_pcm_sframes_t result;
	void __user *buf;
	sndrv_pcm_uframes_t frames;
};

struct sndrv_xfern {
	sndrv_pcm_sframes_t result;
	void __user * __user *bufs;
	sndrv_pcm_uframes_t frames;
};

enum {
	SNDRV_PCM_IOCTL_PVERSION = _IOR('A', 0x00, int),
	SNDRV_PCM_IOCTL_INFO = _IOR('A', 0x01, struct sndrv_pcm_info),
	SNDRV_PCM_IOCTL_TSTAMP = _IOW('A', 0x02, int),
	SNDRV_PCM_IOCTL_HW_REFINE = _IOWR('A', 0x10, struct sndrv_pcm_hw_params),
	SNDRV_PCM_IOCTL_HW_PARAMS = _IOWR('A', 0x11, struct sndrv_pcm_hw_params),
	SNDRV_PCM_IOCTL_HW_FREE = _IO('A', 0x12),
	SNDRV_PCM_IOCTL_SW_PARAMS = _IOWR('A', 0x13, struct sndrv_pcm_sw_params),
	SNDRV_PCM_IOCTL_STATUS = _IOR('A', 0x20, struct sndrv_pcm_status),
	SNDRV_PCM_IOCTL_DELAY = _IOR('A', 0x21, sndrv_pcm_sframes_t),
	SNDRV_PCM_IOCTL_HWSYNC = _IO('A', 0x22),
	SNDRV_PCM_IOCTL_SYNC_PTR = _IOWR('A', 0x23, struct sndrv_pcm_sync_ptr),
	SNDRV_PCM_IOCTL_CHANNEL_INFO = _IOR('A', 0x32, struct sndrv_pcm_channel_info),
	SNDRV_PCM_IOCTL_PREPARE = _IO('A', 0x40),
	SNDRV_PCM_IOCTL_RESET = _IO('A', 0x41),
	SNDRV_PCM_IOCTL_START = _IO('A', 0x42),
	SNDRV_PCM_IOCTL_DROP = _IO('A', 0x43),
	SNDRV_PCM_IOCTL_DRAIN = _IO('A', 0x44),
	SNDRV_PCM_IOCTL_PAUSE = _IOW('A', 0x45, int),
	SNDRV_PCM_IOCTL_REWIND = _IOW('A', 0x46, sndrv_pcm_uframes_t),
	SNDRV_PCM_IOCTL_RESUME = _IO('A', 0x47),
	SNDRV_PCM_IOCTL_XRUN = _IO('A', 0x48),
	SNDRV_PCM_IOCTL_FORWARD = _IOW('A', 0x49, sndrv_pcm_uframes_t),
	SNDRV_PCM_IOCTL_WRITEI_FRAMES = _IOW('A', 0x50, struct sndrv_xferi),
	SNDRV_PCM_IOCTL_READI_FRAMES = _IOR('A', 0x51, struct sndrv_xferi),
	SNDRV_PCM_IOCTL_WRITEN_FRAMES = _IOW('A', 0x52, struct sndrv_xfern),
	SNDRV_PCM_IOCTL_READN_FRAMES = _IOR('A', 0x53, struct sndrv_xfern),
	SNDRV_PCM_IOCTL_LINK = _IOW('A', 0x60, int),
	SNDRV_PCM_IOCTL_UNLINK = _IO('A', 0x61),
};

/* Trick to make alsa-lib/acinclude.m4 happy */
#define SNDRV_PCM_IOCTL_REWIND SNDRV_PCM_IOCTL_REWIND

/*****************************************************************************
 *                                                                           *
 *                            MIDI v1.0 interface                            *
 *                                                                           *
 *****************************************************************************/

/*
 *  Raw MIDI section - /dev/snd/midi??
 */

#define SNDRV_RAWMIDI_VERSION		SNDRV_PROTOCOL_VERSION(2, 0, 0)

enum sndrv_rawmidi_stream {
	SNDRV_RAWMIDI_STREAM_OUTPUT = 0,
	SNDRV_RAWMIDI_STREAM_INPUT,
	SNDRV_RAWMIDI_STREAM_LAST = SNDRV_RAWMIDI_STREAM_INPUT,
};

#define SNDRV_RAWMIDI_INFO_OUTPUT		0x00000001
#define SNDRV_RAWMIDI_INFO_INPUT		0x00000002
#define SNDRV_RAWMIDI_INFO_DUPLEX		0x00000004

struct sndrv_rawmidi_info {
	unsigned int device;		/* RO/WR (control): device number */
	unsigned int subdevice;		/* RO/WR (control): subdevice number */
	enum sndrv_rawmidi_stream stream; /* WR: stream */
	int card;			/* R: card number */
	unsigned int flags;		/* SNDRV_RAWMIDI_INFO_XXXX */
	unsigned char id[64];		/* ID (user selectable) */
	unsigned char name[80];		/* name of device */
	unsigned char subname[32];	/* name of active or selected subdevice */
	unsigned int subdevices_count;
	unsigned int subdevices_avail;
	unsigned char reserved[64];	/* reserved for future use */
};

struct sndrv_rawmidi_params {
	enum sndrv_rawmidi_stream stream;
	size_t buffer_size;		/* queue size in bytes */
	size_t avail_min;		/* minimum avail bytes for wakeup */
	unsigned int no_active_sensing: 1; /* do not send active sensing byte in close() */
	unsigned char reserved[16];	/* reserved for future use */
};

struct sndrv_rawmidi_status {
	enum sndrv_rawmidi_stream stream;
	struct timespec tstamp;		/* Timestamp */
	size_t avail;			/* available bytes */
	size_t xruns;			/* count of overruns since last status (in bytes) */
	unsigned char reserved[16];	/* reserved for future use */
};

enum {
	SNDRV_RAWMIDI_IOCTL_PVERSION = _IOR('W', 0x00, int),
	SNDRV_RAWMIDI_IOCTL_INFO = _IOR('W', 0x01, struct sndrv_rawmidi_info),
	SNDRV_RAWMIDI_IOCTL_PARAMS = _IOWR('W', 0x10, struct sndrv_rawmidi_params),
	SNDRV_RAWMIDI_IOCTL_STATUS = _IOWR('W', 0x20, struct sndrv_rawmidi_status),
	SNDRV_RAWMIDI_IOCTL_DROP = _IOW('W', 0x30, int),
	SNDRV_RAWMIDI_IOCTL_DRAIN = _IOW('W', 0x31, int),
};

/*
 *  Timer section - /dev/snd/timer
 */

#define SNDRV_TIMER_VERSION		SNDRV_PROTOCOL_VERSION(2, 0, 5)

enum sndrv_timer_class {
	SNDRV_TIMER_CLASS_NONE = -1,
	SNDRV_TIMER_CLASS_SLAVE = 0,
	SNDRV_TIMER_CLASS_GLOBAL,
	SNDRV_TIMER_CLASS_CARD,
	SNDRV_TIMER_CLASS_PCM,
	SNDRV_TIMER_CLASS_LAST = SNDRV_TIMER_CLASS_PCM,
};

/* slave timer classes */
enum sndrv_timer_slave_class {
	SNDRV_TIMER_SCLASS_NONE = 0,
	SNDRV_TIMER_SCLASS_APPLICATION,
	SNDRV_TIMER_SCLASS_SEQUENCER,		/* alias */
	SNDRV_TIMER_SCLASS_OSS_SEQUENCER,	/* alias */
	SNDRV_TIMER_SCLASS_LAST = SNDRV_TIMER_SCLASS_OSS_SEQUENCER,
};

/* global timers (device member) */
#define SNDRV_TIMER_GLOBAL_SYSTEM	0
#define SNDRV_TIMER_GLOBAL_RTC		1
#define SNDRV_TIMER_GLOBAL_HPET		2

/* info flags */
#define SNDRV_TIMER_FLG_SLAVE		(1<<0)	/* cannot be controlled */

struct sndrv_timer_id {
	enum sndrv_timer_class dev_class;	
	enum sndrv_timer_slave_class dev_sclass;
	int card;
	int device;
	int subdevice;
};

struct sndrv_timer_ginfo {
	struct sndrv_timer_id tid;	/* requested timer ID */
	unsigned int flags;		/* timer flags - SNDRV_TIMER_FLG_* */
	int card;			/* card number */
	unsigned char id[64];		/* timer identification */
	unsigned char name[80];		/* timer name */
	unsigned long reserved0;	/* reserved for future use */
	unsigned long resolution;	/* average period resolution in ns */
	unsigned long resolution_min;	/* minimal period resolution in ns */
	unsigned long resolution_max;	/* maximal period resolution in ns */
	unsigned int clients;		/* active timer clients */
	unsigned char reserved[32];
};

struct sndrv_timer_gparams {
	struct sndrv_timer_id tid;	/* requested timer ID */
	unsigned long period_num;	/* requested precise period duration (in seconds) - numerator */
	unsigned long period_den;	/* requested precise period duration (in seconds) - denominator */
	unsigned char reserved[32];
};

struct sndrv_timer_gstatus {
	struct sndrv_timer_id tid;	/* requested timer ID */
	unsigned long resolution;	/* current period resolution in ns */
	unsigned long resolution_num;	/* precise current period resolution (in seconds) - numerator */
	unsigned long resolution_den;	/* precise current period resolution (in seconds) - denominator */
	unsigned char reserved[32];
};

struct sndrv_timer_select {
	struct sndrv_timer_id id;	/* bind to timer ID */
	unsigned char reserved[32];	/* reserved */
};

struct sndrv_timer_info {
	unsigned int flags;		/* timer flags - SNDRV_TIMER_FLG_* */
	int card;			/* card number */
	unsigned char id[64];		/* timer identificator */
	unsigned char name[80];		/* timer name */
	unsigned long reserved0;	/* reserved for future use */
	unsigned long resolution;	/* average period resolution in ns */
	unsigned char reserved[64];	/* reserved */
};

#define SNDRV_TIMER_PSFLG_AUTO		(1<<0)	/* auto start, otherwise one-shot */
#define SNDRV_TIMER_PSFLG_EXCLUSIVE	(1<<1)	/* exclusive use, precise start/stop/pause/continue */
#define SNDRV_TIMER_PSFLG_EARLY_EVENT	(1<<2)	/* write early event to the poll queue */

struct sndrv_timer_params {
	unsigned int flags;		/* flags - SNDRV_MIXER_PSFLG_* */
	unsigned int ticks;		/* requested resolution in ticks */
	unsigned int queue_size;	/* total size of queue (32-1024) */
	unsigned int reserved0;		/* reserved, was: failure locations */
	unsigned int filter;		/* event filter (bitmask of SNDRV_TIMER_EVENT_*) */
	unsigned char reserved[60];	/* reserved */
};

struct sndrv_timer_status {
	struct timespec tstamp;		/* Timestamp - last update */
	unsigned int resolution;	/* current period resolution in ns */
	unsigned int lost;		/* counter of master tick lost */
	unsigned int overrun;		/* count of read queue overruns */
	unsigned int queue;		/* used queue size */
	unsigned char reserved[64];	/* reserved */
};

enum {
	SNDRV_TIMER_IOCTL_PVERSION = _IOR('T', 0x00, int),
	SNDRV_TIMER_IOCTL_NEXT_DEVICE = _IOWR('T', 0x01, struct sndrv_timer_id),
	SNDRV_TIMER_IOCTL_TREAD = _IOW('T', 0x02, int),
	SNDRV_TIMER_IOCTL_GINFO = _IOWR('T', 0x03, struct sndrv_timer_ginfo),
	SNDRV_TIMER_IOCTL_GPARAMS = _IOW('T', 0x04, struct sndrv_timer_gparams),
	SNDRV_TIMER_IOCTL_GSTATUS = _IOWR('T', 0x05, struct sndrv_timer_gstatus),
	SNDRV_TIMER_IOCTL_SELECT = _IOW('T', 0x10, struct sndrv_timer_select),
	SNDRV_TIMER_IOCTL_INFO = _IOR('T', 0x11, struct sndrv_timer_info),
	SNDRV_TIMER_IOCTL_PARAMS = _IOW('T', 0x12, struct sndrv_timer_params),
	SNDRV_TIMER_IOCTL_STATUS = _IOR('T', 0x14, struct sndrv_timer_status),
	/* The following four ioctls are changed since 1.0.9 due to confliction */
	SNDRV_TIMER_IOCTL_START = _IO('T', 0xa0),
	SNDRV_TIMER_IOCTL_STOP = _IO('T', 0xa1),
	SNDRV_TIMER_IOCTL_CONTINUE = _IO('T', 0xa2),
	SNDRV_TIMER_IOCTL_PAUSE = _IO('T', 0xa3),
};

struct sndrv_timer_read {
	unsigned int resolution;
	unsigned int ticks;
};

enum sndrv_timer_event {
	SNDRV_TIMER_EVENT_RESOLUTION = 0,	/* val = resolution in ns */
	SNDRV_TIMER_EVENT_TICK,			/* val = ticks */
	SNDRV_TIMER_EVENT_START,		/* val = resolution in ns */
	SNDRV_TIMER_EVENT_STOP,			/* val = 0 */
	SNDRV_TIMER_EVENT_CONTINUE,		/* val = resolution in ns */
	SNDRV_TIMER_EVENT_PAUSE,		/* val = 0 */
	SNDRV_TIMER_EVENT_EARLY,		/* val = 0, early event */
	SNDRV_TIMER_EVENT_SUSPEND,		/* val = 0 */
	SNDRV_TIMER_EVENT_RESUME,		/* val = resolution in ns */
	/* master timer events for slave timer instances */
	SNDRV_TIMER_EVENT_MSTART = SNDRV_TIMER_EVENT_START + 10,
	SNDRV_TIMER_EVENT_MSTOP = SNDRV_TIMER_EVENT_STOP + 10,
	SNDRV_TIMER_EVENT_MCONTINUE = SNDRV_TIMER_EVENT_CONTINUE + 10,
	SNDRV_TIMER_EVENT_MPAUSE = SNDRV_TIMER_EVENT_PAUSE + 10,
	SNDRV_TIMER_EVENT_MSUSPEND = SNDRV_TIMER_EVENT_SUSPEND + 10,
	SNDRV_TIMER_EVENT_MRESUME = SNDRV_TIMER_EVENT_RESUME + 10,
};

struct sndrv_timer_tread {
	enum sndrv_timer_event event;
	struct timespec tstamp;
	unsigned int val;
};

/****************************************************************************
 *                                                                          *
 *        Section for driver control interface - /dev/snd/control?          *
 *                                                                          *
 ****************************************************************************/

#define SNDRV_CTL_VERSION		SNDRV_PROTOCOL_VERSION(2, 0, 3)

struct sndrv_ctl_card_info {
	int card;			/* card number */
	int pad;			/* reserved for future (was type) */
	unsigned char id[16];		/* ID of card (user selectable) */
	unsigned char driver[16];	/* Driver name */
	unsigned char name[32];		/* Short name of soundcard */
	unsigned char longname[80];	/* name + info text about soundcard */
	unsigned char reserved_[16];	/* reserved for future (was ID of mixer) */
	unsigned char mixername[80];	/* visual mixer identification */
	unsigned char components[80];	/* card components / fine identification, delimited with one space (AC97 etc..) */
	unsigned char reserved[48];	/* reserved for future */
};

enum sndrv_ctl_elem_type {
	SNDRV_CTL_ELEM_TYPE_NONE = 0,		/* invalid */
	SNDRV_CTL_ELEM_TYPE_BOOLEAN,		/* boolean type */
	SNDRV_CTL_ELEM_TYPE_INTEGER,		/* integer type */
	SNDRV_CTL_ELEM_TYPE_ENUMERATED,		/* enumerated type */
	SNDRV_CTL_ELEM_TYPE_BYTES,		/* byte array */
	SNDRV_CTL_ELEM_TYPE_IEC958,		/* IEC958 (S/PDIF) setup */
	SNDRV_CTL_ELEM_TYPE_INTEGER64,		/* 64-bit integer type */
	SNDRV_CTL_ELEM_TYPE_LAST = SNDRV_CTL_ELEM_TYPE_INTEGER64,
};

enum sndrv_ctl_elem_iface {
	SNDRV_CTL_ELEM_IFACE_CARD = 0,		/* global control */
	SNDRV_CTL_ELEM_IFACE_HWDEP,		/* hardware dependent device */
	SNDRV_CTL_ELEM_IFACE_MIXER,		/* virtual mixer device */
	SNDRV_CTL_ELEM_IFACE_PCM,		/* PCM device */
	SNDRV_CTL_ELEM_IFACE_RAWMIDI,		/* RawMidi device */
	SNDRV_CTL_ELEM_IFACE_TIMER,		/* timer device */
	SNDRV_CTL_ELEM_IFACE_SEQUENCER,		/* sequencer client */
	SNDRV_CTL_ELEM_IFACE_LAST = SNDRV_CTL_ELEM_IFACE_SEQUENCER,
};

#define SNDRV_CTL_ELEM_ACCESS_READ		(1<<0)
#define SNDRV_CTL_ELEM_ACCESS_WRITE		(1<<1)
#define SNDRV_CTL_ELEM_ACCESS_READWRITE		(SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE)
#define SNDRV_CTL_ELEM_ACCESS_VOLATILE		(1<<2)	/* control value may be changed without a notification */
#define SNDRV_CTL_ELEM_ACCESS_TIMESTAMP		(1<<2)	/* when was control changed */
#define SNDRV_CTL_ELEM_ACCESS_INACTIVE		(1<<8)	/* control does actually nothing, but may be updated */
#define SNDRV_CTL_ELEM_ACCESS_LOCK		(1<<9)	/* write lock */
#define SNDRV_CTL_ELEM_ACCESS_OWNER		(1<<10)	/* write lock owner */
#define SNDRV_CTL_ELEM_ACCESS_USER		(1<<29) /* user space element */
#define SNDRV_CTL_ELEM_ACCESS_DINDIRECT		(1<<30)	/* indirect access for matrix dimensions in the info structure */
#define SNDRV_CTL_ELEM_ACCESS_INDIRECT		(1<<31)	/* indirect access for element value in the value structure */

/* for further details see the ACPI and PCI power management specification */
#define SNDRV_CTL_POWER_D0		0x0000	/* full On */
#define SNDRV_CTL_POWER_D1		0x0100	/* partial On */
#define SNDRV_CTL_POWER_D2		0x0200	/* partial On */
#define SNDRV_CTL_POWER_D3		0x0300	/* Off */
#define SNDRV_CTL_POWER_D3hot		(SNDRV_CTL_POWER_D3|0x0000)	/* Off, with power */
#define SNDRV_CTL_POWER_D3cold		(SNDRV_CTL_POWER_D3|0x0001)	/* Off, without power */

struct sndrv_ctl_elem_id {
	unsigned int numid;		/* numeric identifier, zero = invalid */
	enum sndrv_ctl_elem_iface iface; /* interface identifier */
	unsigned int device;		/* device/client number */
	unsigned int subdevice;		/* subdevice (substream) number */
        unsigned char name[44];		/* ASCII name of item */
	unsigned int index;		/* index of item */
};

struct sndrv_ctl_elem_list {
	unsigned int offset;		/* W: first element ID to get */
	unsigned int space;		/* W: count of element IDs to get */
	unsigned int used;		/* R: count of element IDs set */
	unsigned int count;		/* R: count of all elements */
	struct sndrv_ctl_elem_id __user *pids; /* R: IDs */
	unsigned char reserved[50];
};

struct sndrv_ctl_elem_info {
	struct sndrv_ctl_elem_id id;	/* W: element ID */
	enum sndrv_ctl_elem_type type;	/* R: value type - SNDRV_CTL_ELEM_TYPE_* */
	unsigned int access;		/* R: value access (bitmask) - SNDRV_CTL_ELEM_ACCESS_* */
	unsigned int count;		/* count of values */
	pid_t owner;			/* owner's PID of this control */
	union {
		struct {
			long min;		/* R: minimum value */
			long max;		/* R: maximum value */
			long step;		/* R: step (0 variable) */
		} integer;
		struct {
			long long min;		/* R: minimum value */
			long long max;		/* R: maximum value */
			long long step;		/* R: step (0 variable) */
		} integer64;
		struct {
			unsigned int items;	/* R: number of items */
			unsigned int item;	/* W: item number */
			char name[64];		/* R: value name */
		} enumerated;
		unsigned char reserved[128];
	} value;
	union {
		unsigned short d[4];		/* dimensions */
		unsigned short *d_ptr;		/* indirect */
	} dimen;
	unsigned char reserved[64-4*sizeof(unsigned short)];
};

struct sndrv_ctl_elem_value {
	struct sndrv_ctl_elem_id id;	/* W: element ID */
	unsigned int indirect: 1;	/* W: use indirect pointer (xxx_ptr member) */
        union {
		union {
			long value[128];
			long *value_ptr;
		} integer;
		union {
			long long value[64];
			long long *value_ptr;
		} integer64;
		union {
			unsigned int item[128];
			unsigned int *item_ptr;
		} enumerated;
		union {
			unsigned char data[512];
			unsigned char *data_ptr;
		} bytes;
		struct sndrv_aes_iec958 iec958;
        } value;                /* RO */
	struct timespec tstamp;
        unsigned char reserved[128-sizeof(struct timespec)];
};

enum {
	SNDRV_CTL_IOCTL_PVERSION = _IOR('U', 0x00, int),
	SNDRV_CTL_IOCTL_CARD_INFO = _IOR('U', 0x01, struct sndrv_ctl_card_info),
	SNDRV_CTL_IOCTL_ELEM_LIST = _IOWR('U', 0x10, struct sndrv_ctl_elem_list),
	SNDRV_CTL_IOCTL_ELEM_INFO = _IOWR('U', 0x11, struct sndrv_ctl_elem_info),
	SNDRV_CTL_IOCTL_ELEM_READ = _IOWR('U', 0x12, struct sndrv_ctl_elem_value),
	SNDRV_CTL_IOCTL_ELEM_WRITE = _IOWR('U', 0x13, struct sndrv_ctl_elem_value),
	SNDRV_CTL_IOCTL_ELEM_LOCK = _IOW('U', 0x14, struct sndrv_ctl_elem_id),
	SNDRV_CTL_IOCTL_ELEM_UNLOCK = _IOW('U', 0x15, struct sndrv_ctl_elem_id),
	SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS = _IOWR('U', 0x16, int),
	SNDRV_CTL_IOCTL_ELEM_ADD = _IOWR('U', 0x17, struct sndrv_ctl_elem_info),
	SNDRV_CTL_IOCTL_ELEM_REPLACE = _IOWR('U', 0x18, struct sndrv_ctl_elem_info),
	SNDRV_CTL_IOCTL_ELEM_REMOVE = _IOWR('U', 0x19, struct sndrv_ctl_elem_id),
	SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE = _IOWR('U', 0x20, int),
	SNDRV_CTL_IOCTL_HWDEP_INFO = _IOR('U', 0x21, struct sndrv_hwdep_info),
	SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE = _IOR('U', 0x30, int),
	SNDRV_CTL_IOCTL_PCM_INFO = _IOWR('U', 0x31, struct sndrv_pcm_info),
	SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE = _IOW('U', 0x32, int),
	SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE = _IOWR('U', 0x40, int),
	SNDRV_CTL_IOCTL_RAWMIDI_INFO = _IOWR('U', 0x41, struct sndrv_rawmidi_info),
	SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE = _IOW('U', 0x42, int),
	SNDRV_CTL_IOCTL_POWER = _IOWR('U', 0xd0, int),
	SNDRV_CTL_IOCTL_POWER_STATE = _IOR('U', 0xd1, int),
};

/*
 *  Read interface.
 */

enum sndrv_ctl_event_type {
	SNDRV_CTL_EVENT_ELEM = 0,
	SNDRV_CTL_EVENT_LAST = SNDRV_CTL_EVENT_ELEM,
};

#define SNDRV_CTL_EVENT_MASK_VALUE	(1<<0)	/* element value was changed */
#define SNDRV_CTL_EVENT_MASK_INFO	(1<<1)	/* element info was changed */
#define SNDRV_CTL_EVENT_MASK_ADD	(1<<2)	/* element was added */
#define SNDRV_CTL_EVENT_MASK_REMOVE	(~0U)	/* element was removed */

struct sndrv_ctl_event {
	enum sndrv_ctl_event_type type;	/* event type - SNDRV_CTL_EVENT_* */
	union {
		struct {
			unsigned int mask;
			struct sndrv_ctl_elem_id id;
		} elem;
                unsigned char data8[60];
        } data;
};

/*
 *  Control names
 */

#define SNDRV_CTL_NAME_NONE				""
#define SNDRV_CTL_NAME_PLAYBACK				"Playback "
#define SNDRV_CTL_NAME_CAPTURE				"Capture "

#define SNDRV_CTL_NAME_IEC958_NONE			""
#define SNDRV_CTL_NAME_IEC958_SWITCH			"Switch"
#define SNDRV_CTL_NAME_IEC958_VOLUME			"Volume"
#define SNDRV_CTL_NAME_IEC958_DEFAULT			"Default"
#define SNDRV_CTL_NAME_IEC958_MASK			"Mask"
#define SNDRV_CTL_NAME_IEC958_CON_MASK			"Con Mask"
#define SNDRV_CTL_NAME_IEC958_PRO_MASK			"Pro Mask"
#define SNDRV_CTL_NAME_IEC958_PCM_STREAM		"PCM Stream"
#define SNDRV_CTL_NAME_IEC958(expl,direction,what)	"IEC958 " expl SNDRV_CTL_NAME_##direction SNDRV_CTL_NAME_IEC958_##what

/*
 *
 */

struct sndrv_xferv {
	const struct iovec *vector;
	unsigned long count;
};

enum {
	SNDRV_IOCTL_READV = _IOW('K', 0x00, struct sndrv_xferv),
	SNDRV_IOCTL_WRITEV = _IOW('K', 0x01, struct sndrv_xferv),
};

#endif /* __SOUND_ASOUND_H */
