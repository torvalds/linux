/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 * Copyright (c) 2004
 *	Hartmut Brandt
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
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
 * $Begemot: libunimsg/netnatm/api/unisap.c,v 1.4 2004/07/08 08:22:01 brandt Exp $
 */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/ctype.h>
#include <sys/libkern.h>
#else
#include <ctype.h>
#include <string.h>
#endif
#include <netnatm/msg/unistruct.h>
#include <netnatm/api/unisap.h>

int
unisve_check_addr(const struct unisve_addr *sve)
{
	if (sve->tag == UNISVE_ABSENT)
		return (UNISVE_OK);
	if (sve->tag == UNISVE_ANY)
		return (UNISVE_OK);
	if (sve->tag != UNISVE_PRESENT)
		return (UNISVE_ERROR_BAD_TAG);

	if (sve->type == UNI_ADDR_INTERNATIONAL) {
		if (sve->plan != UNI_ADDR_E164)
			return (UNISVE_ERROR_TYPE_PLAN_CONFLICT);
		if (sve->len == 0 || sve->len > 15)
			return (UNISVE_ERROR_ADDR_LEN);

	} else if (sve->type == UNI_ADDR_UNKNOWN) {
		if (sve->plan != UNI_ADDR_ATME)
			return (UNISVE_ERROR_TYPE_PLAN_CONFLICT);
		if (sve->len != 19)
			return (UNISVE_ERROR_ADDR_LEN);
	} else
		return (UNISVE_ERROR_BAD_ADDR_TYPE);

	return (UNISVE_OK);
}

int
unisve_check_selector(const struct unisve_selector *sve)
{
	if (sve->tag != UNISVE_PRESENT &&
	    sve->tag != UNISVE_ABSENT &&
	    sve->tag != UNISVE_ANY)
		return (UNISVE_ERROR_BAD_TAG);
	return (UNISVE_OK);
}

/*
 * We don't want to check the protocol values here.
 */
int
unisve_check_blli_id2(const struct unisve_blli_id2 *sve)
{
	if (sve->tag != UNISVE_PRESENT &&
	    sve->tag != UNISVE_ABSENT &&
	    sve->tag != UNISVE_ANY)
		return (UNISVE_ERROR_BAD_TAG);
	return (UNISVE_OK);
}

/*
 * We don't want to check the protocol values here.
 */
int
unisve_check_blli_id3(const struct unisve_blli_id3 *sve)
{
	if (sve->tag != UNISVE_PRESENT &&
	    sve->tag != UNISVE_ABSENT &&
	    sve->tag != UNISVE_ANY)
		return (UNISVE_ERROR_BAD_TAG);
	return (UNISVE_OK);
}

int
unisve_check_bhli(const struct unisve_bhli *sve)
{
	if (sve->tag == UNISVE_ABSENT)
		return (UNISVE_OK);
	if (sve->tag == UNISVE_ANY)
		return (UNISVE_OK);

	if (sve->tag != UNISVE_PRESENT)
		return (UNISVE_ERROR_BAD_TAG);

	if (sve->type != UNI_BHLI_ISO &&
	    sve->type != UNI_BHLI_USER &&
	    sve->type != UNI_BHLI_VENDOR)
		return (UNISVE_ERROR_BAD_BHLI_TYPE);

	if (sve->len > sizeof(sve->info))
		return (UNISVE_ERROR_BAD_BHLI_LEN);

	return (UNISVE_OK);
}

int
unisve_check_sap(const struct uni_sap *sap)
{
	int err;

	if ((err = unisve_check_addr(&sap->addr)) != 0 ||
	    (err = unisve_check_selector(&sap->selector)) != 0 ||
	    (err = unisve_check_blli_id2(&sap->blli_id2)) != 0 ||
	    (err = unisve_check_blli_id3(&sap->blli_id3)) != 0 ||
	    (err = unisve_check_bhli(&sap->bhli)) != 0)
		return (err);

	if (sap->addr.plan == UNI_ADDR_E164) {
		if (sap->selector.tag == UNISVE_PRESENT)
			return (UNISVE_ERROR_ADDR_SEL_CONFLICT);
	} else if (sap->addr.plan == UNI_ADDR_ATME) {
		if (sap->selector.tag == UNISVE_ABSENT)
			return (UNISVE_ERROR_ADDR_SEL_CONFLICT);
	}
	return (0);
}

