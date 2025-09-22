/* Natural loop functions
   Copyright (C) 1987, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
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

#ifndef GCC_CFGLOOP_H
#define GCC_CFGLOOP_H

#include "basic-block.h"
/* For rtx_code.  */
#include "rtl.h"

/* Structure to hold decision about unrolling/peeling.  */
enum lpt_dec
{
  LPT_NONE,
  LPT_PEEL_COMPLETELY,
  LPT_PEEL_SIMPLE,
  LPT_UNROLL_CONSTANT,
  LPT_UNROLL_RUNTIME,
  LPT_UNROLL_STUPID
};

struct lpt_decision
{
  enum lpt_dec decision;
  unsigned times;
};

/* The structure describing a bound on number of iterations of a loop.  */

struct nb_iter_bound
{
  tree bound;		/* The constant expression whose value is an upper
			   bound on the number of executions of ...  */
  tree at_stmt;		/* ... this statement during one execution of
			   a loop.  */
  struct nb_iter_bound *next;
			/* The next bound in a list.  */
};

/* Structure to hold information for each natural loop.  */
struct loop
{
  /* Index into loops array.  */
  int num;

  /* Basic block of loop header.  */
  basic_block header;

  /* Basic block of loop latch.  */
  basic_block latch;

  /* For loop unrolling/peeling decision.  */
  struct lpt_decision lpt_decision;

  /* Number of loop insns.  */
  unsigned ninsns;

  /* Average number of executed insns per iteration.  */
  unsigned av_ninsns;

  /* Number of blocks contained within the loop.  */
  unsigned num_nodes;

  /* The loop nesting depth.  */
  int depth;

  /* Superloops of the loop.  */
  struct loop **pred;

  /* The height of the loop (enclosed loop levels) within the loop
     hierarchy tree.  */
  int level;

  /* The outer (parent) loop or NULL if outermost loop.  */
  struct loop *outer;

  /* The first inner (child) loop or NULL if innermost loop.  */
  struct loop *inner;

  /* Link to the next (sibling) loop.  */
  struct loop *next;

  /* Loop that is copy of this loop.  */
  struct loop *copy;

  /* Auxiliary info specific to a pass.  */
  void *aux;

  /* The probable number of times the loop is executed at runtime.
     This is an INTEGER_CST or an expression containing symbolic
     names.  Don't access this field directly:
     number_of_iterations_in_loop computes and caches the computed
     information in this field.  */
  tree nb_iterations;

  /* An INTEGER_CST estimation of the number of iterations.  NULL_TREE
     if there is no estimation.  */
  tree estimated_nb_iterations;

  /* Upper bound on number of iterations of a loop.  */
  struct nb_iter_bound *bounds;

  /* If not NULL, loop has just single exit edge stored here (edges to the
     EXIT_BLOCK_PTR do not count.  */
  edge single_exit;

  /* True when the loop does not carry data dependences, and
     consequently the iterations can be executed in any order.  False
     when the loop carries data dependences, or when the property is
     not decidable.  */
  bool parallel_p;
};

/* Flags for state of loop structure.  */
enum
{
  LOOPS_HAVE_PREHEADERS = 1,
  LOOPS_HAVE_SIMPLE_LATCHES = 2,
  LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS = 4,
  LOOPS_HAVE_MARKED_SINGLE_EXITS = 8
};

#define LOOPS_NORMAL (LOOPS_HAVE_PREHEADERS | LOOPS_HAVE_SIMPLE_LATCHES \
		      | LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS)

/* Structure to hold CFG information about natural loops within a function.  */
struct loops
{
  /* Number of natural loops in the function.  */
  unsigned num;

  /* State of loops.  */
  int state;

  /* We store just pointers to loops here.  
     Note that a loop in this array may actually be NULL, if the loop
     has been removed and the entire loops structure has not been
     recomputed since that time.  */
  struct loop **parray;

  /* Pointer to root of loop hierarchy tree.  */
  struct loop *tree_root;

  /* Information derived from the CFG.  */
  struct cfg
  {
    /* The ordering of the basic blocks in a depth first search.  */
    int *dfs_order;

    /* The reverse completion ordering of the basic blocks found in a
       depth first search.  */
    int *rc_order;
  } cfg;

