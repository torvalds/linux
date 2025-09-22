/*	$OpenBSD: video_if.h,v 1.19 2022/03/21 19:22:40 miod Exp $	*/
/*
 * Copyright (c) 2008 Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2008 Marcus Glocker <mglocker@openbsd.org>
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

#ifndef _SYS_DEV_VIDEO_IF_H
#define _SYS_DEV_VIDEO_IF_H

/*
 * Generic interface to hardware driver
 */

#define VIDEOUNIT(x)	(minor(x))

struct video_hw_if {
	/* open hardware */
	int	(*open)(void *, int, int *, uint8_t *, void (*)(void *),
		    void *);

	/* close hardware */
	int	(*close)(void *);

	/* ioctl's */
	int	(*querycap)(void *, struct v4l2_capability *);
	int	(*enum_fmt)(void *, struct v4l2_fmtdesc *);
	int	(*enum_fsizes)(void *, struct v4l2_frmsizeenum *);
	int	(*enum_fivals)(void *, struct v4l2_frmivalenum *);
	int	(*s_fmt)(void *, struct v4l2_format *);
	int	(*g_fmt)(void *, struct v4l2_format *);
	int	(*s_parm)(void *, struct v4l2_streamparm *);
	int	(*g_parm)(void *, struct v4l2_streamparm *);
	int	(*enum_input)(void *, struct v4l2_input *);
	int	(*s_input)(void *, int);
	int	(*g_input)(void *, int *);
	int	(*reqbufs)(void *, struct v4l2_requestbuffers *);
	int	(*querybuf)(void *, struct v4l2_buffer *);
	int	(*qbuf)(void *, struct v4l2_buffer *);
	int	(*dqbuf)(void *, struct v4l2_buffer *);
	int	(*streamon)(void *, int);
	int	(*streamoff)(void *, int);
	int	(*try_fmt)(void *, struct v4l2_format *);
	int	(*queryctrl)(void *, struct v4l2_queryctrl *);
	int	(*g_ctrl)(void *, struct v4l2_control *);
	int	(*s_ctrl)(void *, struct v4l2_control *);
	caddr_t	(*mappage)(void *, off_t, int);

	/* other functions */
	int	(*get_bufsize)(void *);
	int	(*start_read)(void *);
};

struct video_attach_args {
        const void *hwif;
        void	*hdl;
};

struct device  *video_attach_mi(const struct video_hw_if *, void *,
	    struct device *);

#endif /* _SYS_DEV_VIDEO_IF_H */
