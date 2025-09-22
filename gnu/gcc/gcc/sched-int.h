/* Instruction scheduling pass.  This file contains definitions used
   internally in the scheduler.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

#ifndef GCC_SCHED_INT_H
#define GCC_SCHED_INT_H

/* For state_t.  */
#include "insn-attr.h"
/* For regset_head.  */
#include "basic-block.h"
/* For reg_note.  */
#include "rtl.h"

/* Pointer to data describing the current DFA state.  */
extern state_t curr_state;

/* Forward declaration.  */
struct ready_list;

/* Type to represent status of a dependence.  */
typedef int ds_t;

/* Type to represent weakness of speculative dependence.  */
typedef int dw_t;

/* Describe state of dependencies used during sched_analyze phase.  */
struct deps
{
  /* The *_insns and *_mems are paired lists.  Each pending memory operation
     will have a pointer to the MEM rtx on one list and a pointer to the
     containing insn on the other list in the same place in the list.  */

  /* We can't use add_dependence like the old code did, because a single insn
     may have multiple memory accesses, and hence needs to be on the list
     once for each memory access.  Add_dependence won't let you add an insn
     to a list more than once.  */

  /* An INSN_LIST containing all insns with pending read operations.  */
  rtx pending_read_insns;

  /* An EXPR_LIST containing all MEM rtx's which are pending reads.  */
  rtx pending_read_mems;

  /* An INSN_LIST containing all insns with pending write operations.  */
  rtx pending_write_insns;

  /* An EXPR_LIST containing all MEM rtx's which are pending writes.  */
  rtx pending_write_mems;

  /* Indicates the combined length of the two pending lists.  We must prevent
     these lists from ever growing too large since the number of dependencies
     produced is at least O(N*N), and execution time is at least O(4*N*N), as
     a function of the length of these pending lists.  */
  int pending_lists_length;

  /* Length of the pending memory flush list. Large functions with no
     calls may build up extremely large lists.  */
  int pending_flush_length;

  /* The last insn upon which all memory references must depend.
     This is an insn which flushed the pending lists, creating a dependency
     between it and all previously pending memory references.  This creates
     a barrier (or a checkpoint) which no memory reference is allowed to cross.

     This includes all non constant CALL_INSNs.  When we do interprocedural
     alias analysis, this restriction can be relaxed.
     This may also be an INSN that writes memory if the pending lists grow
     too large.  */
  rtx last_pending_memory_flush;

  /* A list of the last function calls we have seen.  We use a list to
     represent last function calls from multiple predecessor blocks.
     Used to prevent register lifetimes from expanding unnecessarily.  */
  rtx last_function_call;

  /* A list of insns which use a pseudo register that does not already
     cross a call.  We create dependencies between each of those insn
     and the next call insn, to ensure that they won't cross a call after
     scheduling is done.  */
  rtx sched_before_next_call;

  /* Used to keep post-call pseudo/hard reg movements together with
     the call.  */
  enum { not_post_call, post_call, post_call_initial } in_post_call_group_p;

  /* Set to the tail insn of the outermost libcall block.

     When nonzero, we will mark each insn processed by sched_analyze_insn
     with SCHED_GROUP_P to ensure libcalls are scheduled as a unit.  */
  rtx libcall_block_tail_insn;

  /* The maximum register number for the following arrays.  Before reload
     this is max_reg_num; after reload it is FIRST_PSEUDO_REGISTER.  */
  int max_reg;

  /* Element N is the next insn that sets (hard or pseudo) register
     N within the current basic block; or zero, if there is no
     such insn.  Needed for new registers which may be introduced
     by splitting insns.  */
  struct deps_reg
    {
      rtx uses;
      rtx sets;
      rtx clobbers;
      int uses_length;
      int clobbers_length;
    } *reg_last;

  /* Element N is set for each register that has any nonzero element
     in reg_last[N].{uses,sets,clobbers}.  */
  regset_head reg_last_in_use;

  /* Element N is set for each register that is conditionally set.  */
  regset_head reg_conditional_sets;
};

