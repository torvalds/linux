/*-
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
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
 */
#ifndef lint
static const char rcsid[] = "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/sbuf.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <err.h>
#include <unistd.h>

#include <bsdxml.h>

#include "lib80211_regdomain.h"

#include <net80211/_ieee80211.h>

#define	MAXLEVEL	20

struct mystate {
	XML_Parser		parser;
	struct regdata		*rdp;
	struct regdomain	*rd;		/* current domain */
	struct netband		*netband;	/* current netband */
	struct freqband		*freqband;	/* current freqband */
	struct country		*country;	/* current country */
	netband_head		*curband;	/* current netband list */
	int			level;
	struct sbuf		*sbuf[MAXLEVEL];
	int			nident;
};

struct ident {
	const void *id;
	void *p;
	enum { DOMAIN, COUNTRY, FREQBAND } type;
};

static void
start_element(void *data, const char *name, const char **attr)
{
#define	iseq(a,b)	(strcasecmp(a,b) == 0)
	struct mystate *mt;
	const void *id, *ref, *mode;
	int i;

	mt = data;
	if (++mt->level == MAXLEVEL) {
		/* XXX force parser to abort */
		return;
	}
	mt->sbuf[mt->level] = sbuf_new_auto();
	id = ref = mode = NULL;
	for (i = 0; attr[i] != NULL; i += 2) {
		if (iseq(attr[i], "id")) {
			id = attr[i+1];
		} else if (iseq(attr[i], "ref")) {
			ref = attr[i+1];
		} else if (iseq(attr[i], "mode")) {
			mode = attr[i+1];
		} else
			printf("%*.*s[%s = %s]\n", mt->level + 1,
			    mt->level + 1, "", attr[i], attr[i+1]);
	}
	if (iseq(name, "rd") && mt->rd == NULL) {
		if (mt->country == NULL) {
			mt->rd = calloc(1, sizeof(struct regdomain));
			mt->rd->name = strdup(id);
			mt->nident++;
			LIST_INSERT_HEAD(&mt->rdp->domains, mt->rd, next);
		} else
			mt->country->rd = (void *)strdup(ref);
		return;
	}
	if (iseq(name, "defcc") && mt->rd != NULL) {
		mt->rd->cc = (void *)strdup(ref);
		return;
	}
	if (iseq(name, "netband") && mt->curband == NULL && mt->rd != NULL) {
		if (mode == NULL) {
			warnx("no mode for netband at line %ld",
			    XML_GetCurrentLineNumber(mt->parser));
			return;
		}
		if (iseq(mode, "11b"))
			mt->curband = &mt->rd->bands_11b;
		else if (iseq(mode, "11g"))
			mt->curband = &mt->rd->bands_11g;
		else if (iseq(mode, "11a"))
			mt->curband = &mt->rd->bands_11a;
		else if (iseq(mode, "11ng"))
			mt->curband = &mt->rd->bands_11ng;
		else if (iseq(mode, "11na"))
			mt->curband = &mt->rd->bands_11na;
		else if (iseq(mode, "11ac"))
			mt->curband = &mt->rd->bands_11ac;
		else if (iseq(mode, "11acg"))
			mt->curband = &mt->rd->bands_11acg;
		else
			warnx("unknown mode \"%s\" at line %ld",
			    __DECONST(char *, mode),
			    XML_GetCurrentLineNumber(mt->parser));
		return;
	}
	if (iseq(name, "band") && mt->netband == NULL) {
		if (mt->curband == NULL) {
			warnx("band without enclosing netband at line %ld",
			    XML_GetCurrentLineNumber(mt->parser));
			return;
		}
		mt->netband = calloc(1, sizeof(struct netband));
		LIST_INSERT_HEAD(mt->curband, mt->netband, next);
		return;
	}
	if (iseq(name, "freqband") && mt->freqband == NULL && mt->netband != NULL) {
		/* XXX handle inlines and merge into table? */
		if (mt->netband->band != NULL) {
			warnx("duplicate freqband at line %ld ignored",
			    XML_GetCurrentLineNumber(mt->parser));
			/* XXX complain */
		} else
			mt->netband->band = (void *)strdup(ref);
		return;
	}

	if (iseq(name, "country") && mt->country == NULL) {
		mt->country = calloc(1, sizeof(struct country));
		mt->country->isoname = strdup(id);
		mt->country->code = NO_COUNTRY;
		mt->nident++;
		LIST_INSERT_HEAD(&mt->rdp->countries, mt->country, next);
		return;
	}

	if (iseq(name, "freqband") && mt->freqband == NULL) {
		mt->freqband = calloc(1, sizeof(struct freqband));
		mt->freqband->id = strdup(id);
		mt->nident++;
		LIST_INSERT_HEAD(&mt->rdp->freqbands, mt->freqband, next);
		return;
	}
#undef iseq
}

