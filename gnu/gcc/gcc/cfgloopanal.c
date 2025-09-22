/* Natural loop analysis code for GNU compiler.
   Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"
#include "cfgloop.h"
#include "expr.h"
#include "output.h"

/* Checks whether BB is executed exactly once in each LOOP iteration.  */

bool
just_once_each_iteration_p (const struct loop *loop, basic_block bb)
{
  /* It must be executed at least once each iteration.  */
  if (!dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
    return false;

  /* And just once.  */
  if (bb->loop_father != loop)
    return false;

  /* But this was not enough.  We might have some irreducible loop here.  */
  if (bb->flags & BB_IRREDUCIBLE_LOOP)
    return false;

  return true;
}

/* Structure representing edge of a graph.  */

struct edge
{
  int src, dest;	/* Source and destination.  */
  struct edge *pred_next, *succ_next;
			/* Next edge in predecessor and successor lists.  */
  void *data;		/* Data attached to the edge.  */
};

/* Structure representing vertex of a graph.  */

struct vertex
{
  struct edge *pred, *succ;
			/* Lists of predecessors and successors.  */
  int component;	/* Number of dfs restarts before reaching the
			   vertex.  */
  int post;		/* Postorder number.  */
};

/* Structure representing a graph.  */

struct graph
{
  int n_vertices;	/* Number of vertices.  */
  struct vertex *vertices;
			/* The vertices.  */
};

/* Dumps graph G into F.  */

extern void dump_graph (FILE *, struct graph *);

void
dump_graph (FILE *f, struct graph *g)
{
  int i;
  struct edge *e;

  for (i = 0; i < g->n_vertices; i++)
    {
      if (!g->vertices[i].pred
	  && !g->vertices[i].succ)
	continue;

      fprintf (f, "%d (%d)\t<-", i, g->vertices[i].component);
      for (e = g->vertices[i].pred; e; e = e->pred_next)
	fprintf (f, " %d", e->src);
      fprintf (f, "\n");

      fprintf (f, "\t->");
      for (e = g->vertices[i].succ; e; e = e->succ_next)
	fprintf (f, " %d", e->dest);
      fprintf (f, "\n");
    }
}

/* Creates a new graph with N_VERTICES vertices.  */

static struct graph *
new_graph (int n_vertices)
{
  struct graph *g = XNEW (struct graph);

  g->n_vertices = n_vertices;
  g->vertices = XCNEWVEC (struct vertex, n_vertices);

  return g;
}

/* Adds an edge from F to T to graph G, with DATA attached.  */

static void
add_edge (struct graph *g, int f, int t, void *data)
{
  struct edge *e = xmalloc (sizeof (struct edge));

  e->src = f;
  e->dest = t;
  e->data = data;

  e->pred_next = g->vertices[t].pred;
  g->vertices[t].pred = e;

  e->succ_next = g->vertices[f].succ;
  g->vertices[f].succ = e;
}

/* Runs dfs search over vertices of G, from NQ vertices in queue QS.
   The vertices in postorder are stored into QT.  If FORWARD is false,
   backward dfs is run.  */

static void
dfs (struct graph *g, int *qs, int nq, int *qt, bool forward)
{
  int i, tick = 0, v, comp = 0, top;
  struct edge *e;
  struct edge **stack = xmalloc (sizeof (struct edge *) * g->n_vertices);

  for (i = 0; i < g->n_vertices; i++)
    {
      g->vertices[i].component = -1;
      g->vertices[i].post = -1;
    }

#define FST_EDGE(V) (forward ? g->vertices[(V)].succ : g->vertices[(V)].pred)
#define NEXT_EDGE(E) (forward ? (E)->succ_next : (E)->pred_next)
#define EDGE_SRC(E) (forward ? (E)->src : (E)->dest)
#define EDGE_DEST(E) (forward ? (E)->dest : (E)->src)

  for (i = 0; i < nq; i++)
    {
      v = qs[i];
      if (g->vertices[v].post != -1)
	continue;

      g->vertices[v].component = comp++;
      e = FST_EDGE (v);
      top = 0;

      while (1)
	{
	  while (e && g->vertices[EDGE_DEST (e)].component != -1)
	    e = NEXT_EDGE (e);

	  if (!e)
	    {
	      if (qt)
		qt[tick] = v;
	      g->vertices[v].post = tick++;

	      if (!top)
		break;

	      e = stack[--top];
	      v = EDGE_SRC (e);
	      e = NEXT_EDGE (e);
	      continue;
	    }

	  stack[top++] = e;
	  v = EDGE_DEST (e);
	  e = FST_EDGE (v);
	  g->vertices[v].component = comp - 1;
	}
    }

  free (stack);
}

/* Marks the edge E in graph G irreducible if it connects two vertices in the
   same scc.  */

static void
check_irred (struct graph *g, struct edge *e)
{
  edge real = e->data;

  /* All edges should lead from a component with higher number to the
     one with lower one.  */
  gcc_assert (g->vertices[e->src].component >= g->vertices[e->dest].component);

  if (g->vertices[e->src].component != g->vertices[e->dest].component)
    return;

  real->flags |= EDGE_IRREDUCIBLE_LOOP;
  if (flow_bb_inside_loop_p (real->src->loop_father, real->dest))
    real->src->flags |= BB_IRREDUCIBLE_LOOP;
}

/* Runs CALLBACK for all edges in G.  */

static void
for_each_edge (struct graph *g,
	       void (callback) (struct graph *, struct edge *))
{
  struct edge *e;
  int i;

  for (i = 0; i < g->n_vertices; i++)
    for (e = g->vertices[i].succ; e; e = e->succ_next)
      callback (g, e);
}

/* Releases the memory occupied by G.  */

static void
free_graph (struct graph *g)
{
  struct edge *e, *n;
  int i;

  for (i = 0; i < g->n_vertices; i++)
    for (e = g->vertices[i].succ; e; e = n)
      {
	n = e->succ_next;
	free (e);
      }
  free (g->vertices);
  free (g);
}

/* Marks blocks and edges that are part of non-recognized loops; i.e. we
   throw away all latch edges and mark blocks inside any remaining cycle.
   Everything is a bit complicated due to fact we do not want to do this
   for parts of cycles that only "pass" through some loop -- i.e. for
   each cycle, we want to mark blocks that belong directly to innermost
   loop containing the whole cycle.

   LOOPS is the loop tree.  */

#define LOOP_REPR(LOOP) ((LOOP)->num + last_basic_block)
#define BB_REPR(BB) ((BB)->index + 1)

void
mark_irreducible_loops (struct loops *loops)
{
  basic_block act;
  edge e;
  edge_iterator ei;
  int i, src, dest;
  struct graph *g;
  int *queue1 = XNEWVEC (int, last_basic_block + loops->num);
  int *queue2 = XNEWVEC (int, last_basic_block + loops->num);
  int nq, depth;
  struct loop *cloop;

  /* Reset the flags.  */
  FOR_BB_BETWEEN (act, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      act->flags &= ~BB_IRREDUCIBLE_LOOP;
      FOR_EACH_EDGE (e, ei, act->succs)
	e->flags &= ~EDGE_IRREDUCIBLE_LOOP;
    }

  /* Create the edge lists.  */
  g = new_graph (last_basic_block + loops->num);

  FOR_BB_BETWEEN (act, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    FOR_EACH_EDGE (e, ei, act->succs)
      {
	/* Ignore edges to exit.  */
	if (e->dest == EXIT_BLOCK_PTR)
	  continue;

	/* And latch edges.  */
	if (e->dest->loop_father->header == e->dest
	    && e->dest->loop_father->latch == act)
	  continue;

	/* Edges inside a single loop should be left where they are.  Edges
	   to subloop headers should lead to representative of the subloop,
	   but from the same place.

	   Edges exiting loops should lead from representative
	   of the son of nearest common ancestor of the loops in that
	   act lays.  */

	src = BB_REPR (act);
	dest = BB_REPR (e->dest);

	if (e->dest->loop_father->header == e->dest)
	  dest = LOOP_REPR (e->dest->loop_father);

	if (!flow_bb_inside_loop_p (act->loop_father, e->dest))
	  {
	    depth = find_common_loop (act->loop_father,
				      e->dest->loop_father)->depth + 1;
	    if (depth == act->loop_father->depth)
	      cloop = act->loop_father;
	    else
	      cloop = act->loop_father->pred[depth];

	    src = LOOP_REPR (cloop);
	  }

	add_edge (g, src, dest, e);
      }

  /* Find the strongly connected components.  Use the algorithm of Tarjan --
     first determine the postorder dfs numbering in reversed graph, then
     run the dfs on the original graph in the order given by decreasing
     numbers assigned by the previous pass.  */
  nq = 0;
  FOR_BB_BETWEEN (act, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      queue1[nq++] = BB_REPR (act);
    }
  for (i = 1; i < (int) loops->num; i++)
    if (loops->parray[i])
      queue1[nq++] = LOOP_REPR (loops->parray[i]);
  dfs (g, queue1, nq, queue2, false);
  for (i = 0; i < nq; i++)
    queue1[i] = queue2[nq - i - 1];
  dfs (g, queue1, nq, NULL, true);

  /* Mark the irreducible loops.  */
  for_each_edge (g, check_irred);

  free_graph (g);
  free (queue1);
  free (queue2);

  loops->state |= LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS;
}

/* Counts number of insns inside LOOP.  */
int
num_loop_insns (struct loop *loop)
{
  basic_block *bbs, bb;
  unsigned i, ninsns = 0;
  rtx insn;

  bbs = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = bbs[i];
      ninsns++;
      for (insn = BB_HEAD (bb); insn != BB_END (bb); insn = NEXT_INSN (insn))
	if (INSN_P (insn))
	  ninsns++;
    }
  free(bbs);

  return ninsns;
}

/* Counts number of insns executed on average per iteration LOOP.  */
int
average_num_loop_insns (struct loop *loop)
{
  basic_block *bbs, bb;
  unsigned i, binsns, ninsns, ratio;
  rtx insn;

  ninsns = 0;
  bbs = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = bbs[i];

      binsns = 1;
      for (insn = BB_HEAD (bb); insn != BB_END (bb); insn = NEXT_INSN (insn))
	if (INSN_P (insn))
	  binsns++;

      ratio = loop->header->frequency == 0
	      ? BB_FREQ_MAX
	      : (bb->frequency * BB_FREQ_MAX) / loop->header->frequency;
      ninsns += binsns * ratio;
    }
  free(bbs);

  ninsns /= BB_FREQ_MAX;
  if (!ninsns)
    ninsns = 1; /* To avoid division by zero.  */

  return ninsns;
}

