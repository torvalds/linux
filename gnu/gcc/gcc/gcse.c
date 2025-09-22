/* Global common subexpression elimination/Partial redundancy elimination
   and global constant/copy propagation for GNU compiler.
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

/* TODO
   - reordering of memory allocation and freeing to be more space efficient
   - do rough calc of how many regs are needed in each block, and a rough
     calc of how many regs are available in each class and use that to
     throttle back the code in cases where RTX_COST is minimal.
   - a store to the same address as a load does not kill the load if the
     source of the store is also the destination of the load.  Handling this
     allows more load motion, particularly out of loops.
   - ability to realloc sbitmap vectors would allow one initial computation
     of reg_set_in_block with only subsequent additions, rather than
     recomputing it for each pass

*/

/* References searched while implementing this.

   Compilers Principles, Techniques and Tools
   Aho, Sethi, Ullman
   Addison-Wesley, 1988

   Global Optimization by Suppression of Partial Redundancies
   E. Morel, C. Renvoise
   communications of the acm, Vol. 22, Num. 2, Feb. 1979

   A Portable Machine-Independent Global Optimizer - Design and Measurements
   Frederick Chow
   Stanford Ph.D. thesis, Dec. 1983

   A Fast Algorithm for Code Movement Optimization
   D.M. Dhamdhere
   SIGPLAN Notices, Vol. 23, Num. 10, Oct. 1988

   A Solution to a Problem with Morel and Renvoise's
   Global Optimization by Suppression of Partial Redundancies
   K-H Drechsler, M.P. Stadel
   ACM TOPLAS, Vol. 10, Num. 4, Oct. 1988

   Practical Adaptation of the Global Optimization
   Algorithm of Morel and Renvoise
   D.M. Dhamdhere
   ACM TOPLAS, Vol. 13, Num. 2. Apr. 1991

   Efficiently Computing Static Single Assignment Form and the Control
   Dependence Graph
   R. Cytron, J. Ferrante, B.K. Rosen, M.N. Wegman, and F.K. Zadeck
   ACM TOPLAS, Vol. 13, Num. 4, Oct. 1991

   Lazy Code Motion
   J. Knoop, O. Ruthing, B. Steffen
   ACM SIGPLAN Notices Vol. 27, Num. 7, Jul. 1992, '92 Conference on PLDI

   What's In a Region?  Or Computing Control Dependence Regions in Near-Linear
   Time for Reducible Flow Control
   Thomas Ball
   ACM Letters on Programming Languages and Systems,
   Vol. 2, Num. 1-4, Mar-Dec 1993

   An Efficient Representation for Sparse Sets
   Preston Briggs, Linda Torczon
   ACM Letters on Programming Languages and Systems,
   Vol. 2, Num. 1-4, Mar-Dec 1993

   A Variation of Knoop, Ruthing, and Steffen's Lazy Code Motion
   K-H Drechsler, M.P. Stadel
   ACM SIGPLAN Notices, Vol. 28, Num. 5, May 1993

   Partial Dead Code Elimination
   J. Knoop, O. Ruthing, B. Steffen
   ACM SIGPLAN Notices, Vol. 29, Num. 6, Jun. 1994

   Effective Partial Redundancy Elimination
   P. Briggs, K.D. Cooper
   ACM SIGPLAN Notices, Vol. 29, Num. 6, Jun. 1994

   The Program Structure Tree: Computing Control Regions in Linear Time
   R. Johnson, D. Pearson, K. Pingali
   ACM SIGPLAN Notices, Vol. 29, Num. 6, Jun. 1994

   Optimal Code Motion: Theory and Practice
   J. Knoop, O. Ruthing, B. Steffen
   ACM TOPLAS, Vol. 16, Num. 4, Jul. 1994

   The power of assignment motion
   J. Knoop, O. Ruthing, B. Steffen
   ACM SIGPLAN Notices Vol. 30, Num. 6, Jun. 1995, '95 Conference on PLDI

   Global code motion / global value numbering
   C. Click
   ACM SIGPLAN Notices Vol. 30, Num. 6, Jun. 1995, '95 Conference on PLDI

   Value Driven Redundancy Elimination
   L.T. Simpson
   Rice University Ph.D. thesis, Apr. 1996

   Value Numbering
   L.T. Simpson
   Massively Scalar Compiler Project, Rice University, Sep. 1996

   High Performance Compilers for Parallel Computing
   Michael Wolfe
   Addison-Wesley, 1996

   Advanced Compiler Design and Implementation
   Steven Muchnick
   Morgan Kaufmann, 1997

   Building an Optimizing Compiler
   Robert Morgan
   Digital Press, 1998

   People wishing to speed up the code here should read:
     Elimination Algorithms for Data Flow Analysis
     B.G. Ryder, M.C. Paull
     ACM Computing Surveys, Vol. 18, Num. 3, Sep. 1986

     How to Analyze Large Programs Efficiently and Informatively
     D.M. Dhamdhere, B.K. Rosen, F.K. Zadeck
     ACM SIGPLAN Notices Vol. 27, Num. 7, Jul. 1992, '92 Conference on PLDI

   People wishing to do something different can find various possibilities
   in the above papers and elsewhere.
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"

#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "flags.h"
#include "real.h"
#include "insn-config.h"
#include "recog.h"
#include "basic-block.h"
#include "output.h"
#include "function.h"
#include "expr.h"
#include "except.h"
#include "ggc.h"
#include "params.h"
#include "cselib.h"
#include "intl.h"
#include "obstack.h"
#include "timevar.h"
#include "tree-pass.h"
#include "hashtab.h"

/* Propagate flow information through back edges and thus enable PRE's
   moving loop invariant calculations out of loops.

   Originally this tended to create worse overall code, but several
   improvements during the development of PRE seem to have made following
   back edges generally a win.

   Note much of the loop invariant code motion done here would normally
   be done by loop.c, which has more heuristics for when to move invariants
   out of loops.  At some point we might need to move some of those
   heuristics into gcse.c.  */

/* We support GCSE via Partial Redundancy Elimination.  PRE optimizations
   are a superset of those done by GCSE.

   We perform the following steps:

   1) Compute basic block information.

   2) Compute table of places where registers are set.

   3) Perform copy/constant propagation.

   4) Perform global cse using lazy code motion if not optimizing
      for size, or code hoisting if we are.

   5) Perform another pass of copy/constant propagation.

   Two passes of copy/constant propagation are done because the first one
   enables more GCSE and the second one helps to clean up the copies that
   GCSE creates.  This is needed more for PRE than for Classic because Classic
   GCSE will try to use an existing register containing the common
   subexpression rather than create a new one.  This is harder to do for PRE
   because of the code motion (which Classic GCSE doesn't do).

   Expressions we are interested in GCSE-ing are of the form
   (set (pseudo-reg) (expression)).
   Function want_to_gcse_p says what these are.

   PRE handles moving invariant expressions out of loops (by treating them as
   partially redundant).

   Eventually it would be nice to replace cse.c/gcse.c with SSA (static single
   assignment) based GVN (global value numbering).  L. T. Simpson's paper
   (Rice University) on value numbering is a useful reference for this.

   **********************

   We used to support multiple passes but there are diminishing returns in
   doing so.  The first pass usually makes 90% of the changes that are doable.
   A second pass can make a few more changes made possible by the first pass.
   Experiments show any further passes don't make enough changes to justify
   the expense.

   A study of spec92 using an unlimited number of passes:
   [1 pass] = 1208 substitutions, [2] = 577, [3] = 202, [4] = 192, [5] = 83,
   [6] = 34, [7] = 17, [8] = 9, [9] = 4, [10] = 4, [11] = 2,
   [12] = 2, [13] = 1, [15] = 1, [16] = 2, [41] = 1

   It was found doing copy propagation between each pass enables further
   substitutions.

   PRE is quite expensive in complicated functions because the DFA can take
   a while to converge.  Hence we only perform one pass.  The parameter
   max-gcse-passes can be modified if one wants to experiment.

   **********************

   The steps for PRE are:

   1) Build the hash table of expressions we wish to GCSE (expr_hash_table).

   2) Perform the data flow analysis for PRE.

   3) Delete the redundant instructions

   4) Insert the required copies [if any] that make the partially
      redundant instructions fully redundant.

   5) For other reaching expressions, insert an instruction to copy the value
      to a newly created pseudo that will reach the redundant instruction.

   The deletion is done first so that when we do insertions we
   know which pseudo reg to use.

   Various papers have argued that PRE DFA is expensive (O(n^2)) and others
   argue it is not.  The number of iterations for the algorithm to converge
   is typically 2-4 so I don't view it as that expensive (relatively speaking).

   PRE GCSE depends heavily on the second CSE pass to clean up the copies
   we create.  To make an expression reach the place where it's redundant,
   the result of the expression is copied to a new register, and the redundant
   expression is deleted by replacing it with this new register.  Classic GCSE
   doesn't have this problem as much as it computes the reaching defs of
   each register in each block and thus can try to use an existing
   register.  */

/* GCSE global vars.  */

/* Note whether or not we should run jump optimization after gcse.  We
   want to do this for two cases.

    * If we changed any jumps via cprop.

    * If we added any labels via edge splitting.  */
static int run_jump_opt_after_gcse;

/* An obstack for our working variables.  */
static struct obstack gcse_obstack;

struct reg_use {rtx reg_rtx; };

/* Hash table of expressions.  */

struct expr
{
  /* The expression (SET_SRC for expressions, PATTERN for assignments).  */
  rtx expr;
  /* Index in the available expression bitmaps.  */
  int bitmap_index;
  /* Next entry with the same hash.  */
  struct expr *next_same_hash;
  /* List of anticipatable occurrences in basic blocks in the function.
     An "anticipatable occurrence" is one that is the first occurrence in the
     basic block, the operands are not modified in the basic block prior
     to the occurrence and the output is not used between the start of
     the block and the occurrence.  */
  struct occr *antic_occr;
  /* List of available occurrence in basic blocks in the function.
     An "available occurrence" is one that is the last occurrence in the
     basic block and the operands are not modified by following statements in
     the basic block [including this insn].  */
  struct occr *avail_occr;
  /* Non-null if the computation is PRE redundant.
     The value is the newly created pseudo-reg to record a copy of the
     expression in all the places that reach the redundant copy.  */
  rtx reaching_reg;
};

/* Occurrence of an expression.
   There is one per basic block.  If a pattern appears more than once the
   last appearance is used [or first for anticipatable expressions].  */

struct occr
{
  /* Next occurrence of this expression.  */
  struct occr *next;
  /* The insn that computes the expression.  */
  rtx insn;
  /* Nonzero if this [anticipatable] occurrence has been deleted.  */
  char deleted_p;
  /* Nonzero if this [available] occurrence has been copied to
     reaching_reg.  */
  /* ??? This is mutually exclusive with deleted_p, so they could share
     the same byte.  */
  char copied_p;
};

/* Expression and copy propagation hash tables.
   Each hash table is an array of buckets.
   ??? It is known that if it were an array of entries, structure elements
   `next_same_hash' and `bitmap_index' wouldn't be necessary.  However, it is
   not clear whether in the final analysis a sufficient amount of memory would
   be saved as the size of the available expression bitmaps would be larger
   [one could build a mapping table without holes afterwards though].
   Someday I'll perform the computation and figure it out.  */

struct hash_table
{
  /* The table itself.
     This is an array of `expr_hash_table_size' elements.  */
  struct expr **table;

  /* Size of the hash table, in elements.  */
  unsigned int size;

  /* Number of hash table elements.  */
  unsigned int n_elems;

  /* Whether the table is expression of copy propagation one.  */
  int set_p;
};

/* Expression hash table.  */
static struct hash_table expr_hash_table;

/* Copy propagation hash table.  */
static struct hash_table set_hash_table;

/* Mapping of uids to cuids.
   Only real insns get cuids.  */
static int *uid_cuid;

/* Highest UID in UID_CUID.  */
static int max_uid;

/* Get the cuid of an insn.  */
#ifdef ENABLE_CHECKING
#define INSN_CUID(INSN) \
  (gcc_assert (INSN_UID (INSN) <= max_uid), uid_cuid[INSN_UID (INSN)])
#else
#define INSN_CUID(INSN) (uid_cuid[INSN_UID (INSN)])
#endif

/* Number of cuids.  */
static int max_cuid;

/* Mapping of cuids to insns.  */
static rtx *cuid_insn;

/* Get insn from cuid.  */
#define CUID_INSN(CUID) (cuid_insn[CUID])

/* Maximum register number in function prior to doing gcse + 1.
   Registers created during this pass have regno >= max_gcse_regno.
   This is named with "gcse" to not collide with global of same name.  */
static unsigned int max_gcse_regno;

/* Table of registers that are modified.

   For each register, each element is a list of places where the pseudo-reg
   is set.

   For simplicity, GCSE is done on sets of pseudo-regs only.  PRE GCSE only
   requires knowledge of which blocks kill which regs [and thus could use
   a bitmap instead of the lists `reg_set_table' uses].

   `reg_set_table' and could be turned into an array of bitmaps (num-bbs x
   num-regs) [however perhaps it may be useful to keep the data as is].  One
   advantage of recording things this way is that `reg_set_table' is fairly
   sparse with respect to pseudo regs but for hard regs could be fairly dense
   [relatively speaking].  And recording sets of pseudo-regs in lists speeds
   up functions like compute_transp since in the case of pseudo-regs we only
   need to iterate over the number of times a pseudo-reg is set, not over the
   number of basic blocks [clearly there is a bit of a slow down in the cases
   where a pseudo is set more than once in a block, however it is believed
   that the net effect is to speed things up].  This isn't done for hard-regs
   because recording call-clobbered hard-regs in `reg_set_table' at each
   function call can consume a fair bit of memory, and iterating over
   hard-regs stored this way in compute_transp will be more expensive.  */

typedef struct reg_set
{
  /* The next setting of this register.  */
  struct reg_set *next;
  /* The index of the block where it was set.  */
  int bb_index;
} reg_set;

static reg_set **reg_set_table;

/* Size of `reg_set_table'.
   The table starts out at max_gcse_regno + slop, and is enlarged as
   necessary.  */
static int reg_set_table_size;

/* Amount to grow `reg_set_table' by when it's full.  */
#define REG_SET_TABLE_SLOP 100

/* This is a list of expressions which are MEMs and will be used by load
   or store motion.
   Load motion tracks MEMs which aren't killed by
   anything except itself. (i.e., loads and stores to a single location).
   We can then allow movement of these MEM refs with a little special
   allowance. (all stores copy the same value to the reaching reg used
   for the loads).  This means all values used to store into memory must have
   no side effects so we can re-issue the setter value.
   Store Motion uses this structure as an expression table to track stores
   which look interesting, and might be moveable towards the exit block.  */

struct ls_expr
{
  struct expr * expr;		/* Gcse expression reference for LM.  */
  rtx pattern;			/* Pattern of this mem.  */
  rtx pattern_regs;		/* List of registers mentioned by the mem.  */
  rtx loads;			/* INSN list of loads seen.  */
  rtx stores;			/* INSN list of stores seen.  */
  struct ls_expr * next;	/* Next in the list.  */
  int invalid;			/* Invalid for some reason.  */
  int index;			/* If it maps to a bitmap index.  */
  unsigned int hash_index;	/* Index when in a hash table.  */
  rtx reaching_reg;		/* Register to use when re-writing.  */
};

/* Array of implicit set patterns indexed by basic block index.  */
static rtx *implicit_sets;

/* Head of the list of load/store memory refs.  */
static struct ls_expr * pre_ldst_mems = NULL;

/* Hashtable for the load/store memory refs.  */
static htab_t pre_ldst_table = NULL;

/* Bitmap containing one bit for each register in the program.
   Used when performing GCSE to track which registers have been set since
   the start of the basic block.  */
static regset reg_set_bitmap;

/* For each block, a bitmap of registers set in the block.
   This is used by compute_transp.
   It is computed during hash table computation and not by compute_sets
   as it includes registers added since the last pass (or between cprop and
   gcse) and it's currently not easy to realloc sbitmap vectors.  */
static sbitmap *reg_set_in_block;

/* Array, indexed by basic block number for a list of insns which modify
   memory within that block.  */
static rtx * modify_mem_list;
static bitmap modify_mem_list_set;

/* This array parallels modify_mem_list, but is kept canonicalized.  */
static rtx * canon_modify_mem_list;

/* Bitmap indexed by block numbers to record which blocks contain
   function calls.  */
static bitmap blocks_with_calls;

/* Various variables for statistics gathering.  */

/* Memory used in a pass.
   This isn't intended to be absolutely precise.  Its intent is only
   to keep an eye on memory usage.  */
static int bytes_used;

/* GCSE substitutions made.  */
static int gcse_subst_count;
/* Number of copy instructions created.  */
static int gcse_create_count;
/* Number of local constants propagated.  */
static int local_const_prop_count;
/* Number of local copies propagated.  */
static int local_copy_prop_count;
/* Number of global constants propagated.  */
static int global_const_prop_count;
/* Number of global copies propagated.  */
static int global_copy_prop_count;

/* For available exprs */
static sbitmap *ae_kill, *ae_gen;

static void compute_can_copy (void);
static void *gmalloc (size_t) ATTRIBUTE_MALLOC;
static void *gcalloc (size_t, size_t) ATTRIBUTE_MALLOC;
static void *grealloc (void *, size_t);
static void *gcse_alloc (unsigned long);
static void alloc_gcse_mem (void);
static void free_gcse_mem (void);
static void alloc_reg_set_mem (int);
static void free_reg_set_mem (void);
static void record_one_set (int, rtx);
static void record_set_info (rtx, rtx, void *);
static void compute_sets (void);
static void hash_scan_insn (rtx, struct hash_table *, int);
static void hash_scan_set (rtx, rtx, struct hash_table *);
static void hash_scan_clobber (rtx, rtx, struct hash_table *);
static void hash_scan_call (rtx, rtx, struct hash_table *);
static int want_to_gcse_p (rtx);
static bool can_assign_to_reg_p (rtx);
static bool gcse_constant_p (rtx);
static int oprs_unchanged_p (rtx, rtx, int);
static int oprs_anticipatable_p (rtx, rtx);
static int oprs_available_p (rtx, rtx);
static void insert_expr_in_table (rtx, enum machine_mode, rtx, int, int,
				  struct hash_table *);
static void insert_set_in_table (rtx, rtx, struct hash_table *);
static unsigned int hash_expr (rtx, enum machine_mode, int *, int);
static unsigned int hash_set (int, int);
static int expr_equiv_p (rtx, rtx);
static void record_last_reg_set_info (rtx, int);
static void record_last_mem_set_info (rtx);
static void record_last_set_info (rtx, rtx, void *);
static void compute_hash_table (struct hash_table *);
static void alloc_hash_table (int, struct hash_table *, int);
static void free_hash_table (struct hash_table *);
static void compute_hash_table_work (struct hash_table *);
static void dump_hash_table (FILE *, const char *, struct hash_table *);
static struct expr *lookup_set (unsigned int, struct hash_table *);
static struct expr *next_set (unsigned int, struct expr *);
static void reset_opr_set_tables (void);
static int oprs_not_set_p (rtx, rtx);
static void mark_call (rtx);
static void mark_set (rtx, rtx);
static void mark_clobber (rtx, rtx);
static void mark_oprs_set (rtx);
static void alloc_cprop_mem (int, int);
static void free_cprop_mem (void);
static void compute_transp (rtx, int, sbitmap *, int);
static void compute_transpout (void);
static void compute_local_properties (sbitmap *, sbitmap *, sbitmap *,
				      struct hash_table *);
static void compute_cprop_data (void);
static void find_used_regs (rtx *, void *);
static int try_replace_reg (rtx, rtx, rtx);
static struct expr *find_avail_set (int, rtx);
static int cprop_jump (basic_block, rtx, rtx, rtx, rtx);
static void mems_conflict_for_gcse_p (rtx, rtx, void *);
static int load_killed_in_block_p (basic_block, int, rtx, int);
static void canon_list_insert (rtx, rtx, void *);
static int cprop_insn (rtx, int);
static int cprop (int);
static void find_implicit_sets (void);
static int one_cprop_pass (int, bool, bool);
static bool constprop_register (rtx, rtx, rtx, bool);
static struct expr *find_bypass_set (int, int);
static bool reg_killed_on_edge (rtx, edge);
static int bypass_block (basic_block, rtx, rtx);
static int bypass_conditional_jumps (void);
static void alloc_pre_mem (int, int);
static void free_pre_mem (void);
static void compute_pre_data (void);
static int pre_expr_reaches_here_p (basic_block, struct expr *,
				    basic_block);
static void insert_insn_end_bb (struct expr *, basic_block, int);
static void pre_insert_copy_insn (struct expr *, rtx);
static void pre_insert_copies (void);
static int pre_delete (void);
static int pre_gcse (void);
static int one_pre_gcse_pass (int);
static void add_label_notes (rtx, rtx);
static void alloc_code_hoist_mem (int, int);
static void free_code_hoist_mem (void);
static void compute_code_hoist_vbeinout (void);
static void compute_code_hoist_data (void);
static int hoist_expr_reaches_here_p (basic_block, int, basic_block, char *);
static void hoist_code (void);
static int one_code_hoisting_pass (void);
static rtx process_insert_insn (struct expr *);
static int pre_edge_insert (struct edge_list *, struct expr **);
static int pre_expr_reaches_here_p_work (basic_block, struct expr *,
					 basic_block, char *);
static struct ls_expr * ldst_entry (rtx);
static void free_ldst_entry (struct ls_expr *);
static void free_ldst_mems (void);
static void print_ldst_list (FILE *);
static struct ls_expr * find_rtx_in_ldst (rtx);
static int enumerate_ldsts (void);
static inline struct ls_expr * first_ls_expr (void);
static inline struct ls_expr * next_ls_expr (struct ls_expr *);
static int simple_mem (rtx);
static void invalidate_any_buried_refs (rtx);
static void compute_ld_motion_mems (void);
static void trim_ld_motion_mems (void);
static void update_ld_motion_stores (struct expr *);
static void reg_set_info (rtx, rtx, void *);
static void reg_clear_last_set (rtx, rtx, void *);
static bool store_ops_ok (rtx, int *);
static rtx extract_mentioned_regs (rtx);
static rtx extract_mentioned_regs_helper (rtx, rtx);
static void find_moveable_store (rtx, int *, int *);
static int compute_store_table (void);
static bool load_kills_store (rtx, rtx, int);
static bool find_loads (rtx, rtx, int);
static bool store_killed_in_insn (rtx, rtx, rtx, int);
static bool store_killed_after (rtx, rtx, rtx, basic_block, int *, rtx *);
static bool store_killed_before (rtx, rtx, rtx, basic_block, int *);
static void build_store_vectors (void);
static void insert_insn_start_bb (rtx, basic_block);
static int insert_store (struct ls_expr *, edge);
static void remove_reachable_equiv_notes (basic_block, struct ls_expr *);
static void replace_store_insn (rtx, rtx, basic_block, struct ls_expr *);
static void delete_store (struct ls_expr *, basic_block);
static void free_store_memory (void);
static void store_motion (void);
static void free_insn_expr_list_list (rtx *);
static void clear_modify_mem_tables (void);
static void free_modify_mem_tables (void);
static rtx gcse_emit_move_after (rtx, rtx, rtx);
static void local_cprop_find_used_regs (rtx *, void *);
static bool do_local_cprop (rtx, rtx, bool, rtx*);
static bool adjust_libcall_notes (rtx, rtx, rtx, rtx*);
static void local_cprop_pass (bool);
static bool is_too_expensive (const char *);


/* Entry point for global common subexpression elimination.
   F is the first instruction in the function.  Return nonzero if a
   change is mode.  */

