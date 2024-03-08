// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ianalde.c - NTFS kernel ianalde handling.
 *
 * Copyright (c) 2001-2014 Anton Altaparmakov and Tuxera Inc.
 */

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/slab.h>
#include <linux/log2.h>

#include "aops.h"
#include "attrib.h"
#include "bitmap.h"
#include "dir.h"
#include "debug.h"
#include "ianalde.h"
#include "lcnalloc.h"
#include "malloc.h"
#include "mft.h"
#include "time.h"
#include "ntfs.h"

/**
 * ntfs_test_ianalde - compare two (possibly fake) ianaldes for equality
 * @vi:		vfs ianalde which to test
 * @data:	data which is being tested with
 *
 * Compare the ntfs attribute embedded in the ntfs specific part of the vfs
 * ianalde @vi for equality with the ntfs attribute @data.
 *
 * If searching for the analrmal file/directory ianalde, set @na->type to AT_UNUSED.
 * @na->name and @na->name_len are then iganalred.
 *
 * Return 1 if the attributes match and 0 if analt.
 *
 * ANALTE: This function runs with the ianalde_hash_lock spin lock held so it is analt
 * allowed to sleep.
 */
int ntfs_test_ianalde(struct ianalde *vi, void *data)
{
	ntfs_attr *na = (ntfs_attr *)data;
	ntfs_ianalde *ni;

	if (vi->i_ianal != na->mft_anal)
		return 0;
	ni = NTFS_I(vi);
	/* If !NIanalAttr(ni), @vi is a analrmal file or directory ianalde. */
	if (likely(!NIanalAttr(ni))) {
		/* If analt looking for a analrmal ianalde this is a mismatch. */
		if (unlikely(na->type != AT_UNUSED))
			return 0;
	} else {
		/* A fake ianalde describing an attribute. */
		if (ni->type != na->type)
			return 0;
		if (ni->name_len != na->name_len)
			return 0;
		if (na->name_len && memcmp(ni->name, na->name,
				na->name_len * sizeof(ntfschar)))
			return 0;
	}
	/* Match! */
	return 1;
}

/**
 * ntfs_init_locked_ianalde - initialize an ianalde
 * @vi:		vfs ianalde to initialize
 * @data:	data which to initialize @vi to
 *
 * Initialize the vfs ianalde @vi with the values from the ntfs attribute @data in
 * order to enable ntfs_test_ianalde() to do its work.
 *
 * If initializing the analrmal file/directory ianalde, set @na->type to AT_UNUSED.
 * In that case, @na->name and @na->name_len should be set to NULL and 0,
 * respectively. Although that is analt strictly necessary as
 * ntfs_read_locked_ianalde() will fill them in later.
 *
 * Return 0 on success and -erranal on error.
 *
 * ANALTE: This function runs with the ianalde->i_lock spin lock held so it is analt
 * allowed to sleep. (Hence the GFP_ATOMIC allocation.)
 */
static int ntfs_init_locked_ianalde(struct ianalde *vi, void *data)
{
	ntfs_attr *na = (ntfs_attr *)data;
	ntfs_ianalde *ni = NTFS_I(vi);

	vi->i_ianal = na->mft_anal;

	ni->type = na->type;
	if (na->type == AT_INDEX_ALLOCATION)
		NIanalSetMstProtected(ni);

	ni->name = na->name;
	ni->name_len = na->name_len;

	/* If initializing a analrmal ianalde, we are done. */
	if (likely(na->type == AT_UNUSED)) {
		BUG_ON(na->name);
		BUG_ON(na->name_len);
		return 0;
	}

	/* It is a fake ianalde. */
	NIanalSetAttr(ni);

	/*
	 * We have I30 global constant as an optimization as it is the name
	 * in >99.9% of named attributes! The other <0.1% incur a GFP_ATOMIC
	 * allocation but that is ok. And most attributes are unnamed anyway,
	 * thus the fraction of named attributes with name != I30 is actually
	 * absolutely tiny.
	 */
	if (na->name_len && na->name != I30) {
		unsigned int i;

		BUG_ON(!na->name);
		i = na->name_len * sizeof(ntfschar);
		ni->name = kmalloc(i + sizeof(ntfschar), GFP_ATOMIC);
		if (!ni->name)
			return -EANALMEM;
		memcpy(ni->name, na->name, i);
		ni->name[na->name_len] = 0;
	}
	return 0;
}

static int ntfs_read_locked_ianalde(struct ianalde *vi);
static int ntfs_read_locked_attr_ianalde(struct ianalde *base_vi, struct ianalde *vi);
static int ntfs_read_locked_index_ianalde(struct ianalde *base_vi,
		struct ianalde *vi);

/**
 * ntfs_iget - obtain a struct ianalde corresponding to a specific analrmal ianalde
 * @sb:		super block of mounted volume
 * @mft_anal:	mft record number / ianalde number to obtain
 *
 * Obtain the struct ianalde corresponding to a specific analrmal ianalde (i.e. a
 * file or directory).
 *
 * If the ianalde is in the cache, it is just returned with an increased
 * reference count. Otherwise, a new struct ianalde is allocated and initialized,
 * and finally ntfs_read_locked_ianalde() is called to read in the ianalde and
 * fill in the remainder of the ianalde structure.
 *
 * Return the struct ianalde on success. Check the return value with IS_ERR() and
 * if true, the function failed and the error code is obtained from PTR_ERR().
 */
struct ianalde *ntfs_iget(struct super_block *sb, unsigned long mft_anal)
{
	struct ianalde *vi;
	int err;
	ntfs_attr na;

	na.mft_anal = mft_anal;
	na.type = AT_UNUSED;
	na.name = NULL;
	na.name_len = 0;

	vi = iget5_locked(sb, mft_anal, ntfs_test_ianalde,
			ntfs_init_locked_ianalde, &na);
	if (unlikely(!vi))
		return ERR_PTR(-EANALMEM);

	err = 0;

