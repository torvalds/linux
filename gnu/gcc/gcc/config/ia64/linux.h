/* Definitions for ia64-linux target.  */

/* This macro is a C statement to print on `stderr' a string describing the
   particular machine description choice.  */

#define TARGET_VERSION fprintf (stderr, " (IA-64) Linux");

/* This is for -profile to use -lc_p instead of -lc.  */
#undef CC1_SPEC
#define CC1_SPEC "%{profile:-p} %{G*}"

/* Target OS builtins.  */
#define TARGET_OS_CPP_BUILTINS()		\
do {						\
	LINUX_TARGET_OS_CPP_BUILTINS();		\
	builtin_define("_LONGLONG");		\
} while (0)

/* Need to override linux.h STARTFILE_SPEC, since it has crtbeginT.o in.  */
#undef STARTFILE_SPEC
#ifdef HAVE_LD_PIE
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p|profile:gcrt1.o%s;pie:Scrt1.o%s;:crt1.o%s}}\
   crti.o%s %{shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#else
#define STARTFILE_SPEC \
  "%{!shared: %{pg|p|profile:gcrt1.o%s;:crt1.o%s}}\
   crti.o%s %{shared|pie:crtbeginS.o%s;:crtbegin.o%s}"
#endif

/* Similar to standard Linux, but adding -ffast-math support.  */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC \
  "%{ffast-math|funsafe-math-optimizations:crtfastmath.o%s} \
   %{shared|pie:crtendS.o%s;:crtend.o%s} crtn.o%s"

/* Define this for shared library support because it isn't in the main
   linux.h file.  */

#define GLIBC_DYNAMIC_LINKER "/lib/ld-linux-ia64.so.2"

#undef LINK_SPEC
#define LINK_SPEC "\
  %{shared:-shared} \
  %{!shared: \
    %{!static: \
      %{rdynamic:-export-dynamic} \
      %{!dynamic-linker:-dynamic-linker " LINUX_DYNAMIC_LINKER "}} \
      %{static:-static}}"

#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

#define JMP_BUF_SIZE  76

/* Override linux.h LINK_EH_SPEC definition.
   Signalize that because we have fde-glibc, we don't need all C shared libs
   linked against -lgcc_s.  */
#undef LINK_EH_SPEC
#define LINK_EH_SPEC ""

#define MD_UNWIND_SUPPORT "config/ia64/linux-unwind.h"
