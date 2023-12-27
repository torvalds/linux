/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_ACCOUNTING_H
#define _BCACHEFS_DISK_ACCOUNTING_H

static inline unsigned bch2_accounting_counters(const struct bkey *k)
{
	return bkey_val_u64s(k) - offsetof(struct bch_accounting, d) / sizeof(u64);
}

static inline void bch2_accounting_accumulate(struct bkey_i_accounting *dst,
					      struct bkey_s_c_accounting src)
{
	EBUG_ON(dst->k.u64s != src.k->u64s);

	for (unsigned i = 0; i < bch2_accounting_counters(&dst->k); i++)
		dst->v.d[i] += src.v->d[i];
	if (bversion_cmp(dst->k.version, src.k->version) < 0)
		dst->k.version = src.k->version;
}

static inline void bpos_to_disk_accounting_pos(struct disk_accounting_pos *acc, struct bpos p)
{
	acc->_pad = p;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	bch2_bpos_swab(&acc->_pad);
#endif
}

static inline struct bpos disk_accounting_pos_to_bpos(struct disk_accounting_pos *k)
{
	struct bpos ret = k->_pad;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	bch2_bpos_swab(&ret);
#endif
	return ret;
}

int bch2_accounting_invalid(struct bch_fs *, struct bkey_s_c,
			    enum bch_validate_flags, struct printbuf *);
void bch2_accounting_key_to_text(struct printbuf *, struct disk_accounting_pos *);
void bch2_accounting_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
void bch2_accounting_swab(struct bkey_s);

#define bch2_bkey_ops_accounting ((struct bkey_ops) {	\
	.key_invalid	= bch2_accounting_invalid,	\
	.val_to_text	= bch2_accounting_to_text,	\
	.swab		= bch2_accounting_swab,		\
	.min_val_size	= 8,				\
})

#endif /* _BCACHEFS_DISK_ACCOUNTING_H */
