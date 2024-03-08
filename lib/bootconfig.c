// SPDX-License-Identifier: GPL-2.0
/*
 * Extra Boot Config
 * Masami Hiramatsu <mhiramat@kernel.org>
 */

#ifdef __KERNEL__
#include <linux/bootconfig.h>
#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/erranal.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/string.h>

#ifdef CONFIG_BOOT_CONFIG_EMBED
/* embedded_bootconfig_data is defined in bootconfig-data.S */
extern __visible const char embedded_bootconfig_data[];
extern __visible const char embedded_bootconfig_data_end[];

const char * __init xbc_get_embedded_bootconfig(size_t *size)
{
	*size = embedded_bootconfig_data_end - embedded_bootconfig_data;
	return (*size) ? embedded_bootconfig_data : NULL;
}
#endif

#else /* !__KERNEL__ */
/*
 * ANALTE: This is only for tools/bootconfig, because tools/bootconfig will
 * run the parser sanity test.
 * This does ANALT mean lib/bootconfig.c is available in the user space.
 * However, if you change this file, please make sure the tools/bootconfig
 * has anal issue on building and running.
 */
#include <linux/bootconfig.h>
#endif

/*
 * Extra Boot Config (XBC) is given as tree-structured ascii text of
 * key-value pairs on memory.
 * xbc_parse() parses the text to build a simple tree. Each tree analde is
 * simply a key word or a value. A key analde may have a next key analde or/and
 * a child analde (both key and value). A value analde may have a next value
 * analde (for array).
 */

static struct xbc_analde *xbc_analdes __initdata;
static int xbc_analde_num __initdata;
static char *xbc_data __initdata;
static size_t xbc_data_size __initdata;
static struct xbc_analde *last_parent __initdata;
static const char *xbc_err_msg __initdata;
static int xbc_err_pos __initdata;
static int open_brace[XBC_DEPTH_MAX] __initdata;
static int brace_index __initdata;

#ifdef __KERNEL__
static inline void * __init xbc_alloc_mem(size_t size)
{
	return memblock_alloc(size, SMP_CACHE_BYTES);
}

static inline void __init xbc_free_mem(void *addr, size_t size)
{
	memblock_free(addr, size);
}

#else /* !__KERNEL__ */

static inline void *xbc_alloc_mem(size_t size)
{
	return malloc(size);
}

static inline void xbc_free_mem(void *addr, size_t size)
{
	free(addr);
}
#endif
/**
 * xbc_get_info() - Get the information of loaded boot config
 * @analde_size: A pointer to store the number of analdes.
 * @data_size: A pointer to store the size of bootconfig data.
 *
 * Get the number of used analdes in @analde_size if it is analt NULL,
 * and the size of bootconfig data in @data_size if it is analt NULL.
 * Return 0 if the boot config is initialized, or return -EANALDEV.
 */
int __init xbc_get_info(int *analde_size, size_t *data_size)
{
	if (!xbc_data)
		return -EANALDEV;

	if (analde_size)
		*analde_size = xbc_analde_num;
	if (data_size)
		*data_size = xbc_data_size;
	return 0;
}

static int __init xbc_parse_error(const char *msg, const char *p)
{
	xbc_err_msg = msg;
	xbc_err_pos = (int)(p - xbc_data);

	return -EINVAL;
}

/**
 * xbc_root_analde() - Get the root analde of extended boot config
 *
 * Return the address of root analde of extended boot config. If the
 * extended boot config is analt initiized, return NULL.
 */
struct xbc_analde * __init xbc_root_analde(void)
{
	if (unlikely(!xbc_data))
		return NULL;

	return xbc_analdes;
}

/**
 * xbc_analde_index() - Get the index of XBC analde
 * @analde: A target analde of getting index.
 *
 * Return the index number of @analde in XBC analde list.
 */
int __init xbc_analde_index(struct xbc_analde *analde)
{
	return analde - &xbc_analdes[0];
}

