/*	$OpenBSD: logmsg.c,v 1.3 2024/11/21 13:34:51 claudio Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

#include <sys/un.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lpd.h"

#include "io.h"
#include "log.h"
#include "proc.h"

const char *
log_fmt_proto(int p)
{
	switch (p) {
	case PROTO_LPR:
		return "lpr";
	default:
		return NULL;
	}
};

const char *
log_fmt_imsgtype(int type)
{
	static char buf[16];

	switch (type) {
	case IMSG_NONE:
		return "IMSG_NONE";
	case IMSG_SOCK_ENGINE:
		return "IMSG_SOCK_ENGINE";
	case IMSG_SOCK_FRONTEND:
		return "IMSG_SOCK_FRONTEND";
	case IMSG_CONF_START:
		return "IMSG_CONF_START";
	case IMSG_CONF_LISTENER:
		return "IMSG_CONF_LISTENER";
	case IMSG_CONF_END:
		return "IMSG_CONF_END";
	case IMSG_GETADDRINFO:
		return "IMSG_GETADDRINFO";
	case IMSG_GETADDRINFO_END:
		return "IMSG_GETADDRINFO_END";
	case IMSG_GETNAMEINFO:
		return "IMSG_GETNAMEINFO";
	case IMSG_LPR_ALLOWEDHOST:
		return "IMSG_LPR_ALLOWEDHOST";
	case IMSG_LPR_DISPLAYQ:
		return "IMSG_LPR_DISPLAYQ";
	case IMSG_LPR_PRINTJOB:
		return "IMSG_LPR_PRINTJOB";
	case IMSG_LPR_RECVJOB:
		return "IMSG_LPR_RECVJOB";
	case IMSG_LPR_RECVJOB_CLEAR:
		return "IMSG_LPR_RECVJOB_CLEAR";
	case IMSG_LPR_RECVJOB_CF:
		return "IMSG_LPR_RECVJOB_CF";
	case IMSG_LPR_RECVJOB_DF:
		return "IMSG_LPR_RECVJOB_DF";
	case IMSG_LPR_RECVJOB_COMMIT:
		return "IMSG_LPR_RECVJOB_COMMIT";
	case IMSG_LPR_RECVJOB_ROLLBACK:
		return "IMSG_LPR_RECVJOB_ROLLBACK";
	case IMSG_LPR_RMJOB:
		return "IMSG_LPR_RMJOB";
	default:
		snprintf(buf, sizeof(buf), "?%d", type);
		return buf;
	}
}

const char *
log_fmt_proctype(int proctype)
{
	switch (proctype) {
	case PROC_CLIENT:
		return "client";
	case PROC_CONTROL:
		return "control";
	case PROC_ENGINE:
		return "engine";
	case PROC_FRONTEND:
		return "frontend";
	case PROC_PRINTER:
		return "printer";
	case PROC_PRIV:
		return "priv";
	default:
		return NULL;
	}
};

const char *
log_fmt_sockaddr(const struct sockaddr *sa)
{
	static char buf[PATH_MAX];
	char host[NI_MAXHOST], serv[NI_MAXSERV];

	switch (sa->sa_family) {
	case AF_LOCAL:
		(void)strlcpy(buf, ((const struct sockaddr_un*)sa)->sun_path,
		    sizeof(buf));
		return buf;

	case AF_INET:
	case AF_INET6:
		if (getnameinfo(sa, sa->sa_len, host, sizeof(host),
		    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV)) {
			log_warnx("%s: getnameinfo", __func__);
			return NULL;
		}
		if (sa->sa_family == AF_INET6)
			snprintf(buf, sizeof(buf), "[%s]:%s", host, serv);
		else
			snprintf(buf, sizeof(buf), "%s:%s", host, serv);
		return buf;

	default:
		return NULL;
	}
}

void
log_imsg(struct imsgproc *proc, struct imsg *imsg)
{
	if (imsg == NULL)
		log_debug("imsg src=%s closed",
		    log_fmt_proctype(proc_gettype(proc)));
	else
		log_debug("imsg src=%s type=%s len=%d",
		    log_fmt_proctype(proc_gettype(proc)),
		    log_fmt_imsgtype(imsg->hdr.type),
		    imsg->hdr.len);
}

void
log_io(const char *name, struct io *io, int ev)
{
	log_debug("io %s evt=%s io=%s", name, io_strevent(ev),
	    io_strio(io));
}
