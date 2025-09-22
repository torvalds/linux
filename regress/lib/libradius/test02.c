#include "incs.h"

/*
 * set attributes
 */

void test02(void)
{
	RADIUS_PACKET *packet;

	static const uint8_t data0[] = { 0xfe, 0xdc, 0xba, 0x98 };
	static const uint8_t data1[] = { 0x76, 0x54, 0x32, 0x10 };
	static const uint8_t data2[] = { 0x0f, 0x1e, 0x2d, 0x3c };
	static const uint8_t data3[] = { 0x4b, 0x5a, 0x69, 0x78, 0xff };
	static const uint8_t attrs_beforeset[] = {
		10, 6, 0xfe, 0xdc, 0xba, 0x98,
		10, 6, 0x76, 0x54, 0x32, 0x10,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 0x76, 0x54, 0x32, 0x10,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 0xfe, 0xdc, 0xba, 0x98,
	};
	static const uint8_t attrs_afterset[] = {
		10, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		10, 6, 0x76, 0x54, 0x32, 0x10,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 0xfe, 0xdc, 0xba, 0x98,
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_raw_attr(packet, 10, data0, sizeof(data0));
	radius_put_raw_attr(packet, 10, data1, sizeof(data1));
	radius_put_vs_raw_attr(packet, 20, 30, data1, sizeof(data1));
	radius_put_vs_raw_attr(packet, 20, 30, data0, sizeof(data0));

	CHECK(radius_get_length(packet) == sizeof(attrs_beforeset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_beforeset, sizeof(attrs_beforeset)) == 0);

	CHECK(radius_set_raw_attr(packet, 10, data2, sizeof(data2)) == 0);
	CHECK(radius_set_vs_raw_attr(packet, 20, 30, data2, sizeof(data2)) == 0);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterset, sizeof(attrs_afterset)) == 0);

	CHECK(radius_set_raw_attr(packet, 10, data3, sizeof(data2) - 1) != 0);
	CHECK(radius_set_raw_attr(packet, 10, data3, sizeof(data2) + 1) != 0);
	CHECK(radius_set_vs_raw_attr(packet, 20, 30, data3, sizeof(data2) - 1) != 0);
	CHECK(radius_set_vs_raw_attr(packet, 20, 30, data3, sizeof(data2) + 1) != 0);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterset, sizeof(attrs_afterset)) == 0);

	CHECK(radius_set_raw_attr(packet, 90, data3, sizeof(data3)) != 0);
	CHECK(radius_set_vs_raw_attr(packet, 900, 90, data3, sizeof(data3)) != 0);
}

ADD_TEST(test02)
