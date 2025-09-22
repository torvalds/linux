/*	$OpenBSD: mft.c,v 1.132 2025/09/11 08:21:00 tb Exp $ */
/*
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
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/safestack.h>
#include <openssl/sha.h>
#include <openssl/stack.h>
#include <openssl/x509.h>

#include "extern.h"
#include "rpki-asn1.h"

/*
 * Manifest eContent definition in RFC 9286, section 4.2.
 */

ASN1_ITEM_EXP Manifest_it;
ASN1_ITEM_EXP FileAndHash_it;

ASN1_SEQUENCE(Manifest) = {
	ASN1_EXP_OPT(Manifest, version, ASN1_INTEGER, 0),
	ASN1_SIMPLE(Manifest, manifestNumber, ASN1_INTEGER),
	ASN1_SIMPLE(Manifest, thisUpdate, ASN1_GENERALIZEDTIME),
	ASN1_SIMPLE(Manifest, nextUpdate, ASN1_GENERALIZEDTIME),
	ASN1_SIMPLE(Manifest, fileHashAlg, ASN1_OBJECT),
	ASN1_SEQUENCE_OF(Manifest, fileList, FileAndHash),
} ASN1_SEQUENCE_END(Manifest);

IMPLEMENT_ASN1_FUNCTIONS(Manifest);

ASN1_SEQUENCE(FileAndHash) = {
	ASN1_SIMPLE(FileAndHash, file, ASN1_IA5STRING),
	ASN1_SIMPLE(FileAndHash, hash, ASN1_BIT_STRING),
} ASN1_SEQUENCE_END(FileAndHash);

/*
 * Determine rtype corresponding to file extension. Returns RTYPE_INVALID
 * on error or unknown extension.
 */
enum rtype
rtype_from_file_extension(const char *fn)
{
	size_t	 sz;

	sz = strlen(fn);
	if (sz < 5)
		return RTYPE_INVALID;

	if (strcasecmp(fn + sz - 4, ".tal") == 0)
		return RTYPE_TAL;
	if (strcasecmp(fn + sz - 4, ".cer") == 0)
		return RTYPE_CER;
	if (strcasecmp(fn + sz - 4, ".crl") == 0)
		return RTYPE_CRL;
	if (strcasecmp(fn + sz - 4, ".mft") == 0)
		return RTYPE_MFT;
	if (strcasecmp(fn + sz - 4, ".roa") == 0)
		return RTYPE_ROA;
	if (strcasecmp(fn + sz - 4, ".gbr") == 0)
		return RTYPE_GBR;
	if (strcasecmp(fn + sz - 4, ".sig") == 0)
		return RTYPE_RSC;
	if (strcasecmp(fn + sz - 4, ".asa") == 0)
		return RTYPE_ASPA;
	if (strcasecmp(fn + sz - 4, ".tak") == 0)
		return RTYPE_TAK;
	if (strcasecmp(fn + sz - 4, ".csv") == 0)
		return RTYPE_GEOFEED;
	if (strcasecmp(fn + sz - 4, ".spl") == 0)
		return RTYPE_SPL;
	if (strcasecmp(fn + sz - 4, ".ccr") == 0)
		return RTYPE_CCR;

	return RTYPE_INVALID;
}

/*
 * Validate that a filename listed on a Manifest only contains characters
 * permitted in RFC 9286 section 4.2.2.
 * Also ensure that there is exactly one '.'.
 */
static int
valid_mft_filename(const char *fn, size_t len)
{
	const unsigned char *c;

	if (!valid_filename(fn, len))
		return 0;

	c = memchr(fn, '.', len);
	if (c == NULL || c != memrchr(fn, '.', len))
		return 0;

	return 1;
}

/*
 * Check that the file is allowed to be part of a manifest and the parser
 * for this type is implemented in rpki-client.
 * Returns corresponding rtype or RTYPE_INVALID to mark the file as unknown.
 */
