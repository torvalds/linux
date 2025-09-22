/*	$OpenBSD: attributes.c,v 1.7 2021/12/20 13:26:11 claudio Exp $ */

/*
 * Copyright (c) 2009 Martin Hedenfalk <martin@bzero.se>
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

#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <string.h>
#include <time.h>

#include "ldapd.h"
#include "log.h"

struct ber_element *
ldap_get_attribute(struct ber_element *entry, const char *attr)
{
	char			*s;
	struct ber_element	*elm, *a;

	assert(entry);
	assert(attr);
	if (entry->be_encoding != BER_TYPE_SEQUENCE)
		return NULL;

	for (elm = entry->be_sub; elm != NULL; elm = elm->be_next) {
		a = elm->be_sub;
		if (a && ober_get_string(a, &s) == 0 && strcasecmp(s, attr) == 0)
			return a;
	}

	return NULL;
}

struct ber_element *
ldap_find_attribute(struct ber_element *entry, struct attr_type *at)
{
	struct ber_element	*elm = NULL;
	struct name		*an;

	SLIST_FOREACH(an, at->names, next) {
		if ((elm = ldap_get_attribute(entry, an->name)) != NULL)
			return elm;
	}
	if (an == NULL)
		elm = ldap_get_attribute(entry, at->oid);

	return elm;
}

struct ber_element *
ldap_find_value(struct ber_element *elm, const char *value)
{
	char			*s;
	struct ber_element	*a;

	if (elm == NULL)
		return NULL;

	for (a = elm->be_sub; a != NULL; a = a->be_next) {
		if (ober_get_string(a, &s) == 0 && strcasecmp(s, value) == 0)
			return a;
	}

	return NULL;
}

struct ber_element *
ldap_add_attribute(struct ber_element *entry, const char *attr,
	struct ber_element *value_set)
{
	struct ber_element	*elm, *a, *last;

	assert(entry);
	assert(attr);
	assert(value_set);

	if (entry->be_encoding != BER_TYPE_SEQUENCE) {
		log_warnx("entries should be a sequence");
		return NULL;
	}

	if (value_set->be_type != BER_TYPE_SET) {
		log_warnx("values should be a set");
		return NULL;
	}

	last = entry->be_sub;
	if (last == NULL)
		last = entry;
	else while (last != NULL && last->be_next != NULL)
		last = last->be_next;

	if ((elm = ober_add_sequence(last)) == NULL)
		return NULL;
	if ((a = ober_add_string(elm, attr)) == NULL) {
		ober_free_elements(elm);
		return NULL;
	}
	ober_link_elements(a, value_set);

	return elm;
}

int
ldap_set_values(struct ber_element *elm, struct ber_element *vals)
{
	char			*attr;
	struct ber_element	*old_vals;

	assert(elm);
	assert(vals);
	assert(vals->be_sub);

	if (ober_scanf_elements(elm, "se(", &attr, &old_vals) != 0) {
		log_warnx("failed to parse element");
		return -1;
	}

	ober_free_elements(old_vals->be_sub);
	old_vals->be_sub = NULL;
	ober_link_elements(old_vals, vals->be_sub);

	vals->be_sub = NULL;
	ober_free_elements(vals);

	return 0;
}

int
ldap_merge_values(struct ber_element *elm, struct ber_element *vals)
{
	char			*attr;
	struct ber_element	*old_vals, *last;

	assert(elm);
	assert(vals);
	assert(vals->be_type == BER_TYPE_SET);
	assert(vals->be_sub);

	if (ober_scanf_elements(elm, "se(", &attr, &old_vals) != 0) {
		log_warnx("failed to parse element");
		return -1;
	}

	last = old_vals->be_sub;
	while (last && last->be_next)
		last = last->be_next;

	ober_link_elements(last, vals->be_sub);

	vals->be_sub = NULL;
	ober_free_elements(vals);

	return 0;
}


int
ldap_del_attribute(struct ber_element *entry, const char *attrdesc)
{
	struct ber_element	*attr, *prev = NULL;
	char			*s;

	assert(entry);
	assert(attrdesc);

	attr = entry->be_sub;
	while (attr) {
		if (ober_scanf_elements(attr, "{s", &s) != 0) {
			log_warnx("failed to parse attribute");
			return -1;
		}

		if (strcasecmp(s, attrdesc) == 0) {
			if (prev == NULL)
				entry->be_sub = attr->be_next;
			else
				prev->be_next = attr->be_next;
			attr->be_next = NULL;
			ober_free_elements(attr);
			break;
		}

		prev = attr;
		attr = attr->be_next;
	}

	return 0;
}

int
ldap_del_values(struct ber_element *elm, struct ber_element *vals)
{
	char			*attr;
	struct ber_element	*old_vals, *v, *x, *prev, *next;
	struct ber_element	*removed;
	int			removed_p;
	assert(elm);
	assert(vals);
	assert(vals->be_sub);

	if (ober_scanf_elements(elm, "se(", &attr, &old_vals) != 0) {
		log_warnx("failed to parse element");
		return -1;
	}

	prev = old_vals;
	removed_p = 0;
	for (v = old_vals->be_sub; v; v = next) {
		next = v->be_next;

		for (x = vals->be_sub; x; x = x->be_next) {
			if (x && v->be_len == x->be_len &&
			    memcmp(v->be_val, x->be_val, x->be_len) == 0) {
				removed = ober_unlink_elements(prev);
				ober_link_elements(prev, removed->be_next);
				ober_free_element(removed);
				removed_p = 1;
				break;
			}
		}
		if (removed_p) {
			removed_p = 0;
		} else {
			prev = v;
		}
	}

	if (old_vals->be_sub == NULL)
		return 1;

	return 0;
}

char *
ldap_strftime(time_t tm)
{
	static char	 tmbuf[16];
	struct tm	*gmt = gmtime(&tm);

	strftime(tmbuf, sizeof(tmbuf), "%Y%m%d%H%M%SZ", gmt);
	return tmbuf;
}

char *
ldap_now(void)
{
	return ldap_strftime(time(0));
}

