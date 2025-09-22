/* Definitions of various defaults for tm.h macros.
   Copyright (C) 1992, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005
   Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@monkeys.com)

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
02110-1301, USA.  */

#ifndef GCC_DEFAULTS_H
#define GCC_DEFAULTS_H

#ifndef GET_ENVIRONMENT
#define GET_ENVIRONMENT(VALUE, NAME) do { (VALUE) = getenv (NAME); } while (0)
#endif

#define obstack_chunk_alloc	((void *(*) (long)) xmalloc)
#define obstack_chunk_free	((void (*) (void *)) free)
#define OBSTACK_CHUNK_SIZE	0
#define gcc_obstack_init(OBSTACK)			\
  _obstack_begin ((OBSTACK), OBSTACK_CHUNK_SIZE, 0,	\
		  obstack_chunk_alloc,			\
		  obstack_chunk_free)

/* Store in OUTPUT a string (made with alloca) containing an
   assembler-name for a local static variable or function named NAME.
   LABELNO is an integer which is different for each call.  */

#ifndef ASM_PN_FORMAT
# ifndef NO_DOT_IN_LABEL
#  define ASM_PN_FORMAT "%s.%lu"
# else
#  ifndef NO_DOLLAR_IN_LABEL
#   define ASM_PN_FORMAT "%s$%lu"
#  else
#   define ASM_PN_FORMAT "__%s_%lu"
#  endif
# endif
#endif /* ! ASM_PN_FORMAT */

#ifndef ASM_FORMAT_PRIVATE_NAME
# define ASM_FORMAT_PRIVATE_NAME(OUTPUT, NAME, LABELNO) \
  do { const char *const name_ = (NAME); \
       char *const output_ = (OUTPUT) = \
	 (char *) alloca (strlen (name_) + 32); \
       sprintf (output_, ASM_PN_FORMAT, name_, (unsigned long)(LABELNO)); \
  } while (0)
#endif

/* Choose a reasonable default for ASM_OUTPUT_ASCII.  */

#ifndef ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(MYFILE, MYSTRING, MYLENGTH) \
  do {									      \
    FILE *_hide_asm_out_file = (MYFILE);				      \
    const unsigned char *_hide_p = (const unsigned char *) (MYSTRING);	      \
    int _hide_thissize = (MYLENGTH);					      \
    {									      \
      FILE *asm_out_file = _hide_asm_out_file;				      \
      const unsigned char *p = _hide_p;					      \
      int thissize = _hide_thissize;					      \
      int i;								      \
      fprintf (asm_out_file, "\t.ascii \"");				      \
									      \
      for (i = 0; i < thissize; i++)					      \
	{								      \
	  int c = p[i];			   				      \
	  if (c == '\"' || c == '\\')					      \
	    putc ('\\', asm_out_file);					      \
	  if (ISPRINT(c))						      \
	    putc (c, asm_out_file);					      \
	  else								      \
	    {								      \
	      fprintf (asm_out_file, "\\%o", c);			      \
	      /* After an octal-escape, if a digit follows,		      \
		 terminate one string constant and start another.	      \
		 The VAX assembler fails to stop reading the escape	      \
		 after three digits, so this is the only way we		      \
		 can get it to parse the data properly.  */		      \
	      if (i < thissize - 1 && ISDIGIT(p[i + 1]))		      \
		fprintf (asm_out_file, "\"\n\t.ascii \"");		      \
	  }								      \
	}								      \
      fprintf (asm_out_file, "\"\n");					      \
    }									      \
  }									      \
  while (0)
#endif

/* This is how we tell the assembler to equate two values.  */
#ifdef SET_ASM_OP
#ifndef ASM_OUTPUT_DEF
#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
 do {	fprintf ((FILE), "%s", SET_ASM_OP);				\
	assemble_name (FILE, LABEL1);					\
	fprintf (FILE, ",");						\
	assemble_name (FILE, LABEL2);					\
	fprintf (FILE, "\n");						\
  } while (0)
#endif
#endif

#if defined (HAVE_AS_TLS) && !defined (ASM_OUTPUT_TLS_COMMON)
#define ASM_OUTPUT_TLS_COMMON(FILE, DECL, NAME, SIZE)			\
  do									\
    {									\
      fprintf ((FILE), "\t.tls_common\t");				\
      assemble_name ((FILE), (NAME));					\
      fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",%u\n",		\
	       (SIZE), DECL_ALIGN (DECL) / BITS_PER_UNIT);		\
    }									\
  while (0)
