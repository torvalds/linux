/*
 * Copyright (C) The Internet Society (2000).  All Rights Reserved.
 *
 * This document and translations of it may be copied and furnished to
 * others, and derivative works that comment on or otherwise explain it
 * or assist in its implementation may be prepared, copied, published
 * and distributed, in whole or in part, without restriction of any
 * kind, provided that the above copyright notice and this paragraph are
 * included on all such copies and derivative works.  However, this
 * document itself may not be modified in any way, such as by removing
 * the copyright notice or references to the Internet Society or other
 * Internet organizations, except as needed for the purpose of
 * developing Internet standards in which case the procedures for
 * copyrights defined in the Internet Standards process must be
 * followed, or as required to translate it into languages other than
 * English.
 *
 * The limited permissions granted above are perpetual and will not be
 * revoked by the Internet Society or its successors or assigns.
 *
 * This document and the information contained herein is provided on an
 * "AS IS" basis and THE INTERNET SOCIETY AND THE INTERNET ENGINEERING
 * TASK FORCE DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO ANY WARRANTY THAT THE USE OF THE INFORMATION
 * HEREIN WILL NOT INFRINGE ANY RIGHTS OR ANY IMPLIED WARRANTIES OF
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * $FreeBSD$
 */

#ifndef _GSSAPI_GSSAPI_H_
#define _GSSAPI_GSSAPI_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif

/* Compatibility with Heimdal 1.5.1 */
#ifndef GSSAPI_CPP_START
#ifdef __cplusplus
#define GSSAPI_CPP_START	extern "C" {
#define GSSAPI_CPP_END		}
#else
#define GSSAPI_CPP_START
#define GSSAPI_CPP_END
#endif
#endif

/* Compatibility with Heimdal 1.5.1 */
#ifndef BUILD_GSSAPI_LIB
#define GSSAPI_LIB_FUNCTION
#define GSSAPI_LIB_CALL
#define GSSAPI_LIB_VARIABLE
#endif

/* Compatibility with Heimdal 1.5.1 */
#ifndef GSSAPI_DEPRECATED_FUNCTION
#if defined(__GNUC__) && ((__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1 )))
#define GSSAPI_DEPRECATED_FUNCTION(X) __attribute__((deprecated))
#else
#define GSSAPI_DEPRECATED_FUNCTION(X)
#endif
#endif

#if 0
/*
 * If the platform supports the xom.h header file, it should be
 * included here.
 */
#include <xom.h>
#endif


/*
 * Now define the three implementation-dependent types.
 */
typedef struct _gss_ctx_id_t *gss_ctx_id_t;
typedef struct _gss_cred_id_t *gss_cred_id_t;
typedef struct _gss_name_t *gss_name_t;

/*
 * The following type must be defined as the smallest natural
 * unsigned integer supported by the platform that has at least
 * 32 bits of precision.
 */
typedef __uint32_t gss_uint32;


#ifdef OM_STRING
/*
 * We have included the xom.h header file.  Verify that OM_uint32
 * is defined correctly.
 */

#if sizeof(gss_uint32) != sizeof(OM_uint32)
#error Incompatible definition of OM_uint32 from xom.h
#endif

typedef OM_object_identifier gss_OID_desc, *gss_OID;

#else

/*
 * We can't use X/Open definitions, so roll our own.
 */

typedef gss_uint32 OM_uint32;
typedef __uint64_t OM_uint64;

typedef struct gss_OID_desc_struct {
  OM_uint32 length;
  void      *elements;
} gss_OID_desc, *gss_OID;

#endif

typedef struct gss_OID_set_desc_struct  {
  size_t     count;
  gss_OID    elements;
} gss_OID_set_desc, *gss_OID_set;

typedef struct gss_buffer_desc_struct {
  size_t length;
  void *value;
} gss_buffer_desc, *gss_buffer_t;

typedef struct gss_channel_bindings_struct {
  OM_uint32 initiator_addrtype;
  gss_buffer_desc initiator_address;
  OM_uint32 acceptor_addrtype;
  gss_buffer_desc acceptor_address;
  gss_buffer_desc application_data;
} *gss_channel_bindings_t;

