/*
 * xfrd-tcp.c - XFR (transfer) Daemon TCP system source file. Manages tcp conn.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "nsd.h"
#include "xfrd-tcp.h"
#include "buffer.h"
#include "packet.h"
#include "dname.h"
#include "options.h"
#include "namedb.h"
#include "xfrd.h"
#include "xfrd-disk.h"
#include "util.h"
#ifdef HAVE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#endif

#ifdef HAVE_SSL
void log_crypto_err(const char* str); /* in server.c */

/* Extract certificate information for logging */
void
get_cert_info(SSL* ssl, region_type* region, char** cert_serial,
              char** key_id, char** cert_algorithm, char** tls_version)
{
    X509* cert = NULL;
    ASN1_INTEGER* serial = NULL;
    EVP_PKEY* pkey = NULL;
    unsigned char key_fingerprint[EVP_MAX_MD_SIZE];
    unsigned int key_fingerprint_len = 0;
    const EVP_MD* md = EVP_sha256();
    const char* pkey_name = NULL;
    const char* version_name = NULL;
    int i;
    char temp_buffer[1024]; /* Temporary buffer for serial number */

    *cert_serial = NULL;
    *key_id = NULL;
    *cert_algorithm = NULL;
    *tls_version = NULL;

#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
    cert = SSL_get1_peer_certificate(ssl);
#else
    cert = SSL_get_peer_certificate(ssl);
#endif

    if (!cert) {
        return;
    }

    /* Get certificate serial number */
    serial = X509_get_serialNumber(cert);
    if (serial) {
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, NULL);
        if (bn) {
            char* hex_serial = BN_bn2hex(bn);
            if (hex_serial) {
                snprintf(temp_buffer, sizeof(temp_buffer), "%s", hex_serial);
                *cert_serial = region_strdup(region, temp_buffer);
                OPENSSL_free(hex_serial);
            }
            BN_free(bn);
        }
    }

    /* Get public key identifier (SHA-256 fingerprint) */
    if (X509_pubkey_digest(cert, md, key_fingerprint, &key_fingerprint_len) == 1 && key_fingerprint_len >= 8) {
        size_t id_len = 8; /* Use first 8 bytes as key identifier */
        char key_id_buffer[17]; /* 8 bytes * 2 hex chars + null terminator */
        for (i = 0; i < (int)id_len; i++) {
            snprintf(key_id_buffer + (i * 2), sizeof(key_id_buffer) - (i * 2), "%02x", key_fingerprint[i]);
        }
        *key_id = region_strdup(region, key_id_buffer);
    }

    /* Get certificate algorithm using OpenSSL's native functions */
    pkey = X509_get_pubkey(cert);
    if (pkey) {
#ifdef HAVE_EVP_PKEY_GET0_TYPE_NAME
        pkey_name = EVP_PKEY_get0_type_name(pkey);
#else
        pkey_name = OBJ_nid2sn(EVP_PKEY_type(EVP_PKEY_id(pkey)));
#endif
        if (pkey_name) {
            *cert_algorithm = region_strdup(region, pkey_name);
        } else {
            int pkey_type = EVP_PKEY_id(pkey);
            char algo_buffer[32];
            snprintf(algo_buffer, sizeof(algo_buffer), "Unknown(%d)", pkey_type);
            *cert_algorithm = region_strdup(region, algo_buffer);
        }
        EVP_PKEY_free(pkey);
    }

    /* Get TLS version using OpenSSL's native function */
    version_name = SSL_get_version(ssl);
    if (version_name) {
        *tls_version = region_strdup(region, version_name);
    } else {
        int version = SSL_version(ssl);
        char version_buffer[16];
        snprintf(version_buffer, sizeof(version_buffer), "Unknown(%d)", version);
        *tls_version = region_strdup(region, version_buffer);
    }

    X509_free(cert);
}

static SSL_CTX*
create_ssl_context()
{
	SSL_CTX *ctx;
	unsigned char protos[] = { 3, 'd', 'o', 't' };
	ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		log_msg(LOG_ERR, "xfrd tls: Unable to create SSL ctxt");
	}
	else if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
		SSL_CTX_free(ctx);
		log_msg(LOG_ERR, "xfrd tls: Unable to set default SSL verify paths");
		return NULL;
	}
	/* Only trust 1.3 as per the specification */
	else if (!SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION)) {
		SSL_CTX_free(ctx);
		log_msg(LOG_ERR, "xfrd tls: Unable to set minimum TLS version 1.3");
		return NULL;
	}

	if (SSL_CTX_set_alpn_protos(ctx, protos, sizeof(protos)) != 0) {
		SSL_CTX_free(ctx);
		log_msg(LOG_ERR, "xfrd tls: Unable to set ALPN protocols");
		return NULL;
	}
	return ctx;
}

static int
tls_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
	int err = X509_STORE_CTX_get_error(ctx);
	int depth = X509_STORE_CTX_get_error_depth(ctx);

	// report the specific cert error here - will need custom verify code if 
	// SPKI pins are supported
	if (!preverify_ok)
		log_msg(LOG_ERR, "xfrd tls: TLS verify failed - (%d) depth: %d error: %s",
				err,
				depth,
				X509_verify_cert_error_string(err));
	return preverify_ok;
}

static int
setup_ssl(struct xfrd_tcp_pipeline* tp, struct xfrd_tcp_set* tcp_set, 
		  const char* auth_domain_name)
{
	if (!tcp_set->ssl_ctx) {
		log_msg(LOG_ERR, "xfrd tls: No TLS CTX, cannot set up XFR-over-TLS");
		return 0;
	}
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: setting up TLS for tls_auth domain name %s", 
						 auth_domain_name));
	tp->ssl = SSL_new((SSL_CTX*)tcp_set->ssl_ctx);
	if(!tp->ssl) {
		log_msg(LOG_ERR, "xfrd tls: Unable to create TLS object");
		return 0;
	}
	SSL_set_connect_state(tp->ssl);
	(void)SSL_set_mode(tp->ssl, SSL_MODE_AUTO_RETRY);
	if(!SSL_set_fd(tp->ssl, tp->tcp_w->fd)) {
		log_msg(LOG_ERR, "xfrd tls: Unable to set TLS fd");
		SSL_free(tp->ssl);
		tp->ssl = NULL;
		return 0;
	}

	SSL_set_verify(tp->ssl, SSL_VERIFY_PEER, tls_verify_callback);
	if(!SSL_set1_host(tp->ssl, auth_domain_name)) {
		log_msg(LOG_ERR, "xfrd tls: TLS setting of hostname %s failed",
		auth_domain_name);
		SSL_free(tp->ssl);
		tp->ssl = NULL;
		return 0;
	}
	return 1;
}

static int
ssl_handshake(struct xfrd_tcp_pipeline* tp)
{
	int ret;

	ERR_clear_error();
	ret = SSL_do_handshake(tp->ssl);
	if(ret == 1) {
		DEBUG(DEBUG_XFRD, 1, (LOG_INFO, "xfrd: TLS handshake successful"));
		tp->handshake_done = 1;
		return 1;
	}
	tp->handshake_want = SSL_get_error(tp->ssl, ret);
	if(tp->handshake_want == SSL_ERROR_WANT_READ
	|| tp->handshake_want == SSL_ERROR_WANT_WRITE)
		return 1;

	return 0;
}

int password_cb(char *buf, int size, int ATTR_UNUSED(rwflag), void *u)
{
	strlcpy(buf, (char*)u, size);
	return strlen(buf);
}

#endif

/* sort tcppipe, first on IP address, for an IPaddresss, sort on num_unused */
static int
xfrd_pipe_cmp(const void* a, const void* b)
{
	const struct xfrd_tcp_pipeline* x = (struct xfrd_tcp_pipeline*)a;
	const struct xfrd_tcp_pipeline* y = (struct xfrd_tcp_pipeline*)b;
	int r;
	if(x == y)
		return 0;
	if(y->key.ip_len != x->key.ip_len)
		/* subtraction works because nonnegative and small numbers */
		return (int)y->key.ip_len - (int)x->key.ip_len;
	r = memcmp(&x->key.ip, &y->key.ip, x->key.ip_len);
	if(r != 0)
		return r;
	/* sort that num_unused is sorted ascending, */
	if(x->key.num_unused != y->key.num_unused) {
		return (x->key.num_unused < y->key.num_unused) ? -1 : 1;
	}
	/* different pipelines are different still, even with same numunused*/
	return (uintptr_t)x < (uintptr_t)y ? -1 : 1;
}

