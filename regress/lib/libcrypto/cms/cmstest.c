/*	$OpenBSD: cmstest.c,v 1.8 2024/03/29 06:42:42 tb Exp $	*/
/*
 * Copyright (c) 2019 Joel Sing <jsing@openbsd.org>
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

#include <openssl/cms.h>

static int verbose = 0;

static const char cms_msg[] = "Hello CMS!\r\n";

static const char cms_ca_1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICqDCCAZACCQD8ebR8e4kdvjANBgkqhkiG9w0BAQsFADAWMRQwEgYDVQQDDAtU\n"
    "ZXN0IENNUyBDQTAeFw0xOTA1MTExNTUzNTNaFw0yOTA1MDgxNTUzNTNaMBYxFDAS\n"
    "BgNVBAMMC1Rlc3QgQ01TIENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC\n"
    "AQEAoIiW3POGYfhY0BEgG8mIwouOI917M72jsuUE57ccjEXLWseItLb7r9vkiwW/\n"
    "FYbz0UYkJW1JgpZmWaTGOgZGxj+WTzxh1aq7OHyJb6Pxwp9wGrGJu+BEqOZN/bi/\n"
    "aQ1l8x7DxVJkFeI1+4QKDfmGYfWoVzQLgamO3u0vxz3Vi/XzX01ZomcZUYYx0lIq\n"
    "hxAO665HoPUmecqYdLPquJNxdfiy37ieLJOmIsKZJtMcCZAxqhcCwE7I0196Ng3P\n"
    "fK9Sl7BCyTBszb2YC2qOleuI2Wjg/7o1+hugopUkjxz0RGFu5s3K9PhCLwpqylXg\n"
    "IXe9Vwi38gKawD3yjtDBRDNmIwIDAQABMA0GCSqGSIb3DQEBCwUAA4IBAQAvsvtc\n"
    "cO0Eo0F6MvB0bjBIMHBkKyWcmD2c5gVFhbHyRD+XBVXNdn5CcBba2amm0VgShBpM\n"
    "4e1rOtIH/Hf6nB3c/EjZvd16ryoTCTvzayac7sD2Y8IxF1JIAKvjFbu+LmzM/F5f\n"
    "x3/WdY1qs5W7lO46i8xmSUAP88gohWP4cyVUAITNrh/RSOFaWUd5i1/vZ+iEexLI\n"
    "rQWsweJleOxvA8SrXm2gAkqRWEncsxOrsX/MsPl7iJoebLhWbS3cOHhutWrfhdlC\n"
    "2uT6K7SA9rn6qqmvI6mLkHJQpqq++Py2UTDo1u8VKa3ieYNUN070kgxpYiVBGs3L\n"
    "aaACIcEs48gnTRWc\n"
    "-----END CERTIFICATE-----\n";

static const char cms_cert_1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICpDCCAYwCAQMwDQYJKoZIhvcNAQEFBQAwFjEUMBIGA1UEAwwLVGVzdCBDTVMg\n"
    "Q0EwHhcNMTkwNTExMTU1MzU0WhcNMjkwNTA4MTU1MzU0WjAaMRgwFgYDVQQDDA9U\n"
    "ZXN0IENNUyBDZXJ0IDEwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDD\n"
    "MLSuy+tc0AwfrlszgHJ3z7UEpJSn5mcKxquFnEC5DtchgQJ+cj5VFvB9A9G98ykQ\n"
    "0IrHXNUTbS2yvf8ac1PlocuA8ggeDK4gPHCe097j0nUphhT0VzhwwFfP6Uo6VaR8\n"
    "B7Qb3zFTz64bN66V89etZ5NQJKMdrh4oOh5nfxLKvCcTK+9U4ZrgeGVdVXmL6HJp\n"
    "3m9CPobCBsC8DgI+zF/tg4GjDoVCJd6Tv5MRAmKiBrzTGglVeknkgiyIZ9C7gXU/\n"
    "7NMUihmLlt+80zr+nL0P+MA924WV4fZJi1wtf6Eioalq6n/9i93nBRCeu8bEOBrT\n"
    "pAre2oBEoULIJu7Ubx79AgMBAAEwDQYJKoZIhvcNAQEFBQADggEBADnLc6ZzApHq\n"
    "Z8l4zrFKAG/O/oULPMRTA8/zXNQ60BMV10hVtTCxVNq59d48wEljuUOGLfM91rhj\n"
    "gId8AOlsQbfRZE94DxlcaaAXaEjbkVSke56yfdLd4NqkIWrXGrFlbepj4b4ORAHh\n"
    "85kPwDEDnpMgQ63LqNX3gru3xf2AGIa1Fck2ISkVafqW5TH0Y6dCeGGFTtnH/QUT\n"
    "ofTm8uQ2vG9ERn+C1ooqJ2dyAckXFdmCcpor26vO/ZssMEKSee38ZNWR/01LEkOG\n"
    "G0+AL7E1mJdlVOtp3DDFN0hoNY7PbVuuzT+mrAwGLhCp2jnf68iNdrIuDdIE6yvi\n"
    "6WWvmmz+rC0=\n"
    "-----END CERTIFICATE-----\n";

static const char cms_key_1[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDDMLSuy+tc0Awf\n"
    "rlszgHJ3z7UEpJSn5mcKxquFnEC5DtchgQJ+cj5VFvB9A9G98ykQ0IrHXNUTbS2y\n"
    "vf8ac1PlocuA8ggeDK4gPHCe097j0nUphhT0VzhwwFfP6Uo6VaR8B7Qb3zFTz64b\n"
    "N66V89etZ5NQJKMdrh4oOh5nfxLKvCcTK+9U4ZrgeGVdVXmL6HJp3m9CPobCBsC8\n"
    "DgI+zF/tg4GjDoVCJd6Tv5MRAmKiBrzTGglVeknkgiyIZ9C7gXU/7NMUihmLlt+8\n"
    "0zr+nL0P+MA924WV4fZJi1wtf6Eioalq6n/9i93nBRCeu8bEOBrTpAre2oBEoULI\n"
    "Ju7Ubx79AgMBAAECggEAD4XkGLKm+S6iiDJ5llL0x4qBPulH2UJ9l2HNakbO7ui7\n"
    "OzLjW+MCCgpU/dw75ftcnLW5E7nSSEU6iSiLDTN2zKBdatfUxW8EuhOUcU0wQLYQ\n"
    "E0lSiUwWdQEW+rX27US6XBLQxBav+ZZeplN7UvmdgXDnSkxfnJCoXVKh8GEuwWip\n"
    "sM/Lwg8MSZK0o5qFVXtPp7kreB8CWlVyPYW5rDYy3k02R1t9k6WSdO2foPXe9rdZ\n"
    "iiThkALcHdBcFF0NHrIkAgMdtcAxkDIwO2kOnGJQKDXu+txbzPYodMU0Z6eVnlIu\n"
    "jh9ZjnZKBJgX6YVLVPRBwQXHXeGAnvMNm2WXH7SCAQKBgQDmMxvspc3K6HOqMoik\n"
    "59Rq1gXIuaGH0uSMSiUMTkr4laJbh9WgZ6JTAfIPuhj1xKGfDK7LF9VjPQ104SgL\n"
    "dCA1pV6nsuGS3j3vBnaMfmO7yr3yON+p/WDpKOgqC51Z3/pT8reJtMnyowQuDeYe\n"
    "UVRVyeXA11nve0SSc97US4AtXQKBgQDZERtgs6ejiUJQXuflu9HDczEZ/pHfPI1y\n"
    "+RU0tvI4860OTjerVJA2YBeOBLa9Y3hblvNpOU0SoVeMAGQLblEznJAl1nbaWqVY\n"
    "kPgvtQcTOL/awEB90JklvSRqR82WJchMOHMG5SeqrpUx3Dg+cPH6nId0e8UCt3/U\n"
    "W/u/5hP+IQKBgQDfReEmxaZ10MIm6P6p24Wm3dEcYBfxEjbEb0HBzspek1u3JWep\n"
    "PfsuQavTXy/IaKBOENIUgBhjOZssqxnZChgXkD7frtulRNOTW5RuLkRzp3BWWJ1v\n"
    "VifB3gBYj41d16UH+VnVQbnCEiUCuk5hR4bh8oJaaUV8xvW6ipItHNHErQKBgGoe\n"
    "2uuj6UkiSbFRNL4z3JFZN6AlvNsOl3imHZ/v8Ou29dwQkVbJuNdckydzVoOwpZ7h\n"
    "ZY8D3JJHHq3rYv3TqQ86c56MAv8tYbiy5yMrtZHIJMOlSeI4oSa6GZt8Dx5gylO5\n"
    "JUMxtPrU70u5BiZAwYxsCi0AdYimfXAsqB9hNFUBAoGBAJPT7Xsr7NIkrbv+aYXj\n"
    "rVVJ1qokUEKT6H1GmFXO3Fkw3kjPKS8VZloKOB7OiBC+AwMEQIflArCZ+PJdnVNO\n"
    "48ntHnaeaZk8rKXsYdJsqMKgIxZYZuCIazZz9WHeYxn5vkH76Q3DrfqrneJ3HSU/\n"
    "pFtLoXoGoVXRjAtpNvX7fh/G\n"
    "-----END PRIVATE KEY-----\n";

const char cms_ca_2[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBvTCCAW+gAwIBAgIQHioe49U1R3LcahmTCOUmoTAFBgMrZXAwXTEUMBIGA1UE\n"
    "ChMLQ01TIFRlc3QgQ0ExHTAbBgNVBAsMFGNtc3Rlc3RAbGlicmVzc2wub3JnMSYw\n"
    "JAYDVQQDDB1DTVMgVGVzdCBjbXN0ZXN0QGxpYnJlc3NsLm9yZzAeFw0yMzEwMDkw\n"
    "OTAzNDhaFw0zMzEwMDkwOTAzNDhaMF0xFDASBgNVBAoTC0NNUyBUZXN0IENBMR0w\n"
    "GwYDVQQLDBRjbXN0ZXN0QGxpYnJlc3NsLm9yZzEmMCQGA1UEAwwdQ01TIFRlc3Qg\n"
    "Y21zdGVzdEBsaWJyZXNzbC5vcmcwKjAFBgMrZXADIQAYj6pY7cN0DnwmsYHVDLqJ\n"
    "7/Futy5p4QJDKA/FSZ6+6KNFMEMwDgYDVR0PAQH/BAQDAgIEMBIGA1UdEwEB/wQI\n"
    "MAYBAf8CAQAwHQYDVR0OBBYEFE7G7c7O2Vj79+Q786M7ssMd/lflMAUGAytlcANB\n"
    "AOk+RHgs8D82saBM1nQMgIwEsNhYwbj3HhrRFDezYcnZeorBgiZTV3uQd2EndFdU\n"
    "hcs4OYMCRorxqpUXX6EMtwQ=\n"
    "-----END CERTIFICATE-----\n";

const char cms_cert_2[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIB5DCCAZagAwIBAgIQevuGe7FBHIc2pnQ4b4dsIzAFBgMrZXAwXTEUMBIGA1UE\n"
    "ChMLQ01TIFRlc3QgQ0ExHTAbBgNVBAsMFGNtc3Rlc3RAbGlicmVzc2wub3JnMSYw\n"
    "JAYDVQQDDB1DTVMgVGVzdCBjbXN0ZXN0QGxpYnJlc3NsLm9yZzAeFw0yMzEwMDkw\n"
    "OTAzNDhaFw0zMzEwMDkwOTAzNDhaMD4xHTAbBgNVBAoTFENNUyB0ZXN0IGNlcnRp\n"
    "ZmljYXRlMR0wGwYDVQQLDBRjbXN0ZXN0QGxpYnJlc3NsLm9yZzAqMAUGAytlcAMh\n"
    "AFH47Z54SuXMN+i5CCvMVUZJZzSYsDcRY+lPtc+J8h2ko4GKMIGHMA4GA1UdDwEB\n"
    "/wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwQwHwYDVR0jBBgw\n"
    "FoAUTsbtzs7ZWPv35Dvzozuywx3+V+UwNQYDVR0RBC4wLIIUY21zdGVzdC5saWJy\n"
    "ZXNzbC5vcmeBFGNtc3Rlc3RAbGlicmVzc2wub3JnMAUGAytlcANBAAEqYppowFjF\n"
    "fTZhNM3cIyFfmQthJV/+krEE2VTSoKgCokll+fXz1K9P+R3asgrVDoHjnBtvksIE\n"
    "wup36c05XQA=\n"
    "-----END CERTIFICATE-----\n";

const char cms_key_2[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MC4CAQAwBQYDK2VwBCIEIO88YApnGRDewzSwtxAnBvhlTPz9MjSz51mEpE2oi+9g\n"
    "-----END PRIVATE KEY-----\n";

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02x,%s", buf[i - 1], i % 8 ? "" : "\n");
	if (len % 8 != 0)
		fprintf(stderr, "\n");
}

static int
test_cms_encrypt_decrypt(void)
{
	STACK_OF(X509) *certs = NULL;
	CMS_ContentInfo *ci = NULL;
	EVP_PKEY *pkey = NULL;
	BIO *bio_mem = NULL;
	BIO *bio_out = NULL;
	X509 *cert = NULL;
	size_t len;
	long mem_len;
	char *p;
	int failed = 1;

	if ((bio_out = BIO_new_fp(stdout, BIO_NOCLOSE)) == NULL)
		errx(1, "failed to create BIO");

	if ((certs = sk_X509_new_null()) == NULL)
		errx(1, "failed to create certs");
	if ((bio_mem = BIO_new_mem_buf(cms_cert_1, -1)) == NULL)
		errx(1, "failed to create BIO for cert");
	if ((cert = PEM_read_bio_X509(bio_mem, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read cert");
	if (!sk_X509_push(certs, cert))
		errx(1, "failed to push cert");

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new_mem_buf(cms_key_1, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio_mem, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read key");

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new_mem_buf(cms_msg, -1)) == NULL)
		errx(1, "failed to create BIO for message");

	if ((ci = CMS_encrypt(certs, bio_mem, EVP_aes_256_cbc(), 0)) == NULL) {
		fprintf(stderr, "FAIL: CMS_encrypt returned NULL\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (verbose) {
		if (!CMS_ContentInfo_print_ctx(bio_out, ci, 0, NULL))
			errx(1, "failed to print CMS ContentInfo");
		if (!PEM_write_bio_CMS(bio_out, ci))
			errx(1, "failed to print CMS PEM");
	}

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
		errx(1, "failed to create BIO for message");

	if (!CMS_decrypt(ci, pkey, cert, NULL, bio_mem, 0)) {
		fprintf(stderr, "FAIL: CMS_decrypt failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if ((mem_len = BIO_get_mem_data(bio_mem, &p)) <= 0) {
		fprintf(stderr, "FAIL: BIO_get_mem_data returned %ld\n",
		    mem_len);
		goto failure;
	}
	if ((len = strlen(cms_msg)) != (size_t)mem_len) {
		fprintf(stderr, "FAIL: CMS decrypt returned %ld bytes, "
		    "want %zu bytes\n", mem_len, len);
		fprintf(stderr, "Got CMS data:\n");
		hexdump(p, mem_len);
		fprintf(stderr, "Want CMS data:\n");
		hexdump(cms_msg, len);
		goto failure;
	}
	if (memcmp(p, cms_msg, len) != 0) {
		fprintf(stderr, "FAIL: CMS decrypt message differs");
		fprintf(stderr, "Got CMS data:\n");
		hexdump(p, mem_len);
		fprintf(stderr, "Want CMS data:\n");
		hexdump(cms_msg, len);
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio_mem);
	BIO_free(bio_out);
	CMS_ContentInfo_free(ci);
	EVP_PKEY_free(pkey);
	sk_X509_free(certs);
	X509_free(cert);

	return failed;
}

static int
test_cms_sign_verify(const char *ca_pem, const char *cert_pem,
    const char *key_pem)
{
	STACK_OF(X509) *certs = NULL;
	CMS_ContentInfo *ci = NULL;
	X509_STORE *store = NULL;
	EVP_PKEY *pkey = NULL;
	BIO *bio_mem = NULL;
	BIO *bio_out = NULL;
	X509 *cert = NULL;
	X509 *ca = NULL;
	size_t len;
	long mem_len;
	char *p;
	int failed = 1;

	if ((bio_out = BIO_new_fp(stdout, BIO_NOCLOSE)) == NULL)
		errx(1, "failed to create BIO");

	if ((certs = sk_X509_new_null()) == NULL)
		errx(1, "failed to create certs");
	if ((bio_mem = BIO_new_mem_buf(cert_pem, -1)) == NULL)
		errx(1, "failed to create BIO for cert");
	if ((cert = PEM_read_bio_X509(bio_mem, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read cert");
	if (!sk_X509_push(certs, cert))
		errx(1, "failed to push cert");

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new_mem_buf(ca_pem, -1)) == NULL)
		errx(1, "failed to create BIO for cert");
	if ((ca = PEM_read_bio_X509(bio_mem, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read cert");
	if ((store = X509_STORE_new()) == NULL)
		errx(1, "failed to create X509 store");
	if (!X509_STORE_add_cert(store, ca))
		errx(1, "failed to add cert to store");

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new_mem_buf(key_pem, -1)) == NULL)
		errx(1, "failed to create BIO for key");
	if ((pkey = PEM_read_bio_PrivateKey(bio_mem, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read key");

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new_mem_buf(cms_msg, -1)) == NULL)
		errx(1, "failed to create BIO for message");

	if ((ci = CMS_sign(cert, pkey, NULL, bio_mem, 0)) == NULL) {
		fprintf(stderr, "FAIL: CMS sign failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (verbose) {
		if (!CMS_ContentInfo_print_ctx(bio_out, ci, 0, NULL))
			errx(1, "failed to print CMS ContentInfo");
		if (!PEM_write_bio_CMS(bio_out, ci))
			errx(1, "failed to print CMS PEM");
	}

	BIO_free(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
		errx(1, "failed to create BIO for message");

	if (!CMS_verify(ci, certs, store, NULL, bio_mem, 0)) {
		fprintf(stderr, "FAIL: CMS_verify failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if ((mem_len = BIO_get_mem_data(bio_mem, &p)) <= 0) {
		fprintf(stderr, "FAIL: BIO_get_mem_data returned %ld\n",
		    mem_len);
		goto failure;
	}
	if ((len = strlen(cms_msg)) != (size_t)mem_len) {
		fprintf(stderr, "FAIL: CMS verify returned %ld bytes, "
		    "want %zu bytes\n", mem_len, len);
		fprintf(stderr, "Got CMS data:\n");
		hexdump(p, mem_len);
		fprintf(stderr, "Want CMS data:\n");
		hexdump(cms_msg, len);
		goto failure;
	}
	if (memcmp(p, cms_msg, len) != 0) {
		fprintf(stderr, "FAIL: CMS verify message differs");
		fprintf(stderr, "Got CMS data:\n");
		hexdump(p, mem_len);
		fprintf(stderr, "Want CMS data:\n");
		hexdump(cms_msg, len);
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio_mem);
	BIO_free(bio_out);
	CMS_ContentInfo_free(ci);
	EVP_PKEY_free(pkey);
	sk_X509_free(certs);
	X509_free(cert);
	X509_STORE_free(store);
	X509_free(ca);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	ERR_load_crypto_strings();

	failed |= test_cms_encrypt_decrypt();
	failed |= test_cms_sign_verify(cms_ca_1, cms_cert_1, cms_key_1);
	failed |= test_cms_sign_verify(cms_ca_2, cms_cert_2, cms_key_2);

	return failed;
}