/*
 * For now, define a QOP-type as an OM_uint32
 */
typedef OM_uint32 gss_qop_t;

typedef int gss_cred_usage_t;

/*
 * Flag bits for context-level services.
 */
#define GSS_C_DELEG_FLAG      1
#define GSS_C_MUTUAL_FLAG     2
#define GSS_C_REPLAY_FLAG     4
#define GSS_C_SEQUENCE_FLAG   8
#define GSS_C_CONF_FLAG       16
#define GSS_C_INTEG_FLAG      32
#define GSS_C_ANON_FLAG       64
#define GSS_C_PROT_READY_FLAG 128
#define GSS_C_TRANS_FLAG      256

/*
 * Credential usage options
 */
#define GSS_C_BOTH     0
#define GSS_C_INITIATE 1
#define GSS_C_ACCEPT   2

/*
 * Status code types for gss_display_status
 */
#define GSS_C_GSS_CODE  1
#define GSS_C_MECH_CODE 2

/*
 * The constant definitions for channel-bindings address families
 */
#define GSS_C_AF_UNSPEC     0
#define GSS_C_AF_LOCAL      1
#define GSS_C_AF_INET       2
#define GSS_C_AF_IMPLINK    3
#define GSS_C_AF_PUP        4
#define GSS_C_AF_CHAOS      5
#define GSS_C_AF_NS         6
#define GSS_C_AF_NBS        7
#define GSS_C_AF_ECMA       8
#define GSS_C_AF_DATAKIT    9
#define GSS_C_AF_CCITT      10
#define GSS_C_AF_SNA        11
#define GSS_C_AF_DECnet     12
#define GSS_C_AF_DLI        13
#define GSS_C_AF_LAT        14
#define GSS_C_AF_HYLINK     15
#define GSS_C_AF_APPLETALK  16
#define GSS_C_AF_BSC        17
#define GSS_C_AF_DSS        18
#define GSS_C_AF_OSI        19
#define GSS_C_AF_X25        21
#define GSS_C_AF_NULLADDR   255

/*
 * Various Null values
 */
#define GSS_C_NO_NAME ((gss_name_t) 0)
#define GSS_C_NO_BUFFER ((gss_buffer_t) 0)
#define GSS_C_NO_OID ((gss_OID) 0)
#define GSS_C_NO_OID_SET ((gss_OID_set) 0)
#define GSS_C_NO_CONTEXT ((gss_ctx_id_t) 0)
#define GSS_C_NO_CREDENTIAL ((gss_cred_id_t) 0)
#define GSS_C_NO_CHANNEL_BINDINGS ((gss_channel_bindings_t) 0)
#define GSS_C_EMPTY_BUFFER {0, NULL}

/*
 * Some alternate names for a couple of the above
 * values.  These are defined for V1 compatibility.
 */
#define GSS_C_NULL_OID GSS_C_NO_OID
#define GSS_C_NULL_OID_SET GSS_C_NO_OID_SET

/*
 * Define the default Quality of Protection for per-message
 * services.  Note that an implementation that offers multiple
 * levels of QOP may define GSS_C_QOP_DEFAULT to be either zero
 * (as done here) to mean "default protection", or to a specific
 * explicit QOP value.  However, a value of 0 should always be
 * interpreted by a GSS-API implementation as a request for the
 * default protection level.
 */
#define GSS_C_QOP_DEFAULT 0

/*
 * Expiration time of 2^32-1 seconds means infinite lifetime for a
 * credential or security context
 */
