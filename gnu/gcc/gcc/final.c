/* Convert RTL to assembler code and output it, for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997,
   1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

/* This is the final pass of the compiler.
   It looks at the rtl code for a function and outputs assembler code.

   Call `final_start_function' to output the assembler code for function entry,
   `final' to output assembler code for some RTL code,
   `final_end_function' to output assembler code for function exit.
   If a function is compiled in several pieces, each piece is
   output separately with `final'.

   Some optimizations are also done at this level.
   Move instructions that were made unnecessary by good register allocation
   are detected and omitted from the output.  (Though most of these
   are removed by the last jump pass.)

   Instructions to set the condition codes are omitted when it can be
   seen that the condition codes already had the desired values.

   In some cases it is sufficient if the inherited condition codes
   have related values, but this may require the following insn
   (the one that tests the condition codes) to be modified.

   The code for the function prologue and epilogue are generated
   directly in assembler by the target functions function_prologue and
   function_epilogue.  Those instructions never exist as rtl.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "conditions.h"
#include "flags.h"
#include "real.h"
#include "hard-reg-set.h"
#include "output.h"
#include "except.h"
#include "function.h"
#include "toplev.h"
#include "reload.h"
#include "intl.h"
#include "basic-block.h"
#include "target.h"
#include "debug.h"
#include "expr.h"
#include "cfglayout.h"
#include "tree-pass.h"
#include "timevar.h"
#include "cgraph.h"
#include "coverage.h"

#ifdef XCOFF_DEBUGGING_INFO
#include "xcoffout.h"		/* Needed for external data
				   declarations for e.g. AIX 4.x.  */
#endif

#if defined (DWARF2_UNWIND_INFO) || defined (DWARF2_DEBUGGING_INFO)
#include "dwarf2out.h"
#endif

#ifdef DBX_DEBUGGING_INFO
#include "dbxout.h"
#endif

#ifdef SDB_DEBUGGING_INFO
#include "sdbout.h"
#endif

/* If we aren't using cc0, CC_STATUS_INIT shouldn't exist.  So define a
   null default for it to save conditionalization later.  */
#ifndef CC_STATUS_INIT
#define CC_STATUS_INIT
#endif

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

/* Is the given character a logical line separator for the assembler?  */
#ifndef IS_ASM_LOGICAL_LINE_SEPARATOR
#define IS_ASM_LOGICAL_LINE_SEPARATOR(C) ((C) == ';')
#endif

#ifndef JUMP_TABLES_IN_TEXT_SECTION
#define JUMP_TABLES_IN_TEXT_SECTION 0
#endif

/* Bitflags used by final_scan_insn.  */
#define SEEN_BB		1
#define SEEN_NOTE	2
#define SEEN_EMITTED	4

/* Last insn processed by final_scan_insn.  */
static rtx debug_insn;
rtx current_output_insn;

/* Line number of last NOTE.  */
static int last_linenum;

/* Highest line number in current block.  */
static int high_block_linenum;

/* Likewise for function.  */
static int high_function_linenum;

/* Filename of last NOTE.  */
static const char *last_filename;

/* Whether to force emission of a line note before the next insn.  */
static bool force_source_line = false;

extern const int length_unit_log; /* This is defined in insn-attrtab.c.  */

/* Nonzero while outputting an `asm' with operands.
   This means that inconsistencies are the user's fault, so don't die.
   The precise value is the insn being output, to pass to error_for_asm.  */
rtx this_is_asm_operands;

/* Number of operands of this insn, for an `asm' with operands.  */
static unsigned int insn_noperands;

/* Compare optimization flag.  */

static rtx last_ignored_compare = 0;

/* Assign a unique number to each insn that is output.
   This can be used to generate unique local labels.  */

static int insn_counter = 0;

#ifdef HAVE_cc0
/* This variable contains machine-dependent flags (defined in tm.h)
   set and examined by output routines
   that describe how to interpret the condition codes properly.  */

CC_STATUS cc_status;

/* During output of an insn, this contains a copy of cc_status
   from before the insn.  */

CC_STATUS cc_prev_status;
#endif

/* Indexed by hardware reg number, is 1 if that register is ever
   used in the current function.

   In life_analysis, or in stupid_life_analysis, this is set
   up to record the hard regs used explicitly.  Reload adds
   in the hard regs used for holding pseudo regs.  Final uses
   it to generate the code in the function prologue and epilogue
   to save and restore registers as needed.  */

char regs_ever_live[FIRST_PSEUDO_REGISTER];

/* Like regs_ever_live, but 1 if a reg is set or clobbered from an asm.
   Unlike regs_ever_live, elements of this array corresponding to
   eliminable regs like the frame pointer are set if an asm sets them.  */

char regs_asm_clobbered[FIRST_PSEUDO_REGISTER];

/* Nonzero means current function must be given a frame pointer.
   Initialized in function.c to 0.  Set only in reload1.c as per
   the needs of the function.  */

int frame_pointer_needed;

/* Number of unmatched NOTE_INSN_BLOCK_BEG notes we have seen.  */

static int block_depth;

/* Nonzero if have enabled APP processing of our assembler output.  */

static int app_on;

/* If we are outputting an insn sequence, this contains the sequence rtx.
   Zero otherwise.  */

rtx final_sequence;

#ifdef ASSEMBLER_DIALECT

/* Number of the assembler dialect to use, starting at 0.  */
static int dialect_number;
#endif

#ifdef HAVE_conditional_execution
/* Nonnull if the insn currently being emitted was a COND_EXEC pattern.  */
rtx current_insn_predicate;
#endif

#ifdef HAVE_ATTR_length
static int asm_insn_count (rtx);
#endif
static void profile_function (FILE *);
static void profile_after_prologue (FILE *);
static bool notice_source_line (rtx);
static rtx walk_alter_subreg (rtx *);
static void output_asm_name (void);
static void output_alternate_entry_point (FILE *, rtx);
static tree get_mem_expr_from_op (rtx, int *);
static void output_asm_operand_names (rtx *, int *, int);
static void output_operand (rtx, int);
#ifdef LEAF_REGISTERS
static void leaf_renumber_regs (rtx);
#endif
#ifdef HAVE_cc0
static int alter_cond (rtx);
#endif
#ifndef ADDR_VEC_ALIGN
static int final_addr_vec_align (rtx);
#endif
#ifdef HAVE_ATTR_length
static int align_fuzz (rtx, rtx, int, unsigned);
#endif

/* Initialize data in final at the beginning of a compilation.  */

void
init_final (const char *filename ATTRIBUTE_UNUSED)
{
  app_on = 0;
  final_sequence = 0;

#ifdef ASSEMBLER_DIALECT
  dialect_number = ASSEMBLER_DIALECT;
#endif
}

/* Default target function prologue and epilogue assembler output.

   If not overridden for epilogue code, then the function body itself
   contains return instructions wherever needed.  */
void
default_function_pro_epilogue (FILE *file ATTRIBUTE_UNUSED,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
}

/* Default target hook that outputs nothing to a stream.  */
void
no_asm_to_stream (FILE *file ATTRIBUTE_UNUSED)
{
}

/* Enable APP processing of subsequent output.
   Used before the output from an `asm' statement.  */

void
app_enable (void)
{
  if (! app_on)
    {
      fputs (ASM_APP_ON, asm_out_file);
      app_on = 1;
    }
}

/* Disable APP processing of subsequent output.
   Called from varasm.c before most kinds of output.  */

void
app_disable (void)
{
  if (app_on)
    {
      fputs (ASM_APP_OFF, asm_out_file);
      app_on = 0;
    }
}

/* Return the number of slots filled in the current
   delayed branch sequence (we don't count the insn needing the
   delay slot).   Zero if not in a delayed branch sequence.  */

#ifdef DELAY_SLOTS
int
dbr_sequence_length (void)
{
  if (final_sequence != 0)
    return XVECLEN (final_sequence, 0) - 1;
  else
    return 0;
}
#endif

/* The next two pages contain routines used to compute the length of an insn
   and to shorten branches.  */

/* Arrays for insn lengths, and addresses.  The latter is referenced by
   `insn_current_length'.  */

static int *insn_lengths;

varray_type insn_addresses_;

/* Max uid for which the above arrays are valid.  */
static int insn_lengths_max_uid;

/* Address of insn being processed.  Used by `insn_current_length'.  */
int insn_current_address;

/* Address of insn being processed in previous iteration.  */
int insn_last_address;

/* known invariant alignment of insn being processed.  */
int insn_current_align;

/* After shorten_branches, for any insn, uid_align[INSN_UID (insn)]
   gives the next following alignment insn that increases the known
   alignment, or NULL_RTX if there is no such insn.
   For any alignment obtained this way, we can again index uid_align with
   its uid to obtain the next following align that in turn increases the
   alignment, till we reach NULL_RTX; the sequence obtained this way
   for each insn we'll call the alignment chain of this insn in the following
   comments.  */

struct label_alignment
{
  short alignment;
  short max_skip;
};

static rtx *uid_align;
static int *uid_shuid;
static struct label_alignment *label_align;

/* Indicate that branch shortening hasn't yet been done.  */

void
init_insn_lengths (void)
{
  if (uid_shuid)
    {
      free (uid_shuid);
      uid_shuid = 0;
    }
  if (insn_lengths)
    {
      free (insn_lengths);
      insn_lengths = 0;
      insn_lengths_max_uid = 0;
    }
#ifdef HAVE_ATTR_length
  INSN_ADDRESSES_FREE ();
#endif
  if (uid_align)
    {
      free (uid_align);
      uid_align = 0;
    }
}

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, use FALLBACK_FN to calculate the
   length.  */
static inline int
get_attr_length_1 (rtx insn ATTRIBUTE_UNUSED,
		   int (*fallback_fn) (rtx) ATTRIBUTE_UNUSED)
{
#ifdef HAVE_ATTR_length
  rtx body;
  int i;
  int length = 0;

  if (insn_lengths_max_uid > INSN_UID (insn))
    return insn_lengths[INSN_UID (insn)];
  else
    switch (GET_CODE (insn))
      {
      case NOTE:
      case BARRIER:
      case CODE_LABEL:
	return 0;

      case CALL_INSN:
	length = fallback_fn (insn);
	break;

      case JUMP_INSN:
	body = PATTERN (insn);
	if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	  {
	    /* Alignment is machine-dependent and should be handled by
	       ADDR_VEC_ALIGN.  */
	  }
	else
	  length = fallback_fn (insn);
	break;

      case INSN:
	body = PATTERN (insn);
	if (GET_CODE (body) == USE || GET_CODE (body) == CLOBBER)
	  return 0;

	else if (GET_CODE (body) == ASM_INPUT || asm_noperands (body) >= 0)
	  length = asm_insn_count (body) * fallback_fn (insn);
	else if (GET_CODE (body) == SEQUENCE)
	  for (i = 0; i < XVECLEN (body, 0); i++)
	    length += get_attr_length (XVECEXP (body, 0, i));
	else
	  length = fallback_fn (insn);
	break;

      default:
	break;
      }

#ifdef ADJUST_INSN_LENGTH
  ADJUST_INSN_LENGTH (insn, length);
#endif
  return length;
#else /* not HAVE_ATTR_length */
  return 0;
#define insn_default_length 0
#define insn_min_length 0
#endif /* not HAVE_ATTR_length */
}

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, get its maximum length.  */
int
get_attr_length (rtx insn)
{
  return get_attr_length_1 (insn, insn_default_length);
}

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, get its minimum length.  */
int
get_attr_min_length (rtx insn)
{
  return get_attr_length_1 (insn, insn_min_length);
}

/* Code to handle alignment inside shorten_branches.  */

/* Here is an explanation how the algorithm in align_fuzz can give
   proper results:

   Call a sequence of instructions beginning with alignment point X
   and continuing until the next alignment point `block X'.  When `X'
   is used in an expression, it means the alignment value of the
   alignment point.

   Call the distance between the start of the first insn of block X, and
   the end of the last insn of block X `IX', for the `inner size of X'.
   This is clearly the sum of the instruction lengths.

   Likewise with the next alignment-delimited block following X, which we
   shall call block Y.

   Call the distance between the start of the first insn of block X, and
   the start of the first insn of block Y `OX', for the `outer size of X'.

   The estimated padding is then OX - IX.

   OX can be safely estimated as

           if (X >= Y)
                   OX = round_up(IX, Y)
           else
                   OX = round_up(IX, X) + Y - X

   Clearly est(IX) >= real(IX), because that only depends on the
   instruction lengths, and those being overestimated is a given.

   Clearly round_up(foo, Z) >= round_up(bar, Z) if foo >= bar, so
   we needn't worry about that when thinking about OX.

   When X >= Y, the alignment provided by Y adds no uncertainty factor
   for branch ranges starting before X, so we can just round what we have.
   But when X < Y, we don't know anything about the, so to speak,
   `middle bits', so we have to assume the worst when aligning up from an
   address mod X to one mod Y, which is Y - X.  */

#ifndef LABEL_ALIGN
#define LABEL_ALIGN(LABEL) align_labels_log
#endif

#ifndef LABEL_ALIGN_MAX_SKIP
#define LABEL_ALIGN_MAX_SKIP align_labels_max_skip
#endif

#ifndef LOOP_ALIGN
#define LOOP_ALIGN(LABEL) align_loops_log
#endif

#ifndef LOOP_ALIGN_MAX_SKIP
#define LOOP_ALIGN_MAX_SKIP align_loops_max_skip
#endif

#ifndef LABEL_ALIGN_AFTER_BARRIER
#define LABEL_ALIGN_AFTER_BARRIER(LABEL) 0
#endif

#ifndef LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP
#define LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP 0
#endif

#ifndef JUMP_ALIGN
#define JUMP_ALIGN(LABEL) align_jumps_log
#endif

#ifndef JUMP_ALIGN_MAX_SKIP
#define JUMP_ALIGN_MAX_SKIP align_jumps_max_skip
#endif

#ifndef ADDR_VEC_ALIGN
static int
final_addr_vec_align (rtx addr_vec)
{
  int align = GET_MODE_SIZE (GET_MODE (PATTERN (addr_vec)));

  if (align > BIGGEST_ALIGNMENT / BITS_PER_UNIT)
    align = BIGGEST_ALIGNMENT / BITS_PER_UNIT;
  return exact_log2 (align);

}

#define ADDR_VEC_ALIGN(ADDR_VEC) final_addr_vec_align (ADDR_VEC)
#endif

#ifndef INSN_LENGTH_ALIGNMENT
#define INSN_LENGTH_ALIGNMENT(INSN) length_unit_log
#endif

#define INSN_SHUID(INSN) (uid_shuid[INSN_UID (INSN)])

static int min_labelno, max_labelno;

#define LABEL_TO_ALIGNMENT(LABEL) \
  (label_align[CODE_LABEL_NUMBER (LABEL) - min_labelno].alignment)

#define LABEL_TO_MAX_SKIP(LABEL) \
  (label_align[CODE_LABEL_NUMBER (LABEL) - min_labelno].max_skip)

/* For the benefit of port specific code do this also as a function.  */

int
label_to_alignment (rtx label)
{
  return LABEL_TO_ALIGNMENT (label);
}

