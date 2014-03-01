/* -*- linux-c -*- */

/* fs/reiserfs/procfs.c */

/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

/* proc info support a la one created by Sizif@Botik.RU for PGC */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include "reiserfs.h"
#include <linux/init.h>
#include <linux/proc_fs.h>

/*
 * LOCKING:
 *
 * These guys are evicted from procfs as the very first step in ->kill_sb().
 *
 */

static int show_version(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	char *format;

	if (REISERFS_SB(sb)->s_properties & (1 << REISERFS_3_6)) {
		format = "3.6";
	} else if (REISERFS_SB(sb)->s_properties & (1 << REISERFS_3_5)) {
		format = "3.5";
	} else {
		format = "unknown";
	}

	seq_printf(m, "%s format\twith checks %s\n", format,
#if defined( CONFIG_REISERFS_CHECK )
		   "on"
#else
		   "off"
#endif
	    );
	return 0;
}

#define SF( x ) ( r -> x )
#define SFP( x ) SF( s_proc_info_data.x )
#define SFPL( x ) SFP( x[ level ] )
#define SFPF( x ) SFP( scan_bitmap.x )
#define SFPJ( x ) SFP( journal.x )

#define D2C( x ) le16_to_cpu( x )
#define D4C( x ) le32_to_cpu( x )
#define DF( x ) D2C( rs -> s_v1.x )
#define DFL( x ) D4C( rs -> s_v1.x )

#define objectid_map( s, rs ) (old_format_only (s) ?				\
                         (__le32 *)((struct reiserfs_super_block_v1 *)rs + 1) :	\
			 (__le32 *)(rs + 1))
#define MAP( i ) D4C( objectid_map( sb, rs )[ i ] )

#define DJF( x ) le32_to_cpu( rs -> x )
#define DJV( x ) le32_to_cpu( s_v1 -> x )
#define DJP( x ) le32_to_cpu( jp -> x )
#define JF( x ) ( r -> s_journal -> x )

static int show_super(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *r = REISERFS_SB(sb);

	seq_printf(m, "state: \t%s\n"
		   "mount options: \t%s%s%s%s%s%s%s%s%s%s%s\n"
		   "gen. counter: \t%i\n"
		   "s_disk_reads: \t%i\n"
		   "s_disk_writes: \t%i\n"
		   "s_fix_nodes: \t%i\n"
		   "s_do_balance: \t%i\n"
		   "s_unneeded_left_neighbor: \t%i\n"
		   "s_good_search_by_key_reada: \t%i\n"
		   "s_bmaps: \t%i\n"
		   "s_bmaps_without_search: \t%i\n"
		   "s_direct2indirect: \t%i\n"
		   "s_indirect2direct: \t%i\n"
		   "\n"
		   "max_hash_collisions: \t%i\n"
		   "breads: \t%lu\n"
		   "bread_misses: \t%lu\n"
		   "search_by_key: \t%lu\n"
		   "search_by_key_fs_changed: \t%lu\n"
		   "search_by_key_restarted: \t%lu\n"
		   "insert_item_restarted: \t%lu\n"
		   "paste_into_item_restarted: \t%lu\n"
		   "cut_from_item_restarted: \t%lu\n"
		   "delete_solid_item_restarted: \t%lu\n"
		   "delete_item_restarted: \t%lu\n"
		   "leaked_oid: \t%lu\n"
		   "leaves_removable: \t%lu\n",
		   SF(s_mount_state) == REISERFS_VALID_FS ?
		   "REISERFS_VALID_FS" : "REISERFS_ERROR_FS",
		   reiserfs_r5_hash(sb) ? "FORCE_R5 " : "",
		   reiserfs_rupasov_hash(sb) ? "FORCE_RUPASOV " : "",
		   reiserfs_tea_hash(sb) ? "FORCE_TEA " : "",
		   reiserfs_hash_detect(sb) ? "DETECT_HASH " : "",
		   reiserfs_no_border(sb) ? "NO_BORDER " : "BORDER ",
		   reiserfs_no_unhashed_relocation(sb) ?
		   "NO_UNHASHED_RELOCATION " : "",
		   reiserfs_hashed_relocation(sb) ? "UNHASHED_RELOCATION " : "",
		   reiserfs_test4(sb) ? "TEST4 " : "",
		   have_large_tails(sb) ? "TAILS " : have_small_tails(sb) ?
		   "SMALL_TAILS " : "NO_TAILS ",
		   replay_only(sb) ? "REPLAY_ONLY " : "",
		   convert_reiserfs(sb) ? "CONV " : "",
		   atomic_read(&r->s_generation_counter),
		   SF(s_disk_reads), SF(s_disk_writes), SF(s_fix_nodes),
		   SF(s_do_balance), SF(s_unneeded_left_neighbor),
		   SF(s_good_search_by_key_reada), SF(s_bmaps),
		   SF(s_bmaps_without_search), SF(s_direct2indirect),
		   SF(s_indirect2direct), SFP(max_hash_collisions), SFP(breads),
		   SFP(bread_miss), SFP(search_by_key),
		   SFP(search_by_key_fs_changed), SFP(search_by_key_restarted),
		   SFP(insert_item_restarted), SFP(paste_into_item_restarted),
		   SFP(cut_from_item_restarted),
		   SFP(delete_solid_item_restarted), SFP(delete_item_restarted),
		   SFP(leaked_oid), SFP(leaves_removable));

	return 0;
}

