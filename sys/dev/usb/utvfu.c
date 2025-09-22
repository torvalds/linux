/*	$OpenBSD: utvfu.c,v 1.23 2025/02/25 22:10:39 kirill Exp $ */
/*
 * Copyright (c) 2013 Lubomir Rintel
 * Copyright (c) 2013 Federico Simoncelli
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
 * Following LWN articles were very useful in construction of this driver:
 * Video4Linux2 API series: http://lwn.net/Articles/203924/
 * videobuf2 API explanation: http://lwn.net/Articles/447435/
 * Thanks go to Jonathan Corbet for providing this quality documentation.
 * He is awesome.
 *
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/utvfu.h>

#include <dev/audio_if.h>
#include <dev/video_if.h>

#ifdef UTVFU_DEBUG
int utvfu_debug = 1;
#define DPRINTF(l, x...) do { if ((l) <= utvfu_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

struct utvfu_norm_params utvfu_norm_params[] = {
	{
		.norm = V4L2_STD_525_60,
		.cap_width = 720,
		.cap_height = 480,
		/* 4 bytes/2 pixel YUYV/YUV 4:2:2 */
		.frame_len = (720 * 480 * 2),
	},
	{
		.norm = V4L2_STD_PAL,
		.cap_width = 720,
		.cap_height = 576,
		/* 4 bytes/2 pixel YUYV/YUV 4:2:2 */
		.frame_len = (720 * 576 * 2),
	}
};

int
utvfu_set_regs(struct utvfu_softc *sc, const uint16_t regs[][2], int size)
{
	int i;
	usbd_status error;
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTVFU_REQUEST_REG;
	USETW(req.wLength, 0);

	for (i = 0; i < size; i++) {
		USETW(req.wIndex, regs[i][0]);
		USETW(req.wValue, regs[i][1]);

		error = usbd_do_request(sc->sc_udev, &req, NULL);
		if (error != USBD_NORMAL_COMPLETION) {
			DPRINTF(1, "%s: %s: exit EINVAL\n",
			    DEVNAME(sc), __func__);
			return (EINVAL);
		}
	}

	return (0);
}

int
utvfu_max_frame_size(void)
{
	int i, sz = 0;

	for (i = 0; i < nitems(utvfu_norm_params); i++) {
		if (sz < utvfu_norm_params[i].frame_len)
			sz = utvfu_norm_params[i].frame_len;
	}

	return (sz);
}

int
utvfu_configure_for_norm(struct utvfu_softc *sc, v4l2_std_id norm)
{
	int i, ret = EINVAL;
	struct utvfu_norm_params *params = NULL;

	for (i = 0; i < nitems(utvfu_norm_params); i++) {
		if (utvfu_norm_params[i].norm & norm) {
			params = &utvfu_norm_params[i];
			break;
		}
	}

	if (params != NULL) {
		sc->sc_normi = i;
		sc->sc_nchunks = params->cap_width * params->cap_height
		    / 4 / UTVFU_CHUNK;
		ret = 0;
	}

	return (ret);
}

int
utvfu_select_input(struct utvfu_softc *sc, int input)
{
	int ret;

	static const uint16_t composite[][2] = {
		{ UTVFU_BASE + 0x0105, 0x0060 },
		{ UTVFU_BASE + 0x011f, 0x00f2 },
		{ UTVFU_BASE + 0x0127, 0x0060 },
		{ UTVFU_BASE + 0x00ae, 0x0010 },
		{ UTVFU_BASE + 0x0239, 0x0060 },
	};

	static const uint16_t svideo[][2] = {
		{ UTVFU_BASE + 0x0105, 0x0010 },
		{ UTVFU_BASE + 0x011f, 0x00ff },
		{ UTVFU_BASE + 0x0127, 0x0060 },
		{ UTVFU_BASE + 0x00ae, 0x0030 },
		{ UTVFU_BASE + 0x0239, 0x0060 },
	};

	switch (input) {
	case UTVFU_COMPOSITE_INPUT:
		ret = utvfu_set_regs(sc, composite, nitems(composite));
		break;
	case UTVFU_SVIDEO_INPUT:
		ret = utvfu_set_regs(sc, svideo, nitems(svideo));
		break;
	default:
		ret = EINVAL;
	}

	if (ret == 0)
		sc->sc_input = input;

	return (ret);
}

int
utvfu_select_norm(struct utvfu_softc *sc, v4l2_std_id norm)
{
	int ret;
	static const uint16_t pal[][2] = {
		{ UTVFU_BASE + 0x001a, 0x0068 },
		{ UTVFU_BASE + 0x010e, 0x0072 },
		{ UTVFU_BASE + 0x010f, 0x00a2 },
		{ UTVFU_BASE + 0x0112, 0x00b0 },
		{ UTVFU_BASE + 0x0117, 0x0001 },
		{ UTVFU_BASE + 0x0118, 0x002c },
		{ UTVFU_BASE + 0x012d, 0x0010 },
		{ UTVFU_BASE + 0x012f, 0x0020 },
		{ UTVFU_BASE + 0x024f, 0x0002 },
		{ UTVFU_BASE + 0x0254, 0x0059 },
		{ UTVFU_BASE + 0x025a, 0x0016 },
		{ UTVFU_BASE + 0x025b, 0x0035 },
		{ UTVFU_BASE + 0x0263, 0x0017 },
		{ UTVFU_BASE + 0x0266, 0x0016 },
		{ UTVFU_BASE + 0x0267, 0x0036 }
	};

	static const uint16_t ntsc[][2] = {
		{ UTVFU_BASE + 0x001a, 0x0079 },
		{ UTVFU_BASE + 0x010e, 0x0068 },
		{ UTVFU_BASE + 0x010f, 0x009c },
		{ UTVFU_BASE + 0x0112, 0x00f0 },
		{ UTVFU_BASE + 0x0117, 0x0000 },
		{ UTVFU_BASE + 0x0118, 0x00fc },
		{ UTVFU_BASE + 0x012d, 0x0004 },
		{ UTVFU_BASE + 0x012f, 0x0008 },
		{ UTVFU_BASE + 0x024f, 0x0001 },
		{ UTVFU_BASE + 0x0254, 0x005f },
		{ UTVFU_BASE + 0x025a, 0x0012 },
		{ UTVFU_BASE + 0x025b, 0x0001 },
		{ UTVFU_BASE + 0x0263, 0x001c },
		{ UTVFU_BASE + 0x0266, 0x0011 },
		{ UTVFU_BASE + 0x0267, 0x0005 }
	};

	ret = utvfu_configure_for_norm(sc, norm);

	if (ret == 0) {
		if (norm & V4L2_STD_525_60)
			ret = utvfu_set_regs(sc, ntsc, nitems(ntsc));
		else if (norm & V4L2_STD_PAL)
			ret = utvfu_set_regs(sc, pal, nitems(pal));
	}

	return (ret);
}

