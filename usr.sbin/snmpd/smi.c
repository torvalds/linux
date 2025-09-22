/*	$OpenBSD: smi.c,v 1.40 2024/02/06 12:44:27 martijn Exp $	*/

/*
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/tree.h>
#include <sys/time.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include <ber.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <vis.h>

#include "log.h"
#include "mib.h"
#include "smi.h"
#include "snmp.h"
#include "snmpd.h"

struct oid {
	struct ber_oid		 o_id;
#define o_oid			 o_id.bo_id
#define o_oidlen		 o_id.bo_n

	char			*o_name;

	RB_ENTRY(oid)		 o_element;
	RB_ENTRY(oid)		 o_keyword;
};

void		 smi_mibtree(struct oid *);
struct oid	*smi_findkey(char *);
int		 smi_oid_cmp(struct oid *, struct oid *);
int		 smi_key_cmp(struct oid *, struct oid *);

RB_HEAD(oidtree, oid);
RB_PROTOTYPE(oidtree, oid, o_element, smi_oid_cmp);
struct oidtree smi_oidtree;
static struct oid smi_objects[] = MIB_TREE;

RB_HEAD(keytree, oid);
RB_PROTOTYPE(keytree, oid, o_keyword, smi_key_cmp);
struct keytree smi_keytree;

u_long
smi_getticks(void)
{
	struct timeval	 now, run;
	u_long		 ticks;

	gettimeofday(&now, NULL);
	if (timercmp(&now, &snmpd_env->sc_starttime, <=))
		return (0);
	timersub(&now, &snmpd_env->sc_starttime, &run);
	ticks = run.tv_sec * 100;
	if (run.tv_usec)
		ticks += run.tv_usec / 10000;

	return (ticks);
}

char *
smi_oid2string(struct ber_oid *o, char *buf, size_t len, size_t skip)
{
	char		 str[256];
	struct oid	*value, key;
	size_t		 i, lookup = 1;

	bzero(buf, len);
	bzero(&key, sizeof(key));
	bcopy(o, &key.o_id, sizeof(struct ber_oid));

	if (snmpd_env->sc_flags & SNMPD_F_NONAMES)
		lookup = 0;

	for (i = 0; i < o->bo_n; i++) {
		key.o_oidlen = i + 1;
		if (lookup && skip > i)
			continue;
		if (lookup &&
		    (value = RB_FIND(oidtree, &smi_oidtree, &key)) != NULL)
			snprintf(str, sizeof(str), "%s", value->o_name);
		else
			snprintf(str, sizeof(str), "%d", key.o_oid[i]);
		strlcat(buf, str, len);
		if (i < (o->bo_n - 1))
			strlcat(buf, ".", len);
	}

	return (buf);
}

int
smi_string2oid(const char *oidstr, struct ber_oid *o)
{
	char			*sp, *p, str[BUFSIZ];
	const char		*errstr;
	struct oid		*oid;
	struct ber_oid		 ko;

	if (strlcpy(str, oidstr, sizeof(str)) >= sizeof(str))
		return (-1);
	bzero(o, sizeof(*o));

	/*
	 * Parse OID strings in the common form n.n.n or n-n-n.
	 * Based on ober_string2oid with additional support for symbolic names.
	 */
	for (p = sp = str; p != NULL; sp = p) {
		if ((p = strpbrk(p, ".-")) != NULL)
			*p++ = '\0';
		if ((oid = smi_findkey(sp)) != NULL) {
			bcopy(&oid->o_id, &ko, sizeof(ko));
			if (o->bo_n && ober_oid_cmp(o, &ko) != 2)
				return (-1);
			bcopy(&ko, o, sizeof(*o));
			errstr = NULL;
		} else {
			o->bo_id[o->bo_n++] =
			    strtonum(sp, 0, UINT_MAX, &errstr);
		}
		if (errstr || o->bo_n > BER_MAX_OID_LEN)
			return (-1);
	}

	return (0);
}

const char *
smi_insert(struct ber_oid *oid, const char *name)
{
	struct oid	 *object;

	if ((object = calloc(1, sizeof(*object))) == NULL)
		return strerror(errno);

	object->o_id = *oid;
	if ((object->o_name = strdup(name)) == NULL) {
		free(object);
		return strerror(errno);
	}

	if (RB_INSERT(oidtree, &smi_oidtree, object) != NULL) {
		free(object->o_name);
		free(object);
		return "duplicate oid";
	}

	return NULL;
}

