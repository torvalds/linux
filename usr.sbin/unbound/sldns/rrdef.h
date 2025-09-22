/*
 * rrdef.h
 *
 * RR definitions
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2005-2006
 *
 * See the file LICENSE for the license
 */

/**
 * \file
 *
 * Defines resource record types and constants.
 */

#ifndef LDNS_RRDEF_H
#define LDNS_RRDEF_H

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a dname label */
#define LDNS_MAX_LABELLEN     63
/** Maximum length of a complete dname */
#define LDNS_MAX_DOMAINLEN    255
/** Maximum number of pointers in 1 dname */
#define LDNS_MAX_POINTERS	65535
/** The bytes TTL, CLASS and length use up in an rr */
#define LDNS_RR_OVERHEAD	10

#define LDNS_DNSSEC_KEYPROTO    3
#define LDNS_KEY_ZONE_KEY   0x0100 /* set for ZSK&KSK, rfc 4034 */
#define LDNS_KEY_SEP_KEY    0x0001 /* set for KSK, rfc 4034 */
#define LDNS_KEY_REVOKE_KEY 0x0080 /* used to revoke KSK, rfc 5011 */

/* The first fields are contiguous and can be referenced instantly */
#define LDNS_RDATA_FIELD_DESCRIPTORS_COMMON 259

/** lookuptable for rr classes  */
extern struct sldns_struct_lookup_table* sldns_rr_classes;

/**
 *  The different RR classes.
 */
enum sldns_enum_rr_class
{
	/** the Internet */
	LDNS_RR_CLASS_IN 	= 1,
	/** Chaos class */
	LDNS_RR_CLASS_CH	= 3,
	/** Hesiod (Dyer 87) */
	LDNS_RR_CLASS_HS	= 4,
	/** None class, dynamic update */
	LDNS_RR_CLASS_NONE      = 254,
	/** Any class */
	LDNS_RR_CLASS_ANY	= 255,

	LDNS_RR_CLASS_FIRST     = 0,
	LDNS_RR_CLASS_LAST      = 65535,
	LDNS_RR_CLASS_COUNT     = LDNS_RR_CLASS_LAST - LDNS_RR_CLASS_FIRST + 1
};
typedef enum sldns_enum_rr_class sldns_rr_class;

/**
 *  Used to specify whether compression is allowed.
 */
enum sldns_enum_rr_compress
{
	/** compression is allowed */
	LDNS_RR_COMPRESS,
	LDNS_RR_NO_COMPRESS
};
typedef enum sldns_enum_rr_compress sldns_rr_compress;

/**
 * The different RR types.
 */
