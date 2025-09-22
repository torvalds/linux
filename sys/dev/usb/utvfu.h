/*	$OpenBSD: utvfu.h,v 1.6 2025/01/12 16:39:39 mglocker Exp $ */
/*
 * Copyright (c) 2013 Lubomir Rintel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Fushicai USBTV007 Audio-Video Grabber Driver
 *
 * Product web site:
 * http://www.fushicai.com/products_detail/&productId=d05449ee-b690-42f9-a661-aa7353894bed.html
 *
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 */

#ifndef _UTVFU_H_
#define _UTVFU_H_

#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/audioio.h>
#include <sys/videoio.h>

/* Hardware. */
#define UTVFU_VIDEO_ENDP	0x81
#define UTVFU_AUDIO_ENDP	0x83
#define UTVFU_BASE		0xc000
#define UTVFU_REQUEST_REG	12

#define UTVFU_DFLT_IFACE_IDX	0
#define UTVFU_ALT_IFACE_IDX	1

/*
 * Number of concurrent isochronous urbs submitted.
 * Higher numbers was seen to overly saturate the USB bus.
 */
#define UTVFU_ISOC_TRANSFERS	3

#define UTVFU_CHUNK_SIZE	256
#define UTVFU_CHUNK		240

#define UTVFU_AUDIO_URBSIZE	20480
#define UTVFU_AUDIO_HDRSIZE	4
#define UTVFU_AUDIO_BUFFER	65536

#define UTVFU_COMPOSITE_INPUT	0
#define UTVFU_SVIDEO_INPUT	1

/* Chunk header. */
#define UTVFU_MAGIC(hdr)	 (hdr & 0xff000000U)
#define UTVFU_MAGIC_OK(hdr)	((hdr & 0xff000000U) == 0x88000000U)
#define UTVFU_FRAME_ID(hdr)	((hdr & 0x00ff0000U) >> 16)
#define UTVFU_ODD(hdr)		((hdr & 0x0000f000U) >> 15)
#define UTVFU_CHUNK_NO(hdr)	 (hdr & 0x00000fffU)

#define UTVFU_TV_STD		(V4L2_STD_525_60 | V4L2_STD_PAL)

/* parameters for supported TV norms */
struct utvfu_norm_params {
	v4l2_std_id	norm;
	int		cap_width;
	int		cap_height;
	int		frame_len;
};

#define UTVFU_MAX_BUFFERS	32
struct utvfu_mmap {
	SIMPLEQ_ENTRY(utvfu_mmap)	q_frames;
	uint8_t				*buf;
	struct v4l2_buffer		v4l2_buf;
};
typedef SIMPLEQ_HEAD(, utvfu_mmap)	q_mmap;

struct utvfu_frame_buf {
	uint		off;
	uint		size;
	uint16_t	chunks_done;
	uint8_t		fid; 
	uint8_t		last_odd;
	uint8_t		*buf;
};

#define UTVFU_NFRAMES_MAX	40
struct utvfu_isoc_xfer {
	struct utvfu_softc	*sc;
	struct usbd_xfer	*xfer;
	uint16_t		size[UTVFU_NFRAMES_MAX];
};

struct utvfu_vs_iface {
	struct usbd_pipe	*pipeh;
	uint32_t		psize;
	struct utvfu_isoc_xfer	ixfer[UTVFU_ISOC_TRANSFERS];
};

struct utvfu_as_iface {
	struct usbd_pipe	*pipeh;
	struct usbd_xfer	*xfer;
};

struct utvfu_audio_chan {
	uint8_t			*start;
	uint8_t			*end;
	uint8_t			*cur;
	int			blksize;
	void			*intr_arg;
	void			(*intr)(void *);
	struct utvfu_as_iface	iface;
	struct rwlock		rwlock;
};

/* Per-device structure. */
struct utvfu_softc {
	struct device		sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_uifaceh;

	/* audio & video device */
	struct device		*sc_audiodev;
	struct device		*sc_videodev;

	int			sc_flags;
#define UTVFU_FLAG_MMAP		0x01
#define UTVFU_FLAG_AS_RUNNING	0x02

	int			sc_normi;
	int			sc_nchunks;
	int			sc_input;
	int			sc_max_frame_sz;
	int			sc_nframes;

	struct utvfu_vs_iface	sc_iface;
	struct utvfu_frame_buf	sc_fb;

	struct utvfu_audio_chan	sc_audio;

	/* mmap */
	struct utvfu_mmap	sc_mmap[UTVFU_MAX_BUFFERS];
	uint8_t			*sc_mmap_buffer;
	q_mmap			sc_mmap_q;
	int			sc_mmap_bufsz;
	int			sc_mmap_count;

	/* uplayer */
	void			*sc_uplayer_arg;
	int			*sc_uplayer_fsize;
	uint8_t			*sc_uplayer_fbuffer;
	void			(*sc_uplayer_intr)(void *);
};

int	utvfu_max_frame_size(void);
int	utvfu_set_regs(struct utvfu_softc *, const uint16_t regs[][2], int);
void	utvfu_image_chunk(struct utvfu_softc *, u_char *);
int	utvfu_configure_for_norm(struct utvfu_softc *, v4l2_std_id);
int	utvfu_start_capture(struct utvfu_softc *);
int	utvfu_mmap_queue(struct utvfu_softc *, uint8_t *, int);
void	utvfu_read(struct utvfu_softc *, uint8_t *, int);

void	utvfu_audio_decode(struct utvfu_softc *, int);
int	utvfu_audio_start(struct utvfu_softc *);
int	utvfu_audio_stop(struct utvfu_softc *);
int	utvfu_audio_start_chip(struct utvfu_softc *);
int	utvfu_audio_stop_chip(struct utvfu_softc *);

#endif
