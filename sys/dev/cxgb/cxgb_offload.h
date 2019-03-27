/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2008, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$

***************************************************************************/

#ifndef _CXGB_OFFLOAD_H
#define _CXGB_OFFLOAD_H

#ifdef TCP_OFFLOAD
enum {
	ULD_TOM = 1,
	ULD_IWARP = 2,
};

struct adapter;
struct uld_info {
	SLIST_ENTRY(uld_info) link;
	int refcount;
	int uld_id;
	int (*activate)(struct adapter *);
	int (*deactivate)(struct adapter *);
};

struct tom_tunables {
	int sndbuf;
	int ddp;
	int indsz;
	int ddp_thres;
};

/* CPL message priority levels */
enum {
	CPL_PRIORITY_DATA = 0,     /* data messages */
	CPL_PRIORITY_CONTROL = 1   /* offload control messages */
};

#define S_HDR_NDESC	0
#define M_HDR_NDESC	0xf
#define V_HDR_NDESC(x)	((x) << S_HDR_NDESC)
#define G_HDR_NDESC(x)	(((x) >> S_HDR_NDESC) & M_HDR_NDESC)

#define S_HDR_QSET	4
#define M_HDR_QSET	0xf
#define V_HDR_QSET(x)	((x) << S_HDR_QSET)
#define G_HDR_QSET(x)	(((x) >> S_HDR_QSET) & M_HDR_QSET)

#define S_HDR_CTRL	8
#define V_HDR_CTRL(x)	((x) << S_HDR_CTRL)
#define F_HDR_CTRL	V_HDR_CTRL(1U)

#define S_HDR_DF	9
#define V_HDR_DF(x)	((x) << S_HDR_DF)
#define F_HDR_DF	V_HDR_DF(1U)

#define S_HDR_SGL	10
#define V_HDR_SGL(x)	((x) << S_HDR_SGL)
#define F_HDR_SGL	V_HDR_SGL(1U)

struct ofld_hdr
{
	void *sgl;	/* SGL, if F_HDR_SGL set in flags */
	int plen;	/* amount of payload (in bytes) */
	int flags;
};

/*
 * Convenience function for fixed size CPLs that fit in 1 desc.
 */
#define M_GETHDR_OFLD(qset, ctrl, cpl) \
    m_gethdr_ofld(qset, ctrl, sizeof(*cpl), (void **)&cpl)
static inline struct mbuf *
m_gethdr_ofld(int qset, int ctrl, int cpllen, void **cpl)
{
	struct mbuf *m;
	struct ofld_hdr *oh;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);

	oh = mtod(m, struct ofld_hdr *);
	oh->flags = V_HDR_NDESC(1) | V_HDR_QSET(qset) | V_HDR_CTRL(ctrl);
	*cpl = (void *)(oh + 1);
	m->m_pkthdr.len = m->m_len = sizeof(*oh) + cpllen;

	return (m);
}

int t3_register_uld(struct uld_info *);
int t3_unregister_uld(struct uld_info *);
int t3_activate_uld(struct adapter *, int);
int t3_deactivate_uld(struct adapter *, int);
#endif	/* TCP_OFFLOAD */

#define CXGB_UNIMPLEMENTED() \
    panic("IMPLEMENT: %s:%s:%d", __FUNCTION__, __FILE__, __LINE__)

#endif