static int
decode_flag(struct mystate *mt, const char *p, int len)
{
#define	iseq(a,b)	(strcasecmp(a,b) == 0)
	static const struct {
		const char *name;
		int len;
		uint32_t value;
	} flags[] = {
#define	FLAG(x)	{ #x, sizeof(#x)-1, x }
		FLAG(IEEE80211_CHAN_A),
		FLAG(IEEE80211_CHAN_B),
		FLAG(IEEE80211_CHAN_G),
		FLAG(IEEE80211_CHAN_HT20),
		FLAG(IEEE80211_CHAN_HT40),
		FLAG(IEEE80211_CHAN_VHT20),
		FLAG(IEEE80211_CHAN_VHT40),
		FLAG(IEEE80211_CHAN_VHT80),
		/*
		 * XXX VHT80_80? This likely should be done by
		 * 80MHz chan logic in net80211 / ifconfig.
		 */
		FLAG(IEEE80211_CHAN_VHT160),
		FLAG(IEEE80211_CHAN_ST),
		FLAG(IEEE80211_CHAN_TURBO),
		FLAG(IEEE80211_CHAN_PASSIVE),
		FLAG(IEEE80211_CHAN_DFS),
		FLAG(IEEE80211_CHAN_CCK),
		FLAG(IEEE80211_CHAN_OFDM),
		FLAG(IEEE80211_CHAN_2GHZ),
		FLAG(IEEE80211_CHAN_5GHZ),
		FLAG(IEEE80211_CHAN_DYN),
		FLAG(IEEE80211_CHAN_GFSK),
		FLAG(IEEE80211_CHAN_GSM),
		FLAG(IEEE80211_CHAN_STURBO),
		FLAG(IEEE80211_CHAN_HALF),
		FLAG(IEEE80211_CHAN_QUARTER),
		FLAG(IEEE80211_CHAN_HT40U),
		FLAG(IEEE80211_CHAN_HT40D),
		FLAG(IEEE80211_CHAN_4MSXMIT),
		FLAG(IEEE80211_CHAN_NOADHOC),
		FLAG(IEEE80211_CHAN_NOHOSTAP),
		FLAG(IEEE80211_CHAN_11D),
		FLAG(IEEE80211_CHAN_FHSS),
		FLAG(IEEE80211_CHAN_PUREG),
		FLAG(IEEE80211_CHAN_108A),
		FLAG(IEEE80211_CHAN_108G),
#undef FLAG
		{ "ECM",	3,	REQ_ECM },
		{ "INDOOR",	6,	REQ_INDOOR },
		{ "OUTDOOR",	7,	REQ_OUTDOOR },
	};
	unsigned int i;

	for (i = 0; i < nitems(flags); i++)
		if (len == flags[i].len && iseq(p, flags[i].name))
			return flags[i].value;
	warnx("unknown flag \"%.*s\" at line %ld ignored",
	    len, p, XML_GetCurrentLineNumber(mt->parser));
	return 0;
#undef iseq
}

