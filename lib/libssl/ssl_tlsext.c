/* $OpenBSD: ssl_tlsext.c,v 1.156 2025/06/07 10:23:21 tb Exp $ */
/*
 * Copyright (c) 2016, 2017, 2019 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2017 Doug Hogan <doug@openbsd.org>
 * Copyright (c) 2018-2019, 2024 Bob Beck <beck@openbsd.org>
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

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>

#include <openssl/ocsp.h>
#include <openssl/opensslconf.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "ssl_sigalgs.h"
#include "ssl_tlsext.h"

#define TLSEXT_TYPE_alpn TLSEXT_TYPE_application_layer_protocol_negotiation
#define TLSEXT_MAX_SUPPORTED_GROUPS 64

/*
 * Supported Application-Layer Protocol Negotiation - RFC 7301
 */

static int
tlsext_alpn_client_needs(SSL *s, uint16_t msg_type)
{
	/* ALPN protos have been specified and this is the initial handshake */
	return s->alpn_client_proto_list != NULL &&
	    s->s3->hs.finished_len == 0;
}

static int
tlsext_alpn_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB protolist;

	if (!CBB_add_u16_length_prefixed(cbb, &protolist))
		return 0;

	if (!CBB_add_bytes(&protolist, s->alpn_client_proto_list,
	    s->alpn_client_proto_list_len))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

int
tlsext_alpn_check_format(CBS *cbs)
{
	CBS proto_name_list;

	if (CBS_len(cbs) == 0)
		return 0;

	CBS_dup(cbs, &proto_name_list);
	while (CBS_len(&proto_name_list) > 0) {
		CBS proto_name;

		if (!CBS_get_u8_length_prefixed(&proto_name_list, &proto_name))
			return 0;
		if (CBS_len(&proto_name) == 0)
			return 0;
	}

	return 1;
}

static int
tlsext_alpn_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS alpn, selected_cbs;
	const unsigned char *selected;
	unsigned char selected_len;
	int r;

	if (!CBS_get_u16_length_prefixed(cbs, &alpn))
		return 0;
	if (!tlsext_alpn_check_format(&alpn))
		return 0;

	if (s->ctx->alpn_select_cb == NULL)
		return 1;

	/*
	 * XXX - A few things should be considered here:
	 * 1. Ensure that the same protocol is selected on session resumption.
	 * 2. Should the callback be called even if no ALPN extension was sent?
	 * 3. TLSv1.2 and earlier: ensure that SNI has already been processed.
	 */
	r = s->ctx->alpn_select_cb(s, &selected, &selected_len,
	    CBS_data(&alpn), CBS_len(&alpn), s->ctx->alpn_select_cb_arg);

	if (r == SSL_TLSEXT_ERR_OK) {
		CBS_init(&selected_cbs, selected, selected_len);

		if (!CBS_stow(&selected_cbs, &s->s3->alpn_selected,
		    &s->s3->alpn_selected_len)) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}

		return 1;
	}

	/* On SSL_TLSEXT_ERR_NOACK behave as if no callback was present. */
	if (r == SSL_TLSEXT_ERR_NOACK)
		return 1;

	*alert = SSL_AD_NO_APPLICATION_PROTOCOL;
	SSLerror(s, SSL_R_NO_APPLICATION_PROTOCOL);

	return 0;
}

static int
tlsext_alpn_server_needs(SSL *s, uint16_t msg_type)
{
	return s->s3->alpn_selected != NULL;
}

static int
tlsext_alpn_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB list, selected;

	if (!CBB_add_u16_length_prefixed(cbb, &list))
		return 0;

	if (!CBB_add_u8_length_prefixed(&list, &selected))
		return 0;

	if (!CBB_add_bytes(&selected, s->s3->alpn_selected,
	    s->s3->alpn_selected_len))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_alpn_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS list, proto;

	if (s->alpn_client_proto_list == NULL) {
		*alert = SSL_AD_UNSUPPORTED_EXTENSION;
		return 0;
	}

	if (!CBS_get_u16_length_prefixed(cbs, &list))
		return 0;

	if (!CBS_get_u8_length_prefixed(&list, &proto))
		return 0;

	if (CBS_len(&list) != 0)
		return 0;
	if (CBS_len(&proto) == 0)
		return 0;

	if (!CBS_stow(&proto, &s->s3->alpn_selected, &s->s3->alpn_selected_len))
		return 0;

	return 1;
}

/*
 * Supported Groups - RFC 7919 section 2
 */
static int
tlsext_supportedgroups_client_needs(SSL *s, uint16_t msg_type)
{
	return ssl_has_ecc_ciphers(s) ||
	    (s->s3->hs.our_max_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_supportedgroups_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	const uint16_t *groups;
	size_t groups_len;
	CBB grouplist;
	int i;

	tls1_get_group_list(s, 0, &groups, &groups_len);
	if (groups_len == 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return 0;
	}

	if (!CBB_add_u16_length_prefixed(cbb, &grouplist))
		return 0;

	for (i = 0; i < groups_len; i++) {
		if (!ssl_security_supported_group(s, groups[i]))
			continue;
		if (!CBB_add_u16(&grouplist, groups[i]))
			return 0;
	}

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_supportedgroups_server_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	uint16_t *groups = NULL;
	size_t groups_len;
	CBS grouplist;
	int i, j;
	int ret = 0;

	if (!CBS_get_u16_length_prefixed(cbs, &grouplist))
		goto err;

	groups_len = CBS_len(&grouplist);
	if (groups_len == 0 || groups_len % 2 != 0)
		goto err;
	groups_len /= 2;

	if (groups_len > TLSEXT_MAX_SUPPORTED_GROUPS)
		goto err;

	if (s->hit)
		goto done;

	if (s->s3->hs.tls13.hrr) {
		if (s->session->tlsext_supportedgroups == NULL) {
			*alert = SSL_AD_HANDSHAKE_FAILURE;
			return 0;
		}

		/*
		 * The ClientHello extension hashing ensures that the client
		 * did not change its list of supported groups.
		 */

		goto done;
	}

	if (s->session->tlsext_supportedgroups != NULL)
		goto err; /* XXX internal error? */

	if ((groups = reallocarray(NULL, groups_len, sizeof(uint16_t))) == NULL) {
		*alert = SSL_AD_INTERNAL_ERROR;
		goto err;
	}

	for (i = 0; i < groups_len; i++) {
		if (!CBS_get_u16(&grouplist, &groups[i]))
			goto err;
		/*
		 * Do not allow duplicate groups to be sent. This is not
		 * currently specified in RFC 8446 or earlier, but there is no
		 * legitimate justification for this to occur in TLS 1.2 or TLS
		 * 1.3.
		 */
		for (j = 0; j < i; j++) {
			if (groups[i] == groups[j]) {
				*alert = SSL_AD_ILLEGAL_PARAMETER;
				goto err;
			}
		}
	}

	if (CBS_len(&grouplist) != 0)
		goto err;

	s->session->tlsext_supportedgroups = groups;
	s->session->tlsext_supportedgroups_length = groups_len;
	groups = NULL;


 done:
	ret = 1;

 err:
	free(groups);

	return ret;
}

/* This extension is never used by the server. */
static int
tlsext_supportedgroups_server_needs(SSL *s, uint16_t msg_type)
{
	return 0;
}

static int
tlsext_supportedgroups_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return 0;
}

static int
tlsext_supportedgroups_client_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	/*
	 * This extension is only allowed in TLSv1.3 encrypted extensions.
	 * It is not permitted in a ServerHello in any version of TLS.
	 */
	if (msg_type != SSL_TLSEXT_MSG_EE)
		return 0;

	/*
	 * RFC 8446, section 4.2.7: TLSv1.3 servers can send this extension but
	 * clients must not act on it during the handshake. This allows servers
	 * to advertise their preferences for subsequent handshakes. We ignore
	 * this complication.
	 */
	if (!CBS_skip(cbs, CBS_len(cbs))) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}

	return 1;
}

/*
 * Supported Point Formats Extension - RFC 4492 section 5.1.2
 */
static int
tlsext_ecpf_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB ecpf;
	size_t formats_len;
	const uint8_t *formats;

	tls1_get_formatlist(s, 0, &formats, &formats_len);

	if (formats_len == 0) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return 0;
	}

	if (!CBB_add_u8_length_prefixed(cbb, &ecpf))
		return 0;
	if (!CBB_add_bytes(&ecpf, formats, formats_len))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_ecpf_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS ecpf;

	if (!CBS_get_u8_length_prefixed(cbs, &ecpf))
		return 0;
	if (CBS_len(&ecpf) == 0)
		return 0;

	/* Must contain uncompressed (0) - RFC 8422, section 5.1.2. */
	if (!CBS_contains_zero_byte(&ecpf)) {
		SSLerror(s, SSL_R_TLS_INVALID_ECPOINTFORMAT_LIST);
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}

	if (!s->hit) {
		if (!CBS_stow(&ecpf, &(s->session->tlsext_ecpointformatlist),
		    &(s->session->tlsext_ecpointformatlist_length))) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	}

	return 1;
}

