/*
 * testcode/unitdname.c - unit test for dname routines.
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
 * Calls dname unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include <ctype.h>
#include "util/log.h"
#include "testcode/unitmain.h"
#include "util/data/dname.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"

/** put dname into buffer */
static sldns_buffer*
dname_to_buf(sldns_buffer* b, const char* str)
{
	int e;
	size_t len = sldns_buffer_capacity(b);
	sldns_buffer_clear(b);
	e = sldns_str2wire_dname_buf(str, sldns_buffer_begin(b), &len);
	if(e != 0)
		fatal_exit("%s ldns: %s", __func__, 
			sldns_get_errorstr_parse(e));
	sldns_buffer_set_position(b, len);
	sldns_buffer_flip(b);
	return b;
}

/** test query_dname_len function */
static void 
dname_test_qdl(sldns_buffer* buff)
{
	unit_show_func("util/data/dname.c", "query_dname_len");
	unit_assert( query_dname_len(buff) == 0);
	unit_assert( query_dname_len(dname_to_buf(buff, ".")) == 1 );
	unit_assert( query_dname_len(dname_to_buf(buff, "bla.foo.")) == 9 );
	unit_assert( query_dname_len(dname_to_buf(buff, "x.y.z.example.com."
		)) == 19 );
}

/** test query_dname_tolower */
static void
dname_test_qdtl(sldns_buffer* buff)
{
	unit_show_func("util/data/dname.c", "query_dname_tolower");
	sldns_buffer_write_at(buff, 0, "\012abCDeaBCde\003cOm\000", 16);
	query_dname_tolower(sldns_buffer_begin(buff));
	unit_assert( memcmp(sldns_buffer_begin(buff), 
		"\012abcdeabcde\003com\000", 16) == 0);

	sldns_buffer_write_at(buff, 0, "\001+\012abC{e-ZYXe\003NET\000", 18);
	query_dname_tolower(sldns_buffer_begin(buff));
	unit_assert( memcmp(sldns_buffer_begin(buff), 
		"\001+\012abc{e-zyxe\003net\000", 18) == 0);

	sldns_buffer_write_at(buff, 0, "\000", 1);
	query_dname_tolower(sldns_buffer_begin(buff));
	unit_assert( memcmp(sldns_buffer_begin(buff), "\000", 1) == 0);

	sldns_buffer_write_at(buff, 0, "\002NL\000", 4);
	query_dname_tolower(sldns_buffer_begin(buff));
	unit_assert( memcmp(sldns_buffer_begin(buff), "\002nl\000", 4) == 0);
}

/** test query_dname_compare */
static void
dname_test_query_dname_compare(void)
{
	unit_show_func("util/data/dname.c", "query_dname_compare");
	unit_assert(query_dname_compare((uint8_t*)"", (uint8_t*)"") == 0);
	unit_assert(query_dname_compare((uint8_t*)"\001a", 
					(uint8_t*)"\001a") == 0);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001a", 
					(uint8_t*)"\003abc\001a") == 0);
	unit_assert(query_dname_compare((uint8_t*)"\003aBc\001a", 
					(uint8_t*)"\003AbC\001A") == 0);
	unit_assert(query_dname_compare((uint8_t*)"\003abc", 
					(uint8_t*)"\003abc\001a") == -1);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001a", 
					(uint8_t*)"\003abc") == +1);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001a", 
					(uint8_t*)"") == +1);
	unit_assert(query_dname_compare((uint8_t*)"", 
					(uint8_t*)"\003abc\001a") == -1);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001a", 
					(uint8_t*)"\003xxx\001a") == -1);
	unit_assert(query_dname_compare((uint8_t*)"\003axx\001a", 
					(uint8_t*)"\003abc\001a") == 1);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001a", 
					(uint8_t*)"\003abc\001Z") == -1);
	unit_assert(query_dname_compare((uint8_t*)"\003abc\001Z", 
					(uint8_t*)"\003abc\001a") == 1);
}