int
utvfu_setup_capture(struct utvfu_softc *sc)
{
	int ret;
	static const uint16_t setup[][2] = {
		/* These seem to enable the device. */
		{ UTVFU_BASE + 0x0008, 0x0001 },
		{ UTVFU_BASE + 0x01d0, 0x00ff },
		{ UTVFU_BASE + 0x01d9, 0x0002 },

		/*
		 * These seem to influence color parameters, such as
		 * brightness, etc.
		 */
		{ UTVFU_BASE + 0x0239, 0x0040 },
		{ UTVFU_BASE + 0x0240, 0x0000 },
		{ UTVFU_BASE + 0x0241, 0x0000 },
		{ UTVFU_BASE + 0x0242, 0x0002 },
		{ UTVFU_BASE + 0x0243, 0x0080 },
		{ UTVFU_BASE + 0x0244, 0x0012 },
		{ UTVFU_BASE + 0x0245, 0x0090 },
		{ UTVFU_BASE + 0x0246, 0x0000 },

		{ UTVFU_BASE + 0x0278, 0x002d },
		{ UTVFU_BASE + 0x0279, 0x000a },
		{ UTVFU_BASE + 0x027a, 0x0032 },
		{ 0xf890, 0x000c },
		{ 0xf894, 0x0086 },

		{ UTVFU_BASE + 0x00ac, 0x00c0 },
		{ UTVFU_BASE + 0x00ad, 0x0000 },
		{ UTVFU_BASE + 0x00a2, 0x0012 },
		{ UTVFU_BASE + 0x00a3, 0x00e0 },
		{ UTVFU_BASE + 0x00a4, 0x0028 },
		{ UTVFU_BASE + 0x00a5, 0x0082 },
		{ UTVFU_BASE + 0x00a7, 0x0080 },
		{ UTVFU_BASE + 0x0000, 0x0014 },
		{ UTVFU_BASE + 0x0006, 0x0003 },
		{ UTVFU_BASE + 0x0090, 0x0099 },
		{ UTVFU_BASE + 0x0091, 0x0090 },
		{ UTVFU_BASE + 0x0094, 0x0068 },
		{ UTVFU_BASE + 0x0095, 0x0070 },
		{ UTVFU_BASE + 0x009c, 0x0030 },
		{ UTVFU_BASE + 0x009d, 0x00c0 },
		{ UTVFU_BASE + 0x009e, 0x00e0 },
		{ UTVFU_BASE + 0x0019, 0x0006 },
		{ UTVFU_BASE + 0x008c, 0x00ba },
		{ UTVFU_BASE + 0x0101, 0x00ff },
		{ UTVFU_BASE + 0x010c, 0x00b3 },
		{ UTVFU_BASE + 0x01b2, 0x0080 },
		{ UTVFU_BASE + 0x01b4, 0x00a0 },
		{ UTVFU_BASE + 0x014c, 0x00ff },
		{ UTVFU_BASE + 0x014d, 0x00ca },
		{ UTVFU_BASE + 0x0113, 0x0053 },
		{ UTVFU_BASE + 0x0119, 0x008a },
		{ UTVFU_BASE + 0x013c, 0x0003 },
		{ UTVFU_BASE + 0x0150, 0x009c },
		{ UTVFU_BASE + 0x0151, 0x0071 },
		{ UTVFU_BASE + 0x0152, 0x00c6 },
		{ UTVFU_BASE + 0x0153, 0x0084 },
		{ UTVFU_BASE + 0x0154, 0x00bc },
		{ UTVFU_BASE + 0x0155, 0x00a0 },
		{ UTVFU_BASE + 0x0156, 0x00a0 },
		{ UTVFU_BASE + 0x0157, 0x009c },
		{ UTVFU_BASE + 0x0158, 0x001f },
		{ UTVFU_BASE + 0x0159, 0x0006 },
		{ UTVFU_BASE + 0x015d, 0x0000 },

		{ UTVFU_BASE + 0x0003, 0x0004 },
		{ UTVFU_BASE + 0x0100, 0x00d3 },
		{ UTVFU_BASE + 0x0115, 0x0015 },
		{ UTVFU_BASE + 0x0220, 0x002e },
		{ UTVFU_BASE + 0x0225, 0x0008 },
		{ UTVFU_BASE + 0x024e, 0x0002 },
		{ UTVFU_BASE + 0x024e, 0x0002 },
		{ UTVFU_BASE + 0x024f, 0x0002 },
	};

	ret = utvfu_set_regs(sc, setup, nitems(setup));
	if (ret)
		return (ret);

	ret = utvfu_select_norm(sc, utvfu_norm_params[sc->sc_normi].norm);
	if (ret)
		return (ret);

	ret = utvfu_select_input(sc, sc->sc_input);
	if (ret)
		return (ret);

	return (0);
}

/*
 * Copy data from chunk into a frame buffer, deinterlacing the data
 * into every second line. Unfortunately, they don't align nicely into
 * 720 pixel lines, as the chunk is 240 words long, which is 480 pixels.
 * Therefore, we break down the chunk into two halves before copying,
 * so that we can interleave a line if needed.
 *
 * Each "chunk" is 240 words; a word in this context equals 4 bytes.
 * Image format is YUYV/YUV 4:2:2, consisting of Y Cr Y Cb, defining two
 * pixels, the Cr and Cb shared between the two pixels, but each having
 * separate Y values. Thus, the 240 words equal 480 pixels. It therefore,
 * takes 1.5 chunks to make a 720 pixel-wide line for the frame.
 * The image is interlaced, so there is a "scan" of odd lines, followed
 * by "scan" of even numbered lines.
 *
 * Following code is writing the chunks in correct sequence, skipping
 * the rows based on "odd" value.
 * line 1: chunk[0][  0..479] chunk[0][480..959] chunk[1][  0..479]
 * line 3: chunk[1][480..959] chunk[2][  0..479] chunk[2][480..959]
 * ...etc
 */
void
utvfu_chunk_to_vbuf(uint8_t *frame, uint8_t *src, int chunk_no, int odd)
{
	uint8_t *dst;
	int half, line, part_no, part_index;
	#define	UTVFU_STRIDE	(UTVFU_CHUNK/2 * 4)

	for (half = 0; half < 2; half++) {
		part_no = chunk_no * 2 + half;
		line = part_no / 3;
		part_index = (line * 2 + !odd) * 3 + (part_no % 3);

		dst = &frame[part_index * UTVFU_STRIDE];

		memcpy(dst, src, UTVFU_STRIDE);
		src += UTVFU_STRIDE;
	}
	#undef	UTVFU_STRIDE
}

/*
 * Called for each 256-byte image chunk.
 * First word identifies the chunk, followed by 240 words of image
 * data and padding.
 */
void
utvfu_image_chunk(struct utvfu_softc *sc, u_char *chunk)
{
	int frame_id, odd, chunk_no, frame_len;
	uint32_t hdr;

	memcpy(&hdr, chunk, sizeof(hdr));
	chunk += sizeof(hdr);
	hdr = be32toh(hdr);

	/* Ignore corrupted lines. */
	if (!UTVFU_MAGIC_OK(hdr)) {
		DPRINTF(2, "%s: bad magic=0x%08x\n",
		    DEVNAME(sc), UTVFU_MAGIC(hdr));
		return;
	}

	frame_id = UTVFU_FRAME_ID(hdr);
	odd = UTVFU_ODD(hdr);
	chunk_no = UTVFU_CHUNK_NO(hdr);
	if (chunk_no >= sc->sc_nchunks) {
		DPRINTF(2, "%s: chunk_no=%d >= sc_nchunks=%d\n",
		    DEVNAME(sc), chunk_no, sc->sc_nchunks);
		return;
	}

	/* Beginning of a frame. */
	if (chunk_no == 0) {
		sc->sc_fb.fid = frame_id;
		sc->sc_fb.chunks_done = 0;
	}
	else if (sc->sc_fb.fid != frame_id) {
		DPRINTF(2, "%s: frame id mismatch expecting=%d got=%d\n",
		    DEVNAME(sc), sc->sc_fb.fid, frame_id);
		return;
	}

	frame_len = utvfu_norm_params[sc->sc_normi].frame_len;

	/* Copy the chunk data. */
	utvfu_chunk_to_vbuf(sc->sc_fb.buf, chunk, chunk_no, odd);
	sc->sc_fb.chunks_done++;

	/* Last chunk in a field */
	if (chunk_no == sc->sc_nchunks-1) {
		/* Last chunk in a frame, signalling an end */
		if (odd && !sc->sc_fb.last_odd) {
			if (sc->sc_fb.chunks_done != sc->sc_nchunks) {
				DPRINTF(1, "%s: chunks_done=%d != nchunks=%d\n",
				    DEVNAME(sc),
				    sc->sc_fb.chunks_done, sc->sc_nchunks);
			}

			if (sc->sc_flags & UTVFU_FLAG_MMAP) {
				utvfu_mmap_queue(sc, sc->sc_fb.buf, frame_len);
			}
			else {
				utvfu_read(sc, sc->sc_fb.buf, frame_len);
			}
		}
		sc->sc_fb.last_odd = odd;
	}
}

int
utvfu_start_capture(struct utvfu_softc *sc)
{
	usbd_status error;
	int restart_au;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	restart_au = ISSET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING);
	utvfu_audio_stop(sc);

	/* default video stream interface */
	error = usbd_set_interface(sc->sc_uifaceh, UTVFU_DFLT_IFACE_IDX);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	if (utvfu_setup_capture(sc) != 0)
		return (EINVAL);

	/* alt setting */
	error = usbd_set_interface(sc->sc_uifaceh, UTVFU_ALT_IFACE_IDX);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	if (restart_au)
		utvfu_audio_start(sc);

	return (0);
}

