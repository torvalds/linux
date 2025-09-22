/* Forward propagation of expressions for single use variables.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "langhooks.h"

/* This pass propagates the RHS of assignment statements into use
   sites of the LHS of the assignment.  It's basically a specialized
   form of tree combination.   It is hoped all of this can disappear
   when we have a generalized tree combiner.

   Note carefully that after propagation the resulting statement
   must still be a proper gimple statement.  Right now we simply
   only perform propagations we know will result in valid gimple
   code.  One day we'll want to generalize this code.

   One class of common cases we handle is forward propagating a single use
   variable into a COND_EXPR.  

     bb0:
       x = a COND b;
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
       if (a COND b) goto ... else goto ...
 
   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).

   Or (assuming c1 and c2 are constants):

     bb0:
       x = a + c1;  
       if (x EQ/NEQ c2) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a EQ/NEQ (c2 - c1)) goto ... else goto ...

   Similarly for x = a - c1.
    
   Or

     bb0:
       x = !a
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a == 0) goto ... else goto ...

   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).
   For these cases, we propagate A into all, possibly more than one,
   COND_EXPRs that use X.

   Or

     bb0:
       x = (typecast) a
       if (x) goto ... else goto ...

   Will be transformed into:

     bb0:
        if (a != 0) goto ... else goto ...

   (Assuming a is an integral type and x is a boolean or x is an
    integral and a is a boolean.)

   Similarly for the tests (x == 0), (x != 0), (x == 1) and (x != 1).
   For these cases, we propagate A into all, possibly more than one,
   COND_EXPRs that use X.

   In addition to eliminating the variable and the statement which assigns
   a value to the variable, we may be able to later thread the jump without
   adding insane complexity in the dominator optimizer.

   Also note these transformations can cascade.  We handle this by having
   a worklist of COND_EXPR statements to examine.  As we make a change to
   a statement, we put it back on the worklist to examine on the next
   iteration of the main loop.

   A second class of propagation opportunities arises for ADDR_EXPR
   nodes.

     ptr = &x->y->z;
     res = *ptr;

   Will get turned into

     res = x->y->z;

   Or

     ptr = &x[0];
     ptr2 = ptr + <constant>;

   Will get turned into

     ptr2 = &x[constant/elementsize];

  Or

     ptr = &x[0];
     offset = index * element_size;
     offset_p = (pointer) offset;
     ptr2 = ptr + offset_p

  Will get turned into:

     ptr2 = &x[index];

  We also propagate casts into SWITCH_EXPR and COND_EXPR conditions to
  allow us to remove the cast and {NOT_EXPR,NEG_EXPR} into a subsequent
  {NOT_EXPR,NEG_EXPR}.

   This will (of course) be extended as other needs arise.  */


/* Set to true if we delete EH edges during the optimization.  */
static bool cfg_changed;


/* Given an SSA_NAME VAR, return true if and only if VAR is defined by
   a comparison.  */

static bool
ssa_name_defined_by_comparison_p (tree var)
{
  tree def = SSA_NAME_DEF_STMT (var);

  if (TREE_CODE (def) == MODIFY_EXPR)
    {
      tree rhs = TREE_OPERAND (def, 1);
      return COMPARISON_CLASS_P (rhs);
    }

  return 0;
}

/* Forward propagate a single-use variable into COND once.  Return a
   new condition if successful.  Return NULL_TREE otherwise.  */

