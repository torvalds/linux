/* Definitions for ia64-linux target.  */

/* This macro is a C statement to print on `stderr' a string describing the
   particular machine description choice.  */

#define TARGET_VERSION fprintf (stderr, " (OpenBSD/ia64)");

/* Target OS builtins.  */
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
	builtin_define ("__unix__");		\
	builtin_define ("__OpenBSD__");		\
	builtin_assert ("system=unix");		\
	builtin_assert ("system=bsd");		\
	builtin_assert ("system=OpenBSD");	\
    }						\
  while (0)

