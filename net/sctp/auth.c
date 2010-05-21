/* SCTP kernel implementation
 * (C) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *
 * This file is part of the SCTP kernel implementation
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *   Vlad Yasevich     <vladislav.yasevich@hp.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <net/sctp/sctp.h>
#include <net/sctp/auth.h>

static struct sctp_hmac sctp_hmac_list[SCTP_AUTH_NUM_HMACS] = {
	{
		/* id 0 is reserved.  as all 0 */
		.hmac_id = SCTP_AUTH_HMAC_ID_RESERVED_0,
	},
	{
		.hmac_id = SCTP_AUTH_HMAC_ID_SHA1,
		.hmac_name="hmac(sha1)",
		.hmac_len = SCTP_SHA1_SIG_SIZE,
	},
	{
		/* id 2 is reserved as well */
		.hmac_id = SCTP_AUTH_HMAC_ID_RESERVED_2,
	},
#if defined (CONFIG_CRYPTO_SHA256) || defined (CONFIG_CRYPTO_SHA256_MODULE)
	{
		.hmac_id = SCTP_AUTH_HMAC_ID_SHA256,
		.hmac_name="hmac(sha256)",
		.hmac_len = SCTP_SHA256_SIG_SIZE,
	}
#endif
};


void sctp_auth_key_put(struct sctp_auth_bytes *key)
{
	if (!key)
		return;

	if (atomic_dec_and_test(&key->refcnt)) {
		kfree(key);
		SCTP_DBG_OBJCNT_DEC(keys);
	}
}

/* Create a new key structure of a given length */
static struct sctp_auth_bytes *sctp_auth_create_key(__u32 key_len, gfp_t gfp)
{
	struct sctp_auth_bytes *key;

	/* Verify that we are not going to overflow INT_MAX */
	if ((INT_MAX - key_len) < sizeof(struct sctp_auth_bytes))
		return NULL;

	/* Allocate the shared key */
	key = kmalloc(sizeof(struct sctp_auth_bytes) + key_len, gfp);
	if (!key)
		return NULL;

	key->len = key_len;
	atomic_set(&key->refcnt, 1);
	SCTP_DBG_OBJCNT_INC(keys);

	return key;
}

/* Create a new shared key container with a give key id */
struct sctp_shared_key *sctp_auth_shkey_create(__u16 key_id, gfp_t gfp)
{
	struct sctp_shared_key *new;

	/* Allocate the shared key container */
	new = kzalloc(sizeof(struct sctp_shared_key), gfp);
	if (!new)
		return NULL;

	INIT_LIST_HEAD(&new->key_list);
	new->key_id = key_id;

	return new;
}

/* Free the shared key stucture */
static void sctp_auth_shkey_free(struct sctp_shared_key *sh_key)
{
	BUG_ON(!list_empty(&sh_key->key_list));
	sctp_auth_key_put(sh_key->key);
	sh_key->key = NULL;
	kfree(sh_key);
}

/* Destory the entire key list.  This is done during the
 * associon and endpoint free process.
 */
void sctp_auth_destroy_keys(struct list_head *keys)
{
	struct sctp_shared_key *ep_key;
	struct sctp_shared_key *tmp;

	if (list_empty(keys))
		return;

	key_for_each_safe(ep_key, tmp, keys) {
		list_del_init(&ep_key->key_list);
		sctp_auth_shkey_free(ep_key);
	}
}

/* Compare two byte vectors as numbers.  Return values
 * are:
 * 	  0 - vectors are equal
 * 	< 0 - vector 1 is smaller than vector2
 * 	> 0 - vector 1 is greater than vector2
 *
 * Algorithm is:
 * 	This is performed by selecting the numerically smaller key vector...
 *	If the key vectors are equal as numbers but differ in length ...
 *	the shorter vector is considered smaller
 *
 * Examples (with small values):
 * 	000123456789 > 123456789 (first number is longer)
 * 	000123456789 < 234567891 (second number is larger numerically)
 * 	123456789 > 2345678 	 (first number is both larger & longer)
 */
