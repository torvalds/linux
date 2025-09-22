/* Output routines for graphical representation.
   Copyright (C) 1998, 1999, 2000, 2001, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Ulrich Drepper <drepper@cygnus.com>, 1998.

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

#include <config.h>
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "flags.h"
#include "output.h"
#include "function.h"
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"
#include "toplev.h"
#include "graph.h"

static const char *const graph_ext[] =
{
  /* no_graph */ "",
  /* vcg */      ".vcg",
};

static void start_fct (FILE *);
static void start_bb (FILE *, int);
static void node_data (FILE *, rtx);
static void draw_edge (FILE *, int, int, int, int);
static void end_fct (FILE *);
static void end_bb (FILE *);

/* Output text for new basic block.  */
static void
start_fct (FILE *fp)
{
  switch (graph_dump_format)
    {
    case vcg:
      fprintf (fp, "\
graph: { title: \"%s\"\nfolding: 1\nhidden: 2\nnode: { title: \"%s.0\" }\n",
	       current_function_name (), current_function_name ());
      break;
    case no_graph:
      break;
    }
}

static void
start_bb (FILE *fp, int bb)
{
#if 0
  reg_set_iterator rsi;
#endif

  switch (graph_dump_format)
    {
    case vcg:
      fprintf (fp, "\
graph: {\ntitle: \"%s.BB%d\"\nfolding: 1\ncolor: lightblue\n\
label: \"basic block %d",
	       current_function_name (), bb, bb);
      break;
    case no_graph:
      break;
    }

#if 0
  /* FIXME Should this be printed?  It makes the graph significantly larger.  */

  /* Print the live-at-start register list.  */
  fputc ('\n', fp);
  EXECUTE_IF_SET_IN_REG_SET (basic_block_live_at_start[bb], 0, i, rsi)
    {
      fprintf (fp, " %d", i);
      if (i < FIRST_PSEUDO_REGISTER)
	fprintf (fp, " [%s]", reg_names[i]);
    }
#endif

  switch (graph_dump_format)
    {
    case vcg:
      fputs ("\"\n\n", fp);
      break;
    case no_graph:
      break;
    }
}

static void
node_data (FILE *fp, rtx tmp_rtx)
{
  if (PREV_INSN (tmp_rtx) == 0)
    {
      /* This is the first instruction.  Add an edge from the starting
	 block.  */
      switch (graph_dump_format)
	{
	case vcg:
	  fprintf (fp, "\
edge: { sourcename: \"%s.0\" targetname: \"%s.%d\" }\n",
		   current_function_name (),
		   current_function_name (), XINT (tmp_rtx, 0));
	  break;
	case no_graph:
	  break;
	}
    }

  switch (graph_dump_format)
    {
    case vcg:
      fprintf (fp, "node: {\n  title: \"%s.%d\"\n  color: %s\n  \
label: \"%s %d\n",
	       current_function_name (), XINT (tmp_rtx, 0),
	       NOTE_P (tmp_rtx) ? "lightgrey"
	       : NONJUMP_INSN_P (tmp_rtx) ? "green"
	       : JUMP_P (tmp_rtx) ? "darkgreen"
	       : CALL_P (tmp_rtx) ? "darkgreen"
	       : LABEL_P (tmp_rtx) ?  "\
darkgrey\n  shape: ellipse" : "white",
	       GET_RTX_NAME (GET_CODE (tmp_rtx)), XINT (tmp_rtx, 0));
      break;
    case no_graph:
      break;
    }

  /* Print the RTL.  */
  if (NOTE_P (tmp_rtx))
    {
      const char *name = "";
      if (NOTE_LINE_NUMBER (tmp_rtx) < 0)
	name =  GET_NOTE_INSN_NAME (NOTE_LINE_NUMBER (tmp_rtx));
      fprintf (fp, " %s", name);
    }
  else if (INSN_P (tmp_rtx))
    print_rtl_single (fp, PATTERN (tmp_rtx));
  else
    print_rtl_single (fp, tmp_rtx);

  switch (graph_dump_format)
    {
    case vcg:
      fputs ("\"\n}\n", fp);
      break;
    case no_graph:
      break;
    }
}

