#ifndef _PERF_MODULE_
#define _PERF_MODULE_ 1

#include <linux/types.h>
#include "../types.h"
#include <linux/list.h>
#include <linux/rbtree.h>

struct section {
	struct rb_node	rb_node;
	u64		hash;
	u64		vma;
	char		*name;
	char		*path;
};

struct sec_dso {
	struct list_head node;
	struct rb_root	 secs;
	struct section    *(*find_section)(struct sec_dso *, const char *name);
	char		 name[0];
};

struct module {
	struct rb_node	rb_node;
	u64		hash;
	char		*name;
	char		*path;
	struct sec_dso	*sections;
	int		active;
};

struct mod_dso {
	struct list_head node;
	struct rb_root	 mods;
	struct module    *(*find_module)(struct mod_dso *, const char *name);
	char		 name[0];
};

struct sec_dso *sec_dso__new_dso(const char *name);
void sec_dso__delete_sections(struct sec_dso *self);
void sec_dso__delete_self(struct sec_dso *self);
size_t sec_dso__fprintf(struct sec_dso *self, FILE *fp);
struct section *sec_dso__find_section(struct sec_dso *self, const char *name);

struct mod_dso *mod_dso__new_dso(const char *name);
void mod_dso__delete_modules(struct mod_dso *self);
void mod_dso__delete_self(struct mod_dso *self);
size_t mod_dso__fprintf(struct mod_dso *self, FILE *fp);
struct module *mod_dso__find_module(struct mod_dso *self, const char *name);
int mod_dso__load_modules(struct mod_dso *dso);

#endif /* _PERF_MODULE_ */
