/*	$OpenBSD: bt8xx.h,v 1.3 2005/06/23 14:57:48 robert Exp $	*/
/*	$NetBSD: bt8xx.h,v 1.4 2000/12/30 16:55:24 wiz Exp $	*/

/* This file is merged from ioctl_meteor.h and ioctl_bt848.h from FreeBSD. */
/* The copyright below only applies to the ioctl_meteor.h part of this file. */

#ifndef _DEV_IC_BT8XX_H_
#define _DEV_IC_BT8XX_H_
/*
 * Copyright (c) 1995 Mark Tinguely and Jim Lowe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Tinguely and Jim Lowe
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * FreeBSD: src/sys/i386/include/ioctl_meteor.h,v 1.11 1999/12/29 04:33:02 peter Exp
 */
/*
 *	ioctl constants for Matrox Meteor Capture card.
 */


#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

struct meteor_capframe {
	short	command;	/* see below for valid METEORCAPFRM commands */
	short	lowat;		/* start transfer if < this number */
	short	hiwat;		/* stop transfer if > this number */
} ;

/* structure for METEOR[GS]ETGEO - get/set geometry  */
struct meteor_geomet {
	u_short		rows;
	u_short		columns;
	u_short		frames;
	u_int		oformat;
} ;

/* structure for METEORGCOUNT-get count of frames, fifo errors and dma errors */
struct meteor_counts {
	u_int fifo_errors;	/* count of fifo errors since open */
	u_int dma_errors;	/* count of dma errors since open */
	u_int frames_captured;	/* count of frames captured since open */
	u_int even_fields_captured; /* count of even fields captured */
	u_int odd_fields_captured; /* count of odd fields captured */
} ;

/* structure for getting and setting direct transfers to vram */
struct meteor_video {
	u_int	addr;	/* Address of location to dma to */
	u_int	width;	/* Width of memory area */
	u_int	banksize;	/* Size of Vram bank */
	u_int	ramsize;	/* Size of Vram */
};

#define METEORCAPTUR _IOW('x', 1, int)			 /* capture a frame */
#define METEORCAPFRM _IOW('x', 2, struct meteor_capframe)  /* sync capture */
#define METEORSETGEO _IOW('x', 3, struct meteor_geomet)  /* set geometry */
#define METEORGETGEO _IOR('x', 4, struct meteor_geomet)  /* get geometry */
#define METEORSTATUS _IOR('x', 5, unsigned short)	/* get status */
#define METEORSHUE   _IOW('x', 6, signed char)		/* set hue */
#define METEORGHUE   _IOR('x', 6, signed char)		/* get hue */
#define METEORSFMT   _IOW('x', 7, unsigned int)		/* set format */
#define METEORGFMT   _IOR('x', 7, unsigned int)		/* get format */
#define METEORSINPUT _IOW('x', 8, unsigned int)		/* set input dev */
#define METEORGINPUT _IOR('x', 8, unsigned int)		/* get input dev */
#define	METEORSCHCV  _IOW('x', 9, unsigned char)	/* set uv gain */
#define	METEORGCHCV  _IOR('x', 9, unsigned char)	/* get uv gain */
#define	METEORSCOUNT _IOW('x',10, struct meteor_counts)
#define	METEORGCOUNT _IOR('x',10, struct meteor_counts)
#define METEORSFPS   _IOW('x',11, unsigned short)	/* set fps */
#define METEORGFPS   _IOR('x',11, unsigned short)	/* get fps */
#define METEORSSIGNAL _IOW('x', 12, unsigned int)	/* set signal */
#define METEORGSIGNAL _IOR('x', 12, unsigned int)	/* get signal */
#define	METEORSVIDEO _IOW('x', 13, struct meteor_video)	/* set video */
#define	METEORGVIDEO _IOR('x', 13, struct meteor_video)	/* get video */
#define	METEORSBRIG  _IOW('x', 14, unsigned char)	/* set brightness */
#define METEORGBRIG  _IOR('x', 14, unsigned char)	/* get brightness */
#define	METEORSCSAT  _IOW('x', 15, unsigned char)	/* set chroma sat */
#define METEORGCSAT  _IOR('x', 15, unsigned char)	/* get uv saturation */
#define	METEORSCONT  _IOW('x', 16, unsigned char)	/* set contrast */
#define	METEORGCONT  _IOR('x', 16, unsigned char)	/* get contrast */
#define METEORSBT254 _IOW('x', 17, unsigned short)	/* set Bt254 reg */
#define METEORGBT254 _IOR('x', 17, unsigned short)	/* get Bt254 reg */
#define METEORSHWS   _IOW('x', 18, unsigned char)	/* set hor start reg */
#define METEORGHWS   _IOR('x', 18, unsigned char)	/* get hor start reg */
#define METEORSVWS   _IOW('x', 19, unsigned char)	/* set vert start reg */
#define METEORGVWS   _IOR('x', 19, unsigned char)	/* get vert start reg */
#define	METEORSTS    _IOW('x', 20, unsigned char)	/* set time stamp */
#define	METEORGTS    _IOR('x', 20, unsigned char)	/* get time stamp */