void
smi_mibtree(struct oid *oids)
{
	struct oid	*oid, *decl;
	size_t		 i;

	for (i = 0; oids[i].o_oid[0] != 0; i++) {
		oid = &oids[i];
		if (oid->o_name != NULL) {
			RB_INSERT(oidtree, &smi_oidtree, oid);
			RB_INSERT(keytree, &smi_keytree, oid);
			continue;
		}
		decl = RB_FIND(oidtree, &smi_oidtree, oid);
		if (decl == NULL)
			fatalx("smi_mibtree: undeclared MIB");
	}
}

int
smi_init(void)
{
	/* Initialize the Structure of Managed Information (SMI) */
	RB_INIT(&smi_oidtree);
	smi_mibtree(smi_objects);
	return (0);
}

struct oid *
smi_findkey(char *name)
{
	struct oid	oid;
	if (name == NULL)
		return (NULL);
	oid.o_name = name;
	return (RB_FIND(keytree, &smi_keytree, &oid));
}

#ifdef DEBUG
void
smi_debug_elements(struct ber_element *root)
{
	static int	 indent = 0;
	char		*value;
	int		 constructed;

	/* calculate lengths */
	ober_calc_len(root);

	switch (root->be_encoding) {
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
		constructed = root->be_encoding;
		break;
	default:
		constructed = 0;
		break;
	}

	fprintf(stderr, "%*slen %lu ", indent, "", root->be_len);
	switch (root->be_class) {
	case BER_CLASS_UNIVERSAL:
		fprintf(stderr, "class: universal(%u) type: ", root->be_class);
		switch (root->be_type) {
		case BER_TYPE_EOC:
			fprintf(stderr, "end-of-content");
			break;
		case BER_TYPE_INTEGER:
			fprintf(stderr, "integer");
			break;
		case BER_TYPE_BITSTRING:
			fprintf(stderr, "bit-string");
			break;
		case BER_TYPE_OCTETSTRING:
			fprintf(stderr, "octet-string");
			break;
		case BER_TYPE_NULL:
			fprintf(stderr, "null");
			break;
		case BER_TYPE_OBJECT:
			fprintf(stderr, "object");
			break;
		case BER_TYPE_ENUMERATED:
			fprintf(stderr, "enumerated");
			break;
		case BER_TYPE_SEQUENCE:
			fprintf(stderr, "sequence");
			break;
		case BER_TYPE_SET:
			fprintf(stderr, "set");
			break;
		}
		break;
	case BER_CLASS_APPLICATION:
		fprintf(stderr, "class: application(%u) type: ",
		    root->be_class);
		switch (root->be_type) {
		case SNMP_T_IPADDR:
			fprintf(stderr, "ipaddr");
			break;
		case SNMP_T_COUNTER32:
			fprintf(stderr, "counter32");
			break;
		case SNMP_T_GAUGE32:
			fprintf(stderr, "gauge32");
			break;
		case SNMP_T_TIMETICKS:
			fprintf(stderr, "timeticks");
			break;
		case SNMP_T_OPAQUE:
			fprintf(stderr, "opaque");
			break;
		case SNMP_T_COUNTER64:
			fprintf(stderr, "counter64");
			break;
		}
		break;
	case BER_CLASS_CONTEXT:
		fprintf(stderr, "class: context(%u) type: ",
		    root->be_class);
		switch (root->be_type) {
		case SNMP_C_GETREQ:
			fprintf(stderr, "getreq");
			break;
		case SNMP_C_GETNEXTREQ:
			fprintf(stderr, "getnextreq");
			break;
		case SNMP_C_RESPONSE:
			fprintf(stderr, "response");
			break;
		case SNMP_C_SETREQ:
			fprintf(stderr, "setreq");
			break;
		case SNMP_C_TRAP:
			fprintf(stderr, "trap");
			break;
		case SNMP_C_GETBULKREQ:
			fprintf(stderr, "getbulkreq");
			break;
		case SNMP_C_INFORMREQ:
			fprintf(stderr, "informreq");
			break;
		case SNMP_C_TRAPV2:
			fprintf(stderr, "trapv2");
			break;
		case SNMP_C_REPORT:
			fprintf(stderr, "report");
			break;
		}
		break;
	case BER_CLASS_PRIVATE:
		fprintf(stderr, "class: private(%u) type: ", root->be_class);
		break;
	default:
		fprintf(stderr, "class: <INVALID>(%u) type: ", root->be_class);
		break;
	}
	fprintf(stderr, "(%u) encoding %u ",
	    root->be_type, root->be_encoding);

	if ((value = smi_print_element(root)) == NULL)
		goto invalid;

	switch (root->be_encoding) {
	case BER_TYPE_INTEGER:
	case BER_TYPE_ENUMERATED:
		fprintf(stderr, "value %s", value);
		break;
	case BER_TYPE_BITSTRING:
		fprintf(stderr, "hexdump %s", value);
		break;
	case BER_TYPE_OBJECT:
		fprintf(stderr, "oid %s", value);
		break;
	case BER_TYPE_OCTETSTRING:
		if (root->be_class == BER_CLASS_APPLICATION &&
		    root->be_type == SNMP_T_IPADDR) {
			fprintf(stderr, "addr %s", value);
		} else {
			fprintf(stderr, "string %s", value);
		}
		break;
	case BER_TYPE_NULL:	/* no payload */
	case BER_TYPE_EOC:
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
	default:
		fprintf(stderr, "%s", value);
		break;
	}

 invalid:
	if (value == NULL)
		fprintf(stderr, "<INVALID>");
	else
		free(value);
	fprintf(stderr, "\n");

	if (constructed)
		root->be_encoding = constructed;

	if (constructed && root->be_sub) {
		indent += 2;
		smi_debug_elements(root->be_sub);
		indent -= 2;
	}
	if (root->be_next)
		smi_debug_elements(root->be_next);
}
#endif