/** test dname_count_labels */
static void
dname_test_count_labels(void)
{
	unit_show_func("util/data/dname.c", "dname_count_labels");
	unit_assert(dname_count_labels((uint8_t*)"") == 1);
	unit_assert(dname_count_labels((uint8_t*)"\003com") == 2);
	unit_assert(dname_count_labels((uint8_t*)"\003org") == 2);
	unit_assert(dname_count_labels((uint8_t*)"\007example\003com") == 3);
	unit_assert(dname_count_labels((uint8_t*)"\003bla\007example\003com") 
		== 4);
}

/** test dname_count_size_labels */
static void
dname_test_count_size_labels(void)
{
	size_t sz = 0;
	unit_show_func("util/data/dname.c", "dname_count_size_labels");
	unit_assert(dname_count_size_labels((uint8_t*)"", &sz) == 1);
	unit_assert(sz == 1);
	unit_assert(dname_count_size_labels((uint8_t*)"\003com", &sz) == 2);
	unit_assert(sz == 5);
	unit_assert(dname_count_size_labels((uint8_t*)"\003org", &sz) == 2);
	unit_assert(sz == 5);
	unit_assert(dname_count_size_labels((uint8_t*)"\007example\003com", 
		&sz) == 3);
	unit_assert(sz == 13);
	unit_assert(dname_count_size_labels((uint8_t*)"\003bla\007example"
		"\003com", &sz) == 4);
	unit_assert(sz == 17);
}


/** test pkt_dname_len */
static void
dname_test_pkt_dname_len(sldns_buffer* buff)
{
	unit_show_func("util/data/dname.c", "pkt_dname_len");
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\000", 1);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 1 );
	unit_assert( sldns_buffer_position(buff) == 1);

	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\003org\000", 5);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 5 );
	unit_assert( sldns_buffer_position(buff) == 5);

	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\002os\007example\003org\000", 16);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 16 );
	unit_assert( sldns_buffer_position(buff) == 16);

	/* invalid compression pointer: to self */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\300\000os\007example\003org\000", 17);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 0 );

	/* valid compression pointer */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\003com\000\040\300\000", 8);
	sldns_buffer_flip(buff);
	sldns_buffer_set_position(buff, 6);
	unit_assert( pkt_dname_len(buff) == 5 );
	unit_assert( sldns_buffer_position(buff) == 8);

	/* unknown label type */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\002os\107example\003org\000", 16);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 0 );

	/* label too long */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\002os\047example\003org\000", 16);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 0 );

	/* label exceeds packet */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\002os\007example\007org\004", 16);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 0 );

	/* name very long */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, 
		"\020a1cdef5555544444"
		"\020a2cdef5555544444"
		"\020a3cdef5555544444"
		"\020a4cdef5555544444"
		"\020a5cdef5555544444"
		"\020a6cdef5555544444"
		"\020a7cdef5555544444"
		"\020a8cdef5555544444"
		"\020a9cdef5555544444"
		"\020aAcdef5555544444"
		"\020aBcdef5555544444"
		"\020aCcdef5555544444"
		"\020aDcdef5555544444"
		"\020aEcdef5555544444"	/* 238 up to here */
		"\007aabbccd"		/* 246 up to here */
		"\007example\000"	/* 255 to here */
		, 255);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 255 );
	unit_assert( sldns_buffer_position(buff) == 255);

	/* name too long */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, 
		"\020a1cdef5555544444"
		"\020a2cdef5555544444"
		"\020a3cdef5555544444"
		"\020a4cdef5555544444"
		"\020a5cdef5555544444"
		"\020a6cdef5555544444"
		"\020a7cdef5555544444"
		"\020a8cdef5555544444"
		"\020a9cdef5555544444"
		"\020aAcdef5555544444"
		"\020aBcdef5555544444"
		"\020aCcdef5555544444"
		"\020aXcdef5555544444"
		"\020aXcdef5555544444"
		"\020aXcdef5555544444"
		"\020aDcdef5555544444"
		"\020aEcdef5555544444"	/* 238 up to here */
		"\007aabbccd"		/* 246 up to here */
		"\007example\000"	/* 255 to here */
		, 255);
	sldns_buffer_flip(buff);
	unit_assert( pkt_dname_len(buff) == 0 );
}

