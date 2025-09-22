/*	$OpenBSD: dh.c,v 1.35 2025/04/30 03:51:42 tb Exp $	*/

/*
 * Copyright (c) 2010-2014 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <string.h>
#include <event.h>
#include <imsg.h>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/obj_mac.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/bn.h>

#include "dh.h"
#include "iked.h"
#include "crypto_api.h"

int	dh_init(struct dh_group *);
int	dh_getlen(struct dh_group *);
int	dh_secretlen(struct dh_group *);

/* MODP */
int	modp_init(struct dh_group *);
int	modp_getlen(struct dh_group *);
int	modp_create_exchange(struct dh_group *, uint8_t *);
int	modp_create_shared(struct dh_group *, uint8_t *, uint8_t *);

/* ECP */
int	ec_init(struct dh_group *);
int	ec_getlen(struct dh_group *);
int	ec_secretlen(struct dh_group *);
int	ec_create_exchange(struct dh_group *, uint8_t *);
int	ec_create_shared(struct dh_group *, uint8_t *, uint8_t *);

#define EC_POINT2RAW_FULL	0
#define EC_POINT2RAW_XONLY	1
int	ec_point2raw(struct dh_group *, const EC_POINT *, uint8_t *, size_t, int);
EC_POINT *
	ec_raw2point(struct dh_group *, uint8_t *, size_t);

/* curve25519 */
int	ec25519_init(struct dh_group *);
int	ec25519_getlen(struct dh_group *);
int	ec25519_create_exchange(struct dh_group *, uint8_t *);
int	ec25519_create_shared(struct dh_group *, uint8_t *, uint8_t *);

#define CURVE25519_SIZE 32	/* 256 bits */
struct curve25519_key {
	uint8_t		 secret[CURVE25519_SIZE];
	uint8_t		 public[CURVE25519_SIZE];
};
extern int crypto_scalarmult_curve25519(unsigned char a[CURVE25519_SIZE],
    const unsigned char b[CURVE25519_SIZE],
    const unsigned char c[CURVE25519_SIZE])
	__attribute__((__bounded__(__minbytes__, 1, CURVE25519_SIZE)))
	__attribute__((__bounded__(__minbytes__, 2, CURVE25519_SIZE)))
	__attribute__((__bounded__(__minbytes__, 3, CURVE25519_SIZE)));

/* SNTRUP761 with X25519 */
int	kemsx_init(struct dh_group *);
int	kemsx_getlen(struct dh_group *);
int	kemsx_create_exchange2(struct dh_group *, struct ibuf **, struct ibuf *);
int	kemsx_create_shared2(struct dh_group *, struct ibuf **, struct ibuf *);

struct kemsx_key {
	uint8_t		kemkey[crypto_kem_sntrup761_BYTES];
	uint8_t		secret[crypto_kem_sntrup761_SECRETKEYBYTES];
	uint8_t		public[crypto_kem_sntrup761_PUBLICKEYBYTES];
	uint8_t		initiator;
};