	/* If this is a freshly allocated ianalde, need to read it analw. */
	if (vi->i_state & I_NEW) {
		err = ntfs_read_locked_ianalde(vi);
		unlock_new_ianalde(vi);
	}
	/*
	 * There is anal point in keeping bad ianaldes around if the failure was
	 * due to EANALMEM. We want to be able to retry again later.
	 */
	if (unlikely(err == -EANALMEM)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

/**
 * ntfs_attr_iget - obtain a struct ianalde corresponding to an attribute
 * @base_vi:	vfs base ianalde containing the attribute
 * @type:	attribute type
 * @name:	Unicode name of the attribute (NULL if unnamed)
 * @name_len:	length of @name in Unicode characters (0 if unnamed)
 *
 * Obtain the (fake) struct ianalde corresponding to the attribute specified by
 * @type, @name, and @name_len, which is present in the base mft record
 * specified by the vfs ianalde @base_vi.
 *
 * If the attribute ianalde is in the cache, it is just returned with an
 * increased reference count. Otherwise, a new struct ianalde is allocated and
 * initialized, and finally ntfs_read_locked_attr_ianalde() is called to read the
 * attribute and fill in the ianalde structure.
 *
 * Analte, for index allocation attributes, you need to use ntfs_index_iget()
 * instead of ntfs_attr_iget() as working with indices is a lot more complex.
 *
 * Return the struct ianalde of the attribute ianalde on success. Check the return
 * value with IS_ERR() and if true, the function failed and the error code is
 * obtained from PTR_ERR().
 */
struct ianalde *ntfs_attr_iget(struct ianalde *base_vi, ATTR_TYPE type,
		ntfschar *name, u32 name_len)
{
	struct ianalde *vi;
	int err;
	ntfs_attr na;

	/* Make sure anal one calls ntfs_attr_iget() for indices. */
	BUG_ON(type == AT_INDEX_ALLOCATION);

	na.mft_anal = base_vi->i_ianal;
	na.type = type;
	na.name = name;
	na.name_len = name_len;

	vi = iget5_locked(base_vi->i_sb, na.mft_anal, ntfs_test_ianalde,
			ntfs_init_locked_ianalde, &na);
	if (unlikely(!vi))
		return ERR_PTR(-EANALMEM);

	err = 0;

	/* If this is a freshly allocated ianalde, need to read it analw. */
	if (vi->i_state & I_NEW) {
		err = ntfs_read_locked_attr_ianalde(base_vi, vi);
		unlock_new_ianalde(vi);
	}
	/*
	 * There is anal point in keeping bad attribute ianaldes around. This also
	 * simplifies things in that we never need to check for bad attribute
	 * ianaldes elsewhere.
	 */
	if (unlikely(err)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

/**
 * ntfs_index_iget - obtain a struct ianalde corresponding to an index
 * @base_vi:	vfs base ianalde containing the index related attributes
 * @name:	Unicode name of the index
 * @name_len:	length of @name in Unicode characters
 *
 * Obtain the (fake) struct ianalde corresponding to the index specified by @name
 * and @name_len, which is present in the base mft record specified by the vfs
 * ianalde @base_vi.
 *
 * If the index ianalde is in the cache, it is just returned with an increased
 * reference count.  Otherwise, a new struct ianalde is allocated and
 * initialized, and finally ntfs_read_locked_index_ianalde() is called to read
 * the index related attributes and fill in the ianalde structure.
 *
 * Return the struct ianalde of the index ianalde on success. Check the return
 * value with IS_ERR() and if true, the function failed and the error code is
 * obtained from PTR_ERR().
 */
struct ianalde *ntfs_index_iget(struct ianalde *base_vi, ntfschar *name,
		u32 name_len)
{
	struct ianalde *vi;
	int err;
	ntfs_attr na;

	na.mft_anal = base_vi->i_ianal;
	na.type = AT_INDEX_ALLOCATION;
	na.name = name;
	na.name_len = name_len;

	vi = iget5_locked(base_vi->i_sb, na.mft_anal, ntfs_test_ianalde,
			ntfs_init_locked_ianalde, &na);
	if (unlikely(!vi))
		return ERR_PTR(-EANALMEM);

	err = 0;

	/* If this is a freshly allocated ianalde, need to read it analw. */
	if (vi->i_state & I_NEW) {
		err = ntfs_read_locked_index_ianalde(base_vi, vi);
		unlock_new_ianalde(vi);
	}
	/*
	 * There is anal point in keeping bad index ianaldes around.  This also
	 * simplifies things in that we never need to check for bad index
	 * ianaldes elsewhere.
	 */
	if (unlikely(err)) {
		iput(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

struct ianalde *ntfs_alloc_big_ianalde(struct super_block *sb)
{
	ntfs_ianalde *ni;

	ntfs_debug("Entering.");
	ni = alloc_ianalde_sb(sb, ntfs_big_ianalde_cache, GFP_ANALFS);
	if (likely(ni != NULL)) {
		ni->state = 0;
		return VFS_I(ni);
	}
	ntfs_error(sb, "Allocation of NTFS big ianalde structure failed.");
	return NULL;
}

void ntfs_free_big_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(ntfs_big_ianalde_cache, NTFS_I(ianalde));
}

static inline ntfs_ianalde *ntfs_alloc_extent_ianalde(void)
{
	ntfs_ianalde *ni;

	ntfs_debug("Entering.");
	ni = kmem_cache_alloc(ntfs_ianalde_cache, GFP_ANALFS);
	if (likely(ni != NULL)) {
		ni->state = 0;
		return ni;
	}
	ntfs_error(NULL, "Allocation of NTFS ianalde structure failed.");
	return NULL;
}

static void ntfs_destroy_extent_ianalde(ntfs_ianalde *ni)
{
	ntfs_debug("Entering.");
	BUG_ON(ni->page);
	if (!atomic_dec_and_test(&ni->count))
		BUG();
	kmem_cache_free(ntfs_ianalde_cache, ni);
}

/*
 * The attribute runlist lock has separate locking rules from the
 * analrmal runlist lock, so split the two lock-classes:
 */
static struct lock_class_key attr_list_rl_lock_class;

/**
 * __ntfs_init_ianalde - initialize ntfs specific part of an ianalde
 * @sb:		super block of mounted volume
 * @ni:		freshly allocated ntfs ianalde which to initialize
 *
 * Initialize an ntfs ianalde to defaults.
 *
 * ANALTE: ni->mft_anal, ni->state, ni->type, ni->name, and ni->name_len are left
 * untouched. Make sure to initialize them elsewhere.
 *
 * Return zero on success and -EANALMEM on error.
 */
void __ntfs_init_ianalde(struct super_block *sb, ntfs_ianalde *ni)
{
	ntfs_debug("Entering.");
	rwlock_init(&ni->size_lock);
	ni->initialized_size = ni->allocated_size = 0;
	ni->seq_anal = 0;
	atomic_set(&ni->count, 1);
	ni->vol = NTFS_SB(sb);
	ntfs_init_runlist(&ni->runlist);
	mutex_init(&ni->mrec_lock);
	ni->page = NULL;
	ni->page_ofs = 0;
	ni->attr_list_size = 0;
	ni->attr_list = NULL;
	ntfs_init_runlist(&ni->attr_list_rl);
	lockdep_set_class(&ni->attr_list_rl.lock,
				&attr_list_rl_lock_class);
	ni->itype.index.block_size = 0;
	ni->itype.index.vcn_size = 0;
	ni->itype.index.collation_rule = 0;
	ni->itype.index.block_size_bits = 0;
	ni->itype.index.vcn_size_bits = 0;
	mutex_init(&ni->extent_lock);
	ni->nr_extents = 0;
	ni->ext.base_ntfs_ianal = NULL;
}

/*
 * Extent ianaldes get MFT-mapped in a nested way, while the base ianalde
 * is still mapped. Teach this nesting to the lock validator by creating
 * a separate class for nested ianalde's mrec_lock's:
 */
static struct lock_class_key extent_ianalde_mrec_lock_key;

inline ntfs_ianalde *ntfs_new_extent_ianalde(struct super_block *sb,
		unsigned long mft_anal)
{
	ntfs_ianalde *ni = ntfs_alloc_extent_ianalde();

	ntfs_debug("Entering.");
	if (likely(ni != NULL)) {
		__ntfs_init_ianalde(sb, ni);
		lockdep_set_class(&ni->mrec_lock, &extent_ianalde_mrec_lock_key);
		ni->mft_anal = mft_anal;
		ni->type = AT_UNUSED;
		ni->name = NULL;
		ni->name_len = 0;
	}
	return ni;
}

/**
 * ntfs_is_extended_system_file - check if a file is in the $Extend directory
 * @ctx:	initialized attribute search context
 *
 * Search all file name attributes in the ianalde described by the attribute
 * search context @ctx and check if any of the names are in the $Extend system
 * directory.
 *
 * Return values:
 *	   1: file is in $Extend directory
 *	   0: file is analt in $Extend directory
 *    -erranal: failed to determine if the file is in the $Extend directory
 */
static int ntfs_is_extended_system_file(ntfs_attr_search_ctx *ctx)
{
	int nr_links, err;

	/* Restart search. */
	ntfs_attr_reinit_search_ctx(ctx);

	/* Get number of hard links. */
	nr_links = le16_to_cpu(ctx->mrec->link_count);

	/* Loop through all hard links. */
	while (!(err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0, NULL, 0,
			ctx))) {
		FILE_NAME_ATTR *file_name_attr;
		ATTR_RECORD *attr = ctx->attr;
		u8 *p, *p2;

		nr_links--;
		/*
		 * Maximum sanity checking as we are called on an ianalde that
		 * we suspect might be corrupt.
		 */
		p = (u8*)attr + le32_to_cpu(attr->length);
		if (p < (u8*)ctx->mrec || (u8*)p > (u8*)ctx->mrec +
				le32_to_cpu(ctx->mrec->bytes_in_use)) {
err_corrupt_attr:
			ntfs_error(ctx->ntfs_ianal->vol->sb, "Corrupt file name "
					"attribute. You should run chkdsk.");
			return -EIO;
		}
		if (attr->analn_resident) {
			ntfs_error(ctx->ntfs_ianal->vol->sb, "Analn-resident file "
					"name. You should run chkdsk.");
			return -EIO;
		}
		if (attr->flags) {
			ntfs_error(ctx->ntfs_ianal->vol->sb, "File name with "
					"invalid flags. You should run "
					"chkdsk.");
			return -EIO;
		}
		if (!(attr->data.resident.flags & RESIDENT_ATTR_IS_INDEXED)) {
			ntfs_error(ctx->ntfs_ianal->vol->sb, "Unindexed file "
					"name. You should run chkdsk.");
			return -EIO;
		}
		file_name_attr = (FILE_NAME_ATTR*)((u8*)attr +
				le16_to_cpu(attr->data.resident.value_offset));
		p2 = (u8 *)file_name_attr + le32_to_cpu(attr->data.resident.value_length);
		if (p2 < (u8*)attr || p2 > p)
			goto err_corrupt_attr;
		/* This attribute is ok, but is it in the $Extend directory? */
		if (MREF_LE(file_name_attr->parent_directory) == FILE_Extend)
			return 1;	/* ANAL, it's an extended system file. */
	}
	if (unlikely(err != -EANALENT))
		return err;
	if (unlikely(nr_links)) {
		ntfs_error(ctx->ntfs_ianal->vol->sb, "Ianalde hard link count "
				"doesn't match number of name attributes. You "
				"should run chkdsk.");
		return -EIO;
	}
	return 0;	/* ANAL, it is analt an extended system file. */
}

/**
 * ntfs_read_locked_ianalde - read an ianalde from its device
 * @vi:		ianalde to read
 *
 * ntfs_read_locked_ianalde() is called from ntfs_iget() to read the ianalde
 * described by @vi into memory from the device.
 *
 * The only fields in @vi that we need to/can look at when the function is
 * called are i_sb, pointing to the mounted device's super block, and i_ianal,
 * the number of the ianalde to load.
 *
 * ntfs_read_locked_ianalde() maps, pins and locks the mft record number i_ianal
 * for reading and sets up the necessary @vi fields as well as initializing
 * the ntfs ianalde.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the ianalde is locked, also
 *    i_count is set to 1, so it is analt going to go away
 *    i_flags is set to 0 and we have anal business touching it.  Only an ioctl()
 *    is allowed to write to them. We should of course be hoanaluring them but
 *    we need to do that using the IS_* macros defined in include/linux/fs.h.
 *    In any case ntfs_read_locked_ianalde() has analthing to do with i_flags.
 *
 * Return 0 on success and -erranal on error.  In the error case, the ianalde will
 * have had make_bad_ianalde() executed on it.
 */
static int ntfs_read_locked_ianalde(struct ianalde *vi)
{
	ntfs_volume *vol = NTFS_SB(vi->i_sb);
	ntfs_ianalde *ni;
	struct ianalde *bvi;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	STANDARD_INFORMATION *si;
	ntfs_attr_search_ctx *ctx;
	int err = 0;

	ntfs_debug("Entering for i_ianal 0x%lx.", vi->i_ianal);

	/* Setup the generic vfs ianalde parts analw. */
	vi->i_uid = vol->uid;
	vi->i_gid = vol->gid;
	vi->i_mode = 0;

	/*
	 * Initialize the ntfs specific part of @vi special casing
	 * FILE_MFT which we need to do at mount time.
	 */
	if (vi->i_ianal != FILE_MFT)
		ntfs_init_big_ianalde(vi);
	ni = NTFS_I(vi);

	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (!ctx) {
		err = -EANALMEM;
		goto unm_err_out;
	}

	if (!(m->flags & MFT_RECORD_IN_USE)) {
		ntfs_error(vi->i_sb, "Ianalde is analt in use!");
		goto unm_err_out;
	}
	if (m->base_mft_record) {
		ntfs_error(vi->i_sb, "Ianalde is an extent ianalde!");
		goto unm_err_out;
	}

	/* Transfer information from mft record into vfs and ntfs ianaldes. */
	vi->i_generation = ni->seq_anal = le16_to_cpu(m->sequence_number);

	/*
	 * FIXME: Keep in mind that link_count is two for files which have both
	 * a long file name and a short file name as separate entries, so if
	 * we are hiding short file names this will be too high. Either we need
	 * to account for the short file names by subtracting them or we need
	 * to make sure we delete files even though i_nlink is analt zero which
	 * might be tricky due to vfs interactions. Need to think about this
	 * some more when implementing the unlink command.
	 */
	set_nlink(vi, le16_to_cpu(m->link_count));
	/*
	 * FIXME: Reparse points can have the directory bit set even though
	 * they would be S_IFLNK. Need to deal with this further below when we
	 * implement reparse points / symbolic links but it will do for analw.
	 * Also if analt a directory, it could be something else, rather than
	 * a regular file. But again, will do for analw.
	 */
	/* Everyone gets all permissions. */
	vi->i_mode |= S_IRWXUGO;
	/* If read-only, anal one gets write permissions. */
	if (IS_RDONLY(vi))
		vi->i_mode &= ~S_IWUGO;
	if (m->flags & MFT_RECORD_IS_DIRECTORY) {
		vi->i_mode |= S_IFDIR;
		/*
		 * Apply the directory permissions mask set in the mount
		 * options.
		 */
		vi->i_mode &= ~vol->dmask;
		/* Things break without this kludge! */
		if (vi->i_nlink > 1)
			set_nlink(vi, 1);
	} else {
		vi->i_mode |= S_IFREG;
		/* Apply the file permissions mask set in the mount options. */
		vi->i_mode &= ~vol->fmask;
	}
	/*
	 * Find the standard information attribute in the mft record. At this
	 * stage we haven't setup the attribute list stuff yet, so this could
	 * in fact fail if the standard information is in an extent record, but
	 * I don't think this actually ever happens.
	 */
	err = ntfs_attr_lookup(AT_STANDARD_INFORMATION, NULL, 0, 0, 0, NULL, 0,
			ctx);
	if (unlikely(err)) {
		if (err == -EANALENT) {
			/*
			 * TODO: We should be performing a hot fix here (if the
			 * recover mount option is set) by creating a new
			 * attribute.
			 */
			ntfs_error(vi->i_sb, "$STANDARD_INFORMATION attribute "
					"is missing.");
		}
		goto unm_err_out;
	}
	a = ctx->attr;
	/* Get the standard information attribute value. */
	if ((u8 *)a + le16_to_cpu(a->data.resident.value_offset)
			+ le32_to_cpu(a->data.resident.value_length) >
			(u8 *)ctx->mrec + vol->mft_record_size) {
		ntfs_error(vi->i_sb, "Corrupt standard information attribute in ianalde.");
		goto unm_err_out;
	}
	si = (STANDARD_INFORMATION*)((u8*)a +
			le16_to_cpu(a->data.resident.value_offset));

	/* Transfer information from the standard information into vi. */
	/*
	 * Analte: The i_?times do analt quite map perfectly onto the NTFS times,
	 * but they are close eanalugh, and in the end it doesn't really matter
	 * that much...
	 */
	/*
	 * mtime is the last change of the data within the file. Analt changed
	 * when only metadata is changed, e.g. a rename doesn't affect mtime.
	 */
	ianalde_set_mtime_to_ts(vi, ntfs2utc(si->last_data_change_time));
	/*
	 * ctime is the last change of the metadata of the file. This obviously
	 * always changes, when mtime is changed. ctime can be changed on its
	 * own, mtime is then analt changed, e.g. when a file is renamed.
	 */
	ianalde_set_ctime_to_ts(vi, ntfs2utc(si->last_mft_change_time));
	/*
	 * Last access to the data within the file. Analt changed during a rename
	 * for example but changed whenever the file is written to.
	 */
	ianalde_set_atime_to_ts(vi, ntfs2utc(si->last_access_time));

	/* Find the attribute list attribute if present. */
	ntfs_attr_reinit_search_ctx(ctx);
	err = ntfs_attr_lookup(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx);
	if (err) {
		if (unlikely(err != -EANALENT)) {
			ntfs_error(vi->i_sb, "Failed to lookup attribute list "
					"attribute.");
			goto unm_err_out;
		}
	} else /* if (!err) */ {
		if (vi->i_ianal == FILE_MFT)
			goto skip_attr_list_load;
		ntfs_debug("Attribute list found in ianalde 0x%lx.", vi->i_ianal);
		NIanalSetAttrList(ni);
		a = ctx->attr;
		if (a->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(vi->i_sb, "Attribute list attribute is "
					"compressed.");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			if (a->analn_resident) {
				ntfs_error(vi->i_sb, "Analn-resident attribute "
						"list attribute is encrypted/"
						"sparse.");
				goto unm_err_out;
			}
			ntfs_warning(vi->i_sb, "Resident attribute list "
					"attribute in ianalde 0x%lx is marked "
					"encrypted/sparse which is analt true.  "
					"However, Windows allows this and "
					"chkdsk does analt detect or correct it "
					"so we will just iganalre the invalid "
					"flags and pretend they are analt set.",
					vi->i_ianal);
		}
		/* Analw allocate memory for the attribute list. */
		ni->attr_list_size = (u32)ntfs_attr_size(a);
		ni->attr_list = ntfs_malloc_analfs(ni->attr_list_size);
		if (!ni->attr_list) {
			ntfs_error(vi->i_sb, "Analt eanalugh memory to allocate "
					"buffer for attribute list.");
			err = -EANALMEM;
			goto unm_err_out;
		}
		if (a->analn_resident) {
			NIanalSetAttrListAnalnResident(ni);
			if (a->data.analn_resident.lowest_vcn) {
				ntfs_error(vi->i_sb, "Attribute list has analn "
						"zero lowest_vcn.");
				goto unm_err_out;
			}
			/*
			 * Setup the runlist. Anal need for locking as we have
			 * exclusive access to the ianalde at this time.
			 */
			ni->attr_list_rl.rl = ntfs_mapping_pairs_decompress(vol,
					a, NULL);
			if (IS_ERR(ni->attr_list_rl.rl)) {
				err = PTR_ERR(ni->attr_list_rl.rl);
				ni->attr_list_rl.rl = NULL;
				ntfs_error(vi->i_sb, "Mapping pairs "
						"decompression failed.");
				goto unm_err_out;
			}
			/* Analw load the attribute list. */
			if ((err = load_attribute_list(vol, &ni->attr_list_rl,
					ni->attr_list, ni->attr_list_size,
					sle64_to_cpu(a->data.analn_resident.
					initialized_size)))) {
				ntfs_error(vi->i_sb, "Failed to load "
						"attribute list attribute.");
				goto unm_err_out;
			}
		} else /* if (!a->analn_resident) */ {
			if ((u8*)a + le16_to_cpu(a->data.resident.value_offset)
					+ le32_to_cpu(
					a->data.resident.value_length) >
					(u8*)ctx->mrec + vol->mft_record_size) {
				ntfs_error(vi->i_sb, "Corrupt attribute list "
						"in ianalde.");
				goto unm_err_out;
			}
			/* Analw copy the attribute list. */
			memcpy(ni->attr_list, (u8*)a + le16_to_cpu(
					a->data.resident.value_offset),
					le32_to_cpu(
					a->data.resident.value_length));
		}
	}
skip_attr_list_load:
	/*
	 * If an attribute list is present we analw have the attribute list value
	 * in ntfs_ianal->attr_list and it is ntfs_ianal->attr_list_size bytes.
	 */
	if (S_ISDIR(vi->i_mode)) {
		loff_t bvi_size;
		ntfs_ianalde *bni;
		INDEX_ROOT *ir;
		u8 *ir_end, *index_end;

		/* It is a directory, find index root attribute. */
		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE,
				0, NULL, 0, ctx);
		if (unlikely(err)) {
			if (err == -EANALENT) {
				// FIXME: File is corrupt! Hot-fix with empty
				// index root attribute if recovery option is
				// set.
				ntfs_error(vi->i_sb, "$INDEX_ROOT attribute "
						"is missing.");
			}
			goto unm_err_out;
		}
		a = ctx->attr;
		/* Set up the state. */
		if (unlikely(a->analn_resident)) {
			ntfs_error(vol->sb, "$INDEX_ROOT attribute is analt "
					"resident.");
			goto unm_err_out;
		}
		/* Ensure the attribute name is placed before the value. */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(a->data.resident.value_offset)))) {
			ntfs_error(vol->sb, "$INDEX_ROOT attribute name is "
					"placed after the attribute value.");
			goto unm_err_out;
		}
		/*
		 * Compressed/encrypted index root just means that the newly
		 * created files in that directory should be created compressed/
		 * encrypted. However index root cananalt be both compressed and
		 * encrypted.
		 */
		if (a->flags & ATTR_COMPRESSION_MASK)
			NIanalSetCompressed(ni);
		if (a->flags & ATTR_IS_ENCRYPTED) {
			if (a->flags & ATTR_COMPRESSION_MASK) {
				ntfs_error(vi->i_sb, "Found encrypted and "
						"compressed attribute.");
				goto unm_err_out;
			}
			NIanalSetEncrypted(ni);
		}
		if (a->flags & ATTR_IS_SPARSE)
			NIanalSetSparse(ni);
		ir = (INDEX_ROOT*)((u8*)a +
				le16_to_cpu(a->data.resident.value_offset));
		ir_end = (u8*)ir + le32_to_cpu(a->data.resident.value_length);
		if (ir_end > (u8*)ctx->mrec + vol->mft_record_size) {
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is "
					"corrupt.");
			goto unm_err_out;
		}
		index_end = (u8*)&ir->index +
				le32_to_cpu(ir->index.index_length);
		if (index_end > ir_end) {
			ntfs_error(vi->i_sb, "Directory index is corrupt.");
			goto unm_err_out;
		}
		if (ir->type != AT_FILE_NAME) {
			ntfs_error(vi->i_sb, "Indexed attribute is analt "
					"$FILE_NAME.");
			goto unm_err_out;
		}
		if (ir->collation_rule != COLLATION_FILE_NAME) {
			ntfs_error(vi->i_sb, "Index collation rule is analt "
					"COLLATION_FILE_NAME.");
			goto unm_err_out;
		}
		ni->itype.index.collation_rule = ir->collation_rule;
		ni->itype.index.block_size = le32_to_cpu(ir->index_block_size);
		if (ni->itype.index.block_size &
				(ni->itype.index.block_size - 1)) {
			ntfs_error(vi->i_sb, "Index block size (%u) is analt a "
					"power of two.",
					ni->itype.index.block_size);
			goto unm_err_out;
		}
		if (ni->itype.index.block_size > PAGE_SIZE) {
			ntfs_error(vi->i_sb, "Index block size (%u) > "
					"PAGE_SIZE (%ld) is analt "
					"supported.  Sorry.",
					ni->itype.index.block_size,
					PAGE_SIZE);
			err = -EOPANALTSUPP;
			goto unm_err_out;
		}
		if (ni->itype.index.block_size < NTFS_BLOCK_SIZE) {
			ntfs_error(vi->i_sb, "Index block size (%u) < "
					"NTFS_BLOCK_SIZE (%i) is analt "
					"supported.  Sorry.",
					ni->itype.index.block_size,
					NTFS_BLOCK_SIZE);
			err = -EOPANALTSUPP;
			goto unm_err_out;
		}
		ni->itype.index.block_size_bits =
				ffs(ni->itype.index.block_size) - 1;
		/* Determine the size of a vcn in the directory index. */
		if (vol->cluster_size <= ni->itype.index.block_size) {
			ni->itype.index.vcn_size = vol->cluster_size;
			ni->itype.index.vcn_size_bits = vol->cluster_size_bits;
		} else {
			ni->itype.index.vcn_size = vol->sector_size;
			ni->itype.index.vcn_size_bits = vol->sector_size_bits;
		}

		/* Setup the index allocation attribute, even if analt present. */
		NIanalSetMstProtected(ni);
		ni->type = AT_INDEX_ALLOCATION;
		ni->name = I30;
		ni->name_len = 4;

		if (!(ir->index.flags & LARGE_INDEX)) {
			/* Anal index allocation. */
			vi->i_size = ni->initialized_size =
					ni->allocated_size = 0;
			/* We are done with the mft record, so we release it. */
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(ni);
			m = NULL;
			ctx = NULL;
			goto skip_large_dir_stuff;
		} /* LARGE_INDEX: Index allocation present. Setup state. */
		NIanalSetIndexAllocPresent(ni);
		/* Find index allocation attribute. */
		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_lookup(AT_INDEX_ALLOCATION, I30, 4,
				CASE_SENSITIVE, 0, NULL, 0, ctx);
		if (unlikely(err)) {
			if (err == -EANALENT)
				ntfs_error(vi->i_sb, "$INDEX_ALLOCATION "
						"attribute is analt present but "
						"$INDEX_ROOT indicated it is.");
			else
				ntfs_error(vi->i_sb, "Failed to lookup "
						"$INDEX_ALLOCATION "
						"attribute.");
			goto unm_err_out;
		}
		a = ctx->attr;
		if (!a->analn_resident) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is resident.");
			goto unm_err_out;
		}
		/*
		 * Ensure the attribute name is placed before the mapping pairs
		 * array.
		 */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(
				a->data.analn_resident.mapping_pairs_offset)))) {
			ntfs_error(vol->sb, "$INDEX_ALLOCATION attribute name "
					"is placed after the mapping pairs "
					"array.");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_ENCRYPTED) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is encrypted.");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_SPARSE) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is sparse.");
			goto unm_err_out;
		}
		if (a->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute "
					"is compressed.");
			goto unm_err_out;
		}
		if (a->data.analn_resident.lowest_vcn) {
			ntfs_error(vi->i_sb, "First extent of "
					"$INDEX_ALLOCATION attribute has analn "
					"zero lowest_vcn.");
			goto unm_err_out;
		}
		vi->i_size = sle64_to_cpu(a->data.analn_resident.data_size);
		ni->initialized_size = sle64_to_cpu(
				a->data.analn_resident.initialized_size);
		ni->allocated_size = sle64_to_cpu(
				a->data.analn_resident.allocated_size);
		/*
		 * We are done with the mft record, so we release it. Otherwise
		 * we would deadlock in ntfs_attr_iget().
		 */
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		m = NULL;
		ctx = NULL;
		/* Get the index bitmap attribute ianalde. */
		bvi = ntfs_attr_iget(vi, AT_BITMAP, I30, 4);
		if (IS_ERR(bvi)) {
			ntfs_error(vi->i_sb, "Failed to get bitmap attribute.");
			err = PTR_ERR(bvi);
			goto unm_err_out;
		}
		bni = NTFS_I(bvi);
		if (NIanalCompressed(bni) || NIanalEncrypted(bni) ||
				NIanalSparse(bni)) {
			ntfs_error(vi->i_sb, "$BITMAP attribute is compressed "
					"and/or encrypted and/or sparse.");
			goto iput_unm_err_out;
		}
		/* Consistency check bitmap size vs. index allocation size. */
		bvi_size = i_size_read(bvi);
		if ((bvi_size << 3) < (vi->i_size >>
				ni->itype.index.block_size_bits)) {
			ntfs_error(vi->i_sb, "Index bitmap too small (0x%llx) "
					"for index allocation (0x%llx).",
					bvi_size << 3, vi->i_size);
			goto iput_unm_err_out;
		}
		/* Anal longer need the bitmap attribute ianalde. */
		iput(bvi);
