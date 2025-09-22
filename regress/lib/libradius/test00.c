#include "incs.h"

/*
 * basic header operation
 */

void test00(void)
{
	RADIUS_PACKET *packet;
	uint8_t code;
	uint8_t id;
	const uint8_t *pdata;
	uint8_t authenticator[16];

	code = random();
	id = random();
	packet = radius_new_request_packet(code);
	radius_set_id(packet, id);
	pdata = (const uint8_t *)radius_get_data(packet);
	CHECK(pdata[0] == code);
	CHECK(radius_get_code(packet) == code);
	CHECK(pdata[1] == id);
	CHECK(radius_get_id(packet) == id);
	CHECK(((pdata[2] << 8) | pdata[3]) == 20);
	CHECK(radius_get_length(packet) == 20);

	CHECK(radius_get_authenticator_retval(packet) == pdata + 4);
	radius_get_authenticator(packet, authenticator);
	CHECK(memcmp(authenticator, radius_get_authenticator_retval(packet), 16) == 0);
}

ADD_TEST(test00)
