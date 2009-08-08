#ifndef STRLIST_H_
#define STRLIST_H_

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
bool strlist__has_entry(struct strlist *self, const char *entry);

static inline bool strlist__empty(const struct strlist *self)
{
	return self->nr_entries == 0;
}

static inline unsigned int strlist__nr_entries(const struct strlist *self)
{
	return self->nr_entries;
}

int strlist__parse_list(struct strlist *self, const char *s);
#endif /* STRLIST_H_ */
