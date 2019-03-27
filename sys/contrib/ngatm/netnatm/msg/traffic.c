/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/msg/traffic.c,v 1.4 2004/07/08 08:22:05 brandt Exp $
 *
 * Traffic classification
 */

#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <stdio.h>
#endif

/*
 * Try to set the parameters for the CPCS from the parameters of the
 * connection.
 */
enum {
	T_CBR23 = 100, T_nrtVBR2_6_UBR12, T_rtVBR236, T_rtVBR2_6
};

static const u_int fmask = UNI_TRAFFIC_FPCR0_P | UNI_TRAFFIC_FPCR1_P |
    UNI_TRAFFIC_FSCR0_P | UNI_TRAFFIC_FSCR1_P | UNI_TRAFFIC_FMBS0_P |
    UNI_TRAFFIC_FMBS1_P | UNI_TRAFFIC_FABR1_P;
static const u_int bmask = UNI_TRAFFIC_BPCR0_P | UNI_TRAFFIC_BPCR1_P |
    UNI_TRAFFIC_BSCR0_P | UNI_TRAFFIC_BSCR1_P | UNI_TRAFFIC_BMBS0_P |
    UNI_TRAFFIC_BMBS1_P | UNI_TRAFFIC_BABR1_P;

static const u_int fcbr3 = UNI_TRAFFIC_FPCR0_P | UNI_TRAFFIC_FPCR1_P;
static const u_int bcbr3 = UNI_TRAFFIC_BPCR0_P | UNI_TRAFFIC_BPCR1_P;
static const u_int fvbr16 = UNI_TRAFFIC_FPCR1_P | UNI_TRAFFIC_FSCR1_P |
    UNI_TRAFFIC_FMBS1_P;
static const u_int bvbr16 = UNI_TRAFFIC_BPCR1_P | UNI_TRAFFIC_BSCR1_P |
    UNI_TRAFFIC_BMBS1_P;
static const u_int fvbr23 = UNI_TRAFFIC_FPCR1_P | UNI_TRAFFIC_FSCR0_P |
    UNI_TRAFFIC_FMBS0_P;
static const u_int bvbr23 = UNI_TRAFFIC_BPCR1_P | UNI_TRAFFIC_BSCR0_P |
    UNI_TRAFFIC_BMBS0_P;
static const u_int fvbr4 = UNI_TRAFFIC_FPCR0_P | UNI_TRAFFIC_FPCR1_P;
static const u_int bvbr4 = UNI_TRAFFIC_BPCR0_P | UNI_TRAFFIC_BPCR1_P;

