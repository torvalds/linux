/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clock.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/iconv.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_dev.h>

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>
#include <fs/smbfs/smbfs_subr.h>

MALLOC_DEFINE(M_SMBFSDATA, "smbfs_data", "SMBFS private data");
MALLOC_DEFINE(M_SMBFSCRED, "smbfs_cred", "SMBFS cred data");

void
smb_time_local2server(struct timespec *tsp, int tzoff, u_long *seconds)
{
	*seconds = tsp->tv_sec - tzoff * 60 /*- tz_minuteswest * 60 -
	    (wall_cmos_clock ? adjkerntz : 0)*/;
}

void
smb_time_server2local(u_long seconds, int tzoff, struct timespec *tsp)
{
	tsp->tv_sec = seconds + tzoff * 60;
}

/*
 * Number of seconds between 1970 and 1601 year
 */
static int64_t DIFF1970TO1601 = 11644473600ULL;

/*
 * Time from server comes as UTC, so no need to use tz
 */
void
smb_time_NT2local(int64_t nsec, int tzoff, struct timespec *tsp)
{
	smb_time_server2local(nsec / 10000000 - DIFF1970TO1601, 0, tsp);
}

void
smb_time_local2NT(struct timespec *tsp, int tzoff, int64_t *nsec)
{
	u_long seconds;

	smb_time_local2server(tsp, 0, &seconds);
	*nsec = (((int64_t)(seconds) & ~1) + DIFF1970TO1601) * (int64_t)10000000;
}

void
smb_time_unix2dos(struct timespec *tsp, int tzoff, u_int16_t *ddp, 
	u_int16_t *dtp,	u_int8_t *dhp)
{
	struct timespec tt;
	u_long t;

	tt = *tsp;
	smb_time_local2server(tsp, tzoff, &t);
	tt.tv_sec = t;
	timespec2fattime(&tt, 1, ddp, dtp, dhp);
}

void
smb_dos2unixtime(u_int dd, u_int dt, u_int dh, int tzoff,
	struct timespec *tsp)
{

	fattime2timespec(dd, dt, dh, 1, tsp);
	smb_time_server2local(tsp->tv_sec, tzoff, tsp);
}

int
smbfs_fullpath(struct mbchain *mbp, struct smb_vc *vcp, struct smbnode *dnp,
	const char *name, int nmlen)
{
	int caseopt = SMB_CS_NONE;
	int error;

	if (SMB_UNICODE_STRINGS(vcp)) {
		error = mb_put_padbyte(mbp);
		if (error)
			return error;
	}
	if (SMB_DIALECT(vcp) < SMB_DIALECT_LANMAN1_0)
		caseopt |= SMB_CS_UPPER;
	if (dnp != NULL) {
		error = smb_put_dmem(mbp, vcp, dnp->n_rpath, dnp->n_rplen, 
		    caseopt);
		if (error)
			return error;
		if (name) {
			/* Put the separator */
			if (SMB_UNICODE_STRINGS(vcp))
				error = mb_put_uint16le(mbp, '\\');
			else
				error = mb_put_uint8(mbp, '\\');
			if (error)
				return error;
			/* Put the name */
			error = smb_put_dmem(mbp, vcp, name, nmlen, caseopt);
			if (error)
				return error;
		}
	}
	/* Put NULL terminator. */
	if (SMB_UNICODE_STRINGS(vcp))
		error = mb_put_uint16le(mbp, 0);
	else
		error = mb_put_uint8(mbp, 0);
	return error;
}

int
smbfs_fname_tolocal(struct smb_vc *vcp, char *name, int *nmlen, int caseopt)
{
	int copt = (caseopt == SMB_CS_LOWER ? KICONV_FROM_LOWER : 
		    (caseopt == SMB_CS_UPPER ? KICONV_FROM_UPPER : 0));
	int error = 0;
	size_t ilen = *nmlen;
	size_t olen;
	char *ibuf = name;
	char outbuf[SMB_MAXFNAMELEN];
	char *obuf = outbuf;

	if (vcp->vc_tolocal) {
		olen = sizeof(outbuf);
		bzero(outbuf, sizeof(outbuf));

		/*
		error = iconv_conv_case
			(vcp->vc_tolocal, NULL, NULL, &obuf, &olen, copt);
		if (error) return error;
		*/

		error = iconv_conv_case(vcp->vc_tolocal,
		    __DECONST(const char **, &ibuf), &ilen, &obuf, &olen, copt);
		if (error && SMB_UNICODE_STRINGS(vcp)) {
			/*
			 * If using unicode, leaving a file name as it was when
			 * convert fails will cause a problem because the file name
			 * will contain NULL.
			 * Here, put '?' and give converted file name.
			 */
			*obuf = '?';
			olen--;
			error = 0;
		}
		if (!error) {
			*nmlen = sizeof(outbuf) - olen;
			memcpy(name, outbuf, *nmlen);
		}
	}
	return error;
}

void *
smbfs_malloc_scred(void)
{
	return (malloc(sizeof(struct smb_cred), M_SMBFSCRED, M_WAITOK));
}

void
smbfs_free_scred(void *scred)
{
	free(scred, M_SMBFSCRED);
}
