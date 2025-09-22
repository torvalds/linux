/*
 * validator/val_kentry.c - validator key entry definition.
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
 * This file contains functions for dealing with validator key entries.
 */
#include "config.h"
#include "validator/val_kentry.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "util/storage/lookup3.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "sldns/rrdef.h"
#include "sldns/keyraw.h"

size_t 
key_entry_sizefunc(void* key, void* data)
{
	struct key_entry_key* kk = (struct key_entry_key*)key;
	struct key_entry_data* kd = (struct key_entry_data*)data;
	size_t s = sizeof(*kk) + kk->namelen;
	s += sizeof(*kd) + lock_get_mem(&kk->entry.lock);
	if(kd->rrset_data)
		s += packed_rrset_sizeof(kd->rrset_data);
	if(kd->reason)
		s += strlen(kd->reason)+1;
	if(kd->algo)
		s += strlen((char*)kd->algo)+1;
	return s;
}

int 
key_entry_compfunc(void* k1, void* k2)
{
	struct key_entry_key* n1 = (struct key_entry_key*)k1;
	struct key_entry_key* n2 = (struct key_entry_key*)k2;
	if(n1->key_class != n2->key_class) {
		if(n1->key_class < n2->key_class)
			return -1;
		return 1;
	}
	return query_dname_compare(n1->name, n2->name);
}

void 
key_entry_delkeyfunc(void* key, void* ATTR_UNUSED(userarg))
{
	struct key_entry_key* kk = (struct key_entry_key*)key;
	if(!key)
		return;
	lock_rw_destroy(&kk->entry.lock);
	free(kk->name);
	free(kk);
}

void 
key_entry_deldatafunc(void* data, void* ATTR_UNUSED(userarg))
{
	struct key_entry_data* kd = (struct key_entry_data*)data;
	free(kd->reason);
	free(kd->rrset_data);
	free(kd->algo);
	free(kd);
}

void 
key_entry_hash(struct key_entry_key* kk)
{
	kk->entry.hash = 0x654;
	kk->entry.hash = hashlittle(&kk->key_class, sizeof(kk->key_class), 
		kk->entry.hash);
	kk->entry.hash = dname_query_hash(kk->name, kk->entry.hash);
}

struct key_entry_key* 
key_entry_copy_toregion(struct key_entry_key* kkey, struct regional* region)
{
	struct key_entry_key* newk;
	newk = regional_alloc_init(region, kkey, sizeof(*kkey));
	if(!newk)
		return NULL;
	newk->name = regional_alloc_init(region, kkey->name, kkey->namelen);
	if(!newk->name)
		return NULL;
	newk->entry.key = newk;
	if(newk->entry.data) {
		/* copy data element */
		struct key_entry_data *d = (struct key_entry_data*)
			kkey->entry.data;
		struct key_entry_data *newd;
		newd = regional_alloc_init(region, d, sizeof(*d));
		if(!newd)
			return NULL;
		/* copy rrset */
		if(d->rrset_data) {
			newd->rrset_data = regional_alloc_init(region,
				d->rrset_data, 
				packed_rrset_sizeof(d->rrset_data));
			if(!newd->rrset_data)
				return NULL;
			packed_rrset_ptr_fixup(newd->rrset_data);
		}
		if(d->reason) {
			newd->reason = regional_strdup(region, d->reason);
			if(!newd->reason)
				return NULL;
		}
		if(d->algo) {
			newd->algo = (uint8_t*)regional_strdup(region,
				(char*)d->algo);
			if(!newd->algo)
				return NULL;
		}
		newk->entry.data = newd;
	}
	return newk;
}

