/* Definitions of target machine for GNU compiler, for DEC Alpha on Cray
   T3E running Unicos/Mk.
   Copyright (C) 2001, 2002, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Roman Lechtchinsky (rl@cs.tu-berlin.de)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#undef TARGET_ABI_UNICOSMK
#define TARGET_ABI_UNICOSMK 1

/* CAM requires a slash before floating-pointing instruction suffixes.  */

#undef TARGET_AS_SLASH_BEFORE_SUFFIX
#define TARGET_AS_SLASH_BEFORE_SUFFIX 1

/* The following defines are necessary for the standard headers to work
   correctly.  */

#define TARGET_OS_CPP_BUILTINS()				\
    do {							\
	builtin_define ("__unix");				\
	builtin_define ("_UNICOS=205");				\
	builtin_define ("_CRAY");				\
	builtin_define ("_CRAYT3E");				\
	builtin_define ("_CRAYMPP");				\
	builtin_define ("_CRAYIEEE");				\
	builtin_define ("_ADDR64");				\
	builtin_define ("_LD64");				\
	builtin_define ("__UNICOSMK__");			\
    } while (0)

#define SHORT_TYPE_SIZE 32

#undef INT_TYPE_SIZE
#define INT_TYPE_SIZE 64

/* This is consistent with the definition Cray CC uses.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 64

/*
#define SIZE_TYPE "unsigned int"
#define PTRDIFF_TYPE "int"
*/

/* Alphas are operated in big endian mode on the Cray T3E.  */

#undef BITS_BIG_ENDIAN
#undef BYTES_BIG_ENDIAN
#undef WORDS_BIG_ENDIAN
#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 1
#define WORDS_BIG_ENDIAN 1


/* Every structure's size must be a multiple of this.  */

#undef STRUCTURE_SIZE_BOUNDARY
#define STRUCTURE_SIZE_BOUNDARY 64

/* No data type wants to be aligned rounder than this.  */

#undef BIGGEST_ALIGNMENT
#define BIGGEST_ALIGNMENT 256

/* Include the frame pointer in fixed_regs and call_used_regs as it can't be 
   used as a general-purpose register even in frameless functions.
   ??? The global_regs hack is needed for now because -O2 sometimes tries to 
   eliminate $15 increments/decrements in frameless functions.  */

#undef CONDITIONAL_REGISTER_USAGE
#define CONDITIONAL_REGISTER_USAGE	\
  do {					\
    fixed_regs[15] = 1;			\
    call_used_regs[15] = 1;		\
    global_regs[15] = 1;		\
  } while(0)

/* The stack frame grows downward.  */

#define FRAME_GROWS_DOWNWARD 1

/* Define the offset between two registers, one to be eliminated, and the
   other its replacement, at the start of a routine. This is somewhat
   complicated on the T3E which is why we use a function.  */

#undef INITIAL_ELIMINATION_OFFSET
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  do {									\
    (OFFSET) = unicosmk_initial_elimination_offset ((FROM), (TO));	\
  } while (0)


/* Define this if stack space is still allocated for a parameter passed
   in a register. On the T3E, stack space is preallocated for all outgoing
   arguments, including those passed in registers. To avoid problems, we
   assume that at least 48 bytes (i.e. enough space for all arguments passed
   in registers) are allocated.  */

#define REG_PARM_STACK_SPACE(DECL) 48
#define OUTGOING_REG_PARM_STACK_SPACE

/* If an argument can't be passed in registers even though not all argument
   registers have been used yet, it is passed on the stack in the space 
   preallocated for these registers.  */

#define STACK_PARMS_IN_REG_PARM_AREA

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On Unicos/Mk, this is a structure that contains various information for
   the static subroutine information block (SSIB) and the call information
   word (CIW).  */

typedef struct {

  /* The overall number of arguments.  */
  int num_args;

  /* The overall size of the arguments in words.  */
  int num_arg_words;

  /* The number of words passed in registers.  */
  int num_reg_words;

  /* If an argument must be passed in the stack, all subsequent arguments
     must be passed there, too. This flag indicates whether this is the
     case.  */
  int force_stack;

  /* This array indicates whether a word is passed in an integer register or
     a floating point one.  */

  /* For each of the 6 register arguments, the corresponding flag in this
     array indicates whether the argument is passed in an integer or a
     floating point register.  */
  int reg_args_type[6];

} unicosmk_arg_info;

#undef CUMULATIVE_ARGS
#define CUMULATIVE_ARGS unicosmk_arg_info

/* Initialize a variable CUM of type CUMULATIVE_ARGS for a call to a
   function whose data type is FNTYPE.  For a library call, FNTYPE is 0.  */

#undef INIT_CUMULATIVE_ARGS
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  do { (CUM).num_args = 0;					\
       (CUM).num_arg_words = 0;					\
       (CUM).num_reg_words = 0;					\
       (CUM).force_stack = 0;					\
  } while(0)

/* Update the data in CUM to advance over an argument of mode MODE and data
   type TYPE. (TYPE is null for libcalls where that information may not be
   available.)

   On Unicos/Mk, at most 6 words can be passed in registers. Structures
   which fit in two words are passed in registers, larger structures are
   passed on stack.  */

