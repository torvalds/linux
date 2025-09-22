/* Reassociation for trees.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dan@dberlin.org>

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
#include "errors.h"
#include "ggc.h"
#include "tree.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-inline.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-dump.h"
#include "timevar.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "alloc-pool.h"
#include "vec.h"
#include "langhooks.h"

/*  This is a simple global reassociation pass.  It is, in part, based
    on the LLVM pass of the same name (They do some things more/less
    than we do, in different orders, etc).

    It consists of five steps:

    1. Breaking up subtract operations into addition + negate, where
    it would promote the reassociation of adds.

    2. Left linearization of the expression trees, so that (A+B)+(C+D)
    becomes (((A+B)+C)+D), which is easier for us to rewrite later.
    During linearization, we place the operands of the binary
    expressions into a vector of operand_entry_t

    3. Optimization of the operand lists, eliminating things like a +
    -a, a & a, etc.

    4. Rewrite the expression trees we linearized and optimized so
    they are in proper rank order.

    5. Repropagate negates, as nothing else will clean it up ATM.

    A bit of theory on #4, since nobody seems to write anything down
    about why it makes sense to do it the way they do it:

    We could do this much nicer theoretically, but don't (for reasons
    explained after how to do it theoretically nice :P).

    In order to promote the most redundancy elimination, you want
    binary expressions whose operands are the same rank (or
    preferably, the same value) exposed to the redundancy eliminator,
    for possible elimination.

    So the way to do this if we really cared, is to build the new op
    tree from the leaves to the roots, merging as you go, and putting the
    new op on the end of the worklist, until you are left with one
    thing on the worklist.

    IE if you have to rewrite the following set of operands (listed with
    rank in parentheses), with opcode PLUS_EXPR:

    a (1),  b (1),  c (1),  d (2), e (2)


    We start with our merge worklist empty, and the ops list with all of
    those on it.

    You want to first merge all leaves of the same rank, as much as
    possible.

    So first build a binary op of

    mergetmp = a + b, and put "mergetmp" on the merge worklist.

    Because there is no three operand form of PLUS_EXPR, c is not going to
    be exposed to redundancy elimination as a rank 1 operand.

    So you might as well throw it on the merge worklist (you could also
    consider it to now be a rank two operand, and merge it with d and e,
    but in this case, you then have evicted e from a binary op. So at
    least in this situation, you can't win.)

    Then build a binary op of d + e
    mergetmp2 = d + e

    and put mergetmp2 on the merge worklist.
    
    so merge worklist = {mergetmp, c, mergetmp2}
    
    Continue building binary ops of these operations until you have only
    one operation left on the worklist.
    
    So we have
    
    build binary op
    mergetmp3 = mergetmp + c
    
    worklist = {mergetmp2, mergetmp3}
    
    mergetmp4 = mergetmp2 + mergetmp3
    
    worklist = {mergetmp4}
    
    because we have one operation left, we can now just set the original
    statement equal to the result of that operation.
    
    This will at least expose a + b  and d + e to redundancy elimination
    as binary operations.
    
    For extra points, you can reuse the old statements to build the
    mergetmps, since you shouldn't run out.

    So why don't we do this?
    
    Because it's expensive, and rarely will help.  Most trees we are
    reassociating have 3 or less ops.  If they have 2 ops, they already
    will be written into a nice single binary op.  If you have 3 ops, a
    single simple check suffices to tell you whether the first two are of the
    same rank.  If so, you know to order it

    mergetmp = op1 + op2
    newstmt = mergetmp + op3
    
    instead of
    mergetmp = op2 + op3
    newstmt = mergetmp + op1
    
    If all three are of the same rank, you can't expose them all in a
    single binary operator anyway, so the above is *still* the best you
    can do.
    
    Thus, this is what we do.  When we have three ops left, we check to see
    what order to put them in, and call it a day.  As a nod to vector sum
    reduction, we check if any of ops are a really a phi node that is a
    destructive update for the associating op, and keep the destructive
    update together for vector sum reduction recognition.  */


/* Statistics */
static struct
{
  int linearized;
  int constants_eliminated;
  int ops_eliminated;
  int rewritten;
} reassociate_stats;

/* Operator, rank pair.  */
typedef struct operand_entry
{
  unsigned int rank;
  tree op;
} *operand_entry_t;

static alloc_pool operand_entry_pool;


/* Starting rank number for a given basic block, so that we can rank
   operations using unmovable instructions in that BB based on the bb
   depth.  */
static unsigned int *bb_rank;

/* Operand->rank hashtable.  */
static htab_t operand_rank;


/* Look up the operand rank structure for expression E.  */

static operand_entry_t
find_operand_rank (tree e)
{
  void **slot;
  struct operand_entry vrd;

  vrd.op = e;
  slot = htab_find_slot (operand_rank, &vrd, NO_INSERT);
  if (!slot)
    return NULL;
  return ((operand_entry_t) *slot);
}

/* Insert {E,RANK} into the operand rank hashtable.  */

static void
insert_operand_rank (tree e, unsigned int rank)
{
  void **slot;
  operand_entry_t new_pair = pool_alloc (operand_entry_pool);

  new_pair->op = e;
  new_pair->rank = rank;
  slot = htab_find_slot (operand_rank, new_pair, INSERT);
  gcc_assert (*slot == NULL);
  *slot = new_pair;
}

/* Return the hash value for a operand rank structure  */

static hashval_t
operand_entry_hash (const void *p)
{
  const operand_entry_t vr = (operand_entry_t) p;
  return iterative_hash_expr (vr->op, 0);
}

/* Return true if two operand rank structures are equal.  */

static int
operand_entry_eq (const void *p1, const void *p2)
{
  const operand_entry_t vr1 = (operand_entry_t) p1;
  const operand_entry_t vr2 = (operand_entry_t) p2;
  return vr1->op == vr2->op;
}

/* Given an expression E, return the rank of the expression.  */