#endif

/* Decide whether to defer emitting the assembler output for an equate
   of two values.  The default is to not defer output.  */
#ifndef TARGET_DEFERRED_OUTPUT_DEFS
#define TARGET_DEFERRED_OUTPUT_DEFS(DECL,TARGET) false
#endif

/* This is how to output the definition of a user-level label named
   NAME, such as the label on a static function or variable NAME.  */

#ifndef ASM_OUTPUT_LABEL
#define ASM_OUTPUT_LABEL(FILE,NAME) \
  do { assemble_name ((FILE), (NAME)); fputs (":\n", (FILE)); } while (0)
#endif

/* Output the definition of a compiler-generated label named NAME.  */
#ifndef ASM_OUTPUT_INTERNAL_LABEL
#define ASM_OUTPUT_INTERNAL_LABEL(FILE,NAME)	\
  do {						\
    assemble_name_raw ((FILE), (NAME));		\
    fputs (":\n", (FILE));			\
  } while (0)
#endif

/* This is how to output a reference to a user-level label named NAME.  */

#ifndef ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(FILE,NAME)  asm_fprintf ((FILE), "%U%s", (NAME))
#endif

/* Allow target to print debug info labels specially.  This is useful for
   VLIW targets, since debug info labels should go into the middle of
   instruction bundles instead of breaking them.  */

#ifndef ASM_OUTPUT_DEBUG_LABEL
#define ASM_OUTPUT_DEBUG_LABEL(FILE, PREFIX, NUM) \
  (*targetm.asm_out.internal_label) (FILE, PREFIX, NUM)
#endif

/* This is how we tell the assembler that a symbol is weak.  */
#ifndef ASM_OUTPUT_WEAK_ALIAS
#if defined (ASM_WEAKEN_LABEL) && defined (ASM_OUTPUT_DEF)
#define ASM_OUTPUT_WEAK_ALIAS(STREAM, NAME, VALUE)	\
  do							\
    {							\
      ASM_WEAKEN_LABEL (STREAM, NAME);			\
      if (VALUE)					\
        ASM_OUTPUT_DEF (STREAM, NAME, VALUE);		\
    }							\
  while (0)
#endif
#endif

/* This is how we tell the assembler that a symbol is a weak alias to
   another symbol that doesn't require the other symbol to be defined.
   Uses of the former will turn into weak uses of the latter, i.e.,
   uses that, in case the latter is undefined, will not cause errors,
   and will add it to the symbol table as weak undefined.  However, if
   the latter is referenced directly, a strong reference prevails.  */
#ifndef ASM_OUTPUT_WEAKREF
#if defined HAVE_GAS_WEAKREF
#define ASM_OUTPUT_WEAKREF(FILE, DECL, NAME, VALUE)			\
  do									\
    {									\
      fprintf ((FILE), "\t.weakref\t");					\
      assemble_name ((FILE), (NAME));					\
      fprintf ((FILE), ",");						\
      assemble_name ((FILE), (VALUE));					\
      fprintf ((FILE), "\n");						\
    }									\
  while (0)
#endif
#endif

/* How to emit a .type directive.  */
#ifndef ASM_OUTPUT_TYPE_DIRECTIVE
#if defined TYPE_ASM_OP && defined TYPE_OPERAND_FMT
#define ASM_OUTPUT_TYPE_DIRECTIVE(STREAM, NAME, TYPE)	\
  do							\
    {							\
      fputs (TYPE_ASM_OP, STREAM);			\
      assemble_name (STREAM, NAME);			\
      fputs (", ", STREAM);				\
      fprintf (STREAM, TYPE_OPERAND_FMT, TYPE);		\
      putc ('\n', STREAM);				\
    }							\
  while (0)
#endif
#endif

/* How to emit a .size directive.  */
#ifndef ASM_OUTPUT_SIZE_DIRECTIVE
#ifdef SIZE_ASM_OP
#define ASM_OUTPUT_SIZE_DIRECTIVE(STREAM, NAME, SIZE)	\
  do							\
    {							\
      HOST_WIDE_INT size_ = (SIZE);			\
      fputs (SIZE_ASM_OP, STREAM);			\
      assemble_name (STREAM, NAME);			\
      fprintf (STREAM, ", " HOST_WIDE_INT_PRINT_DEC "\n", size_); \
    }							\
  while (0)

