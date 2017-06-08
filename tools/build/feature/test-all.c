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

#define main main_test_dwarf_getlocations
# include "test-dwarf_getlocations.c"
#undef main

#define main main_test_libelf_getphdrnum
# include "test-libelf-getphdrnum.c"
#undef main

#define main main_test_libelf_gelf_getnote
# include "test-libelf-gelf_getnote.c"
#undef main

#define main main_test_libelf_getshdrstrndx
# include "test-libelf-getshdrstrndx.c"
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

#define main main_test_backtrace
# include "test-backtrace.c"
#undef main

#define main main_test_libnuma
# include "test-libnuma.c"
#undef main

#define main main_test_numa_num_possible_cpus
# include "test-numa_num_possible_cpus.c"
#undef main

#define main main_test_timerfd
# include "test-timerfd.c"
#undef main

#define main main_test_stackprotector_all
# include "test-stackprotector-all.c"
#undef main

#define main main_test_libdw_dwarf_unwind
# include "test-libdw-dwarf-unwind.c"
#undef main

#define main main_test_sync_compare_and_swap
# include "test-sync-compare-and-swap.c"
#undef main

#define main main_test_zlib
# include "test-zlib.c"
#undef main

#define main main_test_pthread_attr_setaffinity_np
# include "test-pthread-attr-setaffinity-np.c"
#undef main

#define main main_test_sched_getcpu
# include "test-sched_getcpu.c"
#undef main

# if 0
/*
 * Disable libbabeltrace check for test-all, because the requested
 * library version is not released yet in most distributions. Will
 * reenable later.
 */

#define main main_test_libbabeltrace
# include "test-libbabeltrace.c"
#undef main
#endif

#define main main_test_lzma
# include "test-lzma.c"
#undef main

#define main main_test_get_cpuid
# include "test-get_cpuid.c"
#undef main

#define main main_test_bpf
# include "test-bpf.c"
#undef main

#define main main_test_libcrypto
# include "test-libcrypto.c"
#undef main

#define main main_test_sdt
# include "test-sdt.c"
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
	main_test_dwarf_getlocations();
	main_test_libelf_getphdrnum();
	main_test_libelf_gelf_getnote();
	main_test_libelf_getshdrstrndx();
	main_test_libunwind();
	main_test_libaudit();
	main_test_libslang();
	main_test_gtk2(argc, argv);
	main_test_gtk2_infobar(argc, argv);
	main_test_libbfd();
	main_test_backtrace();
	main_test_libnuma();
	main_test_numa_num_possible_cpus();
	main_test_timerfd();
	main_test_stackprotector_all();
	main_test_libdw_dwarf_unwind();
	main_test_sync_compare_and_swap(argc, argv);
	main_test_zlib();
	main_test_pthread_attr_setaffinity_np();
	main_test_lzma();
	main_test_get_cpuid();
	main_test_bpf();
	main_test_libcrypto();
	main_test_sched_getcpu();
	main_test_sdt();

	return 0;
}
