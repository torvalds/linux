/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <netinet/sctp_os.h>
#include <netinet/sctp.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_auth.h>

#ifdef SCTP_DEBUG
#define SCTP_AUTH_DEBUG		(SCTP_BASE_SYSCTL(sctp_debug_on) & SCTP_DEBUG_AUTH1)
#define SCTP_AUTH_DEBUG2	(SCTP_BASE_SYSCTL(sctp_debug_on) & SCTP_DEBUG_AUTH2)
#endif				/* SCTP_DEBUG */


void
sctp_clear_chunklist(sctp_auth_chklist_t *chklist)
{
	memset(chklist, 0, sizeof(*chklist));
	/* chklist->num_chunks = 0; */
}

sctp_auth_chklist_t *
sctp_alloc_chunklist(void)
{
	sctp_auth_chklist_t *chklist;

	SCTP_MALLOC(chklist, sctp_auth_chklist_t *, sizeof(*chklist),
	    SCTP_M_AUTH_CL);
	if (chklist == NULL) {
		SCTPDBG(SCTP_DEBUG_AUTH1, "sctp_alloc_chunklist: failed to get memory!\n");
	} else {
		sctp_clear_chunklist(chklist);
	}
	return (chklist);
}

void
sctp_free_chunklist(sctp_auth_chklist_t *list)
{
	if (list != NULL)
		SCTP_FREE(list, SCTP_M_AUTH_CL);
}

sctp_auth_chklist_t *
sctp_copy_chunklist(sctp_auth_chklist_t *list)
{
	sctp_auth_chklist_t *new_list;

	if (list == NULL)
		return (NULL);

	/* get a new list */
	new_list = sctp_alloc_chunklist();
	if (new_list == NULL)
		return (NULL);
	/* copy it */
	memcpy(new_list, list, sizeof(*new_list));

	return (new_list);
}


/*
 * add a chunk to the required chunks list
 */
int
sctp_auth_add_chunk(uint8_t chunk, sctp_auth_chklist_t *list)
{
	if (list == NULL)
		return (-1);

	/* is chunk restricted? */
	if ((chunk == SCTP_INITIATION) ||
	    (chunk == SCTP_INITIATION_ACK) ||
	    (chunk == SCTP_SHUTDOWN_COMPLETE) ||
	    (chunk == SCTP_AUTHENTICATION)) {
		return (-1);
	}
	if (list->chunks[chunk] == 0) {
		list->chunks[chunk] = 1;
		list->num_chunks++;
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP: added chunk %u (0x%02x) to Auth list\n",
		    chunk, chunk);
	}
	return (0);
}

/*
 * delete a chunk from the required chunks list
 */
int
sctp_auth_delete_chunk(uint8_t chunk, sctp_auth_chklist_t *list)
{
	if (list == NULL)
		return (-1);

	if (list->chunks[chunk] == 1) {
		list->chunks[chunk] = 0;
		list->num_chunks--;
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP: deleted chunk %u (0x%02x) from Auth list\n",
		    chunk, chunk);
	}
	return (0);
}

size_t
sctp_auth_get_chklist_size(const sctp_auth_chklist_t *list)
{
	if (list == NULL)
		return (0);
	else
		return (list->num_chunks);
}

/*
 * return the current number and list of required chunks caller must
 * guarantee ptr has space for up to 256 bytes
 */
int
sctp_serialize_auth_chunks(const sctp_auth_chklist_t *list, uint8_t *ptr)
{
	int i, count = 0;

	if (list == NULL)
		return (0);

	for (i = 0; i < 256; i++) {
		if (list->chunks[i] != 0) {
			*ptr++ = i;
			count++;
		}
	}
	return (count);
}

int
sctp_pack_auth_chunks(const sctp_auth_chklist_t *list, uint8_t *ptr)
{
	int i, size = 0;

	if (list == NULL)
		return (0);

	if (list->num_chunks <= 32) {
		/* just list them, one byte each */
		for (i = 0; i < 256; i++) {
			if (list->chunks[i] != 0) {
				*ptr++ = i;
				size++;
			}
		}
	} else {
		int index, offset;

		/* pack into a 32 byte bitfield */
		for (i = 0; i < 256; i++) {
			if (list->chunks[i] != 0) {
				index = i / 8;
				offset = i % 8;
				ptr[index] |= (1 << offset);
			}
		}
		size = 32;
	}
	return (size);
}

int
sctp_unpack_auth_chunks(const uint8_t *ptr, uint8_t num_chunks,
    sctp_auth_chklist_t *list)
{
	int i;
	int size;

	if (list == NULL)
		return (0);

	if (num_chunks <= 32) {
		/* just pull them, one byte each */
		for (i = 0; i < num_chunks; i++) {
			(void)sctp_auth_add_chunk(*ptr++, list);
		}
		size = num_chunks;
	} else {
		int index, offset;

		/* unpack from a 32 byte bitfield */
		for (index = 0; index < 32; index++) {
			for (offset = 0; offset < 8; offset++) {
				if (ptr[index] & (1 << offset)) {
					(void)sctp_auth_add_chunk((index * 8) + offset, list);
				}
			}
		}
		size = 32;
	}
	return (size);
}


/*
 * allocate structure space for a key of length keylen
 */
sctp_key_t *
sctp_alloc_key(uint32_t keylen)
{
	sctp_key_t *new_key;

	SCTP_MALLOC(new_key, sctp_key_t *, sizeof(*new_key) + keylen,
	    SCTP_M_AUTH_KY);
	if (new_key == NULL) {
		/* out of memory */
		return (NULL);
	}
	new_key->keylen = keylen;
	return (new_key);
}

void
sctp_free_key(sctp_key_t *key)
{
	if (key != NULL)
		SCTP_FREE(key, SCTP_M_AUTH_KY);
}

void
sctp_print_key(sctp_key_t *key, const char *str)
{
	uint32_t i;

	if (key == NULL) {
		SCTP_PRINTF("%s: [Null key]\n", str);
		return;
	}
	SCTP_PRINTF("%s: len %u, ", str, key->keylen);
	if (key->keylen) {
		for (i = 0; i < key->keylen; i++)
			SCTP_PRINTF("%02x", key->key[i]);
		SCTP_PRINTF("\n");
	} else {
		SCTP_PRINTF("[Null key]\n");
	}
}

void
sctp_show_key(sctp_key_t *key, const char *str)
{
	uint32_t i;

	if (key == NULL) {
		SCTP_PRINTF("%s: [Null key]\n", str);
		return;
	}
	SCTP_PRINTF("%s: len %u, ", str, key->keylen);
	if (key->keylen) {
		for (i = 0; i < key->keylen; i++)
			SCTP_PRINTF("%02x", key->key[i]);
		SCTP_PRINTF("\n");
	} else {
		SCTP_PRINTF("[Null key]\n");
	}
}

static uint32_t
sctp_get_keylen(sctp_key_t *key)
{
	if (key != NULL)
		return (key->keylen);
	else
		return (0);
}

/*
 * generate a new random key of length 'keylen'
 */
sctp_key_t *
sctp_generate_random_key(uint32_t keylen)
{
	sctp_key_t *new_key;

	new_key = sctp_alloc_key(keylen);
	if (new_key == NULL) {
		/* out of memory */
		return (NULL);
	}
	SCTP_READ_RANDOM(new_key->key, keylen);
	new_key->keylen = keylen;
	return (new_key);
}

sctp_key_t *
sctp_set_key(uint8_t *key, uint32_t keylen)
{
	sctp_key_t *new_key;

	new_key = sctp_alloc_key(keylen);
	if (new_key == NULL) {
		/* out of memory */
		return (NULL);
	}
	memcpy(new_key->key, key, keylen);
	return (new_key);
}

/*-
 * given two keys of variable size, compute which key is "larger/smaller"
 * returns:  1 if key1 > key2
 *          -1 if key1 < key2
 *           0 if key1 = key2
 */
