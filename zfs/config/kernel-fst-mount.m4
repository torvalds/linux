dnl #
dnl # 2.6.38 API change
dnl # The .get_sb callback has been replaced by a .mount callback
dnl # in the file_system_type structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FST_MOUNT], [
        AC_MSG_CHECKING([whether fst->mount() exists])
        ZFS_LINUX_TRY_COMPILE([
                #include <linux/fs.h>

                static struct dentry *
                mount(struct file_system_type *fs_type, int flags,
                    const char *osname, void *data) {
                        struct dentry *d = NULL;
                        return (d);
                }

                static struct file_system_type fst __attribute__ ((unused)) = {
                        .mount = mount,
                };
        ],[
        ],[
                AC_MSG_RESULT(yes)
                AC_DEFINE(HAVE_FST_MOUNT, 1, [fst->mount() exists])
        ],[
                AC_MSG_RESULT(no)
        ])
])