static int
gcse_main (rtx f ATTRIBUTE_UNUSED)
{
  int changed, pass;
  /* Bytes used at start of pass.  */
  int initial_bytes_used;
  /* Maximum number of bytes used by a pass.  */
  int max_pass_bytes;
  /* Point to release obstack data from for each pass.  */
  char *gcse_obstack_bottom;

  /* We do not construct an accurate cfg in functions which call
     setjmp, so just punt to be safe.  */
  if (current_function_calls_setjmp)
    return 0;

  /* Assume that we do not need to run jump optimizations after gcse.  */
  run_jump_opt_after_gcse = 0;

  /* Identify the basic block information for this function, including
     successors and predecessors.  */
  max_gcse_regno = max_reg_num ();

  if (dump_file)
    dump_flow_info (dump_file, dump_flags);

  /* Return if there's nothing to do, or it is too expensive.  */
  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1
      || is_too_expensive (_("GCSE disabled")))
    return 0;

  gcc_obstack_init (&gcse_obstack);
  bytes_used = 0;

  /* We need alias.  */
  init_alias_analysis ();
  /* Record where pseudo-registers are set.  This data is kept accurate
     during each pass.  ??? We could also record hard-reg information here
     [since it's unchanging], however it is currently done during hash table
     computation.

     It may be tempting to compute MEM set information here too, but MEM sets
     will be subject to code motion one day and thus we need to compute
     information about memory sets when we build the hash tables.  */

  alloc_reg_set_mem (max_gcse_regno);
  compute_sets ();

  pass = 0;
  initial_bytes_used = bytes_used;
  max_pass_bytes = 0;
  gcse_obstack_bottom = gcse_alloc (1);
  changed = 1;
  while (changed && pass < MAX_GCSE_PASSES)
    {
      changed = 0;
      if (dump_file)
	fprintf (dump_file, "GCSE pass %d\n\n", pass + 1);

      /* Initialize bytes_used to the space for the pred/succ lists,
	 and the reg_set_table data.  */
      bytes_used = initial_bytes_used;

      /* Each pass may create new registers, so recalculate each time.  */
      max_gcse_regno = max_reg_num ();

      alloc_gcse_mem ();

      /* Don't allow constant propagation to modify jumps
	 during this pass.  */
      timevar_push (TV_CPROP1);
      changed = one_cprop_pass (pass + 1, false, false);
      timevar_pop (TV_CPROP1);

      if (optimize_size)
	/* Do nothing.  */ ;
      else
	{
	  timevar_push (TV_PRE);
	  changed |= one_pre_gcse_pass (pass + 1);
	  /* We may have just created new basic blocks.  Release and
	     recompute various things which are sized on the number of
	     basic blocks.  */
	  if (changed)
	    {
	      free_modify_mem_tables ();
	      modify_mem_list = gcalloc (last_basic_block, sizeof (rtx));
	      canon_modify_mem_list = gcalloc (last_basic_block, sizeof (rtx));
	    }
	  free_reg_set_mem ();
	  alloc_reg_set_mem (max_reg_num ());
	  compute_sets ();
	  run_jump_opt_after_gcse = 1;
	  timevar_pop (TV_PRE);
	}

      if (max_pass_bytes < bytes_used)
	max_pass_bytes = bytes_used;

      /* Free up memory, then reallocate for code hoisting.  We can
	 not re-use the existing allocated memory because the tables
	 will not have info for the insns or registers created by
	 partial redundancy elimination.  */
      free_gcse_mem ();

      /* It does not make sense to run code hoisting unless we are optimizing
	 for code size -- it rarely makes programs faster, and can make
	 them bigger if we did partial redundancy elimination (when optimizing
	 for space, we don't run the partial redundancy algorithms).  */
      if (optimize_size)
	{
	  timevar_push (TV_HOIST);
	  max_gcse_regno = max_reg_num ();
	  alloc_gcse_mem ();
	  changed |= one_code_hoisting_pass ();
	  free_gcse_mem ();

	  if (max_pass_bytes < bytes_used)
	    max_pass_bytes = bytes_used;
	  timevar_pop (TV_HOIST);
	}

      if (dump_file)
	{
	  fprintf (dump_file, "\n");
	  fflush (dump_file);
	}

      obstack_free (&gcse_obstack, gcse_obstack_bottom);
      pass++;
    }

  /* Do one last pass of copy propagation, including cprop into
     conditional jumps.  */

  max_gcse_regno = max_reg_num ();
  alloc_gcse_mem ();
  /* This time, go ahead and allow cprop to alter jumps.  */
  timevar_push (TV_CPROP2);
  one_cprop_pass (pass + 1, true, false);
  timevar_pop (TV_CPROP2);
  free_gcse_mem ();

  if (dump_file)
    {
      fprintf (dump_file, "GCSE of %s: %d basic blocks, ",
	       current_function_name (), n_basic_blocks);
      fprintf (dump_file, "%d pass%s, %d bytes\n\n",
	       pass, pass > 1 ? "es" : "", max_pass_bytes);
    }

  obstack_free (&gcse_obstack, NULL);
  free_reg_set_mem ();

  /* We are finished with alias.  */
  end_alias_analysis ();
  allocate_reg_info (max_reg_num (), FALSE, FALSE);

  if (!optimize_size && flag_gcse_sm)
    {
      timevar_push (TV_LSM);
      store_motion ();
      timevar_pop (TV_LSM);
    }

  /* Record where pseudo-registers are set.  */
  return run_jump_opt_after_gcse;
}

/* Misc. utilities.  */

/* Nonzero for each mode that supports (set (reg) (reg)).
   This is trivially true for integer and floating point values.
   It may or may not be true for condition codes.  */
static char can_copy[(int) NUM_MACHINE_MODES];

/* Compute which modes support reg/reg copy operations.  */

static void
compute_can_copy (void)
{
  int i;
#ifndef AVOID_CCMODE_COPIES
  rtx reg, insn;
#endif
  memset (can_copy, 0, NUM_MACHINE_MODES);

  start_sequence ();
  for (i = 0; i < NUM_MACHINE_MODES; i++)
    if (GET_MODE_CLASS (i) == MODE_CC)
      {
#ifdef AVOID_CCMODE_COPIES
	can_copy[i] = 0;
#else
	reg = gen_rtx_REG ((enum machine_mode) i, LAST_VIRTUAL_REGISTER + 1);
	insn = emit_insn (gen_rtx_SET (VOIDmode, reg, reg));
	if (recog (PATTERN (insn), insn, NULL) >= 0)
	  can_copy[i] = 1;
#endif
      }
    else
      can_copy[i] = 1;

  end_sequence ();
}

/* Returns whether the mode supports reg/reg copy operations.  */

bool
can_copy_p (enum machine_mode mode)
{
  static bool can_copy_init_p = false;

  if (! can_copy_init_p)
    {
      compute_can_copy ();
      can_copy_init_p = true;
    }

  return can_copy[mode] != 0;
}

/* Cover function to xmalloc to record bytes allocated.  */

static void *
gmalloc (size_t size)
{
  bytes_used += size;
  return xmalloc (size);
}

/* Cover function to xcalloc to record bytes allocated.  */

static void *
gcalloc (size_t nelem, size_t elsize)
{
  bytes_used += nelem * elsize;
  return xcalloc (nelem, elsize);
}

/* Cover function to xrealloc.
   We don't record the additional size since we don't know it.
   It won't affect memory usage stats much anyway.  */

static void *
grealloc (void *ptr, size_t size)
{
  return xrealloc (ptr, size);
}

/* Cover function to obstack_alloc.  */

static void *
gcse_alloc (unsigned long size)
{
  bytes_used += size;
  return obstack_alloc (&gcse_obstack, size);
}

/* Allocate memory for the cuid mapping array,
   and reg/memory set tracking tables.

   This is called at the start of each pass.  */

static void
alloc_gcse_mem (void)
{
  int i;
  basic_block bb;
  rtx insn;

  /* Find the largest UID and create a mapping from UIDs to CUIDs.
     CUIDs are like UIDs except they increase monotonically, have no gaps,
     and only apply to real insns.
     (Actually, there are gaps, for insn that are not inside a basic block.
     but we should never see those anyway, so this is OK.)  */

  max_uid = get_max_uid ();
  uid_cuid = gcalloc (max_uid + 1, sizeof (int));
  i = 0;
  FOR_EACH_BB (bb)
    FOR_BB_INSNS (bb, insn)
      {
	if (INSN_P (insn))
	  uid_cuid[INSN_UID (insn)] = i++;
	else
	  uid_cuid[INSN_UID (insn)] = i;
      }

  /* Create a table mapping cuids to insns.  */

  max_cuid = i;
  cuid_insn = gcalloc (max_cuid + 1, sizeof (rtx));
  i = 0;
  FOR_EACH_BB (bb)
    FOR_BB_INSNS (bb, insn)
      if (INSN_P (insn))
	CUID_INSN (i++) = insn;

  /* Allocate vars to track sets of regs.  */
  reg_set_bitmap = BITMAP_ALLOC (NULL);

  /* Allocate vars to track sets of regs, memory per block.  */
  reg_set_in_block = sbitmap_vector_alloc (last_basic_block, max_gcse_regno);
  /* Allocate array to keep a list of insns which modify memory in each
     basic block.  */
  modify_mem_list = gcalloc (last_basic_block, sizeof (rtx));
  canon_modify_mem_list = gcalloc (last_basic_block, sizeof (rtx));
  modify_mem_list_set = BITMAP_ALLOC (NULL);
  blocks_with_calls = BITMAP_ALLOC (NULL);
}

/* Free memory allocated by alloc_gcse_mem.  */

static void
free_gcse_mem (void)
{
  free (uid_cuid);
  free (cuid_insn);

  BITMAP_FREE (reg_set_bitmap);

  sbitmap_vector_free (reg_set_in_block);
  free_modify_mem_tables ();
  BITMAP_FREE (modify_mem_list_set);
  BITMAP_FREE (blocks_with_calls);
}

/* Compute the local properties of each recorded expression.

   Local properties are those that are defined by the block, irrespective of
   other blocks.

   An expression is transparent in a block if its operands are not modified
   in the block.

   An expression is computed (locally available) in a block if it is computed
   at least once and expression would contain the same value if the
   computation was moved to the end of the block.

   An expression is locally anticipatable in a block if it is computed at
   least once and expression would contain the same value if the computation
   was moved to the beginning of the block.

   We call this routine for cprop, pre and code hoisting.  They all compute
   basically the same information and thus can easily share this code.

   TRANSP, COMP, and ANTLOC are destination sbitmaps for recording local
   properties.  If NULL, then it is not necessary to compute or record that
   particular property.

   TABLE controls which hash table to look at.  If it is  set hash table,
   additionally, TRANSP is computed as ~TRANSP, since this is really cprop's
   ABSALTERED.  */

static void
compute_local_properties (sbitmap *transp, sbitmap *comp, sbitmap *antloc,
			  struct hash_table *table)
{
  unsigned int i;

  /* Initialize any bitmaps that were passed in.  */
  if (transp)
    {
      if (table->set_p)
	sbitmap_vector_zero (transp, last_basic_block);
      else
	sbitmap_vector_ones (transp, last_basic_block);
    }

  if (comp)
    sbitmap_vector_zero (comp, last_basic_block);
  if (antloc)
    sbitmap_vector_zero (antloc, last_basic_block);

  for (i = 0; i < table->size; i++)
    {
      struct expr *expr;

      for (expr = table->table[i]; expr != NULL; expr = expr->next_same_hash)
	{
	  int indx = expr->bitmap_index;
	  struct occr *occr;

	  /* The expression is transparent in this block if it is not killed.
	     We start by assuming all are transparent [none are killed], and
	     then reset the bits for those that are.  */
	  if (transp)
	    compute_transp (expr->expr, indx, transp, table->set_p);

	  /* The occurrences recorded in antic_occr are exactly those that
	     we want to set to nonzero in ANTLOC.  */
	  if (antloc)
	    for (occr = expr->antic_occr; occr != NULL; occr = occr->next)
	      {
		SET_BIT (antloc[BLOCK_NUM (occr->insn)], indx);

		/* While we're scanning the table, this is a good place to
		   initialize this.  */
		occr->deleted_p = 0;
	      }

	  /* The occurrences recorded in avail_occr are exactly those that
	     we want to set to nonzero in COMP.  */
	  if (comp)
	    for (occr = expr->avail_occr; occr != NULL; occr = occr->next)
	      {
		SET_BIT (comp[BLOCK_NUM (occr->insn)], indx);

		/* While we're scanning the table, this is a good place to
		   initialize this.  */
		occr->copied_p = 0;
	      }

	  /* While we're scanning the table, this is a good place to
	     initialize this.  */
	  expr->reaching_reg = 0;
	}
    }
}

/* Register set information.

   `reg_set_table' records where each register is set or otherwise
   modified.  */

static struct obstack reg_set_obstack;

static void
alloc_reg_set_mem (int n_regs)
{
  reg_set_table_size = n_regs + REG_SET_TABLE_SLOP;
  reg_set_table = gcalloc (reg_set_table_size, sizeof (struct reg_set *));

  gcc_obstack_init (&reg_set_obstack);
}

static void
free_reg_set_mem (void)
{
  free (reg_set_table);
  obstack_free (&reg_set_obstack, NULL);
}

/* Record REGNO in the reg_set table.  */

static void
record_one_set (int regno, rtx insn)
{
  /* Allocate a new reg_set element and link it onto the list.  */
  struct reg_set *new_reg_info;

  /* If the table isn't big enough, enlarge it.  */
  if (regno >= reg_set_table_size)
    {
      int new_size = regno + REG_SET_TABLE_SLOP;

      reg_set_table = grealloc (reg_set_table,
				new_size * sizeof (struct reg_set *));
      memset (reg_set_table + reg_set_table_size, 0,
	      (new_size - reg_set_table_size) * sizeof (struct reg_set *));
      reg_set_table_size = new_size;
    }

  new_reg_info = obstack_alloc (&reg_set_obstack, sizeof (struct reg_set));
  bytes_used += sizeof (struct reg_set);
  new_reg_info->bb_index = BLOCK_NUM (insn);
  new_reg_info->next = reg_set_table[regno];
  reg_set_table[regno] = new_reg_info;
}

/* Called from compute_sets via note_stores to handle one SET or CLOBBER in
   an insn.  The DATA is really the instruction in which the SET is
   occurring.  */

static void
record_set_info (rtx dest, rtx setter ATTRIBUTE_UNUSED, void *data)
{
  rtx record_set_insn = (rtx) data;

  if (REG_P (dest) && REGNO (dest) >= FIRST_PSEUDO_REGISTER)
    record_one_set (REGNO (dest), record_set_insn);
}

/* Scan the function and record each set of each pseudo-register.

   This is called once, at the start of the gcse pass.  See the comments for
   `reg_set_table' for further documentation.  */

static void
compute_sets (void)
{
  basic_block bb;
  rtx insn;

  FOR_EACH_BB (bb)
    FOR_BB_INSNS (bb, insn)
      if (INSN_P (insn))
	note_stores (PATTERN (insn), record_set_info, insn);
}

/* Hash table support.  */

struct reg_avail_info
{
  basic_block last_bb;
  int first_set;
  int last_set;
};

static struct reg_avail_info *reg_avail_info;
static basic_block current_bb;


/* See whether X, the source of a set, is something we want to consider for
   GCSE.  */

static int
want_to_gcse_p (rtx x)
{
#ifdef STACK_REGS
  /* On register stack architectures, don't GCSE constants from the
     constant pool, as the benefits are often swamped by the overhead
     of shuffling the register stack between basic blocks.  */
  if (IS_STACK_MODE (GET_MODE (x)))
    x = avoid_constant_pool_reference (x);
#endif

  switch (GET_CODE (x))
    {
    case REG:
    case SUBREG:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case CALL:
      return 0;

    default:
      return can_assign_to_reg_p (x);
    }
}

/* Used internally by can_assign_to_reg_p.  */

static GTY(()) rtx test_insn;

/* Return true if we can assign X to a pseudo register.  */

static bool
can_assign_to_reg_p (rtx x)
{
  int num_clobbers = 0;
  int icode;

  /* If this is a valid operand, we are OK.  If it's VOIDmode, we aren't.  */
  if (general_operand (x, GET_MODE (x)))
    return 1;
  else if (GET_MODE (x) == VOIDmode)
    return 0;

  /* Otherwise, check if we can make a valid insn from it.  First initialize
     our test insn if we haven't already.  */
  if (test_insn == 0)
    {
      test_insn
	= make_insn_raw (gen_rtx_SET (VOIDmode,
				      gen_rtx_REG (word_mode,
						   FIRST_PSEUDO_REGISTER * 2),
				      const0_rtx));
      NEXT_INSN (test_insn) = PREV_INSN (test_insn) = 0;
    }

  /* Now make an insn like the one we would make when GCSE'ing and see if
     valid.  */
  PUT_MODE (SET_DEST (PATTERN (test_insn)), GET_MODE (x));
  SET_SRC (PATTERN (test_insn)) = x;
  return ((icode = recog (PATTERN (test_insn), test_insn, &num_clobbers)) >= 0
	  && (num_clobbers == 0 || ! added_clobbers_hard_reg_p (icode)));
}

/* Return nonzero if the operands of expression X are unchanged from the
   start of INSN's basic block up to but not including INSN (if AVAIL_P == 0),
   or from INSN to the end of INSN's basic block (if AVAIL_P != 0).  */

static int
oprs_unchanged_p (rtx x, rtx insn, int avail_p)
{
  int i, j;
  enum rtx_code code;
  const char *fmt;

  if (x == 0)
    return 1;

  code = GET_CODE (x);
  switch (code)
    {
    case REG:
      {
	struct reg_avail_info *info = &reg_avail_info[REGNO (x)];

	if (info->last_bb != current_bb)
	  return 1;
	if (avail_p)
	  return info->last_set < INSN_CUID (insn);
	else
	  return info->first_set >= INSN_CUID (insn);
      }

    case MEM:
      if (load_killed_in_block_p (current_bb, INSN_CUID (insn),
				  x, avail_p))
	return 0;
      else
	return oprs_unchanged_p (XEXP (x, 0), insn, avail_p);

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
    case PRE_MODIFY:
    case POST_MODIFY:
      return 0;

    case PC:
    case CC0: /*FIXME*/
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return 1;

    default:
      break;
    }

  for (i = GET_RTX_LENGTH (code) - 1, fmt = GET_RTX_FORMAT (code); i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  /* If we are about to do the last recursive call needed at this
	     level, change it into iteration.  This function is called enough
	     to be worth it.  */
	  if (i == 0)
	    return oprs_unchanged_p (XEXP (x, i), insn, avail_p);

	  else if (! oprs_unchanged_p (XEXP (x, i), insn, avail_p))
	    return 0;
	}
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (! oprs_unchanged_p (XVECEXP (x, i, j), insn, avail_p))
	    return 0;
    }

  return 1;
}

/* Used for communication between mems_conflict_for_gcse_p and
   load_killed_in_block_p.  Nonzero if mems_conflict_for_gcse_p finds a
   conflict between two memory references.  */
static int gcse_mems_conflict_p;

/* Used for communication between mems_conflict_for_gcse_p and
   load_killed_in_block_p.  A memory reference for a load instruction,
   mems_conflict_for_gcse_p will see if a memory store conflicts with
   this memory load.  */
static rtx gcse_mem_operand;

/* DEST is the output of an instruction.  If it is a memory reference, and
   possibly conflicts with the load found in gcse_mem_operand, then set
   gcse_mems_conflict_p to a nonzero value.  */

static void
mems_conflict_for_gcse_p (rtx dest, rtx setter ATTRIBUTE_UNUSED,
			  void *data ATTRIBUTE_UNUSED)
{
  while (GET_CODE (dest) == SUBREG
	 || GET_CODE (dest) == ZERO_EXTRACT
	 || GET_CODE (dest) == STRICT_LOW_PART)
    dest = XEXP (dest, 0);

  /* If DEST is not a MEM, then it will not conflict with the load.  Note
     that function calls are assumed to clobber memory, but are handled
     elsewhere.  */
  if (! MEM_P (dest))
    return;

  /* If we are setting a MEM in our list of specially recognized MEMs,
     don't mark as killed this time.  */

  if (expr_equiv_p (dest, gcse_mem_operand) && pre_ldst_mems != NULL)
    {
      if (!find_rtx_in_ldst (dest))
	gcse_mems_conflict_p = 1;
      return;
    }

  if (true_dependence (dest, GET_MODE (dest), gcse_mem_operand,
		       rtx_addr_varies_p))
    gcse_mems_conflict_p = 1;
}

/* Return nonzero if the expression in X (a memory reference) is killed
   in block BB before or after the insn with the CUID in UID_LIMIT.
   AVAIL_P is nonzero for kills after UID_LIMIT, and zero for kills
   before UID_LIMIT.

   To check the entire block, set UID_LIMIT to max_uid + 1 and
   AVAIL_P to 0.  */

static int
load_killed_in_block_p (basic_block bb, int uid_limit, rtx x, int avail_p)
{
  rtx list_entry = modify_mem_list[bb->index];

  /* If this is a readonly then we aren't going to be changing it.  */
  if (MEM_READONLY_P (x))
    return 0;

  while (list_entry)
    {
      rtx setter;
      /* Ignore entries in the list that do not apply.  */
      if ((avail_p
	   && INSN_CUID (XEXP (list_entry, 0)) < uid_limit)
	  || (! avail_p
	      && INSN_CUID (XEXP (list_entry, 0)) > uid_limit))
	{
	  list_entry = XEXP (list_entry, 1);
	  continue;
	}

      setter = XEXP (list_entry, 0);

      /* If SETTER is a call everything is clobbered.  Note that calls
	 to pure functions are never put on the list, so we need not
	 worry about them.  */
      if (CALL_P (setter))
	return 1;

      /* SETTER must be an INSN of some kind that sets memory.  Call
	 note_stores to examine each hunk of memory that is modified.

	 The note_stores interface is pretty limited, so we have to
	 communicate via global variables.  Yuk.  */
      gcse_mem_operand = x;
      gcse_mems_conflict_p = 0;
      note_stores (PATTERN (setter), mems_conflict_for_gcse_p, NULL);
      if (gcse_mems_conflict_p)
	return 1;
      list_entry = XEXP (list_entry, 1);
    }
  return 0;
}

/* Return nonzero if the operands of expression X are unchanged from
   the start of INSN's basic block up to but not including INSN.  */

static int
oprs_anticipatable_p (rtx x, rtx insn)
{
  return oprs_unchanged_p (x, insn, 0);
}

/* Return nonzero if the operands of expression X are unchanged from
   INSN to the end of INSN's basic block.  */

static int
oprs_available_p (rtx x, rtx insn)
{
  return oprs_unchanged_p (x, insn, 1);
}

/* Hash expression X.

   MODE is only used if X is a CONST_INT.  DO_NOT_RECORD_P is a boolean
   indicating if a volatile operand is found or if the expression contains
   something we don't want to insert in the table.  HASH_TABLE_SIZE is
   the current size of the hash table to be probed.  */

static unsigned int
hash_expr (rtx x, enum machine_mode mode, int *do_not_record_p,
	   int hash_table_size)
{
  unsigned int hash;

  *do_not_record_p = 0;

  hash = hash_rtx (x, mode, do_not_record_p,
		   NULL,  /*have_reg_qty=*/false);
  return hash % hash_table_size;
}

/* Hash a set of register REGNO.

   Sets are hashed on the register that is set.  This simplifies the PRE copy
   propagation code.

   ??? May need to make things more elaborate.  Later, as necessary.  */

static unsigned int
hash_set (int regno, int hash_table_size)
{
  unsigned int hash;

  hash = regno;
  return hash % hash_table_size;
}

/* Return nonzero if exp1 is equivalent to exp2.  */

static int
expr_equiv_p (rtx x, rtx y)
{
  return exp_equiv_p (x, y, 0, true);
}

/* Insert expression X in INSN in the hash TABLE.
   If it is already present, record it as the last occurrence in INSN's
   basic block.

   MODE is the mode of the value X is being stored into.
   It is only used if X is a CONST_INT.

   ANTIC_P is nonzero if X is an anticipatable expression.
   AVAIL_P is nonzero if X is an available expression.  */

static void
insert_expr_in_table (rtx x, enum machine_mode mode, rtx insn, int antic_p,
		      int avail_p, struct hash_table *table)
{
  int found, do_not_record_p;
  unsigned int hash;
  struct expr *cur_expr, *last_expr = NULL;
  struct occr *antic_occr, *avail_occr;

  hash = hash_expr (x, mode, &do_not_record_p, table->size);

  /* Do not insert expression in table if it contains volatile operands,
     or if hash_expr determines the expression is something we don't want
     to or can't handle.  */
  if (do_not_record_p)
    return;

  cur_expr = table->table[hash];
  found = 0;

  while (cur_expr && 0 == (found = expr_equiv_p (cur_expr->expr, x)))
    {
      /* If the expression isn't found, save a pointer to the end of
	 the list.  */
      last_expr = cur_expr;
      cur_expr = cur_expr->next_same_hash;
    }

  if (! found)
    {
      cur_expr = gcse_alloc (sizeof (struct expr));
      bytes_used += sizeof (struct expr);
      if (table->table[hash] == NULL)
	/* This is the first pattern that hashed to this index.  */
	table->table[hash] = cur_expr;
      else
	/* Add EXPR to end of this hash chain.  */
	last_expr->next_same_hash = cur_expr;

      /* Set the fields of the expr element.  */
      cur_expr->expr = x;
      cur_expr->bitmap_index = table->n_elems++;
      cur_expr->next_same_hash = NULL;
      cur_expr->antic_occr = NULL;
      cur_expr->avail_occr = NULL;
    }

  /* Now record the occurrence(s).  */
  if (antic_p)
    {
      antic_occr = cur_expr->antic_occr;

      if (antic_occr && BLOCK_NUM (antic_occr->insn) != BLOCK_NUM (insn))
	antic_occr = NULL;

      if (antic_occr)
	/* Found another instance of the expression in the same basic block.
	   Prefer the currently recorded one.  We want the first one in the
	   block and the block is scanned from start to end.  */
	; /* nothing to do */
      else
	{
	  /* First occurrence of this expression in this basic block.  */
	  antic_occr = gcse_alloc (sizeof (struct occr));
	  bytes_used += sizeof (struct occr);
	  antic_occr->insn = insn;
	  antic_occr->next = cur_expr->antic_occr;
	  antic_occr->deleted_p = 0;
	  cur_expr->antic_occr = antic_occr;
	}
    }

  if (avail_p)
    {
      avail_occr = cur_expr->avail_occr;

      if (avail_occr && BLOCK_NUM (avail_occr->insn) == BLOCK_NUM (insn))
	{
	  /* Found another instance of the expression in the same basic block.
	     Prefer this occurrence to the currently recorded one.  We want
	     the last one in the block and the block is scanned from start
	     to end.  */
	  avail_occr->insn = insn;
	}
      else
	{
	  /* First occurrence of this expression in this basic block.  */
	  avail_occr = gcse_alloc (sizeof (struct occr));
	  bytes_used += sizeof (struct occr);
	  avail_occr->insn = insn;
	  avail_occr->next = cur_expr->avail_occr;
	  avail_occr->deleted_p = 0;
	  cur_expr->avail_occr = avail_occr;
	}
    }
}

/* Insert pattern X in INSN in the hash table.
   X is a SET of a reg to either another reg or a constant.
   If it is already present, record it as the last occurrence in INSN's
   basic block.  */

