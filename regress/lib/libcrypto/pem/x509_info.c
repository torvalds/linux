/*	$OpenBSD: x509_info.c,v 1.2 2020/09/18 14:41:04 tb Exp $	*/
/*
 * Copyright (c) 2020 Ingo Schwarze <schwarze@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

static const char *const bogus_pem = "\
-----BEGIN BOGUS----- \n\
-----END BOGUS----- \n\
";

static const char *const cert_pem = "\
-----BEGIN CERTIFICATE----- \n\
MIIDpTCCAo2gAwIBAgIJAPYm3GvOr5eTMA0GCSqGSIb3DQEBBQUAMHAxCzAJBgNV \n\
BAYTAlVLMRYwFAYDVQQKDA1PcGVuU1NMIEdyb3VwMSIwIAYDVQQLDBlGT1IgVEVT \n\
VElORyBQVVJQT1NFUyBPTkxZMSUwIwYDVQQDDBxPcGVuU1NMIFRlc3QgSW50ZXJt \n\
ZWRpYXRlIENBMB4XDTE0MDUyNDE0NDUxMVoXDTI0MDQwMTE0NDUxMVowZDELMAkG \n\
A1UEBhMCVUsxFjAUBgNVBAoMDU9wZW5TU0wgR3JvdXAxIjAgBgNVBAsMGUZPUiBU \n\
RVNUSU5HIFBVUlBPU0VTIE9OTFkxGTAXBgNVBAMMEFRlc3QgQ2xpZW50IENlcnQw \n\
ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC0ranbHRLcLVqN+0BzcZpY \n\
+yOLqxzDWT1LD9eW1stC4NzXX9/DCtSIVyN7YIHdGLrIPr64IDdXXaMRzgZ2rOKs \n\
lmHCAiFpO/ja99gGCJRxH0xwQatqAULfJVHeUhs7OEGOZc2nWifjqKvGfNTilP7D \n\
nwi69ipQFq9oS19FmhwVHk2wg7KZGHI1qDyG04UrfCZMRitvS9+UVhPpIPjuiBi2 \n\
x3/FZIpL5gXJvvFK6xHY63oq2asyzBATntBgnP4qJFWWcvRx24wF1PnZabxuVoL2 \n\
bPnQ/KvONDrw3IdqkKhYNTul7jEcu3OlcZIMw+7DiaKJLAzKb/bBF5gm/pwW6As9 \n\
AgMBAAGjTjBMMAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/BAQDAgXgMCwGCWCGSAGG \n\
+EIBDQQfFh1PcGVuU1NMIEdlbmVyYXRlZCBDZXJ0aWZpY2F0ZTANBgkqhkiG9w0B \n\
AQUFAAOCAQEAJzA4KTjkjXGSC4He63yX9Br0DneGBzjAwc1H6f72uqnCs8m7jgkE \n\
PQJFdTzQUKh97QPUuayZ2gl8XHagg+iWGy60Kw37gQ0+lumCN2sllvifhHU9R03H \n\
bWtS4kue+yQjMbrzf3zWygMDgwvFOUAIgBpH9qGc+CdNu97INTYd0Mvz51vLlxRn \n\
sC5aBYCWaZFnw3lWYxf9eVFRy9U+DkYFqX0LpmbDtcKP7AZGE6ZwSzaim+Cnoz1u \n\
Cgn+QmpFXgJKMFIZ82iSZISn+JkCCGxctZX1lMvai4Wi8Y0HxW9FTFZ6KBNwwE4B \n\
zjbN/ehBkgLlW/DWfi44DvwUHmuU6QP3cw== \n\
-----END CERTIFICATE----- \n\
";

int
main(void)
{
	BIO			*bp;
	STACK_OF(X509_INFO)	*skin, *skout;
	X509_INFO		*info0, *info1;
	const char		*errdata;
	unsigned long		 errcode;
	int			 errcount, errflags, num;

	errcount = 0;
	if ((skin = sk_X509_INFO_new_null()) == NULL)
		err(1, "sk_X509_INFO_new_null");

	/* Test with empty input. */

	if ((bp = BIO_new_mem_buf("", 0)) == NULL)
		err(1, "BIO_new_mem_buf(empty)");
	if ((skout = PEM_X509_INFO_read_bio(bp, skin, NULL, NULL)) == NULL)
		err(1, "empty input: %s",
		    ERR_error_string(ERR_get_error(), NULL));
	if (skout != skin)
		errx(1, "empty input did not return the same stack");
	skout = NULL;
	if ((num = sk_X509_INFO_num(skin)) != 0)
		errx(1, "empty input created %d X509_INFO objects", num);
	BIO_free(bp);

	/* Test with bogus input. */

	if ((bp = BIO_new_mem_buf(bogus_pem, strlen(bogus_pem))) == NULL)
		err(1, "BIO_new_mem_buf(bogus_pem)");
	if ((skout = PEM_X509_INFO_read_bio(bp, skin, NULL, NULL)) != NULL)
		errx(1, "success with bogus input on first try");
	if ((num = sk_X509_INFO_num(skin)) != 0)
		errx(1, "bogus input created %d X509_INFO objects", num);
	if (BIO_reset(bp) != 1)
		errx(1, "BIO_reset");

	/* Populate stack and test again with bogus input. */

	if ((info0 = X509_INFO_new()) == NULL)
		err(1, "X509_INFO_new");
	info0->references = 2;  /* X509_INFO_up_ref(3) doesn't exist. */
	if (sk_X509_INFO_push(skin, info0) != 1)
		err(1, "sk_X509_INFO_push");
	if ((skout = PEM_X509_INFO_read_bio(bp, skin, NULL, NULL)) != NULL)
		errx(1, "success with bogus input on second try");
	if ((num = sk_X509_INFO_num(skin)) != 1)
		errx(1, "bogus input changed stack size from 1 to %d", num);
	if (sk_X509_INFO_value(skin, 0) != info0)
		errx(1, "bogus input changed stack content");
	if (info0->references != 2) {
		warnx("bogus input changed ref count from 2 to %d",
		    info0->references);
		info0->references = 2;
		errcount++;
	}
	BIO_free(bp);

	/* Use a real certificate object. */

	if ((bp = BIO_new_mem_buf(cert_pem, strlen(cert_pem))) == NULL)
		err(1, "BIO_new_mem_buf(cert_pem)");
	if ((skout = PEM_X509_INFO_read_bio(bp, skin, NULL, NULL)) == NULL) {
		errdata = NULL;
		errflags = 0;
		while ((errcode = ERR_get_error_line_data(NULL, NULL,
		    &errdata, &errflags)) != 0)
			if (errdata != NULL && (errflags & ERR_TXT_STRING))
				warnx("%s --- %s",
				    ERR_error_string(errcode, NULL),
				    errdata);
			else
				warnx("%s", ERR_error_string(errcode, NULL));
		err(1, "real input: parsing failed");
	}
	if (skout != skin)
		errx(1, "real input did not return the same stack");
	skout = NULL;
	if ((num = sk_X509_INFO_num(skin)) != 2)
		errx(1, "real input changed stack size from 1 to %d", num);
	if (sk_X509_INFO_value(skin, 0) != info0)
		errx(1, "real input changed stack content");
	if (info0->references != 2)
		errx(1, "real input changed ref count from 2 to %d",
		    info0->references);
        info1 = sk_X509_INFO_pop(skin);
	if (info1->x509 == NULL)
		errx(1, "real input did not create a certificate");
	X509_INFO_free(info1);
	info1 = NULL;
	BIO_free(bp);

	/* Two real certificates followed by bogus input. */

	if ((bp = BIO_new(BIO_s_mem())) == NULL)
		err(1, "BIO_new");
	if (BIO_puts(bp, cert_pem) != strlen(cert_pem))
		err(1, "BIO_puts(cert_pem) first copy");
	if (BIO_puts(bp, cert_pem) != strlen(cert_pem))
		err(1, "BIO_puts(cert_pem) second copy");
	if (BIO_puts(bp, bogus_pem) != strlen(bogus_pem))
		err(1, "BIO_puts(bogus_pem)");
	if ((skout = PEM_X509_INFO_read_bio(bp, skin, NULL, NULL)) != NULL)
		errx(1, "success with real + bogus input");
	if ((num = sk_X509_INFO_num(skin)) != 1) {
		warnx("real + bogus input changed stack size from 1 to %d",
		    num);
		while (sk_X509_INFO_num(skin) > 1)
			(void)sk_X509_INFO_pop(skin);
		errcount++;
	}
	if (sk_X509_INFO_value(skin, 0) != info0)
		errx(1, "real + bogus input changed stack content");
	if (info0->references != 2) {
		warnx("real + bogus input changed ref count from 2 to %d",
		    info0->references);
		errcount++;
	}
	BIO_free(bp);
	info0->references = 1;
	X509_INFO_free(info0);
	sk_X509_INFO_free(skin);

	if (errcount > 0)
		errx(1, "%d errors detected", errcount);
	return 0;
}
