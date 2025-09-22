/* Update the symbol table (the .T file) in a MIPS object to
   contain debugging information specified by the GNU compiler
   in the form of comments (the mips assembler does not support
   assembly access to debug information).
   Copyright (C) 1991, 1993, 1994, 1995, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Michael Meissner (meissner@cygnus.com).

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


/* Here is a brief description of the MIPS ECOFF symbol table.  The
   MIPS symbol table has the following pieces:

	Symbolic Header
	    |
	    +--	Auxiliary Symbols
	    |
	    +--	Dense number table
	    |
	    +--	Optimizer Symbols
	    |
	    +--	External Strings
	    |
	    +--	External Symbols
	    |
	    +--	Relative file descriptors
	    |
	    +--	File table
		    |
		    +--	Procedure table
		    |
		    +--	Line number table
		    |
		    +--	Local Strings
		    |
		    +--	Local Symbols

   The symbolic header points to each of the other tables, and also
   contains the number of entries.  It also contains a magic number
   and MIPS compiler version number, such as 2.0.

   The auxiliary table is a series of 32 bit integers, that are
   referenced as needed from the local symbol table.  Unlike standard
   COFF, the aux.  information does not follow the symbol that uses
   it, but rather is a separate table.  In theory, this would allow
   the MIPS compilers to collapse duplicate aux. entries, but I've not
   noticed this happening with the 1.31 compiler suite.  The different
   types of aux. entries are:

    1)	dnLow: Low bound on array dimension.

    2)	dnHigh: High bound on array dimension.

    3)	isym: Index to the local symbol which is the start of the
	function for the end of function first aux. entry.

    4)	width: Width of structures and bitfields.

    5)	count: Count of ranges for variant part.

    6)	rndx: A relative index into the symbol table.  The relative
	index field has two parts: rfd which is a pointer into the
	relative file index table or ST_RFDESCAPE which says the next
	aux. entry is the file number, and index: which is the pointer
	into the local symbol within a given file table.  This is for
	things like references to types defined in another file.

    7)	Type information: This is like the COFF type bits, except it
	is 32 bits instead of 16; they still have room to add new
	basic types; and they can handle more than 6 levels of array,
	pointer, function, etc.  Each type information field contains
	the following structure members:

	    a)	fBitfield: a bit that says this is a bitfield, and the
		size in bits follows as the next aux. entry.

	    b)	continued: a bit that says the next aux. entry is a
		continuation of the current type information (in case
		there are more than 6 levels of array/ptr/function).

	    c)	bt: an integer containing the base type before adding
		array, pointer, function, etc. qualifiers.  The
		current base types that I have documentation for are:

			btNil		-- undefined
			btAdr		-- address - integer same size as ptr
			btChar		-- character
			btUChar		-- unsigned character
			btShort		-- short
			btUShort	-- unsigned short
			btInt		-- int
			btUInt		-- unsigned int
			btLong		-- long
			btULong		-- unsigned long
			btFloat		-- float (real)
			btDouble	-- Double (real)
			btStruct	-- Structure (Record)
			btUnion		-- Union (variant)
			btEnum		-- Enumerated
			btTypedef	-- defined via a typedef isymRef
			btRange		-- subrange of int
			btSet		-- pascal sets
			btComplex	-- fortran complex
			btDComplex	-- fortran double complex
			btIndirect	-- forward or unnamed typedef
			btFixedDec	-- Fixed Decimal
			btFloatDec	-- Float Decimal
			btString	-- Varying Length Character String
			btBit		-- Aligned Bit String
			btPicture	-- Picture
			btVoid		-- Void (MIPS cc revision >= 2.00)

	    d)	tq0 - tq5: type qualifier fields as needed.  The
		current type qualifier fields I have documentation for
		are:

			tqNil		-- no more qualifiers
			tqPtr		-- pointer
			tqProc		-- procedure
			tqArray		-- array
			tqFar		-- 8086 far pointers
			tqVol		-- volatile


   The dense number table is used in the front ends, and disappears by
   the time the .o is created.

   With the 1.31 compiler suite, the optimization symbols don't seem
   to be used as far as I can tell.

   The linker is the first entity that creates the relative file
   descriptor table, and I believe it is used so that the individual
   file table pointers don't have to be rewritten when the objects are
   merged together into the program file.

   Unlike COFF, the basic symbol & string tables are split into
   external and local symbols/strings.  The relocation information
   only goes off of the external symbol table, and the debug
   information only goes off of the internal symbol table.  The
   external symbols can have links to an appropriate file index and
   symbol within the file to give it the appropriate type information.
   Because of this, the external symbols are actually larger than the
   internal symbols (to contain the link information), and contain the
   local symbol structure as a member, though this member is not the
   first member of the external symbol structure (!).  I suspect this
   split is to make strip easier to deal with.

   Each file table has offsets for where the line numbers, local
   strings, local symbols, and procedure table starts from within the
   global tables, and the indices are reset to 0 for each of those
   tables for the file.

   The procedure table contains the binary equivalents of the .ent
   (start of the function address), .frame (what register is the
   virtual frame pointer, constant offset from the register to obtain
   the VFP, and what register holds the return address), .mask/.fmask
   (bitmask of saved registers, and where the first register is stored
   relative to the VFP) assembler directives.  It also contains the
   low and high bounds of the line numbers if debugging is turned on.

   The line number table is a compressed form of the normal COFF line
   table.  Each line number entry is either 1 or 3 bytes long, and
   contains a signed delta from the previous line, and an unsigned
   count of the number of instructions this statement takes.

   The local symbol table contains the following fields:

    1)	iss: index to the local string table giving the name of the
	symbol.

    2)	value: value of the symbol (address, register number, etc.).

    3)	st: symbol type.  The current symbol types are:

	    stNil	  -- Nuthin' special
	    stGlobal	  -- external symbol
	    stStatic	  -- static
	    stParam	  -- procedure argument
	    stLocal	  -- local variable
	    stLabel	  -- label
	    stProc	  -- External Procedure
	    stBlock	  -- beginning of block
	    stEnd	  -- end (of anything)
	    stMember	  -- member (of anything)
	    stTypedef	  -- type definition
	    stFile	  -- file name
	    stRegReloc	  -- register relocation
	    stForward	  -- forwarding address
	    stStaticProc  -- Static procedure
	    stConstant	  -- const

    4)	sc: storage class.  The current storage classes are:

	    scText	  -- text symbol
	    scData	  -- initialized data symbol
	    scBss	  -- un-initialized data symbol
	    scRegister	  -- value of symbol is register number
	    scAbs	  -- value of symbol is absolute
	    scUndefined   -- who knows?
	    scCdbLocal	  -- variable's value is IN se->va.??
	    scBits	  -- this is a bit field
	    scCdbSystem	  -- value is IN debugger's address space
	    scRegImage	  -- register value saved on stack
	    scInfo	  -- symbol contains debugger information
	    scUserStruct  -- addr in struct user for current process
	    scSData	  -- load time only small data
	    scSBss	  -- load time only small common
	    scRData	  -- load time only read only data
	    scVar	  -- Var parameter (fortranpascal)
	    scCommon	  -- common variable
	    scSCommon	  -- small common
	    scVarRegister -- Var parameter in a register
	    scVariant	  -- Variant record
	    scSUndefined  -- small undefined(external) data
	    scInit	  -- .init section symbol

    5)	index: pointer to a local symbol or aux. entry.



   For the following program:

	#include <stdio.h>

	main(){
		printf("Hello World!\n");
		return 0;
	}

   Mips-tdump produces the following information:

   Global file header:
       magic number             0x162
       # sections               2
       timestamp                645311799, Wed Jun 13 17:16:39 1990
       symbolic header offset   284
       symbolic header size     96
       optional header          56
       flags                    0x0

   Symbolic header, magic number = 0x7009, vstamp = 1.31:

       Info                      Offset      Number       Bytes
       ====                      ======      ======      =====

       Line numbers                 380           4           4 [13]
       Dense numbers                  0           0           0
       Procedures Tables            384           1          52
       Local Symbols                436          16         192
       Optimization Symbols           0           0           0
       Auxiliary Symbols            628          39         156
       Local Strings                784          80          80
       External Strings             864         144         144
       File Tables                 1008           2         144
       Relative Files                 0           0           0
       External Symbols            1152          20         320

   File #0, "hello2.c"

       Name index  = 1          Readin      = No
       Merge       = No         Endian      = LITTLE
       Debug level = G2         Language    = C
       Adr         = 0x00000000

       Info                       Start      Number        Size      Offset
       ====                       =====      ======        ====      ======
       Local strings                  0          15          15         784
       Local symbols                  0           6          72         436
       Line numbers                   0          13          13         380
       Optimization symbols           0           0           0           0
       Procedures                     0           1          52         384
       Auxiliary symbols              0          14          56         628
       Relative Files                 0           0           0           0

    There are 6 local symbols, starting at 436

	Symbol# 0: "hello2.c"
	    End+1 symbol  = 6
	    String index  = 1
	    Storage class = Text        Index  = 6
	    Symbol type   = File        Value  = 0

	Symbol# 1: "main"
	    End+1 symbol  = 5
	    Type          = int
	    String index  = 10
	    Storage class = Text        Index  = 12
	    Symbol type   = Proc        Value  = 0

	Symbol# 2: ""
	    End+1 symbol  = 4
	    String index  = 0
	    Storage class = Text        Index  = 4
	    Symbol type   = Block       Value  = 8

	Symbol# 3: ""
	    First symbol  = 2
	    String index  = 0
	    Storage class = Text        Index  = 2
	    Symbol type   = End         Value  = 28

	Symbol# 4: "main"
	    First symbol  = 1
	    String index  = 10
	    Storage class = Text        Index  = 1
	    Symbol type   = End         Value  = 52

	Symbol# 5: "hello2.c"
	    First symbol  = 0
	    String index  = 1
	    Storage class = Text        Index  = 0
	    Symbol type   = End         Value  = 0

    There are 14 auxiliary table entries, starting at 628.

	* #0               0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #1              24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]
	* #2               8, [   8/      0], [ 2 0:0 0:0:0:0:0:0]
	* #3              16, [  16/      0], [ 4 0:0 0:0:0:0:0:0]
	* #4              24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]
	* #5              32, [  32/      0], [ 8 0:0 0:0:0:0:0:0]
	* #6              40, [  40/      0], [10 0:0 0:0:0:0:0:0]
	* #7              44, [  44/      0], [11 0:0 0:0:0:0:0:0]
	* #8              12, [  12/      0], [ 3 0:0 0:0:0:0:0:0]
	* #9              20, [  20/      0], [ 5 0:0 0:0:0:0:0:0]
	* #10             28, [  28/      0], [ 7 0:0 0:0:0:0:0:0]
	* #11             36, [  36/      0], [ 9 0:0 0:0:0:0:0:0]
	  #12              5, [   5/      0], [ 1 1:0 0:0:0:0:0:0]
	  #13             24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]

    There are 1 procedure descriptor entries, starting at 0.

	Procedure descriptor 0:
	    Name index   = 10          Name          = "main"
	    .mask 0x80000000,-4        .fmask 0x00000000,0
	    .frame $29,24,$31
	    Opt. start   = -1          Symbols start = 1
	    First line # = 3           Last line #   = 6
	    Line Offset  = 0           Address       = 0x00000000

	There are 4 bytes holding line numbers, starting at 380.
	    Line           3,   delta     0,   count  2
	    Line           4,   delta     1,   count  3
	    Line           5,   delta     1,   count  2
	    Line           6,   delta     1,   count  6

   File #1, "/usr/include/stdio.h"

    Name index  = 1          Readin      = No
    Merge       = Yes        Endian      = LITTLE
    Debug level = G2         Language    = C
    Adr         = 0x00000000

    Info                       Start      Number        Size      Offset
    ====                       =====      ======        ====      ======
    Local strings                 15          65          65         799
    Local symbols                  6          10         120         508
    Line numbers                   0           0           0         380
    Optimization symbols           0           0           0           0
    Procedures                     1           0           0         436
    Auxiliary symbols             14          25         100         684
    Relative Files                 0           0           0           0

    There are 10 local symbols, starting at 442

	Symbol# 0: "/usr/include/stdio.h"
	    End+1 symbol  = 10
	    String index  = 1
	    Storage class = Text        Index  = 10
	    Symbol type   = File        Value  = 0

	Symbol# 1: "_iobuf"
	    End+1 symbol  = 9
	    String index  = 22
	    Storage class = Info        Index  = 9
	    Symbol type   = Block       Value  = 20

	Symbol# 2: "_cnt"
	    Type          = int
	    String index  = 29
	    Storage class = Info        Index  = 4
	    Symbol type   = Member      Value  = 0

	Symbol# 3: "_ptr"
	    Type          = ptr to char
	    String index  = 34
	    Storage class = Info        Index  = 15
	    Symbol type   = Member      Value  = 32

	Symbol# 4: "_base"
	    Type          = ptr to char
	    String index  = 39
	    Storage class = Info        Index  = 16
	    Symbol type   = Member      Value  = 64

	Symbol# 5: "_bufsiz"
	    Type          = int
	    String index  = 45
	    Storage class = Info        Index  = 4
	    Symbol type   = Member      Value  = 96

	Symbol# 6: "_flag"
	    Type          = short
	    String index  = 53
	    Storage class = Info        Index  = 3
	    Symbol type   = Member      Value  = 128

	Symbol# 7: "_file"
	    Type          = char
	    String index  = 59
	    Storage class = Info        Index  = 2
	    Symbol type   = Member      Value  = 144

	Symbol# 8: ""
	    First symbol  = 1
	    String index  = 0
	    Storage class = Info        Index  = 1
	    Symbol type   = End         Value  = 0

	Symbol# 9: "/usr/include/stdio.h"
	    First symbol  = 0
	    String index  = 1
	    Storage class = Text        Index  = 0
	    Symbol type   = End         Value  = 0

    There are 25 auxiliary table entries, starting at 642.

	* #14             -1, [4095/1048575], [63 1:1 f:f:f:f:f:f]
	  #15          65544, [   8/     16], [ 2 0:0 1:0:0:0:0:0]
	  #16          65544, [   8/     16], [ 2 0:0 1:0:0:0:0:0]
	* #17         196656, [  48/     48], [12 0:0 3:0:0:0:0:0]
	* #18           8191, [4095/      1], [63 1:1 0:0:0:0:f:1]
	* #19              1, [   1/      0], [ 0 1:0 0:0:0:0:0:0]
	* #20          20479, [4095/      4], [63 1:1 0:0:0:0:f:4]
	* #21              1, [   1/      0], [ 0 1:0 0:0:0:0:0:0]
	* #22              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #23              2, [   2/      0], [ 0 0:1 0:0:0:0:0:0]
	* #24            160, [ 160/      0], [40 0:0 0:0:0:0:0:0]
	* #25              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #26              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #27              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #28              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #29              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #30              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #31              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #32              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #33              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #34              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #35              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #36              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #37              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #38              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]

    There are 0 procedure descriptor entries, starting at 1.

   There are 20 external symbols, starting at 1152

	Symbol# 0: "_iob"
	    Type          = array [3 {160}] of struct _iobuf { ifd = 1, index = 1 }
	    String index  = 0           Ifd    = 1
	    Storage class = Nil         Index  = 17
	    Symbol type   = Global      Value  = 60

	Symbol# 1: "fopen"
	    String index  = 5           Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 2: "fdopen"
	    String index  = 11          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 3: "freopen"
	    String index  = 18          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 4: "popen"
	    String index  = 26          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 5: "tmpfile"
	    String index  = 32          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 6: "ftell"
	    String index  = 40          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 7: "rewind"
	    String index  = 46          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 8: "setbuf"
	    String index  = 53          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 9: "setbuffer"
	    String index  = 60          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 10: "setlinebuf"
	    String index  = 70          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 11: "fgets"
	    String index  = 81          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 12: "gets"
	    String index  = 87          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 13: "ctermid"
	    String index  = 92          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 14: "cuserid"
	    String index  = 100         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 15: "tempnam"
	    String index  = 108         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 16: "tmpnam"
	    String index  = 116         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 17: "sprintf"
	    String index  = 123         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 18: "main"
	    Type          = int
	    String index  = 131         Ifd    = 0
	    Storage class = Text        Index  = 1
	    Symbol type   = Proc        Value  = 0

	Symbol# 19: "printf"
	    String index  = 136         Ifd    = 0
	    Storage class = Undefined   Index  = 1048575
	    Symbol type   = Proc        Value  = 0

   The following auxiliary table entries were unused:

    #0               0  0x00000000  void
    #2               8  0x00000008  char
    #3              16  0x00000010  short
    #4              24  0x00000018  int
    #5              32  0x00000020  long
    #6              40  0x00000028  float
    #7              44  0x0000002c  double
    #8              12  0x0000000c  unsigned char
    #9              20  0x00000014  unsigned short
    #10             28  0x0000001c  unsigned int
    #11             36  0x00000024  unsigned long
    #14              0  0x00000000  void
    #15             24  0x00000018  int
    #19             32  0x00000020  long
    #20             40  0x00000028  float
    #21             44  0x0000002c  double
    #22             12  0x0000000c  unsigned char
    #23             20  0x00000014  unsigned short
    #24             28  0x0000001c  unsigned int
    #25             36  0x00000024  unsigned long
    #26             48  0x00000030  struct no name { ifd = -1, index = 1048575 }

*/


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "version.h"
#include "intl.h"

#ifndef __SABER__
#define saber_stop()
#endif

/* Include getopt.h for the sake of getopt_long.  */
#include "getopt.h"

#ifndef __LINE__
#define __LINE__ 0
#endif

/* Due to size_t being defined in sys/types.h and different
   in stddef.h, we have to do this by hand.....  Note, these
   types are correct for MIPS based systems, and may not be
   correct for other systems.  Ultrix 4.0 and Silicon Graphics
   have this fixed, but since the following is correct, and
   the fact that including stddef.h gets you GCC's version
   instead of the standard one it's not worth it to fix it.  */

#if defined(__OSF1__) || defined(__OSF__) || defined(__osf__)
#define Size_t		long unsigned int
#else
#define Size_t		unsigned int
#endif
#define Ptrdiff_t	long

/* The following might be called from obstack or malloc,
   so they can't be static.  */

extern void pfatal_with_name (const char *) ATTRIBUTE_NORETURN;
extern void botch (const char *) ATTRIBUTE_NORETURN;

extern void fatal (const char *format, ...) ATTRIBUTE_PRINTF_1 ATTRIBUTE_NORETURN;
extern void error (const char *format, ...) ATTRIBUTE_PRINTF_1;

#ifndef MIPS_DEBUGGING_INFO

static int	 line_number;
static int	 cur_line_start;
static int	 debug;
static int	 had_errors;
static const char *progname;
static const char *input_name;

int
main (void)
{
  fprintf (stderr, "Mips-tfile should only be run on a MIPS computer!\n");
  exit (1);
}

#else				/* MIPS_DEBUGGING defined */

/* The local and global symbols have a field index, so undo any defines
   of index -> strchr.  */

#undef index

#include <signal.h>

#ifndef CROSS_COMPILE
#include <a.out.h>
#else
#include "mips/a.out.h"
#endif /* CROSS_COMPILE */

#include "gstab.h"

#define STAB_CODE_TYPE enum __stab_debug_code

#ifndef MALLOC_CHECK
#ifdef	__SABER__
#define MALLOC_CHECK
#endif
#endif

#define IS_ASM_IDENT(ch) \
  (ISIDNUM (ch) || (ch) == '.' || (ch) == '$')


/* Redefinition of storage classes as an enumeration for better
   debugging.  */

typedef enum sc {
  sc_Nil	 = scNil,	  /* no storage class */
  sc_Text	 = scText,	  /* text symbol */
  sc_Data	 = scData,	  /* initialized data symbol */
  sc_Bss	 = scBss,	  /* un-initialized data symbol */
  sc_Register	 = scRegister,	  /* value of symbol is register number */
  sc_Abs	 = scAbs,	  /* value of symbol is absolute */
  sc_Undefined	 = scUndefined,	  /* who knows? */
  sc_CdbLocal	 = scCdbLocal,	  /* variable's value is IN se->va.?? */
  sc_Bits	 = scBits,	  /* this is a bit field */
  sc_CdbSystem	 = scCdbSystem,	  /* value is IN CDB's address space */
  sc_RegImage	 = scRegImage,	  /* register value saved on stack */
  sc_Info	 = scInfo,	  /* symbol contains debugger information */
  sc_UserStruct	 = scUserStruct,  /* addr in struct user for current process */
  sc_SData	 = scSData,	  /* load time only small data */
  sc_SBss	 = scSBss,	  /* load time only small common */
  sc_RData	 = scRData,	  /* load time only read only data */
  sc_Var	 = scVar,	  /* Var parameter (fortran,pascal) */
  sc_Common	 = scCommon,	  /* common variable */
  sc_SCommon	 = scSCommon,	  /* small common */
  sc_VarRegister = scVarRegister, /* Var parameter in a register */
  sc_Variant	 = scVariant,	  /* Variant record */
  sc_SUndefined	 = scSUndefined,  /* small undefined(external) data */
  sc_Init	 = scInit,	  /* .init section symbol */
  sc_Max	 = scMax	  /* Max storage class+1 */
} sc_t;

/* Redefinition of symbol type.  */

