/*
 * testcode/unitverify.c - unit test for signature verification routines.
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
 *
 */
/**
 * \file
 * Calls verification unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include "util/log.h"
#include "testcode/unitmain.h"
#include "validator/val_sigcrypt.h"
#include "validator/val_secalgo.h"
#include "validator/val_nsec.h"
#include "validator/val_nsec3.h"
#include "validator/validator.h"
#include "testcode/testpkts.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
#include "util/data/dname.h"
#include "util/regional.h"
#include "util/alloc.h"
#include "util/rbtree.h"
#include "util/net_help.h"
#include "util/module.h"
#include "util/config_file.h"
#include "sldns/sbuffer.h"
#include "sldns/keyraw.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"

/** verbose signature test */
static int vsig = 0;

/** entry to packet buffer with wireformat */
static void
entry_to_buf(struct entry* e, sldns_buffer* pkt)
{
	unit_assert(e->reply_list);
	if(e->reply_list->reply_from_hex) {
		sldns_buffer_copy(pkt, e->reply_list->reply_from_hex);
	} else {
		sldns_buffer_clear(pkt);
		sldns_buffer_write(pkt, e->reply_list->reply_pkt,
			e->reply_list->reply_len);
		sldns_buffer_flip(pkt);
	}
}

/** entry to reply info conversion */
static void
entry_to_repinfo(struct entry* e, struct alloc_cache* alloc, 
	struct regional* region, sldns_buffer* pkt, struct query_info* qi, 
	struct reply_info** rep)
{
	int ret;
	struct edns_data edns;
	entry_to_buf(e, pkt);
	/* lock alloc lock to please lock checking software. 
	 * alloc_special_obtain assumes it is talking to a ub-alloc,
	 * and does not need to perform locking. Here the alloc is
	 * the only one, so we lock it here */
	lock_quick_lock(&alloc->lock);
	ret = reply_info_parse(pkt, alloc, qi, rep, region, &edns);
	lock_quick_unlock(&alloc->lock);
	if(ret != 0) {
		char rcode[16];
		sldns_wire2str_rcode_buf(ret, rcode, sizeof(rcode));
		printf("parse code %d: %s\n", ret, rcode);
		unit_assert(ret != 0);
	}
}

/** extract DNSKEY rrset from answer and convert it */
static struct ub_packed_rrset_key* 
extract_keys(struct entry* e, struct alloc_cache* alloc, 
	struct regional* region, sldns_buffer* pkt)
{
	struct ub_packed_rrset_key* dnskey = NULL;
	struct query_info qinfo;
	struct reply_info* rep = NULL;
	size_t i;

	entry_to_repinfo(e, alloc, region, pkt, &qinfo, &rep);
	for(i=0; i<rep->an_numrrsets; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_DNSKEY) {
			dnskey = rep->rrsets[i];
			rep->rrsets[i] = NULL;
			break;
		}
	}
	unit_assert(dnskey);

	reply_info_parsedelete(rep, alloc);
	query_info_clear(&qinfo);
	return dnskey;
}

/** return true if answer should be bogus */
static int
should_be_bogus(struct ub_packed_rrset_key* rrset, struct query_info* qinfo)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)rrset->
		entry.data;
	if(d->rrsig_count == 0)
		return 1;
	/* name 'bogus' as first label signals bogus */
	if(rrset->rk.dname_len > 6 && memcmp(rrset->rk.dname+1, "bogus", 5)==0)
		return 1;
	if(qinfo->qname_len > 6 && memcmp(qinfo->qname+1, "bogus", 5)==0)
		return 1;
	return 0;
}

/** return number of rrs in an rrset */
static size_t
rrset_get_count(struct ub_packed_rrset_key* rrset)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)
	rrset->entry.data;
	if(!d) return 0;
	return d->count;
}

/** setup sig alg list from dnskey */
static void
setup_sigalg(struct ub_packed_rrset_key* dnskey, uint8_t* sigalg)
{
	uint8_t a[ALGO_NEEDS_MAX];
	size_t i, n = 0;
	memset(a, 0, sizeof(a));
	for(i=0; i<rrset_get_count(dnskey); i++) {
		uint8_t algo = (uint8_t)dnskey_get_algo(dnskey, i);
		if(a[algo] == 0) {
			a[algo] = 1;
			sigalg[n++] = algo;
		}
	}
	sigalg[n] = 0;
}

