/*
 * swapcache pages are stored in the swapper_space radix tree.  We want to
 * get good packing density in that tree, so the index should be dense in
 * the low-order bits.
 *
 * We arrange the `type' and `offset' fields so that `type' is at the five
 * high-order bits of the swp_entry_t and `offset' is right-aligned in the
 * remaining bits.
 *
 * swp_entry_t's are *never* stored anywhere in their arch-dependent format.
 */
#define SWP_TYPE_SHIFT(e)	(sizeof(e.val) * 8 - MAX_SWAPFILES_SHIFT)
#define SWP_OFFSET_MASK(e)	((1UL << SWP_TYPE_SHIFT(e)) - 1)

/*
 * Store a type+offset into a swp_entry_t in an arch-independent format
 */
static inline swp_entry_t swp_entry(unsigned long type, pgoff_t offset)
{
	swp_entry_t ret;

	ret.val = (type << SWP_TYPE_SHIFT(ret)) |
			(offset & SWP_OFFSET_MASK(ret));
	return ret;
}

/*
 * Extract the `type' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline unsigned swp_type(swp_entry_t entry)
{
	return (entry.val >> SWP_TYPE_SHIFT(entry));
}

/*
 * Extract the `offset' field from a swp_entry_t.  The swp_entry_t is in
 * arch-independent format
 */
static inline pgoff_t swp_offset(swp_entry_t entry)
{
	return entry.val & SWP_OFFSET_MASK(entry);
}

/*
 * Convert the arch-dependent pte representation of a swp_entry_t into an
 * arch-independent swp_entry_t.
 */
static inline swp_entry_t pte_to_swp_entry(pte_t pte)
{
	swp_entry_t arch_entry;

	BUG_ON(pte_file(pte));
	arch_entry = __pte_to_swp_entry(pte);
	return swp_entry(__swp_type(arch_entry), __swp_offset(arch_entry));
}

/*
 * Convert the arch-independent representation of a swp_entry_t into the
 * arch-dependent pte representation.
 */
static inline pte_t swp_entry_to_pte(swp_entry_t entry)
{
	swp_entry_t arch_entry;

	arch_entry = __swp_entry(swp_type(entry), swp_offset(entry));
	BUG_ON(pte_file(__swp_entry_to_pte(arch_entry)));
	return __swp_entry_to_pte(arch_entry);
}

#ifdef CONFIG_MIGRATION
static inline swp_entry_t make_migration_entry(struct page *page, int write)
{
	BUG_ON(!PageLocked(page));
	return swp_entry(write ? SWP_MIGRATION_WRITE : SWP_MIGRATION_READ,
			page_to_pfn(page));
}

static inline int is_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_READ ||
			swp_type(entry) == SWP_MIGRATION_WRITE);
}

static inline int is_write_migration_entry(swp_entry_t entry)
{
	return unlikely(swp_type(entry) == SWP_MIGRATION_WRITE);
}

static inline struct page *migration_entry_to_page(swp_entry_t entry)
{
	struct page *p = pfn_to_page(swp_offset(entry));
	/*
	 * Any use of migration entries may only occur while the
	 * corresponding page is locked
	 */
	BUG_ON(!PageLocked(p));
	return p;
}

static inline void make_migration_entry_read(swp_entry_t *entry)
{
	*entry = swp_entry(SWP_MIGRATION_READ, swp_offset(*entry));
}

extern void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					unsigned long address);
#else

#define make_migration_entry(page, write) swp_entry(0, 0)
static inline int is_migration_entry(swp_entry_t swp)
{
	return 0;
}
#define migration_entry_to_page(swp) NULL
static inline void make_migration_entry_read(swp_entry_t *entryp) { }
static inline void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
					 unsigned long address) { }
static inline int is_write_migration_entry(swp_entry_t entry)
{
	return 0;
}

#endif

