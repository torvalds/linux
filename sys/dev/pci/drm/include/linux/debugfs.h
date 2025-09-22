/* Public domain. */

#ifndef _LINUX_DEBUGFS_H
#define _LINUX_DEBUGFS_H

struct debugfs_regset32 {
};

#define debugfs_create_atomic_t(a, b, c, d)
#define debugfs_remove(a)
#define debugfs_create_dir(a, b)		ERR_PTR(-ENOSYS)
#define debugfs_create_file(a, b, c, d, e)	ERR_PTR(-ENOSYS)
#define debugfs_create_file_unsafe(a, b, c, d, e)	ERR_PTR(-ENOSYS)
#define debugfs_create_bool(a, b, c, d)

#define DEFINE_DEBUGFS_ATTRIBUTE(a, b, c, d)

#endif