static unsigned int
get_rank (tree e)
{
  operand_entry_t vr;

  /* Constants have rank 0.  */
  if (is_gimple_min_invariant (e))
    return 0;

  /* SSA_NAME's have the rank of the expression they are the result
     of.
     For globals and uninitialized values, the rank is 0.
     For function arguments, use the pre-setup rank.
     For PHI nodes, stores, asm statements, etc, we use the rank of
     the BB.
     For simple operations, the rank is the maximum rank of any of
     its operands, or the bb_rank, whichever is less.
     I make no claims that this is optimal, however, it gives good
     results.  */

  if (TREE_CODE (e) == SSA_NAME)
    {
      tree stmt;
      tree rhs;
      unsigned int rank, maxrank;
      int i;

      if (TREE_CODE (SSA_NAME_VAR (e)) == PARM_DECL
	  && e == default_def (SSA_NAME_VAR (e)))
	return find_operand_rank (e)->rank;

      stmt = SSA_NAME_DEF_STMT (e);
      if (bb_for_stmt (stmt) == NULL)
	return 0;

      if (TREE_CODE (stmt) != MODIFY_EXPR
	  || !ZERO_SSA_OPERANDS (stmt, SSA_OP_VIRTUAL_DEFS))
	return bb_rank[bb_for_stmt (stmt)->index];

      /* If we already have a rank for this expression, use that.  */
      vr = find_operand_rank (e);
      if (vr)
	return vr->rank;

      /* Otherwise, find the maximum rank for the operands, or the bb
	 rank, whichever is less.   */
      rank = 0;
      maxrank = bb_rank[bb_for_stmt(stmt)->index];
      rhs = TREE_OPERAND (stmt, 1);
      if (TREE_CODE_LENGTH (TREE_CODE (rhs)) == 0)
	rank = MAX (rank, get_rank (rhs));
      else
	{
	  for (i = 0;
	       i < TREE_CODE_LENGTH (TREE_CODE (rhs))
		 && TREE_OPERAND (rhs, i)
		 && rank != maxrank;
	       i++)
	    rank = MAX(rank, get_rank (TREE_OPERAND (rhs, i)));
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Rank for ");
	  print_generic_expr (dump_file, e, 0);
	  fprintf (dump_file, " is %d\n", (rank + 1));
	}

      /* Note the rank in the hashtable so we don't recompute it.  */
      insert_operand_rank (e, (rank + 1));
      return (rank + 1);
    }

  /* Globals, etc,  are rank 0 */
  return 0;
}

DEF_VEC_P(operand_entry_t);
DEF_VEC_ALLOC_P(operand_entry_t, heap);

/* We want integer ones to end up last no matter what, since they are
   the ones we can do the most with.  */
#define INTEGER_CONST_TYPE 1 << 3
#define FLOAT_CONST_TYPE 1 << 2
#define OTHER_CONST_TYPE 1 << 1

/* Classify an invariant tree into integer, float, or other, so that
   we can sort them to be near other constants of the same type.  */
static inline int
constant_type (tree t)
{
  if (INTEGRAL_TYPE_P (TREE_TYPE (t)))
    return INTEGER_CONST_TYPE;
  else if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (t)))
    return FLOAT_CONST_TYPE;
  else
    return OTHER_CONST_TYPE;
}

/* qsort comparison function to sort operand entries PA and PB by rank
   so that the sorted array is ordered by rank in decreasing order.  */
static int
sort_by_operand_rank (const void *pa, const void *pb)
{
  const operand_entry_t oea = *(const operand_entry_t *)pa;
  const operand_entry_t oeb = *(const operand_entry_t *)pb;

  /* It's nicer for optimize_expression if constants that are likely
     to fold when added/multiplied//whatever are put next to each
     other.  Since all constants have rank 0, order them by type.  */
  if (oeb->rank == 0 &&  oea->rank == 0)
    return constant_type (oeb->op) - constant_type (oea->op);

  /* Lastly, make sure the versions that are the same go next to each
     other.  We use SSA_NAME_VERSION because it's stable.  */
  if ((oeb->rank - oea->rank == 0)
      && TREE_CODE (oea->op) == SSA_NAME
      && TREE_CODE (oeb->op) == SSA_NAME)
    return SSA_NAME_VERSION (oeb->op) - SSA_NAME_VERSION (oea->op);

  return oeb->rank - oea->rank;
}

/* Add an operand entry to *OPS for the tree operand OP.  */

static void
add_to_ops_vec (VEC(operand_entry_t, heap) **ops, tree op)
{
  operand_entry_t oe = pool_alloc (operand_entry_pool);

  oe->op = op;
  oe->rank = get_rank (op);
  VEC_safe_push (operand_entry_t, heap, *ops, oe);
}

/* Return true if STMT is reassociable operation containing a binary
   operation with tree code CODE.  */

static bool
is_reassociable_op (tree stmt, enum tree_code code)
{
  if (!IS_EMPTY_STMT (stmt)
      && TREE_CODE (stmt) == MODIFY_EXPR
      && TREE_CODE (TREE_OPERAND (stmt, 1)) == code
      && has_single_use (TREE_OPERAND (stmt, 0)))
    return true;
  return false;
}


/* Given NAME, if NAME is defined by a unary operation OPCODE, return the
   operand of the negate operation.  Otherwise, return NULL.  */

static tree
get_unary_op (tree name, enum tree_code opcode)
{
  tree stmt = SSA_NAME_DEF_STMT (name);
  tree rhs;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return NULL_TREE;

  rhs = TREE_OPERAND (stmt, 1);
  if (TREE_CODE (rhs) == opcode)
    return TREE_OPERAND (rhs, 0);
  return NULL_TREE;
}

/* If CURR and LAST are a pair of ops that OPCODE allows us to
   eliminate through equivalences, do so, remove them from OPS, and
   return true.  Otherwise, return false.  */

