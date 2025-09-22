/*	$OpenBSD: wg_noise.c,v 1.7 2024/03/05 17:48:01 mvs Exp $ */
/*
 * Copyright (C) 2015-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2019-2020 Matt Dunwoodie <ncon@noconroy.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

#include <crypto/blake2s.h>
#include <crypto/curve25519.h>
#include <crypto/chachapoly.h>

#include <net/wg_noise.h>

/* Private functions */
static struct noise_keypair *
		noise_remote_keypair_allocate(struct noise_remote *);
static void
		noise_remote_keypair_free(struct noise_remote *,
			struct noise_keypair *);
static uint32_t	noise_remote_handshake_index_get(struct noise_remote *);
static void	noise_remote_handshake_index_drop(struct noise_remote *);

static uint64_t	noise_counter_send(struct noise_counter *);
static int	noise_counter_recv(struct noise_counter *, uint64_t);

static void	noise_kdf(uint8_t *, uint8_t *, uint8_t *, const uint8_t *,
			size_t, size_t, size_t, size_t,
			const uint8_t [NOISE_HASH_LEN]);
static int	noise_mix_dh(
			uint8_t [NOISE_HASH_LEN],
			uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			const uint8_t [NOISE_PUBLIC_KEY_LEN],
			const uint8_t [NOISE_PUBLIC_KEY_LEN]);
static int	noise_mix_ss(
			uint8_t ck[NOISE_HASH_LEN],
			uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
			const uint8_t ss[NOISE_PUBLIC_KEY_LEN]);
static void	noise_mix_hash(
			uint8_t [NOISE_HASH_LEN],
			const uint8_t *,
			size_t);
static void	noise_mix_psk(
			uint8_t [NOISE_HASH_LEN],
			uint8_t [NOISE_HASH_LEN],
			uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			const uint8_t [NOISE_SYMMETRIC_KEY_LEN]);
static void	noise_param_init(
			uint8_t [NOISE_HASH_LEN],
			uint8_t [NOISE_HASH_LEN],
			const uint8_t [NOISE_PUBLIC_KEY_LEN]);

static void	noise_msg_encrypt(uint8_t *, const uint8_t *, size_t,
			uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			uint8_t [NOISE_HASH_LEN]);
static int	noise_msg_decrypt(uint8_t *, const uint8_t *, size_t,
			uint8_t [NOISE_SYMMETRIC_KEY_LEN],
			uint8_t [NOISE_HASH_LEN]);
static void	noise_msg_ephemeral(
			uint8_t [NOISE_HASH_LEN],
			uint8_t [NOISE_HASH_LEN],
			const uint8_t src[NOISE_PUBLIC_KEY_LEN]);

static void	noise_tai64n_now(uint8_t [NOISE_TIMESTAMP_LEN]);
static int	noise_timer_expired(struct timespec *, time_t, long);

/* Set/Get noise parameters */
void
noise_local_init(struct noise_local *l, struct noise_upcall *upcall)
{
	bzero(l, sizeof(*l));
	rw_init(&l->l_identity_lock, "noise_local_identity");
	l->l_upcall = *upcall;
}

void
noise_local_lock_identity(struct noise_local *l)
{
	rw_enter_write(&l->l_identity_lock);
}

void
noise_local_unlock_identity(struct noise_local *l)
{
	rw_exit_write(&l->l_identity_lock);
}

int
noise_local_set_private(struct noise_local *l,
			uint8_t private[NOISE_PUBLIC_KEY_LEN])
{
	rw_assert_wrlock(&l->l_identity_lock);

	memcpy(l->l_private, private, NOISE_PUBLIC_KEY_LEN);
	curve25519_clamp_secret(l->l_private);
	l->l_has_identity = curve25519_generate_public(l->l_public, private);

	return l->l_has_identity ? 0 : ENXIO;
}

int
noise_local_keys(struct noise_local *l, uint8_t public[NOISE_PUBLIC_KEY_LEN],
    uint8_t private[NOISE_PUBLIC_KEY_LEN])
{
	int ret = 0;
	rw_enter_read(&l->l_identity_lock);
	if (l->l_has_identity) {
		if (public != NULL)
			memcpy(public, l->l_public, NOISE_PUBLIC_KEY_LEN);
		if (private != NULL)
			memcpy(private, l->l_private, NOISE_PUBLIC_KEY_LEN);
	} else {
		ret = ENXIO;
	}
	rw_exit_read(&l->l_identity_lock);
	return ret;
}

void
noise_remote_init(struct noise_remote *r, uint8_t public[NOISE_PUBLIC_KEY_LEN],
    struct noise_local *l)
{
	bzero(r, sizeof(*r));
	memcpy(r->r_public, public, NOISE_PUBLIC_KEY_LEN);
	rw_init(&r->r_handshake_lock, "noise_handshake");
	mtx_init_flags(&r->r_keypair_mtx, IPL_NET, "noise_keypair", 0);

	SLIST_INSERT_HEAD(&r->r_unused_keypairs, &r->r_keypair[0], kp_entry);
	SLIST_INSERT_HEAD(&r->r_unused_keypairs, &r->r_keypair[1], kp_entry);
	SLIST_INSERT_HEAD(&r->r_unused_keypairs, &r->r_keypair[2], kp_entry);

	KASSERT(l != NULL);
	r->r_local = l;

	rw_enter_write(&l->l_identity_lock);
	noise_remote_precompute(r);
	rw_exit_write(&l->l_identity_lock);
}

int
noise_remote_set_psk(struct noise_remote *r,
    uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	int same;
	rw_enter_write(&r->r_handshake_lock);
	same = !timingsafe_bcmp(r->r_psk, psk, NOISE_SYMMETRIC_KEY_LEN);
	if (!same) {
		memcpy(r->r_psk, psk, NOISE_SYMMETRIC_KEY_LEN);
	}
	rw_exit_write(&r->r_handshake_lock);
	return same ? EEXIST : 0;
}