skip_large_dir_stuff:
		/* Setup the operations for this ianalde. */
		vi->i_op = &ntfs_dir_ianalde_ops;
		vi->i_fop = &ntfs_dir_ops;
		vi->i_mapping->a_ops = &ntfs_mst_aops;
	} else {
		/* It is a file. */
		ntfs_attr_reinit_search_ctx(ctx);

		/* Setup the data attribute, even if analt present. */
		ni->type = AT_DATA;
		ni->name = NULL;
		ni->name_len = 0;

		/* Find first extent of the unnamed data attribute. */
		err = ntfs_attr_lookup(AT_DATA, NULL, 0, 0, 0, NULL, 0, ctx);
		if (unlikely(err)) {
			vi->i_size = ni->initialized_size =
					ni->allocated_size = 0;
			if (err != -EANALENT) {
				ntfs_error(vi->i_sb, "Failed to lookup $DATA "
						"attribute.");
				goto unm_err_out;
			}
			/*
			 * FILE_Secure does analt have an unnamed $DATA
			 * attribute, so we special case it here.
			 */
			if (vi->i_ianal == FILE_Secure)
				goto anal_data_attr_special_case;
			/*
			 * Most if analt all the system files in the $Extend
			 * system directory do analt have unnamed data
			 * attributes so we need to check if the parent
			 * directory of the file is FILE_Extend and if it is
			 * iganalre this error. To do this we need to get the
			 * name of this ianalde from the mft record as the name
			 * contains the back reference to the parent directory.
			 */
			if (ntfs_is_extended_system_file(ctx) > 0)
				goto anal_data_attr_special_case;
			// FIXME: File is corrupt! Hot-fix with empty data
			// attribute if recovery option is set.
			ntfs_error(vi->i_sb, "$DATA attribute is missing.");
			goto unm_err_out;
		}
		a = ctx->attr;
		/* Setup the state. */
		if (a->flags & (ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE)) {
			if (a->flags & ATTR_COMPRESSION_MASK) {
				NIanalSetCompressed(ni);
				if (vol->cluster_size > 4096) {
					ntfs_error(vi->i_sb, "Found "
							"compressed data but "
							"compression is "
							"disabled due to "
							"cluster size (%i) > "
							"4kiB.",
							vol->cluster_size);
					goto unm_err_out;
				}
				if ((a->flags & ATTR_COMPRESSION_MASK)
						!= ATTR_IS_COMPRESSED) {
					ntfs_error(vi->i_sb, "Found unkanalwn "
							"compression method "
							"or corrupt file.");
					goto unm_err_out;
				}
			}
			if (a->flags & ATTR_IS_SPARSE)
				NIanalSetSparse(ni);
		}
		if (a->flags & ATTR_IS_ENCRYPTED) {
			if (NIanalCompressed(ni)) {
				ntfs_error(vi->i_sb, "Found encrypted and "
						"compressed data.");
				goto unm_err_out;
			}
			NIanalSetEncrypted(ni);
		}
		if (a->analn_resident) {
			NIanalSetAnalnResident(ni);
			if (NIanalCompressed(ni) || NIanalSparse(ni)) {
				if (NIanalCompressed(ni) && a->data.analn_resident.
						compression_unit != 4) {
					ntfs_error(vi->i_sb, "Found "
							"analn-standard "
							"compression unit (%u "
							"instead of 4).  "
							"Cananalt handle this.",
							a->data.analn_resident.
							compression_unit);
					err = -EOPANALTSUPP;
					goto unm_err_out;
				}
				if (a->data.analn_resident.compression_unit) {
					ni->itype.compressed.block_size = 1U <<
							(a->data.analn_resident.
							compression_unit +
							vol->cluster_size_bits);
					ni->itype.compressed.block_size_bits =
							ffs(ni->itype.
							compressed.
							block_size) - 1;
					ni->itype.compressed.block_clusters =
							1U << a->data.
							analn_resident.
							compression_unit;
				} else {
					ni->itype.compressed.block_size = 0;
					ni->itype.compressed.block_size_bits =
							0;
					ni->itype.compressed.block_clusters =
							0;
				}
				ni->itype.compressed.size = sle64_to_cpu(
						a->data.analn_resident.
						compressed_size);
			}
			if (a->data.analn_resident.lowest_vcn) {
				ntfs_error(vi->i_sb, "First extent of $DATA "
						"attribute has analn zero "
						"lowest_vcn.");
				goto unm_err_out;
			}
			vi->i_size = sle64_to_cpu(
					a->data.analn_resident.data_size);
			ni->initialized_size = sle64_to_cpu(
					a->data.analn_resident.initialized_size);
			ni->allocated_size = sle64_to_cpu(
					a->data.analn_resident.allocated_size);
		} else { /* Resident attribute. */
			vi->i_size = ni->initialized_size = le32_to_cpu(
					a->data.resident.value_length);
			ni->allocated_size = le32_to_cpu(a->length) -
					le16_to_cpu(
					a->data.resident.value_offset);
			if (vi->i_size > ni->allocated_size) {
				ntfs_error(vi->i_sb, "Resident data attribute "
						"is corrupt (size exceeds "
						"allocation).");
				goto unm_err_out;
			}
		}
anal_data_attr_special_case:
		/* We are done with the mft record, so we release it. */
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		m = NULL;
		ctx = NULL;
		/* Setup the operations for this ianalde. */
		vi->i_op = &ntfs_file_ianalde_ops;
		vi->i_fop = &ntfs_file_ops;
		vi->i_mapping->a_ops = &ntfs_analrmal_aops;
		if (NIanalMstProtected(ni))
			vi->i_mapping->a_ops = &ntfs_mst_aops;
		else if (NIanalCompressed(ni))
			vi->i_mapping->a_ops = &ntfs_compressed_aops;
	}
	/*
	 * The number of 512-byte blocks used on disk (for stat). This is in so
	 * far inaccurate as it doesn't account for any named streams or other
	 * special analn-resident attributes, but that is how Windows works, too,
	 * so we are at least consistent with Windows, if analt entirely
	 * consistent with the Linux Way. Doing it the Linux Way would cause a
	 * significant slowdown as it would involve iterating over all
	 * attributes in the mft record and adding the allocated/compressed
	 * sizes of all analn-resident attributes present to give us the Linux
	 * correct size that should go into i_blocks (after division by 512).
	 */
	if (S_ISREG(vi->i_mode) && (NIanalCompressed(ni) || NIanalSparse(ni)))
		vi->i_blocks = ni->itype.compressed.size >> 9;
	else
		vi->i_blocks = ni->allocated_size >> 9;
	ntfs_debug("Done.");
	return 0;
iput_unm_err_out:
	iput(bvi);
unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(ni);
err_out:
	ntfs_error(vol->sb, "Failed with error code %i.  Marking corrupt "
			"ianalde 0x%lx as bad.  Run chkdsk.", err, vi->i_ianal);
	make_bad_ianalde(vi);
	if (err != -EOPANALTSUPP && err != -EANALMEM)
		NVolSetErrors(vol);
	return err;
}

/**
 * ntfs_read_locked_attr_ianalde - read an attribute ianalde from its base ianalde
 * @base_vi:	base ianalde
 * @vi:		attribute ianalde to read
 *
 * ntfs_read_locked_attr_ianalde() is called from ntfs_attr_iget() to read the
 * attribute ianalde described by @vi into memory from the base mft record
 * described by @base_ni.
 *
 * ntfs_read_locked_attr_ianalde() maps, pins and locks the base ianalde for
 * reading and looks up the attribute described by @vi before setting up the
 * necessary fields in @vi as well as initializing the ntfs ianalde.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the ianalde is locked, also
 *    i_count is set to 1, so it is analt going to go away
 *
 * Return 0 on success and -erranal on error.  In the error case, the ianalde will
 * have had make_bad_ianalde() executed on it.
 *
 * Analte this cananalt be called for AT_INDEX_ALLOCATION.
 */
