/*	$OpenBSD: aspa.c,v 1.39 2025/08/25 04:13:56 tb Exp $ */
/*
 * Copyright (c) 2022 Job Snijders <job@fastly.com>
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/stack.h>
#include <openssl/safestack.h>
#include <openssl/x509.h>

#include "extern.h"
#include "rpki-asn1.h"

/*
 * ASPA eContent definition in draft-ietf-sidrops-aspa-profile-20, section 3.
 */

ASN1_ITEM_EXP ASProviderAttestation_it;

ASN1_SEQUENCE(ASProviderAttestation) = {
	ASN1_EXP_OPT(ASProviderAttestation, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(ASProviderAttestation, customerASID, ASN1_INTEGER),
	ASN1_SEQUENCE_OF(ASProviderAttestation, providers, ASN1_INTEGER),
} ASN1_SEQUENCE_END(ASProviderAttestation);

IMPLEMENT_ASN1_FUNCTIONS(ASProviderAttestation);

/*
 * Parse the ProviderASSet sequence.
 * Return zero on failure, non-zero on success.
 */
static int
aspa_parse_providers(const char *fn, struct aspa *aspa,
    const STACK_OF(ASN1_INTEGER) *providers)
{
	const ASN1_INTEGER	*pa;
	uint32_t		 provider;
	size_t			 providersz, i;

	if ((providersz = sk_ASN1_INTEGER_num(providers)) == 0) {
		warnx("%s: ASPA: ProviderASSet needs at least one entry", fn);
		return 0;
	}

	if (providersz >= MAX_ASPA_PROVIDERS) {
		warnx("%s: ASPA: too many providers (more than %d)", fn,
		    MAX_ASPA_PROVIDERS);
		return 0;
	}

	aspa->providers = calloc(providersz, sizeof(provider));
	if (aspa->providers == NULL)
		err(1, NULL);

	for (i = 0; i < providersz; i++) {
		pa = sk_ASN1_INTEGER_value(providers, i);

		memset(&provider, 0, sizeof(provider));

		if (!as_id_parse(pa, &provider)) {
			warnx("%s: ASPA: malformed ProviderAS", fn);
			return 0;
		}

		if (aspa->custasid == provider) {
			warnx("%s: ASPA: CustomerASID can't also be Provider",
			    fn);
			return 0;
		}

		if (i > 0) {
			if (aspa->providers[i - 1] > provider) {
				warnx("%s: ASPA: invalid ProviderASSet order",
				    fn);
				return 0;
			}
			if (aspa->providers[i - 1] == provider) {
				warnx("%s: ASPA: duplicate ProviderAS", fn);
				return 0;
			}
		}

		aspa->providers[aspa->num_providers++] = provider;
	}

	return 1;
}

/*
 * Parse the eContent of an ASPA file.
 * Returns zero on failure, non-zero on success.
 */
static int
aspa_parse_econtent(const char *fn, struct aspa *aspa, const unsigned char *d,
    size_t dsz)
{
	const unsigned char	*oder;
	ASProviderAttestation	*aspa_asn1;
	int			 rc = 0;

	oder = d;
	if ((aspa_asn1 = d2i_ASProviderAttestation(NULL, &d, dsz)) == NULL) {
		warnx("%s: ASPA: failed to parse ASProviderAttestation", fn);
		goto out;
	}
	if (d != oder + dsz) {
		warnx("%s: %td bytes trailing garbage in eContent", fn,
		    oder + dsz - d);
		goto out;
	}

	if (!valid_econtent_version(fn, aspa_asn1->version, 1))
		goto out;

	if (!as_id_parse(aspa_asn1->customerASID, &aspa->custasid)) {
		warnx("%s: malformed CustomerASID", fn);
		goto out;
	}

	if (!aspa_parse_providers(fn, aspa, aspa_asn1->providers))
		goto out;

	rc = 1;
 out:
	ASProviderAttestation_free(aspa_asn1);
	return rc;
}

/*
 * Parse a full ASPA file.
 * Returns the payload or NULL if the file was malformed.
 */
struct aspa *
aspa_parse(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len)
{
	struct aspa	*aspa;
	struct cert	*cert = NULL;
	size_t		 cmsz;
	unsigned char	*cms;
	time_t		 signtime = 0;
	int		 rc = 0;

	assert(*out_cert == NULL);

	cms = cms_parse_validate(&cert, fn, talid, der, len, aspa_oid, &cmsz,
	    &signtime);
	if (cms == NULL)
		return NULL;

	if ((aspa = calloc(1, sizeof(*aspa))) == NULL)
		err(1, NULL);
	aspa->signtime = signtime;

	if (cert->num_ips > 0) {
		warnx("%s: superfluous IP Resources extension present", fn);
		goto out;
	}

	if (x509_any_inherits(cert->x509)) {
		warnx("%s: inherit elements not allowed in EE cert", fn);
		goto out;
	}

	if (!aspa_parse_econtent(fn, aspa, cms, cmsz))
		goto out;

	aspa->valid = valid_aspa(fn, cert, aspa);

	*out_cert = cert;
	cert = NULL;

	rc = 1;
 out:
	if (rc == 0) {
		aspa_free(aspa);
		aspa = NULL;
	}
	cert_free(cert);
	free(cms);
	return aspa;
}

/*
 * Free an ASPA pointer.
 * Safe to call with NULL.
 */
void
aspa_free(struct aspa *p)
{
	if (p == NULL)
		return;

	free(p->providers);
	free(p);
}

/*
 * Serialise parsed ASPA content.
 * See aspa_read() for the reader on the other side.
 */
void
aspa_buffer(struct ibuf *b, const struct aspa *p)
{
	io_simple_buffer(b, &p->valid, sizeof(p->valid));
	io_simple_buffer(b, &p->custasid, sizeof(p->custasid));
	io_simple_buffer(b, &p->talid, sizeof(p->talid));
	io_simple_buffer(b, &p->expires, sizeof(p->expires));

	io_simple_buffer(b, &p->num_providers, sizeof(size_t));
	io_simple_buffer(b, p->providers,
	    p->num_providers * sizeof(p->providers[0]));
}

/*
 * Read parsed ASPA content from descriptor.
 * See aspa_buffer() for writer.
 * Result must be passed to aspa_free().
 */
struct aspa *
aspa_read(struct ibuf *b)
{
	struct aspa	*p;

	if ((p = calloc(1, sizeof(struct aspa))) == NULL)
		err(1, NULL);

	io_read_buf(b, &p->valid, sizeof(p->valid));
	io_read_buf(b, &p->custasid, sizeof(p->custasid));
	io_read_buf(b, &p->talid, sizeof(p->talid));
	io_read_buf(b, &p->expires, sizeof(p->expires));

	io_read_buf(b, &p->num_providers, sizeof(size_t));

	if (p->num_providers > 0) {
		if ((p->providers = calloc(p->num_providers,
		    sizeof(p->providers[0]))) == NULL)
			err(1, NULL);
		io_read_buf(b, p->providers,
		    p->num_providers * sizeof(p->providers[0]));
	}

	return p;
}

/*
 * Insert a new uint32_t at index idx in the struct vap v.
 * All elements in the provider array from idx are moved up by one
 * to make space for the new element.
 */
static void
insert_vap(struct vap *v, uint32_t idx, uint32_t *p)
{
	if (idx < v->num_providers)
		memmove(v->providers + idx + 1, v->providers + idx,
		    (v->num_providers - idx) * sizeof(v->providers[0]));
	v->providers[idx] = *p;
	v->num_providers++;
}

/*
 * Add each ProviderAS entry into the Validated ASPA Providers (VAP) tree.
 * Duplicated entries are merged.
 */
void
aspa_insert_vaps(char *fn, struct vap_tree *tree, struct aspa *aspa,
    struct repo *rp)
{
	struct vap	*v, *found;
	size_t		 i, j;

	if ((v = calloc(1, sizeof(*v))) == NULL)
		err(1, NULL);
	v->custasid = aspa->custasid;
	v->talid = aspa->talid;
	v->repoid = repo_id(rp);
	v->expires = aspa->expires;

	if ((found = RB_INSERT(vap_tree, tree, v)) != NULL) {
		if (found->overflowed) {
			free(v);
			return;
		}
		if (found->expires > v->expires) {
			/* decrement found */
			repo_stat_inc(repo_byid(found->repoid), found->talid,
			    RTYPE_ASPA, STYPE_DEC_UNIQUE);
			found->expires = v->expires;
			found->talid = v->talid;
			found->repoid = v->repoid;
			repo_stat_inc(rp, v->talid, RTYPE_ASPA, STYPE_UNIQUE);
		}
		free(v);
		v = found;
	} else
		repo_stat_inc(rp, v->talid, RTYPE_ASPA, STYPE_UNIQUE);

	repo_stat_inc(rp, aspa->talid, RTYPE_ASPA, STYPE_TOTAL);

	v->providers = reallocarray(v->providers,
	    v->num_providers + aspa->num_providers, sizeof(v->providers[0]));
	if (v->providers == NULL)
		err(1, NULL);

	/*
	 * Merge all data from aspa into v: loop over all aspa providers,
	 * insert them in the right place in v->providers while keeping the
	 * order of the providers array.
	 */
	for (i = 0, j = 0; i < aspa->num_providers; ) {
		if (j == v->num_providers ||
		    aspa->providers[i] < v->providers[j]) {
			/* merge provider from aspa into v */
			repo_stat_inc(rp, v->talid, RTYPE_ASPA,
			    STYPE_PROVIDERS);
			insert_vap(v, j, &aspa->providers[i]);
			i++;
		} else if (aspa->providers[i] == v->providers[j])
			i++;

		if (j < v->num_providers)
			j++;
	}

	if (v->num_providers >= MAX_ASPA_PROVIDERS) {
		v->overflowed = 1;
		free(v->providers);
		v->providers = NULL;
		v->num_providers = 0;
		repo_stat_inc(rp, v->talid, RTYPE_ASPA, STYPE_OVERFLOW);
		warnx("%s: too many providers for ASPA Customer ASID %u "
		    "(more than %d)", fn, v->custasid, MAX_ASPA_PROVIDERS);
		return;
	}
}

static inline int
vapcmp(struct vap *a, struct vap *b)
{
	if (a->custasid > b->custasid)
		return 1;
	if (a->custasid < b->custasid)
		return -1;

	return 0;
}

RB_GENERATE(vap_tree, vap, entry, vapcmp);