#ifdef HAVE_ATTR_length
/* The differences in addresses
   between a branch and its target might grow or shrink depending on
   the alignment the start insn of the range (the branch for a forward
   branch or the label for a backward branch) starts out on; if these
   differences are used naively, they can even oscillate infinitely.
   We therefore want to compute a 'worst case' address difference that
   is independent of the alignment the start insn of the range end
   up on, and that is at least as large as the actual difference.
   The function align_fuzz calculates the amount we have to add to the
   naively computed difference, by traversing the part of the alignment
   chain of the start insn of the range that is in front of the end insn
   of the range, and considering for each alignment the maximum amount
   that it might contribute to a size increase.

   For casesi tables, we also want to know worst case minimum amounts of
   address difference, in case a machine description wants to introduce
   some common offset that is added to all offsets in a table.
   For this purpose, align_fuzz with a growth argument of 0 computes the
   appropriate adjustment.  */

/* Compute the maximum delta by which the difference of the addresses of
   START and END might grow / shrink due to a different address for start
   which changes the size of alignment insns between START and END.
   KNOWN_ALIGN_LOG is the alignment known for START.
   GROWTH should be ~0 if the objective is to compute potential code size
   increase, and 0 if the objective is to compute potential shrink.
   The return value is undefined for any other value of GROWTH.  */

static int
align_fuzz (rtx start, rtx end, int known_align_log, unsigned int growth)
{
  int uid = INSN_UID (start);
  rtx align_label;
  int known_align = 1 << known_align_log;
  int end_shuid = INSN_SHUID (end);
  int fuzz = 0;

  for (align_label = uid_align[uid]; align_label; align_label = uid_align[uid])
    {
      int align_addr, new_align;

      uid = INSN_UID (align_label);
      align_addr = INSN_ADDRESSES (uid) - insn_lengths[uid];
      if (uid_shuid[uid] > end_shuid)
	break;
      known_align_log = LABEL_TO_ALIGNMENT (align_label);
      new_align = 1 << known_align_log;
      if (new_align < known_align)
	continue;
      fuzz += (-align_addr ^ growth) & (new_align - known_align);
      known_align = new_align;
    }
  return fuzz;
}

/* Compute a worst-case reference address of a branch so that it
   can be safely used in the presence of aligned labels.  Since the
   size of the branch itself is unknown, the size of the branch is
   not included in the range.  I.e. for a forward branch, the reference
   address is the end address of the branch as known from the previous
   branch shortening pass, minus a value to account for possible size
   increase due to alignment.  For a backward branch, it is the start
   address of the branch as known from the current pass, plus a value
   to account for possible size increase due to alignment.
   NB.: Therefore, the maximum offset allowed for backward branches needs
   to exclude the branch size.  */

int
insn_current_reference_address (rtx branch)
{
  rtx dest, seq;
  int seq_uid;

  if (! INSN_ADDRESSES_SET_P ())
    return 0;

  seq = NEXT_INSN (PREV_INSN (branch));
  seq_uid = INSN_UID (seq);
  if (!JUMP_P (branch))
    /* This can happen for example on the PA; the objective is to know the
       offset to address something in front of the start of the function.
       Thus, we can treat it like a backward branch.
       We assume here that FUNCTION_BOUNDARY / BITS_PER_UNIT is larger than
       any alignment we'd encounter, so we skip the call to align_fuzz.  */
    return insn_current_address;
  dest = JUMP_LABEL (branch);

  /* BRANCH has no proper alignment chain set, so use SEQ.
     BRANCH also has no INSN_SHUID.  */
  if (INSN_SHUID (seq) < INSN_SHUID (dest))
    {
      /* Forward branch.  */
      return (insn_last_address + insn_lengths[seq_uid]
	      - align_fuzz (seq, dest, length_unit_log, ~0));
    }
  else
    {
      /* Backward branch.  */
      return (insn_current_address
	      + align_fuzz (dest, seq, length_unit_log, ~0));
    }
}
#endif /* HAVE_ATTR_length */

/* Compute branch alignments based on frequency information in the
   CFG.  */

static unsigned int
compute_alignments (void)
{
  int log, max_skip, max_log;
  basic_block bb;

  if (label_align)
    {
      free (label_align);
      label_align = 0;
    }

  max_labelno = max_label_num ();
  min_labelno = get_first_label_num ();
  label_align = XCNEWVEC (struct label_alignment, max_labelno - min_labelno + 1);

  /* If not optimizing or optimizing for size, don't assign any alignments.  */
  if (! optimize || optimize_size)
    return 0;

  FOR_EACH_BB (bb)
    {
      rtx label = BB_HEAD (bb);
      int fallthru_frequency = 0, branch_frequency = 0, has_fallthru = 0;
      edge e;
      edge_iterator ei;

      if (!LABEL_P (label)
	  || probably_never_executed_bb_p (bb))
	continue;
      max_log = LABEL_ALIGN (label);
      max_skip = LABEL_ALIGN_MAX_SKIP;

      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  if (e->flags & EDGE_FALLTHRU)
	    has_fallthru = 1, fallthru_frequency += EDGE_FREQUENCY (e);
	  else
	    branch_frequency += EDGE_FREQUENCY (e);
	}

      /* There are two purposes to align block with no fallthru incoming edge:
	 1) to avoid fetch stalls when branch destination is near cache boundary
	 2) to improve cache efficiency in case the previous block is not executed
	    (so it does not need to be in the cache).

	 We to catch first case, we align frequently executed blocks.
	 To catch the second, we align blocks that are executed more frequently
	 than the predecessor and the predecessor is likely to not be executed
	 when function is called.  */

      if (!has_fallthru
	  && (branch_frequency > BB_FREQ_MAX / 10
	      || (bb->frequency > bb->prev_bb->frequency * 10
		  && (bb->prev_bb->frequency
		      <= ENTRY_BLOCK_PTR->frequency / 2))))
	{
	  log = JUMP_ALIGN (label);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = JUMP_ALIGN_MAX_SKIP;
	    }
	}
      /* In case block is frequent and reached mostly by non-fallthru edge,
	 align it.  It is most likely a first block of loop.  */
      if (has_fallthru
	  && maybe_hot_bb_p (bb)
	  && branch_frequency + fallthru_frequency > BB_FREQ_MAX / 10
	  && branch_frequency > fallthru_frequency * 2)
	{
	  log = LOOP_ALIGN (label);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LOOP_ALIGN_MAX_SKIP;
	    }
	}
      LABEL_TO_ALIGNMENT (label) = max_log;
      LABEL_TO_MAX_SKIP (label) = max_skip;
    }
  return 0;
}

struct tree_opt_pass pass_compute_alignments =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  compute_alignments,                   /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};


/* Make a pass over all insns and compute their actual lengths by shortening
   any branches of variable length if possible.  */

/* shorten_branches might be called multiple times:  for example, the SH
   port splits out-of-range conditional branches in MACHINE_DEPENDENT_REORG.
   In order to do this, it needs proper length information, which it obtains
   by calling shorten_branches.  This cannot be collapsed with
   shorten_branches itself into a single pass unless we also want to integrate
   reorg.c, since the branch splitting exposes new instructions with delay
   slots.  */

void
shorten_branches (rtx first ATTRIBUTE_UNUSED)
{
  rtx insn;
  int max_uid;
  int i;
  int max_log;
  int max_skip;
#ifdef HAVE_ATTR_length
#define MAX_CODE_ALIGN 16
  rtx seq;
  int something_changed = 1;
  char *varying_length;
  rtx body;
  int uid;
  rtx align_tab[MAX_CODE_ALIGN];

#endif

  /* Compute maximum UID and allocate label_align / uid_shuid.  */
  max_uid = get_max_uid ();

  /* Free uid_shuid before reallocating it.  */
  free (uid_shuid);

  uid_shuid = XNEWVEC (int, max_uid);

  if (max_labelno != max_label_num ())
    {
      int old = max_labelno;
      int n_labels;
      int n_old_labels;

      max_labelno = max_label_num ();

      n_labels = max_labelno - min_labelno + 1;
      n_old_labels = old - min_labelno + 1;

      label_align = xrealloc (label_align,
			      n_labels * sizeof (struct label_alignment));

      /* Range of labels grows monotonically in the function.  Failing here
         means that the initialization of array got lost.  */
      gcc_assert (n_old_labels <= n_labels);

      memset (label_align + n_old_labels, 0,
	      (n_labels - n_old_labels) * sizeof (struct label_alignment));
    }

  /* Initialize label_align and set up uid_shuid to be strictly
     monotonically rising with insn order.  */
  /* We use max_log here to keep track of the maximum alignment we want to
     impose on the next CODE_LABEL (or the current one if we are processing
     the CODE_LABEL itself).  */

  max_log = 0;
  max_skip = 0;

  for (insn = get_insns (), i = 1; insn; insn = NEXT_INSN (insn))
    {
      int log;

      INSN_SHUID (insn) = i++;
      if (INSN_P (insn))
	continue;

      if (LABEL_P (insn))
	{
	  rtx next;

	  /* Merge in alignments computed by compute_alignments.  */
	  log = LABEL_TO_ALIGNMENT (insn);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LABEL_TO_MAX_SKIP (insn);
	    }

	  log = LABEL_ALIGN (insn);
	  if (max_log < log)
	    {
	      max_log = log;
	      max_skip = LABEL_ALIGN_MAX_SKIP;
	    }
	  next = next_nonnote_insn (insn);
	  /* ADDR_VECs only take room if read-only data goes into the text
	     section.  */
	  if (JUMP_TABLES_IN_TEXT_SECTION
	      || readonly_data_section == text_section)
	    if (next && JUMP_P (next))
	      {
		rtx nextbody = PATTERN (next);
		if (GET_CODE (nextbody) == ADDR_VEC
		    || GET_CODE (nextbody) == ADDR_DIFF_VEC)
		  {
		    log = ADDR_VEC_ALIGN (next);
		    if (max_log < log)
		      {
			max_log = log;
			max_skip = LABEL_ALIGN_MAX_SKIP;
		      }
		  }
	      }
	  LABEL_TO_ALIGNMENT (insn) = max_log;
	  LABEL_TO_MAX_SKIP (insn) = max_skip;
	  max_log = 0;
	  max_skip = 0;
	}
      else if (BARRIER_P (insn))
	{
	  rtx label;

	  for (label = insn; label && ! INSN_P (label);
	       label = NEXT_INSN (label))
	    if (LABEL_P (label))
	      {
		log = LABEL_ALIGN_AFTER_BARRIER (insn);
		if (max_log < log)
		  {
		    max_log = log;
		    max_skip = LABEL_ALIGN_AFTER_BARRIER_MAX_SKIP;
		  }
		break;
	      }
	}
    }
#ifdef HAVE_ATTR_length

  /* Allocate the rest of the arrays.  */
  insn_lengths = XNEWVEC (int, max_uid);
  insn_lengths_max_uid = max_uid;
  /* Syntax errors can lead to labels being outside of the main insn stream.
     Initialize insn_addresses, so that we get reproducible results.  */
  INSN_ADDRESSES_ALLOC (max_uid);

  varying_length = XCNEWVEC (char, max_uid);

  /* Initialize uid_align.  We scan instructions
     from end to start, and keep in align_tab[n] the last seen insn
     that does an alignment of at least n+1, i.e. the successor
     in the alignment chain for an insn that does / has a known
     alignment of n.  */
  uid_align = XCNEWVEC (rtx, max_uid);

  for (i = MAX_CODE_ALIGN; --i >= 0;)
    align_tab[i] = NULL_RTX;
  seq = get_last_insn ();
  for (; seq; seq = PREV_INSN (seq))
    {
      int uid = INSN_UID (seq);
      int log;
      log = (LABEL_P (seq) ? LABEL_TO_ALIGNMENT (seq) : 0);
      uid_align[uid] = align_tab[0];
      if (log)
	{
	  /* Found an alignment label.  */
	  uid_align[uid] = align_tab[log];
	  for (i = log - 1; i >= 0; i--)
	    align_tab[i] = seq;
	}
    }
#ifdef CASE_VECTOR_SHORTEN_MODE
  if (optimize)
    {
      /* Look for ADDR_DIFF_VECs, and initialize their minimum and maximum
         label fields.  */

      int min_shuid = INSN_SHUID (get_insns ()) - 1;
      int max_shuid = INSN_SHUID (get_last_insn ()) + 1;
      int rel;

      for (insn = first; insn != 0; insn = NEXT_INSN (insn))
	{
	  rtx min_lab = NULL_RTX, max_lab = NULL_RTX, pat;
	  int len, i, min, max, insn_shuid;
	  int min_align;
	  addr_diff_vec_flags flags;

	  if (!JUMP_P (insn)
	      || GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC)
	    continue;
	  pat = PATTERN (insn);
	  len = XVECLEN (pat, 1);
	  gcc_assert (len > 0);
	  min_align = MAX_CODE_ALIGN;
	  for (min = max_shuid, max = min_shuid, i = len - 1; i >= 0; i--)
	    {
	      rtx lab = XEXP (XVECEXP (pat, 1, i), 0);
	      int shuid = INSN_SHUID (lab);
	      if (shuid < min)
		{
		  min = shuid;
		  min_lab = lab;
		}
	      if (shuid > max)
		{
		  max = shuid;
		  max_lab = lab;
		}
	      if (min_align > LABEL_TO_ALIGNMENT (lab))
		min_align = LABEL_TO_ALIGNMENT (lab);
	    }
	  XEXP (pat, 2) = gen_rtx_LABEL_REF (Pmode, min_lab);
	  XEXP (pat, 3) = gen_rtx_LABEL_REF (Pmode, max_lab);
	  insn_shuid = INSN_SHUID (insn);
	  rel = INSN_SHUID (XEXP (XEXP (pat, 0), 0));
	  memset (&flags, 0, sizeof (flags));
	  flags.min_align = min_align;
	  flags.base_after_vec = rel > insn_shuid;
	  flags.min_after_vec  = min > insn_shuid;
	  flags.max_after_vec  = max > insn_shuid;
	  flags.min_after_base = min > rel;
	  flags.max_after_base = max > rel;
	  ADDR_DIFF_VEC_FLAGS (pat) = flags;
	}
    }
