/*
 *	Video for Linux version 1 - OBSOLETE
 *
 *	Header file for v4l1 drivers and applications, for
 *	Linux kernels 2.2.x or 2.4.x.
 *
 *	Provides header for legacy drivers and applications
 *
 *	See http://linuxtv.org for more info
 *
 */
#ifndef __LINUX_VIDEODEV_H
#define __LINUX_VIDEODEV_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/videodev2.h>

#if defined(CONFIG_VIDEO_V4L1_COMPAT) || !defined (__KERNEL__)

#define VID_TYPE_CAPTURE	1	/* Can capture */
#define VID_TYPE_TUNER		2	/* Can tune */
#define VID_TYPE_TELETEXT	4	/* Does teletext */
#define VID_TYPE_OVERLAY	8	/* Overlay onto frame buffer */
#define VID_TYPE_CHROMAKEY	16	/* Overlay by chromakey */
#define VID_TYPE_CLIPPING	32	/* Can clip */
#define VID_TYPE_FRAMERAM	64	/* Uses the frame buffer memory */
#define VID_TYPE_SCALES		128	/* Scalable */
#define VID_TYPE_MONOCHROME	256	/* Monochrome only */
#define VID_TYPE_SUBCAPTURE	512	/* Can capture subareas of the image */
#define VID_TYPE_MPEG_DECODER	1024	/* Can decode MPEG streams */
#define VID_TYPE_MPEG_ENCODER	2048	/* Can encode MPEG streams */
#define VID_TYPE_MJPEG_DECODER	4096	/* Can decode MJPEG streams */
#define VID_TYPE_MJPEG_ENCODER	8192	/* Can encode MJPEG streams */

struct video_capability
{
	char name[32];
	int type;
	int channels;	/* Num channels */
	int audios;	/* Num audio devices */
	int maxwidth;	/* Supported width */
	int maxheight;	/* And height */
	int minwidth;	/* Supported width */
	int minheight;	/* And height */
};


struct video_channel
{
	int channel;
	char name[32];
	int tuners;
	__u32  flags;
#define VIDEO_VC_TUNER		1	/* Channel has a tuner */
#define VIDEO_VC_AUDIO		2	/* Channel has audio */
	__u16  type;
#define VIDEO_TYPE_TV		1
#define VIDEO_TYPE_CAMERA	2
	__u16 norm;			/* Norm set by channel */
};

struct video_tuner
{
	int tuner;
	char name[32];
	unsigned long rangelow, rangehigh;	/* Tuner range */
	__u32 flags;
#define VIDEO_TUNER_PAL		1
#define VIDEO_TUNER_NTSC	2
#define VIDEO_TUNER_SECAM	4
#define VIDEO_TUNER_LOW		8	/* Uses KHz not MHz */
#define VIDEO_TUNER_NORM	16	/* Tuner can set norm */
#define VIDEO_TUNER_STEREO_ON	128	/* Tuner is seeing stereo */
#define VIDEO_TUNER_RDS_ON      256     /* Tuner is seeing an RDS datastream */
#define VIDEO_TUNER_MBS_ON      512     /* Tuner is seeing an MBS datastream */
	__u16 mode;			/* PAL/NTSC/SECAM/OTHER */
#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3
	__u16 signal;			/* Signal strength 16bit scale */
};

struct video_picture
{
	__u16	brightness;
	__u16	hue;
	__u16	colour;
	__u16	contrast;
	__u16	whiteness;	/* Black and white only */
	__u16	depth;		/* Capture depth */
	__u16   palette;	/* Palette in use */
#define VIDEO_PALETTE_GREY	1	/* Linear greyscale */
#define VIDEO_PALETTE_HI240	2	/* High 240 cube (BT848) */
#define VIDEO_PALETTE_RGB565	3	/* 565 16 bit RGB */
#define VIDEO_PALETTE_RGB24	4	/* 24bit RGB */
#define VIDEO_PALETTE_RGB32	5	/* 32bit RGB */
#define VIDEO_PALETTE_RGB555	6	/* 555 15bit RGB */
#define VIDEO_PALETTE_YUV422	7	/* YUV422 capture */
#define VIDEO_PALETTE_YUYV	8
#define VIDEO_PALETTE_UYVY	9	/* The great thing about standards is ... */
#define VIDEO_PALETTE_YUV420	10
#define VIDEO_PALETTE_YUV411	11	/* YUV411 capture */
#define VIDEO_PALETTE_RAW	12	/* RAW capture (BT848) */
#define VIDEO_PALETTE_YUV422P	13	/* YUV 4:2:2 Planar */
#define VIDEO_PALETTE_YUV411P	14	/* YUV 4:1:1 Planar */
#define VIDEO_PALETTE_YUV420P	15	/* YUV 4:2:0 Planar */
#define VIDEO_PALETTE_YUV410P	16	/* YUV 4:1:0 Planar */
#define VIDEO_PALETTE_PLANAR	13	/* start of planar entries */
#define VIDEO_PALETTE_COMPONENT 7	/* start of component entries */
};

