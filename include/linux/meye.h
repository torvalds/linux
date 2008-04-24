/*
 * Motion Eye video4linux driver for Sony Vaio PictureBook
 *
 * Copyright (C) 2001-2003 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * Some parts borrowed from various video4linux drivers, especially
 * bttv-driver.c and zoran.c, see original files for credits.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MEYE_H_
#define _MEYE_H_

/****************************************************************************/
/* Private API for handling mjpeg capture / playback.                       */
/****************************************************************************/

struct meye_params {
	unsigned char subsample;
	unsigned char quality;
	unsigned char sharpness;
	unsigned char agc;
	unsigned char picture;
	unsigned char framerate;
};

/* query the extended parameters */
#define MEYEIOC_G_PARAMS	_IOR ('v', BASE_VIDIOCPRIVATE+0, struct meye_params)
/* set the extended parameters */
#define MEYEIOC_S_PARAMS	_IOW ('v', BASE_VIDIOCPRIVATE+1, struct meye_params)
/* queue a buffer for mjpeg capture */
#define MEYEIOC_QBUF_CAPT	_IOW ('v', BASE_VIDIOCPRIVATE+2, int)
/* sync a previously queued mjpeg buffer */
#define MEYEIOC_SYNC		_IOWR('v', BASE_VIDIOCPRIVATE+3, int)
/* get a still uncompressed snapshot */
#define MEYEIOC_STILLCAPT	_IO  ('v', BASE_VIDIOCPRIVATE+4)
/* get a jpeg compressed snapshot */
#define MEYEIOC_STILLJCAPT	_IOR ('v', BASE_VIDIOCPRIVATE+5, int)

/* V4L2 private controls */
#define V4L2_CID_AGC		V4L2_CID_PRIVATE_BASE
#define V4L2_CID_MEYE_SHARPNESS	(V4L2_CID_PRIVATE_BASE + 1)
#define V4L2_CID_PICTURE	(V4L2_CID_PRIVATE_BASE + 2)
#define V4L2_CID_JPEGQUAL	(V4L2_CID_PRIVATE_BASE + 3)
#define V4L2_CID_FRAMERATE	(V4L2_CID_PRIVATE_BASE + 4)

#endif