int
noise_remote_keys(struct noise_remote *r, uint8_t public[NOISE_PUBLIC_KEY_LEN],
    uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	static uint8_t null_psk[NOISE_SYMMETRIC_KEY_LEN];
	int ret;

	if (public != NULL)
		memcpy(public, r->r_public, NOISE_PUBLIC_KEY_LEN);

	rw_enter_read(&r->r_handshake_lock);
	if (psk != NULL)
		memcpy(psk, r->r_psk, NOISE_SYMMETRIC_KEY_LEN);
	ret = timingsafe_bcmp(r->r_psk, null_psk, NOISE_SYMMETRIC_KEY_LEN);
	rw_exit_read(&r->r_handshake_lock);

	/* If r_psk != null_psk return 0, else ENOENT (no psk) */
	return ret ? 0 : ENOENT;
}

void
noise_remote_precompute(struct noise_remote *r)
{
	struct noise_local *l = r->r_local;
	rw_assert_wrlock(&l->l_identity_lock);
	if (!l->l_has_identity)
		bzero(r->r_ss, NOISE_PUBLIC_KEY_LEN);
	else if (!curve25519(r->r_ss, l->l_private, r->r_public))
		bzero(r->r_ss, NOISE_PUBLIC_KEY_LEN);

	rw_enter_write(&r->r_handshake_lock);
	noise_remote_handshake_index_drop(r);
	explicit_bzero(&r->r_handshake, sizeof(r->r_handshake));
	rw_exit_write(&r->r_handshake_lock);
}

/* Handshake functions */
int
noise_create_initiation(struct noise_remote *r, uint32_t *s_idx,
    uint8_t ue[NOISE_PUBLIC_KEY_LEN],
    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN])
{
	struct noise_handshake *hs = &r->r_handshake;
	struct noise_local *l = r->r_local;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	int ret = EINVAL;

	rw_enter_read(&l->l_identity_lock);
	rw_enter_write(&r->r_handshake_lock);
	if (!l->l_has_identity)
		goto error;
	noise_param_init(hs->hs_ck, hs->hs_hash, r->r_public);

	/* e */
	curve25519_generate_secret(hs->hs_e);
	if (curve25519_generate_public(ue, hs->hs_e) == 0)
		goto error;
	noise_msg_ephemeral(hs->hs_ck, hs->hs_hash, ue);

	/* es */
	if (noise_mix_dh(hs->hs_ck, key, hs->hs_e, r->r_public) != 0)
		goto error;

	/* s */
	noise_msg_encrypt(es, l->l_public,
	    NOISE_PUBLIC_KEY_LEN, key, hs->hs_hash);

	/* ss */
	if (noise_mix_ss(hs->hs_ck, key, r->r_ss) != 0)
		goto error;

	/* {t} */
	noise_tai64n_now(ets);
	noise_msg_encrypt(ets, ets,
	    NOISE_TIMESTAMP_LEN, key, hs->hs_hash);

	noise_remote_handshake_index_drop(r);
	hs->hs_state = CREATED_INITIATION;
	hs->hs_local_index = noise_remote_handshake_index_get(r);
	*s_idx = hs->hs_local_index;
	ret = 0;
error:
	rw_exit_write(&r->r_handshake_lock);
	rw_exit_read(&l->l_identity_lock);
	explicit_bzero(key, NOISE_SYMMETRIC_KEY_LEN);
	return ret;
}

int
noise_consume_initiation(struct noise_local *l, struct noise_remote **rp,
    uint32_t s_idx, uint8_t ue[NOISE_PUBLIC_KEY_LEN],
    uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN],
    uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN])
{
	struct noise_remote *r;
	struct noise_handshake hs;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t r_public[NOISE_PUBLIC_KEY_LEN];
	uint8_t	timestamp[NOISE_TIMESTAMP_LEN];
	int ret = EINVAL;

	rw_enter_read(&l->l_identity_lock);
	if (!l->l_has_identity)
		goto error;
	noise_param_init(hs.hs_ck, hs.hs_hash, l->l_public);

	/* e */
	noise_msg_ephemeral(hs.hs_ck, hs.hs_hash, ue);

	/* es */
	if (noise_mix_dh(hs.hs_ck, key, l->l_private, ue) != 0)
		goto error;

	/* s */
	if (noise_msg_decrypt(r_public, es,
	    NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN, key, hs.hs_hash) != 0)
		goto error;

	/* Lookup the remote we received from */
	if ((r = l->l_upcall.u_remote_get(l->l_upcall.u_arg, r_public)) == NULL)
		goto error;

	/* ss */
	if (noise_mix_ss(hs.hs_ck, key, r->r_ss) != 0)
		goto error;

	/* {t} */
	if (noise_msg_decrypt(timestamp, ets,
	    NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN, key, hs.hs_hash) != 0)
		goto error;

	memcpy(hs.hs_e, ue, NOISE_PUBLIC_KEY_LEN);

	/* We have successfully computed the same results, now we ensure that
	 * this is not an initiation replay, or a flood attack */
	rw_enter_write(&r->r_handshake_lock);

	/* Replay */
	if (memcmp(timestamp, r->r_timestamp, NOISE_TIMESTAMP_LEN) > 0)
		memcpy(r->r_timestamp, timestamp, NOISE_TIMESTAMP_LEN);
	else
		goto error_set;
	/* Flood attack */
	if (noise_timer_expired(&r->r_last_init, 0, REJECT_INTERVAL))
		getnanouptime(&r->r_last_init);
	else
		goto error_set;

	/* Ok, we're happy to accept this initiation now */
	noise_remote_handshake_index_drop(r);
	hs.hs_state = CONSUMED_INITIATION;
	hs.hs_local_index = noise_remote_handshake_index_get(r);
	hs.hs_remote_index = s_idx;
	r->r_handshake = hs;
	*rp = r;
	ret = 0;
error_set:
	rw_exit_write(&r->r_handshake_lock);
error:
	rw_exit_read(&l->l_identity_lock);
	explicit_bzero(key, NOISE_SYMMETRIC_KEY_LEN);
	explicit_bzero(&hs, sizeof(hs));
	return ret;
}

