/*	$OpenBSD: extern.h,v 1.264 2025/09/14 14:02:27 job Exp $ */
/*
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
#ifndef EXTERN_H
#define EXTERN_H

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/time.h>

#include <openssl/x509.h>
#include <openssl/x509v3.h>

#define CTASSERT(x)	extern char  _ctassert[(x) ? 1 : -1 ] \
			    __attribute__((__unused__))

#define MAX_MSG_SIZE	(50 * 1024 * 1024)

enum cert_as_type {
	CERT_AS_ID, /* single identifier */
	CERT_AS_INHERIT, /* inherit from issuer */
	CERT_AS_RANGE, /* range of identifiers */
};

/*
 * An AS identifier range.
 * The maximum AS identifier is an unsigned 32 bit integer (RFC 6793).
 */
struct cert_as_range {
	uint32_t	 min; /* minimum non-zero */
	uint32_t	 max; /* maximum */
};

/*
 * An autonomous system (AS) object.
 * AS identifiers are unsigned 32 bit integers (RFC 6793).
 */
struct cert_as {
	enum cert_as_type type; /* type of AS specification */
	union {
		uint32_t id; /* singular identifier */
		struct cert_as_range range; /* range */
	};
};

/*
 * AFI values are assigned by IANA.
 * In rpki-client, we only accept the IPV4 and IPV6 AFI values.
 */
enum afi {
	AFI_IPV4 = 1,
	AFI_IPV6 = 2
};

/*
 * An IP address as parsed from RFC 3779, section 2.2.3.8.
 * This is either in a certificate or an ROA.
 * It may either be IPv4 or IPv6.
 */
struct ip_addr {
	unsigned char	 addr[16]; /* binary address prefix */
	unsigned char	 prefixlen; /* number of valid bits in address */
};

/*
 * An IP address (IPv4 or IPv6) range starting at the minimum and making
 * its way to the maximum.
 */
struct ip_addr_range {
	struct ip_addr min; /* minimum ip */
	struct ip_addr max; /* maximum ip */
};

enum cert_ip_type {
	CERT_IP_ADDR, /* IP address range w/shared prefix */
	CERT_IP_INHERIT, /* inherited IP address */
	CERT_IP_RANGE /* range of IP addresses */
};

/*
 * A single IP address family (AFI, address or range) as defined in RFC
 * 3779, 2.2.3.2.
 * The RFC specifies multiple address or ranges per AFI; this structure
 * encodes both the AFI and a single address or range.
 */
struct cert_ip {
	enum afi		afi; /* AFI value */
	enum cert_ip_type	type; /* type of IP entry */
	unsigned char		min[16]; /* full range minimum */
	unsigned char		max[16]; /* full range maximum */
	union {
		struct ip_addr ip; /* singular address */
		struct ip_addr_range range; /* range */
	};
};

enum cert_purpose {
	CERT_PURPOSE_INVALID,
	CERT_PURPOSE_TA,
	CERT_PURPOSE_CA,
	CERT_PURPOSE_EE,
	CERT_PURPOSE_BGPSEC_ROUTER,
};

/*
 * Parsed components of a validated X509 certificate stipulated by RFC
 * 6847 and further (within) by RFC 3779.
 * All AS numbers are guaranteed to be non-overlapping and properly
 * inheriting.
 */
struct cert {
	struct cert_ip	*ips;	/* list of IP address ranges */
	size_t		 num_ips;
	struct cert_as	*ases;	/* list of AS numbers and ranges */
	size_t		 num_ases;
	int		 talid; /* cert is covered by which TAL */
	int		 certid;
	unsigned int	 repoid; /* repository of this cert file */
	char		*path; /* filename without .rrdp and .rsync prefix */
	char		*repo; /* CA repository (rsync:// uri) */
	char		*mft; /* manifest (rsync:// uri) */
	char		*notify; /* RRDP notify (https:// uri) */
	char		*crl; /* CRL location (rsync:// or NULL) */
	char		*signedobj; /* rsync access location for EE certs. */
	char		*aia; /* AIA (or NULL, for trust anchor) */
	char		*aki; /* AKI (or NULL, for trust anchor) */
	char		*ski; /* SKI */
	enum cert_purpose	 purpose; /* EE, BGPsec, CA, or TA */
	char		*pubkey; /* Subject Public Key Info */
	X509		*x509; /* the cert */
	time_t		 notbefore; /* cert's Not Before */
	time_t		 notafter; /* cert's Not After */
	time_t		 expires; /* when the signature path expires */
};

/*
 * Non-functional CA tree element.
 * Initially all CA and TA certs are added to this tree.
 * They are removed once they are the issuer of a valid mft.
 */
struct nonfunc_ca {
	RB_ENTRY(nonfunc_ca)	 entry;
	char			*location;
	char			*carepo;
	char			*mfturi;
	char			*ski;
	int			 certid;
	int			 talid;
};

/*
 * Tree of nonfunc CAs, sorted by certid.
 */
