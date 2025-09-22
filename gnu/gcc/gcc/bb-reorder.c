/* Basic block reordering routines for the GNU compiler.
   Copyright (C) 2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This (greedy) algorithm constructs traces in several rounds.
   The construction starts from "seeds".  The seed for the first round
   is the entry point of function.  When there are more than one seed
   that one is selected first that has the lowest key in the heap
   (see function bb_to_key).  Then the algorithm repeatedly adds the most
   probable successor to the end of a trace.  Finally it connects the traces.

   There are two parameters: Branch Threshold and Exec Threshold.
   If the edge to a successor of the actual basic block is lower than
   Branch Threshold or the frequency of the successor is lower than
   Exec Threshold the successor will be the seed in one of the next rounds.
   Each round has these parameters lower than the previous one.
   The last round has to have these parameters set to zero
   so that the remaining blocks are picked up.

   The algorithm selects the most probable successor from all unvisited
   successors and successors that have been added to this trace.
   The other successors (that has not been "sent" to the next round) will be
   other seeds for this round and the secondary traces will start in them.
   If the successor has not been visited in this trace it is added to the trace
   (however, there is some heuristic for simple branches).
   If the successor has been visited in this trace the loop has been found.
   If the loop has many iterations the loop is rotated so that the
   source block of the most probable edge going out from the loop
   is the last block of the trace.
   If the loop has few iterations and there is no edge from the last block of
   the loop going out from loop the loop header is duplicated.
   Finally, the construction of the trace is terminated.

   When connecting traces it first checks whether there is an edge from the
   last block of one trace to the first block of another trace.
   When there are still some unconnected traces it checks whether there exists
   a basic block BB such that BB is a successor of the last bb of one trace
   and BB is a predecessor of the first block of another trace. In this case,
   BB is duplicated and the traces are connected through this duplicate.
   The rest of traces are simply connected so there will be a jump to the
   beginning of the rest of trace.


   References:

   "Software Trace Cache"
   A. Ramirez, J. Larriba-Pey, C. Navarro, J. Torrellas and M. Valero; 1999
   http://citeseer.nj.nec.com/15361.html

*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "flags.h"
#include "timevar.h"
#include "output.h"
#include "cfglayout.h"
#include "fibheap.h"
#include "target.h"
#include "function.h"
#include "tm_p.h"
#include "obstack.h"
#include "expr.h"
#include "params.h"
#include "toplev.h"
#include "tree-pass.h"

#ifndef HAVE_conditional_execution
#define HAVE_conditional_execution 0
#endif

/* The number of rounds.  In most cases there will only be 4 rounds, but
   when partitioning hot and cold basic blocks into separate sections of
   the .o file there will be an extra round.*/
#define N_ROUNDS 5

/* Stubs in case we don't have a return insn.
   We have to check at runtime too, not only compiletime.  */

#ifndef HAVE_return
#define HAVE_return 0
#define gen_return() NULL_RTX
#endif


/* Branch thresholds in thousandths (per mille) of the REG_BR_PROB_BASE.  */
static int branch_threshold[N_ROUNDS] = {400, 200, 100, 0, 0};

/* Exec thresholds in thousandths (per mille) of the frequency of bb 0.  */
static int exec_threshold[N_ROUNDS] = {500, 200, 50, 0, 0};

/* If edge frequency is lower than DUPLICATION_THRESHOLD per mille of entry
   block the edge destination is not duplicated while connecting traces.  */
#define DUPLICATION_THRESHOLD 100

/* Length of unconditional jump instruction.  */
static int uncond_jump_length;

/* Structure to hold needed information for each basic block.  */
typedef struct bbro_basic_block_data_def
{
  /* Which trace is the bb start of (-1 means it is not a start of a trace).  */
  int start_of_trace;

  /* Which trace is the bb end of (-1 means it is not an end of a trace).  */
  int end_of_trace;

  /* Which trace is the bb in?  */
  int in_trace;

  /* Which heap is BB in (if any)?  */
  fibheap_t heap;

  /* Which heap node is BB in (if any)?  */
  fibnode_t node;
} bbro_basic_block_data;

/* The current size of the following dynamic array.  */
static int array_size;

/* The array which holds needed information for basic blocks.  */
static bbro_basic_block_data *bbd;

/* To avoid frequent reallocation the size of arrays is greater than needed,
   the number of elements is (not less than) 1.25 * size_wanted.  */
#define GET_ARRAY_SIZE(X) ((((X) / 4) + 1) * 5)

/* Free the memory and set the pointer to NULL.  */
#define FREE(P) (gcc_assert (P), free (P), P = 0)

/* Structure for holding information about a trace.  */
struct trace
{
  /* First and last basic block of the trace.  */
  basic_block first, last;

  /* The round of the STC creation which this trace was found in.  */
  int round;

  /* The length (i.e. the number of basic blocks) of the trace.  */
  int length;
};

/* Maximum frequency and count of one of the entry blocks.  */
static int max_entry_frequency;
static gcov_type max_entry_count;

/* Local function prototypes.  */
static void find_traces (int *, struct trace *);
static basic_block rotate_loop (edge, struct trace *, int);
static void mark_bb_visited (basic_block, int);
static void find_traces_1_round (int, int, gcov_type, struct trace *, int *,
				 int, fibheap_t *, int);
static basic_block copy_bb (basic_block, edge, basic_block, int);
static fibheapkey_t bb_to_key (basic_block);
static bool better_edge_p (basic_block, edge, int, int, int, int, edge);
static void connect_traces (int, struct trace *);
static bool copy_bb_p (basic_block, int);
static int get_uncond_jump_length (void);
static bool push_to_next_round_p (basic_block, int, int, int, gcov_type);
static void find_rarely_executed_basic_blocks_and_crossing_edges (edge *,
								  int *,
								  int *);
static void add_labels_and_missing_jumps (edge *, int);
static void add_reg_crossing_jump_notes (void);
static void fix_up_fall_thru_edges (void);
static void fix_edges_for_rarely_executed_code (edge *, int);
static void fix_crossing_conditional_branches (void);
static void fix_crossing_unconditional_branches (void);

/* Check to see if bb should be pushed into the next round of trace
   collections or not.  Reasons for pushing the block forward are 1).
   If the block is cold, we are doing partitioning, and there will be
   another round (cold partition blocks are not supposed to be
   collected into traces until the very last round); or 2). There will
   be another round, and the basic block is not "hot enough" for the
   current round of trace collection.  */

static bool
push_to_next_round_p (basic_block bb, int round, int number_of_rounds,
		      int exec_th, gcov_type count_th)
{
  bool there_exists_another_round;
  bool block_not_hot_enough;

  there_exists_another_round = round < number_of_rounds - 1;

  block_not_hot_enough = (bb->frequency < exec_th
			  || bb->count < count_th
			  || probably_never_executed_bb_p (bb));

  if (there_exists_another_round
      && block_not_hot_enough)
    return true;
  else
    return false;
}

/* Find the traces for Software Trace Cache.  Chain each trace through
   RBI()->next.  Store the number of traces to N_TRACES and description of
   traces to TRACES.  */

static void
find_traces (int *n_traces, struct trace *traces)
{
  int i;
  int number_of_rounds;
  edge e;
  edge_iterator ei;
  fibheap_t heap;

  /* Add one extra round of trace collection when partitioning hot/cold
     basic blocks into separate sections.  The last round is for all the
     cold blocks (and ONLY the cold blocks).  */

  number_of_rounds = N_ROUNDS - 1;

  /* Insert entry points of function into heap.  */
  heap = fibheap_new ();
  max_entry_frequency = 0;
  max_entry_count = 0;
  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    {
      bbd[e->dest->index].heap = heap;
      bbd[e->dest->index].node = fibheap_insert (heap, bb_to_key (e->dest),
						    e->dest);
      if (e->dest->frequency > max_entry_frequency)
	max_entry_frequency = e->dest->frequency;
      if (e->dest->count > max_entry_count)
	max_entry_count = e->dest->count;
    }

  /* Find the traces.  */
  for (i = 0; i < number_of_rounds; i++)
    {
      gcov_type count_threshold;

      if (dump_file)
	fprintf (dump_file, "STC - round %d\n", i + 1);

      if (max_entry_count < INT_MAX / 1000)
	count_threshold = max_entry_count * exec_threshold[i] / 1000;
      else
	count_threshold = max_entry_count / 1000 * exec_threshold[i];

      find_traces_1_round (REG_BR_PROB_BASE * branch_threshold[i] / 1000,
			   max_entry_frequency * exec_threshold[i] / 1000,
			   count_threshold, traces, n_traces, i, &heap,
			   number_of_rounds);
    }
  fibheap_delete (heap);

  if (dump_file)
    {
      for (i = 0; i < *n_traces; i++)
	{
	  basic_block bb;
	  fprintf (dump_file, "Trace %d (round %d):  ", i + 1,
		   traces[i].round + 1);
	  for (bb = traces[i].first; bb != traces[i].last; bb = bb->aux)
	    fprintf (dump_file, "%d [%d] ", bb->index, bb->frequency);
	  fprintf (dump_file, "%d [%d]\n", bb->index, bb->frequency);
	}
      fflush (dump_file);
    }
}

