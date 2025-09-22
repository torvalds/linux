/* $OpenBSD: rpki-asn1.h,v 1.7 2025/09/15 11:52:07 job Exp $ */
/*
 * Copyright (c) 2025 Job Snijders <job@openbsd.org>
 * Copyright (c) 2025 Theo Buehler <tb@openbsd.org>
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

#ifndef RPKI_ASN1_H
#define RPKI_ASN1_H

#include <unistd.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>

/*
 * Autonomous System Provider Authorization (ASPA)
 * reference: draft-ietf-sidrops-aspa-profile
 */

extern ASN1_ITEM_EXP ASProviderAttestation_it;

typedef struct {
	ASN1_INTEGER *version;
	ASN1_INTEGER *customerASID;
	STACK_OF(ASN1_INTEGER) *providers;
} ASProviderAttestation;

DECLARE_ASN1_FUNCTIONS(ASProviderAttestation);


/*
 * Canonical Cache Representation (CCR)
 * reference: TBD
 */

extern ASN1_ITEM_EXP ContentInfo_it;
extern ASN1_ITEM_EXP CanonicalCacheRepresentation_it;
extern ASN1_ITEM_EXP ManifestRefs_it;
extern ASN1_ITEM_EXP ManifestRef_it;
extern ASN1_ITEM_EXP ROAPayloadSets_it;
extern ASN1_ITEM_EXP ROAPayloadSet_it;
extern ASN1_ITEM_EXP ASPAPayloadSets_it;
extern ASN1_ITEM_EXP ASPAPayloadSet_it;
extern ASN1_ITEM_EXP SubjectKeyIdentifiers_it;
extern ASN1_ITEM_EXP SubjectKeyIdentifier_it;
extern ASN1_ITEM_EXP RouterKeySets_it;
extern ASN1_ITEM_EXP RouterKeySet_it;
extern ASN1_ITEM_EXP RouterKey_it;

typedef struct {
	ASN1_OCTET_STRING *hash;
	ASN1_INTEGER *size;
	ASN1_OCTET_STRING *aki;
	ASN1_INTEGER *manifestNumber;
	ASN1_GENERALIZEDTIME *thisUpdate;
	STACK_OF(ACCESS_DESCRIPTION) *location;
} ManifestRef;

DECLARE_STACK_OF(ManifestRef);

#ifndef DEFINE_STACK_OF
#define sk_ManifestRef_num(st) SKM_sk_num(ManifestRef, (st))
#define sk_ManifestRef_push(st, i) SKM_sk_push(ManifestRef, (st), (i))
#define sk_ManifestRef_value(st, i) SKM_sk_value(ManifestRef, (st), (i))
#endif

DECLARE_ASN1_FUNCTIONS(ManifestRef);

typedef STACK_OF(ManifestRef) ManifestRefs;

DECLARE_ASN1_FUNCTIONS(ManifestRefs);

typedef struct {
	STACK_OF(ManifestRef) *mftrefs;
	ASN1_GENERALIZEDTIME *mostRecentUpdate;
	ASN1_OCTET_STRING *hash;
} ManifestState;

DECLARE_ASN1_FUNCTIONS(ManifestState);

typedef struct {
	ASN1_INTEGER *asID;
	STACK_OF(ROAIPAddressFamily) *ipAddrBlocks;
} ROAPayloadSet;

DECLARE_STACK_OF(ROAPayloadSet);

#ifndef DEFINE_STACK_OF
#define sk_ROAPayloadSet_num(st) SKM_sk_num(ROAPayloadSet, (st))
#define sk_ROAPayloadSet_push(st, i) SKM_sk_push(ROAPayloadSet, (st), (i))
#define sk_ROAPayloadSet_value(st, i) SKM_sk_value(ROAPayloadSet, (st), (i))
#endif

DECLARE_ASN1_FUNCTIONS(ROAPayloadSet);

typedef STACK_OF(ROAPayloadSet) ROAPayloadSets;

DECLARE_ASN1_FUNCTIONS(ROAPayloadSets);

typedef struct {
	STACK_OF(ROAPayloadSet) *rps;
	ASN1_OCTET_STRING *hash;
} ROAPayloadState;

DECLARE_ASN1_FUNCTIONS(ROAPayloadState);

