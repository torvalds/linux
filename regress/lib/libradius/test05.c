#include "incs.h"

/*
 * request/response association
 */

void test05(void)
{
	RADIUS_PACKET *req0, *req1, *resp;

	req0 = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);
	req1 = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);
	CHECK(radius_get_request_packet(req0) == NULL);
	CHECK(radius_get_request_packet(req1) == NULL);

	resp = radius_new_response_packet(RADIUS_CODE_ACCESS_ACCEPT, req0);
	CHECK(radius_get_request_packet(resp) == req0);

	radius_set_request_packet(resp, req1);
	CHECK(radius_get_request_packet(resp) == req1);
}

ADD_TEST(test05)
