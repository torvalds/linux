/*	$OpenBSD: constraints.c,v 1.5 2024/11/12 09:23:07 tb Exp $ */
/*
 * Copyright (c) 2023 Job Snijders <job@openbsd.org>
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/asn1.h>
#include <openssl/x509v3.h>

#include "extern.h"

struct tal_constraints {
	int		 fd;		/* constraints file descriptor or -1. */
	char		*fn;		/* constraints filename */
	char		*warn;		/* warning msg used for violations */
	struct cert_ip	*allow_ips;	/* list of allowed IP address ranges */
	size_t		 num_allow_ips;
	struct cert_as	*allow_ases;	/* allowed AS numbers and ranges */
	size_t		 num_allow_ases;
	struct cert_ip	*deny_ips;	/* forbidden IP address ranges */
	size_t		 num_deny_ips;
	struct cert_as	*deny_ases;	/* forbidden AS numbers and ranges */
	size_t		 num_deny_ases;
} tal_constraints[TALSZ_MAX];

/*
 * If there is a .constraints file next to a .tal file, load its contents
 * into into tal_constraints[talid]. The load function only opens the fd
 * and stores the filename. The actual parsing happens in constraints_parse().
 * Resources of EE certs can then be constrained using constraints_validate().
 */

static void
constraints_load_talid(int talid)
{
	const char	*tal = tals[talid];
	char		*constraints = NULL, *warning = NULL, *cbn;
	int		 fd;
	size_t		 len;
	int		 saved_errno;

	tal_constraints[talid].fd = -1;

	if (rtype_from_file_extension(tal) != RTYPE_TAL)
		return;

	/* Replace .tal suffix with .constraints. */
	len = strlen(tal) - 4;
	if (asprintf(&constraints, "%.*s.constraints", (int)len, tal) == -1)
		err(1, NULL);

	/* prepare warning message for when violations are detected */
	if ((cbn = basename(constraints)) == NULL)
		err(1, "basename");
	if (asprintf(&warning, "resource violates %s", cbn) == -1)
		err(1, NULL);

	saved_errno = errno;

	fd = open(constraints, O_RDONLY);
	if (fd == -1 && errno != ENOENT)
		err(1, "failed to load constraints for %s", tal);

	tal_constraints[talid].fn = constraints;
	tal_constraints[talid].fd = fd;
	tal_constraints[talid].warn = warning;

	errno = saved_errno;
}

/*
 * Iterate over all TALs and load the corresponding constraints files.
 */
void
constraints_load(void)
{
	int	 talid;

	for (talid = 0; talid < talsz; talid++)
		constraints_load_talid(talid);
}

void
constraints_unload(void)
{
	int	 saved_errno, talid;

	saved_errno = errno;
	for (talid = 0; talid < talsz; talid++) {
		if (tal_constraints[talid].fd != -1)
			close(tal_constraints[talid].fd);
		free(tal_constraints[talid].fn);
		free(tal_constraints[talid].warn);
		tal_constraints[talid].fd = -1;
		tal_constraints[talid].fn = NULL;
		tal_constraints[talid].warn = NULL;
	}
	errno = saved_errno;
}

/*
 * Split a string at '-' and trim whitespace around the '-'.
 * Assumes leading and trailing whitespace in p has already been trimmed.
 */
static int
constraints_split_range(char *p, const char **min, const char **max)
{
	char	*pp;

	*min = p;
	if ((*max = pp = strchr(p, '-')) == NULL)
		return 0;

	/* Trim whitespace before '-'. */
	while (pp > *min && isspace((unsigned char)pp[-1]))
		pp--;
	*pp = '\0';

	/* Skip past '-' and whitespace following it. */
	(*max)++;
	while (isspace((unsigned char)**max))
		(*max)++;

	return 1;
}

/*
 * Helper functions to parse textual representations of IP prefixes or ranges.
 * The RFC 3779 API has poor error reporting, so as a debugging aid, we call
 * the prohibitively expensive X509v3_addr_canonize() in high verbosity mode.
 */