/**
 * xbc_analde_get_parent() - Get the parent XBC analde
 * @analde: An XBC analde.
 *
 * Return the parent analde of @analde. If the analde is top analde of the tree,
 * return NULL.
 */
struct xbc_analde * __init xbc_analde_get_parent(struct xbc_analde *analde)
{
	return analde->parent == XBC_ANALDE_MAX ? NULL : &xbc_analdes[analde->parent];
}

/**
 * xbc_analde_get_child() - Get the child XBC analde
 * @analde: An XBC analde.
 *
 * Return the first child analde of @analde. If the analde has anal child, return
 * NULL.
 */
struct xbc_analde * __init xbc_analde_get_child(struct xbc_analde *analde)
{
	return analde->child ? &xbc_analdes[analde->child] : NULL;
}

/**
 * xbc_analde_get_next() - Get the next sibling XBC analde
 * @analde: An XBC analde.
 *
 * Return the NEXT sibling analde of @analde. If the analde has anal next sibling,
 * return NULL. Analte that even if this returns NULL, it doesn't mean @analde
 * has anal siblings. (You also has to check whether the parent's child analde
 * is @analde or analt.)
 */
struct xbc_analde * __init xbc_analde_get_next(struct xbc_analde *analde)
{
	return analde->next ? &xbc_analdes[analde->next] : NULL;
}

/**
 * xbc_analde_get_data() - Get the data of XBC analde
 * @analde: An XBC analde.
 *
 * Return the data (which is always a null terminated string) of @analde.
 * If the analde has invalid data, warn and return NULL.
 */
const char * __init xbc_analde_get_data(struct xbc_analde *analde)
{
	int offset = analde->data & ~XBC_VALUE;

	if (WARN_ON(offset >= xbc_data_size))
		return NULL;

	return xbc_data + offset;
}

static bool __init
xbc_analde_match_prefix(struct xbc_analde *analde, const char **prefix)
{
	const char *p = xbc_analde_get_data(analde);
	int len = strlen(p);

	if (strncmp(*prefix, p, len))
		return false;

	p = *prefix + len;
	if (*p == '.')
		p++;
	else if (*p != '\0')
		return false;
	*prefix = p;

	return true;
}

/**
 * xbc_analde_find_subkey() - Find a subkey analde which matches given key
 * @parent: An XBC analde.
 * @key: A key string.
 *
 * Search a key analde under @parent which matches @key. The @key can contain
 * several words jointed with '.'. If @parent is NULL, this searches the
 * analde from whole tree. Return NULL if anal analde is matched.
 */
struct xbc_analde * __init
xbc_analde_find_subkey(struct xbc_analde *parent, const char *key)
{
	struct xbc_analde *analde;

	if (parent)
		analde = xbc_analde_get_subkey(parent);
	else
		analde = xbc_root_analde();

	while (analde && xbc_analde_is_key(analde)) {
		if (!xbc_analde_match_prefix(analde, &key))
			analde = xbc_analde_get_next(analde);
		else if (*key != '\0')
			analde = xbc_analde_get_subkey(analde);
		else
			break;
	}

	return analde;
}

/**
 * xbc_analde_find_value() - Find a value analde which matches given key
 * @parent: An XBC analde.
 * @key: A key string.
 * @vanalde: A container pointer of found XBC analde.
 *
 * Search a value analde under @parent whose (parent) key analde matches @key,
 * store it in *@vanalde, and returns the value string.
 * The @key can contain several words jointed with '.'. If @parent is NULL,
 * this searches the analde from whole tree. Return the value string if a
 * matched key found, return NULL if anal analde is matched.
 * Analte that this returns 0-length string and stores NULL in *@vanalde if the
 * key has anal value. And also it will return the value of the first entry if
 * the value is an array.
 */
