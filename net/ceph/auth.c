// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <linux/ceph/types.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>
#include "auth_none.h"
#include "auth_x.h"


/*
 * get protocol handler
 */
static u32 supported_protocols[] = {
	CEPH_AUTH_NONE,
	CEPH_AUTH_CEPHX
};

static int init_protocol(struct ceph_auth_client *ac, int proto)
{
	dout("%s proto %d\n", __func__, proto);

	switch (proto) {
	case CEPH_AUTH_NONE:
		return ceph_auth_none_init(ac);
	case CEPH_AUTH_CEPHX:
		return ceph_x_init(ac);
	default:
		pr_err("bad auth protocol %d\n", proto);
		return -EINVAL;
	}
}

void ceph_auth_set_global_id(struct ceph_auth_client *ac, u64 global_id)
{
	dout("%s global_id %llu\n", __func__, global_id);

	if (!global_id)
		pr_err("got zero global_id\n");

	if (ac->global_id && global_id != ac->global_id)
		pr_err("global_id changed from %llu to %llu\n", ac->global_id,
		       global_id);

	ac->global_id = global_id;
}

/*
 * setup, teardown.
 */
struct ceph_auth_client *ceph_auth_init(const char *name,
					const struct ceph_crypto_key *key,
					const int *con_modes)
{
	struct ceph_auth_client *ac;

	ac = kzalloc(sizeof(*ac), GFP_NOFS);
	if (!ac)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ac->mutex);
	ac->negotiating = true;
	if (name)
		ac->name = name;
	else
		ac->name = CEPH_AUTH_NAME_DEFAULT;
	ac->key = key;
	ac->preferred_mode = con_modes[0];
	ac->fallback_mode = con_modes[1];

	dout("%s name '%s' preferred_mode %d fallback_mode %d\n", __func__,
	     ac->name, ac->preferred_mode, ac->fallback_mode);
	return ac;
}

void ceph_auth_destroy(struct ceph_auth_client *ac)
{
	dout("auth_destroy %p\n", ac);
	if (ac->ops)
		ac->ops->destroy(ac);
	kfree(ac);
}

/*
 * Reset occurs when reconnecting to the monitor.
 */
void ceph_auth_reset(struct ceph_auth_client *ac)
{
	mutex_lock(&ac->mutex);
	dout("auth_reset %p\n", ac);
	if (ac->ops && !ac->negotiating)
		ac->ops->reset(ac);
	ac->negotiating = true;
	mutex_unlock(&ac->mutex);
}

/*
 * EntityName, not to be confused with entity_name_t
 */
int ceph_auth_entity_name_encode(const char *name, void **p, void *end)
{
	int len = strlen(name);

	if (*p + 2*sizeof(u32) + len > end)
		return -ERANGE;
	ceph_encode_32(p, CEPH_ENTITY_TYPE_CLIENT);
	ceph_encode_32(p, len);
	ceph_encode_copy(p, name, len);
	return 0;
}

/*
 * Initiate protocol negotiation with monitor.  Include entity name
 * and list supported protocols.
 */
int ceph_auth_build_hello(struct ceph_auth_client *ac, void *buf, size_t len)
{
	struct ceph_mon_request_header *monhdr = buf;
	void *p = monhdr + 1, *end = buf + len, *lenp;
	int i, num;
	int ret;

	mutex_lock(&ac->mutex);
	dout("auth_build_hello\n");
	monhdr->have_version = 0;
	monhdr->session_mon = cpu_to_le16(-1);
	monhdr->session_mon_tid = 0;

	ceph_encode_32(&p, CEPH_AUTH_UNKNOWN);  /* no protocol, yet */

	lenp = p;
	p += sizeof(u32);

	ceph_decode_need(&p, end, 1 + sizeof(u32), bad);
	ceph_encode_8(&p, 1);
	num = ARRAY_SIZE(supported_protocols);
	ceph_encode_32(&p, num);
	ceph_decode_need(&p, end, num * sizeof(u32), bad);
	for (i = 0; i < num; i++)
		ceph_encode_32(&p, supported_protocols[i]);

	ret = ceph_auth_entity_name_encode(ac->name, &p, end);
	if (ret < 0)
		goto out;
	ceph_decode_need(&p, end, sizeof(u64), bad);
	ceph_encode_64(&p, ac->global_id);

	ceph_encode_32(&lenp, p - lenp - sizeof(u32));
	ret = p - buf;
out:
	mutex_unlock(&ac->mutex);
	return ret;

bad:
	ret = -ERANGE;
	goto out;
}

