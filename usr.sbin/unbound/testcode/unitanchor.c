/*
 * testcode/unitanchor.c - unit test for trust anchor storage.
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
 * Calls trust anchor unit tests. Exits with code 1 on a failure. 
 */

#include "config.h"
#include "util/log.h"
#include "util/data/dname.h"
#include "testcode/unitmain.h"
#include "validator/val_anchor.h"
#include "sldns/sbuffer.h"
#include "sldns/rrdef.h"

/** test empty set */
static void
test_anchor_empty(struct val_anchors* a)
{
	uint16_t c = LDNS_RR_CLASS_IN;
	unit_assert(anchors_lookup(a, (uint8_t*)"\000", 1, c) == NULL);
	unit_assert(anchors_lookup(a, (uint8_t*)"\003com\000", 5, c) == NULL);
	unit_assert(anchors_lookup(a, 
		(uint8_t*)"\007example\003com\000", 11, c) == NULL);
	unit_assert(anchors_lookup(a, (uint8_t*)"\002nl\000", 4, c) == NULL);
	unit_assert(anchors_lookup(a, 
		(uint8_t*)"\004labs\002nl\000", 9, c) == NULL);
	unit_assert(anchors_lookup(a, 
		(uint8_t*)"\004fabs\002nl\000", 9, c) == NULL);
}

/** test set of one anchor */
static void
test_anchor_one(sldns_buffer* buff, struct val_anchors* a)
{
	struct trust_anchor* ta;
	uint16_t c = LDNS_RR_CLASS_IN;
	unit_assert(anchor_store_str(a, buff, 
		"nl. DS 42860 5 1 14D739EB566D2B1A5E216A0BA4D17FA9B038BE4A"));
	unit_assert(anchors_lookup(a, (uint8_t*)"\000", 1, c) == NULL);
	unit_assert(anchors_lookup(a, (uint8_t*)"\003com\000", 5, c) == NULL);
	unit_assert(anchors_lookup(a, 
		(uint8_t*)"\007example\003com\000", 11, c) == NULL);

	unit_assert((ta=anchors_lookup(a,
		(uint8_t*)"\002nl\000", 4, c)) != NULL);
	lock_basic_unlock(&ta->lock);

	unit_assert((ta=anchors_lookup(a, 
		(uint8_t*)"\004labs\002nl\000", 9, c)) != NULL);
	lock_basic_unlock(&ta->lock);

	unit_assert((ta=anchors_lookup(a, 
		(uint8_t*)"\004fabs\002nl\000", 9, c)) != NULL);
	lock_basic_unlock(&ta->lock);

	unit_assert(anchors_lookup(a, (uint8_t*)"\002oo\000", 4, c) == NULL);
}

/** test with several anchors */
static void
test_anchors(sldns_buffer* buff, struct val_anchors* a)
{
	struct trust_anchor* ta;
	uint16_t c = LDNS_RR_CLASS_IN;
	unit_assert(anchor_store_str(a, buff, 
	"labs.nl. DS 42860 5 1 14D739EB566D2B1A5E216A0BA4D17FA9B038BE4A"));
	unit_assert(anchors_lookup(a, (uint8_t*)"\000", 1, c) == NULL);
	unit_assert(anchors_lookup(a, (uint8_t*)"\003com\000", 5, c) == NULL);
	unit_assert(anchors_lookup(a, 
		(uint8_t*)"\007example\003com\000", 11, c) == NULL);

	unit_assert(ta = anchors_lookup(a, (uint8_t*)"\002nl\000", 4, c));
	unit_assert(query_dname_compare(ta->name, (uint8_t*)"\002nl\000")==0);
	lock_basic_unlock(&ta->lock);

	unit_assert(ta = anchors_lookup(a, 
		(uint8_t*)"\004labs\002nl\000", 9, c));
	unit_assert(query_dname_compare(ta->name, 
		(uint8_t*)"\004labs\002nl\000") == 0);
	lock_basic_unlock(&ta->lock);

	unit_assert(ta = anchors_lookup(a, 
		(uint8_t*)"\004fabs\002nl\000", 9, c));
	unit_assert(query_dname_compare(ta->name, 
		(uint8_t*)"\002nl\000") == 0);
	lock_basic_unlock(&ta->lock);

	unit_assert(anchors_lookup(a, (uint8_t*)"\002oo\000", 4, c) == NULL);
}

void anchors_test(void)
{
	sldns_buffer* buff = sldns_buffer_new(65800);
	struct val_anchors* a;
	unit_show_feature("trust anchor store");
	unit_assert(a = anchors_create());
	sldns_buffer_flip(buff);
	test_anchor_empty(a);
	test_anchor_one(buff, a);
	test_anchors(buff, a);
	anchors_delete(a);
	sldns_buffer_free(buff);
}
