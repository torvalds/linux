/*
 * tsig.c -- TSIG implementation (RFC 2845).
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */


#include "config.h"
#include <stdlib.h>
#include <ctype.h>

#include "tsig.h"
#include "tsig-openssl.h"
#include "dns.h"
#include "packet.h"
#include "query.h"
#include "rbtree.h"

#if !defined(HAVE_SSL) || !defined(HAVE_CRYPTO_MEMCMP)
/* we need fixed time compare */
#define CRYPTO_memcmp memcmp_fixedtime
int memcmp_fixedtime(const void *s1, const void *s2, size_t n)
{
	size_t i;
	const uint8_t* u1 = (const uint8_t*)s1;
	const uint8_t* u2 = (const uint8_t*)s2;
	int ret = 0, haveit = 0, bret = 0, bhaveit = 0;
	/* this routine loops for every byte in the strings.
	 * every loop, it tests ==, < and >.  All three.  One succeeds,
	 * as every time it must be equal, smaller or larger.  The one
	 * that succeeds has one if-comparison and two assignments. */
	for(i=0; i<n; i++) {
		if(u1[i] == u2[i]) {
			/* waste time equal to < and > statements */
			if(haveit) {
				bret = -1; /* waste time */
				bhaveit = 1;
			} else {
				bret = 1; /* waste time */
				bhaveit = 1;
			}
		}
		if(u1[i] < u2[i]) {
			if(haveit) {
				bret = -1; /* waste time equal to the else */
				bhaveit = 1;
			} else {
				ret = -1;
				haveit = 1;
			}
		}
		if(u1[i] > u2[i]) {
			if(haveit) {
				bret = 1; /* waste time equal to the else */
				bhaveit = 1;
			} else {
				ret = 1;
				haveit = 1;
			}
		}
	}
	/* use the variables to stop the compiler from excluding them */
	if(bhaveit) {
		if(bret == -2)
			ret = 0; /* never happens */
	} else {
		if(bret == -2)
			ret = 0; /* never happens */
	}
	return ret;
}
#endif

static region_type *tsig_region;

struct tsig_key_table
{
	rbnode_type node; /* by dname */
	tsig_key_type *key;
};
typedef struct tsig_key_table tsig_key_table_type;
static rbtree_type *tsig_key_table;

struct tsig_algorithm_table
{
	struct tsig_algorithm_table *next;
	tsig_algorithm_type *algorithm;
};
typedef struct tsig_algorithm_table tsig_algorithm_table_type;
static tsig_algorithm_table_type *tsig_algorithm_table;
static size_t max_algo_digest_size = 0;

static void
tsig_digest_variables(tsig_record_type *tsig, int tsig_timers_only)
{
	uint16_t klass = htons(CLASS_ANY);
	uint32_t ttl = htonl(0);
	uint16_t signed_time_high = htons(tsig->signed_time_high);
	uint32_t signed_time_low = htonl(tsig->signed_time_low);
	uint16_t signed_time_fudge = htons(tsig->signed_time_fudge);
	uint16_t error_code = htons(tsig->error_code);
	uint16_t other_size = htons(tsig->other_size);

	if (!tsig_timers_only) {
		tsig->algorithm->hmac_update(tsig->context,
					     dname_name(tsig->key_name),
					     tsig->key_name->name_size);
		tsig->algorithm->hmac_update(tsig->context,
					     &klass,
					     sizeof(klass));
		tsig->algorithm->hmac_update(tsig->context,
					     &ttl,
					     sizeof(ttl));
		tsig->algorithm->hmac_update(tsig->context,
					     dname_name(tsig->algorithm_name),
					     tsig->algorithm_name->name_size);
	}
	tsig->algorithm->hmac_update(tsig->context,
				     &signed_time_high,
				     sizeof(signed_time_high));
	tsig->algorithm->hmac_update(tsig->context,
				     &signed_time_low,
				     sizeof(signed_time_low));
	tsig->algorithm->hmac_update(tsig->context,
				     &signed_time_fudge,
				     sizeof(signed_time_fudge));
	if (!tsig_timers_only) {
		tsig->algorithm->hmac_update(tsig->context,
					     &error_code,
					     sizeof(error_code));
		tsig->algorithm->hmac_update(tsig->context,
					     &other_size,
					     sizeof(other_size));
		tsig->algorithm->hmac_update(tsig->context,
					     tsig->other_data,
					     tsig->other_size);
	}
}