RB_HEAD(nca_tree, nonfunc_ca);
RB_PROTOTYPE(nca_tree, nonfunc_ca, entry, ncacmp);

/*
 * The TAL file conforms to RFC 7730.
 * It is the top-level structure of RPKI and defines where we can find
 * certificates for TAs (trust anchors).
 * It also includes the public key for verifying those trust anchor
 * certificates.
 */
struct tal {
	char		**uri; /* well-formed rsync URIs */
	size_t		 num_uris;
	unsigned char	*pkey; /* DER-encoded public key */
	size_t		 pkeysz; /* length of pkey */
	char		*descr; /* basename of tal file */
	int		 id; /* ID of this TAL */
};

/*
 * Resource types specified by the RPKI profiles.
 * There might be others we don't consider.
 */
enum rtype {
	RTYPE_INVALID,
	RTYPE_TAL,
	RTYPE_MFT,
	RTYPE_ROA,
	RTYPE_CER,
	RTYPE_CRL,
	RTYPE_GBR,
	RTYPE_REPO,
	RTYPE_FILE,
	RTYPE_RSC,
	RTYPE_ASPA,
	RTYPE_TAK,
	RTYPE_GEOFEED,
	RTYPE_SPL,
	RTYPE_CCR,
};

enum location {
	DIR_UNKNOWN,
	DIR_TEMP,
	DIR_VALID,
};

/*
 * Files specified in an MFT have their bodies hashed with SHA256.
 */
struct mftfile {
	char		*file; /* filename (CER/ROA/CRL, no path) */
	enum rtype	 type; /* file type as determined by extension */
	enum location	 location;	/* temporary or valid directory */
	unsigned char	 hash[SHA256_DIGEST_LENGTH]; /* sha256 of body */
};

/*
 * A manifest, RFC 9286.
 * This consists of a bunch of files found in the same directory as the
 * manifest file.
 */
struct mft {
	char		*path; /* relative path to directory of the MFT */
	struct mftfile	*files; /* file and hash */
	char		*seqnum; /* manifestNumber */
	char		*aki; /* AKI */
	char		*sia; /* SIA signedObject */
	char		*crl; /* CRL file name */
	unsigned char	 mfthash[SHA256_DIGEST_LENGTH];
	size_t		 mftsize;
	unsigned char	 crlhash[SHA256_DIGEST_LENGTH];
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 thisupdate; /* from the eContent */
	time_t		 nextupdate; /* from the eContent */
	time_t		 expires; /* when the signature path expires */
	size_t		 filesz; /* number of filenames */
	unsigned int	 repoid;
	int		 talid;
	int		 certid;
	int		 seqnum_gap; /* was there a gap compared to prev mft? */
};

/*
 * An IP address prefix for a given ROA.
 * This encodes the maximum length, AFI (v6/v4), and address.
 * FIXME: are the min/max necessary or just used in one place?
 */
struct roa_ip {
	enum afi	 afi; /* AFI value */
	struct ip_addr	 addr; /* the address prefix itself */
	unsigned char	 min[16]; /* full range minimum */
	unsigned char	 max[16]; /* full range maximum */
	unsigned char	 maxlength; /* max length or zero */
};

/*
 * An ROA, RFC 9582.
 * This consists of the concerned ASID and its IP prefixes.
 */
struct roa {
	uint32_t	 asid; /* asID of ROA (if 0, RFC 6483 sec 4) */
	struct roa_ip	*ips;	/* IP prefixes */
	size_t		 num_ips;
	int		 talid; /* ROAs are covered by which TAL */
	int		 valid; /* validated resources */
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the signature path expires */
};

struct rscfile {
	char		*filename; /* an optional filename on the checklist */
	unsigned char	 hash[SHA256_DIGEST_LENGTH]; /* the digest */
};

/*
 * A Signed Checklist (RSC)
 */
struct rsc {
	int		 talid; /* RSC covered by what TAL */
	int		 valid; /* eContent resources covered by EE's 3779? */
	struct cert_ip	*ips;	/* IP prefixes */
	size_t		 num_ips;
	struct cert_as	*ases;	/* AS resources */
	size_t		 num_ases;
	struct rscfile	*files; /* FileAndHashes in the RSC */
	size_t		 num_files;
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the signature path expires */
};

/*
 * An IP address prefix in a given SignedPrefixList.
 */
struct spl_pfx {
	enum afi	 afi;
	struct ip_addr	 prefix;
};

/*
 * An SPL, draft-ietf-sidrops-rpki-prefixlist
 * This consists of an ASID and its IP prefixes.
 */
struct spl {
	uint32_t	 asid;
	struct spl_pfx	*prefixes;
	size_t		 num_prefixes;
	int		 talid;
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the certification path expires */
	int		 valid;
};

/*
 * Datastructure representing the TAKey sequence inside TAKs.
 */
struct takey {
	char		**comments; /* Comments */
	size_t		 num_comments;
	char		**uris; /* CertificateURI */
	size_t		 num_uris;
	unsigned char	*pubkey; /* DER encoded SubjectPublicKeyInfo */
	size_t		 pubkeysz;
	char		*ski; /* hex encoded SubjectKeyIdentifier of pubkey */
};