int
utvfu_querycap(void *v, struct v4l2_capability *cap)
{
	struct utvfu_softc *sc = v;

	memset(cap, 0, sizeof(*cap));
	strlcpy(cap->driver, DEVNAME(sc), sizeof(cap->driver));
	strlcpy(cap->card, "utvfu", sizeof(cap->card));
	strlcpy(cap->bus_info, "usb", sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE;
	cap->device_caps |= V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return (0);
}

int
utvfu_enum_input(void *v, struct v4l2_input *i)
{
	struct utvfu_softc *sc = v;

	switch (i->index) {
	case UTVFU_COMPOSITE_INPUT:
		strlcpy(i->name, "Composite", sizeof(i->name));
		break;
	case UTVFU_SVIDEO_INPUT:
		strlcpy(i->name, "S-Video", sizeof(i->name));
		break;
	default:
		return (EINVAL);
	}

	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = utvfu_norm_params[sc->sc_normi].norm;

	return (0);
}

int
utvfu_enum_fmt_vid_cap(void *v, struct v4l2_fmtdesc *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || f->index != 0)
		return (EINVAL);

	strlcpy(f->description, "16 bpp YUY2, 4:2:2, packed",
					sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_YUYV;

	return (0);
}

int
utvfu_enum_fsizes(void *v, struct v4l2_frmsizeenum *fsizes)
{
	struct utvfu_softc *sc = v;

	if (fsizes->pixel_format != V4L2_PIX_FMT_YUYV)
		return (EINVAL);

	/* The device only supports one frame size. */
	if (fsizes->index >= 1)
		return (EINVAL);

	fsizes->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsizes->discrete.width = utvfu_norm_params[sc->sc_normi].cap_width;
	fsizes->discrete.height = utvfu_norm_params[sc->sc_normi].cap_height;

	return (0);
}

int
utvfu_g_fmt(void *v, struct v4l2_format *f)
{
	struct utvfu_softc *sc = v;

	f->fmt.pix.width = utvfu_norm_params[sc->sc_normi].cap_width;
	f->fmt.pix.height = utvfu_norm_params[sc->sc_normi].cap_height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = (f->fmt.pix.bytesperline * f->fmt.pix.height);
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return (0);
}

int
utvfu_s_fmt(void *v, struct v4l2_format *f)
{
	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)
		return (EINVAL);

	return (0);
}

int
utvfu_g_std(void *v, v4l2_std_id *norm)
{
	struct utvfu_softc *sc = v;

	*norm = utvfu_norm_params[sc->sc_normi].norm;

	return (0);
}

int
utvfu_s_std(void *v, v4l2_std_id norm)
{
	int ret = EINVAL;
	struct utvfu_softc *sc = v;

	if ((norm & V4L2_STD_525_60) || (norm & V4L2_STD_PAL))
		ret = utvfu_select_norm(sc, norm);

	return (ret);
}

int
utvfu_g_input(void *v, int *i)
{
	struct utvfu_softc *sc = v;

	*i = sc->sc_input;

	return (0);
}

int
utvfu_s_input(void *v, int i)
{
	return utvfu_select_input(v, i);
}

/* A U D I O */

void
utvfu_audio_decode(struct utvfu_softc *sc, int len)
{
	uint8_t *dst, *src;
	int n, chunk, ncopied;

	if (sc->sc_audio.blksize == 0)
		return;

	src = KERNADDR(&sc->sc_audio.iface.xfer->dmabuf, 0);
	dst = sc->sc_audio.cur;
	ncopied = sc->sc_audio.cur - sc->sc_audio.start;
	/* b/c region start->end is a multiple blksize chunks */
	ncopied %= sc->sc_audio.blksize;

	while (len >= UTVFU_CHUNK_SIZE) {
		/*
		 * The header, skipped here, ranges from 0xdd000000 to
		 * 0xdd0003ff.  The 0xdd seems to be the "magic" and
		 * 0x3ff masks the chunk number.
		 */
		src += UTVFU_AUDIO_HDRSIZE;
		chunk = UTVFU_CHUNK;
		while (chunk > 0) {
			n = min(chunk, sc->sc_audio.blksize - ncopied);
			memcpy(dst, src, n);
			dst += n;
			src += n;
			chunk -= n;
			ncopied += n;
			if (ncopied >= sc->sc_audio.blksize) {
				mtx_enter(&audio_lock);
				(*sc->sc_audio.intr)(sc->sc_audio.intr_arg);
				mtx_leave(&audio_lock);
				ncopied -= sc->sc_audio.blksize;
			}
			if (dst > sc->sc_audio.end)
				dst = sc->sc_audio.start;
		}
		len -= UTVFU_CHUNK_SIZE; /* _CHUNK + _AUDIO_HDRSIZE */
	}
	sc->sc_audio.cur = dst;
}

int
utvfu_audio_start_chip(struct utvfu_softc *sc)
{
	static const uint16_t setup[][2] = {
		/* These seem to enable the device. */
		{ UTVFU_BASE + 0x0008, 0x0001 },
		{ UTVFU_BASE + 0x01d0, 0x00ff },
		{ UTVFU_BASE + 0x01d9, 0x0002 },

		{ UTVFU_BASE + 0x01da, 0x0013 },
		{ UTVFU_BASE + 0x01db, 0x0012 },
		{ UTVFU_BASE + 0x01e9, 0x0002 },
		{ UTVFU_BASE + 0x01ec, 0x006c },
		{ UTVFU_BASE + 0x0294, 0x0020 },
		{ UTVFU_BASE + 0x0255, 0x00cf },
		{ UTVFU_BASE + 0x0256, 0x0020 },
		{ UTVFU_BASE + 0x01eb, 0x0030 },
		{ UTVFU_BASE + 0x027d, 0x00a6 },
		{ UTVFU_BASE + 0x0280, 0x0011 },
		{ UTVFU_BASE + 0x0281, 0x0040 },
		{ UTVFU_BASE + 0x0282, 0x0011 },
		{ UTVFU_BASE + 0x0283, 0x0040 },
		{ 0xf891, 0x0010 },

		/* this sets the input from composite */
		{ UTVFU_BASE + 0x0284, 0x00aa },
	};

	/* starting the stream */
	utvfu_set_regs(sc, setup, nitems(setup));

	return (0);
}

int
utvfu_audio_stop_chip(struct utvfu_softc *sc)
{
	static const uint16_t setup[][2] = {
	/*
	 * The original windows driver sometimes sends also:
	 *   { UTVFU_BASE + 0x00a2, 0x0013 }
	 * but it seems useless and its real effects are untested at
	 * the moment.
	 */
		{ UTVFU_BASE + 0x027d, 0x0000 },
		{ UTVFU_BASE + 0x0280, 0x0010 },
		{ UTVFU_BASE + 0x0282, 0x0010 },
	};

	utvfu_set_regs(sc, setup, nitems(setup));

	return (0);
}

/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Marcus Glocker <mglocker@openbsd.org>
 * Copyright (c) 2016 Patrick Keshishian <patrick@boxsoft.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Heavily based on uvideo.c source.
 */

int		utvfu_match(struct device *, void *, void *);
void		utvfu_attach(struct device *, struct device *, void *);
int		utvfu_detach(struct device *, int);

usbd_status	utvfu_parse_desc(struct utvfu_softc *);

void		utvfu_vs_close(struct utvfu_softc *);
void		utvfu_vs_free_frame(struct utvfu_softc *);
void		utvfu_vs_free_isoc(struct utvfu_softc *);
void		utvfu_vs_start_isoc_ixfer(struct utvfu_softc *,
		    struct utvfu_isoc_xfer *);
void		utvfu_vs_cb(struct usbd_xfer *, void *, usbd_status);

void		utvfu_vs_free(struct utvfu_softc *);
int		utvfu_vs_init(struct utvfu_softc *);
int		utvfu_vs_alloc_frame(struct utvfu_softc *);
usbd_status	utvfu_vs_alloc_isoc(struct utvfu_softc *);

int		utvfu_open(void *, int, int *, uint8_t *,
		    void (*)(void *), void *);