#define ASM_OUTPUT_MEASURED_SIZE(STREAM, NAME)		\
  do							\
    {							\
      fputs (SIZE_ASM_OP, STREAM);			\
      assemble_name (STREAM, NAME);			\
      fputs (", .-", STREAM);				\
      assemble_name (STREAM, NAME);			\
      putc ('\n', STREAM);				\
    }							\
  while (0)

#endif
#endif

/* This determines whether or not we support weak symbols.  */
#ifndef SUPPORTS_WEAK
#if defined (ASM_WEAKEN_LABEL) || defined (ASM_WEAKEN_DECL)
#define SUPPORTS_WEAK 1
#else
#define SUPPORTS_WEAK 0
#endif
#endif

/* This determines whether or not we support link-once semantics.  */
#ifndef SUPPORTS_ONE_ONLY
#ifdef MAKE_DECL_ONE_ONLY
#define SUPPORTS_ONE_ONLY 1
#else
#define SUPPORTS_ONE_ONLY 0
#endif
#endif

/* This determines whether weak symbols must be left out of a static
   archive's table of contents.  Defining this macro to be nonzero has
   the consequence that certain symbols will not be made weak that
   otherwise would be.  The C++ ABI requires this macro to be zero;
   see the documentation.  */
#ifndef TARGET_WEAK_NOT_IN_ARCHIVE_TOC
#define TARGET_WEAK_NOT_IN_ARCHIVE_TOC 0
#endif

/* This determines whether or not we need linkonce unwind information.  */
#ifndef TARGET_USES_WEAK_UNWIND_INFO
#define TARGET_USES_WEAK_UNWIND_INFO 0
#endif

/* By default, there is no prefix on user-defined symbols.  */
#ifndef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""
#endif

/* If the target supports weak symbols, define TARGET_ATTRIBUTE_WEAK to
   provide a weak attribute.  Else define it to nothing.

   This would normally belong in ansidecl.h, but SUPPORTS_WEAK is
   not available at that time.

   Note, this is only for use by target files which we know are to be
   compiled by GCC.  */
#ifndef TARGET_ATTRIBUTE_WEAK
# if SUPPORTS_WEAK
#  define TARGET_ATTRIBUTE_WEAK __attribute__ ((weak))
# else
#  define TARGET_ATTRIBUTE_WEAK
# endif
#endif

/* Determines whether we may use common symbols to represent one-only
   semantics (a.k.a. "vague linkage").  */
#ifndef USE_COMMON_FOR_ONE_ONLY
# define USE_COMMON_FOR_ONE_ONLY 1
#endif

/* By default we can assume that all global symbols are in one namespace,
   across all shared libraries.  */
#ifndef MULTIPLE_SYMBOL_SPACES
# define MULTIPLE_SYMBOL_SPACES 0
#endif

/* If the target supports init_priority C++ attribute, give
   SUPPORTS_INIT_PRIORITY a nonzero value.  */
#ifndef SUPPORTS_INIT_PRIORITY
#define SUPPORTS_INIT_PRIORITY 1
#endif /* SUPPORTS_INIT_PRIORITY */

/* If duplicate library search directories can be removed from a
   linker command without changing the linker's semantics, give this
   symbol a nonzero.  */
#ifndef LINK_ELIMINATE_DUPLICATE_LDIRECTORIES
#define LINK_ELIMINATE_DUPLICATE_LDIRECTORIES 0
#endif /* LINK_ELIMINATE_DUPLICATE_LDIRECTORIES */

/* If we have a definition of INCOMING_RETURN_ADDR_RTX, assume that
   the rest of the DWARF 2 frame unwind support is also provided.  */
#if !defined (DWARF2_UNWIND_INFO) && defined (INCOMING_RETURN_ADDR_RTX)
#define DWARF2_UNWIND_INFO 1
#endif

/* If we have named sections, and we're using crtstuff to run ctors,
   use them for registering eh frame information.  */
#if defined (TARGET_ASM_NAMED_SECTION) && DWARF2_UNWIND_INFO \
    && !defined(EH_FRAME_IN_DATA_SECTION)
#ifndef EH_FRAME_SECTION_NAME
#define EH_FRAME_SECTION_NAME ".eh_frame"
#endif
#endif

/* On many systems, different EH table encodings are used under
   difference circumstances.  Some will require runtime relocations;
   some will not.  For those that do not require runtime relocations,
   we would like to make the table read-only.  However, since the
   read-only tables may need to be combined with read-write tables
   that do require runtime relocation, it is not safe to make the
   tables read-only unless the linker will merge read-only and
   read-write sections into a single read-write section.  If your
   linker does not have this ability, but your system is such that no
   encoding used with non-PIC code will ever require a runtime
   relocation, then you can define EH_TABLES_CAN_BE_READ_ONLY to 1 in
   your target configuration file.  */
