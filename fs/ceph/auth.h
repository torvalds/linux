#ifndef _FS_CEPH_AUTH_H
#define _FS_CEPH_AUTH_H

#include "types.h"
#include "buffer.h"

/*
 * Abstract interface for communicating with the authenticate module.
 * There is some handshake that takes place between us and the monitor
 * to acquire the necessary keys.  These are used to generate an
 * 'authorizer' that we use when connecting to a service (mds, osd).
 */

struct ceph_auth_client;
struct ceph_authorizer;

struct ceph_auth_client_ops {
	const char *name;

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
			    void *buf, void *end);

	/*
	 * Create authorizer for connecting to a service, and verify
	 * the response to authenticate the service.
	 */
	int (*create_authorizer)(struct ceph_auth_client *ac, int peer_type,
				 struct ceph_authorizer **a,
				 void **buf, size_t *len,
				 void **reply_buf, size_t *reply_len);
	int (*verify_authorizer_reply)(struct ceph_auth_client *ac,
				       struct ceph_authorizer *a, size_t len);
	void (*destroy_authorizer)(struct ceph_auth_client *ac,
				   struct ceph_authorizer *a);
	void (*invalidate_authorizer)(struct ceph_auth_client *ac,
				      int peer_type);

	/* reset when we (re)connect to a monitor */
	void (*reset)(struct ceph_auth_client *ac);

	void (*destroy)(struct ceph_auth_client *ac);
};

struct ceph_auth_client {
	u32 protocol;           /* CEPH_AUTH_* */
	void *private;          /* for use by protocol implementation */
	const struct ceph_auth_client_ops *ops;  /* null iff protocol==0 */

	bool negotiating;       /* true if negotiating protocol */
	const char *name;       /* entity name */
	u64 global_id;          /* our unique id in system */
	const char *secret;     /* our secret key */
	unsigned want_keys;     /* which services we want */
};

extern struct ceph_auth_client *ceph_auth_init(const char *name,
					       const char *secret);
extern void ceph_auth_destroy(struct ceph_auth_client *ac);

extern void ceph_auth_reset(struct ceph_auth_client *ac);

extern int ceph_auth_build_hello(struct ceph_auth_client *ac,
				 void *buf, size_t len);
extern int ceph_handle_auth_reply(struct ceph_auth_client *ac,
				  void *buf, size_t len,
				  void *reply_buf, size_t reply_len);
extern int ceph_entity_name_encode(const char *name, void **p, void *end);

extern int ceph_build_auth(struct ceph_auth_client *ac,
		    void *msg_buf, size_t msg_len);

extern int ceph_auth_is_authenticated(struct ceph_auth_client *ac);

#endif