static enum rtype
rtype_from_mftfile(const char *fn)
{
	enum rtype		 type;

	type = rtype_from_file_extension(fn);
	switch (type) {
	case RTYPE_CER:
	case RTYPE_CRL:
	case RTYPE_GBR:
	case RTYPE_ROA:
	case RTYPE_ASPA:
	case RTYPE_SPL:
	case RTYPE_TAK:
		return type;
	default:
		return RTYPE_INVALID;
	}
}

/*
 * Parse an individual "FileAndHash", RFC 9286, sec. 4.2.
 * Return zero on failure, non-zero on success.
 */
static int
mft_parse_filehash(const char *fn, struct mft *mft, const FileAndHash *fh,
    int *found_crl)
{
	char			*file = NULL;
	int			 rc = 0;
	struct mftfile		*fent;
	enum rtype		 type;
	size_t			 new_idx = 0;

	if (!valid_mft_filename(fh->file->data, fh->file->length)) {
		warnx("%s: RFC 9286 section 4.2.2: bad filename", fn);
		goto out;
	}
	file = strndup(fh->file->data, fh->file->length);
	if (file == NULL)
		err(1, NULL);

	if (fh->hash->length != SHA256_DIGEST_LENGTH) {
		warnx("%s: RFC 9286 section 4.2.1: hash: "
		    "invalid SHA256 length, have %d", fn, fh->hash->length);
		goto out;
	}

	type = rtype_from_mftfile(file);
	if (type == RTYPE_CRL) {
		if (*found_crl == 1) {
			warnx("%s: RFC 6487: too many CRLs listed on MFT", fn);
			goto out;
		}
		if (strcmp(file, mft->crl) != 0) {
			warnx("%s: RFC 6487: name (%s) doesn't match CRLDP "
			    "(%s)", fn, file, mft->crl);
			goto out;
		}
		/* remember the filehash for the CRL in struct mft */
		memcpy(mft->crlhash, fh->hash->data, SHA256_DIGEST_LENGTH);
		*found_crl = 1;
	}

	if (filemode)
		fent = &mft->files[mft->filesz++];
	else {
		/* Fisher-Yates shuffle */
		new_idx = arc4random_uniform(mft->filesz + 1);
		mft->files[mft->filesz++] = mft->files[new_idx];
		fent = &mft->files[new_idx];
	}

	fent->type = type;
	fent->file = file;
	file = NULL;
	memcpy(fent->hash, fh->hash->data, SHA256_DIGEST_LENGTH);

	rc = 1;
 out:
	free(file);
	return rc;
}

static int
mft_fh_cmp_name(const FileAndHash *const *a, const FileAndHash *const *b)
{
	if ((*a)->file->length < (*b)->file->length)
		return -1;
	if ((*a)->file->length > (*b)->file->length)
		return 1;

	return memcmp((*a)->file->data, (*b)->file->data, (*b)->file->length);
}

static int
mft_fh_cmp_hash(const FileAndHash *const *a, const FileAndHash *const *b)
{
	assert((*a)->hash->length == SHA256_DIGEST_LENGTH);
	assert((*b)->hash->length == SHA256_DIGEST_LENGTH);

	return memcmp((*a)->hash->data, (*b)->hash->data, (*b)->hash->length);
}

/*
 * Assuming that the hash lengths are validated, this checks that all file names
 * and hashes in a manifest are unique. Returns 1 on success, 0 on failure.
 */
