/*	$OpenBSD: logmsg.c,v 1.1 2016/09/02 16:20:34 benno Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "log.h"

/* names */
const char *
nbr_state_name(int state)
{
	switch (state) {
	case NBR_STA_DOWN:
		return ("DOWN");
	case NBR_STA_1_WAY:
		return ("1-WAY");
	case NBR_STA_2_WAY:
		return ("2-WAY");
	default:
		return ("UNKNOWN");
	}
}

const char *
if_state_name(int state)
{
	switch (state) {
	case IF_STA_DOWN:
		return ("DOWN");
	case IF_STA_QUERIER:
		return ("QUERIER");
	case IF_STA_NONQUERIER:
		return ("NONQUERIER");
	default:
		return ("UNKNOWN");
	}
}

const char *
group_state_name(int state)
{
	switch (state) {
	case GRP_STA_NO_MEMB_PRSNT:
		return ("NO MEMBER");
	case GRP_STA_MEMB_PRSNT:
		return ("MEMBER");
	case GRP_STA_V1_MEMB_PRSNT:
		return ("V1 MEMBER");
	case GRP_STA_CHECK_MEMB:
		return ("CHECKING");
	default:
		return ("UNKNOWN");
	}
}

const char *
if_type_name(enum iface_type type)
{
	switch (type) {
	case IF_TYPE_POINTOPOINT:
		return ("POINTOPOINT");
	case IF_TYPE_BROADCAST:
		return ("BROADCAST");
	}
	/* NOTREACHED */
	return ("UNKNOWN");
}