#define GSS_C_INDEFINITE 0xfffffffful

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 * "\x01\x02\x01\x01"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 * infosys(1) gssapi(2) generic(1) user_name(1)}.  The constant
 * GSS_C_NT_USER_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_USER_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 * infosys(1) gssapi(2) generic(1) machine_uid_name(2)}.
 * The constant GSS_C_NT_MACHINE_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_MACHINE_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x03"},
 * corresponding to an object-identifier value of
 * {iso(1) member-body(2) United States(840) mit(113554)
 * infosys(1) gssapi(2) generic(1) string_uid_name(3)}.
 * The constant GSS_C_NT_STRING_UID_NAME should be
 * initialized to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_STRING_UID_NAME;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x02"},
 * corresponding to an object-identifier value of
 * {iso(1) org(3) dod(6) internet(1) security(5)
 * nametypes(6) gss-host-based-services(2)).  The constant
 * GSS_C_NT_HOSTBASED_SERVICE_X should be initialized to point
 * to that gss_OID_desc.  This is a deprecated OID value, and
 * implementations wishing to support hostbased-service names
 * should instead use the GSS_C_NT_HOSTBASED_SERVICE OID,
 * defined below, to identify such names;
 * GSS_C_NT_HOSTBASED_SERVICE_X should be accepted a synonym
 * for GSS_C_NT_HOSTBASED_SERVICE when presented as an input
 * parameter, but should not be emitted by GSS-API
 * implementations
 */
extern gss_OID GSS_C_NT_HOSTBASED_SERVICE_X;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {10, (void *)"\x2a\x86\x48\x86\xf7\x12"
 *              "\x01\x02\x01\x04"}, corresponding to an
 * object-identifier value of {iso(1) member-body(2)
 * Unites States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) service_name(4)}.  The constant
 * GSS_C_NT_HOSTBASED_SERVICE should be initialized
 * to point to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_HOSTBASED_SERVICE;

/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\01\x05\x06\x03"},
 * corresponding to an object identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 3(gss-anonymous-name)}.  The constant
 * and GSS_C_NT_ANONYMOUS should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_ANONYMOUS;


/*
 * The implementation must reserve static storage for a
 * gss_OID_desc object containing the value
 * {6, (void *)"\x2b\x06\x01\x05\x06\x04"},
 * corresponding to an object-identifier value of
 * {1(iso), 3(org), 6(dod), 1(internet), 5(security),
 * 6(nametypes), 4(gss-api-exported-name)}.  The constant
 * GSS_C_NT_EXPORT_NAME should be initialized to point
 * to that gss_OID_desc.
 */
extern gss_OID GSS_C_NT_EXPORT_NAME;

/*
 *   This name form shall be represented by the Object Identifier {iso(1)
 *   member-body(2) United States(840) mit(113554) infosys(1) gssapi(2)
 *   krb5(2) krb5_name(1)}.  The recommended symbolic name for this type
 *   is "GSS_KRB5_NT_PRINCIPAL_NAME".
 */
extern gss_OID GSS_KRB5_NT_PRINCIPAL_NAME;

/*
 * This name form shall be represented by the Object Identifier {iso(1)
 * member-body(2) United States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) user_name(1)}.  The recommended symbolic name for this
 * type is "GSS_KRB5_NT_USER_NAME".
 */
extern gss_OID GSS_KRB5_NT_USER_NAME;

/*
 * This name form shall be represented by the Object Identifier {iso(1)
 * member-body(2) United States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) machine_uid_name(2)}.  The recommended symbolic name for
 * this type is "GSS_KRB5_NT_MACHINE_UID_NAME".
 */
extern gss_OID GSS_KRB5_NT_MACHINE_UID_NAME;

/*
 * This name form shall be represented by the Object Identifier {iso(1)
 * member-body(2) United States(840) mit(113554) infosys(1) gssapi(2)
 * generic(1) string_uid_name(3)}.  The recommended symbolic name for
 * this type is "GSS_KRB5_NT_STRING_UID_NAME".
 */
extern gss_OID GSS_KRB5_NT_STRING_UID_NAME;

/* Major status codes */

#define GSS_S_COMPLETE 0

/*
 * Some "helper" definitions to make the status code macros obvious.
 */
