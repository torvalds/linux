/*	$OpenBSD: clnt_perror.c,v 1.25 2019/07/03 03:24:04 deraadt Exp $ */

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

static char *auth_errmsg(enum auth_stat stat);
#define CLNT_PERROR_BUFLEN 256

static char buf[CLNT_PERROR_BUFLEN];

/*
 * Print reply error info
 */
char *
clnt_sperror(CLIENT *rpch, char *s)
{
	char *err, *str = buf;
	struct rpc_err e;
	int ret, len = CLNT_PERROR_BUFLEN;

	CLNT_GETERR(rpch, &e);

	ret = snprintf(str, len, "%s: %s", s, clnt_sperrno(e.re_status));
	if (ret < 0)
		ret = 0;
	else if (ret >= len)
		goto truncated;
	str += ret;
	len -= ret;

	switch (e.re_status) {
	case RPC_SUCCESS:
	case RPC_CANTENCODEARGS:
	case RPC_CANTDECODERES:
	case RPC_TIMEDOUT:
	case RPC_PROGUNAVAIL:
	case RPC_PROCUNAVAIL:
	case RPC_CANTDECODEARGS:
	case RPC_SYSTEMERROR:
	case RPC_UNKNOWNHOST:
	case RPC_UNKNOWNPROTO:
	case RPC_PMAPFAILURE:
	case RPC_PROGNOTREGISTERED:
	case RPC_FAILED:
		break;

	case RPC_CANTSEND:
	case RPC_CANTRECV:
		ret = snprintf(str, len, "; errno = %s", strerror(e.re_errno));
		if (ret < 0 || ret >= len)
			goto truncated;
		break;

	case RPC_VERSMISMATCH:
		ret = snprintf(str, len,
		    "; low version = %u, high version = %u",
		    e.re_vers.low, e.re_vers.high);
		if (ret < 0 || ret >= len)
			goto truncated;
		break;

	case RPC_AUTHERROR:
		ret = snprintf(str, len, "; why = ");
		if (ret < 0)
			ret = 0;
		else if (ret >= len)
			goto truncated;
		str += ret;
		len -= ret;
		err = auth_errmsg(e.re_why);
		if (err != NULL) {
			ret = snprintf(str, len, "%s", err);
			if (ret < 0 || ret >= len)
				goto truncated;
		} else {
			ret = snprintf(str, len,
			    "(unknown authentication error - %d)",
			    (int) e.re_why);
			if (ret < 0 || ret >= len)
				goto truncated;
		}
		break;

	case RPC_PROGVERSMISMATCH:
		ret = snprintf(str, len,
		    "; low version = %u, high version = %u",
		    e.re_vers.low, e.re_vers.high);
		if (ret < 0 || ret >= len)
			goto truncated;
		break;

	default:	/* unknown */
		ret = snprintf(str, len, "; s1 = %u, s2 = %u",
		    e.re_lb.s1, e.re_lb.s2);
		if (ret < 0 || ret >= len)
			goto truncated;
		break;
	}
	if (strlcat(buf, "\n", CLNT_PERROR_BUFLEN) >= CLNT_PERROR_BUFLEN)
		goto truncated;
	return (buf);

truncated:
	snprintf(buf + CLNT_PERROR_BUFLEN - 5, 5, "...\n");
	return (buf);
}
DEF_WEAK(clnt_sperror);

void
clnt_perror(CLIENT *rpch, char *s)
{
	(void) fprintf(stderr, "%s", clnt_sperror(rpch, s));
}
DEF_WEAK(clnt_perror);

