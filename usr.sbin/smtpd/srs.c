/*	$OpenBSD: srs.c,v 1.5 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2019 Gilles Chehade <gilles@poolp.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/sha.h>
#include <string.h>

#include "smtpd.h"

static uint8_t	base32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int
minrange(uint16_t tref, uint16_t t2, int drift, int mod)
{
	if (tref > drift) {
		/* t2 must fall in between tref and tref - drift */
		if (t2 <= tref && t2>= tref - drift)
			return 1;
	}
	else {
		/* t2 must fall in between 0 and tref, or wrap */
		if (t2 <= tref || t2 >= mod - (drift - tref))
			return 1;
	}
	return 0;
}

static int
maxrange(uint16_t tref, uint16_t t2, int drift, int mod)
{
	if (tref + drift < 1024) {
		/* t2 must fall in between tref and tref + drift */
		if (t2 >= tref && t2 <= tref + drift)
			return 1;
	}
	else {
		/* t2 must fall in between tref + drift, or wrap */
		if (t2 >= tref || t2 <= (tref + drift) % 1024)
			return 1;
	}
	return 0;
}

static int
timestamp_check_range(uint16_t tref, uint16_t t2)
{
	if (! minrange(tref, t2, env->sc_srs_ttl, 1024) &&
	    ! maxrange(tref, t2, 1, 1024))
		return 0;

	return 1;
}

static const unsigned char *
srs_hash(const char *key, const char *value)
{
	SHA_CTX	c;
	static unsigned char md[SHA_DIGEST_LENGTH];

	SHA1_Init(&c);
	SHA1_Update(&c, key, strlen(key));
	SHA1_Update(&c, value, strlen(value));
	SHA1_Final(md, &c);
	return md;
}

static const char *
srs0_encode(const char *sender, const char *rcpt_domain)
{
	static char dest[SMTPD_MAXMAILADDRSIZE];
	char tmp[SMTPD_MAXMAILADDRSIZE];
	char md[SHA_DIGEST_LENGTH*4+1];
	struct mailaddr maddr;
	uint16_t timestamp;
	int ret;

	/* compute 10 bits timestamp according to spec */
	timestamp = (time(NULL) / (60 * 60 * 24)) % 1024;

	/* parse sender into user and domain */
	if (! text_to_mailaddr(&maddr, sender))
		return sender;

	/* TT=<orig_domainpart>=<orig_userpart>@<new_domainpart> */
	ret = snprintf(tmp, sizeof tmp, "%c%c=%s=%s@%s",
	    base32[(timestamp>>5) & 0x1F],
	    base32[timestamp & 0x1F],
	    maddr.domain, maddr.user, rcpt_domain);
	if (ret == -1 || ret >= (int)sizeof tmp)
		return sender;

	/* compute HHHH */
	base64_encode_rfc3548(srs_hash(env->sc_srs_key, tmp), SHA_DIGEST_LENGTH,
	    md, sizeof md);

	/* prepend SRS0=HHHH= prefix */
	ret = snprintf(dest, sizeof dest, "SRS0=%c%c%c%c=%s",
	    md[0], md[1], md[2], md[3], tmp);
	if (ret == -1 || ret >= (int)sizeof dest)
		return sender;

	return dest;
}

static const char *
srs1_encode_srs0(const char *sender, const char *rcpt_domain)
{
	static char dest[SMTPD_MAXMAILADDRSIZE];
	char tmp[SMTPD_MAXMAILADDRSIZE];
	char md[SHA_DIGEST_LENGTH*4+1];
	struct mailaddr maddr;
	int ret;

	/* parse sender into user and domain */
	if (! text_to_mailaddr(&maddr, sender))
		return sender;

	/* <last_domainpart>==<SRS0_userpart>@<new_domainpart> */
	ret = snprintf(tmp, sizeof tmp, "%s==%s@%s",
	    maddr.domain, maddr.user, rcpt_domain);
	if (ret == -1 || ret >= (int)sizeof tmp)
		return sender;

	/* compute HHHH */
	base64_encode_rfc3548(srs_hash(env->sc_srs_key, tmp), SHA_DIGEST_LENGTH,
		md, sizeof md);

	/* prepend SRS1=HHHH= prefix */
	ret = snprintf(dest, sizeof dest, "SRS1=%c%c%c%c=%s",
	    md[0], md[1], md[2], md[3], tmp);
	if (ret == -1 || ret >= (int)sizeof dest)
		return sender;

	return dest;
}

