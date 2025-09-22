/* Convert a program in SSA form into Normal form.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Andrew Macleod <amacleod@redhat.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "ggc.h"
#include "langhooks.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "expr.h"
#include "function.h"
#include "diagnostic.h"
#include "bitmap.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-inline.h"
#include "varray.h"
#include "timevar.h"
#include "hashtab.h"
#include "tree-dump.h"
#include "tree-ssa-live.h"
#include "tree-pass.h"
#include "toplev.h"
#include "vecprim.h"

/* Flags to pass to remove_ssa_form.  */

#define SSANORM_PERFORM_TER		0x1
#define SSANORM_COMBINE_TEMPS		0x2
#define SSANORM_COALESCE_PARTITIONS	0x4

/* Used to hold all the components required to do SSA PHI elimination.
   The node and pred/succ list is a simple linear list of nodes and
   edges represented as pairs of nodes.

   The predecessor and successor list:  Nodes are entered in pairs, where
   [0] ->PRED, [1]->SUCC.  All the even indexes in the array represent 
   predecessors, all the odd elements are successors. 
   
   Rationale:
   When implemented as bitmaps, very large programs SSA->Normal times were 
   being dominated by clearing the interference graph.

   Typically this list of edges is extremely small since it only includes 
   PHI results and uses from a single edge which have not coalesced with 
   each other.  This means that no virtual PHI nodes are included, and
   empirical evidence suggests that the number of edges rarely exceed
   3, and in a bootstrap of GCC, the maximum size encountered was 7.
   This also limits the number of possible nodes that are involved to
   rarely more than 6, and in the bootstrap of gcc, the maximum number
   of nodes encountered was 12.  */
 
typedef struct _elim_graph {
  /* Size of the elimination vectors.  */
  int size;

  /* List of nodes in the elimination graph.  */
  VEC(tree,heap) *nodes;

  /*  The predecessor and successor edge list.  */
  VEC(int,heap) *edge_list;

  /* Visited vector.  */
  sbitmap visited;

  /* Stack for visited nodes.  */
  VEC(int,heap) *stack;
  
  /* The variable partition map.  */
  var_map map;

  /* Edge being eliminated by this graph.  */
  edge e;

  /* List of constant copies to emit.  These are pushed on in pairs.  */
  VEC(tree,heap) *const_copies;
} *elim_graph;


/* Local functions.  */
static tree create_temp (tree);
static void insert_copy_on_edge (edge, tree, tree);
static elim_graph new_elim_graph (int);
static inline void delete_elim_graph (elim_graph);
static inline void clear_elim_graph (elim_graph);
static inline int elim_graph_size (elim_graph);
static inline void elim_graph_add_node (elim_graph, tree);
static inline void elim_graph_add_edge (elim_graph, int, int);
static inline int elim_graph_remove_succ_edge (elim_graph, int);

static inline void eliminate_name (elim_graph, tree);
static void eliminate_build (elim_graph, basic_block);
static void elim_forward (elim_graph, int);
static int elim_unvisited_predecessor (elim_graph, int);
static void elim_backward (elim_graph, int);
static void elim_create (elim_graph, int);
static void eliminate_phi (edge, elim_graph);
static tree_live_info_p coalesce_ssa_name (var_map, int);
static void assign_vars (var_map);
static bool replace_use_variable (var_map, use_operand_p, tree *);
static bool replace_def_variable (var_map, def_operand_p, tree *);
static void eliminate_virtual_phis (void);
static void coalesce_abnormal_edges (var_map, conflict_graph, root_var_p);
static void print_exprs (FILE *, const char *, tree, const char *, tree,
			 const char *);
static void print_exprs_edge (FILE *, edge, const char *, tree, const char *,
			      tree);


/* Create a temporary variable based on the type of variable T.  Use T's name
   as the prefix.  */

static tree
create_temp (tree t)
{
  tree tmp;
  const char *name = NULL;
  tree type;

  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);

  gcc_assert (TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == PARM_DECL);

  type = TREE_TYPE (t);
  tmp = DECL_NAME (t);
  if (tmp)
    name = IDENTIFIER_POINTER (tmp);

  if (name == NULL)
    name = "temp";
  tmp = create_tmp_var (type, name);

  if (DECL_DEBUG_EXPR_IS_FROM (t) && DECL_DEBUG_EXPR (t))
    {
      SET_DECL_DEBUG_EXPR (tmp, DECL_DEBUG_EXPR (t));  
      DECL_DEBUG_EXPR_IS_FROM (tmp) = 1;
    }
  else if (!DECL_IGNORED_P (t))
    {
      SET_DECL_DEBUG_EXPR (tmp, t);
      DECL_DEBUG_EXPR_IS_FROM (tmp) = 1;
    }
  DECL_ARTIFICIAL (tmp) = DECL_ARTIFICIAL (t);
  DECL_IGNORED_P (tmp) = DECL_IGNORED_P (t);
  add_referenced_var (tmp);

  /* add_referenced_var will create the annotation and set up some
     of the flags in the annotation.  However, some flags we need to
     inherit from our original variable.  */
  var_ann (tmp)->symbol_mem_tag = var_ann (t)->symbol_mem_tag;
  if (is_call_clobbered (t))
    mark_call_clobbered (tmp, var_ann (t)->escape_mask);

  return tmp;
}


/* This helper function fill insert a copy from a constant or variable SRC to 
   variable DEST on edge E.  */

static void
insert_copy_on_edge (edge e, tree dest, tree src)
{
  tree copy;

  copy = build2 (MODIFY_EXPR, TREE_TYPE (dest), dest, src);
  set_is_used (dest);

  if (TREE_CODE (src) == ADDR_EXPR)
    src = TREE_OPERAND (src, 0);
  if (TREE_CODE (src) == VAR_DECL || TREE_CODE (src) == PARM_DECL)
    set_is_used (src);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file,
	       "Inserting a copy on edge BB%d->BB%d :",
	       e->src->index,
	       e->dest->index);
      print_generic_expr (dump_file, copy, dump_flags);
      fprintf (dump_file, "\n");
    }

  bsi_insert_on_edge (e, copy);
}


/* Create an elimination graph with SIZE nodes and associated data
   structures.  */

static elim_graph
new_elim_graph (int size)
{
  elim_graph g = (elim_graph) xmalloc (sizeof (struct _elim_graph));

  g->nodes = VEC_alloc (tree, heap, 30);
  g->const_copies = VEC_alloc (tree, heap, 20);
  g->edge_list = VEC_alloc (int, heap, 20);
  g->stack = VEC_alloc (int, heap, 30);
  
  g->visited = sbitmap_alloc (size);

  return g;
}


/* Empty elimination graph G.  */

static inline void
clear_elim_graph (elim_graph g)
{
  VEC_truncate (tree, g->nodes, 0);
  VEC_truncate (int, g->edge_list, 0);
}


/* Delete elimination graph G.  */

static inline void
delete_elim_graph (elim_graph g)
{
  sbitmap_free (g->visited);
  VEC_free (int, heap, g->stack);
  VEC_free (int, heap, g->edge_list);
  VEC_free (tree, heap, g->const_copies);
  VEC_free (tree, heap, g->nodes);
  free (g);
}


/* Return the number of nodes in graph G.  */

static inline int
elim_graph_size (elim_graph g)
{
  return VEC_length (tree, g->nodes);
}


/* Add NODE to graph G, if it doesn't exist already.  */

static inline void 
elim_graph_add_node (elim_graph g, tree node)
{
  int x;
  tree t;

  for (x = 0; VEC_iterate (tree, g->nodes, x, t); x++)
    if (t == node)
      return;
  VEC_safe_push (tree, heap, g->nodes, node);
}


/* Add the edge PRED->SUCC to graph G.  */

static inline void
elim_graph_add_edge (elim_graph g, int pred, int succ)
{
  VEC_safe_push (int, heap, g->edge_list, pred);
  VEC_safe_push (int, heap, g->edge_list, succ);
}


/* Remove an edge from graph G for which NODE is the predecessor, and
   return the successor node.  -1 is returned if there is no such edge.  */

static inline int
elim_graph_remove_succ_edge (elim_graph g, int node)
{
  int y;
  unsigned x;
  for (x = 0; x < VEC_length (int, g->edge_list); x += 2)
    if (VEC_index (int, g->edge_list, x) == node)
      {
        VEC_replace (int, g->edge_list, x, -1);
	y = VEC_index (int, g->edge_list, x + 1);
	VEC_replace (int, g->edge_list, x + 1, -1);
	return y;
      }
  return -1;
}


/* Find all the nodes in GRAPH which are successors to NODE in the
   edge list.  VAR will hold the partition number found.  CODE is the
   code fragment executed for every node found.  */

#define FOR_EACH_ELIM_GRAPH_SUCC(GRAPH, NODE, VAR, CODE)		\
do {									\
  unsigned x_;								\
  int y_;								\
  for (x_ = 0; x_ < VEC_length (int, (GRAPH)->edge_list); x_ += 2)	\
    {									\
      y_ = VEC_index (int, (GRAPH)->edge_list, x_);			\
      if (y_ != (NODE))							\
        continue;							\
      (VAR) = VEC_index (int, (GRAPH)->edge_list, x_ + 1);		\
      CODE;								\
    }									\
} while (0)


/* Find all the nodes which are predecessors of NODE in the edge list for
   GRAPH.  VAR will hold the partition number found.  CODE is the
   code fragment executed for every node found.  */

