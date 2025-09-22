/*	$OpenBSD: validate.c,v 1.13 2021/12/20 13:18:29 claudio Exp $ */

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

#include <stdlib.h>
#include <string.h>

#include "ldapd.h"
#include "log.h"

static int
validate_required_attributes(struct ber_element *entry, struct object *obj)
{
	struct attr_ptr		*ap;
	struct attr_type	*at;

	if (obj->must == NULL)
		return LDAP_SUCCESS;

	SLIST_FOREACH(ap, obj->must, next) {
		at = ap->attr_type;

		if (ldap_find_attribute(entry, at) == NULL) {
			log_debug("missing required attribute %s",
			    ATTR_NAME(at));
			return LDAP_OBJECT_CLASS_VIOLATION;
		}
	}

	return LDAP_SUCCESS;
}

static int
validate_attribute(struct attr_type *at, struct ber_element *vals)
{
	int			 nvals = 0;
	struct ber_element	*elm;
	char			*val;

	if (vals == NULL) {
		log_debug("missing values");
		return LDAP_OTHER;
	}

	if (vals->be_type != BER_TYPE_SET) {
		log_debug("values should be a set");
		return LDAP_OTHER;
	}

	for (elm = vals->be_sub; elm != NULL; elm = elm->be_next) {
		if (ober_get_string(elm, &val) == -1) {
			log_debug("attribute value not an octet-string");
			return LDAP_PROTOCOL_ERROR;
		}

		if (++nvals > 1 && at->single) {
			log_debug("multiple values for single-valued"
			    " attribute %s", ATTR_NAME(at));
			return LDAP_CONSTRAINT_VIOLATION;
		}

		if (at->syntax->is_valid != NULL &&
		    !at->syntax->is_valid(conf->schema, val, elm->be_len)) {
			log_debug("%s: invalid syntax", ATTR_NAME(at));
			log_debug("syntax = %s", at->syntax->desc);
			log_debug("value: [%.*s]", (int)elm->be_len, val);
			return LDAP_INVALID_SYNTAX;
		}
	}

	/* There must be at least one value in an attribute. */
	if (nvals == 0) {
		log_debug("missing value in attribute %s", ATTR_NAME(at));
		return LDAP_CONSTRAINT_VIOLATION;
	}

	/* FIXME: validate that values are unique */

	return LDAP_SUCCESS;
}

/* FIXME: doesn't handle escaped characters.
 */
static int
validate_dn(const char *dn, struct ber_element *entry)
{
	char			*copy;
	char			*sup_dn, *na, *dv, *p;
	struct namespace	*ns;
	struct attr_type	*at;
	struct ber_element	*vals;

	if ((copy = strdup(dn)) == NULL)
		return LDAP_OTHER;

	sup_dn = strchr(copy, ',');
	if (sup_dn++ == NULL)
		sup_dn = strrchr(copy, '\0');

	/* Validate naming attributes and distinguished values in the RDN.
	 */
	p = copy;
	for (;p < sup_dn;) {
		na = p;
		p = na + strcspn(na, "=");
		if (p == na || p >= sup_dn) {
			free(copy);
			return LDAP_INVALID_DN_SYNTAX;
		}
		*p = '\0';
		dv = p + 1;
		p = dv + strcspn(dv, "+,");
		if (p == dv) {
			free(copy);
			return LDAP_INVALID_DN_SYNTAX;
		}
		*p++ = '\0';

		if ((at = lookup_attribute(conf->schema, na)) == NULL) {
			log_debug("attribute %s not defined in schema", na);
			goto fail;
		}
		if (at->usage != USAGE_USER_APP) {
			log_debug("naming attribute %s is operational", na);
			goto fail;
		}
		if (at->collective) {
			log_debug("naming attribute %s is collective", na);
			goto fail;
		}
		if (at->obsolete) {
			log_debug("naming attribute %s is obsolete", na);
			goto fail;
		}
		if (at->equality == NULL) {
			log_debug("naming attribute %s doesn't define equality",
			    na);
			goto fail;
		}
		if ((vals = ldap_find_attribute(entry, at)) == NULL) {
			log_debug("missing distinguished value for %s", na);
			goto fail;
		}
		if (ldap_find_value(vals->be_next, dv) == NULL) {
			log_debug("missing distinguished value %s"
			    " in naming attribute %s", dv, na);
			goto fail;
		}
	}

	/* Check that the RDN immediate superior exists, or it is a
	 * top-level namespace.
	 */
	if (*sup_dn != '\0') {
		TAILQ_FOREACH(ns, &conf->namespaces, next) {
			if (strcmp(dn, ns->suffix) == 0)
				goto done;
		}
		ns = namespace_for_base(sup_dn);
		if (ns == NULL || !namespace_exists(ns, sup_dn)) {
			free(copy);
			return LDAP_NO_SUCH_OBJECT;
		}
	}

done:
	free(copy);
	return LDAP_SUCCESS;
fail:
	free(copy);
	return LDAP_NAMING_VIOLATION;
}