/** verify and test one rrset against the key rrset */
static void
verifytest_rrset(struct module_env* env, struct val_env* ve, 
	struct ub_packed_rrset_key* rrset, struct ub_packed_rrset_key* dnskey,
	struct query_info* qinfo)
{
	enum sec_status sec;
	char reasonbuf[256];
	char* reason = NULL;
	uint8_t sigalg[ALGO_NEEDS_MAX+1];
	int verified = 0;
	if(vsig) {
		log_nametypeclass(VERB_QUERY, "verify of rrset",
			rrset->rk.dname, ntohs(rrset->rk.type),
			ntohs(rrset->rk.rrset_class));
	}
	setup_sigalg(dnskey, sigalg); /* check all algorithms in the dnskey */
	/* ok to give null as qstate here, won't be used for answer section. */
	sec = dnskeyset_verify_rrset(env, ve, rrset, dnskey, sigalg, &reason,
		NULL, LDNS_SECTION_ANSWER, NULL, &verified, reasonbuf,
		sizeof(reasonbuf));
	if(vsig) {
		printf("verify outcome is: %s %s\n", sec_status_to_string(sec),
			reason?reason:"");
	}
	if(should_be_bogus(rrset, qinfo)) {
		unit_assert(sec == sec_status_bogus);
	} else {
		unit_assert(sec == sec_status_secure);
	}
}

/** verify and test an entry - every rr in the message */
static void
verifytest_entry(struct entry* e, struct alloc_cache* alloc, 
	struct regional* region, sldns_buffer* pkt, 
	struct ub_packed_rrset_key* dnskey, struct module_env* env, 
	struct val_env* ve)
{
	struct query_info qinfo;
	struct reply_info* rep = NULL;
	size_t i;

	regional_free_all(region);
	if(vsig) {
		char* s = sldns_wire2str_pkt(e->reply_list->reply_pkt,
			e->reply_list->reply_len);
		printf("verifying pkt:\n%s\n", s?s:"outofmemory");
		free(s);
	}
	entry_to_repinfo(e, alloc, region, pkt, &qinfo, &rep);

	for(i=0; i<rep->rrset_count; i++) {
		verifytest_rrset(env, ve, rep->rrsets[i], dnskey, &qinfo);
	}

	reply_info_parsedelete(rep, alloc);
	query_info_clear(&qinfo);
}

/** find RRset in reply by type */
static struct ub_packed_rrset_key*
find_rrset_type(struct reply_info* rep, uint16_t type)
{
	size_t i;
	for(i=0; i<rep->rrset_count; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == type)
			return rep->rrsets[i];
	}
	return NULL;
}

/** DS sig test an entry - get DNSKEY and DS in entry and verify */
static void
dstest_entry(struct entry* e, struct alloc_cache* alloc, 
	struct regional* region, sldns_buffer* pkt, struct module_env* env)
{
	struct query_info qinfo;
	struct reply_info* rep = NULL;
	struct ub_packed_rrset_key* ds, *dnskey;
	int ret;

	regional_free_all(region);
	if(vsig) {
		char* s = sldns_wire2str_pkt(e->reply_list->reply_pkt,
			e->reply_list->reply_len);
		printf("verifying DS-DNSKEY match:\n%s\n", s?s:"outofmemory");
		free(s);
	}
	entry_to_repinfo(e, alloc, region, pkt, &qinfo, &rep);
	ds = find_rrset_type(rep, LDNS_RR_TYPE_DS);
	dnskey = find_rrset_type(rep, LDNS_RR_TYPE_DNSKEY);
	/* check test is OK */
	unit_assert(ds && dnskey);

	ret = ds_digest_match_dnskey(env, dnskey, 0, ds, 0);
	if(strncmp((char*)qinfo.qname, "\003yes", 4) == 0) {
		if(vsig) {
			printf("result(yes)= %s\n", ret?"yes":"no");
		}
		unit_assert(ret);
	} else if (strncmp((char*)qinfo.qname, "\002no", 3) == 0) {
		if(vsig) {
			printf("result(no)= %s\n", ret?"yes":"no");
		}
		unit_assert(!ret);
		verbose(VERB_QUERY, "DS fail: OK; matched unit test");
	} else {
		fatal_exit("Bad qname in DS unit test, yes or no");
	}

	reply_info_parsedelete(rep, alloc);
	query_info_clear(&qinfo);
}

/** verify from a file */
static void
verifytest_file(const char* fname, const char* at_date)
{
	/* 
	 * The file contains a list of ldns-testpkts entries.
	 * The first entry must be a query for DNSKEY.
	 * The answer rrset is the keyset that will be used for verification
	 */
	struct ub_packed_rrset_key* dnskey;
	struct regional* region = regional_create();
	struct alloc_cache alloc;
	sldns_buffer* buf = sldns_buffer_new(65535);
	struct entry* e;
	struct entry* list = read_datafile(fname, 1);
	struct module_env env;
	struct val_env ve;
	time_t now = time(NULL);
	unit_show_func("signature verify", fname);

	if(!list)
		fatal_exit("could not read %s: %s", fname, strerror(errno));
	alloc_init(&alloc, NULL, 1);
	memset(&env, 0, sizeof(env));
	memset(&ve, 0, sizeof(ve));
	env.scratch = region;
	env.scratch_buffer = buf;
	env.now = &now;
	ve.date_override = cfg_convert_timeval(at_date);
	unit_assert(region && buf);
	dnskey = extract_keys(list, &alloc, region, buf);
	if(vsig) log_nametypeclass(VERB_QUERY, "test dnskey",
			dnskey->rk.dname, ntohs(dnskey->rk.type), 
			ntohs(dnskey->rk.rrset_class));
	/* ready to go! */
	for(e = list->next; e; e = e->next) {
		verifytest_entry(e, &alloc, region, buf, dnskey, &env, &ve);
	}

	ub_packed_rrset_parsedelete(dnskey, &alloc);
	delete_entry(list);
	regional_destroy(region);
	alloc_clear(&alloc);
	sldns_buffer_free(buf);
}