/** test dname_lab_cmp */
static void
dname_test_dname_lab_cmp(void)
{
	int ml = 0; /* number of labels that matched exactly */
	unit_show_func("util/data/dname.c", "dname_lab_cmp");

	/* test for equality succeeds */
	unit_assert(dname_lab_cmp((uint8_t*)"", 1, (uint8_t*)"", 1, &ml) == 0);
	unit_assert(ml == 1);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003net", 2, 
		(uint8_t*)"\003net", 2, 
		&ml) == 0);
	unit_assert(ml == 2);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\007example\003net", 3, 
		(uint8_t*)"\007example\003net", 3, 
		&ml) == 0);
	unit_assert(ml == 3);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\004test\007example\003net", 4, 
		(uint8_t*)"\004test\007example\003net", 4, 
		&ml) == 0);
	unit_assert(ml == 4);

	/* root is smaller than anything else */
	unit_assert(dname_lab_cmp(
		(uint8_t*)"", 1, 
		(uint8_t*)"\003net", 2, 
		&ml) == -1);
	unit_assert(ml == 1);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003net", 2, 
		(uint8_t*)"", 1, 
		&ml) == 1);
	unit_assert(ml == 1);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"", 1, 
		(uint8_t*)"\007example\003net", 3, 
		&ml) == -1);
	unit_assert(ml == 1);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\007example\003net", 3, 
		(uint8_t*)"", 1, 
		&ml) == 1);
	unit_assert(ml == 1);

	/* label length makes a difference */
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\004neta", 2, 
		(uint8_t*)"\003net", 2, 
		&ml) != 0);
	unit_assert(ml == 1);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\002ne", 2, 
		(uint8_t*)"\004neta", 2, 
		&ml) != 0);
	unit_assert(ml == 1);

	/* contents follow the zone apex */
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003bla\007example\003net", 4, 
		(uint8_t*)"\007example\003net", 3, 
		&ml) == 1);
	unit_assert(ml == 3);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\007example\003net", 3, 
		(uint8_t*)"\003bla\007example\003net", 4, 
		&ml) == -1);
	unit_assert(ml == 3);

	/* label content makes a difference */
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003aag\007example\003net", 4, 
		(uint8_t*)"\003bla\007example\003net", 4, 
		&ml) == -1);
	unit_assert(ml == 3);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003aag\007example\003net", 4, 
		(uint8_t*)"\003bla\007example\003net", 4, 
		&ml) == -1);
	unit_assert(ml == 3);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003bla\003aag\007example\003net", 5, 
		(uint8_t*)"\003aag\003bla\007example\003net", 5, 
		&ml) == -1);
	unit_assert(ml == 3);
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\02sn\003opt\003aag\007example\003net", 6, 
		(uint8_t*)"\02sn\003opt\003bla\007example\003net", 6, 
		&ml) == -1);
	unit_assert(ml == 3);

	/* but lowercase/uppercase does not make a difference. */
	unit_assert(dname_lab_cmp(
		(uint8_t*)"\003bLa\007examPLe\003net", 4, 
		(uint8_t*)"\003bla\007eXAmple\003nET", 4, 
		&ml) == 0);
	unit_assert(ml == 4);
}

/** test dname_subdomain_c */
static void
dname_test_subdomain(void)
{
	unit_show_func("util/data/dname.c", "dname_subdomain");
	unit_assert(dname_subdomain_c(
		(uint8_t*)"",
		(uint8_t*)""));
	unit_assert(dname_subdomain_c(
		(uint8_t*)"\003com",
		(uint8_t*)""));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"",
		(uint8_t*)"\003com"));
	unit_assert(dname_subdomain_c(
		(uint8_t*)"\007example\003com",
		(uint8_t*)"\003com"));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"\003com",
		(uint8_t*)"\007example\003com"));
	unit_assert(dname_subdomain_c(
		(uint8_t*)"\007example\003com",
		(uint8_t*)""));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"\003net",
		(uint8_t*)"\003com"));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"\003net",
		(uint8_t*)"\003org"));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"\007example\003net",
		(uint8_t*)"\003org"));
	unit_assert(!dname_subdomain_c(
		(uint8_t*)"\003net",
		(uint8_t*)"\007example\003org"));
}