int
noise_create_response(struct noise_remote *r, uint32_t *s_idx, uint32_t *r_idx,
    uint8_t ue[NOISE_PUBLIC_KEY_LEN], uint8_t en[0 + NOISE_AUTHTAG_LEN])
{
	struct noise_handshake *hs = &r->r_handshake;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t e[NOISE_PUBLIC_KEY_LEN];
	int ret = EINVAL;

	rw_enter_read(&r->r_local->l_identity_lock);
	rw_enter_write(&r->r_handshake_lock);

	if (hs->hs_state != CONSUMED_INITIATION)
		goto error;

	/* e */
	curve25519_generate_secret(e);
	if (curve25519_generate_public(ue, e) == 0)
		goto error;
	noise_msg_ephemeral(hs->hs_ck, hs->hs_hash, ue);

	/* ee */
	if (noise_mix_dh(hs->hs_ck, NULL, e, hs->hs_e) != 0)
		goto error;

	/* se */
	if (noise_mix_dh(hs->hs_ck, NULL, e, r->r_public) != 0)
		goto error;

	/* psk */
	noise_mix_psk(hs->hs_ck, hs->hs_hash, key, r->r_psk);

	/* {} */
	noise_msg_encrypt(en, NULL, 0, key, hs->hs_hash);

	hs->hs_state = CREATED_RESPONSE;
	*r_idx = hs->hs_remote_index;
	*s_idx = hs->hs_local_index;
	ret = 0;
error:
	rw_exit_write(&r->r_handshake_lock);
	rw_exit_read(&r->r_local->l_identity_lock);
	explicit_bzero(key, NOISE_SYMMETRIC_KEY_LEN);
	explicit_bzero(e, NOISE_PUBLIC_KEY_LEN);
	return ret;
}

int
noise_consume_response(struct noise_remote *r, uint32_t s_idx, uint32_t r_idx,
    uint8_t ue[NOISE_PUBLIC_KEY_LEN], uint8_t en[0 + NOISE_AUTHTAG_LEN])
{
	struct noise_local *l = r->r_local;
	struct noise_handshake hs;
	uint8_t key[NOISE_SYMMETRIC_KEY_LEN];
	uint8_t preshared_key[NOISE_PUBLIC_KEY_LEN];
	int ret = EINVAL;

	rw_enter_read(&l->l_identity_lock);
	if (!l->l_has_identity)
		goto error;

	rw_enter_read(&r->r_handshake_lock);
	hs = r->r_handshake;
	memcpy(preshared_key, r->r_psk, NOISE_SYMMETRIC_KEY_LEN);
	rw_exit_read(&r->r_handshake_lock);

	if (hs.hs_state != CREATED_INITIATION ||
	    hs.hs_local_index != r_idx)
		goto error;

	/* e */
	noise_msg_ephemeral(hs.hs_ck, hs.hs_hash, ue);

	/* ee */
	if (noise_mix_dh(hs.hs_ck, NULL, hs.hs_e, ue) != 0)
		goto error;

	/* se */
	if (noise_mix_dh(hs.hs_ck, NULL, l->l_private, ue) != 0)
		goto error;

	/* psk */
	noise_mix_psk(hs.hs_ck, hs.hs_hash, key, preshared_key);

	/* {} */
	if (noise_msg_decrypt(NULL, en,
	    0 + NOISE_AUTHTAG_LEN, key, hs.hs_hash) != 0)
		goto error;

	hs.hs_remote_index = s_idx;

	rw_enter_write(&r->r_handshake_lock);
	if (r->r_handshake.hs_state == hs.hs_state &&
	    r->r_handshake.hs_local_index == hs.hs_local_index) {
		r->r_handshake = hs;
		r->r_handshake.hs_state = CONSUMED_RESPONSE;
		ret = 0;
	}
	rw_exit_write(&r->r_handshake_lock);
error:
	rw_exit_read(&l->l_identity_lock);
	explicit_bzero(&hs, sizeof(hs));
	explicit_bzero(key, NOISE_SYMMETRIC_KEY_LEN);
	return ret;
}

int
noise_remote_begin_session(struct noise_remote *r)
{
	struct noise_handshake *hs = &r->r_handshake;
	struct noise_keypair kp, *next, *current, *previous;

	rw_enter_write(&r->r_handshake_lock);

	/* We now derive the keypair from the handshake */
	if (hs->hs_state == CONSUMED_RESPONSE) {
		kp.kp_is_initiator = 1;
		noise_kdf(kp.kp_send, kp.kp_recv, NULL, NULL,
		    NOISE_SYMMETRIC_KEY_LEN, NOISE_SYMMETRIC_KEY_LEN, 0, 0,
		    hs->hs_ck);
	} else if (hs->hs_state == CREATED_RESPONSE) {
		kp.kp_is_initiator = 0;
		noise_kdf(kp.kp_recv, kp.kp_send, NULL, NULL,
		    NOISE_SYMMETRIC_KEY_LEN, NOISE_SYMMETRIC_KEY_LEN, 0, 0,
		    hs->hs_ck);
	} else {
		rw_exit_write(&r->r_handshake_lock);
		return EINVAL;
	}

	kp.kp_valid = 1;
	kp.kp_local_index = hs->hs_local_index;
	kp.kp_remote_index = hs->hs_remote_index;
	getnanouptime(&kp.kp_birthdate);
	bzero(&kp.kp_ctr, sizeof(kp.kp_ctr));
	mtx_init_flags(&kp.kp_ctr.c_mtx, IPL_NET, "noise_counter", 0);

	/* Now we need to add_new_keypair */
	mtx_enter(&r->r_keypair_mtx);
	next = r->r_next;
	current = r->r_current;
	previous = r->r_previous;

	if (kp.kp_is_initiator) {
		if (next != NULL) {
			r->r_next = NULL;
			r->r_previous = next;
			noise_remote_keypair_free(r, current);
		} else {
			r->r_previous = current;
		}

		noise_remote_keypair_free(r, previous);

		r->r_current = noise_remote_keypair_allocate(r);
		*r->r_current = kp;
	} else {
		noise_remote_keypair_free(r, next);
		r->r_previous = NULL;
		noise_remote_keypair_free(r, previous);

		r->r_next = noise_remote_keypair_allocate(r);
		*r->r_next = kp;
	}
	mtx_leave(&r->r_keypair_mtx);

	explicit_bzero(&r->r_handshake, sizeof(r->r_handshake));
	rw_exit_write(&r->r_handshake_lock);

	explicit_bzero(&kp, sizeof(kp));
	return 0;
}

void
noise_remote_clear(struct noise_remote *r)
{
	rw_enter_write(&r->r_handshake_lock);
	noise_remote_handshake_index_drop(r);
	explicit_bzero(&r->r_handshake, sizeof(r->r_handshake));
	rw_exit_write(&r->r_handshake_lock);

	mtx_enter(&r->r_keypair_mtx);
	noise_remote_keypair_free(r, r->r_next);
	noise_remote_keypair_free(r, r->r_current);
	noise_remote_keypair_free(r, r->r_previous);
	r->r_next = NULL;
	r->r_current = NULL;
	r->r_previous = NULL;
	mtx_leave(&r->r_keypair_mtx);
}

