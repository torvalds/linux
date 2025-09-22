/*
 * Copyright (c) 2018-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#ifndef _FIDO_TYPES_H
#define _FIDO_TYPES_H

#ifdef __MINGW32__
#include <sys/types.h>
#endif

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct fido_dev;

typedef void *fido_dev_io_open_t(const char *);
typedef void  fido_dev_io_close_t(void *);
typedef int   fido_dev_io_read_t(void *, unsigned char *, size_t, int);
typedef int   fido_dev_io_write_t(void *, const unsigned char *, size_t);
typedef int   fido_dev_rx_t(struct fido_dev *, uint8_t, unsigned char *, size_t, int);
typedef int   fido_dev_tx_t(struct fido_dev *, uint8_t, const unsigned char *, size_t);

typedef struct fido_dev_io {
	fido_dev_io_open_t  *open;
	fido_dev_io_close_t *close;
	fido_dev_io_read_t  *read;
	fido_dev_io_write_t *write;
} fido_dev_io_t;

typedef struct fido_dev_transport {
	fido_dev_rx_t *rx;
	fido_dev_tx_t *tx;
} fido_dev_transport_t;

typedef enum {
	FIDO_OPT_OMIT = 0, /* use authenticator's default */
	FIDO_OPT_FALSE,    /* explicitly set option to false */
	FIDO_OPT_TRUE,     /* explicitly set option to true */
} fido_opt_t;

typedef void fido_log_handler_t(const char *);

#undef  _FIDO_SIGSET_DEFINED
#define _FIDO_SIGSET_DEFINED
#ifdef _WIN32
typedef int fido_sigset_t;
#elif defined(SIG_BLOCK)
typedef sigset_t fido_sigset_t;
#else
#undef _FIDO_SIGSET_DEFINED
#endif

#ifdef _FIDO_INTERNAL
#include "packed.h"
#include "blob.h"

/* COSE ES256 (ECDSA over P-256 with SHA-256) public key */
typedef struct es256_pk {
	unsigned char	x[32];
	unsigned char	y[32];
} es256_pk_t;

/* COSE ES256 (ECDSA over P-256 with SHA-256) (secret) key */
typedef struct es256_sk {
	unsigned char	d[32];
} es256_sk_t;

/* COSE RS256 (2048-bit RSA with PKCS1 padding and SHA-256) public key */
typedef struct rs256_pk {
	unsigned char n[256];
	unsigned char e[3];
} rs256_pk_t;

/* COSE EDDSA (ED25519) */
typedef struct eddsa_pk {
	unsigned char x[32];
} eddsa_pk_t;

PACKED_TYPE(fido_authdata_t,
struct fido_authdata {
	unsigned char rp_id_hash[32]; /* sha256 of fido_rp.id */
	uint8_t       flags;          /* user present/verified */
	uint32_t      sigcount;       /* signature counter */
	/* actually longer */
})

PACKED_TYPE(fido_attcred_raw_t,
struct fido_attcred_raw {
	unsigned char aaguid[16]; /* credential's aaguid */
	uint16_t      id_len;     /* credential id length */
	uint8_t       body[];     /* credential id + pubkey */
})

typedef struct fido_attcred {
	unsigned char aaguid[16]; /* credential's aaguid */
	fido_blob_t   id;         /* credential id */
	int           type;       /* credential's cose algorithm */
	union {                   /* credential's public key */
		es256_pk_t es256;
		rs256_pk_t rs256;
		eddsa_pk_t eddsa;
	} pubkey;
} fido_attcred_t;

typedef struct fido_attstmt {
	fido_blob_t certinfo; /* tpm attestation TPMS_ATTEST structure */
	fido_blob_t pubarea;  /* tpm attestation TPMT_PUBLIC structure */
	fido_blob_t cbor;     /* cbor-encoded attestation statement */
	fido_blob_t x5c;      /* attestation certificate */
	fido_blob_t sig;      /* attestation signature */
	int         alg;      /* attestation algorithm (cose) */
} fido_attstmt_t;

typedef struct fido_rp {
	char *id;   /* relying party id */
	char *name; /* relying party name */
} fido_rp_t;

typedef struct fido_user {
	fido_blob_t  id;           /* required */
	char        *icon;         /* optional */
	char        *name;         /* optional */
	char        *display_name; /* required */
} fido_user_t;

typedef struct fido_cred_ext {
	int    mask;      /* enabled extensions */
	int    prot;      /* protection policy */
	size_t minpinlen; /* minimum pin length */
} fido_cred_ext_t;

typedef struct fido_cred {
	fido_blob_t       cd;            /* client data */
	fido_blob_t       cdh;           /* client data hash */
	fido_rp_t         rp;            /* relying party */
	fido_user_t       user;          /* user entity */
	fido_blob_array_t excl;          /* list of credential ids to exclude */
	fido_opt_t        rk;            /* resident key */
	fido_opt_t        uv;            /* user verification */
	fido_cred_ext_t   ext;           /* extensions */
	int               type;          /* cose algorithm */
	char             *fmt;           /* credential format */
	fido_cred_ext_t   authdata_ext;  /* decoded extensions */
	fido_blob_t       authdata_cbor; /* cbor-encoded payload */
	fido_blob_t       authdata_raw;  /* cbor-decoded payload */
	fido_authdata_t   authdata;      /* decoded authdata payload */
	fido_attcred_t    attcred;       /* returned credential (key + id) */
	fido_attstmt_t    attstmt;       /* attestation statement (x509 + sig) */
	fido_blob_t       largeblob_key; /* decoded large blob key */
	fido_blob_t       blob;          /* CTAP 2.1 credBlob */
} fido_cred_t;