static int
sctp_compare_key(sctp_key_t *key1, sctp_key_t *key2)
{
	uint32_t maxlen;
	uint32_t i;
	uint32_t key1len, key2len;
	uint8_t *key_1, *key_2;
	uint8_t val1, val2;

	/* sanity/length check */
	key1len = sctp_get_keylen(key1);
	key2len = sctp_get_keylen(key2);
	if ((key1len == 0) && (key2len == 0))
		return (0);
	else if (key1len == 0)
		return (-1);
	else if (key2len == 0)
		return (1);

	if (key1len < key2len) {
		maxlen = key2len;
	} else {
		maxlen = key1len;
	}
	key_1 = key1->key;
	key_2 = key2->key;
	/* check for numeric equality */
	for (i = 0; i < maxlen; i++) {
		/* left-pad with zeros */
		val1 = (i < (maxlen - key1len)) ? 0 : *(key_1++);
		val2 = (i < (maxlen - key2len)) ? 0 : *(key_2++);
		if (val1 > val2) {
			return (1);
		} else if (val1 < val2) {
			return (-1);
		}
	}
	/* keys are equal value, so check lengths */
	if (key1len == key2len)
		return (0);
	else if (key1len < key2len)
		return (-1);
	else
		return (1);
}

/*
 * generate the concatenated keying material based on the two keys and the
 * shared key (if available). draft-ietf-tsvwg-auth specifies the specific
 * order for concatenation
 */
sctp_key_t *
sctp_compute_hashkey(sctp_key_t *key1, sctp_key_t *key2, sctp_key_t *shared)
{
	uint32_t keylen;
	sctp_key_t *new_key;
	uint8_t *key_ptr;

	keylen = sctp_get_keylen(key1) + sctp_get_keylen(key2) +
	    sctp_get_keylen(shared);

	if (keylen > 0) {
		/* get space for the new key */
		new_key = sctp_alloc_key(keylen);
		if (new_key == NULL) {
			/* out of memory */
			return (NULL);
		}
		new_key->keylen = keylen;
		key_ptr = new_key->key;
	} else {
		/* all keys empty/null?! */
		return (NULL);
	}

	/* concatenate the keys */
	if (sctp_compare_key(key1, key2) <= 0) {
		/* key is shared + key1 + key2 */
		if (sctp_get_keylen(shared)) {
			memcpy(key_ptr, shared->key, shared->keylen);
			key_ptr += shared->keylen;
		}
		if (sctp_get_keylen(key1)) {
			memcpy(key_ptr, key1->key, key1->keylen);
			key_ptr += key1->keylen;
		}
		if (sctp_get_keylen(key2)) {
			memcpy(key_ptr, key2->key, key2->keylen);
		}
	} else {
		/* key is shared + key2 + key1 */
		if (sctp_get_keylen(shared)) {
			memcpy(key_ptr, shared->key, shared->keylen);
			key_ptr += shared->keylen;
		}
		if (sctp_get_keylen(key2)) {
			memcpy(key_ptr, key2->key, key2->keylen);
			key_ptr += key2->keylen;
		}
		if (sctp_get_keylen(key1)) {
			memcpy(key_ptr, key1->key, key1->keylen);
		}
	}
	return (new_key);
}


sctp_sharedkey_t *
sctp_alloc_sharedkey(void)
{
	sctp_sharedkey_t *new_key;

	SCTP_MALLOC(new_key, sctp_sharedkey_t *, sizeof(*new_key),
	    SCTP_M_AUTH_KY);
	if (new_key == NULL) {
		/* out of memory */
		return (NULL);
	}
	new_key->keyid = 0;
	new_key->key = NULL;
	new_key->refcount = 1;
	new_key->deactivated = 0;
	return (new_key);
}

void
sctp_free_sharedkey(sctp_sharedkey_t *skey)
{
	if (skey == NULL)
		return;

	if (SCTP_DECREMENT_AND_CHECK_REFCOUNT(&skey->refcount)) {
		if (skey->key != NULL)
			sctp_free_key(skey->key);
		SCTP_FREE(skey, SCTP_M_AUTH_KY);
	}
}

sctp_sharedkey_t *
sctp_find_sharedkey(struct sctp_keyhead *shared_keys, uint16_t key_id)
{
	sctp_sharedkey_t *skey;

	LIST_FOREACH(skey, shared_keys, next) {
		if (skey->keyid == key_id)
			return (skey);
	}
	return (NULL);
}

int
sctp_insert_sharedkey(struct sctp_keyhead *shared_keys,
    sctp_sharedkey_t *new_skey)
{
	sctp_sharedkey_t *skey;

	if ((shared_keys == NULL) || (new_skey == NULL))
		return (EINVAL);

	/* insert into an empty list? */
	if (LIST_EMPTY(shared_keys)) {
		LIST_INSERT_HEAD(shared_keys, new_skey, next);
		return (0);
	}
	/* insert into the existing list, ordered by key id */
	LIST_FOREACH(skey, shared_keys, next) {
		if (new_skey->keyid < skey->keyid) {
			/* insert it before here */
			LIST_INSERT_BEFORE(skey, new_skey, next);
			return (0);
		} else if (new_skey->keyid == skey->keyid) {
			/* replace the existing key */
			/* verify this key *can* be replaced */
			if ((skey->deactivated) && (skey->refcount > 1)) {
				SCTPDBG(SCTP_DEBUG_AUTH1,
				    "can't replace shared key id %u\n",
				    new_skey->keyid);
				return (EBUSY);
			}
			SCTPDBG(SCTP_DEBUG_AUTH1,
			    "replacing shared key id %u\n",
			    new_skey->keyid);
			LIST_INSERT_BEFORE(skey, new_skey, next);
			LIST_REMOVE(skey, next);
			sctp_free_sharedkey(skey);
			return (0);
		}
		if (LIST_NEXT(skey, next) == NULL) {
			/* belongs at the end of the list */
			LIST_INSERT_AFTER(skey, new_skey, next);
			return (0);
		}
	}
	/* shouldn't reach here */
	return (EINVAL);
}

void
sctp_auth_key_acquire(struct sctp_tcb *stcb, uint16_t key_id)
{
	sctp_sharedkey_t *skey;

	/* find the shared key */
	skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, key_id);

	/* bump the ref count */
	if (skey) {
		atomic_add_int(&skey->refcount, 1);
		SCTPDBG(SCTP_DEBUG_AUTH2,
		    "%s: stcb %p key %u refcount acquire to %d\n",
		    __func__, (void *)stcb, key_id, skey->refcount);
	}
}

void
sctp_auth_key_release(struct sctp_tcb *stcb, uint16_t key_id, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	sctp_sharedkey_t *skey;

	/* find the shared key */
	skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, key_id);

	/* decrement the ref count */
	if (skey) {
		SCTPDBG(SCTP_DEBUG_AUTH2,
		    "%s: stcb %p key %u refcount release to %d\n",
		    __func__, (void *)stcb, key_id, skey->refcount);

		/* see if a notification should be generated */
		if ((skey->refcount <= 2) && (skey->deactivated)) {
			/* notify ULP that key is no longer used */
			sctp_ulp_notify(SCTP_NOTIFY_AUTH_FREE_KEY, stcb,
			    key_id, 0, so_locked);
			SCTPDBG(SCTP_DEBUG_AUTH2,
			    "%s: stcb %p key %u no longer used, %d\n",
			    __func__, (void *)stcb, key_id, skey->refcount);
		}
		sctp_free_sharedkey(skey);
	}
}

static sctp_sharedkey_t *
sctp_copy_sharedkey(const sctp_sharedkey_t *skey)
{
	sctp_sharedkey_t *new_skey;

	if (skey == NULL)
		return (NULL);
	new_skey = sctp_alloc_sharedkey();
	if (new_skey == NULL)
		return (NULL);
	if (skey->key != NULL)
		new_skey->key = sctp_set_key(skey->key->key, skey->key->keylen);
	else
		new_skey->key = NULL;
	new_skey->keyid = skey->keyid;
	return (new_skey);
}

int
sctp_copy_skeylist(const struct sctp_keyhead *src, struct sctp_keyhead *dest)
{
	sctp_sharedkey_t *skey, *new_skey;
	int count = 0;

	if ((src == NULL) || (dest == NULL))
		return (0);
	LIST_FOREACH(skey, src, next) {
		new_skey = sctp_copy_sharedkey(skey);
		if (new_skey != NULL) {
			if (sctp_insert_sharedkey(dest, new_skey)) {
				sctp_free_sharedkey(new_skey);
			} else {
				count++;
			}
		}
	}
	return (count);
}


