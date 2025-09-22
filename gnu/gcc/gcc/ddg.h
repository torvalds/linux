/* DDG - Data Dependence Graph - interface.
   Copyright (C) 2004
   Free Software Foundation, Inc.
   Contributed by Ayal Zaks and Mustafa Hagog <zaks,mustafa@il.ibm.com>

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

#ifndef GCC_DDG_H
#define GCC_DDG_H

/* For sbitmap.  */
#include "sbitmap.h"
/* For basic_block.  */
#include "basic-block.h"
/* For struct df.  */
#include "df.h"
 
typedef struct ddg_node *ddg_node_ptr;
typedef struct ddg_edge *ddg_edge_ptr;
typedef struct ddg *ddg_ptr;
typedef struct ddg_scc *ddg_scc_ptr;
typedef struct ddg_all_sccs *ddg_all_sccs_ptr;

typedef enum {TRUE_DEP, OUTPUT_DEP, ANTI_DEP} dep_type;
typedef enum {REG_OR_MEM_DEP, REG_DEP, MEM_DEP, REG_AND_MEM_DEP}
	     dep_data_type;

/* The following two macros enables direct access to the successors and
   predecessors bitmaps held in each ddg_node.  Do not make changes to
   these bitmaps, unless you want to change the DDG.  */
#define NODE_SUCCESSORS(x)  ((x)->successors)
#define NODE_PREDECESSORS(x)  ((x)->predecessors)

/* A structure that represents a node in the DDG.  */
struct ddg_node
{
  /* Each node has a unique CUID index.  These indices increase monotonically
     (according to the order of the corresponding INSN in the BB), starting
     from 0 with no gaps.  */
  int cuid;

  /* The insn represented by the node.  */
  rtx insn;

  /* A note preceding INSN (or INSN itself), such that all insns linked
     from FIRST_NOTE until INSN (inclusive of both) are moved together
     when reordering the insns.  This takes care of notes that should
     continue to precede INSN.  */
  rtx first_note;

  /* Incoming and outgoing dependency edges.  */
  ddg_edge_ptr in;
  ddg_edge_ptr out;

  /* Each bit corresponds to a ddg_node according to its cuid, and is
     set iff the node is a successor/predecessor of "this" node.  */
  sbitmap successors;
  sbitmap predecessors;

  /* For general use by algorithms manipulating the ddg.  */
  union {
    int count;
    void *info;
  } aux;
};

/* A structure that represents an edge in the DDG.  */
struct ddg_edge
{
  /* The source and destination nodes of the dependency edge.  */
  ddg_node_ptr src;
  ddg_node_ptr dest;

  /* TRUE, OUTPUT or ANTI dependency.  */
  dep_type type;

  /* REG or MEM dependency.  */
  dep_data_type data_type;

  /* Latency of the dependency.  */
  int latency;

  /* The distance: number of loop iterations the dependency crosses.  */
  int distance;

  /* The following two fields are used to form a linked list of the in/out
     going edges to/from each node.  */
  ddg_edge_ptr next_in;
  ddg_edge_ptr next_out;

  /* For general use by algorithms manipulating the ddg.  */
  union {
    int count;
    void *info;
  } aux;
};

/* This structure holds the Data Dependence Graph for a basic block.  */
struct ddg
{
  /* The basic block for which this DDG is built.  */
  basic_block bb;

  /* Number of instructions in the basic block.  */
  int num_nodes;

  /* Number of load/store instructions in the BB - statistics.  */
  int num_loads;
  int num_stores;

  /* This array holds the nodes in the graph; it is indexed by the node
     cuid, which follows the order of the instructions in the BB.  */
  ddg_node_ptr nodes;

  /* The branch closing the loop.  */
  ddg_node_ptr closing_branch;

  /* Build dependence edges for closing_branch, when set.  In certain cases,
     the closing branch can be dealt with separately from the insns of the
     loop, and then no such deps are needed.  */
  int closing_branch_deps;

  /* Array and number of backarcs (edges with distance > 0) in the DDG.  */
  ddg_edge_ptr *backarcs;
  int num_backarcs;
};


/* Holds information on an SCC (Strongly Connected Component) of the DDG.  */
struct ddg_scc
{
  /* A bitmap that represents the nodes of the DDG that are in the SCC.  */
  sbitmap nodes;

  /* Array and number of backarcs (edges with distance > 0) in the SCC.  */
  ddg_edge_ptr *backarcs;
  int num_backarcs;

  /* The maximum of (total_latency/total_distance) over all cycles in SCC.  */
  int recurrence_length;
};

/* This structure holds the SCCs of the DDG.  */
struct ddg_all_sccs
{
  /* Array that holds the SCCs in the DDG, and their number.  */
  ddg_scc_ptr *sccs;
  int num_sccs;

  ddg_ptr ddg;
};


ddg_ptr create_ddg (basic_block, struct df *, int closing_branch_deps);
void free_ddg (ddg_ptr);

void print_ddg (FILE *, ddg_ptr);
void vcg_print_ddg (FILE *, ddg_ptr);
void print_ddg_edge (FILE *, ddg_edge_ptr);

ddg_node_ptr get_node_of_insn (ddg_ptr, rtx);

void find_successors (sbitmap result, ddg_ptr, sbitmap);
void find_predecessors (sbitmap result, ddg_ptr, sbitmap);

ddg_all_sccs_ptr create_ddg_all_sccs (ddg_ptr);
void free_ddg_all_sccs (ddg_all_sccs_ptr);

int find_nodes_on_paths (sbitmap result, ddg_ptr, sbitmap from, sbitmap to);
int longest_simple_path (ddg_ptr, int from, int to, sbitmap via);

#endif /* GCC_DDG_H */
