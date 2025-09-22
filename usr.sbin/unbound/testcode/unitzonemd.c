/*
 * testcode/unitzonemd.c - unit test for zonemd.
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
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
 * Unit tests for ZONEMD functionality.
 */

#include "config.h"
#include <ctype.h>
#include "util/log.h"
#include "testcode/unitmain.h"
#include "sldns/str2wire.h"
#include "services/authzone.h"
#include "util/data/dname.h"
#include "util/regional.h"
#include "validator/val_anchor.h"

#define xstr(s) str(s)
#define str(s) #s
#define SRCDIRSTR xstr(SRCDIR)

/** Add zone from file for testing */
struct auth_zone* authtest_addzone(struct auth_zones* az, const char* name,
	char* fname);

/** zonemd unit test, generate a zonemd digest and check if correct */
static void zonemd_generate_test(const char* zname, char* zfile,
	int scheme, int hashalgo, const char* digest)
{
	uint8_t zonemd_hash[512];
	size_t hashlen = 0;
	char output[1024+1];
	size_t i;
	struct auth_zones* az;
	struct auth_zone* z;
	int result;
	struct regional* region = NULL;
	struct sldns_buffer* buf = NULL;
	char* reason = NULL;
	char* digestdup;

	if(!zonemd_hashalgo_supported(hashalgo))
		return; /* cannot test unsupported algo */

	/* setup environment */
	az = auth_zones_create();
	unit_assert(az);
	region = regional_create();
	unit_assert(region);
	buf = sldns_buffer_new(65535);
	unit_assert(buf);

	/* read file */
	z = authtest_addzone(az, zname, zfile);
	unit_assert(z);
	lock_rw_wrlock(&z->lock);
	z->zonemd_check = 1;
	lock_rw_unlock(&z->lock);

	/* create zonemd digest */
	result = auth_zone_generate_zonemd_hash(z, scheme, hashalgo,
		zonemd_hash, sizeof(zonemd_hash), &hashlen, region, buf,
		&reason);
	if(reason) printf("zonemd failure reason: %s\n", reason);
	unit_assert(result);

	/* check digest */
	unit_assert(hashlen*2+1 <= sizeof(output));
	for(i=0; i<hashlen; i++) {
		const char* hexl = "0123456789ABCDEF";
		output[i*2] = hexl[(zonemd_hash[i]&0xf0)>>4];
		output[i*2+1] = hexl[zonemd_hash[i]&0xf];
	}
	output[hashlen*2] = 0;
	digestdup = strdup(digest);
	unit_assert(digestdup);
	for(i=0; i<strlen(digestdup); i++) {
		digestdup[i] = toupper((unsigned char)digestdup[i]);
	}
	if(verbosity >= VERB_ALGO) {
		char zname[LDNS_MAX_DOMAINLEN];
		dname_str(z->name, zname);
		printf("zonemd generated for %s in %s with "
			"scheme=%d hashalgo=%d\n", zname, z->zonefile,
			scheme, hashalgo);
		printf("digest %s\n", output);
		printf("wanted %s\n", digestdup);
	}
	unit_assert(strcmp(output, digestdup) == 0);

	/* delete environment */
	free(digestdup);
	auth_zones_delete(az);
	regional_destroy(region);
	sldns_buffer_free(buf);

	if(verbosity >= VERB_ALGO) {
		printf("\n");
	}
}

/** loop over files and test generated zonemd digest */
static void zonemd_generate_tests(void)
{
	unit_show_func("services/authzone.c", "auth_zone_generate_zonemd_hash");
	zonemd_generate_test("example.org", SRCDIRSTR "/testdata/zonemd.example1.zone",
		1, 2, "20564D10F50A0CEBEC856C64032B7DFB53D3C449A421A5BC7A21F7627B4ACEA4DF29F2C6FE82ED9C23ADF6F4D420D5DD63EF6E6349D60FDAB910B65DF8D481B7");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.1 */
	zonemd_generate_test("example", SRCDIRSTR "/testdata/zonemd.example_a1.zone",
		1, 1, "c68090d90a7aed716bc459f9340e3d7c1370d4d24b7e2fc3a1ddc0b9a87153b9a9713b3c9ae5cc27777f98b8e730044c");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.2 */
	zonemd_generate_test("example", SRCDIRSTR "/testdata/zonemd.example_a2.zone",
		1, 1, "31cefb03814f5062ad12fa951ba0ef5f8da6ae354a415767246f7dc932ceb1e742a2108f529db6a33a11c01493de358d");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.3 SHA384 digest */
	zonemd_generate_test("example", SRCDIRSTR "/testdata/zonemd.example_a3.zone",
		1, 1, "62e6cf51b02e54b9b5f967d547ce43136792901f9f88e637493daaf401c92c279dd10f0edb1c56f8080211f8480ee306");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.3 SHA512 digest*/
	zonemd_generate_test("example", SRCDIRSTR "/testdata/zonemd.example_a3.zone",
		1, 2, "08cfa1115c7b948c4163a901270395ea226a930cd2cbcf2fa9a5e6eb85f37c8a4e114d884e66f176eab121cb02db7d652e0cc4827e7a3204f166b47e5613fd27");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.4 */
	zonemd_generate_test("uri.arpa", SRCDIRSTR "/testdata/zonemd.example_a4.zone",
		1, 1, "1291b78ddf7669b1a39d014d87626b709b55774c5d7d58fadc556439889a10eaf6f11d615900a4f996bd46279514e473");

	/* https://tools.ietf.org/html/draft-ietf-dnsop-dns-zone-digest-12
	 * from section A.5.
	 * Adjusted with renumbered B.root. */
	zonemd_generate_test("root-servers.net", SRCDIRSTR "/testdata/zonemd.example_a5.zone",
		1, 1, "5a9521d88984ee123d9626191e2a327a43a16fd4339dd4ecc13d8672d5bae527d066d33645e35778677800005247d199");
}

