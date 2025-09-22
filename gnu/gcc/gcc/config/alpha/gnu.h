/* Configuration for an Alpha running GNU with ELF as the target machine.  */

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (Alpha GNU)");

#undef TARGET_OS_CPP_BUILTINS /* config.gcc includes alpha/linux.h.  */
#define TARGET_OS_CPP_BUILTINS()		\
    do {					\
	HURD_TARGET_OS_CPP_BUILTINS();		\
	builtin_define ("_LONGLONG");		\
    } while (0)

#undef ELF_DYNAMIC_LINKER
#define ELF_DYNAMIC_LINKER	"/lib/ld.so"

#undef	STARTFILE_SPEC
#define STARTFILE_SPEC \
  "%{!shared: \
     %{!static: \
       %{pg:gcrt1.o%s} %{!pg:%{p:gcrt1.o%s} %{!p:crt1.o%s}}} \
     %{static:crt0.o%s}} \
   crti.o%s \
   %{!static:%{!shared:crtbegin.o%s} %{shared:crtbeginS.o%s}}"

/* FIXME: Is a Hurd-specific fallback mechanism necessary?  */
#undef MD_UNWIND_SUPPORT
