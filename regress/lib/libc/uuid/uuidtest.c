/*	$OpenBSD: uuidtest.c,v 1.2 2023/07/03 13:51:55 jasper Exp $	*/
/*
 * Copyright (c) 2021, 2023 Jasper Lievisse Adriaanse <jasper@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid.h>

#define ASSERT_EQ(a, b) assert((a) == (b))

int
main(int argc, char **argv)
{
	struct uuid	uuid, uuid2, uuid_want;
	char		*uuid_str, *uuid_str_want;
	uint32_t	status;
	unsigned char	bin[16];
	int		rc, t = 1;

	/* Test invalid input to uuid_from_string() */
	printf("[%d] uuid_from_string (invalid) ", t);
	uuid_str = "6fc3134d-011d-463d-a6b4-fe1f3a5e57dX";
	uuid_from_string(uuid_str, &uuid, &status);
	if (status != uuid_s_invalid_string_uuid) {
		printf("failed to return uuid_s_invalid_string_uuid for '%s'\n",
		    uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	/* Test a bad version gets recognized */
	printf("[%d] uuid_from_string (bad version) ", t);
	uuid_str = "ffffffff-ffff-ffff-ffff-ffffffffffff";
	uuid_from_string(uuid_str, &uuid, &status);
	if (status != uuid_s_bad_version) {
		printf("failed to return uuid_s_bad_version for '%s'\n",
		    uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	/* Test valid input to uuid_from_string() */
	printf("[%d] uuid_from_string ", t);
	uuid_str = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";

	uuid_want.time_low = 0xf81d4fae;
	uuid_want.time_mid = 0x7dec;
	uuid_want.time_hi_and_version = 0x11d0;
	uuid_want.clock_seq_hi_and_reserved = 0xa7;
	uuid_want.clock_seq_low = 0x65;
	uuid_want.node[0] = 0x00;
	uuid_want.node[1] = 0xa0;
	uuid_want.node[2] = 0xc9;
	uuid_want.node[3] = 0x1e;
	uuid_want.node[4] = 0x6b;
	uuid_want.node[5] = 0xf6;

	uuid_from_string(uuid_str, &uuid, &status);
	if (status != uuid_s_ok) {
		printf("failed to return uuid_s_ok for '%s', got %d\n", uuid_str, status);
		return 1;
	}

	ASSERT_EQ(uuid.time_low, uuid_want.time_low);
	ASSERT_EQ(uuid.time_mid, uuid_want.time_mid);
	ASSERT_EQ(uuid.time_hi_and_version, uuid_want.time_hi_and_version);
	ASSERT_EQ(uuid.clock_seq_hi_and_reserved, uuid_want.clock_seq_hi_and_reserved);
	ASSERT_EQ(uuid.clock_seq_low, uuid_want.clock_seq_low);
	ASSERT_EQ(uuid.node[0], uuid_want.node[0]);
	ASSERT_EQ(uuid.node[1], uuid_want.node[1]);
	ASSERT_EQ(uuid.node[2], uuid_want.node[2]);
	ASSERT_EQ(uuid.node[3], uuid_want.node[3]);
	ASSERT_EQ(uuid.node[4], uuid_want.node[4]);
	ASSERT_EQ(uuid.node[5], uuid_want.node[5]);

	printf("ok\n");
	t++;

	printf("[%d] uuid_to_string ", t);
	/* re-use the handrolled struct uuid from the previous test. */
	uuid_str_want = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";

	uuid_to_string(&uuid, &uuid_str, &status);
	if (status != uuid_s_ok) {
		printf("failed to return uuid_s_ok, got %d\n", status);
		return 1;
	}

	if (strcmp(uuid_str, uuid_str_want) != 0) {
		printf("expected '%s', got '%s'\n", uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_create_nil ", t);
	uuid_create_nil(&uuid, &status);
	if (status != uuid_s_ok) {
		printf("failed to return uuid_s_ok, got: %d\n", status);
		return 1;
	}

	/*
	 * At this point we've done a previous test of uuid_to_string already,
	 * so might as well use it again for uuid_create_nil() here.
	 */
	uuid_to_string(&uuid, &uuid_str, &status);
	if (status != uuid_s_ok) {
		printf("uuid_to_string failed to return uuid_s_ok, got %d\n",
		    status);
		return 1;
	}

	uuid_str_want = "00000000-0000-0000-0000-000000000000";
	if (strcmp(uuid_str, uuid_str_want) != 0) {
		printf("expected '%s', got '%s'\n", uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	/*
	 * Assuming the clock of the system running the test is ahead of the one
	 * where this test was written, we can test uuid_create along with
	 * uuid_compare here.
	 */
	printf("[%d] uuid_create ", t);
	uuid_create(&uuid, &status);
	if (status != uuid_s_ok) {
		printf("uuid_create failed to return uuid_s_ok, got %d\n",
		    status);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_compare ", t);
	/* uuid was just generated, uuid2 was generated before. */
	uuid_from_string(uuid_str, &uuid2, &status);
	rc = uuid_compare(&uuid, &uuid2, &status);
	if ((status != uuid_s_ok) || (rc != 1)) {
		printf("uuid_compare failed, expected 1 got: %d and status: %d\n",
		    rc, status);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_equal ", t);
	rc = uuid_equal(&uuid, &uuid, &status);
	if ((status != uuid_s_ok) || (rc != 1)) {
		printf("uuid_compare failed, expected 1 got: %d and status: %d\n",
		    rc, status);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_equal (nil) ", t);
	uuid_create_nil(&uuid, &status);
	rc = uuid_equal(&uuid, &uuid2, &status);
	if ((status != uuid_s_ok) || (rc != 1)) {
		printf("uuid_compare failed, expected 1 got: %d and status: %d\n",
		    rc, status);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_hash ", t);
	uint16_t hash = uuid_hash(&uuid_want, &status);
	if ((status != uuid_s_ok) || (hash != 0x4fae)) {
		printf("uuid_hash failed, expected 0x4fae got: 0x%04x and status: %d\n",
		    hash, status);
		return 1;
	}

	printf("ok\n");
	t++;

	uuid_str_want = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";
	printf("[%d] uuid_enc_le ", t);
	uuid_from_string(uuid_str_want, &uuid, &status);
	/*
	 * Check two fields to ensure they're in the right order.
	 * If these two are ok, it's safe to assum the rest are too.
	 */
	uuid_enc_le(bin, &uuid);
	if (bin[4] != 0xec || bin[5] != 0x7d) {
		uuid_to_string(&uuid, &uuid_str, &status);
		printf("uuid_enc_le failed, expected %s got %s\n",
		    uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_dec_le ", t);
	uuid_dec_le(bin, &uuid);
	if (uuid_equal(&uuid, &uuid_want, &status) == 0) {
		uuid_to_string(&uuid, &uuid_str, &status);
		printf("uuid_dec_le failed, expected %s got %s\n",
		    uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_enc_be ", t);
	uuid_enc_be(bin, &uuid);
	if (bin[4] != 0x7d || bin[5] != 0xec) {
		uuid_to_string(&uuid, &uuid_str, &status);
		printf("uuid_enc_be failed, expected %s got %s\n",
		    uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	printf("[%d] uuid_dec_be ", t);
	uuid_dec_be(bin, &uuid);
	if (uuid_equal(&uuid, &uuid_want, &status) == 0) {
		uuid_to_string(&uuid, &uuid_str, &status);
		printf("uuid_dec_be failed, expected %s got %s\n",
		    uuid_str_want, uuid_str);
		return 1;
	}

	printf("ok\n");
	t++;

	return 0;
}
