/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef CONFIG_MMU

#ifdef CONFIG_INET
extern const struct vm_operations_struct tcp_vm_ops;
static inline bool vma_is_tcp(const struct vm_area_struct *vma)
{
	return vma->vm_ops == &tcp_vm_ops;
}
#else
static inline bool vma_is_tcp(const struct vm_area_struct *vma)
{
	return false;
}
#endif /* CONFIG_INET*/

#endif /* CONFIG_MMU */