typedef enum st {
  st_Nil	= stNil,	/* Nuthin' special */
  st_Global	= stGlobal,	/* external symbol */
  st_Static	= stStatic,	/* static */
  st_Param	= stParam,	/* procedure argument */
  st_Local	= stLocal,	/* local variable */
  st_Label	= stLabel,	/* label */
  st_Proc	= stProc,	/*     "      "	 Procedure */
  st_Block	= stBlock,	/* beginning of block */
  st_End	= stEnd,	/* end (of anything) */
  st_Member	= stMember,	/* member (of anything	- struct/union/enum */
  st_Typedef	= stTypedef,	/* type definition */
  st_File	= stFile,	/* file name */
  st_RegReloc	= stRegReloc,	/* register relocation */
  st_Forward	= stForward,	/* forwarding address */
  st_StaticProc	= stStaticProc,	/* load time only static procs */
  st_Constant	= stConstant,	/* const */
  st_Str	= stStr,	/* string */
  st_Number	= stNumber,	/* pure number (i.e. 4 NOR 2+2) */
  st_Expr	= stExpr,	/* 2+2 vs. 4 */
  st_Type	= stType,	/* post-coercion SER */
  st_Max	= stMax		/* max type+1 */
} st_t;

/* Redefinition of type qualifiers.  */

typedef enum tq {
  tq_Nil	= tqNil,	/* bt is what you see */
  tq_Ptr	= tqPtr,	/* pointer */
  tq_Proc	= tqProc,	/* procedure */
  tq_Array	= tqArray,	/* duh */
  tq_Far	= tqFar,	/* longer addressing - 8086/8 land */
  tq_Vol	= tqVol,	/* volatile */
  tq_Max	= tqMax		/* Max type qualifier+1 */
} tq_t;

/* Redefinition of basic types.  */

typedef enum bt {
  bt_Nil	= btNil,	/* undefined */
  bt_Adr	= btAdr,	/* address - integer same size as pointer */
  bt_Char	= btChar,	/* character */
  bt_UChar	= btUChar,	/* unsigned character */
  bt_Short	= btShort,	/* short */
  bt_UShort	= btUShort,	/* unsigned short */
  bt_Int	= btInt,	/* int */
  bt_UInt	= btUInt,	/* unsigned int */
  bt_Long	= btLong,	/* long */
  bt_ULong	= btULong,	/* unsigned long */
  bt_Float	= btFloat,	/* float (real) */
  bt_Double	= btDouble,	/* Double (real) */
  bt_Struct	= btStruct,	/* Structure (Record) */
  bt_Union	= btUnion,	/* Union (variant) */
  bt_Enum	= btEnum,	/* Enumerated */
  bt_Typedef	= btTypedef,	/* defined via a typedef, isymRef points */
  bt_Range	= btRange,	/* subrange of int */
  bt_Set	= btSet,	/* pascal sets */
  bt_Complex	= btComplex,	/* fortran complex */
  bt_DComplex	= btDComplex,	/* fortran double complex */
  bt_Indirect	= btIndirect,	/* forward or unnamed typedef */
  bt_FixedDec	= btFixedDec,	/* Fixed Decimal */
  bt_FloatDec	= btFloatDec,	/* Float Decimal */
  bt_String	= btString,	/* Varying Length Character String */
  bt_Bit	= btBit,	/* Aligned Bit String */
  bt_Picture	= btPicture,	/* Picture */

#ifdef btVoid
  bt_Void	= btVoid,	/* Void */
#else
#define bt_Void	bt_Nil
#endif

  bt_Max	= btMax		/* Max basic type+1 */
} bt_t;



/* Basic COFF storage classes.  */
enum coff_storage {
  C_EFCN	= -1,
  C_NULL	= 0,
  C_AUTO	= 1,
  C_EXT		= 2,
  C_STAT	= 3,
  C_REG		= 4,
  C_EXTDEF	= 5,
  C_LABEL	= 6,
  C_ULABEL	= 7,
  C_MOS		= 8,
  C_ARG		= 9,
  C_STRTAG	= 10,
  C_MOU		= 11,
  C_UNTAG	= 12,
  C_TPDEF	= 13,
  C_USTATIC	= 14,
  C_ENTAG	= 15,
  C_MOE		= 16,
  C_REGPARM	= 17,
  C_FIELD	= 18,
  C_BLOCK	= 100,
  C_FCN		= 101,
  C_EOS		= 102,
  C_FILE	= 103,
  C_LINE	= 104,
  C_ALIAS	= 105,
  C_HIDDEN	= 106,
  C_MAX		= 107
} coff_storage_t;

/* Regular COFF fundamental type.  */
typedef enum coff_type {
  T_NULL	= 0,
  T_ARG		= 1,
  T_CHAR	= 2,
  T_SHORT	= 3,
  T_INT		= 4,
  T_LONG	= 5,
  T_FLOAT	= 6,
  T_DOUBLE	= 7,
  T_STRUCT	= 8,
  T_UNION	= 9,
  T_ENUM	= 10,
  T_MOE		= 11,
  T_UCHAR	= 12,
  T_USHORT	= 13,
  T_UINT	= 14,
  T_ULONG	= 15,
  T_MAX		= 16
} coff_type_t;

/* Regular COFF derived types.  */
typedef enum coff_dt {
  DT_NON	= 0,
  DT_PTR	= 1,
  DT_FCN	= 2,
  DT_ARY	= 3,
  DT_MAX	= 4
} coff_dt_t;

#define N_BTMASK	017	/* bitmask to isolate basic type */
#define N_TMASK		003	/* bitmask to isolate derived type */
#define N_BT_SHIFT	4	/* # bits to shift past basic type */
#define N_TQ_SHIFT	2	/* # bits to shift derived types */
#define	N_TQ		6	/* # of type qualifiers */

/* States for whether to hash type or not.  */
typedef enum hash_state {
  hash_no	= 0,		/* don't hash type */
  hash_yes	= 1,		/* ok to hash type, or use previous hash */
  hash_record	= 2		/* ok to record hash, but don't use prev.  */
} hash_state_t;


/* Types of different sized allocation requests.  */
enum alloc_type {
  alloc_type_none,		/* dummy value */
  alloc_type_scope,		/* nested scopes linked list */
  alloc_type_vlinks,		/* glue linking pages in varray */
  alloc_type_shash,		/* string hash element */
  alloc_type_thash,		/* type hash element */
  alloc_type_tag,		/* struct/union/tag element */
  alloc_type_forward,		/* element to hold unknown tag */
  alloc_type_thead,		/* head of type hash list */
  alloc_type_varray,		/* general varray allocation */
  alloc_type_last		/* last+1 element for array bounds */
};


#define WORD_ALIGN(x)  (((x) + (sizeof (long) - 1)) & ~ (sizeof (long) - 1))
#define DWORD_ALIGN(x) (((x) + 7) & ~7)


/* Structures to provide n-number of virtual arrays, each of which can
   grow linearly, and which are written in the object file as sequential
   pages.  On systems with a BSD malloc that define USE_MALLOC, the
   MAX_CLUSTER_PAGES should be 1 less than a power of two, since malloc
   adds its overhead, and rounds up to the next power of 2.  Pages are
   linked together via a linked list.  */

#ifndef PAGE_SIZE
#define PAGE_SIZE 32768		/* size of varray pages */
#endif

#define PAGE_USIZE ((Size_t) PAGE_SIZE)


#ifndef MAX_CLUSTER_PAGES	/* # pages to get from system */
#ifndef USE_MALLOC		/* in one memory request */
#define MAX_CLUSTER_PAGES 64
#else
#define MAX_CLUSTER_PAGES 63
#endif
#endif


/* Linked list connecting separate page allocations.  */
typedef struct vlinks {
  struct vlinks	*prev;		/* previous set of pages */
  struct vlinks *next;		/* next set of pages */
  union  page   *datum;		/* start of page */
  unsigned long	 start_index;	/* starting index # of page */
} vlinks_t;


/* Virtual array header.  */
typedef struct varray {
  vlinks_t	*first;			/* first page link */
  vlinks_t	*last;			/* last page link */
  unsigned long	 num_allocated;		/* # objects allocated */
  unsigned short object_size;		/* size in bytes of each object */
  unsigned short objects_per_page;	/* # objects that can fit on a page */
  unsigned short objects_last_page;	/* # objects allocated on last page */
} varray_t;

#ifndef MALLOC_CHECK
#define OBJECTS_PER_PAGE(type) (PAGE_SIZE / sizeof (type))
#else
#define OBJECTS_PER_PAGE(type) ((sizeof (type) > 1) ? 1 : PAGE_SIZE)
#endif

#define INIT_VARRAY(type) {	/* macro to initialize a varray */	\
  (vlinks_t *) 0,		/* first */				\
  (vlinks_t *) 0,		/* last */				\
  0,				/* num_allocated */			\
  sizeof (type),		/* object_size */			\
  OBJECTS_PER_PAGE (type),	/* objects_per_page */			\
  OBJECTS_PER_PAGE (type),	/* objects_last_page */			\
}

#define INITIALIZE_VARRAY(x,type)			\
do {							\
  (x)->object_size = sizeof (type);			\
  (x)->objects_per_page = OBJECTS_PER_PAGE (type);	\
  (x)->objects_last_page = OBJECTS_PER_PAGE (type);	\
} while (0)

/* Master type for indexes within the symbol table.  */
typedef unsigned long symint_t;


/* Linked list support for nested scopes (file, block, structure, etc.).  */
typedef struct scope {
  struct scope	*prev;		/* previous scope level */
  struct scope	*free;		/* free list pointer */
  SYMR		*lsym;		/* pointer to local symbol node */
  symint_t	 lnumber;	/* lsym index */
  st_t		 type;		/* type of the node */
} scope_t;


/* Forward reference list for tags referenced, but not yet defined.  */
typedef struct forward {
  struct forward *next;		/* next forward reference */
  struct forward *free;		/* free list pointer */
  AUXU		 *ifd_ptr;	/* pointer to store file index */
  AUXU		 *index_ptr;	/* pointer to store symbol index */
  AUXU		 *type_ptr;	/* pointer to munge type info */
} forward_t;


/* Linked list support for tags.  The first tag in the list is always
   the current tag for that block.  */
typedef struct tag {
  struct tag	 *free;		/* free list pointer */
  struct shash	 *hash_ptr;	/* pointer to the hash table head */
  struct tag	 *same_name;	/* tag with same name in outer scope */
  struct tag	 *same_block;	/* next tag defined in the same block.  */
  struct forward *forward_ref;	/* list of forward references */
  bt_t		  basic_type;	/* bt_Struct, bt_Union, or bt_Enum */
  symint_t	  ifd;		/* file # tag defined in */
  symint_t	  indx;		/* index within file's local symbols */
} tag_t;


/* Head of a block's linked list of tags.  */
typedef struct thead {
  struct thead	*prev;		/* previous block */
  struct thead	*free;		/* free list pointer */
  struct tag	*first_tag;	/* first tag in block defined */
} thead_t;


/* Union containing pointers to each the small structures which are freed up.  */
typedef union small_free {
  scope_t	*f_scope;	/* scope structure */
  thead_t	*f_thead;	/* tag head structure */
  tag_t		*f_tag;		/* tag element structure */
  forward_t	*f_forward;	/* forward tag reference */
} small_free_t;


/* String hash table support.  The size of the hash table must fit
   within a page.  */

#ifndef SHASH_SIZE
#define SHASH_SIZE 1009
#endif

#define HASH_LEN_MAX ((1 << 12) - 1)	/* Max length we can store */

typedef struct shash {
  struct shash	*next;		/* next hash value */
  char		*string;	/* string we are hashing */
  symint_t	 len;		/* string length */
  symint_t	 indx;		/* index within string table */
  EXTR		*esym_ptr;	/* global symbol pointer */
  SYMR		*sym_ptr;	/* local symbol pointer */
  SYMR		*end_ptr;	/* symbol pointer to end block */
  tag_t		*tag_ptr;	/* tag pointer */
  PDR		*proc_ptr;	/* procedure descriptor pointer */
} shash_t;


/* Type hash table support.  The size of the hash table must fit
   within a page with the other extended file descriptor information.
   Because unique types which are hashed are fewer in number than
   strings, we use a smaller hash value.  */

#ifndef THASH_SIZE
#define THASH_SIZE 113
#endif

typedef struct thash {
  struct thash	*next;		/* next hash value */
  AUXU		 type;		/* type we are hashing */
  symint_t	 indx;		/* index within string table */
} thash_t;


/* Extended file descriptor that contains all of the support necessary
   to add things to each file separately.  */
typedef struct efdr {
  FDR		 fdr;		/* File header to be written out */
  FDR		*orig_fdr;	/* original file header */
  char		*name;		/* filename */
  int		 name_len;	/* length of the filename */
  symint_t	 void_type;	/* aux. pointer to 'void' type */
  symint_t	 int_type;	/* aux. pointer to 'int' type */
  scope_t	*cur_scope;	/* current nested scopes */
  symint_t	 file_index;	/* current file number */
  int		 nested_scopes;	/* # nested scopes */
  varray_t	 strings;	/* local strings */
  varray_t	 symbols;	/* local symbols */
  varray_t	 procs;		/* procedures */
  varray_t	 aux_syms;	/* auxiliary symbols */
  struct efdr	*next_file;	/* next file descriptor */
				/* string/type hash tables */
  shash_t      **shash_head;	/* string hash table */
  thash_t	*thash_head[THASH_SIZE];
} efdr_t;

/* Pre-initialized extended file structure.  */
static int init_file_initialized = 0;
static efdr_t init_file;

static efdr_t *first_file;			/* first file descriptor */
static efdr_t **last_file_ptr = &first_file;	/* file descriptor tail */


/* Union of various things that are held in pages.  */
typedef union page {
  char		byte	[ PAGE_SIZE ];
  unsigned char	ubyte	[ PAGE_SIZE ];
  efdr_t	file	[ PAGE_SIZE / sizeof (efdr_t)	 ];
  FDR		ofile	[ PAGE_SIZE / sizeof (FDR)	 ];
  PDR		proc	[ PAGE_SIZE / sizeof (PDR)	 ];
  SYMR		sym	[ PAGE_SIZE / sizeof (SYMR)	 ];
  EXTR		esym	[ PAGE_SIZE / sizeof (EXTR)	 ];
  AUXU		aux	[ PAGE_SIZE / sizeof (AUXU)	 ];
  DNR		dense	[ PAGE_SIZE / sizeof (DNR)	 ];
  scope_t	scope	[ PAGE_SIZE / sizeof (scope_t)	 ];
  vlinks_t	vlinks	[ PAGE_SIZE / sizeof (vlinks_t)	 ];
  shash_t	shash	[ PAGE_SIZE / sizeof (shash_t)	 ];
  thash_t	thash	[ PAGE_SIZE / sizeof (thash_t)	 ];
  tag_t		tag	[ PAGE_SIZE / sizeof (tag_t)	 ];
  forward_t	forward	[ PAGE_SIZE / sizeof (forward_t) ];
  thead_t	thead	[ PAGE_SIZE / sizeof (thead_t)	 ];
} page_t;


/* Structure holding allocation information for small sized structures.  */
typedef struct alloc_info {
  const char	*alloc_name;	/* name of this allocation type (must be first) */
  page_t	*cur_page;	/* current page being allocated from */
  small_free_t	 free_list;	/* current free list if any */
  int		 unallocated;	/* number of elements unallocated on page */
  int		 total_alloc;	/* total number of allocations */
  int		 total_free;	/* total number of frees */
  int		 total_pages;	/* total number of pages allocated */
} alloc_info_t;

/* Type information collected together.  */
typedef struct type_info {
  bt_t	      basic_type;		/* basic type */
  coff_type_t orig_type;		/* original COFF-based type */
  int	      num_tq;			/* # type qualifiers */
  int	      num_dims;			/* # dimensions */
  int	      num_sizes;		/* # sizes */
  int	      extra_sizes;		/* # extra sizes not tied with dims */
  tag_t *     tag_ptr;			/* tag pointer */
  int	      bitfield;			/* symbol is a bitfield */
  int	      unknown_tag;		/* this is an unknown tag */
  tq_t	      type_qualifiers[N_TQ];	/* type qualifiers (ptr, func, array)*/
  symint_t    dimensions     [N_TQ];	/* dimensions for each array */
  symint_t    sizes	     [N_TQ+2];	/* sizes of each array slice + size of
					   struct/union/enum + bitfield size */
} type_info_t;

/* Pre-initialized type_info struct.  */
static type_info_t type_info_init = {
  bt_Nil,				/* basic type */
  T_NULL,				/* original COFF-based type */
  0,					/* # type qualifiers */
  0,					/* # dimensions */
  0,					/* # sizes */
  0,					/* sizes not tied with dims */
  NULL,					/* ptr to tag */
  0,					/* bitfield */
  0,					/* unknown tag */
  {					/* type qualifiers */
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
  },
  {					/* dimensions */
    0,
    0,
    0,
    0,
    0,
    0
  },
  {					/* sizes */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
  },
};


/* Global virtual arrays & hash table for external strings as well as
   for the tags table and global tables for file descriptors, and
   dense numbers.  */

static varray_t file_desc	= INIT_VARRAY (efdr_t);
static varray_t dense_num	= INIT_VARRAY (DNR);
static varray_t tag_strings	= INIT_VARRAY (char);
static varray_t ext_strings	= INIT_VARRAY (char);
static varray_t ext_symbols	= INIT_VARRAY (EXTR);

static shash_t	*orig_str_hash[SHASH_SIZE];
static shash_t	*ext_str_hash [SHASH_SIZE];
static shash_t	*tag_hash     [SHASH_SIZE];

/* Static types for int and void.  Also, remember the last function's
   type (which is set up when we encounter the declaration for the
   function, and used when the end block for the function is emitted.  */

static type_info_t int_type_info;
static type_info_t void_type_info;
static type_info_t last_func_type_info;
static EXTR	  *last_func_eptr;


/* Convert COFF basic type to ECOFF basic type.  The T_NULL type
   really should use bt_Void, but this causes the current ecoff GDB to
   issue unsupported type messages, and the Ultrix 4.00 dbx (aka MIPS
   2.0) doesn't understand it, even though the compiler generates it.
   Maybe this will be fixed in 2.10 or 2.20 of the MIPS compiler
   suite, but for now go with what works.  */

static const bt_t map_coff_types[ (int) T_MAX ] = {
  bt_Nil,			/* T_NULL */
  bt_Nil,			/* T_ARG */
  bt_Char,			/* T_CHAR */
  bt_Short,			/* T_SHORT */
  bt_Int,			/* T_INT */
  bt_Long,			/* T_LONG */
  bt_Float,			/* T_FLOAT */
  bt_Double,			/* T_DOUBLE */
  bt_Struct,			/* T_STRUCT */
  bt_Union,			/* T_UNION */
  bt_Enum,			/* T_ENUM */
  bt_Enum,			/* T_MOE */
  bt_UChar,			/* T_UCHAR */
  bt_UShort,			/* T_USHORT */
  bt_UInt,			/* T_UINT */
  bt_ULong			/* T_ULONG */
};

/* Convert COFF storage class to ECOFF storage class.  */
static const sc_t map_coff_storage[ (int) C_MAX ] = {
  sc_Nil,			/*   0: C_NULL */
  sc_Abs,			/*   1: C_AUTO	  auto var */
  sc_Undefined,			/*   2: C_EXT	  external */
  sc_Data,			/*   3: C_STAT	  static */
  sc_Register,			/*   4: C_REG	  register */
  sc_Undefined,			/*   5: C_EXTDEF  ??? */
  sc_Text,			/*   6: C_LABEL	  label */
  sc_Text,			/*   7: C_ULABEL  user label */
  sc_Info,			/*   8: C_MOS	  member of struct */
  sc_Abs,			/*   9: C_ARG	  argument */
  sc_Info,			/*  10: C_STRTAG  struct tag */
  sc_Info,			/*  11: C_MOU	  member of union */
  sc_Info,			/*  12: C_UNTAG   union tag */
  sc_Info,			/*  13: C_TPDEF	  typedef */
  sc_Data,			/*  14: C_USTATIC ??? */
  sc_Info,			/*  15: C_ENTAG	  enum tag */
  sc_Info,			/*  16: C_MOE	  member of enum */
  sc_Register,			/*  17: C_REGPARM register parameter */
  sc_Bits,			/*  18; C_FIELD	  bitfield */
  sc_Nil,			/*  19 */
  sc_Nil,			/*  20 */
  sc_Nil,			/*  21 */
  sc_Nil,			/*  22 */
  sc_Nil,			/*  23 */
  sc_Nil,			/*  24 */
  sc_Nil,			/*  25 */
  sc_Nil,			/*  26 */
  sc_Nil,			/*  27 */
  sc_Nil,			/*  28 */
  sc_Nil,			/*  29 */
  sc_Nil,			/*  30 */
  sc_Nil,			/*  31 */
  sc_Nil,			/*  32 */
  sc_Nil,			/*  33 */
  sc_Nil,			/*  34 */
  sc_Nil,			/*  35 */
  sc_Nil,			/*  36 */
  sc_Nil,			/*  37 */
  sc_Nil,			/*  38 */
  sc_Nil,			/*  39 */
  sc_Nil,			/*  40 */
  sc_Nil,			/*  41 */
  sc_Nil,			/*  42 */
  sc_Nil,			/*  43 */
  sc_Nil,			/*  44 */
  sc_Nil,			/*  45 */
  sc_Nil,			/*  46 */
  sc_Nil,			/*  47 */
  sc_Nil,			/*  48 */
  sc_Nil,			/*  49 */
  sc_Nil,			/*  50 */
  sc_Nil,			/*  51 */
  sc_Nil,			/*  52 */
  sc_Nil,			/*  53 */
  sc_Nil,			/*  54 */
  sc_Nil,			/*  55 */
  sc_Nil,			/*  56 */
  sc_Nil,			/*  57 */
  sc_Nil,			/*  58 */
  sc_Nil,			/*  59 */
  sc_Nil,			/*  60 */
  sc_Nil,			/*  61 */
  sc_Nil,			/*  62 */
  sc_Nil,			/*  63 */
  sc_Nil,			/*  64 */
  sc_Nil,			/*  65 */
  sc_Nil,			/*  66 */
  sc_Nil,			/*  67 */
  sc_Nil,			/*  68 */
  sc_Nil,			/*  69 */
  sc_Nil,			/*  70 */
  sc_Nil,			/*  71 */
  sc_Nil,			/*  72 */
  sc_Nil,			/*  73 */
  sc_Nil,			/*  74 */
  sc_Nil,			/*  75 */
  sc_Nil,			/*  76 */
  sc_Nil,			/*  77 */
  sc_Nil,			/*  78 */
  sc_Nil,			/*  79 */
  sc_Nil,			/*  80 */
  sc_Nil,			/*  81 */
  sc_Nil,			/*  82 */
  sc_Nil,			/*  83 */
  sc_Nil,			/*  84 */
  sc_Nil,			/*  85 */
  sc_Nil,			/*  86 */
  sc_Nil,			/*  87 */
  sc_Nil,			/*  88 */
  sc_Nil,			/*  89 */
  sc_Nil,			/*  90 */
  sc_Nil,			/*  91 */
  sc_Nil,			/*  92 */
  sc_Nil,			/*  93 */
  sc_Nil,			/*  94 */
  sc_Nil,			/*  95 */
  sc_Nil,			/*  96 */
  sc_Nil,			/*  97 */
  sc_Nil,			/*  98 */
  sc_Nil,			/*  99 */
  sc_Text,			/* 100: C_BLOCK  block start/end */
  sc_Text,			/* 101: C_FCN	 function start/end */
  sc_Info,			/* 102: C_EOS	 end of struct/union/enum */
  sc_Nil,			/* 103: C_FILE	 file start */
  sc_Nil,			/* 104: C_LINE	 line number */
  sc_Nil,			/* 105: C_ALIAS	 combined type info */
  sc_Nil,			/* 106: C_HIDDEN ??? */
};

