/*	$OpenBSD: af_frame.c,v 1.3 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2024 David Gwynne <dlg@openbsd.org>
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

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>

#include <net/if_types.h>

const struct domain framedomain;

/* reach over to if_ethersubr.c */
int ether_frm_ctloutput(int, struct socket *, int, int, struct mbuf *);
extern const struct pr_usrreqs ether_frm_usrreqs;

static const struct protosw framesw[] = {
	{
		.pr_type	= SOCK_DGRAM,
		.pr_domain	= &framedomain,
		.pr_protocol	= IFT_ETHER,
		.pr_flags	= PR_ATOMIC|PR_ADDR|PR_MPINPUT,

		.pr_ctloutput	= ether_frm_ctloutput,
		.pr_usrreqs	= &ether_frm_usrreqs,
		.pr_sysctl	= NULL /* ether_frm_sysctl */,
	},
};

const struct domain framedomain = {
	.dom_family	= AF_FRAME,
	.dom_name	= "frame",
	.dom_protosw 	= framesw,
	.dom_protoswNPROTOSW = &framesw[nitems(framesw)],
};

void
af_frameattach(int n)
{
	/* nop */
}
