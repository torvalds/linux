
#pragma GCC diagnostic ignored "-Wstrict-prototypes"

#include <Python.h>

#include <EXTERN.h>
#include <perl.h>

#include <stdio.h>
#include <libelf.h>
#include <gnu/libc-version.h>
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/version.h>
#include <libelf.h>
#include <libunwind.h>
#include <stdlib.h>
#include <libaudit.h>
#include <slang.h>
#include <gtk/gtk.h>
#include <bfd.h>
#include <stdio.h>
#include <execinfo.h>
#include <stdio.h>
#include <numa.h>
#include <numaif.h>

#pragma GCC diagnostic error "-Wstrict-prototypes"

int main1(void)
{
	return puts("hi");
}

int main2(void)
{
	return puts("hi");
}

int main3(void)
{
	return puts("hi");
}

int main4(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ, 0);
	return (long)elf;
}
#
int main5(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ_MMAP, 0);
	return (long)elf;
}

int main6(void)
{
	const char *version = gnu_get_libc_version();
	return (long)version;
}

int main7(void)
{
	Dwarf *dbg = dwarf_begin(0, DWARF_C_READ);
	return (long)dbg;
}

int main8(void)
{
	size_t dst;
	return elf_getphdrnum(0, &dst);
}

extern int UNW_OBJ(dwarf_search_unwind_table) (unw_addr_space_t as,
                                      unw_word_t ip,
                                      unw_dyn_info_t *di,
                                      unw_proc_info_t *pi,
                                      int need_unwind_info, void *arg);


#define dwarf_search_unwind_table UNW_OBJ(dwarf_search_unwind_table)

int main9(void)
{
	unw_addr_space_t addr_space;
	addr_space = unw_create_addr_space(NULL, 0);
	unw_init_remote(NULL, addr_space, NULL);
	dwarf_search_unwind_table(addr_space, 0, NULL, NULL, 0, NULL);
	return 0;
}

int main10(void)
{
	printf("error message: %s\n", audit_errno_to_name(0));
	return audit_open();
}

int main11(void)
{
	return SLsmg_init_smg();
}

int main12(int argc, char *argv[])
{
        gtk_init(&argc, &argv);

        return 0;
}

int main13(void)
{
	gtk_info_bar_new();

	return 0;
}

int main14(void)
{
	perl_alloc();

	return 0;
}

int main15(void)
{
	Py_Initialize();
	return 0;
}

#if PY_VERSION_HEX >= 0x03000000
	#error
#endif

int main16(void)
{
	return 0;
}

int main17(void)
{
	bfd_demangle(0, 0, 0);
	return 0;
}

void exit_function(int x, void *y)
{
}

int main18(void)
{
	return on_exit(exit_function, NULL);
}

int main19(void)
{
	void *backtrace_fns[1];
	size_t entries;

	entries = backtrace(backtrace_fns, 1);
	backtrace_symbols(backtrace_fns, entries);

	return 0;
}

int main20(void)
{
	numa_available();
	return 0;
}

int main(int argc, char *argv[])
{
	main1();
	main2();
	main3();
	main4();
	main5();
	main6();
	main7();
	main8();
	main9();
	main10();
	main11();
	main12(argc, argv);
	main13();
	main14();
	main15();
	main16();
	main17();
	main18();
	main19();
	main20();

	return 0;
}