static int
tree_dname_compare(const void* a, const void* b)
{
	return dname_compare((const dname_type*)a, (const dname_type*)b);
}

int
tsig_init(region_type *region)
{
	tsig_region = region;
	tsig_key_table = rbtree_create(region, &tree_dname_compare);
	tsig_algorithm_table = NULL;

#if defined(HAVE_SSL)
	return tsig_openssl_init(region);
#endif /* defined(HAVE_SSL) */
	return 1;
}

void
tsig_add_key(tsig_key_type *key)
{
	tsig_key_table_type *entry = (tsig_key_table_type *) region_alloc_zero(
		tsig_region, sizeof(tsig_key_table_type));
	entry->key = key;
	entry->node.key = entry->key->name;
	(void)rbtree_insert(tsig_key_table, &entry->node);
}

void
tsig_del_key(tsig_key_type *key)
{
	tsig_key_table_type *entry;
	if(!key) return;
	entry = (tsig_key_table_type*)rbtree_delete(tsig_key_table, key->name);
	if(!entry) return;
	region_recycle(tsig_region, entry, sizeof(tsig_key_table_type));
}

tsig_key_type*
tsig_find_key(const dname_type* name)
{
	tsig_key_table_type* entry;
	entry = (tsig_key_table_type*)rbtree_search(tsig_key_table, name);
	if(entry)
		return entry->key;
	return NULL;
}

void
tsig_add_algorithm(tsig_algorithm_type *algorithm)
{
	tsig_algorithm_table_type *entry
		= (tsig_algorithm_table_type *) region_alloc(
			tsig_region, sizeof(tsig_algorithm_table_type));
	entry->algorithm = algorithm;
	entry->next = tsig_algorithm_table;
	tsig_algorithm_table = entry;
	if(algorithm->maximum_digest_size > max_algo_digest_size)
		max_algo_digest_size = algorithm->maximum_digest_size;
}

/**
 * compare a tsig algorithm string lowercased
 */
int
tsig_strlowercmp(const char* str1, const char* str2)
{
	while (str1 && str2 && *str1 != '\0' && *str2 != '\0') {
		if(tolower((unsigned char)*str1) != tolower((unsigned char)*str2)) {
			if(tolower((unsigned char)*str1) < tolower((unsigned char)*str2))
				return -1;
			return 1;
		}
		str1++;
		str2++;
	}
	if (str1 && str2) {
		if (*str1 == *str2)
			return 0;
		else if (*str1 == '\0')
			return -1;
	}
	else if (!str1 && !str2)
		return 0;
	else if (!str1 && str2)
		return -1;
	return 1;
}


/*
 * Find an HMAC algorithm based on its short name.
 */
tsig_algorithm_type *
tsig_get_algorithm_by_name(const char *name)
{
	tsig_algorithm_table_type *algorithm_entry;

	for (algorithm_entry = tsig_algorithm_table;
	     algorithm_entry;
	     algorithm_entry = algorithm_entry->next)
	{
		if (tsig_strlowercmp(name, algorithm_entry->algorithm->short_name) == 0)
		{
			return algorithm_entry->algorithm;
		}
		if(strncmp("hmac-", algorithm_entry->algorithm->short_name, 5) == 0 && tsig_strlowercmp(name, algorithm_entry->algorithm->short_name+5) == 0) {
			return algorithm_entry->algorithm;
		}
	}

	return NULL;
}