const struct group_id ike_groups[] = {
	{ GROUP_MODP, 1, 768,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A63A3620FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 2, 1024,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381"
	    "FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 5, 1536,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 14, 2048,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
	    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
	    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
	    "15728E5A8AACAA68FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 15, 3072,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
	    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
	    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
	    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
	    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
	    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
	    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
	    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
	    "43DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 16, 4096,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
	    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
	    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
	    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
	    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
	    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
	    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
	    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
	    "43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7"
	    "88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA"
	    "2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6"
	    "287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED"
	    "1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9"
	    "93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934063199"
	    "FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 17, 6144,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
	    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
	    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
	    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
	    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
	    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
	    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
	    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
	    "43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7"
	    "88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA"
	    "2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6"
	    "287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED"
	    "1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9"
	    "93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934028492"
	    "36C3FAB4D27C7026C1D4DCB2602646DEC9751E763DBA37BD"
	    "F8FF9406AD9E530EE5DB382F413001AEB06A53ED9027D831"
	    "179727B0865A8918DA3EDBEBCF9B14ED44CE6CBACED4BB1B"
	    "DB7F1447E6CC254B332051512BD7AF426FB8F401378CD2BF"
	    "5983CA01C64B92ECF032EA15D1721D03F482D7CE6E74FEF6"
	    "D55E702F46980C82B5A84031900B1C9E59E7C97FBEC7E8F3"
	    "23A97A7E36CC88BE0F1D45B7FF585AC54BD407B22B4154AA"
	    "CC8F6D7EBF48E1D814CC5ED20F8037E0A79715EEF29BE328"
	    "06A1D58BB7C5DA76F550AA3D8A1FBFF0EB19CCB1A313D55C"
	    "DA56C9EC2EF29632387FE8D76E3C0468043E8F663F4860EE"
	    "12BF2D5B0B7474D6E694F91E6DCC4024FFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_MODP, 18, 8192,
	    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
	    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
	    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
	    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
	    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
	    "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
	    "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
	    "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
	    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
	    "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
	    "15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64"
	    "ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7"
	    "ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B"
	    "F12FFA06D98A0864D87602733EC86A64521F2B18177B200C"
	    "BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31"
	    "43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7"
	    "88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA"
	    "2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6"
	    "287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED"
	    "1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9"
	    "93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934028492"
	    "36C3FAB4D27C7026C1D4DCB2602646DEC9751E763DBA37BD"
	    "F8FF9406AD9E530EE5DB382F413001AEB06A53ED9027D831"
	    "179727B0865A8918DA3EDBEBCF9B14ED44CE6CBACED4BB1B"
	    "DB7F1447E6CC254B332051512BD7AF426FB8F401378CD2BF"
	    "5983CA01C64B92ECF032EA15D1721D03F482D7CE6E74FEF6"
	    "D55E702F46980C82B5A84031900B1C9E59E7C97FBEC7E8F3"
	    "23A97A7E36CC88BE0F1D45B7FF585AC54BD407B22B4154AA"
	    "CC8F6D7EBF48E1D814CC5ED20F8037E0A79715EEF29BE328"
	    "06A1D58BB7C5DA76F550AA3D8A1FBFF0EB19CCB1A313D55C"
	    "DA56C9EC2EF29632387FE8D76E3C0468043E8F663F4860EE"
	    "12BF2D5B0B7474D6E694F91E6DBE115974A3926F12FEE5E4"
	    "38777CB6A932DF8CD8BEC4D073B931BA3BC832B68D9DD300"
	    "741FA7BF8AFC47ED2576F6936BA424663AAB639C5AE4F568"
	    "3423B4742BF1C978238F16CBE39D652DE3FDB8BEFC848AD9"
	    "22222E04A4037C0713EB57A81A23F0C73473FC646CEA306B"
	    "4BCBC8862F8385DDFA9D4B7FA2C087E879683303ED5BDD3A"
	    "062B3CF5B3A278A66D2A13F83F44F82DDF310EE074AB6A36"
	    "4597E899A0255DC164F31CC50846851DF9AB48195DED7EA1"
	    "B1D510BD7EE74D73FAF36BC31ECFA268359046F4EB879F92"
	    "4009438B481C6CD7889A002ED5EE382BC9190DA6FC026E47"
	    "9558E4475677E9AA9E3050E2765694DFC81F56E880B96E71"
	    "60C980DD98EDD3DFFFFFFFFFFFFFFFFF",
	    "02"
	},
	{ GROUP_ECP, 19, 256, NULL, NULL, NID_X9_62_prime256v1 },
	{ GROUP_ECP, 20, 384, NULL, NULL, NID_secp384r1 },
	{ GROUP_ECP, 21, 521, NULL, NULL, NID_secp521r1 },
	{ GROUP_ECP, 26, 224, NULL, NULL, NID_secp224r1 },
	{ GROUP_ECP, 27, 224, NULL, NULL, NID_brainpoolP224r1 },
	{ GROUP_ECP, 28, 256, NULL, NULL, NID_brainpoolP256r1 },
	{ GROUP_ECP, 29, 384, NULL, NULL, NID_brainpoolP384r1 },
	{ GROUP_ECP, 30, 512, NULL, NULL, NID_brainpoolP512r1 },
	{ GROUP_CURVE25519, 31, CURVE25519_SIZE * 8 },
	/* "Private use" extensions */
	/* PQC KEM */
	{ GROUP_SNTRUP761X25519, 1035,
	   (MAXIMUM(crypto_kem_sntrup761_PUBLICKEYBYTES,
	        crypto_kem_sntrup761_CIPHERTEXTBYTES) +
	    CURVE25519_SIZE) * 8 }
};

void
group_init(void)
{
	/* currently not used */
	return;
}

