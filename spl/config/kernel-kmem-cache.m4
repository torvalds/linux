dnl #
dnl # 2.6.35 API change,
dnl # The cachep->gfpflags member was renamed cachep->allocflags.  These are
dnl # private allocation flags which are applied when allocating a new slab
dnl # in kmem_getpages().  Unfortunately there is no public API for setting
dnl # non-default flags.
dnl #
AC_DEFUN([SPL_AC_KMEM_CACHE_ALLOCFLAGS], [
	AC_MSG_CHECKING([whether struct kmem_cache has allocflags])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/slab.h>
	],[
		struct kmem_cache cachep __attribute__ ((unused));
		cachep.allocflags = GFP_KERNEL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMEM_CACHE_ALLOCFLAGS, 1,
			[struct kmem_cache has allocflags])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether struct kmem_cache has gfpflags])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/slab.h>
		],[
			struct kmem_cache cachep __attribute__ ((unused));
			cachep.gfpflags = GFP_KERNEL;
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_KMEM_CACHE_GFPFLAGS, 1,
				[struct kmem_cache has gfpflags])
		],[
			AC_MSG_RESULT(no)
		])
	])
])

dnl #
dnl # grsecurity API change,
dnl # kmem_cache_create() with SLAB_USERCOPY flag replaced by
dnl # kmem_cache_create_usercopy().
dnl #
AC_DEFUN([SPL_AC_KMEM_CACHE_CREATE_USERCOPY], [
	AC_MSG_CHECKING([whether kmem_cache_create_usercopy() exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/slab.h>
		static void ctor(void *foo)
		{
			// fake ctor
		}
	],[
		struct kmem_cache *skc_linux_cache;
		const char *name = "test";
		size_t size = 4096;
		size_t align = 8;
		unsigned long flags = 0;
		size_t useroffset = 0;
		size_t usersize = size - useroffset;

		skc_linux_cache = kmem_cache_create_usercopy(
			name, size, align, flags, useroffset, usersize, ctor);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMEM_CACHE_CREATE_USERCOPY, 1,
				[kmem_cache_create_usercopy() exists])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
