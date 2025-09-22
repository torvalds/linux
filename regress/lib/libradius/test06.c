#include "incs.h"

/*
 *
 */

void test06(void)
{
	RADIUS_PACKET *pkt;
	u_char data[] = {
		RADIUS_CODE_ACCESS_ACCEPT, 0x01, 0, 20,
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	};
	pkt = radius_convert_packet(data, sizeof(data));
	CHECK(pkt != NULL);
	CHECK(!radius_has_attr(pkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR));
	CHECK(!radius_put_uint32_attr(pkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR, 1));
	//CHECK(memcmp(radius_get_data(pkt), data, sizeof(data)) == 0);
	radius_delete_packet(pkt);
}

ADD_TEST(test06)