void
group_free(struct dh_group *group)
{
	if (group == NULL)
		return;
	if (group->dh != NULL)
		DH_free(group->dh);
	if (group->ec != NULL)
		EC_KEY_free(group->ec);
	freezero(group->curve25519, sizeof(struct curve25519_key));
	freezero(group->kemsx, sizeof(struct kemsx_key));
	group->spec = NULL;
	free(group);
}

struct dh_group *
group_get(uint32_t id)
{
	const struct group_id	*p;
	struct dh_group		*group;

	if ((p = group_getid(id)) == NULL)
		return (NULL);

	if ((group = calloc(1, sizeof(*group))) == NULL)
		return (NULL);

	group->id = id;
	group->spec = p;

	switch (p->type) {
	case GROUP_MODP:
		group->init = modp_init;
		group->getlen = modp_getlen;
		group->exchange = modp_create_exchange;
		group->shared = modp_create_shared;
		break;
	case GROUP_ECP:
		group->init = ec_init;
		group->getlen = ec_getlen;
		group->secretlen = ec_secretlen;
		group->exchange = ec_create_exchange;
		group->shared = ec_create_shared;
		break;
	case GROUP_CURVE25519:
		group->init = ec25519_init;
		group->getlen = ec25519_getlen;
		group->exchange = ec25519_create_exchange;
		group->shared = ec25519_create_shared;
		break;
	case GROUP_SNTRUP761X25519:
		group->init = kemsx_init;
		group->getlen = kemsx_getlen;
		group->exchange2 = kemsx_create_exchange2;
		group->shared2 = kemsx_create_shared2;
		break;
	default:
		group_free(group);
		return (NULL);
	}

	if (dh_init(group) != 0) {
		group_free(group);
		return (NULL);
	}

	return (group);
}

const struct group_id *
group_getid(uint32_t id)
{
	const struct group_id	*p = NULL;
	unsigned int		 i, items;

	items = sizeof(ike_groups) / sizeof(ike_groups[0]);
	for (i = 0; i < items; i++) {
		if (id == ike_groups[i].id) {
			p = &ike_groups[i];
			break;
		}
	}
	return (p);
}

int
dh_init(struct dh_group *group)
{
	return (group->init(group));
}

int
dh_getlen(struct dh_group *group)
{
	return (group->getlen(group));
}

int
dh_secretlen(struct dh_group *group)
{
	if (group->secretlen)
		return (group->secretlen(group));
	else
		return (group->getlen(group));
}

int
dh_create_exchange(struct dh_group *group, struct ibuf **bufp, struct ibuf *iexchange)
{
	struct ibuf *buf;

	*bufp = NULL;
	if (group->exchange2)
		return (group->exchange2(group, bufp, iexchange));
	buf = ibuf_new(NULL, dh_getlen(group));
	if (buf == NULL)
		return -1;
	*bufp = buf;
	return (group->exchange(group, ibuf_data(buf)));
}

int
dh_create_shared(struct dh_group *group, struct ibuf **secretp, struct ibuf *exchange)
{
	struct ibuf *buf;

	*secretp = NULL;
	if (group->shared2)
		return (group->shared2(group, secretp, exchange));
	if (exchange == NULL ||
	    (ssize_t)ibuf_size(exchange) != dh_getlen(group))
		return -1;
	buf = ibuf_new(NULL, dh_secretlen(group));
	if (buf == NULL)
		return -1;
	*secretp = buf;
	return (group->shared(group, ibuf_data(buf), ibuf_data(exchange)));
}

int
modp_init(struct dh_group *group)
{
	BIGNUM	*g = NULL, *p = NULL;
	DH	*dh;
	int	 ret = -1;

	if ((dh = DH_new()) == NULL)
		return (-1);

	if (!BN_hex2bn(&p, group->spec->prime) ||
	    !BN_hex2bn(&g, group->spec->generator) ||
	    DH_set0_pqg(dh, p, NULL, g) == 0)
		goto done;

	p = g = NULL;
	group->dh = dh;

	ret = 0;
 done:
	BN_clear_free(g);
	BN_clear_free(p);

	return (ret);
}

int
modp_getlen(struct dh_group *group)
{
	if (group->spec == NULL)
		return (0);
	return (roundup(group->spec->bits, 8) / 8);
}