/** verify DS matches DNSKEY from a file */
static void
dstest_file(const char* fname)
{
	/* 
	 * The file contains a list of ldns-testpkts entries.
	 * The first entry must be a query for DNSKEY.
	 * The answer rrset is the keyset that will be used for verification
	 */
	struct regional* region = regional_create();
	struct alloc_cache alloc;
	sldns_buffer* buf = sldns_buffer_new(65535);
	struct entry* e;
	struct entry* list = read_datafile(fname, 1);
	struct module_env env;
	unit_show_func("DS verify", fname);

	if(!list)
		fatal_exit("could not read %s: %s", fname, strerror(errno));
	alloc_init(&alloc, NULL, 1);
	memset(&env, 0, sizeof(env));
	env.scratch = region;
	env.scratch_buffer = buf;
	unit_assert(region && buf);

	/* ready to go! */
	for(e = list; e; e = e->next) {
		dstest_entry(e, &alloc, region, buf, &env);
	}

	delete_entry(list);
	regional_destroy(region);
	alloc_clear(&alloc);
	sldns_buffer_free(buf);
}

/** helper for unittest of NSEC routines */
static int
unitest_nsec_has_type_rdata(char* bitmap, size_t len, uint16_t type)
{
	return nsecbitmap_has_type_rdata((uint8_t*)bitmap, len, type);
}

/** Test NSEC type bitmap routine */
static void
nsectest(void)
{
	/* bitmap starts at type bitmap rdata field */
	/* from rfc 4034 example */
	char* bitmap = "\000\006\100\001\000\000\000\003"
		"\004\033\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000"
		"\000\000\000\000\040";
	size_t len = 37;

	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 0));
	unit_assert(unitest_nsec_has_type_rdata(bitmap, len, LDNS_RR_TYPE_A));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 2));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 3));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 4));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 5));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 6));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 7));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 8));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 9));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 10));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 11));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 12));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 13));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 14));
	unit_assert(unitest_nsec_has_type_rdata(bitmap, len, LDNS_RR_TYPE_MX));
	unit_assert(unitest_nsec_has_type_rdata(bitmap, len, LDNS_RR_TYPE_RRSIG));
	unit_assert(unitest_nsec_has_type_rdata(bitmap, len, LDNS_RR_TYPE_NSEC));
	unit_assert(unitest_nsec_has_type_rdata(bitmap, len, 1234));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1233));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1235));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1236));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1237));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1238));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1239));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 1240));
	unit_assert(!unitest_nsec_has_type_rdata(bitmap, len, 2230));
}

/** Test hash algo - NSEC3 hash it and compare result */
static void
nsec3_hash_test_entry(struct entry* e, rbtree_type* ct,
	struct alloc_cache* alloc, struct regional* region, 
	sldns_buffer* buf)
{
	struct query_info qinfo;
	struct reply_info* rep = NULL;
	struct ub_packed_rrset_key* answer, *nsec3, *nsec3_region;
	struct nsec3_cached_hash* hash = NULL;
	int ret;
	uint8_t* qname;

	if(vsig) {
		char* s = sldns_wire2str_pkt(e->reply_list->reply_pkt,
			e->reply_list->reply_len);
		printf("verifying NSEC3 hash:\n%s\n", s?s:"outofmemory");
		free(s);
	}
	entry_to_repinfo(e, alloc, region, buf, &qinfo, &rep);
	nsec3 = find_rrset_type(rep, LDNS_RR_TYPE_NSEC3);
	answer = find_rrset_type(rep, LDNS_RR_TYPE_AAAA);
	qname = regional_alloc_init(region, qinfo.qname, qinfo.qname_len);
	/* check test is OK */
	unit_assert(nsec3 && answer && qname);

	/* Copy the nsec3 to the region, so it can stay referenced by the
	 * ct tree entry. The region is freed when the file is done. */
	nsec3_region = packed_rrset_copy_region(nsec3, region, 0);

	ret = nsec3_hash_name(ct, region, buf, nsec3_region, 0, qname,
		qinfo.qname_len, &hash);
	if(ret < 1) {
		printf("Bad nsec3_hash_name retcode %d\n", ret);
		unit_assert(ret == 1 || ret == 2);
	}
	unit_assert(hash->dname && hash->hash && hash->hash_len &&
		hash->b32 && hash->b32_len);
	unit_assert(hash->b32_len == (size_t)answer->rk.dname[0]);
	/* does not do lowercasing. */
	unit_assert(memcmp(hash->b32, answer->rk.dname+1, hash->b32_len) 
		== 0);

	reply_info_parsedelete(rep, alloc);
	query_info_clear(&qinfo);
}