/* Convert COFF storage class to ECOFF symbol type.  */
static const st_t map_coff_sym_type[ (int) C_MAX ] = {
  st_Nil,			/*   0: C_NULL */
  st_Local,			/*   1: C_AUTO	  auto var */
  st_Global,			/*   2: C_EXT	  external */
  st_Static,			/*   3: C_STAT	  static */
  st_Local,			/*   4: C_REG	  register */
  st_Global,			/*   5: C_EXTDEF  ??? */
  st_Label,			/*   6: C_LABEL	  label */
  st_Label,			/*   7: C_ULABEL  user label */
  st_Member,			/*   8: C_MOS	  member of struct */
  st_Param,			/*   9: C_ARG	  argument */
  st_Block,			/*  10: C_STRTAG  struct tag */
  st_Member,			/*  11: C_MOU	  member of union */
  st_Block,			/*  12: C_UNTAG   union tag */
  st_Typedef,			/*  13: C_TPDEF	  typedef */
  st_Static,			/*  14: C_USTATIC ??? */
  st_Block,			/*  15: C_ENTAG	  enum tag */
  st_Member,			/*  16: C_MOE	  member of enum */
  st_Param,			/*  17: C_REGPARM register parameter */
  st_Member,			/*  18; C_FIELD	  bitfield */
  st_Nil,			/*  19 */
  st_Nil,			/*  20 */
  st_Nil,			/*  21 */
  st_Nil,			/*  22 */
  st_Nil,			/*  23 */
  st_Nil,			/*  24 */
  st_Nil,			/*  25 */
  st_Nil,			/*  26 */
  st_Nil,			/*  27 */
  st_Nil,			/*  28 */
  st_Nil,			/*  29 */
  st_Nil,			/*  30 */
  st_Nil,			/*  31 */
  st_Nil,			/*  32 */
  st_Nil,			/*  33 */
  st_Nil,			/*  34 */
  st_Nil,			/*  35 */
  st_Nil,			/*  36 */
  st_Nil,			/*  37 */
  st_Nil,			/*  38 */
  st_Nil,			/*  39 */
  st_Nil,			/*  40 */
  st_Nil,			/*  41 */
  st_Nil,			/*  42 */
  st_Nil,			/*  43 */
  st_Nil,			/*  44 */
  st_Nil,			/*  45 */
  st_Nil,			/*  46 */
  st_Nil,			/*  47 */
  st_Nil,			/*  48 */
  st_Nil,			/*  49 */
  st_Nil,			/*  50 */
  st_Nil,			/*  51 */
  st_Nil,			/*  52 */
  st_Nil,			/*  53 */
  st_Nil,			/*  54 */
  st_Nil,			/*  55 */
  st_Nil,			/*  56 */
  st_Nil,			/*  57 */
  st_Nil,			/*  58 */
  st_Nil,			/*  59 */
  st_Nil,			/*  60 */
  st_Nil,			/*  61 */
  st_Nil,			/*  62 */
  st_Nil,			/*  63 */
  st_Nil,			/*  64 */
  st_Nil,			/*  65 */
  st_Nil,			/*  66 */
  st_Nil,			/*  67 */
  st_Nil,			/*  68 */
  st_Nil,			/*  69 */
  st_Nil,			/*  70 */
  st_Nil,			/*  71 */
  st_Nil,			/*  72 */
  st_Nil,			/*  73 */
  st_Nil,			/*  74 */
  st_Nil,			/*  75 */
  st_Nil,			/*  76 */
  st_Nil,			/*  77 */
  st_Nil,			/*  78 */
  st_Nil,			/*  79 */
  st_Nil,			/*  80 */
  st_Nil,			/*  81 */
  st_Nil,			/*  82 */
  st_Nil,			/*  83 */
  st_Nil,			/*  84 */
  st_Nil,			/*  85 */
  st_Nil,			/*  86 */
  st_Nil,			/*  87 */
  st_Nil,			/*  88 */
  st_Nil,			/*  89 */
  st_Nil,			/*  90 */
  st_Nil,			/*  91 */
  st_Nil,			/*  92 */
  st_Nil,			/*  93 */
  st_Nil,			/*  94 */
  st_Nil,			/*  95 */
  st_Nil,			/*  96 */
  st_Nil,			/*  97 */
  st_Nil,			/*  98 */
  st_Nil,			/*  99 */
  st_Block,			/* 100: C_BLOCK  block start/end */
  st_Proc,			/* 101: C_FCN	 function start/end */
  st_End,			/* 102: C_EOS	 end of struct/union/enum */
  st_File,			/* 103: C_FILE	 file start */
  st_Nil,			/* 104: C_LINE	 line number */
  st_Nil,			/* 105: C_ALIAS	 combined type info */
  st_Nil,			/* 106: C_HIDDEN ??? */
};

/* Map COFF derived types to ECOFF type qualifiers.  */
static const tq_t map_coff_derived_type[ (int) DT_MAX ] = {
  tq_Nil,			/* 0: DT_NON	no more qualifiers */
  tq_Ptr,			/* 1: DT_PTR	pointer */
  tq_Proc,			/* 2: DT_FCN	function */
  tq_Array,			/* 3: DT_ARY	array */
};


/* Keep track of different sized allocation requests.  */
static alloc_info_t alloc_counts[ (int) alloc_type_last ];


/* Pointers and such to the original symbol table that is read in.  */
static struct filehdr orig_file_header;		/* global object file header */

static HDRR	 orig_sym_hdr;			/* symbolic header on input */
static char	*orig_linenum;			/* line numbers */
static DNR	*orig_dense;			/* dense numbers */
static PDR	*orig_procs;			/* procedures */
static SYMR	*orig_local_syms;		/* local symbols */
static OPTR	*orig_opt_syms;			/* optimization symbols */
static AUXU	*orig_aux_syms;			/* auxiliary symbols */
static char	*orig_local_strs;		/* local strings */
static char	*orig_ext_strs;			/* external strings */
static FDR	*orig_files;			/* file descriptors */
static symint_t	*orig_rfds;			/* relative file desc's */
static EXTR	*orig_ext_syms;			/* external symbols */

/* Macros to convert an index into a given object within the original
   symbol table.  */
#define CHECK(num,max,str) \
  (((unsigned long) num > (unsigned long) max) ? out_of_bounds (num, max, str, __LINE__) : 0)

#define ORIG_LINENUM(indx)	(CHECK ((indx), orig_sym_hdr.cbLine,    "line#"), (indx) + orig_linenum)
#define ORIG_DENSE(indx)	(CHECK ((indx), orig_sym_hdr.idnMax,    "dense"), (indx) + orig_dense)
#define ORIG_PROCS(indx)	(CHECK ((indx), orig_sym_hdr.ipdMax,    "procs"), (indx) + orig_procs)
#define ORIG_FILES(indx)	(CHECK ((indx), orig_sym_hdr.ifdMax,    "funcs"), (indx) + orig_files)
#define ORIG_LSYMS(indx)	(CHECK ((indx), orig_sym_hdr.isymMax,   "lsyms"), (indx) + orig_local_syms)
#define ORIG_LSTRS(indx)	(CHECK ((indx), orig_sym_hdr.issMax,    "lstrs"), (indx) + orig_local_strs)
#define ORIG_ESYMS(indx)	(CHECK ((indx), orig_sym_hdr.iextMax,   "esyms"), (indx) + orig_ext_syms)
#define ORIG_ESTRS(indx)	(CHECK ((indx), orig_sym_hdr.issExtMax, "estrs"), (indx) + orig_ext_strs)
#define ORIG_OPT(indx)		(CHECK ((indx), orig_sym_hdr.ioptMax,   "opt"),   (indx) + orig_opt_syms)
#define ORIG_AUX(indx)		(CHECK ((indx), orig_sym_hdr.iauxMax,   "aux"),   (indx) + orig_aux_syms)
#define ORIG_RFDS(indx)		(CHECK ((indx), orig_sym_hdr.crfd,      "rfds"),  (indx) + orig_rfds)

/* Various other statics.  */
static HDRR	symbolic_header;		/* symbolic header */
static efdr_t  *cur_file_ptr	= (efdr_t *) 0;	/* current file desc. header */
static PDR     *cur_proc_ptr	= (PDR *) 0;	/* current procedure header */
static SYMR    *cur_oproc_begin	= (SYMR *) 0;	/* original proc. sym begin info */
static SYMR    *cur_oproc_end	= (SYMR *) 0;	/* original proc. sym end info */
static PDR     *cur_oproc_ptr	= (PDR *) 0;	/* current original procedure*/
static thead_t *cur_tag_head	= (thead_t *) 0;/* current tag head */
static unsigned long file_offset	= 0;	/* current file offset */
static unsigned long max_file_offset	= 0;	/* maximum file offset */
static FILE    *object_stream	= (FILE *) 0;	/* file desc. to output .o */
static FILE    *obj_in_stream	= (FILE *) 0;	/* file desc. to input .o */
static char    *progname	= (char *) 0;	/* program name for errors */
static const char *input_name	= "stdin";	/* name of input file */
static char    *object_name	= (char *) 0;	/* tmp. name of object file */
static char    *obj_in_name	= (char *) 0;	/* name of input object file */
static char    *cur_line_start	= (char *) 0;	/* current line read in */
static char    *cur_line_ptr	= (char *) 0;	/* ptr within current line */
static unsigned	cur_line_nbytes	= 0;		/* # bytes for current line */
static unsigned	cur_line_alloc	= 0;		/* # bytes total in buffer */
static long	line_number	= 0;		/* current input line number */
static int	debug		= 0;		/* trace functions */
static int	version		= 0;		/* print version # */
static int	verbose		= 0;
static int	had_errors	= 0;		/* != 0 if errors were found */
static int	rename_output	= 0;		/* != 0 if rename output file*/
static int	delete_input	= 0;		/* != 0 if delete input after done */
static int	stabs_seen	= 0;		/* != 0 if stabs have been seen */


/* Pseudo symbol to use when putting stabs into the symbol table.  */
#ifndef STABS_SYMBOL
#define STABS_SYMBOL "@stabs"
#endif

static const char stabs_symbol[] = STABS_SYMBOL;


/* Forward reference for functions.  See the definition for more details.  */

#ifndef STATIC
#define STATIC static
#endif

STATIC int out_of_bounds (symint_t, symint_t, const char *, int);
STATIC shash_t *hash_string (const char *, Ptrdiff_t, shash_t **, symint_t *);
STATIC symint_t	add_string (varray_t *, shash_t **, const char *, const char *,
			    shash_t **);
STATIC symint_t	add_local_symbol (const char *, const char *, st_t, sc_t,
				  symint_t, symint_t);
STATIC symint_t	add_ext_symbol (EXTR *, int);
STATIC symint_t	add_aux_sym_symint (symint_t);
STATIC symint_t	add_aux_sym_rndx (int, symint_t);
STATIC symint_t	add_aux_sym_tir (type_info_t *, hash_state_t, thash_t **);
STATIC tag_t *	get_tag (const char *, const char *, symint_t, bt_t);
STATIC void add_unknown_tag (tag_t *);
STATIC void add_procedure (const char *, const char *);
STATIC void initialize_init_file (void);
STATIC void add_file (const char *, const char *);
STATIC void add_bytes (varray_t *, char *, Size_t);
STATIC void add_varray_page (varray_t *);
STATIC void update_headers (void);
STATIC void write_varray (varray_t *, off_t, const char *);
STATIC void write_object (void);
STATIC const char *st_to_string (st_t);
STATIC const char *sc_to_string (sc_t);
STATIC char *read_line (void);
STATIC void parse_input (void);
STATIC void mark_stabs (const char *);
STATIC void parse_begin (const char *);
STATIC void parse_bend (const char *);
STATIC void parse_def (const char *);
STATIC void parse_end (const char *);
STATIC void parse_ent (const char *);
STATIC void parse_file (const char *);
STATIC void parse_stabs_common (const char *, const char *, const char *);
STATIC void parse_stabs (const char *);
STATIC void parse_stabn (const char *);
STATIC page_t  *read_seek (Size_t, off_t, const char *);
STATIC void copy_object (void);

STATIC void catch_signal (int) ATTRIBUTE_NORETURN;
STATIC page_t *allocate_page (void);
STATIC page_t *allocate_multiple_pages (Size_t);
STATIC void	free_multiple_pages (page_t *, Size_t);

#ifndef MALLOC_CHECK
STATIC page_t  *allocate_cluster (Size_t);
#endif

STATIC forward_t *allocate_forward (void);
STATIC scope_t *allocate_scope (void);
STATIC shash_t *allocate_shash (void);
STATIC tag_t  *allocate_tag (void);
STATIC thash_t *allocate_thash (void);
STATIC thead_t *allocate_thead (void);
STATIC vlinks_t *allocate_vlinks (void);

STATIC void free_forward (forward_t *);
STATIC void free_scope (scope_t *);
STATIC void free_tag (tag_t *);
STATIC void free_thead (thead_t *);

extern char *optarg;
extern int   optind;
extern int   opterr;

/* List of assembler pseudo ops and beginning sequences that need
   special actions.  Someday, this should be a hash table, and such,
   but for now a linear list of names and calls to memcmp will
   do......  */

typedef struct _pseudo_ops {
  const char *const name;			/* pseudo-op in ascii */
  const int len;				/* length of name to compare */
  void (*const func) (const char *);	/* function to handle line */
} pseudo_ops_t;

static const pseudo_ops_t pseudo_ops[] = {
  { "#.def",	sizeof("#.def")-1,	parse_def },
  { "#.begin",	sizeof("#.begin")-1,	parse_begin },
  { "#.bend",	sizeof("#.bend")-1,	parse_bend },
  { ".end",	sizeof(".end")-1,	parse_end },
  { ".ent",	sizeof(".ent")-1,	parse_ent },
  { ".file",	sizeof(".file")-1,	parse_file },
  { "#.stabs",	sizeof("#.stabs")-1,	parse_stabs },
  { "#.stabn",	sizeof("#.stabn")-1,	parse_stabn },
  { ".stabs",	sizeof(".stabs")-1,	parse_stabs },
  { ".stabn",	sizeof(".stabn")-1,	parse_stabn },
  { "#@stabs",	sizeof("#@stabs")-1,	mark_stabs },
};


/* Command line options for getopt_long.  */

static const struct option options[] =
{
  { "version", 0, 0, 'V' },
  { "verbose", 0, 0, 'v' },
  { 0, 0, 0, 0 }
};

/* Add a page to a varray object.  */

STATIC void
add_varray_page (varray_t *vp)
{
  vlinks_t *new_links = allocate_vlinks ();

#ifdef MALLOC_CHECK
  if (vp->object_size > 1)
    new_links->datum = xcalloc (1, vp->object_size);
  else
#endif
    new_links->datum = allocate_page ();

  alloc_counts[ (int) alloc_type_varray ].total_alloc++;
  alloc_counts[ (int) alloc_type_varray ].total_pages++;

  new_links->start_index = vp->num_allocated;
  vp->objects_last_page = 0;

  if (vp->first == (vlinks_t *) 0)		/* first allocation? */
    vp->first = vp->last = new_links;
  else
    {						/* 2nd or greater allocation */
      new_links->prev = vp->last;
      vp->last->next = new_links;
      vp->last = new_links;
    }
}


/* Compute hash code (from tree.c) */

#define HASHBITS 30

STATIC shash_t *
hash_string (const char *text, Ptrdiff_t hash_len, shash_t **hash_tbl,
	     symint_t *ret_hash_index)
{
  unsigned long hi;
  Ptrdiff_t i;
  shash_t *ptr;
  int first_ch = *text;

  hi = hash_len;
  for (i = 0; i < hash_len; i++)
    hi = ((hi & 0x003fffff) * 613) + (text[i] & 0xff);

  hi &= (1 << HASHBITS) - 1;
  hi %= SHASH_SIZE;

  if (ret_hash_index != (symint_t *) 0)
    *ret_hash_index = hi;

  for (ptr = hash_tbl[hi]; ptr != (shash_t *) 0; ptr = ptr->next)
    if ((symint_t) hash_len == ptr->len
	&& first_ch == ptr->string[0]
	&& memcmp (text, ptr->string, hash_len) == 0)
      break;

  return ptr;
}


/* Add a string (and null pad) to one of the string tables.  A
   consequence of hashing strings, is that we don't let strings cross
   page boundaries.  The extra nulls will be ignored.  VP is a string
   virtual array, HASH_TBL a pointer to the hash table, the string
   starts at START and the position one byte after the string is given
   with END_P1, the resulting hash pointer is returned in RET_HASH.  */

STATIC symint_t
add_string (varray_t *vp, shash_t **hash_tbl, const char *start,
	    const char *end_p1, shash_t **ret_hash)
{
  Ptrdiff_t len = end_p1 - start;
  shash_t *hash_ptr;
  symint_t hi;

  if (len >= (Ptrdiff_t) PAGE_USIZE)
    fatal ("string too big (%ld bytes)", (long) len);

  hash_ptr = hash_string (start, len, hash_tbl, &hi);
  if (hash_ptr == (shash_t *) 0)
    {
      char *p;

      if (vp->objects_last_page + len >= (long) PAGE_USIZE)
	{
	  vp->num_allocated
	    = ((vp->num_allocated + PAGE_USIZE - 1) / PAGE_USIZE) * PAGE_USIZE;
	  add_varray_page (vp);
	}

      hash_ptr = allocate_shash ();
      hash_ptr->next = hash_tbl[hi];
      hash_tbl[hi] = hash_ptr;

      hash_ptr->len = len;
      hash_ptr->indx = vp->num_allocated;
      hash_ptr->string = p = & vp->last->datum->byte[ vp->objects_last_page ];

      vp->objects_last_page += len+1;
      vp->num_allocated += len+1;

      while (len-- > 0)
	*p++ = *start++;

      *p = '\0';
    }

  if (ret_hash != (shash_t **) 0)
    *ret_hash = hash_ptr;

  return hash_ptr->indx;
}


/* Add a local symbol.  The symbol string starts at STR_START and the
   first byte after it is marked by STR_END_P1.  The symbol has type
   TYPE and storage class STORAGE and value VALUE.  INDX is an index
   to local/aux. symbols.  */

