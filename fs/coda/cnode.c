// SPDX-License-Identifier: GPL-2.0
/* cyesde related routines for the coda kernel code
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

static const struct iyesde_operations coda_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.setattr	= coda_setattr,
};

/* cyesde.c */
static void coda_fill_iyesde(struct iyesde *iyesde, struct coda_vattr *attr)
{
        coda_vattr_to_iattr(iyesde, attr);

        if (S_ISREG(iyesde->i_mode)) {
                iyesde->i_op = &coda_file_iyesde_operations;
                iyesde->i_fop = &coda_file_operations;
        } else if (S_ISDIR(iyesde->i_mode)) {
                iyesde->i_op = &coda_dir_iyesde_operations;
                iyesde->i_fop = &coda_dir_operations;
        } else if (S_ISLNK(iyesde->i_mode)) {
		iyesde->i_op = &coda_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_data.a_ops = &coda_symlink_aops;
		iyesde->i_mapping = &iyesde->i_data;
	} else
                init_special_iyesde(iyesde, iyesde->i_mode, huge_decode_dev(attr->va_rdev));
}

static int coda_test_iyesde(struct iyesde *iyesde, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_iyesde_info *cii = ITOC(iyesde);
	return coda_fideq(&cii->c_fid, fid);
}

static int coda_set_iyesde(struct iyesde *iyesde, void *data)
{
	struct CodaFid *fid = (struct CodaFid *)data;
	struct coda_iyesde_info *cii = ITOC(iyesde);
	cii->c_fid = *fid;
	return 0;
}

struct iyesde * coda_iget(struct super_block * sb, struct CodaFid * fid,
			 struct coda_vattr * attr)
{
	struct iyesde *iyesde;
	struct coda_iyesde_info *cii;
	unsigned long hash = coda_f2i(fid);

	iyesde = iget5_locked(sb, hash, coda_test_iyesde, coda_set_iyesde, fid);

	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	if (iyesde->i_state & I_NEW) {
		cii = ITOC(iyesde);
		/* we still need to set i_iyes for things like stat(2) */
		iyesde->i_iyes = hash;
		/* iyesde is locked and unique, yes need to grab cii->c_lock */
		cii->c_mapcount = 0;
		unlock_new_iyesde(iyesde);
	}

	/* always replace the attributes, type might have changed */
	coda_fill_iyesde(iyesde, attr);
	return iyesde;
}

/* this is effectively coda_iget:
   - get attributes (might be cached)
   - get the iyesde for the fid using vfs iget
   - link the two up if this is needed
   - fill in the attributes
*/
struct iyesde *coda_cyesde_make(struct CodaFid *fid, struct super_block *sb)
{
        struct coda_vattr attr;
	struct iyesde *iyesde;
        int error;
        
	/* We get iyesde numbers from Venus -- see venus source */
	error = venus_getattr(sb, fid, &attr);
	if (error)
		return ERR_PTR(error);

	iyesde = coda_iget(sb, fid, &attr);
	if (IS_ERR(iyesde))
		pr_warn("%s: coda_iget failed\n", __func__);
	return iyesde;
}


/* Although we treat Coda file identifiers as immutable, there is one
 * special case for files created during a disconnection where they may
 * yest be globally unique. When an identifier collision is detected we
 * first try to flush the cached iyesde from the kernel and finally
 * resort to renaming/rehashing in-place. Userspace remembers both old
 * and new values of the identifier to handle any in-flight upcalls.
 * The real solution is to use globally unique UUIDs as identifiers, but
 * retrofitting the existing userspace code for this is yesn-trivial. */
void coda_replace_fid(struct iyesde *iyesde, struct CodaFid *oldfid, 
		      struct CodaFid *newfid)
{
	struct coda_iyesde_info *cii = ITOC(iyesde);
	unsigned long hash = coda_f2i(newfid);
	
	BUG_ON(!coda_fideq(&cii->c_fid, oldfid));

	/* replace fid and rehash iyesde */
	/* XXX we probably need to hold some lock here! */
	remove_iyesde_hash(iyesde);
	cii->c_fid = *newfid;
	iyesde->i_iyes = hash;
	__insert_iyesde_hash(iyesde, hash);
}

/* convert a fid to an iyesde. */
struct iyesde *coda_fid_to_iyesde(struct CodaFid *fid, struct super_block *sb) 
{
	struct iyesde *iyesde;
	unsigned long hash = coda_f2i(fid);

	iyesde = ilookup5(sb, hash, coda_test_iyesde, fid);
	if ( !iyesde )
		return NULL;

	/* we should never see newly created iyesdes because we intentionally
	 * fail in the initialization callback */
	BUG_ON(iyesde->i_state & I_NEW);

	return iyesde;
}

struct coda_file_info *coda_ftoc(struct file *file)
{
	struct coda_file_info *cfi = file->private_data;

	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);

	return cfi;

}

/* the CONTROL iyesde is made without asking attributes from Venus */
struct iyesde *coda_cyesde_makectl(struct super_block *sb)
{
	struct iyesde *iyesde = new_iyesde(sb);
	if (iyesde) {
		iyesde->i_iyes = CTL_INO;
		iyesde->i_op = &coda_ioctl_iyesde_operations;
		iyesde->i_fop = &coda_ioctl_operations;
		iyesde->i_mode = 0444;
		return iyesde;
	}
	return ERR_PTR(-ENOMEM);
}