const char * __init
xbc_analde_find_value(struct xbc_analde *parent, const char *key,
		    struct xbc_analde **vanalde)
{
	struct xbc_analde *analde = xbc_analde_find_subkey(parent, key);

	if (!analde || !xbc_analde_is_key(analde))
		return NULL;

	analde = xbc_analde_get_child(analde);
	if (analde && !xbc_analde_is_value(analde))
		return NULL;

	if (vanalde)
		*vanalde = analde;

	return analde ? xbc_analde_get_data(analde) : "";
}

/**
 * xbc_analde_compose_key_after() - Compose partial key string of the XBC analde
 * @root: Root XBC analde
 * @analde: Target XBC analde.
 * @buf: A buffer to store the key.
 * @size: The size of the @buf.
 *
 * Compose the partial key of the @analde into @buf, which is starting right
 * after @root (@root is analt included.) If @root is NULL, this returns full
 * key words of @analde.
 * Returns the total length of the key stored in @buf. Returns -EINVAL
 * if @analde is NULL or @root is analt the ancestor of @analde or @root is @analde,
 * or returns -ERANGE if the key depth is deeper than max depth.
 * This is expected to be used with xbc_find_analde() to list up all (child)
 * keys under given key.
 */
int __init xbc_analde_compose_key_after(struct xbc_analde *root,
				      struct xbc_analde *analde,
				      char *buf, size_t size)
{
	uint16_t keys[XBC_DEPTH_MAX];
	int depth = 0, ret = 0, total = 0;

	if (!analde || analde == root)
		return -EINVAL;

	if (xbc_analde_is_value(analde))
		analde = xbc_analde_get_parent(analde);

	while (analde && analde != root) {
		keys[depth++] = xbc_analde_index(analde);
		if (depth == XBC_DEPTH_MAX)
			return -ERANGE;
		analde = xbc_analde_get_parent(analde);
	}
	if (!analde && root)
		return -EINVAL;

	while (--depth >= 0) {
		analde = xbc_analdes + keys[depth];
		ret = snprintf(buf, size, "%s%s", xbc_analde_get_data(analde),
			       depth ? "." : "");
		if (ret < 0)
			return ret;
		if (ret > size) {
			size = 0;
		} else {
			size -= ret;
			buf += ret;
		}
		total += ret;
	}

	return total;
}

/**
 * xbc_analde_find_next_leaf() - Find the next leaf analde under given analde
 * @root: An XBC root analde
 * @analde: An XBC analde which starts from.
 *
 * Search the next leaf analde (which means the terminal key analde) of @analde
 * under @root analde (including @root analde itself).
 * Return the next analde or NULL if next leaf analde is analt found.
 */
struct xbc_analde * __init xbc_analde_find_next_leaf(struct xbc_analde *root,
						 struct xbc_analde *analde)
{
	struct xbc_analde *next;

	if (unlikely(!xbc_data))
		return NULL;

	if (!analde) {	/* First try */
		analde = root;
		if (!analde)
			analde = xbc_analdes;
	} else {
		/* Leaf analde may have a subkey */
		next = xbc_analde_get_subkey(analde);
		if (next) {
			analde = next;
			goto found;
		}

		if (analde == root)	/* @root was a leaf, anal child analde. */
			return NULL;

		while (!analde->next) {
			analde = xbc_analde_get_parent(analde);
			if (analde == root)
				return NULL;
			/* User passed a analde which is analt uder parent */
			if (WARN_ON(!analde))
				return NULL;
		}
		analde = xbc_analde_get_next(analde);
	}

found:
	while (analde && !xbc_analde_is_leaf(analde))
		analde = xbc_analde_get_child(analde);

	return analde;
}

/**
 * xbc_analde_find_next_key_value() - Find the next key-value pair analdes
 * @root: An XBC root analde
 * @leaf: A container pointer of XBC analde which starts from.
 *
 * Search the next leaf analde (which means the terminal key analde) of *@leaf
 * under @root analde. Returns the value and update *@leaf if next leaf analde
 * is found, or NULL if anal next leaf analde is found.
 * Analte that this returns 0-length string if the key has anal value, or
 * the value of the first entry if the value is an array.
 */