#define	METEOR_STATUS_ID_MASK	0xf000	/* ID of 7196 */
#define	METEOR_STATUS_DIR	0x0800	/* Direction of Expansion port YUV */
#define	METEOR_STATUS_OEF	0x0200	/* Field detected: Even/Odd */
#define	METEOR_STATUS_SVP	0x0100	/* State of VRAM Port:inactive/active */
#define	METEOR_STATUS_STTC	0x0080	/* Time Constant: TV/VCR */
#define	METEOR_STATUS_HCLK	0x0040	/* Horiz PLL: locked/unlocked */
#define	METEOR_STATUS_FIDT	0x0020	/* Field detect: 50/60hz */
#define	METEOR_STATUS_ALTD	0x0002	/* Line alt: no line alt/line alt */
#define METEOR_STATUS_CODE	0x0001	/* Colour info: no colour/colour */

				/* METEORCAPTUR capture options */
#define METEOR_CAP_SINGLE	0x0001	/* capture one frame */
#define METEOR_CAP_CONTINOUS	0x0002	/* continuously capture */
#define METEOR_CAP_STOP_CONT	0x0004	/* stop the continuous capture */

				/* METEORCAPFRM capture commands */
#define METEOR_CAP_N_FRAMES	0x0001	/* capture N frames */
#define METEOR_CAP_STOP_FRAMES	0x0002	/* stop capture N frames */
#define	METEOR_HALT_N_FRAMES	0x0003	/* halt of capture N frames */
#define METEOR_CONT_N_FRAMES	0x0004	/* continue after above halt */

				/* valid video input formats:  */
#define METEOR_FMT_NTSC		0x00100	/* NTSC --  initialized default */
#define METEOR_FMT_PAL		0x00200	/* PAL */
#define METEOR_FMT_SECAM	0x00400	/* SECAM */
#define METEOR_FMT_AUTOMODE	0x00800 /* auto-mode */
#define METEOR_INPUT_DEV0	0x01000	/* camera input 0 -- default */
#define METEOR_INPUT_DEV_RCA	METEOR_INPUT_DEV0
#define METEOR_INPUT_DEV1	0x02000	/* camera input 1 */
#define METEOR_INPUT_DEV2	0x04000	/* camera input 2 */
#define METEOR_INPUT_DEV3	0x08000	/* camera input 3 */
#define METEOR_INPUT_DEV_RGB	0x0a000	/* for rgb version of meteor */
#define METEOR_INPUT_DEV_SVIDEO	0x06000 /* S-video input port */

				/* valid video output formats:  */
#define METEOR_GEO_RGB16	0x0010000 /* packed -- initialized default */
#define METEOR_GEO_RGB24	0x0020000 /* RBG 24 bits packed */
					  /* internally stored in 32 bits */
#define METEOR_GEO_YUV_PACKED	0x0040000 /* 4-2-2 YUV 16 bits packed */
#define METEOR_GEO_YUV_PLANAR	0x0080000 /* 4-2-2 YUV 16 bits planer */
#define METEOR_GEO_YUV_PLANER	METEOR_GEO_YUV_PLANAR
#define METEOR_GEO_UNSIGNED	0x0400000 /* unsigned uv outputs */
#define METEOR_GEO_EVEN_ONLY	0x1000000 /* set for even only field capture */
#define METEOR_GEO_ODD_ONLY	0x2000000 /* set for odd only field capture */
#define METEOR_GEO_FIELD_MASK	0x3000000
#define METEOR_GEO_YUV_422	0x4000000 /* 4-2-2 YUV in Y-U-V combined */
#define METEOR_GEO_OUTPUT_MASK	0x40f0000
#define METEOR_GEO_YUV_12	0x10000000	/* YUV 12 format */
#define METEOR_GEO_YUV_9	0x40000000	/* YUV 9 format */

#define	METEOR_FIELD_MODE	0x80000000	/* Field cap or Frame cap */