struct video_audio
{
	int	audio;		/* Audio channel */
	__u16	volume;		/* If settable */
	__u16	bass, treble;
	__u32	flags;
#define VIDEO_AUDIO_MUTE	1
#define VIDEO_AUDIO_MUTABLE	2
#define VIDEO_AUDIO_VOLUME	4
#define VIDEO_AUDIO_BASS	8
#define VIDEO_AUDIO_TREBLE	16
#define VIDEO_AUDIO_BALANCE	32
	char    name[16];
#define VIDEO_SOUND_MONO	1
#define VIDEO_SOUND_STEREO	2
#define VIDEO_SOUND_LANG1	4
#define VIDEO_SOUND_LANG2	8
	__u16   mode;
	__u16	balance;	/* Stereo balance */
	__u16	step;		/* Step actual volume uses */
};

struct video_clip
{
	__s32	x,y;
	__s32	width, height;
	struct	video_clip *next;	/* For user use/driver use only */
};

struct video_window
{
	__u32	x,y;			/* Position of window */
	__u32	width,height;		/* Its size */
	__u32	chromakey;
	__u32	flags;
	struct	video_clip __user *clips;	/* Set only */
	int	clipcount;
#define VIDEO_WINDOW_INTERLACE	1
#define VIDEO_WINDOW_CHROMAKEY	16	/* Overlay by chromakey */
#define VIDEO_CLIP_BITMAP	-1
/* bitmap is 1024x625, a '1' bit represents a clipped pixel */
#define VIDEO_CLIPMAP_SIZE	(128 * 625)
};

struct video_capture
{
	__u32 	x,y;			/* Offsets into image */
	__u32	width, height;		/* Area to capture */
	__u16	decimation;		/* Decimation divider */
	__u16	flags;			/* Flags for capture */
#define VIDEO_CAPTURE_ODD		0	/* Temporal */
#define VIDEO_CAPTURE_EVEN		1
};

struct video_buffer
{
	void	*base;
	int	height,width;
	int	depth;
	int	bytesperline;
};

struct video_mmap
{
	unsigned	int frame;		/* Frame (0 - n) for double buffer */
	int		height,width;
	unsigned	int format;		/* should be VIDEO_PALETTE_* */
};

struct video_key
{
	__u8	key[8];
	__u32	flags;
};

struct video_mbuf
{
	int	size;		/* Total memory to map */
	int	frames;		/* Frames */
	int	offsets[VIDEO_MAX_FRAME];
};

#define 	VIDEO_NO_UNIT	(-1)

struct video_unit
{
	int 	video;		/* Video minor */
	int	vbi;		/* VBI minor */
	int	radio;		/* Radio minor */
	int	audio;		/* Audio minor */
	int	teletext;	/* Teletext minor */
};

struct vbi_format {
	__u32	sampling_rate;	/* in Hz */
	__u32	samples_per_line;
	__u32	sample_format;	/* VIDEO_PALETTE_RAW only (1 byte) */
	__s32	start[2];	/* starting line for each frame */
	__u32	count[2];	/* count of lines for each frame */
	__u32	flags;
#define	VBI_UNSYNC	1	/* can distingues between top/bottom field */
#define	VBI_INTERLACED	2	/* lines are interlaced */
};

/* video_info is biased towards hardware mpeg encode/decode */
/* but it could apply generically to any hardware compressor/decompressor */
struct video_info
{
	__u32	frame_count;	/* frames output since decode/encode began */
	__u32	h_size;		/* current unscaled horizontal size */
	__u32	v_size;		/* current unscaled veritcal size */
	__u32	smpte_timecode;	/* current SMPTE timecode (for current GOP) */
	__u32	picture_type;	/* current picture type */
	__u32	temporal_reference;	/* current temporal reference */
	__u8	user_data[256];	/* user data last found in compressed stream */
	/* user_data[0] contains user data flags, user_data[1] has count */
};

/* generic structure for setting playback modes */
struct video_play_mode
{
	int	mode;
	int	p1;
	int	p2;
};

/* for loading microcode / fpga programming */
struct video_code
{
	char	loadwhat[16];	/* name or tag of file being passed */
	int	datasize;
	__u8	*data;
};