static int
tlsext_ecpf_client_needs(SSL *s, uint16_t msg_type)
{
	return ssl_has_ecc_ciphers(s);
}

static int
tlsext_ecpf_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return tlsext_ecpf_build(s, msg_type, cbb);
}

static int
tlsext_ecpf_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	return tlsext_ecpf_process(s, msg_type, cbs, alert);
}

static int
tlsext_ecpf_server_needs(SSL *s, uint16_t msg_type)
{
	return ssl_using_ecc_cipher(s);
}

static int
tlsext_ecpf_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return tlsext_ecpf_build(s, msg_type, cbb);
}

static int
tlsext_ecpf_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	return tlsext_ecpf_process(s, msg_type, cbs, alert);
}

/*
 * Renegotiation Indication - RFC 5746.
 */
static int
tlsext_ri_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->renegotiate);
}

static int
tlsext_ri_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB reneg;

	if (!CBB_add_u8_length_prefixed(cbb, &reneg))
		return 0;
	if (!CBB_add_bytes(&reneg, s->s3->previous_client_finished,
	    s->s3->previous_client_finished_len))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_ri_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS reneg;

	if (!CBS_get_u8_length_prefixed(cbs, &reneg)) {
		SSLerror(s, SSL_R_RENEGOTIATION_ENCODING_ERR);
		return 0;
	}

	if (!CBS_mem_equal(&reneg, s->s3->previous_client_finished,
	    s->s3->previous_client_finished_len)) {
		SSLerror(s, SSL_R_RENEGOTIATION_MISMATCH);
		*alert = SSL_AD_HANDSHAKE_FAILURE;
		return 0;
	}

	s->s3->renegotiate_seen = 1;
	s->s3->send_connection_binding = 1;

	return 1;
}

static int
tlsext_ri_server_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.negotiated_tls_version < TLS1_3_VERSION &&
	    s->s3->send_connection_binding);
}

static int
tlsext_ri_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB reneg;

	if (!CBB_add_u8_length_prefixed(cbb, &reneg))
		return 0;
	if (!CBB_add_bytes(&reneg, s->s3->previous_client_finished,
	    s->s3->previous_client_finished_len))
		return 0;
	if (!CBB_add_bytes(&reneg, s->s3->previous_server_finished,
	    s->s3->previous_server_finished_len))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_ri_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS reneg, prev_client, prev_server;

	/*
	 * Ensure that the previous client and server values are both not
	 * present, or that they are both present.
	 */
	if ((s->s3->previous_client_finished_len == 0 &&
	    s->s3->previous_server_finished_len != 0) ||
	    (s->s3->previous_client_finished_len != 0 &&
	    s->s3->previous_server_finished_len == 0)) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}

	if (!CBS_get_u8_length_prefixed(cbs, &reneg)) {
		SSLerror(s, SSL_R_RENEGOTIATION_ENCODING_ERR);
		return 0;
	}
	if (!CBS_get_bytes(&reneg, &prev_client,
	    s->s3->previous_client_finished_len)) {
		SSLerror(s, SSL_R_RENEGOTIATION_ENCODING_ERR);
		return 0;
	}
	if (!CBS_get_bytes(&reneg, &prev_server,
	    s->s3->previous_server_finished_len)) {
		SSLerror(s, SSL_R_RENEGOTIATION_ENCODING_ERR);
		return 0;
	}
	if (CBS_len(&reneg) != 0) {
		SSLerror(s, SSL_R_RENEGOTIATION_ENCODING_ERR);
		return 0;
	}

	if (!CBS_mem_equal(&prev_client, s->s3->previous_client_finished,
	    s->s3->previous_client_finished_len)) {
		SSLerror(s, SSL_R_RENEGOTIATION_MISMATCH);
		*alert = SSL_AD_HANDSHAKE_FAILURE;
		return 0;
	}
	if (!CBS_mem_equal(&prev_server, s->s3->previous_server_finished,
	    s->s3->previous_server_finished_len)) {
		SSLerror(s, SSL_R_RENEGOTIATION_MISMATCH);
		*alert = SSL_AD_HANDSHAKE_FAILURE;
		return 0;
	}

	s->s3->renegotiate_seen = 1;
	s->s3->send_connection_binding = 1;

	return 1;
}

/*
 * Signature Algorithms - RFC 5246 section 7.4.1.4.1.
 */
static int
tlsext_sigalgs_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.our_max_tls_version >= TLS1_2_VERSION);
}

static int
tlsext_sigalgs_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	uint16_t tls_version = s->s3->hs.negotiated_tls_version;
	CBB sigalgs;

	if (msg_type == SSL_TLSEXT_MSG_CH)
		tls_version = s->s3->hs.our_min_tls_version;

	if (!CBB_add_u16_length_prefixed(cbb, &sigalgs))
		return 0;
	if (!ssl_sigalgs_build(tls_version, &sigalgs, SSL_get_security_level(s)))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_sigalgs_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS sigalgs;

	if (!CBS_get_u16_length_prefixed(cbs, &sigalgs))
		return 0;
	if (CBS_len(&sigalgs) % 2 != 0 || CBS_len(&sigalgs) > 64)
		return 0;
	if (!CBS_stow(&sigalgs, &s->s3->hs.sigalgs, &s->s3->hs.sigalgs_len))
		return 0;

	return 1;
}

static int
tlsext_sigalgs_server_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_sigalgs_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB sigalgs;

	if (!CBB_add_u16_length_prefixed(cbb, &sigalgs))
		return 0;
	if (!ssl_sigalgs_build(s->s3->hs.negotiated_tls_version, &sigalgs,
	    SSL_get_security_level(s)))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_sigalgs_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS sigalgs;

	if (ssl_effective_tls_version(s) < TLS1_3_VERSION)
		return 0;

	if (!CBS_get_u16_length_prefixed(cbs, &sigalgs))
		return 0;
	if (CBS_len(&sigalgs) % 2 != 0 || CBS_len(&sigalgs) > 64)
		return 0;
	if (!CBS_stow(&sigalgs, &s->s3->hs.sigalgs, &s->s3->hs.sigalgs_len))
		return 0;

	return 1;
}

/*
 * Server Name Indication - RFC 6066, section 3.
 */
static int
tlsext_sni_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->tlsext_hostname != NULL);
}

static int
tlsext_sni_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB server_name_list, host_name;

	if (!CBB_add_u16_length_prefixed(cbb, &server_name_list))
		return 0;
	if (!CBB_add_u8(&server_name_list, TLSEXT_NAMETYPE_host_name))
		return 0;
	if (!CBB_add_u16_length_prefixed(&server_name_list, &host_name))
		return 0;
	if (!CBB_add_bytes(&host_name, (const uint8_t *)s->tlsext_hostname,
	    strlen(s->tlsext_hostname)))
		return 0;
	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_sni_is_ip_literal(CBS *cbs, int *is_ip)
{
	union {
		struct in_addr ip4;
		struct in6_addr ip6;
	} addrbuf;
	char *hostname = NULL;

	*is_ip = 0;

	if (!CBS_strdup(cbs, &hostname))
		return 0;

	if (inet_pton(AF_INET, hostname, &addrbuf) == 1 ||
	    inet_pton(AF_INET6, hostname, &addrbuf) == 1)
		*is_ip = 1;

	free(hostname);

	return 1;
}

/*
 * Validate that the CBS contains only a hostname consisting of RFC 5890
 * compliant A-labels (see RFC 6066 section 3). Not a complete check
 * since we don't parse punycode to verify its validity but limits to
 * correct structure and character set.
 */
int
tlsext_sni_is_valid_hostname(CBS *cbs, int *is_ip)
{
	uint8_t prev, c = 0;
	int component = 0;
	CBS hostname;

	*is_ip = 0;

	CBS_dup(cbs, &hostname);

	if (CBS_len(&hostname) > TLSEXT_MAXLEN_host_name)
		return 0;

	/* An IP literal is invalid as a host name (RFC 6066 section 3). */
	if (!tlsext_sni_is_ip_literal(&hostname, is_ip))
		return 0;
	if (*is_ip)
		return 0;

	while (CBS_len(&hostname) > 0) {
		prev = c;
		if (!CBS_get_u8(&hostname, &c))
			return 0;
		/* Everything has to be ASCII, with no NUL byte. */
		if (!isascii(c) || c == '\0')
			return 0;
		/* It must be alphanumeric, a '-', or a '.' */
		if (!isalnum(c) && c != '-' && c != '.')
			return 0;
		/* '-' and '.' must not start a component or be at the end. */
		if (component == 0 || CBS_len(&hostname) == 0) {
			if (c == '-' || c == '.')
				return 0;
		}
		if (c == '.') {
			/* Components can not end with a dash. */
			if (prev == '-')
				return 0;
			/* Start new component */
			component = 0;
			continue;
		}
		/* Components must be 63 chars or less. */
		if (++component > 63)
			return 0;
	}

	return 1;
}