/** test dname_strict_subdomain */
static void
dname_test_strict_subdomain(void)
{
	unit_show_func("util/data/dname.c", "dname_strict_subdomain");
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"", 1,
		(uint8_t*)"", 1));
	unit_assert(dname_strict_subdomain(
		(uint8_t*)"\003com", 2,
		(uint8_t*)"", 1));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"", 1,
		(uint8_t*)"\003com", 2));
	unit_assert(dname_strict_subdomain(
		(uint8_t*)"\007example\003com", 3,
		(uint8_t*)"\003com", 2));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"\003com", 2,
		(uint8_t*)"\007example\003com", 3));
	unit_assert(dname_strict_subdomain(
		(uint8_t*)"\007example\003com", 3,
		(uint8_t*)"", 1));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"\003net", 2,
		(uint8_t*)"\003com", 2));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"\003net", 2,
		(uint8_t*)"\003org", 2));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"\007example\003net", 3,
		(uint8_t*)"\003org", 2));
	unit_assert(!dname_strict_subdomain(
		(uint8_t*)"\003net", 2,
		(uint8_t*)"\007example\003org", 3));
}

/** test dname_is_root */
static void
dname_test_isroot(void)
{
	unit_show_func("util/data/dname.c", "dname_isroot");
	unit_assert(dname_is_root((uint8_t*)"\000"));
	unit_assert(!dname_is_root((uint8_t*)"\001a\000"));
	unit_assert(!dname_is_root((uint8_t*)"\005abvcd\003com\000"));
	/* malformed dname in this test, but should work */
	unit_assert(!dname_is_root((uint8_t*)"\077a\000"));
	unit_assert(dname_is_root((uint8_t*)"\000"));
}

/** test dname_remove_label */
static void
dname_test_removelabel(void)
{
	uint8_t* orig = (uint8_t*)"\007example\003com\000";
	uint8_t* n = orig;
	size_t l = 13;
	unit_show_func("util/data/dname.c", "dname_remove_label");
	dname_remove_label(&n, &l);
	unit_assert( n == orig+8 );
	unit_assert( l == 5 );
	dname_remove_label(&n, &l);
	unit_assert( n == orig+12 );
	unit_assert( l == 1 );
	dname_remove_label(&n, &l);
	unit_assert( n == orig+12 );
	unit_assert( l == 1 );
}

/** test dname_signame_label_count */
static void
dname_test_sigcount(void)
{
	unit_show_func("util/data/dname.c", "dname_signame_label_count");
	unit_assert(dname_signame_label_count((uint8_t*)"\000") == 0);
	unit_assert(dname_signame_label_count((uint8_t*)"\001*\000") == 0);
	unit_assert(dname_signame_label_count((uint8_t*)"\003xom\000") == 1);
	unit_assert(dname_signame_label_count(
		(uint8_t*)"\001*\003xom\000") == 1);
	unit_assert(dname_signame_label_count(
		(uint8_t*)"\007example\003xom\000") == 2);
	unit_assert(dname_signame_label_count(
		(uint8_t*)"\001*\007example\003xom\000") == 2);
	unit_assert(dname_signame_label_count(
		(uint8_t*)"\003www\007example\003xom\000") == 3);
	unit_assert(dname_signame_label_count(
		(uint8_t*)"\001*\003www\007example\003xom\000") == 3);
}

/** test dname_is_wild routine */
static void
dname_test_iswild(void)
{
	unit_show_func("util/data/dname.c", "dname_iswild");
	unit_assert( !dname_is_wild((uint8_t*)"\000") );
	unit_assert( dname_is_wild((uint8_t*)"\001*\000") );
	unit_assert( !dname_is_wild((uint8_t*)"\003net\000") );
	unit_assert( dname_is_wild((uint8_t*)"\001*\003net\000") );
}

