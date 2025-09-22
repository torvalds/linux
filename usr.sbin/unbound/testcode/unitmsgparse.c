/*
 * testcode/unitmsgparse.c - unit test for msg parse routines.
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
 * Calls msg parse unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include <sys/time.h>
#include "util/log.h"
#include "testcode/unitmain.h"
#include "util/data/msgparse.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "util/data/dname.h"
#include "util/alloc.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "testcode/readhex.h"
#include "testcode/testpkts.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"

/** verbose message parse unit test */
static int vbmp = 0;
/** do not accept formerr */
static int check_formerr_gone = 0;
/** if matching within a section should disregard the order of RRs. */
static int matches_nolocation = 0;
/** see if RRSIGs are properly matched to RRsets. */
static int check_rrsigs = 0;
/** do not check buffer sameness */
static int check_nosameness = 0;

/** see if buffers contain the same packet */
static int
test_buffers(sldns_buffer* pkt, sldns_buffer* out)
{
	/* check binary same */
	if(sldns_buffer_limit(pkt) == sldns_buffer_limit(out) &&
		memcmp(sldns_buffer_begin(pkt), sldns_buffer_begin(out),
			sldns_buffer_limit(pkt)) == 0) {
		if(vbmp) printf("binary the same (length=%u)\n",
				(unsigned)sldns_buffer_limit(pkt));
		return 1;
	}

	if(vbmp) {
		size_t sz = 16;
		size_t count;
		size_t lim = sldns_buffer_limit(out);
		if(sldns_buffer_limit(pkt) < lim)
			lim = sldns_buffer_limit(pkt);
		for(count=0; count<lim; count+=sz) {
			size_t rem = sz;
			if(lim-count < sz) rem = lim-count;
			if(memcmp(sldns_buffer_at(pkt, count), 
				sldns_buffer_at(out, count), rem) == 0) {
				log_info("same %d %d", (int)count, (int)rem);
				log_hex("same: ", sldns_buffer_at(pkt, count),
					rem);
			} else {
				log_info("diff %d %d", (int)count, (int)rem);
				log_hex("difp: ", sldns_buffer_at(pkt, count),
					rem);
				log_hex("difo: ", sldns_buffer_at(out, count),
					rem);
			}
		}
	}

	/* check if it 'means the same' */
	if(vbmp) {
		char* s1, *s2;
		log_buf(0, "orig in hex", pkt);
		log_buf(0, "unbound out in hex", out);
		printf("\npacket from unbound (%d):\n", 
			(int)sldns_buffer_limit(out));
		s1 = sldns_wire2str_pkt(sldns_buffer_begin(out),
			sldns_buffer_limit(out));
		printf("%s\n", s1?s1:"null");
		free(s1);

		printf("\npacket original (%d):\n", 
			(int)sldns_buffer_limit(pkt));
		s2 = sldns_wire2str_pkt(sldns_buffer_begin(pkt),
			sldns_buffer_limit(pkt));
		printf("%s\n", s2?s2:"null");
		free(s2);
		printf("\n");
	}
	/* if it had two EDNS sections, skip comparison */
	if(1) {
		char* s = sldns_wire2str_pkt(sldns_buffer_begin(pkt),
			sldns_buffer_limit(pkt));
		char* e1 = strstr(s, "; EDNS:");
		if(e1 && strstr(e1+4, "; EDNS:")) {
			free(s);
			return 0;
		}
		free(s);
	}
	/* compare packets */
	unit_assert(match_all(sldns_buffer_begin(pkt), sldns_buffer_limit(pkt),
		sldns_buffer_begin(out), sldns_buffer_limit(out), 1,
		matches_nolocation, 0));
	return 0;
}

/** check if unbound formerr equals ldns formerr */
static void
checkformerr(sldns_buffer* pkt)
{
	int status = 0;
	char* s = sldns_wire2str_pkt(sldns_buffer_begin(pkt),
		sldns_buffer_limit(pkt));
	if(!s) fatal_exit("out of memory");
	if(strstr(s, "Error")) status = 1;
	if(strstr(s, "error")) status = 1;
	if(status == 0) {
		printf("Formerr, but ldns gives packet:\n");
		printf("%s\n", s);
		free(s);
		exit(1);
	}
	free(s);
	unit_assert(status != 0);
}