static int show_per_level(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *r = REISERFS_SB(sb);
	int level;

	seq_printf(m, "level\t"
		   "     balances"
		   " [sbk:  reads"
		   "   fs_changed"
		   "   restarted]"
		   "   free space"
		   "        items"
		   "   can_remove"
		   "         lnum"
		   "         rnum"
		   "       lbytes"
		   "       rbytes"
		   "     get_neig"
		   " get_neig_res" "  need_l_neig" "  need_r_neig" "\n");

	for (level = 0; level < MAX_HEIGHT; ++level) {
		seq_printf(m, "%i\t"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12li"
			   " %12li"
			   " %12li"
			   " %12li"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   " %12lu"
			   "\n",
			   level,
			   SFPL(balance_at),
			   SFPL(sbk_read_at),
			   SFPL(sbk_fs_changed),
			   SFPL(sbk_restarted),
			   SFPL(free_at),
			   SFPL(items_at),
			   SFPL(can_node_be_removed),
			   SFPL(lnum),
			   SFPL(rnum),
			   SFPL(lbytes),
			   SFPL(rbytes),
			   SFPL(get_neighbors),
			   SFPL(get_neighbors_restart),
			   SFPL(need_l_neighbor), SFPL(need_r_neighbor)
		    );
	}
	return 0;
}

static int show_bitmap(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *r = REISERFS_SB(sb);

	seq_printf(m, "free_block: %lu\n"
		   "  scan_bitmap:"
		   "          wait"
		   "          bmap"
		   "         retry"
		   "        stolen"
		   "  journal_hint"
		   "journal_nohint"
		   "\n"
		   " %14lu"
		   " %14lu"
		   " %14lu"
		   " %14lu"
		   " %14lu"
		   " %14lu"
		   " %14lu"
		   "\n",
		   SFP(free_block),
		   SFPF(call),
		   SFPF(wait),
		   SFPF(bmap),
		   SFPF(retry),
		   SFPF(stolen),
		   SFPF(in_journal_hint), SFPF(in_journal_nohint));

	return 0;
}

static int show_on_disk_super(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *sb_info = REISERFS_SB(sb);
	struct reiserfs_super_block *rs = sb_info->s_rs;
	int hash_code = DFL(s_hash_function_code);
	__u32 flags = DJF(s_flags);

	seq_printf(m, "block_count: \t%i\n"
		   "free_blocks: \t%i\n"
		   "root_block: \t%i\n"
		   "blocksize: \t%i\n"
		   "oid_maxsize: \t%i\n"
		   "oid_cursize: \t%i\n"
		   "umount_state: \t%i\n"
		   "magic: \t%10.10s\n"
		   "fs_state: \t%i\n"
		   "hash: \t%s\n"
		   "tree_height: \t%i\n"
		   "bmap_nr: \t%i\n"
		   "version: \t%i\n"
		   "flags: \t%x[%s]\n"
		   "reserved_for_journal: \t%i\n",
		   DFL(s_block_count),
		   DFL(s_free_blocks),
		   DFL(s_root_block),
		   DF(s_blocksize),
		   DF(s_oid_maxsize),
		   DF(s_oid_cursize),
		   DF(s_umount_state),
		   rs->s_v1.s_magic,
		   DF(s_fs_state),
		   hash_code == TEA_HASH ? "tea" :
		   (hash_code == YURA_HASH) ? "rupasov" :
		   (hash_code == R5_HASH) ? "r5" :
		   (hash_code == UNSET_HASH) ? "unset" : "unknown",
		   DF(s_tree_height),
		   DF(s_bmap_nr),
		   DF(s_version), flags, (flags & reiserfs_attrs_cleared)
		   ? "attrs_cleared" : "", DF(s_reserved_for_journal));

	return 0;
}