static int build_request(struct ceph_auth_client *ac, bool add_header,
			 void *buf, int buf_len)
{
	void *end = buf + buf_len;
	void *p;
	int ret;

	p = buf;
	if (add_header) {
		/* struct ceph_mon_request_header + protocol */
		ceph_encode_64_safe(&p, end, 0, e_range);
		ceph_encode_16_safe(&p, end, -1, e_range);
		ceph_encode_64_safe(&p, end, 0, e_range);
		ceph_encode_32_safe(&p, end, ac->protocol, e_range);
	}

	ceph_encode_need(&p, end, sizeof(u32), e_range);
	ret = ac->ops->build_request(ac, p + sizeof(u32), end);
	if (ret < 0) {
		pr_err("auth protocol '%s' building request failed: %d\n",
		       ceph_auth_proto_name(ac->protocol), ret);
		return ret;
	}
	dout(" built request %d bytes\n", ret);
	ceph_encode_32(&p, ret);
	return p + ret - buf;

e_range:
	return -ERANGE;
}

/*
 * Handle auth message from monitor.
 */
int ceph_handle_auth_reply(struct ceph_auth_client *ac,
			   void *buf, size_t len,
			   void *reply_buf, size_t reply_len)
{
	void *p = buf;
	void *end = buf + len;
	int protocol;
	s32 result;
	u64 global_id;
	void *payload, *payload_end;
	int payload_len;
	char *result_msg;
	int result_msg_len;
	int ret = -EINVAL;

	mutex_lock(&ac->mutex);
	dout("handle_auth_reply %p %p\n", p, end);
	ceph_decode_need(&p, end, sizeof(u32) * 3 + sizeof(u64), bad);
	protocol = ceph_decode_32(&p);
	result = ceph_decode_32(&p);
	global_id = ceph_decode_64(&p);
	payload_len = ceph_decode_32(&p);
	payload = p;
	p += payload_len;
	ceph_decode_need(&p, end, sizeof(u32), bad);
	result_msg_len = ceph_decode_32(&p);
	result_msg = p;
	p += result_msg_len;
	if (p != end)
		goto bad;

	dout(" result %d '%.*s' gid %llu len %d\n", result, result_msg_len,
	     result_msg, global_id, payload_len);

	payload_end = payload + payload_len;

	if (ac->negotiating) {
		/* server does not support our protocols? */
		if (!protocol && result < 0) {
			ret = result;
			goto out;
		}
		/* set up (new) protocol handler? */
		if (ac->protocol && ac->protocol != protocol) {
			ac->ops->destroy(ac);
			ac->protocol = 0;
			ac->ops = NULL;
		}
		if (ac->protocol != protocol) {
			ret = init_protocol(ac, protocol);
			if (ret) {
				pr_err("auth protocol '%s' init failed: %d\n",
				       ceph_auth_proto_name(protocol), ret);
				goto out;
			}
		}

		ac->negotiating = false;
	}

	if (result) {
		pr_err("auth protocol '%s' mauth authentication failed: %d\n",
		       ceph_auth_proto_name(ac->protocol), result);
		ret = result;
		goto out;
	}

	ret = ac->ops->handle_reply(ac, global_id, payload, payload_end,
				    NULL, NULL, NULL, NULL);
	if (ret == -EAGAIN) {
		ret = build_request(ac, true, reply_buf, reply_len);
		goto out;
	} else if (ret) {
		goto out;
	}

out:
	mutex_unlock(&ac->mutex);
	return ret;

bad:
	pr_err("failed to decode auth msg\n");
	ret = -EINVAL;
	goto out;
}

int ceph_build_auth(struct ceph_auth_client *ac,
		    void *msg_buf, size_t msg_len)
{
	int ret = 0;

	mutex_lock(&ac->mutex);
	if (ac->ops->should_authenticate(ac))
		ret = build_request(ac, true, msg_buf, msg_len);
	mutex_unlock(&ac->mutex);
	return ret;
}

