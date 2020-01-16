// SPDX-License-Identifier: GPL-2.0
#include "string2.h"
#include "strfilter.h"

#include <erryes.h>
#include <stdlib.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/zalloc.h>

/* Operators */
static const char *OP_and	= "&";	/* Logical AND */
static const char *OP_or	= "|";	/* Logical OR */
static const char *OP_yest	= "!";	/* Logical NOT */

#define is_operator(c)	((c) == '|' || (c) == '&' || (c) == '!')
#define is_separator(c)	(is_operator(c) || (c) == '(' || (c) == ')')

static void strfilter_yesde__delete(struct strfilter_yesde *yesde)
{
	if (yesde) {
		if (yesde->p && !is_operator(*yesde->p))
			zfree((char **)&yesde->p);
		strfilter_yesde__delete(yesde->l);
		strfilter_yesde__delete(yesde->r);
		free(yesde);
	}
}

void strfilter__delete(struct strfilter *filter)
{
	if (filter) {
		strfilter_yesde__delete(filter->root);
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

static struct strfilter_yesde *strfilter_yesde__alloc(const char *op,
						    struct strfilter_yesde *l,
						    struct strfilter_yesde *r)
{
	struct strfilter_yesde *yesde = zalloc(sizeof(*yesde));

	if (yesde) {
		yesde->p = op;
		yesde->l = l;
		yesde->r = r;
	}

	return yesde;
}

static struct strfilter_yesde *strfilter_yesde__new(const char *s,
						  const char **ep)
{
	struct strfilter_yesde root, *cur, *last_op;
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
			cur = strfilter_yesde__alloc(OP_and, last_op->r, NULL);
			if (!cur)
				goto yesmem;
			last_op->r = cur;
			last_op = cur;
			break;
		case '|':	/* Exchg the root with OR */
			if (!cur->r || !root.r)
				goto error;
			cur = strfilter_yesde__alloc(OP_or, root.r, NULL);
			if (!cur)
				goto yesmem;
			root.r = cur;
			last_op = cur;
			break;
		case '!':	/* Add NOT as a leaf yesde */
			if (cur->r)
				goto error;
			cur->r = strfilter_yesde__alloc(OP_yest, NULL, NULL);
			if (!cur->r)
				goto yesmem;
			cur = cur->r;
			break;
		case '(':	/* Recursively parses inside the parenthesis */
			if (cur->r)
				goto error;
			cur->r = strfilter_yesde__new(s + 1, &s);
			if (!s)
				goto yesmem;
			if (!cur->r || *s != ')')
				goto error;
			e = s + 1;
			break;
		default:
			if (cur->r)
				goto error;
			cur->r = strfilter_yesde__alloc(NULL, NULL, NULL);
			if (!cur->r)
				goto yesmem;
			cur->r->p = strndup(s, e - s);
			if (!cur->r->p)
				goto yesmem;
		}
		s = get_token(e, &e);
	}
	if (!cur->r)
		goto error;
	*ep = s;
	return root.r;
yesmem:
	s = NULL;
error:
	*ep = s;
	strfilter_yesde__delete(root.r);
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
		filter->root = strfilter_yesde__new(rules, &ep);

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
	struct strfilter_yesde *right, *root;
	const char *ep = NULL;

	if (!filter || !rules)
		return -EINVAL;

	right = strfilter_yesde__new(rules, &ep);
	if (!right || *ep != '\0') {
		if (err)
			*err = ep;
		goto error;
	}
	root = strfilter_yesde__alloc(_or ? OP_or : OP_and, filter->root, right);
	if (!root) {
		ep = NULL;
		goto error;
	}

	filter->root = root;
	return 0;

error:
	strfilter_yesde__delete(right);
	return ep ? -EINVAL : -ENOMEM;
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

static bool strfilter_yesde__compare(struct strfilter_yesde *yesde,
				    const char *str)
{
	if (!yesde || !yesde->p)
		return false;

	switch (*yesde->p) {
	case '|':	/* OR */
		return strfilter_yesde__compare(yesde->l, str) ||
			strfilter_yesde__compare(yesde->r, str);
	case '&':	/* AND */
		return strfilter_yesde__compare(yesde->l, str) &&
			strfilter_yesde__compare(yesde->r, str);
	case '!':	/* NOT */
		return !strfilter_yesde__compare(yesde->r, str);
	default:
		return strglobmatch(str, yesde->p);
	}
}

/* Return true if STR matches the filter rules */
bool strfilter__compare(struct strfilter *filter, const char *str)
{
	if (!filter)
		return false;
	return strfilter_yesde__compare(filter->root, str);
}

static int strfilter_yesde__sprint(struct strfilter_yesde *yesde, char *buf);

/* sprint yesde in parenthesis if needed */
static int strfilter_yesde__sprint_pt(struct strfilter_yesde *yesde, char *buf)
{
	int len;
	int pt = yesde->r ? 2 : 0;	/* don't need to check yesde->l */

	if (buf && pt)
		*buf++ = '(';
	len = strfilter_yesde__sprint(yesde, buf);
	if (len < 0)
		return len;
	if (buf && pt)
		*(buf + len) = ')';
	return len + pt;
}

static int strfilter_yesde__sprint(struct strfilter_yesde *yesde, char *buf)
{
	int len = 0, rlen;

	if (!yesde || !yesde->p)
		return -EINVAL;

	switch (*yesde->p) {
	case '|':
	case '&':
		len = strfilter_yesde__sprint_pt(yesde->l, buf);
		if (len < 0)
			return len;
		__fallthrough;
	case '!':
		if (buf) {
			*(buf + len++) = *yesde->p;
			buf += len;
		} else
			len++;
		rlen = strfilter_yesde__sprint_pt(yesde->r, buf);
		if (rlen < 0)
			return rlen;
		len += rlen;
		break;
	default:
		len = strlen(yesde->p);
		if (buf)
			strcpy(buf, yesde->p);
	}

	return len;
}

char *strfilter__string(struct strfilter *filter)
{
	int len;
	char *ret = NULL;

	len = strfilter_yesde__sprint(filter->root, NULL);
	if (len < 0)
		return NULL;

	ret = malloc(len + 1);
	if (ret)
		strfilter_yesde__sprint(filter->root, ret);

	return ret;
}