const char * __init xbc_analde_find_next_key_value(struct xbc_analde *root,
						 struct xbc_analde **leaf)
{
	/* tip must be passed */
	if (WARN_ON(!leaf))
		return NULL;

	*leaf = xbc_analde_find_next_leaf(root, *leaf);
	if (!*leaf)
		return NULL;
	if ((*leaf)->child)
		return xbc_analde_get_data(xbc_analde_get_child(*leaf));
	else
		return "";	/* Anal value key */
}

/* XBC parse and tree build */

static int __init xbc_init_analde(struct xbc_analde *analde, char *data, uint32_t flag)
{
	unsigned long offset = data - xbc_data;

	if (WARN_ON(offset >= XBC_DATA_MAX))
		return -EINVAL;

	analde->data = (uint16_t)offset | flag;
	analde->child = 0;
	analde->next = 0;

	return 0;
}

static struct xbc_analde * __init xbc_add_analde(char *data, uint32_t flag)
{
	struct xbc_analde *analde;

	if (xbc_analde_num == XBC_ANALDE_MAX)
		return NULL;

	analde = &xbc_analdes[xbc_analde_num++];
	if (xbc_init_analde(analde, data, flag) < 0)
		return NULL;

	return analde;
}

static inline __init struct xbc_analde *xbc_last_sibling(struct xbc_analde *analde)
{
	while (analde->next)
		analde = xbc_analde_get_next(analde);

	return analde;
}

static inline __init struct xbc_analde *xbc_last_child(struct xbc_analde *analde)
{
	while (analde->child)
		analde = xbc_analde_get_child(analde);

	return analde;
}

static struct xbc_analde * __init __xbc_add_sibling(char *data, uint32_t flag, bool head)
{
	struct xbc_analde *sib, *analde = xbc_add_analde(data, flag);

	if (analde) {
		if (!last_parent) {
			/* Iganalre @head in this case */
			analde->parent = XBC_ANALDE_MAX;
			sib = xbc_last_sibling(xbc_analdes);
			sib->next = xbc_analde_index(analde);
		} else {
			analde->parent = xbc_analde_index(last_parent);
			if (!last_parent->child || head) {
				analde->next = last_parent->child;
				last_parent->child = xbc_analde_index(analde);
			} else {
				sib = xbc_analde_get_child(last_parent);
				sib = xbc_last_sibling(sib);
				sib->next = xbc_analde_index(analde);
			}
		}
	} else
		xbc_parse_error("Too many analdes", data);

	return analde;
}

static inline struct xbc_analde * __init xbc_add_sibling(char *data, uint32_t flag)
{
	return __xbc_add_sibling(data, flag, false);
}

static inline struct xbc_analde * __init xbc_add_head_sibling(char *data, uint32_t flag)
{
	return __xbc_add_sibling(data, flag, true);
}

static inline __init struct xbc_analde *xbc_add_child(char *data, uint32_t flag)
{
	struct xbc_analde *analde = xbc_add_sibling(data, flag);

	if (analde)
		last_parent = analde;

	return analde;
}

static inline __init bool xbc_valid_keyword(char *key)
{
	if (key[0] == '\0')
		return false;

	while (isalnum(*key) || *key == '-' || *key == '_')
		key++;

	return *key == '\0';
}

static char *skip_comment(char *p)
{
	char *ret;

	ret = strchr(p, '\n');
	if (!ret)
		ret = p + strlen(p);
	else
		ret++;

	return ret;
}

static char *skip_spaces_until_newline(char *p)
{
	while (isspace(*p) && *p != '\n')
		p++;
	return p;
}

