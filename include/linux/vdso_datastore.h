/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VDSO_DATASTORE_H
#define _LINUX_VDSO_DATASTORE_H

#include <linux/mm_types.h>

extern const struct vm_special_mapping vdso_vvar_mapping;
struct vm_area_struct *vdso_install_vvar_mapping(struct mm_struct *mm, unsigned long addr);

#ifdef CONFIG_VDSO_DATASTORE
void __init vdso_setup_data_pages(void);
#else /* !CONFIG_VDSO_DATASTORE */
static inline void vdso_setup_data_pages(void) { }
#endif /* CONFIG_VDSO_DATASTORE */

#endif /* _LINUX_VDSO_DATASTORE_H */
