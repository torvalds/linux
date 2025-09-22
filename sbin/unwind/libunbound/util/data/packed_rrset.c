/*
 * util/data/packed_rrset.c - data storage for a set of resource records.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains the data storage for RRsets.
 */

#include "config.h"
#include "util/data/msgparse.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "util/storage/lookup3.h"
#include "util/log.h"
#include "util/alloc.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "sldns/rrdef.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"

void
ub_packed_rrset_parsedelete(struct ub_packed_rrset_key* pkey,
        struct alloc_cache* alloc)
{
	if(!pkey)
		return;
	free(pkey->entry.data);
	pkey->entry.data = NULL;
	free(pkey->rk.dname);
	pkey->rk.dname = NULL;
	pkey->id = 0;
	alloc_special_release(alloc, pkey);
}

size_t 
ub_rrset_sizefunc(void* key, void* data)
{
	struct ub_packed_rrset_key* k = (struct ub_packed_rrset_key*)key;
	struct packed_rrset_data* d = (struct packed_rrset_data*)data;
	size_t s = sizeof(struct ub_packed_rrset_key) + k->rk.dname_len;
	s += packed_rrset_sizeof(d) + lock_get_mem(&k->entry.lock);
	return s;
}

size_t 
packed_rrset_sizeof(struct packed_rrset_data* d)
{
	size_t s;
	if(d->rrsig_count > 0) {
		s = ((uint8_t*)d->rr_data[d->count+d->rrsig_count-1] - 
			(uint8_t*)d) + d->rr_len[d->count+d->rrsig_count-1];
	} else {
		log_assert(d->count > 0);
		s = ((uint8_t*)d->rr_data[d->count-1] - (uint8_t*)d) + 
			d->rr_len[d->count-1];
	}
	return s;
}

int 
ub_rrset_compare(void* k1, void* k2)
{
	struct ub_packed_rrset_key* key1 = (struct ub_packed_rrset_key*)k1;
	struct ub_packed_rrset_key* key2 = (struct ub_packed_rrset_key*)k2;
	int c;
	if(key1 == key2)
		return 0;
	if(key1->rk.type != key2->rk.type) {
		if(key1->rk.type < key2->rk.type)
			return -1;
		return 1;
	}
	if(key1->rk.dname_len != key2->rk.dname_len) {
		if(key1->rk.dname_len < key2->rk.dname_len)
			return -1;
		return 1;
	}
	if((c=query_dname_compare(key1->rk.dname, key2->rk.dname)) != 0)
		return c;
	if(key1->rk.rrset_class != key2->rk.rrset_class) {
		if(key1->rk.rrset_class < key2->rk.rrset_class)
			return -1;
		return 1;
	}
	if(key1->rk.flags != key2->rk.flags) {
		if(key1->rk.flags < key2->rk.flags)
			return -1;
		return 1;
	}
	return 0;
}

void 
ub_rrset_key_delete(void* key, void* userdata)
{
	struct ub_packed_rrset_key* k = (struct ub_packed_rrset_key*)key;
	struct alloc_cache* a = (struct alloc_cache*)userdata;
	k->id = 0;
	free(k->rk.dname);
	k->rk.dname = NULL;
	alloc_special_release(a, k);
}

void 
rrset_data_delete(void* data, void* ATTR_UNUSED(userdata))
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)data;
	free(d);
}

int 
rrsetdata_equal(struct packed_rrset_data* d1, struct packed_rrset_data* d2)
{
	size_t i;
	size_t total;
	if(d1->count != d2->count || d1->rrsig_count != d2->rrsig_count) 
		return 0;
	total = d1->count + d1->rrsig_count;
	for(i=0; i<total; i++) {
		if(d1->rr_len[i] != d2->rr_len[i])
			return 0;
		if(memcmp(d1->rr_data[i], d2->rr_data[i], d1->rr_len[i]) != 0)
			return 0;
	}
	return 1;
}

hashvalue_type
rrset_key_hash(struct packed_rrset_key* key)
{
	/* type is hashed in host order */
	uint16_t t = ntohs(key->type);
	/* Note this MUST be identical to pkt_hash_rrset in msgparse.c */
	/* this routine does not have a compressed name */
	hashvalue_type h = 0xab;
	h = dname_query_hash(key->dname, h);
	h = hashlittle(&t, sizeof(t), h);
	h = hashlittle(&key->rrset_class, sizeof(uint16_t), h);
	h = hashlittle(&key->flags, sizeof(uint32_t), h);
	return h;
}