/* This structure holds some state of the current scheduling pass, and
   contains some function pointers that abstract out some of the non-generic
   functionality from functions such as schedule_block or schedule_insn.
   There is one global variable, current_sched_info, which points to the
   sched_info structure currently in use.  */
struct sched_info
{
  /* Add all insns that are initially ready to the ready list.  Called once
     before scheduling a set of insns.  */
  void (*init_ready_list) (void);
  /* Called after taking an insn from the ready list.  Returns nonzero if
     this insn can be scheduled, nonzero if we should silently discard it.  */
  int (*can_schedule_ready_p) (rtx);
  /* Return nonzero if there are more insns that should be scheduled.  */
  int (*schedule_more_p) (void);
  /* Called after an insn has all its hard dependencies resolved. 
     Adjusts status of instruction (which is passed through second parameter)
     to indicate if instruction should be moved to the ready list or the
     queue, or if it should silently discard it (until next resolved
     dependence).  */
  ds_t (*new_ready) (rtx, ds_t);
  /* Compare priority of two insns.  Return a positive number if the second
     insn is to be preferred for scheduling, and a negative one if the first
     is to be preferred.  Zero if they are equally good.  */
  int (*rank) (rtx, rtx);
  /* Return a string that contains the insn uid and optionally anything else
     necessary to identify this insn in an output.  It's valid to use a
     static buffer for this.  The ALIGNED parameter should cause the string
     to be formatted so that multiple output lines will line up nicely.  */
  const char *(*print_insn) (rtx, int);
  /* Return nonzero if an insn should be included in priority
     calculations.  */
  int (*contributes_to_priority) (rtx, rtx);
  /* Called when computing dependencies for a JUMP_INSN.  This function
     should store the set of registers that must be considered as set by
     the jump in the regset.  */
  void (*compute_jump_reg_dependencies) (rtx, regset, regset, regset);

  /* The boundaries of the set of insns to be scheduled.  */
  rtx prev_head, next_tail;

  /* Filled in after the schedule is finished; the first and last scheduled
     insns.  */
  rtx head, tail;

  /* If nonzero, enables an additional sanity check in schedule_block.  */
  unsigned int queue_must_finish_empty:1;
  /* Nonzero if we should use cselib for better alias analysis.  This
     must be 0 if the dependency information is used after sched_analyze
     has completed, e.g. if we're using it to initialize state for successor
     blocks in region scheduling.  */
  unsigned int use_cselib:1;

  /* Maximum priority that has been assigned to an insn.  */
  int sched_max_insns_priority;

  /* Hooks to support speculative scheduling.  */

  /* Called to notify frontend that instruction is being added (second
     parameter == 0) or removed (second parameter == 1).  */     
  void (*add_remove_insn) (rtx, int);

  /* Called to notify frontend that instruction is being scheduled.
     The first parameter - instruction to scheduled, the second parameter -
     last scheduled instruction.  */
  void (*begin_schedule_ready) (rtx, rtx);

  /* Called to notify frontend, that new basic block is being added.
     The first parameter - new basic block.
     The second parameter - block, after which new basic block is being added,
     or EXIT_BLOCK_PTR, if recovery block is being added,
     or NULL, if standalone block is being added.  */
  void (*add_block) (basic_block, basic_block);

  /* If the second parameter is not NULL, return nonnull value, if the
     basic block should be advanced.
     If the second parameter is NULL, return the next basic block in EBB.
     The first parameter is the current basic block in EBB.  */
  basic_block (*advance_target_bb) (basic_block, rtx);

  /* Called after blocks were rearranged due to movement of jump instruction.
     The first parameter - index of basic block, in which jump currently is.
     The second parameter - index of basic block, in which jump used
     to be.
     The third parameter - index of basic block, that follows the second
     parameter.  */
  void (*fix_recovery_cfg) (int, int, int);

#ifdef ENABLE_CHECKING
  /* If the second parameter is zero, return nonzero, if block is head of the
     region.
     If the second parameter is nonzero, return nonzero, if block is leaf of
     the region.
     global_live_at_start should not change in region heads and
     global_live_at_end should not change in region leafs due to scheduling.  */
  int (*region_head_or_leaf_p) (basic_block, int);
#endif

