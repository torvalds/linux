#include "incs.h"

#include <openssl/hmac.h>

/*
 * User-Password attribute
 */

void test24(void)
{
	uint8_t cipher[256],cipher1[256];
	size_t clen;
	char plain[256];
	RADIUS_PACKET *packet;

	uint8_t ra[16] = {
		0xf3, 0xa4, 0x7a, 0x1f, 0x6a, 0x6d, 0x76, 0x71, 0x0b, 0x94, 0x7a, 0xb9, 0x30, 0x41, 0xa0, 0x39,
	};

	uint8_t encryptedpass[16] = {
		0x33, 0x65, 0x75, 0x73, 0x77, 0x82, 0x89, 0xb5, 0x70, 0x88, 0x5e, 0x15, 0x08, 0x48, 0x25, 0xc5,
	};

	clen = sizeof(cipher);
	CHECK(radius_encrypt_user_password_attr(cipher, &clen, "challenge", ra, "xyzzy5461") == 0);
	CHECK(clen == 16);
	CHECK(memcmp(cipher, encryptedpass, 16) == 0);

	CHECK(radius_decrypt_user_password_attr(plain, sizeof(plain), cipher, clen, ra, "xyzzy5461") == 0);
	CHECK(strcmp(plain, "challenge") == 0);

	clen = 15;
	CHECK(radius_encrypt_user_password_attr(cipher, &clen, "challenge", ra, "xyzzy5461") != 0);
	CHECK(radius_decrypt_user_password_attr(plain, 16, cipher, 16, ra, "xyzzy5461") != 0);
	CHECK(radius_decrypt_user_password_attr(plain, 256, cipher, 17, ra, "xyzzy5461") != 0);

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	CHECK(radius_put_user_password_attr(packet, "foobarbaz", "sharedsecret") == 0);
	clen = sizeof(cipher1);
	CHECK(radius_get_raw_attr(packet, RADIUS_TYPE_USER_PASSWORD, cipher1, &clen) == 0);
	CHECK(clen == 16);
	radius_encrypt_user_password_attr(cipher, &clen, "foobarbaz", radius_get_authenticator_retval(packet), "sharedsecret");
	CHECK(memcmp(cipher1, cipher, 16) == 0);

	CHECK(radius_get_user_password_attr(packet, plain, sizeof(plain), "sharedsecret") == 0);
	CHECK(strcmp(plain, "foobarbaz") == 0);
}

ADD_TEST(test24)