/* Rotate loop whose back edge is BACK_EDGE in the tail of trace TRACE
   (with sequential number TRACE_N).  */

static basic_block
rotate_loop (edge back_edge, struct trace *trace, int trace_n)
{
  basic_block bb;

  /* Information about the best end (end after rotation) of the loop.  */
  basic_block best_bb = NULL;
  edge best_edge = NULL;
  int best_freq = -1;
  gcov_type best_count = -1;
  /* The best edge is preferred when its destination is not visited yet
     or is a start block of some trace.  */
  bool is_preferred = false;

  /* Find the most frequent edge that goes out from current trace.  */
  bb = back_edge->dest;
  do
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest != EXIT_BLOCK_PTR
	    && e->dest->il.rtl->visited != trace_n
	    && (e->flags & EDGE_CAN_FALLTHRU)
	    && !(e->flags & EDGE_COMPLEX))
	{
	  if (is_preferred)
	    {
	      /* The best edge is preferred.  */
	      if (!e->dest->il.rtl->visited
		  || bbd[e->dest->index].start_of_trace >= 0)
		{
		  /* The current edge E is also preferred.  */
		  int freq = EDGE_FREQUENCY (e);
		  if (freq > best_freq || e->count > best_count)
		    {
		      best_freq = freq;
		      best_count = e->count;
		      best_edge = e;
		      best_bb = bb;
		    }
		}
	    }
	  else
	    {
	      if (!e->dest->il.rtl->visited
		  || bbd[e->dest->index].start_of_trace >= 0)
		{
		  /* The current edge E is preferred.  */
		  is_preferred = true;
		  best_freq = EDGE_FREQUENCY (e);
		  best_count = e->count;
		  best_edge = e;
		  best_bb = bb;
		}
	      else
		{
		  int freq = EDGE_FREQUENCY (e);
		  if (!best_edge || freq > best_freq || e->count > best_count)
		    {
		      best_freq = freq;
		      best_count = e->count;
		      best_edge = e;
		      best_bb = bb;
		    }
		}
	    }
	}
      bb = bb->aux;
    }
  while (bb != back_edge->dest);

  if (best_bb)
    {
      /* Rotate the loop so that the BEST_EDGE goes out from the last block of
	 the trace.  */
      if (back_edge->dest == trace->first)
	{
	  trace->first = best_bb->aux;
	}
      else
	{
	  basic_block prev_bb;

	  for (prev_bb = trace->first;
	       prev_bb->aux != back_edge->dest;
	       prev_bb = prev_bb->aux)
	    ;
	  prev_bb->aux = best_bb->aux;

	  /* Try to get rid of uncond jump to cond jump.  */
	  if (single_succ_p (prev_bb))
	    {
	      basic_block header = single_succ (prev_bb);

	      /* Duplicate HEADER if it is a small block containing cond jump
		 in the end.  */
	      if (any_condjump_p (BB_END (header)) && copy_bb_p (header, 0)
		  && !find_reg_note (BB_END (header), REG_CROSSING_JUMP,
				     NULL_RTX))
		copy_bb (header, single_succ_edge (prev_bb), prev_bb, trace_n);
	    }
	}
    }
  else
    {
      /* We have not found suitable loop tail so do no rotation.  */
      best_bb = back_edge->src;
    }
  best_bb->aux = NULL;
  return best_bb;
}

/* This function marks BB that it was visited in trace number TRACE.  */

static void
mark_bb_visited (basic_block bb, int trace)
{
  bb->il.rtl->visited = trace;
  if (bbd[bb->index].heap)
    {
      fibheap_delete_node (bbd[bb->index].heap, bbd[bb->index].node);
      bbd[bb->index].heap = NULL;
      bbd[bb->index].node = NULL;
    }
}

/* One round of finding traces. Find traces for BRANCH_TH and EXEC_TH i.e. do
   not include basic blocks their probability is lower than BRANCH_TH or their
   frequency is lower than EXEC_TH into traces (or count is lower than
   COUNT_TH).  It stores the new traces into TRACES and modifies the number of
   traces *N_TRACES. Sets the round (which the trace belongs to) to ROUND. It
   expects that starting basic blocks are in *HEAP and at the end it deletes
   *HEAP and stores starting points for the next round into new *HEAP.  */