STATIC symint_t
add_local_symbol (const char *str_start, const char *str_end_p1, st_t type,
		  sc_t storage,  symint_t value, symint_t indx)
{
  symint_t ret;
  SYMR *psym;
  scope_t *pscope;
  thead_t *ptag_head;
  tag_t *ptag;
  tag_t *ptag_next;
  varray_t *vp = &cur_file_ptr->symbols;
  int scope_delta = 0;
  shash_t *hash_ptr = (shash_t *) 0;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  psym = &vp->last->datum->sym[ vp->objects_last_page++ ];

  psym->value = value;
  psym->st = (unsigned) type;
  psym->sc = (unsigned) storage;
  psym->index = indx;
  psym->iss = (str_start == (const char *) 0)
		? 0
		: add_string (&cur_file_ptr->strings,
			      &cur_file_ptr->shash_head[0],
			      str_start,
			      str_end_p1,
			      &hash_ptr);

  ret = vp->num_allocated++;

  if (MIPS_IS_STAB (psym))
    return ret;

  /* Save the symbol within the hash table if this is a static
     item, and it has a name.  */
  if (hash_ptr != (shash_t *) 0
      && (type == st_Global || type == st_Static || type == st_Label
	  || type == st_Proc || type == st_StaticProc))
    hash_ptr->sym_ptr = psym;

  /* push or pop a scope if appropriate.  */
  switch (type)
    {
    default:
      break;

    case st_File:			/* beginning of file */
    case st_Proc:			/* procedure */
    case st_StaticProc:			/* static procedure */
    case st_Block:			/* begin scope */
      pscope = allocate_scope ();
      pscope->prev = cur_file_ptr->cur_scope;
      pscope->lsym = psym;
      pscope->lnumber = ret;
      pscope->type = type;
      cur_file_ptr->cur_scope = pscope;

      if (type != st_File)
	scope_delta = 1;

      /* For every block type except file, struct, union, or
	 enumeration blocks, push a level on the tag stack.  We omit
	 file types, so that tags can span file boundaries.  */
      if (type != st_File && storage != sc_Info)
	{
	  ptag_head = allocate_thead ();
	  ptag_head->first_tag = 0;
	  ptag_head->prev = cur_tag_head;
	  cur_tag_head = ptag_head;
	}
      break;

    case st_End:
      pscope = cur_file_ptr->cur_scope;
      if (pscope == (scope_t *) 0)
	error ("internal error, too many st_End's");

      else
	{
	  st_t begin_type = (st_t) pscope->lsym->st;

	  if (begin_type != st_File)
	    scope_delta = -1;

	  /* Except for file, structure, union, or enumeration end
	     blocks remove all tags created within this scope.  */
	  if (begin_type != st_File && storage != sc_Info)
	    {
	      ptag_head = cur_tag_head;
	      cur_tag_head = ptag_head->prev;

	      for (ptag = ptag_head->first_tag;
		   ptag != (tag_t *) 0;
		   ptag = ptag_next)
		{
		  if (ptag->forward_ref != (forward_t *) 0)
		    add_unknown_tag (ptag);

		  ptag_next = ptag->same_block;
		  ptag->hash_ptr->tag_ptr = ptag->same_name;
		  free_tag (ptag);
		}

	      free_thead (ptag_head);
	    }

	  cur_file_ptr->cur_scope = pscope->prev;
	  psym->index = pscope->lnumber;	/* blk end gets begin sym # */

	  if (storage != sc_Info)
	    psym->iss = pscope->lsym->iss;	/* blk end gets same name */

	  if (begin_type == st_File || begin_type == st_Block)
	    pscope->lsym->index = ret+1;	/* block begin gets next sym # */

	  /* Functions push two or more aux words as follows:
	     1st word: index+1 of the end symbol
	     2nd word: type of the function (plus any aux words needed).
	     Also, tie the external pointer back to the function begin symbol.  */
	  else
	    {
	      symint_t type;
	      pscope->lsym->index = add_aux_sym_symint (ret+1);
	      type = add_aux_sym_tir (&last_func_type_info,
				      hash_no,
				      &cur_file_ptr->thash_head[0]);
	      if (last_func_eptr)
		{
		  last_func_eptr->ifd = cur_file_ptr->file_index;

		  /* The index for an external st_Proc symbol is the index
		     of the st_Proc symbol in the local symbol table.  */
		  last_func_eptr->asym.index = psym->index;
		}
	    }

	  free_scope (pscope);
	}
    }

  cur_file_ptr->nested_scopes += scope_delta;

  if (debug && type != st_File
      && (debug > 2 || type == st_Block || type == st_End
	  || type == st_Proc || type == st_StaticProc))
    {
      const char *sc_str = sc_to_string (storage);
      const char *st_str = st_to_string (type);
      int depth = cur_file_ptr->nested_scopes + (scope_delta < 0);

      fprintf (stderr,
	       "\tlsym\tv= %10ld, depth= %2d, sc= %-12s",
	       value, depth, sc_str);

      if (str_start && str_end_p1 - str_start > 0)
	fprintf (stderr, " st= %-11s name= %.*s\n",
		 st_str, (int) (str_end_p1 - str_start), str_start);
      else
	{
	  Size_t len = strlen (st_str);
	  fprintf (stderr, " st= %.*s\n", (int) (len-1), st_str);
	}
    }

  return ret;
}


/* Add an external symbol with symbol pointer ESYM and file index
   IFD.  */

STATIC symint_t
add_ext_symbol (EXTR *esym, int ifd)
{
  const char *str_start;		/* first byte in string */
  const char *str_end_p1;		/* first byte after string */
  EXTR *psym;
  varray_t *vp = &ext_symbols;
  shash_t *hash_ptr = (shash_t *) 0;

  str_start = ORIG_ESTRS (esym->asym.iss);
  str_end_p1 = str_start + strlen (str_start);

  if (debug > 1)
    {
      long value = esym->asym.value;
      const char *sc_str = sc_to_string (esym->asym.sc);
      const char *st_str = st_to_string (esym->asym.st);

      fprintf (stderr,
	       "\tesym\tv= %10ld, ifd= %2d, sc= %-12s",
	       value, ifd, sc_str);

      if (str_start && str_end_p1 - str_start > 0)
	fprintf (stderr, " st= %-11s name= %.*s\n",
		 st_str, (int) (str_end_p1 - str_start), str_start);
      else
	fprintf (stderr, " st= %s\n", st_str);
    }

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  psym = &vp->last->datum->esym[ vp->objects_last_page++ ];

  *psym = *esym;
  psym->ifd = ifd;
  psym->asym.index = indexNil;
  psym->asym.iss   = (str_start == (const char *) 0)
			? 0
			: add_string (&ext_strings,
				      &ext_str_hash[0],
				      str_start,
				      str_end_p1,
				      &hash_ptr);

  hash_ptr->esym_ptr = psym;
  return vp->num_allocated++;
}


/* Add an auxiliary symbol (passing a symint).  */

STATIC symint_t
add_aux_sym_symint (symint_t aux_word)
{
  AUXU *aux_ptr;
  efdr_t *file_ptr = cur_file_ptr;
  varray_t *vp = &file_ptr->aux_syms;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[ vp->objects_last_page++ ];
  aux_ptr->isym = aux_word;

  return vp->num_allocated++;
}


/* Add an auxiliary symbol (passing a file/symbol index combo).  */

STATIC symint_t
add_aux_sym_rndx (int file_index, symint_t sym_index)
{
  AUXU *aux_ptr;
  efdr_t *file_ptr = cur_file_ptr;
  varray_t *vp = &file_ptr->aux_syms;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[ vp->objects_last_page++ ];
  aux_ptr->rndx.rfd   = file_index;
  aux_ptr->rndx.index = sym_index;

  return vp->num_allocated++;
}


/* Add an auxiliary symbol (passing the basic type and possibly
   type qualifiers).  */

STATIC symint_t
add_aux_sym_tir (type_info_t *t, hash_state_t state, thash_t **hash_tbl)
{
  AUXU *aux_ptr;
  efdr_t *file_ptr = cur_file_ptr;
  varray_t *vp = &file_ptr->aux_syms;
  static AUXU init_aux;
  symint_t ret;
  int i;
  AUXU aux;

  aux = init_aux;
  aux.ti.bt = (int) t->basic_type;
  aux.ti.continued = 0;
  aux.ti.fBitfield = t->bitfield;

  aux.ti.tq0 = (int) t->type_qualifiers[0];
  aux.ti.tq1 = (int) t->type_qualifiers[1];
  aux.ti.tq2 = (int) t->type_qualifiers[2];
  aux.ti.tq3 = (int) t->type_qualifiers[3];
  aux.ti.tq4 = (int) t->type_qualifiers[4];
  aux.ti.tq5 = (int) t->type_qualifiers[5];


  /* For anything that adds additional information, we must not hash,
     so check here, and reset our state.  */

  if (state != hash_no
      && (t->type_qualifiers[0] == tq_Array
	  || t->type_qualifiers[1] == tq_Array
	  || t->type_qualifiers[2] == tq_Array
	  || t->type_qualifiers[3] == tq_Array
	  || t->type_qualifiers[4] == tq_Array
	  || t->type_qualifiers[5] == tq_Array
	  || t->basic_type == bt_Struct
	  || t->basic_type == bt_Union
	  || t->basic_type == bt_Enum
	  || t->bitfield
	  || t->num_dims > 0))
    state = hash_no;

  /* See if we can hash this type, and save some space, but some types
     can't be hashed (because they contain arrays or continuations),
     and others can be put into the hash list, but cannot use existing
     types because other aux entries precede this one.  */

  if (state != hash_no)
    {
      thash_t *hash_ptr;
      symint_t hi;

      hi = aux.isym & ((1 << HASHBITS) - 1);
      hi %= THASH_SIZE;

      for (hash_ptr = hash_tbl[hi];
	   hash_ptr != (thash_t *) 0;
	   hash_ptr = hash_ptr->next)
	{
	  if (aux.isym == hash_ptr->type.isym)
	    break;
	}

      if (hash_ptr != (thash_t *) 0 && state == hash_yes)
	return hash_ptr->indx;

      if (hash_ptr == (thash_t *) 0)
	{
	  hash_ptr = allocate_thash ();
	  hash_ptr->next = hash_tbl[hi];
	  hash_ptr->type = aux;
	  hash_ptr->indx = vp->num_allocated;
	  hash_tbl[hi] = hash_ptr;
	}
    }

  /* Everything is set up, add the aux symbol.  */
  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[ vp->objects_last_page++ ];
  *aux_ptr = aux;

  ret = vp->num_allocated++;

  /* Add bitfield length if it exists.

     NOTE:  Mips documentation claims bitfield goes at the end of the
     AUX record, but the DECstation compiler emits it here.
     (This would only make a difference for enum bitfields.)

     Also note:  We use the last size given since gcc may emit 2
     for an enum bitfield.  */

  if (t->bitfield)
    (void) add_aux_sym_symint ((symint_t) t->sizes[t->num_sizes-1]);


  /* Add tag information if needed.  Structure, union, and enum
     references add 2 aux symbols: a [file index, symbol index]
     pointer to the structure type, and the current file index.  */

  if (t->basic_type == bt_Struct
      || t->basic_type == bt_Union
      || t->basic_type == bt_Enum)
    {
      symint_t file_index = t->tag_ptr->ifd;
      symint_t sym_index  = t->tag_ptr->indx;

      if (t->unknown_tag)
	{
	  (void) add_aux_sym_rndx (ST_RFDESCAPE, sym_index);
	  (void) add_aux_sym_symint ((symint_t)-1);
	}
      else if (sym_index != indexNil)
	{
	  (void) add_aux_sym_rndx (ST_RFDESCAPE, sym_index);
	  (void) add_aux_sym_symint (file_index);
	}
      else
	{
	  forward_t *forward_ref = allocate_forward ();

	  forward_ref->type_ptr = aux_ptr;
	  forward_ref->next = t->tag_ptr->forward_ref;
	  t->tag_ptr->forward_ref = forward_ref;

	  (void) add_aux_sym_rndx (ST_RFDESCAPE, sym_index);
	  forward_ref->index_ptr
	    = &vp->last->datum->aux[ vp->objects_last_page - 1];

	  (void) add_aux_sym_symint (file_index);
	  forward_ref->ifd_ptr
	    = &vp->last->datum->aux[ vp->objects_last_page - 1];
	}
    }

  /* Add information about array bounds if they exist.  */
  for (i = 0; i < t->num_dims; i++)
    {
      (void) add_aux_sym_rndx (ST_RFDESCAPE,
			       cur_file_ptr->int_type);

      (void) add_aux_sym_symint (cur_file_ptr->file_index);	/* file index*/
      (void) add_aux_sym_symint ((symint_t) 0);			/* low bound */
      (void) add_aux_sym_symint (t->dimensions[i] - 1);		/* high bound*/
      (void) add_aux_sym_symint ((t->dimensions[i] == 0)	/* stride */
			      ? 0
			      : (t->sizes[i] * 8) / t->dimensions[i]);
    };

  /* NOTE:  Mips documentation claims that the bitfield width goes here.
     But it needs to be emitted earlier.  */

  return ret;
}


/* Add a tag to the tag table (unless it already exists).  */

STATIC tag_t *
get_tag (const char *tag_start,		/* 1st byte of tag name */
	 const char *tag_end_p1,	/* 1st byte after tag name */
	 symint_t indx,		/* index of tag start block */
	 bt_t basic_type)		/* bt_Struct, bt_Union, or bt_Enum */

{
  shash_t *hash_ptr;
  tag_t *tag_ptr;
  hash_ptr = hash_string (tag_start,
			  tag_end_p1 - tag_start,
			  &tag_hash[0],
			  (symint_t *) 0);

  if (hash_ptr != (shash_t *) 0
      && hash_ptr->tag_ptr != (tag_t *) 0)
  {
    tag_ptr = hash_ptr->tag_ptr;
    if (indx != indexNil)
      {
	tag_ptr->basic_type = basic_type;
	tag_ptr->ifd	    = cur_file_ptr->file_index;
	tag_ptr->indx	    = indx;
      }
    return tag_ptr;
  }

  (void) add_string (&tag_strings,
		     &tag_hash[0],
		     tag_start,
		     tag_end_p1,
		     &hash_ptr);

  tag_ptr = allocate_tag ();
  tag_ptr->forward_ref	= (forward_t *) 0;
  tag_ptr->hash_ptr	= hash_ptr;
  tag_ptr->same_name	= hash_ptr->tag_ptr;
  tag_ptr->basic_type	= basic_type;
  tag_ptr->indx		= indx;
  tag_ptr->ifd		= (indx == indexNil
			   ? (symint_t) -1 : cur_file_ptr->file_index);
  tag_ptr->same_block	= cur_tag_head->first_tag;

  cur_tag_head->first_tag = tag_ptr;
  hash_ptr->tag_ptr	  = tag_ptr;

  return tag_ptr;
}


/* Add an unknown {struct, union, enum} tag.  */

STATIC void
add_unknown_tag (tag_t *ptag)
{
  shash_t *hash_ptr	= ptag->hash_ptr;
  char *name_start	= hash_ptr->string;
  char *name_end_p1	= name_start + hash_ptr->len;
  forward_t *f_next	= ptag->forward_ref;
  forward_t *f_cur;
  int sym_index;
  int file_index	= cur_file_ptr->file_index;

  if (debug > 1)
    {
      const char *agg_type = "{unknown aggregate type}";
      switch (ptag->basic_type)
	{
	case bt_Struct:	agg_type = "struct";	break;
	case bt_Union:	agg_type = "union";	break;
	case bt_Enum:	agg_type = "enum";	break;
	default:				break;
	}

      fprintf (stderr, "unknown %s %.*s found\n",
	       agg_type, (int) hash_ptr->len, name_start);
    }

  sym_index = add_local_symbol (name_start,
				name_end_p1,
				st_Block,
				sc_Info,
				(symint_t) 0,
				(symint_t) 0);

  (void) add_local_symbol (name_start,
			   name_end_p1,
			   st_End,
			   sc_Info,
			   (symint_t) 0,
			   (symint_t) 0);

  while (f_next != (forward_t *) 0)
    {
      f_cur  = f_next;
      f_next = f_next->next;

      f_cur->ifd_ptr->isym = file_index;
      f_cur->index_ptr->rndx.index = sym_index;

      free_forward (f_cur);
    }

  return;
}


/* Add a procedure to the current file's list of procedures, and record
   this is the current procedure.  If the assembler created a PDR for
   this procedure, use that to initialize the current PDR.  */

STATIC void
add_procedure (const char *func_start,  /* 1st byte of func name */
	       const char *func_end_p1) /* 1st byte after func name */
{
  PDR *new_proc_ptr;
  efdr_t *file_ptr = cur_file_ptr;
  varray_t *vp = &file_ptr->procs;
  symint_t value = 0;
  st_t proc_type = st_Proc;
  shash_t *shash_ptr = hash_string (func_start,
				    func_end_p1 - func_start,
				    &orig_str_hash[0],
				    (symint_t *) 0);

  if (debug)
    fputc ('\n', stderr);

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  cur_proc_ptr = new_proc_ptr = &vp->last->datum->proc[ vp->objects_last_page++ ];

  vp->num_allocated++;


  /* Did the assembler create this procedure?  If so, get the PDR information.  */
  cur_oproc_ptr = (PDR *) 0;
  if (shash_ptr != (shash_t *) 0)
    {
      PDR *old_proc_ptr = shash_ptr->proc_ptr;
      SYMR *sym_ptr = shash_ptr->sym_ptr;

      if (old_proc_ptr != (PDR *) 0
	  && sym_ptr != (SYMR *) 0
	  && ((st_t) sym_ptr->st == st_Proc || (st_t) sym_ptr->st == st_StaticProc))
	{
	  cur_oproc_begin = sym_ptr;
	  cur_oproc_end = shash_ptr->end_ptr;
	  value = sym_ptr->value;

	  cur_oproc_ptr = old_proc_ptr;
	  proc_type = (st_t) sym_ptr->st;
	  *new_proc_ptr = *old_proc_ptr;	/* initialize */
	}
    }

  if (cur_oproc_ptr == (PDR *) 0)
    error ("did not find a PDR block for %.*s",
	   (int) (func_end_p1 - func_start), func_start);

  /* Determine the start of symbols.  */
  new_proc_ptr->isym = file_ptr->symbols.num_allocated;

  /* Push the start of the function.  */
  (void) add_local_symbol (func_start, func_end_p1,
			   proc_type, sc_Text,
			   value,
			   (symint_t) 0);
}


/* Initialize the init_file structure.  */

STATIC void
initialize_init_file (void)
{
  memset (&init_file, 0, sizeof (init_file));

  init_file.fdr.lang = langC;
  init_file.fdr.fMerge = 1;
  init_file.fdr.glevel = GLEVEL_2;

#ifdef WORDS_BIG_ENDIAN
  init_file.fdr.fBigendian = 1;
#endif

  INITIALIZE_VARRAY (&init_file.strings, char);
  INITIALIZE_VARRAY (&init_file.symbols, SYMR);
  INITIALIZE_VARRAY (&init_file.procs, PDR);
  INITIALIZE_VARRAY (&init_file.aux_syms, AUXU);

  init_file_initialized = 1;
}

/* Add a new filename, and set up all of the file relative
   virtual arrays (strings, symbols, aux syms, etc.).  Record
   where the current file structure lives.  */

STATIC void
add_file (const char *file_start,  /* first byte in string */
	  const char *file_end_p1) /* first byte after string */
{
  static char zero_bytes[2] = { '\0', '\0' };

  Ptrdiff_t len = file_end_p1 - file_start;
  int first_ch = *file_start;
  efdr_t *file_ptr;

  if (debug)
    fprintf (stderr, "\tfile\t%.*s\n", (int) len, file_start);

  /* See if the file has already been created.  */
  for (file_ptr = first_file;
       file_ptr != (efdr_t *) 0;
       file_ptr = file_ptr->next_file)
    {
      if (first_ch == file_ptr->name[0]
	  && file_ptr->name[len] == '\0'
	  && memcmp (file_start, file_ptr->name, len) == 0)
	{
	  cur_file_ptr = file_ptr;
	  break;
	}
    }

  /* If this is a new file, create it.  */
  if (file_ptr == (efdr_t *) 0)
    {
      if (file_desc.objects_last_page == file_desc.objects_per_page)
	add_varray_page (&file_desc);

      if (! init_file_initialized)
	initialize_init_file ();

      file_ptr = cur_file_ptr
	= &file_desc.last->datum->file[ file_desc.objects_last_page++ ];
      *file_ptr = init_file;

      file_ptr->file_index = file_desc.num_allocated++;

      /* Allocate the string hash table.  */
      file_ptr->shash_head = (shash_t **) allocate_page ();

      /* Make sure 0 byte in string table is null  */
      add_string (&file_ptr->strings,
		  &file_ptr->shash_head[0],
		  &zero_bytes[0],
		  &zero_bytes[0],
		  (shash_t **) 0);

      if (file_end_p1 - file_start > (long) PAGE_USIZE-2)
	fatal ("filename goes over one page boundary");

      /* Push the start of the filename. We assume that the filename
         will be stored at string offset 1.  */
      (void) add_local_symbol (file_start, file_end_p1, st_File, sc_Text,
			       (symint_t) 0, (symint_t) 0);
      file_ptr->fdr.rss = 1;
      file_ptr->name = &file_ptr->strings.last->datum->byte[1];
      file_ptr->name_len = file_end_p1 - file_start;

      /* Update the linked list of file descriptors.  */
      *last_file_ptr = file_ptr;
      last_file_ptr = &file_ptr->next_file;

      /* Add void & int types to the file (void should be first to catch
	 errant 0's within the index fields).  */
      file_ptr->void_type = add_aux_sym_tir (&void_type_info,
					     hash_yes,
					     &cur_file_ptr->thash_head[0]);

      file_ptr->int_type = add_aux_sym_tir (&int_type_info,
					    hash_yes,
					    &cur_file_ptr->thash_head[0]);
    }
}


/* Add a stream of random bytes to a varray.  */

STATIC void
add_bytes (varray_t *vp,	/* virtual array to add too */
	   char *input_ptr,	/* start of the bytes */
	   Size_t nitems)	/* # items to move */
{
  Size_t move_items;
  Size_t move_bytes;
  char *ptr;

  while (nitems > 0)
    {
      if (vp->objects_last_page >= vp->objects_per_page)
	add_varray_page (vp);

      ptr = &vp->last->datum->byte[ vp->objects_last_page * vp->object_size ];
      move_items = vp->objects_per_page - vp->objects_last_page;
      if (move_items > nitems)
	move_items = nitems;

      move_bytes = move_items * vp->object_size;
      nitems -= move_items;

      if (move_bytes >= 32)
	{
	  (void) memcpy (ptr, input_ptr, move_bytes);
	  input_ptr += move_bytes;
	}
      else
	{
	  while (move_bytes-- > 0)
	    *ptr++ = *input_ptr++;
	}
    }
}


/* Convert storage class to string.  */

