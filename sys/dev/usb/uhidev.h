/*	$OpenBSD: uhidev.h,v 1.41 2022/03/21 12:18:52 thfr Exp $	*/
/*	$NetBSD: uhidev.h,v 1.3 2002/10/08 09:56:17 dan Exp $	*/

/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

struct uhidev_softc {
	struct device sc_dev;		/* base device */
	struct usbd_device *sc_udev;
	struct usbd_interface *sc_iface;/* interface */
	int sc_ifaceno;			/* interface number */
	struct usbd_pipe *sc_ipipe;	/* input interrupt pipe */
	struct usbd_xfer *sc_ixfer;	/* read request */
	int sc_iep_addr;

	u_char *sc_ibuf;
	u_int sc_isize;

	struct usbd_pipe *sc_opipe;	/* output interrupt pipe */
	struct usbd_xfer *sc_oxfer;	/* write request */
	struct usbd_xfer *sc_owxfer;	/* internal write request */
	int sc_oep_addr;

	void *sc_repdesc;
	int sc_repdesc_size;

	u_int sc_nrepid;
	struct uhidev **sc_subdevs;

	int sc_refcnt;

	u_int sc_flags;
#define UHIDEV_F_XB1	0x0001		/* Xbox One controller */
};

struct uhidev {
	struct device sc_dev;		/* base device */
	struct usbd_device *sc_udev;	/* USB device */
	struct uhidev_softc *sc_parent;
	uByte sc_report_id;
	u_int8_t sc_state;
#define	UHIDEV_OPEN	0x01	/* device is open */
	void (*sc_intr)(struct uhidev *, void *, u_int);

	int sc_isize;
	int sc_osize;
	int sc_fsize;
};

struct uhidev_attach_arg {
	struct usb_attach_arg	*uaa;
	struct uhidev_softc	*parent;
	uint8_t			 reportid;
	u_int			 nreports;
	uint8_t			*claimed;
};

#define UHIDEV_CLAIM_MULTIPLE_REPORTID(u)	((u)->claimed != NULL)

int uhidev_report_type_conv(int);
void uhidev_get_report_desc(struct uhidev_softc *, void **, int *);
int uhidev_open(struct uhidev *);
void uhidev_close(struct uhidev *);
int uhidev_ioctl(struct uhidev *, u_long, caddr_t, int, struct proc *);
int uhidev_set_report(struct uhidev_softc *, int, int, void *, int);
int uhidev_set_report_async(struct uhidev_softc *, int, int, void *, int);
int uhidev_get_report(struct uhidev_softc *, int, int, void *, int);
int uhidev_get_report_async(struct uhidev_softc *, int, int, void *, int,
    void *, void (*)(void *, int, void *, int));
usbd_status uhidev_write(struct uhidev_softc *, void *, int);
int uhidev_set_report_dev(struct uhidev_softc *, struct uhidev *, int);