static bool
eliminate_duplicate_pair (enum tree_code opcode,
			  VEC (operand_entry_t, heap) **ops,
			  bool *all_done,
			  unsigned int i,
			  operand_entry_t curr,
			  operand_entry_t last)
{

  /* If we have two of the same op, and the opcode is & |, min, or max,
     we can eliminate one of them.
     If we have two of the same op, and the opcode is ^, we can
     eliminate both of them.  */

  if (last && last->op == curr->op)
    {
      switch (opcode)
	{
	case MAX_EXPR:
	case MIN_EXPR:
	case BIT_IOR_EXPR:
	case BIT_AND_EXPR:
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Equivalence: ");
	      print_generic_expr (dump_file, curr->op, 0);
	      fprintf (dump_file, " [&|minmax] ");
	      print_generic_expr (dump_file, last->op, 0);
	      fprintf (dump_file, " -> ");
	      print_generic_stmt (dump_file, last->op, 0);
	    }

	  VEC_ordered_remove (operand_entry_t, *ops, i);
	  reassociate_stats.ops_eliminated ++;

	  return true;

	case BIT_XOR_EXPR:
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Equivalence: ");
	      print_generic_expr (dump_file, curr->op, 0);
	      fprintf (dump_file, " ^ ");
	      print_generic_expr (dump_file, last->op, 0);
	      fprintf (dump_file, " -> nothing\n");
	    }

	  reassociate_stats.ops_eliminated += 2;

	  if (VEC_length (operand_entry_t, *ops) == 2)
	    {
	      VEC_free (operand_entry_t, heap, *ops);
	      *ops = NULL;
	      add_to_ops_vec (ops, fold_convert (TREE_TYPE (last->op), 
						 integer_zero_node));
	      *all_done = true;
	    }
	  else
	    {
	      VEC_ordered_remove (operand_entry_t, *ops, i-1);
	      VEC_ordered_remove (operand_entry_t, *ops, i-1);
	    }

	  return true;

	default:
	  break;
	}
    }
  return false;
}

/* If OPCODE is PLUS_EXPR, CURR->OP is really a negate expression,
   look in OPS for a corresponding positive operation to cancel it
   out.  If we find one, remove the other from OPS, replace
   OPS[CURRINDEX] with 0, and return true.  Otherwise, return
   false. */

static bool
eliminate_plus_minus_pair (enum tree_code opcode,
			   VEC (operand_entry_t, heap) **ops,
			   unsigned int currindex,
			   operand_entry_t curr)
{
  tree negateop;
  unsigned int i;
  operand_entry_t oe;

  if (opcode != PLUS_EXPR || TREE_CODE (curr->op) != SSA_NAME)
    return false;

  negateop = get_unary_op (curr->op, NEGATE_EXPR);
  if (negateop == NULL_TREE)
    return false;

  /* Any non-negated version will have a rank that is one less than
     the current rank.  So once we hit those ranks, if we don't find
     one, we can stop.  */

  for (i = currindex + 1;
       VEC_iterate (operand_entry_t, *ops, i, oe)
       && oe->rank >= curr->rank - 1 ;
       i++)
    {
      if (oe->op == negateop)
	{

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Equivalence: ");
	      print_generic_expr (dump_file, negateop, 0);
	      fprintf (dump_file, " + -");
	      print_generic_expr (dump_file, oe->op, 0);
	      fprintf (dump_file, " -> 0\n");
	    }

	  VEC_ordered_remove (operand_entry_t, *ops, i);
	  add_to_ops_vec (ops, fold_convert(TREE_TYPE (oe->op), 
					    integer_zero_node));
	  VEC_ordered_remove (operand_entry_t, *ops, currindex);
	  reassociate_stats.ops_eliminated ++;

	  return true;
	}
    }

  return false;
}

/* If OPCODE is BIT_IOR_EXPR, BIT_AND_EXPR, and, CURR->OP is really a
   bitwise not expression, look in OPS for a corresponding operand to
   cancel it out.  If we find one, remove the other from OPS, replace
   OPS[CURRINDEX] with 0, and return true.  Otherwise, return
   false. */

static bool
eliminate_not_pairs (enum tree_code opcode,
		     VEC (operand_entry_t, heap) **ops,
		     unsigned int currindex,
		     operand_entry_t curr)
{
  tree notop;
  unsigned int i;
  operand_entry_t oe;

  if ((opcode != BIT_IOR_EXPR && opcode != BIT_AND_EXPR)
      || TREE_CODE (curr->op) != SSA_NAME)
    return false;

  notop = get_unary_op (curr->op, BIT_NOT_EXPR);
  if (notop == NULL_TREE)
    return false;

  /* Any non-not version will have a rank that is one less than
     the current rank.  So once we hit those ranks, if we don't find
     one, we can stop.  */

  for (i = currindex + 1;
       VEC_iterate (operand_entry_t, *ops, i, oe)
       && oe->rank >= curr->rank - 1;
       i++)
    {
      if (oe->op == notop)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Equivalence: ");
	      print_generic_expr (dump_file, notop, 0);
	      if (opcode == BIT_AND_EXPR)
		fprintf (dump_file, " & ~");
	      else if (opcode == BIT_IOR_EXPR)
		fprintf (dump_file, " | ~");
	      print_generic_expr (dump_file, oe->op, 0);
	      if (opcode == BIT_AND_EXPR)
		fprintf (dump_file, " -> 0\n");
	      else if (opcode == BIT_IOR_EXPR)
		fprintf (dump_file, " -> -1\n");
	    }

	  if (opcode == BIT_AND_EXPR)
	    oe->op = fold_convert (TREE_TYPE (oe->op), integer_zero_node);
	  else if (opcode == BIT_IOR_EXPR)
	    oe->op = build_low_bits_mask (TREE_TYPE (oe->op),
					  TYPE_PRECISION (TREE_TYPE (oe->op)));

	  reassociate_stats.ops_eliminated 
	    += VEC_length (operand_entry_t, *ops) - 1;
	  VEC_free (operand_entry_t, heap, *ops);
	  *ops = NULL;
	  VEC_safe_push (operand_entry_t, heap, *ops, oe);
	  return true;
	}
    }

  return false;
}

