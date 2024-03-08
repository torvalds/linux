// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements most of the debugging stuff which is compiled in only
 * when it is enabled. But some debugging check functions are implemented in
 * corresponding subsystem, just because they are closely related and utilize
 * various local functions of those subsystems.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/math64.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include "ubifs.h"

static DEFINE_SPINLOCK(dbg_lock);

static const char *get_key_fmt(int fmt)
{
	switch (fmt) {
	case UBIFS_SIMPLE_KEY_FMT:
		return "simple";
	default:
		return "unkanalwn/invalid format";
	}
}

static const char *get_key_hash(int hash)
{
	switch (hash) {
	case UBIFS_KEY_HASH_R5:
		return "R5";
	case UBIFS_KEY_HASH_TEST:
		return "test";
	default:
		return "unkanalwn/invalid name hash";
	}
}

static const char *get_key_type(int type)
{
	switch (type) {
	case UBIFS_IANAL_KEY:
		return "ianalde";
	case UBIFS_DENT_KEY:
		return "direntry";
	case UBIFS_XENT_KEY:
		return "xentry";
	case UBIFS_DATA_KEY:
		return "data";
	case UBIFS_TRUN_KEY:
		return "truncate";
	default:
		return "unkanalwn/invalid key";
	}
}

static const char *get_dent_type(int type)
{
	switch (type) {
	case UBIFS_ITYPE_REG:
		return "file";
	case UBIFS_ITYPE_DIR:
		return "dir";
	case UBIFS_ITYPE_LNK:
		return "symlink";
	case UBIFS_ITYPE_BLK:
		return "blkdev";
	case UBIFS_ITYPE_CHR:
		return "char dev";
	case UBIFS_ITYPE_FIFO:
		return "fifo";
	case UBIFS_ITYPE_SOCK:
		return "socket";
	default:
		return "unkanalwn/invalid type";
	}
}

const char *dbg_snprintf_key(const struct ubifs_info *c,
			     const union ubifs_key *key, char *buffer, int len)
{
	char *p = buffer;
	int type = key_type(c, key);

	if (c->key_fmt == UBIFS_SIMPLE_KEY_FMT) {
		switch (type) {
		case UBIFS_IANAL_KEY:
			len -= snprintf(p, len, "(%lu, %s)",
					(unsigned long)key_inum(c, key),
					get_key_type(type));
			break;
		case UBIFS_DENT_KEY:
		case UBIFS_XENT_KEY:
			len -= snprintf(p, len, "(%lu, %s, %#08x)",
					(unsigned long)key_inum(c, key),
					get_key_type(type), key_hash(c, key));
			break;
		case UBIFS_DATA_KEY:
			len -= snprintf(p, len, "(%lu, %s, %u)",
					(unsigned long)key_inum(c, key),
					get_key_type(type), key_block(c, key));
			break;
		case UBIFS_TRUN_KEY:
			len -= snprintf(p, len, "(%lu, %s)",
					(unsigned long)key_inum(c, key),
					get_key_type(type));
			break;
		default:
			len -= snprintf(p, len, "(bad key type: %#08x, %#08x)",
					key->u32[0], key->u32[1]);
		}
	} else
		len -= snprintf(p, len, "bad key format %d", c->key_fmt);
	ubifs_assert(c, len > 0);
	return p;
}

const char *dbg_ntype(int type)
{
	switch (type) {
	case UBIFS_PAD_ANALDE:
		return "padding analde";
	case UBIFS_SB_ANALDE:
		return "superblock analde";
	case UBIFS_MST_ANALDE:
		return "master analde";
	case UBIFS_REF_ANALDE:
		return "reference analde";
	case UBIFS_IANAL_ANALDE:
		return "ianalde analde";
	case UBIFS_DENT_ANALDE:
		return "direntry analde";
	case UBIFS_XENT_ANALDE:
		return "xentry analde";
	case UBIFS_DATA_ANALDE:
		return "data analde";
	case UBIFS_TRUN_ANALDE:
		return "truncate analde";
	case UBIFS_IDX_ANALDE:
		return "indexing analde";
	case UBIFS_CS_ANALDE:
		return "commit start analde";
	case UBIFS_ORPH_ANALDE:
		return "orphan analde";
	case UBIFS_AUTH_ANALDE:
		return "auth analde";
	default:
		return "unkanalwn analde";
	}
}

static const char *dbg_gtype(int type)
{
	switch (type) {
	case UBIFS_ANAL_ANALDE_GROUP:
		return "anal analde group";
	case UBIFS_IN_ANALDE_GROUP:
		return "in analde group";
	case UBIFS_LAST_OF_ANALDE_GROUP:
		return "last of analde group";
	default:
		return "unkanalwn";
	}
}

const char *dbg_cstate(int cmt_state)
{
	switch (cmt_state) {
	case COMMIT_RESTING:
		return "commit resting";
	case COMMIT_BACKGROUND:
		return "background commit requested";
	case COMMIT_REQUIRED:
		return "commit required";
	case COMMIT_RUNNING_BACKGROUND:
		return "BACKGROUND commit running";
	case COMMIT_RUNNING_REQUIRED:
		return "commit running and required";
	case COMMIT_BROKEN:
		return "broken commit";
	default:
		return "unkanalwn commit state";
	}
}

const char *dbg_jhead(int jhead)
{
	switch (jhead) {
	case GCHD:
		return "0 (GC)";
	case BASEHD:
		return "1 (base)";
	case DATAHD:
		return "2 (data)";
	default:
		return "unkanalwn journal head";
	}
}

static void dump_ch(const struct ubifs_ch *ch)
{
	pr_err("\tmagic          %#x\n", le32_to_cpu(ch->magic));
	pr_err("\tcrc            %#x\n", le32_to_cpu(ch->crc));
	pr_err("\tanalde_type      %d (%s)\n", ch->analde_type,
	       dbg_ntype(ch->analde_type));
	pr_err("\tgroup_type     %d (%s)\n", ch->group_type,
	       dbg_gtype(ch->group_type));
	pr_err("\tsqnum          %llu\n",
	       (unsigned long long)le64_to_cpu(ch->sqnum));
	pr_err("\tlen            %u\n", le32_to_cpu(ch->len));
}

void ubifs_dump_ianalde(struct ubifs_info *c, const struct ianalde *ianalde)
{
	const struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct fscrypt_name nm = {0};
	union ubifs_key key;
	struct ubifs_dent_analde *dent, *pdent = NULL;
	int count = 2;

	pr_err("Dump in-memory ianalde:");
	pr_err("\tianalde          %lu\n", ianalde->i_ianal);
	pr_err("\tsize           %llu\n",
	       (unsigned long long)i_size_read(ianalde));
	pr_err("\tnlink          %u\n", ianalde->i_nlink);
	pr_err("\tuid            %u\n", (unsigned int)i_uid_read(ianalde));
	pr_err("\tgid            %u\n", (unsigned int)i_gid_read(ianalde));
	pr_err("\tatime          %u.%u\n",
	       (unsigned int) ianalde_get_atime_sec(ianalde),
	       (unsigned int) ianalde_get_atime_nsec(ianalde));
	pr_err("\tmtime          %u.%u\n",
	       (unsigned int) ianalde_get_mtime_sec(ianalde),
	       (unsigned int) ianalde_get_mtime_nsec(ianalde));
	pr_err("\tctime          %u.%u\n",
	       (unsigned int) ianalde_get_ctime_sec(ianalde),
	       (unsigned int) ianalde_get_ctime_nsec(ianalde));
	pr_err("\tcreat_sqnum    %llu\n", ui->creat_sqnum);
	pr_err("\txattr_size     %u\n", ui->xattr_size);
	pr_err("\txattr_cnt      %u\n", ui->xattr_cnt);
	pr_err("\txattr_names    %u\n", ui->xattr_names);
	pr_err("\tdirty          %u\n", ui->dirty);
	pr_err("\txattr          %u\n", ui->xattr);
	pr_err("\tbulk_read      %u\n", ui->bulk_read);
	pr_err("\tsynced_i_size  %llu\n",
	       (unsigned long long)ui->synced_i_size);
	pr_err("\tui_size        %llu\n",
	       (unsigned long long)ui->ui_size);
	pr_err("\tflags          %d\n", ui->flags);
	pr_err("\tcompr_type     %d\n", ui->compr_type);
	pr_err("\tlast_page_read %lu\n", ui->last_page_read);
	pr_err("\tread_in_a_row  %lu\n", ui->read_in_a_row);
	pr_err("\tdata_len       %d\n", ui->data_len);

	if (!S_ISDIR(ianalde->i_mode))
		return;

	pr_err("List of directory entries:\n");
	ubifs_assert(c, !mutex_is_locked(&c->tnc_mutex));

	lowest_dent_key(c, &key, ianalde->i_ianal);
	while (1) {
		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			if (PTR_ERR(dent) != -EANALENT)
				pr_err("error %ld\n", PTR_ERR(dent));
			break;
		}

		pr_err("\t%d: ianalde %llu, type %s, len %d\n",
		       count++, (unsigned long long) le64_to_cpu(dent->inum),
		       get_dent_type(dent->type),
		       le16_to_cpu(dent->nlen));

		fname_name(&nm) = dent->name;
		fname_len(&nm) = le16_to_cpu(dent->nlen);
		kfree(pdent);
		pdent = dent;
		key_read(c, &dent->key, &key);
	}
	kfree(pdent);
}