static int
tlsext_sni_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS server_name_list, host_name;
	uint8_t name_type;
	int is_ip;

	if (!CBS_get_u16_length_prefixed(cbs, &server_name_list))
		goto err;

	if (!CBS_get_u8(&server_name_list, &name_type))
		goto err;

	/*
	 * RFC 6066 section 3, only one type (host_name) is specified.
	 * We do not tolerate unknown types, neither does BoringSSL.
	 * other implementations appear more tolerant.
	 */
	if (name_type != TLSEXT_NAMETYPE_host_name) {
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		goto err;
	}

	/*
	 * RFC 6066 section 3 specifies a host name must be at least 1 byte
	 * so 0 length is a decode error.
	 */
	if (!CBS_get_u16_length_prefixed(&server_name_list, &host_name))
		goto err;
	if (CBS_len(&host_name) < 1)
		goto err;

	if (!tlsext_sni_is_valid_hostname(&host_name, &is_ip)) {
		/*
		 * Various pieces of software have been known to set the SNI
		 * host name to an IP address, even though that violates the
		 * RFC. If this is the case, pretend the SNI extension does
		 * not exist.
		 */
		if (is_ip)
			goto done;

		*alert = SSL_AD_ILLEGAL_PARAMETER;
		goto err;
	}

	if (s->hit || s->s3->hs.tls13.hrr) {
		if (s->session->tlsext_hostname == NULL) {
			*alert = SSL_AD_UNRECOGNIZED_NAME;
			goto err;
		}
		if (!CBS_mem_equal(&host_name, s->session->tlsext_hostname,
		    strlen(s->session->tlsext_hostname))) {
			*alert = SSL_AD_UNRECOGNIZED_NAME;
			goto err;
		}
	} else {
		if (s->session->tlsext_hostname != NULL)
			goto err;
		if (!CBS_strdup(&host_name, &s->session->tlsext_hostname)) {
			*alert = SSL_AD_INTERNAL_ERROR;
			goto err;
		}
	}

 done:
	/*
	 * RFC 6066 section 3 forbids multiple host names with the same type,
	 * therefore we allow only one entry.
	 */
	if (CBS_len(&server_name_list) != 0) {
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		goto err;
	}

	return 1;

 err:
	return 0;
}

static int
tlsext_sni_server_needs(SSL *s, uint16_t msg_type)
{
	if (s->hit)
		return 0;

	return (s->session->tlsext_hostname != NULL);
}

static int
tlsext_sni_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return 1;
}

static int
tlsext_sni_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	if (s->tlsext_hostname == NULL || CBS_len(cbs) != 0) {
		*alert = SSL_AD_UNRECOGNIZED_NAME;
		return 0;
	}

	if (s->hit) {
		if (s->session->tlsext_hostname == NULL) {
			*alert = SSL_AD_UNRECOGNIZED_NAME;
			return 0;
		}
		if (strcmp(s->tlsext_hostname,
		    s->session->tlsext_hostname) != 0) {
			*alert = SSL_AD_UNRECOGNIZED_NAME;
			return 0;
		}
	} else {
		if (s->session->tlsext_hostname != NULL)
			return 0;
		if ((s->session->tlsext_hostname =
		    strdup(s->tlsext_hostname)) == NULL) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	}

	return 1;
}

/*
 * Certificate Status Request - RFC 6066 section 8.
 */

static int
tlsext_ocsp_client_needs(SSL *s, uint16_t msg_type)
{
	if (msg_type != SSL_TLSEXT_MSG_CH)
		return 0;

	return (s->tlsext_status_type == TLSEXT_STATUSTYPE_ocsp);
}

static int
tlsext_ocsp_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB respid_list, respid, exts;
	unsigned char *ext_data;
	size_t ext_len;
	int i;

	if (!CBB_add_u8(cbb, TLSEXT_STATUSTYPE_ocsp))
		return 0;
	if (!CBB_add_u16_length_prefixed(cbb, &respid_list))
		return 0;
	for (i = 0; i < sk_OCSP_RESPID_num(s->tlsext_ocsp_ids); i++) {
		unsigned char *respid_data;
		OCSP_RESPID *id;
		size_t id_len;

		if ((id = sk_OCSP_RESPID_value(s->tlsext_ocsp_ids,
		    i)) ==  NULL)
			return 0;
		if ((id_len = i2d_OCSP_RESPID(id, NULL)) == -1)
			return 0;
		if (!CBB_add_u16_length_prefixed(&respid_list, &respid))
			return 0;
		if (!CBB_add_space(&respid, &respid_data, id_len))
			return 0;
		if ((i2d_OCSP_RESPID(id, &respid_data)) != id_len)
			return 0;
	}
	if (!CBB_add_u16_length_prefixed(cbb, &exts))
		return 0;
	if ((ext_len = i2d_X509_EXTENSIONS(s->tlsext_ocsp_exts,
	    NULL)) == -1)
		return 0;
	if (!CBB_add_space(&exts, &ext_data, ext_len))
		return 0;
	if ((i2d_X509_EXTENSIONS(s->tlsext_ocsp_exts, &ext_data) !=
	    ext_len))
		return 0;
	if (!CBB_flush(cbb))
		return 0;
	return 1;
}

static int
tlsext_ocsp_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	int alert_desc = SSL_AD_DECODE_ERROR;
	CBS respid_list, respid, exts;
	const unsigned char *p;
	uint8_t status_type;
	int ret = 0;

	if (msg_type != SSL_TLSEXT_MSG_CH)
		goto err;

	if (!CBS_get_u8(cbs, &status_type))
		goto err;
	if (status_type != TLSEXT_STATUSTYPE_ocsp) {
		/* ignore unknown status types */
		s->tlsext_status_type = -1;

		if (!CBS_skip(cbs, CBS_len(cbs))) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
		return 1;
	}
	s->tlsext_status_type = status_type;
	if (!CBS_get_u16_length_prefixed(cbs, &respid_list))
		goto err;

	/* XXX */
	sk_OCSP_RESPID_pop_free(s->tlsext_ocsp_ids, OCSP_RESPID_free);
	s->tlsext_ocsp_ids = NULL;
	if (CBS_len(&respid_list) > 0) {
		s->tlsext_ocsp_ids = sk_OCSP_RESPID_new_null();
		if (s->tlsext_ocsp_ids == NULL) {
			alert_desc = SSL_AD_INTERNAL_ERROR;
			goto err;
		}
	}

	while (CBS_len(&respid_list) > 0) {
		OCSP_RESPID *id;

		if (!CBS_get_u16_length_prefixed(&respid_list, &respid))
			goto err;
		p = CBS_data(&respid);
		if ((id = d2i_OCSP_RESPID(NULL, &p, CBS_len(&respid))) == NULL)
			goto err;
		if (!sk_OCSP_RESPID_push(s->tlsext_ocsp_ids, id)) {
			alert_desc = SSL_AD_INTERNAL_ERROR;
			OCSP_RESPID_free(id);
			goto err;
		}
	}

	/* Read in request_extensions */
	if (!CBS_get_u16_length_prefixed(cbs, &exts))
		goto err;
	if (CBS_len(&exts) > 0) {
		sk_X509_EXTENSION_pop_free(s->tlsext_ocsp_exts,
		    X509_EXTENSION_free);
		p = CBS_data(&exts);
		if ((s->tlsext_ocsp_exts = d2i_X509_EXTENSIONS(NULL,
		    &p, CBS_len(&exts))) == NULL)
			goto err;
	}

	ret = 1;
 err:
	if (ret == 0)
		*alert = alert_desc;
	return ret;
}

static int
tlsext_ocsp_server_needs(SSL *s, uint16_t msg_type)
{
	if (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION &&
	    s->tlsext_status_type == TLSEXT_STATUSTYPE_ocsp &&
	    s->ctx->tlsext_status_cb != NULL) {
		s->tlsext_status_expected = 0;
		if (s->ctx->tlsext_status_cb(s,
		    s->ctx->tlsext_status_arg) == SSL_TLSEXT_ERR_OK &&
		    s->tlsext_ocsp_resp_len > 0)
			s->tlsext_status_expected = 1;
	}
	return s->tlsext_status_expected;
}

static int
tlsext_ocsp_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB ocsp_response;

	if (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION) {
		if (!CBB_add_u8(cbb, TLSEXT_STATUSTYPE_ocsp))
			return 0;
		if (!CBB_add_u24_length_prefixed(cbb, &ocsp_response))
			return 0;
		if (!CBB_add_bytes(&ocsp_response,
		    s->tlsext_ocsp_resp,
		    s->tlsext_ocsp_resp_len))
			return 0;
		if (!CBB_flush(cbb))
			return 0;
	}
	return 1;
}