enum sldns_enum_rr_type
{
	/**  a host address */
	LDNS_RR_TYPE_A = 1,
	/**  an authoritative name server */
	LDNS_RR_TYPE_NS = 2,
	/**  a mail destination (Obsolete - use MX) */
	LDNS_RR_TYPE_MD = 3,
	/**  a mail forwarder (Obsolete - use MX) */
	LDNS_RR_TYPE_MF = 4,
	/**  the canonical name for an alias */
	LDNS_RR_TYPE_CNAME = 5,
	/**  marks the start of a zone of authority */
	LDNS_RR_TYPE_SOA = 6,
	/**  a mailbox domain name (EXPERIMENTAL) */
	LDNS_RR_TYPE_MB = 7,
	/**  a mail group member (EXPERIMENTAL) */
	LDNS_RR_TYPE_MG = 8,
	/**  a mail rename domain name (EXPERIMENTAL) */
	LDNS_RR_TYPE_MR = 9,
	/**  a null RR (EXPERIMENTAL) */
	LDNS_RR_TYPE_NULL = 10,
	/**  a well known service description */
	LDNS_RR_TYPE_WKS = 11,
	/**  a domain name pointer */
	LDNS_RR_TYPE_PTR = 12,
	/**  host information */
	LDNS_RR_TYPE_HINFO = 13,
	/**  mailbox or mail list information */
	LDNS_RR_TYPE_MINFO = 14,
	/**  mail exchange */
	LDNS_RR_TYPE_MX = 15,
	/**  text strings */
	LDNS_RR_TYPE_TXT = 16,
	/**  RFC1183 */
	LDNS_RR_TYPE_RP = 17,
	/**  RFC1183 */
	LDNS_RR_TYPE_AFSDB = 18,
	/**  RFC1183 */
	LDNS_RR_TYPE_X25 = 19,
	/**  RFC1183 */
	LDNS_RR_TYPE_ISDN = 20,
	/**  RFC1183 */
	LDNS_RR_TYPE_RT = 21,
	/**  RFC1706 */
	LDNS_RR_TYPE_NSAP = 22,
	/**  RFC1348 */
	LDNS_RR_TYPE_NSAP_PTR = 23,
	/**  2535typecode */
	LDNS_RR_TYPE_SIG = 24,
	/**  2535typecode */
	LDNS_RR_TYPE_KEY = 25,
	/**  RFC2163 */
	LDNS_RR_TYPE_PX = 26,
	/**  RFC1712 */
	LDNS_RR_TYPE_GPOS = 27,
	/**  ipv6 address */
	LDNS_RR_TYPE_AAAA = 28,
	/**  LOC record  RFC1876 */
	LDNS_RR_TYPE_LOC = 29,
	/**  2535typecode */
	LDNS_RR_TYPE_NXT = 30,
	/**  draft-ietf-nimrod-dns-01.txt */
	LDNS_RR_TYPE_EID = 31,
	/**  draft-ietf-nimrod-dns-01.txt */
	LDNS_RR_TYPE_NIMLOC = 32,
	/**  SRV record RFC2782 */
	LDNS_RR_TYPE_SRV = 33,
	/**  http://www.jhsoft.com/rfc/af-saa-0069.000.rtf */
	LDNS_RR_TYPE_ATMA = 34,
	/**  RFC2915 */
	LDNS_RR_TYPE_NAPTR = 35,
	/**  RFC2230 */
	LDNS_RR_TYPE_KX = 36,
	/**  RFC2538 */
	LDNS_RR_TYPE_CERT = 37,
	/**  RFC2874 */
	LDNS_RR_TYPE_A6 = 38,
	/**  RFC2672 */
	LDNS_RR_TYPE_DNAME = 39,
	/**  dnsind-kitchen-sink-02.txt */
	LDNS_RR_TYPE_SINK = 40,
	/**  Pseudo OPT record... */
	LDNS_RR_TYPE_OPT = 41,
	/**  RFC3123 */
	LDNS_RR_TYPE_APL = 42,
	/**  RFC4034, RFC3658 */
	LDNS_RR_TYPE_DS = 43,
	/**  SSH Key Fingerprint */
	LDNS_RR_TYPE_SSHFP = 44, /* RFC 4255 */
	/**  IPsec Key */
	LDNS_RR_TYPE_IPSECKEY = 45, /* RFC 4025 */
	/**  DNSSEC */
	LDNS_RR_TYPE_RRSIG = 46, /* RFC 4034 */
	LDNS_RR_TYPE_NSEC = 47, /* RFC 4034 */
	LDNS_RR_TYPE_DNSKEY = 48, /* RFC 4034 */

	LDNS_RR_TYPE_DHCID = 49, /* RFC 4701 */
	/* NSEC3 */
	LDNS_RR_TYPE_NSEC3 = 50, /* RFC 5155 */
	LDNS_RR_TYPE_NSEC3PARAM = 51, /* RFC 5155 */
	LDNS_RR_TYPE_NSEC3PARAMS = 51,
	LDNS_RR_TYPE_TLSA = 52, /* RFC 6698 */
	LDNS_RR_TYPE_SMIMEA = 53, /* RFC 8162 */
	LDNS_RR_TYPE_HIP = 55, /* RFC 5205 */

	/** draft-reid-dnsext-zs */
	LDNS_RR_TYPE_NINFO = 56,
	/** draft-reid-dnsext-rkey */
	LDNS_RR_TYPE_RKEY = 57,
        /** draft-ietf-dnsop-trust-history */
        LDNS_RR_TYPE_TALINK = 58,
	LDNS_RR_TYPE_CDS = 59, /** RFC 7344 */
	LDNS_RR_TYPE_CDNSKEY = 60, /** RFC 7344 */
	LDNS_RR_TYPE_OPENPGPKEY = 61, /* RFC 7929 */
	LDNS_RR_TYPE_CSYNC = 62, /* RFC 7477 */
	LDNS_RR_TYPE_ZONEMD = 63, /* draft-ietf-dnsop-dns-zone-digest-12 */
	LDNS_RR_TYPE_SVCB = 64, /* draft-ietf-dnsop-svcb-https-04 */
	LDNS_RR_TYPE_HTTPS = 65, /* draft-ietf-dnsop-svcb-https-04 */

