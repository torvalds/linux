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

static int hpfs_hash_dentry(struct dentry *dentry, struct qstr *qstr)
{
	unsigned long	 hash;
	int		 i;
	unsigned l = qstr->len;

	if (l == 1) if (qstr->name[0]=='.') goto x;
	if (l == 2) if (qstr->name[0]=='.' || qstr->name[1]=='.') goto x;
	hpfs_adjust_length((char *)qstr->name, &l);
	/*if (hpfs_chk_name((char *)qstr->name,&l))*/
		/*return -ENAMETOOLONG;*/
		/*return -ENOENT;*/
	x:

	hash = init_name_hash();
	for (i = 0; i < l; i++)
		hash = partial_name_hash(hpfs_upcase(hpfs_sb(dentry->d_sb)->sb_cp_table,qstr->name[i]), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int hpfs_compare_dentry(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	unsigned al=a->len;
	unsigned bl=b->len;
	hpfs_adjust_length((char *)a->name, &al);
	/*hpfs_adjust_length((char *)b->name, &bl);*/
	/* 'a' is the qstr of an already existing dentry, so the name
	 * must be valid. 'b' must be validated first.
	 */

	if (hpfs_chk_name((char *)b->name, &bl)) return 1;
	if (hpfs_compare_names(dentry->d_sb, (char *)a->name, al, (char *)b->name, bl, 0)) return 1;
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
