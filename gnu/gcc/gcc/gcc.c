/* Compiler driver program that can handle many languages.
   Copyright (C) 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation,
   Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.

This paragraph is here to try to keep Sun CC from dying.
The number of chars here seems crucial!!!!  */

/* This program is the user interface to the C compiler and possibly to
other compilers.  It is used because compilation is a complicated procedure
which involves running several programs and passing temporary files between
them, forwarding the users switches to those programs selectively,
and deleting the temporary files at the end.

CC recognizes how to compile each input file by suffixes in the file names.
Once it knows which kind of compilation to perform, the procedure for
compilation is specified by a string called a "spec".  */

/* A Short Introduction to Adding a Command-Line Option.

   Before adding a command-line option, consider if it is really
   necessary.  Each additional command-line option adds complexity and
   is difficult to remove in subsequent versions.

   In the following, consider adding the command-line argument
   `--bar'.

   1. Each command-line option is specified in the specs file.  The
   notation is described below in the comment entitled "The Specs
   Language".  Read it.

   2. In this file, add an entry to "option_map" equating the long
   `--' argument version and any shorter, single letter version.  Read
   the comments in the declaration of "struct option_map" for an
   explanation.  Do not omit the first `-'.

   3. Look in the "specs" file to determine which program or option
   list should be given the argument, e.g., "cc1_options".  Add the
   appropriate syntax for the shorter option version to the
   corresponding "const char *" entry in this file.  Omit the first
   `-' from the option.  For example, use `-bar', rather than `--bar'.

   4. If the argument takes an argument, e.g., `--baz argument1',
   modify either DEFAULT_SWITCH_TAKES_ARG or
   DEFAULT_WORD_SWITCH_TAKES_ARG in gcc.h.  Omit the first `-'
   from `--baz'.

   5. Document the option in this file's display_help().  If the
   option is passed to a subprogram, modify its corresponding
   function, e.g., cppinit.c:print_help() or toplev.c:display_help(),
   instead.

   6. Compile and test.  Make sure that your new specs file is being
   read.  For example, use a debugger to investigate the value of
   "specs_file" in main().  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "multilib.h" /* before tm.h */
#include "tm.h"
#include <signal.h>
#if ! defined( SIGCHLD ) && defined( SIGCLD )
#  define SIGCHLD SIGCLD
#endif
#include "xregex.h"
#include "obstack.h"
#include "intl.h"
#include "prefix.h"
#include "gcc.h"
#include "flags.h"
#include "opts.h"

/* By default there is no special suffix for target executables.  */
/* FIXME: when autoconf is fixed, remove the host check - dj */
#if defined(TARGET_EXECUTABLE_SUFFIX) && defined(HOST_EXECUTABLE_SUFFIX)
#define HAVE_TARGET_EXECUTABLE_SUFFIX
#endif

/* By default there is no special suffix for host executables.  */
#ifdef HOST_EXECUTABLE_SUFFIX
#define HAVE_HOST_EXECUTABLE_SUFFIX
#else
#define HOST_EXECUTABLE_SUFFIX ""
#endif

/* By default, the suffix for target object files is ".o".  */
#ifdef TARGET_OBJECT_SUFFIX
#define HAVE_TARGET_OBJECT_SUFFIX
#else
#define TARGET_OBJECT_SUFFIX ".o"
#endif

static const char dir_separator_str[] = { DIR_SEPARATOR, 0 };

/* Most every one is fine with LIBRARY_PATH.  For some, it conflicts.  */
#ifndef LIBRARY_PATH_ENV
#define LIBRARY_PATH_ENV "LIBRARY_PATH"
#endif

#ifndef HAVE_KILL
#define kill(p,s) raise(s)
#endif

/* If a stage of compilation returns an exit status >= 1,
   compilation of that file ceases.  */

#define MIN_FATAL_STATUS 1

/* Flag set by cppspec.c to 1.  */
int is_cpp_driver;

/* Flag saying to pass the greatest exit code returned by a sub-process
   to the calling program.  */
static int pass_exit_codes;

/* Definition of string containing the arguments given to configure.  */
#include "configargs.h"

/* Flag saying to print the directories gcc will search through looking for
   programs, libraries, etc.  */

static int print_search_dirs;

/* Flag saying to print the full filename of this file
   as found through our usual search mechanism.  */

static const char *print_file_name = NULL;

/* As print_file_name, but search for executable file.  */

static const char *print_prog_name = NULL;

/* Flag saying to print the relative path we'd use to
   find libgcc.a given the current compiler flags.  */

static int print_multi_directory;

/* Flag saying to print the relative path we'd use to
   find OS libraries given the current compiler flags.  */

static int print_multi_os_directory;

/* Flag saying to print the list of subdirectories and
   compiler flags used to select them in a standard form.  */

static int print_multi_lib;

/* Flag saying to print the command line options understood by gcc and its
   sub-processes.  */

static int print_help_list;

/* Flag indicating whether we should print the command and arguments */

static int verbose_flag;

/* Flag indicating whether we should ONLY print the command and
   arguments (like verbose_flag) without executing the command.
   Displayed arguments are quoted so that the generated command
   line is suitable for execution.  This is intended for use in
   shell scripts to capture the driver-generated command line.  */
static int verbose_only_flag;

/* Flag indicating to print target specific command line options.  */

static int target_help_flag;

/* Flag indicating whether we should report subprocess execution times
   (if this is supported by the system - see pexecute.c).  */

static int report_times;

/* Nonzero means place this string before uses of /, so that include
   and library files can be found in an alternate location.  */

#ifdef TARGET_SYSTEM_ROOT
static const char *target_system_root = TARGET_SYSTEM_ROOT;
#else
static const char *target_system_root = 0;
#endif

/* Nonzero means pass the updated target_system_root to the compiler.  */

static int target_system_root_changed;

/* Nonzero means append this string to target_system_root.  */

static const char *target_sysroot_suffix = 0;

/* Nonzero means append this string to target_system_root for headers.  */

static const char *target_sysroot_hdrs_suffix = 0;

/* Nonzero means write "temp" files in source directory
   and use the source file's name in them, and don't delete them.  */

static int save_temps_flag;

/* Nonzero means pass multiple source files to the compiler at one time.  */

static int combine_flag = 0;

/* Nonzero means use pipes to communicate between subprocesses.
   Overridden by either of the above two flags.  */

static int use_pipes;

/* The compiler version.  */

static const char *compiler_version;

/* The target version specified with -V */

static const char *const spec_version = DEFAULT_TARGET_VERSION;

/* The target machine specified with -b.  */

static const char *spec_machine = DEFAULT_TARGET_MACHINE;

/* Nonzero if cross-compiling.
   When -b is used, the value comes from the `specs' file.  */

#ifdef CROSS_COMPILE
static const char *cross_compile = "1";
#else
static const char *cross_compile = "0";
#endif

#ifdef MODIFY_TARGET_NAME

/* Information on how to alter the target name based on a command-line
   switch.  The only case we support now is simply appending or deleting a
   string to or from the end of the first part of the configuration name.  */

static const struct modify_target
{
  const char *const sw;
  const enum add_del {ADD, DELETE} add_del;
  const char *const str;
}
modify_target[] = MODIFY_TARGET_NAME;
#endif

/* The number of errors that have occurred; the link phase will not be
   run if this is nonzero.  */
static int error_count = 0;

/* Greatest exit code of sub-processes that has been encountered up to
   now.  */
static int greatest_status = 1;

/* This is the obstack which we use to allocate many strings.  */

static struct obstack obstack;

/* This is the obstack to build an environment variable to pass to
   collect2 that describes all of the relevant switches of what to
   pass the compiler in building the list of pointers to constructors
   and destructors.  */

static struct obstack collect_obstack;

/* Forward declaration for prototypes.  */
struct path_prefix;
struct prefix_list;

static void init_spec (void);
static void store_arg (const char *, int, int);
static char *load_specs (const char *);
static void read_specs (const char *, int);
static void set_spec (const char *, const char *);
static struct compiler *lookup_compiler (const char *, size_t, const char *);
static char *build_search_list (const struct path_prefix *, const char *,
				bool, bool);
static void putenv_from_prefixes (const struct path_prefix *, const char *,
				  bool);
static int access_check (const char *, int);
static char *find_a_file (const struct path_prefix *, const char *, int, bool);
static void add_prefix (struct path_prefix *, const char *, const char *,
			int, int, int);
static void add_sysrooted_prefix (struct path_prefix *, const char *,
				  const char *, int, int, int);
static void translate_options (int *, const char *const **);
static char *skip_whitespace (char *);
static void delete_if_ordinary (const char *);
static void delete_temp_files (void);
static void delete_failure_queue (void);
static void clear_failure_queue (void);
static int check_live_switch (int, int);
static const char *handle_braces (const char *);
static inline bool input_suffix_matches (const char *, const char *);
static inline bool switch_matches (const char *, const char *, int);
static inline void mark_matching_switches (const char *, const char *, int);
static inline void process_marked_switches (void);
static const char *process_brace_body (const char *, const char *, const char *, int, int);
static const struct spec_function *lookup_spec_function (const char *);
static const char *eval_spec_function (const char *, const char *);
static const char *handle_spec_function (const char *);
static char *save_string (const char *, int);
static void set_collect_gcc_options (void);
static int do_spec_1 (const char *, int, const char *);
static int do_spec_2 (const char *);
static void do_option_spec (const char *, const char *);
static void do_self_spec (const char *);
static const char *find_file (const char *);
static int is_directory (const char *, bool);
static const char *validate_switches (const char *);
static void validate_all_switches (void);
static inline void validate_switches_from_spec (const char *);
static void give_switch (int, int);
static int used_arg (const char *, int);
static int default_arg (const char *, int);
static void set_multilib_dir (void);
static void print_multilib_info (void);
static void perror_with_name (const char *);
static void fatal_ice (const char *, ...) ATTRIBUTE_PRINTF_1 ATTRIBUTE_NORETURN;
static void notice (const char *, ...) ATTRIBUTE_PRINTF_1;
static void display_help (void);
static void add_preprocessor_option (const char *, int);
static void add_assembler_option (const char *, int);
static void add_linker_option (const char *, int);
static void process_command (int, const char **);
static int execute (void);
static void alloc_args (void);
static void clear_args (void);
static void fatal_error (int);
#if defined(ENABLE_SHARED_LIBGCC) && !defined(REAL_LIBGCC_SPEC)
static void init_gcc_specs (struct obstack *, const char *, const char *,
			    const char *);
#endif
#if defined(HAVE_TARGET_OBJECT_SUFFIX) || defined(HAVE_TARGET_EXECUTABLE_SUFFIX)
static const char *convert_filename (const char *, int, int);
#endif

static const char *if_exists_spec_function (int, const char **);
static const char *if_exists_else_spec_function (int, const char **);
static const char *replace_outfile_spec_function (int, const char **);
static const char *version_compare_spec_function (int, const char **);
static const char *include_spec_function (int, const char **);

/* The Specs Language

Specs are strings containing lines, each of which (if not blank)
is made up of a program name, and arguments separated by spaces.
The program name must be exact and start from root, since no path
is searched and it is unreliable to depend on the current working directory.
Redirection of input or output is not supported; the subprograms must
accept filenames saying what files to read and write.

In addition, the specs can contain %-sequences to substitute variable text
or for conditional text.  Here is a table of all defined %-sequences.
Note that spaces are not generated automatically around the results of
expanding these sequences; therefore, you can concatenate them together
or with constant text in a single argument.

 %%	substitute one % into the program name or argument.
 %i     substitute the name of the input file being processed.
 %b     substitute the basename of the input file being processed.
	This is the substring up to (and not including) the last period
	and not including the directory.
 %B	same as %b, but include the file suffix (text after the last period).
 %gSUFFIX
	substitute a file name that has suffix SUFFIX and is chosen
	once per compilation, and mark the argument a la %d.  To reduce
	exposure to denial-of-service attacks, the file name is now
	chosen in a way that is hard to predict even when previously
	chosen file names are known.  For example, `%g.s ... %g.o ... %g.s'
	might turn into `ccUVUUAU.s ccXYAXZ12.o ccUVUUAU.s'.  SUFFIX matches
	the regexp "[.0-9A-Za-z]*%O"; "%O" is treated exactly as if it
	had been pre-processed.  Previously, %g was simply substituted
	with a file name chosen once per compilation, without regard
	to any appended suffix (which was therefore treated just like
	ordinary text), making such attacks more likely to succeed.
 %|SUFFIX
	like %g, but if -pipe is in effect, expands simply to "-".
 %mSUFFIX
        like %g, but if -pipe is in effect, expands to nothing.  (We have both
	%| and %m to accommodate differences between system assemblers; see
	the AS_NEEDS_DASH_FOR_PIPED_INPUT target macro.)
 %uSUFFIX
	like %g, but generates a new temporary file name even if %uSUFFIX
	was already seen.
 %USUFFIX
	substitutes the last file name generated with %uSUFFIX, generating a
	new one if there is no such last file name.  In the absence of any
	%uSUFFIX, this is just like %gSUFFIX, except they don't share
	the same suffix "space", so `%g.s ... %U.s ... %g.s ... %U.s'
	would involve the generation of two distinct file names, one
	for each `%g.s' and another for each `%U.s'.  Previously, %U was
	simply substituted with a file name chosen for the previous %u,
	without regard to any appended suffix.
 %jSUFFIX
        substitutes the name of the HOST_BIT_BUCKET, if any, and if it is
        writable, and if save-temps is off; otherwise, substitute the name
        of a temporary file, just like %u.  This temporary file is not
        meant for communication between processes, but rather as a junk
        disposal mechanism.
 %.SUFFIX
        substitutes .SUFFIX for the suffixes of a matched switch's args when
        it is subsequently output with %*. SUFFIX is terminated by the next
        space or %.
 %d	marks the argument containing or following the %d as a
	temporary file name, so that that file will be deleted if CC exits
	successfully.  Unlike %g, this contributes no text to the argument.
 %w	marks the argument containing or following the %w as the
	"output file" of this compilation.  This puts the argument
	into the sequence of arguments that %o will substitute later.
 %V	indicates that this compilation produces no "output file".
 %W{...}
	like %{...} but mark last argument supplied within
	as a file to be deleted on failure.
 %o	substitutes the names of all the output files, with spaces
	automatically placed around them.  You should write spaces
	around the %o as well or the results are undefined.
	%o is for use in the specs for running the linker.
	Input files whose names have no recognized suffix are not compiled
	at all, but they are included among the output files, so they will
	be linked.
 %O	substitutes the suffix for object files.  Note that this is
        handled specially when it immediately follows %g, %u, or %U
	(with or without a suffix argument) because of the need for
	those to form complete file names.  The handling is such that
	%O is treated exactly as if it had already been substituted,
	except that %g, %u, and %U do not currently support additional
	SUFFIX characters following %O as they would following, for
	example, `.o'.
 %I	Substitute any of -iprefix (made from GCC_EXEC_PREFIX), -isysroot
	(made from TARGET_SYSTEM_ROOT), -isystem (made from COMPILER_PATH
	and -B options) and -imultilib as necessary.
 %s     current argument is the name of a library or startup file of some sort.
        Search for that file in a standard list of directories
	and substitute the full name found.
 %eSTR  Print STR as an error message.  STR is terminated by a newline.
        Use this when inconsistent options are detected.
 %nSTR  Print STR as a notice.  STR is terminated by a newline.
 %x{OPTION}	Accumulate an option for %X.
 %X	Output the accumulated linker options specified by compilations.
 %Y	Output the accumulated assembler options specified by compilations.
 %Z	Output the accumulated preprocessor options specified by compilations.
 %a     process ASM_SPEC as a spec.
        This allows config.h to specify part of the spec for running as.
 %A	process ASM_FINAL_SPEC as a spec.  A capital A is actually
	used here.  This can be used to run a post-processor after the
	assembler has done its job.
 %D	Dump out a -L option for each directory in startfile_prefixes.
	If multilib_dir is set, extra entries are generated with it affixed.
 %l     process LINK_SPEC as a spec.
 %L     process LIB_SPEC as a spec.
 %G     process LIBGCC_SPEC as a spec.
 %R     Output the concatenation of target_system_root and
        target_sysroot_suffix.
 %S     process STARTFILE_SPEC as a spec.  A capital S is actually used here.
 %E     process ENDFILE_SPEC as a spec.  A capital E is actually used here.
 %C     process CPP_SPEC as a spec.
 %1	process CC1_SPEC as a spec.
 %2	process CC1PLUS_SPEC as a spec.
 %*	substitute the variable part of a matched option.  (See below.)
	Note that each comma in the substituted string is replaced by
	a single space.
 %<S    remove all occurrences of -S from the command line.
        Note - this command is position dependent.  % commands in the
        spec string before this one will see -S, % commands in the
        spec string after this one will not.
 %<S*	remove all occurrences of all switches beginning with -S from the
        command line.
 %:function(args)
	Call the named function FUNCTION, passing it ARGS.  ARGS is
	first processed as a nested spec string, then split into an
	argument vector in the usual fashion.  The function returns
	a string which is processed as if it had appeared literally
	as part of the current spec.
 %{S}   substitutes the -S switch, if that switch was given to CC.
	If that switch was not specified, this substitutes nothing.
	Here S is a metasyntactic variable.
 %{S*}  substitutes all the switches specified to CC whose names start
	with -S.  This is used for -o, -I, etc; switches that take
	arguments.  CC considers `-o foo' as being one switch whose
	name starts with `o'.  %{o*} would substitute this text,
	including the space; thus, two arguments would be generated.
 %{S*&T*} likewise, but preserve order of S and T options (the order
	of S and T in the spec is not significant).  Can be any number
	of ampersand-separated variables; for each the wild card is
	optional.  Useful for CPP as %{D*&U*&A*}.

 %{S:X}   substitutes X, if the -S switch was given to CC.
 %{!S:X}  substitutes X, if the -S switch was NOT given to CC.
 %{S*:X}  substitutes X if one or more switches whose names start
          with -S was given to CC.  Normally X is substituted only
          once, no matter how many such switches appeared.  However,
          if %* appears somewhere in X, then X will be substituted
          once for each matching switch, with the %* replaced by the
          part of that switch that matched the '*'.
 %{.S:X}  substitutes X, if processing a file with suffix S.
 %{!.S:X} substitutes X, if NOT processing a file with suffix S.

 %{S|T:X} substitutes X if either -S or -T was given to CC.  This may be
	  combined with !, ., and * as above binding stronger than the OR.
	  If %* appears in X, all of the alternatives must be starred, and
	  only the first matching alternative is substituted.
 %{S:X;   if S was given to CC, substitutes X;
   T:Y;   else if T was given to CC, substitutes Y;
    :D}   else substitutes D.  There can be as many clauses as you need.
          This may be combined with ., !, |, and * as above.

 %(Spec) processes a specification defined in a specs file as *Spec:
 %[Spec] as above, but put __ around -D arguments

The conditional text X in a %{S:X} or similar construct may contain
other nested % constructs or spaces, or even newlines.  They are
processed as usual, as described above.  Trailing white space in X is
ignored.  White space may also appear anywhere on the left side of the
colon in these constructs, except between . or * and the corresponding
word.

The -O, -f, -m, and -W switches are handled specifically in these
constructs.  If another value of -O or the negated form of a -f, -m, or
-W switch is found later in the command line, the earlier switch
value is ignored, except with {S*} where S is just one letter; this
passes all matching options.

The character | at the beginning of the predicate text is used to indicate
that a command should be piped to the following command, but only if -pipe
is specified.

Note that it is built into CC which switches take arguments and which
do not.  You might think it would be useful to generalize this to
allow each compiler's spec to say which switches take arguments.  But
this cannot be done in a consistent fashion.  CC cannot even decide
which input files have been specified without knowing which switches
take arguments, and it must know which input files to compile in order
to tell which compilers to run.

CC also knows implicitly that arguments starting in `-l' are to be
treated as compiler output files, and passed to the linker in their
proper position among the other output files.  */

/* Define the macros used for specs %a, %l, %L, %S, %C, %1.  */

/* config.h can define ASM_SPEC to provide extra args to the assembler
   or extra switch-translations.  */
#ifndef ASM_SPEC
#define ASM_SPEC ""
#endif

/* config.h can define ASM_FINAL_SPEC to run a post processor after
   the assembler has run.  */
#ifndef ASM_FINAL_SPEC
#define ASM_FINAL_SPEC ""
#endif

/* config.h can define CPP_SPEC to provide extra args to the C preprocessor
   or extra switch-translations.  */
#ifndef CPP_SPEC
#define CPP_SPEC ""
#endif

/* config.h can define CC1_SPEC to provide extra args to cc1 and cc1plus
   or extra switch-translations.  */
#ifndef CC1_SPEC
#define CC1_SPEC ""
#endif

/* config.h can define CC1PLUS_SPEC to provide extra args to cc1plus
   or extra switch-translations.  */
#ifndef CC1PLUS_SPEC
#define CC1PLUS_SPEC ""
#endif

/* config.h can define LINK_SPEC to provide extra args to the linker
   or extra switch-translations.  */
#ifndef LINK_SPEC
#define LINK_SPEC ""
#endif

/* config.h can define LIB_SPEC to override the default libraries.  */
#ifndef LIB_SPEC
#define LIB_SPEC "%{!shared:%{g*:-lg} %{!p:%{!pg:-lc}}%{p:-lc_p}%{pg:-lc_p}}"
#endif

/* mudflap specs */
#ifndef MFWRAP_SPEC
/* XXX: valid only for GNU ld */
/* XXX: should exactly match hooks provided by libmudflap.a */
#define MFWRAP_SPEC " %{static: %{fmudflap|fmudflapth: \
 --wrap=malloc --wrap=free --wrap=calloc --wrap=realloc\
 --wrap=mmap --wrap=munmap --wrap=alloca\
} %{fmudflapth: --wrap=pthread_create\
}} %{fmudflap|fmudflapth: --wrap=main}"
#endif
#ifndef MFLIB_SPEC
#define MFLIB_SPEC "%{fmudflap|fmudflapth: -export-dynamic}"
#endif

/* config.h can define LIBGCC_SPEC to override how and when libgcc.a is
   included.  */
#ifndef LIBGCC_SPEC
#if defined(REAL_LIBGCC_SPEC)
#define LIBGCC_SPEC REAL_LIBGCC_SPEC
#elif defined(LINK_LIBGCC_SPECIAL_1)
/* Have gcc do the search for libgcc.a.  */
#define LIBGCC_SPEC "libgcc.a%s"
#else
#define LIBGCC_SPEC "-lgcc"
#endif
#endif

/* config.h can define STARTFILE_SPEC to override the default crt0 files.  */
#ifndef STARTFILE_SPEC
#define STARTFILE_SPEC  \
  "%{!shared:%{pg:gcrt0%O%s}%{!pg:%{p:mcrt0%O%s}%{!p:crt0%O%s}}}"
#endif

/* config.h can define SWITCHES_NEED_SPACES to control which options
   require spaces between the option and the argument.  */
#ifndef SWITCHES_NEED_SPACES
#define SWITCHES_NEED_SPACES ""
#endif

/* config.h can define ENDFILE_SPEC to override the default crtn files.  */
#ifndef ENDFILE_SPEC
#define ENDFILE_SPEC ""
#endif

#ifndef LINKER_NAME
#define LINKER_NAME "collect2"
#endif

/* Define ASM_DEBUG_SPEC to be a spec suitable for translating '-g'
   to the assembler.  */
#ifndef ASM_DEBUG_SPEC
# if defined(DBX_DEBUGGING_INFO) && defined(DWARF2_DEBUGGING_INFO) \
     && defined(HAVE_AS_GDWARF2_DEBUG_FLAG) && defined(HAVE_AS_GSTABS_DEBUG_FLAG)
#  define ASM_DEBUG_SPEC					\
      (PREFERRED_DEBUGGING_TYPE == DBX_DEBUG			\
       ? "%{gdwarf-2*:--gdwarf2}%{!gdwarf-2*:%{g*:--gstabs}}"	\
       : "%{gstabs*:--gstabs}%{!gstabs*:%{g*:--gdwarf2}}")
# else
#  if defined(DBX_DEBUGGING_INFO) && defined(HAVE_AS_GSTABS_DEBUG_FLAG)
#   define ASM_DEBUG_SPEC "%{g*:--gstabs}"
#  endif
#  if defined(DWARF2_DEBUGGING_INFO) && defined(HAVE_AS_GDWARF2_DEBUG_FLAG)
#   define ASM_DEBUG_SPEC "%{g*:--gdwarf2}"
#  endif
# endif
#endif
#ifndef ASM_DEBUG_SPEC
# define ASM_DEBUG_SPEC ""
#endif

/* Here is the spec for running the linker, after compiling all files.  */

/* This is overridable by the target in case they need to specify the
   -lgcc and -lc order specially, yet not require them to override all
   of LINK_COMMAND_SPEC.  */
#ifndef LINK_GCC_C_SEQUENCE_SPEC
#define LINK_GCC_C_SEQUENCE_SPEC "%G %L %G"
#endif

#ifndef LINK_SSP_SPEC
#ifdef TARGET_LIBC_PROVIDES_SSP
#define LINK_SSP_SPEC "%{fstack-protector:}"
#else
#define LINK_SSP_SPEC "%{fstack-protector|fstack-protector-strong|fstack-protector-all:-lssp_nonshared -lssp}"
#endif
#endif

#ifndef LINK_PIE_SPEC
#ifdef HAVE_LD_PIE
#define LINK_PIE_SPEC "%{pie:-pie} %{p|pg|nopie:-nopie} "
#else
#define LINK_PIE_SPEC "%{pie:} "
#endif
#endif

/* -u* was put back because both BSD and SysV seem to support it.  */
/* %{static:} simply prevents an error message if the target machine
   doesn't handle -static.  */
/* We want %{T*} after %{L*} and %D so that it can be used to specify linker
   scripts which exist in user specified directories, or in standard
   directories.  */
#ifndef LINK_COMMAND_SPEC
#define LINK_COMMAND_SPEC "\
%{!fsyntax-only:%{!c:%{!M:%{!MM:%{!E:%{!S:\
    %(linker) %l " LINK_PIE_SPEC "%X %{o*} %{A} %{d} %{e*} %{m} %{N} %{n} %{r}\
    %{s} %{t} %{u*} %{x} %{z} %{Z} %{!A:%{!nostdlib:%{!nostartfiles:%S}}}\
    %{static:} %{L*} %(mfwrap) %(link_libgcc) %o\
    %{fopenmp:%:include(libgomp.spec)%(link_gomp)} %(mflib)\
    %{fprofile-arcs|fprofile-generate|coverage:-lgcov}\
    %{!nostdlib:%{!nodefaultlibs:%(link_ssp) %(link_gcc_c_sequence)}}\
    %{!A:%{!nostdlib:%{!nostartfiles:%E}}} %{T*} }}}}}}"
#endif

#ifndef LINK_LIBGCC_SPEC
/* Generate -L options for startfile prefix list.  */
# define LINK_LIBGCC_SPEC "%D"
#endif

#ifndef STARTFILE_PREFIX_SPEC
# define STARTFILE_PREFIX_SPEC ""
#endif

#ifndef SYSROOT_SPEC
# define SYSROOT_SPEC "--sysroot=%R"
#endif

#ifndef SYSROOT_SUFFIX_SPEC
# define SYSROOT_SUFFIX_SPEC ""
#endif

#ifndef SYSROOT_HEADERS_SUFFIX_SPEC
# define SYSROOT_HEADERS_SUFFIX_SPEC ""
#endif

static const char *asm_debug;
static const char *cpp_spec = CPP_SPEC;
static const char *cc1_spec = CC1_SPEC;
static const char *cc1plus_spec = CC1PLUS_SPEC;
static const char *link_gcc_c_sequence_spec = LINK_GCC_C_SEQUENCE_SPEC;
static const char *link_ssp_spec = LINK_SSP_SPEC;
static const char *asm_spec = ASM_SPEC;
static const char *asm_final_spec = ASM_FINAL_SPEC;
static const char *link_spec = LINK_SPEC;
static const char *lib_spec = LIB_SPEC;
static const char *mfwrap_spec = MFWRAP_SPEC;
static const char *mflib_spec = MFLIB_SPEC;
static const char *link_gomp_spec = "";
static const char *libgcc_spec = LIBGCC_SPEC;
static const char *endfile_spec = ENDFILE_SPEC;
static const char *startfile_spec = STARTFILE_SPEC;
static const char *switches_need_spaces = SWITCHES_NEED_SPACES;
static const char *linker_name_spec = LINKER_NAME;
static const char *link_command_spec = LINK_COMMAND_SPEC;
static const char *link_libgcc_spec = LINK_LIBGCC_SPEC;
static const char *startfile_prefix_spec = STARTFILE_PREFIX_SPEC;
static const char *sysroot_spec = SYSROOT_SPEC;
static const char *sysroot_suffix_spec = SYSROOT_SUFFIX_SPEC;
static const char *sysroot_hdrs_suffix_spec = SYSROOT_HEADERS_SUFFIX_SPEC;

/* Standard options to cpp, cc1, and as, to reduce duplication in specs.
   There should be no need to override these in target dependent files,
   but we need to copy them to the specs file so that newer versions
   of the GCC driver can correctly drive older tool chains with the
   appropriate -B options.  */

/* When cpplib handles traditional preprocessing, get rid of this, and
   call cc1 (or cc1obj in objc/lang-specs.h) from the main specs so
   that we default the front end language better.  */
static const char *trad_capable_cpp =
"cc1 -E %{traditional|ftraditional|traditional-cpp:-traditional-cpp}";

/* We don't wrap .d files in %W{} since a missing .d file, and
   therefore no dependency entry, confuses make into thinking a .o
   file that happens to exist is up-to-date.  */
static const char *cpp_unique_options =
"%{C|CC:%{!E:%eGCC does not support -C or -CC without -E}}\
 %{!Q:-quiet} %{nostdinc*} %{C} %{CC} %{v} %{I*&F*} %{P} %I\
 %{MD:-MD %{!o:%b.d}%{o*:%.d%*}}\
 %{MMD:-MMD %{!o:%b.d}%{o*:%.d%*}}\
 %{M} %{MM} %{MF*} %{MG} %{MP} %{MQ*} %{MT*}\
 %{!E:%{!M:%{!MM:%{MD|MMD:%{o*:-MQ %*}}}}}\
 %{remap} %{g3:-dD} %{H} %C %{D*&U*&A*} %{i*} %Z %i\
 %{fmudflap:-D_MUDFLAP -include mf-runtime.h}\
 %{fmudflapth:-D_MUDFLAP -D_MUDFLAPTH -include mf-runtime.h}\
 %{E|M|MM:%W{o*}}";

