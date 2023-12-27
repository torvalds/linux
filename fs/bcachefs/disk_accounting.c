// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "buckets.h"
#include "disk_accounting.h"
#include "replicas.h"

static const char * const disk_accounting_type_strs[] = {
#define x(t, n, ...) [n] = #t,
	BCH_DISK_ACCOUNTING_TYPES()
#undef x
	NULL
};

int bch2_accounting_invalid(struct bch_fs *c, struct bkey_s_c k,
			    enum bch_validate_flags flags,
			    struct printbuf *err)
{
	return 0;
}

void bch2_accounting_key_to_text(struct printbuf *out, struct disk_accounting_pos *k)
{
	if (k->type >= BCH_DISK_ACCOUNTING_TYPE_NR) {
		prt_printf(out, "unknown type %u", k->type);
		return;
	}

	prt_str(out, disk_accounting_type_strs[k->type]);
	prt_str(out, " ");

	switch (k->type) {
	case BCH_DISK_ACCOUNTING_nr_inodes:
		break;
	case BCH_DISK_ACCOUNTING_persistent_reserved:
		prt_printf(out, "replicas=%u", k->persistent_reserved.nr_replicas);
		break;
	case BCH_DISK_ACCOUNTING_replicas:
		bch2_replicas_entry_to_text(out, &k->replicas);
		break;
	case BCH_DISK_ACCOUNTING_dev_data_type:
		prt_printf(out, "dev=%u data_type=", k->dev_data_type.dev);
		bch2_prt_data_type(out, k->dev_data_type.data_type);
		break;
	case BCH_DISK_ACCOUNTING_dev_stripe_buckets:
		prt_printf(out, "dev=%u", k->dev_stripe_buckets.dev);
		break;
	}
}

void bch2_accounting_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_accounting acc = bkey_s_c_to_accounting(k);
	struct disk_accounting_pos acc_k;
	bpos_to_disk_accounting_pos(&acc_k, k.k->p);

	bch2_accounting_key_to_text(out, &acc_k);

	for (unsigned i = 0; i < bch2_accounting_counters(k.k); i++)
		prt_printf(out, " %lli", acc.v->d[i]);
}

void bch2_accounting_swab(struct bkey_s k)
{
	for (u64 *p = (u64 *) k.v;
	     p < (u64 *) bkey_val_end(k);
	     p++)
		*p = swab64(*p);
}
