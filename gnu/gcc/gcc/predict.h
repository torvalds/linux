/* Definitions for branch prediction routines in the GNU compiler.
   Copyright (C) 2001, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_PREDICT_H
#define GCC_PREDICT_H

#define DEF_PREDICTOR(ENUM, NAME, HITRATE, FLAGS) ENUM,
enum br_predictor
{
#include "predict.def"

  /* Upper bound on non-language-specific builtins.  */
  END_PREDICTORS
};
#undef DEF_PREDICTOR
enum prediction
{
   NOT_TAKEN,
   TAKEN
};

extern void predict_insn_def (rtx, enum br_predictor, enum prediction);
extern int counts_to_freqs (void);

#endif  /* GCC_PREDICT_H */