int
modp_create_exchange(struct dh_group *group, uint8_t *buf)
{
	const BIGNUM	*pub;
	DH		*dh = group->dh;
	int		 len, ret;

	if (!DH_generate_key(dh))
		return (-1);
	DH_get0_key(group->dh, &pub, NULL);
	ret = BN_bn2bin(pub, buf);
	if (!ret)
		return (-1);

	len = dh_getlen(group);

	/* add zero padding */
	if (ret < len) {
		bcopy(buf, buf + (len - ret), ret);
		bzero(buf, len - ret);
	}

	return (0);
}

int
modp_create_shared(struct dh_group *group, uint8_t *secret, uint8_t *exchange)
{
	BIGNUM	*ex;
	int	 len, ret;

	len = dh_getlen(group);

	if ((ex = BN_bin2bn(exchange, len, NULL)) == NULL)
		return (-1);

	ret = DH_compute_key(secret, ex, group->dh);
	BN_clear_free(ex);
	if (ret <= 0)
		return (-1);

	/* add zero padding */
	if (ret < len) {
		bcopy(secret, secret + (len - ret), ret);
		bzero(secret, len - ret);
	}

	return (0);
}

int
ec_init(struct dh_group *group)
{
	if ((group->ec = EC_KEY_new_by_curve_name(group->spec->nid)) == NULL)
		return (-1);
	if (!EC_KEY_generate_key(group->ec))
		return (-1);
	if (!EC_KEY_check_key(group->ec))
		return (-1);
	return (0);
}

int
ec_getlen(struct dh_group *group)
{
	if (group->spec == NULL)
		return (0);
	/* NB:  Return value will always be even */
	return ((roundup(group->spec->bits, 8) * 2) / 8);
}

/*
 * Note that the shared secret only includes the x value:
 *
 * See RFC 5903, 7. ECP Key Exchange Data Formats:
 *   The Diffie-Hellman shared secret value consists of the x value of the
 *   Diffie-Hellman common value.
 * See also RFC 5903, 9. Changes from RFC 4753.
 */
int
ec_secretlen(struct dh_group *group)
{
	return (ec_getlen(group) / 2);
}

int
ec_create_exchange(struct dh_group *group, uint8_t *buf)
{
	size_t	 len;

	len = ec_getlen(group);
	bzero(buf, len);

	return (ec_point2raw(group, EC_KEY_get0_public_key(group->ec),
	    buf, len, EC_POINT2RAW_FULL));
}

int
ec_create_shared(struct dh_group *group, uint8_t *secret, uint8_t *exchange)
{
	const EC_GROUP	*ecgroup = NULL;
	const BIGNUM	*privkey;
	EC_KEY		*exkey = NULL;
	EC_POINT	*exchangep = NULL, *secretp = NULL;
	int		 ret = -1;

	if ((ecgroup = EC_KEY_get0_group(group->ec)) == NULL ||
	    (privkey = EC_KEY_get0_private_key(group->ec)) == NULL)
		goto done;

	if ((exchangep =
	    ec_raw2point(group, exchange, ec_getlen(group))) == NULL)
		goto done;

	if ((exkey = EC_KEY_new()) == NULL)
		goto done;
	if (!EC_KEY_set_group(exkey, ecgroup))
		goto done;
	if (!EC_KEY_set_public_key(exkey, exchangep))
		goto done;

	/* validate exchangep */
	if (!EC_KEY_check_key(exkey))
		goto done;

	if ((secretp = EC_POINT_new(ecgroup)) == NULL)
		goto done;

	if (!EC_POINT_mul(ecgroup, secretp, NULL, exchangep, privkey, NULL))
		goto done;

	ret = ec_point2raw(group, secretp, secret, ec_secretlen(group),
	    EC_POINT2RAW_XONLY);

 done:
	if (exkey != NULL)
		EC_KEY_free(exkey);
	if (exchangep != NULL)
		EC_POINT_clear_free(exchangep);
	if (secretp != NULL)
		EC_POINT_clear_free(secretp);

	return (ret);
}