/*
 * A Signed TAL (TAK), RFC 9691.
 */
struct tak {
	int		 talid; /* TAK covered by what TAL */
	struct takey	*current;
	struct takey	*predecessor;
	struct takey	*successor;
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the signature path expires */
};

/*
 * A single geofeed record
 */
struct geoip {
	struct cert_ip	*ip;
	char		*loc;
};

/*
 * A geofeed file
 */
struct geofeed {
	struct geoip	*geoips; /* Prefix + location entry in the CSV */
	size_t		 num_geoips;
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the signature path expires */
	int		 valid; /* all resources covered */
};

/*
 * A single Ghostbuster record
 */
struct gbr {
	char		*vcard;
	time_t		 signtime; /* CMS signing-time attribute */
	time_t		 expires; /* when the signature path expires */
	int		 talid; /* TAL the GBR is chained up to */
};

/*
 * A single ASPA record
 */
struct aspa {
	int			 valid; /* contained in issuer auth */
	int			 talid; /* TAL the ASPA is chained up to */
	uint32_t		 custasid; /* the customerASID */
	uint32_t		*providers; /* the providers */
	size_t			 num_providers;
	time_t			 signtime; /* CMS signing-time attribute */
	time_t			 expires; /* when the signature path expires */
};

/*
 * A Validated ASPA Payload (VAP) tree element.
 * To ease transformation, this struct mimics ASPA RTR PDU structure.
 */
struct vap {
	RB_ENTRY(vap)		 entry;
	uint32_t		 custasid;
	uint32_t		*providers;
	size_t			 num_providers;
	time_t			 expires;
	int			 talid;
	unsigned int		 repoid;
	int			 overflowed;
};

/*
 * Tree of VAPs sorted by afi, custasid, and provideras.
 */
RB_HEAD(vap_tree, vap);
RB_PROTOTYPE(vap_tree, vap, entry, vapcmp);

/*
 * A single VRP element (including ASID)
 */
struct vrp {
	RB_ENTRY(vrp)	entry;
	struct ip_addr	addr;
	uint32_t	asid;
	enum afi	afi;
	unsigned char	maxlength;
	time_t		expires; /* transitive expiry moment */
	int		talid; /* covered by which TAL */
	unsigned int	repoid;
};
/*
 * Tree of VRP sorted by afi, addr, maxlength and asid
 */
RB_HEAD(vrp_tree, vrp);
RB_PROTOTYPE(vrp_tree, vrp, entry, vrpcmp);

/*
 * Validated SignedPrefixList Payload
 * A single VSP element (including ASID)
 * draft-ietf-sidrops-rpki-prefixlist
 */
struct vsp {
	RB_ENTRY(vsp)	 entry;
	uint32_t	 asid;
	struct spl_pfx	*prefixes;
	size_t		 num_prefixes;
	time_t		 expires;
	int		 talid;
	unsigned int	 repoid;
};
/*
 * Tree of VSP sorted by asid
 */
RB_HEAD(vsp_tree, vsp);
RB_PROTOTYPE(vsp_tree, vsp, entry, vspcmp);

/*
 * A single BGPsec Router Key (including ASID)
 */
struct brk {
	RB_ENTRY(brk)	 entry;
	uint32_t	 asid;
	int		 talid; /* covered by which TAL */
	char		*ski; /* Subject Key Identifier */
	char		*pubkey; /* Subject Public Key Info */
	time_t		 expires; /* transitive expiry moment */
};
/*
 * Tree of BRK sorted by asid
 */
RB_HEAD(brk_tree, brk);
RB_PROTOTYPE(brk_tree, brk, entry, brkcmp);

struct ccr_mft {
	RB_ENTRY(ccr_mft) entry;
	char hash[SHA256_DIGEST_LENGTH];
	char aki[SHA_DIGEST_LENGTH];
	size_t size;
	time_t thisupdate;
	char *seqnum;
	char *sia;
};

RB_HEAD(ccr_mft_tree, ccr_mft);
RB_PROTOTYPE(ccr_mft_tree, ccr_mft, entry, ccr_mft_cmp);

RB_HEAD(ccr_vrp_tree, vrp);
RB_PROTOTYPE(ccr_vrp_tree, vrp, entry, ccr_vrp_cmp);

struct ccr_tas_ski {
	RB_ENTRY(ccr_tas_ski) entry;
	unsigned char keyid[SHA_DIGEST_LENGTH];
};

RB_HEAD(ccr_tas_tree, ccr_tas_ski);
RB_PROTOTYPE(ccr_tas_tree, ccr_tas_ski, entry, ccr_tas_ski_cmp);

struct ccr {
	struct ccr_mft_tree mfts;
	struct ccr_vrp_tree vrps;
	struct vap_tree vaps; /* only used in filemode */
	struct ccr_tas_tree tas;
	struct brk_tree brks; /* only used in filemode */
	char *mfts_hash;
	char *vrps_hash;
	char *vaps_hash;
	char *tas_hash;
	char *brks_hash;
	time_t producedat;
	time_t most_recent_update;
	unsigned char *der;
	size_t der_len;
};