/* Use constant value that may be present in OPS to try to eliminate
   operands.  Note that this function is only really used when we've
   eliminated ops for other reasons, or merged constants.  Across
   single statements, fold already does all of this, plus more.  There
   is little point in duplicating logic, so I've only included the
   identities that I could ever construct testcases to trigger.  */

static void
eliminate_using_constants (enum tree_code opcode,
			   VEC(operand_entry_t, heap) **ops)
{
  operand_entry_t oelast = VEC_last (operand_entry_t, *ops);

  if (oelast->rank == 0 && INTEGRAL_TYPE_P (TREE_TYPE (oelast->op)))
    {
      switch (opcode)
	{
	case BIT_AND_EXPR:
	  if (integer_zerop (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found & 0, removing all other ops\n");

		  reassociate_stats.ops_eliminated 
		    += VEC_length (operand_entry_t, *ops) - 1;
		  
		  VEC_free (operand_entry_t, heap, *ops);
		  *ops = NULL;
		  VEC_safe_push (operand_entry_t, heap, *ops, oelast);
		  return;
		}
	    }
	  else if (integer_all_onesp (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found & -1, removing\n");
		  VEC_pop (operand_entry_t, *ops);
		  reassociate_stats.ops_eliminated++;
		}
	    }
	  break;
	case BIT_IOR_EXPR:
	  if (integer_all_onesp (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found | -1, removing all other ops\n");

		  reassociate_stats.ops_eliminated 
		    += VEC_length (operand_entry_t, *ops) - 1;
		  
		  VEC_free (operand_entry_t, heap, *ops);
		  *ops = NULL;
		  VEC_safe_push (operand_entry_t, heap, *ops, oelast);
		  return;
		}
	    }	  
	  else if (integer_zerop (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found | 0, removing\n");
		  VEC_pop (operand_entry_t, *ops);
		  reassociate_stats.ops_eliminated++;
		}
	    }
	  break;
	case MULT_EXPR:
	  if (integer_zerop (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found * 0, removing all other ops\n");
		  
		  reassociate_stats.ops_eliminated 
		    += VEC_length (operand_entry_t, *ops) - 1;
		  VEC_free (operand_entry_t, heap, *ops);
		  *ops = NULL;
		  VEC_safe_push (operand_entry_t, heap, *ops, oelast);
		  return;
		}
	    }
	  else if (integer_onep (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found * 1, removing\n");
		  VEC_pop (operand_entry_t, *ops);
		  reassociate_stats.ops_eliminated++;
		  return;
		}
	    }
	  break;
	case BIT_XOR_EXPR:
	case PLUS_EXPR:
	case MINUS_EXPR:
	  if (integer_zerop (oelast->op))
	    {
	      if (VEC_length (operand_entry_t, *ops) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "Found [|^+] 0, removing\n");
		  VEC_pop (operand_entry_t, *ops);
		  reassociate_stats.ops_eliminated++;
		  return;
		}
	    }
	  break;
	default:
	  break;
	}
    }
}

/* Perform various identities and other optimizations on the list of
   operand entries, stored in OPS.  The tree code for the binary
   operation between all the operands is OPCODE.  */

static void
optimize_ops_list (enum tree_code opcode,
		   VEC (operand_entry_t, heap) **ops)
{
  unsigned int length = VEC_length (operand_entry_t, *ops);
  unsigned int i;
  operand_entry_t oe;
  operand_entry_t oelast = NULL;
  bool iterate = false;

  if (length == 1)
    return;

  oelast = VEC_last (operand_entry_t, *ops);

  /* If the last two are constants, pop the constants off, merge them
     and try the next two.  */
  if (oelast->rank == 0 && is_gimple_min_invariant (oelast->op))
    {
      operand_entry_t oelm1 = VEC_index (operand_entry_t, *ops, length - 2);

      if (oelm1->rank == 0
	  && is_gimple_min_invariant (oelm1->op)
	  && lang_hooks.types_compatible_p (TREE_TYPE (oelm1->op),
					    TREE_TYPE (oelast->op)))
	{
	  tree folded = fold_binary (opcode, TREE_TYPE (oelm1->op),
				     oelm1->op, oelast->op);

	  if (folded && is_gimple_min_invariant (folded))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "Merging constants\n");

	      VEC_pop (operand_entry_t, *ops);
	      VEC_pop (operand_entry_t, *ops);

	      add_to_ops_vec (ops, folded);
	      reassociate_stats.constants_eliminated++;

	      optimize_ops_list (opcode, ops);
	      return;
	    }
	}
    }

  eliminate_using_constants (opcode, ops);
  oelast = NULL;

  for (i = 0; VEC_iterate (operand_entry_t, *ops, i, oe);)
    {
      bool done = false;

      if (eliminate_not_pairs (opcode, ops, i, oe))
	return;
      if (eliminate_duplicate_pair (opcode, ops, &done, i, oe, oelast)
	  || (!done && eliminate_plus_minus_pair (opcode, ops, i, oe)))
	{
	  if (done)
	    return;
	  iterate = true;
	  oelast = NULL;
	  continue;
	}
      oelast = oe;
      i++;
    }

  length  = VEC_length (operand_entry_t, *ops);
  oelast = VEC_last (operand_entry_t, *ops);

  if (iterate)
    optimize_ops_list (opcode, ops);
}

/* Return true if OPERAND is defined by a PHI node which uses the LHS
   of STMT in it's operands.  This is also known as a "destructive
   update" operation.  */