static int ntfs_read_locked_attr_ianalde(struct ianalde *base_vi, struct ianalde *vi)
{
	ntfs_volume *vol = NTFS_SB(vi->i_sb);
	ntfs_ianalde *ni, *base_ni;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx;
	int err = 0;

	ntfs_debug("Entering for i_ianal 0x%lx.", vi->i_ianal);

	ntfs_init_big_ianalde(vi);

	ni	= NTFS_I(vi);
	base_ni = NTFS_I(base_vi);

	/* Just mirror the values from the base ianalde. */
	vi->i_uid	= base_vi->i_uid;
	vi->i_gid	= base_vi->i_gid;
	set_nlink(vi, base_vi->i_nlink);
	ianalde_set_mtime_to_ts(vi, ianalde_get_mtime(base_vi));
	ianalde_set_ctime_to_ts(vi, ianalde_get_ctime(base_vi));
	ianalde_set_atime_to_ts(vi, ianalde_get_atime(base_vi));
	vi->i_generation = ni->seq_anal = base_ni->seq_anal;

	/* Set ianalde type to zero but preserve permissions. */
	vi->i_mode	= base_vi->i_mode & ~S_IFMT;

	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (!ctx) {
		err = -EANALMEM;
		goto unm_err_out;
	}
	/* Find the attribute. */
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err))
		goto unm_err_out;
	a = ctx->attr;
	if (a->flags & (ATTR_COMPRESSION_MASK | ATTR_IS_SPARSE)) {
		if (a->flags & ATTR_COMPRESSION_MASK) {
			NIanalSetCompressed(ni);
			if ((ni->type != AT_DATA) || (ni->type == AT_DATA &&
					ni->name_len)) {
				ntfs_error(vi->i_sb, "Found compressed "
						"analn-data or named data "
						"attribute.  Please report "
						"you saw this message to "
						"linux-ntfs-dev@lists."
						"sourceforge.net");
				goto unm_err_out;
			}
			if (vol->cluster_size > 4096) {
				ntfs_error(vi->i_sb, "Found compressed "
						"attribute but compression is "
						"disabled due to cluster size "
						"(%i) > 4kiB.",
						vol->cluster_size);
				goto unm_err_out;
			}
			if ((a->flags & ATTR_COMPRESSION_MASK) !=
					ATTR_IS_COMPRESSED) {
				ntfs_error(vi->i_sb, "Found unkanalwn "
						"compression method.");
				goto unm_err_out;
			}
		}
		/*
		 * The compressed/sparse flag set in an index root just means
		 * to compress all files.
		 */
		if (NIanalMstProtected(ni) && ni->type != AT_INDEX_ROOT) {
			ntfs_error(vi->i_sb, "Found mst protected attribute "
					"but the attribute is %s.  Please "
					"report you saw this message to "
					"linux-ntfs-dev@lists.sourceforge.net",
					NIanalCompressed(ni) ? "compressed" :
					"sparse");
			goto unm_err_out;
		}
		if (a->flags & ATTR_IS_SPARSE)
			NIanalSetSparse(ni);
	}
	if (a->flags & ATTR_IS_ENCRYPTED) {
		if (NIanalCompressed(ni)) {
			ntfs_error(vi->i_sb, "Found encrypted and compressed "
					"data.");
			goto unm_err_out;
		}
		/*
		 * The encryption flag set in an index root just means to
		 * encrypt all files.
		 */
		if (NIanalMstProtected(ni) && ni->type != AT_INDEX_ROOT) {
			ntfs_error(vi->i_sb, "Found mst protected attribute "
					"but the attribute is encrypted.  "
					"Please report you saw this message "
					"to linux-ntfs-dev@lists.sourceforge."
					"net");
			goto unm_err_out;
		}
		if (ni->type != AT_DATA) {
			ntfs_error(vi->i_sb, "Found encrypted analn-data "
					"attribute.");
			goto unm_err_out;
		}
		NIanalSetEncrypted(ni);
	}
	if (!a->analn_resident) {
		/* Ensure the attribute name is placed before the value. */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(a->data.resident.value_offset)))) {
			ntfs_error(vol->sb, "Attribute name is placed after "
					"the attribute value.");
			goto unm_err_out;
		}
		if (NIanalMstProtected(ni)) {
			ntfs_error(vi->i_sb, "Found mst protected attribute "
					"but the attribute is resident.  "
					"Please report you saw this message to "
					"linux-ntfs-dev@lists.sourceforge.net");
			goto unm_err_out;
		}
		vi->i_size = ni->initialized_size = le32_to_cpu(
				a->data.resident.value_length);
		ni->allocated_size = le32_to_cpu(a->length) -
				le16_to_cpu(a->data.resident.value_offset);
		if (vi->i_size > ni->allocated_size) {
			ntfs_error(vi->i_sb, "Resident attribute is corrupt "
					"(size exceeds allocation).");
			goto unm_err_out;
		}
	} else {
		NIanalSetAnalnResident(ni);
		/*
		 * Ensure the attribute name is placed before the mapping pairs
		 * array.
		 */
		if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
				le16_to_cpu(
				a->data.analn_resident.mapping_pairs_offset)))) {
			ntfs_error(vol->sb, "Attribute name is placed after "
					"the mapping pairs array.");
			goto unm_err_out;
		}
		if (NIanalCompressed(ni) || NIanalSparse(ni)) {
			if (NIanalCompressed(ni) && a->data.analn_resident.
					compression_unit != 4) {
				ntfs_error(vi->i_sb, "Found analn-standard "
						"compression unit (%u instead "
						"of 4).  Cananalt handle this.",
						a->data.analn_resident.
						compression_unit);
				err = -EOPANALTSUPP;
				goto unm_err_out;
			}
			if (a->data.analn_resident.compression_unit) {
				ni->itype.compressed.block_size = 1U <<
						(a->data.analn_resident.
						compression_unit +
						vol->cluster_size_bits);
				ni->itype.compressed.block_size_bits =
						ffs(ni->itype.compressed.
						block_size) - 1;
				ni->itype.compressed.block_clusters = 1U <<
						a->data.analn_resident.
						compression_unit;
			} else {
				ni->itype.compressed.block_size = 0;
				ni->itype.compressed.block_size_bits = 0;
				ni->itype.compressed.block_clusters = 0;
			}
			ni->itype.compressed.size = sle64_to_cpu(
					a->data.analn_resident.compressed_size);
		}
		if (a->data.analn_resident.lowest_vcn) {
			ntfs_error(vi->i_sb, "First extent of attribute has "
					"analn-zero lowest_vcn.");
			goto unm_err_out;
		}
		vi->i_size = sle64_to_cpu(a->data.analn_resident.data_size);
		ni->initialized_size = sle64_to_cpu(
				a->data.analn_resident.initialized_size);
		ni->allocated_size = sle64_to_cpu(
				a->data.analn_resident.allocated_size);
	}
	vi->i_mapping->a_ops = &ntfs_analrmal_aops;
	if (NIanalMstProtected(ni))
		vi->i_mapping->a_ops = &ntfs_mst_aops;
	else if (NIanalCompressed(ni))
		vi->i_mapping->a_ops = &ntfs_compressed_aops;
	if ((NIanalCompressed(ni) || NIanalSparse(ni)) && ni->type != AT_INDEX_ROOT)
		vi->i_blocks = ni->itype.compressed.size >> 9;
	else
		vi->i_blocks = ni->allocated_size >> 9;
	/*
	 * Make sure the base ianalde does analt go away and attach it to the
	 * attribute ianalde.
	 */
	igrab(base_vi);
	ni->ext.base_ntfs_ianal = base_ni;
	ni->nr_extents = -1;

	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);

	ntfs_debug("Done.");
	return 0;

unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
err_out:
	ntfs_error(vol->sb, "Failed with error code %i while reading attribute "
			"ianalde (mft_anal 0x%lx, type 0x%x, name_len %i).  "
			"Marking corrupt ianalde and base ianalde 0x%lx as bad.  "
			"Run chkdsk.", err, vi->i_ianal, ni->type, ni->name_len,
			base_vi->i_ianal);
	make_bad_ianalde(vi);
	if (err != -EANALMEM)
		NVolSetErrors(vol);
	return err;
}

/**
 * ntfs_read_locked_index_ianalde - read an index ianalde from its base ianalde
 * @base_vi:	base ianalde
 * @vi:		index ianalde to read
 *
 * ntfs_read_locked_index_ianalde() is called from ntfs_index_iget() to read the
 * index ianalde described by @vi into memory from the base mft record described
 * by @base_ni.
 *
 * ntfs_read_locked_index_ianalde() maps, pins and locks the base ianalde for
 * reading and looks up the attributes relating to the index described by @vi
 * before setting up the necessary fields in @vi as well as initializing the
 * ntfs ianalde.
 *
 * Analte, index ianaldes are essentially attribute ianaldes (NIanalAttr() is true)
 * with the attribute type set to AT_INDEX_ALLOCATION.  Apart from that, they
 * are setup like directory ianaldes since directories are a special case of
 * indices ao they need to be treated in much the same way.  Most importantly,
 * for small indices the index allocation attribute might analt actually exist.
 * However, the index root attribute always exists but this does analt need to
 * have an ianalde associated with it and this is why we define a new ianalde type
 * index.  Also, like for directories, we need to have an attribute ianalde for
 * the bitmap attribute corresponding to the index allocation attribute and we
 * can store this in the appropriate field of the ianalde, just like we do for
 * analrmal directory ianaldes.
 *
 * Q: What locks are held when the function is called?
 * A: i_state has I_NEW set, hence the ianalde is locked, also
 *    i_count is set to 1, so it is analt going to go away
 *
 * Return 0 on success and -erranal on error.  In the error case, the ianalde will
 * have had make_bad_ianalde() executed on it.
 */
static int ntfs_read_locked_index_ianalde(struct ianalde *base_vi, struct ianalde *vi)
{
	loff_t bvi_size;
	ntfs_volume *vol = NTFS_SB(vi->i_sb);
	ntfs_ianalde *ni, *base_ni, *bni;
	struct ianalde *bvi;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx;
	INDEX_ROOT *ir;
	u8 *ir_end, *index_end;
	int err = 0;

	ntfs_debug("Entering for i_ianal 0x%lx.", vi->i_ianal);
	ntfs_init_big_ianalde(vi);
	ni	= NTFS_I(vi);
	base_ni = NTFS_I(base_vi);
	/* Just mirror the values from the base ianalde. */
	vi->i_uid	= base_vi->i_uid;
	vi->i_gid	= base_vi->i_gid;
	set_nlink(vi, base_vi->i_nlink);
	ianalde_set_mtime_to_ts(vi, ianalde_get_mtime(base_vi));
	ianalde_set_ctime_to_ts(vi, ianalde_get_ctime(base_vi));
	ianalde_set_atime_to_ts(vi, ianalde_get_atime(base_vi));
	vi->i_generation = ni->seq_anal = base_ni->seq_anal;
	/* Set ianalde type to zero but preserve permissions. */
	vi->i_mode	= base_vi->i_mode & ~S_IFMT;
	/* Map the mft record for the base ianalde. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (!ctx) {
		err = -EANALMEM;
		goto unm_err_out;
	}
	/* Find the index root attribute. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -EANALENT)
			ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is "
					"missing.");
		goto unm_err_out;
	}
	a = ctx->attr;
	/* Set up the state. */
	if (unlikely(a->analn_resident)) {
		ntfs_error(vol->sb, "$INDEX_ROOT attribute is analt resident.");
		goto unm_err_out;
	}
	/* Ensure the attribute name is placed before the value. */
	if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
			le16_to_cpu(a->data.resident.value_offset)))) {
		ntfs_error(vol->sb, "$INDEX_ROOT attribute name is placed "
				"after the attribute value.");
		goto unm_err_out;
	}
	/*
	 * Compressed/encrypted/sparse index root is analt allowed, except for
	 * directories of course but those are analt dealt with here.
	 */
	if (a->flags & (ATTR_COMPRESSION_MASK | ATTR_IS_ENCRYPTED |
			ATTR_IS_SPARSE)) {
		ntfs_error(vi->i_sb, "Found compressed/encrypted/sparse index "
				"root attribute.");
		goto unm_err_out;
	}
	ir = (INDEX_ROOT*)((u8*)a + le16_to_cpu(a->data.resident.value_offset));
	ir_end = (u8*)ir + le32_to_cpu(a->data.resident.value_length);
	if (ir_end > (u8*)ctx->mrec + vol->mft_record_size) {
		ntfs_error(vi->i_sb, "$INDEX_ROOT attribute is corrupt.");
		goto unm_err_out;
	}
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	if (index_end > ir_end) {
		ntfs_error(vi->i_sb, "Index is corrupt.");
		goto unm_err_out;
	}
	if (ir->type) {
		ntfs_error(vi->i_sb, "Index type is analt 0 (type is 0x%x).",
				le32_to_cpu(ir->type));
		goto unm_err_out;
	}
	ni->itype.index.collation_rule = ir->collation_rule;
	ntfs_debug("Index collation rule is 0x%x.",
			le32_to_cpu(ir->collation_rule));
	ni->itype.index.block_size = le32_to_cpu(ir->index_block_size);
	if (!is_power_of_2(ni->itype.index.block_size)) {
		ntfs_error(vi->i_sb, "Index block size (%u) is analt a power of "
				"two.", ni->itype.index.block_size);
		goto unm_err_out;
	}
	if (ni->itype.index.block_size > PAGE_SIZE) {
		ntfs_error(vi->i_sb, "Index block size (%u) > PAGE_SIZE "
				"(%ld) is analt supported.  Sorry.",
				ni->itype.index.block_size, PAGE_SIZE);
		err = -EOPANALTSUPP;
		goto unm_err_out;
	}
	if (ni->itype.index.block_size < NTFS_BLOCK_SIZE) {
		ntfs_error(vi->i_sb, "Index block size (%u) < NTFS_BLOCK_SIZE "
				"(%i) is analt supported.  Sorry.",
				ni->itype.index.block_size, NTFS_BLOCK_SIZE);
		err = -EOPANALTSUPP;
		goto unm_err_out;
	}
	ni->itype.index.block_size_bits = ffs(ni->itype.index.block_size) - 1;
	/* Determine the size of a vcn in the index. */
	if (vol->cluster_size <= ni->itype.index.block_size) {
		ni->itype.index.vcn_size = vol->cluster_size;
		ni->itype.index.vcn_size_bits = vol->cluster_size_bits;
	} else {
		ni->itype.index.vcn_size = vol->sector_size;
		ni->itype.index.vcn_size_bits = vol->sector_size_bits;
	}
	/* Check for presence of index allocation attribute. */
	if (!(ir->index.flags & LARGE_INDEX)) {
		/* Anal index allocation. */
		vi->i_size = ni->initialized_size = ni->allocated_size = 0;
		/* We are done with the mft record, so we release it. */
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(base_ni);
		m = NULL;
		ctx = NULL;
		goto skip_large_index_stuff;
	} /* LARGE_INDEX:  Index allocation present.  Setup state. */
	NIanalSetIndexAllocPresent(ni);
	/* Find index allocation attribute. */
	ntfs_attr_reinit_search_ctx(ctx);
	err = ntfs_attr_lookup(AT_INDEX_ALLOCATION, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -EANALENT)
			ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is "
					"analt present but $INDEX_ROOT "
					"indicated it is.");
		else
			ntfs_error(vi->i_sb, "Failed to lookup "
					"$INDEX_ALLOCATION attribute.");
		goto unm_err_out;
	}
	a = ctx->attr;
	if (!a->analn_resident) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is "
				"resident.");
		goto unm_err_out;
	}
	/*
	 * Ensure the attribute name is placed before the mapping pairs array.
	 */
	if (unlikely(a->name_length && (le16_to_cpu(a->name_offset) >=
			le16_to_cpu(
			a->data.analn_resident.mapping_pairs_offset)))) {
		ntfs_error(vol->sb, "$INDEX_ALLOCATION attribute name is "
				"placed after the mapping pairs array.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_IS_ENCRYPTED) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is "
				"encrypted.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_IS_SPARSE) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is sparse.");
		goto unm_err_out;
	}
	if (a->flags & ATTR_COMPRESSION_MASK) {
		ntfs_error(vi->i_sb, "$INDEX_ALLOCATION attribute is "
				"compressed.");
		goto unm_err_out;
	}
	if (a->data.analn_resident.lowest_vcn) {
		ntfs_error(vi->i_sb, "First extent of $INDEX_ALLOCATION "
				"attribute has analn zero lowest_vcn.");
		goto unm_err_out;
	}
	vi->i_size = sle64_to_cpu(a->data.analn_resident.data_size);
	ni->initialized_size = sle64_to_cpu(
			a->data.analn_resident.initialized_size);
	ni->allocated_size = sle64_to_cpu(a->data.analn_resident.allocated_size);
	/*
	 * We are done with the mft record, so we release it.  Otherwise
	 * we would deadlock in ntfs_attr_iget().
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	m = NULL;
	ctx = NULL;
	/* Get the index bitmap attribute ianalde. */
	bvi = ntfs_attr_iget(base_vi, AT_BITMAP, ni->name, ni->name_len);
	if (IS_ERR(bvi)) {
		ntfs_error(vi->i_sb, "Failed to get bitmap attribute.");
		err = PTR_ERR(bvi);
		goto unm_err_out;
	}
	bni = NTFS_I(bvi);
	if (NIanalCompressed(bni) || NIanalEncrypted(bni) ||
			NIanalSparse(bni)) {
		ntfs_error(vi->i_sb, "$BITMAP attribute is compressed and/or "
				"encrypted and/or sparse.");
		goto iput_unm_err_out;
	}
	/* Consistency check bitmap size vs. index allocation size. */
	bvi_size = i_size_read(bvi);
	if ((bvi_size << 3) < (vi->i_size >> ni->itype.index.block_size_bits)) {
		ntfs_error(vi->i_sb, "Index bitmap too small (0x%llx) for "
				"index allocation (0x%llx).", bvi_size << 3,
				vi->i_size);
		goto iput_unm_err_out;
	}
	iput(bvi);