const char *
tsig_error(int error_code)
{
	static char message[1000];

	switch (error_code) {
	case TSIG_ERROR_NOERROR:
		return "No Error";
		break;
	case TSIG_ERROR_BADSIG:
		return "Bad Signature";
		break;
	case TSIG_ERROR_BADKEY:
		return "Bad Key";
		break;
	case TSIG_ERROR_BADTIME:
		return "Bad Time";
		break;
	default:
		if(error_code < 16) /* DNS rcodes */
			return rcode2str(error_code);

		snprintf(message, sizeof(message),
			 "Unknown Error %d", error_code);
		break;
	}
	return message;
}

static void
tsig_cleanup(void *data)
{
	tsig_record_type *tsig = (tsig_record_type *) data;
	region_destroy(tsig->rr_region);
	region_destroy(tsig->context_region);
}

void
tsig_create_record(tsig_record_type *tsig, region_type *region)
{
	tsig_create_record_custom(tsig, region, DEFAULT_CHUNK_SIZE,
		DEFAULT_LARGE_OBJECT_SIZE, DEFAULT_INITIAL_CLEANUP_SIZE);
}

void
tsig_create_record_custom(tsig_record_type *tsig, region_type *region,
	size_t chunk_size, size_t large_object_size, size_t initial_cleanup_size)
{
	tsig->rr_region = region_create_custom(xalloc, free, chunk_size,
		large_object_size, initial_cleanup_size, 0);
	tsig->context_region = region_create_custom(xalloc, free, chunk_size,
		large_object_size, initial_cleanup_size, 0);
	if(region)
		region_add_cleanup(region, tsig_cleanup, tsig);
	tsig_init_record(tsig, NULL, NULL);
}

void
tsig_delete_record(tsig_record_type* tsig, region_type* region)
{
	if(region)
		region_remove_cleanup(region, tsig_cleanup, tsig);
	region_destroy(tsig->rr_region);
	region_destroy(tsig->context_region);
}

void
tsig_init_record(tsig_record_type *tsig,
		 tsig_algorithm_type *algorithm,
		 tsig_key_type *key)
{
	tsig->status = TSIG_NOT_PRESENT;
	tsig->error_code = TSIG_ERROR_NOERROR;
	tsig->position = 0;
	tsig->response_count = 0;
	tsig->context = NULL;
	tsig->algorithm = algorithm;
	tsig->key = key;
	tsig->prior_mac_size = 0;
	tsig->prior_mac_data = NULL;
	region_free_all(tsig->context_region);
}

int
tsig_from_query(tsig_record_type *tsig)
{
	tsig_key_type *key = NULL;
	tsig_algorithm_table_type *algorithm_entry;
	tsig_algorithm_type *algorithm = NULL;
	uint64_t current_time;
	uint64_t signed_time;

	assert(tsig->status == TSIG_OK);
	assert(!tsig->algorithm);
	assert(!tsig->key);

	key = (tsig_key_type*)tsig_find_key(tsig->key_name);

	for (algorithm_entry = tsig_algorithm_table;
	     algorithm_entry;
	     algorithm_entry = algorithm_entry->next)
	{
		if (dname_compare(
			    tsig->algorithm_name,
			    algorithm_entry->algorithm->wireformat_name) == 0)
		{
			algorithm = algorithm_entry->algorithm;
			break;
		}
	}

	if (!algorithm || !key) {
		/* Algorithm or key is unknown, cannot authenticate.  */
		tsig->error_code = TSIG_ERROR_BADKEY;
		return 0;
	}

	if ((tsig->algorithm && algorithm != tsig->algorithm)
	    || (tsig->key && key != tsig->key))
	{
		/*
		 * Algorithm or key changed during a single connection,
		 * return error.
		 */
		tsig->error_code = TSIG_ERROR_BADKEY;
		return 0;
	}

	signed_time = ((((uint64_t) tsig->signed_time_high) << 32) |
		       ((uint64_t) tsig->signed_time_low));

	current_time = (uint64_t) time(NULL);
	if ((current_time < signed_time - tsig->signed_time_fudge)
	    || (current_time > signed_time + tsig->signed_time_fudge))
	{
		uint16_t current_time_high;
		uint32_t current_time_low;

#if 0 /* debug */
		char current_time_text[26];
		char signed_time_text[26];
		time_t clock;

		clock = (time_t) current_time;
		ctime_r(&clock, current_time_text);
		current_time_text[24] = '\0';

		clock = (time_t) signed_time;
		ctime_r(&clock, signed_time_text);
		signed_time_text[24] = '\0';

		log_msg(LOG_ERR,
			"current server time %s is outside the range of TSIG"
			" signed time %s with fudge %u",
			current_time_text,
			signed_time_text,
			(unsigned) tsig->signed_time_fudge);
#endif

		tsig->error_code = TSIG_ERROR_BADTIME;
		current_time_high = (uint16_t) (current_time >> 32);
		current_time_low = (uint32_t) current_time;
		tsig->other_size = 6;
		tsig->other_data = (uint8_t *) region_alloc(
			tsig->rr_region, sizeof(uint16_t) + sizeof(uint32_t));
		write_uint16(tsig->other_data, current_time_high);
		write_uint32(tsig->other_data + 2, current_time_low);
		return 0;
	}

	tsig->algorithm = algorithm;
	tsig->key = key;
	tsig->response_count = 0;
	tsig->prior_mac_size = 0;

	return 1;
}