/* This contains cpp options which are common with cc1_options and are passed
   only when preprocessing only to avoid duplication.  We pass the cc1 spec
   options to the preprocessor so that it the cc1 spec may manipulate
   options used to set target flags.  Those special target flags settings may
   in turn cause preprocessor symbols to be defined specially.  */
static const char *cpp_options =
"%(cpp_unique_options) %1 %{m*} %{std*&ansi&trigraphs} %{W*&pedantic*} %{w}\
 %{f*} %{g*:%{!g0:%{!fno-working-directory:-fworking-directory}}} %{O*}\
 %{undef} %{save-temps:-fpch-preprocess}";

/* This contains cpp options which are not passed when the preprocessor
   output will be used by another program.  */
static const char *cpp_debug_options = "%{d*}";

/* NB: This is shared amongst all front-ends.  */
static const char *cc1_options =
"%{pg:%{fomit-frame-pointer:%e-pg and -fomit-frame-pointer are incompatible}}\
 %1 %{!Q:-quiet} -dumpbase %B %{d*} %{m*} %{a*}\
 %{c|S:%{o*:-auxbase-strip %*}%{!o*:-auxbase %b}}%{!c:%{!S:-auxbase %b}}\
 %{g*} %{O*} %{W*&pedantic*} %{w} %{std*&ansi&trigraphs}\
 %{v:-version} %{pg:-p} %{p} %{f*} %{undef}\
 %{Qn:-fno-ident} %{--help:--help}\
 %{--target-help:--target-help}\
 %{!fsyntax-only:%{S:%W{o*}%{!o*:-o %b.s}}}\
 %{fsyntax-only:-o %j} %{-param*}\
 %{fmudflap|fmudflapth:-fno-builtin -fno-merge-constants}\
 %{coverage:-fprofile-arcs -ftest-coverage}";

static const char *asm_options =
"%a %Y %{c:%W{o*}%{!o*:-o %w%b%O}}%{!c:-o %d%w%u%O}";

static const char *invoke_as =
#ifdef AS_NEEDS_DASH_FOR_PIPED_INPUT
"%{!S:-o %|.s |\n as %(asm_options) %|.s %A }";
#else
"%{!S:-o %|.s |\n as %(asm_options) %m.s %A }";
#endif

/* Some compilers have limits on line lengths, and the multilib_select
   and/or multilib_matches strings can be very long, so we build them at
   run time.  */
static struct obstack multilib_obstack;
static const char *multilib_select;
static const char *multilib_matches;
static const char *multilib_defaults;
static const char *multilib_exclusions;

/* Check whether a particular argument is a default argument.  */

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS { "" }
#endif

static const char *const multilib_defaults_raw[] = MULTILIB_DEFAULTS;

#ifndef DRIVER_SELF_SPECS
#define DRIVER_SELF_SPECS ""
#endif

/* Adding -fopenmp should imply pthreads.  This is particularly important
   for targets that use different start files and suchlike.  */
#ifndef GOMP_SELF_SPECS
#define GOMP_SELF_SPECS "%{fopenmp: -pthread}"
#endif

static const char *const driver_self_specs[] = {
  DRIVER_SELF_SPECS, GOMP_SELF_SPECS
};

#ifndef OPTION_DEFAULT_SPECS
#define OPTION_DEFAULT_SPECS { "", "" }
#endif

struct default_spec
{
  const char *name;
  const char *spec;
};

static const struct default_spec
  option_default_specs[] = { OPTION_DEFAULT_SPECS };

struct user_specs
{
  struct user_specs *next;
  const char *filename;
};

static struct user_specs *user_specs_head, *user_specs_tail;

#ifndef SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR) DEFAULT_SWITCH_TAKES_ARG(CHAR)
#endif

#ifndef WORD_SWITCH_TAKES_ARG
#define WORD_SWITCH_TAKES_ARG(STR) DEFAULT_WORD_SWITCH_TAKES_ARG (STR)
#endif

#ifdef HAVE_TARGET_EXECUTABLE_SUFFIX
/* This defines which switches stop a full compilation.  */
#define DEFAULT_SWITCH_CURTAILS_COMPILATION(CHAR) \
  ((CHAR) == 'c' || (CHAR) == 'S')

#ifndef SWITCH_CURTAILS_COMPILATION
#define SWITCH_CURTAILS_COMPILATION(CHAR) \
  DEFAULT_SWITCH_CURTAILS_COMPILATION(CHAR)
#endif
#endif

/* Record the mapping from file suffixes for compilation specs.  */

struct compiler
{
  const char *suffix;		/* Use this compiler for input files
				   whose names end in this suffix.  */

  const char *spec;		/* To use this compiler, run this spec.  */

  const char *cpp_spec;         /* If non-NULL, substitute this spec
				   for `%C', rather than the usual
				   cpp_spec.  */
  const int combinable;          /* If nonzero, compiler can deal with
				    multiple source files at once (IMA).  */
  const int needs_preprocessing; /* If nonzero, source files need to
				    be run through a preprocessor.  */
};

/* Pointer to a vector of `struct compiler' that gives the spec for
   compiling a file, based on its suffix.
   A file that does not end in any of these suffixes will be passed
   unchanged to the loader and nothing else will be done to it.

   An entry containing two 0s is used to terminate the vector.

   If multiple entries match a file, the last matching one is used.  */

static struct compiler *compilers;

/* Number of entries in `compilers', not counting the null terminator.  */

static int n_compilers;

/* The default list of file name suffixes and their compilation specs.  */

static const struct compiler default_compilers[] =
{
  /* Add lists of suffixes of known languages here.  If those languages
     were not present when we built the driver, we will hit these copies
     and be given a more meaningful error than "file not used since
     linking is not done".  */
  {".m",  "#Objective-C", 0, 0, 0}, {".mi",  "#Objective-C", 0, 0, 0},
  {".mm", "#Objective-C++", 0, 0, 0}, {".M", "#Objective-C++", 0, 0, 0},
  {".mii", "#Objective-C++", 0, 0, 0},
  {".cc", "#C++", 0, 0, 0}, {".cxx", "#C++", 0, 0, 0},
  {".cpp", "#C++", 0, 0, 0}, {".cp", "#C++", 0, 0, 0},
  {".c++", "#C++", 0, 0, 0}, {".C", "#C++", 0, 0, 0},
  {".CPP", "#C++", 0, 0, 0}, {".ii", "#C++", 0, 0, 0},
  {".ads", "#Ada", 0, 0, 0}, {".adb", "#Ada", 0, 0, 0},
  {".f", "#Fortran", 0, 0, 0}, {".for", "#Fortran", 0, 0, 0},
  {".fpp", "#Fortran", 0, 0, 0}, {".F", "#Fortran", 0, 0, 0},
  {".FOR", "#Fortran", 0, 0, 0}, {".FPP", "#Fortran", 0, 0, 0},
  {".f90", "#Fortran", 0, 0, 0}, {".f95", "#Fortran", 0, 0, 0},
  {".F90", "#Fortran", 0, 0, 0}, {".F95", "#Fortran", 0, 0, 0},
  {".r", "#Ratfor", 0, 0, 0},
  {".p", "#Pascal", 0, 0, 0}, {".pas", "#Pascal", 0, 0, 0},
  {".java", "#Java", 0, 0, 0}, {".class", "#Java", 0, 0, 0},
  {".zip", "#Java", 0, 0, 0}, {".jar", "#Java", 0, 0, 0},
  /* Next come the entries for C.  */
  {".c", "@c", 0, 1, 1},
  {"@c",
   /* cc1 has an integrated ISO C preprocessor.  We should invoke the
      external preprocessor if -save-temps is given.  */
     "%{E|M|MM:%(trad_capable_cpp) %(cpp_options) %(cpp_debug_options)}\
      %{!E:%{!M:%{!MM:\
          %{traditional|ftraditional:\
%eGNU C no longer supports -traditional without -E}\
       %{!combine:\
	  %{save-temps|traditional-cpp|no-integrated-cpp:%(trad_capable_cpp) \
		%(cpp_options) -o %{save-temps:%b.i} %{!save-temps:%g.i} \n\
		    cc1 -fpreprocessed %{save-temps:%b.i} %{!save-temps:%g.i} \
			%(cc1_options)}\
	  %{!save-temps:%{!traditional-cpp:%{!no-integrated-cpp:\
		cc1 %(cpp_unique_options) %(cc1_options)}}}\
          %{!fsyntax-only:%(invoke_as)}} \
      %{combine:\
	  %{save-temps|traditional-cpp|no-integrated-cpp:%(trad_capable_cpp) \
		%(cpp_options) -o %{save-temps:%b.i} %{!save-temps:%g.i}}\
	  %{!save-temps:%{!traditional-cpp:%{!no-integrated-cpp:\
		cc1 %(cpp_unique_options) %(cc1_options)}}\
                %{!fsyntax-only:%(invoke_as)}}}}}}", 0, 1, 1},
  {"-",
   "%{!E:%e-E or -x required when input is from standard input}\
    %(trad_capable_cpp) %(cpp_options) %(cpp_debug_options)", 0, 0, 0},
  {".h", "@c-header", 0, 0, 0},
  {"@c-header",
   /* cc1 has an integrated ISO C preprocessor.  We should invoke the
      external preprocessor if -save-temps is given.  */
     "%{E|M|MM:%(trad_capable_cpp) %(cpp_options) %(cpp_debug_options)}\
      %{!E:%{!M:%{!MM:\
	  %{save-temps|traditional-cpp|no-integrated-cpp:%(trad_capable_cpp) \
		%(cpp_options) -o %{save-temps:%b.i} %{!save-temps:%g.i} \n\
		    cc1 -fpreprocessed %{save-temps:%b.i} %{!save-temps:%g.i} \
			%(cc1_options)\
                        -o %g.s %{!o*:--output-pch=%i.gch}\
                        %W{o*:--output-pch=%*}%V}\
	  %{!save-temps:%{!traditional-cpp:%{!no-integrated-cpp:\
		cc1 %(cpp_unique_options) %(cc1_options)\
                    -o %g.s %{!o*:--output-pch=%i.gch}\
                    %W{o*:--output-pch=%*}%V}}}}}}", 0, 0, 0},
  {".i", "@cpp-output", 0, 1, 0},
  {"@cpp-output",
   "%{!M:%{!MM:%{!E:cc1 -fpreprocessed %i %(cc1_options) %{!fsyntax-only:%(invoke_as)}}}}", 0, 1, 0},
  {".s", "@assembler", 0, 1, 0},
  {"@assembler",
   "%{!M:%{!MM:%{!E:%{!S:as %(asm_debug) %(asm_options) %i %A }}}}", 0, 1, 0},
  {".S", "@assembler-with-cpp", 0, 1, 0},
  {"@assembler-with-cpp",
#ifdef AS_NEEDS_DASH_FOR_PIPED_INPUT
   "%(trad_capable_cpp) -lang-asm %(cpp_options)\
      %{E|M|MM:%(cpp_debug_options)}\
      %{!M:%{!MM:%{!E:%{!S:-o %|.s |\n\
       as %(asm_debug) %(asm_options) %|.s %A }}}}"
#else
   "%(trad_capable_cpp) -lang-asm %(cpp_options)\
      %{E|M|MM:%(cpp_debug_options)}\
      %{!M:%{!MM:%{!E:%{!S:-o %|.s |\n\
       as %(asm_debug) %(asm_options) %m.s %A }}}}"
#endif
   , 0, 1, 0},

#include "specs.h"
  /* Mark end of table.  */
  {0, 0, 0, 0, 0}
};

/* Number of elements in default_compilers, not counting the terminator.  */

static const int n_default_compilers = ARRAY_SIZE (default_compilers) - 1;

/* A vector of options to give to the linker.
   These options are accumulated by %x,
   and substituted into the linker command with %X.  */
static int n_linker_options;
static char **linker_options;

/* A vector of options to give to the assembler.
   These options are accumulated by -Wa,
   and substituted into the assembler command with %Y.  */
static int n_assembler_options;
static char **assembler_options;

/* A vector of options to give to the preprocessor.
   These options are accumulated by -Wp,
   and substituted into the preprocessor command with %Z.  */
static int n_preprocessor_options;
static char **preprocessor_options;

/* Define how to map long options into short ones.  */

/* This structure describes one mapping.  */
struct option_map
{
  /* The long option's name.  */
  const char *const name;
  /* The equivalent short option.  */
  const char *const equivalent;
  /* Argument info.  A string of flag chars; NULL equals no options.
     a => argument required.
     o => argument optional.
     j => join argument to equivalent, making one word.
     * => require other text after NAME as an argument.  */
  const char *const arg_info;
};

/* This is the table of mappings.  Mappings are tried sequentially
   for each option encountered; the first one that matches, wins.  */

static const struct option_map option_map[] =
 {
   {"--all-warnings", "-Wall", 0},
   {"--ansi", "-ansi", 0},
   {"--assemble", "-S", 0},
   {"--assert", "-A", "a"},
   {"--classpath", "-fclasspath=", "aj"},
   {"--bootclasspath", "-fbootclasspath=", "aj"},
   {"--CLASSPATH", "-fclasspath=", "aj"},
   {"--combine", "-combine", 0},
   {"--comments", "-C", 0},
   {"--comments-in-macros", "-CC", 0},
   {"--compile", "-c", 0},
   {"--debug", "-g", "oj"},
   {"--define-macro", "-D", "aj"},
   {"--dependencies", "-M", 0},
   {"--dump", "-d", "a"},
   {"--dumpbase", "-dumpbase", "a"},
   {"--encoding", "-fencoding=", "aj"},
   {"--entry", "-e", 0},
   {"--extra-warnings", "-W", 0},
   {"--extdirs", "-fextdirs=", "aj"},
   {"--for-assembler", "-Wa", "a"},
   {"--for-linker", "-Xlinker", "a"},
   {"--force-link", "-u", "a"},
   {"--coverage", "-coverage", 0},
   {"--imacros", "-imacros", "a"},
   {"--include", "-include", "a"},
   {"--include-barrier", "-I-", 0},
   {"--include-directory", "-I", "aj"},
   {"--include-directory-after", "-idirafter", "a"},
   {"--include-prefix", "-iprefix", "a"},
   {"--include-with-prefix", "-iwithprefix", "a"},
   {"--include-with-prefix-before", "-iwithprefixbefore", "a"},
   {"--include-with-prefix-after", "-iwithprefix", "a"},
   {"--language", "-x", "a"},
   {"--library-directory", "-L", "a"},
   {"--machine", "-m", "aj"},
   {"--machine-", "-m", "*j"},
   {"--no-integrated-cpp", "-no-integrated-cpp", 0},
   {"--no-line-commands", "-P", 0},
   {"--no-precompiled-includes", "-noprecomp", 0},
   {"--no-standard-includes", "-nostdinc", 0},
   {"--no-standard-libraries", "-nostdlib", 0},
   {"--no-warnings", "-w", 0},
   {"--optimize", "-O", "oj"},
   {"--output", "-o", "a"},
   {"--output-class-directory", "-foutput-class-dir=", "ja"},
   {"--param", "--param", "a"},
   {"--pass-exit-codes", "-pass-exit-codes", 0},
   {"--pedantic", "-pedantic", 0},
   {"--pedantic-errors", "-pedantic-errors", 0},
   {"--pie", "-pie", 0},
   {"--pipe", "-pipe", 0},
   {"--prefix", "-B", "a"},
   {"--preprocess", "-E", 0},
   {"--print-search-dirs", "-print-search-dirs", 0},
   {"--print-file-name", "-print-file-name=", "aj"},
   {"--print-libgcc-file-name", "-print-libgcc-file-name", 0},
   {"--print-missing-file-dependencies", "-MG", 0},
   {"--print-multi-lib", "-print-multi-lib", 0},
   {"--print-multi-directory", "-print-multi-directory", 0},
   {"--print-multi-os-directory", "-print-multi-os-directory", 0},
   {"--print-prog-name", "-print-prog-name=", "aj"},
   {"--profile", "-p", 0},
   {"--profile-blocks", "-a", 0},
   {"--quiet", "-q", 0},
   {"--resource", "-fcompile-resource=", "aj"},
   {"--save-temps", "-save-temps", 0},
   {"--shared", "-shared", 0},
   {"--silent", "-q", 0},
   {"--specs", "-specs=", "aj"},
   {"--static", "-static", 0},
   {"--std", "-std=", "aj"},
   {"--symbolic", "-symbolic", 0},
   {"--sysroot", "--sysroot=", "aj"},
   {"--time", "-time", 0},
   {"--trace-includes", "-H", 0},
   {"--traditional", "-traditional", 0},
   {"--traditional-cpp", "-traditional-cpp", 0},
   {"--trigraphs", "-trigraphs", 0},
   {"--undefine-macro", "-U", "aj"},
   {"--user-dependencies", "-MM", 0},
   {"--verbose", "-v", 0},
   {"--warn-", "-W", "*j"},
   {"--write-dependencies", "-MD", 0},
   {"--write-user-dependencies", "-MMD", 0},
   {"--", "-f", "*j"}
 };


#ifdef TARGET_OPTION_TRANSLATE_TABLE
static const struct {
  const char *const option_found;
  const char *const replacements;
} target_option_translations[] =
{
  TARGET_OPTION_TRANSLATE_TABLE,
  { 0, 0 }
};
#endif

/* Translate the options described by *ARGCP and *ARGVP.
   Make a new vector and store it back in *ARGVP,
   and store its length in *ARGVC.  */

static void
translate_options (int *argcp, const char *const **argvp)
{
  int i;
  int argc = *argcp;
  const char *const *argv = *argvp;
  int newvsize = (argc + 2) * 2 * sizeof (const char *);
  const char **newv = xmalloc (newvsize);
  int newindex = 0;

  i = 0;
  newv[newindex++] = argv[i++];

  while (i < argc)
    {
#ifdef TARGET_OPTION_TRANSLATE_TABLE
      int tott_idx;

      for (tott_idx = 0;
	   target_option_translations[tott_idx].option_found;
	   tott_idx++)
	{
	  if (strcmp (target_option_translations[tott_idx].option_found,
		      argv[i]) == 0)
	    {
	      int spaces = 1;
	      const char *sp;
	      char *np;

	      for (sp = target_option_translations[tott_idx].replacements;
		   *sp; sp++)
		{
		  if (*sp == ' ')
		    spaces ++;
		}

	      newvsize += spaces * sizeof (const char *);
	      newv =  xrealloc (newv, newvsize);

	      sp = target_option_translations[tott_idx].replacements;
	      np = xstrdup (sp);

	      while (1)
		{
		  while (*np == ' ')
		    np++;
		  if (*np == 0)
		    break;
		  newv[newindex++] = np;
		  while (*np != ' ' && *np)
		    np++;
		  if (*np == 0)
		    break;
		  *np++ = 0;
		}

	      i ++;
	      break;
	    }
	}
      if (target_option_translations[tott_idx].option_found)
	continue;
#endif

      /* Translate -- options.  */
      if (argv[i][0] == '-' && argv[i][1] == '-')
	{
	  size_t j;
	  /* Find a mapping that applies to this option.  */
	  for (j = 0; j < ARRAY_SIZE (option_map); j++)
	    {
	      size_t optlen = strlen (option_map[j].name);
	      size_t arglen = strlen (argv[i]);
	      size_t complen = arglen > optlen ? optlen : arglen;
	      const char *arginfo = option_map[j].arg_info;

	      if (arginfo == 0)
		arginfo = "";

	      if (!strncmp (argv[i], option_map[j].name, complen))
		{
		  const char *arg = 0;

		  if (arglen < optlen)
		    {
		      size_t k;
		      for (k = j + 1; k < ARRAY_SIZE (option_map); k++)
			if (strlen (option_map[k].name) >= arglen
			    && !strncmp (argv[i], option_map[k].name, arglen))
			  {
			    error ("ambiguous abbreviation %s", argv[i]);
			    break;
			  }

		      if (k != ARRAY_SIZE (option_map))
			break;
		    }

		  if (arglen > optlen)
		    {
		      /* If the option has an argument, accept that.  */
		      if (argv[i][optlen] == '=')
			arg = argv[i] + optlen + 1;

		      /* If this mapping requires extra text at end of name,
			 accept that as "argument".  */
		      else if (strchr (arginfo, '*') != 0)
			arg = argv[i] + optlen;

		      /* Otherwise, extra text at end means mismatch.
			 Try other mappings.  */
		      else
			continue;
		    }

		  else if (strchr (arginfo, '*') != 0)
		    {
		      error ("incomplete '%s' option", option_map[j].name);
		      break;
		    }

		  /* Handle arguments.  */
		  if (strchr (arginfo, 'a') != 0)
		    {
		      if (arg == 0)
			{
			  if (i + 1 == argc)
			    {
			      error ("missing argument to '%s' option",
				     option_map[j].name);
			      break;
			    }

			  arg = argv[++i];
			}
		    }
		  else if (strchr (arginfo, '*') != 0)
		    ;
		  else if (strchr (arginfo, 'o') == 0)
		    {
		      if (arg != 0)
			error ("extraneous argument to '%s' option",
			       option_map[j].name);
		      arg = 0;
		    }

		  /* Store the translation as one argv elt or as two.  */
		  if (arg != 0 && strchr (arginfo, 'j') != 0)
		    newv[newindex++] = concat (option_map[j].equivalent, arg,
					       NULL);
		  else if (arg != 0)
		    {
		      newv[newindex++] = option_map[j].equivalent;
		      newv[newindex++] = arg;
		    }
		  else
		    newv[newindex++] = option_map[j].equivalent;

		  break;
		}
	    }
	  i++;
	}

      /* Handle old-fashioned options--just copy them through,
	 with their arguments.  */
      else if (argv[i][0] == '-')
	{
	  const char *p = argv[i] + 1;
	  int c = *p;
	  int nskip = 1;

	  if (SWITCH_TAKES_ARG (c) > (p[1] != 0))
	    nskip += SWITCH_TAKES_ARG (c) - (p[1] != 0);
	  else if (WORD_SWITCH_TAKES_ARG (p))
	    nskip += WORD_SWITCH_TAKES_ARG (p);
	  else if ((c == 'B' || c == 'b' || c == 'x')
		   && p[1] == 0)
	    nskip += 1;
	  else if (! strcmp (p, "Xlinker"))
	    nskip += 1;
	  else if (! strcmp (p, "Xpreprocessor"))
	    nskip += 1;
	  else if (! strcmp (p, "Xassembler"))
	    nskip += 1;

	  /* Watch out for an option at the end of the command line that
	     is missing arguments, and avoid skipping past the end of the
	     command line.  */
	  if (nskip + i > argc)
	    nskip = argc - i;

	  while (nskip > 0)
	    {
	      newv[newindex++] = argv[i++];
	      nskip--;
	    }
	}
      else
	/* Ordinary operands, or +e options.  */
	newv[newindex++] = argv[i++];
    }

  newv[newindex] = 0;

  *argvp = newv;
  *argcp = newindex;
}

static char *
skip_whitespace (char *p)
{
  while (1)
    {
      /* A fully-blank line is a delimiter in the SPEC file and shouldn't
	 be considered whitespace.  */
      if (p[0] == '\n' && p[1] == '\n' && p[2] == '\n')
	return p + 1;
      else if (*p == '\n' || *p == ' ' || *p == '\t')
	p++;
      else if (*p == '#')
	{
	  while (*p != '\n')
	    p++;
	  p++;
	}
      else
	break;
    }

  return p;
}
/* Structures to keep track of prefixes to try when looking for files.  */

struct prefix_list
{
  const char *prefix;	      /* String to prepend to the path.  */
  struct prefix_list *next;   /* Next in linked list.  */
  int require_machine_suffix; /* Don't use without machine_suffix.  */
  /* 2 means try both machine_suffix and just_machine_suffix.  */
  int priority;		      /* Sort key - priority within list.  */
  int os_multilib;	      /* 1 if OS multilib scheme should be used,
				 0 for GCC multilib scheme.  */
};

struct path_prefix
{
  struct prefix_list *plist;  /* List of prefixes to try */
  int max_len;                /* Max length of a prefix in PLIST */
  const char *name;           /* Name of this list (used in config stuff) */
};

/* List of prefixes to try when looking for executables.  */

static struct path_prefix exec_prefixes = { 0, 0, "exec" };

/* List of prefixes to try when looking for startup (crt0) files.  */

static struct path_prefix startfile_prefixes = { 0, 0, "startfile" };

/* List of prefixes to try when looking for include files.  */

static struct path_prefix include_prefixes = { 0, 0, "include" };

/* Suffix to attach to directories searched for commands.
   This looks like `MACHINE/VERSION/'.  */

static const char *machine_suffix = 0;

/* Suffix to attach to directories searched for commands.
   This is just `MACHINE/'.  */

static const char *just_machine_suffix = 0;

/* Adjusted value of GCC_EXEC_PREFIX envvar.  */

static const char *gcc_exec_prefix;

/* Adjusted value of standard_libexec_prefix.  */

static const char *gcc_libexec_prefix;

/* Default prefixes to attach to command names.  */

#ifndef STANDARD_STARTFILE_PREFIX_1
#define STANDARD_STARTFILE_PREFIX_1 "/lib/"
#endif
#ifndef STANDARD_STARTFILE_PREFIX_2
#define STANDARD_STARTFILE_PREFIX_2 "/usr/lib/"
#endif

#ifdef CROSS_COMPILE  /* Don't use these prefixes for a cross compiler.  */
#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX
#undef MD_STARTFILE_PREFIX_1
#endif

/* If no prefixes defined, use the null string, which will disable them.  */
#ifndef MD_EXEC_PREFIX
#define MD_EXEC_PREFIX ""
#endif
#ifndef MD_STARTFILE_PREFIX
#define MD_STARTFILE_PREFIX ""
#endif
#ifndef MD_STARTFILE_PREFIX_1
#define MD_STARTFILE_PREFIX_1 ""
#endif

static const char *const standard_exec_prefix = STANDARD_EXEC_PREFIX;
static const char *const standard_exec_prefix_1 = "/usr/libexec/gcc/";
static const char *const standard_exec_prefix_2 = "/usr/lib/gcc/";
static const char *md_exec_prefix = MD_EXEC_PREFIX;

static const char *md_startfile_prefix = MD_STARTFILE_PREFIX;
static const char *md_startfile_prefix_1 = MD_STARTFILE_PREFIX_1;
static const char *const standard_startfile_prefix = STANDARD_STARTFILE_PREFIX;
static const char *const standard_startfile_prefix_1
  = STANDARD_STARTFILE_PREFIX_1;
static const char *const standard_startfile_prefix_2
  = STANDARD_STARTFILE_PREFIX_2;

static const char *const tooldir_base_prefix = TOOLDIR_BASE_PREFIX;
static const char *tooldir_prefix;

static const char *const standard_bindir_prefix = STANDARD_BINDIR_PREFIX;

static const char *standard_libexec_prefix = STANDARD_LIBEXEC_PREFIX;

/* Subdirectory to use for locating libraries.  Set by
   set_multilib_dir based on the compilation options.  */

static const char *multilib_dir;

/* Subdirectory to use for locating libraries in OS conventions.  Set by
   set_multilib_dir based on the compilation options.  */

static const char *multilib_os_dir;

/* Structure to keep track of the specs that have been defined so far.
   These are accessed using %(specname) or %[specname] in a compiler
   or link spec.  */

struct spec_list
{
				/* The following 2 fields must be first */
				/* to allow EXTRA_SPECS to be initialized */
  const char *name;		/* name of the spec.  */
  const char *ptr;		/* available ptr if no static pointer */

				/* The following fields are not initialized */
				/* by EXTRA_SPECS */
  const char **ptr_spec;	/* pointer to the spec itself.  */
  struct spec_list *next;	/* Next spec in linked list.  */
  int name_len;			/* length of the name */
  int alloc_p;			/* whether string was allocated */
};

#define INIT_STATIC_SPEC(NAME,PTR) \
{ NAME, NULL, PTR, (struct spec_list *) 0, sizeof (NAME) - 1, 0 }

/* List of statically defined specs.  */
static struct spec_list static_specs[] =
{
  INIT_STATIC_SPEC ("asm",			&asm_spec),
  INIT_STATIC_SPEC ("asm_debug",		&asm_debug),
  INIT_STATIC_SPEC ("asm_final",		&asm_final_spec),
  INIT_STATIC_SPEC ("asm_options",		&asm_options),
  INIT_STATIC_SPEC ("invoke_as",		&invoke_as),
  INIT_STATIC_SPEC ("cpp",			&cpp_spec),
  INIT_STATIC_SPEC ("cpp_options",		&cpp_options),
  INIT_STATIC_SPEC ("cpp_debug_options",	&cpp_debug_options),
  INIT_STATIC_SPEC ("cpp_unique_options",	&cpp_unique_options),
  INIT_STATIC_SPEC ("trad_capable_cpp",		&trad_capable_cpp),
  INIT_STATIC_SPEC ("cc1",			&cc1_spec),
  INIT_STATIC_SPEC ("cc1_options",		&cc1_options),
  INIT_STATIC_SPEC ("cc1plus",			&cc1plus_spec),
  INIT_STATIC_SPEC ("link_gcc_c_sequence",	&link_gcc_c_sequence_spec),
  INIT_STATIC_SPEC ("link_ssp",			&link_ssp_spec),
  INIT_STATIC_SPEC ("endfile",			&endfile_spec),
  INIT_STATIC_SPEC ("link",			&link_spec),
  INIT_STATIC_SPEC ("lib",			&lib_spec),
  INIT_STATIC_SPEC ("mfwrap",			&mfwrap_spec),
  INIT_STATIC_SPEC ("mflib",			&mflib_spec),
  INIT_STATIC_SPEC ("link_gomp",		&link_gomp_spec),
  INIT_STATIC_SPEC ("libgcc",			&libgcc_spec),
  INIT_STATIC_SPEC ("startfile",		&startfile_spec),
  INIT_STATIC_SPEC ("switches_need_spaces",	&switches_need_spaces),
  INIT_STATIC_SPEC ("cross_compile",		&cross_compile),
  INIT_STATIC_SPEC ("version",			&compiler_version),
  INIT_STATIC_SPEC ("multilib",			&multilib_select),
  INIT_STATIC_SPEC ("multilib_defaults",	&multilib_defaults),
  INIT_STATIC_SPEC ("multilib_extra",		&multilib_extra),
  INIT_STATIC_SPEC ("multilib_matches",		&multilib_matches),
  INIT_STATIC_SPEC ("multilib_exclusions",	&multilib_exclusions),
  INIT_STATIC_SPEC ("multilib_options",		&multilib_options),
  INIT_STATIC_SPEC ("linker",			&linker_name_spec),
  INIT_STATIC_SPEC ("link_libgcc",		&link_libgcc_spec),
  INIT_STATIC_SPEC ("md_exec_prefix",		&md_exec_prefix),
  INIT_STATIC_SPEC ("md_startfile_prefix",	&md_startfile_prefix),
  INIT_STATIC_SPEC ("md_startfile_prefix_1",	&md_startfile_prefix_1),
  INIT_STATIC_SPEC ("startfile_prefix_spec",	&startfile_prefix_spec),
  INIT_STATIC_SPEC ("sysroot_spec",             &sysroot_spec),
  INIT_STATIC_SPEC ("sysroot_suffix_spec",	&sysroot_suffix_spec),
  INIT_STATIC_SPEC ("sysroot_hdrs_suffix_spec",	&sysroot_hdrs_suffix_spec),
};

