/* crypto/pqueue/pq_test.c */
/*
 * DTLS implementation written by Nagendra Modadugu
 * (nagendra@cs.stanford.edu) for the OpenSSL project 2005.
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pqueue.h"

static const unsigned char *pq_expected[3] = {
	"ifragili",
	"sticexpi",
	"supercal"
};

static int
test_pqueue(void)
{
	const unsigned char *prio1 = pq_expected[2];
	const unsigned char *prio2 = pq_expected[0];
	const unsigned char *prio3 = pq_expected[1];
	pqueue pq = NULL;
	pitem *item = NULL;
	pitem *iter = NULL;
	int i = 0;
	int failed = 1;

	if ((pq = pqueue_new()) == NULL)
		goto failure;

	if (!pqueue_insert(pq, pitem_new(prio3, NULL)))
		goto failure;
	if (!pqueue_insert(pq, pitem_new(prio1, NULL)))
		goto failure;
	if (!pqueue_insert(pq, pitem_new(prio2, NULL)))
		goto failure;

	if (pqueue_size(pq) != 3)
		goto failure;

	if ((item = pqueue_find(pq, prio1)) == NULL)
		goto failure;
	if ((item = pqueue_find(pq, prio2)) == NULL)
		goto failure;
	if ((item = pqueue_find(pq, prio3)) == NULL)
		goto failure;

	if ((item = pqueue_peek(pq)) == NULL)
		goto failure;

	if (memcmp(item->priority, pq_expected[0], 8))
		goto failure;

	iter = pqueue_iterator(pq);
	for (item = pqueue_next(&iter); item != NULL; item = pqueue_next(&iter)) {
		if (memcmp(item->priority, pq_expected[i], 8) != 0)
			goto failure;
		i++;
	}

	failed = (i != 3);

 failure:

	for (item = pqueue_pop(pq); item != NULL; item = pqueue_pop(pq))
		pitem_free(item);
	pqueue_free(pq);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_pqueue();

	return failed;
}
