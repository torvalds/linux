/*	$OpenBSD: hildevs.h,v 1.4 2005/05/13 14:54:44 miod Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 */

/* Entries in hildevs_data.h for device probe */
struct	hildevice {
	int		minid;
	int		maxid;
	int		type;
	const char	*descr;
};

/* Arguments passed to attach routines */
struct hil_attach_args {
	int		ha_code;	/* hil code */
	int		ha_type;	/* hil device type */
	int		ha_console;	/* console set to hil */
	int		ha_infolen;	/* identify info length */
	u_int8_t	ha_info[HILBUFSIZE];	/* identify info bits */
#define	ha_id		ha_info[0]	/* hil probe id */

	const char	*ha_descr;	/* device description */
};

/* ha_type values */
#define	HIL_DEVICE_KEYBOARD	1
#define	HIL_DEVICE_IDMODULE	2
#define	HIL_DEVICE_MOUSE	3
#define	HIL_DEVICE_BUTTONBOX	4

/* Common softc part for hil devices */
struct hildev_softc {
	struct device	sc_dev;

	int		sc_code;	/* hil code */
	int		sc_type;	/* hil device type */

	int		sc_infolen;	/* identify info length */
	u_int8_t	sc_info[HILBUFSIZE];	/* identify info bits */

	void		(*sc_fn)(struct hildev_softc *, u_int, u_int8_t *);
};

#define	hd_code		sc_hildev.sc_code
#define	hd_fn		sc_hildev.sc_fn
#define	hd_info		sc_hildev.sc_info
#define	hd_infolen	sc_hildev.sc_infolen
#define	hd_parent	sc_hildev.sc_dev.dv_parent
#define	hd_type		sc_hildev.sc_type
