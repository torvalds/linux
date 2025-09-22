/*	$OpenBSD: nec86var.h,v 1.2 2022/03/21 19:22:39 miod Exp $	*/
/*	$NecBSD: nec86var.h,v 1.6 1998/03/14 07:04:57 kmatsuda Exp $	*/
/*	$NetBSD$	*/

/*
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * nec86var.h
 *
 * NEC PC-9801-86 SoundBoard PCM driver for NetBSD/pc98.
 * Written by NAGAO Tadaaki, Feb 10, 1996.
 */

#ifndef	_NEC86VAR_H_
#define	_NEC86VAR_H_

struct nec86_softc {
	struct nec86hw_softc sc_nec86hw;

	bus_space_tag_t sc_n86iot;
	bus_space_handle_t sc_n86ioh;

	bus_space_tag_t sc_ym_iot;
	bus_space_handle_t sc_ym_iobase;
	bus_space_handle_t sc_ym_ioh;

	int sc_attached;
	int sc_intlevel;
};

extern  const struct  audio_hw_if nec86_hw_if;
int	nec86_probesubr(bus_space_tag_t, bus_space_handle_t,
	    bus_space_handle_t);
void	nec86_attachsubr(struct nec86_softc *);
#endif	/* !_NEC86VAR_H_ */