#define FOR_EACH_ELIM_GRAPH_PRED(GRAPH, NODE, VAR, CODE)		\
do {									\
  unsigned x_;								\
  int y_;								\
  for (x_ = 0; x_ < VEC_length (int, (GRAPH)->edge_list); x_ += 2)	\
    {									\
      y_ = VEC_index (int, (GRAPH)->edge_list, x_ + 1);			\
      if (y_ != (NODE))							\
        continue;							\
      (VAR) = VEC_index (int, (GRAPH)->edge_list, x_);			\
      CODE;								\
    }									\
} while (0)


/* Add T to elimination graph G.  */

static inline void
eliminate_name (elim_graph g, tree T)
{
  elim_graph_add_node (g, T);
}


/* Build elimination graph G for basic block BB on incoming PHI edge
   G->e.  */

static void
eliminate_build (elim_graph g, basic_block B)
{
  tree phi;
  tree T0, Ti;
  int p0, pi;

  clear_elim_graph (g);
  
  for (phi = phi_nodes (B); phi; phi = PHI_CHAIN (phi))
    {
      T0 = var_to_partition_to_var (g->map, PHI_RESULT (phi));
      
      /* Ignore results which are not in partitions.  */
      if (T0 == NULL_TREE)
	continue;

      Ti = PHI_ARG_DEF (phi, g->e->dest_idx);

      /* If this argument is a constant, or a SSA_NAME which is being
	 left in SSA form, just queue a copy to be emitted on this
	 edge.  */
      if (!phi_ssa_name_p (Ti)
	  || (TREE_CODE (Ti) == SSA_NAME
	      && var_to_partition (g->map, Ti) == NO_PARTITION))
        {
	  /* Save constant copies until all other copies have been emitted
	     on this edge.  */
	  VEC_safe_push (tree, heap, g->const_copies, T0);
	  VEC_safe_push (tree, heap, g->const_copies, Ti);
	}
      else
        {
	  Ti = var_to_partition_to_var (g->map, Ti);
	  if (T0 != Ti)
	    {
	      eliminate_name (g, T0);
	      eliminate_name (g, Ti);
	      p0 = var_to_partition (g->map, T0);
	      pi = var_to_partition (g->map, Ti);
	      elim_graph_add_edge (g, p0, pi);
	    }
	}
    }
}


/* Push successors of T onto the elimination stack for G.  */

static void 
elim_forward (elim_graph g, int T)
{
  int S;
  SET_BIT (g->visited, T);
  FOR_EACH_ELIM_GRAPH_SUCC (g, T, S,
    {
      if (!TEST_BIT (g->visited, S))
        elim_forward (g, S);
    });
  VEC_safe_push (int, heap, g->stack, T);
}


/* Return 1 if there unvisited predecessors of T in graph G.  */

static int
elim_unvisited_predecessor (elim_graph g, int T)
{
  int P;
  FOR_EACH_ELIM_GRAPH_PRED (g, T, P, 
    {
      if (!TEST_BIT (g->visited, P))
        return 1;
    });
  return 0;
}

/* Process predecessors first, and insert a copy.  */

static void
elim_backward (elim_graph g, int T)
{
  int P;
  SET_BIT (g->visited, T);
  FOR_EACH_ELIM_GRAPH_PRED (g, T, P, 
    {
      if (!TEST_BIT (g->visited, P))
        {
	  elim_backward (g, P);
	  insert_copy_on_edge (g->e, 
			       partition_to_var (g->map, P), 
			       partition_to_var (g->map, T));
	}
    });
}

/* Insert required copies for T in graph G.  Check for a strongly connected 
   region, and create a temporary to break the cycle if one is found.  */

static void 
elim_create (elim_graph g, int T)
{
  tree U;
  int P, S;

  if (elim_unvisited_predecessor (g, T))
    {
      U = create_temp (partition_to_var (g->map, T));
      insert_copy_on_edge (g->e, U, partition_to_var (g->map, T));
      FOR_EACH_ELIM_GRAPH_PRED (g, T, P, 
	{
	  if (!TEST_BIT (g->visited, P))
	    {
	      elim_backward (g, P);
	      insert_copy_on_edge (g->e, partition_to_var (g->map, P), U);
	    }
	});
    }
  else
    {
      S = elim_graph_remove_succ_edge (g, T);
      if (S != -1)
	{
	  SET_BIT (g->visited, T);
	  insert_copy_on_edge (g->e, 
			       partition_to_var (g->map, T), 
			       partition_to_var (g->map, S));
	}
    }
  
}

/* Eliminate all the phi nodes on edge E in graph G.  */

static void
eliminate_phi (edge e, elim_graph g)
{
  int x;
  basic_block B = e->dest;

  gcc_assert (VEC_length (tree, g->const_copies) == 0);

  /* Abnormal edges already have everything coalesced.  */
  if (e->flags & EDGE_ABNORMAL)
    return;

  g->e = e;

  eliminate_build (g, B);

  if (elim_graph_size (g) != 0)
    {
      tree var;

      sbitmap_zero (g->visited);
      VEC_truncate (int, g->stack, 0);

      for (x = 0; VEC_iterate (tree, g->nodes, x, var); x++)
        {
	  int p = var_to_partition (g->map, var);
	  if (!TEST_BIT (g->visited, p))
	    elim_forward (g, p);
	}
       
      sbitmap_zero (g->visited);
      while (VEC_length (int, g->stack) > 0)
	{
	  x = VEC_pop (int, g->stack);
	  if (!TEST_BIT (g->visited, x))
	    elim_create (g, x);
	}
    }

  /* If there are any pending constant copies, issue them now.  */
  while (VEC_length (tree, g->const_copies) > 0)
    {
      tree src, dest;
      src = VEC_pop (tree, g->const_copies);
      dest = VEC_pop (tree, g->const_copies);
      insert_copy_on_edge (e, dest, src);
    }
}


/* Shortcut routine to print messages to file F of the form:
   "STR1 EXPR1 STR2 EXPR2 STR3."  */

static void
print_exprs (FILE *f, const char *str1, tree expr1, const char *str2,
	     tree expr2, const char *str3)
{
  fprintf (f, "%s", str1);
  print_generic_expr (f, expr1, TDF_SLIM);
  fprintf (f, "%s", str2);
  print_generic_expr (f, expr2, TDF_SLIM);
  fprintf (f, "%s", str3);
}


/* Shortcut routine to print abnormal edge messages to file F of the form:
   "STR1 EXPR1 STR2 EXPR2 across edge E.  */

static void
print_exprs_edge (FILE *f, edge e, const char *str1, tree expr1, 
		  const char *str2, tree expr2)
{
  print_exprs (f, str1, expr1, str2, expr2, " across an abnormal edge");
  fprintf (f, " from BB%d->BB%d\n", e->src->index,
	       e->dest->index);
}


/* Coalesce partitions in MAP which are live across abnormal edges in GRAPH.
   RV is the root variable groupings of the partitions in MAP.  Since code 
   cannot be inserted on these edges, failure to coalesce something across
   an abnormal edge is an error.  */

static void
coalesce_abnormal_edges (var_map map, conflict_graph graph, root_var_p rv)
{
  basic_block bb;
  edge e;
  tree phi, var, tmp;
  int x, y, z;
  edge_iterator ei;

  /* Code cannot be inserted on abnormal edges. Look for all abnormal 
     edges, and coalesce any PHI results with their arguments across 
     that edge.  */

  FOR_EACH_BB (bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
      if (e->dest != EXIT_BLOCK_PTR && e->flags & EDGE_ABNORMAL)
	for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
	  {
	    /* Visit each PHI on the destination side of this abnormal
	       edge, and attempt to coalesce the argument with the result.  */
	    var = PHI_RESULT (phi);
	    x = var_to_partition (map, var);

	    /* Ignore results which are not relevant.  */
	    if (x == NO_PARTITION)
	      continue;

	    tmp = PHI_ARG_DEF (phi, e->dest_idx);
#ifdef ENABLE_CHECKING
	    if (!phi_ssa_name_p (tmp))
	      {
	        print_exprs_edge (stderr, e,
				  "\nConstant argument in PHI. Can't insert :",
				  var, " = ", tmp);
		internal_error ("SSA corruption");
	      }
#else
	    gcc_assert (phi_ssa_name_p (tmp));
#endif
	    y = var_to_partition (map, tmp);
	    gcc_assert (x != NO_PARTITION);
	    gcc_assert (y != NO_PARTITION);
#ifdef ENABLE_CHECKING
	    if (root_var_find (rv, x) != root_var_find (rv, y))
	      {
		print_exprs_edge (stderr, e, "\nDifferent root vars: ",
				  root_var (rv, root_var_find (rv, x)), 
				  " and ", 
				  root_var (rv, root_var_find (rv, y)));
		internal_error ("SSA corruption");
	      }
#else
	    gcc_assert (root_var_find (rv, x) == root_var_find (rv, y));
#endif

	    if (x != y)
	      {
#ifdef ENABLE_CHECKING
		if (conflict_graph_conflict_p (graph, x, y))
		  {
		    print_exprs_edge (stderr, e, "\n Conflict ", 
				      partition_to_var (map, x),
				      " and ", partition_to_var (map, y));
		    internal_error ("SSA corruption");
		  }
#else
		gcc_assert (!conflict_graph_conflict_p (graph, x, y));
#endif
		
		/* Now map the partitions back to their real variables.  */
		var = partition_to_var (map, x);
		tmp = partition_to_var (map, y);
		if (dump_file && (dump_flags & TDF_DETAILS))
		  {
		    print_exprs_edge (dump_file, e, 
				      "ABNORMAL: Coalescing ",
				      var, " and ", tmp);
		  }
	        z = var_union (map, var, tmp);
#ifdef ENABLE_CHECKING
		if (z == NO_PARTITION)
		  {
		    print_exprs_edge (stderr, e, "\nUnable to coalesce", 
				      partition_to_var (map, x), " and ", 
				      partition_to_var (map, y));
		    internal_error ("SSA corruption");
		  }
#else
		gcc_assert (z != NO_PARTITION);
#endif
		gcc_assert (z == x || z == y);
		if (z == x)
		  conflict_graph_merge_regs (graph, x, y);
		else
		  conflict_graph_merge_regs (graph, y, x);
	      }
	  }
}

/* Coalesce potential copies via PHI arguments.  */

static void
coalesce_phi_operands (var_map map, coalesce_list_p cl)
{
  basic_block bb;
  tree phi;

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree res = PHI_RESULT (phi);
	  int p = var_to_partition (map, res);
	  int x;

	  if (p == NO_PARTITION)
	    continue;

	  for (x = 0; x < PHI_NUM_ARGS (phi); x++)
	    {
	      tree arg = PHI_ARG_DEF (phi, x);
	      int p2;

	      if (TREE_CODE (arg) != SSA_NAME)
		continue;
	      if (SSA_NAME_VAR (res) != SSA_NAME_VAR (arg))
		continue;
	      p2 = var_to_partition (map, PHI_ARG_DEF (phi, x));
	      if (p2 != NO_PARTITION)
		{
		  edge e = PHI_ARG_EDGE (phi, x);
		  add_coalesce (cl, p, p2,
				coalesce_cost (EDGE_FREQUENCY (e),
					       maybe_hot_bb_p (bb),
					       EDGE_CRITICAL_P (e)));
		}
	    }
	}
    }
}