static void
end_element(void *data, const char *name)
{
#define	iseq(a,b)	(strcasecmp(a,b) == 0)
	struct mystate *mt;
	int len;
	char *p;

	mt = data;
	sbuf_finish(mt->sbuf[mt->level]);
	p = sbuf_data(mt->sbuf[mt->level]);
	len = sbuf_len(mt->sbuf[mt->level]);

	/* <freqband>...</freqband> */
	if (iseq(name, "freqstart") && mt->freqband != NULL) {
		mt->freqband->freqStart = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "freqend") && mt->freqband != NULL) {
		mt->freqband->freqEnd = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "chanwidth") && mt->freqband != NULL) {
		mt->freqband->chanWidth = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "chansep") && mt->freqband != NULL) {
		mt->freqband->chanSep = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "flags")) {
		if (mt->freqband != NULL)
			mt->freqband->flags |= decode_flag(mt, p, len);
		else if (mt->netband != NULL)
			mt->netband->flags |= decode_flag(mt, p, len);
		else {
			warnx("flags without freqband or netband at line %ld ignored",
			    XML_GetCurrentLineNumber(mt->parser));
		}
		goto done;
	}

	/* <rd> ... </rd> */
	if (iseq(name, "name") && mt->rd != NULL) {
		mt->rd->name = strdup(p);
		goto done;
	}
	if (iseq(name, "sku") && mt->rd != NULL) {
		mt->rd->sku = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "netband") && mt->rd != NULL) {
		mt->curband = NULL;
		goto done;
	}

	/* <band> ... </band> */
	if (iseq(name, "freqband") && mt->netband != NULL) {
		/* XXX handle inline freqbands */
		goto done;
	}
	if (iseq(name, "maxpower") && mt->netband != NULL) {
		mt->netband->maxPower = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "maxpowerdfs") && mt->netband != NULL) {
		mt->netband->maxPowerDFS = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "maxantgain") && mt->netband != NULL) {
		mt->netband->maxAntGain = strtoul(p, NULL, 0);
		goto done;
	}

	/* <country>...</country> */
	if (iseq(name, "isocc") && mt->country != NULL) {
		mt->country->code = strtoul(p, NULL, 0);
		goto done;
	}
	if (iseq(name, "name") && mt->country != NULL) {
		mt->country->name = strdup(p);
		goto done;
	}

	if (len != 0) {
		warnx("unexpected XML token \"%s\" data \"%s\" at line %ld",
		    name, p, XML_GetCurrentLineNumber(mt->parser));
		/* XXX goto done? */
	}
	/* </freqband> */
	if (iseq(name, "freqband") && mt->freqband != NULL) {
		/* XXX must have start/end frequencies */
		/* XXX must have channel width/sep */
		mt->freqband = NULL;
		goto done;
	}
	/* </rd> */
	if (iseq(name, "rd") && mt->rd != NULL) {
		mt->rd = NULL;
		goto done;
	}
	/* </band> */
	if (iseq(name, "band") && mt->netband != NULL) {
		if (mt->netband->band == NULL) {
			warnx("no freqbands for band at line %ld",
			   XML_GetCurrentLineNumber(mt->parser));
		}
		if (mt->netband->maxPower == 0) {
			warnx("no maxpower for band at line %ld",
			   XML_GetCurrentLineNumber(mt->parser));
		}
		/* default max power w/ DFS to max power */
		if (mt->netband->maxPowerDFS == 0)
			mt->netband->maxPowerDFS = mt->netband->maxPower;
		mt->netband = NULL;
		goto done;
	}
	/* </netband> */
	if (iseq(name, "netband") && mt->netband != NULL) {
		mt->curband = NULL;
		goto done;
	}
	/* </country> */
	if (iseq(name, "country") && mt->country != NULL) {
		/* XXX NO_COUNTRY should be in the net80211 country enum */
		if ((int) mt->country->code == NO_COUNTRY) {
			warnx("no ISO cc for country at line %ld",
			   XML_GetCurrentLineNumber(mt->parser));
		}
		if (mt->country->name == NULL) {
			warnx("no name for country at line %ld",
			   XML_GetCurrentLineNumber(mt->parser));
		}
		if (mt->country->rd == NULL) {
			warnx("no regdomain reference for country at line %ld",
			   XML_GetCurrentLineNumber(mt->parser));
		}
		mt->country = NULL;
		goto done;
	}
done:
	sbuf_delete(mt->sbuf[mt->level]);
	mt->sbuf[mt->level--] = NULL;
#undef iseq
}

static void
char_data(void *data, const XML_Char *s, int len)
{
	struct mystate *mt;
	const char *b, *e;

	mt = data;

	b = s;
	e = s + len-1;
	for (; isspace(*b) && b < e; b++)
		;
	for (; isspace(*e) && e > b; e++)
		;
	if (e != b || (*b != '\0' && !isspace(*b)))
		sbuf_bcat(mt->sbuf[mt->level], b, e-b+1);
}