#ifdef EXTRA_SPECS		/* additional specs needed */
/* Structure to keep track of just the first two args of a spec_list.
   That is all that the EXTRA_SPECS macro gives us.  */
struct spec_list_1
{
  const char *const name;
  const char *const ptr;
};

static const struct spec_list_1 extra_specs_1[] = { EXTRA_SPECS };
static struct spec_list *extra_specs = (struct spec_list *) 0;
#endif

/* List of dynamically allocates specs that have been defined so far.  */

static struct spec_list *specs = (struct spec_list *) 0;

/* List of static spec functions.  */

static const struct spec_function static_spec_functions[] =
{
  { "if-exists",		if_exists_spec_function },
  { "if-exists-else",		if_exists_else_spec_function },
  { "replace-outfile",		replace_outfile_spec_function },
  { "version-compare",		version_compare_spec_function },
  { "include",			include_spec_function },
#ifdef EXTRA_SPEC_FUNCTIONS
  EXTRA_SPEC_FUNCTIONS
#endif
  { 0, 0 }
};

static int processing_spec_function;

/* Add appropriate libgcc specs to OBSTACK, taking into account
   various permutations of -shared-libgcc, -shared, and such.  */

#if defined(ENABLE_SHARED_LIBGCC) && !defined(REAL_LIBGCC_SPEC)

#ifndef USE_LD_AS_NEEDED
#define USE_LD_AS_NEEDED 0
#endif

static void
init_gcc_specs (struct obstack *obstack, const char *shared_name,
		const char *static_name, const char *eh_name)
{
  char *buf;

  buf = concat ("%{static|static-libgcc:", static_name, " ", eh_name, "}"
		"%{!static:%{!static-libgcc:"
#if USE_LD_AS_NEEDED
		"%{!shared-libgcc:",
		static_name, " --as-needed ", shared_name, " --no-as-needed"
		"}"
		"%{shared-libgcc:",
		shared_name, "%{!shared: ", static_name, "}"
		"}"
#else
		"%{!shared:"
		"%{!shared-libgcc:", static_name, " ", eh_name, "}"
		"%{shared-libgcc:", shared_name, " ", static_name, "}"
		"}"
#ifdef LINK_EH_SPEC
		"%{shared:"
		"%{shared-libgcc:", shared_name, "}"
		"%{!shared-libgcc:", static_name, "}"
		"}"
#else
		"%{shared:", shared_name, "}"
#endif
#endif
		"}}", NULL);

  obstack_grow (obstack, buf, strlen (buf));
  free (buf);
}
#endif /* ENABLE_SHARED_LIBGCC */

/* Initialize the specs lookup routines.  */

static void
init_spec (void)
{
  struct spec_list *next = (struct spec_list *) 0;
  struct spec_list *sl   = (struct spec_list *) 0;
  int i;

  if (specs)
    return;			/* Already initialized.  */

  if (verbose_flag)
    notice ("Using built-in specs.\n");

#ifdef EXTRA_SPECS
  extra_specs = xcalloc (sizeof (struct spec_list),
			 ARRAY_SIZE (extra_specs_1));

  for (i = ARRAY_SIZE (extra_specs_1) - 1; i >= 0; i--)
    {
      sl = &extra_specs[i];
      sl->name = extra_specs_1[i].name;
      sl->ptr = extra_specs_1[i].ptr;
      sl->next = next;
      sl->name_len = strlen (sl->name);
      sl->ptr_spec = &sl->ptr;
      next = sl;
    }
#endif

  /* Initialize here, not in definition.  The IRIX 6 O32 cc sometimes chokes
     on ?: in file-scope variable initializations.  */
  asm_debug = ASM_DEBUG_SPEC;

  for (i = ARRAY_SIZE (static_specs) - 1; i >= 0; i--)
    {
      sl = &static_specs[i];
      sl->next = next;
      next = sl;
    }

#if defined(ENABLE_SHARED_LIBGCC) && !defined(REAL_LIBGCC_SPEC)
  /* ??? If neither -shared-libgcc nor --static-libgcc was
     seen, then we should be making an educated guess.  Some proposed
     heuristics for ELF include:

	(1) If "-Wl,--export-dynamic", then it's a fair bet that the
	    program will be doing dynamic loading, which will likely
	    need the shared libgcc.

	(2) If "-ldl", then it's also a fair bet that we're doing
	    dynamic loading.

	(3) For each ET_DYN we're linking against (either through -lfoo
	    or /some/path/foo.so), check to see whether it or one of
	    its dependencies depends on a shared libgcc.

	(4) If "-shared"

	    If the runtime is fixed to look for program headers instead
	    of calling __register_frame_info at all, for each object,
	    use the shared libgcc if any EH symbol referenced.

	    If crtstuff is fixed to not invoke __register_frame_info
	    automatically, for each object, use the shared libgcc if
	    any non-empty unwind section found.

     Doing any of this probably requires invoking an external program to
     do the actual object file scanning.  */
  {
    const char *p = libgcc_spec;
    int in_sep = 1;

    /* Transform the extant libgcc_spec into one that uses the shared libgcc
       when given the proper command line arguments.  */
    while (*p)
      {
	if (in_sep && *p == '-' && strncmp (p, "-lgcc", 5) == 0)
	  {
	    init_gcc_specs (&obstack,
			    "-lgcc_s"
#ifdef USE_LIBUNWIND_EXCEPTIONS
			    " -lunwind"
#endif
			    ,
			    "-lgcc",
			    "-lgcc_eh"
#ifdef USE_LIBUNWIND_EXCEPTIONS
# ifdef HAVE_LD_STATIC_DYNAMIC
			    " %{!static:-Bstatic} -lunwind %{!static:-Bdynamic}"
# else
			    " -lunwind"
# endif
#endif
			    );

	    p += 5;
	    in_sep = 0;
	  }
	else if (in_sep && *p == 'l' && strncmp (p, "libgcc.a%s", 10) == 0)
	  {
	    /* Ug.  We don't know shared library extensions.  Hope that
	       systems that use this form don't do shared libraries.  */
	    init_gcc_specs (&obstack,
			    "-lgcc_s",
			    "libgcc.a%s",
			    "libgcc_eh.a%s"
#ifdef USE_LIBUNWIND_EXCEPTIONS
			    " -lunwind"
#endif
			    );
	    p += 10;
	    in_sep = 0;
	  }
	else
	  {
	    obstack_1grow (&obstack, *p);
	    in_sep = (*p == ' ');
	    p += 1;
	  }
      }

    obstack_1grow (&obstack, '\0');
    libgcc_spec = XOBFINISH (&obstack, const char *);
  }
#endif
#ifdef USE_AS_TRADITIONAL_FORMAT
  /* Prepend "--traditional-format" to whatever asm_spec we had before.  */
  {
    static const char tf[] = "--traditional-format ";
    obstack_grow (&obstack, tf, sizeof(tf) - 1);
    obstack_grow0 (&obstack, asm_spec, strlen (asm_spec));
    asm_spec = XOBFINISH (&obstack, const char *);
  }
#endif
#ifdef LINK_EH_SPEC
  /* Prepend LINK_EH_SPEC to whatever link_spec we had before.  */
  obstack_grow (&obstack, LINK_EH_SPEC, sizeof(LINK_EH_SPEC) - 1);
  obstack_grow0 (&obstack, link_spec, strlen (link_spec));
  link_spec = XOBFINISH (&obstack, const char *);
#endif

  specs = sl;
}

/* Change the value of spec NAME to SPEC.  If SPEC is empty, then the spec is
   removed; If the spec starts with a + then SPEC is added to the end of the
   current spec.  */

static void
set_spec (const char *name, const char *spec)
{
  struct spec_list *sl;
  const char *old_spec;
  int name_len = strlen (name);
  int i;

  /* If this is the first call, initialize the statically allocated specs.  */
  if (!specs)
    {
      struct spec_list *next = (struct spec_list *) 0;
      for (i = ARRAY_SIZE (static_specs) - 1; i >= 0; i--)
	{
	  sl = &static_specs[i];
	  sl->next = next;
	  next = sl;
	}
      specs = sl;
    }

  /* See if the spec already exists.  */
  for (sl = specs; sl; sl = sl->next)
    if (name_len == sl->name_len && !strcmp (sl->name, name))
      break;

  if (!sl)
    {
      /* Not found - make it.  */
      sl = XNEW (struct spec_list);
      sl->name = xstrdup (name);
      sl->name_len = name_len;
      sl->ptr_spec = &sl->ptr;
      sl->alloc_p = 0;
      *(sl->ptr_spec) = "";
      sl->next = specs;
      specs = sl;
    }

  old_spec = *(sl->ptr_spec);
  *(sl->ptr_spec) = ((spec[0] == '+' && ISSPACE ((unsigned char)spec[1]))
		     ? concat (old_spec, spec + 1, NULL)
		     : xstrdup (spec));

#ifdef DEBUG_SPECS
  if (verbose_flag)
    notice ("Setting spec %s to '%s'\n\n", name, *(sl->ptr_spec));
#endif

  /* Free the old spec.  */
  if (old_spec && sl->alloc_p)
    free ((void *) old_spec);

  sl->alloc_p = 1;
}

/* Accumulate a command (program name and args), and run it.  */

/* Vector of pointers to arguments in the current line of specifications.  */

static const char **argbuf;

/* Number of elements allocated in argbuf.  */

static int argbuf_length;

/* Number of elements in argbuf currently in use (containing args).  */

static int argbuf_index;

/* Position in the argbuf array containing the name of the output file
   (the value associated with the "-o" flag).  */

static int have_o_argbuf_index = 0;

/* Were the options -c or -S passed.  */
static int have_c = 0;

/* Was the option -o passed.  */
static int have_o = 0;

/* This is the list of suffixes and codes (%g/%u/%U/%j) and the associated
   temp file.  If the HOST_BIT_BUCKET is used for %j, no entry is made for
   it here.  */

static struct temp_name {
  const char *suffix;	/* suffix associated with the code.  */
  int length;		/* strlen (suffix).  */
  int unique;		/* Indicates whether %g or %u/%U was used.  */
  const char *filename;	/* associated filename.  */
  int filename_length;	/* strlen (filename).  */
  struct temp_name *next;
} *temp_names;

/* Number of commands executed so far.  */

static int execution_count;

/* Number of commands that exited with a signal.  */

static int signal_count;

/* Name with which this program was invoked.  */

static const char *programname;

/* Allocate the argument vector.  */

static void
alloc_args (void)
{
  argbuf_length = 10;
  argbuf = XNEWVEC (const char *, argbuf_length);
}

/* Clear out the vector of arguments (after a command is executed).  */

static void
clear_args (void)
{
  argbuf_index = 0;
}

/* Add one argument to the vector at the end.
   This is done when a space is seen or at the end of the line.
   If DELETE_ALWAYS is nonzero, the arg is a filename
    and the file should be deleted eventually.
   If DELETE_FAILURE is nonzero, the arg is a filename
    and the file should be deleted if this compilation fails.  */

static void
store_arg (const char *arg, int delete_always, int delete_failure)
{
  if (argbuf_index + 1 == argbuf_length)
    argbuf = xrealloc (argbuf, (argbuf_length *= 2) * sizeof (const char *));

  argbuf[argbuf_index++] = arg;
  argbuf[argbuf_index] = 0;

  if (strcmp (arg, "-o") == 0)
    have_o_argbuf_index = argbuf_index;
  if (delete_always || delete_failure)
    record_temp_file (arg, delete_always, delete_failure);
}

/* Load specs from a file name named FILENAME, replacing occurrences of
   various different types of line-endings, \r\n, \n\r and just \r, with
   a single \n.  */

static char *
load_specs (const char *filename)
{
  int desc;
  int readlen;
  struct stat statbuf;
  char *buffer;
  char *buffer_p;
  char *specs;
  char *specs_p;

  if (verbose_flag)
    notice ("Reading specs from %s\n", filename);

  /* Open and stat the file.  */
  desc = open (filename, O_RDONLY, 0);
  if (desc < 0)
    pfatal_with_name (filename);
  if (stat (filename, &statbuf) < 0)
    pfatal_with_name (filename);

  /* Read contents of file into BUFFER.  */
  buffer = XNEWVEC (char, statbuf.st_size + 1);
  readlen = read (desc, buffer, (unsigned) statbuf.st_size);
  if (readlen < 0)
    pfatal_with_name (filename);
  buffer[readlen] = 0;
  close (desc);

  specs = XNEWVEC (char, readlen + 1);
  specs_p = specs;
  for (buffer_p = buffer; buffer_p && *buffer_p; buffer_p++)
    {
      int skip = 0;
      char c = *buffer_p;
      if (c == '\r')
	{
	  if (buffer_p > buffer && *(buffer_p - 1) == '\n')	/* \n\r */
	    skip = 1;
	  else if (*(buffer_p + 1) == '\n')			/* \r\n */
	    skip = 1;
	  else							/* \r */
	    c = '\n';
	}
      if (! skip)
	*specs_p++ = c;
    }
  *specs_p = '\0';

  free (buffer);
  return (specs);
}

/* Read compilation specs from a file named FILENAME,
   replacing the default ones.

   A suffix which starts with `*' is a definition for
   one of the machine-specific sub-specs.  The "suffix" should be
   *asm, *cc1, *cpp, *link, *startfile, etc.
   The corresponding spec is stored in asm_spec, etc.,
   rather than in the `compilers' vector.

   Anything invalid in the file is a fatal error.  */

static void
read_specs (const char *filename, int main_p)
{
  char *buffer;
  char *p;

  buffer = load_specs (filename);

  /* Scan BUFFER for specs, putting them in the vector.  */
  p = buffer;
  while (1)
    {
      char *suffix;
      char *spec;
      char *in, *out, *p1, *p2, *p3;

      /* Advance P in BUFFER to the next nonblank nocomment line.  */
      p = skip_whitespace (p);
      if (*p == 0)
	break;

      /* Is this a special command that starts with '%'? */
      /* Don't allow this for the main specs file, since it would
	 encourage people to overwrite it.  */
      if (*p == '%' && !main_p)
	{
	  p1 = p;
	  while (*p && *p != '\n')
	    p++;

	  /* Skip '\n'.  */
	  p++;

	  if (!strncmp (p1, "%include", sizeof ("%include") - 1)
	      && (p1[sizeof "%include" - 1] == ' '
		  || p1[sizeof "%include" - 1] == '\t'))
	    {
	      char *new_filename;

	      p1 += sizeof ("%include");
	      while (*p1 == ' ' || *p1 == '\t')
		p1++;

	      if (*p1++ != '<' || p[-2] != '>')
		fatal ("specs %%include syntax malformed after %ld characters",
		       (long) (p1 - buffer + 1));

	      p[-2] = '\0';
	      new_filename = find_a_file (&startfile_prefixes, p1, R_OK, true);
	      read_specs (new_filename ? new_filename : p1, FALSE);
	      continue;
	    }
	  else if (!strncmp (p1, "%include_noerr", sizeof "%include_noerr" - 1)
		   && (p1[sizeof "%include_noerr" - 1] == ' '
		       || p1[sizeof "%include_noerr" - 1] == '\t'))
	    {
	      char *new_filename;

	      p1 += sizeof "%include_noerr";
	      while (*p1 == ' ' || *p1 == '\t')
		p1++;

	      if (*p1++ != '<' || p[-2] != '>')
		fatal ("specs %%include syntax malformed after %ld characters",
		       (long) (p1 - buffer + 1));

	      p[-2] = '\0';
	      new_filename = find_a_file (&startfile_prefixes, p1, R_OK, true);
	      if (new_filename)
		read_specs (new_filename, FALSE);
	      else if (verbose_flag)
		notice ("could not find specs file %s\n", p1);
	      continue;
	    }
	  else if (!strncmp (p1, "%rename", sizeof "%rename" - 1)
		   && (p1[sizeof "%rename" - 1] == ' '
		       || p1[sizeof "%rename" - 1] == '\t'))
	    {
	      int name_len;
	      struct spec_list *sl;
	      struct spec_list *newsl;

	      /* Get original name.  */
	      p1 += sizeof "%rename";
	      while (*p1 == ' ' || *p1 == '\t')
		p1++;

	      if (! ISALPHA ((unsigned char) *p1))
		fatal ("specs %%rename syntax malformed after %ld characters",
		       (long) (p1 - buffer));

	      p2 = p1;
	      while (*p2 && !ISSPACE ((unsigned char) *p2))
		p2++;

	      if (*p2 != ' ' && *p2 != '\t')
		fatal ("specs %%rename syntax malformed after %ld characters",
		       (long) (p2 - buffer));

	      name_len = p2 - p1;
	      *p2++ = '\0';
	      while (*p2 == ' ' || *p2 == '\t')
		p2++;

	      if (! ISALPHA ((unsigned char) *p2))
		fatal ("specs %%rename syntax malformed after %ld characters",
		       (long) (p2 - buffer));

	      /* Get new spec name.  */
	      p3 = p2;
	      while (*p3 && !ISSPACE ((unsigned char) *p3))
		p3++;

	      if (p3 != p - 1)
		fatal ("specs %%rename syntax malformed after %ld characters",
		       (long) (p3 - buffer));
	      *p3 = '\0';

	      for (sl = specs; sl; sl = sl->next)
		if (name_len == sl->name_len && !strcmp (sl->name, p1))
		  break;

	      if (!sl)
		fatal ("specs %s spec was not found to be renamed", p1);

	      if (strcmp (p1, p2) == 0)
		continue;

	      for (newsl = specs; newsl; newsl = newsl->next)
		if (strcmp (newsl->name, p2) == 0)
		  fatal ("%s: attempt to rename spec '%s' to already defined spec '%s'",
		    filename, p1, p2);

	      if (verbose_flag)
		{
		  notice ("rename spec %s to %s\n", p1, p2);
#ifdef DEBUG_SPECS
		  notice ("spec is '%s'\n\n", *(sl->ptr_spec));
#endif
		}

	      set_spec (p2, *(sl->ptr_spec));
	      if (sl->alloc_p)
		free ((void *) *(sl->ptr_spec));

	      *(sl->ptr_spec) = "";
	      sl->alloc_p = 0;
	      continue;
	    }
	  else
	    fatal ("specs unknown %% command after %ld characters",
		   (long) (p1 - buffer));
	}

      /* Find the colon that should end the suffix.  */
      p1 = p;
      while (*p1 && *p1 != ':' && *p1 != '\n')
	p1++;

      /* The colon shouldn't be missing.  */
      if (*p1 != ':')
	fatal ("specs file malformed after %ld characters",
	       (long) (p1 - buffer));

      /* Skip back over trailing whitespace.  */
      p2 = p1;
      while (p2 > buffer && (p2[-1] == ' ' || p2[-1] == '\t'))
	p2--;

      /* Copy the suffix to a string.  */
      suffix = save_string (p, p2 - p);
      /* Find the next line.  */
      p = skip_whitespace (p1 + 1);
      if (p[1] == 0)
	fatal ("specs file malformed after %ld characters",
	       (long) (p - buffer));

      p1 = p;
      /* Find next blank line or end of string.  */
      while (*p1 && !(*p1 == '\n' && (p1[1] == '\n' || p1[1] == '\0')))
	p1++;

      /* Specs end at the blank line and do not include the newline.  */
      spec = save_string (p, p1 - p);
      p = p1;

      /* Delete backslash-newline sequences from the spec.  */
      in = spec;
      out = spec;
      while (*in != 0)
	{
	  if (in[0] == '\\' && in[1] == '\n')
	    in += 2;
	  else if (in[0] == '#')
	    while (*in && *in != '\n')
	      in++;

	  else
	    *out++ = *in++;
	}
      *out = 0;

      if (suffix[0] == '*')
	{
	  if (! strcmp (suffix, "*link_command"))
	    link_command_spec = spec;
	  else
	    set_spec (suffix + 1, spec);
	}
      else
	{
	  /* Add this pair to the vector.  */
	  compilers
	    = xrealloc (compilers,
			(n_compilers + 2) * sizeof (struct compiler));

	  compilers[n_compilers].suffix = suffix;
	  compilers[n_compilers].spec = spec;
	  n_compilers++;
	  memset (&compilers[n_compilers], 0, sizeof compilers[n_compilers]);
	}

      if (*suffix == 0)
	link_command_spec = spec;
    }

  if (link_command_spec == 0)
    fatal ("spec file has no spec for linking");
}

/* Record the names of temporary files we tell compilers to write,
   and delete them at the end of the run.  */

/* This is the common prefix we use to make temp file names.
   It is chosen once for each run of this program.
   It is substituted into a spec by %g or %j.
   Thus, all temp file names contain this prefix.
   In practice, all temp file names start with this prefix.

   This prefix comes from the envvar TMPDIR if it is defined;
   otherwise, from the P_tmpdir macro if that is defined;
   otherwise, in /usr/tmp or /tmp;
   or finally the current directory if all else fails.  */

static const char *temp_filename;

/* Length of the prefix.  */

static int temp_filename_length;

/* Define the list of temporary files to delete.  */

struct temp_file
{
  const char *name;
  struct temp_file *next;
};

/* Queue of files to delete on success or failure of compilation.  */
static struct temp_file *always_delete_queue;
/* Queue of files to delete on failure of compilation.  */
static struct temp_file *failure_delete_queue;

/* Record FILENAME as a file to be deleted automatically.
   ALWAYS_DELETE nonzero means delete it if all compilation succeeds;
   otherwise delete it in any case.
   FAIL_DELETE nonzero means delete it if a compilation step fails;
   otherwise delete it in any case.  */

void
record_temp_file (const char *filename, int always_delete, int fail_delete)
{
  char *const name = xstrdup (filename);

  if (always_delete)
    {
      struct temp_file *temp;
      for (temp = always_delete_queue; temp; temp = temp->next)
	if (! strcmp (name, temp->name))
	  goto already1;

      temp = XNEW (struct temp_file);
      temp->next = always_delete_queue;
      temp->name = name;
      always_delete_queue = temp;

    already1:;
    }

  if (fail_delete)
    {
      struct temp_file *temp;
      for (temp = failure_delete_queue; temp; temp = temp->next)
	if (! strcmp (name, temp->name))
	  goto already2;

      temp = XNEW (struct temp_file);
      temp->next = failure_delete_queue;
      temp->name = name;
      failure_delete_queue = temp;

    already2:;
    }
}

/* Delete all the temporary files whose names we previously recorded.  */

#ifndef DELETE_IF_ORDINARY
#define DELETE_IF_ORDINARY(NAME,ST,VERBOSE_FLAG)        \
do                                                      \
  {                                                     \
    if (stat (NAME, &ST) >= 0 && S_ISREG (ST.st_mode))  \
      if (unlink (NAME) < 0)                            \
	if (VERBOSE_FLAG)                               \
	  perror_with_name (NAME);                      \
  } while (0)
#endif

static void
delete_if_ordinary (const char *name)
{
  struct stat st;
#ifdef DEBUG
  int i, c;

  printf ("Delete %s? (y or n) ", name);
  fflush (stdout);
  i = getchar ();
  if (i != '\n')
    while ((c = getchar ()) != '\n' && c != EOF)
      ;

  if (i == 'y' || i == 'Y')
#endif /* DEBUG */
  DELETE_IF_ORDINARY (name, st, verbose_flag);
}

static void
delete_temp_files (void)
{
  struct temp_file *temp;

  for (temp = always_delete_queue; temp; temp = temp->next)
    delete_if_ordinary (temp->name);
  always_delete_queue = 0;
}

/* Delete all the files to be deleted on error.  */

static void
delete_failure_queue (void)
{
  struct temp_file *temp;

  for (temp = failure_delete_queue; temp; temp = temp->next)
    delete_if_ordinary (temp->name);
}

static void
clear_failure_queue (void)
{
  failure_delete_queue = 0;
}

/* Call CALLBACK for each path in PATHS, breaking out early if CALLBACK
   returns non-NULL.
   If DO_MULTI is true iterate over the paths twice, first with multilib
   suffix then without, otherwise iterate over the paths once without
   adding a multilib suffix.  When DO_MULTI is true, some attempt is made
   to avoid visiting the same path twice, but we could do better.  For
   instance, /usr/lib/../lib is considered different from /usr/lib.
   At least EXTRA_SPACE chars past the end of the path passed to
   CALLBACK are available for use by the callback.
   CALLBACK_INFO allows extra parameters to be passed to CALLBACK.

   Returns the value returned by CALLBACK.  */

static void *
for_each_path (const struct path_prefix *paths,
	       bool do_multi,
	       size_t extra_space,
	       void *(*callback) (char *, void *),
	       void *callback_info)
{
  struct prefix_list *pl;
  const char *multi_dir = NULL;
  const char *multi_os_dir = NULL;
  const char *multi_suffix;
  const char *just_multi_suffix;
  char *path = NULL;
  void *ret = NULL;
  bool skip_multi_dir = false;
  bool skip_multi_os_dir = false;

  multi_suffix = machine_suffix;
  just_multi_suffix = just_machine_suffix;
  if (do_multi && multilib_dir && strcmp (multilib_dir, ".") != 0)
    {
      multi_dir = concat (multilib_dir, dir_separator_str, NULL);
      multi_suffix = concat (multi_suffix, multi_dir, NULL);
      just_multi_suffix = concat (just_multi_suffix, multi_dir, NULL);
    }
  if (do_multi && multilib_os_dir && strcmp (multilib_os_dir, ".") != 0)
    multi_os_dir = concat (multilib_os_dir, dir_separator_str, NULL);

  while (1)
    {
      size_t multi_dir_len = 0;
      size_t multi_os_dir_len = 0;
      size_t suffix_len;
      size_t just_suffix_len;
      size_t len;

      if (multi_dir)
	multi_dir_len = strlen (multi_dir);
      if (multi_os_dir)
	multi_os_dir_len = strlen (multi_os_dir);
      suffix_len = strlen (multi_suffix);
      just_suffix_len = strlen (just_multi_suffix);

      if (path == NULL)
	{
	  len = paths->max_len + extra_space + 1;
	  if (suffix_len > multi_os_dir_len)
	    len += suffix_len;
	  else
	    len += multi_os_dir_len;
	  path = XNEWVEC (char, len);
	}

      for (pl = paths->plist; pl != 0; pl = pl->next)
	{
	  len = strlen (pl->prefix);
	  memcpy (path, pl->prefix, len);

	  /* Look first in MACHINE/VERSION subdirectory.  */
	  if (!skip_multi_dir)
	    {
	      memcpy (path + len, multi_suffix, suffix_len + 1);
	      ret = callback (path, callback_info);
	      if (ret)
		break;
	    }

	  /* Some paths are tried with just the machine (ie. target)
	     subdir.  This is used for finding as, ld, etc.  */
	  if (!skip_multi_dir
	      && pl->require_machine_suffix == 2)
	    {
	      memcpy (path + len, just_multi_suffix, just_suffix_len + 1);
	      ret = callback (path, callback_info);
	      if (ret)
		break;
	    }

	  /* Now try the base path.  */
	  if (!pl->require_machine_suffix
	      && !(pl->os_multilib ? skip_multi_os_dir : skip_multi_dir))
	    {
	      const char *this_multi;
	      size_t this_multi_len;

	      if (pl->os_multilib)
		{
		  this_multi = multi_os_dir;
		  this_multi_len = multi_os_dir_len;
		}
	      else
		{
		  this_multi = multi_dir;
		  this_multi_len = multi_dir_len;
		}

	      if (this_multi_len)
		memcpy (path + len, this_multi, this_multi_len + 1);
	      else
		path[len] = '\0';

	      ret = callback (path, callback_info);
	      if (ret)
		break;
	    }
	}
      if (pl)
	break;

      if (multi_dir == NULL && multi_os_dir == NULL)
	break;

      /* Run through the paths again, this time without multilibs.
	 Don't repeat any we have already seen.  */
      if (multi_dir)
	{
	  free ((char *) multi_dir);
	  multi_dir = NULL;
	  free ((char *) multi_suffix);
	  multi_suffix = machine_suffix;
	  free ((char *) just_multi_suffix);
	  just_multi_suffix = just_machine_suffix;
	}
      else
	skip_multi_dir = true;
      if (multi_os_dir)
	{
	  free ((char *) multi_os_dir);
	  multi_os_dir = NULL;
	}
      else
	skip_multi_os_dir = true;
    }

  if (multi_dir)
    {
      free ((char *) multi_dir);
      free ((char *) multi_suffix);
      free ((char *) just_multi_suffix);
    }
  if (multi_os_dir)
    free ((char *) multi_os_dir);
  if (ret != path)
    free (path);
  return ret;
}

/* Callback for build_search_list.  Adds path to obstack being built.  */

struct add_to_obstack_info {
  struct obstack *ob;
  bool check_dir;
  bool first_time;
};

static void *
add_to_obstack (char *path, void *data)
{
  struct add_to_obstack_info *info = data;

  if (info->check_dir && !is_directory (path, false))
    return NULL;

  if (!info->first_time)
    obstack_1grow (info->ob, PATH_SEPARATOR);

  obstack_grow (info->ob, path, strlen (path));

  info->first_time = false;
  return NULL;
}

/* Build a list of search directories from PATHS.
   PREFIX is a string to prepend to the list.
   If CHECK_DIR_P is true we ensure the directory exists.
   If DO_MULTI is true, multilib paths are output first, then
   non-multilib paths.
   This is used mostly by putenv_from_prefixes so we use `collect_obstack'.
   It is also used by the --print-search-dirs flag.  */

