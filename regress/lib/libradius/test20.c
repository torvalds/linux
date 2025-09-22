#include "incs.h"

/*
 * string attributes
 */

void test20(void)
{
	RADIUS_PACKET *packet;
	char buf[256];

	static const uint8_t attrs[] = {
		10, 12, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
		RADIUS_TYPE_VENDOR_SPECIFIC, 17, 0, 0, 0, 20, 30, 11, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
		40, 7, 'z', 'x', 'c', 'v', 0,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 50, 60, 6, 'b', 'n', 0, 'm',
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_string_attr(packet, 10, "qwertyuiop");
	radius_put_vs_string_attr(packet, 20, 30, "asdfghjkl");
	radius_put_raw_attr(packet, 40, "zxcv\0", 5);
	radius_put_vs_raw_attr(packet, 50, 60, "bn\0m", 4);

	CHECK(radius_get_length(packet) == sizeof(attrs) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs, sizeof(attrs)) == 0);

	CHECK(radius_get_string_attr(packet, 10, buf, sizeof(buf)) == 0);
	CHECK(strcmp(buf, "qwertyuiop") == 0);

	CHECK(radius_get_vs_string_attr(packet, 20, 30, buf, sizeof(buf)) == 0);
	CHECK(strcmp(buf, "asdfghjkl") == 0);

	CHECK(radius_get_string_attr(packet, 40, buf, sizeof(buf)) != 0);
	CHECK(radius_get_vs_string_attr(packet, 50, 60, buf, sizeof(buf)) != 0);

	CHECK(radius_get_string_attr(packet, 10, buf, 4) == 0);
	CHECK(strcmp(buf, "qwe") == 0);

	CHECK(radius_get_vs_string_attr(packet, 20, 30, buf, 5) == 0);
	CHECK(strcmp(buf, "asdf") == 0);

	CHECK(radius_get_string_attr(packet, 10, buf, 0) == 0);
	CHECK(buf[0] != 0);

	CHECK(radius_get_vs_string_attr(packet, 20, 30, buf, 0) == 0);
	CHECK(buf[0] != 0);
}

ADD_TEST(test20)
