/*	$OpenBSD: mpls_shim.c,v 1.8 2014/12/05 15:50:04 mpi Exp $	*/

/*
 * Copyright (C) 1999, 2000 and 2001 AYAME Project, WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netmpls/mpls.h>

struct mbuf *
mpls_shim_pop(struct mbuf *m)
{
	/* shaves off top shim header from mbuf */
	m_adj(m, sizeof(struct shim_hdr));

	/* catch-up next shim_hdr */
	if (m->m_len < sizeof(struct shim_hdr))
		if ((m = m_pullup(m, sizeof(struct shim_hdr))) == NULL)
			return (NULL);

	/* return mbuf */
	return (m);
}

struct mbuf *
mpls_shim_swap(struct mbuf *m, struct rt_mpls *rt_mpls)
{
	struct shim_hdr *shim;

	/* pullup shim_hdr */
	if (m->m_len < sizeof(struct shim_hdr))
		if ((m = m_pullup(m, sizeof(struct shim_hdr))) == NULL)
			return (NULL);
	shim = mtod(m, struct shim_hdr *);

	/* swap label */
	shim->shim_label &= ~MPLS_LABEL_MASK;
	shim->shim_label |= rt_mpls->mpls_label & MPLS_LABEL_MASK;

	/* swap exp : XXX exp override */
	{
		u_int32_t	t;

		shim->shim_label &= ~MPLS_EXP_MASK;
		t = rt_mpls->mpls_exp << MPLS_EXP_OFFSET;
		shim->shim_label |= htonl(t) & MPLS_EXP_MASK;
	}

	return (m);
}

struct mbuf *
mpls_shim_push(struct mbuf *m, struct rt_mpls *rt_mpls)
{
	struct shim_hdr *shim;

	M_PREPEND(m, sizeof(struct shim_hdr), M_DONTWAIT);
	if (m == NULL)
		return (NULL);

	shim = mtod(m, struct shim_hdr *);
	bzero((caddr_t)shim, sizeof(*shim));

	return (mpls_shim_swap(m, rt_mpls));
}