void
noise_remote_expire_current(struct noise_remote *r)
{
	mtx_enter(&r->r_keypair_mtx);
	if (r->r_next != NULL)
		r->r_next->kp_valid = 0;
	if (r->r_current != NULL)
		r->r_current->kp_valid = 0;
	mtx_leave(&r->r_keypair_mtx);
}

int
noise_remote_ready(struct noise_remote *r)
{
	struct noise_keypair *kp;
	int ret;

	mtx_enter(&r->r_keypair_mtx);
	/* kp_ctr isn't locked here, we're happy to accept a racy read. */
	if ((kp = r->r_current) == NULL ||
	    !kp->kp_valid ||
	    noise_timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME, 0) ||
	    kp->kp_ctr.c_recv >= REJECT_AFTER_MESSAGES ||
	    kp->kp_ctr.c_send >= REJECT_AFTER_MESSAGES)
		ret = EINVAL;
	else
		ret = 0;
	mtx_leave(&r->r_keypair_mtx);
	return ret;
}

int
noise_remote_encrypt(struct noise_remote *r, uint32_t *r_idx, uint64_t *nonce,
    uint8_t *buf, size_t buflen)
{
	struct noise_keypair *kp;
	int ret = EINVAL;

	mtx_enter(&r->r_keypair_mtx);
	if ((kp = r->r_current) == NULL)
		goto error;

	/* We confirm that our values are within our tolerances. We want:
	 *  - a valid keypair
	 *  - our keypair to be less than REJECT_AFTER_TIME seconds old
	 *  - our receive counter to be less than REJECT_AFTER_MESSAGES
	 *  - our send counter to be less than REJECT_AFTER_MESSAGES
	 *
	 * kp_ctr isn't locked here, we're happy to accept a racy read. */
	if (!kp->kp_valid ||
	    noise_timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME, 0) ||
	    kp->kp_ctr.c_recv >= REJECT_AFTER_MESSAGES ||
	    ((*nonce = noise_counter_send(&kp->kp_ctr)) > REJECT_AFTER_MESSAGES))
		goto error;

	/* We encrypt into the same buffer, so the caller must ensure that buf
	 * has NOISE_AUTHTAG_LEN bytes to store the MAC. The nonce and index
	 * are passed back out to the caller through the provided data pointer. */
	*r_idx = kp->kp_remote_index;
	chacha20poly1305_encrypt(buf, buf, buflen,
	    NULL, 0, *nonce, kp->kp_send);

	/* If our values are still within tolerances, but we are approaching
	 * the tolerances, we notify the caller with ESTALE that they should
	 * establish a new keypair. The current keypair can continue to be used
	 * until the tolerances are hit. We notify if:
	 *  - our send counter is valid and not less than REKEY_AFTER_MESSAGES
	 *  - we're the initiator and our keypair is older than
	 *    REKEY_AFTER_TIME seconds */
	ret = ESTALE;
	if ((kp->kp_valid && *nonce >= REKEY_AFTER_MESSAGES) ||
	    (kp->kp_is_initiator &&
	    noise_timer_expired(&kp->kp_birthdate, REKEY_AFTER_TIME, 0)))
		goto error;

	ret = 0;
error:
	mtx_leave(&r->r_keypair_mtx);
	return ret;
}

int
noise_remote_decrypt(struct noise_remote *r, uint32_t r_idx, uint64_t nonce,
    uint8_t *buf, size_t buflen)
{
	struct noise_keypair *kp;
	int ret = EINVAL;

	/* We retrieve the keypair corresponding to the provided index. We
	 * attempt the current keypair first as that is most likely. We also
	 * want to make sure that the keypair is valid as it would be
	 * catastrophic to decrypt against a zero'ed keypair. */
	mtx_enter(&r->r_keypair_mtx);

	if (r->r_current != NULL && r->r_current->kp_local_index == r_idx) {
		kp = r->r_current;
	} else if (r->r_previous != NULL && r->r_previous->kp_local_index == r_idx) {
		kp = r->r_previous;
	} else if (r->r_next != NULL && r->r_next->kp_local_index == r_idx) {
		kp = r->r_next;
	} else {
		goto error;
	}

	/* We confirm that our values are within our tolerances. These values
	 * are the same as the encrypt routine.
	 *
	 * kp_ctr isn't locked here, we're happy to accept a racy read. */
	if (noise_timer_expired(&kp->kp_birthdate, REJECT_AFTER_TIME, 0) ||
	    kp->kp_ctr.c_recv >= REJECT_AFTER_MESSAGES)
		goto error;

	/* Decrypt, then validate the counter. We don't want to validate the
	 * counter before decrypting as we do not know the message is authentic
	 * prior to decryption. */
	if (chacha20poly1305_decrypt(buf, buf, buflen,
	    NULL, 0, nonce, kp->kp_recv) == 0)
		goto error;

	if (noise_counter_recv(&kp->kp_ctr, nonce) != 0)
		goto error;

	/* If we've received the handshake confirming data packet then move the
	 * next keypair into current. If we do slide the next keypair in, then
	 * we skip the REKEY_AFTER_TIME_RECV check. This is safe to do as a
	 * data packet can't confirm a session that we are an INITIATOR of. */
	if (kp == r->r_next) {
		if (kp == r->r_next && kp->kp_local_index == r_idx) {
			noise_remote_keypair_free(r, r->r_previous);
			r->r_previous = r->r_current;
			r->r_current = r->r_next;
			r->r_next = NULL;

			ret = ECONNRESET;
			goto error;
		}
	}

	/* Similar to when we encrypt, we want to notify the caller when we
	 * are approaching our tolerances. We notify if:
	 *  - we're the initiator and the current keypair is older than
	 *    REKEY_AFTER_TIME_RECV seconds. */
	ret = ESTALE;
	kp = r->r_current;
	if (kp != NULL &&
	    kp->kp_valid &&
	    kp->kp_is_initiator &&
	    noise_timer_expired(&kp->kp_birthdate, REKEY_AFTER_TIME_RECV, 0))
		goto error;

	ret = 0;

error:
	mtx_leave(&r->r_keypair_mtx);
	return ret;
}