#ifndef EH_TABLES_CAN_BE_READ_ONLY
#ifdef HAVE_LD_RO_RW_SECTION_MIXING
#define EH_TABLES_CAN_BE_READ_ONLY 1
#else
#define EH_TABLES_CAN_BE_READ_ONLY 0
#endif
#endif

/* If we have named section and we support weak symbols, then use the
   .jcr section for recording java classes which need to be registered
   at program start-up time.  */
#if defined (TARGET_ASM_NAMED_SECTION) && SUPPORTS_WEAK
#ifndef JCR_SECTION_NAME
#define JCR_SECTION_NAME ".jcr"
#endif
#endif

/* This decision to use a .jcr section can be overridden by defining
   USE_JCR_SECTION to 0 in target file.  This is necessary if target
   can define JCR_SECTION_NAME but does not have crtstuff or
   linker support for .jcr section.  */
#ifndef TARGET_USE_JCR_SECTION
#ifdef JCR_SECTION_NAME
#define TARGET_USE_JCR_SECTION 1
#else
#define TARGET_USE_JCR_SECTION 0
#endif
#endif

/* Number of hardware registers that go into the DWARF-2 unwind info.
   If not defined, equals FIRST_PSEUDO_REGISTER  */

#ifndef DWARF_FRAME_REGISTERS
#define DWARF_FRAME_REGISTERS FIRST_PSEUDO_REGISTER
#endif

/* How to renumber registers for dbx and gdb.  If not defined, assume
   no renumbering is necessary.  */

#ifndef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(REGNO) (REGNO)
#endif

/* Default sizes for base C types.  If the sizes are different for
   your target, you should override these values by defining the
   appropriate symbols in your tm.h file.  */

#ifndef BITS_PER_UNIT
#define BITS_PER_UNIT 8
#endif

#ifndef BITS_PER_WORD
#define BITS_PER_WORD (BITS_PER_UNIT * UNITS_PER_WORD)
#endif

#ifndef CHAR_TYPE_SIZE
#define CHAR_TYPE_SIZE BITS_PER_UNIT
#endif

#ifndef BOOL_TYPE_SIZE
/* `bool' has size and alignment `1', on almost all platforms.  */
#define BOOL_TYPE_SIZE CHAR_TYPE_SIZE
#endif

#ifndef SHORT_TYPE_SIZE
#define SHORT_TYPE_SIZE (BITS_PER_UNIT * MIN ((UNITS_PER_WORD + 1) / 2, 2))
#endif

#ifndef INT_TYPE_SIZE
#define INT_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef LONG_TYPE_SIZE
#define LONG_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef LONG_LONG_TYPE_SIZE
#define LONG_LONG_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

#ifndef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE INT_TYPE_SIZE
#endif

#ifndef FLOAT_TYPE_SIZE
#define FLOAT_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef DOUBLE_TYPE_SIZE
#define DOUBLE_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

#ifndef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE (BITS_PER_WORD * 2)
#endif

#ifndef DECIMAL32_TYPE_SIZE
#define DECIMAL32_TYPE_SIZE 32
#endif 

#ifndef DECIMAL64_TYPE_SIZE 
#define DECIMAL64_TYPE_SIZE 64
#endif 

#ifndef DECIMAL128_TYPE_SIZE
#define DECIMAL128_TYPE_SIZE 128
#endif

/* Width in bits of a pointer.  Mind the value of the macro `Pmode'.  */
#ifndef POINTER_SIZE
#define POINTER_SIZE BITS_PER_WORD
#endif

#ifndef PIC_OFFSET_TABLE_REGNUM
#define PIC_OFFSET_TABLE_REGNUM INVALID_REGNUM
#endif

#ifndef TARGET_DLLIMPORT_DECL_ATTRIBUTES
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 0
#endif

#ifndef TARGET_DECLSPEC
#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
/* If the target supports the "dllimport" attribute, users are
   probably used to the "__declspec" syntax.  */
#define TARGET_DECLSPEC 1
#else
#define TARGET_DECLSPEC 0
#endif
#endif

/* By default, the preprocessor should be invoked the same way in C++
   as in C.  */