static void
constraints_parse_ip_prefix(const char *fn, const char *prefix, enum afi afi,
    IPAddrBlocks *addrs)
{
	unsigned char	 addr[16] = { 0 };
	int		 af = afi == AFI_IPV4 ? AF_INET : AF_INET6;
	int		 plen;

	if ((plen = inet_net_pton(af, prefix, addr, sizeof(addr))) == -1)
		errx(1, "%s: failed to parse %s", fn, prefix);

	if (!X509v3_addr_add_prefix(addrs, afi, NULL, addr, plen))
		errx(1, "%s: failed to add prefix %s", fn, prefix);

	if (verbose < 3)
		return;

	if (!X509v3_addr_canonize(addrs))
		errx(1, "%s: failed to canonize with prefix %s", fn, prefix);
}

static void
constraints_parse_ip_range(const char *fn, const char *min, const char *max,
    enum afi afi, IPAddrBlocks *addrs)
{
	unsigned char	 min_addr[16] = {0}, max_addr[16] = {0};
	int		 af = afi == AFI_IPV4 ? AF_INET : AF_INET6;

	if (inet_pton(af, min, min_addr) != 1)
		errx(1, "%s: failed to parse %s", fn, min);
	if (inet_pton(af, max, max_addr) != 1)
		errx(1, "%s: failed to parse %s", fn, max);

	if (!X509v3_addr_add_range(addrs, afi, NULL, min_addr, max_addr))
		errx(1, "%s: failed to add range %s--%s", fn, min, max);

	if (verbose < 3)
		return;

	if (!X509v3_addr_canonize(addrs))
		errx(1, "%s: failed to canonize with range %s--%s", fn,
		    min, max);
}

static void
constraints_parse_ip(const char *fn, char *p, enum afi afi, IPAddrBlocks *addrs)
{
	const char	*min, *max;

	if (strchr(p, '-') == NULL) {
		constraints_parse_ip_prefix(fn, p, afi, addrs);
		return;
	}

	if (!constraints_split_range(p, &min, &max))
		errx(1, "%s: failed to split range: %s", fn, p);

	constraints_parse_ip_range(fn, min, max, afi, addrs);
}

/*
 * Helper functions to parse textual representations of AS numbers or ranges.
 * The RFC 3779 API has poor error reporting, so as a debugging aid, we call
 * the prohibitively expensive X509v3_asid_canonize() in high verbosity mode.
 */

static void
constraints_parse_asn(const char *fn, const char *asn, ASIdentifiers *asids)
{
	ASN1_INTEGER	*id;

	if ((id = s2i_ASN1_INTEGER(NULL, asn)) == NULL)
		errx(1, "%s: failed to parse AS %s", fn, asn);

	if (!X509v3_asid_add_id_or_range(asids, V3_ASID_ASNUM, id, NULL))
		errx(1, "%s: failed to add AS %s", fn, asn);

	if (verbose < 3)
		return;

	if (!X509v3_asid_canonize(asids))
		errx(1, "%s: failed to canonize with AS %s", fn, asn);
}

static void
constraints_parse_asn_range(const char *fn, const char *min, const char *max,
    ASIdentifiers *asids)
{
	ASN1_INTEGER	*min_as, *max_as;

	if ((min_as = s2i_ASN1_INTEGER(NULL, min)) == NULL)
		errx(1, "%s: failed to parse AS %s", fn, min);
	if ((max_as = s2i_ASN1_INTEGER(NULL, max)) == NULL)
		errx(1, "%s: failed to parse AS %s", fn, max);

	if (!X509v3_asid_add_id_or_range(asids, V3_ASID_ASNUM, min_as, max_as))
		errx(1, "%s: failed to add AS range %s--%s", fn, min, max);

	if (verbose < 3)
		return;

	if (!X509v3_asid_canonize(asids))
		errx(1, "%s: failed to canonize with AS range %s--%s", fn,
		    min, max);
}