static int
has_attribute(struct attr_type *at, struct attr_list *alist)
{
	struct attr_ptr		*ap;

	if (alist == NULL)
		return 0;

	SLIST_FOREACH(ap, alist, next) {
		if (at == ap->attr_type)
			return 1;
	}
	return 0;
}

/* Validate that the attribute type is allowed by any object class.
 */
static int
validate_allowed_attribute(struct attr_type *at, struct obj_list *olist)
{
	struct object		*obj;
	struct obj_ptr		*optr;

	if (olist == NULL)
		return LDAP_OBJECT_CLASS_VIOLATION;

	SLIST_FOREACH(optr, olist, next) {
		obj = optr->object;

		if (has_attribute(at, obj->may) ||
		    has_attribute(at, obj->must))
			return LDAP_SUCCESS;

		if (validate_allowed_attribute(at, obj->sup) == LDAP_SUCCESS)
			return LDAP_SUCCESS;
	}

	return LDAP_OBJECT_CLASS_VIOLATION;
}

static void
olist_push(struct obj_list *olist, struct object *obj)
{
	struct obj_ptr		*optr, *sup;

	SLIST_FOREACH(optr, olist, next)
		if (optr->object == obj)
			return;

	if ((optr = calloc(1, sizeof(*optr))) == NULL)
		return;
	optr->object = obj;
	SLIST_INSERT_HEAD(olist, optr, next);

	/* Expand the list of object classes along the superclass chain.
	 */
	if (obj->sup != NULL)
		SLIST_FOREACH(sup, obj->sup, next)
			olist_push(olist, sup->object);
}

static void
olist_free(struct obj_list *olist)
{
	struct obj_ptr		*optr;

	if (olist == NULL)
		return;

	while ((optr = SLIST_FIRST(olist)) != NULL) {
		SLIST_REMOVE_HEAD(olist, next);
		free(optr);
	}

	free(olist);
}

/* Check if sup is a superior object class to obj.
 */
static int
is_super(struct object *sup, struct object *obj)
{
	struct obj_ptr	*optr;

	if (sup == NULL || obj->sup == NULL)
		return 0;

	SLIST_FOREACH(optr, obj->sup, next)
		if (optr->object == sup || is_super(sup, optr->object))
			return 1;

	return 0;
}