#endif /* CASE_VECTOR_SHORTEN_MODE */

  /* Compute initial lengths, addresses, and varying flags for each insn.  */
  for (insn_current_address = 0, insn = first;
       insn != 0;
       insn_current_address += insn_lengths[uid], insn = NEXT_INSN (insn))
    {
      uid = INSN_UID (insn);

      insn_lengths[uid] = 0;

      if (LABEL_P (insn))
	{
	  int log = LABEL_TO_ALIGNMENT (insn);
	  if (log)
	    {
	      int align = 1 << log;
	      int new_address = (insn_current_address + align - 1) & -align;
	      insn_lengths[uid] = new_address - insn_current_address;
	    }
	}

      INSN_ADDRESSES (uid) = insn_current_address + insn_lengths[uid];

      if (NOTE_P (insn) || BARRIER_P (insn)
	  || LABEL_P (insn))
	continue;
      if (INSN_DELETED_P (insn))
	continue;

      body = PATTERN (insn);
      if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	{
	  /* This only takes room if read-only data goes into the text
	     section.  */
	  if (JUMP_TABLES_IN_TEXT_SECTION
	      || readonly_data_section == text_section)
	    insn_lengths[uid] = (XVECLEN (body,
					  GET_CODE (body) == ADDR_DIFF_VEC)
				 * GET_MODE_SIZE (GET_MODE (body)));
	  /* Alignment is handled by ADDR_VEC_ALIGN.  */
	}
      else if (GET_CODE (body) == ASM_INPUT || asm_noperands (body) >= 0)
	insn_lengths[uid] = asm_insn_count (body) * insn_default_length (insn);
      else if (GET_CODE (body) == SEQUENCE)
	{
	  int i;
	  int const_delay_slots;
#ifdef DELAY_SLOTS
	  const_delay_slots = const_num_delay_slots (XVECEXP (body, 0, 0));
#else
	  const_delay_slots = 0;
#endif
	  /* Inside a delay slot sequence, we do not do any branch shortening
	     if the shortening could change the number of delay slots
	     of the branch.  */
	  for (i = 0; i < XVECLEN (body, 0); i++)
	    {
	      rtx inner_insn = XVECEXP (body, 0, i);
	      int inner_uid = INSN_UID (inner_insn);
	      int inner_length;

	      if (GET_CODE (body) == ASM_INPUT
		  || asm_noperands (PATTERN (XVECEXP (body, 0, i))) >= 0)
		inner_length = (asm_insn_count (PATTERN (inner_insn))
				* insn_default_length (inner_insn));
	      else
		inner_length = insn_default_length (inner_insn);

	      insn_lengths[inner_uid] = inner_length;
	      if (const_delay_slots)
		{
		  if ((varying_length[inner_uid]
		       = insn_variable_length_p (inner_insn)) != 0)
		    varying_length[uid] = 1;
		  INSN_ADDRESSES (inner_uid) = (insn_current_address
						+ insn_lengths[uid]);
		}
	      else
		varying_length[inner_uid] = 0;
	      insn_lengths[uid] += inner_length;
	    }
	}
      else if (GET_CODE (body) != USE && GET_CODE (body) != CLOBBER)
	{
	  insn_lengths[uid] = insn_default_length (insn);
	  varying_length[uid] = insn_variable_length_p (insn);
	}

      /* If needed, do any adjustment.  */
#ifdef ADJUST_INSN_LENGTH
      ADJUST_INSN_LENGTH (insn, insn_lengths[uid]);
      if (insn_lengths[uid] < 0)
	fatal_insn ("negative insn length", insn);
#endif
    }

  /* Now loop over all the insns finding varying length insns.  For each,
     get the current insn length.  If it has changed, reflect the change.
     When nothing changes for a full pass, we are done.  */

  while (something_changed)
    {
      something_changed = 0;
      insn_current_align = MAX_CODE_ALIGN - 1;
      for (insn_current_address = 0, insn = first;
	   insn != 0;
	   insn = NEXT_INSN (insn))
	{
	  int new_length;
#ifdef ADJUST_INSN_LENGTH
	  int tmp_length;
#endif
	  int length_align;

	  uid = INSN_UID (insn);

	  if (LABEL_P (insn))
	    {
	      int log = LABEL_TO_ALIGNMENT (insn);
	      if (log > insn_current_align)
		{
		  int align = 1 << log;
		  int new_address= (insn_current_address + align - 1) & -align;
		  insn_lengths[uid] = new_address - insn_current_address;
		  insn_current_align = log;
		  insn_current_address = new_address;
		}
	      else
		insn_lengths[uid] = 0;
	      INSN_ADDRESSES (uid) = insn_current_address;
	      continue;
	    }

	  length_align = INSN_LENGTH_ALIGNMENT (insn);
	  if (length_align < insn_current_align)
	    insn_current_align = length_align;

	  insn_last_address = INSN_ADDRESSES (uid);
	  INSN_ADDRESSES (uid) = insn_current_address;

#ifdef CASE_VECTOR_SHORTEN_MODE
	  if (optimize && JUMP_P (insn)
	      && GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
	    {
	      rtx body = PATTERN (insn);
	      int old_length = insn_lengths[uid];
	      rtx rel_lab = XEXP (XEXP (body, 0), 0);
	      rtx min_lab = XEXP (XEXP (body, 2), 0);
	      rtx max_lab = XEXP (XEXP (body, 3), 0);
	      int rel_addr = INSN_ADDRESSES (INSN_UID (rel_lab));
	      int min_addr = INSN_ADDRESSES (INSN_UID (min_lab));
	      int max_addr = INSN_ADDRESSES (INSN_UID (max_lab));
	      rtx prev;
	      int rel_align = 0;
	      addr_diff_vec_flags flags;

	      /* Avoid automatic aggregate initialization.  */
	      flags = ADDR_DIFF_VEC_FLAGS (body);

	      /* Try to find a known alignment for rel_lab.  */
	      for (prev = rel_lab;
		   prev
		   && ! insn_lengths[INSN_UID (prev)]
		   && ! (varying_length[INSN_UID (prev)] & 1);
		   prev = PREV_INSN (prev))
		if (varying_length[INSN_UID (prev)] & 2)
		  {
		    rel_align = LABEL_TO_ALIGNMENT (prev);
		    break;
		  }

	      /* See the comment on addr_diff_vec_flags in rtl.h for the
		 meaning of the flags values.  base: REL_LAB   vec: INSN  */
	      /* Anything after INSN has still addresses from the last
		 pass; adjust these so that they reflect our current
		 estimate for this pass.  */
	      if (flags.base_after_vec)
		rel_addr += insn_current_address - insn_last_address;
	      if (flags.min_after_vec)
		min_addr += insn_current_address - insn_last_address;
	      if (flags.max_after_vec)
		max_addr += insn_current_address - insn_last_address;
	      /* We want to know the worst case, i.e. lowest possible value
		 for the offset of MIN_LAB.  If MIN_LAB is after REL_LAB,
		 its offset is positive, and we have to be wary of code shrink;
		 otherwise, it is negative, and we have to be vary of code
		 size increase.  */
	      if (flags.min_after_base)
		{
		  /* If INSN is between REL_LAB and MIN_LAB, the size
		     changes we are about to make can change the alignment
		     within the observed offset, therefore we have to break
		     it up into two parts that are independent.  */
		  if (! flags.base_after_vec && flags.min_after_vec)
		    {
		      min_addr -= align_fuzz (rel_lab, insn, rel_align, 0);
		      min_addr -= align_fuzz (insn, min_lab, 0, 0);
		    }
		  else
		    min_addr -= align_fuzz (rel_lab, min_lab, rel_align, 0);
		}
	      else
		{
		  if (flags.base_after_vec && ! flags.min_after_vec)
		    {
		      min_addr -= align_fuzz (min_lab, insn, 0, ~0);
		      min_addr -= align_fuzz (insn, rel_lab, 0, ~0);
		    }
		  else
		    min_addr -= align_fuzz (min_lab, rel_lab, 0, ~0);
		}
	      /* Likewise, determine the highest lowest possible value
		 for the offset of MAX_LAB.  */
	      if (flags.max_after_base)
		{
		  if (! flags.base_after_vec && flags.max_after_vec)
		    {
		      max_addr += align_fuzz (rel_lab, insn, rel_align, ~0);
		      max_addr += align_fuzz (insn, max_lab, 0, ~0);
		    }
		  else
		    max_addr += align_fuzz (rel_lab, max_lab, rel_align, ~0);
		}
	      else
		{
		  if (flags.base_after_vec && ! flags.max_after_vec)
		    {
		      max_addr += align_fuzz (max_lab, insn, 0, 0);
		      max_addr += align_fuzz (insn, rel_lab, 0, 0);
		    }
		  else
		    max_addr += align_fuzz (max_lab, rel_lab, 0, 0);
		}
	      PUT_MODE (body, CASE_VECTOR_SHORTEN_MODE (min_addr - rel_addr,
							max_addr - rel_addr,
							body));
	      if (JUMP_TABLES_IN_TEXT_SECTION
		  || readonly_data_section == text_section)
		{
		  insn_lengths[uid]
		    = (XVECLEN (body, 1) * GET_MODE_SIZE (GET_MODE (body)));
		  insn_current_address += insn_lengths[uid];
		  if (insn_lengths[uid] != old_length)
		    something_changed = 1;
		}

	      continue;
	    }
#endif /* CASE_VECTOR_SHORTEN_MODE */

	  if (! (varying_length[uid]))
	    {
	      if (NONJUMP_INSN_P (insn)
		  && GET_CODE (PATTERN (insn)) == SEQUENCE)
		{
		  int i;

		  body = PATTERN (insn);
		  for (i = 0; i < XVECLEN (body, 0); i++)
		    {
		      rtx inner_insn = XVECEXP (body, 0, i);
		      int inner_uid = INSN_UID (inner_insn);

		      INSN_ADDRESSES (inner_uid) = insn_current_address;

		      insn_current_address += insn_lengths[inner_uid];
		    }
		}
	      else
		insn_current_address += insn_lengths[uid];

	      continue;
	    }

	  if (NONJUMP_INSN_P (insn) && GET_CODE (PATTERN (insn)) == SEQUENCE)
	    {
	      int i;

	      body = PATTERN (insn);
	      new_length = 0;
	      for (i = 0; i < XVECLEN (body, 0); i++)
		{
		  rtx inner_insn = XVECEXP (body, 0, i);
		  int inner_uid = INSN_UID (inner_insn);
		  int inner_length;

		  INSN_ADDRESSES (inner_uid) = insn_current_address;

		  /* insn_current_length returns 0 for insns with a
		     non-varying length.  */
		  if (! varying_length[inner_uid])
		    inner_length = insn_lengths[inner_uid];
		  else
		    inner_length = insn_current_length (inner_insn);

		  if (inner_length != insn_lengths[inner_uid])
		    {
		      insn_lengths[inner_uid] = inner_length;
		      something_changed = 1;
		    }
		  insn_current_address += insn_lengths[inner_uid];
		  new_length += inner_length;
		}
	    }
	  else
	    {
	      new_length = insn_current_length (insn);
	      insn_current_address += new_length;
	    }

#ifdef ADJUST_INSN_LENGTH
	  /* If needed, do any adjustment.  */
	  tmp_length = new_length;
	  ADJUST_INSN_LENGTH (insn, new_length);
	  insn_current_address += (new_length - tmp_length);
#endif

	  if (new_length != insn_lengths[uid])
	    {
	      insn_lengths[uid] = new_length;
	      something_changed = 1;
	    }
	}
      /* For a non-optimizing compile, do only a single pass.  */
      if (!optimize)
	break;
    }

  free (varying_length);

#endif /* HAVE_ATTR_length */
}

#ifdef HAVE_ATTR_length
/* Given the body of an INSN known to be generated by an ASM statement, return
   the number of machine instructions likely to be generated for this insn.
   This is used to compute its length.  */

static int
asm_insn_count (rtx body)
{
  const char *template;
  int count = 1;

  if (GET_CODE (body) == ASM_INPUT)
    template = XSTR (body, 0);
  else
    template = decode_asm_operands (body, NULL, NULL, NULL, NULL);

  for (; *template; template++)
    if (IS_ASM_LOGICAL_LINE_SEPARATOR (*template) || *template == '\n')
      count++;

  return count;
}
#endif

/* Output assembler code for the start of a function,
   and initialize some of the variables in this file
   for the new function.  The label for the function and associated
   assembler pseudo-ops have already been output in `assemble_start_function'.

   FIRST is the first insn of the rtl for the function being compiled.
   FILE is the file to write assembler code to.
   OPTIMIZE is nonzero if we should eliminate redundant
     test and compare insns.  */

void
final_start_function (rtx first ATTRIBUTE_UNUSED, FILE *file,
		      int optimize ATTRIBUTE_UNUSED)
{
  block_depth = 0;

  this_is_asm_operands = 0;

  last_filename = locator_file (prologue_locator);
  last_linenum = locator_line (prologue_locator);

  high_block_linenum = high_function_linenum = last_linenum;

  (*debug_hooks->begin_prologue) (last_linenum, last_filename);

#if defined (DWARF2_UNWIND_INFO) || defined (TARGET_UNWIND_INFO)
  if (write_symbols != DWARF2_DEBUG && write_symbols != VMS_AND_DWARF2_DEBUG)
    dwarf2out_begin_prologue (0, NULL);
#endif

#ifdef LEAF_REG_REMAP
  if (current_function_uses_only_leaf_regs)
    leaf_renumber_regs (first);
#endif

  /* The Sun386i and perhaps other machines don't work right
     if the profiling code comes after the prologue.  */
#ifdef PROFILE_BEFORE_PROLOGUE
  if (current_function_profile)
    profile_function (file);
#endif /* PROFILE_BEFORE_PROLOGUE */

#if defined (DWARF2_UNWIND_INFO) && defined (HAVE_prologue)
  if (dwarf2out_do_frame ())
    dwarf2out_frame_debug (NULL_RTX, false);
#endif

  /* If debugging, assign block numbers to all of the blocks in this
     function.  */
  if (write_symbols)
    {
      reemit_insn_block_notes ();
      number_blocks (current_function_decl);
      /* We never actually put out begin/end notes for the top-level
	 block in the function.  But, conceptually, that block is
	 always needed.  */
      TREE_ASM_WRITTEN (DECL_INITIAL (current_function_decl)) = 1;
    }

  /* First output the function prologue: code to set up the stack frame.  */
  targetm.asm_out.function_prologue (file, get_frame_size ());

  /* If the machine represents the prologue as RTL, the profiling code must
     be emitted when NOTE_INSN_PROLOGUE_END is scanned.  */
#ifdef HAVE_prologue
  if (! HAVE_prologue)
#endif
    profile_after_prologue (file);
}

static void
profile_after_prologue (FILE *file ATTRIBUTE_UNUSED)
{
#ifndef PROFILE_BEFORE_PROLOGUE
  if (current_function_profile)
    profile_function (file);
#endif /* not PROFILE_BEFORE_PROLOGUE */
}

static void
profile_function (FILE *file ATTRIBUTE_UNUSED)
{
#ifndef NO_PROFILE_COUNTERS
# define NO_PROFILE_COUNTERS	0
#endif
#if defined(ASM_OUTPUT_REG_PUSH)
  int sval = current_function_returns_struct;
  rtx svrtx = targetm.calls.struct_value_rtx (TREE_TYPE (current_function_decl), 1);
#if defined(STATIC_CHAIN_INCOMING_REGNUM) || defined(STATIC_CHAIN_REGNUM)
  int cxt = cfun->static_chain_decl != NULL;
#endif
#endif /* ASM_OUTPUT_REG_PUSH */

  if (! NO_PROFILE_COUNTERS)
    {
      int align = MIN (BIGGEST_ALIGNMENT, LONG_TYPE_SIZE);
      switch_to_section (data_section);
      ASM_OUTPUT_ALIGN (file, floor_log2 (align / BITS_PER_UNIT));
      targetm.asm_out.internal_label (file, "LP", current_function_funcdef_no);
      assemble_integer (const0_rtx, LONG_TYPE_SIZE / BITS_PER_UNIT, align, 1);
    }

  switch_to_section (current_function_section ());

#if defined(ASM_OUTPUT_REG_PUSH)
  if (sval && svrtx != NULL_RTX && REG_P (svrtx))
    ASM_OUTPUT_REG_PUSH (file, REGNO (svrtx));
#endif

#if defined(STATIC_CHAIN_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    ASM_OUTPUT_REG_PUSH (file, STATIC_CHAIN_INCOMING_REGNUM);
#else
#if defined(STATIC_CHAIN_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    {
      ASM_OUTPUT_REG_PUSH (file, STATIC_CHAIN_REGNUM);
    }
