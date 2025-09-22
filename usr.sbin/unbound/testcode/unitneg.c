/*
 * testcode/unitneg.c - unit test for negative cache routines.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 *
 */
/**
 * \file
 * Calls negative cache unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "testcode/unitmain.h"
#include "validator/val_neg.h"
#include "sldns/rrdef.h"

/** verbose unit test for negative cache */
static int negverbose = 0;

/** debug printout of neg cache */
static void print_neg_cache(struct val_neg_cache* neg)
{
	char buf[LDNS_MAX_DOMAINLEN];
	struct val_neg_zone* z;
	struct val_neg_data* d;
	printf("neg_cache print\n");
	printf("memuse %d of %d\n", (int)neg->use, (int)neg->max);
	printf("maxiter %d\n", (int)neg->nsec3_max_iter);
	printf("%d zones\n", (int)neg->tree.count);
	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		dname_str(z->name, buf);
		printf("%24s", buf);
		printf(" len=%2.2d labs=%d inuse=%d count=%d tree.count=%d\n",
			(int)z->len, z->labs, (int)z->in_use, z->count,
			(int)z->tree.count);
	}
	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		printf("\n");
		dname_print(stdout, NULL, z->name);
		printf(" zone details\n");
		printf("len=%2.2d labs=%d inuse=%d count=%d tree.count=%d\n",
			(int)z->len, z->labs, (int)z->in_use, z->count,
			(int)z->tree.count);
		if(z->parent) {
			printf("parent=");
			dname_print(stdout, NULL, z->parent->name);
			printf("\n");
		} else {
			printf("parent=NULL\n");
		}

		RBTREE_FOR(d, struct val_neg_data*, &z->tree) {
			dname_str(d->name, buf);
			printf("%24s", buf);
			printf(" len=%2.2d labs=%d inuse=%d count=%d\n",
				(int)d->len, d->labs, (int)d->in_use, d->count);
		}
	}
}

/** get static pointer to random zone name */
static char* get_random_zone(void)
{
	static char zname[36];
	int labels = random() % 3;
	int i;
	char* p = zname;
	int labnum;

	for(i=0; i<labels; i++) {
		labnum = random()%10;
		snprintf(p, sizeof(zname)-(p-zname), "\003%3.3d", labnum);
		p+=4;
	}
	snprintf(p, sizeof(zname)-(p-zname), "\007example\003com");
	return zname;
}

/** get static pointer to random data names from and to */
static void get_random_data(char** fromp, char** top, char* zname)
{
	static char buf1[256], buf2[256];
	int type;
	int lab1, lab2;
	int labnum1[10], labnum2[10];
	int i;
	char* p;
	memset(labnum1, 0, sizeof(int)*10);
	memset(labnum2, 0, sizeof(int)*10);

	*fromp = buf1;
	*top = buf2;
	type = random()%10;

	if(type == 0) {
		/* ENT */
		lab1 = random() %3 + 1;
		lab2 = lab1 + random()%3 + 1;
		for(i=0; i<lab1; i++) {
			labnum1[i] = random()%100;
			labnum2[i] = labnum1[i];
		}
		for(i=lab1; i<lab2; i++) {
			labnum2[i] = random()%100;
		}
	} else if(type == 1) {
		/* end of zone */
		lab2 = 0;
		lab1 = random()%3 + 1;
		for(i=0; i<lab1; i++) {
			labnum1[i] = random()%100;
		}
	} else if(type == 2) {
		/* start of zone */
		lab1 = 0;
		lab2 = random()%3 + 1;
		for(i=0; i<lab2; i++) {
			labnum2[i] = random()%100;
		}
	} else {
		/* normal item */
		int common = random()%3;
		lab1 = random() %3 + 1;
		lab2 = random() %3 + 1;
		for(i=0; i<common; i++) {
			labnum1[i] = random()%100;
			labnum2[i] = labnum1[i];
		}
		labnum1[common] = random()%100;
		labnum2[common] = labnum1[common] + random()%20;
		for(i=common; i<lab1; i++)
			labnum1[i] = random()%100;
		for(i=common; i<lab2; i++)
			labnum2[i] = random()%100;
	} 

	/* construct first */
	p = buf1;
	for(i=0; i<lab1; i++) {
		snprintf(p, 256-(p-buf1), "\003%3.3d", labnum1[i]);
		p+=4;
	}
	snprintf(p, 256-(p-buf1), "%s", zname);

	/* construct 2nd */
	p = buf2+2;
	for(i=0; i<lab2; i++) {
		snprintf(p, 256-(p-buf2)-3, "\003%3.3d", labnum2[i]);
		p+=4;
	}
	snprintf(p, 256-(p-buf2)-3, "%s", zname);
	buf2[0] = (char)(strlen(buf2+2)+1);
	buf2[1] = 0;

	if(negverbose) {
		log_nametypeclass(0, "add from", (uint8_t*)buf1, 0, 0);
		log_nametypeclass(0, "add to  ", (uint8_t*)buf2+2, 0, 0);
	}
}