static void
find_traces_1_round (int branch_th, int exec_th, gcov_type count_th,
		     struct trace *traces, int *n_traces, int round,
		     fibheap_t *heap, int number_of_rounds)
{
  /* Heap for discarded basic blocks which are possible starting points for
     the next round.  */
  fibheap_t new_heap = fibheap_new ();

  while (!fibheap_empty (*heap))
    {
      basic_block bb;
      struct trace *trace;
      edge best_edge, e;
      fibheapkey_t key;
      edge_iterator ei;

      bb = fibheap_extract_min (*heap);
      bbd[bb->index].heap = NULL;
      bbd[bb->index].node = NULL;

      if (dump_file)
	fprintf (dump_file, "Getting bb %d\n", bb->index);

      /* If the BB's frequency is too low send BB to the next round.  When
	 partitioning hot/cold blocks into separate sections, make sure all
	 the cold blocks (and ONLY the cold blocks) go into the (extra) final
	 round.  */

      if (push_to_next_round_p (bb, round, number_of_rounds, exec_th,
				count_th))
	{
	  int key = bb_to_key (bb);
	  bbd[bb->index].heap = new_heap;
	  bbd[bb->index].node = fibheap_insert (new_heap, key, bb);

	  if (dump_file)
	    fprintf (dump_file,
		     "  Possible start point of next round: %d (key: %d)\n",
		     bb->index, key);
	  continue;
	}

      trace = traces + *n_traces;
      trace->first = bb;
      trace->round = round;
      trace->length = 0;
      bbd[bb->index].in_trace = *n_traces;
      (*n_traces)++;

      do
	{
	  int prob, freq;
	  bool ends_in_call;

	  /* The probability and frequency of the best edge.  */
	  int best_prob = INT_MIN / 2;
	  int best_freq = INT_MIN / 2;

	  best_edge = NULL;
	  mark_bb_visited (bb, *n_traces);
	  trace->length++;

	  if (dump_file)
	    fprintf (dump_file, "Basic block %d was visited in trace %d\n",
		     bb->index, *n_traces - 1);

	  ends_in_call = block_ends_with_call_p (bb);

	  /* Select the successor that will be placed after BB.  */
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      gcc_assert (!(e->flags & EDGE_FAKE));

	      if (e->dest == EXIT_BLOCK_PTR)
		continue;

	      if (e->dest->il.rtl->visited
		  && e->dest->il.rtl->visited != *n_traces)
		continue;

	      if (BB_PARTITION (e->dest) != BB_PARTITION (bb))
		continue;

	      prob = e->probability;
	      freq = e->dest->frequency;

	      /* The only sensible preference for a call instruction is the
		 fallthru edge.  Don't bother selecting anything else.  */
	      if (ends_in_call)
		{
		  if (e->flags & EDGE_CAN_FALLTHRU)
		    {
		      best_edge = e;
		      best_prob = prob;
		      best_freq = freq;
		    }
		  continue;
		}

	      /* Edge that cannot be fallthru or improbable or infrequent
		 successor (i.e. it is unsuitable successor).  */
	      if (!(e->flags & EDGE_CAN_FALLTHRU) || (e->flags & EDGE_COMPLEX)
		  || prob < branch_th || EDGE_FREQUENCY (e) < exec_th
		  || e->count < count_th)
		continue;

	      /* If partitioning hot/cold basic blocks, don't consider edges
		 that cross section boundaries.  */

	      if (better_edge_p (bb, e, prob, freq, best_prob, best_freq,
				 best_edge))
		{
		  best_edge = e;
		  best_prob = prob;
		  best_freq = freq;
		}
	    }

	  /* If the best destination has multiple predecessors, and can be
	     duplicated cheaper than a jump, don't allow it to be added
	     to a trace.  We'll duplicate it when connecting traces.  */
	  if (best_edge && EDGE_COUNT (best_edge->dest->preds) >= 2
	      && copy_bb_p (best_edge->dest, 0))
	    best_edge = NULL;

	  /* Add all non-selected successors to the heaps.  */
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      if (e == best_edge
		  || e->dest == EXIT_BLOCK_PTR
		  || e->dest->il.rtl->visited)
		continue;

	      key = bb_to_key (e->dest);

	      if (bbd[e->dest->index].heap)
		{
		  /* E->DEST is already in some heap.  */
		  if (key != bbd[e->dest->index].node->key)
		    {
		      if (dump_file)
			{
			  fprintf (dump_file,
				   "Changing key for bb %d from %ld to %ld.\n",
				   e->dest->index,
				   (long) bbd[e->dest->index].node->key,
				   key);
			}
		      fibheap_replace_key (bbd[e->dest->index].heap,
					   bbd[e->dest->index].node, key);
		    }
		}
	      else
		{
		  fibheap_t which_heap = *heap;

		  prob = e->probability;
		  freq = EDGE_FREQUENCY (e);

		  if (!(e->flags & EDGE_CAN_FALLTHRU)
		      || (e->flags & EDGE_COMPLEX)
		      || prob < branch_th || freq < exec_th
		      || e->count < count_th)
		    {
		      /* When partitioning hot/cold basic blocks, make sure
			 the cold blocks (and only the cold blocks) all get
			 pushed to the last round of trace collection.  */

		      if (push_to_next_round_p (e->dest, round,
						number_of_rounds,
						exec_th, count_th))
			which_heap = new_heap;
		    }

		  bbd[e->dest->index].heap = which_heap;
		  bbd[e->dest->index].node = fibheap_insert (which_heap,
								key, e->dest);

		  if (dump_file)
		    {
		      fprintf (dump_file,
			       "  Possible start of %s round: %d (key: %ld)\n",
			       (which_heap == new_heap) ? "next" : "this",
			       e->dest->index, (long) key);
		    }

		}
	    }

	  if (best_edge) /* Suitable successor was found.  */
	    {
	      if (best_edge->dest->il.rtl->visited == *n_traces)
		{
		  /* We do nothing with one basic block loops.  */
		  if (best_edge->dest != bb)
		    {
		      if (EDGE_FREQUENCY (best_edge)
			  > 4 * best_edge->dest->frequency / 5)
			{
			  /* The loop has at least 4 iterations.  If the loop
			     header is not the first block of the function
			     we can rotate the loop.  */

			  if (best_edge->dest != ENTRY_BLOCK_PTR->next_bb)
			    {
			      if (dump_file)
				{
				  fprintf (dump_file,
					   "Rotating loop %d - %d\n",
					   best_edge->dest->index, bb->index);
				}
			      bb->aux = best_edge->dest;
			      bbd[best_edge->dest->index].in_trace =
							     (*n_traces) - 1;
			      bb = rotate_loop (best_edge, trace, *n_traces);
			    }
			}
		      else
			{
			  /* The loop has less than 4 iterations.  */

			  if (single_succ_p (bb)
			      && copy_bb_p (best_edge->dest, !optimize_size))
			    {
			      bb = copy_bb (best_edge->dest, best_edge, bb,
					    *n_traces);
			      trace->length++;
			    }
			}
		    }

		  /* Terminate the trace.  */
		  break;
		}
	      else
		{
		  /* Check for a situation

		    A
		   /|
		  B |
		   \|
		    C

		  where
		  EDGE_FREQUENCY (AB) + EDGE_FREQUENCY (BC)
		    >= EDGE_FREQUENCY (AC).
		  (i.e. 2 * B->frequency >= EDGE_FREQUENCY (AC) )
		  Best ordering is then A B C.

		  This situation is created for example by:

		  if (A) B;
		  C;

		  */

		  FOR_EACH_EDGE (e, ei, bb->succs)
		    if (e != best_edge
			&& (e->flags & EDGE_CAN_FALLTHRU)
			&& !(e->flags & EDGE_COMPLEX)
			&& !e->dest->il.rtl->visited
			&& single_pred_p (e->dest)
			&& !(e->flags & EDGE_CROSSING)
			&& single_succ_p (e->dest)
			&& (single_succ_edge (e->dest)->flags
			    & EDGE_CAN_FALLTHRU)
			&& !(single_succ_edge (e->dest)->flags & EDGE_COMPLEX)
			&& single_succ (e->dest) == best_edge->dest
			&& 2 * e->dest->frequency >= EDGE_FREQUENCY (best_edge))
		      {
			best_edge = e;
			if (dump_file)
			  fprintf (dump_file, "Selecting BB %d\n",
				   best_edge->dest->index);
			break;
		      }

		  bb->aux = best_edge->dest;
		  bbd[best_edge->dest->index].in_trace = (*n_traces) - 1;
		  bb = best_edge->dest;
		}
	    }
	}
      while (best_edge);
      trace->last = bb;
      bbd[trace->first->index].start_of_trace = *n_traces - 1;
      bbd[trace->last->index].end_of_trace = *n_traces - 1;

      /* The trace is terminated so we have to recount the keys in heap
	 (some block can have a lower key because now one of its predecessors
	 is an end of the trace).  */
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->dest == EXIT_BLOCK_PTR
	      || e->dest->il.rtl->visited)
	    continue;

	  if (bbd[e->dest->index].heap)
	    {
	      key = bb_to_key (e->dest);
	      if (key != bbd[e->dest->index].node->key)
		{
		  if (dump_file)
		    {
		      fprintf (dump_file,
			       "Changing key for bb %d from %ld to %ld.\n",
			       e->dest->index,
			       (long) bbd[e->dest->index].node->key, key);
		    }
		  fibheap_replace_key (bbd[e->dest->index].heap,
				       bbd[e->dest->index].node,
				       key);
		}
	    }
	}
    }

  fibheap_delete (*heap);

  /* "Return" the new heap.  */
  *heap = new_heap;
}

/* Create a duplicate of the basic block OLD_BB and redirect edge E to it, add
   it to trace after BB, mark OLD_BB visited and update pass' data structures
   (TRACE is a number of trace which OLD_BB is duplicated to).  */

static basic_block
copy_bb (basic_block old_bb, edge e, basic_block bb, int trace)
{
  basic_block new_bb;

  new_bb = duplicate_block (old_bb, e, bb);
  BB_COPY_PARTITION (new_bb, old_bb);

  gcc_assert (e->dest == new_bb);
  gcc_assert (!e->dest->il.rtl->visited);

  if (dump_file)
    fprintf (dump_file,
	     "Duplicated bb %d (created bb %d)\n",
	     old_bb->index, new_bb->index);
  new_bb->il.rtl->visited = trace;
  new_bb->aux = bb->aux;
  bb->aux = new_bb;

  if (new_bb->index >= array_size || last_basic_block > array_size)
    {
      int i;
      int new_size;

      new_size = MAX (last_basic_block, new_bb->index + 1);
      new_size = GET_ARRAY_SIZE (new_size);
      bbd = xrealloc (bbd, new_size * sizeof (bbro_basic_block_data));
      for (i = array_size; i < new_size; i++)
	{
	  bbd[i].start_of_trace = -1;
	  bbd[i].in_trace = -1;
	  bbd[i].end_of_trace = -1;
	  bbd[i].heap = NULL;
	  bbd[i].node = NULL;
	}
      array_size = new_size;

      if (dump_file)
	{
	  fprintf (dump_file,
		   "Growing the dynamic array to %d elements.\n",
		   array_size);
	}
    }

  bbd[new_bb->index].in_trace = trace;

  return new_bb;
}

/* Compute and return the key (for the heap) of the basic block BB.  */

static fibheapkey_t
bb_to_key (basic_block bb)
{
  edge e;
  edge_iterator ei;
  int priority = 0;

  /* Do not start in probably never executed blocks.  */

  if (BB_PARTITION (bb) == BB_COLD_PARTITION
      || probably_never_executed_bb_p (bb))
    return BB_FREQ_MAX;

  /* Prefer blocks whose predecessor is an end of some trace
     or whose predecessor edge is EDGE_DFS_BACK.  */
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if ((e->src != ENTRY_BLOCK_PTR && bbd[e->src->index].end_of_trace >= 0)
	  || (e->flags & EDGE_DFS_BACK))
	{
	  int edge_freq = EDGE_FREQUENCY (e);

	  if (edge_freq > priority)
	    priority = edge_freq;
	}
    }

  if (priority)
    /* The block with priority should have significantly lower key.  */
    return -(100 * BB_FREQ_MAX + 100 * priority + bb->frequency);
  return -bb->frequency;
}

/* Return true when the edge E from basic block BB is better than the temporary
   best edge (details are in function).  The probability of edge E is PROB. The
   frequency of the successor is FREQ.  The current best probability is
   BEST_PROB, the best frequency is BEST_FREQ.
   The edge is considered to be equivalent when PROB does not differ much from
   BEST_PROB; similarly for frequency.  */

