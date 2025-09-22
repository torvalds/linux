/*	$OpenBSD: softraid.c,v 1.7 2024/04/25 18:31:49 kn Exp $	*/

/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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

#include <sys/param.h>
#include <sys/queue.h>

#include <dev/biovar.h>
#include <dev/softraidvar.h>

#include <lib/libsa/bcrypt_pbkdf.h>
#include <lib/libsa/hmac_sha1.h>
#include <lib/libsa/pkcs5_pbkdf2.h>
#include <lib/libsa/rijndael.h>

#include "stand.h"
#include "softraid.h"

#define RIJNDAEL128_BLOCK_LEN     16
#define PASSPHRASE_LENGTH 1024

#define SR_CRYPTO_KEYBLOCK_BYTES SR_CRYPTO_MAXKEYS * SR_CRYPTO_KEYBYTES

/* List of softraid volumes. */
struct sr_boot_volume_head sr_volumes;

/* List of softraid keydisks. */
struct sr_boot_keydisk_head sr_keydisks;

#ifdef DEBUG
void
printhex(const char *s, const u_int8_t *buf, size_t len)
{
	u_int8_t n1, n2;
	size_t i;

	printf("%s: ", s);
	for (i = 0; i < len; i++) {
		n1 = buf[i] & 0x0f;
		n2 = buf[i] >> 4;
		printf("%c", n2 > 9 ? n2 + 'a' - 10 : n2 + '0');
		printf("%c", n1 > 9 ? n1 + 'a' - 10 : n1 + '0');
	}
	printf("\n");
}
#endif

void
sr_clear_keys(void)
{
	struct sr_boot_volume *bv;
	struct sr_boot_keydisk *kd, *nkd;

	SLIST_FOREACH(bv, &sr_volumes, sbv_link) {
		if (bv->sbv_level != 'C' && bv->sbv_level != 0x1C)
			continue;
		if (bv->sbv_keys != NULL) {
			explicit_bzero(bv->sbv_keys, SR_CRYPTO_KEYBLOCK_BYTES);
			free(bv->sbv_keys, SR_CRYPTO_KEYBLOCK_BYTES);
			bv->sbv_keys = NULL;
		}
		if (bv->sbv_maskkey != NULL) {
			explicit_bzero(bv->sbv_maskkey, SR_CRYPTO_MAXKEYBYTES);
			free(bv->sbv_maskkey, SR_CRYPTO_MAXKEYBYTES);
			bv->sbv_maskkey = NULL;
		}
	}
	SLIST_FOREACH_SAFE(kd, &sr_keydisks, kd_link, nkd) {
		explicit_bzero(kd, sizeof(*kd));
		free(kd, sizeof(*kd));
	}
}

void
sr_crypto_calculate_check_hmac_sha1(u_int8_t *maskkey, int maskkey_size,
    u_int8_t *key, int key_size, u_char *check_digest)
{
	u_int8_t check_key[SHA1_DIGEST_LENGTH];
	SHA1_CTX shactx;

	explicit_bzero(check_key, sizeof(check_key));
	explicit_bzero(&shactx, sizeof(shactx));

	/* k = SHA1(mask_key) */
	SHA1Init(&shactx);
	SHA1Update(&shactx, maskkey, maskkey_size);
	SHA1Final(check_key, &shactx);

	/* mac = HMAC_SHA1_k(unencrypted key) */
	hmac_sha1(key, key_size, check_key, sizeof(check_key), check_digest);

	explicit_bzero(check_key, sizeof(check_key));
	explicit_bzero(&shactx, sizeof(shactx));
}

static int
sr_crypto_decrypt_keys(struct sr_meta_crypto *cm,
    struct sr_crypto_kdfinfo *kdfinfo, u_int8_t *kp)
{
	u_int8_t digest[SHA1_DIGEST_LENGTH];
	rijndael_ctx ctx;
	u_int8_t *cp;
	int rv = -1;
	int i;

	if (rijndael_set_key(&ctx, kdfinfo->maskkey, 256) != 0)
		goto done;

	cp = (u_int8_t *)cm->scm_key;
	for (i = 0; i < SR_CRYPTO_KEYBLOCK_BYTES; i += RIJNDAEL128_BLOCK_LEN)
		rijndael_decrypt(&ctx, (u_char *)(cp + i), (u_char *)(kp + i));

	/* Check that the key decrypted properly. */
	sr_crypto_calculate_check_hmac_sha1(kdfinfo->maskkey,
	    sizeof(kdfinfo->maskkey), kp, SR_CRYPTO_KEYBLOCK_BYTES, digest);

	if (bcmp(digest, cm->chk_hmac_sha1.sch_mac, sizeof(digest)) == 0)
		rv = 0;

 done:
	explicit_bzero(digest, sizeof(digest));

	return rv;
}

static int
sr_crypto_passphrase_decrypt(struct sr_meta_crypto *cm,
    struct sr_crypto_kdfinfo *kdfinfo, u_int8_t *kp)
{
	char passphrase[PASSPHRASE_LENGTH];
	struct sr_crypto_pbkdf *kdfhint;
	int rv = -1;
	int c, i;