int ceph_auth_is_authenticated(struct ceph_auth_client *ac)
{
	int ret = 0;

	mutex_lock(&ac->mutex);
	if (ac->ops)
		ret = ac->ops->is_authenticated(ac);
	mutex_unlock(&ac->mutex);
	return ret;
}
EXPORT_SYMBOL(ceph_auth_is_authenticated);

int __ceph_auth_get_authorizer(struct ceph_auth_client *ac,
			       struct ceph_auth_handshake *auth,
			       int peer_type, bool force_new,
			       int *proto, int *pref_mode, int *fallb_mode)
{
	int ret;

	mutex_lock(&ac->mutex);
	if (force_new && auth->authorizer) {
		ceph_auth_destroy_authorizer(auth->authorizer);
		auth->authorizer = NULL;
	}
	if (!auth->authorizer)
		ret = ac->ops->create_authorizer(ac, peer_type, auth);
	else if (ac->ops->update_authorizer)
		ret = ac->ops->update_authorizer(ac, peer_type, auth);
	else
		ret = 0;
	if (ret)
		goto out;

	*proto = ac->protocol;
	if (pref_mode && fallb_mode) {
		*pref_mode = ac->preferred_mode;
		*fallb_mode = ac->fallback_mode;
	}

out:
	mutex_unlock(&ac->mutex);
	return ret;
}
EXPORT_SYMBOL(__ceph_auth_get_authorizer);

void ceph_auth_destroy_authorizer(struct ceph_authorizer *a)
{
	a->destroy(a);
}
EXPORT_SYMBOL(ceph_auth_destroy_authorizer);

int ceph_auth_add_authorizer_challenge(struct ceph_auth_client *ac,
				       struct ceph_authorizer *a,
				       void *challenge_buf,
				       int challenge_buf_len)
{
	int ret = 0;

	mutex_lock(&ac->mutex);
	if (ac->ops && ac->ops->add_authorizer_challenge)
		ret = ac->ops->add_authorizer_challenge(ac, a, challenge_buf,
							challenge_buf_len);
	mutex_unlock(&ac->mutex);
	return ret;
}
EXPORT_SYMBOL(ceph_auth_add_authorizer_challenge);

int ceph_auth_verify_authorizer_reply(struct ceph_auth_client *ac,
				      struct ceph_authorizer *a,
				      void *reply, int reply_len,
				      u8 *session_key, int *session_key_len,
				      u8 *con_secret, int *con_secret_len)
{
	int ret = 0;

	mutex_lock(&ac->mutex);
	if (ac->ops && ac->ops->verify_authorizer_reply)
		ret = ac->ops->verify_authorizer_reply(ac, a,
			reply, reply_len, session_key, session_key_len,
			con_secret, con_secret_len);
	mutex_unlock(&ac->mutex);
	return ret;
}
EXPORT_SYMBOL(ceph_auth_verify_authorizer_reply);

void ceph_auth_invalidate_authorizer(struct ceph_auth_client *ac, int peer_type)
{
	mutex_lock(&ac->mutex);
	if (ac->ops && ac->ops->invalidate_authorizer)
		ac->ops->invalidate_authorizer(ac, peer_type);
	mutex_unlock(&ac->mutex);
}
EXPORT_SYMBOL(ceph_auth_invalidate_authorizer);

/*
 * msgr2 authentication
 */

static bool contains(const int *arr, int cnt, int val)
{
	int i;

	for (i = 0; i < cnt; i++) {
		if (arr[i] == val)
			return true;
	}

	return false;
}

static int encode_con_modes(void **p, void *end, int pref_mode, int fallb_mode)
{
	WARN_ON(pref_mode == CEPH_CON_MODE_UNKNOWN);
	if (fallb_mode != CEPH_CON_MODE_UNKNOWN) {
		ceph_encode_32_safe(p, end, 2, e_range);
		ceph_encode_32_safe(p, end, pref_mode, e_range);
		ceph_encode_32_safe(p, end, fallb_mode, e_range);
	} else {
		ceph_encode_32_safe(p, end, 1, e_range);
		ceph_encode_32_safe(p, end, pref_mode, e_range);
	}

	return 0;

e_range:
	return -ERANGE;
}