  /* ??? FIXME: should use straight bitfields inside sched_info instead of
     this flag field.  */
  unsigned int flags;
};

/* This structure holds description of the properties for speculative
   scheduling.  */
struct spec_info_def
{
  /* Holds types of allowed speculations: BEGIN_{DATA|CONTROL},
     BE_IN_{DATA_CONTROL}.  */
  int mask;

  /* A dump file for additional information on speculative scheduling.  */
  FILE *dump;

  /* Minimal cumulative weakness of speculative instruction's
     dependencies, so that insn will be scheduled.  */
  dw_t weakness_cutoff;

  /* Flags from the enum SPEC_SCHED_FLAGS.  */
  int flags;
};
typedef struct spec_info_def *spec_info_t;

extern struct sched_info *current_sched_info;

/* Indexed by INSN_UID, the collection of all data associated with
   a single instruction.  */

struct haifa_insn_data
{
  /* A list of insns which depend on the instruction.  Unlike LOG_LINKS,
     it represents forward dependencies.  */
  rtx depend;

  /* A list of scheduled producers of the instruction.  Links are being moved
     from LOG_LINKS to RESOLVED_DEPS during scheduling.  */
  rtx resolved_deps;
  
  /* The line number note in effect for each insn.  For line number
     notes, this indicates whether the note may be reused.  */
  rtx line_note;

  /* Logical uid gives the original ordering of the insns.  */
  int luid;

  /* A priority for each insn.  */
  int priority;

  /* The number of incoming edges in the forward dependency graph.
     As scheduling proceeds, counts are decreased.  An insn moves to
     the ready queue when its counter reaches zero.  */
  int dep_count;

  /* Number of instructions referring to this insn.  */
  int ref_count;

  /* The minimum clock tick at which the insn becomes ready.  This is
     used to note timing constraints for the insns in the pending list.  */
  int tick;

  /* INTER_TICK is used to adjust INSN_TICKs of instructions from the
     subsequent blocks in a region.  */
  int inter_tick;
  
  /* See comment on QUEUE_INDEX macro in haifa-sched.c.  */
  int queue_index;

  short cost;

  /* This weight is an estimation of the insn's contribution to
     register pressure.  */
  short reg_weight;

  /* Some insns (e.g. call) are not allowed to move across blocks.  */
  unsigned int cant_move : 1;

  /* Set if there's DEF-USE dependence between some speculatively
     moved load insn and this one.  */
  unsigned int fed_by_spec_load : 1;
  unsigned int is_load_insn : 1;

  /* Nonzero if priority has been computed already.  */
  unsigned int priority_known : 1;

  /* Nonzero if instruction has internal dependence
     (e.g. add_dependence was invoked with (insn == elem)).  */
  unsigned int has_internal_dep : 1;
  
  /* What speculations are necessary to apply to schedule the instruction.  */
  ds_t todo_spec;
  /* What speculations were already applied.  */
  ds_t done_spec; 
  /* What speculations are checked by this instruction.  */
  ds_t check_spec;

  /* Recovery block for speculation checks.  */
  basic_block recovery_block;

  /* Original pattern of the instruction.  */
  rtx orig_pat;
};

extern struct haifa_insn_data *h_i_d;
/* Used only if (current_sched_info->flags & USE_GLAT) != 0.
   These regsets store global_live_at_{start, end} information
   for each basic block.  */
extern regset *glat_start, *glat_end;

/* Accessor macros for h_i_d.  There are more in haifa-sched.c and
   sched-rgn.c.  */
