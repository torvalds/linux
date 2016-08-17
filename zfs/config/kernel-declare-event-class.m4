dnl #
dnl # Ensure the DECLARE_EVENT_CLASS macro is available to non-GPL modules.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_DECLARE_EVENT_CLASS], [
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-I\$(src)"

	AC_MSG_CHECKING([whether DECLARE_EVENT_CLASS() is available])
	ZFS_LINUX_TRY_COMPILE_HEADER([
		#include <linux/module.h>
		MODULE_LICENSE(ZFS_META_LICENSE);

		#define CREATE_TRACE_POINTS
		#include "conftest.h"
	],[
		trace_zfs_autoconf_event_one(1UL);
		trace_zfs_autoconf_event_two(2UL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DECLARE_EVENT_CLASS, 1,
		          [DECLARE_EVENT_CLASS() is available])
	],[
		AC_MSG_RESULT(no)
	],[
		#if !defined(_CONFTEST_H) || defined(TRACE_HEADER_MULTI_READ)
		#define _CONFTEST_H

		#undef  TRACE_SYSTEM
		#define TRACE_SYSTEM zfs
		#include <linux/tracepoint.h>

		DECLARE_EVENT_CLASS(zfs_autoconf_event_class,
			TP_PROTO(unsigned long i),
			TP_ARGS(i),
			TP_STRUCT__entry(
				__field(unsigned long, i)
			),
			TP_fast_assign(
				__entry->i = i;
			),
			TP_printk("i = %lu", __entry->i)
		);

		#define DEFINE_AUTOCONF_EVENT(name) \
		DEFINE_EVENT(zfs_autoconf_event_class, name, \
			TP_PROTO(unsigned long i), \
			TP_ARGS(i))
		DEFINE_AUTOCONF_EVENT(zfs_autoconf_event_one);
		DEFINE_AUTOCONF_EVENT(zfs_autoconf_event_two);

		#endif /* _CONFTEST_H */

		#undef  TRACE_INCLUDE_PATH
		#define TRACE_INCLUDE_PATH .
		#define TRACE_INCLUDE_FILE conftest
		#include <trace/define_trace.h>
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