int		utvfu_close(void *);
int		utvfu_querycap(void *, struct v4l2_capability *);
int		utvfu_enum_fmt_vid_cap(void *, struct v4l2_fmtdesc *);
int		utvfu_enum_fsizes(void *, struct v4l2_frmsizeenum *);
int		utvfu_g_fmt(void *, struct v4l2_format *);
int		utvfu_s_fmt(void *, struct v4l2_format *);
int		utvfu_g_parm(void *, struct v4l2_streamparm *);
int		utvfu_s_parm(void *, struct v4l2_streamparm *);
int		utvfu_enum_input(void *, struct v4l2_input *);
int		utvfu_s_input(void *, int);
int		utvfu_g_input(void *, int *);

int		utvfu_reqbufs(void *, struct v4l2_requestbuffers *);
int		utvfu_querybuf(void *, struct v4l2_buffer *);
int		utvfu_qbuf(void *, struct v4l2_buffer *);
int		utvfu_dqbuf(void *, struct v4l2_buffer *);
int		utvfu_streamon(void *, int);
int		utvfu_streamoff(void *, int);
int		utvfu_queryctrl(void *, struct v4l2_queryctrl *);
caddr_t		utvfu_mappage(void *, off_t, int);
int		utvfu_get_bufsize(void *);
int		utvfu_start_read(void *);

int		utvfu_as_init(struct utvfu_softc *);
void		utvfu_as_free(struct utvfu_softc *);

usbd_status	utvfu_as_open(struct utvfu_softc *);
int		utvfu_as_alloc_bulk(struct utvfu_softc *);
void		utvfu_as_free_bulk(struct utvfu_softc *);
int		utvfu_as_start_bulk(struct utvfu_softc *);
void		utvfu_as_bulk_thread(void *);

int		utvfu_audio_open(void *, int);
void		utvfu_audio_close(void *);
int		utvfu_audio_set_params(void *, int, int,
		    struct audio_params *, struct audio_params *);
int		utvfu_audio_halt_out(void *);
int		utvfu_audio_halt_in(void *);
int		utvfu_audio_mixer_set_port(void *, struct mixer_ctrl *);
int		utvfu_audio_mixer_get_port(void *, struct mixer_ctrl *);
int		utvfu_audio_query_devinfo(void *, struct mixer_devinfo *);
int		utvfu_audio_trigger_output(void *, void *, void *, int,
		    void (*)(void *), void *, struct audio_params *);
int		utvfu_audio_trigger_input(void *, void *, void *, int,
		    void (*)(void *), void *, struct audio_params *);

struct cfdriver utvfu_cd = {
	NULL, "utvfu", DV_DULL
};

const struct cfattach utvfu_ca = {
	sizeof(struct utvfu_softc),
	utvfu_match,
	utvfu_attach,
	utvfu_detach,
	NULL
};

const struct video_hw_if utvfu_vid_hw_if = {
	utvfu_open,		/* open */
	utvfu_close,		/* close */
	utvfu_querycap,		/* VIDIOC_QUERYCAP */
	utvfu_enum_fmt_vid_cap,	/* VIDIOC_ENUM_FMT */
	utvfu_enum_fsizes,	/* VIDIOC_ENUM_FRAMESIZES */
	NULL,			/* VIDIOC_ENUM_FRAMEINTERVALS */
	utvfu_s_fmt,		/* VIDIOC_S_FMT */
	utvfu_g_fmt,		/* VIDIOC_G_FMT */
	utvfu_s_parm,		/* VIDIOC_S_PARM */
	utvfu_g_parm,		/* VIDIOC_G_PARM */
	utvfu_enum_input,	/* VIDIOC_ENUMINPUT */
	utvfu_s_input,		/* VIDIOC_S_INPUT */
	utvfu_g_input,		/* VIDIOC_G_INPUT */
	utvfu_reqbufs,		/* VIDIOC_REQBUFS */
	utvfu_querybuf,		/* VIDIOC_QUERYBUF */
	utvfu_qbuf,		/* VIDIOC_QBUF */
	utvfu_dqbuf,		/* VIDIOC_DQBUF */
	utvfu_streamon,		/* VIDIOC_STREAMON */
	utvfu_streamoff,	/* VIDIOC_STREAMOFF */
	NULL,			/* VIDIOC_TRY_FMT */
	utvfu_queryctrl,	/* VIDIOC_QUERYCTRL */
	NULL,			/* VIDIOC_G_CTRL */
	NULL,			/* VIDIOC_S_CTRL */
	utvfu_mappage,		/* mmap */
	utvfu_get_bufsize,	/* read */
	utvfu_start_read	/* start stream for read */
};

const struct audio_hw_if utvfu_au_hw_if = {
	.open = utvfu_audio_open,
	.close = utvfu_audio_close,
	.set_params = utvfu_audio_set_params,
	.halt_output = utvfu_audio_halt_out,
	.halt_input = utvfu_audio_halt_in,
	.set_port = utvfu_audio_mixer_set_port,
	.get_port = utvfu_audio_mixer_get_port,
	.query_devinfo = utvfu_audio_query_devinfo,
	.trigger_output = utvfu_audio_trigger_output,
	.trigger_input = utvfu_audio_trigger_input,
};

int
utvfu_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	const struct usb_descriptor *ud;
	struct usbd_desc_iter iter;
	struct usb_interface_descriptor *uid = NULL;
	const struct usb_endpoint_descriptor *ued = NULL;
	usb_device_descriptor_t *dd;
	int ret = UMATCH_NONE;
	int nep, nalt;
	uint16_t psize = 0;

	if (uaa->iface == NULL)
		return ret;

	dd = usbd_get_device_descriptor(uaa->device);

	if (UGETW(dd->idVendor) == USB_VENDOR_FUSHICAI &&
	    UGETW(dd->idProduct) == USB_PRODUCT_FUSHICAI_USBTV007)
		ret = UMATCH_VENDOR_PRODUCT;
	/*
	 * This seems like a fragile check, but the original driver ensures
	 * there are two alternate settings for the interface, and alternate
	 * setting 1 has four endpoints.
	 *
	 * Comment says "Checks that the device is what we think it is."
	 *
	 * Adding check that wMaxPacketSize for the video endpoint is > 0.
	 */
	nep = nalt = 0;
	usbd_desc_iter_init(uaa->device, &iter);
	while ((ud = usbd_desc_iter_next(&iter)) != NULL) {
		switch (ud->bDescriptorType) {
		default:
			break;
		case UDESC_INTERFACE:
			uid = (void *)ud;
			if (uid->bInterfaceNumber == 0)
				nalt++;
			break;
		case UDESC_ENDPOINT:
			if (uid->bAlternateSetting == 1) {
				ued = (void *)ud;
				if (ued->bEndpointAddress == UTVFU_VIDEO_ENDP)
					psize = UGETW(ued->wMaxPacketSize);
				nep++;
			}
			break;
		}
		if (uid != NULL && uid->bInterfaceNumber > 0)
			break;
	}

	if (nalt != 2 || nep != 4 || psize == 0)
		ret = UMATCH_NONE;

	return (ret);
}

void
utvfu_attach(struct device *parent, struct device *self, void *aux)
{
	int i;
	struct utvfu_softc *sc = (struct utvfu_softc *)self;
	struct usb_attach_arg *uaa = aux;

	sc->sc_udev = uaa->device;
	for (i = 0; i < uaa->nifaces; i++) {
		if (usbd_iface_claimed(sc->sc_udev, i))
			continue;
		usbd_claim_iface(sc->sc_udev, i);
	}

	utvfu_parse_desc(sc);

	/* init mmap queue */
	SIMPLEQ_INIT(&sc->sc_mmap_q);
	sc->sc_mmap_count = 0;

	sc->sc_max_frame_sz = utvfu_max_frame_size();

	/* calculate optimal isoc xfer size */
	sc->sc_nframes = (sc->sc_max_frame_sz + sc->sc_iface.psize - 1)
	    / sc->sc_iface.psize;
	if (sc->sc_nframes > UTVFU_NFRAMES_MAX)
		sc->sc_nframes = UTVFU_NFRAMES_MAX;
	DPRINTF(1, "%s: nframes=%d\n", DEVNAME(sc), sc->sc_nframes);

	rw_init(&sc->sc_audio.rwlock, "audiorwl");

	sc->sc_audiodev = audio_attach_mi(&utvfu_au_hw_if, sc, NULL, &sc->sc_dev);
	sc->sc_videodev = video_attach_mi(&utvfu_vid_hw_if, sc, &sc->sc_dev);
}