void
tsig_init_query(tsig_record_type *tsig, uint16_t original_query_id)
{
	assert(tsig);
	assert(tsig->algorithm);
	assert(tsig->key);

	tsig->response_count = 0;
	tsig->prior_mac_size = 0;
	tsig->algorithm_name = tsig->algorithm->wireformat_name;
	tsig->key_name = tsig->key->name;
	tsig->mac_size = 0;
	tsig->mac_data = NULL;
	tsig->original_query_id = original_query_id;
	tsig->error_code = TSIG_ERROR_NOERROR;
	tsig->other_size = 0;
	tsig->other_data = NULL;
}

void
tsig_prepare(tsig_record_type *tsig)
{
	if (!tsig->context) {
		assert(tsig->algorithm);
		tsig->context = tsig->algorithm->hmac_create_context(
			tsig->context_region);
		tsig->prior_mac_data = (uint8_t *) region_alloc(
			tsig->context_region,
			tsig->algorithm->maximum_digest_size);
	}
	tsig->algorithm->hmac_init_context(tsig->context,
					   tsig->algorithm,
					   tsig->key);

	if (tsig->prior_mac_size > 0) {
		uint16_t mac_size = htons(tsig->prior_mac_size);
		tsig->algorithm->hmac_update(tsig->context,
					     &mac_size,
					     sizeof(mac_size));
		tsig->algorithm->hmac_update(tsig->context,
					     tsig->prior_mac_data,
					     tsig->prior_mac_size);
	}

	tsig->updates_since_last_prepare = 0;
}

void
tsig_update(tsig_record_type *tsig, buffer_type *packet, size_t length)
{
	uint16_t original_query_id = htons(tsig->original_query_id);

	assert(length <= buffer_limit(packet));

	tsig->algorithm->hmac_update(tsig->context,
				     &original_query_id,
				     sizeof(original_query_id));
	tsig->algorithm->hmac_update(
		tsig->context,
		buffer_at(packet, sizeof(original_query_id)),
		length - sizeof(original_query_id));
	if (QR(packet)) {
		++tsig->response_count;
	}

	++tsig->updates_since_last_prepare;
}

void
tsig_sign(tsig_record_type *tsig)
{
	uint64_t current_time = (uint64_t) time(NULL);
	tsig->signed_time_high = (uint16_t) (current_time >> 32);
	tsig->signed_time_low = (uint32_t) current_time;
	tsig->signed_time_fudge = 300; /* XXX; hardcoded value */

	tsig_digest_variables(tsig, tsig->response_count > 1);

	tsig->algorithm->hmac_final(tsig->context,
				    tsig->prior_mac_data,
				    &tsig->prior_mac_size);

	tsig->mac_size = tsig->prior_mac_size;
	tsig->mac_data = tsig->prior_mac_data;
}

