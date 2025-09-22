/* alloca.c -- allocate automatically reclaimed memory
   (Mostly) portable public-domain implementation -- D A Gwyn

   This implementation of the PWB library alloca function,
   which is used to allocate space off the run-time stack so
   that it is automatically reclaimed upon procedure exit,
   was inspired by discussions with J. Q. Johnson of Cornell.
   J.Otto Tennant <jot@cray.com> contributed the Cray support.

   There are some preprocessor constants that can
   be defined when compiling for your specific system, for
   improved efficiency; however, the defaults should be okay.

   The general concept of this implementation is to keep
   track of all alloca-allocated blocks, and reclaim any
   that are found to be deeper in the stack than the current
   invocation.  This heuristic does not reclaim storage as
   soon as it becomes invalid, but it will do so eventually.

   As a special case, alloca(0) reclaims storage without
   allocating any.  It is a good idea to use alloca(0) in
   your main control loop, etc. to force garbage collection.  */

/*

@deftypefn Replacement void* alloca (size_t @var{size})

This function allocates memory which will be automatically reclaimed
after the procedure exits.  The @libib{} implementation does not free
the memory immediately but will do so eventually during subsequent
calls to this function.  Memory is allocated using @code{xmalloc} under
normal circumstances.

The header file @file{alloca-conf.h} can be used in conjunction with the
GNU Autoconf test @code{AC_FUNC_ALLOCA} to test for and properly make
available this function.  The @code{AC_FUNC_ALLOCA} test requires that
client code use a block of preprocessor code to be safe (see the Autoconf
manual for more); this header incorporates that logic and more, including
the possibility of a GCC built-in function.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libiberty.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* These variables are used by the ASTRDUP implementation that relies
   on C_alloca.  */
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
const char *libiberty_optr;
char *libiberty_nptr;
unsigned long libiberty_len;
#ifdef __cplusplus
}
#endif /* __cplusplus */

/* If your stack is a linked list of frames, you have to
   provide an "address metric" ADDRESS_FUNCTION macro.  */

#if defined (CRAY) && defined (CRAY_STACKSEG_END)
static long i00afunc ();
#define ADDRESS_FUNCTION(arg) (char *) i00afunc (&(arg))
#else
#define ADDRESS_FUNCTION(arg) &(arg)
#endif

#ifndef NULL
#define	NULL	0
#endif

/* Define STACK_DIRECTION if you know the direction of stack
   growth for your system; otherwise it will be automatically
   deduced at run-time.

   STACK_DIRECTION > 0 => grows toward higher addresses
   STACK_DIRECTION < 0 => grows toward lower addresses
   STACK_DIRECTION = 0 => direction of growth unknown  */

#ifndef STACK_DIRECTION
#define	STACK_DIRECTION	0	/* Direction unknown.  */
#endif

#if STACK_DIRECTION != 0

#define	STACK_DIR	STACK_DIRECTION	/* Known at compile-time.  */

#else /* STACK_DIRECTION == 0; need run-time code.  */

static int stack_dir;		/* 1 or -1 once known.  */
#define	STACK_DIR	stack_dir

static void
find_stack_direction (void)
{
  static char *addr = NULL;	/* Address of first `dummy', once known.  */
  auto char dummy;		/* To get stack address.  */

  if (addr == NULL)
    {				/* Initial entry.  */
      addr = ADDRESS_FUNCTION (dummy);

      find_stack_direction ();	/* Recurse once.  */
    }
  else
    {
      /* Second entry.  */
      if (ADDRESS_FUNCTION (dummy) > addr)
	stack_dir = 1;		/* Stack grew upward.  */
      else
	stack_dir = -1;		/* Stack grew downward.  */
    }
}

#endif /* STACK_DIRECTION == 0 */

/* An "alloca header" is used to:
   (a) chain together all alloca'ed blocks;
   (b) keep track of stack depth.

   It is very important that sizeof(header) agree with malloc
   alignment chunk size.  The following default should work okay.  */

#ifndef	ALIGN_SIZE
#define	ALIGN_SIZE	sizeof(double)
#endif

typedef union hdr
{
  char align[ALIGN_SIZE];	/* To force sizeof(header).  */
  struct
    {
      union hdr *next;		/* For chaining headers.  */
      char *deep;		/* For stack depth measure.  */
    } h;
} header;

static header *last_alloca_header = NULL;	/* -> last alloca header.  */

/* Return a pointer to at least SIZE bytes of storage,
   which will be automatically reclaimed upon exit from
   the procedure that called alloca.  Originally, this space
   was supposed to be taken from the current stack frame of the
   caller, but that method cannot be made to work for some
   implementations of C, for example under Gould's UTX/32.  */

/* @undocumented C_alloca */