static bool
is_phi_for_stmt (tree stmt, tree operand)
{
  tree def_stmt;
  tree lhs = TREE_OPERAND (stmt, 0);
  use_operand_p arg_p;
  ssa_op_iter i;

  if (TREE_CODE (operand) != SSA_NAME)
    return false;

  def_stmt = SSA_NAME_DEF_STMT (operand);
  if (TREE_CODE (def_stmt) != PHI_NODE)
    return false;

  FOR_EACH_PHI_ARG (arg_p, def_stmt, i, SSA_OP_USE)
    if (lhs == USE_FROM_PTR (arg_p))
      return true;
  return false;
}

/* Recursively rewrite our linearized statements so that the operators
   match those in OPS[OPINDEX], putting the computation in rank
   order.  */

static void
rewrite_expr_tree (tree stmt, unsigned int opindex,
		   VEC(operand_entry_t, heap) * ops)
{
  tree rhs = TREE_OPERAND (stmt, 1);
  operand_entry_t oe;

  /* If we have three operands left, then we want to make sure the one
     that gets the double binary op are the ones with the same rank.

     The alternative we try is to see if this is a destructive
     update style statement, which is like:
     b = phi (a, ...)
     a = c + b;
     In that case, we want to use the destructive update form to
     expose the possible vectorizer sum reduction opportunity.
     In that case, the third operand will be the phi node.

     We could, of course, try to be better as noted above, and do a
     lot of work to try to find these opportunities in >3 operand
     cases, but it is unlikely to be worth it.  */
  if (opindex + 3 == VEC_length (operand_entry_t, ops))
    {
      operand_entry_t oe1, oe2, oe3;

      oe1 = VEC_index (operand_entry_t, ops, opindex);
      oe2 = VEC_index (operand_entry_t, ops, opindex + 1);
      oe3 = VEC_index (operand_entry_t, ops, opindex + 2);

      if ((oe1->rank == oe2->rank
	   && oe2->rank != oe3->rank)
	  || (is_phi_for_stmt (stmt, oe3->op)
	      && !is_phi_for_stmt (stmt, oe1->op)
	      && !is_phi_for_stmt (stmt, oe2->op)))
	{
	  struct operand_entry temp = *oe3;
	  oe3->op = oe1->op;
	  oe3->rank = oe1->rank;
	  oe1->op = temp.op;
	  oe1->rank= temp.rank;
	}
    }

  /* The final recursion case for this function is that you have
     exactly two operations left.
     If we had one exactly one op in the entire list to start with, we
     would have never called this function, and the tail recursion
     rewrites them one at a time.  */
  if (opindex + 2 == VEC_length (operand_entry_t, ops))
    {
      operand_entry_t oe1, oe2;

      oe1 = VEC_index (operand_entry_t, ops, opindex);
      oe2 = VEC_index (operand_entry_t, ops, opindex + 1);

      if (TREE_OPERAND (rhs, 0) != oe1->op
	  || TREE_OPERAND (rhs, 1) != oe2->op)
	{

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Transforming ");
	      print_generic_expr (dump_file, rhs, 0);
	    }

	  TREE_OPERAND (rhs, 0) = oe1->op;
	  TREE_OPERAND (rhs, 1) = oe2->op;
	  update_stmt (stmt);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, " into ");
	      print_generic_stmt (dump_file, rhs, 0);
	    }

	}
      return;
    }

  /* If we hit here, we should have 3 or more ops left.  */
  gcc_assert (opindex + 2 < VEC_length (operand_entry_t, ops));

  /* Rewrite the next operator.  */
  oe = VEC_index (operand_entry_t, ops, opindex);

  if (oe->op != TREE_OPERAND (rhs, 1))
    {

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "Transforming ");
	  print_generic_expr (dump_file, rhs, 0);
	}

      TREE_OPERAND (rhs, 1) = oe->op;
      update_stmt (stmt);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, " into ");
	  print_generic_stmt (dump_file, rhs, 0);
	}
    }
  /* Recurse on the LHS of the binary operator, which is guaranteed to
     be the non-leaf side.  */
  rewrite_expr_tree (SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 0)),
		     opindex + 1, ops);
}

/* Transform STMT, which is really (A +B) + (C + D) into the left
   linear form, ((A+B)+C)+D.
   Recurse on D if necessary.  */

static void
linearize_expr (tree stmt)
{
  block_stmt_iterator bsinow, bsirhs;
  tree rhs = TREE_OPERAND (stmt, 1);
  enum tree_code rhscode = TREE_CODE (rhs);
  tree binrhs = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 1));
  tree binlhs = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 0));
  tree newbinrhs = NULL_TREE;

  gcc_assert (is_reassociable_op (binlhs, TREE_CODE (rhs))
	      && is_reassociable_op (binrhs, TREE_CODE (rhs)));

  bsinow = bsi_for_stmt (stmt);
  bsirhs = bsi_for_stmt (binrhs);
  bsi_move_before (&bsirhs, &bsinow);

  TREE_OPERAND (rhs, 1) = TREE_OPERAND (TREE_OPERAND (binrhs, 1), 0);
  if (TREE_CODE (TREE_OPERAND (rhs, 1)) == SSA_NAME)
    newbinrhs = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 1));
  TREE_OPERAND (TREE_OPERAND (binrhs, 1), 0) = TREE_OPERAND (binlhs, 0);
  TREE_OPERAND (rhs, 0) = TREE_OPERAND (binrhs, 0);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Linearized: ");
      print_generic_stmt (dump_file, rhs, 0);
    }

  reassociate_stats.linearized++;
  update_stmt (binrhs);
  update_stmt (binlhs);
  update_stmt (stmt);
  TREE_VISITED (binrhs) = 1;
  TREE_VISITED (binlhs) = 1;
  TREE_VISITED (stmt) = 1;

  /* Tail recurse on the new rhs if it still needs reassociation.  */
  if (newbinrhs && is_reassociable_op (newbinrhs, rhscode))
    linearize_expr (stmt);

}