static tree
forward_propagate_into_cond_1 (tree cond, tree *test_var_p)
{
  tree new_cond = NULL_TREE;
  enum tree_code cond_code = TREE_CODE (cond);
  tree test_var = NULL_TREE;
  tree def;
  tree def_rhs;

  /* If the condition is not a lone variable or an equality test of an
     SSA_NAME against an integral constant, then we do not have an
     optimizable case.

     Note these conditions also ensure the COND_EXPR has no
     virtual operands or other side effects.  */
  if (cond_code != SSA_NAME
      && !((cond_code == EQ_EXPR || cond_code == NE_EXPR)
	   && TREE_CODE (TREE_OPERAND (cond, 0)) == SSA_NAME
	   && CONSTANT_CLASS_P (TREE_OPERAND (cond, 1))
	   && INTEGRAL_TYPE_P (TREE_TYPE (TREE_OPERAND (cond, 1)))))
    return NULL_TREE;

  /* Extract the single variable used in the test into TEST_VAR.  */
  if (cond_code == SSA_NAME)
    test_var = cond;
  else
    test_var = TREE_OPERAND (cond, 0);

  /* Now get the defining statement for TEST_VAR.  Skip this case if
     it's not defined by some MODIFY_EXPR.  */
  def = SSA_NAME_DEF_STMT (test_var);
  if (TREE_CODE (def) != MODIFY_EXPR)
    return NULL_TREE;

  def_rhs = TREE_OPERAND (def, 1);

  /* If TEST_VAR is set by adding or subtracting a constant
     from an SSA_NAME, then it is interesting to us as we
     can adjust the constant in the conditional and thus
     eliminate the arithmetic operation.  */
  if (TREE_CODE (def_rhs) == PLUS_EXPR
      || TREE_CODE (def_rhs) == MINUS_EXPR)
    {
      tree op0 = TREE_OPERAND (def_rhs, 0);
      tree op1 = TREE_OPERAND (def_rhs, 1);

      /* The first operand must be an SSA_NAME and the second
	 operand must be a constant.  */
      if (TREE_CODE (op0) != SSA_NAME
	  || !CONSTANT_CLASS_P (op1)
	  || !INTEGRAL_TYPE_P (TREE_TYPE (op1)))
	return NULL_TREE;

      /* Don't propagate if the first operand occurs in
	 an abnormal PHI.  */
      if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op0))
	return NULL_TREE;

      if (has_single_use (test_var))
	{
	  enum tree_code new_code;
	  tree t;

	  /* If the variable was defined via X + C, then we must
	     subtract C from the constant in the conditional.
	     Otherwise we add C to the constant in the
	     conditional.  The result must fold into a valid
	     gimple operand to be optimizable.  */
	  new_code = (TREE_CODE (def_rhs) == PLUS_EXPR
		      ? MINUS_EXPR : PLUS_EXPR);
	  t = int_const_binop (new_code, TREE_OPERAND (cond, 1), op1, 0);
	  if (!is_gimple_val (t))
	    return NULL_TREE;

	  new_cond = build2 (cond_code, boolean_type_node, op0, t);
	}
    }

  /* These cases require comparisons of a naked SSA_NAME or
     comparison of an SSA_NAME against zero or one.  */
  else if (TREE_CODE (cond) == SSA_NAME
	   || integer_zerop (TREE_OPERAND (cond, 1))
	   || integer_onep (TREE_OPERAND (cond, 1)))
    {
      /* If TEST_VAR is set from a relational operation
	 between two SSA_NAMEs or a combination of an SSA_NAME
	 and a constant, then it is interesting.  */
      if (COMPARISON_CLASS_P (def_rhs))
	{
	  tree op0 = TREE_OPERAND (def_rhs, 0);
	  tree op1 = TREE_OPERAND (def_rhs, 1);

	  /* Both operands of DEF_RHS must be SSA_NAMEs or
	     constants.  */
	  if ((TREE_CODE (op0) != SSA_NAME
	       && !is_gimple_min_invariant (op0))
	      || (TREE_CODE (op1) != SSA_NAME
		  && !is_gimple_min_invariant (op1)))
	    return NULL_TREE;

	  /* Don't propagate if the first operand occurs in
	     an abnormal PHI.  */
	  if (TREE_CODE (op0) == SSA_NAME
	      && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op0))
	    return NULL_TREE;

	  /* Don't propagate if the second operand occurs in
	     an abnormal PHI.  */
	  if (TREE_CODE (op1) == SSA_NAME
	      && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (op1))
	    return NULL_TREE;

	  if (has_single_use (test_var))
	    {
	      /* TEST_VAR was set from a relational operator.  */
	      new_cond = build2 (TREE_CODE (def_rhs),
				 boolean_type_node, op0, op1);

	      /* Invert the conditional if necessary.  */
	      if ((cond_code == EQ_EXPR
		   && integer_zerop (TREE_OPERAND (cond, 1)))
		  || (cond_code == NE_EXPR
		      && integer_onep (TREE_OPERAND (cond, 1))))
		{
		  new_cond = invert_truthvalue (new_cond);

		  /* If we did not get a simple relational
		     expression or bare SSA_NAME, then we can
		     not optimize this case.  */
		  if (!COMPARISON_CLASS_P (new_cond)
		      && TREE_CODE (new_cond) != SSA_NAME)
		    new_cond = NULL_TREE;
		}
	    }
	}

      /* If TEST_VAR is set from a TRUTH_NOT_EXPR, then it
	 is interesting.  */
      else if (TREE_CODE (def_rhs) == TRUTH_NOT_EXPR)
	{
	  enum tree_code new_code;

	  def_rhs = TREE_OPERAND (def_rhs, 0);

	  /* DEF_RHS must be an SSA_NAME or constant.  */
	  if (TREE_CODE (def_rhs) != SSA_NAME
	      && !is_gimple_min_invariant (def_rhs))
	    return NULL_TREE;

	  /* Don't propagate if the operand occurs in
	     an abnormal PHI.  */
	  if (TREE_CODE (def_rhs) == SSA_NAME
	      && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (def_rhs))
	    return NULL_TREE;

	  if (cond_code == SSA_NAME
	      || (cond_code == NE_EXPR
		  && integer_zerop (TREE_OPERAND (cond, 1)))
	      || (cond_code == EQ_EXPR
		  && integer_onep (TREE_OPERAND (cond, 1))))
	    new_code = EQ_EXPR;
	  else
	    new_code = NE_EXPR;

	  new_cond = build2 (new_code, boolean_type_node, def_rhs,
			     fold_convert (TREE_TYPE (def_rhs),
					   integer_zero_node));
	}

      /* If TEST_VAR was set from a cast of an integer type
	 to a boolean type or a cast of a boolean to an
	 integral, then it is interesting.  */
      else if (TREE_CODE (def_rhs) == NOP_EXPR
	       || TREE_CODE (def_rhs) == CONVERT_EXPR)
	{
	  tree outer_type;
	  tree inner_type;

	  outer_type = TREE_TYPE (def_rhs);
	  inner_type = TREE_TYPE (TREE_OPERAND (def_rhs, 0));

	  if ((TREE_CODE (outer_type) == BOOLEAN_TYPE
	       && INTEGRAL_TYPE_P (inner_type))
	      || (TREE_CODE (inner_type) == BOOLEAN_TYPE
		  && INTEGRAL_TYPE_P (outer_type)))
	    ;
	  else if (INTEGRAL_TYPE_P (outer_type)
		   && INTEGRAL_TYPE_P (inner_type)
		   && TREE_CODE (TREE_OPERAND (def_rhs, 0)) == SSA_NAME
		   && ssa_name_defined_by_comparison_p (TREE_OPERAND (def_rhs,
								      0)))
	    ;
	  else
	    return NULL_TREE;

	  /* Don't propagate if the operand occurs in
	     an abnormal PHI.  */
	  if (TREE_CODE (TREE_OPERAND (def_rhs, 0)) == SSA_NAME
	      && SSA_NAME_OCCURS_IN_ABNORMAL_PHI (TREE_OPERAND
						  (def_rhs, 0)))
	    return NULL_TREE;

	  if (has_single_use (test_var))
	    {
	      enum tree_code new_code;
	      tree new_arg;

	      if (cond_code == SSA_NAME
		  || (cond_code == NE_EXPR
		      && integer_zerop (TREE_OPERAND (cond, 1)))
		  || (cond_code == EQ_EXPR
		      && integer_onep (TREE_OPERAND (cond, 1))))
		new_code = NE_EXPR;
	      else
		new_code = EQ_EXPR;

	      new_arg = TREE_OPERAND (def_rhs, 0);
	      new_cond = build2 (new_code, boolean_type_node, new_arg,
				 fold_convert (TREE_TYPE (new_arg),
					       integer_zero_node));
	    }
	}
    }

  *test_var_p = test_var;
  return new_cond;
}