sctp_hmaclist_t *
sctp_alloc_hmaclist(uint16_t num_hmacs)
{
	sctp_hmaclist_t *new_list;
	int alloc_size;

	alloc_size = sizeof(*new_list) + num_hmacs * sizeof(new_list->hmac[0]);
	SCTP_MALLOC(new_list, sctp_hmaclist_t *, alloc_size,
	    SCTP_M_AUTH_HL);
	if (new_list == NULL) {
		/* out of memory */
		return (NULL);
	}
	new_list->max_algo = num_hmacs;
	new_list->num_algo = 0;
	return (new_list);
}

void
sctp_free_hmaclist(sctp_hmaclist_t *list)
{
	if (list != NULL) {
		SCTP_FREE(list, SCTP_M_AUTH_HL);
		list = NULL;
	}
}

int
sctp_auth_add_hmacid(sctp_hmaclist_t *list, uint16_t hmac_id)
{
	int i;

	if (list == NULL)
		return (-1);
	if (list->num_algo == list->max_algo) {
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP: HMAC id list full, ignoring add %u\n", hmac_id);
		return (-1);
	}
	if ((hmac_id != SCTP_AUTH_HMAC_ID_SHA1) &&
	    (hmac_id != SCTP_AUTH_HMAC_ID_SHA256)) {
		return (-1);
	}
	/* Now is it already in the list */
	for (i = 0; i < list->num_algo; i++) {
		if (list->hmac[i] == hmac_id) {
			/* already in list */
			return (-1);
		}
	}
	SCTPDBG(SCTP_DEBUG_AUTH1, "SCTP: add HMAC id %u to list\n", hmac_id);
	list->hmac[list->num_algo++] = hmac_id;
	return (0);
}

sctp_hmaclist_t *
sctp_copy_hmaclist(sctp_hmaclist_t *list)
{
	sctp_hmaclist_t *new_list;
	int i;

	if (list == NULL)
		return (NULL);
	/* get a new list */
	new_list = sctp_alloc_hmaclist(list->max_algo);
	if (new_list == NULL)
		return (NULL);
	/* copy it */
	new_list->max_algo = list->max_algo;
	new_list->num_algo = list->num_algo;
	for (i = 0; i < list->num_algo; i++)
		new_list->hmac[i] = list->hmac[i];
	return (new_list);
}

sctp_hmaclist_t *
sctp_default_supported_hmaclist(void)
{
	sctp_hmaclist_t *new_list;

	new_list = sctp_alloc_hmaclist(2);
	if (new_list == NULL)
		return (NULL);
	/* We prefer SHA256, so list it first */
	(void)sctp_auth_add_hmacid(new_list, SCTP_AUTH_HMAC_ID_SHA256);
	(void)sctp_auth_add_hmacid(new_list, SCTP_AUTH_HMAC_ID_SHA1);
	return (new_list);
}

/*-
 * HMAC algos are listed in priority/preference order
 * find the best HMAC id to use for the peer based on local support
 */
uint16_t
sctp_negotiate_hmacid(sctp_hmaclist_t *peer, sctp_hmaclist_t *local)
{
	int i, j;

	if ((local == NULL) || (peer == NULL))
		return (SCTP_AUTH_HMAC_ID_RSVD);

	for (i = 0; i < peer->num_algo; i++) {
		for (j = 0; j < local->num_algo; j++) {
			if (peer->hmac[i] == local->hmac[j]) {
				/* found the "best" one */
				SCTPDBG(SCTP_DEBUG_AUTH1,
				    "SCTP: negotiated peer HMAC id %u\n",
				    peer->hmac[i]);
				return (peer->hmac[i]);
			}
		}
	}
	/* didn't find one! */
	return (SCTP_AUTH_HMAC_ID_RSVD);
}

/*-
 * serialize the HMAC algo list and return space used
 * caller must guarantee ptr has appropriate space
 */
int
sctp_serialize_hmaclist(sctp_hmaclist_t *list, uint8_t *ptr)
{
	int i;
	uint16_t hmac_id;

	if (list == NULL)
		return (0);

	for (i = 0; i < list->num_algo; i++) {
		hmac_id = htons(list->hmac[i]);
		memcpy(ptr, &hmac_id, sizeof(hmac_id));
		ptr += sizeof(hmac_id);
	}
	return (list->num_algo * sizeof(hmac_id));
}

int
sctp_verify_hmac_param(struct sctp_auth_hmac_algo *hmacs, uint32_t num_hmacs)
{
	uint32_t i;

	for (i = 0; i < num_hmacs; i++) {
		if (ntohs(hmacs->hmac_ids[i]) == SCTP_AUTH_HMAC_ID_SHA1) {
			return (0);
		}
	}
	return (-1);
}

sctp_authinfo_t *
sctp_alloc_authinfo(void)
{
	sctp_authinfo_t *new_authinfo;

	SCTP_MALLOC(new_authinfo, sctp_authinfo_t *, sizeof(*new_authinfo),
	    SCTP_M_AUTH_IF);

	if (new_authinfo == NULL) {
		/* out of memory */
		return (NULL);
	}
	memset(new_authinfo, 0, sizeof(*new_authinfo));
	return (new_authinfo);
}

void
sctp_free_authinfo(sctp_authinfo_t *authinfo)
{
	if (authinfo == NULL)
		return;

	if (authinfo->random != NULL)
		sctp_free_key(authinfo->random);
	if (authinfo->peer_random != NULL)
		sctp_free_key(authinfo->peer_random);
	if (authinfo->assoc_key != NULL)
		sctp_free_key(authinfo->assoc_key);
	if (authinfo->recv_key != NULL)
		sctp_free_key(authinfo->recv_key);

	/* We are NOT dynamically allocating authinfo's right now... */
	/* SCTP_FREE(authinfo, SCTP_M_AUTH_??); */
}


uint32_t
sctp_get_auth_chunk_len(uint16_t hmac_algo)
{
	int size;

	size = sizeof(struct sctp_auth_chunk) + sctp_get_hmac_digest_len(hmac_algo);
	return (SCTP_SIZE32(size));
}

uint32_t
sctp_get_hmac_digest_len(uint16_t hmac_algo)
{
	switch (hmac_algo) {
	case SCTP_AUTH_HMAC_ID_SHA1:
		return (SCTP_AUTH_DIGEST_LEN_SHA1);
	case SCTP_AUTH_HMAC_ID_SHA256:
		return (SCTP_AUTH_DIGEST_LEN_SHA256);
	default:
		/* unknown HMAC algorithm: can't do anything */
		return (0);
	}			/* end switch */
}

static inline int
sctp_get_hmac_block_len(uint16_t hmac_algo)
{
	switch (hmac_algo) {
	case SCTP_AUTH_HMAC_ID_SHA1:
		return (64);
	case SCTP_AUTH_HMAC_ID_SHA256:
		return (64);
	case SCTP_AUTH_HMAC_ID_RSVD:
	default:
		/* unknown HMAC algorithm: can't do anything */
		return (0);
	}			/* end switch */
}

static void
sctp_hmac_init(uint16_t hmac_algo, sctp_hash_context_t *ctx)
{
	switch (hmac_algo) {
	case SCTP_AUTH_HMAC_ID_SHA1:
		SCTP_SHA1_INIT(&ctx->sha1);
		break;
	case SCTP_AUTH_HMAC_ID_SHA256:
		SCTP_SHA256_INIT(&ctx->sha256);
		break;
	case SCTP_AUTH_HMAC_ID_RSVD:
	default:
		/* unknown HMAC algorithm: can't do anything */
		return;
	}			/* end switch */
}

static void
sctp_hmac_update(uint16_t hmac_algo, sctp_hash_context_t *ctx,
    uint8_t *text, uint32_t textlen)
{
	switch (hmac_algo) {
	case SCTP_AUTH_HMAC_ID_SHA1:
		SCTP_SHA1_UPDATE(&ctx->sha1, text, textlen);
		break;
	case SCTP_AUTH_HMAC_ID_SHA256:
		SCTP_SHA256_UPDATE(&ctx->sha256, text, textlen);
		break;
	case SCTP_AUTH_HMAC_ID_RSVD:
	default:
		/* unknown HMAC algorithm: can't do anything */
		return;
	}			/* end switch */
}