static int
mft_has_unique_names_and_hashes(const char *fn, const Manifest *mft)
{
	STACK_OF(FileAndHash)	*fhs;
	int			 i, ret = 0;

	if ((fhs = sk_FileAndHash_dup(mft->fileList)) == NULL)
		err(1, NULL);

	(void)sk_FileAndHash_set_cmp_func(fhs, mft_fh_cmp_name);
	sk_FileAndHash_sort(fhs);

	for (i = 0; i < sk_FileAndHash_num(fhs) - 1; i++) {
		const FileAndHash *curr = sk_FileAndHash_value(fhs, i);
		const FileAndHash *next = sk_FileAndHash_value(fhs, i + 1);

		if (mft_fh_cmp_name(&curr, &next) == 0) {
			warnx("%s: duplicate name: %.*s", fn,
			    curr->file->length, curr->file->data);
			goto err;
		}
	}

	(void)sk_FileAndHash_set_cmp_func(fhs, mft_fh_cmp_hash);
	sk_FileAndHash_sort(fhs);

	for (i = 0; i < sk_FileAndHash_num(fhs) - 1; i++) {
		const FileAndHash *curr = sk_FileAndHash_value(fhs, i);
		const FileAndHash *next = sk_FileAndHash_value(fhs, i + 1);

		if (mft_fh_cmp_hash(&curr, &next) == 0) {
			warnx("%s: duplicate hash for %.*s and %.*s", fn,
			    curr->file->length, curr->file->data,
			    next->file->length, next->file->data);
			goto err;
		}
	}

	ret = 1;

 err:
	sk_FileAndHash_free(fhs);

	return ret;
}

/*
 * Handle the eContent of the manifest object, RFC 9286 sec. 4.2.
 * Returns 0 on failure and 1 on success.
 */
static int
mft_parse_econtent(const char *fn, struct mft *mft, const unsigned char *d,
    size_t dsz)
{
	const unsigned char	*oder;
	Manifest		*mft_asn1;
	FileAndHash		*fh;
	int			 found_crl, i, rc = 0;

	oder = d;
	if ((mft_asn1 = d2i_Manifest(NULL, &d, dsz)) == NULL) {
		warnx("%s: RFC 9286 section 4: failed to parse Manifest", fn);
		goto out;
	}
	if (d != oder + dsz) {
		warnx("%s: %td bytes trailing garbage in eContent", fn,
		    oder + dsz - d);
		goto out;
	}

	if (!valid_econtent_version(fn, mft_asn1->version, 0))
		goto out;

	mft->seqnum = x509_convert_seqnum(fn, "manifest number",
	    mft_asn1->manifestNumber);
	if (mft->seqnum == NULL)
		goto out;

	if (!x509_get_generalized_time(fn, "manifest thisUpdate",
	    mft_asn1->thisUpdate, &mft->thisupdate))
		goto out;

	if (!x509_get_generalized_time(fn, "manifest nextUpdate",
	    mft_asn1->nextUpdate, &mft->nextupdate))
		goto out;

	if (mft->thisupdate > mft->nextupdate) {
		warnx("%s: bad update interval", fn);
		goto out;
	}

	if (OBJ_obj2nid(mft_asn1->fileHashAlg) != NID_sha256) {
		warnx("%s: RFC 9286 section 4.2.1: fileHashAlg: "
		    "want SHA256 object, have %s", fn,
		    nid2str(OBJ_obj2nid(mft_asn1->fileHashAlg)));
		goto out;
	}

	if (sk_FileAndHash_num(mft_asn1->fileList) <= 0) {
		warnx("%s: no files in manifest fileList", fn);
		goto out;
	}
	if (sk_FileAndHash_num(mft_asn1->fileList) >= MAX_MANIFEST_ENTRIES) {
		warnx("%s: %d exceeds manifest entry limit (%d)", fn,
		    sk_FileAndHash_num(mft_asn1->fileList),
		    MAX_MANIFEST_ENTRIES);
		goto out;
	}

	mft->files = calloc(sk_FileAndHash_num(mft_asn1->fileList),
	    sizeof(struct mftfile));
	if (mft->files == NULL)
		err(1, NULL);

	found_crl = 0;
	for (i = 0; i < sk_FileAndHash_num(mft_asn1->fileList); i++) {
		fh = sk_FileAndHash_value(mft_asn1->fileList, i);
		if (!mft_parse_filehash(fn, mft, fh, &found_crl))
			goto out;
	}

	if (!found_crl) {
		warnx("%s: CRL not part of MFT fileList", fn);
		goto out;
	}

	if (!mft_has_unique_names_and_hashes(fn, mft_asn1))
		goto out;

	rc = 1;
 out:
	Manifest_free(mft_asn1);
	return rc;
}

