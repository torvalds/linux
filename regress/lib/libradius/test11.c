#include "incs.h"

#include <openssl/md5.h>

/*
 * accounting request authenticator
 */

void test11(void)
{
	RADIUS_PACKET *packet;
	MD5_CTX ctx;

	uint8_t packetdata[] = {
		RADIUS_CODE_ACCOUNTING_REQUEST, 0x7f, 0, 31,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* auth */
		10, 11, 'f', 'o', 'o', 'b', 'a', 'r', 'b', 'a', 'z',
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCOUNTING_REQUEST);
	radius_set_id(packet, 0x7f);
	radius_put_string_attr(packet, 10, "foobarbaz");
	radius_set_accounting_request_authenticator(packet, "sharedsecret");

	MD5_Init(&ctx);
	MD5_Update(&ctx, packetdata, sizeof(packetdata));
	MD5_Update(&ctx, "sharedsecret", 12);
	MD5_Final(packetdata + 4, &ctx);

	CHECK(radius_get_length(packet) == sizeof(packetdata));
	CHECK(memcmp(radius_get_data(packet), packetdata, sizeof(packetdata)) == 0);
	CHECK(radius_check_accounting_request_authenticator(packet, "sharedsecret") == 0);

	radius_set_raw_attr(packet, 10, "zapzapzap", 9);
	CHECK(radius_check_accounting_request_authenticator(packet, "sharedsecret") != 0);
}

ADD_TEST(test11)