/* Private functions - these should not be called outside this file under any
 * circumstances. */
static struct noise_keypair *
noise_remote_keypair_allocate(struct noise_remote *r)
{
	struct noise_keypair *kp;
	kp = SLIST_FIRST(&r->r_unused_keypairs);
	SLIST_REMOVE_HEAD(&r->r_unused_keypairs, kp_entry);
	return kp;
}

static void
noise_remote_keypair_free(struct noise_remote *r, struct noise_keypair *kp)
{
	struct noise_upcall *u = &r->r_local->l_upcall;
	if (kp != NULL) {
		SLIST_INSERT_HEAD(&r->r_unused_keypairs, kp, kp_entry);
		u->u_index_drop(u->u_arg, kp->kp_local_index);
		bzero(kp->kp_send, sizeof(kp->kp_send));
		bzero(kp->kp_recv, sizeof(kp->kp_recv));
	}
}

static uint32_t
noise_remote_handshake_index_get(struct noise_remote *r)
{
	struct noise_upcall *u = &r->r_local->l_upcall;
	return u->u_index_set(u->u_arg, r);
}

static void
noise_remote_handshake_index_drop(struct noise_remote *r)
{
	struct noise_handshake *hs = &r->r_handshake;
	struct noise_upcall *u = &r->r_local->l_upcall;
	rw_assert_wrlock(&r->r_handshake_lock);
	if (hs->hs_state != HS_ZEROED)
		u->u_index_drop(u->u_arg, hs->hs_local_index);
}

static uint64_t
noise_counter_send(struct noise_counter *ctr)
{
#ifdef __LP64__
	return atomic_inc_long_nv((u_long *)&ctr->c_send) - 1;
#else
	uint64_t ret;
	mtx_enter(&ctr->c_mtx);
	ret = ctr->c_send++;
	mtx_leave(&ctr->c_mtx);
	return ret;
#endif
}

static int
noise_counter_recv(struct noise_counter *ctr, uint64_t recv)
{
	uint64_t i, top, index_recv, index_ctr;
	unsigned long bit;
	int ret = EEXIST;

	mtx_enter(&ctr->c_mtx);

	/* Check that the recv counter is valid */
	if (ctr->c_recv >= REJECT_AFTER_MESSAGES ||
	    recv >= REJECT_AFTER_MESSAGES)
		goto error;

	/* If the packet is out of the window, invalid */
	if (recv + COUNTER_WINDOW_SIZE < ctr->c_recv)
		goto error;

	/* If the new counter is ahead of the current counter, we'll need to
	 * zero out the bitmap that has previously been used */
	index_recv = recv / COUNTER_BITS;
	index_ctr = ctr->c_recv / COUNTER_BITS;

	if (recv > ctr->c_recv) {
		top = MIN(index_recv - index_ctr, COUNTER_NUM);
		for (i = 1; i <= top; i++)
			ctr->c_backtrack[
			    (i + index_ctr) & (COUNTER_NUM - 1)] = 0;
		ctr->c_recv = recv;
	}

	index_recv %= COUNTER_NUM;
	bit = 1ul << (recv % COUNTER_BITS);

	if (ctr->c_backtrack[index_recv] & bit)
		goto error;

	ctr->c_backtrack[index_recv] |= bit;

	ret = 0;
error:
	mtx_leave(&ctr->c_mtx);
	return ret;
}

static void
noise_kdf(uint8_t *a, uint8_t *b, uint8_t *c, const uint8_t *x,
    size_t a_len, size_t b_len, size_t c_len, size_t x_len,
    const uint8_t ck[NOISE_HASH_LEN])
{
	uint8_t out[BLAKE2S_HASH_SIZE + 1];
	uint8_t sec[BLAKE2S_HASH_SIZE];

	KASSERT(a_len <= BLAKE2S_HASH_SIZE && b_len <= BLAKE2S_HASH_SIZE &&
			c_len <= BLAKE2S_HASH_SIZE);
	KASSERT(!(b || b_len || c || c_len) || (a && a_len));
	KASSERT(!(c || c_len) || (b && b_len));

	/* Extract entropy from "x" into sec */
	blake2s_hmac(sec, x, ck, BLAKE2S_HASH_SIZE, x_len, NOISE_HASH_LEN);

	if (a == NULL || a_len == 0)
		goto out;

	/* Expand first key: key = sec, data = 0x1 */
	out[0] = 1;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE, 1, BLAKE2S_HASH_SIZE);
	memcpy(a, out, a_len);

	if (b == NULL || b_len == 0)
		goto out;

	/* Expand second key: key = sec, data = "a" || 0x2 */
	out[BLAKE2S_HASH_SIZE] = 2;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE, BLAKE2S_HASH_SIZE + 1,
			BLAKE2S_HASH_SIZE);
	memcpy(b, out, b_len);

	if (c == NULL || c_len == 0)
		goto out;

	/* Expand third key: key = sec, data = "b" || 0x3 */
	out[BLAKE2S_HASH_SIZE] = 3;
	blake2s_hmac(out, out, sec, BLAKE2S_HASH_SIZE, BLAKE2S_HASH_SIZE + 1,
			BLAKE2S_HASH_SIZE);
	memcpy(c, out, c_len);

out:
	/* Clear sensitive data from stack */
	explicit_bzero(sec, BLAKE2S_HASH_SIZE);
	explicit_bzero(out, BLAKE2S_HASH_SIZE + 1);
}

static int
noise_mix_dh(uint8_t ck[NOISE_HASH_LEN], uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
    const uint8_t private[NOISE_PUBLIC_KEY_LEN],
    const uint8_t public[NOISE_PUBLIC_KEY_LEN])
{
	uint8_t dh[NOISE_PUBLIC_KEY_LEN];

	if (!curve25519(dh, private, public))
		return EINVAL;
	noise_kdf(ck, key, NULL, dh,
	    NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN, 0, NOISE_PUBLIC_KEY_LEN, ck);
	explicit_bzero(dh, NOISE_PUBLIC_KEY_LEN);
	return 0;
}

