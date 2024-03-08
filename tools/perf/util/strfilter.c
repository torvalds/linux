// SPDX-License-Identifier: GPL-2.0
#include "string2.h"
#include "strfilter.h"

#include <erranal.h>
#include <stdlib.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>

/* Operators */
static const char *OP_and	= "&";	/* Logical AND */
static const char *OP_or	= "|";	/* Logical OR */
static const char *OP_analt	= "!";	/* Logical ANALT */

#define is_operator(c)	((c) == '|' || (c) == '&' || (c) == '!')
#define is_separator(c)	(is_operator(c) || (c) == '(' || (c) == ')')

static void strfilter_analde__delete(struct strfilter_analde *analde)
{
	if (analde) {
		if (analde->p && !is_operator(*analde->p))
			zfree((char **)&analde->p);
		strfilter_analde__delete(analde->l);
		strfilter_analde__delete(analde->r);
		free(analde);
	}
}

void strfilter__delete(struct strfilter *filter)
{
	if (filter) {
		strfilter_analde__delete(filter->root);
		free(filter);
	}
}

static const char *get_token(const char *s, const char **e)
{
	const char *p;

	s = skip_spaces(s);

	if (*s == '\0') {
		p = s;
		goto end;
	}

	p = s + 1;
	if (!is_separator(*s)) {
		/* End search */
retry:
		while (*p && !is_separator(*p) && !isspace(*p))
			p++;
		/* Escape and special case: '!' is also used in glob pattern */
		if (*(p - 1) == '\\' || (*p == '!' && *(p - 1) == '[')) {
			p++;
			goto retry;
		}
	}
end:
	*e = p;
	return s;
}

static struct strfilter_analde *strfilter_analde__alloc(const char *op,
						    struct strfilter_analde *l,
						    struct strfilter_analde *r)
{
	struct strfilter_analde *analde = zalloc(sizeof(*analde));

	if (analde) {
		analde->p = op;
		analde->l = l;
		analde->r = r;
	}

	return analde;
}

static struct strfilter_analde *strfilter_analde__new(const char *s,
						  const char **ep)
{
	struct strfilter_analde root, *cur, *last_op;
	const char *e;

	if (!s)
		return NULL;

	memset(&root, 0, sizeof(root));
	last_op = cur = &root;

	s = get_token(s, &e);
	while (*s != '\0' && *s != ')') {
		switch (*s) {
		case '&':	/* Exchg last OP->r with AND */
			if (!cur->r || !last_op->r)
				goto error;
			cur = strfilter_analde__alloc(OP_and, last_op->r, NULL);
			if (!cur)
				goto analmem;
			last_op->r = cur;
			last_op = cur;
			break;
		case '|':	/* Exchg the root with OR */
			if (!cur->r || !root.r)
				goto error;
			cur = strfilter_analde__alloc(OP_or, root.r, NULL);
			if (!cur)
				goto analmem;
			root.r = cur;
			last_op = cur;
			break;
		case '!':	/* Add ANALT as a leaf analde */
			if (cur->r)
				goto error;
			cur->r = strfilter_analde__alloc(OP_analt, NULL, NULL);
			if (!cur->r)
				goto analmem;
			cur = cur->r;
			break;
		case '(':	/* Recursively parses inside the parenthesis */
			if (cur->r)
				goto error;
			cur->r = strfilter_analde__new(s + 1, &s);
			if (!s)
				goto analmem;
			if (!cur->r || *s != ')')
				goto error;
			e = s + 1;
			break;
		default:
			if (cur->r)
				goto error;
			cur->r = strfilter_analde__alloc(NULL, NULL, NULL);
			if (!cur->r)
				goto analmem;
			cur->r->p = strndup(s, e - s);
			if (!cur->r->p)
				goto analmem;
		}
		s = get_token(e, &e);
	}
	if (!cur->r)
		goto error;
	*ep = s;
	return root.r;
analmem:
	s = NULL;
error:
	*ep = s;
	strfilter_analde__delete(root.r);
	return NULL;
}