#ifndef CPLUSPLUS_CPP_SPEC
#ifdef CPP_SPEC
#define CPLUSPLUS_CPP_SPEC CPP_SPEC
#endif
#endif

#ifndef ACCUMULATE_OUTGOING_ARGS
#define ACCUMULATE_OUTGOING_ARGS 0
#endif

/* Supply a default definition for PUSH_ARGS.  */
#ifndef PUSH_ARGS
#ifdef PUSH_ROUNDING
#define PUSH_ARGS	!ACCUMULATE_OUTGOING_ARGS
#else
#define PUSH_ARGS	0
#endif
#endif

/* Decide whether a function's arguments should be processed
   from first to last or from last to first.

   They should if the stack and args grow in opposite directions, but
   only if we have push insns.  */

#ifdef PUSH_ROUNDING

#ifndef PUSH_ARGS_REVERSED
#if defined (STACK_GROWS_DOWNWARD) != defined (ARGS_GROW_DOWNWARD)
#define PUSH_ARGS_REVERSED  PUSH_ARGS
#endif
#endif

#endif

#ifndef PUSH_ARGS_REVERSED
#define PUSH_ARGS_REVERSED 0
#endif

/* If PREFERRED_STACK_BOUNDARY is not defined, set it to STACK_BOUNDARY.
   STACK_BOUNDARY is required.  */
#ifndef PREFERRED_STACK_BOUNDARY
#define PREFERRED_STACK_BOUNDARY STACK_BOUNDARY
#endif

#ifndef TARGET_DEFAULT_PACK_STRUCT
#define TARGET_DEFAULT_PACK_STRUCT 0
#endif

/* By default, the C++ compiler will use function addresses in the
   vtable entries.  Setting this nonzero tells the compiler to use
   function descriptors instead.  The value of this macro says how
   many words wide the descriptor is (normally 2).  It is assumed
   that the address of a function descriptor may be treated as a
   pointer to a function.  */
#ifndef TARGET_VTABLE_USES_DESCRIPTORS
#define TARGET_VTABLE_USES_DESCRIPTORS 0
#endif

/* By default, the vtable entries are void pointers, the so the alignment
   is the same as pointer alignment.  The value of this macro specifies
   the alignment of the vtable entry in bits.  It should be defined only
   when special alignment is necessary.  */
#ifndef TARGET_VTABLE_ENTRY_ALIGN
#define TARGET_VTABLE_ENTRY_ALIGN POINTER_SIZE
#endif

/* There are a few non-descriptor entries in the vtable at offsets below
   zero.  If these entries must be padded (say, to preserve the alignment
   specified by TARGET_VTABLE_ENTRY_ALIGN), set this to the number of
   words in each data entry.  */
#ifndef TARGET_VTABLE_DATA_ENTRY_DISTANCE
#define TARGET_VTABLE_DATA_ENTRY_DISTANCE 1
#endif

/* Decide whether it is safe to use a local alias for a virtual function
   when constructing thunks.  */
#ifndef TARGET_USE_LOCAL_THUNK_ALIAS_P
#ifdef ASM_OUTPUT_DEF
#define TARGET_USE_LOCAL_THUNK_ALIAS_P(DECL) 1
#else
#define TARGET_USE_LOCAL_THUNK_ALIAS_P(DECL) 0
#endif
#endif

/* Select a format to encode pointers in exception handling data.  We
   prefer those that result in fewer dynamic relocations.  Assume no
   special support here and encode direct references.  */
#ifndef ASM_PREFERRED_EH_DATA_FORMAT
#define ASM_PREFERRED_EH_DATA_FORMAT(CODE,GLOBAL)  DW_EH_PE_absptr
#endif

/* By default, the C++ compiler will use the lowest bit of the pointer
   to function to indicate a pointer-to-member-function points to a
   virtual member function.  However, if FUNCTION_BOUNDARY indicates
   function addresses aren't always even, the lowest bit of the delta
   field will be used.  */
#ifndef TARGET_PTRMEMFUNC_VBIT_LOCATION
#define TARGET_PTRMEMFUNC_VBIT_LOCATION \
  (FUNCTION_BOUNDARY >= 2 * BITS_PER_UNIT \
   ? ptrmemfunc_vbit_in_pfn : ptrmemfunc_vbit_in_delta)
#endif

#ifndef DEFAULT_GDB_EXTENSIONS
#define DEFAULT_GDB_EXTENSIONS 1
#endif

/* If more than one debugging type is supported, you must define
   PREFERRED_DEBUGGING_TYPE to choose the default.  */