/** performance test message encoding */
static void
perf_encode(struct query_info* qi, struct reply_info* rep, uint16_t id, 
	uint16_t flags, sldns_buffer* out, time_t timenow, 
	struct edns_data* edns)
{
	static int num = 0;
	int ret;
	size_t max = 10000;
	size_t i;
	struct timeval start, end;
	double dt;
	struct regional* r2 = regional_create();
	if(gettimeofday(&start, NULL) < 0)
		fatal_exit("gettimeofday: %s", strerror(errno));
	/* encode a couple times */
	for(i=0; i<max; i++) {
		ret = reply_info_encode(qi, rep, id, flags, out, timenow,
			r2, 65535, (int)(edns->bits & EDNS_DO), 0);
		unit_assert(ret != 0); /* udp packets should fit */
		attach_edns_record(out, edns);
		regional_free_all(r2);
	}
	if(gettimeofday(&end, NULL) < 0)
		fatal_exit("gettimeofday: %s", strerror(errno));
	/* time in millisec */
	dt = (double)(end.tv_sec - start.tv_sec)*1000. + 
		((double)end.tv_usec - (double)start.tv_usec)/1000.;
	printf("[%d] did %u in %g msec for %f encode/sec size %d\n", num++,
		(unsigned)max, dt, (double)max / (dt/1000.), 
		(int)sldns_buffer_limit(out));
	regional_destroy(r2);
}

/** perf test a packet */
static void
perftestpkt(sldns_buffer* pkt, struct alloc_cache* alloc, sldns_buffer* out, 
	const char* hex)
{
	struct query_info qi;
	struct reply_info* rep = 0;
	int ret;
	uint16_t id;
	uint16_t flags;
	time_t timenow = 0;
	struct regional* region = regional_create();
	struct edns_data edns;

	hex_to_buf(pkt, hex);
	memmove(&id, sldns_buffer_begin(pkt), sizeof(id));
	if(sldns_buffer_limit(pkt) < 2)
		flags = 0;
	else	memmove(&flags, sldns_buffer_at(pkt, 2), sizeof(flags));
	flags = ntohs(flags);
	ret = reply_info_parse(pkt, alloc, &qi, &rep, region, &edns);
	if(ret != 0) {
		char rbuf[16];
		sldns_wire2str_rcode_buf(ret, rbuf, sizeof(rbuf));
		if(vbmp) printf("parse code %d: %s\n", ret, rbuf);
		if(ret == LDNS_RCODE_FORMERR)
			checkformerr(pkt);
		unit_assert(ret != LDNS_RCODE_SERVFAIL);
	} else {
		perf_encode(&qi, rep, id, flags, out, timenow, &edns);
	} 

	query_info_clear(&qi);
	reply_info_parsedelete(rep, alloc);
	regional_destroy(region);
}

/** print packed rrset */
static void
print_rrset(struct ub_packed_rrset_key* rrset)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)rrset->
	                entry.data;
	char buf[65535];
	size_t i;
	for(i=0; i<d->count+d->rrsig_count; i++) {
		if(!packed_rr_to_string(rrset, i, 0, buf, sizeof(buf)))
			printf("failedtoconvert %d\n", (int)i);
		else
			printf("%s\n", buf);
	}
}

/** debug print a packet that failed */
static void
print_packet_rrsets(struct query_info* qinfo, struct reply_info* rep)
{
	size_t i;
	log_query_info(0, "failed query", qinfo);
	printf(";; ANSWER SECTION (%d rrsets)\n", (int)rep->an_numrrsets);
	for(i=0; i<rep->an_numrrsets; i++) {
		printf("; rrset %d\n", (int)i);
		print_rrset(rep->rrsets[i]);
	}
	printf(";; AUTHORITY SECTION (%d rrsets)\n", (int)rep->ns_numrrsets);
	for(i=rep->an_numrrsets; i<rep->an_numrrsets+rep->ns_numrrsets; i++) {
		printf("; rrset %d\n", (int)i);
		print_rrset(rep->rrsets[i]);
	}
	printf(";; ADDITIONAL SECTION (%d rrsets)\n", (int)rep->ar_numrrsets);
	for(i=rep->an_numrrsets+rep->ns_numrrsets; i<rep->rrset_count; i++) {
		printf("; rrset %d\n", (int)i);
		print_rrset(rep->rrsets[i]);
	}
	printf(";; packet end\n");
}

