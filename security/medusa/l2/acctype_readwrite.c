/* this file is not really a part of the model. however, someone may find
 * it useful.
 */

#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/model.h>
#include <linux/init.h>

#include "kobject_process.h"
#include "kobject_file.h"
#include <linux/medusa/l1/file_handlers.h>

/**
 * medusa_read - L1-called code to check VS
 * @file: file to read
 *
 */
medusa_answer_t medusa_read(struct file * file)
{
	struct dentry * dentry;

	dentry = file->f_path.dentry;
	if (!dentry || IS_ERR(dentry))
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security(dentry->d_inode)) &&
			file_kobj_validate_dentry(dentry,NULL) <= 0)
		return MED_OK;
	if (
		!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(dentry->d_inode))) ||
		!VS_INTERSECT(VSR(&task_security(current)),VS(&inode_security(dentry->d_inode)))
	   ) {
		return MED_NO;
	}

	return MED_OK;
}

/**
 * medusa_write - L1-called code to check VS
 * @file: file to write
 *
 */
medusa_answer_t medusa_write(struct file * file)
{
	struct dentry * dentry;

	dentry = file->f_path.dentry;
	if (!dentry || IS_ERR(dentry))
		return MED_OK;
	if (!MED_MAGIC_VALID(&task_security(current)) &&
		process_kobj_validate_task(current) <= 0)
		return MED_OK;

	if (!MED_MAGIC_VALID(&inode_security(dentry->d_inode)) &&
			file_kobj_validate_dentry(dentry,NULL) <= 0)
		return MED_OK;
	if (
		!VS_INTERSECT(VSS(&task_security(current)),VS(&inode_security(dentry->d_inode))) ||
		!VS_INTERSECT(VSW(&task_security(current)),VS(&inode_security(dentry->d_inode)))
	   ) {
		return MED_NO;
	}

	return MED_OK;
}