#define GSS_C_CALLING_ERROR_OFFSET 24
#define GSS_C_ROUTINE_ERROR_OFFSET 16
#define GSS_C_SUPPLEMENTARY_OFFSET 0
#define GSS_C_CALLING_ERROR_MASK 0377ul
#define GSS_C_ROUTINE_ERROR_MASK 0377ul
#define GSS_C_SUPPLEMENTARY_MASK 0177777ul

/*
 * The macros that test status codes for error conditions.
 * Note that the GSS_ERROR() macro has changed slightly from
 * the V1 GSS-API so that it now evaluates its argument
 * only once.
 */
#define GSS_CALLING_ERROR(x) \
 (x & (GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET))
#define GSS_ROUTINE_ERROR(x) \
 (x & (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET))
#define GSS_SUPPLEMENTARY_INFO(x) \
 (x & (GSS_C_SUPPLEMENTARY_MASK << GSS_C_SUPPLEMENTARY_OFFSET))
#define GSS_ERROR(x) \
 (x & ((GSS_C_CALLING_ERROR_MASK << GSS_C_CALLING_ERROR_OFFSET) | \
       (GSS_C_ROUTINE_ERROR_MASK << GSS_C_ROUTINE_ERROR_OFFSET)))

/*
 * Now the actual status code definitions
 */

/*
 * Calling errors:
 */
#define GSS_S_CALL_INACCESSIBLE_READ \
(1ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_INACCESSIBLE_WRITE \
(2ul << GSS_C_CALLING_ERROR_OFFSET)
#define GSS_S_CALL_BAD_STRUCTURE \
(3ul << GSS_C_CALLING_ERROR_OFFSET)

/*
 * Routine errors:
 */
#define GSS_S_BAD_MECH             (1ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAME             (2ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_NAMETYPE         (3ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_BINDINGS         (4ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_STATUS           (5ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_SIG              (6ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_MIC		   GSS_S_BAD_SIG
#define GSS_S_NO_CRED              (7ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NO_CONTEXT           (8ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_TOKEN      (9ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DEFECTIVE_CREDENTIAL (10ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CREDENTIALS_EXPIRED  (11ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_CONTEXT_EXPIRED      (12ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_FAILURE              (13ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_BAD_QOP              (14ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAUTHORIZED         (15ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_UNAVAILABLE          (16ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_DUPLICATE_ELEMENT    (17ul << GSS_C_ROUTINE_ERROR_OFFSET)
#define GSS_S_NAME_NOT_MN          (18ul << GSS_C_ROUTINE_ERROR_OFFSET)

/*
 * Supplementary info bits:
 */
#define GSS_S_CONTINUE_NEEDED \
	 (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 0))
#define GSS_S_DUPLICATE_TOKEN \
	 (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 1))
#define GSS_S_OLD_TOKEN \
	 (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 2))
#define GSS_S_UNSEQ_TOKEN \
	 (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 3))
#define GSS_S_GAP_TOKEN \
	 (1ul << (GSS_C_SUPPLEMENTARY_OFFSET + 4))

__BEGIN_DECLS

/*
 * Finally, function prototypes for the GSS-API routines.
 */
OM_uint32 gss_acquire_cred
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* desired_name */
	       OM_uint32,              /* time_req */
	       const gss_OID_set,      /* desired_mechs */
	       gss_cred_usage_t,       /* cred_usage */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *             /* time_rec */
	      );

OM_uint32 gss_release_cred
	      (OM_uint32 *,            /* minor_status */
	       gss_cred_id_t *         /* cred_handle */
	      );

OM_uint32 gss_init_sec_context
	      (OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* initiator_cred_handle */
	       gss_ctx_id_t *,         /* context_handle */
	       const gss_name_t,       /* target_name */
	       const gss_OID,          /* mech_type */
	       OM_uint32,              /* req_flags */
	       OM_uint32,              /* time_req */
	       const gss_channel_bindings_t,
				       /* input_chan_bindings */
	       const gss_buffer_t,     /* input_token */
	       gss_OID *,              /* actual_mech_type */
	       gss_buffer_t,           /* output_token */
	       OM_uint32 *,            /* ret_flags */
	       OM_uint32 *             /* time_rec */
	      );