int
utvfu_detach(struct device *self, int flags)
{
	struct utvfu_softc *sc = (struct utvfu_softc *)self;
	int rv = 0;

	/* Wait for outstanding requests to complete */
	usbd_delay_ms(sc->sc_udev, UTVFU_NFRAMES_MAX); /* XXX meh? */

	if (sc->sc_videodev != NULL)
		rv = config_detach(sc->sc_videodev, flags);

	if (sc->sc_audiodev != NULL)
		rv += config_detach(sc->sc_audiodev, flags);

	utvfu_as_free(sc);
	utvfu_vs_free(sc);

	sc->sc_flags = 0;

	return (rv);
}

usbd_status
utvfu_parse_desc(struct utvfu_softc *sc)
{
	int nif, nep;
	uint32_t psize;
	struct usbd_desc_iter iter;
	const struct usb_descriptor *ud;
	struct usb_endpoint_descriptor *ued;
	struct usb_interface_descriptor *uid = NULL;

	nif = nep = 0;
	usbd_desc_iter_init(sc->sc_udev, &iter);
	while ((ud = usbd_desc_iter_next(&iter)) != NULL) {
		if (ud->bDescriptorType != UDESC_INTERFACE)
			continue;
		/* looking for interface 0, alt-setting 1 */
		uid = (void *)ud;
		if (uid->bInterfaceNumber > 0)
			break;
		if (uid->bAlternateSetting == 1)
			break;
	}

	/* this should not fail as it was ensured during match */
	if (uid == NULL || uid->bInterfaceNumber != 0 ||
	    uid->bAlternateSetting != 1) {
		printf("%s: no valid alternate interface found!\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}

	/* bInterfaceNumber = 0 */
	sc->sc_uifaceh = &sc->sc_udev->ifaces[0];

	/* looking for video endpoint to on alternate setting 1 */
	while ((ud = usbd_desc_iter_next(&iter)) != NULL) {
		if (ud->bDescriptorType != UDESC_ENDPOINT)
			break;

		ued = (void *)ud;
		if (ued->bEndpointAddress != UTVFU_VIDEO_ENDP)
			continue;

		psize = UGETW(ued->wMaxPacketSize);
		psize = UE_GET_SIZE(psize) * (1 + UE_GET_TRANS(psize));
		sc->sc_iface.psize = psize;
		break;
	}

	return (USBD_NORMAL_COMPLETION);
}

int
utvfu_open(void *addr, int flags, int *size, uint8_t *buffer,
    void (*intr)(void *), void *arg)
{
	struct utvfu_softc *sc = addr;
	int rv;

	DPRINTF(1, "%s: utvfu_open: sc=%p\n", DEVNAME(sc), sc);

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	if ((rv = utvfu_vs_init(sc)) != 0)
		return (rv);

	/* pointers to upper video layer */
	sc->sc_uplayer_arg = arg;
	sc->sc_uplayer_fsize = size;
	sc->sc_uplayer_fbuffer = buffer;
	sc->sc_uplayer_intr = intr;

	sc->sc_flags &= ~UTVFU_FLAG_MMAP;

	return (0);
}

int
utvfu_close(void *addr)
{
	struct utvfu_softc *sc = addr;

	DPRINTF(1, "%s: utvfu_close: sc=%p\n", DEVNAME(sc), sc);

	/* free & clean up video stream */
	utvfu_vs_free(sc);

	return (0);
}

usbd_status
utvfu_as_open(struct utvfu_softc *sc)
{
	usb_endpoint_descriptor_t *ed;
	usbd_status error;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_audio.iface.pipeh != NULL) {
		printf("%s: %s called while sc_audio.iface.pipeh not NULL\n",
		    DEVNAME(sc), __func__);
		return (USBD_INVAL);
	}

	ed = usbd_get_endpoint_descriptor(sc->sc_uifaceh, UTVFU_AUDIO_ENDP);
	if (ed == NULL) {
		printf("%s: no endpoint descriptor for AS iface\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: open pipe for ", DEVNAME(sc));
	DPRINTF(1, "bEndpointAddress=0x%02x (0x%02x), wMaxPacketSize="
	    "0x%04x (%d)\n",
	    UE_GET_ADDR(ed->bEndpointAddress),
	    UTVFU_AUDIO_ENDP,
	    UGETW(ed->wMaxPacketSize),
	    UE_GET_SIZE(UGETW(ed->wMaxPacketSize))
	    * (1 + UE_GET_TRANS(UGETW(ed->wMaxPacketSize))));

	error = usbd_open_pipe(
	    sc->sc_uifaceh,
	    UTVFU_AUDIO_ENDP,
	    USBD_EXCLUSIVE_USE,
	    &sc->sc_audio.iface.pipeh);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not open AS pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
	}

	return (error);
}

usbd_status
utvfu_vs_open(struct utvfu_softc *sc)
{
	usb_endpoint_descriptor_t *ed;
	usbd_status error;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_iface.pipeh != NULL) {
		printf("%s: %s called while sc_iface.pipeh not NULL\n",
		    DEVNAME(sc), __func__);
		return (USBD_INVAL);
	}

	ed = usbd_get_endpoint_descriptor(sc->sc_uifaceh, UTVFU_VIDEO_ENDP);
	if (ed == NULL) {
		printf("%s: no endpoint descriptor for VS iface\n",
		    DEVNAME(sc));
		return (USBD_INVAL);
	}
	DPRINTF(1, "%s: open pipe for ", DEVNAME(sc));
	DPRINTF(1, "bEndpointAddress=0x%02x (0x%02x), wMaxPacketSize="
	    "0x%04x (%d)\n",
	    UE_GET_ADDR(ed->bEndpointAddress),
	    UTVFU_VIDEO_ENDP,
	    UGETW(ed->wMaxPacketSize),
	    sc->sc_iface.psize);

	error = usbd_open_pipe(
	    sc->sc_uifaceh,
	    UTVFU_VIDEO_ENDP,
	    USBD_EXCLUSIVE_USE,
	    &sc->sc_iface.pipeh);
	if (error != USBD_NORMAL_COMPLETION) {
		printf("%s: could not open VS pipe: %s\n",
		    DEVNAME(sc), usbd_errstr(error));
	}

	return (error);
}

void
utvfu_as_close(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	CLR(sc->sc_flags, UTVFU_FLAG_AS_RUNNING);

	if (sc->sc_audio.iface.pipeh != NULL) {
		usbd_abort_pipe(sc->sc_audio.iface.pipeh);

		usbd_ref_wait(sc->sc_udev);

		usbd_close_pipe(sc->sc_audio.iface.pipeh);
		sc->sc_audio.iface.pipeh = NULL;
	}
}

void
utvfu_vs_close(struct utvfu_softc *sc)
{
	if (sc->sc_iface.pipeh != NULL) {
		usbd_close_pipe(sc->sc_iface.pipeh);
		sc->sc_iface.pipeh = NULL;
	}

	/*
	 * Some devices need time to shutdown before we switch back to
	 * the default interface (0).  Not doing so can leave the device
	 * back in a undefined condition.
	 */
	usbd_delay_ms(sc->sc_udev, 100);

	/* switch back to default interface (turns off cam LED) */
	(void)usbd_set_interface(sc->sc_uifaceh, UTVFU_DFLT_IFACE_IDX);
}

void
utvfu_read(struct utvfu_softc *sc, uint8_t *buf, int len)
{
	/*
	 * Copy video frame to upper layer buffer and call
	 * upper layer interrupt.
	 */
	*sc->sc_uplayer_fsize = len;
	memcpy(sc->sc_uplayer_fbuffer, buf, len);
	(*sc->sc_uplayer_intr)(sc->sc_uplayer_arg);
}

int
utvfu_as_start_bulk(struct utvfu_softc *sc)
{
	int error;

	if (ISSET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING))
		return (0);
	if (sc->sc_audio.iface.pipeh == NULL)
		return (ENXIO);

	SET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING);
	error = kthread_create(utvfu_as_bulk_thread, sc, NULL, DEVNAME(sc));
	if (error) {
		CLR(sc->sc_flags, UTVFU_FLAG_AS_RUNNING);
		printf("%s: can't create kernel thread!", DEVNAME(sc));
	}

	return (error);
}