static bool
better_edge_p (basic_block bb, edge e, int prob, int freq, int best_prob,
	       int best_freq, edge cur_best_edge)
{
  bool is_better_edge;

  /* The BEST_* values do not have to be best, but can be a bit smaller than
     maximum values.  */
  int diff_prob = best_prob / 10;
  int diff_freq = best_freq / 10;

  if (prob > best_prob + diff_prob)
    /* The edge has higher probability than the temporary best edge.  */
    is_better_edge = true;
  else if (prob < best_prob - diff_prob)
    /* The edge has lower probability than the temporary best edge.  */
    is_better_edge = false;
  else if (freq < best_freq - diff_freq)
    /* The edge and the temporary best edge  have almost equivalent
       probabilities.  The higher frequency of a successor now means
       that there is another edge going into that successor.
       This successor has lower frequency so it is better.  */
    is_better_edge = true;
  else if (freq > best_freq + diff_freq)
    /* This successor has higher frequency so it is worse.  */
    is_better_edge = false;
  else if (e->dest->prev_bb == bb)
    /* The edges have equivalent probabilities and the successors
       have equivalent frequencies.  Select the previous successor.  */
    is_better_edge = true;
  else
    is_better_edge = false;

  /* If we are doing hot/cold partitioning, make sure that we always favor
     non-crossing edges over crossing edges.  */

  if (!is_better_edge
      && flag_reorder_blocks_and_partition
      && cur_best_edge
      && (cur_best_edge->flags & EDGE_CROSSING)
      && !(e->flags & EDGE_CROSSING))
    is_better_edge = true;

  return is_better_edge;
}

/* Connect traces in array TRACES, N_TRACES is the count of traces.  */

static void
connect_traces (int n_traces, struct trace *traces)
{
  int i;
  bool *connected;
  bool two_passes;
  int last_trace;
  int current_pass;
  int current_partition;
  int freq_threshold;
  gcov_type count_threshold;

  freq_threshold = max_entry_frequency * DUPLICATION_THRESHOLD / 1000;
  if (max_entry_count < INT_MAX / 1000)
    count_threshold = max_entry_count * DUPLICATION_THRESHOLD / 1000;
  else
    count_threshold = max_entry_count / 1000 * DUPLICATION_THRESHOLD;

  connected = XCNEWVEC (bool, n_traces);
  last_trace = -1;
  current_pass = 1;
  current_partition = BB_PARTITION (traces[0].first);
  two_passes = false;

  if (flag_reorder_blocks_and_partition)
    for (i = 0; i < n_traces && !two_passes; i++)
      if (BB_PARTITION (traces[0].first)
	  != BB_PARTITION (traces[i].first))
	two_passes = true;

  for (i = 0; i < n_traces || (two_passes && current_pass == 1) ; i++)
    {
      int t = i;
      int t2;
      edge e, best;
      int best_len;

      if (i >= n_traces)
	{
	  gcc_assert (two_passes && current_pass == 1);
	  i = 0;
	  t = i;
	  current_pass = 2;
	  if (current_partition == BB_HOT_PARTITION)
	    current_partition = BB_COLD_PARTITION;
	  else
	    current_partition = BB_HOT_PARTITION;
	}

      if (connected[t])
	continue;

      if (two_passes
	  && BB_PARTITION (traces[t].first) != current_partition)
	continue;

      connected[t] = true;

      /* Find the predecessor traces.  */
      for (t2 = t; t2 > 0;)
	{
	  edge_iterator ei;
	  best = NULL;
	  best_len = 0;
	  FOR_EACH_EDGE (e, ei, traces[t2].first->preds)
	    {
	      int si = e->src->index;

	      if (e->src != ENTRY_BLOCK_PTR
		  && (e->flags & EDGE_CAN_FALLTHRU)
		  && !(e->flags & EDGE_COMPLEX)
		  && bbd[si].end_of_trace >= 0
		  && !connected[bbd[si].end_of_trace]
		  && (BB_PARTITION (e->src) == current_partition)
		  && (!best
		      || e->probability > best->probability
		      || (e->probability == best->probability
			  && traces[bbd[si].end_of_trace].length > best_len)))
		{
		  best = e;
		  best_len = traces[bbd[si].end_of_trace].length;
		}
	    }
	  if (best)
	    {
	      best->src->aux = best->dest;
	      t2 = bbd[best->src->index].end_of_trace;
	      connected[t2] = true;

	      if (dump_file)
		{
		  fprintf (dump_file, "Connection: %d %d\n",
			   best->src->index, best->dest->index);
		}
	    }
	  else
	    break;
	}

      if (last_trace >= 0)
	traces[last_trace].last->aux = traces[t2].first;
      last_trace = t;

      /* Find the successor traces.  */
      while (1)
	{
	  /* Find the continuation of the chain.  */
	  edge_iterator ei;
	  best = NULL;
	  best_len = 0;
	  FOR_EACH_EDGE (e, ei, traces[t].last->succs)
	    {
	      int di = e->dest->index;

	      if (e->dest != EXIT_BLOCK_PTR
		  && (e->flags & EDGE_CAN_FALLTHRU)
		  && !(e->flags & EDGE_COMPLEX)
		  && bbd[di].start_of_trace >= 0
		  && !connected[bbd[di].start_of_trace]
		  && (BB_PARTITION (e->dest) == current_partition)
		  && (!best
		      || e->probability > best->probability
		      || (e->probability == best->probability
			  && traces[bbd[di].start_of_trace].length > best_len)))
		{
		  best = e;
		  best_len = traces[bbd[di].start_of_trace].length;
		}
	    }

	  if (best)
	    {
	      if (dump_file)
		{
		  fprintf (dump_file, "Connection: %d %d\n",
			   best->src->index, best->dest->index);
		}
	      t = bbd[best->dest->index].start_of_trace;
	      traces[last_trace].last->aux = traces[t].first;
	      connected[t] = true;
	      last_trace = t;
	    }
	  else
	    {
	      /* Try to connect the traces by duplication of 1 block.  */
	      edge e2;
	      basic_block next_bb = NULL;
	      bool try_copy = false;

	      FOR_EACH_EDGE (e, ei, traces[t].last->succs)
		if (e->dest != EXIT_BLOCK_PTR
		    && (e->flags & EDGE_CAN_FALLTHRU)
		    && !(e->flags & EDGE_COMPLEX)
		    && (!best || e->probability > best->probability))
		  {
		    edge_iterator ei;
		    edge best2 = NULL;
		    int best2_len = 0;

		    /* If the destination is a start of a trace which is only
		       one block long, then no need to search the successor
		       blocks of the trace.  Accept it.  */
		    if (bbd[e->dest->index].start_of_trace >= 0
			&& traces[bbd[e->dest->index].start_of_trace].length
			   == 1)
		      {
			best = e;
			try_copy = true;
			continue;
		      }

		    FOR_EACH_EDGE (e2, ei, e->dest->succs)
		      {
			int di = e2->dest->index;

			if (e2->dest == EXIT_BLOCK_PTR
			    || ((e2->flags & EDGE_CAN_FALLTHRU)
				&& !(e2->flags & EDGE_COMPLEX)
				&& bbd[di].start_of_trace >= 0
				&& !connected[bbd[di].start_of_trace]
				&& (BB_PARTITION (e2->dest) == current_partition)
				&& (EDGE_FREQUENCY (e2) >= freq_threshold)
				&& (e2->count >= count_threshold)
				&& (!best2
				    || e2->probability > best2->probability
				    || (e2->probability == best2->probability
					&& traces[bbd[di].start_of_trace].length
					   > best2_len))))
			  {
			    best = e;
			    best2 = e2;
			    if (e2->dest != EXIT_BLOCK_PTR)
			      best2_len = traces[bbd[di].start_of_trace].length;
			    else
			      best2_len = INT_MAX;
			    next_bb = e2->dest;
			    try_copy = true;
			  }
		      }
		  }

	      if (flag_reorder_blocks_and_partition)
		try_copy = false;

	      /* Copy tiny blocks always; copy larger blocks only when the
		 edge is traversed frequently enough.  */
	      if (try_copy
		  && copy_bb_p (best->dest,
				!optimize_size
				&& EDGE_FREQUENCY (best) >= freq_threshold
				&& best->count >= count_threshold))
		{
		  basic_block new_bb;

		  if (dump_file)
		    {
		      fprintf (dump_file, "Connection: %d %d ",
			       traces[t].last->index, best->dest->index);
		      if (!next_bb)
			fputc ('\n', dump_file);
		      else if (next_bb == EXIT_BLOCK_PTR)
			fprintf (dump_file, "exit\n");
		      else
			fprintf (dump_file, "%d\n", next_bb->index);
		    }

		  new_bb = copy_bb (best->dest, best, traces[t].last, t);
		  traces[t].last = new_bb;
		  if (next_bb && next_bb != EXIT_BLOCK_PTR)
		    {
		      t = bbd[next_bb->index].start_of_trace;
		      traces[last_trace].last->aux = traces[t].first;
		      connected[t] = true;
		      last_trace = t;
		    }
		  else
		    break;	/* Stop finding the successor traces.  */
		}
	      else
		break;	/* Stop finding the successor traces.  */
	    }
	}
    }

  if (dump_file)
    {
      basic_block bb;

      fprintf (dump_file, "Final order:\n");
      for (bb = traces[0].first; bb; bb = bb->aux)
	fprintf (dump_file, "%d ", bb->index);
      fprintf (dump_file, "\n");
      fflush (dump_file);
    }

  FREE (connected);
}