STATIC const char *
sc_to_string (sc_t storage_class)
{
  switch (storage_class)
    {
    case sc_Nil:	 return "Nil,";
    case sc_Text:	 return "Text,";
    case sc_Data:	 return "Data,";
    case sc_Bss:	 return "Bss,";
    case sc_Register:	 return "Register,";
    case sc_Abs:	 return "Abs,";
    case sc_Undefined:	 return "Undefined,";
    case sc_CdbLocal:	 return "CdbLocal,";
    case sc_Bits:	 return "Bits,";
    case sc_CdbSystem:	 return "CdbSystem,";
    case sc_RegImage:	 return "RegImage,";
    case sc_Info:	 return "Info,";
    case sc_UserStruct:	 return "UserStruct,";
    case sc_SData:	 return "SData,";
    case sc_SBss:	 return "SBss,";
    case sc_RData:	 return "RData,";
    case sc_Var:	 return "Var,";
    case sc_Common:	 return "Common,";
    case sc_SCommon:	 return "SCommon,";
    case sc_VarRegister: return "VarRegister,";
    case sc_Variant:	 return "Variant,";
    case sc_SUndefined:	 return "SUndefined,";
    case sc_Init:	 return "Init,";
    case sc_Max:	 return "Max,";
    }

  return "???,";
}


/* Convert symbol type to string.  */

STATIC const char *
st_to_string (st_t symbol_type)
{
  switch (symbol_type)
    {
    case st_Nil:	return "Nil,";
    case st_Global:	return "Global,";
    case st_Static:	return "Static,";
    case st_Param:	return "Param,";
    case st_Local:	return "Local,";
    case st_Label:	return "Label,";
    case st_Proc:	return "Proc,";
    case st_Block:	return "Block,";
    case st_End:	return "End,";
    case st_Member:	return "Member,";
    case st_Typedef:	return "Typedef,";
    case st_File:	return "File,";
    case st_RegReloc:	return "RegReloc,";
    case st_Forward:	return "Forward,";
    case st_StaticProc:	return "StaticProc,";
    case st_Constant:	return "Constant,";
    case st_Str:	return "String,";
    case st_Number:	return "Number,";
    case st_Expr:	return "Expr,";
    case st_Type:	return "Type,";
    case st_Max:	return "Max,";
    }

  return "???,";
}


/* Read a line from standard input, and return the start of the buffer
   (which is grows if the line is too big).  We split lines at the
   semi-colon, and return each logical line independently.  */

STATIC char *
read_line (void)
{
  static   int line_split_p	= 0;
  int string_p		= 0;
  int comment_p	= 0;
  int ch;
  char *ptr;

  if (cur_line_start == (char *) 0)
    {				/* allocate initial page */
      cur_line_start = (char *) allocate_page ();
      cur_line_alloc = PAGE_SIZE;
    }

  if (!line_split_p)
    line_number++;

  line_split_p = 0;
  cur_line_nbytes = 0;

  for (ptr = cur_line_start; (ch = getchar ()) != EOF; *ptr++ = ch)
    {
      if (++cur_line_nbytes >= cur_line_alloc-1)
	{
	  int num_pages = cur_line_alloc / PAGE_SIZE;
	  char *old_buffer = cur_line_start;

	  cur_line_alloc += PAGE_SIZE;
	  cur_line_start = (char *) allocate_multiple_pages (num_pages+1);
	  memcpy (cur_line_start, old_buffer, num_pages * PAGE_SIZE);

	  ptr = cur_line_start + cur_line_nbytes - 1;
	}

      if (ch == '\n')
	{
	  *ptr++ = '\n';
	  *ptr = '\0';
	  cur_line_ptr = cur_line_start;
	  return cur_line_ptr;
	}

      else if (ch == '\0')
	error ("null character found in input");

      else if (!comment_p)
	{
	  if (ch == '"')
	    string_p = !string_p;

	  else if (ch == '#')
	    comment_p++;

	  else if (ch == ';' && !string_p)
	    {
	      line_split_p = 1;
	      *ptr++ = '\n';
	      *ptr = '\0';
	      cur_line_ptr = cur_line_start;
	      return cur_line_ptr;
	    }
	}
    }

  if (ferror (stdin))
    pfatal_with_name (input_name);

  cur_line_ptr = (char *) 0;
  return (char *) 0;
}


/* Parse #.begin directives which have a label as the first argument
   which gives the location of the start of the block.  */

STATIC void
parse_begin (const char *start)
{
  const char *end_p1;			/* end of label */
  int ch;
  shash_t *hash_ptr;			/* hash pointer to lookup label */

  if (cur_file_ptr == (efdr_t *) 0)
    {
      error ("#.begin directive without a preceding .file directive");
      return;
    }

  if (cur_proc_ptr == (PDR *) 0)
    {
      error ("#.begin directive without a preceding .ent directive");
      return;
    }

  for (end_p1 = start; (ch = *end_p1) != '\0' && !ISSPACE (ch); end_p1++)
    ;

  hash_ptr = hash_string (start,
			  end_p1 - start,
			  &orig_str_hash[0],
			  (symint_t *) 0);

  if (hash_ptr == (shash_t *) 0)
    {
      error ("label %.*s not found for #.begin",
	     (int) (end_p1 - start), start);
      return;
    }

  if (cur_oproc_begin == (SYMR *) 0)
    {
      error ("procedure table %.*s not found for #.begin",
	     (int) (end_p1 - start), start);
      return;
    }

  (void) add_local_symbol ((const char *) 0, (const char *) 0,
			   st_Block, sc_Text,
			   (symint_t) hash_ptr->sym_ptr->value - cur_oproc_begin->value,
			   (symint_t) 0);
}


/* Parse #.bend directives which have a label as the first argument
   which gives the location of the end of the block.  */

STATIC void
parse_bend (const char *start)
{
  const char *end_p1;			/* end of label */
  int ch;
  shash_t *hash_ptr;			/* hash pointer to lookup label */

  if (cur_file_ptr == (efdr_t *) 0)
    {
      error ("#.begin directive without a preceding .file directive");
      return;
    }

  if (cur_proc_ptr == (PDR *) 0)
    {
      error ("#.bend directive without a preceding .ent directive");
      return;
    }

  for (end_p1 = start; (ch = *end_p1) != '\0' && !ISSPACE (ch); end_p1++)
    ;

  hash_ptr = hash_string (start,
			  end_p1 - start,
			  &orig_str_hash[0],
			  (symint_t *) 0);

  if (hash_ptr == (shash_t *) 0)
    {
      error ("label %.*s not found for #.bend", (int) (end_p1 - start), start);
      return;
    }

  if (cur_oproc_begin == (SYMR *) 0)
    {
      error ("procedure table %.*s not found for #.bend",
	     (int) (end_p1 - start), start);
      return;
    }

  (void) add_local_symbol ((const char *) 0, (const char *) 0,
			   st_End, sc_Text,
			   (symint_t) hash_ptr->sym_ptr->value - cur_oproc_begin->value,
			   (symint_t) 0);
}


/* Parse #.def directives, which are contain standard COFF subdirectives
   to describe the debugging format.  These subdirectives include:

	.scl	specify storage class
	.val	specify a value
	.endef	specify end of COFF directives
	.type	specify the type
	.size	specify the size of an array
	.dim	specify an array dimension
	.tag	specify a tag for a struct, union, or enum.  */

STATIC void
parse_def (const char *name_start)
{
  const char *dir_start;			/* start of current directive*/
  const char *dir_end_p1;			/* end+1 of current directive*/
  const char *arg_start;			/* start of current argument */
  const char *arg_end_p1;			/* end+1 of current argument */
  const char *name_end_p1;			/* end+1 of label */
  const char *tag_start	  = 0;			/* start of tag name */
  const char *tag_end_p1  = 0;			/* end+1 of tag name */
  sc_t storage_class	  = sc_Nil;
  st_t symbol_type	  = st_Nil;
  type_info_t t;
  EXTR *eptr		  = (EXTR *) 0;		/* ext. sym equivalent to def*/
  int is_function	  = 0;			/* != 0 if function */
  symint_t value	  = 0;
  symint_t indx		  = cur_file_ptr->void_type;
  int error_line	  = 0;
  symint_t arg_number;
  symint_t temp_array[ N_TQ ];
  int arg_was_number;
  int ch, i;
  Ptrdiff_t len;

  static int inside_enumeration = 0;		/* is this an enumeration? */


  /* Initialize the type information.  */
  t = type_info_init;


  /* Search for the end of the name being defined.  */
  /* Allow spaces and such in names for G++ templates, which produce stabs
     that look like:

     #.def   SMANIP<long unsigned int>; .scl 10; .type 0x8; .size 8; .endef */

  for (name_end_p1 = name_start; (ch = *name_end_p1) != ';' && ch != '\0'; name_end_p1++)
    ;

  if (ch == '\0')
    {
      error_line = __LINE__;
      saber_stop ();
      goto bomb_out;
    }

  /* Parse the remaining subdirectives now.  */
  dir_start = name_end_p1+1;
  for (;;)
    {
      while ((ch = *dir_start) == ' ' || ch == '\t')
	++dir_start;

      if (ch != '.')
	{
	  error_line = __LINE__;
	  saber_stop ();
	  goto bomb_out;
	}

      /* Are we done? */
      if (dir_start[1] == 'e'
	  && memcmp (dir_start, ".endef", sizeof (".endef")-1) == 0)
	break;

      /* Pick up the subdirective now.  */
      for (dir_end_p1 = dir_start+1;
	   (ch = *dir_end_p1) != ' ' && ch != '\t';
	   dir_end_p1++)
	{
	  if (ch == '\0' || ISSPACE (ch))
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }
	}

      /* Pick up the subdirective argument now.  */
      arg_was_number = arg_number = 0;
      arg_end_p1 = 0;
      arg_start = dir_end_p1+1;
      ch = *arg_start;
      while (ch == ' ' || ch == '\t')
	ch = *++arg_start;

      if (ISDIGIT (ch) || ch == '-' || ch == '+')
	{
	  int ch2;
	  arg_number = strtol (arg_start, (char **) &arg_end_p1, 0);
	  if (arg_end_p1 != arg_start || ((ch2 = *arg_end_p1) != ';') || ch2 != ',')
	    arg_was_number++;
	}

      else if (ch == '\0' || ISSPACE (ch))
	{
	  error_line = __LINE__;
	  saber_stop ();
	  goto bomb_out;
	}

      if (!arg_was_number)
	{
	  /* Allow spaces and such in names for G++ templates.  */
	  for (arg_end_p1 = arg_start+1;
	       (ch = *arg_end_p1) != ';' && ch != '\0';
	       arg_end_p1++)
	    ;

	  if (ch == '\0')
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }
	}

      /* Classify the directives now.  */
      len = dir_end_p1 - dir_start;
      switch (dir_start[1])
	{
	default:
	  error_line = __LINE__;
	  saber_stop ();
	  goto bomb_out;

	case 'd':
	  if (len == sizeof (".dim")-1
	      && memcmp (dir_start, ".dim", sizeof (".dim")-1) == 0
	      && arg_was_number)
	    {
	      symint_t *t_ptr = &temp_array[ N_TQ-1 ];

	      *t_ptr = arg_number;
	      while (*arg_end_p1 == ',' && arg_was_number)
		{
		  arg_start = arg_end_p1+1;
		  ch = *arg_start;
		  while (ch == ' ' || ch == '\t')
		    ch = *++arg_start;

		  arg_was_number = 0;
		  if (ISDIGIT (ch) || ch == '-' || ch == '+')
		    {
		      int ch2;
		      arg_number = strtol (arg_start, (char **) &arg_end_p1, 0);
		      if (arg_end_p1 != arg_start || ((ch2 = *arg_end_p1) != ';') || ch2 != ',')
			arg_was_number++;

		      if (t_ptr == &temp_array[0])
			{
			  error_line = __LINE__;
			  saber_stop ();
			  goto bomb_out;
			}

		      *--t_ptr = arg_number;
		    }
		}

	      /* Reverse order of dimensions.  */
	      while (t_ptr <= &temp_array[ N_TQ-1 ])
		{
		  if (t.num_dims >= N_TQ-1)
		    {
		      error_line = __LINE__;
		      saber_stop ();
		      goto bomb_out;
		    }

		  t.dimensions[ t.num_dims++ ] = *t_ptr++;
		}
	      break;
	    }
	  else
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }


	case 's':
	  if (len == sizeof (".scl")-1
	      && memcmp (dir_start, ".scl", sizeof (".scl")-1) == 0
	      && arg_was_number
	      && arg_number < ((symint_t) C_MAX))
	    {
	      /* If the symbol is a static or external, we have
		 already gotten the appropriate type and class, so
		 make sure we don't override those values.  This is
		 needed because there are some type and classes that
		 are not in COFF, such as short data, etc.  */
	      if (symbol_type == st_Nil)
		{
		  symbol_type   = map_coff_sym_type[arg_number];
		  storage_class = map_coff_storage [arg_number];
		}
	      break;
	    }

	  else if (len == sizeof (".size")-1
		   && memcmp (dir_start, ".size", sizeof (".size")-1) == 0
		   && arg_was_number)
	    {
	      symint_t *t_ptr = &temp_array[ N_TQ-1 ];

	      *t_ptr = arg_number;
	      while (*arg_end_p1 == ',' && arg_was_number)
		{
		  arg_start = arg_end_p1+1;
		  ch = *arg_start;
		  while (ch == ' ' || ch == '\t')
		    ch = *++arg_start;

		  arg_was_number = 0;
		  if (ISDIGIT (ch) || ch == '-' || ch == '+')
		    {
		      int ch2;
		      arg_number = strtol (arg_start, (char **) &arg_end_p1, 0);
		      if (arg_end_p1 != arg_start || ((ch2 = *arg_end_p1) != ';') || ch2 != ',')
			arg_was_number++;

		      if (t_ptr == &temp_array[0])
			{
			  error_line = __LINE__;
			  saber_stop ();
			  goto bomb_out;
			}

		      *--t_ptr = arg_number;
		    }
		}

	      /* Reverse order of sizes.  */
	      while (t_ptr <= &temp_array[ N_TQ-1 ])
		{
		  if (t.num_sizes >= N_TQ-1)
		    {
		      error_line = __LINE__;
		      saber_stop ();
		      goto bomb_out;
		    }

		  t.sizes[ t.num_sizes++ ] = *t_ptr++;
		}
	      break;
	    }

	  else
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }


	case 't':
	  if (len == sizeof (".type")-1
	      && memcmp (dir_start, ".type", sizeof (".type")-1) == 0
	      && arg_was_number)
	    {
	      tq_t *tq_ptr = &t.type_qualifiers[0];

	      t.orig_type = (coff_type_t) (arg_number & N_BTMASK);
	      t.basic_type = map_coff_types [(int) t.orig_type];
	      for (i = N_TQ-1; i >= 0; i--)
		{
		  int dt = (arg_number >> ((i * N_TQ_SHIFT) + N_BT_SHIFT)
			    & N_TMASK);

		  if (dt != (int) DT_NON)
		    *tq_ptr++ = map_coff_derived_type [dt];
		}

	      /* If this is a function, ignore it, so that we don't get
		 two entries (one from the .ent, and one for the .def
		 that precedes it).  Save the type information so that
		 the end block can properly add it after the begin block
		 index.  For MIPS knows what reason, we must strip off
		 the function type at this point.  */
	      if (tq_ptr != &t.type_qualifiers[0] && tq_ptr[-1] == tq_Proc)
		{
		  is_function = 1;
		  tq_ptr[-1] = tq_Nil;
		}

	      break;
	    }

	  else if (len == sizeof (".tag")-1
	      && memcmp (dir_start, ".tag", sizeof (".tag")-1) == 0)
	    {
	      tag_start = arg_start;
	      tag_end_p1 = arg_end_p1;
	      break;
	    }

	  else
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }


	case 'v':
	  if (len == sizeof (".val")-1
	      && memcmp (dir_start, ".val", sizeof (".val")-1) == 0)
	    {
	      if (arg_was_number)
		value = arg_number;

	      /* If the value is not an integer value, it must be the
		 name of a static or global item.  Look up the name in
		 the original symbol table to pick up the storage
		 class, symbol type, etc.  */
	      else
		{
		  shash_t *orig_hash_ptr;	/* hash within orig sym table*/
		  shash_t *ext_hash_ptr;	/* hash within ext. sym table*/

		  ext_hash_ptr = hash_string (arg_start,
					      arg_end_p1 - arg_start,
					      &ext_str_hash[0],
					      (symint_t *) 0);

		  if (ext_hash_ptr != (shash_t *) 0
		      && ext_hash_ptr->esym_ptr != (EXTR *) 0)
		    eptr = ext_hash_ptr->esym_ptr;

		  orig_hash_ptr = hash_string (arg_start,
					       arg_end_p1 - arg_start,
					       &orig_str_hash[0],
					       (symint_t *) 0);

		  if ((orig_hash_ptr == (shash_t *) 0
		       || orig_hash_ptr->sym_ptr == (SYMR *) 0)
		      && eptr == (EXTR *) 0)
		    {
		      fprintf (stderr, "warning, %.*s not found in original or external symbol tables, value defaults to 0\n",
			       (int) (arg_end_p1 - arg_start),
			       arg_start);
		      value = 0;
		    }
		  else
		    {
		      SYMR *ptr = (orig_hash_ptr != (shash_t *) 0
				   && orig_hash_ptr->sym_ptr != (SYMR *) 0)
					? orig_hash_ptr->sym_ptr
					: &eptr->asym;

		      symbol_type = (st_t) ptr->st;
		      storage_class = (sc_t) ptr->sc;
		      value = ptr->value;
		    }
		}
	      break;
	    }
	  else
	    {
	      error_line = __LINE__;
	      saber_stop ();
	      goto bomb_out;
	    }
	}

      /* Set up to find next directive.  */
      dir_start = arg_end_p1 + 1;
    }


  if (storage_class == sc_Bits)
    {
      t.bitfield = 1;
      t.extra_sizes = 1;
    }
  else
    t.extra_sizes = 0;

  if (t.num_dims > 0)
    {
      int num_real_sizes = t.num_sizes - t.extra_sizes;
      int diff = t.num_dims - num_real_sizes;
      int i = t.num_dims - 1;
      int j;

      if (num_real_sizes != 1 || diff < 0)
	{
	  error_line = __LINE__;
	  saber_stop ();
	  goto bomb_out;
	}

      /* If this is an array, make sure the same number of dimensions
	 and sizes were passed, creating extra sizes for multiply
	 dimensioned arrays if not passed.  */

      if (diff)
	{
	  for (j = ARRAY_SIZE (t.sizes) - 1; j >= 0; j--)
	    t.sizes[ j ] = ((j-diff) >= 0) ? t.sizes[ j-diff ] : 0;

	  t.num_sizes = i + 1;
	  for ( i--; i >= 0; i-- )
	    {
	      if (t.dimensions[ i+1 ])
		t.sizes[ i ] = t.sizes[ i+1 ] / t.dimensions[ i+1 ];
	      else
		t.sizes[ i ] = t.sizes[ i+1 ];
	    }
	}
    }

  /* Except for enumeration members & begin/ending of scopes, put the
     type word in the aux. symbol table.  */

  if (symbol_type == st_Block || symbol_type == st_End)
    indx = 0;

  else if (inside_enumeration)
    indx = cur_file_ptr->void_type;

  else
    {
      if (t.basic_type == bt_Struct
	  || t.basic_type == bt_Union
	  || t.basic_type == bt_Enum)
	{
	  if (tag_start == (char *) 0)
	    {
	      error ("no tag specified for %.*s",
		     (int) (name_end_p1 - name_start),
		     name_start);
	      return;
	    }

	  t.tag_ptr = get_tag (tag_start, tag_end_p1,  (symint_t) indexNil,
			       t.basic_type);
	}

      if (is_function)
	{
	  last_func_type_info = t;
	  last_func_eptr = eptr;
	  return;
	}

      indx = add_aux_sym_tir (&t,
			      hash_yes,
			      &cur_file_ptr->thash_head[0]);
    }


  /* If this is an external or static symbol, update the appropriate
     external symbol.  */

  if (eptr != (EXTR *) 0
      && (eptr->asym.index == indexNil || cur_proc_ptr == (PDR *) 0))
    {
      eptr->ifd = cur_file_ptr->file_index;
      eptr->asym.index = indx;
    }


  /* Do any last minute adjustments that are necessary.  */
  switch (symbol_type)
    {
    default:
      break;


      /* For the beginning of structs, unions, and enumerations, the
	 size info needs to be passed in the value field.  */

    case st_Block:
      if (t.num_sizes - t.num_dims - t.extra_sizes != 1)
	{
	  error_line = __LINE__;
	  saber_stop ();
	  goto bomb_out;
	}

      else
	value = t.sizes[0];

      inside_enumeration = (t.orig_type == T_ENUM);
      break;


      /* For the end of structs, unions, and enumerations, omit the
	 name which is always ".eos".  This needs to be done last, so
	 that any error reporting above gives the correct name.  */

    case st_End:
      name_start = name_end_p1 = 0;
      value = inside_enumeration = 0;
      break;


      /* Members of structures and unions that aren't bitfields, need
	 to adjust the value from a byte offset to a bit offset.
	 Members of enumerations do not have the value adjusted, and
	 can be distinguished by indx == indexNil.  For enumerations,
	 update the maximum enumeration value.  */

    case st_Member:
      if (!t.bitfield && !inside_enumeration)
	value *= 8;

      break;
    }


  /* Add the symbol, except for global symbols outside of functions,
     for which the external symbol table is fine enough.  */

  if (eptr == (EXTR *) 0
      || eptr->asym.st == (int) st_Nil
      || cur_proc_ptr != (PDR *) 0)
    {
      symint_t isym = add_local_symbol (name_start, name_end_p1,
					symbol_type, storage_class,
					value,
					indx);

      /* Deal with struct, union, and enum tags.  */
      if (symbol_type == st_Block)
        {
	  /* Create or update the tag information.  */
	  tag_t *tag_ptr = get_tag (name_start,
				    name_end_p1,
				    isym,
				    t.basic_type);

	  /* If there are any forward references, fill in the appropriate
	     file and symbol indexes.  */

	  symint_t file_index  = cur_file_ptr->file_index;
	  forward_t *f_next = tag_ptr->forward_ref;
	  forward_t *f_cur;

	  while (f_next != (forward_t *) 0)
	    {
	      f_cur  = f_next;
	      f_next = f_next->next;

	      f_cur->ifd_ptr->isym = file_index;
	      f_cur->index_ptr->rndx.index = isym;

	      free_forward (f_cur);
	    }

	  tag_ptr->forward_ref = (forward_t *) 0;
        }
    }

  /* Normal return  */
  return;

  /* Error return, issue message.  */
