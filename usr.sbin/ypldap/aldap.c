/*	$FreeBSD$ */
/*	$Id: aldap.c,v 1.32 2016/04/27 10:53:27 schwarze Exp $ */
/*	$OpenBSD: aldap.c,v 1.32 2016/04/27 10:53:27 schwarze Exp $ */

/*
 * Copyright (c) 2008 Alexander Schrijver <aschrijver@openbsd.org>
 * Copyright (c) 2006, 2007 Marc Balmer <mbalmer@openbsd.org>
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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "aldap.h"

#if 0
#define DEBUG
#endif
#define VERSION 3

static struct ber_element	*ldap_parse_search_filter(struct ber_element *,
				    char *);
static struct ber_element	*ldap_do_parse_search_filter(
				    struct ber_element *, char **);
char				**aldap_get_stringset(struct ber_element *);
char				*utoa(char *);
static int			 isu8cont(unsigned char);
char				*parseval(char *, size_t);
int				aldap_create_page_control(struct ber_element *,
				    int, struct aldap_page_control *);

#ifdef DEBUG
void			 ldap_debug_elements(struct ber_element *);
#endif

#ifdef DEBUG
#define DPRINTF(x...)	printf(x)
#define LDAP_DEBUG(x, y)	do { fprintf(stderr, "*** " x "\n"); ldap_debug_elements(y); } while (0)
#else
#define DPRINTF(x...)	do { } while (0)
#define LDAP_DEBUG(x, y)	do { } while (0)
#endif

int
aldap_close(struct aldap *al)
{
	if (close(al->ber.fd) == -1)
		return (-1);

	ber_free(&al->ber);
	free(al);

	return (0);
}

struct aldap *
aldap_init(int fd)
{
	struct aldap *a;

	if ((a = calloc(1, sizeof(*a))) == NULL)
		return NULL;
	a->ber.fd = fd;

	return a;
}

int
aldap_bind(struct aldap *ldap, char *binddn, char *bindcred)
{
	struct ber_element *root = NULL, *elm;
	int error;

	if (binddn == NULL)
		binddn = "";
	if (bindcred == NULL)
		bindcred = "";

	if ((root = ber_add_sequence(NULL)) == NULL)
		goto fail;

	elm = ber_printf_elements(root, "d{tdsst", ++ldap->msgid, BER_CLASS_APP,
	    (unsigned long)LDAP_REQ_BIND, VERSION, binddn, bindcred,
	    BER_CLASS_CONTEXT, (unsigned long)LDAP_AUTH_SIMPLE);
	if (elm == NULL)
		goto fail;

	LDAP_DEBUG("aldap_bind", root);

	error = ber_write_elements(&ldap->ber, root);
	ber_free_elements(root);
	root = NULL;
	if (error == -1)
		goto fail;

	return (ldap->msgid);
fail:
	if (root != NULL)
		ber_free_elements(root);

	ldap->err = ALDAP_ERR_OPERATION_FAILED;
	return (-1);
}

int
aldap_unbind(struct aldap *ldap)
{
	struct ber_element *root = NULL, *elm;
	int error;

	if ((root = ber_add_sequence(NULL)) == NULL)
		goto fail;
	elm = ber_printf_elements(root, "d{t", ++ldap->msgid, BER_CLASS_APP,
	    LDAP_REQ_UNBIND_30);
	if (elm == NULL)
		goto fail;

	LDAP_DEBUG("aldap_unbind", root);

	error = ber_write_elements(&ldap->ber, root);
	ber_free_elements(root);
	root = NULL;
	if (error == -1)
		goto fail;

	return (ldap->msgid);
fail:
	if (root != NULL)
		ber_free_elements(root);

	ldap->err = ALDAP_ERR_OPERATION_FAILED;

	return (-1);
}

int
aldap_search(struct aldap *ldap, char *basedn, enum scope scope, char *filter,
    char **attrs, int typesonly, int sizelimit, int timelimit,
    struct aldap_page_control *page)
{
	struct ber_element *root = NULL, *ber, *c;
	int i, error;

	if ((root = ber_add_sequence(NULL)) == NULL)
		goto fail;

	ber = ber_printf_elements(root, "d{t", ++ldap->msgid, BER_CLASS_APP,
	    (unsigned long) LDAP_REQ_SEARCH);
	if (ber == NULL) {
		ldap->err = ALDAP_ERR_OPERATION_FAILED;
		goto fail;
	}

	c = ber;	
	ber = ber_printf_elements(ber, "sEEddb", basedn, (long long)scope,
	                         (long long)LDAP_DEREF_NEVER, sizelimit, 
				 timelimit, typesonly);
	if (ber == NULL) {
		ldap->err = ALDAP_ERR_OPERATION_FAILED;
		goto fail;
	}

	if ((ber = ldap_parse_search_filter(ber, filter)) == NULL) {
		ldap->err = ALDAP_ERR_PARSER_ERROR;
		goto fail;
	}

	if ((ber = ber_add_sequence(ber)) == NULL)
		goto fail;
	if (attrs != NULL)
		for (i = 0; attrs[i] != NULL; i++) {
			if ((ber = ber_add_string(ber, attrs[i])) == NULL)
				goto fail;
		}

	aldap_create_page_control(c, 100, page);

	LDAP_DEBUG("aldap_search", root);

	error = ber_write_elements(&ldap->ber, root);
	ber_free_elements(root);
	root = NULL;
	if (error == -1) {
		ldap->err = ALDAP_ERR_OPERATION_FAILED;
		goto fail;
	}

	return (ldap->msgid);

fail:
	if (root != NULL)
		ber_free_elements(root);

	return (-1);
}

int
aldap_create_page_control(struct ber_element *elm, int size,
    struct aldap_page_control *page)
{
	int len;
	struct ber c;
	struct ber_element *ber = NULL;

	c.br_wbuf = NULL;
	c.fd = -1;

	ber = ber_add_sequence(NULL);

	if (page == NULL) {
		if (ber_printf_elements(ber, "ds", 50, "") == NULL)
			goto fail;
	} else {
		if (ber_printf_elements(ber, "dx", 50, page->cookie,
			    page->cookie_len) == NULL)
			goto fail;
	}

	if ((len = ber_write_elements(&c, ber)) < 1)
		goto fail;
	if (ber_printf_elements(elm, "{t{sx", 2, 0, LDAP_PAGED_OID,
		                c.br_wbuf, (size_t)len) == NULL)
		goto fail;

	ber_free_elements(ber);
	ber_free(&c);
	return len;
fail:
	if (ber != NULL)
		ber_free_elements(ber);
	ber_free(&c);	

	return (-1);
}

struct aldap_message *
aldap_parse(struct aldap *ldap)
{
	int			 class;
	unsigned long		 type;
	long long		 msgid = 0;
	struct aldap_message	*m;
	struct ber_element	*a = NULL, *ep;

	if ((m = calloc(1, sizeof(struct aldap_message))) == NULL)
		return NULL;

	if ((m->msg = ber_read_elements(&ldap->ber, NULL)) == NULL)
		goto parsefail;

	LDAP_DEBUG("message", m->msg);

	if (ber_scanf_elements(m->msg, "{ite", &msgid, &class, &type, &a) != 0)
		goto parsefail;
	m->msgid = msgid;
	m->message_type = type;
	m->protocol_op = a;

	switch (m->message_type) {
	case LDAP_RES_BIND:
	case LDAP_RES_MODIFY:
	case LDAP_RES_ADD:
	case LDAP_RES_DELETE:
	case LDAP_RES_MODRDN:
	case LDAP_RES_COMPARE:
	case LDAP_RES_SEARCH_RESULT:
		if (ber_scanf_elements(m->protocol_op, "{EeSeSe",
		    &m->body.res.rescode, &m->dn, &m->body.res.diagmsg, &a) != 0)
			goto parsefail;
		if (m->body.res.rescode == LDAP_REFERRAL)
			if (ber_scanf_elements(a, "{e", &m->references) != 0)
				goto parsefail;
		if (m->msg->be_sub) {
			for (ep = m->msg->be_sub; ep != NULL; ep = ep->be_next) {
				ber_scanf_elements(ep, "t", &class, &type);
				if (class == 2 && type == 0)
					m->page = aldap_parse_page_control(ep->be_sub->be_sub,
					    ep->be_sub->be_sub->be_len);
			}
		} else
			m->page = NULL;
		break;
	case LDAP_RES_SEARCH_ENTRY:
		if (ber_scanf_elements(m->protocol_op, "{eS{e", &m->dn,
		    &m->body.search.attrs) != 0)
			goto parsefail;
		break;
	case LDAP_RES_SEARCH_REFERENCE:
		if (ber_scanf_elements(m->protocol_op, "{e", &m->references) != 0)
			goto parsefail;
		break;
	}

	return m;
parsefail:
	ldap->err = ALDAP_ERR_PARSER_ERROR;
	aldap_freemsg(m);
	return NULL;
}

struct aldap_page_control *
aldap_parse_page_control(struct ber_element *control, size_t len) 
{
	char *oid, *s;
	char *encoded;
	struct ber b;
	struct ber_element *elm;
	struct aldap_page_control *page;

	b.br_wbuf = NULL;
	b.fd = -1;
	ber_scanf_elements(control, "ss", &oid, &encoded);
	ber_set_readbuf(&b, encoded, control->be_next->be_len);
	elm = ber_read_elements(&b, NULL);

	if ((page = malloc(sizeof(struct aldap_page_control))) == NULL) {
		if (elm != NULL)
			ber_free_elements(elm);
		ber_free(&b);
		return NULL;
	}

	ber_scanf_elements(elm->be_sub, "is", &page->size, &s);
	page->cookie_len = elm->be_sub->be_next->be_len;

	if ((page->cookie = malloc(page->cookie_len)) == NULL) {
		if (elm != NULL)
			ber_free_elements(elm);
		ber_free(&b);
		free(page);
		return NULL;
	}
	memcpy(page->cookie, s, page->cookie_len);

	ber_free_elements(elm);
	ber_free(&b);
	return page;
}

void
aldap_freepage(struct aldap_page_control *page)
{
	free(page->cookie);
	free(page);
}

void
aldap_freemsg(struct aldap_message *msg)
{
	if (msg->msg)
		ber_free_elements(msg->msg);
	free(msg);
}

int
aldap_get_resultcode(struct aldap_message *msg)
{
	return msg->body.res.rescode;
}

char *
aldap_get_dn(struct aldap_message *msg)
{
	char *dn;

	if (msg->dn == NULL)
		return NULL;

	if (ber_get_string(msg->dn, &dn) == -1)
		return NULL;

	return utoa(dn);
}

char **
aldap_get_references(struct aldap_message *msg)
{
	if (msg->references == NULL)
		return NULL;
	return aldap_get_stringset(msg->references);
}

void
aldap_free_references(char **values)
{
	int i;

	if (values == NULL)
		return;

	for (i = 0; values[i] != NULL; i++)
		free(values[i]);

	free(values);
}

char *
aldap_get_diagmsg(struct aldap_message *msg)
{
	char *s;

	if (msg->body.res.diagmsg == NULL)
		return NULL;

	if (ber_get_string(msg->body.res.diagmsg, &s) == -1)
		return NULL;

	return utoa(s);
}

int
aldap_count_attrs(struct aldap_message *msg)
{
	int i;
	struct ber_element *a;

	if (msg->body.search.attrs == NULL)
		return (-1);

	for (i = 0, a = msg->body.search.attrs;
	    a != NULL && ber_get_eoc(a) != 0;
	    i++, a = a->be_next)
		;

	return i;
}

int
aldap_first_attr(struct aldap_message *msg, char **outkey, char ***outvalues)
{
	struct ber_element *b, *c;
	char *key;
	char **ret;

	if (msg->body.search.attrs == NULL)
		goto fail;

	if (ber_scanf_elements(msg->body.search.attrs, "{s(e)}e",
	    &key, &b, &c) != 0)
		goto fail;

	msg->body.search.iter = msg->body.search.attrs->be_next;

	if ((ret = aldap_get_stringset(b)) == NULL)
		goto fail;

	(*outvalues) = ret;
	(*outkey) = utoa(key);

	return (1);
fail:
	(*outkey) = NULL;
	(*outvalues) = NULL;
	return (-1);
}

int
aldap_next_attr(struct aldap_message *msg, char **outkey, char ***outvalues)
{
	struct ber_element *a, *b;
	char *key;
	char **ret;

	if (msg->body.search.iter == NULL)
		goto notfound;

	LDAP_DEBUG("attr", msg->body.search.iter);

	if (ber_get_eoc(msg->body.search.iter) == 0)
		goto notfound;

	if (ber_scanf_elements(msg->body.search.iter, "{s(e)}e", &key, &a, &b)
	    != 0)
		goto fail;

	msg->body.search.iter = msg->body.search.iter->be_next;

	if ((ret = aldap_get_stringset(a)) == NULL)
		goto fail;

	(*outvalues) = ret;
	(*outkey) = utoa(key);

	return (1);
fail:
notfound:
	(*outkey) = NULL;
	(*outvalues) = NULL;
	return (-1);
}

int
aldap_match_attr(struct aldap_message *msg, char *inkey, char ***outvalues)
{
	struct ber_element *a, *b;
	char *descr = NULL;
	char **ret;

	if (msg->body.search.attrs == NULL)
		goto fail;

	LDAP_DEBUG("attr", msg->body.search.attrs);

	for (a = msg->body.search.attrs;;) {
		if (a == NULL)
			goto notfound;
		if (ber_get_eoc(a) == 0)
			goto notfound;
		if (ber_scanf_elements(a, "{s(e", &descr, &b) != 0)
			goto fail;
		if (strcasecmp(descr, inkey) == 0)
			goto attrfound;
		a = a->be_next;
	}

attrfound:
	if ((ret = aldap_get_stringset(b)) == NULL)
		goto fail;

	(*outvalues) = ret;

	return (1);
fail:
notfound:
	(*outvalues) = NULL;
	return (-1);
}

int
aldap_free_attr(char **values)
{
	int i;

	if (values == NULL)
		return -1;

	for (i = 0; values[i] != NULL; i++)
		free(values[i]);

	free(values);

	return (1);
}

#if 0
void
aldap_free_url(struct aldap_url *lu)
{
	free(lu->buffer);
	free(lu->filter);
}

int
aldap_parse_url(char *url, struct aldap_url *lu)
{
	char		*p, *forward, *forward2;
	const char	*errstr = NULL;
	int		 i;

	if ((lu->buffer = p = strdup(url)) == NULL)
		return (-1);

	/* protocol */
	if (strncasecmp(LDAP_URL, p, strlen(LDAP_URL)) != 0)
		goto fail;
	lu->protocol = LDAP;
	p += strlen(LDAP_URL);

	/* host and optional port */
	if ((forward = strchr(p, '/')) != NULL)
		*forward = '\0';
	/* find the optional port */
	if ((forward2 = strchr(p, ':')) != NULL) {
		*forward2 = '\0';
		/* if a port is given */
		if (*(forward2+1) != '\0') {
#define PORT_MAX UINT16_MAX
			lu->port = strtonum(++forward2, 0, PORT_MAX, &errstr);
			if (errstr)
				goto fail;
		}
	}
	/* fail if no host is given */
	if (strlen(p) == 0)
		goto fail;
	lu->host = p;
	if (forward == NULL)
		goto done;
	/* p is assigned either a pointer to a character or to '\0' */
	p = ++forward;
	if (strlen(p) == 0)
		goto done;

	/* dn */
	if ((forward = strchr(p, '?')) != NULL)
		*forward = '\0';
	lu->dn = p;
	if (forward == NULL)
		goto done;
	/* p is assigned either a pointer to a character or to '\0' */
	p = ++forward;
	if (strlen(p) == 0)
		goto done;

	/* attributes */
	if ((forward = strchr(p, '?')) != NULL)
		*forward = '\0';
	for (i = 0; i < MAXATTR; i++) {
		if ((forward2 = strchr(p, ',')) == NULL) {
			if (strlen(p) == 0)
				break;
			lu->attributes[i] = p;
			break;
		}
		*forward2 = '\0';
		lu->attributes[i] = p;
		p = ++forward2;
	}
	if (forward == NULL)
		goto done;
	/* p is assigned either a pointer to a character or to '\0' */
	p = ++forward;
	if (strlen(p) == 0)
		goto done;

	/* scope */
	if ((forward = strchr(p, '?')) != NULL)
		*forward = '\0';
	if (strcmp(p, "base") == 0)
		lu->scope = LDAP_SCOPE_BASE;
	else if (strcmp(p, "one") == 0)
		lu->scope = LDAP_SCOPE_ONELEVEL;
	else if (strcmp(p, "sub") == 0)
		lu->scope = LDAP_SCOPE_SUBTREE;
	else
		goto fail;
	if (forward == NULL)
		goto done;
	p = ++forward;
	if (strlen(p) == 0)
		goto done;

	/* filter */
	if (p)
		lu->filter = p;