#if 1 < (defined (DBX_DEBUGGING_INFO) + defined (SDB_DEBUGGING_INFO) \
         + defined (DWARF2_DEBUGGING_INFO) + defined (XCOFF_DEBUGGING_INFO) \
         + defined (VMS_DEBUGGING_INFO))
#ifndef PREFERRED_DEBUGGING_TYPE
#error You must define PREFERRED_DEBUGGING_TYPE
#endif /* no PREFERRED_DEBUGGING_TYPE */

/* If only one debugging format is supported, define PREFERRED_DEBUGGING_TYPE
   here so other code needn't care.  */
#elif defined DBX_DEBUGGING_INFO
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#elif defined SDB_DEBUGGING_INFO
#define PREFERRED_DEBUGGING_TYPE SDB_DEBUG

#elif defined DWARF2_DEBUGGING_INFO
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG

#elif defined VMS_DEBUGGING_INFO
#define PREFERRED_DEBUGGING_TYPE VMS_AND_DWARF2_DEBUG

#elif defined XCOFF_DEBUGGING_INFO
#define PREFERRED_DEBUGGING_TYPE XCOFF_DEBUG

#else
/* No debugging format is supported by this target.  */
#define PREFERRED_DEBUGGING_TYPE NO_DEBUG
#endif

/* Define codes for all the float formats that we know of.  */
#define UNKNOWN_FLOAT_FORMAT 0
#define IEEE_FLOAT_FORMAT 1
#define VAX_FLOAT_FORMAT 2
#define IBM_FLOAT_FORMAT 3
#define C4X_FLOAT_FORMAT 4

/* Default to IEEE float if not specified.  Nearly all machines use it.  */
#ifndef TARGET_FLOAT_FORMAT
#define	TARGET_FLOAT_FORMAT	IEEE_FLOAT_FORMAT
#endif

#ifndef LARGEST_EXPONENT_IS_NORMAL
#define LARGEST_EXPONENT_IS_NORMAL(SIZE) 0
#endif

#ifndef ROUND_TOWARDS_ZERO
#define ROUND_TOWARDS_ZERO 0
#endif

#ifndef MODE_HAS_NANS
#define MODE_HAS_NANS(MODE)					\
  (FLOAT_MODE_P (MODE)						\
   && TARGET_FLOAT_FORMAT == IEEE_FLOAT_FORMAT			\
   && !LARGEST_EXPONENT_IS_NORMAL (GET_MODE_BITSIZE (MODE)))
#endif

#ifndef MODE_HAS_INFINITIES
#define MODE_HAS_INFINITIES(MODE)				\
  (FLOAT_MODE_P (MODE)						\
   && TARGET_FLOAT_FORMAT == IEEE_FLOAT_FORMAT			\
   && !LARGEST_EXPONENT_IS_NORMAL (GET_MODE_BITSIZE (MODE)))
#endif

#ifndef MODE_HAS_SIGNED_ZEROS
#define MODE_HAS_SIGNED_ZEROS(MODE) \
  (FLOAT_MODE_P (MODE) && TARGET_FLOAT_FORMAT == IEEE_FLOAT_FORMAT)
#endif

#ifndef MODE_HAS_SIGN_DEPENDENT_ROUNDING
#define MODE_HAS_SIGN_DEPENDENT_ROUNDING(MODE)			\
  (FLOAT_MODE_P (MODE)						\
   && TARGET_FLOAT_FORMAT == IEEE_FLOAT_FORMAT			\
   && !ROUND_TOWARDS_ZERO)
#endif

#ifndef FLOAT_LIB_COMPARE_RETURNS_BOOL
#define FLOAT_LIB_COMPARE_RETURNS_BOOL(MODE, COMPARISON) false
#endif

/* True if the targets integer-comparison functions return { 0, 1, 2
   } to indicate { <, ==, > }.  False if { -1, 0, 1 } is used
   instead.  The libgcc routines are biased.  */
#ifndef TARGET_LIB_INT_CMP_BIASED
#define TARGET_LIB_INT_CMP_BIASED (true)
#endif

/* If FLOAT_WORDS_BIG_ENDIAN is not defined in the header files,
   then the word-endianness is the same as for integers.  */
#ifndef FLOAT_WORDS_BIG_ENDIAN
#define FLOAT_WORDS_BIG_ENDIAN WORDS_BIG_ENDIAN
#endif

