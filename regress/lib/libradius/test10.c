#include "incs.h"

#include <openssl/md5.h>

/*
 * response authenticator
 */

void test10(void)
{
	RADIUS_PACKET *request;
	RADIUS_PACKET *response;
	MD5_CTX ctx;

	uint8_t responsedata[] = {
		RADIUS_CODE_ACCESS_ACCEPT, 0x7f, 0, 31,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* auth */
		10, 11, 'f', 'o', 'o', 'b', 'a', 'r', 'b', 'a', 'z',
	};

	request = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST);
	radius_set_id(request, 0x7f);
	response = radius_new_response_packet(RADIUS_CODE_ACCESS_ACCEPT, request);
	radius_put_string_attr(response, 10, "foobarbaz");
	radius_set_response_authenticator(response, "sharedsecret");

	MD5_Init(&ctx);
	MD5_Update(&ctx, responsedata, 4);
	MD5_Update(&ctx, radius_get_authenticator_retval(request), 16);
	MD5_Update(&ctx, responsedata + 20, sizeof(responsedata) - 20);
	MD5_Update(&ctx, "sharedsecret", 12);
	MD5_Final(responsedata + 4, &ctx);

	CHECK(radius_get_length(response) == sizeof(responsedata));
	CHECK(memcmp(radius_get_data(response), responsedata, sizeof(responsedata)) == 0);
	CHECK(radius_check_response_authenticator(response, "sharedsecret") == 0);

	radius_set_raw_attr(response, 10, "zapzapzap", 9);
	CHECK(radius_check_response_authenticator(response, "sharedsecret") != 0);
}

ADD_TEST(test10)