int
ec_point2raw(struct dh_group *group, const EC_POINT *point,
    uint8_t *buf, size_t len, int mode)
{
	const EC_GROUP	*ecgroup = NULL;
	BN_CTX		*bnctx = NULL;
	BIGNUM		*x = NULL, *y = NULL;
	int		 ret = -1;
	size_t		 eclen, xlen, ylen;
	off_t		 xoff, yoff;

	if ((bnctx = BN_CTX_new()) == NULL)
		goto done;
	BN_CTX_start(bnctx);
	if ((x = BN_CTX_get(bnctx)) == NULL ||
	    (y = BN_CTX_get(bnctx)) == NULL)
		goto done;

	eclen = ec_getlen(group);
	switch (mode) {
	case EC_POINT2RAW_XONLY:
		xlen = eclen / 2;
		ylen = 0;
		break;
	case EC_POINT2RAW_FULL:
		xlen = ylen = eclen / 2;
		break;
	default:
		goto done;
	}
	if (len < xlen + ylen)
		goto done;

	if ((ecgroup = EC_KEY_get0_group(group->ec)) == NULL)
		goto done;

	if (!EC_POINT_get_affine_coordinates(ecgroup, point, x, y, bnctx))
		goto done;

	xoff = xlen - BN_num_bytes(x);
	bzero(buf, xoff);
	if (!BN_bn2bin(x, buf + xoff))
		goto done;

	if (ylen > 0) {
		yoff = (ylen - BN_num_bytes(y)) + xlen;
		bzero(buf + xlen, yoff - xlen);
		if (!BN_bn2bin(y, buf + yoff))
			goto done;
	}

	ret = 0;
 done:
	/* Make sure to erase sensitive data */
	if (x != NULL)
		BN_clear(x);
	if (y != NULL)
		BN_clear(y);
	BN_CTX_end(bnctx);
	BN_CTX_free(bnctx);

	return (ret);
}

EC_POINT *
ec_raw2point(struct dh_group *group, uint8_t *buf, size_t len)
{
	const EC_GROUP	*ecgroup = NULL;
	EC_POINT	*point = NULL;
	EC_POINT	*ret = NULL;
	BN_CTX		*bnctx = NULL;
	BIGNUM		*x = NULL, *y = NULL;
	size_t		 eclen;
	size_t		 xlen, ylen;

	if ((bnctx = BN_CTX_new()) == NULL)
		goto done;
	BN_CTX_start(bnctx);
	if ((x = BN_CTX_get(bnctx)) == NULL ||
	    (y = BN_CTX_get(bnctx)) == NULL)
		goto done;

	eclen = ec_getlen(group);
	if (len < eclen)
		goto done;
	xlen = ylen = eclen / 2;
	if ((x = BN_bin2bn(buf, xlen, x)) == NULL ||
	    (y = BN_bin2bn(buf + xlen, ylen, y)) == NULL)
		goto done;

	if ((ecgroup = EC_KEY_get0_group(group->ec)) == NULL)
		goto done;

	if ((point = EC_POINT_new(ecgroup)) == NULL)
		goto done;

	if (!EC_POINT_set_affine_coordinates(ecgroup, point, x, y, bnctx))
		goto done;

	/* success */
	ret = point;
	point = NULL;	/* owned by caller */

 done:
	EC_POINT_clear_free(point);
	/* Make sure to erase sensitive data */
	if (x != NULL)
		BN_clear(x);
	if (y != NULL)
		BN_clear(y);
	BN_CTX_end(bnctx);
	BN_CTX_free(bnctx);

	return (ret);
}

int
ec25519_init(struct dh_group *group)
{
	static const uint8_t	 basepoint[CURVE25519_SIZE] = { 9 };
	struct curve25519_key	*curve25519;

	if ((curve25519 = calloc(1, sizeof(*curve25519))) == NULL)
		return (-1);

	group->curve25519 = curve25519;

	arc4random_buf(curve25519->secret, CURVE25519_SIZE);
	crypto_scalarmult_curve25519(curve25519->public,
	    curve25519->secret, basepoint);

	return (0);
}

int
ec25519_getlen(struct dh_group *group)
{
	if (group->spec == NULL)
		return (0);
	return (CURVE25519_SIZE);
}

int
ec25519_create_exchange(struct dh_group *group, uint8_t *buf)
{
	struct curve25519_key	*curve25519 = group->curve25519;

	memcpy(buf, curve25519->public, ec25519_getlen(group));
	return (0);
}

int
ec25519_create_shared(struct dh_group *group, uint8_t *shared, uint8_t *public)
{
	struct curve25519_key	*curve25519 = group->curve25519;

	crypto_scalarmult_curve25519(shared, curve25519->secret, public);
	return (0);
}

/* combine sntrup761 with curve25519 */

int
kemsx_init(struct dh_group *group)
{
	/* delayed until kemsx_create_exchange2 */
	return (0);
}

int
kemsx_getlen(struct dh_group *group)
{
	return (0);
}

