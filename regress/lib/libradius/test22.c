#include "incs.h"

#include <arpa/inet.h>

/*
 * inernet address (struct in_addr, struct in6_addr) attributes
 */

void test22(void)
{
	RADIUS_PACKET *packet;
	struct in_addr in4a;
	struct in6_addr in6a, in6b;

	static const uint8_t attrs_beforeset[] = {
		1, 3, 0,
		10, 6, 192, 168, 0, 1,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 10, 20, 30, 40,
		40, 18, 0x20, 0x01, 0x0d, 0xb8, 0xde, 0xad, 0xbe, 0xef, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
		RADIUS_TYPE_VENDOR_SPECIFIC, 24, 0, 0, 0, 50, 60, 18, 0x3f, 0xff, 0x0e, 0xca, 0x86, 0x42, 0xfd, 0xb9, 0x75, 0x31, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
	};
	static const uint8_t attrs_afterset[] = {
		1, 3, 0,
		10, 6, 10, 20, 30, 40,
		RADIUS_TYPE_VENDOR_SPECIFIC, 12, 0, 0, 0, 20, 30, 6, 192, 168, 0, 1,
		40, 18, 0x3f, 0xff, 0x0e, 0xca, 0x86, 0x42, 0xfd, 0xb9, 0x75, 0x31, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54,
		RADIUS_TYPE_VENDOR_SPECIFIC, 24, 0, 0, 0, 50, 60, 18, 0x20, 0x01, 0x0d, 0xb8, 0xde, 0xad, 0xbe, 0xef, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
	};

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	radius_put_raw_attr(packet, 1, "", 1); /* padding for UNalignment */
	in4a.s_addr = inet_addr("192.168.0.1");
	radius_put_ipv4_attr(packet, 10, in4a);
	in4a.s_addr = inet_addr("10.20.30.40");
	radius_put_vs_ipv4_attr(packet, 20, 30, in4a);
	inet_pton(AF_INET6, "2001:0db8:dead:beef:1234:5678:9abc:def0", &in6a);
	radius_put_ipv6_attr(packet, 40, &in6a);
	inet_pton(AF_INET6, "3fff:0eca:8642:fdb9:7531:fedc:ba98:7654", &in6a);
	radius_put_vs_ipv6_attr(packet, 50, 60, &in6a);

	CHECK(radius_get_length(packet) == sizeof(attrs_beforeset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_beforeset, sizeof(attrs_beforeset)) == 0);

	CHECK(radius_get_ipv4_attr(packet, 10, &in4a) == 0);
	CHECK(in4a.s_addr == inet_addr("192.168.0.1"));
	CHECK(radius_get_vs_ipv4_attr(packet, 20, 30, &in4a) == 0);
	CHECK(in4a.s_addr == inet_addr("10.20.30.40"));

	CHECK(radius_get_ipv6_attr(packet, 40, &in6b) == 0);
	inet_pton(AF_INET6, "2001:0db8:dead:beef:1234:5678:9abc:def0", &in6a);
	CHECK(memcmp(&in6b, &in6a, sizeof(struct in6_addr)) == 0);
	CHECK(radius_get_vs_ipv6_attr(packet, 50, 60, &in6b) == 0);
	inet_pton(AF_INET6, "3fff:0eca:8642:fdb9:7531:fedc:ba98:7654", &in6a);
	CHECK(memcmp(&in6b, &in6a, sizeof(struct in6_addr)) == 0);

	in4a.s_addr = inet_addr("10.20.30.40");
	radius_set_ipv4_attr(packet, 10, in4a);
	in4a.s_addr = inet_addr("192.168.0.1");
	radius_set_vs_ipv4_attr(packet, 20, 30, in4a);
	inet_pton(AF_INET6, "3fff:0eca:8642:fdb9:7531:fedc:ba98:7654", &in6a);
	radius_set_ipv6_attr(packet, 40, &in6a);
	inet_pton(AF_INET6, "2001:0db8:dead:beef:1234:5678:9abc:def0", &in6a);
	radius_set_vs_ipv6_attr(packet, 50, 60, &in6a);

	CHECK(radius_get_length(packet) == sizeof(attrs_afterset) + 20);
	CHECK(memcmp(radius_get_data(packet) + 20, attrs_afterset, sizeof(attrs_afterset)) == 0);

	CHECK(radius_get_ipv4_attr(packet, 40, &in4a) != 0);
	CHECK(radius_get_vs_ipv4_attr(packet, 50, 60, &in4a) != 0);
	CHECK(radius_get_ipv6_attr(packet, 10, &in6b) != 0);
	CHECK(radius_get_vs_ipv6_attr(packet, 20, 30, &in6b) != 0);
}

ADD_TEST(test22)