static void
draw_edge (FILE *fp, int from, int to, int bb_edge, int class)
{
  const char * color;
  switch (graph_dump_format)
    {
    case vcg:
      color = "";
      if (class == 2)
	color = "color: red ";
      else if (bb_edge)
	color = "color: blue ";
      else if (class == 3)
	color = "color: green ";
      fprintf (fp,
	       "edge: { sourcename: \"%s.%d\" targetname: \"%s.%d\" %s",
	       current_function_name (), from,
	       current_function_name (), to, color);
      if (class)
	fprintf (fp, "class: %d ", class);
      fputs ("}\n", fp);
      break;
    case no_graph:
      break;
    }
}

static void
end_bb (FILE *fp)
{
  switch (graph_dump_format)
    {
    case vcg:
      fputs ("}\n", fp);
      break;
    case no_graph:
      break;
    }
}

static void
end_fct (FILE *fp)
{
  switch (graph_dump_format)
    {
    case vcg:
      fprintf (fp, "node: { title: \"%s.999999\" label: \"END\" }\n}\n",
	       current_function_name ());
      break;
    case no_graph:
      break;
    }
}

/* Like print_rtl, but also print out live information for the start of each
   basic block.  */
void
print_rtl_graph_with_bb (const char *base, rtx rtx_first)
{
  rtx tmp_rtx;
  size_t namelen = strlen (base);
  size_t extlen = strlen (graph_ext[graph_dump_format]) + 1;
  char *buf = alloca (namelen + extlen);
  FILE *fp;

  if (basic_block_info == NULL)
    return;

  memcpy (buf, base, namelen);
  memcpy (buf + namelen, graph_ext[graph_dump_format], extlen);

  fp = fopen (buf, "a");
  if (fp == NULL)
    return;

  if (rtx_first == 0)
    fprintf (fp, "(nil)\n");
  else
    {
      enum bb_state { NOT_IN_BB, IN_ONE_BB, IN_MULTIPLE_BB };
      int max_uid = get_max_uid ();
      int *start = XNEWVEC (int, max_uid);
      int *end = XNEWVEC (int, max_uid);
      enum bb_state *in_bb_p = XNEWVEC (enum bb_state, max_uid);
      basic_block bb;
      int i;

      for (i = 0; i < max_uid; ++i)
	{
	  start[i] = end[i] = -1;
	  in_bb_p[i] = NOT_IN_BB;
	}

      FOR_EACH_BB_REVERSE (bb)
	{
	  rtx x;
	  start[INSN_UID (BB_HEAD (bb))] = bb->index;
	  end[INSN_UID (BB_END (bb))] = bb->index;
	  for (x = BB_HEAD (bb); x != NULL_RTX; x = NEXT_INSN (x))
	    {
	      in_bb_p[INSN_UID (x)]
		= (in_bb_p[INSN_UID (x)] == NOT_IN_BB)
		 ? IN_ONE_BB : IN_MULTIPLE_BB;
	      if (x == BB_END (bb))
		break;
	    }
	}

      /* Tell print-rtl that we want graph output.  */
      dump_for_graph = 1;

      /* Start new function.  */
      start_fct (fp);

      for (tmp_rtx = NEXT_INSN (rtx_first); NULL != tmp_rtx;
	   tmp_rtx = NEXT_INSN (tmp_rtx))
	{
	  int edge_printed = 0;
	  rtx next_insn;

	  if (start[INSN_UID (tmp_rtx)] < 0 && end[INSN_UID (tmp_rtx)] < 0)
	    {
	      if (BARRIER_P (tmp_rtx))
		continue;
	      if (NOTE_P (tmp_rtx)
		  && (1 || in_bb_p[INSN_UID (tmp_rtx)] == NOT_IN_BB))
		continue;
	    }

	  if ((i = start[INSN_UID (tmp_rtx)]) >= 0)
	    {
	      /* We start a subgraph for each basic block.  */
	      start_bb (fp, i);

	      if (i == 0)
		draw_edge (fp, 0, INSN_UID (tmp_rtx), 1, 0);
	    }

	  /* Print the data for this node.  */
	  node_data (fp, tmp_rtx);
	  next_insn = next_nonnote_insn (tmp_rtx);

	  if ((i = end[INSN_UID (tmp_rtx)]) >= 0)
	    {
	      edge e;
	      edge_iterator ei;

	      bb = BASIC_BLOCK (i);

	      /* End of the basic block.  */
	      end_bb (fp);

	      /* Now specify the edges to all the successors of this
		 basic block.  */
	      FOR_EACH_EDGE (e, ei, bb->succs)
		{
		  if (e->dest != EXIT_BLOCK_PTR)
		    {
		      rtx block_head = BB_HEAD (e->dest);

		      draw_edge (fp, INSN_UID (tmp_rtx),
				 INSN_UID (block_head),
				 next_insn != block_head,
				 (e->flags & EDGE_ABNORMAL ? 2 : 0));

		      if (block_head == next_insn)
			edge_printed = 1;
		    }
		  else
		    {
		      draw_edge (fp, INSN_UID (tmp_rtx), 999999,
				 next_insn != 0,
				 (e->flags & EDGE_ABNORMAL ? 2 : 0));

		      if (next_insn == 0)
			edge_printed = 1;
		    }
		}
	    }

	  if (!edge_printed)
	    {
	      /* Don't print edges to barriers.  */
	      if (next_insn == 0
		  || !BARRIER_P (next_insn))
		draw_edge (fp, XINT (tmp_rtx, 0),
			   next_insn ? INSN_UID (next_insn) : 999999, 0, 0);
	      else
		{
		  /* We draw the remaining edges in class 3.  We have
		     to skip over the barrier since these nodes are
		     not printed at all.  */
		  do
		    next_insn = NEXT_INSN (next_insn);
		  while (next_insn
			 && (NOTE_P (next_insn)
			     || BARRIER_P (next_insn)));

		  draw_edge (fp, XINT (tmp_rtx, 0),
			     next_insn ? INSN_UID (next_insn) : 999999, 0, 3);
		}
	    }
	}

      dump_for_graph = 0;

      end_fct (fp);

      /* Clean up.  */
      free (start);
      free (end);
      free (in_bb_p);
    }

  fclose (fp);
}