int
kemsx_create_exchange2(struct dh_group *group, struct ibuf **bufp,
    struct ibuf *iexchange)
{
	struct kemsx_key	*kemsx;
	struct curve25519_key	*curve25519;
	struct ibuf		*buf = NULL;
	u_char *cp, *pk;
	size_t have, need;

	if (ec25519_init(group) == -1)
		return (-1);
	if (group->curve25519 == NULL)
		return (-1);
	if ((kemsx = calloc(1, sizeof(*kemsx))) == NULL)
		return (-1);
	group->kemsx = kemsx;

	if (iexchange == NULL) {
		kemsx->initiator = 1;
		crypto_kem_sntrup761_keypair(kemsx->public, kemsx->secret);
		/* output */
		need = crypto_kem_sntrup761_PUBLICKEYBYTES +
		    CURVE25519_SIZE;
		buf = ibuf_new(NULL, need);
		if (buf == NULL)
			return -1;
		cp = ibuf_data(buf);
		memcpy(cp, kemsx->public,
		    crypto_kem_sntrup761_PUBLICKEYBYTES);
		cp += crypto_kem_sntrup761_PUBLICKEYBYTES;
	} else {
		kemsx->initiator = 0;
		/* input */
		have = ibuf_size(iexchange);
		need = crypto_kem_sntrup761_PUBLICKEYBYTES +
		    CURVE25519_SIZE;
		if (have != need)
			return -1;
		/* output */
		need = crypto_kem_sntrup761_CIPHERTEXTBYTES +
		    CURVE25519_SIZE;
		buf = ibuf_new(NULL, need);
		if (buf == NULL)
			return -1;
		cp = ibuf_data(buf);
		pk = ibuf_data(iexchange);
		crypto_kem_sntrup761_enc(cp, kemsx->kemkey, pk);
		cp += crypto_kem_sntrup761_CIPHERTEXTBYTES;
	}
	curve25519 = group->curve25519;
	memcpy(cp, curve25519->public, CURVE25519_SIZE);
	*bufp = buf;
	return (0);
}

int
kemsx_create_shared2(struct dh_group *group, struct ibuf **sharedp,
    struct ibuf *exchange)
{
	struct curve25519_key	*curve25519 = group->curve25519;
	struct kemsx_key	*kemsx = group->kemsx;
	struct ibuf		*buf = NULL;
	EVP_MD_CTX		*ctx = NULL;
	uint8_t			*cp;
	uint8_t			 shared[CURVE25519_SIZE];
	size_t			 have, need;
	u_int			 len;

	*sharedp = NULL;
	if (kemsx == NULL)
		return (-1);
	if (exchange == NULL)
		return (-1);

	have = ibuf_size(exchange);
	cp = ibuf_data(exchange);
	if (kemsx->initiator) {
		/* input */
		need = crypto_kem_sntrup761_CIPHERTEXTBYTES +
		    CURVE25519_SIZE;
		if (have != need)
			return (-1);
		crypto_kem_sntrup761_dec(kemsx->kemkey, cp, kemsx->secret);
		cp += crypto_kem_sntrup761_CIPHERTEXTBYTES;
	} else {
		/* input, should have been checked before */
		need = crypto_kem_sntrup761_PUBLICKEYBYTES +
		    CURVE25519_SIZE;
		if (have != need)
			return (-1);
		cp += crypto_kem_sntrup761_PUBLICKEYBYTES;
	}
	crypto_scalarmult_curve25519(shared, curve25519->secret, cp);

	/* result is hash of concatenation of KEM key and DH shared secret */
	len = SHA512_DIGEST_LENGTH;
	buf = ibuf_new(NULL, len);
	if (buf == NULL)
		return (-1);
	if ((ctx = EVP_MD_CTX_new()) == NULL ||
	    EVP_DigestInit_ex(ctx, EVP_sha512(), NULL) != 1 ||
	    EVP_DigestUpdate(ctx, kemsx->kemkey, sizeof(kemsx->kemkey)) != 1 ||
	    EVP_DigestUpdate(ctx, shared, sizeof(shared)) != 1 ||
	    EVP_DigestFinal_ex(ctx, ibuf_data(buf), &len) != 1) {
		EVP_MD_CTX_free(ctx);
		ibuf_free(buf);
		return (-1);
	}
	EVP_MD_CTX_free(ctx);
	*sharedp = buf;
	return (0);
}
