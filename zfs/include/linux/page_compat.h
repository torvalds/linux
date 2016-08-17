#ifndef	_ZFS_PAGE_COMPAT_H
#define	_ZFS_PAGE_COMPAT_H

/*
 * We have various enum members moving between two separate enum types,
 * and accessed by different functions at various times. Centralise the
 * insanity.
 *
 * < v4.8: all enums in zone_stat_item, via global_page_state()
 * v4.8: some enums moved to node_stat_item, global_node_page_state() introduced
 * v4.13: some enums moved from zone_stat_item to node_state_item
 * v4.14: global_page_state() rename to global_zone_page_state()
 *
 * The defines used here are created by config/kernel-global_page_state.m4
 */

/*
 * Create our own accessor functions to follow the Linux API changes
 */
#if	defined(ZFS_GLOBAL_ZONE_PAGE_STATE)

/* global_zone_page_state() introduced */
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_FILE_PAGES)
#define	nr_file_pages() global_node_page_state(NR_FILE_PAGES)
#else
#define	nr_file_pages() global_zone_page_state(NR_FILE_PAGES)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_INACTIVE_ANON)
#define	nr_inactive_anon_pages() global_node_page_state(NR_INACTIVE_ANON)
#else
#define	nr_inactive_anon_pages() global_zone_page_state(NR_INACTIVE_ANON)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_INACTIVE_FILE)
#define	nr_inactive_file_pages() global_node_page_state(NR_INACTIVE_FILE)
#else
#define	nr_inactive_file_pages() global_zone_page_state(NR_INACTIVE_FILE)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_SLAB_RECLAIMABLE)
#define	nr_slab_reclaimable_pages() global_node_page_state(NR_SLAB_RECLAIMABLE)
#else
#define	nr_slab_reclaimable_pages() global_zone_page_state(NR_SLAB_RECLAIMABLE)
#endif

#elif	defined(ZFS_GLOBAL_NODE_PAGE_STATE)

/* global_node_page_state() introduced */
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_FILE_PAGES)
#define	nr_file_pages() global_node_page_state(NR_FILE_PAGES)
#else
#define	nr_file_pages() global_page_state(NR_FILE_PAGES)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_INACTIVE_ANON)
#define	nr_inactive_anon_pages() global_node_page_state(NR_INACTIVE_ANON)
#else
#define	nr_inactive_anon_pages() global_page_state(NR_INACTIVE_ANON)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_INACTIVE_FILE)
#define	nr_inactive_file_pages() global_node_page_state(NR_INACTIVE_FILE)
#else
#define	nr_inactive_file_pages() global_page_state(NR_INACTIVE_FILE)
#endif
#if	defined(ZFS_ENUM_NODE_STAT_ITEM_NR_SLAB_RECLAIMABLE)
#define	nr_slab_reclaimable_pages() global_node_page_state(NR_SLAB_RECLAIMABLE)
#else
#define	nr_slab_reclaimable_pages() global_page_state(NR_SLAB_RECLAIMABLE)
#endif

#else

/* global_page_state() only */
#define	nr_file_pages()			global_page_state(NR_FILE_PAGES)
#define	nr_inactive_anon_pages()	global_page_state(NR_INACTIVE_ANON)
#define	nr_inactive_file_pages()	global_page_state(NR_INACTIVE_FILE)
#define	nr_slab_reclaimable_pages()	global_page_state(NR_SLAB_RECLAIMABLE)

#endif /* ZFS_GLOBAL_ZONE_PAGE_STATE */

#endif /* _ZFS_PAGE_COMPAT_H */