static const char *
srs1_encode_srs1(const char *sender, const char *rcpt_domain)
{
	static char dest[SMTPD_MAXMAILADDRSIZE];
	char tmp[SMTPD_MAXMAILADDRSIZE];
	char md[SHA_DIGEST_LENGTH*4+1];
	struct mailaddr maddr;
	int ret;

	/* parse sender into user and domain */
	if (! text_to_mailaddr(&maddr, sender))
		return sender;

	/* <SRS1_userpart>@<new_domainpart> */
	ret = snprintf(tmp, sizeof tmp, "%s@%s", maddr.user, rcpt_domain);
	if (ret == -1 || ret >= (int)sizeof tmp)
		return sender;

	/* sanity check: there's at least room for a checksum
	 * with allowed delimiter =, + or -
	 */
	if (strlen(tmp) < 5)
		return sender;
	if (tmp[4] != '=' && tmp[4] != '+' && tmp[4] != '-')
		return sender;

	/* compute HHHH */
	base64_encode_rfc3548(srs_hash(env->sc_srs_key, tmp + 5), SHA_DIGEST_LENGTH,
		md, sizeof md);

	/* prepend SRS1=HHHH= prefix skipping previous hops' HHHH */
	ret = snprintf(dest, sizeof dest, "SRS1=%c%c%c%c=%s",
	    md[0], md[1], md[2], md[3], tmp + 5);
	if (ret == -1 || ret >= (int)sizeof dest)
		return sender;

	return dest;
}

const char *
srs_encode(const char *sender, const char *rcpt_domain)
{
	if (strncasecmp(sender, "SRS0=", 5) == 0)
		return srs1_encode_srs0(sender+5, rcpt_domain);
	if (strncasecmp(sender, "SRS1=", 5) == 0)
		return srs1_encode_srs1(sender+5, rcpt_domain);
	return srs0_encode(sender, rcpt_domain);
}

static const char *
srs0_decode(const char *rcpt)
{
	static char dest[SMTPD_MAXMAILADDRSIZE];
	char md[SHA_DIGEST_LENGTH*4+1];
	struct mailaddr maddr;
	char *p;
	uint8_t *idx;
	int ret;
	uint16_t timestamp, srs_timestamp;

	/* sanity check: we have room for a checksum and delimiter */
	if (strlen(rcpt) < 5)
		return NULL;

	/* compute checksum */
	base64_encode_rfc3548(srs_hash(env->sc_srs_key, rcpt+5), SHA_DIGEST_LENGTH,
	    md, sizeof md);

	/* compare prefix checksum with computed checksum */
	if (strncmp(md, rcpt, 4) != 0) {
		if (env->sc_srs_key_backup == NULL)
			return NULL;
		base64_encode_rfc3548(srs_hash(env->sc_srs_key_backup, rcpt+5),
		    SHA_DIGEST_LENGTH, md, sizeof md);
		if (strncmp(md, rcpt, 4) != 0)
			return NULL;
	}
	rcpt += 5;

	/* sanity check: we have room for a timestamp and delimiter */
	if (strlen(rcpt) < 3)
		return NULL;

	/* decode timestamp */
	if ((idx = strchr(base32, rcpt[0])) == NULL)
		return NULL;
	srs_timestamp = ((idx - base32) << 5);

	if ((idx = strchr(base32, rcpt[1])) == NULL)
		return NULL;
	srs_timestamp |= (idx - base32);
	rcpt += 3;

	/* compute current 10 bits timestamp */
	timestamp = (time(NULL) / (60 * 60 * 24)) % 1024;

	/* check that SRS timestamp isn't too far from current */
	if (timestamp != srs_timestamp)
		if (! timestamp_check_range(timestamp, srs_timestamp))
			return NULL;

	if (! text_to_mailaddr(&maddr, rcpt))
		return NULL;

	/* sanity check: we have at least one SRS separator */
	if ((p = strchr(maddr.user, '=')) == NULL)
		return NULL;
	*p++ = '\0';

	/* maddr.user holds "domain\0user", with p pointing at user */
	ret = snprintf(dest, sizeof dest, "%s@%s", p, maddr.user);
	if (ret == -1 || ret >= (int)sizeof dest)
		return NULL;

	return dest;
}