/* Coalesce all the result decls together.  */

static void
coalesce_result_decls (var_map map, coalesce_list_p cl)
{
  unsigned int i, x;
  tree var = NULL;

  for (i = x = 0; x < num_var_partitions (map); x++)
    {
      tree p = partition_to_var (map, x);
      if (TREE_CODE (SSA_NAME_VAR (p)) == RESULT_DECL)
	{
	  if (var == NULL_TREE)
	    {
	      var = p;
	      i = x;
	    }
	  else
	    add_coalesce (cl, i, x,
			  coalesce_cost (EXIT_BLOCK_PTR->frequency,
					 maybe_hot_bb_p (EXIT_BLOCK_PTR),
					 false));
	}
    }
}

/* Coalesce matching constraints in asms.  */

static void
coalesce_asm_operands (var_map map, coalesce_list_p cl)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree stmt = bsi_stmt (bsi);
	  unsigned long noutputs, i;
	  tree *outputs, link;

	  if (TREE_CODE (stmt) != ASM_EXPR)
	    continue;

	  noutputs = list_length (ASM_OUTPUTS (stmt));
	  outputs = (tree *) alloca (noutputs * sizeof (tree));
	  for (i = 0, link = ASM_OUTPUTS (stmt); link;
	       ++i, link = TREE_CHAIN (link))
	    outputs[i] = TREE_VALUE (link);

	  for (link = ASM_INPUTS (stmt); link; link = TREE_CHAIN (link))
	    {
	      const char *constraint
		= TREE_STRING_POINTER (TREE_VALUE (TREE_PURPOSE (link)));
	      tree input = TREE_VALUE (link);
	      char *end;
	      unsigned long match;
	      int p1, p2;

	      if (TREE_CODE (input) != SSA_NAME && !DECL_P (input))
		continue;

	      match = strtoul (constraint, &end, 10);
	      if (match >= noutputs || end == constraint)
		continue;

	      if (TREE_CODE (outputs[match]) != SSA_NAME
		  && !DECL_P (outputs[match]))
		continue;

	      p1 = var_to_partition (map, outputs[match]);
	      if (p1 == NO_PARTITION)
		continue;
	      p2 = var_to_partition (map, input);
	      if (p2 == NO_PARTITION)
		continue;

	      add_coalesce (cl, p1, p2, coalesce_cost (REG_BR_PROB_BASE,
						       maybe_hot_bb_p (bb),
						       false));
	    }
	}
    }
}

/* Reduce the number of live ranges in MAP.  Live range information is 
   returned if FLAGS indicates that we are combining temporaries, otherwise 
   NULL is returned.  The only partitions which are associated with actual 
   variables at this point are those which are forced to be coalesced for 
   various reason. (live on entry, live across abnormal edges, etc.).  */

static tree_live_info_p
coalesce_ssa_name (var_map map, int flags)
{
  unsigned num, x;
  sbitmap live;
  root_var_p rv;
  tree_live_info_p liveinfo;
  conflict_graph graph;
  coalesce_list_p cl = NULL;
  sbitmap_iterator sbi;

  if (num_var_partitions (map) <= 1)
    return NULL;

  liveinfo = calculate_live_on_entry (map);
  calculate_live_on_exit (liveinfo);
  rv = root_var_init (map);

  /* Remove single element variable from the list.  */
  root_var_compact (rv);

  cl = create_coalesce_list (map);

  coalesce_phi_operands (map, cl);
  coalesce_result_decls (map, cl);
  coalesce_asm_operands (map, cl);

  /* Build a conflict graph.  */
  graph = build_tree_conflict_graph (liveinfo, rv, cl);

  if (cl)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Before sorting:\n");
	  dump_coalesce_list (dump_file, cl);
	}

      sort_coalesce_list (cl);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "\nAfter sorting:\n");
	  dump_coalesce_list (dump_file, cl);
	}
    }

  /* Put the single element variables back in.  */
  root_var_decompact (rv);

  /* First, coalesce all live on entry variables to their root variable. 
     This will ensure the first use is coming from the correct location.  */

  num = num_var_partitions (map);
  live = sbitmap_alloc (num);
  sbitmap_zero (live);

  /* Set 'live' vector to indicate live on entry partitions.  */
  for (x = 0 ; x < num; x++)
    {
      tree var = partition_to_var (map, x);
      if (default_def (SSA_NAME_VAR (var)) == var)
	SET_BIT (live, x);
    }

  if ((flags & SSANORM_COMBINE_TEMPS) == 0)
    {
      delete_tree_live_info (liveinfo);
      liveinfo = NULL;
    }

  /* Assign root variable as partition representative for each live on entry
     partition.  */
  EXECUTE_IF_SET_IN_SBITMAP (live, 0, x, sbi)
    {
      tree var = root_var (rv, root_var_find (rv, x));
      var_ann_t ann = var_ann (var);
      /* If these aren't already coalesced...  */
      if (partition_to_var (map, x) != var)
	{
	  /* This root variable should have not already been assigned
	     to another partition which is not coalesced with this one.  */
	  gcc_assert (!ann->out_of_ssa_tag);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      print_exprs (dump_file, "Must coalesce ", 
			   partition_to_var (map, x),
			   " with the root variable ", var, ".\n");
	    }

	  change_partition_var (map, var, x);
	}
    }

  sbitmap_free (live);

  /* Coalesce partitions live across abnormal edges.  */
  coalesce_abnormal_edges (map, graph, rv);

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_var_map (dump_file, map);

  /* Coalesce partitions.  */
  coalesce_tpa_members (rv, graph, map, cl,
			((dump_flags & TDF_DETAILS) ? dump_file
			 : NULL));

  if (flags & SSANORM_COALESCE_PARTITIONS)
    coalesce_tpa_members (rv, graph, map, NULL,
			  ((dump_flags & TDF_DETAILS) ? dump_file
			   : NULL));
  if (cl)
    delete_coalesce_list (cl);
  root_var_delete (rv);
  conflict_graph_delete (graph);

  return liveinfo;
}


/* Take the ssa-name var_map MAP, and assign real variables to each 
   partition.  */

static void
assign_vars (var_map map)
{
  int x, i, num, rep;
  tree t, var;
  var_ann_t ann;
  root_var_p rv;

  rv = root_var_init (map);
  if (!rv) 
    return;

  /* Coalescing may already have forced some partitions to their root 
     variable. Find these and tag them.  */

  num = num_var_partitions (map);
  for (x = 0; x < num; x++)
    {
      var = partition_to_var (map, x);
      if (TREE_CODE (var) != SSA_NAME)
	{
	  /* Coalescing will already have verified that more than one
	     partition doesn't have the same root variable. Simply marked
	     the variable as assigned.  */
	  ann = var_ann (var);
	  ann->out_of_ssa_tag = 1;
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "partition %d has variable ", x);
	      print_generic_expr (dump_file, var, TDF_SLIM);
	      fprintf (dump_file, " assigned to it.\n");
	    }

	}
    }

  num = root_var_num (rv);
  for (x = 0; x < num; x++)
    {
      var = root_var (rv, x);
      ann = var_ann (var);
      for (i = root_var_first_partition (rv, x);
	   i != ROOT_VAR_NONE;
	   i = root_var_next_partition (rv, i))
	{
	  t = partition_to_var (map, i);

	  if (t == var || TREE_CODE (t) != SSA_NAME)
	    continue;

	  rep = var_to_partition (map, t);
	  
	  if (!ann->out_of_ssa_tag)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		print_exprs (dump_file, "", t, "  --> ", var, "\n");
	      change_partition_var (map, var, rep);
	      continue;
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    print_exprs (dump_file, "", t, " not coalesced with ", var, 
			 "");

	  var = create_temp (t);
	  change_partition_var (map, var, rep);
	  ann = var_ann (var);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, " -->  New temp:  '");
	      print_generic_expr (dump_file, var, TDF_SLIM);
	      fprintf (dump_file, "'\n");
	    }
	}
    }

  root_var_delete (rv);
}