static void
sctp_hmac_final(uint16_t hmac_algo, sctp_hash_context_t *ctx,
    uint8_t *digest)
{
	switch (hmac_algo) {
	case SCTP_AUTH_HMAC_ID_SHA1:
		SCTP_SHA1_FINAL(digest, &ctx->sha1);
		break;
	case SCTP_AUTH_HMAC_ID_SHA256:
		SCTP_SHA256_FINAL(digest, &ctx->sha256);
		break;
	case SCTP_AUTH_HMAC_ID_RSVD:
	default:
		/* unknown HMAC algorithm: can't do anything */
		return;
	}			/* end switch */
}

/*-
 * Keyed-Hashing for Message Authentication: FIPS 198 (RFC 2104)
 *
 * Compute the HMAC digest using the desired hash key, text, and HMAC
 * algorithm.  Resulting digest is placed in 'digest' and digest length
 * is returned, if the HMAC was performed.
 *
 * WARNING: it is up to the caller to supply sufficient space to hold the
 * resultant digest.
 */
uint32_t
sctp_hmac(uint16_t hmac_algo, uint8_t *key, uint32_t keylen,
    uint8_t *text, uint32_t textlen, uint8_t *digest)
{
	uint32_t digestlen;
	uint32_t blocklen;
	sctp_hash_context_t ctx;
	uint8_t ipad[128], opad[128];	/* keyed hash inner/outer pads */
	uint8_t temp[SCTP_AUTH_DIGEST_LEN_MAX];
	uint32_t i;

	/* sanity check the material and length */
	if ((key == NULL) || (keylen == 0) || (text == NULL) ||
	    (textlen == 0) || (digest == NULL)) {
		/* can't do HMAC with empty key or text or digest store */
		return (0);
	}
	/* validate the hmac algo and get the digest length */
	digestlen = sctp_get_hmac_digest_len(hmac_algo);
	if (digestlen == 0)
		return (0);

	/* hash the key if it is longer than the hash block size */
	blocklen = sctp_get_hmac_block_len(hmac_algo);
	if (keylen > blocklen) {
		sctp_hmac_init(hmac_algo, &ctx);
		sctp_hmac_update(hmac_algo, &ctx, key, keylen);
		sctp_hmac_final(hmac_algo, &ctx, temp);
		/* set the hashed key as the key */
		keylen = digestlen;
		key = temp;
	}
	/* initialize the inner/outer pads with the key and "append" zeroes */
	memset(ipad, 0, blocklen);
	memset(opad, 0, blocklen);
	memcpy(ipad, key, keylen);
	memcpy(opad, key, keylen);

	/* XOR the key with ipad and opad values */
	for (i = 0; i < blocklen; i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}

	/* perform inner hash */
	sctp_hmac_init(hmac_algo, &ctx);
	sctp_hmac_update(hmac_algo, &ctx, ipad, blocklen);
	sctp_hmac_update(hmac_algo, &ctx, text, textlen);
	sctp_hmac_final(hmac_algo, &ctx, temp);

	/* perform outer hash */
	sctp_hmac_init(hmac_algo, &ctx);
	sctp_hmac_update(hmac_algo, &ctx, opad, blocklen);
	sctp_hmac_update(hmac_algo, &ctx, temp, digestlen);
	sctp_hmac_final(hmac_algo, &ctx, digest);

	return (digestlen);
}

/* mbuf version */
uint32_t
sctp_hmac_m(uint16_t hmac_algo, uint8_t *key, uint32_t keylen,
    struct mbuf *m, uint32_t m_offset, uint8_t *digest, uint32_t trailer)
{
	uint32_t digestlen;
	uint32_t blocklen;
	sctp_hash_context_t ctx;
	uint8_t ipad[128], opad[128];	/* keyed hash inner/outer pads */
	uint8_t temp[SCTP_AUTH_DIGEST_LEN_MAX];
	uint32_t i;
	struct mbuf *m_tmp;

	/* sanity check the material and length */
	if ((key == NULL) || (keylen == 0) || (m == NULL) || (digest == NULL)) {
		/* can't do HMAC with empty key or text or digest store */
		return (0);
	}
	/* validate the hmac algo and get the digest length */
	digestlen = sctp_get_hmac_digest_len(hmac_algo);
	if (digestlen == 0)
		return (0);

	/* hash the key if it is longer than the hash block size */
	blocklen = sctp_get_hmac_block_len(hmac_algo);
	if (keylen > blocklen) {
		sctp_hmac_init(hmac_algo, &ctx);
		sctp_hmac_update(hmac_algo, &ctx, key, keylen);
		sctp_hmac_final(hmac_algo, &ctx, temp);
		/* set the hashed key as the key */
		keylen = digestlen;
		key = temp;
	}
	/* initialize the inner/outer pads with the key and "append" zeroes */
	memset(ipad, 0, blocklen);
	memset(opad, 0, blocklen);
	memcpy(ipad, key, keylen);
	memcpy(opad, key, keylen);

	/* XOR the key with ipad and opad values */
	for (i = 0; i < blocklen; i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}

	/* perform inner hash */
	sctp_hmac_init(hmac_algo, &ctx);
	sctp_hmac_update(hmac_algo, &ctx, ipad, blocklen);
	/* find the correct starting mbuf and offset (get start of text) */
	m_tmp = m;
	while ((m_tmp != NULL) && (m_offset >= (uint32_t)SCTP_BUF_LEN(m_tmp))) {
		m_offset -= SCTP_BUF_LEN(m_tmp);
		m_tmp = SCTP_BUF_NEXT(m_tmp);
	}
	/* now use the rest of the mbuf chain for the text */
	while (m_tmp != NULL) {
		if ((SCTP_BUF_NEXT(m_tmp) == NULL) && trailer) {
			sctp_hmac_update(hmac_algo, &ctx, mtod(m_tmp, uint8_t *)+m_offset,
			    SCTP_BUF_LEN(m_tmp) - (trailer + m_offset));
		} else {
			sctp_hmac_update(hmac_algo, &ctx, mtod(m_tmp, uint8_t *)+m_offset,
			    SCTP_BUF_LEN(m_tmp) - m_offset);
		}

		/* clear the offset since it's only for the first mbuf */
		m_offset = 0;
		m_tmp = SCTP_BUF_NEXT(m_tmp);
	}
	sctp_hmac_final(hmac_algo, &ctx, temp);

	/* perform outer hash */
	sctp_hmac_init(hmac_algo, &ctx);
	sctp_hmac_update(hmac_algo, &ctx, opad, blocklen);
	sctp_hmac_update(hmac_algo, &ctx, temp, digestlen);
	sctp_hmac_final(hmac_algo, &ctx, digest);

	return (digestlen);
}

/*
 * computes the requested HMAC using a key struct (which may be modified if
 * the keylen exceeds the HMAC block len).
 */
uint32_t
sctp_compute_hmac(uint16_t hmac_algo, sctp_key_t *key, uint8_t *text,
    uint32_t textlen, uint8_t *digest)
{
	uint32_t digestlen;
	uint32_t blocklen;
	sctp_hash_context_t ctx;
	uint8_t temp[SCTP_AUTH_DIGEST_LEN_MAX];

	/* sanity check */
	if ((key == NULL) || (text == NULL) || (textlen == 0) ||
	    (digest == NULL)) {
		/* can't do HMAC with empty key or text or digest store */
		return (0);
	}
	/* validate the hmac algo and get the digest length */
	digestlen = sctp_get_hmac_digest_len(hmac_algo);
	if (digestlen == 0)
		return (0);

	/* hash the key if it is longer than the hash block size */
	blocklen = sctp_get_hmac_block_len(hmac_algo);
	if (key->keylen > blocklen) {
		sctp_hmac_init(hmac_algo, &ctx);
		sctp_hmac_update(hmac_algo, &ctx, key->key, key->keylen);
		sctp_hmac_final(hmac_algo, &ctx, temp);
		/* save the hashed key as the new key */
		key->keylen = digestlen;
		memcpy(key->key, temp, key->keylen);
	}
	return (sctp_hmac(hmac_algo, key->key, key->keylen, text, textlen,
	    digest));
}