PTR
C_alloca (size_t size)
{
  auto char probe;		/* Probes stack depth: */
  register char *depth = ADDRESS_FUNCTION (probe);

#if STACK_DIRECTION == 0
  if (STACK_DIR == 0)		/* Unknown growth direction.  */
    find_stack_direction ();
#endif

  /* Reclaim garbage, defined as all alloca'd storage that
     was allocated from deeper in the stack than currently.  */

  {
    register header *hp;	/* Traverses linked list.  */

    for (hp = last_alloca_header; hp != NULL;)
      if ((STACK_DIR > 0 && hp->h.deep > depth)
	  || (STACK_DIR < 0 && hp->h.deep < depth))
	{
	  register header *np = hp->h.next;

	  free ((PTR) hp);	/* Collect garbage.  */

	  hp = np;		/* -> next header.  */
	}
      else
	break;			/* Rest are not deeper.  */

    last_alloca_header = hp;	/* -> last valid storage.  */
  }

  if (size == 0)
    return NULL;		/* No allocation required.  */

  /* Allocate combined header + user data storage.  */

  {
    register void *new_storage = XNEWVEC (char, sizeof (header) + size);
    /* Address of header.  */

    if (new_storage == 0)
      abort();

    ((header *) new_storage)->h.next = last_alloca_header;
    ((header *) new_storage)->h.deep = depth;

    last_alloca_header = (header *) new_storage;

    /* User storage begins just after header.  */

    return (PTR) ((char *) new_storage + sizeof (header));
  }
}

#if defined (CRAY) && defined (CRAY_STACKSEG_END)

#ifdef DEBUG_I00AFUNC
#include <stdio.h>
#endif

#ifndef CRAY_STACK
#define CRAY_STACK
#ifndef CRAY2
/* Stack structures for CRAY-1, CRAY X-MP, and CRAY Y-MP */
struct stack_control_header
  {
    long shgrow:32;		/* Number of times stack has grown.  */
    long shaseg:32;		/* Size of increments to stack.  */
    long shhwm:32;		/* High water mark of stack.  */
    long shsize:32;		/* Current size of stack (all segments).  */
  };

/* The stack segment linkage control information occurs at
   the high-address end of a stack segment.  (The stack
   grows from low addresses to high addresses.)  The initial
   part of the stack segment linkage control information is
   0200 (octal) words.  This provides for register storage
   for the routine which overflows the stack.  */

struct stack_segment_linkage
  {
    long ss[0200];		/* 0200 overflow words.  */
    long sssize:32;		/* Number of words in this segment.  */
    long ssbase:32;		/* Offset to stack base.  */
    long:32;
    long sspseg:32;		/* Offset to linkage control of previous
				   segment of stack.  */
    long:32;
    long sstcpt:32;		/* Pointer to task common address block.  */
    long sscsnm;		/* Private control structure number for
				   microtasking.  */
    long ssusr1;		/* Reserved for user.  */
    long ssusr2;		/* Reserved for user.  */
    long sstpid;		/* Process ID for pid based multi-tasking.  */
    long ssgvup;		/* Pointer to multitasking thread giveup.  */
    long sscray[7];		/* Reserved for Cray Research.  */
    long ssa0;
    long ssa1;
    long ssa2;
    long ssa3;
    long ssa4;
    long ssa5;
    long ssa6;
    long ssa7;
    long sss0;
    long sss1;
    long sss2;
    long sss3;
    long sss4;
    long sss5;
    long sss6;
    long sss7;
  };

#else /* CRAY2 */
/* The following structure defines the vector of words
   returned by the STKSTAT library routine.  */
struct stk_stat
  {
    long now;			/* Current total stack size.  */
    long maxc;			/* Amount of contiguous space which would
				   be required to satisfy the maximum
				   stack demand to date.  */
    long high_water;		/* Stack high-water mark.  */
    long overflows;		/* Number of stack overflow ($STKOFEN) calls.  */
    long hits;			/* Number of internal buffer hits.  */
    long extends;		/* Number of block extensions.  */
    long stko_mallocs;		/* Block allocations by $STKOFEN.  */
    long underflows;		/* Number of stack underflow calls ($STKRETN).  */
    long stko_free;		/* Number of deallocations by $STKRETN.  */
    long stkm_free;		/* Number of deallocations by $STKMRET.  */
    long segments;		/* Current number of stack segments.  */
    long maxs;			/* Maximum number of stack segments so far.  */
    long pad_size;		/* Stack pad size.  */
    long current_address;	/* Current stack segment address.  */
    long current_size;		/* Current stack segment size.  This
				   number is actually corrupted by STKSTAT to
				   include the fifteen word trailer area.  */
    long initial_address;	/* Address of initial segment.  */
    long initial_size;		/* Size of initial segment.  */
  };