static char *
build_search_list (const struct path_prefix *paths, const char *prefix,
		   bool check_dir, bool do_multi)
{
  struct add_to_obstack_info info;

  info.ob = &collect_obstack;
  info.check_dir = check_dir;
  info.first_time = true;

  obstack_grow (&collect_obstack, prefix, strlen (prefix));
  obstack_1grow (&collect_obstack, '=');

  for_each_path (paths, do_multi, 0, add_to_obstack, &info);

  obstack_1grow (&collect_obstack, '\0');
  return XOBFINISH (&collect_obstack, char *);
}

/* Rebuild the COMPILER_PATH and LIBRARY_PATH environment variables
   for collect.  */

static void
putenv_from_prefixes (const struct path_prefix *paths, const char *env_var,
		      bool do_multi)
{
  putenv (build_search_list (paths, env_var, true, do_multi));
}

/* Check whether NAME can be accessed in MODE.  This is like access,
   except that it never considers directories to be executable.  */

static int
access_check (const char *name, int mode)
{
  if (mode == X_OK)
    {
      struct stat st;

      if (stat (name, &st) < 0
	  || S_ISDIR (st.st_mode))
	return -1;
    }

  return access (name, mode);
}

/* Callback for find_a_file.  Appends the file name to the directory
   path.  If the resulting file exists in the right mode, return the
   full pathname to the file.  */

struct file_at_path_info {
  const char *name;
  const char *suffix;
  int name_len;
  int suffix_len;
  int mode;
};

static void *
file_at_path (char *path, void *data)
{
  struct file_at_path_info *info = data;
  size_t len = strlen (path);

  memcpy (path + len, info->name, info->name_len);
  len += info->name_len;

  /* Some systems have a suffix for executable files.
     So try appending that first.  */
  if (info->suffix_len)
    {
      memcpy (path + len, info->suffix, info->suffix_len + 1);
      if (access_check (path, info->mode) == 0)
	return path;
    }

  path[len] = '\0';
  if (access_check (path, info->mode) == 0)
    return path;

  return NULL;
}

/* Search for NAME using the prefix list PREFIXES.  MODE is passed to
   access to check permissions.  If DO_MULTI is true, search multilib
   paths then non-multilib paths, otherwise do not search multilib paths.
   Return 0 if not found, otherwise return its name, allocated with malloc.  */

static char *
find_a_file (const struct path_prefix *pprefix, const char *name, int mode,
	     bool do_multi)
{
  struct file_at_path_info info;

#ifdef DEFAULT_ASSEMBLER
  if (! strcmp (name, "as") && access (DEFAULT_ASSEMBLER, mode) == 0)
    return xstrdup (DEFAULT_ASSEMBLER);
#endif

#ifdef DEFAULT_LINKER
  if (! strcmp(name, "ld") && access (DEFAULT_LINKER, mode) == 0)
    return xstrdup (DEFAULT_LINKER);
#endif

  /* Determine the filename to execute (special case for absolute paths).  */

  if (IS_ABSOLUTE_PATH (name))
    {
      if (access (name, mode) == 0)
	return xstrdup (name);

      return NULL;
    }

  info.name = name;
  info.suffix = (mode & X_OK) != 0 ? HOST_EXECUTABLE_SUFFIX : "";
  info.name_len = strlen (info.name);
  info.suffix_len = strlen (info.suffix);
  info.mode = mode;

  return for_each_path (pprefix, do_multi, info.name_len + info.suffix_len,
			file_at_path, &info);
}

/* Ranking of prefixes in the sort list. -B prefixes are put before
   all others.  */

enum path_prefix_priority
{
  PREFIX_PRIORITY_B_OPT,
  PREFIX_PRIORITY_LAST
};

/* Add an entry for PREFIX in PLIST.  The PLIST is kept in ascending
   order according to PRIORITY.  Within each PRIORITY, new entries are
   appended.

   If WARN is nonzero, we will warn if no file is found
   through this prefix.  WARN should point to an int
   which will be set to 1 if this entry is used.

   COMPONENT is the value to be passed to update_path.

   REQUIRE_MACHINE_SUFFIX is 1 if this prefix can't be used without
   the complete value of machine_suffix.
   2 means try both machine_suffix and just_machine_suffix.  */

static void
add_prefix (struct path_prefix *pprefix, const char *prefix,
	    const char *component, /* enum prefix_priority */ int priority,
	    int require_machine_suffix, int os_multilib)
{
  struct prefix_list *pl, **prev;
  int len;

  for (prev = &pprefix->plist;
       (*prev) != NULL && (*prev)->priority <= priority;
       prev = &(*prev)->next)
    ;

  /* Keep track of the longest prefix.  */

  prefix = update_path (prefix, component);
  len = strlen (prefix);
  if (len > pprefix->max_len)
    pprefix->max_len = len;

  pl = XNEW (struct prefix_list);
  pl->prefix = prefix;
  pl->require_machine_suffix = require_machine_suffix;
  pl->priority = priority;
  pl->os_multilib = os_multilib;

  /* Insert after PREV.  */
  pl->next = (*prev);
  (*prev) = pl;
}

/* Same as add_prefix, but prepending target_system_root to prefix.  */
static void
add_sysrooted_prefix (struct path_prefix *pprefix, const char *prefix,
		      const char *component,
		      /* enum prefix_priority */ int priority,
		      int require_machine_suffix, int os_multilib)
{
  if (!IS_ABSOLUTE_PATH (prefix))
    fatal ("system path '%s' is not absolute", prefix);

  if (target_system_root)
    {
      if (target_sysroot_suffix)
	  prefix = concat (target_sysroot_suffix, prefix, NULL);
      prefix = concat (target_system_root, prefix, NULL);

      /* We have to override this because GCC's notion of sysroot
	 moves along with GCC.  */
      component = "GCC";
    }

  add_prefix (pprefix, prefix, component, priority,
	      require_machine_suffix, os_multilib);
}

/* Execute the command specified by the arguments on the current line of spec.
   When using pipes, this includes several piped-together commands
   with `|' between them.

   Return 0 if successful, -1 if failed.  */

static int
execute (void)
{
  int i;
  int n_commands;		/* # of command.  */
  char *string;
  struct pex_obj *pex;
  struct command
  {
    const char *prog;		/* program name.  */
    const char **argv;		/* vector of args.  */
  };

  struct command *commands;	/* each command buffer with above info.  */

  gcc_assert (!processing_spec_function);

  /* Count # of piped commands.  */
  for (n_commands = 1, i = 0; i < argbuf_index; i++)
    if (strcmp (argbuf[i], "|") == 0)
      n_commands++;

  /* Get storage for each command.  */
  commands = alloca (n_commands * sizeof (struct command));

  /* Split argbuf into its separate piped processes,
     and record info about each one.
     Also search for the programs that are to be run.  */

  commands[0].prog = argbuf[0]; /* first command.  */
  commands[0].argv = &argbuf[0];
  string = find_a_file (&exec_prefixes, commands[0].prog, X_OK, false);

  if (string)
    commands[0].argv[0] = string;

  for (n_commands = 1, i = 0; i < argbuf_index; i++)
    if (strcmp (argbuf[i], "|") == 0)
      {				/* each command.  */
#if defined (__MSDOS__) || defined (OS2) || defined (VMS)
	fatal ("-pipe not supported");
#endif
	argbuf[i] = 0;	/* termination of command args.  */
	commands[n_commands].prog = argbuf[i + 1];
	commands[n_commands].argv = &argbuf[i + 1];
	string = find_a_file (&exec_prefixes, commands[n_commands].prog,
			      X_OK, false);
	if (string)
	  commands[n_commands].argv[0] = string;
	n_commands++;
      }

  argbuf[argbuf_index] = 0;

  /* If -v, print what we are about to do, and maybe query.  */

  if (verbose_flag)
    {
      /* For help listings, put a blank line between sub-processes.  */
      if (print_help_list)
	fputc ('\n', stderr);

      /* Print each piped command as a separate line.  */
      for (i = 0; i < n_commands; i++)
	{
	  const char *const *j;

	  if (verbose_only_flag)
	    {
	      for (j = commands[i].argv; *j; j++)
		{
		  const char *p;
		  fprintf (stderr, " \"");
		  for (p = *j; *p; ++p)
		    {
		      if (*p == '"' || *p == '\\' || *p == '$')
			fputc ('\\', stderr);
		      fputc (*p, stderr);
		    }
		  fputc ('"', stderr);
		}
	    }
	  else
	    for (j = commands[i].argv; *j; j++)
	      fprintf (stderr, " %s", *j);

	  /* Print a pipe symbol after all but the last command.  */
	  if (i + 1 != n_commands)
	    fprintf (stderr, " |");
	  fprintf (stderr, "\n");
	}
      fflush (stderr);
      if (verbose_only_flag != 0)
        {
	  /* verbose_only_flag should act as if the spec was
	     executed, so increment execution_count before
	     returning.  This prevents spurious warnings about
	     unused linker input files, etc.  */
	  execution_count++;
	  return 0;
        }
#ifdef DEBUG
      notice ("\nGo ahead? (y or n) ");
      fflush (stderr);
      i = getchar ();
      if (i != '\n')
	while (getchar () != '\n')
	  ;

      if (i != 'y' && i != 'Y')
	return 0;
#endif /* DEBUG */
    }

#ifdef ENABLE_VALGRIND_CHECKING
  /* Run the each command through valgrind.  To simplify prepending the
     path to valgrind and the option "-q" (for quiet operation unless
     something triggers), we allocate a separate argv array.  */

  for (i = 0; i < n_commands; i++)
    {
      const char **argv;
      int argc;
      int j;

      for (argc = 0; commands[i].argv[argc] != NULL; argc++)
	;

      argv = alloca ((argc + 3) * sizeof (char *));

      argv[0] = VALGRIND_PATH;
      argv[1] = "-q";
      for (j = 2; j < argc + 2; j++)
	argv[j] = commands[i].argv[j - 2];
      argv[j] = NULL;

      commands[i].argv = argv;
      commands[i].prog = argv[0];
    }
#endif

  /* Run each piped subprocess.  */

  pex = pex_init (PEX_USE_PIPES | (report_times ? PEX_RECORD_TIMES : 0),
		  programname, temp_filename);
  if (pex == NULL)
    pfatal_with_name (_("pex_init failed"));

  for (i = 0; i < n_commands; i++)
    {
      const char *errmsg;
      int err;
      const char *string = commands[i].argv[0];

      errmsg = pex_run (pex,
			((i + 1 == n_commands ? PEX_LAST : 0)
			 | (string == commands[i].prog ? PEX_SEARCH : 0)),
			string, (char * const *) commands[i].argv,
			NULL, NULL, &err);
      if (errmsg != NULL)
	{
	  if (err == 0)
	    fatal (errmsg);
	  else
	    {
	      errno = err;
	      pfatal_with_name (errmsg);
	    }
	}

      if (string != commands[i].prog)
	free ((void *) string);
    }

  execution_count++;

  /* Wait for all the subprocesses to finish.  */

  {
    int *statuses;
    struct pex_time *times = NULL;
    int ret_code = 0;

    statuses = alloca (n_commands * sizeof (int));
    if (!pex_get_status (pex, n_commands, statuses))
      pfatal_with_name (_("failed to get exit status"));

    if (report_times)
      {
	times = alloca (n_commands * sizeof (struct pex_time));
	if (!pex_get_times (pex, n_commands, times))
	  pfatal_with_name (_("failed to get process times"));
      }

    pex_free (pex);

    for (i = 0; i < n_commands; ++i)
      {
	int status = statuses[i];

	if (WIFSIGNALED (status))
	  {
#ifdef SIGPIPE
	    /* SIGPIPE is a special case.  It happens in -pipe mode
	       when the compiler dies before the preprocessor is done,
	       or the assembler dies before the compiler is done.
	       There's generally been an error already, and this is
	       just fallout.  So don't generate another error unless
	       we would otherwise have succeeded.  */
	    if (WTERMSIG (status) == SIGPIPE
		&& (signal_count || greatest_status >= MIN_FATAL_STATUS))
	      {
		signal_count++;
		ret_code = -1;
	      }
	    else
#endif
	      fatal_ice ("\
Internal error: %s (program %s)\n\
Please submit a full bug report.\n\
See %s for instructions.",
		 	strsignal (WTERMSIG (status)), commands[i].prog,
		 	bug_report_url);
	  }
	else if (WIFEXITED (status)
		 && WEXITSTATUS (status) >= MIN_FATAL_STATUS)
	  {
	    if (WEXITSTATUS (status) > greatest_status)
	      greatest_status = WEXITSTATUS (status);
	    ret_code = -1;
	  }

	if (report_times)
	  {
	    struct pex_time *pt = &times[i];
	    double ut, st;

	    ut = ((double) pt->user_seconds
		  + (double) pt->user_microseconds / 1.0e6);
	    st = ((double) pt->system_seconds
		  + (double) pt->system_microseconds / 1.0e6);

	    if (ut + st != 0)
	      notice ("# %s %.2f %.2f\n", commands[i].prog, ut, st);
	  }
      }

    return ret_code;
  }
}

/* Find all the switches given to us
   and make a vector describing them.
   The elements of the vector are strings, one per switch given.
   If a switch uses following arguments, then the `part1' field
   is the switch itself and the `args' field
   is a null-terminated vector containing the following arguments.
   The `live_cond' field is:
   0 when initialized
   1 if the switch is true in a conditional spec,
   -1 if false (overridden by a later switch)
   -2 if this switch should be ignored (used in %<S)
   The `validated' field is nonzero if any spec has looked at this switch;
   if it remains zero at the end of the run, it must be meaningless.  */

#define SWITCH_OK       0
#define SWITCH_FALSE   -1
#define SWITCH_IGNORE  -2
#define SWITCH_LIVE     1

struct switchstr
{
  const char *part1;
  const char **args;
  int live_cond;
  unsigned char validated;
  unsigned char ordering;
};

static struct switchstr *switches;

static int n_switches;

/* Language is one of three things:

   1) The name of a real programming language.
   2) NULL, indicating that no one has figured out
   what it is yet.
   3) '*', indicating that the file should be passed
   to the linker.  */
struct infile
{
  const char *name;
  const char *language;
  struct compiler *incompiler;
  bool compiled;
  bool preprocessed;
};

/* Also a vector of input files specified.  */

static struct infile *infiles;

int n_infiles;

/* True if multiple input files are being compiled to a single
   assembly file.  */

static bool combine_inputs;

/* This counts the number of libraries added by lang_specific_driver, so that
   we can tell if there were any user supplied any files or libraries.  */

static int added_libraries;

/* And a vector of corresponding output files is made up later.  */

const char **outfiles;

#if defined(HAVE_TARGET_OBJECT_SUFFIX) || defined(HAVE_TARGET_EXECUTABLE_SUFFIX)

/* Convert NAME to a new name if it is the standard suffix.  DO_EXE
   is true if we should look for an executable suffix.  DO_OBJ
   is true if we should look for an object suffix.  */

static const char *
convert_filename (const char *name, int do_exe ATTRIBUTE_UNUSED,
		  int do_obj ATTRIBUTE_UNUSED)
{
#if defined(HAVE_TARGET_EXECUTABLE_SUFFIX)
  int i;
#endif
  int len;

  if (name == NULL)
    return NULL;

  len = strlen (name);

#ifdef HAVE_TARGET_OBJECT_SUFFIX
  /* Convert x.o to x.obj if TARGET_OBJECT_SUFFIX is ".obj".  */
  if (do_obj && len > 2
      && name[len - 2] == '.'
      && name[len - 1] == 'o')
    {
      obstack_grow (&obstack, name, len - 2);
      obstack_grow0 (&obstack, TARGET_OBJECT_SUFFIX, strlen (TARGET_OBJECT_SUFFIX));
      name = XOBFINISH (&obstack, const char *);
    }
#endif

#if defined(HAVE_TARGET_EXECUTABLE_SUFFIX)
  /* If there is no filetype, make it the executable suffix (which includes
     the ".").  But don't get confused if we have just "-o".  */
  if (! do_exe || TARGET_EXECUTABLE_SUFFIX[0] == 0 || (len == 2 && name[0] == '-'))
    return name;

  for (i = len - 1; i >= 0; i--)
    if (IS_DIR_SEPARATOR (name[i]))
      break;

  for (i++; i < len; i++)
    if (name[i] == '.')
      return name;

  obstack_grow (&obstack, name, len);
  obstack_grow0 (&obstack, TARGET_EXECUTABLE_SUFFIX,
		 strlen (TARGET_EXECUTABLE_SUFFIX));
  name = XOBFINISH (&obstack, const char *);
#endif

  return name;
}
#endif

/* Display the command line switches accepted by gcc.  */
static void
display_help (void)
{
  printf (_("Usage: %s [options] file...\n"), programname);
  fputs (_("Options:\n"), stdout);

  fputs (_("  -pass-exit-codes         Exit with highest error code from a phase\n"), stdout);
  fputs (_("  --help                   Display this information\n"), stdout);
  fputs (_("  --target-help            Display target specific command line options\n"), stdout);
  if (! verbose_flag)
    fputs (_("  (Use '-v --help' to display command line options of sub-processes)\n"), stdout);
  fputs (_("  -dumpspecs               Display all of the built in spec strings\n"), stdout);
  fputs (_("  -dumpversion             Display the version of the compiler\n"), stdout);
  fputs (_("  -dumpmachine             Display the compiler's target processor\n"), stdout);
  fputs (_("  -print-search-dirs       Display the directories in the compiler's search path\n"), stdout);
  fputs (_("  -print-libgcc-file-name  Display the name of the compiler's companion library\n"), stdout);
  fputs (_("  -print-file-name=<lib>   Display the full path to library <lib>\n"), stdout);
  fputs (_("  -print-prog-name=<prog>  Display the full path to compiler component <prog>\n"), stdout);
  fputs (_("  -print-multi-directory   Display the root directory for versions of libgcc\n"), stdout);
  fputs (_("\
  -print-multi-lib         Display the mapping between command line options and\n\
                           multiple library search directories\n"), stdout);
  fputs (_("  -print-multi-os-directory Display the relative path to OS libraries\n"), stdout);
  fputs (_("  -Wa,<options>            Pass comma-separated <options> on to the assembler\n"), stdout);
  fputs (_("  -Wp,<options>            Pass comma-separated <options> on to the preprocessor\n"), stdout);
  fputs (_("  -Wl,<options>            Pass comma-separated <options> on to the linker\n"), stdout);
  fputs (_("  -Xassembler <arg>        Pass <arg> on to the assembler\n"), stdout);
  fputs (_("  -Xpreprocessor <arg>     Pass <arg> on to the preprocessor\n"), stdout);
  fputs (_("  -Xlinker <arg>           Pass <arg> on to the linker\n"), stdout);
  fputs (_("  -combine                 Pass multiple source files to compiler at once\n"), stdout);
  fputs (_("  -save-temps              Do not delete intermediate files\n"), stdout);
  fputs (_("  -pipe                    Use pipes rather than intermediate files\n"), stdout);
  fputs (_("  -time                    Time the execution of each subprocess\n"), stdout);
  fputs (_("  -specs=<file>            Override built-in specs with the contents of <file>\n"), stdout);
  fputs (_("  -std=<standard>          Assume that the input sources are for <standard>\n"), stdout);
  fputs (_("\
  --sysroot=<directory>    Use <directory> as the root directory for headers\n\
                           and libraries\n"), stdout);
  fputs (_("  -B <directory>           Add <directory> to the compiler's search paths\n"), stdout);
  fputs (_("  -b <machine>             Run gcc for target <machine>, if installed\n"), stdout);
  fputs (_("  -V <version>             Run gcc version number <version>, if installed\n"), stdout);
  fputs (_("  -v                       Display the programs invoked by the compiler\n"), stdout);
  fputs (_("  -###                     Like -v but options quoted and commands not executed\n"), stdout);
  fputs (_("  -E                       Preprocess only; do not compile, assemble or link\n"), stdout);
  fputs (_("  -S                       Compile only; do not assemble or link\n"), stdout);
  fputs (_("  -c                       Compile and assemble, but do not link\n"), stdout);
  fputs (_("  -o <file>                Place the output into <file>\n"), stdout);
  fputs (_("\
  -x <language>            Specify the language of the following input files\n\
                           Permissible languages include: c c++ assembler none\n\
                           'none' means revert to the default behavior of\n\
                           guessing the language based on the file's extension\n\
"), stdout);

  printf (_("\
\nOptions starting with -g, -f, -m, -O, -W, or --param are automatically\n\
 passed on to the various sub-processes invoked by %s.  In order to pass\n\
 other options on to these processes the -W<letter> options must be used.\n\
"), programname);

  /* The rest of the options are displayed by invocations of the various
     sub-processes.  */
}

static void
add_preprocessor_option (const char *option, int len)
{
  n_preprocessor_options++;

  if (! preprocessor_options)
    preprocessor_options = XNEWVEC (char *, n_preprocessor_options);
  else
    preprocessor_options = xrealloc (preprocessor_options,
				     n_preprocessor_options * sizeof (char *));

  preprocessor_options [n_preprocessor_options - 1] =
    save_string (option, len);
}

static void
add_assembler_option (const char *option, int len)
{
  n_assembler_options++;

  if (! assembler_options)
    assembler_options = XNEWVEC (char *, n_assembler_options);
  else
    assembler_options = xrealloc (assembler_options,
				  n_assembler_options * sizeof (char *));

  assembler_options [n_assembler_options - 1] = save_string (option, len);
}

static void
add_linker_option (const char *option, int len)
{
  n_linker_options++;

  if (! linker_options)
    linker_options = XNEWVEC (char *, n_linker_options);
  else
    linker_options = xrealloc (linker_options,
			       n_linker_options * sizeof (char *));

  linker_options [n_linker_options - 1] = save_string (option, len);
}

/* Create the vector `switches' and its contents.
   Store its length in `n_switches'.  */