static const char *const rpc_errlist[] = {
	"RPC: Success",				/*  0 - RPC_SUCCESS */
	"RPC: Can't encode arguments",		/*  1 - RPC_CANTENCODEARGS */
	"RPC: Can't decode result",		/*  2 - RPC_CANTDECODERES */
	"RPC: Unable to send",			/*  3 - RPC_CANTSEND */
	"RPC: Unable to receive",		/*  4 - RPC_CANTRECV */
	"RPC: Timed out",			/*  5 - RPC_TIMEDOUT */
	"RPC: Incompatible versions of RPC",	/*  6 - RPC_VERSMISMATCH */
	"RPC: Authentication error",		/*  7 - RPC_AUTHERROR */
	"RPC: Program unavailable",		/*  8 - RPC_PROGUNAVAIL */
	"RPC: Program/version mismatch",	/*  9 - RPC_PROGVERSMISMATCH */
	"RPC: Procedure unavailable",		/* 10 - RPC_PROCUNAVAIL */
	"RPC: Server can't decode arguments",	/* 11 - RPC_CANTDECODEARGS */
	"RPC: Remote system error",		/* 12 - RPC_SYSTEMERROR */
	"RPC: Unknown host",			/* 13 - RPC_UNKNOWNHOST */
	"RPC: Port mapper failure",		/* 14 - RPC_PMAPFAILURE */
	"RPC: Program not registered",		/* 15 - RPC_PROGNOTREGISTERED */
	"RPC: Failed (unspecified error)",	/* 16 - RPC_FAILED */
	"RPC: Unknown protocol"			/* 17 - RPC_UNKNOWNPROTO */
};


/*
 * This interface for use by clntrpc
 */
char *
clnt_sperrno(enum clnt_stat stat)
{
	unsigned int errnum = stat;

	if (errnum < (sizeof(rpc_errlist)/sizeof(rpc_errlist[0])))
		return (char *)rpc_errlist[errnum];

	return ("RPC: (unknown error code)");
}
DEF_WEAK(clnt_sperrno);

void
clnt_perrno(enum clnt_stat num)
{
	(void) fprintf(stderr, "%s\n", clnt_sperrno(num));
}


char *
clnt_spcreateerror(char *s)
{
	switch (rpc_createerr.cf_stat) {
	case RPC_PMAPFAILURE:
		(void) snprintf(buf, CLNT_PERROR_BUFLEN, "%s: %s - %s\n", s,
		    clnt_sperrno(rpc_createerr.cf_stat),
		    clnt_sperrno(rpc_createerr.cf_error.re_status));
		break;

	case RPC_SYSTEMERROR:
		(void) snprintf(buf, CLNT_PERROR_BUFLEN, "%s: %s - %s\n", s,
		    clnt_sperrno(rpc_createerr.cf_stat),
		    strerror(rpc_createerr.cf_error.re_errno));
		break;

	default:
		(void) snprintf(buf, CLNT_PERROR_BUFLEN, "%s: %s\n", s,
		    clnt_sperrno(rpc_createerr.cf_stat));
		break;
	}
	buf[CLNT_PERROR_BUFLEN-2] = '\n';
	buf[CLNT_PERROR_BUFLEN-1] = '\0';
	return (buf);
}
DEF_WEAK(clnt_spcreateerror);

void
clnt_pcreateerror(char *s)
{
	fprintf(stderr, "%s", clnt_spcreateerror(s));
}
DEF_WEAK(clnt_pcreateerror);

static const char *const auth_errlist[] = {
	"Authentication OK",			/* 0 - AUTH_OK */
	"Invalid client credential",		/* 1 - AUTH_BADCRED */
	"Server rejected credential",		/* 2 - AUTH_REJECTEDCRED */
	"Invalid client verifier", 		/* 3 - AUTH_BADVERF */
	"Server rejected verifier", 		/* 4 - AUTH_REJECTEDVERF */
	"Client credential too weak",		/* 5 - AUTH_TOOWEAK */
	"Invalid server verifier",		/* 6 - AUTH_INVALIDRESP */
	"Failed (unspecified error)"		/* 7 - AUTH_FAILED */
};

static char *
auth_errmsg(enum auth_stat stat)
{
	unsigned int errnum = stat;

	if (errnum < (sizeof(auth_errlist)/sizeof(auth_errlist[0])))
		return (char *)auth_errlist[errnum];

	return (NULL);
}
