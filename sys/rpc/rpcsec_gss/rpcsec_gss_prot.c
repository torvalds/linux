/*
  rpcsec_gss_prot.c
  
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2000 The Regents of the University of Michigan.
  All rights reserved.
  
  Copyright (c) 2000 Dug Song <dugsong@UMICH.EDU>.
  All rights reserved, all wrongs reversed.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  $Id: authgss_prot.c,v 1.18 2000/09/01 04:14:03 dugsong Exp $
*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>

#include <rpc/rpc.h>
#include <rpc/rpcsec_gss.h>

#include "rpcsec_gss_int.h"

#define MAX_GSS_SIZE	10240	/* XXX */

#if 0				/* use the one from kgssapi */
bool_t
xdr_gss_buffer_desc(XDR *xdrs, gss_buffer_desc *p)
{
	char *val;
	u_int len;
	bool_t ret;

	val = p->value;
	len = p->length;
	ret = xdr_bytes(xdrs, &val, &len, MAX_GSS_SIZE);
	p->value = val;
	p->length = len;

	return (ret);
}
#endif

bool_t
xdr_rpc_gss_cred(XDR *xdrs, struct rpc_gss_cred *p)
{
	enum_t proc, svc;
	bool_t ret;

	proc = p->gc_proc;
	svc = p->gc_svc;
	ret = (xdr_u_int(xdrs, &p->gc_version) &&
	    xdr_enum(xdrs, &proc) &&
	    xdr_u_int(xdrs, &p->gc_seq) &&
	    xdr_enum(xdrs, &svc) &&
	    xdr_gss_buffer_desc(xdrs, &p->gc_handle));
	p->gc_proc = proc;
	p->gc_svc = svc;

	return (ret);
}

bool_t
xdr_rpc_gss_init_res(XDR *xdrs, struct rpc_gss_init_res *p)
{

	return (xdr_gss_buffer_desc(xdrs, &p->gr_handle) &&
	    xdr_u_int(xdrs, &p->gr_major) &&
	    xdr_u_int(xdrs, &p->gr_minor) &&
	    xdr_u_int(xdrs, &p->gr_win) &&
	    xdr_gss_buffer_desc(xdrs, &p->gr_token));
}

static void
put_uint32(struct mbuf **mp, uint32_t v)
{
	struct mbuf *m = *mp;
	uint32_t n;

	M_PREPEND(m, sizeof(uint32_t), M_WAITOK);
	n = htonl(v);
	bcopy(&n, mtod(m, uint32_t *), sizeof(uint32_t));
	*mp = m;
}

bool_t
xdr_rpc_gss_wrap_data(struct mbuf **argsp,
		      gss_ctx_id_t ctx, gss_qop_t qop,
		      rpc_gss_service_t svc, u_int seq)
{
	struct mbuf	*args, *mic;
	OM_uint32	maj_stat, min_stat;
	int		conf_state;
	u_int		len;
	static char	zpad[4];

	args = *argsp;

	/*
	 * Prepend the sequence number before calling gss_get_mic or gss_wrap.
	 */
	put_uint32(&args, seq);
	len = m_length(args, NULL);

	if (svc == rpc_gss_svc_integrity) {
		/* Checksum rpc_gss_data_t. */
		maj_stat = gss_get_mic_mbuf(&min_stat, ctx, qop, args, &mic);
		if (maj_stat != GSS_S_COMPLETE) {
			rpc_gss_log_debug("gss_get_mic failed");
			m_freem(args);
			return (FALSE);
		}

		/*
		 * Marshal databody_integ. Note that since args is
		 * already RPC encoded, there will be no padding.
		 */
		put_uint32(&args, len);

		/*
		 * Marshal checksum. This is likely to need padding.
		 */
		len = m_length(mic, NULL);
		put_uint32(&mic, len);
		if (len != RNDUP(len)) {
			m_append(mic, RNDUP(len) - len, zpad);
		}

		/*
		 * Concatenate databody_integ with checksum.
		 */
		m_cat(args, mic);
	} else if (svc == rpc_gss_svc_privacy) {
		/* Encrypt rpc_gss_data_t. */
		maj_stat = gss_wrap_mbuf(&min_stat, ctx, TRUE, qop,
		    &args, &conf_state);
		if (maj_stat != GSS_S_COMPLETE) {
			rpc_gss_log_status("gss_wrap", NULL,
			    maj_stat, min_stat);
			return (FALSE);
		}

		/*
		 *  Marshal databody_priv and deal with RPC padding.
		 */
		len = m_length(args, NULL);
		put_uint32(&args, len);
		if (len != RNDUP(len)) {
			m_append(args, RNDUP(len) - len, zpad);
		}
	}
	*argsp = args;
	return (TRUE);
}

