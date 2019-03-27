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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/signalvar.h>
#include <sys/mbuf.h>

#include <sys/iconv.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_subr.h>

static MALLOC_DEFINE(M_SMBDATA, "SMBDATA", "Misc netsmb data");
static MALLOC_DEFINE(M_SMBSTR, "SMBSTR", "netsmb string data");
MALLOC_DEFINE(M_SMBTEMP, "SMBTEMP", "Temp netsmb data");

smb_unichar smb_unieol = 0;

void
smb_makescred(struct smb_cred *scred, struct thread *td, struct ucred *cred)
{
	if (td) {
		scred->scr_td = td;
		scred->scr_cred = cred ? cred : td->td_ucred;
	} else {
		scred->scr_td = NULL;
		scred->scr_cred = cred ? cred : NULL;
	}
}

int
smb_td_intr(struct thread *td)
{
	struct proc *p;
	sigset_t tmpset;

	if (td == NULL)
		return 0;

	p = td->td_proc;
	PROC_LOCK(p);
	tmpset = p->p_siglist;
	SIGSETOR(tmpset, td->td_siglist);
	SIGSETNAND(tmpset, td->td_sigmask);
	mtx_lock(&p->p_sigacts->ps_mtx);
	SIGSETNAND(tmpset, p->p_sigacts->ps_sigignore);
	mtx_unlock(&p->p_sigacts->ps_mtx);
	if (SIGNOTEMPTY(td->td_siglist) && SMB_SIGMASK(tmpset)) {
		PROC_UNLOCK(p);
                return EINTR;
	}
	PROC_UNLOCK(p);
	return 0;
}

char *
smb_strdup(const char *s)
{
	char *p;
	size_t len;

	len = s ? strlen(s) + 1 : 1;
	p = malloc(len, M_SMBSTR, M_WAITOK);
	if (s)
		bcopy(s, p, len);
	else
		*p = 0;
	return p;
}

/*
 * duplicate string from a user space.
 */
char *
smb_strdupin(char *s, size_t maxlen)
{
	char *p;
	int error;

	p = malloc(maxlen + 1, M_SMBSTR, M_WAITOK);
	error = copyinstr(s, p, maxlen + 1, NULL);
	if (error) {
		free(p, M_SMBSTR);
		return (NULL);
	}
	return p;
}

/*
 * duplicate memory block from a user space.
 */
void *
smb_memdupin(void *umem, size_t len)
{
	char *p;

	if (len > 8 * 1024)
		return NULL;
	p = malloc(len, M_SMBSTR, M_WAITOK);
	if (copyin(umem, p, len) == 0)
		return p;
	free(p, M_SMBSTR);
	return NULL;
}

/*
 * duplicate memory block in the kernel space.
 */
void *
smb_memdup(const void *umem, int len)
{
	char *p;

	if (len > 8 * 1024)
		return NULL;
	p = malloc(len, M_SMBSTR, M_WAITOK);
	if (p == NULL)
		return NULL;
	bcopy(umem, p, len);
	return p;
}

void
smb_strfree(char *s)
{
	free(s, M_SMBSTR);
}

void
smb_memfree(void *s)
{
	free(s, M_SMBSTR);
}

void *
smb_zmalloc(size_t size, struct malloc_type *type, int flags)
{

	return malloc(size, type, flags | M_ZERO);
}

void
smb_strtouni(u_int16_t *dst, const char *src)
{
	while (*src) {
		*dst++ = htole16(*src++);
	}
	*dst = 0;
}

#ifdef SMB_SOCKETDATA_DEBUG
void
m_dumpm(struct mbuf *m) {
	char *p;
	size_t len;
	printf("d=");
	while(m) {
		p=mtod(m,char *);
		len=m->m_len;
		printf("(%zu)",len);
		while(len--){
			printf("%02x ",((int)*(p++)) & 0xff);
		}
		m=m->m_next;
	}
	printf("\n");
}
#endif

