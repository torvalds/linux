/*
 * testcode/unitldns.c - unit test for ldns routines.
 *
 * Copyright (c) 2014, NLnet Labs. All rights reserved.
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
 * Calls ldns unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include "util/log.h"
#include "testcode/unitmain.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"
#include "sldns/parseutil.h"

/** verbose this unit test */
static int vbmp = 0;

/** print buffer to hex into string */
static void
buf_to_hex(uint8_t* b, size_t blen, char* s, size_t slen)
{
	const char* h = "0123456789ABCDEF";
	size_t i;
	if(slen < blen*2+2 && vbmp) printf("hexstring buffer too small\n");
	unit_assert(slen >= blen*2+2);
	for(i=0; i<blen; i++) {
		s[i*2] = h[(b[i]&0xf0)>>4];
		s[i*2+1] = h[b[i]&0x0f];
	}
	s[blen*2] = '\n';
	s[blen*2+1] = 0;
}

/** Transform input.
 * @param txt_in: input text format.
 * @param wire1: output wireformat in hex (txt_in converted to wire).
 * @param txt_out: output text format (converted from wire_out).
 * @param wire2: output wireformat in hex, txt_out converted back to wireformat.
 * @param bufs: size of the text buffers.
 */
static void
rr_transform(char* txt_in, char* wire1, char* txt_out, char* wire2, 
	size_t bufs)
{
	uint8_t b[65536];
	size_t len;
	int err;

	len = sizeof(b);
	err = sldns_str2wire_rr_buf(txt_in, b, &len, NULL, 3600,
		NULL, 0, NULL, 0);
	if(err != 0) {
		if(vbmp) printf("sldns_str2wire_rr_buf, pos %d: %s\n",
			LDNS_WIREPARSE_OFFSET(err),
			sldns_get_errorstr_parse(err));
	}
	unit_assert(err == 0);
	buf_to_hex(b, len, wire1, bufs);
	if(vbmp) printf("wire1: %s", wire1);

	err = sldns_wire2str_rr_buf(b, len, txt_out, bufs);
	unit_assert(err < (int)bufs && err > 0);
	if(vbmp) printf("txt: %s", txt_out);

	len = sizeof(b);
	err = sldns_str2wire_rr_buf(txt_out, b, &len, NULL, 3600,
		NULL, 0, NULL, 0);
	if(err != 0) {
		if(vbmp) printf("sldns_str2wire_rr_buf-2, pos %d: %s\n",
			LDNS_WIREPARSE_OFFSET(err),
			sldns_get_errorstr_parse(err));
	}
	unit_assert(err == 0);
	buf_to_hex(b, len, wire2, bufs);
	if(vbmp) printf("wire2: %s", wire2);
}

/** Check if results are correct */
static void
rr_checks(char* wire_chk, char* txt_chk, char* txt_out, char* wire_out,
	char* back)
{
#ifdef __APPLE__
	/* the wiretostr on ipv6 is weird on apple, we cannot check it.
	 * skip AAAA on OSX */
	if(strstr(txt_out, "IN	AAAA"))
		txt_out = txt_chk; /* skip this test, but test wirefmt */
			/* so we know that txt_out back to wire is the same */
#endif

	if(strcmp(txt_chk, txt_out) != 0 && vbmp)
		printf("txt different\n");
	if(strcmp(wire_chk, wire_out) != 0 && vbmp)
		printf("wire1 different\n");
	if(strcmp(wire_chk, back) != 0 && vbmp)
		printf("wire2 different\n");

	unit_assert(strcmp(txt_chk, txt_out) == 0);
	unit_assert(strcmp(wire_chk, wire_out) == 0);
	unit_assert(strcmp(wire_chk, back) == 0);
}

/** read rrs to and from string, and wireformat
 * Skips empty lines and comments.
 * @param input: input file with text format.
 * @param check: check file with hex and then textformat
 */
