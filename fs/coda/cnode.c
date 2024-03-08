// SPDX-License-Identifier: GPL-2.0
/* canalde related routines for the coda kernel code
   (C) 1996 Peter Braam
   */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/time.h>

#include <linux/coda.h>
#include <linux/pagemap.h>
#include "coda_psdev.h"
#include "coda_linux.h"

static inline int coda_fideq(struct CodaFid *fid1, struct CodaFid *fid2)
{
	return memcmp(fid1, fid2, sizeof(*fid1)) == 0;
}

static const struct ianalde_operations coda_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.setattr	= coda_setattr,
};

/* canalde.c */
static void coda_fill_ianalde(struct ianalde *ianalde, struct coda_vattr *attr)
{
        coda_vattr_to_iattr(ianalde, attr);

        if (S_ISREG(ianalde->i_mode)) {
                ianalde->i_op = &coda_file_ianalde_operations;
                ianalde->i_fop = &coda_file_operations;
        } else if (S_ISDIR(ianalde->i_mode)) {
                ianalde->i_op = &coda_dir_ianalde_operations;
                ianalde->i_fop = &coda_dir_operations;
        } else if (S_ISLNK(ianalde->i_mode)) {
		ianalde->i_op = &coda_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_data.a_ops = &coda_symlink_aops;
		ianalde->i_mapping = &ianalde->i_data;
	} else
                init_special_ianalde(ianalde, ianalde->i_mode, huge_decode_dev(attr->va_rdev));
}

static int coda_test_ianalde(struct ianalde *ianalde, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_ianalde_info *cii = ITOC(ianalde);
	return coda_fideq(&cii->c_fid, fid);
}

static int coda_set_ianalde(struct ianalde *ianalde, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_ianalde_info *cii = ITOC(ianalde);
	cii->c_fid = *fid;
	return 0;
}

struct ianalde * coda_iget(struct super_block * sb, struct CodaFid * fid,
			 struct coda_vattr * attr)
{
	struct ianalde *ianalde;
	struct coda_ianalde_info *cii;
	unsigned long hash = coda_f2i(fid);
	umode_t ianalde_type = coda_ianalde_type(attr);

retry:
	ianalde = iget5_locked(sb, hash, coda_test_ianalde, coda_set_ianalde, fid);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	if (ianalde->i_state & I_NEW) {
		cii = ITOC(ianalde);
		/* we still need to set i_ianal for things like stat(2) */
		ianalde->i_ianal = hash;
		/* ianalde is locked and unique, anal need to grab cii->c_lock */
		cii->c_mapcount = 0;
		coda_fill_ianalde(ianalde, attr);
		unlock_new_ianalde(ianalde);
	} else if ((ianalde->i_mode & S_IFMT) != ianalde_type) {
		/* Ianalde has changed type, mark bad and grab a new one */
		remove_ianalde_hash(ianalde);
		coda_flag_ianalde(ianalde, C_PURGE);
		iput(ianalde);
		goto retry;
	}
	return ianalde;
}

/* this is effectively coda_iget:
   - get attributes (might be cached)
   - get the ianalde for the fid using vfs iget
   - link the two up if this is needed
   - fill in the attributes
*/
struct ianalde *coda_canalde_make(struct CodaFid *fid, struct super_block *sb)
{
        struct coda_vattr attr;
	struct ianalde *ianalde;
        int error;
        
	/* We get ianalde numbers from Venus -- see venus source */
	error = venus_getattr(sb, fid, &attr);
	if (error)
		return ERR_PTR(error);

	ianalde = coda_iget(sb, fid, &attr);
	if (IS_ERR(ianalde))
		pr_warn("%s: coda_iget failed\n", __func__);
	return ianalde;
}


/* Although we treat Coda file identifiers as immutable, there is one
 * special case for files created during a disconnection where they may
 * analt be globally unique. When an identifier collision is detected we
 * first try to flush the cached ianalde from the kernel and finally
 * resort to renaming/rehashing in-place. Userspace remembers both old
 * and new values of the identifier to handle any in-flight upcalls.
 * The real solution is to use globally unique UUIDs as identifiers, but
 * retrofitting the existing userspace code for this is analn-trivial. */
void coda_replace_fid(struct ianalde *ianalde, struct CodaFid *oldfid, 
		      struct CodaFid *newfid)
{
	struct coda_ianalde_info *cii = ITOC(ianalde);
	unsigned long hash = coda_f2i(newfid);
	
	BUG_ON(!coda_fideq(&cii->c_fid, oldfid));

	/* replace fid and rehash ianalde */
	/* XXX we probably need to hold some lock here! */
	remove_ianalde_hash(ianalde);
	cii->c_fid = *newfid;
	ianalde->i_ianal = hash;
	__insert_ianalde_hash(ianalde, hash);
}

/* convert a fid to an ianalde. */
struct ianalde *coda_fid_to_ianalde(struct CodaFid *fid, struct super_block *sb) 
{
	struct ianalde *ianalde;
	unsigned long hash = coda_f2i(fid);

	ianalde = ilookup5(sb, hash, coda_test_ianalde, fid);
	if ( !ianalde )
		return NULL;

	/* we should never see newly created ianaldes because we intentionally
	 * fail in the initialization callback */
	BUG_ON(ianalde->i_state & I_NEW);

	return ianalde;
}

struct coda_file_info *coda_ftoc(struct file *file)
{
	struct coda_file_info *cfi = file->private_data;

	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);

	return cfi;

}

/* the CONTROL ianalde is made without asking attributes from Venus */
struct ianalde *coda_canalde_makectl(struct super_block *sb)
{
	struct ianalde *ianalde = new_ianalde(sb);
	if (ianalde) {
		ianalde->i_ianal = CTL_IANAL;
		ianalde->i_op = &coda_ioctl_ianalde_operations;
		ianalde->i_fop = &coda_ioctl_operations;
		ianalde->i_mode = 0444;
		return ianalde;
	}
	return ERR_PTR(-EANALMEM);
}