#define COMMON_OVERLAP(A1,A2)						\
	if ((A1->tag == UNISVE_ABSENT && A2->tag == UNISVE_ABSENT) ||	\
	    A1->tag == UNISVE_ANY || A2->tag == UNISVE_ANY)		\
		return (1);						\
	if ((A1->tag == UNISVE_ABSENT && A2->tag == UNISVE_PRESENT) ||	\
	    (A2->tag == UNISVE_ABSENT && A1->tag == UNISVE_PRESENT))	\
		return (0);

int
unisve_overlap_addr(const struct unisve_addr *s1, const struct unisve_addr *s2)
{
	COMMON_OVERLAP(s1, s2);

	return (s1->type == s2->type && s1->plan == s2->plan &&
	    s1->len == s2->len && memcmp(s1->addr, s2->addr, s1->len) == 0);
}

int
unisve_overlap_selector(const struct unisve_selector *s1,
    const struct unisve_selector *s2)
{
	COMMON_OVERLAP(s1, s2);

	return (s1->selector == s2->selector);
}

int
unisve_overlap_blli_id2(const struct unisve_blli_id2 *s1,
    const struct unisve_blli_id2 *s2)
{
	COMMON_OVERLAP(s1, s2);

	return (s1->proto == s2->proto &&
	    (s1->proto != UNI_BLLI_L2_USER || s1->user == s2->user));
}

int
unisve_overlap_blli_id3(const struct unisve_blli_id3 *s1,
    const struct unisve_blli_id3 *s2)
{
	COMMON_OVERLAP(s1, s2);

	if (s1->proto != s2->proto)
		return (0);
	if (s1->proto == UNI_BLLI_L3_USER)
		return (s1->user == s2->user);
	if (s1->proto == UNI_BLLI_L3_TR9577) {
		if (s1->noipi && s2->noipi)
			return (1);
		if (!s1->noipi && !s2->noipi) {
			if (s1->ipi == s2->ipi) {
				if (s1->ipi != UNI_BLLI_L3_SNAP)
					return (1);
				if (s1->oui == s2->oui && s1->pid == s2->pid)
					return (1);
			}
		}
		return (0);
	}
	return (1);
}

int
unisve_overlap_bhli(const struct unisve_bhli *s1, const struct unisve_bhli *s2)
{
	COMMON_OVERLAP(s1, s2);

	return (s1->type == s2->type && s1->len == s2->len &&
	    memcmp(s1->info, s2->info, s1->len) == 0);
}

int
unisve_overlap_sap(const struct uni_sap *s1, const struct uni_sap *s2)
{
	int any1, any2;

	/*
	 * Two catch-all's SAP's are not allowed. A catch-all does never
	 * overlap with a non-catch all SAP.
	 */
	any1 = unisve_is_catchall(s1);
	any2 = unisve_is_catchall(s2);

	if (any1 && any2)
		return (1);
	 if(any1 || any2)
		return (0);

	return (unisve_overlap_addr(&s1->addr, &s2->addr) &&
	    unisve_overlap_selector(&s1->selector, &s2->selector) &&
	    unisve_overlap_blli_id2(&s1->blli_id2, &s2->blli_id2) &&
	    unisve_overlap_blli_id3(&s1->blli_id3, &s2->blli_id3) &&
	    unisve_overlap_bhli(&s1->bhli, &s2->bhli));
}

int
unisve_is_catchall(const struct uni_sap *sap)
{
	return (sap->addr.tag == UNISVE_ANY &&
	    sap->selector.tag == UNISVE_ANY &&
	    sap->blli_id2.tag == UNISVE_ANY &&
	    sap->blli_id3.tag == UNISVE_ANY &&
	    sap->bhli.tag == UNISVE_ANY);
}

