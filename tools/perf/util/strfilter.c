// SPDX-License-Identifier: GPL-2.0
#include "util.h"
#include "string2.h"
#include "strfilter.h"

#include <errno.h>
#include <linux/ctype.h>

/* Operators */
static const char *OP_and	= "&";	/* Logical AND */
static const char *OP_or	= "|";	/* Logical OR */
static const char *OP_not	= "!";	/* Logical NOT */

#define is_operator(c)	((c) == '|' || (c) == '&' || (c) == '!')
#define is_separator(c)	(is_operator(c) || (c) == '(' || (c) == ')')

static void strfilter_node__delete(struct strfilter_node *node)
{
	if (node) {
		if (node->p && !is_operator(*node->p))
			zfree((char **)&node->p);
		strfilter_node__delete(node->l);
		strfilter_node__delete(node->r);
		free(node);
	}
}

void strfilter__delete(struct strfilter *filter)
{
	if (filter) {
		strfilter_node__delete(filter->root);
		free(filter);
	}
}

static const char *get_token(const char *s, const char **e)
{
	const char *p;

	while (isspace(*s))	/* Skip spaces */
		s++;

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

static struct strfilter_node *strfilter_node__alloc(const char *op,
						    struct strfilter_node *l,
						    struct strfilter_node *r)
{
	struct strfilter_node *node = zalloc(sizeof(*node));

	if (node) {
		node->p = op;
		node->l = l;
		node->r = r;
	}

	return node;
}

static struct strfilter_node *strfilter_node__new(const char *s,
						  const char **ep)
{
	struct strfilter_node root, *cur, *last_op;
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
			cur = strfilter_node__alloc(OP_and, last_op->r, NULL);
			if (!cur)
				goto nomem;
			last_op->r = cur;
			last_op = cur;
			break;
		case '|':	/* Exchg the root with OR */
			if (!cur->r || !root.r)
				goto error;
			cur = strfilter_node__alloc(OP_or, root.r, NULL);
			if (!cur)
				goto nomem;
			root.r = cur;
			last_op = cur;
			break;
		case '!':	/* Add NOT as a leaf node */
			if (cur->r)
				goto error;
			cur->r = strfilter_node__alloc(OP_not, NULL, NULL);
			if (!cur->r)
				goto nomem;
			cur = cur->r;
			break;
		case '(':	/* Recursively parses inside the parenthesis */
			if (cur->r)
				goto error;
			cur->r = strfilter_node__new(s + 1, &s);
			if (!s)
				goto nomem;
			if (!cur->r || *s != ')')
				goto error;
			e = s + 1;
			break;
		default:
			if (cur->r)
				goto error;
			cur->r = strfilter_node__alloc(NULL, NULL, NULL);
			if (!cur->r)
				goto nomem;
			cur->r->p = strndup(s, e - s);
			if (!cur->r->p)
				goto nomem;
		}
		s = get_token(e, &e);
	}
	if (!cur->r)
		goto error;
	*ep = s;
	return root.r;
nomem:
	s = NULL;
error:
	*ep = s;
	strfilter_node__delete(root.r);
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
		filter->root = strfilter_node__new(rules, &ep);

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
	struct strfilter_node *right, *root;
	const char *ep = NULL;

	if (!filter || !rules)
		return -EINVAL;

	right = strfilter_node__new(rules, &ep);
	if (!right || *ep != '\0') {
		if (err)
			*err = ep;
		goto error;
	}
	root = strfilter_node__alloc(_or ? OP_or : OP_and, filter->root, right);
	if (!root) {
		ep = NULL;
		goto error;
	}

	filter->root = root;
	return 0;

error:
	strfilter_node__delete(right);
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

static bool strfilter_node__compare(struct strfilter_node *node,
				    const char *str)
{
	if (!node || !node->p)
		return false;

	switch (*node->p) {
	case '|':	/* OR */
		return strfilter_node__compare(node->l, str) ||
			strfilter_node__compare(node->r, str);
	case '&':	/* AND */
		return strfilter_node__compare(node->l, str) &&
			strfilter_node__compare(node->r, str);
	case '!':	/* NOT */
		return !strfilter_node__compare(node->r, str);
	default:
		return strglobmatch(str, node->p);
	}
}

/* Return true if STR matches the filter rules */
bool strfilter__compare(struct strfilter *filter, const char *str)
{
	if (!filter)
		return false;
	return strfilter_node__compare(filter->root, str);
}

static int strfilter_node__sprint(struct strfilter_node *node, char *buf);

/* sprint node in parenthesis if needed */
static int strfilter_node__sprint_pt(struct strfilter_node *node, char *buf)
{
	int len;
	int pt = node->r ? 2 : 0;	/* don't need to check node->l */

	if (buf && pt)
		*buf++ = '(';
	len = strfilter_node__sprint(node, buf);
	if (len < 0)
		return len;
	if (buf && pt)
		*(buf + len) = ')';
	return len + pt;
}

static int strfilter_node__sprint(struct strfilter_node *node, char *buf)
{
	int len = 0, rlen;

	if (!node || !node->p)
		return -EINVAL;

	switch (*node->p) {
	case '|':
	case '&':
		len = strfilter_node__sprint_pt(node->l, buf);
		if (len < 0)
			return len;
		__fallthrough;
	case '!':
		if (buf) {
			*(buf + len++) = *node->p;
			buf += len;
		} else
			len++;
		rlen = strfilter_node__sprint_pt(node->r, buf);
		if (rlen < 0)
			return rlen;
		len += rlen;
		break;
	default:
		len = strlen(node->p);
		if (buf)
			strcpy(buf, node->p);
	}

	return len;
}

char *strfilter__string(struct strfilter *filter)
{
	int len;
	char *ret = NULL;

	len = strfilter_node__sprint(filter->root, NULL);
	if (len < 0)
		return NULL;

	ret = malloc(len + 1);
	if (ret)
		strfilter_node__sprint(filter->root, ret);

	return ret;
}
