/*	$OpenBSD: radiusd.h,v 1.9 2024/07/14 15:27:57 yasuoka Exp $	*/

#ifndef	RADIUSD_H
#define	RADIUSD_H 1
/*
 * Copyright (c) 2013 Internet Initiative Japan Inc.
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
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#define	RADIUSD_MODULE_NAME_LEN		32
#define	RADIUSD_SECRET_MAX		128
#define	RADIUSD_SOCK			"/var/run/radiusd.sock"
#define	RADIUSD_USER			"_radiusd"

enum imsg_type {
	IMSG_NONE = 0,
	IMSG_OK,
	IMSG_NG,
	IMSG_RADIUSD_MODULE_LOAD,
	IMSG_RADIUSD_MODULE_SET_CONFIG,
	IMSG_RADIUSD_MODULE_START,
	IMSG_RADIUSD_MODULE_NOTIFY_SECRET,
	IMSG_RADIUSD_MODULE_USERPASS,
	IMSG_RADIUSD_MODULE_USERPASS_OK,
	IMSG_RADIUSD_MODULE_USERPASS_FAIL,
	IMSG_RADIUSD_MODULE_ACCSREQ,
	/* Check the response's authenticator if the module doesn't */
	IMSG_RADIUSD_MODULE_ACCSREQ_ANSWER,
	IMSG_RADIUSD_MODULE_ACCSREQ_ABORTED,
	IMSG_RADIUSD_MODULE_ACCSREQ_NEXT, /* fall through to the next auth */
	IMSG_RADIUSD_MODULE_NEXTRES,	  /* receive the respo from tht next */
	IMSG_RADIUSD_MODULE_REQDECO,
	IMSG_RADIUSD_MODULE_REQDECO_DONE,
	IMSG_RADIUSD_MODULE_RESDECO0_REQ, /* request pkt for RESDECO */
	IMSG_RADIUSD_MODULE_RESDECO,
	IMSG_RADIUSD_MODULE_RESDECO_DONE,
	IMSG_RADIUSD_MODULE_ACCTREQ,
	IMSG_RADIUSD_MODULE_CTRL_BIND,		/* request by module */
	IMSG_RADIUSD_MODULE_CTRL_UNBIND,	/* notice by control */
	IMSG_RADIUSD_MODULE_STOP,
	IMSG_RADIUSD_MODULE_MIN = 10000
};

/* Module sends LOAD when it becomes ready */
struct radiusd_module_load_arg {
	uint32_t	cap;	/* module capability bits */
#define RADIUSD_MODULE_CAP_USERPASS	0x01
#define RADIUSD_MODULE_CAP_ACCSREQ	0x02
#define RADIUSD_MODULE_CAP_REQDECO	0x04
#define RADIUSD_MODULE_CAP_RESDECO	0x08
#define RADIUSD_MODULE_CAP_ACCTREQ	0x10
#define RADIUSD_MODULE_CAP_CONTROL	0x20
#define RADIUSD_MODULE_CAP_NEXTRES	0x40
};

struct radiusd_module_object {
	size_t	size;
};

struct radiusd_module_set_arg {
	char	paramname[32];
	u_int	nparamval;
};

struct radiusd_module_userpass_arg {
	u_int	q_id;
	bool	has_pass;
	char	user[256];
	char	pass[256];
};

struct radiusd_module_radpkt_arg {
	u_int	q_id;
	bool	final;
	int	pktlen;		/* total length of radpkt */
};

#endif
