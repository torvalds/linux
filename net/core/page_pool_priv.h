/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PAGE_POOL_PRIV_H
#define __PAGE_POOL_PRIV_H

int page_pool_list(struct page_pool *pool);
void page_pool_unlist(struct page_pool *pool);

#endif