static int show_oidmap(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *sb_info = REISERFS_SB(sb);
	struct reiserfs_super_block *rs = sb_info->s_rs;
	unsigned int mapsize = le16_to_cpu(rs->s_v1.s_oid_cursize);
	unsigned long total_used = 0;
	int i;

	for (i = 0; i < mapsize; ++i) {
		__u32 right;

		right = (i == mapsize - 1) ? MAX_KEY_OBJECTID : MAP(i + 1);
		seq_printf(m, "%s: [ %x .. %x )\n",
			   (i & 1) ? "free" : "used", MAP(i), right);
		if (!(i & 1)) {
			total_used += right - MAP(i);
		}
	}
#if defined( REISERFS_USE_OIDMAPF )
	if (sb_info->oidmap.use_file && (sb_info->oidmap.mapf != NULL)) {
		loff_t size = file_inode(sb_info->oidmap.mapf)->i_size;
		total_used += size / sizeof(reiserfs_oidinterval_d_t);
	}
#endif
	seq_printf(m, "total: \t%i [%i/%i] used: %lu [exact]\n",
		   mapsize,
		   mapsize, le16_to_cpu(rs->s_v1.s_oid_maxsize), total_used);
	return 0;
}

static int show_journal(struct seq_file *m, void *unused)
{
	struct super_block *sb = m->private;
	struct reiserfs_sb_info *r = REISERFS_SB(sb);
	struct reiserfs_super_block *rs = r->s_rs;
	struct journal_params *jp = &rs->s_v1.s_journal;
	char b[BDEVNAME_SIZE];

	seq_printf(m,		/* on-disk fields */
		   "jp_journal_1st_block: \t%i\n"
		   "jp_journal_dev: \t%s[%x]\n"
		   "jp_journal_size: \t%i\n"
		   "jp_journal_trans_max: \t%i\n"
		   "jp_journal_magic: \t%i\n"
		   "jp_journal_max_batch: \t%i\n"
		   "jp_journal_max_commit_age: \t%i\n"
		   "jp_journal_max_trans_age: \t%i\n"
		   /* incore fields */
		   "j_1st_reserved_block: \t%i\n"
		   "j_state: \t%li\n"
		   "j_trans_id: \t%u\n"
		   "j_mount_id: \t%lu\n"
		   "j_start: \t%lu\n"
		   "j_len: \t%lu\n"
		   "j_len_alloc: \t%lu\n"
		   "j_wcount: \t%i\n"
		   "j_bcount: \t%lu\n"
		   "j_first_unflushed_offset: \t%lu\n"
		   "j_last_flush_trans_id: \t%u\n"
		   "j_trans_start_time: \t%li\n"
		   "j_list_bitmap_index: \t%i\n"
		   "j_must_wait: \t%i\n"
		   "j_next_full_flush: \t%i\n"
		   "j_next_async_flush: \t%i\n"
		   "j_cnode_used: \t%i\n" "j_cnode_free: \t%i\n" "\n"
		   /* reiserfs_proc_info_data_t.journal fields */
		   "in_journal: \t%12lu\n"
		   "in_journal_bitmap: \t%12lu\n"
		   "in_journal_reusable: \t%12lu\n"
		   "lock_journal: \t%12lu\n"
		   "lock_journal_wait: \t%12lu\n"
		   "journal_begin: \t%12lu\n"
		   "journal_relock_writers: \t%12lu\n"
		   "journal_relock_wcount: \t%12lu\n"
		   "mark_dirty: \t%12lu\n"
		   "mark_dirty_already: \t%12lu\n"
		   "mark_dirty_notjournal: \t%12lu\n"
		   "restore_prepared: \t%12lu\n"
		   "prepare: \t%12lu\n"
		   "prepare_retry: \t%12lu\n",
		   DJP(jp_journal_1st_block),
		   bdevname(SB_JOURNAL(sb)->j_dev_bd, b),
		   DJP(jp_journal_dev),
		   DJP(jp_journal_size),
		   DJP(jp_journal_trans_max),
		   DJP(jp_journal_magic),
		   DJP(jp_journal_max_batch),
		   SB_JOURNAL(sb)->j_max_commit_age,
		   DJP(jp_journal_max_trans_age),
		   JF(j_1st_reserved_block),
		   JF(j_state),
		   JF(j_trans_id),
		   JF(j_mount_id),
		   JF(j_start),
		   JF(j_len),
		   JF(j_len_alloc),
		   atomic_read(&r->s_journal->j_wcount),
		   JF(j_bcount),
		   JF(j_first_unflushed_offset),
		   JF(j_last_flush_trans_id),
		   JF(j_trans_start_time),
		   JF(j_list_bitmap_index),
		   JF(j_must_wait),
		   JF(j_next_full_flush),
		   JF(j_next_async_flush),
		   JF(j_cnode_used),
		   JF(j_cnode_free),
		   SFPJ(in_journal),
		   SFPJ(in_journal_bitmap),
		   SFPJ(in_journal_reusable),
		   SFPJ(lock_journal),
		   SFPJ(lock_journal_wait),
		   SFPJ(journal_being),
		   SFPJ(journal_relock_writers),
		   SFPJ(journal_relock_wcount),
		   SFPJ(mark_dirty),
		   SFPJ(mark_dirty_already),
		   SFPJ(mark_dirty_notjournal),
		   SFPJ(restore_prepared), SFPJ(prepare), SFPJ(prepare_retry)
	    );
	return 0;
}