struct validation_data {
	struct vrp_tree	vrps;
	struct brk_tree	brks;
	struct vap_tree	vaps;
	struct vsp_tree	vsps;
	struct nca_tree ncas;
	struct ccr ccr;
};

/*
 * A single CRL
 */
struct crl {
	RB_ENTRY(crl)	 entry;
	char		*aki;
	char		*mftpath;
	X509_CRL	*x509_crl;
	time_t		 thisupdate;	/* do not use before */
	time_t		 nextupdate;	/* do not use after */
};
/*
 * Tree of CRLs sorted by uri
 */
RB_HEAD(crl_tree, crl);

/*
 * An authentication tuple.
 * This specifies a public key and a subject key identifier used to
 * verify children nodes in the tree of entities.
 */
struct auth {
	RB_ENTRY(auth)	 entry;
	struct cert	*cert; /* owner information */
	struct auth	*issuer; /* pointer to issuer or NULL for TA cert */
	int		 any_inherits;
	int		 depth;
};
/*
 * Tree of auth sorted by ski
 */
RB_HEAD(auth_tree, auth);

struct auth	*auth_find(struct auth_tree *, int);
struct auth	*auth_insert(const char *, struct auth_tree *, struct cert *,
		    struct auth *);

enum http_result {
	HTTP_FAILED,	/* anything else */
	HTTP_OK,	/* 200 OK */
	HTTP_NOT_MOD,	/* 304 Not Modified */
};

/*
 * Message types for communication with RRDP process.
 */
enum rrdp_msg {
	RRDP_START,
	RRDP_SESSION,
	RRDP_FILE,
	RRDP_CLEAR,
	RRDP_END,
	RRDP_HTTP_REQ,
	RRDP_HTTP_INI,
	RRDP_HTTP_FIN,
	RRDP_ABORT,
};

/* Maximum number of delta files per RRDP notification file. */
#define MAX_RRDP_DELTAS		300

/*
 * RRDP session state, needed to pickup at the right spot on next run.
 */
struct rrdp_session {
	char			*last_mod;
	char			*session_id;
	long long		 serial;
	char			*deltas[MAX_RRDP_DELTAS];
};

/*
 * File types used in RRDP_FILE messages.
 */
enum publish_type {
	PUB_ADD,
	PUB_UPD,
	PUB_DEL,
};

/*
 * An entity (MFT, ROA, certificate, etc.) that needs to be downloaded
 * and parsed.
 */
struct entity {
	TAILQ_ENTRY(entity) entries;
	char		*path;		/* path relative to repository */
	char		*file;		/* filename or valid repo path */
	char		*mftaki;	/* expected AKI (taken from Manifest) */
	unsigned char	*data;		/* optional data blob */
	size_t		 datasz;	/* length of optional data blob */
	unsigned int	 repoid;	/* repository identifier */
	int		 talid;		/* tal identifier */
	int		 certid;
	enum rtype	 type;		/* type of entity (not RTYPE_EOF) */
	enum location	 location;	/* which directory the file lives in */
};
TAILQ_HEAD(entityq, entity);

enum stype {
	STYPE_OK,
	STYPE_FAIL,
	STYPE_INVALID,
	STYPE_BGPSEC,
	STYPE_TOTAL,
	STYPE_UNIQUE,
	STYPE_DEC_UNIQUE,
	STYPE_PROVIDERS,
	STYPE_OVERFLOW,
	STYPE_SEQNUM_GAP,
	STYPE_FUNC,
	STYPE_NONFUNC,
};

struct repo;
struct filepath;
RB_HEAD(filepath_tree, filepath);


/*
 * Statistics collected during run-time.
 */
struct repotalstats {
	uint32_t	 certs; /* certificates */
	uint32_t	 certs_fail; /* invalid certificate */
	uint32_t	 certs_nonfunc; /* non-functional CA certificates */
	uint32_t	 mfts; /* total number of manifests */
	uint32_t	 mfts_gap; /* manifests with sequence gaps */
	uint32_t	 mfts_fail; /* failing syntactic parse */
	uint32_t	 roas; /* route origin authorizations */
	uint32_t	 roas_fail; /* failing syntactic parse */
	uint32_t	 roas_invalid; /* invalid resources */
	uint32_t	 aspas; /* ASPA objects */
	uint32_t	 aspas_fail; /* ASPA objects failing syntactic parse */
	uint32_t	 aspas_invalid; /* ASPAs with invalid customerASID */
	uint32_t	 brks; /* number of BGPsec Router Key (BRK) certs */
	uint32_t	 crls; /* revocation lists */
	uint32_t	 gbrs; /* ghostbuster records */
	uint32_t	 taks; /* signed TAL objects */
	uint32_t	 vaps; /* total number of Validated ASPA Payloads */
	uint32_t	 vaps_uniqs; /* total number of unique VAPs */
	uint32_t	 vaps_pas; /* total number of providers */
	uint32_t	 vaps_overflowed; /* VAPs with too many providers */
	uint32_t	 vrps; /* total number of Validated ROA Payloads */
	uint32_t	 vrps_uniqs; /* number of unique vrps */
	uint32_t	 spls; /* signed prefix list */
	uint32_t	 spls_fail; /* failing syntactic parse */
	uint32_t	 spls_invalid; /* invalid spls */
	uint32_t	 vsps; /* total number of Validated SPL Payloads */
	uint32_t	 vsps_uniqs; /* number of unique vsps */
};