  /* Headers shared by multiple loops that should be merged.  */
  sbitmap shared_headers;
};

/* The loop tree currently optimized.  */

extern struct loops *current_loops;

/* Loop recognition.  */
extern int flow_loops_find (struct loops *);
extern void flow_loops_free (struct loops *);
extern void flow_loops_dump (const struct loops *, FILE *,
			     void (*)(const struct loop *, FILE *, int), int);
extern void flow_loop_dump (const struct loop *, FILE *,
			    void (*)(const struct loop *, FILE *, int), int);
extern void flow_loop_free (struct loop *);
int flow_loop_nodes_find (basic_block, struct loop *);
void fix_loop_structure (struct loops *, bitmap changed_bbs);
void mark_irreducible_loops (struct loops *);
void mark_single_exit_loops (struct loops *);

/* Loop data structure manipulation/querying.  */
extern void flow_loop_tree_node_add (struct loop *, struct loop *);
extern void flow_loop_tree_node_remove (struct loop *);
extern bool flow_loop_nested_p	(const struct loop *, const struct loop *);
extern bool flow_bb_inside_loop_p (const struct loop *, const basic_block);
extern struct loop * find_common_loop (struct loop *, struct loop *);
struct loop *superloop_at_depth (struct loop *, unsigned);
extern unsigned tree_num_loop_insns (struct loop *);
extern int num_loop_insns (struct loop *);
extern int average_num_loop_insns (struct loop *);
extern unsigned get_loop_level (const struct loop *);
extern bool loop_exit_edge_p (const struct loop *, edge);
extern void mark_loop_exit_edges (struct loops *);

/* Loops & cfg manipulation.  */
extern basic_block *get_loop_body (const struct loop *);
extern basic_block *get_loop_body_in_dom_order (const struct loop *);
extern basic_block *get_loop_body_in_bfs_order (const struct loop *);
extern edge *get_loop_exit_edges (const struct loop *, unsigned *);
extern unsigned num_loop_branches (const struct loop *);

extern edge loop_preheader_edge (const struct loop *);
extern edge loop_latch_edge (const struct loop *);

extern void add_bb_to_loop (basic_block, struct loop *);
extern void remove_bb_from_loops (basic_block);

extern void cancel_loop_tree (struct loops *, struct loop *);

extern basic_block loop_split_edge_with (edge, rtx);
extern int fix_loop_placement (struct loop *);

enum
{
  CP_SIMPLE_PREHEADERS = 1
};

extern void create_preheaders (struct loops *, int);
extern void force_single_succ_latches (struct loops *);

extern void verify_loop_structure (struct loops *);

/* Loop analysis.  */
extern bool just_once_each_iteration_p (const struct loop *, basic_block);
extern unsigned expected_loop_iterations (const struct loop *);
extern rtx doloop_condition_get (rtx);

/* Loop manipulation.  */
extern bool can_duplicate_loop_p (struct loop *loop);

#define DLTHE_FLAG_UPDATE_FREQ	1	/* Update frequencies in
					   duplicate_loop_to_header_edge.  */
#define DLTHE_RECORD_COPY_NUMBER 2	/* Record copy number in the aux
					   field of newly create BB.  */
#define DLTHE_FLAG_COMPLETTE_PEEL 4	/* Update frequencies expecting
					   a complete peeling.  */

extern struct loop * duplicate_loop (struct loops *, struct loop *,
				     struct loop *);
extern bool duplicate_loop_to_header_edge (struct loop *, edge, struct loops *,
					   unsigned, sbitmap, edge, edge *,
					   unsigned *, int);
extern struct loop *loopify (struct loops *, edge, edge,
			     basic_block, edge, edge, bool);
struct loop * loop_version (struct loops *, struct loop *, void *,
			    basic_block *, bool);
extern bool remove_path (struct loops *, edge);

/* Induction variable analysis.  */

/* The description of induction variable.  The things are a bit complicated
   due to need to handle subregs and extends.  The value of the object described
   by it can be obtained as follows (all computations are done in extend_mode):

   Value in i-th iteration is
     delta + mult * extend_{extend_mode} (subreg_{mode} (base + i * step)).

   If first_special is true, the value in the first iteration is
     delta + mult * base

   If extend = UNKNOWN, first_special must be false, delta 0, mult 1 and value is
     subreg_{mode} (base + i * step)

   The get_iv_value function can be used to obtain these expressions.

   ??? Add a third mode field that would specify the mode in that inner
   computation is done, which would enable it to be different from the
   outer one?  */

