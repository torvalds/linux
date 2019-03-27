/*
 * Simple netbios-dgm transparent proxy for in-kernel use.
 * For use with the NAT code.
 * $Id$
 */

/*-
 * Copyright (c) 2002-2003 Paul J. Ledbetter III
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
 * $Id$
 */

#define	IPF_NETBIOS_PROXY

void ipf_p_netbios_main_load __P((void));
void ipf_p_netbios_main_unload __P((void));
int ipf_p_netbios_out __P((void *, fr_info_t *, ap_session_t *, nat_t *));

static	frentry_t	netbiosfr;

int	netbios_proxy_init = 0;

/*
 * Initialize local structures.
 */
void
ipf_p_netbios_main_load()
{
	bzero((char *)&netbiosfr, sizeof(netbiosfr));
	netbiosfr.fr_ref = 1;
	netbiosfr.fr_flags = FR_INQUE|FR_PASS|FR_QUICK|FR_KEEPSTATE;
	MUTEX_INIT(&netbiosfr.fr_lock, "NETBIOS proxy rule lock");
	netbios_proxy_init = 1;
}


void
ipf_p_netbios_main_unload()
{
	if (netbios_proxy_init == 1) {
		MUTEX_DESTROY(&netbiosfr.fr_lock);
		netbios_proxy_init = 0;
	}
}


int
ipf_p_netbios_out(arg, fin, aps, nat)
	void *arg;
	fr_info_t *fin;
	ap_session_t *aps;
	nat_t *nat;
{
	char dgmbuf[6];
	int off, dlen;
	udphdr_t *udp;
	ip_t *ip;
	mb_t *m;

	aps = aps;	/* LINT */
	nat = nat;	/* LINT */

	m = fin->fin_m;
	dlen = fin->fin_dlen - sizeof(*udp);
	/*
	 * no net bios datagram could possibly be shorter than this
	 */
	if (dlen < 11)
		return 0;

	ip = fin->fin_ip;
	udp = (udphdr_t *)fin->fin_dp;
	off = (char *)udp - (char *)ip + sizeof(*udp) + fin->fin_ipoff;

	/*
	 * move past the
	 *	ip header;
	 *	udp header;
	 *	4 bytes into the net bios dgm header.
	 *  According to rfc1002, this should be the exact location of
	 *  the source address/port
	 */
	off += 4;

	/* Copy NATed source Address/port*/
	dgmbuf[0] = (char)((ip->ip_src.s_addr     ) &0xFF);
	dgmbuf[1] = (char)((ip->ip_src.s_addr >> 8) &0xFF);
	dgmbuf[2] = (char)((ip->ip_src.s_addr >> 16)&0xFF);
	dgmbuf[3] = (char)((ip->ip_src.s_addr >> 24)&0xFF);

	dgmbuf[4] = (char)((udp->uh_sport     )&0xFF);
	dgmbuf[5] = (char)((udp->uh_sport >> 8)&0xFF);

	/* replace data in packet */
	COPYBACK(m, off, sizeof(dgmbuf), dgmbuf);

	return 0;
}