struct repostats {
	uint32_t	 del_files;	/* number of files removed in cleanup */
	uint32_t	 extra_files;	/* number of superfluous files */
	uint32_t	 del_extra_files;/* number of removed extra files */
	uint32_t	 del_dirs;	/* number of dirs removed in cleanup */
	uint32_t	 new_files;	/* moved from DIR_TEMP to DIR_VALID */
	struct timespec	 sync_time;	/* time to sync repo */
};

struct stats {
	uint32_t	 tals; /* total number of locators */
	uint32_t	 repos; /* repositories */
	uint32_t	 rsync_repos; /* synced rsync repositories */
	uint32_t	 rsync_fails; /* failed rsync repositories */
	uint32_t	 http_repos; /* synced http repositories */
	uint32_t	 http_fails; /* failed http repositories */
	uint32_t	 rrdp_repos; /* synced rrdp repositories */
	uint32_t	 rrdp_fails; /* failed rrdp repositories */
	uint32_t	 skiplistentries; /* number of skiplist entries */

	struct repotalstats	repo_tal_stats;
	struct repostats	repo_stats;
	struct timespec		elapsed_time;
	struct timespec		user_time;
	struct timespec		system_time;
};

struct ibuf;
struct msgbuf;
struct ibufqueue;

/* global variables */
extern ASN1_OBJECT *certpol_oid;
extern ASN1_OBJECT *caissuers_oid;
extern ASN1_OBJECT *carepo_oid;
extern ASN1_OBJECT *manifest_oid;
extern ASN1_OBJECT *signedobj_oid;
extern ASN1_OBJECT *notify_oid;
extern ASN1_OBJECT *roa_oid;
extern ASN1_OBJECT *mft_oid;
extern ASN1_OBJECT *gbr_oid;
extern ASN1_OBJECT *bgpsec_oid;
extern ASN1_OBJECT *cnt_type_oid;
extern ASN1_OBJECT *msg_dgst_oid;
extern ASN1_OBJECT *sign_time_oid;
extern ASN1_OBJECT *rsc_oid;
extern ASN1_OBJECT *aspa_oid;
extern ASN1_OBJECT *tak_oid;
extern ASN1_OBJECT *geofeed_oid;
extern ASN1_OBJECT *spl_oid;
extern ASN1_OBJECT *ccr_oid;

extern int verbose;
extern int noop;
extern int filemode;
extern int excludeaspa;
extern int experimental;
extern int excludeas0;
extern const char *tals[];
extern const char *taldescs[];
extern unsigned int talrepocnt[];
extern struct repotalstats talstats[];
extern int talsz;

/* Routines for RPKI entities. */

void		 tal_buffer(struct ibuf *, const struct tal *);
void		 tal_free(struct tal *);
struct tal	*tal_parse(const char *, char *, size_t);
struct tal	*tal_read(struct ibuf *);

void		 cert_buffer(struct ibuf *, const struct cert *);
void		 cert_free(struct cert *);
void		 auth_tree_free(struct auth_tree *);
struct cert	*cert_parse_ee_cert(const char *, int, X509 *);
struct cert	*cert_parse(const char *, const unsigned char *, size_t);
struct cert	*ta_parse(const char *, struct cert *, const unsigned char *,
		    size_t);
struct cert	*cert_read(struct ibuf *);
void		 cert_insert_brks(struct brk_tree *, struct cert *);
void		 cert_insert_nca(struct nca_tree *, const struct cert *,
		    struct repo *);
void		 cert_remove_nca(struct nca_tree *, int, struct repo *);

enum rtype	 rtype_from_file_extension(const char *);
void		 mft_buffer(struct ibuf *, const struct mft *);
void		 mft_free(struct mft *);
struct mft	*mft_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);
struct mft	*mft_read(struct ibuf *);
int		 mft_compare_issued(const struct mft *, const struct mft *);
int		 mft_compare_seqnum(const struct mft *, const struct mft *);
int		 mft_seqnum_gap_present(const struct mft *, const struct mft *,
		    BN_CTX *);

void		 roa_buffer(struct ibuf *, const struct roa *);
void		 roa_free(struct roa *);
struct roa	*roa_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);
struct roa	*roa_read(struct ibuf *);
void		 roa_insert_vrps(struct vrp_tree *, struct roa *,
		    struct repo *);

