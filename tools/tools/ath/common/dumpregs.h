#ifndef _DUMPREGS_
#define	_DUMPREGS_
/*-
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#define	__constructor	__attribute__((constructor))

struct dumpreg {
	uint32_t	addr;
	const char	*name;
	const char	*bits;
	int		type;
	u_int		srevMin, srevMax;
	u_int		phyMin, phyMax;
};
#define	SREV(v,r)	(((v) << 16) | (r))
#define	MAC_MATCH(dr, mv, mr) \
	((dr)->srevMin <= SREV(mv,mr) && SREV(mv,mr) < (dr)->srevMax)

#define	PHY_MATCH(dr, pr) \
	((dr)->phyMin <= (pr) && (pr) < (dr)->phyMax)
#define	PHYANY	0,0xffff

enum {
	DUMP_BASIC	= 0x0001,	/* basic/default registers */
	DUMP_KEYCACHE	= 0x0002,	/* key cache */
	DUMP_BASEBAND	= 0x0004,	/* baseband */
	DUMP_INTERRUPT	= 0x0008,	/* interrupt state */
	DUMP_XR		= 0x0010,	/* XR state */
	DUMP_QCU	= 0x0020,	/* QCU state */
	DUMP_DCU	= 0x0040,	/* DCU state */

	DUMP_PUBLIC	= 0x0061,	/* public = BASIC+QCU+DCU */
	DUMP_ALL	= 0xffff
};

#define	_DEFREG(_addr, _name, _type) \
    { .addr = _addr, .name = _name, .type = _type }
#define	_DEFREGx(_addr, _name, _type, _srevmin, _srevmax) \
    { .addr = _addr, .name = _name, .type = _type, \
     .srevMin = _srevmin, .srevMax = _srevmax }
#define	_DEFREGfmt(_addr, _name, _type, _fmt) \
    { .addr = _addr, .name = _name, .type = _type, .bits = _fmt }
#define	DEFVOID(_addr, _name)	_DEFREG(_addr, _name, 0)
#define	DEFVOIDx(_addr, _name, _smin, _smax) \
	__DEFREGx(_addr, _name, _smin, _smax, 0)
#define	DEFVOIDfmt(_addr, _name, _fmt) \
	_DEFREGfmt(_addr, _name, 0, _fmt)
#define	DEFBASIC(_addr, _name)	_DEFREG(_addr, _name, DUMP_BASIC)
#define	DEFBASICfmt(_addr, _name, _fmt) \
	_DEFREGfmt(_addr, _name, DUMP_BASIC, _fmt)
#define	DEFBASICx(_addr, _name, _smin, _smax) \
	_DEFREGx(_addr, _name, DUMP_BASIC, _smin, _smax)
#define	DEFBB(_addr, _name)	_DEFREG(_addr, _name, DUMP_BASEBAND)
#define	DEFINT(_addr, _name)	_DEFREG(_addr, _name, DUMP_INTERRUPT)
#define	DEFINTfmt(_addr, _name, _fmt) \
	_DEFREGfmt(_addr, _name, DUMP_INTERRUPT, _fmt)
#define	DEFQCU(_addr, _name)	_DEFREG(_addr, _name, DUMP_QCU)
#define	DEFDCU(_addr, _name)	_DEFREG(_addr, _name, DUMP_DCU)

void	register_regs(struct dumpreg *_regs, u_int _nregs,
	    int def_srev_min, int def_srev_max,
	    int def_phy_min, int def_phy_max);
void	register_keycache(u_int nslots,
	    int def_srev_min, int def_srev_max,
	    int def_phy_min, int def_phy_max);
void	register_range(u_int brange, u_int erange, int what,
	    int def_srev_min, int def_srev_max,
	    int def_phy_min, int def_phy_max);
#endif /* _DUMPREGS_ */