/** test dname_canonical_compare */
static void
dname_test_canoncmp(void)
{
	unit_show_func("util/data/dname.c", "dname_canonical_compare");
	/* equality */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\000",
		(uint8_t*)"\000"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003net\000",
		(uint8_t*)"\003net\000"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example\003net\000",
		(uint8_t*)"\007example\003net\000"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\004test\007example\003net\000",
		(uint8_t*)"\004test\007example\003net\000"
		) == 0);

	/* subdomains */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003com",
		(uint8_t*)"\000"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\000",
		(uint8_t*)"\003com"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example\003com",
		(uint8_t*)"\003com"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003com",
		(uint8_t*)"\007example\003com"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example\003com",
		(uint8_t*)"\000"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\000",
		(uint8_t*)"\007example\003com"
		) == -1);

	/* compare rightmost label */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003com",
		(uint8_t*)"\003net"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003net",
		(uint8_t*)"\003com"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003net",
		(uint8_t*)"\003org"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example\003net",
		(uint8_t*)"\003org"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003org",
		(uint8_t*)"\007example\003net"
		) == 1);

	/* label length makes a difference; but only if rest is equal */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\004neta",
		(uint8_t*)"\003net"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\002ne",
		(uint8_t*)"\004neta"
		) == -1);

	/* label content */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003aag\007example\003net",
		(uint8_t*)"\003bla\007example\003net"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003bla\007example\003net",
		(uint8_t*)"\003aag\007example\003net"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003bla\003aag\007example\003net",
		(uint8_t*)"\003aag\003bla\007example\003net"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\02sn\003opt\003aag\007example\003net",
		(uint8_t*)"\02sn\003opt\003bla\007example\003net"
		) == -1);

	/* lowercase during compare */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\003bLa\007examPLe\003net",
		(uint8_t*)"\003bla\007eXAmple\003nET"
		) == 0);

	/* example from 4034 */
	/* example a.example yljkjljk.a.example Z.a.example zABC.a.EXAMPLE
	 z.example \001.z.example *.z.example \200.z.example */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"",
		(uint8_t*)"\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example",
		(uint8_t*)"\001a\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001a\007example",
		(uint8_t*)"\010yljkjljk\001a\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\010yljkjljk\001a\007example",
		(uint8_t*)"\001Z\001a\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001Z\001a\007example",
		(uint8_t*)"\004zABC\001a\007EXAMPLE"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\004zABC\001a\007EXAMPLE",
		(uint8_t*)"\001z\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001z\007example",
		(uint8_t*)"\001\001\001z\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001\001\001z\007example",
		(uint8_t*)"\001*\001z\007example"
		) == -1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001*\001z\007example",
		(uint8_t*)"\001\200\001z\007example"
		) == -1);
	/* same example in reverse */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example",
		(uint8_t*)""
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001a\007example",
		(uint8_t*)"\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\010yljkjljk\001a\007example",
		(uint8_t*)"\001a\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001Z\001a\007example",
		(uint8_t*)"\010yljkjljk\001a\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\004zABC\001a\007EXAMPLE",
		(uint8_t*)"\001Z\001a\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001z\007example",
		(uint8_t*)"\004zABC\001a\007EXAMPLE"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001\001\001z\007example",
		(uint8_t*)"\001z\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001*\001z\007example",
		(uint8_t*)"\001\001\001z\007example"
		) == 1);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001\200\001z\007example",
		(uint8_t*)"\001*\001z\007example"
		) == 1);
	/* same example for equality */
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\007example",
		(uint8_t*)"\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001a\007example",
		(uint8_t*)"\001a\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\010yljkjljk\001a\007example",
		(uint8_t*)"\010yljkjljk\001a\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001Z\001a\007example",
		(uint8_t*)"\001Z\001a\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\004zABC\001a\007EXAMPLE",
		(uint8_t*)"\004zABC\001a\007EXAMPLE"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001z\007example",
		(uint8_t*)"\001z\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001\001\001z\007example",
		(uint8_t*)"\001\001\001z\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001*\001z\007example",
		(uint8_t*)"\001*\001z\007example"
		) == 0);
	unit_assert( dname_canonical_compare(
		(uint8_t*)"\001\200\001z\007example",
		(uint8_t*)"\001\200\001z\007example"
		) == 0);
}