struct xfrd_tcp_set* xfrd_tcp_set_create(struct region* region, const char *tls_cert_bundle, int tcp_max, int tcp_pipeline)
{
	int i;
	struct xfrd_tcp_set* tcp_set = region_alloc(region,
		sizeof(struct xfrd_tcp_set));
	memset(tcp_set, 0, sizeof(struct xfrd_tcp_set));
	tcp_set->tcp_state = NULL;
	tcp_set->tcp_max = tcp_max;
	tcp_set->tcp_pipeline = tcp_pipeline;
	tcp_set->tcp_count = 0;
	tcp_set->tcp_waiting_first = 0;
	tcp_set->tcp_waiting_last = 0;
#ifdef HAVE_TLS_1_3
	/* Set up SSL context */
	tcp_set->ssl_ctx = create_ssl_context();
	if (tcp_set->ssl_ctx == NULL)
		log_msg(LOG_ERR, "xfrd: XFR-over-TLS not available");

	else if (tls_cert_bundle && tls_cert_bundle[0] && SSL_CTX_load_verify_locations(
				tcp_set->ssl_ctx, tls_cert_bundle, NULL) != 1) {
		log_msg(LOG_ERR, "xfrd tls: Unable to set the certificate bundle file %s",
				tls_cert_bundle);
	}
#else
	(void)tls_cert_bundle;
	log_msg(LOG_INFO, "xfrd: No TLS 1.3 support - XFR-over-TLS not available");
#endif
	tcp_set->tcp_state = region_alloc(region,
		sizeof(*tcp_set->tcp_state)*tcp_set->tcp_max);
	for(i=0; i<tcp_set->tcp_max; i++)
		tcp_set->tcp_state[i] = xfrd_tcp_pipeline_create(region,
			tcp_pipeline);
	tcp_set->pipetree = rbtree_create(region, &xfrd_pipe_cmp);
	return tcp_set;
}

static int pipeline_id_compare(const void* x, const void* y)
{
	struct xfrd_tcp_pipeline_id* a = (struct xfrd_tcp_pipeline_id*)x;
	struct xfrd_tcp_pipeline_id* b = (struct xfrd_tcp_pipeline_id*)y;
	if(a->id < b->id)
		return -1;
	if(a->id > b->id)
		return 1;
	return 0;
}

void pick_id_values(uint16_t* array, int num, int max)
{
	uint8_t inserted[65536];
	int j, done;
	if(num == 65536) {
		/* all of them, loop and insert */
		int i;
		for(i=0; i<num; i++)
			array[i] = (uint16_t)i;
		return;
	}
	assert(max <= 65536);
	/* This uses the Robert Floyd sampling algorithm */
	/* keep track if values are already inserted, using the bitmap
	 * in insert array */
	memset(inserted, 0, sizeof(inserted[0])*max);
	done=0;
	for(j = max-num; j<max; j++) {
		/* random generate creates from 0..arg-1 */
		int t;
		if(j+1 <= 1)
			t = 0;
		else	t = random_generate(j+1);
		if(!inserted[t]) {
			array[done++]=t;
			inserted[t] = 1;
		} else {
			array[done++]=j;
			inserted[j] = 1;
		}
	}
}

static void
clear_pipeline_entry(struct xfrd_tcp_pipeline* tp, rbnode_type* node)
{
	struct xfrd_tcp_pipeline_id *n;
	if(node == NULL || node == RBTREE_NULL)
		return;
	clear_pipeline_entry(tp, node->left);
	node->left = NULL;
	clear_pipeline_entry(tp, node->right);
	node->right = NULL;
	/* move the node into the free list */
	n = (struct xfrd_tcp_pipeline_id*)node;
	n->next_free = tp->pipe_id_free_list;
	tp->pipe_id_free_list = n;
}

static void
xfrd_tcp_pipeline_cleanup(struct xfrd_tcp_pipeline* tp)
{
	/* move entries into free list */
	clear_pipeline_entry(tp, tp->zone_per_id->root);
	/* clear the tree */
	tp->zone_per_id->count = 0;
	tp->zone_per_id->root = RBTREE_NULL;
}

static void
xfrd_tcp_pipeline_init(struct xfrd_tcp_pipeline* tp)
{
	tp->key.node.key = tp;
	tp->key.num_unused = tp->pipe_num;
	tp->key.num_skip = 0;
	tp->tcp_send_first = NULL;
	tp->tcp_send_last = NULL;
	xfrd_tcp_pipeline_cleanup(tp);
	pick_id_values(tp->unused, tp->pipe_num, 65536);
}

struct xfrd_tcp_pipeline*
xfrd_tcp_pipeline_create(region_type* region, int tcp_pipeline)
{
	int i;
	struct xfrd_tcp_pipeline* tp = (struct xfrd_tcp_pipeline*)
		region_alloc_zero(region, sizeof(*tp));
	if(tcp_pipeline < 0)
		tcp_pipeline = 0;
	if(tcp_pipeline > 65536)
		tcp_pipeline = 65536; /* max 16 bit ID numbers */
	tp->pipe_num = tcp_pipeline;
	tp->key.num_unused = tp->pipe_num;
	tp->zone_per_id = rbtree_create(region, &pipeline_id_compare);
	tp->pipe_id_free_list = NULL;
	for(i=0; i<tp->pipe_num; i++) {
		struct xfrd_tcp_pipeline_id* n = (struct xfrd_tcp_pipeline_id*)
			region_alloc_zero(region, sizeof(*n));
		n->next_free = tp->pipe_id_free_list;
		tp->pipe_id_free_list = n;
	}
	tp->unused = (uint16_t*)region_alloc_zero(region,
		sizeof(tp->unused[0])*tp->pipe_num);
	tp->tcp_r = xfrd_tcp_create(region, QIOBUFSZ);
	tp->tcp_w = xfrd_tcp_create(region, QIOBUFSZ);
	xfrd_tcp_pipeline_init(tp);
	return tp;
}

static struct xfrd_zone*
xfrd_tcp_pipeline_lookup_id(struct xfrd_tcp_pipeline* tp, uint16_t id)
{
	struct xfrd_tcp_pipeline_id key;
	rbnode_type* n;
	memset(&key, 0, sizeof(key));
	key.node.key = &key;
	key.id = id;
	n = rbtree_search(tp->zone_per_id, &key);
	if(n && n != RBTREE_NULL) {
		return ((struct xfrd_tcp_pipeline_id*)n)->zone;
	}
	return NULL;
}

static void
xfrd_tcp_pipeline_insert_id(struct xfrd_tcp_pipeline* tp, uint16_t id,
	struct xfrd_zone* zone)
{
	struct xfrd_tcp_pipeline_id* n;
	/* because there are tp->pipe_num preallocated entries, and we have
	 * only tp->pipe_num id values, the list cannot be empty now. */
	assert(tp->pipe_id_free_list != NULL);
	/* pick up next free xfrd_tcp_pipeline_id node */
	n = tp->pipe_id_free_list;
	tp->pipe_id_free_list = n->next_free;
	n->next_free = NULL;
	memset(&n->node, 0, sizeof(n->node));
	n->node.key = n;
	n->id = id;
	n->zone = zone;
	rbtree_insert(tp->zone_per_id, &n->node);
}

static void
xfrd_tcp_pipeline_remove_id(struct xfrd_tcp_pipeline* tp, uint16_t id)
{
	struct xfrd_tcp_pipeline_id key;
	rbnode_type* node;
	memset(&key, 0, sizeof(key));
	key.node.key = &key;
	key.id = id;
	node = rbtree_delete(tp->zone_per_id, &key);
	if(node && node != RBTREE_NULL) {
		struct xfrd_tcp_pipeline_id* n =
			(struct xfrd_tcp_pipeline_id*)node;
		n->next_free = tp->pipe_id_free_list;
		tp->pipe_id_free_list = n;
	}
}

static void
xfrd_tcp_pipeline_skip_id(struct xfrd_tcp_pipeline* tp, uint16_t id)
{
	struct xfrd_tcp_pipeline_id key;
	rbnode_type* n;
	memset(&key, 0, sizeof(key));
	key.node.key = &key;
	key.id = id;
	n = rbtree_search(tp->zone_per_id, &key);
	if(n && n != RBTREE_NULL) {
		struct xfrd_tcp_pipeline_id* zid = (struct xfrd_tcp_pipeline_id*)n;
		zid->zone = TCP_NULL_SKIP;
	}
}

