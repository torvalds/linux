#include "incs.h"

/*
 * put/get/has attributes
 */

void test01(void)
{
	RADIUS_PACKET *packet;
	uint8_t buf[256];
	size_t len;
	const void *ptr;

	static const uint8_t data0[] = { 0xfe, 0xdc, 0xba, 0x98 };
	static const uint8_t data1[] = { 0x76, 0x54, 0x32 };
	static const uint8_t data2[] = { 0x0f, 0x1e, 0x2d, 0x3c };
	static const uint8_t data3[] = { 0x4b, 0x5a, 0x69 };
	static const uint8_t attrs[] = {
		10, 6, 0xfe, 0xdc, 0xba, 0x98,
		20, 5, 0x76, 0x54, 0x32,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 300/256, 300%256, 40, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		RADIUS_TYPE_VENDOR_SPECIFIC, 11, 0, 0, 500/256, 500%256, 60, 5, 0x4b, 0x5a, 0x69,
		20, 6, 0x0f, 0x1e, 0x2d, 0x3c,
		RADIUS_TYPE_VENDOR_SPECIFIC, 11, 0, 0, 300/256, 300%256, 40, 5, 0x76, 0x54, 0x32,
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_raw_attr(packet, 10, data0, sizeof(data0));
	radius_put_raw_attr(packet, 20, data1, sizeof(data1));
	radius_put_vs_raw_attr(packet, 300, 40, data2, sizeof(data2));
	radius_put_vs_raw_attr(packet, 500, 60, data3, sizeof(data3));
	radius_put_raw_attr(packet, 20, data2, sizeof(data2));
	radius_put_vs_raw_attr(packet, 300, 40, data1, sizeof(data1));

	CHECK(radius_get_length(packet) == sizeof(attrs) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs, sizeof(attrs)) == 0);

	len = sizeof(buf);
	CHECK(radius_get_raw_attr(packet, 10, buf, &len) == 0);
	CHECK(len == sizeof(data0));
	CHECK(memcmp(buf, data0, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_raw_attr(packet, 20, buf, &len) == 0);
	CHECK(len == sizeof(data1));
	CHECK(memcmp(buf, data1, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_vs_raw_attr(packet, 300, 40, buf, &len) == 0);
	CHECK(len == sizeof(data2));
	CHECK(memcmp(buf, data2, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_vs_raw_attr(packet, 500, 60, buf, &len) == 0);
	CHECK(len == sizeof(data3));
	CHECK(memcmp(buf, data3, len) == 0);

	len = 2;
	CHECK(radius_get_raw_attr(packet, 10, buf, &len) == 0);
	CHECK(len == 2);
	CHECK(memcmp(buf, data0, len) == 0);

	len = 2;
	CHECK(radius_get_vs_raw_attr(packet, 300, 40, buf, &len) == 0);
	CHECK(len == 2);
	CHECK(memcmp(buf, data2, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_raw_attr(packet, 90, buf, &len) != 0);
	CHECK(radius_get_vs_raw_attr(packet, 300, 90, buf, &len) != 0);
	CHECK(radius_get_vs_raw_attr(packet, 900, 40, buf, &len) != 0);

	len = 0;
	CHECK(radius_get_raw_attr_ptr(packet, 10, &ptr, &len) == 0);
	CHECK(len == 4);
	CHECK(ptr == radius_get_data(packet) + 20 +  0 + 2);

	len = 0;
	CHECK(radius_get_vs_raw_attr_ptr(packet, 500, 60, &ptr, &len) == 0);
	CHECK(len == 3);
	CHECK(ptr == radius_get_data(packet) + 20 + 23 + 8);

	CHECK(radius_get_raw_attr_ptr(packet, 90, &ptr, &len) != 0);
	CHECK(radius_get_vs_raw_attr_ptr(packet, 300, 90, &ptr, &len) != 0);
	CHECK(radius_get_vs_raw_attr_ptr(packet, 900, 40, &ptr, &len) != 0);

	CHECK(radius_has_attr(packet, 10));
	CHECK(radius_has_attr(packet, 20));
	CHECK(radius_has_vs_attr(packet, 300, 40));
	CHECK(radius_has_vs_attr(packet, 500, 60));
	CHECK(!radius_has_attr(packet, 90));
	CHECK(!radius_has_vs_attr(packet, 300, 90));
	CHECK(!radius_has_vs_attr(packet, 900, 40));
}

ADD_TEST(test01)