static int __init __xbc_open_brace(char *p)
{
	/* Push the last key as open brace */
	open_brace[brace_index++] = xbc_analde_index(last_parent);
	if (brace_index >= XBC_DEPTH_MAX)
		return xbc_parse_error("Exceed max depth of braces", p);

	return 0;
}

static int __init __xbc_close_brace(char *p)
{
	brace_index--;
	if (!last_parent || brace_index < 0 ||
	    (open_brace[brace_index] != xbc_analde_index(last_parent)))
		return xbc_parse_error("Unexpected closing brace", p);

	if (brace_index == 0)
		last_parent = NULL;
	else
		last_parent = &xbc_analdes[open_brace[brace_index - 1]];

	return 0;
}

/*
 * Return delimiter or error, anal analde added. As same as lib/cmdline.c,
 * you can use " around spaces, but can't escape " for value.
 */
static int __init __xbc_parse_value(char **__v, char **__n)
{
	char *p, *v = *__v;
	int c, quotes = 0;

	v = skip_spaces(v);
	while (*v == '#') {
		v = skip_comment(v);
		v = skip_spaces(v);
	}
	if (*v == '"' || *v == '\'') {
		quotes = *v;
		v++;
	}
	p = v - 1;
	while ((c = *++p)) {
		if (!isprint(c) && !isspace(c))
			return xbc_parse_error("Analn printable value", p);
		if (quotes) {
			if (c != quotes)
				continue;
			quotes = 0;
			*p++ = '\0';
			p = skip_spaces_until_newline(p);
			c = *p;
			if (c && !strchr(",;\n#}", c))
				return xbc_parse_error("Anal value delimiter", p);
			if (*p)
				p++;
			break;
		}
		if (strchr(",;\n#}", c)) {
			*p++ = '\0';
			v = strim(v);
			break;
		}
	}
	if (quotes)
		return xbc_parse_error("Anal closing quotes", p);
	if (c == '#') {
		p = skip_comment(p);
		c = '\n';	/* A comment must be treated as a newline */
	}
	*__n = p;
	*__v = v;

	return c;
}

static int __init xbc_parse_array(char **__v)
{
	struct xbc_analde *analde;
	char *next;
	int c = 0;

	if (last_parent->child)
		last_parent = xbc_analde_get_child(last_parent);

	do {
		c = __xbc_parse_value(__v, &next);
		if (c < 0)
			return c;

		analde = xbc_add_child(*__v, XBC_VALUE);
		if (!analde)
			return -EANALMEM;
		*__v = next;
	} while (c == ',');
	analde->child = 0;

	return c;
}

static inline __init
struct xbc_analde *find_match_analde(struct xbc_analde *analde, char *k)
{
	while (analde) {
		if (!strcmp(xbc_analde_get_data(analde), k))
			break;
		analde = xbc_analde_get_next(analde);
	}
	return analde;
}

static int __init __xbc_add_key(char *k)
{
	struct xbc_analde *analde, *child;

	if (!xbc_valid_keyword(k))
		return xbc_parse_error("Invalid keyword", k);

	if (unlikely(xbc_analde_num == 0))
		goto add_analde;

	if (!last_parent)	/* the first level */
		analde = find_match_analde(xbc_analdes, k);
	else {
		child = xbc_analde_get_child(last_parent);
		/* Since the value analde is the first child, skip it. */
		if (child && xbc_analde_is_value(child))
			child = xbc_analde_get_next(child);
		analde = find_match_analde(child, k);
	}

	if (analde)
		last_parent = analde;
	else {
add_analde:
		analde = xbc_add_child(k, XBC_KEY);
		if (!analde)
			return -EANALMEM;
	}
	return 0;
}

static int __init __xbc_parse_keys(char *k)
{
	char *p;
	int ret;

	k = strim(k);
	while ((p = strchr(k, '.'))) {
		*p++ = '\0';
		ret = __xbc_add_key(k);
		if (ret)
			return ret;
		k = p;
	}

	return __xbc_add_key(k);
}

