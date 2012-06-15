define SOURCE_HELLO
#include <stdio.h>
int main(void)
{
	return puts(\"hi\");
}
endef

ifndef NO_DWARF
define SOURCE_DWARF
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/version.h>
#ifndef _ELFUTILS_PREREQ
#error
#endif

int main(void)
{
	Dwarf *dbg = dwarf_begin(0, DWARF_C_READ);
	return (long)dbg;
}
endef
endif

define SOURCE_LIBELF
#include <libelf.h>

int main(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ, 0);
	return (long)elf;
}
endef

define SOURCE_GLIBC
#include <gnu/libc-version.h>

int main(void)
{
	const char *version = gnu_get_libc_version();
	return (long)version;
}
endef

define SOURCE_ELF_MMAP
#include <libelf.h>
int main(void)
{
	Elf *elf = elf_begin(0, ELF_C_READ_MMAP, 0);
	return (long)elf;
}
endef

ifndef NO_NEWT
define SOURCE_NEWT
#include <newt.h>

int main(void)
{
	newtInit();
	newtCls();
	return newtFinished();
}
endef
endif

ifndef NO_GTK2
define SOURCE_GTK2
#pragma GCC diagnostic ignored \"-Wstrict-prototypes\"
#include <gtk/gtk.h>
#pragma GCC diagnostic error \"-Wstrict-prototypes\"

int main(int argc, char *argv[])
{
        gtk_init(&argc, &argv);

        return 0;
}
endef
endif

ifndef NO_LIBPERL
define SOURCE_PERL_EMBED
#include <EXTERN.h>
#include <perl.h>

int main(void)
{
perl_alloc();
return 0;
}
endef
endif

ifndef NO_LIBPYTHON
define SOURCE_PYTHON_VERSION
#include <Python.h>
#if PY_VERSION_HEX >= 0x03000000
	#error
#endif
int main(void){}
endef
define SOURCE_PYTHON_EMBED
#include <Python.h>
int main(void)
{
	Py_Initialize();
	return 0;
}
endef
endif

define SOURCE_BFD
#include <bfd.h>

int main(void)
{
	bfd_demangle(0, 0, 0);
	return 0;
}
endef

define SOURCE_CPLUS_DEMANGLE
extern char *cplus_demangle(const char *, int);

int main(void)
{
	cplus_demangle(0, 0);
	return 0;
}
endef

define SOURCE_STRLCPY
#include <stdlib.h>
extern size_t strlcpy(char *dest, const char *src, size_t size);

int main(void)
{
	strlcpy(NULL, NULL, 0);
	return 0;
}
endef
