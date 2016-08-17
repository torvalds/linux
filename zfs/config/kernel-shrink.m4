dnl #
dnl # 3.1 API change
dnl # The super_block structure now stores a per-filesystem shrinker.
dnl # This interface is preferable because it can be used to specifically
dnl # target only the zfs filesystem for pruning.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SHRINK], [
	AC_MSG_CHECKING([whether super_block has s_shrink])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int shrink(struct shrinker *s, struct shrink_control *sc)
		    { return 0; }

		static const struct super_block
		    sb __attribute__ ((unused)) = {
			.s_shrink.shrink = shrink,
			.s_shrink.seeks = DEFAULT_SEEKS,
			.s_shrink.batch = 0,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK, 1, [struct super_block has s_shrink])

	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.3 API change
dnl # The super_block structure was changed to use an hlist_node instead
dnl # of a list_head for the .s_instance linkage.
dnl #
dnl # This was done in part to resolve a race in the iterate_supers_type()
dnl # function which was introduced in Linux 3.0 kernel.  The iterator
dnl # was supposed to provide a safe way to call an arbitrary function on
dnl # all super blocks of a specific type.  Unfortunately, because a
dnl # list_head was used it was possible for iterate_supers_type() to
dnl # get stuck spinning a super block which was just deactivated.
dnl #
dnl # This can occur because when the list head is removed from the
dnl # fs_supers list it is reinitialized to point to itself.  If the
dnl # iterate_supers_type() function happened to be processing the
dnl # removed list_head it will get stuck spinning on that list_head.
dnl #
dnl # To resolve the issue for existing 3.0 - 3.2 kernels we detect when
dnl # a list_head is used.  Then to prevent the spinning from occurring
dnl # the .next pointer is set to the fs_supers list_head which ensures
dnl # the iterate_supers_type() function will always terminate.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_S_INSTANCES_LIST_HEAD], [
	AC_MSG_CHECKING([whether super_block has s_instances list_head])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct super_block sb __attribute__ ((unused));

		INIT_LIST_HEAD(&sb.s_instances);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_S_INSTANCES_LIST_HEAD, 1,
		    [struct super_block has s_instances list_head])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int nr_cached_objects(struct super_block *sb) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.nr_cached_objects = nr_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NR_CACHED_OBJECTS, 1,
			[sops->nr_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FREE_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->free_cached_objects() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		void free_cached_objects(struct super_block *sb, int x)
		    { return; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.free_cached_objects = free_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FREE_CACHED_OBJECTS, 1,
			[sops->free_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.12 API change
dnl # The nid member was added to struct shrink_control to support
dnl # NUMA-aware shrinkers.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID], [
	AC_MSG_CHECKING([whether shrink_control has nid])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct shrink_control sc __attribute__ ((unused));
		unsigned long scnidsize __attribute__ ((unused)) =
		    sizeof(sc.nid);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(SHRINK_CONTROL_HAS_NID, 1,
		    [struct shrink_control has nid])
	],[
		AC_MSG_RESULT(no)
	])
])