/* mbuf version */
uint32_t
sctp_compute_hmac_m(uint16_t hmac_algo, sctp_key_t *key, struct mbuf *m,
    uint32_t m_offset, uint8_t *digest)
{
	uint32_t digestlen;
	uint32_t blocklen;
	sctp_hash_context_t ctx;
	uint8_t temp[SCTP_AUTH_DIGEST_LEN_MAX];

	/* sanity check */
	if ((key == NULL) || (m == NULL) || (digest == NULL)) {
		/* can't do HMAC with empty key or text or digest store */
		return (0);
	}
	/* validate the hmac algo and get the digest length */
	digestlen = sctp_get_hmac_digest_len(hmac_algo);
	if (digestlen == 0)
		return (0);

	/* hash the key if it is longer than the hash block size */
	blocklen = sctp_get_hmac_block_len(hmac_algo);
	if (key->keylen > blocklen) {
		sctp_hmac_init(hmac_algo, &ctx);
		sctp_hmac_update(hmac_algo, &ctx, key->key, key->keylen);
		sctp_hmac_final(hmac_algo, &ctx, temp);
		/* save the hashed key as the new key */
		key->keylen = digestlen;
		memcpy(key->key, temp, key->keylen);
	}
	return (sctp_hmac_m(hmac_algo, key->key, key->keylen, m, m_offset, digest, 0));
}

int
sctp_auth_is_supported_hmac(sctp_hmaclist_t *list, uint16_t id)
{
	int i;

	if ((list == NULL) || (id == SCTP_AUTH_HMAC_ID_RSVD))
		return (0);

	for (i = 0; i < list->num_algo; i++)
		if (list->hmac[i] == id)
			return (1);

	/* not in the list */
	return (0);
}


/*-
 * clear any cached key(s) if they match the given key id on an association.
 * the cached key(s) will be recomputed and re-cached at next use.
 * ASSUMES TCB_LOCK is already held
 */
void
sctp_clear_cachedkeys(struct sctp_tcb *stcb, uint16_t keyid)
{
	if (stcb == NULL)
		return;

	if (keyid == stcb->asoc.authinfo.assoc_keyid) {
		sctp_free_key(stcb->asoc.authinfo.assoc_key);
		stcb->asoc.authinfo.assoc_key = NULL;
	}
	if (keyid == stcb->asoc.authinfo.recv_keyid) {
		sctp_free_key(stcb->asoc.authinfo.recv_key);
		stcb->asoc.authinfo.recv_key = NULL;
	}
}

/*-
 * clear any cached key(s) if they match the given key id for all assocs on
 * an endpoint.
 * ASSUMES INP_WLOCK is already held
 */
void
sctp_clear_cachedkeys_ep(struct sctp_inpcb *inp, uint16_t keyid)
{
	struct sctp_tcb *stcb;

	if (inp == NULL)
		return;

	/* clear the cached keys on all assocs on this instance */
	LIST_FOREACH(stcb, &inp->sctp_asoc_list, sctp_tcblist) {
		SCTP_TCB_LOCK(stcb);
		sctp_clear_cachedkeys(stcb, keyid);
		SCTP_TCB_UNLOCK(stcb);
	}
}

/*-
 * delete a shared key from an association
 * ASSUMES TCB_LOCK is already held
 */
int
sctp_delete_sharedkey(struct sctp_tcb *stcb, uint16_t keyid)
{
	sctp_sharedkey_t *skey;

	if (stcb == NULL)
		return (-1);

	/* is the keyid the assoc active sending key */
	if (keyid == stcb->asoc.authinfo.active_keyid)
		return (-1);

	/* does the key exist? */
	skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, keyid);
	if (skey == NULL)
		return (-1);

	/* are there other refcount holders on the key? */
	if (skey->refcount > 1)
		return (-1);

	/* remove it */
	LIST_REMOVE(skey, next);
	sctp_free_sharedkey(skey);	/* frees skey->key as well */

	/* clear any cached keys */
	sctp_clear_cachedkeys(stcb, keyid);
	return (0);
}

/*-
 * deletes a shared key from the endpoint
 * ASSUMES INP_WLOCK is already held
 */
int
sctp_delete_sharedkey_ep(struct sctp_inpcb *inp, uint16_t keyid)
{
	sctp_sharedkey_t *skey;

	if (inp == NULL)
		return (-1);

	/* is the keyid the active sending key on the endpoint */
	if (keyid == inp->sctp_ep.default_keyid)
		return (-1);

	/* does the key exist? */
	skey = sctp_find_sharedkey(&inp->sctp_ep.shared_keys, keyid);
	if (skey == NULL)
		return (-1);

	/* endpoint keys are not refcounted */

	/* remove it */
	LIST_REMOVE(skey, next);
	sctp_free_sharedkey(skey);	/* frees skey->key as well */

	/* clear any cached keys */
	sctp_clear_cachedkeys_ep(inp, keyid);
	return (0);
}

/*-
 * set the active key on an association
 * ASSUMES TCB_LOCK is already held
 */
int
sctp_auth_setactivekey(struct sctp_tcb *stcb, uint16_t keyid)
{
	sctp_sharedkey_t *skey = NULL;

	/* find the key on the assoc */
	skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, keyid);
	if (skey == NULL) {
		/* that key doesn't exist */
		return (-1);
	}
	if ((skey->deactivated) && (skey->refcount > 1)) {
		/* can't reactivate a deactivated key with other refcounts */
		return (-1);
	}

	/* set the (new) active key */
	stcb->asoc.authinfo.active_keyid = keyid;
	/* reset the deactivated flag */
	skey->deactivated = 0;

	return (0);
}

/*-
 * set the active key on an endpoint
 * ASSUMES INP_WLOCK is already held
 */
int
sctp_auth_setactivekey_ep(struct sctp_inpcb *inp, uint16_t keyid)
{
	sctp_sharedkey_t *skey;

	/* find the key */
	skey = sctp_find_sharedkey(&inp->sctp_ep.shared_keys, keyid);
	if (skey == NULL) {
		/* that key doesn't exist */
		return (-1);
	}
	inp->sctp_ep.default_keyid = keyid;
	return (0);
}

/*-
 * deactivates a shared key from the association
 * ASSUMES INP_WLOCK is already held
 */
int
sctp_deact_sharedkey(struct sctp_tcb *stcb, uint16_t keyid)
{
	sctp_sharedkey_t *skey;

	if (stcb == NULL)
		return (-1);

	/* is the keyid the assoc active sending key */
	if (keyid == stcb->asoc.authinfo.active_keyid)
		return (-1);

	/* does the key exist? */
	skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, keyid);
	if (skey == NULL)
		return (-1);

	/* are there other refcount holders on the key? */
	if (skey->refcount == 1) {
		/* no other users, send a notification for this key */
		sctp_ulp_notify(SCTP_NOTIFY_AUTH_FREE_KEY, stcb, keyid, 0,
		    SCTP_SO_LOCKED);
	}

	/* mark the key as deactivated */
	skey->deactivated = 1;

	return (0);
}

/*-
 * deactivates a shared key from the endpoint
 * ASSUMES INP_WLOCK is already held
 */
int
sctp_deact_sharedkey_ep(struct sctp_inpcb *inp, uint16_t keyid)
{
	sctp_sharedkey_t *skey;

	if (inp == NULL)
		return (-1);

	/* is the keyid the active sending key on the endpoint */
	if (keyid == inp->sctp_ep.default_keyid)
		return (-1);

	/* does the key exist? */
	skey = sctp_find_sharedkey(&inp->sctp_ep.shared_keys, keyid);
	if (skey == NULL)
		return (-1);

	/* endpoint keys are not refcounted */

	/* remove it */
	LIST_REMOVE(skey, next);
	sctp_free_sharedkey(skey);	/* frees skey->key as well */

	return (0);
}

/*
 * get local authentication parameters from cookie (from INIT-ACK)
 */