/* Replace use operand P with whatever variable it has been rewritten to based 
   on the partitions in MAP.  EXPR is an optional expression vector over SSA 
   versions which is used to replace P with an expression instead of a variable.
   If the stmt is changed, return true.  */ 

static inline bool
replace_use_variable (var_map map, use_operand_p p, tree *expr)
{
  tree new_var;
  tree var = USE_FROM_PTR (p);

  /* Check if we are replacing this variable with an expression.  */
  if (expr)
    {
      int version = SSA_NAME_VERSION (var);
      if (expr[version])
        {
	  tree new_expr = TREE_OPERAND (expr[version], 1);
	  SET_USE (p, new_expr);
	  /* Clear the stmt's RHS, or GC might bite us.  */
	  TREE_OPERAND (expr[version], 1) = NULL_TREE;
	  return true;
	}
    }

  new_var = var_to_partition_to_var (map, var);
  if (new_var)
    {
      SET_USE (p, new_var);
      set_is_used (new_var);
      return true;
    }
  return false;
}


/* Replace def operand DEF_P with whatever variable it has been rewritten to 
   based on the partitions in MAP.  EXPR is an optional expression vector over
   SSA versions which is used to replace DEF_P with an expression instead of a 
   variable.  If the stmt is changed, return true.  */ 

static inline bool
replace_def_variable (var_map map, def_operand_p def_p, tree *expr)
{
  tree new_var;
  tree var = DEF_FROM_PTR (def_p);

  /* Check if we are replacing this variable with an expression.  */
  if (expr)
    {
      int version = SSA_NAME_VERSION (var);
      if (expr[version])
        {
	  tree new_expr = TREE_OPERAND (expr[version], 1);
	  SET_DEF (def_p, new_expr);
	  /* Clear the stmt's RHS, or GC might bite us.  */
	  TREE_OPERAND (expr[version], 1) = NULL_TREE;
	  return true;
	}
    }

  new_var = var_to_partition_to_var (map, var);
  if (new_var)
    {
      SET_DEF (def_p, new_var);
      set_is_used (new_var);
      return true;
    }
  return false;
}


/* Remove any PHI node which is a virtual PHI.  */

static void
eliminate_virtual_phis (void)
{
  basic_block bb;
  tree phi, next;

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = next)
        {
	  next = PHI_CHAIN (phi);
	  if (!is_gimple_reg (SSA_NAME_VAR (PHI_RESULT (phi))))
	    {
#ifdef ENABLE_CHECKING
	      int i;
	      /* There should be no arguments of this PHI which are in
		 the partition list, or we get incorrect results.  */
	      for (i = 0; i < PHI_NUM_ARGS (phi); i++)
	        {
		  tree arg = PHI_ARG_DEF (phi, i);
		  if (TREE_CODE (arg) == SSA_NAME 
		      && is_gimple_reg (SSA_NAME_VAR (arg)))
		    {
		      fprintf (stderr, "Argument of PHI is not virtual (");
		      print_generic_expr (stderr, arg, TDF_SLIM);
		      fprintf (stderr, "), but the result is :");
		      print_generic_stmt (stderr, phi, TDF_SLIM);
		      internal_error ("SSA corruption");
		    }
		}
#endif
	      remove_phi_node (phi, NULL_TREE);
	    }
	}
    }
}


/* This routine will coalesce variables in MAP of the same type which do not 
   interfere with each other. LIVEINFO is the live range info for variables
   of interest.  This will both reduce the memory footprint of the stack, and 
   allow us to coalesce together local copies of globals and scalarized 
   component refs.  */

static void
coalesce_vars (var_map map, tree_live_info_p liveinfo)
{
  basic_block bb;
  type_var_p tv;
  tree var;
  unsigned x, p, p2;
  coalesce_list_p cl;
  conflict_graph graph;

  cl = create_coalesce_list (map);

  /* Merge all the live on entry vectors for coalesced partitions.  */
  for (x = 0; x < num_var_partitions (map); x++)
    {
      var = partition_to_var (map, x);
      p = var_to_partition (map, var);
      if (p != x)
        live_merge_and_clear (liveinfo, p, x);
    }

  /* When PHI nodes are turned into copies, the result of each PHI node
     becomes live on entry to the block. Mark these now.  */
  FOR_EACH_BB (bb)
    {
      tree phi, arg;
      unsigned p;
      
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  p = var_to_partition (map, PHI_RESULT (phi));

	  /* Skip virtual PHI nodes.  */
	  if (p == (unsigned)NO_PARTITION)
	    continue;

	  make_live_on_entry (liveinfo, bb, p);

	  /* Each argument is a potential copy operation. Add any arguments 
	     which are not coalesced to the result to the coalesce list.  */
	  for (x = 0; x < (unsigned)PHI_NUM_ARGS (phi); x++)
	    {
	      arg = PHI_ARG_DEF (phi, x);
	      if (!phi_ssa_name_p (arg))
	        continue;
	      p2 = var_to_partition (map, arg);
	      if (p2 == (unsigned)NO_PARTITION)
		continue;
	      if (p != p2)
		{
		  edge e = PHI_ARG_EDGE (phi, x);

		  add_coalesce (cl, p, p2, 
				coalesce_cost (EDGE_FREQUENCY (e),
					       maybe_hot_bb_p (bb),
					       EDGE_CRITICAL_P (e)));
		}
	    }
	}
   }

  
  /* Re-calculate live on exit info.  */
  calculate_live_on_exit (liveinfo);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Live range info for variable memory coalescing.\n");
      dump_live_info (dump_file, liveinfo, LIVEDUMP_ALL);

      fprintf (dump_file, "Coalesce list from phi nodes:\n");
      dump_coalesce_list (dump_file, cl);
    }


  tv = type_var_init (map);
  if (dump_file)
    type_var_dump (dump_file, tv);
  type_var_compact (tv);
  if (dump_file)
    type_var_dump (dump_file, tv);

  graph = build_tree_conflict_graph (liveinfo, tv, cl);

  type_var_decompact (tv);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "type var list now looks like:n");
      type_var_dump (dump_file, tv);

      fprintf (dump_file, "Coalesce list after conflict graph build:\n");
      dump_coalesce_list (dump_file, cl);
    }

  sort_coalesce_list (cl);
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Coalesce list after sorting:\n");
      dump_coalesce_list (dump_file, cl);
    }

  coalesce_tpa_members (tv, graph, map, cl, 
			((dump_flags & TDF_DETAILS) ? dump_file : NULL));

  type_var_delete (tv);
  delete_coalesce_list (cl);
}


/* Temporary Expression Replacement (TER)

   Replace SSA version variables during out-of-ssa with their defining
   expression if there is only one use of the variable.

   A pass is made through the function, one block at a time.  No cross block
   information is tracked.

   Variables which only have one use, and whose defining stmt is considered
   a replaceable expression (see check_replaceable) are entered into 
   consideration by adding a list of dependent partitions to the version_info
   vector for that ssa_name_version.  This information comes from the partition
   mapping for each USE.  At the same time, the partition_dep_list vector for 
   these partitions have this version number entered into their lists.

   When the use of a replaceable ssa_variable is encountered, the dependence
   list in version_info[] is moved to the "pending_dependence" list in case
   the current expression is also replaceable. (To be determined later in 
   processing this stmt.) version_info[] for the version is then updated to 
   point to the defining stmt and the 'replaceable' bit is set.

   Any partition which is defined by a statement 'kills' any expression which
   is dependent on this partition.  Every ssa version in the partitions' 
   dependence list is removed from future consideration.

   All virtual references are lumped together.  Any expression which is
   dependent on any virtual variable (via a VUSE) has a dependence added
   to the special partition defined by VIRTUAL_PARTITION.

   Whenever a V_MAY_DEF is seen, all expressions dependent this 
   VIRTUAL_PARTITION are removed from consideration.

   At the end of a basic block, all expression are removed from consideration
   in preparation for the next block.  
   
   The end result is a vector over SSA_NAME_VERSION which is passed back to
   rewrite_out_of_ssa.  As the SSA variables are being rewritten, instead of
   replacing the SSA_NAME tree element with the partition it was assigned, 
   it is replaced with the RHS of the defining expression.  */


/* Dependency list element.  This can contain either a partition index or a
   version number, depending on which list it is in.  */

typedef struct value_expr_d 
{
  int value;
  struct value_expr_d *next;
} *value_expr_p;


/* Temporary Expression Replacement (TER) table information.  */

typedef struct temp_expr_table_d 
{
  var_map map;
  void **version_info;
  bitmap *expr_vars;
  value_expr_p *partition_dep_list;
  bitmap replaceable;
  bool saw_replaceable;
  int virtual_partition;
  bitmap partition_in_use;
  value_expr_p free_list;
  value_expr_p pending_dependence;
} *temp_expr_table_p;

/* Used to indicate a dependency on V_MAY_DEFs.  */
#define VIRTUAL_PARTITION(table)	(table->virtual_partition)

static temp_expr_table_p new_temp_expr_table (var_map);
static tree *free_temp_expr_table (temp_expr_table_p);
static inline value_expr_p new_value_expr (temp_expr_table_p);
static inline void free_value_expr (temp_expr_table_p, value_expr_p);
static inline value_expr_p find_value_in_list (value_expr_p, int, 
					       value_expr_p *);