static void
constraints_parse_as(const char *fn, char *p, ASIdentifiers *asids)
{
	const char	*min, *max;

	if (strchr(p, '-') == NULL) {
		constraints_parse_asn(fn, p, asids);
		return;
	}

	if (!constraints_split_range(p, &min, &max))
		errx(1, "%s: failed to split range: %s", fn, p);

	constraints_parse_asn_range(fn, min, max, asids);
}

/*
 * Work around an annoying bug in X509v3_addr_add_range(). The upper bound
 * of a range can have unused bits set in its ASN1_BIT_STRING representation.
 * This triggers a check in ip_addr_parse(). A round trip through DER fixes
 * this mess up. For extra special fun, {d2i,i2d}_IPAddrBlocks() isn't part
 * of the API and implementing them for OpenSSL 3 is hairy, so do the round
 * tripping once per address family.
 */
static void
constraints_normalize_ip_addrblocks(const char *fn, IPAddrBlocks **addrs)
{
	IPAddrBlocks		*new_addrs;
	IPAddressFamily		*af;
	const unsigned char	*p;
	unsigned char		*der;
	int			 der_len, i;

	if ((new_addrs = IPAddrBlocks_new()) == NULL)
		err(1, NULL);

	for (i = 0; i < sk_IPAddressFamily_num(*addrs); i++) {
		af = sk_IPAddressFamily_value(*addrs, i);

		der = NULL;
		if ((der_len = i2d_IPAddressFamily(af, &der)) <= 0)
			errx(1, "%s: failed to convert to DER", fn);
		p = der;
		if ((af = d2i_IPAddressFamily(NULL, &p, der_len)) == NULL)
			errx(1, "%s: failed to convert from DER", fn);
		free(der);

		if (!sk_IPAddressFamily_push(new_addrs, af))
			errx(1, "%s: failed to push constraints", fn);
	}

	IPAddrBlocks_free(*addrs);
	*addrs = new_addrs;
}

/*
 * If there is a constraints file for tals[talid], load it into a buffer
 * and parse it line by line. Leverage the above parse helpers to build up
 * IPAddrBlocks and ASIdentifiers. We use the RFC 3779 API to benefit from
 * the limited abilities of X509v3_{addr,asid}_canonize() to sort and merge
 * adjacent ranges. This doesn't deal with overlaps or duplicates, but it's
 * better than nothing.
 */