struct key_entry_key* 
key_entry_copy(struct key_entry_key* kkey, int copy_reason)
{
	struct key_entry_key* newk;
	if(!kkey)
		return NULL;
	newk = memdup(kkey, sizeof(*kkey));
	if(!newk)
		return NULL;
	newk->name = memdup(kkey->name, kkey->namelen);
	if(!newk->name) {
		free(newk);
		return NULL;
	}
	lock_rw_init(&newk->entry.lock);
	newk->entry.key = newk;
	if(newk->entry.data) {
		/* copy data element */
		struct key_entry_data *d = (struct key_entry_data*)
			kkey->entry.data;
		struct key_entry_data *newd;
		newd = memdup(d, sizeof(*d));
		if(!newd) {
			free(newk->name);
			free(newk);
			return NULL;
		}
		/* copy rrset */
		if(d->rrset_data) {
			newd->rrset_data = memdup(d->rrset_data, 
				packed_rrset_sizeof(d->rrset_data));
			if(!newd->rrset_data) {
				free(newd);
				free(newk->name);
				free(newk);
				return NULL;
			}
			packed_rrset_ptr_fixup(newd->rrset_data);
		}
		if(copy_reason && d->reason && *d->reason != 0) {
			newd->reason = strdup(d->reason);
			if(!newd->reason) {
				free(newd->rrset_data);
				free(newd);
				free(newk->name);
				free(newk);
				return NULL;
			}
		} else {
			newd->reason = NULL;
		}
		if(d->algo) {
			newd->algo = (uint8_t*)strdup((char*)d->algo);
			if(!newd->algo) {
				free(newd->rrset_data);
				free(newd->reason);
				free(newd);
				free(newk->name);
				free(newk);
				return NULL;
			}
		}
		newk->entry.data = newd;
	}
	return newk;
}

int 
key_entry_isnull(struct key_entry_key* kkey)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	return (!d->isbad && d->rrset_data == NULL);
}

int 
key_entry_isgood(struct key_entry_key* kkey)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	return (!d->isbad && d->rrset_data != NULL);
}

int 
key_entry_isbad(struct key_entry_key* kkey)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	return (int)(d->isbad);
}

char*
key_entry_get_reason(struct key_entry_key* kkey)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	return d->reason;
}

sldns_ede_code
key_entry_get_reason_bogus(struct key_entry_key* kkey)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	return d->reason_bogus;

}

/** setup key entry in region */
static int
key_entry_setup(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass, 
	struct key_entry_key** k, struct key_entry_data** d)
{
	*k = regional_alloc(region, sizeof(**k));
	if(!*k)
		return 0;
	memset(*k, 0, sizeof(**k));
	(*k)->entry.key = *k;
	(*k)->name = regional_alloc_init(region, name, namelen);
	if(!(*k)->name)
		return 0;
	(*k)->namelen = namelen;
	(*k)->key_class = dclass;
	*d = regional_alloc(region, sizeof(**d));
	if(!*d)
		return 0;
	(*k)->entry.data = *d;
	return 1;
}

struct key_entry_key* 
key_entry_create_null(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass, time_t ttl,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now)
{
	struct key_entry_key* k;
	struct key_entry_data* d;
	if(!key_entry_setup(region, name, namelen, dclass, &k, &d))
		return NULL;
	d->ttl = now + ttl;
	d->isbad = 0;
	d->reason = (!reason || *reason == 0)
		?NULL :(char*)regional_strdup(region, reason);
		/* On allocation error we don't store the reason string */
	d->reason_bogus = reason_bogus;
	d->rrset_type = LDNS_RR_TYPE_DNSKEY;
	d->rrset_data = NULL;
	d->algo = NULL;
	return k;
}

struct key_entry_key* 
key_entry_create_rrset(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass,
	struct ub_packed_rrset_key* rrset, uint8_t* sigalg,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now)
{
	struct key_entry_key* k;
	struct key_entry_data* d;
	struct packed_rrset_data* rd = (struct packed_rrset_data*)
		rrset->entry.data;
	if(!key_entry_setup(region, name, namelen, dclass, &k, &d))
		return NULL;
	d->ttl = rd->ttl + now;
	d->isbad = 0;
	d->reason = (!reason || *reason == 0)
		?NULL :(char*)regional_strdup(region, reason);
		/* On allocation error we don't store the reason string */
	d->reason_bogus = reason_bogus;
	d->rrset_type = ntohs(rrset->rk.type);
	d->rrset_data = (struct packed_rrset_data*)regional_alloc_init(region,
		rd, packed_rrset_sizeof(rd));
	if(!d->rrset_data)
		return NULL;
	if(sigalg) {
		d->algo = (uint8_t*)regional_strdup(region, (char*)sigalg);
		if(!d->algo)
			return NULL;
	} else d->algo = NULL;
	packed_rrset_ptr_fixup(d->rrset_data);
	return k;
}