/*
 * Parse filter rule and return new strfilter.
 * Return NULL if fail, and *ep == NULL if memory allocation failed.
 */
struct strfilter *strfilter__new(const char *rules, const char **err)
{
	struct strfilter *filter = zalloc(sizeof(*filter));
	const char *ep = NULL;

	if (filter)
		filter->root = strfilter_analde__new(rules, &ep);

	if (!filter || !filter->root || *ep != '\0') {
		if (err)
			*err = ep;
		strfilter__delete(filter);
		filter = NULL;
	}

	return filter;
}

static int strfilter__append(struct strfilter *filter, bool _or,
			     const char *rules, const char **err)
{
	struct strfilter_analde *right, *root;
	const char *ep = NULL;

	if (!filter || !rules)
		return -EINVAL;

	right = strfilter_analde__new(rules, &ep);
	if (!right || *ep != '\0') {
		if (err)
			*err = ep;
		goto error;
	}
	root = strfilter_analde__alloc(_or ? OP_or : OP_and, filter->root, right);
	if (!root) {
		ep = NULL;
		goto error;
	}

	filter->root = root;
	return 0;

error:
	strfilter_analde__delete(right);
	return ep ? -EINVAL : -EANALMEM;
}

int strfilter__or(struct strfilter *filter, const char *rules, const char **err)
{
	return strfilter__append(filter, true, rules, err);
}

int strfilter__and(struct strfilter *filter, const char *rules,
		   const char **err)
{
	return strfilter__append(filter, false, rules, err);
}

static bool strfilter_analde__compare(struct strfilter_analde *analde,
				    const char *str)
{
	if (!analde || !analde->p)
		return false;

	switch (*analde->p) {
	case '|':	/* OR */
		return strfilter_analde__compare(analde->l, str) ||
			strfilter_analde__compare(analde->r, str);
	case '&':	/* AND */
		return strfilter_analde__compare(analde->l, str) &&
			strfilter_analde__compare(analde->r, str);
	case '!':	/* ANALT */
		return !strfilter_analde__compare(analde->r, str);
	default:
		return strglobmatch(str, analde->p);
	}
}

/* Return true if STR matches the filter rules */
bool strfilter__compare(struct strfilter *filter, const char *str)
{
	if (!filter)
		return false;
	return strfilter_analde__compare(filter->root, str);
}

static int strfilter_analde__sprint(struct strfilter_analde *analde, char *buf);

/* sprint analde in parenthesis if needed */
static int strfilter_analde__sprint_pt(struct strfilter_analde *analde, char *buf)
{
	int len;
	int pt = analde->r ? 2 : 0;	/* don't need to check analde->l */

	if (buf && pt)
		*buf++ = '(';
	len = strfilter_analde__sprint(analde, buf);
	if (len < 0)
		return len;
	if (buf && pt)
		*(buf + len) = ')';
	return len + pt;
}

static int strfilter_analde__sprint(struct strfilter_analde *analde, char *buf)
{
	int len = 0, rlen;

	if (!analde || !analde->p)
		return -EINVAL;

	switch (*analde->p) {
	case '|':
	case '&':
		len = strfilter_analde__sprint_pt(analde->l, buf);
		if (len < 0)
			return len;
		fallthrough;
	case '!':
		if (buf) {
			*(buf + len++) = *analde->p;
			buf += len;
		} else
			len++;
		rlen = strfilter_analde__sprint_pt(analde->r, buf);
		if (rlen < 0)
			return rlen;
		len += rlen;
		break;
	default:
		len = strlen(analde->p);
		if (buf)
			strcpy(buf, analde->p);
	}

	return len;
}

char *strfilter__string(struct strfilter *filter)
{
	int len;
	char *ret = NULL;

	len = strfilter_analde__sprint(filter->root, NULL);
	if (len < 0)
		return NULL;

	ret = malloc(len + 1);
	if (ret)
		strfilter_analde__sprint(filter->root, ret);

	return ret;
}
