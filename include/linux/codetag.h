/* SPDX-License-Identifier: GPL-2.0 */
/*
 * code tagging framework
 */
#ifndef _LINUX_CODETAG_H
#define _LINUX_CODETAG_H

#include <linux/types.h>

struct codetag_iterator;
struct codetag_type;
struct codetag_module;
struct seq_buf;
struct module;

#define CODETAG_SECTION_START_PREFIX	"__start_"
#define CODETAG_SECTION_STOP_PREFIX	"__stop_"

/* codetag flags */
#define CODETAG_FLAG_INACCURATE	(1 << 0)

/*
 * An instance of this structure is created in a special ELF section at every
 * code location being tagged.  At runtime, the special section is treated as
 * an array of these.
 */
struct codetag {
	unsigned int flags;
	unsigned int lineno;
	const char *modname;
	const char *function;
	const char *filename;
} __aligned(8);

union codetag_ref {
	struct codetag *ct;
};

struct codetag_type_desc {
	const char *section;
	size_t tag_size;
	int (*module_load)(struct module *mod,
			   struct codetag *start, struct codetag *end);
	void (*module_unload)(struct module *mod,
			      struct codetag *start, struct codetag *end);
#ifdef CONFIG_MODULES
	void (*module_replaced)(struct module *mod, struct module *new_mod);
	bool (*needs_section_mem)(struct module *mod, unsigned long size);
	void *(*alloc_section_mem)(struct module *mod, unsigned long size,
				   unsigned int prepend, unsigned long align);
	void (*free_section_mem)(struct module *mod, bool used);
#endif
};

struct codetag_iterator {
	struct codetag_type *cttype;
	struct codetag_module *cmod;
	unsigned long mod_id;
	struct codetag *ct;
	unsigned long mod_seq;
};

#ifdef MODULE
#define CT_MODULE_NAME KBUILD_MODNAME
#else
#define CT_MODULE_NAME NULL
#endif

#define CODE_TAG_INIT {					\
	.modname	= CT_MODULE_NAME,		\
	.function	= __func__,			\
	.filename	= __FILE__,			\
	.lineno		= __LINE__,			\
	.flags		= 0,				\
}

void codetag_lock_module_list(struct codetag_type *cttype, bool lock);
bool codetag_trylock_module_list(struct codetag_type *cttype);
struct codetag_iterator codetag_get_ct_iter(struct codetag_type *cttype);
struct codetag *codetag_next_ct(struct codetag_iterator *iter);

void codetag_to_text(struct seq_buf *out, struct codetag *ct);

struct codetag_type *
codetag_register_type(const struct codetag_type_desc *desc);

#if defined(CONFIG_CODE_TAGGING) && defined(CONFIG_MODULES)

bool codetag_needs_module_section(struct module *mod, const char *name,
				  unsigned long size);
void *codetag_alloc_module_section(struct module *mod, const char *name,
				   unsigned long size, unsigned int prepend,
				   unsigned long align);
void codetag_free_module_sections(struct module *mod);
void codetag_module_replaced(struct module *mod, struct module *new_mod);
int codetag_load_module(struct module *mod);
void codetag_unload_module(struct module *mod);

#else /* defined(CONFIG_CODE_TAGGING) && defined(CONFIG_MODULES) */

static inline bool
codetag_needs_module_section(struct module *mod, const char *name,
			     unsigned long size) { return false; }
static inline void *
codetag_alloc_module_section(struct module *mod, const char *name,
			     unsigned long size, unsigned int prepend,
			     unsigned long align) { return NULL; }
static inline void codetag_free_module_sections(struct module *mod) {}
static inline void codetag_module_replaced(struct module *mod, struct module *new_mod) {}
static inline int codetag_load_module(struct module *mod) { return 0; }
static inline void codetag_unload_module(struct module *mod) {}

#endif /* defined(CONFIG_CODE_TAGGING) && defined(CONFIG_MODULES) */

#endif /* _LINUX_CODETAG_H */