/** check that there is no data element that matches the RRSIG */
static int
no_data_for_rrsig(struct reply_info* rep, struct ub_packed_rrset_key* rrsig)
{
	size_t i;
	for(i=0; i<rep->rrset_count; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_RRSIG)
			continue;
		if(query_dname_compare(rep->rrsets[i]->rk.dname, 
			rrsig->rk.dname) == 0)
			/* only name is compared right now */
			return 0;
	}
	return 1;
}

/** check RRSIGs in packet */
static void
check_the_rrsigs(struct query_info* qinfo, struct reply_info* rep)
{
	/* every RRSIG must be matched to an RRset */
	size_t i;
	for(i=0; i<rep->rrset_count; i++) {
		struct ub_packed_rrset_key* s = rep->rrsets[i];
		if(ntohs(s->rk.type) == LDNS_RR_TYPE_RRSIG) {
			/* see if really a problem, i.e. is there a data
			 * element. */
			if(no_data_for_rrsig(rep, rep->rrsets[i]))
				continue;
			log_dns_msg("rrsig failed for packet", qinfo, rep);
			print_packet_rrsets(qinfo, rep);
			printf("failed rrset is nr %d\n", (int)i);
			unit_assert(0);
		}
	}
}

/** test a packet */
static void
testpkt(sldns_buffer* pkt, struct alloc_cache* alloc, sldns_buffer* out, 
	const char* hex)
{
	struct query_info qi;
	struct reply_info* rep = 0;
	int ret;
	uint16_t id;
	uint16_t flags;
	uint32_t timenow = 0;
	struct regional* region = regional_create();
	struct edns_data edns;

	hex_to_buf(pkt, hex);
	memmove(&id, sldns_buffer_begin(pkt), sizeof(id));
	if(sldns_buffer_limit(pkt) < 2)
		flags = 0;
	else	memmove(&flags, sldns_buffer_at(pkt, 2), sizeof(flags));
	flags = ntohs(flags);
	ret = reply_info_parse(pkt, alloc, &qi, &rep, region, &edns);
	if(ret != 0) {
		char rbuf[16];
		sldns_wire2str_rcode_buf(ret, rbuf, sizeof(rbuf));
		if(vbmp) printf("parse code %d: %s\n", ret, rbuf);
		if(ret == LDNS_RCODE_FORMERR) {
			unit_assert(!check_formerr_gone);
			checkformerr(pkt);
		}
		unit_assert(ret != LDNS_RCODE_SERVFAIL);
	} else if(!check_formerr_gone) {
		const size_t lim = 512;
		ret = reply_info_encode(&qi, rep, id, flags, out, timenow,
			region, 65535, (int)(edns.bits & EDNS_DO), 0);
		unit_assert(ret != 0); /* udp packets should fit */
		attach_edns_record(out, &edns);
		if(vbmp) printf("inlen %u outlen %u\n", 
			(unsigned)sldns_buffer_limit(pkt),
			(unsigned)sldns_buffer_limit(out));
		if(!check_nosameness)
			test_buffers(pkt, out);
		if(check_rrsigs)
			check_the_rrsigs(&qi, rep);

		if(sldns_buffer_limit(out) > lim) {
			ret = reply_info_encode(&qi, rep, id, flags, out, 
				timenow, region, 
				lim - calc_edns_field_size(&edns),
				(int)(edns.bits & EDNS_DO), 0);
			unit_assert(ret != 0); /* should fit, but with TC */
			attach_edns_record(out, &edns);
			if( LDNS_QDCOUNT(sldns_buffer_begin(out)) !=
				LDNS_QDCOUNT(sldns_buffer_begin(pkt)) ||
				LDNS_ANCOUNT(sldns_buffer_begin(out)) !=
				LDNS_ANCOUNT(sldns_buffer_begin(pkt)) ||
				LDNS_NSCOUNT(sldns_buffer_begin(out)) !=
				LDNS_NSCOUNT(sldns_buffer_begin(pkt)))
				unit_assert(
				LDNS_TC_WIRE(sldns_buffer_begin(out)));
				/* must set TC bit if shortened */
			unit_assert(sldns_buffer_limit(out) <= lim);
		}
	} 

	query_info_clear(&qi);
	reply_info_parsedelete(rep, alloc);
	regional_destroy(region);
}

