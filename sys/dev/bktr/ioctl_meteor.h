/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 * $FreeBSD$
 */
/*
 *	ioctl constants for Matrox Meteor Capture card.
 */

#ifndef	_DEV_BKTR_IOCTL_METEOR_H_
#define	_DEV_BKTR_IOCTL_METEOR_H_

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
	unsigned short		rows;
	unsigned short		columns;
	unsigned short		frames;
	unsigned long		oformat;
} ;

/* structure for METEORGCOUNT-get count of frames, fifo errors and dma errors */
struct meteor_counts {
	unsigned long fifo_errors;	/* count of fifo errors since open */
	unsigned long dma_errors;	/* count of dma errors since open */
	unsigned long frames_captured;	/* count of frames captured since open */
	unsigned long even_fields_captured; /* count of even fields captured */
	unsigned long odd_fields_captured; /* count of odd fields captured */
} ;

/* structure for getting and setting direct transfers to vram */
struct meteor_video {
	unsigned long	addr;		/* Address of location to dma to */
	unsigned long	width;		/* Width of memory area */
	unsigned long	banksize;	/* Size of Vram bank */
	unsigned long	ramsize;	/* Size of Vram */
};

#define METEORCAPTUR _IOW('x', 1, int)			 /* capture a frame */
#define METEORCAPFRM _IOW('x', 2, struct meteor_capframe)  /* sync capture */
#define METEORSETGEO _IOW('x', 3, struct meteor_geomet)  /* set geometry */
#define METEORGETGEO _IOR('x', 4, struct meteor_geomet)  /* get geometry */
#define METEORSTATUS _IOR('x', 5, unsigned short)	/* get status */
#define METEORSHUE   _IOW('x', 6, signed char)		/* set hue */
#define METEORGHUE   _IOR('x', 6, signed char)		/* get hue */
#define METEORSFMT   _IOW('x', 7, unsigned long)	/* set format */
#define METEORGFMT   _IOR('x', 7, unsigned long)	/* get format */
#define METEORSINPUT _IOW('x', 8, unsigned long)	/* set input dev */
#define METEORGINPUT _IOR('x', 8, unsigned long)	/* get input dev */
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

#endif /* !_DEV_BKTR_IOCTL_METEOR_H_ */
