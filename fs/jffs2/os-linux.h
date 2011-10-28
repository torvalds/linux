/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#ifndef __JFFS2_OS_LINUX_H__
#define __JFFS2_OS_LINUX_H__

/* JFFS2 uses Linux mode bits natively -- no need for conversion */
#define os_to_jffs2_mode(x) (x)
#define jffs2_to_os_mode(x) (x)

struct kstatfs;
struct kvec;

#define JFFS2_INODE_INFO(i) (list_entry(i, struct jffs2_inode_info, vfs_inode))
#define OFNI_EDONI_2SFFJ(f)  (&(f)->vfs_inode)
#define JFFS2_SB_INFO(sb) (sb->s_fs_info)
#define OFNI_BS_2SFFJ(c)  ((struct super_block *)c->os_priv)


#define JFFS2_F_I_SIZE(f) (OFNI_EDONI_2SFFJ(f)->i_size)
#define JFFS2_F_I_MODE(f) (OFNI_EDONI_2SFFJ(f)->i_mode)
#define JFFS2_F_I_UID(f) (OFNI_EDONI_2SFFJ(f)->i_uid)
#define JFFS2_F_I_GID(f) (OFNI_EDONI_2SFFJ(f)->i_gid)
#define JFFS2_F_I_RDEV(f) (OFNI_EDONI_2SFFJ(f)->i_rdev)

#define ITIME(sec) ((struct timespec){sec, 0})
#define I_SEC(tv) ((tv).tv_sec)
#define JFFS2_F_I_CTIME(f) (OFNI_EDONI_2SFFJ(f)->i_ctime.tv_sec)
#define JFFS2_F_I_MTIME(f) (OFNI_EDONI_2SFFJ(f)->i_mtime.tv_sec)
#define JFFS2_F_I_ATIME(f) (OFNI_EDONI_2SFFJ(f)->i_atime.tv_sec)

#define sleep_on_spinunlock(wq, s)				\
	do {							\
		DECLARE_WAITQUEUE(__wait, current);		\
		add_wait_queue((wq), &__wait);			\
		set_current_state(TASK_UNINTERRUPTIBLE);	\
		spin_unlock(s);					\
		schedule();					\
		remove_wait_queue((wq), &__wait);		\
	} while(0)

static inline void jffs2_init_inode_info(struct jffs2_inode_info *f)
{
	f->highest_version = 0;
	f->fragtree = RB_ROOT;
	f->metadata = NULL;
	f->dents = NULL;
	f->target = NULL;
	f->flags = 0;
	f->usercompr = 0;
}


#define jffs2_is_readonly(c) (OFNI_BS_2SFFJ(c)->s_flags & MS_RDONLY)

#define SECTOR_ADDR(x) ( (((unsigned long)(x) / c->sector_size) * c->sector_size) )
#ifndef CONFIG_JFFS2_FS_WRITEBUFFER


#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (1)
#endif

#define jffs2_is_writebuffered(c) (0)
#define jffs2_cleanmarker_oob(c) (0)
#define jffs2_write_nand_cleanmarker(c,jeb) (-EIO)

#define jffs2_flash_write(c, ofs, len, retlen, buf) jffs2_flash_direct_write(c, ofs, len, retlen, buf)
#define jffs2_flash_read(c, ofs, len, retlen, buf) ((c)->mtd->read((c)->mtd, ofs, len, retlen, buf))
#define jffs2_flush_wbuf_pad(c) ({ do{} while(0); (void)(c), 0; })
#define jffs2_flush_wbuf_gc(c, i) ({ do{} while(0); (void)(c), (void) i, 0; })
#define jffs2_write_nand_badblock(c,jeb,bad_offset) (1)
#define jffs2_nand_flash_setup(c) (0)
#define jffs2_nand_flash_cleanup(c) do {} while(0)
#define jffs2_wbuf_dirty(c) (0)
#define jffs2_flash_writev(a,b,c,d,e,f) jffs2_flash_direct_writev(a,b,c,d,e)
#define jffs2_wbuf_timeout NULL
#define jffs2_wbuf_process NULL
#define jffs2_dataflash(c) (0)
#define jffs2_dataflash_setup(c) (0)
#define jffs2_dataflash_cleanup(c) do {} while (0)
#define jffs2_nor_wbuf_flash(c) (0)
#define jffs2_nor_wbuf_flash_setup(c) (0)
#define jffs2_nor_wbuf_flash_cleanup(c) do {} while (0)
#define jffs2_ubivol(c) (0)
#define jffs2_ubivol_setup(c) (0)
#define jffs2_ubivol_cleanup(c) do {} while (0)

#else /* NAND and/or ECC'd NOR support present */

#define jffs2_is_writebuffered(c) (c->wbuf != NULL)