#define INSN_DEPEND(INSN)	(h_i_d[INSN_UID (INSN)].depend)
#define RESOLVED_DEPS(INSN)     (h_i_d[INSN_UID (INSN)].resolved_deps)
#define INSN_LUID(INSN)		(h_i_d[INSN_UID (INSN)].luid)
#define CANT_MOVE(insn)		(h_i_d[INSN_UID (insn)].cant_move)
#define INSN_DEP_COUNT(INSN)	(h_i_d[INSN_UID (INSN)].dep_count)
#define INSN_PRIORITY(INSN)	(h_i_d[INSN_UID (INSN)].priority)
#define INSN_PRIORITY_KNOWN(INSN) (h_i_d[INSN_UID (INSN)].priority_known)
#define INSN_COST(INSN)		(h_i_d[INSN_UID (INSN)].cost)
#define INSN_REG_WEIGHT(INSN)	(h_i_d[INSN_UID (INSN)].reg_weight)
#define HAS_INTERNAL_DEP(INSN)  (h_i_d[INSN_UID (INSN)].has_internal_dep)
#define TODO_SPEC(INSN)         (h_i_d[INSN_UID (INSN)].todo_spec)
#define DONE_SPEC(INSN)         (h_i_d[INSN_UID (INSN)].done_spec)
#define CHECK_SPEC(INSN)        (h_i_d[INSN_UID (INSN)].check_spec)
#define RECOVERY_BLOCK(INSN)    (h_i_d[INSN_UID (INSN)].recovery_block)
#define ORIG_PAT(INSN)          (h_i_d[INSN_UID (INSN)].orig_pat)

/* INSN is either a simple or a branchy speculation check.  */
#define IS_SPECULATION_CHECK_P(INSN) (RECOVERY_BLOCK (INSN) != NULL)

/* INSN is a speculation check that will simply reexecute the speculatively
   scheduled instruction if the speculation fails.  */
#define IS_SPECULATION_SIMPLE_CHECK_P(INSN) \
  (RECOVERY_BLOCK (INSN) == EXIT_BLOCK_PTR)

/* INSN is a speculation check that will branch to RECOVERY_BLOCK if the
   speculation fails.  Insns in that block will reexecute the speculatively
   scheduled code and then will return immediately after INSN thus preserving
   semantics of the program.  */
#define IS_SPECULATION_BRANCHY_CHECK_P(INSN) \
  (RECOVERY_BLOCK (INSN) != NULL && RECOVERY_BLOCK (INSN) != EXIT_BLOCK_PTR)

/* DEP_STATUS of the link encapsulates information, that is needed for
   speculative scheduling.  Namely, it is 4 integers in the range
   [0, MAX_DEP_WEAK] and 3 bits.
   The integers correspond to the probability of the dependence to *not*
   exist, it is the probability, that overcoming of this dependence will
   not be followed by execution of the recovery code.  Nevertheless,
   whatever high the probability of success is, recovery code should still
   be generated to preserve semantics of the program.  To find a way to
   get/set these integers, please refer to the {get, set}_dep_weak ()
   functions in sched-deps.c .
   The 3 bits in the DEP_STATUS correspond to 3 dependence types: true-,
   output- and anti- dependence.  It is not enough for speculative scheduling
   to know just the major type of all the dependence between two instructions,
   as only true dependence can be overcome.
   There also is the 4-th bit in the DEP_STATUS (HARD_DEP), that is reserved
   for using to describe instruction's status.  It is set whenever instruction
   has at least one dependence, that cannot be overcome.
   See also: check_dep_status () in sched-deps.c .  */
#define DEP_STATUS(LINK) XINT (LINK, 2)

/* We exclude sign bit.  */
#define BITS_PER_DEP_STATUS (HOST_BITS_PER_INT - 1)

/* First '4' stands for 3 dep type bits and HARD_DEP bit.
   Second '4' stands for BEGIN_{DATA, CONTROL}, BE_IN_{DATA, CONTROL}
   dep weakness.  */
#define BITS_PER_DEP_WEAK ((BITS_PER_DEP_STATUS - 4) / 4)

/* Mask of speculative weakness in dep_status.  */
#define DEP_WEAK_MASK ((1 << BITS_PER_DEP_WEAK) - 1)