/*
 * Parse the objects that have been published in the manifest.
 * Return mft if it conforms to RFC 9286, otherwise NULL.
 */
struct mft *
mft_parse(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len)
{
	struct mft	*mft;
	struct cert	*cert = NULL;
	int		 rc = 0;
	size_t		 cmsz;
	unsigned char	*cms;
	char		*crlfile;
	time_t		 signtime = 0;

	assert(*out_cert == NULL);

	cms = cms_parse_validate(&cert, fn, talid, der, len, mft_oid, &cmsz,
	    &signtime);
	if (cms == NULL)
		return NULL;

	if ((mft = calloc(1, sizeof(*mft))) == NULL)
		err(1, NULL);
	mft->signtime = signtime;
	mft->mftsize = len;

	if ((mft->aki = strdup(cert->aki)) == NULL)
		err(1, NULL);
	if ((mft->sia = strdup(cert->signedobj)) == NULL)
		err(1, NULL);

	if (!x509_inherits(cert->x509)) {
		warnx("%s: RFC 3779 extension not set to inherit", fn);
		goto out;
	}

	crlfile = strrchr(cert->crl, '/');
	if (crlfile == NULL) {
		warnx("%s: RFC 6487 section 4.8.6: "
		    "invalid CRL distribution point", fn);
		goto out;
	}
	crlfile++;
	if (!valid_mft_filename(crlfile, strlen(crlfile)) ||
	    rtype_from_file_extension(crlfile) != RTYPE_CRL) {
		warnx("%s: RFC 6487 section 4.8.6: CRL: "
		    "bad CRL distribution point extension", fn);
		goto out;
	}
	if ((mft->crl = strdup(crlfile)) == NULL)
		err(1, NULL);

	if (mft_parse_econtent(fn, mft, cms, cmsz) == 0)
		goto out;

	if (mft->signtime > mft->nextupdate) {
		warnx("%s: dating issue: CMS signing-time after MFT nextUpdate",
		    fn);
		goto out;
	}

	*out_cert = cert;
	cert = NULL;

	rc = 1;
out:
	if (rc == 0) {
		mft_free(mft);
		mft = NULL;
	}
	cert_free(cert);
	free(cms);
	return mft;
}

/*
 * Free an MFT pointer.
 * Safe to call with NULL.
 */
void
mft_free(struct mft *p)
{
	size_t	 i;

	if (p == NULL)
		return;

	for (i = 0; i < p->filesz; i++)
		free(p->files[i].file);

	free(p->path);
	free(p->files);
	free(p->seqnum);
	free(p->aki);
	free(p->sia);
	free(p->crl);
	free(p);
}

/*
 * Serialise MFT parsed content into the given buffer.
 * See mft_read() for the other side of the pipe.
 */
void
mft_buffer(struct ibuf *b, const struct mft *p)
{
	size_t		 i;

	io_simple_buffer(b, &p->repoid, sizeof(p->repoid));
	io_simple_buffer(b, &p->talid, sizeof(p->talid));
	io_simple_buffer(b, &p->certid, sizeof(p->certid));
	io_simple_buffer(b, &p->seqnum_gap, sizeof(p->seqnum_gap));
	io_opt_str_buffer(b, p->path);

	io_str_buffer(b, p->aki);
	io_str_buffer(b, p->seqnum);
	io_str_buffer(b, p->sia);
	io_simple_buffer(b, &p->thisupdate, sizeof(p->thisupdate));
	io_simple_buffer(b, p->mfthash, sizeof(p->mfthash));
	io_simple_buffer(b, &p->mftsize, sizeof(p->mftsize));

	io_simple_buffer(b, &p->filesz, sizeof(size_t));
	for (i = 0; i < p->filesz; i++) {
		io_str_buffer(b, p->files[i].file);
		io_simple_buffer(b, &p->files[i].type,
		    sizeof(p->files[i].type));
		io_simple_buffer(b, &p->files[i].location,
		    sizeof(p->files[i].location));
		io_simple_buffer(b, p->files[i].hash, SHA256_DIGEST_LENGTH);
	}
}

