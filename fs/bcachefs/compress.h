/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_COMPRESS_H
#define _BCACHEFS_COMPRESS_H

#include "extents_types.h"

int bch2_bio_uncompress_inplace(struct bch_fs *, struct bio *,
				struct bch_extent_crc_unpacked *);
int bch2_bio_uncompress(struct bch_fs *, struct bio *, struct bio *,
		       struct bvec_iter, struct bch_extent_crc_unpacked);
unsigned bch2_bio_compress(struct bch_fs *, struct bio *, size_t *,
			   struct bio *, size_t *, unsigned);

int bch2_check_set_has_compressed_data(struct bch_fs *, unsigned);
void bch2_fs_compress_exit(struct bch_fs *);
int bch2_fs_compress_init(struct bch_fs *);

#endif /* _BCACHEFS_COMPRESS_H */