/* Return true when BB can and should be copied. CODE_MAY_GROW is true
   when code size is allowed to grow by duplication.  */

static bool
copy_bb_p (basic_block bb, int code_may_grow)
{
  int size = 0;
  int max_size = uncond_jump_length;
  rtx insn;

  if (!bb->frequency)
    return false;
  if (EDGE_COUNT (bb->preds) < 2)
    return false;
  if (!can_duplicate_block_p (bb))
    return false;

  /* Avoid duplicating blocks which have many successors (PR/13430).  */
  if (EDGE_COUNT (bb->succs) > 8)
    return false;

  if (code_may_grow && maybe_hot_bb_p (bb))
    max_size *= PARAM_VALUE (PARAM_MAX_GROW_COPY_BB_INSNS);

  FOR_BB_INSNS (bb, insn)
    {
      if (INSN_P (insn))
	size += get_attr_min_length (insn);
    }

  if (size <= max_size)
    return true;

  if (dump_file)
    {
      fprintf (dump_file,
	       "Block %d can't be copied because its size = %d.\n",
	       bb->index, size);
    }

  return false;
}

/* Return the length of unconditional jump instruction.  */

static int
get_uncond_jump_length (void)
{
  rtx label, jump;
  int length;

  label = emit_label_before (gen_label_rtx (), get_insns ());
  jump = emit_jump_insn (gen_jump (label));

  length = get_attr_min_length (jump);

  delete_insn (jump);
  delete_insn (label);
  return length;
}

/* Find the basic blocks that are rarely executed and need to be moved to
   a separate section of the .o file (to cut down on paging and improve
   cache locality).  */

static void
find_rarely_executed_basic_blocks_and_crossing_edges (edge *crossing_edges,
						      int *n_crossing_edges,
						      int *max_idx)
{
  basic_block bb;
  bool has_hot_blocks = false;
  edge e;
  int i;
  edge_iterator ei;

  /* Mark which partition (hot/cold) each basic block belongs in.  */

  FOR_EACH_BB (bb)
    {
      if (probably_never_executed_bb_p (bb))
	BB_SET_PARTITION (bb, BB_COLD_PARTITION);
      else
	{
	  BB_SET_PARTITION (bb, BB_HOT_PARTITION);
	  has_hot_blocks = true;
	}
    }

  /* Mark every edge that crosses between sections.  */

  i = 0;
  FOR_EACH_BB (bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->src != ENTRY_BLOCK_PTR
	  && e->dest != EXIT_BLOCK_PTR
	  && BB_PARTITION (e->src) != BB_PARTITION (e->dest))
	{
	  e->flags |= EDGE_CROSSING;
	  if (i == *max_idx)
	    {
	      *max_idx *= 2;
	      crossing_edges = xrealloc (crossing_edges,
					 (*max_idx) * sizeof (edge));
	    }
	  crossing_edges[i++] = e;
	}
      else
	e->flags &= ~EDGE_CROSSING;
    }
  *n_crossing_edges = i;
}

/* If any destination of a crossing edge does not have a label, add label;
   Convert any fall-through crossing edges (for blocks that do not contain
   a jump) to unconditional jumps.  */

static void
add_labels_and_missing_jumps (edge *crossing_edges, int n_crossing_edges)
{
  int i;
  basic_block src;
  basic_block dest;
  rtx label;
  rtx barrier;
  rtx new_jump;

  for (i=0; i < n_crossing_edges; i++)
    {
      if (crossing_edges[i])
	{
	  src = crossing_edges[i]->src;
	  dest = crossing_edges[i]->dest;

	  /* Make sure dest has a label.  */

	  if (dest && (dest != EXIT_BLOCK_PTR))
	    {
	      label = block_label (dest);

	      /* Make sure source block ends with a jump.  */

	      if (src && (src != ENTRY_BLOCK_PTR))
		{
		  if (!JUMP_P (BB_END (src)))
		    /* bb just falls through.  */
		    {
		      /* make sure there's only one successor */
		      gcc_assert (single_succ_p (src));

		      /* Find label in dest block.  */
		      label = block_label (dest);

		      new_jump = emit_jump_insn_after (gen_jump (label),
						       BB_END (src));
		      barrier = emit_barrier_after (new_jump);
		      JUMP_LABEL (new_jump) = label;
		      LABEL_NUSES (label) += 1;
		      src->il.rtl->footer = unlink_insn_chain (barrier, barrier);
		      /* Mark edge as non-fallthru.  */
		      crossing_edges[i]->flags &= ~EDGE_FALLTHRU;
		    } /* end: 'if (GET_CODE ... '  */
		} /* end: 'if (src && src->index...'  */
	    } /* end: 'if (dest && dest->index...'  */
	} /* end: 'if (crossing_edges[i]...'  */
    } /* end for loop  */
}

/* Find any bb's where the fall-through edge is a crossing edge (note that
   these bb's must also contain a conditional jump; we've already
   dealt with fall-through edges for blocks that didn't have a
   conditional jump in the call to add_labels_and_missing_jumps).
   Convert the fall-through edge to non-crossing edge by inserting a
   new bb to fall-through into.  The new bb will contain an
   unconditional jump (crossing edge) to the original fall through
   destination.  */

static void
fix_up_fall_thru_edges (void)
{
  basic_block cur_bb;
  basic_block new_bb;
  edge succ1;
  edge succ2;
  edge fall_thru;
  edge cond_jump = NULL;
  edge e;
  bool cond_jump_crosses;
  int invert_worked;
  rtx old_jump;
  rtx fall_thru_label;
  rtx barrier;

  FOR_EACH_BB (cur_bb)
    {
      fall_thru = NULL;
      if (EDGE_COUNT (cur_bb->succs) > 0)
	succ1 = EDGE_SUCC (cur_bb, 0);
      else
	succ1 = NULL;

      if (EDGE_COUNT (cur_bb->succs) > 1)
	succ2 = EDGE_SUCC (cur_bb, 1);
      else
	succ2 = NULL;

      /* Find the fall-through edge.  */

      if (succ1
	  && (succ1->flags & EDGE_FALLTHRU))
	{
	  fall_thru = succ1;
	  cond_jump = succ2;
	}
      else if (succ2
	       && (succ2->flags & EDGE_FALLTHRU))
	{
	  fall_thru = succ2;
	  cond_jump = succ1;
	}

      if (fall_thru && (fall_thru->dest != EXIT_BLOCK_PTR))
	{
	  /* Check to see if the fall-thru edge is a crossing edge.  */

	  if (fall_thru->flags & EDGE_CROSSING)
	    {
	      /* The fall_thru edge crosses; now check the cond jump edge, if
		 it exists.  */

	      cond_jump_crosses = true;
	      invert_worked  = 0;
	      old_jump = BB_END (cur_bb);

	      /* Find the jump instruction, if there is one.  */

	      if (cond_jump)
		{
		  if (!(cond_jump->flags & EDGE_CROSSING))
		    cond_jump_crosses = false;

		  /* We know the fall-thru edge crosses; if the cond
		     jump edge does NOT cross, and its destination is the
		     next block in the bb order, invert the jump
		     (i.e. fix it so the fall thru does not cross and
		     the cond jump does).  */

		  if (!cond_jump_crosses
		      && cur_bb->aux == cond_jump->dest)
		    {
		      /* Find label in fall_thru block. We've already added
			 any missing labels, so there must be one.  */

		      fall_thru_label = block_label (fall_thru->dest);

		      if (old_jump && fall_thru_label)
			invert_worked = invert_jump (old_jump,
						     fall_thru_label,0);
		      if (invert_worked)
			{
			  fall_thru->flags &= ~EDGE_FALLTHRU;
			  cond_jump->flags |= EDGE_FALLTHRU;
			  update_br_prob_note (cur_bb);
			  e = fall_thru;
			  fall_thru = cond_jump;
			  cond_jump = e;
			  cond_jump->flags |= EDGE_CROSSING;
			  fall_thru->flags &= ~EDGE_CROSSING;
			}
		    }
		}

	      if (cond_jump_crosses || !invert_worked)
		{
		  /* This is the case where both edges out of the basic
		     block are crossing edges. Here we will fix up the
		     fall through edge. The jump edge will be taken care
		     of later.  */

		  new_bb = force_nonfallthru (fall_thru);

		  if (new_bb)
		    {
		      new_bb->aux = cur_bb->aux;
		      cur_bb->aux = new_bb;

		      /* Make sure new fall-through bb is in same
			 partition as bb it's falling through from.  */

		      BB_COPY_PARTITION (new_bb, cur_bb);
		      single_succ_edge (new_bb)->flags |= EDGE_CROSSING;
		    }

		  /* Add barrier after new jump */

		  if (new_bb)
		    {
		      barrier = emit_barrier_after (BB_END (new_bb));
		      new_bb->il.rtl->footer = unlink_insn_chain (barrier,
							       barrier);
		    }
		  else
		    {
		      barrier = emit_barrier_after (BB_END (cur_bb));
		      cur_bb->il.rtl->footer = unlink_insn_chain (barrier,
							       barrier);
		    }
		}
	    }
	}
    }
}