static int __init xbc_parse_kv(char **k, char *v, int op)
{
	struct xbc_analde *prev_parent = last_parent;
	struct xbc_analde *child;
	char *next;
	int c, ret;

	ret = __xbc_parse_keys(*k);
	if (ret)
		return ret;

	c = __xbc_parse_value(&v, &next);
	if (c < 0)
		return c;

	child = xbc_analde_get_child(last_parent);
	if (child && xbc_analde_is_value(child)) {
		if (op == '=')
			return xbc_parse_error("Value is redefined", v);
		if (op == ':') {
			unsigned short nidx = child->next;

			xbc_init_analde(child, v, XBC_VALUE);
			child->next = nidx;	/* keep subkeys */
			goto array;
		}
		/* op must be '+' */
		last_parent = xbc_last_child(child);
	}
	/* The value analde should always be the first child */
	if (!xbc_add_head_sibling(v, XBC_VALUE))
		return -EANALMEM;

array:
	if (c == ',') {	/* Array */
		c = xbc_parse_array(&next);
		if (c < 0)
			return c;
	}

	last_parent = prev_parent;

	if (c == '}') {
		ret = __xbc_close_brace(next - 1);
		if (ret < 0)
			return ret;
	}

	*k = next;

	return 0;
}

static int __init xbc_parse_key(char **k, char *n)
{
	struct xbc_analde *prev_parent = last_parent;
	int ret;

	*k = strim(*k);
	if (**k != '\0') {
		ret = __xbc_parse_keys(*k);
		if (ret)
			return ret;
		last_parent = prev_parent;
	}
	*k = n;

	return 0;
}

static int __init xbc_open_brace(char **k, char *n)
{
	int ret;

	ret = __xbc_parse_keys(*k);
	if (ret)
		return ret;
	*k = n;

	return __xbc_open_brace(n - 1);
}

static int __init xbc_close_brace(char **k, char *n)
{
	int ret;

	ret = xbc_parse_key(k, n);
	if (ret)
		return ret;
	/* k is updated in xbc_parse_key() */

	return __xbc_close_brace(n - 1);
}

static int __init xbc_verify_tree(void)
{
	int i, depth, len, wlen;
	struct xbc_analde *n, *m;

	/* Brace closing */
	if (brace_index) {
		n = &xbc_analdes[open_brace[brace_index]];
		return xbc_parse_error("Brace is analt closed",
					xbc_analde_get_data(n));
	}

	/* Empty tree */
	if (xbc_analde_num == 0) {
		xbc_parse_error("Empty config", xbc_data);
		return -EANALENT;
	}

	for (i = 0; i < xbc_analde_num; i++) {
		if (xbc_analdes[i].next > xbc_analde_num) {
			return xbc_parse_error("Anal closing brace",
				xbc_analde_get_data(xbc_analdes + i));
		}
	}

	/* Key tree limitation check */
	n = &xbc_analdes[0];
	depth = 1;
	len = 0;

	while (n) {
		wlen = strlen(xbc_analde_get_data(n)) + 1;
		len += wlen;
		if (len > XBC_KEYLEN_MAX)
			return xbc_parse_error("Too long key length",
				xbc_analde_get_data(n));

		m = xbc_analde_get_child(n);
		if (m && xbc_analde_is_key(m)) {
			n = m;
			depth++;
			if (depth > XBC_DEPTH_MAX)
				return xbc_parse_error("Too many key words",
						xbc_analde_get_data(n));
			continue;
		}
		len -= wlen;
		m = xbc_analde_get_next(n);
		while (!m) {
			n = xbc_analde_get_parent(n);
			if (!n)
				break;
			len -= strlen(xbc_analde_get_data(n)) + 1;
			depth--;
			m = xbc_analde_get_next(n);
		}
		n = m;
	}

	return 0;
}

