/*	$OpenBSD: matching.c,v 1.2 2010/11/04 15:35:00 martinh Exp $ */

/*
 * Copyright (c) 2010 Martin Hedenfalk <martinh@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema.h"

#ifndef nitems
# define nitems(_a)	 (sizeof((_a)) / sizeof((_a)[0]))
#endif

static const char *ia5string_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.26",
	NULL
};

static const char *dir_string_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.15",
	"1.3.6.1.4.1.1466.115.121.1.44",
	"1.3.6.1.4.1.1466.115.121.1.11",
	"1.3.6.1.4.1.1466.115.121.1.50",
	"1.3.6.1.4.1.1466.115.121.1.26",
	NULL
};

static const char *num_string_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.36",
	NULL
};

static const char *telephone_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.50",
	NULL
};

static const char *dir_string_sequence_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.41",
	NULL
};

static const char *int_first_component_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.17",
	NULL
};

static const char *oid_first_component_syntaxes[] = {
	"1.3.6.1.4.1.1466.115.121.1.3",
	"1.3.6.1.4.1.1466.115.121.1.16",
	"1.3.6.1.4.1.1466.115.121.1.54",
	"1.3.6.1.4.1.1466.115.121.1.30",
	"1.3.6.1.4.1.1466.115.121.1.31",
	"1.3.6.1.4.1.1466.115.121.1.35",
	"1.3.6.1.4.1.1466.115.121.1.37",
	NULL
};

struct match_rule match_rules[] = {

	{ "1.3.6.1.1.16.2", "uuidMatch", MATCH_EQUALITY, NULL, "1.3.6.1.1.16.1", NULL },
	{ "1.3.6.1.1.16.3", "uuidOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.1.16.1", NULL },
	{ "1.3.6.1.4.1.1466.109.114.1", "caseExactIA5Match", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.26", ia5string_syntaxes },
	{ "1.3.6.1.4.1.1466.109.114.2", "caseIgnoreIA5Match", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.26", ia5string_syntaxes },
	{ "1.3.6.1.4.1.1466.109.114.3", "caseIgnoreIA5SubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", ia5string_syntaxes },
	{ "2.5.13.0", "objectIdentifierMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.38", NULL },
	{ "2.5.13.1", "distinguishedNameMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.12", NULL },
	{ "2.5.13.10", "numericStringSubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", num_string_syntaxes },
	{ "2.5.13.11", "caseIgnoreListMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.41", NULL },
	{ "2.5.13.12", "caseIgnoreListSubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", dir_string_sequence_syntaxes },
	{ "2.5.13.13", "booleanMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.7", NULL },
	{ "2.5.13.14", "integerMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.27", NULL },
	{ "2.5.13.15", "integerOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.27", NULL },
	{ "2.5.13.16", "bitStringMatch", MATCH_EQUALITY,  NULL, "1.3.6.1.4.1.1466.115.121.1.6", NULL },
	{ "2.5.13.17", "octetStringMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.40", NULL },
	{ "2.5.13.18", "octetStringOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.40", NULL },
	{ "2.5.13.2", "caseIgnoreMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.15", dir_string_syntaxes },
	{ "2.5.13.20", "telephoneNumberMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.50", NULL },
	{ "2.5.13.21", "telephoneNumberSubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", telephone_syntaxes },
	{ "2.5.13.23", "uniqueMemberMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.34", NULL },
	{ "2.5.13.27", "generalizedTimeMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.24", NULL },
	{ "2.5.13.28", "generalizedTimeOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.24", NULL },
	{ "2.5.13.29", "integerFirstComponentMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.27", int_first_component_syntaxes },
	{ "2.5.13.3", "caseIgnoreOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.15", dir_string_syntaxes },
	{ "2.5.13.30", "objectIdentifierFirstComponentMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.38", oid_first_component_syntaxes },
	{ "2.5.13.31", "directoryStringFirstComponentMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.15", NULL },
	{ "2.5.13.4", "caseIgnoreSubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", dir_string_syntaxes },
	{ "2.5.13.5", "caseExactMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.15", NULL },
	{ "2.5.13.6", "caseExactOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.15", NULL },
	{ "2.5.13.7", "caseExactSubstringsMatch", MATCH_SUBSTR, NULL, "1.3.6.1.4.1.1466.115.121.1.58", dir_string_syntaxes },
	{ "2.5.13.8", "numericStringMatch", MATCH_EQUALITY, NULL, "1.3.6.1.4.1.1466.115.121.1.36", NULL },
	{ "2.5.13.9", "numericStringOrderingMatch", MATCH_ORDERING, NULL, "1.3.6.1.4.1.1466.115.121.1.36", NULL },

#if 0
	{ "2.5.13.32", "wordMatch", "1.3.6.1.4.1.1466.115.121.1.15", MATCH_EQUALITY, NULL },
	{ "2.5.13.33", "keywordMatch", "1.3.6.1.4.1.1466.115.121.1.15", MATCH_EQUALITY, NULL },
#endif
};

int num_match_rules = nitems(match_rules);

static struct match_rule_alias {
	char	*name;
	char	*oid;
} aliases[] = {
	{ "caseExactIA5SubstringsMatch", "caseExactSubstringsMatch" },
};

const struct match_rule *
match_rule_lookup(const char *oid_or_name)
{
	unsigned int		 i;

	for (i = 0; i < nitems(match_rules); i++) {
		if (strcasecmp(oid_or_name, match_rules[i].name) == 0 ||
		    strcmp(oid_or_name, match_rules[i].oid) == 0)
			return &match_rules[i];
	}

	for (i = 0; i < nitems(aliases); i++) {
		if (strcasecmp(oid_or_name, aliases[i].name) == 0)
			return match_rule_lookup(aliases[i].oid);
	}

	return NULL;
}