static void
insert_set_in_table (rtx x, rtx insn, struct hash_table *table)
{
  int found;
  unsigned int hash;
  struct expr *cur_expr, *last_expr = NULL;
  struct occr *cur_occr;

  gcc_assert (GET_CODE (x) == SET && REG_P (SET_DEST (x)));

  hash = hash_set (REGNO (SET_DEST (x)), table->size);

  cur_expr = table->table[hash];
  found = 0;

  while (cur_expr && 0 == (found = expr_equiv_p (cur_expr->expr, x)))
    {
      /* If the expression isn't found, save a pointer to the end of
	 the list.  */
      last_expr = cur_expr;
      cur_expr = cur_expr->next_same_hash;
    }

  if (! found)
    {
      cur_expr = gcse_alloc (sizeof (struct expr));
      bytes_used += sizeof (struct expr);
      if (table->table[hash] == NULL)
	/* This is the first pattern that hashed to this index.  */
	table->table[hash] = cur_expr;
      else
	/* Add EXPR to end of this hash chain.  */
	last_expr->next_same_hash = cur_expr;

      /* Set the fields of the expr element.
	 We must copy X because it can be modified when copy propagation is
	 performed on its operands.  */
      cur_expr->expr = copy_rtx (x);
      cur_expr->bitmap_index = table->n_elems++;
      cur_expr->next_same_hash = NULL;
      cur_expr->antic_occr = NULL;
      cur_expr->avail_occr = NULL;
    }

  /* Now record the occurrence.  */
  cur_occr = cur_expr->avail_occr;

  if (cur_occr && BLOCK_NUM (cur_occr->insn) == BLOCK_NUM (insn))
    {
      /* Found another instance of the expression in the same basic block.
	 Prefer this occurrence to the currently recorded one.  We want
	 the last one in the block and the block is scanned from start
	 to end.  */
      cur_occr->insn = insn;
    }
  else
    {
      /* First occurrence of this expression in this basic block.  */
      cur_occr = gcse_alloc (sizeof (struct occr));
      bytes_used += sizeof (struct occr);

	  cur_occr->insn = insn;
	  cur_occr->next = cur_expr->avail_occr;
	  cur_occr->deleted_p = 0;
	  cur_expr->avail_occr = cur_occr;
    }
}

/* Determine whether the rtx X should be treated as a constant for
   the purposes of GCSE's constant propagation.  */

static bool
gcse_constant_p (rtx x)
{
  /* Consider a COMPARE of two integers constant.  */
  if (GET_CODE (x) == COMPARE
      && GET_CODE (XEXP (x, 0)) == CONST_INT
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    return true;

  /* Consider a COMPARE of the same registers is a constant
     if they are not floating point registers.  */
  if (GET_CODE(x) == COMPARE
      && REG_P (XEXP (x, 0)) && REG_P (XEXP (x, 1))
      && REGNO (XEXP (x, 0)) == REGNO (XEXP (x, 1))
      && ! FLOAT_MODE_P (GET_MODE (XEXP (x, 0)))
      && ! FLOAT_MODE_P (GET_MODE (XEXP (x, 1))))
    return true;

  return CONSTANT_P (x);
}

/* Scan pattern PAT of INSN and add an entry to the hash TABLE (set or
   expression one).  */

static void
hash_scan_set (rtx pat, rtx insn, struct hash_table *table)
{
  rtx src = SET_SRC (pat);
  rtx dest = SET_DEST (pat);
  rtx note;

  if (GET_CODE (src) == CALL)
    hash_scan_call (src, insn, table);

  else if (REG_P (dest))
    {
      unsigned int regno = REGNO (dest);
      rtx tmp;

      /* See if a REG_NOTE shows this equivalent to a simpler expression.
	 This allows us to do a single GCSE pass and still eliminate
	 redundant constants, addresses or other expressions that are
	 constructed with multiple instructions.  */
      note = find_reg_equal_equiv_note (insn);
      if (note != 0
	  && (table->set_p
	      ? gcse_constant_p (XEXP (note, 0))
	      : want_to_gcse_p (XEXP (note, 0))))
	src = XEXP (note, 0), pat = gen_rtx_SET (VOIDmode, dest, src);

      /* Only record sets of pseudo-regs in the hash table.  */
      if (! table->set_p
	  && regno >= FIRST_PSEUDO_REGISTER
	  /* Don't GCSE something if we can't do a reg/reg copy.  */
	  && can_copy_p (GET_MODE (dest))
	  /* GCSE commonly inserts instruction after the insn.  We can't
	     do that easily for EH_REGION notes so disable GCSE on these
	     for now.  */
	  && !find_reg_note (insn, REG_EH_REGION, NULL_RTX)
	  /* Is SET_SRC something we want to gcse?  */
	  && want_to_gcse_p (src)
	  /* Don't CSE a nop.  */
	  && ! set_noop_p (pat)
	  /* Don't GCSE if it has attached REG_EQUIV note.
	     At this point this only function parameters should have
	     REG_EQUIV notes and if the argument slot is used somewhere
	     explicitly, it means address of parameter has been taken,
	     so we should not extend the lifetime of the pseudo.  */
	  && (note == NULL_RTX || ! MEM_P (XEXP (note, 0))))
	{
	  /* An expression is not anticipatable if its operands are
	     modified before this insn or if this is not the only SET in
	     this insn.  */
	  int antic_p = oprs_anticipatable_p (src, insn) && single_set (insn);
	  /* An expression is not available if its operands are
	     subsequently modified, including this insn.  It's also not
	     available if this is a branch, because we can't insert
	     a set after the branch.  */
	  int avail_p = (oprs_available_p (src, insn)
			 && ! JUMP_P (insn));

	  insert_expr_in_table (src, GET_MODE (dest), insn, antic_p, avail_p, table);
	}

      /* Record sets for constant/copy propagation.  */
      else if (table->set_p
	       && regno >= FIRST_PSEUDO_REGISTER
	       && ((REG_P (src)
		    && REGNO (src) >= FIRST_PSEUDO_REGISTER
		    && can_copy_p (GET_MODE (dest))
		    && REGNO (src) != regno)
		   || gcse_constant_p (src))
	       /* A copy is not available if its src or dest is subsequently
		  modified.  Here we want to search from INSN+1 on, but
		  oprs_available_p searches from INSN on.  */
	       && (insn == BB_END (BLOCK_FOR_INSN (insn))
		   || ((tmp = next_nonnote_insn (insn)) != NULL_RTX
		       && oprs_available_p (pat, tmp))))
	insert_set_in_table (pat, insn, table);
    }
  /* In case of store we want to consider the memory value as available in
     the REG stored in that memory. This makes it possible to remove
     redundant loads from due to stores to the same location.  */
  else if (flag_gcse_las && REG_P (src) && MEM_P (dest))
      {
        unsigned int regno = REGNO (src);

        /* Do not do this for constant/copy propagation.  */
        if (! table->set_p
            /* Only record sets of pseudo-regs in the hash table.  */
	    && regno >= FIRST_PSEUDO_REGISTER
	   /* Don't GCSE something if we can't do a reg/reg copy.  */
	   && can_copy_p (GET_MODE (src))
	   /* GCSE commonly inserts instruction after the insn.  We can't
	      do that easily for EH_REGION notes so disable GCSE on these
	      for now.  */
	   && ! find_reg_note (insn, REG_EH_REGION, NULL_RTX)
	   /* Is SET_DEST something we want to gcse?  */
	   && want_to_gcse_p (dest)
	   /* Don't CSE a nop.  */
	   && ! set_noop_p (pat)
	   /* Don't GCSE if it has attached REG_EQUIV note.
	      At this point this only function parameters should have
	      REG_EQUIV notes and if the argument slot is used somewhere
	      explicitly, it means address of parameter has been taken,
	      so we should not extend the lifetime of the pseudo.  */
	   && ((note = find_reg_note (insn, REG_EQUIV, NULL_RTX)) == 0
	       || ! MEM_P (XEXP (note, 0))))
             {
               /* Stores are never anticipatable.  */
               int antic_p = 0;
	       /* An expression is not available if its operands are
	          subsequently modified, including this insn.  It's also not
	          available if this is a branch, because we can't insert
	          a set after the branch.  */
               int avail_p = oprs_available_p (dest, insn)
			     && ! JUMP_P (insn);

	       /* Record the memory expression (DEST) in the hash table.  */
	       insert_expr_in_table (dest, GET_MODE (dest), insn,
				     antic_p, avail_p, table);
             }
      }
}

static void
hash_scan_clobber (rtx x ATTRIBUTE_UNUSED, rtx insn ATTRIBUTE_UNUSED,
		   struct hash_table *table ATTRIBUTE_UNUSED)
{
  /* Currently nothing to do.  */
}

static void
hash_scan_call (rtx x ATTRIBUTE_UNUSED, rtx insn ATTRIBUTE_UNUSED,
		struct hash_table *table ATTRIBUTE_UNUSED)
{
  /* Currently nothing to do.  */
}

/* Process INSN and add hash table entries as appropriate.

   Only available expressions that set a single pseudo-reg are recorded.

   Single sets in a PARALLEL could be handled, but it's an extra complication
   that isn't dealt with right now.  The trick is handling the CLOBBERs that
   are also in the PARALLEL.  Later.

   If SET_P is nonzero, this is for the assignment hash table,
   otherwise it is for the expression hash table.
   If IN_LIBCALL_BLOCK nonzero, we are in a libcall block, and should
   not record any expressions.  */

static void
hash_scan_insn (rtx insn, struct hash_table *table, int in_libcall_block)
{
  rtx pat = PATTERN (insn);
  int i;

  if (in_libcall_block)
    return;

  /* Pick out the sets of INSN and for other forms of instructions record
     what's been modified.  */

  if (GET_CODE (pat) == SET)
    hash_scan_set (pat, insn, table);
  else if (GET_CODE (pat) == PARALLEL)
    for (i = 0; i < XVECLEN (pat, 0); i++)
      {
	rtx x = XVECEXP (pat, 0, i);

	if (GET_CODE (x) == SET)
	  hash_scan_set (x, insn, table);
	else if (GET_CODE (x) == CLOBBER)
	  hash_scan_clobber (x, insn, table);
	else if (GET_CODE (x) == CALL)
	  hash_scan_call (x, insn, table);
      }

  else if (GET_CODE (pat) == CLOBBER)
    hash_scan_clobber (pat, insn, table);
  else if (GET_CODE (pat) == CALL)
    hash_scan_call (pat, insn, table);
}

static void
dump_hash_table (FILE *file, const char *name, struct hash_table *table)
{
  int i;
  /* Flattened out table, so it's printed in proper order.  */
  struct expr **flat_table;
  unsigned int *hash_val;
  struct expr *expr;

  flat_table = xcalloc (table->n_elems, sizeof (struct expr *));
  hash_val = xmalloc (table->n_elems * sizeof (unsigned int));

  for (i = 0; i < (int) table->size; i++)
    for (expr = table->table[i]; expr != NULL; expr = expr->next_same_hash)
      {
	flat_table[expr->bitmap_index] = expr;
	hash_val[expr->bitmap_index] = i;
      }

  fprintf (file, "%s hash table (%d buckets, %d entries)\n",
	   name, table->size, table->n_elems);

  for (i = 0; i < (int) table->n_elems; i++)
    if (flat_table[i] != 0)
      {
	expr = flat_table[i];
	fprintf (file, "Index %d (hash value %d)\n  ",
		 expr->bitmap_index, hash_val[i]);
	print_rtl (file, expr->expr);
	fprintf (file, "\n");
      }

  fprintf (file, "\n");

  free (flat_table);
  free (hash_val);
}

/* Record register first/last/block set information for REGNO in INSN.

   first_set records the first place in the block where the register
   is set and is used to compute "anticipatability".

   last_set records the last place in the block where the register
   is set and is used to compute "availability".

   last_bb records the block for which first_set and last_set are
   valid, as a quick test to invalidate them.

   reg_set_in_block records whether the register is set in the block
   and is used to compute "transparency".  */

static void
record_last_reg_set_info (rtx insn, int regno)
{
  struct reg_avail_info *info = &reg_avail_info[regno];
  int cuid = INSN_CUID (insn);

  info->last_set = cuid;
  if (info->last_bb != current_bb)
    {
      info->last_bb = current_bb;
      info->first_set = cuid;
      SET_BIT (reg_set_in_block[current_bb->index], regno);
    }
}


/* Record all of the canonicalized MEMs of record_last_mem_set_info's insn.
   Note we store a pair of elements in the list, so they have to be
   taken off pairwise.  */

static void
canon_list_insert (rtx dest ATTRIBUTE_UNUSED, rtx unused1 ATTRIBUTE_UNUSED,
		   void * v_insn)
{
  rtx dest_addr, insn;
  int bb;

  while (GET_CODE (dest) == SUBREG
      || GET_CODE (dest) == ZERO_EXTRACT
      || GET_CODE (dest) == STRICT_LOW_PART)
    dest = XEXP (dest, 0);

  /* If DEST is not a MEM, then it will not conflict with a load.  Note
     that function calls are assumed to clobber memory, but are handled
     elsewhere.  */

  if (! MEM_P (dest))
    return;

  dest_addr = get_addr (XEXP (dest, 0));
  dest_addr = canon_rtx (dest_addr);
  insn = (rtx) v_insn;
  bb = BLOCK_NUM (insn);

  canon_modify_mem_list[bb] =
    alloc_EXPR_LIST (VOIDmode, dest_addr, canon_modify_mem_list[bb]);
  canon_modify_mem_list[bb] =
    alloc_EXPR_LIST (VOIDmode, dest, canon_modify_mem_list[bb]);
}

/* Record memory modification information for INSN.  We do not actually care
   about the memory location(s) that are set, or even how they are set (consider
   a CALL_INSN).  We merely need to record which insns modify memory.  */

static void
record_last_mem_set_info (rtx insn)
{
  int bb = BLOCK_NUM (insn);

  /* load_killed_in_block_p will handle the case of calls clobbering
     everything.  */
  modify_mem_list[bb] = alloc_INSN_LIST (insn, modify_mem_list[bb]);
  bitmap_set_bit (modify_mem_list_set, bb);

  if (CALL_P (insn))
    {
      /* Note that traversals of this loop (other than for free-ing)
	 will break after encountering a CALL_INSN.  So, there's no
	 need to insert a pair of items, as canon_list_insert does.  */
      canon_modify_mem_list[bb] =
	alloc_INSN_LIST (insn, canon_modify_mem_list[bb]);
      bitmap_set_bit (blocks_with_calls, bb);
    }
  else
    note_stores (PATTERN (insn), canon_list_insert, (void*) insn);
}

/* Called from compute_hash_table via note_stores to handle one
   SET or CLOBBER in an insn.  DATA is really the instruction in which
   the SET is taking place.  */

static void
record_last_set_info (rtx dest, rtx setter ATTRIBUTE_UNUSED, void *data)
{
  rtx last_set_insn = (rtx) data;

  if (GET_CODE (dest) == SUBREG)
    dest = SUBREG_REG (dest);

  if (REG_P (dest))
    record_last_reg_set_info (last_set_insn, REGNO (dest));
  else if (MEM_P (dest)
	   /* Ignore pushes, they clobber nothing.  */
	   && ! push_operand (dest, GET_MODE (dest)))
    record_last_mem_set_info (last_set_insn);
}

/* Top level function to create an expression or assignment hash table.

   Expression entries are placed in the hash table if
   - they are of the form (set (pseudo-reg) src),
   - src is something we want to perform GCSE on,
   - none of the operands are subsequently modified in the block

   Assignment entries are placed in the hash table if
   - they are of the form (set (pseudo-reg) src),
   - src is something we want to perform const/copy propagation on,
   - none of the operands or target are subsequently modified in the block

   Currently src must be a pseudo-reg or a const_int.

   TABLE is the table computed.  */

static void
compute_hash_table_work (struct hash_table *table)
{
  unsigned int i;

  /* While we compute the hash table we also compute a bit array of which
     registers are set in which blocks.
     ??? This isn't needed during const/copy propagation, but it's cheap to
     compute.  Later.  */
  sbitmap_vector_zero (reg_set_in_block, last_basic_block);

  /* re-Cache any INSN_LIST nodes we have allocated.  */
  clear_modify_mem_tables ();
  /* Some working arrays used to track first and last set in each block.  */
  reg_avail_info = gmalloc (max_gcse_regno * sizeof (struct reg_avail_info));

  for (i = 0; i < max_gcse_regno; ++i)
    reg_avail_info[i].last_bb = NULL;

  FOR_EACH_BB (current_bb)
    {
      rtx insn;
      unsigned int regno;
      int in_libcall_block;

      /* First pass over the instructions records information used to
	 determine when registers and memory are first and last set.
	 ??? hard-reg reg_set_in_block computation
	 could be moved to compute_sets since they currently don't change.  */

      FOR_BB_INSNS (current_bb, insn)
	{
	  if (! INSN_P (insn))
	    continue;

	  if (CALL_P (insn))
	    {
	      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
		if (TEST_HARD_REG_BIT (regs_invalidated_by_call, regno))
		  record_last_reg_set_info (insn, regno);

	      mark_call (insn);
	    }

	  note_stores (PATTERN (insn), record_last_set_info, insn);
	}

      /* Insert implicit sets in the hash table.  */
      if (table->set_p
	  && implicit_sets[current_bb->index] != NULL_RTX)
	hash_scan_set (implicit_sets[current_bb->index],
		       BB_HEAD (current_bb), table);

      /* The next pass builds the hash table.  */
      in_libcall_block = 0;
      FOR_BB_INSNS (current_bb, insn)
	if (INSN_P (insn))
	  {
	    if (find_reg_note (insn, REG_LIBCALL, NULL_RTX))
	      in_libcall_block = 1;
	    else if (table->set_p && find_reg_note (insn, REG_RETVAL, NULL_RTX))
	      in_libcall_block = 0;
	    hash_scan_insn (insn, table, in_libcall_block);
	    if (!table->set_p && find_reg_note (insn, REG_RETVAL, NULL_RTX))
	      in_libcall_block = 0;
	  }
    }

  free (reg_avail_info);
  reg_avail_info = NULL;
}

/* Allocate space for the set/expr hash TABLE.
   N_INSNS is the number of instructions in the function.
   It is used to determine the number of buckets to use.
   SET_P determines whether set or expression table will
   be created.  */

static void
alloc_hash_table (int n_insns, struct hash_table *table, int set_p)
{
  int n;

  table->size = n_insns / 4;
  if (table->size < 11)
    table->size = 11;

  /* Attempt to maintain efficient use of hash table.
     Making it an odd number is simplest for now.
     ??? Later take some measurements.  */
  table->size |= 1;
  n = table->size * sizeof (struct expr *);
  table->table = gmalloc (n);
  table->set_p = set_p;
}

/* Free things allocated by alloc_hash_table.  */

static void
free_hash_table (struct hash_table *table)
{
  free (table->table);
}

/* Compute the hash TABLE for doing copy/const propagation or
   expression hash table.  */

static void
compute_hash_table (struct hash_table *table)
{
  /* Initialize count of number of entries in hash table.  */
  table->n_elems = 0;
  memset (table->table, 0, table->size * sizeof (struct expr *));

  compute_hash_table_work (table);
}

/* Expression tracking support.  */

/* Lookup REGNO in the set TABLE.  The result is a pointer to the
   table entry, or NULL if not found.  */

static struct expr *
lookup_set (unsigned int regno, struct hash_table *table)
{
  unsigned int hash = hash_set (regno, table->size);
  struct expr *expr;

  expr = table->table[hash];

  while (expr && REGNO (SET_DEST (expr->expr)) != regno)
    expr = expr->next_same_hash;

  return expr;
}

/* Return the next entry for REGNO in list EXPR.  */

static struct expr *
next_set (unsigned int regno, struct expr *expr)
{
  do
    expr = expr->next_same_hash;
  while (expr && REGNO (SET_DEST (expr->expr)) != regno);

  return expr;
}

/* Like free_INSN_LIST_list or free_EXPR_LIST_list, except that the node
   types may be mixed.  */

static void
free_insn_expr_list_list (rtx *listp)
{
  rtx list, next;

  for (list = *listp; list ; list = next)
    {
      next = XEXP (list, 1);
      if (GET_CODE (list) == EXPR_LIST)
	free_EXPR_LIST_node (list);
      else
	free_INSN_LIST_node (list);
    }

  *listp = NULL;
}

/* Clear canon_modify_mem_list and modify_mem_list tables.  */
static void
clear_modify_mem_tables (void)
{
  unsigned i;
  bitmap_iterator bi;

  EXECUTE_IF_SET_IN_BITMAP (modify_mem_list_set, 0, i, bi)
    {
      free_INSN_LIST_list (modify_mem_list + i);
      free_insn_expr_list_list (canon_modify_mem_list + i);
    }
  bitmap_clear (modify_mem_list_set);
  bitmap_clear (blocks_with_calls);
}

/* Release memory used by modify_mem_list_set.  */

static void
free_modify_mem_tables (void)
{
  clear_modify_mem_tables ();
  free (modify_mem_list);
  free (canon_modify_mem_list);
  modify_mem_list = 0;
  canon_modify_mem_list = 0;
}

/* Reset tables used to keep track of what's still available [since the
   start of the block].  */

static void
reset_opr_set_tables (void)
{
  /* Maintain a bitmap of which regs have been set since beginning of
     the block.  */
  CLEAR_REG_SET (reg_set_bitmap);

  /* Also keep a record of the last instruction to modify memory.
     For now this is very trivial, we only record whether any memory
     location has been modified.  */
  clear_modify_mem_tables ();
}

/* Return nonzero if the operands of X are not set before INSN in
   INSN's basic block.  */

static int
oprs_not_set_p (rtx x, rtx insn)
{
  int i, j;
  enum rtx_code code;
  const char *fmt;

  if (x == 0)
    return 1;

  code = GET_CODE (x);
  switch (code)
    {
    case PC:
    case CC0:
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return 1;

    case MEM:
      if (load_killed_in_block_p (BLOCK_FOR_INSN (insn),
				  INSN_CUID (insn), x, 0))
	return 0;
      else
	return oprs_not_set_p (XEXP (x, 0), insn);

    case REG:
      return ! REGNO_REG_SET_P (reg_set_bitmap, REGNO (x));

    default:
      break;
    }

  for (i = GET_RTX_LENGTH (code) - 1, fmt = GET_RTX_FORMAT (code); i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  /* If we are about to do the last recursive call
	     needed at this level, change it into iteration.
	     This function is called enough to be worth it.  */
	  if (i == 0)
	    return oprs_not_set_p (XEXP (x, i), insn);

	  if (! oprs_not_set_p (XEXP (x, i), insn))
	    return 0;
	}
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  if (! oprs_not_set_p (XVECEXP (x, i, j), insn))
	    return 0;
    }

  return 1;
}

/* Mark things set by a CALL.  */

static void
mark_call (rtx insn)
{
  if (! CONST_OR_PURE_CALL_P (insn))
    record_last_mem_set_info (insn);
}

/* Mark things set by a SET.  */

static void
mark_set (rtx pat, rtx insn)
{
  rtx dest = SET_DEST (pat);

  while (GET_CODE (dest) == SUBREG
	 || GET_CODE (dest) == ZERO_EXTRACT
	 || GET_CODE (dest) == STRICT_LOW_PART)
    dest = XEXP (dest, 0);

  if (REG_P (dest))
    SET_REGNO_REG_SET (reg_set_bitmap, REGNO (dest));
  else if (MEM_P (dest))
    record_last_mem_set_info (insn);

  if (GET_CODE (SET_SRC (pat)) == CALL)
    mark_call (insn);
}

/* Record things set by a CLOBBER.  */

static void
mark_clobber (rtx pat, rtx insn)
{
  rtx clob = XEXP (pat, 0);

  while (GET_CODE (clob) == SUBREG || GET_CODE (clob) == STRICT_LOW_PART)
    clob = XEXP (clob, 0);

  if (REG_P (clob))
    SET_REGNO_REG_SET (reg_set_bitmap, REGNO (clob));
  else
    record_last_mem_set_info (insn);
}

/* Record things set by INSN.
   This data is used by oprs_not_set_p.  */

static void
mark_oprs_set (rtx insn)
{
  rtx pat = PATTERN (insn);
  int i;

  if (GET_CODE (pat) == SET)
    mark_set (pat, insn);
  else if (GET_CODE (pat) == PARALLEL)
    for (i = 0; i < XVECLEN (pat, 0); i++)
      {
	rtx x = XVECEXP (pat, 0, i);

	if (GET_CODE (x) == SET)
	  mark_set (x, insn);
	else if (GET_CODE (x) == CLOBBER)
	  mark_clobber (x, insn);
	else if (GET_CODE (x) == CALL)
	  mark_call (insn);
      }

  else if (GET_CODE (pat) == CLOBBER)
    mark_clobber (pat, insn);
  else if (GET_CODE (pat) == CALL)
    mark_call (insn);
}


/* Compute copy/constant propagation working variables.  */

/* Local properties of assignments.  */
static sbitmap *cprop_pavloc;
static sbitmap *cprop_absaltered;

/* Global properties of assignments (computed from the local properties).  */
static sbitmap *cprop_avin;
static sbitmap *cprop_avout;

/* Allocate vars used for copy/const propagation.  N_BLOCKS is the number of
   basic blocks.  N_SETS is the number of sets.  */

static void
alloc_cprop_mem (int n_blocks, int n_sets)
{
  cprop_pavloc = sbitmap_vector_alloc (n_blocks, n_sets);
  cprop_absaltered = sbitmap_vector_alloc (n_blocks, n_sets);

  cprop_avin = sbitmap_vector_alloc (n_blocks, n_sets);
  cprop_avout = sbitmap_vector_alloc (n_blocks, n_sets);
}

/* Free vars used by copy/const propagation.  */

static void
free_cprop_mem (void)
{
  sbitmap_vector_free (cprop_pavloc);
  sbitmap_vector_free (cprop_absaltered);
  sbitmap_vector_free (cprop_avin);
  sbitmap_vector_free (cprop_avout);
}

/* For each block, compute whether X is transparent.  X is either an
   expression or an assignment [though we don't care which, for this context
   an assignment is treated as an expression].  For each block where an
   element of X is modified, set (SET_P == 1) or reset (SET_P == 0) the INDX
   bit in BMAP.  */