/* Keep around so trap handle scripts don't break */
char *
smi_print_element_legacy(struct ber_element *root)
{
	char		*str = NULL, *buf, *p;
	size_t		 len, i;
	long long	 v;
	struct ber_oid	 o;
	char		 strbuf[BUFSIZ];

	switch (root->be_encoding) {
	case BER_TYPE_INTEGER:
	case BER_TYPE_ENUMERATED:
		if (ober_get_integer(root, &v) == -1)
			goto fail;
		if (asprintf(&str, "%lld", v) == -1)
			goto fail;
		break;
	case BER_TYPE_BITSTRING:
		if (ober_get_bitstring(root, (void *)&buf, &len) == -1)
			goto fail;
		if ((str = calloc(1, len * 2 + 1)) == NULL)
			goto fail;
		for (p = str, i = 0; i < len; i++) {
			snprintf(p, 3, "%02x", buf[i]);
			p += 2;
		}
		break;
	case BER_TYPE_OBJECT:
		if (ober_get_oid(root, &o) == -1)
			goto fail;
		if (asprintf(&str, "%s",
		    smi_oid2string(&o, strbuf, sizeof(strbuf), 0)) == -1)
			goto fail;
		break;
	case BER_TYPE_OCTETSTRING:
		if (ober_get_string(root, &buf) == -1)
			goto fail;
		if (root->be_class == BER_CLASS_APPLICATION &&
		    root->be_type == SNMP_T_IPADDR) {
			if (asprintf(&str, "%s",
			    inet_ntoa(*(struct in_addr *)buf)) == -1)
				goto fail;
		} else {
			if ((p = reallocarray(NULL, 4, root->be_len + 1)) == NULL)
				goto fail;
			strvisx(p, buf, root->be_len, VIS_NL);
			if (asprintf(&str, "\"%s\"", p) == -1) {
				free(p);
				goto fail;
			}
			free(p);
		}
		break;
	case BER_TYPE_NULL:	/* no payload */
	case BER_TYPE_EOC:
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
	default:
		str = strdup("");
		break;
	}

	return (str);

 fail:
	free(str);
	return (NULL);
}