static void
constraints_parse_talid(int talid)
{
	IPAddrBlocks	*allow_addrs, *deny_addrs;
	ASIdentifiers	*allow_asids, *deny_asids;
	FILE		*f;
	char		*fn, *p, *pp;
	struct cert_as	*allow_ases = NULL, *deny_ases = NULL;
	struct cert_ip	*allow_ips = NULL, *deny_ips = NULL;
	size_t		 num_allow_ases = 0, num_allow_ips = 0,
			 num_deny_as = 0, num_deny_ips = 0;
	char		*line = NULL;
	size_t		 len = 0;
	ssize_t		 n;
	int		 fd, have_allow_as = 0, have_allow_ips = 0,
			 have_deny_as = 0, have_deny_ips = 0;

	fd = tal_constraints[talid].fd;
	fn = tal_constraints[talid].fn;
	tal_constraints[talid].fd = -1;
	tal_constraints[talid].fn = NULL;

	if (fd == -1) {
		free(fn);
		return;
	}

	if ((f = fdopen(fd, "r")) == NULL)
		err(1, "fdopen");

	if ((allow_addrs = IPAddrBlocks_new()) == NULL)
		err(1, NULL);
	if ((allow_asids = ASIdentifiers_new()) == NULL)
		err(1, NULL);
	if ((deny_addrs = IPAddrBlocks_new()) == NULL)
		err(1, NULL);
	if ((deny_asids = ASIdentifiers_new()) == NULL)
		err(1, NULL);

	while ((n = getline(&line, &len, f)) != -1) {
		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

		p = line;

		/* Zap leading whitespace */
		while (isspace((unsigned char)*p))
			p++;

		/* Zap comments */
		if ((pp = strchr(p, '#')) != NULL)
			*pp = '\0';

		/* Zap trailing whitespace */
		if (pp == NULL)
			pp = p + strlen(p);
		while (pp > p && isspace((unsigned char)pp[-1]))
			pp--;
		*pp = '\0';

		if (strlen(p) == 0)
			continue;

		if (strncmp(p, "allow", strlen("allow")) == 0) {
			p += strlen("allow");

			/* Ensure there's whitespace and jump over it. */
			if (!isspace((unsigned char)*p))
				errx(1, "%s: failed to parse %s", fn, p);
			while (isspace((unsigned char)*p))
				p++;

			if (strchr(p, '.') != NULL) {
				constraints_parse_ip(fn, p, AFI_IPV4,
				    allow_addrs);
				have_allow_ips = 1;
			} else if (strchr(p, ':') != NULL) {
				constraints_parse_ip(fn, p, AFI_IPV6,
				    allow_addrs);
				have_allow_ips = 1;
			} else {
				constraints_parse_as(fn, p, allow_asids);
				have_allow_as = 1;
			}
		} else if (strncmp(p, "deny", strlen("deny")) == 0) {
			p += strlen("deny");

			/* Ensure there's whitespace and jump over it. */
			if (!isspace((unsigned char)*p))
				errx(1, "%s: failed to parse %s", fn, p);
			/* Zap leading whitespace */
			while (isspace((unsigned char)*p))
				p++;

			if (strchr(p, '.') != NULL) {
				constraints_parse_ip(fn, p, AFI_IPV4,
				    deny_addrs);
				have_deny_ips = 1;
			} else if (strchr(p, ':') != NULL) {
				constraints_parse_ip(fn, p, AFI_IPV6,
				    deny_addrs);
				have_deny_ips = 1;
			} else {
				constraints_parse_as(fn, p, deny_asids);
				have_deny_as = 1;
			}
		} else
			errx(1, "%s: failed to parse %s", fn, p);
	}
	free(line);

	if (ferror(f))
		err(1, "%s", fn);
	fclose(f);

	if (!X509v3_addr_canonize(allow_addrs))
		errx(1, "%s: failed to canonize IP addresses allowlist", fn);
	if (!X509v3_asid_canonize(allow_asids))
		errx(1, "%s: failed to canonize AS numbers allowlist", fn);
	if (!X509v3_addr_canonize(deny_addrs))
		errx(1, "%s: failed to canonize IP addresses denylist", fn);
	if (!X509v3_asid_canonize(deny_asids))
		errx(1, "%s: failed to canonize AS numbers denylist", fn);

	if (have_allow_as) {
		if (!sbgp_parse_assysnum(fn, allow_asids, &allow_ases,
		    &num_allow_ases))
			errx(1, "%s: failed to parse AS identifiers allowlist",
			    fn);
	}
	if (have_deny_as) {
		if (!sbgp_parse_assysnum(fn, deny_asids, &deny_ases,
		    &num_deny_as))
			errx(1, "%s: failed to parse AS identifiers denylist",
			    fn);
	}
	if (have_allow_ips) {
		constraints_normalize_ip_addrblocks(fn, &allow_addrs);

		if (!sbgp_parse_ipaddrblk(fn, allow_addrs, &allow_ips,
		    &num_allow_ips))
			errx(1, "%s: failed to parse IP addresses allowlist",
			    fn);
	}
	if (have_deny_ips) {
		constraints_normalize_ip_addrblocks(fn, &deny_addrs);

		if (!sbgp_parse_ipaddrblk(fn, deny_addrs, &deny_ips,
		    &num_deny_ips))
			errx(1, "%s: failed to parse IP addresses denylist",
			    fn);
	}

	tal_constraints[talid].allow_ases = allow_ases;
	tal_constraints[talid].num_allow_ases = num_allow_ases;
	tal_constraints[talid].allow_ips = allow_ips;
	tal_constraints[talid].num_allow_ips = num_allow_ips;
	tal_constraints[talid].deny_ases = deny_ases;
	tal_constraints[talid].num_deny_ases = num_deny_as;
	tal_constraints[talid].deny_ips = deny_ips;
	tal_constraints[talid].num_deny_ips = num_deny_ips;

	IPAddrBlocks_free(allow_addrs);
	IPAddrBlocks_free(deny_addrs);
	ASIdentifiers_free(allow_asids);
	ASIdentifiers_free(deny_asids);

	free(fn);
}