void
xfrd_setup_packet(buffer_type* packet,
	uint16_t type, uint16_t klass, const dname_type* dname, uint16_t qid,
	int* apex_compress)
{
	/* Set up the header */
	buffer_clear(packet);
	ID_SET(packet, qid);
	FLAGS_SET(packet, 0);
	OPCODE_SET(packet, OPCODE_QUERY);
	QDCOUNT_SET(packet, 1);
	ANCOUNT_SET(packet, 0);
	NSCOUNT_SET(packet, 0);
	ARCOUNT_SET(packet, 0);
	buffer_skip(packet, QHEADERSZ);

	/* The question record. */
	if(apex_compress)
		*apex_compress = buffer_position(packet);
	buffer_write(packet, dname_name(dname), dname->name_size);
	buffer_write_u16(packet, type);
	buffer_write_u16(packet, klass);
}

static socklen_t
#ifdef INET6
xfrd_acl_sockaddr(acl_options_type* acl, unsigned int port,
	struct sockaddr_storage *sck)
#else
xfrd_acl_sockaddr(acl_options_type* acl, unsigned int port,
	struct sockaddr_in *sck, const char* fromto)
#endif /* INET6 */
{
	/* setup address structure */
#ifdef INET6
	memset(sck, 0, sizeof(struct sockaddr_storage));
#else
	memset(sck, 0, sizeof(struct sockaddr_in));
#endif
	if(acl->is_ipv6) {
#ifdef INET6
		struct sockaddr_in6* sa = (struct sockaddr_in6*)sck;
		sa->sin6_family = AF_INET6;
		sa->sin6_port = htons(port);
		sa->sin6_addr = acl->addr.addr6;
		return sizeof(struct sockaddr_in6);
#else
		log_msg(LOG_ERR, "xfrd: IPv6 connection %s %s attempted but no \
INET6.", fromto, acl->ip_address_spec);
		return 0;
#endif
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*)sck;
		sa->sin_family = AF_INET;
		sa->sin_port = htons(port);
		sa->sin_addr = acl->addr.addr;
		return sizeof(struct sockaddr_in);
	}
}

socklen_t
#ifdef INET6
xfrd_acl_sockaddr_to(acl_options_type* acl, struct sockaddr_storage *to)
#else
xfrd_acl_sockaddr_to(acl_options_type* acl, struct sockaddr_in *to)
#endif /* INET6 */
{
#ifdef HAVE_TLS_1_3
	unsigned int port = acl->port?acl->port:(acl->tls_auth_options?
						(unsigned)atoi(TLS_PORT):(unsigned)atoi(TCP_PORT));
#else
	unsigned int port = acl->port?acl->port:(unsigned)atoi(TCP_PORT);
#endif
#ifdef INET6
	return xfrd_acl_sockaddr(acl, port, to);
#else
	return xfrd_acl_sockaddr(acl, port, to, "to");
#endif /* INET6 */
}

socklen_t
#ifdef INET6
xfrd_acl_sockaddr_frm(acl_options_type* acl, struct sockaddr_storage *frm)
#else
xfrd_acl_sockaddr_frm(acl_options_type* acl, struct sockaddr_in *frm)
#endif /* INET6 */
{
	unsigned int port = acl->port?acl->port:0;
#ifdef INET6
	return xfrd_acl_sockaddr(acl, port, frm);
#else
	return xfrd_acl_sockaddr(acl, port, frm, "from");
#endif /* INET6 */
}

void
xfrd_write_soa_buffer(struct buffer* packet,
	const dname_type* apex, struct xfrd_soa* soa, int apex_compress)
{
	size_t rdlength_pos;
	uint16_t rdlength;
	if(apex_compress > 0 && apex_compress < (int)buffer_limit(packet) &&
		apex->name_size > 1)
		buffer_write_u16(packet, 0xc000 | apex_compress);
	else	buffer_write(packet, dname_name(apex), apex->name_size);

	/* already in network order */
	buffer_write(packet, &soa->type, sizeof(soa->type));
	buffer_write(packet, &soa->klass, sizeof(soa->klass));
	buffer_write(packet, &soa->ttl, sizeof(soa->ttl));
	rdlength_pos = buffer_position(packet);
	buffer_skip(packet, sizeof(rdlength));

	/* compress dnames to apex if possible */
	if(apex_compress > 0 && apex_compress < (int)buffer_limit(packet) &&
		apex->name_size > 1 && is_dname_subdomain_of_case(
		soa->prim_ns+1, soa->prim_ns[0], dname_name(apex),
		apex->name_size)) {
		if(soa->prim_ns[0] > apex->name_size)
			buffer_write(packet, soa->prim_ns+1, soa->prim_ns[0]-
				apex->name_size);
		buffer_write_u16(packet, 0xc000 | apex_compress);
	} else {
		buffer_write(packet, soa->prim_ns+1, soa->prim_ns[0]);
	}
	if(apex_compress > 0 && apex_compress < (int)buffer_limit(packet) &&
		apex->name_size > 1 && is_dname_subdomain_of_case(soa->email+1,
		soa->email[0], dname_name(apex), apex->name_size)) {
		if(soa->email[0] > apex->name_size)
			buffer_write(packet, soa->email+1, soa->email[0]-
				apex->name_size);
		buffer_write_u16(packet, 0xc000 | apex_compress);
	} else {
		buffer_write(packet, soa->email+1, soa->email[0]);
	}

	buffer_write(packet, &soa->serial, sizeof(uint32_t));
	buffer_write(packet, &soa->refresh, sizeof(uint32_t));
	buffer_write(packet, &soa->retry, sizeof(uint32_t));
	buffer_write(packet, &soa->expire, sizeof(uint32_t));
	buffer_write(packet, &soa->minimum, sizeof(uint32_t));

	/* write length of RR */
	rdlength = buffer_position(packet) - rdlength_pos - sizeof(rdlength);
	buffer_write_u16_at(packet, rdlength_pos, rdlength);
}

struct xfrd_tcp*
xfrd_tcp_create(region_type* region, size_t bufsize)
{
	struct xfrd_tcp* tcp_state = (struct xfrd_tcp*)region_alloc(
		region, sizeof(struct xfrd_tcp));
	memset(tcp_state, 0, sizeof(struct xfrd_tcp));
	tcp_state->packet = buffer_create(region, bufsize);
	tcp_state->fd = -1;

	return tcp_state;
}

static struct xfrd_tcp_pipeline*
pipeline_find(struct xfrd_tcp_set* set, xfrd_zone_type* zone)
{
	rbnode_type* sme = NULL;
	struct xfrd_tcp_pipeline* r;
	/* smaller buf than a full pipeline with 64kb ID array, only need
	 * the front part with the key info, this front part contains the
	 * members that the compare function uses. */
	struct xfrd_tcp_pipeline_key k, *key=&k;
	key->node.key = key;
	key->ip_len = xfrd_acl_sockaddr_to(zone->master, &key->ip);
	key->num_unused = set->tcp_pipeline;
	/* lookup existing tcp transfer to the master with highest unused */
	if(rbtree_find_less_equal(set->pipetree, key, &sme)) {
		/* exact match, strange, fully unused tcp cannot be open */
		assert(0);
	} 
	if(!sme)
		return NULL;
	r = (struct xfrd_tcp_pipeline*)sme->key;
	/* <= key pointed at, is the master correct ? */
	if(r->key.ip_len != key->ip_len)
		return NULL;
	if(memcmp(&r->key.ip, &key->ip, key->ip_len) != 0)
		return NULL;
	/* correct master, is there a slot free for this transfer? */
	if(r->key.num_unused == 0)
		return NULL;
	return r;
}

/* remove zone from tcp waiting list */
static void
tcp_zone_waiting_list_popfirst(struct xfrd_tcp_set* set, xfrd_zone_type* zone)
{
	assert(zone->tcp_waiting);
	set->tcp_waiting_first = zone->tcp_waiting_next;
	if(zone->tcp_waiting_next)
		zone->tcp_waiting_next->tcp_waiting_prev = NULL;
	else	set->tcp_waiting_last = 0;
	zone->tcp_waiting_next = 0;
	zone->tcp_waiting = 0;
}