#endif
#endif

  FUNCTION_PROFILER (file, current_function_funcdef_no);

#if defined(STATIC_CHAIN_INCOMING_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    ASM_OUTPUT_REG_POP (file, STATIC_CHAIN_INCOMING_REGNUM);
#else
#if defined(STATIC_CHAIN_REGNUM) && defined(ASM_OUTPUT_REG_PUSH)
  if (cxt)
    {
      ASM_OUTPUT_REG_POP (file, STATIC_CHAIN_REGNUM);
    }
#endif
#endif

#if defined(ASM_OUTPUT_REG_PUSH)
  if (sval && svrtx != NULL_RTX && REG_P (svrtx))
    ASM_OUTPUT_REG_POP (file, REGNO (svrtx));
#endif
}

/* Output assembler code for the end of a function.
   For clarity, args are same as those of `final_start_function'
   even though not all of them are needed.  */

void
final_end_function (void)
{
  app_disable ();

  (*debug_hooks->end_function) (high_function_linenum);

  /* Finally, output the function epilogue:
     code to restore the stack frame and return to the caller.  */
  targetm.asm_out.function_epilogue (asm_out_file, get_frame_size ());

  /* And debug output.  */
  (*debug_hooks->end_epilogue) (last_linenum, last_filename);

#if defined (DWARF2_UNWIND_INFO)
  if (write_symbols != DWARF2_DEBUG && write_symbols != VMS_AND_DWARF2_DEBUG
      && dwarf2out_do_frame ())
    dwarf2out_end_epilogue (last_linenum, last_filename);
#endif
}

/* Output assembler code for some insns: all or part of a function.
   For description of args, see `final_start_function', above.  */

void
final (rtx first, FILE *file, int optimize)
{
  rtx insn;
  int max_uid = 0;
  int seen = 0;

  last_ignored_compare = 0;

#ifdef SDB_DEBUGGING_INFO
  /* When producing SDB debugging info, delete troublesome line number
     notes from inlined functions in other files as well as duplicate
     line number notes.  */
  if (write_symbols == SDB_DEBUG)
    {
      rtx last = 0;
      for (insn = first; insn; insn = NEXT_INSN (insn))
	if (NOTE_P (insn) && NOTE_LINE_NUMBER (insn) > 0)
	  {
	    if (last != 0
#ifdef USE_MAPPED_LOCATION
		&& NOTE_SOURCE_LOCATION (insn) == NOTE_SOURCE_LOCATION (last)
#else
		&& NOTE_LINE_NUMBER (insn) == NOTE_LINE_NUMBER (last)
		&& NOTE_SOURCE_FILE (insn) == NOTE_SOURCE_FILE (last)
#endif
	      )
	      {
		delete_insn (insn);	/* Use delete_note.  */
		continue;
	      }
	    last = insn;
	  }
    }
#endif

  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      if (INSN_UID (insn) > max_uid)       /* Find largest UID.  */
	max_uid = INSN_UID (insn);
#ifdef HAVE_cc0
      /* If CC tracking across branches is enabled, record the insn which
	 jumps to each branch only reached from one place.  */
      if (optimize && JUMP_P (insn))
	{
	  rtx lab = JUMP_LABEL (insn);
	  if (lab && LABEL_NUSES (lab) == 1)
	    {
	      LABEL_REFS (lab) = insn;
	    }
	}
#endif
    }

  init_recog ();

  CC_STATUS_INIT;

  /* Output the insns.  */
  for (insn = NEXT_INSN (first); insn;)
    {
#ifdef HAVE_ATTR_length
      if ((unsigned) INSN_UID (insn) >= INSN_ADDRESSES_SIZE ())
	{
	  /* This can be triggered by bugs elsewhere in the compiler if
	     new insns are created after init_insn_lengths is called.  */
	  gcc_assert (NOTE_P (insn));
	  insn_current_address = -1;
	}
      else
	insn_current_address = INSN_ADDRESSES (INSN_UID (insn));
#endif /* HAVE_ATTR_length */

      insn = final_scan_insn (insn, file, optimize, 0, &seen);
    }
}

const char *
get_insn_template (int code, rtx insn)
{
  switch (insn_data[code].output_format)
    {
    case INSN_OUTPUT_FORMAT_SINGLE:
      return insn_data[code].output.single;
    case INSN_OUTPUT_FORMAT_MULTI:
      return insn_data[code].output.multi[which_alternative];
    case INSN_OUTPUT_FORMAT_FUNCTION:
      gcc_assert (insn);
      return (*insn_data[code].output.function) (recog_data.operand, insn);

    default:
      gcc_unreachable ();
    }
}

/* Emit the appropriate declaration for an alternate-entry-point
   symbol represented by INSN, to FILE.  INSN is a CODE_LABEL with
   LABEL_KIND != LABEL_NORMAL.

   The case fall-through in this function is intentional.  */
static void
output_alternate_entry_point (FILE *file, rtx insn)
{
  const char *name = LABEL_NAME (insn);

  switch (LABEL_KIND (insn))
    {
    case LABEL_WEAK_ENTRY:
#ifdef ASM_WEAKEN_LABEL
      ASM_WEAKEN_LABEL (file, name);
#endif
    case LABEL_GLOBAL_ENTRY:
      targetm.asm_out.globalize_label (file, name);
    case LABEL_STATIC_ENTRY:
#ifdef ASM_OUTPUT_TYPE_DIRECTIVE
      ASM_OUTPUT_TYPE_DIRECTIVE (file, name, "function");
#endif
      ASM_OUTPUT_LABEL (file, name);
      break;

    case LABEL_NORMAL:
    default:
      gcc_unreachable ();
    }
}

/* The final scan for one insn, INSN.
   Args are same as in `final', except that INSN
   is the insn being scanned.
   Value returned is the next insn to be scanned.

   NOPEEPHOLES is the flag to disallow peephole processing (currently
   used for within delayed branch sequence output).

   SEEN is used to track the end of the prologue, for emitting
   debug information.  We force the emission of a line note after
   both NOTE_INSN_PROLOGUE_END and NOTE_INSN_FUNCTION_BEG, or
   at the beginning of the second basic block, whichever comes
   first.  */

rtx
final_scan_insn (rtx insn, FILE *file, int optimize ATTRIBUTE_UNUSED,
		 int nopeepholes ATTRIBUTE_UNUSED, int *seen)
{
#ifdef HAVE_cc0
  rtx set;
#endif
  rtx next;

  insn_counter++;

  /* Ignore deleted insns.  These can occur when we split insns (due to a
     template of "#") while not optimizing.  */
  if (INSN_DELETED_P (insn))
    return NEXT_INSN (insn);

  switch (GET_CODE (insn))
    {
    case NOTE:
      switch (NOTE_LINE_NUMBER (insn))
	{
	case NOTE_INSN_DELETED:
	case NOTE_INSN_FUNCTION_END:
	case NOTE_INSN_REPEATED_LINE_NUMBER:
	case NOTE_INSN_EXPECTED_VALUE:
	  break;

	case NOTE_INSN_SWITCH_TEXT_SECTIONS:
	  in_cold_section_p = !in_cold_section_p;
	  (*debug_hooks->switch_text_section) ();
	  switch_to_section (current_function_section ());
	  break;

	case NOTE_INSN_BASIC_BLOCK:
#ifdef TARGET_UNWIND_INFO
	  targetm.asm_out.unwind_emit (asm_out_file, insn);
#endif

	  if (flag_debug_asm)
	    fprintf (asm_out_file, "\t%s basic block %d\n",
		     ASM_COMMENT_START, NOTE_BASIC_BLOCK (insn)->index);

	  if ((*seen & (SEEN_EMITTED | SEEN_BB)) == SEEN_BB)
	    {
	      *seen |= SEEN_EMITTED;
	      force_source_line = true;
	    }
	  else
	    *seen |= SEEN_BB;

	  break;

	case NOTE_INSN_EH_REGION_BEG:
	  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "LEHB",
				  NOTE_EH_HANDLER (insn));
	  break;

	case NOTE_INSN_EH_REGION_END:
	  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "LEHE",
				  NOTE_EH_HANDLER (insn));
	  break;

	case NOTE_INSN_PROLOGUE_END:
	  targetm.asm_out.function_end_prologue (file);
	  profile_after_prologue (file);

	  if ((*seen & (SEEN_EMITTED | SEEN_NOTE)) == SEEN_NOTE)
	    {
	      *seen |= SEEN_EMITTED;
	      force_source_line = true;
	    }
	  else
	    *seen |= SEEN_NOTE;

	  break;

	case NOTE_INSN_EPILOGUE_BEG:
	  targetm.asm_out.function_begin_epilogue (file);
	  break;

	case NOTE_INSN_FUNCTION_BEG:
	  app_disable ();
	  (*debug_hooks->end_prologue) (last_linenum, last_filename);

	  if ((*seen & (SEEN_EMITTED | SEEN_NOTE)) == SEEN_NOTE)
	    {
	      *seen |= SEEN_EMITTED;
	      force_source_line = true;
	    }
	  else
	    *seen |= SEEN_NOTE;

	  break;

	case NOTE_INSN_BLOCK_BEG:
	  if (debug_info_level == DINFO_LEVEL_NORMAL
	      || debug_info_level == DINFO_LEVEL_VERBOSE
	      || write_symbols == DWARF2_DEBUG
	      || write_symbols == VMS_AND_DWARF2_DEBUG
	      || write_symbols == VMS_DEBUG)
	    {
	      int n = BLOCK_NUMBER (NOTE_BLOCK (insn));

	      app_disable ();
	      ++block_depth;
	      high_block_linenum = last_linenum;

	      /* Output debugging info about the symbol-block beginning.  */
	      (*debug_hooks->begin_block) (last_linenum, n);

	      /* Mark this block as output.  */
	      TREE_ASM_WRITTEN (NOTE_BLOCK (insn)) = 1;
	    }
	  break;

	case NOTE_INSN_BLOCK_END:
	  if (debug_info_level == DINFO_LEVEL_NORMAL
	      || debug_info_level == DINFO_LEVEL_VERBOSE
	      || write_symbols == DWARF2_DEBUG
	      || write_symbols == VMS_AND_DWARF2_DEBUG
	      || write_symbols == VMS_DEBUG)
	    {
	      int n = BLOCK_NUMBER (NOTE_BLOCK (insn));

	      app_disable ();

	      /* End of a symbol-block.  */
	      --block_depth;
	      gcc_assert (block_depth >= 0);

	      (*debug_hooks->end_block) (high_block_linenum, n);
	    }
	  break;

	case NOTE_INSN_DELETED_LABEL:
	  /* Emit the label.  We may have deleted the CODE_LABEL because
	     the label could be proved to be unreachable, though still
	     referenced (in the form of having its address taken.  */
	  ASM_OUTPUT_DEBUG_LABEL (file, "L", CODE_LABEL_NUMBER (insn));
	  break;

	case NOTE_INSN_VAR_LOCATION:
	  (*debug_hooks->var_location) (insn);
	  break;

	case 0:
	  break;

	default:
	  gcc_assert (NOTE_LINE_NUMBER (insn) > 0);
	  break;
	}
      break;

    case BARRIER:
#if defined (DWARF2_UNWIND_INFO)
      if (dwarf2out_do_frame ())
	dwarf2out_frame_debug (insn, false);
#endif
      break;

    case CODE_LABEL:
      /* The target port might emit labels in the output function for
	 some insn, e.g. sh.c output_branchy_insn.  */
      if (CODE_LABEL_NUMBER (insn) <= max_labelno)
	{
	  int align = LABEL_TO_ALIGNMENT (insn);
#ifdef ASM_OUTPUT_MAX_SKIP_ALIGN
	  int max_skip = LABEL_TO_MAX_SKIP (insn);
#endif

	  if (align && NEXT_INSN (insn))
	    {
#ifdef ASM_OUTPUT_MAX_SKIP_ALIGN
	      ASM_OUTPUT_MAX_SKIP_ALIGN (file, align, max_skip);
#else
#ifdef ASM_OUTPUT_ALIGN_WITH_NOP
              ASM_OUTPUT_ALIGN_WITH_NOP (file, align);
#else
	      ASM_OUTPUT_ALIGN (file, align);
#endif
#endif
	    }
	}
#ifdef HAVE_cc0
      CC_STATUS_INIT;
      /* If this label is reached from only one place, set the condition
	 codes from the instruction just before the branch.  */

      /* Disabled because some insns set cc_status in the C output code
	 and NOTICE_UPDATE_CC alone can set incorrect status.  */
      if (0 /* optimize && LABEL_NUSES (insn) == 1*/)
	{
	  rtx jump = LABEL_REFS (insn);
	  rtx barrier = prev_nonnote_insn (insn);
	  rtx prev;
	  /* If the LABEL_REFS field of this label has been set to point
	     at a branch, the predecessor of the branch is a regular
	     insn, and that branch is the only way to reach this label,
	     set the condition codes based on the branch and its
	     predecessor.  */
	  if (barrier && BARRIER_P (barrier)
	      && jump && JUMP_P (jump)
	      && (prev = prev_nonnote_insn (jump))
	      && NONJUMP_INSN_P (prev))
	    {
	      NOTICE_UPDATE_CC (PATTERN (prev), prev);
	      NOTICE_UPDATE_CC (PATTERN (jump), jump);
	    }
	}