static inline void add_value_to_list (temp_expr_table_p, value_expr_p *, int);
static inline void add_info_to_list (temp_expr_table_p, value_expr_p *, 
				     value_expr_p);
static value_expr_p remove_value_from_list (value_expr_p *, int);
static void add_dependence (temp_expr_table_p, int, tree);
static bool check_replaceable (temp_expr_table_p, tree);
static void finish_expr (temp_expr_table_p, int, bool);
static void mark_replaceable (temp_expr_table_p, tree);
static inline void kill_expr (temp_expr_table_p, int, bool);
static inline void kill_virtual_exprs (temp_expr_table_p, bool);
static void find_replaceable_in_bb (temp_expr_table_p, basic_block);
static tree *find_replaceable_exprs (var_map);
static void dump_replaceable_exprs (FILE *, tree *);


/* Create a new TER table for MAP.  */

static temp_expr_table_p
new_temp_expr_table (var_map map)
{
  temp_expr_table_p t;

  t = XNEW (struct temp_expr_table_d);
  t->map = map;

  t->version_info = XCNEWVEC (void *, num_ssa_names + 1);
  t->expr_vars = XCNEWVEC (bitmap, num_ssa_names + 1);
  t->partition_dep_list = XCNEWVEC (value_expr_p,
                                    num_var_partitions (map) + 1);

  t->replaceable = BITMAP_ALLOC (NULL);
  t->partition_in_use = BITMAP_ALLOC (NULL);

  t->saw_replaceable = false;
  t->virtual_partition = num_var_partitions (map);
  t->free_list = NULL;
  t->pending_dependence = NULL;

  return t;
}


/* Free TER table T.  If there are valid replacements, return the expression 
   vector.  */

static tree *
free_temp_expr_table (temp_expr_table_p t)
{
  value_expr_p p;
  tree *ret = NULL;
  unsigned i;

#ifdef ENABLE_CHECKING
  unsigned x;
  for (x = 0; x <= num_var_partitions (t->map); x++)
    gcc_assert (!t->partition_dep_list[x]);
#endif

  while ((p = t->free_list))
    {
      t->free_list = p->next;
      free (p);
    }

  BITMAP_FREE (t->partition_in_use);
  BITMAP_FREE (t->replaceable);

  for (i = 0; i <= num_ssa_names; i++)
    if (t->expr_vars[i])
      BITMAP_FREE (t->expr_vars[i]);
  free (t->expr_vars);

  free (t->partition_dep_list);
  if (t->saw_replaceable)
    ret = (tree *)t->version_info;
  else
    free (t->version_info);
  
  free (t);
  return ret;
}


/* Allocate a new value list node. Take it from the free list in TABLE if 
   possible.  */

static inline value_expr_p
new_value_expr (temp_expr_table_p table)
{
  value_expr_p p;
  if (table->free_list)
    {
      p = table->free_list;
      table->free_list = p->next;
    }
  else
    p = (value_expr_p) xmalloc (sizeof (struct value_expr_d));

  return p;
}


/* Add value list node P to the free list in TABLE.  */

static inline void
free_value_expr (temp_expr_table_p table, value_expr_p p)
{
  p->next = table->free_list;
  table->free_list = p;
}


/* Find VALUE if it's in LIST.  Return a pointer to the list object if found,  
   else return NULL.  If LAST_PTR is provided, it will point to the previous 
   item upon return, or NULL if this is the first item in the list.  */

static inline value_expr_p
find_value_in_list (value_expr_p list, int value, value_expr_p *last_ptr)
{
  value_expr_p curr;
  value_expr_p last = NULL;

  for (curr = list; curr; last = curr, curr = curr->next)
    {
      if (curr->value == value)
        break;
    }
  if (last_ptr)
    *last_ptr = last;
  return curr;
}


/* Add VALUE to LIST, if it isn't already present.  TAB is the expression 
   table */

static inline void
add_value_to_list (temp_expr_table_p tab, value_expr_p *list, int value)
{
  value_expr_p info;

  if (!find_value_in_list (*list, value, NULL))
    {
      info = new_value_expr (tab);
      info->value = value;
      info->next = *list;
      *list = info;
    }
}


/* Add value node INFO if it's value isn't already in LIST.  Free INFO if
   it is already in the list. TAB is the expression table.  */

static inline void
add_info_to_list (temp_expr_table_p tab, value_expr_p *list, value_expr_p info)
{
  if (find_value_in_list (*list, info->value, NULL))
    free_value_expr (tab, info);
  else
    {
      info->next = *list;
      *list = info;
    }
}


/* Look for VALUE in LIST.  If found, remove it from the list and return it's 
   pointer.  */

static value_expr_p
remove_value_from_list (value_expr_p *list, int value)
{
  value_expr_p info, last;

  info = find_value_in_list (*list, value, &last);
  if (!info)
    return NULL;
  if (!last)
    *list = info->next;
  else
    last->next = info->next;
 
  return info;
}


/* Add a dependency between the def of ssa VERSION and VAR.  If VAR is 
   replaceable by an expression, add a dependence each of the elements of the 
   expression.  These are contained in the pending list.  TAB is the
   expression table.  */

static void
add_dependence (temp_expr_table_p tab, int version, tree var)
{
  int i, x;
  value_expr_p info;

  i = SSA_NAME_VERSION (var);
  if (bitmap_bit_p (tab->replaceable, i))
    {
      /* This variable is being substituted, so use whatever dependences
         were queued up when we marked this as replaceable earlier.  */
      while ((info = tab->pending_dependence))
        {
	  tab->pending_dependence = info->next;
	  /* Get the partition this variable was dependent on. Reuse this
	     object to represent the current  expression instead.  */
	  x = info->value;
	  info->value = version;
	  add_info_to_list (tab, &(tab->partition_dep_list[x]), info);
          add_value_to_list (tab, 
			     (value_expr_p *)&(tab->version_info[version]), x);
	  bitmap_set_bit (tab->partition_in_use, x);
	}
    }
  else
    {
      i = var_to_partition (tab->map, var);
      gcc_assert (i != NO_PARTITION);
      add_value_to_list (tab, &(tab->partition_dep_list[i]), version);
      add_value_to_list (tab, 
			 (value_expr_p *)&(tab->version_info[version]), i);
      bitmap_set_bit (tab->partition_in_use, i);
    }
}


/* Check if expression STMT is suitable for replacement in table TAB.  If so, 
   create an expression entry.  Return true if this stmt is replaceable.  */

static bool
check_replaceable (temp_expr_table_p tab, tree stmt)
{
  tree var, def, basevar;
  int version;
  var_map map = tab->map;
  ssa_op_iter iter;
  tree call_expr;
  bitmap def_vars, use_vars;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;
  
  /* Punt if there is more than 1 def, or more than 1 use.  */
  def = SINGLE_SSA_TREE_OPERAND (stmt, SSA_OP_DEF);
  if (!def)
    return false;

  if (version_ref_count (map, def) != 1)
    return false;

  /* There must be no V_MAY_DEFS or V_MUST_DEFS.  */
  if (!(ZERO_SSA_OPERANDS (stmt, (SSA_OP_VMAYDEF | SSA_OP_VMUSTDEF))))
    return false;

  /* Float expressions must go through memory if float-store is on.  */
  if (flag_float_store && FLOAT_TYPE_P (TREE_TYPE (TREE_OPERAND (stmt, 1))))
    return false;

  /* An assignment with a register variable on the RHS is not
     replaceable.  */
  if (TREE_CODE (TREE_OPERAND (stmt, 1)) == VAR_DECL
     && DECL_HARD_REGISTER (TREE_OPERAND (stmt, 1)))
   return false;

  /* Calls to functions with side-effects cannot be replaced.  */
  if ((call_expr = get_call_expr_in (stmt)) != NULL_TREE)
    {
      int call_flags = call_expr_flags (call_expr);
      if (TREE_SIDE_EFFECTS (call_expr)
	  && !(call_flags & (ECF_PURE | ECF_CONST | ECF_NORETURN)))
	return false;
    }

  version = SSA_NAME_VERSION (def);
  basevar = SSA_NAME_VAR (def);
  def_vars = BITMAP_ALLOC (NULL);
  bitmap_set_bit (def_vars, DECL_UID (basevar));

  /* Add this expression to the dependency list for each use partition.  */
  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_USE)
    {
      add_dependence (tab, version, var);

      use_vars = tab->expr_vars[SSA_NAME_VERSION (var)];
      if (use_vars)
	bitmap_ior_into (def_vars, use_vars);
    }
  tab->expr_vars[version] = def_vars;

  /* If there are VUSES, add a dependence on virtual defs.  */
  if (!ZERO_SSA_OPERANDS (stmt, SSA_OP_VUSE))
    {
      add_value_to_list (tab, (value_expr_p *)&(tab->version_info[version]), 
			 VIRTUAL_PARTITION (tab));
      add_value_to_list (tab, 
			 &(tab->partition_dep_list[VIRTUAL_PARTITION (tab)]), 
			 version);
      bitmap_set_bit (tab->partition_in_use, VIRTUAL_PARTITION (tab));
    }

  return true;
}


/* This function will remove the expression for VERSION from replacement 
   consideration.n table TAB  If 'replace' is true, it is marked as 
   replaceable, otherwise not.  */