done:
	free(url);
	return (1);
fail:
	free(lu->buffer);
	lu->buffer = NULL;
	return (-1);
}

int
aldap_search_url(struct aldap *ldap, char *url, int typesonly, int sizelimit,
    int timelimit)
{
	struct aldap_url *lu;

	if ((lu = calloc(1, sizeof(*lu))) == NULL)
		return (-1);

	if (aldap_parse_url(url, lu))
		goto fail;

	if (aldap_search(ldap, lu->dn, lu->scope, lu->filter, lu->attributes,
	    typesonly, sizelimit, timelimit) == -1)
		goto fail;

	aldap_free_url(lu);
	return (ldap->msgid);
fail:
	aldap_free_url(lu);
	return (-1);
}
#endif /* 0 */

/*
 * internal functions
 */

char **
aldap_get_stringset(struct ber_element *elm)
{
	struct ber_element *a;
	int i;
	char **ret;
	char *s;

	if (elm->be_type != BER_TYPE_OCTETSTRING)
		return NULL;

	for (a = elm, i = 1; i > 0 && a != NULL && a->be_type ==
	    BER_TYPE_OCTETSTRING; a = a->be_next, i++)
		;
	if (i == 1)
		return NULL;

	if ((ret = calloc(i + 1, sizeof(char *))) == NULL)
		return NULL;

	for (a = elm, i = 0; a != NULL && a->be_type == BER_TYPE_OCTETSTRING;
	    a = a->be_next) {

		ber_get_string(a, &s);
		ret[i] = utoa(s);
		if (ret[i] != NULL)
			i++;
		
	}
	if (i == 0) {
		free(ret);
		return NULL;
	}
	ret[i] = NULL;

	return ret;
}