void
sctp_auth_get_cookie_params(struct sctp_tcb *stcb, struct mbuf *m,
    uint32_t offset, uint32_t length)
{
	struct sctp_paramhdr *phdr, tmp_param;
	uint16_t plen, ptype;
	uint8_t random_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_random *p_random = NULL;
	uint16_t random_len = 0;
	uint8_t hmacs_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_hmac_algo *hmacs = NULL;
	uint16_t hmacs_len = 0;
	uint8_t chunks_store[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_auth_chunk_list *chunks = NULL;
	uint16_t num_chunks = 0;
	sctp_key_t *new_key;
	uint32_t keylen;

	/* convert to upper bound */
	length += offset;

	phdr = (struct sctp_paramhdr *)sctp_m_getptr(m, offset,
	    sizeof(struct sctp_paramhdr), (uint8_t *)&tmp_param);
	while (phdr != NULL) {
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);

		if ((plen == 0) || (offset + plen > length))
			break;

		if (ptype == SCTP_RANDOM) {
			if (plen > sizeof(random_store))
				break;
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)random_store, plen);
			if (phdr == NULL)
				return;
			/* save the random and length for the key */
			p_random = (struct sctp_auth_random *)phdr;
			random_len = plen - sizeof(*p_random);
		} else if (ptype == SCTP_HMAC_LIST) {
			uint16_t num_hmacs;
			uint16_t i;

			if (plen > sizeof(hmacs_store))
				break;
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)hmacs_store, plen);
			if (phdr == NULL)
				return;
			/* save the hmacs list and num for the key */
			hmacs = (struct sctp_auth_hmac_algo *)phdr;
			hmacs_len = plen - sizeof(*hmacs);
			num_hmacs = hmacs_len / sizeof(hmacs->hmac_ids[0]);
			if (stcb->asoc.local_hmacs != NULL)
				sctp_free_hmaclist(stcb->asoc.local_hmacs);
			stcb->asoc.local_hmacs = sctp_alloc_hmaclist(num_hmacs);
			if (stcb->asoc.local_hmacs != NULL) {
				for (i = 0; i < num_hmacs; i++) {
					(void)sctp_auth_add_hmacid(stcb->asoc.local_hmacs,
					    ntohs(hmacs->hmac_ids[i]));
				}
			}
		} else if (ptype == SCTP_CHUNK_LIST) {
			int i;

			if (plen > sizeof(chunks_store))
				break;
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)chunks_store, plen);
			if (phdr == NULL)
				return;
			chunks = (struct sctp_auth_chunk_list *)phdr;
			num_chunks = plen - sizeof(*chunks);
			/* save chunks list and num for the key */
			if (stcb->asoc.local_auth_chunks != NULL)
				sctp_clear_chunklist(stcb->asoc.local_auth_chunks);
			else
				stcb->asoc.local_auth_chunks = sctp_alloc_chunklist();
			for (i = 0; i < num_chunks; i++) {
				(void)sctp_auth_add_chunk(chunks->chunk_types[i],
				    stcb->asoc.local_auth_chunks);
			}
		}
		/* get next parameter */
		offset += SCTP_SIZE32(plen);
		if (offset + sizeof(struct sctp_paramhdr) > length)
			break;
		phdr = (struct sctp_paramhdr *)sctp_m_getptr(m, offset, sizeof(struct sctp_paramhdr),
		    (uint8_t *)&tmp_param);
	}
	/* concatenate the full random key */
	keylen = sizeof(*p_random) + random_len + sizeof(*hmacs) + hmacs_len;
	if (chunks != NULL) {
		keylen += sizeof(*chunks) + num_chunks;
	}
	new_key = sctp_alloc_key(keylen);
	if (new_key != NULL) {
		/* copy in the RANDOM */
		if (p_random != NULL) {
			keylen = sizeof(*p_random) + random_len;
			memcpy(new_key->key, p_random, keylen);
		} else {
			keylen = 0;
		}
		/* append in the AUTH chunks */
		if (chunks != NULL) {
			memcpy(new_key->key + keylen, chunks,
			    sizeof(*chunks) + num_chunks);
			keylen += sizeof(*chunks) + num_chunks;
		}
		/* append in the HMACs */
		if (hmacs != NULL) {
			memcpy(new_key->key + keylen, hmacs,
			    sizeof(*hmacs) + hmacs_len);
		}
	}
	if (stcb->asoc.authinfo.random != NULL)
		sctp_free_key(stcb->asoc.authinfo.random);
	stcb->asoc.authinfo.random = new_key;
	stcb->asoc.authinfo.random_len = random_len;
	sctp_clear_cachedkeys(stcb, stcb->asoc.authinfo.assoc_keyid);
	sctp_clear_cachedkeys(stcb, stcb->asoc.authinfo.recv_keyid);

	/* negotiate what HMAC to use for the peer */
	stcb->asoc.peer_hmac_id = sctp_negotiate_hmacid(stcb->asoc.peer_hmacs,
	    stcb->asoc.local_hmacs);

	/* copy defaults from the endpoint */
	/* FIX ME: put in cookie? */
	stcb->asoc.authinfo.active_keyid = stcb->sctp_ep->sctp_ep.default_keyid;
	/* copy out the shared key list (by reference) from the endpoint */
	(void)sctp_copy_skeylist(&stcb->sctp_ep->sctp_ep.shared_keys,
	    &stcb->asoc.shared_keys);
}

/*
 * compute and fill in the HMAC digest for a packet
 */
void
sctp_fill_hmac_digest_m(struct mbuf *m, uint32_t auth_offset,
    struct sctp_auth_chunk *auth, struct sctp_tcb *stcb, uint16_t keyid)
{
	uint32_t digestlen;
	sctp_sharedkey_t *skey;
	sctp_key_t *key;

	if ((stcb == NULL) || (auth == NULL))
		return;

	/* zero the digest + chunk padding */
	digestlen = sctp_get_hmac_digest_len(stcb->asoc.peer_hmac_id);
	memset(auth->hmac, 0, SCTP_SIZE32(digestlen));

	/* is the desired key cached? */
	if ((keyid != stcb->asoc.authinfo.assoc_keyid) ||
	    (stcb->asoc.authinfo.assoc_key == NULL)) {
		if (stcb->asoc.authinfo.assoc_key != NULL) {
			/* free the old cached key */
			sctp_free_key(stcb->asoc.authinfo.assoc_key);
		}
		skey = sctp_find_sharedkey(&stcb->asoc.shared_keys, keyid);
		/* the only way skey is NULL is if null key id 0 is used */
		if (skey != NULL)
			key = skey->key;
		else
			key = NULL;
		/* compute a new assoc key and cache it */
		stcb->asoc.authinfo.assoc_key =
		    sctp_compute_hashkey(stcb->asoc.authinfo.random,
		    stcb->asoc.authinfo.peer_random, key);
		stcb->asoc.authinfo.assoc_keyid = keyid;
		SCTPDBG(SCTP_DEBUG_AUTH1, "caching key id %u\n",
		    stcb->asoc.authinfo.assoc_keyid);
#ifdef SCTP_DEBUG
		if (SCTP_AUTH_DEBUG)
			sctp_print_key(stcb->asoc.authinfo.assoc_key,
			    "Assoc Key");
#endif
	}

	/* set in the active key id */
	auth->shared_key_id = htons(keyid);

	/* compute and fill in the digest */
	(void)sctp_compute_hmac_m(stcb->asoc.peer_hmac_id, stcb->asoc.authinfo.assoc_key,
	    m, auth_offset, auth->hmac);
}


static void
sctp_zero_m(struct mbuf *m, uint32_t m_offset, uint32_t size)
{
	struct mbuf *m_tmp;
	uint8_t *data;

	/* sanity check */
	if (m == NULL)
		return;

	/* find the correct starting mbuf and offset (get start position) */
	m_tmp = m;
	while ((m_tmp != NULL) && (m_offset >= (uint32_t)SCTP_BUF_LEN(m_tmp))) {
		m_offset -= SCTP_BUF_LEN(m_tmp);
		m_tmp = SCTP_BUF_NEXT(m_tmp);
	}
	/* now use the rest of the mbuf chain */
	while ((m_tmp != NULL) && (size > 0)) {
		data = mtod(m_tmp, uint8_t *)+m_offset;
		if (size > (uint32_t)(SCTP_BUF_LEN(m_tmp) - m_offset)) {
			memset(data, 0, SCTP_BUF_LEN(m_tmp) - m_offset);
			size -= SCTP_BUF_LEN(m_tmp) - m_offset;
		} else {
			memset(data, 0, size);
			size = 0;
		}
		/* clear the offset since it's only for the first mbuf */
		m_offset = 0;
		m_tmp = SCTP_BUF_NEXT(m_tmp);
	}
}