struct key_entry_key* 
key_entry_create_bad(struct regional* region,
	uint8_t* name, size_t namelen, uint16_t dclass, time_t ttl,
	sldns_ede_code reason_bogus, const char* reason,
	time_t now)
{
	struct key_entry_key* k;
	struct key_entry_data* d;
	if(!key_entry_setup(region, name, namelen, dclass, &k, &d))
		return NULL;
	d->ttl = now + ttl;
	d->isbad = 1;
	d->reason = (!reason || *reason == 0)
		?NULL :(char*)regional_strdup(region, reason);
		/* On allocation error we don't store the reason string */
	d->reason_bogus = reason_bogus;
	d->rrset_type = LDNS_RR_TYPE_DNSKEY;
	d->rrset_data = NULL;
	d->algo = NULL;
	return k;
}

struct ub_packed_rrset_key* 
key_entry_get_rrset(struct key_entry_key* kkey, struct regional* region)
{
	struct key_entry_data* d = (struct key_entry_data*)kkey->entry.data;
	struct ub_packed_rrset_key* rrk;
	struct packed_rrset_data* rrd;
	if(!d || !d->rrset_data)
		return NULL;
	rrk = regional_alloc(region, sizeof(*rrk));
	if(!rrk)
		return NULL;
	memset(rrk, 0, sizeof(*rrk));
	rrk->rk.dname = regional_alloc_init(region, kkey->name, kkey->namelen);
	if(!rrk->rk.dname)
		return NULL;
	rrk->rk.dname_len = kkey->namelen;
	rrk->rk.type = htons(d->rrset_type);
	rrk->rk.rrset_class = htons(kkey->key_class);
	rrk->entry.key = rrk;
	rrd = regional_alloc_init(region, d->rrset_data, 
		packed_rrset_sizeof(d->rrset_data));
	if(!rrd)
		return NULL;
	rrk->entry.data = rrd;
	packed_rrset_ptr_fixup(rrd);
	return rrk;
}

/** Get size of key in keyset */
static size_t
dnskey_get_keysize(struct packed_rrset_data* data, size_t idx)
{
	unsigned char* pk;
	unsigned int pklen = 0;
	int algo;
	if(data->rr_len[idx] < 2+5)
		return 0;
	algo = (int)data->rr_data[idx][2+3];
	pk = (unsigned char*)data->rr_data[idx]+2+4;
	pklen = (unsigned)data->rr_len[idx]-2-4;
	return sldns_rr_dnskey_key_size_raw(pk, pklen, algo);
}

/** get dnskey flags from data */
static uint16_t
kd_get_flags(struct packed_rrset_data* data, size_t idx)
{
	uint16_t f;
	if(data->rr_len[idx] < 2+2)
		return 0;
	memmove(&f, data->rr_data[idx]+2, 2);
	f = ntohs(f);
	return f;
}

size_t 
key_entry_keysize(struct key_entry_key* kkey)
{
	struct packed_rrset_data* d;
	/* compute size of smallest ZSK key in the rrset */
	size_t i;
	size_t bits = 0;
	if(!key_entry_isgood(kkey))
		return 0;
	d = ((struct key_entry_data*)kkey->entry.data)->rrset_data;
	for(i=0; i<d->count; i++) {
		if(!(kd_get_flags(d, i) & DNSKEY_BIT_ZSK))
			continue;
		if(i==0 || dnskey_get_keysize(d, i) < bits)
			bits = dnskey_get_keysize(d, i);
	}
	return bits;
}