/** Test dname_get_shared_topdomain */
static void
dname_test_topdomain(void)
{
	unit_show_func("util/data/dname.c", "dname_get_shared_topdomain");
	unit_assert( query_dname_compare(
		dname_get_shared_topdomain(
			(uint8_t*)"",
			(uint8_t*)""), 
		(uint8_t*)"") == 0);
	unit_assert( query_dname_compare(
		dname_get_shared_topdomain(
			(uint8_t*)"\003www\007example\003com",
			(uint8_t*)"\003www\007example\003com"), 
		(uint8_t*)"\003www\007example\003com") == 0);
	unit_assert( query_dname_compare(
		dname_get_shared_topdomain(
			(uint8_t*)"\003www\007example\003com",
			(uint8_t*)"\003bla\007example\003com"), 
		(uint8_t*)"\007example\003com") == 0);
}

/** Test dname_valid */
static void
dname_test_valid(void)
{
	unit_show_func("util/data/dname.c", "dname_valid");
	unit_assert( dname_valid( 
			(uint8_t*)"\003www\007example\003com", 255) == 17);
	unit_assert( dname_valid((uint8_t*)"", 255) == 1);
	unit_assert( dname_valid( (uint8_t*)
		"\020a1cdef5555544444"
		"\020a2cdef5555544444"
		"\020a3cdef5555544444"
		"\020a4cdef5555544444"
		"\020a5cdef5555544444"
		"\020a6cdef5555544444"
		"\020a7cdef5555544444"
		"\020a8cdef5555544444"
		"\020a9cdef5555544444"
		"\020aAcdef5555544444"
		"\020aBcdef5555544444"
		"\020aCcdef5555544444"
		"\020aDcdef5555544444"
		"\020aEcdef5555544444"	/* 238 up to here */
		"\007aabbccd"		/* 246 up to here */
		"\007example\000"	/* 255 to here */
		, 255) == 255);
	unit_assert( dname_valid( (uint8_t*)
		"\020a1cdef5555544444"
		"\020a2cdef5555544444"
		"\020a3cdef5555544444"
		"\020a4cdef5555544444"
		"\020a5cdef5555544444"
		"\020a6cdef5555544444"
		"\020a7cdef5555544444"
		"\020a8cdef5555544444"
		"\020a9cdef5555544444"
		"\020aAcdef5555544444"
		"\020aBcdef5555544444"
		"\020aCcdef5555544444"
		"\020aDcdef5555544444"
		"\020aEcdef5555544444"	/* 238 up to here */
		"\007aabbccd"		/* 246 up to here */
		"\010exampleX\000"	/* 256 to here */
		, 4096) == 0);
}

/** Test dname_has_label */
static void
dname_test_has_label(void)
{
	unit_show_func("util/data/dname.c", "dname_has_label");
	/* label past root label */
	unit_assert(dname_has_label((uint8_t*)"\01a\0\01c", 5, (uint8_t*)"\01c") == 0);
	/* label not found */
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c\0", 6, (uint8_t*)"\01e") == 0);
	/* buffer too short */
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c\0", 5, (uint8_t*)"\0") == 0);
	unit_assert(dname_has_label((uint8_t*)"\1a\0", 2, (uint8_t*)"\0") == 0);
	unit_assert(dname_has_label((uint8_t*)"\0", 0, (uint8_t*)"\0") == 0);
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c", 4, (uint8_t*)"\01c") == 0);
	unit_assert(dname_has_label((uint8_t*)"\02ab\03qwe\06oqieur\03def\01c\0", 19, (uint8_t*)"\01c") == 0);

	/* positive cases  */
	unit_assert(dname_has_label((uint8_t*)"\0", 1, (uint8_t*)"\0") == 1);
	unit_assert(dname_has_label((uint8_t*)"\1a\0", 3, (uint8_t*)"\0") == 1);
	unit_assert(dname_has_label((uint8_t*)"\01a\0\01c", 5, (uint8_t*)"\0") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c", 5, (uint8_t*)"\01c") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c\0", 10, (uint8_t*)"\0") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\01c\0", 7, (uint8_t*)"\0") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\03qwe\06oqieur\03def\01c\0", 22, (uint8_t*)"\03def") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\03qwe\06oqieur\03def\01c\0", 22, (uint8_t*)"\02ab") == 1);
	unit_assert(dname_has_label((uint8_t*)"\02ab\03qwe\06oqieur\03def\01c\0", 22, (uint8_t*)"\01c") == 1);
}