/* COND is a condition of the form:

     x == const or x != const

   Look back to x's defining statement and see if x is defined as

     x = (type) y;

   If const is unchanged if we convert it to type, then we can build
   the equivalent expression:


      y == const or y != const

   Which may allow further optimizations.

   Return the equivalent comparison or NULL if no such equivalent comparison
   was found.  */

static tree
find_equivalent_equality_comparison (tree cond)
{
  tree op0 = TREE_OPERAND (cond, 0);
  tree op1 = TREE_OPERAND (cond, 1);
  tree def_stmt = SSA_NAME_DEF_STMT (op0);

  while (def_stmt
	 && TREE_CODE (def_stmt) == MODIFY_EXPR
	 && TREE_CODE (TREE_OPERAND (def_stmt, 1)) == SSA_NAME)
    def_stmt = SSA_NAME_DEF_STMT (TREE_OPERAND (def_stmt, 1));

  /* OP0 might have been a parameter, so first make sure it
     was defined by a MODIFY_EXPR.  */
  if (def_stmt && TREE_CODE (def_stmt) == MODIFY_EXPR)
    {
      tree def_rhs = TREE_OPERAND (def_stmt, 1);

      /* If either operand to the comparison is a pointer to
	 a function, then we can not apply this optimization
	 as some targets require function pointers to be
	 canonicalized and in this case this optimization would
	 eliminate a necessary canonicalization.  */
      if ((POINTER_TYPE_P (TREE_TYPE (op0))
	   && TREE_CODE (TREE_TYPE (TREE_TYPE (op0))) == FUNCTION_TYPE)
	  || (POINTER_TYPE_P (TREE_TYPE (op1))
	      && TREE_CODE (TREE_TYPE (TREE_TYPE (op1))) == FUNCTION_TYPE))
	return NULL;
	      
      /* Now make sure the RHS of the MODIFY_EXPR is a typecast.  */
      if ((TREE_CODE (def_rhs) == NOP_EXPR
	   || TREE_CODE (def_rhs) == CONVERT_EXPR)
	  && TREE_CODE (TREE_OPERAND (def_rhs, 0)) == SSA_NAME)
	{
	  tree def_rhs_inner = TREE_OPERAND (def_rhs, 0);
	  tree def_rhs_inner_type = TREE_TYPE (def_rhs_inner);
	  tree new;

	  if (TYPE_PRECISION (def_rhs_inner_type)
	      > TYPE_PRECISION (TREE_TYPE (def_rhs)))
	    return NULL;

	  /* If the inner type of the conversion is a pointer to
	     a function, then we can not apply this optimization
	     as some targets require function pointers to be
	     canonicalized.  This optimization would result in
	     canonicalization of the pointer when it was not originally
	     needed/intended.  */
	  if (POINTER_TYPE_P (def_rhs_inner_type)
	      && TREE_CODE (TREE_TYPE (def_rhs_inner_type)) == FUNCTION_TYPE)
	    return NULL;

	  /* What we want to prove is that if we convert OP1 to
	     the type of the object inside the NOP_EXPR that the
	     result is still equivalent to SRC. 

	     If that is true, the build and return new equivalent
	     condition which uses the source of the typecast and the
	     new constant (which has only changed its type).  */
	  new = fold_build1 (TREE_CODE (def_rhs), def_rhs_inner_type, op1);
	  STRIP_USELESS_TYPE_CONVERSION (new);
	  if (is_gimple_val (new) && tree_int_cst_equal (new, op1))
	    return build2 (TREE_CODE (cond), TREE_TYPE (cond),
			   def_rhs_inner, new);
	}
    }
  return NULL;
}

