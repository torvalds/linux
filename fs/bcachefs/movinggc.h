/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_MOVINGGC_H
#define _BCACHEFS_MOVINGGC_H

void bch2_copygc_stop(struct bch_dev *);
int bch2_copygc_start(struct bch_fs *, struct bch_dev *);
void bch2_dev_copygc_init(struct bch_dev *);

#endif /* _BCACHEFS_MOVINGGC_H */