/* Returns expected number of LOOP iterations.
   Compute upper bound on number of iterations in case they do not fit integer
   to help loop peeling heuristics.  Use exact counts if at all possible.  */
unsigned
expected_loop_iterations (const struct loop *loop)
{
  edge e;
  edge_iterator ei;

  if (loop->latch->count || loop->header->count)
    {
      gcov_type count_in, count_latch, expected;

      count_in = 0;
      count_latch = 0;

      FOR_EACH_EDGE (e, ei, loop->header->preds)
	if (e->src == loop->latch)
	  count_latch = e->count;
	else
	  count_in += e->count;

      if (count_in == 0)
	expected = count_latch * 2;
      else
	expected = (count_latch + count_in - 1) / count_in;

      /* Avoid overflows.  */
      return (expected > REG_BR_PROB_BASE ? REG_BR_PROB_BASE : expected);
    }
  else
    {
      int freq_in, freq_latch;

      freq_in = 0;
      freq_latch = 0;

      FOR_EACH_EDGE (e, ei, loop->header->preds)
	if (e->src == loop->latch)
	  freq_latch = EDGE_FREQUENCY (e);
	else
	  freq_in += EDGE_FREQUENCY (e);

      if (freq_in == 0)
	return freq_latch * 2;

      return (freq_latch + freq_in - 1) / freq_in;
    }
}