OM_uint32 gss_accept_sec_context
	      (OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       const gss_cred_id_t,    /* acceptor_cred_handle */
	       const gss_buffer_t,     /* input_token_buffer */
	       const gss_channel_bindings_t,
				       /* input_chan_bindings */
	       gss_name_t *,           /* src_name */
	       gss_OID *,              /* mech_type */
	       gss_buffer_t,           /* output_token */
	       OM_uint32 *,            /* ret_flags */
	       OM_uint32 *,            /* time_rec */
	       gss_cred_id_t *         /* delegated_cred_handle */
	      );

OM_uint32 gss_process_context_token
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t      /* token_buffer */
	      );

OM_uint32 gss_delete_sec_context
	      (OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       gss_buffer_t            /* output_token */
	      );

OM_uint32 gss_context_time
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       OM_uint32 *             /* time_rec */
	      );

OM_uint32 gss_get_mic
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       gss_qop_t,              /* qop_req */
	       const gss_buffer_t,     /* message_buffer */
	       gss_buffer_t            /* message_token */
	      );

OM_uint32 gss_verify_mic
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t,     /* message_buffer */
	       const gss_buffer_t,     /* token_buffer */
	       gss_qop_t *             /* qop_state */
	      );

OM_uint32 gss_wrap
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       int,                    /* conf_req_flag */
	       gss_qop_t,              /* qop_req */
	       const gss_buffer_t,     /* input_message_buffer */
	       int *,                  /* conf_state */
	       gss_buffer_t            /* output_message_buffer */
	      );

OM_uint32 gss_unwrap
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t,     /* input_message_buffer */
	       gss_buffer_t,           /* output_message_buffer */
	       int *,                  /* conf_state */
	       gss_qop_t *             /* qop_state */
	      );

OM_uint32 gss_display_status
	      (OM_uint32 *,            /* minor_status */
	       OM_uint32,              /* status_value */
	       int,                    /* status_type */
	       const gss_OID,          /* mech_type */
	       OM_uint32 *,            /* message_context */
	       gss_buffer_t            /* status_string */
	      );

OM_uint32 gss_indicate_mechs
	      (OM_uint32 *,            /* minor_status */
	       gss_OID_set *           /* mech_set */
	      );

OM_uint32 gss_compare_name
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* name1 */
	       const gss_name_t,       /* name2 */
	       int *                   /* name_equal */
	      );

OM_uint32 gss_display_name
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_buffer_t,           /* output_name_buffer */
	       gss_OID *               /* output_name_type */
	      );

OM_uint32 gss_import_name
	      (OM_uint32 *,            /* minor_status */
	       const gss_buffer_t,     /* input_name_buffer */
	       const gss_OID,          /* input_name_type */
	       gss_name_t *            /* output_name */
	      );

OM_uint32 gss_export_name
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_buffer_t            /* exported_name */
	      );

OM_uint32 gss_release_name
	      (OM_uint32 *,            /* minor_status */
	       gss_name_t *            /* input_name */
	      );

OM_uint32 gss_release_buffer
	      (OM_uint32 *,            /* minor_status */
	       gss_buffer_t            /* buffer */
	      );

OM_uint32 gss_release_oid_set
	      (OM_uint32 *,            /* minor_status */
	       gss_OID_set *           /* set */
	      );

OM_uint32 gss_inquire_cred
	      (OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* cred_handle */
	       gss_name_t *,           /* name */
	       OM_uint32 *,            /* lifetime */
	       gss_cred_usage_t *,     /* cred_usage */
	       gss_OID_set *           /* mechanisms */
	      );

OM_uint32 gss_inquire_context (
	       OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       gss_name_t *,           /* src_name */
	       gss_name_t *,           /* targ_name */
	       OM_uint32 *,            /* lifetime_rec */
	       gss_OID *,              /* mech_type */
	       OM_uint32 *,            /* ctx_flags */
	       int *,                  /* locally_initiated */
	       int *                   /* open */
	      );