/* STMT is a COND_EXPR

   This routine attempts to find equivalent forms of the condition
   which we may be able to optimize better.  */

static void
simplify_cond (tree stmt)
{
  tree cond = COND_EXPR_COND (stmt);

  if (COMPARISON_CLASS_P (cond))
    {
      tree op0 = TREE_OPERAND (cond, 0);
      tree op1 = TREE_OPERAND (cond, 1);

      if (TREE_CODE (op0) == SSA_NAME && is_gimple_min_invariant (op1))
	{
	  /* First see if we have test of an SSA_NAME against a constant
	     where the SSA_NAME is defined by an earlier typecast which
	     is irrelevant when performing tests against the given
	     constant.  */
	  if (TREE_CODE (cond) == EQ_EXPR || TREE_CODE (cond) == NE_EXPR)
	    {
	      tree new_cond = find_equivalent_equality_comparison (cond);

	      if (new_cond)
		{
		  COND_EXPR_COND (stmt) = new_cond;
		  update_stmt (stmt);
		}
	    }
	}
    }
}

/* Forward propagate a single-use variable into COND_EXPR as many
   times as possible.  */

static void
forward_propagate_into_cond (tree cond_expr)
{
  gcc_assert (TREE_CODE (cond_expr) == COND_EXPR);

  while (1)
    {
      tree test_var = NULL_TREE;
      tree cond = COND_EXPR_COND (cond_expr);
      tree new_cond = forward_propagate_into_cond_1 (cond, &test_var);

      /* Return if unsuccessful.  */
      if (new_cond == NULL_TREE)
	break;

      /* Dump details.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Replaced '");
	  print_generic_expr (dump_file, cond, dump_flags);
	  fprintf (dump_file, "' with '");
	  print_generic_expr (dump_file, new_cond, dump_flags);
	  fprintf (dump_file, "'\n");
	}

      COND_EXPR_COND (cond_expr) = new_cond;
      update_stmt (cond_expr);

      if (has_zero_uses (test_var))
	{
	  tree def = SSA_NAME_DEF_STMT (test_var);
	  block_stmt_iterator bsi = bsi_for_stmt (def);
	  bsi_remove (&bsi, true);
	}
    }

  /* There are further simplifications that can be performed
     on COND_EXPRs.  Specifically, when comparing an SSA_NAME
     against a constant where the SSA_NAME is the result of a
     conversion.  Perhaps this should be folded into the rest
     of the COND_EXPR simplification code.  */
  simplify_cond (cond_expr);
}

/* We've just substituted an ADDR_EXPR into stmt.  Update all the 
   relevant data structures to match.  */

static void
tidy_after_forward_propagate_addr (tree stmt)
{
  /* We may have turned a trapping insn into a non-trapping insn.  */
  if (maybe_clean_or_replace_eh_stmt (stmt, stmt)
      && tree_purge_dead_eh_edges (bb_for_stmt (stmt)))
    cfg_changed = true;

  if (TREE_CODE (TREE_OPERAND (stmt, 1)) == ADDR_EXPR)
     recompute_tree_invariant_for_addr_expr (TREE_OPERAND (stmt, 1));

  mark_new_vars_to_rename (stmt);
}

/* STMT defines LHS which is contains the address of the 0th element
   in an array.  USE_STMT uses LHS to compute the address of an
   arbitrary element within the array.  The (variable) byte offset
   of the element is contained in OFFSET.

   We walk back through the use-def chains of OFFSET to verify that
   it is indeed computing the offset of an element within the array
   and extract the index corresponding to the given byte offset.

   We then try to fold the entire address expression into a form
   &array[index].

   If we are successful, we replace the right hand side of USE_STMT
   with the new address computation.  */

static bool
forward_propagate_addr_into_variable_array_index (tree offset, tree lhs,
						  tree stmt, tree use_stmt)
{
  tree index;

  /* The offset must be defined by a simple MODIFY_EXPR statement.  */
  if (TREE_CODE (offset) != MODIFY_EXPR)
    return false;

  /* The RHS of the statement which defines OFFSET must be a gimple
     cast of another SSA_NAME.  */
  offset = TREE_OPERAND (offset, 1);
  if (!is_gimple_cast (offset))
    return false;

  offset = TREE_OPERAND (offset, 0);
  if (TREE_CODE (offset) != SSA_NAME)
    return false;

  /* Get the defining statement of the offset before type
     conversion.  */
  offset = SSA_NAME_DEF_STMT (offset);

  /* The statement which defines OFFSET before type conversion
     must be a simple MODIFY_EXPR.  */
  if (TREE_CODE (offset) != MODIFY_EXPR)
    return false;

  /* The RHS of the statement which defines OFFSET must be a
     multiplication of an object by the size of the array elements. 
     This implicitly verifies that the size of the array elements
     is constant.  */
  offset = TREE_OPERAND (offset, 1);
  if (TREE_CODE (offset) != MULT_EXPR
      || TREE_CODE (TREE_OPERAND (offset, 1)) != INTEGER_CST
      || !simple_cst_equal (TREE_OPERAND (offset, 1),
			    TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (lhs)))))
    return false;

  /* The first operand to the MULT_EXPR is the desired index.  */
  index = TREE_OPERAND (offset, 0);

  /* Replace the pointer addition with array indexing.  */
  TREE_OPERAND (use_stmt, 1) = unshare_expr (TREE_OPERAND (stmt, 1));
  TREE_OPERAND (TREE_OPERAND (TREE_OPERAND (use_stmt, 1), 0), 1) = index;

  /* That should have created gimple, so there is no need to
     record information to undo the propagation.  */
  fold_stmt_inplace (use_stmt);
  tidy_after_forward_propagate_addr (use_stmt);
  return true;
}

