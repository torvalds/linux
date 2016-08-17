dnl #
dnl # 4.8 API change
dnl #
dnl # 75ef71840539 mm, vmstat: add infrastructure for per-node vmstats
dnl # 599d0c954f91 mm, vmscan: move LRU lists to node
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_NODE_PAGE_STATE], [
	AC_MSG_CHECKING([whether global_node_page_state() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
		#include <linux/vmstat.h>
	],[
		(void) global_node_page_state(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_GLOBAL_NODE_PAGE_STATE, 1, [global_node_page_state() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.14 API change
dnl #
dnl # c41f012ade0b mm: rename global_page_state to global_zone_page_state
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_ZONE_PAGE_STATE], [
	AC_MSG_CHECKING([whether global_zone_page_state() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
		#include <linux/vmstat.h>
	],[
		(void) global_zone_page_state(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_GLOBAL_ZONE_PAGE_STATE, 1, [global_zone_page_state() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Create a define and autoconf variable for an enum member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_ENUM_MEMBER], [
	AC_MSG_CHECKING([whether enum $2 contains $1])
	AS_IF([AC_TRY_COMMAND("${srcdir}/scripts/enum-extract.pl" "$2" "$3" | egrep -qx $1)],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(m4_join([_], [ZFS_ENUM], m4_toupper($2), $1), 1, [enum $2 contains $1])
		m4_join([_], [ZFS_ENUM], m4_toupper($2), $1)=1
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Sanity check helpers
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_ERROR],[
	AC_MSG_RESULT(no)
	AC_MSG_RESULT([$1 in either node_stat_item or zone_stat_item: $2])
	AC_MSG_RESULT([configure needs updating, see: config/kernel-global_page_state.m4])
	AC_MSG_FAILURE([SHUT 'ER DOWN CLANCY, SHE'S PUMPIN' MUD!])
])

AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_CHECK], [
	enum_check_a="m4_join([_], [$ZFS_ENUM_NODE_STAT_ITEM], $1)"
	enum_check_b="m4_join([_], [$ZFS_ENUM_ZONE_STAT_ITEM], $1)"
	AS_IF([test -n "$enum_check_a" -a -n "$enum_check_b"],[
		ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_ERROR([$1], [DUPLICATE])
	])
	AS_IF([test -z "$enum_check_a" -a -z "$enum_check_b"],[
		ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_ERROR([$1], [NOT FOUND])
	])
])

dnl #
dnl # Ensure the config tests are finding one and only one of each enum of interest
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_ZONE_PAGE_STATE_SANITY], [
	AC_MSG_CHECKING([global_page_state enums are sane])

	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_CHECK([NR_FILE_PAGES])
	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_CHECK([NR_INACTIVE_ANON])
	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_CHECK([NR_INACTIVE_FILE])
	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE_ENUM_CHECK([NR_SLAB_RECLAIMABLE])

	AC_MSG_RESULT(yes)
])

dnl #
dnl # enum members in which we're interested
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GLOBAL_PAGE_STATE], [
	ZFS_AC_KERNEL_GLOBAL_NODE_PAGE_STATE
	ZFS_AC_KERNEL_GLOBAL_ZONE_PAGE_STATE

	ZFS_AC_KERNEL_ENUM_MEMBER([NR_FILE_PAGES],		[node_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_INACTIVE_ANON],		[node_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_INACTIVE_FILE],		[node_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_SLAB_RECLAIMABLE],	[node_stat_item], [$LINUX/include/linux/mmzone.h])

	ZFS_AC_KERNEL_ENUM_MEMBER([NR_FILE_PAGES],		[zone_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_INACTIVE_ANON],		[zone_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_INACTIVE_FILE],		[zone_stat_item], [$LINUX/include/linux/mmzone.h])
	ZFS_AC_KERNEL_ENUM_MEMBER([NR_SLAB_RECLAIMABLE],	[zone_stat_item], [$LINUX/include/linux/mmzone.h])

	ZFS_AC_KERNEL_GLOBAL_ZONE_PAGE_STATE_SANITY
])