/* This function checks the destination blockof a "crossing jump" to
   see if it has any crossing predecessors that begin with a code label
   and end with an unconditional jump.  If so, it returns that predecessor
   block.  (This is to avoid creating lots of new basic blocks that all
   contain unconditional jumps to the same destination).  */

static basic_block
find_jump_block (basic_block jump_dest)
{
  basic_block source_bb = NULL;
  edge e;
  rtx insn;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, jump_dest->preds)
    if (e->flags & EDGE_CROSSING)
      {
	basic_block src = e->src;

	/* Check each predecessor to see if it has a label, and contains
	   only one executable instruction, which is an unconditional jump.
	   If so, we can use it.  */

	if (LABEL_P (BB_HEAD (src)))
	  for (insn = BB_HEAD (src);
	       !INSN_P (insn) && insn != NEXT_INSN (BB_END (src));
	       insn = NEXT_INSN (insn))
	    {
	      if (INSN_P (insn)
		  && insn == BB_END (src)
		  && JUMP_P (insn)
		  && !any_condjump_p (insn))
		{
		  source_bb = src;
		  break;
		}
	    }

	if (source_bb)
	  break;
      }

  return source_bb;
}

/* Find all BB's with conditional jumps that are crossing edges;
   insert a new bb and make the conditional jump branch to the new
   bb instead (make the new bb same color so conditional branch won't
   be a 'crossing' edge).  Insert an unconditional jump from the
   new bb to the original destination of the conditional jump.  */

static void
fix_crossing_conditional_branches (void)
{
  basic_block cur_bb;
  basic_block new_bb;
  basic_block last_bb;
  basic_block dest;
  basic_block prev_bb;
  edge succ1;
  edge succ2;
  edge crossing_edge;
  edge new_edge;
  rtx old_jump;
  rtx set_src;
  rtx old_label = NULL_RTX;
  rtx new_label;
  rtx new_jump;
  rtx barrier;

 last_bb = EXIT_BLOCK_PTR->prev_bb;

  FOR_EACH_BB (cur_bb)
    {
      crossing_edge = NULL;
      if (EDGE_COUNT (cur_bb->succs) > 0)
	succ1 = EDGE_SUCC (cur_bb, 0);
      else
	succ1 = NULL;

      if (EDGE_COUNT (cur_bb->succs) > 1)
	succ2 = EDGE_SUCC (cur_bb, 1);
      else
	succ2 = NULL;

      /* We already took care of fall-through edges, so only one successor
	 can be a crossing edge.  */

      if (succ1 && (succ1->flags & EDGE_CROSSING))
	crossing_edge = succ1;
      else if (succ2 && (succ2->flags & EDGE_CROSSING))
	crossing_edge = succ2;

      if (crossing_edge)
	{
	  old_jump = BB_END (cur_bb);

	  /* Check to make sure the jump instruction is a
	     conditional jump.  */

	  set_src = NULL_RTX;

	  if (any_condjump_p (old_jump))
	    {
	      if (GET_CODE (PATTERN (old_jump)) == SET)
		set_src = SET_SRC (PATTERN (old_jump));
	      else if (GET_CODE (PATTERN (old_jump)) == PARALLEL)
		{
		  set_src = XVECEXP (PATTERN (old_jump), 0,0);
		  if (GET_CODE (set_src) == SET)
		    set_src = SET_SRC (set_src);
		  else
		    set_src = NULL_RTX;
		}
	    }

	  if (set_src && (GET_CODE (set_src) == IF_THEN_ELSE))
	    {
	      if (GET_CODE (XEXP (set_src, 1)) == PC)
		old_label = XEXP (set_src, 2);
	      else if (GET_CODE (XEXP (set_src, 2)) == PC)
		old_label = XEXP (set_src, 1);

	      /* Check to see if new bb for jumping to that dest has
		 already been created; if so, use it; if not, create
		 a new one.  */

	      new_bb = find_jump_block (crossing_edge->dest);

	      if (new_bb)
		new_label = block_label (new_bb);
	      else
		{
		  /* Create new basic block to be dest for
		     conditional jump.  */

		  new_bb = create_basic_block (NULL, NULL, last_bb);
		  new_bb->aux = last_bb->aux;
		  last_bb->aux = new_bb;
		  prev_bb = last_bb;
		  last_bb = new_bb;

		  /* Update register liveness information.  */

		  new_bb->il.rtl->global_live_at_start = ALLOC_REG_SET (&reg_obstack);
		  new_bb->il.rtl->global_live_at_end = ALLOC_REG_SET (&reg_obstack);
		  COPY_REG_SET (new_bb->il.rtl->global_live_at_end,
				prev_bb->il.rtl->global_live_at_end);
		  COPY_REG_SET (new_bb->il.rtl->global_live_at_start,
				prev_bb->il.rtl->global_live_at_end);

		  /* Put appropriate instructions in new bb.  */

		  new_label = gen_label_rtx ();
		  emit_label_before (new_label, BB_HEAD (new_bb));
		  BB_HEAD (new_bb) = new_label;

		  if (GET_CODE (old_label) == LABEL_REF)
		    {
		      old_label = JUMP_LABEL (old_jump);
		      new_jump = emit_jump_insn_after (gen_jump
						       (old_label),
						       BB_END (new_bb));
		    }
		  else
		    {
		      gcc_assert (HAVE_return
				  && GET_CODE (old_label) == RETURN);
		      new_jump = emit_jump_insn_after (gen_return (),
						       BB_END (new_bb));
		    }

		  barrier = emit_barrier_after (new_jump);
		  JUMP_LABEL (new_jump) = old_label;
		  new_bb->il.rtl->footer = unlink_insn_chain (barrier,
							   barrier);

		  /* Make sure new bb is in same partition as source
		     of conditional branch.  */
		  BB_COPY_PARTITION (new_bb, cur_bb);
		}

	      /* Make old jump branch to new bb.  */

	      redirect_jump (old_jump, new_label, 0);

	      /* Remove crossing_edge as predecessor of 'dest'.  */

	      dest = crossing_edge->dest;

	      redirect_edge_succ (crossing_edge, new_bb);

	      /* Make a new edge from new_bb to old dest; new edge
		 will be a successor for new_bb and a predecessor
		 for 'dest'.  */

	      if (EDGE_COUNT (new_bb->succs) == 0)
		new_edge = make_edge (new_bb, dest, 0);
	      else
		new_edge = EDGE_SUCC (new_bb, 0);

	      crossing_edge->flags &= ~EDGE_CROSSING;
	      new_edge->flags |= EDGE_CROSSING;
	    }
	}
    }
}

/* Find any unconditional branches that cross between hot and cold
   sections.  Convert them into indirect jumps instead.  */

static void
fix_crossing_unconditional_branches (void)
{
  basic_block cur_bb;
  rtx last_insn;
  rtx label;
  rtx label_addr;
  rtx indirect_jump_sequence;
  rtx jump_insn = NULL_RTX;
  rtx new_reg;
  rtx cur_insn;
  edge succ;

  FOR_EACH_BB (cur_bb)
    {
      last_insn = BB_END (cur_bb);

      if (EDGE_COUNT (cur_bb->succs) < 1)
	continue;

      succ = EDGE_SUCC (cur_bb, 0);

      /* Check to see if bb ends in a crossing (unconditional) jump.  At
	 this point, no crossing jumps should be conditional.  */

      if (JUMP_P (last_insn)
	  && (succ->flags & EDGE_CROSSING))
	{
	  rtx label2, table;

	  gcc_assert (!any_condjump_p (last_insn));

	  /* Make sure the jump is not already an indirect or table jump.  */

	  if (!computed_jump_p (last_insn)
	      && !tablejump_p (last_insn, &label2, &table))
	    {
	      /* We have found a "crossing" unconditional branch.  Now
		 we must convert it to an indirect jump.  First create
		 reference of label, as target for jump.  */

	      label = JUMP_LABEL (last_insn);
	      label_addr = gen_rtx_LABEL_REF (Pmode, label);
	      LABEL_NUSES (label) += 1;

	      /* Get a register to use for the indirect jump.  */

	      new_reg = gen_reg_rtx (Pmode);

	      /* Generate indirect the jump sequence.  */

	      start_sequence ();
	      emit_move_insn (new_reg, label_addr);
	      emit_indirect_jump (new_reg);
	      indirect_jump_sequence = get_insns ();
	      end_sequence ();

	      /* Make sure every instruction in the new jump sequence has
		 its basic block set to be cur_bb.  */

	      for (cur_insn = indirect_jump_sequence; cur_insn;
		   cur_insn = NEXT_INSN (cur_insn))
		{
		  if (!BARRIER_P (cur_insn))
		    BLOCK_FOR_INSN (cur_insn) = cur_bb;
		  if (JUMP_P (cur_insn))
		    jump_insn = cur_insn;
		}

	      /* Insert the new (indirect) jump sequence immediately before
		 the unconditional jump, then delete the unconditional jump.  */

	      emit_insn_before (indirect_jump_sequence, last_insn);
	      delete_insn (last_insn);

	      /* Make BB_END for cur_bb be the jump instruction (NOT the
		 barrier instruction at the end of the sequence...).  */

	      BB_END (cur_bb) = jump_insn;
	    }
	}
    }
}

