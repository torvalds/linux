/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Including this file is mandatory for all USB related c-files in the loader.
 */

#ifndef _USB_FREEBSD_LOADER_H_
#define	_USB_FREEBSD_LOADER_H_

/* Default USB configuration */
#define	USB_HAVE_UGEN 0
#define	USB_HAVE_DEVCTL 0
#define	USB_HAVE_BUSDMA 1
#define	USB_HAVE_COMPAT_LINUX 0
#define	USB_HAVE_USER_IO 0
#define	USB_HAVE_MBUF 0
#define	USB_HAVE_TT_SUPPORT 1
#define	USB_HAVE_POWERD 1
#define	USB_HAVE_MSCTEST 1
#define	USB_HAVE_MSCTEST_DETACH 0
#define	USB_HAVE_PF 0
#define	USB_HAVE_ROOT_MOUNT_HOLD 0
#define	USB_HAVE_ID_SECTION 0
#define	USB_HAVE_PER_BUS_PROCESS 0
#define	USB_HAVE_FIXED_ENDPOINT 0
#define	USB_HAVE_FIXED_IFACE 0
#define	USB_HAVE_FIXED_CONFIG 0
#define	USB_HAVE_FIXED_PORT 0
#define	USB_HAVE_DISABLE_ENUM 0

#define	USB_CALLOUT_ZERO_TICKS 1

#define	USB_TD_GET_PROC(td) (td)->td_proc
#define	USB_PROC_GET_GID(td) (td)->p_pgid

#if (!defined(USB_HOST_ALIGN)) || (USB_HOST_ALIGN <= 0)
/* Use default value. */
#undef USB_HOST_ALIGN
#define	USB_HOST_ALIGN    8		/* bytes, must be power of two */
#endif
/* Sanity check for USB_HOST_ALIGN: Verify power of two. */
#if ((-USB_HOST_ALIGN) & USB_HOST_ALIGN) != USB_HOST_ALIGN
#error "USB_HOST_ALIGN is not power of two."
#endif
#define	USB_FS_ISOC_UFRAME_MAX 4	/* exclusive unit */
#define	USB_BUS_MAX 256			/* units */
#define	USB_MAX_DEVICES 128		/* units */
#define	USB_CONFIG_MAX 65535		/* bytes */
#define	USB_IFACE_MAX 32		/* units */
#define	USB_FIFO_MAX 128		/* units */
#define	USB_MAX_EP_UNITS 32		/* units */
#define	USB_MAX_EP_STREAMS 8		/* units */
#define	USB_MAX_PORTS 255		/* units */

#define	USB_MAX_FS_ISOC_FRAMES_PER_XFER (120)	/* units */
#define	USB_MAX_HS_ISOC_FRAMES_PER_XFER (8*120)	/* units */

#define	USB_HUB_MAX_DEPTH	5
#define	USB_EP0_BUFSIZE		1024	/* bytes */
#define	USB_CS_RESET_LIMIT	20	/* failures = 20 * 50 ms = 1sec */

#define	USB_MAX_AUTO_QUIRK	8	/* maximum number of dynamic quirks */

#define	USB_IN_POLLING_MODE_FUNC() 0
#define	USB_IN_POLLING_MODE_VALUE() 0

typedef uint32_t usb_timeout_t;		/* milliseconds */
typedef uint32_t usb_frlength_t;	/* bytes */
typedef uint32_t usb_frcount_t;		/* units */
typedef uint32_t usb_size_t;		/* bytes */
typedef uint32_t usb_ticks_t;		/* system defined */
typedef uint16_t usb_power_mask_t;	/* see "USB_HW_POWER_XXX" */
typedef uint16_t usb_stream_t;		/* stream ID */

#endif					/* _USB_FREEBSD_LOADER_H_ */
