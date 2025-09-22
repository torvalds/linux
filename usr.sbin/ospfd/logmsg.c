/*	$OpenBSD: logmsg.c,v 1.1 2016/09/02 14:04:25 benno Exp $ */

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

#include "ospfd.h"
#include "log.h"

/* names */
const char *
nbr_state_name(int state)
{
	switch (state) {
	case NBR_STA_DOWN:
		return ("DOWN");
	case NBR_STA_ATTEMPT:
		return ("ATTMP");
	case NBR_STA_INIT:
		return ("INIT");
	case NBR_STA_2_WAY:
		return ("2-WAY");
	case NBR_STA_XSTRT:
		return ("EXSTA");
	case NBR_STA_SNAP:
		return ("SNAP");
	case NBR_STA_XCHNG:
		return ("EXCHG");
	case NBR_STA_LOAD:
		return ("LOAD");
	case NBR_STA_FULL:
		return ("FULL");
	default:
		return ("UNKNW");
	}
}

const char *
if_state_name(int state)
{
	switch (state) {
	case IF_STA_DOWN:
		return ("DOWN");
	case IF_STA_LOOPBACK:
		return ("LOOP");
	case IF_STA_WAITING:
		return ("WAIT");
	case IF_STA_POINTTOPOINT:
		return ("P2P");
	case IF_STA_DROTHER:
		return ("OTHER");
	case IF_STA_BACKUP:
		return ("BCKUP");
	case IF_STA_DR:
		return ("DR");
	default:
		return ("UNKNW");
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
	case IF_TYPE_NBMA:
		return ("NBMA");
	case IF_TYPE_POINTOMULTIPOINT:
		return ("POINTOMULTIPOINT");
	case IF_TYPE_VIRTUALLINK:
		return ("VIRTUALLINK");
	}
	/* NOTREACHED */
	return ("UNKNOWN");
}

const char *
if_auth_name(enum auth_type type)
{
	switch (type) {
	case AUTH_NONE:
		return ("none");
	case AUTH_SIMPLE:
		return ("simple");
	case AUTH_CRYPT:
		return ("crypt");
	}
	/* NOTREACHED */
	return ("unknown");
}

const char *
dst_type_name(enum dst_type type)
{
	switch (type) {
	case DT_NET:
		return ("Network");
	case DT_RTR:
		return ("Router");
	}
	/* NOTREACHED */
	return ("unknown");
}

const char *
path_type_name(enum path_type type)
{
	switch (type) {
	case PT_INTRA_AREA:
		return ("Intra-Area");
	case PT_INTER_AREA:
		return ("Inter-Area");
	case PT_TYPE1_EXT:
		return ("Type 1 ext");
	case PT_TYPE2_EXT:
		return ("Type 2 ext");
	}
	/* NOTREACHED */
	return ("unknown");
}