/* The following structure describes the data structure which trails
   any stack segment.  I think that the description in 'asdef' is
   out of date.  I only describe the parts that I am sure about.  */

struct stk_trailer
  {
    long this_address;		/* Address of this block.  */
    long this_size;		/* Size of this block (does not include
				   this trailer).  */
    long unknown2;
    long unknown3;
    long link;			/* Address of trailer block of previous
				   segment.  */
    long unknown5;
    long unknown6;
    long unknown7;
    long unknown8;
    long unknown9;
    long unknown10;
    long unknown11;
    long unknown12;
    long unknown13;
    long unknown14;
  };

#endif /* CRAY2 */
#endif /* not CRAY_STACK */

#ifdef CRAY2
/* Determine a "stack measure" for an arbitrary ADDRESS.
   I doubt that "lint" will like this much.  */

static long
i00afunc (long *address)
{
  struct stk_stat status;
  struct stk_trailer *trailer;
  long *block, size;
  long result = 0;

  /* We want to iterate through all of the segments.  The first
     step is to get the stack status structure.  We could do this
     more quickly and more directly, perhaps, by referencing the
     $LM00 common block, but I know that this works.  */

  STKSTAT (&status);

  /* Set up the iteration.  */

  trailer = (struct stk_trailer *) (status.current_address
				    + status.current_size
				    - 15);

  /* There must be at least one stack segment.  Therefore it is
     a fatal error if "trailer" is null.  */

  if (trailer == 0)
    abort ();

  /* Discard segments that do not contain our argument address.  */

  while (trailer != 0)
    {
      block = (long *) trailer->this_address;
      size = trailer->this_size;
      if (block == 0 || size == 0)
	abort ();
      trailer = (struct stk_trailer *) trailer->link;
      if ((block <= address) && (address < (block + size)))
	break;
    }

  /* Set the result to the offset in this segment and add the sizes
     of all predecessor segments.  */

  result = address - block;

  if (trailer == 0)
    {
      return result;
    }

  do
    {
      if (trailer->this_size <= 0)
	abort ();
      result += trailer->this_size;
      trailer = (struct stk_trailer *) trailer->link;
    }
  while (trailer != 0);

  /* We are done.  Note that if you present a bogus address (one
     not in any segment), you will get a different number back, formed
     from subtracting the address of the first block.  This is probably
     not what you want.  */

  return (result);
}

#else /* not CRAY2 */
/* Stack address function for a CRAY-1, CRAY X-MP, or CRAY Y-MP.
   Determine the number of the cell within the stack,
   given the address of the cell.  The purpose of this
   routine is to linearize, in some sense, stack addresses
   for alloca.  */

static long
i00afunc (long address)
{
  long stkl = 0;

  long size, pseg, this_segment, stack;
  long result = 0;

  struct stack_segment_linkage *ssptr;

  /* Register B67 contains the address of the end of the
     current stack segment.  If you (as a subprogram) store
     your registers on the stack and find that you are past
     the contents of B67, you have overflowed the segment.

     B67 also points to the stack segment linkage control
     area, which is what we are really interested in.  */

  stkl = CRAY_STACKSEG_END ();
  ssptr = (struct stack_segment_linkage *) stkl;

  /* If one subtracts 'size' from the end of the segment,
     one has the address of the first word of the segment.

     If this is not the first segment, 'pseg' will be
     nonzero.  */

  pseg = ssptr->sspseg;
  size = ssptr->sssize;

  this_segment = stkl - size;

  /* It is possible that calling this routine itself caused
     a stack overflow.  Discard stack segments which do not
     contain the target address.  */

  while (!(this_segment <= address && address <= stkl))
    {
#ifdef DEBUG_I00AFUNC
      fprintf (stderr, "%011o %011o %011o\n", this_segment, address, stkl);
#endif
      if (pseg == 0)
	break;
      stkl = stkl - pseg;
      ssptr = (struct stack_segment_linkage *) stkl;
      size = ssptr->sssize;
      pseg = ssptr->sspseg;
      this_segment = stkl - size;
    }

  result = address - this_segment;

  /* If you subtract pseg from the current end of the stack,
     you get the address of the previous stack segment's end.
     This seems a little convoluted to me, but I'll bet you save
     a cycle somewhere.  */

  while (pseg != 0)
    {
#ifdef DEBUG_I00AFUNC
      fprintf (stderr, "%011o %011o\n", pseg, size);
#endif
      stkl = stkl - pseg;
      ssptr = (struct stack_segment_linkage *) stkl;
      size = ssptr->sssize;
      pseg = ssptr->sspseg;
      result += size;
    }
  return (result);
}

#endif /* not CRAY2 */
#endif /* CRAY */
