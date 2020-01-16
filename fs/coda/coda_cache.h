/* SPDX-License-Identifier: GPL-2.0 */
/* Coda filesystem -- Linux Minicache
 *
 * Copyright (C) 1989 - 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this software to
 * contribute improvements to the Coda project. Contact Peter Braam
 * <coda@cs.cmu.edu>
 */

#ifndef _CFSNC_HEADER_
#define _CFSNC_HEADER_

/* credential cache */
void coda_cache_enter(struct iyesde *iyesde, int mask);
void coda_cache_clear_iyesde(struct iyesde *);
void coda_cache_clear_all(struct super_block *sb);
int coda_cache_check(struct iyesde *iyesde, int mask);

/* for downcalls and attributes and lookups */
void coda_flag_iyesde_children(struct iyesde *iyesde, int flag);

#endif /* _CFSNC_HEADER_ */