#undef FUNCTION_ARG_ADVANCE
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)		\
do {								\
  int size;							\
								\
  size = ALPHA_ARG_SIZE (MODE, TYPE, NAMED);			\
                                                                \
  if (size > 2							\
      || (CUM).num_reg_words + size > 6				\
      || targetm.calls.must_pass_in_stack (MODE, TYPE))		\
    (CUM).force_stack = 1;					\
                                                                \
  if (! (CUM).force_stack)					\
    {								\
      int i;							\
      int isfloat;						\
      isfloat = (GET_MODE_CLASS (MODE) == MODE_COMPLEX_FLOAT	\
              || GET_MODE_CLASS (MODE) == MODE_FLOAT);		\
      for (i = 0; i < size; i++)				\
        {							\
          (CUM).reg_args_type[(CUM).num_reg_words] = isfloat;	\
          ++(CUM).num_reg_words;				\
        }							\
    }								\
  (CUM).num_arg_words += size;					\
  ++(CUM).num_args;						\
} while(0)

/* This ensures that $15 increments/decrements in leaf functions won't get
   eliminated.  */

#undef EPILOGUE_USES
#define EPILOGUE_USES(REGNO)  ((REGNO) == 26 || (REGNO) == 15)

/* Would have worked, only the stack doesn't seem to be executable
#undef TRAMPOLINE_TEMPLATE
#define TRAMPOLINE_TEMPLATE(FILE)			\
do { fprintf (FILE, "\tbr $1,0\n");			\
     fprintf (FILE, "\tldq $0,12($1)\n");		\
     fprintf (FILE, "\tldq $1,20($1)\n");		\
     fprintf (FILE, "\tjmp $31,(r0)\n");		\
     fprintf (FILE, "\tbis $31,$31,$31\n");		\
     fprintf (FILE, "\tbis $31,$31,$31\n");		\
} while (0) */

/* We don't support nested functions (yet).  */

#undef TRAMPOLINE_TEMPLATE
#define TRAMPOLINE_TEMPLATE(FILE) gcc_unreachable ()

/* Specify the machine mode that this machine uses for the index in the
   tablejump instruction. On Unicos/Mk, we don't support relative case
   vectors yet, thus the entries should be absolute addresses.  */ 

#undef CASE_VECTOR_MODE
#define CASE_VECTOR_MODE DImode

#undef CASE_VECTOR_PC_RELATIVE

/* Define this as 1 if `char' should by default be signed; else as 0.  */
/* #define DEFAULT_SIGNED_CHAR 1 */

/* There are no read-only sections on Unicos/Mk.  */

#undef READONLY_DATA_SECTION_ASM_OP

/* We take care of this in unicosmk_file_start.  */

#undef ASM_OUTPUT_SOURCE_FILENAME

/* This is how to output a label for a jump table.  Arguments are the same as
   for (*targetm.asm_out.internal_label), except the insn for the jump table is
   passed.  */

#undef ASM_OUTPUT_CASE_LABEL
#define ASM_OUTPUT_CASE_LABEL(FILE,PREFIX,NUM,TABLEINSN)	\
  (*targetm.asm_out.internal_label) (FILE, PREFIX, NUM)

/* CAM has some restrictions with respect to string literals. It won't
   accept lines with more that 256 characters which means that we have
   to split long strings. Moreover, it only accepts escape sequences of
   the form \nnn in the range 0 to 127. We generate .byte directives for
   escapes characters greater than 127. And finally, ` must be escaped.  */

#undef ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(MYFILE, MYSTRING, MYLENGTH) \
  do {									      \
    FILE *_hide_asm_out_file = (MYFILE);				      \
    const unsigned char *_hide_p = (const unsigned char *) (MYSTRING);	      \
    int _hide_thissize = (MYLENGTH);					      \
    int _size_so_far = 0;						      \
    {									      \
      FILE *asm_out_file = _hide_asm_out_file;				      \
      const unsigned char *p = _hide_p;					      \
      int thissize = _hide_thissize;					      \
      int in_ascii = 0;							      \
      int i;								      \
									      \
      for (i = 0; i < thissize; i++)					      \
	{								      \
	  register int c = p[i];					      \
									      \
	  if (c > 127)							      \
	    {								      \
	      if (in_ascii)						      \
		{							      \
		  fprintf (asm_out_file, "\"\n");			      \
		  in_ascii = 0;						      \
		}							      \
									      \
	      fprintf (asm_out_file, "\t.byte\t%d\n", c);		      \
	    }								      \
	  else								      \
	    {								      \
	      if (! in_ascii)						      \
		{							      \
		  fprintf (asm_out_file, "\t.ascii\t\"");		      \
		  in_ascii = 1;						      \
		  _size_so_far = 0;					      \
		}							      \
	      else if (_size_so_far >= 64)				      \
		{							      \
		  fprintf (asm_out_file, "\"\n\t.ascii\t\"");		      \
		  _size_so_far = 0;					      \
		}							      \
									      \
	      if (c == '\"' || c == '\\' || c == '`')			      \
		putc ('\\', asm_out_file);				      \
	      if (c >= ' ')						      \
		putc (c, asm_out_file);					      \
	      else							      \
		fprintf (asm_out_file, "\\%.3o", c);			      \
	      ++ _size_so_far;						      \
	    }								      \
	}								      \
      if (in_ascii)							      \
	fprintf (asm_out_file, "\"\n");					      \
    }									      \
  } while(0)