/** test pkt_dname_tolower */
static void
dname_test_pdtl(sldns_buffer* loopbuf, sldns_buffer* boundbuf)
{
	unit_show_func("util/data/dname.c", "pkt_dname_tolower");
	pkt_dname_tolower(loopbuf, sldns_buffer_at(loopbuf, 12));
	pkt_dname_tolower(boundbuf, sldns_buffer_at(boundbuf, 12));
}

/** setup looped dname and out-of-bounds dname ptr */
static void
dname_setup_bufs(sldns_buffer* loopbuf, sldns_buffer* boundbuf)
{
	sldns_buffer_write_u16(loopbuf, 0xd54d);  /* id */
	sldns_buffer_write_u16(loopbuf, 0x12);    /* flags  */
	sldns_buffer_write_u16(loopbuf, 1);       /* qdcount */
	sldns_buffer_write_u16(loopbuf, 0);       /* ancount */
	sldns_buffer_write_u16(loopbuf, 0);       /* nscount */
	sldns_buffer_write_u16(loopbuf, 0);       /* arcount */
	sldns_buffer_write_u8(loopbuf, 0xc0); /* PTR back at itself */
	sldns_buffer_write_u8(loopbuf, 0x0c);
	sldns_buffer_flip(loopbuf);

	sldns_buffer_write_u16(boundbuf, 0xd54d);  /* id */
	sldns_buffer_write_u16(boundbuf, 0x12);    /* flags  */
	sldns_buffer_write_u16(boundbuf, 1);       /* qdcount */
	sldns_buffer_write_u16(boundbuf, 0);       /* ancount */
	sldns_buffer_write_u16(boundbuf, 0);       /* nscount */
	sldns_buffer_write_u16(boundbuf, 0);       /* arcount */
	sldns_buffer_write_u8(boundbuf, 0x01); /* len=1 */
	sldns_buffer_write_u8(boundbuf, (uint8_t)'A'); /* A. label */
	sldns_buffer_write_u8(boundbuf, 0xc0); /* PTR out of bounds */
	sldns_buffer_write_u8(boundbuf, 0xcc);
	sldns_buffer_flip(boundbuf);
}