/* This constant means that dependence is fake with 99.999...% probability.
   This is the maximum value, that can appear in dep_status.
   Note, that we don't want MAX_DEP_WEAK to be the same as DEP_WEAK_MASK for
   debugging reasons.  Though, it can be set to DEP_WEAK_MASK, and, when
   done so, we'll get fast (mul for)/(div by) NO_DEP_WEAK.  */
#define MAX_DEP_WEAK (DEP_WEAK_MASK - 1)

/* This constant means that dependence is 99.999...% real and it is a really
   bad idea to overcome it (though this can be done, preserving program
   semantics).  */
#define MIN_DEP_WEAK 1

/* This constant represents 100% probability.
   E.g. it is used to represent weakness of dependence, that doesn't exist.  */
#define NO_DEP_WEAK (MAX_DEP_WEAK + MIN_DEP_WEAK)

/* Default weakness of speculative dependence.  Used when we can't say
   neither bad nor good about the dependence.  */
#define UNCERTAIN_DEP_WEAK (MAX_DEP_WEAK - MAX_DEP_WEAK / 4)

/* Offset for speculative weaknesses in dep_status.  */
enum SPEC_TYPES_OFFSETS {
  BEGIN_DATA_BITS_OFFSET = 0,
  BE_IN_DATA_BITS_OFFSET = BEGIN_DATA_BITS_OFFSET + BITS_PER_DEP_WEAK,
  BEGIN_CONTROL_BITS_OFFSET = BE_IN_DATA_BITS_OFFSET + BITS_PER_DEP_WEAK,
  BE_IN_CONTROL_BITS_OFFSET = BEGIN_CONTROL_BITS_OFFSET + BITS_PER_DEP_WEAK
};

/* The following defines provide numerous constants used to distinguish between
   different types of speculative dependencies.  */

/* Dependence can be overcome with generation of new data speculative
   instruction.  */
#define BEGIN_DATA (((ds_t) DEP_WEAK_MASK) << BEGIN_DATA_BITS_OFFSET)

/* This dependence is to the instruction in the recovery block, that was
   formed to recover after data-speculation failure.
   Thus, this dependence can overcome with generating of the copy of
   this instruction in the recovery block.  */
#define BE_IN_DATA (((ds_t) DEP_WEAK_MASK) << BE_IN_DATA_BITS_OFFSET)

/* Dependence can be overcome with generation of new control speculative
   instruction.  */
#define BEGIN_CONTROL (((ds_t) DEP_WEAK_MASK) << BEGIN_CONTROL_BITS_OFFSET)

/* This dependence is to the instruction in the recovery block, that was
   formed to recover after control-speculation failure.
   Thus, this dependence can be overcome with generating of the copy of
   this instruction in the recovery block.  */
#define BE_IN_CONTROL (((ds_t) DEP_WEAK_MASK) << BE_IN_CONTROL_BITS_OFFSET)

/* A few convenient combinations.  */
#define BEGIN_SPEC (BEGIN_DATA | BEGIN_CONTROL)
#define DATA_SPEC (BEGIN_DATA | BE_IN_DATA)
#define CONTROL_SPEC (BEGIN_CONTROL | BE_IN_CONTROL)
#define SPECULATIVE (DATA_SPEC | CONTROL_SPEC)
#define BE_IN_SPEC (BE_IN_DATA | BE_IN_CONTROL)

/* Constants, that are helpful in iterating through dep_status.  */
#define FIRST_SPEC_TYPE BEGIN_DATA
#define LAST_SPEC_TYPE BE_IN_CONTROL
#define SPEC_TYPE_SHIFT BITS_PER_DEP_WEAK

/* Dependence on instruction can be of multiple types
   (e.g. true and output). This fields enhance REG_NOTE_KIND information
   of the dependence.  */
#define DEP_TRUE (((ds_t) 1) << (BE_IN_CONTROL_BITS_OFFSET + BITS_PER_DEP_WEAK))
#define DEP_OUTPUT (DEP_TRUE << 1)
#define DEP_ANTI (DEP_OUTPUT << 1)