/* If LHS has a single immediate use that is a MODIFY_EXPR, return
   it.  Otherwise, return NULL.  */

static tree
get_single_immediate_use (tree lhs)
{
  use_operand_p immuse;
  tree immusestmt;

  if (TREE_CODE (lhs) == SSA_NAME
      && single_imm_use (lhs, &immuse, &immusestmt))
    {
      if (TREE_CODE (immusestmt) == RETURN_EXPR)
	immusestmt = TREE_OPERAND (immusestmt, 0);
      if (TREE_CODE (immusestmt) == MODIFY_EXPR)
	return immusestmt;
    }
  return NULL_TREE;
}
static VEC(tree, heap) *broken_up_subtracts;


/* Recursively negate the value of TONEGATE, and return the SSA_NAME
   representing the negated value.  Insertions of any necessary
   instructions go before BSI.
   This function is recursive in that, if you hand it "a_5" as the
   value to negate, and a_5 is defined by "a_5 = b_3 + b_4", it will
   transform b_3 + b_4 into a_5 = -b_3 + -b_4.  */

static tree
negate_value (tree tonegate, block_stmt_iterator *bsi)
{
  tree negatedef = tonegate;
  tree resultofnegate;

  if (TREE_CODE (tonegate) == SSA_NAME)
    negatedef = SSA_NAME_DEF_STMT (tonegate);

  /* If we are trying to negate a name, defined by an add, negate the
     add operands instead.  */
  if (TREE_CODE (tonegate) == SSA_NAME
      && TREE_CODE (negatedef) == MODIFY_EXPR
      && TREE_CODE (TREE_OPERAND (negatedef, 0)) == SSA_NAME
      && has_single_use (TREE_OPERAND (negatedef, 0))
      && TREE_CODE (TREE_OPERAND (negatedef, 1)) == PLUS_EXPR)
    {
      block_stmt_iterator bsi;
      tree binop = TREE_OPERAND (negatedef, 1);

      bsi = bsi_for_stmt (negatedef);
      TREE_OPERAND (binop, 0) = negate_value (TREE_OPERAND (binop, 0),
					      &bsi);
      bsi = bsi_for_stmt (negatedef);
      TREE_OPERAND (binop, 1) = negate_value (TREE_OPERAND (binop, 1),
					      &bsi);
      update_stmt (negatedef);
      return TREE_OPERAND (negatedef, 0);
    }

  tonegate = fold_build1 (NEGATE_EXPR, TREE_TYPE (tonegate), tonegate);
  resultofnegate = force_gimple_operand_bsi (bsi, tonegate, true,
					     NULL_TREE);
  VEC_safe_push (tree, heap, broken_up_subtracts, resultofnegate);
  return resultofnegate;

}

/* Return true if we should break up the subtract in STMT into an add
   with negate.  This is true when we the subtract operands are really
   adds, or the subtract itself is used in an add expression.  In
   either case, breaking up the subtract into an add with negate
   exposes the adds to reassociation.  */

static bool
should_break_up_subtract (tree stmt)
{

  tree lhs = TREE_OPERAND (stmt, 0);
  tree rhs = TREE_OPERAND (stmt, 1);
  tree binlhs = TREE_OPERAND (rhs, 0);
  tree binrhs = TREE_OPERAND (rhs, 1);
  tree immusestmt;

  if (TREE_CODE (binlhs) == SSA_NAME
      && is_reassociable_op (SSA_NAME_DEF_STMT (binlhs), PLUS_EXPR))
    return true;

  if (TREE_CODE (binrhs) == SSA_NAME
      && is_reassociable_op (SSA_NAME_DEF_STMT (binrhs), PLUS_EXPR))
    return true;

  if (TREE_CODE (lhs) == SSA_NAME
      && (immusestmt = get_single_immediate_use (lhs))
      && TREE_CODE (TREE_OPERAND (immusestmt, 1)) == PLUS_EXPR)
    return true;
  return false;

}

/* Transform STMT from A - B into A + -B.  */

static void
break_up_subtract (tree stmt, block_stmt_iterator *bsi)
{
  tree rhs = TREE_OPERAND (stmt, 1);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Breaking up subtract ");
      print_generic_stmt (dump_file, stmt, 0);
    }

  TREE_SET_CODE (TREE_OPERAND (stmt, 1), PLUS_EXPR);
  TREE_OPERAND (rhs, 1) = negate_value (TREE_OPERAND (rhs, 1), bsi);

  update_stmt (stmt);
}

/* Recursively linearize a binary expression that is the RHS of STMT.
   Place the operands of the expression tree in the vector named OPS.  */