void		 spl_buffer(struct ibuf *, const struct spl *);
void		 spl_free(struct spl *);
struct spl	*spl_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);
struct spl	*spl_read(struct ibuf *);
void		 spl_insert_vsps(struct vsp_tree *, struct spl *,
		    struct repo *);

void		 gbr_free(struct gbr *);
struct gbr	*gbr_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);

void		 geofeed_free(struct geofeed *);
struct geofeed	*geofeed_parse(struct cert **, const char *, int, char *,
		    size_t);

void		 rsc_free(struct rsc *);
struct rsc	*rsc_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);

void		 takey_free(struct takey *);
void		 tak_free(struct tak *);
struct tak	*tak_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);

void		 aspa_buffer(struct ibuf *, const struct aspa *);
void		 aspa_free(struct aspa *);
void		 aspa_insert_vaps(char *, struct vap_tree *, struct aspa *,
		    struct repo *);
struct aspa	*aspa_parse(struct cert **, const char *, int,
		    const unsigned char *, size_t);
struct aspa	*aspa_read(struct ibuf *);

/* crl.c */
struct crl	*crl_parse(const char *, const unsigned char *, size_t);
struct crl	*crl_get(struct crl_tree *, const struct auth *);
int		 crl_insert(struct crl_tree *, struct crl *);
void		 crl_free(struct crl *);
void		 crl_tree_free(struct crl_tree *);

/* Validation of our objects. */

int		 valid_cert(const char *, struct auth *, const struct cert *);
int		 valid_roa(const char *, struct cert *, struct roa *);
int		 valid_filehash(int, const char *, size_t);
int		 valid_hash(unsigned char *, size_t, const char *, size_t);
int		 valid_filename(const char *, size_t);
int		 valid_uri(const char *, size_t, const char *);
int		 valid_origin(const char *, const char *);
int		 valid_x509(char *, X509_STORE_CTX *, X509 *, struct auth *,
		    struct crl *, const char **);
int		 valid_rsc(const char *, struct cert *, struct rsc *);
int		 valid_econtent_version(const char *, const ASN1_INTEGER *,
		    uint64_t);
int		 valid_aspa(const char *, struct cert *, struct aspa *);
int		 valid_geofeed(const char *, struct cert *, struct geofeed *);
int		 valid_uuid(const char *);
int		 valid_spl(const char *, struct cert *, struct spl *);

/* Working with CMS. */
unsigned char	*cms_parse_validate(struct cert **, const char *, int,
		    const unsigned char *, size_t, const ASN1_OBJECT *,
		    size_t *, time_t *);
int		 cms_parse_validate_detached(struct cert **, const char *, int,
		    const unsigned char *, size_t, const ASN1_OBJECT *, BIO *,
		    time_t *);

/* Work with RFC 3779 IP addresses, prefixes, ranges. */

int		 ip_addr_afi_parse(const char *, const ASN1_OCTET_STRING *,
		    enum afi *);
int		 ip_addr_parse(const ASN1_BIT_STRING *,
		    enum afi, const char *, struct ip_addr *);
void		 ip_addr_print(const struct ip_addr *, enum afi, char *,
		    size_t);
int		 ip_addr_check_overlap(const struct cert_ip *,
		    const char *, const struct cert_ip *, size_t, int);
int		 ip_addr_check_covered(enum afi, const unsigned char *,
		    const unsigned char *, const struct cert_ip *, size_t);
int		 ip_cert_compose_ranges(struct cert_ip *);
void		 ip_roa_compose_ranges(struct roa_ip *);
void		 ip_warn(const char *, const char *, const struct cert_ip *);

int		 sbgp_addr(const char *, struct cert_ip *, size_t *,
		    enum afi, const ASN1_BIT_STRING *);
int		 sbgp_addr_range(const char *, struct cert_ip *, size_t *,
		    enum afi, const IPAddressRange *);

int		 sbgp_parse_ipaddrblk(const char *, const IPAddrBlocks *,
		    struct cert_ip **, size_t *);

/* Work with RFC 3779 AS numbers, ranges. */

int		 as_id_parse(const ASN1_INTEGER *, uint32_t *);
int		 as_check_overlap(const struct cert_as *, const char *,
		    const struct cert_as *, size_t, int);
int		 as_check_covered(uint32_t, uint32_t,
		    const struct cert_as *, size_t);
void		 as_warn(const char *, const char *, const struct cert_as *);

int		 sbgp_as_id(const char *, struct cert_as *, size_t *,
		    const ASN1_INTEGER *);
int		 sbgp_as_range(const char *, struct cert_as *, size_t *,
		    const ASRange *);

int		 sbgp_parse_assysnum(const char *, const ASIdentifiers *,
		    struct cert_as **, size_t *);

/* Constraints-specific */
void		 constraints_load(void);
void		 constraints_unload(void);
void		 constraints_parse(void);
int		 constraints_validate(const char *, const struct cert *);

