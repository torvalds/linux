/*	$OpenBSD: rsc.c,v 1.42 2025/08/24 12:34:39 tb Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/safestack.h>
#include <openssl/stack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "extern.h"
#include "rpki-asn1.h"

/*
 * RSC eContent definition in RFC 9323, section 4.
 */

ASN1_ITEM_EXP RpkiSignedChecklist_it;
ASN1_ITEM_EXP FileNameAndHash_it;
ASN1_ITEM_EXP ResourceBlock_it;
ASN1_ITEM_EXP ConstrainedIPAddrBlocks_it;
ASN1_ITEM_EXP ConstrainedIPAddressFamily_it;
ASN1_ITEM_EXP ConstrainedASIdentifiers_it;

ASN1_SEQUENCE(RpkiSignedChecklist) = {
	ASN1_EXP_OPT(RpkiSignedChecklist, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(RpkiSignedChecklist, resources, ResourceBlock),
	ASN1_SIMPLE(RpkiSignedChecklist, digestAlgorithm, X509_ALGOR),
	ASN1_SEQUENCE_OF(RpkiSignedChecklist, checkList, FileNameAndHash),
} ASN1_SEQUENCE_END(RpkiSignedChecklist);

IMPLEMENT_ASN1_FUNCTIONS(RpkiSignedChecklist);

ASN1_SEQUENCE(FileNameAndHash) = {
	ASN1_OPT(FileNameAndHash, fileName, ASN1_IA5STRING),
	ASN1_SIMPLE(FileNameAndHash, hash, ASN1_OCTET_STRING),
} ASN1_SEQUENCE_END(FileNameAndHash);

ASN1_SEQUENCE(ResourceBlock) = {
	ASN1_EXP_OPT(ResourceBlock, asID, ConstrainedASIdentifiers, 0),
	ASN1_EXP_SEQUENCE_OF_OPT(ResourceBlock, ipAddrBlocks,
	    ConstrainedIPAddressFamily, 1)
} ASN1_SEQUENCE_END(ResourceBlock);

ASN1_ITEM_TEMPLATE(ConstrainedIPAddrBlocks) =
	ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, ConstrainedIPAddrBlocks,
	    ConstrainedIPAddressFamily)
ASN1_ITEM_TEMPLATE_END(ConstrainedIPAddrBlocks);

ASN1_SEQUENCE(ConstrainedIPAddressFamily) = {
	ASN1_SIMPLE(ConstrainedIPAddressFamily, addressFamily,
	    ASN1_OCTET_STRING),
	ASN1_SEQUENCE_OF(ConstrainedIPAddressFamily, addressesOrRanges,
	    IPAddressOrRange),
} ASN1_SEQUENCE_END(ConstrainedIPAddressFamily);

ASN1_SEQUENCE(ConstrainedASIdentifiers) = {
	ASN1_EXP_SEQUENCE_OF(ConstrainedASIdentifiers, asnum, ASIdOrRange, 0),
} ASN1_SEQUENCE_END(ConstrainedASIdentifiers);

/*
 * Parse asID (inside ResourceBlock)
 * Return 0 on failure.
 */
static int
rsc_parse_aslist(const char *fn, struct rsc *rsc,
    const ConstrainedASIdentifiers *asids)
{
	int	 i, num_ases;

	if (asids == NULL)
		return 1;

	if ((num_ases = sk_ASIdOrRange_num(asids->asnum)) == 0) {
		warnx("%s: RSC asID empty", fn);
		return 0;
	}

	if (num_ases >= MAX_AS_SIZE) {
		warnx("%s: too many AS number entries: limit %d",
		    fn, MAX_AS_SIZE);
		return 0;
	}

	if ((rsc->ases = calloc(num_ases, sizeof(struct cert_as))) == NULL)
		err(1, NULL);

	for (i = 0; i < num_ases; i++) {
		const ASIdOrRange *aor;

		aor = sk_ASIdOrRange_value(asids->asnum, i);

		switch (aor->type) {
		case ASIdOrRange_id:
			if (!sbgp_as_id(fn, rsc->ases, &rsc->num_ases,
			    aor->u.id))
				return 0;
			break;
		case ASIdOrRange_range:
			if (!sbgp_as_range(fn, rsc->ases, &rsc->num_ases,
			    aor->u.range))
				return 0;
			break;
		default:
			warnx("%s: RSC AsList: unknown type %d", fn, aor->type);
			return 0;
		}
	}

	return 1;
}

