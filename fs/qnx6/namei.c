// SPDX-License-Identifier: GPL-2.0
/*
 * QNX6 file system, Linux implementation.
 *
 * Version : 1.0.0
 *
 * History :
 *
 * 01-02-2012 by Kai Bankett (chaosman@ontika.net) : first release.
 * 16-02-2012 pagemap extension by Al Viro
 *
 */

#include "qnx6.h"

struct dentry *qnx6_lookup(struct iyesde *dir, struct dentry *dentry,
				unsigned int flags)
{
	unsigned iyes;
	struct page *page;
	struct iyesde *foundiyesde = NULL;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;

	if (len > QNX6_LONG_NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	iyes = qnx6_find_entry(len, dir, name, &page);
	if (iyes) {
		foundiyesde = qnx6_iget(dir->i_sb, iyes);
		qnx6_put_page(page);
		if (IS_ERR(foundiyesde))
			pr_debug("lookup->iget ->  error %ld\n",
				 PTR_ERR(foundiyesde));
	} else {
		pr_debug("%s(): yest found %s\n", __func__, name);
	}
	return d_splice_alias(foundiyesde, dentry);
}