/*
 * Similar to ceph_auth_build_hello().
 */
int ceph_auth_get_request(struct ceph_auth_client *ac, void *buf, int buf_len)
{
	int proto = ac->key ? CEPH_AUTH_CEPHX : CEPH_AUTH_NONE;
	void *end = buf + buf_len;
	void *lenp;
	void *p;
	int ret;

	mutex_lock(&ac->mutex);
	if (ac->protocol == CEPH_AUTH_UNKNOWN) {
		ret = init_protocol(ac, proto);
		if (ret) {
			pr_err("auth protocol '%s' init failed: %d\n",
			       ceph_auth_proto_name(proto), ret);
			goto out;
		}
	} else {
		WARN_ON(ac->protocol != proto);
		ac->ops->reset(ac);
	}

	p = buf;
	ceph_encode_32_safe(&p, end, ac->protocol, e_range);
	ret = encode_con_modes(&p, end, ac->preferred_mode, ac->fallback_mode);
	if (ret)
		goto out;

	lenp = p;
	p += 4;  /* space for len */

	ceph_encode_8_safe(&p, end, CEPH_AUTH_MODE_MON, e_range);
	ret = ceph_auth_entity_name_encode(ac->name, &p, end);
	if (ret)
		goto out;

	ceph_encode_64_safe(&p, end, ac->global_id, e_range);
	ceph_encode_32(&lenp, p - lenp - 4);
	ret = p - buf;

out:
	mutex_unlock(&ac->mutex);
	return ret;

e_range:
	ret = -ERANGE;
	goto out;
}

int ceph_auth_handle_reply_more(struct ceph_auth_client *ac, void *reply,
				int reply_len, void *buf, int buf_len)
{
	int ret;

	mutex_lock(&ac->mutex);
	ret = ac->ops->handle_reply(ac, 0, reply, reply + reply_len,
				    NULL, NULL, NULL, NULL);
	if (ret == -EAGAIN)
		ret = build_request(ac, false, buf, buf_len);
	else
		WARN_ON(ret >= 0);
	mutex_unlock(&ac->mutex);
	return ret;
}

int ceph_auth_handle_reply_done(struct ceph_auth_client *ac,
				u64 global_id, void *reply, int reply_len,
				u8 *session_key, int *session_key_len,
				u8 *con_secret, int *con_secret_len)
{
	int ret;

	mutex_lock(&ac->mutex);
	ret = ac->ops->handle_reply(ac, global_id, reply, reply + reply_len,
				    session_key, session_key_len,
				    con_secret, con_secret_len);
	WARN_ON(ret == -EAGAIN || ret > 0);
	mutex_unlock(&ac->mutex);
	return ret;
}

bool ceph_auth_handle_bad_method(struct ceph_auth_client *ac,
				 int used_proto, int result,
				 const int *allowed_protos, int proto_cnt,
				 const int *allowed_modes, int mode_cnt)
{
	mutex_lock(&ac->mutex);
	WARN_ON(used_proto != ac->protocol);

	if (result == -EOPNOTSUPP) {
		if (!contains(allowed_protos, proto_cnt, ac->protocol)) {
			pr_err("auth protocol '%s' not allowed\n",
			       ceph_auth_proto_name(ac->protocol));
			goto not_allowed;
		}
		if (!contains(allowed_modes, mode_cnt, ac->preferred_mode) &&
		    (ac->fallback_mode == CEPH_CON_MODE_UNKNOWN ||
		     !contains(allowed_modes, mode_cnt, ac->fallback_mode))) {
			pr_err("preferred mode '%s' not allowed\n",
			       ceph_con_mode_name(ac->preferred_mode));
			if (ac->fallback_mode == CEPH_CON_MODE_UNKNOWN)
				pr_err("no fallback mode\n");
			else
				pr_err("fallback mode '%s' not allowed\n",
				       ceph_con_mode_name(ac->fallback_mode));
			goto not_allowed;
		}
	}

	WARN_ON(result == -EOPNOTSUPP || result >= 0);
	pr_err("auth protocol '%s' msgr authentication failed: %d\n",
	       ceph_auth_proto_name(ac->protocol), result);

	mutex_unlock(&ac->mutex);
	return true;

not_allowed:
	mutex_unlock(&ac->mutex);
	return false;
}

