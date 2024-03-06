/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_CHARDEV_H
#define _BCACHEFS_CHARDEV_H

#ifndef NO_BCACHEFS_FS

long bch2_fs_ioctl(struct bch_fs *, unsigned, void __user *);

void bch2_fs_chardev_exit(struct bch_fs *);
int bch2_fs_chardev_init(struct bch_fs *);

void bch2_chardev_exit(void);
int __init bch2_chardev_init(void);

#else

static inline long bch2_fs_ioctl(struct bch_fs *c,
				unsigned cmd, void __user * arg)
{
	return -ENOTTY;
}

static inline void bch2_fs_chardev_exit(struct bch_fs *c) {}
static inline int bch2_fs_chardev_init(struct bch_fs *c) { return 0; }

static inline void bch2_chardev_exit(void) {}
static inline int __init bch2_chardev_init(void) { return 0; }

#endif /* NO_BCACHEFS_FS */

#endif /* _BCACHEFS_CHARDEV_H */