static void
process_command (int argc, const char **argv)
{
  int i;
  const char *temp;
  char *temp1;
  const char *spec_lang = 0;
  int last_language_n_infiles;
  int lang_n_infiles = 0;
#ifdef MODIFY_TARGET_NAME
  int is_modify_target_name;
  unsigned int j;
#endif

  GET_ENVIRONMENT (gcc_exec_prefix, "GCC_EXEC_PREFIX");

  n_switches = 0;
  n_infiles = 0;
  added_libraries = 0;

  /* Figure compiler version from version string.  */

  compiler_version = temp1 = xstrdup (version_string);

  for (; *temp1; ++temp1)
    {
      if (*temp1 == ' ')
	{
	  *temp1 = '\0';
	  break;
	}
    }

  /* If there is a -V or -b option (or both), process it now, before
     trying to interpret the rest of the command line.
     Use heuristic that all configuration names must have at least
     one dash '-'. This allows us to pass options starting with -b.  */
  if (argc > 1 && argv[1][0] == '-'
      && (argv[1][1] == 'V' ||
	 ((argv[1][1] == 'b') && (NULL != strchr(argv[1] + 2,'-')))))
    {
      const char *new_version = DEFAULT_TARGET_VERSION;
      const char *new_machine = DEFAULT_TARGET_MACHINE;
      const char *progname = argv[0];
      char **new_argv;
      char *new_argv0;
      int baselen;

      while (argc > 1 && argv[1][0] == '-'
	     && (argv[1][1] == 'V' ||
		((argv[1][1] == 'b') && ( NULL != strchr(argv[1] + 2,'-')))))
	{
	  char opt = argv[1][1];
	  const char *arg;
	  if (argv[1][2] != '\0')
	    {
	      arg = argv[1] + 2;
	      argc -= 1;
	      argv += 1;
	    }
	  else if (argc > 2)
	    {
	      arg = argv[2];
	      argc -= 2;
	      argv += 2;
	    }
	  else
	    fatal ("'-%c' option must have argument", opt);
	  if (opt == 'V')
	    new_version = arg;
	  else
	    new_machine = arg;
	}

      for (baselen = strlen (progname); baselen > 0; baselen--)
	if (IS_DIR_SEPARATOR (progname[baselen-1]))
	  break;
      new_argv0 = xmemdup (progname, baselen,
			   baselen + concat_length (new_version, new_machine,
						    "-gcc-", NULL) + 1);
      strcpy (new_argv0 + baselen, new_machine);
      strcat (new_argv0, "-gcc-");
      strcat (new_argv0, new_version);

      new_argv = xmemdup (argv, (argc + 1) * sizeof (argv[0]),
			  (argc + 1) * sizeof (argv[0]));
      new_argv[0] = new_argv0;

      execvp (new_argv0, new_argv);
      fatal ("couldn't run '%s': %s", new_argv0, xstrerror (errno));
    }

  /* Set up the default search paths.  If there is no GCC_EXEC_PREFIX,
     see if we can create it from the pathname specified in argv[0].  */

  gcc_libexec_prefix = standard_libexec_prefix;
#ifndef VMS
  /* FIXME: make_relative_prefix doesn't yet work for VMS.  */
  if (!gcc_exec_prefix)
    {
      gcc_exec_prefix = make_relative_prefix (argv[0], standard_bindir_prefix,
					      standard_exec_prefix);
      gcc_libexec_prefix = make_relative_prefix (argv[0],
						 standard_bindir_prefix,
						 standard_libexec_prefix);
      if (gcc_exec_prefix)
	putenv (concat ("GCC_EXEC_PREFIX=", gcc_exec_prefix, NULL));
    }
  else
    {
      /* make_relative_prefix requires a program name, but
	 GCC_EXEC_PREFIX is typically a directory name with a trailing
	 / (which is ignored by make_relative_prefix), so append a
	 program name.  */
      char *tmp_prefix = concat (gcc_exec_prefix, "gcc", NULL);
      gcc_libexec_prefix = make_relative_prefix (tmp_prefix,
						 standard_exec_prefix,
						 standard_libexec_prefix);
      free (tmp_prefix);
    }
#else
#endif

  if (gcc_exec_prefix)
    {
      int len = strlen (gcc_exec_prefix);

      if (len > (int) sizeof ("/lib/gcc/") - 1
	  && (IS_DIR_SEPARATOR (gcc_exec_prefix[len-1])))
	{
	  temp = gcc_exec_prefix + len - sizeof ("/lib/gcc/") + 1;
	  if (IS_DIR_SEPARATOR (*temp)
	      && strncmp (temp + 1, "lib", 3) == 0
	      && IS_DIR_SEPARATOR (temp[4])
	      && strncmp (temp + 5, "gcc", 3) == 0)
	    len -= sizeof ("/lib/gcc/") - 1;
	}

      set_std_prefix (gcc_exec_prefix, len);
      add_prefix (&exec_prefixes, gcc_libexec_prefix, "GCC",
		  PREFIX_PRIORITY_LAST, 0, 0);
      add_prefix (&startfile_prefixes, gcc_exec_prefix, "GCC",
		  PREFIX_PRIORITY_LAST, 0, 0);
    }

  /* COMPILER_PATH and LIBRARY_PATH have values
     that are lists of directory names with colons.  */

  GET_ENVIRONMENT (temp, "COMPILER_PATH");
  if (temp)
    {
      const char *startp, *endp;
      char *nstore = alloca (strlen (temp) + 3);

      startp = endp = temp;
      while (1)
	{
	  if (*endp == PATH_SEPARATOR || *endp == 0)
	    {
	      strncpy (nstore, startp, endp - startp);
	      if (endp == startp)
		strcpy (nstore, concat (".", dir_separator_str, NULL));
	      else if (!IS_DIR_SEPARATOR (endp[-1]))
		{
		  nstore[endp - startp] = DIR_SEPARATOR;
		  nstore[endp - startp + 1] = 0;
		}
	      else
		nstore[endp - startp] = 0;
	      add_prefix (&exec_prefixes, nstore, 0,
			  PREFIX_PRIORITY_LAST, 0, 0);
	      add_prefix (&include_prefixes, nstore, 0,
			  PREFIX_PRIORITY_LAST, 0, 0);
	      if (*endp == 0)
		break;
	      endp = startp = endp + 1;
	    }
	  else
	    endp++;
	}
    }

  GET_ENVIRONMENT (temp, LIBRARY_PATH_ENV);
  if (temp && *cross_compile == '0')
    {
      const char *startp, *endp;
      char *nstore = alloca (strlen (temp) + 3);

      startp = endp = temp;
      while (1)
	{
	  if (*endp == PATH_SEPARATOR || *endp == 0)
	    {
	      strncpy (nstore, startp, endp - startp);
	      if (endp == startp)
		strcpy (nstore, concat (".", dir_separator_str, NULL));
	      else if (!IS_DIR_SEPARATOR (endp[-1]))
		{
		  nstore[endp - startp] = DIR_SEPARATOR;
		  nstore[endp - startp + 1] = 0;
		}
	      else
		nstore[endp - startp] = 0;
	      add_prefix (&startfile_prefixes, nstore, NULL,
			  PREFIX_PRIORITY_LAST, 0, 1);
	      if (*endp == 0)
		break;
	      endp = startp = endp + 1;
	    }
	  else
	    endp++;
	}
    }

  /* Use LPATH like LIBRARY_PATH (for the CMU build program).  */
  GET_ENVIRONMENT (temp, "LPATH");
  if (temp && *cross_compile == '0')
    {
      const char *startp, *endp;
      char *nstore = alloca (strlen (temp) + 3);

      startp = endp = temp;
      while (1)
	{
	  if (*endp == PATH_SEPARATOR || *endp == 0)
	    {
	      strncpy (nstore, startp, endp - startp);
	      if (endp == startp)
		strcpy (nstore, concat (".", dir_separator_str, NULL));
	      else if (!IS_DIR_SEPARATOR (endp[-1]))
		{
		  nstore[endp - startp] = DIR_SEPARATOR;
		  nstore[endp - startp + 1] = 0;
		}
	      else
		nstore[endp - startp] = 0;
	      add_prefix (&startfile_prefixes, nstore, NULL,
			  PREFIX_PRIORITY_LAST, 0, 1);
	      if (*endp == 0)
		break;
	      endp = startp = endp + 1;
	    }
	  else
	    endp++;
	}
    }

  /* Convert new-style -- options to old-style.  */
  translate_options (&argc, (const char *const **) &argv);

  /* Do language-specific adjustment/addition of flags.  */
  lang_specific_driver (&argc, (const char *const **) &argv, &added_libraries);

  /* Scan argv twice.  Here, the first time, just count how many switches
     there will be in their vector, and how many input files in theirs.
     Here we also parse the switches that cc itself uses (e.g. -v).  */

  for (i = 1; i < argc; i++)
    {
      if (! strcmp (argv[i], "-dumpspecs"))
	{
	  struct spec_list *sl;
	  init_spec ();
	  for (sl = specs; sl; sl = sl->next)
	    printf ("*%s:\n%s\n\n", sl->name, *(sl->ptr_spec));
	  if (link_command_spec)
	    printf ("*link_command:\n%s\n\n", link_command_spec);
	  exit (0);
	}
      else if (! strcmp (argv[i], "-dumpversion"))
	{
	  printf ("%s\n", spec_version);
	  exit (0);
	}
      else if (! strcmp (argv[i], "-dumpmachine"))
	{
	  printf ("%s\n", spec_machine);
	  exit (0);
	}
      else if (strcmp (argv[i], "-fversion") == 0)
	{
	  /* translate_options () has turned --version into -fversion.  */
	  printf (_("%s (GCC) %s\n"), programname, version_string);
	  printf ("Copyright %s 2007 Free Software Foundation, Inc.\n",
		  _("(C)"));
	  fputs (_("This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n"),
		 stdout);
	  exit (0);
	}
      else if (strcmp (argv[i], "-fhelp") == 0)
	{
	  /* translate_options () has turned --help into -fhelp.  */
	  print_help_list = 1;

	  /* We will be passing a dummy file on to the sub-processes.  */
	  n_infiles++;
	  n_switches++;

	  /* CPP driver cannot obtain switch from cc1_options.  */
	  if (is_cpp_driver)
	    add_preprocessor_option ("--help", 6);
	  add_assembler_option ("--help", 6);
	  add_linker_option ("--help", 6);
	}
      else if (strcmp (argv[i], "-ftarget-help") == 0)
	{
	  /* translate_options() has turned --target-help into -ftarget-help.  */
	  target_help_flag = 1;

	  /* We will be passing a dummy file on to the sub-processes.  */
	  n_infiles++;
	  n_switches++;

	  /* CPP driver cannot obtain switch from cc1_options.  */
	  if (is_cpp_driver)
	    add_preprocessor_option ("--target-help", 13);
	  add_assembler_option ("--target-help", 13);
	  add_linker_option ("--target-help", 13);
	}
      else if (! strcmp (argv[i], "-pass-exit-codes"))
	{
	  pass_exit_codes = 1;
	  n_switches++;
	}
      else if (! strcmp (argv[i], "-print-search-dirs"))
	print_search_dirs = 1;
      else if (! strcmp (argv[i], "-print-libgcc-file-name"))
	print_file_name = "libgcc.a";
      else if (! strncmp (argv[i], "-print-file-name=", 17))
	print_file_name = argv[i] + 17;
      else if (! strncmp (argv[i], "-print-prog-name=", 17))
	print_prog_name = argv[i] + 17;
      else if (! strcmp (argv[i], "-print-multi-lib"))
	print_multi_lib = 1;
      else if (! strcmp (argv[i], "-print-multi-directory"))
	print_multi_directory = 1;
      else if (! strcmp (argv[i], "-print-multi-os-directory"))
	print_multi_os_directory = 1;
      else if (! strncmp (argv[i], "-Wa,", 4))
	{
	  int prev, j;
	  /* Pass the rest of this option to the assembler.  */

	  /* Split the argument at commas.  */
	  prev = 4;
	  for (j = 4; argv[i][j]; j++)
	    if (argv[i][j] == ',')
	      {
		add_assembler_option (argv[i] + prev, j - prev);
		prev = j + 1;
	      }

	  /* Record the part after the last comma.  */
	  add_assembler_option (argv[i] + prev, j - prev);
	}
      else if (! strncmp (argv[i], "-Wp,", 4))
	{
	  int prev, j;
	  /* Pass the rest of this option to the preprocessor.  */

	  /* Split the argument at commas.  */
	  prev = 4;
	  for (j = 4; argv[i][j]; j++)
	    if (argv[i][j] == ',')
	      {
		add_preprocessor_option (argv[i] + prev, j - prev);
		prev = j + 1;
	      }

	  /* Record the part after the last comma.  */
	  add_preprocessor_option (argv[i] + prev, j - prev);
	}
      else if (argv[i][0] == '+' && argv[i][1] == 'e')
	/* The +e options to the C++ front-end.  */
	n_switches++;
      else if (strncmp (argv[i], "-Wl,", 4) == 0)
	{
	  int j;
	  /* Split the argument at commas.  */
	  for (j = 3; argv[i][j]; j++)
	    n_infiles += (argv[i][j] == ',');
	}
      else if (strcmp (argv[i], "-Xlinker") == 0)
	{
	  if (i + 1 == argc)
	    fatal ("argument to '-Xlinker' is missing");

	  n_infiles++;
	  i++;
	}
      else if (strcmp (argv[i], "-Xpreprocessor") == 0)
	{
	  if (i + 1 == argc)
	    fatal ("argument to '-Xpreprocessor' is missing");

	  add_preprocessor_option (argv[i+1], strlen (argv[i+1]));
	}
      else if (strcmp (argv[i], "-Xassembler") == 0)
	{
	  if (i + 1 == argc)
	    fatal ("argument to '-Xassembler' is missing");

	  add_assembler_option (argv[i+1], strlen (argv[i+1]));
	}
      else if (strcmp (argv[i], "-l") == 0)
	{
	  if (i + 1 == argc)
	    fatal ("argument to '-l' is missing");

	  n_infiles++;
	  i++;
	}
      else if (strncmp (argv[i], "-l", 2) == 0)
	n_infiles++;
      else if (strcmp (argv[i], "-save-temps") == 0)
	{
	  save_temps_flag = 1;
	  n_switches++;
	}
      else if (strcmp (argv[i], "-combine") == 0)
	{
	  combine_flag = 1;
	  n_switches++;
	}
      else if (strcmp (argv[i], "-specs") == 0)
	{
	  struct user_specs *user = XNEW (struct user_specs);
	  if (++i >= argc)
	    fatal ("argument to '-specs' is missing");

	  user->next = (struct user_specs *) 0;
	  user->filename = argv[i];
	  if (user_specs_tail)
	    user_specs_tail->next = user;
	  else
	    user_specs_head = user;
	  user_specs_tail = user;
	}
      else if (strncmp (argv[i], "-specs=", 7) == 0)
	{
	  struct user_specs *user = XNEW (struct user_specs);
	  if (strlen (argv[i]) == 7)
	    fatal ("argument to '-specs=' is missing");

	  user->next = (struct user_specs *) 0;
	  user->filename = argv[i] + 7;
	  if (user_specs_tail)
	    user_specs_tail->next = user;
	  else
	    user_specs_head = user;
	  user_specs_tail = user;
	}
      else if (strcmp (argv[i], "-time") == 0)
	report_times = 1;
      else if (strcmp (argv[i], "-pipe") == 0)
	{
	  /* -pipe has to go into the switches array as well as
	     setting a flag.  */
	  use_pipes = 1;
	  n_switches++;
	}
      else if (strcmp (argv[i], "-###") == 0)
	{
	  /* This is similar to -v except that there is no execution
	     of the commands and the echoed arguments are quoted.  It
	     is intended for use in shell scripts to capture the
	     driver-generated command line.  */
	  verbose_only_flag++;
	  verbose_flag++;
	}
      else if (argv[i][0] == '-' && argv[i][1] != 0)
	{
	  const char *p = &argv[i][1];
	  int c = *p;

	  switch (c)
	    {
	    case 'b':
	      if (NULL == strchr(argv[i] + 2, '-'))
		goto normal_switch;

	      /* Fall through.  */
	    case 'V':
	      fatal ("'-%c' must come at the start of the command line", c);
	      break;

	    case 'B':
	      {
		const char *value;
		int len;

		if (p[1] == 0 && i + 1 == argc)
		  fatal ("argument to '-B' is missing");
		if (p[1] == 0)
		  value = argv[++i];
		else
		  value = p + 1;

		len = strlen (value);

		/* Catch the case where the user has forgotten to append a
		   directory separator to the path.  Note, they may be using
		   -B to add an executable name prefix, eg "i386-elf-", in
		   order to distinguish between multiple installations of
		   GCC in the same directory.  Hence we must check to see
		   if appending a directory separator actually makes a
		   valid directory name.  */
		if (! IS_DIR_SEPARATOR (value [len - 1])
		    && is_directory (value, false))
		  {
		    char *tmp = XNEWVEC (char, len + 2);
		    strcpy (tmp, value);
		    tmp[len] = DIR_SEPARATOR;
		    tmp[++ len] = 0;
		    value = tmp;
		  }

		/* As a kludge, if the arg is "[foo/]stageN/", just
		   add "[foo/]include" to the include prefix.  */
		if ((len == 7
		     || (len > 7
			 && (IS_DIR_SEPARATOR (value[len - 8]))))
		    && strncmp (value + len - 7, "stage", 5) == 0
		    && ISDIGIT (value[len - 2])
		    && (IS_DIR_SEPARATOR (value[len - 1])))
		  {
		    if (len == 7)
		      add_prefix (&include_prefixes, "./", NULL,
				  PREFIX_PRIORITY_B_OPT, 0, 0);
		    else
		      {
		        char *string = xmalloc (len - 6);
			memcpy (string, value, len - 7);
			string[len - 7] = 0;
		        add_prefix (&include_prefixes, string, NULL,
				    PREFIX_PRIORITY_B_OPT, 0, 0);
		      }
		  }

		add_prefix (&exec_prefixes, value, NULL,
			    PREFIX_PRIORITY_B_OPT, 0, 0);
		add_prefix (&startfile_prefixes, value, NULL,
			    PREFIX_PRIORITY_B_OPT, 0, 0);
		add_prefix (&include_prefixes, value, NULL,
			    PREFIX_PRIORITY_B_OPT, 0, 0);
		n_switches++;
	      }
	      break;

	    case 'v':	/* Print our subcommands and print versions.  */
	      n_switches++;
	      /* If they do anything other than exactly `-v', don't set
		 verbose_flag; rather, continue on to give the error.  */
	      if (p[1] != 0)
		break;
	      verbose_flag++;
	      break;

	    case 'S':
	    case 'c':
	      if (p[1] == 0)
		{
		  have_c = 1;
		  n_switches++;
		  break;
		}
	      goto normal_switch;

	    case 'o':
	      have_o = 1;
#if defined(HAVE_TARGET_EXECUTABLE_SUFFIX)
	      if (! have_c)
		{
		  int skip;

		  /* Forward scan, just in case -S or -c is specified
		     after -o.  */
		  int j = i + 1;
		  if (p[1] == 0)
		    ++j;
		  while (j < argc)
		    {
		      if (argv[j][0] == '-')
			{
			  if (SWITCH_CURTAILS_COMPILATION (argv[j][1])
			      && argv[j][2] == 0)
			    {
			      have_c = 1;
			      break;
			    }
			  else if ((skip = SWITCH_TAKES_ARG (argv[j][1])))
			    j += skip - (argv[j][2] != 0);
			  else if ((skip = WORD_SWITCH_TAKES_ARG (argv[j] + 1)))
			    j += skip;
			}
		      j++;
		    }
		}
#endif
#if defined(HAVE_TARGET_EXECUTABLE_SUFFIX) || defined(HAVE_TARGET_OBJECT_SUFFIX)
	      if (p[1] == 0)
		argv[i + 1] = convert_filename (argv[i + 1], ! have_c, 0);
	      else
		argv[i] = convert_filename (argv[i], ! have_c, 0);
#endif
	      goto normal_switch;

	    default:
	    normal_switch:

#ifdef MODIFY_TARGET_NAME
	      is_modify_target_name = 0;

	      for (j = 0; j < ARRAY_SIZE (modify_target); j++)
		if (! strcmp (argv[i], modify_target[j].sw))
		  {
		    char *new_name = xmalloc (strlen (modify_target[j].str)
					      + strlen (spec_machine));
		    const char *p, *r;
		    char *q;
		    int made_addition = 0;

		    is_modify_target_name = 1;
		    for (p = spec_machine, q = new_name; *p != 0; )
		      {
			if (modify_target[j].add_del == DELETE
			    && (! strncmp (q, modify_target[j].str,
					   strlen (modify_target[j].str))))
			  p += strlen (modify_target[j].str);
			else if (modify_target[j].add_del == ADD
				 && ! made_addition && *p == '-')
			  {
			    for (r = modify_target[j].str; *r != 0; )
			      *q++ = *r++;
			    made_addition = 1;
			  }

			*q++ = *p++;
		      }

		    spec_machine = new_name;
		  }

	      if (is_modify_target_name)
		break;
#endif

	      n_switches++;

	      if (SWITCH_TAKES_ARG (c) > (p[1] != 0))
		i += SWITCH_TAKES_ARG (c) - (p[1] != 0);
	      else if (WORD_SWITCH_TAKES_ARG (p))
		i += WORD_SWITCH_TAKES_ARG (p);
	    }
	}
      else
	{
	  n_infiles++;
	  lang_n_infiles++;
	}
    }

  if (save_temps_flag && use_pipes)
    {
      /* -save-temps overrides -pipe, so that temp files are produced */
      if (save_temps_flag)
	error ("warning: -pipe ignored because -save-temps specified");
      use_pipes = 0;
    }

  /* Set up the search paths before we go looking for config files.  */

  /* These come before the md prefixes so that we will find gcc's subcommands
     (such as cpp) rather than those of the host system.  */
  /* Use 2 as fourth arg meaning try just the machine as a suffix,
     as well as trying the machine and the version.  */
#ifndef OS2
  add_prefix (&exec_prefixes, standard_libexec_prefix, "GCC",
	      PREFIX_PRIORITY_LAST, 1, 0);
  add_prefix (&exec_prefixes, standard_libexec_prefix, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 2, 0);
  add_prefix (&exec_prefixes, standard_exec_prefix, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 2, 0);
  add_prefix (&exec_prefixes, standard_exec_prefix_1, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 2, 0);
  add_prefix (&exec_prefixes, standard_exec_prefix_2, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 2, 0);
#endif

  add_prefix (&startfile_prefixes, standard_exec_prefix, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 1, 0);
  add_prefix (&startfile_prefixes, standard_exec_prefix_2, "BINUTILS",
	      PREFIX_PRIORITY_LAST, 1, 0);

  tooldir_prefix = concat (tooldir_base_prefix, spec_machine,
			   dir_separator_str, NULL);

  /* If tooldir is relative, base it on exec_prefixes.  A relative
     tooldir lets us move the installed tree as a unit.

     If GCC_EXEC_PREFIX is defined, then we want to add two relative
     directories, so that we can search both the user specified directory
     and the standard place.  */

  if (!IS_ABSOLUTE_PATH (tooldir_prefix))
    {
      if (gcc_exec_prefix)
	{
	  char *gcc_exec_tooldir_prefix
	    = concat (gcc_exec_prefix, spec_machine, dir_separator_str,
		      spec_version, dir_separator_str, tooldir_prefix, NULL);

	  add_prefix (&exec_prefixes,
		      concat (gcc_exec_tooldir_prefix, "bin",
			      dir_separator_str, NULL),
		      NULL, PREFIX_PRIORITY_LAST, 0, 0);
	  add_prefix (&startfile_prefixes,
		      concat (gcc_exec_tooldir_prefix, "lib",
			      dir_separator_str, NULL),
		      NULL, PREFIX_PRIORITY_LAST, 0, 1);
	}

      tooldir_prefix = concat (standard_exec_prefix, spec_machine,
			       dir_separator_str, spec_version,
			       dir_separator_str, tooldir_prefix, NULL);
    }

  add_prefix (&exec_prefixes,
	      concat (tooldir_prefix, "bin", dir_separator_str, NULL),
	      "BINUTILS", PREFIX_PRIORITY_LAST, 0, 0);
  add_prefix (&startfile_prefixes,
	      concat (tooldir_prefix, "lib", dir_separator_str, NULL),
	      "BINUTILS", PREFIX_PRIORITY_LAST, 0, 1);

#if defined(TARGET_SYSTEM_ROOT_RELOCATABLE) && !defined(VMS)
  /* If the normal TARGET_SYSTEM_ROOT is inside of $exec_prefix,
     then consider it to relocate with the rest of the GCC installation
     if GCC_EXEC_PREFIX is set.
     ``make_relative_prefix'' is not compiled for VMS, so don't call it.  */
  if (target_system_root && gcc_exec_prefix)
    {
      char *tmp_prefix = make_relative_prefix (argv[0],
					       standard_bindir_prefix,
					       target_system_root);
      if (tmp_prefix && access_check (tmp_prefix, F_OK) == 0)
	{
	  target_system_root = tmp_prefix;
	  target_system_root_changed = 1;
	}
    }
#endif

  /* More prefixes are enabled in main, after we read the specs file
     and determine whether this is cross-compilation or not.  */

  /* Then create the space for the vectors and scan again.  */

  switches = XNEWVEC (struct switchstr, n_switches + 1);
  infiles = XNEWVEC (struct infile, n_infiles + 1);
  n_switches = 0;
  n_infiles = 0;
  last_language_n_infiles = -1;

  /* This, time, copy the text of each switch and store a pointer
     to the copy in the vector of switches.
     Store all the infiles in their vector.  */

  for (i = 1; i < argc; i++)
    {
      /* Just skip the switches that were handled by the preceding loop.  */
#ifdef MODIFY_TARGET_NAME
      is_modify_target_name = 0;

      for (j = 0; j < ARRAY_SIZE (modify_target); j++)
	if (! strcmp (argv[i], modify_target[j].sw))
	  is_modify_target_name = 1;

      if (is_modify_target_name)
	;
      else
#endif
      if (! strncmp (argv[i], "-Wa,", 4))
	;
      else if (! strncmp (argv[i], "-Wp,", 4))
	;
      else if (! strcmp (argv[i], "-pass-exit-codes"))
	;
      else if (! strcmp (argv[i], "-print-search-dirs"))
	;
      else if (! strcmp (argv[i], "-print-libgcc-file-name"))
	;
      else if (! strncmp (argv[i], "-print-file-name=", 17))
	;
      else if (! strncmp (argv[i], "-print-prog-name=", 17))
	;
      else if (! strcmp (argv[i], "-print-multi-lib"))
	;
      else if (! strcmp (argv[i], "-print-multi-directory"))
	;
      else if (! strcmp (argv[i], "-print-multi-os-directory"))
	;
      else if (! strcmp (argv[i], "-ftarget-help"))
	;
      else if (! strcmp (argv[i], "-fhelp"))
	;
      else if (! strncmp (argv[i], "--sysroot=", strlen ("--sysroot=")))
	{
	  target_system_root = argv[i] + strlen ("--sysroot=");
	  target_system_root_changed = 1;
	}
      else if (argv[i][0] == '+' && argv[i][1] == 'e')
	{
	  /* Compensate for the +e options to the C++ front-end;
	     they're there simply for cfront call-compatibility.  We do
	     some magic in default_compilers to pass them down properly.
	     Note we deliberately start at the `+' here, to avoid passing
	     -e0 or -e1 down into the linker.  */
	  switches[n_switches].part1 = &argv[i][0];
	  switches[n_switches].args = 0;
	  switches[n_switches].live_cond = SWITCH_OK;
	  switches[n_switches].validated = 0;
	  n_switches++;
	}
      else if (strncmp (argv[i], "-Wl,", 4) == 0)
	{
	  int prev, j;
	  /* Split the argument at commas.  */
	  prev = 4;
	  for (j = 4; argv[i][j]; j++)
	    if (argv[i][j] == ',')
	      {
		infiles[n_infiles].language = "*";
		infiles[n_infiles++].name
		  = save_string (argv[i] + prev, j - prev);
		prev = j + 1;
	      }
	  /* Record the part after the last comma.  */
	  infiles[n_infiles].language = "*";
	  infiles[n_infiles++].name = argv[i] + prev;
	}
      else if (strcmp (argv[i], "-Xlinker") == 0)
	{
	  infiles[n_infiles].language = "*";
	  infiles[n_infiles++].name = argv[++i];
	}
      /* Xassembler and Xpreprocessor were already handled in the first argv
	 scan, so all we need to do here is ignore them and their argument.  */
      else if (strcmp (argv[i], "-Xassembler") == 0)
	i++;
      else if (strcmp (argv[i], "-Xpreprocessor") == 0)
	i++;
      else if (strcmp (argv[i], "-l") == 0)
	{ /* POSIX allows separation of -l and the lib arg;
	     canonicalize by concatenating -l with its arg */
	  infiles[n_infiles].language = "*";
	  infiles[n_infiles++].name = concat ("-l", argv[++i], NULL);
	}
      else if (strncmp (argv[i], "-l", 2) == 0)
	{
	  infiles[n_infiles].language = "*";
	  infiles[n_infiles++].name = argv[i];
	}
      else if (strcmp (argv[i], "-specs") == 0)
	i++;
      else if (strncmp (argv[i], "-specs=", 7) == 0)
	;
      else if (strcmp (argv[i], "-time") == 0)
	;
      else if (strcmp (argv[i], "-###") == 0)
	;
      else if (argv[i][0] == '-' && argv[i][1] != 0)
	{
	  const char *p = &argv[i][1];
	  int c = *p;

	  if (c == 'x')
	    {
	      if (p[1] == 0 && i + 1 == argc)
		fatal ("argument to '-x' is missing");
	      if (p[1] == 0)
		spec_lang = argv[++i];
	      else
		spec_lang = p + 1;
	      if (! strcmp (spec_lang, "none"))
		/* Suppress the warning if -xnone comes after the last input
		   file, because alternate command interfaces like g++ might
		   find it useful to place -xnone after each input file.  */
		spec_lang = 0;
	      else
		last_language_n_infiles = n_infiles;
	      continue;
	    }
	  switches[n_switches].part1 = p;
	  /* Deal with option arguments in separate argv elements.  */
	  if ((SWITCH_TAKES_ARG (c) > (p[1] != 0))
	      || WORD_SWITCH_TAKES_ARG (p))
	    {
	      int j = 0;
	      int n_args = WORD_SWITCH_TAKES_ARG (p);

	      if (n_args == 0)
		{
		  /* Count only the option arguments in separate argv elements.  */
		  n_args = SWITCH_TAKES_ARG (c) - (p[1] != 0);
		}
	      if (i + n_args >= argc)
		fatal ("argument to '-%s' is missing", p);
	      switches[n_switches].args
		= XNEWVEC (const char *, n_args + 1);
	      while (j < n_args)
		switches[n_switches].args[j++] = argv[++i];
	      /* Null-terminate the vector.  */
	      switches[n_switches].args[j] = 0;
	    }
	  else if (strchr (switches_need_spaces, c))
	    {
	      /* On some systems, ld cannot handle some options without
		 a space.  So split the option from its argument.  */
	      char *part1 = XNEWVEC (char, 2);
	      part1[0] = c;
	      part1[1] = '\0';

	      switches[n_switches].part1 = part1;
	      switches[n_switches].args = XNEWVEC (const char *, 2);
	      switches[n_switches].args[0] = xstrdup (p+1);
	      switches[n_switches].args[1] = 0;
	    }
	  else
	    switches[n_switches].args = 0;

	  switches[n_switches].live_cond = SWITCH_OK;
	  switches[n_switches].validated = 0;
	  switches[n_switches].ordering = 0;
	  /* These are always valid, since gcc.c itself understands them.  */
	  if (!strcmp (p, "save-temps")
	      || !strcmp (p, "static-libgcc")
	      || !strcmp (p, "shared-libgcc")
	      || !strcmp (p, "pipe"))
	    switches[n_switches].validated = 1;
	  else
	    {
	      char ch = switches[n_switches].part1[0];
	      if (ch == 'B')
		switches[n_switches].validated = 1;
	    }
	  n_switches++;
	}
      else
	{
#ifdef HAVE_TARGET_OBJECT_SUFFIX
	  argv[i] = convert_filename (argv[i], 0, access (argv[i], F_OK));
#endif

	  if (strcmp (argv[i], "-") != 0 && access (argv[i], F_OK) < 0)
	    {
	      perror_with_name (argv[i]);
	      error_count++;
	    }
	  else
	    {
	      infiles[n_infiles].language = spec_lang;
	      infiles[n_infiles++].name = argv[i];
	    }
	}
    }

  if (n_infiles == last_language_n_infiles && spec_lang != 0)
    error ("warning: '-x %s' after last input file has no effect", spec_lang);

  /* Ensure we only invoke each subprocess once.  */
  if (target_help_flag || print_help_list)
    {
      n_infiles = 1;

      /* Create a dummy input file, so that we can pass --target-help on to
	 the various sub-processes.  */
      infiles[0].language = "c";
      infiles[0].name   = "help-dummy";

      if (target_help_flag)
	{
	  switches[n_switches].part1     = "--target-help";
	  switches[n_switches].args      = 0;
	  switches[n_switches].live_cond = SWITCH_OK;
	  switches[n_switches].validated = 0;

	  n_switches++;
	}

      if (print_help_list)
	{
	  switches[n_switches].part1     = "--help";
	  switches[n_switches].args      = 0;
	  switches[n_switches].live_cond = SWITCH_OK;
	  switches[n_switches].validated = 0;

	  n_switches++;
	}
    }

  switches[n_switches].part1 = 0;
  infiles[n_infiles].name = 0;
}

/* Store switches not filtered out by %<S in spec in COLLECT_GCC_OPTIONS
   and place that in the environment.  */

static void
set_collect_gcc_options (void)
{
  int i;
  int first_time;

  /* Build COLLECT_GCC_OPTIONS to have all of the options specified to
     the compiler.  */
  obstack_grow (&collect_obstack, "COLLECT_GCC_OPTIONS=",
		sizeof ("COLLECT_GCC_OPTIONS=") - 1);

  first_time = TRUE;
  for (i = 0; (int) i < n_switches; i++)
    {
      const char *const *args;
      const char *p, *q;
      if (!first_time)
	obstack_grow (&collect_obstack, " ", 1);

      first_time = FALSE;

      /* Ignore elided switches.  */
      if (switches[i].live_cond == SWITCH_IGNORE)
	continue;

      obstack_grow (&collect_obstack, "'-", 2);
      q = switches[i].part1;
      while ((p = strchr (q, '\'')))
	{
	  obstack_grow (&collect_obstack, q, p - q);
	  obstack_grow (&collect_obstack, "'\\''", 4);
	  q = ++p;
	}
      obstack_grow (&collect_obstack, q, strlen (q));
      obstack_grow (&collect_obstack, "'", 1);

      for (args = switches[i].args; args && *args; args++)
	{
	  obstack_grow (&collect_obstack, " '", 2);
	  q = *args;
	  while ((p = strchr (q, '\'')))
	    {
	      obstack_grow (&collect_obstack, q, p - q);
	      obstack_grow (&collect_obstack, "'\\''", 4);
	      q = ++p;
	    }
	  obstack_grow (&collect_obstack, q, strlen (q));
	  obstack_grow (&collect_obstack, "'", 1);
	}
    }
  obstack_grow (&collect_obstack, "\0", 1);
  putenv (XOBFINISH (&collect_obstack, char *));
}

/* Process a spec string, accumulating and running commands.  */

/* These variables describe the input file name.
   input_file_number is the index on outfiles of this file,
   so that the output file name can be stored for later use by %o.
   input_basename is the start of the part of the input file
   sans all directory names, and basename_length is the number
   of characters starting there excluding the suffix .c or whatever.  */

static const char *input_filename;
static int input_file_number;
size_t input_filename_length;
static int basename_length;
static int suffixed_basename_length;
static const char *input_basename;
static const char *input_suffix;
#ifndef HOST_LACKS_INODE_NUMBERS
static struct stat input_stat;
#endif
static int input_stat_set;

/* The compiler used to process the current input file.  */
static struct compiler *input_file_compiler;

/* These are variables used within do_spec and do_spec_1.  */

/* Nonzero if an arg has been started and not yet terminated
   (with space, tab or newline).  */
static int arg_going;

/* Nonzero means %d or %g has been seen; the next arg to be terminated
   is a temporary file name.  */
static int delete_this_arg;

/* Nonzero means %w has been seen; the next arg to be terminated
   is the output file name of this compilation.  */
static int this_is_output_file;

/* Nonzero means %s has been seen; the next arg to be terminated
   is the name of a library file and we should try the standard
   search dirs for it.  */
static int this_is_library_file;

/* Nonzero means that the input of this command is coming from a pipe.  */
static int input_from_pipe;

/* Nonnull means substitute this for any suffix when outputting a switches
   arguments.  */
static const char *suffix_subst;

/* Process the spec SPEC and run the commands specified therein.
   Returns 0 if the spec is successfully processed; -1 if failed.  */

int
do_spec (const char *spec)
{
  int value;

  value = do_spec_2 (spec);

  /* Force out any unfinished command.
     If -pipe, this forces out the last command if it ended in `|'.  */
  if (value == 0)
    {
      if (argbuf_index > 0 && !strcmp (argbuf[argbuf_index - 1], "|"))
	argbuf_index--;

      set_collect_gcc_options ();

      if (argbuf_index > 0)
	value = execute ();
    }

  return value;
}

static int
do_spec_2 (const char *spec)
{
  const char *string;
  int result;

  clear_args ();
  arg_going = 0;
  delete_this_arg = 0;
  this_is_output_file = 0;
  this_is_library_file = 0;
  input_from_pipe = 0;
  suffix_subst = NULL;

  result = do_spec_1 (spec, 0, NULL);

  /* End any pending argument.  */
  if (arg_going)
    {
      obstack_1grow (&obstack, 0);
      string = XOBFINISH (&obstack, const char *);
      if (this_is_library_file)
	string = find_file (string);
      store_arg (string, delete_this_arg, this_is_output_file);
      if (this_is_output_file)
	outfiles[input_file_number] = string;
      arg_going = 0;
    }

  return result;
}


/* Process the given spec string and add any new options to the end
   of the switches/n_switches array.  */

static void
do_option_spec (const char *name, const char *spec)
{
  unsigned int i, value_count, value_len;
  const char *p, *q, *value;
  char *tmp_spec, *tmp_spec_p;

  if (configure_default_options[0].name == NULL)
    return;

  for (i = 0; i < ARRAY_SIZE (configure_default_options); i++)
    if (strcmp (configure_default_options[i].name, name) == 0)
      break;
  if (i == ARRAY_SIZE (configure_default_options))
    return;

  value = configure_default_options[i].value;
  value_len = strlen (value);

  /* Compute the size of the final spec.  */
  value_count = 0;
  p = spec;
  while ((p = strstr (p, "%(VALUE)")) != NULL)
    {
      p ++;
      value_count ++;
    }

  /* Replace each %(VALUE) by the specified value.  */
  tmp_spec = alloca (strlen (spec) + 1
		     + value_count * (value_len - strlen ("%(VALUE)")));
  tmp_spec_p = tmp_spec;
  q = spec;
  while ((p = strstr (q, "%(VALUE)")) != NULL)
    {
      memcpy (tmp_spec_p, q, p - q);
      tmp_spec_p = tmp_spec_p + (p - q);
      memcpy (tmp_spec_p, value, value_len);
      tmp_spec_p += value_len;
      q = p + strlen ("%(VALUE)");
    }
  strcpy (tmp_spec_p, q);

  do_self_spec (tmp_spec);
}