static int
tlsext_ocsp_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	uint8_t status_type;
	CBS response;

	if (ssl_effective_tls_version(s) >= TLS1_3_VERSION) {
		if (msg_type == SSL_TLSEXT_MSG_CR) {
			/*
			 * RFC 8446, 4.4.2.1 - the server may request an OCSP
			 * response with an empty status_request.
			 */
			if (CBS_len(cbs) == 0)
				return 1;

			SSLerror(s, SSL_R_LENGTH_MISMATCH);
			return 0;
		}
		if (!CBS_get_u8(cbs, &status_type)) {
			SSLerror(s, SSL_R_LENGTH_MISMATCH);
			return 0;
		}
		if (status_type != TLSEXT_STATUSTYPE_ocsp) {
			SSLerror(s, SSL_R_UNSUPPORTED_STATUS_TYPE);
			return 0;
		}
		if (!CBS_get_u24_length_prefixed(cbs, &response)) {
			SSLerror(s, SSL_R_LENGTH_MISMATCH);
			return 0;
		}
		if (CBS_len(&response) > 65536) {
			SSLerror(s, SSL_R_DATA_LENGTH_TOO_LONG);
			return 0;
		}
		if (!CBS_stow(&response, &s->tlsext_ocsp_resp,
		    &s->tlsext_ocsp_resp_len)) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	} else {
		if (s->tlsext_status_type == -1) {
			*alert = SSL_AD_UNSUPPORTED_EXTENSION;
			return 0;
		}
		/* Set flag to expect CertificateStatus message */
		s->tlsext_status_expected = 1;
	}
	return 1;
}

/*
 * SessionTicket extension - RFC 5077 section 3.2
 */
static int
tlsext_sessionticket_client_needs(SSL *s, uint16_t msg_type)
{
	/*
	 * Send session ticket extension when enabled and not overridden.
	 *
	 * When renegotiating, send an empty session ticket to indicate support.
	 */
	if ((SSL_get_options(s) & SSL_OP_NO_TICKET) != 0)
		return 0;

	if (!ssl_security_tickets(s))
		return 0;

	if (s->new_session)
		return 1;

	if (s->tlsext_session_ticket != NULL &&
	    s->tlsext_session_ticket->data == NULL)
		return 0;

	return 1;
}

static int
tlsext_sessionticket_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	/*
	 * Signal that we support session tickets by sending an empty
	 * extension when renegotiating or no session found.
	 */
	if (s->new_session || s->session == NULL)
		return 1;

	if (s->session->tlsext_tick != NULL) {
		/* Attempt to resume with an existing session ticket */
		if (!CBB_add_bytes(cbb, s->session->tlsext_tick,
		    s->session->tlsext_ticklen))
			return 0;

	} else if (s->tlsext_session_ticket != NULL) {
		/*
		 * Attempt to resume with a custom provided session ticket set
		 * by SSL_set_session_ticket_ext().
		 */
		if (s->tlsext_session_ticket->length > 0) {
			size_t ticklen = s->tlsext_session_ticket->length;

			if ((s->session->tlsext_tick = malloc(ticklen)) == NULL)
				return 0;
			memcpy(s->session->tlsext_tick,
			    s->tlsext_session_ticket->data,
			    ticklen);
			s->session->tlsext_ticklen = ticklen;

			if (!CBB_add_bytes(cbb, s->session->tlsext_tick,
			    s->session->tlsext_ticklen))
				return 0;
		}
	}

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_sessionticket_server_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	if (s->tls_session_ticket_ext_cb) {
		if (!s->tls_session_ticket_ext_cb(s, CBS_data(cbs),
		    (int)CBS_len(cbs),
		    s->tls_session_ticket_ext_cb_arg)) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	}

	/* We need to signal that this was processed fully */
	if (!CBS_skip(cbs, CBS_len(cbs))) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}

	return 1;
}

static int
tlsext_sessionticket_server_needs(SSL *s, uint16_t msg_type)
{
	return (s->tlsext_ticket_expected &&
	    !(SSL_get_options(s) & SSL_OP_NO_TICKET) &&
	    ssl_security_tickets(s));
}

static int
tlsext_sessionticket_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	/* Empty ticket */
	return 1;
}

static int
tlsext_sessionticket_client_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	if (s->tls_session_ticket_ext_cb) {
		if (!s->tls_session_ticket_ext_cb(s, CBS_data(cbs),
		    (int)CBS_len(cbs),
		    s->tls_session_ticket_ext_cb_arg)) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	}

	if ((SSL_get_options(s) & SSL_OP_NO_TICKET) != 0 || CBS_len(cbs) > 0) {
		*alert = SSL_AD_UNSUPPORTED_EXTENSION;
		return 0;
	}

	s->tlsext_ticket_expected = 1;

	return 1;
}

/*
 * DTLS extension for SRTP key establishment - RFC 5764
 */

#ifndef OPENSSL_NO_SRTP

static int
tlsext_srtp_client_needs(SSL *s, uint16_t msg_type)
{
	return SSL_is_dtls(s) && SSL_get_srtp_profiles(s) != NULL;
}

static int
tlsext_srtp_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB profiles, mki;
	int ct, i;
	STACK_OF(SRTP_PROTECTION_PROFILE) *clnt = NULL;
	const SRTP_PROTECTION_PROFILE *prof;

	if ((clnt = SSL_get_srtp_profiles(s)) == NULL) {
		SSLerror(s, SSL_R_EMPTY_SRTP_PROTECTION_PROFILE_LIST);
		return 0;
	}

	if ((ct = sk_SRTP_PROTECTION_PROFILE_num(clnt)) < 1) {
		SSLerror(s, SSL_R_EMPTY_SRTP_PROTECTION_PROFILE_LIST);
		return 0;
	}

	if (!CBB_add_u16_length_prefixed(cbb, &profiles))
		return 0;

	for (i = 0; i < ct; i++) {
		if ((prof = sk_SRTP_PROTECTION_PROFILE_value(clnt, i)) == NULL)
			return 0;
		if (!CBB_add_u16(&profiles, prof->id))
			return 0;
	}

	if (!CBB_add_u8_length_prefixed(cbb, &mki))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_srtp_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	const SRTP_PROTECTION_PROFILE *cprof, *sprof;
	STACK_OF(SRTP_PROTECTION_PROFILE) *clnt = NULL, *srvr;
	int i, j;
	int ret;
	uint16_t id;
	CBS profiles, mki;

	ret = 0;

	if (!CBS_get_u16_length_prefixed(cbs, &profiles))
		goto err;
	if (CBS_len(&profiles) == 0 || CBS_len(&profiles) % 2 != 0)
		goto err;

	if ((clnt = sk_SRTP_PROTECTION_PROFILE_new_null()) == NULL)
		goto err;

	while (CBS_len(&profiles) > 0) {
		if (!CBS_get_u16(&profiles, &id))
			goto err;

		if (!srtp_find_profile_by_num(id, &cprof)) {
			if (!sk_SRTP_PROTECTION_PROFILE_push(clnt, cprof))
				goto err;
		}
	}

	if (!CBS_get_u8_length_prefixed(cbs, &mki) || CBS_len(&mki) != 0) {
		SSLerror(s, SSL_R_BAD_SRTP_MKI_VALUE);
		goto done;
	}

	/*
	 * Per RFC 5764 section 4.1.1
	 *
	 * Find the server preferred profile using the client's list.
	 *
	 * The server MUST send a profile if it sends the use_srtp
	 * extension.  If one is not found, it should fall back to the
	 * negotiated DTLS cipher suite or return a DTLS alert.
	 */
	if ((srvr = SSL_get_srtp_profiles(s)) == NULL)
		goto err;
	for (i = 0; i < sk_SRTP_PROTECTION_PROFILE_num(srvr); i++) {
		if ((sprof = sk_SRTP_PROTECTION_PROFILE_value(srvr, i)) == NULL)
			goto err;

		for (j = 0; j < sk_SRTP_PROTECTION_PROFILE_num(clnt); j++) {
			if ((cprof = sk_SRTP_PROTECTION_PROFILE_value(clnt, j))
			    == NULL)
				goto err;

			if (cprof->id == sprof->id) {
				s->srtp_profile = sprof;
				ret = 1;
				goto done;
			}
		}
	}

	/* If we didn't find anything, fall back to the negotiated */
	ret = 1;
	goto done;

 err:
	SSLerror(s, SSL_R_BAD_SRTP_PROTECTION_PROFILE_LIST);

 done:
	sk_SRTP_PROTECTION_PROFILE_free(clnt);
	return ret;
}

