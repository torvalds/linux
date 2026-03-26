/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TIME_NAMESPACE_INTERNAL_H
#define _TIME_NAMESPACE_INTERNAL_H

#include <linux/mutex.h>

struct time_namespace;

/*
 * Protects possibly multiple offsets writers racing each other
 * and tasks entering the namespace.
 */
extern struct mutex timens_offset_lock;

#ifdef CONFIG_TIME_NS_VDSO
int timens_vdso_alloc_vvar_page(struct time_namespace *ns);
void timens_vdso_free_vvar_page(struct time_namespace *ns);
#else /* !CONFIG_TIME_NS_VDSO */
static inline int timens_vdso_alloc_vvar_page(struct time_namespace *ns)
{
	return 0;
}
static inline void timens_vdso_free_vvar_page(struct time_namespace *ns)
{
}
#endif /* CONFIG_TIME_NS_VDSO */

#endif /* _TIME_NAMESPACE_INTERNAL_H */