int
smb_maperror(int eclass, int eno)
{
	if (eclass == 0 && eno == 0)
		return 0;
	switch (eclass) {
	    case ERRDOS:
		switch (eno) {
		    case ERRbadfunc:
		    case ERRbadmcb:
		    case ERRbadenv:
		    case ERRbadformat:
		    case ERRrmuns:
			return EINVAL;
		    case ERRbadfile:
		    case ERRbadpath:
		    case ERRremcd:
		    case 66:		/* nt returns it when share not available */
		    case 67:		/* observed from nt4sp6 when sharename wrong */
			return ENOENT;
		    case ERRnofids:
			return EMFILE;
		    case ERRnoaccess:
		    case ERRbadshare:
			return EACCES;
		    case ERRbadfid:
			return EBADF;
		    case ERRnomem:
			return ENOMEM;	/* actually remote no mem... */
		    case ERRbadmem:
			return EFAULT;
		    case ERRbadaccess:
			return EACCES;
		    case ERRbaddata:
			return E2BIG;
		    case ERRbaddrive:
		    case ERRnotready:	/* nt */
			return ENXIO;
		    case ERRdiffdevice:
			return EXDEV;
		    case ERRnofiles:
			return 0;	/* eeof ? */
			return ETXTBSY;
		    case ERRlock:
			return EDEADLK;
		    case ERRfilexists:
			return EEXIST;
		    case 123:		/* dunno what is it, but samba maps as noent */
			return ENOENT;
		    case 145:		/* samba */
			return ENOTEMPTY;
		    case ERRnotlocked:
			return 0;	/* file become unlocked */
		    case 183:
			return EEXIST;
		    case ERRquota:
			return EDQUOT;
		}
		break;
	    case ERRSRV:
		switch (eno) {
		    case ERRerror:
			return EINVAL;
		    case ERRbadpw:
		    case ERRpasswordExpired:
			return EAUTH;
		    case ERRaccess:
			return EACCES;
		    case ERRinvnid:
			return ENETRESET;
		    case ERRinvnetname:
			SMBERROR("NetBIOS name is invalid\n");
			return EAUTH;
		    case 3:		/* reserved and returned */
			return EIO;
		    case ERRaccountExpired:
		    case ERRbadClient:
		    case ERRbadLogonTime:
			return EPERM;
		    case ERRnosupport:
			return EBADRPC;
		}
		break;
	    case ERRHRD:
		switch (eno) {
		    case ERRnowrite:
			return EROFS;
		    case ERRbadunit:
			return ENODEV;
		    case ERRnotready:
		    case ERRbadcmd:
		    case ERRdata:
			return EIO;
		    case ERRbadreq:
			return EBADRPC;
		    case ERRbadshare:
			return ETXTBSY;
		    case ERRlock:
			return EDEADLK;
		}
		break;
	}
	SMBERROR("Unmapped error %d:%d\n", eclass, eno);
	return EBADRPC;
}

static int
smb_copy_iconv(struct mbchain *mbp, c_caddr_t src, caddr_t dst,
    size_t *srclen, size_t *dstlen)
{
	int error;
	size_t inlen = *srclen, outlen = *dstlen;

	error = iconv_conv((struct iconv_drv*)mbp->mb_udata, &src, &inlen,
	    &dst, &outlen);
	if (inlen != *srclen || outlen != *dstlen) {
		*srclen -= inlen;
		*dstlen -= outlen;
		return 0;
	} else
		return error;
}

int
smb_put_dmem(struct mbchain *mbp, struct smb_vc *vcp, const char *src,
	size_t size, int caseopt)
{
	struct iconv_drv *dp = vcp->vc_toserver;

	if (size == 0)
		return 0;
	if (dp == NULL) {
		return mb_put_mem(mbp, src, size, MB_MSYSTEM);
	}
	mbp->mb_copy = smb_copy_iconv;
	mbp->mb_udata = dp;
	if (SMB_UNICODE_STRINGS(vcp))
		mb_put_padbyte(mbp);
	return mb_put_mem(mbp, src, size, MB_MCUSTOM);
}

int
smb_put_dstring(struct mbchain *mbp, struct smb_vc *vcp, const char *src,
	int caseopt)
{
	int error;

	error = smb_put_dmem(mbp, vcp, src, strlen(src), caseopt);
	if (error)
		return error;
	if (SMB_UNICODE_STRINGS(vcp))
		return mb_put_uint16le(mbp, 0);
	return mb_put_uint8(mbp, 0);
}

int
smb_put_asunistring(struct smb_rq *rqp, const char *src)
{
	struct mbchain *mbp = &rqp->sr_rq;
	struct iconv_drv *dp = rqp->sr_vc->vc_toserver;
	u_char c;
	int error;

	while (*src) {
		iconv_convmem(dp, &c, src++, 1);
		error = mb_put_uint16le(mbp, c);
		if (error)
			return error;
	}
	return mb_put_uint16le(mbp, 0);
}