static int sctp_auth_compare_vectors(struct sctp_auth_bytes *vector1,
			      struct sctp_auth_bytes *vector2)
{
	int diff;
	int i;
	const __u8 *longer;

	diff = vector1->len - vector2->len;
	if (diff) {
		longer = (diff > 0) ? vector1->data : vector2->data;

		/* Check to see if the longer number is
		 * lead-zero padded.  If it is not, it
		 * is automatically larger numerically.
		 */
		for (i = 0; i < abs(diff); i++ ) {
			if (longer[i] != 0)
				return diff;
		}
	}

	/* lengths are the same, compare numbers */
	return memcmp(vector1->data, vector2->data, vector1->len);
}

/*
 * Create a key vector as described in SCTP-AUTH, Section 6.1
 *    The RANDOM parameter, the CHUNKS parameter and the HMAC-ALGO
 *    parameter sent by each endpoint are concatenated as byte vectors.
 *    These parameters include the parameter type, parameter length, and
 *    the parameter value, but padding is omitted; all padding MUST be
 *    removed from this concatenation before proceeding with further
 *    computation of keys.  Parameters which were not sent are simply
 *    omitted from the concatenation process.  The resulting two vectors
 *    are called the two key vectors.
 */
static struct sctp_auth_bytes *sctp_auth_make_key_vector(
			sctp_random_param_t *random,
			sctp_chunks_param_t *chunks,
			sctp_hmac_algo_param_t *hmacs,
			gfp_t gfp)
{
	struct sctp_auth_bytes *new;
	__u32	len;
	__u32	offset = 0;

	len = ntohs(random->param_hdr.length) + ntohs(hmacs->param_hdr.length);
        if (chunks)
		len += ntohs(chunks->param_hdr.length);

	new = kmalloc(sizeof(struct sctp_auth_bytes) + len, gfp);
	if (!new)
		return NULL;

	new->len = len;

	memcpy(new->data, random, ntohs(random->param_hdr.length));
	offset += ntohs(random->param_hdr.length);

	if (chunks) {
		memcpy(new->data + offset, chunks,
			ntohs(chunks->param_hdr.length));
		offset += ntohs(chunks->param_hdr.length);
	}

	memcpy(new->data + offset, hmacs, ntohs(hmacs->param_hdr.length));

	return new;
}


/* Make a key vector based on our local parameters */
static struct sctp_auth_bytes *sctp_auth_make_local_vector(
				    const struct sctp_association *asoc,
				    gfp_t gfp)
{
	return sctp_auth_make_key_vector(
				    (sctp_random_param_t*)asoc->c.auth_random,
				    (sctp_chunks_param_t*)asoc->c.auth_chunks,
				    (sctp_hmac_algo_param_t*)asoc->c.auth_hmacs,
				    gfp);
}

/* Make a key vector based on peer's parameters */
static struct sctp_auth_bytes *sctp_auth_make_peer_vector(
				    const struct sctp_association *asoc,
				    gfp_t gfp)
{
	return sctp_auth_make_key_vector(asoc->peer.peer_random,
					 asoc->peer.peer_chunks,
					 asoc->peer.peer_hmacs,
					 gfp);
}


/* Set the value of the association shared key base on the parameters
 * given.  The algorithm is:
 *    From the endpoint pair shared keys and the key vectors the
 *    association shared keys are computed.  This is performed by selecting
 *    the numerically smaller key vector and concatenating it to the
 *    endpoint pair shared key, and then concatenating the numerically
 *    larger key vector to that.  The result of the concatenation is the
 *    association shared key.
 */
static struct sctp_auth_bytes *sctp_auth_asoc_set_secret(
			struct sctp_shared_key *ep_key,
			struct sctp_auth_bytes *first_vector,
			struct sctp_auth_bytes *last_vector,
			gfp_t gfp)
{
	struct sctp_auth_bytes *secret;
	__u32 offset = 0;
	__u32 auth_len;

	auth_len = first_vector->len + last_vector->len;
	if (ep_key->key)
		auth_len += ep_key->key->len;

	secret = sctp_auth_create_key(auth_len, gfp);
	if (!secret)
		return NULL;

	if (ep_key->key) {
		memcpy(secret->data, ep_key->key->data, ep_key->key->len);
		offset += ep_key->key->len;
	}

	memcpy(secret->data + offset, first_vector->data, first_vector->len);
	offset += first_vector->len;

	memcpy(secret->data + offset, last_vector->data, last_vector->len);

	return secret;
}

