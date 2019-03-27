/*-
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _LIB80211_REGDOMAIN_H_
#define _LIB80211_REGDOMAIN_H_

#include <sys/cdefs.h>
#include <sys/queue.h>

#include <net80211/ieee80211_regdomain.h>

__BEGIN_DECLS

struct freqband {
	uint16_t	freqStart;	/* starting frequency (MHz) */
	uint16_t	freqEnd;	/* ending frequency (MHz) */
	uint8_t		chanWidth;	/* channel width (MHz) */
	uint8_t		chanSep;	/* channel sepaaration (MHz) */
	uint32_t	flags;		/* common operational constraints */

	const void	*id;
	LIST_ENTRY(freqband) next;
};

/* private flags, don't pass to os */
#define	REQ_ECM		0x1		/* enable if ECM set */
#define	REQ_INDOOR	0x2		/* enable only for indoor operation */
#define	REQ_OUTDOOR	0x4		/* enable only for outdoor operation */

#define	REQ_FLAGS	(REQ_ECM|REQ_INDOOR|REQ_OUTDOOR)

struct netband {
	const struct freqband *band;	/* channel list description */
	uint8_t		maxPower;	/* regulatory cap on tx power (dBm) */
	uint8_t		maxPowerDFS;	/* regulatory cap w/ DFS (dBm) */
	uint8_t		maxAntGain;	/* max allowed antenna gain (.5 dBm) */
	uint32_t	flags;		/* net80211 channel flags */

	LIST_ENTRY(netband) next;
};
typedef LIST_HEAD(, netband) netband_head;

struct country;

struct regdomain {
	enum RegdomainCode	sku;	/* regdomain code/SKU */
	const char		*name;	/* printable name */ 
	const struct country	*cc;	/* country code for 1-1/default map */

	netband_head	 bands_11b;	/* 11b operation */
	netband_head	 bands_11g;	/* 11g operation */
	netband_head	 bands_11a;	/* 11a operation */
	netband_head	 bands_11ng;/* 11ng operation */
	netband_head	 bands_11na;/* 11na operation */
	netband_head	 bands_11ac;/* 11ac 5GHz operation */
	netband_head	 bands_11acg;/* 11ac 2GHz operation */

	LIST_ENTRY(regdomain)	next;
};

struct country {
	enum ISOCountryCode	code;	   
#define	NO_COUNTRY	0xffff
	const struct regdomain	*rd;
	const char*		isoname;
	const char*		name;

	LIST_ENTRY(country)	next;
};

struct ident;

struct regdata {
	LIST_HEAD(, country)	countries;	/* country code table */
	LIST_HEAD(, regdomain)	domains;	/* regulatory domains */
	LIST_HEAD(, freqband)	freqbands;	/* frequency band table */
	struct ident		*ident;		/* identifier table */
};

#define	_PATH_REGDOMAIN	"/etc/regdomain.xml"

struct regdata *lib80211_alloc_regdata(void);
void	lib80211_free_regdata(struct regdata *);

int	lib80211_regdomain_readconfig(struct regdata *, const void *, size_t);
void	lib80211_regdomain_cleanup(struct regdata *);

const struct regdomain *lib80211_regdomain_findbysku(const struct regdata *,
	enum RegdomainCode);
const struct regdomain *lib80211_regdomain_findbyname(const struct regdata *,
	const char *);

const struct country *lib80211_country_findbycc(const struct regdata *,
	enum ISOCountryCode);
const struct country *lib80211_country_findbyname(const struct regdata *,
	const char *);

__END_DECLS

#endif /* _LIB80211_REGDOMAIN_H_ */