/** add a random item */
static void add_item(struct val_neg_cache* neg)
{
	struct val_neg_zone* z;
	struct packed_rrset_data rd;
	struct ub_packed_rrset_key nsec;
	size_t rr_len;
	time_t rr_ttl;
	uint8_t* rr_data;
	char* zname = get_random_zone();
	char* from, *to;

	lock_basic_lock(&neg->lock);
	if(negverbose)
		log_nametypeclass(0, "add to zone", (uint8_t*)zname, 0, 0);
	z = neg_find_zone(neg, (uint8_t*)zname, strlen(zname)+1, 
		LDNS_RR_CLASS_IN);
	if(!z) {
		z = neg_create_zone(neg,  (uint8_t*)zname, strlen(zname)+1,
		                LDNS_RR_CLASS_IN);
	}
	unit_assert(z);
	val_neg_zone_take_inuse(z);

	/* construct random NSEC item */
	get_random_data(&from, &to, zname);

	/* create nsec and insert it */
	memset(&rd, 0, sizeof(rd));
	memset(&nsec, 0, sizeof(nsec));
	nsec.rk.dname = (uint8_t*)from;
	nsec.rk.dname_len = strlen(from)+1;
	nsec.rk.type = htons(LDNS_RR_TYPE_NSEC);
	nsec.rk.rrset_class = htons(LDNS_RR_CLASS_IN);
	nsec.entry.data = &rd;
	rd.security = sec_status_secure;
	rd.count = 1;
	rd.rr_len = &rr_len;
	rr_len = 19;
	rd.rr_ttl = &rr_ttl;
	rr_ttl = 0;
	rd.rr_data = &rr_data;
	rr_data = (uint8_t*)to;

	neg_insert_data(neg, z, &nsec);
	lock_basic_unlock(&neg->lock);
}

/** remove a random item */
static void remove_item(struct val_neg_cache* neg)
{
	int n, i;
	struct val_neg_data* d;
	rbnode_type* walk;
	struct val_neg_zone* z;
	
	lock_basic_lock(&neg->lock);
	if(neg->tree.count == 0) {
		lock_basic_unlock(&neg->lock);
		return; /* nothing to delete */
	}

	/* pick a random zone */
	walk = rbtree_first(&neg->tree); /* first highest parent, big count */
	z = (struct val_neg_zone*)walk;
	n = random() % (int)(z->count);
	if(negverbose)
		printf("neg stress delete zone %d\n", n);
	i=0;
	walk = rbtree_first(&neg->tree);
	z = (struct val_neg_zone*)walk;
	while(i!=n+1 && walk && walk != RBTREE_NULL && !z->in_use) {
		walk = rbtree_next(walk);
		z = (struct val_neg_zone*)walk;
		if(z->in_use)
			i++;
	}
	if(!walk || walk == RBTREE_NULL) {
		lock_basic_unlock(&neg->lock);
		return;
	}
	if(!z->in_use) {
		lock_basic_unlock(&neg->lock);
		return;
	}
	if(negverbose)
		log_nametypeclass(0, "delete zone", z->name, 0, 0);

	/* pick a random nsec item. - that is in use */
	walk = rbtree_first(&z->tree); /* first is highest parent */
	d = (struct val_neg_data*)walk;
	n = random() % (int)(d->count);
	if(negverbose)
		printf("neg stress delete item %d\n", n);
	i=0;
	walk = rbtree_first(&z->tree);
	d = (struct val_neg_data*)walk;
	while(i!=n+1 && walk && walk != RBTREE_NULL && !d->in_use) {
		walk = rbtree_next(walk);
		d = (struct val_neg_data*)walk;
		if(d->in_use)
			i++;
	}
	if(!walk || walk == RBTREE_NULL) {
		lock_basic_unlock(&neg->lock);
		return;
	}
	if(d->in_use) {
		if(negverbose)
			log_nametypeclass(0, "neg delete item:", d->name, 0, 0);
		neg_delete_data(neg, d);
	}
	lock_basic_unlock(&neg->lock);
}