#endif

      if (LABEL_NAME (insn))
	(*debug_hooks->label) (insn);

      if (app_on)
	{
	  fputs (ASM_APP_OFF, file);
	  app_on = 0;
	}

      next = next_nonnote_insn (insn);
      if (next != 0 && JUMP_P (next))
	{
	  rtx nextbody = PATTERN (next);

	  /* If this label is followed by a jump-table,
	     make sure we put the label in the read-only section.  Also
	     possibly write the label and jump table together.  */

	  if (GET_CODE (nextbody) == ADDR_VEC
	      || GET_CODE (nextbody) == ADDR_DIFF_VEC)
	    {
#if defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC)
	      /* In this case, the case vector is being moved by the
		 target, so don't output the label at all.  Leave that
		 to the back end macros.  */
#else
	      if (! JUMP_TABLES_IN_TEXT_SECTION)
		{
		  int log_align;

		  switch_to_section (targetm.asm_out.function_rodata_section
				     (current_function_decl));

#ifdef ADDR_VEC_ALIGN
		  log_align = ADDR_VEC_ALIGN (next);
#else
		  log_align = exact_log2 (BIGGEST_ALIGNMENT / BITS_PER_UNIT);
#endif
		  ASM_OUTPUT_ALIGN (file, log_align);
		}
	      else
		switch_to_section (current_function_section ());

#ifdef ASM_OUTPUT_CASE_LABEL
	      ASM_OUTPUT_CASE_LABEL (file, "L", CODE_LABEL_NUMBER (insn),
				     next);
#else
	      targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (insn));
#endif
#endif
	      break;
	    }
	}
      if (LABEL_ALT_ENTRY_P (insn))
	output_alternate_entry_point (file, insn);
      else
	targetm.asm_out.internal_label (file, "L", CODE_LABEL_NUMBER (insn));
      break;

    default:
      {
	rtx body = PATTERN (insn);
	int insn_code_number;
	const char *template;

#ifdef HAVE_conditional_execution
	/* Reset this early so it is correct for ASM statements.  */
	current_insn_predicate = NULL_RTX;
#endif
	/* An INSN, JUMP_INSN or CALL_INSN.
	   First check for special kinds that recog doesn't recognize.  */

	if (GET_CODE (body) == USE /* These are just declarations.  */
	    || GET_CODE (body) == CLOBBER)
	  break;

#ifdef HAVE_cc0
	{
	  /* If there is a REG_CC_SETTER note on this insn, it means that
	     the setting of the condition code was done in the delay slot
	     of the insn that branched here.  So recover the cc status
	     from the insn that set it.  */

	  rtx note = find_reg_note (insn, REG_CC_SETTER, NULL_RTX);
	  if (note)
	    {
	      NOTICE_UPDATE_CC (PATTERN (XEXP (note, 0)), XEXP (note, 0));
	      cc_prev_status = cc_status;
	    }
	}
#endif

	/* Detect insns that are really jump-tables
	   and output them as such.  */

	if (GET_CODE (body) == ADDR_VEC || GET_CODE (body) == ADDR_DIFF_VEC)
	  {
#if !(defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC))
	    int vlen, idx;
#endif

	    if (! JUMP_TABLES_IN_TEXT_SECTION)
	      switch_to_section (targetm.asm_out.function_rodata_section
				 (current_function_decl));
	    else
	      switch_to_section (current_function_section ());

	    if (app_on)
	      {
		fputs (ASM_APP_OFF, file);
		app_on = 0;
	      }

#if defined(ASM_OUTPUT_ADDR_VEC) || defined(ASM_OUTPUT_ADDR_DIFF_VEC)
	    if (GET_CODE (body) == ADDR_VEC)
	      {
#ifdef ASM_OUTPUT_ADDR_VEC
		ASM_OUTPUT_ADDR_VEC (PREV_INSN (insn), body);
#else
		gcc_unreachable ();
#endif
	      }
	    else
	      {
#ifdef ASM_OUTPUT_ADDR_DIFF_VEC
		ASM_OUTPUT_ADDR_DIFF_VEC (PREV_INSN (insn), body);
#else
		gcc_unreachable ();
#endif
	      }
#else
	    vlen = XVECLEN (body, GET_CODE (body) == ADDR_DIFF_VEC);
	    for (idx = 0; idx < vlen; idx++)
	      {
		if (GET_CODE (body) == ADDR_VEC)
		  {
#ifdef ASM_OUTPUT_ADDR_VEC_ELT
		    ASM_OUTPUT_ADDR_VEC_ELT
		      (file, CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 0, idx), 0)));
#else
		    gcc_unreachable ();
#endif
		  }
		else
		  {
#ifdef ASM_OUTPUT_ADDR_DIFF_ELT
		    ASM_OUTPUT_ADDR_DIFF_ELT
		      (file,
		       body,
		       CODE_LABEL_NUMBER (XEXP (XVECEXP (body, 1, idx), 0)),
		       CODE_LABEL_NUMBER (XEXP (XEXP (body, 0), 0)));
#else
		    gcc_unreachable ();
#endif
		  }
	      }
#ifdef ASM_OUTPUT_CASE_END
	    ASM_OUTPUT_CASE_END (file,
				 CODE_LABEL_NUMBER (PREV_INSN (insn)),
				 insn);
#endif
#endif

	    switch_to_section (current_function_section ());

	    break;
	  }
	/* Output this line note if it is the first or the last line
	   note in a row.  */
	if (notice_source_line (insn))
	  {
	    (*debug_hooks->source_line) (last_linenum, last_filename);
	  }

	if (GET_CODE (body) == ASM_INPUT)
	  {
	    const char *string = XSTR (body, 0);

	    /* There's no telling what that did to the condition codes.  */
	    CC_STATUS_INIT;

	    if (string[0])
	      {
		if (! app_on)
		  {
		    fputs (ASM_APP_ON, file);
		    app_on = 1;
		  }
		fprintf (asm_out_file, "\t%s\n", string);
	      }
	    break;
	  }

	/* Detect `asm' construct with operands.  */
	if (asm_noperands (body) >= 0)
	  {
	    unsigned int noperands = asm_noperands (body);
	    rtx *ops = alloca (noperands * sizeof (rtx));
	    const char *string;

	    /* There's no telling what that did to the condition codes.  */
	    CC_STATUS_INIT;

	    /* Get out the operand values.  */
	    string = decode_asm_operands (body, ops, NULL, NULL, NULL);
	    /* Inhibit dieing on what would otherwise be compiler bugs.  */
	    insn_noperands = noperands;
	    this_is_asm_operands = insn;

#ifdef FINAL_PRESCAN_INSN
	    FINAL_PRESCAN_INSN (insn, ops, insn_noperands);
#endif

	    /* Output the insn using them.  */
	    if (string[0])
	      {
		if (! app_on)
		  {
		    fputs (ASM_APP_ON, file);
		    app_on = 1;
		  }
	        output_asm_insn (string, ops);
	      }

	    this_is_asm_operands = 0;
	    break;
	  }

	if (app_on)
	  {
	    fputs (ASM_APP_OFF, file);
	    app_on = 0;
	  }

	if (GET_CODE (body) == SEQUENCE)
	  {
	    /* A delayed-branch sequence */
	    int i;

	    final_sequence = body;

	    /* Record the delay slots' frame information before the branch.
	       This is needed for delayed calls: see execute_cfa_program().  */
#if defined (DWARF2_UNWIND_INFO)
	    if (dwarf2out_do_frame ())
	      for (i = 1; i < XVECLEN (body, 0); i++)
		dwarf2out_frame_debug (XVECEXP (body, 0, i), false);
#endif

	    /* The first insn in this SEQUENCE might be a JUMP_INSN that will
	       force the restoration of a comparison that was previously
	       thought unnecessary.  If that happens, cancel this sequence
	       and cause that insn to be restored.  */

	    next = final_scan_insn (XVECEXP (body, 0, 0), file, 0, 1, seen);
	    if (next != XVECEXP (body, 0, 1))
	      {
		final_sequence = 0;
		return next;
	      }

	    for (i = 1; i < XVECLEN (body, 0); i++)
	      {
		rtx insn = XVECEXP (body, 0, i);
		rtx next = NEXT_INSN (insn);
		/* We loop in case any instruction in a delay slot gets
		   split.  */
		do
		  insn = final_scan_insn (insn, file, 0, 1, seen);
		while (insn != next);
	      }
#ifdef DBR_OUTPUT_SEQEND
	    DBR_OUTPUT_SEQEND (file);
#endif
	    final_sequence = 0;

	    /* If the insn requiring the delay slot was a CALL_INSN, the
	       insns in the delay slot are actually executed before the
	       called function.  Hence we don't preserve any CC-setting
	       actions in these insns and the CC must be marked as being
	       clobbered by the function.  */
	    if (CALL_P (XVECEXP (body, 0, 0)))
	      {
		CC_STATUS_INIT;
	      }
	    break;
	  }

	/* We have a real machine instruction as rtl.  */

	body = PATTERN (insn);

#ifdef HAVE_cc0
	set = single_set (insn);

	/* Check for redundant test and compare instructions
	   (when the condition codes are already set up as desired).
	   This is done only when optimizing; if not optimizing,
	   it should be possible for the user to alter a variable
	   with the debugger in between statements
	   and the next statement should reexamine the variable
	   to compute the condition codes.  */

	if (optimize)
	  {
	    if (set
		&& GET_CODE (SET_DEST (set)) == CC0
		&& insn != last_ignored_compare)
	      {
		if (GET_CODE (SET_SRC (set)) == SUBREG)
		  SET_SRC (set) = alter_subreg (&SET_SRC (set));
		else if (GET_CODE (SET_SRC (set)) == COMPARE)
		  {
		    if (GET_CODE (XEXP (SET_SRC (set), 0)) == SUBREG)
		      XEXP (SET_SRC (set), 0)
			= alter_subreg (&XEXP (SET_SRC (set), 0));
		    if (GET_CODE (XEXP (SET_SRC (set), 1)) == SUBREG)
		      XEXP (SET_SRC (set), 1)
			= alter_subreg (&XEXP (SET_SRC (set), 1));
		  }
		if ((cc_status.value1 != 0
		     && rtx_equal_p (SET_SRC (set), cc_status.value1))
		    || (cc_status.value2 != 0
			&& rtx_equal_p (SET_SRC (set), cc_status.value2)))
		  {
		    /* Don't delete insn if it has an addressing side-effect.  */
		    if (! FIND_REG_INC_NOTE (insn, NULL_RTX)
			/* or if anything in it is volatile.  */
			&& ! volatile_refs_p (PATTERN (insn)))
		      {
			/* We don't really delete the insn; just ignore it.  */
			last_ignored_compare = insn;
			break;
		      }
		  }
	      }
	  }
#endif

#ifdef HAVE_cc0
	/* If this is a conditional branch, maybe modify it
	   if the cc's are in a nonstandard state
	   so that it accomplishes the same thing that it would
	   do straightforwardly if the cc's were set up normally.  */

	if (cc_status.flags != 0
	    && JUMP_P (insn)
	    && GET_CODE (body) == SET
	    && SET_DEST (body) == pc_rtx
	    && GET_CODE (SET_SRC (body)) == IF_THEN_ELSE
	    && COMPARISON_P (XEXP (SET_SRC (body), 0))
	    && XEXP (XEXP (SET_SRC (body), 0), 0) == cc0_rtx)
	  {
	    /* This function may alter the contents of its argument
	       and clear some of the cc_status.flags bits.
	       It may also return 1 meaning condition now always true
	       or -1 meaning condition now always false
	       or 2 meaning condition nontrivial but altered.  */
	    int result = alter_cond (XEXP (SET_SRC (body), 0));
	    /* If condition now has fixed value, replace the IF_THEN_ELSE
	       with its then-operand or its else-operand.  */
	    if (result == 1)
	      SET_SRC (body) = XEXP (SET_SRC (body), 1);
	    if (result == -1)
	      SET_SRC (body) = XEXP (SET_SRC (body), 2);

	    /* The jump is now either unconditional or a no-op.
	       If it has become a no-op, don't try to output it.
	       (It would not be recognized.)  */
	    if (SET_SRC (body) == pc_rtx)
	      {
	        delete_insn (insn);
		break;
	      }
	    else if (GET_CODE (SET_SRC (body)) == RETURN)
	      /* Replace (set (pc) (return)) with (return).  */
	      PATTERN (insn) = body = SET_SRC (body);

	    /* Rerecognize the instruction if it has changed.  */
	    if (result != 0)
	      INSN_CODE (insn) = -1;
	  }

	/* Make same adjustments to instructions that examine the
	   condition codes without jumping and instructions that
	   handle conditional moves (if this machine has either one).  */

	if (cc_status.flags != 0
	    && set != 0)
	  {
	    rtx cond_rtx, then_rtx, else_rtx;

	    if (!JUMP_P (insn)
		&& GET_CODE (SET_SRC (set)) == IF_THEN_ELSE)
	      {
		cond_rtx = XEXP (SET_SRC (set), 0);
		then_rtx = XEXP (SET_SRC (set), 1);
		else_rtx = XEXP (SET_SRC (set), 2);
	      }
	    else
	      {
		cond_rtx = SET_SRC (set);
		then_rtx = const_true_rtx;
		else_rtx = const0_rtx;
	      }

	    switch (GET_CODE (cond_rtx))
	      {
	      case GTU:
	      case GT:
	      case LTU:
	      case LT:
	      case GEU:
	      case GE:
	      case LEU:
	      case LE:
	      case EQ:
	      case NE:
		{
		  int result;
		  if (XEXP (cond_rtx, 0) != cc0_rtx)
		    break;
		  result = alter_cond (cond_rtx);
		  if (result == 1)
		    validate_change (insn, &SET_SRC (set), then_rtx, 0);
		  else if (result == -1)
		    validate_change (insn, &SET_SRC (set), else_rtx, 0);
		  else if (result == 2)
		    INSN_CODE (insn) = -1;
		  if (SET_DEST (set) == SET_SRC (set))
		    delete_insn (insn);
		}
		break;

	      default:
		break;
	      }
	  }

#endif

#ifdef HAVE_peephole
	/* Do machine-specific peephole optimizations if desired.  */

	if (optimize && !flag_no_peephole && !nopeepholes)
	  {
	    rtx next = peephole (insn);
	    /* When peepholing, if there were notes within the peephole,
	       emit them before the peephole.  */
	    if (next != 0 && next != NEXT_INSN (insn))
	      {
		rtx note, prev = PREV_INSN (insn);

		for (note = NEXT_INSN (insn); note != next;
		     note = NEXT_INSN (note))
		  final_scan_insn (note, file, optimize, nopeepholes, seen);

		/* Put the notes in the proper position for a later
		   rescan.  For example, the SH target can do this
		   when generating a far jump in a delayed branch
		   sequence.  */
		note = NEXT_INSN (insn);
		PREV_INSN (note) = prev;
		NEXT_INSN (prev) = note;
		NEXT_INSN (PREV_INSN (next)) = insn;
		PREV_INSN (insn) = PREV_INSN (next);
		NEXT_INSN (insn) = next;
		PREV_INSN (next) = insn;
	      }

	    /* PEEPHOLE might have changed this.  */
	    body = PATTERN (insn);
	  }
#endif

	/* Try to recognize the instruction.
	   If successful, verify that the operands satisfy the
	   constraints for the instruction.  Crash if they don't,
	   since `reload' should have changed them so that they do.  */

	insn_code_number = recog_memoized (insn);
	cleanup_subreg_operands (insn);

	/* Dump the insn in the assembly for debugging.  */
	if (flag_dump_rtl_in_asm)
	  {
	    print_rtx_head = ASM_COMMENT_START;
	    print_rtl_single (asm_out_file, insn);
	    print_rtx_head = "";
	  }

	if (! constrain_operands_cached (1))
	  fatal_insn_not_found (insn);

	/* Some target machines need to prescan each insn before
	   it is output.  */

#ifdef FINAL_PRESCAN_INSN
	FINAL_PRESCAN_INSN (insn, recog_data.operand, recog_data.n_operands);
#endif

#ifdef HAVE_conditional_execution
	if (GET_CODE (PATTERN (insn)) == COND_EXEC)
	  current_insn_predicate = COND_EXEC_TEST (PATTERN (insn));
#endif

#ifdef HAVE_cc0
	cc_prev_status = cc_status;

	/* Update `cc_status' for this instruction.
	   The instruction's output routine may change it further.
	   If the output routine for a jump insn needs to depend
	   on the cc status, it should look at cc_prev_status.  */

	NOTICE_UPDATE_CC (body, insn);
#endif

	current_output_insn = debug_insn = insn;

#if defined (DWARF2_UNWIND_INFO)
	if (CALL_P (insn) && dwarf2out_do_frame ())
	  dwarf2out_frame_debug (insn, false);
