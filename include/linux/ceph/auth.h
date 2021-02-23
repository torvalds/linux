/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_AUTH_H
#define _FS_CEPH_AUTH_H

#include <linux/ceph/types.h>
#include <linux/ceph/buffer.h>

/*
 * Abstract interface for communicating with the authenticate module.
 * There is some handshake that takes place between us and the monitor
 * to acquire the necessary keys.  These are used to generate an
 * 'authorizer' that we use when connecting to a service (mds, osd).
 */

struct ceph_auth_client;
struct ceph_msg;

struct ceph_authorizer {
	void (*destroy)(struct ceph_authorizer *);
};

struct ceph_auth_handshake {
	struct ceph_authorizer *authorizer;
	void *authorizer_buf;
	size_t authorizer_buf_len;
	void *authorizer_reply_buf;
	size_t authorizer_reply_buf_len;
	int (*sign_message)(struct ceph_auth_handshake *auth,
			    struct ceph_msg *msg);
	int (*check_message_signature)(struct ceph_auth_handshake *auth,
				       struct ceph_msg *msg);
};

struct ceph_auth_client_ops {
	/*
	 * true if we are authenticated and can connect to
	 * services.
	 */
	int (*is_authenticated)(struct ceph_auth_client *ac);

	/*
	 * true if we should (re)authenticate, e.g., when our tickets
	 * are getting old and crusty.
	 */
	int (*should_authenticate)(struct ceph_auth_client *ac);

	/*
	 * build requests and process replies during monitor
	 * handshake.  if handle_reply returns -EAGAIN, we build
	 * another request.
	 */
	int (*build_request)(struct ceph_auth_client *ac, void *buf, void *end);
	int (*handle_reply)(struct ceph_auth_client *ac, int result,
			    void *buf, void *end, u8 *session_key,
			    int *session_key_len, u8 *con_secret,
			    int *con_secret_len);

	/*
	 * Create authorizer for connecting to a service, and verify
	 * the response to authenticate the service.
	 */
	int (*create_authorizer)(struct ceph_auth_client *ac, int peer_type,
				 struct ceph_auth_handshake *auth);
	/* ensure that an existing authorizer is up to date */
	int (*update_authorizer)(struct ceph_auth_client *ac, int peer_type,
				 struct ceph_auth_handshake *auth);
	int (*add_authorizer_challenge)(struct ceph_auth_client *ac,
					struct ceph_authorizer *a,
					void *challenge_buf,
					int challenge_buf_len);
	int (*verify_authorizer_reply)(struct ceph_auth_client *ac,
				       struct ceph_authorizer *a,
				       void *reply, int reply_len,
				       u8 *session_key, int *session_key_len,
				       u8 *con_secret, int *con_secret_len);
	void (*invalidate_authorizer)(struct ceph_auth_client *ac,
				      int peer_type);

	/* reset when we (re)connect to a monitor */
	void (*reset)(struct ceph_auth_client *ac);

	void (*destroy)(struct ceph_auth_client *ac);

	int (*sign_message)(struct ceph_auth_handshake *auth,
			    struct ceph_msg *msg);
	int (*check_message_signature)(struct ceph_auth_handshake *auth,
				       struct ceph_msg *msg);
};

struct ceph_auth_client {
	u32 protocol;           /* CEPH_AUTH_* */
	void *private;          /* for use by protocol implementation */
	const struct ceph_auth_client_ops *ops;  /* null iff protocol==0 */

	bool negotiating;       /* true if negotiating protocol */
	const char *name;       /* entity name */
	u64 global_id;          /* our unique id in system */
	const struct ceph_crypto_key *key;     /* our secret key */
	unsigned want_keys;     /* which services we want */

	int preferred_mode;	/* CEPH_CON_MODE_* */
	int fallback_mode;	/* ditto */

	struct mutex mutex;
};

struct ceph_auth_client *ceph_auth_init(const char *name,
					const struct ceph_crypto_key *key,
					const int *con_modes);
extern void ceph_auth_destroy(struct ceph_auth_client *ac);

extern void ceph_auth_reset(struct ceph_auth_client *ac);

extern int ceph_auth_build_hello(struct ceph_auth_client *ac,
				 void *buf, size_t len);
extern int ceph_handle_auth_reply(struct ceph_auth_client *ac,
				  void *buf, size_t len,
				  void *reply_buf, size_t reply_len);
int ceph_auth_entity_name_encode(const char *name, void **p, void *end);

extern int ceph_build_auth(struct ceph_auth_client *ac,
		    void *msg_buf, size_t msg_len);
extern int ceph_auth_is_authenticated(struct ceph_auth_client *ac);

int __ceph_auth_get_authorizer(struct ceph_auth_client *ac,
			       struct ceph_auth_handshake *auth,
			       int peer_type, bool force_new,
			       int *proto, int *pref_mode, int *fallb_mode);
void ceph_auth_destroy_authorizer(struct ceph_authorizer *a);
int ceph_auth_add_authorizer_challenge(struct ceph_auth_client *ac,
				       struct ceph_authorizer *a,
				       void *challenge_buf,
				       int challenge_buf_len);
int ceph_auth_verify_authorizer_reply(struct ceph_auth_client *ac,
				      struct ceph_authorizer *a,
				      void *reply, int reply_len,
				      u8 *session_key, int *session_key_len,
				      u8 *con_secret, int *con_secret_len);
extern void ceph_auth_invalidate_authorizer(struct ceph_auth_client *ac,
					    int peer_type);

static inline int ceph_auth_sign_message(struct ceph_auth_handshake *auth,
					 struct ceph_msg *msg)
{
	if (auth->sign_message)
		return auth->sign_message(auth, msg);
	return 0;
}

static inline
int ceph_auth_check_message_signature(struct ceph_auth_handshake *auth,
				      struct ceph_msg *msg)
{
	if (auth->check_message_signature)
		return auth->check_message_signature(auth, msg);
	return 0;
}

int ceph_auth_get_request(struct ceph_auth_client *ac, void *buf, int buf_len);
int ceph_auth_handle_reply_more(struct ceph_auth_client *ac, void *reply,
				int reply_len, void *buf, int buf_len);
int ceph_auth_handle_reply_done(struct ceph_auth_client *ac,
				u64 global_id, void *reply, int reply_len,
				u8 *session_key, int *session_key_len,
				u8 *con_secret, int *con_secret_len);
bool ceph_auth_handle_bad_method(struct ceph_auth_client *ac,
				 int used_proto, int result,
				 const int *allowed_protos, int proto_cnt,
				 const int *allowed_modes, int mode_cnt);

int ceph_auth_get_authorizer(struct ceph_auth_client *ac,
			     struct ceph_auth_handshake *auth,
			     int peer_type, void *buf, int *buf_len);
int ceph_auth_handle_svc_reply_more(struct ceph_auth_client *ac,
				    struct ceph_auth_handshake *auth,
				    void *reply, int reply_len,
				    void *buf, int *buf_len);
int ceph_auth_handle_svc_reply_done(struct ceph_auth_client *ac,
				    struct ceph_auth_handshake *auth,
				    void *reply, int reply_len,
				    u8 *session_key, int *session_key_len,
				    u8 *con_secret, int *con_secret_len);
bool ceph_auth_handle_bad_authorizer(struct ceph_auth_client *ac,
				     int peer_type, int used_proto, int result,
				     const int *allowed_protos, int proto_cnt,
				     const int *allowed_modes, int mode_cnt);

#endif