static int
tlsext_srtp_server_needs(SSL *s, uint16_t msg_type)
{
	return SSL_is_dtls(s) && SSL_get_selected_srtp_profile(s) != NULL;
}

static int
tlsext_srtp_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	SRTP_PROTECTION_PROFILE *profile;
	CBB srtp, mki;

	if (!CBB_add_u16_length_prefixed(cbb, &srtp))
		return 0;

	if ((profile = SSL_get_selected_srtp_profile(s)) == NULL)
		return 0;

	if (!CBB_add_u16(&srtp, profile->id))
		return 0;

	if (!CBB_add_u8_length_prefixed(cbb, &mki))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_srtp_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	STACK_OF(SRTP_PROTECTION_PROFILE) *clnt;
	const SRTP_PROTECTION_PROFILE *prof;
	int i;
	uint16_t id;
	CBS profile_ids, mki;

	if (!CBS_get_u16_length_prefixed(cbs, &profile_ids)) {
		SSLerror(s, SSL_R_BAD_SRTP_PROTECTION_PROFILE_LIST);
		return 0;
	}

	if (!CBS_get_u16(&profile_ids, &id) || CBS_len(&profile_ids) != 0) {
		SSLerror(s, SSL_R_BAD_SRTP_PROTECTION_PROFILE_LIST);
		return 0;
	}

	if (!CBS_get_u8_length_prefixed(cbs, &mki) || CBS_len(&mki) != 0) {
		SSLerror(s, SSL_R_BAD_SRTP_MKI_VALUE);
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}

	if ((clnt = SSL_get_srtp_profiles(s)) == NULL) {
		SSLerror(s, SSL_R_NO_SRTP_PROFILES);
		return 0;
	}

	for (i = 0; i < sk_SRTP_PROTECTION_PROFILE_num(clnt); i++) {
		if ((prof = sk_SRTP_PROTECTION_PROFILE_value(clnt, i))
		    == NULL) {
			SSLerror(s, SSL_R_NO_SRTP_PROFILES);
			return 0;
		}

		if (prof->id == id) {
			s->srtp_profile = prof;
			return 1;
		}
	}

	SSLerror(s, SSL_R_BAD_SRTP_PROTECTION_PROFILE_LIST);

	return 0;
}

#endif /* OPENSSL_NO_SRTP */

/*
 * TLSv1.3 Key Share - RFC 8446 section 4.2.8.
 */
static int
tlsext_keyshare_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.our_max_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_keyshare_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB client_shares, key_exchange;

	if (!CBB_add_u16_length_prefixed(cbb, &client_shares))
		return 0;

	if (!CBB_add_u16(&client_shares,
	    tls_key_share_group(s->s3->hs.key_share)))
		return 0;
	if (!CBB_add_u16_length_prefixed(&client_shares, &key_exchange))
		return 0;
	if (!tls_key_share_public(s->s3->hs.key_share, &key_exchange))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_keyshare_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	const uint16_t *client_groups = NULL, *server_groups = NULL;
	size_t client_groups_len = 0, server_groups_len = 0;
	size_t i, j, client_groups_index;
	int preferred_group_found = 0;
	int decode_error;
	uint16_t client_preferred_group = 0;
	uint16_t group;
	CBS client_shares, key_exchange;

	/*
	 * RFC 8446 section 4.2.8:
	 *
	 * Each KeyShareEntry value MUST correspond to a group offered in the
	 * "supported_groups" extension and MUST appear in the same order.
	 * However, the values MAY be a non-contiguous subset of the
	 * "supported_groups".
	 */

	if (!tlsext_extension_seen(s, TLSEXT_TYPE_supported_groups)) {
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}
	if (!tlsext_extension_processed(s, TLSEXT_TYPE_supported_groups)) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}

	if (s->s3->hs.tls13.hrr) {
		if (!CBS_get_u16_length_prefixed(cbs, &client_shares))
			return 0;

		/* Unpack client share. */
		if (!CBS_get_u16(&client_shares, &group))
			return 0;
		if (!CBS_get_u16_length_prefixed(&client_shares, &key_exchange))
			return 0;

		/* There should only be one share. */
		if (CBS_len(&client_shares) != 0)
			return 0;

		if (group != s->s3->hs.tls13.server_group) {
			*alert = SSL_AD_ILLEGAL_PARAMETER;
			return 0;
		}

		if (s->s3->hs.key_share != NULL) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}

		/* Decode and store the selected key share. */
		if ((s->s3->hs.key_share = tls_key_share_new(group)) == NULL) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
		if (!tls_key_share_peer_public(s->s3->hs.key_share,
		    &key_exchange, &decode_error, NULL)) {
			if (!decode_error)
				*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}

		return 1;
	}

	/*
	 * XXX similar to tls1_get_supported_group, but client pref
	 * only - consider deduping later.
	 */
	/*
	 * We are now assured of at least one client group.
	 * Get the client and server group preference orders.
	 */
	tls1_get_group_list(s, 0, &server_groups, &server_groups_len);
	tls1_get_group_list(s, 1, &client_groups, &client_groups_len);

	/*
	 * Find the group that is most preferred by the client that
	 * we also support.
	 */
	for (i = 0; i < client_groups_len && !preferred_group_found; i++) {
		if (!ssl_security_supported_group(s, client_groups[i]))
			continue;
		for (j = 0; j < server_groups_len; j++) {
			if (server_groups[j] == client_groups[i]) {
				client_preferred_group = client_groups[i];
				preferred_group_found = 1;
				break;
			}
		}
	}

	if (!CBS_get_u16_length_prefixed(cbs, &client_shares))
		return 0;

	client_groups_index = 0;
	while (CBS_len(&client_shares) > 0) {
		int client_sent_group;

		/* Unpack client share. */
		if (!CBS_get_u16(&client_shares, &group))
			return 0;
		if (!CBS_get_u16_length_prefixed(&client_shares, &key_exchange))
			return 0;

		/* Ignore this client share if we're using earlier than TLSv1.3 */
		if (s->s3->hs.our_max_tls_version < TLS1_3_VERSION)
			continue;

		/*
		 * Ensure the client share group was sent in supported groups,
		 * and was sent in the same order as supported groups. The
		 * supported groups has already been checked for duplicates.
		 */
		client_sent_group = 0;
		while (client_groups_index < client_groups_len) {
			if (group == client_groups[client_groups_index++]) {
				client_sent_group = 1;
				break;
			}
		}
		if (!client_sent_group) {
			*alert = SSL_AD_ILLEGAL_PARAMETER;
			return 0;
		}

		/* Ignore this client share if we have already selected a key share */
		if (s->s3->hs.key_share != NULL)
			continue;

		/*
		 * Ignore this client share if it is not for the most client
		 * preferred supported group. This avoids a potential downgrade
		 * situation where the client sends a client share for something
		 * less preferred, and we choose to to use it instead of
		 * requesting the more preferred group.
		 */
		if (!preferred_group_found || group != client_preferred_group)
			continue;

		/* Decode and store the selected key share. */
		if ((s->s3->hs.key_share = tls_key_share_new(group)) == NULL) {
			*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
		if (!tls_key_share_peer_public(s->s3->hs.key_share,
		    &key_exchange, &decode_error, NULL)) {
			if (!decode_error)
				*alert = SSL_AD_INTERNAL_ERROR;
			return 0;
		}
	}

	return 1;
}

static int
tlsext_keyshare_server_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION &&
	    tlsext_extension_seen(s, TLSEXT_TYPE_key_share));
}

static int
tlsext_keyshare_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB key_exchange;

	/* In the case of a HRR, we only send the server selected group. */
	if (s->s3->hs.tls13.hrr) {
		if (s->s3->hs.tls13.server_group == 0)
			return 0;
		return CBB_add_u16(cbb, s->s3->hs.tls13.server_group);
	}

	if (s->s3->hs.key_share == NULL)
		return 0;

	if (!CBB_add_u16(cbb, tls_key_share_group(s->s3->hs.key_share)))
		return 0;
	if (!CBB_add_u16_length_prefixed(cbb, &key_exchange))
		return 0;
	if (!tls_key_share_public(s->s3->hs.key_share, &key_exchange))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_keyshare_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS key_exchange;
	int decode_error;
	uint16_t group;

	/* Unpack server share. */
	if (!CBS_get_u16(cbs, &group))
		return 0;

	if (CBS_len(cbs) == 0) {
		/* HRR does not include an actual key share, only the group. */
		if (msg_type != SSL_TLSEXT_MSG_HRR)
			return 0;

		s->s3->hs.tls13.server_group = group;
		return 1;
	}

	if (!CBS_get_u16_length_prefixed(cbs, &key_exchange))
		return 0;

	if (s->s3->hs.key_share == NULL) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}
	if (tls_key_share_group(s->s3->hs.key_share) != group) {
		*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}
	if (!tls_key_share_peer_public(s->s3->hs.key_share,
	    &key_exchange, &decode_error, NULL)) {
		if (!decode_error)
			*alert = SSL_AD_INTERNAL_ERROR;
		return 0;
	}

	return 1;
}