int
uni_classify_traffic(const struct uni_ie_bearer *bearer,
    const struct uni_ie_traffic *traffic,
    enum uni_traffic_class *fclass, enum uni_traffic_class *bclass,
    char *ebuf, size_t ebufsiz)
{
	u_int tclass;
	u_int ft, bt, be, ftag, btag;

	/* classify */
	switch (bearer->bclass) {

	  case UNI_BEARER_A:
		if (!(bearer->h.present & UNI_BEARER_ATC_P)) {
			tclass = T_CBR23;
			break;
		}
		switch (bearer->atc) {

		  case UNI_BEARER_ATC_CBR1:
			tclass = UNI_TRAFFIC_CBR1;
			break;

		  default:
			snprintf(ebuf, ebufsiz, "bad ATC=%#02x for BCOB-A",
			    bearer->atc);
			return (-1);
		}
		break;

	  case UNI_BEARER_C:
		if (!(bearer->h.present & UNI_BEARER_ATC_P)) {
			tclass = T_nrtVBR2_6_UBR12;
			break;
		}
		switch (bearer->atc) {

		  case UNI_BEARER_ATC_VBR1:
			tclass = UNI_TRAFFIC_rtVBR1;
			break;

		  case UNI_BEARER_ATC_VBR:
			tclass = T_rtVBR236;
			break;

		  case UNI_BEARER_ATC_NVBR1:
			tclass = UNI_TRAFFIC_nrtVBR1;
			break;

		  case UNI_BEARER_ATC_ABR:
			tclass = UNI_TRAFFIC_ABR;
			break;

		  default:
			snprintf(ebuf, ebufsiz, "bad ATC=%#02x for BCOB-C",
			    bearer->atc);
			return (-1);
		}
		break;

	  case UNI_BEARER_X:
		if (!(bearer->h.present & UNI_BEARER_ATC_P)) {
			tclass = T_nrtVBR2_6_UBR12;
			break;
		}
		switch (bearer->atc) {

		  case UNI_BEARER_ATC_CBR1:
			tclass = UNI_TRAFFIC_CBR1;
			break;

		  case UNI_BEARER_ATC_CBR:
		  case UNI_BEARER_ATCX_4:
		  case UNI_BEARER_ATCX_6:
			tclass = T_CBR23;
			break;

		  case UNI_BEARER_ATC_VBR1:
			tclass = UNI_TRAFFIC_rtVBR1;
			break;

		  case UNI_BEARER_ATCX_1:
		  case UNI_BEARER_ATC_VBR:
			tclass = T_rtVBR2_6;
			break;

		  case UNI_BEARER_ATC_NVBR1:
			tclass = UNI_TRAFFIC_nrtVBR1;
			break;

		  case UNI_BEARER_ATCX_0:
		  case UNI_BEARER_ATCX_2:
		  case UNI_BEARER_ATCX_8:
		  case UNI_BEARER_ATC_NVBR:
			tclass = T_nrtVBR2_6_UBR12;
			break;

		  case UNI_BEARER_ATC_ABR:
			tclass = UNI_TRAFFIC_ABR;
			break;

		  default:
			snprintf(ebuf, ebufsiz, "bad ATC=%#02x for BCOB-X",
			    bearer->atc);
			return (-1);
		}
		break;

	  case UNI_BEARER_TVP:
		snprintf(ebuf, ebufsiz, "unsupported bearer class tVP");
		return (-1);

	  default:
		snprintf(ebuf, ebufsiz, "bad bearer class %#02x",
		    bearer->bclass);
		return (-1);
	}

	/*
	 * Now traffic IE
	 */
	ft = traffic->h.present & fmask;
	bt = traffic->h.present & bmask;
	be = traffic->h.present & UNI_TRAFFIC_BEST_P;
	ftag = (traffic->h.present & UNI_TRAFFIC_MOPT_P) && traffic->t.ftag;
	btag = (traffic->h.present & UNI_TRAFFIC_MOPT_P) && traffic->t.btag;

#define NOBE(C)								\
	if (be) {							\
		snprintf(ebuf, ebufsiz, "illegal BE for " C);		\
		return (-1);						\
	}

#define NOFT(C)								\
	if (ftag) {							\
		snprintf(ebuf, ebufsiz, "illegal forward tag in " C);	\
		return (-1);						\
	}

#define NOBT(C)								\
	if (btag) {							\
		snprintf(ebuf, ebufsiz, "illegal backward tag in " C);	\
		return (-1);						\
	}

#define FBAD(C) do {							\
	snprintf(ebuf, ebufsiz, "bad forward CRs for " C);		\
	return (-1);							\
    } while (0)