/*
 * Base case for ldap_do_parse_search_filter
 *
 * returns:
 *	struct ber_element *, ber_element tree
 *	NULL, parse failed
 */
static struct ber_element *
ldap_parse_search_filter(struct ber_element *ber, char *filter)
{
	struct ber_element *elm;
	char *cp;

	cp = filter;

	if (cp == NULL || *cp == '\0') {
		errno = EINVAL;
		return (NULL);
	}

	if ((elm = ldap_do_parse_search_filter(ber, &cp)) == NULL)
		return (NULL);

	if (*cp != '\0') {
		ber_free_elements(elm);
		ber_link_elements(ber, NULL);
		errno = EINVAL;
		return (NULL);
	}

	return (elm);
}

/*
 * Translate RFC4515 search filter string into ber_element tree
 *
 * returns:
 *	struct ber_element *, ber_element tree
 *	NULL, parse failed
 *
 * notes:
 *	when cp is passed to a recursive invocation, it is updated
 *	    to point one character beyond the filter that was passed
 *	    i.e., cp jumps to "(filter)" upon return
 *	                               ^
 *	goto's used to discriminate error-handling based on error type
 *	doesn't handle extended filters (yet)
 *
 */
static struct ber_element *
ldap_do_parse_search_filter(struct ber_element *prev, char **cpp)
{
	struct ber_element *elm, *root = NULL;
	char *attr_desc, *attr_val, *parsed_val, *cp;
	size_t len;
	unsigned long type;

	root = NULL;

	/* cpp should pass in pointer to opening parenthesis of "(filter)" */
	cp = *cpp;
	if (*cp != '(')
		goto syntaxfail;

	switch (*++cp) {
	case '&':		/* AND */
	case '|':		/* OR */
		if (*cp == '&')
			type = LDAP_FILT_AND;
		else
			type = LDAP_FILT_OR;

		if ((elm = ber_add_set(prev)) == NULL)
			goto callfail;
		root = elm;
		ber_set_header(elm, BER_CLASS_CONTEXT, type);

		if (*++cp != '(')		/* opening `(` of filter */
			goto syntaxfail;

		while (*cp == '(') {
			if ((elm =
			    ldap_do_parse_search_filter(elm, &cp)) == NULL)
				goto bad;
		}

		if (*cp != ')')			/* trailing `)` of filter */
			goto syntaxfail;
		break;

	case '!':		/* NOT */
		if ((root = ber_add_sequence(prev)) == NULL)
			goto callfail;
		ber_set_header(root, BER_CLASS_CONTEXT, LDAP_FILT_NOT);

		cp++;				/* now points to sub-filter */
		if ((elm = ldap_do_parse_search_filter(root, &cp)) == NULL)
			goto bad;

		if (*cp != ')')			/* trailing `)` of filter */
			goto syntaxfail;
		break;

	default:	/* SIMPLE || PRESENCE */
		attr_desc = cp;

		len = strcspn(cp, "()<>~=");
		cp += len;
		switch (*cp) {
		case '~':
			type = LDAP_FILT_APPR;
			cp++;
			break;
		case '<':
			type = LDAP_FILT_LE;
			cp++;
			break;
		case '>':
			type = LDAP_FILT_GE;
			cp++;
			break;
		case '=':
			type = LDAP_FILT_EQ;	/* assume EQ until disproven */
			break;
		case '(':
		case ')':
		default:
			goto syntaxfail;
		}
		attr_val = ++cp;

		/* presence filter */
		if (strncmp(attr_val, "*)", 2) == 0) {
			cp++;			/* point to trailing `)` */
			if ((root =
			    ber_add_nstring(prev, attr_desc, len)) == NULL)
				goto bad;

			ber_set_header(root, BER_CLASS_CONTEXT, LDAP_FILT_PRES);
			break;
		}

		if ((root = ber_add_sequence(prev)) == NULL)
			goto callfail;
		ber_set_header(root, BER_CLASS_CONTEXT, type);

		if ((elm = ber_add_nstring(root, attr_desc, len)) == NULL)
			goto callfail;

		len = strcspn(attr_val, "*)");
		if (len == 0 && *cp != '*')
			goto syntaxfail;
		cp += len;
		if (*cp == '\0')
			goto syntaxfail;

		if (*cp == '*') {	/* substring filter */
			int initial;

			cp = attr_val;

			ber_set_header(root, BER_CLASS_CONTEXT, LDAP_FILT_SUBS);

			if ((elm = ber_add_sequence(elm)) == NULL)
				goto callfail;

			for (initial = 1;; cp++, initial = 0) {
				attr_val = cp;

				len = strcspn(attr_val, "*)");
				if (len == 0) {
					if (*cp == ')')
						break;
					else
						continue;
				}
				cp += len;
				if (*cp == '\0')
					goto syntaxfail;

				if (initial)
					type = LDAP_FILT_SUBS_INIT;
				else if (*cp == ')')
					type = LDAP_FILT_SUBS_FIN;
				else
					type = LDAP_FILT_SUBS_ANY;

				if ((parsed_val = parseval(attr_val, len)) ==
				    NULL)
					goto callfail;
				elm = ber_add_nstring(elm, parsed_val,
				    strlen(parsed_val));
				free(parsed_val);
				if (elm == NULL)
					goto callfail;
				ber_set_header(elm, BER_CLASS_CONTEXT, type);
				if (type == LDAP_FILT_SUBS_FIN)
					break;
			}
			break;
		}

		if ((parsed_val = parseval(attr_val, len)) == NULL)
			goto callfail;
		elm = ber_add_nstring(elm, parsed_val, strlen(parsed_val));
		free(parsed_val);
		if (elm == NULL)
			goto callfail;
		break;
	}

	cp++;		/* now points one char beyond the trailing `)` */

	*cpp = cp;
	return (root);

syntaxfail:		/* XXX -- error reporting */
callfail:
bad:
	if (root != NULL)
		ber_free_elements(root);
	ber_link_elements(prev, NULL);
	return (NULL);
}