static void
compute_transp (rtx x, int indx, sbitmap *bmap, int set_p)
{
  int i, j;
  basic_block bb;
  enum rtx_code code;
  reg_set *r;
  const char *fmt;

  /* repeat is used to turn tail-recursion into iteration since GCC
     can't do it when there's no return value.  */
 repeat:

  if (x == 0)
    return;

  code = GET_CODE (x);
  switch (code)
    {
    case REG:
      if (set_p)
	{
	  if (REGNO (x) < FIRST_PSEUDO_REGISTER)
	    {
	      FOR_EACH_BB (bb)
		if (TEST_BIT (reg_set_in_block[bb->index], REGNO (x)))
		  SET_BIT (bmap[bb->index], indx);
	    }
	  else
	    {
	      for (r = reg_set_table[REGNO (x)]; r != NULL; r = r->next)
		SET_BIT (bmap[r->bb_index], indx);
	    }
	}
      else
	{
	  if (REGNO (x) < FIRST_PSEUDO_REGISTER)
	    {
	      FOR_EACH_BB (bb)
		if (TEST_BIT (reg_set_in_block[bb->index], REGNO (x)))
		  RESET_BIT (bmap[bb->index], indx);
	    }
	  else
	    {
	      for (r = reg_set_table[REGNO (x)]; r != NULL; r = r->next)
		RESET_BIT (bmap[r->bb_index], indx);
	    }
	}

      return;

    case MEM:
      if (! MEM_READONLY_P (x))
	{
	  bitmap_iterator bi;
	  unsigned bb_index;

	  /* First handle all the blocks with calls.  We don't need to
	     do any list walking for them.  */
	  EXECUTE_IF_SET_IN_BITMAP (blocks_with_calls, 0, bb_index, bi)
	    {
	      if (set_p)
		SET_BIT (bmap[bb_index], indx);
	      else
		RESET_BIT (bmap[bb_index], indx);
	    }

	    /* Now iterate over the blocks which have memory modifications
	       but which do not have any calls.  */
	    EXECUTE_IF_AND_COMPL_IN_BITMAP (modify_mem_list_set, 
					    blocks_with_calls,
					    0, bb_index, bi)
	      {
		rtx list_entry = canon_modify_mem_list[bb_index];

		while (list_entry)
		  {
		    rtx dest, dest_addr;

		    /* LIST_ENTRY must be an INSN of some kind that sets memory.
		       Examine each hunk of memory that is modified.  */

		    dest = XEXP (list_entry, 0);
		    list_entry = XEXP (list_entry, 1);
		    dest_addr = XEXP (list_entry, 0);

		    if (canon_true_dependence (dest, GET_MODE (dest), dest_addr,
					       x, rtx_addr_varies_p))
		      {
			if (set_p)
			  SET_BIT (bmap[bb_index], indx);
			else
			  RESET_BIT (bmap[bb_index], indx);
			break;
		      }
		    list_entry = XEXP (list_entry, 1);
	          }
	      }
	}

      x = XEXP (x, 0);
      goto repeat;

    case PC:
    case CC0: /*FIXME*/
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return;

    default:
      break;
    }

  for (i = GET_RTX_LENGTH (code) - 1, fmt = GET_RTX_FORMAT (code); i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  /* If we are about to do the last recursive call
	     needed at this level, change it into iteration.
	     This function is called enough to be worth it.  */
	  if (i == 0)
	    {
	      x = XEXP (x, i);
	      goto repeat;
	    }

	  compute_transp (XEXP (x, i), indx, bmap, set_p);
	}
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  compute_transp (XVECEXP (x, i, j), indx, bmap, set_p);
    }
}

/* Top level routine to do the dataflow analysis needed by copy/const
   propagation.  */

static void
compute_cprop_data (void)
{
  compute_local_properties (cprop_absaltered, cprop_pavloc, NULL, &set_hash_table);
  compute_available (cprop_pavloc, cprop_absaltered,
		     cprop_avout, cprop_avin);
}

/* Copy/constant propagation.  */

/* Maximum number of register uses in an insn that we handle.  */
#define MAX_USES 8

/* Table of uses found in an insn.
   Allocated statically to avoid alloc/free complexity and overhead.  */
static struct reg_use reg_use_table[MAX_USES];

/* Index into `reg_use_table' while building it.  */
static int reg_use_count;

/* Set up a list of register numbers used in INSN.  The found uses are stored
   in `reg_use_table'.  `reg_use_count' is initialized to zero before entry,
   and contains the number of uses in the table upon exit.

   ??? If a register appears multiple times we will record it multiple times.
   This doesn't hurt anything but it will slow things down.  */

static void
find_used_regs (rtx *xptr, void *data ATTRIBUTE_UNUSED)
{
  int i, j;
  enum rtx_code code;
  const char *fmt;
  rtx x = *xptr;

  /* repeat is used to turn tail-recursion into iteration since GCC
     can't do it when there's no return value.  */
 repeat:
  if (x == 0)
    return;

  code = GET_CODE (x);
  if (REG_P (x))
    {
      if (reg_use_count == MAX_USES)
	return;

      reg_use_table[reg_use_count].reg_rtx = x;
      reg_use_count++;
    }

  /* Recursively scan the operands of this expression.  */

  for (i = GET_RTX_LENGTH (code) - 1, fmt = GET_RTX_FORMAT (code); i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  /* If we are about to do the last recursive call
	     needed at this level, change it into iteration.
	     This function is called enough to be worth it.  */
	  if (i == 0)
	    {
	      x = XEXP (x, 0);
	      goto repeat;
	    }

	  find_used_regs (&XEXP (x, i), data);
	}
      else if (fmt[i] == 'E')
	for (j = 0; j < XVECLEN (x, i); j++)
	  find_used_regs (&XVECEXP (x, i, j), data);
    }
}

/* Try to replace all non-SET_DEST occurrences of FROM in INSN with TO.
   Returns nonzero is successful.  */

static int
try_replace_reg (rtx from, rtx to, rtx insn)
{
  rtx note = find_reg_equal_equiv_note (insn);
  rtx src = 0;
  int success = 0;
  rtx set = single_set (insn);

  validate_replace_src_group (from, to, insn);
  if (num_changes_pending () && apply_change_group ())
    success = 1;

  /* Try to simplify SET_SRC if we have substituted a constant.  */
  if (success && set && CONSTANT_P (to))
    {
      src = simplify_rtx (SET_SRC (set));

      if (src)
	validate_change (insn, &SET_SRC (set), src, 0);
    }

  /* If there is already a REG_EQUAL note, update the expression in it
     with our replacement.  */
  if (note != 0 && REG_NOTE_KIND (note) == REG_EQUAL)
    XEXP (note, 0) = simplify_replace_rtx (XEXP (note, 0), from, to);

  if (!success && set && reg_mentioned_p (from, SET_SRC (set)))
    {
      /* If above failed and this is a single set, try to simplify the source of
	 the set given our substitution.  We could perhaps try this for multiple
	 SETs, but it probably won't buy us anything.  */
      src = simplify_replace_rtx (SET_SRC (set), from, to);

      if (!rtx_equal_p (src, SET_SRC (set))
	  && validate_change (insn, &SET_SRC (set), src, 0))
	success = 1;

      /* If we've failed to do replacement, have a single SET, don't already
	 have a note, and have no special SET, add a REG_EQUAL note to not
	 lose information.  */
      if (!success && note == 0 && set != 0
	  && GET_CODE (SET_DEST (set)) != ZERO_EXTRACT
	  && GET_CODE (SET_DEST (set)) != STRICT_LOW_PART)
	note = set_unique_reg_note (insn, REG_EQUAL, copy_rtx (src));
    }

  /* REG_EQUAL may get simplified into register.
     We don't allow that. Remove that note. This code ought
     not to happen, because previous code ought to synthesize
     reg-reg move, but be on the safe side.  */
  if (note && REG_NOTE_KIND (note) == REG_EQUAL && REG_P (XEXP (note, 0)))
    remove_note (insn, note);

  return success;
}

/* Find a set of REGNOs that are available on entry to INSN's block.  Returns
   NULL no such set is found.  */

static struct expr *
find_avail_set (int regno, rtx insn)
{
  /* SET1 contains the last set found that can be returned to the caller for
     use in a substitution.  */
  struct expr *set1 = 0;

  /* Loops are not possible here.  To get a loop we would need two sets
     available at the start of the block containing INSN.  i.e. we would
     need two sets like this available at the start of the block:

       (set (reg X) (reg Y))
       (set (reg Y) (reg X))

     This can not happen since the set of (reg Y) would have killed the
     set of (reg X) making it unavailable at the start of this block.  */
  while (1)
    {
      rtx src;
      struct expr *set = lookup_set (regno, &set_hash_table);

      /* Find a set that is available at the start of the block
	 which contains INSN.  */
      while (set)
	{
	  if (TEST_BIT (cprop_avin[BLOCK_NUM (insn)], set->bitmap_index))
	    break;
	  set = next_set (regno, set);
	}

      /* If no available set was found we've reached the end of the
	 (possibly empty) copy chain.  */
      if (set == 0)
	break;

      gcc_assert (GET_CODE (set->expr) == SET);

      src = SET_SRC (set->expr);

      /* We know the set is available.
	 Now check that SRC is ANTLOC (i.e. none of the source operands
	 have changed since the start of the block).

         If the source operand changed, we may still use it for the next
         iteration of this loop, but we may not use it for substitutions.  */

      if (gcse_constant_p (src) || oprs_not_set_p (src, insn))
	set1 = set;

      /* If the source of the set is anything except a register, then
	 we have reached the end of the copy chain.  */
      if (! REG_P (src))
	break;

      /* Follow the copy chain, i.e. start another iteration of the loop
	 and see if we have an available copy into SRC.  */
      regno = REGNO (src);
    }

  /* SET1 holds the last set that was available and anticipatable at
     INSN.  */
  return set1;
}

/* Subroutine of cprop_insn that tries to propagate constants into
   JUMP_INSNS.  JUMP must be a conditional jump.  If SETCC is non-NULL
   it is the instruction that immediately precedes JUMP, and must be a
   single SET of a register.  FROM is what we will try to replace,
   SRC is the constant we will try to substitute for it.  Returns nonzero
   if a change was made.  */

static int
cprop_jump (basic_block bb, rtx setcc, rtx jump, rtx from, rtx src)
{
  rtx new, set_src, note_src;
  rtx set = pc_set (jump);
  rtx note = find_reg_equal_equiv_note (jump);

  if (note)
    {
      note_src = XEXP (note, 0);
      if (GET_CODE (note_src) == EXPR_LIST)
	note_src = NULL_RTX;
    }
  else note_src = NULL_RTX;

  /* Prefer REG_EQUAL notes except those containing EXPR_LISTs.  */
  set_src = note_src ? note_src : SET_SRC (set);

  /* First substitute the SETCC condition into the JUMP instruction,
     then substitute that given values into this expanded JUMP.  */
  if (setcc != NULL_RTX
      && !modified_between_p (from, setcc, jump)
      && !modified_between_p (src, setcc, jump))
    {
      rtx setcc_src;
      rtx setcc_set = single_set (setcc);
      rtx setcc_note = find_reg_equal_equiv_note (setcc);
      setcc_src = (setcc_note && GET_CODE (XEXP (setcc_note, 0)) != EXPR_LIST)
		? XEXP (setcc_note, 0) : SET_SRC (setcc_set);
      set_src = simplify_replace_rtx (set_src, SET_DEST (setcc_set),
				      setcc_src);
    }
  else
    setcc = NULL_RTX;

  new = simplify_replace_rtx (set_src, from, src);

  /* If no simplification can be made, then try the next register.  */
  if (rtx_equal_p (new, SET_SRC (set)))
    return 0;

  /* If this is now a no-op delete it, otherwise this must be a valid insn.  */
  if (new == pc_rtx)
    delete_insn (jump);
  else
    {
      /* Ensure the value computed inside the jump insn to be equivalent
         to one computed by setcc.  */
      if (setcc && modified_in_p (new, setcc))
	return 0;
      if (! validate_change (jump, &SET_SRC (set), new, 0))
	{
	  /* When (some) constants are not valid in a comparison, and there
	     are two registers to be replaced by constants before the entire
	     comparison can be folded into a constant, we need to keep
	     intermediate information in REG_EQUAL notes.  For targets with
	     separate compare insns, such notes are added by try_replace_reg.
	     When we have a combined compare-and-branch instruction, however,
	     we need to attach a note to the branch itself to make this
	     optimization work.  */

	  if (!rtx_equal_p (new, note_src))
	    set_unique_reg_note (jump, REG_EQUAL, copy_rtx (new));
	  return 0;
	}

      /* Remove REG_EQUAL note after simplification.  */
      if (note_src)
	remove_note (jump, note);

      /* If this has turned into an unconditional jump,
	 then put a barrier after it so that the unreachable
	 code will be deleted.  */
      if (GET_CODE (SET_SRC (set)) == LABEL_REF)
	emit_barrier_after (jump);
     }

#ifdef HAVE_cc0
  /* Delete the cc0 setter.  */
  if (setcc != NULL && CC0_P (SET_DEST (single_set (setcc))))
    delete_insn (setcc);
#endif

  run_jump_opt_after_gcse = 1;

  global_const_prop_count++;
  if (dump_file != NULL)
    {
      fprintf (dump_file,
	       "GLOBAL CONST-PROP: Replacing reg %d in jump_insn %d with constant ",
	       REGNO (from), INSN_UID (jump));
      print_rtl (dump_file, src);
      fprintf (dump_file, "\n");
    }
  purge_dead_edges (bb);

  return 1;
}

static bool
constprop_register (rtx insn, rtx from, rtx to, bool alter_jumps)
{
  rtx sset;

  /* Check for reg or cc0 setting instructions followed by
     conditional branch instructions first.  */
  if (alter_jumps
      && (sset = single_set (insn)) != NULL
      && NEXT_INSN (insn)
      && any_condjump_p (NEXT_INSN (insn)) && onlyjump_p (NEXT_INSN (insn)))
    {
      rtx dest = SET_DEST (sset);
      if ((REG_P (dest) || CC0_P (dest))
	  && cprop_jump (BLOCK_FOR_INSN (insn), insn, NEXT_INSN (insn), from, to))
	return 1;
    }

  /* Handle normal insns next.  */
  if (NONJUMP_INSN_P (insn)
      && try_replace_reg (from, to, insn))
    return 1;

  /* Try to propagate a CONST_INT into a conditional jump.
     We're pretty specific about what we will handle in this
     code, we can extend this as necessary over time.

     Right now the insn in question must look like
     (set (pc) (if_then_else ...))  */
  else if (alter_jumps && any_condjump_p (insn) && onlyjump_p (insn))
    return cprop_jump (BLOCK_FOR_INSN (insn), NULL, insn, from, to);
  return 0;
}

/* Perform constant and copy propagation on INSN.
   The result is nonzero if a change was made.  */

static int
cprop_insn (rtx insn, int alter_jumps)
{
  struct reg_use *reg_used;
  int changed = 0;
  rtx note;

  if (!INSN_P (insn))
    return 0;

  reg_use_count = 0;
  note_uses (&PATTERN (insn), find_used_regs, NULL);

  note = find_reg_equal_equiv_note (insn);

  /* We may win even when propagating constants into notes.  */
  if (note)
    find_used_regs (&XEXP (note, 0), NULL);

  for (reg_used = &reg_use_table[0]; reg_use_count > 0;
       reg_used++, reg_use_count--)
    {
      unsigned int regno = REGNO (reg_used->reg_rtx);
      rtx pat, src;
      struct expr *set;

      /* Ignore registers created by GCSE.
	 We do this because ...  */
      if (regno >= max_gcse_regno)
	continue;

      /* If the register has already been set in this block, there's
	 nothing we can do.  */
      if (! oprs_not_set_p (reg_used->reg_rtx, insn))
	continue;

      /* Find an assignment that sets reg_used and is available
	 at the start of the block.  */
      set = find_avail_set (regno, insn);
      if (! set)
	continue;

      pat = set->expr;
      /* ??? We might be able to handle PARALLELs.  Later.  */
      gcc_assert (GET_CODE (pat) == SET);

      src = SET_SRC (pat);

      /* Constant propagation.  */
      if (gcse_constant_p (src))
	{
          if (constprop_register (insn, reg_used->reg_rtx, src, alter_jumps))
	    {
	      changed = 1;
	      global_const_prop_count++;
	      if (dump_file != NULL)
		{
		  fprintf (dump_file, "GLOBAL CONST-PROP: Replacing reg %d in ", regno);
		  fprintf (dump_file, "insn %d with constant ", INSN_UID (insn));
		  print_rtl (dump_file, src);
		  fprintf (dump_file, "\n");
		}
	      if (INSN_DELETED_P (insn))
		return 1;
	    }
	}
      else if (REG_P (src)
	       && REGNO (src) >= FIRST_PSEUDO_REGISTER
	       && REGNO (src) != regno)
	{
	  if (try_replace_reg (reg_used->reg_rtx, src, insn))
	    {
	      changed = 1;
	      global_copy_prop_count++;
	      if (dump_file != NULL)
		{
		  fprintf (dump_file, "GLOBAL COPY-PROP: Replacing reg %d in insn %d",
			   regno, INSN_UID (insn));
		  fprintf (dump_file, " with reg %d\n", REGNO (src));
		}

	      /* The original insn setting reg_used may or may not now be
		 deletable.  We leave the deletion to flow.  */
	      /* FIXME: If it turns out that the insn isn't deletable,
		 then we may have unnecessarily extended register lifetimes
		 and made things worse.  */
	    }
	}
    }

  return changed;
}

/* Like find_used_regs, but avoid recording uses that appear in
   input-output contexts such as zero_extract or pre_dec.  This
   restricts the cases we consider to those for which local cprop
   can legitimately make replacements.  */

static void
local_cprop_find_used_regs (rtx *xptr, void *data)
{
  rtx x = *xptr;

  if (x == 0)
    return;

  switch (GET_CODE (x))
    {
    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
    case STRICT_LOW_PART:
      return;

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
    case PRE_MODIFY:
    case POST_MODIFY:
      /* Can only legitimately appear this early in the context of
	 stack pushes for function arguments, but handle all of the
	 codes nonetheless.  */
      return;

    case SUBREG:
      /* Setting a subreg of a register larger than word_mode leaves
	 the non-written words unchanged.  */
      if (GET_MODE_BITSIZE (GET_MODE (SUBREG_REG (x))) > BITS_PER_WORD)
	return;
      break;

    default:
      break;
    }

  find_used_regs (xptr, data);
}

/* LIBCALL_SP is a zero-terminated array of insns at the end of a libcall;
   their REG_EQUAL notes need updating.  */

static bool
do_local_cprop (rtx x, rtx insn, bool alter_jumps, rtx *libcall_sp)
{
  rtx newreg = NULL, newcnst = NULL;

  /* Rule out USE instructions and ASM statements as we don't want to
     change the hard registers mentioned.  */
  if (REG_P (x)
      && (REGNO (x) >= FIRST_PSEUDO_REGISTER
          || (GET_CODE (PATTERN (insn)) != USE
	      && asm_noperands (PATTERN (insn)) < 0)))
    {
      cselib_val *val = cselib_lookup (x, GET_MODE (x), 0);
      struct elt_loc_list *l;

      if (!val)
	return false;
      for (l = val->locs; l; l = l->next)
	{
	  rtx this_rtx = l->loc;
	  rtx note;

	  /* Don't CSE non-constant values out of libcall blocks.  */
	  if (l->in_libcall && ! CONSTANT_P (this_rtx))
	    continue;

	  if (gcse_constant_p (this_rtx))
	    newcnst = this_rtx;
	  if (REG_P (this_rtx) && REGNO (this_rtx) >= FIRST_PSEUDO_REGISTER
	      /* Don't copy propagate if it has attached REG_EQUIV note.
		 At this point this only function parameters should have
		 REG_EQUIV notes and if the argument slot is used somewhere
		 explicitly, it means address of parameter has been taken,
		 so we should not extend the lifetime of the pseudo.  */
	      && (!(note = find_reg_note (l->setting_insn, REG_EQUIV, NULL_RTX))
		  || ! MEM_P (XEXP (note, 0))))
	    newreg = this_rtx;
	}
      if (newcnst && constprop_register (insn, x, newcnst, alter_jumps))
	{
	  /* If we find a case where we can't fix the retval REG_EQUAL notes
	     match the new register, we either have to abandon this replacement
	     or fix delete_trivially_dead_insns to preserve the setting insn,
	     or make it delete the REG_EUAQL note, and fix up all passes that
	     require the REG_EQUAL note there.  */
	  bool adjusted;

	  adjusted = adjust_libcall_notes (x, newcnst, insn, libcall_sp);
	  gcc_assert (adjusted);
	  
	  if (dump_file != NULL)
	    {
	      fprintf (dump_file, "LOCAL CONST-PROP: Replacing reg %d in ",
		       REGNO (x));
	      fprintf (dump_file, "insn %d with constant ",
		       INSN_UID (insn));
	      print_rtl (dump_file, newcnst);
	      fprintf (dump_file, "\n");
	    }
	  local_const_prop_count++;
	  return true;
	}
      else if (newreg && newreg != x && try_replace_reg (x, newreg, insn))
	{
	  adjust_libcall_notes (x, newreg, insn, libcall_sp);
	  if (dump_file != NULL)
	    {
	      fprintf (dump_file,
		       "LOCAL COPY-PROP: Replacing reg %d in insn %d",
		       REGNO (x), INSN_UID (insn));
	      fprintf (dump_file, " with reg %d\n", REGNO (newreg));
	    }
	  local_copy_prop_count++;
	  return true;
	}
    }
  return false;
}

/* LIBCALL_SP is a zero-terminated array of insns at the end of a libcall;
   their REG_EQUAL notes need updating to reflect that OLDREG has been
   replaced with NEWVAL in INSN.  Return true if all substitutions could
   be made.  */
static bool
adjust_libcall_notes (rtx oldreg, rtx newval, rtx insn, rtx *libcall_sp)
{
  rtx end;

  while ((end = *libcall_sp++))
    {
      rtx note = find_reg_equal_equiv_note (end);

      if (! note)
	continue;

      if (REG_P (newval))
	{
	  if (reg_set_between_p (newval, PREV_INSN (insn), end))
	    {
	      do
		{
		  note = find_reg_equal_equiv_note (end);
		  if (! note)
		    continue;
		  if (reg_mentioned_p (newval, XEXP (note, 0)))
		    return false;
		}
	      while ((end = *libcall_sp++));
	      return true;
	    }
	}
      XEXP (note, 0) = simplify_replace_rtx (XEXP (note, 0), oldreg, newval);
      insn = end;
    }
  return true;
}

#define MAX_NESTED_LIBCALLS 9

/* Do local const/copy propagation (i.e. within each basic block).
   If ALTER_JUMPS is true, allow propagating into jump insns, which
   could modify the CFG.  */

static void
local_cprop_pass (bool alter_jumps)
{
  basic_block bb;
  rtx insn;
  struct reg_use *reg_used;
  rtx libcall_stack[MAX_NESTED_LIBCALLS + 1], *libcall_sp;
  bool changed = false;

  cselib_init (false);
  libcall_sp = &libcall_stack[MAX_NESTED_LIBCALLS];
  *libcall_sp = 0;
  FOR_EACH_BB (bb)
    {
      FOR_BB_INSNS (bb, insn)
	{
	  if (INSN_P (insn))
	    {
	      rtx note = find_reg_note (insn, REG_LIBCALL, NULL_RTX);

	      if (note)
		{
		  gcc_assert (libcall_sp != libcall_stack);
		  *--libcall_sp = XEXP (note, 0);
		}
	      note = find_reg_note (insn, REG_RETVAL, NULL_RTX);
	      if (note)
		libcall_sp++;
	      note = find_reg_equal_equiv_note (insn);
	      do
		{
		  reg_use_count = 0;
		  note_uses (&PATTERN (insn), local_cprop_find_used_regs,
			     NULL);
		  if (note)
		    local_cprop_find_used_regs (&XEXP (note, 0), NULL);

		  for (reg_used = &reg_use_table[0]; reg_use_count > 0;
		       reg_used++, reg_use_count--)
		    if (do_local_cprop (reg_used->reg_rtx, insn, alter_jumps,
			libcall_sp))
		      {
			changed = true;
			break;
		      }
		  if (INSN_DELETED_P (insn))
		    break;
		}
	      while (reg_use_count);
	    }
	  cselib_process_insn (insn);
	}

      /* Forget everything at the end of a basic block.  Make sure we are
	 not inside a libcall, they should never cross basic blocks.  */
      cselib_clear_table ();
      gcc_assert (libcall_sp == &libcall_stack[MAX_NESTED_LIBCALLS]);
    }

  cselib_finish ();

  /* Global analysis may get into infinite loops for unreachable blocks.  */
  if (changed && alter_jumps)
    {
      delete_unreachable_blocks ();
      free_reg_set_mem ();
      alloc_reg_set_mem (max_reg_num ());
      compute_sets ();
    }
}

/* Forward propagate copies.  This includes copies and constants.  Return
   nonzero if a change was made.  */

static int
cprop (int alter_jumps)
{
  int changed;
  basic_block bb;
  rtx insn;

  /* Note we start at block 1.  */
  if (ENTRY_BLOCK_PTR->next_bb == EXIT_BLOCK_PTR)
    {
      if (dump_file != NULL)
	fprintf (dump_file, "\n");
      return 0;
    }

  changed = 0;
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR->next_bb->next_bb, EXIT_BLOCK_PTR, next_bb)
    {
      /* Reset tables used to keep track of what's still valid [since the
	 start of the block].  */
      reset_opr_set_tables ();

      FOR_BB_INSNS (bb, insn)
	if (INSN_P (insn))
	  {
	    changed |= cprop_insn (insn, alter_jumps);

	    /* Keep track of everything modified by this insn.  */
	    /* ??? Need to be careful w.r.t. mods done to INSN.  Don't
	       call mark_oprs_set if we turned the insn into a NOTE.  */
	    if (! NOTE_P (insn))
	      mark_oprs_set (insn);
	  }
    }

  if (dump_file != NULL)
    fprintf (dump_file, "\n");

  return changed;
}

/* Similar to get_condition, only the resulting condition must be
   valid at JUMP, instead of at EARLIEST.

   This differs from noce_get_condition in ifcvt.c in that we prefer not to
   settle for the condition variable in the jump instruction being integral.
   We prefer to be able to record the value of a user variable, rather than
   the value of a temporary used in a condition.  This could be solved by
   recording the value of *every* register scanned by canonicalize_condition,
   but this would require some code reorganization.  */

rtx
fis_get_condition (rtx jump)
{
  return get_condition (jump, NULL, false, true);
}

