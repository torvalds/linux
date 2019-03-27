/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
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
 * $Begemot: libunimsg/netnatm/api/unisap.h,v 1.6 2005/05/23 11:49:17 brandt_h Exp $
 */
#ifndef _NETNATM_API_UNISAP_H_
#define _NETNATM_API_UNISAP_H_

#include <netnatm/msg/uni_config.h>

enum unisve_tag {
	UNISVE_ABSENT,		/* Element is absent */
	UNISVE_PRESENT,		/* Element is present with specific value */
	UNISVE_ANY		/* Any values is acceptable */
};

struct unisve_addr {
	enum unisve_tag		tag;
	enum uni_addr_type	type;	/* type of address */
	enum uni_addr_plan	plan;	/* addressing plan */
	uint32_t		len;	/* length of address */
	u_char			addr[UNI_ADDR_MAXLEN];
};

struct unisve_selector {
	enum unisve_tag	tag;
	uint8_t		selector;
};

struct unisve_blli_id2 {
	enum unisve_tag	tag;
	u_int		proto:5;	/* the protocol */
	u_int		user:7;		/* user specific protocol */
};

struct unisve_blli_id3 {
	enum unisve_tag	tag;
	u_int		proto:5;	/* L3 protocol */
	u_int		user:7;		/* user specific protocol */
	u_int		ipi:8;		/* ISO/IEC TR 9557 IPI */
	u_int		oui:24;		/* IEEE 802.1 OUI */
	u_int		pid:16;		/* IEEE 802.1 PID */
	uint32_t	noipi;		/* ISO/IEC TR 9557 per frame */
};

struct unisve_bhli {
	enum unisve_tag	tag;
	enum uni_bhli	type;		/* type of info */
	uint32_t	len;		/* length of info */
	uint8_t		info[8];	/* info itself */
};

struct uni_sap {
	struct unisve_addr	addr;
	struct unisve_selector	selector;
	struct unisve_blli_id2	blli_id2;
	struct unisve_blli_id3	blli_id3;
	struct unisve_bhli	bhli;
};

int unisve_check_addr(const struct unisve_addr *);
int unisve_check_selector(const struct unisve_selector *);
int unisve_check_blli_id2(const struct unisve_blli_id2 *);
int unisve_check_blli_id3(const struct unisve_blli_id3 *);
int unisve_check_bhli(const struct unisve_bhli *);

int unisve_check_sap(const struct uni_sap *);

int unisve_overlap_addr(const struct unisve_addr *, const struct unisve_addr *);
int unisve_overlap_selector(const struct unisve_selector *,
    const struct unisve_selector *);
int unisve_overlap_blli_id2(const struct unisve_blli_id2 *,
    const struct unisve_blli_id2 *);
int unisve_overlap_blli_id3(const struct unisve_blli_id3 *,
    const struct unisve_blli_id3 *);
int unisve_overlap_bhli(const struct unisve_bhli *,
    const struct unisve_bhli *);
int unisve_overlap_sap(const struct uni_sap *, const struct uni_sap *);

int unisve_is_catchall(const struct uni_sap *);
int unisve_match(const struct uni_sap *, const struct uni_ie_called *,
	const struct uni_ie_blli *, const struct uni_ie_bhli *);

enum {
	UNISVE_OK = 0,
	UNISVE_ERROR_BAD_TAG,
	UNISVE_ERROR_TYPE_PLAN_CONFLICT,
	UNISVE_ERROR_ADDR_SEL_CONFLICT,
	UNISVE_ERROR_ADDR_LEN,
	UNISVE_ERROR_BAD_ADDR_TYPE,
	UNISVE_ERROR_BAD_BHLI_TYPE,
	UNISVE_ERROR_BAD_BHLI_LEN,
};

#define UNISVE_ERRSTR					\
	"no error",					\
	"bad SVE tag",					\
	"bad address type/plan combination",		\
	"bad address plan/selector tag combination",	\
	"bad address length in SVE",			\
	"unknown address type in SVE",			\
	"bad BHLI type in SVE",				\
	"BHLI info too long in SVE",

#endif
