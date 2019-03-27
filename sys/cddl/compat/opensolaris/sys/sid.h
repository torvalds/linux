/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_SID_H_
#define	_OPENSOLARIS_SYS_SID_H_

typedef struct ksiddomain {
	char	*kd_name;	/* Domain part of SID */
	uint_t	kd_len;
} ksiddomain_t;
typedef void	ksid_t;

static __inline ksiddomain_t *
ksid_lookupdomain(const char *domain)
{
	ksiddomain_t *kd;
	size_t len;

	len = strlen(domain) + 1;
	kd = kmem_alloc(sizeof(*kd), KM_SLEEP);
	kd->kd_len = (uint_t)len;
	kd->kd_name = kmem_alloc(len, KM_SLEEP);
	strcpy(kd->kd_name, domain);
	return (kd);
}

static __inline void
ksiddomain_rele(ksiddomain_t *kd)
{

	kmem_free(kd->kd_name, kd->kd_len);
	kmem_free(kd, sizeof(*kd));
}

static __inline uint_t
ksid_getid(ksid_t *ks)
{

	panic("%s has been unexpectedly called", __func__);
}

static __inline const char *
ksid_getdomain(ksid_t *ks)
{

	panic("%s has been unexpectedly called", __func__);
}

static __inline uint_t
ksid_getrid(ksid_t *ks)
{

	panic("%s has been unexpectedly called", __func__);
}

#define	kidmap_getsidbyuid(zone, uid, sid_prefix, rid)	(1)
#define	kidmap_getsidbygid(zone, gid, sid_prefix, rid)	(1)

#endif	/* _OPENSOLARIS_SYS_SID_H_ */