/* remove zone from tcp pipe write-wait list */
static void
tcp_pipe_sendlist_remove(struct xfrd_tcp_pipeline* tp, xfrd_zone_type* zone)
{
	if(zone->in_tcp_send) {
		if(zone->tcp_send_prev)
			zone->tcp_send_prev->tcp_send_next=zone->tcp_send_next;
		else	tp->tcp_send_first=zone->tcp_send_next;
		if(zone->tcp_send_next)
			zone->tcp_send_next->tcp_send_prev=zone->tcp_send_prev;
		else	tp->tcp_send_last=zone->tcp_send_prev;
		zone->in_tcp_send = 0;
	}
}

/* remove first from write-wait list */
static void
tcp_pipe_sendlist_popfirst(struct xfrd_tcp_pipeline* tp, xfrd_zone_type* zone)
{
	tp->tcp_send_first = zone->tcp_send_next;
	if(tp->tcp_send_first)
		tp->tcp_send_first->tcp_send_prev = NULL;
	else	tp->tcp_send_last = NULL;
	zone->in_tcp_send = 0;
}

/* remove zone from tcp pipe ID map */
static void
tcp_pipe_id_remove(struct xfrd_tcp_pipeline* tp, xfrd_zone_type* zone,
	int alsotree)
{
	assert(tp->key.num_unused < tp->pipe_num && tp->key.num_unused >= 0);
	if(alsotree)
		xfrd_tcp_pipeline_remove_id(tp, zone->query_id);
	tp->unused[tp->key.num_unused] = zone->query_id;
	/* must remove and re-add for sort order in tree */
	(void)rbtree_delete(xfrd->tcp_set->pipetree, &tp->key.node);
	tp->key.num_unused++;
	(void)rbtree_insert(xfrd->tcp_set->pipetree, &tp->key.node);
}

/* stop the tcp pipe (and all its zones need to retry) */
static void
xfrd_tcp_pipe_stop(struct xfrd_tcp_pipeline* tp)
{
	struct xfrd_tcp_pipeline_id* zid;
	int conn = -1;
	assert(tp->key.num_unused < tp->pipe_num); /* at least one 'in-use' */
	assert(tp->pipe_num - tp->key.num_unused > tp->key.num_skip); /* at least one 'nonskip' */
	/* need to retry for all the zones connected to it */
	/* these could use different lists and go to a different nextmaster*/
	RBTREE_FOR(zid, struct xfrd_tcp_pipeline_id*, tp->zone_per_id) {
		xfrd_zone_type* zone = zid->zone;
		if(zone && zone != TCP_NULL_SKIP) {
			assert(zone->query_id == zid->id);
			conn = zone->tcp_conn;
			zone->tcp_conn = -1;
			zone->tcp_waiting = 0;
			tcp_pipe_sendlist_remove(tp, zone);
			tcp_pipe_id_remove(tp, zone, 0);
			xfrd_set_refresh_now(zone);
		}
	}
	xfrd_tcp_pipeline_cleanup(tp);
	assert(conn != -1);
	/* now release the entire tcp pipe */
	xfrd_tcp_pipe_release(xfrd->tcp_set, tp, conn);
}

static void
tcp_pipe_reset_timeout(struct xfrd_tcp_pipeline* tp)
{
	int fd = tp->handler.ev_fd;
	struct timeval tv;
	tv.tv_sec = xfrd->tcp_set->tcp_timeout;
	tv.tv_usec = 0;
	if(tp->handler_added)
		event_del(&tp->handler);
	memset(&tp->handler, 0, sizeof(tp->handler));
	event_set(&tp->handler, fd, EV_PERSIST|EV_TIMEOUT|EV_READ|
#ifdef HAVE_TLS_1_3
		( tp->ssl
		? ( tp->handshake_done ?  ( tp->tcp_send_first ? EV_WRITE : 0 )
		  : tp->handshake_want == SSL_ERROR_WANT_WRITE ? EV_WRITE : 0 )
		: tp->tcp_send_first ? EV_WRITE : 0 ),
#else
		( tp->tcp_send_first ? EV_WRITE : 0 ),
#endif
		xfrd_handle_tcp_pipe, tp);
	if(event_base_set(xfrd->event_base, &tp->handler) != 0)
		log_msg(LOG_ERR, "xfrd tcp: event_base_set failed");
	if(event_add(&tp->handler, &tv) != 0)
		log_msg(LOG_ERR, "xfrd tcp: event_add failed");
	tp->handler_added = 1;
}

/* handle event from fd of tcp pipe */
void
xfrd_handle_tcp_pipe(int ATTR_UNUSED(fd), short event, void* arg)
{
	struct xfrd_tcp_pipeline* tp = (struct xfrd_tcp_pipeline*)arg;
	if((event & EV_WRITE)) {
		tcp_pipe_reset_timeout(tp);
		if(tp->tcp_send_first) {
			DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: event tcp write, zone %s",
				tp->tcp_send_first->apex_str));
			xfrd_tcp_write(tp, tp->tcp_send_first);
		}
	}
	if((event & EV_READ) && tp->handler_added) {
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: event tcp read"));
		tcp_pipe_reset_timeout(tp);
		xfrd_tcp_read(tp);
	}
	if((event & EV_TIMEOUT) && tp->handler_added) {
		/* tcp connection timed out */
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: event tcp timeout"));
		xfrd_tcp_pipe_stop(tp);
	}
}

/* add a zone to the pipeline, it starts to want to write its query */
static void
pipeline_setup_new_zone(struct xfrd_tcp_set* set, struct xfrd_tcp_pipeline* tp,
	xfrd_zone_type* zone)
{
	/* assign the ID */
	int idx;
	assert(tp->key.num_unused > 0);
	/* we pick a random ID, even though it is TCP anyway */
	idx = random_generate(tp->key.num_unused);
	zone->query_id = tp->unused[idx];
	tp->unused[idx] = tp->unused[tp->key.num_unused-1];
	xfrd_tcp_pipeline_insert_id(tp, zone->query_id, zone);
	/* decrement unused counter, and fixup tree */
	(void)rbtree_delete(set->pipetree, &tp->key.node);
	tp->key.num_unused--;
	(void)rbtree_insert(set->pipetree, &tp->key.node);

	/* add to sendlist, at end */
	zone->tcp_send_next = NULL;
	zone->tcp_send_prev = tp->tcp_send_last;
	zone->in_tcp_send = 1;
	if(tp->tcp_send_last)
		tp->tcp_send_last->tcp_send_next = zone;
	else	tp->tcp_send_first = zone;
	tp->tcp_send_last = zone;

	/* is it first in line? */
	if(tp->tcp_send_first == zone) {
		xfrd_tcp_setup_write_packet(tp, zone);
		/* add write to event handler */
		tcp_pipe_reset_timeout(tp);
	}
}

void
xfrd_tcp_obtain(struct xfrd_tcp_set* set, xfrd_zone_type* zone)
{
	struct xfrd_tcp_pipeline* tp;
	assert(zone->tcp_conn == -1);
	assert(zone->tcp_waiting == 0);

	if(set->tcp_count < set->tcp_max) {
		int i;
		assert(!set->tcp_waiting_first);
		set->tcp_count ++;
		/* find a free tcp_buffer */
		for(i=0; i<set->tcp_max; i++) {
			if(set->tcp_state[i]->tcp_r->fd == -1) {
				zone->tcp_conn = i;
				break;
			}
		}
		/** What if there is no free tcp_buffer? return; */
		if (zone->tcp_conn < 0) {
			return;
		}

		tp = set->tcp_state[zone->tcp_conn];
		zone->tcp_waiting = 0;

		/* stop udp use (if any) */
		if(zone->zone_handler.ev_fd != -1)
			xfrd_udp_release(zone);

		if(!xfrd_tcp_open(set, tp, zone)) {
			zone->tcp_conn = -1;
			set->tcp_count --;
			xfrd_set_refresh_now(zone);
			return;
		}
		/* ip and ip_len set by tcp_open */
		xfrd_tcp_pipeline_init(tp);

		/* insert into tree */
		(void)rbtree_insert(set->pipetree, &tp->key.node);
		xfrd_deactivate_zone(zone);
		xfrd_unset_timer(zone);
		pipeline_setup_new_zone(set, tp, zone);
		return;
	}
	/* check for a pipeline to the same master with unused ID */
	if((tp = pipeline_find(set, zone))!= NULL) {
		int i;
		if(zone->zone_handler.ev_fd != -1)
			xfrd_udp_release(zone);
		for(i=0; i<set->tcp_max; i++) {
			if(set->tcp_state[i] == tp)
				zone->tcp_conn = i;
		}
		xfrd_deactivate_zone(zone);
		xfrd_unset_timer(zone);
		pipeline_setup_new_zone(set, tp, zone);
		return;
	}

	/* wait, at end of line */
	DEBUG(DEBUG_XFRD,2, (LOG_INFO, "xfrd: max number of tcp "
		"connections (%d) reached.", set->tcp_max));
	zone->tcp_waiting_next = 0;
	zone->tcp_waiting_prev = set->tcp_waiting_last;
	zone->tcp_waiting = 1;
	if(!set->tcp_waiting_last) {
		set->tcp_waiting_first = zone;
		set->tcp_waiting_last = zone;
	} else {
		set->tcp_waiting_last->tcp_waiting_next = zone;
		set->tcp_waiting_last = zone;
	}
	xfrd_deactivate_zone(zone);
	xfrd_unset_timer(zone);
}

