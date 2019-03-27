/*-
 * Copyright (c) 2015 EMC Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

ATF_TC(slist_test);
ATF_TC_HEAD(slist_test, tc)
{

	atf_tc_set_md_var(tc, "descr", "SLIST macro feature tests");
}

ATF_TC_BODY(slist_test, tc)
{
	SLIST_HEAD(stailhead, entry) head = SLIST_HEAD_INITIALIZER(head);
	struct entry {
		SLIST_ENTRY(entry) entries;
		int i;
	} *n1, *n2, *n3, *np;
	int i, j, length;

	SLIST_INIT(&head);

	printf("Ensuring SLIST_EMPTY works\n");

	ATF_REQUIRE(SLIST_EMPTY(&head));

	i = length = 0;

	SLIST_FOREACH(np, &head, entries) {
		length++;
	}
	ATF_REQUIRE_EQ(length, 0);

	printf("Ensuring SLIST_INSERT_HEAD works\n");

	n1 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n1 != NULL);
	n1->i = i++;

	SLIST_INSERT_HEAD(&head, n1, entries);

	printf("Ensuring SLIST_FIRST returns element 1\n");
	ATF_REQUIRE_EQ(SLIST_FIRST(&head), n1);

	j = length = 0;
	SLIST_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 1);

	printf("Ensuring SLIST_INSERT_AFTER works\n");

	n2 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n2 != NULL);
	n2->i = i++;

	SLIST_INSERT_AFTER(n1, n2, entries);

	n3 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n3 != NULL);
	n3->i = i++;

	SLIST_INSERT_AFTER(n2, n3, entries);

	j = length = 0;
	SLIST_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 3);

	printf("Ensuring SLIST_REMOVE_HEAD works\n");

	printf("Ensuring SLIST_FIRST returns element 1\n");
	ATF_REQUIRE_EQ(SLIST_FIRST(&head), n1);

	SLIST_REMOVE_HEAD(&head, entries);

	printf("Ensuring SLIST_FIRST now returns element 2\n");
	ATF_REQUIRE_EQ(SLIST_FIRST(&head), n2);

	j = 1; /* Starting point's 1 this time */
	length = 0;
	SLIST_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 2);

	printf("Ensuring SLIST_REMOVE_AFTER works by removing the tail\n");

	SLIST_REMOVE_AFTER(n2, entries);

	j = 1; /* Starting point's 1 this time */
	length = 0;
	SLIST_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 1);

	printf("Ensuring SLIST_FIRST returns element 2\n");
	ATF_REQUIRE_EQ(SLIST_FIRST(&head), n2);

}

ATF_TC(stailq_test);
ATF_TC_HEAD(stailq_test, tc)
{

	atf_tc_set_md_var(tc, "descr", "STAILQ macro feature tests");
}

ATF_TC_BODY(stailq_test, tc)
{
	STAILQ_HEAD(stailhead, entry) head = STAILQ_HEAD_INITIALIZER(head);
	struct entry {
		STAILQ_ENTRY(entry) entries;
		int i;
	} *n1, *n2, *n3, *np;
	int i, j, length;

	printf("Ensuring empty STAILQs are treated properly\n");
	STAILQ_INIT(&head);
	ATF_REQUIRE(STAILQ_EMPTY(&head));

	i = length = 0;

	STAILQ_FOREACH(np, &head, entries) {
		length++;
	}
	ATF_REQUIRE_EQ(length, 0);

	printf("Ensuring STAILQ_INSERT_HEAD works\n");

	n1 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n1 != NULL);
	n1->i = i++;

	STAILQ_INSERT_HEAD(&head, n1, entries);

	j = length = 0;
	STAILQ_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 1);

	printf("Ensuring STAILQ_INSERT_TAIL works\n");

	n2 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n2 != NULL);
	n2->i = i++;

	STAILQ_INSERT_TAIL(&head, n2, entries);

	n3 = malloc(sizeof(struct entry));
	ATF_REQUIRE(n3 != NULL);
	n3->i = i++;

	STAILQ_INSERT_TAIL(&head, n3, entries);

	j = length = 0;
	STAILQ_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 3);

	printf("Ensuring STAILQ_REMOVE_HEAD works\n");

	STAILQ_REMOVE_HEAD(&head, entries);

	j = 1; /* Starting point's 1 this time */
	length = 0;
	STAILQ_FOREACH(np, &head, entries) {
		ATF_REQUIRE_EQ_MSG(np->i, j,
		    "%d (entry counter) != %d (counter)", np->i, j);
		j++;
		length++;
	}
	ATF_REQUIRE_EQ(length, 2);

}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, slist_test);
	ATF_TP_ADD_TC(tp, stailq_test);

	return (atf_no_error());
}