int
validate_entry(const char *dn, struct ber_element *entry, int relax)
{
	int			 rc, extensible = 0;
	char			*s;
	struct ber_element	*objclass, *a, *vals;
	struct object		*obj, *structural_obj = NULL;
	struct attr_type	*at;
	struct obj_list		*olist = NULL;
	struct obj_ptr		*optr, *optr2;

	if (relax)
		goto rdn;

	/* There must be an objectClass attribute.
	 */
	objclass = ldap_get_attribute(entry, "objectClass");
	if (objclass == NULL) {
		log_debug("missing objectClass attribute");
		return LDAP_OBJECT_CLASS_VIOLATION;
	}

	if ((olist = calloc(1, sizeof(*olist))) == NULL)
		return LDAP_OTHER;
	SLIST_INIT(olist);

	/* Check objectClass(es) against schema.
	 */
	objclass = objclass->be_next;		/* skip attribute description */
	for (a = objclass->be_sub; a != NULL; a = a->be_next) {
		if (ober_get_string(a, &s) != 0) {
			log_debug("invalid ObjectClass encoding");
			rc = LDAP_INVALID_SYNTAX;
			goto done;
		}

		if ((obj = lookup_object(conf->schema, s)) == NULL) {
			log_debug("objectClass %s not defined in schema", s);
			rc = LDAP_NAMING_VIOLATION;
			goto done;
		}

		if (obj->kind == KIND_STRUCTURAL) {
			if (structural_obj != NULL) {
				if (is_super(structural_obj, obj))
					structural_obj = obj;
				else if (!is_super(obj, structural_obj)) {
					log_debug("multiple structural"
					    " object classes");
					rc = LDAP_OBJECT_CLASS_VIOLATION;
					goto done;
				}
			} else
				structural_obj = obj;
		}

		olist_push(olist, obj);

                /* RFC4512, section 4.3:
		 * "The 'extensibleObject' auxiliary object class allows
		 * entries that belong to it to hold any user attribute."
                 */
                if (strcmp(obj->oid, "1.3.6.1.4.1.1466.101.120.111") == 0)
                        extensible = 1;
        }

	/* Must have exactly one structural object class.
	 */
	if (structural_obj == NULL) {
		log_debug("no structural object class defined");
		rc = LDAP_OBJECT_CLASS_VIOLATION;
		goto done;
	}

	/* "An entry cannot belong to an abstract object class
	 *  unless it belongs to a structural or auxiliary class that
	 *  inherits from that abstract class."
	 */
        SLIST_FOREACH(optr, olist, next) {
		if (optr->object->kind != KIND_ABSTRACT)
			continue;

		/* Check the structural object class. */
		if (is_super(optr->object, structural_obj))
			continue;

		/* Check all auxiliary object classes. */
		SLIST_FOREACH(optr2, olist, next) {
			if (optr2->object->kind != KIND_AUXILIARY)
				continue;
			if (is_super(optr->object, optr2->object))
				break;
		}

		if (optr2 == NULL) {
			/* No subclassed object class found. */
			log_debug("abstract class '%s' not subclassed",
			    OBJ_NAME(optr->object));
			rc = LDAP_OBJECT_CLASS_VIOLATION;
			goto done;
		}
	}

	/* Check all required attributes.
	 */
	SLIST_FOREACH(optr, olist, next) {
		rc = validate_required_attributes(entry, optr->object);
		if (rc != LDAP_SUCCESS)
			goto done;
	}

	/* Check all attributes against schema.
	 */
	for (a = entry->be_sub; a != NULL; a = a->be_next) {
		if (ober_scanf_elements(a, "{se{", &s, &vals) != 0) {
			log_debug("invalid attribute encoding");
			rc = LDAP_INVALID_SYNTAX;
			goto done;
		}
		if ((at = lookup_attribute(conf->schema, s)) == NULL) {
			log_debug("attribute %s not defined in schema", s);
			rc = LDAP_NAMING_VIOLATION;
			goto done;
		}
		if ((rc = validate_attribute(at, vals)) != LDAP_SUCCESS)
			goto done;
		if (!extensible && at->usage == USAGE_USER_APP &&
		    (rc = validate_allowed_attribute(at, olist)) != LDAP_SUCCESS) {
			log_debug("%s not allowed by any object class",
			    ATTR_NAME(at));
			goto done;
		}
	}

rdn:
	rc = validate_dn(dn, entry);

done:
	olist_free(olist);
	return rc;
}