int
unisve_match(const struct uni_sap *sap, const struct uni_ie_called *called,
	const struct uni_ie_blli *blli, const struct uni_ie_bhli *bhli)
{
	switch (sap->addr.tag) {
	  case UNISVE_ABSENT:
		if (IE_ISGOOD(*called))
			return (0);
		break;

	  case UNISVE_ANY:
		break;

	  case UNISVE_PRESENT:
		if (!IE_ISGOOD(*called))
			return (0);
		if (called->addr.type != sap->addr.type ||
		    called->addr.plan != sap->addr.plan)
			return (0);
		if (called->addr.plan == UNI_ADDR_E164) {
			if (called->addr.len != sap->addr.len ||
			    memcmp(called->addr.addr, sap->addr.addr,
			    called->addr.len) != 0)
				return (0);
		} else if (called->addr.plan == UNI_ADDR_ATME) {
			if (called->addr.len != 20 ||
			    memcmp(called->addr.addr, sap->addr.addr, 19) != 0)
				return (0);
		}
		break;

	  default:
		return (0);
	}

	switch (sap->selector.tag) {

	  case UNISVE_ABSENT:
		if (IE_ISGOOD(*called) && called->addr.plan == UNI_ADDR_ATME)
			return (0);
		break;

	  case UNISVE_ANY:
		break;

	  case UNISVE_PRESENT:
		if (!IE_ISGOOD(*called))
			return (0);
		if (called->addr.plan != UNI_ADDR_ATME)
			return (0);
		if (called->addr.addr[19] != sap->selector.selector)
			return (0);
		break;

	  default:
		return (0);
	}

	switch (sap->blli_id2.tag) {

	  case UNISVE_ABSENT:
		if (IE_ISGOOD(*blli) && (blli->h.present & UNI_BLLI_L2_P))
			return (0);
		break;

	  case UNISVE_ANY:
		break;

	  case UNISVE_PRESENT:
		if (!IE_ISGOOD(*blli) || (blli->h.present & UNI_BLLI_L2_P) == 0)
			return (0);
		if (blli->l2 != sap->blli_id2.proto)
			return (0);
		if (blli->l2 == UNI_BLLI_L2_USER) {
			if ((blli->h.present & UNI_BLLI_L2_USER_P) == 0)
				return (0);
			if (blli->l2_user != sap->blli_id2.user)
				return (0);
		}
		break;

	  default:
		return (0);
	}

	switch (sap->blli_id3.tag) {

	  case UNISVE_ABSENT:
		if (IE_ISGOOD(*blli) && (blli->h.present & UNI_BLLI_L3_P))
			return (0);
		break;

	  case UNISVE_ANY:
		break;

	  case UNISVE_PRESENT:
		if (!IE_ISGOOD(*blli) || (blli->h.present & UNI_BLLI_L3_P) == 0)
			return (0);
		if (blli->l3 != sap->blli_id3.proto)
			return (0);
		if (blli->l3 == UNI_BLLI_L3_USER) {
			if ((blli->h.present & UNI_BLLI_L3_USER_P) == 0)
				return (0);
			if (blli->l3_user != sap->blli_id3.user)
				return (0);
			break;
		}
		if (blli->l3 == UNI_BLLI_L3_TR9577) {
			if (sap->blli_id3.noipi) {
				if (blli->h.present & UNI_BLLI_L3_IPI_P)
					return (0);
			} else {
				if (!(blli->h.present & UNI_BLLI_L3_IPI_P))
					return (0);
				if (blli->l3_ipi != sap->blli_id3.ipi)
					return (0);
				if (blli->l3_ipi == UNI_BLLI_L3_SNAP) {
					if (!(blli->h.present &
					    UNI_BLLI_L3_SNAP_P))
						return (0);
					if (blli->oui != sap->blli_id3.oui ||
					    blli->pid != sap->blli_id3.pid)
						return (0);
				}
			}
		}
		break;

	  default:
		return (0);
	}

	switch (sap->bhli.tag) {

	  case UNISVE_ABSENT:
		if (IE_ISGOOD(*bhli))
			return (0);
		break;

	  case UNISVE_ANY:
		break;

	  case UNISVE_PRESENT:
		if (!IE_ISGOOD(*bhli))
			return (0);
		if (sap->bhli.type != bhli->type)
			return (0);
		if (sap->bhli.len != bhli->len)
			return (0);
		if (memcmp(sap->bhli.info, bhli->info, bhli->len) != 0)
			return (0);
		break;

	  default:
		return (0);
	}
	/* Uff */
	return (1);
}