	LDNS_RR_TYPE_SPF = 99, /* RFC 4408 */

	LDNS_RR_TYPE_UINFO = 100,
	LDNS_RR_TYPE_UID = 101,
	LDNS_RR_TYPE_GID = 102,
	LDNS_RR_TYPE_UNSPEC = 103,

	LDNS_RR_TYPE_NID = 104, /* RFC 6742 */
	LDNS_RR_TYPE_L32 = 105, /* RFC 6742 */
	LDNS_RR_TYPE_L64 = 106, /* RFC 6742 */
	LDNS_RR_TYPE_LP = 107, /* RFC 6742 */

	/** draft-jabley-dnsext-eui48-eui64-rrtypes */
	LDNS_RR_TYPE_EUI48 = 108,
	LDNS_RR_TYPE_EUI64 = 109,

	LDNS_RR_TYPE_TKEY = 249, /* RFC 2930 */
	LDNS_RR_TYPE_TSIG = 250,
	LDNS_RR_TYPE_IXFR = 251,
	LDNS_RR_TYPE_AXFR = 252,
	/**  A request for mailbox-related records (MB, MG or MR) */
	LDNS_RR_TYPE_MAILB = 253,
	/**  A request for mail agent RRs (Obsolete - see MX) */
	LDNS_RR_TYPE_MAILA = 254,
	/**  any type (wildcard) */
	LDNS_RR_TYPE_ANY = 255,
	LDNS_RR_TYPE_URI = 256, /* RFC 7553 */
	LDNS_RR_TYPE_CAA = 257, /* RFC 6844 */
	LDNS_RR_TYPE_AVC = 258,

	LDNS_RR_TYPE_RESINFO = 261, /* RFC 9606 */

	/** DNSSEC Trust Authorities */
	LDNS_RR_TYPE_TA = 32768,
	/* RFC 4431, 5074, DNSSEC Lookaside Validation */
	LDNS_RR_TYPE_DLV = 32769,

	/* type codes from nsec3 experimental phase
	LDNS_RR_TYPE_NSEC3 = 65324,
	LDNS_RR_TYPE_NSEC3PARAMS = 65325, */
	LDNS_RR_TYPE_FIRST = 0,
	LDNS_RR_TYPE_LAST  = 65535,
	LDNS_RR_TYPE_COUNT = LDNS_RR_TYPE_LAST - LDNS_RR_TYPE_FIRST + 1
};
typedef enum sldns_enum_rr_type sldns_rr_type;

/* RDATA */
#define LDNS_MAX_RDFLEN	65535

#define LDNS_RDF_SIZE_BYTE              1
#define LDNS_RDF_SIZE_WORD              2
#define LDNS_RDF_SIZE_DOUBLEWORD        4
#define LDNS_RDF_SIZE_6BYTES            6
#define LDNS_RDF_SIZE_8BYTES            8
#define LDNS_RDF_SIZE_16BYTES           16

#define LDNS_NSEC3_VARS_OPTOUT_MASK 0x01

#define LDNS_APL_IP4            1
#define LDNS_APL_IP6            2
#define LDNS_APL_MASK           0x7f
#define LDNS_APL_NEGATION       0x80

/**
 * The different types of RDATA fields.
 */
