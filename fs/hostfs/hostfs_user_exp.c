#include <linux/module.h>
#include "hostfs.h"

EXPORT_SYMBOL_GPL(stat_file);
EXPORT_SYMBOL_GPL(access_file);
EXPORT_SYMBOL_GPL(open_file);
EXPORT_SYMBOL_GPL(open_dir);
EXPORT_SYMBOL_GPL(seek_dir);
EXPORT_SYMBOL_GPL(read_dir);
EXPORT_SYMBOL_GPL(read_file);
EXPORT_SYMBOL_GPL(write_file);
EXPORT_SYMBOL_GPL(lseek_file);
EXPORT_SYMBOL_GPL(fsync_file);
EXPORT_SYMBOL_GPL(replace_file);
EXPORT_SYMBOL_GPL(close_file);
EXPORT_SYMBOL_GPL(close_dir);
EXPORT_SYMBOL_GPL(file_create);
EXPORT_SYMBOL_GPL(set_attr);
EXPORT_SYMBOL_GPL(make_symlink);
EXPORT_SYMBOL_GPL(unlink_file);
EXPORT_SYMBOL_GPL(do_mkdir);
EXPORT_SYMBOL_GPL(hostfs_do_rmdir);
EXPORT_SYMBOL_GPL(do_mknod);
EXPORT_SYMBOL_GPL(link_file);
EXPORT_SYMBOL_GPL(hostfs_do_readlink);
EXPORT_SYMBOL_GPL(rename_file);
EXPORT_SYMBOL_GPL(rename2_file);
EXPORT_SYMBOL_GPL(do_statfs);