int
tsig_verify(tsig_record_type *tsig)
{
	tsig_digest_variables(tsig, tsig->response_count > 1);

	tsig->algorithm->hmac_final(tsig->context,
				    tsig->prior_mac_data,
				    &tsig->prior_mac_size);

	if (tsig->mac_size != tsig->prior_mac_size
	    || CRYPTO_memcmp(tsig->mac_data,
		      tsig->prior_mac_data,
		      tsig->mac_size) != 0)
	{
		/* Digest is incorrect, cannot authenticate.  */
		tsig->error_code = TSIG_ERROR_BADSIG;
		return 0;
	} else {
		return 1;
	}
}

int
tsig_find_rr(tsig_record_type *tsig, buffer_type *packet)
{
	size_t saved_position = buffer_position(packet);
	size_t rrcount = ((size_t)QDCOUNT(packet)
			  + (size_t)ANCOUNT(packet)
			  + (size_t)NSCOUNT(packet)
			  + (size_t)ARCOUNT(packet));
	size_t i;
	int result;

	if (ARCOUNT(packet) == 0) {
		tsig->status = TSIG_NOT_PRESENT;
		return 1;
	}
	if(rrcount > 65530) {
		/* impossibly high number of records in 64k, reject packet */
		buffer_set_position(packet, saved_position);
		return 0;
	}

	buffer_set_position(packet, QHEADERSZ);

	/* TSIG must be the last record, so skip all others. */
	for (i = 0; i < rrcount - 1; ++i) {
		if (!packet_skip_rr(packet, i < QDCOUNT(packet))) {
			buffer_set_position(packet, saved_position);
			return 0;
		}
	}

	result = tsig_parse_rr(tsig, packet);
	buffer_set_position(packet, saved_position);
	return result;
}

int
tsig_parse_rr(tsig_record_type *tsig, buffer_type *packet)
{
	uint16_t type;
	uint16_t klass;
	uint32_t ttl;
	uint16_t rdlen;

	tsig->status = TSIG_NOT_PRESENT;
	tsig->position = buffer_position(packet);
	tsig->key_name = NULL;
	tsig->algorithm_name = NULL;
	tsig->mac_data = NULL;
	tsig->other_data = NULL;
	region_free_all(tsig->rr_region);

	tsig->key_name = dname_make_from_packet(tsig->rr_region, packet, 1, 1);
	if (!tsig->key_name) {
		buffer_set_position(packet, tsig->position);
		return 0;
	}

	if (!buffer_available(packet, 10)) {
		buffer_set_position(packet, tsig->position);
		return 0;
	}

	type = buffer_read_u16(packet);
	klass = buffer_read_u16(packet);

	/* TSIG not present */
	if (type != TYPE_TSIG || klass != CLASS_ANY) {
		buffer_set_position(packet, tsig->position);
		return 1;
	}

	ttl = buffer_read_u32(packet);
	rdlen = buffer_read_u16(packet);

	tsig->status = TSIG_ERROR;
	tsig->error_code = RCODE_FORMAT;
	if (ttl != 0 || !buffer_available(packet, rdlen)) {
		buffer_set_position(packet, tsig->position);
		return 0;
	}

	tsig->algorithm_name = dname_make_from_packet(
		tsig->rr_region, packet, 1, 1);
	if (!tsig->algorithm_name || !buffer_available(packet, 10)) {
		buffer_set_position(packet, tsig->position);
		return 0;
	}

	tsig->signed_time_high = buffer_read_u16(packet);
	tsig->signed_time_low = buffer_read_u32(packet);
	tsig->signed_time_fudge = buffer_read_u16(packet);
	tsig->mac_size = buffer_read_u16(packet);
	if (!buffer_available(packet, tsig->mac_size)) {
		buffer_set_position(packet, tsig->position);
		tsig->mac_size = 0;
		return 0;
	}
	if(tsig->mac_size > 16384) {
		/* the hash should not be too big, really 512/8=64 bytes */
		buffer_set_position(packet, tsig->position);
		tsig->mac_size = 0;
		return 0;
	}
	tsig->mac_data = (uint8_t *) region_alloc_init(
		tsig->rr_region, buffer_current(packet), tsig->mac_size);
	buffer_skip(packet, tsig->mac_size);
	if (!buffer_available(packet, 6)) {
		buffer_set_position(packet, tsig->position);
		return 0;
	}
	tsig->original_query_id = buffer_read_u16(packet);
	tsig->error_code = buffer_read_u16(packet);
	tsig->other_size = buffer_read_u16(packet);
	if (!buffer_available(packet, tsig->other_size) || tsig->other_size > 16) {
		tsig->other_size = 0;
		buffer_set_position(packet, tsig->position);
		return 0;
	}
	tsig->other_data = (uint8_t *) region_alloc_init(
		tsig->rr_region, buffer_current(packet), tsig->other_size);
	buffer_skip(packet, tsig->other_size);
	tsig->status = TSIG_OK;
	return 1;
}