static uint32_t
get_uint32(struct mbuf **mp)
{
	struct mbuf *m = *mp;
	uint32_t n;

	if (m->m_len < sizeof(uint32_t)) {
		m = m_pullup(m, sizeof(uint32_t));
		if (!m) {
			*mp = NULL;
			return (0);
		}
	}
	bcopy(mtod(m, uint32_t *), &n, sizeof(uint32_t));
	m_adj(m, sizeof(uint32_t));
	*mp = m;
	return (ntohl(n));
}

static void
m_trim(struct mbuf *m, int len)
{
	struct mbuf *n;
	int off;

	if (m == NULL)
		return;
	n = m_getptr(m, len, &off);
	if (n) {
		n->m_len = off;
		if (n->m_next) {
			m_freem(n->m_next);
			n->m_next = NULL;
		}
	}
}

bool_t
xdr_rpc_gss_unwrap_data(struct mbuf **resultsp,
			gss_ctx_id_t ctx, gss_qop_t qop,
			rpc_gss_service_t svc, u_int seq)
{
	struct mbuf	*results, *message, *mic;
	uint32_t	len, cklen;
	OM_uint32	maj_stat, min_stat;
	u_int		seq_num, conf_state, qop_state;

	results = *resultsp;
	*resultsp = NULL;
	
	message = NULL;
	if (svc == rpc_gss_svc_integrity) {
		/*
		 * Extract the seq+message part. Remember that there
		 * may be extra RPC padding in the checksum. The
		 * message part is RPC encoded already so no
		 * padding.
		 */
		len = get_uint32(&results);
		message = results;
		results = m_split(results, len, M_WAITOK);
		if (!results) {
			m_freem(message);
			return (FALSE);
		}

		/*
		 * Extract the MIC and make it contiguous.
		 */
		cklen = get_uint32(&results);
		if (!results) {
			m_freem(message);
			return (FALSE);
		}
		KASSERT(cklen <= MHLEN, ("unexpected large GSS-API checksum"));
		mic = results;
		if (cklen > mic->m_len) {
			mic = m_pullup(mic, cklen);
			if (!mic) {
				m_freem(message);
				return (FALSE);
			}
		}
		if (cklen != RNDUP(cklen))
			m_trim(mic, cklen);

		/* Verify checksum and QOP. */
		maj_stat = gss_verify_mic_mbuf(&min_stat, ctx,
		    message, mic, &qop_state);
		m_freem(mic);
		
		if (maj_stat != GSS_S_COMPLETE || qop_state != qop) {
			m_freem(message);
			rpc_gss_log_status("gss_verify_mic", NULL,
			    maj_stat, min_stat);
			return (FALSE);
		}
	} else if (svc == rpc_gss_svc_privacy) {
		/* Decode databody_priv. */
		len = get_uint32(&results);
		if (!results)
			return (FALSE);

		/* Decrypt databody. */
		message = results;
		if (len != RNDUP(len))
			m_trim(message, len);
		maj_stat = gss_unwrap_mbuf(&min_stat, ctx, &message,
		    &conf_state, &qop_state);
		
		/* Verify encryption and QOP. */
		if (maj_stat != GSS_S_COMPLETE) {
			rpc_gss_log_status("gss_unwrap", NULL,
			    maj_stat, min_stat);
			return (FALSE);
		}
		if (qop_state != qop || conf_state != TRUE) {
			m_freem(results);
			return (FALSE);
		}
	}

	/* Decode rpc_gss_data_t (sequence number + arguments). */
	seq_num = get_uint32(&message);
	if (!message)
		return (FALSE);
	
	/* Verify sequence number. */
	if (seq_num != seq) {
		rpc_gss_log_debug("wrong sequence number in databody");
		m_freem(message);
		return (FALSE);
	}

	*resultsp = message;
	return (TRUE);
}

#ifdef DEBUG
#include <machine/stdarg.h>

void
rpc_gss_log_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("rpcsec_gss: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

void
rpc_gss_log_status(const char *m, gss_OID mech, OM_uint32 maj_stat, OM_uint32 min_stat)
{
	OM_uint32 min;
	gss_buffer_desc msg;
	int msg_ctx = 0;

	printf("rpcsec_gss: %s: ", m);
	
	gss_display_status(&min, maj_stat, GSS_C_GSS_CODE, GSS_C_NULL_OID,
			   &msg_ctx, &msg);
	printf("%s - ", (char *)msg.value);
	gss_release_buffer(&min, &msg);

	gss_display_status(&min, min_stat, GSS_C_MECH_CODE, mech,
			   &msg_ctx, &msg);
	printf("%s\n", (char *)msg.value);
	gss_release_buffer(&min, &msg);
}

#else

void
rpc_gss_log_debug(__unused const char *fmt, ...)
{
}

void
rpc_gss_log_status(__unused const char *m, __unused gss_OID mech,
    __unused OM_uint32 maj_stat, __unused OM_uint32 min_stat)
{
}

#endif