#ifdef DEBUG
/*
 * Display a list of ber elements.
 *
 */
void
ldap_debug_elements(struct ber_element *root)
{
	static int	 indent = 0;
	long long	 v;
	int		 d;
	char		*buf;
	size_t		 len;
	u_int		 i;
	int		 constructed;
	struct ber_oid	 o;

	/* calculate lengths */
	ber_calc_len(root);

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
		case BER_TYPE_BOOLEAN:
			fprintf(stderr, "boolean");
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
		case LDAP_REQ_BIND:
			fprintf(stderr, "bind");
			break;
		case LDAP_RES_BIND:
			fprintf(stderr, "bind");
			break;
		case LDAP_REQ_UNBIND_30:
			break;
		case LDAP_REQ_SEARCH:
			fprintf(stderr, "search");
			break;
		case LDAP_RES_SEARCH_ENTRY:
			fprintf(stderr, "search_entry");
			break;
		case LDAP_RES_SEARCH_RESULT:
			fprintf(stderr, "search_result");
			break;
		case LDAP_REQ_MODIFY:
			fprintf(stderr, "modify");
			break;
		case LDAP_RES_MODIFY:
			fprintf(stderr, "modify");
			break;
		case LDAP_REQ_ADD:
			fprintf(stderr, "add");
			break;
		case LDAP_RES_ADD:
			fprintf(stderr, "add");
			break;
		case LDAP_REQ_DELETE_30:
			fprintf(stderr, "delete");
			break;
		case LDAP_RES_DELETE:
			fprintf(stderr, "delete");
			break;
		case LDAP_REQ_MODRDN:
			fprintf(stderr, "modrdn");
			break;
		case LDAP_RES_MODRDN:
			fprintf(stderr, "modrdn");
			break;
		case LDAP_REQ_COMPARE:
			fprintf(stderr, "compare");
			break;
		case LDAP_RES_COMPARE:
			fprintf(stderr, "compare");
			break;
		case LDAP_REQ_ABANDON_30:
			fprintf(stderr, "abandon");
			break;
		}
		break;
	case BER_CLASS_PRIVATE:
		fprintf(stderr, "class: private(%u) type: ", root->be_class);
		fprintf(stderr, "encoding (%lu) type: ", root->be_encoding);
		break;
	case BER_CLASS_CONTEXT:
		/* XXX: this is not correct */
		fprintf(stderr, "class: context(%u) type: ", root->be_class);
		switch(root->be_type) {
		case LDAP_AUTH_SIMPLE:
			fprintf(stderr, "auth simple");
			break;
		}
		break;
	default:
		fprintf(stderr, "class: <INVALID>(%u) type: ", root->be_class);
		break;
	}
	fprintf(stderr, "(%lu) encoding %lu ",
	    root->be_type, root->be_encoding);

	if (constructed)
		root->be_encoding = constructed;

	switch (root->be_encoding) {
	case BER_TYPE_BOOLEAN:
		if (ber_get_boolean(root, &d) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "%s(%d)\n", d ? "true" : "false", d);
		break;
	case BER_TYPE_INTEGER:
		if (ber_get_integer(root, &v) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "value %lld\n", v);
		break;
	case BER_TYPE_ENUMERATED:
		if (ber_get_enumerated(root, &v) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "value %lld\n", v);
		break;
	case BER_TYPE_BITSTRING:
		if (ber_get_bitstring(root, (void *)&buf, &len) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "hexdump ");
		for (i = 0; i < len; i++)
			fprintf(stderr, "%02x", buf[i]);
		fprintf(stderr, "\n");
		break;
	case BER_TYPE_OBJECT:
		if (ber_get_oid(root, &o) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "\n");
		break;
	case BER_TYPE_OCTETSTRING:
		if (ber_get_nstring(root, (void *)&buf, &len) == -1) {
			fprintf(stderr, "<INVALID>\n");
			break;
		}
		fprintf(stderr, "string \"%.*s\"\n",  len, buf);
		break;
	case BER_TYPE_NULL:	/* no payload */
	case BER_TYPE_EOC:
	case BER_TYPE_SEQUENCE:
	case BER_TYPE_SET:
	default:
		fprintf(stderr, "\n");
		break;
	}

	if (constructed && root->be_sub) {
		indent += 2;
		ldap_debug_elements(root->be_sub);
		indent -= 2;
	}
	if (root->be_next)
		ldap_debug_elements(root->be_next);
}
#endif

