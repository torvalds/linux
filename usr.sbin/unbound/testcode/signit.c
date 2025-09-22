/*
 * testcode/signit.c - debug tool to sign rrsets with given keys.
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
 * This program signs rrsets with the given keys. It can be used to 
 * construct input to test the validator with.
 */
#include "config.h"
#include <ldns/ldns.h>
#include <assert.h>

#define DNSKEY_BIT_ZSK 0x0100

/**
 * Key settings
 */
struct keysets {
	/** signature inception */
	uint32_t incep;
	/** signature expiration */
	uint32_t expi;
	/** owner name */
	char* owner;
	/** keytag */
	uint16_t keytag;
	/** DNSKEY flags */
	uint16_t flags;
};

/** print usage and exit */
static void
usage(void)
{
	printf("usage:	signit expi ince keytag owner keyfile\n");
	printf("present rrset data on stdin.\n");
	printf("signed data is printed to stdout.\n");
	printf("\n");
	printf("Or use:	signit NSEC3PARAM hash flags iter salt\n");
	printf("present names on stdin, hashed names are printed to stdout.\n");
	exit(1);
}

static time_t 
convert_timeval(const char* str)
{
	time_t t;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	if(strlen(str) < 14)
		return 0;
	if(sscanf(str, "%4d%2d%2d%2d%2d%2d", &tm.tm_year, &tm.tm_mon, 
		&tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
		return 0;
	tm.tm_year -= 1900;
	tm.tm_mon--;
	/* Check values */
	if (tm.tm_year < 70)	return 0;
	if (tm.tm_mon < 0 || tm.tm_mon > 11)	return 0;
	if (tm.tm_mday < 1 || tm.tm_mday > 31) 	return 0;
	if (tm.tm_hour < 0 || tm.tm_hour > 23)	return 0;
	if (tm.tm_min < 0 || tm.tm_min > 59)	return 0;
	if (tm.tm_sec < 0 || tm.tm_sec > 59)	return 0;
	/* call ldns conversion function */
	t = ldns_mktime_from_utc(&tm);
	return t;
}

static void fatal_exit(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	printf("fatal exit: ");
	vprintf(format, args);
	va_end(args);
	exit(1);
}

/** read expi ince keytag owner from cmdline */
static void
parse_cmdline(char *argv[], struct keysets* s)
{
	s->expi = convert_timeval(argv[1]);
	s->incep = convert_timeval(argv[2]);
	s->keytag = (uint16_t)atoi(argv[3]);
	s->owner = argv[4];
	s->flags = DNSKEY_BIT_ZSK; /* to enforce signing */
}

/** read all key files, exit on error */
static ldns_key_list*
read_keys(int num, char* names[], struct keysets* set)
{
	int i;
	ldns_key_list* keys = ldns_key_list_new();
	ldns_key* k;
	ldns_rdf* rdf;
	ldns_status s;
	int b;
	FILE* in;

	if(!keys) fatal_exit("alloc failure");
	for(i=0; i<num; i++) {
		printf("read keyfile %s\n", names[i]);
		in = fopen(names[i], "r");
		if(!in) fatal_exit("could not open %s: %s", names[i],
				strerror(errno));
		s = ldns_key_new_frm_fp(&k, in);
		fclose(in);
		if(s != LDNS_STATUS_OK)
			fatal_exit("bad keyfile %s: %s", names[i],
				ldns_get_errorstr_by_id(s));
		ldns_key_set_expiration(k, set->expi);
		ldns_key_set_inception(k, set->incep);
		s = ldns_str2rdf_dname(&rdf, set->owner);
		if(s != LDNS_STATUS_OK)
			fatal_exit("bad owner name %s: %s", set->owner,
				ldns_get_errorstr_by_id(s));
		ldns_key_set_pubkey_owner(k, rdf);
		ldns_key_set_flags(k, set->flags);
		ldns_key_set_keytag(k, set->keytag);
		b = ldns_key_list_push_key(keys, k);
		assert(b);
	}
	return keys;
}

/** read list of rrs from the file */
static ldns_rr_list*
read_rrs(FILE* in)
{
	uint32_t my_ttl = 3600;
	ldns_rdf *my_origin = NULL;
	ldns_rdf *my_prev = NULL;
	ldns_status s;
	int line_nr = 1;
	int b;

	ldns_rr_list* list;
	ldns_rr *rr;

	list = ldns_rr_list_new();
	if(!list) fatal_exit("alloc error");

	while(!feof(in)) {
		s = ldns_rr_new_frm_fp_l(&rr, in, &my_ttl, &my_origin,
			&my_prev, &line_nr);
		if(s == LDNS_STATUS_SYNTAX_TTL || 
			s == LDNS_STATUS_SYNTAX_ORIGIN ||
			s == LDNS_STATUS_SYNTAX_EMPTY)
			continue;
		else if(s != LDNS_STATUS_OK)
			fatal_exit("parse error in line %d: %s", line_nr,
				ldns_get_errorstr_by_id(s));
		b = ldns_rr_list_push_rr(list, rr);
		assert(b);
	}
	printf("read %d lines\n", line_nr);

	return list;
}

/** sign the rrs with the keys */
static void
signit(ldns_rr_list* rrs, ldns_key_list* keys)
{
	ldns_rr_list* rrset;
	ldns_rr_list* sigs;
	
	while(ldns_rr_list_rr_count(rrs) > 0) {
		rrset = ldns_rr_list_pop_rrset(rrs);
		if(!rrset) fatal_exit("copy alloc failure");
		sigs = ldns_sign_public(rrset, keys);
		if(!sigs) fatal_exit("failed to sign");
		ldns_rr_list_print(stdout, rrset);
		ldns_rr_list_print(stdout, sigs);
		printf("\n");
		ldns_rr_list_free(rrset);
		ldns_rr_list_free(sigs);
	}
}

/** process keys and signit */
static void
process_keys(int argc, char* argv[])
{
	ldns_rr_list* rrs;
	ldns_key_list* keys;
	struct keysets settings;
	assert(argc == 6);

	parse_cmdline(argv, &settings);
	keys = read_keys(1, argv+5, &settings);
	rrs = read_rrs(stdin);
	signit(rrs, keys);

	ldns_rr_list_deep_free(rrs);
	ldns_key_list_free(keys);
}

/** process nsec3 params and perform hashing */
static void
process_nsec3(int argc, char* argv[])
{
	char line[10240];
	ldns_rdf* salt;
	ldns_rdf* in, *out;
	ldns_status status;
	status = ldns_str2rdf_nsec3_salt(&salt, argv[5]);
	if(status != LDNS_STATUS_OK)
		fatal_exit("Could not parse salt %s: %s", argv[5],
			ldns_get_errorstr_by_id(status));
	assert(argc == 6);
	while(fgets(line, (int)sizeof(line), stdin)) {
		if(strlen(line) > 0)
			line[strlen(line)-1] = 0; /* remove trailing newline */
		if(line[0]==0)
			continue;
		status = ldns_str2rdf_dname(&in, line);
		if(status != LDNS_STATUS_OK)
			fatal_exit("Could not parse name %s: %s", line,
				ldns_get_errorstr_by_id(status));
		ldns_rdf_print(stdout, in);
		printf(" -> ");
		/* arg 3 is flags, unused */
		out = ldns_nsec3_hash_name(in, (uint8_t)atoi(argv[2]), 
			(uint16_t)atoi(argv[4]),
			ldns_rdf_data(salt)[0], ldns_rdf_data(salt)+1);
		if(!out)
			fatal_exit("Could not hash %s", line);
		ldns_rdf_print(stdout, out);
		printf("\n");
		ldns_rdf_deep_free(in);
		ldns_rdf_deep_free(out);
	}
	ldns_rdf_deep_free(salt);
}

/** main program */
int main(int argc, char* argv[])
{
	if(argc != 6) {
		usage();
	}
	if(strcmp(argv[1], "NSEC3PARAM") == 0) {
		process_nsec3(argc, argv);
		return 0;
	}
	process_keys(argc, argv);
	return 0;
}
