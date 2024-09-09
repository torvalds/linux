// SPDX-License-Identifier: GPL-2.0-only
#include <linux/codetag.h>
#include <linux/idr.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

struct codetag_type {
	struct list_head link;
	unsigned int count;
	struct idr mod_idr;
	struct rw_semaphore mod_lock; /* protects mod_idr */
	struct codetag_type_desc desc;
};

struct codetag_range {
	struct codetag *start;
	struct codetag *stop;
};

struct codetag_module {
	struct module *mod;
	struct codetag_range range;
};

static DEFINE_MUTEX(codetag_lock);
static LIST_HEAD(codetag_types);

void codetag_lock_module_list(struct codetag_type *cttype, bool lock)
{
	if (lock)
		down_read(&cttype->mod_lock);
	else
		up_read(&cttype->mod_lock);
}

bool codetag_trylock_module_list(struct codetag_type *cttype)
{
	return down_read_trylock(&cttype->mod_lock) != 0;
}

struct codetag_iterator codetag_get_ct_iter(struct codetag_type *cttype)
{
	struct codetag_iterator iter = {
		.cttype = cttype,
		.cmod = NULL,
		.mod_id = 0,
		.ct = NULL,
	};

	return iter;
}

static inline struct codetag *get_first_module_ct(struct codetag_module *cmod)
{
	return cmod->range.start < cmod->range.stop ? cmod->range.start : NULL;
}

static inline
struct codetag *get_next_module_ct(struct codetag_iterator *iter)
{
	struct codetag *res = (struct codetag *)
			((char *)iter->ct + iter->cttype->desc.tag_size);

	return res < iter->cmod->range.stop ? res : NULL;
}

struct codetag *codetag_next_ct(struct codetag_iterator *iter)
{
	struct codetag_type *cttype = iter->cttype;
	struct codetag_module *cmod;
	struct codetag *ct;

	lockdep_assert_held(&cttype->mod_lock);

	if (unlikely(idr_is_empty(&cttype->mod_idr)))
		return NULL;

	ct = NULL;
	while (true) {
		cmod = idr_find(&cttype->mod_idr, iter->mod_id);

		/* If module was removed move to the next one */
		if (!cmod)
			cmod = idr_get_next_ul(&cttype->mod_idr,
					       &iter->mod_id);

		/* Exit if no more modules */
		if (!cmod)
			break;

		if (cmod != iter->cmod) {
			iter->cmod = cmod;
			ct = get_first_module_ct(cmod);
		} else
			ct = get_next_module_ct(iter);

		if (ct)
			break;

		iter->mod_id++;
	}

	iter->ct = ct;
	return ct;
}

void codetag_to_text(struct seq_buf *out, struct codetag *ct)
{
	if (ct->modname)
		seq_buf_printf(out, "%s:%u [%s] func:%s",
			       ct->filename, ct->lineno,
			       ct->modname, ct->function);
	else
		seq_buf_printf(out, "%s:%u func:%s",
			       ct->filename, ct->lineno, ct->function);
}

static inline size_t range_size(const struct codetag_type *cttype,
				const struct codetag_range *range)
{
	return ((char *)range->stop - (char *)range->start) /
			cttype->desc.tag_size;
}

static void *get_symbol(struct module *mod, const char *prefix, const char *name)
{
	DECLARE_SEQ_BUF(sb, KSYM_NAME_LEN);
	const char *buf;
	void *ret;

	seq_buf_printf(&sb, "%s%s", prefix, name);
	if (seq_buf_has_overflowed(&sb))
		return NULL;

	buf = seq_buf_str(&sb);
	preempt_disable();
	ret = mod ?
		(void *)find_kallsyms_symbol_value(mod, buf) :
		(void *)kallsyms_lookup_name(buf);
	preempt_enable();

	return ret;
}

static struct codetag_range get_section_range(struct module *mod,
					      const char *section)
{
	return (struct codetag_range) {
		get_symbol(mod, "__start_", section),
		get_symbol(mod, "__stop_", section),
	};
}

static const char *get_mod_name(__maybe_unused struct module *mod)
{
#ifdef CONFIG_MODULES
	if (mod)
		return mod->name;
#endif
	return "(built-in)";
}

static int codetag_module_init(struct codetag_type *cttype, struct module *mod)
{
	struct codetag_range range;
	struct codetag_module *cmod;
	int err;

	range = get_section_range(mod, cttype->desc.section);
	if (!range.start || !range.stop) {
		pr_warn("Failed to load code tags of type %s from the module %s\n",
			cttype->desc.section, get_mod_name(mod));
		return -EINVAL;
	}

	/* Ignore empty ranges */
	if (range.start == range.stop)
		return 0;

	BUG_ON(range.start > range.stop);

	cmod = kmalloc(sizeof(*cmod), GFP_KERNEL);
	if (unlikely(!cmod))
		return -ENOMEM;

	cmod->mod = mod;
	cmod->range = range;

	down_write(&cttype->mod_lock);
	err = idr_alloc(&cttype->mod_idr, cmod, 0, 0, GFP_KERNEL);
	if (err >= 0) {
		cttype->count += range_size(cttype, &range);
		if (cttype->desc.module_load)
			cttype->desc.module_load(cttype, cmod);
	}
	up_write(&cttype->mod_lock);

	if (err < 0) {
		kfree(cmod);
		return err;
	}

	return 0;
}

#ifdef CONFIG_MODULES
void codetag_load_module(struct module *mod)
{
	struct codetag_type *cttype;

	if (!mod)
		return;

	mutex_lock(&codetag_lock);
	list_for_each_entry(cttype, &codetag_types, link)
		codetag_module_init(cttype, mod);
	mutex_unlock(&codetag_lock);
}

bool codetag_unload_module(struct module *mod)
{
	struct codetag_type *cttype;
	bool unload_ok = true;

	if (!mod)
		return true;

	mutex_lock(&codetag_lock);
	list_for_each_entry(cttype, &codetag_types, link) {
		struct codetag_module *found = NULL;
		struct codetag_module *cmod;
		unsigned long mod_id, tmp;

		down_write(&cttype->mod_lock);
		idr_for_each_entry_ul(&cttype->mod_idr, cmod, tmp, mod_id) {
			if (cmod->mod && cmod->mod == mod) {
				found = cmod;
				break;
			}
		}
		if (found) {
			if (cttype->desc.module_unload)
				if (!cttype->desc.module_unload(cttype, cmod))
					unload_ok = false;

			cttype->count -= range_size(cttype, &cmod->range);
			idr_remove(&cttype->mod_idr, mod_id);
			kfree(cmod);
		}
		up_write(&cttype->mod_lock);
	}
	mutex_unlock(&codetag_lock);

	return unload_ok;
}
#endif /* CONFIG_MODULES */

struct codetag_type *
codetag_register_type(const struct codetag_type_desc *desc)
{
	struct codetag_type *cttype;
	int err;

	BUG_ON(desc->tag_size <= 0);

	cttype = kzalloc(sizeof(*cttype), GFP_KERNEL);
	if (unlikely(!cttype))
		return ERR_PTR(-ENOMEM);

	cttype->desc = *desc;
	idr_init(&cttype->mod_idr);
	init_rwsem(&cttype->mod_lock);

	err = codetag_module_init(cttype, NULL);
	if (unlikely(err)) {
		kfree(cttype);
		return ERR_PTR(err);
	}

	mutex_lock(&codetag_lock);
	list_add_tail(&cttype->link, &codetag_types);
	mutex_unlock(&codetag_lock);

	return cttype;
}