static void
linearize_expr_tree (VEC(operand_entry_t, heap) **ops, tree stmt)
{
  block_stmt_iterator bsinow, bsilhs;
  tree rhs = TREE_OPERAND (stmt, 1);
  tree binrhs = TREE_OPERAND (rhs, 1);
  tree binlhs = TREE_OPERAND (rhs, 0);
  tree binlhsdef, binrhsdef;
  bool binlhsisreassoc = false;
  bool binrhsisreassoc = false;
  enum tree_code rhscode = TREE_CODE (rhs);

  TREE_VISITED (stmt) = 1;

  if (TREE_CODE (binlhs) == SSA_NAME)
    {
      binlhsdef = SSA_NAME_DEF_STMT (binlhs);
      binlhsisreassoc = is_reassociable_op (binlhsdef, rhscode);
    }

  if (TREE_CODE (binrhs) == SSA_NAME)
    {
      binrhsdef = SSA_NAME_DEF_STMT (binrhs);
      binrhsisreassoc = is_reassociable_op (binrhsdef, rhscode);
    }

  /* If the LHS is not reassociable, but the RHS is, we need to swap
     them.  If neither is reassociable, there is nothing we can do, so
     just put them in the ops vector.  If the LHS is reassociable,
     linearize it.  If both are reassociable, then linearize the RHS
     and the LHS.  */

  if (!binlhsisreassoc)
    {
      tree temp;

      if (!binrhsisreassoc)
	{
	  add_to_ops_vec (ops, binrhs);
	  add_to_ops_vec (ops, binlhs);
	  return;
	}

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "swapping operands of ");
	  print_generic_expr (dump_file, stmt, 0);
	}

      swap_tree_operands (stmt, &TREE_OPERAND (rhs, 0),
			  &TREE_OPERAND (rhs, 1));
      update_stmt (stmt);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, " is now ");
	  print_generic_stmt (dump_file, stmt, 0);
	}

      /* We want to make it so the lhs is always the reassociative op,
	 so swap.  */
      temp = binlhs;
      binlhs = binrhs;
      binrhs = temp;
    }
  else if (binrhsisreassoc)
    {
      linearize_expr (stmt);
      gcc_assert (rhs == TREE_OPERAND (stmt, 1));
      binlhs = TREE_OPERAND (rhs, 0);
      binrhs = TREE_OPERAND (rhs, 1);
    }

  gcc_assert (TREE_CODE (binrhs) != SSA_NAME
	      || !is_reassociable_op (SSA_NAME_DEF_STMT (binrhs), rhscode));
  bsinow = bsi_for_stmt (stmt);
  bsilhs = bsi_for_stmt (SSA_NAME_DEF_STMT (binlhs));
  bsi_move_before (&bsilhs, &bsinow);
  linearize_expr_tree (ops, SSA_NAME_DEF_STMT (binlhs));
  add_to_ops_vec (ops, binrhs);
}

/* Repropagate the negates back into subtracts, since no other pass
   currently does it.  */

static void
repropagate_negates (void)
{
  unsigned int i = 0;
  tree negate;

  for (i = 0; VEC_iterate (tree, broken_up_subtracts, i, negate); i++)
    {
      tree user = get_single_immediate_use (negate);

      /* The negate operand can be either operand of a PLUS_EXPR
	 (it can be the LHS if the RHS is a constant for example).

	 Force the negate operand to the RHS of the PLUS_EXPR, then
	 transform the PLUS_EXPR into a MINUS_EXPR.  */
      if (user
	  && TREE_CODE (user) == MODIFY_EXPR
	  && TREE_CODE (TREE_OPERAND (user, 1)) == PLUS_EXPR)
	{
	  tree rhs = TREE_OPERAND (user, 1);

	  /* If the negated operand appears on the LHS of the
	     PLUS_EXPR, exchange the operands of the PLUS_EXPR
	     to force the negated operand to the RHS of the PLUS_EXPR.  */
	  if (TREE_OPERAND (TREE_OPERAND (user, 1), 0) == negate)
	    {
	      tree temp = TREE_OPERAND (rhs, 0);
	      TREE_OPERAND (rhs, 0) = TREE_OPERAND (rhs, 1);
	      TREE_OPERAND (rhs, 1) = temp;
	    }

	  /* Now transform the PLUS_EXPR into a MINUS_EXPR and replace
	     the RHS of the PLUS_EXPR with the operand of the NEGATE_EXPR.  */
	  if (TREE_OPERAND (TREE_OPERAND (user, 1), 1) == negate)
	    {
	      TREE_SET_CODE (rhs, MINUS_EXPR);
	      TREE_OPERAND (rhs, 1) = get_unary_op (negate, NEGATE_EXPR);
	      update_stmt (user);
	    }
	}
    }
}

/* Break up subtract operations in block BB.

   We do this top down because we don't know whether the subtract is
   part of a possible chain of reassociation except at the top.
 
   IE given
   d = f + g
   c = a + e
   b = c - d
   q = b - r
   k = t - q
   
   we want to break up k = t - q, but we won't until we've transformed q
   = b - r, which won't be broken up until we transform b = c - d.  */

static void
break_up_subtract_bb (basic_block bb)
{
  block_stmt_iterator bsi;
  basic_block son;

  for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      tree stmt = bsi_stmt (bsi);

      if (TREE_CODE (stmt) == MODIFY_EXPR)
	{
	  tree lhs = TREE_OPERAND (stmt, 0);
	  tree rhs = TREE_OPERAND (stmt, 1);

	  TREE_VISITED (stmt) = 0;
	  /* If unsafe math optimizations we can do reassociation for
	     non-integral types.  */
	  if ((!INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	       || !INTEGRAL_TYPE_P (TREE_TYPE (rhs)))
	      && (!SCALAR_FLOAT_TYPE_P (TREE_TYPE (rhs))
		  || !SCALAR_FLOAT_TYPE_P (TREE_TYPE(lhs))
		  || !flag_unsafe_math_optimizations))
	    continue;

	  /* Check for a subtract used only in an addition.  If this
	     is the case, transform it into add of a negate for better
	     reassociation.  IE transform C = A-B into C = A + -B if C
	     is only used in an addition.  */
	  if (TREE_CODE (rhs) == MINUS_EXPR)
	    if (should_break_up_subtract (stmt))
	      break_up_subtract (stmt, &bsi);
	}
    }
  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    break_up_subtract_bb (son);
}

/* Reassociate expressions in basic block BB and its post-dominator as
   children.  */