static int r_open(struct inode *inode, struct file *file)
{
	return single_open(file, PDE_DATA(inode), 
				proc_get_parent_data(inode));
}

static const struct file_operations r_file_operations = {
	.open = r_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct proc_dir_entry *proc_info_root = NULL;
static const char proc_info_root_name[] = "fs/reiserfs";

static void add_file(struct super_block *sb, char *name,
		     int (*func) (struct seq_file *, void *))
{
	proc_create_data(name, 0, REISERFS_SB(sb)->procdir,
			 &r_file_operations, func);
}

int reiserfs_proc_info_init(struct super_block *sb)
{
	char b[BDEVNAME_SIZE];
	char *s;

	/* Some block devices use /'s */
	strlcpy(b, sb->s_id, BDEVNAME_SIZE);
	s = strchr(b, '/');
	if (s)
		*s = '!';

	spin_lock_init(&__PINFO(sb).lock);
	REISERFS_SB(sb)->procdir = proc_mkdir_data(b, 0, proc_info_root, sb);
	if (REISERFS_SB(sb)->procdir) {
		add_file(sb, "version", show_version);
		add_file(sb, "super", show_super);
		add_file(sb, "per-level", show_per_level);
		add_file(sb, "bitmap", show_bitmap);
		add_file(sb, "on-disk-super", show_on_disk_super);
		add_file(sb, "oidmap", show_oidmap);
		add_file(sb, "journal", show_journal);
		return 0;
	}
	reiserfs_warning(sb, "cannot create /proc/%s/%s",
			 proc_info_root_name, b);
	return 1;
}

int reiserfs_proc_info_done(struct super_block *sb)
{
	struct proc_dir_entry *de = REISERFS_SB(sb)->procdir;
	if (de) {
		char b[BDEVNAME_SIZE];
		char *s;

		/* Some block devices use /'s */
		strlcpy(b, sb->s_id, BDEVNAME_SIZE);
		s = strchr(b, '/');
		if (s)
			*s = '!';

		remove_proc_subtree(b, proc_info_root);
		REISERFS_SB(sb)->procdir = NULL;
	}
	return 0;
}

int reiserfs_proc_info_global_init(void)
{
	if (proc_info_root == NULL) {
		proc_info_root = proc_mkdir(proc_info_root_name, NULL);
		if (!proc_info_root) {
			reiserfs_warning(NULL, "cannot create /proc/%s",
					 proc_info_root_name);
			return 1;
		}
	}
	return 0;
}

int reiserfs_proc_info_global_done(void)
{
	if (proc_info_root != NULL) {
		proc_info_root = NULL;
		remove_proc_entry(proc_info_root_name, NULL);
	}
	return 0;
}
/*
 * Revision 1.1.8.2  2001/07/15 17:08:42  god
 *  . use get_super() in procfs.c
 *  . remove remove_save_link() from reiserfs_do_truncate()
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 * Revision 1.1.8.1  2001/07/11 16:48:50  god
 * proc info support
 *
 * I accept terms and conditions stated in the Legal Agreement
 * (available at http://www.namesys.com/legalese.html)
 *
 */

/*
 * Make Linus happy.
 * Local variables:
 * c-indentation-style: "K&R"
 * mode-name: "LC"
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