int
xfrd_tcp_open(struct xfrd_tcp_set* set, struct xfrd_tcp_pipeline* tp,
	xfrd_zone_type* zone)
{
	int fd, family, conn;
	struct timeval tv;
	assert(zone->tcp_conn != -1);

	/* if there is no next master, fallback to use the first one */
	/* but there really should be a master set */
	if(!zone->master) {
		zone->master = zone->zone_options->pattern->request_xfr;
		zone->master_num = 0;
	}

	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s open tcp conn to %s",
		zone->apex_str, zone->master->ip_address_spec));
	tp->tcp_r->is_reading = 1;
	tp->tcp_r->total_bytes = 0;
	tp->tcp_r->msglen = 0;
	buffer_clear(tp->tcp_r->packet);
	tp->tcp_w->is_reading = 0;
	tp->tcp_w->total_bytes = 0;
	tp->tcp_w->msglen = 0;
	tp->connection_established = 0;

	if(zone->master->is_ipv6) {
#ifdef INET6
		family = PF_INET6;
#else
		xfrd_set_refresh_now(zone);
		return 0;
#endif
	} else {
		family = PF_INET;
	}
	fd = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if(fd == -1) {
		/* squelch 'Address family not supported by protocol' at low
		 * verbosity levels */
		if(errno != EAFNOSUPPORT || verbosity > 2)
		    log_msg(LOG_ERR, "xfrd: %s cannot create tcp socket: %s",
			zone->master->ip_address_spec, strerror(errno));
		xfrd_set_refresh_now(zone);
		return 0;
	}
	if(fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
		log_msg(LOG_ERR, "xfrd: fcntl failed: %s", strerror(errno));
		close(fd);
		xfrd_set_refresh_now(zone);
		return 0;
	}

	if(xfrd->nsd->outgoing_tcp_mss > 0) {
#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
		if(setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG,
			(void*)&xfrd->nsd->outgoing_tcp_mss,
			sizeof(xfrd->nsd->outgoing_tcp_mss)) < 0) {
			log_msg(LOG_ERR, "xfrd: setsockopt(TCP_MAXSEG)"
					"failed: %s", strerror(errno));
		}
#else
		log_msg(LOG_ERR, "setsockopt(TCP_MAXSEG) unsupported");
#endif
	}

	tp->key.ip_len = xfrd_acl_sockaddr_to(zone->master, &tp->key.ip);

	/* bind it */
	if (!xfrd_bind_local_interface(fd, zone->zone_options->pattern->
		outgoing_interface, zone->master, 1)) {
		close(fd);
		xfrd_set_refresh_now(zone);
		return 0;
        }

	conn = connect(fd, (struct sockaddr*)&tp->key.ip, tp->key.ip_len);
	if (conn == -1 && errno != EINPROGRESS) {
		log_msg(LOG_ERR, "xfrd: connect %s failed: %s",
			zone->master->ip_address_spec, strerror(errno));
		close(fd);
		xfrd_set_refresh_now(zone);
		return 0;
	}
	tp->tcp_r->fd = fd;
	tp->tcp_w->fd = fd;

	/* Check if an tls_auth name is configured which means we should try to
	   establish an SSL connection */
	if (zone->master->tls_auth_options &&
		zone->master->tls_auth_options->auth_domain_name) {
#ifdef HAVE_TLS_1_3
		/* Load client certificate (if provided) */
		if (zone->master->tls_auth_options->client_cert &&
		    zone->master->tls_auth_options->client_key) {
			if (SSL_CTX_use_certificate_chain_file(set->ssl_ctx,
			                                       zone->master->tls_auth_options->client_cert) != 1) {
				log_msg(LOG_ERR, "xfrd tls: Unable to load client certificate from file %s", zone->master->tls_auth_options->client_cert);
			}

			if (zone->master->tls_auth_options->client_key_pw) {
				SSL_CTX_set_default_passwd_cb(set->ssl_ctx, password_cb);
				SSL_CTX_set_default_passwd_cb_userdata(set->ssl_ctx, zone->master->tls_auth_options->client_key_pw);
			}

			if (SSL_CTX_use_PrivateKey_file(set->ssl_ctx, zone->master->tls_auth_options->client_key, SSL_FILETYPE_PEM) != 1) {
				log_msg(LOG_ERR, "xfrd tls: Unable to load private key from file %s", zone->master->tls_auth_options->client_key);
			}

			if (!SSL_CTX_check_private_key(set->ssl_ctx)) {
				log_msg(LOG_ERR, "xfrd tls: Client private key from file %s does not match the certificate from file %s",
				                 zone->master->tls_auth_options->client_key,
				                 zone->master->tls_auth_options->client_cert);
			}
		/* If client certificate/private key loading has failed,
		   client will not try to authenticate to the server but the connection
		   will procceed and will be up to the server to allow or deny the
		   unauthenticated connection. A server that does not enforce authentication
		   (or a badly configured server?) might allow the transfer.
		   XXX: Maybe we should close the connection now to make it obvious that
		   there is something wrong from our side. Alternatively make it obvious
		   to the operator that we're not being authenticated to the server.
		*/
		}

		if (!setup_ssl(tp, set, zone->master->tls_auth_options->auth_domain_name)) {
			log_msg(LOG_ERR, "xfrd: Cannot setup TLS on pipeline for %s to %s",
					zone->apex_str, zone->master->ip_address_spec);
			close(fd);
			xfrd_set_refresh_now(zone);
			return 0;
		}

		tp->handshake_done = 0;
		if(!ssl_handshake(tp)) {
			if(tp->handshake_want == SSL_ERROR_SYSCALL) {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed "
					"for %s to %s: %s", zone->apex_str,
					zone->master->ip_address_spec,
					strerror(errno));

			} else if(tp->handshake_want == SSL_ERROR_SSL) {
				char errmsg[1024];
				snprintf(errmsg, sizeof(errmsg), "xfrd: "
					"TLS handshake failed for %s to %s",
					zone->apex_str,
					zone->master->ip_address_spec);
				log_crypto_err(errmsg);
			} else {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed "
					"for %s to %s with %d", zone->apex_str,
					zone->master->ip_address_spec,
					tp->handshake_want);
			}
			close(fd);
			xfrd_set_refresh_now(zone);
			return 0;
		}
#else
		log_msg(LOG_ERR, "xfrd: TLS 1.3 is not available, XFR-over-TLS is "
						 "not supported for %s to %s",
						  zone->apex_str, zone->master->ip_address_spec);
		close(fd);
		xfrd_set_refresh_now(zone);
		return 0;
#endif
	}

	/* set the tcp pipe event */
	if(tp->handler_added)
		event_del(&tp->handler);
	memset(&tp->handler, 0, sizeof(tp->handler));
	event_set(&tp->handler, fd, EV_PERSIST|EV_TIMEOUT|EV_READ|
#ifdef HAVE_TLS_1_3
		( !tp->ssl
		|| tp->handshake_done
		|| tp->handshake_want == SSL_ERROR_WANT_WRITE ? EV_WRITE : 0),
#else
		EV_WRITE,
#endif
	        xfrd_handle_tcp_pipe, tp);
	if(event_base_set(xfrd->event_base, &tp->handler) != 0)
		log_msg(LOG_ERR, "xfrd tcp: event_base_set failed");
	tv.tv_sec = set->tcp_timeout;
	tv.tv_usec = 0;
	if(event_add(&tp->handler, &tv) != 0)
		log_msg(LOG_ERR, "xfrd tcp: event_add failed");
	tp->handler_added = 1;
	return 1;
}