/* Create an association shared key.  Follow the algorithm
 * described in SCTP-AUTH, Section 6.1
 */
static struct sctp_auth_bytes *sctp_auth_asoc_create_secret(
				 const struct sctp_association *asoc,
				 struct sctp_shared_key *ep_key,
				 gfp_t gfp)
{
	struct sctp_auth_bytes *local_key_vector;
	struct sctp_auth_bytes *peer_key_vector;
	struct sctp_auth_bytes	*first_vector,
				*last_vector;
	struct sctp_auth_bytes	*secret = NULL;
	int	cmp;


	/* Now we need to build the key vectors
	 * SCTP-AUTH , Section 6.1
	 *    The RANDOM parameter, the CHUNKS parameter and the HMAC-ALGO
	 *    parameter sent by each endpoint are concatenated as byte vectors.
	 *    These parameters include the parameter type, parameter length, and
	 *    the parameter value, but padding is omitted; all padding MUST be
	 *    removed from this concatenation before proceeding with further
	 *    computation of keys.  Parameters which were not sent are simply
	 *    omitted from the concatenation process.  The resulting two vectors
	 *    are called the two key vectors.
	 */

	local_key_vector = sctp_auth_make_local_vector(asoc, gfp);
	peer_key_vector = sctp_auth_make_peer_vector(asoc, gfp);

	if (!peer_key_vector || !local_key_vector)
		goto out;

	/* Figure out the order in wich the key_vectors will be
	 * added to the endpoint shared key.
	 * SCTP-AUTH, Section 6.1:
	 *   This is performed by selecting the numerically smaller key
	 *   vector and concatenating it to the endpoint pair shared
	 *   key, and then concatenating the numerically larger key
	 *   vector to that.  If the key vectors are equal as numbers
	 *   but differ in length, then the concatenation order is the
	 *   endpoint shared key, followed by the shorter key vector,
	 *   followed by the longer key vector.  Otherwise, the key
	 *   vectors are identical, and may be concatenated to the
	 *   endpoint pair key in any order.
	 */
	cmp = sctp_auth_compare_vectors(local_key_vector,
					peer_key_vector);
	if (cmp < 0) {
		first_vector = local_key_vector;
		last_vector = peer_key_vector;
	} else {
		first_vector = peer_key_vector;
		last_vector = local_key_vector;
	}

	secret = sctp_auth_asoc_set_secret(ep_key, first_vector, last_vector,
					    gfp);
out:
	kfree(local_key_vector);
	kfree(peer_key_vector);

	return secret;
}

/*
 * Populate the association overlay list with the list
 * from the endpoint.
 */
int sctp_auth_asoc_copy_shkeys(const struct sctp_endpoint *ep,
				struct sctp_association *asoc,
				gfp_t gfp)
{
	struct sctp_shared_key *sh_key;
	struct sctp_shared_key *new;

	BUG_ON(!list_empty(&asoc->endpoint_shared_keys));

	key_for_each(sh_key, &ep->endpoint_shared_keys) {
		new = sctp_auth_shkey_create(sh_key->key_id, gfp);
		if (!new)
			goto nomem;

		new->key = sh_key->key;
		sctp_auth_key_hold(new->key);
		list_add(&new->key_list, &asoc->endpoint_shared_keys);
	}

	return 0;

nomem:
	sctp_auth_destroy_keys(&asoc->endpoint_shared_keys);
	return -ENOMEM;
}


/* Public interface to creat the association shared key.
 * See code above for the algorithm.
 */
