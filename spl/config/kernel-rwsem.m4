dnl #
dnl # 3.1 API Change
dnl #
dnl # The rw_semaphore.wait_lock member was changed from spinlock_t to
dnl # raw_spinlock_t at commit ddb6c9b58a19edcfac93ac670b066c836ff729f1.
dnl #
AC_DEFUN([SPL_AC_RWSEM_SPINLOCK_IS_RAW], [
	AC_MSG_CHECKING([whether struct rw_semaphore member wait_lock is raw])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		raw_spinlock_t dummy_lock __attribute__ ((unused)) =
		    __RAW_SPIN_LOCK_INITIALIZER(dummy_lock);
		dummy_semaphore.wait_lock = dummy_lock;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(RWSEM_SPINLOCK_IS_RAW, 1,
		[struct rw_semaphore member wait_lock is raw_spinlock_t])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.16 API Change
dnl #
dnl # rwsem-spinlock "->activity" changed to "->count"
dnl #
AC_DEFUN([SPL_AC_RWSEM_ACTIVITY], [
	AC_MSG_CHECKING([whether struct rw_semaphore has member activity])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		dummy_semaphore.activity = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ACTIVITY, 1,
		[struct rw_semaphore has member activity])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 4.8 API Change
dnl #
dnl # rwsem "->count" changed to atomic_long_t type
dnl #
AC_DEFUN([SPL_AC_RWSEM_ATOMIC_LONG_COUNT], [
	AC_MSG_CHECKING(
	[whether struct rw_semaphore has atomic_long_t member count])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/rwsem.h>
	],[
		DECLARE_RWSEM(dummy_semaphore);
		(void) atomic_long_read(&dummy_semaphore.count);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ATOMIC_LONG_COUNT, 1,
		[struct rw_semaphore has atomic_long_t member count])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