/** simple test of parsing */
static void
simpletest(sldns_buffer* pkt, struct alloc_cache* alloc, sldns_buffer* out)
{
	/* a root query  drill -q - */
	testpkt(pkt, alloc, out, 
		" c5 40 01 00 00 01 00 00 00 00 00 00 00 00 02 00 01 ");

	/* very small packet */
	testpkt(pkt, alloc, out, 
"; 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\n"
";-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n"
"74 0c 85 83 00 01 00 00 00 01 00 00 03 62 6c 61 09 6e 6c 6e    ;          1-  20\n"
"65 74 6c 61 62 73 02 6e 6c 00 00 0f 00 01 09 6e 6c 6e 65 74    ;         21-  40\n"
"6c 61 62 73 02 6e 6c 00 00 06 00 01 00 00 46 50 00 40 04 6f    ;         41-  60\n"
"70 65 6e 09 6e 6c 6e 65 74 6c 61 62 73 02 6e 6c 00 0a 68 6f    ;         61-  80\n"
"73 74 6d 61 73 74 65 72 09 6e 6c 6e 65 74 6c 61 62 73 02 6e    ;         81- 100\n"
"6c 00 77 a1 02 58 00 00 70 80 00 00 1c 20 00 09 3a 80 00 00    ;        101- 120\n"
"46 50\n");
	
	/* a root reply  drill -w - */
	testpkt(pkt, alloc, out, 
	" ; 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19\n"
	" ;-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --\n"
	" 97 3f 81 80 00 01 00 0d 00 00 00 02 00 00 02 00 01 00 00 02    ;          1-  20\n"
	" 00 01 00 06 6d 38 00 14 01 49 0c 52 4f 4f 54 2d 53 45 52 56    ;         21-  40\n"
	" 45 52 53 03 4e 45 54 00 00 00 02 00 01 00 06 6d 38 00 14 01    ;         41-  60\n"
	" 4a 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00 00    ;         61-  80\n"
	" 00 02 00 01 00 06 6d 38 00 14 01 4b 0c 52 4f 4f 54 2d 53 45    ;         81- 100\n"
	" 52 56 45 52 53 03 4e 45 54 00 00 00 02 00 01 00 06 6d 38 00    ;        101- 120\n"
	" 14 01 4c 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54    ;        121- 140\n"
	" 00 00 00 02 00 01 00 06 6d 38 00 14 01 4d 0c 52 4f 4f 54 2d    ;        141- 160\n"
	" 53 45 52 56 45 52 53 03 4e 45 54 00 00 00 02 00 01 00 06 6d    ;        161- 180\n"
	" 38 00 14 01 41 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e    ;        181- 200\n"
	" 45 54 00 00 00 02 00 01 00 06 6d 38 00 14 01 42 0c 52 4f 4f    ;        201- 220\n"
	" 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00 00 00 02 00 01 00    ;        221- 240\n"
	" 06 6d 38 00 14 01 43 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53    ;        241- 260\n"
	" 03 4e 45 54 00 00 00 02 00 01 00 06 6d 38 00 14 01 44 0c 52    ;        261- 280\n"
	" 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00 00 00 02 00    ;        281- 300\n"
	" 01 00 06 6d 38 00 14 01 45 0c 52 4f 4f 54 2d 53 45 52 56 45    ;        301- 320\n"
	" 52 53 03 4e 45 54 00 00 00 02 00 01 00 06 6d 38 00 14 01 46    ;        321- 340\n"
	" 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00 00 00    ;        341- 360\n"
	" 02 00 01 00 06 6d 38 00 14 01 47 0c 52 4f 4f 54 2d 53 45 52    ;        361- 380\n"
	" 56 45 52 53 03 4e 45 54 00 00 00 02 00 01 00 06 6d 38 00 14    ;        381- 400\n"
	" 01 48 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00    ;        401- 420\n"
	" 01 41 0c 52 4f 4f 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00    ;        421- 440\n"
	" 00 01 00 01 00 02 64 b9 00 04 c6 29 00 04 01 4a 0c 52 4f 4f    ;        441- 460\n"
	" 54 2d 53 45 52 56 45 52 53 03 4e 45 54 00 00 01 00 01 00 02    ;        461- 480\n"
	" 64 b9 00 04 c0 3a 80 1e  ");

	/* root delegation from unbound trace with new AAAA glue */
	perftestpkt(pkt, alloc, out,
	"55BC84000001000D00000014000002000100000200010007E900001401610C726F6F742D73657276657273036E65740000000200010007E90000040162C01E00000200010007E90000040163C01E00000200010007E90000040164C01E00000200010007E90000040165C01E00000200010007E90000040166C01E00000200010007E90000040167C01E00000200010007E90000040168C01E00000200010007E90000040169C01E00000200010007E9000004016AC01E00000200010007E9000004016BC01E00000200010007E9000004016CC01E00000200010007E9000004016DC01EC01C000100010007E9000004C6290004C03B000100010007E9000004C0E44FC9C04A000100010007E9000004C021040CC059000100010007E900000480080A5AC068000100010007E9000004C0CBE60AC077000100010007E9000004C00505F1C086000100010007E9000004C0702404C095000100010007E9000004803F0235C0A4000100010007E9000004C0249411C0B3000100010007E9000004C03A801EC0C2000100010007E9000004C1000E81C0D1000100010007E9000004C707532AC0E0000100010007E9000004CA0C1B21C01C001C00010007E900001020010503BA3E00000000000000020030C077001C00010007E900001020010500002F0000000000000000000FC095001C00010007E90000102001050000010000"
	"00000000803F0235C0B3001C00010007E9000010200105030C2700000000000000020030C0C2001C00010007E9000010200107FD000000000000000000000001C0E0001C00010007E900001020010DC30000000000000000000000350000291000000000000000"
	);
}

