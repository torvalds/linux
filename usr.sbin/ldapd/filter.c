/*	$OpenBSD: filter.c,v 1.9 2019/10/24 12:39:26 tb Exp $ */

/*
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martinh@openbsd.org>
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

#include <string.h>
#include <stdint.h>

#include "ldapd.h"
#include "log.h"

static int	 ldap_filt_eq(struct ber_element *root, struct plan *plan);
static int	 ldap_filt_subs(struct ber_element *root, struct plan *plan);
static int	 ldap_filt_and(struct ber_element *root, struct plan *plan);
static int	 ldap_filt_or(struct ber_element *root, struct plan *plan);
static int	 ldap_filt_not(struct ber_element *root, struct plan *plan);

static int
ldap_filt_eq(struct ber_element *root, struct plan *plan)
{
	char			*vs;
	struct ber_element	*a, *vals, *v;

	if (plan->undefined)
		return -1;
	else if (plan->adesc != NULL)
		a = ldap_get_attribute(root, plan->adesc);
	else
		a = ldap_find_attribute(root, plan->at);
	if (a == NULL) {
		log_debug("no attribute [%s] found",
		    plan->adesc ? plan->adesc : ATTR_NAME(plan->at));
		return -1;
	}

	vals = a->be_next;
	if (vals == NULL)
		return -1;

	for (v = vals->be_sub; v; v = v->be_next) {
		if (ober_get_string(v, &vs) != 0)
			continue;
		if (strcasecmp(plan->assert.value, vs) == 0)
			return 0;
	}

	return -1;
}

static int
ldap_filt_subs_value(struct ber_element *v, struct ber_element *sub)
{
	int		 class;
	unsigned int	 type;
	const char	*cmpval;
	char		*vs, *p, *end;

	if (ober_get_string(v, &vs) != 0)
		return -1;

	for (; sub; sub = sub->be_next) {
		if (ober_scanf_elements(sub, "ts", &class, &type, &cmpval) != 0)
			return -1;

		if (class != BER_CLASS_CONTEXT)
			return -1;

		switch (type) {
		case LDAP_FILT_SUBS_INIT:
			if (strncasecmp(cmpval, vs, strlen(cmpval)) == 0)
				vs += strlen(cmpval);
			else
				return 1; /* no match */
			break;
		case LDAP_FILT_SUBS_ANY:
			if ((p = strcasestr(vs, cmpval)) != NULL)
				vs = p + strlen(cmpval);
			else
				return 1; /* no match */
			break;
		case LDAP_FILT_SUBS_FIN:
			if (strlen(vs) < strlen(cmpval))
				return 1; /* no match */
			end = vs + strlen(vs) - strlen(cmpval);
			if (strcasecmp(end, cmpval) == 0)
				vs = end + strlen(cmpval);
			else
				return 1; /* no match */
			break;
		default:
			log_warnx("invalid subfilter type %u", type);
			return -1;
		}
	}

	return 0; /* match */
}

static int
ldap_filt_subs(struct ber_element *root, struct plan *plan)
{
	const char		*attr;
	struct ber_element	*a, *v;

	if (plan->undefined)
		return -1;
	else if (plan->adesc != NULL)
		a = ldap_get_attribute(root, plan->adesc);
	else
		a = ldap_find_attribute(root, plan->at);
	if (a == NULL) {
		log_debug("no attribute [%s] found",
		    plan->adesc ? plan->adesc : ATTR_NAME(plan->at));
		return -1;
	}

	if (ober_scanf_elements(a, "s(e", &attr, &v) != 0)
		return -1; /* internal failure, false or undefined? */

	/* Loop through all values, stop if any matches.
	 */
	for (; v; v = v->be_next) {
		/* All substrings must match. */
		switch (ldap_filt_subs_value(v, plan->assert.substring)) {
		case 0:
			return 0;
		case -1:
			return -1;
		default:
			break;
		}
	}

	/* All values checked, no match. */
	return -1;
}

static int
ldap_filt_and(struct ber_element *root, struct plan *plan)
{
	struct plan	*arg;

	TAILQ_FOREACH(arg, &plan->args, next)
		if (ldap_matches_filter(root, arg) != 0)
			return -1;

	return 0;
}

static int
ldap_filt_or(struct ber_element *root, struct plan *plan)
{
	struct plan	*arg;

	TAILQ_FOREACH(arg, &plan->args, next)
		if (ldap_matches_filter(root, arg) == 0)
			return 0;

	return -1;
}

static int
ldap_filt_not(struct ber_element *root, struct plan *plan)
{
	struct plan	*arg;

	TAILQ_FOREACH(arg, &plan->args, next)
		if (ldap_matches_filter(root, arg) != 0)
			return 0;

	return -1;
}

static int
ldap_filt_presence(struct ber_element *root, struct plan *plan)
{
	struct ber_element	*a;

	if (plan->undefined)
		return -1;
	else if (plan->adesc != NULL)
		a = ldap_get_attribute(root, plan->adesc);
	else
		a = ldap_find_attribute(root, plan->at);
	if (a == NULL) {
		log_debug("no attribute [%s] found",
		    plan->adesc ? plan->adesc : ATTR_NAME(plan->at));
		return -1;
	}

	return 0;
}

int
ldap_matches_filter(struct ber_element *root, struct plan *plan)
{
	if (plan == NULL)
		return 0;

	switch (plan->op) {
	case LDAP_FILT_EQ:
	case LDAP_FILT_APPR:
		return ldap_filt_eq(root, plan);
	case LDAP_FILT_SUBS:
		return ldap_filt_subs(root, plan);
	case LDAP_FILT_AND:
		return ldap_filt_and(root, plan);
	case LDAP_FILT_OR:
		return ldap_filt_or(root, plan);
	case LDAP_FILT_NOT:
		return ldap_filt_not(root, plan);
	case LDAP_FILT_PRES:
		return ldap_filt_presence(root, plan);
	default:
		log_warnx("filter type %d not implemented", plan->op);
		return -1;
	}
}