/*
 * Strip UTF-8 down to ASCII without validation.
 * notes:
 *	non-ASCII characters are displayed as '?'
 *	the argument u should be a NULL terminated sequence of UTF-8 bytes.
 */
char *
utoa(char *u)
{
	int	 len, i, j;
	char	*str;

	/* calculate the length to allocate */
	for (len = 0, i = 0; u[i] != '\0'; i++)
		if (!isu8cont(u[i]))
			len++;

	if ((str = calloc(len + 1, sizeof(char))) == NULL)
		return NULL;

	/* copy the ASCII characters to the newly allocated string */
	for (i = 0, j = 0; u[i] != '\0'; i++)
		if (!isu8cont(u[i]))
			str[j++] = isascii((unsigned char)u[i]) ? u[i] : '?';

	return str;
}

static int
isu8cont(unsigned char c)
{
	return (c & (0x80 | 0x40)) == 0x80;
}

/*
 * Parse a LDAP value
 * notes:
 *	the argument p should be a NUL-terminated sequence of ASCII bytes.
 */
char *
parseval(char *p, size_t len)
{
	char	 hex[3];
	char	*buffer;
	size_t	 i, j;

	if ((buffer = calloc(1, len + 1)) == NULL)
		return NULL;

	for (i = j = 0; j < len; i++) {
		if (p[j] == '\\') {
			strlcpy(hex, p + j + 1, sizeof(hex));
			buffer[i] = (char)strtoumax(hex, NULL, 16);
			j += 3;
		} else {
			buffer[i] = p[j];
			j++;
		}
	}

	return buffer;
}

int
aldap_get_errno(struct aldap *a, const char **estr)
{
	switch (a->err) {
	case ALDAP_ERR_SUCCESS:
		*estr = "success";
		break;
	case ALDAP_ERR_PARSER_ERROR:
		*estr = "parser failed";
		break;
	case ALDAP_ERR_INVALID_FILTER:
		*estr = "invalid filter";
		break;
	case ALDAP_ERR_OPERATION_FAILED:
		*estr = "operation failed";
		break;
	default:
		*estr = "unknown";
		break;
	}
	return (a->err);
}