/* Parser-specific */
void		 entity_free(struct entity *);
void		 entity_read_req(struct ibuf *, struct entity *);
void		 entityq_flush(struct entityq *, struct repo *);
void		 proc_parser(int, int) __attribute__((noreturn));
void		 proc_filemode(int) __attribute__((noreturn));

/* Rsync-specific. */

char		*rsync_base_uri(const char *);
void		 proc_rsync(char *, char *, int) __attribute__((noreturn));

/* HTTP and RRDP processes. */

void		 proc_http(char *, int) __attribute__((noreturn));
void		 proc_rrdp(int) __attribute__((noreturn));

/* Repository handling */
int		 filepath_add(struct filepath_tree *, char *, int, time_t, int);
int		 filepath_valid(struct filepath_tree *, char *, int);
void		 rrdp_clear(unsigned int);
void		 rrdp_session_save(unsigned int, struct rrdp_session *);
void		 rrdp_session_free(struct rrdp_session *);
void		 rrdp_session_buffer(struct ibuf *,
		    const struct rrdp_session *);
struct rrdp_session	*rrdp_session_read(struct ibuf *);
int		 rrdp_handle_file(unsigned int, enum publish_type, char *,
		    char *, size_t, char *, size_t);
char		*repo_basedir(const struct repo *, int);
unsigned int	 repo_id(const struct repo *);
const char	*repo_uri(const struct repo *);
void		 repo_fetch_uris(const struct repo *, const char **,
		    const char **);
int		 repo_synced(const struct repo *);
const char	*repo_proto(const struct repo *);
int		 repo_talid(const struct repo *);
struct repo	*ta_lookup(int, struct tal *);
struct repo	*repo_lookup(int, const char *, const char *);
struct repo	*repo_byid(unsigned int);
int		 repo_queued(struct repo *, struct entity *);
void		 repo_printinfo(size_t);
void		 repo_cleanup(struct filepath_tree *, int);
int		 repo_check_timeout(int);
void		 repostats_new_files_inc(struct repo *, const char *);
void		 repo_stat_inc(struct repo *, int, enum rtype, enum stype);
void		 repo_tal_stats_collect(void (*)(const struct repo *,
		    const struct repotalstats *, void *), int, void *);
void		 repo_stats_collect(void (*)(const struct repo *,
		    const struct repostats *, void *), void *);
void		 repo_free(void);

void		 rsync_finish(unsigned int, int);
void		 http_finish(unsigned int, enum http_result, const char *);
void		 rrdp_finish(unsigned int, int);

void		 rsync_fetch(unsigned int, const char *, const char *,
		    const char *);
void		 rsync_abort(unsigned int);
void		 http_fetch(unsigned int, const char *, const char *, int);
void		 rrdp_fetch(unsigned int, const char *, const char *,
		    struct rrdp_session *);
void		 rrdp_abort(unsigned int);
void		 rrdp_http_done(unsigned int, enum http_result, const char *);

/* Encoding functions for hex and base64. */

unsigned char	*load_file(const char *, size_t *);
int		 base64_decode_len(size_t, size_t *);
int		 base64_decode(const unsigned char *, size_t,
		    unsigned char **, size_t *);
int		 base64_encode_len(size_t, size_t *);
int		 base64_encode(const unsigned char *, size_t, char **);
char		*hex_encode(const unsigned char *, size_t);
int		 hex_decode(const char *, char *, size_t);


/* Functions for moving data between processes. */

struct ibuf	*io_new_buffer(void);
void		 io_simple_buffer(struct ibuf *, const void *, size_t);
void		 io_buf_buffer(struct ibuf *, const void *, size_t);
void		 io_str_buffer(struct ibuf *, const char *);
void		 io_opt_str_buffer(struct ibuf *, const char *);
void		 io_close_buffer(struct msgbuf *, struct ibuf *);
void		 io_close_queue(struct ibufqueue *, struct ibuf *);
void		 io_read_buf(struct ibuf *, void *, size_t);
void		 io_read_str(struct ibuf *, char **);
void		 io_read_opt_str(struct ibuf *, char **);
void		 io_read_buf_alloc(struct ibuf *, void **, size_t *);
struct ibuf	*io_parse_hdr(struct ibuf *, void *, int *);
struct ibuf	*io_buf_get(struct msgbuf *);

/* X509 helpers. */

void		 x509_init_oid(void);
char		*x509_pubkey_get_ski(X509_PUBKEY *, const char *);
int		 x509_get_time(const ASN1_TIME *, time_t *);
int		 x509_get_generalized_time(const char *, const char *,
		    const ASN1_TIME *, time_t *);
char		*x509_convert_seqnum(const char *, const char *,
		    const ASN1_INTEGER *);
int		 x509_valid_seqnum(const char *, const char *,
		    const ASN1_INTEGER *);
int		 x509_check_tbs_sigalg(const char *, const X509_ALGOR *);
int		 x509_location(const char *, const char *, GENERAL_NAME *,
		    char **);
int		 x509_inherits(X509 *);
int		 x509_any_inherits(X509 *);
int		 x509_valid_name(const char *, const char *, const X509_NAME *);
time_t		 x509_find_expires(time_t, struct auth *, struct crl_tree *);