#endif

	/* Find the proper template for this insn.  */
	template = get_insn_template (insn_code_number, insn);

	/* If the C code returns 0, it means that it is a jump insn
	   which follows a deleted test insn, and that test insn
	   needs to be reinserted.  */
	if (template == 0)
	  {
	    rtx prev;

	    gcc_assert (prev_nonnote_insn (insn) == last_ignored_compare);

	    /* We have already processed the notes between the setter and
	       the user.  Make sure we don't process them again, this is
	       particularly important if one of the notes is a block
	       scope note or an EH note.  */
	    for (prev = insn;
		 prev != last_ignored_compare;
		 prev = PREV_INSN (prev))
	      {
		if (NOTE_P (prev))
		  delete_insn (prev);	/* Use delete_note.  */
	      }

	    return prev;
	  }

	/* If the template is the string "#", it means that this insn must
	   be split.  */
	if (template[0] == '#' && template[1] == '\0')
	  {
	    rtx new = try_split (body, insn, 0);

	    /* If we didn't split the insn, go away.  */
	    if (new == insn && PATTERN (new) == body)
	      fatal_insn ("could not split insn", insn);

#ifdef HAVE_ATTR_length
	    /* This instruction should have been split in shorten_branches,
	       to ensure that we would have valid length info for the
	       splitees.  */
	    gcc_unreachable ();
#endif

	    return new;
	  }

#ifdef TARGET_UNWIND_INFO
	/* ??? This will put the directives in the wrong place if
	   get_insn_template outputs assembly directly.  However calling it
	   before get_insn_template breaks if the insns is split.  */
	targetm.asm_out.unwind_emit (asm_out_file, insn);
#endif

	/* Output assembler code from the template.  */
	output_asm_insn (template, recog_data.operand);

	/* If necessary, report the effect that the instruction has on
	   the unwind info.   We've already done this for delay slots
	   and call instructions.  */
#if defined (DWARF2_UNWIND_INFO)
	if (final_sequence == 0
#if !defined (HAVE_prologue)
	    && !ACCUMULATE_OUTGOING_ARGS
#endif
	    && dwarf2out_do_frame ())
	  dwarf2out_frame_debug (insn, true);
#endif

	current_output_insn = debug_insn = 0;
      }
    }
  return NEXT_INSN (insn);
}

/* Return whether a source line note needs to be emitted before INSN.  */

static bool
notice_source_line (rtx insn)
{
  const char *filename = insn_file (insn);
  int linenum = insn_line (insn);

  if (filename
      && (force_source_line
	  || filename != last_filename
	  || last_linenum != linenum))
    {
      force_source_line = false;
      last_filename = filename;
      last_linenum = linenum;
      high_block_linenum = MAX (last_linenum, high_block_linenum);
      high_function_linenum = MAX (last_linenum, high_function_linenum);
      return true;
    }
  return false;
}

/* For each operand in INSN, simplify (subreg (reg)) so that it refers
   directly to the desired hard register.  */

void
cleanup_subreg_operands (rtx insn)
{
  int i;
  extract_insn_cached (insn);
  for (i = 0; i < recog_data.n_operands; i++)
    {
      /* The following test cannot use recog_data.operand when testing
	 for a SUBREG: the underlying object might have been changed
	 already if we are inside a match_operator expression that
	 matches the else clause.  Instead we test the underlying
	 expression directly.  */
      if (GET_CODE (*recog_data.operand_loc[i]) == SUBREG)
	recog_data.operand[i] = alter_subreg (recog_data.operand_loc[i]);
      else if (GET_CODE (recog_data.operand[i]) == PLUS
	       || GET_CODE (recog_data.operand[i]) == MULT
	       || MEM_P (recog_data.operand[i]))
	recog_data.operand[i] = walk_alter_subreg (recog_data.operand_loc[i]);
    }

  for (i = 0; i < recog_data.n_dups; i++)
    {
      if (GET_CODE (*recog_data.dup_loc[i]) == SUBREG)
	*recog_data.dup_loc[i] = alter_subreg (recog_data.dup_loc[i]);
      else if (GET_CODE (*recog_data.dup_loc[i]) == PLUS
	       || GET_CODE (*recog_data.dup_loc[i]) == MULT
	       || MEM_P (*recog_data.dup_loc[i]))
	*recog_data.dup_loc[i] = walk_alter_subreg (recog_data.dup_loc[i]);
    }
}

/* If X is a SUBREG, replace it with a REG or a MEM,
   based on the thing it is a subreg of.  */

rtx
alter_subreg (rtx *xp)
{
  rtx x = *xp;
  rtx y = SUBREG_REG (x);

  /* simplify_subreg does not remove subreg from volatile references.
     We are required to.  */
  if (MEM_P (y))
    {
      int offset = SUBREG_BYTE (x);

      /* For paradoxical subregs on big-endian machines, SUBREG_BYTE
	 contains 0 instead of the proper offset.  See simplify_subreg.  */
      if (offset == 0
	  && GET_MODE_SIZE (GET_MODE (y)) < GET_MODE_SIZE (GET_MODE (x)))
        {
          int difference = GET_MODE_SIZE (GET_MODE (y))
			   - GET_MODE_SIZE (GET_MODE (x));
          if (WORDS_BIG_ENDIAN)
            offset += (difference / UNITS_PER_WORD) * UNITS_PER_WORD;
          if (BYTES_BIG_ENDIAN)
            offset += difference % UNITS_PER_WORD;
        }

      *xp = adjust_address (y, GET_MODE (x), offset);
    }
  else
    {
      rtx new = simplify_subreg (GET_MODE (x), y, GET_MODE (y),
				 SUBREG_BYTE (x));

      if (new != 0)
	*xp = new;
      else if (REG_P (y))
	{
	  /* Simplify_subreg can't handle some REG cases, but we have to.  */
	  unsigned int regno = subreg_regno (x);
	  *xp = gen_rtx_REG_offset (y, GET_MODE (x), regno, SUBREG_BYTE (x));
	}
    }

  return *xp;
}

/* Do alter_subreg on all the SUBREGs contained in X.  */

static rtx
walk_alter_subreg (rtx *xp)
{
  rtx x = *xp;
  switch (GET_CODE (x))
    {
    case PLUS:
    case MULT:
    case AND:
      XEXP (x, 0) = walk_alter_subreg (&XEXP (x, 0));
      XEXP (x, 1) = walk_alter_subreg (&XEXP (x, 1));
      break;

    case MEM:
    case ZERO_EXTEND:
      XEXP (x, 0) = walk_alter_subreg (&XEXP (x, 0));
      break;

    case SUBREG:
      return alter_subreg (xp);

    default:
      break;
    }

  return *xp;
}

#ifdef HAVE_cc0

/* Given BODY, the body of a jump instruction, alter the jump condition
   as required by the bits that are set in cc_status.flags.
   Not all of the bits there can be handled at this level in all cases.

   The value is normally 0.
   1 means that the condition has become always true.
   -1 means that the condition has become always false.
   2 means that COND has been altered.  */

static int
alter_cond (rtx cond)
{
  int value = 0;

  if (cc_status.flags & CC_REVERSED)
    {
      value = 2;
      PUT_CODE (cond, swap_condition (GET_CODE (cond)));
    }

  if (cc_status.flags & CC_INVERTED)
    {
      value = 2;
      PUT_CODE (cond, reverse_condition (GET_CODE (cond)));
    }

  if (cc_status.flags & CC_NOT_POSITIVE)
    switch (GET_CODE (cond))
      {
      case LE:
      case LEU:
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case GT:
      case GTU:
      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      case GE:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case LT:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      default:
	break;
      }

  if (cc_status.flags & CC_NOT_NEGATIVE)
    switch (GET_CODE (cond))
      {
      case GE:
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case LT:
      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      case LE:
      case LEU:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case GT:
      case GTU:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      default:
	break;
      }

  if (cc_status.flags & CC_NO_OVERFLOW)
    switch (GET_CODE (cond))
      {
      case GEU:
	/* Jump becomes unconditional.  */
	return 1;

      case LEU:
	PUT_CODE (cond, EQ);
	value = 2;
	break;

      case GTU:
	PUT_CODE (cond, NE);
	value = 2;
	break;

      case LTU:
	/* Jump becomes no-op.  */
	return -1;

      default:
	break;
      }

  if (cc_status.flags & (CC_Z_IN_NOT_N | CC_Z_IN_N))
    switch (GET_CODE (cond))
      {
      default:
	gcc_unreachable ();

      case NE:
	PUT_CODE (cond, cc_status.flags & CC_Z_IN_N ? GE : LT);
	value = 2;
	break;

      case EQ:
	PUT_CODE (cond, cc_status.flags & CC_Z_IN_N ? LT : GE);
	value = 2;
	break;
      }

  if (cc_status.flags & CC_NOT_SIGNED)
    /* The flags are valid if signed condition operators are converted
       to unsigned.  */
    switch (GET_CODE (cond))
      {
      case LE:
	PUT_CODE (cond, LEU);
	value = 2;
	break;

      case LT:
	PUT_CODE (cond, LTU);
	value = 2;
	break;

      case GT:
	PUT_CODE (cond, GTU);
	value = 2;
	break;

      case GE:
	PUT_CODE (cond, GEU);
	value = 2;
	break;

      default:
	break;
      }

  return value;
}
#endif

/* Report inconsistency between the assembler template and the operands.
   In an `asm', it's the user's fault; otherwise, the compiler's fault.  */

void
output_operand_lossage (const char *cmsgid, ...)
{
  char *fmt_string;
  char *new_message;
  const char *pfx_str;
  va_list ap;

  va_start (ap, cmsgid);

  pfx_str = this_is_asm_operands ? _("invalid 'asm': ") : "output_operand: ";
  asprintf (&fmt_string, "%s%s", pfx_str, _(cmsgid));
  vasprintf (&new_message, fmt_string, ap);

  if (this_is_asm_operands)
    error_for_asm (this_is_asm_operands, "%s", new_message);
  else
    internal_error ("%s", new_message);

  free (fmt_string);
  free (new_message);
  va_end (ap);
}

/* Output of assembler code from a template, and its subroutines.  */

/* Annotate the assembly with a comment describing the pattern and
   alternative used.  */

static void
output_asm_name (void)
{
  if (debug_insn)
    {
      int num = INSN_CODE (debug_insn);
      fprintf (asm_out_file, "\t%s %d\t%s",
	       ASM_COMMENT_START, INSN_UID (debug_insn),
	       insn_data[num].name);
      if (insn_data[num].n_alternatives > 1)
	fprintf (asm_out_file, "/%d", which_alternative + 1);
#ifdef HAVE_ATTR_length
      fprintf (asm_out_file, "\t[length = %d]",
	       get_attr_length (debug_insn));
#endif
      /* Clear this so only the first assembler insn
	 of any rtl insn will get the special comment for -dp.  */
      debug_insn = 0;
    }
}

/* If OP is a REG or MEM and we can find a MEM_EXPR corresponding to it
   or its address, return that expr .  Set *PADDRESSP to 1 if the expr
   corresponds to the address of the object and 0 if to the object.  */

static tree
get_mem_expr_from_op (rtx op, int *paddressp)
{
  tree expr;
  int inner_addressp;

  *paddressp = 0;

  if (REG_P (op))
    return REG_EXPR (op);
  else if (!MEM_P (op))
    return 0;

  if (MEM_EXPR (op) != 0)
    return MEM_EXPR (op);

  /* Otherwise we have an address, so indicate it and look at the address.  */
  *paddressp = 1;
  op = XEXP (op, 0);

  /* First check if we have a decl for the address, then look at the right side
     if it is a PLUS.  Otherwise, strip off arithmetic and keep looking.
     But don't allow the address to itself be indirect.  */
  if ((expr = get_mem_expr_from_op (op, &inner_addressp)) && ! inner_addressp)
    return expr;
  else if (GET_CODE (op) == PLUS
	   && (expr = get_mem_expr_from_op (XEXP (op, 1), &inner_addressp)))
    return expr;

  while (GET_RTX_CLASS (GET_CODE (op)) == RTX_UNARY
	 || GET_RTX_CLASS (GET_CODE (op)) == RTX_BIN_ARITH)
    op = XEXP (op, 0);

  expr = get_mem_expr_from_op (op, &inner_addressp);
  return inner_addressp ? 0 : expr;
}

/* Output operand names for assembler instructions.  OPERANDS is the
   operand vector, OPORDER is the order to write the operands, and NOPS
   is the number of operands to write.  */

static void
output_asm_operand_names (rtx *operands, int *oporder, int nops)
{
  int wrote = 0;
  int i;

  for (i = 0; i < nops; i++)
    {
      int addressp;
      rtx op = operands[oporder[i]];
      tree expr = get_mem_expr_from_op (op, &addressp);

      fprintf (asm_out_file, "%c%s",
	       wrote ? ',' : '\t', wrote ? "" : ASM_COMMENT_START);
      wrote = 1;
      if (expr)
	{
	  fprintf (asm_out_file, "%s",
		   addressp ? "*" : "");
	  print_mem_expr (asm_out_file, expr);
	  wrote = 1;
	}
      else if (REG_P (op) && ORIGINAL_REGNO (op)
	       && ORIGINAL_REGNO (op) != REGNO (op))
	fprintf (asm_out_file, " tmp%i", ORIGINAL_REGNO (op));
    }
}

/* Output text from TEMPLATE to the assembler output file,
   obeying %-directions to substitute operands taken from
   the vector OPERANDS.

   %N (for N a digit) means print operand N in usual manner.
   %lN means require operand N to be a CODE_LABEL or LABEL_REF
      and print the label name with no punctuation.
   %cN means require operand N to be a constant
      and print the constant expression with no punctuation.
   %aN means expect operand N to be a memory address
      (not a memory reference!) and print a reference
      to that address.
   %nN means expect operand N to be a constant
      and print a constant expression for minus the value
      of the operand, with no other punctuation.  */