static int
rsc_parse_iplist(const char *fn, struct rsc *rsc,
    const ConstrainedIPAddrBlocks *ipAddrBlocks)
{
	const ConstrainedIPAddressFamily	*af;
	const IPAddressOrRanges			*aors;
	const IPAddressOrRange			*aor;
	size_t					 num_ips;
	enum afi				 afi;
	int					 i, j;

	if (ipAddrBlocks == NULL)
		return 1;

	if (sk_ConstrainedIPAddressFamily_num(ipAddrBlocks) == 0) {
		warnx("%s: RSC ipAddrBlocks empty", fn);
		return 0;
	}

	for (i = 0; i < sk_ConstrainedIPAddressFamily_num(ipAddrBlocks); i++) {
		af = sk_ConstrainedIPAddressFamily_value(ipAddrBlocks, i);
		aors = af->addressesOrRanges;

		num_ips = rsc->num_ips + sk_IPAddressOrRange_num(aors);
		if (num_ips >= MAX_IP_SIZE) {
			warnx("%s: too many IP address entries: limit %d",
			    fn, MAX_IP_SIZE);
			return 0;
		}

		rsc->ips = recallocarray(rsc->ips, rsc->num_ips, num_ips,
		    sizeof(struct cert_ip));
		if (rsc->ips == NULL)
			err(1, NULL);

		if (!ip_addr_afi_parse(fn, af->addressFamily, &afi)) {
			warnx("%s: RSC: invalid AFI", fn);
			return 0;
		}

		for (j = 0; j < sk_IPAddressOrRange_num(aors); j++) {
			aor = sk_IPAddressOrRange_value(aors, j);
			switch (aor->type) {
			case IPAddressOrRange_addressPrefix:
				if (!sbgp_addr(fn, rsc->ips,
				    &rsc->num_ips, afi, aor->u.addressPrefix))
					return 0;
				break;
			case IPAddressOrRange_addressRange:
				if (!sbgp_addr_range(fn, rsc->ips,
				    &rsc->num_ips, afi, aor->u.addressRange))
					return 0;
				break;
			default:
				warnx("%s: RFC 3779: IPAddressOrRange: "
				    "unknown type %d", fn, aor->type);
				return 0;
			}
		}
	}

	return 1;
}

static int
rsc_check_digesttype(const char *fn, struct rsc *rsc, const X509_ALGOR *alg)
{
	const ASN1_OBJECT	*obj;
	int			 type, nid;

	X509_ALGOR_get0(&obj, &type, NULL, alg);

	if (type != V_ASN1_UNDEF) {
		warnx("%s: RSC DigestAlgorithmIdentifier unexpected parameters:"
		    " %d", fn, type);
		return 0;
	}

	if ((nid = OBJ_obj2nid(obj)) != NID_sha256) {
		warnx("%s: RSC DigestAlgorithmIdentifier: want SHA256, have %s",
		    fn, nid2str(nid));
		return 0;
	}

	return 1;
}

/*
 * Parse the FileNameAndHash sequence, RFC 9323, section 4.4.
 * Return zero on failure, non-zero on success.
 */