int sctp_auth_asoc_init_active_key(struct sctp_association *asoc, gfp_t gfp)
{
	struct sctp_auth_bytes	*secret;
	struct sctp_shared_key *ep_key;

	/* If we don't support AUTH, or peer is not capable
	 * we don't need to do anything.
	 */
	if (!sctp_auth_enable || !asoc->peer.auth_capable)
		return 0;

	/* If the key_id is non-zero and we couldn't find an
	 * endpoint pair shared key, we can't compute the
	 * secret.
	 * For key_id 0, endpoint pair shared key is a NULL key.
	 */
	ep_key = sctp_auth_get_shkey(asoc, asoc->active_key_id);
	BUG_ON(!ep_key);

	secret = sctp_auth_asoc_create_secret(asoc, ep_key, gfp);
	if (!secret)
		return -ENOMEM;

	sctp_auth_key_put(asoc->asoc_shared_key);
	asoc->asoc_shared_key = secret;

	return 0;
}


/* Find the endpoint pair shared key based on the key_id */
struct sctp_shared_key *sctp_auth_get_shkey(
				const struct sctp_association *asoc,
				__u16 key_id)
{
	struct sctp_shared_key *key;

	/* First search associations set of endpoint pair shared keys */
	key_for_each(key, &asoc->endpoint_shared_keys) {
		if (key->key_id == key_id)
			return key;
	}

	return NULL;
}

/*
 * Initialize all the possible digest transforms that we can use.  Right now
 * now, the supported digests are SHA1 and SHA256.  We do this here once
 * because of the restrictiong that transforms may only be allocated in
 * user context.  This forces us to pre-allocated all possible transforms
 * at the endpoint init time.
 */
int sctp_auth_init_hmacs(struct sctp_endpoint *ep, gfp_t gfp)
{
	struct crypto_hash *tfm = NULL;
	__u16   id;

	/* if the transforms are already allocted, we are done */
	if (!sctp_auth_enable) {
		ep->auth_hmacs = NULL;
		return 0;
	}

	if (ep->auth_hmacs)
		return 0;

	/* Allocated the array of pointers to transorms */
	ep->auth_hmacs = kzalloc(
			    sizeof(struct crypto_hash *) * SCTP_AUTH_NUM_HMACS,
			    gfp);
	if (!ep->auth_hmacs)
		return -ENOMEM;

	for (id = 0; id < SCTP_AUTH_NUM_HMACS; id++) {

		/* See is we support the id.  Supported IDs have name and
		 * length fields set, so that we can allocated and use
		 * them.  We can safely just check for name, for without the
		 * name, we can't allocate the TFM.
		 */
		if (!sctp_hmac_list[id].hmac_name)
			continue;

		/* If this TFM has been allocated, we are all set */
		if (ep->auth_hmacs[id])
			continue;

		/* Allocate the ID */
		tfm = crypto_alloc_hash(sctp_hmac_list[id].hmac_name, 0,
					CRYPTO_ALG_ASYNC);
		if (IS_ERR(tfm))
			goto out_err;

		ep->auth_hmacs[id] = tfm;
	}

	return 0;

out_err:
	/* Clean up any successful allocations */
	sctp_auth_destroy_hmacs(ep->auth_hmacs);
	return -ENOMEM;
}

/* Destroy the hmac tfm array */
void sctp_auth_destroy_hmacs(struct crypto_hash *auth_hmacs[])
{
	int i;

	if (!auth_hmacs)
		return;

	for (i = 0; i < SCTP_AUTH_NUM_HMACS; i++)
	{
		if (auth_hmacs[i])
			crypto_free_hash(auth_hmacs[i]);
	}
	kfree(auth_hmacs);
}


struct sctp_hmac *sctp_auth_get_hmac(__u16 hmac_id)
{
	return &sctp_hmac_list[hmac_id];
}

/* Get an hmac description information that we can use to build
 * the AUTH chunk
 */
struct sctp_hmac *sctp_auth_asoc_get_hmac(const struct sctp_association *asoc)
{
	struct sctp_hmac_algo_param *hmacs;
	__u16 n_elt;
	__u16 id = 0;
	int i;

	/* If we have a default entry, use it */
	if (asoc->default_hmac_id)
		return &sctp_hmac_list[asoc->default_hmac_id];

	/* Since we do not have a default entry, find the first entry
	 * we support and return that.  Do not cache that id.
	 */
	hmacs = asoc->peer.peer_hmacs;
	if (!hmacs)
		return NULL;