#define BBAD(C) do {							\
	snprintf(ebuf, ebufsiz, "bad backward CRs for " C);		\
	return (-1);							\
    } while (0)

	switch (tclass) {

	  case UNI_TRAFFIC_CBR1:
		NOBE("CBR.1");
		if (ft != UNI_TRAFFIC_FPCR1_P)
			FBAD("CBR.1");
		NOFT("CBR.1");
		if (bt != UNI_TRAFFIC_BPCR1_P)
			BBAD("CBR.1");
		NOBT("CBR.1");
		*fclass = *bclass = UNI_TRAFFIC_CBR1;
		break;

	  case T_CBR23:
		NOBE("CBR.2/3");
		if (ft == UNI_TRAFFIC_FPCR0_P) {
			*fclass = UNI_TRAFFIC_CBR2;
			NOFT("CBR.2");
		} else if (ft == fcbr3) {
			*fclass = UNI_TRAFFIC_CBR3;
			if (!ftag) {
				snprintf(ebuf, ebufsiz, "need forward tagging for CBR.3");
				return (-1);
			}
		} else
			FBAD("CBR.2/3");
		if (bt == UNI_TRAFFIC_BPCR0_P) {
			*bclass = UNI_TRAFFIC_CBR2;
			NOBT("CBR.2");
		} else if (bt == bcbr3) {
			*bclass = UNI_TRAFFIC_CBR3;
			if (!btag) {
				snprintf(ebuf, ebufsiz, "need backward tagging for CBR.3");
				return (-1);
			}
		} else
			BBAD("CBR.2/3");
		break;

	  case UNI_TRAFFIC_rtVBR1:
		NOBE("rtVBR.1");
		if (ft != fvbr16)
			FBAD("rtVBR.1");
		NOFT("rtVBR.1");
		if (bt != bvbr16)
			BBAD("rtVBR.1");
		NOBT("rtVBR.1");
		*fclass = *bclass = UNI_TRAFFIC_rtVBR1;
		break;

	  case T_rtVBR236:
		NOBE("rtVBR.2/3/6");
		if (ft == fvbr23) {
			if (ftag)
				*fclass = UNI_TRAFFIC_rtVBR3;
			else
				*fclass = UNI_TRAFFIC_rtVBR2;
		} else if (ft == fvbr16) {
			*fclass = UNI_TRAFFIC_rtVBR6;
			NOFT("rtVBR.6");
		} else
			FBAD("rtVBR.2/3/6");
		if (bt == bvbr23) {
			if (btag)
				*bclass = UNI_TRAFFIC_rtVBR3;
			else
				*bclass = UNI_TRAFFIC_rtVBR2;
		} else if (bt == bvbr16) {
			*bclass = UNI_TRAFFIC_rtVBR6;
			NOBT("rtVBR.6");
		} else
			BBAD("rtVBR.2/3/6");
		break;

	  case T_rtVBR2_6:
		NOBE("rtVBR.2-6");
		if (ft == fvbr23) {
			if (ftag)
				*fclass = UNI_TRAFFIC_rtVBR3;
			else
				*fclass = UNI_TRAFFIC_rtVBR2;
		} else if (ft == fvbr4) {
			*fclass = UNI_TRAFFIC_rtVBR4;
		} else if (ft == UNI_TRAFFIC_FPCR1_P) {
			*fclass = UNI_TRAFFIC_rtVBR5;
			NOFT("rtVBR.5");
		} else if (ft == fvbr16) {
			*fclass = UNI_TRAFFIC_rtVBR6;
			NOFT("rtVBR.6");
		} else
			FBAD("rtVBR.2-6");
		if (bt == bvbr23) {
			if (btag)
				*bclass = UNI_TRAFFIC_rtVBR3;
			else
				*bclass = UNI_TRAFFIC_rtVBR2;
		} else if (bt == bvbr4) {
			*bclass = UNI_TRAFFIC_rtVBR4;
		} else if (bt == UNI_TRAFFIC_BPCR1_P) {
			*bclass = UNI_TRAFFIC_rtVBR5;
			NOBT("rtVBR.5");
		} else if (bt == bvbr16) {
			*bclass = UNI_TRAFFIC_rtVBR6;
			NOBT("rtVBR.6");
		} else
			BBAD("rtVBR.2-6");
		break;

	  case UNI_TRAFFIC_nrtVBR1:
		NOBE("nrtVBR.1");
		if (ft != fvbr16)
			FBAD("nrtVBR.1");
		NOFT("nrtVBR.1");
		if (bt != bvbr16)
			BBAD("nrtVBR.1");
		NOBT("nrtVBR.1");
		*fclass = *bclass = UNI_TRAFFIC_nrtVBR1;
		break;

	  case T_nrtVBR2_6_UBR12:
		if (be) {
			if (ft != UNI_TRAFFIC_FPCR1_P)
				FBAD("UBR.1/2");
			if (bt != UNI_TRAFFIC_BPCR1_P)
				BBAD("UBR.1/2");
			if (ftag)
				*fclass = UNI_TRAFFIC_UBR2;
			else
				*fclass = UNI_TRAFFIC_UBR1;
			if (btag)
				*bclass = UNI_TRAFFIC_UBR2;
			else
				*bclass = UNI_TRAFFIC_UBR1;
			break;
		}
		if (ft == fvbr23) {
			if (ftag)
				*fclass = UNI_TRAFFIC_nrtVBR3;
			else
				*fclass = UNI_TRAFFIC_nrtVBR2;
		} else if (ft == fvbr4) {
			*fclass = UNI_TRAFFIC_nrtVBR4;
		} else if (ft == UNI_TRAFFIC_FPCR1_P) {
			*fclass = UNI_TRAFFIC_nrtVBR5;
			NOFT("nrtVBR.5");
		} else if (ft == fvbr16) {
			*fclass = UNI_TRAFFIC_nrtVBR6;
			NOFT("nrtVBR.6");
		} else
			FBAD("nrtVBR.2-6");
		if (bt == bvbr23) {
			if (btag)
				*bclass = UNI_TRAFFIC_nrtVBR3;
			else
				*bclass = UNI_TRAFFIC_nrtVBR2;
		} else if (bt == bvbr4) {
			*bclass = UNI_TRAFFIC_nrtVBR4;
		} else if (bt == UNI_TRAFFIC_BPCR1_P) {
			*bclass = UNI_TRAFFIC_nrtVBR5;
			NOBT("nrtVBR.5");
		} else if (bt == bvbr16) {
			*bclass = UNI_TRAFFIC_nrtVBR6;
			NOBT("nrtVBR.6");
		} else
			BBAD("nrtVBR.2-6");
		break;

	  case UNI_TRAFFIC_ABR:
		NOBE("ABR");
		if (ft != UNI_TRAFFIC_FPCR1_P)
			FBAD("ABR");
		if (bt != UNI_TRAFFIC_BPCR1_P)
			BBAD("ABR");
		NOFT("ABR");
		NOBT("ABR");
		*fclass = *bclass = UNI_TRAFFIC_ABR;
		break;
	}

	return (0);
}