void
tsig_append_rr(tsig_record_type *tsig, buffer_type *packet)
{
	size_t rdlength_pos;

	/* XXX: TODO key name compression? */
	if(tsig->key_name)
		buffer_write(packet, dname_name(tsig->key_name),
		     tsig->key_name->name_size);
	else	buffer_write_u8(packet, 0);
	buffer_write_u16(packet, TYPE_TSIG);
	buffer_write_u16(packet, CLASS_ANY);
	buffer_write_u32(packet, 0); /* TTL */
	rdlength_pos = buffer_position(packet);
	buffer_skip(packet, sizeof(uint16_t));
	if(tsig->algorithm_name)
		buffer_write(packet, dname_name(tsig->algorithm_name),
		     tsig->algorithm_name->name_size);
	else 	buffer_write_u8(packet, 0);
	buffer_write_u16(packet, tsig->signed_time_high);
	buffer_write_u32(packet, tsig->signed_time_low);
	buffer_write_u16(packet, tsig->signed_time_fudge);
	buffer_write_u16(packet, tsig->mac_size);
	if(tsig->mac_size != 0)
		buffer_write(packet, tsig->mac_data, tsig->mac_size);
	buffer_write_u16(packet, tsig->original_query_id);
	buffer_write_u16(packet, tsig->error_code);
	buffer_write_u16(packet, tsig->other_size);
	if(tsig->other_size != 0)
		buffer_write(packet, tsig->other_data, tsig->other_size);

	buffer_write_u16_at(packet, rdlength_pos,
			    buffer_position(packet) - rdlength_pos
			    - sizeof(uint16_t));
}

size_t
tsig_reserved_space(tsig_record_type *tsig)
{
	if (tsig->status == TSIG_NOT_PRESENT)
		return 0;

	return (
		(tsig->key_name?tsig->key_name->name_size:1)   /* Owner */
		+ sizeof(uint16_t)	    /* Type */
		+ sizeof(uint16_t)	    /* Class */
		+ sizeof(uint32_t)	    /* TTL */
		+ sizeof(uint16_t)	    /* RDATA length */
		+ (tsig->algorithm_name?tsig->algorithm_name->name_size:1)
		+ sizeof(uint16_t)	    /* Signed time (high) */
		+ sizeof(uint32_t)	    /* Signed time (low) */
		+ sizeof(uint16_t)	    /* Signed time fudge */
		+ sizeof(uint16_t)	    /* MAC size */
		+ max_algo_digest_size 	    /* MAC data */
		+ sizeof(uint16_t)	    /* Original query ID */
		+ sizeof(uint16_t)	    /* Error code */
		+ sizeof(uint16_t)	    /* Other size */
		+ tsig->other_size);	    /* Other data */
}

void
tsig_error_reply(tsig_record_type *tsig)
{
	if(tsig->mac_data)
		memset(tsig->mac_data, 0, tsig->mac_size);
	tsig->mac_size = 0;
}

void
tsig_finalize()
{
#if defined(HAVE_SSL)
	tsig_openssl_finalize();
#endif /* defined(HAVE_SSL) */
}
