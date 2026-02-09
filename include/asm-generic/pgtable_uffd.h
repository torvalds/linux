#ifndef _ASM_GENERIC_PGTABLE_UFFD_H
#define _ASM_GENERIC_PGTABLE_UFFD_H

/*
 * Some platforms can customize the uffd-wp bit, making it unavailable
 * even if the architecture provides the resource.
 * Adding this API allows architectures to add their own checks for the
 * devices on which the kernel is running.
 * Note: When overriding it, please make sure the
 * CONFIG_HAVE_ARCH_USERFAULTFD_WP is part of this macro.
 */
#ifndef pgtable_supports_uffd_wp
#define pgtable_supports_uffd_wp()	IS_ENABLED(CONFIG_HAVE_ARCH_USERFAULTFD_WP)
#endif

static inline bool uffd_supports_wp_marker(void)
{
	return pgtable_supports_uffd_wp() && IS_ENABLED(CONFIG_PTE_MARKER_UFFD_WP);
}

#ifndef CONFIG_HAVE_ARCH_USERFAULTFD_WP
static __always_inline int pte_uffd_wp(pte_t pte)
{
	return 0;
}

static __always_inline int pmd_uffd_wp(pmd_t pmd)
{
	return 0;
}

static __always_inline pte_t pte_mkuffd_wp(pte_t pte)
{
	return pte;
}

static __always_inline pmd_t pmd_mkuffd_wp(pmd_t pmd)
{
	return pmd;
}

static __always_inline pte_t pte_clear_uffd_wp(pte_t pte)
{
	return pte;
}

static __always_inline pmd_t pmd_clear_uffd_wp(pmd_t pmd)
{
	return pmd;
}

static __always_inline pte_t pte_swp_mkuffd_wp(pte_t pte)
{
	return pte;
}

static __always_inline int pte_swp_uffd_wp(pte_t pte)
{
	return 0;
}

static __always_inline pte_t pte_swp_clear_uffd_wp(pte_t pte)
{
	return pte;
}

static inline pmd_t pmd_swp_mkuffd_wp(pmd_t pmd)
{
	return pmd;
}

static inline int pmd_swp_uffd_wp(pmd_t pmd)
{
	return 0;
}

static inline pmd_t pmd_swp_clear_uffd_wp(pmd_t pmd)
{
	return pmd;
}
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_WP */

#endif /* _ASM_GENERIC_PGTABLE_UFFD_H */