/* Process the given spec string and add any new options to the end
   of the switches/n_switches array.  */

static void
do_self_spec (const char *spec)
{
  do_spec_2 (spec);
  do_spec_1 (" ", 0, NULL);

  if (argbuf_index > 0)
    {
      int i, first;

      first = n_switches;
      n_switches += argbuf_index;
      switches = xrealloc (switches,
			   sizeof (struct switchstr) * (n_switches + 1));

      switches[n_switches] = switches[first];
      for (i = 0; i < argbuf_index; i++)
	{
	  struct switchstr *sw;

	  /* Each switch should start with '-'.  */
	  if (argbuf[i][0] != '-')
	    fatal ("switch '%s' does not start with '-'", argbuf[i]);

	  sw = &switches[i + first];
	  sw->part1 = &argbuf[i][1];
	  sw->args = 0;
	  sw->live_cond = SWITCH_OK;
	  sw->validated = 0;
	  sw->ordering = 0;
	}
    }
}

/* Callback for processing %D and %I specs.  */

struct spec_path_info {
  const char *option;
  const char *append;
  size_t append_len;
  bool omit_relative;
  bool separate_options;
};

static void *
spec_path (char *path, void *data)
{
  struct spec_path_info *info = data;
  size_t len = 0;
  char save = 0;

  if (info->omit_relative && !IS_ABSOLUTE_PATH (path))
    return NULL;

  if (info->append_len != 0)
    {
      len = strlen (path);
      memcpy (path + len, info->append, info->append_len + 1);
    }

  if (!is_directory (path, true))
    return NULL;

  do_spec_1 (info->option, 1, NULL);
  if (info->separate_options)
    do_spec_1 (" ", 0, NULL);

  if (info->append_len == 0)
    {
      len = strlen (path);
      save = path[len - 1];
      if (IS_DIR_SEPARATOR (path[len - 1]))
	path[len - 1] = '\0';
    }

  do_spec_1 (path, 1, NULL);
  do_spec_1 (" ", 0, NULL);

  /* Must not damage the original path.  */
  if (info->append_len == 0)
    path[len - 1] = save;

  return NULL;
}

/* Process the sub-spec SPEC as a portion of a larger spec.
   This is like processing a whole spec except that we do
   not initialize at the beginning and we do not supply a
   newline by default at the end.
   INSWITCH nonzero means don't process %-sequences in SPEC;
   in this case, % is treated as an ordinary character.
   This is used while substituting switches.
   INSWITCH nonzero also causes SPC not to terminate an argument.

   Value is zero unless a line was finished
   and the command on that line reported an error.  */