/*-
 * process the incoming Authentication chunk
 * return codes:
 *   -1 on any authentication error
 *    0 on authentication verification
 */
int
sctp_handle_auth(struct sctp_tcb *stcb, struct sctp_auth_chunk *auth,
    struct mbuf *m, uint32_t offset)
{
	uint16_t chunklen;
	uint16_t shared_key_id;
	uint16_t hmac_id;
	sctp_sharedkey_t *skey;
	uint32_t digestlen;
	uint8_t digest[SCTP_AUTH_DIGEST_LEN_MAX];
	uint8_t computed_digest[SCTP_AUTH_DIGEST_LEN_MAX];

	/* auth is checked for NULL by caller */
	chunklen = ntohs(auth->ch.chunk_length);
	if (chunklen < sizeof(*auth)) {
		SCTP_STAT_INCR(sctps_recvauthfailed);
		return (-1);
	}
	SCTP_STAT_INCR(sctps_recvauth);

	/* get the auth params */
	shared_key_id = ntohs(auth->shared_key_id);
	hmac_id = ntohs(auth->hmac_id);
	SCTPDBG(SCTP_DEBUG_AUTH1,
	    "SCTP AUTH Chunk: shared key %u, HMAC id %u\n",
	    shared_key_id, hmac_id);

	/* is the indicated HMAC supported? */
	if (!sctp_auth_is_supported_hmac(stcb->asoc.local_hmacs, hmac_id)) {
		struct mbuf *op_err;
		struct sctp_error_auth_invalid_hmac *cause;

		SCTP_STAT_INCR(sctps_recvivalhmacid);
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP Auth: unsupported HMAC id %u\n",
		    hmac_id);
		/*
		 * report this in an Error Chunk: Unsupported HMAC
		 * Identifier
		 */
		op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_error_auth_invalid_hmac),
		    0, M_NOWAIT, 1, MT_HEADER);
		if (op_err != NULL) {
			/* pre-reserve some space */
			SCTP_BUF_RESV_UF(op_err, sizeof(struct sctp_chunkhdr));
			/* fill in the error */
			cause = mtod(op_err, struct sctp_error_auth_invalid_hmac *);
			cause->cause.code = htons(SCTP_CAUSE_UNSUPPORTED_HMACID);
			cause->cause.length = htons(sizeof(struct sctp_error_auth_invalid_hmac));
			cause->hmac_id = ntohs(hmac_id);
			SCTP_BUF_LEN(op_err) = sizeof(struct sctp_error_auth_invalid_hmac);
			/* queue it */
			sctp_queue_op_err(stcb, op_err);
		}
		return (-1);
	}
	/* get the indicated shared key, if available */
	if ((stcb->asoc.authinfo.recv_key == NULL) ||
	    (stcb->asoc.authinfo.recv_keyid != shared_key_id)) {
		/* find the shared key on the assoc first */
		skey = sctp_find_sharedkey(&stcb->asoc.shared_keys,
		    shared_key_id);
		/* if the shared key isn't found, discard the chunk */
		if (skey == NULL) {
			SCTP_STAT_INCR(sctps_recvivalkeyid);
			SCTPDBG(SCTP_DEBUG_AUTH1,
			    "SCTP Auth: unknown key id %u\n",
			    shared_key_id);
			return (-1);
		}
		/* generate a notification if this is a new key id */
		if (stcb->asoc.authinfo.recv_keyid != shared_key_id)
			/*
			 * sctp_ulp_notify(SCTP_NOTIFY_AUTH_NEW_KEY, stcb,
			 * shared_key_id, (void
			 * *)stcb->asoc.authinfo.recv_keyid);
			 */
			sctp_notify_authentication(stcb, SCTP_AUTH_NEW_KEY,
			    shared_key_id, stcb->asoc.authinfo.recv_keyid,
			    SCTP_SO_NOT_LOCKED);
		/* compute a new recv assoc key and cache it */
		if (stcb->asoc.authinfo.recv_key != NULL)
			sctp_free_key(stcb->asoc.authinfo.recv_key);
		stcb->asoc.authinfo.recv_key =
		    sctp_compute_hashkey(stcb->asoc.authinfo.random,
		    stcb->asoc.authinfo.peer_random, skey->key);
		stcb->asoc.authinfo.recv_keyid = shared_key_id;
#ifdef SCTP_DEBUG
		if (SCTP_AUTH_DEBUG)
			sctp_print_key(stcb->asoc.authinfo.recv_key, "Recv Key");
#endif
	}
	/* validate the digest length */
	digestlen = sctp_get_hmac_digest_len(hmac_id);
	if (chunklen < (sizeof(*auth) + digestlen)) {
		/* invalid digest length */
		SCTP_STAT_INCR(sctps_recvauthfailed);
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP Auth: chunk too short for HMAC\n");
		return (-1);
	}
	/* save a copy of the digest, zero the pseudo header, and validate */
	memcpy(digest, auth->hmac, digestlen);
	sctp_zero_m(m, offset + sizeof(*auth), SCTP_SIZE32(digestlen));
	(void)sctp_compute_hmac_m(hmac_id, stcb->asoc.authinfo.recv_key,
	    m, offset, computed_digest);

	/* compare the computed digest with the one in the AUTH chunk */
	if (timingsafe_bcmp(digest, computed_digest, digestlen) != 0) {
		SCTP_STAT_INCR(sctps_recvauthfailed);
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP Auth: HMAC digest check failed\n");
		return (-1);
	}
	return (0);
}

/*
 * Generate NOTIFICATION
 */
void
sctp_notify_authentication(struct sctp_tcb *stcb, uint32_t indication,
    uint16_t keyid, uint16_t alt_keyid, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	struct mbuf *m_notify;
	struct sctp_authkey_event *auth;
	struct sctp_queued_to_read *control;

	if ((stcb == NULL) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET)
	    ) {
		/* If the socket is gone we are out of here */
		return;
	}

	if (sctp_stcb_is_feature_off(stcb->sctp_ep, stcb, SCTP_PCB_FLAGS_AUTHEVNT))
		/* event not enabled */
		return;

	m_notify = sctp_get_mbuf_for_msg(sizeof(struct sctp_authkey_event),
	    0, M_NOWAIT, 1, MT_HEADER);
	if (m_notify == NULL)
		/* no space left */
		return;

	SCTP_BUF_LEN(m_notify) = 0;
	auth = mtod(m_notify, struct sctp_authkey_event *);
	memset(auth, 0, sizeof(struct sctp_authkey_event));
	auth->auth_type = SCTP_AUTHENTICATION_EVENT;
	auth->auth_flags = 0;
	auth->auth_length = sizeof(*auth);
	auth->auth_keynumber = keyid;
	auth->auth_altkeynumber = alt_keyid;
	auth->auth_indication = indication;
	auth->auth_assoc_id = sctp_get_associd(stcb);

	SCTP_BUF_LEN(m_notify) = sizeof(*auth);
	SCTP_BUF_NEXT(m_notify) = NULL;

	/* append to socket */
	control = sctp_build_readq_entry(stcb, stcb->asoc.primary_destination,
	    0, 0, stcb->asoc.context, 0, 0, 0, m_notify);
	if (control == NULL) {
		/* no memory */
		sctp_m_freem(m_notify);
		return;
	}
	control->length = SCTP_BUF_LEN(m_notify);
	control->spec_flags = M_NOTIFICATION;
	/* not that we need this */
	control->tail_mbuf = m_notify;
	sctp_add_to_readq(stcb->sctp_ep, stcb, control,
	    &stcb->sctp_socket->so_rcv, 1, SCTP_READ_LOCK_NOT_HELD, so_locked);
}


/*-
 * validates the AUTHentication related parameters in an INIT/INIT-ACK
 * Note: currently only used for INIT as INIT-ACK is handled inline
 * with sctp_load_addresses_from_init()
 */