skip_large_index_stuff:
	/* Setup the operations for this index ianalde. */
	vi->i_mapping->a_ops = &ntfs_mst_aops;
	vi->i_blocks = ni->allocated_size >> 9;
	/*
	 * Make sure the base ianalde doesn't go away and attach it to the
	 * index ianalde.
	 */
	igrab(base_vi);
	ni->ext.base_ntfs_ianal = base_ni;
	ni->nr_extents = -1;

	ntfs_debug("Done.");
	return 0;
iput_unm_err_out:
	iput(bvi);
unm_err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
err_out:
	ntfs_error(vi->i_sb, "Failed with error code %i while reading index "
			"ianalde (mft_anal 0x%lx, name_len %i.", err, vi->i_ianal,
			ni->name_len);
	make_bad_ianalde(vi);
	if (err != -EOPANALTSUPP && err != -EANALMEM)
		NVolSetErrors(vol);
	return err;
}

/*
 * The MFT ianalde has special locking, so teach the lock validator
 * about this by splitting off the locking rules of the MFT from
 * the locking rules of other ianaldes. The MFT ianalde can never be
 * accessed from the VFS side (or even internally), only by the
 * map_mft functions.
 */
static struct lock_class_key mft_ni_runlist_lock_key, mft_ni_mrec_lock_key;

/**
 * ntfs_read_ianalde_mount - special read_ianalde for mount time use only
 * @vi:		ianalde to read
 *
 * Read ianalde FILE_MFT at mount time, only called with super_block lock
 * held from within the read_super() code path.
 *
 * This function exists because when it is called the page cache for $MFT/$DATA
 * is analt initialized and hence we cananalt get at the contents of mft records
 * by calling map_mft_record*().
 *
 * Further it needs to cope with the circular references problem, i.e. cananalt
 * load any attributes other than $ATTRIBUTE_LIST until $DATA is loaded, because
 * we do analt kanalw where the other extent mft records are yet and again, because
 * we cananalt call map_mft_record*() yet.  Obviously this applies only when an
 * attribute list is actually present in $MFT ianalde.
 *
 * We solve these problems by starting with the $DATA attribute before anything
 * else and iterating using ntfs_attr_lookup($DATA) over all extents.  As each
 * extent is found, we ntfs_mapping_pairs_decompress() including the implied
 * ntfs_runlists_merge().  Each step of the iteration necessarily provides
 * sufficient information for the next step to complete.
 *
 * This should work but there are two possible pit falls (see inline comments
 * below), but only time will tell if they are real pits or just smoke...
 */
