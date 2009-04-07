#ifndef LINUX_PAGE_DEBUG_FLAGS_H
#define  LINUX_PAGE_DEBUG_FLAGS_H

/*
 * page->debug_flags bits:
 *
 * PAGE_DEBUG_FLAG_POISON is set for poisoned pages. This is used to
 * implement generic debug pagealloc feature. The pages are filled with
 * poison patterns and set this flag after free_pages(). The poisoned
 * pages are verified whether the patterns are not corrupted and clear
 * the flag before alloc_pages().
 */

enum page_debug_flags {
	PAGE_DEBUG_FLAG_POISON,		/* Page is poisoned */
};

/*
 * Ensure that CONFIG_WANT_PAGE_DEBUG_FLAGS reliably
 * gets turned off when no debug features are enabling it!
 */

#ifdef CONFIG_WANT_PAGE_DEBUG_FLAGS
#if !defined(CONFIG_PAGE_POISONING) \
/* && !defined(CONFIG_PAGE_DEBUG_SOMETHING_ELSE) && ... */
#error WANT_PAGE_DEBUG_FLAGS is turned on with no debug features!
#endif
#endif /* CONFIG_WANT_PAGE_DEBUG_FLAGS */

#endif /* LINUX_PAGE_DEBUG_FLAGS_H */