#ifdef CONFIG_JFFS2_SUMMARY
#define jffs2_can_mark_obsolete(c) (0)
#else
#define jffs2_can_mark_obsolete(c) (c->mtd->flags & (MTD_BIT_WRITEABLE))
#endif

#define jffs2_cleanmarker_oob(c) (c->mtd->type == MTD_NANDFLASH)

#define jffs2_flash_write_oob(c, ofs, len, retlen, buf) ((c)->mtd->write_oob((c)->mtd, ofs, len, retlen, buf))
#define jffs2_flash_read_oob(c, ofs, len, retlen, buf) ((c)->mtd->read_oob((c)->mtd, ofs, len, retlen, buf))
#define jffs2_wbuf_dirty(c) (!!(c)->wbuf_len)

/* wbuf.c */
int jffs2_flash_writev(struct jffs2_sb_info *c, const struct kvec *vecs, unsigned long count, loff_t to, size_t *retlen, uint32_t ino);
int jffs2_flash_write(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf);
int jffs2_flash_read(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf);
int jffs2_check_oob_empty(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,int mode);
int jffs2_check_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);
int jffs2_write_nand_badblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset);
void jffs2_wbuf_timeout(unsigned long data);
void jffs2_wbuf_process(void *data);
int jffs2_flush_wbuf_gc(struct jffs2_sb_info *c, uint32_t ino);
int jffs2_flush_wbuf_pad(struct jffs2_sb_info *c);
int jffs2_nand_flash_setup(struct jffs2_sb_info *c);
void jffs2_nand_flash_cleanup(struct jffs2_sb_info *c);

#define jffs2_dataflash(c) (c->mtd->type == MTD_DATAFLASH)
int jffs2_dataflash_setup(struct jffs2_sb_info *c);
void jffs2_dataflash_cleanup(struct jffs2_sb_info *c);
#define jffs2_ubivol(c) (c->mtd->type == MTD_UBIVOLUME)
int jffs2_ubivol_setup(struct jffs2_sb_info *c);
void jffs2_ubivol_cleanup(struct jffs2_sb_info *c);

#define jffs2_nor_wbuf_flash(c) (c->mtd->type == MTD_NORFLASH && ! (c->mtd->flags & MTD_BIT_WRITEABLE))
int jffs2_nor_wbuf_flash_setup(struct jffs2_sb_info *c);
void jffs2_nor_wbuf_flash_cleanup(struct jffs2_sb_info *c);

#endif /* WRITEBUFFER */

static inline void jffs2_dirty_trigger(struct jffs2_sb_info *c)
{
	OFNI_BS_2SFFJ(c)->s_dirt = 1;
}

/* background.c */
int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c);
void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c);
void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c);

/* dir.c */
extern const struct file_operations jffs2_dir_operations;
extern const struct inode_operations jffs2_dir_inode_operations;

/* file.c */
extern const struct file_operations jffs2_file_operations;
extern const struct inode_operations jffs2_file_inode_operations;
extern const struct address_space_operations jffs2_file_address_operations;
int jffs2_fsync(struct file *, int);
int jffs2_do_readpage_unlock (struct inode *inode, struct page *pg);

/* ioctl.c */
long jffs2_ioctl(struct file *, unsigned int, unsigned long);

/* symlink.c */
extern const struct inode_operations jffs2_symlink_inode_operations;

/* fs.c */
int jffs2_setattr (struct dentry *, struct iattr *);
int jffs2_do_setattr (struct inode *, struct iattr *);
struct inode *jffs2_iget(struct super_block *, unsigned long);
void jffs2_evict_inode (struct inode *);
void jffs2_dirty_inode(struct inode *inode, int flags);
struct inode *jffs2_new_inode (struct inode *dir_i, int mode,
			       struct jffs2_raw_inode *ri);
int jffs2_statfs (struct dentry *, struct kstatfs *);
int jffs2_remount_fs (struct super_block *, int *, char *);
int jffs2_do_fill_super(struct super_block *sb, void *data, int silent);
void jffs2_gc_release_inode(struct jffs2_sb_info *c,
			    struct jffs2_inode_info *f);
struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      int inum, int unlinked);

unsigned char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv);
void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   unsigned char *pg,
			   unsigned long *priv);
void jffs2_flash_cleanup(struct jffs2_sb_info *c);


/* writev.c */
int jffs2_flash_direct_writev(struct jffs2_sb_info *c, const struct kvec *vecs,
		       unsigned long count, loff_t to, size_t *retlen);
int jffs2_flash_direct_write(struct jffs2_sb_info *c, loff_t ofs, size_t len,
			size_t *retlen, const u_char *buf);

#endif /* __JFFS2_OS_LINUX_H__ */