static int
noise_mix_ss(uint8_t ck[NOISE_HASH_LEN], uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
    const uint8_t ss[NOISE_PUBLIC_KEY_LEN])
{
	static uint8_t null_point[NOISE_PUBLIC_KEY_LEN];
	if (timingsafe_bcmp(ss, null_point, NOISE_PUBLIC_KEY_LEN) == 0)
		return ENOENT;
	noise_kdf(ck, key, NULL, ss,
	    NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN, 0, NOISE_PUBLIC_KEY_LEN, ck);
	return 0;
}

static void
noise_mix_hash(uint8_t hash[NOISE_HASH_LEN], const uint8_t *src,
    size_t src_len)
{
	struct blake2s_state blake;

	blake2s_init(&blake, NOISE_HASH_LEN);
	blake2s_update(&blake, hash, NOISE_HASH_LEN);
	blake2s_update(&blake, src, src_len);
	blake2s_final(&blake, hash);
}

static void
noise_mix_psk(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
    uint8_t key[NOISE_SYMMETRIC_KEY_LEN],
    const uint8_t psk[NOISE_SYMMETRIC_KEY_LEN])
{
	uint8_t tmp[NOISE_HASH_LEN];

	noise_kdf(ck, tmp, key, psk,
	    NOISE_HASH_LEN, NOISE_HASH_LEN, NOISE_SYMMETRIC_KEY_LEN,
	    NOISE_SYMMETRIC_KEY_LEN, ck);
	noise_mix_hash(hash, tmp, NOISE_HASH_LEN);
	explicit_bzero(tmp, NOISE_HASH_LEN);
}

static void
noise_param_init(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
    const uint8_t s[NOISE_PUBLIC_KEY_LEN])
{
	struct blake2s_state blake;

	blake2s(ck, (uint8_t *)NOISE_HANDSHAKE_NAME, NULL,
	    NOISE_HASH_LEN, strlen(NOISE_HANDSHAKE_NAME), 0);
	blake2s_init(&blake, NOISE_HASH_LEN);
	blake2s_update(&blake, ck, NOISE_HASH_LEN);
	blake2s_update(&blake, (uint8_t *)NOISE_IDENTIFIER_NAME,
	    strlen(NOISE_IDENTIFIER_NAME));
	blake2s_final(&blake, hash);

	noise_mix_hash(hash, s, NOISE_PUBLIC_KEY_LEN);
}

static void
noise_msg_encrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
    uint8_t key[NOISE_SYMMETRIC_KEY_LEN], uint8_t hash[NOISE_HASH_LEN])
{
	/* Nonce always zero for Noise_IK */
	chacha20poly1305_encrypt(dst, src, src_len,
	    hash, NOISE_HASH_LEN, 0, key);
	noise_mix_hash(hash, dst, src_len + NOISE_AUTHTAG_LEN);
}

static int
noise_msg_decrypt(uint8_t *dst, const uint8_t *src, size_t src_len,
    uint8_t key[NOISE_SYMMETRIC_KEY_LEN], uint8_t hash[NOISE_HASH_LEN])
{
	/* Nonce always zero for Noise_IK */
	if (!chacha20poly1305_decrypt(dst, src, src_len,
	    hash, NOISE_HASH_LEN, 0, key))
		return EINVAL;
	noise_mix_hash(hash, src, src_len);
	return 0;
}

static void
noise_msg_ephemeral(uint8_t ck[NOISE_HASH_LEN], uint8_t hash[NOISE_HASH_LEN],
    const uint8_t src[NOISE_PUBLIC_KEY_LEN])
{
	noise_mix_hash(hash, src, NOISE_PUBLIC_KEY_LEN);
	noise_kdf(ck, NULL, NULL, src, NOISE_HASH_LEN, 0, 0,
		  NOISE_PUBLIC_KEY_LEN, ck);
}

static void
noise_tai64n_now(uint8_t output[NOISE_TIMESTAMP_LEN])
{
	struct timespec time;
	uint64_t sec;
	uint32_t nsec;

	getnanotime(&time);

	/* Round down the nsec counter to limit precise timing leak. */
	time.tv_nsec &= REJECT_INTERVAL_MASK;

	/* https://cr.yp.to/libtai/tai64.html */
	sec = htobe64(0x400000000000000aULL + time.tv_sec);
	nsec = htobe32(time.tv_nsec);

	/* memcpy to output buffer, assuming output could be unaligned. */
	memcpy(output, &sec, sizeof(sec));
	memcpy(output + sizeof(sec), &nsec, sizeof(nsec));
}

static int
noise_timer_expired(struct timespec *birthdate, time_t sec, long nsec)
{
	struct timespec uptime;
	struct timespec expire = { .tv_sec = sec, .tv_nsec = nsec };

	/* We don't really worry about a zeroed birthdate, to avoid the extra
	 * check on every encrypt/decrypt. This does mean that r_last_init
	 * check may fail if getnanouptime is < REJECT_INTERVAL from 0. */

	getnanouptime(&uptime);
	timespecadd(birthdate, &expire, &expire);
	return timespeccmp(&uptime, &expire, >) ? ETIMEDOUT : 0;
}

#ifdef WGTEST

#define MESSAGE_LEN 64
#define LARGE_MESSAGE_LEN 1420

#define T_LIM (COUNTER_WINDOW_SIZE + 1)
#define T_INIT do {				\
	bzero(&ctr, sizeof(ctr));		\
	mtx_init_flags(&ctr.c_mtx, IPL_NET, "counter", 0);	\
} while (0)
#define T(num, v, e) do {						\
	if (noise_counter_recv(&ctr, v) != e) {				\
		printf("%s, test %d: failed.\n", __func__, num);	\
		return;							\
	}								\
} while (0)
#define T_FAILED(test) do {				\
	printf("%s %s: failed\n", __func__, test);	\
	return;						\
} while (0)
#define T_PASSED printf("%s: passed.\n", __func__)

static struct noise_local	al, bl;
static struct noise_remote	ar, br;

static struct noise_initiation {
	uint32_t s_idx;
	uint8_t ue[NOISE_PUBLIC_KEY_LEN];
	uint8_t es[NOISE_PUBLIC_KEY_LEN + NOISE_AUTHTAG_LEN];
	uint8_t ets[NOISE_TIMESTAMP_LEN + NOISE_AUTHTAG_LEN];
} init;

static struct noise_response {
	uint32_t s_idx;
	uint32_t r_idx;
	uint8_t ue[NOISE_PUBLIC_KEY_LEN];
	uint8_t en[0 + NOISE_AUTHTAG_LEN];
} resp;