char *
smi_print_element(struct ber_element *root)
{
	char		*str = NULL, *buf, *p;
	long long	 v;
	struct ber_oid	 o;
	char		 strbuf[BUFSIZ];

	switch (root->be_class) {
	case BER_CLASS_UNIVERSAL:
		switch (root->be_type) {
		case BER_TYPE_INTEGER:
			if (ober_get_integer(root, &v) == -1)
				goto fail;
			if (asprintf(&str, "%lld", v) == -1)
				goto fail;
			break;
		case BER_TYPE_OBJECT:
			if (ober_get_oid(root, &o) == -1)
				goto fail;
			if (asprintf(&str, "%s", mib_oid2string(&o, strbuf,
			    sizeof(strbuf), snmpd_env->sc_oidfmt)) == -1)
				goto fail;
			break;
		case BER_TYPE_OCTETSTRING:
			if (ober_get_string(root, &buf) == -1)
				goto fail;
			p = reallocarray(NULL, 4, root->be_len + 1);
			if (p == NULL)
				goto fail;
			strvisx(p, buf, root->be_len, VIS_NL);
			if (asprintf(&str, "\"%s\"", p) == -1) {
				free(p);
				goto fail;
			}
			free(p);
			break;
		case BER_TYPE_NULL:
			if (asprintf(&str, "null") == -1)
				goto fail;
			break;
		default:
			/* Should not happen in a valid SNMP packet */
			if (asprintf(&str, "[U/%u]", root->be_type) == -1)
				goto fail;
			break;
		}
		break;
	case BER_CLASS_APPLICATION:
		switch (root->be_type) {
		case SNMP_T_IPADDR:
			if (ober_get_string(root, &buf) == -1)
				goto fail;
			if (asprintf(&str, "%s",
			    inet_ntoa(*(struct in_addr *)buf)) == -1)
					goto fail;
			break;
		case SNMP_T_COUNTER32:
			if (ober_get_integer(root, &v) == -1)
				goto fail;
			if (asprintf(&str, "%lld(c32)", v) == -1)
				goto fail;
			break;
		case SNMP_T_GAUGE32:
			if (ober_get_integer(root, &v) == -1)
				goto fail;
			if (asprintf(&str, "%lld(g32)", v) == -1)
				goto fail;
			break;
		case SNMP_T_TIMETICKS:
			if (ober_get_integer(root, &v) == -1)
				goto fail;
			if (asprintf(&str, "%lld.%llds", v/100, v%100) == -1)
				goto fail;
			break;
		case SNMP_T_OPAQUE:
			if (ober_get_string(root, &buf) == -1)
				goto fail;
			p = reallocarray(NULL, 4, root->be_len + 1);
			if (p == NULL)
				goto fail;
			strvisx(p, buf, root->be_len, VIS_NL);
			if (asprintf(&str, "\"%s\"(opaque)", p) == -1) {
				free(p);
				goto fail;
			}
			free(p);
			break;
		case SNMP_T_COUNTER64:
			if (ober_get_integer(root, &v) == -1)
				goto fail;
			if (asprintf(&str, "%lld(c64)", v) == -1)
				goto fail;
			break;
		default:
			/* Should not happen in a valid SNMP packet */
			if (asprintf(&str, "[A/%u]", root->be_type) == -1)
				goto fail;
			break;
		}
		break;
	case BER_CLASS_CONTEXT:
		switch (root->be_type) {
		case SNMP_V_NOSUCHOBJECT:
			str = strdup("noSuchObject");
			break;
		case SNMP_V_NOSUCHINSTANCE:
			str = strdup("noSuchInstance");
			break;
		case SNMP_V_ENDOFMIBVIEW:
			str = strdup("endOfMibView");
			break;
		default:
			/* Should not happen in a valid SNMP packet */
			if (asprintf(&str, "[C/%u]", root->be_type) == -1)
				goto fail;
			break;
		}
		break;
	default:
		/* Should not happen in a valid SNMP packet */
		if (asprintf(&str, "[%hhu/%u]", root->be_class,
		    root->be_type) == -1)
			goto fail;
		break;
	}

	return (str);

 fail:
	free(str);
	return (NULL);
}

unsigned int
smi_application(struct ber_element *elm)
{
	if (elm->be_class != BER_CLASS_APPLICATION)
		return (BER_TYPE_OCTETSTRING);

	switch (elm->be_type) {
	case SNMP_T_IPADDR:
		return (BER_TYPE_OCTETSTRING);
	case SNMP_T_COUNTER32:
	case SNMP_T_GAUGE32:
	case SNMP_T_TIMETICKS:
	case SNMP_T_OPAQUE:
	case SNMP_T_COUNTER64:
		return (BER_TYPE_INTEGER);
	default:
		break;
	}
	return (BER_TYPE_OCTETSTRING);
}

int
smi_oid_cmp(struct oid *a, struct oid *b)
{
	return ober_oid_cmp(&a->o_id, &b->o_id);
}

RB_GENERATE(oidtree, oid, o_element, smi_oid_cmp);

int
smi_key_cmp(struct oid *a, struct oid *b)
{
	if (a->o_name == NULL || b->o_name == NULL)
		return (-1);
	return (strcasecmp(a->o_name, b->o_name));
}

RB_GENERATE(keytree, oid, o_keyword, smi_key_cmp);