#define VIDIOCGCAP		_IOR('v',1,struct video_capability)	/* Get capabilities */
#define VIDIOCGCHAN		_IOWR('v',2,struct video_channel)	/* Get channel info (sources) */
#define VIDIOCSCHAN		_IOW('v',3,struct video_channel)	/* Set channel 	*/
#define VIDIOCGTUNER		_IOWR('v',4,struct video_tuner)		/* Get tuner abilities */
#define VIDIOCSTUNER		_IOW('v',5,struct video_tuner)		/* Tune the tuner for the current channel */
#define VIDIOCGPICT		_IOR('v',6,struct video_picture)	/* Get picture properties */
#define VIDIOCSPICT		_IOW('v',7,struct video_picture)	/* Set picture properties */
#define VIDIOCCAPTURE		_IOW('v',8,int)				/* Start, end capture */
#define VIDIOCGWIN		_IOR('v',9, struct video_window)	/* Get the video overlay window */
#define VIDIOCSWIN		_IOW('v',10, struct video_window)	/* Set the video overlay window - passes clip list for hardware smarts , chromakey etc */
#define VIDIOCGFBUF		_IOR('v',11, struct video_buffer)	/* Get frame buffer */
#define VIDIOCSFBUF		_IOW('v',12, struct video_buffer)	/* Set frame buffer - root only */
#define VIDIOCKEY		_IOR('v',13, struct video_key)		/* Video key event - to dev 255 is to all - cuts capture on all DMA windows with this key (0xFFFFFFFF == all) */
#define VIDIOCGFREQ		_IOR('v',14, unsigned long)		/* Set tuner */
#define VIDIOCSFREQ		_IOW('v',15, unsigned long)		/* Set tuner */
#define VIDIOCGAUDIO		_IOR('v',16, struct video_audio)	/* Get audio info */
#define VIDIOCSAUDIO		_IOW('v',17, struct video_audio)	/* Audio source, mute etc */
#define VIDIOCSYNC		_IOW('v',18, int)			/* Sync with mmap grabbing */
#define VIDIOCMCAPTURE		_IOW('v',19, struct video_mmap)		/* Grab frames */
#define VIDIOCGMBUF		_IOR('v',20, struct video_mbuf)		/* Memory map buffer info */
#define VIDIOCGUNIT		_IOR('v',21, struct video_unit)		/* Get attached units */
#define VIDIOCGCAPTURE		_IOR('v',22, struct video_capture)	/* Get subcapture */
#define VIDIOCSCAPTURE		_IOW('v',23, struct video_capture)	/* Set subcapture */
#define VIDIOCSPLAYMODE		_IOW('v',24, struct video_play_mode)	/* Set output video mode/feature */
#define VIDIOCSWRITEMODE	_IOW('v',25, int)			/* Set write mode */
#define VIDIOCGPLAYINFO		_IOR('v',26, struct video_info)		/* Get current playback info from hardware */
#define VIDIOCSMICROCODE	_IOW('v',27, struct video_code)		/* Load microcode into hardware */
#define	VIDIOCGVBIFMT		_IOR('v',28, struct vbi_format)		/* Get VBI information */
#define	VIDIOCSVBIFMT		_IOW('v',29, struct vbi_format)		/* Set VBI information */


#define BASE_VIDIOCPRIVATE	192		/* 192-255 are private */

/* VIDIOCSWRITEMODE */
#define VID_WRITE_MPEG_AUD		0
#define VID_WRITE_MPEG_VID		1
#define VID_WRITE_OSD			2
#define VID_WRITE_TTX			3
#define VID_WRITE_CC			4
#define VID_WRITE_MJPEG			5

/* VIDIOCSPLAYMODE */
#define VID_PLAY_VID_OUT_MODE		0
	/* p1: = VIDEO_MODE_PAL, VIDEO_MODE_NTSC, etc ... */
#define VID_PLAY_GENLOCK		1
	/* p1: 0 = OFF, 1 = ON */
	/* p2: GENLOCK FINE DELAY value */
#define VID_PLAY_NORMAL			2
#define VID_PLAY_PAUSE			3
#define VID_PLAY_SINGLE_FRAME		4
#define VID_PLAY_FAST_FORWARD		5
#define VID_PLAY_SLOW_MOTION		6
#define VID_PLAY_IMMEDIATE_NORMAL	7
#define VID_PLAY_SWITCH_CHANNELS	8
#define VID_PLAY_FREEZE_FRAME		9
#define VID_PLAY_STILL_MODE		10
#define VID_PLAY_MASTER_MODE		11
	/* p1: see below */
#define		VID_PLAY_MASTER_NONE	1
#define		VID_PLAY_MASTER_VIDEO	2
#define		VID_PLAY_MASTER_AUDIO	3
#define VID_PLAY_ACTIVE_SCANLINES	12
	/* p1 = first active; p2 = last active */
#define VID_PLAY_RESET			13
#define VID_PLAY_END_MARK		14

#endif /* CONFIG_VIDEO_V4L1_COMPAT */

#endif /* __LINUX_VIDEODEV_H */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