/*
 * Read an MFT structure from the file descriptor.
 * Result must be passed to mft_free().
 */
struct mft *
mft_read(struct ibuf *b)
{
	struct mft	*p = NULL;
	size_t		 i;

	if ((p = calloc(1, sizeof(struct mft))) == NULL)
		err(1, NULL);

	io_read_buf(b, &p->repoid, sizeof(p->repoid));
	io_read_buf(b, &p->talid, sizeof(p->talid));
	io_read_buf(b, &p->certid, sizeof(p->certid));
	io_read_buf(b, &p->seqnum_gap, sizeof(p->seqnum_gap));
	io_read_opt_str(b, &p->path);

	io_read_str(b, &p->aki);
	io_read_str(b, &p->seqnum);
	io_read_str(b, &p->sia);
	io_read_buf(b, &p->thisupdate, sizeof(p->thisupdate));
	io_read_buf(b, &p->mfthash, sizeof(p->mfthash));
	io_read_buf(b, &p->mftsize, sizeof(p->mftsize));

	io_read_buf(b, &p->filesz, sizeof(size_t));
	if (p->filesz == 0)
		err(1, "mft_read: bad message");
	if ((p->files = calloc(p->filesz, sizeof(struct mftfile))) == NULL)
		err(1, NULL);

	for (i = 0; i < p->filesz; i++) {
		io_read_str(b, &p->files[i].file);
		io_read_buf(b, &p->files[i].type, sizeof(p->files[i].type));
		io_read_buf(b, &p->files[i].location,
		    sizeof(p->files[i].location));
		io_read_buf(b, p->files[i].hash, SHA256_DIGEST_LENGTH);
	}

	return p;
}

/*
 * Compare the thisupdate time of two mft files.
 */
int
mft_compare_issued(const struct mft *a, const struct mft *b)
{
	if (a->thisupdate > b->thisupdate)
		return 1;
	if (a->thisupdate < b->thisupdate)
		return -1;
	return 0;
}

/*
 * Compare the manifestNumber of two mft files.
 */
int
mft_compare_seqnum(const struct mft *a, const struct mft *b)
{
	int r;

	r = strlen(a->seqnum) - strlen(b->seqnum);
	if (r > 0)	/* seqnum in a is longer -> higher */
		return 1;
	if (r < 0)	/* seqnum in a is shorter -> smaller */
		return -1;

	r = strcmp(a->seqnum, b->seqnum);
	if (r > 0)	/* a is greater, prefer a */
		return 1;
	if (r < 0)	/* b is greater, prefer b */
		return -1;

	return 0;
}

/*
 * Test if there is a gap in the sequence numbers of two MFTs.
 * Return 1 if a gap is detected.
 */
int
mft_seqnum_gap_present(const struct mft *a, const struct mft *b, BN_CTX *bn_ctx)
{
	BIGNUM *diff, *seqnum_a, *seqnum_b;
	int ret = 0;

	BN_CTX_start(bn_ctx);
	if ((diff = BN_CTX_get(bn_ctx)) == NULL ||
	    (seqnum_a = BN_CTX_get(bn_ctx)) == NULL ||
	    (seqnum_b = BN_CTX_get(bn_ctx)) == NULL)
		errx(1, "BN_CTX_get");

	if (!BN_hex2bn(&seqnum_a, a->seqnum))
		errx(1, "BN_hex2bn");

	if (!BN_hex2bn(&seqnum_b, b->seqnum))
		errx(1, "BN_hex2bn");

	if (!BN_sub(diff, seqnum_a, seqnum_b))
		errx(1, "BN_sub");

	ret = !BN_is_one(diff);

	BN_CTX_end(bn_ctx);

	return ret;
}