/* This is how to output an element of a case-vector that is absolute.  */

#undef ASM_OUTPUT_ADDR_VEC_ELT
#define ASM_OUTPUT_ADDR_VEC_ELT(FILE, VALUE)	\
  fprintf (FILE, "\t.quad $L%d\n", (VALUE))

/* This is how to output an element of a case-vector that is relative.
   (Unicos/Mk does not use such vectors yet).  */

#undef ASM_OUTPUT_ADDR_DIFF_ELT
#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) gcc_unreachable ()

/* We can't output case vectors in the same section as the function code
   because CAM doesn't allow data definitions in code sections. Thus, we
   simply record the case vectors and put them in a separate section after
   the function.  */

#define ASM_OUTPUT_ADDR_VEC(LAB,VEC) \
  unicosmk_defer_case_vector ((LAB),(VEC))

#define ASM_OUTPUT_ADDR_DIFF_VEC(LAB,VEC) gcc_unreachable ()

/* This is how to output an assembler line that says to advance the location
   counter to a multiple of 2**LOG bytes. Annoyingly, CAM always uses zeroes
   to fill the unused space which does not work in code sections. We have to 
   be careful not to use the .align directive in code sections.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(STREAM,LOG) unicosmk_output_align (STREAM, LOG)

/* This is how to advance the location counter by SIZE bytes.  */

#undef ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(STREAM,SIZE)			\
  fprintf ((STREAM), "\t.byte\t0:"HOST_WIDE_INT_PRINT_UNSIGNED"\n",\
	   (SIZE));

/* This says how to output an assembler line to define a global common
   symbol. We need the alignment information because it has to be supplied
   in the section header.  */ 

#undef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)	\
  unicosmk_output_common ((FILE), (NAME), (SIZE), (ALIGN))

/* This says how to output an assembler line to define a local symbol.  */

#undef ASM_OUTPUT_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(FILE, NAME, SIZE, ALIGN) \
  do { switch_to_section (data_section);		\
       fprintf (FILE, "\t.align\t%d\n", floor_log2 ((ALIGN) / BITS_PER_UNIT));\
       ASM_OUTPUT_LABEL ((FILE), (NAME));		\
       fprintf (FILE, "\t.byte 0:"HOST_WIDE_INT_PRINT_UNSIGNED"\n",(SIZE));\
  } while (0)

/* CAM does not allow us to declare a symbol as external first and then
   define it in the same file later. Thus, we keep a list of all external
   references, remove all symbols defined locally from it and output it at
   the end of the asm file.  */
   
#define ASM_OUTPUT_EXTERNAL(FILE,DECL,NAME) \
  unicosmk_add_extern ((NAME))

#define ASM_OUTPUT_EXTERNAL_LIBCALL(STREAM,SYMREF)	\
  unicosmk_add_extern (XSTR ((SYMREF), 0))

/* This is how to declare an object. We don't have to output anything if
   it is a global variable because those go into unique `common' sections
   and the section name is globally visible. For local variables, we simply
   output the label. In any case, we have to record that no extern
   declaration should be generated for the symbol.  */

#define ASM_DECLARE_OBJECT_NAME(STREAM,NAME,DECL) 	\
  do { tree name_tree;					\
       name_tree = get_identifier ((NAME));		\
       TREE_ASM_WRITTEN (name_tree) = 1;		\
       if (!TREE_PUBLIC (DECL))				\
	 {						\
	   assemble_name (STREAM, NAME);		\
	   fputs (":\n", STREAM);			\
         }						\
  } while(0)

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION unicosmk_asm_named_section
#define TARGET_ASM_INIT_SECTIONS unicosmk_init_sections

#undef ASM_OUTPUT_MAX_SKIP_ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(STREAM,POWER,MAXSKIP)

#undef NM_FLAGS

#undef OBJECT_FORMAT_COFF

/* We cannot generate debugging information on Unicos/Mk.  */

#undef SDB_DEBUGGING_INFO
#undef MIPS_DEBUGGING_INFO
#undef DBX_DEBUGGING_INFO
#undef DWARF2_DEBUGGING_INFO
#undef DWARF2_UNWIND_INFO
#undef INCOMING_RETURN_ADDR_RTX
#undef PREFERRED_DEBUGGING_TYPE

/* We don't need a start file.  */

#undef STARTFILE_SPEC
#define STARTFILE_SPEC ""

/* These are the libraries we have to link with.
   ??? The Craylibs directory should be autoconfed.  */
#undef LIB_SPEC
#define LIB_SPEC "-L/opt/ctl/craylibs/craylibs -lu -lm -lc -lsma"

#undef EXPAND_BUILTIN_VA_START

#define EH_FRAME_IN_DATA_SECTION 1
