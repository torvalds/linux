/*
 * testcode/unitmain.h - unit test main program for unbound.
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
 * Declarations useful for the unit tests.
 */

#ifndef TESTCODE_UNITMAIN_H
#define TESTCODE_UNITMAIN_H
#include "util/log.h"

/** number of tests done */
extern int testcount;
/** test bool x, exits on failure, increases testcount. */
#ifdef DEBUG_UNBOUND
#define unit_assert(x) do {testcount++; log_assert(x);} while(0)
#else
#define unit_assert(x) do {testcount++; if(!(x)) { fprintf(stderr, "assertion failure %s:%d\n", __FILE__, __LINE__); exit(1);}} while(0)
#endif

/** we are now testing this function */
void unit_show_func(const char* file, const char* func);
/** we are testing this functionality */
void unit_show_feature(const char* feature);

/** unit test lruhashtable implementation */
void lruhash_test(void);
/** unit test slabhashtable implementation */
void slabhash_test(void);
/** unit test for msgreply and msgparse */
void msgparse_test(void);
/** unit test dname handling functions */
void dname_test(void);
/** unit test trust anchor storage functions */
void anchors_test(void);
/** unit test for verification functions */
void verify_test(void);
/** unit test for negative cache functions */
void neg_test(void);
/** unit test for regional allocator functions */
void regional_test(void);
#ifdef CLIENT_SUBNET
/** Unit test for ECS functions */
void ecs_test(void);
#endif /* CLIENT_SUBNET */
/** unit test for ldns functions */
void ldns_test(void);
/** unit test for auth zone functions */
void authzone_test(void);
/** unit test for zonemd functions */
void zonemd_test(void);
/** unit test for tcp_reuse functions */
void tcpreuse_test(void);
/** unit test for doq functions */
void doq_test(void);
/** unit test for infra cache functions */
void infra_test(void);

#endif /* TESTCODE_UNITMAIN_H */