	n_elt = (ntohs(hmacs->param_hdr.length) - sizeof(sctp_paramhdr_t)) >> 1;
	for (i = 0; i < n_elt; i++) {
		id = ntohs(hmacs->hmac_ids[i]);

		/* Check the id is in the supported range */
		if (id > SCTP_AUTH_HMAC_ID_MAX)
			continue;

		/* See is we support the id.  Supported IDs have name and
		 * length fields set, so that we can allocated and use
		 * them.  We can safely just check for name, for without the
		 * name, we can't allocate the TFM.
		 */
		if (!sctp_hmac_list[id].hmac_name)
			continue;

		break;
	}

	if (id == 0)
		return NULL;

	return &sctp_hmac_list[id];
}

static int __sctp_auth_find_hmacid(__be16 *hmacs, int n_elts, __be16 hmac_id)
{
	int  found = 0;
	int  i;

	for (i = 0; i < n_elts; i++) {
		if (hmac_id == hmacs[i]) {
			found = 1;
			break;
		}
	}

	return found;
}

/* See if the HMAC_ID is one that we claim as supported */
int sctp_auth_asoc_verify_hmac_id(const struct sctp_association *asoc,
				    __be16 hmac_id)
{
	struct sctp_hmac_algo_param *hmacs;
	__u16 n_elt;

	if (!asoc)
		return 0;

	hmacs = (struct sctp_hmac_algo_param *)asoc->c.auth_hmacs;
	n_elt = (ntohs(hmacs->param_hdr.length) - sizeof(sctp_paramhdr_t)) >> 1;

	return __sctp_auth_find_hmacid(hmacs->hmac_ids, n_elt, hmac_id);
}


/* Cache the default HMAC id.  This to follow this text from SCTP-AUTH:
 * Section 6.1:
 *   The receiver of a HMAC-ALGO parameter SHOULD use the first listed
 *   algorithm it supports.
 */
void sctp_auth_asoc_set_default_hmac(struct sctp_association *asoc,
				     struct sctp_hmac_algo_param *hmacs)
{
	struct sctp_endpoint *ep;
	__u16   id;
	int	i;
	int	n_params;

	/* if the default id is already set, use it */
	if (asoc->default_hmac_id)
		return;

	n_params = (ntohs(hmacs->param_hdr.length)
				- sizeof(sctp_paramhdr_t)) >> 1;
	ep = asoc->ep;
	for (i = 0; i < n_params; i++) {
		id = ntohs(hmacs->hmac_ids[i]);

		/* Check the id is in the supported range */
		if (id > SCTP_AUTH_HMAC_ID_MAX)
			continue;

		/* If this TFM has been allocated, use this id */
		if (ep->auth_hmacs[id]) {
			asoc->default_hmac_id = id;
			break;
		}
	}
}


/* Check to see if the given chunk is supposed to be authenticated */
static int __sctp_auth_cid(sctp_cid_t chunk, struct sctp_chunks_param *param)
{
	unsigned short len;
	int found = 0;
	int i;

	if (!param || param->param_hdr.length == 0)
		return 0;

	len = ntohs(param->param_hdr.length) - sizeof(sctp_paramhdr_t);

	/* SCTP-AUTH, Section 3.2
	 *    The chunk types for INIT, INIT-ACK, SHUTDOWN-COMPLETE and AUTH
	 *    chunks MUST NOT be listed in the CHUNKS parameter.  However, if
	 *    a CHUNKS parameter is received then the types for INIT, INIT-ACK,
	 *    SHUTDOWN-COMPLETE and AUTH chunks MUST be ignored.
	 */
	for (i = 0; !found && i < len; i++) {
		switch (param->chunks[i]) {
		    case SCTP_CID_INIT:
		    case SCTP_CID_INIT_ACK:
		    case SCTP_CID_SHUTDOWN_COMPLETE:
		    case SCTP_CID_AUTH:
			break;

		    default:
			if (param->chunks[i] == chunk)
			    found = 1;
			break;
		}
	}

	return found;
}