/** Read file to test NSEC3 hash algo */
static void
nsec3_hash_test(const char* fname)
{
	/* 
	 * The list contains a list of ldns-testpkts entries.
	 * Every entry is a test.
	 * 	The qname is hashed.
	 * 	The answer section AAAA RR name is the required result.
	 * 	The auth section NSEC3 is used to get hash parameters.
	 * The hash cache is maintained per file.
	 *
	 * The test does not perform canonicalization during the compare.
	 */
	rbtree_type ct;
	struct regional* region = regional_create();
	struct alloc_cache alloc;
	sldns_buffer* buf = sldns_buffer_new(65535);
	struct entry* e;
	struct entry* list = read_datafile(fname, 1);
	unit_show_func("NSEC3 hash", fname);

	if(!list)
		fatal_exit("could not read %s: %s", fname, strerror(errno));
	rbtree_init(&ct, &nsec3_hash_cmp);
	alloc_init(&alloc, NULL, 1);
	unit_assert(region && buf);

	/* ready to go! */
	for(e = list; e; e = e->next) {
		nsec3_hash_test_entry(e, &ct, &alloc, region, buf);
	}

	delete_entry(list);
	regional_destroy(region);
	alloc_clear(&alloc);
	sldns_buffer_free(buf);
}

#define xstr(s) str(s)
#define str(s) #s

#define SRCDIRSTR xstr(SRCDIR)

void 
verify_test(void)
{
	unit_show_feature("signature verify");
#ifdef USE_SHA1
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.1", "20070818005004");
#endif
#if defined(USE_DSA) && defined(USE_SHA1)
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.2", "20080414005004");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.3", "20080416005004");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.4", "20080416005004");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.5", "20080416005004");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.6", "20080416005004");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.7", "20070829144150");
#endif /* USE_DSA */
#ifdef USE_SHA1
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.8", "20070829144150");
#endif
#if (defined(HAVE_EVP_SHA256) || defined(HAVE_NSS) || defined(HAVE_NETTLE)) && defined(USE_SHA2)
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.rsasha256", "20070829144150");
#  ifdef USE_SHA1
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.sha1_and_256", "20070829144150");
#  endif
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.rsasha256_draft", "20090101000000");
#endif
#if (defined(HAVE_EVP_SHA512) || defined(HAVE_NSS) || defined(HAVE_NETTLE)) && defined(USE_SHA2)
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.rsasha512_draft", "20070829144150");
	verifytest_file(SRCDIRSTR "/testdata/test_signatures.9", "20171215000000");
#endif
#ifdef USE_SHA1
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.hinfo", "20090107100022");
	verifytest_file(SRCDIRSTR "/testdata/test_sigs.revoked", "20080414005004");
#endif
#ifdef USE_GOST
	if(sldns_key_EVP_load_gost_id())
	  verifytest_file(SRCDIRSTR "/testdata/test_sigs.gost", "20090807060504");
	else printf("Warning: skipped GOST, openssl does not provide gost.\n");
#endif
#ifdef USE_ECDSA
	/* test for support in case we use libNSS and ECC is removed */
	if(dnskey_algo_id_is_supported(LDNS_ECDSAP256SHA256)) {
		verifytest_file(SRCDIRSTR "/testdata/test_sigs.ecdsa_p256", "20100908100439");
		verifytest_file(SRCDIRSTR "/testdata/test_sigs.ecdsa_p384", "20100908100439");
	}
	dstest_file(SRCDIRSTR "/testdata/test_ds.sha384");
#endif
#ifdef USE_ED25519
	if(dnskey_algo_id_is_supported(LDNS_ED25519)) {
		verifytest_file(SRCDIRSTR "/testdata/test_sigs.ed25519", "20170530140439");
	}
#endif
#ifdef USE_ED448
	if(dnskey_algo_id_is_supported(LDNS_ED448)) {
		verifytest_file(SRCDIRSTR "/testdata/test_sigs.ed448", "20180408143630");
	}
#endif
#ifdef USE_SHA1
	dstest_file(SRCDIRSTR "/testdata/test_ds.sha1");
#endif
	nsectest();
	nsec3_hash_test(SRCDIRSTR "/testdata/test_nsec3_hash.1");
}
