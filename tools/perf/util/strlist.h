#ifndef __PERF_STRLIST_H
#define __PERF_STRLIST_H

#include <linux/rbtree.h>
#include <stdbool.h>

struct str_node {
	struct rb_node rb_node;
	const char     *s;
};

struct strlist {
	struct rb_root entries;
	unsigned int   nr_entries;
	bool	       dupstr;
};

struct strlist *strlist__new(bool dupstr, const char *slist);
void strlist__delete(struct strlist *self);

void strlist__remove(struct strlist *self, struct str_node *sn);
int strlist__load(struct strlist *self, const char *filename);
int strlist__add(struct strlist *self, const char *str);

struct str_node *strlist__entry(const struct strlist *self, unsigned int idx);
struct str_node *strlist__find(struct strlist *self, const char *entry);

static inline bool strlist__has_entry(struct strlist *self, const char *entry)
{
	return strlist__find(self, entry) != NULL;
}

static inline bool strlist__empty(const struct strlist *self)
{
	return self->nr_entries == 0;
}

static inline unsigned int strlist__nr_entries(const struct strlist *self)
{
	return self->nr_entries;
}

/* For strlist iteration */
static inline struct str_node *strlist__first(struct strlist *self)
{
	struct rb_node *rn = rb_first(&self->entries);
	return rn ? rb_entry(rn, struct str_node, rb_node) : NULL;
}
static inline struct str_node *strlist__next(struct str_node *sn)
{
	struct rb_node *rn;
	if (!sn)
		return NULL;
	rn = rb_next(&sn->rb_node);
	return rn ? rb_entry(rn, struct str_node, rb_node) : NULL;
}

/**
 * strlist_for_each      - iterate over a strlist
 * @pos:	the &struct str_node to use as a loop cursor.
 * @self:	the &struct strlist for loop.
 */
#define strlist__for_each(pos, self)	\
	for (pos = strlist__first(self); pos; pos = strlist__next(pos))

/**
 * strlist_for_each_safe - iterate over a strlist safe against removal of
 *                         str_node
 * @pos:	the &struct str_node to use as a loop cursor.
 * @n:		another &struct str_node to use as temporary storage.
 * @self:	the &struct strlist for loop.
 */
#define strlist__for_each_safe(pos, n, self)	\
	for (pos = strlist__first(self), n = strlist__next(pos); pos;\
	     pos = n, n = strlist__next(n))

int strlist__parse_list(struct strlist *self, const char *s);
#endif /* __PERF_STRLIST_H */
