/* Register conflict graph computation routines.
   Copyright (C) 2000, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC

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

/* References:

   Building an Optimizing Compiler
   Robert Morgan
   Butterworth-Heinemann, 1998 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "obstack.h"
#include "hashtab.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "basic-block.h"

/* A register conflict graph is an undirected graph containing nodes
   for some or all of the regs used in a function.  Arcs represent
   conflicts, i.e. two nodes are connected by an arc if there is a
   point in the function at which the regs corresponding to the two
   nodes are both live.

   The conflict graph is represented by the data structures described
   in Morgan section 11.3.1.  Nodes are not stored explicitly; only
   arcs are.  An arc stores the numbers of the regs it connects.

   Arcs can be located by two methods:

     - The two reg numbers for each arc are hashed into a single
       value, and the arc is placed in a hash table according to this
       value.  This permits quick determination of whether a specific
       conflict is present in the graph.  

     - Additionally, the arc data structures are threaded by a set of
       linked lists by single reg number.  Since each arc references
       two regs, there are two next pointers, one for the
       smaller-numbered reg and one for the larger-numbered reg.  This
       permits the quick enumeration of conflicts for a single
       register.

   Arcs are allocated from an obstack.  */

/* An arc in a conflict graph.  */

struct conflict_graph_arc_def
{
  /* The next element of the list of conflicts involving the
     smaller-numbered reg, as an index in the table of arcs of this
     graph.   Contains NULL if this is the tail.  */
  struct conflict_graph_arc_def *smaller_next;

  /* The next element of the list of conflicts involving the
     larger-numbered reg, as an index in the table of arcs of this
     graph.  Contains NULL if this is the tail.  */
  struct conflict_graph_arc_def *larger_next;

  /* The smaller-numbered reg involved in this conflict.  */
  int smaller;

  /* The larger-numbered reg involved in this conflict.  */
  int larger;
};

typedef struct conflict_graph_arc_def *conflict_graph_arc;
typedef const struct conflict_graph_arc_def *const_conflict_graph_arc;


/* A conflict graph.  */

struct conflict_graph_def
{
  /* A hash table of arcs.  Used to search for a specific conflict.  */
  htab_t arc_hash_table;

  /* The number of regs this conflict graph handles.  */
  int num_regs;

  /* For each reg, the arc at the head of a list that threads through
     all the arcs involving that reg.  An entry is NULL if no
     conflicts exist involving that reg.  */
  conflict_graph_arc *neighbor_heads;

  /* Arcs are allocated from here.  */
  struct obstack arc_obstack;
};

/* The initial capacity (number of conflict arcs) for newly-created
   conflict graphs.  */
#define INITIAL_ARC_CAPACITY 64


/* Computes the hash value of the conflict graph arc connecting regs
   R1 and R2.  R1 is assumed to be smaller or equal to R2.  */
#define CONFLICT_HASH_FN(R1, R2) ((R2) * ((R2) - 1) / 2 + (R1))

static hashval_t arc_hash (const void *);
static int arc_eq (const void *, const void *);
static int print_conflict (int, int, void *);

/* Callback function to compute the hash value of an arc.  Uses
   current_graph to locate the graph to which the arc belongs.  */

static hashval_t
arc_hash (const void *arcp)
{
  const_conflict_graph_arc arc = (const_conflict_graph_arc) arcp;

  return CONFLICT_HASH_FN (arc->smaller, arc->larger);
}

/* Callback function to determine the equality of two arcs in the hash
   table.  */

static int
arc_eq (const void *arcp1, const void *arcp2)
{
  const_conflict_graph_arc arc1 = (const_conflict_graph_arc) arcp1;
  const_conflict_graph_arc arc2 = (const_conflict_graph_arc) arcp2;

  return arc1->smaller == arc2->smaller && arc1->larger == arc2->larger;
}

/* Creates an empty conflict graph to hold conflicts among NUM_REGS
   registers.  */

conflict_graph
conflict_graph_new (int num_regs)
{
  conflict_graph graph = XNEW (struct conflict_graph_def);
  graph->num_regs = num_regs;

  /* Set up the hash table.  No delete action is specified; memory
     management of arcs is through the obstack.  */
  graph->arc_hash_table
    = htab_create (INITIAL_ARC_CAPACITY, &arc_hash, &arc_eq, NULL);

  /* Create an obstack for allocating arcs.  */
  obstack_init (&graph->arc_obstack);
	     
  /* Create and zero the lookup table by register number.  */
  graph->neighbor_heads = XCNEWVEC (conflict_graph_arc, num_regs);

  return graph;
}

/* Deletes a conflict graph.  */

void
conflict_graph_delete (conflict_graph graph)
{
  obstack_free (&graph->arc_obstack, NULL);
  htab_delete (graph->arc_hash_table);
  free (graph->neighbor_heads);
  free (graph);
}

/* Adds a conflict to GRAPH between regs REG1 and REG2, which must be
   distinct.  Returns nonzero, unless the conflict is already present
   in GRAPH, in which case it does nothing and returns zero.  */