/* Check the comparison COND to see if we can safely form an implicit set from
   it.  COND is either an EQ or NE comparison.  */

static bool
implicit_set_cond_p (rtx cond)
{
  enum machine_mode mode = GET_MODE (XEXP (cond, 0));
  rtx cst = XEXP (cond, 1);

  /* We can't perform this optimization if either operand might be or might
     contain a signed zero.  */
  if (HONOR_SIGNED_ZEROS (mode))
    {
      /* It is sufficient to check if CST is or contains a zero.  We must
	 handle float, complex, and vector.  If any subpart is a zero, then
	 the optimization can't be performed.  */
      /* ??? The complex and vector checks are not implemented yet.  We just
	 always return zero for them.  */
      if (GET_CODE (cst) == CONST_DOUBLE)
	{
	  REAL_VALUE_TYPE d;
	  REAL_VALUE_FROM_CONST_DOUBLE (d, cst);
	  if (REAL_VALUES_EQUAL (d, dconst0))
	    return 0;
	}
      else
	return 0;
    }

  return gcse_constant_p (cst);
}

/* Find the implicit sets of a function.  An "implicit set" is a constraint
   on the value of a variable, implied by a conditional jump.  For example,
   following "if (x == 2)", the then branch may be optimized as though the
   conditional performed an "explicit set", in this example, "x = 2".  This
   function records the set patterns that are implicit at the start of each
   basic block.  */

static void
find_implicit_sets (void)
{
  basic_block bb, dest;
  unsigned int count;
  rtx cond, new;

  count = 0;
  FOR_EACH_BB (bb)
    /* Check for more than one successor.  */
    if (EDGE_COUNT (bb->succs) > 1)
      {
	cond = fis_get_condition (BB_END (bb));

	if (cond
	    && (GET_CODE (cond) == EQ || GET_CODE (cond) == NE)
	    && REG_P (XEXP (cond, 0))
	    && REGNO (XEXP (cond, 0)) >= FIRST_PSEUDO_REGISTER
	    && implicit_set_cond_p (cond))
	  {
	    dest = GET_CODE (cond) == EQ ? BRANCH_EDGE (bb)->dest
					 : FALLTHRU_EDGE (bb)->dest;

	    if (dest && single_pred_p (dest)
		&& dest != EXIT_BLOCK_PTR)
	      {
		new = gen_rtx_SET (VOIDmode, XEXP (cond, 0),
					     XEXP (cond, 1));
		implicit_sets[dest->index] = new;
		if (dump_file)
		  {
		    fprintf(dump_file, "Implicit set of reg %d in ",
			    REGNO (XEXP (cond, 0)));
		    fprintf(dump_file, "basic block %d\n", dest->index);
		  }
		count++;
	      }
	  }
      }

  if (dump_file)
    fprintf (dump_file, "Found %d implicit sets\n", count);
}

/* Perform one copy/constant propagation pass.
   PASS is the pass count.  If CPROP_JUMPS is true, perform constant
   propagation into conditional jumps.  If BYPASS_JUMPS is true,
   perform conditional jump bypassing optimizations.  */

static int
one_cprop_pass (int pass, bool cprop_jumps, bool bypass_jumps)
{
  int changed = 0;

  global_const_prop_count = local_const_prop_count = 0;
  global_copy_prop_count = local_copy_prop_count = 0;

  local_cprop_pass (cprop_jumps);

  /* Determine implicit sets.  */
  implicit_sets = XCNEWVEC (rtx, last_basic_block);
  find_implicit_sets ();

  alloc_hash_table (max_cuid, &set_hash_table, 1);
  compute_hash_table (&set_hash_table);

  /* Free implicit_sets before peak usage.  */
  free (implicit_sets);
  implicit_sets = NULL;

  if (dump_file)
    dump_hash_table (dump_file, "SET", &set_hash_table);
  if (set_hash_table.n_elems > 0)
    {
      alloc_cprop_mem (last_basic_block, set_hash_table.n_elems);
      compute_cprop_data ();
      changed = cprop (cprop_jumps);
      if (bypass_jumps)
	changed |= bypass_conditional_jumps ();
      free_cprop_mem ();
    }

  free_hash_table (&set_hash_table);

  if (dump_file)
    {
      fprintf (dump_file, "CPROP of %s, pass %d: %d bytes needed, ",
	       current_function_name (), pass, bytes_used);
      fprintf (dump_file, "%d local const props, %d local copy props, ",
	       local_const_prop_count, local_copy_prop_count);
      fprintf (dump_file, "%d global const props, %d global copy props\n\n",
	       global_const_prop_count, global_copy_prop_count);
    }
  /* Global analysis may get into infinite loops for unreachable blocks.  */
  if (changed && cprop_jumps)
    delete_unreachable_blocks ();

  return changed;
}

/* Bypass conditional jumps.  */

/* The value of last_basic_block at the beginning of the jump_bypass
   pass.  The use of redirect_edge_and_branch_force may introduce new
   basic blocks, but the data flow analysis is only valid for basic
   block indices less than bypass_last_basic_block.  */

static int bypass_last_basic_block;

/* Find a set of REGNO to a constant that is available at the end of basic
   block BB.  Returns NULL if no such set is found.  Based heavily upon
   find_avail_set.  */

static struct expr *
find_bypass_set (int regno, int bb)
{
  struct expr *result = 0;

  for (;;)
    {
      rtx src;
      struct expr *set = lookup_set (regno, &set_hash_table);

      while (set)
	{
	  if (TEST_BIT (cprop_avout[bb], set->bitmap_index))
	    break;
	  set = next_set (regno, set);
	}

      if (set == 0)
	break;

      gcc_assert (GET_CODE (set->expr) == SET);

      src = SET_SRC (set->expr);
      if (gcse_constant_p (src))
	result = set;

      if (! REG_P (src))
	break;

      regno = REGNO (src);
    }
  return result;
}


/* Subroutine of bypass_block that checks whether a pseudo is killed by
   any of the instructions inserted on an edge.  Jump bypassing places
   condition code setters on CFG edges using insert_insn_on_edge.  This
   function is required to check that our data flow analysis is still
   valid prior to commit_edge_insertions.  */

static bool
reg_killed_on_edge (rtx reg, edge e)
{
  rtx insn;

  for (insn = e->insns.r; insn; insn = NEXT_INSN (insn))
    if (INSN_P (insn) && reg_set_p (reg, insn))
      return true;

  return false;
}

/* Subroutine of bypass_conditional_jumps that attempts to bypass the given
   basic block BB which has more than one predecessor.  If not NULL, SETCC
   is the first instruction of BB, which is immediately followed by JUMP_INSN
   JUMP.  Otherwise, SETCC is NULL, and JUMP is the first insn of BB.
   Returns nonzero if a change was made.

   During the jump bypassing pass, we may place copies of SETCC instructions
   on CFG edges.  The following routine must be careful to pay attention to
   these inserted insns when performing its transformations.  */

static int
bypass_block (basic_block bb, rtx setcc, rtx jump)
{
  rtx insn, note;
  edge e, edest;
  int i, change;
  int may_be_loop_header;
  unsigned removed_p;
  edge_iterator ei;

  insn = (setcc != NULL) ? setcc : jump;

  /* Determine set of register uses in INSN.  */
  reg_use_count = 0;
  note_uses (&PATTERN (insn), find_used_regs, NULL);
  note = find_reg_equal_equiv_note (insn);
  if (note)
    find_used_regs (&XEXP (note, 0), NULL);

  may_be_loop_header = false;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->flags & EDGE_DFS_BACK)
      {
	may_be_loop_header = true;
	break;
      }

  change = 0;
  for (ei = ei_start (bb->preds); (e = ei_safe_edge (ei)); )
    {
      removed_p = 0;
	  
      if (e->flags & EDGE_COMPLEX)
	{
	  ei_next (&ei);
	  continue;
	}

      /* We can't redirect edges from new basic blocks.  */
      if (e->src->index >= bypass_last_basic_block)
	{
	  ei_next (&ei);
	  continue;
	}

      /* The irreducible loops created by redirecting of edges entering the
	 loop from outside would decrease effectiveness of some of the following
	 optimizations, so prevent this.  */
      if (may_be_loop_header
	  && !(e->flags & EDGE_DFS_BACK))
	{
	  ei_next (&ei);
	  continue;
	}

      for (i = 0; i < reg_use_count; i++)
	{
	  struct reg_use *reg_used = &reg_use_table[i];
	  unsigned int regno = REGNO (reg_used->reg_rtx);
	  basic_block dest, old_dest;
	  struct expr *set;
	  rtx src, new;

	  if (regno >= max_gcse_regno)
	    continue;

	  set = find_bypass_set (regno, e->src->index);

	  if (! set)
	    continue;

	  /* Check the data flow is valid after edge insertions.  */
	  if (e->insns.r && reg_killed_on_edge (reg_used->reg_rtx, e))
	    continue;

	  src = SET_SRC (pc_set (jump));

	  if (setcc != NULL)
	      src = simplify_replace_rtx (src,
					  SET_DEST (PATTERN (setcc)),
					  SET_SRC (PATTERN (setcc)));

	  new = simplify_replace_rtx (src, reg_used->reg_rtx,
				      SET_SRC (set->expr));

	  /* Jump bypassing may have already placed instructions on
	     edges of the CFG.  We can't bypass an outgoing edge that
	     has instructions associated with it, as these insns won't
	     get executed if the incoming edge is redirected.  */

	  if (new == pc_rtx)
	    {
	      edest = FALLTHRU_EDGE (bb);
	      dest = edest->insns.r ? NULL : edest->dest;
	    }
	  else if (GET_CODE (new) == LABEL_REF)
	    {
	      dest = BLOCK_FOR_INSN (XEXP (new, 0));
	      /* Don't bypass edges containing instructions.  */
	      edest = find_edge (bb, dest);
	      if (edest && edest->insns.r)
		dest = NULL;
	    }
	  else
	    dest = NULL;

	  /* Avoid unification of the edge with other edges from original
	     branch.  We would end up emitting the instruction on "both"
	     edges.  */

	  if (dest && setcc && !CC0_P (SET_DEST (PATTERN (setcc)))
	      && find_edge (e->src, dest))
	    dest = NULL;

	  old_dest = e->dest;
	  if (dest != NULL
	      && dest != old_dest
	      && dest != EXIT_BLOCK_PTR)
            {
	      redirect_edge_and_branch_force (e, dest);

	      /* Copy the register setter to the redirected edge.
		 Don't copy CC0 setters, as CC0 is dead after jump.  */
	      if (setcc)
		{
		  rtx pat = PATTERN (setcc);
		  if (!CC0_P (SET_DEST (pat)))
		    insert_insn_on_edge (copy_insn (pat), e);
		}

	      if (dump_file != NULL)
		{
		  fprintf (dump_file, "JUMP-BYPASS: Proved reg %d "
				      "in jump_insn %d equals constant ",
			   regno, INSN_UID (jump));
		  print_rtl (dump_file, SET_SRC (set->expr));
		  fprintf (dump_file, "\nBypass edge from %d->%d to %d\n",
			   e->src->index, old_dest->index, dest->index);
		}
	      change = 1;
	      removed_p = 1;
	      break;
	    }
	}
      if (!removed_p)
	ei_next (&ei);
    }
  return change;
}

/* Find basic blocks with more than one predecessor that only contain a
   single conditional jump.  If the result of the comparison is known at
   compile-time from any incoming edge, redirect that edge to the
   appropriate target.  Returns nonzero if a change was made.

   This function is now mis-named, because we also handle indirect jumps.  */

static int
bypass_conditional_jumps (void)
{
  basic_block bb;
  int changed;
  rtx setcc;
  rtx insn;
  rtx dest;

  /* Note we start at block 1.  */
  if (ENTRY_BLOCK_PTR->next_bb == EXIT_BLOCK_PTR)
    return 0;

  bypass_last_basic_block = last_basic_block;
  mark_dfs_back_edges ();

  changed = 0;
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR->next_bb->next_bb,
		  EXIT_BLOCK_PTR, next_bb)
    {
      /* Check for more than one predecessor.  */
      if (!single_pred_p (bb))
	{
	  setcc = NULL_RTX;
	  FOR_BB_INSNS (bb, insn)
	    if (NONJUMP_INSN_P (insn))
	      {
		if (setcc)
		  break;
		if (GET_CODE (PATTERN (insn)) != SET)
		  break;

		dest = SET_DEST (PATTERN (insn));
		if (REG_P (dest) || CC0_P (dest))
		  setcc = insn;
		else
		  break;
	      }
	    else if (JUMP_P (insn))
	      {
		if ((any_condjump_p (insn) || computed_jump_p (insn))
		    && onlyjump_p (insn))
		  changed |= bypass_block (bb, setcc, insn);
		break;
	      }
	    else if (INSN_P (insn))
	      break;
	}
    }

  /* If we bypassed any register setting insns, we inserted a
     copy on the redirected edge.  These need to be committed.  */
  if (changed)
    commit_edge_insertions();

  return changed;
}

/* Compute PRE+LCM working variables.  */

/* Local properties of expressions.  */
/* Nonzero for expressions that are transparent in the block.  */
static sbitmap *transp;

/* Nonzero for expressions that are transparent at the end of the block.
   This is only zero for expressions killed by abnormal critical edge
   created by a calls.  */
static sbitmap *transpout;

/* Nonzero for expressions that are computed (available) in the block.  */
static sbitmap *comp;

/* Nonzero for expressions that are locally anticipatable in the block.  */
static sbitmap *antloc;

/* Nonzero for expressions where this block is an optimal computation
   point.  */
static sbitmap *pre_optimal;

/* Nonzero for expressions which are redundant in a particular block.  */
static sbitmap *pre_redundant;

/* Nonzero for expressions which should be inserted on a specific edge.  */
static sbitmap *pre_insert_map;

/* Nonzero for expressions which should be deleted in a specific block.  */
static sbitmap *pre_delete_map;

/* Contains the edge_list returned by pre_edge_lcm.  */
static struct edge_list *edge_list;

/* Redundant insns.  */
static sbitmap pre_redundant_insns;

/* Allocate vars used for PRE analysis.  */

static void
alloc_pre_mem (int n_blocks, int n_exprs)
{
  transp = sbitmap_vector_alloc (n_blocks, n_exprs);
  comp = sbitmap_vector_alloc (n_blocks, n_exprs);
  antloc = sbitmap_vector_alloc (n_blocks, n_exprs);

  pre_optimal = NULL;
  pre_redundant = NULL;
  pre_insert_map = NULL;
  pre_delete_map = NULL;
  ae_kill = sbitmap_vector_alloc (n_blocks, n_exprs);

  /* pre_insert and pre_delete are allocated later.  */
}

/* Free vars used for PRE analysis.  */

static void
free_pre_mem (void)
{
  sbitmap_vector_free (transp);
  sbitmap_vector_free (comp);

  /* ANTLOC and AE_KILL are freed just after pre_lcm finishes.  */

  if (pre_optimal)
    sbitmap_vector_free (pre_optimal);
  if (pre_redundant)
    sbitmap_vector_free (pre_redundant);
  if (pre_insert_map)
    sbitmap_vector_free (pre_insert_map);
  if (pre_delete_map)
    sbitmap_vector_free (pre_delete_map);

  transp = comp = NULL;
  pre_optimal = pre_redundant = pre_insert_map = pre_delete_map = NULL;
}

/* Top level routine to do the dataflow analysis needed by PRE.  */