/* Returns the maximum level of nesting of subloops of LOOP.  */

unsigned
get_loop_level (const struct loop *loop)
{
  const struct loop *ploop;
  unsigned mx = 0, l;

  for (ploop = loop->inner; ploop; ploop = ploop->next)
    {
      l = get_loop_level (ploop);
      if (l >= mx)
	mx = l + 1;
    }
  return mx;
}

/* Returns estimate on cost of computing SEQ.  */

static unsigned
seq_cost (rtx seq)
{
  unsigned cost = 0;
  rtx set;

  for (; seq; seq = NEXT_INSN (seq))
    {
      set = single_set (seq);
      if (set)
	cost += rtx_cost (set, SET);
      else
	cost++;
    }

  return cost;
}

/* The properties of the target.  */

unsigned target_avail_regs;	/* Number of available registers.  */
unsigned target_res_regs;	/* Number of reserved registers.  */
unsigned target_small_cost;	/* The cost for register when there is a free one.  */
unsigned target_pres_cost;	/* The cost for register when there are not too many
				   free ones.  */
unsigned target_spill_cost;	/* The cost for register when we need to spill.  */

/* Initialize the constants for computing set costs.  */

void
init_set_costs (void)
{
  rtx seq;
  rtx reg1 = gen_raw_REG (SImode, FIRST_PSEUDO_REGISTER);
  rtx reg2 = gen_raw_REG (SImode, FIRST_PSEUDO_REGISTER + 1);
  rtx addr = gen_raw_REG (Pmode, FIRST_PSEUDO_REGISTER + 2);
  rtx mem = validize_mem (gen_rtx_MEM (SImode, addr));
  unsigned i;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    if (TEST_HARD_REG_BIT (reg_class_contents[GENERAL_REGS], i)
	&& !fixed_regs[i])
      target_avail_regs++;

  target_res_regs = 3;

  /* These are really just heuristic values.  */

  start_sequence ();
  emit_move_insn (reg1, reg2);
  seq = get_insns ();
  end_sequence ();
  target_small_cost = seq_cost (seq);
  target_pres_cost = 2 * target_small_cost;

  start_sequence ();
  emit_move_insn (mem, reg1);
  emit_move_insn (reg2, mem);
  seq = get_insns ();
  end_sequence ();
  target_spill_cost = seq_cost (seq);
}

/* Calculates cost for having SIZE new loop global variables.  REGS_USED is the
   number of global registers used in loop.  N_USES is the number of relevant
   variable uses.  */

unsigned
global_cost_for_size (unsigned size, unsigned regs_used, unsigned n_uses)
{
  unsigned regs_needed = regs_used + size;
  unsigned cost = 0;

  if (regs_needed + target_res_regs <= target_avail_regs)
    cost += target_small_cost * size;
  else if (regs_needed <= target_avail_regs)
    cost += target_pres_cost * size;
  else
    {
      cost += target_pres_cost * size;
      cost += target_spill_cost * n_uses * (regs_needed - target_avail_regs) / regs_needed;
    }

  return cost;
}

/* Sets EDGE_LOOP_EXIT flag for all exits of LOOPS.  */

void
mark_loop_exit_edges (struct loops *loops)
{
  basic_block bb;
  edge e;

  if (loops->num <= 1)
    return;

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (bb->loop_father->outer
	      && loop_exit_edge_p (bb->loop_father, e))
	    e->flags |= EDGE_LOOP_EXIT;
	  else
	    e->flags &= ~EDGE_LOOP_EXIT;
	}
    }
}

