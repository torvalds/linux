/*	$OpenBSD: uipc_proto.c,v 1.25 2022/11/13 16:01:32 mvs Exp $	*/
/*	$NetBSD: uipc_proto.c,v 1.8 1996/02/13 21:10:47 christos Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)uipc_proto.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/unpcb.h>

/*
 * Definitions of protocols supported in the UNIX domain.
 */

const struct protosw unixsw[] = {
{
  .pr_type	= SOCK_STREAM,
  .pr_domain	= &unixdomain,
  .pr_protocol	= PF_UNIX,
  .pr_flags	= PR_CONNREQUIRED|PR_WANTRCVD|PR_RIGHTS,
  .pr_usrreqs	= &uipc_usrreqs,
},
{
  .pr_type	= SOCK_SEQPACKET,
  .pr_domain	= &unixdomain,
  .pr_protocol	= PF_UNIX,
  .pr_flags	= PR_ATOMIC|PR_CONNREQUIRED|PR_WANTRCVD|PR_RIGHTS,
  .pr_usrreqs	= &uipc_usrreqs,
},
{
  .pr_type	= SOCK_DGRAM,
  .pr_domain	= &unixdomain,
  .pr_protocol	= PF_UNIX,
  .pr_flags	= PR_ATOMIC|PR_ADDR|PR_RIGHTS,
  .pr_usrreqs	= &uipc_dgram_usrreqs,
}
};

const struct domain unixdomain = {
  .dom_family = AF_UNIX,
  .dom_name = "unix",
  .dom_init = unp_init,
  .dom_externalize = unp_externalize,
  .dom_dispose = unp_dispose,
  .dom_protosw = unixsw,
  .dom_protoswNPROTOSW = &unixsw[nitems(unixsw)]
};
