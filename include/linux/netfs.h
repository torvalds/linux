/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Network filesystem support services.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * See:
 *
 *	Documentation/filesystems/netfs_library.rst
 *
 * for a description of the network filesystem interface declared here.
 */

#ifndef _LINUX_NETFS_H
#define _LINUX_NETFS_H

#include <linux/pagemap.h>

/*
 * Overload PG_private_2 to give us PG_fscache - this is used to indicate that
 * a page is currently backed by a local disk cache
 */
#define PageFsCache(page)		PagePrivate2((page))
#define SetPageFsCache(page)		SetPagePrivate2((page))
#define ClearPageFsCache(page)		ClearPagePrivate2((page))
#define TestSetPageFsCache(page)	TestSetPagePrivate2((page))
#define TestClearPageFsCache(page)	TestClearPagePrivate2((page))

/**
 * set_page_fscache - Set PG_fscache on a page and take a ref
 * @page: The page.
 *
 * Set the PG_fscache (PG_private_2) flag on a page and take the reference
 * needed for the VM to handle its lifetime correctly.  This sets the flag and
 * takes the reference unconditionally, so care must be taken not to set the
 * flag again if it's already set.
 */
static inline void set_page_fscache(struct page *page)
{
	set_page_private_2(page);
}

/**
 * end_page_fscache - Clear PG_fscache and release any waiters
 * @page: The page
 *
 * Clear the PG_fscache (PG_private_2) bit on a page and wake up any sleepers
 * waiting for this.  The page ref held for PG_private_2 being set is released.
 *
 * This is, for example, used when a netfs page is being written to a local
 * disk cache, thereby allowing writes to the cache for the same page to be
 * serialised.
 */
static inline void end_page_fscache(struct page *page)
{
	end_page_private_2(page);
}

/**
 * wait_on_page_fscache - Wait for PG_fscache to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_fscache (aka PG_private_2) to be cleared on a page.
 */
static inline void wait_on_page_fscache(struct page *page)
{
	wait_on_page_private_2(page);
}

/**
 * wait_on_page_fscache_killable - Wait for PG_fscache to be cleared on a page
 * @page: The page to wait on
 *
 * Wait for PG_fscache (aka PG_private_2) to be cleared on a page or until a
 * fatal signal is received by the calling task.
 *
 * Return:
 * - 0 if successful.
 * - -EINTR if a fatal signal was encountered.
 */
static inline int wait_on_page_fscache_killable(struct page *page)
{
	return wait_on_page_private_2_killable(page);
}

#endif /* _LINUX_NETFS_H */