OM_uint32 gss_wrap_size_limit (
	       OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       int,                    /* conf_req_flag */
	       gss_qop_t,              /* qop_req */
	       OM_uint32,              /* req_output_size */
	       OM_uint32 *             /* max_input_size */
	      );

OM_uint32 gss_add_cred (
	       OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* input_cred_handle */
	       const gss_name_t,       /* desired_name */
	       const gss_OID,          /* desired_mech */
	       gss_cred_usage_t,       /* cred_usage */
	       OM_uint32,              /* initiator_time_req */
	       OM_uint32,              /* acceptor_time_req */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *,            /* initiator_time_rec */
	       OM_uint32 *             /* acceptor_time_rec */
	      );

OM_uint32 gss_inquire_cred_by_mech (
	       OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* cred_handle */
	       const gss_OID,          /* mech_type */
	       gss_name_t *,           /* name */
	       OM_uint32 *,            /* initiator_lifetime */
	       OM_uint32 *,            /* acceptor_lifetime */
	       gss_cred_usage_t *      /* cred_usage */
	      );

OM_uint32 gss_export_sec_context (
	       OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       gss_buffer_t            /* interprocess_token */
	      );

OM_uint32 gss_import_sec_context (
	       OM_uint32 *,            /* minor_status */
	       const gss_buffer_t,     /* interprocess_token */
	       gss_ctx_id_t *          /* context_handle */
	      );

OM_uint32 gss_create_empty_oid_set (
	       OM_uint32 *,            /* minor_status */
	       gss_OID_set *           /* oid_set */
	      );

OM_uint32 gss_add_oid_set_member (
	       OM_uint32 *,            /* minor_status */
	       const gss_OID,          /* member_oid */
	       gss_OID_set *           /* oid_set */
	      );

OM_uint32 gss_test_oid_set_member (
	       OM_uint32 *,            /* minor_status */
	       const gss_OID,          /* member */
	       const gss_OID_set,      /* set */
	       int *                   /* present */
	      );

OM_uint32 gss_inquire_names_for_mech (
	       OM_uint32 *,            /* minor_status */
	       const gss_OID,          /* mechanism */
	       gss_OID_set *           /* name_types */
	      );

OM_uint32 gss_inquire_mechs_for_name (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_OID_set *           /* mech_types */
	      );

OM_uint32 gss_canonicalize_name (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       const gss_OID,          /* mech_type */
	       gss_name_t *            /* output_name */
	      );

OM_uint32 gss_duplicate_name (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* src_name */
	       gss_name_t *            /* dest_name */
	      );

/*
 * The following routines are obsolete variants of gss_get_mic,
 * gss_verify_mic, gss_wrap and gss_unwrap.  They should be
 * provided by GSS-API V2 implementations for backwards
 * compatibility with V1 applications.  Distinct entrypoints
 * (as opposed to #defines) should be provided, both to allow
 * GSS-API V1 applications to link against GSS-API V2 implementations,
 * and to retain the slight parameter type differences between the
 * obsolete versions of these routines and their current forms.
 */

OM_uint32 gss_sign
	      (OM_uint32 *,       /* minor_status */
	       gss_ctx_id_t,      /* context_handle */
	       int,               /* qop_req */
	       gss_buffer_t,      /* message_buffer */
	       gss_buffer_t       /* message_token */
	      );


OM_uint32 gss_verify
	      (OM_uint32 *,       /* minor_status */
	       gss_ctx_id_t,      /* context_handle */
	       gss_buffer_t,      /* message_buffer */
	       gss_buffer_t,      /* token_buffer */
	       int *              /* qop_state */
	      );

OM_uint32 gss_seal
	      (OM_uint32 *,       /* minor_status */
	       gss_ctx_id_t,      /* context_handle */
	       int,               /* conf_req_flag */
	       int,               /* qop_req */
	       gss_buffer_t,      /* input_message_buffer */
	       int *,             /* conf_state */
	       gss_buffer_t       /* output_message_buffer */
	      );