static uint64_t nonce;
static uint32_t index;
static uint8_t data[MESSAGE_LEN + NOISE_AUTHTAG_LEN];
static uint8_t largedata[LARGE_MESSAGE_LEN + NOISE_AUTHTAG_LEN];

static struct noise_remote *
upcall_get(void *x0, uint8_t *x1) { return x0; }
static uint32_t
upcall_set(void *x0, struct noise_remote *x1) { return 5; }
static void
upcall_drop(void *x0, uint32_t x1) { }

static void
noise_counter_test()
{
	struct noise_counter ctr;
	int i;

	T_INIT;
	/* T(test number, nonce, expected_response) */
	T( 1, 0, 0);
	T( 2, 1, 0);
	T( 3, 1, EEXIST);
	T( 4, 9, 0);
	T( 5, 8, 0);
	T( 6, 7, 0);
	T( 7, 7, EEXIST);
	T( 8, T_LIM, 0);
	T( 9, T_LIM - 1, 0);
	T(10, T_LIM - 1, EEXIST);
	T(11, T_LIM - 2, 0);
	T(12, 2, 0);
	T(13, 2, EEXIST);
	T(14, T_LIM + 16, 0);
	T(15, 3, EEXIST);
	T(16, T_LIM + 16, EEXIST);
	T(17, T_LIM * 4, 0);
	T(18, T_LIM * 4 - (T_LIM - 1), 0);
	T(19, 10, EEXIST);
	T(20, T_LIM * 4 - T_LIM, EEXIST);
	T(21, T_LIM * 4 - (T_LIM + 1), EEXIST);
	T(22, T_LIM * 4 - (T_LIM - 2), 0);
	T(23, T_LIM * 4 + 1 - T_LIM, EEXIST);
	T(24, 0, EEXIST);
	T(25, REJECT_AFTER_MESSAGES, EEXIST);
	T(26, REJECT_AFTER_MESSAGES - 1, 0);
	T(27, REJECT_AFTER_MESSAGES, EEXIST);
	T(28, REJECT_AFTER_MESSAGES - 1, EEXIST);
	T(29, REJECT_AFTER_MESSAGES - 2, 0);
	T(30, REJECT_AFTER_MESSAGES + 1, EEXIST);
	T(31, REJECT_AFTER_MESSAGES + 2, EEXIST);
	T(32, REJECT_AFTER_MESSAGES - 2, EEXIST);
	T(33, REJECT_AFTER_MESSAGES - 3, 0);
	T(34, 0, EEXIST);

	T_INIT;
	for (i = 1; i <= COUNTER_WINDOW_SIZE; ++i)
		T(35, i, 0);
	T(36, 0, 0);
	T(37, 0, EEXIST);

	T_INIT;
	for (i = 2; i <= COUNTER_WINDOW_SIZE + 1; ++i)
		T(38, i, 0);
	T(39, 1, 0);
	T(40, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 0;)
		T(41, i, 0);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 2; i-- > 1;)
		T(42, i, 0);
	T(43, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(44, i, 0);
	T(45, COUNTER_WINDOW_SIZE + 1, 0);
	T(46, 0, EEXIST);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(47, i, 0);
	T(48, 0, 0);
	T(49, COUNTER_WINDOW_SIZE + 1, 0);

	T_PASSED;
}

static void
noise_handshake_init(struct noise_local *al, struct noise_remote *ar,
    struct noise_local *bl, struct noise_remote *br)
{
	uint8_t apriv[NOISE_PUBLIC_KEY_LEN], bpriv[NOISE_PUBLIC_KEY_LEN];
	uint8_t apub[NOISE_PUBLIC_KEY_LEN], bpub[NOISE_PUBLIC_KEY_LEN];
	uint8_t psk[NOISE_SYMMETRIC_KEY_LEN];

	struct noise_upcall upcall = {
		.u_arg = NULL,
		.u_remote_get = upcall_get,
		.u_index_set = upcall_set,
		.u_index_drop = upcall_drop,
	};

	upcall.u_arg = ar;
	noise_local_init(al, &upcall);
	upcall.u_arg = br;
	noise_local_init(bl, &upcall);

	arc4random_buf(apriv, NOISE_PUBLIC_KEY_LEN);
	arc4random_buf(bpriv, NOISE_PUBLIC_KEY_LEN);

	noise_local_lock_identity(al);
	noise_local_set_private(al, apriv);
	noise_local_unlock_identity(al);

	noise_local_lock_identity(bl);
	noise_local_set_private(bl, bpriv);
	noise_local_unlock_identity(bl);

	noise_local_keys(al, apub, NULL);
	noise_local_keys(bl, bpub, NULL);

	noise_remote_init(ar, bpub, al);
	noise_remote_init(br, apub, bl);

	arc4random_buf(psk, NOISE_SYMMETRIC_KEY_LEN);
	noise_remote_set_psk(ar, psk);
	noise_remote_set_psk(br, psk);
}