static void
compute_pre_data (void)
{
  sbitmap trapping_expr;
  basic_block bb;
  unsigned int ui;

  compute_local_properties (transp, comp, antloc, &expr_hash_table);
  sbitmap_vector_zero (ae_kill, last_basic_block);

  /* Collect expressions which might trap.  */
  trapping_expr = sbitmap_alloc (expr_hash_table.n_elems);
  sbitmap_zero (trapping_expr);
  for (ui = 0; ui < expr_hash_table.size; ui++)
    {
      struct expr *e;
      for (e = expr_hash_table.table[ui]; e != NULL; e = e->next_same_hash)
	if (may_trap_p (e->expr))
	  SET_BIT (trapping_expr, e->bitmap_index);
    }

  /* Compute ae_kill for each basic block using:

     ~(TRANSP | COMP)
  */

  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;

      /* If the current block is the destination of an abnormal edge, we
	 kill all trapping expressions because we won't be able to properly
	 place the instruction on the edge.  So make them neither
	 anticipatable nor transparent.  This is fairly conservative.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	if (e->flags & EDGE_ABNORMAL)
	  {
	    sbitmap_difference (antloc[bb->index], antloc[bb->index], trapping_expr);
	    sbitmap_difference (transp[bb->index], transp[bb->index], trapping_expr);
	    break;
	  }

      sbitmap_a_or_b (ae_kill[bb->index], transp[bb->index], comp[bb->index]);
      sbitmap_not (ae_kill[bb->index], ae_kill[bb->index]);
    }

  edge_list = pre_edge_lcm (expr_hash_table.n_elems, transp, comp, antloc,
			    ae_kill, &pre_insert_map, &pre_delete_map);
  sbitmap_vector_free (antloc);
  antloc = NULL;
  sbitmap_vector_free (ae_kill);
  ae_kill = NULL;
  sbitmap_free (trapping_expr);
}

/* PRE utilities */

/* Return nonzero if an occurrence of expression EXPR in OCCR_BB would reach
   block BB.

   VISITED is a pointer to a working buffer for tracking which BB's have
   been visited.  It is NULL for the top-level call.

   We treat reaching expressions that go through blocks containing the same
   reaching expression as "not reaching".  E.g. if EXPR is generated in blocks
   2 and 3, INSN is in block 4, and 2->3->4, we treat the expression in block
   2 as not reaching.  The intent is to improve the probability of finding
   only one reaching expression and to reduce register lifetimes by picking
   the closest such expression.  */

static int
pre_expr_reaches_here_p_work (basic_block occr_bb, struct expr *expr, basic_block bb, char *visited)
{
  edge pred;
  edge_iterator ei;
  
  FOR_EACH_EDGE (pred, ei, bb->preds)
    {
      basic_block pred_bb = pred->src;

      if (pred->src == ENTRY_BLOCK_PTR
	  /* Has predecessor has already been visited?  */
	  || visited[pred_bb->index])
	;/* Nothing to do.  */

      /* Does this predecessor generate this expression?  */
      else if (TEST_BIT (comp[pred_bb->index], expr->bitmap_index))
	{
	  /* Is this the occurrence we're looking for?
	     Note that there's only one generating occurrence per block
	     so we just need to check the block number.  */
	  if (occr_bb == pred_bb)
	    return 1;

	  visited[pred_bb->index] = 1;
	}
      /* Ignore this predecessor if it kills the expression.  */
      else if (! TEST_BIT (transp[pred_bb->index], expr->bitmap_index))
	visited[pred_bb->index] = 1;

      /* Neither gen nor kill.  */
      else
	{
	  visited[pred_bb->index] = 1;
	  if (pre_expr_reaches_here_p_work (occr_bb, expr, pred_bb, visited))
	    return 1;
	}
    }

  /* All paths have been checked.  */
  return 0;
}

/* The wrapper for pre_expr_reaches_here_work that ensures that any
   memory allocated for that function is returned.  */

static int
pre_expr_reaches_here_p (basic_block occr_bb, struct expr *expr, basic_block bb)
{
  int rval;
  char *visited = XCNEWVEC (char, last_basic_block);

  rval = pre_expr_reaches_here_p_work (occr_bb, expr, bb, visited);

  free (visited);
  return rval;
}


/* Given an expr, generate RTL which we can insert at the end of a BB,
   or on an edge.  Set the block number of any insns generated to
   the value of BB.  */

static rtx
process_insert_insn (struct expr *expr)
{
  rtx reg = expr->reaching_reg;
  rtx exp = copy_rtx (expr->expr);
  rtx pat;

  start_sequence ();

  /* If the expression is something that's an operand, like a constant,
     just copy it to a register.  */
  if (general_operand (exp, GET_MODE (reg)))
    emit_move_insn (reg, exp);

  /* Otherwise, make a new insn to compute this expression and make sure the
     insn will be recognized (this also adds any needed CLOBBERs).  Copy the
     expression to make sure we don't have any sharing issues.  */
  else
    {
      rtx insn = emit_insn (gen_rtx_SET (VOIDmode, reg, exp));

      if (insn_invalid_p (insn))
	gcc_unreachable ();
    }
  

  pat = get_insns ();
  end_sequence ();

  return pat;
}

/* Add EXPR to the end of basic block BB.

   This is used by both the PRE and code hoisting.

   For PRE, we want to verify that the expr is either transparent
   or locally anticipatable in the target block.  This check makes
   no sense for code hoisting.  */

static void
insert_insn_end_bb (struct expr *expr, basic_block bb, int pre)
{
  rtx insn = BB_END (bb);
  rtx new_insn;
  rtx reg = expr->reaching_reg;
  int regno = REGNO (reg);
  rtx pat, pat_end;

  pat = process_insert_insn (expr);
  gcc_assert (pat && INSN_P (pat));

  pat_end = pat;
  while (NEXT_INSN (pat_end) != NULL_RTX)
    pat_end = NEXT_INSN (pat_end);

  /* If the last insn is a jump, insert EXPR in front [taking care to
     handle cc0, etc. properly].  Similarly we need to care trapping
     instructions in presence of non-call exceptions.  */

  if (JUMP_P (insn)
      || (NONJUMP_INSN_P (insn)
	  && (!single_succ_p (bb)
	      || single_succ_edge (bb)->flags & EDGE_ABNORMAL)))
    {
#ifdef HAVE_cc0
      rtx note;
#endif
      /* It should always be the case that we can put these instructions
	 anywhere in the basic block with performing PRE optimizations.
	 Check this.  */
      gcc_assert (!NONJUMP_INSN_P (insn) || !pre
		  || TEST_BIT (antloc[bb->index], expr->bitmap_index)
		  || TEST_BIT (transp[bb->index], expr->bitmap_index));

      /* If this is a jump table, then we can't insert stuff here.  Since
	 we know the previous real insn must be the tablejump, we insert
	 the new instruction just before the tablejump.  */
      if (GET_CODE (PATTERN (insn)) == ADDR_VEC
	  || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC)
	insn = prev_real_insn (insn);

#ifdef HAVE_cc0
      /* FIXME: 'twould be nice to call prev_cc0_setter here but it aborts
	 if cc0 isn't set.  */
      note = find_reg_note (insn, REG_CC_SETTER, NULL_RTX);
      if (note)
	insn = XEXP (note, 0);
      else
	{
	  rtx maybe_cc0_setter = prev_nonnote_insn (insn);
	  if (maybe_cc0_setter
	      && INSN_P (maybe_cc0_setter)
	      && sets_cc0_p (PATTERN (maybe_cc0_setter)))
	    insn = maybe_cc0_setter;
	}
#endif
      /* FIXME: What if something in cc0/jump uses value set in new insn?  */
      new_insn = emit_insn_before_noloc (pat, insn);
    }

  /* Likewise if the last insn is a call, as will happen in the presence
     of exception handling.  */
  else if (CALL_P (insn)
	   && (!single_succ_p (bb)
	       || single_succ_edge (bb)->flags & EDGE_ABNORMAL))
    {
      /* Keeping in mind SMALL_REGISTER_CLASSES and parameters in registers,
	 we search backward and place the instructions before the first
	 parameter is loaded.  Do this for everyone for consistency and a
	 presumption that we'll get better code elsewhere as well.

	 It should always be the case that we can put these instructions
	 anywhere in the basic block with performing PRE optimizations.
	 Check this.  */

      gcc_assert (!pre
		  || TEST_BIT (antloc[bb->index], expr->bitmap_index)
		  || TEST_BIT (transp[bb->index], expr->bitmap_index));

      /* Since different machines initialize their parameter registers
	 in different orders, assume nothing.  Collect the set of all
	 parameter registers.  */
      insn = find_first_parameter_load (insn, BB_HEAD (bb));

      /* If we found all the parameter loads, then we want to insert
	 before the first parameter load.

	 If we did not find all the parameter loads, then we might have
	 stopped on the head of the block, which could be a CODE_LABEL.
	 If we inserted before the CODE_LABEL, then we would be putting
	 the insn in the wrong basic block.  In that case, put the insn
	 after the CODE_LABEL.  Also, respect NOTE_INSN_BASIC_BLOCK.  */
      while (LABEL_P (insn)
	     || NOTE_INSN_BASIC_BLOCK_P (insn))
	insn = NEXT_INSN (insn);

      new_insn = emit_insn_before_noloc (pat, insn);
    }
  else
    new_insn = emit_insn_after_noloc (pat, insn);

  while (1)
    {
      if (INSN_P (pat))
	{
	  add_label_notes (PATTERN (pat), new_insn);
	  note_stores (PATTERN (pat), record_set_info, pat);
	}
      if (pat == pat_end)
	break;
      pat = NEXT_INSN (pat);
    }

  gcse_create_count++;

  if (dump_file)
    {
      fprintf (dump_file, "PRE/HOIST: end of bb %d, insn %d, ",
	       bb->index, INSN_UID (new_insn));
      fprintf (dump_file, "copying expression %d to reg %d\n",
	       expr->bitmap_index, regno);
    }
}

/* Insert partially redundant expressions on edges in the CFG to make
   the expressions fully redundant.  */

static int
pre_edge_insert (struct edge_list *edge_list, struct expr **index_map)
{
  int e, i, j, num_edges, set_size, did_insert = 0;
  sbitmap *inserted;

  /* Where PRE_INSERT_MAP is nonzero, we add the expression on that edge
     if it reaches any of the deleted expressions.  */

  set_size = pre_insert_map[0]->size;
  num_edges = NUM_EDGES (edge_list);
  inserted = sbitmap_vector_alloc (num_edges, expr_hash_table.n_elems);
  sbitmap_vector_zero (inserted, num_edges);

  for (e = 0; e < num_edges; e++)
    {
      int indx;
      basic_block bb = INDEX_EDGE_PRED_BB (edge_list, e);

      for (i = indx = 0; i < set_size; i++, indx += SBITMAP_ELT_BITS)
	{
	  SBITMAP_ELT_TYPE insert = pre_insert_map[e]->elms[i];

	  for (j = indx; insert && j < (int) expr_hash_table.n_elems; j++, insert >>= 1)
	    if ((insert & 1) != 0 && index_map[j]->reaching_reg != NULL_RTX)
	      {
		struct expr *expr = index_map[j];
		struct occr *occr;

		/* Now look at each deleted occurrence of this expression.  */
		for (occr = expr->antic_occr; occr != NULL; occr = occr->next)
		  {
		    if (! occr->deleted_p)
		      continue;

		    /* Insert this expression on this edge if it would
		       reach the deleted occurrence in BB.  */
		    if (!TEST_BIT (inserted[e], j))
		      {
			rtx insn;
			edge eg = INDEX_EDGE (edge_list, e);

			/* We can't insert anything on an abnormal and
			   critical edge, so we insert the insn at the end of
			   the previous block. There are several alternatives
			   detailed in Morgans book P277 (sec 10.5) for
			   handling this situation.  This one is easiest for
			   now.  */

			if (eg->flags & EDGE_ABNORMAL)
			  insert_insn_end_bb (index_map[j], bb, 0);
			else
			  {
			    insn = process_insert_insn (index_map[j]);
			    insert_insn_on_edge (insn, eg);
			  }

			if (dump_file)
			  {
			    fprintf (dump_file, "PRE/HOIST: edge (%d,%d), ",
				     bb->index,
				     INDEX_EDGE_SUCC_BB (edge_list, e)->index);
			    fprintf (dump_file, "copy expression %d\n",
				     expr->bitmap_index);
			  }

			update_ld_motion_stores (expr);
			SET_BIT (inserted[e], j);
			did_insert = 1;
			gcse_create_count++;
		      }
		  }
	      }
	}
    }

  sbitmap_vector_free (inserted);
  return did_insert;
}

/* Copy the result of EXPR->EXPR generated by INSN to EXPR->REACHING_REG.
   Given "old_reg <- expr" (INSN), instead of adding after it
     reaching_reg <- old_reg
   it's better to do the following:
     reaching_reg <- expr
     old_reg      <- reaching_reg
   because this way copy propagation can discover additional PRE
   opportunities.  But if this fails, we try the old way.
   When "expr" is a store, i.e.
   given "MEM <- old_reg", instead of adding after it
     reaching_reg <- old_reg
   it's better to add it before as follows:
     reaching_reg <- old_reg
     MEM          <- reaching_reg.  */

static void
pre_insert_copy_insn (struct expr *expr, rtx insn)
{
  rtx reg = expr->reaching_reg;
  int regno = REGNO (reg);
  int indx = expr->bitmap_index;
  rtx pat = PATTERN (insn);
  rtx set, first_set, new_insn;
  rtx old_reg;
  int i;

  /* This block matches the logic in hash_scan_insn.  */
  switch (GET_CODE (pat))
    {
    case SET:
      set = pat;
      break;

    case PARALLEL:
      /* Search through the parallel looking for the set whose
	 source was the expression that we're interested in.  */
      first_set = NULL_RTX;
      set = NULL_RTX;
      for (i = 0; i < XVECLEN (pat, 0); i++)
	{
	  rtx x = XVECEXP (pat, 0, i);
	  if (GET_CODE (x) == SET)
	    {
	      /* If the source was a REG_EQUAL or REG_EQUIV note, we
		 may not find an equivalent expression, but in this
		 case the PARALLEL will have a single set.  */
	      if (first_set == NULL_RTX)
		first_set = x;
	      if (expr_equiv_p (SET_SRC (x), expr->expr))
	        {
	          set = x;
	          break;
	        }
	    }
	}

      gcc_assert (first_set);
      if (set == NULL_RTX)
        set = first_set;
      break;

    default:
      gcc_unreachable ();
    }

  if (REG_P (SET_DEST (set)))
    {
      old_reg = SET_DEST (set);
      /* Check if we can modify the set destination in the original insn.  */
      if (validate_change (insn, &SET_DEST (set), reg, 0))
        {
          new_insn = gen_move_insn (old_reg, reg);
          new_insn = emit_insn_after (new_insn, insn);

          /* Keep register set table up to date.  */
          record_one_set (regno, insn);
        }
      else
        {
          new_insn = gen_move_insn (reg, old_reg);
          new_insn = emit_insn_after (new_insn, insn);

          /* Keep register set table up to date.  */
          record_one_set (regno, new_insn);
        }
    }
  else /* This is possible only in case of a store to memory.  */
    {
      old_reg = SET_SRC (set);
      new_insn = gen_move_insn (reg, old_reg);

      /* Check if we can modify the set source in the original insn.  */
      if (validate_change (insn, &SET_SRC (set), reg, 0))
        new_insn = emit_insn_before (new_insn, insn);
      else
        new_insn = emit_insn_after (new_insn, insn);

      /* Keep register set table up to date.  */
      record_one_set (regno, new_insn);
    }

  gcse_create_count++;

  if (dump_file)
    fprintf (dump_file,
	     "PRE: bb %d, insn %d, copy expression %d in insn %d to reg %d\n",
	      BLOCK_NUM (insn), INSN_UID (new_insn), indx,
	      INSN_UID (insn), regno);
}

/* Copy available expressions that reach the redundant expression
   to `reaching_reg'.  */

static void
pre_insert_copies (void)
{
  unsigned int i, added_copy;
  struct expr *expr;
  struct occr *occr;
  struct occr *avail;

  /* For each available expression in the table, copy the result to
     `reaching_reg' if the expression reaches a deleted one.

     ??? The current algorithm is rather brute force.
     Need to do some profiling.  */

  for (i = 0; i < expr_hash_table.size; i++)
    for (expr = expr_hash_table.table[i]; expr != NULL; expr = expr->next_same_hash)
      {
	/* If the basic block isn't reachable, PPOUT will be TRUE.  However,
	   we don't want to insert a copy here because the expression may not
	   really be redundant.  So only insert an insn if the expression was
	   deleted.  This test also avoids further processing if the
	   expression wasn't deleted anywhere.  */
	if (expr->reaching_reg == NULL)
	  continue;

	/* Set when we add a copy for that expression.  */
	added_copy = 0;

	for (occr = expr->antic_occr; occr != NULL; occr = occr->next)
	  {
	    if (! occr->deleted_p)
	      continue;

	    for (avail = expr->avail_occr; avail != NULL; avail = avail->next)
	      {
		rtx insn = avail->insn;

		/* No need to handle this one if handled already.  */
		if (avail->copied_p)
		  continue;

		/* Don't handle this one if it's a redundant one.  */
		if (TEST_BIT (pre_redundant_insns, INSN_CUID (insn)))
		  continue;

		/* Or if the expression doesn't reach the deleted one.  */
		if (! pre_expr_reaches_here_p (BLOCK_FOR_INSN (avail->insn),
					       expr,
					       BLOCK_FOR_INSN (occr->insn)))
		  continue;

                added_copy = 1;

		/* Copy the result of avail to reaching_reg.  */
		pre_insert_copy_insn (expr, insn);
		avail->copied_p = 1;
	      }
	  }

	  if (added_copy)
            update_ld_motion_stores (expr);
      }
}

/* Emit move from SRC to DEST noting the equivalence with expression computed
   in INSN.  */
static rtx
gcse_emit_move_after (rtx src, rtx dest, rtx insn)
{
  rtx new;
  rtx set = single_set (insn), set2;
  rtx note;
  rtx eqv;

  /* This should never fail since we're creating a reg->reg copy
     we've verified to be valid.  */

  new = emit_insn_after (gen_move_insn (dest, src), insn);

  /* Note the equivalence for local CSE pass.  */
  set2 = single_set (new);
  if (!set2 || !rtx_equal_p (SET_DEST (set2), dest))
    return new;
  if ((note = find_reg_equal_equiv_note (insn)))
    eqv = XEXP (note, 0);
  else
    eqv = SET_SRC (set);

  set_unique_reg_note (new, REG_EQUAL, copy_insn_1 (eqv));

  return new;
}

/* Delete redundant computations.
   Deletion is done by changing the insn to copy the `reaching_reg' of
   the expression into the result of the SET.  It is left to later passes
   (cprop, cse2, flow, combine, regmove) to propagate the copy or eliminate it.

   Returns nonzero if a change is made.  */

static int
pre_delete (void)
{
  unsigned int i;
  int changed;
  struct expr *expr;
  struct occr *occr;

  changed = 0;
  for (i = 0; i < expr_hash_table.size; i++)
    for (expr = expr_hash_table.table[i];
	 expr != NULL;
	 expr = expr->next_same_hash)
      {
	int indx = expr->bitmap_index;

	/* We only need to search antic_occr since we require
	   ANTLOC != 0.  */

	for (occr = expr->antic_occr; occr != NULL; occr = occr->next)
	  {
	    rtx insn = occr->insn;
	    rtx set;
	    basic_block bb = BLOCK_FOR_INSN (insn);

	    /* We only delete insns that have a single_set.  */
	    if (TEST_BIT (pre_delete_map[bb->index], indx)
		&& (set = single_set (insn)) != 0)
	      {
		/* Create a pseudo-reg to store the result of reaching
		   expressions into.  Get the mode for the new pseudo from
		   the mode of the original destination pseudo.  */
		if (expr->reaching_reg == NULL)
		  expr->reaching_reg
		    = gen_reg_rtx (GET_MODE (SET_DEST (set)));

		gcse_emit_move_after (expr->reaching_reg, SET_DEST (set), insn);
		delete_insn (insn);
		occr->deleted_p = 1;
		SET_BIT (pre_redundant_insns, INSN_CUID (insn));
		changed = 1;
		gcse_subst_count++;

		if (dump_file)
		  {
		    fprintf (dump_file,
			     "PRE: redundant insn %d (expression %d) in ",
			       INSN_UID (insn), indx);
		    fprintf (dump_file, "bb %d, reaching reg is %d\n",
			     bb->index, REGNO (expr->reaching_reg));
		  }
	      }
	  }
      }

  return changed;
}

/* Perform GCSE optimizations using PRE.
   This is called by one_pre_gcse_pass after all the dataflow analysis
   has been done.

   This is based on the original Morel-Renvoise paper Fred Chow's thesis, and
   lazy code motion from Knoop, Ruthing and Steffen as described in Advanced
   Compiler Design and Implementation.

   ??? A new pseudo reg is created to hold the reaching expression.  The nice
   thing about the classical approach is that it would try to use an existing
   reg.  If the register can't be adequately optimized [i.e. we introduce
   reload problems], one could add a pass here to propagate the new register
   through the block.

   ??? We don't handle single sets in PARALLELs because we're [currently] not
   able to copy the rest of the parallel when we insert copies to create full
   redundancies from partial redundancies.  However, there's no reason why we
   can't handle PARALLELs in the cases where there are no partial
   redundancies.  */

static int
pre_gcse (void)
{
  unsigned int i;
  int did_insert, changed;
  struct expr **index_map;
  struct expr *expr;

  /* Compute a mapping from expression number (`bitmap_index') to
     hash table entry.  */

  index_map = XCNEWVEC (struct expr *, expr_hash_table.n_elems);
  for (i = 0; i < expr_hash_table.size; i++)
    for (expr = expr_hash_table.table[i]; expr != NULL; expr = expr->next_same_hash)
      index_map[expr->bitmap_index] = expr;

  /* Reset bitmap used to track which insns are redundant.  */
  pre_redundant_insns = sbitmap_alloc (max_cuid);
  sbitmap_zero (pre_redundant_insns);

  /* Delete the redundant insns first so that
     - we know what register to use for the new insns and for the other
       ones with reaching expressions
     - we know which insns are redundant when we go to create copies  */

  changed = pre_delete ();

  did_insert = pre_edge_insert (edge_list, index_map);

  /* In other places with reaching expressions, copy the expression to the
     specially allocated pseudo-reg that reaches the redundant expr.  */
  pre_insert_copies ();
  if (did_insert)
    {
      commit_edge_insertions ();
      changed = 1;
    }

  free (index_map);
  sbitmap_free (pre_redundant_insns);
  return changed;
}

/* Top level routine to perform one PRE GCSE pass.

   Return nonzero if a change was made.  */

static int
one_pre_gcse_pass (int pass)
{
  int changed = 0;

  gcse_subst_count = 0;
  gcse_create_count = 0;

  alloc_hash_table (max_cuid, &expr_hash_table, 0);
  add_noreturn_fake_exit_edges ();
  if (flag_gcse_lm)
    compute_ld_motion_mems ();

  compute_hash_table (&expr_hash_table);
  trim_ld_motion_mems ();
  if (dump_file)
    dump_hash_table (dump_file, "Expression", &expr_hash_table);

  if (expr_hash_table.n_elems > 0)
    {
      alloc_pre_mem (last_basic_block, expr_hash_table.n_elems);
      compute_pre_data ();
      changed |= pre_gcse ();
      free_edge_list (edge_list);
      free_pre_mem ();
    }

  free_ldst_mems ();
  remove_fake_exit_edges ();
  free_hash_table (&expr_hash_table);

  if (dump_file)
    {
      fprintf (dump_file, "\nPRE GCSE of %s, pass %d: %d bytes needed, ",
	       current_function_name (), pass, bytes_used);
      fprintf (dump_file, "%d substs, %d insns created\n",
	       gcse_subst_count, gcse_create_count);
    }

  return changed;
}

/* If X contains any LABEL_REF's, add REG_LABEL notes for them to INSN.
   If notes are added to an insn which references a CODE_LABEL, the
   LABEL_NUSES count is incremented.  We have to add REG_LABEL notes,
   because the following loop optimization pass requires them.  */

/* ??? If there was a jump optimization pass after gcse and before loop,
   then we would not need to do this here, because jump would add the
   necessary REG_LABEL notes.  */

static void
add_label_notes (rtx x, rtx insn)
{
  enum rtx_code code = GET_CODE (x);
  int i, j;
  const char *fmt;

  if (code == LABEL_REF && !LABEL_REF_NONLOCAL_P (x))
    {
      /* This code used to ignore labels that referred to dispatch tables to
	 avoid flow generating (slightly) worse code.

	 We no longer ignore such label references (see LABEL_REF handling in
	 mark_jump_label for additional information).  */

      REG_NOTES (insn) = gen_rtx_INSN_LIST (REG_LABEL, XEXP (x, 0),
					    REG_NOTES (insn));
      if (LABEL_P (XEXP (x, 0)))
	LABEL_NUSES (XEXP (x, 0))++;
      return;
    }

  for (i = GET_RTX_LENGTH (code) - 1, fmt = GET_RTX_FORMAT (code); i >= 0; i--)
    {
      if (fmt[i] == 'e')
	add_label_notes (XEXP (x, i), insn);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  add_label_notes (XVECEXP (x, i, j), insn);
    }
}

/* Compute transparent outgoing information for each block.

   An expression is transparent to an edge unless it is killed by
   the edge itself.  This can only happen with abnormal control flow,
   when the edge is traversed through a call.  This happens with
   non-local labels and exceptions.

   This would not be necessary if we split the edge.  While this is
   normally impossible for abnormal critical edges, with some effort
   it should be possible with exception handling, since we still have
   control over which handler should be invoked.  But due to increased
   EH table sizes, this may not be worthwhile.  */

static void
compute_transpout (void)
{
  basic_block bb;
  unsigned int i;
  struct expr *expr;

  sbitmap_vector_ones (transpout, last_basic_block);

  FOR_EACH_BB (bb)
    {
      /* Note that flow inserted a nop a the end of basic blocks that
	 end in call instructions for reasons other than abnormal
	 control flow.  */
      if (! CALL_P (BB_END (bb)))
	continue;

      for (i = 0; i < expr_hash_table.size; i++)
	for (expr = expr_hash_table.table[i]; expr ; expr = expr->next_same_hash)
	  if (MEM_P (expr->expr))
	    {
	      if (GET_CODE (XEXP (expr->expr, 0)) == SYMBOL_REF
		  && CONSTANT_POOL_ADDRESS_P (XEXP (expr->expr, 0)))
		continue;

	      /* ??? Optimally, we would use interprocedural alias
		 analysis to determine if this mem is actually killed
		 by this call.  */
	      RESET_BIT (transpout[bb->index], expr->bitmap_index);
	    }
    }
}

/* Code Hoisting variables and subroutines.  */

/* Very busy expressions.  */
static sbitmap *hoist_vbein;
static sbitmap *hoist_vbeout;

/* Hoistable expressions.  */
static sbitmap *hoist_exprs;

/* ??? We could compute post dominators and run this algorithm in
   reverse to perform tail merging, doing so would probably be
   more effective than the tail merging code in jump.c.

   It's unclear if tail merging could be run in parallel with
   code hoisting.  It would be nice.  */

/* Allocate vars used for code hoisting analysis.  */

static void
alloc_code_hoist_mem (int n_blocks, int n_exprs)
{
  antloc = sbitmap_vector_alloc (n_blocks, n_exprs);
  transp = sbitmap_vector_alloc (n_blocks, n_exprs);
  comp = sbitmap_vector_alloc (n_blocks, n_exprs);

  hoist_vbein = sbitmap_vector_alloc (n_blocks, n_exprs);
  hoist_vbeout = sbitmap_vector_alloc (n_blocks, n_exprs);
  hoist_exprs = sbitmap_vector_alloc (n_blocks, n_exprs);
  transpout = sbitmap_vector_alloc (n_blocks, n_exprs);
}

/* Free vars used for code hoisting analysis.  */

static void
free_code_hoist_mem (void)
{
  sbitmap_vector_free (antloc);
  sbitmap_vector_free (transp);
  sbitmap_vector_free (comp);

  sbitmap_vector_free (hoist_vbein);
  sbitmap_vector_free (hoist_vbeout);
  sbitmap_vector_free (hoist_exprs);
  sbitmap_vector_free (transpout);

  free_dominance_info (CDI_DOMINATORS);
}

/* Compute the very busy expressions at entry/exit from each block.

   An expression is very busy if all paths from a given point
   compute the expression.  */

static void
compute_code_hoist_vbeinout (void)
{
  int changed, passes;
  basic_block bb;

  sbitmap_vector_zero (hoist_vbeout, last_basic_block);
  sbitmap_vector_zero (hoist_vbein, last_basic_block);

  passes = 0;
  changed = 1;

  while (changed)
    {
      changed = 0;

      /* We scan the blocks in the reverse order to speed up
	 the convergence.  */
      FOR_EACH_BB_REVERSE (bb)
	{
	  changed |= sbitmap_a_or_b_and_c_cg (hoist_vbein[bb->index], antloc[bb->index],
					      hoist_vbeout[bb->index], transp[bb->index]);
	  if (bb->next_bb != EXIT_BLOCK_PTR)
	    sbitmap_intersection_of_succs (hoist_vbeout[bb->index], hoist_vbein, bb->index);
	}

      passes++;
    }

  if (dump_file)
    fprintf (dump_file, "hoisting vbeinout computation: %d passes\n", passes);
}

/* Top level routine to do the dataflow analysis needed by code hoisting.  */

static void
compute_code_hoist_data (void)
{
  compute_local_properties (transp, comp, antloc, &expr_hash_table);
  compute_transpout ();
  compute_code_hoist_vbeinout ();
  calculate_dominance_info (CDI_DOMINATORS);
  if (dump_file)
    fprintf (dump_file, "\n");
}

/* Determine if the expression identified by EXPR_INDEX would
   reach BB unimpared if it was placed at the end of EXPR_BB.

   It's unclear exactly what Muchnick meant by "unimpared".  It seems
   to me that the expression must either be computed or transparent in
   *every* block in the path(s) from EXPR_BB to BB.  Any other definition
   would allow the expression to be hoisted out of loops, even if
   the expression wasn't a loop invariant.

   Contrast this to reachability for PRE where an expression is
   considered reachable if *any* path reaches instead of *all*
   paths.  */

static int
hoist_expr_reaches_here_p (basic_block expr_bb, int expr_index, basic_block bb, char *visited)
{
  edge pred;
  edge_iterator ei;
  int visited_allocated_locally = 0;


  if (visited == NULL)
    {
      visited_allocated_locally = 1;
      visited = XCNEWVEC (char, last_basic_block);
    }

  FOR_EACH_EDGE (pred, ei, bb->preds)
    {
      basic_block pred_bb = pred->src;

      if (pred->src == ENTRY_BLOCK_PTR)
	break;
      else if (pred_bb == expr_bb)
	continue;
      else if (visited[pred_bb->index])
	continue;

      /* Does this predecessor generate this expression?  */
      else if (TEST_BIT (comp[pred_bb->index], expr_index))
	break;
      else if (! TEST_BIT (transp[pred_bb->index], expr_index))
	break;

      /* Not killed.  */
      else
	{
	  visited[pred_bb->index] = 1;
	  if (! hoist_expr_reaches_here_p (expr_bb, expr_index,
					   pred_bb, visited))
	    break;
	}
    }
  if (visited_allocated_locally)
    free (visited);

  return (pred == NULL);
}

/* Actually perform code hoisting.  */

static void
hoist_code (void)
{
  basic_block bb, dominated;
  basic_block *domby;
  unsigned int domby_len;
  unsigned int i,j;
  struct expr **index_map;
  struct expr *expr;

  sbitmap_vector_zero (hoist_exprs, last_basic_block);

  /* Compute a mapping from expression number (`bitmap_index') to
     hash table entry.  */

  index_map = XCNEWVEC (struct expr *, expr_hash_table.n_elems);
  for (i = 0; i < expr_hash_table.size; i++)
    for (expr = expr_hash_table.table[i]; expr != NULL; expr = expr->next_same_hash)
      index_map[expr->bitmap_index] = expr;

  /* Walk over each basic block looking for potentially hoistable
     expressions, nothing gets hoisted from the entry block.  */
  FOR_EACH_BB (bb)
    {
      int found = 0;
      int insn_inserted_p;

      domby_len = get_dominated_by (CDI_DOMINATORS, bb, &domby);
      /* Examine each expression that is very busy at the exit of this
	 block.  These are the potentially hoistable expressions.  */
      for (i = 0; i < hoist_vbeout[bb->index]->n_bits; i++)
	{
	  int hoistable = 0;

	  if (TEST_BIT (hoist_vbeout[bb->index], i)
	      && TEST_BIT (transpout[bb->index], i))
	    {
	      /* We've found a potentially hoistable expression, now
		 we look at every block BB dominates to see if it
		 computes the expression.  */
	      for (j = 0; j < domby_len; j++)
		{
		  dominated = domby[j];
		  /* Ignore self dominance.  */
		  if (bb == dominated)
		    continue;
		  /* We've found a dominated block, now see if it computes
		     the busy expression and whether or not moving that
		     expression to the "beginning" of that block is safe.  */
		  if (!TEST_BIT (antloc[dominated->index], i))
		    continue;

		  /* Note if the expression would reach the dominated block
		     unimpared if it was placed at the end of BB.

		     Keep track of how many times this expression is hoistable
		     from a dominated block into BB.  */
		  if (hoist_expr_reaches_here_p (bb, i, dominated, NULL))
		    hoistable++;
		}

	      /* If we found more than one hoistable occurrence of this
		 expression, then note it in the bitmap of expressions to
		 hoist.  It makes no sense to hoist things which are computed
		 in only one BB, and doing so tends to pessimize register
		 allocation.  One could increase this value to try harder
		 to avoid any possible code expansion due to register
		 allocation issues; however experiments have shown that
		 the vast majority of hoistable expressions are only movable
		 from two successors, so raising this threshold is likely
		 to nullify any benefit we get from code hoisting.  */
	      if (hoistable > 1)
		{
		  SET_BIT (hoist_exprs[bb->index], i);
		  found = 1;
		}
	    }
	}
      /* If we found nothing to hoist, then quit now.  */
      if (! found)
        {
	  free (domby);
	continue;
	}

      /* Loop over all the hoistable expressions.  */
      for (i = 0; i < hoist_exprs[bb->index]->n_bits; i++)
	{
	  /* We want to insert the expression into BB only once, so
	     note when we've inserted it.  */
	  insn_inserted_p = 0;

	  /* These tests should be the same as the tests above.  */
	  if (TEST_BIT (hoist_exprs[bb->index], i))
	    {
	      /* We've found a potentially hoistable expression, now
		 we look at every block BB dominates to see if it
		 computes the expression.  */
	      for (j = 0; j < domby_len; j++)
		{
		  dominated = domby[j];
		  /* Ignore self dominance.  */
		  if (bb == dominated)
		    continue;

		  /* We've found a dominated block, now see if it computes
		     the busy expression and whether or not moving that
		     expression to the "beginning" of that block is safe.  */
		  if (!TEST_BIT (antloc[dominated->index], i))
		    continue;

		  /* The expression is computed in the dominated block and
		     it would be safe to compute it at the start of the
		     dominated block.  Now we have to determine if the
		     expression would reach the dominated block if it was
		     placed at the end of BB.  */
		  if (hoist_expr_reaches_here_p (bb, i, dominated, NULL))
		    {
		      struct expr *expr = index_map[i];
		      struct occr *occr = expr->antic_occr;
		      rtx insn;
		      rtx set;

		      /* Find the right occurrence of this expression.  */
		      while (BLOCK_FOR_INSN (occr->insn) != dominated && occr)
			occr = occr->next;

		      gcc_assert (occr);
		      insn = occr->insn;
		      set = single_set (insn);
		      gcc_assert (set);

		      /* Create a pseudo-reg to store the result of reaching
			 expressions into.  Get the mode for the new pseudo
			 from the mode of the original destination pseudo.  */
		      if (expr->reaching_reg == NULL)
			expr->reaching_reg
			  = gen_reg_rtx (GET_MODE (SET_DEST (set)));

		      gcse_emit_move_after (expr->reaching_reg, SET_DEST (set), insn);
		      delete_insn (insn);
		      occr->deleted_p = 1;
		      if (!insn_inserted_p)
			{
			  insert_insn_end_bb (index_map[i], bb, 0);
			  insn_inserted_p = 1;
			}
		    }
		}
	    }
	}
      free (domby);
    }

  free (index_map);
}

/* Top level routine to perform one code hoisting (aka unification) pass

   Return nonzero if a change was made.  */

static int
one_code_hoisting_pass (void)
{
  int changed = 0;

  alloc_hash_table (max_cuid, &expr_hash_table, 0);
  compute_hash_table (&expr_hash_table);
  if (dump_file)
    dump_hash_table (dump_file, "Code Hosting Expressions", &expr_hash_table);

  if (expr_hash_table.n_elems > 0)
    {
      alloc_code_hoist_mem (last_basic_block, expr_hash_table.n_elems);
      compute_code_hoist_data ();
      hoist_code ();
      free_code_hoist_mem ();
    }

  free_hash_table (&expr_hash_table);

  return changed;
}

/*  Here we provide the things required to do store motion towards
    the exit. In order for this to be effective, gcse also needed to
    be taught how to move a load when it is kill only by a store to itself.

	    int i;
	    float a[10];

	    void foo(float scale)
	    {
	      for (i=0; i<10; i++)
		a[i] *= scale;
	    }

    'i' is both loaded and stored to in the loop. Normally, gcse cannot move
    the load out since its live around the loop, and stored at the bottom
    of the loop.

      The 'Load Motion' referred to and implemented in this file is
    an enhancement to gcse which when using edge based lcm, recognizes
    this situation and allows gcse to move the load out of the loop.

      Once gcse has hoisted the load, store motion can then push this
    load towards the exit, and we end up with no loads or stores of 'i'
    in the loop.  */

static hashval_t
pre_ldst_expr_hash (const void *p)
{
  int do_not_record_p = 0;
  const struct ls_expr *x = p;
  return hash_rtx (x->pattern, GET_MODE (x->pattern), &do_not_record_p, NULL, false);
}

static int
pre_ldst_expr_eq (const void *p1, const void *p2)
{
  const struct ls_expr *ptr1 = p1, *ptr2 = p2;
  return expr_equiv_p (ptr1->pattern, ptr2->pattern);
}

/* This will search the ldst list for a matching expression. If it
   doesn't find one, we create one and initialize it.  */

static struct ls_expr *
ldst_entry (rtx x)
{
  int do_not_record_p = 0;
  struct ls_expr * ptr;
  unsigned int hash;
  void **slot;
  struct ls_expr e;

  hash = hash_rtx (x, GET_MODE (x), &do_not_record_p,
		   NULL,  /*have_reg_qty=*/false);

  e.pattern = x;
  slot = htab_find_slot_with_hash (pre_ldst_table, &e, hash, INSERT);
  if (*slot)
    return (struct ls_expr *)*slot;

  ptr = XNEW (struct ls_expr);

  ptr->next         = pre_ldst_mems;
  ptr->expr         = NULL;
  ptr->pattern      = x;
  ptr->pattern_regs = NULL_RTX;
  ptr->loads        = NULL_RTX;
  ptr->stores       = NULL_RTX;
  ptr->reaching_reg = NULL_RTX;
  ptr->invalid      = 0;
  ptr->index        = 0;
  ptr->hash_index   = hash;
  pre_ldst_mems     = ptr;
  *slot = ptr;

  return ptr;
}

/* Free up an individual ldst entry.  */

static void
free_ldst_entry (struct ls_expr * ptr)
{
  free_INSN_LIST_list (& ptr->loads);
  free_INSN_LIST_list (& ptr->stores);

  free (ptr);
}

/* Free up all memory associated with the ldst list.  */

static void
free_ldst_mems (void)
{
  if (pre_ldst_table)
    htab_delete (pre_ldst_table);
  pre_ldst_table = NULL;

  while (pre_ldst_mems)
    {
      struct ls_expr * tmp = pre_ldst_mems;

      pre_ldst_mems = pre_ldst_mems->next;

      free_ldst_entry (tmp);
    }

  pre_ldst_mems = NULL;
}

/* Dump debugging info about the ldst list.  */

static void
print_ldst_list (FILE * file)
{
  struct ls_expr * ptr;

  fprintf (file, "LDST list: \n");

  for (ptr = first_ls_expr(); ptr != NULL; ptr = next_ls_expr (ptr))
    {
      fprintf (file, "  Pattern (%3d): ", ptr->index);

      print_rtl (file, ptr->pattern);

      fprintf (file, "\n	 Loads : ");

      if (ptr->loads)
	print_rtl (file, ptr->loads);
      else
	fprintf (file, "(nil)");

      fprintf (file, "\n	Stores : ");

      if (ptr->stores)
	print_rtl (file, ptr->stores);
      else
	fprintf (file, "(nil)");

      fprintf (file, "\n\n");
    }

  fprintf (file, "\n");
}

/* Returns 1 if X is in the list of ldst only expressions.  */

static struct ls_expr *
find_rtx_in_ldst (rtx x)
{
  struct ls_expr e;
  void **slot;
  if (!pre_ldst_table)
    return NULL;
  e.pattern = x;
  slot = htab_find_slot (pre_ldst_table, &e, NO_INSERT);
  if (!slot || ((struct ls_expr *)*slot)->invalid)
    return NULL;
  return *slot;
}

/* Assign each element of the list of mems a monotonically increasing value.  */

static int
enumerate_ldsts (void)
{
  struct ls_expr * ptr;
  int n = 0;

  for (ptr = pre_ldst_mems; ptr != NULL; ptr = ptr->next)
    ptr->index = n++;

  return n;
}

/* Return first item in the list.  */

static inline struct ls_expr *
first_ls_expr (void)
{
  return pre_ldst_mems;
}

/* Return the next item in the list after the specified one.  */

static inline struct ls_expr *
next_ls_expr (struct ls_expr * ptr)
{
  return ptr->next;
}

/* Load Motion for loads which only kill themselves.  */

/* Return true if x is a simple MEM operation, with no registers or
   side effects. These are the types of loads we consider for the
   ld_motion list, otherwise we let the usual aliasing take care of it.  */

static int
simple_mem (rtx x)
{
  if (! MEM_P (x))
    return 0;

  if (MEM_VOLATILE_P (x))
    return 0;

  if (GET_MODE (x) == BLKmode)
    return 0;

  /* If we are handling exceptions, we must be careful with memory references
     that may trap. If we are not, the behavior is undefined, so we may just
     continue.  */
  if (flag_non_call_exceptions && may_trap_p (x))
    return 0;

  if (side_effects_p (x))
    return 0;

  /* Do not consider function arguments passed on stack.  */
  if (reg_mentioned_p (stack_pointer_rtx, x))
    return 0;

  if (flag_float_store && FLOAT_MODE_P (GET_MODE (x)))
    return 0;

  return 1;
}

/* Make sure there isn't a buried reference in this pattern anywhere.
   If there is, invalidate the entry for it since we're not capable
   of fixing it up just yet.. We have to be sure we know about ALL
   loads since the aliasing code will allow all entries in the
   ld_motion list to not-alias itself.  If we miss a load, we will get
   the wrong value since gcse might common it and we won't know to
   fix it up.  */

static void
invalidate_any_buried_refs (rtx x)
{
  const char * fmt;
  int i, j;
  struct ls_expr * ptr;

  /* Invalidate it in the list.  */
  if (MEM_P (x) && simple_mem (x))
    {
      ptr = ldst_entry (x);
      ptr->invalid = 1;
    }

  /* Recursively process the insn.  */
  fmt = GET_RTX_FORMAT (GET_CODE (x));

  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	invalidate_any_buried_refs (XEXP (x, i));
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  invalidate_any_buried_refs (XVECEXP (x, i, j));
    }
}

