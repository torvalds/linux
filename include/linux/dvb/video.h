/*
 * video.h
 *
 * Copyright (C) 2000 Marcus Metzler <marcus@convergence.de>
 *                  & Ralph  Metzler <ralph@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBVIDEO_H_
#define _DVBVIDEO_H_

#include <linux/compiler.h>

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <time.h>
#endif


typedef enum {
	VIDEO_FORMAT_4_3,     /* Select 4:3 format */
	VIDEO_FORMAT_16_9,    /* Select 16:9 format. */
	VIDEO_FORMAT_221_1    /* 2.21:1 */
} video_format_t;


typedef enum {
	 VIDEO_SYSTEM_PAL,
	 VIDEO_SYSTEM_NTSC,
	 VIDEO_SYSTEM_PALN,
	 VIDEO_SYSTEM_PALNc,
	 VIDEO_SYSTEM_PALM,
	 VIDEO_SYSTEM_NTSC60,
	 VIDEO_SYSTEM_PAL60,
	 VIDEO_SYSTEM_PALM60
} video_system_t;


typedef enum {
	VIDEO_PAN_SCAN,       /* use pan and scan format */
	VIDEO_LETTER_BOX,     /* use letterbox format */
	VIDEO_CENTER_CUT_OUT  /* use center cut out format */
} video_displayformat_t;

typedef struct {
	int w;
	int h;
	video_format_t aspect_ratio;
} video_size_t;

typedef enum {
	VIDEO_SOURCE_DEMUX, /* Select the demux as the main source */
	VIDEO_SOURCE_MEMORY /* If this source is selected, the stream
			       comes from the user through the write
			       system call */
} video_stream_source_t;


typedef enum {
	VIDEO_STOPPED, /* Video is stopped */
	VIDEO_PLAYING, /* Video is currently playing */
	VIDEO_FREEZED  /* Video is freezed */
} video_play_state_t;


/* Decoder commands */
#define VIDEO_CMD_PLAY        (0)
#define VIDEO_CMD_STOP        (1)
#define VIDEO_CMD_FREEZE      (2)
#define VIDEO_CMD_CONTINUE    (3)

/* Flags for VIDEO_CMD_FREEZE */
#define VIDEO_CMD_FREEZE_TO_BLACK     	(1 << 0)

/* Flags for VIDEO_CMD_STOP */
#define VIDEO_CMD_STOP_TO_BLACK      	(1 << 0)
#define VIDEO_CMD_STOP_IMMEDIATELY     	(1 << 1)

/* Play input formats: */
/* The decoder has no special format requirements */
#define VIDEO_PLAY_FMT_NONE         (0)
/* The decoder requires full GOPs */
#define VIDEO_PLAY_FMT_GOP          (1)

/* The structure must be zeroed before use by the application
   This ensures it can be extended safely in the future. */
struct video_command {
	__u32 cmd;
	__u32 flags;
	union {
		struct {
			__u64 pts;
		} stop;

		struct {
			/* 0 or 1000 specifies normal speed,
			   1 specifies forward single stepping,
			   -1 specifies backward single stepping,
			   >1: playback at speed/1000 of the normal speed,
			   <-1: reverse playback at (-speed/1000) of the normal speed. */
			__s32 speed;
			__u32 format;
		} play;

		struct {
			__u32 data[16];
		} raw;
	};
};

/* FIELD_UNKNOWN can be used if the hardware does not know whether
   the Vsync is for an odd, even or progressive (i.e. non-interlaced)
   field. */
#define VIDEO_VSYNC_FIELD_UNKNOWN  	(0)
#define VIDEO_VSYNC_FIELD_ODD 		(1)
#define VIDEO_VSYNC_FIELD_EVEN		(2)
#define VIDEO_VSYNC_FIELD_PROGRESSIVE	(3)

struct video_event {
	int32_t type;
#define VIDEO_EVENT_SIZE_CHANGED	1
#define VIDEO_EVENT_FRAME_RATE_CHANGED	2
#define VIDEO_EVENT_DECODER_STOPPED 	3
#define VIDEO_EVENT_VSYNC 		4
	time_t timestamp;
	union {
		video_size_t size;
		unsigned int frame_rate;	/* in frames per 1000sec */
		unsigned char vsync_field;	/* unknown/odd/even/progressive */
	} u;
};


struct video_status {
	int                   video_blank;   /* blank video on freeze? */
	video_play_state_t    play_state;    /* current state of playback */
	video_stream_source_t stream_source; /* current source (demux/memory) */
	video_format_t        video_format;  /* current aspect ratio of stream*/
	video_displayformat_t display_format;/* selected cropping mode */
};


struct video_still_picture {
	char __user *iFrame;        /* pointer to a single iframe in memory */
	int32_t size;
};