/** sum up the zone trees */
static size_t sumtrees_all(struct val_neg_cache* neg)
{
	size_t res = 0;
	struct val_neg_zone* z;
	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		res += z->tree.count;
	}
	return res;
}

/** sum up the zone trees, in_use only */
static size_t sumtrees_inuse(struct val_neg_cache* neg)
{
	size_t res = 0;
	struct val_neg_zone* z;
	struct val_neg_data* d;
	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		/* get count of highest parent for num in use */
		d = (struct val_neg_data*)rbtree_first(&z->tree);
		if(d && (rbnode_type*)d!=RBTREE_NULL)
			res += d->count;
	}
	return res;
}

/** check if lru is still valid */
static void check_lru(struct val_neg_cache* neg)
{
	struct val_neg_data* p, *np;
	size_t num = 0;
	size_t inuse;
	p = neg->first;
	while(p) {
		if(!p->prev) {
			unit_assert(neg->first == p);
		}
		np = p->next;
		if(np) {
			unit_assert(np->prev == p);
		} else {
			unit_assert(neg->last == p);
		}
		num++;
		p = np;
	}
	inuse = sumtrees_inuse(neg);
	if(negverbose)
		printf("num lru %d, inuse %d, all %d\n",
			(int)num, (int)sumtrees_inuse(neg), 
			(int)sumtrees_all(neg));
	unit_assert( num == inuse);
	unit_assert( inuse <= sumtrees_all(neg));
}

/** sum up number of items inuse in subtree */
static int sum_subtree_inuse(struct val_neg_zone* zone, 
	struct val_neg_data* data)
{
	struct val_neg_data* d;
	int num = 0;
	RBTREE_FOR(d, struct val_neg_data*, &zone->tree) {
		if(dname_subdomain_c(d->name, data->name)) {
			if(d->in_use)
				num++;
		}
	}
	return num;
}

/** sum up number of items inuse in subtree */
static int sum_zone_subtree_inuse(struct val_neg_cache* neg,
	struct val_neg_zone* zone)
{
	struct val_neg_zone* z;
	int num = 0;
	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		if(dname_subdomain_c(z->name, zone->name)) {
			if(z->in_use)
				num++;
		}
	}
	return num;
}

/** check point in data tree */
static void check_data(struct val_neg_zone* zone, struct val_neg_data* data)
{
	unit_assert(data->count > 0);
	if(data->parent) {
		unit_assert(data->parent->count >= data->count);
		if(data->parent->in_use) {
			unit_assert(data->parent->count > data->count);
		}
		unit_assert(data->parent->labs == data->labs-1);
		/* and parent must be one label shorter */
		unit_assert(data->name[0] == (data->len-data->parent->len-1));
		unit_assert(query_dname_compare(data->name + data->name[0]+1,
			data->parent->name) == 0);
	} else {
		/* must be apex */
		unit_assert(dname_is_root(data->name));
	}
	/* tree property: */
	unit_assert(data->count == sum_subtree_inuse(zone, data));
}