int ceph_auth_get_authorizer(struct ceph_auth_client *ac,
			     struct ceph_auth_handshake *auth,
			     int peer_type, void *buf, int *buf_len)
{
	void *end = buf + *buf_len;
	int pref_mode, fallb_mode;
	int proto;
	void *p;
	int ret;

	ret = __ceph_auth_get_authorizer(ac, auth, peer_type, true, &proto,
					 &pref_mode, &fallb_mode);
	if (ret)
		return ret;

	p = buf;
	ceph_encode_32_safe(&p, end, proto, e_range);
	ret = encode_con_modes(&p, end, pref_mode, fallb_mode);
	if (ret)
		return ret;

	ceph_encode_32_safe(&p, end, auth->authorizer_buf_len, e_range);
	*buf_len = p - buf;
	return 0;

e_range:
	return -ERANGE;
}
EXPORT_SYMBOL(ceph_auth_get_authorizer);

int ceph_auth_handle_svc_reply_more(struct ceph_auth_client *ac,
				    struct ceph_auth_handshake *auth,
				    void *reply, int reply_len,
				    void *buf, int *buf_len)
{
	void *end = buf + *buf_len;
	void *p;
	int ret;

	ret = ceph_auth_add_authorizer_challenge(ac, auth->authorizer,
						 reply, reply_len);
	if (ret)
		return ret;

	p = buf;
	ceph_encode_32_safe(&p, end, auth->authorizer_buf_len, e_range);
	*buf_len = p - buf;
	return 0;

e_range:
	return -ERANGE;
}
EXPORT_SYMBOL(ceph_auth_handle_svc_reply_more);

int ceph_auth_handle_svc_reply_done(struct ceph_auth_client *ac,
				    struct ceph_auth_handshake *auth,
				    void *reply, int reply_len,
				    u8 *session_key, int *session_key_len,
				    u8 *con_secret, int *con_secret_len)
{
	return ceph_auth_verify_authorizer_reply(ac, auth->authorizer,
		reply, reply_len, session_key, session_key_len,
		con_secret, con_secret_len);
}
EXPORT_SYMBOL(ceph_auth_handle_svc_reply_done);

bool ceph_auth_handle_bad_authorizer(struct ceph_auth_client *ac,
				     int peer_type, int used_proto, int result,
				     const int *allowed_protos, int proto_cnt,
				     const int *allowed_modes, int mode_cnt)
{
	mutex_lock(&ac->mutex);
	WARN_ON(used_proto != ac->protocol);

	if (result == -EOPNOTSUPP) {
		if (!contains(allowed_protos, proto_cnt, ac->protocol)) {
			pr_err("auth protocol '%s' not allowed by %s\n",
			       ceph_auth_proto_name(ac->protocol),
			       ceph_entity_type_name(peer_type));
			goto not_allowed;
		}
		if (!contains(allowed_modes, mode_cnt, ac->preferred_mode) &&
		    (ac->fallback_mode == CEPH_CON_MODE_UNKNOWN ||
		     !contains(allowed_modes, mode_cnt, ac->fallback_mode))) {
			pr_err("preferred mode '%s' not allowed by %s\n",
			       ceph_con_mode_name(ac->preferred_mode),
			       ceph_entity_type_name(peer_type));
			if (ac->fallback_mode == CEPH_CON_MODE_UNKNOWN)
				pr_err("no fallback mode\n");
			else
				pr_err("fallback mode '%s' not allowed by %s\n",
				       ceph_con_mode_name(ac->fallback_mode),
				       ceph_entity_type_name(peer_type));
			goto not_allowed;
		}
	}

	WARN_ON(result == -EOPNOTSUPP || result >= 0);
	pr_err("auth protocol '%s' authorization to %s failed: %d\n",
	       ceph_auth_proto_name(ac->protocol),
	       ceph_entity_type_name(peer_type), result);

	if (ac->ops->invalidate_authorizer)
		ac->ops->invalidate_authorizer(ac, peer_type);

	mutex_unlock(&ac->mutex);
	return true;

not_allowed:
	mutex_unlock(&ac->mutex);
	return false;
}
EXPORT_SYMBOL(ceph_auth_handle_bad_authorizer);