/* Find all the 'simple' MEMs which are used in LOADs and STORES.  Simple
   being defined as MEM loads and stores to symbols, with no side effects
   and no registers in the expression.  For a MEM destination, we also
   check that the insn is still valid if we replace the destination with a
   REG, as is done in update_ld_motion_stores.  If there are any uses/defs
   which don't match this criteria, they are invalidated and trimmed out
   later.  */

static void
compute_ld_motion_mems (void)
{
  struct ls_expr * ptr;
  basic_block bb;
  rtx insn;

  pre_ldst_mems = NULL;
  pre_ldst_table = htab_create (13, pre_ldst_expr_hash,
				pre_ldst_expr_eq, NULL);

  FOR_EACH_BB (bb)
    {
      FOR_BB_INSNS (bb, insn)
	{
	  if (INSN_P (insn))
	    {
	      if (GET_CODE (PATTERN (insn)) == SET)
		{
		  rtx src = SET_SRC (PATTERN (insn));
		  rtx dest = SET_DEST (PATTERN (insn));

		  /* Check for a simple LOAD...  */
		  if (MEM_P (src) && simple_mem (src))
		    {
		      ptr = ldst_entry (src);
		      if (REG_P (dest))
			ptr->loads = alloc_INSN_LIST (insn, ptr->loads);
		      else
			ptr->invalid = 1;
		    }
		  else
		    {
		      /* Make sure there isn't a buried load somewhere.  */
		      invalidate_any_buried_refs (src);
		    }

		  /* Check for stores. Don't worry about aliased ones, they
		     will block any movement we might do later. We only care
		     about this exact pattern since those are the only
		     circumstance that we will ignore the aliasing info.  */
		  if (MEM_P (dest) && simple_mem (dest))
		    {
		      ptr = ldst_entry (dest);

		      if (! MEM_P (src)
			  && GET_CODE (src) != ASM_OPERANDS
			  /* Check for REG manually since want_to_gcse_p
			     returns 0 for all REGs.  */
			  && can_assign_to_reg_p (src))
			ptr->stores = alloc_INSN_LIST (insn, ptr->stores);
		      else
			ptr->invalid = 1;
		    }
		}
	      else
		invalidate_any_buried_refs (PATTERN (insn));
	    }
	}
    }
}

/* Remove any references that have been either invalidated or are not in the
   expression list for pre gcse.  */

static void
trim_ld_motion_mems (void)
{
  struct ls_expr * * last = & pre_ldst_mems;
  struct ls_expr * ptr = pre_ldst_mems;

  while (ptr != NULL)
    {
      struct expr * expr;

      /* Delete if entry has been made invalid.  */
      if (! ptr->invalid)
	{
	  /* Delete if we cannot find this mem in the expression list.  */
	  unsigned int hash = ptr->hash_index % expr_hash_table.size;

	  for (expr = expr_hash_table.table[hash];
	       expr != NULL;
	       expr = expr->next_same_hash)
	    if (expr_equiv_p (expr->expr, ptr->pattern))
	      break;
	}
      else
	expr = (struct expr *) 0;

      if (expr)
	{
	  /* Set the expression field if we are keeping it.  */
	  ptr->expr = expr;
	  last = & ptr->next;
	  ptr = ptr->next;
	}
      else
	{
	  *last = ptr->next;
	  htab_remove_elt_with_hash (pre_ldst_table, ptr, ptr->hash_index);
	  free_ldst_entry (ptr);
	  ptr = * last;
	}
    }

  /* Show the world what we've found.  */
  if (dump_file && pre_ldst_mems != NULL)
    print_ldst_list (dump_file);
}

/* This routine will take an expression which we are replacing with
   a reaching register, and update any stores that are needed if
   that expression is in the ld_motion list.  Stores are updated by
   copying their SRC to the reaching register, and then storing
   the reaching register into the store location. These keeps the
   correct value in the reaching register for the loads.  */

static void
update_ld_motion_stores (struct expr * expr)
{
  struct ls_expr * mem_ptr;

  if ((mem_ptr = find_rtx_in_ldst (expr->expr)))
    {
      /* We can try to find just the REACHED stores, but is shouldn't
	 matter to set the reaching reg everywhere...  some might be
	 dead and should be eliminated later.  */

      /* We replace (set mem expr) with (set reg expr) (set mem reg)
	 where reg is the reaching reg used in the load.  We checked in
	 compute_ld_motion_mems that we can replace (set mem expr) with
	 (set reg expr) in that insn.  */
      rtx list = mem_ptr->stores;

      for ( ; list != NULL_RTX; list = XEXP (list, 1))
	{
	  rtx insn = XEXP (list, 0);
	  rtx pat = PATTERN (insn);
	  rtx src = SET_SRC (pat);
	  rtx reg = expr->reaching_reg;
	  rtx copy, new;

	  /* If we've already copied it, continue.  */
	  if (expr->reaching_reg == src)
	    continue;

	  if (dump_file)
	    {
	      fprintf (dump_file, "PRE:  store updated with reaching reg ");
	      print_rtl (dump_file, expr->reaching_reg);
	      fprintf (dump_file, ":\n	");
	      print_inline_rtx (dump_file, insn, 8);
	      fprintf (dump_file, "\n");
	    }

	  copy = gen_move_insn ( reg, copy_rtx (SET_SRC (pat)));
	  new = emit_insn_before (copy, insn);
	  record_one_set (REGNO (reg), new);
	  SET_SRC (pat) = reg;

	  /* un-recognize this pattern since it's probably different now.  */
	  INSN_CODE (insn) = -1;
	  gcse_create_count++;
	}
    }
}

/* Store motion code.  */

#define ANTIC_STORE_LIST(x)		((x)->loads)
#define AVAIL_STORE_LIST(x)		((x)->stores)
#define LAST_AVAIL_CHECK_FAILURE(x)	((x)->reaching_reg)

/* This is used to communicate the target bitvector we want to use in the
   reg_set_info routine when called via the note_stores mechanism.  */
static int * regvec;

/* And current insn, for the same routine.  */
static rtx compute_store_table_current_insn;

/* Used in computing the reverse edge graph bit vectors.  */
static sbitmap * st_antloc;

/* Global holding the number of store expressions we are dealing with.  */
static int num_stores;

/* Checks to set if we need to mark a register set.  Called from
   note_stores.  */

static void
reg_set_info (rtx dest, rtx setter ATTRIBUTE_UNUSED,
	      void *data)
{
  sbitmap bb_reg = data;

  if (GET_CODE (dest) == SUBREG)
    dest = SUBREG_REG (dest);

  if (REG_P (dest))
    {
      regvec[REGNO (dest)] = INSN_UID (compute_store_table_current_insn);
      if (bb_reg)
	SET_BIT (bb_reg, REGNO (dest));
    }
}

/* Clear any mark that says that this insn sets dest.  Called from
   note_stores.  */

static void
reg_clear_last_set (rtx dest, rtx setter ATTRIBUTE_UNUSED,
	      void *data)
{
  int *dead_vec = data;

  if (GET_CODE (dest) == SUBREG)
    dest = SUBREG_REG (dest);

  if (REG_P (dest) &&
      dead_vec[REGNO (dest)] == INSN_UID (compute_store_table_current_insn))
    dead_vec[REGNO (dest)] = 0;
}

/* Return zero if some of the registers in list X are killed
   due to set of registers in bitmap REGS_SET.  */

static bool
store_ops_ok (rtx x, int *regs_set)
{
  rtx reg;

  for (; x; x = XEXP (x, 1))
    {
      reg = XEXP (x, 0);
      if (regs_set[REGNO(reg)])
	return false;
    }

  return true;
}

/* Returns a list of registers mentioned in X.  */
static rtx
extract_mentioned_regs (rtx x)
{
  return extract_mentioned_regs_helper (x, NULL_RTX);
}

/* Helper for extract_mentioned_regs; ACCUM is used to accumulate used
   registers.  */
static rtx
extract_mentioned_regs_helper (rtx x, rtx accum)
{
  int i;
  enum rtx_code code;
  const char * fmt;

  /* Repeat is used to turn tail-recursion into iteration.  */
 repeat:

  if (x == 0)
    return accum;

  code = GET_CODE (x);
  switch (code)
    {
    case REG:
      return alloc_EXPR_LIST (0, x, accum);

    case MEM:
      x = XEXP (x, 0);
      goto repeat;

    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
      /* We do not run this function with arguments having side effects.  */
      gcc_unreachable ();

    case PC:
    case CC0: /*FIXME*/
    case CONST:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case LABEL_REF:
    case ADDR_VEC:
    case ADDR_DIFF_VEC:
      return accum;

    default:
      break;
    }

  i = GET_RTX_LENGTH (code) - 1;
  fmt = GET_RTX_FORMAT (code);

  for (; i >= 0; i--)
    {
      if (fmt[i] == 'e')
	{
	  rtx tem = XEXP (x, i);

	  /* If we are about to do the last recursive call
	     needed at this level, change it into iteration.  */
	  if (i == 0)
	    {
	      x = tem;
	      goto repeat;
	    }

	  accum = extract_mentioned_regs_helper (tem, accum);
	}
      else if (fmt[i] == 'E')
	{
	  int j;

	  for (j = 0; j < XVECLEN (x, i); j++)
	    accum = extract_mentioned_regs_helper (XVECEXP (x, i, j), accum);
	}
    }

  return accum;
}

/* Determine whether INSN is MEM store pattern that we will consider moving.
   REGS_SET_BEFORE is bitmap of registers set before (and including) the
   current insn, REGS_SET_AFTER is bitmap of registers set after (and
   including) the insn in this basic block.  We must be passing through BB from
   head to end, as we are using this fact to speed things up.

   The results are stored this way:

   -- the first anticipatable expression is added into ANTIC_STORE_LIST
   -- if the processed expression is not anticipatable, NULL_RTX is added
      there instead, so that we can use it as indicator that no further
      expression of this type may be anticipatable
   -- if the expression is available, it is added as head of AVAIL_STORE_LIST;
      consequently, all of them but this head are dead and may be deleted.
   -- if the expression is not available, the insn due to that it fails to be
      available is stored in reaching_reg.

   The things are complicated a bit by fact that there already may be stores
   to the same MEM from other blocks; also caller must take care of the
   necessary cleanup of the temporary markers after end of the basic block.
   */

static void
find_moveable_store (rtx insn, int *regs_set_before, int *regs_set_after)
{
  struct ls_expr * ptr;
  rtx dest, set, tmp;
  int check_anticipatable, check_available;
  basic_block bb = BLOCK_FOR_INSN (insn);

  set = single_set (insn);
  if (!set)
    return;

  dest = SET_DEST (set);

  if (! MEM_P (dest) || MEM_VOLATILE_P (dest)
      || GET_MODE (dest) == BLKmode)
    return;

  if (side_effects_p (dest))
    return;

  /* If we are handling exceptions, we must be careful with memory references
     that may trap. If we are not, the behavior is undefined, so we may just
     continue.  */
  if (flag_non_call_exceptions && may_trap_p (dest))
    return;

  /* Even if the destination cannot trap, the source may.  In this case we'd
     need to handle updating the REG_EH_REGION note.  */
  if (find_reg_note (insn, REG_EH_REGION, NULL_RTX))
    return;

  /* Make sure that the SET_SRC of this store insns can be assigned to
     a register, or we will fail later on in replace_store_insn, which
     assumes that we can do this.  But sometimes the target machine has
     oddities like MEM read-modify-write instruction.  See for example
     PR24257.  */
  if (!can_assign_to_reg_p (SET_SRC (set)))
    return;

  ptr = ldst_entry (dest);
  if (!ptr->pattern_regs)
    ptr->pattern_regs = extract_mentioned_regs (dest);

  /* Do not check for anticipatability if we either found one anticipatable
     store already, or tested for one and found out that it was killed.  */
  check_anticipatable = 0;
  if (!ANTIC_STORE_LIST (ptr))
    check_anticipatable = 1;
  else
    {
      tmp = XEXP (ANTIC_STORE_LIST (ptr), 0);
      if (tmp != NULL_RTX
	  && BLOCK_FOR_INSN (tmp) != bb)
	check_anticipatable = 1;
    }
  if (check_anticipatable)
    {
      if (store_killed_before (dest, ptr->pattern_regs, insn, bb, regs_set_before))
	tmp = NULL_RTX;
      else
	tmp = insn;
      ANTIC_STORE_LIST (ptr) = alloc_INSN_LIST (tmp,
						ANTIC_STORE_LIST (ptr));
    }

  /* It is not necessary to check whether store is available if we did
     it successfully before; if we failed before, do not bother to check
     until we reach the insn that caused us to fail.  */
  check_available = 0;
  if (!AVAIL_STORE_LIST (ptr))
    check_available = 1;
  else
    {
      tmp = XEXP (AVAIL_STORE_LIST (ptr), 0);
      if (BLOCK_FOR_INSN (tmp) != bb)
	check_available = 1;
    }
  if (check_available)
    {
      /* Check that we have already reached the insn at that the check
	 failed last time.  */
      if (LAST_AVAIL_CHECK_FAILURE (ptr))
	{
	  for (tmp = BB_END (bb);
	       tmp != insn && tmp != LAST_AVAIL_CHECK_FAILURE (ptr);
	       tmp = PREV_INSN (tmp))
	    continue;
	  if (tmp == insn)
	    check_available = 0;
	}
      else
	check_available = store_killed_after (dest, ptr->pattern_regs, insn,
					      bb, regs_set_after,
					      &LAST_AVAIL_CHECK_FAILURE (ptr));
    }
  if (!check_available)
    AVAIL_STORE_LIST (ptr) = alloc_INSN_LIST (insn, AVAIL_STORE_LIST (ptr));
}

/* Find available and anticipatable stores.  */

static int
compute_store_table (void)
{
  int ret;
  basic_block bb;
  unsigned regno;
  rtx insn, pat, tmp;
  int *last_set_in, *already_set;
  struct ls_expr * ptr, **prev_next_ptr_ptr;

  max_gcse_regno = max_reg_num ();

  reg_set_in_block = sbitmap_vector_alloc (last_basic_block,
						       max_gcse_regno);
  sbitmap_vector_zero (reg_set_in_block, last_basic_block);
  pre_ldst_mems = 0;
  pre_ldst_table = htab_create (13, pre_ldst_expr_hash,
				pre_ldst_expr_eq, NULL);
  last_set_in = XCNEWVEC (int, max_gcse_regno);
  already_set = XNEWVEC (int, max_gcse_regno);

  /* Find all the stores we care about.  */
  FOR_EACH_BB (bb)
    {
      /* First compute the registers set in this block.  */
      regvec = last_set_in;

      FOR_BB_INSNS (bb, insn)
	{
	  if (! INSN_P (insn))
	    continue;

	  if (CALL_P (insn))
	    {
	      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
		if (TEST_HARD_REG_BIT (regs_invalidated_by_call, regno))
		  {
		    last_set_in[regno] = INSN_UID (insn);
		    SET_BIT (reg_set_in_block[bb->index], regno);
		  }
	    }

	  pat = PATTERN (insn);
	  compute_store_table_current_insn = insn;
	  note_stores (pat, reg_set_info, reg_set_in_block[bb->index]);
	}

      /* Now find the stores.  */
      memset (already_set, 0, sizeof (int) * max_gcse_regno);
      regvec = already_set;
      FOR_BB_INSNS (bb, insn)
	{
	  if (! INSN_P (insn))
	    continue;

	  if (CALL_P (insn))
	    {
	      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
		if (TEST_HARD_REG_BIT (regs_invalidated_by_call, regno))
		  already_set[regno] = 1;
	    }

	  pat = PATTERN (insn);
	  note_stores (pat, reg_set_info, NULL);

	  /* Now that we've marked regs, look for stores.  */
	  find_moveable_store (insn, already_set, last_set_in);

	  /* Unmark regs that are no longer set.  */
	  compute_store_table_current_insn = insn;
	  note_stores (pat, reg_clear_last_set, last_set_in);
	  if (CALL_P (insn))
	    {
	      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
		if (TEST_HARD_REG_BIT (regs_invalidated_by_call, regno)
		    && last_set_in[regno] == INSN_UID (insn))
		  last_set_in[regno] = 0;
	    }
	}

#ifdef ENABLE_CHECKING
      /* last_set_in should now be all-zero.  */
      for (regno = 0; regno < max_gcse_regno; regno++)
	gcc_assert (!last_set_in[regno]);
#endif

      /* Clear temporary marks.  */
      for (ptr = first_ls_expr (); ptr != NULL; ptr = next_ls_expr (ptr))
	{
	  LAST_AVAIL_CHECK_FAILURE(ptr) = NULL_RTX;
	  if (ANTIC_STORE_LIST (ptr)
	      && (tmp = XEXP (ANTIC_STORE_LIST (ptr), 0)) == NULL_RTX)
	    ANTIC_STORE_LIST (ptr) = XEXP (ANTIC_STORE_LIST (ptr), 1);
	}
    }

  /* Remove the stores that are not available anywhere, as there will
     be no opportunity to optimize them.  */
  for (ptr = pre_ldst_mems, prev_next_ptr_ptr = &pre_ldst_mems;
       ptr != NULL;
       ptr = *prev_next_ptr_ptr)
    {
      if (!AVAIL_STORE_LIST (ptr))
	{
	  *prev_next_ptr_ptr = ptr->next;
	  htab_remove_elt_with_hash (pre_ldst_table, ptr, ptr->hash_index);
	  free_ldst_entry (ptr);
	}
      else
	prev_next_ptr_ptr = &ptr->next;
    }

  ret = enumerate_ldsts ();

  if (dump_file)
    {
      fprintf (dump_file, "ST_avail and ST_antic (shown under loads..)\n");
      print_ldst_list (dump_file);
    }

  free (last_set_in);
  free (already_set);
  return ret;
}

/* Check to see if the load X is aliased with STORE_PATTERN.
   AFTER is true if we are checking the case when STORE_PATTERN occurs
   after the X.  */

static bool
load_kills_store (rtx x, rtx store_pattern, int after)
{
  if (after)
    return anti_dependence (x, store_pattern);
  else
    return true_dependence (store_pattern, GET_MODE (store_pattern), x,
			    rtx_addr_varies_p);
}

/* Go through the entire insn X, looking for any loads which might alias
   STORE_PATTERN.  Return true if found.
   AFTER is true if we are checking the case when STORE_PATTERN occurs
   after the insn X.  */

