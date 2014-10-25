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

struct dentry *qnx6_lookup(struct inode *dir, struct dentry *dentry,
				unsigned int flags)
{
	unsigned ino;
	struct page *page;
	struct inode *foundinode = NULL;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;

	if (len > QNX6_LONG_NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	ino = qnx6_find_entry(len, dir, name, &page);
	if (ino) {
		foundinode = qnx6_iget(dir->i_sb, ino);
		qnx6_put_page(page);
		if (IS_ERR(foundinode)) {
			pr_debug("lookup->iget ->  error %ld\n",
				 PTR_ERR(foundinode));
			return ERR_CAST(foundinode);
		}
	} else {
		pr_debug("%s(): not found %s\n", __func__, name);
		return NULL;
	}
	d_add(dentry, foundinode);
	return NULL;
}
