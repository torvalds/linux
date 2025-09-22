/* Configuration for an i386 running Mach as the target machine.  */

#define TARGET_VERSION fprintf (stderr, " (80386, Mach)"); 

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define_std ("unix");		\
	builtin_define_std ("MACH");		\
	builtin_assert ("system=unix");		\
	builtin_assert ("system=mach");		\
    }						\
  while (0)

/* Specify extra dir to search for include files.  */
#define SYSTEM_INCLUDE_DIR "/usr/mach/include"

/* Don't default to pcc-struct-return, because gcc is the only compiler, and
   we want to retain compatibility with older gcc versions.  */
#define DEFAULT_PCC_STRUCT_RETURN 0
