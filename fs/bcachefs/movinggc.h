/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVINGGC_H
#define _BCACHEFS_MOVINGGC_H

unsigned long bch2_copygc_wait_amount(struct bch_fs *);
void bch2_copygc_wait_to_text(struct printbuf *, struct bch_fs *);

void bch2_copygc_stop(struct bch_fs *);
int bch2_copygc_start(struct bch_fs *);
void bch2_fs_copygc_init(struct bch_fs *);

#endif /* _BCACHEFS_MOVINGGC_H */