static const char *
srs1_decode(const char *rcpt)
{
	static char dest[SMTPD_MAXMAILADDRSIZE];
	char md[SHA_DIGEST_LENGTH*4+1];
	struct mailaddr maddr;
	char *p;
	uint8_t *idx;
	int ret;
	uint16_t timestamp, srs_timestamp;

	/* sanity check: we have room for a checksum and delimiter */
	if (strlen(rcpt) < 5)
		return NULL;

	/* compute checksum */
	base64_encode_rfc3548(srs_hash(env->sc_srs_key, rcpt+5), SHA_DIGEST_LENGTH,
	    md, sizeof md);

	/* compare prefix checksum with computed checksum */
	if (strncmp(md, rcpt, 4) != 0) {
		if (env->sc_srs_key_backup == NULL)
			return NULL;
		base64_encode_rfc3548(srs_hash(env->sc_srs_key_backup, rcpt+5),
		    SHA_DIGEST_LENGTH, md, sizeof md);
		if (strncmp(md, rcpt, 4) != 0)
			return NULL;
	}
	rcpt += 5;

	if (! text_to_mailaddr(&maddr, rcpt))
		return NULL;

	/* sanity check: we have at least one SRS separator */
	if ((p = strchr(maddr.user, '=')) == NULL)
		return NULL;
	*p++ = '\0';

	/* maddr.user holds "domain\0user", with p pointing at user */
	ret = snprintf(dest, sizeof dest, "SRS0%s@%s", p, maddr.user);
	if (ret == -1 || ret >= (int)sizeof dest)
		return NULL;


	/* we're ready to return decoded address, but let's check if
	 * SRS0 timestamp is valid.
	 */

	/* first, get rid of SRS0 checksum (=HHHH=), we can't check it */
	if (strlen(p) < 6)
		return NULL;
	p += 6;

	/* we should be pointing to a timestamp, check that we're indeed */
	if (strlen(p) < 3)
		return NULL;
	if (p[2] != '=' && p[2] != '+' && p[2] != '-')
		return NULL;
	p[2] = '\0';

	if ((idx = strchr(base32, p[0])) == NULL)
		return NULL;
	srs_timestamp = ((idx - base32) << 5);

	if ((idx = strchr(base32, p[1])) == NULL)
		return NULL;
	srs_timestamp |= (idx - base32);

	/* compute current 10 bits timestamp */
	timestamp = (time(NULL) / (60 * 60 * 24)) % 1024;

	/* check that SRS timestamp isn't too far from current */
	if (timestamp != srs_timestamp)
		if (! timestamp_check_range(timestamp, srs_timestamp))
			return NULL;

	return dest;
}

const char *
srs_decode(const char *rcpt)
{
	if (strncasecmp(rcpt, "SRS0=", 5) == 0)
		return srs0_decode(rcpt + 5);
	if (strncasecmp(rcpt, "SRS1=", 5) == 0)
		return srs1_decode(rcpt + 5);

	return NULL;
}