static void
rr_test_file(const char* input, const char* check)
{
	size_t bufs = 131072;
	FILE* inf, *chf, *of;
	int lineno = 0, chlineno = 0;
	char* txt_in = (char*)malloc(bufs);
	char* txt_out = (char*)malloc(bufs);
	char* txt_chk = (char*)malloc(bufs);
	char* wire_out = (char*)malloc(bufs);
	char* wire_chk = (char*)malloc(bufs);
	char* back = (char*)malloc(bufs);
	if(!txt_in || !txt_out || !txt_chk || !wire_out || !wire_chk || !back)
		fatal_exit("malloc failure");
	inf = fopen(input, "r");
	if(!inf) fatal_exit("cannot open %s: %s", input, strerror(errno));
	chf = fopen(check, "r");
	if(!chf) fatal_exit("cannot open %s: %s", check, strerror(errno));

	of = NULL;
	if(0) {
		/* debug: create check file */
		of = fopen("outputfile", "w");
		if(!of) fatal_exit("cannot write output: %s", strerror(errno));
	}

	while(fgets(txt_in, (int)bufs, inf)) {
		lineno++;
		if(vbmp) printf("\n%s:%d %s", input, lineno, txt_in);
		/* skip empty lines and comments */
		if(txt_in[0] == 0 || txt_in[0] == '\n' || txt_in[0] == ';')
			continue;
		/* read check lines */
		if(!fgets(wire_chk, (int)bufs, chf)) {
			printf("%s too short\n", check);
			unit_assert(0);
		}
		if(!fgets(txt_chk, (int)bufs, chf)) {
			printf("%s too short\n", check);
			unit_assert(0);
		}
		chlineno += 2;
		if(vbmp) printf("%s:%d %s", check, chlineno-1, wire_chk);
		if(vbmp) printf("%s:%d %s", check, chlineno, txt_chk);
		/* generate results */
		rr_transform(txt_in, wire_out, txt_out, back, bufs);
		/* checks */
		if(of) {
			fprintf(of, "%s%s", wire_out, txt_out);
		} else {
			rr_checks(wire_chk, txt_chk, txt_out, wire_out, back);
		}
	}
	
	if(of) fclose(of);
	fclose(inf);
	fclose(chf);
	free(txt_in);
	free(txt_out);
	free(txt_chk);
	free(wire_out);
	free(wire_chk);
	free(back);
}

#define xstr(s) str(s)
#define str(s) #s

#define SRCDIRSTR xstr(SRCDIR)

/** read rrs to and from string, to and from wireformat */
static void
rr_tests(void)
{
	rr_test_file(SRCDIRSTR "/testdata/test_ldnsrr.1",
		SRCDIRSTR "/testdata/test_ldnsrr.c1");
	rr_test_file(SRCDIRSTR "/testdata/test_ldnsrr.2",
		SRCDIRSTR "/testdata/test_ldnsrr.c2");
	rr_test_file(SRCDIRSTR "/testdata/test_ldnsrr.3",
		SRCDIRSTR "/testdata/test_ldnsrr.c3");
	rr_test_file(SRCDIRSTR "/testdata/test_ldnsrr.4",
		SRCDIRSTR "/testdata/test_ldnsrr.c4");
	rr_test_file(SRCDIRSTR "/testdata/test_ldnsrr.5",
		SRCDIRSTR "/testdata/test_ldnsrr.c5");
}

/** test various base64 decoding options */
static void
b64_test(void)
{
	/* "normal" b64 alphabet, with padding */
	char* p1 = "aGVsbG8="; /* "hello" */
	char* p2 = "aGVsbG8+"; /* "hello>" */
	char* p3 = "aGVsbG8/IQ=="; /* "hello?!" */
	char* p4 = "aGVsbG8"; /* "hel" + extra garbage */

	/* base64 url, without padding */
	char* u1 = "aGVsbG8"; /* "hello" */
	char* u2 = "aGVsbG8-"; /* "hello>" */
	char* u3 = "aGVsbG8_IQ"; /* "hello?!" */
	char* u4 = "aaaaa"; /* garbage */

	char target[128];
	size_t tarsize = 128;
	int result;

	memset(target, 0, sizeof(target));
	result = sldns_b64_pton(p1, (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello") && strcmp(target, "hello") == 0);
	memset(target, 0, sizeof(target));
	result = sldns_b64_pton(p2, (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello>") && strcmp(target, "hello>") == 0);
	memset(target, 0, sizeof(target));
	result = sldns_b64_pton(p3, (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello?!") && strcmp(target, "hello?!") == 0);
	memset(target, 0, sizeof(target));
	result = sldns_b64_pton(p4, (uint8_t*)target, tarsize);
	/* when padding is used everything that is not a block of 4 will be
	 * ignored */
	unit_assert(result == (int)strlen("hel") && strcmp(target, "hel") == 0);

	memset(target, 0, sizeof(target));
	result = sldns_b64url_pton(u1, strlen(u1), (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello") && strcmp(target, "hello") == 0);
	memset(target, 0, sizeof(target));
	result = sldns_b64url_pton(u2, strlen(u2), (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello>") && strcmp(target, "hello>") == 0);
	memset(target, 0, sizeof(target));
	result = sldns_b64url_pton(u3, strlen(u3), (uint8_t*)target, tarsize);
	unit_assert(result == (int)strlen("hello+/") && strcmp(target, "hello?!") == 0);
	/* one item in block of four is not allowed */
	memset(target, 0, sizeof(target));
	result = sldns_b64url_pton(u4, strlen(u4), (uint8_t*)target, tarsize);
	unit_assert(result == -1);
}

void
ldns_test(void)
{
	unit_show_feature("sldns");
	rr_tests();
	b64_test();
}