void 
packed_rrset_ptr_fixup(struct packed_rrset_data* data)
{
	size_t i;
	size_t total = data->count + data->rrsig_count;
	uint8_t* nextrdata;
	/* fixup pointers in packed rrset data */
	data->rr_len = (size_t*)((uint8_t*)data +
		sizeof(struct packed_rrset_data));
	data->rr_data = (uint8_t**)&(data->rr_len[total]);
	data->rr_ttl = (time_t*)&(data->rr_data[total]);
	nextrdata = (uint8_t*)&(data->rr_ttl[total]);
	for(i=0; i<total; i++) {
		data->rr_data[i] = nextrdata;
		nextrdata += data->rr_len[i];
	}
}

void 
get_cname_target(struct ub_packed_rrset_key* rrset, uint8_t** dname, 
	size_t* dname_len)
{
	struct packed_rrset_data* d;
	size_t len;
	if(ntohs(rrset->rk.type) != LDNS_RR_TYPE_CNAME && 
		ntohs(rrset->rk.type) != LDNS_RR_TYPE_DNAME)
		return;
	d = (struct packed_rrset_data*)rrset->entry.data;
	if(d->count < 1)
		return;
	if(d->rr_len[0] < 3) /* at least rdatalen + 0byte root label */
		return;
	len = sldns_read_uint16(d->rr_data[0]);
	if(len != d->rr_len[0] - sizeof(uint16_t))
		return;
	if(dname_valid(d->rr_data[0]+sizeof(uint16_t), len) != len)
		return;
	*dname = d->rr_data[0]+sizeof(uint16_t);
	*dname_len = len;
}

void 
packed_rrset_ttl_add(struct packed_rrset_data* data, time_t add)
{
	size_t i;
	size_t total = data->count + data->rrsig_count;
	data->ttl_add = add;
	data->ttl += add;
	for(i=0; i<total; i++)
		data->rr_ttl[i] += add;
}

const char* 
rrset_trust_to_string(enum rrset_trust s)
{
	switch(s) {
	case rrset_trust_none: 		return "rrset_trust_none";
	case rrset_trust_add_noAA: 	return "rrset_trust_add_noAA";
	case rrset_trust_auth_noAA: 	return "rrset_trust_auth_noAA";
	case rrset_trust_add_AA: 	return "rrset_trust_add_AA";
	case rrset_trust_nonauth_ans_AA:return "rrset_trust_nonauth_ans_AA";
	case rrset_trust_ans_noAA: 	return "rrset_trust_ans_noAA";
	case rrset_trust_glue: 		return "rrset_trust_glue";
	case rrset_trust_auth_AA: 	return "rrset_trust_auth_AA";
	case rrset_trust_ans_AA: 	return "rrset_trust_ans_AA";
	case rrset_trust_sec_noglue: 	return "rrset_trust_sec_noglue";
	case rrset_trust_prim_noglue: 	return "rrset_trust_prim_noglue";
	case rrset_trust_validated: 	return "rrset_trust_validated";
	case rrset_trust_ultimate: 	return "rrset_trust_ultimate";
	}
	return "unknown_rrset_trust_value";
}

const char* 
sec_status_to_string(enum sec_status s)
{
	switch(s) {
	case sec_status_unchecked: 	return "sec_status_unchecked";
	case sec_status_bogus: 		return "sec_status_bogus";
	case sec_status_indeterminate: 	return "sec_status_indeterminate";
	case sec_status_insecure: 	return "sec_status_insecure";
	case sec_status_secure_sentinel_fail: 	return "sec_status_secure_sentinel_fail";
	case sec_status_secure: 	return "sec_status_secure";
	}
	return "unknown_sec_status_value";
}

void log_rrset_key(enum verbosity_value v, const char* str, 
	struct ub_packed_rrset_key* rrset)
{
	if(verbosity >= v)
		log_nametypeclass(v, str, rrset->rk.dname,
			ntohs(rrset->rk.type), ntohs(rrset->rk.rrset_class));
}

int packed_rr_to_string(struct ub_packed_rrset_key* rrset, size_t i,
	time_t now, char* dest, size_t dest_len)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)rrset->
		entry.data;
	uint8_t rr[65535];
	size_t wlen;
	size_t rlen = rrset->rk.dname_len + 2 + 2 + 4 + d->rr_len[i];
	time_t adjust = 0;
	log_assert(dest_len > 0 && dest);
	if(rlen > dest_len) {
		dest[0] = 0;
		return 0;
	}
	memmove(rr, rrset->rk.dname, rrset->rk.dname_len);
	if(i < d->count)
		memmove(rr+rrset->rk.dname_len, &rrset->rk.type, 2);
	else	sldns_write_uint16(rr+rrset->rk.dname_len, LDNS_RR_TYPE_RRSIG);
	memmove(rr+rrset->rk.dname_len+2, &rrset->rk.rrset_class, 2);
	adjust = SERVE_ORIGINAL_TTL ? d->ttl_add : now;
	if (d->rr_ttl[i] < adjust) adjust = d->rr_ttl[i]; /* Prevent negative TTL overflow */
	sldns_write_uint32(rr+rrset->rk.dname_len+4,
		(uint32_t)(d->rr_ttl[i]-adjust));
	memmove(rr+rrset->rk.dname_len+8, d->rr_data[i], d->rr_len[i]);
	wlen = (size_t)sldns_wire2str_rr_buf(rr, rlen, dest, dest_len);
	if(wlen >= dest_len) {
		/* the output string was truncated */
		log_info("rrbuf failure %d %s", (int)d->rr_len[i], dest);
		dest[0] = 0;
		return 0;
	} 
	return 1;
}