#define DEP_TYPES (DEP_TRUE | DEP_OUTPUT | DEP_ANTI)

/* Instruction has non-speculative dependence.  This bit represents the
   property of an instruction - not the one of a dependence.
   Therefore, it can appear only in TODO_SPEC field of an instruction.  */
#define HARD_DEP (DEP_ANTI << 1)

/* This represents the results of calling sched-deps.c functions, 
   which modify dependencies.  Possible choices are: a dependence
   is already present and nothing has been changed; a dependence type
   has been changed; brand new dependence has been created.  */
enum DEPS_ADJUST_RESULT {
  DEP_PRESENT = 1,
  DEP_CHANGED = 2,
  DEP_CREATED = 3
};

/* Represents the bits that can be set in the flags field of the 
   sched_info structure.  */
enum SCHED_FLAGS {
  /* If set, generate links between instruction as DEPS_LIST.
     Otherwise, generate usual INSN_LIST links.  */
  USE_DEPS_LIST = 1,
  /* Perform data or control (or both) speculation.
     Results in generation of data and control speculative dependencies.
     Requires USE_DEPS_LIST set.  */
  DO_SPECULATION = USE_DEPS_LIST << 1,
  SCHED_RGN = DO_SPECULATION << 1,
  SCHED_EBB = SCHED_RGN << 1,
  /* Detach register live information from basic block headers.
     This is necessary to invoke functions, that change CFG (e.g. split_edge).
     Requires USE_GLAT.  */
  DETACH_LIFE_INFO = SCHED_EBB << 1,
  /* Save register live information from basic block headers to
     glat_{start, end} arrays.  */
  USE_GLAT = DETACH_LIFE_INFO << 1
};

enum SPEC_SCHED_FLAGS {
  COUNT_SPEC_IN_CRITICAL_PATH = 1,
  PREFER_NON_DATA_SPEC = COUNT_SPEC_IN_CRITICAL_PATH << 1,
  PREFER_NON_CONTROL_SPEC = PREFER_NON_DATA_SPEC << 1
};

#define NOTE_NOT_BB_P(NOTE) (NOTE_P (NOTE) && (NOTE_LINE_NUMBER (NOTE)	\
					       != NOTE_INSN_BASIC_BLOCK))

extern FILE *sched_dump;
extern int sched_verbose;

/* Exception Free Loads:

   We define five classes of speculative loads: IFREE, IRISKY,
   PFREE, PRISKY, and MFREE.

   IFREE loads are loads that are proved to be exception-free, just
   by examining the load insn.  Examples for such loads are loads
   from TOC and loads of global data.

   IRISKY loads are loads that are proved to be exception-risky,
   just by examining the load insn.  Examples for such loads are
   volatile loads and loads from shared memory.

   PFREE loads are loads for which we can prove, by examining other
   insns, that they are exception-free.  Currently, this class consists
   of loads for which we are able to find a "similar load", either in
   the target block, or, if only one split-block exists, in that split
   block.  Load2 is similar to load1 if both have same single base
   register.  We identify only part of the similar loads, by finding
   an insn upon which both load1 and load2 have a DEF-USE dependence.

   PRISKY loads are loads for which we can prove, by examining other
   insns, that they are exception-risky.  Currently we have two proofs for
   such loads.  The first proof detects loads that are probably guarded by a
   test on the memory address.  This proof is based on the
   backward and forward data dependence information for the region.
   Let load-insn be the examined load.
   Load-insn is PRISKY iff ALL the following hold:

   - insn1 is not in the same block as load-insn
   - there is a DEF-USE dependence chain (insn1, ..., load-insn)
   - test-insn is either a compare or a branch, not in the same block
     as load-insn
   - load-insn is reachable from test-insn
   - there is a DEF-USE dependence chain (insn1, ..., test-insn)

   This proof might fail when the compare and the load are fed
   by an insn not in the region.  To solve this, we will add to this
   group all loads that have no input DEF-USE dependence.

   The second proof detects loads that are directly or indirectly
   fed by a speculative load.  This proof is affected by the
   scheduling process.  We will use the flag  fed_by_spec_load.
   Initially, all insns have this flag reset.  After a speculative
   motion of an insn, if insn is either a load, or marked as
   fed_by_spec_load, we will also mark as fed_by_spec_load every
   insn1 for which a DEF-USE dependence (insn, insn1) exists.  A
   load which is fed_by_spec_load is also PRISKY.

   MFREE (maybe-free) loads are all the remaining loads. They may be
   exception-free, but we cannot prove it.

   Now, all loads in IFREE and PFREE classes are considered
   exception-free, while all loads in IRISKY and PRISKY classes are
   considered exception-risky.  As for loads in the MFREE class,
   these are considered either exception-free or exception-risky,
   depending on whether we are pessimistic or optimistic.  We have
   to take the pessimistic approach to assure the safety of
   speculative scheduling, but we can take the optimistic approach
   by invoking the -fsched_spec_load_dangerous option.  */