/* STMT is a statement of the form SSA_NAME = ADDR_EXPR <whatever>.

   Try to forward propagate the ADDR_EXPR into the use USE_STMT.
   Often this will allow for removal of an ADDR_EXPR and INDIRECT_REF
   node or for recovery of array indexing from pointer arithmetic.
   
   CHANGED is an optional pointer to a boolean variable set to true if
   either the LHS or RHS was changed in the USE_STMT.  

   Return true if the propagation was successful (the propagation can
   be not totally successful, yet things may have been changed).  */

static bool
forward_propagate_addr_expr_1 (tree stmt, tree use_stmt, bool *changed)
{
  tree name = TREE_OPERAND (stmt, 0);
  tree lhs, rhs, array_ref;

  /* Strip away any outer COMPONENT_REF/ARRAY_REF nodes from the LHS. 
     ADDR_EXPR will not appear on the LHS.  */
  lhs = TREE_OPERAND (use_stmt, 0);
  while (TREE_CODE (lhs) == COMPONENT_REF || TREE_CODE (lhs) == ARRAY_REF)
    lhs = TREE_OPERAND (lhs, 0);

  /* Now see if the LHS node is an INDIRECT_REF using NAME.  If so, 
     propagate the ADDR_EXPR into the use of NAME and fold the result.  */
  if (TREE_CODE (lhs) == INDIRECT_REF && TREE_OPERAND (lhs, 0) == name)
    {
      /* This should always succeed in creating gimple, so there is
	 no need to save enough state to undo this propagation.  */
      TREE_OPERAND (lhs, 0) = unshare_expr (TREE_OPERAND (stmt, 1));
      fold_stmt_inplace (use_stmt);
      tidy_after_forward_propagate_addr (use_stmt);
      if (changed)
	*changed = true;
    }

  /* Trivial case.  The use statement could be a trivial copy.  We
     go ahead and handle that case here since it's trivial and
     removes the need to run copy-prop before this pass to get
     the best results.  Also note that by handling this case here
     we can catch some cascading effects, ie the single use is
     in a copy, and the copy is used later by a single INDIRECT_REF
     for example.  */
  else if (TREE_CODE (lhs) == SSA_NAME && TREE_OPERAND (use_stmt, 1) == name)
    {
      TREE_OPERAND (use_stmt, 1) = unshare_expr (TREE_OPERAND (stmt, 1));
      tidy_after_forward_propagate_addr (use_stmt);
      if (changed)
	*changed = true;
      return true;
    }

  /* Strip away any outer COMPONENT_REF, ARRAY_REF or ADDR_EXPR
     nodes from the RHS.  */
  rhs = TREE_OPERAND (use_stmt, 1);
  while (TREE_CODE (rhs) == COMPONENT_REF
	 || TREE_CODE (rhs) == ARRAY_REF
	 || TREE_CODE (rhs) == ADDR_EXPR)
    rhs = TREE_OPERAND (rhs, 0);

  /* Now see if the RHS node is an INDIRECT_REF using NAME.  If so, 
     propagate the ADDR_EXPR into the use of NAME and fold the result.  */
  if (TREE_CODE (rhs) == INDIRECT_REF && TREE_OPERAND (rhs, 0) == name)
    {
      /* This should always succeed in creating gimple, so there is
         no need to save enough state to undo this propagation.  */
      TREE_OPERAND (rhs, 0) = unshare_expr (TREE_OPERAND (stmt, 1));
      fold_stmt_inplace (use_stmt);
      tidy_after_forward_propagate_addr (use_stmt);
      if (changed)
	*changed = true;
      return true;
    }

  /* The remaining cases are all for turning pointer arithmetic into
     array indexing.  They only apply when we have the address of
     element zero in an array.  If that is not the case then there
     is nothing to do.  */
  array_ref = TREE_OPERAND (TREE_OPERAND (stmt, 1), 0);
  if (TREE_CODE (array_ref) != ARRAY_REF
      || TREE_CODE (TREE_TYPE (TREE_OPERAND (array_ref, 0))) != ARRAY_TYPE
      || !integer_zerop (TREE_OPERAND (array_ref, 1)))
    return false;

  /* If the use of the ADDR_EXPR must be a PLUS_EXPR, or else there
     is nothing to do. */
  if (TREE_CODE (rhs) != PLUS_EXPR)
    return false;

  /* Try to optimize &x[0] + C where C is a multiple of the size
     of the elements in X into &x[C/element size].  */
  if (TREE_OPERAND (rhs, 0) == name
      && TREE_CODE (TREE_OPERAND (rhs, 1)) == INTEGER_CST)
    {
      tree orig = unshare_expr (rhs);
      TREE_OPERAND (rhs, 0) = unshare_expr (TREE_OPERAND (stmt, 1));

      /* If folding succeeds, then we have just exposed new variables
	 in USE_STMT which will need to be renamed.  If folding fails,
	 then we need to put everything back the way it was.  */
      if (fold_stmt_inplace (use_stmt))
	{
	  tidy_after_forward_propagate_addr (use_stmt);
	  if (changed)
	    *changed = true;
	  return true;
	}
      else
	{
	  TREE_OPERAND (use_stmt, 1) = orig;
	  update_stmt (use_stmt);
	  return false;
	}
    }

  /* Try to optimize &x[0] + OFFSET where OFFSET is defined by
     converting a multiplication of an index by the size of the
     array elements, then the result is converted into the proper
     type for the arithmetic.  */
  if (TREE_OPERAND (rhs, 0) == name
      && TREE_CODE (TREE_OPERAND (rhs, 1)) == SSA_NAME
      /* Avoid problems with IVopts creating PLUS_EXPRs with a
	 different type than their operands.  */
      && lang_hooks.types_compatible_p (TREE_TYPE (name), TREE_TYPE (rhs)))
    {
      bool res;
      tree offset_stmt = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 1));
      
      res = forward_propagate_addr_into_variable_array_index (offset_stmt, lhs,
							      stmt, use_stmt);
      if (res && changed)
	*changed = true;
      return res;
    }
	      
  /* Same as the previous case, except the operands of the PLUS_EXPR
     were reversed.  */
  if (TREE_OPERAND (rhs, 1) == name
      && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME
      /* Avoid problems with IVopts creating PLUS_EXPRs with a
	 different type than their operands.  */
      && lang_hooks.types_compatible_p (TREE_TYPE (name), TREE_TYPE (rhs)))
    {
      bool res;
      tree offset_stmt = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 0));
      res = forward_propagate_addr_into_variable_array_index (offset_stmt, lhs,
							      stmt, use_stmt);
      if (res && changed)
	*changed = true;
      return res;
    }
  return false;
}