bomb_out:
  if (error_line)
    error ("compiler error, badly formed #.def (internal line # = %d)", error_line);
  else
    error ("compiler error, badly formed #.def");

  return;
}


/* Parse .end directives.  */

STATIC void
parse_end (const char *start)
{
  const char *start_func, *end_func_p1;
  int ch;
  symint_t value;
  FDR *orig_fdr;

  if (cur_file_ptr == (efdr_t *) 0)
    {
      error (".end directive without a preceding .file directive");
      return;
    }

  if (cur_proc_ptr == (PDR *) 0)
    {
      error (".end directive without a preceding .ent directive");
      return;
    }

  /* Get the function name, skipping whitespace.  */
  for (start_func = start; ISSPACE ((unsigned char)*start_func); start_func++)
    ;

  ch = *start_func;
  if (!IS_ASM_IDENT (ch))
    {
      error (".end directive has no name");
      return;
    }

  for (end_func_p1 = start_func; IS_ASM_IDENT (ch); ch = *++end_func_p1)
    ;


  /* Get the value field for creating the end from the original object
     file (which we find by locating the procedure start, and using the
     pointer to the end+1 block and backing up.  The index points to a
     two word aux. symbol, whose first word is the index of the end
     symbol, and the second word is the type of the function return
     value.  */

  orig_fdr = cur_file_ptr->orig_fdr;
  value = 0;
  if (orig_fdr != (FDR *) 0 && cur_oproc_end != (SYMR *) 0)
    value = cur_oproc_end->value;

  else
    error ("cannot find .end block for %.*s",
	   (int) (end_func_p1 - start_func), start_func);

  (void) add_local_symbol (start_func, end_func_p1,
			   st_End, sc_Text,
			   value,
			   (symint_t) 0);

  cur_proc_ptr = cur_oproc_ptr = (PDR *) 0;
}


/* Parse .ent directives.  */

STATIC void
parse_ent (const char *start)
{
  const char *start_func, *end_func_p1;
  int ch;

  if (cur_file_ptr == (efdr_t *) 0)
    {
      error (".ent directive without a preceding .file directive");
      return;
    }

  if (cur_proc_ptr != (PDR *) 0)
    {
      error ("second .ent directive found before .end directive");
      return;
    }

  for (start_func = start; ISSPACE ((unsigned char)*start_func); start_func++)
    ;

  ch = *start_func;
  if (!IS_ASM_IDENT (ch))
    {
      error (".ent directive has no name");
      return;
    }

  for (end_func_p1 = start_func; IS_ASM_IDENT (ch); ch = *++end_func_p1)
    ;

  (void) add_procedure (start_func, end_func_p1);
}


/* Parse .file directives.  */

STATIC void
parse_file (const char *start)
{
  char *p;
  char *start_name, *end_name_p1;

  (void) strtol (start, &p, 0);
  if (start == p
      || (start_name = strchr (p, '"')) == (char *) 0
      || (end_name_p1 = strrchr (++start_name, '"')) == (char *) 0)
    {
      error ("invalid .file directive");
      return;
    }

  if (cur_proc_ptr != (PDR *) 0)
    {
      error ("no way to handle .file within .ent/.end section");
      return;
    }

  add_file (start_name, end_name_p1);
}


/* Make sure the @stabs symbol is emitted.  */

static void
mark_stabs (const char *start ATTRIBUTE_UNUSED)
{
  if (!stabs_seen)
    {
      /* Add a dummy @stabs symbol.  */
      stabs_seen = 1;
      (void) add_local_symbol (stabs_symbol,
			       stabs_symbol + sizeof (stabs_symbol),
			       stNil, scInfo, -1, MIPS_MARK_STAB (0));

    }
}


/* Parse .stabs directives.

   .stabs directives have five fields:
	"string"	a string, encoding the type information.
	code		a numeric code, defined in <stab.h>
	0		a zero
	0		a zero or line number
	value		a numeric value or an address.

    If the value is relocatable, we transform this into:
	iss		points as an index into string space
	value		value from lookup of the name
	st		st from lookup of the name
	sc		sc from lookup of the name
	index		code|CODE_MASK

    If the value is not relocatable, we transform this into:
	iss		points as an index into string space
	value		value
	st		st_Nil
	sc		sc_Nil
	index		code|CODE_MASK

    .stabn directives have four fields (string is null):
	code		a numeric code, defined in <stab.h>
	0		a zero
	0		a zero or a line number
	value		a numeric value or an address.  */

STATIC void
parse_stabs_common (const char *string_start,	/* start of string or NULL */
		    const char *string_end,	/* end+1 of string or NULL */
		    const char *rest)		/* rest of the directive.  */
{
  efdr_t *save_file_ptr = cur_file_ptr;
  symint_t code;
  symint_t value;
  char *p;
  st_t st;
  sc_t sc;
  int ch;

  if (stabs_seen == 0)
    mark_stabs ("");

  /* Read code from stabs.  */
  if (!ISDIGIT (*rest))
    {
      error ("invalid .stabs/.stabn directive, code is non-numeric");
      return;
    }

  code = strtol (rest, &p, 0);

  /* Line number stabs are handled differently, since they have two values,
     the line number and the address of the label.  We use the index field
     (aka code) to hold the line number, and the value field to hold the
     address.  The symbol type is st_Label, which should be different from
     the other stabs, so that gdb can recognize it.  */

  if (code == (int) N_SLINE)
    {
      SYMR *sym_ptr, dummy_symr;
      shash_t *shash_ptr;

      /* Skip ,0, */
      if (p[0] != ',' || p[1] != '0' || p[2] != ',' || !ISDIGIT (p[3]))
	{
	  error ("invalid line number .stabs/.stabn directive");
	  return;
	}

      code = strtol (p+3, &p, 0);
      ch = *++p;
      if (p[-1] != ',' || ISDIGIT (ch) || !IS_ASM_IDENT (ch))
	{
	  error ("invalid line number .stabs/.stabn directive");
	  return;
	}

      dummy_symr.index = code;
      if (dummy_symr.index != code)
	{
	  error ("line number (%lu) for .stabs/.stabn directive cannot fit in index field (20 bits)",
		 code);

	  return;
	}

      shash_ptr = hash_string (p,
			       strlen (p) - 1,
			       &orig_str_hash[0],
			       (symint_t *) 0);

      if (shash_ptr == (shash_t *) 0
	  || (sym_ptr = shash_ptr->sym_ptr) == (SYMR *) 0)
	{
	  error ("invalid .stabs/.stabn directive, value not found");
	  return;
	}

      if ((st_t) sym_ptr->st != st_Label)
	{
	  error ("invalid line number .stabs/.stabn directive");
	  return;
	}

      st = st_Label;
      sc = (sc_t) sym_ptr->sc;
      value = sym_ptr->value;
    }
  else
    {
      /* Skip ,<num>,<num>, */
      if (*p++ != ',')
	goto failure;
      for (; ISDIGIT (*p); p++)
	;
      if (*p++ != ',')
	goto failure;
      for (; ISDIGIT (*p); p++)
	;
      if (*p++ != ',')
	goto failure;
      ch = *p;
      if (!IS_ASM_IDENT (ch) && ch != '-')
	{
	failure:
	  error ("invalid .stabs/.stabn directive, bad character");
	  return;
	}

      if (ISDIGIT (ch) || ch == '-')
	{
	  st = st_Nil;
	  sc = sc_Nil;
	  value = strtol (p, &p, 0);
	  if (*p != '\n')
	    {
	      error ("invalid .stabs/.stabn directive, stuff after numeric value");
	      return;
	    }
	}
      else if (!IS_ASM_IDENT (ch))
	{
	  error ("invalid .stabs/.stabn directive, bad character");
	  return;
	}
      else
	{
	  SYMR *sym_ptr;
	  shash_t *shash_ptr;
	  const char *start, *end_p1;

	  start = p;
	  if ((end_p1 = strchr (start, '+')) == (char *) 0)
	    {
	      if ((end_p1 = strchr (start, '-')) == (char *) 0)
		end_p1 = start + strlen (start) - 1;
	    }

	  shash_ptr = hash_string (start,
				   end_p1 - start,
				   &orig_str_hash[0],
				   (symint_t *) 0);

	  if (shash_ptr == (shash_t *) 0
	      || (sym_ptr = shash_ptr->sym_ptr) == (SYMR *) 0)
	    {
	      shash_ptr = hash_string (start,
				       end_p1 - start,
				       &ext_str_hash[0],
				       (symint_t *) 0);

	      if (shash_ptr == (shash_t *) 0
		  || shash_ptr->esym_ptr == (EXTR *) 0)
		{
		  error ("invalid .stabs/.stabn directive, value not found");
		  return;
		}
	      else
		sym_ptr = &(shash_ptr->esym_ptr->asym);
	    }

	  /* Traditionally, N_LBRAC and N_RBRAC are *not* relocated.  */
	  if (code == (int) N_LBRAC || code == (int) N_RBRAC)
	    {
	      sc = scNil;
	      st = stNil;
	    }
	  else
	    {
	      sc = (sc_t) sym_ptr->sc;
	      st = (st_t) sym_ptr->st;
	    }
	  value = sym_ptr->value;

	  ch = *end_p1++;
	  if (ch != '\n')
	    {
	      if (((!ISDIGIT (*end_p1)) && (*end_p1 != '-'))
		  || ((ch != '+') && (ch != '-')))
		{
		  error ("invalid .stabs/.stabn directive, badly formed value");
		  return;
		}
	      if (ch == '+')
		value += strtol (end_p1, &p, 0);
	      else if (ch == '-')
		value -= strtol (end_p1, &p, 0);

	      if (*p != '\n')
		{
		  error ("invalid .stabs/.stabn directive, stuff after numeric value");
		  return;
		}
	    }
	}
      code = MIPS_MARK_STAB (code);
    }

  (void) add_local_symbol (string_start, string_end, st, sc, value, code);
  /* Restore normal file type.  */
  cur_file_ptr = save_file_ptr;
}


STATIC void
parse_stabs (const char *start)
{
  const char *end = strchr (start+1, '"');

  if (*start != '"' || end == (const char *) 0 || end[1] != ',')
    {
      error ("invalid .stabs directive, no string");
      return;
    }

  parse_stabs_common (start+1, end, end+2);
}


STATIC void
parse_stabn (const char *start)
{
  parse_stabs_common ((const char *) 0, (const char *) 0, start);
}


/* Parse the input file, and write the lines to the output file
   if needed.  */

STATIC void
parse_input (void)
{
  char *p;
  Size_t i;
  thead_t *ptag_head;
  tag_t *ptag;
  tag_t *ptag_next;

  if (debug)
    fprintf (stderr, "\tinput\n");

  /* Add a dummy scope block around the entire compilation unit for
     structures defined outside of blocks.  */
  ptag_head = allocate_thead ();
  ptag_head->first_tag = 0;
  ptag_head->prev = cur_tag_head;
  cur_tag_head = ptag_head;

  while ((p = read_line ()) != (char *) 0)
    {
      /* Skip leading blanks.  */
      while (ISSPACE ((unsigned char)*p))
	p++;

      /* See if it's a directive we handle.  If so, dispatch handler.  */
      for (i = 0; i < ARRAY_SIZE (pseudo_ops); i++)
	if (memcmp (p, pseudo_ops[i].name, pseudo_ops[i].len) == 0
	    && ISSPACE ((unsigned char)(p[pseudo_ops[i].len])))
	  {
	    p += pseudo_ops[i].len;	/* skip to first argument */
	    while (ISSPACE ((unsigned char)*p))
	      p++;

	    (*pseudo_ops[i].func)( p );
	    break;
	  }
    }

  /* Process any tags at global level.  */
  ptag_head = cur_tag_head;
  cur_tag_head = ptag_head->prev;

  for (ptag = ptag_head->first_tag;
       ptag != (tag_t *) 0;
       ptag = ptag_next)
    {
      if (ptag->forward_ref != (forward_t *) 0)
	add_unknown_tag (ptag);

      ptag_next = ptag->same_block;
      ptag->hash_ptr->tag_ptr = ptag->same_name;
      free_tag (ptag);
    }

  free_thead (ptag_head);

}


/* Update the global headers with the final offsets in preparation
   to write out the .T file.  */