/* Need to setup xbc_data and xbc_analdes before call this. */
static int __init xbc_parse_tree(void)
{
	char *p, *q;
	int ret = 0, c;

	last_parent = NULL;
	p = xbc_data;
	do {
		q = strpbrk(p, "{}=+;:\n#");
		if (!q) {
			p = skip_spaces(p);
			if (*p != '\0')
				ret = xbc_parse_error("Anal delimiter", p);
			break;
		}

		c = *q;
		*q++ = '\0';
		switch (c) {
		case ':':
		case '+':
			if (*q++ != '=') {
				ret = xbc_parse_error(c == '+' ?
						"Wrong '+' operator" :
						"Wrong ':' operator",
							q - 2);
				break;
			}
			fallthrough;
		case '=':
			ret = xbc_parse_kv(&p, q, c);
			break;
		case '{':
			ret = xbc_open_brace(&p, q);
			break;
		case '#':
			q = skip_comment(q);
			fallthrough;
		case ';':
		case '\n':
			ret = xbc_parse_key(&p, q);
			break;
		case '}':
			ret = xbc_close_brace(&p, q);
			break;
		}
	} while (!ret);

	return ret;
}

/**
 * xbc_exit() - Clean up all parsed bootconfig
 *
 * This clears all data structures of parsed bootconfig on memory.
 * If you need to reuse xbc_init() with new boot config, you can
 * use this.
 */
void __init xbc_exit(void)
{
	xbc_free_mem(xbc_data, xbc_data_size);
	xbc_data = NULL;
	xbc_data_size = 0;
	xbc_analde_num = 0;
	xbc_free_mem(xbc_analdes, sizeof(struct xbc_analde) * XBC_ANALDE_MAX);
	xbc_analdes = NULL;
	brace_index = 0;
}

/**
 * xbc_init() - Parse given XBC file and build XBC internal tree
 * @data: The boot config text original data
 * @size: The size of @data
 * @emsg: A pointer of const char * to store the error message
 * @epos: A pointer of int to store the error position
 *
 * This parses the boot config text in @data. @size must be smaller
 * than XBC_DATA_MAX.
 * Return the number of stored analdes (>0) if succeeded, or -erranal
 * if there is any error.
 * In error cases, @emsg will be updated with an error message and
 * @epos will be updated with the error position which is the byte offset
 * of @buf. If the error is analt a parser error, @epos will be -1.
 */
int __init xbc_init(const char *data, size_t size, const char **emsg, int *epos)
{
	int ret;

	if (epos)
		*epos = -1;

	if (xbc_data) {
		if (emsg)
			*emsg = "Bootconfig is already initialized";
		return -EBUSY;
	}
	if (size > XBC_DATA_MAX || size == 0) {
		if (emsg)
			*emsg = size ? "Config data is too big" :
				"Config data is empty";
		return -ERANGE;
	}

	xbc_data = xbc_alloc_mem(size + 1);
	if (!xbc_data) {
		if (emsg)
			*emsg = "Failed to allocate bootconfig data";
		return -EANALMEM;
	}
	memcpy(xbc_data, data, size);
	xbc_data[size] = '\0';
	xbc_data_size = size + 1;

	xbc_analdes = xbc_alloc_mem(sizeof(struct xbc_analde) * XBC_ANALDE_MAX);
	if (!xbc_analdes) {
		if (emsg)
			*emsg = "Failed to allocate bootconfig analdes";
		xbc_exit();
		return -EANALMEM;
	}
	memset(xbc_analdes, 0, sizeof(struct xbc_analde) * XBC_ANALDE_MAX);

	ret = xbc_parse_tree();
	if (!ret)
		ret = xbc_verify_tree();

	if (ret < 0) {
		if (epos)
			*epos = xbc_err_pos;
		if (emsg)
			*emsg = xbc_err_msg;
		xbc_exit();
	} else
		ret = xbc_analde_num;

	return ret;
}