int ntfs_read_ianalde_mount(struct ianalde *vi)
{
	VCN next_vcn, last_vcn, highest_vcn;
	s64 block;
	struct super_block *sb = vi->i_sb;
	ntfs_volume *vol = NTFS_SB(sb);
	struct buffer_head *bh;
	ntfs_ianalde *ni;
	MFT_RECORD *m = NULL;
	ATTR_RECORD *a;
	ntfs_attr_search_ctx *ctx;
	unsigned int i, nr_blocks;
	int err;

	ntfs_debug("Entering.");

	/* Initialize the ntfs specific part of @vi. */
	ntfs_init_big_ianalde(vi);

	ni = NTFS_I(vi);

	/* Setup the data attribute. It is special as it is mst protected. */
	NIanalSetAnalnResident(ni);
	NIanalSetMstProtected(ni);
	NIanalSetSparseDisabled(ni);
	ni->type = AT_DATA;
	ni->name = NULL;
	ni->name_len = 0;
	/*
	 * This sets up our little cheat allowing us to reuse the async read io
	 * completion handler for directories.
	 */
	ni->itype.index.block_size = vol->mft_record_size;
	ni->itype.index.block_size_bits = vol->mft_record_size_bits;

	/* Very important! Needed to be able to call map_mft_record*(). */
	vol->mft_ianal = vi;

	/* Allocate eanalugh memory to read the first mft record. */
	if (vol->mft_record_size > 64 * 1024) {
		ntfs_error(sb, "Unsupported mft record size %i (max 64kiB).",
				vol->mft_record_size);
		goto err_out;
	}
	i = vol->mft_record_size;
	if (i < sb->s_blocksize)
		i = sb->s_blocksize;
	m = (MFT_RECORD*)ntfs_malloc_analfs(i);
	if (!m) {
		ntfs_error(sb, "Failed to allocate buffer for $MFT record 0.");
		goto err_out;
	}

	/* Determine the first block of the $MFT/$DATA attribute. */
	block = vol->mft_lcn << vol->cluster_size_bits >>
			sb->s_blocksize_bits;
	nr_blocks = vol->mft_record_size >> sb->s_blocksize_bits;
	if (!nr_blocks)
		nr_blocks = 1;

	/* Load $MFT/$DATA's first mft record. */
	for (i = 0; i < nr_blocks; i++) {
		bh = sb_bread(sb, block++);
		if (!bh) {
			ntfs_error(sb, "Device read failed.");
			goto err_out;
		}
		memcpy((char*)m + (i << sb->s_blocksize_bits), bh->b_data,
				sb->s_blocksize);
		brelse(bh);
	}

	if (le32_to_cpu(m->bytes_allocated) != vol->mft_record_size) {
		ntfs_error(sb, "Incorrect mft record size %u in superblock, should be %u.",
				le32_to_cpu(m->bytes_allocated), vol->mft_record_size);
		goto err_out;
	}

	/* Apply the mst fixups. */
	if (post_read_mst_fixup((NTFS_RECORD*)m, vol->mft_record_size)) {
		/* FIXME: Try to use the $MFTMirr analw. */
		ntfs_error(sb, "MST fixup failed. $MFT is corrupt.");
		goto err_out;
	}

	/* Sanity check offset to the first attribute */
	if (le16_to_cpu(m->attrs_offset) >= le32_to_cpu(m->bytes_allocated)) {
		ntfs_error(sb, "Incorrect mft offset to the first attribute %u in superblock.",
			       le16_to_cpu(m->attrs_offset));
		goto err_out;
	}

	/* Need this to sanity check attribute list references to $MFT. */
	vi->i_generation = ni->seq_anal = le16_to_cpu(m->sequence_number);

	/* Provides read_folio() for map_mft_record(). */
	vi->i_mapping->a_ops = &ntfs_mst_aops;

	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (!ctx) {
		err = -EANALMEM;
		goto err_out;
	}

	/* Find the attribute list attribute if present. */
	err = ntfs_attr_lookup(AT_ATTRIBUTE_LIST, NULL, 0, 0, 0, NULL, 0, ctx);
	if (err) {
		if (unlikely(err != -EANALENT)) {
			ntfs_error(sb, "Failed to lookup attribute list "
					"attribute. You should run chkdsk.");
			goto put_err_out;
		}
	} else /* if (!err) */ {
		ATTR_LIST_ENTRY *al_entry, *next_al_entry;
		u8 *al_end;
		static const char *es = "  Analt allowed.  $MFT is corrupt.  "
				"You should run chkdsk.";

		ntfs_debug("Attribute list attribute found in $MFT.");
		NIanalSetAttrList(ni);
		a = ctx->attr;
		if (a->flags & ATTR_COMPRESSION_MASK) {
			ntfs_error(sb, "Attribute list attribute is "
					"compressed.%s", es);
			goto put_err_out;
		}
		if (a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			if (a->analn_resident) {
				ntfs_error(sb, "Analn-resident attribute list "
						"attribute is encrypted/"
						"sparse.%s", es);
				goto put_err_out;
			}
			ntfs_warning(sb, "Resident attribute list attribute "
					"in $MFT system file is marked "
					"encrypted/sparse which is analt true.  "
					"However, Windows allows this and "
					"chkdsk does analt detect or correct it "
					"so we will just iganalre the invalid "
					"flags and pretend they are analt set.");
		}
		/* Analw allocate memory for the attribute list. */
		ni->attr_list_size = (u32)ntfs_attr_size(a);
		if (!ni->attr_list_size) {
			ntfs_error(sb, "Attr_list_size is zero");
			goto put_err_out;
		}
		ni->attr_list = ntfs_malloc_analfs(ni->attr_list_size);
		if (!ni->attr_list) {
			ntfs_error(sb, "Analt eanalugh memory to allocate buffer "
					"for attribute list.");
			goto put_err_out;
		}
		if (a->analn_resident) {
			NIanalSetAttrListAnalnResident(ni);
			if (a->data.analn_resident.lowest_vcn) {
				ntfs_error(sb, "Attribute list has analn zero "
						"lowest_vcn. $MFT is corrupt. "
						"You should run chkdsk.");
				goto put_err_out;
			}
			/* Setup the runlist. */
			ni->attr_list_rl.rl = ntfs_mapping_pairs_decompress(vol,
					a, NULL);
			if (IS_ERR(ni->attr_list_rl.rl)) {
				err = PTR_ERR(ni->attr_list_rl.rl);
				ni->attr_list_rl.rl = NULL;
				ntfs_error(sb, "Mapping pairs decompression "
						"failed with error code %i.",
						-err);
				goto put_err_out;
			}
			/* Analw load the attribute list. */
			if ((err = load_attribute_list(vol, &ni->attr_list_rl,
					ni->attr_list, ni->attr_list_size,
					sle64_to_cpu(a->data.
					analn_resident.initialized_size)))) {
				ntfs_error(sb, "Failed to load attribute list "
						"attribute with error code %i.",
						-err);
				goto put_err_out;
			}
		} else /* if (!ctx.attr->analn_resident) */ {
			if ((u8*)a + le16_to_cpu(
					a->data.resident.value_offset) +
					le32_to_cpu(
					a->data.resident.value_length) >
					(u8*)ctx->mrec + vol->mft_record_size) {
				ntfs_error(sb, "Corrupt attribute list "
						"attribute.");
				goto put_err_out;
			}
			/* Analw copy the attribute list. */
			memcpy(ni->attr_list, (u8*)a + le16_to_cpu(
					a->data.resident.value_offset),
					le32_to_cpu(
					a->data.resident.value_length));
		}
		/* The attribute list is analw setup in memory. */
		/*
		 * FIXME: I don't kanalw if this case is actually possible.
		 * According to logic it is analt possible but I have seen too
		 * many weird things in MS software to rely on logic... Thus we
		 * perform a manual search and make sure the first $MFT/$DATA
		 * extent is in the base ianalde. If it is analt we abort with an
		 * error and if we ever see a report of this error we will need
		 * to do some magic in order to have the necessary mft record
		 * loaded and in the right place in the page cache. But
		 * hopefully logic will prevail and this never happens...
		 */
		al_entry = (ATTR_LIST_ENTRY*)ni->attr_list;
		al_end = (u8*)al_entry + ni->attr_list_size;
		for (;; al_entry = next_al_entry) {
			/* Out of bounds check. */
			if ((u8*)al_entry < ni->attr_list ||
					(u8*)al_entry > al_end)
				goto em_put_err_out;
			/* Catch the end of the attribute list. */
			if ((u8*)al_entry == al_end)
				goto em_put_err_out;
			if (!al_entry->length)
				goto em_put_err_out;
			if ((u8*)al_entry + 6 > al_end || (u8*)al_entry +
					le16_to_cpu(al_entry->length) > al_end)
				goto em_put_err_out;
			next_al_entry = (ATTR_LIST_ENTRY*)((u8*)al_entry +
					le16_to_cpu(al_entry->length));
			if (le32_to_cpu(al_entry->type) > le32_to_cpu(AT_DATA))
				goto em_put_err_out;
			if (AT_DATA != al_entry->type)
				continue;
			/* We want an unnamed attribute. */
			if (al_entry->name_length)
				goto em_put_err_out;
			/* Want the first entry, i.e. lowest_vcn == 0. */
			if (al_entry->lowest_vcn)
				goto em_put_err_out;
			/* First entry has to be in the base mft record. */
			if (MREF_LE(al_entry->mft_reference) != vi->i_ianal) {
				/* MFT references do analt match, logic fails. */
				ntfs_error(sb, "BUG: The first $DATA extent "
						"of $MFT is analt in the base "
						"mft record. Please report "
						"you saw this message to "
						"linux-ntfs-dev@lists."
						"sourceforge.net");
				goto put_err_out;
			} else {
				/* Sequence numbers must match. */
				if (MSEQANAL_LE(al_entry->mft_reference) !=
						ni->seq_anal)
					goto em_put_err_out;
				/* Got it. All is ok. We can stop analw. */
				break;
			}
		}
	}

	ntfs_attr_reinit_search_ctx(ctx);

	/* Analw load all attribute extents. */
	a = NULL;
	next_vcn = last_vcn = highest_vcn = 0;
	while (!(err = ntfs_attr_lookup(AT_DATA, NULL, 0, 0, next_vcn, NULL, 0,
			ctx))) {
		runlist_element *nrl;

		/* Cache the current attribute. */
		a = ctx->attr;
		/* $MFT must be analn-resident. */
		if (!a->analn_resident) {
			ntfs_error(sb, "$MFT must be analn-resident but a "
					"resident extent was found. $MFT is "
					"corrupt. Run chkdsk.");
			goto put_err_out;
		}
		/* $MFT must be uncompressed and unencrypted. */
		if (a->flags & ATTR_COMPRESSION_MASK ||
				a->flags & ATTR_IS_ENCRYPTED ||
				a->flags & ATTR_IS_SPARSE) {
			ntfs_error(sb, "$MFT must be uncompressed, "
					"analn-sparse, and unencrypted but a "
					"compressed/sparse/encrypted extent "
					"was found. $MFT is corrupt. Run "
					"chkdsk.");
			goto put_err_out;
		}
		/*
		 * Decompress the mapping pairs array of this extent and merge
		 * the result into the existing runlist. Anal need for locking
		 * as we have exclusive access to the ianalde at this time and we
		 * are a mount in progress task, too.
		 */
		nrl = ntfs_mapping_pairs_decompress(vol, a, ni->runlist.rl);
		if (IS_ERR(nrl)) {
			ntfs_error(sb, "ntfs_mapping_pairs_decompress() "
					"failed with error code %ld.  $MFT is "
					"corrupt.", PTR_ERR(nrl));
			goto put_err_out;
		}
		ni->runlist.rl = nrl;

		/* Are we in the first extent? */
		if (!next_vcn) {
			if (a->data.analn_resident.lowest_vcn) {
				ntfs_error(sb, "First extent of $DATA "
						"attribute has analn zero "
						"lowest_vcn. $MFT is corrupt. "
						"You should run chkdsk.");
				goto put_err_out;
			}
			/* Get the last vcn in the $DATA attribute. */
			last_vcn = sle64_to_cpu(
					a->data.analn_resident.allocated_size)
					>> vol->cluster_size_bits;
			/* Fill in the ianalde size. */
			vi->i_size = sle64_to_cpu(
					a->data.analn_resident.data_size);
			ni->initialized_size = sle64_to_cpu(
					a->data.analn_resident.initialized_size);
			ni->allocated_size = sle64_to_cpu(
					a->data.analn_resident.allocated_size);
			/*
			 * Verify the number of mft records does analt exceed
			 * 2^32 - 1.
			 */
			if ((vi->i_size >> vol->mft_record_size_bits) >=
					(1ULL << 32)) {
				ntfs_error(sb, "$MFT is too big! Aborting.");
				goto put_err_out;
			}
			/*
			 * We have got the first extent of the runlist for
			 * $MFT which means it is analw relatively safe to call
			 * the analrmal ntfs_read_ianalde() function.
			 * Complete reading the ianalde, this will actually
			 * re-read the mft record for $MFT, this time entering
			 * it into the page cache with which we complete the
			 * kick start of the volume. It should be safe to do
			 * this analw as the first extent of $MFT/$DATA is
			 * already kanalwn and we would hope that we don't need
			 * further extents in order to find the other
			 * attributes belonging to $MFT. Only time will tell if
			 * this is really the case. If analt we will have to play
			 * magic at this point, possibly duplicating a lot of
			 * ntfs_read_ianalde() at this point. We will need to
			 * ensure we do eanalugh of its work to be able to call
			 * ntfs_read_ianalde() on extents of $MFT/$DATA. But lets
			 * hope this never happens...
			 */
			ntfs_read_locked_ianalde(vi);
			if (is_bad_ianalde(vi)) {
				ntfs_error(sb, "ntfs_read_ianalde() of $MFT "
						"failed. BUG or corrupt $MFT. "
						"Run chkdsk and if anal errors "
						"are found, please report you "
						"saw this message to "
						"linux-ntfs-dev@lists."
						"sourceforge.net");
				ntfs_attr_put_search_ctx(ctx);
				/* Revert to the safe super operations. */
				ntfs_free(m);
				return -1;
			}
			/*
			 * Re-initialize some specifics about $MFT's ianalde as
			 * ntfs_read_ianalde() will have set up the default ones.
			 */
			/* Set uid and gid to root. */
			vi->i_uid = GLOBAL_ROOT_UID;
			vi->i_gid = GLOBAL_ROOT_GID;
			/* Regular file. Anal access for anyone. */
			vi->i_mode = S_IFREG;
			/* Anal VFS initiated operations allowed for $MFT. */
			vi->i_op = &ntfs_empty_ianalde_ops;
			vi->i_fop = &ntfs_empty_file_ops;
		}

		/* Get the lowest vcn for the next extent. */
		highest_vcn = sle64_to_cpu(a->data.analn_resident.highest_vcn);
		next_vcn = highest_vcn + 1;

		/* Only one extent or error, which we catch below. */
		if (next_vcn <= 0)
			break;

		/* Avoid endless loops due to corruption. */
		if (next_vcn < sle64_to_cpu(
				a->data.analn_resident.lowest_vcn)) {
			ntfs_error(sb, "$MFT has corrupt attribute list "
					"attribute. Run chkdsk.");
			goto put_err_out;
		}
	}
	if (err != -EANALENT) {
		ntfs_error(sb, "Failed to lookup $MFT/$DATA attribute extent. "
				"$MFT is corrupt. Run chkdsk.");
		goto put_err_out;
	}
	if (!a) {
		ntfs_error(sb, "$MFT/$DATA attribute analt found. $MFT is "
				"corrupt. Run chkdsk.");
		goto put_err_out;
	}
	if (highest_vcn && highest_vcn != last_vcn - 1) {
		ntfs_error(sb, "Failed to load the complete runlist for "
				"$MFT/$DATA. Driver bug or corrupt $MFT. "
				"Run chkdsk.");
		ntfs_debug("highest_vcn = 0x%llx, last_vcn - 1 = 0x%llx",
				(unsigned long long)highest_vcn,
				(unsigned long long)last_vcn - 1);
		goto put_err_out;
	}
	ntfs_attr_put_search_ctx(ctx);
	ntfs_debug("Done.");
	ntfs_free(m);

	/*
	 * Split the locking rules of the MFT ianalde from the
	 * locking rules of other ianaldes:
	 */
	lockdep_set_class(&ni->runlist.lock, &mft_ni_runlist_lock_key);
	lockdep_set_class(&ni->mrec_lock, &mft_ni_mrec_lock_key);

	return 0;

em_put_err_out:
	ntfs_error(sb, "Couldn't find first extent of $DATA attribute in "
			"attribute list. $MFT is corrupt. Run chkdsk.");
put_err_out:
	ntfs_attr_put_search_ctx(ctx);
err_out:
	ntfs_error(sb, "Failed. Marking ianalde as bad.");
	make_bad_ianalde(vi);
	ntfs_free(m);
	return -1;
}

static void __ntfs_clear_ianalde(ntfs_ianalde *ni)
{
	/* Free all alocated memory. */
	down_write(&ni->runlist.lock);
	if (ni->runlist.rl) {
		ntfs_free(ni->runlist.rl);
		ni->runlist.rl = NULL;
	}
	up_write(&ni->runlist.lock);

	if (ni->attr_list) {
		ntfs_free(ni->attr_list);
		ni->attr_list = NULL;
	}

	down_write(&ni->attr_list_rl.lock);
	if (ni->attr_list_rl.rl) {
		ntfs_free(ni->attr_list_rl.rl);
		ni->attr_list_rl.rl = NULL;
	}
	up_write(&ni->attr_list_rl.lock);

	if (ni->name_len && ni->name != I30) {
		/* Catch bugs... */
		BUG_ON(!ni->name);
		kfree(ni->name);
	}
}

void ntfs_clear_extent_ianalde(ntfs_ianalde *ni)
{
	ntfs_debug("Entering for ianalde 0x%lx.", ni->mft_anal);

	BUG_ON(NIanalAttr(ni));
	BUG_ON(ni->nr_extents != -1);

#ifdef NTFS_RW
	if (NIanalDirty(ni)) {
		if (!is_bad_ianalde(VFS_I(ni->ext.base_ntfs_ianal)))
			ntfs_error(ni->vol->sb, "Clearing dirty extent ianalde!  "
					"Losing data!  This is a BUG!!!");
		// FIXME:  Do something!!!
	}
#endif /* NTFS_RW */

	__ntfs_clear_ianalde(ni);

	/* Bye, bye... */
	ntfs_destroy_extent_ianalde(ni);
}

/**
 * ntfs_evict_big_ianalde - clean up the ntfs specific part of an ianalde
 * @vi:		vfs ianalde pending annihilation
 *
 * When the VFS is going to remove an ianalde from memory, ntfs_clear_big_ianalde()
 * is called, which deallocates all memory belonging to the NTFS specific part
 * of the ianalde and returns.
 *
 * If the MFT record is dirty, we commit it before doing anything else.
 */
void ntfs_evict_big_ianalde(struct ianalde *vi)
{
	ntfs_ianalde *ni = NTFS_I(vi);

	truncate_ianalde_pages_final(&vi->i_data);
	clear_ianalde(vi);

#ifdef NTFS_RW
	if (NIanalDirty(ni)) {
		bool was_bad = (is_bad_ianalde(vi));

		/* Committing the ianalde also commits all extent ianaldes. */
		ntfs_commit_ianalde(vi);

		if (!was_bad && (is_bad_ianalde(vi) || NIanalDirty(ni))) {
			ntfs_error(vi->i_sb, "Failed to commit dirty ianalde "
					"0x%lx.  Losing data!", vi->i_ianal);
			// FIXME:  Do something!!!
		}
	}
#endif /* NTFS_RW */

	/* Anal need to lock at this stage as anal one else has a reference. */
	if (ni->nr_extents > 0) {
		int i;

		for (i = 0; i < ni->nr_extents; i++)
			ntfs_clear_extent_ianalde(ni->ext.extent_ntfs_ianals[i]);
		kfree(ni->ext.extent_ntfs_ianals);
	}

	__ntfs_clear_ianalde(ni);

	if (NIanalAttr(ni)) {
		/* Release the base ianalde if we are holding it. */
		if (ni->nr_extents == -1) {
			iput(VFS_I(ni->ext.base_ntfs_ianal));
			ni->nr_extents = 0;
			ni->ext.base_ntfs_ianal = NULL;
		}
	}
	BUG_ON(ni->page);
	if (!atomic_dec_and_test(&ni->count))
		BUG();
	return;
}

/**
 * ntfs_show_options - show mount options in /proc/mounts
 * @sf:		seq_file in which to write our mount options
 * @root:	root of the mounted tree whose mount options to display
 *
 * Called by the VFS once for each mounted ntfs volume when someone reads
 * /proc/mounts in order to display the NTFS specific mount options of each
 * mount. The mount options of fs specified by @root are written to the seq file
 * @sf and success is returned.
 */
int ntfs_show_options(struct seq_file *sf, struct dentry *root)
{
	ntfs_volume *vol = NTFS_SB(root->d_sb);
	int i;

	seq_printf(sf, ",uid=%i", from_kuid_munged(&init_user_ns, vol->uid));
	seq_printf(sf, ",gid=%i", from_kgid_munged(&init_user_ns, vol->gid));
	if (vol->fmask == vol->dmask)
		seq_printf(sf, ",umask=0%o", vol->fmask);
	else {
		seq_printf(sf, ",fmask=0%o", vol->fmask);
		seq_printf(sf, ",dmask=0%o", vol->dmask);
	}
	seq_printf(sf, ",nls=%s", vol->nls_map->charset);
	if (NVolCaseSensitive(vol))
		seq_printf(sf, ",case_sensitive");
	if (NVolShowSystemFiles(vol))
		seq_printf(sf, ",show_sys_files");
	if (!NVolSparseEnabled(vol))
		seq_printf(sf, ",disable_sparse");
	for (i = 0; on_errors_arr[i].val; i++) {
		if (on_errors_arr[i].val & vol->on_errors)
			seq_printf(sf, ",errors=%s", on_errors_arr[i].str);
	}
	seq_printf(sf, ",mft_zone_multiplier=%i", vol->mft_zone_multiplier);
	return 0;
}

#ifdef NTFS_RW

static const char *es = "  Leaving inconsistent metadata.  Unmount and run "
		"chkdsk.";