#define	METEOR_SIG_MODE_MASK	0xffff0000
#define	METEOR_SIG_FRAME	0x00000000	/* signal every frame */
#define	METEOR_SIG_FIELD	0x00010000	/* signal every field */

	/* following structure is used to coordinate the synchronous */
	   
struct meteor_mem {
		/* kernel write only  */
	int	frame_size;	 /* row*columns*depth */
	unsigned num_bufs;	 /* number of frames in buffer (1-32) */
		/* user and kernel change these */
	int	lowat;		 /* kernel starts capture if < this number */
	int	hiwat;		 /* kernel stops capture if > this number.
				    hiwat <= numbufs */
	unsigned active;	 /* bit mask of active frame buffers
				    kernel sets, user clears */
	int	num_active_bufs; /* count of active frame buffer
				    kernel increments, user decrements */

		/* reference to mmapped data */
	caddr_t	buf;		 /* The real space (virtual addr) */
} ;

/*
 * extensions to ioctl_meteor.h for the bt848 cards
 *
 * FreeBSD: src/sys/i386/include/ioctl_bt848.h,v 1.27 2000/10/26 16:41:48 roger Exp
 */


/*
 * frequency sets
 */
#define CHNLSET_NABCST		1
#define CHNLSET_CABLEIRC	2
#define CHNLSET_CABLEHRC	3
#define CHNLSET_WEUROPE		4
#define CHNLSET_JPNBCST         5
#define CHNLSET_JPNCABLE        6
#define CHNLSET_XUSSR           7
#define CHNLSET_AUSTRALIA       8
#define CHNLSET_FRANCE          9
#define CHNLSET_MIN	        CHNLSET_NABCST
#define CHNLSET_MAX	        CHNLSET_FRANCE


/*
 * constants for various tuner registers
 */
#define BT848_HUEMIN		(-90)
#define BT848_HUEMAX		90
#define BT848_HUECENTER		0
#define BT848_HUERANGE		179.3
#define BT848_HUEREGMIN		(-128)
#define BT848_HUEREGMAX		127
#define BT848_HUESTEPS		256

#define BT848_BRIGHTMIN		(-50)
#define BT848_BRIGHTMAX		50
#define BT848_BRIGHTCENTER	0
#define BT848_BRIGHTRANGE	99.6
#define BT848_BRIGHTREGMIN	(-128)
#define BT848_BRIGHTREGMAX	127
#define BT848_BRIGHTSTEPS	256

#define BT848_CONTRASTMIN	0
#define BT848_CONTRASTMAX	237
#define BT848_CONTRASTCENTER	100
#define BT848_CONTRASTRANGE	236.57
#define BT848_CONTRASTREGMIN	0
#define BT848_CONTRASTREGMAX	511
#define BT848_CONTRASTSTEPS	512

#define BT848_CHROMAMIN		0
#define BT848_CHROMAMAX		284
#define BT848_CHROMACENTER	100
#define BT848_CHROMARANGE	283.89
#define BT848_CHROMAREGMIN	0
#define BT848_CHROMAREGMAX	511
#define BT848_CHROMASTEPS	512

#define BT848_SATUMIN		0
#define BT848_SATUMAX		202
#define BT848_SATUCENTER	100
#define BT848_SATURANGE		201.18
#define BT848_SATUREGMIN	0
#define BT848_SATUREGMAX	511
#define BT848_SATUSTEPS		512

#define BT848_SATVMIN		0
#define BT848_SATVMAX		284
#define BT848_SATVCENTER	100
#define BT848_SATVRANGE		283.89
#define BT848_SATVREGMIN	0
#define BT848_SATVREGMAX	511
#define BT848_SATVSTEPS		512


/*
 * audio stuff
 */
#define AUDIO_TUNER		0x00	/* command for the audio routine */
#define AUDIO_EXTERN		0x01	/* don't confuse them with bit */
#define AUDIO_INTERN		0x02	/* settings */
#define AUDIO_MUTE		0x80
#define AUDIO_UNMUTE		0x81


/*
 * EEProm stuff
 */
struct eeProm {
	short	offset;
	short	count;
	u_char	bytes[ 256 ];
};


/*
 * XXX: this is a hack, should be in ioctl_meteor.h
 * here to avoid touching that file for now...
 */
#define	TVTUNER_SETCHNL    _IOW('x', 32, unsigned int)	/* set channel */
#define	TVTUNER_GETCHNL    _IOR('x', 32, unsigned int)	/* get channel */
#define	TVTUNER_SETTYPE    _IOW('x', 33, unsigned int)	/* set tuner type */
#define	TVTUNER_GETTYPE    _IOR('x', 33, unsigned int)	/* get tuner type */
#define	TVTUNER_GETSTATUS  _IOR('x', 34, unsigned int)	/* get tuner status */
#define	TVTUNER_SETFREQ    _IOW('x', 35, unsigned int)	/* set frequency */
#define	TVTUNER_GETFREQ    _IOR('x', 36, unsigned int)	/* get frequency */
 