/** test the zonemd check routine */
static void zonemd_check_test(void)
{
	const char* zname = "example.org";
	char* zfile = SRCDIRSTR "/testdata/zonemd.example1.zone";
	int scheme = 1;
	int hashalgo = 2;
	const char* digest = "20564D10F50A0CEBEC856C64032B7DFB53D3C449A421A5BC7A21F7627B4ACEA4DF29F2C6FE82ED9C23ADF6F4D420D5DD63EF6E6349D60FDAB910B65DF8D481B7";
	const char* digestwrong = "20564D10F50A0CEBEC856C64032B7DFB53D3C449A421A5BC7A21F7627B4ACEA4DF29F2C6FE82ED9C23ADF6F4D420D5DD63EF6E6349D60FDAB910B65DF8D48100";
	uint8_t hash[512], hashwrong[512];
	size_t hashlen = 0, hashwronglen = 0;
	struct auth_zones* az;
	struct auth_zone* z;
	int result;
	struct regional* region = NULL;
	struct sldns_buffer* buf = NULL;
	char* reason = NULL;

	if(!zonemd_hashalgo_supported(hashalgo))
		return; /* cannot test unsupported algo */
	unit_show_func("services/authzone.c", "auth_zone_generate_zonemd_check");

	/* setup environment */
	az = auth_zones_create();
	unit_assert(az);
	region = regional_create();
	unit_assert(region);
	buf = sldns_buffer_new(65535);
	unit_assert(buf);

	/* read file */
	z = authtest_addzone(az, zname, zfile);
	unit_assert(z);
	lock_rw_wrlock(&z->lock);
	z->zonemd_check = 1;
	lock_rw_unlock(&z->lock);
	hashlen = sizeof(hash);
	if(sldns_str2wire_hex_buf(digest, hash, &hashlen) != 0) {
		unit_assert(0); /* parse failure */
	}
	hashwronglen = sizeof(hashwrong);
	if(sldns_str2wire_hex_buf(digestwrong, hashwrong, &hashwronglen) != 0) {
		unit_assert(0); /* parse failure */
	}

	/* check return values of the check routine */
	result = auth_zone_generate_zonemd_check(z, scheme, hashalgo,
		hash, hashlen, region, buf, &reason);
	unit_assert(result && reason == NULL);
	result = auth_zone_generate_zonemd_check(z, 241, hashalgo,
		hash, hashlen, region, buf, &reason);
	unit_assert(result && strcmp(reason, "unsupported scheme")==0);
	result = auth_zone_generate_zonemd_check(z, scheme, 242,
		hash, hashlen, region, buf, &reason);
	unit_assert(result && strcmp(reason, "unsupported algorithm")==0);
	result = auth_zone_generate_zonemd_check(z, scheme, hashalgo,
		hash, 2, region, buf, &reason);
	unit_assert(!result && strcmp(reason, "digest length too small, less than 12")==0);
	result = auth_zone_generate_zonemd_check(z, scheme, hashalgo,
		hashwrong, hashwronglen, region, buf, &reason);
	unit_assert(!result && strcmp(reason, "incorrect digest")==0);
	result = auth_zone_generate_zonemd_check(z, scheme, hashalgo,
		hashwrong, hashwronglen-3, region, buf, &reason);
	unit_assert(!result && strcmp(reason, "incorrect digest length")==0);

	/* delete environment */
	auth_zones_delete(az);
	regional_destroy(region);
	sldns_buffer_free(buf);

	if(verbosity >= VERB_ALGO) {
		printf("\n");
	}
}

