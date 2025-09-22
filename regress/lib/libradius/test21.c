#include "incs.h"

/*
 * integer (uint16_t, uint32_t, uint64_t) attributes
 */

void test21(void)
{
	RADIUS_PACKET *packet;
	uint16_t v16;
	uint32_t v32;
	uint64_t v64;

	static const uint8_t attrs_beforeset[] = {
		1, 3, 0,
		10, 4, 0x12, 0x34,
		RADIUS_TYPE_VENDOR_SPECIFIC, 10, 0, 0, 0, 20, 30, 4, 0x43, 0x21,
		40, 6, 0x13, 0x57, 0x9b, 0xdf,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 50, 60, 6, 0x24, 0x68, 0xac, 0xe0,
		70, 10, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		RADIUS_TYPE_VENDOR_SPECIFIC, 16, 0, 0, 0, 80, 90, 10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
	};
	static const uint8_t attrs_afterset[] = {
		1, 3, 0,
		10, 4, 0x43, 0x21,
		RADIUS_TYPE_VENDOR_SPECIFIC, 10, 0, 0, 0, 20, 30, 4, 0x12, 0x34,
		40, 6, 0x24, 0x68, 0xac, 0xe0,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 50, 60, 6, 0x13, 0x57, 0x9b, 0xdf,
		70, 10, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
		RADIUS_TYPE_VENDOR_SPECIFIC, 16, 0, 0, 0, 80, 90, 10, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_raw_attr(packet, 1, "", 1); /* padding for UNalignment */
	radius_put_uint16_attr(packet, 10, 0x1234);
	radius_put_vs_uint16_attr(packet, 20, 30, 0x4321);
	radius_put_uint32_attr(packet, 40, 0x13579bdfU);
	radius_put_vs_uint32_attr(packet, 50, 60, 0x2468ace0U);
	radius_put_uint64_attr(packet, 70, 0x0123456789abcdefULL);
	radius_put_vs_uint64_attr(packet, 80, 90, 0xfedcba9876543210ULL);

	CHECK(radius_get_length(packet) == sizeof(attrs_beforeset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_beforeset, sizeof(attrs_beforeset)) == 0);

	CHECK(radius_get_uint16_attr(packet, 10, &v16) == 0);
	CHECK(v16 == 0x1234);
	CHECK(radius_get_vs_uint16_attr(packet, 20, 30, &v16) == 0);
	CHECK(v16 == 0x4321);

	CHECK(radius_get_uint32_attr(packet, 40, &v32) == 0);
	CHECK(v32 == 0x13579bdfU);
	CHECK(radius_get_vs_uint32_attr(packet, 50, 60, &v32) == 0);
	CHECK(v32 == 0x2468ace0U);

	CHECK(radius_get_uint64_attr(packet, 70, &v64) == 0);
	CHECK(v64 == 0x0123456789abcdefULL);
	CHECK(radius_get_vs_uint64_attr(packet, 80, 90, &v64) == 0);
	CHECK(v64 == 0xfedcba9876543210ULL);

	CHECK(radius_set_uint16_attr(packet, 10, 0x4321) == 0);
	CHECK(radius_set_vs_uint16_attr(packet, 20, 30, 0x1234) == 0);
	CHECK(radius_set_uint32_attr(packet, 40, 0x2468ace0U) == 0);
	CHECK(radius_set_vs_uint32_attr(packet, 50, 60, 0x13579bdfU) == 0);
	CHECK(radius_set_uint64_attr(packet, 70, 0xfedcba9876543210ULL) == 0);
	CHECK(radius_set_vs_uint64_attr(packet, 80, 90, 0x0123456789abcdefULL) == 0);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterset, sizeof(attrs_afterset)) == 0);

	CHECK(radius_get_uint16_attr(packet, 40, &v16) != 0);
	CHECK(radius_get_vs_uint16_attr(packet, 50, 60, &v16) != 0);
	CHECK(radius_get_uint32_attr(packet, 70, &v32) != 0);
	CHECK(radius_get_vs_uint32_attr(packet, 80, 90, &v32) != 0);
	CHECK(radius_get_uint64_attr(packet, 10, &v64) != 0);
	CHECK(radius_get_vs_uint64_attr(packet, 20, 30, &v64) != 0);
}

ADD_TEST(test21)