typedef
struct video_highlight {
	int     active;      /*    1=show highlight, 0=hide highlight */
	uint8_t contrast1;   /*    7- 4  Pattern pixel contrast */
			     /*    3- 0  Background pixel contrast */
	uint8_t contrast2;   /*    7- 4  Emphasis pixel-2 contrast */
			     /*    3- 0  Emphasis pixel-1 contrast */
	uint8_t color1;      /*    7- 4  Pattern pixel color */
			     /*    3- 0  Background pixel color */
	uint8_t color2;      /*    7- 4  Emphasis pixel-2 color */
			     /*    3- 0  Emphasis pixel-1 color */
	uint32_t ypos;       /*   23-22  auto action mode */
			     /*   21-12  start y */
			     /*    9- 0  end y */
	uint32_t xpos;       /*   23-22  button color number */
			     /*   21-12  start x */
			     /*    9- 0  end x */
} video_highlight_t;


typedef struct video_spu {
	int active;
	int stream_id;
} video_spu_t;


typedef struct video_spu_palette {      /* SPU Palette information */
	int length;
	uint8_t __user *palette;
} video_spu_palette_t;


typedef struct video_navi_pack {
	int length;          /* 0 ... 1024 */
	uint8_t data[1024];
} video_navi_pack_t;


typedef uint16_t video_attributes_t;
/*   bits: descr. */
/*   15-14 Video compression mode (0=MPEG-1, 1=MPEG-2) */
/*   13-12 TV system (0=525/60, 1=625/50) */
/*   11-10 Aspect ratio (0=4:3, 3=16:9) */
/*    9- 8 permitted display mode on 4:3 monitor (0=both, 1=only pan-sca */
/*    7    line 21-1 data present in GOP (1=yes, 0=no) */
/*    6    line 21-2 data present in GOP (1=yes, 0=no) */
/*    5- 3 source resolution (0=720x480/576, 1=704x480/576, 2=352x480/57 */
/*    2    source letterboxed (1=yes, 0=no) */
/*    0    film/camera mode (0=camera, 1=film (625/50 only)) */


/* bit definitions for capabilities: */
/* can the hardware decode MPEG1 and/or MPEG2? */
#define VIDEO_CAP_MPEG1   1
#define VIDEO_CAP_MPEG2   2
/* can you send a system and/or program stream to video device?
   (you still have to open the video and the audio device but only
    send the stream to the video device) */
#define VIDEO_CAP_SYS     4
#define VIDEO_CAP_PROG    8
/* can the driver also handle SPU, NAVI and CSS encoded data?
   (CSS API is not present yet) */
#define VIDEO_CAP_SPU    16
#define VIDEO_CAP_NAVI   32
#define VIDEO_CAP_CSS    64


#define VIDEO_STOP                 _IO('o', 21)
#define VIDEO_PLAY                 _IO('o', 22)
#define VIDEO_FREEZE               _IO('o', 23)
#define VIDEO_CONTINUE             _IO('o', 24)
#define VIDEO_SELECT_SOURCE        _IO('o', 25)
#define VIDEO_SET_BLANK            _IO('o', 26)
#define VIDEO_GET_STATUS           _IOR('o', 27, struct video_status)
#define VIDEO_GET_EVENT            _IOR('o', 28, struct video_event)
#define VIDEO_SET_DISPLAY_FORMAT   _IO('o', 29)
#define VIDEO_STILLPICTURE         _IOW('o', 30, struct video_still_picture)
#define VIDEO_FAST_FORWARD         _IO('o', 31)
#define VIDEO_SLOWMOTION           _IO('o', 32)
#define VIDEO_GET_CAPABILITIES     _IOR('o', 33, unsigned int)
#define VIDEO_CLEAR_BUFFER         _IO('o',  34)
#define VIDEO_SET_ID               _IO('o', 35)
#define VIDEO_SET_STREAMTYPE       _IO('o', 36)
#define VIDEO_SET_FORMAT           _IO('o', 37)
#define VIDEO_SET_SYSTEM           _IO('o', 38)
#define VIDEO_SET_HIGHLIGHT        _IOW('o', 39, video_highlight_t)
#define VIDEO_SET_SPU              _IOW('o', 50, video_spu_t)
#define VIDEO_SET_SPU_PALETTE      _IOW('o', 51, video_spu_palette_t)
#define VIDEO_GET_NAVI             _IOR('o', 52, video_navi_pack_t)
#define VIDEO_SET_ATTRIBUTES       _IO('o', 53)
#define VIDEO_GET_SIZE             _IOR('o', 55, video_size_t)
#define VIDEO_GET_FRAME_RATE       _IOR('o', 56, unsigned int)

/**
 * VIDEO_GET_PTS
 *
 * Read the 33 bit presentation time stamp as defined
 * in ITU T-REC-H.222.0 / ISO/IEC 13818-1.
 *
 * The PTS should belong to the currently played
 * frame if possible, but may also be a value close to it
 * like the PTS of the last decoded frame or the last PTS
 * extracted by the PES parser.
 */
#define VIDEO_GET_PTS              _IOR('o', 57, __u64)

/* Read the number of displayed frames since the decoder was started */
#define VIDEO_GET_FRAME_COUNT  	   _IOR('o', 58, __u64)

#define VIDEO_COMMAND     	   _IOWR('o', 59, struct video_command)
#define VIDEO_TRY_COMMAND 	   _IOWR('o', 60, struct video_command)

#endif /*_DVBVIDEO_H_*/
