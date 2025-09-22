/*	$OpenBSD: dh.c,v 1.29 2025/04/30 03:53:21 tb Exp $	*/

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

#include <string.h>

#include <openssl/obj_mac.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>
#include <openssl/bn.h>

#include "dh.h"

#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

int	dh_init(struct group *);

int	modp_init(struct group *);
int	modp_getlen(struct group *);
int	modp_create_exchange(struct group *, u_int8_t *);
int	modp_create_shared(struct group *, u_int8_t *, u_int8_t *);

int	ec_init(struct group *);
int	ec_getlen(struct group *);
int	ec_secretlen(struct group *);
int	ec_create_exchange(struct group *, u_int8_t *);
int	ec_create_shared(struct group *, u_int8_t *, u_int8_t *);

#define EC_POINT2RAW_FULL	0
#define EC_POINT2RAW_XONLY	1
int	ec_point2raw(struct group *, const EC_POINT *, uint8_t *, size_t, int);
EC_POINT *
	ec_raw2point(struct group *, u_int8_t *, size_t);

struct group_id ike_groups[] = {
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
#ifndef OPENSSL_NO_EC2M
	{ GROUP_EC2N, 3, 155, NULL, NULL, NID_ipsec3 },
	{ GROUP_EC2N, 4, 185, NULL, NULL, NID_ipsec4 },
#endif
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
	{ GROUP_ECP, 30, 512, NULL, NULL, NID_brainpoolP512r1 }
};

void
group_init(void)
{
	/* currently not used */
	return;
}

void
group_free(struct group *group)
{
	if (group == NULL)
		return;
	if (group->dh != NULL)
		DH_free(group->dh);
	if (group->ec != NULL)
		EC_KEY_free(group->ec);
	group->spec = NULL;
	free(group);
}

struct group *
group_get(u_int32_t id)
{
	struct group_id	*p = NULL;
	struct group	*group;
	u_int		 i, items;

	items = sizeof(ike_groups) / sizeof(ike_groups[0]);
	for (i = 0; i < items; i++) {
		if (id == ike_groups[i].id) {
			p = &ike_groups[i];
			break;
		}
	}
	if (p == NULL)
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
#ifndef OPENSSL_NO_EC2M
	case GROUP_EC2N:
#endif
	case GROUP_ECP:
		group->init = ec_init;
		group->getlen = ec_getlen;
		group->secretlen = ec_secretlen;
		group->exchange = ec_create_exchange;
		group->shared = ec_create_shared;
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

int
dh_init(struct group *group)
{
	return (group->init(group));
}

int
dh_getlen(struct group *group)
{
	return (group->getlen(group));
}

int
dh_secretlen(struct group *group)
{
	if (group->secretlen)
		return (group->secretlen(group));
	else
		return (group->getlen(group));
}

int
dh_create_exchange(struct group *group, u_int8_t *buf)
{
	return (group->exchange(group, buf));
}

int
dh_create_shared(struct group *group, u_int8_t *secret, u_int8_t *exchange)
{
	return (group->shared(group, secret, exchange));
}

int
modp_init(struct group *group)
{
	DH	*dh;
	BIGNUM	*p = NULL, *g = NULL;

	if ((dh = DH_new()) == NULL)
		return (-1);
	group->dh = dh;

	if (!BN_hex2bn(&p, group->spec->prime) ||
	    !BN_hex2bn(&g, group->spec->generator)) {
		BN_free(p);
		BN_free(g);
		return (-1);
	}

	if (!DH_set0_pqg(dh, p, NULL, g)) {
		BN_free(p);
		BN_free(g);
		return (-1);
	}

	return (0);
}

int
modp_getlen(struct group *group)
{
	if (group->spec == NULL)
		return (0);
	return (roundup(group->spec->bits, 8) / 8);
}

int
modp_create_exchange(struct group *group, u_int8_t *buf)
{
	DH	*dh = group->dh;
	int	 len, ret;

	if (!DH_generate_key(dh))
		return (-1);
	ret = BN_bn2bin(DH_get0_pub_key(dh), buf);
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
modp_create_shared(struct group *group, u_int8_t *secret, u_int8_t *exchange)
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
ec_init(struct group *group)
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
ec_getlen(struct group *group)
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
ec_secretlen(struct group *group)
{
	return (ec_getlen(group) / 2);
}

int
ec_create_exchange(struct group *group, u_int8_t *buf)
{
	size_t	 len;

	len = ec_getlen(group);
	bzero(buf, len);

	return (ec_point2raw(group, EC_KEY_get0_public_key(group->ec),
	    buf, len, EC_POINT2RAW_FULL));
}

int
ec_create_shared(struct group *group, u_int8_t *secret, u_int8_t *exchange)
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
ec_point2raw(struct group *group, const EC_POINT *point,
    u_int8_t *buf, size_t len, int mode)
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
ec_raw2point(struct group *group, u_int8_t *buf, size_t len)
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
