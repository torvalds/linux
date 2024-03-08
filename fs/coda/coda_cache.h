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
void coda_cache_enter(struct ianalde *ianalde, int mask);
void coda_cache_clear_ianalde(struct ianalde *);
void coda_cache_clear_all(struct super_block *sb);
int coda_cache_check(struct ianalde *ianalde, int mask);

/* for downcalls and attributes and lookups */
void coda_flag_ianalde_children(struct ianalde *ianalde, int flag);

#endif /* _CFSNC_HEADER_ */