/* Check if peer requested that this chunk is authenticated */
int sctp_auth_send_cid(sctp_cid_t chunk, const struct sctp_association *asoc)
{
	if (!sctp_auth_enable || !asoc || !asoc->peer.auth_capable)
		return 0;

	return __sctp_auth_cid(chunk, asoc->peer.peer_chunks);
}

/* Check if we requested that peer authenticate this chunk. */
int sctp_auth_recv_cid(sctp_cid_t chunk, const struct sctp_association *asoc)
{
	if (!sctp_auth_enable || !asoc)
		return 0;

	return __sctp_auth_cid(chunk,
			      (struct sctp_chunks_param *)asoc->c.auth_chunks);
}

/* SCTP-AUTH: Section 6.2:
 *    The sender MUST calculate the MAC as described in RFC2104 [2] using
 *    the hash function H as described by the MAC Identifier and the shared
 *    association key K based on the endpoint pair shared key described by
 *    the shared key identifier.  The 'data' used for the computation of
 *    the AUTH-chunk is given by the AUTH chunk with its HMAC field set to
 *    zero (as shown in Figure 6) followed by all chunks that are placed
 *    after the AUTH chunk in the SCTP packet.
 */
void sctp_auth_calculate_hmac(const struct sctp_association *asoc,
			      struct sk_buff *skb,
			      struct sctp_auth_chunk *auth,
			      gfp_t gfp)
{
	struct scatterlist sg;
	struct hash_desc desc;
	struct sctp_auth_bytes *asoc_key;
	__u16 key_id, hmac_id;
	__u8 *digest;
	unsigned char *end;
	int free_key = 0;

	/* Extract the info we need:
	 * - hmac id
	 * - key id
	 */
	key_id = ntohs(auth->auth_hdr.shkey_id);
	hmac_id = ntohs(auth->auth_hdr.hmac_id);

	if (key_id == asoc->active_key_id)
		asoc_key = asoc->asoc_shared_key;
	else {
		struct sctp_shared_key *ep_key;

		ep_key = sctp_auth_get_shkey(asoc, key_id);
		if (!ep_key)
			return;

		asoc_key = sctp_auth_asoc_create_secret(asoc, ep_key, gfp);
		if (!asoc_key)
			return;

		free_key = 1;
	}

	/* set up scatter list */
	end = skb_tail_pointer(skb);
	sg_init_one(&sg, auth, end - (unsigned char *)auth);

	desc.tfm = asoc->ep->auth_hmacs[hmac_id];
	desc.flags = 0;

	digest = auth->auth_hdr.hmac;
	if (crypto_hash_setkey(desc.tfm, &asoc_key->data[0], asoc_key->len))
		goto free;

	crypto_hash_digest(&desc, &sg, sg.length, digest);

free:
	if (free_key)
		sctp_auth_key_put(asoc_key);
}

/* API Helpers */

/* Add a chunk to the endpoint authenticated chunk list */
int sctp_auth_ep_add_chunkid(struct sctp_endpoint *ep, __u8 chunk_id)
{
	struct sctp_chunks_param *p = ep->auth_chunk_list;
	__u16 nchunks;
	__u16 param_len;

	/* If this chunk is already specified, we are done */
	if (__sctp_auth_cid(chunk_id, p))
		return 0;

	/* Check if we can add this chunk to the array */
	param_len = ntohs(p->param_hdr.length);
	nchunks = param_len - sizeof(sctp_paramhdr_t);
	if (nchunks == SCTP_NUM_CHUNK_TYPES)
		return -EINVAL;

	p->chunks[nchunks] = chunk_id;
	p->param_hdr.length = htons(param_len + 1);
	return 0;
}

/* Add hmac identifires to the endpoint list of supported hmac ids */
int sctp_auth_ep_set_hmacs(struct sctp_endpoint *ep,
			   struct sctp_hmacalgo *hmacs)
{
	int has_sha1 = 0;
	__u16 id;
	int i;

	/* Scan the list looking for unsupported id.  Also make sure that
	 * SHA1 is specified.
	 */
	for (i = 0; i < hmacs->shmac_num_idents; i++) {
		id = hmacs->shmac_idents[i];

		if (id > SCTP_AUTH_HMAC_ID_MAX)
			return -EOPNOTSUPP;

		if (SCTP_AUTH_HMAC_ID_SHA1 == id)
			has_sha1 = 1;

		if (!sctp_hmac_list[id].hmac_name)
			return -EOPNOTSUPP;
	}