void
utvfu_as_bulk_thread(void *arg)
{
	struct utvfu_softc *sc = arg;
	struct utvfu_as_iface *iface;
	usbd_status error;
	uint32_t actlen;

	DPRINTF(1, "%s %s\n", DEVNAME(sc), __func__);

	iface = &sc->sc_audio.iface;
	usbd_ref_incr(sc->sc_udev);
	while (ISSET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING)) {
		usbd_setup_xfer(
		    iface->xfer,
		    iface->pipeh,
		    0,
		    NULL,
		    UTVFU_AUDIO_URBSIZE,
		    USBD_NO_COPY | USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS,
		    0,
		    NULL);
		error = usbd_transfer(iface->xfer);

		if (error != USBD_NORMAL_COMPLETION) {
			DPRINTF(1, "%s: error in bulk xfer: %s!\n",
			    DEVNAME(sc), usbd_errstr(error));
			break;
		}

		usbd_get_xfer_status(iface->xfer, NULL, NULL, &actlen,
		    NULL);
		DPRINTF(2, "%s: *** buffer len = %d\n", DEVNAME(sc), actlen);

		rw_enter_read(&sc->sc_audio.rwlock);
		utvfu_audio_decode(sc, actlen);
		rw_exit_read(&sc->sc_audio.rwlock);
	}

	CLR(sc->sc_flags, UTVFU_FLAG_AS_RUNNING);
	usbd_ref_decr(sc->sc_udev);

	DPRINTF(1, "%s %s: exiting\n", DEVNAME(sc), __func__);

	kthread_exit(0);
}

void
utvfu_vs_start_isoc(struct utvfu_softc *sc)
{
	int i;

	for (i = 0; i < UTVFU_ISOC_TRANSFERS; i++)
		utvfu_vs_start_isoc_ixfer(sc, &sc->sc_iface.ixfer[i]);
}

void
utvfu_vs_start_isoc_ixfer(struct utvfu_softc *sc,
	struct utvfu_isoc_xfer *ixfer)
{
	int i;
	usbd_status error;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	if (usbd_is_dying(sc->sc_udev))
		return;

	for (i = 0; i < sc->sc_nframes; i++)
		ixfer->size[i] = sc->sc_iface.psize;

	usbd_setup_isoc_xfer(
	    ixfer->xfer,
	    sc->sc_iface.pipeh,
	    ixfer,
	    ixfer->size,
	    sc->sc_nframes,
	    USBD_NO_COPY | USBD_SHORT_XFER_OK,
	    utvfu_vs_cb);

	error = usbd_transfer(ixfer->xfer);
	if (error && error != USBD_IN_PROGRESS) {
		DPRINTF(1, "%s: usbd_transfer error=%s!\n",
		    DEVNAME(sc), usbd_errstr(error));
	}
}

/*
 * Each packet contains a number of 256-byte chunks composing the image frame.
 */
void
utvfu_vs_cb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct utvfu_isoc_xfer *ixfer = priv;
	struct utvfu_softc *sc = ixfer->sc;
	int i, off, frame_size;
	uint32_t actlen;
	uint8_t *frame;

	DPRINTF(2, "%s: %s\n", DEVNAME(sc), __func__);

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(1, "%s: %s: %s\n", DEVNAME(sc), __func__,
		    usbd_errstr(status));
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &actlen, NULL);

	DPRINTF(2, "%s: *** buffer len = %d\n", DEVNAME(sc), actlen);
	if (actlen == 0)
		goto skip;

	frame = KERNADDR(&xfer->dmabuf, 0);
	for (i = 0; i < sc->sc_nframes; i++, frame += sc->sc_iface.psize) {
		frame_size = ixfer->size[i];

		if (frame_size == 0)
			/* frame is empty */
			continue;

		#define	CHUNK_STRIDE	(UTVFU_CHUNK_SIZE*4)
		for (off = 0; off + CHUNK_STRIDE <= frame_size;
		     off += CHUNK_STRIDE) {
			utvfu_image_chunk(sc, frame + off);
		}
		#undef	CHUNK_STRIDE
	}

skip:	/* setup new transfer */
	utvfu_vs_start_isoc_ixfer(sc, ixfer);
}

int
utvfu_find_queued(struct utvfu_softc *sc)
{
	int i;

	/* find a buffer which is ready for queueing */
	for (i = 0; i < sc->sc_mmap_count; i++) {
		if (sc->sc_mmap[i].v4l2_buf.flags & V4L2_BUF_FLAG_DONE)
			continue;
		if (sc->sc_mmap[i].v4l2_buf.flags & V4L2_BUF_FLAG_QUEUED)
			return (i);
	}

	return (-1);
}

int
utvfu_mmap_queue(struct utvfu_softc *sc, uint8_t *buf, int len)
{
	int i;

	if (sc->sc_mmap_count == 0 || sc->sc_mmap_buffer == NULL)
		panic("%s: mmap buffers not allocated", __func__);

	/* find a buffer which is ready for queueing */
	if ((i = utvfu_find_queued(sc)) == -1) {
		DPRINTF(2, "%s: mmap queue is full!\n", DEVNAME(sc));
		return (ENOMEM);
	}

	/* copy frame to mmap buffer and report length */
	memcpy(sc->sc_mmap[i].buf, buf, len);
	sc->sc_mmap[i].v4l2_buf.bytesused = len;

	/* timestamp it */
	getmicrouptime(&sc->sc_mmap[i].v4l2_buf.timestamp);
	sc->sc_mmap[i].v4l2_buf.flags &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
	sc->sc_mmap[i].v4l2_buf.flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	sc->sc_mmap[i].v4l2_buf.flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	sc->sc_mmap[i].v4l2_buf.flags |= V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	sc->sc_mmap[i].v4l2_buf.flags &= ~V4L2_BUF_FLAG_TIMECODE;

	/* appropriately set/clear flags */
	sc->sc_mmap[i].v4l2_buf.flags &= ~V4L2_BUF_FLAG_QUEUED;
	sc->sc_mmap[i].v4l2_buf.flags |= V4L2_BUF_FLAG_DONE;

	/* queue it */
	SIMPLEQ_INSERT_TAIL(&sc->sc_mmap_q, &sc->sc_mmap[i], q_frames);
	DPRINTF(2, "%s: %s: frame queued on index %d\n",
	    DEVNAME(sc), __func__, i);

	wakeup(sc);

	/*
	 * In case userland uses poll(2), signal that we have a frame
	 * ready to dequeue.
	 */
	(*sc->sc_uplayer_intr)(sc->sc_uplayer_arg);

	return (0);
}

caddr_t
utvfu_mappage(void *v, off_t off, int prot)
{
	struct utvfu_softc *sc = v;
	caddr_t p = NULL;

	if (off < sc->sc_mmap_bufsz) {
		if ((sc->sc_flags & UTVFU_FLAG_MMAP) == 0)
			sc->sc_flags |= UTVFU_FLAG_MMAP;

		p = sc->sc_mmap_buffer + off;
	}

	return (p);
}

int
utvfu_get_bufsize(void *v)
{
	struct utvfu_softc *sc = v;

	/* YUYV/YUV-422: 4 bytes/2 pixel */
	return (utvfu_norm_params[sc->sc_normi].cap_width *
	    utvfu_norm_params[sc->sc_normi].cap_height * 2);
}

int
utvfu_start_read(void *v)
{
	struct utvfu_softc *sc = v;
	usbd_status error;

	if (sc->sc_flags & UTVFU_FLAG_MMAP)
		sc->sc_flags &= ~UTVFU_FLAG_MMAP;

	/* open video stream pipe */
	error = utvfu_vs_open(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	utvfu_vs_start_isoc(sc);

	return (0);
}

void
utvfu_audio_clear_client(struct utvfu_softc *sc)
{
	rw_enter_write(&sc->sc_audio.rwlock);

	sc->sc_audio.intr = NULL;
	sc->sc_audio.intr_arg = NULL;
	sc->sc_audio.start = NULL;
	sc->sc_audio.end = NULL;
	sc->sc_audio.cur = NULL;
	sc->sc_audio.blksize = 0;

	rw_exit_write(&sc->sc_audio.rwlock);
}

void
utvfu_as_free(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	utvfu_as_close(sc);
	utvfu_as_free_bulk(sc);
}

void
utvfu_vs_free(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);
	utvfu_vs_close(sc);
	utvfu_vs_free_isoc(sc);
	utvfu_vs_free_frame(sc);
}