typedef struct {
	ASN1_INTEGER *asID;
	STACK_OF(ASN1_INTEGER) *providers;
} ASPAPayloadSet;

DECLARE_STACK_OF(ASPAPayloadSet);

#ifndef DEFINE_STACK_OF
#define sk_ASPAPayloadSet_num(st) SKM_sk_num(ASPAPayloadSet, (st))
#define sk_ASPAPayloadSet_push(st, i) SKM_sk_push(ASPAPayloadSet, (st), (i))
#define sk_ASPAPayloadSet_value(st, i) SKM_sk_value(ASPAPayloadSet, (st), (i))
#endif

DECLARE_ASN1_FUNCTIONS(ASPAPayloadSet);

typedef STACK_OF(ASPAPayloadSet) ASPAPayloadSets;

DECLARE_ASN1_FUNCTIONS(ASPAPayloadSets);

typedef struct {
	STACK_OF(ASPAPayloadSet) *aps;
	ASN1_OCTET_STRING *hash;
} ASPAPayloadState;

DECLARE_ASN1_FUNCTIONS(ASPAPayloadState);

typedef ASN1_OCTET_STRING SubjectKeyIdentifier;

DECLARE_ASN1_FUNCTIONS(SubjectKeyIdentifier);

DECLARE_STACK_OF(SubjectKeyIdentifier);

#ifndef DEFINE_STACK_OF
#define sk_SubjectKeyIdentifier_num(st) SKM_sk_num(SubjectKeyIdentifier, (st))
#define sk_SubjectKeyIdentifier_push(st, i) \
    SKM_sk_push(SubjectKeyIdentifier, (st), (i))
#define sk_SubjectKeyIdentifier_value(st, i) \
    SKM_sk_value(SubjectKeyIdentifier, (st), (i))
#endif

typedef STACK_OF(SubjectKeyIdentifier) SubjectKeyIdentifiers;

DECLARE_ASN1_FUNCTIONS(SubjectKeyIdentifiers);

typedef struct {
	STACK_OF(SubjectKeyIdentifier) *skis;
	ASN1_OCTET_STRING *hash;
} TrustAnchorState;

DECLARE_ASN1_FUNCTIONS(TrustAnchorState);

typedef struct {
	STACK_OF(RouterKeySet) *rksets;
	ASN1_OCTET_STRING *hash;
} RouterKeyState;

DECLARE_ASN1_FUNCTIONS(RouterKeyState);

typedef struct {
	ASN1_INTEGER *asID;
	STACK_OF(RouterKey) *routerKeys;
} RouterKeySet;

DECLARE_STACK_OF(RouterKeySet);

#ifndef DEFINE_STACK_OF
#define sk_RouterKeySet_num(st) SKM_sk_num(RouterKeySet, (st))
#define sk_RouterKeySet_push(st, i) SKM_sk_push(RouterKeySet, (st), (i))
#define sk_RouterKeySet_value(st, i) SKM_sk_value(RouterKeySet, (st), (i))
#endif

DECLARE_ASN1_FUNCTIONS(RouterKeySet);

typedef STACK_OF(RouterKeySet) RouterKeySets;

DECLARE_ASN1_FUNCTIONS(RouterKeySets);

typedef struct {
	SubjectKeyIdentifier *ski;
	X509_PUBKEY *spki;
} RouterKey;

DECLARE_STACK_OF(RouterKey);

#ifndef DEFINE_STACK_OF
#define sk_RouterKey_num(st) SKM_sk_num(RouterKey, (st))
#define sk_RouterKey_push(st, i) SKM_sk_push(RouterKey, (st), (i))
#define sk_RouterKey_value(st, i) SKM_sk_value(RouterKey, (st), (i))
#endif

DECLARE_ASN1_FUNCTIONS(RouterKey);

typedef struct {
	ASN1_INTEGER *version;
	ASN1_OBJECT *hashAlg;
	ASN1_GENERALIZEDTIME *producedAt;
	ManifestState *mfts;
	ROAPayloadState *vrps;
	ASPAPayloadState *vaps;
	TrustAnchorState *tas;
	RouterKeyState *rks;
} CanonicalCacheRepresentation;

DECLARE_ASN1_FUNCTIONS(CanonicalCacheRepresentation);