	if (!has_sha1)
		return -EINVAL;

	memcpy(ep->auth_hmacs_list->hmac_ids, &hmacs->shmac_idents[0],
		hmacs->shmac_num_idents * sizeof(__u16));
	ep->auth_hmacs_list->param_hdr.length = htons(sizeof(sctp_paramhdr_t) +
				hmacs->shmac_num_idents * sizeof(__u16));
	return 0;
}

/* Set a new shared key on either endpoint or association.  If the
 * the key with a same ID already exists, replace the key (remove the
 * old key and add a new one).
 */
int sctp_auth_set_key(struct sctp_endpoint *ep,
		      struct sctp_association *asoc,
		      struct sctp_authkey *auth_key)
{
	struct sctp_shared_key *cur_key = NULL;
	struct sctp_auth_bytes *key;
	struct list_head *sh_keys;
	int replace = 0;

	/* Try to find the given key id to see if
	 * we are doing a replace, or adding a new key
	 */
	if (asoc)
		sh_keys = &asoc->endpoint_shared_keys;
	else
		sh_keys = &ep->endpoint_shared_keys;

	key_for_each(cur_key, sh_keys) {
		if (cur_key->key_id == auth_key->sca_keynumber) {
			replace = 1;
			break;
		}
	}

	/* If we are not replacing a key id, we need to allocate
	 * a shared key.
	 */
	if (!replace) {
		cur_key = sctp_auth_shkey_create(auth_key->sca_keynumber,
						 GFP_KERNEL);
		if (!cur_key)
			return -ENOMEM;
	}

	/* Create a new key data based on the info passed in */
	key = sctp_auth_create_key(auth_key->sca_keylength, GFP_KERNEL);
	if (!key)
		goto nomem;

	memcpy(key->data, &auth_key->sca_key[0], auth_key->sca_keylength);

	/* If we are replacing, remove the old keys data from the
	 * key id.  If we are adding new key id, add it to the
	 * list.
	 */
	if (replace)
		sctp_auth_key_put(cur_key->key);
	else
		list_add(&cur_key->key_list, sh_keys);

	cur_key->key = key;
	sctp_auth_key_hold(key);

	return 0;
nomem:
	if (!replace)
		sctp_auth_shkey_free(cur_key);

	return -ENOMEM;
}

int sctp_auth_set_active_key(struct sctp_endpoint *ep,
			     struct sctp_association *asoc,
			     __u16  key_id)
{
	struct sctp_shared_key *key;
	struct list_head *sh_keys;
	int found = 0;

	/* The key identifier MUST correst to an existing key */
	if (asoc)
		sh_keys = &asoc->endpoint_shared_keys;
	else
		sh_keys = &ep->endpoint_shared_keys;

	key_for_each(key, sh_keys) {
		if (key->key_id == key_id) {
			found = 1;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	if (asoc) {
		asoc->active_key_id = key_id;
		sctp_auth_asoc_init_active_key(asoc, GFP_KERNEL);
	} else
		ep->active_key_id = key_id;

	return 0;
}

int sctp_auth_del_key_id(struct sctp_endpoint *ep,
			 struct sctp_association *asoc,
			 __u16  key_id)
{
	struct sctp_shared_key *key;
	struct list_head *sh_keys;
	int found = 0;

	/* The key identifier MUST NOT be the current active key
	 * The key identifier MUST correst to an existing key
	 */
	if (asoc) {
		if (asoc->active_key_id == key_id)
			return -EINVAL;

		sh_keys = &asoc->endpoint_shared_keys;
	} else {
		if (ep->active_key_id == key_id)
			return -EINVAL;

		sh_keys = &ep->endpoint_shared_keys;
	}

	key_for_each(key, sh_keys) {
		if (key->key_id == key_id) {
			found = 1;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	/* Delete the shared key */
	list_del_init(&key->key_list);
	sctp_auth_shkey_free(key);

	return 0;
}