/* STMT is a statement of the form SSA_NAME = ADDR_EXPR <whatever>.
   SOME is a pointer to a boolean value indicating whether we
   propagated the address expression anywhere.

   Try to forward propagate the ADDR_EXPR into all uses of the SSA_NAME.
   Often this will allow for removal of an ADDR_EXPR and INDIRECT_REF
   node or for recovery of array indexing from pointer arithmetic.
   Returns true, if all uses have been propagated into.  */

static bool
forward_propagate_addr_expr (tree stmt, bool *some)
{
  int stmt_loop_depth = bb_for_stmt (stmt)->loop_depth;
  tree name = TREE_OPERAND (stmt, 0);
  imm_use_iterator iter;
  tree use_stmt;
  bool all = true;

  FOR_EACH_IMM_USE_STMT (use_stmt, iter, name)
    {
      bool result;

      /* If the use is not in a simple assignment statement, then
	 there is nothing we can do.  */
      if (TREE_CODE (use_stmt) != MODIFY_EXPR)
	{
	  all = false;
	  continue;
	}

      /* If the use is in a deeper loop nest, then we do not want
	 to propagate the ADDR_EXPR into the loop as that is likely
	 adding expression evaluations into the loop.  */
      if (bb_for_stmt (use_stmt)->loop_depth > stmt_loop_depth)
	{
	  all = false;
	  continue;
	}

      /* If the use_stmt has side-effects, don't propagate into it.  */
      if (stmt_ann (use_stmt)->has_volatile_ops)
        {
	  all = false;
	  continue;
	}
 
      result = forward_propagate_addr_expr_1 (stmt, use_stmt, some);
      *some |= result;
      all &= result;
    }

  return all;
}

