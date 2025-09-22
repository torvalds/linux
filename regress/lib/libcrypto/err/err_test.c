/* $OpenBSD: err_test.c,v 1.2 2024/10/11 07:54:22 jsing Exp $ */
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#include <string.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

/*
 * This should also test:
 *  - error handling with more than ERR_NUM_ERRORS.
 *  - multi-threaded use.
 */

static int
err_test(void)
{
	const char *file, *s;
	char buf[2048];
	unsigned long err;
	int line;
	int failed = 1;

	ERR_load_crypto_strings();

	ERR_remove_state(0);

	ERR_clear_error();

	if ((err = ERR_peek_error()) != 0) {
		fprintf(stderr, "FAIL: ERR_peek_error() = %lx, want "
		    "0x0\n", err);
		goto failure;
	}
	if ((err = ERR_get_error()) != 0) {
		fprintf(stderr, "FAIL: ERR_get_error() = %lx, want "
		    "0x0\n", err);
		goto failure;
	}

	ERR_put_error(ERR_LIB_SYS, SYS_F_SOCKET, ERR_R_MALLOC_FAILURE,
	    "sys.c", 100);
	ERR_put_error(ERR_LIB_BN, BN_F_BN_USUB, BN_R_DIV_BY_ZERO,
	    "bn.c", 200);

	if ((err = ERR_peek_error()) != 0x2004041UL) {
		fprintf(stderr, "FAIL: ERR_peek_error() = %lx, want "
		    "0x2004041UL\n", err);
		goto failure;
	}
	if ((err = ERR_peek_error_line(&file, &line)) != 0x2004041UL) {
		fprintf(stderr, "FAIL: ERR_peek_error_line() = %lx, want "
		    "0x2004041\n", err);
		goto failure;
	}
	if (strcmp(file, "sys.c") != 0) {
		fprintf(stderr, "FAIL: got file '%s', want 'sys.c'", file);
		goto failure;
	}
	if (line != 100) {
		fprintf(stderr, "FAIL: line = %d, want 100", line);
		goto failure;
	}

	if ((err = ERR_peek_last_error()) != 0x3073067UL) {
		fprintf(stderr, "FAIL: ERR_peek_error() = %lx, want "
		    "0x3073067\n", err);
		goto failure;
	}
	if ((err = ERR_peek_last_error_line(&file, &line)) != 0x3073067UL) {
		fprintf(stderr, "FAIL: ERR_peek_last_error_line() = %lx, want "
		    "0x3073067\n", err);
		goto failure;
	}
	if (strcmp(file, "bn.c") != 0) {
		fprintf(stderr, "FAIL: got file '%s', want 'bn.c'", file);
		goto failure;
	}
	if (line != 200) {
		fprintf(stderr, "FAIL: line = %d, want 200", line);
		goto failure;
	}

	if ((err = ERR_get_error()) != 0x2004041UL) {
		fprintf(stderr, "FAIL: ERR_get_error() = %lx, want "
		    "0x2004041\n", err);
		goto failure;
	}

	if ((err = ERR_peek_error()) != 0x3073067UL) {
		fprintf(stderr, "FAIL: ERR_peek_error() = %lx, want "
		    "0x3073067\n", err);
		goto failure;
	}

	if ((err = ERR_get_error_line(&file, &line)) != 0x3073067UL) {
		fprintf(stderr, "FAIL: ERR_get_error_line() = %lx, want "
		    "0x3073067\n", err);
		goto failure;
	}
	if (strcmp(file, "bn.c") != 0) {
		fprintf(stderr, "FAIL: got file '%s', want 'bn.c'", file);
		goto failure;
	}
	if (line != 200) {
		fprintf(stderr, "FAIL: line = %d, want 200", line);
		goto failure;
	}

	if ((err = ERR_get_error()) != 0) {
		fprintf(stderr, "FAIL: ERR_get_error() = %lx, want "
		    "0x0\n", err);
		goto failure;
	}

	ERR_clear_error();

	/*
	 * Check SYSerror() reasons, which are dynamically populated from
	 * strerror().
	 */
	ERR_put_error(ERR_LIB_SYS, 0xfff, 1, "err.c", 300);

	if ((err = ERR_get_error()) != 0x2fff001UL) {
		fprintf(stderr, "FAIL: ERR_get_error() = %lx, want "
		    "0x2fff001UL\n", err);
		goto failure;
	}
	s = ERR_reason_error_string(err);
	if (strcmp(s, strerror(ERR_GET_REASON(err))) != 0) {
		fprintf(stderr, "FAIL: ERR_reason_error_string() = '%s', "
		    "want '%s'\n", s, strerror(ERR_GET_REASON(err)));
		goto failure;
	}

	s = ERR_lib_error_string(0x3fff067UL);
	if (strcmp(s, "bignum routines") != 0) {
		fprintf(stderr, "FAIL: ERR_lib_error_string() = '%s', "
		    "want 'bignum routines'\n", s);
		goto failure;
	}

	s = ERR_func_error_string(0x3fff067UL);
	if (strcmp(s, "CRYPTO_internal") != 0) {
		fprintf(stderr, "FAIL: ERR_func_error_string() = '%s', "
		    "want 'CRYPTO_internal'\n", s);
		goto failure;
	}

	s = ERR_reason_error_string(0x3fff067UL);
	if (strcmp(s, "div by zero") != 0) {
		fprintf(stderr, "FAIL: ERR_reason_error_string() = '%s', "
		    "want 'div by zero'\n", s);
		goto failure;
	}

	ERR_remove_state(0);

	s = ERR_error_string(0x3fff067UL, NULL);
	if (strcmp(s, "error:03FFF067:bignum routines:CRYPTO_internal:div by zero") != 0) {
		fprintf(stderr, "FAIL: ERR_error_string() = '%s', "
		    "want 'error:03FFF067:bignum routines:CRYPTO_internal:div by zero'\n", s);
		goto failure;
	}
	memset(buf, 0xdb, sizeof(buf));
	s = ERR_error_string(0x3fff067UL, buf);
	if (s != buf) {
		fprintf(stderr, "FAIL: ERR_error_string() did not "
		    "return buffer\n");
		goto failure;
	}
	if (strcmp(s, "error:03FFF067:bignum routines:CRYPTO_internal:div by zero") != 0) {
		fprintf(stderr, "FAIL: ERR_error_string() = '%s', "
		    "want 'error:03FFF067:bignum routines:CRYPTO_internal:div by zero'\n", s);
		goto failure;
	}

	memset(buf, 0xdb, sizeof(buf));
	ERR_error_string_n(0x3fff067UL, buf, sizeof(buf));
	if (strcmp(s, "error:03FFF067:bignum routines:CRYPTO_internal:div by zero") != 0) {
		fprintf(stderr, "FAIL: ERR_error_string() = '%s', "
		    "want 'error:03FFF067:bignum routines:CRYPTO_internal:div by zero'\n", s);
		goto failure;
	}

	failed = 0;

 failure:

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= err_test();

	/* Force a clean up. */
	OPENSSL_cleanup();

	return failed;
}
