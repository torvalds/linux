/* $OpenBSD: ssl_methods.c,v 1.32 2024/07/23 14:40:54 jsing Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include "dtls_local.h"
#include "ssl_local.h"
#include "tls13_internal.h"

static const SSL_METHOD DTLS_method_data = {
	.dtls = 1,
	.server = 1,
	.version = DTLS1_2_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

static const SSL_METHOD DTLS_client_method_data = {
	.dtls = 1,
	.server = 0,
	.version = DTLS1_2_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

static const SSL_METHOD DTLSv1_method_data = {
	.dtls = 1,
	.server = 1,
	.version = DTLS1_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_1_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_1_ENC_FLAGS,
};

static const SSL_METHOD DTLSv1_client_method_data = {
	.dtls = 1,
	.server = 0,
	.version = DTLS1_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_1_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_1_ENC_FLAGS,
};

static const SSL_METHOD DTLSv1_2_method_data = {
	.dtls = 1,
	.server = 1,
	.version = DTLS1_2_VERSION,
	.min_tls_version = TLS1_2_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

static const SSL_METHOD DTLSv1_2_client_method_data = {
	.dtls = 1,
	.server = 0,
	.version = DTLS1_2_VERSION,
	.min_tls_version = TLS1_2_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = dtls1_new,
	.ssl_clear = dtls1_clear,
	.ssl_free = dtls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = dtls1_read_bytes,
	.ssl_write_bytes = dtls1_write_app_data_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

const SSL_METHOD *
DTLSv1_client_method(void)
{
	return &DTLSv1_client_method_data;
}
LSSL_ALIAS(DTLSv1_client_method);

const SSL_METHOD *
DTLSv1_method(void)
{
	return &DTLSv1_method_data;
}
LSSL_ALIAS(DTLSv1_method);

const SSL_METHOD *
DTLSv1_server_method(void)
{
	return &DTLSv1_method_data;
}
LSSL_ALIAS(DTLSv1_server_method);

const SSL_METHOD *
DTLSv1_2_client_method(void)
{
	return &DTLSv1_2_client_method_data;
}
LSSL_ALIAS(DTLSv1_2_client_method);

const SSL_METHOD *
DTLSv1_2_method(void)
{
	return &DTLSv1_2_method_data;
}
LSSL_ALIAS(DTLSv1_2_method);

const SSL_METHOD *
DTLSv1_2_server_method(void)
{
	return &DTLSv1_2_method_data;
}
LSSL_ALIAS(DTLSv1_2_server_method);

const SSL_METHOD *
DTLS_client_method(void)
{
	return &DTLS_client_method_data;
}
LSSL_ALIAS(DTLS_client_method);

const SSL_METHOD *
DTLS_method(void)
{
	return &DTLS_method_data;
}
LSSL_ALIAS(DTLS_method);

const SSL_METHOD *
DTLS_server_method(void)
{
	return &DTLS_method_data;
}
LSSL_ALIAS(DTLS_server_method);

static const SSL_METHOD TLS_method_data = {
	.dtls = 0,
	.server = 1,
	.version = TLS1_3_VERSION,
	.min_tls_version = TLS1_VERSION,
	.max_tls_version = TLS1_3_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = tls13_legacy_accept,
	.ssl_connect = tls13_legacy_connect,
	.ssl_shutdown = tls13_legacy_shutdown,
	.ssl_renegotiate = ssl_undefined_function,
	.ssl_renegotiate_check = ssl_ok,
	.ssl_pending = tls13_legacy_pending,
	.ssl_read_bytes = tls13_legacy_read_bytes,
	.ssl_write_bytes = tls13_legacy_write_bytes,
	.enc_flags = TLSV1_3_ENC_FLAGS,
};

static const SSL_METHOD TLS_legacy_method_data = {
	.dtls = 0,
	.server = 1,
	.version = TLS1_2_VERSION,
	.min_tls_version = TLS1_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl_undefined_function,
	.ssl_renegotiate_check = ssl_ok,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

static const SSL_METHOD TLS_client_method_data = {
	.dtls = 0,
	.server = 0,
	.version = TLS1_3_VERSION,
	.min_tls_version = TLS1_VERSION,
	.max_tls_version = TLS1_3_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = tls13_legacy_accept,
	.ssl_connect = tls13_legacy_connect,
	.ssl_shutdown = tls13_legacy_shutdown,
	.ssl_renegotiate = ssl_undefined_function,
	.ssl_renegotiate_check = ssl_ok,
	.ssl_pending = tls13_legacy_pending,
	.ssl_read_bytes = tls13_legacy_read_bytes,
	.ssl_write_bytes = tls13_legacy_write_bytes,
	.enc_flags = TLSV1_3_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_method_data = {
	.dtls = 0,
	.server = 1,
	.version = TLS1_VERSION,
	.min_tls_version = TLS1_VERSION,
	.max_tls_version = TLS1_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_client_method_data = {
	.dtls = 0,
	.server = 0,
	.version = TLS1_VERSION,
	.min_tls_version = TLS1_VERSION,
	.max_tls_version = TLS1_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_1_method_data = {
	.dtls = 0,
	.server = 1,
	.version = TLS1_1_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_1_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_1_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_1_client_method_data = {
	.dtls = 0,
	.server = 0,
	.version = TLS1_1_VERSION,
	.min_tls_version = TLS1_1_VERSION,
	.max_tls_version = TLS1_1_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_1_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_2_method_data = {
	.dtls = 0,
	.server = 1,
	.version = TLS1_2_VERSION,
	.min_tls_version = TLS1_2_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl3_accept,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

static const SSL_METHOD TLSv1_2_client_method_data = {
	.dtls = 0,
	.server = 0,
	.version = TLS1_2_VERSION,
	.min_tls_version = TLS1_2_VERSION,
	.max_tls_version = TLS1_2_VERSION,
	.ssl_new = tls1_new,
	.ssl_clear = tls1_clear,
	.ssl_free = tls1_free,
	.ssl_accept = ssl_undefined_function,
	.ssl_connect = ssl3_connect,
	.ssl_shutdown = ssl3_shutdown,
	.ssl_renegotiate = ssl3_renegotiate,
	.ssl_renegotiate_check = ssl3_renegotiate_check,
	.ssl_pending = ssl3_pending,
	.ssl_read_bytes = ssl3_read_bytes,
	.ssl_write_bytes = ssl3_write_bytes,
	.enc_flags = TLSV1_2_ENC_FLAGS,
};

const SSL_METHOD *
TLS_client_method(void)
{
	return (&TLS_client_method_data);
}
LSSL_ALIAS(TLS_client_method);

const SSL_METHOD *
TLS_method(void)
{
	return (&TLS_method_data);
}
LSSL_ALIAS(TLS_method);

const SSL_METHOD *
TLS_server_method(void)
{
	return TLS_method();
}
LSSL_ALIAS(TLS_server_method);

const SSL_METHOD *
tls_legacy_method(void)
{
	return (&TLS_legacy_method_data);
}

const SSL_METHOD *
SSLv23_client_method(void)
{
	return TLS_client_method();
}
LSSL_ALIAS(SSLv23_client_method);

const SSL_METHOD *
SSLv23_method(void)
{
	return TLS_method();
}
LSSL_ALIAS(SSLv23_method);

const SSL_METHOD *
SSLv23_server_method(void)
{
	return TLS_method();
}
LSSL_ALIAS(SSLv23_server_method);

const SSL_METHOD *
TLSv1_client_method(void)
{
	return (&TLSv1_client_method_data);
}
LSSL_ALIAS(TLSv1_client_method);

const SSL_METHOD *
TLSv1_method(void)
{
	return (&TLSv1_method_data);
}
LSSL_ALIAS(TLSv1_method);

const SSL_METHOD *
TLSv1_server_method(void)
{
	return (&TLSv1_method_data);
}
LSSL_ALIAS(TLSv1_server_method);

const SSL_METHOD *
TLSv1_1_client_method(void)
{
	return (&TLSv1_1_client_method_data);
}
LSSL_ALIAS(TLSv1_1_client_method);

const SSL_METHOD *
TLSv1_1_method(void)
{
	return (&TLSv1_1_method_data);
}
LSSL_ALIAS(TLSv1_1_method);

const SSL_METHOD *
TLSv1_1_server_method(void)
{
	return (&TLSv1_1_method_data);
}
LSSL_ALIAS(TLSv1_1_server_method);

const SSL_METHOD *
TLSv1_2_client_method(void)
{
	return (&TLSv1_2_client_method_data);
}
LSSL_ALIAS(TLSv1_2_client_method);

const SSL_METHOD *
TLSv1_2_method(void)
{
	return (&TLSv1_2_method_data);
}
LSSL_ALIAS(TLSv1_2_method);

const SSL_METHOD *
TLSv1_2_server_method(void)
{
	return (&TLSv1_2_method_data);
}
LSSL_ALIAS(TLSv1_2_server_method);

const SSL_METHOD *
ssl_get_method(uint16_t version)
{
	if (version == TLS1_3_VERSION)
		return (TLS_method());
	if (version == TLS1_2_VERSION)
		return (TLSv1_2_method());
	if (version == TLS1_1_VERSION)
		return (TLSv1_1_method());
	if (version == TLS1_VERSION)
		return (TLSv1_method());
	if (version == DTLS1_VERSION)
		return (DTLSv1_method());
	if (version == DTLS1_2_VERSION)
		return (DTLSv1_2_method());

	return (NULL);
}
