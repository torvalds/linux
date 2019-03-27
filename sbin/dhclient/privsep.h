/*	$OpenBSD: privsep.h,v 1.2 2004/05/04 18:51:18 henning Exp $ */

/*
 * Copyright (c) 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE, ABUSE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <poll.h>
#include <pwd.h>

struct buf {
	u_char			*buf;
	size_t			 size;
	size_t			 wpos;
	size_t			 rpos;
};

enum imsg_code {
	IMSG_NONE,
	IMSG_SCRIPT_INIT,
	IMSG_SCRIPT_WRITE_PARAMS,
	IMSG_SCRIPT_GO,
	IMSG_SCRIPT_GO_RET,
	IMSG_SEND_PACKET,
	IMSG_SET_INTERFACE_MTU,
};

struct imsg_hdr {
	enum imsg_code	code;
	size_t		len;
};
