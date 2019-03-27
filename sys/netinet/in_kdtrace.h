/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Mark Johnston <markj@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
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

#ifndef _SYS_IN_KDTRACE_H_
#define	_SYS_IN_KDTRACE_H_

#define	IP_PROBE(probe, arg0, arg1, arg2, arg3, arg4, arg5)		\
	SDT_PROBE6(ip, , , probe, arg0, arg1, arg2, arg3, arg4, arg5)
#define	UDP_PROBE(probe, arg0, arg1, arg2, arg3, arg4)			\
	SDT_PROBE5(udp, , , probe, arg0, arg1, arg2, arg3, arg4)
#define	UDPLITE_PROBE(probe, arg0, arg1, arg2, arg3, arg4)		\
	SDT_PROBE5(udplite, , , probe, arg0, arg1, arg2, arg3, arg4)
#define	TCP_PROBE1(probe, arg0)						\
	SDT_PROBE1(tcp, , , probe, arg0)
#define	TCP_PROBE2(probe, arg0, arg1)					\
	SDT_PROBE2(tcp, , , probe, arg0, arg1)
#define	TCP_PROBE3(probe, arg0, arg1, arg2)				\
	SDT_PROBE3(tcp, , , probe, arg0, arg1, arg2)
#define	TCP_PROBE4(probe, arg0, arg1, arg2, arg3)			\
	SDT_PROBE4(tcp, , , probe, arg0, arg1, arg2, arg3)
#define	TCP_PROBE5(probe, arg0, arg1, arg2, arg3, arg4)			\
	SDT_PROBE5(tcp, , , probe, arg0, arg1, arg2, arg3, arg4)
#define	TCP_PROBE6(probe, arg0, arg1, arg2, arg3, arg4, arg5)		\
	SDT_PROBE6(tcp, , , probe, arg0, arg1, arg2, arg3, arg4, arg5)
#define	SCTP_PROBE1(probe, arg0)					\
	SDT_PROBE1(sctp, , , probe, arg0)
#define	SCTP_PROBE2(probe, arg0, arg1)					\
	SDT_PROBE2(sctp, , , probe, arg0, arg1)
#define	SCTP_PROBE3(probe, arg0, arg1, arg2)				\
	SDT_PROBE3(sctp, , , probe, arg0, arg1, arg2)
#define	SCTP_PROBE4(probe, arg0, arg1, arg2, arg3)			\
	SDT_PROBE4(sctp, , , probe, arg0, arg1, arg2, arg3)
#define	SCTP_PROBE5(probe, arg0, arg1, arg2, arg3, arg4)		\
	SDT_PROBE5(sctp, , , probe, arg0, arg1, arg2, arg3, arg4)
#define	SCTP_PROBE6(probe, arg0, arg1, arg2, arg3, arg4, arg5)		\
	SDT_PROBE6(sctp, , , probe, arg0, arg1, arg2, arg3, arg4, arg5)

SDT_PROVIDER_DECLARE(ip);
SDT_PROVIDER_DECLARE(sctp);
SDT_PROVIDER_DECLARE(tcp);
SDT_PROVIDER_DECLARE(udp);
SDT_PROVIDER_DECLARE(udplite);

SDT_PROBE_DECLARE(ip, , , receive);
SDT_PROBE_DECLARE(ip, , , send);

SDT_PROBE_DECLARE(sctp, , , receive);
SDT_PROBE_DECLARE(sctp, , , send);
SDT_PROBE_DECLARE(sctp, , , state__change);

SDT_PROBE_DECLARE(tcp, , , accept__established);
SDT_PROBE_DECLARE(tcp, , , accept__refused);
SDT_PROBE_DECLARE(tcp, , , connect__established);
SDT_PROBE_DECLARE(tcp, , , connect__refused);
SDT_PROBE_DECLARE(tcp, , , connect__request);
SDT_PROBE_DECLARE(tcp, , , receive);
SDT_PROBE_DECLARE(tcp, , , send);
SDT_PROBE_DECLARE(tcp, , , siftr);
SDT_PROBE_DECLARE(tcp, , , state__change);
SDT_PROBE_DECLARE(tcp, , , debug__input);
SDT_PROBE_DECLARE(tcp, , , debug__output);
SDT_PROBE_DECLARE(tcp, , , debug__user);
SDT_PROBE_DECLARE(tcp, , , debug__drop);
SDT_PROBE_DECLARE(tcp, , , receive__autoresize);

SDT_PROBE_DECLARE(udp, , , receive);
SDT_PROBE_DECLARE(udp, , , send);

SDT_PROBE_DECLARE(udplite, , , receive);
SDT_PROBE_DECLARE(udplite, , , send);

#endif