int
conflict_graph_add (conflict_graph graph, int reg1, int reg2)
{
  int smaller = MIN (reg1, reg2);
  int larger = MAX (reg1, reg2);
  struct conflict_graph_arc_def dummy;
  conflict_graph_arc arc;
  void **slot;

  /* A reg cannot conflict with itself.  */
  gcc_assert (reg1 != reg2);

  dummy.smaller = smaller;
  dummy.larger = larger;
  slot = htab_find_slot (graph->arc_hash_table, (void *) &dummy, INSERT);
  
  /* If the conflict is already there, do nothing.  */
  if (*slot != NULL)
    return 0;

  /* Allocate an arc.  */
  arc
    = obstack_alloc (&graph->arc_obstack,
		     sizeof (struct conflict_graph_arc_def));
  
  /* Record the reg numbers.  */
  arc->smaller = smaller;
  arc->larger = larger;

  /* Link the conflict into two lists, one for each reg.  */
  arc->smaller_next = graph->neighbor_heads[smaller];
  graph->neighbor_heads[smaller] = arc;
  arc->larger_next = graph->neighbor_heads[larger];
  graph->neighbor_heads[larger] = arc;

  /* Put it in the hash table.  */
  *slot = (void *) arc;

  return 1;
}

/* Returns nonzero if a conflict exists in GRAPH between regs REG1
   and REG2.  */

int
conflict_graph_conflict_p (conflict_graph graph, int reg1, int reg2)
{
  /* Build an arc to search for.  */
  struct conflict_graph_arc_def arc;
  arc.smaller = MIN (reg1, reg2);
  arc.larger = MAX (reg1, reg2);

  return htab_find (graph->arc_hash_table, (void *) &arc) != NULL;
}

/* Calls ENUM_FN for each conflict in GRAPH involving REG.  EXTRA is
   passed back to ENUM_FN.  */

void
conflict_graph_enum (conflict_graph graph, int reg,
		     conflict_graph_enum_fn enum_fn, void *extra)
{
  conflict_graph_arc arc = graph->neighbor_heads[reg];
  while (arc != NULL)
    {
      /* Invoke the callback.  */
      if ((*enum_fn) (arc->smaller, arc->larger, extra))
	/* Stop if requested.  */
	break;
      
      /* Which next pointer to follow depends on whether REG is the
	 smaller or larger reg in this conflict.  */
      if (reg < arc->larger)
	arc = arc->smaller_next;
      else
	arc = arc->larger_next;
    }
}

/* For each conflict between a register x and SRC in GRAPH, adds a
   conflict to GRAPH between x and TARGET.  */

void
conflict_graph_merge_regs (conflict_graph graph, int target, int src)
{
  conflict_graph_arc arc = graph->neighbor_heads[src];

  if (target == src)
    return;

  while (arc != NULL)
    {
      int other = arc->smaller;

      if (other == src)
	other = arc->larger;

      conflict_graph_add (graph, target, other);

      /* Which next pointer to follow depends on whether REG is the
	 smaller or larger reg in this conflict.  */
      if (src < arc->larger)
	arc = arc->smaller_next;
      else
	arc = arc->larger_next;
    }
}

/* Holds context information while a conflict graph is being traversed
   for printing.  */

struct print_context
{
  /* The file pointer to which we're printing.  */
  FILE *fp;

  /* The reg whose conflicts we're printing.  */
  int reg;

  /* Whether a conflict has already been printed for this reg.  */
  int started;
};

/* Callback function when enumerating conflicts during printing.  */

static int
print_conflict (int reg1, int reg2, void *contextp)
{
  struct print_context *context = (struct print_context *) contextp;
  int reg;

  /* If this is the first conflict printed for this reg, start a new
     line.  */
  if (! context->started)
    {
      fprintf (context->fp, " %d:", context->reg);
      context->started = 1;
    }

  /* Figure out the reg whose conflicts we're printing.  The other reg
     is the interesting one.  */
  if (reg1 == context->reg)
    reg = reg2;
  else
    {
      gcc_assert (reg2 == context->reg);
      reg = reg1;
    }

  /* Print the conflict.  */
  fprintf (context->fp, " %d", reg);

  /* Continue enumerating.  */
  return 0;
}

/* Prints the conflicts in GRAPH to FP.  */

void
conflict_graph_print (conflict_graph graph, FILE *fp)
{
  int reg;
  struct print_context context;

  context.fp = fp;
  fprintf (fp, "Conflicts:\n");

  /* Loop over registers supported in this graph.  */
  for (reg = 0; reg < graph->num_regs; ++reg)
    {
      context.reg = reg;
      context.started = 0;

      /* Scan the conflicts for reg, printing as we go.  A label for
	 this line will be printed the first time a conflict is
	 printed for the reg; we won't start a new line if this reg
	 has no conflicts.  */
      conflict_graph_enum (graph, reg, &print_conflict, &context);

      /* If this reg does have conflicts, end the line.  */
      if (context.started)
	fputc ('\n', fp);
    }
}