STATIC void
update_headers (void)
{
  symint_t i;
  efdr_t *file_ptr;

  /* Set up the symbolic header.  */
  file_offset = sizeof (symbolic_header) + orig_file_header.f_symptr;
  symbolic_header.magic = orig_sym_hdr.magic;
  symbolic_header.vstamp = orig_sym_hdr.vstamp;

  /* Set up global counts.  */
  symbolic_header.issExtMax = ext_strings.num_allocated;
  symbolic_header.idnMax    = dense_num.num_allocated;
  symbolic_header.ifdMax    = file_desc.num_allocated;
  symbolic_header.iextMax   = ext_symbols.num_allocated;
  symbolic_header.ilineMax  = orig_sym_hdr.ilineMax;
  symbolic_header.ioptMax   = orig_sym_hdr.ioptMax;
  symbolic_header.cbLine    = orig_sym_hdr.cbLine;
  symbolic_header.crfd      = orig_sym_hdr.crfd;


  /* Loop through each file, figuring out how many local syms,
     line numbers, etc. there are.  Also, put out end symbol
     for the filename.  */

  for (file_ptr = first_file;
       file_ptr != (efdr_t *) 0;
       file_ptr = file_ptr->next_file)
    {
      SYMR *sym_start;
      SYMR *sym;
      SYMR *sym_end_p1;
      FDR *fd_ptr = file_ptr->orig_fdr;

      cur_file_ptr = file_ptr;

      /* Copy st_Static symbols from the original local symbol table if
	 they did not get added to the new local symbol table.
	 This happens with stabs-in-ecoff or if the source file is
	 compiled without debugging.  */
      sym_start = ORIG_LSYMS (fd_ptr->isymBase);
      sym_end_p1 = sym_start + fd_ptr->csym;
      for (sym = sym_start; sym < sym_end_p1; sym++)
	{
	  if ((st_t) sym->st == st_Static)
	    {
	      char *str = ORIG_LSTRS (fd_ptr->issBase + sym->iss);
	      Size_t len = strlen (str);
	      shash_t *hash_ptr;

	      /* Ignore internal labels.  */
	      if (str[0] == '$' && str[1] == 'L')
		continue;
	      hash_ptr = hash_string (str,
				      (Ptrdiff_t) len,
				      &file_ptr->shash_head[0],
				      (symint_t *) 0);
	      if (hash_ptr == (shash_t *) 0)
		{
		  (void) add_local_symbol (str, str + len,
					   (st_t) sym->st, (sc_t) sym->sc,
					   (symint_t) sym->value,
					   (symint_t) indexNil);
		}
	    }
	}
      (void) add_local_symbol ((const char *) 0, (const char *) 0,
			       st_End, sc_Text,
			       (symint_t) 0,
			       (symint_t) 0);

      file_ptr->fdr.cpd = file_ptr->procs.num_allocated;
      file_ptr->fdr.ipdFirst = symbolic_header.ipdMax;
      symbolic_header.ipdMax += file_ptr->fdr.cpd;

      file_ptr->fdr.csym = file_ptr->symbols.num_allocated;
      file_ptr->fdr.isymBase = symbolic_header.isymMax;
      symbolic_header.isymMax += file_ptr->fdr.csym;

      file_ptr->fdr.caux = file_ptr->aux_syms.num_allocated;
      file_ptr->fdr.iauxBase = symbolic_header.iauxMax;
      symbolic_header.iauxMax += file_ptr->fdr.caux;

      file_ptr->fdr.cbSs = file_ptr->strings.num_allocated;
      file_ptr->fdr.issBase = symbolic_header.issMax;
      symbolic_header.issMax += file_ptr->fdr.cbSs;
    }

#ifndef ALIGN_SYMTABLE_OFFSET
#define ALIGN_SYMTABLE_OFFSET(OFFSET) (OFFSET)
#endif

  file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
  i = WORD_ALIGN (symbolic_header.cbLine);	/* line numbers */
  if (i > 0)
    {
      symbolic_header.cbLineOffset = file_offset;
      file_offset += i;
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.ioptMax;			/* optimization symbols */
  if (((long) i) > 0)
    {
      symbolic_header.cbOptOffset = file_offset;
      file_offset += i * sizeof (OPTR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.idnMax;			/* dense numbers */
  if (i > 0)
    {
      symbolic_header.cbDnOffset = file_offset;
      file_offset += i * sizeof (DNR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.ipdMax;			/* procedure tables */
  if (i > 0)
    {
      symbolic_header.cbPdOffset = file_offset;
      file_offset += i * sizeof (PDR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.isymMax;			/* local symbols */
  if (i > 0)
    {
      symbolic_header.cbSymOffset = file_offset;
      file_offset += i * sizeof (SYMR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.iauxMax;			/* aux syms.  */
  if (i > 0)
    {
      symbolic_header.cbAuxOffset = file_offset;
      file_offset += i * sizeof (TIR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = WORD_ALIGN (symbolic_header.issMax);	/* local strings */
  if (i > 0)
    {
      symbolic_header.cbSsOffset = file_offset;
      file_offset += i;
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = WORD_ALIGN (symbolic_header.issExtMax);	/* external strings */
  if (i > 0)
    {
      symbolic_header.cbSsExtOffset = file_offset;
      file_offset += i;
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.ifdMax;			/* file tables */
  if (i > 0)
    {
      symbolic_header.cbFdOffset = file_offset;
      file_offset += i * sizeof (FDR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.crfd;			/* relative file descriptors */
  if (i > 0)
    {
      symbolic_header.cbRfdOffset = file_offset;
      file_offset += i * sizeof (symint_t);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }

  i = symbolic_header.iextMax;			/* external symbols */
  if (i > 0)
    {
      symbolic_header.cbExtOffset = file_offset;
      file_offset += i * sizeof (EXTR);
      file_offset = ALIGN_SYMTABLE_OFFSET (file_offset);
    }
}


/* Write out a varray at a given location.  */

STATIC void
write_varray (varray_t *vp,    /* virtual array */
	      off_t offset,    /* offset to write varray to */
	      const char *str) /* string to print out when tracing */
{
  int num_write, sys_write;
  vlinks_t *ptr;

  if (vp->num_allocated == 0)
    return;

  if (debug)
    fprintf (stderr, "\twarray\tvp = %p, offset = %7lu, size = %7lu, %s\n",
	     (void *) vp, (unsigned long) offset,
	     vp->num_allocated * vp->object_size, str);

  if (file_offset != (unsigned long) offset
      && fseek (object_stream, (long) offset, SEEK_SET) < 0)
    pfatal_with_name (object_name);

  for (ptr = vp->first; ptr != (vlinks_t *) 0; ptr = ptr->next)
    {
      num_write = (ptr->next == (vlinks_t *) 0)
	? vp->objects_last_page * vp->object_size
	: vp->objects_per_page  * vp->object_size;

      sys_write = fwrite (ptr->datum, 1, num_write, object_stream);
      if (sys_write <= 0)
	pfatal_with_name (object_name);

      else if (sys_write != num_write)
	fatal ("wrote %d bytes to %s, system returned %d",
	       num_write,
	       object_name,
	       sys_write);

      file_offset += num_write;
    }
}


/* Write out the symbol table in the object file.  */

STATIC void
write_object (void)
{
  int sys_write;
  efdr_t *file_ptr;
  off_t offset;

  if (debug)
    fprintf (stderr, "\n\twrite\tvp = %p, offset = %7u, size = %7lu, %s\n",
	     (void *) &symbolic_header, 0,
	     (unsigned long) sizeof (symbolic_header), "symbolic header");

  sys_write = fwrite (&symbolic_header,
		      1,
		      sizeof (symbolic_header),
		      object_stream);

  if (sys_write < 0)
    pfatal_with_name (object_name);

  else if (sys_write != sizeof (symbolic_header))
    fatal ("wrote %d bytes to %s, system returned %d",
	   (int) sizeof (symbolic_header),
	   object_name,
	   sys_write);


  file_offset = sizeof (symbolic_header) + orig_file_header.f_symptr;

  if (symbolic_header.cbLine > 0)		/* line numbers */
    {
      long sys_write;

      if (file_offset != (unsigned long) symbolic_header.cbLineOffset
	  && fseek (object_stream, symbolic_header.cbLineOffset, SEEK_SET) != 0)
	pfatal_with_name (object_name);

      if (debug)
	fprintf (stderr, "\twrite\tvp = %p, offset = %7lu, size = %7lu, %s\n",
		 (void *) &orig_linenum, (long) symbolic_header.cbLineOffset,
		 (long) symbolic_header.cbLine, "Line numbers");

      sys_write = fwrite (orig_linenum,
			  1,
			  symbolic_header.cbLine,
			  object_stream);

      if (sys_write <= 0)
	pfatal_with_name (object_name);

      else if (sys_write != symbolic_header.cbLine)
	fatal ("wrote %ld bytes to %s, system returned %ld",
	       (long) symbolic_header.cbLine,
	       object_name,
	       sys_write);

      file_offset = symbolic_header.cbLineOffset + symbolic_header.cbLine;
    }

  if (symbolic_header.ioptMax > 0)		/* optimization symbols */
    {
      long sys_write;
      long num_write = symbolic_header.ioptMax * sizeof (OPTR);

      if (file_offset != (unsigned long) symbolic_header.cbOptOffset
	  && fseek (object_stream, symbolic_header.cbOptOffset, SEEK_SET) != 0)
	pfatal_with_name (object_name);

      if (debug)
	fprintf (stderr, "\twrite\tvp = %p, offset = %7lu, size = %7lu, %s\n",
		 (void *) &orig_opt_syms, (long) symbolic_header.cbOptOffset,
		 num_write, "Optimizer symbols");

      sys_write = fwrite (orig_opt_syms,
			  1,
			  num_write,
			  object_stream);

      if (sys_write <= 0)
	pfatal_with_name (object_name);

      else if (sys_write != num_write)
	fatal ("wrote %ld bytes to %s, system returned %ld",
	       num_write,
	       object_name,
	       sys_write);

      file_offset = symbolic_header.cbOptOffset + num_write;
    }

  if (symbolic_header.idnMax > 0)		/* dense numbers */
    write_varray (&dense_num, (off_t) symbolic_header.cbDnOffset, "Dense numbers");

  if (symbolic_header.ipdMax > 0)		/* procedure tables */
    {
      offset = symbolic_header.cbPdOffset;
      for (file_ptr = first_file;
	   file_ptr != (efdr_t *) 0;
	   file_ptr = file_ptr->next_file)
	{
	  write_varray (&file_ptr->procs, offset, "Procedure tables");
	  offset = file_offset;
	}
    }

  if (symbolic_header.isymMax > 0)		/* local symbols */
    {
      offset = symbolic_header.cbSymOffset;
      for (file_ptr = first_file;
	   file_ptr != (efdr_t *) 0;
	   file_ptr = file_ptr->next_file)
	{
	  write_varray (&file_ptr->symbols, offset, "Local symbols");
	  offset = file_offset;
	}
    }

  if (symbolic_header.iauxMax > 0)		/* aux symbols */
    {
      offset = symbolic_header.cbAuxOffset;
      for (file_ptr = first_file;
	   file_ptr != (efdr_t *) 0;
	   file_ptr = file_ptr->next_file)
	{
	  write_varray (&file_ptr->aux_syms, offset, "Aux. symbols");
	  offset = file_offset;
	}
    }

  if (symbolic_header.issMax > 0)		/* local strings */
    {
      offset = symbolic_header.cbSsOffset;
      for (file_ptr = first_file;
	   file_ptr != (efdr_t *) 0;
	   file_ptr = file_ptr->next_file)
	{
	  write_varray (&file_ptr->strings, offset, "Local strings");
	  offset = file_offset;
	}
    }

  if (symbolic_header.issExtMax > 0)		/* external strings */
    write_varray (&ext_strings, symbolic_header.cbSsExtOffset, "External strings");

  if (symbolic_header.ifdMax > 0)		/* file tables */
    {
      offset = symbolic_header.cbFdOffset;
      if (file_offset != (unsigned long) offset
	  && fseek (object_stream, (long) offset, SEEK_SET) < 0)
	pfatal_with_name (object_name);

      file_offset = offset;
      for (file_ptr = first_file;
	   file_ptr != (efdr_t *) 0;
	   file_ptr = file_ptr->next_file)
	{
	  if (debug)
	    fprintf (stderr, "\twrite\tvp = %p, offset = %7lu, size = %7lu, %s\n",
		     (void *) &file_ptr->fdr, file_offset,
		     (unsigned long) sizeof (FDR), "File header");

	  sys_write = fwrite (&file_ptr->fdr,
			      1,
			      sizeof (FDR),
			      object_stream);

	  if (sys_write < 0)
	    pfatal_with_name (object_name);

	  else if (sys_write != sizeof (FDR))
	    fatal ("wrote %d bytes to %s, system returned %d",
		   (int) sizeof (FDR),
		   object_name,
		   sys_write);

	  file_offset = offset += sizeof (FDR);
	}
    }

  if (symbolic_header.crfd > 0)			/* relative file descriptors */
    {
      long sys_write;
      symint_t num_write = symbolic_header.crfd * sizeof (symint_t);

      if (file_offset != (unsigned long) symbolic_header.cbRfdOffset
	  && fseek (object_stream, symbolic_header.cbRfdOffset, SEEK_SET) != 0)
	pfatal_with_name (object_name);

      if (debug)
	fprintf (stderr, "\twrite\tvp = %p, offset = %7lu, size = %7lu, %s\n",
		 (void *) &orig_rfds, (long) symbolic_header.cbRfdOffset,
		 num_write, "Relative file descriptors");

      sys_write = fwrite (orig_rfds,
			  1,
			  num_write,
			  object_stream);

      if (sys_write <= 0)
	pfatal_with_name (object_name);

      else if (sys_write != (long) num_write)
	fatal ("wrote %lu bytes to %s, system returned %ld",
	       num_write,
	       object_name,
	       sys_write);

      file_offset = symbolic_header.cbRfdOffset + num_write;
    }

  if (symbolic_header.issExtMax > 0)		/* external symbols */
    write_varray (&ext_symbols, (off_t) symbolic_header.cbExtOffset, "External symbols");

  if (fclose (object_stream) != 0)
    pfatal_with_name (object_name);
}


/* Read some bytes at a specified location, and return a pointer.  */

STATIC page_t *
read_seek (Size_t size,		/* # bytes to read */
	   off_t offset,	/* offset to read at */
	   const char *str)	/* name for tracing */
{
  page_t *ptr;
  long sys_read = 0;

  if (size == 0)		/* nothing to read */
    return (page_t *) 0;

  if (debug)
    fprintf (stderr,
	     "\trseek\tsize = %7lu, offset = %7lu, currently at %7lu, %s\n",
	     (unsigned long) size, (unsigned long) offset, file_offset, str);

#ifndef MALLOC_CHECK
  ptr = allocate_multiple_pages ((size + PAGE_USIZE - 1) / PAGE_USIZE);
#else
  ptr = xcalloc (1, size);
#endif

  /* If we need to seek, and the distance is nearby, just do some reads,
     to speed things up.  */
  if (file_offset != (unsigned long) offset)
    {
      symint_t difference = offset - file_offset;

      if (difference < 8)
	{
	  char small_buffer[8];

	  sys_read = fread (small_buffer, 1, difference, obj_in_stream);
	  if (sys_read <= 0)
	    pfatal_with_name (obj_in_name);

	  if ((symint_t) sys_read != difference)
	    fatal ("wanted to read %lu bytes from %s, system returned %ld",
		   (unsigned long) size,
		   obj_in_name,
		   sys_read);
	}
      else if (fseek (obj_in_stream, offset, SEEK_SET) < 0)
	pfatal_with_name (obj_in_name);
    }

  sys_read = fread (ptr, 1, size, obj_in_stream);
  if (sys_read <= 0)
    pfatal_with_name (obj_in_name);

  if (sys_read != (long) size)
    fatal ("wanted to read %lu bytes from %s, system returned %ld",
	   (unsigned long) size,
	   obj_in_name,
	   sys_read);

  file_offset = offset + size;

  if (file_offset > max_file_offset)
    max_file_offset = file_offset;

  return ptr;
}


/* Read the existing object file (and copy to the output object file
   if it is different from the input object file), and remove the old
   symbol table.  */

STATIC void
copy_object (void)
{
  char buffer[ PAGE_SIZE ];
  int sys_read;
  int remaining;
  int num_write;
  int sys_write;
  int fd, es;
  int delete_ifd = 0;
  int *remap_file_number;
  struct stat stat_buf;

  if (debug)
    fprintf (stderr, "\tcopy\n");

  if (fstat (fileno (obj_in_stream), &stat_buf) != 0
      || fseek (obj_in_stream, 0L, SEEK_SET) != 0)
    pfatal_with_name (obj_in_name);

  sys_read = fread (&orig_file_header,
		    1,
		    sizeof (struct filehdr),
		    obj_in_stream);

  if (sys_read < 0)
    pfatal_with_name (obj_in_name);

  else if (sys_read == 0 && feof (obj_in_stream))
    return;			/* create a .T file sans file header */

  else if (sys_read < (int) sizeof (struct filehdr))
    fatal ("wanted to read %d bytes from %s, system returned %d",
	   (int) sizeof (struct filehdr),
	   obj_in_name,
	   sys_read);


  if (orig_file_header.f_nsyms != sizeof (HDRR))
    fatal ("%s symbolic header wrong size (%ld bytes, should be %ld)",
	   input_name, (long) orig_file_header.f_nsyms, (long) sizeof (HDRR));


  /* Read in the current symbolic header.  */
  if (fseek (obj_in_stream, (long) orig_file_header.f_symptr, SEEK_SET) != 0)
    pfatal_with_name (input_name);

  sys_read = fread (&orig_sym_hdr,
		    1,
		    sizeof (orig_sym_hdr),
		    obj_in_stream);

  if (sys_read < 0)
    pfatal_with_name (object_name);

  else if (sys_read < (int) sizeof (struct filehdr))
    fatal ("wanted to read %d bytes from %s, system returned %d",
	   (int) sizeof (struct filehdr),
	   obj_in_name,
	   sys_read);


  /* Read in each of the sections if they exist in the object file.
     We read things in the order the mips assembler creates the
     sections, so in theory no extra seeks are done.

     For simplicity sake, round each read up to a page boundary,
     we may want to revisit this later....  */

  file_offset =  orig_file_header.f_symptr + sizeof (struct filehdr);

  if (orig_sym_hdr.cbLine > 0)			/* line numbers */
    orig_linenum = (char *) read_seek (orig_sym_hdr.cbLine,
				       orig_sym_hdr.cbLineOffset,
				       "Line numbers");

  if (orig_sym_hdr.ipdMax > 0)			/* procedure tables */
    orig_procs = (PDR *) read_seek (orig_sym_hdr.ipdMax * sizeof (PDR),
				    orig_sym_hdr.cbPdOffset,
				    "Procedure tables");

  if (orig_sym_hdr.isymMax > 0)			/* local symbols */
    orig_local_syms = (SYMR *) read_seek (orig_sym_hdr.isymMax * sizeof (SYMR),
					  orig_sym_hdr.cbSymOffset,
					  "Local symbols");

  if (orig_sym_hdr.iauxMax > 0)			/* aux symbols */
    orig_aux_syms = (AUXU *) read_seek (orig_sym_hdr.iauxMax * sizeof (AUXU),
					orig_sym_hdr.cbAuxOffset,
					"Aux. symbols");

  if (orig_sym_hdr.issMax > 0)			/* local strings */
    orig_local_strs = (char *) read_seek (orig_sym_hdr.issMax,
					  orig_sym_hdr.cbSsOffset,
					  "Local strings");

  if (orig_sym_hdr.issExtMax > 0)		/* external strings */
    orig_ext_strs = (char *) read_seek (orig_sym_hdr.issExtMax,
					orig_sym_hdr.cbSsExtOffset,
					"External strings");

  if (orig_sym_hdr.ifdMax > 0)			/* file tables */
    orig_files = (FDR *) read_seek (orig_sym_hdr.ifdMax * sizeof (FDR),
				    orig_sym_hdr.cbFdOffset,
				    "File tables");

  if (orig_sym_hdr.crfd > 0)			/* relative file descriptors */
    orig_rfds = (symint_t *) read_seek (orig_sym_hdr.crfd * sizeof (symint_t),
					orig_sym_hdr.cbRfdOffset,
					"Relative file descriptors");

  if (orig_sym_hdr.issExtMax > 0)		/* external symbols */
    orig_ext_syms = (EXTR *) read_seek (orig_sym_hdr.iextMax * sizeof (EXTR),
					orig_sym_hdr.cbExtOffset,
					"External symbols");

  if (orig_sym_hdr.idnMax > 0)			/* dense numbers */
    {
      orig_dense = (DNR *) read_seek (orig_sym_hdr.idnMax * sizeof (DNR),
				      orig_sym_hdr.cbDnOffset,
				      "Dense numbers");

      add_bytes (&dense_num, (char *) orig_dense, orig_sym_hdr.idnMax);
    }

  if (orig_sym_hdr.ioptMax > 0)			/* opt symbols */
    orig_opt_syms = (OPTR *) read_seek (orig_sym_hdr.ioptMax * sizeof (OPTR),
					orig_sym_hdr.cbOptOffset,
					"Optimizer symbols");



  /* The symbol table should be last.  */
  if (max_file_offset != (unsigned long) stat_buf.st_size)
    fatal ("symbol table is not last (symbol table ends at %ld, .o ends at %ld",
	   max_file_offset,
	   (long) stat_buf.st_size);


  /* If the first original file descriptor is a dummy which the assembler
     put out, but there are no symbols in it, skip it now.  */
  if (orig_sym_hdr.ifdMax > 1
      && orig_files->csym == 2
      && orig_files->caux == 0)
    {
      char *filename = orig_local_strs + (orig_files->issBase + orig_files->rss);
      char *suffix = strrchr (filename, '.');

      if (suffix != (char *) 0 && strcmp (suffix, ".s") == 0)
	delete_ifd = 1;
    }


  /* Create array to map original file numbers to the new file numbers
     (in case there are duplicate filenames, we collapse them into one
     file section, the MIPS assembler may or may not collapse them).  */

  remap_file_number = alloca (sizeof (int) * orig_sym_hdr.ifdMax);

  for (fd = delete_ifd; fd < orig_sym_hdr.ifdMax; fd++)
    {
      FDR *fd_ptr = ORIG_FILES (fd);
      char *filename = ORIG_LSTRS (fd_ptr->issBase + fd_ptr->rss);

      /* file support itself.  */
      add_file (filename, filename + strlen (filename));
      remap_file_number[fd] = cur_file_ptr->file_index;
    }

  if (delete_ifd > 0)		/* just in case */
    remap_file_number[0] = remap_file_number[1];


  /* Loop, adding each of the external symbols.  These must be in
     order or otherwise we would have to change the relocation
     entries.  We don't just call add_bytes, because we need to have
     the names put into the external hash table.  We set the type to
     'void' for now, and parse_def will fill in the correct type if it
     is in the symbol table.  We must add the external symbols before
     the locals, since the locals do lookups against the externals.  */

  if (debug)
    fprintf (stderr, "\tehash\n");

  for (es = 0; es < orig_sym_hdr.iextMax; es++)
    {
      EXTR *eptr = orig_ext_syms + es;
      int ifd = eptr->ifd;

      (void) add_ext_symbol (eptr, ((long) ifd < orig_sym_hdr.ifdMax)
			     ? remap_file_number[ ifd ] : ifd );
    }


  /* For each of the files in the object file, copy the symbols, and such
     into the varrays for the new object file.  */

  for (fd = delete_ifd; fd < orig_sym_hdr.ifdMax; fd++)
    {
      FDR *fd_ptr = ORIG_FILES (fd);
      char *filename = ORIG_LSTRS (fd_ptr->issBase + fd_ptr->rss);
      SYMR *sym_start;
      SYMR *sym;
      SYMR *sym_end_p1;
      PDR *proc_start;
      PDR *proc;
      PDR *proc_end_p1;

      /* file support itself.  */
      add_file (filename, filename + strlen (filename));
      cur_file_ptr->orig_fdr = fd_ptr;

      /* Copy stuff that's just passed through (such as line #'s) */
      cur_file_ptr->fdr.adr	     = fd_ptr->adr;
      cur_file_ptr->fdr.ilineBase    = fd_ptr->ilineBase;
      cur_file_ptr->fdr.cline	     = fd_ptr->cline;
      cur_file_ptr->fdr.rfdBase	     = fd_ptr->rfdBase;
      cur_file_ptr->fdr.crfd	     = fd_ptr->crfd;
      cur_file_ptr->fdr.cbLineOffset = fd_ptr->cbLineOffset;
      cur_file_ptr->fdr.cbLine	     = fd_ptr->cbLine;
      cur_file_ptr->fdr.fMerge	     = fd_ptr->fMerge;
      cur_file_ptr->fdr.fReadin	     = fd_ptr->fReadin;
      cur_file_ptr->fdr.glevel	     = fd_ptr->glevel;

      if (debug)
	fprintf (stderr, "\thash\tstart, filename %s\n", filename);

      /* For each of the static and global symbols defined, add them
	 to the hash table of original symbols, so we can look up
	 their values.  */

      sym_start = ORIG_LSYMS (fd_ptr->isymBase);
      sym_end_p1 = sym_start + fd_ptr->csym;
      for (sym = sym_start; sym < sym_end_p1; sym++)
	{
	  switch ((st_t) sym->st)
	    {
	    default:
	      break;

	    case st_Global:
	    case st_Static:
	    case st_Label:
	    case st_Proc:
	    case st_StaticProc:
	      {
		auto symint_t hash_index;
		char *str = ORIG_LSTRS (fd_ptr->issBase + sym->iss);
		Size_t len = strlen (str);
		shash_t *shash_ptr = hash_string (str,
						  (Ptrdiff_t) len,
						  &orig_str_hash[0],
						  &hash_index);

		if (shash_ptr != (shash_t *) 0)
		  error ("internal error, %s is already in original symbol table", str);

		else
		  {
		    shash_ptr = allocate_shash ();
		    shash_ptr->next = orig_str_hash[hash_index];
		    orig_str_hash[hash_index] = shash_ptr;

		    shash_ptr->len = len;
		    shash_ptr->indx = indexNil;
		    shash_ptr->string = str;
		    shash_ptr->sym_ptr = sym;
		  }
	      }
	      break;

	    case st_End:
	      if ((sc_t) sym->sc == sc_Text)
		{
		  char *str = ORIG_LSTRS (fd_ptr->issBase + sym->iss);

		  if (*str != '\0')
		    {
		      Size_t len = strlen (str);
		      shash_t *shash_ptr = hash_string (str,
							(Ptrdiff_t) len,
							&orig_str_hash[0],
							(symint_t *) 0);

		      if (shash_ptr != (shash_t *) 0)
			shash_ptr->end_ptr = sym;
		    }
		}
	      break;

	    }
	}

      if (debug)
	{
	  fprintf (stderr, "\thash\tdone,  filename %s\n", filename);
	  fprintf (stderr, "\tproc\tstart, filename %s\n", filename);
	}

      /* Go through each of the procedures in this file, and add the
	 procedure pointer to the hash entry for the given name.  */

      proc_start = ORIG_PROCS (fd_ptr->ipdFirst);
      proc_end_p1 = proc_start + fd_ptr->cpd;
      for (proc = proc_start; proc < proc_end_p1; proc++)
	{
	  SYMR *proc_sym = ORIG_LSYMS (fd_ptr->isymBase + proc->isym);
	  char *str = ORIG_LSTRS (fd_ptr->issBase + proc_sym->iss);
	  Size_t len = strlen (str);
	  shash_t *shash_ptr = hash_string (str,
					    (Ptrdiff_t) len,
					    &orig_str_hash[0],
					    (symint_t *) 0);

	  if (shash_ptr == (shash_t *) 0)
	    error ("internal error, function %s is not in original symbol table", str);

	  else
	    shash_ptr->proc_ptr = proc;
	}

      if (debug)
	fprintf (stderr, "\tproc\tdone,  filename %s\n", filename);

    }
  cur_file_ptr = first_file;


  /* Copy all of the object file up to the symbol table.  Originally
     we were going to use ftruncate, but that doesn't seem to work
     on Ultrix 3.1....  */

  if (fseek (obj_in_stream, (long) 0, SEEK_SET) != 0)
    pfatal_with_name (obj_in_name);

  if (fseek (object_stream, (long) 0, SEEK_SET) != 0)
    pfatal_with_name (object_name);

  for (remaining = orig_file_header.f_symptr;
       remaining > 0;
       remaining -= num_write)
    {
      num_write
	= (remaining <= (int) sizeof (buffer))
	  ? remaining : (int) sizeof (buffer);
      sys_read = fread (buffer, 1, num_write, obj_in_stream);
      if (sys_read <= 0)
	pfatal_with_name (obj_in_name);

      else if (sys_read != num_write)
	fatal ("wanted to read %d bytes from %s, system returned %d",
	       num_write,
	       obj_in_name,
	       sys_read);

      sys_write = fwrite (buffer, 1, num_write, object_stream);
      if (sys_write <= 0)
	pfatal_with_name (object_name);

      else if (sys_write != num_write)
	fatal ("wrote %d bytes to %s, system returned %d",
	       num_write,
	       object_name,
	       sys_write);
    }
}


/* Ye olde main program.  */

extern int main (int, char **);

int
main (int argc, char **argv)
{
  int iflag = 0;
  char *p = strrchr (argv[0], '/');
  char *num_end;
  int option;
  int i;

  progname = (p != 0) ? p+1 : argv[0];

  (void) signal (SIGSEGV, catch_signal);
  (void) signal (SIGBUS,  catch_signal);
  (void) signal (SIGABRT, catch_signal);

#if !defined(__SABER__) && !defined(lint)
  if (sizeof (efdr_t) > PAGE_USIZE)
    fatal ("efdr_t has a sizeof %d bytes, when it should be less than %d",
	   (int) sizeof (efdr_t),
	   (int) PAGE_USIZE);

  if (sizeof (page_t) != PAGE_USIZE)
    fatal ("page_t has a sizeof %d bytes, when it should be %d",
	   (int) sizeof (page_t),
	   (int) PAGE_USIZE);

#endif

  alloc_counts[ alloc_type_none    ].alloc_name = "none";
  alloc_counts[ alloc_type_scope   ].alloc_name = "scope";
  alloc_counts[ alloc_type_vlinks  ].alloc_name = "vlinks";
  alloc_counts[ alloc_type_shash   ].alloc_name = "shash";
  alloc_counts[ alloc_type_thash   ].alloc_name = "thash";
  alloc_counts[ alloc_type_tag     ].alloc_name = "tag";
  alloc_counts[ alloc_type_forward ].alloc_name = "forward";
  alloc_counts[ alloc_type_thead   ].alloc_name = "thead";
  alloc_counts[ alloc_type_varray  ].alloc_name = "varray";

  int_type_info  = type_info_init;
  int_type_info.basic_type = bt_Int;

  void_type_info = type_info_init;
  void_type_info.basic_type = bt_Void;

  while ((option = getopt_long (argc, argv, "d:i:I:o:v", options, NULL)) != -1)
    switch (option)
      {
      default:
	had_errors++;
	break;

      case 'd':
	debug = strtol (optarg, &num_end, 0);
	if ((unsigned) debug > 4 || num_end == optarg)
	  had_errors++;

	break;

      case 'I':
	if (rename_output || obj_in_name != (char *) 0)
	  had_errors++;
	else
	  rename_output = 1;

	/* Fall through to 'i' case.  */

      case 'i':
	if (obj_in_name == (char *) 0)
	  {
	    obj_in_name = optarg;
	    iflag++;
	  }
	else
	  had_errors++;
	break;

      case 'o':
	if (object_name == (char *) 0)
	  object_name = optarg;
	else
	  had_errors++;
	break;

      case 'v':
	verbose++;
	break;

      case 'V':
	version++;
	break;
      }

  if (version)
    {
      printf (_("mips-tfile (GCC) %s\n"), version_string);
      fputs ("Copyright (C) 2006 Free Software Foundation, Inc.\n", stdout);
      fputs (_("This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n"),
	     stdout);
      exit (0);
    }

  if (obj_in_name == (char *) 0 && optind <= argc - 2)
    obj_in_name = argv[--argc];

  if (object_name == (char *) 0 && optind <= argc - 2)
    object_name = argv[--argc];

  /* If there is an output name, but no input name use
     the same file for both, deleting the name between
     opening it for input and opening it for output.  */
  if (obj_in_name == (char *) 0 && object_name != (char *) 0)
    {
      obj_in_name = object_name;
      delete_input = 1;
    }

  if (optind != argc - 1)
    had_errors++;

  if (verbose || had_errors)
    {
      fprintf (stderr, _("mips-tfile (GCC) %s"), version_string);
#ifdef TARGET_VERSION
      TARGET_VERSION;
#endif
      fputc ('\n', stderr);
    }

  if (object_name == (char *) 0 || had_errors)
    {
      fprintf (stderr, _("Calling Sequence:\n"));
      fprintf (stderr, _("\tmips-tfile [-d <num>] [-v] [-i <o-in-file>] -o <o-out-file> <s-file> (or)\n"));
      fprintf (stderr, _("\tmips-tfile [-d <num>] [-v] [-I <o-in-file>] -o <o-out-file> <s-file> (or)\n"));
      fprintf (stderr, _("\tmips-tfile [-d <num>] [-v] <s-file> <o-in-file> <o-out-file>\n"));
      fprintf (stderr, "\n");
      fprintf (stderr, _("Debug levels are:\n"));
      fprintf (stderr, _("    1\tGeneral debug + trace functions/blocks.\n"));
      fprintf (stderr, _("    2\tDebug level 1 + trace externals.\n"));
      fprintf (stderr, _("    3\tDebug level 2 + trace all symbols.\n"));
      fprintf (stderr, _("    4\tDebug level 3 + trace memory allocations.\n"));
      return 1;
    }

  if (obj_in_name == (char *) 0)
    obj_in_name = object_name;

  if (rename_output && rename (object_name, obj_in_name) != 0)
    {
      char *buffer = (char *) allocate_multiple_pages (4);
      int len;
      int len2;
      int in_fd;
      int out_fd;

      /* Rename failed, copy input file */
      in_fd = open (object_name, O_RDONLY, 0666);
      if (in_fd < 0)
	pfatal_with_name (object_name);

      out_fd = open (obj_in_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (out_fd < 0)
	pfatal_with_name (obj_in_name);

      while ((len = read (in_fd, buffer, 4*PAGE_SIZE)) > 0)
	{
	  len2 = write (out_fd, buffer, len);
	  if (len2 < 0)
	    pfatal_with_name (object_name);

	  if (len != len2)
	    fatal ("wrote %d bytes to %s, expected to write %d", len2, obj_in_name, len);
	}

      free_multiple_pages ((page_t *) buffer, 4);

      if (len < 0)
	pfatal_with_name (object_name);

      if (close (in_fd) < 0)
	pfatal_with_name (object_name);

      if (close (out_fd) < 0)
	pfatal_with_name (obj_in_name);
    }

  /* Must open input before output, since the output may be the same file, and
     we need to get the input handle before truncating it.  */
  obj_in_stream = fopen (obj_in_name, "r");
  if (obj_in_stream == (FILE *) 0)
    pfatal_with_name (obj_in_name);

  if (delete_input && unlink (obj_in_name) != 0)
    pfatal_with_name (obj_in_name);

  object_stream = fopen (object_name, "w");
  if (object_stream == (FILE *) 0)
    pfatal_with_name (object_name);

  if (strcmp (argv[optind], "-") != 0)
    {
      input_name = argv[optind];
      if (freopen (argv[optind], "r", stdin) != stdin)
	pfatal_with_name (argv[optind]);
    }

  copy_object ();			/* scan & copy object file */
  parse_input ();			/* scan all of input */

  update_headers ();			/* write out tfile */
  write_object ();

  if (debug)
    {
      fprintf (stderr, "\n\tAllocation summary:\n\n");
      for (i = (int) alloc_type_none; i < (int) alloc_type_last; i++)
	if (alloc_counts[i].total_alloc)
	  {
	    fprintf (stderr,
		     "\t%s\t%5d allocation(s), %5d free(s), %2d page(s)\n",
		     alloc_counts[i].alloc_name,
		     alloc_counts[i].total_alloc,
		     alloc_counts[i].total_free,
		     alloc_counts[i].total_pages);
	  }
    }

  return (had_errors) ? 1 : 0;
}


/* Catch a signal and exit without dumping core.  */

STATIC void
catch_signal (int signum)
{
  (void) signal (signum, SIG_DFL);	/* just in case...  */
  fatal ("%s", strsignal (signum));
}

/* Print a fatal error message.  NAME is the text.
   Also include a system error message based on `errno'.  */

void
pfatal_with_name (const char *msg)
{
  int save_errno = errno;		/* just in case....  */
  if (line_number > 0)
    fprintf (stderr, "%s, %s:%ld ", progname, input_name, line_number);
  else
    fprintf (stderr, "%s:", progname);

  errno = save_errno;
  if (errno == 0)
    fprintf (stderr, "[errno = 0] %s\n", msg);
  else
    perror (msg);

  exit (1);
}


/* Procedure to die with an out of bounds error message.  It has
   type int, so it can be used with an ?: expression within the
   ORIG_xxx macros, but the function never returns.  */

static int
out_of_bounds (symint_t indx,	/* index that is out of bounds */
	       symint_t max,	/* maximum index */
	       const char *str, /* string to print out */
	       int prog_line)	/* line number within mips-tfile.c */
{
  if (indx < max)		/* just in case */
    return 0;

  fprintf (stderr, "%s, %s:%ld index %lu is out of bounds for %s, max is %lu, mips-tfile.c line# %d\n",
	   progname, input_name, line_number, indx, str, max, prog_line);

  exit (1);
  return 0;			/* turn off warning messages */
}


/* Allocate a cluster of pages.  USE_MALLOC says that malloc does not
   like sbrk's behind its back (or sbrk isn't available).  If we use
   sbrk, we assume it gives us zeroed pages.  */

#ifndef MALLOC_CHECK
#ifdef USE_MALLOC

STATIC page_t *
allocate_cluster (Size_t npages)
{
  page_t *value = xcalloc (npages, PAGE_USIZE);

  if (debug > 3)
    fprintf (stderr, "\talloc\tnpages = %d, value = 0x%.8x\n", npages, value);

  return value;
}

#else /* USE_MALLOC */

STATIC page_t *
allocate_cluster (Size_t npages)
{
  page_t *ptr = (page_t *) sbrk (0);	/* current sbreak */
  unsigned long offset = ((unsigned long) ptr) & (PAGE_SIZE - 1);

  if (offset != 0)			/* align to a page boundary */
    {
      if (sbrk (PAGE_USIZE - offset) == (char *)-1)
	pfatal_with_name ("allocate_cluster");

      ptr = (page_t *) (((char *) ptr) + PAGE_SIZE - offset);
    }

  if (sbrk (npages * PAGE_USIZE) == (char *) -1)
    pfatal_with_name ("allocate_cluster");

  if (debug > 3)
    fprintf (stderr, "\talloc\tnpages = %lu, value = %p\n",
	     (unsigned long) npages, (void *) ptr);

  return ptr;
}

#endif /* USE_MALLOC */


static page_t	*cluster_ptr	= NULL;
static unsigned	 pages_left	= 0;

#endif /* MALLOC_CHECK */


/* Allocate some pages (which is initialized to 0).  */

STATIC page_t *
allocate_multiple_pages (Size_t npages)
{
#ifndef MALLOC_CHECK
  if (pages_left == 0 && npages < MAX_CLUSTER_PAGES)
    {
      pages_left = MAX_CLUSTER_PAGES;
      cluster_ptr = allocate_cluster (MAX_CLUSTER_PAGES);
    }

  if (npages <= pages_left)
    {
      page_t *ptr = cluster_ptr;
      cluster_ptr += npages;
      pages_left -= npages;
      return ptr;
    }

  return allocate_cluster (npages);

#else	/* MALLOC_CHECK */
  return xcalloc (npages, PAGE_SIZE);

#endif	/* MALLOC_CHECK */
}


/* Release some pages.  */

STATIC void
free_multiple_pages (page_t *page_ptr, Size_t npages)
{
#ifndef MALLOC_CHECK
  if (pages_left == 0)
    {
      cluster_ptr = page_ptr;
      pages_left = npages;
    }

  else if ((page_ptr + npages) == cluster_ptr)
    {
      cluster_ptr -= npages;
      pages_left += npages;
    }

  /* otherwise the page is not freed.  If more than call is
     done, we probably should worry about it, but at present,
     the free pages is done right after an allocate.  */

#else	/* MALLOC_CHECK */
  free (page_ptr);

#endif	/* MALLOC_CHECK */
}


/* Allocate one page (which is initialized to 0).  */

STATIC page_t *
allocate_page (void)
{
#ifndef MALLOC_CHECK
  if (pages_left == 0)
    {
      pages_left = MAX_CLUSTER_PAGES;
      cluster_ptr = allocate_cluster (MAX_CLUSTER_PAGES);
    }

  pages_left--;
  return cluster_ptr++;

#else	/* MALLOC_CHECK */
  return xcalloc (1, PAGE_SIZE);

#endif	/* MALLOC_CHECK */
}


/* Allocate scoping information.  */

STATIC scope_t *
allocate_scope (void)
{
  scope_t *ptr;
  static scope_t initial_scope;

#ifndef MALLOC_CHECK
  ptr = alloc_counts[ (int) alloc_type_scope ].free_list.f_scope;
  if (ptr != (scope_t *) 0)
    alloc_counts[ (int) alloc_type_scope ].free_list.f_scope = ptr->free;

  else
    {
      int unallocated	= alloc_counts[ (int) alloc_type_scope ].unallocated;
      page_t *cur_page	= alloc_counts[ (int) alloc_type_scope ].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (scope_t);
	  alloc_counts[ (int) alloc_type_scope ].cur_page = cur_page = allocate_page ();
	  alloc_counts[ (int) alloc_type_scope ].total_pages++;
	}

      ptr = &cur_page->scope[ --unallocated ];
      alloc_counts[ (int) alloc_type_scope ].unallocated = unallocated;
    }

#else
  ptr = xmalloc (sizeof (scope_t));

#endif

  alloc_counts[ (int) alloc_type_scope ].total_alloc++;
  *ptr = initial_scope;
  return ptr;
}

/* Free scoping information.  */

STATIC void
free_scope (scope_t *ptr)
{
  alloc_counts[ (int) alloc_type_scope ].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = alloc_counts[ (int) alloc_type_scope ].free_list.f_scope;
  alloc_counts[ (int) alloc_type_scope ].free_list.f_scope = ptr;

#else
  free (ptr);
#endif

}


/* Allocate links for pages in a virtual array.  */

STATIC vlinks_t *
allocate_vlinks (void)
{
  vlinks_t *ptr;
  static vlinks_t initial_vlinks;

#ifndef MALLOC_CHECK
  int unallocated	= alloc_counts[ (int) alloc_type_vlinks ].unallocated;
  page_t *cur_page	= alloc_counts[ (int) alloc_type_vlinks ].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (vlinks_t);
      alloc_counts[ (int) alloc_type_vlinks ].cur_page = cur_page = allocate_page ();
      alloc_counts[ (int) alloc_type_vlinks ].total_pages++;
    }

  ptr = &cur_page->vlinks[ --unallocated ];
  alloc_counts[ (int) alloc_type_vlinks ].unallocated = unallocated;

#else
  ptr = xmalloc (sizeof (vlinks_t));

#endif

  alloc_counts[ (int) alloc_type_vlinks ].total_alloc++;
  *ptr = initial_vlinks;
  return ptr;
}


/* Allocate string hash buckets.  */

STATIC shash_t *
allocate_shash (void)
{
  shash_t *ptr;
  static shash_t initial_shash;

#ifndef MALLOC_CHECK
  int unallocated	= alloc_counts[ (int) alloc_type_shash ].unallocated;
  page_t *cur_page	= alloc_counts[ (int) alloc_type_shash ].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (shash_t);
      alloc_counts[ (int) alloc_type_shash ].cur_page = cur_page = allocate_page ();
      alloc_counts[ (int) alloc_type_shash ].total_pages++;
    }

  ptr = &cur_page->shash[ --unallocated ];
  alloc_counts[ (int) alloc_type_shash ].unallocated = unallocated;

#else
  ptr = xmalloc (sizeof (shash_t));

#endif

  alloc_counts[ (int) alloc_type_shash ].total_alloc++;
  *ptr = initial_shash;
  return ptr;
}


/* Allocate type hash buckets.  */

STATIC thash_t *
allocate_thash (void)
{
  thash_t *ptr;
  static thash_t initial_thash;

#ifndef MALLOC_CHECK
  int unallocated	= alloc_counts[ (int) alloc_type_thash ].unallocated;
  page_t *cur_page	= alloc_counts[ (int) alloc_type_thash ].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (thash_t);
      alloc_counts[ (int) alloc_type_thash ].cur_page = cur_page = allocate_page ();
      alloc_counts[ (int) alloc_type_thash ].total_pages++;
    }

  ptr = &cur_page->thash[ --unallocated ];
  alloc_counts[ (int) alloc_type_thash ].unallocated = unallocated;

#else
  ptr = xmalloc (sizeof (thash_t));

#endif

  alloc_counts[ (int) alloc_type_thash ].total_alloc++;
  *ptr = initial_thash;
  return ptr;
}


/* Allocate structure, union, or enum tag information.  */

STATIC tag_t *
allocate_tag (void)
{
  tag_t *ptr;
  static tag_t initial_tag;

#ifndef MALLOC_CHECK
  ptr = alloc_counts[ (int) alloc_type_tag ].free_list.f_tag;
  if (ptr != (tag_t *) 0)
    alloc_counts[ (int) alloc_type_tag ].free_list.f_tag = ptr->free;

  else
    {
      int unallocated	= alloc_counts[ (int) alloc_type_tag ].unallocated;
      page_t *cur_page	= alloc_counts[ (int) alloc_type_tag ].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (tag_t);
	  alloc_counts[ (int) alloc_type_tag ].cur_page = cur_page = allocate_page ();
	  alloc_counts[ (int) alloc_type_tag ].total_pages++;
	}

      ptr = &cur_page->tag[ --unallocated ];
      alloc_counts[ (int) alloc_type_tag ].unallocated = unallocated;
    }

#else
  ptr = xmalloc (sizeof (tag_t));

#endif

  alloc_counts[ (int) alloc_type_tag ].total_alloc++;
  *ptr = initial_tag;
  return ptr;
}

/* Free scoping information.  */

STATIC void
free_tag (tag_t *ptr)
{
  alloc_counts[ (int) alloc_type_tag ].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = alloc_counts[ (int) alloc_type_tag ].free_list.f_tag;
  alloc_counts[ (int) alloc_type_tag ].free_list.f_tag = ptr;

#else
  free (ptr);
#endif

}


/* Allocate forward reference to a yet unknown tag.  */

STATIC forward_t *
allocate_forward (void)
{
  forward_t *ptr;
  static forward_t initial_forward;

#ifndef MALLOC_CHECK
  ptr = alloc_counts[ (int) alloc_type_forward ].free_list.f_forward;
  if (ptr != (forward_t *) 0)
    alloc_counts[ (int) alloc_type_forward ].free_list.f_forward = ptr->free;

  else
    {
      int unallocated	= alloc_counts[ (int) alloc_type_forward ].unallocated;
      page_t *cur_page	= alloc_counts[ (int) alloc_type_forward ].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (forward_t);
	  alloc_counts[ (int) alloc_type_forward ].cur_page = cur_page = allocate_page ();
	  alloc_counts[ (int) alloc_type_forward ].total_pages++;
	}

      ptr = &cur_page->forward[ --unallocated ];
      alloc_counts[ (int) alloc_type_forward ].unallocated = unallocated;
    }

#else
  ptr = xmalloc (sizeof (forward_t));

#endif

  alloc_counts[ (int) alloc_type_forward ].total_alloc++;
  *ptr = initial_forward;
  return ptr;
}

/* Free scoping information.  */

STATIC void
free_forward (forward_t *ptr)
{
  alloc_counts[ (int) alloc_type_forward ].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = alloc_counts[ (int) alloc_type_forward ].free_list.f_forward;
  alloc_counts[ (int) alloc_type_forward ].free_list.f_forward = ptr;

#else
  free (ptr);
#endif

}


/* Allocate head of type hash list.  */

STATIC thead_t *
allocate_thead (void)
{
  thead_t *ptr;
  static thead_t initial_thead;

#ifndef MALLOC_CHECK
  ptr = alloc_counts[ (int) alloc_type_thead ].free_list.f_thead;
  if (ptr != (thead_t *) 0)
    alloc_counts[ (int) alloc_type_thead ].free_list.f_thead = ptr->free;

  else
    {
      int unallocated	= alloc_counts[ (int) alloc_type_thead ].unallocated;
      page_t *cur_page	= alloc_counts[ (int) alloc_type_thead ].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (thead_t);
	  alloc_counts[ (int) alloc_type_thead ].cur_page = cur_page = allocate_page ();
	  alloc_counts[ (int) alloc_type_thead ].total_pages++;
	}

      ptr = &cur_page->thead[ --unallocated ];
      alloc_counts[ (int) alloc_type_thead ].unallocated = unallocated;
    }

#else
  ptr = xmalloc (sizeof (thead_t));

#endif

  alloc_counts[ (int) alloc_type_thead ].total_alloc++;
  *ptr = initial_thead;
  return ptr;
}

/* Free scoping information.  */

STATIC void
free_thead (thead_t *ptr)
{
  alloc_counts[ (int) alloc_type_thead ].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = (thead_t *) alloc_counts[ (int) alloc_type_thead ].free_list.f_thead;
  alloc_counts[ (int) alloc_type_thead ].free_list.f_thead = ptr;

#else
  free (ptr);
#endif

}

#endif /* MIPS_DEBUGGING_INFO */


/* Output an error message and exit.  */

void
fatal (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);

  if (line_number > 0)
    fprintf (stderr, "%s, %s:%ld ", progname, input_name, line_number);
  else
    fprintf (stderr, "%s:", progname);

  vfprintf (stderr, format, ap);
  va_end (ap);
  fprintf (stderr, "\n");
  if (line_number > 0)
    fprintf (stderr, "line:\t%s\n", cur_line_start);

  saber_stop ();
  exit (1);
}

void
error (const char *format, ...)
{
  va_list ap;

  va_start (ap, format);

  if (line_number > 0)
    fprintf (stderr, "%s, %s:%ld ", progname, input_name, line_number);
  else
    fprintf (stderr, "%s:", progname);

  vfprintf (stderr, format, ap);
  fprintf (stderr, "\n");
  if (line_number > 0)
    fprintf (stderr, "line:\t%s\n", cur_line_start);

  had_errors++;
  va_end (ap);

  saber_stop ();
}

/* More 'friendly' abort that prints the line and file.  */

void
fancy_abort (const char *file, int line, const char *func)
{
  fatal ("abort in %s, at %s:%d", func, file, line);
}


/* When `malloc.c' is compiled with `rcheck' defined,
   it calls this function to report clobberage.  */

void
botch (const char *s)
{
  fatal ("%s", s);
}