/** simple test of parsing, pcat file */
static void
testfromfile(sldns_buffer* pkt, struct alloc_cache* alloc, sldns_buffer* out,
	const char* fname)
{
	FILE* in = fopen(fname, "r");
	char buf[102400];
	int no=0;
	if(!in) {
		perror("fname");
		return;
	}
	while(fgets(buf, (int)sizeof(buf), in)) {
		if(buf[0] == ';') /* comment */
			continue;
		if(strlen(buf) < 10) /* skip pcat line numbers. */
			continue;
		if(vbmp) {
			printf("test no %d: %s", no, buf);
			fflush(stdout);
		}
		testpkt(pkt, alloc, out, buf);
		no++;
	}
	fclose(in);
}

/** simple test of parsing, drill file */
static void
testfromdrillfile(sldns_buffer* pkt, struct alloc_cache* alloc, 
	sldns_buffer* out, const char* fname)
{
	/*  ;-- is used to indicate a new message */
	FILE* in = fopen(fname, "r");
	char buf[102400];
	char* np = buf;
	buf[0]=0;
	if(!in) {
		perror("fname");
		return;
	}
	while(fgets(np, (int)sizeof(buf) - (np-buf), in)) {
		if(strncmp(np, ";--", 3) == 0) {
			/* new entry */
			/* test previous */
			if(np != buf)
				testpkt(pkt, alloc, out, buf);
			/* set for new entry */
			np = buf;
			buf[0]=0;
			continue;
		}
		if(np[0] == ';') /* comment */
			continue;
		np = &np[strlen(np)];
	}
	testpkt(pkt, alloc, out, buf);
	fclose(in);
}

#define xstr(s) str(s)
#define str(s) #s

#define SRCDIRSTR xstr(SRCDIR)

void msgparse_test(void)
{
	time_t origttl = MAX_NEG_TTL;
	sldns_buffer* pkt = sldns_buffer_new(65553);
	sldns_buffer* out = sldns_buffer_new(65553);
	struct alloc_cache super_a, alloc;
	MAX_NEG_TTL = 86400;
	/* init */
	alloc_init(&super_a, NULL, 0);
	alloc_init(&alloc, &super_a, 2);

	unit_show_feature("message parse");
	simpletest(pkt, &alloc, out);
	/* plain hex dumps, like pcat */
	testfromfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.1");
	testfromfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.2");
	testfromfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.3");
	/* like from drill -w - */
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.4");
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.5");

	matches_nolocation = 1; /* RR order not important for the next test */
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.6");
	check_rrsigs = 1;
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.7");
	check_rrsigs = 0;
	matches_nolocation = 0; 

	check_formerr_gone = 1;
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.8");
	check_formerr_gone = 0;

	check_rrsigs = 1;
	check_nosameness = 1;
	testfromdrillfile(pkt, &alloc, out, SRCDIRSTR "/testdata/test_packets.9");
	check_nosameness = 0;
	check_rrsigs = 0;

	/* cleanup */
	alloc_clear(&alloc);
	alloc_clear(&super_a);
	sldns_buffer_free(pkt);
	sldns_buffer_free(out);
	MAX_NEG_TTL = origttl;
}