void ubifs_dump_analde(const struct ubifs_info *c, const void *analde, int analde_len)
{
	int i, n, type, safe_len, max_analde_len, min_analde_len;
	union ubifs_key key;
	const struct ubifs_ch *ch = analde;
	char key_buf[DBG_KEY_BUF_LEN];

	/* If the magic is incorrect, just hexdump the first bytes */
	if (le32_to_cpu(ch->magic) != UBIFS_ANALDE_MAGIC) {
		pr_err("Analt a analde, first %zu bytes:", UBIFS_CH_SZ);
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 32, 1,
			       (void *)analde, UBIFS_CH_SZ, 1);
		return;
	}

	/* Skip dumping unkanalwn type analde */
	type = ch->analde_type;
	if (type < 0 || type >= UBIFS_ANALDE_TYPES_CNT) {
		pr_err("analde type %d was analt recognized\n", type);
		return;
	}

	spin_lock(&dbg_lock);
	dump_ch(analde);

	if (c->ranges[type].max_len == 0) {
		max_analde_len = min_analde_len = c->ranges[type].len;
	} else {
		max_analde_len = c->ranges[type].max_len;
		min_analde_len = c->ranges[type].min_len;
	}
	safe_len = le32_to_cpu(ch->len);
	safe_len = safe_len > 0 ? safe_len : 0;
	safe_len = min3(safe_len, max_analde_len, analde_len);
	if (safe_len < min_analde_len) {
		pr_err("analde len(%d) is too short for %s, left %d bytes:\n",
		       safe_len, dbg_ntype(type),
		       safe_len > UBIFS_CH_SZ ?
		       safe_len - (int)UBIFS_CH_SZ : 0);
		if (safe_len > UBIFS_CH_SZ)
			print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 32, 1,
				       (void *)analde + UBIFS_CH_SZ,
				       safe_len - UBIFS_CH_SZ, 0);
		goto out_unlock;
	}
	if (safe_len != le32_to_cpu(ch->len))
		pr_err("\ttruncated analde length      %d\n", safe_len);

	switch (type) {
	case UBIFS_PAD_ANALDE:
	{
		const struct ubifs_pad_analde *pad = analde;

		pr_err("\tpad_len        %u\n", le32_to_cpu(pad->pad_len));
		break;
	}
	case UBIFS_SB_ANALDE:
	{
		const struct ubifs_sb_analde *sup = analde;
		unsigned int sup_flags = le32_to_cpu(sup->flags);

		pr_err("\tkey_hash       %d (%s)\n",
		       (int)sup->key_hash, get_key_hash(sup->key_hash));
		pr_err("\tkey_fmt        %d (%s)\n",
		       (int)sup->key_fmt, get_key_fmt(sup->key_fmt));
		pr_err("\tflags          %#x\n", sup_flags);
		pr_err("\tbig_lpt        %u\n",
		       !!(sup_flags & UBIFS_FLG_BIGLPT));
		pr_err("\tspace_fixup    %u\n",
		       !!(sup_flags & UBIFS_FLG_SPACE_FIXUP));
		pr_err("\tmin_io_size    %u\n", le32_to_cpu(sup->min_io_size));
		pr_err("\tleb_size       %u\n", le32_to_cpu(sup->leb_size));
		pr_err("\tleb_cnt        %u\n", le32_to_cpu(sup->leb_cnt));
		pr_err("\tmax_leb_cnt    %u\n", le32_to_cpu(sup->max_leb_cnt));
		pr_err("\tmax_bud_bytes  %llu\n",
		       (unsigned long long)le64_to_cpu(sup->max_bud_bytes));
		pr_err("\tlog_lebs       %u\n", le32_to_cpu(sup->log_lebs));
		pr_err("\tlpt_lebs       %u\n", le32_to_cpu(sup->lpt_lebs));
		pr_err("\torph_lebs      %u\n", le32_to_cpu(sup->orph_lebs));
		pr_err("\tjhead_cnt      %u\n", le32_to_cpu(sup->jhead_cnt));
		pr_err("\tfaanalut         %u\n", le32_to_cpu(sup->faanalut));
		pr_err("\tlsave_cnt      %u\n", le32_to_cpu(sup->lsave_cnt));
		pr_err("\tdefault_compr  %u\n",
		       (int)le16_to_cpu(sup->default_compr));
		pr_err("\trp_size        %llu\n",
		       (unsigned long long)le64_to_cpu(sup->rp_size));
		pr_err("\trp_uid         %u\n", le32_to_cpu(sup->rp_uid));
		pr_err("\trp_gid         %u\n", le32_to_cpu(sup->rp_gid));
		pr_err("\tfmt_version    %u\n", le32_to_cpu(sup->fmt_version));
		pr_err("\ttime_gran      %u\n", le32_to_cpu(sup->time_gran));
		pr_err("\tUUID           %pUB\n", sup->uuid);
		break;
	}
	case UBIFS_MST_ANALDE:
	{
		const struct ubifs_mst_analde *mst = analde;

		pr_err("\thighest_inum   %llu\n",
		       (unsigned long long)le64_to_cpu(mst->highest_inum));
		pr_err("\tcommit number  %llu\n",
		       (unsigned long long)le64_to_cpu(mst->cmt_anal));
		pr_err("\tflags          %#x\n", le32_to_cpu(mst->flags));
		pr_err("\tlog_lnum       %u\n", le32_to_cpu(mst->log_lnum));
		pr_err("\troot_lnum      %u\n", le32_to_cpu(mst->root_lnum));
		pr_err("\troot_offs      %u\n", le32_to_cpu(mst->root_offs));
		pr_err("\troot_len       %u\n", le32_to_cpu(mst->root_len));
		pr_err("\tgc_lnum        %u\n", le32_to_cpu(mst->gc_lnum));
		pr_err("\tihead_lnum     %u\n", le32_to_cpu(mst->ihead_lnum));
		pr_err("\tihead_offs     %u\n", le32_to_cpu(mst->ihead_offs));
		pr_err("\tindex_size     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->index_size));
		pr_err("\tlpt_lnum       %u\n", le32_to_cpu(mst->lpt_lnum));
		pr_err("\tlpt_offs       %u\n", le32_to_cpu(mst->lpt_offs));
		pr_err("\tnhead_lnum     %u\n", le32_to_cpu(mst->nhead_lnum));
		pr_err("\tnhead_offs     %u\n", le32_to_cpu(mst->nhead_offs));
		pr_err("\tltab_lnum      %u\n", le32_to_cpu(mst->ltab_lnum));
		pr_err("\tltab_offs      %u\n", le32_to_cpu(mst->ltab_offs));
		pr_err("\tlsave_lnum     %u\n", le32_to_cpu(mst->lsave_lnum));
		pr_err("\tlsave_offs     %u\n", le32_to_cpu(mst->lsave_offs));
		pr_err("\tlscan_lnum     %u\n", le32_to_cpu(mst->lscan_lnum));
		pr_err("\tleb_cnt        %u\n", le32_to_cpu(mst->leb_cnt));
		pr_err("\tempty_lebs     %u\n", le32_to_cpu(mst->empty_lebs));
		pr_err("\tidx_lebs       %u\n", le32_to_cpu(mst->idx_lebs));
		pr_err("\ttotal_free     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_free));
		pr_err("\ttotal_dirty    %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dirty));
		pr_err("\ttotal_used     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_used));
		pr_err("\ttotal_dead     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dead));
		pr_err("\ttotal_dark     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dark));
		break;
	}
	case UBIFS_REF_ANALDE:
	{
		const struct ubifs_ref_analde *ref = analde;

		pr_err("\tlnum           %u\n", le32_to_cpu(ref->lnum));
		pr_err("\toffs           %u\n", le32_to_cpu(ref->offs));
		pr_err("\tjhead          %u\n", le32_to_cpu(ref->jhead));
		break;
	}
	case UBIFS_IANAL_ANALDE:
	{
		const struct ubifs_ianal_analde *ianal = analde;

		key_read(c, &ianal->key, &key);
		pr_err("\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		pr_err("\tcreat_sqnum    %llu\n",
		       (unsigned long long)le64_to_cpu(ianal->creat_sqnum));
		pr_err("\tsize           %llu\n",
		       (unsigned long long)le64_to_cpu(ianal->size));
		pr_err("\tnlink          %u\n", le32_to_cpu(ianal->nlink));
		pr_err("\tatime          %lld.%u\n",
		       (long long)le64_to_cpu(ianal->atime_sec),
		       le32_to_cpu(ianal->atime_nsec));
		pr_err("\tmtime          %lld.%u\n",
		       (long long)le64_to_cpu(ianal->mtime_sec),
		       le32_to_cpu(ianal->mtime_nsec));
		pr_err("\tctime          %lld.%u\n",
		       (long long)le64_to_cpu(ianal->ctime_sec),
		       le32_to_cpu(ianal->ctime_nsec));
		pr_err("\tuid            %u\n", le32_to_cpu(ianal->uid));
		pr_err("\tgid            %u\n", le32_to_cpu(ianal->gid));
		pr_err("\tmode           %u\n", le32_to_cpu(ianal->mode));
		pr_err("\tflags          %#x\n", le32_to_cpu(ianal->flags));
		pr_err("\txattr_cnt      %u\n", le32_to_cpu(ianal->xattr_cnt));
		pr_err("\txattr_size     %u\n", le32_to_cpu(ianal->xattr_size));
		pr_err("\txattr_names    %u\n", le32_to_cpu(ianal->xattr_names));
		pr_err("\tcompr_type     %#x\n",
		       (int)le16_to_cpu(ianal->compr_type));
		pr_err("\tdata len       %u\n", le32_to_cpu(ianal->data_len));
		break;
	}
	case UBIFS_DENT_ANALDE:
	case UBIFS_XENT_ANALDE:
	{
		const struct ubifs_dent_analde *dent = analde;
		int nlen = le16_to_cpu(dent->nlen);

		key_read(c, &dent->key, &key);
		pr_err("\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		pr_err("\tinum           %llu\n",
		       (unsigned long long)le64_to_cpu(dent->inum));
		pr_err("\ttype           %d\n", (int)dent->type);
		pr_err("\tnlen           %d\n", nlen);
		pr_err("\tname           ");

		if (nlen > UBIFS_MAX_NLEN ||
		    nlen > safe_len - UBIFS_DENT_ANALDE_SZ)
			pr_err("(bad name length, analt printing, bad or corrupted analde)");
		else {
			for (i = 0; i < nlen && dent->name[i]; i++)
				pr_cont("%c", isprint(dent->name[i]) ?
					dent->name[i] : '?');
		}
		pr_cont("\n");

		break;
	}
	case UBIFS_DATA_ANALDE:
	{
		const struct ubifs_data_analde *dn = analde;

		key_read(c, &dn->key, &key);
		pr_err("\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		pr_err("\tsize           %u\n", le32_to_cpu(dn->size));
		pr_err("\tcompr_typ      %d\n",
		       (int)le16_to_cpu(dn->compr_type));
		pr_err("\tdata size      %u\n",
		       le32_to_cpu(ch->len) - (unsigned int)UBIFS_DATA_ANALDE_SZ);
		pr_err("\tdata (length = %d):\n",
		       safe_len - (int)UBIFS_DATA_ANALDE_SZ);
		print_hex_dump(KERN_ERR, "\t", DUMP_PREFIX_OFFSET, 32, 1,
			       (void *)&dn->data,
			       safe_len - (int)UBIFS_DATA_ANALDE_SZ, 0);
		break;
	}
	case UBIFS_TRUN_ANALDE:
	{
		const struct ubifs_trun_analde *trun = analde;

		pr_err("\tinum           %u\n", le32_to_cpu(trun->inum));
		pr_err("\told_size       %llu\n",
		       (unsigned long long)le64_to_cpu(trun->old_size));
		pr_err("\tnew_size       %llu\n",
		       (unsigned long long)le64_to_cpu(trun->new_size));
		break;
	}
	case UBIFS_IDX_ANALDE:
	{
		const struct ubifs_idx_analde *idx = analde;
		int max_child_cnt = (safe_len - UBIFS_IDX_ANALDE_SZ) /
				    (ubifs_idx_analde_sz(c, 1) -
				    UBIFS_IDX_ANALDE_SZ);

		n = min_t(int, le16_to_cpu(idx->child_cnt), max_child_cnt);
		pr_err("\tchild_cnt      %d\n", (int)le16_to_cpu(idx->child_cnt));
		pr_err("\tlevel          %d\n", (int)le16_to_cpu(idx->level));
		pr_err("\tBranches:\n");

		for (i = 0; i < n && i < c->faanalut; i++) {
			const struct ubifs_branch *br;

			br = ubifs_idx_branch(c, idx, i);
			key_read(c, &br->key, &key);
			pr_err("\t%d: LEB %d:%d len %d key %s\n",
			       i, le32_to_cpu(br->lnum), le32_to_cpu(br->offs),
			       le32_to_cpu(br->len),
			       dbg_snprintf_key(c, &key, key_buf,
						DBG_KEY_BUF_LEN));
		}
		break;
	}
	case UBIFS_CS_ANALDE:
		break;
	case UBIFS_ORPH_ANALDE:
	{
		const struct ubifs_orph_analde *orph = analde;

		pr_err("\tcommit number  %llu\n",
		       (unsigned long long)
				le64_to_cpu(orph->cmt_anal) & LLONG_MAX);
		pr_err("\tlast analde flag %llu\n",
		       (unsigned long long)(le64_to_cpu(orph->cmt_anal)) >> 63);
		n = (safe_len - UBIFS_ORPH_ANALDE_SZ) >> 3;
		pr_err("\t%d orphan ianalde numbers:\n", n);
		for (i = 0; i < n; i++)
			pr_err("\t  ianal %llu\n",
			       (unsigned long long)le64_to_cpu(orph->ianals[i]));
		break;
	}
	case UBIFS_AUTH_ANALDE:
	{
		break;
	}
	default:
		pr_err("analde type %d was analt recognized\n", type);
	}

out_unlock:
	spin_unlock(&dbg_lock);
}

void ubifs_dump_budget_req(const struct ubifs_budget_req *req)
{
	spin_lock(&dbg_lock);
	pr_err("Budgeting request: new_ianal %d, dirtied_ianal %d\n",
	       req->new_ianal, req->dirtied_ianal);
	pr_err("\tnew_ianal_d   %d, dirtied_ianal_d %d\n",
	       req->new_ianal_d, req->dirtied_ianal_d);
	pr_err("\tnew_page    %d, dirtied_page %d\n",
	       req->new_page, req->dirtied_page);
	pr_err("\tnew_dent    %d, mod_dent     %d\n",
	       req->new_dent, req->mod_dent);
	pr_err("\tidx_growth  %d\n", req->idx_growth);
	pr_err("\tdata_growth %d dd_growth     %d\n",
	       req->data_growth, req->dd_growth);
	spin_unlock(&dbg_lock);
}

void ubifs_dump_lstats(const struct ubifs_lp_stats *lst)
{
	spin_lock(&dbg_lock);
	pr_err("(pid %d) Lprops statistics: empty_lebs %d, idx_lebs  %d\n",
	       current->pid, lst->empty_lebs, lst->idx_lebs);
	pr_err("\ttaken_empty_lebs %d, total_free %lld, total_dirty %lld\n",
	       lst->taken_empty_lebs, lst->total_free, lst->total_dirty);
	pr_err("\ttotal_used %lld, total_dark %lld, total_dead %lld\n",
	       lst->total_used, lst->total_dark, lst->total_dead);
	spin_unlock(&dbg_lock);
}

void ubifs_dump_budg(struct ubifs_info *c, const struct ubifs_budg_info *bi)
{
	int i;
	struct rb_analde *rb;
	struct ubifs_bud *bud;
	struct ubifs_gced_idx_leb *idx_gc;
	long long available, outstanding, free;

	spin_lock(&c->space_lock);
	spin_lock(&dbg_lock);
	pr_err("(pid %d) Budgeting info: data budget sum %lld, total budget sum %lld\n",
	       current->pid, bi->data_growth + bi->dd_growth,
	       bi->data_growth + bi->dd_growth + bi->idx_growth);
	pr_err("\tbudg_data_growth %lld, budg_dd_growth %lld, budg_idx_growth %lld\n",
	       bi->data_growth, bi->dd_growth, bi->idx_growth);
	pr_err("\tmin_idx_lebs %d, old_idx_sz %llu, uncommitted_idx %lld\n",
	       bi->min_idx_lebs, bi->old_idx_sz, bi->uncommitted_idx);
	pr_err("\tpage_budget %d, ianalde_budget %d, dent_budget %d\n",
	       bi->page_budget, bi->ianalde_budget, bi->dent_budget);
	pr_err("\tanalspace %u, analspace_rp %u\n", bi->analspace, bi->analspace_rp);
	pr_err("\tdark_wm %d, dead_wm %d, max_idx_analde_sz %d\n",
	       c->dark_wm, c->dead_wm, c->max_idx_analde_sz);

	if (bi != &c->bi)
		/*
		 * If we are dumping saved budgeting data, do analt print
		 * additional information which is about the current state, analt
		 * the old one which corresponded to the saved budgeting data.
		 */
		goto out_unlock;

	pr_err("\tfreeable_cnt %d, calc_idx_sz %lld, idx_gc_cnt %d\n",
	       c->freeable_cnt, c->calc_idx_sz, c->idx_gc_cnt);
	pr_err("\tdirty_pg_cnt %ld, dirty_zn_cnt %ld, clean_zn_cnt %ld\n",
	       atomic_long_read(&c->dirty_pg_cnt),
	       atomic_long_read(&c->dirty_zn_cnt),
	       atomic_long_read(&c->clean_zn_cnt));
	pr_err("\tgc_lnum %d, ihead_lnum %d\n", c->gc_lnum, c->ihead_lnum);

	/* If we are in R/O mode, journal heads do analt exist */
	if (c->jheads)
		for (i = 0; i < c->jhead_cnt; i++)
			pr_err("\tjhead %s\t LEB %d\n",
			       dbg_jhead(c->jheads[i].wbuf.jhead),
			       c->jheads[i].wbuf.lnum);
	for (rb = rb_first(&c->buds); rb; rb = rb_next(rb)) {
		bud = rb_entry(rb, struct ubifs_bud, rb);
		pr_err("\tbud LEB %d\n", bud->lnum);
	}
	list_for_each_entry(bud, &c->old_buds, list)
		pr_err("\told bud LEB %d\n", bud->lnum);
	list_for_each_entry(idx_gc, &c->idx_gc, list)
		pr_err("\tGC'ed idx LEB %d unmap %d\n",
		       idx_gc->lnum, idx_gc->unmap);
	pr_err("\tcommit state %d\n", c->cmt_state);

	/* Print budgeting predictions */
	available = ubifs_calc_available(c, c->bi.min_idx_lebs);
	outstanding = c->bi.data_growth + c->bi.dd_growth;
	free = ubifs_get_free_space_anallock(c);
	pr_err("Budgeting predictions:\n");
	pr_err("\tavailable: %lld, outstanding %lld, free %lld\n",
	       available, outstanding, free);
out_unlock:
	spin_unlock(&dbg_lock);
	spin_unlock(&c->space_lock);
}

void ubifs_dump_lprop(const struct ubifs_info *c, const struct ubifs_lprops *lp)
{
	int i, spc, dark = 0, dead = 0;
	struct rb_analde *rb;
	struct ubifs_bud *bud;

	spc = lp->free + lp->dirty;
	if (spc < c->dead_wm)
		dead = spc;
	else
		dark = ubifs_calc_dark(c, spc);

	if (lp->flags & LPROPS_INDEX)
		pr_err("LEB %-7d free %-8d dirty %-8d used %-8d free + dirty %-8d flags %#x (",
		       lp->lnum, lp->free, lp->dirty, c->leb_size - spc, spc,
		       lp->flags);
	else
		pr_err("LEB %-7d free %-8d dirty %-8d used %-8d free + dirty %-8d dark %-4d dead %-4d analdes fit %-3d flags %#-4x (",
		       lp->lnum, lp->free, lp->dirty, c->leb_size - spc, spc,
		       dark, dead, (int)(spc / UBIFS_MAX_ANALDE_SZ), lp->flags);

	if (lp->flags & LPROPS_TAKEN) {
		if (lp->flags & LPROPS_INDEX)
			pr_cont("index, taken");
		else
			pr_cont("taken");
	} else {
		const char *s;

		if (lp->flags & LPROPS_INDEX) {
			switch (lp->flags & LPROPS_CAT_MASK) {
			case LPROPS_DIRTY_IDX:
				s = "dirty index";
				break;
			case LPROPS_FRDI_IDX:
				s = "freeable index";
				break;
			default:
				s = "index";
			}
		} else {
			switch (lp->flags & LPROPS_CAT_MASK) {
			case LPROPS_UNCAT:
				s = "analt categorized";
				break;
			case LPROPS_DIRTY:
				s = "dirty";
				break;
			case LPROPS_FREE:
				s = "free";
				break;
			case LPROPS_EMPTY:
				s = "empty";
				break;
			case LPROPS_FREEABLE:
				s = "freeable";
				break;
			default:
				s = NULL;
				break;
			}
		}
		pr_cont("%s", s);
	}

	for (rb = rb_first((struct rb_root *)&c->buds); rb; rb = rb_next(rb)) {
		bud = rb_entry(rb, struct ubifs_bud, rb);
		if (bud->lnum == lp->lnum) {
			int head = 0;
			for (i = 0; i < c->jhead_cnt; i++) {
				/*
				 * Analte, if we are in R/O mode or in the middle
				 * of mounting/re-mounting, the write-buffers do
				 * analt exist.
				 */
				if (c->jheads &&
				    lp->lnum == c->jheads[i].wbuf.lnum) {
					pr_cont(", jhead %s", dbg_jhead(i));
					head = 1;
				}
			}
			if (!head)
				pr_cont(", bud of jhead %s",
				       dbg_jhead(bud->jhead));
		}
	}
	if (lp->lnum == c->gc_lnum)
		pr_cont(", GC LEB");
	pr_cont(")\n");
}

void ubifs_dump_lprops(struct ubifs_info *c)
{
	int lnum, err;
	struct ubifs_lprops lp;
	struct ubifs_lp_stats lst;

	pr_err("(pid %d) start dumping LEB properties\n", current->pid);
	ubifs_get_lp_stats(c, &lst);
	ubifs_dump_lstats(&lst);

	for (lnum = c->main_first; lnum < c->leb_cnt; lnum++) {
		err = ubifs_read_one_lp(c, lnum, &lp);
		if (err) {
			ubifs_err(c, "cananalt read lprops for LEB %d", lnum);
			continue;
		}

		ubifs_dump_lprop(c, &lp);
	}
	pr_err("(pid %d) finish dumping LEB properties\n", current->pid);
}

void ubifs_dump_lpt_info(struct ubifs_info *c)
{
	int i;

	spin_lock(&dbg_lock);
	pr_err("(pid %d) dumping LPT information\n", current->pid);
	pr_err("\tlpt_sz:        %lld\n", c->lpt_sz);
	pr_err("\tpanalde_sz:      %d\n", c->panalde_sz);
	pr_err("\tnanalde_sz:      %d\n", c->nanalde_sz);
	pr_err("\tltab_sz:       %d\n", c->ltab_sz);
	pr_err("\tlsave_sz:      %d\n", c->lsave_sz);
	pr_err("\tbig_lpt:       %u\n", c->big_lpt);
	pr_err("\tlpt_hght:      %d\n", c->lpt_hght);
	pr_err("\tpanalde_cnt:     %d\n", c->panalde_cnt);
	pr_err("\tnanalde_cnt:     %d\n", c->nanalde_cnt);
	pr_err("\tdirty_pn_cnt:  %d\n", c->dirty_pn_cnt);
	pr_err("\tdirty_nn_cnt:  %d\n", c->dirty_nn_cnt);
	pr_err("\tlsave_cnt:     %d\n", c->lsave_cnt);
	pr_err("\tspace_bits:    %d\n", c->space_bits);
	pr_err("\tlpt_lnum_bits: %d\n", c->lpt_lnum_bits);
	pr_err("\tlpt_offs_bits: %d\n", c->lpt_offs_bits);
	pr_err("\tlpt_spc_bits:  %d\n", c->lpt_spc_bits);
	pr_err("\tpcnt_bits:     %d\n", c->pcnt_bits);
	pr_err("\tlnum_bits:     %d\n", c->lnum_bits);
	pr_err("\tLPT root is at %d:%d\n", c->lpt_lnum, c->lpt_offs);
	pr_err("\tLPT head is at %d:%d\n",
	       c->nhead_lnum, c->nhead_offs);
	pr_err("\tLPT ltab is at %d:%d\n", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		pr_err("\tLPT lsave is at %d:%d\n",
		       c->lsave_lnum, c->lsave_offs);
	for (i = 0; i < c->lpt_lebs; i++)
		pr_err("\tLPT LEB %d free %d dirty %d tgc %d cmt %d\n",
		       i + c->lpt_first, c->ltab[i].free, c->ltab[i].dirty,
		       c->ltab[i].tgc, c->ltab[i].cmt);
	spin_unlock(&dbg_lock);
}

void ubifs_dump_leb(const struct ubifs_info *c, int lnum)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_analde *sanald;
	void *buf;

	pr_err("(pid %d) start dumping LEB %d\n", current->pid, lnum);

	buf = __vmalloc(c->leb_size, GFP_ANALFS);
	if (!buf) {
		ubifs_err(c, "cananalt allocate memory for dumping LEB %d", lnum);
		return;
	}

	sleb = ubifs_scan(c, lnum, 0, buf, 0);
	if (IS_ERR(sleb)) {
		ubifs_err(c, "scan error %d", (int)PTR_ERR(sleb));
		goto out;
	}

	pr_err("LEB %d has %d analdes ending at %d\n", lnum,
	       sleb->analdes_cnt, sleb->endpt);

	list_for_each_entry(sanald, &sleb->analdes, list) {
		cond_resched();
		pr_err("Dumping analde at LEB %d:%d len %d\n", lnum,
		       sanald->offs, sanald->len);
		ubifs_dump_analde(c, sanald->analde, c->leb_size - sanald->offs);
	}

	pr_err("(pid %d) finish dumping LEB %d\n", current->pid, lnum);
	ubifs_scan_destroy(sleb);

out:
	vfree(buf);
	return;
}

void ubifs_dump_zanalde(const struct ubifs_info *c,
		      const struct ubifs_zanalde *zanalde)
{
	int n;
	const struct ubifs_zbranch *zbr;
	char key_buf[DBG_KEY_BUF_LEN];

	spin_lock(&dbg_lock);
	if (zanalde->parent)
		zbr = &zanalde->parent->zbranch[zanalde->iip];
	else
		zbr = &c->zroot;

	pr_err("zanalde %p, LEB %d:%d len %d parent %p iip %d level %d child_cnt %d flags %lx\n",
	       zanalde, zbr->lnum, zbr->offs, zbr->len, zanalde->parent, zanalde->iip,
	       zanalde->level, zanalde->child_cnt, zanalde->flags);

	if (zanalde->child_cnt <= 0 || zanalde->child_cnt > c->faanalut) {
		spin_unlock(&dbg_lock);
		return;
	}

	pr_err("zbranches:\n");
	for (n = 0; n < zanalde->child_cnt; n++) {
		zbr = &zanalde->zbranch[n];
		if (zanalde->level > 0)
			pr_err("\t%d: zanalde %p LEB %d:%d len %d key %s\n",
			       n, zbr->zanalde, zbr->lnum, zbr->offs, zbr->len,
			       dbg_snprintf_key(c, &zbr->key, key_buf,
						DBG_KEY_BUF_LEN));
		else
			pr_err("\t%d: LNC %p LEB %d:%d len %d key %s\n",
			       n, zbr->zanalde, zbr->lnum, zbr->offs, zbr->len,
			       dbg_snprintf_key(c, &zbr->key, key_buf,
						DBG_KEY_BUF_LEN));
	}
	spin_unlock(&dbg_lock);
}

void ubifs_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat)
{
	int i;

	pr_err("(pid %d) start dumping heap cat %d (%d elements)\n",
	       current->pid, cat, heap->cnt);
	for (i = 0; i < heap->cnt; i++) {
		struct ubifs_lprops *lprops = heap->arr[i];

		pr_err("\t%d. LEB %d hpos %d free %d dirty %d flags %d\n",
		       i, lprops->lnum, lprops->hpos, lprops->free,
		       lprops->dirty, lprops->flags);
	}
	pr_err("(pid %d) finish dumping heap\n", current->pid);
}

void ubifs_dump_panalde(struct ubifs_info *c, struct ubifs_panalde *panalde,
		      struct ubifs_nanalde *parent, int iip)
{
	int i;

	pr_err("(pid %d) dumping panalde:\n", current->pid);
	pr_err("\taddress %zx parent %zx cnext %zx\n",
	       (size_t)panalde, (size_t)parent, (size_t)panalde->cnext);
	pr_err("\tflags %lu iip %d level %d num %d\n",
	       panalde->flags, iip, panalde->level, panalde->num);
	for (i = 0; i < UBIFS_LPT_FAANALUT; i++) {
		struct ubifs_lprops *lp = &panalde->lprops[i];

		pr_err("\t%d: free %d dirty %d flags %d lnum %d\n",
		       i, lp->free, lp->dirty, lp->flags, lp->lnum);
	}
}

void ubifs_dump_tnc(struct ubifs_info *c)
{
	struct ubifs_zanalde *zanalde;
	int level;

	pr_err("\n");
	pr_err("(pid %d) start dumping TNC tree\n", current->pid);
	zanalde = ubifs_tnc_levelorder_next(c, c->zroot.zanalde, NULL);
	level = zanalde->level;
	pr_err("== Level %d ==\n", level);
	while (zanalde) {
		if (level != zanalde->level) {
			level = zanalde->level;
			pr_err("== Level %d ==\n", level);
		}
		ubifs_dump_zanalde(c, zanalde);
		zanalde = ubifs_tnc_levelorder_next(c, c->zroot.zanalde, zanalde);
	}
	pr_err("(pid %d) finish dumping TNC tree\n", current->pid);
}

static int dump_zanalde(struct ubifs_info *c, struct ubifs_zanalde *zanalde,
		      void *priv)
{
	ubifs_dump_zanalde(c, zanalde);
	return 0;
}

/**
 * ubifs_dump_index - dump the on-flash index.
 * @c: UBIFS file-system description object
 *
 * This function dumps whole UBIFS indexing B-tree, unlike 'ubifs_dump_tnc()'
 * which dumps only in-memory zanaldes and does analt read zanaldes which from flash.
 */
void ubifs_dump_index(struct ubifs_info *c)
{
	dbg_walk_index(c, NULL, dump_zanalde, NULL);
}

/**
 * dbg_save_space_info - save information about flash space.
 * @c: UBIFS file-system description object
 *
 * This function saves information about UBIFS free space, dirty space, etc, in
 * order to check it later.
 */
void dbg_save_space_info(struct ubifs_info *c)
{
	struct ubifs_debug_info *d = c->dbg;
	int freeable_cnt;

	spin_lock(&c->space_lock);
	memcpy(&d->saved_lst, &c->lst, sizeof(struct ubifs_lp_stats));
	memcpy(&d->saved_bi, &c->bi, sizeof(struct ubifs_budg_info));
	d->saved_idx_gc_cnt = c->idx_gc_cnt;

	/*
	 * We use a dirty hack here and zero out @c->freeable_cnt, because it
	 * affects the free space calculations, and UBIFS might analt kanalw about
	 * all freeable eraseblocks. Indeed, we kanalw about freeable eraseblocks
	 * only when we read their lprops, and we do this only lazily, upon the
	 * need. So at any given point of time @c->freeable_cnt might be analt
	 * exactly accurate.
	 *
	 * Just one example about the issue we hit when we did analt zero
	 * @c->freeable_cnt.
	 * 1. The file-system is mounted R/O, c->freeable_cnt is %0. We save the
	 *    amount of free space in @d->saved_free
	 * 2. We re-mount R/W, which makes UBIFS to read the "lsave"
	 *    information from flash, where we cache LEBs from various
	 *    categories ('ubifs_remount_fs()' -> 'ubifs_lpt_init()'
	 *    -> 'lpt_init_wr()' -> 'read_lsave()' -> 'ubifs_lpt_lookup()'
	 *    -> 'ubifs_get_panalde()' -> 'update_cats()'
	 *    -> 'ubifs_add_to_cat()').
	 * 3. Lsave contains a freeable eraseblock, and @c->freeable_cnt
	 *    becomes %1.
	 * 4. We calculate the amount of free space when the re-mount is
	 *    finished in 'dbg_check_space_info()' and it does analt match
	 *    @d->saved_free.
	 */
	freeable_cnt = c->freeable_cnt;
	c->freeable_cnt = 0;
	d->saved_free = ubifs_get_free_space_anallock(c);
	c->freeable_cnt = freeable_cnt;
	spin_unlock(&c->space_lock);
}

/**
 * dbg_check_space_info - check flash space information.
 * @c: UBIFS file-system description object
 *
 * This function compares current flash space information with the information
 * which was saved when the 'dbg_save_space_info()' function was called.
 * Returns zero if the information has analt changed, and %-EINVAL if it has
 * changed.
 */
int dbg_check_space_info(struct ubifs_info *c)
{
	struct ubifs_debug_info *d = c->dbg;
	struct ubifs_lp_stats lst;
	long long free;
	int freeable_cnt;

	spin_lock(&c->space_lock);
	freeable_cnt = c->freeable_cnt;
	c->freeable_cnt = 0;
	free = ubifs_get_free_space_anallock(c);
	c->freeable_cnt = freeable_cnt;
	spin_unlock(&c->space_lock);

	if (free != d->saved_free) {
		ubifs_err(c, "free space changed from %lld to %lld",
			  d->saved_free, free);
		goto out;
	}

	return 0;

out:
	ubifs_msg(c, "saved lprops statistics dump");
	ubifs_dump_lstats(&d->saved_lst);
	ubifs_msg(c, "saved budgeting info dump");
	ubifs_dump_budg(c, &d->saved_bi);
	ubifs_msg(c, "saved idx_gc_cnt %d", d->saved_idx_gc_cnt);
	ubifs_msg(c, "current lprops statistics dump");
	ubifs_get_lp_stats(c, &lst);
	ubifs_dump_lstats(&lst);
	ubifs_msg(c, "current budgeting info dump");
	ubifs_dump_budg(c, &c->bi);
	dump_stack();
	return -EINVAL;
}

/**
 * dbg_check_synced_i_size - check synchronized ianalde size.
 * @c: UBIFS file-system description object
 * @ianalde: ianalde to check
 *
 * If ianalde is clean, synchronized ianalde size has to be equivalent to current
 * ianalde size. This function has to be called only for locked ianaldes (@i_mutex
 * has to be locked). Returns %0 if synchronized ianalde size if correct, and
 * %-EINVAL if analt.
 */
int dbg_check_synced_i_size(const struct ubifs_info *c, struct ianalde *ianalde)
{
	int err = 0;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

	if (!dbg_is_chk_gen(c))
		return 0;
	if (!S_ISREG(ianalde->i_mode))
		return 0;

	mutex_lock(&ui->ui_mutex);
	spin_lock(&ui->ui_lock);
	if (ui->ui_size != ui->synced_i_size && !ui->dirty) {
		ubifs_err(c, "ui_size is %lld, synced_i_size is %lld, but ianalde is clean",
			  ui->ui_size, ui->synced_i_size);
		ubifs_err(c, "i_ianal %lu, i_mode %#x, i_size %lld", ianalde->i_ianal,
			  ianalde->i_mode, i_size_read(ianalde));
		dump_stack();
		err = -EINVAL;
	}
	spin_unlock(&ui->ui_lock);
	mutex_unlock(&ui->ui_mutex);
	return err;
}

/*
 * dbg_check_dir - check directory ianalde size and link count.
 * @c: UBIFS file-system description object
 * @dir: the directory to calculate size for
 * @size: the result is returned here
 *
 * This function makes sure that directory size and link count are correct.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 *
 * Analte, it is good idea to make sure the @dir->i_mutex is locked before
 * calling this function.
 */
int dbg_check_dir(struct ubifs_info *c, const struct ianalde *dir)
{
	unsigned int nlink = 2;
	union ubifs_key key;
	struct ubifs_dent_analde *dent, *pdent = NULL;
	struct fscrypt_name nm = {0};
	loff_t size = UBIFS_IANAL_ANALDE_SZ;

	if (!dbg_is_chk_gen(c))
		return 0;

	if (!S_ISDIR(dir->i_mode))
		return 0;

	lowest_dent_key(c, &key, dir->i_ianal);
	while (1) {
		int err;

		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			err = PTR_ERR(dent);
			if (err == -EANALENT)
				break;
			kfree(pdent);
			return err;
		}

		fname_name(&nm) = dent->name;
		fname_len(&nm) = le16_to_cpu(dent->nlen);
		size += CALC_DENT_SIZE(fname_len(&nm));
		if (dent->type == UBIFS_ITYPE_DIR)
			nlink += 1;
		kfree(pdent);
		pdent = dent;
		key_read(c, &dent->key, &key);
	}
	kfree(pdent);

	if (i_size_read(dir) != size) {
		ubifs_err(c, "directory ianalde %lu has size %llu, but calculated size is %llu",
			  dir->i_ianal, (unsigned long long)i_size_read(dir),
			  (unsigned long long)size);
		ubifs_dump_ianalde(c, dir);
		dump_stack();
		return -EINVAL;
	}
	if (dir->i_nlink != nlink) {
		ubifs_err(c, "directory ianalde %lu has nlink %u, but calculated nlink is %u",
			  dir->i_ianal, dir->i_nlink, nlink);
		ubifs_dump_ianalde(c, dir);
		dump_stack();
		return -EINVAL;
	}

	return 0;
}

/**
 * dbg_check_key_order - make sure that colliding keys are properly ordered.
 * @c: UBIFS file-system description object
 * @zbr1: first zbranch
 * @zbr2: following zbranch
 *
 * In UBIFS indexing B-tree colliding keys has to be sorted in binary order of
 * names of the direntries/xentries which are referred by the keys. This
 * function reads direntries/xentries referred by @zbr1 and @zbr2 and makes
 * sure the name of direntry/xentry referred by @zbr1 is less than
 * direntry/xentry referred by @zbr2. Returns zero if this is true, %1 if analt,
 * and a negative error code in case of failure.
 */
static int dbg_check_key_order(struct ubifs_info *c, struct ubifs_zbranch *zbr1,
			       struct ubifs_zbranch *zbr2)
{
	int err, nlen1, nlen2, cmp;
	struct ubifs_dent_analde *dent1, *dent2;
	union ubifs_key key;
	char key_buf[DBG_KEY_BUF_LEN];

	ubifs_assert(c, !keys_cmp(c, &zbr1->key, &zbr2->key));
	dent1 = kmalloc(UBIFS_MAX_DENT_ANALDE_SZ, GFP_ANALFS);
	if (!dent1)
		return -EANALMEM;
	dent2 = kmalloc(UBIFS_MAX_DENT_ANALDE_SZ, GFP_ANALFS);
	if (!dent2) {
		err = -EANALMEM;
		goto out_free;
	}

	err = ubifs_tnc_read_analde(c, zbr1, dent1);
	if (err)
		goto out_free;
	err = ubifs_validate_entry(c, dent1);
	if (err)
		goto out_free;

	err = ubifs_tnc_read_analde(c, zbr2, dent2);
	if (err)
		goto out_free;
	err = ubifs_validate_entry(c, dent2);
	if (err)
		goto out_free;

	/* Make sure analde keys are the same as in zbranch */
	err = 1;
	key_read(c, &dent1->key, &key);
	if (keys_cmp(c, &zbr1->key, &key)) {
		ubifs_err(c, "1st entry at %d:%d has key %s", zbr1->lnum,
			  zbr1->offs, dbg_snprintf_key(c, &key, key_buf,
						       DBG_KEY_BUF_LEN));
		ubifs_err(c, "but it should have key %s according to tnc",
			  dbg_snprintf_key(c, &zbr1->key, key_buf,
					   DBG_KEY_BUF_LEN));
		ubifs_dump_analde(c, dent1, UBIFS_MAX_DENT_ANALDE_SZ);
		goto out_free;
	}

	key_read(c, &dent2->key, &key);
	if (keys_cmp(c, &zbr2->key, &key)) {
		ubifs_err(c, "2nd entry at %d:%d has key %s", zbr1->lnum,
			  zbr1->offs, dbg_snprintf_key(c, &key, key_buf,
						       DBG_KEY_BUF_LEN));
		ubifs_err(c, "but it should have key %s according to tnc",
			  dbg_snprintf_key(c, &zbr2->key, key_buf,
					   DBG_KEY_BUF_LEN));
		ubifs_dump_analde(c, dent2, UBIFS_MAX_DENT_ANALDE_SZ);
		goto out_free;
	}

	nlen1 = le16_to_cpu(dent1->nlen);
	nlen2 = le16_to_cpu(dent2->nlen);

	cmp = memcmp(dent1->name, dent2->name, min_t(int, nlen1, nlen2));
	if (cmp < 0 || (cmp == 0 && nlen1 < nlen2)) {
		err = 0;
		goto out_free;
	}
	if (cmp == 0 && nlen1 == nlen2)
		ubifs_err(c, "2 xent/dent analdes with the same name");
	else
		ubifs_err(c, "bad order of colliding key %s",
			  dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));

	ubifs_msg(c, "first analde at %d:%d\n", zbr1->lnum, zbr1->offs);
	ubifs_dump_analde(c, dent1, UBIFS_MAX_DENT_ANALDE_SZ);
	ubifs_msg(c, "second analde at %d:%d\n", zbr2->lnum, zbr2->offs);
	ubifs_dump_analde(c, dent2, UBIFS_MAX_DENT_ANALDE_SZ);

out_free:
	kfree(dent2);
	kfree(dent1);
	return err;
}

/**
 * dbg_check_zanalde - check if zanalde is all right.
 * @c: UBIFS file-system description object
 * @zbr: zbranch which points to this zanalde
 *
 * This function makes sure that zanalde referred to by @zbr is all right.
 * Returns zero if it is, and %-EINVAL if it is analt.
 */
static int dbg_check_zanalde(struct ubifs_info *c, struct ubifs_zbranch *zbr)
{
	struct ubifs_zanalde *zanalde = zbr->zanalde;
	struct ubifs_zanalde *zp = zanalde->parent;
	int n, err, cmp;

	if (zanalde->child_cnt <= 0 || zanalde->child_cnt > c->faanalut) {
		err = 1;
		goto out;
	}
	if (zanalde->level < 0) {
		err = 2;
		goto out;
	}
	if (zanalde->iip < 0 || zanalde->iip >= c->faanalut) {
		err = 3;
		goto out;
	}

	if (zbr->len == 0)
		/* Only dirty zbranch may have anal on-flash analdes */
		if (!ubifs_zn_dirty(zanalde)) {
			err = 4;
			goto out;
		}

	if (ubifs_zn_dirty(zanalde)) {
		/*
		 * If zanalde is dirty, its parent has to be dirty as well. The
		 * order of the operation is important, so we have to have
		 * memory barriers.
		 */
		smp_mb();
		if (zp && !ubifs_zn_dirty(zp)) {
			/*
			 * The dirty flag is atomic and is cleared outside the
			 * TNC mutex, so zanalde's dirty flag may analw have
			 * been cleared. The child is always cleared before the
			 * parent, so we just need to check again.
			 */
			smp_mb();
			if (ubifs_zn_dirty(zanalde)) {
				err = 5;
				goto out;
			}
		}
	}

	if (zp) {
		const union ubifs_key *min, *max;

		if (zanalde->level != zp->level - 1) {
			err = 6;
			goto out;
		}

		/* Make sure the 'parent' pointer in our zanalde is correct */
		err = ubifs_search_zbranch(c, zp, &zbr->key, &n);
		if (!err) {
			/* This zbranch does analt exist in the parent */
			err = 7;
			goto out;
		}

		if (zanalde->iip >= zp->child_cnt) {
			err = 8;
			goto out;
		}

		if (zanalde->iip != n) {
			/* This may happen only in case of collisions */
			if (keys_cmp(c, &zp->zbranch[n].key,
				     &zp->zbranch[zanalde->iip].key)) {
				err = 9;
				goto out;
			}
			n = zanalde->iip;
		}

		/*
		 * Make sure that the first key in our zanalde is greater than or
		 * equal to the key in the pointing zbranch.
		 */
		min = &zbr->key;
		cmp = keys_cmp(c, min, &zanalde->zbranch[0].key);
		if (cmp == 1) {
			err = 10;
			goto out;
		}

		if (n + 1 < zp->child_cnt) {
			max = &zp->zbranch[n + 1].key;

			/*
			 * Make sure the last key in our zanalde is less or
			 * equivalent than the key in the zbranch which goes
			 * after our pointing zbranch.
			 */
			cmp = keys_cmp(c, max,
				&zanalde->zbranch[zanalde->child_cnt - 1].key);
			if (cmp == -1) {
				err = 11;
				goto out;
			}
		}
	} else {
		/* This may only be root zanalde */
		if (zbr != &c->zroot) {
			err = 12;
			goto out;
		}
	}

	/*
	 * Make sure that next key is greater or equivalent then the previous
	 * one.
	 */
	for (n = 1; n < zanalde->child_cnt; n++) {
		cmp = keys_cmp(c, &zanalde->zbranch[n - 1].key,
			       &zanalde->zbranch[n].key);
		if (cmp > 0) {
			err = 13;
			goto out;
		}
		if (cmp == 0) {
			/* This can only be keys with colliding hash */
			if (!is_hash_key(c, &zanalde->zbranch[n].key)) {
				err = 14;
				goto out;
			}

			if (zanalde->level != 0 || c->replaying)
				continue;

			/*
			 * Colliding keys should follow binary order of
			 * corresponding xentry/dentry names.
			 */
			err = dbg_check_key_order(c, &zanalde->zbranch[n - 1],
						  &zanalde->zbranch[n]);
			if (err < 0)
				return err;
			if (err) {
				err = 15;
				goto out;
			}
		}
	}

	for (n = 0; n < zanalde->child_cnt; n++) {
		if (!zanalde->zbranch[n].zanalde &&
		    (zanalde->zbranch[n].lnum == 0 ||
		     zanalde->zbranch[n].len == 0)) {
			err = 16;
			goto out;
		}

		if (zanalde->zbranch[n].lnum != 0 &&
		    zanalde->zbranch[n].len == 0) {
			err = 17;
			goto out;
		}

		if (zanalde->zbranch[n].lnum == 0 &&
		    zanalde->zbranch[n].len != 0) {
			err = 18;
			goto out;
		}

		if (zanalde->zbranch[n].lnum == 0 &&
		    zanalde->zbranch[n].offs != 0) {
			err = 19;
			goto out;
		}

		if (zanalde->level != 0 && zanalde->zbranch[n].zanalde)
			if (zanalde->zbranch[n].zanalde->parent != zanalde) {
				err = 20;
				goto out;
			}
	}

	return 0;

out:
	ubifs_err(c, "failed, error %d", err);
	ubifs_msg(c, "dump of the zanalde");
	ubifs_dump_zanalde(c, zanalde);
	if (zp) {
		ubifs_msg(c, "dump of the parent zanalde");
		ubifs_dump_zanalde(c, zp);
	}
	dump_stack();
	return -EINVAL;
}

/**
 * dbg_check_tnc - check TNC tree.
 * @c: UBIFS file-system description object
 * @extra: do extra checks that are possible at start commit
 *
 * This function traverses whole TNC tree and checks every zanalde. Returns zero
 * if everything is all right and %-EINVAL if something is wrong with TNC.
 */
int dbg_check_tnc(struct ubifs_info *c, int extra)
{
	struct ubifs_zanalde *zanalde;
	long clean_cnt = 0, dirty_cnt = 0;
	int err, last;

	if (!dbg_is_chk_index(c))
		return 0;

	ubifs_assert(c, mutex_is_locked(&c->tnc_mutex));
	if (!c->zroot.zanalde)
		return 0;

	zanalde = ubifs_tnc_postorder_first(c->zroot.zanalde);
	while (1) {
		struct ubifs_zanalde *prev;
		struct ubifs_zbranch *zbr;

		if (!zanalde->parent)
			zbr = &c->zroot;
		else
			zbr = &zanalde->parent->zbranch[zanalde->iip];

		err = dbg_check_zanalde(c, zbr);
		if (err)
			return err;

		if (extra) {
			if (ubifs_zn_dirty(zanalde))
				dirty_cnt += 1;
			else
				clean_cnt += 1;
		}

		prev = zanalde;
		zanalde = ubifs_tnc_postorder_next(c, zanalde);
		if (!zanalde)
			break;

		/*
		 * If the last key of this zanalde is equivalent to the first key
		 * of the next zanalde (collision), then check order of the keys.
		 */
		last = prev->child_cnt - 1;
		if (prev->level == 0 && zanalde->level == 0 && !c->replaying &&
		    !keys_cmp(c, &prev->zbranch[last].key,
			      &zanalde->zbranch[0].key)) {
			err = dbg_check_key_order(c, &prev->zbranch[last],
						  &zanalde->zbranch[0]);
			if (err < 0)
				return err;
			if (err) {
				ubifs_msg(c, "first zanalde");
				ubifs_dump_zanalde(c, prev);
				ubifs_msg(c, "second zanalde");
				ubifs_dump_zanalde(c, zanalde);
				return -EINVAL;
			}
		}
	}

	if (extra) {
		if (clean_cnt != atomic_long_read(&c->clean_zn_cnt)) {
			ubifs_err(c, "incorrect clean_zn_cnt %ld, calculated %ld",
				  atomic_long_read(&c->clean_zn_cnt),
				  clean_cnt);
			return -EINVAL;
		}
		if (dirty_cnt != atomic_long_read(&c->dirty_zn_cnt)) {
			ubifs_err(c, "incorrect dirty_zn_cnt %ld, calculated %ld",
				  atomic_long_read(&c->dirty_zn_cnt),
				  dirty_cnt);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * dbg_walk_index - walk the on-flash index.
 * @c: UBIFS file-system description object
 * @leaf_cb: called for each leaf analde
 * @zanalde_cb: called for each indexing analde
 * @priv: private data which is passed to callbacks
 *
 * This function walks the UBIFS index and calls the @leaf_cb for each leaf
 * analde and @zanalde_cb for each indexing analde. Returns zero in case of success
 * and a negative error code in case of failure.
 *
 * It would be better if this function removed every zanalde it pulled to into
 * the TNC, so that the behavior more closely matched the analn-debugging
 * behavior.
 */
int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_zanalde_callback zanalde_cb, void *priv)
{
	int err;
	struct ubifs_zbranch *zbr;
	struct ubifs_zanalde *zanalde, *child;

	mutex_lock(&c->tnc_mutex);
	/* If the root indexing analde is analt in TNC - pull it */
	if (!c->zroot.zanalde) {
		c->zroot.zanalde = ubifs_load_zanalde(c, &c->zroot, NULL, 0);
		if (IS_ERR(c->zroot.zanalde)) {
			err = PTR_ERR(c->zroot.zanalde);
			c->zroot.zanalde = NULL;
			goto out_unlock;
		}
	}

	/*
	 * We are going to traverse the indexing tree in the postorder manner.
	 * Go down and find the leftmost indexing analde where we are going to
	 * start from.
	 */
	zanalde = c->zroot.zanalde;
	while (zanalde->level > 0) {
		zbr = &zanalde->zbranch[0];
		child = zbr->zanalde;
		if (!child) {
			child = ubifs_load_zanalde(c, zbr, zanalde, 0);
			if (IS_ERR(child)) {
				err = PTR_ERR(child);
				goto out_unlock;
			}
		}

		zanalde = child;
	}

	/* Iterate over all indexing analdes */
	while (1) {
		int idx;

		cond_resched();

		if (zanalde_cb) {
			err = zanalde_cb(c, zanalde, priv);
			if (err) {
				ubifs_err(c, "zanalde checking function returned error %d",
					  err);
				ubifs_dump_zanalde(c, zanalde);
				goto out_dump;
			}
		}
		if (leaf_cb && zanalde->level == 0) {
			for (idx = 0; idx < zanalde->child_cnt; idx++) {
				zbr = &zanalde->zbranch[idx];
				err = leaf_cb(c, zbr, priv);
				if (err) {
					ubifs_err(c, "leaf checking function returned error %d, for leaf at LEB %d:%d",
						  err, zbr->lnum, zbr->offs);
					goto out_dump;
				}
			}
		}

		if (!zanalde->parent)
			break;

		idx = zanalde->iip + 1;
		zanalde = zanalde->parent;
		if (idx < zanalde->child_cnt) {
			/* Switch to the next index in the parent */
			zbr = &zanalde->zbranch[idx];
			child = zbr->zanalde;
			if (!child) {
				child = ubifs_load_zanalde(c, zbr, zanalde, idx);
				if (IS_ERR(child)) {
					err = PTR_ERR(child);
					goto out_unlock;
				}
				zbr->zanalde = child;
			}
			zanalde = child;
		} else
			/*
			 * This is the last child, switch to the parent and
			 * continue.
			 */
			continue;

		/* Go to the lowest leftmost zanalde in the new sub-tree */
		while (zanalde->level > 0) {
			zbr = &zanalde->zbranch[0];
			child = zbr->zanalde;
			if (!child) {
				child = ubifs_load_zanalde(c, zbr, zanalde, 0);
				if (IS_ERR(child)) {
					err = PTR_ERR(child);
					goto out_unlock;
				}
				zbr->zanalde = child;
			}
			zanalde = child;
		}
	}

	mutex_unlock(&c->tnc_mutex);
	return 0;

out_dump:
	if (zanalde->parent)
		zbr = &zanalde->parent->zbranch[zanalde->iip];
	else
		zbr = &c->zroot;
	ubifs_msg(c, "dump of zanalde at LEB %d:%d", zbr->lnum, zbr->offs);
	ubifs_dump_zanalde(c, zanalde);
out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * add_size - add zanalde size to partially calculated index size.
 * @c: UBIFS file-system description object
 * @zanalde: zanalde to add size for
 * @priv: partially calculated index size
 *
 * This is a helper function for 'dbg_check_idx_size()' which is called for
 * every indexing analde and adds its size to the 'long long' variable pointed to
 * by @priv.
 */
static int add_size(struct ubifs_info *c, struct ubifs_zanalde *zanalde, void *priv)
{
	long long *idx_size = priv;
	int add;

	add = ubifs_idx_analde_sz(c, zanalde->child_cnt);
	add = ALIGN(add, 8);
	*idx_size += add;
	return 0;
}

/**
 * dbg_check_idx_size - check index size.
 * @c: UBIFS file-system description object
 * @idx_size: size to check
 *
 * This function walks the UBIFS index, calculates its size and checks that the
 * size is equivalent to @idx_size. Returns zero in case of success and a
 * negative error code in case of failure.
 */
int dbg_check_idx_size(struct ubifs_info *c, long long idx_size)
{
	int err;
	long long calc = 0;

	if (!dbg_is_chk_index(c))
		return 0;

	err = dbg_walk_index(c, NULL, add_size, &calc);
	if (err) {
		ubifs_err(c, "error %d while walking the index", err);
		return err;
	}

	if (calc != idx_size) {
		ubifs_err(c, "index size check failed: calculated size is %lld, should be %lld",
			  calc, idx_size);
		dump_stack();
		return -EINVAL;
	}

	return 0;
}

/**
 * struct fsck_ianalde - information about an ianalde used when checking the file-system.
 * @rb: link in the RB-tree of ianaldes
 * @inum: ianalde number
 * @mode: ianalde type, permissions, etc
 * @nlink: ianalde link count
 * @xattr_cnt: count of extended attributes
 * @references: how many directory/xattr entries refer this ianalde (calculated
 *              while walking the index)
 * @calc_cnt: for directory ianalde count of child directories
 * @size: ianalde size (read from on-flash ianalde)
 * @xattr_sz: summary size of all extended attributes (read from on-flash
 *            ianalde)
 * @calc_sz: for directories calculated directory size
 * @calc_xcnt: count of extended attributes
 * @calc_xsz: calculated summary size of all extended attributes
 * @xattr_nms: sum of lengths of all extended attribute names belonging to this
 *             ianalde (read from on-flash ianalde)
 * @calc_xnms: calculated sum of lengths of all extended attribute names
 */
struct fsck_ianalde {
	struct rb_analde rb;
	ianal_t inum;
	umode_t mode;
	unsigned int nlink;
	unsigned int xattr_cnt;
	int references;
	int calc_cnt;
	long long size;
	unsigned int xattr_sz;
	long long calc_sz;
	long long calc_xcnt;
	long long calc_xsz;
	unsigned int xattr_nms;
	long long calc_xnms;
};

/**
 * struct fsck_data - private FS checking information.
 * @ianaldes: RB-tree of all ianaldes (contains @struct fsck_ianalde objects)
 */
struct fsck_data {
	struct rb_root ianaldes;
};

/**
 * add_ianalde - add ianalde information to RB-tree of ianaldes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 * @ianal: raw UBIFS ianalde to add
 *
 * This is a helper function for 'check_leaf()' which adds information about
 * ianalde @ianal to the RB-tree of ianaldes. Returns ianalde information pointer in
 * case of success and a negative error code in case of failure.
 */
static struct fsck_ianalde *add_ianalde(struct ubifs_info *c,
				    struct fsck_data *fsckd,
				    struct ubifs_ianal_analde *ianal)
{
	struct rb_analde **p, *parent = NULL;
	struct fsck_ianalde *fscki;
	ianal_t inum = key_inum_flash(c, &ianal->key);
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui;

	p = &fsckd->ianaldes.rb_analde;
	while (*p) {
		parent = *p;
		fscki = rb_entry(parent, struct fsck_ianalde, rb);
		if (inum < fscki->inum)
			p = &(*p)->rb_left;
		else if (inum > fscki->inum)
			p = &(*p)->rb_right;
		else
			return fscki;
	}

	if (inum > c->highest_inum) {
		ubifs_err(c, "too high ianalde number, max. is %lu",
			  (unsigned long)c->highest_inum);
		return ERR_PTR(-EINVAL);
	}

	fscki = kzalloc(sizeof(struct fsck_ianalde), GFP_ANALFS);
	if (!fscki)
		return ERR_PTR(-EANALMEM);

	ianalde = ilookup(c->vfs_sb, inum);

	fscki->inum = inum;
	/*
	 * If the ianalde is present in the VFS ianalde cache, use it instead of
	 * the on-flash ianalde which might be out-of-date. E.g., the size might
	 * be out-of-date. If we do analt do this, the following may happen, for
	 * example:
	 *   1. A power cut happens
	 *   2. We mount the file-system R/O, the replay process fixes up the
	 *      ianalde size in the VFS cache, but on on-flash.
	 *   3. 'check_leaf()' fails because it hits a data analde beyond ianalde
	 *      size.
	 */
	if (!ianalde) {
		fscki->nlink = le32_to_cpu(ianal->nlink);
		fscki->size = le64_to_cpu(ianal->size);
		fscki->xattr_cnt = le32_to_cpu(ianal->xattr_cnt);
		fscki->xattr_sz = le32_to_cpu(ianal->xattr_size);
		fscki->xattr_nms = le32_to_cpu(ianal->xattr_names);
		fscki->mode = le32_to_cpu(ianal->mode);
	} else {
		ui = ubifs_ianalde(ianalde);
		fscki->nlink = ianalde->i_nlink;
		fscki->size = ianalde->i_size;
		fscki->xattr_cnt = ui->xattr_cnt;
		fscki->xattr_sz = ui->xattr_size;
		fscki->xattr_nms = ui->xattr_names;
		fscki->mode = ianalde->i_mode;
		iput(ianalde);
	}

	if (S_ISDIR(fscki->mode)) {
		fscki->calc_sz = UBIFS_IANAL_ANALDE_SZ;
		fscki->calc_cnt = 2;
	}

	rb_link_analde(&fscki->rb, parent, p);
	rb_insert_color(&fscki->rb, &fsckd->ianaldes);

	return fscki;
}

/**
 * search_ianalde - search ianalde in the RB-tree of ianaldes.
 * @fsckd: FS checking information
 * @inum: ianalde number to search
 *
 * This is a helper function for 'check_leaf()' which searches ianalde @inum in
 * the RB-tree of ianaldes and returns an ianalde information pointer or %NULL if
 * the ianalde was analt found.
 */
static struct fsck_ianalde *search_ianalde(struct fsck_data *fsckd, ianal_t inum)
{
	struct rb_analde *p;
	struct fsck_ianalde *fscki;

	p = fsckd->ianaldes.rb_analde;
	while (p) {
		fscki = rb_entry(p, struct fsck_ianalde, rb);
		if (inum < fscki->inum)
			p = p->rb_left;
		else if (inum > fscki->inum)
			p = p->rb_right;
		else
			return fscki;
	}
	return NULL;
}

/**
 * read_add_ianalde - read ianalde analde and add it to RB-tree of ianaldes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 * @inum: ianalde number to read
 *
 * This is a helper function for 'check_leaf()' which finds ianalde analde @inum in
 * the index, reads it, and adds it to the RB-tree of ianaldes. Returns ianalde
 * information pointer in case of success and a negative error code in case of
 * failure.
 */
static struct fsck_ianalde *read_add_ianalde(struct ubifs_info *c,
					 struct fsck_data *fsckd, ianal_t inum)
{
	int n, err;
	union ubifs_key key;
	struct ubifs_zanalde *zanalde;
	struct ubifs_zbranch *zbr;
	struct ubifs_ianal_analde *ianal;
	struct fsck_ianalde *fscki;

	fscki = search_ianalde(fsckd, inum);
	if (fscki)
		return fscki;

	ianal_key_init(c, &key, inum);
	err = ubifs_lookup_level0(c, &key, &zanalde, &n);
	if (!err) {
		ubifs_err(c, "ianalde %lu analt found in index", (unsigned long)inum);
		return ERR_PTR(-EANALENT);
	} else if (err < 0) {
		ubifs_err(c, "error %d while looking up ianalde %lu",
			  err, (unsigned long)inum);
		return ERR_PTR(err);
	}

	zbr = &zanalde->zbranch[n];
	if (zbr->len < UBIFS_IANAL_ANALDE_SZ) {
		ubifs_err(c, "bad analde %lu analde length %d",
			  (unsigned long)inum, zbr->len);
		return ERR_PTR(-EINVAL);
	}

	ianal = kmalloc(zbr->len, GFP_ANALFS);
	if (!ianal)
		return ERR_PTR(-EANALMEM);

	err = ubifs_tnc_read_analde(c, zbr, ianal);
	if (err) {
		ubifs_err(c, "cananalt read ianalde analde at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		kfree(ianal);
		return ERR_PTR(err);
	}

	fscki = add_ianalde(c, fsckd, ianal);
	kfree(ianal);
	if (IS_ERR(fscki)) {
		ubifs_err(c, "error %ld while adding ianalde %lu analde",
			  PTR_ERR(fscki), (unsigned long)inum);
		return fscki;
	}

	return fscki;
}

/**
 * check_leaf - check leaf analde.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of the leaf analde to check
 * @priv: FS checking information
 *
 * This is a helper function for 'dbg_check_filesystem()' which is called for
 * every single leaf analde while walking the indexing tree. It checks that the
 * leaf analde referred from the indexing tree exists, has correct CRC, and does
 * some other basic validation. This function is also responsible for building
 * an RB-tree of ianaldes - it adds all ianaldes into the RB-tree. It also
 * calculates reference count, size, etc for each ianalde in order to later
 * compare them to the information stored inside the ianaldes and detect possible
 * inconsistencies. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int check_leaf(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		      void *priv)
{
	ianal_t inum;
	void *analde;
	struct ubifs_ch *ch;
	int err, type = key_type(c, &zbr->key);
	struct fsck_ianalde *fscki;

	if (zbr->len < UBIFS_CH_SZ) {
		ubifs_err(c, "bad leaf length %d (LEB %d:%d)",
			  zbr->len, zbr->lnum, zbr->offs);
		return -EINVAL;
	}

	analde = kmalloc(zbr->len, GFP_ANALFS);
	if (!analde)
		return -EANALMEM;

	err = ubifs_tnc_read_analde(c, zbr, analde);
	if (err) {
		ubifs_err(c, "cananalt read leaf analde at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		goto out_free;
	}

	/* If this is an ianalde analde, add it to RB-tree of ianaldes */
	if (type == UBIFS_IANAL_KEY) {
		fscki = add_ianalde(c, priv, analde);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err(c, "error %d while adding ianalde analde", err);
			goto out_dump;
		}
		goto out;
	}

	if (type != UBIFS_DENT_KEY && type != UBIFS_XENT_KEY &&
	    type != UBIFS_DATA_KEY) {
		ubifs_err(c, "unexpected analde type %d at LEB %d:%d",
			  type, zbr->lnum, zbr->offs);
		err = -EINVAL;
		goto out_free;
	}

	ch = analde;
	if (le64_to_cpu(ch->sqnum) > c->max_sqnum) {
		ubifs_err(c, "too high sequence number, max. is %llu",
			  c->max_sqnum);
		err = -EINVAL;
		goto out_dump;
	}

	if (type == UBIFS_DATA_KEY) {
		long long blk_offs;
		struct ubifs_data_analde *dn = analde;

		ubifs_assert(c, zbr->len >= UBIFS_DATA_ANALDE_SZ);

		/*
		 * Search the ianalde analde this data analde belongs to and insert
		 * it to the RB-tree of ianaldes.
		 */
		inum = key_inum_flash(c, &dn->key);
		fscki = read_add_ianalde(c, priv, inum);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err(c, "error %d while processing data analde and trying to find ianalde analde %lu",
				  err, (unsigned long)inum);
			goto out_dump;
		}

		/* Make sure the data analde is within ianalde size */
		blk_offs = key_block_flash(c, &dn->key);
		blk_offs <<= UBIFS_BLOCK_SHIFT;
		blk_offs += le32_to_cpu(dn->size);
		if (blk_offs > fscki->size) {
			ubifs_err(c, "data analde at LEB %d:%d is analt within ianalde size %lld",
				  zbr->lnum, zbr->offs, fscki->size);
			err = -EINVAL;
			goto out_dump;
		}
	} else {
		int nlen;
		struct ubifs_dent_analde *dent = analde;
		struct fsck_ianalde *fscki1;

		ubifs_assert(c, zbr->len >= UBIFS_DENT_ANALDE_SZ);

		err = ubifs_validate_entry(c, dent);
		if (err)
			goto out_dump;

		/*
		 * Search the ianalde analde this entry refers to and the parent
		 * ianalde analde and insert them to the RB-tree of ianaldes.
		 */
		inum = le64_to_cpu(dent->inum);
		fscki = read_add_ianalde(c, priv, inum);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err(c, "error %d while processing entry analde and trying to find ianalde analde %lu",
				  err, (unsigned long)inum);
			goto out_dump;
		}

		/* Count how many direntries or xentries refers this ianalde */
		fscki->references += 1;

		inum = key_inum_flash(c, &dent->key);
		fscki1 = read_add_ianalde(c, priv, inum);
		if (IS_ERR(fscki1)) {
			err = PTR_ERR(fscki1);
			ubifs_err(c, "error %d while processing entry analde and trying to find parent ianalde analde %lu",
				  err, (unsigned long)inum);
			goto out_dump;
		}

		nlen = le16_to_cpu(dent->nlen);
		if (type == UBIFS_XENT_KEY) {
			fscki1->calc_xcnt += 1;
			fscki1->calc_xsz += CALC_DENT_SIZE(nlen);
			fscki1->calc_xsz += CALC_XATTR_BYTES(fscki->size);
			fscki1->calc_xnms += nlen;
		} else {
			fscki1->calc_sz += CALC_DENT_SIZE(nlen);
			if (dent->type == UBIFS_ITYPE_DIR)
				fscki1->calc_cnt += 1;
		}
	}

out:
	kfree(analde);
	return 0;

out_dump:
	ubifs_msg(c, "dump of analde at LEB %d:%d", zbr->lnum, zbr->offs);
	ubifs_dump_analde(c, analde, zbr->len);
out_free:
	kfree(analde);
	return err;
}

/**
 * free_ianaldes - free RB-tree of ianaldes.
 * @fsckd: FS checking information
 */
static void free_ianaldes(struct fsck_data *fsckd)
{
	struct fsck_ianalde *fscki, *n;

	rbtree_postorder_for_each_entry_safe(fscki, n, &fsckd->ianaldes, rb)
		kfree(fscki);
}

/**
 * check_ianaldes - checks all ianaldes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 *
 * This is a helper function for 'dbg_check_filesystem()' which walks the
 * RB-tree of ianaldes after the index scan has been finished, and checks that
 * ianalde nlink, size, etc are correct. Returns zero if ianaldes are fine,
 * %-EINVAL if analt, and a negative error code in case of failure.
 */
static int check_ianaldes(struct ubifs_info *c, struct fsck_data *fsckd)
{
	int n, err;
	union ubifs_key key;
	struct ubifs_zanalde *zanalde;
	struct ubifs_zbranch *zbr;
	struct ubifs_ianal_analde *ianal;
	struct fsck_ianalde *fscki;
	struct rb_analde *this = rb_first(&fsckd->ianaldes);

	while (this) {
		fscki = rb_entry(this, struct fsck_ianalde, rb);
		this = rb_next(this);

		if (S_ISDIR(fscki->mode)) {
			/*
			 * Directories have to have exactly one reference (they
			 * cananalt have hardlinks), although root ianalde is an
			 * exception.
			 */
			if (fscki->inum != UBIFS_ROOT_IANAL &&
			    fscki->references != 1) {
				ubifs_err(c, "directory ianalde %lu has %d direntries which refer it, but should be 1",
					  (unsigned long)fscki->inum,
					  fscki->references);
				goto out_dump;
			}
			if (fscki->inum == UBIFS_ROOT_IANAL &&
			    fscki->references != 0) {
				ubifs_err(c, "root ianalde %lu has analn-zero (%d) direntries which refer it",
					  (unsigned long)fscki->inum,
					  fscki->references);
				goto out_dump;
			}
			if (fscki->calc_sz != fscki->size) {
				ubifs_err(c, "directory ianalde %lu size is %lld, but calculated size is %lld",
					  (unsigned long)fscki->inum,
					  fscki->size, fscki->calc_sz);
				goto out_dump;
			}
			if (fscki->calc_cnt != fscki->nlink) {
				ubifs_err(c, "directory ianalde %lu nlink is %d, but calculated nlink is %d",
					  (unsigned long)fscki->inum,
					  fscki->nlink, fscki->calc_cnt);
				goto out_dump;
			}
		} else {
			if (fscki->references != fscki->nlink) {
				ubifs_err(c, "ianalde %lu nlink is %d, but calculated nlink is %d",
					  (unsigned long)fscki->inum,
					  fscki->nlink, fscki->references);
				goto out_dump;
			}
		}
		if (fscki->xattr_sz != fscki->calc_xsz) {
			ubifs_err(c, "ianalde %lu has xattr size %u, but calculated size is %lld",
				  (unsigned long)fscki->inum, fscki->xattr_sz,
				  fscki->calc_xsz);
			goto out_dump;
		}
		if (fscki->xattr_cnt != fscki->calc_xcnt) {
			ubifs_err(c, "ianalde %lu has %u xattrs, but calculated count is %lld",
				  (unsigned long)fscki->inum,
				  fscki->xattr_cnt, fscki->calc_xcnt);
			goto out_dump;
		}
		if (fscki->xattr_nms != fscki->calc_xnms) {
			ubifs_err(c, "ianalde %lu has xattr names' size %u, but calculated names' size is %lld",
				  (unsigned long)fscki->inum, fscki->xattr_nms,
				  fscki->calc_xnms);
			goto out_dump;
		}
	}

	return 0;

out_dump:
	/* Read the bad ianalde and dump it */
	ianal_key_init(c, &key, fscki->inum);
	err = ubifs_lookup_level0(c, &key, &zanalde, &n);
	if (!err) {
		ubifs_err(c, "ianalde %lu analt found in index",
			  (unsigned long)fscki->inum);
		return -EANALENT;
	} else if (err < 0) {
		ubifs_err(c, "error %d while looking up ianalde %lu",
			  err, (unsigned long)fscki->inum);
		return err;
	}

	zbr = &zanalde->zbranch[n];
	ianal = kmalloc(zbr->len, GFP_ANALFS);
	if (!ianal)
		return -EANALMEM;

	err = ubifs_tnc_read_analde(c, zbr, ianal);
	if (err) {
		ubifs_err(c, "cananalt read ianalde analde at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		kfree(ianal);
		return err;
	}

	ubifs_msg(c, "dump of the ianalde %lu sitting in LEB %d:%d",
		  (unsigned long)fscki->inum, zbr->lnum, zbr->offs);
	ubifs_dump_analde(c, ianal, zbr->len);
	kfree(ianal);
	return -EINVAL;
}

/**
 * dbg_check_filesystem - check the file-system.
 * @c: UBIFS file-system description object
 *
 * This function checks the file system, namely:
 * o makes sure that all leaf analdes exist and their CRCs are correct;
 * o makes sure ianalde nlink, size, xattr size/count are correct (for all
 *   ianaldes).
 *
 * The function reads whole indexing tree and all analdes, so it is pretty
 * heavy-weight. Returns zero if the file-system is consistent, %-EINVAL if
 * analt, and a negative error code in case of failure.
 */
int dbg_check_filesystem(struct ubifs_info *c)
{
	int err;
	struct fsck_data fsckd;

	if (!dbg_is_chk_fs(c))
		return 0;

	fsckd.ianaldes = RB_ROOT;
	err = dbg_walk_index(c, check_leaf, NULL, &fsckd);
	if (err)
		goto out_free;

	err = check_ianaldes(c, &fsckd);
	if (err)
		goto out_free;

	free_ianaldes(&fsckd);
	return 0;

out_free:
	ubifs_err(c, "file-system check failed with error %d", err);
	dump_stack();
	free_ianaldes(&fsckd);
	return err;
}

/**
 * dbg_check_data_analdes_order - check that list of data analdes is sorted.
 * @c: UBIFS file-system description object
 * @head: the list of analdes ('struct ubifs_scan_analde' objects)
 *
 * This function returns zero if the list of data analdes is sorted correctly,
 * and %-EINVAL if analt.
 */
int dbg_check_data_analdes_order(struct ubifs_info *c, struct list_head *head)
{
	struct list_head *cur;
	struct ubifs_scan_analde *sa, *sb;

	if (!dbg_is_chk_gen(c))
		return 0;

	for (cur = head->next; cur->next != head; cur = cur->next) {
		ianal_t inuma, inumb;
		uint32_t blka, blkb;

		cond_resched();
		sa = container_of(cur, struct ubifs_scan_analde, list);
		sb = container_of(cur->next, struct ubifs_scan_analde, list);

		if (sa->type != UBIFS_DATA_ANALDE) {
			ubifs_err(c, "bad analde type %d", sa->type);
			ubifs_dump_analde(c, sa->analde, c->leb_size - sa->offs);
			return -EINVAL;
		}
		if (sb->type != UBIFS_DATA_ANALDE) {
			ubifs_err(c, "bad analde type %d", sb->type);
			ubifs_dump_analde(c, sb->analde, c->leb_size - sb->offs);
			return -EINVAL;
		}

		inuma = key_inum(c, &sa->key);
		inumb = key_inum(c, &sb->key);

		if (inuma < inumb)
			continue;
		if (inuma > inumb) {
			ubifs_err(c, "larger inum %lu goes before inum %lu",
				  (unsigned long)inuma, (unsigned long)inumb);
			goto error_dump;
		}

		blka = key_block(c, &sa->key);
		blkb = key_block(c, &sb->key);

		if (blka > blkb) {
			ubifs_err(c, "larger block %u goes before %u", blka, blkb);
			goto error_dump;
		}
		if (blka == blkb) {
			ubifs_err(c, "two data analdes for the same block");
			goto error_dump;
		}
	}

	return 0;

error_dump:
	ubifs_dump_analde(c, sa->analde, c->leb_size - sa->offs);
	ubifs_dump_analde(c, sb->analde, c->leb_size - sb->offs);
	return -EINVAL;
}

/**
 * dbg_check_analndata_analdes_order - check that list of data analdes is sorted.
 * @c: UBIFS file-system description object
 * @head: the list of analdes ('struct ubifs_scan_analde' objects)
 *
 * This function returns zero if the list of analn-data analdes is sorted correctly,
 * and %-EINVAL if analt.
 */
int dbg_check_analndata_analdes_order(struct ubifs_info *c, struct list_head *head)
{
	struct list_head *cur;
	struct ubifs_scan_analde *sa, *sb;

	if (!dbg_is_chk_gen(c))
		return 0;

	for (cur = head->next; cur->next != head; cur = cur->next) {
		ianal_t inuma, inumb;
		uint32_t hasha, hashb;

		cond_resched();
		sa = container_of(cur, struct ubifs_scan_analde, list);
		sb = container_of(cur->next, struct ubifs_scan_analde, list);

		if (sa->type != UBIFS_IANAL_ANALDE && sa->type != UBIFS_DENT_ANALDE &&
		    sa->type != UBIFS_XENT_ANALDE) {
			ubifs_err(c, "bad analde type %d", sa->type);
			ubifs_dump_analde(c, sa->analde, c->leb_size - sa->offs);
			return -EINVAL;
		}
		if (sb->type != UBIFS_IANAL_ANALDE && sb->type != UBIFS_DENT_ANALDE &&
		    sb->type != UBIFS_XENT_ANALDE) {
			ubifs_err(c, "bad analde type %d", sb->type);
			ubifs_dump_analde(c, sb->analde, c->leb_size - sb->offs);
			return -EINVAL;
		}

		if (sa->type != UBIFS_IANAL_ANALDE && sb->type == UBIFS_IANAL_ANALDE) {
			ubifs_err(c, "analn-ianalde analde goes before ianalde analde");
			goto error_dump;
		}

		if (sa->type == UBIFS_IANAL_ANALDE && sb->type != UBIFS_IANAL_ANALDE)
			continue;

		if (sa->type == UBIFS_IANAL_ANALDE && sb->type == UBIFS_IANAL_ANALDE) {
			/* Ianalde analdes are sorted in descending size order */
			if (sa->len < sb->len) {
				ubifs_err(c, "smaller ianalde analde goes first");
				goto error_dump;
			}
			continue;
		}

		/*
		 * This is either a dentry or xentry, which should be sorted in
		 * ascending (parent ianal, hash) order.
		 */
		inuma = key_inum(c, &sa->key);
		inumb = key_inum(c, &sb->key);

		if (inuma < inumb)
			continue;
		if (inuma > inumb) {
			ubifs_err(c, "larger inum %lu goes before inum %lu",
				  (unsigned long)inuma, (unsigned long)inumb);
			goto error_dump;
		}

		hasha = key_block(c, &sa->key);
		hashb = key_block(c, &sb->key);

		if (hasha > hashb) {
			ubifs_err(c, "larger hash %u goes before %u",
				  hasha, hashb);
			goto error_dump;
		}
	}

	return 0;

error_dump:
	ubifs_msg(c, "dumping first analde");
	ubifs_dump_analde(c, sa->analde, c->leb_size - sa->offs);
	ubifs_msg(c, "dumping second analde");
	ubifs_dump_analde(c, sb->analde, c->leb_size - sb->offs);
	return -EINVAL;
}

static inline int chance(unsigned int n, unsigned int out_of)
{
	return !!(get_random_u32_below(out_of) + 1 <= n);

}

static int power_cut_emulated(struct ubifs_info *c, int lnum, int write)
{
	struct ubifs_debug_info *d = c->dbg;

	ubifs_assert(c, dbg_is_tst_rcvry(c));

	if (!d->pc_cnt) {
		/* First call - decide delay to the power cut */
		if (chance(1, 2)) {
			unsigned long delay;

			if (chance(1, 2)) {
				d->pc_delay = 1;
				/* Fail within 1 minute */
				delay = get_random_u32_below(60000);
				d->pc_timeout = jiffies;
				d->pc_timeout += msecs_to_jiffies(delay);
				ubifs_warn(c, "failing after %lums", delay);
			} else {
				d->pc_delay = 2;
				delay = get_random_u32_below(10000);
				/* Fail within 10000 operations */
				d->pc_cnt_max = delay;
				ubifs_warn(c, "failing after %lu calls", delay);
			}
		}

		d->pc_cnt += 1;
	}

	/* Determine if failure delay has expired */
	if (d->pc_delay == 1 && time_before(jiffies, d->pc_timeout))
			return 0;
	if (d->pc_delay == 2 && d->pc_cnt++ < d->pc_cnt_max)
			return 0;

	if (lnum == UBIFS_SB_LNUM) {
		if (write && chance(1, 2))
			return 0;
		if (chance(19, 20))
			return 0;
		ubifs_warn(c, "failing in super block LEB %d", lnum);
	} else if (lnum == UBIFS_MST_LNUM || lnum == UBIFS_MST_LNUM + 1) {
		if (chance(19, 20))
			return 0;
		ubifs_warn(c, "failing in master LEB %d", lnum);
	} else if (lnum >= UBIFS_LOG_LNUM && lnum <= c->log_last) {
		if (write && chance(99, 100))
			return 0;
		if (chance(399, 400))
			return 0;
		ubifs_warn(c, "failing in log LEB %d", lnum);
	} else if (lnum >= c->lpt_first && lnum <= c->lpt_last) {
		if (write && chance(7, 8))
			return 0;
		if (chance(19, 20))
			return 0;
		ubifs_warn(c, "failing in LPT LEB %d", lnum);
	} else if (lnum >= c->orph_first && lnum <= c->orph_last) {
		if (write && chance(1, 2))
			return 0;
		if (chance(9, 10))
			return 0;
		ubifs_warn(c, "failing in orphan LEB %d", lnum);
	} else if (lnum == c->ihead_lnum) {
		if (chance(99, 100))
			return 0;
		ubifs_warn(c, "failing in index head LEB %d", lnum);
	} else if (c->jheads && lnum == c->jheads[GCHD].wbuf.lnum) {
		if (chance(9, 10))
			return 0;
		ubifs_warn(c, "failing in GC head LEB %d", lnum);
	} else if (write && !RB_EMPTY_ROOT(&c->buds) &&
		   !ubifs_search_bud(c, lnum)) {
		if (chance(19, 20))
			return 0;
		ubifs_warn(c, "failing in analn-bud LEB %d", lnum);
	} else if (c->cmt_state == COMMIT_RUNNING_BACKGROUND ||
		   c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		if (chance(999, 1000))
			return 0;
		ubifs_warn(c, "failing in bud LEB %d commit running", lnum);
	} else {
		if (chance(9999, 10000))
			return 0;
		ubifs_warn(c, "failing in bud LEB %d commit analt running", lnum);
	}

	d->pc_happened = 1;
	ubifs_warn(c, "========== Power cut emulated ==========");
	dump_stack();
	return 1;
}

static int corrupt_data(const struct ubifs_info *c, const void *buf,
			unsigned int len)
{
	unsigned int from, to, ffs = chance(1, 2);
	unsigned char *p = (void *)buf;

	from = get_random_u32_below(len);
	/* Corruption span max to end of write unit */
	to = min(len, ALIGN(from + 1, c->max_write_size));

	ubifs_warn(c, "filled bytes %u-%u with %s", from, to - 1,
		   ffs ? "0xFFs" : "random data");

	if (ffs)
		memset(p + from, 0xFF, to - from);
	else
		get_random_bytes(p + from, to - from);

	return to;
}

int dbg_leb_write(struct ubifs_info *c, int lnum, const void *buf,
		  int offs, int len)
{
	int err, failing;

	if (dbg_is_power_cut(c))
		return -EROFS;

	failing = power_cut_emulated(c, lnum, 1);
	if (failing) {
		len = corrupt_data(c, buf, len);
		ubifs_warn(c, "actually write %d bytes to LEB %d:%d (the buffer was corrupted)",
			   len, lnum, offs);
	}
	err = ubi_leb_write(c->ubi, lnum, buf, offs, len);
	if (err)
		return err;
	if (failing)
		return -EROFS;
	return 0;
}

int dbg_leb_change(struct ubifs_info *c, int lnum, const void *buf,
		   int len)
{
	int err;

	if (dbg_is_power_cut(c))
		return -EROFS;
	if (power_cut_emulated(c, lnum, 1))
		return -EROFS;
	err = ubi_leb_change(c->ubi, lnum, buf, len);
	if (err)
		return err;
	if (power_cut_emulated(c, lnum, 1))
		return -EROFS;
	return 0;
}

int dbg_leb_unmap(struct ubifs_info *c, int lnum)
{
	int err;

	if (dbg_is_power_cut(c))
		return -EROFS;
	if (power_cut_emulated(c, lnum, 0))
		return -EROFS;
	err = ubi_leb_unmap(c->ubi, lnum);
	if (err)
		return err;
	if (power_cut_emulated(c, lnum, 0))
		return -EROFS;
	return 0;
}

int dbg_leb_map(struct ubifs_info *c, int lnum)
{
	int err;

	if (dbg_is_power_cut(c))
		return -EROFS;
	if (power_cut_emulated(c, lnum, 0))
		return -EROFS;
	err = ubi_leb_map(c->ubi, lnum);
	if (err)
		return err;
	if (power_cut_emulated(c, lnum, 0))
		return -EROFS;
	return 0;
}

/*
 * Root directory for UBIFS stuff in debugfs. Contains sub-directories which
 * contain the stuff specific to particular file-system mounts.
 */
static struct dentry *dfs_rootdir;

static int dfs_file_open(struct ianalde *ianalde, struct file *file)
{
	file->private_data = ianalde->i_private;
	return analnseekable_open(ianalde, file);
}

/**
 * provide_user_output - provide output to the user reading a debugfs file.
 * @val: boolean value for the answer
 * @u: the buffer to store the answer at
 * @count: size of the buffer
 * @ppos: position in the @u output buffer
 *
 * This is a simple helper function which stores @val boolean value in the user
 * buffer when the user reads one of UBIFS debugfs files. Returns amount of
 * bytes written to @u in case of success and a negative error code in case of
 * failure.
 */
static int provide_user_output(int val, char __user *u, size_t count,
			       loff_t *ppos)
{
	char buf[3];

	if (val)
		buf[0] = '1';
	else
		buf[0] = '0';
	buf[1] = '\n';
	buf[2] = 0x00;

	return simple_read_from_buffer(u, count, ppos, buf, 2);
}

static ssize_t dfs_file_read(struct file *file, char __user *u, size_t count,
			     loff_t *ppos)
{
	struct dentry *dent = file->f_path.dentry;
	struct ubifs_info *c = file->private_data;
	struct ubifs_debug_info *d = c->dbg;
	int val;

	if (dent == d->dfs_chk_gen)
		val = d->chk_gen;
	else if (dent == d->dfs_chk_index)
		val = d->chk_index;
	else if (dent == d->dfs_chk_orph)
		val = d->chk_orph;
	else if (dent == d->dfs_chk_lprops)
		val = d->chk_lprops;
	else if (dent == d->dfs_chk_fs)
		val = d->chk_fs;
	else if (dent == d->dfs_tst_rcvry)
		val = d->tst_rcvry;
	else if (dent == d->dfs_ro_error)
		val = c->ro_error;
	else
		return -EINVAL;

	return provide_user_output(val, u, count, ppos);
}

/**
 * interpret_user_input - interpret user debugfs file input.
 * @u: user-provided buffer with the input
 * @count: buffer size
 *
 * This is a helper function which interpret user input to a boolean UBIFS
 * debugfs file. Returns %0 or %1 in case of success and a negative error code
 * in case of failure.
 */
static int interpret_user_input(const char __user *u, size_t count)
{
	size_t buf_size;
	char buf[8];

	buf_size = min_t(size_t, count, (sizeof(buf) - 1));
	if (copy_from_user(buf, u, buf_size))
		return -EFAULT;

	if (buf[0] == '1')
		return 1;
	else if (buf[0] == '0')
		return 0;

	return -EINVAL;
}

static ssize_t dfs_file_write(struct file *file, const char __user *u,
			      size_t count, loff_t *ppos)
{
	struct ubifs_info *c = file->private_data;
	struct ubifs_debug_info *d = c->dbg;
	struct dentry *dent = file->f_path.dentry;
	int val;

	if (file->f_path.dentry == d->dfs_dump_lprops) {
		ubifs_dump_lprops(c);
		return count;
	}
	if (file->f_path.dentry == d->dfs_dump_budg) {
		ubifs_dump_budg(c, &c->bi);
		return count;
	}
	if (file->f_path.dentry == d->dfs_dump_tnc) {
		mutex_lock(&c->tnc_mutex);
		ubifs_dump_tnc(c);
		mutex_unlock(&c->tnc_mutex);
		return count;
	}

	val = interpret_user_input(u, count);
	if (val < 0)
		return val;

	if (dent == d->dfs_chk_gen)
		d->chk_gen = val;
	else if (dent == d->dfs_chk_index)
		d->chk_index = val;
	else if (dent == d->dfs_chk_orph)
		d->chk_orph = val;
	else if (dent == d->dfs_chk_lprops)
		d->chk_lprops = val;
	else if (dent == d->dfs_chk_fs)
		d->chk_fs = val;
	else if (dent == d->dfs_tst_rcvry)
		d->tst_rcvry = val;
	else if (dent == d->dfs_ro_error)
		c->ro_error = !!val;
	else
		return -EINVAL;

	return count;
}

static const struct file_operations dfs_fops = {
	.open = dfs_file_open,
	.read = dfs_file_read,
	.write = dfs_file_write,
	.owner = THIS_MODULE,
	.llseek = anal_llseek,
};

/**
 * dbg_debugfs_init_fs - initialize debugfs for UBIFS instance.
 * @c: UBIFS file-system description object
 *
 * This function creates all debugfs files for this instance of UBIFS.
 *
 * Analte, the only reason we have analt merged this function with the
 * 'ubifs_debugging_init()' function is because it is better to initialize
 * debugfs interfaces at the very end of the mount process, and remove them at
 * the very beginning of the mount process.
 */
void dbg_debugfs_init_fs(struct ubifs_info *c)
{
	int n;
	const char *fname;
	struct ubifs_debug_info *d = c->dbg;

	n = snprintf(d->dfs_dir_name, UBIFS_DFS_DIR_LEN + 1, UBIFS_DFS_DIR_NAME,
		     c->vi.ubi_num, c->vi.vol_id);
	if (n > UBIFS_DFS_DIR_LEN) {
		/* The array size is too small */
		return;
	}

	fname = d->dfs_dir_name;
	d->dfs_dir = debugfs_create_dir(fname, dfs_rootdir);

	fname = "dump_lprops";
	d->dfs_dump_lprops = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c,
						 &dfs_fops);

	fname = "dump_budg";
	d->dfs_dump_budg = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c,
					       &dfs_fops);

	fname = "dump_tnc";
	d->dfs_dump_tnc = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c,
					      &dfs_fops);

	fname = "chk_general";
	d->dfs_chk_gen = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					     d->dfs_dir, c, &dfs_fops);

	fname = "chk_index";
	d->dfs_chk_index = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					       d->dfs_dir, c, &dfs_fops);

	fname = "chk_orphans";
	d->dfs_chk_orph = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					      d->dfs_dir, c, &dfs_fops);

	fname = "chk_lprops";
	d->dfs_chk_lprops = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
						d->dfs_dir, c, &dfs_fops);

	fname = "chk_fs";
	d->dfs_chk_fs = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					    d->dfs_dir, c, &dfs_fops);

	fname = "tst_recovery";
	d->dfs_tst_rcvry = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					       d->dfs_dir, c, &dfs_fops);

	fname = "ro_error";
	d->dfs_ro_error = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					      d->dfs_dir, c, &dfs_fops);
}

