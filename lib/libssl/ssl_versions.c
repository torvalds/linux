/* $OpenBSD: ssl_versions.c,v 1.27 2023/07/02 17:21:32 beck Exp $ */
/*
 * Copyright (c) 2016, 2017 Joel Sing <jsing@openbsd.org>
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

#include "ssl_local.h"

static uint16_t
ssl_dtls_to_tls_version(uint16_t dtls_ver)
{
	if (dtls_ver == DTLS1_VERSION)
		return TLS1_1_VERSION;
	if (dtls_ver == DTLS1_2_VERSION)
		return TLS1_2_VERSION;
	return 0;
}

static uint16_t
ssl_tls_to_dtls_version(uint16_t tls_ver)
{
	if (tls_ver == TLS1_1_VERSION)
		return DTLS1_VERSION;
	if (tls_ver == TLS1_2_VERSION)
		return DTLS1_2_VERSION;
	return 0;
}

static int
ssl_clamp_tls_version_range(uint16_t *min_ver, uint16_t *max_ver,
    uint16_t clamp_min, uint16_t clamp_max)
{
	if (clamp_min > clamp_max || *min_ver > *max_ver)
		return 0;
	if (clamp_max < *min_ver || clamp_min > *max_ver)
		return 0;

	if (*min_ver < clamp_min)
		*min_ver = clamp_min;
	if (*max_ver > clamp_max)
		*max_ver = clamp_max;

	return 1;
}

int
ssl_version_set_min(const SSL_METHOD *meth, uint16_t proto_ver,
    uint16_t max_tls_ver, uint16_t *out_tls_ver, uint16_t *out_proto_ver)
{
	uint16_t min_proto, min_version, max_version;

	if (proto_ver == 0) {
		*out_tls_ver = meth->min_tls_version;
		*out_proto_ver = 0;
		return 1;
	}

	min_version = proto_ver;
	max_version = max_tls_ver;

	if (meth->dtls) {
		if ((min_version = ssl_dtls_to_tls_version(proto_ver)) == 0)
			return 0;
	}

	if (!ssl_clamp_tls_version_range(&min_version, &max_version,
	    meth->min_tls_version, meth->max_tls_version))
		return 0;

	min_proto = min_version;
	if (meth->dtls) {
		if ((min_proto = ssl_tls_to_dtls_version(min_version)) == 0)
			return 0;
	}
	*out_tls_ver = min_version;
	*out_proto_ver = min_proto;

	return 1;
}

int
ssl_version_set_max(const SSL_METHOD *meth, uint16_t proto_ver,
    uint16_t min_tls_ver, uint16_t *out_tls_ver, uint16_t *out_proto_ver)
{
	uint16_t max_proto, min_version, max_version;

	if (proto_ver == 0) {
		*out_tls_ver = meth->max_tls_version;
		*out_proto_ver = 0;
		return 1;
	}

	min_version = min_tls_ver;
	max_version = proto_ver;

	if (meth->dtls) {
		if ((max_version = ssl_dtls_to_tls_version(proto_ver)) == 0)
			return 0;
	}

	if (!ssl_clamp_tls_version_range(&min_version, &max_version,
	    meth->min_tls_version, meth->max_tls_version))
		return 0;

	max_proto = max_version;
	if (meth->dtls) {
		if ((max_proto = ssl_tls_to_dtls_version(max_version)) == 0)
			return 0;
	}
	*out_tls_ver = max_version;
	*out_proto_ver = max_proto;

	return 1;
}

int
ssl_enabled_tls_version_range(SSL *s, uint16_t *min_ver, uint16_t *max_ver)
{
	uint16_t min_version, max_version;
	unsigned long options;

	/*
	 * The enabled versions have to be a contiguous range, which means we
	 * cannot enable and disable single versions at our whim, even though
	 * this is what the OpenSSL flags allow. The historical way this has
	 * been handled is by making a flag mean that all higher versions
	 * are disabled, if any version lower than the flag is enabled.
	 */

	min_version = 0;
	max_version = TLS1_3_VERSION;
	options = s->options;

	if (SSL_is_dtls(s)) {
		options = 0;
		if (s->options & SSL_OP_NO_DTLSv1)
			options |= SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
		if (s->options & SSL_OP_NO_DTLSv1_2)
			options |= SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_2;
	}

	if ((options & SSL_OP_NO_TLSv1_2) == 0)
		min_version = TLS1_2_VERSION;
	else if ((options & SSL_OP_NO_TLSv1_3) == 0)
		min_version = TLS1_3_VERSION;

	if ((options & SSL_OP_NO_TLSv1_3) && min_version < TLS1_3_VERSION)
		max_version = TLS1_2_VERSION;
	if ((options & SSL_OP_NO_TLSv1_2) && min_version < TLS1_2_VERSION)
		max_version = 0;

	/* Everything has been disabled... */
	if (min_version == 0 || max_version == 0)
		return 0;

	/* Limit to configured version range. */
	if (!ssl_clamp_tls_version_range(&min_version, &max_version,
	    s->min_tls_version, s->max_tls_version))
		return 0;

	/* QUIC requires a minimum of TLSv1.3. */
	if (SSL_is_quic(s)) {
		if (max_version < TLS1_3_VERSION)
			return 0;
		if (min_version < TLS1_3_VERSION)
			min_version = TLS1_3_VERSION;
	}

	if (min_ver != NULL)
		*min_ver = min_version;
	if (max_ver != NULL)
		*max_ver = max_version;

	return 1;
}