/* Add REG_CROSSING_JUMP note to all crossing jump insns.  */

static void
add_reg_crossing_jump_notes (void)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  FOR_EACH_BB (bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
      if ((e->flags & EDGE_CROSSING)
	  && JUMP_P (BB_END (e->src)))
	REG_NOTES (BB_END (e->src)) = gen_rtx_EXPR_LIST (REG_CROSSING_JUMP,
							 NULL_RTX,
							 REG_NOTES (BB_END
								  (e->src)));
}

/* Hot and cold basic blocks are partitioned and put in separate
   sections of the .o file, to reduce paging and improve cache
   performance (hopefully).  This can result in bits of code from the
   same function being widely separated in the .o file.  However this
   is not obvious to the current bb structure.  Therefore we must take
   care to ensure that: 1). There are no fall_thru edges that cross
   between sections; 2). For those architectures which have "short"
   conditional branches, all conditional branches that attempt to
   cross between sections are converted to unconditional branches;
   and, 3). For those architectures which have "short" unconditional
   branches, all unconditional branches that attempt to cross between
   sections are converted to indirect jumps.

   The code for fixing up fall_thru edges that cross between hot and
   cold basic blocks does so by creating new basic blocks containing
   unconditional branches to the appropriate label in the "other"
   section.  The new basic block is then put in the same (hot or cold)
   section as the original conditional branch, and the fall_thru edge
   is modified to fall into the new basic block instead.  By adding
   this level of indirection we end up with only unconditional branches
   crossing between hot and cold sections.

   Conditional branches are dealt with by adding a level of indirection.
   A new basic block is added in the same (hot/cold) section as the
   conditional branch, and the conditional branch is retargeted to the
   new basic block.  The new basic block contains an unconditional branch
   to the original target of the conditional branch (in the other section).

   Unconditional branches are dealt with by converting them into
   indirect jumps.  */

static void
fix_edges_for_rarely_executed_code (edge *crossing_edges,
				    int n_crossing_edges)
{
  /* Make sure the source of any crossing edge ends in a jump and the
     destination of any crossing edge has a label.  */

  add_labels_and_missing_jumps (crossing_edges, n_crossing_edges);

  /* Convert all crossing fall_thru edges to non-crossing fall
     thrus to unconditional jumps (that jump to the original fall
     thru dest).  */

  fix_up_fall_thru_edges ();

  /* If the architecture does not have conditional branches that can
     span all of memory, convert crossing conditional branches into
     crossing unconditional branches.  */

  if (!HAS_LONG_COND_BRANCH)
    fix_crossing_conditional_branches ();

  /* If the architecture does not have unconditional branches that
     can span all of memory, convert crossing unconditional branches
     into indirect jumps.  Since adding an indirect jump also adds
     a new register usage, update the register usage information as
     well.  */

  if (!HAS_LONG_UNCOND_BRANCH)
    {
      fix_crossing_unconditional_branches ();
      reg_scan (get_insns(), max_reg_num ());
    }

  add_reg_crossing_jump_notes ();
}

/* Verify, in the basic block chain, that there is at most one switch
   between hot/cold partitions. This is modelled on
   rtl_verify_flow_info_1, but it cannot go inside that function
   because this condition will not be true until after
   reorder_basic_blocks is called.  */

static void
verify_hot_cold_block_grouping (void)
{
  basic_block bb;
  int err = 0;
  bool switched_sections = false;
  int current_partition = 0;

  FOR_EACH_BB (bb)
    {
      if (!current_partition)
	current_partition = BB_PARTITION (bb);
      if (BB_PARTITION (bb) != current_partition)
	{
	  if (switched_sections)
	    {
	      error ("multiple hot/cold transitions found (bb %i)",
		     bb->index);
	      err = 1;
	    }
	  else
	    {
	      switched_sections = true;
	      current_partition = BB_PARTITION (bb);
	    }
	}
    }

  gcc_assert(!err);
}

/* Reorder basic blocks.  The main entry point to this file.  FLAGS is
   the set of flags to pass to cfg_layout_initialize().  */

void
reorder_basic_blocks (unsigned int flags)
{
  int n_traces;
  int i;
  struct trace *traces;

  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1)
    return;

  if (targetm.cannot_modify_jumps_p ())
    return;

  cfg_layout_initialize (flags);

  set_edge_can_fallthru_flag ();
  mark_dfs_back_edges ();

  /* We are estimating the length of uncond jump insn only once since the code
     for getting the insn length always returns the minimal length now.  */
  if (uncond_jump_length == 0)
    uncond_jump_length = get_uncond_jump_length ();

  /* We need to know some information for each basic block.  */
  array_size = GET_ARRAY_SIZE (last_basic_block);
  bbd = XNEWVEC (bbro_basic_block_data, array_size);
  for (i = 0; i < array_size; i++)
    {
      bbd[i].start_of_trace = -1;
      bbd[i].in_trace = -1;
      bbd[i].end_of_trace = -1;
      bbd[i].heap = NULL;
      bbd[i].node = NULL;
    }

  traces = XNEWVEC (struct trace, n_basic_blocks);
  n_traces = 0;
  find_traces (&n_traces, traces);
  connect_traces (n_traces, traces);
  FREE (traces);
  FREE (bbd);

  if (dump_file)
    dump_flow_info (dump_file, dump_flags);

  cfg_layout_finalize ();
  if (flag_reorder_blocks_and_partition)
    verify_hot_cold_block_grouping ();
}

/* Determine which partition the first basic block in the function
   belongs to, then find the first basic block in the current function
   that belongs to a different section, and insert a
   NOTE_INSN_SWITCH_TEXT_SECTIONS note immediately before it in the
   instruction stream.  When writing out the assembly code,
   encountering this note will make the compiler switch between the
   hot and cold text sections.  */

static void
insert_section_boundary_note (void)
{
  basic_block bb;
  rtx new_note;
  int first_partition = 0;

  if (flag_reorder_blocks_and_partition)
    FOR_EACH_BB (bb)
    {
      if (!first_partition)
	first_partition = BB_PARTITION (bb);
      if (BB_PARTITION (bb) != first_partition)
	{
	  new_note = emit_note_before (NOTE_INSN_SWITCH_TEXT_SECTIONS,
				       BB_HEAD (bb));
	  break;
	}
    }
}

/* Duplicate the blocks containing computed gotos.  This basically unfactors
   computed gotos that were factored early on in the compilation process to
   speed up edge based data flow.  We used to not unfactoring them again,
   which can seriously pessimize code with many computed jumps in the source
   code, such as interpreters.  See e.g. PR15242.  */

static bool
gate_duplicate_computed_gotos (void)
{
  return (optimize > 0 && flag_expensive_optimizations && !optimize_size);
}