void log_packed_rrset(enum verbosity_value v, const char* str,
	struct ub_packed_rrset_key* rrset)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)rrset->
		entry.data;
	char buf[65535];
	size_t i;
	if(verbosity < v)
		return;
	for(i=0; i<d->count+d->rrsig_count; i++) {
		if(!packed_rr_to_string(rrset, i, 0, buf, sizeof(buf))) {
			log_info("%s: rr %d wire2str-error", str, (int)i);
		} else {
			log_info("%s: %s", str, buf);
		}
	}
}

time_t 
ub_packed_rrset_ttl(struct ub_packed_rrset_key* key)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)key->
		entry.data;
	return d->ttl;
}

struct ub_packed_rrset_key*
packed_rrset_copy_region(struct ub_packed_rrset_key* key, 
	struct regional* region, time_t now)
{
	struct ub_packed_rrset_key* ck = regional_alloc(region, 
		sizeof(struct ub_packed_rrset_key));
	struct packed_rrset_data* d;
	struct packed_rrset_data* data = (struct packed_rrset_data*)
		key->entry.data;
	size_t dsize, i;
	time_t adjust = 0;
	if(!ck)
		return NULL;
	ck->id = key->id;
	memset(&ck->entry, 0, sizeof(ck->entry));
	ck->entry.hash = key->entry.hash;
	ck->entry.key = ck;
	ck->rk = key->rk;
	ck->rk.dname = regional_alloc_init(region, key->rk.dname, 
		key->rk.dname_len);
	if(!ck->rk.dname)
		return NULL;
	dsize = packed_rrset_sizeof(data);
	d = (struct packed_rrset_data*)regional_alloc_init(region, data, dsize);
	if(!d)
		return NULL;
	ck->entry.data = d;
	packed_rrset_ptr_fixup(d);
	/* make TTLs relative - once per rrset */
	adjust = SERVE_ORIGINAL_TTL ? data->ttl_add : now;
	for(i=0; i<d->count + d->rrsig_count; i++) {
		if(d->rr_ttl[i] < adjust)
			d->rr_ttl[i] = SERVE_EXPIRED?SERVE_EXPIRED_REPLY_TTL:0;
		else	d->rr_ttl[i] -= adjust;
	}
	if(d->ttl < adjust)
		d->ttl = SERVE_EXPIRED?SERVE_EXPIRED_REPLY_TTL:0;
	else	d->ttl -= adjust;
	d->ttl_add = 0; /* TTLs have been made relative */
	return ck;
}

struct ub_packed_rrset_key* 
packed_rrset_copy_alloc(struct ub_packed_rrset_key* key, 
	struct alloc_cache* alloc, time_t now)
{
	struct packed_rrset_data* fd, *dd;
	struct ub_packed_rrset_key* dk = alloc_special_obtain(alloc);
	if(!dk) return NULL;
	fd = (struct packed_rrset_data*)key->entry.data;
	dk->entry.hash = key->entry.hash;
	dk->rk = key->rk;
	dk->rk.dname = (uint8_t*)memdup(key->rk.dname, key->rk.dname_len);
	if(!dk->rk.dname) {
		alloc_special_release(alloc, dk);
		return NULL;
	}
	dd = (struct packed_rrset_data*)memdup(fd, packed_rrset_sizeof(fd));
	if(!dd) {
		free(dk->rk.dname);
		alloc_special_release(alloc, dk);
		return NULL;
	}
	packed_rrset_ptr_fixup(dd);
	dk->entry.data = (void*)dd;
	packed_rrset_ttl_add(dd, now);
	return dk;
}

int
packed_rrset_find_rr(struct packed_rrset_data* d, uint8_t* rdata, size_t len,
	size_t* index)
{
	size_t i;
	for(i=0; i<d->count; i++) {
		if(d->rr_len[i] != len)
			continue;
		if(memcmp(d->rr_data[i], rdata, len) == 0) {
			*index = i;
			return 1;
		}
	}
	return 0;
}