#ifndef TARGET_FLT_EVAL_METHOD
#define TARGET_FLT_EVAL_METHOD 0
#endif

#ifndef TARGET_DEC_EVAL_METHOD
#define TARGET_DEC_EVAL_METHOD 2
#endif

#ifndef HOT_TEXT_SECTION_NAME
#define HOT_TEXT_SECTION_NAME ".text.hot"
#endif

#ifndef UNLIKELY_EXECUTED_TEXT_SECTION_NAME
#define UNLIKELY_EXECUTED_TEXT_SECTION_NAME ".text.unlikely"
#endif

#ifndef HAS_LONG_COND_BRANCH
#define HAS_LONG_COND_BRANCH 0
#endif

#ifndef HAS_LONG_UNCOND_BRANCH
#define HAS_LONG_UNCOND_BRANCH 0
#endif

/* By default, only attempt to parallelize bitwise operations, and
   possibly adds/subtracts using bit-twiddling.  */
#ifndef UNITS_PER_SIMD_WORD
#define UNITS_PER_SIMD_WORD UNITS_PER_WORD
#endif

/* Determine whether __cxa_atexit, rather than atexit, is used to
   register C++ destructors for local statics and global objects.  */
#ifndef DEFAULT_USE_CXA_ATEXIT
#define DEFAULT_USE_CXA_ATEXIT 0
#endif

/* If none of these macros are defined, the port must use the new
   technique of defining constraints in the machine description.
   tm_p.h will define those macros that machine-independent code
   still uses.  */
#if  !defined CONSTRAINT_LEN			\
  && !defined REG_CLASS_FROM_LETTER		\
  && !defined REG_CLASS_FROM_CONSTRAINT		\
  && !defined CONST_OK_FOR_LETTER_P		\
  && !defined CONST_OK_FOR_CONSTRAINT_P		\
  && !defined CONST_DOUBLE_OK_FOR_LETTER_P	\
  && !defined CONST_DOUBLE_OK_FOR_CONSTRAINT_P  \
  && !defined EXTRA_CONSTRAINT			\
  && !defined EXTRA_CONSTRAINT_STR		\
  && !defined EXTRA_MEMORY_CONSTRAINT		\
  && !defined EXTRA_ADDRESS_CONSTRAINT

#define USE_MD_CONSTRAINTS

#if GCC_VERSION >= 3000 && defined IN_GCC
/* These old constraint macros shouldn't appear anywhere in a
   configuration using MD constraint definitions.  */
#pragma GCC poison REG_CLASS_FROM_LETTER CONST_OK_FOR_LETTER_P \
                   CONST_DOUBLE_OK_FOR_LETTER_P EXTRA_CONSTRAINT
#endif

#else /* old constraint mechanism in use */

/* Determine whether extra constraint letter should be handled
   via address reload (like 'o').  */
#ifndef EXTRA_MEMORY_CONSTRAINT
#define EXTRA_MEMORY_CONSTRAINT(C,STR) 0
#endif

/* Determine whether extra constraint letter should be handled
   as an address (like 'p').  */
#ifndef EXTRA_ADDRESS_CONSTRAINT
#define EXTRA_ADDRESS_CONSTRAINT(C,STR) 0
#endif

/* When a port defines CONSTRAINT_LEN, it should use DEFAULT_CONSTRAINT_LEN
   for all the characters that it does not want to change, so things like the
  'length' of a digit in a matching constraint is an implementation detail,
   and not part of the interface.  */
#define DEFAULT_CONSTRAINT_LEN(C,STR) 1

#ifndef CONSTRAINT_LEN
#define CONSTRAINT_LEN(C,STR) DEFAULT_CONSTRAINT_LEN (C, STR)
#endif

#if defined (CONST_OK_FOR_LETTER_P) && ! defined (CONST_OK_FOR_CONSTRAINT_P)
#define CONST_OK_FOR_CONSTRAINT_P(VAL,C,STR) CONST_OK_FOR_LETTER_P (VAL, C)
#endif

#if defined (CONST_DOUBLE_OK_FOR_LETTER_P) && ! defined (CONST_DOUBLE_OK_FOR_CONSTRAINT_P)
#define CONST_DOUBLE_OK_FOR_CONSTRAINT_P(OP,C,STR) \
  CONST_DOUBLE_OK_FOR_LETTER_P (OP, C)
#endif

#ifndef REG_CLASS_FROM_CONSTRAINT
#define REG_CLASS_FROM_CONSTRAINT(C,STR) REG_CLASS_FROM_LETTER (C)
#endif