static unsigned int
duplicate_computed_gotos (void)
{
  basic_block bb, new_bb;
  bitmap candidates;
  int max_size;

  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1)
    return 0;

  if (targetm.cannot_modify_jumps_p ())
    return 0;

  cfg_layout_initialize (0);

  /* We are estimating the length of uncond jump insn only once
     since the code for getting the insn length always returns
     the minimal length now.  */
  if (uncond_jump_length == 0)
    uncond_jump_length = get_uncond_jump_length ();

  max_size = uncond_jump_length * PARAM_VALUE (PARAM_MAX_GOTO_DUPLICATION_INSNS);
  candidates = BITMAP_ALLOC (NULL);

  /* Look for blocks that end in a computed jump, and see if such blocks
     are suitable for unfactoring.  If a block is a candidate for unfactoring,
     mark it in the candidates.  */
  FOR_EACH_BB (bb)
    {
      rtx insn;
      edge e;
      edge_iterator ei;
      int size, all_flags;

      /* Build the reorder chain for the original order of blocks.  */
      if (bb->next_bb != EXIT_BLOCK_PTR)
	bb->aux = bb->next_bb;

      /* Obviously the block has to end in a computed jump.  */
      if (!computed_jump_p (BB_END (bb)))
	continue;

      /* Only consider blocks that can be duplicated.  */
      if (find_reg_note (BB_END (bb), REG_CROSSING_JUMP, NULL_RTX)
	  || !can_duplicate_block_p (bb))
	continue;

      /* Make sure that the block is small enough.  */
      size = 0;
      FOR_BB_INSNS (bb, insn)
	if (INSN_P (insn))
	  {
	    size += get_attr_min_length (insn);
	    if (size > max_size)
	       break;
	  }
      if (size > max_size)
	continue;

      /* Final check: there must not be any incoming abnormal edges.  */
      all_flags = 0;
      FOR_EACH_EDGE (e, ei, bb->preds)
	all_flags |= e->flags;
      if (all_flags & EDGE_COMPLEX)
	continue;

      bitmap_set_bit (candidates, bb->index);
    }

  /* Nothing to do if there is no computed jump here.  */
  if (bitmap_empty_p (candidates))
    goto done;

  /* Duplicate computed gotos.  */
  FOR_EACH_BB (bb)
    {
      if (bb->il.rtl->visited)
	continue;

      bb->il.rtl->visited = 1;

      /* BB must have one outgoing edge.  That edge must not lead to
	 the exit block or the next block.
	 The destination must have more than one predecessor.  */
      if (!single_succ_p (bb)
	  || single_succ (bb) == EXIT_BLOCK_PTR
	  || single_succ (bb) == bb->next_bb
	  || single_pred_p (single_succ (bb)))
	continue;

      /* The successor block has to be a duplication candidate.  */
      if (!bitmap_bit_p (candidates, single_succ (bb)->index))
	continue;

      new_bb = duplicate_block (single_succ (bb), single_succ_edge (bb), bb);
      new_bb->aux = bb->aux;
      bb->aux = new_bb;
      new_bb->il.rtl->visited = 1;
    }

done:
  cfg_layout_finalize ();

  BITMAP_FREE (candidates);
  return 0;
}

struct tree_opt_pass pass_duplicate_computed_gotos =
{
  "compgotos",                          /* name */
  gate_duplicate_computed_gotos,        /* gate */
  duplicate_computed_gotos,             /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_REORDER_BLOCKS,                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  0                                     /* letter */
};


/* This function is the main 'entrance' for the optimization that
   partitions hot and cold basic blocks into separate sections of the
   .o file (to improve performance and cache locality).  Ideally it
   would be called after all optimizations that rearrange the CFG have
   been called.  However part of this optimization may introduce new
   register usage, so it must be called before register allocation has
   occurred.  This means that this optimization is actually called
   well before the optimization that reorders basic blocks (see
   function above).

   This optimization checks the feedback information to determine
   which basic blocks are hot/cold, updates flags on the basic blocks
   to indicate which section they belong in.  This information is
   later used for writing out sections in the .o file.  Because hot
   and cold sections can be arbitrarily large (within the bounds of
   memory), far beyond the size of a single function, it is necessary
   to fix up all edges that cross section boundaries, to make sure the
   instructions used can actually span the required distance.  The
   fixes are described below.

   Fall-through edges must be changed into jumps; it is not safe or
   legal to fall through across a section boundary.  Whenever a
   fall-through edge crossing a section boundary is encountered, a new
   basic block is inserted (in the same section as the fall-through
   source), and the fall through edge is redirected to the new basic
   block.  The new basic block contains an unconditional jump to the
   original fall-through target.  (If the unconditional jump is
   insufficient to cross section boundaries, that is dealt with a
   little later, see below).

   In order to deal with architectures that have short conditional
   branches (which cannot span all of memory) we take any conditional
   jump that attempts to cross a section boundary and add a level of
   indirection: it becomes a conditional jump to a new basic block, in
   the same section.  The new basic block contains an unconditional
   jump to the original target, in the other section.

   For those architectures whose unconditional branch is also
   incapable of reaching all of memory, those unconditional jumps are
   converted into indirect jumps, through a register.

   IMPORTANT NOTE: This optimization causes some messy interactions
   with the cfg cleanup optimizations; those optimizations want to
   merge blocks wherever possible, and to collapse indirect jump
   sequences (change "A jumps to B jumps to C" directly into "A jumps
   to C").  Those optimizations can undo the jump fixes that
   partitioning is required to make (see above), in order to ensure
   that jumps attempting to cross section boundaries are really able
   to cover whatever distance the jump requires (on many architectures
   conditional or unconditional jumps are not able to reach all of
   memory).  Therefore tests have to be inserted into each such
   optimization to make sure that it does not undo stuff necessary to
   cross partition boundaries.  This would be much less of a problem
   if we could perform this optimization later in the compilation, but
   unfortunately the fact that we may need to create indirect jumps
   (through registers) requires that this optimization be performed
   before register allocation.  */

static void
partition_hot_cold_basic_blocks (void)
{
  basic_block cur_bb;
  edge *crossing_edges;
  int n_crossing_edges;
  int max_edges = 2 * last_basic_block;

  if (n_basic_blocks <= NUM_FIXED_BLOCKS + 1)
    return;

  crossing_edges = XCNEWVEC (edge, max_edges);

  cfg_layout_initialize (0);

  FOR_EACH_BB (cur_bb)
    if (cur_bb->index >= NUM_FIXED_BLOCKS
	&& cur_bb->next_bb->index >= NUM_FIXED_BLOCKS)
      cur_bb->aux = cur_bb->next_bb;

  find_rarely_executed_basic_blocks_and_crossing_edges (crossing_edges,
							&n_crossing_edges,
							&max_edges);

  if (n_crossing_edges > 0)
    fix_edges_for_rarely_executed_code (crossing_edges, n_crossing_edges);

  free (crossing_edges);

  cfg_layout_finalize();
}

static bool
gate_handle_reorder_blocks (void)
{
  return (optimize > 0);
}


/* Reorder basic blocks.  */
static unsigned int
rest_of_handle_reorder_blocks (void)
{
  bool changed;
  unsigned int liveness_flags;

  /* Last attempt to optimize CFG, as scheduling, peepholing and insn
     splitting possibly introduced more crossjumping opportunities.  */
  liveness_flags = (!HAVE_conditional_execution ? CLEANUP_UPDATE_LIFE : 0);
  changed = cleanup_cfg (CLEANUP_EXPENSIVE | liveness_flags);

  if (flag_sched2_use_traces && flag_schedule_insns_after_reload)
    {
      timevar_push (TV_TRACER);
      tracer (liveness_flags);
      timevar_pop (TV_TRACER);
    }

  if (flag_reorder_blocks || flag_reorder_blocks_and_partition)
    reorder_basic_blocks (liveness_flags);
  if (flag_reorder_blocks || flag_reorder_blocks_and_partition
      || (flag_sched2_use_traces && flag_schedule_insns_after_reload))
    changed |= cleanup_cfg (CLEANUP_EXPENSIVE | liveness_flags);

  /* On conditional execution targets we can not update the life cheaply, so
     we deffer the updating to after both cleanups.  This may lose some cases
     but should not be terribly bad.  */
  if (changed && HAVE_conditional_execution)
    update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
		      PROP_DEATH_NOTES);

  /* Add NOTE_INSN_SWITCH_TEXT_SECTIONS notes.  */
  insert_section_boundary_note ();
  return 0;
}

struct tree_opt_pass pass_reorder_blocks =
{
  "bbro",                               /* name */
  gate_handle_reorder_blocks,           /* gate */
  rest_of_handle_reorder_blocks,        /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_REORDER_BLOCKS,                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'B'                                   /* letter */
};

static bool
gate_handle_partition_blocks (void)
{
  /* The optimization to partition hot/cold basic blocks into separate
     sections of the .o file does not work well with linkonce or with
     user defined section attributes.  Don't call it if either case
     arises.  */

  return (flag_reorder_blocks_and_partition
	  && !DECL_ONE_ONLY (current_function_decl)
	  && !user_defined_section_attribute);
}

/* Partition hot and cold basic blocks.  */
static unsigned int
rest_of_handle_partition_blocks (void)
{
  no_new_pseudos = 0;
  partition_hot_cold_basic_blocks ();
  allocate_reg_life_data ();
  update_life_info (NULL, UPDATE_LIFE_GLOBAL_RM_NOTES,
		    PROP_LOG_LINKS | PROP_REG_INFO | PROP_DEATH_NOTES);
  no_new_pseudos = 1;
  return 0;
}

struct tree_opt_pass pass_partition_blocks =
{
  "bbpart",                             /* name */
  gate_handle_partition_blocks,         /* gate */
  rest_of_handle_partition_blocks,      /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_REORDER_BLOCKS,                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  0                                     /* letter */
};