OM_uint32 gss_unseal
	      (OM_uint32 *,       /* minor_status */
	       gss_ctx_id_t,      /* context_handle */
	       gss_buffer_t,      /* input_message_buffer */
	       gss_buffer_t,      /* output_message_buffer */
	       int *,             /* conf_state */
	       int *              /* qop_state */
	      );

/*
 * Other extensions and helper functions.
 */

int gss_oid_equal
	      (const gss_OID,	  /* first OID to compare */
	       const gss_OID	  /* second OID to compare */
	      );

OM_uint32 gss_release_oid
	      (OM_uint32 *,	  /* minor status */
	       gss_OID *	  /* oid to free */
	      );

OM_uint32 gss_decapsulate_token
	      (const gss_buffer_t,  /* mechanism independent token */
	       gss_OID,		 /* desired mechanism */
	       gss_buffer_t	 /* decapsulated mechanism dependent token */
	      );

OM_uint32 gss_encapsulate_token
	      (const gss_buffer_t,  /* mechanism dependent token */
	       gss_OID,		 /* desired mechanism */
	       gss_buffer_t	 /* encapsulated mechanism independent token */
	      );

OM_uint32 gss_duplicate_oid
              (OM_uint32 *,	/* minor status */
	       const gss_OID,	/* oid to copy */
	       gss_OID *	/* result */
	      );

OM_uint32 gss_oid_to_str
	      (OM_uint32 *,	/* minor status */
	       gss_OID,		/* oid to convert */
	       gss_buffer_t	/* buffer to contain string */
	      );

typedef struct gss_buffer_set_desc_struct  {
  size_t	count;
  gss_buffer_desc *elements;
} gss_buffer_set_desc, *gss_buffer_set_t;

#define GSS_C_NO_BUFFER_SET ((gss_buffer_set_t) 0)

OM_uint32 gss_create_empty_buffer_set
	      (OM_uint32 *,		/* minor status */
	       gss_buffer_set_t *	/* location for new buffer set */
	      );

OM_uint32 gss_add_buffer_set_member
	      (OM_uint32 *,		/* minor status */
	       gss_buffer_t,		/* buffer to add */
	       gss_buffer_set_t *	/* set to add to */
	      );

OM_uint32 gss_release_buffer_set
	      (OM_uint32 *,		/* minor status */
	       gss_buffer_set_t *	/* set to release */
	      );

OM_uint32 gss_inquire_sec_context_by_oid
	      (OM_uint32 *,		/* minor_status */
	       const gss_ctx_id_t,	/* context_handle */
	       const gss_OID,		/* desired_object */
	       gss_buffer_set_t *	/* result */
	      );

OM_uint32 gss_inquire_cred_by_oid
	      (OM_uint32 *,		/* minor_status */
	       const gss_cred_id_t,	/* cred_handle */
	       const gss_OID,		/* desired_object */
	       gss_buffer_set_t *	/* result */
	      );

OM_uint32 gss_set_sec_context_option
	      (OM_uint32 *,		/* minor status */
	       gss_ctx_id_t *,		/* context */
	       const gss_OID,		/* option to set */
	       const gss_buffer_t	/* option value */
	      );

OM_uint32 gss_set_cred_option
	      (OM_uint32 *,		/* minor status */
	       gss_cred_id_t *,		/* cred */
	       const gss_OID,		/* option to set */
	       const gss_buffer_t	/* option value */
	      );

OM_uint32 gss_pseudo_random
	      (OM_uint32 *,		/* minor status */
	       gss_ctx_id_t,		/* context handle */
	       int prf_key,		/* XXX */
	       const gss_buffer_t,	/* data to seed generator */
	       ssize_t,			/* amount of data required */
	       gss_buffer_t		/* buffer for result */
	      );

#ifdef _UID_T_DECLARED
OM_uint32 gss_pname_to_uid
	      (OM_uint32 *,		/* minor status */
	       const gss_name_t pname,	/* principal name */
	       const gss_OID mech,	/* mechanism to query */
	       uid_t *uidp		/* pointer to UID for result */
	      );
#endif

__END_DECLS

#endif /* _GSSAPI_GSSAPI_H_ */
