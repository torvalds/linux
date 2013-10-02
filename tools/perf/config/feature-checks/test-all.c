/*
 * test-all.c: Try to build all the main testcases at once.
 *
 * A well-configured system will have all the prereqs installed, so we can speed
 * up auto-detection on such systems.
 */

/*
 * Quirk: Python and Perl headers cannot be in arbitrary places, so keep
 * these 3 testcases at the top:
 */
#define main main_test_libpython
# include "test-libpython.c"
#undef main

#define main main_test_libpython_version
# include "test-libpython-version.c"
#undef main

#define main main_test_libperl
# include "test-libperl.c"
#undef main

#define main main_test_hello
# include "test-hello.c"
#undef main

#define main main_test_libelf
# include "test-libelf.c"
#undef main

#define main main_test_libelf_mmap
# include "test-libelf-mmap.c"
#undef main

#define main main_test_glibc
# include "test-glibc.c"
#undef main

#define main main_test_dwarf
# include "test-dwarf.c"
#undef main

#define main main_test_libelf_getphdrnum
# include "test-libelf-getphdrnum.c"
#undef main

#define main main_test_libunwind
# include "test-libunwind.c"
#undef main

#define main main_test_libaudit
# include "test-libaudit.c"
#undef main

#define main main_test_libslang
# include "test-libslang.c"
#undef main

#define main main_test_gtk2
# include "test-gtk2.c"
#undef main

#define main main_test_gtk2_infobar
# include "test-gtk2-infobar.c"
#undef main

#define main main_test_libbfd
# include "test-libbfd.c"
#undef main

#define main main_test_on_exit
# include "test-on-exit.c"
#undef main

#define main main_test_backtrace
# include "test-backtrace.c"
#undef main

#define main main_test_libnuma
# include "test-libnuma.c"
#undef main

int main(int argc, char *argv[])
{
	main_test_libpython();
	main_test_libpython_version();
	main_test_libperl();
	main_test_hello();
	main_test_libelf();
	main_test_libelf_mmap();
	main_test_glibc();
	main_test_dwarf();
	main_test_libelf_getphdrnum();
	main_test_libunwind();
	main_test_libaudit();
	main_test_libslang();
	main_test_gtk2(argc, argv);
	main_test_gtk2_infobar(argc, argv);
	main_test_libbfd();
	main_test_on_exit();
	main_test_backtrace();
	main_test_libnuma();

	return 0;
}
