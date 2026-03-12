/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_CEPH_AUTH_X_PROTOCOL
#define __FS_CEPH_AUTH_X_PROTOCOL

#define CEPHX_GET_AUTH_SESSION_KEY      0x0100
#define CEPHX_GET_PRINCIPAL_SESSION_KEY 0x0200
#define CEPHX_GET_ROTATING_KEY          0x0400

/* Client <-> AuthMonitor */
/*
 * The AUTH session's connection secret: encrypted with the AUTH
 * ticket session key
 */
#define CEPHX_KEY_USAGE_AUTH_CONNECTION_SECRET	0x03
/*
 * The ticket's blob for the client ("blob for me", contains the
 * session key): encrypted with the client's secret key in case of
 * the AUTH ticket and the AUTH ticket session key in case of other
 * service tickets
 */
#define CEPHX_KEY_USAGE_TICKET_SESSION_KEY	0x04
/*
 * The ticket's blob for the service (ceph_x_ticket_blob): possibly
 * encrypted with the old AUTH ticket session key in case of the AUTH
 * ticket and not encrypted in case of other service tickets
 */
#define CEPHX_KEY_USAGE_TICKET_BLOB		0x05

/* Client <-> Service */
/*
 * The client's authorization request (ceph_x_authorize_b):
 * encrypted with the service ticket session key
 */
#define CEPHX_KEY_USAGE_AUTHORIZE		0x10
/*
 * The service's challenge (ceph_x_authorize_challenge):
 * encrypted with the service ticket session key
 */
#define CEPHX_KEY_USAGE_AUTHORIZE_CHALLENGE	0x11
/*
 * The service's final reply (ceph_x_authorize_reply + the service
 * session's connection secret): encrypted with the service ticket
 * session key
 */
#define CEPHX_KEY_USAGE_AUTHORIZE_REPLY		0x12

/* common bits */
struct ceph_x_ticket_blob {
	__u8 struct_v;
	__le64 secret_id;
	__le32 blob_len;
	char blob[];
} __attribute__ ((packed));


/* common request/reply headers */
struct ceph_x_request_header {
	__le16 op;
} __attribute__ ((packed));

struct ceph_x_reply_header {
	__le16 op;
	__le32 result;
} __attribute__ ((packed));


/* authenticate handshake */

/* initial hello (no reply header) */
struct ceph_x_server_challenge {
	__u8 struct_v;
	__le64 server_challenge;
} __attribute__ ((packed));

struct ceph_x_authenticate {
	__u8 struct_v;
	__le64 client_challenge;
	__le64 key;
	/* old_ticket blob */
	/* nautilus+: other_keys */
} __attribute__ ((packed));

struct ceph_x_service_ticket_request {
	__u8 struct_v;
	__le32 keys;
} __attribute__ ((packed));

struct ceph_x_challenge_blob {
	__le64 server_challenge;
	__le64 client_challenge;
} __attribute__ ((packed));



/* authorize handshake */

/*
 * The authorizer consists of two pieces:
 *  a - service id, ticket blob
 *  b - encrypted with session key
 */
struct ceph_x_authorize_a {
	__u8 struct_v;
	__le64 global_id;
	__le32 service_id;
	struct ceph_x_ticket_blob ticket_blob;
} __attribute__ ((packed));

struct ceph_x_authorize_b {
	__u8 struct_v;
	__le64 nonce;
	__u8 have_challenge;
	__le64 server_challenge_plus_one;
} __attribute__ ((packed));

struct ceph_x_authorize_challenge {
	__u8 struct_v;
	__le64 server_challenge;
} __attribute__ ((packed));

struct ceph_x_authorize_reply {
	__u8 struct_v;
	__le64 nonce_plus_one;
} __attribute__ ((packed));


/*
 * encryption bundle
 */
#define CEPHX_ENC_MAGIC 0xff009cad8826aa55ull

struct ceph_x_encrypt_header {
	__u8 struct_v;
	__le64 magic;
} __attribute__ ((packed));

#endif