void
output_asm_insn (const char *template, rtx *operands)
{
  const char *p;
  int c;
#ifdef ASSEMBLER_DIALECT
  int dialect = 0;
#endif
  int oporder[MAX_RECOG_OPERANDS];
  char opoutput[MAX_RECOG_OPERANDS];
  int ops = 0;

  /* An insn may return a null string template
     in a case where no assembler code is needed.  */
  if (*template == 0)
    return;

  memset (opoutput, 0, sizeof opoutput);
  p = template;
  putc ('\t', asm_out_file);

#ifdef ASM_OUTPUT_OPCODE
  ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif

  while ((c = *p++))
    switch (c)
      {
      case '\n':
	if (flag_verbose_asm)
	  output_asm_operand_names (operands, oporder, ops);
	if (flag_print_asm_name)
	  output_asm_name ();

	ops = 0;
	memset (opoutput, 0, sizeof opoutput);

	putc (c, asm_out_file);
#ifdef ASM_OUTPUT_OPCODE
	while ((c = *p) == '\t')
	  {
	    putc (c, asm_out_file);
	    p++;
	  }
	ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif
	break;

#ifdef ASSEMBLER_DIALECT
      case '{':
	{
	  int i;

	  if (dialect)
	    output_operand_lossage ("nested assembly dialect alternatives");
	  else
	    dialect = 1;

	  /* If we want the first dialect, do nothing.  Otherwise, skip
	     DIALECT_NUMBER of strings ending with '|'.  */
	  for (i = 0; i < dialect_number; i++)
	    {
	      while (*p && *p != '}' && *p++ != '|')
		;
	      if (*p == '}')
		break;
	      if (*p == '|')
		p++;
	    }

	  if (*p == '\0')
	    output_operand_lossage ("unterminated assembly dialect alternative");
	}
	break;

      case '|':
	if (dialect)
	  {
	    /* Skip to close brace.  */
	    do
	      {
		if (*p == '\0')
		  {
		    output_operand_lossage ("unterminated assembly dialect alternative");
		    break;
		  }
	      }
	    while (*p++ != '}');
	    dialect = 0;
	  }
	else
	  putc (c, asm_out_file);
	break;

      case '}':
	if (! dialect)
	  putc (c, asm_out_file);
	dialect = 0;
	break;
#endif

      case '%':
	/* %% outputs a single %.  */
	if (*p == '%')
	  {
	    p++;
	    putc (c, asm_out_file);
	  }
	/* %= outputs a number which is unique to each insn in the entire
	   compilation.  This is useful for making local labels that are
	   referred to more than once in a given insn.  */
	else if (*p == '=')
	  {
	    p++;
	    fprintf (asm_out_file, "%d", insn_counter);
	  }
	/* % followed by a letter and some digits
	   outputs an operand in a special way depending on the letter.
	   Letters `acln' are implemented directly.
	   Other letters are passed to `output_operand' so that
	   the PRINT_OPERAND macro can define them.  */
	else if (ISALPHA (*p))
	  {
	    int letter = *p++;
	    unsigned long opnum;
	    char *endptr;

	    opnum = strtoul (p, &endptr, 10);

	    if (endptr == p)
	      output_operand_lossage ("operand number missing "
				      "after %%-letter");
	    else if (this_is_asm_operands && opnum >= insn_noperands)
	      output_operand_lossage ("operand number out of range");
	    else if (letter == 'l')
	      output_asm_label (operands[opnum]);
	    else if (letter == 'a')
	      output_address (operands[opnum]);
	    else if (letter == 'c')
	      {
		if (CONSTANT_ADDRESS_P (operands[opnum]))
		  output_addr_const (asm_out_file, operands[opnum]);
		else
		  output_operand (operands[opnum], 'c');
	      }
	    else if (letter == 'n')
	      {
		if (GET_CODE (operands[opnum]) == CONST_INT)
		  fprintf (asm_out_file, HOST_WIDE_INT_PRINT_DEC,
			   - INTVAL (operands[opnum]));
		else
		  {
		    putc ('-', asm_out_file);
		    output_addr_const (asm_out_file, operands[opnum]);
		  }
	      }
	    else
	      output_operand (operands[opnum], letter);

	    if (!opoutput[opnum])
	      oporder[ops++] = opnum;
	    opoutput[opnum] = 1;

	    p = endptr;
	    c = *p;
	  }
	/* % followed by a digit outputs an operand the default way.  */
	else if (ISDIGIT (*p))
	  {
	    unsigned long opnum;
	    char *endptr;

	    opnum = strtoul (p, &endptr, 10);
	    if (this_is_asm_operands && opnum >= insn_noperands)
	      output_operand_lossage ("operand number out of range");
	    else
	      output_operand (operands[opnum], 0);

	    if (!opoutput[opnum])
	      oporder[ops++] = opnum;
	    opoutput[opnum] = 1;

	    p = endptr;
	    c = *p;
	  }
	/* % followed by punctuation: output something for that
	   punctuation character alone, with no operand.
	   The PRINT_OPERAND macro decides what is actually done.  */
#ifdef PRINT_OPERAND_PUNCT_VALID_P
	else if (PRINT_OPERAND_PUNCT_VALID_P ((unsigned char) *p))
	  output_operand (NULL_RTX, *p++);
#endif
	else
	  output_operand_lossage ("invalid %%-code");
	break;

      default:
	putc (c, asm_out_file);
      }

  /* Write out the variable names for operands, if we know them.  */
  if (flag_verbose_asm)
    output_asm_operand_names (operands, oporder, ops);
  if (flag_print_asm_name)
    output_asm_name ();

  putc ('\n', asm_out_file);
}

/* Output a LABEL_REF, or a bare CODE_LABEL, as an assembler symbol.  */

void
output_asm_label (rtx x)
{
  char buf[256];

  if (GET_CODE (x) == LABEL_REF)
    x = XEXP (x, 0);
  if (LABEL_P (x)
      || (NOTE_P (x)
	  && NOTE_LINE_NUMBER (x) == NOTE_INSN_DELETED_LABEL))
    ASM_GENERATE_INTERNAL_LABEL (buf, "L", CODE_LABEL_NUMBER (x));
  else
    output_operand_lossage ("'%%l' operand isn't a label");

  assemble_name (asm_out_file, buf);
}

/* Print operand X using machine-dependent assembler syntax.
   The macro PRINT_OPERAND is defined just to control this function.
   CODE is a non-digit that preceded the operand-number in the % spec,
   such as 'z' if the spec was `%z3'.  CODE is 0 if there was no char
   between the % and the digits.
   When CODE is a non-letter, X is 0.

   The meanings of the letters are machine-dependent and controlled
   by PRINT_OPERAND.  */

static void
output_operand (rtx x, int code ATTRIBUTE_UNUSED)
{
  if (x && GET_CODE (x) == SUBREG)
    x = alter_subreg (&x);

  /* X must not be a pseudo reg.  */
  gcc_assert (!x || !REG_P (x) || REGNO (x) < FIRST_PSEUDO_REGISTER);

  PRINT_OPERAND (asm_out_file, x, code);
}

/* Print a memory reference operand for address X
   using machine-dependent assembler syntax.
   The macro PRINT_OPERAND_ADDRESS exists just to control this function.  */

void
output_address (rtx x)
{
  walk_alter_subreg (&x);
  PRINT_OPERAND_ADDRESS (asm_out_file, x);
}

/* Print an integer constant expression in assembler syntax.
   Addition and subtraction are the only arithmetic
   that may appear in these expressions.  */

void
output_addr_const (FILE *file, rtx x)
{
  char buf[256];

 restart:
  switch (GET_CODE (x))
    {
    case PC:
      putc ('.', file);
      break;

    case SYMBOL_REF:
      if (SYMBOL_REF_DECL (x))
	mark_decl_referenced (SYMBOL_REF_DECL (x));
#ifdef ASM_OUTPUT_SYMBOL_REF
      ASM_OUTPUT_SYMBOL_REF (file, x);
#else
      assemble_name (file, XSTR (x, 0));
#endif
      break;

    case LABEL_REF:
      x = XEXP (x, 0);
      /* Fall through.  */
    case CODE_LABEL:
      ASM_GENERATE_INTERNAL_LABEL (buf, "L", CODE_LABEL_NUMBER (x));
#ifdef ASM_OUTPUT_LABEL_REF
      ASM_OUTPUT_LABEL_REF (file, buf);
#else
      assemble_name (file, buf);
#endif
      break;

    case CONST_INT:
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;

    case CONST:
      /* This used to output parentheses around the expression,
	 but that does not work on the 386 (either ATT or BSD assembler).  */
      output_addr_const (file, XEXP (x, 0));
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	{
	  /* We can use %d if the number is one word and positive.  */
	  if (CONST_DOUBLE_HIGH (x))
	    fprintf (file, HOST_WIDE_INT_PRINT_DOUBLE_HEX,
		     CONST_DOUBLE_HIGH (x), CONST_DOUBLE_LOW (x));
	  else if (CONST_DOUBLE_LOW (x) < 0)
	    fprintf (file, HOST_WIDE_INT_PRINT_HEX, CONST_DOUBLE_LOW (x));
	  else
	    fprintf (file, HOST_WIDE_INT_PRINT_DEC, CONST_DOUBLE_LOW (x));
	}
      else
	/* We can't handle floating point constants;
	   PRINT_OPERAND must handle them.  */
	output_operand_lossage ("floating constant misused");
      break;

    case PLUS:
      /* Some assemblers need integer constants to appear last (eg masm).  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	{
	  output_addr_const (file, XEXP (x, 1));
	  if (INTVAL (XEXP (x, 0)) >= 0)
	    fprintf (file, "+");
	  output_addr_const (file, XEXP (x, 0));
	}
      else
	{
	  output_addr_const (file, XEXP (x, 0));
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT
	      || INTVAL (XEXP (x, 1)) >= 0)
	    fprintf (file, "+");
	  output_addr_const (file, XEXP (x, 1));
	}
      break;

    case MINUS:
      /* Avoid outputting things like x-x or x+5-x,
	 since some assemblers can't handle that.  */
      x = simplify_subtraction (x);
      if (GET_CODE (x) != MINUS)
	goto restart;

      output_addr_const (file, XEXP (x, 0));
      fprintf (file, "-");
      if ((GET_CODE (XEXP (x, 1)) == CONST_INT && INTVAL (XEXP (x, 1)) >= 0)
	  || GET_CODE (XEXP (x, 1)) == PC
	  || GET_CODE (XEXP (x, 1)) == SYMBOL_REF)
	output_addr_const (file, XEXP (x, 1));
      else
	{
	  fputs (targetm.asm_out.open_paren, file);
	  output_addr_const (file, XEXP (x, 1));
	  fputs (targetm.asm_out.close_paren, file);
	}
      break;

    case ZERO_EXTEND:
    case SIGN_EXTEND:
    case SUBREG:
      output_addr_const (file, XEXP (x, 0));
      break;

    default:
#ifdef OUTPUT_ADDR_CONST_EXTRA
      OUTPUT_ADDR_CONST_EXTRA (file, x, fail);
      break;

    fail:
#endif
      output_operand_lossage ("invalid expression as operand");
    }
}

/* A poor man's fprintf, with the added features of %I, %R, %L, and %U.
   %R prints the value of REGISTER_PREFIX.
   %L prints the value of LOCAL_LABEL_PREFIX.
   %U prints the value of USER_LABEL_PREFIX.
   %I prints the value of IMMEDIATE_PREFIX.
   %O runs ASM_OUTPUT_OPCODE to transform what follows in the string.
   Also supported are %d, %i, %u, %x, %X, %o, %c, %s and %%.

   We handle alternate assembler dialects here, just like output_asm_insn.  */

void
asm_fprintf (FILE *file, const char *p, ...)
{
  char buf[10];
  char *q, c;
  va_list argptr;

  va_start (argptr, p);

  buf[0] = '%';

  while ((c = *p++))
    switch (c)
      {
#ifdef ASSEMBLER_DIALECT
      case '{':
	{
	  int i;

	  /* If we want the first dialect, do nothing.  Otherwise, skip
	     DIALECT_NUMBER of strings ending with '|'.  */
	  for (i = 0; i < dialect_number; i++)
	    {
	      while (*p && *p++ != '|')
		;

	      if (*p == '|')
		p++;
	    }
	}
	break;

      case '|':
	/* Skip to close brace.  */
	while (*p && *p++ != '}')
	  ;
	break;

      case '}':
	break;
#endif

      case '%':
	c = *p++;
	q = &buf[1];
	while (strchr ("-+ #0", c))
	  {
	    *q++ = c;
	    c = *p++;
	  }
	while (ISDIGIT (c) || c == '.')
	  {
	    *q++ = c;
	    c = *p++;
	  }
	switch (c)
	  {
	  case '%':
	    putc ('%', file);
	    break;

	  case 'd':  case 'i':  case 'u':
	  case 'x':  case 'X':  case 'o':
	  case 'c':
	    *q++ = c;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, int));
	    break;

	  case 'w':
	    /* This is a prefix to the 'd', 'i', 'u', 'x', 'X', and
	       'o' cases, but we do not check for those cases.  It
	       means that the value is a HOST_WIDE_INT, which may be
	       either `long' or `long long'.  */
	    memcpy (q, HOST_WIDE_INT_PRINT, strlen (HOST_WIDE_INT_PRINT));
	    q += strlen (HOST_WIDE_INT_PRINT);
	    *q++ = *p++;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, HOST_WIDE_INT));
	    break;

	  case 'l':
	    *q++ = c;
#ifdef HAVE_LONG_LONG
	    if (*p == 'l')
	      {
		*q++ = *p++;
		*q++ = *p++;
		*q = 0;
		fprintf (file, buf, va_arg (argptr, long long));
	      }
	    else
#endif
	      {
		*q++ = *p++;
		*q = 0;
		fprintf (file, buf, va_arg (argptr, long));
	      }

	    break;

	  case 's':
	    *q++ = c;
	    *q = 0;
	    fprintf (file, buf, va_arg (argptr, char *));
	    break;

	  case 'O':
#ifdef ASM_OUTPUT_OPCODE
	    ASM_OUTPUT_OPCODE (asm_out_file, p);
#endif
	    break;

	  case 'R':
#ifdef REGISTER_PREFIX
	    fprintf (file, "%s", REGISTER_PREFIX);
#endif
	    break;

	  case 'I':
#ifdef IMMEDIATE_PREFIX
	    fprintf (file, "%s", IMMEDIATE_PREFIX);
#endif
	    break;

	  case 'L':
#ifdef LOCAL_LABEL_PREFIX
	    fprintf (file, "%s", LOCAL_LABEL_PREFIX);
#endif
	    break;

	  case 'U':
	    fputs (user_label_prefix, file);
	    break;

#ifdef ASM_FPRINTF_EXTENSIONS
	    /* Uppercase letters are reserved for general use by asm_fprintf
	       and so are not available to target specific code.  In order to
	       prevent the ASM_FPRINTF_EXTENSIONS macro from using them then,
	       they are defined here.  As they get turned into real extensions
	       to asm_fprintf they should be removed from this list.  */
	  case 'A': case 'B': case 'C': case 'D': case 'E':
	  case 'F': case 'G': case 'H': case 'J': case 'K':
	  case 'M': case 'N': case 'P': case 'Q': case 'S':
	  case 'T': case 'V': case 'W': case 'Y': case 'Z':
	    break;

	  ASM_FPRINTF_EXTENSIONS (file, argptr, p)
#endif
	  default:
	    gcc_unreachable ();
	  }
	break;

      default:
	putc (c, file);
      }
  va_end (argptr);
}

/* Split up a CONST_DOUBLE or integer constant rtx
   into two rtx's for single words,
   storing in *FIRST the word that comes first in memory in the target
   and in *SECOND the other.  */

