/* SPDX-License-Identifier: GPL-2.0 */

int __init init_mount(const char *dev_name, const char *dir_name,
		const char *type_page, unsigned long flags, void *data_page);
int __init init_umount(const char *name, int flags);
int __init init_chdir(const char *filename);
int __init init_chroot(const char *filename);
int __init init_chown(const char *filename, uid_t user, gid_t group, int flags);
int __init init_chmod(const char *filename, umode_t mode);
int __init init_eaccess(const char *filename);
int __init init_stat(const char *filename, struct kstat *stat, int flags);
int __init init_mknod(const char *filename, umode_t mode, unsigned int dev);
int __init init_link(const char *oldname, const char *newname);
int __init init_symlink(const char *oldname, const char *newname);
int __init init_unlink(const char *pathname);
int __init init_mkdir(const char *pathname, umode_t mode);
int __init init_rmdir(const char *pathname);
int __init init_utimes(char *filename, struct timespec64 *ts);
int __init init_dup(struct file *file);