/** zonemd test verify */
static void zonemd_verify_test(char* zname, char* zfile, char* tastr,
	char* date_override, char* result_wanted)
{
	time_t now = 0;
	struct module_stack mods;
	struct module_env env;
	char* result = NULL;
	struct auth_zone* z;

	/* setup test harness */
	memset(&env, 0, sizeof(env));
	env.scratch = regional_create();
	if(!env.scratch)
		fatal_exit("out of memory");
	env.scratch_buffer = sldns_buffer_new(65553);
	if(!env.scratch_buffer)
		fatal_exit("out of memory");
	env.cfg = config_create();
	if(!env.cfg)
		fatal_exit("out of memory");
	env.now = &now;
	env.cfg->val_date_override = cfg_convert_timeval(date_override);
	if(!env.cfg->val_date_override)
		fatal_exit("could not parse datetime %s", date_override);
	if(env.cfg->module_conf)
		free(env.cfg->module_conf);
	env.cfg->module_conf = strdup("validator iterator");
	if(!env.cfg->module_conf)
		fatal_exit("out of memory");
	if(tastr) {
		if(!cfg_strlist_insert(&env.cfg->trust_anchor_list,
			strdup(tastr)))
			fatal_exit("out of memory");
	}
	env.anchors = anchors_create();
	if(!env.anchors)
		fatal_exit("out of memory");
	env.auth_zones = auth_zones_create();
	if(!env.auth_zones)
		fatal_exit("out of memory");
	modstack_init(&mods);
	if(!modstack_call_startup(&mods, env.cfg->module_conf, &env))
		fatal_exit("could not modstack_startup");
	if(!modstack_call_init(&mods, env.cfg->module_conf, &env))
		fatal_exit("could not modstack_call_init");
	env.mesh = mesh_create(&mods, &env);
	if(!env.mesh)
		fatal_exit("out of memory");

	/* load data */
	z = authtest_addzone(env.auth_zones, zname, zfile);
	if(!z)
		fatal_exit("could not addzone %s %s", zname, zfile);

	/* test */
	lock_rw_wrlock(&z->lock);
	z->zonemd_check = 1;
	auth_zone_verify_zonemd(z, &env, &mods, &result, 1, 0);
	lock_rw_unlock(&z->lock);
	if(verbosity >= VERB_ALGO) {
		printf("auth zone %s: ZONEMD verification %s: %s\n", zname,
			(strcmp(result, "ZONEMD verification successful")==0?"successful":"failed"),
			result);
	}
	if(!result)
		fatal_exit("out of memory");
	unit_assert(strcmp(result, result_wanted) == 0);
	if(strcmp(result, "ZONEMD verification successful") == 0 ||
		strcmp(result, "DNSSEC verified nonexistence of ZONEMD") == 0 ||
		strcmp(result, "no ZONEMD present") == 0) {
		lock_rw_rdlock(&z->lock);
		unit_assert(!z->zone_expired);
		lock_rw_unlock(&z->lock);
	} else {
		lock_rw_rdlock(&z->lock);
		unit_assert(z->zone_expired);
		lock_rw_unlock(&z->lock);
	}
	free(result);

	/* desetup test harness */
	mesh_delete(env.mesh);
	modstack_call_deinit(&mods, &env);
	modstack_call_destartup(&mods, &env);
	modstack_free(&mods);
	auth_zones_delete(env.auth_zones);
	anchors_delete(env.anchors);
	config_delete(env.cfg);
	regional_destroy(env.scratch);
	sldns_buffer_free(env.scratch_buffer);

	if(verbosity >= VERB_ALGO) {
		printf("\n");
	}
}