void
xfrd_tcp_setup_write_packet(struct xfrd_tcp_pipeline* tp, xfrd_zone_type* zone)
{
	struct xfrd_tcp* tcp = tp->tcp_w;
	assert(zone->tcp_conn != -1);
	assert(zone->tcp_waiting == 0);
	/* start AXFR or IXFR for the zone */
	if(zone->soa_disk_acquired == 0 || zone->master->use_axfr_only ||
		zone->master->ixfr_disabled ||
		/* if zone expired, after the first round, do not ask for
		 * IXFR any more, but full AXFR (of any serial number) */
		(zone->state == xfrd_zone_expired && zone->round_num != 0)) {
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "request full zone transfer "
						"(AXFR) for %s to %s",
			zone->apex_str, zone->master->ip_address_spec));
		VERBOSITY(3, (LOG_INFO, "request full zone transfer "
						"(AXFR) for %s to %s",
			zone->apex_str, zone->master->ip_address_spec));

		xfrd_setup_packet(tcp->packet, TYPE_AXFR, CLASS_IN, zone->apex,
			zone->query_id, NULL);
		xfrd_prepare_zone_xfr(zone, TYPE_AXFR);
	} else {
		int apex_compress = 0;
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "request incremental zone "
						"transfer (IXFR) for %s to %s",
			zone->apex_str, zone->master->ip_address_spec));
		VERBOSITY(3, (LOG_INFO, "request incremental zone "
						"transfer (IXFR) for %s to %s",
			zone->apex_str, zone->master->ip_address_spec));

		xfrd_setup_packet(tcp->packet, TYPE_IXFR, CLASS_IN, zone->apex,
			zone->query_id, &apex_compress);
		xfrd_prepare_zone_xfr(zone, TYPE_IXFR);
		NSCOUNT_SET(tcp->packet, 1);
		xfrd_write_soa_buffer(tcp->packet, zone->apex, &zone->soa_disk,
			apex_compress);
	}
	if(zone->master->key_options && zone->master->key_options->tsig_key) {
		xfrd_tsig_sign_request(
			tcp->packet, &zone->latest_xfr->tsig, zone->master);
	}
	buffer_flip(tcp->packet);
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "sent tcp query with ID %d", zone->query_id));
	tcp->msglen = buffer_limit(tcp->packet);
	tcp->total_bytes = 0;
}

static void
tcp_conn_ready_for_reading(struct xfrd_tcp* tcp)
{
	tcp->total_bytes = 0;
	tcp->msglen = 0;
	buffer_clear(tcp->packet);
}

#ifdef HAVE_TLS_1_3
static int
conn_write_ssl(struct xfrd_tcp* tcp, SSL* ssl)
{
	int request_length;
	ssize_t sent;

	if(tcp->total_bytes < sizeof(tcp->msglen)) {
		uint16_t sendlen = htons(tcp->msglen);
		// send
		request_length = sizeof(tcp->msglen) - tcp->total_bytes;
		ERR_clear_error();
		sent = SSL_write(ssl, (const char*)&sendlen + tcp->total_bytes,
						 request_length);
		switch(SSL_get_error(ssl,sent)) {
			case SSL_ERROR_NONE:
				break;
			default:
				log_msg(LOG_ERR, "xfrd: generic write problem with tls");
		}

		if(sent == -1) {
			if(errno == EAGAIN || errno == EINTR) {
				/* write would block, try later */
				return 0;
			} else {
				return -1;
			}
		}

		tcp->total_bytes += sent;
		if(sent > (ssize_t)sizeof(tcp->msglen))
			buffer_skip(tcp->packet, sent-sizeof(tcp->msglen));
		if(tcp->total_bytes < sizeof(tcp->msglen)) {
			/* incomplete write, resume later */
			return 0;
		}
		assert(tcp->total_bytes >= sizeof(tcp->msglen));
	}

	assert(tcp->total_bytes < tcp->msglen + sizeof(tcp->msglen));

	request_length = buffer_remaining(tcp->packet);
	ERR_clear_error();
	sent = SSL_write(ssl, buffer_current(tcp->packet), request_length);
	switch(SSL_get_error(ssl,sent)) {
		case SSL_ERROR_NONE:
			break;
		default:
			log_msg(LOG_ERR, "xfrd: generic write problem with tls");
	}
	if(sent == -1) {
		if(errno == EAGAIN || errno == EINTR) {
			/* write would block, try later */
			return 0;
		} else {
			return -1;
		}
	}

	buffer_skip(tcp->packet, sent);
	tcp->total_bytes += sent;

	if(tcp->total_bytes < tcp->msglen + sizeof(tcp->msglen)) {
		/* more to write when socket becomes writable again */
		return 0;
	}

	assert(tcp->total_bytes == tcp->msglen + sizeof(tcp->msglen));
	return 1;
}
#endif

int conn_write(struct xfrd_tcp* tcp)
{
	ssize_t sent;

	if(tcp->total_bytes < sizeof(tcp->msglen)) {
		uint16_t sendlen = htons(tcp->msglen);
#ifdef HAVE_WRITEV
		struct iovec iov[2];
		iov[0].iov_base = (uint8_t*)&sendlen + tcp->total_bytes;
		iov[0].iov_len = sizeof(sendlen) - tcp->total_bytes;
		iov[1].iov_base = buffer_begin(tcp->packet);
		iov[1].iov_len = buffer_limit(tcp->packet);
		sent = writev(tcp->fd, iov, 2);
#else /* HAVE_WRITEV */
		sent = write(tcp->fd,
			(const char*)&sendlen + tcp->total_bytes,
			sizeof(tcp->msglen) - tcp->total_bytes);
#endif /* HAVE_WRITEV */

		if(sent == -1) {
			if(errno == EAGAIN || errno == EINTR) {
				/* write would block, try later */
				return 0;
			} else {
				return -1;
			}
		}

		tcp->total_bytes += sent;
		if(sent > (ssize_t)sizeof(tcp->msglen))
			buffer_skip(tcp->packet, sent-sizeof(tcp->msglen));
		if(tcp->total_bytes < sizeof(tcp->msglen)) {
			/* incomplete write, resume later */
			return 0;
		}
#ifdef HAVE_WRITEV
		if(tcp->total_bytes == tcp->msglen + sizeof(tcp->msglen)) {
			/* packet done */
			return 1;
		}
#endif
		assert(tcp->total_bytes >= sizeof(tcp->msglen));
	}

	assert(tcp->total_bytes < tcp->msglen + sizeof(tcp->msglen));

	sent = write(tcp->fd,
		buffer_current(tcp->packet),
		buffer_remaining(tcp->packet));
	if(sent == -1) {
		if(errno == EAGAIN || errno == EINTR) {
			/* write would block, try later */
			return 0;
		} else {
			return -1;
		}
	}

	buffer_skip(tcp->packet, sent);
	tcp->total_bytes += sent;

	if(tcp->total_bytes < tcp->msglen + sizeof(tcp->msglen)) {
		/* more to write when socket becomes writable again */
		return 0;
	}

	assert(tcp->total_bytes == tcp->msglen + sizeof(tcp->msglen));
	return 1;
}