int
ssl_supported_tls_version_range(SSL *s, uint16_t *min_ver, uint16_t *max_ver)
{
	uint16_t min_version, max_version;

	if (!ssl_enabled_tls_version_range(s, &min_version, &max_version))
		return 0;

	/* Limit to the versions supported by this method. */
	if (!ssl_clamp_tls_version_range(&min_version, &max_version,
	    s->method->min_tls_version, s->method->max_tls_version))
		return 0;

	if (min_ver != NULL)
		*min_ver = min_version;
	if (max_ver != NULL)
		*max_ver = max_version;

	return 1;
}

uint16_t
ssl_tls_version(uint16_t version)
{
	if (version == TLS1_VERSION || version == TLS1_1_VERSION ||
	    version == TLS1_2_VERSION || version == TLS1_3_VERSION)
		return version;

	if (version == DTLS1_VERSION)
		return TLS1_1_VERSION;
	if (version == DTLS1_2_VERSION)
		return TLS1_2_VERSION;

	return 0;
}

uint16_t
ssl_effective_tls_version(SSL *s)
{
	if (s->s3->hs.negotiated_tls_version > 0)
		return s->s3->hs.negotiated_tls_version;

	return s->s3->hs.our_max_tls_version;
}

int
ssl_max_supported_version(SSL *s, uint16_t *max_ver)
{
	uint16_t max_version;

	*max_ver = 0;

	if (!ssl_supported_tls_version_range(s, NULL, &max_version))
		return 0;

	if (SSL_is_dtls(s)) {
		if ((max_version = ssl_tls_to_dtls_version(max_version)) == 0)
			return 0;
	}

	*max_ver = max_version;

	return 1;
}

int
ssl_max_legacy_version(SSL *s, uint16_t *max_ver)
{
	uint16_t max_version;

	if ((max_version = s->s3->hs.our_max_tls_version) > TLS1_2_VERSION)
		max_version = TLS1_2_VERSION;

	if (SSL_is_dtls(s)) {
		if ((max_version = ssl_tls_to_dtls_version(max_version)) == 0)
			return 0;
	}

	*max_ver = max_version;

	return 1;
}

int
ssl_max_shared_version(SSL *s, uint16_t peer_ver, uint16_t *max_ver)
{
	uint16_t min_version, max_version, peer_tls_version, shared_version;

	*max_ver = 0;
	peer_tls_version = peer_ver;

	if (SSL_is_dtls(s)) {
		if ((peer_ver >> 8) != DTLS1_VERSION_MAJOR)
			return 0;

		/*
		 * Convert the peer version to a TLS version - DTLS versions are
		 * the 1's complement of TLS version numbers (but not the actual
		 * protocol version numbers, that would be too sensible). Not to
		 * mention that DTLSv1.0 is really equivalent to DTLSv1.1.
		 */
		peer_tls_version = ssl_dtls_to_tls_version(peer_ver);

		/*
		 * This may be a version that we do not know about, if it is
		 * newer than DTLS1_2_VERSION (yes, less than is correct due
		 * to the "clever" versioning scheme), use TLS1_2_VERSION.
		 */
		if (peer_tls_version == 0) {
			if (peer_ver < DTLS1_2_VERSION)
				peer_tls_version = TLS1_2_VERSION;
		}
	}

	if (peer_tls_version >= TLS1_3_VERSION)
		shared_version = TLS1_3_VERSION;
	else if (peer_tls_version >= TLS1_2_VERSION)
		shared_version = TLS1_2_VERSION;
	else if (peer_tls_version >= TLS1_1_VERSION)
		shared_version = TLS1_1_VERSION;
	else if (peer_tls_version >= TLS1_VERSION)
		shared_version = TLS1_VERSION;
	else
		return 0;

	if (!ssl_supported_tls_version_range(s, &min_version, &max_version))
		return 0;

	if (shared_version < min_version)
		return 0;

	if (shared_version > max_version)
		shared_version = max_version;

	if (SSL_is_dtls(s)) {
		/*
		 * The resulting shared version will by definition be something
		 * that we know about. Switch back from TLS to DTLS.
		 */
		shared_version = ssl_tls_to_dtls_version(shared_version);
		if (shared_version == 0)
			return 0;
	}

	if (!ssl_security_version(s, shared_version))
		return 0;

	*max_ver = shared_version;

	return 1;
}

int
ssl_check_version_from_server(SSL *s, uint16_t server_version)
{
	uint16_t min_tls_version, max_tls_version, server_tls_version;

	/* Ensure that the version selected by the server is valid. */

	server_tls_version = server_version;
	if (SSL_is_dtls(s)) {
		server_tls_version = ssl_dtls_to_tls_version(server_version);
		if (server_tls_version == 0)
			return 0;
	}

	if (!ssl_supported_tls_version_range(s, &min_tls_version,
	    &max_tls_version))
		return 0;

	if (server_tls_version < min_tls_version ||
	    server_tls_version > max_tls_version)
		return 0;

	return ssl_security_version(s, server_tls_version);
}

int
ssl_legacy_stack_version(SSL *s, uint16_t version)
{
	if (SSL_is_dtls(s))
		return version == DTLS1_VERSION || version == DTLS1_2_VERSION;

	return version == TLS1_VERSION || version == TLS1_1_VERSION ||
	    version == TLS1_2_VERSION;
}
