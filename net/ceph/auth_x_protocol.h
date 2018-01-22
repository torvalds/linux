/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_CEPH_AUTH_X_PROTOCOL
#define __FS_CEPH_AUTH_X_PROTOCOL

#define CEPHX_GET_AUTH_SESSION_KEY      0x0100
#define CEPHX_GET_PRINCIPAL_SESSION_KEY 0x0200
#define CEPHX_GET_ROTATING_KEY          0x0400

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
	/* ticket blob */
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
} __attribute__ ((packed));

struct ceph_x_authorize_reply {
	__u8 struct_v;
	__le64 nonce_plus_one;
} __attribute__ ((packed));


/*
 * encyption bundle
 */
#define CEPHX_ENC_MAGIC 0xff009cad8826aa55ull

struct ceph_x_encrypt_header {
	__u8 struct_v;
	__le64 magic;
} __attribute__ ((packed));

#endif