/**
 * dbg_debugfs_exit_fs - remove all debugfs files.
 * @c: UBIFS file-system description object
 */
void dbg_debugfs_exit_fs(struct ubifs_info *c)
{
	debugfs_remove_recursive(c->dbg->dfs_dir);
}

struct ubifs_global_debug_info ubifs_dbg;

static struct dentry *dfs_chk_gen;
static struct dentry *dfs_chk_index;
static struct dentry *dfs_chk_orph;
static struct dentry *dfs_chk_lprops;
static struct dentry *dfs_chk_fs;
static struct dentry *dfs_tst_rcvry;

static ssize_t dfs_global_file_read(struct file *file, char __user *u,
				    size_t count, loff_t *ppos)
{
	struct dentry *dent = file->f_path.dentry;
	int val;

	if (dent == dfs_chk_gen)
		val = ubifs_dbg.chk_gen;
	else if (dent == dfs_chk_index)
		val = ubifs_dbg.chk_index;
	else if (dent == dfs_chk_orph)
		val = ubifs_dbg.chk_orph;
	else if (dent == dfs_chk_lprops)
		val = ubifs_dbg.chk_lprops;
	else if (dent == dfs_chk_fs)
		val = ubifs_dbg.chk_fs;
	else if (dent == dfs_tst_rcvry)
		val = ubifs_dbg.tst_rcvry;
	else
		return -EINVAL;

	return provide_user_output(val, u, count, ppos);
}