enum sldns_enum_rdf_type
{
	/** none */
	LDNS_RDF_TYPE_NONE,
	/** domain name */
	LDNS_RDF_TYPE_DNAME,
	/** 8 bits */
	LDNS_RDF_TYPE_INT8,
	/** 16 bits */
	LDNS_RDF_TYPE_INT16,
	/** 32 bits */
	LDNS_RDF_TYPE_INT32,
	/** A record */
	LDNS_RDF_TYPE_A,
	/** AAAA record */
	LDNS_RDF_TYPE_AAAA,
	/** txt string */
	LDNS_RDF_TYPE_STR,
	/** apl data */
	LDNS_RDF_TYPE_APL,
	/** b32 string */
	LDNS_RDF_TYPE_B32_EXT,
	/** b64 string */
	LDNS_RDF_TYPE_B64,
	/** hex string */
	LDNS_RDF_TYPE_HEX,
	/** nsec type codes */
	LDNS_RDF_TYPE_NSEC,
	/** a RR type */
	LDNS_RDF_TYPE_TYPE,
	/** a class */
	LDNS_RDF_TYPE_CLASS,
	/** certificate algorithm */
	LDNS_RDF_TYPE_CERT_ALG,
	/** a key algorithm */
        LDNS_RDF_TYPE_ALG,
        /** unknown types */
        LDNS_RDF_TYPE_UNKNOWN,
        /** time (32 bits) */
        LDNS_RDF_TYPE_TIME,
        /** period */
        LDNS_RDF_TYPE_PERIOD,
        /** tsig time 48 bits */
        LDNS_RDF_TYPE_TSIGTIME,
	/** Represents the Public Key Algorithm, HIT and Public Key fields
	    for the HIP RR types.  A HIP specific rdf type is used because of
	    the unusual layout in wireformat (see RFC 5205 Section 5) */
	LDNS_RDF_TYPE_HIP,
        /** variable length any type rdata where the length
            is specified by the first 2 bytes */
        LDNS_RDF_TYPE_INT16_DATA,
        /** protocol and port bitmaps */
        LDNS_RDF_TYPE_SERVICE,
        /** location data */
        LDNS_RDF_TYPE_LOC,
        /** well known services */
        LDNS_RDF_TYPE_WKS,
        /** NSAP */
        LDNS_RDF_TYPE_NSAP,
        /** ATMA */
        LDNS_RDF_TYPE_ATMA,
        /** IPSECKEY */
        LDNS_RDF_TYPE_IPSECKEY,
        /** nsec3 hash salt */
        LDNS_RDF_TYPE_NSEC3_SALT,
        /** nsec3 base32 string (with length byte on wire */
        LDNS_RDF_TYPE_NSEC3_NEXT_OWNER,

        /** 4 shorts represented as 4 * 16 bit hex numbers
         *  separated by colons. For NID and L64.
         */
        LDNS_RDF_TYPE_ILNP64,

        /** 6 * 8 bit hex numbers separated by dashes. For EUI48. */
        LDNS_RDF_TYPE_EUI48,
        /** 8 * 8 bit hex numbers separated by dashes. For EUI64. */
        LDNS_RDF_TYPE_EUI64,

	/** Character string without quotes. */
	LDNS_RDF_TYPE_UNQUOTED,

        /** A non-zero sequence of US-ASCII letters and numbers in lower case.
         *  For CAA.
         */
        LDNS_RDF_TYPE_TAG,

        /** A <character-string> encoding of the value field as specified 
         * [RFC1035], Section 5.1., encoded as remaining rdata.
         * For CAA, URI.
         */
        LDNS_RDF_TYPE_LONG_STR,

	/** TSIG extended 16bit error value */
	LDNS_RDF_TYPE_TSIGERROR,

	/* draft-ietf-dnsop-svcb-https-05:
	 * each SvcParam consisting of a SvcParamKey=SvcParamValue pair or
	 * a standalone SvcParamKey */
	LDNS_RDF_TYPE_SVCPARAM,

        /* Aliases */
        LDNS_RDF_TYPE_BITMAP = LDNS_RDF_TYPE_NSEC,
};
typedef enum sldns_enum_rdf_type sldns_rdf_type;

/**
 * Algorithms used in dns
 */
enum sldns_enum_algorithm
{
        LDNS_RSAMD5             = 1,   /* RFC 4034,4035 */
        LDNS_DH                 = 2,
        LDNS_DSA                = 3,
        LDNS_ECC                = 4,
        LDNS_RSASHA1            = 5,
        LDNS_DSA_NSEC3          = 6,
        LDNS_RSASHA1_NSEC3      = 7,
        LDNS_RSASHA256          = 8,   /* RFC 5702 */
        LDNS_RSASHA512          = 10,  /* RFC 5702 */
        LDNS_ECC_GOST           = 12,  /* RFC 5933 */
        LDNS_ECDSAP256SHA256    = 13,  /* RFC 6605 */
        LDNS_ECDSAP384SHA384    = 14,  /* RFC 6605 */
	LDNS_ED25519		= 15,  /* RFC 8080 */
	LDNS_ED448		= 16,  /* RFC 8080 */
        LDNS_INDIRECT           = 252,
        LDNS_PRIVATEDNS         = 253,
        LDNS_PRIVATEOID         = 254
};
typedef enum sldns_enum_algorithm sldns_algorithm;

