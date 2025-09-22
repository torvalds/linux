/*	$OpenBSD: syntax.c,v 1.5 2017/05/28 15:48:49 jmatthew Exp $ */

/*
 * Copyright (c) 2010 Martin Hedenfalk <martin@bzero.se>
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "schema.h"
#include "uuid.h"

#define SYNTAX_DECL(TYPE) \
	static int syntax_is_##TYPE(struct schema *schema, char *value, size_t len)

SYNTAX_DECL(bit_string);
SYNTAX_DECL(boolean);
SYNTAX_DECL(country);
SYNTAX_DECL(directory_string);
SYNTAX_DECL(dn);
SYNTAX_DECL(gentime);
SYNTAX_DECL(ia5_string);
SYNTAX_DECL(integer);
SYNTAX_DECL(numeric_string);
SYNTAX_DECL(octet_string);
SYNTAX_DECL(oid);
SYNTAX_DECL(printable_string);
SYNTAX_DECL(utctime);
SYNTAX_DECL(uuid);

static struct syntax syntaxes[] = {
	/*
	 * Keep these sorted.
	 */
	{ "1.3.6.1.1.1.0.0", "NIS netgroup triple", NULL },
	{ "1.3.6.1.1.1.0.1", "Boot parameter", NULL },
	{ "1.3.6.1.1.16.1", "UUID", syntax_is_uuid },
	{ "1.3.6.1.4.1.1466.115.121.1.11", "Country String", syntax_is_country },
	{ "1.3.6.1.4.1.1466.115.121.1.12", "DN", syntax_is_dn },
	{ "1.3.6.1.4.1.1466.115.121.1.14", "Delivery Method", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.15", "Directory String", syntax_is_directory_string },
	{ "1.3.6.1.4.1.1466.115.121.1.16", "DIT Content Rule Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.17", "DIT Structure Rule Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.21", "Enhanced Guide", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.22", "Facsimile Telephone Number", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.23", "Fax", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.24", "Generalized Time", syntax_is_gentime },
	{ "1.3.6.1.4.1.1466.115.121.1.25", "Guide", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.26", "IA5 String", syntax_is_ia5_string },
	{ "1.3.6.1.4.1.1466.115.121.1.27", "INTEGER", syntax_is_integer },
	{ "1.3.6.1.4.1.1466.115.121.1.28", "JPEG", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.3",  "Attribute Type Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.30", "Matching Rule Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.31", "Matching Rule Use Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.34", "Name And Optional UID", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.35", "Name Form Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.36", "Numeric String", syntax_is_numeric_string },
	{ "1.3.6.1.4.1.1466.115.121.1.37", "Object Class Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.38", "OID", syntax_is_oid },
	{ "1.3.6.1.4.1.1466.115.121.1.39", "Other Mailbox", syntax_is_ia5_string },
	{ "1.3.6.1.4.1.1466.115.121.1.40", "Octet String", syntax_is_octet_string },
	{ "1.3.6.1.4.1.1466.115.121.1.41", "Postal Address", syntax_is_directory_string },
	{ "1.3.6.1.4.1.1466.115.121.1.44", "Printable String", syntax_is_printable_string },
	{ "1.3.6.1.4.1.1466.115.121.1.45", "Subtree Specification", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.5",  "Binary", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.50", "Telephone Number", syntax_is_printable_string },
	{ "1.3.6.1.4.1.1466.115.121.1.51", "Teletex Terminal Identifier", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.52", "Telex Number", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.53", "UTC Time", syntax_is_utctime },
	{ "1.3.6.1.4.1.1466.115.121.1.54", "LDAP Syntax Description", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.58", "Substring Assertion", NULL },
	{ "1.3.6.1.4.1.1466.115.121.1.6",  "Bit String", syntax_is_bit_string },
	{ "1.3.6.1.4.1.1466.115.121.1.7",  "Boolean", syntax_is_boolean },
	{ "1.3.6.1.4.1.1466.115.121.1.8",  "Certificate", NULL },

};

static int
syntax_cmp(const void *k, const void *e)
{
	return (strcmp(k, ((const struct syntax *)e)->oid));
}

const struct syntax *
syntax_lookup(const char *oid)
{
	return bsearch(oid, syntaxes, sizeof(syntaxes)/sizeof(syntaxes[0]),
	    sizeof(syntaxes[0]), syntax_cmp);
}

/*
 * A value of the Octet String syntax is a sequence of zero, one, or
 * more arbitrary octets.
 */
static int
syntax_is_octet_string(struct schema *schema, char *value, size_t len)
{
	return 1;
}

/*
 * A string of one or more arbitrary UTF-8 characters.
 */
static int
syntax_is_directory_string(struct schema *schema, char *value, size_t len)
{
	/* FIXME: validate UTF-8 characters. */
	return len >= 1 && *value != '\0';
}

/*
 * A value of the Printable String syntax is a string of one or more
 * latin alphabetic, numeric, and selected punctuation characters as
 * specified by the <PrintableCharacter> rule in Section 3.2.
 *
 *    PrintableCharacter = ALPHA / DIGIT / SQUOTE / LPAREN / RPAREN /
 *                         PLUS / COMMA / HYPHEN / DOT / EQUALS /
 *                         SLASH / COLON / QUESTION / SPACE
 */
static int
syntax_is_printable_string(struct schema *schema, char *value, size_t len)
{
	static char	*special = "'()+,-.=/:? ";
	char		*p;

	for (p = value; len > 0 && *p != '\0'; p++, len--) {
		if (!isalnum((unsigned char)*p) && strchr(special, *p) == NULL)
			return 0;
	}

	return (p != value);
}

/*
 * A value of the IA5 String syntax is a string of zero, one, or more
 * characters from International Alphabet 5 (IA5).
 *   IA5String          = *(%x00-7F)
 */
static int
syntax_is_ia5_string(struct schema *schema, char *value, size_t len)
{
	char		*p;

	for (p = value; *p != '\0'; p++) {
		if ((unsigned char)*p > 0x7F)
			return 0;
	}

	return 1;
}

/*
 * A value of the Integer syntax is a whole number of unlimited magnitude.
 *   Integer = ( HYPHEN LDIGIT *DIGIT ) / number
 *   number  = DIGIT / ( LDIGIT 1*DIGIT )
 */
static int
syntax_is_integer(struct schema *schema, char *value, size_t len)
{
	if (*value == '-')
		value++;
	if (*value == '0')
		return value[1] == '\0';
	for (value++; *value != '\0'; value++)
		if (!isdigit((unsigned char)*value))
			return 0;
	return 1;
}

static int
syntax_is_dn(struct schema *schema, char *value, size_t len)
{
	if (!syntax_is_directory_string(schema, value, len))
		return 0;

	/* FIXME: DN syntax not implemented */

	return 1;
}

static int
syntax_is_oid(struct schema *schema, char *value, size_t len)
{
	char	*symoid = NULL;

	if (len == 0 || *value == '\0')
		return 0;
	if (is_oidstr(value))
		return 1;

	/*
	 * Check for a symbolic OID: object class, attribute type or symoid.
	 */
	if (lookup_object_by_name(schema, value) != NULL ||
	    lookup_attribute_by_name(schema, value) != NULL ||
	    (symoid = lookup_symbolic_oid(schema, value)) != NULL) {
		free(symoid);
		return 1;
	}

	return 0;
}

static int
syntax_is_uuid(struct schema *schema, char *value, size_t len)
{
	int	 i;

	if (len != 36)
		return 0;

#define IS_XDIGITS(n, c)				\
	do {						\
		for (i = 0; i < (n); i++)		\
			if (!isxdigit(*value++))	\
				return 0;		\
		if (*value++ != (c))			\
			return 0;			\
	} while(0)

	IS_XDIGITS(8, '-');
	IS_XDIGITS(4, '-');
	IS_XDIGITS(4, '-');
	IS_XDIGITS(4, '-');
	IS_XDIGITS(12, '\0');

	return 1;
}

/*
 * NumericString = 1*(DIGIT / SPACE)
 */
static int
syntax_is_numeric_string(struct schema *schema, char *value, size_t len)
{
	char	*p;

	for (p = value; *p != '\0'; p++)
		if (!isdigit((unsigned char)*p) || *p != ' ')
			return 0;

	return p != value;
}

static int
syntax_is_time(struct schema *schema, char *value, size_t len, int gen)
{
	int	 n;
	char	*p = value;

#define CHECK_RANGE(min, max) \
	do {						\
		if (!isdigit((unsigned char)p[0]) ||	\
		    !isdigit((unsigned char)p[1]))	\
			return 0;			\
		n = (p[0] - '0') * 10 + (p[1] - '0');	\
		if (n < min || n > max)			\
			return 0;			\
		p += 2;					\
	} while (0)

	if (gen)
		CHECK_RANGE(0, 99);		/* century */
	CHECK_RANGE(0, 99);			/* year */
	CHECK_RANGE(1, 12);			/* month */
	CHECK_RANGE(1, 31);			/* day */
	/* FIXME: should check number of days in month */
	CHECK_RANGE(0, 23);			/* hour */

	if (!gen || isdigit((unsigned char)*p)) {
		CHECK_RANGE(0, 59);		/* minute */
		if (isdigit((unsigned char)*p))
			CHECK_RANGE(0, 59+gen);	/* second or leap-second */
		if (!gen && *p == '\0')
			return 1;
	}
						/* fraction */
	if (!gen && ((*p == ',' || *p == '.') &&
	    !isdigit((unsigned char)*++p)))
		return 0;

	if (*p == '-' || *p == '+') {
		++p;
		CHECK_RANGE(0, 23);		/* hour */
		if (!gen || isdigit((unsigned char)*p))
			CHECK_RANGE(0, 59);	/* minute */
	} else if (*p++ != 'Z')
		return 0;

	return *p == '\0';
}

static int
syntax_is_gentime(struct schema *schema, char *value, size_t len)
{
	return syntax_is_time(schema, value, len, 1);
}

static int
syntax_is_utctime(struct schema *schema, char *value, size_t len)
{
	return syntax_is_time(schema, value, len, 0);
}

static int
syntax_is_country(struct schema *schema, char *value, size_t len)
{
	if (len != 2)
		return 0;
	return syntax_is_printable_string(schema, value, len);
}

static int
syntax_is_bit_string(struct schema *schema, char *value, size_t len)
{
	if (*value++ != '\'')
		return 0;

	for (; *value != '\0'; value++) {
		if (*value == '\'')
			break;
		if (*value != '0' && *value != '1')
			return 0;
	}

	if (++*value != 'B')
		return 0;

	return *value == '\0';
}

static int
syntax_is_boolean(struct schema *schema, char *value, size_t len)
{
	return strcmp(value, "TRUE") == 0 || strcmp(value, "FALSE") == 0;
}