static bool
find_loads (rtx x, rtx store_pattern, int after)
{
  const char * fmt;
  int i, j;
  int ret = false;

  if (!x)
    return false;

  if (GET_CODE (x) == SET)
    x = SET_SRC (x);

  if (MEM_P (x))
    {
      if (load_kills_store (x, store_pattern, after))
	return true;
    }

  /* Recursively process the insn.  */
  fmt = GET_RTX_FORMAT (GET_CODE (x));

  for (i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0 && !ret; i--)
    {
      if (fmt[i] == 'e')
	ret |= find_loads (XEXP (x, i), store_pattern, after);
      else if (fmt[i] == 'E')
	for (j = XVECLEN (x, i) - 1; j >= 0; j--)
	  ret |= find_loads (XVECEXP (x, i, j), store_pattern, after);
    }
  return ret;
}

/* Check if INSN kills the store pattern X (is aliased with it).
   AFTER is true if we are checking the case when store X occurs
   after the insn.  Return true if it does.  */

static bool
store_killed_in_insn (rtx x, rtx x_regs, rtx insn, int after)
{
  rtx reg, base, note;

  if (!INSN_P (insn))
    return false;

  if (CALL_P (insn))
    {
      /* A normal or pure call might read from pattern,
	 but a const call will not.  */
      if (! CONST_OR_PURE_CALL_P (insn) || pure_call_p (insn))
	return true;

      /* But even a const call reads its parameters.  Check whether the
	 base of some of registers used in mem is stack pointer.  */
      for (reg = x_regs; reg; reg = XEXP (reg, 1))
	{
	  base = find_base_term (XEXP (reg, 0));
	  if (!base
	      || (GET_CODE (base) == ADDRESS
		  && GET_MODE (base) == Pmode
		  && XEXP (base, 0) == stack_pointer_rtx))
	    return true;
	}

      return false;
    }

  if (GET_CODE (PATTERN (insn)) == SET)
    {
      rtx pat = PATTERN (insn);
      rtx dest = SET_DEST (pat);

      if (GET_CODE (dest) == ZERO_EXTRACT)
	dest = XEXP (dest, 0);

      /* Check for memory stores to aliased objects.  */
      if (MEM_P (dest)
	  && !expr_equiv_p (dest, x))
	{
	  if (after)
	    {
	      if (output_dependence (dest, x))
		return true;
	    }
	  else
	    {
	      if (output_dependence (x, dest))
		return true;
	    }
	}
      if (find_loads (SET_SRC (pat), x, after))
	return true;
    }
  else if (find_loads (PATTERN (insn), x, after))
    return true;

  /* If this insn has a REG_EQUAL or REG_EQUIV note referencing a memory
     location aliased with X, then this insn kills X.  */
  note = find_reg_equal_equiv_note (insn);
  if (! note)
    return false;
  note = XEXP (note, 0);

  /* However, if the note represents a must alias rather than a may
     alias relationship, then it does not kill X.  */
  if (expr_equiv_p (note, x))
    return false;

  /* See if there are any aliased loads in the note.  */
  return find_loads (note, x, after);
}

/* Returns true if the expression X is loaded or clobbered on or after INSN
   within basic block BB.  REGS_SET_AFTER is bitmap of registers set in
   or after the insn.  X_REGS is list of registers mentioned in X. If the store
   is killed, return the last insn in that it occurs in FAIL_INSN.  */

static bool
store_killed_after (rtx x, rtx x_regs, rtx insn, basic_block bb,
		    int *regs_set_after, rtx *fail_insn)
{
  rtx last = BB_END (bb), act;

  if (!store_ops_ok (x_regs, regs_set_after))
    {
      /* We do not know where it will happen.  */
      if (fail_insn)
	*fail_insn = NULL_RTX;
      return true;
    }

  /* Scan from the end, so that fail_insn is determined correctly.  */
  for (act = last; act != PREV_INSN (insn); act = PREV_INSN (act))
    if (store_killed_in_insn (x, x_regs, act, false))
      {
	if (fail_insn)
	  *fail_insn = act;
	return true;
      }

  return false;
}

/* Returns true if the expression X is loaded or clobbered on or before INSN
   within basic block BB. X_REGS is list of registers mentioned in X.
   REGS_SET_BEFORE is bitmap of registers set before or in this insn.  */
static bool
store_killed_before (rtx x, rtx x_regs, rtx insn, basic_block bb,
		     int *regs_set_before)
{
  rtx first = BB_HEAD (bb);

  if (!store_ops_ok (x_regs, regs_set_before))
    return true;

  for ( ; insn != PREV_INSN (first); insn = PREV_INSN (insn))
    if (store_killed_in_insn (x, x_regs, insn, true))
      return true;

  return false;
}

/* Fill in available, anticipatable, transparent and kill vectors in
   STORE_DATA, based on lists of available and anticipatable stores.  */
static void
build_store_vectors (void)
{
  basic_block bb;
  int *regs_set_in_block;
  rtx insn, st;
  struct ls_expr * ptr;
  unsigned regno;

  /* Build the gen_vector. This is any store in the table which is not killed
     by aliasing later in its block.  */
  ae_gen = sbitmap_vector_alloc (last_basic_block, num_stores);
  sbitmap_vector_zero (ae_gen, last_basic_block);

  st_antloc = sbitmap_vector_alloc (last_basic_block, num_stores);
  sbitmap_vector_zero (st_antloc, last_basic_block);

  for (ptr = first_ls_expr (); ptr != NULL; ptr = next_ls_expr (ptr))
    {
      for (st = AVAIL_STORE_LIST (ptr); st != NULL; st = XEXP (st, 1))
	{
	  insn = XEXP (st, 0);
	  bb = BLOCK_FOR_INSN (insn);

	  /* If we've already seen an available expression in this block,
	     we can delete this one (It occurs earlier in the block). We'll
	     copy the SRC expression to an unused register in case there
	     are any side effects.  */
	  if (TEST_BIT (ae_gen[bb->index], ptr->index))
	    {
	      rtx r = gen_reg_rtx (GET_MODE (ptr->pattern));
	      if (dump_file)
		fprintf (dump_file, "Removing redundant store:\n");
	      replace_store_insn (r, XEXP (st, 0), bb, ptr);
	      continue;
	    }
	  SET_BIT (ae_gen[bb->index], ptr->index);
	}

      for (st = ANTIC_STORE_LIST (ptr); st != NULL; st = XEXP (st, 1))
	{
	  insn = XEXP (st, 0);
	  bb = BLOCK_FOR_INSN (insn);
	  SET_BIT (st_antloc[bb->index], ptr->index);
	}
    }

  ae_kill = sbitmap_vector_alloc (last_basic_block, num_stores);
  sbitmap_vector_zero (ae_kill, last_basic_block);

  transp = sbitmap_vector_alloc (last_basic_block, num_stores);
  sbitmap_vector_zero (transp, last_basic_block);
  regs_set_in_block = XNEWVEC (int, max_gcse_regno);

  FOR_EACH_BB (bb)
    {
      for (regno = 0; regno < max_gcse_regno; regno++)
	regs_set_in_block[regno] = TEST_BIT (reg_set_in_block[bb->index], regno);

      for (ptr = first_ls_expr (); ptr != NULL; ptr = next_ls_expr (ptr))
	{
	  if (store_killed_after (ptr->pattern, ptr->pattern_regs, BB_HEAD (bb),
				  bb, regs_set_in_block, NULL))
	    {
	      /* It should not be necessary to consider the expression
		 killed if it is both anticipatable and available.  */
	      if (!TEST_BIT (st_antloc[bb->index], ptr->index)
		  || !TEST_BIT (ae_gen[bb->index], ptr->index))
		SET_BIT (ae_kill[bb->index], ptr->index);
	    }
	  else
	    SET_BIT (transp[bb->index], ptr->index);
	}
    }

  free (regs_set_in_block);

  if (dump_file)
    {
      dump_sbitmap_vector (dump_file, "st_antloc", "", st_antloc, last_basic_block);
      dump_sbitmap_vector (dump_file, "st_kill", "", ae_kill, last_basic_block);
      dump_sbitmap_vector (dump_file, "Transpt", "", transp, last_basic_block);
      dump_sbitmap_vector (dump_file, "st_avloc", "", ae_gen, last_basic_block);
    }
}

/* Insert an instruction at the beginning of a basic block, and update
   the BB_HEAD if needed.  */

static void
insert_insn_start_bb (rtx insn, basic_block bb)
{
  /* Insert at start of successor block.  */
  rtx prev = PREV_INSN (BB_HEAD (bb));
  rtx before = BB_HEAD (bb);
  while (before != 0)
    {
      if (! LABEL_P (before)
	  && (! NOTE_P (before)
	      || NOTE_LINE_NUMBER (before) != NOTE_INSN_BASIC_BLOCK))
	break;
      prev = before;
      if (prev == BB_END (bb))
	break;
      before = NEXT_INSN (before);
    }

  insn = emit_insn_after_noloc (insn, prev);

  if (dump_file)
    {
      fprintf (dump_file, "STORE_MOTION  insert store at start of BB %d:\n",
	       bb->index);
      print_inline_rtx (dump_file, insn, 6);
      fprintf (dump_file, "\n");
    }
}

/* This routine will insert a store on an edge. EXPR is the ldst entry for
   the memory reference, and E is the edge to insert it on.  Returns nonzero
   if an edge insertion was performed.  */

static int
insert_store (struct ls_expr * expr, edge e)
{
  rtx reg, insn;
  basic_block bb;
  edge tmp;
  edge_iterator ei;

  /* We did all the deleted before this insert, so if we didn't delete a
     store, then we haven't set the reaching reg yet either.  */
  if (expr->reaching_reg == NULL_RTX)
    return 0;

  if (e->flags & EDGE_FAKE)
    return 0;

  reg = expr->reaching_reg;
  insn = gen_move_insn (copy_rtx (expr->pattern), reg);

  /* If we are inserting this expression on ALL predecessor edges of a BB,
     insert it at the start of the BB, and reset the insert bits on the other
     edges so we don't try to insert it on the other edges.  */
  bb = e->dest;
  FOR_EACH_EDGE (tmp, ei, e->dest->preds)
    if (!(tmp->flags & EDGE_FAKE))
      {
	int index = EDGE_INDEX (edge_list, tmp->src, tmp->dest);
	
	gcc_assert (index != EDGE_INDEX_NO_EDGE);
	if (! TEST_BIT (pre_insert_map[index], expr->index))
	  break;
      }

  /* If tmp is NULL, we found an insertion on every edge, blank the
     insertion vector for these edges, and insert at the start of the BB.  */
  if (!tmp && bb != EXIT_BLOCK_PTR)
    {
      FOR_EACH_EDGE (tmp, ei, e->dest->preds)
	{
	  int index = EDGE_INDEX (edge_list, tmp->src, tmp->dest);
	  RESET_BIT (pre_insert_map[index], expr->index);
	}
      insert_insn_start_bb (insn, bb);
      return 0;
    }

  /* We can't put stores in the front of blocks pointed to by abnormal
     edges since that may put a store where one didn't used to be.  */
  gcc_assert (!(e->flags & EDGE_ABNORMAL));

  insert_insn_on_edge (insn, e);

  if (dump_file)
    {
      fprintf (dump_file, "STORE_MOTION  insert insn on edge (%d, %d):\n",
	       e->src->index, e->dest->index);
      print_inline_rtx (dump_file, insn, 6);
      fprintf (dump_file, "\n");
    }

  return 1;
}

/* Remove any REG_EQUAL or REG_EQUIV notes containing a reference to the
   memory location in SMEXPR set in basic block BB.

   This could be rather expensive.  */

static void
remove_reachable_equiv_notes (basic_block bb, struct ls_expr *smexpr)
{
  edge_iterator *stack, ei;
  int sp;
  edge act;
  sbitmap visited = sbitmap_alloc (last_basic_block);
  rtx last, insn, note;
  rtx mem = smexpr->pattern;

  stack = XNEWVEC (edge_iterator, n_basic_blocks);
  sp = 0;
  ei = ei_start (bb->succs);

  sbitmap_zero (visited);

  act = (EDGE_COUNT (ei_container (ei)) > 0 ? EDGE_I (ei_container (ei), 0) : NULL);
  while (1)
    {
      if (!act)
	{
	  if (!sp)
	    {
	      free (stack);
	      sbitmap_free (visited);
	      return;
	    }
	  act = ei_edge (stack[--sp]);
	}
      bb = act->dest;

      if (bb == EXIT_BLOCK_PTR
	  || TEST_BIT (visited, bb->index))
	{
	  if (!ei_end_p (ei))
	      ei_next (&ei);
	  act = (! ei_end_p (ei)) ? ei_edge (ei) : NULL;
	  continue;
	}
      SET_BIT (visited, bb->index);

      if (TEST_BIT (st_antloc[bb->index], smexpr->index))
	{
	  for (last = ANTIC_STORE_LIST (smexpr);
	       BLOCK_FOR_INSN (XEXP (last, 0)) != bb;
	       last = XEXP (last, 1))
	    continue;
	  last = XEXP (last, 0);
	}
      else
	last = NEXT_INSN (BB_END (bb));

      for (insn = BB_HEAD (bb); insn != last; insn = NEXT_INSN (insn))
	if (INSN_P (insn))
	  {
	    note = find_reg_equal_equiv_note (insn);
	    if (!note || !expr_equiv_p (XEXP (note, 0), mem))
	      continue;

	    if (dump_file)
	      fprintf (dump_file, "STORE_MOTION  drop REG_EQUAL note at insn %d:\n",
		       INSN_UID (insn));
	    remove_note (insn, note);
	  }

      if (!ei_end_p (ei))
	ei_next (&ei);
      act = (! ei_end_p (ei)) ? ei_edge (ei) : NULL;

      if (EDGE_COUNT (bb->succs) > 0)
	{
	  if (act)
	    stack[sp++] = ei;
	  ei = ei_start (bb->succs);
	  act = (EDGE_COUNT (ei_container (ei)) > 0 ? EDGE_I (ei_container (ei), 0) : NULL);
	}
    }
}

/* This routine will replace a store with a SET to a specified register.  */

static void
replace_store_insn (rtx reg, rtx del, basic_block bb, struct ls_expr *smexpr)
{
  rtx insn, mem, note, set, ptr, pair;

  mem = smexpr->pattern;
  insn = gen_move_insn (reg, SET_SRC (single_set (del)));
  insn = emit_insn_after (insn, del);

  if (dump_file)
    {
      fprintf (dump_file,
	       "STORE_MOTION  delete insn in BB %d:\n      ", bb->index);
      print_inline_rtx (dump_file, del, 6);
      fprintf (dump_file, "\nSTORE MOTION  replaced with insn:\n      ");
      print_inline_rtx (dump_file, insn, 6);
      fprintf (dump_file, "\n");
    }

  for (ptr = ANTIC_STORE_LIST (smexpr); ptr; ptr = XEXP (ptr, 1))
    if (XEXP (ptr, 0) == del)
      {
	XEXP (ptr, 0) = insn;
	break;
      }

  /* Move the notes from the deleted insn to its replacement, and patch
     up the LIBCALL notes.  */
  REG_NOTES (insn) = REG_NOTES (del);

  note = find_reg_note (insn, REG_RETVAL, NULL_RTX);
  if (note)
    {
      pair = XEXP (note, 0);
      note = find_reg_note (pair, REG_LIBCALL, NULL_RTX);
      XEXP (note, 0) = insn;
    }
  note = find_reg_note (insn, REG_LIBCALL, NULL_RTX);
  if (note)
    {
      pair = XEXP (note, 0);
      note = find_reg_note (pair, REG_RETVAL, NULL_RTX);
      XEXP (note, 0) = insn;
    }

  delete_insn (del);

  /* Now we must handle REG_EQUAL notes whose contents is equal to the mem;
     they are no longer accurate provided that they are reached by this
     definition, so drop them.  */
  for (; insn != NEXT_INSN (BB_END (bb)); insn = NEXT_INSN (insn))
    if (INSN_P (insn))
      {
	set = single_set (insn);
	if (!set)
	  continue;
	if (expr_equiv_p (SET_DEST (set), mem))
	  return;
	note = find_reg_equal_equiv_note (insn);
	if (!note || !expr_equiv_p (XEXP (note, 0), mem))
	  continue;

	if (dump_file)
	  fprintf (dump_file, "STORE_MOTION  drop REG_EQUAL note at insn %d:\n",
		   INSN_UID (insn));
	remove_note (insn, note);
      }
  remove_reachable_equiv_notes (bb, smexpr);
}


/* Delete a store, but copy the value that would have been stored into
   the reaching_reg for later storing.  */

static void
delete_store (struct ls_expr * expr, basic_block bb)
{
  rtx reg, i, del;

  if (expr->reaching_reg == NULL_RTX)
    expr->reaching_reg = gen_reg_rtx (GET_MODE (expr->pattern));

  reg = expr->reaching_reg;

  for (i = AVAIL_STORE_LIST (expr); i; i = XEXP (i, 1))
    {
      del = XEXP (i, 0);
      if (BLOCK_FOR_INSN (del) == bb)
	{
	  /* We know there is only one since we deleted redundant
	     ones during the available computation.  */
	  replace_store_insn (reg, del, bb, expr);
	  break;
	}
    }
}

/* Free memory used by store motion.  */

static void
free_store_memory (void)
{
  free_ldst_mems ();

  if (ae_gen)
    sbitmap_vector_free (ae_gen);
  if (ae_kill)
    sbitmap_vector_free (ae_kill);
  if (transp)
    sbitmap_vector_free (transp);
  if (st_antloc)
    sbitmap_vector_free (st_antloc);
  if (pre_insert_map)
    sbitmap_vector_free (pre_insert_map);
  if (pre_delete_map)
    sbitmap_vector_free (pre_delete_map);
  if (reg_set_in_block)
    sbitmap_vector_free (reg_set_in_block);

  ae_gen = ae_kill = transp = st_antloc = NULL;
  pre_insert_map = pre_delete_map = reg_set_in_block = NULL;
}

/* Perform store motion. Much like gcse, except we move expressions the
   other way by looking at the flowgraph in reverse.  */

static void
store_motion (void)
{
  basic_block bb;
  int x;
  struct ls_expr * ptr;
  int update_flow = 0;

  if (dump_file)
    {
      fprintf (dump_file, "before store motion\n");
      print_rtl (dump_file, get_insns ());
    }

  init_alias_analysis ();

  /* Find all the available and anticipatable stores.  */
  num_stores = compute_store_table ();
  if (num_stores == 0)
    {
      htab_delete (pre_ldst_table);
      pre_ldst_table = NULL;
      sbitmap_vector_free (reg_set_in_block);
      end_alias_analysis ();
      return;
    }

  /* Now compute kill & transp vectors.  */
  build_store_vectors ();
  add_noreturn_fake_exit_edges ();
  connect_infinite_loops_to_exit ();

  edge_list = pre_edge_rev_lcm (num_stores, transp, ae_gen,
				st_antloc, ae_kill, &pre_insert_map,
				&pre_delete_map);

  /* Now we want to insert the new stores which are going to be needed.  */
  for (ptr = first_ls_expr (); ptr != NULL; ptr = next_ls_expr (ptr))
    {
      /* If any of the edges we have above are abnormal, we can't move this
	 store.  */
      for (x = NUM_EDGES (edge_list) - 1; x >= 0; x--)
	if (TEST_BIT (pre_insert_map[x], ptr->index)
	    && (INDEX_EDGE (edge_list, x)->flags & EDGE_ABNORMAL))
	  break;

      if (x >= 0)
	{
	  if (dump_file != NULL)
	    fprintf (dump_file,
		     "Can't replace store %d: abnormal edge from %d to %d\n",
		     ptr->index, INDEX_EDGE (edge_list, x)->src->index,
		     INDEX_EDGE (edge_list, x)->dest->index);
	  continue;
	}
		      
      /* Now we want to insert the new stores which are going to be needed.  */

      FOR_EACH_BB (bb)
	if (TEST_BIT (pre_delete_map[bb->index], ptr->index))
	  delete_store (ptr, bb);

      for (x = 0; x < NUM_EDGES (edge_list); x++)
	if (TEST_BIT (pre_insert_map[x], ptr->index))
	  update_flow |= insert_store (ptr, INDEX_EDGE (edge_list, x));
    }

  if (update_flow)
    commit_edge_insertions ();

  free_store_memory ();
  free_edge_list (edge_list);
  remove_fake_exit_edges ();
  end_alias_analysis ();
}


/* Entry point for jump bypassing optimization pass.  */

static int
bypass_jumps (void)
{
  int changed;

  /* We do not construct an accurate cfg in functions which call
     setjmp, so just punt to be safe.  */
  if (current_function_calls_setjmp)
    return 0;

  /* Identify the basic block information for this function, including
     successors and predecessors.  */
  max_gcse_regno = max_reg_num ();

  if (dump_file)
    dump_flow_info (dump_file, dump_flags);

  /* Return if there's nothing to do, or it is too expensive.  */
  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1
      || is_too_expensive (_ ("jump bypassing disabled")))
    return 0;

  gcc_obstack_init (&gcse_obstack);
  bytes_used = 0;

  /* We need alias.  */
  init_alias_analysis ();

  /* Record where pseudo-registers are set.  This data is kept accurate
     during each pass.  ??? We could also record hard-reg information here
     [since it's unchanging], however it is currently done during hash table
     computation.

     It may be tempting to compute MEM set information here too, but MEM sets
     will be subject to code motion one day and thus we need to compute
     information about memory sets when we build the hash tables.  */

  alloc_reg_set_mem (max_gcse_regno);
  compute_sets ();

  max_gcse_regno = max_reg_num ();
  alloc_gcse_mem ();
  changed = one_cprop_pass (MAX_GCSE_PASSES + 2, true, true);
  free_gcse_mem ();

  if (dump_file)
    {
      fprintf (dump_file, "BYPASS of %s: %d basic blocks, ",
	       current_function_name (), n_basic_blocks);
      fprintf (dump_file, "%d bytes\n\n", bytes_used);
    }

  obstack_free (&gcse_obstack, NULL);
  free_reg_set_mem ();

  /* We are finished with alias.  */
  end_alias_analysis ();
  allocate_reg_info (max_reg_num (), FALSE, FALSE);

  return changed;
}

/* Return true if the graph is too expensive to optimize. PASS is the
   optimization about to be performed.  */

static bool
is_too_expensive (const char *pass)
{
  /* Trying to perform global optimizations on flow graphs which have
     a high connectivity will take a long time and is unlikely to be
     particularly useful.

     In normal circumstances a cfg should have about twice as many
     edges as blocks.  But we do not want to punish small functions
     which have a couple switch statements.  Rather than simply
     threshold the number of blocks, uses something with a more
     graceful degradation.  */
  if (n_edges > 20000 + n_basic_blocks * 4)
    {
      warning (OPT_Wdisabled_optimization,
	       "%s: %d basic blocks and %d edges/basic block",
	       pass, n_basic_blocks, n_edges / n_basic_blocks);

      return true;
    }

  /* If allocating memory for the cprop bitmap would take up too much
     storage it's better just to disable the optimization.  */
  if ((n_basic_blocks
       * SBITMAP_SET_SIZE (max_reg_num ())
       * sizeof (SBITMAP_ELT_TYPE)) > MAX_GCSE_MEMORY)
    {
      warning (OPT_Wdisabled_optimization,
	       "%s: %d basic blocks and %d registers",
	       pass, n_basic_blocks, max_reg_num ());

      return true;
    }

  return false;
}

static bool
gate_handle_jump_bypass (void)
{
  return optimize > 0 && flag_gcse;
}

/* Perform jump bypassing and control flow optimizations.  */
static unsigned int
rest_of_handle_jump_bypass (void)
{
  cleanup_cfg (CLEANUP_EXPENSIVE);
  reg_scan (get_insns (), max_reg_num ());

  if (bypass_jumps ())
    {
      rebuild_jump_labels (get_insns ());
      cleanup_cfg (CLEANUP_EXPENSIVE);
      delete_trivially_dead_insns (get_insns (), max_reg_num ());
    }
  return 0;
}

struct tree_opt_pass pass_jump_bypass =
{
  "bypass",                             /* name */
  gate_handle_jump_bypass,              /* gate */   
  rest_of_handle_jump_bypass,           /* execute */       
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_BYPASS,                            /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_ggc_collect | TODO_verify_flow,  /* todo_flags_finish */
  'G'                                   /* letter */
};


static bool
gate_handle_gcse (void)
{
  return optimize > 0 && flag_gcse;
}


static unsigned int
rest_of_handle_gcse (void)
{
  int save_csb, save_cfj;
  int tem2 = 0, tem;

  tem = gcse_main (get_insns ());
  rebuild_jump_labels (get_insns ());
  delete_trivially_dead_insns (get_insns (), max_reg_num ());

  save_csb = flag_cse_skip_blocks;
  save_cfj = flag_cse_follow_jumps;
  flag_cse_skip_blocks = flag_cse_follow_jumps = 0;

  /* If -fexpensive-optimizations, re-run CSE to clean up things done
     by gcse.  */
  if (flag_expensive_optimizations)
    {
      timevar_push (TV_CSE);
      reg_scan (get_insns (), max_reg_num ());
      tem2 = cse_main (get_insns (), max_reg_num ());
      purge_all_dead_edges ();
      delete_trivially_dead_insns (get_insns (), max_reg_num ());
      timevar_pop (TV_CSE);
      cse_not_expected = !flag_rerun_cse_after_loop;
    }

  /* If gcse or cse altered any jumps, rerun jump optimizations to clean
     things up.  */
  if (tem || tem2)
    {
      timevar_push (TV_JUMP);
      rebuild_jump_labels (get_insns ());
      delete_dead_jumptables ();
      cleanup_cfg (CLEANUP_EXPENSIVE);
      timevar_pop (TV_JUMP);
    }

  flag_cse_skip_blocks = save_csb;
  flag_cse_follow_jumps = save_cfj;
  return 0;
}

struct tree_opt_pass pass_gcse =
{
  "gcse1",                              /* name */
  gate_handle_gcse,                     /* gate */   
  rest_of_handle_gcse,			/* execute */       
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_GCSE,                              /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func |
  TODO_verify_flow | TODO_ggc_collect,  /* todo_flags_finish */
  'G'                                   /* letter */
};


#include "gt-gcse.h"