/**
 * Hashing algorithms used in the DS record
 */
enum sldns_enum_hash
{
        LDNS_SHA1               = 1,  /* RFC 4034 */
        LDNS_SHA256             = 2,  /* RFC 4509 */
        LDNS_HASH_GOST          = 3,  /* RFC 5933 */
        LDNS_SHA384             = 4   /* RFC 6605 */
};
typedef enum sldns_enum_hash sldns_hash;

/**
 * algorithms used in CERT rrs
 */
enum sldns_enum_cert_algorithm
{
        LDNS_CERT_PKIX          = 1,
        LDNS_CERT_SPKI          = 2,
        LDNS_CERT_PGP           = 3,
        LDNS_CERT_IPKIX         = 4,
        LDNS_CERT_ISPKI         = 5,
        LDNS_CERT_IPGP          = 6,
        LDNS_CERT_ACPKIX        = 7,
        LDNS_CERT_IACPKIX       = 8,
        LDNS_CERT_URI           = 253,
        LDNS_CERT_OID           = 254
};
typedef enum sldns_enum_cert_algorithm sldns_cert_algorithm;

/**
 * EDNS option codes
 */
enum sldns_enum_edns_option
{
	LDNS_EDNS_LLQ = 1, /* http://files.dns-sd.org/draft-sekar-dns-llq.txt */
	LDNS_EDNS_UL = 2, /* http://files.dns-sd.org/draft-sekar-dns-ul.txt */
	LDNS_EDNS_NSID = 3, /* RFC5001 */
	/* 4 draft-cheshire-edns0-owner-option */
	LDNS_EDNS_DAU = 5, /* RFC6975 */
	LDNS_EDNS_DHU = 6, /* RFC6975 */
	LDNS_EDNS_N3U = 7, /* RFC6975 */
	LDNS_EDNS_CLIENT_SUBNET = 8, /* RFC7871 */
	LDNS_EDNS_COOKIE = 10, /* RFC7873 */
	LDNS_EDNS_KEEPALIVE = 11, /* draft-ietf-dnsop-edns-tcp-keepalive*/
	LDNS_EDNS_PADDING = 12, /* RFC7830 */
	LDNS_EDNS_EDE = 15, /* RFC8914 */
	LDNS_EDNS_CLIENT_TAG = 16, /* draft-bellis-dnsop-edns-tags-01 */
	LDNS_EDNS_REPORT_CHANNEL = 18, /* RFC9567 */
	LDNS_EDNS_UNBOUND_CACHEDB_TESTFRAME_TEST = 65534
};
typedef enum sldns_enum_edns_option sldns_edns_option;

enum sldns_enum_ede_code
{
	LDNS_EDE_NONE = -1, /* EDE undefined for internal use */
	LDNS_EDE_OTHER = 0,
	LDNS_EDE_UNSUPPORTED_DNSKEY_ALG = 1,
	LDNS_EDE_UNSUPPORTED_DS_DIGEST = 2,
	LDNS_EDE_STALE_ANSWER = 3,
	LDNS_EDE_FORGED_ANSWER = 4,
	LDNS_EDE_DNSSEC_INDETERMINATE = 5,
	LDNS_EDE_DNSSEC_BOGUS = 6,
	LDNS_EDE_SIGNATURE_EXPIRED = 7,
	LDNS_EDE_SIGNATURE_NOT_YET_VALID = 8,
	LDNS_EDE_DNSKEY_MISSING = 9,
	LDNS_EDE_RRSIGS_MISSING = 10,
	LDNS_EDE_NO_ZONE_KEY_BIT_SET = 11,
	LDNS_EDE_NSEC_MISSING = 12,
	LDNS_EDE_CACHED_ERROR = 13,
	LDNS_EDE_NOT_READY = 14,
	LDNS_EDE_BLOCKED = 15,
	LDNS_EDE_CENSORED = 16,
	LDNS_EDE_FILTERED = 17,
	LDNS_EDE_PROHIBITED = 18,
	LDNS_EDE_STALE_NXDOMAIN_ANSWER = 19,
	LDNS_EDE_NOT_AUTHORITATIVE = 20,
	LDNS_EDE_NOT_SUPPORTED = 21,
	LDNS_EDE_NO_REACHABLE_AUTHORITY = 22,
	LDNS_EDE_NETWORK_ERROR = 23,
	LDNS_EDE_INVALID_DATA = 24,
	LDNS_EDE_SIGNATURE_EXPIRED_BEFORE_VALID = 25,
	LDNS_EDE_TOO_EARLY = 26,
	LDNS_EDE_UNSUPPORTED_NSEC3_ITERATIONS = 27,
	LDNS_EDE_BADPROXYPOLICY = 28,
	LDNS_EDE_SYNTHESIZED = 29
};
typedef enum sldns_enum_ede_code sldns_ede_code;