/*
 * Iterate over all TALs and parse the constraints files loaded previously.
 */
void
constraints_parse(void)
{
	int	 talid;

	for (talid = 0; talid < talsz; talid++)
		constraints_parse_talid(talid);
}

static int
constraints_check_as(const char *fn, struct cert_as *cert,
    const struct cert_as *allow_ases, size_t num_allow_ases,
    const struct cert_as *deny_ases, size_t num_deny_ases)
{
	uint32_t min, max;

	/* Inheriting EE resources are not to be constrained. */
	if (cert->type == CERT_AS_INHERIT)
		return 1;

	if (cert->type == CERT_AS_ID) {
		min = cert->id;
		max = cert->id;
	} else {
		min = cert->range.min;
		max = cert->range.max;
	}

	if (deny_ases != NULL) {
		if (!as_check_overlap(cert, fn, deny_ases, num_deny_ases, 1))
			return 0;
	}
	if (allow_ases != NULL) {
		if (as_check_covered(min, max, allow_ases, num_allow_ases) <= 0)
			return 0;
	}
	return 1;
}

static int
constraints_check_ips(const char *fn, struct cert_ip *cert,
    const struct cert_ip *allow_ips, size_t num_allow_ips,
    const struct cert_ip *deny_ips, size_t num_deny_ips)
{
	/* Inheriting EE resources are not to be constrained. */
	if (cert->type == CERT_IP_INHERIT)
		return 1;

	if (deny_ips != NULL) {
		if (!ip_addr_check_overlap(cert, fn, deny_ips, num_deny_ips, 1))
			return 0;
	}
	if (allow_ips != NULL) {
		if (ip_addr_check_covered(cert->afi, cert->min, cert->max,
		    allow_ips, num_allow_ips) <= 0)
			return 0;
	}
	return 1;
}

/*
 * Check whether an EE cert's resources are covered by its TAL's constraints.
 * We accept certs with a negative talid as "unknown TAL" for filemode. The
 * logic nearly duplicates valid_cert().
 */
int
constraints_validate(const char *fn, const struct cert *cert)
{
	int		 talid = cert->talid;
	struct cert_as	*allow_ases, *deny_ases;
	struct cert_ip	*allow_ips, *deny_ips;
	size_t		 num_allow_ases, num_allow_ips;
	size_t		 num_deny_ases, num_deny_ips;
	size_t		 i;

	/* Accept negative talid to bypass validation. */
	if (talid < 0)
		return 1;
	if (talid >= talsz)
		errx(1, "%s: talid out of range %d", fn, talid);

	allow_ases = tal_constraints[talid].allow_ases;
	num_allow_ases = tal_constraints[talid].num_allow_ases;
	deny_ases = tal_constraints[talid].deny_ases;
	num_deny_ases = tal_constraints[talid].num_deny_ases;

	for (i = 0; i < cert->num_ases; i++) {
		if (constraints_check_as(fn, &cert->ases[i],
		    allow_ases, num_allow_ases, deny_ases, num_deny_ases))
			continue;

		as_warn(fn, tal_constraints[talid].warn, &cert->ases[i]);
		return 0;
	}

	allow_ips = tal_constraints[talid].allow_ips;
	num_allow_ips = tal_constraints[talid].num_allow_ips;
	deny_ips = tal_constraints[talid].deny_ips;
	num_deny_ips = tal_constraints[talid].num_deny_ips;

	for (i = 0; i < cert->num_ips; i++) {
		if (constraints_check_ips(fn, &cert->ips[i], allow_ips,
		    num_allow_ips, deny_ips, num_deny_ips))
			continue;

		ip_warn(fn, tal_constraints[talid].warn, &cert->ips[i]);
		return 0;
	}

	return 1;
}