void
split_double (rtx value, rtx *first, rtx *second)
{
  if (GET_CODE (value) == CONST_INT)
    {
      if (HOST_BITS_PER_WIDE_INT >= (2 * BITS_PER_WORD))
	{
	  /* In this case the CONST_INT holds both target words.
	     Extract the bits from it into two word-sized pieces.
	     Sign extend each half to HOST_WIDE_INT.  */
	  unsigned HOST_WIDE_INT low, high;
	  unsigned HOST_WIDE_INT mask, sign_bit, sign_extend;

	  /* Set sign_bit to the most significant bit of a word.  */
	  sign_bit = 1;
	  sign_bit <<= BITS_PER_WORD - 1;

	  /* Set mask so that all bits of the word are set.  We could
	     have used 1 << BITS_PER_WORD instead of basing the
	     calculation on sign_bit.  However, on machines where
	     HOST_BITS_PER_WIDE_INT == BITS_PER_WORD, it could cause a
	     compiler warning, even though the code would never be
	     executed.  */
	  mask = sign_bit << 1;
	  mask--;

	  /* Set sign_extend as any remaining bits.  */
	  sign_extend = ~mask;

	  /* Pick the lower word and sign-extend it.  */
	  low = INTVAL (value);
	  low &= mask;
	  if (low & sign_bit)
	    low |= sign_extend;

	  /* Pick the higher word, shifted to the least significant
	     bits, and sign-extend it.  */
	  high = INTVAL (value);
	  high >>= BITS_PER_WORD - 1;
	  high >>= 1;
	  high &= mask;
	  if (high & sign_bit)
	    high |= sign_extend;

	  /* Store the words in the target machine order.  */
	  if (WORDS_BIG_ENDIAN)
	    {
	      *first = GEN_INT (high);
	      *second = GEN_INT (low);
	    }
	  else
	    {
	      *first = GEN_INT (low);
	      *second = GEN_INT (high);
	    }
	}
      else
	{
	  /* The rule for using CONST_INT for a wider mode
	     is that we regard the value as signed.
	     So sign-extend it.  */
	  rtx high = (INTVAL (value) < 0 ? constm1_rtx : const0_rtx);
	  if (WORDS_BIG_ENDIAN)
	    {
	      *first = high;
	      *second = value;
	    }
	  else
	    {
	      *first = value;
	      *second = high;
	    }
	}
    }
  else if (GET_CODE (value) != CONST_DOUBLE)
    {
      if (WORDS_BIG_ENDIAN)
	{
	  *first = const0_rtx;
	  *second = value;
	}
      else
	{
	  *first = value;
	  *second = const0_rtx;
	}
    }
  else if (GET_MODE (value) == VOIDmode
	   /* This is the old way we did CONST_DOUBLE integers.  */
	   || GET_MODE_CLASS (GET_MODE (value)) == MODE_INT)
    {
      /* In an integer, the words are defined as most and least significant.
	 So order them by the target's convention.  */
      if (WORDS_BIG_ENDIAN)
	{
	  *first = GEN_INT (CONST_DOUBLE_HIGH (value));
	  *second = GEN_INT (CONST_DOUBLE_LOW (value));
	}
      else
	{
	  *first = GEN_INT (CONST_DOUBLE_LOW (value));
	  *second = GEN_INT (CONST_DOUBLE_HIGH (value));
	}
    }
  else
    {
      REAL_VALUE_TYPE r;
      long l[2];
      REAL_VALUE_FROM_CONST_DOUBLE (r, value);

      /* Note, this converts the REAL_VALUE_TYPE to the target's
	 format, splits up the floating point double and outputs
	 exactly 32 bits of it into each of l[0] and l[1] --
	 not necessarily BITS_PER_WORD bits.  */
      REAL_VALUE_TO_TARGET_DOUBLE (r, l);

      /* If 32 bits is an entire word for the target, but not for the host,
	 then sign-extend on the host so that the number will look the same
	 way on the host that it would on the target.  See for instance
	 simplify_unary_operation.  The #if is needed to avoid compiler
	 warnings.  */

#if HOST_BITS_PER_LONG > 32
      if (BITS_PER_WORD < HOST_BITS_PER_LONG && BITS_PER_WORD == 32)
	{
	  if (l[0] & ((long) 1 << 31))
	    l[0] |= ((long) (-1) << 32);
	  if (l[1] & ((long) 1 << 31))
	    l[1] |= ((long) (-1) << 32);
	}
#endif

      *first = GEN_INT (l[0]);
      *second = GEN_INT (l[1]);
    }
}

/* Return nonzero if this function has no function calls.  */

int
leaf_function_p (void)
{
  rtx insn;
  rtx link;

  if (current_function_profile || profile_arc_flag)
    return 0;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {
      if (CALL_P (insn)
	  && ! SIBLING_CALL_P (insn))
	return 0;
      if (NONJUMP_INSN_P (insn)
	  && GET_CODE (PATTERN (insn)) == SEQUENCE
	  && CALL_P (XVECEXP (PATTERN (insn), 0, 0))
	  && ! SIBLING_CALL_P (XVECEXP (PATTERN (insn), 0, 0)))
	return 0;
    }
  for (link = current_function_epilogue_delay_list;
       link;
       link = XEXP (link, 1))
    {
      insn = XEXP (link, 0);

      if (CALL_P (insn)
	  && ! SIBLING_CALL_P (insn))
	return 0;
      if (NONJUMP_INSN_P (insn)
	  && GET_CODE (PATTERN (insn)) == SEQUENCE
	  && CALL_P (XVECEXP (PATTERN (insn), 0, 0))
	  && ! SIBLING_CALL_P (XVECEXP (PATTERN (insn), 0, 0)))
	return 0;
    }

  return 1;
}

/* Return 1 if branch is a forward branch.
   Uses insn_shuid array, so it works only in the final pass.  May be used by
   output templates to customary add branch prediction hints.
 */
int
final_forward_branch_p (rtx insn)
{
  int insn_id, label_id;

  gcc_assert (uid_shuid);
  insn_id = INSN_SHUID (insn);
  label_id = INSN_SHUID (JUMP_LABEL (insn));
  /* We've hit some insns that does not have id information available.  */
  gcc_assert (insn_id && label_id);
  return insn_id < label_id;
}

/* On some machines, a function with no call insns
   can run faster if it doesn't create its own register window.
   When output, the leaf function should use only the "output"
   registers.  Ordinarily, the function would be compiled to use
   the "input" registers to find its arguments; it is a candidate
   for leaf treatment if it uses only the "input" registers.
   Leaf function treatment means renumbering so the function
   uses the "output" registers instead.  */

#ifdef LEAF_REGISTERS

/* Return 1 if this function uses only the registers that can be
   safely renumbered.  */

int
only_leaf_regs_used (void)
{
  int i;
  const char *const permitted_reg_in_leaf_functions = LEAF_REGISTERS;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if ((regs_ever_live[i] || global_regs[i])
	&& ! permitted_reg_in_leaf_functions[i])
      return 0;

  if (current_function_uses_pic_offset_table
      && pic_offset_table_rtx != 0
      && REG_P (pic_offset_table_rtx)
      && ! permitted_reg_in_leaf_functions[REGNO (pic_offset_table_rtx)])
    return 0;

  return 1;
}

/* Scan all instructions and renumber all registers into those
   available in leaf functions.  */

static void
leaf_renumber_regs (rtx first)
{
  rtx insn;

  /* Renumber only the actual patterns.
     The reg-notes can contain frame pointer refs,
     and renumbering them could crash, and should not be needed.  */
  for (insn = first; insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      leaf_renumber_regs_insn (PATTERN (insn));
  for (insn = current_function_epilogue_delay_list;
       insn;
       insn = XEXP (insn, 1))
    if (INSN_P (XEXP (insn, 0)))
      leaf_renumber_regs_insn (PATTERN (XEXP (insn, 0)));
}

/* Scan IN_RTX and its subexpressions, and renumber all regs into those
   available in leaf functions.  */

void
leaf_renumber_regs_insn (rtx in_rtx)
{
  int i, j;
  const char *format_ptr;

  if (in_rtx == 0)
    return;

  /* Renumber all input-registers into output-registers.
     renumbered_regs would be 1 for an output-register;
     they  */

  if (REG_P (in_rtx))
    {
      int newreg;

      /* Don't renumber the same reg twice.  */
      if (in_rtx->used)
	return;

      newreg = REGNO (in_rtx);
      /* Don't try to renumber pseudo regs.  It is possible for a pseudo reg
	 to reach here as part of a REG_NOTE.  */
      if (newreg >= FIRST_PSEUDO_REGISTER)
	{
	  in_rtx->used = 1;
	  return;
	}
      newreg = LEAF_REG_REMAP (newreg);
      gcc_assert (newreg >= 0);
      regs_ever_live[REGNO (in_rtx)] = 0;
      regs_ever_live[newreg] = 1;
      REGNO (in_rtx) = newreg;
      in_rtx->used = 1;
    }

  if (INSN_P (in_rtx))
    {
      /* Inside a SEQUENCE, we find insns.
	 Renumber just the patterns of these insns,
	 just as we do for the top-level insns.  */
      leaf_renumber_regs_insn (PATTERN (in_rtx));
      return;
    }

  format_ptr = GET_RTX_FORMAT (GET_CODE (in_rtx));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (in_rtx)); i++)
    switch (*format_ptr++)
      {
      case 'e':
	leaf_renumber_regs_insn (XEXP (in_rtx, i));
	break;

      case 'E':
	if (NULL != XVEC (in_rtx, i))
	  {
	    for (j = 0; j < XVECLEN (in_rtx, i); j++)
	      leaf_renumber_regs_insn (XVECEXP (in_rtx, i, j));
	  }
	break;

      case 'S':
      case 's':
      case '0':
      case 'i':
      case 'w':
      case 'n':
      case 'u':
	break;

      default:
	gcc_unreachable ();
      }
}
#endif


/* When -gused is used, emit debug info for only used symbols. But in
   addition to the standard intercepted debug_hooks there are some direct
   calls into this file, i.e., dbxout_symbol, dbxout_parms, and dbxout_reg_params.
   Those routines may also be called from a higher level intercepted routine. So
   to prevent recording data for an inner call to one of these for an intercept,
   we maintain an intercept nesting counter (debug_nesting). We only save the
   intercepted arguments if the nesting is 1.  */
int debug_nesting = 0;

static tree *symbol_queue;
int symbol_queue_index = 0;
static int symbol_queue_size = 0;

/* Generate the symbols for any queued up type symbols we encountered
   while generating the type info for some originally used symbol.
   This might generate additional entries in the queue.  Only when
   the nesting depth goes to 0 is this routine called.  */

void
debug_flush_symbol_queue (void)
{
  int i;

  /* Make sure that additionally queued items are not flushed
     prematurely.  */

  ++debug_nesting;

  for (i = 0; i < symbol_queue_index; ++i)
    {
      /* If we pushed queued symbols then such symbols must be
         output no matter what anyone else says.  Specifically,
         we need to make sure dbxout_symbol() thinks the symbol was
         used and also we need to override TYPE_DECL_SUPPRESS_DEBUG
         which may be set for outside reasons.  */
      int saved_tree_used = TREE_USED (symbol_queue[i]);
      int saved_suppress_debug = TYPE_DECL_SUPPRESS_DEBUG (symbol_queue[i]);
      TREE_USED (symbol_queue[i]) = 1;
      TYPE_DECL_SUPPRESS_DEBUG (symbol_queue[i]) = 0;

#ifdef DBX_DEBUGGING_INFO
      dbxout_symbol (symbol_queue[i], 0);
#endif

      TREE_USED (symbol_queue[i]) = saved_tree_used;
      TYPE_DECL_SUPPRESS_DEBUG (symbol_queue[i]) = saved_suppress_debug;
    }

  symbol_queue_index = 0;
  --debug_nesting;
}

/* Queue a type symbol needed as part of the definition of a decl
   symbol.  These symbols are generated when debug_flush_symbol_queue()
   is called.  */

void
debug_queue_symbol (tree decl)
{
  if (symbol_queue_index >= symbol_queue_size)
    {
      symbol_queue_size += 10;
      symbol_queue = xrealloc (symbol_queue,
			       symbol_queue_size * sizeof (tree));
    }

  symbol_queue[symbol_queue_index++] = decl;
}

/* Free symbol queue.  */
void
debug_free_queue (void)
{
  if (symbol_queue)
    {
      free (symbol_queue);
      symbol_queue = NULL;
      symbol_queue_size = 0;
    }
}

/* Turn the RTL into assembly.  */
static unsigned int
rest_of_handle_final (void)
{
  rtx x;
  const char *fnname;

  /* Get the function's name, as described by its RTL.  This may be
     different from the DECL_NAME name used in the source file.  */

  x = DECL_RTL (current_function_decl);
  gcc_assert (MEM_P (x));
  x = XEXP (x, 0);
  gcc_assert (GET_CODE (x) == SYMBOL_REF);
  fnname = XSTR (x, 0);

  assemble_start_function (current_function_decl, fnname);
  final_start_function (get_insns (), asm_out_file, optimize);
  final (get_insns (), asm_out_file, optimize);
  final_end_function ();

#ifdef TARGET_UNWIND_INFO
  /* ??? The IA-64 ".handlerdata" directive must be issued before
     the ".endp" directive that closes the procedure descriptor.  */
  output_function_exception_table ();
#endif

  assemble_end_function (current_function_decl, fnname);

#ifndef TARGET_UNWIND_INFO
  /* Otherwise, it feels unclean to switch sections in the middle.  */
  output_function_exception_table ();
#endif

  user_defined_section_attribute = false;

  if (! quiet_flag)
    fflush (asm_out_file);

  /* Release all memory allocated by flow.  */
  free_basic_block_vars ();

  /* Write DBX symbols if requested.  */

  /* Note that for those inline functions where we don't initially
     know for certain that we will be generating an out-of-line copy,
     the first invocation of this routine (rest_of_compilation) will
     skip over this code by doing a `goto exit_rest_of_compilation;'.
     Later on, wrapup_global_declarations will (indirectly) call
     rest_of_compilation again for those inline functions that need
     to have out-of-line copies generated.  During that call, we
     *will* be routed past here.  */

  timevar_push (TV_SYMOUT);
  (*debug_hooks->function_decl) (current_function_decl);
  timevar_pop (TV_SYMOUT);
  return 0;
}

struct tree_opt_pass pass_final =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  rest_of_handle_final,                 /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_FINAL,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_ggc_collect,                     /* todo_flags_finish */
  0                                     /* letter */
};


static unsigned int
rest_of_handle_shorten_branches (void)
{
  /* Shorten branches.  */
  shorten_branches (get_insns ());
  return 0;
}

struct tree_opt_pass pass_shorten_branches =
{
  "shorten",                            /* name */
  NULL,                                 /* gate */
  rest_of_handle_shorten_branches,      /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_FINAL,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  0                                     /* letter */
};


static unsigned int
rest_of_clean_state (void)
{
  rtx insn, next;

  /* It is very important to decompose the RTL instruction chain here:
     debug information keeps pointing into CODE_LABEL insns inside the function
     body.  If these remain pointing to the other insns, we end up preserving
     whole RTL chain and attached detailed debug info in memory.  */
  for (insn = get_insns (); insn; insn = next)
    {
      next = NEXT_INSN (insn);
      NEXT_INSN (insn) = NULL;
      PREV_INSN (insn) = NULL;
    }

  /* In case the function was not output,
     don't leave any temporary anonymous types
     queued up for sdb output.  */
#ifdef SDB_DEBUGGING_INFO
  if (write_symbols == SDB_DEBUG)
    sdbout_types (NULL_TREE);
#endif

  reload_completed = 0;
  epilogue_completed = 0;
  flow2_completed = 0;
  no_new_pseudos = 0;
#ifdef STACK_REGS
  regstack_completed = 0;
#endif

  /* Clear out the insn_length contents now that they are no
     longer valid.  */
  init_insn_lengths ();

  /* Show no temporary slots allocated.  */
  init_temp_slots ();

  free_basic_block_vars ();
  free_bb_for_insn ();


  if (targetm.binds_local_p (current_function_decl))
    {
      int pref = cfun->preferred_stack_boundary;
      if (cfun->stack_alignment_needed > cfun->preferred_stack_boundary)
        pref = cfun->stack_alignment_needed;
      cgraph_rtl_info (current_function_decl)->preferred_incoming_stack_boundary
        = pref;
    }

  /* Make sure volatile mem refs aren't considered valid operands for
     arithmetic insns.  We must call this here if this is a nested inline
     function, since the above code leaves us in the init_recog state,
     and the function context push/pop code does not save/restore volatile_ok.

     ??? Maybe it isn't necessary for expand_start_function to call this
     anymore if we do it here?  */

  init_recog_no_volatile ();

  /* We're done with this function.  Free up memory if we can.  */
  free_after_parsing (cfun);
  free_after_compilation (cfun);
  return 0;
}

struct tree_opt_pass pass_clean_state =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  rest_of_clean_state,                  /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_FINAL,                             /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  PROP_rtl,                             /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};

