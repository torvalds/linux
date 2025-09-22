#include "incs.h"

/*
 * put/get cat attributes
 */

void test04(void)
{
	RADIUS_PACKET *packet;
	uint8_t buf[1024];
	size_t len;
	uint8_t *p;

#define ATTRLEN (256 + 256 + 103)
	uint8_t data0[ATTRLEN];
	uint8_t data1[ATTRLEN];
	uint8_t data2[] = { 0x10, 0x20, 0x30, 0x40 };
	uint8_t data3[] = { 0x10, 0x20, 0x30, 0x40, 0x10, 0x20, 0x30, 0x40 };
	uint8_t attrs[2048];

	packet = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);

	for (int i = 0; i < ATTRLEN; i++)
		data0[i] = random();
	for (int i = 0; i < ATTRLEN; i++)
		data1[i] = random();

	p = attrs;
	*p++ = 20; *p++ = 6;
	memcpy(p, data2, 4); p += 4;

	*p++ = 10; *p++ = 255;
	memcpy(p, data0, 253); p += 253;
	*p++ = 10; *p++ = 255;
	memcpy(p, data0 + 253, 253); p += 253;
	*p++ = 10; *p++ = ATTRLEN-253*2+2;
	memcpy(p, data0 + 253*2, ATTRLEN-253*2); p += ATTRLEN-253*2;

	*p++ = RADIUS_TYPE_VENDOR_SPECIFIC; *p++ = 255;
	*p++ = 0; *p++ = 0; *p++ = 0; *p++ = 20; *p++ = 30; *p++ = 249;
	memcpy(p, data1, 247); p += 247;
	*p++ = RADIUS_TYPE_VENDOR_SPECIFIC; *p++ = 255;
	*p++ = 0; *p++ = 0; *p++ = 0; *p++ = 20; *p++ = 30; *p++ = 249;
	memcpy(p, data1 + 247, 247); p += 247;
	*p++ = RADIUS_TYPE_VENDOR_SPECIFIC; *p++ = ATTRLEN-247*2+8;
	*p++ = 0; *p++ = 0; *p++ = 0; *p++ = 20; *p++ = 30; *p++ = ATTRLEN-247*2+2;
	memcpy(p, data1 + 247*2, ATTRLEN-247*2); p += ATTRLEN-247*2;

	*p++ = 20; *p++ = 6;
	memcpy(p, data2, 4); p += 4;


	radius_put_raw_attr(packet, 20, data2, sizeof(data2));
	radius_put_raw_attr_cat(packet, 10, data0, ATTRLEN);
	radius_put_vs_raw_attr_cat(packet, 20, 30, data1, ATTRLEN);
	radius_put_raw_attr(packet, 20, data2, sizeof(data2));

	CHECK(radius_get_length(packet) == 20 + (p-attrs));
	CHECK(memcmp(radius_get_data(packet) + 20, attrs, p-attrs) == 0);

	len = sizeof(buf);
	CHECK(radius_get_raw_attr_cat(packet, 10, buf, &len) == 0);
	CHECK(len == ATTRLEN);
	CHECK(memcmp(buf, data0, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_vs_raw_attr_cat(packet, 20, 30, buf, &len) == 0);
	CHECK(len == ATTRLEN);
	CHECK(memcmp(buf, data1, len) == 0);

	len = sizeof(buf);
	CHECK(radius_get_raw_attr_cat(packet, 20,buf, &len) == 0);
	CHECK(len == sizeof(data3));
	CHECK(memcmp(buf, data3, len) == 0);
}

ADD_TEST(test04)