static void
noise_handshake_test()
{
	struct noise_remote *r;
	int i;

	noise_handshake_init(&al, &ar, &bl, &br);

	/* Create initiation */
	if (noise_create_initiation(&ar, &init.s_idx,
	    init.ue, init.es, init.ets) != 0)
		T_FAILED("create_initiation");

	/* Check encrypted (es) validation */
	for (i = 0; i < sizeof(init.es); i++) {
		init.es[i] = ~init.es[i];
		if (noise_consume_initiation(&bl, &r, init.s_idx,
		    init.ue, init.es, init.ets) != EINVAL)
			T_FAILED("consume_initiation_es");
		init.es[i] = ~init.es[i];
	}

	/* Check encrypted (ets) validation */
	for (i = 0; i < sizeof(init.ets); i++) {
		init.ets[i] = ~init.ets[i];
		if (noise_consume_initiation(&bl, &r, init.s_idx,
		    init.ue, init.es, init.ets) != EINVAL)
			T_FAILED("consume_initiation_ets");
		init.ets[i] = ~init.ets[i];
	}

	/* Consume initiation properly */
	if (noise_consume_initiation(&bl, &r, init.s_idx,
	    init.ue, init.es, init.ets) != 0)
		T_FAILED("consume_initiation");
	if (r != &br)
		T_FAILED("remote_lookup");

	/* Replay initiation */
	if (noise_consume_initiation(&bl, &r, init.s_idx,
	    init.ue, init.es, init.ets) != EINVAL)
		T_FAILED("consume_initiation_replay");
	if (r != &br)
		T_FAILED("remote_lookup_r_unchanged");

	/* Create response */
	if (noise_create_response(&br, &resp.s_idx,
	    &resp.r_idx, resp.ue, resp.en) != 0)
		T_FAILED("create_response");

	/* Check encrypted (en) validation */
	for (i = 0; i < sizeof(resp.en); i++) {
		resp.en[i] = ~resp.en[i];
		if (noise_consume_response(&ar, resp.s_idx,
		    resp.r_idx, resp.ue, resp.en) != EINVAL)
			T_FAILED("consume_response_en");
		resp.en[i] = ~resp.en[i];
	}

	/* Consume response properly */
	if (noise_consume_response(&ar, resp.s_idx,
	    resp.r_idx, resp.ue, resp.en) != 0)
		T_FAILED("consume_response");

	/* Derive keys on both sides */
	if (noise_remote_begin_session(&ar) != 0)
		T_FAILED("promote_ar");
	if (noise_remote_begin_session(&br) != 0)
		T_FAILED("promote_br");

	for (i = 0; i < MESSAGE_LEN; i++)
		data[i] = i;

	/* Since bob is responder, he must not encrypt until confirmed */
	if (noise_remote_encrypt(&br, &index, &nonce,
	    data, MESSAGE_LEN) != EINVAL)
		T_FAILED("encrypt_kci_wait");

	/* Alice now encrypt and gets bob to decrypt */
	if (noise_remote_encrypt(&ar, &index, &nonce,
	    data, MESSAGE_LEN) != 0)
		T_FAILED("encrypt_akp");
	if (noise_remote_decrypt(&br, index, nonce,
	    data, MESSAGE_LEN + NOISE_AUTHTAG_LEN) != ECONNRESET)
		T_FAILED("decrypt_bkp");

	for (i = 0; i < MESSAGE_LEN; i++)
		if (data[i] != i)
			T_FAILED("decrypt_message_akp_bkp");

	/* Now bob has received confirmation, he can encrypt */
	if (noise_remote_encrypt(&br, &index, &nonce,
	    data, MESSAGE_LEN) != 0)
		T_FAILED("encrypt_kci_ready");
	if (noise_remote_decrypt(&ar, index, nonce,
	    data, MESSAGE_LEN + NOISE_AUTHTAG_LEN) != 0)
		T_FAILED("decrypt_akp");

	for (i = 0; i < MESSAGE_LEN; i++)
		if (data[i] != i)
			T_FAILED("decrypt_message_bkp_akp");

	T_PASSED;
}

static void
noise_speed_test()
{
#define SPEED_ITER (1<<16)
	struct timespec start, end;
	struct noise_remote *r;
	int nsec, i;

#define NSEC 1000000000
#define T_TIME_START(iter, size) do {					\
	printf("%s %d %d byte encryptions\n", __func__, iter, size);	\
	nanouptime(&start);						\
} while (0)
#define T_TIME_END(iter, size) do {					\
	nanouptime(&end);						\
	timespecsub(&end, &start, &end);				\
	nsec = (end.tv_sec * NSEC + end.tv_nsec) / iter;		\
	printf("%s %d nsec/iter, %d iter/sec, %d byte/sec\n",		\
	    __func__, nsec, NSEC / nsec, NSEC / nsec * size);		\
} while (0)
#define T_TIME_START_SINGLE(name) do {		\
	printf("%s %s\n", __func__, name);	\
	nanouptime(&start);			\
} while (0)
#define T_TIME_END_SINGLE() do {					\
	nanouptime(&end);						\
	timespecsub(&end, &start, &end);				\
	nsec = (end.tv_sec * NSEC + end.tv_nsec);			\
	printf("%s %d nsec/iter, %d iter/sec\n",			\
	    __func__, nsec, NSEC / nsec);				\
} while (0)

	noise_handshake_init(&al, &ar, &bl, &br);

	T_TIME_START_SINGLE("create_initiation");
	if (noise_create_initiation(&ar, &init.s_idx,
	    init.ue, init.es, init.ets) != 0)
		T_FAILED("create_initiation");
	T_TIME_END_SINGLE();

	T_TIME_START_SINGLE("consume_initiation");
	if (noise_consume_initiation(&bl, &r, init.s_idx,
	    init.ue, init.es, init.ets) != 0)
		T_FAILED("consume_initiation");
	T_TIME_END_SINGLE();

	T_TIME_START_SINGLE("create_response");
	if (noise_create_response(&br, &resp.s_idx,
	    &resp.r_idx, resp.ue, resp.en) != 0)
		T_FAILED("create_response");
	T_TIME_END_SINGLE();

	T_TIME_START_SINGLE("consume_response");
	if (noise_consume_response(&ar, resp.s_idx,
	    resp.r_idx, resp.ue, resp.en) != 0)
		T_FAILED("consume_response");
	T_TIME_END_SINGLE();

	/* Derive keys on both sides */
	T_TIME_START_SINGLE("derive_keys");
	if (noise_remote_begin_session(&ar) != 0)
		T_FAILED("begin_ar");
	T_TIME_END_SINGLE();
	if (noise_remote_begin_session(&br) != 0)
		T_FAILED("begin_br");

	/* Small data encryptions */
	T_TIME_START(SPEED_ITER, MESSAGE_LEN);
	for (i = 0; i < SPEED_ITER; i++) {
		if (noise_remote_encrypt(&ar, &index, &nonce,
		    data, MESSAGE_LEN) != 0)
			T_FAILED("encrypt_akp");
	}
	T_TIME_END(SPEED_ITER, MESSAGE_LEN);


	/* Large data encryptions */
	T_TIME_START(SPEED_ITER, LARGE_MESSAGE_LEN);
	for (i = 0; i < SPEED_ITER; i++) {
		if (noise_remote_encrypt(&ar, &index, &nonce,
		    largedata, LARGE_MESSAGE_LEN) != 0)
			T_FAILED("encrypt_akp");
	}
	T_TIME_END(SPEED_ITER, LARGE_MESSAGE_LEN);
}

void
noise_test()
{
	noise_counter_test();
	noise_handshake_test();
	noise_speed_test();
}

#endif /* WGTEST */
