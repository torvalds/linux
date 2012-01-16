/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
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
#include "ubifs.h"

#ifdef CONFIG_UBIFS_FS_DEBUG

DEFINE_SPINLOCK(dbg_lock);

static const char *get_key_fmt(int fmt)
{
	switch (fmt) {
	case UBIFS_SIMPLE_KEY_FMT:
		return "simple";
	default:
		return "unknown/invalid format";
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
		return "unknown/invalid name hash";
	}
}

static const char *get_key_type(int type)
{
	switch (type) {
	case UBIFS_INO_KEY:
		return "inode";
	case UBIFS_DENT_KEY:
		return "direntry";
	case UBIFS_XENT_KEY:
		return "xentry";
	case UBIFS_DATA_KEY:
		return "data";
	case UBIFS_TRUN_KEY:
		return "truncate";
	default:
		return "unknown/invalid key";
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
		return "unknown/invalid type";
	}
}

const char *dbg_snprintf_key(const struct ubifs_info *c,
			     const union ubifs_key *key, char *buffer, int len)
{
	char *p = buffer;
	int type = key_type(c, key);

	if (c->key_fmt == UBIFS_SIMPLE_KEY_FMT) {
		switch (type) {
		case UBIFS_INO_KEY:
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
	ubifs_assert(len > 0);
	return p;
}

const char *dbg_ntype(int type)
{
	switch (type) {
	case UBIFS_PAD_NODE:
		return "padding node";
	case UBIFS_SB_NODE:
		return "superblock node";
	case UBIFS_MST_NODE:
		return "master node";
	case UBIFS_REF_NODE:
		return "reference node";
	case UBIFS_INO_NODE:
		return "inode node";
	case UBIFS_DENT_NODE:
		return "direntry node";
	case UBIFS_XENT_NODE:
		return "xentry node";
	case UBIFS_DATA_NODE:
		return "data node";
	case UBIFS_TRUN_NODE:
		return "truncate node";
	case UBIFS_IDX_NODE:
		return "indexing node";
	case UBIFS_CS_NODE:
		return "commit start node";
	case UBIFS_ORPH_NODE:
		return "orphan node";
	default:
		return "unknown node";
	}
}

static const char *dbg_gtype(int type)
{
	switch (type) {
	case UBIFS_NO_NODE_GROUP:
		return "no node group";
	case UBIFS_IN_NODE_GROUP:
		return "in node group";
	case UBIFS_LAST_OF_NODE_GROUP:
		return "last of node group";
	default:
		return "unknown";
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
		return "unknown commit state";
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
		return "unknown journal head";
	}
}

static void dump_ch(const struct ubifs_ch *ch)
{
	printk(KERN_DEBUG "\tmagic          %#x\n", le32_to_cpu(ch->magic));
	printk(KERN_DEBUG "\tcrc            %#x\n", le32_to_cpu(ch->crc));
	printk(KERN_DEBUG "\tnode_type      %d (%s)\n", ch->node_type,
	       dbg_ntype(ch->node_type));
	printk(KERN_DEBUG "\tgroup_type     %d (%s)\n", ch->group_type,
	       dbg_gtype(ch->group_type));
	printk(KERN_DEBUG "\tsqnum          %llu\n",
	       (unsigned long long)le64_to_cpu(ch->sqnum));
	printk(KERN_DEBUG "\tlen            %u\n", le32_to_cpu(ch->len));
}

void dbg_dump_inode(struct ubifs_info *c, const struct inode *inode)
{
	const struct ubifs_inode *ui = ubifs_inode(inode);
	struct qstr nm = { .name = NULL };
	union ubifs_key key;
	struct ubifs_dent_node *dent, *pdent = NULL;
	int count = 2;

	printk(KERN_DEBUG "Dump in-memory inode:");
	printk(KERN_DEBUG "\tinode          %lu\n", inode->i_ino);
	printk(KERN_DEBUG "\tsize           %llu\n",
	       (unsigned long long)i_size_read(inode));
	printk(KERN_DEBUG "\tnlink          %u\n", inode->i_nlink);
	printk(KERN_DEBUG "\tuid            %u\n", (unsigned int)inode->i_uid);
	printk(KERN_DEBUG "\tgid            %u\n", (unsigned int)inode->i_gid);
	printk(KERN_DEBUG "\tatime          %u.%u\n",
	       (unsigned int)inode->i_atime.tv_sec,
	       (unsigned int)inode->i_atime.tv_nsec);
	printk(KERN_DEBUG "\tmtime          %u.%u\n",
	       (unsigned int)inode->i_mtime.tv_sec,
	       (unsigned int)inode->i_mtime.tv_nsec);
	printk(KERN_DEBUG "\tctime          %u.%u\n",
	       (unsigned int)inode->i_ctime.tv_sec,
	       (unsigned int)inode->i_ctime.tv_nsec);
	printk(KERN_DEBUG "\tcreat_sqnum    %llu\n", ui->creat_sqnum);
	printk(KERN_DEBUG "\txattr_size     %u\n", ui->xattr_size);
	printk(KERN_DEBUG "\txattr_cnt      %u\n", ui->xattr_cnt);
	printk(KERN_DEBUG "\txattr_names    %u\n", ui->xattr_names);
	printk(KERN_DEBUG "\tdirty          %u\n", ui->dirty);
	printk(KERN_DEBUG "\txattr          %u\n", ui->xattr);
	printk(KERN_DEBUG "\tbulk_read      %u\n", ui->xattr);
	printk(KERN_DEBUG "\tsynced_i_size  %llu\n",
	       (unsigned long long)ui->synced_i_size);
	printk(KERN_DEBUG "\tui_size        %llu\n",
	       (unsigned long long)ui->ui_size);
	printk(KERN_DEBUG "\tflags          %d\n", ui->flags);
	printk(KERN_DEBUG "\tcompr_type     %d\n", ui->compr_type);
	printk(KERN_DEBUG "\tlast_page_read %lu\n", ui->last_page_read);
	printk(KERN_DEBUG "\tread_in_a_row  %lu\n", ui->read_in_a_row);
	printk(KERN_DEBUG "\tdata_len       %d\n", ui->data_len);

	if (!S_ISDIR(inode->i_mode))
		return;

	printk(KERN_DEBUG "List of directory entries:\n");
	ubifs_assert(!mutex_is_locked(&c->tnc_mutex));

	lowest_dent_key(c, &key, inode->i_ino);
	while (1) {
		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			if (PTR_ERR(dent) != -ENOENT)
				printk(KERN_DEBUG "error %ld\n", PTR_ERR(dent));
			break;
		}

		printk(KERN_DEBUG "\t%d: %s (%s)\n",
		       count++, dent->name, get_dent_type(dent->type));

		nm.name = dent->name;
		nm.len = le16_to_cpu(dent->nlen);
		kfree(pdent);
		pdent = dent;
		key_read(c, &dent->key, &key);
	}
	kfree(pdent);
}

void dbg_dump_node(const struct ubifs_info *c, const void *node)
{
	int i, n;
	union ubifs_key key;
	const struct ubifs_ch *ch = node;
	char key_buf[DBG_KEY_BUF_LEN];

	if (dbg_is_tst_rcvry(c))
		return;

	/* If the magic is incorrect, just hexdump the first bytes */
	if (le32_to_cpu(ch->magic) != UBIFS_NODE_MAGIC) {
		printk(KERN_DEBUG "Not a node, first %zu bytes:", UBIFS_CH_SZ);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
			       (void *)node, UBIFS_CH_SZ, 1);
		return;
	}

	spin_lock(&dbg_lock);
	dump_ch(node);

	switch (ch->node_type) {
	case UBIFS_PAD_NODE:
	{
		const struct ubifs_pad_node *pad = node;

		printk(KERN_DEBUG "\tpad_len        %u\n",
		       le32_to_cpu(pad->pad_len));
		break;
	}
	case UBIFS_SB_NODE:
	{
		const struct ubifs_sb_node *sup = node;
		unsigned int sup_flags = le32_to_cpu(sup->flags);

		printk(KERN_DEBUG "\tkey_hash       %d (%s)\n",
		       (int)sup->key_hash, get_key_hash(sup->key_hash));
		printk(KERN_DEBUG "\tkey_fmt        %d (%s)\n",
		       (int)sup->key_fmt, get_key_fmt(sup->key_fmt));
		printk(KERN_DEBUG "\tflags          %#x\n", sup_flags);
		printk(KERN_DEBUG "\t  big_lpt      %u\n",
		       !!(sup_flags & UBIFS_FLG_BIGLPT));
		printk(KERN_DEBUG "\t  space_fixup  %u\n",
		       !!(sup_flags & UBIFS_FLG_SPACE_FIXUP));
		printk(KERN_DEBUG "\tmin_io_size    %u\n",
		       le32_to_cpu(sup->min_io_size));
		printk(KERN_DEBUG "\tleb_size       %u\n",
		       le32_to_cpu(sup->leb_size));
		printk(KERN_DEBUG "\tleb_cnt        %u\n",
		       le32_to_cpu(sup->leb_cnt));
		printk(KERN_DEBUG "\tmax_leb_cnt    %u\n",
		       le32_to_cpu(sup->max_leb_cnt));
		printk(KERN_DEBUG "\tmax_bud_bytes  %llu\n",
		       (unsigned long long)le64_to_cpu(sup->max_bud_bytes));
		printk(KERN_DEBUG "\tlog_lebs       %u\n",
		       le32_to_cpu(sup->log_lebs));
		printk(KERN_DEBUG "\tlpt_lebs       %u\n",
		       le32_to_cpu(sup->lpt_lebs));
		printk(KERN_DEBUG "\torph_lebs      %u\n",
		       le32_to_cpu(sup->orph_lebs));
		printk(KERN_DEBUG "\tjhead_cnt      %u\n",
		       le32_to_cpu(sup->jhead_cnt));
		printk(KERN_DEBUG "\tfanout         %u\n",
		       le32_to_cpu(sup->fanout));
		printk(KERN_DEBUG "\tlsave_cnt      %u\n",
		       le32_to_cpu(sup->lsave_cnt));
		printk(KERN_DEBUG "\tdefault_compr  %u\n",
		       (int)le16_to_cpu(sup->default_compr));
		printk(KERN_DEBUG "\trp_size        %llu\n",
		       (unsigned long long)le64_to_cpu(sup->rp_size));
		printk(KERN_DEBUG "\trp_uid         %u\n",
		       le32_to_cpu(sup->rp_uid));
		printk(KERN_DEBUG "\trp_gid         %u\n",
		       le32_to_cpu(sup->rp_gid));
		printk(KERN_DEBUG "\tfmt_version    %u\n",
		       le32_to_cpu(sup->fmt_version));
		printk(KERN_DEBUG "\ttime_gran      %u\n",
		       le32_to_cpu(sup->time_gran));
		printk(KERN_DEBUG "\tUUID           %pUB\n",
		       sup->uuid);
		break;
	}
	case UBIFS_MST_NODE:
	{
		const struct ubifs_mst_node *mst = node;

		printk(KERN_DEBUG "\thighest_inum   %llu\n",
		       (unsigned long long)le64_to_cpu(mst->highest_inum));
		printk(KERN_DEBUG "\tcommit number  %llu\n",
		       (unsigned long long)le64_to_cpu(mst->cmt_no));
		printk(KERN_DEBUG "\tflags          %#x\n",
		       le32_to_cpu(mst->flags));
		printk(KERN_DEBUG "\tlog_lnum       %u\n",
		       le32_to_cpu(mst->log_lnum));
		printk(KERN_DEBUG "\troot_lnum      %u\n",
		       le32_to_cpu(mst->root_lnum));
		printk(KERN_DEBUG "\troot_offs      %u\n",
		       le32_to_cpu(mst->root_offs));
		printk(KERN_DEBUG "\troot_len       %u\n",
		       le32_to_cpu(mst->root_len));
		printk(KERN_DEBUG "\tgc_lnum        %u\n",
		       le32_to_cpu(mst->gc_lnum));
		printk(KERN_DEBUG "\tihead_lnum     %u\n",
		       le32_to_cpu(mst->ihead_lnum));
		printk(KERN_DEBUG "\tihead_offs     %u\n",
		       le32_to_cpu(mst->ihead_offs));
		printk(KERN_DEBUG "\tindex_size     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->index_size));
		printk(KERN_DEBUG "\tlpt_lnum       %u\n",
		       le32_to_cpu(mst->lpt_lnum));
		printk(KERN_DEBUG "\tlpt_offs       %u\n",
		       le32_to_cpu(mst->lpt_offs));
		printk(KERN_DEBUG "\tnhead_lnum     %u\n",
		       le32_to_cpu(mst->nhead_lnum));
		printk(KERN_DEBUG "\tnhead_offs     %u\n",
		       le32_to_cpu(mst->nhead_offs));
		printk(KERN_DEBUG "\tltab_lnum      %u\n",
		       le32_to_cpu(mst->ltab_lnum));
		printk(KERN_DEBUG "\tltab_offs      %u\n",
		       le32_to_cpu(mst->ltab_offs));
		printk(KERN_DEBUG "\tlsave_lnum     %u\n",
		       le32_to_cpu(mst->lsave_lnum));
		printk(KERN_DEBUG "\tlsave_offs     %u\n",
		       le32_to_cpu(mst->lsave_offs));
		printk(KERN_DEBUG "\tlscan_lnum     %u\n",
		       le32_to_cpu(mst->lscan_lnum));
		printk(KERN_DEBUG "\tleb_cnt        %u\n",
		       le32_to_cpu(mst->leb_cnt));
		printk(KERN_DEBUG "\tempty_lebs     %u\n",
		       le32_to_cpu(mst->empty_lebs));
		printk(KERN_DEBUG "\tidx_lebs       %u\n",
		       le32_to_cpu(mst->idx_lebs));
		printk(KERN_DEBUG "\ttotal_free     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_free));
		printk(KERN_DEBUG "\ttotal_dirty    %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dirty));
		printk(KERN_DEBUG "\ttotal_used     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_used));
		printk(KERN_DEBUG "\ttotal_dead     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dead));
		printk(KERN_DEBUG "\ttotal_dark     %llu\n",
		       (unsigned long long)le64_to_cpu(mst->total_dark));
		break;
	}
	case UBIFS_REF_NODE:
	{
		const struct ubifs_ref_node *ref = node;

		printk(KERN_DEBUG "\tlnum           %u\n",
		       le32_to_cpu(ref->lnum));
		printk(KERN_DEBUG "\toffs           %u\n",
		       le32_to_cpu(ref->offs));
		printk(KERN_DEBUG "\tjhead          %u\n",
		       le32_to_cpu(ref->jhead));
		break;
	}
	case UBIFS_INO_NODE:
	{
		const struct ubifs_ino_node *ino = node;

		key_read(c, &ino->key, &key);
		printk(KERN_DEBUG "\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		printk(KERN_DEBUG "\tcreat_sqnum    %llu\n",
		       (unsigned long long)le64_to_cpu(ino->creat_sqnum));
		printk(KERN_DEBUG "\tsize           %llu\n",
		       (unsigned long long)le64_to_cpu(ino->size));
		printk(KERN_DEBUG "\tnlink          %u\n",
		       le32_to_cpu(ino->nlink));
		printk(KERN_DEBUG "\tatime          %lld.%u\n",
		       (long long)le64_to_cpu(ino->atime_sec),
		       le32_to_cpu(ino->atime_nsec));
		printk(KERN_DEBUG "\tmtime          %lld.%u\n",
		       (long long)le64_to_cpu(ino->mtime_sec),
		       le32_to_cpu(ino->mtime_nsec));
		printk(KERN_DEBUG "\tctime          %lld.%u\n",
		       (long long)le64_to_cpu(ino->ctime_sec),
		       le32_to_cpu(ino->ctime_nsec));
		printk(KERN_DEBUG "\tuid            %u\n",
		       le32_to_cpu(ino->uid));
		printk(KERN_DEBUG "\tgid            %u\n",
		       le32_to_cpu(ino->gid));
		printk(KERN_DEBUG "\tmode           %u\n",
		       le32_to_cpu(ino->mode));
		printk(KERN_DEBUG "\tflags          %#x\n",
		       le32_to_cpu(ino->flags));
		printk(KERN_DEBUG "\txattr_cnt      %u\n",
		       le32_to_cpu(ino->xattr_cnt));
		printk(KERN_DEBUG "\txattr_size     %u\n",
		       le32_to_cpu(ino->xattr_size));
		printk(KERN_DEBUG "\txattr_names    %u\n",
		       le32_to_cpu(ino->xattr_names));
		printk(KERN_DEBUG "\tcompr_type     %#x\n",
		       (int)le16_to_cpu(ino->compr_type));
		printk(KERN_DEBUG "\tdata len       %u\n",
		       le32_to_cpu(ino->data_len));
		break;
	}
	case UBIFS_DENT_NODE:
	case UBIFS_XENT_NODE:
	{
		const struct ubifs_dent_node *dent = node;
		int nlen = le16_to_cpu(dent->nlen);

		key_read(c, &dent->key, &key);
		printk(KERN_DEBUG "\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		printk(KERN_DEBUG "\tinum           %llu\n",
		       (unsigned long long)le64_to_cpu(dent->inum));
		printk(KERN_DEBUG "\ttype           %d\n", (int)dent->type);
		printk(KERN_DEBUG "\tnlen           %d\n", nlen);
		printk(KERN_DEBUG "\tname           ");

		if (nlen > UBIFS_MAX_NLEN)
			printk(KERN_DEBUG "(bad name length, not printing, "
					  "bad or corrupted node)");
		else {
			for (i = 0; i < nlen && dent->name[i]; i++)
				printk(KERN_CONT "%c", dent->name[i]);
		}
		printk(KERN_CONT "\n");

		break;
	}
	case UBIFS_DATA_NODE:
	{
		const struct ubifs_data_node *dn = node;
		int dlen = le32_to_cpu(ch->len) - UBIFS_DATA_NODE_SZ;

		key_read(c, &dn->key, &key);
		printk(KERN_DEBUG "\tkey            %s\n",
		       dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));
		printk(KERN_DEBUG "\tsize           %u\n",
		       le32_to_cpu(dn->size));
		printk(KERN_DEBUG "\tcompr_typ      %d\n",
		       (int)le16_to_cpu(dn->compr_type));
		printk(KERN_DEBUG "\tdata size      %d\n",
		       dlen);
		printk(KERN_DEBUG "\tdata:\n");
		print_hex_dump(KERN_DEBUG, "\t", DUMP_PREFIX_OFFSET, 32, 1,
			       (void *)&dn->data, dlen, 0);
		break;
	}
	case UBIFS_TRUN_NODE:
	{
		const struct ubifs_trun_node *trun = node;

		printk(KERN_DEBUG "\tinum           %u\n",
		       le32_to_cpu(trun->inum));
		printk(KERN_DEBUG "\told_size       %llu\n",
		       (unsigned long long)le64_to_cpu(trun->old_size));
		printk(KERN_DEBUG "\tnew_size       %llu\n",
		       (unsigned long long)le64_to_cpu(trun->new_size));
		break;
	}
	case UBIFS_IDX_NODE:
	{
		const struct ubifs_idx_node *idx = node;

		n = le16_to_cpu(idx->child_cnt);
		printk(KERN_DEBUG "\tchild_cnt      %d\n", n);
		printk(KERN_DEBUG "\tlevel          %d\n",
		       (int)le16_to_cpu(idx->level));
		printk(KERN_DEBUG "\tBranches:\n");

		for (i = 0; i < n && i < c->fanout - 1; i++) {
			const struct ubifs_branch *br;

			br = ubifs_idx_branch(c, idx, i);
			key_read(c, &br->key, &key);
			printk(KERN_DEBUG "\t%d: LEB %d:%d len %d key %s\n",
			       i, le32_to_cpu(br->lnum), le32_to_cpu(br->offs),
			       le32_to_cpu(br->len),
			       dbg_snprintf_key(c, &key, key_buf,
						DBG_KEY_BUF_LEN));
		}
		break;
	}
	case UBIFS_CS_NODE:
		break;
	case UBIFS_ORPH_NODE:
	{
		const struct ubifs_orph_node *orph = node;

		printk(KERN_DEBUG "\tcommit number  %llu\n",
		       (unsigned long long)
				le64_to_cpu(orph->cmt_no) & LLONG_MAX);
		printk(KERN_DEBUG "\tlast node flag %llu\n",
		       (unsigned long long)(le64_to_cpu(orph->cmt_no)) >> 63);
		n = (le32_to_cpu(ch->len) - UBIFS_ORPH_NODE_SZ) >> 3;
		printk(KERN_DEBUG "\t%d orphan inode numbers:\n", n);
		for (i = 0; i < n; i++)
			printk(KERN_DEBUG "\t  ino %llu\n",
			       (unsigned long long)le64_to_cpu(orph->inos[i]));
		break;
	}
	default:
		printk(KERN_DEBUG "node type %d was not recognized\n",
		       (int)ch->node_type);
	}
	spin_unlock(&dbg_lock);
}

void dbg_dump_budget_req(const struct ubifs_budget_req *req)
{
	spin_lock(&dbg_lock);
	printk(KERN_DEBUG "Budgeting request: new_ino %d, dirtied_ino %d\n",
	       req->new_ino, req->dirtied_ino);
	printk(KERN_DEBUG "\tnew_ino_d   %d, dirtied_ino_d %d\n",
	       req->new_ino_d, req->dirtied_ino_d);
	printk(KERN_DEBUG "\tnew_page    %d, dirtied_page %d\n",
	       req->new_page, req->dirtied_page);
	printk(KERN_DEBUG "\tnew_dent    %d, mod_dent     %d\n",
	       req->new_dent, req->mod_dent);
	printk(KERN_DEBUG "\tidx_growth  %d\n", req->idx_growth);
	printk(KERN_DEBUG "\tdata_growth %d dd_growth     %d\n",
	       req->data_growth, req->dd_growth);
	spin_unlock(&dbg_lock);
}

void dbg_dump_lstats(const struct ubifs_lp_stats *lst)
{
	spin_lock(&dbg_lock);
	printk(KERN_DEBUG "(pid %d) Lprops statistics: empty_lebs %d, "
	       "idx_lebs  %d\n", current->pid, lst->empty_lebs, lst->idx_lebs);
	printk(KERN_DEBUG "\ttaken_empty_lebs %d, total_free %lld, "
	       "total_dirty %lld\n", lst->taken_empty_lebs, lst->total_free,
	       lst->total_dirty);
	printk(KERN_DEBUG "\ttotal_used %lld, total_dark %lld, "
	       "total_dead %lld\n", lst->total_used, lst->total_dark,
	       lst->total_dead);
	spin_unlock(&dbg_lock);
}

void dbg_dump_budg(struct ubifs_info *c, const struct ubifs_budg_info *bi)
{
	int i;
	struct rb_node *rb;
	struct ubifs_bud *bud;
	struct ubifs_gced_idx_leb *idx_gc;
	long long available, outstanding, free;

	spin_lock(&c->space_lock);
	spin_lock(&dbg_lock);
	printk(KERN_DEBUG "(pid %d) Budgeting info: data budget sum %lld, "
	       "total budget sum %lld\n", current->pid,
	       bi->data_growth + bi->dd_growth,
	       bi->data_growth + bi->dd_growth + bi->idx_growth);
	printk(KERN_DEBUG "\tbudg_data_growth %lld, budg_dd_growth %lld, "
	       "budg_idx_growth %lld\n", bi->data_growth, bi->dd_growth,
	       bi->idx_growth);
	printk(KERN_DEBUG "\tmin_idx_lebs %d, old_idx_sz %llu, "
	       "uncommitted_idx %lld\n", bi->min_idx_lebs, bi->old_idx_sz,
	       bi->uncommitted_idx);
	printk(KERN_DEBUG "\tpage_budget %d, inode_budget %d, dent_budget %d\n",
	       bi->page_budget, bi->inode_budget, bi->dent_budget);
	printk(KERN_DEBUG "\tnospace %u, nospace_rp %u\n",
	       bi->nospace, bi->nospace_rp);
	printk(KERN_DEBUG "\tdark_wm %d, dead_wm %d, max_idx_node_sz %d\n",
	       c->dark_wm, c->dead_wm, c->max_idx_node_sz);

	if (bi != &c->bi)
		/*
		 * If we are dumping saved budgeting data, do not print
		 * additional information which is about the current state, not
		 * the old one which corresponded to the saved budgeting data.
		 */
		goto out_unlock;

	printk(KERN_DEBUG "\tfreeable_cnt %d, calc_idx_sz %lld, idx_gc_cnt %d\n",
	       c->freeable_cnt, c->calc_idx_sz, c->idx_gc_cnt);
	printk(KERN_DEBUG "\tdirty_pg_cnt %ld, dirty_zn_cnt %ld, "
	       "clean_zn_cnt %ld\n", atomic_long_read(&c->dirty_pg_cnt),
	       atomic_long_read(&c->dirty_zn_cnt),
	       atomic_long_read(&c->clean_zn_cnt));
	printk(KERN_DEBUG "\tgc_lnum %d, ihead_lnum %d\n",
	       c->gc_lnum, c->ihead_lnum);

	/* If we are in R/O mode, journal heads do not exist */
	if (c->jheads)
		for (i = 0; i < c->jhead_cnt; i++)
			printk(KERN_DEBUG "\tjhead %s\t LEB %d\n",
			       dbg_jhead(c->jheads[i].wbuf.jhead),
			       c->jheads[i].wbuf.lnum);
	for (rb = rb_first(&c->buds); rb; rb = rb_next(rb)) {
		bud = rb_entry(rb, struct ubifs_bud, rb);
		printk(KERN_DEBUG "\tbud LEB %d\n", bud->lnum);
	}
	list_for_each_entry(bud, &c->old_buds, list)
		printk(KERN_DEBUG "\told bud LEB %d\n", bud->lnum);
	list_for_each_entry(idx_gc, &c->idx_gc, list)
		printk(KERN_DEBUG "\tGC'ed idx LEB %d unmap %d\n",
		       idx_gc->lnum, idx_gc->unmap);
	printk(KERN_DEBUG "\tcommit state %d\n", c->cmt_state);

	/* Print budgeting predictions */
	available = ubifs_calc_available(c, c->bi.min_idx_lebs);
	outstanding = c->bi.data_growth + c->bi.dd_growth;
	free = ubifs_get_free_space_nolock(c);
	printk(KERN_DEBUG "Budgeting predictions:\n");
	printk(KERN_DEBUG "\tavailable: %lld, outstanding %lld, free %lld\n",
	       available, outstanding, free);
out_unlock:
	spin_unlock(&dbg_lock);
	spin_unlock(&c->space_lock);
}

void dbg_dump_lprop(const struct ubifs_info *c, const struct ubifs_lprops *lp)
{
	int i, spc, dark = 0, dead = 0;
	struct rb_node *rb;
	struct ubifs_bud *bud;

	spc = lp->free + lp->dirty;
	if (spc < c->dead_wm)
		dead = spc;
	else
		dark = ubifs_calc_dark(c, spc);

	if (lp->flags & LPROPS_INDEX)
		printk(KERN_DEBUG "LEB %-7d free %-8d dirty %-8d used %-8d "
		       "free + dirty %-8d flags %#x (", lp->lnum, lp->free,
		       lp->dirty, c->leb_size - spc, spc, lp->flags);
	else
		printk(KERN_DEBUG "LEB %-7d free %-8d dirty %-8d used %-8d "
		       "free + dirty %-8d dark %-4d dead %-4d nodes fit %-3d "
		       "flags %#-4x (", lp->lnum, lp->free, lp->dirty,
		       c->leb_size - spc, spc, dark, dead,
		       (int)(spc / UBIFS_MAX_NODE_SZ), lp->flags);

	if (lp->flags & LPROPS_TAKEN) {
		if (lp->flags & LPROPS_INDEX)
			printk(KERN_CONT "index, taken");
		else
			printk(KERN_CONT "taken");
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
				s = "not categorized";
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
		printk(KERN_CONT "%s", s);
	}

	for (rb = rb_first((struct rb_root *)&c->buds); rb; rb = rb_next(rb)) {
		bud = rb_entry(rb, struct ubifs_bud, rb);
		if (bud->lnum == lp->lnum) {
			int head = 0;
			for (i = 0; i < c->jhead_cnt; i++) {
				/*
				 * Note, if we are in R/O mode or in the middle
				 * of mounting/re-mounting, the write-buffers do
				 * not exist.
				 */
				if (c->jheads &&
				    lp->lnum == c->jheads[i].wbuf.lnum) {
					printk(KERN_CONT ", jhead %s",
					       dbg_jhead(i));
					head = 1;
				}
			}
			if (!head)
				printk(KERN_CONT ", bud of jhead %s",
				       dbg_jhead(bud->jhead));
		}
	}
	if (lp->lnum == c->gc_lnum)
		printk(KERN_CONT ", GC LEB");
	printk(KERN_CONT ")\n");
}

void dbg_dump_lprops(struct ubifs_info *c)
{
	int lnum, err;
	struct ubifs_lprops lp;
	struct ubifs_lp_stats lst;

	printk(KERN_DEBUG "(pid %d) start dumping LEB properties\n",
	       current->pid);
	ubifs_get_lp_stats(c, &lst);
	dbg_dump_lstats(&lst);

	for (lnum = c->main_first; lnum < c->leb_cnt; lnum++) {
		err = ubifs_read_one_lp(c, lnum, &lp);
		if (err)
			ubifs_err("cannot read lprops for LEB %d", lnum);

		dbg_dump_lprop(c, &lp);
	}
	printk(KERN_DEBUG "(pid %d) finish dumping LEB properties\n",
	       current->pid);
}

void dbg_dump_lpt_info(struct ubifs_info *c)
{
	int i;

	spin_lock(&dbg_lock);
	printk(KERN_DEBUG "(pid %d) dumping LPT information\n", current->pid);
	printk(KERN_DEBUG "\tlpt_sz:        %lld\n", c->lpt_sz);
	printk(KERN_DEBUG "\tpnode_sz:      %d\n", c->pnode_sz);
	printk(KERN_DEBUG "\tnnode_sz:      %d\n", c->nnode_sz);
	printk(KERN_DEBUG "\tltab_sz:       %d\n", c->ltab_sz);
	printk(KERN_DEBUG "\tlsave_sz:      %d\n", c->lsave_sz);
	printk(KERN_DEBUG "\tbig_lpt:       %d\n", c->big_lpt);
	printk(KERN_DEBUG "\tlpt_hght:      %d\n", c->lpt_hght);
	printk(KERN_DEBUG "\tpnode_cnt:     %d\n", c->pnode_cnt);
	printk(KERN_DEBUG "\tnnode_cnt:     %d\n", c->nnode_cnt);
	printk(KERN_DEBUG "\tdirty_pn_cnt:  %d\n", c->dirty_pn_cnt);
	printk(KERN_DEBUG "\tdirty_nn_cnt:  %d\n", c->dirty_nn_cnt);
	printk(KERN_DEBUG "\tlsave_cnt:     %d\n", c->lsave_cnt);
	printk(KERN_DEBUG "\tspace_bits:    %d\n", c->space_bits);
	printk(KERN_DEBUG "\tlpt_lnum_bits: %d\n", c->lpt_lnum_bits);
	printk(KERN_DEBUG "\tlpt_offs_bits: %d\n", c->lpt_offs_bits);
	printk(KERN_DEBUG "\tlpt_spc_bits:  %d\n", c->lpt_spc_bits);
	printk(KERN_DEBUG "\tpcnt_bits:     %d\n", c->pcnt_bits);
	printk(KERN_DEBUG "\tlnum_bits:     %d\n", c->lnum_bits);
	printk(KERN_DEBUG "\tLPT root is at %d:%d\n", c->lpt_lnum, c->lpt_offs);
	printk(KERN_DEBUG "\tLPT head is at %d:%d\n",
	       c->nhead_lnum, c->nhead_offs);
	printk(KERN_DEBUG "\tLPT ltab is at %d:%d\n",
	       c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		printk(KERN_DEBUG "\tLPT lsave is at %d:%d\n",
		       c->lsave_lnum, c->lsave_offs);
	for (i = 0; i < c->lpt_lebs; i++)
		printk(KERN_DEBUG "\tLPT LEB %d free %d dirty %d tgc %d "
		       "cmt %d\n", i + c->lpt_first, c->ltab[i].free,
		       c->ltab[i].dirty, c->ltab[i].tgc, c->ltab[i].cmt);
	spin_unlock(&dbg_lock);
}

void dbg_dump_sleb(const struct ubifs_info *c,
		   const struct ubifs_scan_leb *sleb, int offs)
{
	struct ubifs_scan_node *snod;

	printk(KERN_DEBUG "(pid %d) start dumping scanned data from LEB %d:%d\n",
	       current->pid, sleb->lnum, offs);

	list_for_each_entry(snod, &sleb->nodes, list) {
		cond_resched();
		printk(KERN_DEBUG "Dumping node at LEB %d:%d len %d\n", sleb->lnum,
		       snod->offs, snod->len);
		dbg_dump_node(c, snod->node);
	}
}

void dbg_dump_leb(const struct ubifs_info *c, int lnum)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;
	void *buf;

	if (dbg_is_tst_rcvry(c))
		return;

	printk(KERN_DEBUG "(pid %d) start dumping LEB %d\n",
	       current->pid, lnum);

	buf = __vmalloc(c->leb_size, GFP_NOFS, PAGE_KERNEL);
	if (!buf) {
		ubifs_err("cannot allocate memory for dumping LEB %d", lnum);
		return;
	}

	sleb = ubifs_scan(c, lnum, 0, buf, 0);
	if (IS_ERR(sleb)) {
		ubifs_err("scan error %d", (int)PTR_ERR(sleb));
		goto out;
	}

	printk(KERN_DEBUG "LEB %d has %d nodes ending at %d\n", lnum,
	       sleb->nodes_cnt, sleb->endpt);

	list_for_each_entry(snod, &sleb->nodes, list) {
		cond_resched();
		printk(KERN_DEBUG "Dumping node at LEB %d:%d len %d\n", lnum,
		       snod->offs, snod->len);
		dbg_dump_node(c, snod->node);
	}

	printk(KERN_DEBUG "(pid %d) finish dumping LEB %d\n",
	       current->pid, lnum);
	ubifs_scan_destroy(sleb);

out:
	vfree(buf);
	return;
}

void dbg_dump_znode(const struct ubifs_info *c,
		    const struct ubifs_znode *znode)
{
	int n;
	const struct ubifs_zbranch *zbr;
	char key_buf[DBG_KEY_BUF_LEN];

	spin_lock(&dbg_lock);
	if (znode->parent)
		zbr = &znode->parent->zbranch[znode->iip];
	else
		zbr = &c->zroot;

	printk(KERN_DEBUG "znode %p, LEB %d:%d len %d parent %p iip %d level %d"
	       " child_cnt %d flags %lx\n", znode, zbr->lnum, zbr->offs,
	       zbr->len, znode->parent, znode->iip, znode->level,
	       znode->child_cnt, znode->flags);

	if (znode->child_cnt <= 0 || znode->child_cnt > c->fanout) {
		spin_unlock(&dbg_lock);
		return;
	}

	printk(KERN_DEBUG "zbranches:\n");
	for (n = 0; n < znode->child_cnt; n++) {
		zbr = &znode->zbranch[n];
		if (znode->level > 0)
			printk(KERN_DEBUG "\t%d: znode %p LEB %d:%d len %d key "
					  "%s\n", n, zbr->znode, zbr->lnum,
					  zbr->offs, zbr->len,
					  dbg_snprintf_key(c, &zbr->key,
							   key_buf,
							   DBG_KEY_BUF_LEN));
		else
			printk(KERN_DEBUG "\t%d: LNC %p LEB %d:%d len %d key "
					  "%s\n", n, zbr->znode, zbr->lnum,
					  zbr->offs, zbr->len,
					  dbg_snprintf_key(c, &zbr->key,
							   key_buf,
							   DBG_KEY_BUF_LEN));
	}
	spin_unlock(&dbg_lock);
}

void dbg_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat)
{
	int i;

	printk(KERN_DEBUG "(pid %d) start dumping heap cat %d (%d elements)\n",
	       current->pid, cat, heap->cnt);
	for (i = 0; i < heap->cnt; i++) {
		struct ubifs_lprops *lprops = heap->arr[i];

		printk(KERN_DEBUG "\t%d. LEB %d hpos %d free %d dirty %d "
		       "flags %d\n", i, lprops->lnum, lprops->hpos,
		       lprops->free, lprops->dirty, lprops->flags);
	}
	printk(KERN_DEBUG "(pid %d) finish dumping heap\n", current->pid);
}

void dbg_dump_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
		    struct ubifs_nnode *parent, int iip)
{
	int i;

	printk(KERN_DEBUG "(pid %d) dumping pnode:\n", current->pid);
	printk(KERN_DEBUG "\taddress %zx parent %zx cnext %zx\n",
	       (size_t)pnode, (size_t)parent, (size_t)pnode->cnext);
	printk(KERN_DEBUG "\tflags %lu iip %d level %d num %d\n",
	       pnode->flags, iip, pnode->level, pnode->num);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops *lp = &pnode->lprops[i];

		printk(KERN_DEBUG "\t%d: free %d dirty %d flags %d lnum %d\n",
		       i, lp->free, lp->dirty, lp->flags, lp->lnum);
	}
}

void dbg_dump_tnc(struct ubifs_info *c)
{
	struct ubifs_znode *znode;
	int level;

	printk(KERN_DEBUG "\n");
	printk(KERN_DEBUG "(pid %d) start dumping TNC tree\n", current->pid);
	znode = ubifs_tnc_levelorder_next(c->zroot.znode, NULL);
	level = znode->level;
	printk(KERN_DEBUG "== Level %d ==\n", level);
	while (znode) {
		if (level != znode->level) {
			level = znode->level;
			printk(KERN_DEBUG "== Level %d ==\n", level);
		}
		dbg_dump_znode(c, znode);
		znode = ubifs_tnc_levelorder_next(c->zroot.znode, znode);
	}
	printk(KERN_DEBUG "(pid %d) finish dumping TNC tree\n", current->pid);
}

static int dump_znode(struct ubifs_info *c, struct ubifs_znode *znode,
		      void *priv)
{
	dbg_dump_znode(c, znode);
	return 0;
}

/**
 * dbg_dump_index - dump the on-flash index.
 * @c: UBIFS file-system description object
 *
 * This function dumps whole UBIFS indexing B-tree, unlike 'dbg_dump_tnc()'
 * which dumps only in-memory znodes and does not read znodes which from flash.
 */
void dbg_dump_index(struct ubifs_info *c)
{
	dbg_walk_index(c, NULL, dump_znode, NULL);
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
	 * affects the free space calculations, and UBIFS might not know about
	 * all freeable eraseblocks. Indeed, we know about freeable eraseblocks
	 * only when we read their lprops, and we do this only lazily, upon the
	 * need. So at any given point of time @c->freeable_cnt might be not
	 * exactly accurate.
	 *
	 * Just one example about the issue we hit when we did not zero
	 * @c->freeable_cnt.
	 * 1. The file-system is mounted R/O, c->freeable_cnt is %0. We save the
	 *    amount of free space in @d->saved_free
	 * 2. We re-mount R/W, which makes UBIFS to read the "lsave"
	 *    information from flash, where we cache LEBs from various
	 *    categories ('ubifs_remount_fs()' -> 'ubifs_lpt_init()'
	 *    -> 'lpt_init_wr()' -> 'read_lsave()' -> 'ubifs_lpt_lookup()'
	 *    -> 'ubifs_get_pnode()' -> 'update_cats()'
	 *    -> 'ubifs_add_to_cat()').
	 * 3. Lsave contains a freeable eraseblock, and @c->freeable_cnt
	 *    becomes %1.
	 * 4. We calculate the amount of free space when the re-mount is
	 *    finished in 'dbg_check_space_info()' and it does not match
	 *    @d->saved_free.
	 */
	freeable_cnt = c->freeable_cnt;
	c->freeable_cnt = 0;
	d->saved_free = ubifs_get_free_space_nolock(c);
	c->freeable_cnt = freeable_cnt;
	spin_unlock(&c->space_lock);
}

/**
 * dbg_check_space_info - check flash space information.
 * @c: UBIFS file-system description object
 *
 * This function compares current flash space information with the information
 * which was saved when the 'dbg_save_space_info()' function was called.
 * Returns zero if the information has not changed, and %-EINVAL it it has
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
	free = ubifs_get_free_space_nolock(c);
	c->freeable_cnt = freeable_cnt;
	spin_unlock(&c->space_lock);

	if (free != d->saved_free) {
		ubifs_err("free space changed from %lld to %lld",
			  d->saved_free, free);
		goto out;
	}

	return 0;

out:
	ubifs_msg("saved lprops statistics dump");
	dbg_dump_lstats(&d->saved_lst);
	ubifs_msg("saved budgeting info dump");
	dbg_dump_budg(c, &d->saved_bi);
	ubifs_msg("saved idx_gc_cnt %d", d->saved_idx_gc_cnt);
	ubifs_msg("current lprops statistics dump");
	ubifs_get_lp_stats(c, &lst);
	dbg_dump_lstats(&lst);
	ubifs_msg("current budgeting info dump");
	dbg_dump_budg(c, &c->bi);
	dump_stack();
	return -EINVAL;
}

/**
 * dbg_check_synced_i_size - check synchronized inode size.
 * @c: UBIFS file-system description object
 * @inode: inode to check
 *
 * If inode is clean, synchronized inode size has to be equivalent to current
 * inode size. This function has to be called only for locked inodes (@i_mutex
 * has to be locked). Returns %0 if synchronized inode size if correct, and
 * %-EINVAL if not.
 */
int dbg_check_synced_i_size(const struct ubifs_info *c, struct inode *inode)
{
	int err = 0;
	struct ubifs_inode *ui = ubifs_inode(inode);

	if (!dbg_is_chk_gen(c))
		return 0;
	if (!S_ISREG(inode->i_mode))
		return 0;

	mutex_lock(&ui->ui_mutex);
	spin_lock(&ui->ui_lock);
	if (ui->ui_size != ui->synced_i_size && !ui->dirty) {
		ubifs_err("ui_size is %lld, synced_i_size is %lld, but inode "
			  "is clean", ui->ui_size, ui->synced_i_size);
		ubifs_err("i_ino %lu, i_mode %#x, i_size %lld", inode->i_ino,
			  inode->i_mode, i_size_read(inode));
		dbg_dump_stack();
		err = -EINVAL;
	}
	spin_unlock(&ui->ui_lock);
	mutex_unlock(&ui->ui_mutex);
	return err;
}

/*
 * dbg_check_dir - check directory inode size and link count.
 * @c: UBIFS file-system description object
 * @dir: the directory to calculate size for
 * @size: the result is returned here
 *
 * This function makes sure that directory size and link count are correct.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 *
 * Note, it is good idea to make sure the @dir->i_mutex is locked before
 * calling this function.
 */
int dbg_check_dir(struct ubifs_info *c, const struct inode *dir)
{
	unsigned int nlink = 2;
	union ubifs_key key;
	struct ubifs_dent_node *dent, *pdent = NULL;
	struct qstr nm = { .name = NULL };
	loff_t size = UBIFS_INO_NODE_SZ;

	if (!dbg_is_chk_gen(c))
		return 0;

	if (!S_ISDIR(dir->i_mode))
		return 0;

	lowest_dent_key(c, &key, dir->i_ino);
	while (1) {
		int err;

		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			err = PTR_ERR(dent);
			if (err == -ENOENT)
				break;
			return err;
		}

		nm.name = dent->name;
		nm.len = le16_to_cpu(dent->nlen);
		size += CALC_DENT_SIZE(nm.len);
		if (dent->type == UBIFS_ITYPE_DIR)
			nlink += 1;
		kfree(pdent);
		pdent = dent;
		key_read(c, &dent->key, &key);
	}
	kfree(pdent);

	if (i_size_read(dir) != size) {
		ubifs_err("directory inode %lu has size %llu, "
			  "but calculated size is %llu", dir->i_ino,
			  (unsigned long long)i_size_read(dir),
			  (unsigned long long)size);
		dbg_dump_inode(c, dir);
		dump_stack();
		return -EINVAL;
	}
	if (dir->i_nlink != nlink) {
		ubifs_err("directory inode %lu has nlink %u, but calculated "
			  "nlink is %u", dir->i_ino, dir->i_nlink, nlink);
		dbg_dump_inode(c, dir);
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
 * direntry/xentry referred by @zbr2. Returns zero if this is true, %1 if not,
 * and a negative error code in case of failure.
 */
static int dbg_check_key_order(struct ubifs_info *c, struct ubifs_zbranch *zbr1,
			       struct ubifs_zbranch *zbr2)
{
	int err, nlen1, nlen2, cmp;
	struct ubifs_dent_node *dent1, *dent2;
	union ubifs_key key;
	char key_buf[DBG_KEY_BUF_LEN];

	ubifs_assert(!keys_cmp(c, &zbr1->key, &zbr2->key));
	dent1 = kmalloc(UBIFS_MAX_DENT_NODE_SZ, GFP_NOFS);
	if (!dent1)
		return -ENOMEM;
	dent2 = kmalloc(UBIFS_MAX_DENT_NODE_SZ, GFP_NOFS);
	if (!dent2) {
		err = -ENOMEM;
		goto out_free;
	}

	err = ubifs_tnc_read_node(c, zbr1, dent1);
	if (err)
		goto out_free;
	err = ubifs_validate_entry(c, dent1);
	if (err)
		goto out_free;

	err = ubifs_tnc_read_node(c, zbr2, dent2);
	if (err)
		goto out_free;
	err = ubifs_validate_entry(c, dent2);
	if (err)
		goto out_free;

	/* Make sure node keys are the same as in zbranch */
	err = 1;
	key_read(c, &dent1->key, &key);
	if (keys_cmp(c, &zbr1->key, &key)) {
		dbg_err("1st entry at %d:%d has key %s", zbr1->lnum,
			zbr1->offs, dbg_snprintf_key(c, &key, key_buf,
						     DBG_KEY_BUF_LEN));
		dbg_err("but it should have key %s according to tnc",
			dbg_snprintf_key(c, &zbr1->key, key_buf,
					 DBG_KEY_BUF_LEN));
		dbg_dump_node(c, dent1);
		goto out_free;
	}

	key_read(c, &dent2->key, &key);
	if (keys_cmp(c, &zbr2->key, &key)) {
		dbg_err("2nd entry at %d:%d has key %s", zbr1->lnum,
			zbr1->offs, dbg_snprintf_key(c, &key, key_buf,
						     DBG_KEY_BUF_LEN));
		dbg_err("but it should have key %s according to tnc",
			dbg_snprintf_key(c, &zbr2->key, key_buf,
					 DBG_KEY_BUF_LEN));
		dbg_dump_node(c, dent2);
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
		dbg_err("2 xent/dent nodes with the same name");
	else
		dbg_err("bad order of colliding key %s",
			dbg_snprintf_key(c, &key, key_buf, DBG_KEY_BUF_LEN));

	ubifs_msg("first node at %d:%d\n", zbr1->lnum, zbr1->offs);
	dbg_dump_node(c, dent1);
	ubifs_msg("second node at %d:%d\n", zbr2->lnum, zbr2->offs);
	dbg_dump_node(c, dent2);

out_free:
	kfree(dent2);
	kfree(dent1);
	return err;
}

/**
 * dbg_check_znode - check if znode is all right.
 * @c: UBIFS file-system description object
 * @zbr: zbranch which points to this znode
 *
 * This function makes sure that znode referred to by @zbr is all right.
 * Returns zero if it is, and %-EINVAL if it is not.
 */
static int dbg_check_znode(struct ubifs_info *c, struct ubifs_zbranch *zbr)
{
	struct ubifs_znode *znode = zbr->znode;
	struct ubifs_znode *zp = znode->parent;
	int n, err, cmp;

	if (znode->child_cnt <= 0 || znode->child_cnt > c->fanout) {
		err = 1;
		goto out;
	}
	if (znode->level < 0) {
		err = 2;
		goto out;
	}
	if (znode->iip < 0 || znode->iip >= c->fanout) {
		err = 3;
		goto out;
	}

	if (zbr->len == 0)
		/* Only dirty zbranch may have no on-flash nodes */
		if (!ubifs_zn_dirty(znode)) {
			err = 4;
			goto out;
		}

	if (ubifs_zn_dirty(znode)) {
		/*
		 * If znode is dirty, its parent has to be dirty as well. The
		 * order of the operation is important, so we have to have
		 * memory barriers.
		 */
		smp_mb();
		if (zp && !ubifs_zn_dirty(zp)) {
			/*
			 * The dirty flag is atomic and is cleared outside the
			 * TNC mutex, so znode's dirty flag may now have
			 * been cleared. The child is always cleared before the
			 * parent, so we just need to check again.
			 */
			smp_mb();
			if (ubifs_zn_dirty(znode)) {
				err = 5;
				goto out;
			}
		}
	}

	if (zp) {
		const union ubifs_key *min, *max;

		if (znode->level != zp->level - 1) {
			err = 6;
			goto out;
		}

		/* Make sure the 'parent' pointer in our znode is correct */
		err = ubifs_search_zbranch(c, zp, &zbr->key, &n);
		if (!err) {
			/* This zbranch does not exist in the parent */
			err = 7;
			goto out;
		}

		if (znode->iip >= zp->child_cnt) {
			err = 8;
			goto out;
		}

		if (znode->iip != n) {
			/* This may happen only in case of collisions */
			if (keys_cmp(c, &zp->zbranch[n].key,
				     &zp->zbranch[znode->iip].key)) {
				err = 9;
				goto out;
			}
			n = znode->iip;
		}

		/*
		 * Make sure that the first key in our znode is greater than or
		 * equal to the key in the pointing zbranch.
		 */
		min = &zbr->key;
		cmp = keys_cmp(c, min, &znode->zbranch[0].key);
		if (cmp == 1) {
			err = 10;
			goto out;
		}

		if (n + 1 < zp->child_cnt) {
			max = &zp->zbranch[n + 1].key;

			/*
			 * Make sure the last key in our znode is less or
			 * equivalent than the key in the zbranch which goes
			 * after our pointing zbranch.
			 */
			cmp = keys_cmp(c, max,
				&znode->zbranch[znode->child_cnt - 1].key);
			if (cmp == -1) {
				err = 11;
				goto out;
			}
		}
	} else {
		/* This may only be root znode */
		if (zbr != &c->zroot) {
			err = 12;
			goto out;
		}
	}

	/*
	 * Make sure that next key is greater or equivalent then the previous
	 * one.
	 */
	for (n = 1; n < znode->child_cnt; n++) {
		cmp = keys_cmp(c, &znode->zbranch[n - 1].key,
			       &znode->zbranch[n].key);
		if (cmp > 0) {
			err = 13;
			goto out;
		}
		if (cmp == 0) {
			/* This can only be keys with colliding hash */
			if (!is_hash_key(c, &znode->zbranch[n].key)) {
				err = 14;
				goto out;
			}

			if (znode->level != 0 || c->replaying)
				continue;

			/*
			 * Colliding keys should follow binary order of
			 * corresponding xentry/dentry names.
			 */
			err = dbg_check_key_order(c, &znode->zbranch[n - 1],
						  &znode->zbranch[n]);
			if (err < 0)
				return err;
			if (err) {
				err = 15;
				goto out;
			}
		}
	}

	for (n = 0; n < znode->child_cnt; n++) {
		if (!znode->zbranch[n].znode &&
		    (znode->zbranch[n].lnum == 0 ||
		     znode->zbranch[n].len == 0)) {
			err = 16;
			goto out;
		}

		if (znode->zbranch[n].lnum != 0 &&
		    znode->zbranch[n].len == 0) {
			err = 17;
			goto out;
		}

		if (znode->zbranch[n].lnum == 0 &&
		    znode->zbranch[n].len != 0) {
			err = 18;
			goto out;
		}

		if (znode->zbranch[n].lnum == 0 &&
		    znode->zbranch[n].offs != 0) {
			err = 19;
			goto out;
		}

		if (znode->level != 0 && znode->zbranch[n].znode)
			if (znode->zbranch[n].znode->parent != znode) {
				err = 20;
				goto out;
			}
	}

	return 0;

out:
	ubifs_err("failed, error %d", err);
	ubifs_msg("dump of the znode");
	dbg_dump_znode(c, znode);
	if (zp) {
		ubifs_msg("dump of the parent znode");
		dbg_dump_znode(c, zp);
	}
	dump_stack();
	return -EINVAL;
}

/**
 * dbg_check_tnc - check TNC tree.
 * @c: UBIFS file-system description object
 * @extra: do extra checks that are possible at start commit
 *
 * This function traverses whole TNC tree and checks every znode. Returns zero
 * if everything is all right and %-EINVAL if something is wrong with TNC.
 */
int dbg_check_tnc(struct ubifs_info *c, int extra)
{
	struct ubifs_znode *znode;
	long clean_cnt = 0, dirty_cnt = 0;
	int err, last;

	if (!dbg_is_chk_index(c))
		return 0;

	ubifs_assert(mutex_is_locked(&c->tnc_mutex));
	if (!c->zroot.znode)
		return 0;

	znode = ubifs_tnc_postorder_first(c->zroot.znode);
	while (1) {
		struct ubifs_znode *prev;
		struct ubifs_zbranch *zbr;

		if (!znode->parent)
			zbr = &c->zroot;
		else
			zbr = &znode->parent->zbranch[znode->iip];

		err = dbg_check_znode(c, zbr);
		if (err)
			return err;

		if (extra) {
			if (ubifs_zn_dirty(znode))
				dirty_cnt += 1;
			else
				clean_cnt += 1;
		}

		prev = znode;
		znode = ubifs_tnc_postorder_next(znode);
		if (!znode)
			break;

		/*
		 * If the last key of this znode is equivalent to the first key
		 * of the next znode (collision), then check order of the keys.
		 */
		last = prev->child_cnt - 1;
		if (prev->level == 0 && znode->level == 0 && !c->replaying &&
		    !keys_cmp(c, &prev->zbranch[last].key,
			      &znode->zbranch[0].key)) {
			err = dbg_check_key_order(c, &prev->zbranch[last],
						  &znode->zbranch[0]);
			if (err < 0)
				return err;
			if (err) {
				ubifs_msg("first znode");
				dbg_dump_znode(c, prev);
				ubifs_msg("second znode");
				dbg_dump_znode(c, znode);
				return -EINVAL;
			}
		}
	}

	if (extra) {
		if (clean_cnt != atomic_long_read(&c->clean_zn_cnt)) {
			ubifs_err("incorrect clean_zn_cnt %ld, calculated %ld",
				  atomic_long_read(&c->clean_zn_cnt),
				  clean_cnt);
			return -EINVAL;
		}
		if (dirty_cnt != atomic_long_read(&c->dirty_zn_cnt)) {
			ubifs_err("incorrect dirty_zn_cnt %ld, calculated %ld",
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
 * @leaf_cb: called for each leaf node
 * @znode_cb: called for each indexing node
 * @priv: private data which is passed to callbacks
 *
 * This function walks the UBIFS index and calls the @leaf_cb for each leaf
 * node and @znode_cb for each indexing node. Returns zero in case of success
 * and a negative error code in case of failure.
 *
 * It would be better if this function removed every znode it pulled to into
 * the TNC, so that the behavior more closely matched the non-debugging
 * behavior.
 */
int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv)
{
	int err;
	struct ubifs_zbranch *zbr;
	struct ubifs_znode *znode, *child;

	mutex_lock(&c->tnc_mutex);
	/* If the root indexing node is not in TNC - pull it */
	if (!c->zroot.znode) {
		c->zroot.znode = ubifs_load_znode(c, &c->zroot, NULL, 0);
		if (IS_ERR(c->zroot.znode)) {
			err = PTR_ERR(c->zroot.znode);
			c->zroot.znode = NULL;
			goto out_unlock;
		}
	}

	/*
	 * We are going to traverse the indexing tree in the postorder manner.
	 * Go down and find the leftmost indexing node where we are going to
	 * start from.
	 */
	znode = c->zroot.znode;
	while (znode->level > 0) {
		zbr = &znode->zbranch[0];
		child = zbr->znode;
		if (!child) {
			child = ubifs_load_znode(c, zbr, znode, 0);
			if (IS_ERR(child)) {
				err = PTR_ERR(child);
				goto out_unlock;
			}
			zbr->znode = child;
		}

		znode = child;
	}

	/* Iterate over all indexing nodes */
	while (1) {
		int idx;

		cond_resched();

		if (znode_cb) {
			err = znode_cb(c, znode, priv);
			if (err) {
				ubifs_err("znode checking function returned "
					  "error %d", err);
				dbg_dump_znode(c, znode);
				goto out_dump;
			}
		}
		if (leaf_cb && znode->level == 0) {
			for (idx = 0; idx < znode->child_cnt; idx++) {
				zbr = &znode->zbranch[idx];
				err = leaf_cb(c, zbr, priv);
				if (err) {
					ubifs_err("leaf checking function "
						  "returned error %d, for leaf "
						  "at LEB %d:%d",
						  err, zbr->lnum, zbr->offs);
					goto out_dump;
				}
			}
		}

		if (!znode->parent)
			break;

		idx = znode->iip + 1;
		znode = znode->parent;
		if (idx < znode->child_cnt) {
			/* Switch to the next index in the parent */
			zbr = &znode->zbranch[idx];
			child = zbr->znode;
			if (!child) {
				child = ubifs_load_znode(c, zbr, znode, idx);
				if (IS_ERR(child)) {
					err = PTR_ERR(child);
					goto out_unlock;
				}
				zbr->znode = child;
			}
			znode = child;
		} else
			/*
			 * This is the last child, switch to the parent and
			 * continue.
			 */
			continue;

		/* Go to the lowest leftmost znode in the new sub-tree */
		while (znode->level > 0) {
			zbr = &znode->zbranch[0];
			child = zbr->znode;
			if (!child) {
				child = ubifs_load_znode(c, zbr, znode, 0);
				if (IS_ERR(child)) {
					err = PTR_ERR(child);
					goto out_unlock;
				}
				zbr->znode = child;
			}
			znode = child;
		}
	}

	mutex_unlock(&c->tnc_mutex);
	return 0;

out_dump:
	if (znode->parent)
		zbr = &znode->parent->zbranch[znode->iip];
	else
		zbr = &c->zroot;
	ubifs_msg("dump of znode at LEB %d:%d", zbr->lnum, zbr->offs);
	dbg_dump_znode(c, znode);
out_unlock:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * add_size - add znode size to partially calculated index size.
 * @c: UBIFS file-system description object
 * @znode: znode to add size for
 * @priv: partially calculated index size
 *
 * This is a helper function for 'dbg_check_idx_size()' which is called for
 * every indexing node and adds its size to the 'long long' variable pointed to
 * by @priv.
 */
static int add_size(struct ubifs_info *c, struct ubifs_znode *znode, void *priv)
{
	long long *idx_size = priv;
	int add;

	add = ubifs_idx_node_sz(c, znode->child_cnt);
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
		ubifs_err("error %d while walking the index", err);
		return err;
	}

	if (calc != idx_size) {
		ubifs_err("index size check failed: calculated size is %lld, "
			  "should be %lld", calc, idx_size);
		dump_stack();
		return -EINVAL;
	}

	return 0;
}

/**
 * struct fsck_inode - information about an inode used when checking the file-system.
 * @rb: link in the RB-tree of inodes
 * @inum: inode number
 * @mode: inode type, permissions, etc
 * @nlink: inode link count
 * @xattr_cnt: count of extended attributes
 * @references: how many directory/xattr entries refer this inode (calculated
 *              while walking the index)
 * @calc_cnt: for directory inode count of child directories
 * @size: inode size (read from on-flash inode)
 * @xattr_sz: summary size of all extended attributes (read from on-flash
 *            inode)
 * @calc_sz: for directories calculated directory size
 * @calc_xcnt: count of extended attributes
 * @calc_xsz: calculated summary size of all extended attributes
 * @xattr_nms: sum of lengths of all extended attribute names belonging to this
 *             inode (read from on-flash inode)
 * @calc_xnms: calculated sum of lengths of all extended attribute names
 */
struct fsck_inode {
	struct rb_node rb;
	ino_t inum;
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
 * @inodes: RB-tree of all inodes (contains @struct fsck_inode objects)
 */
struct fsck_data {
	struct rb_root inodes;
};

/**
 * add_inode - add inode information to RB-tree of inodes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 * @ino: raw UBIFS inode to add
 *
 * This is a helper function for 'check_leaf()' which adds information about
 * inode @ino to the RB-tree of inodes. Returns inode information pointer in
 * case of success and a negative error code in case of failure.
 */
static struct fsck_inode *add_inode(struct ubifs_info *c,
				    struct fsck_data *fsckd,
				    struct ubifs_ino_node *ino)
{
	struct rb_node **p, *parent = NULL;
	struct fsck_inode *fscki;
	ino_t inum = key_inum_flash(c, &ino->key);
	struct inode *inode;
	struct ubifs_inode *ui;

	p = &fsckd->inodes.rb_node;
	while (*p) {
		parent = *p;
		fscki = rb_entry(parent, struct fsck_inode, rb);
		if (inum < fscki->inum)
			p = &(*p)->rb_left;
		else if (inum > fscki->inum)
			p = &(*p)->rb_right;
		else
			return fscki;
	}

	if (inum > c->highest_inum) {
		ubifs_err("too high inode number, max. is %lu",
			  (unsigned long)c->highest_inum);
		return ERR_PTR(-EINVAL);
	}

	fscki = kzalloc(sizeof(struct fsck_inode), GFP_NOFS);
	if (!fscki)
		return ERR_PTR(-ENOMEM);

	inode = ilookup(c->vfs_sb, inum);

	fscki->inum = inum;
	/*
	 * If the inode is present in the VFS inode cache, use it instead of
	 * the on-flash inode which might be out-of-date. E.g., the size might
	 * be out-of-date. If we do not do this, the following may happen, for
	 * example:
	 *   1. A power cut happens
	 *   2. We mount the file-system R/O, the replay process fixes up the
	 *      inode size in the VFS cache, but on on-flash.
	 *   3. 'check_leaf()' fails because it hits a data node beyond inode
	 *      size.
	 */
	if (!inode) {
		fscki->nlink = le32_to_cpu(ino->nlink);
		fscki->size = le64_to_cpu(ino->size);
		fscki->xattr_cnt = le32_to_cpu(ino->xattr_cnt);
		fscki->xattr_sz = le32_to_cpu(ino->xattr_size);
		fscki->xattr_nms = le32_to_cpu(ino->xattr_names);
		fscki->mode = le32_to_cpu(ino->mode);
	} else {
		ui = ubifs_inode(inode);
		fscki->nlink = inode->i_nlink;
		fscki->size = inode->i_size;
		fscki->xattr_cnt = ui->xattr_cnt;
		fscki->xattr_sz = ui->xattr_size;
		fscki->xattr_nms = ui->xattr_names;
		fscki->mode = inode->i_mode;
		iput(inode);
	}

	if (S_ISDIR(fscki->mode)) {
		fscki->calc_sz = UBIFS_INO_NODE_SZ;
		fscki->calc_cnt = 2;
	}

	rb_link_node(&fscki->rb, parent, p);
	rb_insert_color(&fscki->rb, &fsckd->inodes);

	return fscki;
}

/**
 * search_inode - search inode in the RB-tree of inodes.
 * @fsckd: FS checking information
 * @inum: inode number to search
 *
 * This is a helper function for 'check_leaf()' which searches inode @inum in
 * the RB-tree of inodes and returns an inode information pointer or %NULL if
 * the inode was not found.
 */
static struct fsck_inode *search_inode(struct fsck_data *fsckd, ino_t inum)
{
	struct rb_node *p;
	struct fsck_inode *fscki;

	p = fsckd->inodes.rb_node;
	while (p) {
		fscki = rb_entry(p, struct fsck_inode, rb);
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
 * read_add_inode - read inode node and add it to RB-tree of inodes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 * @inum: inode number to read
 *
 * This is a helper function for 'check_leaf()' which finds inode node @inum in
 * the index, reads it, and adds it to the RB-tree of inodes. Returns inode
 * information pointer in case of success and a negative error code in case of
 * failure.
 */
static struct fsck_inode *read_add_inode(struct ubifs_info *c,
					 struct fsck_data *fsckd, ino_t inum)
{
	int n, err;
	union ubifs_key key;
	struct ubifs_znode *znode;
	struct ubifs_zbranch *zbr;
	struct ubifs_ino_node *ino;
	struct fsck_inode *fscki;

	fscki = search_inode(fsckd, inum);
	if (fscki)
		return fscki;

	ino_key_init(c, &key, inum);
	err = ubifs_lookup_level0(c, &key, &znode, &n);
	if (!err) {
		ubifs_err("inode %lu not found in index", (unsigned long)inum);
		return ERR_PTR(-ENOENT);
	} else if (err < 0) {
		ubifs_err("error %d while looking up inode %lu",
			  err, (unsigned long)inum);
		return ERR_PTR(err);
	}

	zbr = &znode->zbranch[n];
	if (zbr->len < UBIFS_INO_NODE_SZ) {
		ubifs_err("bad node %lu node length %d",
			  (unsigned long)inum, zbr->len);
		return ERR_PTR(-EINVAL);
	}

	ino = kmalloc(zbr->len, GFP_NOFS);
	if (!ino)
		return ERR_PTR(-ENOMEM);

	err = ubifs_tnc_read_node(c, zbr, ino);
	if (err) {
		ubifs_err("cannot read inode node at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		kfree(ino);
		return ERR_PTR(err);
	}

	fscki = add_inode(c, fsckd, ino);
	kfree(ino);
	if (IS_ERR(fscki)) {
		ubifs_err("error %ld while adding inode %lu node",
			  PTR_ERR(fscki), (unsigned long)inum);
		return fscki;
	}

	return fscki;
}

/**
 * check_leaf - check leaf node.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of the leaf node to check
 * @priv: FS checking information
 *
 * This is a helper function for 'dbg_check_filesystem()' which is called for
 * every single leaf node while walking the indexing tree. It checks that the
 * leaf node referred from the indexing tree exists, has correct CRC, and does
 * some other basic validation. This function is also responsible for building
 * an RB-tree of inodes - it adds all inodes into the RB-tree. It also
 * calculates reference count, size, etc for each inode in order to later
 * compare them to the information stored inside the inodes and detect possible
 * inconsistencies. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int check_leaf(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		      void *priv)
{
	ino_t inum;
	void *node;
	struct ubifs_ch *ch;
	int err, type = key_type(c, &zbr->key);
	struct fsck_inode *fscki;

	if (zbr->len < UBIFS_CH_SZ) {
		ubifs_err("bad leaf length %d (LEB %d:%d)",
			  zbr->len, zbr->lnum, zbr->offs);
		return -EINVAL;
	}

	node = kmalloc(zbr->len, GFP_NOFS);
	if (!node)
		return -ENOMEM;

	err = ubifs_tnc_read_node(c, zbr, node);
	if (err) {
		ubifs_err("cannot read leaf node at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		goto out_free;
	}

	/* If this is an inode node, add it to RB-tree of inodes */
	if (type == UBIFS_INO_KEY) {
		fscki = add_inode(c, priv, node);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err("error %d while adding inode node", err);
			goto out_dump;
		}
		goto out;
	}

	if (type != UBIFS_DENT_KEY && type != UBIFS_XENT_KEY &&
	    type != UBIFS_DATA_KEY) {
		ubifs_err("unexpected node type %d at LEB %d:%d",
			  type, zbr->lnum, zbr->offs);
		err = -EINVAL;
		goto out_free;
	}

	ch = node;
	if (le64_to_cpu(ch->sqnum) > c->max_sqnum) {
		ubifs_err("too high sequence number, max. is %llu",
			  c->max_sqnum);
		err = -EINVAL;
		goto out_dump;
	}

	if (type == UBIFS_DATA_KEY) {
		long long blk_offs;
		struct ubifs_data_node *dn = node;

		/*
		 * Search the inode node this data node belongs to and insert
		 * it to the RB-tree of inodes.
		 */
		inum = key_inum_flash(c, &dn->key);
		fscki = read_add_inode(c, priv, inum);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err("error %d while processing data node and "
				  "trying to find inode node %lu",
				  err, (unsigned long)inum);
			goto out_dump;
		}

		/* Make sure the data node is within inode size */
		blk_offs = key_block_flash(c, &dn->key);
		blk_offs <<= UBIFS_BLOCK_SHIFT;
		blk_offs += le32_to_cpu(dn->size);
		if (blk_offs > fscki->size) {
			ubifs_err("data node at LEB %d:%d is not within inode "
				  "size %lld", zbr->lnum, zbr->offs,
				  fscki->size);
			err = -EINVAL;
			goto out_dump;
		}
	} else {
		int nlen;
		struct ubifs_dent_node *dent = node;
		struct fsck_inode *fscki1;

		err = ubifs_validate_entry(c, dent);
		if (err)
			goto out_dump;

		/*
		 * Search the inode node this entry refers to and the parent
		 * inode node and insert them to the RB-tree of inodes.
		 */
		inum = le64_to_cpu(dent->inum);
		fscki = read_add_inode(c, priv, inum);
		if (IS_ERR(fscki)) {
			err = PTR_ERR(fscki);
			ubifs_err("error %d while processing entry node and "
				  "trying to find inode node %lu",
				  err, (unsigned long)inum);
			goto out_dump;
		}

		/* Count how many direntries or xentries refers this inode */
		fscki->references += 1;

		inum = key_inum_flash(c, &dent->key);
		fscki1 = read_add_inode(c, priv, inum);
		if (IS_ERR(fscki1)) {
			err = PTR_ERR(fscki1);
			ubifs_err("error %d while processing entry node and "
				  "trying to find parent inode node %lu",
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
	kfree(node);
	return 0;

out_dump:
	ubifs_msg("dump of node at LEB %d:%d", zbr->lnum, zbr->offs);
	dbg_dump_node(c, node);
out_free:
	kfree(node);
	return err;
}

/**
 * free_inodes - free RB-tree of inodes.
 * @fsckd: FS checking information
 */
static void free_inodes(struct fsck_data *fsckd)
{
	struct rb_node *this = fsckd->inodes.rb_node;
	struct fsck_inode *fscki;

	while (this) {
		if (this->rb_left)
			this = this->rb_left;
		else if (this->rb_right)
			this = this->rb_right;
		else {
			fscki = rb_entry(this, struct fsck_inode, rb);
			this = rb_parent(this);
			if (this) {
				if (this->rb_left == &fscki->rb)
					this->rb_left = NULL;
				else
					this->rb_right = NULL;
			}
			kfree(fscki);
		}
	}
}

/**
 * check_inodes - checks all inodes.
 * @c: UBIFS file-system description object
 * @fsckd: FS checking information
 *
 * This is a helper function for 'dbg_check_filesystem()' which walks the
 * RB-tree of inodes after the index scan has been finished, and checks that
 * inode nlink, size, etc are correct. Returns zero if inodes are fine,
 * %-EINVAL if not, and a negative error code in case of failure.
 */
static int check_inodes(struct ubifs_info *c, struct fsck_data *fsckd)
{
	int n, err;
	union ubifs_key key;
	struct ubifs_znode *znode;
	struct ubifs_zbranch *zbr;
	struct ubifs_ino_node *ino;
	struct fsck_inode *fscki;
	struct rb_node *this = rb_first(&fsckd->inodes);

	while (this) {
		fscki = rb_entry(this, struct fsck_inode, rb);
		this = rb_next(this);

		if (S_ISDIR(fscki->mode)) {
			/*
			 * Directories have to have exactly one reference (they
			 * cannot have hardlinks), although root inode is an
			 * exception.
			 */
			if (fscki->inum != UBIFS_ROOT_INO &&
			    fscki->references != 1) {
				ubifs_err("directory inode %lu has %d "
					  "direntries which refer it, but "
					  "should be 1",
					  (unsigned long)fscki->inum,
					  fscki->references);
				goto out_dump;
			}
			if (fscki->inum == UBIFS_ROOT_INO &&
			    fscki->references != 0) {
				ubifs_err("root inode %lu has non-zero (%d) "
					  "direntries which refer it",
					  (unsigned long)fscki->inum,
					  fscki->references);
				goto out_dump;
			}
			if (fscki->calc_sz != fscki->size) {
				ubifs_err("directory inode %lu size is %lld, "
					  "but calculated size is %lld",
					  (unsigned long)fscki->inum,
					  fscki->size, fscki->calc_sz);
				goto out_dump;
			}
			if (fscki->calc_cnt != fscki->nlink) {
				ubifs_err("directory inode %lu nlink is %d, "
					  "but calculated nlink is %d",
					  (unsigned long)fscki->inum,
					  fscki->nlink, fscki->calc_cnt);
				goto out_dump;
			}
		} else {
			if (fscki->references != fscki->nlink) {
				ubifs_err("inode %lu nlink is %d, but "
					  "calculated nlink is %d",
					  (unsigned long)fscki->inum,
					  fscki->nlink, fscki->references);
				goto out_dump;
			}
		}
		if (fscki->xattr_sz != fscki->calc_xsz) {
			ubifs_err("inode %lu has xattr size %u, but "
				  "calculated size is %lld",
				  (unsigned long)fscki->inum, fscki->xattr_sz,
				  fscki->calc_xsz);
			goto out_dump;
		}
		if (fscki->xattr_cnt != fscki->calc_xcnt) {
			ubifs_err("inode %lu has %u xattrs, but "
				  "calculated count is %lld",
				  (unsigned long)fscki->inum,
				  fscki->xattr_cnt, fscki->calc_xcnt);
			goto out_dump;
		}
		if (fscki->xattr_nms != fscki->calc_xnms) {
			ubifs_err("inode %lu has xattr names' size %u, but "
				  "calculated names' size is %lld",
				  (unsigned long)fscki->inum, fscki->xattr_nms,
				  fscki->calc_xnms);
			goto out_dump;
		}
	}

	return 0;

out_dump:
	/* Read the bad inode and dump it */
	ino_key_init(c, &key, fscki->inum);
	err = ubifs_lookup_level0(c, &key, &znode, &n);
	if (!err) {
		ubifs_err("inode %lu not found in index",
			  (unsigned long)fscki->inum);
		return -ENOENT;
	} else if (err < 0) {
		ubifs_err("error %d while looking up inode %lu",
			  err, (unsigned long)fscki->inum);
		return err;
	}

	zbr = &znode->zbranch[n];
	ino = kmalloc(zbr->len, GFP_NOFS);
	if (!ino)
		return -ENOMEM;

	err = ubifs_tnc_read_node(c, zbr, ino);
	if (err) {
		ubifs_err("cannot read inode node at LEB %d:%d, error %d",
			  zbr->lnum, zbr->offs, err);
		kfree(ino);
		return err;
	}

	ubifs_msg("dump of the inode %lu sitting in LEB %d:%d",
		  (unsigned long)fscki->inum, zbr->lnum, zbr->offs);
	dbg_dump_node(c, ino);
	kfree(ino);
	return -EINVAL;
}

/**
 * dbg_check_filesystem - check the file-system.
 * @c: UBIFS file-system description object
 *
 * This function checks the file system, namely:
 * o makes sure that all leaf nodes exist and their CRCs are correct;
 * o makes sure inode nlink, size, xattr size/count are correct (for all
 *   inodes).
 *
 * The function reads whole indexing tree and all nodes, so it is pretty
 * heavy-weight. Returns zero if the file-system is consistent, %-EINVAL if
 * not, and a negative error code in case of failure.
 */
int dbg_check_filesystem(struct ubifs_info *c)
{
	int err;
	struct fsck_data fsckd;

	if (!dbg_is_chk_fs(c))
		return 0;

	fsckd.inodes = RB_ROOT;
	err = dbg_walk_index(c, check_leaf, NULL, &fsckd);
	if (err)
		goto out_free;

	err = check_inodes(c, &fsckd);
	if (err)
		goto out_free;

	free_inodes(&fsckd);
	return 0;

out_free:
	ubifs_err("file-system check failed with error %d", err);
	dump_stack();
	free_inodes(&fsckd);
	return err;
}

/**
 * dbg_check_data_nodes_order - check that list of data nodes is sorted.
 * @c: UBIFS file-system description object
 * @head: the list of nodes ('struct ubifs_scan_node' objects)
 *
 * This function returns zero if the list of data nodes is sorted correctly,
 * and %-EINVAL if not.
 */
int dbg_check_data_nodes_order(struct ubifs_info *c, struct list_head *head)
{
	struct list_head *cur;
	struct ubifs_scan_node *sa, *sb;

	if (!dbg_is_chk_gen(c))
		return 0;

	for (cur = head->next; cur->next != head; cur = cur->next) {
		ino_t inuma, inumb;
		uint32_t blka, blkb;

		cond_resched();
		sa = container_of(cur, struct ubifs_scan_node, list);
		sb = container_of(cur->next, struct ubifs_scan_node, list);

		if (sa->type != UBIFS_DATA_NODE) {
			ubifs_err("bad node type %d", sa->type);
			dbg_dump_node(c, sa->node);
			return -EINVAL;
		}
		if (sb->type != UBIFS_DATA_NODE) {
			ubifs_err("bad node type %d", sb->type);
			dbg_dump_node(c, sb->node);
			return -EINVAL;
		}

		inuma = key_inum(c, &sa->key);
		inumb = key_inum(c, &sb->key);

		if (inuma < inumb)
			continue;
		if (inuma > inumb) {
			ubifs_err("larger inum %lu goes before inum %lu",
				  (unsigned long)inuma, (unsigned long)inumb);
			goto error_dump;
		}

		blka = key_block(c, &sa->key);
		blkb = key_block(c, &sb->key);

		if (blka > blkb) {
			ubifs_err("larger block %u goes before %u", blka, blkb);
			goto error_dump;
		}
		if (blka == blkb) {
			ubifs_err("two data nodes for the same block");
			goto error_dump;
		}
	}

	return 0;

error_dump:
	dbg_dump_node(c, sa->node);
	dbg_dump_node(c, sb->node);
	return -EINVAL;
}

/**
 * dbg_check_nondata_nodes_order - check that list of data nodes is sorted.
 * @c: UBIFS file-system description object
 * @head: the list of nodes ('struct ubifs_scan_node' objects)
 *
 * This function returns zero if the list of non-data nodes is sorted correctly,
 * and %-EINVAL if not.
 */
int dbg_check_nondata_nodes_order(struct ubifs_info *c, struct list_head *head)
{
	struct list_head *cur;
	struct ubifs_scan_node *sa, *sb;

	if (!dbg_is_chk_gen(c))
		return 0;

	for (cur = head->next; cur->next != head; cur = cur->next) {
		ino_t inuma, inumb;
		uint32_t hasha, hashb;

		cond_resched();
		sa = container_of(cur, struct ubifs_scan_node, list);
		sb = container_of(cur->next, struct ubifs_scan_node, list);

		if (sa->type != UBIFS_INO_NODE && sa->type != UBIFS_DENT_NODE &&
		    sa->type != UBIFS_XENT_NODE) {
			ubifs_err("bad node type %d", sa->type);
			dbg_dump_node(c, sa->node);
			return -EINVAL;
		}
		if (sa->type != UBIFS_INO_NODE && sa->type != UBIFS_DENT_NODE &&
		    sa->type != UBIFS_XENT_NODE) {
			ubifs_err("bad node type %d", sb->type);
			dbg_dump_node(c, sb->node);
			return -EINVAL;
		}

		if (sa->type != UBIFS_INO_NODE && sb->type == UBIFS_INO_NODE) {
			ubifs_err("non-inode node goes before inode node");
			goto error_dump;
		}

		if (sa->type == UBIFS_INO_NODE && sb->type != UBIFS_INO_NODE)
			continue;

		if (sa->type == UBIFS_INO_NODE && sb->type == UBIFS_INO_NODE) {
			/* Inode nodes are sorted in descending size order */
			if (sa->len < sb->len) {
				ubifs_err("smaller inode node goes first");
				goto error_dump;
			}
			continue;
		}

		/*
		 * This is either a dentry or xentry, which should be sorted in
		 * ascending (parent ino, hash) order.
		 */
		inuma = key_inum(c, &sa->key);
		inumb = key_inum(c, &sb->key);

		if (inuma < inumb)
			continue;
		if (inuma > inumb) {
			ubifs_err("larger inum %lu goes before inum %lu",
				  (unsigned long)inuma, (unsigned long)inumb);
			goto error_dump;
		}

		hasha = key_block(c, &sa->key);
		hashb = key_block(c, &sb->key);

		if (hasha > hashb) {
			ubifs_err("larger hash %u goes before %u",
				  hasha, hashb);
			goto error_dump;
		}
	}

	return 0;

error_dump:
	ubifs_msg("dumping first node");
	dbg_dump_node(c, sa->node);
	ubifs_msg("dumping second node");
	dbg_dump_node(c, sb->node);
	return -EINVAL;
	return 0;
}

static inline int chance(unsigned int n, unsigned int out_of)
{
	return !!((random32() % out_of) + 1 <= n);

}

static int power_cut_emulated(struct ubifs_info *c, int lnum, int write)
{
	struct ubifs_debug_info *d = c->dbg;

	ubifs_assert(dbg_is_tst_rcvry(c));

	if (!d->pc_cnt) {
		/* First call - decide delay to the power cut */
		if (chance(1, 2)) {
			unsigned long delay;

			if (chance(1, 2)) {
				d->pc_delay = 1;
				/* Fail withing 1 minute */
				delay = random32() % 60000;
				d->pc_timeout = jiffies;
				d->pc_timeout += msecs_to_jiffies(delay);
				ubifs_warn("failing after %lums", delay);
			} else {
				d->pc_delay = 2;
				delay = random32() % 10000;
				/* Fail within 10000 operations */
				d->pc_cnt_max = delay;
				ubifs_warn("failing after %lu calls", delay);
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
		ubifs_warn("failing in super block LEB %d", lnum);
	} else if (lnum == UBIFS_MST_LNUM || lnum == UBIFS_MST_LNUM + 1) {
		if (chance(19, 20))
			return 0;
		ubifs_warn("failing in master LEB %d", lnum);
	} else if (lnum >= UBIFS_LOG_LNUM && lnum <= c->log_last) {
		if (write && chance(99, 100))
			return 0;
		if (chance(399, 400))
			return 0;
		ubifs_warn("failing in log LEB %d", lnum);
	} else if (lnum >= c->lpt_first && lnum <= c->lpt_last) {
		if (write && chance(7, 8))
			return 0;
		if (chance(19, 20))
			return 0;
		ubifs_warn("failing in LPT LEB %d", lnum);
	} else if (lnum >= c->orph_first && lnum <= c->orph_last) {
		if (write && chance(1, 2))
			return 0;
		if (chance(9, 10))
			return 0;
		ubifs_warn("failing in orphan LEB %d", lnum);
	} else if (lnum == c->ihead_lnum) {
		if (chance(99, 100))
			return 0;
		ubifs_warn("failing in index head LEB %d", lnum);
	} else if (c->jheads && lnum == c->jheads[GCHD].wbuf.lnum) {
		if (chance(9, 10))
			return 0;
		ubifs_warn("failing in GC head LEB %d", lnum);
	} else if (write && !RB_EMPTY_ROOT(&c->buds) &&
		   !ubifs_search_bud(c, lnum)) {
		if (chance(19, 20))
			return 0;
		ubifs_warn("failing in non-bud LEB %d", lnum);
	} else if (c->cmt_state == COMMIT_RUNNING_BACKGROUND ||
		   c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		if (chance(999, 1000))
			return 0;
		ubifs_warn("failing in bud LEB %d commit running", lnum);
	} else {
		if (chance(9999, 10000))
			return 0;
		ubifs_warn("failing in bud LEB %d commit not running", lnum);
	}

	d->pc_happened = 1;
	ubifs_warn("========== Power cut emulated ==========");
	dump_stack();
	return 1;
}

static void cut_data(const void *buf, unsigned int len)
{
	unsigned int from, to, i, ffs = chance(1, 2);
	unsigned char *p = (void *)buf;

	from = random32() % (len + 1);
	if (chance(1, 2))
		to = random32() % (len - from + 1);
	else
		to = len;

	if (from < to)
		ubifs_warn("filled bytes %u-%u with %s", from, to - 1,
			   ffs ? "0xFFs" : "random data");

	if (ffs)
		for (i = from; i < to; i++)
			p[i] = 0xFF;
	else
		for (i = from; i < to; i++)
			p[i] = random32() % 0x100;
}

int dbg_leb_write(struct ubifs_info *c, int lnum, const void *buf,
		  int offs, int len, int dtype)
{
	int err, failing;

	if (c->dbg->pc_happened)
		return -EROFS;

	failing = power_cut_emulated(c, lnum, 1);
	if (failing)
		cut_data(buf, len);
	err = ubi_leb_write(c->ubi, lnum, buf, offs, len, dtype);
	if (err)
		return err;
	if (failing)
		return -EROFS;
	return 0;
}

int dbg_leb_change(struct ubifs_info *c, int lnum, const void *buf,
		   int len, int dtype)
{
	int err;

	if (c->dbg->pc_happened)
		return -EROFS;
	if (power_cut_emulated(c, lnum, 1))
		return -EROFS;
	err = ubi_leb_change(c->ubi, lnum, buf, len, dtype);
	if (err)
		return err;
	if (power_cut_emulated(c, lnum, 1))
		return -EROFS;
	return 0;
}

int dbg_leb_unmap(struct ubifs_info *c, int lnum)
{
	int err;

	if (c->dbg->pc_happened)
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

int dbg_leb_map(struct ubifs_info *c, int lnum, int dtype)
{
	int err;

	if (c->dbg->pc_happened)
		return -EROFS;
	if (power_cut_emulated(c, lnum, 0))
		return -EROFS;
	err = ubi_leb_map(c->ubi, lnum, dtype);
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

static int dfs_file_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return nonseekable_open(inode, file);
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

	/*
	 * TODO: this is racy - the file-system might have already been
	 * unmounted and we'd oops in this case. The plan is to fix it with
	 * help of 'iterate_supers_type()' which we should have in v3.0: when
	 * a debugfs opened, we rember FS's UUID in file->private_data. Then
	 * whenever we access the FS via a debugfs file, we iterate all UBIFS
	 * superblocks and fine the one with the same UUID, and take the
	 * locking right.
	 *
	 * The other way to go suggested by Al Viro is to create a separate
	 * 'ubifs-debug' file-system instead.
	 */
	if (file->f_path.dentry == d->dfs_dump_lprops) {
		dbg_dump_lprops(c);
		return count;
	}
	if (file->f_path.dentry == d->dfs_dump_budg) {
		dbg_dump_budg(c, &c->bi);
		return count;
	}
	if (file->f_path.dentry == d->dfs_dump_tnc) {
		mutex_lock(&c->tnc_mutex);
		dbg_dump_tnc(c);
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
	else
		return -EINVAL;

	return count;
}

static const struct file_operations dfs_fops = {
	.open = dfs_file_open,
	.read = dfs_file_read,
	.write = dfs_file_write,
	.owner = THIS_MODULE,
	.llseek = no_llseek,
};

/**
 * dbg_debugfs_init_fs - initialize debugfs for UBIFS instance.
 * @c: UBIFS file-system description object
 *
 * This function creates all debugfs files for this instance of UBIFS. Returns
 * zero in case of success and a negative error code in case of failure.
 *
 * Note, the only reason we have not merged this function with the
 * 'ubifs_debugging_init()' function is because it is better to initialize
 * debugfs interfaces at the very end of the mount process, and remove them at
 * the very beginning of the mount process.
 */
int dbg_debugfs_init_fs(struct ubifs_info *c)
{
	int err, n;
	const char *fname;
	struct dentry *dent;
	struct ubifs_debug_info *d = c->dbg;

	n = snprintf(d->dfs_dir_name, UBIFS_DFS_DIR_LEN + 1, UBIFS_DFS_DIR_NAME,
		     c->vi.ubi_num, c->vi.vol_id);
	if (n == UBIFS_DFS_DIR_LEN) {
		/* The array size is too small */
		fname = UBIFS_DFS_DIR_NAME;
		dent = ERR_PTR(-EINVAL);
		goto out;
	}

	fname = d->dfs_dir_name;
	dent = debugfs_create_dir(fname, dfs_rootdir);
	if (IS_ERR_OR_NULL(dent))
		goto out;
	d->dfs_dir = dent;

	fname = "dump_lprops";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c, &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_dump_lprops = dent;

	fname = "dump_budg";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c, &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_dump_budg = dent;

	fname = "dump_tnc";
	dent = debugfs_create_file(fname, S_IWUSR, d->dfs_dir, c, &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_dump_tnc = dent;

	fname = "chk_general";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_gen = dent;

	fname = "chk_index";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_index = dent;

	fname = "chk_orphans";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_orph = dent;

	fname = "chk_lprops";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_lprops = dent;

	fname = "chk_fs";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_chk_fs = dent;

	fname = "tst_recovery";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, d->dfs_dir, c,
				   &dfs_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	d->dfs_tst_rcvry = dent;

	return 0;

out_remove:
	debugfs_remove_recursive(d->dfs_dir);
out:
	err = dent ? PTR_ERR(dent) : -ENODEV;
	ubifs_err("cannot create \"%s\" debugfs file or directory, error %d\n",
		  fname, err);
	return err;
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
	.llseek = no_llseek,
};

/**
 * dbg_debugfs_init - initialize debugfs file-system.
 *
 * UBIFS uses debugfs file-system to expose various debugging knobs to
 * user-space. This function creates "ubifs" directory in the debugfs
 * file-system. Returns zero in case of success and a negative error code in
 * case of failure.
 */
int dbg_debugfs_init(void)
{
	int err;
	const char *fname;
	struct dentry *dent;

	fname = "ubifs";
	dent = debugfs_create_dir(fname, NULL);
	if (IS_ERR_OR_NULL(dent))
		goto out;
	dfs_rootdir = dent;

	fname = "chk_general";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_chk_gen = dent;

	fname = "chk_index";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_chk_index = dent;

	fname = "chk_orphans";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_chk_orph = dent;

	fname = "chk_lprops";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_chk_lprops = dent;

	fname = "chk_fs";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_chk_fs = dent;

	fname = "tst_recovery";
	dent = debugfs_create_file(fname, S_IRUSR | S_IWUSR, dfs_rootdir, NULL,
				   &dfs_global_fops);
	if (IS_ERR_OR_NULL(dent))
		goto out_remove;
	dfs_tst_rcvry = dent;

	return 0;

out_remove:
	debugfs_remove_recursive(dfs_rootdir);
out:
	err = dent ? PTR_ERR(dent) : -ENODEV;
	ubifs_err("cannot create \"%s\" debugfs file or directory, error %d\n",
		  fname, err);
	return err;
}

/**
 * dbg_debugfs_exit - remove the "ubifs" directory from debugfs file-system.
 */
void dbg_debugfs_exit(void)
{
	debugfs_remove_recursive(dfs_rootdir);
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
		return -ENOMEM;

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

#endif /* CONFIG_UBIFS_FS_DEBUG */