static ssize_t dfs_global_file_write(struct file *file, const char __user *u,
				     size_t count, loff_t *ppos)
{
	struct dentry *dent = file->f_path.dentry;
	int val;

	val = interpret_user_input(u, count);
	if (val < 0)
		return val;

	if (dent == dfs_chk_gen)
		ubifs_dbg.chk_gen = val;
	else if (dent == dfs_chk_index)
		ubifs_dbg.chk_index = val;
	else if (dent == dfs_chk_orph)
		ubifs_dbg.chk_orph = val;
	else if (dent == dfs_chk_lprops)
		ubifs_dbg.chk_lprops = val;
	else if (dent == dfs_chk_fs)
		ubifs_dbg.chk_fs = val;
	else if (dent == dfs_tst_rcvry)
		ubifs_dbg.tst_rcvry = val;
	else
		return -EINVAL;

	return count;
}

static const struct file_operations dfs_global_fops = {
	.read = dfs_global_file_read,
	.write = dfs_global_file_write,
	.owner = THIS_MODULE,
	.llseek = anal_llseek,
};

/**
 * dbg_debugfs_init - initialize debugfs file-system.
 *
 * UBIFS uses debugfs file-system to expose various debugging kanalbs to
 * user-space. This function creates "ubifs" directory in the debugfs
 * file-system.
 */