int
sctp_validate_init_auth_params(struct mbuf *m, int offset, int limit)
{
	struct sctp_paramhdr *phdr, param_buf;
	uint16_t ptype, plen;
	int peer_supports_asconf = 0;
	int peer_supports_auth = 0;
	int got_random = 0, got_hmacs = 0, got_chklist = 0;
	uint8_t saw_asconf = 0;
	uint8_t saw_asconf_ack = 0;

	/* go through each of the params. */
	phdr = sctp_get_next_param(m, offset, &param_buf, sizeof(param_buf));
	while (phdr) {
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);

		if (offset + plen > limit) {
			break;
		}
		if (plen < sizeof(struct sctp_paramhdr)) {
			break;
		}
		if (ptype == SCTP_SUPPORTED_CHUNK_EXT) {
			/* A supported extension chunk */
			struct sctp_supported_chunk_types_param *pr_supported;
			uint8_t local_store[SCTP_SMALL_CHUNK_STORE];
			int num_ent, i;

			if (plen > sizeof(local_store)) {
				break;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)&local_store,
			    plen);
			if (phdr == NULL) {
				return (-1);
			}
			pr_supported = (struct sctp_supported_chunk_types_param *)phdr;
			num_ent = plen - sizeof(struct sctp_paramhdr);
			for (i = 0; i < num_ent; i++) {
				switch (pr_supported->chunk_types[i]) {
				case SCTP_ASCONF:
				case SCTP_ASCONF_ACK:
					peer_supports_asconf = 1;
					break;
				default:
					/* one we don't care about */
					break;
				}
			}
		} else if (ptype == SCTP_RANDOM) {
			/* enforce the random length */
			if (plen != (sizeof(struct sctp_auth_random) +
			    SCTP_AUTH_RANDOM_SIZE_REQUIRED)) {
				SCTPDBG(SCTP_DEBUG_AUTH1,
				    "SCTP: invalid RANDOM len\n");
				return (-1);
			}
			got_random = 1;
		} else if (ptype == SCTP_HMAC_LIST) {
			struct sctp_auth_hmac_algo *hmacs;
			uint8_t store[SCTP_PARAM_BUFFER_SIZE];
			int num_hmacs;

			if (plen > sizeof(store)) {
				break;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)store,
			    plen);
			if (phdr == NULL) {
				return (-1);
			}
			hmacs = (struct sctp_auth_hmac_algo *)phdr;
			num_hmacs = (plen - sizeof(*hmacs)) / sizeof(hmacs->hmac_ids[0]);
			/* validate the hmac list */
			if (sctp_verify_hmac_param(hmacs, num_hmacs)) {
				SCTPDBG(SCTP_DEBUG_AUTH1,
				    "SCTP: invalid HMAC param\n");
				return (-1);
			}
			got_hmacs = 1;
		} else if (ptype == SCTP_CHUNK_LIST) {
			struct sctp_auth_chunk_list *chunks;
			uint8_t chunks_store[SCTP_SMALL_CHUNK_STORE];
			int i, num_chunks;

			if (plen > sizeof(chunks_store)) {
				break;
			}
			phdr = sctp_get_next_param(m, offset,
			    (struct sctp_paramhdr *)chunks_store,
			    plen);
			if (phdr == NULL) {
				return (-1);
			}
			/*-
			 * Flip through the list and mark that the
			 * peer supports asconf/asconf_ack.
			 */
			chunks = (struct sctp_auth_chunk_list *)phdr;
			num_chunks = plen - sizeof(*chunks);
			for (i = 0; i < num_chunks; i++) {
				/* record asconf/asconf-ack if listed */
				if (chunks->chunk_types[i] == SCTP_ASCONF)
					saw_asconf = 1;
				if (chunks->chunk_types[i] == SCTP_ASCONF_ACK)
					saw_asconf_ack = 1;

			}
			if (num_chunks)
				got_chklist = 1;
		}

		offset += SCTP_SIZE32(plen);
		if (offset >= limit) {
			break;
		}
		phdr = sctp_get_next_param(m, offset, &param_buf,
		    sizeof(param_buf));
	}
	/* validate authentication required parameters */
	if (got_random && got_hmacs) {
		peer_supports_auth = 1;
	} else {
		peer_supports_auth = 0;
	}
	if (!peer_supports_auth && got_chklist) {
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP: peer sent chunk list w/o AUTH\n");
		return (-1);
	}
	if (peer_supports_asconf && !peer_supports_auth) {
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "SCTP: peer supports ASCONF but not AUTH\n");
		return (-1);
	} else if ((peer_supports_asconf) && (peer_supports_auth) &&
	    ((saw_asconf == 0) || (saw_asconf_ack == 0))) {
		return (-2);
	}
	return (0);
}

void
sctp_initialize_auth_params(struct sctp_inpcb *inp, struct sctp_tcb *stcb)
{
	uint16_t chunks_len = 0;
	uint16_t hmacs_len = 0;
	uint16_t random_len = SCTP_AUTH_RANDOM_SIZE_DEFAULT;
	sctp_key_t *new_key;
	uint16_t keylen;

	/* initialize hmac list from endpoint */
	stcb->asoc.local_hmacs = sctp_copy_hmaclist(inp->sctp_ep.local_hmacs);
	if (stcb->asoc.local_hmacs != NULL) {
		hmacs_len = stcb->asoc.local_hmacs->num_algo *
		    sizeof(stcb->asoc.local_hmacs->hmac[0]);
	}
	/* initialize auth chunks list from endpoint */
	stcb->asoc.local_auth_chunks =
	    sctp_copy_chunklist(inp->sctp_ep.local_auth_chunks);
	if (stcb->asoc.local_auth_chunks != NULL) {
		int i;

		for (i = 0; i < 256; i++) {
			if (stcb->asoc.local_auth_chunks->chunks[i])
				chunks_len++;
		}
	}
	/* copy defaults from the endpoint */
	stcb->asoc.authinfo.active_keyid = inp->sctp_ep.default_keyid;

	/* copy out the shared key list (by reference) from the endpoint */
	(void)sctp_copy_skeylist(&inp->sctp_ep.shared_keys,
	    &stcb->asoc.shared_keys);

	/* now set the concatenated key (random + chunks + hmacs) */
	/* key includes parameter headers */
	keylen = (3 * sizeof(struct sctp_paramhdr)) + random_len + chunks_len +
	    hmacs_len;
	new_key = sctp_alloc_key(keylen);
	if (new_key != NULL) {
		struct sctp_paramhdr *ph;
		int plen;

		/* generate and copy in the RANDOM */
		ph = (struct sctp_paramhdr *)new_key->key;
		ph->param_type = htons(SCTP_RANDOM);
		plen = sizeof(*ph) + random_len;
		ph->param_length = htons(plen);
		SCTP_READ_RANDOM(new_key->key + sizeof(*ph), random_len);
		keylen = plen;

		/* append in the AUTH chunks */
		/* NOTE: currently we always have chunks to list */
		ph = (struct sctp_paramhdr *)(new_key->key + keylen);
		ph->param_type = htons(SCTP_CHUNK_LIST);
		plen = sizeof(*ph) + chunks_len;
		ph->param_length = htons(plen);
		keylen += sizeof(*ph);
		if (stcb->asoc.local_auth_chunks) {
			int i;

			for (i = 0; i < 256; i++) {
				if (stcb->asoc.local_auth_chunks->chunks[i])
					new_key->key[keylen++] = i;
			}
		}

		/* append in the HMACs */
		ph = (struct sctp_paramhdr *)(new_key->key + keylen);
		ph->param_type = htons(SCTP_HMAC_LIST);
		plen = sizeof(*ph) + hmacs_len;
		ph->param_length = htons(plen);
		keylen += sizeof(*ph);
		(void)sctp_serialize_hmaclist(stcb->asoc.local_hmacs,
		    new_key->key + keylen);
	}
	if (stcb->asoc.authinfo.random != NULL)
		sctp_free_key(stcb->asoc.authinfo.random);
	stcb->asoc.authinfo.random = new_key;
	stcb->asoc.authinfo.random_len = random_len;
}