/*
 * Supported Versions - RFC 8446 section 4.2.1.
 */
static int
tlsext_versions_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.our_max_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_versions_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	uint16_t max, min;
	uint16_t version;
	CBB versions;

	max = s->s3->hs.our_max_tls_version;
	min = s->s3->hs.our_min_tls_version;

	if (!CBB_add_u8_length_prefixed(cbb, &versions))
		return 0;

	/* XXX - fix, but contiguous for now... */
	for (version = max; version >= min; version--) {
		if (!CBB_add_u16(&versions, version))
			return 0;
	}

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_versions_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS versions;
	uint16_t version;
	uint16_t max, min;
	uint16_t matched_version = 0;

	max = s->s3->hs.our_max_tls_version;
	min = s->s3->hs.our_min_tls_version;

	if (!CBS_get_u8_length_prefixed(cbs, &versions))
		return 0;

	while (CBS_len(&versions) > 0) {
		if (!CBS_get_u16(&versions, &version))
			return 0;
		/*
		 * XXX What is below implements client preference, and
		 * ignores any server preference entirely.
		 */
		if (matched_version == 0 && version >= min && version <= max)
			matched_version = version;
	}

	if (matched_version > 0)  {
		/* XXX - this should be stored for later processing. */
		s->version = matched_version;
		return 1;
	}

	*alert = SSL_AD_PROTOCOL_VERSION;
	return 0;
}

static int
tlsext_versions_server_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_versions_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return CBB_add_u16(cbb, TLS1_3_VERSION);
}

static int
tlsext_versions_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	uint16_t selected_version;

	if (!CBS_get_u16(cbs, &selected_version))
		return 0;

	/* XXX - need to fix for DTLS 1.3 */
	if (selected_version < TLS1_3_VERSION) {
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}

	/* XXX test between min and max once initialization code goes in */
	s->s3->hs.tls13.server_version = selected_version;

	return 1;
}


/*
 * Cookie - RFC 8446 section 4.2.2.
 */

static int
tlsext_cookie_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.our_max_tls_version >= TLS1_3_VERSION &&
	    s->s3->hs.tls13.cookie_len > 0 && s->s3->hs.tls13.cookie != NULL);
}

static int
tlsext_cookie_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB cookie;

	if (!CBB_add_u16_length_prefixed(cbb, &cookie))
		return 0;

	if (!CBB_add_bytes(&cookie, s->s3->hs.tls13.cookie,
	    s->s3->hs.tls13.cookie_len))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_cookie_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS cookie;

	if (!CBS_get_u16_length_prefixed(cbs, &cookie))
		return 0;

	if (CBS_len(&cookie) != s->s3->hs.tls13.cookie_len)
		return 0;

	/*
	 * Check provided cookie value against what server previously
	 * sent - client *MUST* send the same cookie with new CR after
	 * a cookie is sent by the server with an HRR.
	 */
	if (!CBS_mem_equal(&cookie, s->s3->hs.tls13.cookie,
	    s->s3->hs.tls13.cookie_len)) {
		/* XXX special cookie mismatch alert? */
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}

	return 1;
}

static int
tlsext_cookie_server_needs(SSL *s, uint16_t msg_type)
{
	/*
	 * Server needs to set cookie value in tls13 handshake
	 * in order to send one, should only be sent with HRR.
	 */
	return (s->s3->hs.our_max_tls_version >= TLS1_3_VERSION &&
	    s->s3->hs.tls13.cookie_len > 0 && s->s3->hs.tls13.cookie != NULL);
}

static int
tlsext_cookie_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB cookie;

	/* XXX deduplicate with client code */

	if (!CBB_add_u16_length_prefixed(cbb, &cookie))
		return 0;

	if (!CBB_add_bytes(&cookie, s->s3->hs.tls13.cookie,
	    s->s3->hs.tls13.cookie_len))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_cookie_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	CBS cookie;

	/*
	 * XXX This currently assumes we will not get a second
	 * HRR from a server with a cookie to process after accepting
	 * one from the server in the same handshake
	 */
	if (s->s3->hs.tls13.cookie != NULL ||
	    s->s3->hs.tls13.cookie_len != 0) {
		*alert = SSL_AD_ILLEGAL_PARAMETER;
		return 0;
	}

	if (!CBS_get_u16_length_prefixed(cbs, &cookie))
		return 0;

	if (!CBS_stow(&cookie, &s->s3->hs.tls13.cookie,
	    &s->s3->hs.tls13.cookie_len))
		return 0;

	return 1;
}

/*
 * Pre-Shared Key Exchange Modes - RFC 8446, 4.2.9.
 */

static int
tlsext_psk_kex_modes_client_needs(SSL *s, uint16_t msg_type)
{
	return (s->s3->hs.tls13.use_psk_dhe_ke &&
	    s->s3->hs.our_max_tls_version >= TLS1_3_VERSION);
}

static int
tlsext_psk_kex_modes_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	CBB ke_modes;

	if (!CBB_add_u8_length_prefixed(cbb, &ke_modes))
		return 0;

	/* Only indicate support for PSK with DHE key establishment. */
	if (!CBB_add_u8(&ke_modes, TLS13_PSK_DHE_KE))
		return 0;

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

static int
tlsext_psk_kex_modes_server_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	CBS ke_modes;
	uint8_t ke_mode;

	if (!CBS_get_u8_length_prefixed(cbs, &ke_modes))
		return 0;

	while (CBS_len(&ke_modes) > 0) {
		if (!CBS_get_u8(&ke_modes, &ke_mode))
			return 0;

		if (ke_mode == TLS13_PSK_DHE_KE)
			s->s3->hs.tls13.use_psk_dhe_ke = 1;
	}

	return 1;
}

static int
tlsext_psk_kex_modes_server_needs(SSL *s, uint16_t msg_type)
{
	/* Servers MUST NOT send this extension. */
	return 0;
}

static int
tlsext_psk_kex_modes_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return 0;
}

static int
tlsext_psk_kex_modes_client_process(SSL *s, uint16_t msg_type, CBS *cbs,
    int *alert)
{
	return 0;
}

/*
 * Pre-Shared Key Extension - RFC 8446, 4.2.11
 */

static int
tlsext_psk_client_needs(SSL *s, uint16_t msg_type)
{
	return 0;
}

static int
tlsext_psk_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return 0;
}

static int
tlsext_psk_client_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	return CBS_skip(cbs, CBS_len(cbs));
}

static int
tlsext_psk_server_needs(SSL *s, uint16_t msg_type)
{
	return 0;
}

static int
tlsext_psk_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return 0;
}

static int
tlsext_psk_server_process(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	return CBS_skip(cbs, CBS_len(cbs));
}

/*
 * QUIC transport parameters extension - RFC 9001 section 8.2.
 */

static int
tlsext_quic_transport_parameters_client_needs(SSL *s, uint16_t msg_type)
{
	return SSL_is_quic(s) && s->quic_transport_params_len > 0;
}

static int
tlsext_quic_transport_parameters_client_build(SSL *s, uint16_t msg_type,
    CBB *cbb)
{
	if (!CBB_add_bytes(cbb, s->quic_transport_params,
	    s->quic_transport_params_len))
		return 0;

	return 1;
}

static int
tlsext_quic_transport_parameters_client_process(SSL *s, uint16_t msg_type,
    CBS *cbs, int *alert)
{
	if (!SSL_is_quic(s)) {
		*alert = SSL_AD_UNSUPPORTED_EXTENSION;
		return 0;
	}

	if (!CBS_stow(cbs, &s->s3->peer_quic_transport_params,
	    &s->s3->peer_quic_transport_params_len))
		return 0;
	if (!CBS_skip(cbs, s->s3->peer_quic_transport_params_len))
		return 0;

	return 1;
}

static int
tlsext_quic_transport_parameters_server_needs(SSL *s, uint16_t msg_type)
{
	return SSL_is_quic(s) && s->quic_transport_params_len > 0;
}

static int
tlsext_quic_transport_parameters_server_build(SSL *s, uint16_t msg_type,
    CBB *cbb)
{
	if (!CBB_add_bytes(cbb, s->quic_transport_params,
	    s->quic_transport_params_len))
		return 0;

	return 1;
}

static int
tlsext_quic_transport_parameters_server_process(SSL *s, uint16_t msg_type,
    CBS *cbs, int *alert)
{
	if (!SSL_is_quic(s)) {
		*alert = SSL_AD_UNSUPPORTED_EXTENSION;
		return 0;
	}

	if (!CBS_stow(cbs, &s->s3->peer_quic_transport_params,
	    &s->s3->peer_quic_transport_params_len))
		return 0;
	if (!CBS_skip(cbs, s->s3->peer_quic_transport_params_len))
		return 0;

	return 1;
}