#define BT848_SHUE	_IOW('x', 37, int)		/* set hue */
#define BT848_GHUE	_IOR('x', 37, int)		/* get hue */
#define	BT848_SBRIG	_IOW('x', 38, int)		/* set brightness */
#define BT848_GBRIG	_IOR('x', 38, int)		/* get brightness */
#define	BT848_SCSAT	_IOW('x', 39, int)		/* set chroma sat */
#define BT848_GCSAT	_IOR('x', 39, int)		/* get UV saturation */
#define	BT848_SCONT	_IOW('x', 40, int)		/* set contrast */
#define	BT848_GCONT	_IOR('x', 40, int)		/* get contrast */
#define	BT848_SVSAT	_IOW('x', 41, int)		/* set chroma V sat */
#define BT848_GVSAT	_IOR('x', 41, int)		/* get V saturation */
#define	BT848_SUSAT	_IOW('x', 42, int)		/* set chroma U sat */
#define BT848_GUSAT	_IOR('x', 42, int)		/* get U saturation */

#define	BT848_SCBARS	_IOR('x', 43, int)		/* set colorbar */
#define	BT848_CCBARS	_IOR('x', 44, int)		/* clear colorbar */


#define	BT848_SAUDIO	_IOW('x', 46, int)		/* set audio channel */
#define BT848_GAUDIO	_IOR('x', 47, int)		/* get audio channel */
#define	BT848_SBTSC	_IOW('x', 48, int)		/* set audio channel */

#define	BT848_GSTATUS	_IOR('x', 49, unsigned int)	/* reap status */

#define	BT848_WEEPROM	_IOWR('x', 50, struct eeProm)	/* write to EEProm */
#define	BT848_REEPROM	_IOWR('x', 51, struct eeProm)	/* read from EEProm */

#define	BT848_SIGNATURE	_IOWR('x', 52, struct eeProm)	/* read card sig */

#define	TVTUNER_SETAFC	_IOW('x', 53, int)		/* turn AFC on/off */
#define TVTUNER_GETAFC	_IOR('x', 54, int)		/* query AFC on/off */
#define BT848_SLNOTCH	_IOW('x', 55, int)		/* set luma notch */
#define BT848_GLNOTCH	_IOR('x', 56, int)		/* get luma notch */

/* Read/Write the BT848's I2C bus directly
 * b7-b0:    data (read/write)
 * b15-b8:   internal peripheral register (write)   
 * b23-b16:  i2c addr (write)
 * b31-b24:  1 = write, 0 = read 
 */
#define BT848_I2CWR     _IOWR('x', 57, u_int)    /* i2c read-write */

struct bktr_msp_control {
	unsigned char function;
	unsigned int  address;
	unsigned int  data;
};

#define BT848_MSP_RESET _IO('x', 76)				/* MSP chip reset */
#define BT848_MSP_READ  _IOWR('x', 77, struct bktr_msp_control)	/* MSP chip read */
#define BT848_MSP_WRITE _IOWR('x', 78, struct bktr_msp_control)	/* MSP chip write */

/* Support for radio tuner */
#define RADIO_SETMODE	 _IOW('x', 58, unsigned int)  /* set radio modes */
#define RADIO_GETMODE	 _IOR('x', 58, unsigned char)  /* get radio modes */
#define   RADIO_AFC	 0x01		/* These modes will probably not */
#define   RADIO_MONO	 0x02		/*  work on the FRxxxx. It does	 */
#define   RADIO_MUTE	 0x08		/*  work on the FMxxxx.	*/
#define RADIO_SETFREQ    _IOW('x', 59, unsigned int)  /* set frequency   */
#define RADIO_GETFREQ    _IOR('x', 59, unsigned int)  /* set frequency   */
 /*        Argument is frequency*100MHz  */

/*
 * XXX: more bad magic,
 *      we need to fix the METEORGINPUT to return something public
 *      duplicate them here for now...
 */
#define	METEOR_DEV0		0x00001000
#define	METEOR_DEV1		0x00002000
#define	METEOR_DEV2		0x00004000
#define	METEOR_DEV3		0x00008000
#define	METEOR_DEV_SVIDEO	0x00006000
/*
 * right now I don't know were to put these, but as they are suppose to be
 * a part of a common video capture interface, these should be relocated to
 * another place.  Probably most of the METEOR_xxx defines need to be
 * renamed and moved to a common header
 */