struct rtx_iv
{
  /* Its base and step (mode of base and step is supposed to be extend_mode,
     see the description above).  */
  rtx base, step;

  /* The type of extend applied to it (SIGN_EXTEND, ZERO_EXTEND or UNKNOWN).  */
  enum rtx_code extend;

  /* Operations applied in the extended mode.  */
  rtx delta, mult;

  /* The mode it is extended to.  */
  enum machine_mode extend_mode;

  /* The mode the variable iterates in.  */
  enum machine_mode mode;

  /* Whether the first iteration needs to be handled specially.  */
  unsigned first_special : 1;
};

/* The description of an exit from the loop and of the number of iterations
   till we take the exit.  */

struct niter_desc
{
  /* The edge out of the loop.  */
  edge out_edge;

  /* The other edge leading from the condition.  */
  edge in_edge;

  /* True if we are able to say anything about number of iterations of the
     loop.  */
  bool simple_p;

  /* True if the loop iterates the constant number of times.  */
  bool const_iter;

  /* Number of iterations if constant.  */
  unsigned HOST_WIDEST_INT niter;

  /* Upper bound on the number of iterations.  */
  unsigned HOST_WIDEST_INT niter_max;

  /* Assumptions under that the rest of the information is valid.  */
  rtx assumptions;

  /* Assumptions under that the loop ends before reaching the latch,
     even if value of niter_expr says otherwise.  */
  rtx noloop_assumptions;

  /* Condition under that the loop is infinite.  */
  rtx infinite;

  /* Whether the comparison is signed.  */
  bool signed_p;

  /* The mode in that niter_expr should be computed.  */
  enum machine_mode mode;

  /* The number of iterations of the loop.  */
  rtx niter_expr;
};

extern void iv_analysis_loop_init (struct loop *);
extern bool iv_analyze (rtx, rtx, struct rtx_iv *);
extern bool iv_analyze_result (rtx, rtx, struct rtx_iv *);
extern bool iv_analyze_expr (rtx, rtx, enum machine_mode, struct rtx_iv *);
extern rtx get_iv_value (struct rtx_iv *, rtx);
extern bool biv_p (rtx, rtx);
extern void find_simple_exit (struct loop *, struct niter_desc *);
extern void iv_analysis_done (void);
extern struct df *iv_current_loop_df (void);

extern struct niter_desc *get_simple_loop_desc (struct loop *loop);
extern void free_simple_loop_desc (struct loop *loop);

static inline struct niter_desc *
simple_loop_desc (struct loop *loop)
{
  return (struct niter_desc *) loop->aux;
}

/* The properties of the target.  */

extern unsigned target_avail_regs;	/* Number of available registers.  */
extern unsigned target_res_regs;	/* Number of reserved registers.  */
extern unsigned target_small_cost;	/* The cost for register when there
					   is a free one.  */
extern unsigned target_pres_cost;	/* The cost for register when there are
					   not too many free ones.  */
extern unsigned target_spill_cost;	/* The cost for register when we need
					   to spill.  */

/* Register pressure estimation for induction variable optimizations & loop
   invariant motion.  */
extern unsigned global_cost_for_size (unsigned, unsigned, unsigned);
extern void init_set_costs (void);

/* Loop optimizer initialization.  */
extern struct loops *loop_optimizer_init (unsigned);
extern void loop_optimizer_finalize (struct loops *);

/* Optimization passes.  */
extern void unswitch_loops (struct loops *);

enum
{
  UAP_PEEL = 1,		/* Enables loop peeling.  */
  UAP_UNROLL = 2,	/* Enables peeling of loops if it seems profitable.  */
  UAP_UNROLL_ALL = 4	/* Enables peeling of all loops.  */
};

extern void unroll_and_peel_loops (struct loops *, int);
extern void doloop_optimize_loops (struct loops *);
extern void move_loop_invariants (struct loops *);
extern void record_estimate (struct loop *, tree, tree, tree);

#endif /* GCC_CFGLOOP_H */