	kdfhint = (struct sr_crypto_pbkdf *)&cm->scm_kdfhint;

	for (;;) {
		printf("Passphrase: ");
#ifdef IDLE_POWEROFF
extern int idle_poweroff(void);
		idle_poweroff();
#endif /* IDLE_POWEROFF */
		for (i = 0; i < PASSPHRASE_LENGTH - 1; i++) {
			c = cngetc();
			if (c == '\r' || c == '\n') {
				break;
			} else if (c == '\b') {
				i = i > 0 ? i - 2 : -1;
				continue;
			}
			passphrase[i] = (c & 0xff);
		}
		passphrase[i] = 0;
		printf("\n");

		/* Abort on an empty passphrase. */
		if (i == 0) {
			printf("aborting...\n");
			goto done;
		}

#ifdef DEBUG
		printf("Got passphrase: %s with len %d\n",
		    passphrase, strlen(passphrase));
#endif

		switch (kdfhint->generic.type) {
		case SR_CRYPTOKDFT_PKCS5_PBKDF2:
			if (pkcs5_pbkdf2(passphrase, strlen(passphrase),
			    kdfhint->salt, sizeof(kdfhint->salt),
			    kdfinfo->maskkey, sizeof(kdfinfo->maskkey),
			    kdfhint->rounds) != 0) {
				printf("pkcs5_pbkdf2 failed\n");
				goto done;
			}
			break;

		case SR_CRYPTOKDFT_BCRYPT_PBKDF:
			if (bcrypt_pbkdf(passphrase, strlen(passphrase),
			    kdfhint->salt, sizeof(kdfhint->salt),
			    kdfinfo->maskkey, sizeof(kdfinfo->maskkey),
			    kdfhint->rounds) != 0) {
				printf("bcrypt_pbkdf failed\n");
				goto done;
			}
			break;

		default:
			printf("unknown KDF type %u\n", kdfhint->generic.type);
			goto done;
		}

		if (sr_crypto_decrypt_keys(cm, kdfinfo, kp) == 0) {
			rv = 0;
			goto done;
		}

		printf("incorrect passphrase\n");
	}

 done:
	explicit_bzero(passphrase, PASSPHRASE_LENGTH);

	return rv;
}

int
sr_crypto_unlock_volume(struct sr_boot_volume *bv)
{
	struct sr_meta_crypto *cm;
	struct sr_boot_keydisk *kd;
	struct sr_meta_opt_item *omi;
	struct sr_crypto_pbkdf *kdfhint;
	struct sr_crypto_kdfinfo kdfinfo;
	u_int8_t *keys = NULL;
	int rv = -1;

	SLIST_FOREACH(omi, &bv->sbv_meta_opt, omi_link)
		if (omi->omi_som->som_type == SR_OPT_CRYPTO)
			break;

	if (omi == NULL) {
		printf("crypto metadata not found!\n");
		goto done;
	}

	cm = (struct sr_meta_crypto *)omi->omi_som;
	kdfhint = (struct sr_crypto_pbkdf *)&cm->scm_kdfhint;

	switch (cm->scm_mask_alg) {
	case SR_CRYPTOM_AES_ECB_256:
		break;
	default:
		printf("unsupported encryption algorithm %u\n",
		    cm->scm_mask_alg);
		goto done;
	}

	keys = alloc(SR_CRYPTO_KEYBLOCK_BYTES);
	bzero(keys, SR_CRYPTO_KEYBLOCK_BYTES);

	switch (kdfhint->generic.type) {
	case SR_CRYPTOKDFT_KEYDISK:
		SLIST_FOREACH(kd, &sr_keydisks, kd_link) {
			if (bcmp(&kd->kd_uuid, &bv->sbv_uuid,
			    sizeof(kd->kd_uuid)) == 0)
				break;
		}
		if (kd == NULL) {
			printf("keydisk not found\n");
			goto done;
		}
		bcopy(&kd->kd_key, &kdfinfo.maskkey, sizeof(kdfinfo.maskkey));
		if (sr_crypto_decrypt_keys(cm, &kdfinfo, keys) != 0) {
			printf("incorrect keydisk\n");
			goto done;
		}
		break;

	case SR_CRYPTOKDFT_BCRYPT_PBKDF:
	case SR_CRYPTOKDFT_PKCS5_PBKDF2:
		if (sr_crypto_passphrase_decrypt(cm, &kdfinfo, keys) != 0)
			goto done;
		break;

	default:
		printf("unknown KDF type %u\n", kdfhint->generic.type);
		goto done;
	}

	/* Keys and keydisks will be cleared before boot and from _rtt. */
	bv->sbv_keys = keys;
	bv->sbv_maskkey = alloc(sizeof(kdfinfo.maskkey));
	bcopy(&kdfinfo.maskkey, bv->sbv_maskkey, sizeof(kdfinfo.maskkey));

	rv = 0;

 done:
	explicit_bzero(&kdfinfo, sizeof(kdfinfo));

	if (keys != NULL && rv != 0) {
		explicit_bzero(keys, SR_CRYPTO_KEYBLOCK_BYTES);
		free(keys, SR_CRYPTO_KEYBLOCK_BYTES);
	}

	return (rv);
}