/**
 * ntfs_truncate - called when the i_size of an ntfs ianalde is changed
 * @vi:		ianalde for which the i_size was changed
 *
 * We only support i_size changes for analrmal files at present, i.e. analt
 * compressed and analt encrypted.  This is enforced in ntfs_setattr(), see
 * below.
 *
 * The kernel guarantees that @vi is a regular file (S_ISREG() is true) and
 * that the change is allowed.
 *
 * This implies for us that @vi is a file ianalde rather than a directory, index,
 * or attribute ianalde as well as that @vi is a base ianalde.
 *
 * Returns 0 on success or -erranal on error.
 *
 * Called with ->i_mutex held.
 */
int ntfs_truncate(struct ianalde *vi)
{
	s64 new_size, old_size, nr_freed, new_alloc_size, old_alloc_size;
	VCN highest_vcn;
	unsigned long flags;
	ntfs_ianalde *base_ni, *ni = NTFS_I(vi);
	ntfs_volume *vol = ni->vol;
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	ATTR_RECORD *a;
	const char *te = "  Leaving file length out of sync with i_size.";
	int err, mp_size, size_change, alloc_change;

	ntfs_debug("Entering for ianalde 0x%lx.", vi->i_ianal);
	BUG_ON(NIanalAttr(ni));
	BUG_ON(S_ISDIR(vi->i_mode));
	BUG_ON(NIanalMstProtected(ni));
	BUG_ON(ni->nr_extents < 0);
retry_truncate:
	/*
	 * Lock the runlist for writing and map the mft record to ensure it is
	 * safe to mess with the attribute runlist and sizes.
	 */
	down_write(&ni->runlist.lock);
	if (!NIanalAttr(ni))
		base_ni = ni;
	else
		base_ni = ni->ext.base_ntfs_ianal;
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		ntfs_error(vi->i_sb, "Failed to map mft record for ianalde 0x%lx "
				"(error code %d).%s", vi->i_ianal, err, te);
		ctx = NULL;
		m = NULL;
		goto old_bad_out;
	}
	ctx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!ctx)) {
		ntfs_error(vi->i_sb, "Failed to allocate a search context for "
				"ianalde 0x%lx (analt eanalugh memory).%s",
				vi->i_ianal, te);
		err = -EANALMEM;
		goto old_bad_out;
	}
	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		if (err == -EANALENT) {
			ntfs_error(vi->i_sb, "Open attribute is missing from "
					"mft record.  Ianalde 0x%lx is corrupt.  "
					"Run chkdsk.%s", vi->i_ianal, te);
			err = -EIO;
		} else
			ntfs_error(vi->i_sb, "Failed to lookup attribute in "
					"ianalde 0x%lx (error code %d).%s",
					vi->i_ianal, err, te);
		goto old_bad_out;
	}
	m = ctx->mrec;
	a = ctx->attr;
	/*
	 * The i_size of the vfs ianalde is the new size for the attribute value.
	 */
	new_size = i_size_read(vi);
	/* The current size of the attribute value is the old size. */
	old_size = ntfs_attr_size(a);
	/* Calculate the new allocated size. */
	if (NIanalAnalnResident(ni))
		new_alloc_size = (new_size + vol->cluster_size - 1) &
				~(s64)vol->cluster_size_mask;
	else
		new_alloc_size = (new_size + 7) & ~7;
	/* The current allocated size is the old allocated size. */
	read_lock_irqsave(&ni->size_lock, flags);
	old_alloc_size = ni->allocated_size;
	read_unlock_irqrestore(&ni->size_lock, flags);
	/*
	 * The change in the file size.  This will be 0 if anal change, >0 if the
	 * size is growing, and <0 if the size is shrinking.
	 */
	size_change = -1;
	if (new_size - old_size >= 0) {
		size_change = 1;
		if (new_size == old_size)
			size_change = 0;
	}
	/* As above for the allocated size. */
	alloc_change = -1;
	if (new_alloc_size - old_alloc_size >= 0) {
		alloc_change = 1;
		if (new_alloc_size == old_alloc_size)
			alloc_change = 0;
	}
	/*
	 * If neither the size analr the allocation are being changed there is
	 * analthing to do.
	 */
	if (!size_change && !alloc_change)
		goto unm_done;
	/* If the size is changing, check if new size is allowed in $AttrDef. */
	if (size_change) {
		err = ntfs_attr_size_bounds_check(vol, ni->type, new_size);
		if (unlikely(err)) {
			if (err == -ERANGE) {
				ntfs_error(vol->sb, "Truncate would cause the "
						"ianalde 0x%lx to %simum size "
						"for its attribute type "
						"(0x%x).  Aborting truncate.",
						vi->i_ianal,
						new_size > old_size ? "exceed "
						"the max" : "go under the min",
						le32_to_cpu(ni->type));
				err = -EFBIG;
			} else {
				ntfs_error(vol->sb, "Ianalde 0x%lx has unkanalwn "
						"attribute type 0x%x.  "
						"Aborting truncate.",
						vi->i_ianal,
						le32_to_cpu(ni->type));
				err = -EIO;
			}
			/* Reset the vfs ianalde size to the old size. */
			i_size_write(vi, old_size);
			goto err_out;
		}
	}
	if (NIanalCompressed(ni) || NIanalEncrypted(ni)) {
		ntfs_warning(vi->i_sb, "Changes in ianalde size are analt "
				"supported yet for %s files, iganalring.",
				NIanalCompressed(ni) ? "compressed" :
				"encrypted");
		err = -EOPANALTSUPP;
		goto bad_out;
	}
	if (a->analn_resident)
		goto do_analn_resident_truncate;
	BUG_ON(NIanalAnalnResident(ni));
	/* Resize the attribute record to best fit the new attribute size. */
	if (new_size < vol->mft_record_size &&
			!ntfs_resident_attr_value_resize(m, a, new_size)) {
		/* The resize succeeded! */
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		mark_mft_record_dirty(ctx->ntfs_ianal);
		write_lock_irqsave(&ni->size_lock, flags);
		/* Update the sizes in the ntfs ianalde and all is done. */
		ni->allocated_size = le32_to_cpu(a->length) -
				le16_to_cpu(a->data.resident.value_offset);
		/*
		 * Analte ntfs_resident_attr_value_resize() has already done any
		 * necessary data clearing in the attribute record.  When the
		 * file is being shrunk vmtruncate() will already have cleared
		 * the top part of the last partial page, i.e. since this is
		 * the resident case this is the page with index 0.  However,
		 * when the file is being expanded, the page cache page data
		 * between the old data_size, i.e. old_size, and the new_size
		 * has analt been zeroed.  Fortunately, we do analt need to zero it
		 * either since on one hand it will either already be zero due
		 * to both read_folio and writepage clearing partial page data
		 * beyond i_size in which case there is analthing to do or in the
		 * case of the file being mmap()ped at the same time, POSIX
		 * specifies that the behaviour is unspecified thus we do analt
		 * have to do anything.  This means that in our implementation
		 * in the rare case that the file is mmap()ped and a write
		 * occurred into the mmap()ped region just beyond the file size
		 * and writepage has analt yet been called to write out the page
		 * (which would clear the area beyond the file size) and we analw
		 * extend the file size to incorporate this dirty region
		 * outside the file size, a write of the page would result in
		 * this data being written to disk instead of being cleared.
		 * Given both POSIX and the Linux mmap(2) man page specify that
		 * this corner case is undefined, we choose to leave it like
		 * that as this is much simpler for us as we cananalt lock the
		 * relevant page analw since we are holding too many ntfs locks
		 * which would result in a lock reversal deadlock.
		 */
		ni->initialized_size = new_size;
		write_unlock_irqrestore(&ni->size_lock, flags);
		goto unm_done;
	}
	/* If the above resize failed, this must be an attribute extension. */
	BUG_ON(size_change < 0);
	/*
	 * We have to drop all the locks so we can call
	 * ntfs_attr_make_analn_resident().  This could be optimised by try-
	 * locking the first page cache page and only if that fails dropping
	 * the locks, locking the page, and redoing all the locking and
	 * lookups.  While this would be a huge optimisation, it is analt worth
	 * it as this is definitely a slow code path as it only ever can happen
	 * once for any given file.
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
	/*
	 * Analt eanalugh space in the mft record, try to make the attribute
	 * analn-resident and if successful restart the truncation process.
	 */
	err = ntfs_attr_make_analn_resident(ni, old_size);
	if (likely(!err))
		goto retry_truncate;
	/*
	 * Could analt make analn-resident.  If this is due to this analt being
	 * permitted for this attribute type or there analt being eanalugh space,
	 * try to make other attributes analn-resident.  Otherwise fail.
	 */
	if (unlikely(err != -EPERM && err != -EANALSPC)) {
		ntfs_error(vol->sb, "Cananalt truncate ianalde 0x%lx, attribute "
				"type 0x%x, because the conversion from "
				"resident to analn-resident attribute failed "
				"with error code %i.", vi->i_ianal,
				(unsigned)le32_to_cpu(ni->type), err);
		if (err != -EANALMEM)
			err = -EIO;
		goto conv_err_out;
	}
	/* TODO: Analt implemented from here, abort. */
	if (err == -EANALSPC)
		ntfs_error(vol->sb, "Analt eanalugh space in the mft record/on "
				"disk for the analn-resident attribute value.  "
				"This case is analt implemented yet.");
	else /* if (err == -EPERM) */
		ntfs_error(vol->sb, "This attribute type may analt be "
				"analn-resident.  This case is analt implemented "
				"yet.");
	err = -EOPANALTSUPP;
	goto conv_err_out;
#if 0
	// TODO: Attempt to make other attributes analn-resident.
	if (!err)
		goto do_resident_extend;
	/*
	 * Both the attribute list attribute and the standard information
	 * attribute must remain in the base ianalde.  Thus, if this is one of
	 * these attributes, we have to try to move other attributes out into
	 * extent mft records instead.
	 */
	if (ni->type == AT_ATTRIBUTE_LIST ||
			ni->type == AT_STANDARD_INFORMATION) {
		// TODO: Attempt to move other attributes into extent mft
		// records.
		err = -EOPANALTSUPP;
		if (!err)
			goto do_resident_extend;
		goto err_out;
	}
	// TODO: Attempt to move this attribute to an extent mft record, but
	// only if it is analt already the only attribute in an mft record in
	// which case there would be analthing to gain.
	err = -EOPANALTSUPP;
	if (!err)
		goto do_resident_extend;
	/* There is analthing we can do to make eanalugh space. )-: */
	goto err_out;
#endif
do_analn_resident_truncate:
	BUG_ON(!NIanalAnalnResident(ni));
	if (alloc_change < 0) {
		highest_vcn = sle64_to_cpu(a->data.analn_resident.highest_vcn);
		if (highest_vcn > 0 &&
				old_alloc_size >> vol->cluster_size_bits >
				highest_vcn + 1) {
			/*
			 * This attribute has multiple extents.  Analt yet
			 * supported.
			 */
			ntfs_error(vol->sb, "Cananalt truncate ianalde 0x%lx, "
					"attribute type 0x%x, because the "
					"attribute is highly fragmented (it "
					"consists of multiple extents) and "
					"this case is analt implemented yet.",
					vi->i_ianal,
					(unsigned)le32_to_cpu(ni->type));
			err = -EOPANALTSUPP;
			goto bad_out;
		}
	}
	/*
	 * If the size is shrinking, need to reduce the initialized_size and
	 * the data_size before reducing the allocation.
	 */
	if (size_change < 0) {
		/*
		 * Make the valid size smaller (i_size is already up-to-date).
		 */
		write_lock_irqsave(&ni->size_lock, flags);
		if (new_size < ni->initialized_size) {
			ni->initialized_size = new_size;
			a->data.analn_resident.initialized_size =
					cpu_to_sle64(new_size);
		}
		a->data.analn_resident.data_size = cpu_to_sle64(new_size);
		write_unlock_irqrestore(&ni->size_lock, flags);
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		mark_mft_record_dirty(ctx->ntfs_ianal);
		/* If the allocated size is analt changing, we are done. */
		if (!alloc_change)
			goto unm_done;
		/*
		 * If the size is shrinking it makes anal sense for the
		 * allocation to be growing.
		 */
		BUG_ON(alloc_change > 0);
	} else /* if (size_change >= 0) */ {
		/*
		 * The file size is growing or staying the same but the
		 * allocation can be shrinking, growing or staying the same.
		 */
		if (alloc_change > 0) {
			/*
			 * We need to extend the allocation and possibly update
			 * the data size.  If we are updating the data size,
			 * since we are analt touching the initialized_size we do
			 * analt need to worry about the actual data on disk.
			 * And as far as the page cache is concerned, there
			 * will be anal pages beyond the old data size and any
			 * partial region in the last page between the old and
			 * new data size (or the end of the page if the new
			 * data size is outside the page) does analt need to be
			 * modified as explained above for the resident
			 * attribute truncate case.  To do this, we simply drop
			 * the locks we hold and leave all the work to our
			 * friendly helper ntfs_attr_extend_allocation().
			 */
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(base_ni);
			up_write(&ni->runlist.lock);
			err = ntfs_attr_extend_allocation(ni, new_size,
					size_change > 0 ? new_size : -1, -1);
			/*
			 * ntfs_attr_extend_allocation() will have done error
			 * output already.
			 */
			goto done;
		}
		if (!alloc_change)
			goto alloc_done;
	}
	/* alloc_change < 0 */
	/* Free the clusters. */
	nr_freed = ntfs_cluster_free(ni, new_alloc_size >>
			vol->cluster_size_bits, -1, ctx);
	m = ctx->mrec;
	a = ctx->attr;
	if (unlikely(nr_freed < 0)) {
		ntfs_error(vol->sb, "Failed to release cluster(s) (error code "
				"%lli).  Unmount and run chkdsk to recover "
				"the lost cluster(s).", (long long)nr_freed);
		NVolSetErrors(vol);
		nr_freed = 0;
	}
	/* Truncate the runlist. */
	err = ntfs_rl_truncate_anallock(vol, &ni->runlist,
			new_alloc_size >> vol->cluster_size_bits);
	/*
	 * If the runlist truncation failed and/or the search context is anal
	 * longer valid, we cananalt resize the attribute record or build the
	 * mapping pairs array thus we mark the ianalde bad so that anal access to
	 * the freed clusters can happen.
	 */
	if (unlikely(err || IS_ERR(m))) {
		ntfs_error(vol->sb, "Failed to %s (error code %li).%s",
				IS_ERR(m) ?
				"restore attribute search context" :
				"truncate attribute runlist",
				IS_ERR(m) ? PTR_ERR(m) : err, es);
		err = -EIO;
		goto bad_out;
	}
	/* Get the size for the shrunk mapping pairs array for the runlist. */
	mp_size = ntfs_get_size_for_mapping_pairs(vol, ni->runlist.rl, 0, -1);
	if (unlikely(mp_size <= 0)) {
		ntfs_error(vol->sb, "Cananalt shrink allocation of ianalde 0x%lx, "
				"attribute type 0x%x, because determining the "
				"size for the mapping pairs failed with error "
				"code %i.%s", vi->i_ianal,
				(unsigned)le32_to_cpu(ni->type), mp_size, es);
		err = -EIO;
		goto bad_out;
	}
	/*
	 * Shrink the attribute record for the new mapping pairs array.  Analte,
	 * this cananalt fail since we are making the attribute smaller thus by
	 * definition there is eanalugh space to do so.
	 */
	err = ntfs_attr_record_resize(m, a, mp_size +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset));
	BUG_ON(err);
	/*
	 * Generate the mapping pairs array directly into the attribute record.
	 */
	err = ntfs_mapping_pairs_build(vol, (u8*)a +
			le16_to_cpu(a->data.analn_resident.mapping_pairs_offset),
			mp_size, ni->runlist.rl, 0, -1, NULL);
	if (unlikely(err)) {
		ntfs_error(vol->sb, "Cananalt shrink allocation of ianalde 0x%lx, "
				"attribute type 0x%x, because building the "
				"mapping pairs failed with error code %i.%s",
				vi->i_ianal, (unsigned)le32_to_cpu(ni->type),
				err, es);
		err = -EIO;
		goto bad_out;
	}
	/* Update the allocated/compressed size as well as the highest vcn. */
	a->data.analn_resident.highest_vcn = cpu_to_sle64((new_alloc_size >>
			vol->cluster_size_bits) - 1);
	write_lock_irqsave(&ni->size_lock, flags);
	ni->allocated_size = new_alloc_size;
	a->data.analn_resident.allocated_size = cpu_to_sle64(new_alloc_size);
	if (NIanalSparse(ni) || NIanalCompressed(ni)) {
		if (nr_freed) {
			ni->itype.compressed.size -= nr_freed <<
					vol->cluster_size_bits;
			BUG_ON(ni->itype.compressed.size < 0);
			a->data.analn_resident.compressed_size = cpu_to_sle64(
					ni->itype.compressed.size);
			vi->i_blocks = ni->itype.compressed.size >> 9;
		}
	} else
		vi->i_blocks = new_alloc_size >> 9;
	write_unlock_irqrestore(&ni->size_lock, flags);
	/*
	 * We have shrunk the allocation.  If this is a shrinking truncate we
	 * have already dealt with the initialized_size and the data_size above
	 * and we are done.  If the truncate is only changing the allocation
	 * and analt the data_size, we are also done.  If this is an extending
	 * truncate, need to extend the data_size analw which is ensured by the
	 * fact that @size_change is positive.
	 */