void
xfrd_tcp_write(struct xfrd_tcp_pipeline* tp, xfrd_zone_type* zone)
{
	int ret;
	struct xfrd_tcp* tcp = tp->tcp_w;
	assert(zone->tcp_conn != -1);
	assert(zone == tp->tcp_send_first);
	/* see if for non-established connection, there is a connect error */
	if(!tp->connection_established) {
		/* check for pending error from nonblocking connect */
		/* from Stevens, unix network programming, vol1, 3rd ed, p450 */
		int error = 0;
		socklen_t len = sizeof(error);
		if(getsockopt(tcp->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0){
			error = errno; /* on solaris errno is error */
		}
		if(error == EINPROGRESS || error == EWOULDBLOCK)
			return; /* try again later */
		if(error != 0) {
			log_msg(LOG_ERR, "%s: Could not tcp connect to %s: %s",
				zone->apex_str, zone->master->ip_address_spec,
				strerror(error));
			xfrd_tcp_pipe_stop(tp);
			return;
		}
	}
#ifdef HAVE_TLS_1_3
	if (tp->ssl) {
		if(tp->handshake_done) {
			ret = conn_write_ssl(tcp, tp->ssl);

		} else if(ssl_handshake(tp)) {
			tcp_pipe_reset_timeout(tp); /* reschedule */
			return;

		} else {
			if(tp->handshake_want == SSL_ERROR_SYSCALL) {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed: %s",
					strerror(errno));

			} else if(tp->handshake_want == SSL_ERROR_SSL) {
				log_crypto_err("xfrd: TLS handshake failed");
			} else {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed "
					"with value: %d", tp->handshake_want);
			}
			xfrd_tcp_pipe_stop(tp);
			return;
		}
	} else
#endif
		ret = conn_write(tcp);
	if(ret == -1) {
		log_msg(LOG_ERR, "xfrd: failed writing tcp %s", strerror(errno));
		xfrd_tcp_pipe_stop(tp);
		return;
	}
	if(tcp->total_bytes != 0 && !tp->connection_established)
		tp->connection_established = 1;
	if(ret == 0) {
		return; /* write again later */
	}
	/* done writing this message */

	/* remove first zone from sendlist */
	tcp_pipe_sendlist_popfirst(tp, zone);

	/* see if other zone wants to write; init; let it write (now) */
	/* and use a loop, because 64k stack calls is a too much */
	while(tp->tcp_send_first) {
		/* setup to write for this zone */
		xfrd_tcp_setup_write_packet(tp, tp->tcp_send_first);
		/* attempt to write for this zone (if success, continue loop)*/
#ifdef HAVE_TLS_1_3
		if (tp->ssl)
			ret = conn_write_ssl(tcp, tp->ssl);
		else
#endif
			ret = conn_write(tcp);
		if(ret == -1) {
			log_msg(LOG_ERR, "xfrd: failed writing tcp %s", strerror(errno));
			xfrd_tcp_pipe_stop(tp);
			return;
		}
		if(ret == 0)
			return; /* write again later */
		tcp_pipe_sendlist_popfirst(tp, tp->tcp_send_first);
	}

	/* if sendlist empty, remove WRITE from event */

	/* listen to READ, and not WRITE events */
	assert(tp->tcp_send_first == NULL);
	tcp_pipe_reset_timeout(tp);
}

#ifdef HAVE_TLS_1_3
static int
conn_read_ssl(struct xfrd_tcp* tcp, SSL* ssl)
{
	ssize_t received;
	/* receive leading packet length bytes */
	if(tcp->total_bytes < sizeof(tcp->msglen)) {
		ERR_clear_error();
		received = SSL_read(ssl,
						(char*) &tcp->msglen + tcp->total_bytes,
						sizeof(tcp->msglen) - tcp->total_bytes);
		if (received <= 0) {
			int err = SSL_get_error(ssl, received);
			if(err == SSL_ERROR_WANT_READ && errno == EAGAIN) {
				return 0;
			}
			if(err == SSL_ERROR_ZERO_RETURN) {
				/* EOF */
				return -1;
			}
			if(err == SSL_ERROR_SYSCALL)
				log_msg(LOG_ERR, "ssl_read returned error SSL_ERROR_SYSCALL with received %zd: %s", received, strerror(errno));
			else
				log_msg(LOG_ERR, "ssl_read returned error %d with received %zd", err, received);
		}
		if(received == -1) {
			if(errno == EAGAIN || errno == EINTR) {
				/* read would block, try later */
				return 0;
			} else {
#ifdef ECONNRESET
				if (verbosity >= 2 || errno != ECONNRESET)
#endif /* ECONNRESET */
					log_msg(LOG_ERR, "tls read sz: %s", strerror(errno));
				return -1;
			}
		} else if(received == 0) {
			/* EOF */
			return -1;
		}
		tcp->total_bytes += received;
		if(tcp->total_bytes < sizeof(tcp->msglen)) {
			/* not complete yet, try later */
			return 0;
		}

		assert(tcp->total_bytes == sizeof(tcp->msglen));
		tcp->msglen = ntohs(tcp->msglen);

		if(tcp->msglen == 0) {
			buffer_set_limit(tcp->packet, tcp->msglen);
			return 1;
		}
		if(tcp->msglen > buffer_capacity(tcp->packet)) {
			log_msg(LOG_ERR, "buffer too small, dropping connection");
			return 0;
		}
		buffer_set_limit(tcp->packet, tcp->msglen);
	}

	assert(buffer_remaining(tcp->packet) > 0);
	ERR_clear_error();

	received = SSL_read(ssl, buffer_current(tcp->packet),
					buffer_remaining(tcp->packet));

	if (received <= 0) {
		int err = SSL_get_error(ssl, received);
		if(err == SSL_ERROR_ZERO_RETURN) {
			/* EOF */
			return -1;
		}
		if(err == SSL_ERROR_SYSCALL)
			log_msg(LOG_ERR, "ssl_read returned error SSL_ERROR_SYSCALL with received %zd: %s", received, strerror(errno));
		else
			log_msg(LOG_ERR, "ssl_read returned error %d with received %zd", err, received);
	}
	if(received == -1) {
		if(errno == EAGAIN || errno == EINTR) {
			/* read would block, try later */
			return 0;
		} else {
#ifdef ECONNRESET
			if (verbosity >= 2 || errno != ECONNRESET)
#endif /* ECONNRESET */
				log_msg(LOG_ERR, "tcp read %s", strerror(errno));
			return -1;
		}
	} else if(received == 0) {
		/* EOF */
		return -1;
	}

	tcp->total_bytes += received;
	buffer_skip(tcp->packet, received);

	if(buffer_remaining(tcp->packet) > 0) {
		/* not complete yet, wait for more */
		return 0;
	}

	/* completed */
	assert(buffer_position(tcp->packet) == tcp->msglen);
	return 1;
}
#endif

int
conn_read(struct xfrd_tcp* tcp)
{
	ssize_t received;
	/* receive leading packet length bytes */
	if(tcp->total_bytes < sizeof(tcp->msglen)) {
		received = read(tcp->fd,
			(char*) &tcp->msglen + tcp->total_bytes,
			sizeof(tcp->msglen) - tcp->total_bytes);
		if(received == -1) {
			if(errno == EAGAIN || errno == EINTR) {
				/* read would block, try later */
				return 0;
			} else {
#ifdef ECONNRESET
				if (verbosity >= 2 || errno != ECONNRESET)
#endif /* ECONNRESET */
				log_msg(LOG_ERR, "tcp read sz: %s", strerror(errno));
				return -1;
			}
		} else if(received == 0) {
			/* EOF */
			return -1;
		}
		tcp->total_bytes += received;
		if(tcp->total_bytes < sizeof(tcp->msglen)) {
			/* not complete yet, try later */
			return 0;
		}

		assert(tcp->total_bytes == sizeof(tcp->msglen));
		tcp->msglen = ntohs(tcp->msglen);

		if(tcp->msglen == 0) {
			buffer_set_limit(tcp->packet, tcp->msglen);
			return 1;
		}
		if(tcp->msglen > buffer_capacity(tcp->packet)) {
			log_msg(LOG_ERR, "buffer too small, dropping connection");
			return 0;
		}
		buffer_set_limit(tcp->packet, tcp->msglen);
	}

	assert(buffer_remaining(tcp->packet) > 0);

	received = read(tcp->fd, buffer_current(tcp->packet),
		buffer_remaining(tcp->packet));
	if(received == -1) {
		if(errno == EAGAIN || errno == EINTR) {
			/* read would block, try later */
			return 0;
		} else {
#ifdef ECONNRESET
			if (verbosity >= 2 || errno != ECONNRESET)
#endif /* ECONNRESET */
			log_msg(LOG_ERR, "tcp read %s", strerror(errno));
			return -1;
		}
	} else if(received == 0) {
		/* EOF */
		return -1;
	}

	tcp->total_bytes += received;
	buffer_skip(tcp->packet, received);

	if(buffer_remaining(tcp->packet) > 0) {
		/* not complete yet, wait for more */
		return 0;
	}

	/* completed */
	assert(buffer_position(tcp->packet) == tcp->msglen);
	return 1;
}

