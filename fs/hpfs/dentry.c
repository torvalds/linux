/*
 *  linux/fs/hpfs/dentry.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  dcache operations
 */

#include "hpfs_fn.h"

/*
 * Note: the dentry argument is the parent dentry.
 */

static int hpfs_hash_dentry(const struct dentry *dentry, const struct inode *inode,
		struct qstr *qstr)
{
	unsigned long	 hash;
	int		 i;
	unsigned l = qstr->len;

	if (l == 1) if (qstr->name[0]=='.') goto x;
	if (l == 2) if (qstr->name[0]=='.' || qstr->name[1]=='.') goto x;
	hpfs_adjust_length(qstr->name, &l);
	/*if (hpfs_chk_name(qstr->name,&l))*/
		/*return -ENAMETOOLONG;*/
		/*return -ENOENT;*/
	x:

	hash = init_name_hash();
	for (i = 0; i < l; i++)
		hash = partial_name_hash(hpfs_upcase(hpfs_sb(dentry->d_sb)->sb_cp_table,qstr->name[i]), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int hpfs_compare_dentry(const struct dentry *parent,
		const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	unsigned al = len;
	unsigned bl = name->len;

	hpfs_adjust_length(str, &al);
	/*hpfs_adjust_length(b->name, &bl);*/

	/*
	 * 'str' is the nane of an already existing dentry, so the name
	 * must be valid. 'name' must be validated first.
	 */

	if (hpfs_chk_name(name->name, &bl))
		return 1;
	if (hpfs_compare_names(parent->d_sb, str, al, name->name, bl, 0))
		return 1;
	return 0;
}

static const struct dentry_operations hpfs_dentry_operations = {
	.d_hash		= hpfs_hash_dentry,
	.d_compare	= hpfs_compare_dentry,
};

void hpfs_set_dentry_operations(struct dentry *dentry)
{
	dentry->d_op = &hpfs_dentry_operations;
}