typedef struct {
	ASN1_OBJECT *contentType;
	ASN1_OCTET_STRING *content;
} ContentInfo;

DECLARE_ASN1_FUNCTIONS(ContentInfo);


/*
 * RPKI Manifest
 * reference: RFC 9286.
 */

extern ASN1_ITEM_EXP FileAndHash_it;
extern ASN1_ITEM_EXP Manifest_it;

typedef struct {
	ASN1_IA5STRING *file;
	ASN1_BIT_STRING	*hash;
} FileAndHash;

DECLARE_STACK_OF(FileAndHash);

#ifndef DEFINE_STACK_OF
#define sk_FileAndHash_dup(sk)		SKM_sk_dup(FileAndHash, (sk))
#define sk_FileAndHash_free(sk)		SKM_sk_free(FileAndHash, (sk))
#define sk_FileAndHash_num(sk)		SKM_sk_num(FileAndHash, (sk))
#define sk_FileAndHash_value(sk, i)	SKM_sk_value(FileAndHash, (sk), (i))
#define sk_FileAndHash_sort(sk)		SKM_sk_sort(FileAndHash, (sk))
#define sk_FileAndHash_set_cmp_func(sk, cmp) \
    SKM_sk_set_cmp_func(FileAndHash, (sk), (cmp))
#endif

typedef struct {
	ASN1_INTEGER *version;
	ASN1_INTEGER *manifestNumber;
	ASN1_GENERALIZEDTIME *thisUpdate;
	ASN1_GENERALIZEDTIME *nextUpdate;
	ASN1_OBJECT *fileHashAlg;
	STACK_OF(FileAndHash) *fileList;
} Manifest;

DECLARE_ASN1_FUNCTIONS(Manifest);


/*
 * Route Origin Authorization (ROA)
 * reference: RFC 9582
 */

extern ASN1_ITEM_EXP ROAIPAddress_it;
extern ASN1_ITEM_EXP ROAIPAddressFamily_it;
extern ASN1_ITEM_EXP RouteOriginAttestation_it;

typedef struct {
	ASN1_BIT_STRING *address;
	ASN1_INTEGER *maxLength;
} ROAIPAddress;

DECLARE_ASN1_FUNCTIONS(ROAIPAddress);
DECLARE_STACK_OF(ROAIPAddress);

#ifndef DEFINE_STACK_OF
#define sk_ROAIPAddress_num(st) SKM_sk_num(ROAIPAddress, (st))
#define sk_ROAIPAddress_push(st, i) SKM_sk_push(ROAIPAddress, (st), (i))
#define sk_ROAIPAddress_value(st, i) SKM_sk_value(ROAIPAddress, (st), (i))
#endif

typedef struct {
	ASN1_OCTET_STRING *addressFamily;
	STACK_OF(ROAIPAddress) *addresses;
} ROAIPAddressFamily;

DECLARE_ASN1_FUNCTIONS(ROAIPAddressFamily);
DECLARE_STACK_OF(ROAIPAddressFamily);

#ifndef DEFINE_STACK_OF
#define sk_ROAIPAddressFamily_num(st) SKM_sk_num(ROAIPAddressFamily, (st))
#define sk_ROAIPAddressFamily_push(st, i) \
    SKM_sk_push(ROAIPAddressFamily, (st), (i))
#define sk_ROAIPAddressFamily_value(st, i) \
    SKM_sk_value(ROAIPAddressFamily, (st), (i))
#endif

typedef struct {
	ASN1_INTEGER *version;
	ASN1_INTEGER *asid;
	STACK_OF(ROAIPAddressFamily) *ipAddrBlocks;
} RouteOriginAttestation;

DECLARE_ASN1_FUNCTIONS(RouteOriginAttestation);


/*
 * RPKI Signed Checklist (RSC)
 * reference: RFC 9323
 */

extern ASN1_ITEM_EXP ConstrainedASIdentifiers_it;
extern ASN1_ITEM_EXP ConstrainedIPAddressFamily_it;
extern ASN1_ITEM_EXP ConstrainedIPAddrBlocks_it;
extern ASN1_ITEM_EXP FileNameAndHash_it;
extern ASN1_ITEM_EXP ResourceBlock_it;
extern ASN1_ITEM_EXP RpkiSignedChecklist_it;

