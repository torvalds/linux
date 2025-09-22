#include "incs.h"

/*
 * del attributes
 */

void test03(void)
{
	RADIUS_PACKET *packet;

	static const uint8_t data0[] = { 0xfe, 0xdc, 0xba, 0x98 };
	static const uint8_t data1[] = { 0x76, 0x54, 0x32, 0x10 };
	static const uint8_t data2[] = { 0x0f, 0x1e, 0x2d, 0x3c };
	static const uint8_t data3[] = { 0x4b, 0x5a, 0x69, 0x67 };
	static const uint8_t attrs_afterdel[] = {
		80, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		80, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 70, 60, 6, 0x0f, 0x1e, 0x2d, 0x3c,
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_raw_attr(packet, 10, data0, sizeof(data0));
	radius_put_raw_attr(packet, 80, data2, sizeof(data2));
	radius_put_raw_attr(packet, 10, data1, sizeof(data1));
	radius_put_raw_attr(packet, 80, data2, sizeof(data2));
	radius_put_vs_raw_attr(packet, 20, 30, data1, sizeof(data1));
	radius_put_vs_raw_attr(packet, 20, 30, data0, sizeof(data0));
	radius_put_vs_raw_attr(packet, 70, 60, data2, sizeof(data2));
	radius_put_vs_raw_attr(packet, 20, 30, data3, sizeof(data3));

	CHECK(radius_del_attr_all(packet, 10) == 0);
	CHECK(radius_del_vs_attr_all(packet, 20, 30) == 0);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterdel) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterdel, sizeof(attrs_afterdel)) == 0);

	CHECK(radius_del_attr_all(packet, 90) == 0);
	CHECK(radius_del_vs_attr_all(packet, 90, 90) == 0);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterdel) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterdel, sizeof(attrs_afterdel)) == 0);
}

ADD_TEST(test03)