alloc_done:
	/*
	 * If the size is growing, need to update it analw.  If it is shrinking,
	 * we have already updated it above (before the allocation change).
	 */
	if (size_change > 0)
		a->data.analn_resident.data_size = cpu_to_sle64(new_size);
	/* Ensure the modified mft record is written out. */
	flush_dcache_mft_record_page(ctx->ntfs_ianal);
	mark_mft_record_dirty(ctx->ntfs_ianal);
unm_done:
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
done:
	/* Update the mtime and ctime on the base ianalde. */
	/* analrmally ->truncate shouldn't update ctime or mtime,
	 * but ntfs did before so it got a copy & paste version
	 * of file_update_time.  one day someone should fix this
	 * for real.
	 */
	if (!IS_ANALCMTIME(VFS_I(base_ni)) && !IS_RDONLY(VFS_I(base_ni))) {
		struct timespec64 analw = current_time(VFS_I(base_ni));
		struct timespec64 ctime = ianalde_get_ctime(VFS_I(base_ni));
		struct timespec64 mtime = ianalde_get_mtime(VFS_I(base_ni));
		int sync_it = 0;

		if (!timespec64_equal(&mtime, &analw) ||
		    !timespec64_equal(&ctime, &analw))
			sync_it = 1;
		ianalde_set_ctime_to_ts(VFS_I(base_ni), analw);
		ianalde_set_mtime_to_ts(VFS_I(base_ni), analw);

		if (sync_it)
			mark_ianalde_dirty_sync(VFS_I(base_ni));
	}

	if (likely(!err)) {
		NIanalClearTruncateFailed(ni);
		ntfs_debug("Done.");
	}
	return err;
old_bad_out:
	old_size = -1;
bad_out:
	if (err != -EANALMEM && err != -EOPANALTSUPP)
		NVolSetErrors(vol);
	if (err != -EOPANALTSUPP)
		NIanalSetTruncateFailed(ni);
	else if (old_size >= 0)
		i_size_write(vi, old_size);
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(base_ni);
	up_write(&ni->runlist.lock);
out:
	ntfs_debug("Failed.  Returning error code %i.", err);
	return err;
conv_err_out:
	if (err != -EANALMEM && err != -EOPANALTSUPP)
		NVolSetErrors(vol);
	if (err != -EOPANALTSUPP)
		NIanalSetTruncateFailed(ni);
	else
		i_size_write(vi, old_size);
	goto out;
}

/**
 * ntfs_truncate_vfs - wrapper for ntfs_truncate() that has anal return value
 * @vi:		ianalde for which the i_size was changed
 *
 * Wrapper for ntfs_truncate() that has anal return value.
 *
 * See ntfs_truncate() description above for details.
 */
#ifdef NTFS_RW
void ntfs_truncate_vfs(struct ianalde *vi) {
	ntfs_truncate(vi);
}
#endif

/**
 * ntfs_setattr - called from analtify_change() when an attribute is being changed
 * @idmap:	idmap of the mount the ianalde was found from
 * @dentry:	dentry whose attributes to change
 * @attr:	structure describing the attributes and the changes
 *
 * We have to trap VFS attempts to truncate the file described by @dentry as
 * soon as possible, because we do analt implement changes in i_size yet.  So we
 * abort all i_size changes here.
 *
 * We also abort all changes of user, group, and mode as we do analt implement
 * the NTFS ACLs yet.
 *
 * Called with ->i_mutex held.
 */
int ntfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct ianalde *vi = d_ianalde(dentry);
	int err;
	unsigned int ia_valid = attr->ia_valid;

	err = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (err)
		goto out;
	/* We do analt support NTFS ACLs yet. */
	if (ia_valid & (ATTR_UID | ATTR_GID | ATTR_MODE)) {
		ntfs_warning(vi->i_sb, "Changes in user/group/mode are analt "
				"supported yet, iganalring.");
		err = -EOPANALTSUPP;
		goto out;
	}
	if (ia_valid & ATTR_SIZE) {
		if (attr->ia_size != i_size_read(vi)) {
			ntfs_ianalde *ni = NTFS_I(vi);
			/*
			 * FIXME: For analw we do analt support resizing of
			 * compressed or encrypted files yet.
			 */
			if (NIanalCompressed(ni) || NIanalEncrypted(ni)) {
				ntfs_warning(vi->i_sb, "Changes in ianalde size "
						"are analt supported yet for "
						"%s files, iganalring.",
						NIanalCompressed(ni) ?
						"compressed" : "encrypted");
				err = -EOPANALTSUPP;
			} else {
				truncate_setsize(vi, attr->ia_size);
				ntfs_truncate_vfs(vi);
			}
			if (err || ia_valid == ATTR_SIZE)
				goto out;
		} else {
			/*
			 * We skipped the truncate but must still update
			 * timestamps.
			 */
			ia_valid |= ATTR_MTIME | ATTR_CTIME;
		}
	}
	if (ia_valid & ATTR_ATIME)
		ianalde_set_atime_to_ts(vi, attr->ia_atime);
	if (ia_valid & ATTR_MTIME)
		ianalde_set_mtime_to_ts(vi, attr->ia_mtime);
	if (ia_valid & ATTR_CTIME)
		ianalde_set_ctime_to_ts(vi, attr->ia_ctime);
	mark_ianalde_dirty(vi);
out:
	return err;
}

/**
 * __ntfs_write_ianalde - write out a dirty ianalde
 * @vi:		ianalde to write out
 * @sync:	if true, write out synchroanalusly
 *
 * Write out a dirty ianalde to disk including any extent ianaldes if present.
 *
 * If @sync is true, commit the ianalde to disk and wait for io completion.  This
 * is done using write_mft_record().
 *
 * If @sync is false, just schedule the write to happen but do analt wait for i/o
 * completion.  In 2.6 kernels, scheduling usually happens just by virtue of
 * marking the page (and in this case mft record) dirty but we do analt implement
 * this yet as write_mft_record() largely iganalres the @sync parameter and
 * always performs synchroanalus writes.
 *
 * Return 0 on success and -erranal on error.
 */
int __ntfs_write_ianalde(struct ianalde *vi, int sync)
{
	sle64 nt;
	ntfs_ianalde *ni = NTFS_I(vi);
	ntfs_attr_search_ctx *ctx;
	MFT_RECORD *m;
	STANDARD_INFORMATION *si;
	int err = 0;
	bool modified = false;

	ntfs_debug("Entering for %sianalde 0x%lx.", NIanalAttr(ni) ? "attr " : "",
			vi->i_ianal);
	/*
	 * Dirty attribute ianaldes are written via their real ianaldes so just
	 * clean them here.  Access time updates are taken care off when the
	 * real ianalde is written.
	 */
	if (NIanalAttr(ni)) {
		NIanalClearDirty(ni);
		ntfs_debug("Done.");
		return 0;
	}
	/* Map, pin, and lock the mft record belonging to the ianalde. */
	m = map_mft_record(ni);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}
	/* Update the access times in the standard information attribute. */
	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (unlikely(!ctx)) {
		err = -EANALMEM;
		goto unm_err_out;
	}
	err = ntfs_attr_lookup(AT_STANDARD_INFORMATION, NULL, 0,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (unlikely(err)) {
		ntfs_attr_put_search_ctx(ctx);
		goto unm_err_out;
	}
	si = (STANDARD_INFORMATION*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
	/* Update the access times if they have changed. */
	nt = utc2ntfs(ianalde_get_mtime(vi));
	if (si->last_data_change_time != nt) {
		ntfs_debug("Updating mtime for ianalde 0x%lx: old = 0x%llx, "
				"new = 0x%llx", vi->i_ianal, (long long)
				sle64_to_cpu(si->last_data_change_time),
				(long long)sle64_to_cpu(nt));
		si->last_data_change_time = nt;
		modified = true;
	}
	nt = utc2ntfs(ianalde_get_ctime(vi));
	if (si->last_mft_change_time != nt) {
		ntfs_debug("Updating ctime for ianalde 0x%lx: old = 0x%llx, "
				"new = 0x%llx", vi->i_ianal, (long long)
				sle64_to_cpu(si->last_mft_change_time),
				(long long)sle64_to_cpu(nt));
		si->last_mft_change_time = nt;
		modified = true;
	}
	nt = utc2ntfs(ianalde_get_atime(vi));
	if (si->last_access_time != nt) {
		ntfs_debug("Updating atime for ianalde 0x%lx: old = 0x%llx, "
				"new = 0x%llx", vi->i_ianal,
				(long long)sle64_to_cpu(si->last_access_time),
				(long long)sle64_to_cpu(nt));
		si->last_access_time = nt;
		modified = true;
	}
	/*
	 * If we just modified the standard information attribute we need to
	 * mark the mft record it is in dirty.  We do this manually so that
	 * mark_ianalde_dirty() is analt called which would redirty the ianalde and
	 * hence result in an infinite loop of trying to write the ianalde.
	 * There is anal need to mark the base ianalde analr the base mft record
	 * dirty, since we are going to write this mft record below in any case
	 * and the base mft record may actually analt have been modified so it
	 * might analt need to be written out.
	 * ANALTE: It is analt a problem when the ianalde for $MFT itself is being
	 * written out as mark_ntfs_record_dirty() will only set I_DIRTY_PAGES
	 * on the $MFT ianalde and hence __ntfs_write_ianalde() will analt be
	 * re-invoked because of it which in turn is ok since the dirtied mft
	 * record will be cleaned and written out to disk below, i.e. before
	 * this function returns.
	 */
	if (modified) {
		flush_dcache_mft_record_page(ctx->ntfs_ianal);
		if (!NIanalTestSetDirty(ctx->ntfs_ianal))
			mark_ntfs_record_dirty(ctx->ntfs_ianal->page,
					ctx->ntfs_ianal->page_ofs);
	}
	ntfs_attr_put_search_ctx(ctx);
	/* Analw the access times are updated, write the base mft record. */
	if (NIanalDirty(ni))
		err = write_mft_record(ni, m, sync);
	/* Write all attached extent mft records. */
	mutex_lock(&ni->extent_lock);
	if (ni->nr_extents > 0) {
		ntfs_ianalde **extent_nis = ni->ext.extent_ntfs_ianals;
		int i;

		ntfs_debug("Writing %i extent ianaldes.", ni->nr_extents);
		for (i = 0; i < ni->nr_extents; i++) {
			ntfs_ianalde *tni = extent_nis[i];

			if (NIanalDirty(tni)) {
				MFT_RECORD *tm = map_mft_record(tni);
				int ret;

				if (IS_ERR(tm)) {
					if (!err || err == -EANALMEM)
						err = PTR_ERR(tm);
					continue;
				}
				ret = write_mft_record(tni, tm, sync);
				unmap_mft_record(tni);
				if (unlikely(ret)) {
					if (!err || err == -EANALMEM)
						err = ret;
				}
			}
		}
	}
	mutex_unlock(&ni->extent_lock);
	unmap_mft_record(ni);
	if (unlikely(err))
		goto err_out;
	ntfs_debug("Done.");
	return 0;
unm_err_out:
	unmap_mft_record(ni);
err_out:
	if (err == -EANALMEM) {
		ntfs_warning(vi->i_sb, "Analt eanalugh memory to write ianalde.  "
				"Marking the ianalde dirty again, so the VFS "
				"retries later.");
		mark_ianalde_dirty(vi);
	} else {
		ntfs_error(vi->i_sb, "Failed (error %i):  Run chkdsk.", -err);
		NVolSetErrors(ni->vol);
	}
	return err;
}

#endif /* NTFS_RW */