static int
do_spec_1 (const char *spec, int inswitch, const char *soft_matched_part)
{
  const char *p = spec;
  int c;
  int i;
  const char *string;
  int value;

  while ((c = *p++))
    /* If substituting a switch, treat all chars like letters.
       Otherwise, NL, SPC, TAB and % are special.  */
    switch (inswitch ? 'a' : c)
      {
      case '\n':
	/* End of line: finish any pending argument,
	   then run the pending command if one has been started.  */
	if (arg_going)
	  {
	    obstack_1grow (&obstack, 0);
	    string = XOBFINISH (&obstack, const char *);
	    if (this_is_library_file)
	      string = find_file (string);
	    store_arg (string, delete_this_arg, this_is_output_file);
	    if (this_is_output_file)
	      outfiles[input_file_number] = string;
	  }
	arg_going = 0;

	if (argbuf_index > 0 && !strcmp (argbuf[argbuf_index - 1], "|"))
	  {
	    /* A `|' before the newline means use a pipe here,
	       but only if -pipe was specified.
	       Otherwise, execute now and don't pass the `|' as an arg.  */
	    if (use_pipes)
	      {
		input_from_pipe = 1;
		break;
	      }
	    else
	      argbuf_index--;
	  }

	set_collect_gcc_options ();

	if (argbuf_index > 0)
	  {
	    value = execute ();
	    if (value)
	      return value;
	  }
	/* Reinitialize for a new command, and for a new argument.  */
	clear_args ();
	arg_going = 0;
	delete_this_arg = 0;
	this_is_output_file = 0;
	this_is_library_file = 0;
	input_from_pipe = 0;
	break;

      case '|':
	/* End any pending argument.  */
	if (arg_going)
	  {
	    obstack_1grow (&obstack, 0);
	    string = XOBFINISH (&obstack, const char *);
	    if (this_is_library_file)
	      string = find_file (string);
	    store_arg (string, delete_this_arg, this_is_output_file);
	    if (this_is_output_file)
	      outfiles[input_file_number] = string;
	  }

	/* Use pipe */
	obstack_1grow (&obstack, c);
	arg_going = 1;
	break;

      case '\t':
      case ' ':
	/* Space or tab ends an argument if one is pending.  */
	if (arg_going)
	  {
	    obstack_1grow (&obstack, 0);
	    string = XOBFINISH (&obstack, const char *);
	    if (this_is_library_file)
	      string = find_file (string);
	    store_arg (string, delete_this_arg, this_is_output_file);
	    if (this_is_output_file)
	      outfiles[input_file_number] = string;
	  }
	/* Reinitialize for a new argument.  */
	arg_going = 0;
	delete_this_arg = 0;
	this_is_output_file = 0;
	this_is_library_file = 0;
	break;

      case '%':
	switch (c = *p++)
	  {
	  case 0:
	    fatal ("spec '%s' invalid", spec);

	  case 'b':
	    obstack_grow (&obstack, input_basename, basename_length);
	    arg_going = 1;
	    break;

	  case 'B':
	    obstack_grow (&obstack, input_basename, suffixed_basename_length);
	    arg_going = 1;
	    break;

	  case 'd':
	    delete_this_arg = 2;
	    break;

	  /* Dump out the directories specified with LIBRARY_PATH,
	     followed by the absolute directories
	     that we search for startfiles.  */
	  case 'D':
	    {
	      struct spec_path_info info;

	      info.option = "-L";
	      info.append_len = 0;
#ifdef RELATIVE_PREFIX_NOT_LINKDIR
	      /* Used on systems which record the specified -L dirs
		 and use them to search for dynamic linking.
		 Relative directories always come from -B,
		 and it is better not to use them for searching
		 at run time.  In particular, stage1 loses.  */
	      info.omit_relative = true;
#else
	      info.omit_relative = false;
#endif
	      info.separate_options = false;

	      for_each_path (&startfile_prefixes, true, 0, spec_path, &info);
	    }
	    break;

	  case 'e':
	    /* %efoo means report an error with `foo' as error message
	       and don't execute any more commands for this file.  */
	    {
	      const char *q = p;
	      char *buf;
	      while (*p != 0 && *p != '\n')
		p++;
	      buf = alloca (p - q + 1);
	      strncpy (buf, q, p - q);
	      buf[p - q] = 0;
	      error ("%s", buf);
	      return -1;
	    }
	    break;
	  case 'n':
	    /* %nfoo means report a notice with `foo' on stderr.  */
	    {
	      const char *q = p;
	      char *buf;
	      while (*p != 0 && *p != '\n')
		p++;
	      buf = alloca (p - q + 1);
	      strncpy (buf, q, p - q);
	      buf[p - q] = 0;
	      notice ("%s\n", buf);
	      if (*p)
		p++;
	    }
	    break;

	  case 'j':
	    {
	      struct stat st;

	      /* If save_temps_flag is off, and the HOST_BIT_BUCKET is
		 defined, and it is not a directory, and it is
		 writable, use it.  Otherwise, treat this like any
		 other temporary file.  */

	      if ((!save_temps_flag)
		  && (stat (HOST_BIT_BUCKET, &st) == 0) && (!S_ISDIR (st.st_mode))
		  && (access (HOST_BIT_BUCKET, W_OK) == 0))
		{
		  obstack_grow (&obstack, HOST_BIT_BUCKET,
				strlen (HOST_BIT_BUCKET));
		  delete_this_arg = 0;
		  arg_going = 1;
		  break;
		}
	    }
	    goto create_temp_file;
	  case '|':
	    if (use_pipes)
	      {
		obstack_1grow (&obstack, '-');
		delete_this_arg = 0;
		arg_going = 1;

		/* consume suffix */
		while (*p == '.' || ISALNUM ((unsigned char) *p))
		  p++;
		if (p[0] == '%' && p[1] == 'O')
		  p += 2;

		break;
	      }
	    goto create_temp_file;
	  case 'm':
	    if (use_pipes)
	      {
		/* consume suffix */
		while (*p == '.' || ISALNUM ((unsigned char) *p))
		  p++;
		if (p[0] == '%' && p[1] == 'O')
		  p += 2;

		break;
	      }
	    goto create_temp_file;
	  case 'g':
	  case 'u':
	  case 'U':
	  create_temp_file:
	      {
		struct temp_name *t;
		int suffix_length;
		const char *suffix = p;
		char *saved_suffix = NULL;

		while (*p == '.' || ISALNUM ((unsigned char) *p))
		  p++;
		suffix_length = p - suffix;
		if (p[0] == '%' && p[1] == 'O')
		  {
		    p += 2;
		    /* We don't support extra suffix characters after %O.  */
		    if (*p == '.' || ISALNUM ((unsigned char) *p))
		      fatal ("spec '%s' has invalid '%%0%c'", spec, *p);
		    if (suffix_length == 0)
		      suffix = TARGET_OBJECT_SUFFIX;
		    else
		      {
			saved_suffix
			  = XNEWVEC (char, suffix_length
				     + strlen (TARGET_OBJECT_SUFFIX));
			strncpy (saved_suffix, suffix, suffix_length);
			strcpy (saved_suffix + suffix_length,
				TARGET_OBJECT_SUFFIX);
		      }
		    suffix_length += strlen (TARGET_OBJECT_SUFFIX);
		  }

		/* If the input_filename has the same suffix specified
		   for the %g, %u, or %U, and -save-temps is specified,
		   we could end up using that file as an intermediate
		   thus clobbering the user's source file (.e.g.,
		   gcc -save-temps foo.s would clobber foo.s with the
		   output of cpp0).  So check for this condition and
		   generate a temp file as the intermediate.  */

		if (save_temps_flag)
		  {
		    temp_filename_length = basename_length + suffix_length;
		    temp_filename = alloca (temp_filename_length + 1);
		    strncpy ((char *) temp_filename, input_basename, basename_length);
		    strncpy ((char *) temp_filename + basename_length, suffix,
			     suffix_length);
		    *((char *) temp_filename + temp_filename_length) = '\0';
		    if (strcmp (temp_filename, input_filename) != 0)
		      {
#ifndef HOST_LACKS_INODE_NUMBERS
			struct stat st_temp;

			/* Note, set_input() resets input_stat_set to 0.  */
			if (input_stat_set == 0)
			  {
			    input_stat_set = stat (input_filename, &input_stat);
			    if (input_stat_set >= 0)
			      input_stat_set = 1;
			  }

			/* If we have the stat for the input_filename
			   and we can do the stat for the temp_filename
			   then the they could still refer to the same
			   file if st_dev/st_ino's are the same.  */
			if (input_stat_set != 1
			    || stat (temp_filename, &st_temp) < 0
			    || input_stat.st_dev != st_temp.st_dev
			    || input_stat.st_ino != st_temp.st_ino)
#else
			/* Just compare canonical pathnames.  */
			char* input_realname = lrealpath (input_filename);
			char* temp_realname = lrealpath (temp_filename);
			bool files_differ = strcmp (input_realname, temp_realname);
			free (input_realname);
			free (temp_realname);
			if (files_differ)
#endif
			  {
			    temp_filename = save_string (temp_filename,
							 temp_filename_length + 1);
			    obstack_grow (&obstack, temp_filename,
						    temp_filename_length);
			    arg_going = 1;
			    delete_this_arg = 0;
			    break;
			  }
		      }
		  }

		/* See if we already have an association of %g/%u/%U and
		   suffix.  */
		for (t = temp_names; t; t = t->next)
		  if (t->length == suffix_length
		      && strncmp (t->suffix, suffix, suffix_length) == 0
		      && t->unique == (c == 'u' || c == 'U' || c == 'j'))
		    break;

		/* Make a new association if needed.  %u and %j
		   require one.  */
		if (t == 0 || c == 'u' || c == 'j')
		  {
		    if (t == 0)
		      {
			t = xmalloc (sizeof (struct temp_name));
			t->next = temp_names;
			temp_names = t;
		      }
		    t->length = suffix_length;
		    if (saved_suffix)
		      {
			t->suffix = saved_suffix;
			saved_suffix = NULL;
		      }
		    else
		      t->suffix = save_string (suffix, suffix_length);
		    t->unique = (c == 'u' || c == 'U' || c == 'j');
		    temp_filename = make_temp_file (t->suffix);
		    temp_filename_length = strlen (temp_filename);
		    t->filename = temp_filename;
		    t->filename_length = temp_filename_length;
		  }

		if (saved_suffix)
		  free (saved_suffix);

		obstack_grow (&obstack, t->filename, t->filename_length);
		delete_this_arg = 1;
	      }
	    arg_going = 1;
	    break;

	  case 'i':
	    if (combine_inputs)
	      {
		for (i = 0; (int) i < n_infiles; i++)
		  if ((!infiles[i].language) || (infiles[i].language[0] != '*'))
		    if (infiles[i].incompiler == input_file_compiler)
		      {
			store_arg (infiles[i].name, 0, 0);
			infiles[i].compiled = true;
		      }
	      }
	    else
	      {
		obstack_grow (&obstack, input_filename, input_filename_length);
		arg_going = 1;
	      }
	    break;

	  case 'I':
	    {
	      struct spec_path_info info;

	      if (multilib_dir)
		{
		  do_spec_1 ("-imultilib", 1, NULL);
		  /* Make this a separate argument.  */
		  do_spec_1 (" ", 0, NULL);
		  do_spec_1 (multilib_dir, 1, NULL);
		  do_spec_1 (" ", 0, NULL);
		}

	      if (gcc_exec_prefix)
		{
		  do_spec_1 ("-iprefix", 1, NULL);
		  /* Make this a separate argument.  */
		  do_spec_1 (" ", 0, NULL);
		  do_spec_1 (gcc_exec_prefix, 1, NULL);
		  do_spec_1 (" ", 0, NULL);
		}

	      if (target_system_root_changed ||
		  (target_system_root && target_sysroot_hdrs_suffix))
		{
		  do_spec_1 ("-isysroot", 1, NULL);
		  /* Make this a separate argument.  */
		  do_spec_1 (" ", 0, NULL);
		  do_spec_1 (target_system_root, 1, NULL);
		  if (target_sysroot_hdrs_suffix)
		    do_spec_1 (target_sysroot_hdrs_suffix, 1, NULL);
		  do_spec_1 (" ", 0, NULL);
		}

	      info.option = "-isystem";
	      info.append = "include";
	      info.append_len = strlen (info.append);
	      info.omit_relative = false;
	      info.separate_options = true;

	      for_each_path (&include_prefixes, false, info.append_len,
			     spec_path, &info);
	    }
	    break;

	  case 'o':
	    {
	      int max = n_infiles;
	      max += lang_specific_extra_outfiles;

	      for (i = 0; i < max; i++)
		if (outfiles[i])
		  store_arg (outfiles[i], 0, 0);
	      break;
	    }

	  case 'O':
	    obstack_grow (&obstack, TARGET_OBJECT_SUFFIX, strlen (TARGET_OBJECT_SUFFIX));
	    arg_going = 1;
	    break;

	  case 's':
	    this_is_library_file = 1;
	    break;

	  case 'V':
	    outfiles[input_file_number] = NULL;
	    break;

	  case 'w':
	    this_is_output_file = 1;
	    break;

	  case 'W':
	    {
	      int cur_index = argbuf_index;
	      /* Handle the {...} following the %W.  */
	      if (*p != '{')
		fatal ("spec '%s' has invalid '%%W%c", spec, *p);
	      p = handle_braces (p + 1);
	      if (p == 0)
		return -1;
	      /* End any pending argument.  */
	      if (arg_going)
		{
		  obstack_1grow (&obstack, 0);
		  string = XOBFINISH (&obstack, const char *);
		  if (this_is_library_file)
		    string = find_file (string);
		  store_arg (string, delete_this_arg, this_is_output_file);
		  if (this_is_output_file)
		    outfiles[input_file_number] = string;
		  arg_going = 0;
		}
	      /* If any args were output, mark the last one for deletion
		 on failure.  */
	      if (argbuf_index != cur_index)
		record_temp_file (argbuf[argbuf_index - 1], 0, 1);
	      break;
	    }

	  /* %x{OPTION} records OPTION for %X to output.  */
	  case 'x':
	    {
	      const char *p1 = p;
	      char *string;

	      /* Skip past the option value and make a copy.  */
	      if (*p != '{')
		fatal ("spec '%s' has invalid '%%x%c'", spec, *p);
	      while (*p++ != '}')
		;
	      string = save_string (p1 + 1, p - p1 - 2);

	      /* See if we already recorded this option.  */
	      for (i = 0; i < n_linker_options; i++)
		if (! strcmp (string, linker_options[i]))
		  {
		    free (string);
		    return 0;
		  }

	      /* This option is new; add it.  */
	      add_linker_option (string, strlen (string));
	    }
	    break;

	  /* Dump out the options accumulated previously using %x.  */
	  case 'X':
	    for (i = 0; i < n_linker_options; i++)
	      {
		do_spec_1 (linker_options[i], 1, NULL);
		/* Make each accumulated option a separate argument.  */
		do_spec_1 (" ", 0, NULL);
	      }
	    break;

	  /* Dump out the options accumulated previously using -Wa,.  */
	  case 'Y':
	    for (i = 0; i < n_assembler_options; i++)
	      {
		do_spec_1 (assembler_options[i], 1, NULL);
		/* Make each accumulated option a separate argument.  */
		do_spec_1 (" ", 0, NULL);
	      }
	    break;

	  /* Dump out the options accumulated previously using -Wp,.  */
	  case 'Z':
	    for (i = 0; i < n_preprocessor_options; i++)
	      {
		do_spec_1 (preprocessor_options[i], 1, NULL);
		/* Make each accumulated option a separate argument.  */
		do_spec_1 (" ", 0, NULL);
	      }
	    break;

	    /* Here are digits and numbers that just process
	       a certain constant string as a spec.  */

	  case '1':
	    value = do_spec_1 (cc1_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case '2':
	    value = do_spec_1 (cc1plus_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'a':
	    value = do_spec_1 (asm_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'A':
	    value = do_spec_1 (asm_final_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'C':
	    {
	      const char *const spec
		= (input_file_compiler->cpp_spec
		   ? input_file_compiler->cpp_spec
		   : cpp_spec);
	      value = do_spec_1 (spec, 0, NULL);
	      if (value != 0)
		return value;
	    }
	    break;

	  case 'E':
	    value = do_spec_1 (endfile_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'l':
	    value = do_spec_1 (link_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'L':
	    value = do_spec_1 (lib_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'G':
	    value = do_spec_1 (libgcc_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	  case 'R':
	    /* We assume there is a directory
	       separator at the end of this string.  */
	    if (target_system_root)
	      {
	        obstack_grow (&obstack, target_system_root,
			      strlen (target_system_root));
		if (target_sysroot_suffix)
		  obstack_grow (&obstack, target_sysroot_suffix,
				strlen (target_sysroot_suffix));
	      }
	    break;

	  case 'S':
	    value = do_spec_1 (startfile_spec, 0, NULL);
	    if (value != 0)
	      return value;
	    break;

	    /* Here we define characters other than letters and digits.  */

	  case '{':
	    p = handle_braces (p);
	    if (p == 0)
	      return -1;
	    break;

	  case ':':
	    p = handle_spec_function (p);
	    if (p == 0)
	      return -1;
	    break;

	  case '%':
	    obstack_1grow (&obstack, '%');
	    break;

	  case '.':
	    {
	      unsigned len = 0;

	      while (p[len] && p[len] != ' ' && p[len] != '%')
		len++;
	      suffix_subst = save_string (p - 1, len + 1);
	      p += len;
	    }
	   break;

	   /* Henceforth ignore the option(s) matching the pattern
	      after the %<.  */
	  case '<':
	    {
	      unsigned len = 0;
	      int have_wildcard = 0;
	      int i;

	      while (p[len] && p[len] != ' ' && p[len] != '\t')
		len++;

	      if (p[len-1] == '*')
		have_wildcard = 1;

	      for (i = 0; i < n_switches; i++)
		if (!strncmp (switches[i].part1, p, len - have_wildcard)
		    && (have_wildcard || switches[i].part1[len] == '\0'))
		  {
		    switches[i].live_cond = SWITCH_IGNORE;
		    switches[i].validated = 1;
		  }

	      p += len;
	    }
	    break;

	  case '*':
	    if (soft_matched_part)
	      {
		do_spec_1 (soft_matched_part, 1, NULL);
		do_spec_1 (" ", 0, NULL);
	      }
	    else
	      /* Catch the case where a spec string contains something like
		 '%{foo:%*}'.  i.e. there is no * in the pattern on the left
		 hand side of the :.  */
	      error ("spec failure: '%%*' has not been initialized by pattern match");
	    break;

	    /* Process a string found as the value of a spec given by name.
	       This feature allows individual machine descriptions
	       to add and use their own specs.
	       %[...] modifies -D options the way %P does;
	       %(...) uses the spec unmodified.  */
	  case '[':
	    error ("warning: use of obsolete %%[ operator in specs");
	  case '(':
	    {
	      const char *name = p;
	      struct spec_list *sl;
	      int len;

	      /* The string after the S/P is the name of a spec that is to be
		 processed.  */
	      while (*p && *p != ')' && *p != ']')
		p++;

	      /* See if it's in the list.  */
	      for (len = p - name, sl = specs; sl; sl = sl->next)
		if (sl->name_len == len && !strncmp (sl->name, name, len))
		  {
		    name = *(sl->ptr_spec);
#ifdef DEBUG_SPECS
		    notice ("Processing spec %c%s%c, which is '%s'\n",
			    c, sl->name, (c == '(') ? ')' : ']', name);
#endif
		    break;
		  }

	      if (sl)
		{
		  if (c == '(')
		    {
		      value = do_spec_1 (name, 0, NULL);
		      if (value != 0)
			return value;
		    }
		  else
		    {
		      char *x = alloca (strlen (name) * 2 + 1);
		      char *buf = x;
		      const char *y = name;
		      int flag = 0;

		      /* Copy all of NAME into BUF, but put __ after
			 every -D and at the end of each arg.  */
		      while (1)
			{
			  if (! strncmp (y, "-D", 2))
			    {
			      *x++ = '-';
			      *x++ = 'D';
			      *x++ = '_';
			      *x++ = '_';
			      y += 2;
			      flag = 1;
			      continue;
			    }
			  else if (flag
				   && (*y == ' ' || *y == '\t' || *y == '='
				       || *y == '}' || *y == 0))
			    {
			      *x++ = '_';
			      *x++ = '_';
			      flag = 0;
			    }
			  if (*y == 0)
			    break;
			  else
			    *x++ = *y++;
			}
		      *x = 0;

		      value = do_spec_1 (buf, 0, NULL);
		      if (value != 0)
			return value;
		    }
		}

	      /* Discard the closing paren or bracket.  */
	      if (*p)
		p++;
	    }
	    break;

	  default:
	    error ("spec failure: unrecognized spec option '%c'", c);
	    break;
	  }
	break;

      case '\\':
	/* Backslash: treat next character as ordinary.  */
	c = *p++;

	/* Fall through.  */
      default:
	/* Ordinary character: put it into the current argument.  */
	obstack_1grow (&obstack, c);
	arg_going = 1;
      }

  /* End of string.  If we are processing a spec function, we need to
     end any pending argument.  */
  if (processing_spec_function && arg_going)
    {
      obstack_1grow (&obstack, 0);
      string = XOBFINISH (&obstack, const char *);
      if (this_is_library_file)
        string = find_file (string);
      store_arg (string, delete_this_arg, this_is_output_file);
      if (this_is_output_file)
        outfiles[input_file_number] = string;
      arg_going = 0;
    }

  return 0;
}

/* Look up a spec function.  */

static const struct spec_function *
lookup_spec_function (const char *name)
{
  const struct spec_function *sf;

  for (sf = static_spec_functions; sf->name != NULL; sf++)
    if (strcmp (sf->name, name) == 0)
      return sf;

  return NULL;
}

/* Evaluate a spec function.  */

static const char *
eval_spec_function (const char *func, const char *args)
{
  const struct spec_function *sf;
  const char *funcval;

  /* Saved spec processing context.  */
  int save_argbuf_index;
  int save_argbuf_length;
  const char **save_argbuf;

  int save_arg_going;
  int save_delete_this_arg;
  int save_this_is_output_file;
  int save_this_is_library_file;
  int save_input_from_pipe;
  const char *save_suffix_subst;


  sf = lookup_spec_function (func);
  if (sf == NULL)
    fatal ("unknown spec function '%s'", func);

  /* Push the spec processing context.  */
  save_argbuf_index = argbuf_index;
  save_argbuf_length = argbuf_length;
  save_argbuf = argbuf;

  save_arg_going = arg_going;
  save_delete_this_arg = delete_this_arg;
  save_this_is_output_file = this_is_output_file;
  save_this_is_library_file = this_is_library_file;
  save_input_from_pipe = input_from_pipe;
  save_suffix_subst = suffix_subst;

  /* Create a new spec processing context, and build the function
     arguments.  */

  alloc_args ();
  if (do_spec_2 (args) < 0)
    fatal ("error in args to spec function '%s'", func);

  /* argbuf_index is an index for the next argument to be inserted, and
     so contains the count of the args already inserted.  */

  funcval = (*sf->func) (argbuf_index, argbuf);

  /* Pop the spec processing context.  */
  argbuf_index = save_argbuf_index;
  argbuf_length = save_argbuf_length;
  free (argbuf);
  argbuf = save_argbuf;

  arg_going = save_arg_going;
  delete_this_arg = save_delete_this_arg;
  this_is_output_file = save_this_is_output_file;
  this_is_library_file = save_this_is_library_file;
  input_from_pipe = save_input_from_pipe;
  suffix_subst = save_suffix_subst;

  return funcval;
}

/* Handle a spec function call of the form:

   %:function(args)

   ARGS is processed as a spec in a separate context and split into an
   argument vector in the normal fashion.  The function returns a string
   containing a spec which we then process in the caller's context, or
   NULL if no processing is required.  */

static const char *
handle_spec_function (const char *p)
{
  char *func, *args;
  const char *endp, *funcval;
  int count;

  processing_spec_function++;

  /* Get the function name.  */
  for (endp = p; *endp != '\0'; endp++)
    {
      if (*endp == '(')		/* ) */
        break;
      /* Only allow [A-Za-z0-9], -, and _ in function names.  */
      if (!ISALNUM (*endp) && !(*endp == '-' || *endp == '_'))
	fatal ("malformed spec function name");
    }
  if (*endp != '(')		/* ) */
    fatal ("no arguments for spec function");
  func = save_string (p, endp - p);
  p = ++endp;

  /* Get the arguments.  */
  for (count = 0; *endp != '\0'; endp++)
    {
      /* ( */
      if (*endp == ')')
	{
	  if (count == 0)
	    break;
	  count--;
	}
      else if (*endp == '(')	/* ) */
	count++;
    }
  /* ( */
  if (*endp != ')')
    fatal ("malformed spec function arguments");
  args = save_string (p, endp - p);
  p = ++endp;

  /* p now points to just past the end of the spec function expression.  */

  funcval = eval_spec_function (func, args);
  if (funcval != NULL && do_spec_1 (funcval, 0, NULL) < 0)
    p = NULL;

  free (func);
  free (args);

  processing_spec_function--;

  return p;
}

/* Inline subroutine of handle_braces.  Returns true if the current
   input suffix matches the atom bracketed by ATOM and END_ATOM.  */
static inline bool
input_suffix_matches (const char *atom, const char *end_atom)
{
  /* We special case the semantics of {.s:...} and {.S:...} and their
     negative variants.  Instead of testing the input filename suffix,
     we test whether the input source file is an assembler file or an
     assembler-with-cpp file respectively.  This allows us to correctly
     handle the -x command line option.  */

  if (atom + 1 == end_atom
      && input_file_compiler
      && input_file_compiler->suffix)
    {
      if (*atom == 's')
	return !strcmp (input_file_compiler->suffix, "@assembler");
      if (*atom == 'S')
	return !strcmp (input_file_compiler->suffix, "@assembler-with-cpp");
    }

  return (input_suffix
	  && !strncmp (input_suffix, atom, end_atom - atom)
	  && input_suffix[end_atom - atom] == '\0');
}

/* Subroutine of handle_braces.  Returns true if a switch
   matching the atom bracketed by ATOM and END_ATOM appeared on the
   command line.  */
static bool
switch_matches (const char *atom, const char *end_atom, int starred)
{
  int i;
  int len = end_atom - atom;
  int plen = starred ? len : -1;

  for (i = 0; i < n_switches; i++)
    if (!strncmp (switches[i].part1, atom, len)
	&& (starred || switches[i].part1[len] == '\0')
	&& check_live_switch (i, plen))
      return true;

  return false;
}

/* Inline subroutine of handle_braces.  Mark all of the switches which
   match ATOM (extends to END_ATOM; STARRED indicates whether there
   was a star after the atom) for later processing.  */
static inline void
mark_matching_switches (const char *atom, const char *end_atom, int starred)
{
  int i;
  int len = end_atom - atom;
  int plen = starred ? len : -1;

  for (i = 0; i < n_switches; i++)
    if (!strncmp (switches[i].part1, atom, len)
	&& (starred || switches[i].part1[len] == '\0')
	&& check_live_switch (i, plen))
      switches[i].ordering = 1;
}

/* Inline subroutine of handle_braces.  Process all the currently
   marked switches through give_switch, and clear the marks.  */
static inline void
process_marked_switches (void)
{
  int i;

  for (i = 0; i < n_switches; i++)
    if (switches[i].ordering == 1)
      {
	switches[i].ordering = 0;
	give_switch (i, 0);
      }
}

/* Handle a %{ ... } construct.  P points just inside the leading {.
   Returns a pointer one past the end of the brace block, or 0
   if we call do_spec_1 and that returns -1.  */

static const char *
handle_braces (const char *p)
{
  const char *atom, *end_atom;
  const char *d_atom = NULL, *d_end_atom = NULL;
  const char *orig = p;

  bool a_is_suffix;
  bool a_is_starred;
  bool a_is_negated;
  bool a_matched;

  bool a_must_be_last = false;
  bool ordered_set    = false;
  bool disjunct_set   = false;
  bool disj_matched   = false;
  bool disj_starred   = true;
  bool n_way_choice   = false;
  bool n_way_matched  = false;

#define SKIP_WHITE() do { while (*p == ' ' || *p == '\t') p++; } while (0)

  do
    {
      if (a_must_be_last)
	goto invalid;

      /* Scan one "atom" (S in the description above of %{}, possibly
	 with !, ., or * modifiers).  */
      a_matched = a_is_suffix = a_is_starred = a_is_negated = false;

      SKIP_WHITE();
      if (*p == '!')
	p++, a_is_negated = true;

      SKIP_WHITE();
      if (*p == '.')
	p++, a_is_suffix = true;

      atom = p;
      while (ISIDNUM(*p) || *p == '-' || *p == '+' || *p == '='
	     || *p == ',' || *p == '.' || *p == '@')
	p++;
      end_atom = p;

      if (*p == '*')
	p++, a_is_starred = 1;

      SKIP_WHITE();
      switch (*p)
	{
	case '&': case '}':
	  /* Substitute the switch(es) indicated by the current atom.  */
	  ordered_set = true;
	  if (disjunct_set || n_way_choice || a_is_negated || a_is_suffix
	      || atom == end_atom)
	    goto invalid;

	  mark_matching_switches (atom, end_atom, a_is_starred);

	  if (*p == '}')
	    process_marked_switches ();
	  break;

	case '|': case ':':
	  /* Substitute some text if the current atom appears as a switch
	     or suffix.  */
	  disjunct_set = true;
	  if (ordered_set)
	    goto invalid;

	  if (atom == end_atom)
	    {
	      if (!n_way_choice || disj_matched || *p == '|'
		  || a_is_negated || a_is_suffix || a_is_starred)
		goto invalid;

	      /* An empty term may appear as the last choice of an
		 N-way choice set; it means "otherwise".  */
	      a_must_be_last = true;
	      disj_matched = !n_way_matched;
	      disj_starred = false;
	    }
	  else
	    {
	       if (a_is_suffix && a_is_starred)
		 goto invalid;

	       if (!a_is_starred)
		 disj_starred = false;

	       /* Don't bother testing this atom if we already have a
                  match.  */
	       if (!disj_matched && !n_way_matched)
		 {
		   if (a_is_suffix)
		     a_matched = input_suffix_matches (atom, end_atom);
		   else
		     a_matched = switch_matches (atom, end_atom, a_is_starred);

		   if (a_matched != a_is_negated)
		     {
		       disj_matched = true;
		       d_atom = atom;
		       d_end_atom = end_atom;
		     }
		 }
	    }

	  if (*p == ':')
	    {
	      /* Found the body, that is, the text to substitute if the
		 current disjunction matches.  */
	      p = process_brace_body (p + 1, d_atom, d_end_atom, disj_starred,
				      disj_matched && !n_way_matched);
	      if (p == 0)
		return 0;

	      /* If we have an N-way choice, reset state for the next
		 disjunction.  */
	      if (*p == ';')
		{
		  n_way_choice = true;
		  n_way_matched |= disj_matched;
		  disj_matched = false;
		  disj_starred = true;
		  d_atom = d_end_atom = NULL;
		}
	    }
	  break;

	default:
	  goto invalid;
	}
    }
  while (*p++ != '}');

  return p;

 invalid:
  fatal ("braced spec '%s' is invalid at '%c'", orig, *p);

#undef SKIP_WHITE
}

/* Subroutine of handle_braces.  Scan and process a brace substitution body
   (X in the description of %{} syntax).  P points one past the colon;
   ATOM and END_ATOM bracket the first atom which was found to be true
   (present) in the current disjunction; STARRED indicates whether all
   the atoms in the current disjunction were starred (for syntax validation);
   MATCHED indicates whether the disjunction matched or not, and therefore
   whether or not the body is to be processed through do_spec_1 or just
   skipped.  Returns a pointer to the closing } or ;, or 0 if do_spec_1
   returns -1.  */

static const char *
process_brace_body (const char *p, const char *atom, const char *end_atom,
		    int starred, int matched)
{
  const char *body, *end_body;
  unsigned int nesting_level;
  bool have_subst     = false;

  /* Locate the closing } or ;, honoring nested braces.
     Trim trailing whitespace.  */
  body = p;
  nesting_level = 1;
  for (;;)
    {
      if (*p == '{')
	nesting_level++;
      else if (*p == '}')
	{
	  if (!--nesting_level)
	    break;
	}
      else if (*p == ';' && nesting_level == 1)
	break;
      else if (*p == '%' && p[1] == '*' && nesting_level == 1)
	have_subst = true;
      else if (*p == '\0')
	goto invalid;
      p++;
    }

  end_body = p;
  while (end_body[-1] == ' ' || end_body[-1] == '\t')
    end_body--;

  if (have_subst && !starred)
    goto invalid;

  if (matched)
    {
      /* Copy the substitution body to permanent storage and execute it.
	 If have_subst is false, this is a simple matter of running the
	 body through do_spec_1...  */
      char *string = save_string (body, end_body - body);
      if (!have_subst)
	{
	  if (do_spec_1 (string, 0, NULL) < 0)
	    return 0;
	}
      else
	{
	  /* ... but if have_subst is true, we have to process the
	     body once for each matching switch, with %* set to the
	     variant part of the switch.  */
	  unsigned int hard_match_len = end_atom - atom;
	  int i;

	  for (i = 0; i < n_switches; i++)
	    if (!strncmp (switches[i].part1, atom, hard_match_len)
		&& check_live_switch (i, hard_match_len))
	      {
		if (do_spec_1 (string, 0,
			       &switches[i].part1[hard_match_len]) < 0)
		  return 0;
		/* Pass any arguments this switch has.  */
		give_switch (i, 1);
		suffix_subst = NULL;
	      }
	}
    }

  return p;

 invalid:
  fatal ("braced spec body '%s' is invalid", body);
}

/* Return 0 iff switch number SWITCHNUM is obsoleted by a later switch
   on the command line.  PREFIX_LENGTH is the length of XXX in an {XXX*}
   spec, or -1 if either exact match or %* is used.

   A -O switch is obsoleted by a later -O switch.  A -f, -m, or -W switch
   whose value does not begin with "no-" is obsoleted by the same value
   with the "no-", similarly for a switch with the "no-" prefix.  */

static int
check_live_switch (int switchnum, int prefix_length)
{
  const char *name = switches[switchnum].part1;
  int i;

  /* In the common case of {<at-most-one-letter>*}, a negating
     switch would always match, so ignore that case.  We will just
     send the conflicting switches to the compiler phase.  */
  if (prefix_length >= 0 && prefix_length <= 1)
    return 1;

  /* If we already processed this switch and determined if it was
     live or not, return our past determination.  */
  if (switches[switchnum].live_cond != 0)
    return switches[switchnum].live_cond > 0;

  /* Now search for duplicate in a manner that depends on the name.  */
  switch (*name)
    {
    case 'O':
      for (i = switchnum + 1; i < n_switches; i++)
	if (switches[i].part1[0] == 'O')
	  {
	    switches[switchnum].validated = 1;
	    switches[switchnum].live_cond = SWITCH_FALSE;
	    return 0;
	  }
      break;

    case 'W':  case 'f':  case 'm':
      if (! strncmp (name + 1, "no-", 3))
	{
	  /* We have Xno-YYY, search for XYYY.  */
	  for (i = switchnum + 1; i < n_switches; i++)
	    if (switches[i].part1[0] == name[0]
		&& ! strcmp (&switches[i].part1[1], &name[4]))
	      {
		switches[switchnum].validated = 1;
		switches[switchnum].live_cond = SWITCH_FALSE;
		return 0;
	      }
	}
      else
	{
	  /* We have XYYY, search for Xno-YYY.  */
	  for (i = switchnum + 1; i < n_switches; i++)
	    if (switches[i].part1[0] == name[0]
		&& switches[i].part1[1] == 'n'
		&& switches[i].part1[2] == 'o'
		&& switches[i].part1[3] == '-'
		&& !strcmp (&switches[i].part1[4], &name[1]))
	      {
		switches[switchnum].validated = 1;
		switches[switchnum].live_cond = SWITCH_FALSE;
		return 0;
	      }
	}
      break;
    }

  /* Otherwise the switch is live.  */
  switches[switchnum].live_cond = SWITCH_LIVE;
  return 1;
}

/* Pass a switch to the current accumulating command
   in the same form that we received it.
   SWITCHNUM identifies the switch; it is an index into
   the vector of switches gcc received, which is `switches'.
   This cannot fail since it never finishes a command line.

   If OMIT_FIRST_WORD is nonzero, then we omit .part1 of the argument.  */

static void
give_switch (int switchnum, int omit_first_word)
{
  if (switches[switchnum].live_cond == SWITCH_IGNORE)
    return;

  if (!omit_first_word)
    {
      do_spec_1 ("-", 0, NULL);
      do_spec_1 (switches[switchnum].part1, 1, NULL);
    }

  if (switches[switchnum].args != 0)
    {
      const char **p;
      for (p = switches[switchnum].args; *p; p++)
	{
	  const char *arg = *p;

	  do_spec_1 (" ", 0, NULL);
	  if (suffix_subst)
	    {
	      unsigned length = strlen (arg);
	      int dot = 0;

	      while (length-- && !IS_DIR_SEPARATOR (arg[length]))
		if (arg[length] == '.')
		  {
		    ((char *)arg)[length] = 0;
		    dot = 1;
		    break;
		  }
	      do_spec_1 (arg, 1, NULL);
	      if (dot)
		((char *)arg)[length] = '.';
	      do_spec_1 (suffix_subst, 1, NULL);
	    }
	  else
	    do_spec_1 (arg, 1, NULL);
	}
    }

  do_spec_1 (" ", 0, NULL);
  switches[switchnum].validated = 1;
}

/* Search for a file named NAME trying various prefixes including the
   user's -B prefix and some standard ones.
   Return the absolute file name found.  If nothing is found, return NAME.  */

static const char *
find_file (const char *name)
{
  char *newname = find_a_file (&startfile_prefixes, name, R_OK, true);
  return newname ? newname : name;
}

/* Determine whether a directory exists.  If LINKER, return 0 for
   certain fixed names not needed by the linker.  */

static int
is_directory (const char *path1, bool linker)
{
  int len1;
  char *path;
  char *cp;
  struct stat st;

  /* Ensure the string ends with "/.".  The resulting path will be a
     directory even if the given path is a symbolic link.  */
  len1 = strlen (path1);
  path = alloca (3 + len1);
  memcpy (path, path1, len1);
  cp = path + len1;
  if (!IS_DIR_SEPARATOR (cp[-1]))
    *cp++ = DIR_SEPARATOR;
  *cp++ = '.';
  *cp = '\0';

  /* Exclude directories that the linker is known to search.  */
  if (linker
      && IS_DIR_SEPARATOR (path[0])
      && ((cp - path == 6
	   && strncmp (path + 1, "lib", 3) == 0)
	  || (cp - path == 10
	      && strncmp (path + 1, "usr", 3) == 0
	      && IS_DIR_SEPARATOR (path[4])
	      && strncmp (path + 5, "lib", 3) == 0)))
    return 0;

  return (stat (path, &st) >= 0 && S_ISDIR (st.st_mode));
}

/* Set up the various global variables to indicate that we're processing
   the input file named FILENAME.  */

void
set_input (const char *filename)
{
  const char *p;

  input_filename = filename;
  input_filename_length = strlen (input_filename);

  input_basename = input_filename;
#ifdef HAVE_DOS_BASED_FILE_SYSTEM
  /* Skip drive name so 'x:foo' is handled properly.  */
  if (input_basename[1] == ':')
    input_basename += 2;
#endif
  for (p = input_basename; *p; p++)
    if (IS_DIR_SEPARATOR (*p))
      input_basename = p + 1;

  /* Find a suffix starting with the last period,
     and set basename_length to exclude that suffix.  */
  basename_length = strlen (input_basename);
  suffixed_basename_length = basename_length;
  p = input_basename + basename_length;
  while (p != input_basename && *p != '.')
    --p;
  if (*p == '.' && p != input_basename)
    {
      basename_length = p - input_basename;
      input_suffix = p + 1;
    }
  else
    input_suffix = "";

  /* If a spec for 'g', 'u', or 'U' is seen with -save-temps then
     we will need to do a stat on the input_filename.  The
     INPUT_STAT_SET signals that the stat is needed.  */
  input_stat_set = 0;
}

/* On fatal signals, delete all the temporary files.  */

static void
fatal_error (int signum)
{
  signal (signum, SIG_DFL);
  delete_failure_queue ();
  delete_temp_files ();
  /* Get the same signal again, this time not handled,
     so its normal effect occurs.  */
  kill (getpid (), signum);
}

extern int main (int, char **);

int
main (int argc, char **argv)
{
  size_t i;
  int value;
  int linker_was_run = 0;
  int lang_n_infiles = 0;
  int num_linker_inputs = 0;
  char *explicit_link_files;
  char *specs_file;
  const char *p;
  struct user_specs *uptr;

  p = argv[0] + strlen (argv[0]);
  while (p != argv[0] && !IS_DIR_SEPARATOR (p[-1]))
    --p;
  programname = p;

  xmalloc_set_program_name (programname);

  if (pledge ("stdio rpath wpath cpath proc exec", NULL) == -1) {
    error ("pledge: %s", xstrerror (errno));
    exit (1);
  }

  expandargv (&argc, &argv);

  prune_options (&argc, &argv);

#ifdef GCC_DRIVER_HOST_INITIALIZATION
  /* Perform host dependent initialization when needed.  */
  GCC_DRIVER_HOST_INITIALIZATION;
#endif

  /* Unlock the stdio streams.  */
  unlock_std_streams ();

  gcc_init_libintl ();

  if (signal (SIGINT, SIG_IGN) != SIG_IGN)
    signal (SIGINT, fatal_error);
#ifdef SIGHUP
  if (signal (SIGHUP, SIG_IGN) != SIG_IGN)
    signal (SIGHUP, fatal_error);
#endif
  if (signal (SIGTERM, SIG_IGN) != SIG_IGN)
    signal (SIGTERM, fatal_error);
#ifdef SIGPIPE
  if (signal (SIGPIPE, SIG_IGN) != SIG_IGN)
    signal (SIGPIPE, fatal_error);
#endif
#ifdef SIGCHLD
  /* We *MUST* set SIGCHLD to SIG_DFL so that the wait4() call will
     receive the signal.  A different setting is inheritable */
  signal (SIGCHLD, SIG_DFL);
#endif

  /* Allocate the argument vector.  */
  alloc_args ();

  obstack_init (&obstack);

  /* Build multilib_select, et. al from the separate lines that make up each
     multilib selection.  */
  {
    const char *const *q = multilib_raw;
    int need_space;

    obstack_init (&multilib_obstack);
    while ((p = *q++) != (char *) 0)
      obstack_grow (&multilib_obstack, p, strlen (p));

    obstack_1grow (&multilib_obstack, 0);
    multilib_select = XOBFINISH (&multilib_obstack, const char *);

    q = multilib_matches_raw;
    while ((p = *q++) != (char *) 0)
      obstack_grow (&multilib_obstack, p, strlen (p));

    obstack_1grow (&multilib_obstack, 0);
    multilib_matches = XOBFINISH (&multilib_obstack, const char *);

    q = multilib_exclusions_raw;
    while ((p = *q++) != (char *) 0)
      obstack_grow (&multilib_obstack, p, strlen (p));

    obstack_1grow (&multilib_obstack, 0);
    multilib_exclusions = XOBFINISH (&multilib_obstack, const char *);

    need_space = FALSE;
    for (i = 0; i < ARRAY_SIZE (multilib_defaults_raw); i++)
      {
	if (need_space)
	  obstack_1grow (&multilib_obstack, ' ');
	obstack_grow (&multilib_obstack,
		      multilib_defaults_raw[i],
		      strlen (multilib_defaults_raw[i]));
	need_space = TRUE;
      }

    obstack_1grow (&multilib_obstack, 0);
    multilib_defaults = XOBFINISH (&multilib_obstack, const char *);
  }

  /* Set up to remember the pathname of gcc and any options
     needed for collect.  We use argv[0] instead of programname because
     we need the complete pathname.  */
  obstack_init (&collect_obstack);
  obstack_grow (&collect_obstack, "COLLECT_GCC=", sizeof ("COLLECT_GCC=") - 1);
  obstack_grow (&collect_obstack, argv[0], strlen (argv[0]) + 1);
  putenv (XOBFINISH (&collect_obstack, char *));

#ifdef INIT_ENVIRONMENT
  /* Set up any other necessary machine specific environment variables.  */
  putenv (INIT_ENVIRONMENT);
#endif

  /* Make a table of what switches there are (switches, n_switches).
     Make a table of specified input files (infiles, n_infiles).
     Decode switches that are handled locally.  */

  process_command (argc, (const char **) argv);

  /* Initialize the vector of specs to just the default.
     This means one element containing 0s, as a terminator.  */

  compilers = xmalloc (sizeof default_compilers);
  memcpy (compilers, default_compilers, sizeof default_compilers);
  n_compilers = n_default_compilers;

  /* Read specs from a file if there is one.  */

  machine_suffix = concat (spec_machine, dir_separator_str,
			   spec_version, dir_separator_str, NULL);
  just_machine_suffix = concat (spec_machine, dir_separator_str, NULL);

  specs_file = find_a_file (&startfile_prefixes, "specs", R_OK, true);
  /* Read the specs file unless it is a default one.  */
  if (specs_file != 0 && strcmp (specs_file, "specs"))
    read_specs (specs_file, TRUE);
  else
    init_spec ();

  /* We need to check standard_exec_prefix/just_machine_suffix/specs
     for any override of as, ld and libraries.  */
  specs_file = alloca (strlen (standard_exec_prefix)
		       + strlen (just_machine_suffix) + sizeof ("specs"));

  strcpy (specs_file, standard_exec_prefix);
  strcat (specs_file, just_machine_suffix);
  strcat (specs_file, "specs");
  if (access (specs_file, R_OK) == 0)
    read_specs (specs_file, TRUE);

  /* Process any configure-time defaults specified for the command line
     options, via OPTION_DEFAULT_SPECS.  */
  for (i = 0; i < ARRAY_SIZE (option_default_specs); i++)
    do_option_spec (option_default_specs[i].name,
		    option_default_specs[i].spec);

  /* Process DRIVER_SELF_SPECS, adding any new options to the end
     of the command line.  */

  for (i = 0; i < ARRAY_SIZE (driver_self_specs); i++)
    do_self_spec (driver_self_specs[i]);

  /* If not cross-compiling, look for executables in the standard
     places.  */
  if (*cross_compile == '0')
    {
      if (*md_exec_prefix)
	{
	  add_prefix (&exec_prefixes, md_exec_prefix, "GCC",
		      PREFIX_PRIORITY_LAST, 0, 0);
	}
    }

  /* Process sysroot_suffix_spec.  */
  if (*sysroot_suffix_spec != 0
      && do_spec_2 (sysroot_suffix_spec) == 0)
    {
      if (argbuf_index > 1)
        error ("spec failure: more than one arg to SYSROOT_SUFFIX_SPEC");
      else if (argbuf_index == 1)
        target_sysroot_suffix = xstrdup (argbuf[argbuf_index -1]);
    }

#ifdef HAVE_LD_SYSROOT
  /* Pass the --sysroot option to the linker, if it supports that.  If
     there is a sysroot_suffix_spec, it has already been processed by
     this point, so target_system_root really is the system root we
     should be using.  */
  if (target_system_root)
    {
      obstack_grow (&obstack, "%(sysroot_spec) ", strlen ("%(sysroot_spec) "));
      obstack_grow0 (&obstack, link_spec, strlen (link_spec));
      set_spec ("link", XOBFINISH (&obstack, const char *));
    }
#endif

  /* Process sysroot_hdrs_suffix_spec.  */
  if (*sysroot_hdrs_suffix_spec != 0
      && do_spec_2 (sysroot_hdrs_suffix_spec) == 0)
    {
      if (argbuf_index > 1)
        error ("spec failure: more than one arg to SYSROOT_HEADERS_SUFFIX_SPEC");
      else if (argbuf_index == 1)
        target_sysroot_hdrs_suffix = xstrdup (argbuf[argbuf_index -1]);
    }

  /* Look for startfiles in the standard places.  */
  if (*startfile_prefix_spec != 0
      && do_spec_2 (startfile_prefix_spec) == 0
      && do_spec_1 (" ", 0, NULL) == 0)
    {
      int ndx;
      for (ndx = 0; ndx < argbuf_index; ndx++)
	add_sysrooted_prefix (&startfile_prefixes, argbuf[ndx], "BINUTILS",
			      PREFIX_PRIORITY_LAST, 0, 1);
    }
  /* We should eventually get rid of all these and stick to
     startfile_prefix_spec exclusively.  */
  else if (*cross_compile == '0' || target_system_root)
    {
      if (*md_startfile_prefix)
	add_sysrooted_prefix (&startfile_prefixes, md_startfile_prefix,
			      "GCC", PREFIX_PRIORITY_LAST, 0, 1);

      if (*md_startfile_prefix_1)
	add_sysrooted_prefix (&startfile_prefixes, md_startfile_prefix_1,
			      "GCC", PREFIX_PRIORITY_LAST, 0, 1);

      /* If standard_startfile_prefix is relative, base it on
	 standard_exec_prefix.  This lets us move the installed tree
	 as a unit.  If GCC_EXEC_PREFIX is defined, base
	 standard_startfile_prefix on that as well.

         If the prefix is relative, only search it for native compilers;
         otherwise we will search a directory containing host libraries.  */
      if (IS_ABSOLUTE_PATH (standard_startfile_prefix))
	add_sysrooted_prefix (&startfile_prefixes,
			      standard_startfile_prefix, "BINUTILS",
			      PREFIX_PRIORITY_LAST, 0, 1);
      else if (*cross_compile == '0')
	{
	  if (gcc_exec_prefix)
	    add_prefix (&startfile_prefixes,
			concat (gcc_exec_prefix, machine_suffix,
				standard_startfile_prefix, NULL),
			NULL, PREFIX_PRIORITY_LAST, 0, 1);
	  add_prefix (&startfile_prefixes,
		      concat (standard_exec_prefix,
			      machine_suffix,
			      standard_startfile_prefix, NULL),
		      NULL, PREFIX_PRIORITY_LAST, 0, 1);
	}

      if (*standard_startfile_prefix_1)
 	add_sysrooted_prefix (&startfile_prefixes,
			      standard_startfile_prefix_1, "BINUTILS",
			      PREFIX_PRIORITY_LAST, 0, 1);
      if (*standard_startfile_prefix_2)
	add_sysrooted_prefix (&startfile_prefixes,
			      standard_startfile_prefix_2, "BINUTILS",
			      PREFIX_PRIORITY_LAST, 0, 1);
    }

  /* Process any user specified specs in the order given on the command
     line.  */
  for (uptr = user_specs_head; uptr; uptr = uptr->next)
    {
      char *filename = find_a_file (&startfile_prefixes, uptr->filename,
				    R_OK, true);
      read_specs (filename ? filename : uptr->filename, FALSE);
    }

  /* If we have a GCC_EXEC_PREFIX envvar, modify it for cpp's sake.  */
  if (gcc_exec_prefix)
    gcc_exec_prefix = concat (gcc_exec_prefix, spec_machine, dir_separator_str,
			      spec_version, dir_separator_str, NULL);

  /* Now we have the specs.
     Set the `valid' bits for switches that match anything in any spec.  */

  validate_all_switches ();

  /* Now that we have the switches and the specs, set
     the subdirectory based on the options.  */
  set_multilib_dir ();

  /* Warn about any switches that no pass was interested in.  */

  for (i = 0; (int) i < n_switches; i++)
    if (! switches[i].validated)
      error ("unrecognized option '-%s'", switches[i].part1);

  /* Obey some of the options.  */

  if (print_search_dirs)
    {
      printf (_("install: %s%s\n"), standard_exec_prefix, machine_suffix);
      printf (_("programs: %s\n"),
	      build_search_list (&exec_prefixes, "", false, false));
      printf (_("libraries: %s\n"),
	      build_search_list (&startfile_prefixes, "", false, true));
      return (0);
    }

  if (print_file_name)
    {
      printf ("%s\n", find_file (print_file_name));
      return (0);
    }

  if (print_prog_name)
    {
      char *newname = find_a_file (&exec_prefixes, print_prog_name, X_OK, 0);
      printf ("%s\n", (newname ? newname : print_prog_name));
      return (0);
    }

  if (print_multi_lib)
    {
      print_multilib_info ();
      return (0);
    }

  if (print_multi_directory)
    {
      if (multilib_dir == NULL)
	printf (".\n");
      else
	printf ("%s\n", multilib_dir);
      return (0);
    }

  if (print_multi_os_directory)
    {
      if (multilib_os_dir == NULL)
	printf (".\n");
      else
	printf ("%s\n", multilib_os_dir);
      return (0);
    }

  if (target_help_flag)
   {
      /* Print if any target specific options.  */

      /* We do not exit here. Instead we have created a fake input file
         called 'target-dummy' which needs to be compiled, and we pass this
         on to the various sub-processes, along with the --target-help
         switch.  */
    }

  if (print_help_list)
    {
      display_help ();

      if (! verbose_flag)
	{
	  printf (_("\nFor bug reporting instructions, please see:\n"));
	  printf ("%s.\n", bug_report_url);

	  return (0);
	}

      /* We do not exit here.  Instead we have created a fake input file
	 called 'help-dummy' which needs to be compiled, and we pass this
	 on the various sub-processes, along with the --help switch.  */
    }

  if (verbose_flag)
    {
      int n;
      const char *thrmod;

      notice ("Target: %s\n", spec_machine);
      notice ("Configured with: %s\n", configuration_arguments);

#ifdef THREAD_MODEL_SPEC
      /* We could have defined THREAD_MODEL_SPEC to "%*" by default,
	 but there's no point in doing all this processing just to get
	 thread_model back.  */
      obstack_init (&obstack);
      do_spec_1 (THREAD_MODEL_SPEC, 0, thread_model);
      obstack_1grow (&obstack, '\0');
      thrmod = XOBFINISH (&obstack, const char *);
#else
      thrmod = thread_model;
#endif

      notice ("Thread model: %s\n", thrmod);

      /* compiler_version is truncated at the first space when initialized
	 from version string, so truncate version_string at the first space
	 before comparing.  */
      for (n = 0; version_string[n]; n++)
	if (version_string[n] == ' ')
	  break;

      if (! strncmp (version_string, compiler_version, n)
	  && compiler_version[n] == 0)
	notice ("gcc version %s\n", version_string);
      else
	notice ("gcc driver version %s executing gcc version %s\n",
		version_string, compiler_version);

      if (n_infiles == 0)
	return (0);
    }

  if (n_infiles == added_libraries)
    fatal ("no input files");

  /* Make a place to record the compiler output file names
     that correspond to the input files.  */

  i = n_infiles;
  i += lang_specific_extra_outfiles;
  outfiles = XCNEWVEC (const char *, i);

  /* Record which files were specified explicitly as link input.  */

  explicit_link_files = XCNEWVEC (char, n_infiles);

  if (combine_flag)
    combine_inputs = true;
  else
    combine_inputs = false;

  for (i = 0; (int) i < n_infiles; i++)
    {
      const char *name = infiles[i].name;
      struct compiler *compiler = lookup_compiler (name,
						   strlen (name),
						   infiles[i].language);

      if (compiler && !(compiler->combinable))
	combine_inputs = false;

      if (lang_n_infiles > 0 && compiler != input_file_compiler
	  && infiles[i].language && infiles[i].language[0] != '*')
	infiles[i].incompiler = compiler;
      else if (compiler)
	{
	  lang_n_infiles++;
	  input_file_compiler = compiler;
	  infiles[i].incompiler = compiler;
	}
      else
	{
	  /* Since there is no compiler for this input file, assume it is a
	     linker file.  */
	  explicit_link_files[i] = 1;
	  infiles[i].incompiler = NULL;
	}
      infiles[i].compiled = false;
      infiles[i].preprocessed = false;
    }

  if (!combine_inputs && have_c && have_o && lang_n_infiles > 1)
   fatal ("cannot specify -o with -c or -S with multiple files");

  if (combine_flag && save_temps_flag)
    {
      bool save_combine_inputs = combine_inputs;
      /* Must do a separate pre-processing pass for C & Objective-C files, to
	 obtain individual .i files.  */

      combine_inputs = false;
      for (i = 0; (int) i < n_infiles; i++)
	{
	  int this_file_error = 0;

	  input_file_number = i;
	  set_input (infiles[i].name);
	  if (infiles[i].incompiler
	      && (infiles[i].incompiler)->needs_preprocessing)
	    input_file_compiler = infiles[i].incompiler;
	  else
	    continue;

	  if (input_file_compiler)
	    {
	      if (input_file_compiler->spec[0] == '#')
		{
		  error ("%s: %s compiler not installed on this system",
			 input_filename, &input_file_compiler->spec[1]);
		  this_file_error = 1;
		}
	      else
		{
		  value = do_spec (input_file_compiler->spec);
		  infiles[i].preprocessed = true;
		  if (!have_o_argbuf_index)
		    fatal ("spec '%s' is invalid", input_file_compiler->spec);
		  infiles[i].name = argbuf[have_o_argbuf_index];
		  infiles[i].incompiler
		    = lookup_compiler (infiles[i].name,
				       strlen (infiles[i].name),
				       infiles[i].language);

		  if (value < 0)
		    this_file_error = 1;
		}
	    }

	  if (this_file_error)
	    {
	      delete_failure_queue ();
	      error_count++;
	      break;
	    }
	  clear_failure_queue ();
	}
      combine_inputs = save_combine_inputs;
    }

  for (i = 0; (int) i < n_infiles; i++)
    {
      int this_file_error = 0;

      /* Tell do_spec what to substitute for %i.  */

      input_file_number = i;
      set_input (infiles[i].name);

      if (infiles[i].compiled)
	continue;

      /* Use the same thing in %o, unless cp->spec says otherwise.  */

      outfiles[i] = input_filename;

      /* Figure out which compiler from the file's suffix.  */

      if (! combine_inputs)
	input_file_compiler
	  = lookup_compiler (infiles[i].name, input_filename_length,
			     infiles[i].language);
      else
	input_file_compiler = infiles[i].incompiler;

      if (input_file_compiler)
	{
	  /* Ok, we found an applicable compiler.  Run its spec.  */

	  if (input_file_compiler->spec[0] == '#')
	    {
	      error ("%s: %s compiler not installed on this system",
		     input_filename, &input_file_compiler->spec[1]);
	      this_file_error = 1;
	    }
	  else
	    {
	      value = do_spec (input_file_compiler->spec);
	      infiles[i].compiled = true;
	      if (value < 0)
		this_file_error = 1;
	    }
	}

      /* If this file's name does not contain a recognized suffix,
	 record it as explicit linker input.  */

      else
	explicit_link_files[i] = 1;

      /* Clear the delete-on-failure queue, deleting the files in it
	 if this compilation failed.  */

      if (this_file_error)
	{
	  delete_failure_queue ();
	  error_count++;
	}
      /* If this compilation succeeded, don't delete those files later.  */
      clear_failure_queue ();
    }

  /* Reset the input file name to the first compile/object file name, for use
     with %b in LINK_SPEC. We use the first input file that we can find
     a compiler to compile it instead of using infiles.language since for
     languages other than C we use aliases that we then lookup later.  */
  if (n_infiles > 0)
    {
      int i;

      for (i = 0; i < n_infiles ; i++)
	if (infiles[i].language && infiles[i].language[0] != '*')
	  {
	    set_input (infiles[i].name);
	    break;
	  }
    }

  if (error_count == 0)
    {
      /* Make sure INPUT_FILE_NUMBER points to first available open
	 slot.  */
      input_file_number = n_infiles;
      if (lang_specific_pre_link ())
	error_count++;
    }

  /* Determine if there are any linker input files.  */
  num_linker_inputs = 0;
  for (i = 0; (int) i < n_infiles; i++)
    if (explicit_link_files[i] || outfiles[i] != NULL)
      num_linker_inputs++;

  /* Run ld to link all the compiler output files.  */

  if (num_linker_inputs > 0 && error_count == 0)
    {
      int tmp = execution_count;

      /* We'll use ld if we can't find collect2.  */
      if (! strcmp (linker_name_spec, "collect2"))
	{
	  char *s = find_a_file (&exec_prefixes, "collect2", X_OK, false);
	  if (s == NULL)
	    linker_name_spec = "ld";
	}
      /* Rebuild the COMPILER_PATH and LIBRARY_PATH environment variables
	 for collect.  */
      putenv_from_prefixes (&exec_prefixes, "COMPILER_PATH", false);
      putenv_from_prefixes (&startfile_prefixes, LIBRARY_PATH_ENV, true);

      value = do_spec (link_command_spec);
      if (value < 0)
	error_count = 1;
      linker_was_run = (tmp != execution_count);
    }

  /* If options said don't run linker,
     complain about input files to be given to the linker.  */

  if (! linker_was_run && error_count == 0)
    for (i = 0; (int) i < n_infiles; i++)
      if (explicit_link_files[i])
	error ("%s: linker input file unused because linking not done",
	       outfiles[i]);

  /* Delete some or all of the temporary files we made.  */

  if (error_count)
    delete_failure_queue ();
  delete_temp_files ();

  if (print_help_list)
    {
      printf (("\nFor bug reporting instructions, please see:\n"));
      printf ("%s\n", bug_report_url);
    }

  return (signal_count != 0 ? 2
	  : error_count > 0 ? (pass_exit_codes ? greatest_status : 1)
	  : 0);
}

/* Find the proper compilation spec for the file name NAME,
   whose length is LENGTH.  LANGUAGE is the specified language,
   or 0 if this file is to be passed to the linker.  */

static struct compiler *
lookup_compiler (const char *name, size_t length, const char *language)
{
  struct compiler *cp;

  /* If this was specified by the user to be a linker input, indicate that.  */
  if (language != 0 && language[0] == '*')
    return 0;

  /* Otherwise, look for the language, if one is spec'd.  */
  if (language != 0)
    {
      for (cp = compilers + n_compilers - 1; cp >= compilers; cp--)
	if (cp->suffix[0] == '@' && !strcmp (cp->suffix + 1, language))
	  return cp;

      error ("language %s not recognized", language);
      return 0;
    }

  /* Look for a suffix.  */
  for (cp = compilers + n_compilers - 1; cp >= compilers; cp--)
    {
      if (/* The suffix `-' matches only the file name `-'.  */
	  (!strcmp (cp->suffix, "-") && !strcmp (name, "-"))
	  || (strlen (cp->suffix) < length
	      /* See if the suffix matches the end of NAME.  */
	      && !strcmp (cp->suffix,
			  name + length - strlen (cp->suffix))
	 ))
	break;
    }

#if defined (OS2) ||defined (HAVE_DOS_BASED_FILE_SYSTEM)
  /* Look again, but case-insensitively this time.  */
  if (cp < compilers)
    for (cp = compilers + n_compilers - 1; cp >= compilers; cp--)
      {
	if (/* The suffix `-' matches only the file name `-'.  */
	    (!strcmp (cp->suffix, "-") && !strcmp (name, "-"))
	    || (strlen (cp->suffix) < length
		/* See if the suffix matches the end of NAME.  */
		&& ((!strcmp (cp->suffix,
			     name + length - strlen (cp->suffix))
		     || !strpbrk (cp->suffix, "ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
		    && !strcasecmp (cp->suffix,
				    name + length - strlen (cp->suffix)))
	   ))
	  break;
      }
#endif

  if (cp >= compilers)
    {
      if (cp->spec[0] != '@')
	/* A non-alias entry: return it.  */
	return cp;

      /* An alias entry maps a suffix to a language.
	 Search for the language; pass 0 for NAME and LENGTH
	 to avoid infinite recursion if language not found.  */
      return lookup_compiler (NULL, 0, cp->spec + 1);
    }
  return 0;
}

static char *
save_string (const char *s, int len)
{
  char *result = XNEWVEC (char, len + 1);

  memcpy (result, s, len);
  result[len] = 0;
  return result;
}

void
pfatal_with_name (const char *name)
{
  perror_with_name (name);
  delete_temp_files ();
  exit (1);
}

static void
perror_with_name (const char *name)
{
  error ("%s: %s", name, xstrerror (errno));
}

/* Output an error message and exit.  */

void
fancy_abort (const char *file, int line, const char *func)
{
  fatal_ice ("internal gcc abort in %s, at %s:%d", func, file, line);
}

/* Output an error message and exit.  */

void
fatal_ice (const char *cmsgid, ...)
{
  va_list ap;

  va_start (ap, cmsgid);

  fprintf (stderr, "%s: ", programname);
  vfprintf (stderr, _(cmsgid), ap);
  va_end (ap);
  fprintf (stderr, "\n");
  delete_temp_files ();
  exit (pass_exit_codes ? ICE_EXIT_CODE : 1);
}

void
fatal (const char *cmsgid, ...)
{
  va_list ap;

  va_start (ap, cmsgid);

  fprintf (stderr, "%s: ", programname);
  vfprintf (stderr, _(cmsgid), ap);
  va_end (ap);
  fprintf (stderr, "\n");
  delete_temp_files ();
  exit (1);
}

/* The argument is actually c-format, not gcc-internal-format,
   but because functions with identical names are used through
   the rest of the compiler with gcc-internal-format, we just
   need to hope all users of these functions use the common
   subset between c-format and gcc-internal-format.  */

void
error (const char *gmsgid, ...)
{
  va_list ap;

  va_start (ap, gmsgid);
  fprintf (stderr, "%s: ", programname);
  vfprintf (stderr, _(gmsgid), ap);
  va_end (ap);

  fprintf (stderr, "\n");
}

static void
notice (const char *cmsgid, ...)
{
  va_list ap;

  va_start (ap, cmsgid);
  vfprintf (stderr, _(cmsgid), ap);
  va_end (ap);
}

static inline void
validate_switches_from_spec (const char *spec)
{
  const char *p = spec;
  char c;
  while ((c = *p++))
    if (c == '%' && (*p == '{' || *p == '<' || (*p == 'W' && *++p == '{')))
      /* We have a switch spec.  */
      p = validate_switches (p + 1);
}

static void
validate_all_switches (void)
{
  struct compiler *comp;
  struct spec_list *spec;

  for (comp = compilers; comp->spec; comp++)
    validate_switches_from_spec (comp->spec);

  /* Look through the linked list of specs read from the specs file.  */
  for (spec = specs; spec; spec = spec->next)
    validate_switches_from_spec (*spec->ptr_spec);

  validate_switches_from_spec (link_command_spec);
}

/* Look at the switch-name that comes after START
   and mark as valid all supplied switches that match it.  */

static const char *
validate_switches (const char *start)
{
  const char *p = start;
  const char *atom;
  size_t len;
  int i;
  bool suffix = false;
  bool starred = false;

#define SKIP_WHITE() do { while (*p == ' ' || *p == '\t') p++; } while (0)

next_member:
  SKIP_WHITE ();

  if (*p == '!')
    p++;

  SKIP_WHITE ();
  if (*p == '.')
    suffix = true, p++;

  atom = p;
  while (ISIDNUM (*p) || *p == '-' || *p == '+' || *p == '='
	 || *p == ',' || *p == '.' || *p == '@')
    p++;
  len = p - atom;

  if (*p == '*')
    starred = true, p++;

  SKIP_WHITE ();

  if (!suffix)
    {
      /* Mark all matching switches as valid.  */
      for (i = 0; i < n_switches; i++)
	if (!strncmp (switches[i].part1, atom, len)
	    && (starred || switches[i].part1[len] == 0))
	  switches[i].validated = 1;
    }

  if (*p) p++;
  if (*p && (p[-1] == '|' || p[-1] == '&'))
    goto next_member;

  if (*p && p[-1] == ':')
    {
      while (*p && *p != ';' && *p != '}')
	{
	  if (*p == '%')
	    {
	      p++;
	      if (*p == '{' || *p == '<')
		p = validate_switches (p+1);
	      else if (p[0] == 'W' && p[1] == '{')
		p = validate_switches (p+2);
	    }
	  else
	    p++;
	}

      if (*p) p++;
      if (*p && p[-1] == ';')
	goto next_member;
    }

  return p;
#undef SKIP_WHITE
}

struct mdswitchstr
{
  const char *str;
  int len;
};

static struct mdswitchstr *mdswitches;
static int n_mdswitches;

/* Check whether a particular argument was used.  The first time we
   canonicalize the switches to keep only the ones we care about.  */

static int
used_arg (const char *p, int len)
{
  struct mswitchstr
  {
    const char *str;
    const char *replace;
    int len;
    int rep_len;
  };

  static struct mswitchstr *mswitches;
  static int n_mswitches;
  int i, j;

  if (!mswitches)
    {
      struct mswitchstr *matches;
      const char *q;
      int cnt = 0;

      /* Break multilib_matches into the component strings of string
         and replacement string.  */
      for (q = multilib_matches; *q != '\0'; q++)
	if (*q == ';')
	  cnt++;

      matches = alloca ((sizeof (struct mswitchstr)) * cnt);
      i = 0;
      q = multilib_matches;
      while (*q != '\0')
	{
	  matches[i].str = q;
	  while (*q != ' ')
	    {
	      if (*q == '\0')
		{
		invalid_matches:
		  fatal ("multilib spec '%s' is invalid", multilib_matches);
		}
	      q++;
	    }
	  matches[i].len = q - matches[i].str;

	  matches[i].replace = ++q;
	  while (*q != ';' && *q != '\0')
	    {
	      if (*q == ' ')
		goto invalid_matches;
	      q++;
	    }
	  matches[i].rep_len = q - matches[i].replace;
	  i++;
	  if (*q == ';')
	    q++;
	}

      /* Now build a list of the replacement string for switches that we care
	 about.  Make sure we allocate at least one entry.  This prevents
	 xmalloc from calling fatal, and prevents us from re-executing this
	 block of code.  */
      mswitches
	= XNEWVEC (struct mswitchstr, n_mdswitches + (n_switches ? n_switches : 1));
      for (i = 0; i < n_switches; i++)
	if (switches[i].live_cond != SWITCH_IGNORE)
	  {
	    int xlen = strlen (switches[i].part1);
	    for (j = 0; j < cnt; j++)
	      if (xlen == matches[j].len
		  && ! strncmp (switches[i].part1, matches[j].str, xlen))
		{
		  mswitches[n_mswitches].str = matches[j].replace;
		  mswitches[n_mswitches].len = matches[j].rep_len;
		  mswitches[n_mswitches].replace = (char *) 0;
		  mswitches[n_mswitches].rep_len = 0;
		  n_mswitches++;
		  break;
		}
	  }

      /* Add MULTILIB_DEFAULTS switches too, as long as they were not present
	 on the command line nor any options mutually incompatible with
	 them.  */
      for (i = 0; i < n_mdswitches; i++)
	{
	  const char *r;

	  for (q = multilib_options; *q != '\0'; q++)
	    {
	      while (*q == ' ')
		q++;

	      r = q;
	      while (strncmp (q, mdswitches[i].str, mdswitches[i].len) != 0
		     || strchr (" /", q[mdswitches[i].len]) == NULL)
		{
		  while (*q != ' ' && *q != '/' && *q != '\0')
		    q++;
		  if (*q != '/')
		    break;
		  q++;
		}

	      if (*q != ' ' && *q != '\0')
		{
		  while (*r != ' ' && *r != '\0')
		    {
		      q = r;
		      while (*q != ' ' && *q != '/' && *q != '\0')
			q++;

		      if (used_arg (r, q - r))
			break;

		      if (*q != '/')
			{
			  mswitches[n_mswitches].str = mdswitches[i].str;
			  mswitches[n_mswitches].len = mdswitches[i].len;
			  mswitches[n_mswitches].replace = (char *) 0;
			  mswitches[n_mswitches].rep_len = 0;
			  n_mswitches++;
			  break;
			}

		      r = q + 1;
		    }
		  break;
		}
	    }
	}
    }

  for (i = 0; i < n_mswitches; i++)
    if (len == mswitches[i].len && ! strncmp (p, mswitches[i].str, len))
      return 1;

  return 0;
}

static int
default_arg (const char *p, int len)
{
  int i;

  for (i = 0; i < n_mdswitches; i++)
    if (len == mdswitches[i].len && ! strncmp (p, mdswitches[i].str, len))
      return 1;

  return 0;
}

/* Work out the subdirectory to use based on the options. The format of
   multilib_select is a list of elements. Each element is a subdirectory
   name followed by a list of options followed by a semicolon. The format
   of multilib_exclusions is the same, but without the preceding
   directory. First gcc will check the exclusions, if none of the options
   beginning with an exclamation point are present, and all of the other
   options are present, then we will ignore this completely. Passing
   that, gcc will consider each multilib_select in turn using the same
   rules for matching the options. If a match is found, that subdirectory
   will be used.  */

static void
set_multilib_dir (void)
{
  const char *p;
  unsigned int this_path_len;
  const char *this_path, *this_arg;
  const char *start, *end;
  int not_arg;
  int ok, ndfltok, first;

  n_mdswitches = 0;
  start = multilib_defaults;
  while (*start == ' ' || *start == '\t')
    start++;
  while (*start != '\0')
    {
      n_mdswitches++;
      while (*start != ' ' && *start != '\t' && *start != '\0')
	start++;
      while (*start == ' ' || *start == '\t')
        start++;
    }

  if (n_mdswitches)
    {
      int i = 0;

      mdswitches = XNEWVEC (struct mdswitchstr, n_mdswitches);
      for (start = multilib_defaults; *start != '\0'; start = end + 1)
	{
	  while (*start == ' ' || *start == '\t')
	    start++;

	  if (*start == '\0')
	    break;

	  for (end = start + 1;
	       *end != ' ' && *end != '\t' && *end != '\0'; end++)
	    ;

	  obstack_grow (&multilib_obstack, start, end - start);
	  obstack_1grow (&multilib_obstack, 0);
	  mdswitches[i].str = XOBFINISH (&multilib_obstack, const char *);
	  mdswitches[i++].len = end - start;

	  if (*end == '\0')
	    break;
	}
    }

  p = multilib_exclusions;
  while (*p != '\0')
    {
      /* Ignore newlines.  */
      if (*p == '\n')
	{
	  ++p;
	  continue;
	}

      /* Check the arguments.  */
      ok = 1;
      while (*p != ';')
	{
	  if (*p == '\0')
	    {
	    invalid_exclusions:
	      fatal ("multilib exclusions '%s' is invalid",
		     multilib_exclusions);
	    }

	  if (! ok)
	    {
	      ++p;
	      continue;
	    }

	  this_arg = p;
	  while (*p != ' ' && *p != ';')
	    {
	      if (*p == '\0')
		goto invalid_exclusions;
	      ++p;
	    }

	  if (*this_arg != '!')
	    not_arg = 0;
	  else
	    {
	      not_arg = 1;
	      ++this_arg;
	    }

	  ok = used_arg (this_arg, p - this_arg);
	  if (not_arg)
	    ok = ! ok;

	  if (*p == ' ')
	    ++p;
	}

      if (ok)
	return;

      ++p;
    }

  first = 1;
  p = multilib_select;
  while (*p != '\0')
    {
      /* Ignore newlines.  */
      if (*p == '\n')
	{
	  ++p;
	  continue;
	}

      /* Get the initial path.  */
      this_path = p;
      while (*p != ' ')
	{
	  if (*p == '\0')
	    {
	    invalid_select:
	      fatal ("multilib select '%s' is invalid",
		     multilib_select);
	    }
	  ++p;
	}
      this_path_len = p - this_path;

      /* Check the arguments.  */
      ok = 1;
      ndfltok = 1;
      ++p;
      while (*p != ';')
	{
	  if (*p == '\0')
	    goto invalid_select;

	  if (! ok)
	    {
	      ++p;
	      continue;
	    }

	  this_arg = p;
	  while (*p != ' ' && *p != ';')
	    {
	      if (*p == '\0')
		goto invalid_select;
	      ++p;
	    }

	  if (*this_arg != '!')
	    not_arg = 0;
	  else
	    {
	      not_arg = 1;
	      ++this_arg;
	    }

	  /* If this is a default argument, we can just ignore it.
	     This is true even if this_arg begins with '!'.  Beginning
	     with '!' does not mean that this argument is necessarily
	     inappropriate for this library: it merely means that
	     there is a more specific library which uses this
	     argument.  If this argument is a default, we need not
	     consider that more specific library.  */
	  ok = used_arg (this_arg, p - this_arg);
	  if (not_arg)
	    ok = ! ok;

	  if (! ok)
	    ndfltok = 0;

	  if (default_arg (this_arg, p - this_arg))
	    ok = 1;

	  if (*p == ' ')
	    ++p;
	}

      if (ok && first)
	{
	  if (this_path_len != 1
	      || this_path[0] != '.')
	    {
	      char *new_multilib_dir = XNEWVEC (char, this_path_len + 1);
	      char *q;

	      strncpy (new_multilib_dir, this_path, this_path_len);
	      new_multilib_dir[this_path_len] = '\0';
	      q = strchr (new_multilib_dir, ':');
	      if (q != NULL)
		*q = '\0';
	      multilib_dir = new_multilib_dir;
	    }
	  first = 0;
	}

      if (ndfltok)
	{
	  const char *q = this_path, *end = this_path + this_path_len;

	  while (q < end && *q != ':')
	    q++;
	  if (q < end)
	    {
	      char *new_multilib_os_dir = XNEWVEC (char, end - q);
	      memcpy (new_multilib_os_dir, q + 1, end - q - 1);
	      new_multilib_os_dir[end - q - 1] = '\0';
	      multilib_os_dir = new_multilib_os_dir;
	      break;
	    }
	}

      ++p;
    }

  if (multilib_dir == NULL && multilib_os_dir != NULL
      && strcmp (multilib_os_dir, ".") == 0)
    {
      free ((char *) multilib_os_dir);
      multilib_os_dir = NULL;
    }
  else if (multilib_dir != NULL && multilib_os_dir == NULL)
    multilib_os_dir = multilib_dir;
}

/* Print out the multiple library subdirectory selection
   information.  This prints out a series of lines.  Each line looks
   like SUBDIRECTORY;@OPTION@OPTION, with as many options as is
   required.  Only the desired options are printed out, the negative
   matches.  The options are print without a leading dash.  There are
   no spaces to make it easy to use the information in the shell.
   Each subdirectory is printed only once.  This assumes the ordering
   generated by the genmultilib script. Also, we leave out ones that match
   the exclusions.  */

static void
print_multilib_info (void)
{
  const char *p = multilib_select;
  const char *last_path = 0, *this_path;
  int skip;
  unsigned int last_path_len = 0;

  while (*p != '\0')
    {
      skip = 0;
      /* Ignore newlines.  */
      if (*p == '\n')
	{
	  ++p;
	  continue;
	}

      /* Get the initial path.  */
      this_path = p;
      while (*p != ' ')
	{
	  if (*p == '\0')
	    {
	    invalid_select:
	      fatal ("multilib select '%s' is invalid", multilib_select);
	    }

	  ++p;
	}

      /* When --disable-multilib was used but target defines
	 MULTILIB_OSDIRNAMES, entries starting with .: are there just
	 to find multilib_os_dir, so skip them from output.  */
      if (this_path[0] == '.' && this_path[1] == ':')
	skip = 1;

      /* Check for matches with the multilib_exclusions. We don't bother
         with the '!' in either list. If any of the exclusion rules match
         all of its options with the select rule, we skip it.  */
      {
	const char *e = multilib_exclusions;
	const char *this_arg;

	while (*e != '\0')
	  {
	    int m = 1;
	    /* Ignore newlines.  */
	    if (*e == '\n')
	      {
		++e;
		continue;
	      }

	    /* Check the arguments.  */
	    while (*e != ';')
	      {
		const char *q;
		int mp = 0;

		if (*e == '\0')
		  {
		  invalid_exclusion:
		    fatal ("multilib exclusion '%s' is invalid",
			   multilib_exclusions);
		  }

		if (! m)
		  {
		    ++e;
		    continue;
		  }

		this_arg = e;

		while (*e != ' ' && *e != ';')
		  {
		    if (*e == '\0')
		      goto invalid_exclusion;
		    ++e;
		  }

		q = p + 1;
		while (*q != ';')
		  {
		    const char *arg;
		    int len = e - this_arg;

		    if (*q == '\0')
		      goto invalid_select;

		    arg = q;

		    while (*q != ' ' && *q != ';')
		      {
			if (*q == '\0')
			  goto invalid_select;
			++q;
		      }

		    if (! strncmp (arg, this_arg,
				   (len < q - arg) ? q - arg : len)
			|| default_arg (this_arg, e - this_arg))
		      {
			mp = 1;
			break;
		      }

		    if (*q == ' ')
		      ++q;
		  }

		if (! mp)
		  m = 0;

		if (*e == ' ')
		  ++e;
	      }

	    if (m)
	      {
		skip = 1;
		break;
	      }

	    if (*e != '\0')
	      ++e;
	  }
      }

      if (! skip)
	{
	  /* If this is a duplicate, skip it.  */
	  skip = (last_path != 0
		  && (unsigned int) (p - this_path) == last_path_len
		  && ! strncmp (last_path, this_path, last_path_len));

	  last_path = this_path;
	  last_path_len = p - this_path;
	}

      /* If this directory requires any default arguments, we can skip
	 it.  We will already have printed a directory identical to
	 this one which does not require that default argument.  */
      if (! skip)
	{
	  const char *q;

	  q = p + 1;
	  while (*q != ';')
	    {
	      const char *arg;

	      if (*q == '\0')
		goto invalid_select;

	      if (*q == '!')
		arg = NULL;
	      else
		arg = q;

	      while (*q != ' ' && *q != ';')
		{
		  if (*q == '\0')
		    goto invalid_select;
		  ++q;
		}

	      if (arg != NULL
		  && default_arg (arg, q - arg))
		{
		  skip = 1;
		  break;
		}

	      if (*q == ' ')
		++q;
	    }
	}

      if (! skip)
	{
	  const char *p1;

	  for (p1 = last_path; p1 < p && *p1 != ':'; p1++)
	    putchar (*p1);
	  putchar (';');
	}

      ++p;
      while (*p != ';')
	{
	  int use_arg;

	  if (*p == '\0')
	    goto invalid_select;

	  if (skip)
	    {
	      ++p;
	      continue;
	    }

	  use_arg = *p != '!';

	  if (use_arg)
	    putchar ('@');

	  while (*p != ' ' && *p != ';')
	    {
	      if (*p == '\0')
		goto invalid_select;
	      if (use_arg)
		putchar (*p);
	      ++p;
	    }

	  if (*p == ' ')
	    ++p;
	}

      if (! skip)
	{
	  /* If there are extra options, print them now.  */
	  if (multilib_extra && *multilib_extra)
	    {
	      int print_at = TRUE;
	      const char *q;

	      for (q = multilib_extra; *q != '\0'; q++)
		{
		  if (*q == ' ')
		    print_at = TRUE;
		  else
		    {
		      if (print_at)
			putchar ('@');
		      putchar (*q);
		      print_at = FALSE;
		    }
		}
	    }

	  putchar ('\n');
	}

      ++p;
    }
}

/* if-exists built-in spec function.

   Checks to see if the file specified by the absolute pathname in
   ARGS exists.  Returns that pathname if found.

   The usual use for this function is to check for a library file
   (whose name has been expanded with %s).  */

static const char *
if_exists_spec_function (int argc, const char **argv)
{
  /* Must have only one argument.  */
  if (argc == 1 && IS_ABSOLUTE_PATH (argv[0]) && ! access (argv[0], R_OK))
    return argv[0];

  return NULL;
}

/* if-exists-else built-in spec function.

   This is like if-exists, but takes an additional argument which
   is returned if the first argument does not exist.  */

static const char *
if_exists_else_spec_function (int argc, const char **argv)
{
  /* Must have exactly two arguments.  */
  if (argc != 2)
    return NULL;

  if (IS_ABSOLUTE_PATH (argv[0]) && ! access (argv[0], R_OK))
    return argv[0];

  return argv[1];
}

/* replace-outfile built-in spec function.

   This looks for the first argument in the outfiles array's name and
   replaces it with the second argument.  */

static const char *
replace_outfile_spec_function (int argc, const char **argv)
{
  int i;
  /* Must have exactly two arguments.  */
  if (argc != 2)
    abort ();

  for (i = 0; i < n_infiles; i++)
    {
      if (outfiles[i] && !strcmp (outfiles[i], argv[0]))
	outfiles[i] = xstrdup (argv[1]);
    }
  return NULL;
}

/* Given two version numbers, compares the two numbers.
   A version number must match the regular expression
   ([1-9][0-9]*|0)(\.([1-9][0-9]*|0))*
*/
static int
compare_version_strings (const char *v1, const char *v2)
{
  int rresult;
  regex_t r;

  if (regcomp (&r, "^([1-9][0-9]*|0)(\\.([1-9][0-9]*|0))*$",
	       REG_EXTENDED | REG_NOSUB) != 0)
    abort ();
  rresult = regexec (&r, v1, 0, NULL, 0);
  if (rresult == REG_NOMATCH)
    fatal ("invalid version number `%s'", v1);
  else if (rresult != 0)
    abort ();
  rresult = regexec (&r, v2, 0, NULL, 0);
  if (rresult == REG_NOMATCH)
    fatal ("invalid version number `%s'", v2);
  else if (rresult != 0)
    abort ();

  return strverscmp (v1, v2);
}


/* version_compare built-in spec function.

   This takes an argument of the following form:

   <comparison-op> <arg1> [<arg2>] <switch> <result>

   and produces "result" if the comparison evaluates to true,
   and nothing if it doesn't.

   The supported <comparison-op> values are:

   >=  true if switch is a later (or same) version than arg1
   !>  opposite of >=
   <   true if switch is an earlier version than arg1
   !<  opposite of <
   ><  true if switch is arg1 or later, and earlier than arg2
   <>  true if switch is earlier than arg1 or is arg2 or later

   If the switch is not present, the condition is false unless
   the first character of the <comparison-op> is '!'.

   For example,
   %:version-compare(>= 10.3 mmacosx-version-min= -lmx)
   adds -lmx if -mmacosx-version-min=10.3.9 was passed.  */

static const char *
version_compare_spec_function (int argc, const char **argv)
{
  int comp1, comp2;
  size_t switch_len;
  const char *switch_value = NULL;
  int nargs = 1, i;
  bool result;

  if (argc < 3)
    fatal ("too few arguments to %%:version-compare");
  if (argv[0][0] == '\0')
    abort ();
  if ((argv[0][1] == '<' || argv[0][1] == '>') && argv[0][0] != '!')
    nargs = 2;
  if (argc != nargs + 3)
    fatal ("too many arguments to %%:version-compare");

  switch_len = strlen (argv[nargs + 1]);
  for (i = 0; i < n_switches; i++)
    if (!strncmp (switches[i].part1, argv[nargs + 1], switch_len)
	&& check_live_switch (i, switch_len))
      switch_value = switches[i].part1 + switch_len;

  if (switch_value == NULL)
    comp1 = comp2 = -1;
  else
    {
      comp1 = compare_version_strings (switch_value, argv[1]);
      if (nargs == 2)
	comp2 = compare_version_strings (switch_value, argv[2]);
      else
	comp2 = -1;  /* This value unused.  */
    }

  switch (argv[0][0] << 8 | argv[0][1])
    {
    case '>' << 8 | '=':
      result = comp1 >= 0;
      break;
    case '!' << 8 | '<':
      result = comp1 >= 0 || switch_value == NULL;
      break;
    case '<' << 8:
      result = comp1 < 0;
      break;
    case '!' << 8 | '>':
      result = comp1 < 0 || switch_value == NULL;
      break;
    case '>' << 8 | '<':
      result = comp1 >= 0 && comp2 < 0;
      break;
    case '<' << 8 | '>':
      result = comp1 < 0 || comp2 >= 0;
      break;

    default:
      fatal ("unknown operator '%s' in %%:version-compare", argv[0]);
    }
  if (! result)
    return NULL;

  return argv[nargs + 2];
}

/* %:include builtin spec function.  This differs from %include in that it
   can be nested inside a spec, and thus be conditionalized.  It takes
   one argument, the filename, and looks for it in the startfile path.
   The result is always NULL, i.e. an empty expansion.  */

static const char *
include_spec_function (int argc, const char **argv)
{
  char *file;

  if (argc != 1)
    abort ();

  file = find_a_file (&startfile_prefixes, argv[0], R_OK, 0);
  read_specs (file ? file : argv[0], FALSE);

  return NULL;
}