#if defined (EXTRA_CONSTRAINT) && ! defined (EXTRA_CONSTRAINT_STR)
#define EXTRA_CONSTRAINT_STR(OP, C,STR) EXTRA_CONSTRAINT (OP, C)
#endif

#endif /* old constraint mechanism in use */

#ifndef REGISTER_MOVE_COST
#define REGISTER_MOVE_COST(m, x, y) 2
#endif

/* Determine whether the entire c99 runtime
   is present in the runtime library.  */
#ifndef TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS 0
#endif

/* Indicate that CLZ and CTZ are undefined at zero.  */
#ifndef CLZ_DEFINED_VALUE_AT_ZERO
#define CLZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE)  0
#endif
#ifndef CTZ_DEFINED_VALUE_AT_ZERO
#define CTZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE)  0
#endif

/* Provide a default value for STORE_FLAG_VALUE.  */
#ifndef STORE_FLAG_VALUE
#define STORE_FLAG_VALUE  1
#endif

/* This macro is used to determine what the largest unit size that
   move_by_pieces can use is.  */

/* MOVE_MAX_PIECES is the number of bytes at a time which we can
   move efficiently, as opposed to  MOVE_MAX which is the maximum
   number of bytes we can move with a single instruction.  */

#ifndef MOVE_MAX_PIECES
#define MOVE_MAX_PIECES   MOVE_MAX
#endif

#ifndef STACK_POINTER_OFFSET
#define STACK_POINTER_OFFSET    0
#endif

#ifndef LOCAL_REGNO
#define LOCAL_REGNO(REGNO)  0
#endif

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.  */
#ifndef EXIT_IGNORE_STACK
#define EXIT_IGNORE_STACK 0
#endif

/* Assume that case vectors are not pc-relative.  */
#ifndef CASE_VECTOR_PC_RELATIVE
#define CASE_VECTOR_PC_RELATIVE 0
#endif

/* Assume that trampolines need function alignment.  */
#ifndef TRAMPOLINE_ALIGNMENT
#define TRAMPOLINE_ALIGNMENT FUNCTION_BOUNDARY
#endif

/* Register mappings for target machines without register windows.  */
#ifndef INCOMING_REGNO
#define INCOMING_REGNO(N) (N)
#endif

#ifndef OUTGOING_REGNO
#define OUTGOING_REGNO(N) (N)
#endif

#ifndef SHIFT_COUNT_TRUNCATED
#define SHIFT_COUNT_TRUNCATED 0
#endif

#ifndef LEGITIMIZE_ADDRESS
#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)
#endif

#ifndef LEGITIMATE_PIC_OPERAND_P
#define LEGITIMATE_PIC_OPERAND_P(X) 1
#endif

#ifndef REVERSIBLE_CC_MODE
#define REVERSIBLE_CC_MODE(MODE) 0
#endif

/* Biggest alignment supported by the object file format of this machine.  */
#ifndef MAX_OFILE_ALIGNMENT
#define MAX_OFILE_ALIGNMENT BIGGEST_ALIGNMENT
#endif

#ifndef FRAME_GROWS_DOWNWARD
#define FRAME_GROWS_DOWNWARD 0
#endif

/* On most machines, the CFA coincides with the first incoming parm.  */
#ifndef ARG_POINTER_CFA_OFFSET
#define ARG_POINTER_CFA_OFFSET(FNDECL) FIRST_PARM_OFFSET (FNDECL)
#endif

/* On most machines, we use the CFA as DW_AT_frame_base.  */
#ifndef CFA_FRAME_BASE_OFFSET
#define CFA_FRAME_BASE_OFFSET(FNDECL) 0
#endif

/* The offset from the incoming value of %sp to the top of the stack frame
   for the current function.  */
#ifndef INCOMING_FRAME_SP_OFFSET
#define INCOMING_FRAME_SP_OFFSET 0
#endif

#ifndef HARD_REGNO_NREGS_HAS_PADDING
#define HARD_REGNO_NREGS_HAS_PADDING(REGNO, MODE) 0
#define HARD_REGNO_NREGS_WITH_PADDING(REGNO, MODE) -1
#endif

#ifndef PIE_DEFAULT
#define PIE_DEFAULT 0
#endif

#ifndef JUMP_TABLES_DEFAULT
#define JUMP_TABLES_DEFAULT 1
#endif

#endif  /* ! GCC_DEFAULTS_H */