typedef struct fido_assert_extattr {
	int         mask;            /* decoded extensions */
	fido_blob_t hmac_secret_enc; /* hmac secret, encrypted */
	fido_blob_t blob;            /* decoded CTAP 2.1 credBlob */
} fido_assert_extattr_t;

typedef struct _fido_assert_stmt {
	fido_blob_t           id;            /* credential id */
	fido_user_t           user;          /* user attributes */
	fido_blob_t           hmac_secret;   /* hmac secret */
	fido_assert_extattr_t authdata_ext;  /* decoded extensions */
	fido_blob_t           authdata_cbor; /* raw cbor payload */
	fido_authdata_t       authdata;      /* decoded authdata payload */
	fido_blob_t           sig;           /* signature of cdh + authdata */
	fido_blob_t           largeblob_key; /* decoded large blob key */
} fido_assert_stmt;

typedef struct fido_assert_ext {
	int         mask;                /* enabled extensions */
	fido_blob_t hmac_salt;           /* optional hmac-secret salt */
} fido_assert_ext_t;

typedef struct fido_assert {
	char              *rp_id;        /* relying party id */
	fido_blob_t        cd;           /* client data */
	fido_blob_t        cdh;          /* client data hash */
	fido_blob_array_t  allow_list;   /* list of allowed credentials */
	fido_opt_t         up;           /* user presence */
	fido_opt_t         uv;           /* user verification */
	fido_assert_ext_t  ext;          /* enabled extensions */
	fido_assert_stmt  *stmt;         /* array of expected assertions */
	size_t             stmt_cnt;     /* number of allocated assertions */
	size_t             stmt_len;     /* number of received assertions */
} fido_assert_t;

typedef struct fido_opt_array {
	char **name;
	bool *value;
	size_t len;
} fido_opt_array_t;

typedef struct fido_str_array {
	char **ptr;
	size_t len;
} fido_str_array_t;

typedef struct fido_byte_array {
	uint8_t *ptr;
	size_t len;
} fido_byte_array_t;

typedef struct fido_algo {
	char *type;
	int cose;
} fido_algo_t;

typedef struct fido_algo_array {
	fido_algo_t *ptr;
	size_t len;
} fido_algo_array_t;

typedef struct fido_cbor_info {
	fido_str_array_t  versions;       /* supported versions: fido2|u2f */
	fido_str_array_t  extensions;     /* list of supported extensions */
	fido_str_array_t  transports;     /* list of supported transports */
	unsigned char     aaguid[16];     /* aaguid */
	fido_opt_array_t  options;        /* list of supported options */
	uint64_t          maxmsgsiz;      /* maximum message size */
	fido_byte_array_t protocols;      /* supported pin protocols */
	fido_algo_array_t algorithms;     /* list of supported algorithms */
	uint64_t          maxcredcntlst;  /* max credentials in list */
	uint64_t          maxcredidlen;   /* max credential ID length */
	uint64_t          fwversion;      /* firmware version */
	uint64_t          maxcredbloblen; /* max credBlob length */
	uint64_t          maxlargeblob;   /* max largeBlob array length */
} fido_cbor_info_t;

typedef struct fido_dev_info {
	char                 *path;         /* device path */
	int16_t               vendor_id;    /* 2-byte vendor id */
	int16_t               product_id;   /* 2-byte product id */
	char                 *manufacturer; /* manufacturer string */
	char                 *product;      /* product string */
	fido_dev_io_t         io;           /* i/o functions */
	fido_dev_transport_t  transport;    /* transport functions */
} fido_dev_info_t;

PACKED_TYPE(fido_ctap_info_t,
/* defined in section 8.1.9.1.3 (CTAPHID_INIT) of the fido2 ctap spec */
struct fido_ctap_info {
	uint64_t nonce;    /* echoed nonce */
	uint32_t cid;      /* channel id */
	uint8_t  protocol; /* ctaphid protocol id */
	uint8_t  major;    /* major version number */
	uint8_t  minor;    /* minor version number */
	uint8_t  build;    /* build version number */
	uint8_t  flags;    /* capabilities flags; see FIDO_CAP_* */
})

typedef struct fido_dev {
	uint64_t              nonce;      /* issued nonce */
	fido_ctap_info_t      attr;       /* device attributes */
	uint32_t              cid;        /* assigned channel id */
	char                 *path;       /* device path */
	void                 *io_handle;  /* abstract i/o handle */
	fido_dev_io_t         io;         /* i/o functions */
	bool                  io_own;     /* device has own io/transport */
	size_t                rx_len;     /* length of HID input reports */
	size_t                tx_len;     /* length of HID output reports */
	int                   flags;      /* internal flags; see FIDO_DEV_* */
	fido_dev_transport_t  transport;  /* transport functions */
	uint64_t	      maxmsgsize; /* max message size */
	int		      timeout_ms; /* read timeout in ms */
} fido_dev_t;

#else
typedef struct fido_assert fido_assert_t;
typedef struct fido_cbor_info fido_cbor_info_t;
typedef struct fido_cred fido_cred_t;
typedef struct fido_dev fido_dev_t;
typedef struct fido_dev_info fido_dev_info_t;
typedef struct es256_pk es256_pk_t;
typedef struct es256_sk es256_sk_t;
typedef struct rs256_pk rs256_pk_t;
typedef struct eddsa_pk eddsa_pk_t;
#endif /* _FIDO_INTERNAL */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* !_FIDO_TYPES_H */