static int
rsc_parse_checklist(const char *fn, struct rsc *rsc,
    const STACK_OF(FileNameAndHash) *checkList)
{
	FileNameAndHash		*fh;
	ASN1_IA5STRING		*fileName;
	struct rscfile		*file;
	size_t			 num_files, i;

	if ((num_files = sk_FileNameAndHash_num(checkList)) == 0) {
		warnx("%s: RSC checkList needs at least one entry", fn);
		return 0;
	}

	if (num_files >= MAX_CHECKLIST_ENTRIES) {
		warnx("%s: %zu exceeds checklist entry limit (%d)", fn,
		    num_files, MAX_CHECKLIST_ENTRIES);
		return 0;
	}

	rsc->files = calloc(num_files, sizeof(struct rscfile));
	if (rsc->files == NULL)
		err(1, NULL);
	rsc->num_files = num_files;

	for (i = 0; i < num_files; i++) {
		fh = sk_FileNameAndHash_value(checkList, i);

		file = &rsc->files[i];

		if (fh->hash->length != SHA256_DIGEST_LENGTH) {
			warnx("%s: RSC Digest: invalid SHA256 length", fn);
			return 0;
		}
		memcpy(file->hash, fh->hash->data, SHA256_DIGEST_LENGTH);

		if ((fileName = fh->fileName) == NULL)
			continue;

		if (!valid_filename(fileName->data, fileName->length)) {
			warnx("%s: RSC FileNameAndHash: bad filename", fn);
			return 0;
		}

		file->filename = strndup(fileName->data, fileName->length);
		if (file->filename == NULL)
			err(1, NULL);
	}

	return 1;
}

/*
 * Parses the eContent segment of an RSC file
 * RFC 9323, section 4
 * Returns zero on failure, non-zero on success.
 */
static int
rsc_parse_econtent(const char *fn, struct rsc *rsc, const unsigned char *d,
    size_t dsz)
{
	const unsigned char	*oder;
	RpkiSignedChecklist	*rsc_asn1;
	ResourceBlock		*resources;
	int			 rc = 0;

	/*
	 * RFC 9323 section 4
	 */

	oder = d;
	if ((rsc_asn1 = d2i_RpkiSignedChecklist(NULL, &d, dsz)) == NULL) {
		warnx("%s: RSC: failed to parse RpkiSignedChecklist", fn);
		goto out;
	}
	if (d != oder + dsz) {
		warnx("%s: %td bytes trailing garbage in eContent", fn,
		    oder + dsz - d);
		goto out;
	}

	if (!valid_econtent_version(fn, rsc_asn1->version, 0))
		goto out;

	resources = rsc_asn1->resources;
	if (resources->asID == NULL && resources->ipAddrBlocks == NULL) {
		warnx("%s: RSC: one of asID or ipAddrBlocks must be present",
		    fn);
		goto out;
	}

	if (!rsc_parse_aslist(fn, rsc, resources->asID))
		goto out;

	if (!rsc_parse_iplist(fn, rsc, resources->ipAddrBlocks))
		goto out;

	if (!rsc_check_digesttype(fn, rsc, rsc_asn1->digestAlgorithm))
		goto out;

	if (!rsc_parse_checklist(fn, rsc, rsc_asn1->checkList))
		goto out;

	rc = 1;
 out:
	RpkiSignedChecklist_free(rsc_asn1);
	return rc;
}

/*
 * Parse a full RFC 9323 file.
 * Returns the RSC or NULL if the object was malformed.
 */
struct rsc *
rsc_parse(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len)
{
	struct rsc		*rsc;
	struct cert		*cert = NULL;
	unsigned char		*cms;
	size_t			 cmsz;
	time_t			 signtime = 0;
	int			 rc = 0;

	assert(*out_cert == NULL);

	cms = cms_parse_validate(&cert, fn, talid, der, len, rsc_oid, &cmsz,
	    &signtime);
	if (cms == NULL)
		return NULL;

	if ((rsc = calloc(1, sizeof(struct rsc))) == NULL)
		err(1, NULL);
	rsc->signtime = signtime;

	if (x509_any_inherits(cert->x509)) {
		warnx("%s: inherit elements not allowed in EE cert", fn);
		goto out;
	}

	if (!rsc_parse_econtent(fn, rsc, cms, cmsz))
		goto out;

	rsc->valid = valid_rsc(fn, cert, rsc);

	*out_cert = cert;
	cert = NULL;

	rc = 1;
 out:
	if (rc == 0) {
		rsc_free(rsc);
		rsc = NULL;
	}
	cert_free(cert);
	free(cms);
	return rsc;
}

/*
 * Free an RSC pointer.
 * Safe to call with NULL.
 */
void
rsc_free(struct rsc *p)
{
	size_t	i;

	if (p == NULL)
		return;

	for (i = 0; i < p->num_files; i++)
		free(p->files[i].filename);

	free(p->ips);
	free(p->ases);
	free(p->files);
	free(p);
}