static void
finish_expr (temp_expr_table_p tab, int version, bool replace)
{
  value_expr_p info, tmp;
  int partition;

  /* Remove this expression from its dependent lists.  The partition dependence
     list is retained and transfered later to whomever uses this version.  */
  for (info = (value_expr_p) tab->version_info[version]; info; info = tmp)
    {
      partition = info->value;
      gcc_assert (tab->partition_dep_list[partition]);
      tmp = remove_value_from_list (&(tab->partition_dep_list[partition]), 
				    version);
      gcc_assert (tmp);
      free_value_expr (tab, tmp);
      /* Only clear the bit when the dependency list is emptied via 
         a replacement. Otherwise kill_expr will take care of it.  */
      if (!(tab->partition_dep_list[partition]) && replace)
        bitmap_clear_bit (tab->partition_in_use, partition);
      tmp = info->next;
      if (!replace)
        free_value_expr (tab, info);
    }

  if (replace)
    {
      tab->saw_replaceable = true;
      bitmap_set_bit (tab->replaceable, version);
    }
  else
    {
      gcc_assert (!bitmap_bit_p (tab->replaceable, version));
      tab->version_info[version] = NULL;
    }
}


/* Mark the expression associated with VAR as replaceable, and enter
   the defining stmt into the version_info table TAB.  */

static void
mark_replaceable (temp_expr_table_p tab, tree var)
{
  value_expr_p info;
  int version = SSA_NAME_VERSION (var);
  finish_expr (tab, version, true);

  /* Move the dependence list to the pending list.  */
  if (tab->version_info[version])
    {
      info = (value_expr_p) tab->version_info[version]; 
      for ( ; info->next; info = info->next)
	continue;
      info->next = tab->pending_dependence;
      tab->pending_dependence = (value_expr_p)tab->version_info[version];
    }

  tab->version_info[version] = SSA_NAME_DEF_STMT (var);
}


/* This function marks any expression in TAB which is dependent on PARTITION
   as NOT replaceable.  CLEAR_BIT is used to determine whether partition_in_use
   should have its bit cleared.  Since this routine can be called within an
   EXECUTE_IF_SET_IN_BITMAP, the bit can't always be cleared.  */

static inline void
kill_expr (temp_expr_table_p tab, int partition, bool clear_bit)
{
  value_expr_p ptr;

  /* Mark every active expr dependent on this var as not replaceable.  */
  while ((ptr = tab->partition_dep_list[partition]) != NULL)
    finish_expr (tab, ptr->value, false);

  if (clear_bit)
    bitmap_clear_bit (tab->partition_in_use, partition);
}


/* This function kills all expressions in TAB which are dependent on virtual 
   DEFs.  CLEAR_BIT determines whether partition_in_use gets cleared.  */

static inline void
kill_virtual_exprs (temp_expr_table_p tab, bool clear_bit)
{
  kill_expr (tab, VIRTUAL_PARTITION (tab), clear_bit);
}


/* This function processes basic block BB, and looks for variables which can
   be replaced by their expressions.  Results are stored in TAB.  */

static void
find_replaceable_in_bb (temp_expr_table_p tab, basic_block bb)
{
  block_stmt_iterator bsi;
  tree stmt, def, use;
  stmt_ann_t ann;
  int partition;
  var_map map = tab->map;
  value_expr_p p;
  ssa_op_iter iter;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      stmt = bsi_stmt (bsi);
      ann = stmt_ann (stmt);

      /* Determine if this stmt finishes an existing expression.  */
      FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE)
	{
	  unsigned ver = SSA_NAME_VERSION (use);

	  if (tab->version_info[ver])
	    {
	      bool same_root_var = false;
	      ssa_op_iter iter2;
	      bitmap vars = tab->expr_vars[ver];

	      /* See if the root variables are the same.  If they are, we
		 do not want to do the replacement to avoid problems with
		 code size, see PR tree-optimization/17549.  */
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter2, SSA_OP_DEF)
		{
		  if (bitmap_bit_p (vars, DECL_UID (SSA_NAME_VAR (def))))
		    {
		      same_root_var = true;
		      break;
		    }
		}

	      /* Mark expression as replaceable unless stmt is volatile
		 or DEF sets the same root variable as STMT.  */
	      if (!ann->has_volatile_ops && !same_root_var)
		mark_replaceable (tab, use);
	      else
		finish_expr (tab, ver, false);
	    }
	}
      
      /* Next, see if this stmt kills off an active expression.  */
      FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_DEF)
	{
	  partition = var_to_partition (map, def);
	  if (partition != NO_PARTITION && tab->partition_dep_list[partition])
	    kill_expr (tab, partition, true);
	}

      /* Now see if we are creating a new expression or not.  */
      if (!ann->has_volatile_ops)
	check_replaceable (tab, stmt);

      /* Free any unused dependency lists.  */
      while ((p = tab->pending_dependence))
	{
	  tab->pending_dependence = p->next;
	  free_value_expr (tab, p);
	}

      /* A V_{MAY,MUST}_DEF kills any expression using a virtual operand.  */
      if (!ZERO_SSA_OPERANDS (stmt, SSA_OP_VIRTUAL_DEFS))
        kill_virtual_exprs (tab, true);
    }
}


/* This function is the driver routine for replacement of temporary expressions
   in the SSA->normal phase, operating on MAP.  If there are replaceable 
   expressions, a table is returned which maps SSA versions to the 
   expressions they should be replaced with.  A NULL_TREE indicates no 
   replacement should take place.  If there are no replacements at all, 
   NULL is returned by the function, otherwise an expression vector indexed
   by SSA_NAME version numbers.  */

static tree *
find_replaceable_exprs (var_map map)
{
  basic_block bb;
  unsigned i;
  temp_expr_table_p table;
  tree *ret;

  table = new_temp_expr_table (map);
  FOR_EACH_BB (bb)
    {
      bitmap_iterator bi;

      find_replaceable_in_bb (table, bb);
      EXECUTE_IF_SET_IN_BITMAP ((table->partition_in_use), 0, i, bi)
        {
	  kill_expr (table, i, false);
	}
    }

  ret = free_temp_expr_table (table);
  return ret;
}


/* Dump TER expression table EXPR to file F.  */

static void
dump_replaceable_exprs (FILE *f, tree *expr)
{
  tree stmt, var;
  int x;
  fprintf (f, "\nReplacing Expressions\n");
  for (x = 0; x < (int)num_ssa_names + 1; x++)
    if (expr[x])
      {
        stmt = expr[x];
	var = SINGLE_SSA_TREE_OPERAND (stmt, SSA_OP_DEF);
	gcc_assert (var != NULL_TREE);
	print_generic_expr (f, var, TDF_SLIM);
	fprintf (f, " replace with --> ");
	print_generic_expr (f, TREE_OPERAND (stmt, 1), TDF_SLIM);
	fprintf (f, "\n");
      }
  fprintf (f, "\n");
}


/* This function will rewrite the current program using the variable mapping
   found in MAP.  If the replacement vector VALUES is provided, any 
   occurrences of partitions with non-null entries in the vector will be 
   replaced with the expression in the vector instead of its mapped 
   variable.  */

static void
rewrite_trees (var_map map, tree *values)
{
  elim_graph g;
  basic_block bb;
  block_stmt_iterator si;
  edge e;
  tree phi;
  bool changed;
 
#ifdef ENABLE_CHECKING
  /* Search for PHIs where the destination has no partition, but one
     or more arguments has a partition.  This should not happen and can
     create incorrect code.  */
  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree T0 = var_to_partition_to_var (map, PHI_RESULT (phi));
      
	  if (T0 == NULL_TREE)
	    {
	      int i;

	      for (i = 0; i < PHI_NUM_ARGS (phi); i++)
		{
		  tree arg = PHI_ARG_DEF (phi, i);

		  if (TREE_CODE (arg) == SSA_NAME
		      && var_to_partition (map, arg) != NO_PARTITION)
		    {
		      fprintf (stderr, "Argument of PHI is in a partition :(");
		      print_generic_expr (stderr, arg, TDF_SLIM);
		      fprintf (stderr, "), but the result is not :");
		      print_generic_stmt (stderr, phi, TDF_SLIM);
		      internal_error ("SSA corruption");
		    }
		}
	    }
	}
    }
#endif

  /* Replace PHI nodes with any required copies.  */
  g = new_elim_graph (map->num_partitions);
  g->map = map;
  FOR_EACH_BB (bb)
    {
      for (si = bsi_start (bb); !bsi_end_p (si); )
	{
	  tree stmt = bsi_stmt (si);
	  use_operand_p use_p, copy_use_p;
	  def_operand_p def_p;
	  bool remove = false, is_copy = false;
	  int num_uses = 0;
	  stmt_ann_t ann;
	  ssa_op_iter iter;

	  ann = stmt_ann (stmt);
	  changed = false;

	  if (TREE_CODE (stmt) == MODIFY_EXPR 
	      && (TREE_CODE (TREE_OPERAND (stmt, 1)) == SSA_NAME))
	    is_copy = true;

	  copy_use_p = NULL_USE_OPERAND_P;
	  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_USE)
	    {
	      if (replace_use_variable (map, use_p, values))
		changed = true;
	      copy_use_p = use_p;
	      num_uses++;
	    }

	  if (num_uses != 1)
	    is_copy = false;

	  def_p = SINGLE_SSA_DEF_OPERAND (stmt, SSA_OP_DEF);

	  if (def_p != NULL)
	    {
	      /* Mark this stmt for removal if it is the list of replaceable 
		 expressions.  */
	      if (values && values[SSA_NAME_VERSION (DEF_FROM_PTR (def_p))])
		remove = true;
	      else
		{
		  if (replace_def_variable (map, def_p, NULL))
		    changed = true;
		  /* If both SSA_NAMEs coalesce to the same variable,
		     mark the now redundant copy for removal.  */
		  if (is_copy)
		    {
		      gcc_assert (copy_use_p != NULL_USE_OPERAND_P);
		      if (DEF_FROM_PTR (def_p) == USE_FROM_PTR (copy_use_p))
			remove = true;
		    }
		}
	    }
	  else
	    FOR_EACH_SSA_DEF_OPERAND (def_p, stmt, iter, SSA_OP_DEF)
	      if (replace_def_variable (map, def_p, NULL))
		changed = true;

	  /* Remove any stmts marked for removal.  */
	  if (remove)
	    bsi_remove (&si, true);
	  else
	    bsi_next (&si);
	}

      phi = phi_nodes (bb);
      if (phi)
        {
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    eliminate_phi (e, g);
	}
    }

  delete_elim_graph (g);
}