typedef struct {
	ASIdOrRanges *asnum;
} ConstrainedASIdentifiers;

typedef struct {
	ASN1_OCTET_STRING *addressFamily;
	STACK_OF(IPAddressOrRange) *addressesOrRanges;
} ConstrainedIPAddressFamily;

typedef STACK_OF(ConstrainedIPAddressFamily) ConstrainedIPAddrBlocks;
DECLARE_STACK_OF(ConstrainedIPAddressFamily);

typedef struct {
	ConstrainedASIdentifiers *asID;
	ConstrainedIPAddrBlocks *ipAddrBlocks;
} ResourceBlock;

typedef struct {
	ASN1_IA5STRING *fileName;
	ASN1_OCTET_STRING *hash;
} FileNameAndHash;

DECLARE_STACK_OF(FileNameAndHash);

#ifndef DEFINE_STACK_OF
#define sk_ConstrainedIPAddressFamily_num(sk) \
    SKM_sk_num(ConstrainedIPAddressFamily, (sk))
#define sk_ConstrainedIPAddressFamily_value(sk, i) \
    SKM_sk_value(ConstrainedIPAddressFamily, (sk), (i))

#define sk_FileNameAndHash_num(sk)	SKM_sk_num(FileNameAndHash, (sk))
#define sk_FileNameAndHash_value(sk, i)	SKM_sk_value(FileNameAndHash, (sk), (i))
#endif

typedef struct {
	ASN1_INTEGER *version;
	ResourceBlock *resources;
	X509_ALGOR *digestAlgorithm;
	STACK_OF(FileNameAndHash) *checkList;
} RpkiSignedChecklist;

DECLARE_ASN1_FUNCTIONS(RpkiSignedChecklist);


/*
 * Signed Prefix List (SPL)
 * reference: draft-ietf-sidrops-rpki-prefixlist
 */

extern ASN1_ITEM_EXP AddressFamilyPrefixes_it;
extern ASN1_ITEM_EXP SignedPrefixList_it;

DECLARE_STACK_OF(ASN1_BIT_STRING);

typedef struct {
	ASN1_OCTET_STRING *addressFamily;
	STACK_OF(ASN1_BIT_STRING) *addressPrefixes;
} AddressFamilyPrefixes;

DECLARE_STACK_OF(AddressFamilyPrefixes);

#ifndef DEFINE_STACK_OF
#define sk_ASN1_BIT_STRING_num(st)	SKM_sk_num(ASN1_BIT_STRING, (st))
#define sk_ASN1_BIT_STRING_value(st, i)	SKM_sk_value(ASN1_BIT_STRING, (st), (i))

#define sk_AddressFamilyPrefixes_num(st)	\
    SKM_sk_num(AddressFamilyPrefixes, (st))
#define sk_AddressFamilyPrefixes_value(st, i)	\
    SKM_sk_value(AddressFamilyPrefixes, (st), (i))
#endif

typedef struct {
	ASN1_INTEGER *version;
	ASN1_INTEGER *asid;
	STACK_OF(AddressFamilyPrefixes) *prefixBlocks;
} SignedPrefixList;

DECLARE_ASN1_FUNCTIONS(SignedPrefixList);


/*
 * Trust Anchor Key (TAK)
 * reference: RFC 9691
 */

extern ASN1_ITEM_EXP TAKey_it;
extern ASN1_ITEM_EXP TAK_it;

DECLARE_STACK_OF(ASN1_IA5STRING);

#ifndef DEFINE_STACK_OF
#define sk_ASN1_IA5STRING_num(st) SKM_sk_num(ASN1_IA5STRING, (st))
#define sk_ASN1_IA5STRING_value(st, i) SKM_sk_value(ASN1_IA5STRING, (st), (i))
#endif

typedef struct {
	STACK_OF(ASN1_UTF8STRING) *comments;
	STACK_OF(ASN1_IA5STRING) *certificateURIs;
	X509_PUBKEY *subjectPublicKeyInfo;
} TAKey;

typedef struct {
	ASN1_INTEGER *version;
	TAKey *current;
	TAKey *predecessor;
	TAKey *successor;
} TAK;

DECLARE_ASN1_FUNCTIONS(TAK);


#endif /* ! RPKI_ASN1_H */