/* printers */
char		*nid2str(int);
const char	*purpose2str(enum cert_purpose);
char		*time2str(time_t);
void		 x509_print(const X509 *);
void		 tal_print(const struct tal *);
void		 cert_print(const struct cert *);
void		 crl_print(const struct crl *);
void		 mft_print(const struct cert *, const struct mft *);
void		 roa_print(const struct cert *, const struct roa *);
void		 gbr_print(const struct cert *, const struct gbr *);
void		 rsc_print(const struct cert *, const struct rsc *);
void		 aspa_print(const struct cert *, const struct aspa *);
void		 tak_print(const struct cert *, const struct tak *);
void		 geofeed_print(const struct cert *, const struct geofeed *);
void		 spl_print(const struct cert *, const struct spl *);

/* Missing RFC 3779 API */
IPAddrBlocks *IPAddrBlocks_new(void);
void IPAddrBlocks_free(IPAddrBlocks *);

/* Output! */

extern int	 outformats;
#define FORMAT_OPENBGPD	0x01
#define FORMAT_BIRD	0x02
#define FORMAT_CSV	0x04
#define FORMAT_JSON	0x08
#define FORMAT_OMETRIC	0x10
#define FORMAT_CCR	0x20

int		 outputfiles(struct validation_data *, struct stats *);
int		 outputheader(FILE *, struct validation_data *, struct stats *);
int		 output_bgpd(FILE *, struct validation_data *, struct stats *);
int		 output_bird(FILE *, struct validation_data *, struct stats *);
int		 output_csv(FILE *, struct validation_data *, struct stats *);
int		 output_json(FILE *, struct validation_data *, struct stats *);
int		 output_ometric(FILE *, struct validation_data *,
		    struct stats *);
int		 output_ccr_der(FILE *, struct validation_data *, struct stats *);

/*
 * Canonical Cache Representation
 */
void ccr_free(struct ccr *);
void ccr_print(struct ccr *);
struct ccr *ccr_parse(const char *, const unsigned char *, size_t);
void ccr_insert_mft(struct ccr_mft_tree *, const struct mft *);
void ccr_insert_roa(struct ccr_vrp_tree *, const struct roa *);
void ccr_insert_tas(struct ccr_tas_tree *, const struct cert *);
void serialize_ccr_content(struct validation_data *);

void		 logx(const char *fmt, ...)
		    __attribute__((format(printf, 1, 2)));
time_t		 getmonotime(void);
time_t		 get_current_time(void);

int	mkpath(const char *);
int	mkpathat(int, const char *);

#define RPKI_PATH_OUT_DIR	"/var/db/rpki-client"
#define RPKI_PATH_BASE_DIR	"/var/cache/rpki-client"

#define DEFAULT_SKIPLIST_FILE	"/etc/rpki/skiplist"

/* Interval in which random reinitialization to an RRDP snapshot happens. */
#define RRDP_RANDOM_REINIT_MAX	12 /* weeks */

/* Maximum number of TAL files we'll load. */
#define	TALSZ_MAX		8
#define	CERTID_MAX		1000000

/*
 * Maximum number of elements in the sbgp-ipAddrBlock (IP) and
 * sbgp-autonomousSysNum (AS) X.509v3 extension of CA/EE certificates.
 */
#define MAX_IP_SIZE		200000
#define MAX_AS_SIZE		200000

/* Maximum acceptable URI length */
#define MAX_URI_LENGTH		2048

/* Min/Max acceptable file size */
#define MIN_FILE_SIZE		100
#define MAX_FILE_SIZE		8000000

/* Maximum number of FileNameAndHash entries per RSC checklist. */
#define MAX_CHECKLIST_ENTRIES	100000

/* Maximum number of FileAndHash entries per manifest. */
#define MAX_MANIFEST_ENTRIES	100000

/* Maximum number of Providers per ASPA object. */
#define MAX_ASPA_PROVIDERS	10000

/* Maximum depth of the RPKI tree. */
#define MAX_CERT_DEPTH		12

/* Maximum number of concurrent http and rsync requests. */
#define MAX_HTTP_REQUESTS	64
#define MAX_RSYNC_REQUESTS	16

/* How many seconds to wait for a connection to succeed. */
#define MAX_CONN_TIMEOUT	15

/* How many seconds to wait for IO from a remote server. */
#define MAX_IO_TIMEOUT		30

/* Maximum number of delegated hosting locations (repositories) for each TAL. */
#define MAX_REPO_PER_TAL	1000

#define HTTP_PROTO		"http://"
#define HTTP_PROTO_LEN		(sizeof(HTTP_PROTO) - 1)
#define HTTPS_PROTO		"https://"
#define HTTPS_PROTO_LEN		(sizeof(HTTPS_PROTO) - 1)
#define RSYNC_PROTO		"rsync://"
#define RSYNC_PROTO_LEN		(sizeof(RSYNC_PROTO) - 1)

#endif /* ! EXTERN_H */
