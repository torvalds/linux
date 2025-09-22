/*	$OpenBSD: geofeed.c,v 1.22 2025/08/01 14:57:15 tb Exp $ */
/*
 * Copyright (c) 2022 Job Snijders <job@fastly.com>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/socket.h>

#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>

#include <openssl/bio.h>
#include <openssl/x509.h>

#include "extern.h"

/*
 * Take a CIDR prefix (in presentation format) and add it to parse results.
 * Returns 1 on success, 0 on failure.
 */
static int
geofeed_parse_geoip(struct geofeed *geofeed, char *cidr, char *loc)
{
	struct geoip	*geoip;
	struct ip_addr	*ipaddr;
	enum afi	 afi;
	int		 plen;

	if ((ipaddr = calloc(1, sizeof(struct ip_addr))) == NULL)
		err(1, NULL);

	if ((plen = inet_net_pton(AF_INET, cidr, ipaddr->addr,
	    sizeof(ipaddr->addr))) != -1)
		afi = AFI_IPV4;
	else if ((plen = inet_net_pton(AF_INET6, cidr, ipaddr->addr,
	    sizeof(ipaddr->addr))) != -1)
		afi = AFI_IPV6;
	else {
		static char buf[80];

		if (strnvis(buf, cidr, sizeof(buf), VIS_SAFE)
		    >= (int)sizeof(buf)) {
			memcpy(buf + sizeof(buf) - 4, "...", 4);
		}
		warnx("invalid address: %s", buf);
		free(ipaddr);
		return 0;
	}

	ipaddr->prefixlen = plen;

	geofeed->geoips = recallocarray(geofeed->geoips, geofeed->num_geoips,
	    geofeed->num_geoips + 1, sizeof(struct geoip));
	if (geofeed->geoips == NULL)
		err(1, NULL);
	geoip = &geofeed->geoips[geofeed->num_geoips++];

	if ((geoip->ip = calloc(1, sizeof(struct cert_ip))) == NULL)
		err(1, NULL);

	geoip->ip->type = CERT_IP_ADDR;
	geoip->ip->ip = *ipaddr;
	geoip->ip->afi = afi;

	free(ipaddr);

	if ((geoip->loc = strdup(loc)) == NULL)
		err(1, NULL);

	if (!ip_cert_compose_ranges(geoip->ip))
		return 0;

	return 1;
}

/*
 * Parse a full RFC 9632 file.
 * Returns the Geofeed, or NULL if the object was malformed.
 */
struct geofeed *
geofeed_parse(struct cert **out_cert, const char *fn, int talid, char *buf,
    size_t len)
{
	struct geofeed	*geofeed;
	struct cert	*cert = NULL;
	char		*delim, *line, *loc, *nl;
	ssize_t		 linelen;
	BIO		*bio;
	char		*b64 = NULL;
	size_t		 b64sz = 0;
	unsigned char	*der = NULL;
	size_t		 dersz;
	int		 rpki_signature_seen = 0, end_signature_seen = 0;
	int		 rc = 0;

	assert(*out_cert == NULL);

	bio = BIO_new(BIO_s_mem());
	if (bio == NULL)
		errx(1, "BIO_new");

	if ((geofeed = calloc(1, sizeof(*geofeed))) == NULL)
		err(1, NULL);

	while ((nl = memchr(buf, '\n', len)) != NULL) {
		line = buf;

		/* advance buffer to next line */
		len -= nl + 1 - buf;
		buf = nl + 1;

		/* replace LF and CR with NUL, point nl at first NUL */
		*nl = '\0';
		if (nl > line && nl[-1] == '\r') {
			nl[-1] = '\0';
			nl--;
			linelen = nl - line;
		} else {
			warnx("%s: malformed file, expected CRLF line"
			    " endings", fn);
			goto out;
		}

		if (end_signature_seen) {
			warnx("%s: trailing data after signature section", fn);
			goto out;
		}

		if (rpki_signature_seen) {
			if (strncmp(line, "# End Signature:",
			    strlen("# End Signature:")) == 0) {
				end_signature_seen = 1;
				continue;
			}

			if (linelen > 74) {
				warnx("%s: line in signature section too long",
				    fn);
				goto out;
			}
			if (strncmp(line, "# ", strlen("# ")) != 0) {
				warnx("%s: line in signature section too "
				    "short", fn);
				goto out;
			}

			/* skip over "# " */
			line += 2;
			strlcat(b64, line, b64sz);
			continue;
		}

		if (strncmp(line, "# RPKI Signature:",
		    strlen("# RPKI Signature:")) == 0) {
			rpki_signature_seen = 1;

			if ((b64 = calloc(1, len)) == NULL)
				err(1, NULL);
			b64sz = len;

			continue;
		}

		/*
		 * Read the Geofeed CSV records into a BIO to later on
		 * calculate the message digest and compare with the one
		 * in the detached CMS signature.
		 */
		if (BIO_puts(bio, line) != linelen ||
		    BIO_puts(bio, "\r\n") != 2) {
			warnx("%s: BIO_puts failed", fn);
			goto out;
		}

		/* Zap comments and whitespace before them. */
		delim = memchr(line, '#', linelen);
		if (delim != NULL) {
			while (delim > line &&
			    isspace((unsigned char)delim[-1]))
				delim--;
			*delim = '\0';
			linelen = delim - line;
		}

		/* Skip empty lines. */
		if (linelen == 0)
			continue;

		/* Split prefix and location info */
		delim = memchr(line, ',', linelen);
		if (delim != NULL) {
			*delim = '\0';
			loc = delim + 1;
		} else
			loc = "";

		/* read each prefix  */
		if (!geofeed_parse_geoip(geofeed, line, loc))
			goto out;
	}

	if (!rpki_signature_seen || !end_signature_seen) {
		warnx("%s: absent or invalid signature", fn);
		goto out;
	}

	if ((base64_decode(b64, strlen(b64), &der, &dersz)) == -1) {
		warnx("%s: base64_decode failed", fn);
		goto out;
	}

	if (!cms_parse_validate_detached(&cert, fn, talid, der, dersz,
	    geofeed_oid, bio, &geofeed->signtime))
		goto out;

	if (x509_any_inherits(cert->x509)) {
		warnx("%s: inherit elements not allowed in EE cert", fn);
		goto out;
	}

	if (cert->num_ases > 0) {
		warnx("%s: superfluous AS Resources extension present", fn);
		goto out;
	}

	geofeed->valid = valid_geofeed(fn, cert, geofeed);

	*out_cert = cert;
	cert = NULL;

	rc = 1;
 out:
	if (rc == 0) {
		geofeed_free(geofeed);
		geofeed = NULL;
	}
	cert_free(cert);
	BIO_free(bio);
	free(b64);
	free(der);

	return geofeed;
}

/*
 * Free what follows a pointer to a geofeed structure.
 * Safe to call with NULL.
 */
void
geofeed_free(struct geofeed *p)
{
	size_t i;

	if (p == NULL)
		return;

	for (i = 0; i < p->num_geoips; i++) {
		free(p->geoips[i].ip);
		free(p->geoips[i].loc);
	}

	free(p->geoips);
	free(p);
}
