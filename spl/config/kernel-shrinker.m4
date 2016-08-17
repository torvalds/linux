AC_DEFUN([SPL_AC_SHRINKER_CALLBACK],[
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	dnl #
	dnl # 2.6.23 to 2.6.34 API change
	dnl # ->shrink(int nr_to_scan, gfp_t gfp_mask)
	dnl #
	AC_MSG_CHECKING([whether old 2-argument shrinker exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>

		int shrinker_cb(int nr_to_scan, gfp_t gfp_mask);
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_OLD_SHRINKER_CALLBACK, 1,
			[old shrinker callback wants 2 args])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # 2.6.35 - 2.6.39 API change
		dnl # ->shrink(struct shrinker *,
		dnl #          int nr_to_scan, gfp_t gfp_mask)
		dnl #
		AC_MSG_CHECKING([whether old 3-argument shrinker exists])
		SPL_LINUX_TRY_COMPILE([
			#include <linux/mm.h>

			int shrinker_cb(struct shrinker *, int nr_to_scan,
					gfp_t gfp_mask);
		],[
			struct shrinker cache_shrinker = {
				.shrink = shrinker_cb,
				.seeks = DEFAULT_SEEKS,
			};
			register_shrinker(&cache_shrinker);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_SHRINKER_CALLBACK, 1,
				[old shrinker callback wants 3 args])
		],[
			AC_MSG_RESULT(no)
			dnl #
			dnl # 3.0 - 3.11 API change
			dnl # ->shrink(struct shrinker *,
			dnl #          struct shrink_control *sc)
			dnl #
			AC_MSG_CHECKING(
				[whether new 2-argument shrinker exists])
			SPL_LINUX_TRY_COMPILE([
				#include <linux/mm.h>

				int shrinker_cb(struct shrinker *,
						struct shrink_control *sc);
			],[
				struct shrinker cache_shrinker = {
					.shrink = shrinker_cb,
					.seeks = DEFAULT_SEEKS,
				};
				register_shrinker(&cache_shrinker);
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_2ARGS_NEW_SHRINKER_CALLBACK, 1,
					[new shrinker callback wants 2 args])
			],[
				AC_MSG_RESULT(no)
				dnl #
				dnl # 3.12 API change,
				dnl # ->shrink() is logically split in to
				dnl # ->count_objects() and ->scan_objects()
				dnl #
				AC_MSG_CHECKING(
				    [whether ->count_objects callback exists])
				SPL_LINUX_TRY_COMPILE([
					#include <linux/mm.h>

					unsigned long shrinker_cb(
						struct shrinker *,
						struct shrink_control *sc);
				],[
					struct shrinker cache_shrinker = {
						.count_objects = shrinker_cb,
						.scan_objects = shrinker_cb,
						.seeks = DEFAULT_SEEKS,
					};
					register_shrinker(&cache_shrinker);
				],[
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK,
						1, [->count_objects exists])
				],[
					AC_MSG_ERROR(error)
				])
			])
		])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.39 API change,
dnl # Shrinker adjust to use common shrink_control structure.
dnl #
AC_DEFUN([SPL_AC_SHRINK_CONTROL_STRUCT], [
	AC_MSG_CHECKING([whether struct shrink_control exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		struct shrink_control sc __attribute__ ((unused));

		sc.nr_to_scan = 0;
		sc.gfp_mask = GFP_KERNEL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK_CONTROL_STRUCT, 1,
			[struct shrink_control exists])
	],[
		AC_MSG_RESULT(no)
	])
])