int
utvfu_as_init(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_audio.iface.xfer != NULL)
		return (0);

	/* allocate audio and video stream xfer buffer */
	return utvfu_as_alloc_bulk(sc);
}

int
utvfu_vs_init(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (utvfu_start_capture(sc) != 0)
		return (EINVAL);

	if (utvfu_vs_alloc_isoc(sc) != 0 || utvfu_vs_alloc_frame(sc) != 0)
		return (ENOMEM);

	return (0);
}

int
utvfu_vs_alloc_frame(struct utvfu_softc *sc)
{
	struct utvfu_frame_buf *fb = &sc->sc_fb;

	fb->size = sc->sc_max_frame_sz;
	fb->buf = malloc(fb->size, M_USBDEV, M_NOWAIT);
	if (fb->buf == NULL) {
		printf("%s: can't allocate frame buffer!\n", DEVNAME(sc));
		return (ENOMEM);
	}

	DPRINTF(1, "%s: %s: allocated %d bytes frame buffer\n",
	    DEVNAME(sc), __func__, fb->size);

	fb->chunks_done = 0;
	fb->fid = 0;
	fb->last_odd = 1;

	return (0);
}

void
utvfu_vs_free_frame(struct utvfu_softc *sc)
{
	struct utvfu_frame_buf *fb = &sc->sc_fb;

	if (fb->buf != NULL) {
		free(fb->buf, M_USBDEV, fb->size);
		fb->buf = NULL;
	}

	if (sc->sc_mmap_buffer != NULL) {
		free(sc->sc_mmap_buffer, M_USBDEV, sc->sc_mmap_bufsz);
		sc->sc_mmap_buffer = NULL;
		memset(sc->sc_mmap, 0, sizeof(sc->sc_mmap));
	}

	while (!SIMPLEQ_EMPTY(&sc->sc_mmap_q))
		SIMPLEQ_REMOVE_HEAD(&sc->sc_mmap_q, q_frames);

	sc->sc_mmap_count = 0;
}

usbd_status
utvfu_vs_alloc_isoc(struct utvfu_softc *sc)
{
	int size, i;
	void *buf;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	for (i = 0; i < UTVFU_ISOC_TRANSFERS; i++) {
		sc->sc_iface.ixfer[i].sc = sc;
		sc->sc_iface.ixfer[i].xfer = usbd_alloc_xfer(sc->sc_udev);	
		if (sc->sc_iface.ixfer[i].xfer == NULL) {
			printf("%s: could not allocate isoc VS xfer!\n",
			    DEVNAME(sc));
			return (USBD_NOMEM);	
		}

		size = sc->sc_iface.psize * sc->sc_nframes;

		buf = usbd_alloc_buffer(sc->sc_iface.ixfer[i].xfer, size);
		if (buf == NULL) {
			printf("%s: could not allocate isoc VS buffer!\n",
			    DEVNAME(sc));
			return (USBD_NOMEM);
		}
		DPRINTF(1, "%s: allocated %d bytes isoc VS xfer buffer\n",
		    DEVNAME(sc), size);
	}

	return (USBD_NORMAL_COMPLETION);
}

int
utvfu_as_alloc_bulk(struct utvfu_softc *sc)
{
	struct usbd_xfer *xfer;

	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL) {
		printf("%s: could not allocate bulk AUDIO xfer!\n",
		    DEVNAME(sc));
		return (ENOMEM);
	}

	if (usbd_alloc_buffer(xfer, UTVFU_AUDIO_URBSIZE) == NULL) {
		usbd_free_xfer(xfer);
		printf("%s: could not allocate bulk AUDIO buffer!\n",
		    DEVNAME(sc));
		return (ENOMEM);
	}
	DPRINTF(1, "%s: allocated %d bytes bulk AUDIO xfer buffer\n",
	    DEVNAME(sc), UTVFU_AUDIO_URBSIZE);

	sc->sc_audio.iface.xfer = xfer;

	return (0);
}

void
utvfu_vs_free_isoc(struct utvfu_softc *sc)
{
	int i;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	for (i = 0; i < UTVFU_ISOC_TRANSFERS; i++) {
		if (sc->sc_iface.ixfer[i].xfer != NULL) {
			usbd_free_xfer(sc->sc_iface.ixfer[i].xfer);
			sc->sc_iface.ixfer[i].xfer = NULL;
		}
	}
}

void
utvfu_as_free_bulk(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (sc->sc_audio.iface.xfer != NULL) {
		usbd_free_xfer(sc->sc_audio.iface.xfer);
		sc->sc_audio.iface.xfer = NULL;
	}
}

int
utvfu_reqbufs(void *v, struct v4l2_requestbuffers *rb)
{
	struct utvfu_softc *sc = v;
	int i;

	DPRINTF(1, "%s: %s: count=%d\n", DEVNAME(sc), __func__, rb->count);

	/* We do not support freeing buffers via reqbufs(0) */
	if (rb->count == 0)
		return (EINVAL);

	if (sc->sc_mmap_count > 0 || sc->sc_mmap_buffer != NULL) {
		DPRINTF(1, "%s: %s: mmap buffers already allocated\n",
		    DEVNAME(sc), __func__);
		return (EINVAL);
	}

	/* limit the buffers */
	if (rb->count > UTVFU_MAX_BUFFERS)
		sc->sc_mmap_count = UTVFU_MAX_BUFFERS;
	else
		sc->sc_mmap_count = rb->count;

	/* allocate the total mmap buffer */	
	sc->sc_mmap_bufsz = sc->sc_max_frame_sz;
	if (INT_MAX / sc->sc_mmap_count < sc->sc_max_frame_sz) /* overflow */
		return (ENOMEM);
	sc->sc_mmap_bufsz *= sc->sc_mmap_count;
	sc->sc_mmap_bufsz = round_page(sc->sc_mmap_bufsz); /* page align */
	sc->sc_mmap_buffer = malloc(sc->sc_mmap_bufsz, M_USBDEV, M_NOWAIT);
	if (sc->sc_mmap_buffer == NULL) {
		printf("%s: can't allocate mmap buffer!\n", DEVNAME(sc));
		return (ENOMEM);
	}
	DPRINTF(1, "%s: allocated %d bytes mmap buffer\n",
	    DEVNAME(sc), sc->sc_mmap_bufsz);

	/* fill the v4l2_buffer structure */
	for (i = 0; i < sc->sc_mmap_count; i++) {
		sc->sc_mmap[i].buf = sc->sc_mmap_buffer
		    + (i * sc->sc_max_frame_sz);
		sc->sc_mmap[i].v4l2_buf.index = i;
		sc->sc_mmap[i].v4l2_buf.m.offset = i * sc->sc_max_frame_sz;
		sc->sc_mmap[i].v4l2_buf.length = sc->sc_max_frame_sz;
		sc->sc_mmap[i].v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		sc->sc_mmap[i].v4l2_buf.sequence = 0;
		sc->sc_mmap[i].v4l2_buf.field = V4L2_FIELD_NONE;
		sc->sc_mmap[i].v4l2_buf.memory = V4L2_MEMORY_MMAP;
		sc->sc_mmap[i].v4l2_buf.flags = V4L2_BUF_FLAG_MAPPED;

		DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
		    DEVNAME(sc), __func__,
		    sc->sc_mmap[i].v4l2_buf.index,
		    sc->sc_mmap[i].v4l2_buf.m.offset,
		    sc->sc_mmap[i].v4l2_buf.length);
	}

	/* tell how many buffers we have really allocated */
	rb->count = sc->sc_mmap_count;

	rb->capabilities = V4L2_BUF_CAP_SUPPORTS_MMAP;

	return (0);
}

int
utvfu_querybuf(void *v, struct v4l2_buffer *qb)
{
	struct utvfu_softc *sc = v;

	if (qb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    qb->memory != V4L2_MEMORY_MMAP ||
	    qb->index >= sc->sc_mmap_count)
		return (EINVAL);

	memcpy(qb, &sc->sc_mmap[qb->index].v4l2_buf,
	    sizeof(struct v4l2_buffer));

	DPRINTF(1, "%s: %s: index=%d, offset=%d, length=%d\n",
	    DEVNAME(sc), __func__, qb->index, qb->m.offset, qb->length);

	return (0);
}