/* If we have lhs = ~x (STMT), look and see if earlier we had x = ~y.
   If so, we can change STMT into lhs = y which can later be copy
   propagated.  Similarly for negation. 

   This could trivially be formulated as a forward propagation 
   to immediate uses.  However, we already had an implementation
   from DOM which used backward propagation via the use-def links.

   It turns out that backward propagation is actually faster as
   there's less work to do for each NOT/NEG expression we find.
   Backwards propagation needs to look at the statement in a single
   backlink.  Forward propagation needs to look at potentially more
   than one forward link.  */

static void
simplify_not_neg_expr (tree stmt)
{
  tree rhs = TREE_OPERAND (stmt, 1);
  tree rhs_def_stmt = SSA_NAME_DEF_STMT (TREE_OPERAND (rhs, 0));

  /* See if the RHS_DEF_STMT has the same form as our statement.  */
  if (TREE_CODE (rhs_def_stmt) == MODIFY_EXPR
      && TREE_CODE (TREE_OPERAND (rhs_def_stmt, 1)) == TREE_CODE (rhs))
    {
      tree rhs_def_operand = TREE_OPERAND (TREE_OPERAND (rhs_def_stmt, 1), 0);

      /* Verify that RHS_DEF_OPERAND is a suitable SSA_NAME.  */
      if (TREE_CODE (rhs_def_operand) == SSA_NAME
	  && ! SSA_NAME_OCCURS_IN_ABNORMAL_PHI (rhs_def_operand))
	{
	  TREE_OPERAND (stmt, 1) = rhs_def_operand;
	  update_stmt (stmt);
	}
    }
}

