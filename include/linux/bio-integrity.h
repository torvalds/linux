/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BIO_INTEGRITY_H
#define _LINUX_BIO_INTEGRITY_H

#include <linux/bio.h>

enum bip_flags {
	BIP_BLOCK_INTEGRITY	= 1 << 0, /* block layer owns integrity data */
	BIP_MAPPED_INTEGRITY	= 1 << 1, /* ref tag has been remapped */
	BIP_DISK_NOCHECK	= 1 << 2, /* disable disk integrity checking */
	BIP_IP_CHECKSUM		= 1 << 3, /* IP checksum */
	BIP_COPY_USER		= 1 << 4, /* Kernel bounce buffer in use */
	BIP_CHECK_GUARD		= 1 << 5, /* guard check */
	BIP_CHECK_REFTAG	= 1 << 6, /* reftag check */
	BIP_CHECK_APPTAG	= 1 << 7, /* apptag check */
	BIP_P2P_DMA		= 1 << 8, /* using P2P address */
};

struct bio_integrity_payload {
	struct bvec_iter	bip_iter;

	unsigned short		bip_vcnt;	/* # of integrity bio_vecs */
	unsigned short		bip_max_vcnt;	/* integrity bio_vec slots */
	unsigned short		bip_flags;	/* control flags */
	u16			app_tag;	/* application tag value */

	struct bio_vec		*bip_vec;
};

#define BIP_CLONE_FLAGS (BIP_MAPPED_INTEGRITY | BIP_IP_CHECKSUM | \
			 BIP_CHECK_GUARD | BIP_CHECK_REFTAG | BIP_CHECK_APPTAG)

#ifdef CONFIG_BLK_DEV_INTEGRITY

#define bip_for_each_vec(bvl, bip, iter)				\
	for_each_bvec(bvl, (bip)->bip_vec, iter, (bip)->bip_iter)

#define bio_for_each_integrity_vec(_bvl, _bio, _iter)			\
	for_each_bio(_bio)						\
		bip_for_each_vec(_bvl, _bio->bi_integrity, _iter)

static inline struct bio_integrity_payload *bio_integrity(struct bio *bio)
{
	if (bio->bi_opf & REQ_INTEGRITY)
		return bio->bi_integrity;

	return NULL;
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	struct bio_integrity_payload *bip = bio_integrity(bio);

	if (bip)
		return bip->bip_flags & flag;

	return false;
}

static inline sector_t bip_get_seed(struct bio_integrity_payload *bip)
{
	return bip->bip_iter.bi_sector;
}

static inline void bip_set_seed(struct bio_integrity_payload *bip,
				sector_t seed)
{
	bip->bip_iter.bi_sector = seed;
}

void bio_integrity_init(struct bio *bio, struct bio_integrity_payload *bip,
		struct bio_vec *bvecs, unsigned int nr_vecs);
struct bio_integrity_payload *bio_integrity_alloc(struct bio *bio, gfp_t gfp,
		unsigned int nr);
int bio_integrity_add_page(struct bio *bio, struct page *page, unsigned int len,
		unsigned int offset);
int bio_integrity_map_user(struct bio *bio, struct iov_iter *iter);
int bio_integrity_map_iter(struct bio *bio, struct uio_meta *meta);
void bio_integrity_unmap_user(struct bio *bio);
bool bio_integrity_prep(struct bio *bio);
void bio_integrity_advance(struct bio *bio, unsigned int bytes_done);
void bio_integrity_trim(struct bio *bio);
int bio_integrity_clone(struct bio *bio, struct bio *bio_src, gfp_t gfp_mask);

#else /* CONFIG_BLK_DEV_INTEGRITY */

static inline struct bio_integrity_payload *bio_integrity(struct bio *bio)
{
	return NULL;
}

static inline int bio_integrity_map_user(struct bio *bio, struct iov_iter *iter)
{
	return -EINVAL;
}

static inline int bio_integrity_map_iter(struct bio *bio, struct uio_meta *meta)
{
	return -EINVAL;
}

static inline void bio_integrity_unmap_user(struct bio *bio)
{
}

static inline bool bio_integrity_prep(struct bio *bio)
{
	return true;
}

static inline int bio_integrity_clone(struct bio *bio, struct bio *bio_src,
		gfp_t gfp_mask)
{
	return 0;
}

static inline void bio_integrity_advance(struct bio *bio,
		unsigned int bytes_done)
{
}

static inline void bio_integrity_trim(struct bio *bio)
{
}

static inline bool bio_integrity_flagged(struct bio *bio, enum bip_flags flag)
{
	return false;
}

static inline struct bio_integrity_payload *
bio_integrity_alloc(struct bio *bio, gfp_t gfp, unsigned int nr)
{
	return ERR_PTR(-EINVAL);
}

static inline int bio_integrity_add_page(struct bio *bio, struct page *page,
					unsigned int len, unsigned int offset)
{
	return 0;
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */
#endif /* _LINUX_BIO_INTEGRITY_H */