typedef enum { METEOR_PIXTYPE_RGB, METEOR_PIXTYPE_YUV,
	       METEOR_PIXTYPE_YUV_PACKED,
	       METEOR_PIXTYPE_YUV_12 } METEOR_PIXTYPE;


struct meteor_pixfmt {
	u_int          index;         /* Index in supported pixfmt list     */
	METEOR_PIXTYPE type;          /* What's the board gonna feed us     */
	u_int          Bpp;           /* Bytes per pixel                    */
	u_int          masks[3];      /* R,G,B or Y,U,V masks, respectively */
	unsigned       swap_bytes :1; /* Bytes  swapped within shorts       */
	unsigned       swap_shorts:1; /* Shorts swapped within longs        */
};


struct bktr_clip {
    int          x_min;
    int          x_max;
    int          y_min;
    int          y_max;
};

#define BT848_MAX_CLIP_NODE 100
struct _bktr_clip {
    struct bktr_clip x[BT848_MAX_CLIP_NODE];
};

/*
 * I'm using METEOR_xxx just because that will be common to other interface
 * and less of a surprise
 */
#define METEORSACTPIXFMT	_IOW('x', 64, int )
#define METEORGACTPIXFMT	_IOR('x', 64, int )
#define METEORGSUPPIXFMT	_IOWR('x', 65, struct meteor_pixfmt)

/* set clip list */
#define BT848SCLIP     _IOW('x', 66, struct _bktr_clip )
#define BT848GCLIP     _IOR('x', 66, struct _bktr_clip )


/* set input format */
#define BT848SFMT		_IOW('x', 67, unsigned int )
#define BT848GFMT		_IOR('x', 67, unsigned int )

/* set clear-buffer-on-start */
#define BT848SCBUF	_IOW('x', 68, int)
#define BT848GCBUF	_IOR('x', 68, int)

/* set capture area */
/* The capture area is the area of the video image which is grabbed */
/* Usually the capture area is 640x480 (768x576 PAL) pixels */
/* This area is then scaled to the dimensions the user requires */
/* using the METEORGEO ioctl */
/* However, the capture area could be 400x300 pixels from the top right */
/* corner of the video image */
struct bktr_capture_area {
   int      x_offset;
   int      y_offset;
   int      x_size;
   int      y_size;
};
#define BT848_SCAPAREA   _IOW('x', 69, struct bktr_capture_area)
#define BT848_GCAPAREA   _IOR('x', 69, struct bktr_capture_area)


/* Get channel Set */
#define BT848_MAX_CHNLSET_NAME_LEN 16
struct bktr_chnlset {
       short   index;
       short   max_channel;
       char    name[BT848_MAX_CHNLSET_NAME_LEN];
};
#define	TVTUNER_GETCHNLSET _IOWR('x', 70, struct bktr_chnlset)



/* Infra Red Remote Control */
struct bktr_remote {
       unsigned char data[3];
};
#define	REMOTE_GETKEY      _IOR('x', 71, struct bktr_remote)/*read the remote */
                                                            /*control receiver*/
                                                            /*returns raw data*/

 
/*
 * Direct access to GPIO pins. You must add BKTR_GPIO_ACCESS to your kernel
 * configuration file to use these 
 */
#define BT848_GPIO_SET_EN      _IOW('x', 72, int)      /* set gpio_out_en */
#define BT848_GPIO_GET_EN      _IOR('x', 73, int)      /* get gpio_out_en */
#define BT848_GPIO_SET_DATA    _IOW('x', 74, int)      /* set gpio_data */
#define BT848_GPIO_GET_DATA    _IOR('x', 75, int)      /* get gpio_data */



/*  XXX - Copied from /sys/pci/brktree_reg.h  */
#define BT848_IFORM_FORMAT              (0x7<<0)
# define BT848_IFORM_F_RSVD             (0x7)
# define BT848_IFORM_F_SECAM            (0x6)
# define BT848_IFORM_F_PALN             (0x5)
# define BT848_IFORM_F_PALM             (0x4)
# define BT848_IFORM_F_PALBDGHI         (0x3)
# define BT848_IFORM_F_NTSCJ            (0x2)
# define BT848_IFORM_F_NTSCM            (0x1)
# define BT848_IFORM_F_AUTO             (0x0)



/* XXX Silly constants not always defined by the environment.  */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


#endif /* _DEV_IC_BT8XX_H_ */