#define LDNS_EDNS_MASK_DO_BIT 0x8000

/** TSIG and TKEY extended rcodes (16bit), 0-15 are the normal rcodes. */
#define LDNS_TSIG_ERROR_NOERROR  0
#define LDNS_TSIG_ERROR_BADSIG   16
#define LDNS_TSIG_ERROR_BADKEY   17
#define LDNS_TSIG_ERROR_BADTIME  18
#define LDNS_TSIG_ERROR_BADMODE  19
#define LDNS_TSIG_ERROR_BADNAME  20
#define LDNS_TSIG_ERROR_BADALG   21

/** DNS Cookie extended rcode */
#define LDNS_EXT_RCODE_BADCOOKIE 23

/**
 * Contains all information about resource record types.
 *
 * This structure contains, for all rr types, the rdata fields that are defined.
 */
struct sldns_struct_rr_descriptor
{
	/** Type of the RR that is described here */
	sldns_rr_type    _type;
	/** Textual name of the RR type.  */
	const char *_name;
	/** Minimum number of rdata fields in the RRs of this type.  */
	uint8_t     _minimum;
	/** Maximum number of rdata fields in the RRs of this type.  */
	uint8_t     _maximum;
	/** Wireformat specification for the rr, i.e. the types of rdata fields in their respective order. */
	const sldns_rdf_type *_wireformat;
	/** Special rdf types */
	sldns_rdf_type _variable;
	/** Specifies whether compression can be used for dnames in this RR type. */
	sldns_rr_compress _compress;
	/** The number of DNAMEs in the _wireformat string, for parsing. */
	uint8_t _dname_count;
};
typedef struct sldns_struct_rr_descriptor sldns_rr_descriptor;

/**
 * returns the resource record descriptor for the given rr type.
 *
 * \param[in] type the type value of the rr type
 *\return the sldns_rr_descriptor for this type
 */
const sldns_rr_descriptor *sldns_rr_descript(uint16_t type);

/**
 * returns the minimum number of rdata fields of the rr type this descriptor describes.
 *
 * \param[in]  descriptor for an rr type
 * \return the minimum number of rdata fields
 */
size_t sldns_rr_descriptor_minimum(const sldns_rr_descriptor *descriptor);

/**
 * returns the maximum number of rdata fields of the rr type this descriptor describes.
 *
 * \param[in]  descriptor for an rr type
 * \return the maximum number of rdata fields
 */
size_t sldns_rr_descriptor_maximum(const sldns_rr_descriptor *descriptor);

/**
 * returns the rdf type for the given rdata field number of the rr type for the given descriptor.
 *
 * \param[in] descriptor for an rr type
 * \param[in] field the field number
 * \return the rdf type for the field
 */
sldns_rdf_type sldns_rr_descriptor_field_type(const sldns_rr_descriptor *descriptor, size_t field);

/**
 * retrieves a rrtype by looking up its name.
 * \param[in] name a string with the name
 * \return the type which corresponds with the name
 */
sldns_rr_type sldns_get_rr_type_by_name(const char *name);

/**
 * retrieves a class by looking up its name.
 * \param[in] name string with the name
 * \return the cass which corresponds with the name
 */
sldns_rr_class sldns_get_rr_class_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* LDNS_RRDEF_H */