enum INSN_TRAP_CLASS
{
  TRAP_FREE = 0, IFREE = 1, PFREE_CANDIDATE = 2,
  PRISKY_CANDIDATE = 3, IRISKY = 4, TRAP_RISKY = 5
};

#define WORST_CLASS(class1, class2) \
((class1 > class2) ? class1 : class2)

#ifndef __GNUC__
#define __inline
#endif

#ifndef HAIFA_INLINE
#define HAIFA_INLINE __inline
#endif

/* Functions in sched-vis.c.  */
extern void print_insn (char *, rtx, int);

/* Functions in sched-deps.c.  */
extern bool sched_insns_conditions_mutex_p (rtx, rtx);
extern void add_dependence (rtx, rtx, enum reg_note);
extern void sched_analyze (struct deps *, rtx, rtx);
extern void init_deps (struct deps *);
extern void free_deps (struct deps *);
extern void init_deps_global (void);
extern void finish_deps_global (void);
extern void add_forw_dep (rtx, rtx);
extern void compute_forward_dependences (rtx, rtx);
extern rtx find_insn_list (rtx, rtx);
extern void init_dependency_caches (int);
extern void free_dependency_caches (void);
extern void extend_dependency_caches (int, bool);
extern enum DEPS_ADJUST_RESULT add_or_update_back_dep (rtx, rtx, 
						       enum reg_note, ds_t);
extern void add_or_update_back_forw_dep (rtx, rtx, enum reg_note, ds_t);
extern void add_back_forw_dep (rtx, rtx, enum reg_note, ds_t);
extern void delete_back_forw_dep (rtx, rtx);
extern dw_t get_dep_weak (ds_t, ds_t);
extern ds_t set_dep_weak (ds_t, ds_t, dw_t);
extern ds_t ds_merge (ds_t, ds_t);

/* Functions in haifa-sched.c.  */
extern int haifa_classify_insn (rtx);
extern void get_ebb_head_tail (basic_block, basic_block, rtx *, rtx *);
extern int no_real_insns_p (rtx, rtx);

extern void rm_line_notes (rtx, rtx);
extern void save_line_notes (int, rtx, rtx);
extern void restore_line_notes (rtx, rtx);
extern void rm_redundant_line_notes (void);
extern void rm_other_notes (rtx, rtx);

extern int insn_cost (rtx, rtx, rtx);
extern int set_priorities (rtx, rtx);

extern void schedule_block (basic_block *, int);
extern void sched_init (void);
extern void sched_finish (void);

extern int try_ready (rtx);
extern void * xrecalloc (void *, size_t, size_t, size_t);
extern void unlink_bb_notes (basic_block, basic_block);
extern void add_block (basic_block, basic_block);
extern void attach_life_info (void);
extern rtx bb_note (basic_block);

#ifdef ENABLE_CHECKING
extern void check_reg_live (bool);
#endif

#endif /* GCC_SCHED_INT_H */