int
utvfu_qbuf(void *v, struct v4l2_buffer *qb)
{
	struct utvfu_softc *sc = v;

	if (qb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    qb->memory != V4L2_MEMORY_MMAP ||
	    qb->index >= sc->sc_mmap_count)
		return (EINVAL);

	sc->sc_mmap[qb->index].v4l2_buf.flags &= ~V4L2_BUF_FLAG_DONE;
	sc->sc_mmap[qb->index].v4l2_buf.flags |= V4L2_BUF_FLAG_MAPPED;
	sc->sc_mmap[qb->index].v4l2_buf.flags |= V4L2_BUF_FLAG_QUEUED;

	DPRINTF(2, "%s: %s: buffer on index %d ready for queueing\n",
	    DEVNAME(sc), __func__, qb->index);

	return (0);
}

int
utvfu_dqbuf(void *v, struct v4l2_buffer *dqb)
{
	struct utvfu_softc *sc = v;
	struct utvfu_mmap *mmap;
	int error;

	if (dqb->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    dqb->memory != V4L2_MEMORY_MMAP)
		return (EINVAL);

	if (SIMPLEQ_EMPTY(&sc->sc_mmap_q)) {
		/* mmap queue is empty, block until first frame is queued */
		error = tsleep_nsec(sc, 0, "vid_mmap", SEC_TO_NSEC(10));
		if (error)
			return (EINVAL);
	}

	mmap = SIMPLEQ_FIRST(&sc->sc_mmap_q);
	if (mmap == NULL)
		panic("utvfu_dqbuf: NULL pointer!");

	memcpy(dqb, &mmap->v4l2_buf, sizeof(struct v4l2_buffer));

	mmap->v4l2_buf.flags &= ~(V4L2_BUF_FLAG_DONE|V4L2_BUF_FLAG_QUEUED);
	mmap->v4l2_buf.flags |= V4L2_BUF_FLAG_MAPPED;

	DPRINTF(2, "%s: %s: frame dequeued from index %d\n",
	    DEVNAME(sc), __func__, mmap->v4l2_buf.index);
	SIMPLEQ_REMOVE_HEAD(&sc->sc_mmap_q, q_frames);

	return (0);
}

int
utvfu_streamon(void *v, int type)
{
	struct utvfu_softc *sc = v;
	usbd_status error;

	/* open video stream pipe */
	error = utvfu_vs_open(sc);
	if (error != USBD_NORMAL_COMPLETION)
		return (EINVAL);

	utvfu_vs_start_isoc(sc);

	return (0);
}

int
utvfu_streamoff(void *v, int type)
{
	utvfu_vs_close(v);

	return (0);
}

int
utvfu_queryctrl(void *v, struct v4l2_queryctrl *qctrl)
{
	qctrl->flags = V4L2_CTRL_FLAG_DISABLED;

	return (0);
}

int
utvfu_g_parm(void *v, struct v4l2_streamparm *parm)
{
	struct utvfu_softc *sc = v;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	/*
	 * XXX Unsure whether there is a way to negotiate this with the
	 * device, but returning 0 will allow xenocara's video to run
	 */
	switch (utvfu_norm_params[sc->sc_normi].norm) {
	default:
		return (EINVAL);
	case V4L2_STD_525_60:
		parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
		parm->parm.capture.capturemode = 0;
		parm->parm.capture.timeperframe.numerator = 30;
		parm->parm.capture.timeperframe.denominator = 1;
		break;
	case V4L2_STD_PAL:
		parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
		parm->parm.capture.capturemode = 0;
		parm->parm.capture.timeperframe.numerator = 25;
		parm->parm.capture.timeperframe.denominator = 1;
		break;
	}

	return (0);
}

int
utvfu_s_parm(void *v, struct v4l2_streamparm *parm)
{
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return (EINVAL);

	return (0);
}

/*
 * A U D I O   O P S
 */

int
utvfu_audio_open(void *v, int flags)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	if ((flags & FWRITE))
		return (ENXIO);

	if (ISSET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING))
		return (EBUSY);

	return utvfu_as_init(sc);
}

void
utvfu_audio_close(void *v)
{
	struct utvfu_softc *sc = v;

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	utvfu_audio_stop(sc);
	utvfu_audio_clear_client(sc);
}

int
utvfu_audio_set_params(void *v, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(1, "%s %s\n", DEVNAME(sc), __func__);

	/* XXX ? */
	play->sample_rate = 0;
	play->encoding = AUDIO_ENCODING_NONE;

	rec->sample_rate = 48000;
	rec->encoding = AUDIO_ENCODING_SLINEAR_LE;
	rec->precision = 16;
	rec->bps = 2;
	rec->msb = 1;
	rec->channels = 2;

	return (0);
}

int
utvfu_audio_halt_out(void *v)
{
	return (EIO);
}

int
utvfu_audio_halt_in(void *v)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	utvfu_audio_stop(sc);
	utvfu_audio_clear_client(sc);

	return (0);
}

int
utvfu_audio_mixer_set_port(void *v, struct mixer_ctrl *cp)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(1, "%s %s\n", DEVNAME(sc), __func__);

	if (cp->type != AUDIO_MIXER_ENUM ||
	    cp->un.ord < 0 || cp->un.ord > 1)
		return (EINVAL);

	/* XXX TODO */

	DPRINTF(1, "%s %s: cp->un.ord=%d\n", DEVNAME(sc), __func__, cp->un.ord);

	return (0);
}

int
utvfu_audio_mixer_get_port(void *v, struct mixer_ctrl *cp)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(1, "%s %s\n", DEVNAME(sc), __func__);

	if (cp->type != AUDIO_MIXER_ENUM ||
	    cp->un.ord < 0 || cp->un.ord > 1)
		return (EINVAL);

	/* XXX TODO */

	DPRINTF(1, "%s %s: cp->un.ord=%d\n", DEVNAME(sc), __func__, cp->un.ord);

	return (0);
}

int
utvfu_audio_query_devinfo(void *v, struct mixer_devinfo *mi)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(1, "%s %s\n", DEVNAME(sc), __func__);

	if (mi->index != 0)
		return (EINVAL);

	/* XXX SOMEONE WITH AUDIO EXPERTIZE NEEDS TO HELP HERE */
	strlcpy(mi->label.name, "mix0-i0", sizeof(mi->label.name));
	mi->type = AUDIO_MIXER_ENUM;
	mi->un.e.num_mem = 2;
	mi->un.e.member[0].ord = 0;
	strlcpy(mi->un.e.member[0].label.name, AudioNoff,
	    sizeof(mi->un.e.member[0].label.name));
	mi->un.e.member[1].ord = 1;
	strlcpy(mi->un.e.member[1].label.name, AudioNon,
	    sizeof(mi->un.e.member[1].label.name));

	return (0);
}

int
utvfu_audio_trigger_output(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	return (EIO);
}

int
utvfu_audio_trigger_input(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct utvfu_softc *sc = v;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	rw_enter_write(&sc->sc_audio.rwlock);

	sc->sc_audio.intr_arg = arg;
	sc->sc_audio.intr = intr;
	sc->sc_audio.start = start;
	sc->sc_audio.end = end;
	sc->sc_audio.cur = start;
	sc->sc_audio.blksize = blksize;

	rw_exit_write(&sc->sc_audio.rwlock);

	DPRINTF(1, "%s %s: start=%p end=%p diff=%lu blksize=%d\n",
	    DEVNAME(sc), __func__, start, end,
	    ((u_char *)end - (u_char *)start), blksize);

	return utvfu_audio_start(sc);
}

int
utvfu_audio_start(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	if (ISSET(sc->sc_flags, UTVFU_FLAG_AS_RUNNING))
		return (0);

	utvfu_audio_start_chip(sc);

	if (utvfu_as_init(sc) != 0)
		return (ENOMEM);
	if (sc->sc_audio.iface.pipeh == NULL) {
		if (utvfu_as_open(sc) != USBD_NORMAL_COMPLETION)
			return (ENOMEM);
	}

	return utvfu_as_start_bulk(sc);
}

int
utvfu_audio_stop(struct utvfu_softc *sc)
{
	DPRINTF(1, "%s: %s\n", DEVNAME(sc), __func__);

	utvfu_audio_stop_chip(sc);
	utvfu_as_free(sc);

	return (0);
}