/** check if tree of data in zone is valid */
static void checkzonetree(struct val_neg_zone* zone)
{
	struct val_neg_data* d;

	/* check all data in tree */
	RBTREE_FOR(d, struct val_neg_data*, &zone->tree) {
		check_data(zone, d);
	}
}

/** check if negative cache is still valid */
static void check_zone_invariants(struct val_neg_cache* neg, 
	struct val_neg_zone* zone)
{
	unit_assert(zone->nsec3_hash == 0);
	unit_assert(zone->tree.cmp == &val_neg_data_compare);
	unit_assert(zone->count != 0);

	if(zone->tree.count == 0)
		unit_assert(!zone->in_use);
	else {
		if(!zone->in_use) {
			/* details on error */
			log_nametypeclass(0, "zone", zone->name, 0, 0);
			log_err("inuse %d count=%d tree.count=%d",
				zone->in_use, zone->count, 
				(int)zone->tree.count);
			if(negverbose)
				print_neg_cache(neg);
		}
		unit_assert(zone->in_use);
	}
	
	if(zone->parent) {
		unit_assert(zone->parent->count >= zone->count);
		if(zone->parent->in_use) {
			unit_assert(zone->parent->count > zone->count);
		}
		unit_assert(zone->parent->labs == zone->labs-1);
		/* and parent must be one label shorter */
		unit_assert(zone->name[0] == (zone->len-zone->parent->len-1));
		unit_assert(query_dname_compare(zone->name + zone->name[0]+1,
			zone->parent->name) == 0);
	} else {
		/* must be apex */
		unit_assert(dname_is_root(zone->name));
	}
	/* tree property: */
	unit_assert(zone->count == sum_zone_subtree_inuse(neg, zone));

	/* check structure of zone data tree */
	checkzonetree(zone);
}

/** check if negative cache is still valid */
static void check_neg_invariants(struct val_neg_cache* neg)
{
	struct val_neg_zone* z;
	/* check structure of LRU list */
	lock_basic_lock(&neg->lock);
	check_lru(neg);
	unit_assert(neg->max == 1024*1024);
	unit_assert(neg->nsec3_max_iter == 1500);
	unit_assert(neg->tree.cmp == &val_neg_zone_compare);

	if(neg->tree.count == 0) {
		/* empty */
		unit_assert(neg->tree.count == 0);
		unit_assert(neg->first == NULL);
		unit_assert(neg->last == NULL);
		unit_assert(neg->use == 0);
		lock_basic_unlock(&neg->lock);
		return;
	}

	unit_assert(neg->first != NULL);
	unit_assert(neg->last != NULL);

	RBTREE_FOR(z, struct val_neg_zone*, &neg->tree) {
		check_zone_invariants(neg, z);
	}
	lock_basic_unlock(&neg->lock);
}

/** perform stress test on insert and delete in neg cache */
static void stress_test(struct val_neg_cache* neg)
{
	int i;
	if(negverbose)
		printf("negcache test\n");
	for(i=0; i<100; i++) {
		if(random() % 10 < 8)
			add_item(neg);
		else	remove_item(neg);
		check_neg_invariants(neg);
	}
	/* empty it */
	if(negverbose)
		printf("neg stress empty\n");
	while(neg->first) {
		remove_item(neg);
		check_neg_invariants(neg);
	}
	if(negverbose)
		printf("neg stress emptied\n");
	unit_assert(neg->first == NULL);
	/* insert again */
	for(i=0; i<100; i++) {
		if(random() % 10 < 8)
			add_item(neg);
		else	remove_item(neg);
		check_neg_invariants(neg);
	}
}

void neg_test(void)
{
	struct val_neg_cache* neg;
	srandom(48);
	unit_show_feature("negative cache");

	/* create with defaults */
	neg = val_neg_create(NULL, 1500);
	unit_assert(neg);
	
	stress_test(neg);

	neg_cache_delete(neg);
}