void
xfrd_tcp_read(struct xfrd_tcp_pipeline* tp)
{
	xfrd_zone_type* zone;
	struct xfrd_tcp* tcp = tp->tcp_r;
	int ret;
	enum xfrd_packet_result pkt_result;
#ifdef HAVE_TLS_1_3
	if(tp->ssl) {
		if(tp->handshake_done) {
			ret = conn_read_ssl(tcp, tp->ssl);

		} else if(ssl_handshake(tp)) {
			tcp_pipe_reset_timeout(tp); /* reschedule */
			return;

		} else {
			if(tp->handshake_want == SSL_ERROR_SYSCALL) {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed: %s",
					strerror(errno));

			} else if(tp->handshake_want == SSL_ERROR_SSL) {
				log_crypto_err("xfrd: TLS handshake failed");
			} else {
				log_msg(LOG_ERR, "xfrd: TLS handshake failed "
					"with value: %d", tp->handshake_want);
			}
			xfrd_tcp_pipe_stop(tp);
			return;
		}
	} else 
#endif
		ret = conn_read(tcp);
	if(ret == -1) {
		if(errno != 0)
			log_msg(LOG_ERR, "xfrd: failed reading tcp %s", strerror(errno));
		else
			log_msg(LOG_ERR, "xfrd: failed reading tcp: closed");
		xfrd_tcp_pipe_stop(tp);
		return;
	}
	if(ret == 0)
		return;
	/* completed msg */
	buffer_flip(tcp->packet);
	/* see which ID number it is, if skip, handle skip, NULL: warn */
	if(tcp->msglen < QHEADERSZ) {
		/* too short for DNS header, skip it */
		DEBUG(DEBUG_XFRD,1, (LOG_INFO,
			"xfrd: tcp skip response that is too short"));
		tcp_conn_ready_for_reading(tcp);
		return;
	}
	zone = xfrd_tcp_pipeline_lookup_id(tp, ID(tcp->packet));
	if(!zone || zone == TCP_NULL_SKIP) {
		/* no zone for this id? skip it */
		DEBUG(DEBUG_XFRD,1, (LOG_INFO,
			"xfrd: tcp skip response with %s ID",
			zone?"set-to-skip":"unknown"));
		tcp_conn_ready_for_reading(tcp);
		return;
	}
	assert(zone->tcp_conn != -1);

	/* handle message for zone */
	pkt_result = xfrd_handle_received_xfr_packet(zone, tcp->packet);
	/* setup for reading the next packet on this connection */
	tcp_conn_ready_for_reading(tcp);
	switch(pkt_result) {
		case xfrd_packet_more:
			/* wait for next packet */
			break;
		case xfrd_packet_newlease:
			/* set to skip if more packets with this ID */
			xfrd_tcp_pipeline_skip_id(tp, zone->query_id);
			tp->key.num_skip++;
			/* fall through to remove zone from tp */
			/* fallthrough */
		case xfrd_packet_transfer:
			if(zone->zone_options->pattern->multi_primary_check) {
				xfrd_tcp_release(xfrd->tcp_set, zone);
				xfrd_make_request(zone);
				break;
			}
			xfrd_tcp_release(xfrd->tcp_set, zone);
			assert(zone->round_num == -1);
			break;
		case xfrd_packet_notimpl:
			xfrd_disable_ixfr(zone);
			xfrd_tcp_release(xfrd->tcp_set, zone);
			/* query next server */
			xfrd_make_request(zone);
			break;
		case xfrd_packet_bad:
		case xfrd_packet_tcp:
		default:
			/* set to skip if more packets with this ID */
			xfrd_tcp_pipeline_skip_id(tp, zone->query_id);
			tp->key.num_skip++;
			xfrd_tcp_release(xfrd->tcp_set, zone);
			/* query next server */
			xfrd_make_request(zone);
			break;
	}
}

void
xfrd_tcp_release(struct xfrd_tcp_set* set, xfrd_zone_type* zone)
{
	int conn = zone->tcp_conn;
	struct xfrd_tcp_pipeline* tp = set->tcp_state[conn];
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: zone %s released tcp conn to %s",
		zone->apex_str, zone->master->ip_address_spec));
	assert(zone->tcp_conn != -1);
	assert(zone->tcp_waiting == 0);
	zone->tcp_conn = -1;
	zone->tcp_waiting = 0;

	/* remove from tcp_send list */
	tcp_pipe_sendlist_remove(tp, zone);
	/* remove it from the ID list */
	if(xfrd_tcp_pipeline_lookup_id(tp, zone->query_id) != TCP_NULL_SKIP)
		tcp_pipe_id_remove(tp, zone, 1);
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: released tcp pipe now %d unused",
		tp->key.num_unused));
	/* if pipe was full, but no more, then see if waiting element is
	 * for the same master, and can fill the unused ID */
	if(tp->key.num_unused == 1 && set->tcp_waiting_first) {
#ifdef INET6
		struct sockaddr_storage to;
#else
		struct sockaddr_in to;
#endif
		socklen_t to_len = xfrd_acl_sockaddr_to(
			set->tcp_waiting_first->master, &to);
		if(to_len == tp->key.ip_len && memcmp(&to, &tp->key.ip, to_len) == 0) {
			/* use this connection for the waiting zone */
			zone = set->tcp_waiting_first;
			assert(zone->tcp_conn == -1);
			zone->tcp_conn = conn;
			tcp_zone_waiting_list_popfirst(set, zone);
			if(zone->zone_handler.ev_fd != -1)
				xfrd_udp_release(zone);
			xfrd_unset_timer(zone);
			pipeline_setup_new_zone(set, tp, zone);
			return;
		}
		/* waiting zone did not go to same server */
	}

	/* if all unused, or only skipped leftover, close the pipeline */
	if(tp->key.num_unused >= tp->pipe_num || tp->key.num_skip >= tp->pipe_num - tp->key.num_unused)
		xfrd_tcp_pipe_release(set, tp, conn);
}

void
xfrd_tcp_pipe_release(struct xfrd_tcp_set* set, struct xfrd_tcp_pipeline* tp,
	int conn)
{
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: tcp pipe released"));
	/* one handler per tcp pipe */
	if(tp->handler_added)
		event_del(&tp->handler);
	tp->handler_added = 0;

#ifdef HAVE_TLS_1_3
	/* close SSL */
	if (tp->ssl) {
		DEBUG(DEBUG_XFRD, 1, (LOG_INFO, "xfrd: Shutting down TLS"));
		SSL_shutdown(tp->ssl);
		SSL_free(tp->ssl);
		tp->ssl = NULL;
	}
#endif

	/* fd in tcp_r and tcp_w is the same, close once */
	if(tp->tcp_r->fd != -1)
		close(tp->tcp_r->fd);
	tp->tcp_r->fd = -1;
	tp->tcp_w->fd = -1;

	/* remove from pipetree */
	(void)rbtree_delete(xfrd->tcp_set->pipetree, &tp->key.node);

	/* a waiting zone can use the free tcp slot (to another server) */
	/* if that zone fails to set-up or connect, we try to start the next
	 * waiting zone in the list */
	while(set->tcp_count == set->tcp_max && set->tcp_waiting_first) {
		/* pop first waiting process */
		xfrd_zone_type* zone = set->tcp_waiting_first;
		/* start it */
		assert(zone->tcp_conn == -1);
		zone->tcp_conn = conn;
		tcp_zone_waiting_list_popfirst(set, zone);

		/* stop udp (if any) */
		if(zone->zone_handler.ev_fd != -1)
			xfrd_udp_release(zone);
		if(!xfrd_tcp_open(set, tp, zone)) {
			zone->tcp_conn = -1;
			xfrd_set_refresh_now(zone);
			/* try to start the next zone (if any) */
			continue;
		}
		/* re-init this tcppipe */
		/* ip and ip_len set by tcp_open */
		xfrd_tcp_pipeline_init(tp);

		/* insert into tree */
		(void)rbtree_insert(set->pipetree, &tp->key.node);
		/* setup write */
		xfrd_unset_timer(zone);
		pipeline_setup_new_zone(set, tp, zone);
		/* started a task, no need for cleanups, so return */
		return;
	}
	/* no task to start, cleanup */
	assert(!set->tcp_waiting_first);
	set->tcp_count --;
	assert(set->tcp_count >= 0);
}