struct tls_extension_funcs {
	int (*needs)(SSL *s, uint16_t msg_type);
	int (*build)(SSL *s, uint16_t msg_type, CBB *cbb);
	int (*process)(SSL *s, uint16_t msg_type, CBS *cbs, int *alert);
};

struct tls_extension {
	uint16_t type;
	uint16_t messages;
	struct tls_extension_funcs client;
	struct tls_extension_funcs server;
};

/*
 * TLS extensions (in processing order).
 */
static const struct tls_extension tls_extensions[] = {
	{
		.type = TLSEXT_TYPE_supported_versions,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH |
		    SSL_TLSEXT_MSG_HRR,
		.client = {
			.needs = tlsext_versions_client_needs,
			.build = tlsext_versions_client_build,
			.process = tlsext_versions_client_process,
		},
		.server = {
			.needs = tlsext_versions_server_needs,
			.build = tlsext_versions_server_build,
			.process = tlsext_versions_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_supported_groups,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_EE,
		.client = {
			.needs = tlsext_supportedgroups_client_needs,
			.build = tlsext_supportedgroups_client_build,
			.process = tlsext_supportedgroups_client_process,
		},
		.server = {
			.needs = tlsext_supportedgroups_server_needs,
			.build = tlsext_supportedgroups_server_build,
			.process = tlsext_supportedgroups_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_key_share,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH |
		    SSL_TLSEXT_MSG_HRR,
		.client = {
			.needs = tlsext_keyshare_client_needs,
			.build = tlsext_keyshare_client_build,
			.process = tlsext_keyshare_client_process,
		},
		.server = {
			.needs = tlsext_keyshare_server_needs,
			.build = tlsext_keyshare_server_build,
			.process = tlsext_keyshare_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_server_name,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_EE,
		.client = {
			.needs = tlsext_sni_client_needs,
			.build = tlsext_sni_client_build,
			.process = tlsext_sni_client_process,
		},
		.server = {
			.needs = tlsext_sni_server_needs,
			.build = tlsext_sni_server_build,
			.process = tlsext_sni_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_renegotiate,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH,
		.client = {
			.needs = tlsext_ri_client_needs,
			.build = tlsext_ri_client_build,
			.process = tlsext_ri_client_process,
		},
		.server = {
			.needs = tlsext_ri_server_needs,
			.build = tlsext_ri_server_build,
			.process = tlsext_ri_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_status_request,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_CR |
		    SSL_TLSEXT_MSG_CT,
		.client = {
			.needs = tlsext_ocsp_client_needs,
			.build = tlsext_ocsp_client_build,
			.process = tlsext_ocsp_client_process,
		},
		.server = {
			.needs = tlsext_ocsp_server_needs,
			.build = tlsext_ocsp_server_build,
			.process = tlsext_ocsp_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_ec_point_formats,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH,
		.client = {
			.needs = tlsext_ecpf_client_needs,
			.build = tlsext_ecpf_client_build,
			.process = tlsext_ecpf_client_process,
		},
		.server = {
			.needs = tlsext_ecpf_server_needs,
			.build = tlsext_ecpf_server_build,
			.process = tlsext_ecpf_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_session_ticket,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH,
		.client = {
			.needs = tlsext_sessionticket_client_needs,
			.build = tlsext_sessionticket_client_build,
			.process = tlsext_sessionticket_client_process,
		},
		.server = {
			.needs = tlsext_sessionticket_server_needs,
			.build = tlsext_sessionticket_server_build,
			.process = tlsext_sessionticket_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_signature_algorithms,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_CR,
		.client = {
			.needs = tlsext_sigalgs_client_needs,
			.build = tlsext_sigalgs_client_build,
			.process = tlsext_sigalgs_client_process,
		},
		.server = {
			.needs = tlsext_sigalgs_server_needs,
			.build = tlsext_sigalgs_server_build,
			.process = tlsext_sigalgs_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_alpn,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_EE,
		.client = {
			.needs = tlsext_alpn_client_needs,
			.build = tlsext_alpn_client_build,
			.process = tlsext_alpn_client_process,
		},
		.server = {
			.needs = tlsext_alpn_server_needs,
			.build = tlsext_alpn_server_build,
			.process = tlsext_alpn_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_cookie,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_HRR,
		.client = {
			.needs = tlsext_cookie_client_needs,
			.build = tlsext_cookie_client_build,
			.process = tlsext_cookie_client_process,
		},
		.server = {
			.needs = tlsext_cookie_server_needs,
			.build = tlsext_cookie_server_build,
			.process = tlsext_cookie_server_process,
		},
	},
#ifndef OPENSSL_NO_SRTP
	{
		.type = TLSEXT_TYPE_use_srtp,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH /* XXX */ |
		    SSL_TLSEXT_MSG_EE,
		.client = {
			.needs = tlsext_srtp_client_needs,
			.build = tlsext_srtp_client_build,
			.process = tlsext_srtp_client_process,
		},
		.server = {
			.needs = tlsext_srtp_server_needs,
			.build = tlsext_srtp_server_build,
			.process = tlsext_srtp_server_process,
		},
	},
#endif /* OPENSSL_NO_SRTP */
	{
		.type = TLSEXT_TYPE_quic_transport_parameters,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_EE,
		.client = {
			.needs = tlsext_quic_transport_parameters_client_needs,
			.build = tlsext_quic_transport_parameters_client_build,
			.process = tlsext_quic_transport_parameters_client_process,
		},
		.server = {
			.needs = tlsext_quic_transport_parameters_server_needs,
			.build = tlsext_quic_transport_parameters_server_build,
			.process = tlsext_quic_transport_parameters_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_psk_key_exchange_modes,
		.messages = SSL_TLSEXT_MSG_CH,
		.client = {
			.needs = tlsext_psk_kex_modes_client_needs,
			.build = tlsext_psk_kex_modes_client_build,
			.process = tlsext_psk_kex_modes_client_process,
		},
		.server = {
			.needs = tlsext_psk_kex_modes_server_needs,
			.build = tlsext_psk_kex_modes_server_build,
			.process = tlsext_psk_kex_modes_server_process,
		},
	},
	{
		.type = TLSEXT_TYPE_pre_shared_key,
		.messages = SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH,
		.client = {
			.needs = tlsext_psk_client_needs,
			.build = tlsext_psk_client_build,
			.process = tlsext_psk_client_process,
		},
		.server = {
			.needs = tlsext_psk_server_needs,
			.build = tlsext_psk_server_build,
			.process = tlsext_psk_server_process,
		},
	},
};

#define N_TLS_EXTENSIONS (sizeof(tls_extensions) / sizeof(*tls_extensions))

/* Ensure that extensions fit in a uint32_t bitmask. */
CTASSERT(N_TLS_EXTENSIONS <= (sizeof(uint32_t) * 8));

struct tlsext_data {
	CBS extensions[N_TLS_EXTENSIONS];
};

static struct tlsext_data *
tlsext_data_new(void)
{
	return calloc(1, sizeof(struct tlsext_data));
}

static void
tlsext_data_free(struct tlsext_data *td)
{
	freezero(td, sizeof(*td));
}

uint16_t
tls_extension_type(const struct tls_extension *extension)
{
	return extension->type;
}

const struct tls_extension *
tls_extension_find(uint16_t type, size_t *tls_extensions_idx)
{
	size_t i;

	for (i = 0; i < N_TLS_EXTENSIONS; i++) {
		if (tls_extensions[i].type == type) {
			if (tls_extensions_idx != NULL)
				*tls_extensions_idx = i;
			return &tls_extensions[i];
		}
	}

	return NULL;
}

int
tlsext_extension_seen(SSL *s, uint16_t type)
{
	size_t idx;

	if (tls_extension_find(type, &idx) == NULL)
		return 0;
	return ((s->s3->hs.extensions_seen & (1 << idx)) != 0);
}

int
tlsext_extension_processed(SSL *s, uint16_t type)
{
	size_t idx;

	if (tls_extension_find(type, &idx) == NULL)
		return 0;
	return ((s->s3->hs.extensions_processed & (1 << idx)) != 0);
}

const struct tls_extension_funcs *
tlsext_funcs(const struct tls_extension *tlsext, int is_server)
{
	if (is_server)
		return &tlsext->server;

	return &tlsext->client;
}

int
tlsext_randomize_build_order(SSL *s)
{
	const struct tls_extension *psk_ext;
	size_t idx, new_idx;

	free(s->tlsext_build_order);
	s->tlsext_build_order_len = 0;

	if ((s->tlsext_build_order = calloc(N_TLS_EXTENSIONS,
	    sizeof(*s->tlsext_build_order))) == NULL)
		return 0;
	s->tlsext_build_order_len = N_TLS_EXTENSIONS;

	/* RFC 8446, section 4.2 - PSK MUST be the last extension in the CH. */
	if ((psk_ext = tls_extension_find(TLSEXT_TYPE_pre_shared_key,
	    NULL)) == NULL)
		return 0;
	s->tlsext_build_order[N_TLS_EXTENSIONS - 1] = psk_ext;

	/* Fisher-Yates shuffle with PSK fixed. */
	for (idx = 0; idx < N_TLS_EXTENSIONS - 1; idx++) {
		new_idx = arc4random_uniform(idx + 1);
		s->tlsext_build_order[idx] = s->tlsext_build_order[new_idx];
		s->tlsext_build_order[new_idx] = &tls_extensions[idx];
	}

	return 1;
}

int
tlsext_linearize_build_order(SSL *s)
{
	size_t idx;

	free(s->tlsext_build_order);
	s->tlsext_build_order_len = 0;

	if ((s->tlsext_build_order = calloc(N_TLS_EXTENSIONS,
	    sizeof(*s->tlsext_build_order))) == NULL)
		return 0;
	s->tlsext_build_order_len = N_TLS_EXTENSIONS;

	for (idx = 0; idx < N_TLS_EXTENSIONS; idx++)
		s->tlsext_build_order[idx] = &tls_extensions[idx];

	return 1;
}

static int
tlsext_build(SSL *s, int is_server, uint16_t msg_type, CBB *cbb)
{
	const struct tls_extension_funcs *ext;
	const struct tls_extension *tlsext;
	CBB extensions, extension_data;
	int extensions_present = 0;
	uint16_t tls_version;
	size_t i;

	tls_version = ssl_effective_tls_version(s);

	if (!CBB_add_u16_length_prefixed(cbb, &extensions))
		return 0;

	for (i = 0; i < N_TLS_EXTENSIONS; i++) {
		tlsext = s->tlsext_build_order[i];
		ext = tlsext_funcs(tlsext, is_server);

		/* RFC 8446 Section 4.2 */
		if (tls_version >= TLS1_3_VERSION &&
		    !(tlsext->messages & msg_type))
			continue;

		if (!ext->needs(s, msg_type))
			continue;

		if (!CBB_add_u16(&extensions, tlsext->type))
			return 0;
		if (!CBB_add_u16_length_prefixed(&extensions, &extension_data))
			return 0;

		if (!ext->build(s, msg_type, &extension_data))
			return 0;

		extensions_present = 1;
	}

	if (!extensions_present &&
	    (msg_type & (SSL_TLSEXT_MSG_CH | SSL_TLSEXT_MSG_SH)) != 0)
		CBB_discard_child(cbb);

	if (!CBB_flush(cbb))
		return 0;

	return 1;
}

int
tlsext_clienthello_hash_extension(SSL *s, uint16_t type, CBS *cbs)
{
	/*
	 * RFC 8446 4.1.2. For subsequent CH, early data will be removed,
	 * cookie may be added, padding may be removed.
	 */
	struct tls13_ctx *ctx = s->tls13;

	if (type == TLSEXT_TYPE_early_data || type == TLSEXT_TYPE_cookie ||
	    type == TLSEXT_TYPE_padding)
		return 1;
	if (!tls13_clienthello_hash_update_bytes(ctx, (void *)&type,
	    sizeof(type)))
		return 0;
	/*
	 * key_share data may be changed, and pre_shared_key data may
	 * be changed.
	 */
	if (type == TLSEXT_TYPE_pre_shared_key || type == TLSEXT_TYPE_key_share)
		return 1;
	if (!tls13_clienthello_hash_update(ctx, cbs))
		return 0;

	return 1;
}

static int
tlsext_parse(SSL *s, struct tlsext_data *td, int is_server, uint16_t msg_type,
    CBS *cbs, int *alert)
{
	const struct tls_extension *tlsext;
	CBS extensions, extension_data;
	uint16_t type;
	size_t idx;
	uint16_t tls_version;
	int alert_desc;

	tls_version = ssl_effective_tls_version(s);

	s->s3->hs.extensions_seen = 0;

	/* An empty extensions block is valid. */
	if (CBS_len(cbs) == 0)
		return 1;

	alert_desc = SSL_AD_DECODE_ERROR;

	if (!CBS_get_u16_length_prefixed(cbs, &extensions))
		goto err;

	while (CBS_len(&extensions) > 0) {
		if (!CBS_get_u16(&extensions, &type))
			goto err;
		if (!CBS_get_u16_length_prefixed(&extensions, &extension_data))
			goto err;

		if (s->tlsext_debug_cb != NULL)
			s->tlsext_debug_cb(s, !is_server, type,
			    (unsigned char *)CBS_data(&extension_data),
			    CBS_len(&extension_data),
			    s->tlsext_debug_arg);

		/* Unknown extensions are ignored. */
		if ((tlsext = tls_extension_find(type, &idx)) == NULL)
			continue;

		if (tls_version >= TLS1_3_VERSION && is_server &&
		    msg_type == SSL_TLSEXT_MSG_CH) {
			if (!tlsext_clienthello_hash_extension(s, type,
			    &extension_data))
				goto err;
		}

		/* RFC 8446 Section 4.2 */
		if (tls_version >= TLS1_3_VERSION &&
		    !(tlsext->messages & msg_type)) {
			alert_desc = SSL_AD_ILLEGAL_PARAMETER;
			goto err;
		}

		/* Check for duplicate known extensions. */
		if ((s->s3->hs.extensions_seen & (1 << idx)) != 0)
			goto err;
		s->s3->hs.extensions_seen |= (1 << idx);

		CBS_dup(&extension_data, &td->extensions[idx]);
	}

	return 1;

 err:
	*alert = alert_desc;

	return 0;
}

static int
tlsext_process(SSL *s, struct tlsext_data *td, int is_server, uint16_t msg_type,
    int *alert)
{
	const struct tls_extension_funcs *ext;
	const struct tls_extension *tlsext;
	int alert_desc;
	size_t idx;

	alert_desc = SSL_AD_DECODE_ERROR;

	s->s3->hs.extensions_processed = 0;

	/* Run processing for present TLS extensions, in a defined order. */
	for (idx = 0; idx < N_TLS_EXTENSIONS; idx++) {
		tlsext = &tls_extensions[idx];
		if ((s->s3->hs.extensions_seen & (1 << idx)) == 0)
			continue;
		ext = tlsext_funcs(tlsext, is_server);
		if (ext->process == NULL)
			continue;
		if (!ext->process(s, msg_type, &td->extensions[idx], &alert_desc))
			goto err;

		if (CBS_len(&td->extensions[idx]) != 0)
			goto err;

		s->s3->hs.extensions_processed |= (1 << idx);
	}

	return 1;

 err:
	*alert = alert_desc;

	return 0;
}

static void
tlsext_server_reset_state(SSL *s)
{
	s->tlsext_status_type = -1;
	s->s3->renegotiate_seen = 0;
	free(s->s3->alpn_selected);
	s->s3->alpn_selected = NULL;
	s->s3->alpn_selected_len = 0;
	s->srtp_profile = NULL;
}

int
tlsext_server_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return tlsext_build(s, 1, msg_type, cbb);
}

int
tlsext_server_parse(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	struct tlsext_data *td;
	int ret = 0;

	if ((td = tlsext_data_new()) == NULL)
		goto err;

	/* XXX - this should be done by the caller... */
	if (msg_type == SSL_TLSEXT_MSG_CH)
		tlsext_server_reset_state(s);

	if (!tlsext_parse(s, td, 1, msg_type, cbs, alert))
		goto err;
	if (!tlsext_process(s, td, 1, msg_type, alert))
		goto err;

	ret = 1;

 err:
	tlsext_data_free(td);

	return ret;
}

static void
tlsext_client_reset_state(SSL *s)
{
	s->s3->renegotiate_seen = 0;
	free(s->s3->alpn_selected);
	s->s3->alpn_selected = NULL;
	s->s3->alpn_selected_len = 0;
}

int
tlsext_client_build(SSL *s, uint16_t msg_type, CBB *cbb)
{
	return tlsext_build(s, 0, msg_type, cbb);
}

int
tlsext_client_parse(SSL *s, uint16_t msg_type, CBS *cbs, int *alert)
{
	struct tlsext_data *td;
	int ret = 0;

	if ((td = tlsext_data_new()) == NULL)
		goto err;

	/* XXX - this should be done by the caller... */
	if (msg_type == SSL_TLSEXT_MSG_SH)
		tlsext_client_reset_state(s);

	if (!tlsext_parse(s, td, 0, msg_type, cbs, alert))
		goto err;
	if (!tlsext_process(s, td, 0, msg_type, alert))
		goto err;

	ret = 1;

 err:
	tlsext_data_free(td);

	return ret;
}