static void *
findid(struct regdata *rdp, const void *id, int type)
{
	struct ident *ip;

	for (ip = rdp->ident; ip->id != NULL; ip++)
		if ((int) ip->type == type && strcasecmp(ip->id, id) == 0)
			return ip->p;
	return NULL;
}

/*
 * Parse an regdomain XML configuration and build the internal representation.
 */
int
lib80211_regdomain_readconfig(struct regdata *rdp, const void *p, size_t len)
{
	struct mystate *mt;
	struct regdomain *dp;
	struct country *cp;
	struct freqband *fp;
	struct netband *nb;
	const void *id;
	int i, errors;

	memset(rdp, 0, sizeof(struct regdata));
	mt = calloc(1, sizeof(struct mystate));
	if (mt == NULL)
		return ENOMEM;
	/* parse the XML input */
	mt->rdp = rdp;
	mt->parser = XML_ParserCreate(NULL);
	XML_SetUserData(mt->parser, mt);
	XML_SetElementHandler(mt->parser, start_element, end_element);
	XML_SetCharacterDataHandler(mt->parser, char_data);
	if (XML_Parse(mt->parser, p, len, 1) != XML_STATUS_OK) {
		warnx("%s: %s at line %ld", __func__,
		   XML_ErrorString(XML_GetErrorCode(mt->parser)),
		   XML_GetCurrentLineNumber(mt->parser));
		return -1;
	}
	XML_ParserFree(mt->parser);

	/* setup the identifer table */
	rdp->ident = calloc(sizeof(struct ident), mt->nident + 1);
	if (rdp->ident == NULL)
		return ENOMEM;
	free(mt);

	errors = 0;
	i = 0;
	LIST_FOREACH(dp, &rdp->domains, next) {
		rdp->ident[i].id = dp->name;
		rdp->ident[i].p = dp;
		rdp->ident[i].type = DOMAIN;
		i++;
	}
	LIST_FOREACH(fp, &rdp->freqbands, next) {
		rdp->ident[i].id = fp->id;
		rdp->ident[i].p = fp;
		rdp->ident[i].type = FREQBAND;
		i++;
	}
	LIST_FOREACH(cp, &rdp->countries, next) {
		rdp->ident[i].id = cp->isoname;
		rdp->ident[i].p = cp;
		rdp->ident[i].type = COUNTRY;
		i++;
	}

	/* patch references */
	LIST_FOREACH(dp, &rdp->domains, next) {
		if (dp->cc != NULL) {
			id = dp->cc;
			dp->cc = findid(rdp, id, COUNTRY);
			if (dp->cc == NULL) {
				warnx("undefined country \"%s\"",
				    __DECONST(char *, id));
				errors++;
			}
			free(__DECONST(char *, id));
		}
		LIST_FOREACH(nb, &dp->bands_11b, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11b band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11g, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11g band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11a, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11a band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11ng, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11ng band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11na, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11na band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11ac, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11ac band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
		LIST_FOREACH(nb, &dp->bands_11acg, next) {
			id = findid(rdp, nb->band, FREQBAND);
			if (id == NULL) {
				warnx("undefined 11acg band \"%s\"",
				    __DECONST(char *, nb->band));
				errors++;
			}
			nb->band = id;
		}
	}
	LIST_FOREACH(cp, &rdp->countries, next) {
		id = cp->rd;
		cp->rd = findid(rdp, id, DOMAIN);
		if (cp->rd == NULL) {
			warnx("undefined country \"%s\"",
			    __DECONST(char *, id));
			errors++;
		}
		free(__DECONST(char *, id));
	}

	return errors ? EINVAL : 0;
}

static void
cleanup_bands(netband_head *head)
{
	struct netband *nb;

	for (;;) {
		nb = LIST_FIRST(head);
		if (nb == NULL)
			break;
		LIST_REMOVE(nb, next);
		free(nb);
	}
}

/*
 * Cleanup state/resources for a previously parsed regdomain database.
 */
void
lib80211_regdomain_cleanup(struct regdata *rdp)
{

	free(rdp->ident);
	rdp->ident = NULL;
	for (;;) {
		struct regdomain *dp = LIST_FIRST(&rdp->domains);
		if (dp == NULL)
			break;
		LIST_REMOVE(dp, next);
		cleanup_bands(&dp->bands_11b);
		cleanup_bands(&dp->bands_11g);
		cleanup_bands(&dp->bands_11a);
		cleanup_bands(&dp->bands_11ng);
		cleanup_bands(&dp->bands_11na);
		cleanup_bands(&dp->bands_11ac);
		cleanup_bands(&dp->bands_11acg);
		if (dp->name != NULL)
			free(__DECONST(char *, dp->name));
	}
	for (;;) {
		struct country *cp = LIST_FIRST(&rdp->countries);
		if (cp == NULL)
			break;
		LIST_REMOVE(cp, next);
		if (cp->name != NULL)
			free(__DECONST(char *, cp->name));
		free(cp);
	}
	for (;;) {
		struct freqband *fp = LIST_FIRST(&rdp->freqbands);
		if (fp == NULL)
			break;
		LIST_REMOVE(fp, next);
		free(fp);
	}
}

struct regdata *
lib80211_alloc_regdata(void)
{
	struct regdata *rdp;
	struct stat sb;
	void *xml;
	int fd;

	rdp = calloc(1, sizeof(struct regdata));

	fd = open(_PATH_REGDOMAIN, O_RDONLY);
	if (fd < 0) {
#ifdef DEBUG
		warn("%s: open(%s)", __func__, _PATH_REGDOMAIN);
#endif
		free(rdp);
		return NULL;
	}
	if (fstat(fd, &sb) < 0) {
#ifdef DEBUG
		warn("%s: fstat(%s)", __func__, _PATH_REGDOMAIN);
#endif
		close(fd);
		free(rdp);
		return NULL;
	}
	xml = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (xml == MAP_FAILED) {
#ifdef DEBUG
		warn("%s: mmap", __func__);
#endif
		close(fd);
		free(rdp);
		return NULL;
	}
	if (lib80211_regdomain_readconfig(rdp, xml, sb.st_size) != 0) {
#ifdef DEBUG
		warn("%s: error reading regulatory database", __func__);
#endif
		munmap(xml, sb.st_size);
		close(fd);
		free(rdp);
		return NULL;
	}
	munmap(xml, sb.st_size);
	close(fd);

	return rdp;
}

void
lib80211_free_regdata(struct regdata *rdp)
{
	lib80211_regdomain_cleanup(rdp);
	free(rdp);
}

/*
 * Lookup a regdomain by SKU.
 */
const struct regdomain *
lib80211_regdomain_findbysku(const struct regdata *rdp, enum RegdomainCode sku)
{
	const struct regdomain *dp;

	LIST_FOREACH(dp, &rdp->domains, next) {
		if (dp->sku == sku)
			return dp;
	}
	return NULL;
}

/*
 * Lookup a regdomain by name.
 */
const struct regdomain *
lib80211_regdomain_findbyname(const struct regdata *rdp, const char *name)
{
	const struct regdomain *dp;

	LIST_FOREACH(dp, &rdp->domains, next) {
		if (strcasecmp(dp->name, name) == 0)
			return dp;
	}
	return NULL;
}

/*
 * Lookup a country by ISO country code.
 */
const struct country *
lib80211_country_findbycc(const struct regdata *rdp, enum ISOCountryCode cc)
{
	const struct country *cp;

	LIST_FOREACH(cp, &rdp->countries, next) {
		if (cp->code == cc)
			return cp;
	}
	return NULL;
}

/*
 * Lookup a country by ISO/long name.
 */
const struct country *
lib80211_country_findbyname(const struct regdata *rdp, const char *name)
{
	const struct country *cp;
	int len;

	len = strlen(name);
	LIST_FOREACH(cp, &rdp->countries, next) {
		if (strcasecmp(cp->isoname, name) == 0)
			return cp;
	}
	LIST_FOREACH(cp, &rdp->countries, next) {
		if (strncasecmp(cp->name, name, len) == 0)
			return cp;
	}
	return NULL;
}
