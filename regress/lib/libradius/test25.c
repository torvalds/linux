#include "incs.h"

/*
 * MS-MPPE-{Send,Recv}-Key attribute
 */

void test25(void)
{
	uint8_t ra[] = {
		0x67, 0x8a, 0xe6, 0xed, 0x69, 0x6e, 0x1b, 0xd1, 0x0a, 0xe2, 0xfe, 0xa1, 0x05, 0xd4, 0x6a, 0x56,
	};
	uint8_t encrypted[] = {
		0x80, 0x36,
		0xf4, 0xab, 0xbe, 0x21, 0x17, 0xfb, 0x3e, 0x4a, 0x78, 0x74, 0xdc, 0xe9, 0x1c, 0x5b, 0x04, 0x49,
		0x8e, 0xc7, 0x72, 0x0f, 0x16, 0x86, 0x88, 0x56, 0x2d, 0xbc, 0x88, 0xe2, 0x1c, 0xab, 0x62, 0x71,
	};
	uint8_t plainkey[] = {
		0xfc, 0x6e, 0xa4, 0x18, 0x37, 0x3d, 0x8e, 0x90, 0xc1, 0x36, 0xfa, 0xe3, 0x73, 0x5e, 0x37, 0xd1,
	};
	uint8_t plainkey2[] = {
		0x86, 0xfe, 0x22, 0x0e, 0x76, 0x24, 0xba, 0x2a, 0x10, 0x05, 0xf6, 0xbf, 0x9b, 0x55, 0xe0, 0xb2,
	};

	uint8_t plain[256];
	size_t plen;
	uint8_t cipher[256];
	size_t clen;
	RADIUS_PACKET *packet, *response;

	plen = sizeof(plain);
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, encrypted, sizeof(encrypted), ra, "hogehogefugafuga") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey, 16) == 0);

	clen = sizeof(cipher);
	CHECK(radius_encrypt_mppe_key_attr(cipher, &clen, plainkey, 16, ra, "hogehogefugafuga") == 0);
	CHECK(clen == 34);
	memset(plain, 0, sizeof(plain));
	plen = sizeof(plain);
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, cipher, clen, ra, "hogehogefugafuga") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey, 16) == 0);

	clen = 33;
	CHECK(radius_encrypt_mppe_key_attr(cipher, &clen, plainkey, 16, ra, "hogehogefugafuga") != 0);
	plen = 15;
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, cipher, 34, ra, "hogehogefugafuga") != 0);
	plen = 16;
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, cipher, 33, ra, "hogehogefugafuga") != 0);

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	CHECK(radius_put_mppe_send_key_attr(packet, plainkey, sizeof(plainkey), "sharedsecret") == 0);
	clen = sizeof(cipher);
	CHECK(radius_get_vs_raw_attr(packet, RADIUS_VENDOR_MICROSOFT, RADIUS_VTYPE_MPPE_SEND_KEY, cipher, &clen) == 0);
	CHECK(clen == 34);
	plen = sizeof(plain);
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, cipher, clen, radius_get_authenticator_retval(packet), "sharedsecret") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey, plen) == 0);
	memset(plain, 0, sizeof(plain));
	plen = sizeof(plain);
	CHECK(radius_get_mppe_send_key_attr(packet, plain, &plen, "sharedsecret") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey, plen) == 0);

	response = radius_new_response_packet(RADIUS_CODE_ACCESS_ACCEPT, packet);

	CHECK(radius_put_mppe_recv_key_attr(response, plainkey2, sizeof(plainkey2), "sharedsecret") == 0);
	clen = sizeof(cipher);
	CHECK(radius_get_vs_raw_attr(response, RADIUS_VENDOR_MICROSOFT, RADIUS_VTYPE_MPPE_RECV_KEY, cipher, &clen) == 0);
	CHECK(clen == 34);
	plen = sizeof(plain);
	CHECK(radius_decrypt_mppe_key_attr(plain, &plen, cipher, clen, radius_get_authenticator_retval(packet), "sharedsecret") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey2, plen) == 0);
	memset(plain, 0, sizeof(plain));
	plen = sizeof(plain);
	CHECK(radius_get_mppe_recv_key_attr(response, plain, &plen, "sharedsecret") == 0);
	CHECK(plen == 16);
	CHECK(memcmp(plain, plainkey2, plen) == 0);
}

ADD_TEST(test25)