DEF_VEC_ALLOC_P(edge,heap);

/* These are the local work structures used to determine the best place to 
   insert the copies that were placed on edges by the SSA->normal pass..  */
static VEC(edge,heap) *edge_leader;
static VEC(tree,heap) *stmt_list;
static bitmap leader_has_match = NULL;
static edge leader_match = NULL;


/* Pass this function to make_forwarder_block so that all the edges with
   matching PENDING_STMT lists to 'curr_stmt_list' get redirected.  */
static bool 
same_stmt_list_p (edge e)
{
  return (e->aux == (PTR) leader_match) ? true : false;
}


/* Return TRUE if S1 and S2 are equivalent copies.  */
static inline bool
identical_copies_p (tree s1, tree s2)
{
#ifdef ENABLE_CHECKING
  gcc_assert (TREE_CODE (s1) == MODIFY_EXPR);
  gcc_assert (TREE_CODE (s2) == MODIFY_EXPR);
  gcc_assert (DECL_P (TREE_OPERAND (s1, 0)));
  gcc_assert (DECL_P (TREE_OPERAND (s2, 0)));
#endif

  if (TREE_OPERAND (s1, 0) != TREE_OPERAND (s2, 0))
    return false;

  s1 = TREE_OPERAND (s1, 1);
  s2 = TREE_OPERAND (s2, 1);

  if (s1 != s2)
    return false;

  return true;
}


/* Compare the PENDING_STMT list for two edges, and return true if the lists
   contain the same sequence of copies.  */

static inline bool 
identical_stmt_lists_p (edge e1, edge e2)
{
  tree t1 = PENDING_STMT (e1);
  tree t2 = PENDING_STMT (e2);
  tree_stmt_iterator tsi1, tsi2;

  gcc_assert (TREE_CODE (t1) == STATEMENT_LIST);
  gcc_assert (TREE_CODE (t2) == STATEMENT_LIST);

  for (tsi1 = tsi_start (t1), tsi2 = tsi_start (t2);
       !tsi_end_p (tsi1) && !tsi_end_p (tsi2); 
       tsi_next (&tsi1), tsi_next (&tsi2))
    {
      if (!identical_copies_p (tsi_stmt (tsi1), tsi_stmt (tsi2)))
        break;
    }

  if (!tsi_end_p (tsi1) || ! tsi_end_p (tsi2))
    return false;

  return true;
}


/* Allocate data structures used in analyze_edges_for_bb.   */

static void
init_analyze_edges_for_bb (void)
{
  edge_leader = VEC_alloc (edge, heap, 25);
  stmt_list = VEC_alloc (tree, heap, 25);
  leader_has_match = BITMAP_ALLOC (NULL);
}


/* Free data structures used in analyze_edges_for_bb.   */

static void
fini_analyze_edges_for_bb (void)
{
  VEC_free (edge, heap, edge_leader);
  VEC_free (tree, heap, stmt_list);
  BITMAP_FREE (leader_has_match);
}


/* Look at all the incoming edges to block BB, and decide where the best place
   to insert the stmts on each edge are, and perform those insertions.  */

static void
analyze_edges_for_bb (basic_block bb)
{
  edge e;
  edge_iterator ei;
  int count;
  unsigned int x;
  bool have_opportunity;
  block_stmt_iterator bsi;
  tree stmt;
  edge single_edge = NULL;
  bool is_label;
  edge leader;

  count = 0;

  /* Blocks which contain at least one abnormal edge cannot use 
     make_forwarder_block.  Look for these blocks, and commit any PENDING_STMTs
     found on edges in these block.  */
  have_opportunity = true;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->flags & EDGE_ABNORMAL)
      {
        have_opportunity = false;
	break;
      }

  if (!have_opportunity)
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	if (PENDING_STMT (e))
	  bsi_commit_one_edge_insert (e, NULL);
      return;
    }
  /* Find out how many edges there are with interesting pending stmts on them.  
     Commit the stmts on edges we are not interested in.  */
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (PENDING_STMT (e))
        {
	  gcc_assert (!(e->flags & EDGE_ABNORMAL));
	  if (e->flags & EDGE_FALLTHRU)
	    {
	      bsi = bsi_start (e->src);
	      if (!bsi_end_p (bsi))
	        {
		  stmt = bsi_stmt (bsi);
		  bsi_next (&bsi);
		  gcc_assert (stmt != NULL_TREE);
		  is_label = (TREE_CODE (stmt) == LABEL_EXPR);
		  /* Punt if it has non-label stmts, or isn't local.  */
		  if (!is_label || DECL_NONLOCAL (TREE_OPERAND (stmt, 0)) 
		      || !bsi_end_p (bsi))
		    {
		      bsi_commit_one_edge_insert (e, NULL);
		      continue;
		    }
		}
	    }
	  single_edge = e;
	  count++;
	}
    }

  /* If there aren't at least 2 edges, no sharing will happen.  */
  if (count < 2)
    {
      if (single_edge)
        bsi_commit_one_edge_insert (single_edge, NULL);
      return;
    }

  /* Ensure that we have empty worklists.  */
#ifdef ENABLE_CHECKING
  gcc_assert (VEC_length (edge, edge_leader) == 0);
  gcc_assert (VEC_length (tree, stmt_list) == 0);
  gcc_assert (bitmap_empty_p (leader_has_match));
#endif

  /* Find the "leader" block for each set of unique stmt lists.  Preference is
     given to FALLTHRU blocks since they would need a GOTO to arrive at another
     block.  The leader edge destination is the block which all the other edges
     with the same stmt list will be redirected to.  */
  have_opportunity = false;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (PENDING_STMT (e))
	{
	  bool found = false;

	  /* Look for the same stmt list in edge leaders list.  */
	  for (x = 0; VEC_iterate (edge, edge_leader, x, leader); x++)
	    {
	      if (identical_stmt_lists_p (leader, e))
		{
		  /* Give this edge the same stmt list pointer.  */
		  PENDING_STMT (e) = NULL;
		  e->aux = leader;
		  bitmap_set_bit (leader_has_match, x);
		  have_opportunity = found = true;
		  break;
		}
	    }

	  /* If no similar stmt list, add this edge to the leader list.  */
	  if (!found)
	    {
	      VEC_safe_push (edge, heap, edge_leader, e);
	      VEC_safe_push (tree, heap, stmt_list, PENDING_STMT (e));
	    }
	}
     }

  /* If there are no similar lists, just issue the stmts.  */
  if (!have_opportunity)
    {
      for (x = 0; VEC_iterate (edge, edge_leader, x, leader); x++)
	bsi_commit_one_edge_insert (leader, NULL);
      VEC_truncate (edge, edge_leader, 0);
      VEC_truncate (tree, stmt_list, 0);
      bitmap_clear (leader_has_match);
      return;
    }


  if (dump_file)
    fprintf (dump_file, "\nOpportunities in BB %d for stmt/block reduction:\n",
	     bb->index);

  
  /* For each common list, create a forwarding block and issue the stmt's
     in that block.  */
  for (x = 0; VEC_iterate (edge, edge_leader, x, leader); x++)
    if (bitmap_bit_p (leader_has_match, x))
      {
	edge new_edge;
	block_stmt_iterator bsi;
	tree curr_stmt_list;

	leader_match = leader;

	/* The tree_* cfg manipulation routines use the PENDING_EDGE field
	   for various PHI manipulations, so it gets cleared when calls are 
	   made to make_forwarder_block(). So make sure the edge is clear, 
	   and use the saved stmt list.  */
	PENDING_STMT (leader) = NULL;
	leader->aux = leader;
	curr_stmt_list = VEC_index (tree, stmt_list, x);

        new_edge = make_forwarder_block (leader->dest, same_stmt_list_p, 
					 NULL);
	bb = new_edge->dest;
	if (dump_file)
	  {
	    fprintf (dump_file, "Splitting BB %d for Common stmt list.  ", 
		     leader->dest->index);
	    fprintf (dump_file, "Original block is now BB%d.\n", bb->index);
	    print_generic_stmt (dump_file, curr_stmt_list, TDF_VOPS);
	  }

	FOR_EACH_EDGE (e, ei, new_edge->src->preds)
	  {
	    e->aux = NULL;
	    if (dump_file)
	      fprintf (dump_file, "  Edge (%d->%d) lands here.\n", 
		       e->src->index, e->dest->index);
	  }

	bsi = bsi_last (leader->dest);
	bsi_insert_after (&bsi, curr_stmt_list, BSI_NEW_STMT);

	leader_match = NULL;
	/* We should never get a new block now.  */
      }
    else
      {
	PENDING_STMT (leader) = VEC_index (tree, stmt_list, x);
	bsi_commit_one_edge_insert (leader, NULL);
      }

   
  /* Clear the working data structures.  */
  VEC_truncate (edge, edge_leader, 0);
  VEC_truncate (tree, stmt_list, 0);
  bitmap_clear (leader_has_match);
}


