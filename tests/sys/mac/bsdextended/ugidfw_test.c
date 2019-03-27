/*-
 * Copyright (c) 2005 McAfee, Inc.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/mac.h>
#include <sys/mount.h>

#include <security/mac_bsdextended/mac_bsdextended.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ugidfw.h>
#include <unistd.h>

/*
 * Starting point for a regression test for mac_bsdextended(4) and the
 * supporting libugidfw(3).
 */

/*
 * This section of the regression test passes some test cases through the
 * rule<->string routines to confirm they work approximately as desired.
 */

/*
 * List of users and groups we must check exists before we can begin, since
 * they are used in the string test rules.  We use users and groups that will
 * always exist in a default install used for regression testing.
 */
static const char *test_users[] = {
	"root",
	"daemon",
	"operator",
	"bin",
};

static const char *test_groups[] = {
	"wheel",
	"daemon",
	"operator",
	"bin",
};

static int test_num;

/*
 * List of test strings that must go in (and come out) of libugidfw intact.
 */
static const char *test_strings[] = {
	/* Variations on subject and object uids. */
	"subject uid root object uid root mode n",
	"subject uid root object uid daemon mode n",
	"subject uid daemon object uid root mode n",
	"subject uid daemon object uid daemon mode n",
	/* Variations on mode. */
	"subject uid root object uid root mode a",
	"subject uid root object uid root mode r",
	"subject uid root object uid root mode s",
	"subject uid root object uid root mode w",
	"subject uid root object uid root mode x",
	"subject uid root object uid root mode arswx",
	/* Variations on subject and object gids. */
	"subject gid wheel object gid wheel mode n",
	"subject gid wheel object gid daemon mode n",
	"subject gid daemon object gid wheel mode n",
	"subject gid daemon object gid daemon mode n",
	/* Subject uids and subject gids. */
	"subject uid bin gid daemon object uid operator gid wheel mode n",
	/* Not */
	"subject not uid operator object uid bin mode n",
	"subject uid bin object not uid operator mode n",
	"subject not uid daemon object not uid operator mode n",
	/* Ranges */
	"subject uid root:operator object gid wheel:bin mode n",
	/* Jail ID */
	"subject jailid 1 object uid root mode n",
	/* Filesys */
	"subject uid root object filesys / mode n",
	"subject uid root object filesys /dev mode n",
	/* S/UGID */
	"subject not uid root object sgid mode n",
	"subject not uid root object sgid mode n",
	/* Matching uid/gid */
	"subject not uid root:operator object not uid_of_subject mode n",
	"subject not gid wheel:bin object not gid_of_subject mode n",
	/* Object types */
	"subject uid root object type a mode a",
	"subject uid root object type r mode a",
	"subject uid root object type d mode a",
	"subject uid root object type b mode a",
	"subject uid root object type c mode a",
	"subject uid root object type l mode a",
	"subject uid root object type s mode a",
	"subject uid root object type rbc mode a",
	"subject uid root object type dls mode a",
	/* Empty rules always match */
	"subject object mode a",
	/* Partial negations */
	"subject ! uid root object mode n",
	"subject ! gid wheel object mode n",
	"subject ! jailid 2 object mode n",
	"subject object ! uid root mode n",
	"subject object ! gid wheel mode n",
	"subject object ! filesys / mode n",
	"subject object ! suid mode n",
	"subject object ! sgid mode n",
	"subject object ! uid_of_subject mode n",
	"subject object ! gid_of_subject mode n",
	"subject object ! type d mode n",
	/* All out nonsense */
	"subject uid root ! gid wheel:bin ! jailid 1 "
	    "object ! uid root:daemon gid daemon filesys / suid sgid uid_of_subject gid_of_subject ! type r "
	    "mode rsx",
};

static void
test_libugidfw_strings(void)
{
	struct mac_bsdextended_rule rule;
	char errorstr[256];
	char rulestr[256];
	size_t i;
	int error;

	for (i = 0; i < nitems(test_users); i++, test_num++) {
		if (getpwnam(test_users[i]) == NULL)
			printf("not ok %d # test_libugidfw_strings: getpwnam(%s) "
			    "failed: %s\n", test_num, test_users[i], strerror(errno));
		else
			printf("ok %d\n", test_num);
	}

	for (i = 0; i < nitems(test_groups); i++, test_num++) {
		if (getgrnam(test_groups[i]) == NULL)
			printf("not ok %d # test_libugidfw_strings: getgrnam(%s) "
			    "failed: %s\n", test_num, test_groups[i], strerror(errno));
		else
			printf("ok %d\n", test_num);
	}

	for (i = 0; i < nitems(test_strings); i++) {
		error = bsde_parse_rule_string(test_strings[i], &rule,
		    sizeof(errorstr), errorstr);
		if (error == -1)
			printf("not ok %d # bsde_parse_rule_string: '%s' (%zu) "
			    "failed: %s\n", test_num, test_strings[i], i, errorstr);
		else
			printf("ok %d\n", test_num);
		test_num++;

		error = bsde_rule_to_string(&rule, rulestr, sizeof(rulestr));
		if (error < 0)
			printf("not ok %d # bsde_rule_to_string: rule for '%s' "
			    "returned %d\n", test_num, test_strings[i], error);
		else
			printf("ok %d\n", test_num);
		test_num++;

		if (strcmp(test_strings[i], rulestr) != 0)
			printf("not ok %d # test_libugidfw: '%s' in, '%s' "
			    "out\n", test_num, test_strings[i], rulestr);
		else
			printf("ok %d\n", test_num);
		test_num++;
	}
}

int
main(void)
{
	char errorstr[256];
	int count, slots;

	test_num = 1;

	/* Print an error if a non-root user attemps to run the tests. */
	if (getuid() != 0) {
		printf("1..0 # SKIP you must be root\n");
		return (0);
	}

	switch (mac_is_present("bsdextended")) {
	case -1:
		printf("1..0 # SKIP mac_is_present failed: %s\n",
		    strerror(errno));
		return (0);
	case 1:
		break;
	case 0:
	default:
		printf("1..0 # SKIP mac_bsdextended not loaded\n");
		return (0);
	}

	printf("1..%zu\n", nitems(test_users) + nitems(test_groups) +
	    3 * nitems(test_strings) + 2);

	test_libugidfw_strings();

	/*
	 * Some simple up-front checks to see if we're able to query the
	 * policy for basic state.  We want the rule count to be 0 before
	 * starting, but "slots" is a property of prior runs and so we ignore
	 * the return value.
	 */
	count = bsde_get_rule_count(sizeof(errorstr), errorstr);
	if (count == -1)
		printf("not ok %d # bsde_get_rule_count: %s\n", test_num,
		    errorstr);
	else
		printf("ok %d\n", test_num);

	test_num++;

	slots = bsde_get_rule_slots(sizeof(errorstr), errorstr);
	if (slots == -1)
		printf("not ok %d # bsde_get_rule_slots: %s\n", test_num,
		    errorstr);
	else
		printf("ok %d\n", test_num);

	return (0);
}