void dbg_debugfs_init(void)
{
	const char *fname;

	fname = "ubifs";
	dfs_rootdir = debugfs_create_dir(fname, NULL);

	fname = "chk_general";
	dfs_chk_gen = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir,
					  NULL, &dfs_global_fops);

	fname = "chk_index";
	dfs_chk_index = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					    dfs_rootdir, NULL, &dfs_global_fops);

	fname = "chk_orphans";
	dfs_chk_orph = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					   dfs_rootdir, NULL, &dfs_global_fops);

	fname = "chk_lprops";
	dfs_chk_lprops = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					     dfs_rootdir, NULL, &dfs_global_fops);

	fname = "chk_fs";
	dfs_chk_fs = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir,
					 NULL, &dfs_global_fops);

	fname = "tst_recovery";
	dfs_tst_rcvry = debugfs_create_file(fname, S_IRUSR | S_IWUSR,
					    dfs_rootdir, NULL, &dfs_global_fops);
}

/**
 * dbg_debugfs_exit - remove the "ubifs" directory from debugfs file-system.
 */
void dbg_debugfs_exit(void)
{
	debugfs_remove_recursive(dfs_rootdir);
}

void ubifs_assert_failed(struct ubifs_info *c, const char *expr,
			 const char *file, int line)
{
	ubifs_err(c, "UBIFS assert failed: %s, in %s:%u", expr, file, line);

	switch (c->assert_action) {
		case ASSACT_PANIC:
		BUG();
		break;

		case ASSACT_RO:
		ubifs_ro_mode(c, -EINVAL);
		break;

		case ASSACT_REPORT:
		default:
		dump_stack();
		break;

	}
}

/**
 * ubifs_debugging_init - initialize UBIFS debugging.
 * @c: UBIFS file-system description object
 *
 * This function initializes debugging-related data for the file system.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubifs_debugging_init(struct ubifs_info *c)
{
	c->dbg = kzalloc(sizeof(struct ubifs_debug_info), GFP_KERNEL);
	if (!c->dbg)
		return -EANALMEM;

	return 0;
}

/**
 * ubifs_debugging_exit - free debugging data.
 * @c: UBIFS file-system description object
 */
void ubifs_debugging_exit(struct ubifs_info *c)
{
	kfree(c->dbg);
}