/** zonemd test verify suite */
static void zonemd_verify_tests(void)
{
	unit_show_func("services/authzone.c", "auth_zone_verify_zonemd");
	/* give trustanchor for unsigned zone, should fail */
	zonemd_verify_test("example.org",
		SRCDIRSTR "/testdata/zonemd.example1.zone",
		"example.org. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20180302005009",
		"verify DNSKEY RRset with trust anchor failed: have trust anchor, but zone has no DNSKEY");
	/* unsigned zone without ZONEMD in it */
	zonemd_verify_test("example.org",
		SRCDIRSTR "/testdata/zonemd.example1.zone",
		NULL,
		"20180302005009",
		"no ZONEMD present");
	/* no trust anchor, so it succeeds for zone with a correct ZONEMD */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example2.zone",
		NULL,
		"20180302005009",
		"ZONEMD verification successful");
	/* trust anchor for another zone, so it is indeterminate */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example2.zone",
		"example.org. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20180302005009",
		"ZONEMD verification successful");

	/* load a DNSSEC signed zone, but no trust anchor */
	/* this zonefile has an incorrect ZONEMD digest, with correct
	 * DNSSEC signature. */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example3.zone",
		NULL,
		"20180302005009",
		"incorrect digest");
	/* load a DNSSEC zone with NSEC3, but no trust anchor */
	/* this zonefile has an incorrect ZONEMD digest, with correct
	 * DNSSEC signature. */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example4.zone",
		NULL,
		"20180302005009",
		"incorrect digest");
	/* valid zonemd, in dnssec signed zone, no trust anchor*/
	/* this zonefile has a correct ZONEMD digest and
	 * correct DNSSEC signature */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example5.zone",
		NULL,
		"20180302005009",
		"ZONEMD verification successful");
	/* valid zonemd, in dnssec NSEC3 zone, no trust anchor*/
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example6.zone",
		NULL,
		"20180302005009",
		"ZONEMD verification successful");

	/* load a DNSSEC signed zone with a trust anchor, valid ZONEMD */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example5.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"ZONEMD verification successful");
	/* load a DNSSEC NSEC3 signed zone with a trust anchor, valid ZONEMD */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example6.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"ZONEMD verification successful");

	/* load a DNSSEC NSEC zone without ZONEMD */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example7.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"DNSSEC verified nonexistence of ZONEMD");
	/* load a DNSSEC NSEC3 zone without ZONEMD */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example8.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"DNSSEC verified nonexistence of ZONEMD");

	/* load DNSSEC zone but RRSIG on ZONEMD is wrong */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example9.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
#ifdef HAVE_SSL
		"DNSSEC verify failed for ZONEMD RRset: signature crypto failed"
#else /* HAVE_NETTLE */
		"DNSSEC verify failed for ZONEMD RRset: RSA signature verification failed"
#endif
		);
	/* load DNSSEC zone but RRSIG on SOA is wrong */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example10.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
#ifdef HAVE_SSL
		"DNSSEC verify failed for SOA RRset: signature crypto failed"
#else /* HAVE_NETTLE */
		"DNSSEC verify failed for SOA RRset: RSA signature verification failed"
#endif
		);

	/* load DNSSEC zone without ZONEMD, but NSEC bitmap says it exists */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example11.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"DNSSEC NSEC bitmap says type ZONEMD exists");
	/* load DNSSEC zone without ZONEMD, but NSEC3 bitmap says it exists */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example12.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"DNSSEC NSEC3 bitmap says type ZONEMD exists");

	/* load DNSSEC zone without ZONEMD, but RRSIG on NSEC not okay */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example13.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
#ifdef HAVE_SSL
		"DNSSEC verify failed for NSEC RRset: signature crypto failed"
#else /* HAVE_NETTLE */
		"DNSSEC verify failed for NSEC RRset: RSA signature verification failed"
#endif
		);
	/* load DNSSEC zone without ZONEMD, but RRSIG on NSEC3 not okay */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example14.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
#ifdef HAVE_SSL
		"DNSSEC verify failed for NSEC3 RRset: signature crypto failed"
#else /* HAVE_NETTLE */
		"DNSSEC verify failed for NSEC3 RRset: RSA signature verification failed"
#endif
		);

	/* load DNSSEC zone, with ZONEMD, but DNSKEY RRSIG is not okay. */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example15.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
#ifdef HAVE_SSL
		"verify DNSKEY RRset with trust anchor failed: signature crypto failed"
#else /* HAVE_NETTLE */
		"verify DNSKEY RRset with trust anchor failed: RSA signature verification failed"
#endif
		);
	/* load DNSSEC zone, but trust anchor mismatches DNSKEY */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example5.zone",
		/* okay anchor is
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af", */
		"example.com. IN DS 55566 8 2 0000000000111111222223333444444dfcf92595148022f2c2fd98e5deee90af",
		"20201020135527",
		"verify DNSKEY RRset with trust anchor failed: DS hash mismatches key");
	/* load DNSSEC zone, but trust anchor fails because the zone
	 * has expired signatures.  We set the date for it */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example5.zone",
		"example.com. IN DS 55566 8 2 9c148338951ce1c3b5cd3da532f3d90dfcf92595148022f2c2fd98e5deee90af",
		/* okay date: "20201020135527", */
		"20221020135527",
		"verify DNSKEY RRset with trust anchor failed: signature expired");

	/* duplicate zonemd with same scheme and algorithm */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example16.zone",
		NULL,
		"20180302005009",
		"ZONEMD RRSet contains more than one RR with the same scheme and hash algorithm");
	/* different capitalisation of ns name and owner names, should
	 * be canonicalized. */
	zonemd_verify_test("example.com",
		SRCDIRSTR "/testdata/zonemd.example17.zone",
		NULL,
		"20180302005009",
		"ZONEMD verification successful");
}

/** zonemd unit tests */
void zonemd_test(void)
{
	unit_show_feature("zonemd");
	zonemd_generate_tests();
	zonemd_check_test();
	zonemd_verify_tests();
}
