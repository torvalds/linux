dnl #
dnl # 4.8 API change
dnl # kernel vm counters change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VM_NODE_STAT], [
	AC_MSG_CHECKING([whether to use vm_node_stat based fn's])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
		#include <linux/vmstat.h>
        ],[
			int a __attribute__ ((unused)) = NR_VM_NODE_STAT_ITEMS;
			long x __attribute__ ((unused)) =
				atomic_long_read(&vm_node_stat[0]);
			(void) global_node_page_state(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_GLOBAL_NODE_PAGE_STATE, 1,
			[using global_node_page_state()])
	],[
		AC_MSG_RESULT(no)
	])
])