/* Similar as clean_dump_file, but this time for graph output files.  */

void
clean_graph_dump_file (const char *base)
{
  size_t namelen = strlen (base);
  size_t extlen = strlen (graph_ext[graph_dump_format]) + 1;
  char *buf = alloca (namelen + extlen);
  FILE *fp;

  memcpy (buf, base, namelen);
  memcpy (buf + namelen, graph_ext[graph_dump_format], extlen);

  fp = fopen (buf, "w");

  if (fp == NULL)
    fatal_error ("can't open %s: %m", buf);

  gcc_assert (graph_dump_format == vcg);
  fputs ("graph: {\nport_sharing: no\n", fp);

  fclose (fp);
}


/* Do final work on the graph output file.  */
void
finish_graph_dump_file (const char *base)
{
  size_t namelen = strlen (base);
  size_t extlen = strlen (graph_ext[graph_dump_format]) + 1;
  char *buf = alloca (namelen + extlen);
  FILE *fp;

  memcpy (buf, base, namelen);
  memcpy (buf + namelen, graph_ext[graph_dump_format], extlen);

  fp = fopen (buf, "a");
  if (fp != NULL)
    {
      gcc_assert (graph_dump_format == vcg);
      fputs ("}\n", fp);
      fclose (fp);
    }
}