static void
reassociate_bb (basic_block bb)
{
  block_stmt_iterator bsi;
  basic_block son;

  for (bsi = bsi_last (bb); !bsi_end_p (bsi); bsi_prev (&bsi))
    {
      tree stmt = bsi_stmt (bsi);

      if (TREE_CODE (stmt) == MODIFY_EXPR)
	{
	  tree lhs = TREE_OPERAND (stmt, 0);
	  tree rhs = TREE_OPERAND (stmt, 1);

	  /* If this was part of an already processed tree, we don't
	     need to touch it again. */
	  if (TREE_VISITED (stmt))
	    continue;

	  /* If unsafe math optimizations we can do reassociation for
	     non-integral types.  */
	  if ((!INTEGRAL_TYPE_P (TREE_TYPE (lhs))
	       || !INTEGRAL_TYPE_P (TREE_TYPE (rhs)))
	      && (!SCALAR_FLOAT_TYPE_P (TREE_TYPE (rhs))
		  || !SCALAR_FLOAT_TYPE_P (TREE_TYPE(lhs))
		  || !flag_unsafe_math_optimizations))
	    continue;

	  if (associative_tree_code (TREE_CODE (rhs)))
	    {
	      VEC(operand_entry_t, heap) *ops = NULL;

	      /* There may be no immediate uses left by the time we
		 get here because we may have eliminated them all.  */
	      if (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs))
		continue;

	      TREE_VISITED (stmt) = 1;
	      linearize_expr_tree (&ops, stmt);
	      qsort (VEC_address (operand_entry_t, ops),
		     VEC_length (operand_entry_t, ops),
		     sizeof (operand_entry_t),
		     sort_by_operand_rank);
	      optimize_ops_list (TREE_CODE (rhs), &ops);

	      if (VEC_length (operand_entry_t, ops) == 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "Transforming ");
		      print_generic_expr (dump_file, rhs, 0);
		    }
		  TREE_OPERAND (stmt, 1) = VEC_last (operand_entry_t, ops)->op;
		  update_stmt (stmt);

		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, " into ");
		      print_generic_stmt (dump_file,
					  TREE_OPERAND (stmt, 1), 0);
		    }
		}
	      else
		{
		  rewrite_expr_tree (stmt, 0, ops);
		}

	      VEC_free (operand_entry_t, heap, ops);
	    }
	}
    }
  for (son = first_dom_son (CDI_POST_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_POST_DOMINATORS, son))
    reassociate_bb (son);
}

void dump_ops_vector (FILE *file, VEC (operand_entry_t, heap) *ops);
void debug_ops_vector (VEC (operand_entry_t, heap) *ops);

/* Dump the operand entry vector OPS to FILE.  */

void
dump_ops_vector (FILE *file, VEC (operand_entry_t, heap) *ops)
{
  operand_entry_t oe;
  unsigned int i;

  for (i = 0; VEC_iterate (operand_entry_t, ops, i, oe); i++)
    {
      fprintf (file, "Op %d -> rank: %d, tree: ", i, oe->rank);
      print_generic_stmt (file, oe->op, 0);
    }
}

/* Dump the operand entry vector OPS to STDERR.  */

void
debug_ops_vector (VEC (operand_entry_t, heap) *ops)
{
  dump_ops_vector (stderr, ops);
}

static void
do_reassoc (void)
{
  break_up_subtract_bb (ENTRY_BLOCK_PTR);
  reassociate_bb (EXIT_BLOCK_PTR);
}

/* Initialize the reassociation pass.  */

static void
init_reassoc (void)
{
  int i;
  unsigned int rank = 2;
  tree param;
  int *bbs = XNEWVEC (int, last_basic_block + 1);

  memset (&reassociate_stats, 0, sizeof (reassociate_stats));

  operand_entry_pool = create_alloc_pool ("operand entry pool",
					  sizeof (struct operand_entry), 30);

  /* Reverse RPO (Reverse Post Order) will give us something where
     deeper loops come later.  */
  pre_and_rev_post_order_compute (NULL, bbs, false);
  bb_rank = XCNEWVEC (unsigned int, last_basic_block + 1);
  
  operand_rank = htab_create (511, operand_entry_hash,
			      operand_entry_eq, 0);

  /* Give each argument a distinct rank.   */
  for (param = DECL_ARGUMENTS (current_function_decl);
       param;
       param = TREE_CHAIN (param))
    {
      if (default_def (param) != NULL)
	{
	  tree def = default_def (param);
	  insert_operand_rank (def, ++rank);
	}
    }

  /* Give the chain decl a distinct rank. */
  if (cfun->static_chain_decl != NULL)
    {
      tree def = default_def (cfun->static_chain_decl);
      if (def != NULL)
	insert_operand_rank (def, ++rank);
    }

  /* Set up rank for each BB  */
  for (i = 0; i < n_basic_blocks - NUM_FIXED_BLOCKS; i++)
    bb_rank[bbs[i]] = ++rank  << 16;

  free (bbs);
  calculate_dominance_info (CDI_DOMINATORS);
  calculate_dominance_info (CDI_POST_DOMINATORS);
  broken_up_subtracts = NULL;
}

/* Cleanup after the reassociation pass, and print stats if
   requested.  */

static void
fini_reassoc (void)
{

  if (dump_file && (dump_flags & TDF_STATS))
    {
      fprintf (dump_file, "Reassociation stats:\n");
      fprintf (dump_file, "Linearized: %d\n", 
	       reassociate_stats.linearized);
      fprintf (dump_file, "Constants eliminated: %d\n",
	       reassociate_stats.constants_eliminated);
      fprintf (dump_file, "Ops eliminated: %d\n",
	       reassociate_stats.ops_eliminated);
      fprintf (dump_file, "Statements rewritten: %d\n",
	       reassociate_stats.rewritten);
    }
  htab_delete (operand_rank);

  free_alloc_pool (operand_entry_pool);
  free (bb_rank);
  VEC_free (tree, heap, broken_up_subtracts);
  free_dominance_info (CDI_POST_DOMINATORS);
}

/* Gate and execute functions for Reassociation.  */

static unsigned int
execute_reassoc (void)
{
  init_reassoc ();

  do_reassoc ();
  repropagate_negates ();

  fini_reassoc ();
  return 0;
}

struct tree_opt_pass pass_reassoc =
{
  "reassoc",				/* name */
  NULL,				/* gate */
  execute_reassoc,				/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_REASSOC,				/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect | TODO_verify_ssa, /* todo_flags_finish */
  0					/* letter */
};