static void
dname_test_str(sldns_buffer* buff)
{
	char result[LDNS_MAX_DOMAINLEN], expect[LDNS_MAX_DOMAINLEN], *e;
	size_t i;
	unit_show_func("util/data/dname.c", "dname_str");

	/* root ; expected OK */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff, "\000", 1);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 1 );
	unit_assert( pkt_dname_len(buff) == 1 );
	dname_str(sldns_buffer_begin(buff), result);
	unit_assert( strcmp( ".", result) == 0 );

	/* LDNS_MAX_DOMAINLEN - 1 ; expected OK */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff,
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /*  64 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 128 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 192 up to here */
		"\074abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"     /* 253 up to here */
		"\000"                                                                 /* 254 up to here */
		, 254);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 254 );
	unit_assert( pkt_dname_len(buff) == 254 );
	dname_str(sldns_buffer_begin(buff), result);
	unit_assert( strcmp(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"
		".", result) == 0 );

	/* LDNS_MAX_DOMAINLEN ; expected OK */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff,
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /*  64 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 128 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 192 up to here */
		"\075abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012345678"    /* 254 up to here */
		"\000"                                                                 /* 255 up to here */
		, 255);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 255 );
	unit_assert( pkt_dname_len(buff) == 255 );
	dname_str(sldns_buffer_begin(buff), result);
	unit_assert( strcmp(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012345678"
		".", result) == 0 );

	/* LDNS_MAX_DOMAINLEN + 1 ; expected to fail, output uses '&' on the latest label */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff,
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /*  64 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 128 up to here */
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /* 192 up to here */
		"\076abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"   /* 255 up to here */
		"\000"                                                                 /* 256 up to here */
		, 256);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 256 );
	unit_assert( pkt_dname_len(buff) == 0 );
	dname_str(sldns_buffer_begin(buff), result);
	unit_assert( strcmp(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"&", result) == 0 );

	/* LDNS_MAX_LABELLEN + 1 ; expected to fail, output uses '#' on the offending label */
	sldns_buffer_clear(buff);
	sldns_buffer_write(buff,
		"\077abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890"  /*  64 up to here */
		"\100abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890a" /* 129 up to here */
		"\000"                                                                 /* 130 up to here */
		, 130);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 130 );
	unit_assert( pkt_dname_len(buff) == 0 );
	dname_str(sldns_buffer_begin(buff), result);
	unit_assert( strcmp(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890."
		"#", result) == 0 );

	/* LDNS_MAX_DOMAINLEN with single labels; expected OK */
	sldns_buffer_clear(buff);
	for(i=0; i<=252; i+=2)
		sldns_buffer_write(buff, "\001a", 2);
	sldns_buffer_write_u8(buff, 0);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 255 );
	unit_assert( pkt_dname_len(buff) == 255 );
	dname_str(sldns_buffer_begin(buff), result);
	e = expect;
	for(i=0; i<=252; i+=2) {
		*e++ = 'a';
		*e++ = '.';
	}
	*e = '\0';
	unit_assert( strcmp(expect, result) == 0 );

	/* LDNS_MAX_DOMAINLEN + 1 with single labels; expected to fail, output uses '&' on the latest label */
	sldns_buffer_clear(buff);
	for(i=0; i<=250; i+=2)
		sldns_buffer_write(buff, "\001a", 2);
	sldns_buffer_write(buff, "\002ab", 3);
	sldns_buffer_write_u8(buff, 0);
	sldns_buffer_flip(buff);
	unit_assert( sldns_buffer_limit(buff) == 256 );
	unit_assert( pkt_dname_len(buff) == 0 );
	dname_str(sldns_buffer_begin(buff), result);
	e = expect;
	for(i=0; i<=250; i+=2) {
		*e++ = 'a';
		*e++ = '.';
	}
	*e++ = '&';
	*e = '\0';
	unit_assert( strcmp(expect, result) == 0 );

	/* Only alphas, numericals and '-', '_' and '*' are allowed in the output */
	for(i=1; i<=255; i++) {
		if(isalnum(i) || i == '-' || i == '_' || i == '*')
			continue;
		sldns_buffer_clear(buff);
		sldns_buffer_write_u8(buff, 1);
		sldns_buffer_write_u8(buff, (uint8_t)i);
		sldns_buffer_write_u8(buff, 0);
		sldns_buffer_flip(buff);
		unit_assert( sldns_buffer_limit(buff) == 3 );
		unit_assert( pkt_dname_len(buff) == 3);
		dname_str(sldns_buffer_begin(buff), result);
		if(strcmp( "?.", result) != 0 ) {
			log_err("ASCII value '0x%lX' allowed in string output", (unsigned long)i);
			unit_assert(0);
		}
	}
}

void dname_test(void)
{
	sldns_buffer* loopbuf = sldns_buffer_new(14);
	sldns_buffer* boundbuf = sldns_buffer_new(16);
	sldns_buffer* buff = sldns_buffer_new(65800);
	unit_assert(loopbuf && boundbuf && buff);
	sldns_buffer_flip(buff);
	dname_setup_bufs(loopbuf, boundbuf);
	dname_test_qdl(buff);
	dname_test_qdtl(buff);
	dname_test_pdtl(loopbuf, boundbuf);
	dname_test_query_dname_compare();
	dname_test_count_labels();
	dname_test_count_size_labels();
	dname_test_dname_lab_cmp();
	dname_test_pkt_dname_len(buff);
	dname_test_strict_subdomain();
	dname_test_subdomain();
	dname_test_isroot();
	dname_test_removelabel();
	dname_test_sigcount();
	dname_test_iswild();
	dname_test_canoncmp();
	dname_test_topdomain();
	dname_test_valid();
	dname_test_has_label();
	dname_test_str(buff);
	sldns_buffer_free(buff);
	sldns_buffer_free(loopbuf);
	sldns_buffer_free(boundbuf);
}
