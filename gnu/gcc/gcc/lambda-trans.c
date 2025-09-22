/* Lambda matrix transformations.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>.

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
#include "ggc.h"
#include "tree.h"
#include "target.h"
#include "lambda.h"

/* Allocate a new transformation matrix.  */

lambda_trans_matrix
lambda_trans_matrix_new (int colsize, int rowsize)
{
  lambda_trans_matrix ret;
  
  ret = ggc_alloc (sizeof (*ret));
  LTM_MATRIX (ret) = lambda_matrix_new (rowsize, colsize);
  LTM_ROWSIZE (ret) = rowsize;
  LTM_COLSIZE (ret) = colsize;
  LTM_DENOMINATOR (ret) = 1;
  return ret;
}

/* Return true if MAT is an identity matrix.  */

bool
lambda_trans_matrix_id_p (lambda_trans_matrix mat)
{
  if (LTM_ROWSIZE (mat) != LTM_COLSIZE (mat))
    return false;
  return lambda_matrix_id_p (LTM_MATRIX (mat), LTM_ROWSIZE (mat));
}


/* Compute the inverse of the transformation matrix MAT.  */

lambda_trans_matrix 
lambda_trans_matrix_inverse (lambda_trans_matrix mat)
{
  lambda_trans_matrix inverse;
  int determinant;
  
  inverse = lambda_trans_matrix_new (LTM_ROWSIZE (mat), LTM_COLSIZE (mat));
  determinant = lambda_matrix_inverse (LTM_MATRIX (mat), LTM_MATRIX (inverse), 
				       LTM_ROWSIZE (mat));
  LTM_DENOMINATOR (inverse) = determinant;
  return inverse;
}


/* Print out a transformation matrix.  */

void
print_lambda_trans_matrix (FILE *outfile, lambda_trans_matrix mat)
{
  print_lambda_matrix (outfile, LTM_MATRIX (mat), LTM_ROWSIZE (mat), 
		       LTM_COLSIZE (mat));
}