/* STMT is a SWITCH_EXPR for which we attempt to find equivalent forms of
   the condition which we may be able to optimize better.  */

static void
simplify_switch_expr (tree stmt)
{
  tree cond = SWITCH_COND (stmt);
  tree def, to, ti;

  /* The optimization that we really care about is removing unnecessary
     casts.  That will let us do much better in propagating the inferred
     constant at the switch target.  */
  if (TREE_CODE (cond) == SSA_NAME)
    {
      def = SSA_NAME_DEF_STMT (cond);
      if (TREE_CODE (def) == MODIFY_EXPR)
	{
	  def = TREE_OPERAND (def, 1);
	  if (TREE_CODE (def) == NOP_EXPR)
	    {
	      int need_precision;
	      bool fail;

	      def = TREE_OPERAND (def, 0);

#ifdef ENABLE_CHECKING
	      /* ??? Why was Jeff testing this?  We are gimple...  */
	      gcc_assert (is_gimple_val (def));
#endif

	      to = TREE_TYPE (cond);
	      ti = TREE_TYPE (def);

	      /* If we have an extension that preserves value, then we
		 can copy the source value into the switch.  */

	      need_precision = TYPE_PRECISION (ti);
	      fail = false;
	      if (! INTEGRAL_TYPE_P (ti))
		fail = true;
	      else if (TYPE_UNSIGNED (to) && !TYPE_UNSIGNED (ti))
		fail = true;
	      else if (!TYPE_UNSIGNED (to) && TYPE_UNSIGNED (ti))
		need_precision += 1;
	      if (TYPE_PRECISION (to) < need_precision)
		fail = true;

	      if (!fail)
		{
		  SWITCH_COND (stmt) = def;
		  update_stmt (stmt);
		}
	    }
	}
    }
}

/* Main entry point for the forward propagation optimizer.  */

static unsigned int
tree_ssa_forward_propagate_single_use_vars (void)
{
  basic_block bb;
  unsigned int todoflags = 0;

  cfg_changed = false;

  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;

      /* Note we update BSI within the loop as necessary.  */
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); )
	{
	  tree stmt = bsi_stmt (bsi);

	  /* If this statement sets an SSA_NAME to an address,
	     try to propagate the address into the uses of the SSA_NAME.  */
	  if (TREE_CODE (stmt) == MODIFY_EXPR)
	    {
	      tree lhs = TREE_OPERAND (stmt, 0);
	      tree rhs = TREE_OPERAND (stmt, 1);


	      if (TREE_CODE (lhs) != SSA_NAME)
		{
		  bsi_next (&bsi);
		  continue;
		}

	      if (TREE_CODE (rhs) == ADDR_EXPR)
		{
		  bool some = false;
		  if (forward_propagate_addr_expr (stmt, &some))
		    bsi_remove (&bsi, true);
		  else
		    bsi_next (&bsi);
		  if (some)
		    todoflags |= TODO_update_smt_usage;
		}
	      else if ((TREE_CODE (rhs) == BIT_NOT_EXPR
		        || TREE_CODE (rhs) == NEGATE_EXPR)
		       && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME)
		{
		  simplify_not_neg_expr (stmt);
		  bsi_next (&bsi);
		}
	      else
		bsi_next (&bsi);
	    }
	  else if (TREE_CODE (stmt) == SWITCH_EXPR)
	    {
	      simplify_switch_expr (stmt);
	      bsi_next (&bsi);
	    }
	  else if (TREE_CODE (stmt) == COND_EXPR)
	    {
	      forward_propagate_into_cond (stmt);
	      bsi_next (&bsi);
	    }
	  else
	    bsi_next (&bsi);
	}
    }

  if (cfg_changed)
    cleanup_tree_cfg ();
  return todoflags;
}


static bool
gate_forwprop (void)
{
  return 1;
}

struct tree_opt_pass pass_forwprop = {
  "forwprop",			/* name */
  gate_forwprop,		/* gate */
  tree_ssa_forward_propagate_single_use_vars,	/* execute */
  NULL,				/* sub */
  NULL,				/* next */
  0,				/* static_pass_number */
  TV_TREE_FORWPROP,		/* tv_id */
  PROP_cfg | PROP_ssa
    | PROP_alias,		/* properties_required */
  0,				/* properties_provided */
  PROP_smt_usage,		/* properties_destroyed */
  0,				/* todo_flags_start */
  TODO_dump_func /* todo_flags_finish */
  | TODO_ggc_collect
  | TODO_update_ssa | TODO_verify_ssa,
  0					/* letter */
};