/* This function will analyze the insertions which were performed on edges,
   and decide whether they should be left on that edge, or whether it is more
   efficient to emit some subset of them in a single block.  All stmts are
   inserted somewhere.  */

static void
perform_edge_inserts (void)
{
  basic_block bb;

  if (dump_file)
    fprintf(dump_file, "Analyzing Edge Insertions.\n");

  /* analyze_edges_for_bb calls make_forwarder_block, which tries to
     incrementally update the dominator information.  Since we don't
     need dominator information after this pass, go ahead and free the
     dominator information.  */
  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);

  /* Allocate data structures used in analyze_edges_for_bb.   */
  init_analyze_edges_for_bb ();

  FOR_EACH_BB (bb)
    analyze_edges_for_bb (bb);

  analyze_edges_for_bb (EXIT_BLOCK_PTR);

  /* Free data structures used in analyze_edges_for_bb.   */
  fini_analyze_edges_for_bb ();

#ifdef ENABLE_CHECKING
  {
    edge_iterator ei;
    edge e;
    FOR_EACH_BB (bb)
      {
	FOR_EACH_EDGE (e, ei, bb->preds)
	  {
	    if (PENDING_STMT (e))
	      error (" Pending stmts not issued on PRED edge (%d, %d)\n", 
		     e->src->index, e->dest->index);
	  }
	FOR_EACH_EDGE (e, ei, bb->succs)
	  {
	    if (PENDING_STMT (e))
	      error (" Pending stmts not issued on SUCC edge (%d, %d)\n", 
		     e->src->index, e->dest->index);
	  }
      }
    FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
      {
	if (PENDING_STMT (e))
	  error (" Pending stmts not issued on ENTRY edge (%d, %d)\n", 
		 e->src->index, e->dest->index);
      }
    FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
      {
	if (PENDING_STMT (e))
	  error (" Pending stmts not issued on EXIT edge (%d, %d)\n", 
		 e->src->index, e->dest->index);
      }
  }
#endif
}


/* Remove the variables specified in MAP from SSA form.  FLAGS indicate what
   options should be used.  */

static void
remove_ssa_form (var_map map, int flags)
{
  tree_live_info_p liveinfo;
  basic_block bb;
  tree phi, next;
  tree *values = NULL;

  /* If we are not combining temps, don't calculate live ranges for variables
     with only one SSA version.  */
  if ((flags & SSANORM_COMBINE_TEMPS) == 0)
    compact_var_map (map, VARMAP_NO_SINGLE_DEFS);
  else
    compact_var_map (map, VARMAP_NORMAL);

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_var_map (dump_file, map);

  liveinfo = coalesce_ssa_name (map, flags);

  /* Make sure even single occurrence variables are in the list now.  */
  if ((flags & SSANORM_COMBINE_TEMPS) == 0)
    compact_var_map (map, VARMAP_NORMAL);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "After Coalescing:\n");
      dump_var_map (dump_file, map);
    }

  if (flags & SSANORM_PERFORM_TER)
    {
      values = find_replaceable_exprs (map);
      if (values && dump_file && (dump_flags & TDF_DETAILS))
	dump_replaceable_exprs (dump_file, values);
    }

  /* Assign real variables to the partitions now.  */
  assign_vars (map);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "After Root variable replacement:\n");
      dump_var_map (dump_file, map);
    }

  if ((flags & SSANORM_COMBINE_TEMPS) && liveinfo)
    {
      coalesce_vars (map, liveinfo);
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "After variable memory coalescing:\n");
	  dump_var_map (dump_file, map);
	}
    }
  
  if (liveinfo)
    delete_tree_live_info (liveinfo);

  rewrite_trees (map, values);

  if (values)
    free (values);

  /* Remove phi nodes which have been translated back to real variables.  */
  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = next)
	{
	  next = PHI_CHAIN (phi);
	  remove_phi_node (phi, NULL_TREE);
	}
    }

  /* we no longer maintain the SSA operand cache at this point.  */
  fini_ssa_operands ();

  /* If any copies were inserted on edges, analyze and insert them now.  */
  perform_edge_inserts ();
}

/* Search every PHI node for arguments associated with backedges which
   we can trivially determine will need a copy (the argument is either
   not an SSA_NAME or the argument has a different underlying variable
   than the PHI result).

   Insert a copy from the PHI argument to a new destination at the
   end of the block with the backedge to the top of the loop.  Update
   the PHI argument to reference this new destination.  */

static void
insert_backedge_copies (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  tree result = PHI_RESULT (phi);
	  tree result_var;
	  int i;

	  if (!is_gimple_reg (result))
	    continue;

	  result_var = SSA_NAME_VAR (result);
	  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
	    {
	      tree arg = PHI_ARG_DEF (phi, i);
	      edge e = PHI_ARG_EDGE (phi, i);

	      /* If the argument is not an SSA_NAME, then we will
		 need a constant initialization.  If the argument is
		 an SSA_NAME with a different underlying variable and
		 we are not combining temporaries, then we will
		 need a copy statement.  */
	      if ((e->flags & EDGE_DFS_BACK)
		  && (TREE_CODE (arg) != SSA_NAME
		      || (!flag_tree_combine_temps
			  && SSA_NAME_VAR (arg) != result_var)))
		{
		  tree stmt, name, last = NULL;
		  block_stmt_iterator bsi;

		  bsi = bsi_last (PHI_ARG_EDGE (phi, i)->src);
		  if (!bsi_end_p (bsi))
		    last = bsi_stmt (bsi);

		  /* In theory the only way we ought to get back to the
		     start of a loop should be with a COND_EXPR or GOTO_EXPR.
		     However, better safe than sorry. 

		     If the block ends with a control statement or
		     something that might throw, then we have to
		     insert this assignment before the last
		     statement.  Else insert it after the last statement.  */
		  if (last && stmt_ends_bb_p (last))
		    {
		      /* If the last statement in the block is the definition
			 site of the PHI argument, then we can't insert
			 anything after it.  */
		      if (TREE_CODE (arg) == SSA_NAME
			  && SSA_NAME_DEF_STMT (arg) == last)
			continue;
		    }

		  /* Create a new instance of the underlying
		     variable of the PHI result.  */
		  stmt = build2 (MODIFY_EXPR, TREE_TYPE (result_var),
				 NULL_TREE, PHI_ARG_DEF (phi, i));
		  name = make_ssa_name (result_var, stmt);
		  TREE_OPERAND (stmt, 0) = name;

		  /* Insert the new statement into the block and update
		     the PHI node.  */
		  if (last && stmt_ends_bb_p (last))
		    bsi_insert_before (&bsi, stmt, BSI_NEW_STMT);
		  else
		    bsi_insert_after (&bsi, stmt, BSI_NEW_STMT);
		  SET_PHI_ARG_DEF (phi, i, name);
		}
	    }
	}
    }
}

/* Take the current function out of SSA form, as described in
   R. Morgan, ``Building an Optimizing Compiler'',
   Butterworth-Heinemann, Boston, MA, 1998. pp 176-186.  */

static unsigned int
rewrite_out_of_ssa (void)
{
  var_map map;
  int var_flags = 0;
  int ssa_flags = 0;

  /* If elimination of a PHI requires inserting a copy on a backedge,
     then we will have to split the backedge which has numerous
     undesirable performance effects.

     A significant number of such cases can be handled here by inserting
     copies into the loop itself.  */
  insert_backedge_copies ();

  if (!flag_tree_live_range_split)
    ssa_flags |= SSANORM_COALESCE_PARTITIONS;
    
  eliminate_virtual_phis ();

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_tree_cfg (dump_file, dump_flags & ~TDF_DETAILS);

  /* We cannot allow unssa to un-gimplify trees before we instrument them.  */
  if (flag_tree_ter && !flag_mudflap)
    var_flags = SSA_VAR_MAP_REF_COUNT;

  map = create_ssa_var_map (var_flags);

  if (flag_tree_combine_temps)
    ssa_flags |= SSANORM_COMBINE_TEMPS;
  if (flag_tree_ter && !flag_mudflap)
    ssa_flags |= SSANORM_PERFORM_TER;

  remove_ssa_form (map, ssa_flags);

  if (dump_file && (dump_flags & TDF_DETAILS))
    dump_tree_cfg (dump_file, dump_flags & ~TDF_DETAILS);

  /* Flush out flow graph and SSA data.  */
  delete_var_map (map);

  in_ssa_p = false;
  return 0;
}


/* Define the parameters of the out of SSA pass.  */

struct tree_opt_pass pass_del_ssa = 
{
  "optimized",				/* name */
  NULL,					/* gate */
  rewrite_out_of_ssa,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_SSA_TO_NORMAL,		/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  /* ??? If TER is enabled, we also kill gimple.  */
  PROP_ssa,				/* properties_destroyed */
  TODO_verify_ssa | TODO_verify_flow
    | TODO_verify_stmts,		/* todo_flags_start */
  TODO_dump_func
  | TODO_ggc_collect
  | TODO_remove_unused_locals,		/* todo_flags_finish */
  0					/* letter */
};
