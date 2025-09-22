/* Subroutines used for code generation on IA-32.
   Copyright (C) 1988, 1992, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-codes.h"
#include "insn-attr.h"
#include "flags.h"
#include "except.h"
#include "function.h"
#include "recog.h"
#include "expr.h"
#include "optabs.h"
#include "toplev.h"
#include "basic-block.h"
#include "ggc.h"
#include "target.h"
#include "target-def.h"
#include "langhooks.h"
#include "cgraph.h"
#include "tree-gimple.h"
#include "dwarf2.h"
#include "tm-constrs.h"

#ifndef CHECK_STACK_LIMIT
#define CHECK_STACK_LIMIT (-1)
#endif

/* Return index of given mode in mult and division cost tables.  */
#define MODE_INDEX(mode)					\
  ((mode) == QImode ? 0						\
   : (mode) == HImode ? 1					\
   : (mode) == SImode ? 2					\
   : (mode) == DImode ? 3					\
   : 4)

/* Processor costs (relative to an add) */
/* We assume COSTS_N_INSNS is defined as (N)*4 and an addition is 2 bytes.  */
#define COSTS_N_BYTES(N) ((N) * 2)

static const
struct processor_costs size_cost = {	/* costs for tuning for size */
  COSTS_N_BYTES (2),			/* cost of an add instruction */
  COSTS_N_BYTES (3),			/* cost of a lea instruction */
  COSTS_N_BYTES (2),			/* variable shift costs */
  COSTS_N_BYTES (3),			/* constant shift costs */
  {COSTS_N_BYTES (3),			/* cost of starting multiply for QI */
   COSTS_N_BYTES (3),			/*                               HI */
   COSTS_N_BYTES (3),			/*                               SI */
   COSTS_N_BYTES (3),			/*                               DI */
   COSTS_N_BYTES (5)},			/*                            other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_BYTES (3),			/* cost of a divide/mod for QI */
   COSTS_N_BYTES (3),			/*                          HI */
   COSTS_N_BYTES (3),			/*                          SI */
   COSTS_N_BYTES (3),			/*                          DI */
   COSTS_N_BYTES (5)},			/*                       other */
  COSTS_N_BYTES (3),			/* cost of movsx */
  COSTS_N_BYTES (3),			/* cost of movzx */
  0,					/* "large" insn */
  2,					/* MOVE_RATIO */
  2,					/* cost for loading QImode using movzbl */
  {2, 2, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 2, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 2},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {2, 2, 2},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  3,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {3, 3},				/* cost of storing MMX registers
					   in SImode and DImode */
  3,					/* cost of moving SSE register */
  {3, 3, 3},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {3, 3, 3},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_BYTES (2),			/* cost of FADD and FSUB insns.  */
  COSTS_N_BYTES (2),			/* cost of FMUL instruction.  */
  COSTS_N_BYTES (2),			/* cost of FDIV instruction.  */
  COSTS_N_BYTES (2),			/* cost of FABS instruction.  */
  COSTS_N_BYTES (2),			/* cost of FCHS instruction.  */
  COSTS_N_BYTES (2),			/* cost of FSQRT instruction.  */
};

/* Processor costs (relative to an add) */
static const
struct processor_costs i386_cost = {	/* 386 specific costs */
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (3),			/* variable shift costs */
  COSTS_N_INSNS (2),			/* constant shift costs */
  {COSTS_N_INSNS (6),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (6),			/*                               HI */
   COSTS_N_INSNS (6),			/*                               SI */
   COSTS_N_INSNS (6),			/*                               DI */
   COSTS_N_INSNS (6)},			/*                               other */
  COSTS_N_INSNS (1),			/* cost of multiply per each bit set */
  {COSTS_N_INSNS (23),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (23),			/*                          HI */
   COSTS_N_INSNS (23),			/*                          SI */
   COSTS_N_INSNS (23),			/*                          DI */
   COSTS_N_INSNS (23)},			/*                          other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  15,					/* "large" insn */
  3,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {8, 8, 8},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {8, 8, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (23),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (27),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (88),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (22),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (24),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (122),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs i486_cost = {	/* 486 specific costs */
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (3),			/* variable shift costs */
  COSTS_N_INSNS (2),			/* constant shift costs */
  {COSTS_N_INSNS (12),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (12),			/*                               HI */
   COSTS_N_INSNS (12),			/*                               SI */
   COSTS_N_INSNS (12),			/*                               DI */
   COSTS_N_INSNS (12)},			/*                               other */
  1,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (40),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (40),			/*                          HI */
   COSTS_N_INSNS (40),			/*                          SI */
   COSTS_N_INSNS (40),			/*                          DI */
   COSTS_N_INSNS (40)},			/*                          other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  15,					/* "large" insn */
  3,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {8, 8, 8},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {8, 8, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (16),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (73),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (3),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (3),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (83),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs pentium_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (4),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (11),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (11),			/*                               HI */
   COSTS_N_INSNS (11),			/*                               SI */
   COSTS_N_INSNS (11),			/*                               DI */
   COSTS_N_INSNS (11)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (25),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (25),			/*                          HI */
   COSTS_N_INSNS (25),			/*                          SI */
   COSTS_N_INSNS (25),			/*                          DI */
   COSTS_N_INSNS (25)},			/*                          other */
  COSTS_N_INSNS (3),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  8,					/* "large" insn */
  6,					/* MOVE_RATIO */
  6,					/* cost for loading QImode using movzbl */
  {2, 4, 2},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 4, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  8,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 8, 16},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 8, 16},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  0,					/* size of prefetch block */
  0,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (3),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (3),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (39),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (1),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (1),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (70),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs pentiumpro_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (4),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*                               HI */
   COSTS_N_INSNS (4),			/*                               SI */
   COSTS_N_INSNS (4),			/*                               DI */
   COSTS_N_INSNS (4)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (17),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (17),			/*                          HI */
   COSTS_N_INSNS (17),			/*                          SI */
   COSTS_N_INSNS (17),			/*                          DI */
   COSTS_N_INSNS (17)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  6,					/* MOVE_RATIO */
  2,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 2, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {2, 2, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  3,					/* MMX or SSE register to integer */
  32,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (3),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (5),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (56),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (56),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs k6_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (3),			/*                               HI */
   COSTS_N_INSNS (3),			/*                               SI */
   COSTS_N_INSNS (3),			/*                               DI */
   COSTS_N_INSNS (3)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (18),			/*                          HI */
   COSTS_N_INSNS (18),			/*                          SI */
   COSTS_N_INSNS (18),			/*                          DI */
   COSTS_N_INSNS (18)},			/*                          other */
  COSTS_N_INSNS (2),			/* cost of movsx */
  COSTS_N_INSNS (2),			/* cost of movzx */
  8,					/* "large" insn */
  4,					/* MOVE_RATIO */
  3,					/* cost for loading QImode using movzbl */
  {4, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 3, 2},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {6, 6, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 4},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {2, 2, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  6,					/* MMX or SSE register to integer */
  32,					/* size of prefetch block */
  1,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (2),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (2),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (56),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (56),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs athlon_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (5),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (5),			/*                               HI */
   COSTS_N_INSNS (5),			/*                               SI */
   COSTS_N_INSNS (5),			/*                               DI */
   COSTS_N_INSNS (5)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*                          HI */
   COSTS_N_INSNS (42),			/*                          SI */
   COSTS_N_INSNS (74),			/*                          DI */
   COSTS_N_INSNS (74)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {4, 4},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 4, 6},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  5,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (24),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs k8_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (2),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*                               HI */
   COSTS_N_INSNS (3),			/*                               SI */
   COSTS_N_INSNS (4),			/*                               DI */
   COSTS_N_INSNS (5)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*                          HI */
   COSTS_N_INSNS (42),			/*                          SI */
   COSTS_N_INSNS (74),			/*                          DI */
   COSTS_N_INSNS (74)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  9,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {3, 4, 3},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {3, 4, 3},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {4, 4, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {3, 3},				/* cost of loading MMX registers
					   in SImode and DImode */
  {4, 4},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {4, 3, 6},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {4, 4, 5},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  5,					/* Branch cost */
  COSTS_N_INSNS (4),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (4),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (19),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (35),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs pentium4_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (3),			/* cost of a lea instruction */
  COSTS_N_INSNS (4),			/* variable shift costs */
  COSTS_N_INSNS (4),			/* constant shift costs */
  {COSTS_N_INSNS (15),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (15),			/*                               HI */
   COSTS_N_INSNS (15),			/*                               SI */
   COSTS_N_INSNS (15),			/*                               DI */
   COSTS_N_INSNS (15)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (56),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (56),			/*                          HI */
   COSTS_N_INSNS (56),			/*                          SI */
   COSTS_N_INSNS (56),			/*                          DI */
   COSTS_N_INSNS (56)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  16,					/* "large" insn */
  6,					/* MOVE_RATIO */
  2,					/* cost for loading QImode using movzbl */
  {4, 5, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {2, 3, 2},				/* cost of storing integer registers */
  2,					/* cost of reg,reg fld/fst */
  {2, 2, 6},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 6},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {2, 2},				/* cost of loading MMX registers
					   in SImode and DImode */
  {2, 2},				/* cost of storing MMX registers
					   in SImode and DImode */
  12,					/* cost of moving SSE register */
  {12, 12, 12},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {2, 2, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  10,					/* MMX or SSE register to integer */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  2,					/* Branch cost */
  COSTS_N_INSNS (5),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (7),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (43),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (2),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (2),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (43),			/* cost of FSQRT instruction.  */
};

static const
struct processor_costs nocona_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1),			/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (10),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (10),			/*                               HI */
   COSTS_N_INSNS (10),			/*                               SI */
   COSTS_N_INSNS (10),			/*                               DI */
   COSTS_N_INSNS (10)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (66),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (66),			/*                          HI */
   COSTS_N_INSNS (66),			/*                          SI */
   COSTS_N_INSNS (66),			/*                          DI */
   COSTS_N_INSNS (66)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  16,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  3,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {4, 4, 4},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  6,					/* cost of moving MMX register */
  {12, 12},				/* cost of loading MMX registers
					   in SImode and DImode */
  {12, 12},				/* cost of storing MMX registers
					   in SImode and DImode */
  6,					/* cost of moving SSE register */
  {12, 12, 12},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {12, 12, 12},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  8,					/* MMX or SSE register to integer */
  128,					/* size of prefetch block */
  8,					/* number of parallel prefetches */
  1,					/* Branch cost */
  COSTS_N_INSNS (6),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (40),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (3),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (3),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (44),			/* cost of FSQRT instruction.  */
};

/* Generic64 should produce code tuned for Nocona and K8.  */
static const
struct processor_costs generic64_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  /* On all chips taken into consideration lea is 2 cycles and more.  With
     this cost however our current implementation of synth_mult results in
     use of unnecessary temporary registers causing regression on several
     SPECfp benchmarks.  */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*                               HI */
   COSTS_N_INSNS (3),			/*                               SI */
   COSTS_N_INSNS (4),			/*                               DI */
   COSTS_N_INSNS (2)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*                          HI */
   COSTS_N_INSNS (42),			/*                          SI */
   COSTS_N_INSNS (74),			/*                          DI */
   COSTS_N_INSNS (74)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  /* Benchmarks shows large regressions on K8 sixtrack benchmark when this value
     is increased to perhaps more appropriate value of 5.  */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
};

/* Generic32 should produce code tuned for Athlon, PPro, Pentium4, Nocona and K8.  */
static const
struct processor_costs generic32_cost = {
  COSTS_N_INSNS (1),			/* cost of an add instruction */
  COSTS_N_INSNS (1) + 1,		/* cost of a lea instruction */
  COSTS_N_INSNS (1),			/* variable shift costs */
  COSTS_N_INSNS (1),			/* constant shift costs */
  {COSTS_N_INSNS (3),			/* cost of starting multiply for QI */
   COSTS_N_INSNS (4),			/*                               HI */
   COSTS_N_INSNS (3),			/*                               SI */
   COSTS_N_INSNS (4),			/*                               DI */
   COSTS_N_INSNS (2)},			/*                               other */
  0,					/* cost of multiply per each bit set */
  {COSTS_N_INSNS (18),			/* cost of a divide/mod for QI */
   COSTS_N_INSNS (26),			/*                          HI */
   COSTS_N_INSNS (42),			/*                          SI */
   COSTS_N_INSNS (74),			/*                          DI */
   COSTS_N_INSNS (74)},			/*                          other */
  COSTS_N_INSNS (1),			/* cost of movsx */
  COSTS_N_INSNS (1),			/* cost of movzx */
  8,					/* "large" insn */
  17,					/* MOVE_RATIO */
  4,					/* cost for loading QImode using movzbl */
  {4, 4, 4},				/* cost of loading integer registers
					   in QImode, HImode and SImode.
					   Relative to reg-reg move (2).  */
  {4, 4, 4},				/* cost of storing integer registers */
  4,					/* cost of reg,reg fld/fst */
  {12, 12, 12},				/* cost of loading fp registers
					   in SFmode, DFmode and XFmode */
  {6, 6, 8},				/* cost of storing fp registers
					   in SFmode, DFmode and XFmode */
  2,					/* cost of moving MMX register */
  {8, 8},				/* cost of loading MMX registers
					   in SImode and DImode */
  {8, 8},				/* cost of storing MMX registers
					   in SImode and DImode */
  2,					/* cost of moving SSE register */
  {8, 8, 8},				/* cost of loading SSE registers
					   in SImode, DImode and TImode */
  {8, 8, 8},				/* cost of storing SSE registers
					   in SImode, DImode and TImode */
  5,					/* MMX or SSE register to integer */
  64,					/* size of prefetch block */
  6,					/* number of parallel prefetches */
  3,					/* Branch cost */
  COSTS_N_INSNS (8),			/* cost of FADD and FSUB insns.  */
  COSTS_N_INSNS (8),			/* cost of FMUL instruction.  */
  COSTS_N_INSNS (20),			/* cost of FDIV instruction.  */
  COSTS_N_INSNS (8),			/* cost of FABS instruction.  */
  COSTS_N_INSNS (8),			/* cost of FCHS instruction.  */
  COSTS_N_INSNS (40),			/* cost of FSQRT instruction.  */
};

const struct processor_costs *ix86_cost = &pentium_cost;

/* Processor feature/optimization bitmasks.  */
#define m_386 (1<<PROCESSOR_I386)
#define m_486 (1<<PROCESSOR_I486)
#define m_PENT (1<<PROCESSOR_PENTIUM)
#define m_PPRO (1<<PROCESSOR_PENTIUMPRO)
#define m_K6  (1<<PROCESSOR_K6)
#define m_ATHLON  (1<<PROCESSOR_ATHLON)
#define m_PENT4  (1<<PROCESSOR_PENTIUM4)
#define m_K8  (1<<PROCESSOR_K8)
#define m_ATHLON_K8  (m_K8 | m_ATHLON)
#define m_NOCONA  (1<<PROCESSOR_NOCONA)
#define m_GENERIC32 (1<<PROCESSOR_GENERIC32)
#define m_GENERIC64 (1<<PROCESSOR_GENERIC64)
#define m_GENERIC (m_GENERIC32 | m_GENERIC64)

/* Generic instruction choice should be common subset of supported CPUs
   (PPro/PENT4/NOCONA/Athlon/K8).  */

/* Leave is not affecting Nocona SPEC2000 results negatively, so enabling for
   Generic64 seems like good code size tradeoff.  We can't enable it for 32bit
   generic because it is not working well with PPro base chips.  */
const int x86_use_leave = m_386 | m_K6 | m_ATHLON_K8 | m_GENERIC64;
const int x86_push_memory = m_386 | m_K6 | m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_zero_extend_with_and = m_486 | m_PENT;
const int x86_movx = m_ATHLON_K8 | m_PPRO | m_PENT4 | m_NOCONA | m_GENERIC /* m_386 | m_K6 */;
const int x86_double_with_add = ~m_386;
const int x86_use_bit_test = m_386;
const int x86_unroll_strlen = m_486 | m_PENT | m_PPRO | m_ATHLON_K8 | m_K6 | m_GENERIC;
const int x86_cmove = m_PPRO | m_ATHLON_K8 | m_PENT4 | m_NOCONA;
const int x86_3dnow_a = m_ATHLON_K8;
const int x86_deep_branch = m_PPRO | m_K6 | m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
/* Branch hints were put in P4 based on simulation result. But
   after P4 was made, no performance benefit was observed with
   branch hints. It also increases the code size. As the result,
   icc never generates branch hints.  */
const int x86_branch_hints = 0;
const int x86_use_sahf = m_PPRO | m_K6 | m_PENT4 | m_NOCONA | m_GENERIC32; /*m_GENERIC | m_ATHLON_K8 ? */
/* We probably ought to watch for partial register stalls on Generic32
   compilation setting as well.  However in current implementation the
   partial register stalls are not eliminated very well - they can
   be introduced via subregs synthesized by combine and can happen
   in caller/callee saving sequences.
   Because this option pays back little on PPro based chips and is in conflict
   with partial reg. dependencies used by Athlon/P4 based chips, it is better
   to leave it off for generic32 for now.  */
const int x86_partial_reg_stall = m_PPRO;
const int x86_partial_flag_reg_stall = m_GENERIC;
const int x86_use_himode_fiop = m_386 | m_486 | m_K6;
const int x86_use_simode_fiop = ~(m_PPRO | m_ATHLON_K8 | m_PENT | m_GENERIC);
const int x86_use_mov0 = m_K6;
const int x86_use_cltd = ~(m_PENT | m_K6 | m_GENERIC);
const int x86_read_modify_write = ~m_PENT;
const int x86_read_modify = ~(m_PENT | m_PPRO);
const int x86_split_long_moves = m_PPRO;
const int x86_promote_QImode = m_K6 | m_PENT | m_386 | m_486 | m_ATHLON_K8 | m_GENERIC; /* m_PENT4 ? */
const int x86_fast_prefix = ~(m_PENT | m_486 | m_386);
const int x86_single_stringop = m_386 | m_PENT4 | m_NOCONA;
const int x86_qimode_math = ~(0);
const int x86_promote_qi_regs = 0;
/* On PPro this flag is meant to avoid partial register stalls.  Just like
   the x86_partial_reg_stall this option might be considered for Generic32
   if our scheme for avoiding partial stalls was more effective.  */
const int x86_himode_math = ~(m_PPRO);
const int x86_promote_hi_regs = m_PPRO;
const int x86_sub_esp_4 = m_ATHLON_K8 | m_PPRO | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_sub_esp_8 = m_ATHLON_K8 | m_PPRO | m_386 | m_486 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_add_esp_4 = m_ATHLON_K8 | m_K6 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_add_esp_8 = m_ATHLON_K8 | m_PPRO | m_K6 | m_386 | m_486 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_integer_DFmode_moves = ~(m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_PPRO | m_GENERIC);
const int x86_partial_reg_dependency = m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_memory_mismatch_stall = m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_accumulate_outgoing_args = m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_PPRO | m_GENERIC;
const int x86_prologue_using_move = m_ATHLON_K8 | m_PPRO | m_GENERIC;
const int x86_epilogue_using_move = m_ATHLON_K8 | m_PPRO | m_GENERIC;
const int x86_shift1 = ~m_486;
const int x86_arch_always_fancy_math_387 = m_PENT | m_PPRO | m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
/* In Generic model we have an conflict here in between PPro/Pentium4 based chips
   that thread 128bit SSE registers as single units versus K8 based chips that
   divide SSE registers to two 64bit halves.
   x86_sse_partial_reg_dependency promote all store destinations to be 128bit
   to allow register renaming on 128bit SSE units, but usually results in one
   extra microop on 64bit SSE units.  Experimental results shows that disabling
   this option on P4 brings over 20% SPECfp regression, while enabling it on
   K8 brings roughly 2.4% regression that can be partly masked by careful scheduling
   of moves.  */
const int x86_sse_partial_reg_dependency = m_PENT4 | m_NOCONA | m_PPRO | m_GENERIC;
/* Set for machines where the type and dependencies are resolved on SSE
   register parts instead of whole registers, so we may maintain just
   lower part of scalar values in proper format leaving the upper part
   undefined.  */
const int x86_sse_split_regs = m_ATHLON_K8;
const int x86_sse_typeless_stores = m_ATHLON_K8;
const int x86_sse_load0_by_pxor = m_PPRO | m_PENT4 | m_NOCONA;
const int x86_use_ffreep = m_ATHLON_K8;
const int x86_rep_movl_optimal = m_386 | m_PENT | m_PPRO | m_K6;
const int x86_use_incdec = ~(m_PENT4 | m_NOCONA | m_GENERIC);

/* ??? Allowing interunit moves makes it all too easy for the compiler to put
   integer data in xmm registers.  Which results in pretty abysmal code.  */
const int x86_inter_unit_moves = 0 /* ~(m_ATHLON_K8) */;

const int x86_ext_80387_constants = m_K6 | m_ATHLON | m_PENT4 | m_NOCONA | m_PPRO | m_GENERIC32;
/* Some CPU cores are not able to predict more than 4 branch instructions in
   the 16 byte window.  */
const int x86_four_jump_limit = m_PPRO | m_ATHLON_K8 | m_PENT4 | m_NOCONA | m_GENERIC;
const int x86_schedule = m_PPRO | m_ATHLON_K8 | m_K6 | m_PENT | m_GENERIC;
const int x86_use_bt = m_ATHLON_K8;
/* Compare and exchange was added for 80486.  */
const int x86_cmpxchg = ~m_386;
/* Compare and exchange 8 bytes was added for pentium.  */
const int x86_cmpxchg8b = ~(m_386 | m_486);
/* Compare and exchange 16 bytes was added for nocona.  */
const int x86_cmpxchg16b = m_NOCONA;
/* Exchange and add was added for 80486.  */
const int x86_xadd = ~m_386;
const int x86_pad_returns = m_ATHLON_K8 | m_GENERIC;

/* In case the average insn count for single function invocation is
   lower than this constant, emit fast (but longer) prologue and
   epilogue code.  */
#define FAST_PROLOGUE_INSN_COUNT 20

/* Names for 8 (low), 8 (high), and 16-bit registers, respectively.  */
static const char *const qi_reg_name[] = QI_REGISTER_NAMES;
static const char *const qi_high_reg_name[] = QI_HIGH_REGISTER_NAMES;
static const char *const hi_reg_name[] = HI_REGISTER_NAMES;

/* Array of the smallest class containing reg number REGNO, indexed by
   REGNO.  Used by REGNO_REG_CLASS in i386.h.  */

enum reg_class const regclass_map[FIRST_PSEUDO_REGISTER] =
{
  /* ax, dx, cx, bx */
  AREG, DREG, CREG, BREG,
  /* si, di, bp, sp */
  SIREG, DIREG, NON_Q_REGS, NON_Q_REGS,
  /* FP registers */
  FP_TOP_REG, FP_SECOND_REG, FLOAT_REGS, FLOAT_REGS,
  FLOAT_REGS, FLOAT_REGS, FLOAT_REGS, FLOAT_REGS,
  /* arg pointer */
  NON_Q_REGS,
  /* flags, fpsr, dirflag, frame */
  NO_REGS, NO_REGS, NO_REGS, NON_Q_REGS,
  SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS,
  SSE_REGS, SSE_REGS,
  MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS, MMX_REGS,
  MMX_REGS, MMX_REGS,
  NON_Q_REGS, NON_Q_REGS, NON_Q_REGS, NON_Q_REGS,
  NON_Q_REGS, NON_Q_REGS, NON_Q_REGS, NON_Q_REGS,
  SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS, SSE_REGS,
  SSE_REGS, SSE_REGS,
};

/* The "default" register map used in 32bit mode.  */

int const dbx_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 2, 1, 3, 6, 7, 4, 5,		/* general regs */
  12, 13, 14, 15, 16, 17, 18, 19,	/* fp regs */
  -1, -1, -1, -1, -1,			/* arg, flags, fpsr, dir, frame */
  21, 22, 23, 24, 25, 26, 27, 28,	/* SSE */
  29, 30, 31, 32, 33, 34, 35, 36,       /* MMX */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended integer registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended SSE registers */
};

static int const x86_64_int_parameter_registers[6] =
{
  5 /*RDI*/, 4 /*RSI*/, 1 /*RDX*/, 2 /*RCX*/,
  FIRST_REX_INT_REG /*R8 */, FIRST_REX_INT_REG + 1 /*R9 */
};

static int const x86_64_int_return_registers[4] =
{
  0 /*RAX*/, 1 /*RDI*/, 5 /*RDI*/, 4 /*RSI*/
};

/* The "default" register map used in 64bit mode.  */
int const dbx64_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 1, 2, 3, 4, 5, 6, 7,		/* general regs */
  33, 34, 35, 36, 37, 38, 39, 40,	/* fp regs */
  -1, -1, -1, -1, -1,			/* arg, flags, fpsr, dir, frame */
  17, 18, 19, 20, 21, 22, 23, 24,	/* SSE */
  41, 42, 43, 44, 45, 46, 47, 48,       /* MMX */
  8,9,10,11,12,13,14,15,		/* extended integer registers */
  25, 26, 27, 28, 29, 30, 31, 32,	/* extended SSE registers */
};

/* Define the register numbers to be used in Dwarf debugging information.
   The SVR4 reference port C compiler uses the following register numbers
   in its Dwarf output code:
	0 for %eax (gcc regno = 0)
	1 for %ecx (gcc regno = 2)
	2 for %edx (gcc regno = 1)
	3 for %ebx (gcc regno = 3)
	4 for %esp (gcc regno = 7)
	5 for %ebp (gcc regno = 6)
	6 for %esi (gcc regno = 4)
	7 for %edi (gcc regno = 5)
   The following three DWARF register numbers are never generated by
   the SVR4 C compiler or by the GNU compilers, but SDB on x86/svr4
   believes these numbers have these meanings.
	8  for %eip    (no gcc equivalent)
	9  for %eflags (gcc regno = 17)
	10 for %trapno (no gcc equivalent)
   It is not at all clear how we should number the FP stack registers
   for the x86 architecture.  If the version of SDB on x86/svr4 were
   a bit less brain dead with respect to floating-point then we would
   have a precedent to follow with respect to DWARF register numbers
   for x86 FP registers, but the SDB on x86/svr4 is so completely
   broken with respect to FP registers that it is hardly worth thinking
   of it as something to strive for compatibility with.
   The version of x86/svr4 SDB I have at the moment does (partially)
   seem to believe that DWARF register number 11 is associated with
   the x86 register %st(0), but that's about all.  Higher DWARF
   register numbers don't seem to be associated with anything in
   particular, and even for DWARF regno 11, SDB only seems to under-
   stand that it should say that a variable lives in %st(0) (when
   asked via an `=' command) if we said it was in DWARF regno 11,
   but SDB still prints garbage when asked for the value of the
   variable in question (via a `/' command).
   (Also note that the labels SDB prints for various FP stack regs
   when doing an `x' command are all wrong.)
   Note that these problems generally don't affect the native SVR4
   C compiler because it doesn't allow the use of -O with -g and
   because when it is *not* optimizing, it allocates a memory
   location for each floating-point variable, and the memory
   location is what gets described in the DWARF AT_location
   attribute for the variable in question.
   Regardless of the severe mental illness of the x86/svr4 SDB, we
   do something sensible here and we use the following DWARF
   register numbers.  Note that these are all stack-top-relative
   numbers.
	11 for %st(0) (gcc regno = 8)
	12 for %st(1) (gcc regno = 9)
	13 for %st(2) (gcc regno = 10)
	14 for %st(3) (gcc regno = 11)
	15 for %st(4) (gcc regno = 12)
	16 for %st(5) (gcc regno = 13)
	17 for %st(6) (gcc regno = 14)
	18 for %st(7) (gcc regno = 15)
*/
int const svr4_dbx_register_map[FIRST_PSEUDO_REGISTER] =
{
  0, 2, 1, 3, 6, 7, 5, 4,		/* general regs */
  11, 12, 13, 14, 15, 16, 17, 18,	/* fp regs */
  -1, 9, -1, -1, -1,			/* arg, flags, fpsr, dir, frame */
  21, 22, 23, 24, 25, 26, 27, 28,	/* SSE registers */
  29, 30, 31, 32, 33, 34, 35, 36,	/* MMX registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended integer registers */
  -1, -1, -1, -1, -1, -1, -1, -1,	/* extended SSE registers */
};

/* Test and compare insns in i386.md store the information needed to
   generate branch and scc insns here.  */

rtx ix86_compare_op0 = NULL_RTX;
rtx ix86_compare_op1 = NULL_RTX;
rtx ix86_compare_emitted = NULL_RTX;

/* Size of the register save area.  */
#define X86_64_VARARGS_SIZE (REGPARM_MAX * UNITS_PER_WORD + SSE_REGPARM_MAX * 16)

/* Define the structure for the machine field in struct function.  */

struct stack_local_entry GTY(())
{
  unsigned short mode;
  unsigned short n;
  rtx rtl;
  struct stack_local_entry *next;
};

/* Structure describing stack frame layout.
   Stack grows downward:

   [arguments]
					      <- ARG_POINTER
   saved pc

   saved frame pointer if frame_pointer_needed
					      <- HARD_FRAME_POINTER
   [-msave-args]

   [padding0]

   [saved regs]

   [padding1]          \
		        )
   [va_arg registers]  (
		        > to_allocate	      <- FRAME_POINTER
   [frame]	       (
		        )
   [padding2]	       /
  */
struct ix86_frame
{
  int nmsave_args;
  int padding0;
  int nregs;
  int padding1;
  int va_arg_size;
  HOST_WIDE_INT frame;
  int padding2;
  int outgoing_arguments_size;
  int red_zone_size;

  HOST_WIDE_INT to_allocate;
  /* The offsets relative to ARG_POINTER.  */
  HOST_WIDE_INT frame_pointer_offset;
  HOST_WIDE_INT hard_frame_pointer_offset;
  HOST_WIDE_INT stack_pointer_offset;

  HOST_WIDE_INT local_size;

  /* When save_regs_using_mov is set, emit prologue using
     move instead of push instructions.  */
  bool save_regs_using_mov;
};

/* Code model option.  */
enum cmodel ix86_cmodel;
/* Asm dialect.  */
enum asm_dialect ix86_asm_dialect = ASM_ATT;
/* TLS dialects.  */
enum tls_dialect ix86_tls_dialect = TLS_DIALECT_GNU;

/* Which unit we are generating floating point math for.  */
enum fpmath_unit ix86_fpmath;

/* Which cpu are we scheduling for.  */
enum processor_type ix86_tune;
/* Which instruction set architecture to use.  */
enum processor_type ix86_arch;

/* true if sse prefetch instruction is not NOOP.  */
int x86_prefetch_sse;

/* ix86_regparm_string as a number */
static int ix86_regparm;

/* -mstackrealign option */
extern int ix86_force_align_arg_pointer;
static const char ix86_force_align_arg_pointer_string[] = "force_align_arg_pointer";

/* Preferred alignment for stack boundary in bits.  */
unsigned int ix86_preferred_stack_boundary;

/* Values 1-5: see jump.c */
int ix86_branch_cost;

/* Variables which are this size or smaller are put in the data/bss
   or ldata/lbss sections.  */

int ix86_section_threshold = 65536;

/* Prefix built by ASM_GENERATE_INTERNAL_LABEL.  */
char internal_label_prefix[16];
int internal_label_prefix_len;

static bool ix86_handle_option (size_t, const char *, int);
static void output_pic_addr_const (FILE *, rtx, int);
static void put_condition_code (enum rtx_code, enum machine_mode,
				int, int, FILE *);
static const char *get_some_local_dynamic_name (void);
static int get_some_local_dynamic_name_1 (rtx *, void *);
static rtx ix86_expand_int_compare (enum rtx_code, rtx, rtx);
static enum rtx_code ix86_prepare_fp_compare_args (enum rtx_code, rtx *,
						   rtx *);
static bool ix86_fixed_condition_code_regs (unsigned int *, unsigned int *);
static enum machine_mode ix86_cc_modes_compatible (enum machine_mode,
						   enum machine_mode);
static rtx get_thread_pointer (int);
static rtx legitimize_tls_address (rtx, enum tls_model, int);
static void get_pc_thunk_name (char [32], unsigned int);
static rtx gen_push (rtx);
static int ix86_flags_dependent (rtx, rtx, enum attr_type);
static int ix86_agi_dependent (rtx, rtx, enum attr_type);
static struct machine_function * ix86_init_machine_status (void);
static int ix86_split_to_parts (rtx, rtx *, enum machine_mode);
static int ix86_nsaved_regs (void);
static void ix86_emit_save_regs (void);
static void ix86_emit_save_regs_using_mov (rtx, HOST_WIDE_INT);
static void ix86_emit_restore_regs_using_mov (rtx, HOST_WIDE_INT, int);
static void ix86_output_function_epilogue (FILE *, HOST_WIDE_INT);
static HOST_WIDE_INT ix86_GOT_alias_set (void);
static void ix86_adjust_counter (rtx, HOST_WIDE_INT);
static rtx ix86_expand_aligntest (rtx, int);
static void ix86_expand_strlensi_unroll_1 (rtx, rtx, rtx);
static int ix86_issue_rate (void);
static int ix86_adjust_cost (rtx, rtx, rtx, int);
static int ia32_multipass_dfa_lookahead (void);
static void ix86_init_mmx_sse_builtins (void);
static rtx x86_this_parameter (tree);
static void x86_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				 HOST_WIDE_INT, tree);
static bool x86_can_output_mi_thunk (tree, HOST_WIDE_INT, HOST_WIDE_INT, tree);
static void x86_file_start (void);
static void ix86_reorg (void);
static bool ix86_expand_carry_flag_compare (enum rtx_code, rtx, rtx, rtx*);
static tree ix86_build_builtin_va_list (void);
static void ix86_setup_incoming_varargs (CUMULATIVE_ARGS *, enum machine_mode,
					 tree, int *, int);
static tree ix86_gimplify_va_arg (tree, tree, tree *, tree *);
static bool ix86_scalar_mode_supported_p (enum machine_mode);
static bool ix86_vector_mode_supported_p (enum machine_mode);

static int ix86_address_cost (rtx);
static bool ix86_cannot_force_const_mem (rtx);
static rtx ix86_delegitimize_address (rtx);

static void i386_output_dwarf_dtprel (FILE *, int, rtx) ATTRIBUTE_UNUSED;

struct builtin_description;
static rtx ix86_expand_sse_comi (const struct builtin_description *,
				 tree, rtx);
static rtx ix86_expand_sse_compare (const struct builtin_description *,
				    tree, rtx);
static rtx ix86_expand_unop1_builtin (enum insn_code, tree, rtx);
static rtx ix86_expand_unop_builtin (enum insn_code, tree, rtx, int);
static rtx ix86_expand_binop_builtin (enum insn_code, tree, rtx);
static rtx ix86_expand_store_builtin (enum insn_code, tree);
static rtx safe_vector_operand (rtx, enum machine_mode);
static rtx ix86_expand_fp_compare (enum rtx_code, rtx, rtx, rtx, rtx *, rtx *);
static int ix86_fp_comparison_arithmetics_cost (enum rtx_code code);
static int ix86_fp_comparison_fcomi_cost (enum rtx_code code);
static int ix86_fp_comparison_sahf_cost (enum rtx_code code);
static int ix86_fp_comparison_cost (enum rtx_code code);
static unsigned int ix86_select_alt_pic_regnum (void);
static int ix86_save_reg (unsigned int, int);
static void ix86_compute_frame_layout (struct ix86_frame *);
static int ix86_comp_type_attributes (tree, tree);
static int ix86_function_regparm (tree, tree);
const struct attribute_spec ix86_attribute_table[];
static bool ix86_function_ok_for_sibcall (tree, tree);
static tree ix86_handle_cconv_attribute (tree *, tree, tree, int, bool *);
static int ix86_value_regno (enum machine_mode, tree, tree);
static bool contains_128bit_aligned_vector_p (tree);
static rtx ix86_struct_value_rtx (tree, int);
static bool ix86_ms_bitfield_layout_p (tree);
static tree ix86_handle_struct_attribute (tree *, tree, tree, int, bool *);
static int extended_reg_mentioned_1 (rtx *, void *);
static bool ix86_rtx_costs (rtx, int, int, int *);
static int min_insn_size (rtx);
static tree ix86_md_asm_clobbers (tree outputs, tree inputs, tree clobbers);
static bool ix86_must_pass_in_stack (enum machine_mode mode, tree type);
static bool ix86_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				    tree, bool);
static void ix86_init_builtins (void);
static rtx ix86_expand_builtin (tree, rtx, rtx, enum machine_mode, int);
static const char *ix86_mangle_fundamental_type (tree);
static tree ix86_stack_protect_fail (void);
static rtx ix86_internal_arg_pointer (void);
static void ix86_dwarf_handle_frame_unspec (const char *, rtx, int);
static void pro_epilogue_adjust_stack (rtx, rtx, rtx, int);

/* This function is only used on Solaris.  */
static void i386_solaris_elf_named_section (const char *, unsigned int, tree)
  ATTRIBUTE_UNUSED;

/* Register class used for passing given 64bit part of the argument.
   These represent classes as documented by the PS ABI, with the exception
   of SSESF, SSEDF classes, that are basically SSE class, just gcc will
   use SF or DFmode move instead of DImode to avoid reformatting penalties.

   Similarly we play games with INTEGERSI_CLASS to use cheaper SImode moves
   whenever possible (upper half does contain padding).
 */
enum x86_64_reg_class
  {
    X86_64_NO_CLASS,
    X86_64_INTEGER_CLASS,
    X86_64_INTEGERSI_CLASS,
    X86_64_SSE_CLASS,
    X86_64_SSESF_CLASS,
    X86_64_SSEDF_CLASS,
    X86_64_SSEUP_CLASS,
    X86_64_X87_CLASS,
    X86_64_X87UP_CLASS,
    X86_64_COMPLEX_X87_CLASS,
    X86_64_MEMORY_CLASS
  };
static const char * const x86_64_reg_class_name[] = {
  "no", "integer", "integerSI", "sse", "sseSF", "sseDF",
  "sseup", "x87", "x87up", "cplx87", "no"
};

#define MAX_CLASSES 4

/* Table of constants used by fldpi, fldln2, etc....  */
static REAL_VALUE_TYPE ext_80387_constants_table [5];
static bool ext_80387_constants_init = 0;
static void init_ext_80387_constants (void);
static bool ix86_in_large_data_p (tree) ATTRIBUTE_UNUSED;
static void ix86_encode_section_info (tree, rtx, int) ATTRIBUTE_UNUSED;
static void x86_64_elf_unique_section (tree decl, int reloc) ATTRIBUTE_UNUSED;
static section *x86_64_elf_select_section (tree decl, int reloc,
					   unsigned HOST_WIDE_INT align)
					     ATTRIBUTE_UNUSED;

/* Initialize the GCC target structure.  */
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE ix86_attribute_table
#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
#  undef TARGET_MERGE_DECL_ATTRIBUTES
#  define TARGET_MERGE_DECL_ATTRIBUTES merge_dllimport_decl_attributes
#endif

#undef TARGET_COMP_TYPE_ATTRIBUTES
#define TARGET_COMP_TYPE_ATTRIBUTES ix86_comp_type_attributes

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS ix86_init_builtins
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN ix86_expand_builtin

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE ix86_output_function_epilogue

#undef TARGET_ENCODE_SECTION_INFO
#ifndef SUBTARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO ix86_encode_section_info
#else
#define TARGET_ENCODE_SECTION_INFO SUBTARGET_ENCODE_SECTION_INFO
#endif

#undef TARGET_ASM_OPEN_PAREN
#define TARGET_ASM_OPEN_PAREN ""
#undef TARGET_ASM_CLOSE_PAREN
#define TARGET_ASM_CLOSE_PAREN ""

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP ASM_SHORT
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP ASM_LONG
#ifdef ASM_QUAD
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP ASM_QUAD
#endif

#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP TARGET_ASM_ALIGNED_HI_OP
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP TARGET_ASM_ALIGNED_SI_OP
#undef TARGET_ASM_UNALIGNED_DI_OP
#define TARGET_ASM_UNALIGNED_DI_OP TARGET_ASM_ALIGNED_DI_OP

#undef TARGET_SCHED_ADJUST_COST
#define TARGET_SCHED_ADJUST_COST ix86_adjust_cost
#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE ix86_issue_rate
#undef TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD
#define TARGET_SCHED_FIRST_CYCLE_MULTIPASS_DFA_LOOKAHEAD \
  ia32_multipass_dfa_lookahead

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL ix86_function_ok_for_sibcall

#ifdef HAVE_AS_TLS
#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS true
#endif
#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM ix86_cannot_force_const_mem
#undef TARGET_USE_BLOCKS_FOR_CONSTANT_P
#define TARGET_USE_BLOCKS_FOR_CONSTANT_P hook_bool_mode_rtx_true

#undef TARGET_DELEGITIMIZE_ADDRESS
#define TARGET_DELEGITIMIZE_ADDRESS ix86_delegitimize_address

#undef TARGET_MS_BITFIELD_LAYOUT_P
#define TARGET_MS_BITFIELD_LAYOUT_P ix86_ms_bitfield_layout_p

#if TARGET_MACHO
#undef TARGET_BINDS_LOCAL_P
#define TARGET_BINDS_LOCAL_P darwin_binds_local_p
#endif

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK x86_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK x86_can_output_mi_thunk

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START x86_file_start

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS	\
  (TARGET_DEFAULT			\
   | TARGET_64BIT_DEFAULT		\
   | TARGET_SUBTARGET_DEFAULT		\
   | TARGET_TLS_DIRECT_SEG_REFS_DEFAULT)

#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION ix86_handle_option

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS ix86_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST ix86_address_cost

#undef TARGET_FIXED_CONDITION_CODE_REGS
#define TARGET_FIXED_CONDITION_CODE_REGS ix86_fixed_condition_code_regs
#undef TARGET_CC_MODES_COMPATIBLE
#define TARGET_CC_MODES_COMPATIBLE ix86_cc_modes_compatible

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG ix86_reorg

#undef TARGET_BUILD_BUILTIN_VA_LIST
#define TARGET_BUILD_BUILTIN_VA_LIST ix86_build_builtin_va_list

#undef TARGET_MD_ASM_CLOBBERS
#define TARGET_MD_ASM_CLOBBERS ix86_md_asm_clobbers

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true
#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX ix86_struct_value_rtx
#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS ix86_setup_incoming_varargs
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK ix86_must_pass_in_stack
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE ix86_pass_by_reference
#undef TARGET_INTERNAL_ARG_POINTER
#define TARGET_INTERNAL_ARG_POINTER ix86_internal_arg_pointer
#undef TARGET_DWARF_HANDLE_FRAME_UNSPEC
#define TARGET_DWARF_HANDLE_FRAME_UNSPEC ix86_dwarf_handle_frame_unspec

#undef TARGET_GIMPLIFY_VA_ARG_EXPR
#define TARGET_GIMPLIFY_VA_ARG_EXPR ix86_gimplify_va_arg

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P ix86_scalar_mode_supported_p

#undef TARGET_VECTOR_MODE_SUPPORTED_P
#define TARGET_VECTOR_MODE_SUPPORTED_P ix86_vector_mode_supported_p

#ifdef HAVE_AS_TLS
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL i386_output_dwarf_dtprel
#endif

#ifdef SUBTARGET_INSERT_ATTRIBUTES
#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES SUBTARGET_INSERT_ATTRIBUTES
#endif

#undef TARGET_MANGLE_FUNDAMENTAL_TYPE
#define TARGET_MANGLE_FUNDAMENTAL_TYPE ix86_mangle_fundamental_type

#undef TARGET_STACK_PROTECT_FAIL
#define TARGET_STACK_PROTECT_FAIL ix86_stack_protect_fail

#undef TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE ix86_function_value

struct gcc_target targetm = TARGET_INITIALIZER;


/* The svr4 ABI for the i386 says that records and unions are returned
   in memory.  */
#ifndef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1
#endif

/* Implement TARGET_HANDLE_OPTION.  */

static bool
ix86_handle_option (size_t code, const char *arg ATTRIBUTE_UNUSED, int value)
{
  switch (code)
    {
    case OPT_m3dnow:
      if (!value)
	{
	  target_flags &= ~MASK_3DNOW_A;
	  target_flags_explicit |= MASK_3DNOW_A;
	}
      return true;

    case OPT_mmmx:
      if (!value)
	{
	  target_flags &= ~(MASK_3DNOW | MASK_3DNOW_A);
	  target_flags_explicit |= MASK_3DNOW | MASK_3DNOW_A;
	}
      return true;

    case OPT_msse:
      if (!value)
	{
	  target_flags &= ~(MASK_SSE2 | MASK_SSE3);
	  target_flags_explicit |= MASK_SSE2 | MASK_SSE3;
	}
      return true;

    case OPT_msse2:
      if (!value)
	{
	  target_flags &= ~MASK_SSE3;
	  target_flags_explicit |= MASK_SSE3;
	}
      return true;

    default:
      return true;
    }
}

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */

void
override_options (void)
{
  int i;
  int ix86_tune_defaulted = 0;

  /* Comes from final.c -- no real reason to change it.  */
#define MAX_CODE_ALIGN 16

  static struct ptt
    {
      const struct processor_costs *cost;	/* Processor costs */
      const int target_enable;			/* Target flags to enable.  */
      const int target_disable;			/* Target flags to disable.  */
      const int align_loop;			/* Default alignments.  */
      const int align_loop_max_skip;
      const int align_jump;
      const int align_jump_max_skip;
      const int align_func;
    }
  const processor_target_table[PROCESSOR_max] =
    {
      {&i386_cost, 0, 0, 4, 3, 4, 3, 4},
      {&i486_cost, 0, 0, 16, 15, 16, 15, 16},
      {&pentium_cost, 0, 0, 16, 7, 16, 7, 16},
      {&pentiumpro_cost, 0, 0, 16, 15, 16, 7, 16},
      {&k6_cost, 0, 0, 32, 7, 32, 7, 32},
      {&athlon_cost, 0, 0, 16, 7, 16, 7, 16},
      {&pentium4_cost, 0, 0, 0, 0, 0, 0, 0},
      {&k8_cost, 0, 0, 16, 7, 16, 7, 16},
      {&nocona_cost, 0, 0, 0, 0, 0, 0, 0},
      {&generic32_cost, 0, 0, 16, 7, 16, 7, 16},
      {&generic64_cost, 0, 0, 16, 7, 16, 7, 16}
    };

  static const char * const cpu_names[] = TARGET_CPU_DEFAULT_NAMES;
  static struct pta
    {
      const char *const name;		/* processor name or nickname.  */
      const enum processor_type processor;
      const enum pta_flags
	{
	  PTA_SSE = 1,
	  PTA_SSE2 = 2,
	  PTA_SSE3 = 4,
	  PTA_MMX = 8,
	  PTA_PREFETCH_SSE = 16,
	  PTA_3DNOW = 32,
	  PTA_3DNOW_A = 64,
	  PTA_64BIT = 128
	} flags;
    }
  const processor_alias_table[] =
    {
      {"i386", PROCESSOR_I386, 0},
      {"i486", PROCESSOR_I486, 0},
      {"i586", PROCESSOR_PENTIUM, 0},
      {"pentium", PROCESSOR_PENTIUM, 0},
      {"pentium-mmx", PROCESSOR_PENTIUM, PTA_MMX},
      {"winchip-c6", PROCESSOR_I486, PTA_MMX},
      {"winchip2", PROCESSOR_I486, PTA_MMX | PTA_3DNOW},
      {"c3", PROCESSOR_I486, PTA_MMX | PTA_3DNOW},
      {"c3-2", PROCESSOR_PENTIUMPRO, PTA_MMX | PTA_PREFETCH_SSE | PTA_SSE},
      {"i686", PROCESSOR_PENTIUMPRO, 0},
      {"pentiumpro", PROCESSOR_PENTIUMPRO, 0},
      {"pentium2", PROCESSOR_PENTIUMPRO, PTA_MMX},
      {"pentium3", PROCESSOR_PENTIUMPRO, PTA_MMX | PTA_SSE | PTA_PREFETCH_SSE},
      {"pentium3m", PROCESSOR_PENTIUMPRO, PTA_MMX | PTA_SSE | PTA_PREFETCH_SSE},
      {"pentium-m", PROCESSOR_PENTIUMPRO, PTA_MMX | PTA_SSE | PTA_PREFETCH_SSE | PTA_SSE2},
      {"pentium4", PROCESSOR_PENTIUM4, PTA_SSE | PTA_SSE2
				       | PTA_MMX | PTA_PREFETCH_SSE},
      {"pentium4m", PROCESSOR_PENTIUM4, PTA_SSE | PTA_SSE2
				        | PTA_MMX | PTA_PREFETCH_SSE},
      {"prescott", PROCESSOR_NOCONA, PTA_SSE | PTA_SSE2 | PTA_SSE3
				        | PTA_MMX | PTA_PREFETCH_SSE},
      {"nocona", PROCESSOR_NOCONA, PTA_SSE | PTA_SSE2 | PTA_SSE3 | PTA_64BIT
				        | PTA_MMX | PTA_PREFETCH_SSE},
      {"k6", PROCESSOR_K6, PTA_MMX},
      {"k6-2", PROCESSOR_K6, PTA_MMX | PTA_3DNOW},
      {"k6-3", PROCESSOR_K6, PTA_MMX | PTA_3DNOW},
      {"athlon", PROCESSOR_ATHLON, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW
				   | PTA_3DNOW_A},
      {"athlon-tbird", PROCESSOR_ATHLON, PTA_MMX | PTA_PREFETCH_SSE
					 | PTA_3DNOW | PTA_3DNOW_A},
      {"athlon-4", PROCESSOR_ATHLON, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW
				    | PTA_3DNOW_A | PTA_SSE},
      {"athlon-xp", PROCESSOR_ATHLON, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW
				      | PTA_3DNOW_A | PTA_SSE},
      {"athlon-mp", PROCESSOR_ATHLON, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW
				      | PTA_3DNOW_A | PTA_SSE},
      {"x86-64", PROCESSOR_K8, PTA_MMX | PTA_PREFETCH_SSE | PTA_64BIT
			       | PTA_SSE | PTA_SSE2 },
      {"k8", PROCESSOR_K8, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW | PTA_64BIT
				      | PTA_3DNOW_A | PTA_SSE | PTA_SSE2},
      {"opteron", PROCESSOR_K8, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW | PTA_64BIT
				      | PTA_3DNOW_A | PTA_SSE | PTA_SSE2},
      {"athlon64", PROCESSOR_K8, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW | PTA_64BIT
				      | PTA_3DNOW_A | PTA_SSE | PTA_SSE2},
      {"athlon-fx", PROCESSOR_K8, PTA_MMX | PTA_PREFETCH_SSE | PTA_3DNOW | PTA_64BIT
				      | PTA_3DNOW_A | PTA_SSE | PTA_SSE2},
      {"generic32", PROCESSOR_GENERIC32, 0 /* flags are only used for -march switch.  */ },
      {"generic64", PROCESSOR_GENERIC64, PTA_64BIT /* flags are only used for -march switch.  */ },
    };

  int const pta_size = ARRAY_SIZE (processor_alias_table);

#ifdef SUBTARGET_OVERRIDE_OPTIONS
  SUBTARGET_OVERRIDE_OPTIONS;
#endif

#ifdef SUBSUBTARGET_OVERRIDE_OPTIONS
  SUBSUBTARGET_OVERRIDE_OPTIONS;
#endif

  /* -fPIC is the default for x86_64.  */
  if (TARGET_MACHO && TARGET_64BIT)
    flag_pic = 2;

  /* Set the default values for switches whose default depends on TARGET_64BIT
     in case they weren't overwritten by command line options.  */
  if (TARGET_64BIT)
    {
      /* Mach-O doesn't support omitting the frame pointer for now.  */
      if (flag_omit_frame_pointer == 2)
	flag_omit_frame_pointer = (TARGET_MACHO ? 0 : 1);
      if (flag_asynchronous_unwind_tables == 2)
	flag_asynchronous_unwind_tables = 1;
      if (flag_pcc_struct_return == 2)
	flag_pcc_struct_return = 0;
    }
  else
    {
      if (flag_omit_frame_pointer == 2)
	flag_omit_frame_pointer = 0;
      if (flag_asynchronous_unwind_tables == 2)
	flag_asynchronous_unwind_tables = 0;
      if (flag_pcc_struct_return == 2)
	flag_pcc_struct_return = DEFAULT_PCC_STRUCT_RETURN;
    }

  /* Need to check -mtune=generic first.  */
  if (ix86_tune_string)
    {
      if (!strcmp (ix86_tune_string, "generic")
	  || !strcmp (ix86_tune_string, "i686")
	  /* As special support for cross compilers we read -mtune=native
	     as -mtune=generic.  With native compilers we won't see the
	     -mtune=native, as it was changed by the driver.  */
	  || !strcmp (ix86_tune_string, "native"))
	{
	  if (TARGET_64BIT)
	    ix86_tune_string = "generic64";
	  else
	    ix86_tune_string = "generic32";
	}
      else if (!strncmp (ix86_tune_string, "generic", 7))
	error ("bad value (%s) for -mtune= switch", ix86_tune_string);
    }
  else
    {
      if (ix86_arch_string)
	ix86_tune_string = ix86_arch_string;
      if (!ix86_tune_string)
	{
	  ix86_tune_string = cpu_names [TARGET_CPU_DEFAULT];
	  ix86_tune_defaulted = 1;
	}

      /* ix86_tune_string is set to ix86_arch_string or defaulted.  We
	 need to use a sensible tune option.  */
      if (!strcmp (ix86_tune_string, "generic")
	  || !strcmp (ix86_tune_string, "x86-64")
	  || !strcmp (ix86_tune_string, "i686"))
	{
	  if (TARGET_64BIT)
	    ix86_tune_string = "generic64";
	  else
	    ix86_tune_string = "generic32";
	}
    }
  if (!strcmp (ix86_tune_string, "x86-64"))
    warning (OPT_Wdeprecated, "-mtune=x86-64 is deprecated.  Use -mtune=k8 or "
	     "-mtune=generic instead as appropriate.");

  if (!ix86_arch_string)
    ix86_arch_string = TARGET_64BIT ? "x86-64" : "i486";
  if (!strcmp (ix86_arch_string, "generic"))
    error ("generic CPU can be used only for -mtune= switch");
  if (!strncmp (ix86_arch_string, "generic", 7))
    error ("bad value (%s) for -march= switch", ix86_arch_string);

  if (ix86_cmodel_string != 0)
    {
      if (!strcmp (ix86_cmodel_string, "small"))
	ix86_cmodel = flag_pic ? CM_SMALL_PIC : CM_SMALL;
      else if (!strcmp (ix86_cmodel_string, "medium"))
	ix86_cmodel = flag_pic ? CM_MEDIUM_PIC : CM_MEDIUM;
      else if (flag_pic)
	sorry ("code model %s not supported in PIC mode", ix86_cmodel_string);
      else if (!strcmp (ix86_cmodel_string, "32"))
	ix86_cmodel = CM_32;
      else if (!strcmp (ix86_cmodel_string, "kernel") && !flag_pic)
	ix86_cmodel = CM_KERNEL;
      else if (!strcmp (ix86_cmodel_string, "large") && !flag_pic)
	ix86_cmodel = CM_LARGE;
      else
	error ("bad value (%s) for -mcmodel= switch", ix86_cmodel_string);
    }
  else
    {
      ix86_cmodel = CM_32;
      if (TARGET_64BIT)
	ix86_cmodel = flag_pic ? CM_SMALL_PIC : CM_SMALL;
    }
  if (ix86_asm_string != 0)
    {
      if (! TARGET_MACHO
	  && !strcmp (ix86_asm_string, "intel"))
	ix86_asm_dialect = ASM_INTEL;
      else if (!strcmp (ix86_asm_string, "att"))
	ix86_asm_dialect = ASM_ATT;
      else
	error ("bad value (%s) for -masm= switch", ix86_asm_string);
    }
  if ((TARGET_64BIT == 0) != (ix86_cmodel == CM_32))
    error ("code model %qs not supported in the %s bit mode",
	   ix86_cmodel_string, TARGET_64BIT ? "64" : "32");
  if (ix86_cmodel == CM_LARGE)
    sorry ("code model %<large%> not supported yet");
  if ((TARGET_64BIT != 0) != ((target_flags & MASK_64BIT) != 0))
    sorry ("%i-bit mode not compiled in",
	   (target_flags & MASK_64BIT) ? 64 : 32);

  for (i = 0; i < pta_size; i++)
    if (! strcmp (ix86_arch_string, processor_alias_table[i].name))
      {
	ix86_arch = processor_alias_table[i].processor;
	/* Default cpu tuning to the architecture.  */
	ix86_tune = ix86_arch;
	if (processor_alias_table[i].flags & PTA_MMX
	    && !(target_flags_explicit & MASK_MMX))
	  target_flags |= MASK_MMX;
	if (processor_alias_table[i].flags & PTA_3DNOW
	    && !(target_flags_explicit & MASK_3DNOW))
	  target_flags |= MASK_3DNOW;
	if (processor_alias_table[i].flags & PTA_3DNOW_A
	    && !(target_flags_explicit & MASK_3DNOW_A))
	  target_flags |= MASK_3DNOW_A;
	if (processor_alias_table[i].flags & PTA_SSE
	    && !(target_flags_explicit & MASK_SSE))
	  target_flags |= MASK_SSE;
	if (processor_alias_table[i].flags & PTA_SSE2
	    && !(target_flags_explicit & MASK_SSE2))
	  target_flags |= MASK_SSE2;
	if (processor_alias_table[i].flags & PTA_SSE3
	    && !(target_flags_explicit & MASK_SSE3))
	  target_flags |= MASK_SSE3;
	if (processor_alias_table[i].flags & PTA_PREFETCH_SSE)
	  x86_prefetch_sse = true;
	if (TARGET_64BIT && !(processor_alias_table[i].flags & PTA_64BIT))
	  error ("CPU you selected does not support x86-64 "
		 "instruction set");
	break;
      }

  if (i == pta_size)
    error ("bad value (%s) for -march= switch", ix86_arch_string);

  for (i = 0; i < pta_size; i++)
    if (! strcmp (ix86_tune_string, processor_alias_table[i].name))
      {
	ix86_tune = processor_alias_table[i].processor;
	if (TARGET_64BIT && !(processor_alias_table[i].flags & PTA_64BIT))
	  {
	    if (ix86_tune_defaulted)
	      {
		ix86_tune_string = "x86-64";
		for (i = 0; i < pta_size; i++)
		  if (! strcmp (ix86_tune_string,
				processor_alias_table[i].name))
		    break;
		ix86_tune = processor_alias_table[i].processor;
	      }
	    else
	      error ("CPU you selected does not support x86-64 "
		     "instruction set");
	  }
        /* Intel CPUs have always interpreted SSE prefetch instructions as
	   NOPs; so, we can enable SSE prefetch instructions even when
	   -mtune (rather than -march) points us to a processor that has them.
	   However, the VIA C3 gives a SIGILL, so we only do that for i686 and
	   higher processors.  */
	if (TARGET_CMOVE && (processor_alias_table[i].flags & PTA_PREFETCH_SSE))
	  x86_prefetch_sse = true;
	break;
      }
  if (i == pta_size)
    error ("bad value (%s) for -mtune= switch", ix86_tune_string);

  if (optimize_size)
    ix86_cost = &size_cost;
  else
    ix86_cost = processor_target_table[ix86_tune].cost;
  target_flags |= processor_target_table[ix86_tune].target_enable;
  target_flags &= ~processor_target_table[ix86_tune].target_disable;

  /* Arrange to set up i386_stack_locals for all functions.  */
  init_machine_status = ix86_init_machine_status;

  /* Validate -mregparm= value.  */
  if (ix86_regparm_string)
    {
      i = atoi (ix86_regparm_string);
      if (i < 0 || i > REGPARM_MAX)
	error ("-mregparm=%d is not between 0 and %d", i, REGPARM_MAX);
      else
	ix86_regparm = i;
    }
  else
   if (TARGET_64BIT)
     ix86_regparm = REGPARM_MAX;

  /* If the user has provided any of the -malign-* options,
     warn and use that value only if -falign-* is not set.
     Remove this code in GCC 3.2 or later.  */
  if (ix86_align_loops_string)
    {
      warning (0, "-malign-loops is obsolete, use -falign-loops");
      if (align_loops == 0)
	{
	  i = atoi (ix86_align_loops_string);
	  if (i < 0 || i > MAX_CODE_ALIGN)
	    error ("-malign-loops=%d is not between 0 and %d", i, MAX_CODE_ALIGN);
	  else
	    align_loops = 1 << i;
	}
    }

  if (ix86_align_jumps_string)
    {
      warning (0, "-malign-jumps is obsolete, use -falign-jumps");
      if (align_jumps == 0)
	{
	  i = atoi (ix86_align_jumps_string);
	  if (i < 0 || i > MAX_CODE_ALIGN)
	    error ("-malign-loops=%d is not between 0 and %d", i, MAX_CODE_ALIGN);
	  else
	    align_jumps = 1 << i;
	}
    }

  if (ix86_align_funcs_string)
    {
      warning (0, "-malign-functions is obsolete, use -falign-functions");
      if (align_functions == 0)
	{
	  i = atoi (ix86_align_funcs_string);
	  if (i < 0 || i > MAX_CODE_ALIGN)
	    error ("-malign-loops=%d is not between 0 and %d", i, MAX_CODE_ALIGN);
	  else
	    align_functions = 1 << i;
	}
    }

  /* Default align_* from the processor table.  */
  if (align_loops == 0)
    {
      align_loops = processor_target_table[ix86_tune].align_loop;
      align_loops_max_skip = processor_target_table[ix86_tune].align_loop_max_skip;
    }
  if (align_jumps == 0)
    {
      align_jumps = processor_target_table[ix86_tune].align_jump;
      align_jumps_max_skip = processor_target_table[ix86_tune].align_jump_max_skip;
    }
  if (align_functions == 0)
    {
      align_functions = processor_target_table[ix86_tune].align_func;
    }

  /* Validate -mbranch-cost= value, or provide default.  */
  ix86_branch_cost = ix86_cost->branch_cost;
  if (ix86_branch_cost_string)
    {
      i = atoi (ix86_branch_cost_string);
      if (i < 0 || i > 5)
	error ("-mbranch-cost=%d is not between 0 and 5", i);
      else
	ix86_branch_cost = i;
    }
  if (ix86_section_threshold_string)
    {
      i = atoi (ix86_section_threshold_string);
      if (i < 0)
	error ("-mlarge-data-threshold=%d is negative", i);
      else
	ix86_section_threshold = i;
    }

  if (ix86_tls_dialect_string)
    {
      if (strcmp (ix86_tls_dialect_string, "gnu") == 0)
	ix86_tls_dialect = TLS_DIALECT_GNU;
      else if (strcmp (ix86_tls_dialect_string, "gnu2") == 0)
	ix86_tls_dialect = TLS_DIALECT_GNU2;
      else if (strcmp (ix86_tls_dialect_string, "sun") == 0)
	ix86_tls_dialect = TLS_DIALECT_SUN;
      else
	error ("bad value (%s) for -mtls-dialect= switch",
	       ix86_tls_dialect_string);
    }

  /* Keep nonleaf frame pointers.  */
  if (flag_omit_frame_pointer)
    target_flags &= ~MASK_OMIT_LEAF_FRAME_POINTER;
  else if (TARGET_OMIT_LEAF_FRAME_POINTER)
    flag_omit_frame_pointer = 1;

  /* If we're doing fast math, we don't care about comparison order
     wrt NaNs.  This lets us use a shorter comparison sequence.  */
  if (flag_finite_math_only)
    target_flags &= ~MASK_IEEE_FP;

  /* If the architecture always has an FPU, turn off NO_FANCY_MATH_387,
     since the insns won't need emulation.  */
  if (x86_arch_always_fancy_math_387 & (1 << ix86_arch))
    target_flags &= ~MASK_NO_FANCY_MATH_387;

  /* Likewise, if the target doesn't have a 387, or we've specified
     software floating point, don't use 387 inline intrinsics.  */
  if (!TARGET_80387)
    target_flags |= MASK_NO_FANCY_MATH_387;

  /* Turn on SSE2 builtins for -msse3.  */
  if (TARGET_SSE3)
    target_flags |= MASK_SSE2;

  /* Turn on SSE builtins for -msse2.  */
  if (TARGET_SSE2)
    target_flags |= MASK_SSE;

  /* Turn on MMX builtins for -msse.  */
  if (TARGET_SSE)
    {
      target_flags |= MASK_MMX & ~target_flags_explicit;
      x86_prefetch_sse = true;
    }

  /* Turn on MMX builtins for 3Dnow.  */
  if (TARGET_3DNOW)
    target_flags |= MASK_MMX;

  if (TARGET_64BIT)
    {
      if (TARGET_ALIGN_DOUBLE)
	error ("-malign-double makes no sense in the 64bit mode");
      if (TARGET_RTD)
	error ("-mrtd calling convention not supported in the 64bit mode");

      /* Enable by default the SSE and MMX builtins.  Do allow the user to
	 explicitly disable any of these.  In particular, disabling SSE and
	 MMX for kernel code is extremely useful.  */
      target_flags
	|= ((MASK_SSE2 | MASK_SSE | MASK_MMX | MASK_128BIT_LONG_DOUBLE)
	    & ~target_flags_explicit);
     }
  else
    {
      if (TARGET_SAVE_ARGS)
        error ("-msave-args makes no sense in the 32-bit mode");
      /* i386 ABI does not specify red zone.  It still makes sense to use it
         when programmer takes care to stack from being destroyed.  */
      if (!(target_flags_explicit & MASK_NO_RED_ZONE))
        target_flags |= MASK_NO_RED_ZONE;
    }

  /* Validate -mpreferred-stack-boundary= value, or provide default.
     The default of 128 bits is for Pentium III's SSE __m128.  We can't
     change it because of optimize_size.  Otherwise, we can't mix object
     files compiled with -Os and -On.  */
  ix86_preferred_stack_boundary = 128;
  if (ix86_preferred_stack_boundary_string)
    {
      i = atoi (ix86_preferred_stack_boundary_string);
      if (i < (TARGET_64BIT ? 4 : 2) || i > 12)
	error ("-mpreferred-stack-boundary=%d is not between %d and 12", i,
	       TARGET_64BIT ? 4 : 2);
      else
	ix86_preferred_stack_boundary = (1 << i) * BITS_PER_UNIT;
    }

  /* Accept -msseregparm only if at least SSE support is enabled.  */
  if (TARGET_SSEREGPARM
      && ! TARGET_SSE)
    error ("-msseregparm used without SSE enabled");

  ix86_fpmath = TARGET_FPMATH_DEFAULT;

  if (ix86_fpmath_string != 0)
    {
      if (! strcmp (ix86_fpmath_string, "387"))
	ix86_fpmath = FPMATH_387;
      else if (! strcmp (ix86_fpmath_string, "sse"))
	{
	  if (!TARGET_SSE)
	    {
	      warning (0, "SSE instruction set disabled, using 387 arithmetics");
	      ix86_fpmath = FPMATH_387;
	    }
	  else
	    ix86_fpmath = FPMATH_SSE;
	}
      else if (! strcmp (ix86_fpmath_string, "387,sse")
	       || ! strcmp (ix86_fpmath_string, "sse,387"))
	{
	  if (!TARGET_SSE)
	    {
	      warning (0, "SSE instruction set disabled, using 387 arithmetics");
	      ix86_fpmath = FPMATH_387;
	    }
	  else if (!TARGET_80387)
	    {
	      warning (0, "387 instruction set disabled, using SSE arithmetics");
	      ix86_fpmath = FPMATH_SSE;
	    }
	  else
	    ix86_fpmath = FPMATH_SSE | FPMATH_387;
	}
      else
	error ("bad value (%s) for -mfpmath= switch", ix86_fpmath_string);
    }

  /* If the i387 is disabled, then do not return values in it. */
  if (!TARGET_80387)
    target_flags &= ~MASK_FLOAT_RETURNS;

  if ((x86_accumulate_outgoing_args & TUNEMASK)
      && !(target_flags_explicit & MASK_ACCUMULATE_OUTGOING_ARGS)
      && !optimize_size)
    target_flags |= MASK_ACCUMULATE_OUTGOING_ARGS;

  /* ??? Unwind info is not correct around the CFG unless either a frame
     pointer is present or M_A_O_A is set.  Fixing this requires rewriting
     unwind info generation to be aware of the CFG and propagating states
     around edges.  */
  if ((flag_unwind_tables || flag_asynchronous_unwind_tables
       || flag_exceptions || flag_non_call_exceptions)
      && flag_omit_frame_pointer
      && !(target_flags & MASK_ACCUMULATE_OUTGOING_ARGS))
    {
      if (target_flags_explicit & MASK_ACCUMULATE_OUTGOING_ARGS)
	warning (0, "unwind tables currently require either a frame pointer "
		 "or -maccumulate-outgoing-args for correctness");
      target_flags |= MASK_ACCUMULATE_OUTGOING_ARGS;
    }

  /* Figure out what ASM_GENERATE_INTERNAL_LABEL builds as a prefix.  */
  {
    char *p;
    ASM_GENERATE_INTERNAL_LABEL (internal_label_prefix, "LX", 0);
    p = strchr (internal_label_prefix, 'X');
    internal_label_prefix_len = p - internal_label_prefix;
    *p = '\0';
  }

  /* When scheduling description is not available, disable scheduler pass
     so it won't slow down the compilation and make x87 code slower.  */
  if (!TARGET_SCHEDULE)
    flag_schedule_insns_after_reload = flag_schedule_insns = 0;
}

/* switch to the appropriate section for output of DECL.
   DECL is either a `VAR_DECL' node or a constant of some sort.
   RELOC indicates whether forming the initial value of DECL requires
   link-time relocations.  */

static section *
x86_64_elf_select_section (tree decl, int reloc,
			   unsigned HOST_WIDE_INT align)
{
  if ((ix86_cmodel == CM_MEDIUM || ix86_cmodel == CM_MEDIUM_PIC)
      && ix86_in_large_data_p (decl))
    {
      const char *sname = NULL;
      unsigned int flags = SECTION_WRITE;
      switch (categorize_decl_for_section (decl, reloc))
	{
	case SECCAT_DATA:
	  sname = ".ldata";
	  break;
	case SECCAT_DATA_REL:
	  sname = ".ldata.rel";
	  break;
	case SECCAT_DATA_REL_LOCAL:
	  sname = ".ldata.rel.local";
	  break;
	case SECCAT_DATA_REL_RO:
	  sname = ".ldata.rel.ro";
	  break;
	case SECCAT_DATA_REL_RO_LOCAL:
	  sname = ".ldata.rel.ro.local";
	  break;
	case SECCAT_BSS:
	  sname = ".lbss";
	  flags |= SECTION_BSS;
	  break;
	case SECCAT_RODATA:
	case SECCAT_RODATA_MERGE_STR:
	case SECCAT_RODATA_MERGE_STR_INIT:
	case SECCAT_RODATA_MERGE_CONST:
	  sname = ".lrodata";
	  flags = 0;
	  break;
	case SECCAT_SRODATA:
	case SECCAT_SDATA:
	case SECCAT_SBSS:
	  gcc_unreachable ();
	case SECCAT_TEXT:
	case SECCAT_TDATA:
	case SECCAT_TBSS:
	  /* We don't split these for medium model.  Place them into
	     default sections and hope for best.  */
	  break;
	}
      if (sname)
	{
	  /* We might get called with string constants, but get_named_section
	     doesn't like them as they are not DECLs.  Also, we need to set
	     flags in that case.  */
	  if (!DECL_P (decl))
	    return get_section (sname, flags, NULL);
	  return get_named_section (decl, sname, reloc);
	}
    }
  return default_elf_select_section (decl, reloc, align);
}

/* Build up a unique section name, expressed as a
   STRING_CST node, and assign it to DECL_SECTION_NAME (decl).
   RELOC indicates whether the initial value of EXP requires
   link-time relocations.  */

static void
x86_64_elf_unique_section (tree decl, int reloc)
{
  if ((ix86_cmodel == CM_MEDIUM || ix86_cmodel == CM_MEDIUM_PIC)
      && ix86_in_large_data_p (decl))
    {
      const char *prefix = NULL;
      /* We only need to use .gnu.linkonce if we don't have COMDAT groups.  */
      bool one_only = DECL_ONE_ONLY (decl) && !HAVE_COMDAT_GROUP;

      switch (categorize_decl_for_section (decl, reloc))
	{
	case SECCAT_DATA:
	case SECCAT_DATA_REL:
	case SECCAT_DATA_REL_LOCAL:
	case SECCAT_DATA_REL_RO:
	case SECCAT_DATA_REL_RO_LOCAL:
          prefix = one_only ? ".gnu.linkonce.ld." : ".ldata.";
	  break;
	case SECCAT_BSS:
          prefix = one_only ? ".gnu.linkonce.lb." : ".lbss.";
	  break;
	case SECCAT_RODATA:
	case SECCAT_RODATA_MERGE_STR:
	case SECCAT_RODATA_MERGE_STR_INIT:
	case SECCAT_RODATA_MERGE_CONST:
          prefix = one_only ? ".gnu.linkonce.lr." : ".lrodata.";
	  break;
	case SECCAT_SRODATA:
	case SECCAT_SDATA:
	case SECCAT_SBSS:
	  gcc_unreachable ();
	case SECCAT_TEXT:
	case SECCAT_TDATA:
	case SECCAT_TBSS:
	  /* We don't split these for medium model.  Place them into
	     default sections and hope for best.  */
	  break;
	}
      if (prefix)
	{
	  const char *name;
	  size_t nlen, plen;
	  char *string;
	  plen = strlen (prefix);

	  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
	  name = targetm.strip_name_encoding (name);
	  nlen = strlen (name);

	  string = alloca (nlen + plen + 1);
	  memcpy (string, prefix, plen);
	  memcpy (string + plen, name, nlen + 1);

	  DECL_SECTION_NAME (decl) = build_string (nlen + plen, string);
	  return;
	}
    }
  default_unique_section (decl, reloc);
}

#ifdef COMMON_ASM_OP
/* This says how to output assembler code to declare an
   uninitialized external linkage data object.

   For medium model x86-64 we need to use .largecomm opcode for
   large objects.  */
void
x86_elf_aligned_common (FILE *file,
			const char *name, unsigned HOST_WIDE_INT size,
			int align)
{
  if ((ix86_cmodel == CM_MEDIUM || ix86_cmodel == CM_MEDIUM_PIC)
      && size > (unsigned int)ix86_section_threshold)
    fprintf (file, ".largecomm\t");
  else
    fprintf (file, "%s", COMMON_ASM_OP);
  assemble_name (file, name);
  fprintf (file, ","HOST_WIDE_INT_PRINT_UNSIGNED",%u\n",
	   size, align / BITS_PER_UNIT);
}

/* Utility function for targets to use in implementing
   ASM_OUTPUT_ALIGNED_BSS.  */

void
x86_output_aligned_bss (FILE *file, tree decl ATTRIBUTE_UNUSED,
			const char *name, unsigned HOST_WIDE_INT size,
			int align)
{
  if ((ix86_cmodel == CM_MEDIUM || ix86_cmodel == CM_MEDIUM_PIC)
      && size > (unsigned int)ix86_section_threshold)
    switch_to_section (get_named_section (decl, ".lbss", 0));
  else
    switch_to_section (bss_section);
  ASM_OUTPUT_ALIGN (file, floor_log2 (align / BITS_PER_UNIT));
#ifdef ASM_DECLARE_OBJECT_NAME
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (file, name, decl);
#else
  /* Standard thing is just output label for the object.  */
  ASM_OUTPUT_LABEL (file, name);
#endif /* ASM_DECLARE_OBJECT_NAME */
  ASM_OUTPUT_SKIP (file, size ? size : 1);
}
#endif

void
optimization_options (int level, int size ATTRIBUTE_UNUSED)
{
  /* For -O2 and beyond, turn off -fschedule-insns by default.  It tends to
     make the problem with not enough registers even worse.  */
#ifdef INSN_SCHEDULING
  if (level > 1)
    flag_schedule_insns = 0;
#endif

  if (TARGET_MACHO)
    /* The Darwin libraries never set errno, so we might as well
       avoid calling them when that's the only reason we would.  */
    flag_errno_math = 0;

  /* The default values of these switches depend on the TARGET_64BIT
     that is not known at this moment.  Mark these values with 2 and
     let user the to override these.  In case there is no command line option
     specifying them, we will set the defaults in override_options.  */
  if (optimize >= 1)
    flag_omit_frame_pointer = 2;
  flag_pcc_struct_return = 2;
  flag_asynchronous_unwind_tables = 2;
#ifdef SUBTARGET_OPTIMIZATION_OPTIONS
  SUBTARGET_OPTIMIZATION_OPTIONS;
#endif
}

/* Table of valid machine attributes.  */
const struct attribute_spec ix86_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  /* Stdcall attribute says callee is responsible for popping arguments
     if they are not variable.  */
  { "stdcall",   0, 0, false, true,  true,  ix86_handle_cconv_attribute },
  /* Fastcall attribute says callee is responsible for popping arguments
     if they are not variable.  */
  { "fastcall",  0, 0, false, true,  true,  ix86_handle_cconv_attribute },
  /* Cdecl attribute says the callee is a normal C declaration */
  { "cdecl",     0, 0, false, true,  true,  ix86_handle_cconv_attribute },
  /* Regparm attribute specifies how many integer arguments are to be
     passed in registers.  */
  { "regparm",   1, 1, false, true,  true,  ix86_handle_cconv_attribute },
  /* Sseregparm attribute says we are using x86_64 calling conventions
     for FP arguments.  */
  { "sseregparm", 0, 0, false, true, true, ix86_handle_cconv_attribute },
  /* force_align_arg_pointer says this function realigns the stack at entry.  */
  { (const char *)&ix86_force_align_arg_pointer_string, 0, 0,
    false, true,  true, ix86_handle_cconv_attribute },
#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
  { "dllimport", 0, 0, false, false, false, handle_dll_attribute },
  { "dllexport", 0, 0, false, false, false, handle_dll_attribute },
  { "shared",    0, 0, true,  false, false, ix86_handle_shared_attribute },
#endif
  { "ms_struct", 0, 0, false, false,  false, ix86_handle_struct_attribute },
  { "gcc_struct", 0, 0, false, false,  false, ix86_handle_struct_attribute },
#ifdef SUBTARGET_ATTRIBUTE_TABLE
  SUBTARGET_ATTRIBUTE_TABLE,
#endif
  { NULL,        0, 0, false, false, false, NULL }
};

/* Decide whether we can make a sibling call to a function.  DECL is the
   declaration of the function being targeted by the call and EXP is the
   CALL_EXPR representing the call.  */

static bool
ix86_function_ok_for_sibcall (tree decl, tree exp)
{
  tree func;
  rtx a, b;

  /* If we are generating position-independent code, we cannot sibcall
     optimize any indirect call, or a direct call to a global function,
     as the PLT requires %ebx be live.  */
  if (!TARGET_64BIT && flag_pic && (!decl || !targetm.binds_local_p (decl)))
    return false;

  if (decl)
    func = decl;
  else
    {
      func = TREE_TYPE (TREE_OPERAND (exp, 0));
      if (POINTER_TYPE_P (func))
        func = TREE_TYPE (func);
    }

  /* Check that the return value locations are the same.  Like
     if we are returning floats on the 80387 register stack, we cannot
     make a sibcall from a function that doesn't return a float to a
     function that does or, conversely, from a function that does return
     a float to a function that doesn't; the necessary stack adjustment
     would not be executed.  This is also the place we notice
     differences in the return value ABI.  Note that it is ok for one
     of the functions to have void return type as long as the return
     value of the other is passed in a register.  */
  a = ix86_function_value (TREE_TYPE (exp), func, false);
  b = ix86_function_value (TREE_TYPE (DECL_RESULT (cfun->decl)),
			   cfun->decl, false);
  if (STACK_REG_P (a) || STACK_REG_P (b))
    {
      if (!rtx_equal_p (a, b))
	return false;
    }
  else if (VOID_TYPE_P (TREE_TYPE (DECL_RESULT (cfun->decl))))
    ;
  else if (!rtx_equal_p (a, b))
    return false;

  /* If this call is indirect, we'll need to be able to use a call-clobbered
     register for the address of the target function.  Make sure that all
     such registers are not used for passing parameters.  */
  if (!decl && !TARGET_64BIT)
    {
      tree type;

      /* We're looking at the CALL_EXPR, we need the type of the function.  */
      type = TREE_OPERAND (exp, 0);		/* pointer expression */
      type = TREE_TYPE (type);			/* pointer type */
      type = TREE_TYPE (type);			/* function type */

      if (ix86_function_regparm (type, NULL) >= 3)
	{
	  /* ??? Need to count the actual number of registers to be used,
	     not the possible number of registers.  Fix later.  */
	  return false;
	}
    }

#if TARGET_DLLIMPORT_DECL_ATTRIBUTES
  /* Dllimport'd functions are also called indirectly.  */
  if (decl && DECL_DLLIMPORT_P (decl)
      && ix86_function_regparm (TREE_TYPE (decl), NULL) >= 3)
    return false;
#endif

  /* If we forced aligned the stack, then sibcalling would unalign the
     stack, which may break the called function.  */
  if (cfun->machine->force_align_arg_pointer)
    return false;

  /* Otherwise okay.  That also includes certain types of indirect calls.  */
  return true;
}

/* Handle "cdecl", "stdcall", "fastcall", "regparm" and "sseregparm"
   calling convention attributes;
   arguments as in struct attribute_spec.handler.  */

static tree
ix86_handle_cconv_attribute (tree *node, tree name,
				   tree args,
				   int flags ATTRIBUTE_UNUSED,
				   bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_TYPE
      && TREE_CODE (*node) != METHOD_TYPE
      && TREE_CODE (*node) != FIELD_DECL
      && TREE_CODE (*node) != TYPE_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* Can combine regparm with all attributes but fastcall.  */
  if (is_attribute_p ("regparm", name))
    {
      tree cst;

      if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (*node)))
        {
	  error ("fastcall and regparm attributes are not compatible");
	}

      cst = TREE_VALUE (args);
      if (TREE_CODE (cst) != INTEGER_CST)
	{
	  warning (OPT_Wattributes,
		   "%qs attribute requires an integer constant argument",
		   IDENTIFIER_POINTER (name));
	  *no_add_attrs = true;
	}
      else if (compare_tree_int (cst, REGPARM_MAX) > 0)
	{
	  warning (OPT_Wattributes, "argument to %qs attribute larger than %d",
		   IDENTIFIER_POINTER (name), REGPARM_MAX);
	  *no_add_attrs = true;
	}

      if (!TARGET_64BIT
	  && lookup_attribute (ix86_force_align_arg_pointer_string,
			       TYPE_ATTRIBUTES (*node))
	  && compare_tree_int (cst, REGPARM_MAX-1))
	{
	  error ("%s functions limited to %d register parameters",
		 ix86_force_align_arg_pointer_string, REGPARM_MAX-1);
	}

      return NULL_TREE;
    }

  if (TARGET_64BIT)
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* Can combine fastcall with stdcall (redundant) and sseregparm.  */
  if (is_attribute_p ("fastcall", name))
    {
      if (lookup_attribute ("cdecl", TYPE_ATTRIBUTES (*node)))
        {
	  error ("fastcall and cdecl attributes are not compatible");
	}
      if (lookup_attribute ("stdcall", TYPE_ATTRIBUTES (*node)))
        {
	  error ("fastcall and stdcall attributes are not compatible");
	}
      if (lookup_attribute ("regparm", TYPE_ATTRIBUTES (*node)))
        {
	  error ("fastcall and regparm attributes are not compatible");
	}
    }

  /* Can combine stdcall with fastcall (redundant), regparm and
     sseregparm.  */
  else if (is_attribute_p ("stdcall", name))
    {
      if (lookup_attribute ("cdecl", TYPE_ATTRIBUTES (*node)))
        {
	  error ("stdcall and cdecl attributes are not compatible");
	}
      if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (*node)))
        {
	  error ("stdcall and fastcall attributes are not compatible");
	}
    }

  /* Can combine cdecl with regparm and sseregparm.  */
  else if (is_attribute_p ("cdecl", name))
    {
      if (lookup_attribute ("stdcall", TYPE_ATTRIBUTES (*node)))
        {
	  error ("stdcall and cdecl attributes are not compatible");
	}
      if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (*node)))
        {
	  error ("fastcall and cdecl attributes are not compatible");
	}
    }

  /* Can combine sseregparm with all attributes.  */

  return NULL_TREE;
}

/* Return 0 if the attributes for two types are incompatible, 1 if they
   are compatible, and 2 if they are nearly compatible (which causes a
   warning to be generated).  */

static int
ix86_comp_type_attributes (tree type1, tree type2)
{
  /* Check for mismatch of non-default calling convention.  */
  const char *const rtdstr = TARGET_RTD ? "cdecl" : "stdcall";

  if (TREE_CODE (type1) != FUNCTION_TYPE)
    return 1;

  /* Check for mismatched fastcall/regparm types.  */
  if ((!lookup_attribute ("fastcall", TYPE_ATTRIBUTES (type1))
       != !lookup_attribute ("fastcall", TYPE_ATTRIBUTES (type2)))
      || (ix86_function_regparm (type1, NULL)
	  != ix86_function_regparm (type2, NULL)))
    return 0;

  /* Check for mismatched sseregparm types.  */
  if (!lookup_attribute ("sseregparm", TYPE_ATTRIBUTES (type1))
      != !lookup_attribute ("sseregparm", TYPE_ATTRIBUTES (type2)))
    return 0;

  /* Check for mismatched return types (cdecl vs stdcall).  */
  if (!lookup_attribute (rtdstr, TYPE_ATTRIBUTES (type1))
      != !lookup_attribute (rtdstr, TYPE_ATTRIBUTES (type2)))
    return 0;

  return 1;
}

/* Return the regparm value for a function with the indicated TYPE and DECL.
   DECL may be NULL when calling function indirectly
   or considering a libcall.  */

static int
ix86_function_regparm (tree type, tree decl)
{
  tree attr;
  int regparm = ix86_regparm;
  bool user_convention = false;

  if (!TARGET_64BIT)
    {
      attr = lookup_attribute ("regparm", TYPE_ATTRIBUTES (type));
      if (attr)
	{
	  regparm = TREE_INT_CST_LOW (TREE_VALUE (TREE_VALUE (attr)));
	  user_convention = true;
	}

      if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (type)))
	{
	  regparm = 2;
	  user_convention = true;
	}

      /* Use register calling convention for local functions when possible.  */
      if (!TARGET_64BIT && !user_convention && decl
	  && flag_unit_at_a_time && !profile_flag)
	{
	  struct cgraph_local_info *i = cgraph_local_info (decl);
	  if (i && i->local)
	    {
	      int local_regparm, globals = 0, regno;

	      /* Make sure no regparm register is taken by a global register
		 variable.  */
	      for (local_regparm = 0; local_regparm < 3; local_regparm++)
		if (global_regs[local_regparm])
		  break;
	      /* We can't use regparm(3) for nested functions as these use
		 static chain pointer in third argument.  */
	      if (local_regparm == 3
		  && decl_function_context (decl)
		  && !DECL_NO_STATIC_CHAIN (decl))
		local_regparm = 2;
	      /* If the function realigns its stackpointer, the
		 prologue will clobber %ecx.  If we've already
		 generated code for the callee, the callee
		 DECL_STRUCT_FUNCTION is gone, so we fall back to
		 scanning the attributes for the self-realigning
		 property.  */
	      if ((DECL_STRUCT_FUNCTION (decl)
		   && DECL_STRUCT_FUNCTION (decl)->machine->force_align_arg_pointer)
		  || (!DECL_STRUCT_FUNCTION (decl)
		      && lookup_attribute (ix86_force_align_arg_pointer_string,
					   TYPE_ATTRIBUTES (TREE_TYPE (decl)))))
		local_regparm = 2;
	      /* Each global register variable increases register preassure,
		 so the more global reg vars there are, the smaller regparm
		 optimization use, unless requested by the user explicitly.  */
	      for (regno = 0; regno < 6; regno++)
		if (global_regs[regno])
		  globals++;
	      local_regparm
		= globals < local_regparm ? local_regparm - globals : 0;

	      if (local_regparm > regparm)
		regparm = local_regparm;
	    }
	}
    }
  return regparm;
}

/* Return 1 or 2, if we can pass up to SSE_REGPARM_MAX SFmode (1) and
   DFmode (2) arguments in SSE registers for a function with the
   indicated TYPE and DECL.  DECL may be NULL when calling function
   indirectly or considering a libcall.  Otherwise return 0.  */

static int
ix86_function_sseregparm (tree type, tree decl)
{
  /* Use SSE registers to pass SFmode and DFmode arguments if requested
     by the sseregparm attribute.  */
  if (TARGET_SSEREGPARM
      || (type
	  && lookup_attribute ("sseregparm", TYPE_ATTRIBUTES (type))))
    {
      if (!TARGET_SSE)
	{
	  if (decl)
	    error ("Calling %qD with attribute sseregparm without "
		   "SSE/SSE2 enabled", decl);
	  else
	    error ("Calling %qT with attribute sseregparm without "
		   "SSE/SSE2 enabled", type);
	  return 0;
	}

      return 2;
    }

  /* For local functions, pass up to SSE_REGPARM_MAX SFmode
     (and DFmode for SSE2) arguments in SSE registers,
     even for 32-bit targets.  */
  if (!TARGET_64BIT && decl
      && TARGET_SSE_MATH && flag_unit_at_a_time && !profile_flag)
    {
      struct cgraph_local_info *i = cgraph_local_info (decl);
      if (i && i->local)
	return TARGET_SSE2 ? 2 : 1;
    }

  return 0;
}

/* Return true if EAX is live at the start of the function.  Used by
   ix86_expand_prologue to determine if we need special help before
   calling allocate_stack_worker.  */

static bool
ix86_eax_live_at_start_p (void)
{
  /* Cheat.  Don't bother working forward from ix86_function_regparm
     to the function type to whether an actual argument is located in
     eax.  Instead just look at cfg info, which is still close enough
     to correct at this point.  This gives false positives for broken
     functions that might use uninitialized data that happens to be
     allocated in eax, but who cares?  */
  return REGNO_REG_SET_P (ENTRY_BLOCK_PTR->il.rtl->global_live_at_end, 0);
}

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the 80386, the RTD insn may be used to pop them if the number
     of args is fixed, but if the number is variable then the caller
     must pop them all.  RTD can't be used for library calls now
     because the library is compiled with the Unix compiler.
   Use of RTD is a selectable option, since it is incompatible with
   standard Unix calling sequences.  If the option is not selected,
   the caller must always pop the args.

   The attribute stdcall is equivalent to RTD on a per module basis.  */

int
ix86_return_pops_args (tree fundecl, tree funtype, int size)
{
  int rtd = TARGET_RTD && (!fundecl || TREE_CODE (fundecl) != IDENTIFIER_NODE);

  /* Cdecl functions override -mrtd, and never pop the stack.  */
  if (! lookup_attribute ("cdecl", TYPE_ATTRIBUTES (funtype))) {

    /* Stdcall and fastcall functions will pop the stack if not
       variable args.  */
    if (lookup_attribute ("stdcall", TYPE_ATTRIBUTES (funtype))
        || lookup_attribute ("fastcall", TYPE_ATTRIBUTES (funtype)))
      rtd = 1;

    if (rtd
        && (TYPE_ARG_TYPES (funtype) == NULL_TREE
	    || (TREE_VALUE (tree_last (TYPE_ARG_TYPES (funtype)))
		== void_type_node)))
      return size;
  }

  /* Lose any fake structure return argument if it is passed on the stack.  */
  if (aggregate_value_p (TREE_TYPE (funtype), fundecl)
      && !TARGET_64BIT
      && !KEEP_AGGREGATE_RETURN_POINTER)
    {
      int nregs = ix86_function_regparm (funtype, fundecl);

      if (!nregs)
	return GET_MODE_SIZE (Pmode);
    }

  return 0;
}

/* Argument support functions.  */

/* Return true when register may be used to pass function parameters.  */
bool
ix86_function_arg_regno_p (int regno)
{
  int i;
  if (!TARGET_64BIT)
    {
      if (TARGET_MACHO)
        return (regno < REGPARM_MAX
                || (TARGET_SSE && SSE_REGNO_P (regno) && !fixed_regs[regno]));
      else
        return (regno < REGPARM_MAX
	        || (TARGET_MMX && MMX_REGNO_P (regno)
	  	    && (regno < FIRST_MMX_REG + MMX_REGPARM_MAX))
	        || (TARGET_SSE && SSE_REGNO_P (regno)
		    && (regno < FIRST_SSE_REG + SSE_REGPARM_MAX)));
    }

  if (TARGET_MACHO)
    {
      if (SSE_REGNO_P (regno) && TARGET_SSE)
        return true;
    }
  else
    {
      if (TARGET_SSE && SSE_REGNO_P (regno)
          && (regno < FIRST_SSE_REG + SSE_REGPARM_MAX))
        return true;
    }
  /* RAX is used as hidden argument to va_arg functions.  */
  if (!regno)
    return true;
  for (i = 0; i < REGPARM_MAX; i++)
    if (regno == x86_64_int_parameter_registers[i])
      return true;
  return false;
}

/* Return if we do not know how to pass TYPE solely in registers.  */

static bool
ix86_must_pass_in_stack (enum machine_mode mode, tree type)
{
  if (must_pass_in_stack_var_size_or_pad (mode, type))
    return true;

  /* For 32-bit, we want TImode aggregates to go on the stack.  But watch out!
     The layout_type routine is crafty and tries to trick us into passing
     currently unsupported vector types on the stack by using TImode.  */
  return (!TARGET_64BIT && mode == TImode
	  && type && TREE_CODE (type) != VECTOR_TYPE);
}

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

void
init_cumulative_args (CUMULATIVE_ARGS *cum,  /* Argument info to initialize */
		      tree fntype,	/* tree ptr for function decl */
		      rtx libname,	/* SYMBOL_REF of library name or 0 */
		      tree fndecl)
{
  static CUMULATIVE_ARGS zero_cum;
  tree param, next_param;

  if (TARGET_DEBUG_ARG)
    {
      fprintf (stderr, "\ninit_cumulative_args (");
      if (fntype)
	fprintf (stderr, "fntype code = %s, ret code = %s",
		 tree_code_name[(int) TREE_CODE (fntype)],
		 tree_code_name[(int) TREE_CODE (TREE_TYPE (fntype))]);
      else
	fprintf (stderr, "no fntype");

      if (libname)
	fprintf (stderr, ", libname = %s", XSTR (libname, 0));
    }

  *cum = zero_cum;

  /* Set up the number of registers to use for passing arguments.  */
  cum->nregs = ix86_regparm;
  if (TARGET_SSE)
    cum->sse_nregs = SSE_REGPARM_MAX;
  if (TARGET_MMX)
    cum->mmx_nregs = MMX_REGPARM_MAX;
  cum->warn_sse = true;
  cum->warn_mmx = true;
  cum->maybe_vaarg = false;

  /* Use ecx and edx registers if function has fastcall attribute,
     else look for regparm information.  */
  if (fntype && !TARGET_64BIT)
    {
      if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (fntype)))
	{
	  cum->nregs = 2;
	  cum->fastcall = 1;
	}
      else
	cum->nregs = ix86_function_regparm (fntype, fndecl);
    }

  /* Set up the number of SSE registers used for passing SFmode
     and DFmode arguments.  Warn for mismatching ABI.  */
  cum->float_in_sse = ix86_function_sseregparm (fntype, fndecl);

  /* Determine if this function has variable arguments.  This is
     indicated by the last argument being 'void_type_mode' if there
     are no variable arguments.  If there are variable arguments, then
     we won't pass anything in registers in 32-bit mode. */

  if (cum->nregs || cum->mmx_nregs || cum->sse_nregs)
    {
      for (param = (fntype) ? TYPE_ARG_TYPES (fntype) : 0;
	   param != 0; param = next_param)
	{
	  next_param = TREE_CHAIN (param);
	  if (next_param == 0 && TREE_VALUE (param) != void_type_node)
	    {
	      if (!TARGET_64BIT)
		{
		  cum->nregs = 0;
		  cum->sse_nregs = 0;
		  cum->mmx_nregs = 0;
		  cum->warn_sse = 0;
		  cum->warn_mmx = 0;
		  cum->fastcall = 0;
		  cum->float_in_sse = 0;
		}
	      cum->maybe_vaarg = true;
	    }
	}
    }
  if ((!fntype && !libname)
      || (fntype && !TYPE_ARG_TYPES (fntype)))
    cum->maybe_vaarg = true;

  if (TARGET_DEBUG_ARG)
    fprintf (stderr, ", nregs=%d )\n", cum->nregs);

  return;
}

/* Return the "natural" mode for TYPE.  In most cases, this is just TYPE_MODE.
   But in the case of vector types, it is some vector mode.

   When we have only some of our vector isa extensions enabled, then there
   are some modes for which vector_mode_supported_p is false.  For these
   modes, the generic vector support in gcc will choose some non-vector mode
   in order to implement the type.  By computing the natural mode, we'll
   select the proper ABI location for the operand and not depend on whatever
   the middle-end decides to do with these vector types.  */

static enum machine_mode
type_natural_mode (tree type)
{
  enum machine_mode mode = TYPE_MODE (type);

  if (TREE_CODE (type) == VECTOR_TYPE && !VECTOR_MODE_P (mode))
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      if ((size == 8 || size == 16)
	  /* ??? Generic code allows us to create width 1 vectors.  Ignore.  */
	  && TYPE_VECTOR_SUBPARTS (type) > 1)
	{
	  enum machine_mode innermode = TYPE_MODE (TREE_TYPE (type));

	  if (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE)
	    mode = MIN_MODE_VECTOR_FLOAT;
	  else
	    mode = MIN_MODE_VECTOR_INT;

	  /* Get the mode which has this inner mode and number of units.  */
	  for (; mode != VOIDmode; mode = GET_MODE_WIDER_MODE (mode))
	    if (GET_MODE_NUNITS (mode) == TYPE_VECTOR_SUBPARTS (type)
		&& GET_MODE_INNER (mode) == innermode)
	      return mode;

	  gcc_unreachable ();
	}
    }

  return mode;
}

/* We want to pass a value in REGNO whose "natural" mode is MODE.  However,
   this may not agree with the mode that the type system has chosen for the
   register, which is ORIG_MODE.  If ORIG_MODE is not BLKmode, then we can
   go ahead and use it.  Otherwise we have to build a PARALLEL instead.  */

static rtx
gen_reg_or_parallel (enum machine_mode mode, enum machine_mode orig_mode,
		     unsigned int regno)
{
  rtx tmp;

  if (orig_mode != BLKmode)
    tmp = gen_rtx_REG (orig_mode, regno);
  else
    {
      tmp = gen_rtx_REG (mode, regno);
      tmp = gen_rtx_EXPR_LIST (VOIDmode, tmp, const0_rtx);
      tmp = gen_rtx_PARALLEL (orig_mode, gen_rtvec (1, tmp));
    }

  return tmp;
}

/* x86-64 register passing implementation.  See x86-64 ABI for details.  Goal
   of this code is to classify each 8bytes of incoming argument by the register
   class and assign registers accordingly.  */

/* Return the union class of CLASS1 and CLASS2.
   See the x86-64 PS ABI for details.  */

static enum x86_64_reg_class
merge_classes (enum x86_64_reg_class class1, enum x86_64_reg_class class2)
{
  /* Rule #1: If both classes are equal, this is the resulting class.  */
  if (class1 == class2)
    return class1;

  /* Rule #2: If one of the classes is NO_CLASS, the resulting class is
     the other class.  */
  if (class1 == X86_64_NO_CLASS)
    return class2;
  if (class2 == X86_64_NO_CLASS)
    return class1;

  /* Rule #3: If one of the classes is MEMORY, the result is MEMORY.  */
  if (class1 == X86_64_MEMORY_CLASS || class2 == X86_64_MEMORY_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #4: If one of the classes is INTEGER, the result is INTEGER.  */
  if ((class1 == X86_64_INTEGERSI_CLASS && class2 == X86_64_SSESF_CLASS)
      || (class2 == X86_64_INTEGERSI_CLASS && class1 == X86_64_SSESF_CLASS))
    return X86_64_INTEGERSI_CLASS;
  if (class1 == X86_64_INTEGER_CLASS || class1 == X86_64_INTEGERSI_CLASS
      || class2 == X86_64_INTEGER_CLASS || class2 == X86_64_INTEGERSI_CLASS)
    return X86_64_INTEGER_CLASS;

  /* Rule #5: If one of the classes is X87, X87UP, or COMPLEX_X87 class,
     MEMORY is used.  */
  if (class1 == X86_64_X87_CLASS
      || class1 == X86_64_X87UP_CLASS
      || class1 == X86_64_COMPLEX_X87_CLASS
      || class2 == X86_64_X87_CLASS
      || class2 == X86_64_X87UP_CLASS
      || class2 == X86_64_COMPLEX_X87_CLASS)
    return X86_64_MEMORY_CLASS;

  /* Rule #6: Otherwise class SSE is used.  */
  return X86_64_SSE_CLASS;
}

/* Classify the argument of type TYPE and mode MODE.
   CLASSES will be filled by the register class used to pass each word
   of the operand.  The number of words is returned.  In case the parameter
   should be passed in memory, 0 is returned. As a special case for zero
   sized containers, classes[0] will be NO_CLASS and 1 is returned.

   BIT_OFFSET is used internally for handling records and specifies offset
   of the offset in bits modulo 256 to avoid overflow cases.

   See the x86-64 PS ABI for details.
*/

static int
classify_argument (enum machine_mode mode, tree type,
		   enum x86_64_reg_class classes[MAX_CLASSES], int bit_offset)
{
  HOST_WIDE_INT bytes =
    (mode == BLKmode) ? int_size_in_bytes (type) : (int) GET_MODE_SIZE (mode);
  int words = (bytes + (bit_offset % 64) / 8 + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  /* Variable sized entities are always passed/returned in memory.  */
  if (bytes < 0)
    return 0;

  if (mode != VOIDmode
      && targetm.calls.must_pass_in_stack (mode, type))
    return 0;

  if (type && AGGREGATE_TYPE_P (type))
    {
      int i;
      tree field;
      enum x86_64_reg_class subclasses[MAX_CLASSES];

      /* On x86-64 we pass structures larger than 16 bytes on the stack.  */
      if (bytes > 16)
	return 0;

      for (i = 0; i < words; i++)
	classes[i] = X86_64_NO_CLASS;

      /* Zero sized arrays or structures are NO_CLASS.  We return 0 to
	 signalize memory class, so handle it as special case.  */
      if (!words)
	{
	  classes[0] = X86_64_NO_CLASS;
	  return 1;
	}

      /* Classify each field of record and merge classes.  */
      switch (TREE_CODE (type))
	{
	case RECORD_TYPE:
	  /* For classes first merge in the field of the subclasses.  */
	  if (TYPE_BINFO (type))
	    {
	      tree binfo, base_binfo;
	      int basenum;

	      for (binfo = TYPE_BINFO (type), basenum = 0;
		   BINFO_BASE_ITERATE (binfo, basenum, base_binfo); basenum++)
		{
		   int num;
		   int offset = tree_low_cst (BINFO_OFFSET (base_binfo), 0) * 8;
		   tree type = BINFO_TYPE (base_binfo);

		   num = classify_argument (TYPE_MODE (type),
					    type, subclasses,
					    (offset + bit_offset) % 256);
		   if (!num)
		     return 0;
		   for (i = 0; i < num; i++)
		     {
		       int pos = (offset + (bit_offset % 64)) / 8 / 8;
		       classes[i + pos] =
			 merge_classes (subclasses[i], classes[i + pos]);
		     }
		}
	    }
	  /* And now merge the fields of structure.  */
	  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	    {
	      if (TREE_CODE (field) == FIELD_DECL)
		{
		  int num;

		  if (TREE_TYPE (field) == error_mark_node)
		    continue;

		  /* Bitfields are always classified as integer.  Handle them
		     early, since later code would consider them to be
		     misaligned integers.  */
		  if (DECL_BIT_FIELD (field))
		    {
		      for (i = (int_bit_position (field) + (bit_offset % 64)) / 8 / 8;
			   i < ((int_bit_position (field) + (bit_offset % 64))
			        + tree_low_cst (DECL_SIZE (field), 0)
				+ 63) / 8 / 8; i++)
			classes[i] =
			  merge_classes (X86_64_INTEGER_CLASS,
					 classes[i]);
		    }
		  else
		    {
		      num = classify_argument (TYPE_MODE (TREE_TYPE (field)),
					       TREE_TYPE (field), subclasses,
					       (int_bit_position (field)
						+ bit_offset) % 256);
		      if (!num)
			return 0;
		      for (i = 0; i < num; i++)
			{
			  int pos =
			    (int_bit_position (field) + (bit_offset % 64)) / 8 / 8;
			  classes[i + pos] =
			    merge_classes (subclasses[i], classes[i + pos]);
			}
		    }
		}
	    }
	  break;

	case ARRAY_TYPE:
	  /* Arrays are handled as small records.  */
	  {
	    int num;
	    num = classify_argument (TYPE_MODE (TREE_TYPE (type)),
				     TREE_TYPE (type), subclasses, bit_offset);
	    if (!num)
	      return 0;

	    /* The partial classes are now full classes.  */
	    if (subclasses[0] == X86_64_SSESF_CLASS && bytes != 4)
	      subclasses[0] = X86_64_SSE_CLASS;
	    if (subclasses[0] == X86_64_INTEGERSI_CLASS && bytes != 4)
	      subclasses[0] = X86_64_INTEGER_CLASS;

	    for (i = 0; i < words; i++)
	      classes[i] = subclasses[i % num];

	    break;
	  }
	case UNION_TYPE:
	case QUAL_UNION_TYPE:
	  /* Unions are similar to RECORD_TYPE but offset is always 0.
	     */

	  /* Unions are not derived.  */
	  gcc_assert (!TYPE_BINFO (type)
		      || !BINFO_N_BASE_BINFOS (TYPE_BINFO (type)));
	  for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	    {
	      if (TREE_CODE (field) == FIELD_DECL)
		{
		  int num;

		  if (TREE_TYPE (field) == error_mark_node)
		    continue;

		  num = classify_argument (TYPE_MODE (TREE_TYPE (field)),
					   TREE_TYPE (field), subclasses,
					   bit_offset);
		  if (!num)
		    return 0;
		  for (i = 0; i < num; i++)
		    classes[i] = merge_classes (subclasses[i], classes[i]);
		}
	    }
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Final merger cleanup.  */
      for (i = 0; i < words; i++)
	{
	  /* If one class is MEMORY, everything should be passed in
	     memory.  */
	  if (classes[i] == X86_64_MEMORY_CLASS)
	    return 0;

	  /* The X86_64_SSEUP_CLASS should be always preceded by
	     X86_64_SSE_CLASS.  */
	  if (classes[i] == X86_64_SSEUP_CLASS
	      && (i == 0 || classes[i - 1] != X86_64_SSE_CLASS))
	    classes[i] = X86_64_SSE_CLASS;

	  /*  X86_64_X87UP_CLASS should be preceded by X86_64_X87_CLASS.  */
	  if (classes[i] == X86_64_X87UP_CLASS
	      && (i == 0 || classes[i - 1] != X86_64_X87_CLASS))
	    classes[i] = X86_64_SSE_CLASS;
	}
      return words;
    }

  /* Compute alignment needed.  We align all types to natural boundaries with
     exception of XFmode that is aligned to 64bits.  */
  if (mode != VOIDmode && mode != BLKmode)
    {
      int mode_alignment = GET_MODE_BITSIZE (mode);

      if (mode == XFmode)
	mode_alignment = 128;
      else if (mode == XCmode)
	mode_alignment = 256;
      if (COMPLEX_MODE_P (mode))
	mode_alignment /= 2;
      /* Misaligned fields are always returned in memory.  */
      if (bit_offset % mode_alignment)
	return 0;
    }

  /* for V1xx modes, just use the base mode */
  if (VECTOR_MODE_P (mode)
      && GET_MODE_SIZE (GET_MODE_INNER (mode)) == bytes)
    mode = GET_MODE_INNER (mode);

  /* Classification of atomic types.  */
  switch (mode)
    {
    case SDmode:
    case DDmode:
      classes[0] = X86_64_SSE_CLASS;
      return 1;
    case TDmode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case DImode:
    case SImode:
    case HImode:
    case QImode:
    case CSImode:
    case CHImode:
    case CQImode:
      if (bit_offset + GET_MODE_BITSIZE (mode) <= 32)
	classes[0] = X86_64_INTEGERSI_CLASS;
      else
	classes[0] = X86_64_INTEGER_CLASS;
      return 1;
    case CDImode:
    case TImode:
      classes[0] = classes[1] = X86_64_INTEGER_CLASS;
      return 2;
    case CTImode:
      return 0;
    case SFmode:
      if (!(bit_offset % 64))
	classes[0] = X86_64_SSESF_CLASS;
      else
	classes[0] = X86_64_SSE_CLASS;
      return 1;
    case DFmode:
      classes[0] = X86_64_SSEDF_CLASS;
      return 1;
    case XFmode:
      classes[0] = X86_64_X87_CLASS;
      classes[1] = X86_64_X87UP_CLASS;
      return 2;
    case TFmode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case SCmode:
      classes[0] = X86_64_SSE_CLASS;
      return 1;
    case DCmode:
      classes[0] = X86_64_SSEDF_CLASS;
      classes[1] = X86_64_SSEDF_CLASS;
      return 2;
    case XCmode:
      classes[0] = X86_64_COMPLEX_X87_CLASS;
      return 1;
    case TCmode:
      /* This modes is larger than 16 bytes.  */
      return 0;
    case V4SFmode:
    case V4SImode:
    case V16QImode:
    case V8HImode:
    case V2DFmode:
    case V2DImode:
      classes[0] = X86_64_SSE_CLASS;
      classes[1] = X86_64_SSEUP_CLASS;
      return 2;
    case V2SFmode:
    case V2SImode:
    case V4HImode:
    case V8QImode:
      classes[0] = X86_64_SSE_CLASS;
      return 1;
    case BLKmode:
    case VOIDmode:
      return 0;
    default:
      gcc_assert (VECTOR_MODE_P (mode));

      if (bytes > 16)
	return 0;

      gcc_assert (GET_MODE_CLASS (GET_MODE_INNER (mode)) == MODE_INT);

      if (bit_offset + GET_MODE_BITSIZE (mode) <= 32)
	classes[0] = X86_64_INTEGERSI_CLASS;
      else
	classes[0] = X86_64_INTEGER_CLASS;
      classes[1] = X86_64_INTEGER_CLASS;
      return 1 + (bytes > 8);
    }
}

/* Examine the argument and return set number of register required in each
   class.  Return 0 iff parameter should be passed in memory.  */
static int
examine_argument (enum machine_mode mode, tree type, int in_return,
		  int *int_nregs, int *sse_nregs)
{
  enum x86_64_reg_class class[MAX_CLASSES];
  int n = classify_argument (mode, type, class, 0);

  *int_nregs = 0;
  *sse_nregs = 0;
  if (!n)
    return 0;
  for (n--; n >= 0; n--)
    switch (class[n])
      {
      case X86_64_INTEGER_CLASS:
      case X86_64_INTEGERSI_CLASS:
	(*int_nregs)++;
	break;
      case X86_64_SSE_CLASS:
      case X86_64_SSESF_CLASS:
      case X86_64_SSEDF_CLASS:
	(*sse_nregs)++;
	break;
      case X86_64_NO_CLASS:
      case X86_64_SSEUP_CLASS:
	break;
      case X86_64_X87_CLASS:
      case X86_64_X87UP_CLASS:
	if (!in_return)
	  return 0;
	break;
      case X86_64_COMPLEX_X87_CLASS:
	return in_return ? 2 : 0;
      case X86_64_MEMORY_CLASS:
	gcc_unreachable ();
      }
  return 1;
}

/* Construct container for the argument used by GCC interface.  See
   FUNCTION_ARG for the detailed description.  */

static rtx
construct_container (enum machine_mode mode, enum machine_mode orig_mode,
		     tree type, int in_return, int nintregs, int nsseregs,
		     const int *intreg, int sse_regno)
{
  /* The following variables hold the static issued_error state.  */
  static bool issued_sse_arg_error;
  static bool issued_sse_ret_error;
  static bool issued_x87_ret_error;

  enum machine_mode tmpmode;
  int bytes =
    (mode == BLKmode) ? int_size_in_bytes (type) : (int) GET_MODE_SIZE (mode);
  enum x86_64_reg_class class[MAX_CLASSES];
  int n;
  int i;
  int nexps = 0;
  int needed_sseregs, needed_intregs;
  rtx exp[MAX_CLASSES];
  rtx ret;

  n = classify_argument (mode, type, class, 0);
  if (TARGET_DEBUG_ARG)
    {
      if (!n)
	fprintf (stderr, "Memory class\n");
      else
	{
	  fprintf (stderr, "Classes:");
	  for (i = 0; i < n; i++)
	    {
	      fprintf (stderr, " %s", x86_64_reg_class_name[class[i]]);
	    }
	   fprintf (stderr, "\n");
	}
    }
  if (!n)
    return NULL;
  if (!examine_argument (mode, type, in_return, &needed_intregs,
			 &needed_sseregs))
    return NULL;
  if (needed_intregs > nintregs || needed_sseregs > nsseregs)
    return NULL;

  /* We allowed the user to turn off SSE for kernel mode.  Don't crash if
     some less clueful developer tries to use floating-point anyway.  */
  if (needed_sseregs && !TARGET_SSE)
    {
      if (in_return)
	{
	  if (!issued_sse_ret_error)
	    {
	      error ("SSE register return with SSE disabled");
	      issued_sse_ret_error = true;
	    }
	}
      else if (!issued_sse_arg_error)
	{
	  error ("SSE register argument with SSE disabled");
	  issued_sse_arg_error = true;
	}
      return NULL;
    }

  /* Likewise, error if the ABI requires us to return values in the
     x87 registers and the user specified -mno-80387.  */
  if (!TARGET_80387 && in_return)
    for (i = 0; i < n; i++)
      if (class[i] == X86_64_X87_CLASS
	  || class[i] == X86_64_X87UP_CLASS
	  || class[i] == X86_64_COMPLEX_X87_CLASS)
	{
	  if (!issued_x87_ret_error)
	    {
	      error ("x87 register return with x87 disabled");
	      issued_x87_ret_error = true;
	    }
	  return NULL;
	}

  /* First construct simple cases.  Avoid SCmode, since we want to use
     single register to pass this type.  */
  if (n == 1 && mode != SCmode)
    switch (class[0])
      {
      case X86_64_INTEGER_CLASS:
      case X86_64_INTEGERSI_CLASS:
	return gen_rtx_REG (mode, intreg[0]);
      case X86_64_SSE_CLASS:
      case X86_64_SSESF_CLASS:
      case X86_64_SSEDF_CLASS:
	return gen_reg_or_parallel (mode, orig_mode, SSE_REGNO (sse_regno));
      case X86_64_X87_CLASS:
      case X86_64_COMPLEX_X87_CLASS:
	return gen_rtx_REG (mode, FIRST_STACK_REG);
      case X86_64_NO_CLASS:
	/* Zero sized array, struct or class.  */
	return NULL;
      default:
	gcc_unreachable ();
      }
  if (n == 2 && class[0] == X86_64_SSE_CLASS && class[1] == X86_64_SSEUP_CLASS
      && mode != BLKmode)
    return gen_rtx_REG (mode, SSE_REGNO (sse_regno));
  if (n == 2
      && class[0] == X86_64_X87_CLASS && class[1] == X86_64_X87UP_CLASS)
    return gen_rtx_REG (XFmode, FIRST_STACK_REG);
  if (n == 2 && class[0] == X86_64_INTEGER_CLASS
      && class[1] == X86_64_INTEGER_CLASS
      && (mode == CDImode || mode == TImode || mode == TFmode)
      && intreg[0] + 1 == intreg[1])
    return gen_rtx_REG (mode, intreg[0]);

  /* Otherwise figure out the entries of the PARALLEL.  */
  for (i = 0; i < n; i++)
    {
      switch (class[i])
        {
	  case X86_64_NO_CLASS:
	    break;
	  case X86_64_INTEGER_CLASS:
	  case X86_64_INTEGERSI_CLASS:
	    /* Merge TImodes on aligned occasions here too.  */
	    if (i * 8 + 8 > bytes)
	      tmpmode = mode_for_size ((bytes - i * 8) * BITS_PER_UNIT, MODE_INT, 0);
	    else if (class[i] == X86_64_INTEGERSI_CLASS)
	      tmpmode = SImode;
	    else
	      tmpmode = DImode;
	    /* We've requested 24 bytes we don't have mode for.  Use DImode.  */
	    if (tmpmode == BLKmode)
	      tmpmode = DImode;
	    exp [nexps++] = gen_rtx_EXPR_LIST (VOIDmode,
					       gen_rtx_REG (tmpmode, *intreg),
					       GEN_INT (i*8));
	    intreg++;
	    break;
	  case X86_64_SSESF_CLASS:
	    exp [nexps++] = gen_rtx_EXPR_LIST (VOIDmode,
					       gen_rtx_REG (SFmode,
							    SSE_REGNO (sse_regno)),
					       GEN_INT (i*8));
	    sse_regno++;
	    break;
	  case X86_64_SSEDF_CLASS:
	    exp [nexps++] = gen_rtx_EXPR_LIST (VOIDmode,
					       gen_rtx_REG (DFmode,
							    SSE_REGNO (sse_regno)),
					       GEN_INT (i*8));
	    sse_regno++;
	    break;
	  case X86_64_SSE_CLASS:
	    if (i < n - 1 && class[i + 1] == X86_64_SSEUP_CLASS)
	      tmpmode = TImode;
	    else
	      tmpmode = DImode;
	    exp [nexps++] = gen_rtx_EXPR_LIST (VOIDmode,
					       gen_rtx_REG (tmpmode,
							    SSE_REGNO (sse_regno)),
					       GEN_INT (i*8));
	    if (tmpmode == TImode)
	      i++;
	    sse_regno++;
	    break;
	  default:
	    gcc_unreachable ();
	}
    }

  /* Empty aligned struct, union or class.  */
  if (nexps == 0)
    return NULL;

  ret =  gen_rtx_PARALLEL (mode, rtvec_alloc (nexps));
  for (i = 0; i < nexps; i++)
    XVECEXP (ret, 0, i) = exp [i];
  return ret;
}

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

void
function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
		      tree type, int named)
{
  int bytes =
    (mode == BLKmode) ? int_size_in_bytes (type) : (int) GET_MODE_SIZE (mode);
  int words = (bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (type)
    mode = type_natural_mode (type);

  if (TARGET_DEBUG_ARG)
    fprintf (stderr, "function_adv (sz=%d, wds=%2d, nregs=%d, ssenregs=%d, "
	     "mode=%s, named=%d)\n\n",
	     words, cum->words, cum->nregs, cum->sse_nregs,
	     GET_MODE_NAME (mode), named);

  if (TARGET_64BIT)
    {
      int int_nregs, sse_nregs;
      if (!examine_argument (mode, type, 0, &int_nregs, &sse_nregs))
	cum->words += words;
      else if (sse_nregs <= cum->sse_nregs && int_nregs <= cum->nregs)
	{
	  cum->nregs -= int_nregs;
	  cum->sse_nregs -= sse_nregs;
	  cum->regno += int_nregs;
	  cum->sse_regno += sse_nregs;
	}
      else
	cum->words += words;
    }
  else
    {
      switch (mode)
	{
	default:
	  break;

	case BLKmode:
	  if (bytes < 0)
	    break;
	  /* FALLTHRU */

	case DImode:
	case SImode:
	case HImode:
	case QImode:
	  cum->words += words;
	  cum->nregs -= words;
	  cum->regno += words;

	  if (cum->nregs <= 0)
	    {
	      cum->nregs = 0;
	      cum->regno = 0;
	    }
	  break;

	case DFmode:
	  if (cum->float_in_sse < 2)
	    break;
	case SFmode:
	  if (cum->float_in_sse < 1)
	    break;
	  /* FALLTHRU */

	case TImode:
	case V16QImode:
	case V8HImode:
	case V4SImode:
	case V2DImode:
	case V4SFmode:
	case V2DFmode:
	  if (!type || !AGGREGATE_TYPE_P (type))
	    {
	      cum->sse_words += words;
	      cum->sse_nregs -= 1;
	      cum->sse_regno += 1;
	      if (cum->sse_nregs <= 0)
		{
		  cum->sse_nregs = 0;
		  cum->sse_regno = 0;
		}
	    }
	  break;

	case V8QImode:
	case V4HImode:
	case V2SImode:
	case V2SFmode:
	  if (!type || !AGGREGATE_TYPE_P (type))
	    {
	      cum->mmx_words += words;
	      cum->mmx_nregs -= 1;
	      cum->mmx_regno += 1;
	      if (cum->mmx_nregs <= 0)
		{
		  cum->mmx_nregs = 0;
		  cum->mmx_regno = 0;
		}
	    }
	  break;
	}
    }
}

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */

rtx
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode orig_mode,
	      tree type, int named)
{
  enum machine_mode mode = orig_mode;
  rtx ret = NULL_RTX;
  int bytes =
    (mode == BLKmode) ? int_size_in_bytes (type) : (int) GET_MODE_SIZE (mode);
  int words = (bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
  static bool warnedsse, warnedmmx;

  /* To simplify the code below, represent vector types with a vector mode
     even if MMX/SSE are not active.  */
  if (type && TREE_CODE (type) == VECTOR_TYPE)
    mode = type_natural_mode (type);

  /* Handle a hidden AL argument containing number of registers for varargs
     x86-64 functions.  For i386 ABI just return constm1_rtx to avoid
     any AL settings.  */
  if (mode == VOIDmode)
    {
      if (TARGET_64BIT)
	return GEN_INT (cum->maybe_vaarg
			? (cum->sse_nregs < 0
			   ? SSE_REGPARM_MAX
			   : cum->sse_regno)
			: -1);
      else
	return constm1_rtx;
    }
  if (TARGET_64BIT)
    ret = construct_container (mode, orig_mode, type, 0, cum->nregs,
			       cum->sse_nregs,
			       &x86_64_int_parameter_registers [cum->regno],
			       cum->sse_regno);
  else
    switch (mode)
      {
	/* For now, pass fp/complex values on the stack.  */
      default:
	break;

      case BLKmode:
	if (bytes < 0)
	  break;
	/* FALLTHRU */
      case DImode:
      case SImode:
      case HImode:
      case QImode:
	if (words <= cum->nregs)
	  {
	    int regno = cum->regno;

	    /* Fastcall allocates the first two DWORD (SImode) or
	       smaller arguments to ECX and EDX.  */
	    if (cum->fastcall)
	      {
	        if (mode == BLKmode || mode == DImode)
	          break;

	        /* ECX not EAX is the first allocated register.  */
	        if (regno == 0)
		  regno = 2;
	      }
	    ret = gen_rtx_REG (mode, regno);
	  }
	break;
      case DFmode:
	if (cum->float_in_sse < 2)
	  break;
      case SFmode:
	if (cum->float_in_sse < 1)
	  break;
	/* FALLTHRU */
      case TImode:
      case V16QImode:
      case V8HImode:
      case V4SImode:
      case V2DImode:
      case V4SFmode:
      case V2DFmode:
	if (!type || !AGGREGATE_TYPE_P (type))
	  {
	    if (!TARGET_SSE && !warnedsse && cum->warn_sse)
	      {
		warnedsse = true;
		warning (0, "SSE vector argument without SSE enabled "
			 "changes the ABI");
	      }
	    if (cum->sse_nregs)
	      ret = gen_reg_or_parallel (mode, orig_mode,
					 cum->sse_regno + FIRST_SSE_REG);
	  }
	break;
      case V8QImode:
      case V4HImode:
      case V2SImode:
      case V2SFmode:
	if (!type || !AGGREGATE_TYPE_P (type))
	  {
	    if (!TARGET_MMX && !warnedmmx && cum->warn_mmx)
	      {
		warnedmmx = true;
		warning (0, "MMX vector argument without MMX enabled "
			 "changes the ABI");
	      }
	    if (cum->mmx_nregs)
	      ret = gen_reg_or_parallel (mode, orig_mode,
					 cum->mmx_regno + FIRST_MMX_REG);
	  }
	break;
      }

  if (TARGET_DEBUG_ARG)
    {
      fprintf (stderr,
	       "function_arg (size=%d, wds=%2d, nregs=%d, mode=%4s, named=%d, ",
	       words, cum->words, cum->nregs, GET_MODE_NAME (mode), named);

      if (ret)
	print_simple_rtl (stderr, ret);
      else
	fprintf (stderr, ", stack");

      fprintf (stderr, " )\n");
    }

  return ret;
}

/* A C expression that indicates when an argument must be passed by
   reference.  If nonzero for an argument, a copy of that argument is
   made in memory and a pointer to the argument is passed instead of
   the argument itself.  The pointer is passed in whatever way is
   appropriate for passing a pointer to that type.  */

static bool
ix86_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			tree type, bool named ATTRIBUTE_UNUSED)
{
  if (!TARGET_64BIT)
    return 0;

  if (type && int_size_in_bytes (type) == -1)
    {
      if (TARGET_DEBUG_ARG)
	fprintf (stderr, "function_arg_pass_by_reference\n");
      return 1;
    }

  return 0;
}

/* Return true when TYPE should be 128bit aligned for 32bit argument passing
   ABI.  Only called if TARGET_SSE.  */
static bool
contains_128bit_aligned_vector_p (tree type)
{
  enum machine_mode mode = TYPE_MODE (type);
  if (SSE_REG_MODE_P (mode)
      && (!TYPE_USER_ALIGN (type) || TYPE_ALIGN (type) > 128))
    return true;
  if (TYPE_ALIGN (type) < 128)
    return false;

  if (AGGREGATE_TYPE_P (type))
    {
      /* Walk the aggregates recursively.  */
      switch (TREE_CODE (type))
	{
	case RECORD_TYPE:
	case UNION_TYPE:
	case QUAL_UNION_TYPE:
	  {
	    tree field;

	    if (TYPE_BINFO (type))
	      {
		tree binfo, base_binfo;
		int i;

		for (binfo = TYPE_BINFO (type), i = 0;
		     BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
		  if (contains_128bit_aligned_vector_p
		      (BINFO_TYPE (base_binfo)))
		    return true;
	      }
	    /* And now merge the fields of structure.  */
	    for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	      {
		if (TREE_CODE (field) == FIELD_DECL
		    && contains_128bit_aligned_vector_p (TREE_TYPE (field)))
		  return true;
	      }
	    break;
	  }

	case ARRAY_TYPE:
	  /* Just for use if some languages passes arrays by value.  */
	  if (contains_128bit_aligned_vector_p (TREE_TYPE (type)))
	    return true;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  return false;
}

/* Gives the alignment boundary, in bits, of an argument with the
   specified mode and type.  */

int
ix86_function_arg_boundary (enum machine_mode mode, tree type)
{
  int align;
  if (type)
    align = TYPE_ALIGN (type);
  else
    align = GET_MODE_ALIGNMENT (mode);
  if (align < PARM_BOUNDARY)
    align = PARM_BOUNDARY;
  if (!TARGET_64BIT)
    {
      /* i386 ABI defines all arguments to be 4 byte aligned.  We have to
	 make an exception for SSE modes since these require 128bit
	 alignment.

	 The handling here differs from field_alignment.  ICC aligns MMX
	 arguments to 4 byte boundaries, while structure fields are aligned
	 to 8 byte boundaries.  */
      if (!TARGET_SSE)
	align = PARM_BOUNDARY;
      else if (!type)
	{
	  if (!SSE_REG_MODE_P (mode))
	    align = PARM_BOUNDARY;
	}
      else
	{
	  if (!contains_128bit_aligned_vector_p (type))
	    align = PARM_BOUNDARY;
	}
    }
  if (align > 128)
    align = 128;
  return align;
}

/* Return true if N is a possible register number of function value.  */
bool
ix86_function_value_regno_p (int regno)
{
  if (TARGET_MACHO)
    {
      if (!TARGET_64BIT)
        {
          return ((regno) == 0
                  || ((regno) == FIRST_FLOAT_REG && TARGET_FLOAT_RETURNS_IN_80387)
                  || ((regno) == FIRST_SSE_REG && TARGET_SSE));
        }
      return ((regno) == 0 || (regno) == FIRST_FLOAT_REG
              || ((regno) == FIRST_SSE_REG && TARGET_SSE)
              || ((regno) == FIRST_FLOAT_REG && TARGET_FLOAT_RETURNS_IN_80387));
      }
  else
    {
      if (regno == 0
          || (regno == FIRST_FLOAT_REG && TARGET_FLOAT_RETURNS_IN_80387)
          || (regno == FIRST_SSE_REG && TARGET_SSE))
        return true;

      if (!TARGET_64BIT
          && (regno == FIRST_MMX_REG && TARGET_MMX))
	    return true;

      return false;
    }
}

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */
rtx
ix86_function_value (tree valtype, tree fntype_or_decl,
		     bool outgoing ATTRIBUTE_UNUSED)
{
  enum machine_mode natmode = type_natural_mode (valtype);

  if (TARGET_64BIT)
    {
      rtx ret = construct_container (natmode, TYPE_MODE (valtype), valtype,
				     1, REGPARM_MAX, SSE_REGPARM_MAX,
				     x86_64_int_return_registers, 0);
      /* For zero sized structures, construct_container return NULL, but we
	 need to keep rest of compiler happy by returning meaningful value.  */
      if (!ret)
	ret = gen_rtx_REG (TYPE_MODE (valtype), 0);
      return ret;
    }
  else
    {
      tree fn = NULL_TREE, fntype;
      if (fntype_or_decl
	  && DECL_P (fntype_or_decl))
        fn = fntype_or_decl;
      fntype = fn ? TREE_TYPE (fn) : fntype_or_decl;
      return gen_rtx_REG (TYPE_MODE (valtype),
			  ix86_value_regno (natmode, fn, fntype));
    }
}

/* Return true iff type is returned in memory.  */
int
ix86_return_in_memory (tree type)
{
  int needed_intregs, needed_sseregs, size;
  enum machine_mode mode = type_natural_mode (type);

  if (TARGET_64BIT)
    return !examine_argument (mode, type, 1, &needed_intregs, &needed_sseregs);

  if (mode == BLKmode)
    return 1;

  size = int_size_in_bytes (type);

  if (MS_AGGREGATE_RETURN && AGGREGATE_TYPE_P (type) && size <= 8)
    return 0;

  if (VECTOR_MODE_P (mode) || mode == TImode)
    {
      /* User-created vectors small enough to fit in EAX.  */
      if (size < 8)
	return 0;

      /* MMX/3dNow values are returned in MM0,
	 except when it doesn't exits.  */
      if (size == 8)
	return (TARGET_MMX ? 0 : 1);

      /* SSE values are returned in XMM0, except when it doesn't exist.  */
      if (size == 16)
	return (TARGET_SSE ? 0 : 1);
    }

  if (mode == XFmode)
    return 0;

  if (mode == TDmode)
    return 1;

  if (size > 12)
    return 1;
  return 0;
}

/* When returning SSE vector types, we have a choice of either
     (1) being abi incompatible with a -march switch, or
     (2) generating an error.
   Given no good solution, I think the safest thing is one warning.
   The user won't be able to use -Werror, but....

   Choose the STRUCT_VALUE_RTX hook because that's (at present) only
   called in response to actually generating a caller or callee that
   uses such a type.  As opposed to RETURN_IN_MEMORY, which is called
   via aggregate_value_p for general type probing from tree-ssa.  */

static rtx
ix86_struct_value_rtx (tree type, int incoming ATTRIBUTE_UNUSED)
{
  static bool warnedsse, warnedmmx;

  if (type)
    {
      /* Look at the return type of the function, not the function type.  */
      enum machine_mode mode = TYPE_MODE (TREE_TYPE (type));

      if (!TARGET_SSE && !warnedsse)
	{
	  if (mode == TImode
	      || (VECTOR_MODE_P (mode) && GET_MODE_SIZE (mode) == 16))
	    {
	      warnedsse = true;
	      warning (0, "SSE vector return without SSE enabled "
		       "changes the ABI");
	    }
	}

      if (!TARGET_MMX && !warnedmmx)
	{
	  if (VECTOR_MODE_P (mode) && GET_MODE_SIZE (mode) == 8)
	    {
	      warnedmmx = true;
	      warning (0, "MMX vector return without MMX enabled "
		       "changes the ABI");
	    }
	}
    }

  return NULL;
}

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
rtx
ix86_libcall_value (enum machine_mode mode)
{
  if (TARGET_64BIT)
    {
      switch (mode)
	{
	case SFmode:
	case SCmode:
	case DFmode:
	case DCmode:
	case TFmode:
	case SDmode:
	case DDmode:
	case TDmode:
	  return gen_rtx_REG (mode, FIRST_SSE_REG);
	case XFmode:
	case XCmode:
	  return gen_rtx_REG (mode, FIRST_FLOAT_REG);
	case TCmode:
	  return NULL;
	default:
	  return gen_rtx_REG (mode, 0);
	}
    }
  else
    return gen_rtx_REG (mode, ix86_value_regno (mode, NULL, NULL));
}

/* Given a mode, return the register to use for a return value.  */

static int
ix86_value_regno (enum machine_mode mode, tree func, tree fntype)
{
  gcc_assert (!TARGET_64BIT);

  /* 8-byte vector modes in %mm0. See ix86_return_in_memory for where
     we normally prevent this case when mmx is not available.  However
     some ABIs may require the result to be returned like DImode.  */
  if (VECTOR_MODE_P (mode) && GET_MODE_SIZE (mode) == 8)
    return TARGET_MMX ? FIRST_MMX_REG : 0;

  /* 16-byte vector modes in %xmm0.  See ix86_return_in_memory for where
     we prevent this case when sse is not available.  However some ABIs
     may require the result to be returned like integer TImode.  */
  if (mode == TImode || (VECTOR_MODE_P (mode) && GET_MODE_SIZE (mode) == 16))
    return TARGET_SSE ? FIRST_SSE_REG : 0;

  /* Decimal floating point values can go in %eax, unlike other float modes.  */
  if (DECIMAL_FLOAT_MODE_P (mode))
    return 0;

  /* Most things go in %eax, except (unless -mno-fp-ret-in-387) fp values.  */
  if (!SCALAR_FLOAT_MODE_P (mode) || !TARGET_FLOAT_RETURNS_IN_80387)
    return 0;

  /* Floating point return values in %st(0), except for local functions when
     SSE math is enabled or for functions with sseregparm attribute.  */
  if ((func || fntype)
      && (mode == SFmode || mode == DFmode))
    {
      int sse_level = ix86_function_sseregparm (fntype, func);
      if ((sse_level >= 1 && mode == SFmode)
	  || (sse_level == 2 && mode == DFmode))
        return FIRST_SSE_REG;
    }

  return FIRST_FLOAT_REG;
}

/* Create the va_list data type.  */

static tree
ix86_build_builtin_va_list (void)
{
  tree f_gpr, f_fpr, f_ovf, f_sav, record, type_decl;

  /* For i386 we use plain pointer to argument area.  */
  if (!TARGET_64BIT)
    return build_pointer_type (char_type_node);

  record = (*lang_hooks.types.make_type) (RECORD_TYPE);
  type_decl = build_decl (TYPE_DECL, get_identifier ("__va_list_tag"), record);

  f_gpr = build_decl (FIELD_DECL, get_identifier ("gp_offset"),
		      unsigned_type_node);
  f_fpr = build_decl (FIELD_DECL, get_identifier ("fp_offset"),
		      unsigned_type_node);
  f_ovf = build_decl (FIELD_DECL, get_identifier ("overflow_arg_area"),
		      ptr_type_node);
  f_sav = build_decl (FIELD_DECL, get_identifier ("reg_save_area"),
		      ptr_type_node);

  va_list_gpr_counter_field = f_gpr;
  va_list_fpr_counter_field = f_fpr;

  DECL_FIELD_CONTEXT (f_gpr) = record;
  DECL_FIELD_CONTEXT (f_fpr) = record;
  DECL_FIELD_CONTEXT (f_ovf) = record;
  DECL_FIELD_CONTEXT (f_sav) = record;

  TREE_CHAIN (record) = type_decl;
  TYPE_NAME (record) = type_decl;
  TYPE_FIELDS (record) = f_gpr;
  TREE_CHAIN (f_gpr) = f_fpr;
  TREE_CHAIN (f_fpr) = f_ovf;
  TREE_CHAIN (f_ovf) = f_sav;

  layout_type (record);

  /* The correct type is an array type of one element.  */
  return build_array_type (record, build_index_type (size_zero_node));
}

/* Worker function for TARGET_SETUP_INCOMING_VARARGS.  */

static void
ix86_setup_incoming_varargs (CUMULATIVE_ARGS *cum, enum machine_mode mode,
			     tree type, int *pretend_size ATTRIBUTE_UNUSED,
			     int no_rtl)
{
  CUMULATIVE_ARGS next_cum;
  rtx save_area = NULL_RTX, mem;
  rtx label;
  rtx label_ref;
  rtx tmp_reg;
  rtx nsse_reg;
  int set;
  tree fntype;
  int stdarg_p;
  int i;

  if (!TARGET_64BIT)
    return;

  if (! cfun->va_list_gpr_size && ! cfun->va_list_fpr_size)
    return;

  /* Indicate to allocate space on the stack for varargs save area.  */
  ix86_save_varrargs_registers = 1;

  cfun->stack_alignment_needed = 128;

  fntype = TREE_TYPE (current_function_decl);
  stdarg_p = (TYPE_ARG_TYPES (fntype) != 0
	      && (TREE_VALUE (tree_last (TYPE_ARG_TYPES (fntype)))
		  != void_type_node));

  /* For varargs, we do not want to skip the dummy va_dcl argument.
     For stdargs, we do want to skip the last named argument.  */
  next_cum = *cum;
  if (stdarg_p)
    function_arg_advance (&next_cum, mode, type, 1);

  if (!no_rtl)
    save_area = frame_pointer_rtx;

  set = get_varargs_alias_set ();

  for (i = next_cum.regno;
       i < ix86_regparm
       && i < next_cum.regno + cfun->va_list_gpr_size / UNITS_PER_WORD;
       i++)
    {
      mem = gen_rtx_MEM (Pmode,
			 plus_constant (save_area, i * UNITS_PER_WORD));
      MEM_NOTRAP_P (mem) = 1;
      set_mem_alias_set (mem, set);
      emit_move_insn (mem, gen_rtx_REG (Pmode,
					x86_64_int_parameter_registers[i]));
    }

  if (next_cum.sse_nregs && cfun->va_list_fpr_size)
    {
      /* Now emit code to save SSE registers.  The AX parameter contains number
	 of SSE parameter registers used to call this function.  We use
	 sse_prologue_save insn template that produces computed jump across
	 SSE saves.  We need some preparation work to get this working.  */

      label = gen_label_rtx ();
      label_ref = gen_rtx_LABEL_REF (Pmode, label);

      /* Compute address to jump to :
         label - 5*eax + nnamed_sse_arguments*5  */
      tmp_reg = gen_reg_rtx (Pmode);
      nsse_reg = gen_reg_rtx (Pmode);
      emit_insn (gen_zero_extendqidi2 (nsse_reg, gen_rtx_REG (QImode, 0)));
      emit_insn (gen_rtx_SET (VOIDmode, tmp_reg,
			      gen_rtx_MULT (Pmode, nsse_reg,
					    GEN_INT (4))));
      if (next_cum.sse_regno)
	emit_move_insn
	  (nsse_reg,
	   gen_rtx_CONST (DImode,
			  gen_rtx_PLUS (DImode,
					label_ref,
					GEN_INT (next_cum.sse_regno * 4))));
      else
	emit_move_insn (nsse_reg, label_ref);
      emit_insn (gen_subdi3 (nsse_reg, nsse_reg, tmp_reg));

      /* Compute address of memory block we save into.  We always use pointer
	 pointing 127 bytes after first byte to store - this is needed to keep
	 instruction size limited by 4 bytes.  */
      tmp_reg = gen_reg_rtx (Pmode);
      emit_insn (gen_rtx_SET (VOIDmode, tmp_reg,
			      plus_constant (save_area,
					     8 * REGPARM_MAX + 127)));
      mem = gen_rtx_MEM (BLKmode, plus_constant (tmp_reg, -127));
      MEM_NOTRAP_P (mem) = 1;
      set_mem_alias_set (mem, set);
      set_mem_align (mem, BITS_PER_WORD);

      /* And finally do the dirty job!  */
      emit_insn (gen_sse_prologue_save (mem, nsse_reg,
					GEN_INT (next_cum.sse_regno), label));
    }

}

/* Implement va_start.  */

void
ix86_va_start (tree valist, rtx nextarg)
{
  HOST_WIDE_INT words, n_gpr, n_fpr;
  tree f_gpr, f_fpr, f_ovf, f_sav;
  tree gpr, fpr, ovf, sav, t;
  tree type;

  /* Only 64bit target needs something special.  */
  if (!TARGET_64BIT)
    {
      std_expand_builtin_va_start (valist, nextarg);
      return;
    }

  f_gpr = TYPE_FIELDS (TREE_TYPE (va_list_type_node));
  f_fpr = TREE_CHAIN (f_gpr);
  f_ovf = TREE_CHAIN (f_fpr);
  f_sav = TREE_CHAIN (f_ovf);

  valist = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (valist)), valist);
  gpr = build3 (COMPONENT_REF, TREE_TYPE (f_gpr), valist, f_gpr, NULL_TREE);
  fpr = build3 (COMPONENT_REF, TREE_TYPE (f_fpr), valist, f_fpr, NULL_TREE);
  ovf = build3 (COMPONENT_REF, TREE_TYPE (f_ovf), valist, f_ovf, NULL_TREE);
  sav = build3 (COMPONENT_REF, TREE_TYPE (f_sav), valist, f_sav, NULL_TREE);

  /* Count number of gp and fp argument registers used.  */
  words = current_function_args_info.words;
  n_gpr = current_function_args_info.regno;
  n_fpr = current_function_args_info.sse_regno;

  if (TARGET_DEBUG_ARG)
    fprintf (stderr, "va_start: words = %d, n_gpr = %d, n_fpr = %d\n",
	     (int) words, (int) n_gpr, (int) n_fpr);

  if (cfun->va_list_gpr_size)
    {
      type = TREE_TYPE (gpr);
      t = build2 (MODIFY_EXPR, type, gpr,
		  build_int_cst (type, n_gpr * 8));
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }

  if (cfun->va_list_fpr_size)
    {
      type = TREE_TYPE (fpr);
      t = build2 (MODIFY_EXPR, type, fpr,
		  build_int_cst (type, n_fpr * 16 + 8*REGPARM_MAX));
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }

  /* Find the overflow area.  */
  type = TREE_TYPE (ovf);
  t = make_tree (type, virtual_incoming_args_rtx);
  if (words != 0)
    t = build2 (PLUS_EXPR, type, t,
	        build_int_cst (type, words * UNITS_PER_WORD));
  t = build2 (MODIFY_EXPR, type, ovf, t);
  TREE_SIDE_EFFECTS (t) = 1;
  expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);

  if (cfun->va_list_gpr_size || cfun->va_list_fpr_size)
    {
      /* Find the register save area.
	 Prologue of the function save it right above stack frame.  */
      type = TREE_TYPE (sav);
      t = make_tree (type, frame_pointer_rtx);
      t = build2 (MODIFY_EXPR, type, sav, t);
      TREE_SIDE_EFFECTS (t) = 1;
      expand_expr (t, const0_rtx, VOIDmode, EXPAND_NORMAL);
    }
}

/* Implement va_arg.  */

tree
ix86_gimplify_va_arg (tree valist, tree type, tree *pre_p, tree *post_p)
{
  static const int intreg[6] = { 0, 1, 2, 3, 4, 5 };
  tree f_gpr, f_fpr, f_ovf, f_sav;
  tree gpr, fpr, ovf, sav, t;
  int size, rsize;
  tree lab_false, lab_over = NULL_TREE;
  tree addr, t2;
  rtx container;
  int indirect_p = 0;
  tree ptrtype;
  enum machine_mode nat_mode;

  /* Only 64bit target needs something special.  */
  if (!TARGET_64BIT)
    return std_gimplify_va_arg_expr (valist, type, pre_p, post_p);

  f_gpr = TYPE_FIELDS (TREE_TYPE (va_list_type_node));
  f_fpr = TREE_CHAIN (f_gpr);
  f_ovf = TREE_CHAIN (f_fpr);
  f_sav = TREE_CHAIN (f_ovf);

  valist = build_va_arg_indirect_ref (valist);
  gpr = build3 (COMPONENT_REF, TREE_TYPE (f_gpr), valist, f_gpr, NULL_TREE);
  fpr = build3 (COMPONENT_REF, TREE_TYPE (f_fpr), valist, f_fpr, NULL_TREE);
  ovf = build3 (COMPONENT_REF, TREE_TYPE (f_ovf), valist, f_ovf, NULL_TREE);
  sav = build3 (COMPONENT_REF, TREE_TYPE (f_sav), valist, f_sav, NULL_TREE);

  indirect_p = pass_by_reference (NULL, TYPE_MODE (type), type, false);
  if (indirect_p)
    type = build_pointer_type (type);
  size = int_size_in_bytes (type);
  rsize = (size + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  nat_mode = type_natural_mode (type);
  container = construct_container (nat_mode, TYPE_MODE (type), type, 0,
				   REGPARM_MAX, SSE_REGPARM_MAX, intreg, 0);

  /* Pull the value out of the saved registers.  */

  addr = create_tmp_var (ptr_type_node, "addr");
  DECL_POINTER_ALIAS_SET (addr) = get_varargs_alias_set ();

  if (container)
    {
      int needed_intregs, needed_sseregs;
      bool need_temp;
      tree int_addr, sse_addr;

      lab_false = create_artificial_label ();
      lab_over = create_artificial_label ();

      examine_argument (nat_mode, type, 0, &needed_intregs, &needed_sseregs);

      need_temp = (!REG_P (container)
		   && ((needed_intregs && TYPE_ALIGN (type) > 64)
		       || TYPE_ALIGN (type) > 128));

      /* In case we are passing structure, verify that it is consecutive block
         on the register save area.  If not we need to do moves.  */
      if (!need_temp && !REG_P (container))
	{
	  /* Verify that all registers are strictly consecutive  */
	  if (SSE_REGNO_P (REGNO (XEXP (XVECEXP (container, 0, 0), 0))))
	    {
	      int i;

	      for (i = 0; i < XVECLEN (container, 0) && !need_temp; i++)
		{
		  rtx slot = XVECEXP (container, 0, i);
		  if (REGNO (XEXP (slot, 0)) != FIRST_SSE_REG + (unsigned int) i
		      || INTVAL (XEXP (slot, 1)) != i * 16)
		    need_temp = 1;
		}
	    }
	  else
	    {
	      int i;

	      for (i = 0; i < XVECLEN (container, 0) && !need_temp; i++)
		{
		  rtx slot = XVECEXP (container, 0, i);
		  if (REGNO (XEXP (slot, 0)) != (unsigned int) i
		      || INTVAL (XEXP (slot, 1)) != i * 8)
		    need_temp = 1;
		}
	    }
	}
      if (!need_temp)
	{
	  int_addr = addr;
	  sse_addr = addr;
	}
      else
	{
	  int_addr = create_tmp_var (ptr_type_node, "int_addr");
	  DECL_POINTER_ALIAS_SET (int_addr) = get_varargs_alias_set ();
	  sse_addr = create_tmp_var (ptr_type_node, "sse_addr");
	  DECL_POINTER_ALIAS_SET (sse_addr) = get_varargs_alias_set ();
	}

      /* First ensure that we fit completely in registers.  */
      if (needed_intregs)
	{
	  t = build_int_cst (TREE_TYPE (gpr),
			     (REGPARM_MAX - needed_intregs + 1) * 8);
	  t = build2 (GE_EXPR, boolean_type_node, gpr, t);
	  t2 = build1 (GOTO_EXPR, void_type_node, lab_false);
	  t = build3 (COND_EXPR, void_type_node, t, t2, NULL_TREE);
	  gimplify_and_add (t, pre_p);
	}
      if (needed_sseregs)
	{
	  t = build_int_cst (TREE_TYPE (fpr),
			     (SSE_REGPARM_MAX - needed_sseregs + 1) * 16
			     + REGPARM_MAX * 8);
	  t = build2 (GE_EXPR, boolean_type_node, fpr, t);
	  t2 = build1 (GOTO_EXPR, void_type_node, lab_false);
	  t = build3 (COND_EXPR, void_type_node, t, t2, NULL_TREE);
	  gimplify_and_add (t, pre_p);
	}

      /* Compute index to start of area used for integer regs.  */
      if (needed_intregs)
	{
	  /* int_addr = gpr + sav; */
	  t = fold_convert (ptr_type_node, gpr);
	  t = build2 (PLUS_EXPR, ptr_type_node, sav, t);
	  t = build2 (MODIFY_EXPR, void_type_node, int_addr, t);
	  gimplify_and_add (t, pre_p);
	}
      if (needed_sseregs)
	{
	  /* sse_addr = fpr + sav; */
	  t = fold_convert (ptr_type_node, fpr);
	  t = build2 (PLUS_EXPR, ptr_type_node, sav, t);
	  t = build2 (MODIFY_EXPR, void_type_node, sse_addr, t);
	  gimplify_and_add (t, pre_p);
	}
      if (need_temp)
	{
	  int i;
	  tree temp = create_tmp_var (type, "va_arg_tmp");

	  /* addr = &temp; */
	  t = build1 (ADDR_EXPR, build_pointer_type (type), temp);
	  t = build2 (MODIFY_EXPR, void_type_node, addr, t);
	  gimplify_and_add (t, pre_p);

	  for (i = 0; i < XVECLEN (container, 0); i++)
	    {
	      rtx slot = XVECEXP (container, 0, i);
	      rtx reg = XEXP (slot, 0);
	      enum machine_mode mode = GET_MODE (reg);
	      tree piece_type = lang_hooks.types.type_for_mode (mode, 1);
	      tree addr_type = build_pointer_type (piece_type);
	      tree src_addr, src;
	      int src_offset;
	      tree dest_addr, dest;

	      if (SSE_REGNO_P (REGNO (reg)))
		{
		  src_addr = sse_addr;
		  src_offset = (REGNO (reg) - FIRST_SSE_REG) * 16;
		}
	      else
		{
		  src_addr = int_addr;
		  src_offset = REGNO (reg) * 8;
		}
	      src_addr = fold_convert (addr_type, src_addr);
	      src_addr = fold (build2 (PLUS_EXPR, addr_type, src_addr,
				       size_int (src_offset)));
	      src = build_va_arg_indirect_ref (src_addr);

	      dest_addr = fold_convert (addr_type, addr);
	      dest_addr = fold (build2 (PLUS_EXPR, addr_type, dest_addr,
					size_int (INTVAL (XEXP (slot, 1)))));
	      dest = build_va_arg_indirect_ref (dest_addr);

	      t = build2 (MODIFY_EXPR, void_type_node, dest, src);
	      gimplify_and_add (t, pre_p);
	    }
	}

      if (needed_intregs)
	{
	  t = build2 (PLUS_EXPR, TREE_TYPE (gpr), gpr,
		      build_int_cst (TREE_TYPE (gpr), needed_intregs * 8));
	  t = build2 (MODIFY_EXPR, TREE_TYPE (gpr), gpr, t);
	  gimplify_and_add (t, pre_p);
	}
      if (needed_sseregs)
	{
	  t = build2 (PLUS_EXPR, TREE_TYPE (fpr), fpr,
		      build_int_cst (TREE_TYPE (fpr), needed_sseregs * 16));
	  t = build2 (MODIFY_EXPR, TREE_TYPE (fpr), fpr, t);
	  gimplify_and_add (t, pre_p);
	}

      t = build1 (GOTO_EXPR, void_type_node, lab_over);
      gimplify_and_add (t, pre_p);

      t = build1 (LABEL_EXPR, void_type_node, lab_false);
      append_to_statement_list (t, pre_p);
    }

  /* ... otherwise out of the overflow area.  */

  /* Care for on-stack alignment if needed.  */
  if (FUNCTION_ARG_BOUNDARY (VOIDmode, type) <= 64
      || integer_zerop (TYPE_SIZE (type)))
    t = ovf;
  else
    {
      HOST_WIDE_INT align = FUNCTION_ARG_BOUNDARY (VOIDmode, type) / 8;
      t = build2 (PLUS_EXPR, TREE_TYPE (ovf), ovf,
		  build_int_cst (TREE_TYPE (ovf), align - 1));
      t = build2 (BIT_AND_EXPR, TREE_TYPE (t), t,
		  build_int_cst (TREE_TYPE (t), -align));
    }
  gimplify_expr (&t, pre_p, NULL, is_gimple_val, fb_rvalue);

  t2 = build2 (MODIFY_EXPR, void_type_node, addr, t);
  gimplify_and_add (t2, pre_p);

  t = build2 (PLUS_EXPR, TREE_TYPE (t), t,
	      build_int_cst (TREE_TYPE (t), rsize * UNITS_PER_WORD));
  t = build2 (MODIFY_EXPR, TREE_TYPE (ovf), ovf, t);
  gimplify_and_add (t, pre_p);

  if (container)
    {
      t = build1 (LABEL_EXPR, void_type_node, lab_over);
      append_to_statement_list (t, pre_p);
    }

  ptrtype = build_pointer_type (type);
  addr = fold_convert (ptrtype, addr);

  if (indirect_p)
    addr = build_va_arg_indirect_ref (addr);
  return build_va_arg_indirect_ref (addr);
}

/* Return nonzero if OPNUM's MEM should be matched
   in movabs* patterns.  */

int
ix86_check_movabs (rtx insn, int opnum)
{
  rtx set, mem;

  set = PATTERN (insn);
  if (GET_CODE (set) == PARALLEL)
    set = XVECEXP (set, 0, 0);
  gcc_assert (GET_CODE (set) == SET);
  mem = XEXP (set, opnum);
  while (GET_CODE (mem) == SUBREG)
    mem = SUBREG_REG (mem);
  gcc_assert (GET_CODE (mem) == MEM);
  return (volatile_ok || !MEM_VOLATILE_P (mem));
}

/* Initialize the table of extra 80387 mathematical constants.  */

static void
init_ext_80387_constants (void)
{
  static const char * cst[5] =
  {
    "0.3010299956639811952256464283594894482",  /* 0: fldlg2  */
    "0.6931471805599453094286904741849753009",  /* 1: fldln2  */
    "1.4426950408889634073876517827983434472",  /* 2: fldl2e  */
    "3.3219280948873623478083405569094566090",  /* 3: fldl2t  */
    "3.1415926535897932385128089594061862044",  /* 4: fldpi   */
  };
  int i;

  for (i = 0; i < 5; i++)
    {
      real_from_string (&ext_80387_constants_table[i], cst[i]);
      /* Ensure each constant is rounded to XFmode precision.  */
      real_convert (&ext_80387_constants_table[i],
		    XFmode, &ext_80387_constants_table[i]);
    }

  ext_80387_constants_init = 1;
}

/* Return true if the constant is something that can be loaded with
   a special instruction.  */

int
standard_80387_constant_p (rtx x)
{
  if (GET_CODE (x) != CONST_DOUBLE || !FLOAT_MODE_P (GET_MODE (x)))
    return -1;

  if (x == CONST0_RTX (GET_MODE (x)))
    return 1;
  if (x == CONST1_RTX (GET_MODE (x)))
    return 2;

  /* For XFmode constants, try to find a special 80387 instruction when
     optimizing for size or on those CPUs that benefit from them.  */
  if (GET_MODE (x) == XFmode
      && (optimize_size || x86_ext_80387_constants & TUNEMASK))
    {
      REAL_VALUE_TYPE r;
      int i;

      if (! ext_80387_constants_init)
	init_ext_80387_constants ();

      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
      for (i = 0; i < 5; i++)
        if (real_identical (&r, &ext_80387_constants_table[i]))
	  return i + 3;
    }

  return 0;
}

/* Return the opcode of the special instruction to be used to load
   the constant X.  */

const char *
standard_80387_constant_opcode (rtx x)
{
  switch (standard_80387_constant_p (x))
    {
    case 1:
      return "fldz";
    case 2:
      return "fld1";
    case 3:
      return "fldlg2";
    case 4:
      return "fldln2";
    case 5:
      return "fldl2e";
    case 6:
      return "fldl2t";
    case 7:
      return "fldpi";
    default:
      gcc_unreachable ();
    }
}

/* Return the CONST_DOUBLE representing the 80387 constant that is
   loaded by the specified special instruction.  The argument IDX
   matches the return value from standard_80387_constant_p.  */

rtx
standard_80387_constant_rtx (int idx)
{
  int i;

  if (! ext_80387_constants_init)
    init_ext_80387_constants ();

  switch (idx)
    {
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
      i = idx - 3;
      break;

    default:
      gcc_unreachable ();
    }

  return CONST_DOUBLE_FROM_REAL_VALUE (ext_80387_constants_table[i],
				       XFmode);
}

/* Return 1 if mode is a valid mode for sse.  */
static int
standard_sse_mode_p (enum machine_mode mode)
{
  switch (mode)
    {
    case V16QImode:
    case V8HImode:
    case V4SImode:
    case V2DImode:
    case V4SFmode:
    case V2DFmode:
      return 1;

    default:
      return 0;
    }
}

/* Return 1 if X is FP constant we can load to SSE register w/o using memory.
 */
int
standard_sse_constant_p (rtx x)
{
  enum machine_mode mode = GET_MODE (x);

  if (x == const0_rtx || x == CONST0_RTX (GET_MODE (x)))
    return 1;
  if (vector_all_ones_operand (x, mode)
      && standard_sse_mode_p (mode))
    return TARGET_SSE2 ? 2 : -1;

  return 0;
}

/* Return the opcode of the special instruction to be used to load
   the constant X.  */

const char *
standard_sse_constant_opcode (rtx insn, rtx x)
{
  switch (standard_sse_constant_p (x))
    {
    case 1:
      if (get_attr_mode (insn) == MODE_V4SF)
        return "xorps\t%0, %0";
      else if (get_attr_mode (insn) == MODE_V2DF)
        return "xorpd\t%0, %0";
      else
        return "pxor\t%0, %0";
    case 2:
      return "pcmpeqd\t%0, %0";
    }
  gcc_unreachable ();
}

int
cmpxchg8b_mem_constraint (rtx op)
{
  struct ix86_address parts;

  if (TARGET_64BIT || !flag_pic)
    return 1;

  if (GET_CODE (op) != MEM)
    return 0;
  if (!ix86_decompose_address (XEXP (op, 0), &parts))
    return 0;

  if (parts.base && GET_CODE (parts.base) == SUBREG)
    parts.base = SUBREG_REG (parts.base);
  if (parts.index && GET_CODE (parts.index) == SUBREG)
    parts.index = SUBREG_REG (parts.index);

  if (parts.base && REG_P (parts.base)
      && REGNO_REG_CLASS (REGNO (parts.base)) == BREG)
    return 0;
  if (parts.index && REG_P (parts.index)
      && REGNO_REG_CLASS (REGNO (parts.index)) == BREG)
    return 0;

  return 1;
}

/* Returns 1 if OP contains a symbol reference */

int
symbolic_reference_mentioned_p (rtx op)
{
  const char *fmt;
  int i;

  if (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == LABEL_REF)
    return 1;

  fmt = GET_RTX_FORMAT (GET_CODE (op));
  for (i = GET_RTX_LENGTH (GET_CODE (op)) - 1; i >= 0; i--)
    {
      if (fmt[i] == 'E')
	{
	  int j;

	  for (j = XVECLEN (op, i) - 1; j >= 0; j--)
	    if (symbolic_reference_mentioned_p (XVECEXP (op, i, j)))
	      return 1;
	}

      else if (fmt[i] == 'e' && symbolic_reference_mentioned_p (XEXP (op, i)))
	return 1;
    }

  return 0;
}

/* Return 1 if it is appropriate to emit `ret' instructions in the
   body of a function.  Do this only if the epilogue is simple, needing a
   couple of insns.  Prior to reloading, we can't tell how many registers
   must be saved, so return 0 then.  Return 0 if there is no frame
   marker to de-allocate.  */

int
ix86_can_use_return_insn_p (void)
{
  struct ix86_frame frame;

  if (! reload_completed || frame_pointer_needed)
    return 0;

  /* Don't allow more than 32 pop, since that's all we can do
     with one instruction.  */
  if (current_function_pops_args
      && current_function_args_size >= 32768)
    return 0;

  ix86_compute_frame_layout (&frame);
  return frame.to_allocate == 0 && frame.nmsave_args == 0 && frame.nregs == 0;
}

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may
   be accessed via the stack pointer) in functions that seem suitable.  */

int
ix86_frame_pointer_required (void)
{
  /* If we accessed previous frames, then the generated code expects
     to be able to access the saved ebp value in our frame.  */
  if (cfun->machine->accesses_prev_frame)
    return 1;

  /* Several x86 os'es need a frame pointer for other reasons,
     usually pertaining to setjmp.  */
  if (SUBTARGET_FRAME_POINTER_REQUIRED)
    return 1;

  if (TARGET_SAVE_ARGS)
    return 1;

  /* In override_options, TARGET_OMIT_LEAF_FRAME_POINTER turns off
     the frame pointer by default.  Turn it back on now if we've not
     got a leaf function.  */
  if (TARGET_OMIT_LEAF_FRAME_POINTER
      && (!current_function_is_leaf
	  || ix86_current_function_calls_tls_descriptor))
    return 1;

  if (current_function_profile)
    return 1;

  return 0;
}

/* Record that the current function accesses previous call frames.  */

void
ix86_setup_frame_addresses (void)
{
  cfun->machine->accesses_prev_frame = 1;
}

#if (defined(HAVE_GAS_HIDDEN) && (SUPPORTS_ONE_ONLY - 0)) || TARGET_MACHO
# define USE_HIDDEN_LINKONCE 1
#else
# define USE_HIDDEN_LINKONCE 0
#endif

static int pic_labels_used;

/* Fills in the label name that should be used for a pc thunk for
   the given register.  */

static void
get_pc_thunk_name (char name[32], unsigned int regno)
{
  gcc_assert (!TARGET_64BIT);

  if (USE_HIDDEN_LINKONCE)
    sprintf (name, "__i686.get_pc_thunk.%s", reg_names[regno]);
  else
    ASM_GENERATE_INTERNAL_LABEL (name, "LPR", regno);
}


/* This function generates code for -fpic that loads %ebx with
   the return address of the caller and then returns.  */

void
ix86_file_end (void)
{
  rtx xops[2];
  int regno;

  for (regno = 0; regno < 8; ++regno)
    {
      char name[32];

      if (! ((pic_labels_used >> regno) & 1))
	continue;

      get_pc_thunk_name (name, regno);

#if TARGET_MACHO
      if (TARGET_MACHO)
	{
	  switch_to_section (darwin_sections[text_coal_section]);
	  fputs ("\t.weak_definition\t", asm_out_file);
	  assemble_name (asm_out_file, name);
	  fputs ("\n\t.private_extern\t", asm_out_file);
	  assemble_name (asm_out_file, name);
	  fputs ("\n", asm_out_file);
	  ASM_OUTPUT_LABEL (asm_out_file, name);
	}
      else
#endif
      if (USE_HIDDEN_LINKONCE)
	{
	  tree decl;

	  decl = build_decl (FUNCTION_DECL, get_identifier (name),
			     error_mark_node);
	  TREE_PUBLIC (decl) = 1;
	  TREE_STATIC (decl) = 1;
	  DECL_ONE_ONLY (decl) = 1;

	  (*targetm.asm_out.unique_section) (decl, 0);
	  switch_to_section (get_named_section (decl, NULL, 0));

	  (*targetm.asm_out.globalize_label) (asm_out_file, name);
	  fputs ("\t.hidden\t", asm_out_file);
	  assemble_name (asm_out_file, name);
	  fputc ('\n', asm_out_file);
	  ASM_DECLARE_FUNCTION_NAME (asm_out_file, name, decl);
	}
      else
	{
	  switch_to_section (text_section);
	  ASM_OUTPUT_LABEL (asm_out_file, name);
	}

      xops[0] = gen_rtx_REG (SImode, regno);
      xops[1] = gen_rtx_MEM (SImode, stack_pointer_rtx);
      output_asm_insn ("mov{l}\t{%1, %0|%0, %1}", xops);
      output_asm_insn ("ret", xops);
    }

  if (NEED_INDICATE_EXEC_STACK)
    file_end_indicate_exec_stack ();
}

/* Emit code for the SET_GOT patterns.  */

const char *
output_set_got (rtx dest, rtx label ATTRIBUTE_UNUSED)
{
  rtx xops[3];

  xops[0] = dest;
  xops[1] = gen_rtx_SYMBOL_REF (Pmode, GOT_SYMBOL_NAME);

  if (! TARGET_DEEP_BRANCH_PREDICTION || !flag_pic)
    {
      xops[2] = gen_rtx_LABEL_REF (Pmode, label ? label : gen_label_rtx ());

      if (!flag_pic)
	output_asm_insn ("mov{l}\t{%2, %0|%0, %2}", xops);
      else
	output_asm_insn ("call\t%a2", xops);

#if TARGET_MACHO
      /* Output the Mach-O "canonical" label name ("Lxx$pb") here too.  This
         is what will be referenced by the Mach-O PIC subsystem.  */
      if (!label)
	ASM_OUTPUT_LABEL (asm_out_file, machopic_function_base_name ());
#endif

      (*targetm.asm_out.internal_label) (asm_out_file, "L",
				 CODE_LABEL_NUMBER (XEXP (xops[2], 0)));

      if (flag_pic)
	output_asm_insn ("pop{l}\t%0", xops);
    }
  else
    {
      char name[32];
      get_pc_thunk_name (name, REGNO (dest));
      pic_labels_used |= 1 << REGNO (dest);

      xops[2] = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (name));
      xops[2] = gen_rtx_MEM (QImode, xops[2]);
      output_asm_insn ("call\t%X2", xops);
      /* Output the Mach-O "canonical" label name ("Lxx$pb") here too.  This
         is what will be referenced by the Mach-O PIC subsystem.  */
#if TARGET_MACHO
      if (!label)
	ASM_OUTPUT_LABEL (asm_out_file, machopic_function_base_name ());
      else
        targetm.asm_out.internal_label (asm_out_file, "L",
					   CODE_LABEL_NUMBER (label));
#endif
    }

  if (TARGET_MACHO)
    return "";

  if (!flag_pic || TARGET_DEEP_BRANCH_PREDICTION)
    output_asm_insn ("add{l}\t{%1, %0|%0, %1}", xops);
  else
    output_asm_insn ("add{l}\t{%1+[.-%a2], %0|%0, %1+(.-%a2)}", xops);

  return "";
}

/* Generate an "push" pattern for input ARG.  */

static rtx
gen_push (rtx arg)
{
  return gen_rtx_SET (VOIDmode,
		      gen_rtx_MEM (Pmode,
				   gen_rtx_PRE_DEC (Pmode,
						    stack_pointer_rtx)),
		      arg);
}

/* Return >= 0 if there is an unused call-clobbered register available
   for the entire function.  */

static unsigned int
ix86_select_alt_pic_regnum (void)
{
  if (current_function_is_leaf && !current_function_profile
      && !ix86_current_function_calls_tls_descriptor)
    {
      int i;
      for (i = 2; i >= 0; --i)
        if (!regs_ever_live[i])
	  return i;
    }

  return INVALID_REGNUM;
}

/* Return 1 if we need to save REGNO.  */
static int
ix86_save_reg (unsigned int regno, int maybe_eh_return)
{
  if (pic_offset_table_rtx
      && regno == REAL_PIC_OFFSET_TABLE_REGNUM
      && (regs_ever_live[REAL_PIC_OFFSET_TABLE_REGNUM]
	  || current_function_profile
	  || current_function_calls_eh_return
	  || current_function_uses_const_pool))
    {
      if (ix86_select_alt_pic_regnum () != INVALID_REGNUM)
	return 0;
      return 1;
    }

  if (current_function_calls_eh_return && maybe_eh_return)
    {
      unsigned i;
      for (i = 0; ; i++)
	{
	  unsigned test = EH_RETURN_DATA_REGNO (i);
	  if (test == INVALID_REGNUM)
	    break;
	  if (test == regno)
	    return 1;
	}
    }

  if (cfun->machine->force_align_arg_pointer
      && regno == REGNO (cfun->machine->force_align_arg_pointer))
    return 1;

  return (regs_ever_live[regno]
	  && !call_used_regs[regno]
	  && !fixed_regs[regno]
	  && (regno != HARD_FRAME_POINTER_REGNUM || !frame_pointer_needed));
}

/* Return number of registers to be saved on the stack.  */

static int
ix86_nsaved_regs (void)
{
  int nregs = 0;
  int regno;

  for (regno = FIRST_PSEUDO_REGISTER - 1; regno >= 0; regno--)
    if (ix86_save_reg (regno, true))
      nregs++;
  return nregs;
}

/* Return number of arguments to be saved on the stack with
   -msave-args.  */

static int
ix86_nsaved_args (void)
{
  if (TARGET_SAVE_ARGS)
    return current_function_args_info.regno - current_function_returns_struct;
  else
    return 0;
}

/* Return the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */

HOST_WIDE_INT
ix86_initial_elimination_offset (int from, int to)
{
  struct ix86_frame frame;
  ix86_compute_frame_layout (&frame);

  if (from == ARG_POINTER_REGNUM && to == HARD_FRAME_POINTER_REGNUM)
    return frame.hard_frame_pointer_offset;
  else if (from == FRAME_POINTER_REGNUM
	   && to == HARD_FRAME_POINTER_REGNUM)
    return frame.hard_frame_pointer_offset - frame.frame_pointer_offset;
  else
    {
      gcc_assert (to == STACK_POINTER_REGNUM);

      if (from == ARG_POINTER_REGNUM)
	return frame.stack_pointer_offset;

      gcc_assert (from == FRAME_POINTER_REGNUM);
      return frame.stack_pointer_offset - frame.frame_pointer_offset;
    }
}

/* Fill structure ix86_frame about frame of currently computed function.  */

static void
ix86_compute_frame_layout (struct ix86_frame *frame)
{
  HOST_WIDE_INT total_size;
  unsigned int stack_alignment_needed;
  HOST_WIDE_INT offset;
  unsigned int preferred_alignment;
  HOST_WIDE_INT size = get_frame_size ();

  frame->local_size = size;
  frame->nregs = ix86_nsaved_regs ();
  frame->nmsave_args = ix86_nsaved_args ();
  total_size = size;

  stack_alignment_needed = cfun->stack_alignment_needed / BITS_PER_UNIT;
  preferred_alignment = cfun->preferred_stack_boundary / BITS_PER_UNIT;

  /* During reload iteration the amount of registers saved can change.
     Recompute the value as needed.  Do not recompute when amount of registers
     didn't change as reload does multiple calls to the function and does not
     expect the decision to change within single iteration.  */
  if (!optimize_size
      && cfun->machine->use_fast_prologue_epilogue_nregs != frame->nregs)
    {
      int count = frame->nregs;

      cfun->machine->use_fast_prologue_epilogue_nregs = count;
      /* The fast prologue uses move instead of push to save registers.  This
         is significantly longer, but also executes faster as modern hardware
         can execute the moves in parallel, but can't do that for push/pop.

	 Be careful about choosing what prologue to emit:  When function takes
	 many instructions to execute we may use slow version as well as in
	 case function is known to be outside hot spot (this is known with
	 feedback only).  Weight the size of function by number of registers
	 to save as it is cheap to use one or two push instructions but very
	 slow to use many of them.  */
      if (count)
	count = (count - 1) * FAST_PROLOGUE_INSN_COUNT;
      if (cfun->function_frequency < FUNCTION_FREQUENCY_NORMAL
	  || (flag_branch_probabilities
	      && cfun->function_frequency < FUNCTION_FREQUENCY_HOT))
        cfun->machine->use_fast_prologue_epilogue = false;
      else
        cfun->machine->use_fast_prologue_epilogue
	   = !expensive_function_p (count);
    }
  if (TARGET_PROLOGUE_USING_MOVE
      && cfun->machine->use_fast_prologue_epilogue)
    frame->save_regs_using_mov = true;
  else
    frame->save_regs_using_mov = false;

  if (TARGET_SAVE_ARGS)
    {
	cfun->machine->use_fast_prologue_epilogue = true;
	frame->save_regs_using_mov = true;
    }

  /* Skip return address and saved base pointer.  */
  offset = frame_pointer_needed ? UNITS_PER_WORD * 2 : UNITS_PER_WORD;

  frame->hard_frame_pointer_offset = offset;

  /* Do some sanity checking of stack_alignment_needed and
     preferred_alignment, since i386 port is the only using those features
     that may break easily.  */

  gcc_assert (!size || stack_alignment_needed);
  gcc_assert (preferred_alignment >= STACK_BOUNDARY / BITS_PER_UNIT);
  gcc_assert (preferred_alignment <= PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT);
  gcc_assert (stack_alignment_needed
	      <= PREFERRED_STACK_BOUNDARY / BITS_PER_UNIT);

  if (stack_alignment_needed < STACK_BOUNDARY / BITS_PER_UNIT)
    stack_alignment_needed = STACK_BOUNDARY / BITS_PER_UNIT;

  /* Argument save area */
  if (TARGET_SAVE_ARGS)
    {
	offset += frame->nmsave_args * UNITS_PER_WORD;
	frame->padding0 = (frame->nmsave_args % 2) * UNITS_PER_WORD;
	offset += frame->padding0;
    }
  else
    frame->padding0 = 0;

  /* Register save area */
  offset += frame->nregs * UNITS_PER_WORD;

  /* Va-arg area */
  if (ix86_save_varrargs_registers)
    {
      offset += X86_64_VARARGS_SIZE;
      frame->va_arg_size = X86_64_VARARGS_SIZE;
    }
  else
    frame->va_arg_size = 0;

  /* Align start of frame for local function.  */
  frame->padding1 = ((offset + stack_alignment_needed - 1)
		     & -stack_alignment_needed) - offset;

  offset += frame->padding1;

  /* Frame pointer points here.  */
  frame->frame_pointer_offset = offset;

  offset += size;

  /* Add outgoing arguments area.  Can be skipped if we eliminated
     all the function calls as dead code.
     Skipping is however impossible when function calls alloca.  Alloca
     expander assumes that last current_function_outgoing_args_size
     of stack frame are unused.  */
  if (ACCUMULATE_OUTGOING_ARGS
      && (!current_function_is_leaf || current_function_calls_alloca
	  || ix86_current_function_calls_tls_descriptor))
    {
      offset += current_function_outgoing_args_size;
      frame->outgoing_arguments_size = current_function_outgoing_args_size;
    }
  else
    frame->outgoing_arguments_size = 0;

  /* Align stack boundary.  Only needed if we're calling another function
     or using alloca.  */
  if (!current_function_is_leaf || current_function_calls_alloca
      || ix86_current_function_calls_tls_descriptor)
    frame->padding2 = ((offset + preferred_alignment - 1)
		       & -preferred_alignment) - offset;
  else
    frame->padding2 = 0;

  offset += frame->padding2;

  /* We've reached end of stack frame.  */
  frame->stack_pointer_offset = offset;

  /* Size prologue needs to allocate.  */
  frame->to_allocate =
    (size + frame->padding1 + frame->padding2
     + frame->outgoing_arguments_size + frame->va_arg_size);

  if (!TARGET_SAVE_ARGS
      && ((!frame->to_allocate && frame->nregs <= 1)
	  || (TARGET_64BIT
	      && frame->to_allocate >= (HOST_WIDE_INT) 0x80000000)))
    frame->save_regs_using_mov = false;

  if (TARGET_RED_ZONE && current_function_sp_is_unchanging
      && current_function_is_leaf
      && !ix86_current_function_calls_tls_descriptor)
    {
      frame->red_zone_size = frame->to_allocate;
      if (frame->save_regs_using_mov)
      {
	  frame->red_zone_size
	    += (frame->nregs + frame->nmsave_args) * UNITS_PER_WORD;
	  frame->red_zone_size += frame->padding0;
      }
      if (frame->red_zone_size > RED_ZONE_SIZE - RED_ZONE_RESERVE)
	frame->red_zone_size = RED_ZONE_SIZE - RED_ZONE_RESERVE;
    }
  else
    frame->red_zone_size = 0;
  frame->to_allocate -= frame->red_zone_size;
  frame->stack_pointer_offset -= frame->red_zone_size;
#if 0
  fprintf (stderr, "nmsave_args: %i\n", frame->nmsave_args);
  fprintf (stderr, "padding0: %i\n", frame->padding0);
  fprintf (stderr, "nregs: %i\n", frame->nregs);
  fprintf (stderr, "size: %i\n", size);
  fprintf (stderr, "alignment1: %i\n", stack_alignment_needed);
  fprintf (stderr, "padding1: %i\n", frame->padding1);
  fprintf (stderr, "va_arg: %i\n", frame->va_arg_size);
  fprintf (stderr, "padding2: %i\n", frame->padding2);
  fprintf (stderr, "to_allocate: %i\n", frame->to_allocate);
  fprintf (stderr, "red_zone_size: %i\n", frame->red_zone_size);
  fprintf (stderr, "frame_pointer_offset: %i\n", frame->frame_pointer_offset);
  fprintf (stderr, "hard_frame_pointer_offset: %i\n",
	   frame->hard_frame_pointer_offset);
  fprintf (stderr, "stack_pointer_offset: %i\n", frame->stack_pointer_offset);
#endif
}

/* Emit code to save registers in the prologue.  */

static void
ix86_emit_save_regs (void)
{
  unsigned int regno;
  rtx insn;

  if (TARGET_SAVE_ARGS)
    {
      int i;
      int nsaved = ix86_nsaved_args ();
      int start = cfun->returns_struct;
      for (i = start; i < start + nsaved; i++)
	{
	  regno = x86_64_int_parameter_registers[i];
	  insn = emit_insn (gen_push (gen_rtx_REG (Pmode, regno)));
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
      if (nsaved % 2 != 0)
	pro_epilogue_adjust_stack (stack_pointer_rtx, stack_pointer_rtx,
				   GEN_INT (-UNITS_PER_WORD), -1);
    }

  for (regno = FIRST_PSEUDO_REGISTER; regno-- > 0; )
    if (ix86_save_reg (regno, true))
      {
	insn = emit_insn (gen_push (gen_rtx_REG (Pmode, regno)));
	RTX_FRAME_RELATED_P (insn) = 1;
      }
}

/* Emit code to save registers using MOV insns.  First register
   is restored from POINTER + OFFSET.  */
static void
ix86_emit_save_regs_using_mov (rtx pointer, HOST_WIDE_INT offset)
{
  unsigned int regno;
  rtx insn;

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (ix86_save_reg (regno, true))
      {
	insn = emit_move_insn (adjust_address (gen_rtx_MEM (Pmode, pointer),
					       Pmode, offset),
			       gen_rtx_REG (Pmode, regno));
	RTX_FRAME_RELATED_P (insn) = 1;
	offset += UNITS_PER_WORD;
      }

  if (TARGET_SAVE_ARGS)
    {
      int i;
      int nsaved = ix86_nsaved_args ();
      int start = cfun->returns_struct;
      if (nsaved % 2 != 0)
	offset += UNITS_PER_WORD;
      for (i = start + nsaved - 1; i >= start; i--)
	{
	  regno = x86_64_int_parameter_registers[i];
	  insn = emit_move_insn (adjust_address (gen_rtx_MEM (Pmode, pointer),
						 Pmode, offset),
				 gen_rtx_REG (Pmode, regno));
	  RTX_FRAME_RELATED_P (insn) = 1;
	  offset += UNITS_PER_WORD;
	}
    }

}

/* Expand prologue or epilogue stack adjustment.
   The pattern exist to put a dependency on all ebp-based memory accesses.
   STYLE should be negative if instructions should be marked as frame related,
   zero if %r11 register is live and cannot be freely used and positive
   otherwise.  */

static void
pro_epilogue_adjust_stack (rtx dest, rtx src, rtx offset, int style)
{
  rtx insn;

  if (! TARGET_64BIT)
    insn = emit_insn (gen_pro_epilogue_adjust_stack_1 (dest, src, offset));
  else if (x86_64_immediate_operand (offset, DImode))
    insn = emit_insn (gen_pro_epilogue_adjust_stack_rex64 (dest, src, offset));
  else
    {
      rtx r11;
      /* r11 is used by indirect sibcall return as well, set before the
	 epilogue and used after the epilogue.  ATM indirect sibcall
	 shouldn't be used together with huge frame sizes in one
	 function because of the frame_size check in sibcall.c.  */
      gcc_assert (style);
      r11 = gen_rtx_REG (DImode, FIRST_REX_INT_REG + 3 /* R11 */);
      insn = emit_insn (gen_rtx_SET (DImode, r11, offset));
      if (style < 0)
	RTX_FRAME_RELATED_P (insn) = 1;
      insn = emit_insn (gen_pro_epilogue_adjust_stack_rex64_2 (dest, src, r11,
							       offset));
    }
  if (style < 0)
    RTX_FRAME_RELATED_P (insn) = 1;
}

/* Handle the TARGET_INTERNAL_ARG_POINTER hook.  */

static rtx
ix86_internal_arg_pointer (void)
{
  bool has_force_align_arg_pointer =
    (0 != lookup_attribute (ix86_force_align_arg_pointer_string,
			    TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl))));
  if ((FORCE_PREFERRED_STACK_BOUNDARY_IN_MAIN
       && DECL_NAME (current_function_decl)
       && MAIN_NAME_P (DECL_NAME (current_function_decl))
       && DECL_FILE_SCOPE_P (current_function_decl))
      || ix86_force_align_arg_pointer
      || has_force_align_arg_pointer)
    {
      /* Nested functions can't realign the stack due to a register
	 conflict.  */
      if (DECL_CONTEXT (current_function_decl)
	  && TREE_CODE (DECL_CONTEXT (current_function_decl)) == FUNCTION_DECL)
	{
	  if (ix86_force_align_arg_pointer)
	    warning (0, "-mstackrealign ignored for nested functions");
	  if (has_force_align_arg_pointer)
	    error ("%s not supported for nested functions",
		   ix86_force_align_arg_pointer_string);
	  return virtual_incoming_args_rtx;
	}
      cfun->machine->force_align_arg_pointer = gen_rtx_REG (Pmode, 2);
      return copy_to_reg (cfun->machine->force_align_arg_pointer);
    }
  else
    return virtual_incoming_args_rtx;
}

/* Handle the TARGET_DWARF_HANDLE_FRAME_UNSPEC hook.
   This is called from dwarf2out.c to emit call frame instructions
   for frame-related insns containing UNSPECs and UNSPEC_VOLATILEs. */
static void
ix86_dwarf_handle_frame_unspec (const char *label, rtx pattern, int index)
{
  rtx unspec = SET_SRC (pattern);
  gcc_assert (GET_CODE (unspec) == UNSPEC);

  switch (index)
    {
    case UNSPEC_REG_SAVE:
      dwarf2out_reg_save_reg (label, XVECEXP (unspec, 0, 0),
			      SET_DEST (pattern));
      break;
    case UNSPEC_DEF_CFA:
      dwarf2out_def_cfa (label, REGNO (SET_DEST (pattern)),
			 INTVAL (XVECEXP (unspec, 0, 0)));
      break;
    default:
      gcc_unreachable ();
    }
}

/* Expand the prologue into a bunch of separate insns.  */

void
ix86_expand_prologue (void)
{
  rtx insn;
  bool pic_reg_used;
  struct ix86_frame frame;
  HOST_WIDE_INT allocate;

  ix86_compute_frame_layout (&frame);

  if (cfun->machine->force_align_arg_pointer)
    {
      rtx x, y;

      /* Grab the argument pointer.  */
      x = plus_constant (stack_pointer_rtx, 4);
      y = cfun->machine->force_align_arg_pointer;
      insn = emit_insn (gen_rtx_SET (VOIDmode, y, x));
      RTX_FRAME_RELATED_P (insn) = 1;

      /* The unwind info consists of two parts: install the fafp as the cfa,
	 and record the fafp as the "save register" of the stack pointer.
	 The later is there in order that the unwinder can see where it
	 should restore the stack pointer across the and insn.  */
      x = gen_rtx_UNSPEC (VOIDmode, gen_rtvec (1, const0_rtx), UNSPEC_DEF_CFA);
      x = gen_rtx_SET (VOIDmode, y, x);
      RTX_FRAME_RELATED_P (x) = 1;
      y = gen_rtx_UNSPEC (VOIDmode, gen_rtvec (1, stack_pointer_rtx),
			  UNSPEC_REG_SAVE);
      y = gen_rtx_SET (VOIDmode, cfun->machine->force_align_arg_pointer, y);
      RTX_FRAME_RELATED_P (y) = 1;
      x = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, x, y));
      x = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, x, NULL);
      REG_NOTES (insn) = x;

      /* Align the stack.  */
      emit_insn (gen_andsi3 (stack_pointer_rtx, stack_pointer_rtx,
			     GEN_INT (-16)));

      /* And here we cheat like madmen with the unwind info.  We force the
	 cfa register back to sp+4, which is exactly what it was at the
	 start of the function.  Re-pushing the return address results in
	 the return at the same spot relative to the cfa, and thus is
	 correct wrt the unwind info.  */
      x = cfun->machine->force_align_arg_pointer;
      x = gen_frame_mem (Pmode, plus_constant (x, -4));
      insn = emit_insn (gen_push (x));
      RTX_FRAME_RELATED_P (insn) = 1;

      x = GEN_INT (4);
      x = gen_rtx_UNSPEC (VOIDmode, gen_rtvec (1, x), UNSPEC_DEF_CFA);
      x = gen_rtx_SET (VOIDmode, stack_pointer_rtx, x);
      x = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, x, NULL);
      REG_NOTES (insn) = x;
    }

  if (warn_stack_larger_than && frame.local_size > stack_larger_than_size)
    warning (0, "stack usage is %d bytes", frame.local_size); 

  /* Note: AT&T enter does NOT have reversed args.  Enter is probably
     slower on all targets.  Also sdb doesn't like it.  */

  if (frame_pointer_needed)
    {
      insn = emit_insn (gen_push (hard_frame_pointer_rtx));
      RTX_FRAME_RELATED_P (insn) = 1;

      insn = emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  allocate = frame.to_allocate;

  if (!frame.save_regs_using_mov)
    ix86_emit_save_regs ();
  else
    allocate += (frame.nregs + frame.nmsave_args) * UNITS_PER_WORD
      + frame.padding0;

  /* When using red zone we may start register saving before allocating
     the stack frame saving one cycle of the prologue.  */
  if (TARGET_RED_ZONE && frame.save_regs_using_mov)
    ix86_emit_save_regs_using_mov (frame_pointer_needed ? hard_frame_pointer_rtx
				   : stack_pointer_rtx,
				   -(frame.nregs + frame.nmsave_args)
				    * UNITS_PER_WORD - frame.padding0);

  if (allocate == 0)
    ;
  else if (! TARGET_STACK_PROBE || allocate < CHECK_STACK_LIMIT)
    pro_epilogue_adjust_stack (stack_pointer_rtx, stack_pointer_rtx,
			       GEN_INT (-allocate), -1);
  else
    {
      /* Only valid for Win32.  */
      rtx eax = gen_rtx_REG (SImode, 0);
      bool eax_live = ix86_eax_live_at_start_p ();
      rtx t;

      gcc_assert (!TARGET_64BIT);

      if (eax_live)
	{
	  emit_insn (gen_push (eax));
	  allocate -= 4;
	}

      emit_move_insn (eax, GEN_INT (allocate));

      insn = emit_insn (gen_allocate_stack_worker (eax));
      RTX_FRAME_RELATED_P (insn) = 1;
      t = gen_rtx_PLUS (Pmode, stack_pointer_rtx, GEN_INT (-allocate));
      t = gen_rtx_SET (VOIDmode, stack_pointer_rtx, t);
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR,
					    t, REG_NOTES (insn));

      if (eax_live)
	{
	  if (frame_pointer_needed)
	    t = plus_constant (hard_frame_pointer_rtx,
			       allocate
			       - frame.to_allocate
			       - (frame.nregs + frame.nmsave_args)
				 * UNITS_PER_WORD - frame.padding0);
	  else
	    t = plus_constant (stack_pointer_rtx, allocate);
	  emit_move_insn (eax, gen_rtx_MEM (SImode, t));
	}
    }

  if (frame.save_regs_using_mov && !TARGET_RED_ZONE)
    {
      if (!TARGET_SAVE_ARGS &&
       (!frame_pointer_needed || !frame.to_allocate))
        ix86_emit_save_regs_using_mov (stack_pointer_rtx, frame.to_allocate);
      else
        ix86_emit_save_regs_using_mov (hard_frame_pointer_rtx,
				       -(frame.nregs + frame.nmsave_args)
					* UNITS_PER_WORD - frame.padding0);
    }

  pic_reg_used = false;
  if (pic_offset_table_rtx
      && (regs_ever_live[REAL_PIC_OFFSET_TABLE_REGNUM]
	  || current_function_profile))
    {
      unsigned int alt_pic_reg_used = ix86_select_alt_pic_regnum ();

      if (alt_pic_reg_used != INVALID_REGNUM)
	REGNO (pic_offset_table_rtx) = alt_pic_reg_used;

      pic_reg_used = true;
    }

  if (pic_reg_used)
    {
      if (TARGET_64BIT)
        insn = emit_insn (gen_set_got_rex64 (pic_offset_table_rtx));
      else
        insn = emit_insn (gen_set_got (pic_offset_table_rtx));

      /* Even with accurate pre-reload life analysis, we can wind up
	 deleting all references to the pic register after reload.
	 Consider if cross-jumping unifies two sides of a branch
	 controlled by a comparison vs the only read from a global.
	 In which case, allow the set_got to be deleted, though we're
	 too late to do anything about the ebx save in the prologue.  */
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_MAYBE_DEAD, const0_rtx, NULL);
    }

  /* Prevent function calls from be scheduled before the call to mcount.
     In the pic_reg_used case, make sure that the got load isn't deleted.  */
  if (current_function_profile)
    emit_insn (gen_blockage (pic_reg_used ? pic_offset_table_rtx : const0_rtx));
}

/* Emit code to restore saved registers using MOV insns.  First register
   is restored from POINTER + OFFSET.  */
static void
ix86_emit_restore_regs_using_mov (rtx pointer, HOST_WIDE_INT offset,
				  int maybe_eh_return)
{
  int regno;
  rtx base_address = gen_rtx_MEM (Pmode, pointer);

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (ix86_save_reg (regno, maybe_eh_return))
      {
	/* Ensure that adjust_address won't be forced to produce pointer
	   out of range allowed by x86-64 instruction set.  */
	if (TARGET_64BIT && offset != trunc_int_for_mode (offset, SImode))
	  {
	    rtx r11;

	    r11 = gen_rtx_REG (DImode, FIRST_REX_INT_REG + 3 /* R11 */);
	    emit_move_insn (r11, GEN_INT (offset));
	    emit_insn (gen_adddi3 (r11, r11, pointer));
	    base_address = gen_rtx_MEM (Pmode, r11);
	    offset = 0;
	  }
	emit_move_insn (gen_rtx_REG (Pmode, regno),
			adjust_address (base_address, Pmode, offset));
	offset += UNITS_PER_WORD;
      }
}

/* Restore function stack, frame, and registers.  */

void
ix86_expand_epilogue (int style)
{
  int regno;
  int sp_valid = !frame_pointer_needed || current_function_sp_is_unchanging;
  struct ix86_frame frame;
  HOST_WIDE_INT offset;

  ix86_compute_frame_layout (&frame);

  /* Calculate start of saved registers relative to ebp.  Special care
     must be taken for the normal return case of a function using
     eh_return: the eax and edx registers are marked as saved, but not
     restored along this path.  */
  offset = frame.nregs + frame.nmsave_args;
  if (current_function_calls_eh_return && style != 2)
    offset -= 2;
  offset *= -UNITS_PER_WORD;
  offset -= frame.padding0;

  /* If we're only restoring one register and sp is not valid then
     using a move instruction to restore the register since it's
     less work than reloading sp and popping the register.

     The default code result in stack adjustment using add/lea instruction,
     while this code results in LEAVE instruction (or discrete equivalent),
     so it is profitable in some other cases as well.  Especially when there
     are no registers to restore.  We also use this code when TARGET_USE_LEAVE
     and there is exactly one register to pop. This heuristic may need some
     tuning in future.  */
  if ((!sp_valid && frame.nregs <= 1)
      || (TARGET_EPILOGUE_USING_MOVE
	  && cfun->machine->use_fast_prologue_epilogue
	  && (frame.nregs > 1 || frame.to_allocate))
      || (frame_pointer_needed && !frame.nregs && frame.to_allocate)
      || (frame_pointer_needed && TARGET_USE_LEAVE
	  && cfun->machine->use_fast_prologue_epilogue
	  && frame.nregs == 1)
      || current_function_calls_eh_return)
    {
      /* Restore registers.  We can use ebp or esp to address the memory
	 locations.  If both are available, default to ebp, since offsets
	 are known to be small.  Only exception is esp pointing directly to the
	 end of block of saved registers, where we may simplify addressing
	 mode.  */

      if (!frame_pointer_needed || (sp_valid && !frame.to_allocate))
	ix86_emit_restore_regs_using_mov (stack_pointer_rtx,
					  frame.to_allocate, style == 2);
      else
	ix86_emit_restore_regs_using_mov (hard_frame_pointer_rtx,
					  offset, style == 2);

      /* eh_return epilogues need %ecx added to the stack pointer.  */
      if (style == 2)
	{
	  rtx tmp, sa = EH_RETURN_STACKADJ_RTX;

	  if (frame_pointer_needed)
	    {
	      tmp = gen_rtx_PLUS (Pmode, hard_frame_pointer_rtx, sa);
	      tmp = plus_constant (tmp, UNITS_PER_WORD);
	      emit_insn (gen_rtx_SET (VOIDmode, sa, tmp));

	      tmp = gen_rtx_MEM (Pmode, hard_frame_pointer_rtx);
	      emit_move_insn (hard_frame_pointer_rtx, tmp);

	      pro_epilogue_adjust_stack (stack_pointer_rtx, sa,
					 const0_rtx, style);
	    }
	  else
	    {
	      tmp = gen_rtx_PLUS (Pmode, stack_pointer_rtx, sa);
	      tmp = plus_constant (tmp, (frame.to_allocate
                                         + (frame.nregs + frame.nmsave_args)
					   * UNITS_PER_WORD + frame.padding0));
	      emit_insn (gen_rtx_SET (VOIDmode, stack_pointer_rtx, tmp));
	    }
	}
      else if (!frame_pointer_needed)
	pro_epilogue_adjust_stack (stack_pointer_rtx, stack_pointer_rtx,
				   GEN_INT (frame.to_allocate
					    + (frame.nregs + frame.nmsave_args)
					     * UNITS_PER_WORD + frame.padding0),
				   style);
      /* If not an i386, mov & pop is faster than "leave".  */
      else if (TARGET_USE_LEAVE || optimize_size
	       || !cfun->machine->use_fast_prologue_epilogue)
	emit_insn (TARGET_64BIT ? gen_leave_rex64 () : gen_leave ());
      else
	{
	  pro_epilogue_adjust_stack (stack_pointer_rtx,
				     hard_frame_pointer_rtx,
				     const0_rtx, style);
	  if (TARGET_64BIT)
	    emit_insn (gen_popdi1 (hard_frame_pointer_rtx));
	  else
	    emit_insn (gen_popsi1 (hard_frame_pointer_rtx));
	}
    }
  else
    {
      /* First step is to deallocate the stack frame so that we can
	 pop the registers.  */
      if (!sp_valid)
	{
	  gcc_assert (frame_pointer_needed);
	  pro_epilogue_adjust_stack (stack_pointer_rtx,
				     hard_frame_pointer_rtx,
				     GEN_INT (offset), style);
	}
      else if (frame.to_allocate)
	pro_epilogue_adjust_stack (stack_pointer_rtx, stack_pointer_rtx,
				   GEN_INT (frame.to_allocate), style);

      for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
	if (ix86_save_reg (regno, false))
	  {
	    if (TARGET_64BIT)
	      emit_insn (gen_popdi1 (gen_rtx_REG (Pmode, regno)));
	    else
	      emit_insn (gen_popsi1 (gen_rtx_REG (Pmode, regno)));
	  }
      if (frame.nmsave_args)
        pro_epilogue_adjust_stack (stack_pointer_rtx, stack_pointer_rtx,
				 GEN_INT (frame.nmsave_args * UNITS_PER_WORD
					  + frame.padding0), style);
      if (frame_pointer_needed)
	{
	  /* Leave results in shorter dependency chains on CPUs that are
	     able to grok it fast.  */
	  if (TARGET_USE_LEAVE)
	    emit_insn (TARGET_64BIT ? gen_leave_rex64 () : gen_leave ());
	  else if (TARGET_64BIT)
	    emit_insn (gen_popdi1 (hard_frame_pointer_rtx));
	  else
	    emit_insn (gen_popsi1 (hard_frame_pointer_rtx));
	}
    }

  if (cfun->machine->force_align_arg_pointer)
    {
      emit_insn (gen_addsi3 (stack_pointer_rtx,
			     cfun->machine->force_align_arg_pointer,
			     GEN_INT (-4)));
    }

  /* Sibcall epilogues don't want a return instruction.  */
  if (style == 0)
    return;

  if (current_function_pops_args && current_function_args_size)
    {
      rtx popc = GEN_INT (current_function_pops_args);

      /* i386 can only pop 64K bytes.  If asked to pop more, pop
	 return address, do explicit add, and jump indirectly to the
	 caller.  */

      if (current_function_pops_args >= 65536)
	{
	  rtx ecx = gen_rtx_REG (SImode, 2);

	  /* There is no "pascal" calling convention in 64bit ABI.  */
	  gcc_assert (!TARGET_64BIT);

	  emit_insn (gen_popsi1 (ecx));
	  emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx, popc));
	  emit_jump_insn (gen_return_indirect_internal (ecx));
	}
      else
	emit_jump_insn (gen_return_pop_internal (popc));
    }
  else
    emit_jump_insn (gen_return_internal ());
}

/* Reset from the function's potential modifications.  */

static void
ix86_output_function_epilogue (FILE *file ATTRIBUTE_UNUSED,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  if (pic_offset_table_rtx)
    REGNO (pic_offset_table_rtx) = REAL_PIC_OFFSET_TABLE_REGNUM;
#if TARGET_MACHO
  /* Mach-O doesn't support labels at the end of objects, so if
     it looks like we might want one, insert a NOP.  */
  {
    rtx insn = get_last_insn ();
    while (insn
	   && NOTE_P (insn)
	   && NOTE_LINE_NUMBER (insn) != NOTE_INSN_DELETED_LABEL)
      insn = PREV_INSN (insn);
    if (insn
	&& (LABEL_P (insn)
	    || (NOTE_P (insn)
		&& NOTE_LINE_NUMBER (insn) == NOTE_INSN_DELETED_LABEL)))
      fputs ("\tnop\n", file);
  }
#endif

}

/* Extract the parts of an RTL expression that is a valid memory address
   for an instruction.  Return 0 if the structure of the address is
   grossly off.  Return -1 if the address contains ASHIFT, so it is not
   strictly valid, but still used for computing length of lea instruction.  */

int
ix86_decompose_address (rtx addr, struct ix86_address *out)
{
  rtx base = NULL_RTX, index = NULL_RTX, disp = NULL_RTX;
  rtx base_reg, index_reg;
  HOST_WIDE_INT scale = 1;
  rtx scale_rtx = NULL_RTX;
  int retval = 1;
  enum ix86_address_seg seg = SEG_DEFAULT;

  if (GET_CODE (addr) == REG || GET_CODE (addr) == SUBREG)
    base = addr;
  else if (GET_CODE (addr) == PLUS)
    {
      rtx addends[4], op;
      int n = 0, i;

      op = addr;
      do
	{
	  if (n >= 4)
	    return 0;
	  addends[n++] = XEXP (op, 1);
	  op = XEXP (op, 0);
	}
      while (GET_CODE (op) == PLUS);
      if (n >= 4)
	return 0;
      addends[n] = op;

      for (i = n; i >= 0; --i)
	{
	  op = addends[i];
	  switch (GET_CODE (op))
	    {
	    case MULT:
	      if (index)
		return 0;
	      index = XEXP (op, 0);
	      scale_rtx = XEXP (op, 1);
	      break;

	    case UNSPEC:
	      if (XINT (op, 1) == UNSPEC_TP
	          && TARGET_TLS_DIRECT_SEG_REFS
	          && seg == SEG_DEFAULT)
		seg = TARGET_64BIT ? SEG_FS : SEG_GS;
	      else
		return 0;
	      break;

	    case REG:
	    case SUBREG:
	      if (!base)
		base = op;
	      else if (!index)
		index = op;
	      else
		return 0;
	      break;

	    case CONST:
	    case CONST_INT:
	    case SYMBOL_REF:
	    case LABEL_REF:
	      if (disp)
		return 0;
	      disp = op;
	      break;

	    default:
	      return 0;
	    }
	}
    }
  else if (GET_CODE (addr) == MULT)
    {
      index = XEXP (addr, 0);		/* index*scale */
      scale_rtx = XEXP (addr, 1);
    }
  else if (GET_CODE (addr) == ASHIFT)
    {
      rtx tmp;

      /* We're called for lea too, which implements ashift on occasion.  */
      index = XEXP (addr, 0);
      tmp = XEXP (addr, 1);
      if (GET_CODE (tmp) != CONST_INT)
	return 0;
      scale = INTVAL (tmp);
      if ((unsigned HOST_WIDE_INT) scale > 3)
	return 0;
      scale = 1 << scale;
      retval = -1;
    }
  else
    disp = addr;			/* displacement */

  /* Extract the integral value of scale.  */
  if (scale_rtx)
    {
      if (GET_CODE (scale_rtx) != CONST_INT)
	return 0;
      scale = INTVAL (scale_rtx);
    }

  base_reg = base && GET_CODE (base) == SUBREG ? SUBREG_REG (base) : base;
  index_reg = index && GET_CODE (index) == SUBREG ? SUBREG_REG (index) : index;

  /* Allow arg pointer and stack pointer as index if there is not scaling.  */
  if (base_reg && index_reg && scale == 1
      && (index_reg == arg_pointer_rtx
	  || index_reg == frame_pointer_rtx
	  || (REG_P (index_reg) && REGNO (index_reg) == STACK_POINTER_REGNUM)))
    {
      rtx tmp;
      tmp = base, base = index, index = tmp;
      tmp = base_reg, base_reg = index_reg, index_reg = tmp;
    }

  /* Special case: %ebp cannot be encoded as a base without a displacement.  */
  if ((base_reg == hard_frame_pointer_rtx
       || base_reg == frame_pointer_rtx
       || base_reg == arg_pointer_rtx) && !disp)
    disp = const0_rtx;

  /* Special case: on K6, [%esi] makes the instruction vector decoded.
     Avoid this by transforming to [%esi+0].  */
  if (ix86_tune == PROCESSOR_K6 && !optimize_size
      && base_reg && !index_reg && !disp
      && REG_P (base_reg)
      && REGNO_REG_CLASS (REGNO (base_reg)) == SIREG)
    disp = const0_rtx;

  /* Special case: encode reg+reg instead of reg*2.  */
  if (!base && index && scale && scale == 2)
    base = index, base_reg = index_reg, scale = 1;

  /* Special case: scaling cannot be encoded without base or displacement.  */
  if (!base && !disp && index && scale != 1)
    disp = const0_rtx;

  out->base = base;
  out->index = index;
  out->disp = disp;
  out->scale = scale;
  out->seg = seg;

  return retval;
}

/* Return cost of the memory address x.
   For i386, it is better to use a complex address than let gcc copy
   the address into a reg and make a new pseudo.  But not if the address
   requires to two regs - that would mean more pseudos with longer
   lifetimes.  */
static int
ix86_address_cost (rtx x)
{
  struct ix86_address parts;
  int cost = 1;
  int ok = ix86_decompose_address (x, &parts);

  gcc_assert (ok);

  if (parts.base && GET_CODE (parts.base) == SUBREG)
    parts.base = SUBREG_REG (parts.base);
  if (parts.index && GET_CODE (parts.index) == SUBREG)
    parts.index = SUBREG_REG (parts.index);

  /* More complex memory references are better.  */
  if (parts.disp && parts.disp != const0_rtx)
    cost--;
  if (parts.seg != SEG_DEFAULT)
    cost--;

  /* Attempt to minimize number of registers in the address.  */
  if ((parts.base
       && (!REG_P (parts.base) || REGNO (parts.base) >= FIRST_PSEUDO_REGISTER))
      || (parts.index
	  && (!REG_P (parts.index)
	      || REGNO (parts.index) >= FIRST_PSEUDO_REGISTER)))
    cost++;

  if (parts.base
      && (!REG_P (parts.base) || REGNO (parts.base) >= FIRST_PSEUDO_REGISTER)
      && parts.index
      && (!REG_P (parts.index) || REGNO (parts.index) >= FIRST_PSEUDO_REGISTER)
      && parts.base != parts.index)
    cost++;

  /* AMD-K6 don't like addresses with ModR/M set to 00_xxx_100b,
     since it's predecode logic can't detect the length of instructions
     and it degenerates to vector decoded.  Increase cost of such
     addresses here.  The penalty is minimally 2 cycles.  It may be worthwhile
     to split such addresses or even refuse such addresses at all.

     Following addressing modes are affected:
      [base+scale*index]
      [scale*index+disp]
      [base+index]

     The first and last case  may be avoidable by explicitly coding the zero in
     memory address, but I don't have AMD-K6 machine handy to check this
     theory.  */

  if (TARGET_K6
      && ((!parts.disp && parts.base && parts.index && parts.scale != 1)
	  || (parts.disp && !parts.base && parts.index && parts.scale != 1)
	  || (!parts.disp && parts.base && parts.index && parts.scale == 1)))
    cost += 10;

  return cost;
}

/* If X is a machine specific address (i.e. a symbol or label being
   referenced as a displacement from the GOT implemented using an
   UNSPEC), then return the base term.  Otherwise return X.  */

rtx
ix86_find_base_term (rtx x)
{
  rtx term;

  if (TARGET_64BIT)
    {
      if (GET_CODE (x) != CONST)
	return x;
      term = XEXP (x, 0);
      if (GET_CODE (term) == PLUS
	  && (GET_CODE (XEXP (term, 1)) == CONST_INT
	      || GET_CODE (XEXP (term, 1)) == CONST_DOUBLE))
	term = XEXP (term, 0);
      if (GET_CODE (term) != UNSPEC
	  || XINT (term, 1) != UNSPEC_GOTPCREL)
	return x;

      term = XVECEXP (term, 0, 0);

      if (GET_CODE (term) != SYMBOL_REF
	  && GET_CODE (term) != LABEL_REF)
	return x;

      return term;
    }

  term = ix86_delegitimize_address (x);

  if (GET_CODE (term) != SYMBOL_REF
      && GET_CODE (term) != LABEL_REF)
    return x;

  return term;
}

/* Allow {LABEL | SYMBOL}_REF - SYMBOL_REF-FOR-PICBASE for Mach-O as
   this is used for to form addresses to local data when -fPIC is in
   use.  */

static bool
darwin_local_data_pic (rtx disp)
{
  if (GET_CODE (disp) == MINUS)
    {
      if (GET_CODE (XEXP (disp, 0)) == LABEL_REF
          || GET_CODE (XEXP (disp, 0)) == SYMBOL_REF)
        if (GET_CODE (XEXP (disp, 1)) == SYMBOL_REF)
          {
            const char *sym_name = XSTR (XEXP (disp, 1), 0);
            if (! strcmp (sym_name, "<pic base>"))
              return true;
          }
    }

  return false;
}

/* Determine if a given RTX is a valid constant.  We already know this
   satisfies CONSTANT_P.  */

bool
legitimate_constant_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST:
      x = XEXP (x, 0);

      if (GET_CODE (x) == PLUS)
	{
	  if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	    return false;
	  x = XEXP (x, 0);
	}

      if (TARGET_MACHO && darwin_local_data_pic (x))
	return true;

      /* Only some unspecs are valid as "constants".  */
      if (GET_CODE (x) == UNSPEC)
	switch (XINT (x, 1))
	  {
	  case UNSPEC_GOTOFF:
	    return TARGET_64BIT;
	  case UNSPEC_TPOFF:
	  case UNSPEC_NTPOFF:
	    x = XVECEXP (x, 0, 0);
	    return (GET_CODE (x) == SYMBOL_REF
		    && SYMBOL_REF_TLS_MODEL (x) == TLS_MODEL_LOCAL_EXEC);
	  case UNSPEC_DTPOFF:
	    x = XVECEXP (x, 0, 0);
	    return (GET_CODE (x) == SYMBOL_REF
		    && SYMBOL_REF_TLS_MODEL (x) == TLS_MODEL_LOCAL_DYNAMIC);
	  default:
	    return false;
	  }

      /* We must have drilled down to a symbol.  */
      if (GET_CODE (x) == LABEL_REF)
	return true;
      if (GET_CODE (x) != SYMBOL_REF)
	return false;
      /* FALLTHRU */

    case SYMBOL_REF:
      /* TLS symbols are never valid.  */
      if (SYMBOL_REF_TLS_MODEL (x))
	return false;
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == TImode
	  && x != CONST0_RTX (TImode)
          && !TARGET_64BIT)
	return false;
      break;

    case CONST_VECTOR:
      if (x == CONST0_RTX (GET_MODE (x)))
	return true;
      return false;

    default:
      break;
    }

  /* Otherwise we handle everything else in the move patterns.  */
  return true;
}

/* Determine if it's legal to put X into the constant pool.  This
   is not possible for the address of thread-local symbols, which
   is checked above.  */

static bool
ix86_cannot_force_const_mem (rtx x)
{
  /* We can always put integral constants and vectors in memory.  */
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
      return false;

    default:
      break;
    }
  return !legitimate_constant_p (x);
}

/* Determine if a given RTX is a valid constant address.  */

bool
constant_address_p (rtx x)
{
  return CONSTANT_P (x) && legitimate_address_p (Pmode, x, 1);
}

/* Nonzero if the constant value X is a legitimate general operand
   when generating PIC code.  It is given that flag_pic is on and
   that X satisfies CONSTANT_P or is a CONST_DOUBLE.  */

bool
legitimate_pic_operand_p (rtx x)
{
  rtx inner;

  switch (GET_CODE (x))
    {
    case CONST:
      inner = XEXP (x, 0);
      if (GET_CODE (inner) == PLUS
	  && GET_CODE (XEXP (inner, 1)) == CONST_INT)
	inner = XEXP (inner, 0);

      /* Only some unspecs are valid as "constants".  */
      if (GET_CODE (inner) == UNSPEC)
	switch (XINT (inner, 1))
	  {
	  case UNSPEC_GOTOFF:
	    return TARGET_64BIT;
	  case UNSPEC_TPOFF:
	    x = XVECEXP (inner, 0, 0);
	    return (GET_CODE (x) == SYMBOL_REF
		    && SYMBOL_REF_TLS_MODEL (x) == TLS_MODEL_LOCAL_EXEC);
	  default:
	    return false;
	  }
      /* FALLTHRU */

    case SYMBOL_REF:
    case LABEL_REF:
      return legitimate_pic_address_disp_p (x);

    default:
      return true;
    }
}

/* Determine if a given CONST RTX is a valid memory displacement
   in PIC mode.  */

int
legitimate_pic_address_disp_p (rtx disp)
{
  bool saw_plus;

  /* In 64bit mode we can allow direct addresses of symbols and labels
     when they are not dynamic symbols.  */
  if (TARGET_64BIT)
    {
      rtx op0 = disp, op1;

      switch (GET_CODE (disp))
	{
	case LABEL_REF:
	  return true;

	case CONST:
	  if (GET_CODE (XEXP (disp, 0)) != PLUS)
	    break;
	  op0 = XEXP (XEXP (disp, 0), 0);
	  op1 = XEXP (XEXP (disp, 0), 1);
	  if (GET_CODE (op1) != CONST_INT
	      || INTVAL (op1) >= 16*1024*1024
	      || INTVAL (op1) < -16*1024*1024)
            break;
	  if (GET_CODE (op0) == LABEL_REF)
	    return true;
	  if (GET_CODE (op0) != SYMBOL_REF)
	    break;
	  /* FALLTHRU */

	case SYMBOL_REF:
	  /* TLS references should always be enclosed in UNSPEC.  */
	  if (SYMBOL_REF_TLS_MODEL (op0))
	    return false;
	  if (!SYMBOL_REF_FAR_ADDR_P (op0) && SYMBOL_REF_LOCAL_P (op0))
	    return true;
	  break;

	default:
	  break;
	}
    }
  if (GET_CODE (disp) != CONST)
    return 0;
  disp = XEXP (disp, 0);

  if (TARGET_64BIT)
    {
      /* We are unsafe to allow PLUS expressions.  This limit allowed distance
         of GOT tables.  We should not need these anyway.  */
      if (GET_CODE (disp) != UNSPEC
	  || (XINT (disp, 1) != UNSPEC_GOTPCREL
	      && XINT (disp, 1) != UNSPEC_GOTOFF))
	return 0;

      if (GET_CODE (XVECEXP (disp, 0, 0)) != SYMBOL_REF
	  && GET_CODE (XVECEXP (disp, 0, 0)) != LABEL_REF)
	return 0;
      return 1;
    }

  saw_plus = false;
  if (GET_CODE (disp) == PLUS)
    {
      if (GET_CODE (XEXP (disp, 1)) != CONST_INT)
	return 0;
      disp = XEXP (disp, 0);
      saw_plus = true;
    }

  if (TARGET_MACHO && darwin_local_data_pic (disp))
    return 1;

  if (GET_CODE (disp) != UNSPEC)
    return 0;

  switch (XINT (disp, 1))
    {
    case UNSPEC_GOT:
      if (saw_plus)
	return false;
      return GET_CODE (XVECEXP (disp, 0, 0)) == SYMBOL_REF;
    case UNSPEC_GOTOFF:
      /* Refuse GOTOFF in 64bit mode since it is always 64bit when used.
	 While ABI specify also 32bit relocation but we don't produce it in
	 small PIC model at all.  */
      if ((GET_CODE (XVECEXP (disp, 0, 0)) == SYMBOL_REF
	   || GET_CODE (XVECEXP (disp, 0, 0)) == LABEL_REF)
	  && !TARGET_64BIT)
        return local_symbolic_operand (XVECEXP (disp, 0, 0), Pmode);
      return false;
    case UNSPEC_GOTTPOFF:
    case UNSPEC_GOTNTPOFF:
    case UNSPEC_INDNTPOFF:
      if (saw_plus)
	return false;
      disp = XVECEXP (disp, 0, 0);
      return (GET_CODE (disp) == SYMBOL_REF
	      && SYMBOL_REF_TLS_MODEL (disp) == TLS_MODEL_INITIAL_EXEC);
    case UNSPEC_NTPOFF:
      disp = XVECEXP (disp, 0, 0);
      return (GET_CODE (disp) == SYMBOL_REF
	      && SYMBOL_REF_TLS_MODEL (disp) == TLS_MODEL_LOCAL_EXEC);
    case UNSPEC_DTPOFF:
      disp = XVECEXP (disp, 0, 0);
      return (GET_CODE (disp) == SYMBOL_REF
	      && SYMBOL_REF_TLS_MODEL (disp) == TLS_MODEL_LOCAL_DYNAMIC);
    }

  return 0;
}

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression that is a valid
   memory address for an instruction.  The MODE argument is the machine mode
   for the MEM expression that wants to use this address.

   It only recognizes address in canonical form.  LEGITIMIZE_ADDRESS should
   convert common non-canonical forms to canonical form so that they will
   be recognized.  */

int
legitimate_address_p (enum machine_mode mode, rtx addr, int strict)
{
  struct ix86_address parts;
  rtx base, index, disp;
  HOST_WIDE_INT scale;
  const char *reason = NULL;
  rtx reason_rtx = NULL_RTX;

  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr,
	       "\n======\nGO_IF_LEGITIMATE_ADDRESS, mode = %s, strict = %d\n",
	       GET_MODE_NAME (mode), strict);
      debug_rtx (addr);
    }

  if (ix86_decompose_address (addr, &parts) <= 0)
    {
      reason = "decomposition failed";
      goto report_error;
    }

  base = parts.base;
  index = parts.index;
  disp = parts.disp;
  scale = parts.scale;

  /* Validate base register.

     Don't allow SUBREG's that span more than a word here.  It can lead to spill
     failures when the base is one word out of a two word structure, which is
     represented internally as a DImode int.  */

  if (base)
    {
      rtx reg;
      reason_rtx = base;

      if (REG_P (base))
  	reg = base;
      else if (GET_CODE (base) == SUBREG
	       && REG_P (SUBREG_REG (base))
	       && GET_MODE_SIZE (GET_MODE (SUBREG_REG (base)))
		  <= UNITS_PER_WORD)
  	reg = SUBREG_REG (base);
      else
	{
	  reason = "base is not a register";
	  goto report_error;
	}

      if (GET_MODE (base) != Pmode)
	{
	  reason = "base is not in Pmode";
	  goto report_error;
	}

      if ((strict && ! REG_OK_FOR_BASE_STRICT_P (reg))
	  || (! strict && ! REG_OK_FOR_BASE_NONSTRICT_P (reg)))
	{
	  reason = "base is not valid";
	  goto report_error;
	}
    }

  /* Validate index register.

     Don't allow SUBREG's that span more than a word here -- same as above.  */

  if (index)
    {
      rtx reg;
      reason_rtx = index;

      if (REG_P (index))
  	reg = index;
      else if (GET_CODE (index) == SUBREG
	       && REG_P (SUBREG_REG (index))
	       && GET_MODE_SIZE (GET_MODE (SUBREG_REG (index)))
		  <= UNITS_PER_WORD)
  	reg = SUBREG_REG (index);
      else
	{
	  reason = "index is not a register";
	  goto report_error;
	}

      if (GET_MODE (index) != Pmode)
	{
	  reason = "index is not in Pmode";
	  goto report_error;
	}

      if ((strict && ! REG_OK_FOR_INDEX_STRICT_P (reg))
	  || (! strict && ! REG_OK_FOR_INDEX_NONSTRICT_P (reg)))
	{
	  reason = "index is not valid";
	  goto report_error;
	}
    }

  /* Validate scale factor.  */
  if (scale != 1)
    {
      reason_rtx = GEN_INT (scale);
      if (!index)
	{
	  reason = "scale without index";
	  goto report_error;
	}

      if (scale != 2 && scale != 4 && scale != 8)
	{
	  reason = "scale is not a valid multiplier";
	  goto report_error;
	}
    }

  /* Validate displacement.  */
  if (disp)
    {
      reason_rtx = disp;

      if (GET_CODE (disp) == CONST
	  && GET_CODE (XEXP (disp, 0)) == UNSPEC)
	switch (XINT (XEXP (disp, 0), 1))
	  {
	  /* Refuse GOTOFF and GOT in 64bit mode since it is always 64bit when
	     used.  While ABI specify also 32bit relocations, we don't produce
	     them at all and use IP relative instead.  */
	  case UNSPEC_GOT:
	  case UNSPEC_GOTOFF:
	    gcc_assert (flag_pic);
	    if (!TARGET_64BIT)
	      goto is_legitimate_pic;
	    reason = "64bit address unspec";
	    goto report_error;

	  case UNSPEC_GOTPCREL:
	    gcc_assert (flag_pic);
	    goto is_legitimate_pic;

	  case UNSPEC_GOTTPOFF:
	  case UNSPEC_GOTNTPOFF:
	  case UNSPEC_INDNTPOFF:
	  case UNSPEC_NTPOFF:
	  case UNSPEC_DTPOFF:
	    break;

	  default:
	    reason = "invalid address unspec";
	    goto report_error;
	  }

      else if (SYMBOLIC_CONST (disp)
	       && (flag_pic
		   || (TARGET_MACHO
#if TARGET_MACHO
		       && MACHOPIC_INDIRECT
		       && !machopic_operand_p (disp)
#endif
	       )))
	{

	is_legitimate_pic:
	  if (TARGET_64BIT && (index || base))
	    {
	      /* foo@dtpoff(%rX) is ok.  */
	      if (GET_CODE (disp) != CONST
		  || GET_CODE (XEXP (disp, 0)) != PLUS
		  || GET_CODE (XEXP (XEXP (disp, 0), 0)) != UNSPEC
		  || GET_CODE (XEXP (XEXP (disp, 0), 1)) != CONST_INT
		  || (XINT (XEXP (XEXP (disp, 0), 0), 1) != UNSPEC_DTPOFF
		      && XINT (XEXP (XEXP (disp, 0), 0), 1) != UNSPEC_NTPOFF))
		{
		  reason = "non-constant pic memory reference";
		  goto report_error;
		}
	    }
	  else if (! legitimate_pic_address_disp_p (disp))
	    {
	      reason = "displacement is an invalid pic construct";
	      goto report_error;
	    }

          /* This code used to verify that a symbolic pic displacement
	     includes the pic_offset_table_rtx register.

	     While this is good idea, unfortunately these constructs may
	     be created by "adds using lea" optimization for incorrect
	     code like:

	     int a;
	     int foo(int i)
	       {
	         return *(&a+i);
	       }

	     This code is nonsensical, but results in addressing
	     GOT table with pic_offset_table_rtx base.  We can't
	     just refuse it easily, since it gets matched by
	     "addsi3" pattern, that later gets split to lea in the
	     case output register differs from input.  While this
	     can be handled by separate addsi pattern for this case
	     that never results in lea, this seems to be easier and
	     correct fix for crash to disable this test.  */
	}
      else if (GET_CODE (disp) != LABEL_REF
	       && GET_CODE (disp) != CONST_INT
	       && (GET_CODE (disp) != CONST
		   || !legitimate_constant_p (disp))
	       && (GET_CODE (disp) != SYMBOL_REF
		   || !legitimate_constant_p (disp)))
	{
	  reason = "displacement is not constant";
	  goto report_error;
	}
      else if (TARGET_64BIT
	       && !x86_64_immediate_operand (disp, VOIDmode))
	{
	  reason = "displacement is out of range";
	  goto report_error;
	}
    }

  /* Everything looks valid.  */
  if (TARGET_DEBUG_ADDR)
    fprintf (stderr, "Success.\n");
  return TRUE;

 report_error:
  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr, "Error: %s\n", reason);
      debug_rtx (reason_rtx);
    }
  return FALSE;
}

/* Return a unique alias set for the GOT.  */

static HOST_WIDE_INT
ix86_GOT_alias_set (void)
{
  static HOST_WIDE_INT set = -1;
  if (set == -1)
    set = new_alias_set ();
  return set;
}

/* Return a legitimate reference for ORIG (an address) using the
   register REG.  If REG is 0, a new pseudo is generated.

   There are two types of references that must be handled:

   1. Global data references must load the address from the GOT, via
      the PIC reg.  An insn is emitted to do this load, and the reg is
      returned.

   2. Static data references, constant pool addresses, and code labels
      compute the address as an offset from the GOT, whose base is in
      the PIC reg.  Static data objects have SYMBOL_FLAG_LOCAL set to
      differentiate them from global data objects.  The returned
      address is the PIC reg + an unspec constant.

   GO_IF_LEGITIMATE_ADDRESS rejects symbolic references unless the PIC
   reg also appears in the address.  */

static rtx
legitimize_pic_address (rtx orig, rtx reg)
{
  rtx addr = orig;
  rtx new = orig;
  rtx base;

#if TARGET_MACHO
  if (TARGET_MACHO && !TARGET_64BIT)
    {
      if (reg == 0)
	reg = gen_reg_rtx (Pmode);
      /* Use the generic Mach-O PIC machinery.  */
      return machopic_legitimize_pic_address (orig, GET_MODE (orig), reg);
    }
#endif

  if (TARGET_64BIT && legitimate_pic_address_disp_p (addr))
    new = addr;
  else if (TARGET_64BIT
	   && ix86_cmodel != CM_SMALL_PIC
	   && local_symbolic_operand (addr, Pmode))
    {
      rtx tmpreg;
      /* This symbol may be referenced via a displacement from the PIC
	 base address (@GOTOFF).  */

      if (reload_in_progress)
	regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;
      if (GET_CODE (addr) == CONST)
	addr = XEXP (addr, 0);
      if (GET_CODE (addr) == PLUS)
	  {
            new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, XEXP (addr, 0)), UNSPEC_GOTOFF);
	    new = gen_rtx_PLUS (Pmode, new, XEXP (addr, 1));
	  }
	else
          new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTOFF);
      new = gen_rtx_CONST (Pmode, new);
      if (!reg)
        tmpreg = gen_reg_rtx (Pmode);
      else
	tmpreg = reg;
      emit_move_insn (tmpreg, new);

      if (reg != 0)
	{
	  new = expand_simple_binop (Pmode, PLUS, reg, pic_offset_table_rtx,
				     tmpreg, 1, OPTAB_DIRECT);
	  new = reg;
	}
      else new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, tmpreg);
    }
  else if (!TARGET_64BIT && local_symbolic_operand (addr, Pmode))
    {
      /* This symbol may be referenced via a displacement from the PIC
	 base address (@GOTOFF).  */

      if (reload_in_progress)
	regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;
      if (GET_CODE (addr) == CONST)
	addr = XEXP (addr, 0);
      if (GET_CODE (addr) == PLUS)
	  {
            new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, XEXP (addr, 0)), UNSPEC_GOTOFF);
	    new = gen_rtx_PLUS (Pmode, new, XEXP (addr, 1));
	  }
	else
          new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTOFF);
      new = gen_rtx_CONST (Pmode, new);
      new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, new);

      if (reg != 0)
	{
	  emit_move_insn (reg, new);
	  new = reg;
	}
    }
  else if (GET_CODE (addr) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (addr) == 0)
    {
      if (TARGET_64BIT)
	{
	  new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOTPCREL);
	  new = gen_rtx_CONST (Pmode, new);
	  new = gen_const_mem (Pmode, new);
	  set_mem_alias_set (new, ix86_GOT_alias_set ());

	  if (reg == 0)
	    reg = gen_reg_rtx (Pmode);
	  /* Use directly gen_movsi, otherwise the address is loaded
	     into register for CSE.  We don't want to CSE this addresses,
	     instead we CSE addresses from the GOT table, so skip this.  */
	  emit_insn (gen_movsi (reg, new));
	  new = reg;
	}
      else
	{
	  /* This symbol must be referenced via a load from the
	     Global Offset Table (@GOT).  */

	  if (reload_in_progress)
	    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;
	  new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, addr), UNSPEC_GOT);
	  new = gen_rtx_CONST (Pmode, new);
	  new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, new);
	  new = gen_const_mem (Pmode, new);
	  set_mem_alias_set (new, ix86_GOT_alias_set ());

	  if (reg == 0)
	    reg = gen_reg_rtx (Pmode);
	  emit_move_insn (reg, new);
	  new = reg;
	}
    }
  else
    {
      if (GET_CODE (addr) == CONST_INT
	  && !x86_64_immediate_operand (addr, VOIDmode))
	{
	  if (reg)
	    {
	      emit_move_insn (reg, addr);
	      new = reg;
	    }
	  else
	    new = force_reg (Pmode, addr);
	}
      else if (GET_CODE (addr) == CONST)
	{
	  addr = XEXP (addr, 0);

	  /* We must match stuff we generate before.  Assume the only
	     unspecs that can get here are ours.  Not that we could do
	     anything with them anyway....  */
	  if (GET_CODE (addr) == UNSPEC
	      || (GET_CODE (addr) == PLUS
		  && GET_CODE (XEXP (addr, 0)) == UNSPEC))
	    return orig;
	  gcc_assert (GET_CODE (addr) == PLUS);
	}
      if (GET_CODE (addr) == PLUS)
	{
	  rtx op0 = XEXP (addr, 0), op1 = XEXP (addr, 1);

	  /* Check first to see if this is a constant offset from a @GOTOFF
	     symbol reference.  */
	  if (local_symbolic_operand (op0, Pmode)
	      && GET_CODE (op1) == CONST_INT)
	    {
	      if (!TARGET_64BIT)
		{
		  if (reload_in_progress)
		    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;
		  new = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, op0),
					UNSPEC_GOTOFF);
		  new = gen_rtx_PLUS (Pmode, new, op1);
		  new = gen_rtx_CONST (Pmode, new);
		  new = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, new);

		  if (reg != 0)
		    {
		      emit_move_insn (reg, new);
		      new = reg;
		    }
		}
	      else
		{
		  if (INTVAL (op1) < -16*1024*1024
		      || INTVAL (op1) >= 16*1024*1024)
		    {
		      if (!x86_64_immediate_operand (op1, Pmode))
			op1 = force_reg (Pmode, op1);
		      new = gen_rtx_PLUS (Pmode, force_reg (Pmode, op0), op1);
		    }
		}
	    }
	  else
	    {
	      base = legitimize_pic_address (XEXP (addr, 0), reg);
	      new  = legitimize_pic_address (XEXP (addr, 1),
					     base == reg ? NULL_RTX : reg);

	      if (GET_CODE (new) == CONST_INT)
		new = plus_constant (base, INTVAL (new));
	      else
		{
		  if (GET_CODE (new) == PLUS && CONSTANT_P (XEXP (new, 1)))
		    {
		      base = gen_rtx_PLUS (Pmode, base, XEXP (new, 0));
		      new = XEXP (new, 1);
		    }
		  new = gen_rtx_PLUS (Pmode, base, new);
		}
	    }
	}
    }
  return new;
}

/* Load the thread pointer.  If TO_REG is true, force it into a register.  */

static rtx
get_thread_pointer (int to_reg)
{
  rtx tp, reg, insn;

  tp = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, const0_rtx), UNSPEC_TP);
  if (!to_reg)
    return tp;

  reg = gen_reg_rtx (Pmode);
  insn = gen_rtx_SET (VOIDmode, reg, tp);
  insn = emit_insn (insn);

  return reg;
}

/* A subroutine of legitimize_address and ix86_expand_move.  FOR_MOV is
   false if we expect this to be used for a memory address and true if
   we expect to load the address into a register.  */

static rtx
legitimize_tls_address (rtx x, enum tls_model model, int for_mov)
{
  rtx dest, base, off, pic, tp;
  int type;

  switch (model)
    {
    case TLS_MODEL_GLOBAL_DYNAMIC:
      dest = gen_reg_rtx (Pmode);
      tp = TARGET_GNU2_TLS ? get_thread_pointer (1) : 0;

      if (TARGET_64BIT && ! TARGET_GNU2_TLS)
	{
	  rtx rax = gen_rtx_REG (Pmode, 0), insns;

	  start_sequence ();
	  emit_call_insn (gen_tls_global_dynamic_64 (rax, x));
	  insns = get_insns ();
	  end_sequence ();

	  emit_libcall_block (insns, dest, rax, x);
	}
      else if (TARGET_64BIT && TARGET_GNU2_TLS)
	emit_insn (gen_tls_global_dynamic_64 (dest, x));
      else
	emit_insn (gen_tls_global_dynamic_32 (dest, x));

      if (TARGET_GNU2_TLS)
	{
	  dest = force_reg (Pmode, gen_rtx_PLUS (Pmode, tp, dest));

	  set_unique_reg_note (get_last_insn (), REG_EQUIV, x);
	}
      break;

    case TLS_MODEL_LOCAL_DYNAMIC:
      base = gen_reg_rtx (Pmode);
      tp = TARGET_GNU2_TLS ? get_thread_pointer (1) : 0;

      if (TARGET_64BIT && ! TARGET_GNU2_TLS)
	{
	  rtx rax = gen_rtx_REG (Pmode, 0), insns, note;

	  start_sequence ();
	  emit_call_insn (gen_tls_local_dynamic_base_64 (rax));
	  insns = get_insns ();
	  end_sequence ();

	  note = gen_rtx_EXPR_LIST (VOIDmode, const0_rtx, NULL);
	  note = gen_rtx_EXPR_LIST (VOIDmode, ix86_tls_get_addr (), note);
	  emit_libcall_block (insns, base, rax, note);
	}
      else if (TARGET_64BIT && TARGET_GNU2_TLS)
	emit_insn (gen_tls_local_dynamic_base_64 (base));
      else
	emit_insn (gen_tls_local_dynamic_base_32 (base));

      if (TARGET_GNU2_TLS)
	{
	  rtx x = ix86_tls_module_base ();

	  set_unique_reg_note (get_last_insn (), REG_EQUIV,
			       gen_rtx_MINUS (Pmode, x, tp));
	}

      off = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, x), UNSPEC_DTPOFF);
      off = gen_rtx_CONST (Pmode, off);

      dest = force_reg (Pmode, gen_rtx_PLUS (Pmode, base, off));

      if (TARGET_GNU2_TLS)
	{
	  dest = force_reg (Pmode, gen_rtx_PLUS (Pmode, dest, tp));

	  set_unique_reg_note (get_last_insn (), REG_EQUIV, x);
	}

      break;

    case TLS_MODEL_INITIAL_EXEC:
      if (TARGET_64BIT)
	{
	  pic = NULL;
	  type = UNSPEC_GOTNTPOFF;
	}
      else if (flag_pic)
	{
	  if (reload_in_progress)
	    regs_ever_live[PIC_OFFSET_TABLE_REGNUM] = 1;
	  pic = pic_offset_table_rtx;
	  type = TARGET_ANY_GNU_TLS ? UNSPEC_GOTNTPOFF : UNSPEC_GOTTPOFF;
	}
      else if (!TARGET_ANY_GNU_TLS)
	{
	  pic = gen_reg_rtx (Pmode);
	  emit_insn (gen_set_got (pic));
	  type = UNSPEC_GOTTPOFF;
	}
      else
	{
	  pic = NULL;
	  type = UNSPEC_INDNTPOFF;
	}

      off = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, x), type);
      off = gen_rtx_CONST (Pmode, off);
      if (pic)
	off = gen_rtx_PLUS (Pmode, pic, off);
      off = gen_const_mem (Pmode, off);
      set_mem_alias_set (off, ix86_GOT_alias_set ());

      if (TARGET_64BIT || TARGET_ANY_GNU_TLS)
	{
          base = get_thread_pointer (for_mov || !TARGET_TLS_DIRECT_SEG_REFS);
	  off = force_reg (Pmode, off);
	  return gen_rtx_PLUS (Pmode, base, off);
	}
      else
	{
	  base = get_thread_pointer (true);
	  dest = gen_reg_rtx (Pmode);
	  emit_insn (gen_subsi3 (dest, base, off));
	}
      break;

    case TLS_MODEL_LOCAL_EXEC:
      off = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, x),
			    (TARGET_64BIT || TARGET_ANY_GNU_TLS)
			    ? UNSPEC_NTPOFF : UNSPEC_TPOFF);
      off = gen_rtx_CONST (Pmode, off);

      if (TARGET_64BIT || TARGET_ANY_GNU_TLS)
	{
	  base = get_thread_pointer (for_mov || !TARGET_TLS_DIRECT_SEG_REFS);
	  return gen_rtx_PLUS (Pmode, base, off);
	}
      else
	{
	  base = get_thread_pointer (true);
	  dest = gen_reg_rtx (Pmode);
	  emit_insn (gen_subsi3 (dest, base, off));
	}
      break;

    default:
      gcc_unreachable ();
    }

  return dest;
}

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.

   For the 80386, we handle X+REG by loading X into a register R and
   using R+REG.  R will go in a general reg and indexing will be used.
   However, if REG is a broken-out memory address or multiplication,
   nothing needs to be done because REG can certainly go in a general reg.

   When -fpic is used, special handling is needed for symbolic references.
   See comments by legitimize_pic_address in i386.c for details.  */

rtx
legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED, enum machine_mode mode)
{
  int changed = 0;
  unsigned log;

  if (TARGET_DEBUG_ADDR)
    {
      fprintf (stderr, "\n==========\nLEGITIMIZE_ADDRESS, mode = %s\n",
	       GET_MODE_NAME (mode));
      debug_rtx (x);
    }

  log = GET_CODE (x) == SYMBOL_REF ? SYMBOL_REF_TLS_MODEL (x) : 0;
  if (log)
    return legitimize_tls_address (x, log, false);
  if (GET_CODE (x) == CONST
      && GET_CODE (XEXP (x, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == SYMBOL_REF
      && (log = SYMBOL_REF_TLS_MODEL (XEXP (XEXP (x, 0), 0))))
    {
      rtx t = legitimize_tls_address (XEXP (XEXP (x, 0), 0), log, false);
      return gen_rtx_PLUS (Pmode, t, XEXP (XEXP (x, 0), 1));
    }

  if (flag_pic && SYMBOLIC_CONST (x))
    return legitimize_pic_address (x, 0);

  /* Canonicalize shifts by 0, 1, 2, 3 into multiply */
  if (GET_CODE (x) == ASHIFT
      && GET_CODE (XEXP (x, 1)) == CONST_INT
      && (unsigned HOST_WIDE_INT) INTVAL (XEXP (x, 1)) < 4)
    {
      changed = 1;
      log = INTVAL (XEXP (x, 1));
      x = gen_rtx_MULT (Pmode, force_reg (Pmode, XEXP (x, 0)),
			GEN_INT (1 << log));
    }

  if (GET_CODE (x) == PLUS)
    {
      /* Canonicalize shifts by 0, 1, 2, 3 into multiply.  */

      if (GET_CODE (XEXP (x, 0)) == ASHIFT
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && (unsigned HOST_WIDE_INT) INTVAL (XEXP (XEXP (x, 0), 1)) < 4)
	{
	  changed = 1;
	  log = INTVAL (XEXP (XEXP (x, 0), 1));
	  XEXP (x, 0) = gen_rtx_MULT (Pmode,
				      force_reg (Pmode, XEXP (XEXP (x, 0), 0)),
				      GEN_INT (1 << log));
	}

      if (GET_CODE (XEXP (x, 1)) == ASHIFT
	  && GET_CODE (XEXP (XEXP (x, 1), 1)) == CONST_INT
	  && (unsigned HOST_WIDE_INT) INTVAL (XEXP (XEXP (x, 1), 1)) < 4)
	{
	  changed = 1;
	  log = INTVAL (XEXP (XEXP (x, 1), 1));
	  XEXP (x, 1) = gen_rtx_MULT (Pmode,
				      force_reg (Pmode, XEXP (XEXP (x, 1), 0)),
				      GEN_INT (1 << log));
	}

      /* Put multiply first if it isn't already.  */
      if (GET_CODE (XEXP (x, 1)) == MULT)
	{
	  rtx tmp = XEXP (x, 0);
	  XEXP (x, 0) = XEXP (x, 1);
	  XEXP (x, 1) = tmp;
	  changed = 1;
	}

      /* Canonicalize (plus (mult (reg) (const)) (plus (reg) (const)))
	 into (plus (plus (mult (reg) (const)) (reg)) (const)).  This can be
	 created by virtual register instantiation, register elimination, and
	 similar optimizations.  */
      if (GET_CODE (XEXP (x, 0)) == MULT && GET_CODE (XEXP (x, 1)) == PLUS)
	{
	  changed = 1;
	  x = gen_rtx_PLUS (Pmode,
			    gen_rtx_PLUS (Pmode, XEXP (x, 0),
					  XEXP (XEXP (x, 1), 0)),
			    XEXP (XEXP (x, 1), 1));
	}

      /* Canonicalize
	 (plus (plus (mult (reg) (const)) (plus (reg) (const))) const)
	 into (plus (plus (mult (reg) (const)) (reg)) (const)).  */
      else if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 0)) == PLUS
	       && GET_CODE (XEXP (XEXP (x, 0), 0)) == MULT
	       && GET_CODE (XEXP (XEXP (x, 0), 1)) == PLUS
	       && CONSTANT_P (XEXP (x, 1)))
	{
	  rtx constant;
	  rtx other = NULL_RTX;

	  if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    {
	      constant = XEXP (x, 1);
	      other = XEXP (XEXP (XEXP (x, 0), 1), 1);
	    }
	  else if (GET_CODE (XEXP (XEXP (XEXP (x, 0), 1), 1)) == CONST_INT)
	    {
	      constant = XEXP (XEXP (XEXP (x, 0), 1), 1);
	      other = XEXP (x, 1);
	    }
	  else
	    constant = 0;

	  if (constant)
	    {
	      changed = 1;
	      x = gen_rtx_PLUS (Pmode,
				gen_rtx_PLUS (Pmode, XEXP (XEXP (x, 0), 0),
					      XEXP (XEXP (XEXP (x, 0), 1), 0)),
				plus_constant (other, INTVAL (constant)));
	    }
	}

      if (changed && legitimate_address_p (mode, x, FALSE))
	return x;

      if (GET_CODE (XEXP (x, 0)) == MULT)
	{
	  changed = 1;
	  XEXP (x, 0) = force_operand (XEXP (x, 0), 0);
	}

      if (GET_CODE (XEXP (x, 1)) == MULT)
	{
	  changed = 1;
	  XEXP (x, 1) = force_operand (XEXP (x, 1), 0);
	}

      if (changed
	  && GET_CODE (XEXP (x, 1)) == REG
	  && GET_CODE (XEXP (x, 0)) == REG)
	return x;

      if (flag_pic && SYMBOLIC_CONST (XEXP (x, 1)))
	{
	  changed = 1;
	  x = legitimize_pic_address (x, 0);
	}

      if (changed && legitimate_address_p (mode, x, FALSE))
	return x;

      if (GET_CODE (XEXP (x, 0)) == REG)
	{
	  rtx temp = gen_reg_rtx (Pmode);
	  rtx val  = force_operand (XEXP (x, 1), temp);
	  if (val != temp)
	    emit_move_insn (temp, val);

	  XEXP (x, 1) = temp;
	  return x;
	}

      else if (GET_CODE (XEXP (x, 1)) == REG)
	{
	  rtx temp = gen_reg_rtx (Pmode);
	  rtx val  = force_operand (XEXP (x, 0), temp);
	  if (val != temp)
	    emit_move_insn (temp, val);

	  XEXP (x, 0) = temp;
	  return x;
	}
    }

  return x;
}

/* Print an integer constant expression in assembler syntax.  Addition
   and subtraction are the only arithmetic that may appear in these
   expressions.  FILE is the stdio stream to write to, X is the rtx, and
   CODE is the operand print code from the output string.  */

static void
output_pic_addr_const (FILE *file, rtx x, int code)
{
  char buf[256];

  switch (GET_CODE (x))
    {
    case PC:
      gcc_assert (flag_pic);
      putc ('.', file);
      break;

    case SYMBOL_REF:
      if (! TARGET_MACHO || TARGET_64BIT)
	output_addr_const (file, x);
      else
	{
	  const char *name = XSTR (x, 0);

	  /* Mark the decl as referenced so that cgraph will output the function.  */
	  if (SYMBOL_REF_DECL (x))
	    mark_decl_referenced (SYMBOL_REF_DECL (x));

#if TARGET_MACHO
	  if (MACHOPIC_INDIRECT
	      && machopic_classify_symbol (x) == MACHOPIC_UNDEFINED_FUNCTION)
	    name = machopic_indirection_name (x, /*stub_p=*/true);
#endif
	  assemble_name (file, name);
	}
      if (!TARGET_MACHO && code == 'P' && ! SYMBOL_REF_LOCAL_P (x))
	fputs ("@PLT", file);
      break;

    case LABEL_REF:
      x = XEXP (x, 0);
      /* FALLTHRU */
    case CODE_LABEL:
      ASM_GENERATE_INTERNAL_LABEL (buf, "L", CODE_LABEL_NUMBER (x));
      assemble_name (asm_out_file, buf);
      break;

    case CONST_INT:
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      break;

    case CONST:
      /* This used to output parentheses around the expression,
	 but that does not work on the 386 (either ATT or BSD assembler).  */
      output_pic_addr_const (file, XEXP (x, 0), code);
      break;

    case CONST_DOUBLE:
      if (GET_MODE (x) == VOIDmode)
	{
	  /* We can use %d if the number is <32 bits and positive.  */
	  if (CONST_DOUBLE_HIGH (x) || CONST_DOUBLE_LOW (x) < 0)
	    fprintf (file, "0x%lx%08lx",
		     (unsigned long) CONST_DOUBLE_HIGH (x),
		     (unsigned long) CONST_DOUBLE_LOW (x));
	  else
	    fprintf (file, HOST_WIDE_INT_PRINT_DEC, CONST_DOUBLE_LOW (x));
	}
      else
	/* We can't handle floating point constants;
	   PRINT_OPERAND must handle them.  */
	output_operand_lossage ("floating constant misused");
      break;

    case PLUS:
      /* Some assemblers need integer constants to appear first.  */
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	{
	  output_pic_addr_const (file, XEXP (x, 0), code);
	  putc ('+', file);
	  output_pic_addr_const (file, XEXP (x, 1), code);
	}
      else
	{
	  gcc_assert (GET_CODE (XEXP (x, 1)) == CONST_INT);
	  output_pic_addr_const (file, XEXP (x, 1), code);
	  putc ('+', file);
	  output_pic_addr_const (file, XEXP (x, 0), code);
	}
      break;

    case MINUS:
      if (!TARGET_MACHO)
	putc (ASSEMBLER_DIALECT == ASM_INTEL ? '(' : '[', file);
      output_pic_addr_const (file, XEXP (x, 0), code);
      putc ('-', file);
      output_pic_addr_const (file, XEXP (x, 1), code);
      if (!TARGET_MACHO)
	putc (ASSEMBLER_DIALECT == ASM_INTEL ? ')' : ']', file);
      break;

     case UNSPEC:
       gcc_assert (XVECLEN (x, 0) == 1);
       output_pic_addr_const (file, XVECEXP (x, 0, 0), code);
       switch (XINT (x, 1))
	{
	case UNSPEC_GOT:
	  fputs ("@GOT", file);
	  break;
	case UNSPEC_GOTOFF:
	  fputs ("@GOTOFF", file);
	  break;
	case UNSPEC_GOTPCREL:
	  fputs ("@GOTPCREL(%rip)", file);
	  break;
	case UNSPEC_GOTTPOFF:
	  /* FIXME: This might be @TPOFF in Sun ld too.  */
	  fputs ("@GOTTPOFF", file);
	  break;
	case UNSPEC_TPOFF:
	  fputs ("@TPOFF", file);
	  break;
	case UNSPEC_NTPOFF:
	  if (TARGET_64BIT)
	    fputs ("@TPOFF", file);
	  else
	    fputs ("@NTPOFF", file);
	  break;
	case UNSPEC_DTPOFF:
	  fputs ("@DTPOFF", file);
	  break;
	case UNSPEC_GOTNTPOFF:
	  if (TARGET_64BIT)
	    fputs ("@GOTTPOFF(%rip)", file);
	  else
	    fputs ("@GOTNTPOFF", file);
	  break;
	case UNSPEC_INDNTPOFF:
	  fputs ("@INDNTPOFF", file);
	  break;
	default:
	  output_operand_lossage ("invalid UNSPEC as operand");
	  break;
	}
       break;

    default:
      output_operand_lossage ("invalid expression as operand");
    }
}

/* This is called from dwarf2out.c via TARGET_ASM_OUTPUT_DWARF_DTPREL.
   We need to emit DTP-relative relocations.  */

static void
i386_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  fputs (ASM_LONG, file);
  output_addr_const (file, x);
  fputs ("@DTPOFF", file);
  switch (size)
    {
    case 4:
      break;
    case 8:
      fputs (", 0", file);
      break;
    default:
      gcc_unreachable ();
   }
}

/* In the name of slightly smaller debug output, and to cater to
   general assembler lossage, recognize PIC+GOTOFF and turn it back
   into a direct symbol reference.

   On Darwin, this is necessary to avoid a crash, because Darwin
   has a different PIC label for each routine but the DWARF debugging
   information is not associated with any particular routine, so it's
   necessary to remove references to the PIC label from RTL stored by
   the DWARF output code.  */

static rtx
ix86_delegitimize_address (rtx orig_x)
{
  rtx x = orig_x;
  /* reg_addend is NULL or a multiple of some register.  */
  rtx reg_addend = NULL_RTX;
  /* const_addend is NULL or a const_int.  */
  rtx const_addend = NULL_RTX;
  /* This is the result, or NULL.  */
  rtx result = NULL_RTX;

  if (GET_CODE (x) == MEM)
    x = XEXP (x, 0);

  if (TARGET_64BIT)
    {
      if (GET_CODE (x) != CONST
	  || GET_CODE (XEXP (x, 0)) != UNSPEC
	  || XINT (XEXP (x, 0), 1) != UNSPEC_GOTPCREL
	  || GET_CODE (orig_x) != MEM)
	return orig_x;
      return XVECEXP (XEXP (x, 0), 0, 0);
    }

  if (GET_CODE (x) != PLUS
      || GET_CODE (XEXP (x, 1)) != CONST)
    return orig_x;

  if (GET_CODE (XEXP (x, 0)) == REG
      && REGNO (XEXP (x, 0)) == PIC_OFFSET_TABLE_REGNUM)
    /* %ebx + GOT/GOTOFF */
    ;
  else if (GET_CODE (XEXP (x, 0)) == PLUS)
    {
      /* %ebx + %reg * scale + GOT/GOTOFF */
      reg_addend = XEXP (x, 0);
      if (GET_CODE (XEXP (reg_addend, 0)) == REG
	  && REGNO (XEXP (reg_addend, 0)) == PIC_OFFSET_TABLE_REGNUM)
	reg_addend = XEXP (reg_addend, 1);
      else if (GET_CODE (XEXP (reg_addend, 1)) == REG
	       && REGNO (XEXP (reg_addend, 1)) == PIC_OFFSET_TABLE_REGNUM)
	reg_addend = XEXP (reg_addend, 0);
      else
	return orig_x;
      if (GET_CODE (reg_addend) != REG
	  && GET_CODE (reg_addend) != MULT
	  && GET_CODE (reg_addend) != ASHIFT)
	return orig_x;
    }
  else
    return orig_x;

  x = XEXP (XEXP (x, 1), 0);
  if (GET_CODE (x) == PLUS
      && GET_CODE (XEXP (x, 1)) == CONST_INT)
    {
      const_addend = XEXP (x, 1);
      x = XEXP (x, 0);
    }

  if (GET_CODE (x) == UNSPEC
      && ((XINT (x, 1) == UNSPEC_GOT && GET_CODE (orig_x) == MEM)
	  || (XINT (x, 1) == UNSPEC_GOTOFF && GET_CODE (orig_x) != MEM)))
    result = XVECEXP (x, 0, 0);

  if (TARGET_MACHO && darwin_local_data_pic (x)
      && GET_CODE (orig_x) != MEM)
    result = XEXP (x, 0);

  if (! result)
    return orig_x;

  if (const_addend)
    result = gen_rtx_PLUS (Pmode, result, const_addend);
  if (reg_addend)
    result = gen_rtx_PLUS (Pmode, reg_addend, result);
  return result;
}

static void
put_condition_code (enum rtx_code code, enum machine_mode mode, int reverse,
		    int fp, FILE *file)
{
  const char *suffix;

  if (mode == CCFPmode || mode == CCFPUmode)
    {
      enum rtx_code second_code, bypass_code;
      ix86_fp_comparison_codes (code, &bypass_code, &code, &second_code);
      gcc_assert (bypass_code == UNKNOWN && second_code == UNKNOWN);
      code = ix86_fp_compare_code_to_integer (code);
      mode = CCmode;
    }
  if (reverse)
    code = reverse_condition (code);

  switch (code)
    {
    case EQ:
      suffix = "e";
      break;
    case NE:
      suffix = "ne";
      break;
    case GT:
      gcc_assert (mode == CCmode || mode == CCNOmode || mode == CCGCmode);
      suffix = "g";
      break;
    case GTU:
      /* ??? Use "nbe" instead of "a" for fcmov lossage on some assemblers.
	 Those same assemblers have the same but opposite lossage on cmov.  */
      gcc_assert (mode == CCmode);
      suffix = fp ? "nbe" : "a";
      break;
    case LT:
      switch (mode)
	{
	case CCNOmode:
	case CCGOCmode:
	  suffix = "s";
	  break;

	case CCmode:
	case CCGCmode:
	  suffix = "l";
	  break;

	default:
	  gcc_unreachable ();
	}
      break;
    case LTU:
      gcc_assert (mode == CCmode);
      suffix = "b";
      break;
    case GE:
      switch (mode)
	{
	case CCNOmode:
	case CCGOCmode:
	  suffix = "ns";
	  break;

	case CCmode:
	case CCGCmode:
	  suffix = "ge";
	  break;

	default:
	  gcc_unreachable ();
	}
      break;
    case GEU:
      /* ??? As above.  */
      gcc_assert (mode == CCmode);
      suffix = fp ? "nb" : "ae";
      break;
    case LE:
      gcc_assert (mode == CCmode || mode == CCGCmode || mode == CCNOmode);
      suffix = "le";
      break;
    case LEU:
      gcc_assert (mode == CCmode);
      suffix = "be";
      break;
    case UNORDERED:
      suffix = fp ? "u" : "p";
      break;
    case ORDERED:
      suffix = fp ? "nu" : "np";
      break;
    default:
      gcc_unreachable ();
    }
  fputs (suffix, file);
}

/* Print the name of register X to FILE based on its machine mode and number.
   If CODE is 'w', pretend the mode is HImode.
   If CODE is 'b', pretend the mode is QImode.
   If CODE is 'k', pretend the mode is SImode.
   If CODE is 'q', pretend the mode is DImode.
   If CODE is 'h', pretend the reg is the 'high' byte register.
   If CODE is 'y', print "st(0)" instead of "st", if the reg is stack op.  */

void
print_reg (rtx x, int code, FILE *file)
{
  gcc_assert (REGNO (x) != ARG_POINTER_REGNUM
	      && REGNO (x) != FRAME_POINTER_REGNUM
	      && REGNO (x) != FLAGS_REG
	      && REGNO (x) != FPSR_REG);

  if (ASSEMBLER_DIALECT == ASM_ATT || USER_LABEL_PREFIX[0] == 0)
    putc ('%', file);

  if (code == 'w' || MMX_REG_P (x))
    code = 2;
  else if (code == 'b')
    code = 1;
  else if (code == 'k')
    code = 4;
  else if (code == 'q')
    code = 8;
  else if (code == 'y')
    code = 3;
  else if (code == 'h')
    code = 0;
  else
    code = GET_MODE_SIZE (GET_MODE (x));

  /* Irritatingly, AMD extended registers use different naming convention
     from the normal registers.  */
  if (REX_INT_REG_P (x))
    {
      gcc_assert (TARGET_64BIT);
      switch (code)
	{
	  case 0:
	    error ("extended registers have no high halves");
	    break;
	  case 1:
	    fprintf (file, "r%ib", REGNO (x) - FIRST_REX_INT_REG + 8);
	    break;
	  case 2:
	    fprintf (file, "r%iw", REGNO (x) - FIRST_REX_INT_REG + 8);
	    break;
	  case 4:
	    fprintf (file, "r%id", REGNO (x) - FIRST_REX_INT_REG + 8);
	    break;
	  case 8:
	    fprintf (file, "r%i", REGNO (x) - FIRST_REX_INT_REG + 8);
	    break;
	  default:
	    error ("unsupported operand size for extended register");
	    break;
	}
      return;
    }
  switch (code)
    {
    case 3:
      if (STACK_TOP_P (x))
	{
	  fputs ("st(0)", file);
	  break;
	}
      /* FALLTHRU */
    case 8:
    case 4:
    case 12:
      if (! ANY_FP_REG_P (x))
	putc (code == 8 && TARGET_64BIT ? 'r' : 'e', file);
      /* FALLTHRU */
    case 16:
    case 2:
    normal:
      fputs (hi_reg_name[REGNO (x)], file);
      break;
    case 1:
      if (REGNO (x) >= ARRAY_SIZE (qi_reg_name))
	goto normal;
      fputs (qi_reg_name[REGNO (x)], file);
      break;
    case 0:
      if (REGNO (x) >= ARRAY_SIZE (qi_high_reg_name))
	goto normal;
      fputs (qi_high_reg_name[REGNO (x)], file);
      break;
    default:
      gcc_unreachable ();
    }
}

/* Locate some local-dynamic symbol still in use by this function
   so that we can print its name in some tls_local_dynamic_base
   pattern.  */

static const char *
get_some_local_dynamic_name (void)
{
  rtx insn;

  if (cfun->machine->some_ld_name)
    return cfun->machine->some_ld_name;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    if (INSN_P (insn)
	&& for_each_rtx (&PATTERN (insn), get_some_local_dynamic_name_1, 0))
      return cfun->machine->some_ld_name;

  gcc_unreachable ();
}

static int
get_some_local_dynamic_name_1 (rtx *px, void *data ATTRIBUTE_UNUSED)
{
  rtx x = *px;

  if (GET_CODE (x) == SYMBOL_REF
      && SYMBOL_REF_TLS_MODEL (x) == TLS_MODEL_LOCAL_DYNAMIC)
    {
      cfun->machine->some_ld_name = XSTR (x, 0);
      return 1;
    }

  return 0;
}

/* Meaning of CODE:
   L,W,B,Q,S,T -- print the opcode suffix for specified size of operand.
   C -- print opcode suffix for set/cmov insn.
   c -- like C, but print reversed condition
   F,f -- likewise, but for floating-point.
   O -- if HAVE_AS_IX86_CMOV_SUN_SYNTAX, expand to "w.", "l." or "q.",
        otherwise nothing
   R -- print the prefix for register names.
   z -- print the opcode suffix for the size of the current operand.
   * -- print a star (in certain assembler syntax)
   A -- print an absolute memory reference.
   w -- print the operand as if it's a "word" (HImode) even if it isn't.
   s -- print a shift double count, followed by the assemblers argument
	delimiter.
   b -- print the QImode name of the register for the indicated operand.
	%b0 would print %al if operands[0] is reg 0.
   w --  likewise, print the HImode name of the register.
   k --  likewise, print the SImode name of the register.
   q --  likewise, print the DImode name of the register.
   h -- print the QImode name for a "high" register, either ah, bh, ch or dh.
   y -- print "st(0)" instead of "st" as a register.
   D -- print condition for SSE cmp instruction.
   P -- if PIC, print an @PLT suffix.
   X -- don't print any sort of PIC '@' suffix for a symbol.
   & -- print some in-use local-dynamic symbol name.
   H -- print a memory address offset by 8; used for sse high-parts
 */

void
print_operand (FILE *file, rtx x, int code)
{
  if (code)
    {
      switch (code)
	{
	case '*':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('*', file);
	  return;

	case '&':
	  assemble_name (file, get_some_local_dynamic_name ());
	  return;

	case 'A':
	  switch (ASSEMBLER_DIALECT)
	    {
	    case ASM_ATT:
	      putc ('*', file);
	      break;

	    case ASM_INTEL:
	      /* Intel syntax. For absolute addresses, registers should not
		 be surrounded by braces.  */
	      if (GET_CODE (x) != REG)
		{
		  putc ('[', file);
		  PRINT_OPERAND (file, x, 0);
		  putc (']', file);
		  return;
		}
	      break;

	    default:
	      gcc_unreachable ();
	    }

	  PRINT_OPERAND (file, x, 0);
	  return;


	case 'L':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('l', file);
	  return;

	case 'W':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('w', file);
	  return;

	case 'B':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('b', file);
	  return;

	case 'Q':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('l', file);
	  return;

	case 'S':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('s', file);
	  return;

	case 'T':
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('t', file);
	  return;

	case 'z':
	  /* 387 opcodes don't get size suffixes if the operands are
	     registers.  */
	  if (STACK_REG_P (x))
	    return;

	  /* Likewise if using Intel opcodes.  */
	  if (ASSEMBLER_DIALECT == ASM_INTEL)
	    return;

	  /* This is the size of op from size of operand.  */
	  switch (GET_MODE_SIZE (GET_MODE (x)))
	    {
	    case 2:
#ifdef HAVE_GAS_FILDS_FISTS
	      putc ('s', file);
#endif
	      return;

	    case 4:
	      if (GET_MODE (x) == SFmode)
		{
		  putc ('s', file);
		  return;
		}
	      else
		putc ('l', file);
	      return;

	    case 12:
	    case 16:
	      putc ('t', file);
	      return;

	    case 8:
	      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_INT)
		{
#ifdef GAS_MNEMONICS
		  putc ('q', file);
#else
		  putc ('l', file);
		  putc ('l', file);
#endif
		}
	      else
	        putc ('l', file);
	      return;

	    default:
	      gcc_unreachable ();
	    }

	case 'b':
	case 'w':
	case 'k':
	case 'q':
	case 'h':
	case 'y':
	case 'X':
	case 'P':
	  break;

	case 's':
	  if (GET_CODE (x) == CONST_INT || ! SHIFT_DOUBLE_OMITS_COUNT)
	    {
	      PRINT_OPERAND (file, x, 0);
	      putc (',', file);
	    }
	  return;

	case 'D':
	  /* Little bit of braindamage here.  The SSE compare instructions
	     does use completely different names for the comparisons that the
	     fp conditional moves.  */
	  switch (GET_CODE (x))
	    {
	    case EQ:
	    case UNEQ:
	      fputs ("eq", file);
	      break;
	    case LT:
	    case UNLT:
	      fputs ("lt", file);
	      break;
	    case LE:
	    case UNLE:
	      fputs ("le", file);
	      break;
	    case UNORDERED:
	      fputs ("unord", file);
	      break;
	    case NE:
	    case LTGT:
	      fputs ("neq", file);
	      break;
	    case UNGE:
	    case GE:
	      fputs ("nlt", file);
	      break;
	    case UNGT:
	    case GT:
	      fputs ("nle", file);
	      break;
	    case ORDERED:
	      fputs ("ord", file);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  return;
	case 'O':
#ifdef HAVE_AS_IX86_CMOV_SUN_SYNTAX
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    {
	      switch (GET_MODE (x))
		{
		case HImode: putc ('w', file); break;
		case SImode:
		case SFmode: putc ('l', file); break;
		case DImode:
		case DFmode: putc ('q', file); break;
		default: gcc_unreachable ();
		}
	      putc ('.', file);
	    }
#endif
	  return;
	case 'C':
	  put_condition_code (GET_CODE (x), GET_MODE (XEXP (x, 0)), 0, 0, file);
	  return;
	case 'F':
#ifdef HAVE_AS_IX86_CMOV_SUN_SYNTAX
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('.', file);
#endif
	  put_condition_code (GET_CODE (x), GET_MODE (XEXP (x, 0)), 0, 1, file);
	  return;

	  /* Like above, but reverse condition */
	case 'c':
	  /* Check to see if argument to %c is really a constant
	     and not a condition code which needs to be reversed.  */
	  if (!COMPARISON_P (x))
	  {
	    output_operand_lossage ("operand is neither a constant nor a condition code, invalid operand code 'c'");
	     return;
	  }
	  put_condition_code (GET_CODE (x), GET_MODE (XEXP (x, 0)), 1, 0, file);
	  return;
	case 'f':
#ifdef HAVE_AS_IX86_CMOV_SUN_SYNTAX
	  if (ASSEMBLER_DIALECT == ASM_ATT)
	    putc ('.', file);
#endif
	  put_condition_code (GET_CODE (x), GET_MODE (XEXP (x, 0)), 1, 1, file);
	  return;

	case 'H':
	  /* It doesn't actually matter what mode we use here, as we're
	     only going to use this for printing.  */
	  x = adjust_address_nv (x, DImode, 8);
	  break;

	case '+':
	  {
	    rtx x;

	    if (!optimize || optimize_size || !TARGET_BRANCH_PREDICTION_HINTS)
	      return;

	    x = find_reg_note (current_output_insn, REG_BR_PROB, 0);
	    if (x)
	      {
		int pred_val = INTVAL (XEXP (x, 0));

		if (pred_val < REG_BR_PROB_BASE * 45 / 100
		    || pred_val > REG_BR_PROB_BASE * 55 / 100)
		  {
		    int taken = pred_val > REG_BR_PROB_BASE / 2;
		    int cputaken = final_forward_branch_p (current_output_insn) == 0;

		    /* Emit hints only in the case default branch prediction
		       heuristics would fail.  */
		    if (taken != cputaken)
		      {
			/* We use 3e (DS) prefix for taken branches and
			   2e (CS) prefix for not taken branches.  */
			if (taken)
			  fputs ("ds ; ", file);
			else
			  fputs ("cs ; ", file);
		      }
		  }
	      }
	    return;
	  }
	default:
	    output_operand_lossage ("invalid operand code '%c'", code);
	}
    }

  if (GET_CODE (x) == REG)
    print_reg (x, code, file);

  else if (GET_CODE (x) == MEM)
    {
      /* No `byte ptr' prefix for call instructions.  */
      if (ASSEMBLER_DIALECT == ASM_INTEL && code != 'X' && code != 'P')
	{
	  const char * size;
	  switch (GET_MODE_SIZE (GET_MODE (x)))
	    {
	    case 1: size = "BYTE"; break;
	    case 2: size = "WORD"; break;
	    case 4: size = "DWORD"; break;
	    case 8: size = "QWORD"; break;
	    case 12: size = "XWORD"; break;
	    case 16: size = "XMMWORD"; break;
	    default:
	      gcc_unreachable ();
	    }

	  /* Check for explicit size override (codes 'b', 'w' and 'k')  */
	  if (code == 'b')
	    size = "BYTE";
	  else if (code == 'w')
	    size = "WORD";
	  else if (code == 'k')
	    size = "DWORD";

	  fputs (size, file);
	  fputs (" PTR ", file);
	}

      x = XEXP (x, 0);
      /* Avoid (%rip) for call operands.  */
      if (CONSTANT_ADDRESS_P (x) && code == 'P'
	       && GET_CODE (x) != CONST_INT)
	output_addr_const (file, x);
      else if (this_is_asm_operands && ! address_operand (x, VOIDmode))
	output_operand_lossage ("invalid constraints for operand");
      else
	output_address (x);
    }

  else if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) == SFmode)
    {
      REAL_VALUE_TYPE r;
      long l;

      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
      REAL_VALUE_TO_TARGET_SINGLE (r, l);

      if (ASSEMBLER_DIALECT == ASM_ATT)
	putc ('$', file);
      fprintf (file, "0x%08lx", l);
    }

  /* These float cases don't actually occur as immediate operands.  */
  else if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) == DFmode)
    {
      char dstr[30];

      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (x), sizeof (dstr), 0, 1);
      fprintf (file, "%s", dstr);
    }

  else if (GET_CODE (x) == CONST_DOUBLE
	   && GET_MODE (x) == XFmode)
    {
      char dstr[30];

      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (x), sizeof (dstr), 0, 1);
      fprintf (file, "%s", dstr);
    }

  else
    {
      /* We have patterns that allow zero sets of memory, for instance.
	 In 64-bit mode, we should probably support all 8-byte vectors,
	 since we can in fact encode that into an immediate.  */
      if (GET_CODE (x) == CONST_VECTOR)
	{
	  gcc_assert (x == CONST0_RTX (GET_MODE (x)));
	  x = const0_rtx;
	}

      if (code != 'P')
	{
	  if (GET_CODE (x) == CONST_INT || GET_CODE (x) == CONST_DOUBLE)
	    {
	      if (ASSEMBLER_DIALECT == ASM_ATT)
		putc ('$', file);
	    }
	  else if (GET_CODE (x) == CONST || GET_CODE (x) == SYMBOL_REF
		   || GET_CODE (x) == LABEL_REF)
	    {
	      if (ASSEMBLER_DIALECT == ASM_ATT)
		putc ('$', file);
	      else
		fputs ("OFFSET FLAT:", file);
	    }
	}
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      else if (flag_pic)
	output_pic_addr_const (file, x, code);
      else
	output_addr_const (file, x);
    }
}

/* Print a memory operand whose address is ADDR.  */

void
print_operand_address (FILE *file, rtx addr)
{
  struct ix86_address parts;
  rtx base, index, disp;
  int scale;
  int ok = ix86_decompose_address (addr, &parts);

  gcc_assert (ok);

  base = parts.base;
  index = parts.index;
  disp = parts.disp;
  scale = parts.scale;

  switch (parts.seg)
    {
    case SEG_DEFAULT:
      break;
    case SEG_FS:
    case SEG_GS:
      if (USER_LABEL_PREFIX[0] == 0)
	putc ('%', file);
      fputs ((parts.seg == SEG_FS ? "fs:" : "gs:"), file);
      break;
    default:
      gcc_unreachable ();
    }

  if (!base && !index)
    {
      /* Displacement only requires special attention.  */

      if (GET_CODE (disp) == CONST_INT)
	{
	  if (ASSEMBLER_DIALECT == ASM_INTEL && parts.seg == SEG_DEFAULT)
	    {
	      if (USER_LABEL_PREFIX[0] == 0)
		putc ('%', file);
	      fputs ("ds:", file);
	    }
	  fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (disp));
	}
      else if (flag_pic)
	output_pic_addr_const (file, disp, 0);
      else
	output_addr_const (file, disp);

      /* Use one byte shorter RIP relative addressing for 64bit mode.  */
      if (TARGET_64BIT)
	{
	  if (GET_CODE (disp) == CONST
	      && GET_CODE (XEXP (disp, 0)) == PLUS
	      && GET_CODE (XEXP (XEXP (disp, 0), 1)) == CONST_INT)
	    disp = XEXP (XEXP (disp, 0), 0);
	  if (GET_CODE (disp) == LABEL_REF
	      || (GET_CODE (disp) == SYMBOL_REF
		  && SYMBOL_REF_TLS_MODEL (disp) == 0))
	    fputs ("(%rip)", file);
	}
    }
  else
    {
      if (ASSEMBLER_DIALECT == ASM_ATT)
	{
	  if (disp)
	    {
	      if (flag_pic)
		output_pic_addr_const (file, disp, 0);
	      else if (GET_CODE (disp) == LABEL_REF)
		output_asm_label (disp);
	      else
		output_addr_const (file, disp);
	    }

	  putc ('(', file);
	  if (base)
	    print_reg (base, 0, file);
	  if (index)
	    {
	      putc (',', file);
	      print_reg (index, 0, file);
	      if (scale != 1)
		fprintf (file, ",%d", scale);
	    }
	  putc (')', file);
	}
      else
	{
	  rtx offset = NULL_RTX;

	  if (disp)
	    {
	      /* Pull out the offset of a symbol; print any symbol itself.  */
	      if (GET_CODE (disp) == CONST
		  && GET_CODE (XEXP (disp, 0)) == PLUS
		  && GET_CODE (XEXP (XEXP (disp, 0), 1)) == CONST_INT)
		{
		  offset = XEXP (XEXP (disp, 0), 1);
		  disp = gen_rtx_CONST (VOIDmode,
					XEXP (XEXP (disp, 0), 0));
		}

	      if (flag_pic)
		output_pic_addr_const (file, disp, 0);
	      else if (GET_CODE (disp) == LABEL_REF)
		output_asm_label (disp);
	      else if (GET_CODE (disp) == CONST_INT)
		offset = disp;
	      else
		output_addr_const (file, disp);
	    }

	  putc ('[', file);
	  if (base)
	    {
	      print_reg (base, 0, file);
	      if (offset)
		{
		  if (INTVAL (offset) >= 0)
		    putc ('+', file);
		  fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (offset));
		}
	    }
	  else if (offset)
	    fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (offset));
	  else
	    putc ('0', file);

	  if (index)
	    {
	      putc ('+', file);
	      print_reg (index, 0, file);
	      if (scale != 1)
		fprintf (file, "*%d", scale);
	    }
	  putc (']', file);
	}
    }
}

bool
output_addr_const_extra (FILE *file, rtx x)
{
  rtx op;

  if (GET_CODE (x) != UNSPEC)
    return false;

  op = XVECEXP (x, 0, 0);
  switch (XINT (x, 1))
    {
    case UNSPEC_GOTTPOFF:
      output_addr_const (file, op);
      /* FIXME: This might be @TPOFF in Sun ld.  */
      fputs ("@GOTTPOFF", file);
      break;
    case UNSPEC_TPOFF:
      output_addr_const (file, op);
      fputs ("@TPOFF", file);
      break;
    case UNSPEC_NTPOFF:
      output_addr_const (file, op);
      if (TARGET_64BIT)
	fputs ("@TPOFF", file);
      else
	fputs ("@NTPOFF", file);
      break;
    case UNSPEC_DTPOFF:
      output_addr_const (file, op);
      fputs ("@DTPOFF", file);
      break;
    case UNSPEC_GOTNTPOFF:
      output_addr_const (file, op);
      if (TARGET_64BIT)
	fputs ("@GOTTPOFF(%rip)", file);
      else
	fputs ("@GOTNTPOFF", file);
      break;
    case UNSPEC_INDNTPOFF:
      output_addr_const (file, op);
      fputs ("@INDNTPOFF", file);
      break;

    default:
      return false;
    }

  return true;
}

/* Split one or more DImode RTL references into pairs of SImode
   references.  The RTL can be REG, offsettable MEM, integer constant, or
   CONST_DOUBLE.  "operands" is a pointer to an array of DImode RTL to
   split and "num" is its length.  lo_half and hi_half are output arrays
   that parallel "operands".  */

void
split_di (rtx operands[], int num, rtx lo_half[], rtx hi_half[])
{
  while (num--)
    {
      rtx op = operands[num];

      /* simplify_subreg refuse to split volatile memory addresses,
         but we still have to handle it.  */
      if (GET_CODE (op) == MEM)
	{
	  lo_half[num] = adjust_address (op, SImode, 0);
	  hi_half[num] = adjust_address (op, SImode, 4);
	}
      else
	{
	  lo_half[num] = simplify_gen_subreg (SImode, op,
					      GET_MODE (op) == VOIDmode
					      ? DImode : GET_MODE (op), 0);
	  hi_half[num] = simplify_gen_subreg (SImode, op,
					      GET_MODE (op) == VOIDmode
					      ? DImode : GET_MODE (op), 4);
	}
    }
}
/* Split one or more TImode RTL references into pairs of DImode
   references.  The RTL can be REG, offsettable MEM, integer constant, or
   CONST_DOUBLE.  "operands" is a pointer to an array of DImode RTL to
   split and "num" is its length.  lo_half and hi_half are output arrays
   that parallel "operands".  */

void
split_ti (rtx operands[], int num, rtx lo_half[], rtx hi_half[])
{
  while (num--)
    {
      rtx op = operands[num];

      /* simplify_subreg refuse to split volatile memory addresses, but we
         still have to handle it.  */
      if (GET_CODE (op) == MEM)
	{
	  lo_half[num] = adjust_address (op, DImode, 0);
	  hi_half[num] = adjust_address (op, DImode, 8);
	}
      else
	{
	  lo_half[num] = simplify_gen_subreg (DImode, op, TImode, 0);
	  hi_half[num] = simplify_gen_subreg (DImode, op, TImode, 8);
	}
    }
}

/* Output code to perform a 387 binary operation in INSN, one of PLUS,
   MINUS, MULT or DIV.  OPERANDS are the insn operands, where operands[3]
   is the expression of the binary operation.  The output may either be
   emitted here, or returned to the caller, like all output_* functions.

   There is no guarantee that the operands are the same mode, as they
   might be within FLOAT or FLOAT_EXTEND expressions.  */

#ifndef SYSV386_COMPAT
/* Set to 1 for compatibility with brain-damaged assemblers.  No-one
   wants to fix the assemblers because that causes incompatibility
   with gcc.  No-one wants to fix gcc because that causes
   incompatibility with assemblers...  You can use the option of
   -DSYSV386_COMPAT=0 if you recompile both gcc and gas this way.  */
#define SYSV386_COMPAT 1
#endif

const char *
output_387_binary_op (rtx insn, rtx *operands)
{
  static char buf[30];
  const char *p;
  const char *ssep;
  int is_sse = SSE_REG_P (operands[0]) || SSE_REG_P (operands[1]) || SSE_REG_P (operands[2]);

#ifdef ENABLE_CHECKING
  /* Even if we do not want to check the inputs, this documents input
     constraints.  Which helps in understanding the following code.  */
  if (STACK_REG_P (operands[0])
      && ((REG_P (operands[1])
	   && REGNO (operands[0]) == REGNO (operands[1])
	   && (STACK_REG_P (operands[2]) || GET_CODE (operands[2]) == MEM))
	  || (REG_P (operands[2])
	      && REGNO (operands[0]) == REGNO (operands[2])
	      && (STACK_REG_P (operands[1]) || GET_CODE (operands[1]) == MEM)))
      && (STACK_TOP_P (operands[1]) || STACK_TOP_P (operands[2])))
    ; /* ok */
  else
    gcc_assert (is_sse);
#endif

  switch (GET_CODE (operands[3]))
    {
    case PLUS:
      if (GET_MODE_CLASS (GET_MODE (operands[1])) == MODE_INT
	  || GET_MODE_CLASS (GET_MODE (operands[2])) == MODE_INT)
	p = "fiadd";
      else
	p = "fadd";
      ssep = "add";
      break;

    case MINUS:
      if (GET_MODE_CLASS (GET_MODE (operands[1])) == MODE_INT
	  || GET_MODE_CLASS (GET_MODE (operands[2])) == MODE_INT)
	p = "fisub";
      else
	p = "fsub";
      ssep = "sub";
      break;

    case MULT:
      if (GET_MODE_CLASS (GET_MODE (operands[1])) == MODE_INT
	  || GET_MODE_CLASS (GET_MODE (operands[2])) == MODE_INT)
	p = "fimul";
      else
	p = "fmul";
      ssep = "mul";
      break;

    case DIV:
      if (GET_MODE_CLASS (GET_MODE (operands[1])) == MODE_INT
	  || GET_MODE_CLASS (GET_MODE (operands[2])) == MODE_INT)
	p = "fidiv";
      else
	p = "fdiv";
      ssep = "div";
      break;

    default:
      gcc_unreachable ();
    }

  if (is_sse)
   {
      strcpy (buf, ssep);
      if (GET_MODE (operands[0]) == SFmode)
	strcat (buf, "ss\t{%2, %0|%0, %2}");
      else
	strcat (buf, "sd\t{%2, %0|%0, %2}");
      return buf;
   }
  strcpy (buf, p);

  switch (GET_CODE (operands[3]))
    {
    case MULT:
    case PLUS:
      if (REG_P (operands[2]) && REGNO (operands[0]) == REGNO (operands[2]))
	{
	  rtx temp = operands[2];
	  operands[2] = operands[1];
	  operands[1] = temp;
	}

      /* know operands[0] == operands[1].  */

      if (GET_CODE (operands[2]) == MEM)
	{
	  p = "%z2\t%2";
	  break;
	}

      if (find_regno_note (insn, REG_DEAD, REGNO (operands[2])))
	{
	  if (STACK_TOP_P (operands[0]))
	    /* How is it that we are storing to a dead operand[2]?
	       Well, presumably operands[1] is dead too.  We can't
	       store the result to st(0) as st(0) gets popped on this
	       instruction.  Instead store to operands[2] (which I
	       think has to be st(1)).  st(1) will be popped later.
	       gcc <= 2.8.1 didn't have this check and generated
	       assembly code that the Unixware assembler rejected.  */
	    p = "p\t{%0, %2|%2, %0}";	/* st(1) = st(0) op st(1); pop */
	  else
	    p = "p\t{%2, %0|%0, %2}";	/* st(r1) = st(r1) op st(0); pop */
	  break;
	}

      if (STACK_TOP_P (operands[0]))
	p = "\t{%y2, %0|%0, %y2}";	/* st(0) = st(0) op st(r2) */
      else
	p = "\t{%2, %0|%0, %2}";	/* st(r1) = st(r1) op st(0) */
      break;

    case MINUS:
    case DIV:
      if (GET_CODE (operands[1]) == MEM)
	{
	  p = "r%z1\t%1";
	  break;
	}

      if (GET_CODE (operands[2]) == MEM)
	{
	  p = "%z2\t%2";
	  break;
	}

      if (find_regno_note (insn, REG_DEAD, REGNO (operands[2])))
	{
#if SYSV386_COMPAT
	  /* The SystemV/386 SVR3.2 assembler, and probably all AT&T
	     derived assemblers, confusingly reverse the direction of
	     the operation for fsub{r} and fdiv{r} when the
	     destination register is not st(0).  The Intel assembler
	     doesn't have this brain damage.  Read !SYSV386_COMPAT to
	     figure out what the hardware really does.  */
	  if (STACK_TOP_P (operands[0]))
	    p = "{p\t%0, %2|rp\t%2, %0}";
	  else
	    p = "{rp\t%2, %0|p\t%0, %2}";
#else
	  if (STACK_TOP_P (operands[0]))
	    /* As above for fmul/fadd, we can't store to st(0).  */
	    p = "rp\t{%0, %2|%2, %0}";	/* st(1) = st(0) op st(1); pop */
	  else
	    p = "p\t{%2, %0|%0, %2}";	/* st(r1) = st(r1) op st(0); pop */
#endif
	  break;
	}

      if (find_regno_note (insn, REG_DEAD, REGNO (operands[1])))
	{
#if SYSV386_COMPAT
	  if (STACK_TOP_P (operands[0]))
	    p = "{rp\t%0, %1|p\t%1, %0}";
	  else
	    p = "{p\t%1, %0|rp\t%0, %1}";
#else
	  if (STACK_TOP_P (operands[0]))
	    p = "p\t{%0, %1|%1, %0}";	/* st(1) = st(1) op st(0); pop */
	  else
	    p = "rp\t{%1, %0|%0, %1}";	/* st(r2) = st(0) op st(r2); pop */
#endif
	  break;
	}

      if (STACK_TOP_P (operands[0]))
	{
	  if (STACK_TOP_P (operands[1]))
	    p = "\t{%y2, %0|%0, %y2}";	/* st(0) = st(0) op st(r2) */
	  else
	    p = "r\t{%y1, %0|%0, %y1}";	/* st(0) = st(r1) op st(0) */
	  break;
	}
      else if (STACK_TOP_P (operands[1]))
	{
#if SYSV386_COMPAT
	  p = "{\t%1, %0|r\t%0, %1}";
#else
	  p = "r\t{%1, %0|%0, %1}";	/* st(r2) = st(0) op st(r2) */
#endif
	}
      else
	{
#if SYSV386_COMPAT
	  p = "{r\t%2, %0|\t%0, %2}";
#else
	  p = "\t{%2, %0|%0, %2}";	/* st(r1) = st(r1) op st(0) */
#endif
	}
      break;

    default:
      gcc_unreachable ();
    }

  strcat (buf, p);
  return buf;
}

/* Return needed mode for entity in optimize_mode_switching pass.  */

int
ix86_mode_needed (int entity, rtx insn)
{
  enum attr_i387_cw mode;

  /* The mode UNINITIALIZED is used to store control word after a
     function call or ASM pattern.  The mode ANY specify that function
     has no requirements on the control word and make no changes in the
     bits we are interested in.  */

  if (CALL_P (insn)
      || (NONJUMP_INSN_P (insn)
	  && (asm_noperands (PATTERN (insn)) >= 0
	      || GET_CODE (PATTERN (insn)) == ASM_INPUT)))
    return I387_CW_UNINITIALIZED;

  if (recog_memoized (insn) < 0)
    return I387_CW_ANY;

  mode = get_attr_i387_cw (insn);

  switch (entity)
    {
    case I387_TRUNC:
      if (mode == I387_CW_TRUNC)
	return mode;
      break;

    case I387_FLOOR:
      if (mode == I387_CW_FLOOR)
	return mode;
      break;

    case I387_CEIL:
      if (mode == I387_CW_CEIL)
	return mode;
      break;

    case I387_MASK_PM:
      if (mode == I387_CW_MASK_PM)
	return mode;
      break;

    default:
      gcc_unreachable ();
    }

  return I387_CW_ANY;
}

/* Output code to initialize control word copies used by trunc?f?i and
   rounding patterns.  CURRENT_MODE is set to current control word,
   while NEW_MODE is set to new control word.  */

void
emit_i387_cw_initialization (int mode)
{
  rtx stored_mode = assign_386_stack_local (HImode, SLOT_CW_STORED);
  rtx new_mode;

  int slot;

  rtx reg = gen_reg_rtx (HImode);

  emit_insn (gen_x86_fnstcw_1 (stored_mode));
  emit_move_insn (reg, stored_mode);

  if (TARGET_64BIT || TARGET_PARTIAL_REG_STALL || optimize_size)
    {
      switch (mode)
	{
	case I387_CW_TRUNC:
	  /* round toward zero (truncate) */
	  emit_insn (gen_iorhi3 (reg, reg, GEN_INT (0x0c00)));
	  slot = SLOT_CW_TRUNC;
	  break;

	case I387_CW_FLOOR:
	  /* round down toward -oo */
	  emit_insn (gen_andhi3 (reg, reg, GEN_INT (~0x0c00)));
	  emit_insn (gen_iorhi3 (reg, reg, GEN_INT (0x0400)));
	  slot = SLOT_CW_FLOOR;
	  break;

	case I387_CW_CEIL:
	  /* round up toward +oo */
	  emit_insn (gen_andhi3 (reg, reg, GEN_INT (~0x0c00)));
	  emit_insn (gen_iorhi3 (reg, reg, GEN_INT (0x0800)));
	  slot = SLOT_CW_CEIL;
	  break;

	case I387_CW_MASK_PM:
	  /* mask precision exception for nearbyint() */
	  emit_insn (gen_iorhi3 (reg, reg, GEN_INT (0x0020)));
	  slot = SLOT_CW_MASK_PM;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    {
      switch (mode)
	{
	case I387_CW_TRUNC:
	  /* round toward zero (truncate) */
	  emit_insn (gen_movsi_insv_1 (reg, GEN_INT (0xc)));
	  slot = SLOT_CW_TRUNC;
	  break;

	case I387_CW_FLOOR:
	  /* round down toward -oo */
	  emit_insn (gen_movsi_insv_1 (reg, GEN_INT (0x4)));
	  slot = SLOT_CW_FLOOR;
	  break;

	case I387_CW_CEIL:
	  /* round up toward +oo */
	  emit_insn (gen_movsi_insv_1 (reg, GEN_INT (0x8)));
	  slot = SLOT_CW_CEIL;
	  break;

	case I387_CW_MASK_PM:
	  /* mask precision exception for nearbyint() */
	  emit_insn (gen_iorhi3 (reg, reg, GEN_INT (0x0020)));
	  slot = SLOT_CW_MASK_PM;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  gcc_assert (slot < MAX_386_STACK_LOCALS);

  new_mode = assign_386_stack_local (HImode, slot);
  emit_move_insn (new_mode, reg);
}

/* Output code for INSN to convert a float to a signed int.  OPERANDS
   are the insn operands.  The output may be [HSD]Imode and the input
   operand may be [SDX]Fmode.  */

const char *
output_fix_trunc (rtx insn, rtx *operands, int fisttp)
{
  int stack_top_dies = find_regno_note (insn, REG_DEAD, FIRST_STACK_REG) != 0;
  int dimode_p = GET_MODE (operands[0]) == DImode;
  int round_mode = get_attr_i387_cw (insn);

  /* Jump through a hoop or two for DImode, since the hardware has no
     non-popping instruction.  We used to do this a different way, but
     that was somewhat fragile and broke with post-reload splitters.  */
  if ((dimode_p || fisttp) && !stack_top_dies)
    output_asm_insn ("fld\t%y1", operands);

  gcc_assert (STACK_TOP_P (operands[1]));
  gcc_assert (GET_CODE (operands[0]) == MEM);

  if (fisttp)
      output_asm_insn ("fisttp%z0\t%0", operands);
  else
    {
      if (round_mode != I387_CW_ANY)
	output_asm_insn ("fldcw\t%3", operands);
      if (stack_top_dies || dimode_p)
	output_asm_insn ("fistp%z0\t%0", operands);
      else
	output_asm_insn ("fist%z0\t%0", operands);
      if (round_mode != I387_CW_ANY)
	output_asm_insn ("fldcw\t%2", operands);
    }

  return "";
}

/* Output code for x87 ffreep insn.  The OPNO argument, which may only
   have the values zero or one, indicates the ffreep insn's operand
   from the OPERANDS array.  */

static const char *
output_387_ffreep (rtx *operands ATTRIBUTE_UNUSED, int opno)
{
  if (TARGET_USE_FFREEP)
#if HAVE_AS_IX86_FFREEP
    return opno ? "ffreep\t%y1" : "ffreep\t%y0";
#else
    switch (REGNO (operands[opno]))
      {
      case FIRST_STACK_REG + 0: return ".word\t0xc0df";
      case FIRST_STACK_REG + 1: return ".word\t0xc1df";
      case FIRST_STACK_REG + 2: return ".word\t0xc2df";
      case FIRST_STACK_REG + 3: return ".word\t0xc3df";
      case FIRST_STACK_REG + 4: return ".word\t0xc4df";
      case FIRST_STACK_REG + 5: return ".word\t0xc5df";
      case FIRST_STACK_REG + 6: return ".word\t0xc6df";
      case FIRST_STACK_REG + 7: return ".word\t0xc7df";
      }
#endif

  return opno ? "fstp\t%y1" : "fstp\t%y0";
}


/* Output code for INSN to compare OPERANDS.  EFLAGS_P is 1 when fcomi
   should be used.  UNORDERED_P is true when fucom should be used.  */

const char *
output_fp_compare (rtx insn, rtx *operands, int eflags_p, int unordered_p)
{
  int stack_top_dies;
  rtx cmp_op0, cmp_op1;
  int is_sse = SSE_REG_P (operands[0]) || SSE_REG_P (operands[1]);

  if (eflags_p)
    {
      cmp_op0 = operands[0];
      cmp_op1 = operands[1];
    }
  else
    {
      cmp_op0 = operands[1];
      cmp_op1 = operands[2];
    }

  if (is_sse)
    {
      if (GET_MODE (operands[0]) == SFmode)
	if (unordered_p)
	  return "ucomiss\t{%1, %0|%0, %1}";
	else
	  return "comiss\t{%1, %0|%0, %1}";
      else
	if (unordered_p)
	  return "ucomisd\t{%1, %0|%0, %1}";
	else
	  return "comisd\t{%1, %0|%0, %1}";
    }

  gcc_assert (STACK_TOP_P (cmp_op0));

  stack_top_dies = find_regno_note (insn, REG_DEAD, FIRST_STACK_REG) != 0;

  if (cmp_op1 == CONST0_RTX (GET_MODE (cmp_op1)))
    {
      if (stack_top_dies)
	{
	  output_asm_insn ("ftst\n\tfnstsw\t%0", operands);
	  return output_387_ffreep (operands, 1);
	}
      else
	return "ftst\n\tfnstsw\t%0";
    }

  if (STACK_REG_P (cmp_op1)
      && stack_top_dies
      && find_regno_note (insn, REG_DEAD, REGNO (cmp_op1))
      && REGNO (cmp_op1) != FIRST_STACK_REG)
    {
      /* If both the top of the 387 stack dies, and the other operand
	 is also a stack register that dies, then this must be a
	 `fcompp' float compare */

      if (eflags_p)
	{
	  /* There is no double popping fcomi variant.  Fortunately,
	     eflags is immune from the fstp's cc clobbering.  */
	  if (unordered_p)
	    output_asm_insn ("fucomip\t{%y1, %0|%0, %y1}", operands);
	  else
	    output_asm_insn ("fcomip\t{%y1, %0|%0, %y1}", operands);
	  return output_387_ffreep (operands, 0);
	}
      else
	{
	  if (unordered_p)
	    return "fucompp\n\tfnstsw\t%0";
	  else
	    return "fcompp\n\tfnstsw\t%0";
	}
    }
  else
    {
      /* Encoded here as eflags_p | intmode | unordered_p | stack_top_dies.  */

      static const char * const alt[16] =
      {
	"fcom%z2\t%y2\n\tfnstsw\t%0",
	"fcomp%z2\t%y2\n\tfnstsw\t%0",
	"fucom%z2\t%y2\n\tfnstsw\t%0",
	"fucomp%z2\t%y2\n\tfnstsw\t%0",

	"ficom%z2\t%y2\n\tfnstsw\t%0",
	"ficomp%z2\t%y2\n\tfnstsw\t%0",
	NULL,
	NULL,

	"fcomi\t{%y1, %0|%0, %y1}",
	"fcomip\t{%y1, %0|%0, %y1}",
	"fucomi\t{%y1, %0|%0, %y1}",
	"fucomip\t{%y1, %0|%0, %y1}",

	NULL,
	NULL,
	NULL,
	NULL
      };

      int mask;
      const char *ret;

      mask  = eflags_p << 3;
      mask |= (GET_MODE_CLASS (GET_MODE (cmp_op1)) == MODE_INT) << 2;
      mask |= unordered_p << 1;
      mask |= stack_top_dies;

      gcc_assert (mask < 16);
      ret = alt[mask];
      gcc_assert (ret);

      return ret;
    }
}

void
ix86_output_addr_vec_elt (FILE *file, int value)
{
  const char *directive = ASM_LONG;

#ifdef ASM_QUAD
  if (TARGET_64BIT)
    directive = ASM_QUAD;
#else
  gcc_assert (!TARGET_64BIT);
#endif

  fprintf (file, "%s%s%d\n", directive, LPREFIX, value);
}

void
ix86_output_addr_diff_elt (FILE *file, int value, int rel)
{
  if (TARGET_64BIT)
    fprintf (file, "%s%s%d-%s%d\n",
	     ASM_LONG, LPREFIX, value, LPREFIX, rel);
  else if (HAVE_AS_GOTOFF_IN_DATA)
    fprintf (file, "%s%s%d@GOTOFF\n", ASM_LONG, LPREFIX, value);
#if TARGET_MACHO
  else if (TARGET_MACHO)
    {
      fprintf (file, "%s%s%d-", ASM_LONG, LPREFIX, value);
      machopic_output_function_base_name (file);
      fprintf(file, "\n");
    }
#endif
  else
    asm_fprintf (file, "%s%U%s+[.-%s%d]\n",
		 ASM_LONG, GOT_SYMBOL_NAME, LPREFIX, value);
}

/* Generate either "mov $0, reg" or "xor reg, reg", as appropriate
   for the target.  */

void
ix86_expand_clear (rtx dest)
{
  rtx tmp;

  /* We play register width games, which are only valid after reload.  */
  gcc_assert (reload_completed);

  /* Avoid HImode and its attendant prefix byte.  */
  if (GET_MODE_SIZE (GET_MODE (dest)) < 4)
    dest = gen_rtx_REG (SImode, REGNO (dest));

  tmp = gen_rtx_SET (VOIDmode, dest, const0_rtx);

  /* This predicate should match that for movsi_xor and movdi_xor_rex64.  */
  if (reload_completed && (!TARGET_USE_MOV0 || optimize_size))
    {
      rtx clob = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, 17));
      tmp = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, tmp, clob));
    }

  emit_insn (tmp);
}

/* X is an unchanging MEM.  If it is a constant pool reference, return
   the constant pool rtx, else NULL.  */

rtx
maybe_get_pool_constant (rtx x)
{
  x = ix86_delegitimize_address (XEXP (x, 0));

  if (GET_CODE (x) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (x))
    return get_pool_constant (x);

  return NULL_RTX;
}

void
ix86_expand_move (enum machine_mode mode, rtx operands[])
{
  int strict = (reload_in_progress || reload_completed);
  rtx op0, op1;
  enum tls_model model;

  op0 = operands[0];
  op1 = operands[1];

  if (GET_CODE (op1) == SYMBOL_REF)
    {
      model = SYMBOL_REF_TLS_MODEL (op1);
      if (model)
	{
	  op1 = legitimize_tls_address (op1, model, true);
	  op1 = force_operand (op1, op0);
	  if (op1 == op0)
	    return;
	}
    }
  else if (GET_CODE (op1) == CONST
	   && GET_CODE (XEXP (op1, 0)) == PLUS
	   && GET_CODE (XEXP (XEXP (op1, 0), 0)) == SYMBOL_REF)
    {
      model = SYMBOL_REF_TLS_MODEL (XEXP (XEXP (op1, 0), 0));
      if (model)
	{
	  rtx addend = XEXP (XEXP (op1, 0), 1);
	  op1 = legitimize_tls_address (XEXP (XEXP (op1, 0), 0), model, true);
	  op1 = force_operand (op1, NULL);
	  op1 = expand_simple_binop (Pmode, PLUS, op1, addend,
				     op0, 1, OPTAB_DIRECT);
	  if (op1 == op0)
	    return;
	}
    }

  if (flag_pic && mode == Pmode && symbolic_operand (op1, Pmode))
    {
      if (TARGET_MACHO && !TARGET_64BIT)
	{
#if TARGET_MACHO
	  if (MACHOPIC_PURE)
	    {
	      rtx temp = ((reload_in_progress
			   || ((op0 && GET_CODE (op0) == REG)
			       && mode == Pmode))
			  ? op0 : gen_reg_rtx (Pmode));
	      op1 = machopic_indirect_data_reference (op1, temp);
	      op1 = machopic_legitimize_pic_address (op1, mode,
						     temp == op1 ? 0 : temp);
	    }
	  else if (MACHOPIC_INDIRECT)
	    op1 = machopic_indirect_data_reference (op1, 0);
	  if (op0 == op1)
	    return;
#endif
	}
      else
	{
	  if (GET_CODE (op0) == MEM)
	    op1 = force_reg (Pmode, op1);
	  else
	    op1 = legitimize_address (op1, op1, Pmode);
	}
    }
  else
    {
      if (GET_CODE (op0) == MEM
	  && (PUSH_ROUNDING (GET_MODE_SIZE (mode)) != GET_MODE_SIZE (mode)
	      || !push_operand (op0, mode))
	  && GET_CODE (op1) == MEM)
	op1 = force_reg (mode, op1);

      if (push_operand (op0, mode)
	  && ! general_no_elim_operand (op1, mode))
	op1 = copy_to_mode_reg (mode, op1);

      /* Force large constants in 64bit compilation into register
	 to get them CSEed.  */
      if (TARGET_64BIT && mode == DImode
	  && immediate_operand (op1, mode)
	  && !x86_64_zext_immediate_operand (op1, VOIDmode)
	  && !register_operand (op0, mode)
	  && optimize && !reload_completed && !reload_in_progress)
	op1 = copy_to_mode_reg (mode, op1);

      if (FLOAT_MODE_P (mode))
	{
	  /* If we are loading a floating point constant to a register,
	     force the value to memory now, since we'll get better code
	     out the back end.  */

	  if (strict)
	    ;
	  else if (GET_CODE (op1) == CONST_DOUBLE)
	    {
	      op1 = validize_mem (force_const_mem (mode, op1));
	      if (!register_operand (op0, mode))
		{
		  rtx temp = gen_reg_rtx (mode);
		  emit_insn (gen_rtx_SET (VOIDmode, temp, op1));
		  emit_move_insn (op0, temp);
		  return;
		}
	    }
	}
    }

  emit_insn (gen_rtx_SET (VOIDmode, op0, op1));
}

void
ix86_expand_vector_move (enum machine_mode mode, rtx operands[])
{
  rtx op0 = operands[0], op1 = operands[1];

  /* Force constants other than zero into memory.  We do not know how
     the instructions used to build constants modify the upper 64 bits
     of the register, once we have that information we may be able
     to handle some of them more efficiently.  */
  if ((reload_in_progress | reload_completed) == 0
      && register_operand (op0, mode)
      && CONSTANT_P (op1)
      && standard_sse_constant_p (op1) <= 0)
    op1 = validize_mem (force_const_mem (mode, op1));

  /* Make operand1 a register if it isn't already.  */
  if (!no_new_pseudos
      && !register_operand (op0, mode)
      && !register_operand (op1, mode))
    {
      emit_move_insn (op0, force_reg (GET_MODE (op0), op1));
      return;
    }

  emit_insn (gen_rtx_SET (VOIDmode, op0, op1));
}

/* Implement the movmisalign patterns for SSE.  Non-SSE modes go
   straight to ix86_expand_vector_move.  */

void
ix86_expand_vector_move_misalign (enum machine_mode mode, rtx operands[])
{
  rtx op0, op1, m;

  op0 = operands[0];
  op1 = operands[1];

  if (MEM_P (op1))
    {
      /* If we're optimizing for size, movups is the smallest.  */
      if (optimize_size)
	{
	  op0 = gen_lowpart (V4SFmode, op0);
	  op1 = gen_lowpart (V4SFmode, op1);
	  emit_insn (gen_sse_movups (op0, op1));
	  return;
	}

      /* ??? If we have typed data, then it would appear that using
	 movdqu is the only way to get unaligned data loaded with
	 integer type.  */
      if (TARGET_SSE2 && GET_MODE_CLASS (mode) == MODE_VECTOR_INT)
	{
	  op0 = gen_lowpart (V16QImode, op0);
	  op1 = gen_lowpart (V16QImode, op1);
	  emit_insn (gen_sse2_movdqu (op0, op1));
	  return;
	}

      if (TARGET_SSE2 && mode == V2DFmode)
	{
	  rtx zero;

	  /* When SSE registers are split into halves, we can avoid
	     writing to the top half twice.  */
	  if (TARGET_SSE_SPLIT_REGS)
	    {
	      emit_insn (gen_rtx_CLOBBER (VOIDmode, op0));
	      zero = op0;
	    }
	  else
	    {
	      /* ??? Not sure about the best option for the Intel chips.
		 The following would seem to satisfy; the register is
		 entirely cleared, breaking the dependency chain.  We
		 then store to the upper half, with a dependency depth
		 of one.  A rumor has it that Intel recommends two movsd
		 followed by an unpacklpd, but this is unconfirmed.  And
		 given that the dependency depth of the unpacklpd would
		 still be one, I'm not sure why this would be better.  */
	      zero = CONST0_RTX (V2DFmode);
	    }

	  m = adjust_address (op1, DFmode, 0);
	  emit_insn (gen_sse2_loadlpd (op0, zero, m));
	  m = adjust_address (op1, DFmode, 8);
	  emit_insn (gen_sse2_loadhpd (op0, op0, m));
	}
      else
	{
	  if (TARGET_SSE_PARTIAL_REG_DEPENDENCY)
	    emit_move_insn (op0, CONST0_RTX (mode));
	  else
	    emit_insn (gen_rtx_CLOBBER (VOIDmode, op0));

	  if (mode != V4SFmode)
	    op0 = gen_lowpart (V4SFmode, op0);
	  m = adjust_address (op1, V2SFmode, 0);
	  emit_insn (gen_sse_loadlps (op0, op0, m));
	  m = adjust_address (op1, V2SFmode, 8);
	  emit_insn (gen_sse_loadhps (op0, op0, m));
	}
    }
  else if (MEM_P (op0))
    {
      /* If we're optimizing for size, movups is the smallest.  */
      if (optimize_size)
	{
	  op0 = gen_lowpart (V4SFmode, op0);
	  op1 = gen_lowpart (V4SFmode, op1);
	  emit_insn (gen_sse_movups (op0, op1));
	  return;
	}

      /* ??? Similar to above, only less clear because of quote
	 typeless stores unquote.  */
      if (TARGET_SSE2 && !TARGET_SSE_TYPELESS_STORES
	  && GET_MODE_CLASS (mode) == MODE_VECTOR_INT)
        {
	  op0 = gen_lowpart (V16QImode, op0);
	  op1 = gen_lowpart (V16QImode, op1);
	  emit_insn (gen_sse2_movdqu (op0, op1));
	  return;
	}

      if (TARGET_SSE2 && mode == V2DFmode)
	{
	  m = adjust_address (op0, DFmode, 0);
	  emit_insn (gen_sse2_storelpd (m, op1));
	  m = adjust_address (op0, DFmode, 8);
	  emit_insn (gen_sse2_storehpd (m, op1));
	}
      else
	{
	  if (mode != V4SFmode)
	    op1 = gen_lowpart (V4SFmode, op1);
	  m = adjust_address (op0, V2SFmode, 0);
	  emit_insn (gen_sse_storelps (m, op1));
	  m = adjust_address (op0, V2SFmode, 8);
	  emit_insn (gen_sse_storehps (m, op1));
	}
    }
  else
    gcc_unreachable ();
}

/* Expand a push in MODE.  This is some mode for which we do not support
   proper push instructions, at least from the registers that we expect
   the value to live in.  */

void
ix86_expand_push (enum machine_mode mode, rtx x)
{
  rtx tmp;

  tmp = expand_simple_binop (Pmode, PLUS, stack_pointer_rtx,
			     GEN_INT (-GET_MODE_SIZE (mode)),
			     stack_pointer_rtx, 1, OPTAB_DIRECT);
  if (tmp != stack_pointer_rtx)
    emit_move_insn (stack_pointer_rtx, tmp);

  tmp = gen_rtx_MEM (mode, stack_pointer_rtx);
  emit_move_insn (tmp, x);
}

/* Fix up OPERANDS to satisfy ix86_binary_operator_ok.  Return the
   destination to use for the operation.  If different from the true
   destination in operands[0], a copy operation will be required.  */

rtx
ix86_fixup_binary_operands (enum rtx_code code, enum machine_mode mode,
			    rtx operands[])
{
  int matching_memory;
  rtx src1, src2, dst;

  dst = operands[0];
  src1 = operands[1];
  src2 = operands[2];

  /* Recognize <var1> = <value> <op> <var1> for commutative operators */
  if (GET_RTX_CLASS (code) == RTX_COMM_ARITH
      && (rtx_equal_p (dst, src2)
	  || immediate_operand (src1, mode)))
    {
      rtx temp = src1;
      src1 = src2;
      src2 = temp;
    }

  /* If the destination is memory, and we do not have matching source
     operands, do things in registers.  */
  matching_memory = 0;
  if (GET_CODE (dst) == MEM)
    {
      if (rtx_equal_p (dst, src1))
	matching_memory = 1;
      else if (GET_RTX_CLASS (code) == RTX_COMM_ARITH
	       && rtx_equal_p (dst, src2))
	matching_memory = 2;
      else
	dst = gen_reg_rtx (mode);
    }

  /* Both source operands cannot be in memory.  */
  if (GET_CODE (src1) == MEM && GET_CODE (src2) == MEM)
    {
      if (matching_memory != 2)
	src2 = force_reg (mode, src2);
      else
	src1 = force_reg (mode, src1);
    }

  /* If the operation is not commutable, source 1 cannot be a constant
     or non-matching memory.  */
  if ((CONSTANT_P (src1)
       || (!matching_memory && GET_CODE (src1) == MEM))
      && GET_RTX_CLASS (code) != RTX_COMM_ARITH)
    src1 = force_reg (mode, src1);

  src1 = operands[1] = src1;
  src2 = operands[2] = src2;
  return dst;
}

/* Similarly, but assume that the destination has already been
   set up properly.  */

void
ix86_fixup_binary_operands_no_copy (enum rtx_code code,
				    enum machine_mode mode, rtx operands[])
{
  rtx dst = ix86_fixup_binary_operands (code, mode, operands);
  gcc_assert (dst == operands[0]);
}

/* Attempt to expand a binary operator.  Make the expansion closer to the
   actual machine, then just general_operand, which will allow 3 separate
   memory references (one output, two input) in a single insn.  */

void
ix86_expand_binary_operator (enum rtx_code code, enum machine_mode mode,
			     rtx operands[])
{
  rtx src1, src2, dst, op, clob;

  dst = ix86_fixup_binary_operands (code, mode, operands);
  src1 = operands[1];
  src2 = operands[2];

 /* Emit the instruction.  */

  op = gen_rtx_SET (VOIDmode, dst, gen_rtx_fmt_ee (code, mode, src1, src2));
  if (reload_in_progress)
    {
      /* Reload doesn't know about the flags register, and doesn't know that
         it doesn't want to clobber it.  We can only do this with PLUS.  */
      gcc_assert (code == PLUS);
      emit_insn (op);
    }
  else
    {
      clob = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, FLAGS_REG));
      emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, op, clob)));
    }

  /* Fix up the destination if needed.  */
  if (dst != operands[0])
    emit_move_insn (operands[0], dst);
}

/* Return TRUE or FALSE depending on whether the binary operator meets the
   appropriate constraints.  */

int
ix86_binary_operator_ok (enum rtx_code code,
			 enum machine_mode mode ATTRIBUTE_UNUSED,
			 rtx operands[3])
{
  /* Both source operands cannot be in memory.  */
  if (GET_CODE (operands[1]) == MEM && GET_CODE (operands[2]) == MEM)
    return 0;
  /* If the operation is not commutable, source 1 cannot be a constant.  */
  if (CONSTANT_P (operands[1]) && GET_RTX_CLASS (code) != RTX_COMM_ARITH)
    return 0;
  /* If the destination is memory, we must have a matching source operand.  */
  if (GET_CODE (operands[0]) == MEM
      && ! (rtx_equal_p (operands[0], operands[1])
	    || (GET_RTX_CLASS (code) == RTX_COMM_ARITH
		&& rtx_equal_p (operands[0], operands[2]))))
    return 0;
  /* If the operation is not commutable and the source 1 is memory, we must
     have a matching destination.  */
  if (GET_CODE (operands[1]) == MEM
      && GET_RTX_CLASS (code) != RTX_COMM_ARITH
      && ! rtx_equal_p (operands[0], operands[1]))
    return 0;
  return 1;
}

/* Attempt to expand a unary operator.  Make the expansion closer to the
   actual machine, then just general_operand, which will allow 2 separate
   memory references (one output, one input) in a single insn.  */

void
ix86_expand_unary_operator (enum rtx_code code, enum machine_mode mode,
			    rtx operands[])
{
  int matching_memory;
  rtx src, dst, op, clob;

  dst = operands[0];
  src = operands[1];

  /* If the destination is memory, and we do not have matching source
     operands, do things in registers.  */
  matching_memory = 0;
  if (MEM_P (dst))
    {
      if (rtx_equal_p (dst, src))
	matching_memory = 1;
      else
	dst = gen_reg_rtx (mode);
    }

  /* When source operand is memory, destination must match.  */
  if (MEM_P (src) && !matching_memory)
    src = force_reg (mode, src);

  /* Emit the instruction.  */

  op = gen_rtx_SET (VOIDmode, dst, gen_rtx_fmt_e (code, mode, src));
  if (reload_in_progress || code == NOT)
    {
      /* Reload doesn't know about the flags register, and doesn't know that
         it doesn't want to clobber it.  */
      gcc_assert (code == NOT);
      emit_insn (op);
    }
  else
    {
      clob = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, FLAGS_REG));
      emit_insn (gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, op, clob)));
    }

  /* Fix up the destination if needed.  */
  if (dst != operands[0])
    emit_move_insn (operands[0], dst);
}

/* Return TRUE or FALSE depending on whether the unary operator meets the
   appropriate constraints.  */

int
ix86_unary_operator_ok (enum rtx_code code ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			rtx operands[2] ATTRIBUTE_UNUSED)
{
  /* If one of operands is memory, source and destination must match.  */
  if ((GET_CODE (operands[0]) == MEM
       || GET_CODE (operands[1]) == MEM)
      && ! rtx_equal_p (operands[0], operands[1]))
    return FALSE;
  return TRUE;
}

/* A subroutine of ix86_expand_fp_absneg_operator and copysign expanders.
   Create a mask for the sign bit in MODE for an SSE register.  If VECT is
   true, then replicate the mask for all elements of the vector register.
   If INVERT is true, then create a mask excluding the sign bit.  */

rtx
ix86_build_signbit_mask (enum machine_mode mode, bool vect, bool invert)
{
  enum machine_mode vec_mode;
  HOST_WIDE_INT hi, lo;
  int shift = 63;
  rtvec v;
  rtx mask;

  /* Find the sign bit, sign extended to 2*HWI.  */
  if (mode == SFmode)
    lo = 0x80000000, hi = lo < 0;
  else if (HOST_BITS_PER_WIDE_INT >= 64)
    lo = (HOST_WIDE_INT)1 << shift, hi = -1;
  else
    lo = 0, hi = (HOST_WIDE_INT)1 << (shift - HOST_BITS_PER_WIDE_INT);

  if (invert)
    lo = ~lo, hi = ~hi;

  /* Force this value into the low part of a fp vector constant.  */
  mask = immed_double_const (lo, hi, mode == SFmode ? SImode : DImode);
  mask = gen_lowpart (mode, mask);

  if (mode == SFmode)
    {
      if (vect)
	v = gen_rtvec (4, mask, mask, mask, mask);
      else
	v = gen_rtvec (4, mask, CONST0_RTX (SFmode),
		       CONST0_RTX (SFmode), CONST0_RTX (SFmode));
      vec_mode = V4SFmode;
    }
  else
    {
      if (vect)
	v = gen_rtvec (2, mask, mask);
      else
	v = gen_rtvec (2, mask, CONST0_RTX (DFmode));
      vec_mode = V2DFmode;
    }

  return force_reg (vec_mode, gen_rtx_CONST_VECTOR (vec_mode, v));
}

/* Generate code for floating point ABS or NEG.  */

void
ix86_expand_fp_absneg_operator (enum rtx_code code, enum machine_mode mode,
				rtx operands[])
{
  rtx mask, set, use, clob, dst, src;
  bool matching_memory;
  bool use_sse = false;
  bool vector_mode = VECTOR_MODE_P (mode);
  enum machine_mode elt_mode = mode;

  if (vector_mode)
    {
      elt_mode = GET_MODE_INNER (mode);
      use_sse = true;
    }
  else if (TARGET_SSE_MATH)
    use_sse = SSE_FLOAT_MODE_P (mode);

  /* NEG and ABS performed with SSE use bitwise mask operations.
     Create the appropriate mask now.  */
  if (use_sse)
    mask = ix86_build_signbit_mask (elt_mode, vector_mode, code == ABS);
  else
    mask = NULL_RTX;

  dst = operands[0];
  src = operands[1];

  /* If the destination is memory, and we don't have matching source
     operands or we're using the x87, do things in registers.  */
  matching_memory = false;
  if (MEM_P (dst))
    {
      if (use_sse && rtx_equal_p (dst, src))
	matching_memory = true;
      else
	dst = gen_reg_rtx (mode);
    }
  if (MEM_P (src) && !matching_memory)
    src = force_reg (mode, src);

  if (vector_mode)
    {
      set = gen_rtx_fmt_ee (code == NEG ? XOR : AND, mode, src, mask);
      set = gen_rtx_SET (VOIDmode, dst, set);
      emit_insn (set);
    }
  else
    {
      set = gen_rtx_fmt_e (code, mode, src);
      set = gen_rtx_SET (VOIDmode, dst, set);
      if (mask)
        {
          use = gen_rtx_USE (VOIDmode, mask);
          clob = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, FLAGS_REG));
          emit_insn (gen_rtx_PARALLEL (VOIDmode,
				       gen_rtvec (3, set, use, clob)));
        }
      else
	emit_insn (set);
    }

  if (dst != operands[0])
    emit_move_insn (operands[0], dst);
}

/* Expand a copysign operation.  Special case operand 0 being a constant.  */

void
ix86_expand_copysign (rtx operands[])
{
  enum machine_mode mode, vmode;
  rtx dest, op0, op1, mask, nmask;

  dest = operands[0];
  op0 = operands[1];
  op1 = operands[2];

  mode = GET_MODE (dest);
  vmode = mode == SFmode ? V4SFmode : V2DFmode;

  if (GET_CODE (op0) == CONST_DOUBLE)
    {
      rtvec v;

      if (real_isneg (CONST_DOUBLE_REAL_VALUE (op0)))
	op0 = simplify_unary_operation (ABS, mode, op0, mode);

      if (op0 == CONST0_RTX (mode))
	op0 = CONST0_RTX (vmode);
      else
        {
	  if (mode == SFmode)
	    v = gen_rtvec (4, op0, CONST0_RTX (SFmode),
                           CONST0_RTX (SFmode), CONST0_RTX (SFmode));
	  else
	    v = gen_rtvec (2, op0, CONST0_RTX (DFmode));
          op0 = force_reg (vmode, gen_rtx_CONST_VECTOR (vmode, v));
	}

      mask = ix86_build_signbit_mask (mode, 0, 0);

      if (mode == SFmode)
	emit_insn (gen_copysignsf3_const (dest, op0, op1, mask));
      else
	emit_insn (gen_copysigndf3_const (dest, op0, op1, mask));
    }
  else
    {
      nmask = ix86_build_signbit_mask (mode, 0, 1);
      mask = ix86_build_signbit_mask (mode, 0, 0);

      if (mode == SFmode)
	emit_insn (gen_copysignsf3_var (dest, NULL, op0, op1, nmask, mask));
      else
	emit_insn (gen_copysigndf3_var (dest, NULL, op0, op1, nmask, mask));
    }
}

/* Deconstruct a copysign operation into bit masks.  Operand 0 is known to
   be a constant, and so has already been expanded into a vector constant.  */

void
ix86_split_copysign_const (rtx operands[])
{
  enum machine_mode mode, vmode;
  rtx dest, op0, op1, mask, x;

  dest = operands[0];
  op0 = operands[1];
  op1 = operands[2];
  mask = operands[3];

  mode = GET_MODE (dest);
  vmode = GET_MODE (mask);

  dest = simplify_gen_subreg (vmode, dest, mode, 0);
  x = gen_rtx_AND (vmode, dest, mask);
  emit_insn (gen_rtx_SET (VOIDmode, dest, x));

  if (op0 != CONST0_RTX (vmode))
    {
      x = gen_rtx_IOR (vmode, dest, op0);
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }
}

/* Deconstruct a copysign operation into bit masks.  Operand 0 is variable,
   so we have to do two masks.  */

void
ix86_split_copysign_var (rtx operands[])
{
  enum machine_mode mode, vmode;
  rtx dest, scratch, op0, op1, mask, nmask, x;

  dest = operands[0];
  scratch = operands[1];
  op0 = operands[2];
  op1 = operands[3];
  nmask = operands[4];
  mask = operands[5];

  mode = GET_MODE (dest);
  vmode = GET_MODE (mask);

  if (rtx_equal_p (op0, op1))
    {
      /* Shouldn't happen often (it's useless, obviously), but when it does
	 we'd generate incorrect code if we continue below.  */
      emit_move_insn (dest, op0);
      return;
    }

  if (REG_P (mask) && REGNO (dest) == REGNO (mask))	/* alternative 0 */
    {
      gcc_assert (REGNO (op1) == REGNO (scratch));

      x = gen_rtx_AND (vmode, scratch, mask);
      emit_insn (gen_rtx_SET (VOIDmode, scratch, x));

      dest = mask;
      op0 = simplify_gen_subreg (vmode, op0, mode, 0);
      x = gen_rtx_NOT (vmode, dest);
      x = gen_rtx_AND (vmode, x, op0);
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }
  else
    {
      if (REGNO (op1) == REGNO (scratch))		/* alternative 1,3 */
	{
	  x = gen_rtx_AND (vmode, scratch, mask);
	}
      else						/* alternative 2,4 */
	{
          gcc_assert (REGNO (mask) == REGNO (scratch));
          op1 = simplify_gen_subreg (vmode, op1, mode, 0);
	  x = gen_rtx_AND (vmode, scratch, op1);
	}
      emit_insn (gen_rtx_SET (VOIDmode, scratch, x));

      if (REGNO (op0) == REGNO (dest))			/* alternative 1,2 */
	{
	  dest = simplify_gen_subreg (vmode, op0, mode, 0);
	  x = gen_rtx_AND (vmode, dest, nmask);
	}
      else						/* alternative 3,4 */
	{
          gcc_assert (REGNO (nmask) == REGNO (dest));
	  dest = nmask;
	  op0 = simplify_gen_subreg (vmode, op0, mode, 0);
	  x = gen_rtx_AND (vmode, dest, op0);
	}
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }

  x = gen_rtx_IOR (vmode, dest, scratch);
  emit_insn (gen_rtx_SET (VOIDmode, dest, x));
}

/* Return TRUE or FALSE depending on whether the first SET in INSN
   has source and destination with matching CC modes, and that the
   CC mode is at least as constrained as REQ_MODE.  */

int
ix86_match_ccmode (rtx insn, enum machine_mode req_mode)
{
  rtx set;
  enum machine_mode set_mode;

  set = PATTERN (insn);
  if (GET_CODE (set) == PARALLEL)
    set = XVECEXP (set, 0, 0);
  gcc_assert (GET_CODE (set) == SET);
  gcc_assert (GET_CODE (SET_SRC (set)) == COMPARE);

  set_mode = GET_MODE (SET_DEST (set));
  switch (set_mode)
    {
    case CCNOmode:
      if (req_mode != CCNOmode
	  && (req_mode != CCmode
	      || XEXP (SET_SRC (set), 1) != const0_rtx))
	return 0;
      break;
    case CCmode:
      if (req_mode == CCGCmode)
	return 0;
      /* FALLTHRU */
    case CCGCmode:
      if (req_mode == CCGOCmode || req_mode == CCNOmode)
	return 0;
      /* FALLTHRU */
    case CCGOCmode:
      if (req_mode == CCZmode)
	return 0;
      /* FALLTHRU */
    case CCZmode:
      break;

    default:
      gcc_unreachable ();
    }

  return (GET_MODE (SET_SRC (set)) == set_mode);
}

/* Generate insn patterns to do an integer compare of OPERANDS.  */

static rtx
ix86_expand_int_compare (enum rtx_code code, rtx op0, rtx op1)
{
  enum machine_mode cmpmode;
  rtx tmp, flags;

  cmpmode = SELECT_CC_MODE (code, op0, op1);
  flags = gen_rtx_REG (cmpmode, FLAGS_REG);

  /* This is very simple, but making the interface the same as in the
     FP case makes the rest of the code easier.  */
  tmp = gen_rtx_COMPARE (cmpmode, op0, op1);
  emit_insn (gen_rtx_SET (VOIDmode, flags, tmp));

  /* Return the test that should be put into the flags user, i.e.
     the bcc, scc, or cmov instruction.  */
  return gen_rtx_fmt_ee (code, VOIDmode, flags, const0_rtx);
}

/* Figure out whether to use ordered or unordered fp comparisons.
   Return the appropriate mode to use.  */

enum machine_mode
ix86_fp_compare_mode (enum rtx_code code ATTRIBUTE_UNUSED)
{
  /* ??? In order to make all comparisons reversible, we do all comparisons
     non-trapping when compiling for IEEE.  Once gcc is able to distinguish
     all forms trapping and nontrapping comparisons, we can make inequality
     comparisons trapping again, since it results in better code when using
     FCOM based compares.  */
  return TARGET_IEEE_FP ? CCFPUmode : CCFPmode;
}

enum machine_mode
ix86_cc_mode (enum rtx_code code, rtx op0, rtx op1)
{
  if (SCALAR_FLOAT_MODE_P (GET_MODE (op0)))
    return ix86_fp_compare_mode (code);
  switch (code)
    {
      /* Only zero flag is needed.  */
    case EQ:			/* ZF=0 */
    case NE:			/* ZF!=0 */
      return CCZmode;
      /* Codes needing carry flag.  */
    case GEU:			/* CF=0 */
    case GTU:			/* CF=0 & ZF=0 */
    case LTU:			/* CF=1 */
    case LEU:			/* CF=1 | ZF=1 */
      return CCmode;
      /* Codes possibly doable only with sign flag when
         comparing against zero.  */
    case GE:			/* SF=OF   or   SF=0 */
    case LT:			/* SF<>OF  or   SF=1 */
      if (op1 == const0_rtx)
	return CCGOCmode;
      else
	/* For other cases Carry flag is not required.  */
	return CCGCmode;
      /* Codes doable only with sign flag when comparing
         against zero, but we miss jump instruction for it
         so we need to use relational tests against overflow
         that thus needs to be zero.  */
    case GT:			/* ZF=0 & SF=OF */
    case LE:			/* ZF=1 | SF<>OF */
      if (op1 == const0_rtx)
	return CCNOmode;
      else
	return CCGCmode;
      /* strcmp pattern do (use flags) and combine may ask us for proper
	 mode.  */
    case USE:
      return CCmode;
    default:
      gcc_unreachable ();
    }
}

/* Return the fixed registers used for condition codes.  */

static bool
ix86_fixed_condition_code_regs (unsigned int *p1, unsigned int *p2)
{
  *p1 = FLAGS_REG;
  *p2 = FPSR_REG;
  return true;
}

/* If two condition code modes are compatible, return a condition code
   mode which is compatible with both.  Otherwise, return
   VOIDmode.  */

static enum machine_mode
ix86_cc_modes_compatible (enum machine_mode m1, enum machine_mode m2)
{
  if (m1 == m2)
    return m1;

  if (GET_MODE_CLASS (m1) != MODE_CC || GET_MODE_CLASS (m2) != MODE_CC)
    return VOIDmode;

  if ((m1 == CCGCmode && m2 == CCGOCmode)
      || (m1 == CCGOCmode && m2 == CCGCmode))
    return CCGCmode;

  switch (m1)
    {
    default:
      gcc_unreachable ();

    case CCmode:
    case CCGCmode:
    case CCGOCmode:
    case CCNOmode:
    case CCZmode:
      switch (m2)
	{
	default:
	  return VOIDmode;

	case CCmode:
	case CCGCmode:
	case CCGOCmode:
	case CCNOmode:
	case CCZmode:
	  return CCmode;
	}

    case CCFPmode:
    case CCFPUmode:
      /* These are only compatible with themselves, which we already
	 checked above.  */
      return VOIDmode;
    }
}

/* Return true if we should use an FCOMI instruction for this fp comparison.  */

int
ix86_use_fcomi_compare (enum rtx_code code ATTRIBUTE_UNUSED)
{
  enum rtx_code swapped_code = swap_condition (code);
  return ((ix86_fp_comparison_cost (code) == ix86_fp_comparison_fcomi_cost (code))
	  || (ix86_fp_comparison_cost (swapped_code)
	      == ix86_fp_comparison_fcomi_cost (swapped_code)));
}

/* Swap, force into registers, or otherwise massage the two operands
   to a fp comparison.  The operands are updated in place; the new
   comparison code is returned.  */

static enum rtx_code
ix86_prepare_fp_compare_args (enum rtx_code code, rtx *pop0, rtx *pop1)
{
  enum machine_mode fpcmp_mode = ix86_fp_compare_mode (code);
  rtx op0 = *pop0, op1 = *pop1;
  enum machine_mode op_mode = GET_MODE (op0);
  int is_sse = TARGET_SSE_MATH && SSE_FLOAT_MODE_P (op_mode);

  /* All of the unordered compare instructions only work on registers.
     The same is true of the fcomi compare instructions.  The XFmode
     compare instructions require registers except when comparing
     against zero or when converting operand 1 from fixed point to
     floating point.  */

  if (!is_sse
      && (fpcmp_mode == CCFPUmode
	  || (op_mode == XFmode
	      && ! (standard_80387_constant_p (op0) == 1
		    || standard_80387_constant_p (op1) == 1)
	      && GET_CODE (op1) != FLOAT)
	  || ix86_use_fcomi_compare (code)))
    {
      op0 = force_reg (op_mode, op0);
      op1 = force_reg (op_mode, op1);
    }
  else
    {
      /* %%% We only allow op1 in memory; op0 must be st(0).  So swap
	 things around if they appear profitable, otherwise force op0
	 into a register.  */

      if (standard_80387_constant_p (op0) == 0
	  || (GET_CODE (op0) == MEM
	      && ! (standard_80387_constant_p (op1) == 0
		    || GET_CODE (op1) == MEM)))
	{
	  rtx tmp;
	  tmp = op0, op0 = op1, op1 = tmp;
	  code = swap_condition (code);
	}

      if (GET_CODE (op0) != REG)
	op0 = force_reg (op_mode, op0);

      if (CONSTANT_P (op1))
	{
	  int tmp = standard_80387_constant_p (op1);
	  if (tmp == 0)
	    op1 = validize_mem (force_const_mem (op_mode, op1));
	  else if (tmp == 1)
	    {
	      if (TARGET_CMOVE)
		op1 = force_reg (op_mode, op1);
	    }
	  else
	    op1 = force_reg (op_mode, op1);
	}
    }

  /* Try to rearrange the comparison to make it cheaper.  */
  if (ix86_fp_comparison_cost (code)
      > ix86_fp_comparison_cost (swap_condition (code))
      && (GET_CODE (op1) == REG || !no_new_pseudos))
    {
      rtx tmp;
      tmp = op0, op0 = op1, op1 = tmp;
      code = swap_condition (code);
      if (GET_CODE (op0) != REG)
	op0 = force_reg (op_mode, op0);
    }

  *pop0 = op0;
  *pop1 = op1;
  return code;
}

/* Convert comparison codes we use to represent FP comparison to integer
   code that will result in proper branch.  Return UNKNOWN if no such code
   is available.  */

enum rtx_code
ix86_fp_compare_code_to_integer (enum rtx_code code)
{
  switch (code)
    {
    case GT:
      return GTU;
    case GE:
      return GEU;
    case ORDERED:
    case UNORDERED:
      return code;
      break;
    case UNEQ:
      return EQ;
      break;
    case UNLT:
      return LTU;
      break;
    case UNLE:
      return LEU;
      break;
    case LTGT:
      return NE;
      break;
    default:
      return UNKNOWN;
    }
}

/* Split comparison code CODE into comparisons we can do using branch
   instructions.  BYPASS_CODE is comparison code for branch that will
   branch around FIRST_CODE and SECOND_CODE.  If some of branches
   is not required, set value to UNKNOWN.
   We never require more than two branches.  */

void
ix86_fp_comparison_codes (enum rtx_code code, enum rtx_code *bypass_code,
			  enum rtx_code *first_code,
			  enum rtx_code *second_code)
{
  *first_code = code;
  *bypass_code = UNKNOWN;
  *second_code = UNKNOWN;

  /* The fcomi comparison sets flags as follows:

     cmp    ZF PF CF
     >      0  0  0
     <      0  0  1
     =      1  0  0
     un     1  1  1 */

  switch (code)
    {
    case GT:			/* GTU - CF=0 & ZF=0 */
    case GE:			/* GEU - CF=0 */
    case ORDERED:		/* PF=0 */
    case UNORDERED:		/* PF=1 */
    case UNEQ:			/* EQ - ZF=1 */
    case UNLT:			/* LTU - CF=1 */
    case UNLE:			/* LEU - CF=1 | ZF=1 */
    case LTGT:			/* EQ - ZF=0 */
      break;
    case LT:			/* LTU - CF=1 - fails on unordered */
      *first_code = UNLT;
      *bypass_code = UNORDERED;
      break;
    case LE:			/* LEU - CF=1 | ZF=1 - fails on unordered */
      *first_code = UNLE;
      *bypass_code = UNORDERED;
      break;
    case EQ:			/* EQ - ZF=1 - fails on unordered */
      *first_code = UNEQ;
      *bypass_code = UNORDERED;
      break;
    case NE:			/* NE - ZF=0 - fails on unordered */
      *first_code = LTGT;
      *second_code = UNORDERED;
      break;
    case UNGE:			/* GEU - CF=0 - fails on unordered */
      *first_code = GE;
      *second_code = UNORDERED;
      break;
    case UNGT:			/* GTU - CF=0 & ZF=0 - fails on unordered */
      *first_code = GT;
      *second_code = UNORDERED;
      break;
    default:
      gcc_unreachable ();
    }
  if (!TARGET_IEEE_FP)
    {
      *second_code = UNKNOWN;
      *bypass_code = UNKNOWN;
    }
}

/* Return cost of comparison done fcom + arithmetics operations on AX.
   All following functions do use number of instructions as a cost metrics.
   In future this should be tweaked to compute bytes for optimize_size and
   take into account performance of various instructions on various CPUs.  */
static int
ix86_fp_comparison_arithmetics_cost (enum rtx_code code)
{
  if (!TARGET_IEEE_FP)
    return 4;
  /* The cost of code output by ix86_expand_fp_compare.  */
  switch (code)
    {
    case UNLE:
    case UNLT:
    case LTGT:
    case GT:
    case GE:
    case UNORDERED:
    case ORDERED:
    case UNEQ:
      return 4;
      break;
    case LT:
    case NE:
    case EQ:
    case UNGE:
      return 5;
      break;
    case LE:
    case UNGT:
      return 6;
      break;
    default:
      gcc_unreachable ();
    }
}

/* Return cost of comparison done using fcomi operation.
   See ix86_fp_comparison_arithmetics_cost for the metrics.  */
static int
ix86_fp_comparison_fcomi_cost (enum rtx_code code)
{
  enum rtx_code bypass_code, first_code, second_code;
  /* Return arbitrarily high cost when instruction is not supported - this
     prevents gcc from using it.  */
  if (!TARGET_CMOVE)
    return 1024;
  ix86_fp_comparison_codes (code, &bypass_code, &first_code, &second_code);
  return (bypass_code != UNKNOWN || second_code != UNKNOWN) + 2;
}

/* Return cost of comparison done using sahf operation.
   See ix86_fp_comparison_arithmetics_cost for the metrics.  */
static int
ix86_fp_comparison_sahf_cost (enum rtx_code code)
{
  enum rtx_code bypass_code, first_code, second_code;
  /* Return arbitrarily high cost when instruction is not preferred - this
     avoids gcc from using it.  */
  if (!TARGET_USE_SAHF && !optimize_size)
    return 1024;
  ix86_fp_comparison_codes (code, &bypass_code, &first_code, &second_code);
  return (bypass_code != UNKNOWN || second_code != UNKNOWN) + 3;
}

/* Compute cost of the comparison done using any method.
   See ix86_fp_comparison_arithmetics_cost for the metrics.  */
static int
ix86_fp_comparison_cost (enum rtx_code code)
{
  int fcomi_cost, sahf_cost, arithmetics_cost = 1024;
  int min;

  fcomi_cost = ix86_fp_comparison_fcomi_cost (code);
  sahf_cost = ix86_fp_comparison_sahf_cost (code);

  min = arithmetics_cost = ix86_fp_comparison_arithmetics_cost (code);
  if (min > sahf_cost)
    min = sahf_cost;
  if (min > fcomi_cost)
    min = fcomi_cost;
  return min;
}

/* Generate insn patterns to do a floating point compare of OPERANDS.  */

static rtx
ix86_expand_fp_compare (enum rtx_code code, rtx op0, rtx op1, rtx scratch,
			rtx *second_test, rtx *bypass_test)
{
  enum machine_mode fpcmp_mode, intcmp_mode;
  rtx tmp, tmp2;
  int cost = ix86_fp_comparison_cost (code);
  enum rtx_code bypass_code, first_code, second_code;

  fpcmp_mode = ix86_fp_compare_mode (code);
  code = ix86_prepare_fp_compare_args (code, &op0, &op1);

  if (second_test)
    *second_test = NULL_RTX;
  if (bypass_test)
    *bypass_test = NULL_RTX;

  ix86_fp_comparison_codes (code, &bypass_code, &first_code, &second_code);

  /* Do fcomi/sahf based test when profitable.  */
  if ((bypass_code == UNKNOWN || bypass_test)
      && (second_code == UNKNOWN || second_test)
      && ix86_fp_comparison_arithmetics_cost (code) > cost)
    {
      if (TARGET_CMOVE)
	{
	  tmp = gen_rtx_COMPARE (fpcmp_mode, op0, op1);
	  tmp = gen_rtx_SET (VOIDmode, gen_rtx_REG (fpcmp_mode, FLAGS_REG),
			     tmp);
	  emit_insn (tmp);
	}
      else
	{
	  tmp = gen_rtx_COMPARE (fpcmp_mode, op0, op1);
	  tmp2 = gen_rtx_UNSPEC (HImode, gen_rtvec (1, tmp), UNSPEC_FNSTSW);
	  if (!scratch)
	    scratch = gen_reg_rtx (HImode);
	  emit_insn (gen_rtx_SET (VOIDmode, scratch, tmp2));
	  emit_insn (gen_x86_sahf_1 (scratch));
	}

      /* The FP codes work out to act like unsigned.  */
      intcmp_mode = fpcmp_mode;
      code = first_code;
      if (bypass_code != UNKNOWN)
	*bypass_test = gen_rtx_fmt_ee (bypass_code, VOIDmode,
				       gen_rtx_REG (intcmp_mode, FLAGS_REG),
				       const0_rtx);
      if (second_code != UNKNOWN)
	*second_test = gen_rtx_fmt_ee (second_code, VOIDmode,
				       gen_rtx_REG (intcmp_mode, FLAGS_REG),
				       const0_rtx);
    }
  else
    {
      /* Sadness wrt reg-stack pops killing fpsr -- gotta get fnstsw first.  */
      tmp = gen_rtx_COMPARE (fpcmp_mode, op0, op1);
      tmp2 = gen_rtx_UNSPEC (HImode, gen_rtvec (1, tmp), UNSPEC_FNSTSW);
      if (!scratch)
	scratch = gen_reg_rtx (HImode);
      emit_insn (gen_rtx_SET (VOIDmode, scratch, tmp2));

      /* In the unordered case, we have to check C2 for NaN's, which
	 doesn't happen to work out to anything nice combination-wise.
	 So do some bit twiddling on the value we've got in AH to come
	 up with an appropriate set of condition codes.  */

      intcmp_mode = CCNOmode;
      switch (code)
	{
	case GT:
	case UNGT:
	  if (code == GT || !TARGET_IEEE_FP)
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x45)));
	      code = EQ;
	    }
	  else
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_addqi_ext_1 (scratch, scratch, constm1_rtx));
	      emit_insn (gen_cmpqi_ext_3 (scratch, GEN_INT (0x44)));
	      intcmp_mode = CCmode;
	      code = GEU;
	    }
	  break;
	case LT:
	case UNLT:
	  if (code == LT && TARGET_IEEE_FP)
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_cmpqi_ext_3 (scratch, GEN_INT (0x01)));
	      intcmp_mode = CCmode;
	      code = EQ;
	    }
	  else
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x01)));
	      code = NE;
	    }
	  break;
	case GE:
	case UNGE:
	  if (code == GE || !TARGET_IEEE_FP)
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x05)));
	      code = EQ;
	    }
	  else
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_xorqi_cc_ext_1 (scratch, scratch,
					     GEN_INT (0x01)));
	      code = NE;
	    }
	  break;
	case LE:
	case UNLE:
	  if (code == LE && TARGET_IEEE_FP)
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_addqi_ext_1 (scratch, scratch, constm1_rtx));
	      emit_insn (gen_cmpqi_ext_3 (scratch, GEN_INT (0x40)));
	      intcmp_mode = CCmode;
	      code = LTU;
	    }
	  else
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x45)));
	      code = NE;
	    }
	  break;
	case EQ:
	case UNEQ:
	  if (code == EQ && TARGET_IEEE_FP)
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_cmpqi_ext_3 (scratch, GEN_INT (0x40)));
	      intcmp_mode = CCmode;
	      code = EQ;
	    }
	  else
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x40)));
	      code = NE;
	      break;
	    }
	  break;
	case NE:
	case LTGT:
	  if (code == NE && TARGET_IEEE_FP)
	    {
	      emit_insn (gen_andqi_ext_0 (scratch, scratch, GEN_INT (0x45)));
	      emit_insn (gen_xorqi_cc_ext_1 (scratch, scratch,
					     GEN_INT (0x40)));
	      code = NE;
	    }
	  else
	    {
	      emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x40)));
	      code = EQ;
	    }
	  break;

	case UNORDERED:
	  emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x04)));
	  code = NE;
	  break;
	case ORDERED:
	  emit_insn (gen_testqi_ext_ccno_0 (scratch, GEN_INT (0x04)));
	  code = EQ;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  /* Return the test that should be put into the flags user, i.e.
     the bcc, scc, or cmov instruction.  */
  return gen_rtx_fmt_ee (code, VOIDmode,
			 gen_rtx_REG (intcmp_mode, FLAGS_REG),
			 const0_rtx);
}

rtx
ix86_expand_compare (enum rtx_code code, rtx *second_test, rtx *bypass_test)
{
  rtx op0, op1, ret;
  op0 = ix86_compare_op0;
  op1 = ix86_compare_op1;

  if (second_test)
    *second_test = NULL_RTX;
  if (bypass_test)
    *bypass_test = NULL_RTX;

  if (ix86_compare_emitted)
    {
      ret = gen_rtx_fmt_ee (code, VOIDmode, ix86_compare_emitted, const0_rtx);
      ix86_compare_emitted = NULL_RTX;
    }
  else if (SCALAR_FLOAT_MODE_P (GET_MODE (op0)))
    ret = ix86_expand_fp_compare (code, op0, op1, NULL_RTX,
				  second_test, bypass_test);
  else
    ret = ix86_expand_int_compare (code, op0, op1);

  return ret;
}

/* Return true if the CODE will result in nontrivial jump sequence.  */
bool
ix86_fp_jump_nontrivial_p (enum rtx_code code)
{
  enum rtx_code bypass_code, first_code, second_code;
  if (!TARGET_CMOVE)
    return true;
  ix86_fp_comparison_codes (code, &bypass_code, &first_code, &second_code);
  return bypass_code != UNKNOWN || second_code != UNKNOWN;
}

void
ix86_expand_branch (enum rtx_code code, rtx label)
{
  rtx tmp;

  /* If we have emitted a compare insn, go straight to simple.
     ix86_expand_compare won't emit anything if ix86_compare_emitted
     is non NULL.  */
  if (ix86_compare_emitted)
    goto simple;

  switch (GET_MODE (ix86_compare_op0))
    {
    case QImode:
    case HImode:
    case SImode:
      simple:
      tmp = ix86_expand_compare (code, NULL, NULL);
      tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
				  gen_rtx_LABEL_REF (VOIDmode, label),
				  pc_rtx);
      emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, tmp));
      return;

    case SFmode:
    case DFmode:
    case XFmode:
      {
	rtvec vec;
	int use_fcomi;
	enum rtx_code bypass_code, first_code, second_code;

	code = ix86_prepare_fp_compare_args (code, &ix86_compare_op0,
					     &ix86_compare_op1);

	ix86_fp_comparison_codes (code, &bypass_code, &first_code, &second_code);

	/* Check whether we will use the natural sequence with one jump.  If
	   so, we can expand jump early.  Otherwise delay expansion by
	   creating compound insn to not confuse optimizers.  */
	if (bypass_code == UNKNOWN && second_code == UNKNOWN
	    && TARGET_CMOVE)
	  {
	    ix86_split_fp_branch (code, ix86_compare_op0, ix86_compare_op1,
				  gen_rtx_LABEL_REF (VOIDmode, label),
				  pc_rtx, NULL_RTX, NULL_RTX);
	  }
	else
	  {
	    tmp = gen_rtx_fmt_ee (code, VOIDmode,
				  ix86_compare_op0, ix86_compare_op1);
	    tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
					gen_rtx_LABEL_REF (VOIDmode, label),
					pc_rtx);
	    tmp = gen_rtx_SET (VOIDmode, pc_rtx, tmp);

	    use_fcomi = ix86_use_fcomi_compare (code);
	    vec = rtvec_alloc (3 + !use_fcomi);
	    RTVEC_ELT (vec, 0) = tmp;
	    RTVEC_ELT (vec, 1)
	      = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCFPmode, 18));
	    RTVEC_ELT (vec, 2)
	      = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCFPmode, 17));
	    if (! use_fcomi)
	      RTVEC_ELT (vec, 3)
		= gen_rtx_CLOBBER (VOIDmode, gen_rtx_SCRATCH (HImode));

	    emit_jump_insn (gen_rtx_PARALLEL (VOIDmode, vec));
	  }
	return;
      }

    case DImode:
      if (TARGET_64BIT)
	goto simple;
    case TImode:
      /* Expand DImode branch into multiple compare+branch.  */
      {
	rtx lo[2], hi[2], label2;
	enum rtx_code code1, code2, code3;
	enum machine_mode submode;

	if (CONSTANT_P (ix86_compare_op0) && ! CONSTANT_P (ix86_compare_op1))
	  {
	    tmp = ix86_compare_op0;
	    ix86_compare_op0 = ix86_compare_op1;
	    ix86_compare_op1 = tmp;
	    code = swap_condition (code);
	  }
	if (GET_MODE (ix86_compare_op0) == DImode)
	  {
	    split_di (&ix86_compare_op0, 1, lo+0, hi+0);
	    split_di (&ix86_compare_op1, 1, lo+1, hi+1);
	    submode = SImode;
	  }
	else
	  {
	    split_ti (&ix86_compare_op0, 1, lo+0, hi+0);
	    split_ti (&ix86_compare_op1, 1, lo+1, hi+1);
	    submode = DImode;
	  }

	/* When comparing for equality, we can use (hi0^hi1)|(lo0^lo1) to
	   avoid two branches.  This costs one extra insn, so disable when
	   optimizing for size.  */

	if ((code == EQ || code == NE)
	    && (!optimize_size
	        || hi[1] == const0_rtx || lo[1] == const0_rtx))
	  {
	    rtx xor0, xor1;

	    xor1 = hi[0];
	    if (hi[1] != const0_rtx)
	      xor1 = expand_binop (submode, xor_optab, xor1, hi[1],
				   NULL_RTX, 0, OPTAB_WIDEN);

	    xor0 = lo[0];
	    if (lo[1] != const0_rtx)
	      xor0 = expand_binop (submode, xor_optab, xor0, lo[1],
				   NULL_RTX, 0, OPTAB_WIDEN);

	    tmp = expand_binop (submode, ior_optab, xor1, xor0,
				NULL_RTX, 0, OPTAB_WIDEN);

	    ix86_compare_op0 = tmp;
	    ix86_compare_op1 = const0_rtx;
	    ix86_expand_branch (code, label);
	    return;
	  }

	/* Otherwise, if we are doing less-than or greater-or-equal-than,
	   op1 is a constant and the low word is zero, then we can just
	   examine the high word.  */

	if (GET_CODE (hi[1]) == CONST_INT && lo[1] == const0_rtx)
	  switch (code)
	    {
	    case LT: case LTU: case GE: case GEU:
	      ix86_compare_op0 = hi[0];
	      ix86_compare_op1 = hi[1];
	      ix86_expand_branch (code, label);
	      return;
	    default:
	      break;
	    }

	/* Otherwise, we need two or three jumps.  */

	label2 = gen_label_rtx ();

	code1 = code;
	code2 = swap_condition (code);
	code3 = unsigned_condition (code);

	switch (code)
	  {
	  case LT: case GT: case LTU: case GTU:
	    break;

	  case LE:   code1 = LT;  code2 = GT;  break;
	  case GE:   code1 = GT;  code2 = LT;  break;
	  case LEU:  code1 = LTU; code2 = GTU; break;
	  case GEU:  code1 = GTU; code2 = LTU; break;

	  case EQ:   code1 = UNKNOWN; code2 = NE;  break;
	  case NE:   code2 = UNKNOWN; break;

	  default:
	    gcc_unreachable ();
	  }

	/*
	 * a < b =>
	 *    if (hi(a) < hi(b)) goto true;
	 *    if (hi(a) > hi(b)) goto false;
	 *    if (lo(a) < lo(b)) goto true;
	 *  false:
	 */

	ix86_compare_op0 = hi[0];
	ix86_compare_op1 = hi[1];

	if (code1 != UNKNOWN)
	  ix86_expand_branch (code1, label);
	if (code2 != UNKNOWN)
	  ix86_expand_branch (code2, label2);

	ix86_compare_op0 = lo[0];
	ix86_compare_op1 = lo[1];
	ix86_expand_branch (code3, label);

	if (code2 != UNKNOWN)
	  emit_label (label2);
	return;
      }

    default:
      gcc_unreachable ();
    }
}

/* Split branch based on floating point condition.  */
void
ix86_split_fp_branch (enum rtx_code code, rtx op1, rtx op2,
		      rtx target1, rtx target2, rtx tmp, rtx pushed)
{
  rtx second, bypass;
  rtx label = NULL_RTX;
  rtx condition;
  int bypass_probability = -1, second_probability = -1, probability = -1;
  rtx i;

  if (target2 != pc_rtx)
    {
      rtx tmp = target2;
      code = reverse_condition_maybe_unordered (code);
      target2 = target1;
      target1 = tmp;
    }

  condition = ix86_expand_fp_compare (code, op1, op2,
				      tmp, &second, &bypass);

  /* Remove pushed operand from stack.  */
  if (pushed)
    ix86_free_from_memory (GET_MODE (pushed));

  if (split_branch_probability >= 0)
    {
      /* Distribute the probabilities across the jumps.
	 Assume the BYPASS and SECOND to be always test
	 for UNORDERED.  */
      probability = split_branch_probability;

      /* Value of 1 is low enough to make no need for probability
	 to be updated.  Later we may run some experiments and see
	 if unordered values are more frequent in practice.  */
      if (bypass)
	bypass_probability = 1;
      if (second)
	second_probability = 1;
    }
  if (bypass != NULL_RTX)
    {
      label = gen_label_rtx ();
      i = emit_jump_insn (gen_rtx_SET
			  (VOIDmode, pc_rtx,
			   gen_rtx_IF_THEN_ELSE (VOIDmode,
						 bypass,
						 gen_rtx_LABEL_REF (VOIDmode,
								    label),
						 pc_rtx)));
      if (bypass_probability >= 0)
	REG_NOTES (i)
	  = gen_rtx_EXPR_LIST (REG_BR_PROB,
			       GEN_INT (bypass_probability),
			       REG_NOTES (i));
    }
  i = emit_jump_insn (gen_rtx_SET
		      (VOIDmode, pc_rtx,
		       gen_rtx_IF_THEN_ELSE (VOIDmode,
					     condition, target1, target2)));
  if (probability >= 0)
    REG_NOTES (i)
      = gen_rtx_EXPR_LIST (REG_BR_PROB,
			   GEN_INT (probability),
			   REG_NOTES (i));
  if (second != NULL_RTX)
    {
      i = emit_jump_insn (gen_rtx_SET
			  (VOIDmode, pc_rtx,
			   gen_rtx_IF_THEN_ELSE (VOIDmode, second, target1,
						 target2)));
      if (second_probability >= 0)
	REG_NOTES (i)
	  = gen_rtx_EXPR_LIST (REG_BR_PROB,
			       GEN_INT (second_probability),
			       REG_NOTES (i));
    }
  if (label != NULL_RTX)
    emit_label (label);
}

int
ix86_expand_setcc (enum rtx_code code, rtx dest)
{
  rtx ret, tmp, tmpreg, equiv;
  rtx second_test, bypass_test;

  if (GET_MODE (ix86_compare_op0) == (TARGET_64BIT ? TImode : DImode))
    return 0; /* FAIL */

  gcc_assert (GET_MODE (dest) == QImode);

  ret = ix86_expand_compare (code, &second_test, &bypass_test);
  PUT_MODE (ret, QImode);

  tmp = dest;
  tmpreg = dest;

  emit_insn (gen_rtx_SET (VOIDmode, tmp, ret));
  if (bypass_test || second_test)
    {
      rtx test = second_test;
      int bypass = 0;
      rtx tmp2 = gen_reg_rtx (QImode);
      if (bypass_test)
	{
	  gcc_assert (!second_test);
	  test = bypass_test;
	  bypass = 1;
	  PUT_CODE (test, reverse_condition_maybe_unordered (GET_CODE (test)));
	}
      PUT_MODE (test, QImode);
      emit_insn (gen_rtx_SET (VOIDmode, tmp2, test));

      if (bypass)
	emit_insn (gen_andqi3 (tmp, tmpreg, tmp2));
      else
	emit_insn (gen_iorqi3 (tmp, tmpreg, tmp2));
    }

  /* Attach a REG_EQUAL note describing the comparison result.  */
  if (ix86_compare_op0 && ix86_compare_op1)
    {
      equiv = simplify_gen_relational (code, QImode,
				       GET_MODE (ix86_compare_op0),
				       ix86_compare_op0, ix86_compare_op1);
      set_unique_reg_note (get_last_insn (), REG_EQUAL, equiv);
    }

  return 1; /* DONE */
}

/* Expand comparison setting or clearing carry flag.  Return true when
   successful and set pop for the operation.  */
static bool
ix86_expand_carry_flag_compare (enum rtx_code code, rtx op0, rtx op1, rtx *pop)
{
  enum machine_mode mode =
    GET_MODE (op0) != VOIDmode ? GET_MODE (op0) : GET_MODE (op1);

  /* Do not handle DImode compares that go through special path.  Also we can't
     deal with FP compares yet.  This is possible to add.  */
  if (mode == (TARGET_64BIT ? TImode : DImode))
    return false;
  if (FLOAT_MODE_P (mode))
    {
      rtx second_test = NULL, bypass_test = NULL;
      rtx compare_op, compare_seq;

      /* Shortcut:  following common codes never translate into carry flag compares.  */
      if (code == EQ || code == NE || code == UNEQ || code == LTGT
	  || code == ORDERED || code == UNORDERED)
	return false;

      /* These comparisons require zero flag; swap operands so they won't.  */
      if ((code == GT || code == UNLE || code == LE || code == UNGT)
	  && !TARGET_IEEE_FP)
	{
	  rtx tmp = op0;
	  op0 = op1;
	  op1 = tmp;
	  code = swap_condition (code);
	}

      /* Try to expand the comparison and verify that we end up with carry flag
	 based comparison.  This is fails to be true only when we decide to expand
	 comparison using arithmetic that is not too common scenario.  */
      start_sequence ();
      compare_op = ix86_expand_fp_compare (code, op0, op1, NULL_RTX,
					   &second_test, &bypass_test);
      compare_seq = get_insns ();
      end_sequence ();

      if (second_test || bypass_test)
	return false;
      if (GET_MODE (XEXP (compare_op, 0)) == CCFPmode
	  || GET_MODE (XEXP (compare_op, 0)) == CCFPUmode)
        code = ix86_fp_compare_code_to_integer (GET_CODE (compare_op));
      else
	code = GET_CODE (compare_op);
      if (code != LTU && code != GEU)
	return false;
      emit_insn (compare_seq);
      *pop = compare_op;
      return true;
    }
  if (!INTEGRAL_MODE_P (mode))
    return false;
  switch (code)
    {
    case LTU:
    case GEU:
      break;

    /* Convert a==0 into (unsigned)a<1.  */
    case EQ:
    case NE:
      if (op1 != const0_rtx)
	return false;
      op1 = const1_rtx;
      code = (code == EQ ? LTU : GEU);
      break;

    /* Convert a>b into b<a or a>=b-1.  */
    case GTU:
    case LEU:
      if (GET_CODE (op1) == CONST_INT)
	{
	  op1 = gen_int_mode (INTVAL (op1) + 1, GET_MODE (op0));
	  /* Bail out on overflow.  We still can swap operands but that
	     would force loading of the constant into register.  */
	  if (op1 == const0_rtx
	      || !x86_64_immediate_operand (op1, GET_MODE (op1)))
	    return false;
	  code = (code == GTU ? GEU : LTU);
	}
      else
	{
	  rtx tmp = op1;
	  op1 = op0;
	  op0 = tmp;
	  code = (code == GTU ? LTU : GEU);
	}
      break;

    /* Convert a>=0 into (unsigned)a<0x80000000.  */
    case LT:
    case GE:
      if (mode == DImode || op1 != const0_rtx)
	return false;
      op1 = gen_int_mode (1 << (GET_MODE_BITSIZE (mode) - 1), mode);
      code = (code == LT ? GEU : LTU);
      break;
    case LE:
    case GT:
      if (mode == DImode || op1 != constm1_rtx)
	return false;
      op1 = gen_int_mode (1 << (GET_MODE_BITSIZE (mode) - 1), mode);
      code = (code == LE ? GEU : LTU);
      break;

    default:
      return false;
    }
  /* Swapping operands may cause constant to appear as first operand.  */
  if (!nonimmediate_operand (op0, VOIDmode))
    {
      if (no_new_pseudos)
	return false;
      op0 = force_reg (mode, op0);
    }
  ix86_compare_op0 = op0;
  ix86_compare_op1 = op1;
  *pop = ix86_expand_compare (code, NULL, NULL);
  gcc_assert (GET_CODE (*pop) == LTU || GET_CODE (*pop) == GEU);
  return true;
}

int
ix86_expand_int_movcc (rtx operands[])
{
  enum rtx_code code = GET_CODE (operands[1]), compare_code;
  rtx compare_seq, compare_op;
  rtx second_test, bypass_test;
  enum machine_mode mode = GET_MODE (operands[0]);
  bool sign_bit_compare_p = false;;

  start_sequence ();
  compare_op = ix86_expand_compare (code, &second_test, &bypass_test);
  compare_seq = get_insns ();
  end_sequence ();

  compare_code = GET_CODE (compare_op);

  if ((ix86_compare_op1 == const0_rtx && (code == GE || code == LT))
      || (ix86_compare_op1 == constm1_rtx && (code == GT || code == LE)))
    sign_bit_compare_p = true;

  /* Don't attempt mode expansion here -- if we had to expand 5 or 6
     HImode insns, we'd be swallowed in word prefix ops.  */

  if ((mode != HImode || TARGET_FAST_PREFIX)
      && (mode != (TARGET_64BIT ? TImode : DImode))
      && GET_CODE (operands[2]) == CONST_INT
      && GET_CODE (operands[3]) == CONST_INT)
    {
      rtx out = operands[0];
      HOST_WIDE_INT ct = INTVAL (operands[2]);
      HOST_WIDE_INT cf = INTVAL (operands[3]);
      HOST_WIDE_INT diff;

      diff = ct - cf;
      /*  Sign bit compares are better done using shifts than we do by using
	  sbb.  */
      if (sign_bit_compare_p
	  || ix86_expand_carry_flag_compare (code, ix86_compare_op0,
					     ix86_compare_op1, &compare_op))
	{
	  /* Detect overlap between destination and compare sources.  */
	  rtx tmp = out;

          if (!sign_bit_compare_p)
	    {
	      bool fpcmp = false;

	      compare_code = GET_CODE (compare_op);

	      if (GET_MODE (XEXP (compare_op, 0)) == CCFPmode
		  || GET_MODE (XEXP (compare_op, 0)) == CCFPUmode)
		{
		  fpcmp = true;
		  compare_code = ix86_fp_compare_code_to_integer (compare_code);
		}

	      /* To simplify rest of code, restrict to the GEU case.  */
	      if (compare_code == LTU)
		{
		  HOST_WIDE_INT tmp = ct;
		  ct = cf;
		  cf = tmp;
		  compare_code = reverse_condition (compare_code);
		  code = reverse_condition (code);
		}
	      else
		{
		  if (fpcmp)
		    PUT_CODE (compare_op,
			      reverse_condition_maybe_unordered
			        (GET_CODE (compare_op)));
		  else
		    PUT_CODE (compare_op, reverse_condition (GET_CODE (compare_op)));
		}
	      diff = ct - cf;

	      if (reg_overlap_mentioned_p (out, ix86_compare_op0)
		  || reg_overlap_mentioned_p (out, ix86_compare_op1))
		tmp = gen_reg_rtx (mode);

	      if (mode == DImode)
		emit_insn (gen_x86_movdicc_0_m1_rex64 (tmp, compare_op));
	      else
		emit_insn (gen_x86_movsicc_0_m1 (gen_lowpart (SImode, tmp), compare_op));
	    }
	  else
	    {
	      if (code == GT || code == GE)
		code = reverse_condition (code);
	      else
		{
		  HOST_WIDE_INT tmp = ct;
		  ct = cf;
		  cf = tmp;
		  diff = ct - cf;
		}
	      tmp = emit_store_flag (tmp, code, ix86_compare_op0,
				     ix86_compare_op1, VOIDmode, 0, -1);
	    }

	  if (diff == 1)
	    {
	      /*
	       * cmpl op0,op1
	       * sbbl dest,dest
	       * [addl dest, ct]
	       *
	       * Size 5 - 8.
	       */
	      if (ct)
		tmp = expand_simple_binop (mode, PLUS,
					   tmp, GEN_INT (ct),
					   copy_rtx (tmp), 1, OPTAB_DIRECT);
	    }
	  else if (cf == -1)
	    {
	      /*
	       * cmpl op0,op1
	       * sbbl dest,dest
	       * orl $ct, dest
	       *
	       * Size 8.
	       */
	      tmp = expand_simple_binop (mode, IOR,
					 tmp, GEN_INT (ct),
					 copy_rtx (tmp), 1, OPTAB_DIRECT);
	    }
	  else if (diff == -1 && ct)
	    {
	      /*
	       * cmpl op0,op1
	       * sbbl dest,dest
	       * notl dest
	       * [addl dest, cf]
	       *
	       * Size 8 - 11.
	       */
	      tmp = expand_simple_unop (mode, NOT, tmp, copy_rtx (tmp), 1);
	      if (cf)
		tmp = expand_simple_binop (mode, PLUS,
					   copy_rtx (tmp), GEN_INT (cf),
					   copy_rtx (tmp), 1, OPTAB_DIRECT);
	    }
	  else
	    {
	      /*
	       * cmpl op0,op1
	       * sbbl dest,dest
	       * [notl dest]
	       * andl cf - ct, dest
	       * [addl dest, ct]
	       *
	       * Size 8 - 11.
	       */

	      if (cf == 0)
		{
		  cf = ct;
		  ct = 0;
		  tmp = expand_simple_unop (mode, NOT, tmp, copy_rtx (tmp), 1);
		}

	      tmp = expand_simple_binop (mode, AND,
					 copy_rtx (tmp),
					 gen_int_mode (cf - ct, mode),
					 copy_rtx (tmp), 1, OPTAB_DIRECT);
	      if (ct)
		tmp = expand_simple_binop (mode, PLUS,
					   copy_rtx (tmp), GEN_INT (ct),
					   copy_rtx (tmp), 1, OPTAB_DIRECT);
	    }

	  if (!rtx_equal_p (tmp, out))
	    emit_move_insn (copy_rtx (out), copy_rtx (tmp));

	  return 1; /* DONE */
	}

      if (diff < 0)
	{
	  HOST_WIDE_INT tmp;
	  tmp = ct, ct = cf, cf = tmp;
	  diff = -diff;
	  if (FLOAT_MODE_P (GET_MODE (ix86_compare_op0)))
	    {
	      /* We may be reversing unordered compare to normal compare, that
		 is not valid in general (we may convert non-trapping condition
		 to trapping one), however on i386 we currently emit all
		 comparisons unordered.  */
	      compare_code = reverse_condition_maybe_unordered (compare_code);
	      code = reverse_condition_maybe_unordered (code);
	    }
	  else
	    {
	      compare_code = reverse_condition (compare_code);
	      code = reverse_condition (code);
	    }
	}

      compare_code = UNKNOWN;
      if (GET_MODE_CLASS (GET_MODE (ix86_compare_op0)) == MODE_INT
	  && GET_CODE (ix86_compare_op1) == CONST_INT)
	{
	  if (ix86_compare_op1 == const0_rtx
	      && (code == LT || code == GE))
	    compare_code = code;
	  else if (ix86_compare_op1 == constm1_rtx)
	    {
	      if (code == LE)
		compare_code = LT;
	      else if (code == GT)
		compare_code = GE;
	    }
	}

      /* Optimize dest = (op0 < 0) ? -1 : cf.  */
      if (compare_code != UNKNOWN
	  && GET_MODE (ix86_compare_op0) == GET_MODE (out)
	  && (cf == -1 || ct == -1))
	{
	  /* If lea code below could be used, only optimize
	     if it results in a 2 insn sequence.  */

	  if (! (diff == 1 || diff == 2 || diff == 4 || diff == 8
		 || diff == 3 || diff == 5 || diff == 9)
	      || (compare_code == LT && ct == -1)
	      || (compare_code == GE && cf == -1))
	    {
	      /*
	       * notl op1	(if necessary)
	       * sarl $31, op1
	       * orl cf, op1
	       */
	      if (ct != -1)
		{
		  cf = ct;
		  ct = -1;
		  code = reverse_condition (code);
		}

	      out = emit_store_flag (out, code, ix86_compare_op0,
				     ix86_compare_op1, VOIDmode, 0, -1);

	      out = expand_simple_binop (mode, IOR,
					 out, GEN_INT (cf),
					 out, 1, OPTAB_DIRECT);
	      if (out != operands[0])
		emit_move_insn (operands[0], out);

	      return 1; /* DONE */
	    }
	}


      if ((diff == 1 || diff == 2 || diff == 4 || diff == 8
	   || diff == 3 || diff == 5 || diff == 9)
	  && ((mode != QImode && mode != HImode) || !TARGET_PARTIAL_REG_STALL)
	  && (mode != DImode
	      || x86_64_immediate_operand (GEN_INT (cf), VOIDmode)))
	{
	  /*
	   * xorl dest,dest
	   * cmpl op1,op2
	   * setcc dest
	   * lea cf(dest*(ct-cf)),dest
	   *
	   * Size 14.
	   *
	   * This also catches the degenerate setcc-only case.
	   */

	  rtx tmp;
	  int nops;

	  out = emit_store_flag (out, code, ix86_compare_op0,
				 ix86_compare_op1, VOIDmode, 0, 1);

	  nops = 0;
	  /* On x86_64 the lea instruction operates on Pmode, so we need
	     to get arithmetics done in proper mode to match.  */
	  if (diff == 1)
	    tmp = copy_rtx (out);
	  else
	    {
	      rtx out1;
	      out1 = copy_rtx (out);
	      tmp = gen_rtx_MULT (mode, out1, GEN_INT (diff & ~1));
	      nops++;
	      if (diff & 1)
		{
		  tmp = gen_rtx_PLUS (mode, tmp, out1);
		  nops++;
		}
	    }
	  if (cf != 0)
	    {
	      tmp = gen_rtx_PLUS (mode, tmp, GEN_INT (cf));
	      nops++;
	    }
	  if (!rtx_equal_p (tmp, out))
	    {
	      if (nops == 1)
		out = force_operand (tmp, copy_rtx (out));
	      else
		emit_insn (gen_rtx_SET (VOIDmode, copy_rtx (out), copy_rtx (tmp)));
	    }
	  if (!rtx_equal_p (out, operands[0]))
	    emit_move_insn (operands[0], copy_rtx (out));

	  return 1; /* DONE */
	}

      /*
       * General case:			Jumpful:
       *   xorl dest,dest		cmpl op1, op2
       *   cmpl op1, op2		movl ct, dest
       *   setcc dest			jcc 1f
       *   decl dest			movl cf, dest
       *   andl (cf-ct),dest		1:
       *   addl ct,dest
       *
       * Size 20.			Size 14.
       *
       * This is reasonably steep, but branch mispredict costs are
       * high on modern cpus, so consider failing only if optimizing
       * for space.
       */

      if ((!TARGET_CMOVE || (mode == QImode && TARGET_PARTIAL_REG_STALL))
	  && BRANCH_COST >= 2)
	{
	  if (cf == 0)
	    {
	      cf = ct;
	      ct = 0;
	      if (FLOAT_MODE_P (GET_MODE (ix86_compare_op0)))
		/* We may be reversing unordered compare to normal compare,
		   that is not valid in general (we may convert non-trapping
		   condition to trapping one), however on i386 we currently
		   emit all comparisons unordered.  */
		code = reverse_condition_maybe_unordered (code);
	      else
		{
		  code = reverse_condition (code);
		  if (compare_code != UNKNOWN)
		    compare_code = reverse_condition (compare_code);
		}
	    }

	  if (compare_code != UNKNOWN)
	    {
	      /* notl op1	(if needed)
		 sarl $31, op1
		 andl (cf-ct), op1
		 addl ct, op1

		 For x < 0 (resp. x <= -1) there will be no notl,
		 so if possible swap the constants to get rid of the
		 complement.
		 True/false will be -1/0 while code below (store flag
		 followed by decrement) is 0/-1, so the constants need
		 to be exchanged once more.  */

	      if (compare_code == GE || !cf)
		{
		  code = reverse_condition (code);
		  compare_code = LT;
		}
	      else
		{
		  HOST_WIDE_INT tmp = cf;
		  cf = ct;
		  ct = tmp;
		}

	      out = emit_store_flag (out, code, ix86_compare_op0,
				     ix86_compare_op1, VOIDmode, 0, -1);
	    }
	  else
	    {
	      out = emit_store_flag (out, code, ix86_compare_op0,
				     ix86_compare_op1, VOIDmode, 0, 1);

	      out = expand_simple_binop (mode, PLUS, copy_rtx (out), constm1_rtx,
					 copy_rtx (out), 1, OPTAB_DIRECT);
	    }

	  out = expand_simple_binop (mode, AND, copy_rtx (out),
				     gen_int_mode (cf - ct, mode),
				     copy_rtx (out), 1, OPTAB_DIRECT);
	  if (ct)
	    out = expand_simple_binop (mode, PLUS, copy_rtx (out), GEN_INT (ct),
				       copy_rtx (out), 1, OPTAB_DIRECT);
	  if (!rtx_equal_p (out, operands[0]))
	    emit_move_insn (operands[0], copy_rtx (out));

	  return 1; /* DONE */
	}
    }

  if (!TARGET_CMOVE || (mode == QImode && TARGET_PARTIAL_REG_STALL))
    {
      /* Try a few things more with specific constants and a variable.  */

      optab op;
      rtx var, orig_out, out, tmp;

      if (BRANCH_COST <= 2)
	return 0; /* FAIL */

      /* If one of the two operands is an interesting constant, load a
	 constant with the above and mask it in with a logical operation.  */

      if (GET_CODE (operands[2]) == CONST_INT)
	{
	  var = operands[3];
	  if (INTVAL (operands[2]) == 0 && operands[3] != constm1_rtx)
	    operands[3] = constm1_rtx, op = and_optab;
	  else if (INTVAL (operands[2]) == -1 && operands[3] != const0_rtx)
	    operands[3] = const0_rtx, op = ior_optab;
	  else
	    return 0; /* FAIL */
	}
      else if (GET_CODE (operands[3]) == CONST_INT)
	{
	  var = operands[2];
	  if (INTVAL (operands[3]) == 0 && operands[2] != constm1_rtx)
	    operands[2] = constm1_rtx, op = and_optab;
	  else if (INTVAL (operands[3]) == -1 && operands[3] != const0_rtx)
	    operands[2] = const0_rtx, op = ior_optab;
	  else
	    return 0; /* FAIL */
	}
      else
        return 0; /* FAIL */

      orig_out = operands[0];
      tmp = gen_reg_rtx (mode);
      operands[0] = tmp;

      /* Recurse to get the constant loaded.  */
      if (ix86_expand_int_movcc (operands) == 0)
        return 0; /* FAIL */

      /* Mask in the interesting variable.  */
      out = expand_binop (mode, op, var, tmp, orig_out, 0,
			  OPTAB_WIDEN);
      if (!rtx_equal_p (out, orig_out))
	emit_move_insn (copy_rtx (orig_out), copy_rtx (out));

      return 1; /* DONE */
    }

  /*
   * For comparison with above,
   *
   * movl cf,dest
   * movl ct,tmp
   * cmpl op1,op2
   * cmovcc tmp,dest
   *
   * Size 15.
   */

  if (! nonimmediate_operand (operands[2], mode))
    operands[2] = force_reg (mode, operands[2]);
  if (! nonimmediate_operand (operands[3], mode))
    operands[3] = force_reg (mode, operands[3]);

  if (bypass_test && reg_overlap_mentioned_p (operands[0], operands[3]))
    {
      rtx tmp = gen_reg_rtx (mode);
      emit_move_insn (tmp, operands[3]);
      operands[3] = tmp;
    }
  if (second_test && reg_overlap_mentioned_p (operands[0], operands[2]))
    {
      rtx tmp = gen_reg_rtx (mode);
      emit_move_insn (tmp, operands[2]);
      operands[2] = tmp;
    }

  if (! register_operand (operands[2], VOIDmode)
      && (mode == QImode
          || ! register_operand (operands[3], VOIDmode)))
    operands[2] = force_reg (mode, operands[2]);

  if (mode == QImode
      && ! register_operand (operands[3], VOIDmode))
    operands[3] = force_reg (mode, operands[3]);

  emit_insn (compare_seq);
  emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			  gen_rtx_IF_THEN_ELSE (mode,
						compare_op, operands[2],
						operands[3])));
  if (bypass_test)
    emit_insn (gen_rtx_SET (VOIDmode, copy_rtx (operands[0]),
			    gen_rtx_IF_THEN_ELSE (mode,
				  bypass_test,
				  copy_rtx (operands[3]),
				  copy_rtx (operands[0]))));
  if (second_test)
    emit_insn (gen_rtx_SET (VOIDmode, copy_rtx (operands[0]),
			    gen_rtx_IF_THEN_ELSE (mode,
				  second_test,
				  copy_rtx (operands[2]),
				  copy_rtx (operands[0]))));

  return 1; /* DONE */
}

/* Swap, force into registers, or otherwise massage the two operands
   to an sse comparison with a mask result.  Thus we differ a bit from
   ix86_prepare_fp_compare_args which expects to produce a flags result.

   The DEST operand exists to help determine whether to commute commutative
   operators.  The POP0/POP1 operands are updated in place.  The new
   comparison code is returned, or UNKNOWN if not implementable.  */

static enum rtx_code
ix86_prepare_sse_fp_compare_args (rtx dest, enum rtx_code code,
				  rtx *pop0, rtx *pop1)
{
  rtx tmp;

  switch (code)
    {
    case LTGT:
    case UNEQ:
      /* We have no LTGT as an operator.  We could implement it with
	 NE & ORDERED, but this requires an extra temporary.  It's
	 not clear that it's worth it.  */
      return UNKNOWN;

    case LT:
    case LE:
    case UNGT:
    case UNGE:
      /* These are supported directly.  */
      break;

    case EQ:
    case NE:
    case UNORDERED:
    case ORDERED:
      /* For commutative operators, try to canonicalize the destination
	 operand to be first in the comparison - this helps reload to
	 avoid extra moves.  */
      if (!dest || !rtx_equal_p (dest, *pop1))
	break;
      /* FALLTHRU */

    case GE:
    case GT:
    case UNLE:
    case UNLT:
      /* These are not supported directly.  Swap the comparison operands
	 to transform into something that is supported.  */
      tmp = *pop0;
      *pop0 = *pop1;
      *pop1 = tmp;
      code = swap_condition (code);
      break;

    default:
      gcc_unreachable ();
    }

  return code;
}

/* Detect conditional moves that exactly match min/max operational
   semantics.  Note that this is IEEE safe, as long as we don't
   interchange the operands.

   Returns FALSE if this conditional move doesn't match a MIN/MAX,
   and TRUE if the operation is successful and instructions are emitted.  */

static bool
ix86_expand_sse_fp_minmax (rtx dest, enum rtx_code code, rtx cmp_op0,
			   rtx cmp_op1, rtx if_true, rtx if_false)
{
  enum machine_mode mode;
  bool is_min;
  rtx tmp;

  if (code == LT)
    ;
  else if (code == UNGE)
    {
      tmp = if_true;
      if_true = if_false;
      if_false = tmp;
    }
  else
    return false;

  if (rtx_equal_p (cmp_op0, if_true) && rtx_equal_p (cmp_op1, if_false))
    is_min = true;
  else if (rtx_equal_p (cmp_op1, if_true) && rtx_equal_p (cmp_op0, if_false))
    is_min = false;
  else
    return false;

  mode = GET_MODE (dest);

  /* We want to check HONOR_NANS and HONOR_SIGNED_ZEROS here,
     but MODE may be a vector mode and thus not appropriate.  */
  if (!flag_finite_math_only || !flag_unsafe_math_optimizations)
    {
      int u = is_min ? UNSPEC_IEEE_MIN : UNSPEC_IEEE_MAX;
      rtvec v;

      if_true = force_reg (mode, if_true);
      v = gen_rtvec (2, if_true, if_false);
      tmp = gen_rtx_UNSPEC (mode, v, u);
    }
  else
    {
      code = is_min ? SMIN : SMAX;
      tmp = gen_rtx_fmt_ee (code, mode, if_true, if_false);
    }

  emit_insn (gen_rtx_SET (VOIDmode, dest, tmp));
  return true;
}

/* Expand an sse vector comparison.  Return the register with the result.  */

static rtx
ix86_expand_sse_cmp (rtx dest, enum rtx_code code, rtx cmp_op0, rtx cmp_op1,
		     rtx op_true, rtx op_false)
{
  enum machine_mode mode = GET_MODE (dest);
  rtx x;

  cmp_op0 = force_reg (mode, cmp_op0);
  if (!nonimmediate_operand (cmp_op1, mode))
    cmp_op1 = force_reg (mode, cmp_op1);

  if (optimize
      || reg_overlap_mentioned_p (dest, op_true)
      || reg_overlap_mentioned_p (dest, op_false))
    dest = gen_reg_rtx (mode);

  x = gen_rtx_fmt_ee (code, mode, cmp_op0, cmp_op1);
  emit_insn (gen_rtx_SET (VOIDmode, dest, x));

  return dest;
}

/* Expand DEST = CMP ? OP_TRUE : OP_FALSE into a sequence of logical
   operations.  This is used for both scalar and vector conditional moves.  */

static void
ix86_expand_sse_movcc (rtx dest, rtx cmp, rtx op_true, rtx op_false)
{
  enum machine_mode mode = GET_MODE (dest);
  rtx t2, t3, x;

  if (op_false == CONST0_RTX (mode))
    {
      op_true = force_reg (mode, op_true);
      x = gen_rtx_AND (mode, cmp, op_true);
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }
  else if (op_true == CONST0_RTX (mode))
    {
      op_false = force_reg (mode, op_false);
      x = gen_rtx_NOT (mode, cmp);
      x = gen_rtx_AND (mode, x, op_false);
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }
  else
    {
      op_true = force_reg (mode, op_true);
      op_false = force_reg (mode, op_false);

      t2 = gen_reg_rtx (mode);
      if (optimize)
	t3 = gen_reg_rtx (mode);
      else
	t3 = dest;

      x = gen_rtx_AND (mode, op_true, cmp);
      emit_insn (gen_rtx_SET (VOIDmode, t2, x));

      x = gen_rtx_NOT (mode, cmp);
      x = gen_rtx_AND (mode, x, op_false);
      emit_insn (gen_rtx_SET (VOIDmode, t3, x));

      x = gen_rtx_IOR (mode, t3, t2);
      emit_insn (gen_rtx_SET (VOIDmode, dest, x));
    }
}

/* Expand a floating-point conditional move.  Return true if successful.  */

int
ix86_expand_fp_movcc (rtx operands[])
{
  enum machine_mode mode = GET_MODE (operands[0]);
  enum rtx_code code = GET_CODE (operands[1]);
  rtx tmp, compare_op, second_test, bypass_test;

  if (TARGET_SSE_MATH && SSE_FLOAT_MODE_P (mode))
    {
      enum machine_mode cmode;

      /* Since we've no cmove for sse registers, don't force bad register
	 allocation just to gain access to it.  Deny movcc when the
	 comparison mode doesn't match the move mode.  */
      cmode = GET_MODE (ix86_compare_op0);
      if (cmode == VOIDmode)
	cmode = GET_MODE (ix86_compare_op1);
      if (cmode != mode)
	return 0;

      code = ix86_prepare_sse_fp_compare_args (operands[0], code,
					       &ix86_compare_op0,
					       &ix86_compare_op1);
      if (code == UNKNOWN)
	return 0;

      if (ix86_expand_sse_fp_minmax (operands[0], code, ix86_compare_op0,
				     ix86_compare_op1, operands[2],
				     operands[3]))
	return 1;

      tmp = ix86_expand_sse_cmp (operands[0], code, ix86_compare_op0,
				 ix86_compare_op1, operands[2], operands[3]);
      ix86_expand_sse_movcc (operands[0], tmp, operands[2], operands[3]);
      return 1;
    }

  /* The floating point conditional move instructions don't directly
     support conditions resulting from a signed integer comparison.  */

  compare_op = ix86_expand_compare (code, &second_test, &bypass_test);

  /* The floating point conditional move instructions don't directly
     support signed integer comparisons.  */

  if (!fcmov_comparison_operator (compare_op, VOIDmode))
    {
      gcc_assert (!second_test && !bypass_test);
      tmp = gen_reg_rtx (QImode);
      ix86_expand_setcc (code, tmp);
      code = NE;
      ix86_compare_op0 = tmp;
      ix86_compare_op1 = const0_rtx;
      compare_op = ix86_expand_compare (code,  &second_test, &bypass_test);
    }
  if (bypass_test && reg_overlap_mentioned_p (operands[0], operands[3]))
    {
      tmp = gen_reg_rtx (mode);
      emit_move_insn (tmp, operands[3]);
      operands[3] = tmp;
    }
  if (second_test && reg_overlap_mentioned_p (operands[0], operands[2]))
    {
      tmp = gen_reg_rtx (mode);
      emit_move_insn (tmp, operands[2]);
      operands[2] = tmp;
    }

  emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			  gen_rtx_IF_THEN_ELSE (mode, compare_op,
						operands[2], operands[3])));
  if (bypass_test)
    emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			    gen_rtx_IF_THEN_ELSE (mode, bypass_test,
						  operands[3], operands[0])));
  if (second_test)
    emit_insn (gen_rtx_SET (VOIDmode, operands[0],
			    gen_rtx_IF_THEN_ELSE (mode, second_test,
						  operands[2], operands[0])));

  return 1;
}

/* Expand a floating-point vector conditional move; a vcond operation
   rather than a movcc operation.  */

bool
ix86_expand_fp_vcond (rtx operands[])
{
  enum rtx_code code = GET_CODE (operands[3]);
  rtx cmp;

  code = ix86_prepare_sse_fp_compare_args (operands[0], code,
					   &operands[4], &operands[5]);
  if (code == UNKNOWN)
    return false;

  if (ix86_expand_sse_fp_minmax (operands[0], code, operands[4],
				 operands[5], operands[1], operands[2]))
    return true;

  cmp = ix86_expand_sse_cmp (operands[0], code, operands[4], operands[5],
			     operands[1], operands[2]);
  ix86_expand_sse_movcc (operands[0], cmp, operands[1], operands[2]);
  return true;
}

/* Expand a signed integral vector conditional move.  */

bool
ix86_expand_int_vcond (rtx operands[])
{
  enum machine_mode mode = GET_MODE (operands[0]);
  enum rtx_code code = GET_CODE (operands[3]);
  bool negate = false;
  rtx x, cop0, cop1;

  cop0 = operands[4];
  cop1 = operands[5];

  /* Canonicalize the comparison to EQ, GT, GTU.  */
  switch (code)
    {
    case EQ:
    case GT:
    case GTU:
      break;

    case NE:
    case LE:
    case LEU:
      code = reverse_condition (code);
      negate = true;
      break;

    case GE:
    case GEU:
      code = reverse_condition (code);
      negate = true;
      /* FALLTHRU */

    case LT:
    case LTU:
      code = swap_condition (code);
      x = cop0, cop0 = cop1, cop1 = x;
      break;

    default:
      gcc_unreachable ();
    }

  /* Unsigned parallel compare is not supported by the hardware.  Play some
     tricks to turn this into a signed comparison against 0.  */
  if (code == GTU)
    {
      cop0 = force_reg (mode, cop0);

      switch (mode)
	{
	case V4SImode:
	  {
	    rtx t1, t2, mask;

	    /* Perform a parallel modulo subtraction.  */
	    t1 = gen_reg_rtx (mode);
	    emit_insn (gen_subv4si3 (t1, cop0, cop1));

	    /* Extract the original sign bit of op0.  */
	    mask = GEN_INT (-0x80000000);
	    mask = gen_rtx_CONST_VECTOR (mode,
			gen_rtvec (4, mask, mask, mask, mask));
	    mask = force_reg (mode, mask);
	    t2 = gen_reg_rtx (mode);
	    emit_insn (gen_andv4si3 (t2, cop0, mask));

	    /* XOR it back into the result of the subtraction.  This results
	       in the sign bit set iff we saw unsigned underflow.  */
	    x = gen_reg_rtx (mode);
	    emit_insn (gen_xorv4si3 (x, t1, t2));

	    code = GT;
	  }
	  break;

	case V16QImode:
	case V8HImode:
	  /* Perform a parallel unsigned saturating subtraction.  */
	  x = gen_reg_rtx (mode);
	  emit_insn (gen_rtx_SET (VOIDmode, x,
				  gen_rtx_US_MINUS (mode, cop0, cop1)));

	  code = EQ;
	  negate = !negate;
	  break;

	default:
	  gcc_unreachable ();
	}

      cop0 = x;
      cop1 = CONST0_RTX (mode);
    }

  x = ix86_expand_sse_cmp (operands[0], code, cop0, cop1,
			   operands[1+negate], operands[2-negate]);

  ix86_expand_sse_movcc (operands[0], x, operands[1+negate],
			 operands[2-negate]);
  return true;
}

/* Expand conditional increment or decrement using adb/sbb instructions.
   The default case using setcc followed by the conditional move can be
   done by generic code.  */
int
ix86_expand_int_addcc (rtx operands[])
{
  enum rtx_code code = GET_CODE (operands[1]);
  rtx compare_op;
  rtx val = const0_rtx;
  bool fpcmp = false;
  enum machine_mode mode = GET_MODE (operands[0]);

  if (operands[3] != const1_rtx
      && operands[3] != constm1_rtx)
    return 0;
  if (!ix86_expand_carry_flag_compare (code, ix86_compare_op0,
				       ix86_compare_op1, &compare_op))
     return 0;
  code = GET_CODE (compare_op);

  if (GET_MODE (XEXP (compare_op, 0)) == CCFPmode
      || GET_MODE (XEXP (compare_op, 0)) == CCFPUmode)
    {
      fpcmp = true;
      code = ix86_fp_compare_code_to_integer (code);
    }

  if (code != LTU)
    {
      val = constm1_rtx;
      if (fpcmp)
	PUT_CODE (compare_op,
		  reverse_condition_maybe_unordered
		    (GET_CODE (compare_op)));
      else
	PUT_CODE (compare_op, reverse_condition (GET_CODE (compare_op)));
    }
  PUT_MODE (compare_op, mode);

  /* Construct either adc or sbb insn.  */
  if ((code == LTU) == (operands[3] == constm1_rtx))
    {
      switch (GET_MODE (operands[0]))
	{
	  case QImode:
            emit_insn (gen_subqi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case HImode:
            emit_insn (gen_subhi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case SImode:
            emit_insn (gen_subsi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case DImode:
            emit_insn (gen_subdi3_carry_rex64 (operands[0], operands[2], val, compare_op));
	    break;
	  default:
	    gcc_unreachable ();
	}
    }
  else
    {
      switch (GET_MODE (operands[0]))
	{
	  case QImode:
            emit_insn (gen_addqi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case HImode:
            emit_insn (gen_addhi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case SImode:
            emit_insn (gen_addsi3_carry (operands[0], operands[2], val, compare_op));
	    break;
	  case DImode:
            emit_insn (gen_adddi3_carry_rex64 (operands[0], operands[2], val, compare_op));
	    break;
	  default:
	    gcc_unreachable ();
	}
    }
  return 1; /* DONE */
}


/* Split operands 0 and 1 into SImode parts.  Similar to split_di, but
   works for floating pointer parameters and nonoffsetable memories.
   For pushes, it returns just stack offsets; the values will be saved
   in the right order.  Maximally three parts are generated.  */

static int
ix86_split_to_parts (rtx operand, rtx *parts, enum machine_mode mode)
{
  int size;

  if (!TARGET_64BIT)
    size = mode==XFmode ? 3 : GET_MODE_SIZE (mode) / 4;
  else
    size = (GET_MODE_SIZE (mode) + 4) / 8;

  gcc_assert (GET_CODE (operand) != REG || !MMX_REGNO_P (REGNO (operand)));
  gcc_assert (size >= 2 && size <= 3);

  /* Optimize constant pool reference to immediates.  This is used by fp
     moves, that force all constants to memory to allow combining.  */
  if (GET_CODE (operand) == MEM && MEM_READONLY_P (operand))
    {
      rtx tmp = maybe_get_pool_constant (operand);
      if (tmp)
	operand = tmp;
    }

  if (GET_CODE (operand) == MEM && !offsettable_memref_p (operand))
    {
      /* The only non-offsetable memories we handle are pushes.  */
      int ok = push_operand (operand, VOIDmode);

      gcc_assert (ok);

      operand = copy_rtx (operand);
      PUT_MODE (operand, Pmode);
      parts[0] = parts[1] = parts[2] = operand;
      return size;
    }

  if (GET_CODE (operand) == CONST_VECTOR)
    {
      enum machine_mode imode = int_mode_for_mode (mode);
      /* Caution: if we looked through a constant pool memory above,
	 the operand may actually have a different mode now.  That's
	 ok, since we want to pun this all the way back to an integer.  */
      operand = simplify_subreg (imode, operand, GET_MODE (operand), 0);
      gcc_assert (operand != NULL);
      mode = imode;
    }

  if (!TARGET_64BIT)
    {
      if (mode == DImode)
	split_di (&operand, 1, &parts[0], &parts[1]);
      else
	{
	  if (REG_P (operand))
	    {
	      gcc_assert (reload_completed);
	      parts[0] = gen_rtx_REG (SImode, REGNO (operand) + 0);
	      parts[1] = gen_rtx_REG (SImode, REGNO (operand) + 1);
	      if (size == 3)
		parts[2] = gen_rtx_REG (SImode, REGNO (operand) + 2);
	    }
	  else if (offsettable_memref_p (operand))
	    {
	      operand = adjust_address (operand, SImode, 0);
	      parts[0] = operand;
	      parts[1] = adjust_address (operand, SImode, 4);
	      if (size == 3)
		parts[2] = adjust_address (operand, SImode, 8);
	    }
	  else if (GET_CODE (operand) == CONST_DOUBLE)
	    {
	      REAL_VALUE_TYPE r;
	      long l[4];

	      REAL_VALUE_FROM_CONST_DOUBLE (r, operand);
	      switch (mode)
		{
		case XFmode:
		  REAL_VALUE_TO_TARGET_LONG_DOUBLE (r, l);
		  parts[2] = gen_int_mode (l[2], SImode);
		  break;
		case DFmode:
		  REAL_VALUE_TO_TARGET_DOUBLE (r, l);
		  break;
		default:
		  gcc_unreachable ();
		}
	      parts[1] = gen_int_mode (l[1], SImode);
	      parts[0] = gen_int_mode (l[0], SImode);
	    }
	  else
	    gcc_unreachable ();
	}
    }
  else
    {
      if (mode == TImode)
	split_ti (&operand, 1, &parts[0], &parts[1]);
      if (mode == XFmode || mode == TFmode)
	{
	  enum machine_mode upper_mode = mode==XFmode ? SImode : DImode;
	  if (REG_P (operand))
	    {
	      gcc_assert (reload_completed);
	      parts[0] = gen_rtx_REG (DImode, REGNO (operand) + 0);
	      parts[1] = gen_rtx_REG (upper_mode, REGNO (operand) + 1);
	    }
	  else if (offsettable_memref_p (operand))
	    {
	      operand = adjust_address (operand, DImode, 0);
	      parts[0] = operand;
	      parts[1] = adjust_address (operand, upper_mode, 8);
	    }
	  else if (GET_CODE (operand) == CONST_DOUBLE)
	    {
	      REAL_VALUE_TYPE r;
	      long l[4];

	      REAL_VALUE_FROM_CONST_DOUBLE (r, operand);
	      real_to_target (l, &r, mode);

	      /* Do not use shift by 32 to avoid warning on 32bit systems.  */
	      if (HOST_BITS_PER_WIDE_INT >= 64)
	        parts[0]
		  = gen_int_mode
		      ((l[0] & (((HOST_WIDE_INT) 2 << 31) - 1))
		       + ((((HOST_WIDE_INT) l[1]) << 31) << 1),
		       DImode);
	      else
	        parts[0] = immed_double_const (l[0], l[1], DImode);

	      if (upper_mode == SImode)
	        parts[1] = gen_int_mode (l[2], SImode);
	      else if (HOST_BITS_PER_WIDE_INT >= 64)
	        parts[1]
		  = gen_int_mode
		      ((l[2] & (((HOST_WIDE_INT) 2 << 31) - 1))
		       + ((((HOST_WIDE_INT) l[3]) << 31) << 1),
		       DImode);
	      else
	        parts[1] = immed_double_const (l[2], l[3], DImode);
	    }
	  else
	    gcc_unreachable ();
	}
    }

  return size;
}

/* Emit insns to perform a move or push of DI, DF, and XF values.
   Return false when normal moves are needed; true when all required
   insns have been emitted.  Operands 2-4 contain the input values
   int the correct order; operands 5-7 contain the output values.  */

void
ix86_split_long_move (rtx operands[])
{
  rtx part[2][3];
  int nparts;
  int push = 0;
  int collisions = 0;
  enum machine_mode mode = GET_MODE (operands[0]);

  /* The DFmode expanders may ask us to move double.
     For 64bit target this is single move.  By hiding the fact
     here we simplify i386.md splitters.  */
  if (GET_MODE_SIZE (GET_MODE (operands[0])) == 8 && TARGET_64BIT)
    {
      /* Optimize constant pool reference to immediates.  This is used by
	 fp moves, that force all constants to memory to allow combining.  */

      if (GET_CODE (operands[1]) == MEM
	  && GET_CODE (XEXP (operands[1], 0)) == SYMBOL_REF
	  && CONSTANT_POOL_ADDRESS_P (XEXP (operands[1], 0)))
	operands[1] = get_pool_constant (XEXP (operands[1], 0));
      if (push_operand (operands[0], VOIDmode))
	{
	  operands[0] = copy_rtx (operands[0]);
	  PUT_MODE (operands[0], Pmode);
	}
      else
        operands[0] = gen_lowpart (DImode, operands[0]);
      operands[1] = gen_lowpart (DImode, operands[1]);
      emit_move_insn (operands[0], operands[1]);
      return;
    }

  /* The only non-offsettable memory we handle is push.  */
  if (push_operand (operands[0], VOIDmode))
    push = 1;
  else
    gcc_assert (GET_CODE (operands[0]) != MEM
		|| offsettable_memref_p (operands[0]));

  nparts = ix86_split_to_parts (operands[1], part[1], GET_MODE (operands[0]));
  ix86_split_to_parts (operands[0], part[0], GET_MODE (operands[0]));

  /* When emitting push, take care for source operands on the stack.  */
  if (push && GET_CODE (operands[1]) == MEM
      && reg_overlap_mentioned_p (stack_pointer_rtx, operands[1]))
    {
      if (nparts == 3)
	part[1][1] = change_address (part[1][1], GET_MODE (part[1][1]),
				     XEXP (part[1][2], 0));
      part[1][0] = change_address (part[1][0], GET_MODE (part[1][0]),
				   XEXP (part[1][1], 0));
    }

  /* We need to do copy in the right order in case an address register
     of the source overlaps the destination.  */
  if (REG_P (part[0][0]) && GET_CODE (part[1][0]) == MEM)
    {
      if (reg_overlap_mentioned_p (part[0][0], XEXP (part[1][0], 0)))
	collisions++;
      if (reg_overlap_mentioned_p (part[0][1], XEXP (part[1][0], 0)))
	collisions++;
      if (nparts == 3
	  && reg_overlap_mentioned_p (part[0][2], XEXP (part[1][0], 0)))
	collisions++;

      /* Collision in the middle part can be handled by reordering.  */
      if (collisions == 1 && nparts == 3
	  && reg_overlap_mentioned_p (part[0][1], XEXP (part[1][0], 0)))
	{
	  rtx tmp;
	  tmp = part[0][1]; part[0][1] = part[0][2]; part[0][2] = tmp;
	  tmp = part[1][1]; part[1][1] = part[1][2]; part[1][2] = tmp;
	}

      /* If there are more collisions, we can't handle it by reordering.
	 Do an lea to the last part and use only one colliding move.  */
      else if (collisions > 1)
	{
	  rtx base;

	  collisions = 1;

	  base = part[0][nparts - 1];

	  /* Handle the case when the last part isn't valid for lea.
	     Happens in 64-bit mode storing the 12-byte XFmode.  */
	  if (GET_MODE (base) != Pmode)
	    base = gen_rtx_REG (Pmode, REGNO (base));

	  emit_insn (gen_rtx_SET (VOIDmode, base, XEXP (part[1][0], 0)));
	  part[1][0] = replace_equiv_address (part[1][0], base);
	  part[1][1] = replace_equiv_address (part[1][1],
				      plus_constant (base, UNITS_PER_WORD));
	  if (nparts == 3)
	    part[1][2] = replace_equiv_address (part[1][2],
				      plus_constant (base, 8));
	}
    }

  if (push)
    {
      if (!TARGET_64BIT)
	{
	  if (nparts == 3)
	    {
	      if (TARGET_128BIT_LONG_DOUBLE && mode == XFmode)
                emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx, GEN_INT (-4)));
	      emit_move_insn (part[0][2], part[1][2]);
	    }
	}
      else
	{
	  /* In 64bit mode we don't have 32bit push available.  In case this is
	     register, it is OK - we will just use larger counterpart.  We also
	     retype memory - these comes from attempt to avoid REX prefix on
	     moving of second half of TFmode value.  */
	  if (GET_MODE (part[1][1]) == SImode)
	    {
	      switch (GET_CODE (part[1][1]))
		{
		case MEM:
		  part[1][1] = adjust_address (part[1][1], DImode, 0);
		  break;

		case REG:
		  part[1][1] = gen_rtx_REG (DImode, REGNO (part[1][1]));
		  break;

		default:
		  gcc_unreachable ();
		}

	      if (GET_MODE (part[1][0]) == SImode)
		part[1][0] = part[1][1];
	    }
	}
      emit_move_insn (part[0][1], part[1][1]);
      emit_move_insn (part[0][0], part[1][0]);
      return;
    }

  /* Choose correct order to not overwrite the source before it is copied.  */
  if ((REG_P (part[0][0])
       && REG_P (part[1][1])
       && (REGNO (part[0][0]) == REGNO (part[1][1])
	   || (nparts == 3
	       && REGNO (part[0][0]) == REGNO (part[1][2]))))
      || (collisions > 0
	  && reg_overlap_mentioned_p (part[0][0], XEXP (part[1][0], 0))))
    {
      if (nparts == 3)
	{
	  operands[2] = part[0][2];
	  operands[3] = part[0][1];
	  operands[4] = part[0][0];
	  operands[5] = part[1][2];
	  operands[6] = part[1][1];
	  operands[7] = part[1][0];
	}
      else
	{
	  operands[2] = part[0][1];
	  operands[3] = part[0][0];
	  operands[5] = part[1][1];
	  operands[6] = part[1][0];
	}
    }
  else
    {
      if (nparts == 3)
	{
	  operands[2] = part[0][0];
	  operands[3] = part[0][1];
	  operands[4] = part[0][2];
	  operands[5] = part[1][0];
	  operands[6] = part[1][1];
	  operands[7] = part[1][2];
	}
      else
	{
	  operands[2] = part[0][0];
	  operands[3] = part[0][1];
	  operands[5] = part[1][0];
	  operands[6] = part[1][1];
	}
    }

  /* If optimizing for size, attempt to locally unCSE nonzero constants.  */
  if (optimize_size)
    {
      if (GET_CODE (operands[5]) == CONST_INT
	  && operands[5] != const0_rtx
	  && REG_P (operands[2]))
	{
	  if (GET_CODE (operands[6]) == CONST_INT
	      && INTVAL (operands[6]) == INTVAL (operands[5]))
	    operands[6] = operands[2];

	  if (nparts == 3
	      && GET_CODE (operands[7]) == CONST_INT
	      && INTVAL (operands[7]) == INTVAL (operands[5]))
	    operands[7] = operands[2];
	}

      if (nparts == 3
	  && GET_CODE (operands[6]) == CONST_INT
	  && operands[6] != const0_rtx
	  && REG_P (operands[3])
	  && GET_CODE (operands[7]) == CONST_INT
	  && INTVAL (operands[7]) == INTVAL (operands[6]))
	operands[7] = operands[3];
    }

  emit_move_insn (operands[2], operands[5]);
  emit_move_insn (operands[3], operands[6]);
  if (nparts == 3)
    emit_move_insn (operands[4], operands[7]);

  return;
}

/* Helper function of ix86_split_ashl used to generate an SImode/DImode
   left shift by a constant, either using a single shift or
   a sequence of add instructions.  */

static void
ix86_expand_ashl_const (rtx operand, int count, enum machine_mode mode)
{
  if (count == 1)
    {
      emit_insn ((mode == DImode
		  ? gen_addsi3
		  : gen_adddi3) (operand, operand, operand));
    }
  else if (!optimize_size
	   && count * ix86_cost->add <= ix86_cost->shift_const)
    {
      int i;
      for (i=0; i<count; i++)
	{
	  emit_insn ((mode == DImode
		      ? gen_addsi3
		      : gen_adddi3) (operand, operand, operand));
	}
    }
  else
    emit_insn ((mode == DImode
		? gen_ashlsi3
		: gen_ashldi3) (operand, operand, GEN_INT (count)));
}

void
ix86_split_ashl (rtx *operands, rtx scratch, enum machine_mode mode)
{
  rtx low[2], high[2];
  int count;
  const int single_width = mode == DImode ? 32 : 64;

  if (GET_CODE (operands[2]) == CONST_INT)
    {
      (mode == DImode ? split_di : split_ti) (operands, 2, low, high);
      count = INTVAL (operands[2]) & (single_width * 2 - 1);

      if (count >= single_width)
	{
	  emit_move_insn (high[0], low[1]);
	  emit_move_insn (low[0], const0_rtx);

	  if (count > single_width)
	    ix86_expand_ashl_const (high[0], count - single_width, mode);
	}
      else
	{
	  if (!rtx_equal_p (operands[0], operands[1]))
	    emit_move_insn (operands[0], operands[1]);
	  emit_insn ((mode == DImode
		     ? gen_x86_shld_1
		     : gen_x86_64_shld) (high[0], low[0], GEN_INT (count)));
	  ix86_expand_ashl_const (low[0], count, mode);
	}
      return;
    }

  (mode == DImode ? split_di : split_ti) (operands, 1, low, high);

  if (operands[1] == const1_rtx)
    {
      /* Assuming we've chosen a QImode capable registers, then 1 << N
	 can be done with two 32/64-bit shifts, no branches, no cmoves.  */
      if (ANY_QI_REG_P (low[0]) && ANY_QI_REG_P (high[0]))
	{
	  rtx s, d, flags = gen_rtx_REG (CCZmode, FLAGS_REG);

	  ix86_expand_clear (low[0]);
	  ix86_expand_clear (high[0]);
	  emit_insn (gen_testqi_ccz_1 (operands[2], GEN_INT (single_width)));

	  d = gen_lowpart (QImode, low[0]);
	  d = gen_rtx_STRICT_LOW_PART (VOIDmode, d);
	  s = gen_rtx_EQ (QImode, flags, const0_rtx);
	  emit_insn (gen_rtx_SET (VOIDmode, d, s));

	  d = gen_lowpart (QImode, high[0]);
	  d = gen_rtx_STRICT_LOW_PART (VOIDmode, d);
	  s = gen_rtx_NE (QImode, flags, const0_rtx);
	  emit_insn (gen_rtx_SET (VOIDmode, d, s));
	}

      /* Otherwise, we can get the same results by manually performing
	 a bit extract operation on bit 5/6, and then performing the two
	 shifts.  The two methods of getting 0/1 into low/high are exactly
	 the same size.  Avoiding the shift in the bit extract case helps
	 pentium4 a bit; no one else seems to care much either way.  */
      else
	{
	  rtx x;

	  if (TARGET_PARTIAL_REG_STALL && !optimize_size)
	    x = gen_rtx_ZERO_EXTEND (mode == DImode ? SImode : DImode, operands[2]);
	  else
	    x = gen_lowpart (mode == DImode ? SImode : DImode, operands[2]);
	  emit_insn (gen_rtx_SET (VOIDmode, high[0], x));

	  emit_insn ((mode == DImode
		      ? gen_lshrsi3
		      : gen_lshrdi3) (high[0], high[0], GEN_INT (mode == DImode ? 5 : 6)));
	  emit_insn ((mode == DImode
		      ? gen_andsi3
		      : gen_anddi3) (high[0], high[0], GEN_INT (1)));
	  emit_move_insn (low[0], high[0]);
	  emit_insn ((mode == DImode
		      ? gen_xorsi3
		      : gen_xordi3) (low[0], low[0], GEN_INT (1)));
	}

      emit_insn ((mode == DImode
		    ? gen_ashlsi3
		    : gen_ashldi3) (low[0], low[0], operands[2]));
      emit_insn ((mode == DImode
		    ? gen_ashlsi3
		    : gen_ashldi3) (high[0], high[0], operands[2]));
      return;
    }

  if (operands[1] == constm1_rtx)
    {
      /* For -1 << N, we can avoid the shld instruction, because we
	 know that we're shifting 0...31/63 ones into a -1.  */
      emit_move_insn (low[0], constm1_rtx);
      if (optimize_size)
	emit_move_insn (high[0], low[0]);
      else
	emit_move_insn (high[0], constm1_rtx);
    }
  else
    {
      if (!rtx_equal_p (operands[0], operands[1]))
	emit_move_insn (operands[0], operands[1]);

      (mode == DImode ? split_di : split_ti) (operands, 1, low, high);
      emit_insn ((mode == DImode
		  ? gen_x86_shld_1
		  : gen_x86_64_shld) (high[0], low[0], operands[2]));
    }

  emit_insn ((mode == DImode ? gen_ashlsi3 : gen_ashldi3) (low[0], low[0], operands[2]));

  if (TARGET_CMOVE && scratch)
    {
      ix86_expand_clear (scratch);
      emit_insn ((mode == DImode
		  ? gen_x86_shift_adj_1
		  : gen_x86_64_shift_adj) (high[0], low[0], operands[2], scratch));
    }
  else
    emit_insn (gen_x86_shift_adj_2 (high[0], low[0], operands[2]));
}

void
ix86_split_ashr (rtx *operands, rtx scratch, enum machine_mode mode)
{
  rtx low[2], high[2];
  int count;
  const int single_width = mode == DImode ? 32 : 64;

  if (GET_CODE (operands[2]) == CONST_INT)
    {
      (mode == DImode ? split_di : split_ti) (operands, 2, low, high);
      count = INTVAL (operands[2]) & (single_width * 2 - 1);

      if (count == single_width * 2 - 1)
	{
	  emit_move_insn (high[0], high[1]);
	  emit_insn ((mode == DImode
		      ? gen_ashrsi3
		      : gen_ashrdi3) (high[0], high[0],
				      GEN_INT (single_width - 1)));
	  emit_move_insn (low[0], high[0]);

	}
      else if (count >= single_width)
	{
	  emit_move_insn (low[0], high[1]);
	  emit_move_insn (high[0], low[0]);
	  emit_insn ((mode == DImode
		      ? gen_ashrsi3
		      : gen_ashrdi3) (high[0], high[0],
				      GEN_INT (single_width - 1)));
	  if (count > single_width)
	    emit_insn ((mode == DImode
			? gen_ashrsi3
			: gen_ashrdi3) (low[0], low[0],
					GEN_INT (count - single_width)));
	}
      else
	{
	  if (!rtx_equal_p (operands[0], operands[1]))
	    emit_move_insn (operands[0], operands[1]);
	  emit_insn ((mode == DImode
		      ? gen_x86_shrd_1
		      : gen_x86_64_shrd) (low[0], high[0], GEN_INT (count)));
	  emit_insn ((mode == DImode
		      ? gen_ashrsi3
		      : gen_ashrdi3) (high[0], high[0], GEN_INT (count)));
	}
    }
  else
    {
      if (!rtx_equal_p (operands[0], operands[1]))
	emit_move_insn (operands[0], operands[1]);

      (mode == DImode ? split_di : split_ti) (operands, 1, low, high);

      emit_insn ((mode == DImode
		  ? gen_x86_shrd_1
		  : gen_x86_64_shrd) (low[0], high[0], operands[2]));
      emit_insn ((mode == DImode
		  ? gen_ashrsi3
		  : gen_ashrdi3)  (high[0], high[0], operands[2]));

      if (TARGET_CMOVE && scratch)
	{
	  emit_move_insn (scratch, high[0]);
	  emit_insn ((mode == DImode
		      ? gen_ashrsi3
		      : gen_ashrdi3) (scratch, scratch,
				      GEN_INT (single_width - 1)));
	  emit_insn ((mode == DImode
		      ? gen_x86_shift_adj_1
		      : gen_x86_64_shift_adj) (low[0], high[0], operands[2],
					 scratch));
	}
      else
	emit_insn (gen_x86_shift_adj_3 (low[0], high[0], operands[2]));
    }
}

void
ix86_split_lshr (rtx *operands, rtx scratch, enum machine_mode mode)
{
  rtx low[2], high[2];
  int count;
  const int single_width = mode == DImode ? 32 : 64;

  if (GET_CODE (operands[2]) == CONST_INT)
    {
      (mode == DImode ? split_di : split_ti) (operands, 2, low, high);
      count = INTVAL (operands[2]) & (single_width * 2 - 1);

      if (count >= single_width)
	{
	  emit_move_insn (low[0], high[1]);
	  ix86_expand_clear (high[0]);

	  if (count > single_width)
	    emit_insn ((mode == DImode
			? gen_lshrsi3
			: gen_lshrdi3) (low[0], low[0],
					GEN_INT (count - single_width)));
	}
      else
	{
	  if (!rtx_equal_p (operands[0], operands[1]))
	    emit_move_insn (operands[0], operands[1]);
	  emit_insn ((mode == DImode
		      ? gen_x86_shrd_1
		      : gen_x86_64_shrd) (low[0], high[0], GEN_INT (count)));
	  emit_insn ((mode == DImode
		      ? gen_lshrsi3
		      : gen_lshrdi3) (high[0], high[0], GEN_INT (count)));
	}
    }
  else
    {
      if (!rtx_equal_p (operands[0], operands[1]))
	emit_move_insn (operands[0], operands[1]);

      (mode == DImode ? split_di : split_ti) (operands, 1, low, high);

      emit_insn ((mode == DImode
		  ? gen_x86_shrd_1
		  : gen_x86_64_shrd) (low[0], high[0], operands[2]));
      emit_insn ((mode == DImode
		  ? gen_lshrsi3
		  : gen_lshrdi3) (high[0], high[0], operands[2]));

      /* Heh.  By reversing the arguments, we can reuse this pattern.  */
      if (TARGET_CMOVE && scratch)
	{
	  ix86_expand_clear (scratch);
	  emit_insn ((mode == DImode
		      ? gen_x86_shift_adj_1
		      : gen_x86_64_shift_adj) (low[0], high[0], operands[2],
					       scratch));
	}
      else
	emit_insn (gen_x86_shift_adj_2 (low[0], high[0], operands[2]));
    }
}

/* Helper function for the string operations below.  Dest VARIABLE whether
   it is aligned to VALUE bytes.  If true, jump to the label.  */
static rtx
ix86_expand_aligntest (rtx variable, int value)
{
  rtx label = gen_label_rtx ();
  rtx tmpcount = gen_reg_rtx (GET_MODE (variable));
  if (GET_MODE (variable) == DImode)
    emit_insn (gen_anddi3 (tmpcount, variable, GEN_INT (value)));
  else
    emit_insn (gen_andsi3 (tmpcount, variable, GEN_INT (value)));
  emit_cmp_and_jump_insns (tmpcount, const0_rtx, EQ, 0, GET_MODE (variable),
			   1, label);
  return label;
}

/* Adjust COUNTER by the VALUE.  */
static void
ix86_adjust_counter (rtx countreg, HOST_WIDE_INT value)
{
  if (GET_MODE (countreg) == DImode)
    emit_insn (gen_adddi3 (countreg, countreg, GEN_INT (-value)));
  else
    emit_insn (gen_addsi3 (countreg, countreg, GEN_INT (-value)));
}

/* Zero extend possibly SImode EXP to Pmode register.  */
rtx
ix86_zero_extend_to_Pmode (rtx exp)
{
  rtx r;
  if (GET_MODE (exp) == VOIDmode)
    return force_reg (Pmode, exp);
  if (GET_MODE (exp) == Pmode)
    return copy_to_mode_reg (Pmode, exp);
  r = gen_reg_rtx (Pmode);
  emit_insn (gen_zero_extendsidi2 (r, exp));
  return r;
}

/* Expand string move (memcpy) operation.  Use i386 string operations when
   profitable.  expand_clrmem contains similar code.  */
int
ix86_expand_movmem (rtx dst, rtx src, rtx count_exp, rtx align_exp)
{
  rtx srcreg, destreg, countreg, srcexp, destexp;
  enum machine_mode counter_mode;
  HOST_WIDE_INT align = 0;
  unsigned HOST_WIDE_INT count = 0;

  if (GET_CODE (align_exp) == CONST_INT)
    align = INTVAL (align_exp);

  /* Can't use any of this if the user has appropriated esi or edi.  */
  if (global_regs[4] || global_regs[5])
    return 0;

  /* This simple hack avoids all inlining code and simplifies code below.  */
  if (!TARGET_ALIGN_STRINGOPS)
    align = 64;

  if (GET_CODE (count_exp) == CONST_INT)
    {
      count = INTVAL (count_exp);
      if (!TARGET_INLINE_ALL_STRINGOPS && count > 64)
	return 0;
    }

  /* Figure out proper mode for counter.  For 32bits it is always SImode,
     for 64bits use SImode when possible, otherwise DImode.
     Set count to number of bytes copied when known at compile time.  */
  if (!TARGET_64BIT
      || GET_MODE (count_exp) == SImode
      || x86_64_zext_immediate_operand (count_exp, VOIDmode))
    counter_mode = SImode;
  else
    counter_mode = DImode;

  gcc_assert (counter_mode == SImode || counter_mode == DImode);

  destreg = copy_to_mode_reg (Pmode, XEXP (dst, 0));
  if (destreg != XEXP (dst, 0))
    dst = replace_equiv_address_nv (dst, destreg);
  srcreg = copy_to_mode_reg (Pmode, XEXP (src, 0));
  if (srcreg != XEXP (src, 0))
    src = replace_equiv_address_nv (src, srcreg);

  /* When optimizing for size emit simple rep ; movsb instruction for
     counts not divisible by 4, except when (movsl;)*(movsw;)?(movsb;)?
     sequence is shorter than mov{b,l} $count, %{ecx,cl}; rep; movsb.
     Sice of (movsl;)*(movsw;)?(movsb;)? sequence is
     count / 4 + (count & 3), the other sequence is either 4 or 7 bytes,
     but we don't know whether upper 24 (resp. 56) bits of %ecx will be
     known to be zero or not.  The rep; movsb sequence causes higher
     register pressure though, so take that into account.  */

  if ((!optimize || optimize_size)
      && (count == 0
	  || ((count & 0x03)
	      && (!optimize_size
		  || count > 5 * 4
		  || (count & 3) + count / 4 > 6))))
    {
      emit_insn (gen_cld ());
      countreg = ix86_zero_extend_to_Pmode (count_exp);
      destexp = gen_rtx_PLUS (Pmode, destreg, countreg);
      srcexp = gen_rtx_PLUS (Pmode, srcreg, countreg);
      emit_insn (gen_rep_mov (destreg, dst, srcreg, src, countreg,
			      destexp, srcexp));
    }

  /* For constant aligned (or small unaligned) copies use rep movsl
     followed by code copying the rest.  For PentiumPro ensure 8 byte
     alignment to allow rep movsl acceleration.  */

  else if (count != 0
	   && (align >= 8
	       || (!TARGET_PENTIUMPRO && !TARGET_64BIT && align >= 4)
	       || optimize_size || count < (unsigned int) 64))
    {
      unsigned HOST_WIDE_INT offset = 0;
      int size = TARGET_64BIT && !optimize_size ? 8 : 4;
      rtx srcmem, dstmem;

      emit_insn (gen_cld ());
      if (count & ~(size - 1))
	{
	  if ((TARGET_SINGLE_STRINGOP || optimize_size) && count < 5 * 4)
	    {
	      enum machine_mode movs_mode = size == 4 ? SImode : DImode;

	      while (offset < (count & ~(size - 1)))
		{
		  srcmem = adjust_automodify_address_nv (src, movs_mode,
							 srcreg, offset);
		  dstmem = adjust_automodify_address_nv (dst, movs_mode,
							 destreg, offset);
		  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
		  offset += size;
		}
	    }
	  else
	    {
	      countreg = GEN_INT ((count >> (size == 4 ? 2 : 3))
				  & (TARGET_64BIT ? -1 : 0x3fffffff));
	      countreg = copy_to_mode_reg (counter_mode, countreg);
	      countreg = ix86_zero_extend_to_Pmode (countreg);

	      destexp = gen_rtx_ASHIFT (Pmode, countreg,
					GEN_INT (size == 4 ? 2 : 3));
	      srcexp = gen_rtx_PLUS (Pmode, destexp, srcreg);
	      destexp = gen_rtx_PLUS (Pmode, destexp, destreg);

	      emit_insn (gen_rep_mov (destreg, dst, srcreg, src,
				      countreg, destexp, srcexp));
	      offset = count & ~(size - 1);
	    }
	}
      if (size == 8 && (count & 0x04))
	{
	  srcmem = adjust_automodify_address_nv (src, SImode, srcreg,
						 offset);
	  dstmem = adjust_automodify_address_nv (dst, SImode, destreg,
						 offset);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  offset += 4;
	}
      if (count & 0x02)
	{
	  srcmem = adjust_automodify_address_nv (src, HImode, srcreg,
						 offset);
	  dstmem = adjust_automodify_address_nv (dst, HImode, destreg,
						 offset);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  offset += 2;
	}
      if (count & 0x01)
	{
	  srcmem = adjust_automodify_address_nv (src, QImode, srcreg,
						 offset);
	  dstmem = adjust_automodify_address_nv (dst, QImode, destreg,
						 offset);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	}
    }
  /* The generic code based on the glibc implementation:
     - align destination to 4 bytes (8 byte alignment is used for PentiumPro
     allowing accelerated copying there)
     - copy the data using rep movsl
     - copy the rest.  */
  else
    {
      rtx countreg2;
      rtx label = NULL;
      rtx srcmem, dstmem;
      int desired_alignment = (TARGET_PENTIUMPRO
			       && (count == 0 || count >= (unsigned int) 260)
			       ? 8 : UNITS_PER_WORD);
      /* Get rid of MEM_OFFSETs, they won't be accurate.  */
      dst = change_address (dst, BLKmode, destreg);
      src = change_address (src, BLKmode, srcreg);

      /* In case we don't know anything about the alignment, default to
         library version, since it is usually equally fast and result in
         shorter code.

	 Also emit call when we know that the count is large and call overhead
	 will not be important.  */
      if (!TARGET_INLINE_ALL_STRINGOPS
	  && (align < UNITS_PER_WORD || !TARGET_REP_MOVL_OPTIMAL))
	return 0;

      if (TARGET_SINGLE_STRINGOP)
	emit_insn (gen_cld ());

      countreg2 = gen_reg_rtx (Pmode);
      countreg = copy_to_mode_reg (counter_mode, count_exp);

      /* We don't use loops to align destination and to copy parts smaller
         than 4 bytes, because gcc is able to optimize such code better (in
         the case the destination or the count really is aligned, gcc is often
         able to predict the branches) and also it is friendlier to the
         hardware branch prediction.

         Using loops is beneficial for generic case, because we can
         handle small counts using the loops.  Many CPUs (such as Athlon)
         have large REP prefix setup costs.

         This is quite costly.  Maybe we can revisit this decision later or
         add some customizability to this code.  */

      if (count == 0 && align < desired_alignment)
	{
	  label = gen_label_rtx ();
	  emit_cmp_and_jump_insns (countreg, GEN_INT (desired_alignment - 1),
				   LEU, 0, counter_mode, 1, label);
	}
      if (align <= 1)
	{
	  rtx label = ix86_expand_aligntest (destreg, 1);
	  srcmem = change_address (src, QImode, srcreg);
	  dstmem = change_address (dst, QImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  ix86_adjust_counter (countreg, 1);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align <= 2)
	{
	  rtx label = ix86_expand_aligntest (destreg, 2);
	  srcmem = change_address (src, HImode, srcreg);
	  dstmem = change_address (dst, HImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  ix86_adjust_counter (countreg, 2);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align <= 4 && desired_alignment > 4)
	{
	  rtx label = ix86_expand_aligntest (destreg, 4);
	  srcmem = change_address (src, SImode, srcreg);
	  dstmem = change_address (dst, SImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  ix86_adjust_counter (countreg, 4);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}

      if (label && desired_alignment > 4 && !TARGET_64BIT)
	{
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	  label = NULL_RTX;
	}
      if (!TARGET_SINGLE_STRINGOP)
	emit_insn (gen_cld ());
      if (TARGET_64BIT)
	{
	  emit_insn (gen_lshrdi3 (countreg2, ix86_zero_extend_to_Pmode (countreg),
				  GEN_INT (3)));
	  destexp = gen_rtx_ASHIFT (Pmode, countreg2, GEN_INT (3));
	}
      else
	{
	  emit_insn (gen_lshrsi3 (countreg2, countreg, const2_rtx));
	  destexp = gen_rtx_ASHIFT (Pmode, countreg2, const2_rtx);
	}
      srcexp = gen_rtx_PLUS (Pmode, destexp, srcreg);
      destexp = gen_rtx_PLUS (Pmode, destexp, destreg);
      emit_insn (gen_rep_mov (destreg, dst, srcreg, src,
			      countreg2, destexp, srcexp));

      if (label)
	{
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (TARGET_64BIT && align > 4 && count != 0 && (count & 4))
	{
	  srcmem = change_address (src, SImode, srcreg);
	  dstmem = change_address (dst, SImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	}
      if ((align <= 4 || count == 0) && TARGET_64BIT)
	{
	  rtx label = ix86_expand_aligntest (countreg, 4);
	  srcmem = change_address (src, SImode, srcreg);
	  dstmem = change_address (dst, SImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align > 2 && count != 0 && (count & 2))
	{
	  srcmem = change_address (src, HImode, srcreg);
	  dstmem = change_address (dst, HImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	}
      if (align <= 2 || count == 0)
	{
	  rtx label = ix86_expand_aligntest (countreg, 2);
	  srcmem = change_address (src, HImode, srcreg);
	  dstmem = change_address (dst, HImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align > 1 && count != 0 && (count & 1))
	{
	  srcmem = change_address (src, QImode, srcreg);
	  dstmem = change_address (dst, QImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	}
      if (align <= 1 || count == 0)
	{
	  rtx label = ix86_expand_aligntest (countreg, 1);
	  srcmem = change_address (src, QImode, srcreg);
	  dstmem = change_address (dst, QImode, destreg);
	  emit_insn (gen_strmov (destreg, dstmem, srcreg, srcmem));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
    }

  return 1;
}

/* Expand string clear operation (bzero).  Use i386 string operations when
   profitable.  expand_movmem contains similar code.  */
int
ix86_expand_clrmem (rtx dst, rtx count_exp, rtx align_exp)
{
  rtx destreg, zeroreg, countreg, destexp;
  enum machine_mode counter_mode;
  HOST_WIDE_INT align = 0;
  unsigned HOST_WIDE_INT count = 0;

  if (GET_CODE (align_exp) == CONST_INT)
    align = INTVAL (align_exp);

  /* Can't use any of this if the user has appropriated esi.  */
  if (global_regs[4])
    return 0;

  /* This simple hack avoids all inlining code and simplifies code below.  */
  if (!TARGET_ALIGN_STRINGOPS)
    align = 32;

  if (GET_CODE (count_exp) == CONST_INT)
    {
      count = INTVAL (count_exp);
      if (!TARGET_INLINE_ALL_STRINGOPS && count > 64)
	return 0;
    }
  /* Figure out proper mode for counter.  For 32bits it is always SImode,
     for 64bits use SImode when possible, otherwise DImode.
     Set count to number of bytes copied when known at compile time.  */
  if (!TARGET_64BIT
      || GET_MODE (count_exp) == SImode
      || x86_64_zext_immediate_operand (count_exp, VOIDmode))
    counter_mode = SImode;
  else
    counter_mode = DImode;

  destreg = copy_to_mode_reg (Pmode, XEXP (dst, 0));
  if (destreg != XEXP (dst, 0))
    dst = replace_equiv_address_nv (dst, destreg);


  /* When optimizing for size emit simple rep ; movsb instruction for
     counts not divisible by 4.  The movl $N, %ecx; rep; stosb
     sequence is 7 bytes long, so if optimizing for size and count is
     small enough that some stosl, stosw and stosb instructions without
     rep are shorter, fall back into the next if.  */

  if ((!optimize || optimize_size)
      && (count == 0
	  || ((count & 0x03)
	      && (!optimize_size || (count & 0x03) + (count >> 2) > 7))))
    {
      emit_insn (gen_cld ());

      countreg = ix86_zero_extend_to_Pmode (count_exp);
      zeroreg = copy_to_mode_reg (QImode, const0_rtx);
      destexp = gen_rtx_PLUS (Pmode, destreg, countreg);
      emit_insn (gen_rep_stos (destreg, countreg, dst, zeroreg, destexp));
    }
  else if (count != 0
	   && (align >= 8
	       || (!TARGET_PENTIUMPRO && !TARGET_64BIT && align >= 4)
	       || optimize_size || count < (unsigned int) 64))
    {
      int size = TARGET_64BIT && !optimize_size ? 8 : 4;
      unsigned HOST_WIDE_INT offset = 0;

      emit_insn (gen_cld ());

      zeroreg = copy_to_mode_reg (size == 4 ? SImode : DImode, const0_rtx);
      if (count & ~(size - 1))
	{
	  unsigned HOST_WIDE_INT repcount;
	  unsigned int max_nonrep;

	  repcount = count >> (size == 4 ? 2 : 3);
	  if (!TARGET_64BIT)
	    repcount &= 0x3fffffff;

	  /* movl $N, %ecx; rep; stosl is 7 bytes, while N x stosl is N bytes.
	     movl $N, %ecx; rep; stosq is 8 bytes, while N x stosq is 2xN
	     bytes.  In both cases the latter seems to be faster for small
	     values of N.  */
	  max_nonrep = size == 4 ? 7 : 4;
	  if (!optimize_size)
	    switch (ix86_tune)
	      {
	      case PROCESSOR_PENTIUM4:
	      case PROCESSOR_NOCONA:
	        max_nonrep = 3;
	        break;
	      default:
	        break;
	      }

	  if (repcount <= max_nonrep)
	    while (repcount-- > 0)
	      {
		rtx mem = adjust_automodify_address_nv (dst,
							GET_MODE (zeroreg),
							destreg, offset);
		emit_insn (gen_strset (destreg, mem, zeroreg));
		offset += size;
	      }
	  else
	    {
	      countreg = copy_to_mode_reg (counter_mode, GEN_INT (repcount));
	      countreg = ix86_zero_extend_to_Pmode (countreg);
	      destexp = gen_rtx_ASHIFT (Pmode, countreg,
					GEN_INT (size == 4 ? 2 : 3));
	      destexp = gen_rtx_PLUS (Pmode, destexp, destreg);
	      emit_insn (gen_rep_stos (destreg, countreg, dst, zeroreg,
				       destexp));
	      offset = count & ~(size - 1);
	    }
	}
      if (size == 8 && (count & 0x04))
	{
	  rtx mem = adjust_automodify_address_nv (dst, SImode, destreg,
						  offset);
	  emit_insn (gen_strset (destreg, mem,
				 gen_rtx_SUBREG (SImode, zeroreg, 0)));
	  offset += 4;
	}
      if (count & 0x02)
	{
	  rtx mem = adjust_automodify_address_nv (dst, HImode, destreg,
						  offset);
	  emit_insn (gen_strset (destreg, mem,
				 gen_rtx_SUBREG (HImode, zeroreg, 0)));
	  offset += 2;
	}
      if (count & 0x01)
	{
	  rtx mem = adjust_automodify_address_nv (dst, QImode, destreg,
						  offset);
	  emit_insn (gen_strset (destreg, mem,
				 gen_rtx_SUBREG (QImode, zeroreg, 0)));
	}
    }
  else
    {
      rtx countreg2;
      rtx label = NULL;
      /* Compute desired alignment of the string operation.  */
      int desired_alignment = (TARGET_PENTIUMPRO
			       && (count == 0 || count >= (unsigned int) 260)
			       ? 8 : UNITS_PER_WORD);

      /* In case we don't know anything about the alignment, default to
         library version, since it is usually equally fast and result in
         shorter code.

	 Also emit call when we know that the count is large and call overhead
	 will not be important.  */
      if (!TARGET_INLINE_ALL_STRINGOPS
	  && (align < UNITS_PER_WORD || !TARGET_REP_MOVL_OPTIMAL))
	return 0;

      if (TARGET_SINGLE_STRINGOP)
	emit_insn (gen_cld ());

      countreg2 = gen_reg_rtx (Pmode);
      countreg = copy_to_mode_reg (counter_mode, count_exp);
      zeroreg = copy_to_mode_reg (Pmode, const0_rtx);
      /* Get rid of MEM_OFFSET, it won't be accurate.  */
      dst = change_address (dst, BLKmode, destreg);

      if (count == 0 && align < desired_alignment)
	{
	  label = gen_label_rtx ();
	  emit_cmp_and_jump_insns (countreg, GEN_INT (desired_alignment - 1),
				   LEU, 0, counter_mode, 1, label);
	}
      if (align <= 1)
	{
	  rtx label = ix86_expand_aligntest (destreg, 1);
	  emit_insn (gen_strset (destreg, dst,
				 gen_rtx_SUBREG (QImode, zeroreg, 0)));
	  ix86_adjust_counter (countreg, 1);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align <= 2)
	{
	  rtx label = ix86_expand_aligntest (destreg, 2);
	  emit_insn (gen_strset (destreg, dst,
				 gen_rtx_SUBREG (HImode, zeroreg, 0)));
	  ix86_adjust_counter (countreg, 2);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align <= 4 && desired_alignment > 4)
	{
	  rtx label = ix86_expand_aligntest (destreg, 4);
	  emit_insn (gen_strset (destreg, dst,
				 (TARGET_64BIT
				  ? gen_rtx_SUBREG (SImode, zeroreg, 0)
				  : zeroreg)));
	  ix86_adjust_counter (countreg, 4);
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}

      if (label && desired_alignment > 4 && !TARGET_64BIT)
	{
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	  label = NULL_RTX;
	}

      if (!TARGET_SINGLE_STRINGOP)
	emit_insn (gen_cld ());
      if (TARGET_64BIT)
	{
	  emit_insn (gen_lshrdi3 (countreg2, ix86_zero_extend_to_Pmode (countreg),
				  GEN_INT (3)));
	  destexp = gen_rtx_ASHIFT (Pmode, countreg2, GEN_INT (3));
	}
      else
	{
	  emit_insn (gen_lshrsi3 (countreg2, countreg, const2_rtx));
	  destexp = gen_rtx_ASHIFT (Pmode, countreg2, const2_rtx);
	}
      destexp = gen_rtx_PLUS (Pmode, destexp, destreg);
      emit_insn (gen_rep_stos (destreg, countreg2, dst, zeroreg, destexp));

      if (label)
	{
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}

      if (TARGET_64BIT && align > 4 && count != 0 && (count & 4))
	emit_insn (gen_strset (destreg, dst,
			       gen_rtx_SUBREG (SImode, zeroreg, 0)));
      if (TARGET_64BIT && (align <= 4 || count == 0))
	{
	  rtx label = ix86_expand_aligntest (countreg, 4);
	  emit_insn (gen_strset (destreg, dst,
				 gen_rtx_SUBREG (SImode, zeroreg, 0)));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align > 2 && count != 0 && (count & 2))
	emit_insn (gen_strset (destreg, dst,
			       gen_rtx_SUBREG (HImode, zeroreg, 0)));
      if (align <= 2 || count == 0)
	{
	  rtx label = ix86_expand_aligntest (countreg, 2);
	  emit_insn (gen_strset (destreg, dst,
				 gen_rtx_SUBREG (HImode, zeroreg, 0)));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
      if (align > 1 && count != 0 && (count & 1))
	emit_insn (gen_strset (destreg, dst,
			       gen_rtx_SUBREG (QImode, zeroreg, 0)));
      if (align <= 1 || count == 0)
	{
	  rtx label = ix86_expand_aligntest (countreg, 1);
	  emit_insn (gen_strset (destreg, dst,
				 gen_rtx_SUBREG (QImode, zeroreg, 0)));
	  emit_label (label);
	  LABEL_NUSES (label) = 1;
	}
    }
  return 1;
}

/* Expand strlen.  */
int
ix86_expand_strlen (rtx out, rtx src, rtx eoschar, rtx align)
{
  rtx addr, scratch1, scratch2, scratch3, scratch4;

  /* The generic case of strlen expander is long.  Avoid it's
     expanding unless TARGET_INLINE_ALL_STRINGOPS.  */

  if (TARGET_UNROLL_STRLEN && eoschar == const0_rtx && optimize > 1
      && !TARGET_INLINE_ALL_STRINGOPS
      && !optimize_size
      && (GET_CODE (align) != CONST_INT || INTVAL (align) < 4))
    return 0;

  addr = force_reg (Pmode, XEXP (src, 0));
  scratch1 = gen_reg_rtx (Pmode);

  if (TARGET_UNROLL_STRLEN && eoschar == const0_rtx && optimize > 1
      && !optimize_size)
    {
      /* Well it seems that some optimizer does not combine a call like
         foo(strlen(bar), strlen(bar));
         when the move and the subtraction is done here.  It does calculate
         the length just once when these instructions are done inside of
         output_strlen_unroll().  But I think since &bar[strlen(bar)] is
         often used and I use one fewer register for the lifetime of
         output_strlen_unroll() this is better.  */

      emit_move_insn (out, addr);

      ix86_expand_strlensi_unroll_1 (out, src, align);

      /* strlensi_unroll_1 returns the address of the zero at the end of
         the string, like memchr(), so compute the length by subtracting
         the start address.  */
      if (TARGET_64BIT)
	emit_insn (gen_subdi3 (out, out, addr));
      else
	emit_insn (gen_subsi3 (out, out, addr));
    }
  else
    {
      rtx unspec;
      scratch2 = gen_reg_rtx (Pmode);
      scratch3 = gen_reg_rtx (Pmode);
      scratch4 = force_reg (Pmode, constm1_rtx);

      emit_move_insn (scratch3, addr);
      eoschar = force_reg (QImode, eoschar);

      emit_insn (gen_cld ());
      src = replace_equiv_address_nv (src, scratch3);

      /* If .md starts supporting :P, this can be done in .md.  */
      unspec = gen_rtx_UNSPEC (Pmode, gen_rtvec (4, src, eoschar, align,
						 scratch4), UNSPEC_SCAS);
      emit_insn (gen_strlenqi_1 (scratch1, scratch3, unspec));
      if (TARGET_64BIT)
	{
	  emit_insn (gen_one_cmpldi2 (scratch2, scratch1));
	  emit_insn (gen_adddi3 (out, scratch2, constm1_rtx));
	}
      else
	{
	  emit_insn (gen_one_cmplsi2 (scratch2, scratch1));
	  emit_insn (gen_addsi3 (out, scratch2, constm1_rtx));
	}
    }
  return 1;
}

/* Expand the appropriate insns for doing strlen if not just doing
   repnz; scasb

   out = result, initialized with the start address
   align_rtx = alignment of the address.
   scratch = scratch register, initialized with the startaddress when
	not aligned, otherwise undefined

   This is just the body. It needs the initializations mentioned above and
   some address computing at the end.  These things are done in i386.md.  */

static void
ix86_expand_strlensi_unroll_1 (rtx out, rtx src, rtx align_rtx)
{
  int align;
  rtx tmp;
  rtx align_2_label = NULL_RTX;
  rtx align_3_label = NULL_RTX;
  rtx align_4_label = gen_label_rtx ();
  rtx end_0_label = gen_label_rtx ();
  rtx mem;
  rtx tmpreg = gen_reg_rtx (SImode);
  rtx scratch = gen_reg_rtx (SImode);
  rtx cmp;

  align = 0;
  if (GET_CODE (align_rtx) == CONST_INT)
    align = INTVAL (align_rtx);

  /* Loop to check 1..3 bytes for null to get an aligned pointer.  */

  /* Is there a known alignment and is it less than 4?  */
  if (align < 4)
    {
      rtx scratch1 = gen_reg_rtx (Pmode);
      emit_move_insn (scratch1, out);
      /* Is there a known alignment and is it not 2? */
      if (align != 2)
	{
	  align_3_label = gen_label_rtx (); /* Label when aligned to 3-byte */
	  align_2_label = gen_label_rtx (); /* Label when aligned to 2-byte */

	  /* Leave just the 3 lower bits.  */
	  align_rtx = expand_binop (Pmode, and_optab, scratch1, GEN_INT (3),
				    NULL_RTX, 0, OPTAB_WIDEN);

	  emit_cmp_and_jump_insns (align_rtx, const0_rtx, EQ, NULL,
				   Pmode, 1, align_4_label);
	  emit_cmp_and_jump_insns (align_rtx, const2_rtx, EQ, NULL,
				   Pmode, 1, align_2_label);
	  emit_cmp_and_jump_insns (align_rtx, const2_rtx, GTU, NULL,
				   Pmode, 1, align_3_label);
	}
      else
        {
	  /* Since the alignment is 2, we have to check 2 or 0 bytes;
	     check if is aligned to 4 - byte.  */

	  align_rtx = expand_binop (Pmode, and_optab, scratch1, const2_rtx,
				    NULL_RTX, 0, OPTAB_WIDEN);

	  emit_cmp_and_jump_insns (align_rtx, const0_rtx, EQ, NULL,
				   Pmode, 1, align_4_label);
        }

      mem = change_address (src, QImode, out);

      /* Now compare the bytes.  */

      /* Compare the first n unaligned byte on a byte per byte basis.  */
      emit_cmp_and_jump_insns (mem, const0_rtx, EQ, NULL,
			       QImode, 1, end_0_label);

      /* Increment the address.  */
      if (TARGET_64BIT)
	emit_insn (gen_adddi3 (out, out, const1_rtx));
      else
	emit_insn (gen_addsi3 (out, out, const1_rtx));

      /* Not needed with an alignment of 2 */
      if (align != 2)
	{
	  emit_label (align_2_label);

	  emit_cmp_and_jump_insns (mem, const0_rtx, EQ, NULL, QImode, 1,
				   end_0_label);

	  if (TARGET_64BIT)
	    emit_insn (gen_adddi3 (out, out, const1_rtx));
	  else
	    emit_insn (gen_addsi3 (out, out, const1_rtx));

	  emit_label (align_3_label);
	}

      emit_cmp_and_jump_insns (mem, const0_rtx, EQ, NULL, QImode, 1,
			       end_0_label);

      if (TARGET_64BIT)
	emit_insn (gen_adddi3 (out, out, const1_rtx));
      else
	emit_insn (gen_addsi3 (out, out, const1_rtx));
    }

  /* Generate loop to check 4 bytes at a time.  It is not a good idea to
     align this loop.  It gives only huge programs, but does not help to
     speed up.  */
  emit_label (align_4_label);

  mem = change_address (src, SImode, out);
  emit_move_insn (scratch, mem);
  if (TARGET_64BIT)
    emit_insn (gen_adddi3 (out, out, GEN_INT (4)));
  else
    emit_insn (gen_addsi3 (out, out, GEN_INT (4)));

  /* This formula yields a nonzero result iff one of the bytes is zero.
     This saves three branches inside loop and many cycles.  */

  emit_insn (gen_addsi3 (tmpreg, scratch, GEN_INT (-0x01010101)));
  emit_insn (gen_one_cmplsi2 (scratch, scratch));
  emit_insn (gen_andsi3 (tmpreg, tmpreg, scratch));
  emit_insn (gen_andsi3 (tmpreg, tmpreg,
			 gen_int_mode (0x80808080, SImode)));
  emit_cmp_and_jump_insns (tmpreg, const0_rtx, EQ, 0, SImode, 1,
			   align_4_label);

  if (TARGET_CMOVE)
    {
       rtx reg = gen_reg_rtx (SImode);
       rtx reg2 = gen_reg_rtx (Pmode);
       emit_move_insn (reg, tmpreg);
       emit_insn (gen_lshrsi3 (reg, reg, GEN_INT (16)));

       /* If zero is not in the first two bytes, move two bytes forward.  */
       emit_insn (gen_testsi_ccno_1 (tmpreg, GEN_INT (0x8080)));
       tmp = gen_rtx_REG (CCNOmode, FLAGS_REG);
       tmp = gen_rtx_EQ (VOIDmode, tmp, const0_rtx);
       emit_insn (gen_rtx_SET (VOIDmode, tmpreg,
			       gen_rtx_IF_THEN_ELSE (SImode, tmp,
						     reg,
						     tmpreg)));
       /* Emit lea manually to avoid clobbering of flags.  */
       emit_insn (gen_rtx_SET (SImode, reg2,
			       gen_rtx_PLUS (Pmode, out, const2_rtx)));

       tmp = gen_rtx_REG (CCNOmode, FLAGS_REG);
       tmp = gen_rtx_EQ (VOIDmode, tmp, const0_rtx);
       emit_insn (gen_rtx_SET (VOIDmode, out,
			       gen_rtx_IF_THEN_ELSE (Pmode, tmp,
						     reg2,
						     out)));

    }
  else
    {
       rtx end_2_label = gen_label_rtx ();
       /* Is zero in the first two bytes? */

       emit_insn (gen_testsi_ccno_1 (tmpreg, GEN_INT (0x8080)));
       tmp = gen_rtx_REG (CCNOmode, FLAGS_REG);
       tmp = gen_rtx_NE (VOIDmode, tmp, const0_rtx);
       tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
                            gen_rtx_LABEL_REF (VOIDmode, end_2_label),
                            pc_rtx);
       tmp = emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, tmp));
       JUMP_LABEL (tmp) = end_2_label;

       /* Not in the first two.  Move two bytes forward.  */
       emit_insn (gen_lshrsi3 (tmpreg, tmpreg, GEN_INT (16)));
       if (TARGET_64BIT)
	 emit_insn (gen_adddi3 (out, out, const2_rtx));
       else
	 emit_insn (gen_addsi3 (out, out, const2_rtx));

       emit_label (end_2_label);

    }

  /* Avoid branch in fixing the byte.  */
  tmpreg = gen_lowpart (QImode, tmpreg);
  emit_insn (gen_addqi3_cc (tmpreg, tmpreg, tmpreg));
  cmp = gen_rtx_LTU (Pmode, gen_rtx_REG (CCmode, 17), const0_rtx);
  if (TARGET_64BIT)
    emit_insn (gen_subdi3_carry_rex64 (out, out, GEN_INT (3), cmp));
  else
    emit_insn (gen_subsi3_carry (out, out, GEN_INT (3), cmp));

  emit_label (end_0_label);
}

void
ix86_expand_call (rtx retval, rtx fnaddr, rtx callarg1,
		  rtx callarg2 ATTRIBUTE_UNUSED,
		  rtx pop, int sibcall)
{
  rtx use = NULL, call;

  if (pop == const0_rtx)
    pop = NULL;
  gcc_assert (!TARGET_64BIT || !pop);

  if (TARGET_MACHO && !TARGET_64BIT)
    {
#if TARGET_MACHO
      if (flag_pic && GET_CODE (XEXP (fnaddr, 0)) == SYMBOL_REF)
	fnaddr = machopic_indirect_call_target (fnaddr);
#endif
    }
  else
    {
      /* Static functions and indirect calls don't need the pic register.  */
      if (! TARGET_64BIT && flag_pic
	  && GET_CODE (XEXP (fnaddr, 0)) == SYMBOL_REF
	  && ! SYMBOL_REF_LOCAL_P (XEXP (fnaddr, 0)))
	use_reg (&use, pic_offset_table_rtx);
    }

  if (TARGET_64BIT && INTVAL (callarg2) >= 0)
    {
      rtx al = gen_rtx_REG (QImode, 0);
      emit_move_insn (al, callarg2);
      use_reg (&use, al);
    }

  if (! call_insn_operand (XEXP (fnaddr, 0), Pmode))
    {
      fnaddr = copy_to_mode_reg (Pmode, XEXP (fnaddr, 0));
      fnaddr = gen_rtx_MEM (QImode, fnaddr);
    }
  if (sibcall && TARGET_64BIT
      && !constant_call_address_operand (XEXP (fnaddr, 0), Pmode))
    {
      rtx addr;
      addr = copy_to_mode_reg (Pmode, XEXP (fnaddr, 0));
      fnaddr = gen_rtx_REG (Pmode, FIRST_REX_INT_REG + 3 /* R11 */);
      emit_move_insn (fnaddr, addr);
      fnaddr = gen_rtx_MEM (QImode, fnaddr);
    }

  call = gen_rtx_CALL (VOIDmode, fnaddr, callarg1);
  if (retval)
    call = gen_rtx_SET (VOIDmode, retval, call);
  if (pop)
    {
      pop = gen_rtx_PLUS (Pmode, stack_pointer_rtx, pop);
      pop = gen_rtx_SET (VOIDmode, stack_pointer_rtx, pop);
      call = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, call, pop));
    }

  call = emit_call_insn (call);
  if (use)
    CALL_INSN_FUNCTION_USAGE (call) = use;
}


/* Clear stack slot assignments remembered from previous functions.
   This is called from INIT_EXPANDERS once before RTL is emitted for each
   function.  */

static struct machine_function *
ix86_init_machine_status (void)
{
  struct machine_function *f;

  f = ggc_alloc_cleared (sizeof (struct machine_function));
  f->use_fast_prologue_epilogue_nregs = -1;
  f->tls_descriptor_call_expanded_p = 0;

  return f;
}

/* Return a MEM corresponding to a stack slot with mode MODE.
   Allocate a new slot if necessary.

   The RTL for a function can have several slots available: N is
   which slot to use.  */

rtx
assign_386_stack_local (enum machine_mode mode, enum ix86_stack_slot n)
{
  struct stack_local_entry *s;

  gcc_assert (n < MAX_386_STACK_LOCALS);

  /* Virtual slot is valid only before vregs are instantiated.  */
  gcc_assert ((n == SLOT_VIRTUAL) == !virtuals_instantiated);

  for (s = ix86_stack_locals; s; s = s->next)
    if (s->mode == mode && s->n == n)
      return s->rtl;

  s = (struct stack_local_entry *)
    ggc_alloc (sizeof (struct stack_local_entry));
  s->n = n;
  s->mode = mode;
  s->rtl = assign_stack_local (mode, GET_MODE_SIZE (mode), 0);

  s->next = ix86_stack_locals;
  ix86_stack_locals = s;
  return s->rtl;
}

/* Construct the SYMBOL_REF for the tls_get_addr function.  */

static GTY(()) rtx ix86_tls_symbol;
rtx
ix86_tls_get_addr (void)
{

  if (!ix86_tls_symbol)
    {
      ix86_tls_symbol = gen_rtx_SYMBOL_REF (Pmode,
					    (TARGET_ANY_GNU_TLS
					     && !TARGET_64BIT)
					    ? "___tls_get_addr"
					    : "__tls_get_addr");
    }

  return ix86_tls_symbol;
}

/* Construct the SYMBOL_REF for the _TLS_MODULE_BASE_ symbol.  */

static GTY(()) rtx ix86_tls_module_base_symbol;
rtx
ix86_tls_module_base (void)
{

  if (!ix86_tls_module_base_symbol)
    {
      ix86_tls_module_base_symbol = gen_rtx_SYMBOL_REF (Pmode,
							"_TLS_MODULE_BASE_");
      SYMBOL_REF_FLAGS (ix86_tls_module_base_symbol)
	|= TLS_MODEL_GLOBAL_DYNAMIC << SYMBOL_FLAG_TLS_SHIFT;
    }

  return ix86_tls_module_base_symbol;
}

/* Calculate the length of the memory address in the instruction
   encoding.  Does not include the one-byte modrm, opcode, or prefix.  */

int
memory_address_length (rtx addr)
{
  struct ix86_address parts;
  rtx base, index, disp;
  int len;
  int ok;

  if (GET_CODE (addr) == PRE_DEC
      || GET_CODE (addr) == POST_INC
      || GET_CODE (addr) == PRE_MODIFY
      || GET_CODE (addr) == POST_MODIFY)
    return 0;

  ok = ix86_decompose_address (addr, &parts);
  gcc_assert (ok);

  if (parts.base && GET_CODE (parts.base) == SUBREG)
    parts.base = SUBREG_REG (parts.base);
  if (parts.index && GET_CODE (parts.index) == SUBREG)
    parts.index = SUBREG_REG (parts.index);

  base = parts.base;
  index = parts.index;
  disp = parts.disp;
  len = 0;

  /* Rule of thumb:
       - esp as the base always wants an index,
       - ebp as the base always wants a displacement.  */

  /* Register Indirect.  */
  if (base && !index && !disp)
    {
      /* esp (for its index) and ebp (for its displacement) need
	 the two-byte modrm form.  */
      if (addr == stack_pointer_rtx
	  || addr == arg_pointer_rtx
	  || addr == frame_pointer_rtx
	  || addr == hard_frame_pointer_rtx)
	len = 1;
    }

  /* Direct Addressing.  */
  else if (disp && !base && !index)
    len = 4;

  else
    {
      /* Find the length of the displacement constant.  */
      if (disp)
	{
	  if (base && satisfies_constraint_K (disp))
	    len = 1;
	  else
	    len = 4;
	}
      /* ebp always wants a displacement.  */
      else if (base == hard_frame_pointer_rtx)
        len = 1;

      /* An index requires the two-byte modrm form....  */
      if (index
	  /* ...like esp, which always wants an index.  */
	  || base == stack_pointer_rtx
	  || base == arg_pointer_rtx
	  || base == frame_pointer_rtx)
	len += 1;
    }

  return len;
}

/* Compute default value for "length_immediate" attribute.  When SHORTFORM
   is set, expect that insn have 8bit immediate alternative.  */
int
ix86_attr_length_immediate_default (rtx insn, int shortform)
{
  int len = 0;
  int i;
  extract_insn_cached (insn);
  for (i = recog_data.n_operands - 1; i >= 0; --i)
    if (CONSTANT_P (recog_data.operand[i]))
      {
	gcc_assert (!len);
	if (shortform && satisfies_constraint_K (recog_data.operand[i]))
	  len = 1;
	else
	  {
	    switch (get_attr_mode (insn))
	      {
		case MODE_QI:
		  len+=1;
		  break;
		case MODE_HI:
		  len+=2;
		  break;
		case MODE_SI:
		  len+=4;
		  break;
		/* Immediates for DImode instructions are encoded as 32bit sign extended values.  */
		case MODE_DI:
		  len+=4;
		  break;
		default:
		  fatal_insn ("unknown insn mode", insn);
	      }
	  }
      }
  return len;
}
/* Compute default value for "length_address" attribute.  */
int
ix86_attr_length_address_default (rtx insn)
{
  int i;

  if (get_attr_type (insn) == TYPE_LEA)
    {
      rtx set = PATTERN (insn);

      if (GET_CODE (set) == PARALLEL)
	set = XVECEXP (set, 0, 0);

      gcc_assert (GET_CODE (set) == SET);

      return memory_address_length (SET_SRC (set));
    }

  extract_insn_cached (insn);
  for (i = recog_data.n_operands - 1; i >= 0; --i)
    if (GET_CODE (recog_data.operand[i]) == MEM)
      {
	return memory_address_length (XEXP (recog_data.operand[i], 0));
	break;
      }
  return 0;
}

/* Return the maximum number of instructions a cpu can issue.  */

static int
ix86_issue_rate (void)
{
  switch (ix86_tune)
    {
    case PROCESSOR_PENTIUM:
    case PROCESSOR_K6:
      return 2;

    case PROCESSOR_PENTIUMPRO:
    case PROCESSOR_PENTIUM4:
    case PROCESSOR_ATHLON:
    case PROCESSOR_K8:
    case PROCESSOR_NOCONA:
    case PROCESSOR_GENERIC32:
    case PROCESSOR_GENERIC64:
      return 3;

    default:
      return 1;
    }
}

/* A subroutine of ix86_adjust_cost -- return true iff INSN reads flags set
   by DEP_INSN and nothing set by DEP_INSN.  */

static int
ix86_flags_dependent (rtx insn, rtx dep_insn, enum attr_type insn_type)
{
  rtx set, set2;

  /* Simplify the test for uninteresting insns.  */
  if (insn_type != TYPE_SETCC
      && insn_type != TYPE_ICMOV
      && insn_type != TYPE_FCMOV
      && insn_type != TYPE_IBR)
    return 0;

  if ((set = single_set (dep_insn)) != 0)
    {
      set = SET_DEST (set);
      set2 = NULL_RTX;
    }
  else if (GET_CODE (PATTERN (dep_insn)) == PARALLEL
	   && XVECLEN (PATTERN (dep_insn), 0) == 2
	   && GET_CODE (XVECEXP (PATTERN (dep_insn), 0, 0)) == SET
	   && GET_CODE (XVECEXP (PATTERN (dep_insn), 0, 1)) == SET)
    {
      set = SET_DEST (XVECEXP (PATTERN (dep_insn), 0, 0));
      set2 = SET_DEST (XVECEXP (PATTERN (dep_insn), 0, 0));
    }
  else
    return 0;

  if (GET_CODE (set) != REG || REGNO (set) != FLAGS_REG)
    return 0;

  /* This test is true if the dependent insn reads the flags but
     not any other potentially set register.  */
  if (!reg_overlap_mentioned_p (set, PATTERN (insn)))
    return 0;

  if (set2 && reg_overlap_mentioned_p (set2, PATTERN (insn)))
    return 0;

  return 1;
}

/* A subroutine of ix86_adjust_cost -- return true iff INSN has a memory
   address with operands set by DEP_INSN.  */

static int
ix86_agi_dependent (rtx insn, rtx dep_insn, enum attr_type insn_type)
{
  rtx addr;

  if (insn_type == TYPE_LEA
      && TARGET_PENTIUM)
    {
      addr = PATTERN (insn);

      if (GET_CODE (addr) == PARALLEL)
	addr = XVECEXP (addr, 0, 0);

      gcc_assert (GET_CODE (addr) == SET);

      addr = SET_SRC (addr);
    }
  else
    {
      int i;
      extract_insn_cached (insn);
      for (i = recog_data.n_operands - 1; i >= 0; --i)
	if (GET_CODE (recog_data.operand[i]) == MEM)
	  {
	    addr = XEXP (recog_data.operand[i], 0);
	    goto found;
	  }
      return 0;
    found:;
    }

  return modified_in_p (addr, dep_insn);
}

static int
ix86_adjust_cost (rtx insn, rtx link, rtx dep_insn, int cost)
{
  enum attr_type insn_type, dep_insn_type;
  enum attr_memory memory;
  rtx set, set2;
  int dep_insn_code_number;

  /* Anti and output dependencies have zero cost on all CPUs.  */
  if (REG_NOTE_KIND (link) != 0)
    return 0;

  dep_insn_code_number = recog_memoized (dep_insn);

  /* If we can't recognize the insns, we can't really do anything.  */
  if (dep_insn_code_number < 0 || recog_memoized (insn) < 0)
    return cost;

  insn_type = get_attr_type (insn);
  dep_insn_type = get_attr_type (dep_insn);

  switch (ix86_tune)
    {
    case PROCESSOR_PENTIUM:
      /* Address Generation Interlock adds a cycle of latency.  */
      if (ix86_agi_dependent (insn, dep_insn, insn_type))
	cost += 1;

      /* ??? Compares pair with jump/setcc.  */
      if (ix86_flags_dependent (insn, dep_insn, insn_type))
	cost = 0;

      /* Floating point stores require value to be ready one cycle earlier.  */
      if (insn_type == TYPE_FMOV
	  && get_attr_memory (insn) == MEMORY_STORE
	  && !ix86_agi_dependent (insn, dep_insn, insn_type))
	cost += 1;
      break;

    case PROCESSOR_PENTIUMPRO:
      memory = get_attr_memory (insn);

      /* INT->FP conversion is expensive.  */
      if (get_attr_fp_int_src (dep_insn))
	cost += 5;

      /* There is one cycle extra latency between an FP op and a store.  */
      if (insn_type == TYPE_FMOV
	  && (set = single_set (dep_insn)) != NULL_RTX
	  && (set2 = single_set (insn)) != NULL_RTX
	  && rtx_equal_p (SET_DEST (set), SET_SRC (set2))
	  && GET_CODE (SET_DEST (set2)) == MEM)
	cost += 1;

      /* Show ability of reorder buffer to hide latency of load by executing
	 in parallel with previous instruction in case
	 previous instruction is not needed to compute the address.  */
      if ((memory == MEMORY_LOAD || memory == MEMORY_BOTH)
	  && !ix86_agi_dependent (insn, dep_insn, insn_type))
	{
	  /* Claim moves to take one cycle, as core can issue one load
	     at time and the next load can start cycle later.  */
	  if (dep_insn_type == TYPE_IMOV
	      || dep_insn_type == TYPE_FMOV)
	    cost = 1;
	  else if (cost > 1)
	    cost--;
	}
      break;

    case PROCESSOR_K6:
      memory = get_attr_memory (insn);

      /* The esp dependency is resolved before the instruction is really
         finished.  */
      if ((insn_type == TYPE_PUSH || insn_type == TYPE_POP)
	  && (dep_insn_type == TYPE_PUSH || dep_insn_type == TYPE_POP))
	return 1;

      /* INT->FP conversion is expensive.  */
      if (get_attr_fp_int_src (dep_insn))
	cost += 5;

      /* Show ability of reorder buffer to hide latency of load by executing
	 in parallel with previous instruction in case
	 previous instruction is not needed to compute the address.  */
      if ((memory == MEMORY_LOAD || memory == MEMORY_BOTH)
	  && !ix86_agi_dependent (insn, dep_insn, insn_type))
	{
	  /* Claim moves to take one cycle, as core can issue one load
	     at time and the next load can start cycle later.  */
	  if (dep_insn_type == TYPE_IMOV
	      || dep_insn_type == TYPE_FMOV)
	    cost = 1;
	  else if (cost > 2)
	    cost -= 2;
	  else
	    cost = 1;
	}
      break;

    case PROCESSOR_ATHLON:
    case PROCESSOR_K8:
    case PROCESSOR_GENERIC32:
    case PROCESSOR_GENERIC64:
      memory = get_attr_memory (insn);

      /* Show ability of reorder buffer to hide latency of load by executing
	 in parallel with previous instruction in case
	 previous instruction is not needed to compute the address.  */
      if ((memory == MEMORY_LOAD || memory == MEMORY_BOTH)
	  && !ix86_agi_dependent (insn, dep_insn, insn_type))
	{
	  enum attr_unit unit = get_attr_unit (insn);
	  int loadcost = 3;

	  /* Because of the difference between the length of integer and
	     floating unit pipeline preparation stages, the memory operands
	     for floating point are cheaper.

	     ??? For Athlon it the difference is most probably 2.  */
	  if (unit == UNIT_INTEGER || unit == UNIT_UNKNOWN)
	    loadcost = 3;
	  else
	    loadcost = TARGET_ATHLON ? 2 : 0;

	  if (cost >= loadcost)
	    cost -= loadcost;
	  else
	    cost = 0;
	}

    default:
      break;
    }

  return cost;
}

/* How many alternative schedules to try.  This should be as wide as the
   scheduling freedom in the DFA, but no wider.  Making this value too
   large results extra work for the scheduler.  */

static int
ia32_multipass_dfa_lookahead (void)
{
  if (ix86_tune == PROCESSOR_PENTIUM)
    return 2;

  if (ix86_tune == PROCESSOR_PENTIUMPRO
      || ix86_tune == PROCESSOR_K6)
    return 1;

  else
    return 0;
}


/* Compute the alignment given to a constant that is being placed in memory.
   EXP is the constant and ALIGN is the alignment that the object would
   ordinarily have.
   The value of this function is used instead of that alignment to align
   the object.  */

int
ix86_constant_alignment (tree exp, int align)
{
  if (TREE_CODE (exp) == REAL_CST)
    {
      if (TYPE_MODE (TREE_TYPE (exp)) == DFmode && align < 64)
	return 64;
      else if (ALIGN_MODE_128 (TYPE_MODE (TREE_TYPE (exp))) && align < 128)
	return 128;
    }
  else if (!optimize_size && TREE_CODE (exp) == STRING_CST
	   && TREE_STRING_LENGTH (exp) >= 31 && align < BITS_PER_WORD)
    return BITS_PER_WORD;

  return align;
}

/* Compute the alignment for a static variable.
   TYPE is the data type, and ALIGN is the alignment that
   the object would ordinarily have.  The value of this function is used
   instead of that alignment to align the object.  */

int
ix86_data_alignment (tree type, int align)
{
  int max_align = optimize_size ? BITS_PER_WORD : 256;

  if (AGGREGATE_TYPE_P (type)
      && TYPE_SIZE (type)
      && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
      && (TREE_INT_CST_LOW (TYPE_SIZE (type)) >= (unsigned) max_align
	  || TREE_INT_CST_HIGH (TYPE_SIZE (type)))
      && align < max_align)
    align = max_align;

  /* x86-64 ABI requires arrays greater than 16 bytes to be aligned
     to 16byte boundary.  */
  if (TARGET_64BIT)
    {
      if (AGGREGATE_TYPE_P (type)
	   && TYPE_SIZE (type)
	   && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
	   && (TREE_INT_CST_LOW (TYPE_SIZE (type)) >= 128
	       || TREE_INT_CST_HIGH (TYPE_SIZE (type))) && align < 128)
	return 128;
    }

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      if (TYPE_MODE (TREE_TYPE (type)) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (TYPE_MODE (TREE_TYPE (type))) && align < 128)
	return 128;
    }
  else if (TREE_CODE (type) == COMPLEX_TYPE)
    {

      if (TYPE_MODE (type) == DCmode && align < 64)
	return 64;
      if (TYPE_MODE (type) == XCmode && align < 128)
	return 128;
    }
  else if ((TREE_CODE (type) == RECORD_TYPE
	    || TREE_CODE (type) == UNION_TYPE
	    || TREE_CODE (type) == QUAL_UNION_TYPE)
	   && TYPE_FIELDS (type))
    {
      if (DECL_MODE (TYPE_FIELDS (type)) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (DECL_MODE (TYPE_FIELDS (type))) && align < 128)
	return 128;
    }
  else if (TREE_CODE (type) == REAL_TYPE || TREE_CODE (type) == VECTOR_TYPE
	   || TREE_CODE (type) == INTEGER_TYPE)
    {
      if (TYPE_MODE (type) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (TYPE_MODE (type)) && align < 128)
	return 128;
    }

  return align;
}

/* Compute the alignment for a local variable.
   TYPE is the data type, and ALIGN is the alignment that
   the object would ordinarily have.  The value of this macro is used
   instead of that alignment to align the object.  */

int
ix86_local_alignment (tree type, int align)
{
  /* x86-64 ABI requires arrays greater than 16 bytes to be aligned
     to 16byte boundary.  */
  if (TARGET_64BIT)
    {
      if (AGGREGATE_TYPE_P (type)
	   && TYPE_SIZE (type)
	   && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST
	   && (TREE_INT_CST_LOW (TYPE_SIZE (type)) >= 128
	       || TREE_INT_CST_HIGH (TYPE_SIZE (type))) && align < 128)
	return 128;
    }
  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      if (TYPE_MODE (TREE_TYPE (type)) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (TYPE_MODE (TREE_TYPE (type))) && align < 128)
	return 128;
    }
  else if (TREE_CODE (type) == COMPLEX_TYPE)
    {
      if (TYPE_MODE (type) == DCmode && align < 64)
	return 64;
      if (TYPE_MODE (type) == XCmode && align < 128)
	return 128;
    }
  else if ((TREE_CODE (type) == RECORD_TYPE
	    || TREE_CODE (type) == UNION_TYPE
	    || TREE_CODE (type) == QUAL_UNION_TYPE)
	   && TYPE_FIELDS (type))
    {
      if (DECL_MODE (TYPE_FIELDS (type)) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (DECL_MODE (TYPE_FIELDS (type))) && align < 128)
	return 128;
    }
  else if (TREE_CODE (type) == REAL_TYPE || TREE_CODE (type) == VECTOR_TYPE
	   || TREE_CODE (type) == INTEGER_TYPE)
    {

      if (TYPE_MODE (type) == DFmode && align < 64)
	return 64;
      if (ALIGN_MODE_128 (TYPE_MODE (type)) && align < 128)
	return 128;
    }
  return align;
}

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */
void
x86_initialize_trampoline (rtx tramp, rtx fnaddr, rtx cxt)
{
  if (!TARGET_64BIT)
    {
      /* Compute offset from the end of the jmp to the target function.  */
      rtx disp = expand_binop (SImode, sub_optab, fnaddr,
			       plus_constant (tramp, 10),
			       NULL_RTX, 1, OPTAB_DIRECT);
      emit_move_insn (gen_rtx_MEM (QImode, tramp),
		      gen_int_mode (0xb9, QImode));
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, 1)), cxt);
      emit_move_insn (gen_rtx_MEM (QImode, plus_constant (tramp, 5)),
		      gen_int_mode (0xe9, QImode));
      emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, 6)), disp);
    }
  else
    {
      int offset = 0;
      /* Try to load address using shorter movl instead of movabs.
         We may want to support movq for kernel mode, but kernel does not use
         trampolines at the moment.  */
      if (x86_64_zext_immediate_operand (fnaddr, VOIDmode))
	{
	  fnaddr = copy_to_mode_reg (DImode, fnaddr);
	  emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, offset)),
			  gen_int_mode (0xbb41, HImode));
	  emit_move_insn (gen_rtx_MEM (SImode, plus_constant (tramp, offset + 2)),
			  gen_lowpart (SImode, fnaddr));
	  offset += 6;
	}
      else
	{
	  emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, offset)),
			  gen_int_mode (0xbb49, HImode));
	  emit_move_insn (gen_rtx_MEM (DImode, plus_constant (tramp, offset + 2)),
			  fnaddr);
	  offset += 10;
	}
      /* Load static chain using movabs to r10.  */
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, offset)),
		      gen_int_mode (0xba49, HImode));
      emit_move_insn (gen_rtx_MEM (DImode, plus_constant (tramp, offset + 2)),
		      cxt);
      offset += 10;
      /* Jump to the r11 */
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, offset)),
		      gen_int_mode (0xff49, HImode));
      emit_move_insn (gen_rtx_MEM (QImode, plus_constant (tramp, offset+2)),
		      gen_int_mode (0xe3, QImode));
      offset += 3;
      gcc_assert (offset <= TRAMPOLINE_SIZE);
    }

#ifdef ENABLE_EXECUTE_STACK
  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, "__enable_execute_stack"),
		     LCT_NORMAL, VOIDmode, 1, tramp, Pmode);
#endif
}

/* Codes for all the SSE/MMX builtins.  */
enum ix86_builtins
{
  IX86_BUILTIN_ADDPS,
  IX86_BUILTIN_ADDSS,
  IX86_BUILTIN_DIVPS,
  IX86_BUILTIN_DIVSS,
  IX86_BUILTIN_MULPS,
  IX86_BUILTIN_MULSS,
  IX86_BUILTIN_SUBPS,
  IX86_BUILTIN_SUBSS,

  IX86_BUILTIN_CMPEQPS,
  IX86_BUILTIN_CMPLTPS,
  IX86_BUILTIN_CMPLEPS,
  IX86_BUILTIN_CMPGTPS,
  IX86_BUILTIN_CMPGEPS,
  IX86_BUILTIN_CMPNEQPS,
  IX86_BUILTIN_CMPNLTPS,
  IX86_BUILTIN_CMPNLEPS,
  IX86_BUILTIN_CMPNGTPS,
  IX86_BUILTIN_CMPNGEPS,
  IX86_BUILTIN_CMPORDPS,
  IX86_BUILTIN_CMPUNORDPS,
  IX86_BUILTIN_CMPEQSS,
  IX86_BUILTIN_CMPLTSS,
  IX86_BUILTIN_CMPLESS,
  IX86_BUILTIN_CMPNEQSS,
  IX86_BUILTIN_CMPNLTSS,
  IX86_BUILTIN_CMPNLESS,
  IX86_BUILTIN_CMPNGTSS,
  IX86_BUILTIN_CMPNGESS,
  IX86_BUILTIN_CMPORDSS,
  IX86_BUILTIN_CMPUNORDSS,

  IX86_BUILTIN_COMIEQSS,
  IX86_BUILTIN_COMILTSS,
  IX86_BUILTIN_COMILESS,
  IX86_BUILTIN_COMIGTSS,
  IX86_BUILTIN_COMIGESS,
  IX86_BUILTIN_COMINEQSS,
  IX86_BUILTIN_UCOMIEQSS,
  IX86_BUILTIN_UCOMILTSS,
  IX86_BUILTIN_UCOMILESS,
  IX86_BUILTIN_UCOMIGTSS,
  IX86_BUILTIN_UCOMIGESS,
  IX86_BUILTIN_UCOMINEQSS,

  IX86_BUILTIN_CVTPI2PS,
  IX86_BUILTIN_CVTPS2PI,
  IX86_BUILTIN_CVTSI2SS,
  IX86_BUILTIN_CVTSI642SS,
  IX86_BUILTIN_CVTSS2SI,
  IX86_BUILTIN_CVTSS2SI64,
  IX86_BUILTIN_CVTTPS2PI,
  IX86_BUILTIN_CVTTSS2SI,
  IX86_BUILTIN_CVTTSS2SI64,

  IX86_BUILTIN_MAXPS,
  IX86_BUILTIN_MAXSS,
  IX86_BUILTIN_MINPS,
  IX86_BUILTIN_MINSS,

  IX86_BUILTIN_LOADUPS,
  IX86_BUILTIN_STOREUPS,
  IX86_BUILTIN_MOVSS,

  IX86_BUILTIN_MOVHLPS,
  IX86_BUILTIN_MOVLHPS,
  IX86_BUILTIN_LOADHPS,
  IX86_BUILTIN_LOADLPS,
  IX86_BUILTIN_STOREHPS,
  IX86_BUILTIN_STORELPS,

  IX86_BUILTIN_MASKMOVQ,
  IX86_BUILTIN_MOVMSKPS,
  IX86_BUILTIN_PMOVMSKB,

  IX86_BUILTIN_MOVNTPS,
  IX86_BUILTIN_MOVNTQ,

  IX86_BUILTIN_LOADDQU,
  IX86_BUILTIN_STOREDQU,

  IX86_BUILTIN_PACKSSWB,
  IX86_BUILTIN_PACKSSDW,
  IX86_BUILTIN_PACKUSWB,

  IX86_BUILTIN_PADDB,
  IX86_BUILTIN_PADDW,
  IX86_BUILTIN_PADDD,
  IX86_BUILTIN_PADDQ,
  IX86_BUILTIN_PADDSB,
  IX86_BUILTIN_PADDSW,
  IX86_BUILTIN_PADDUSB,
  IX86_BUILTIN_PADDUSW,
  IX86_BUILTIN_PSUBB,
  IX86_BUILTIN_PSUBW,
  IX86_BUILTIN_PSUBD,
  IX86_BUILTIN_PSUBQ,
  IX86_BUILTIN_PSUBSB,
  IX86_BUILTIN_PSUBSW,
  IX86_BUILTIN_PSUBUSB,
  IX86_BUILTIN_PSUBUSW,

  IX86_BUILTIN_PAND,
  IX86_BUILTIN_PANDN,
  IX86_BUILTIN_POR,
  IX86_BUILTIN_PXOR,

  IX86_BUILTIN_PAVGB,
  IX86_BUILTIN_PAVGW,

  IX86_BUILTIN_PCMPEQB,
  IX86_BUILTIN_PCMPEQW,
  IX86_BUILTIN_PCMPEQD,
  IX86_BUILTIN_PCMPGTB,
  IX86_BUILTIN_PCMPGTW,
  IX86_BUILTIN_PCMPGTD,

  IX86_BUILTIN_PMADDWD,

  IX86_BUILTIN_PMAXSW,
  IX86_BUILTIN_PMAXUB,
  IX86_BUILTIN_PMINSW,
  IX86_BUILTIN_PMINUB,

  IX86_BUILTIN_PMULHUW,
  IX86_BUILTIN_PMULHW,
  IX86_BUILTIN_PMULLW,

  IX86_BUILTIN_PSADBW,
  IX86_BUILTIN_PSHUFW,

  IX86_BUILTIN_PSLLW,
  IX86_BUILTIN_PSLLD,
  IX86_BUILTIN_PSLLQ,
  IX86_BUILTIN_PSRAW,
  IX86_BUILTIN_PSRAD,
  IX86_BUILTIN_PSRLW,
  IX86_BUILTIN_PSRLD,
  IX86_BUILTIN_PSRLQ,
  IX86_BUILTIN_PSLLWI,
  IX86_BUILTIN_PSLLDI,
  IX86_BUILTIN_PSLLQI,
  IX86_BUILTIN_PSRAWI,
  IX86_BUILTIN_PSRADI,
  IX86_BUILTIN_PSRLWI,
  IX86_BUILTIN_PSRLDI,
  IX86_BUILTIN_PSRLQI,

  IX86_BUILTIN_PUNPCKHBW,
  IX86_BUILTIN_PUNPCKHWD,
  IX86_BUILTIN_PUNPCKHDQ,
  IX86_BUILTIN_PUNPCKLBW,
  IX86_BUILTIN_PUNPCKLWD,
  IX86_BUILTIN_PUNPCKLDQ,

  IX86_BUILTIN_SHUFPS,

  IX86_BUILTIN_RCPPS,
  IX86_BUILTIN_RCPSS,
  IX86_BUILTIN_RSQRTPS,
  IX86_BUILTIN_RSQRTSS,
  IX86_BUILTIN_SQRTPS,
  IX86_BUILTIN_SQRTSS,

  IX86_BUILTIN_UNPCKHPS,
  IX86_BUILTIN_UNPCKLPS,

  IX86_BUILTIN_ANDPS,
  IX86_BUILTIN_ANDNPS,
  IX86_BUILTIN_ORPS,
  IX86_BUILTIN_XORPS,

  IX86_BUILTIN_EMMS,
  IX86_BUILTIN_LDMXCSR,
  IX86_BUILTIN_STMXCSR,
  IX86_BUILTIN_SFENCE,

  /* 3DNow! Original */
  IX86_BUILTIN_FEMMS,
  IX86_BUILTIN_PAVGUSB,
  IX86_BUILTIN_PF2ID,
  IX86_BUILTIN_PFACC,
  IX86_BUILTIN_PFADD,
  IX86_BUILTIN_PFCMPEQ,
  IX86_BUILTIN_PFCMPGE,
  IX86_BUILTIN_PFCMPGT,
  IX86_BUILTIN_PFMAX,
  IX86_BUILTIN_PFMIN,
  IX86_BUILTIN_PFMUL,
  IX86_BUILTIN_PFRCP,
  IX86_BUILTIN_PFRCPIT1,
  IX86_BUILTIN_PFRCPIT2,
  IX86_BUILTIN_PFRSQIT1,
  IX86_BUILTIN_PFRSQRT,
  IX86_BUILTIN_PFSUB,
  IX86_BUILTIN_PFSUBR,
  IX86_BUILTIN_PI2FD,
  IX86_BUILTIN_PMULHRW,

  /* 3DNow! Athlon Extensions */
  IX86_BUILTIN_PF2IW,
  IX86_BUILTIN_PFNACC,
  IX86_BUILTIN_PFPNACC,
  IX86_BUILTIN_PI2FW,
  IX86_BUILTIN_PSWAPDSI,
  IX86_BUILTIN_PSWAPDSF,

  /* SSE2 */
  IX86_BUILTIN_ADDPD,
  IX86_BUILTIN_ADDSD,
  IX86_BUILTIN_DIVPD,
  IX86_BUILTIN_DIVSD,
  IX86_BUILTIN_MULPD,
  IX86_BUILTIN_MULSD,
  IX86_BUILTIN_SUBPD,
  IX86_BUILTIN_SUBSD,

  IX86_BUILTIN_CMPEQPD,
  IX86_BUILTIN_CMPLTPD,
  IX86_BUILTIN_CMPLEPD,
  IX86_BUILTIN_CMPGTPD,
  IX86_BUILTIN_CMPGEPD,
  IX86_BUILTIN_CMPNEQPD,
  IX86_BUILTIN_CMPNLTPD,
  IX86_BUILTIN_CMPNLEPD,
  IX86_BUILTIN_CMPNGTPD,
  IX86_BUILTIN_CMPNGEPD,
  IX86_BUILTIN_CMPORDPD,
  IX86_BUILTIN_CMPUNORDPD,
  IX86_BUILTIN_CMPNEPD,
  IX86_BUILTIN_CMPEQSD,
  IX86_BUILTIN_CMPLTSD,
  IX86_BUILTIN_CMPLESD,
  IX86_BUILTIN_CMPNEQSD,
  IX86_BUILTIN_CMPNLTSD,
  IX86_BUILTIN_CMPNLESD,
  IX86_BUILTIN_CMPORDSD,
  IX86_BUILTIN_CMPUNORDSD,
  IX86_BUILTIN_CMPNESD,

  IX86_BUILTIN_COMIEQSD,
  IX86_BUILTIN_COMILTSD,
  IX86_BUILTIN_COMILESD,
  IX86_BUILTIN_COMIGTSD,
  IX86_BUILTIN_COMIGESD,
  IX86_BUILTIN_COMINEQSD,
  IX86_BUILTIN_UCOMIEQSD,
  IX86_BUILTIN_UCOMILTSD,
  IX86_BUILTIN_UCOMILESD,
  IX86_BUILTIN_UCOMIGTSD,
  IX86_BUILTIN_UCOMIGESD,
  IX86_BUILTIN_UCOMINEQSD,

  IX86_BUILTIN_MAXPD,
  IX86_BUILTIN_MAXSD,
  IX86_BUILTIN_MINPD,
  IX86_BUILTIN_MINSD,

  IX86_BUILTIN_ANDPD,
  IX86_BUILTIN_ANDNPD,
  IX86_BUILTIN_ORPD,
  IX86_BUILTIN_XORPD,

  IX86_BUILTIN_SQRTPD,
  IX86_BUILTIN_SQRTSD,

  IX86_BUILTIN_UNPCKHPD,
  IX86_BUILTIN_UNPCKLPD,

  IX86_BUILTIN_SHUFPD,

  IX86_BUILTIN_LOADUPD,
  IX86_BUILTIN_STOREUPD,
  IX86_BUILTIN_MOVSD,

  IX86_BUILTIN_LOADHPD,
  IX86_BUILTIN_LOADLPD,

  IX86_BUILTIN_CVTDQ2PD,
  IX86_BUILTIN_CVTDQ2PS,

  IX86_BUILTIN_CVTPD2DQ,
  IX86_BUILTIN_CVTPD2PI,
  IX86_BUILTIN_CVTPD2PS,
  IX86_BUILTIN_CVTTPD2DQ,
  IX86_BUILTIN_CVTTPD2PI,

  IX86_BUILTIN_CVTPI2PD,
  IX86_BUILTIN_CVTSI2SD,
  IX86_BUILTIN_CVTSI642SD,

  IX86_BUILTIN_CVTSD2SI,
  IX86_BUILTIN_CVTSD2SI64,
  IX86_BUILTIN_CVTSD2SS,
  IX86_BUILTIN_CVTSS2SD,
  IX86_BUILTIN_CVTTSD2SI,
  IX86_BUILTIN_CVTTSD2SI64,

  IX86_BUILTIN_CVTPS2DQ,
  IX86_BUILTIN_CVTPS2PD,
  IX86_BUILTIN_CVTTPS2DQ,

  IX86_BUILTIN_MOVNTI,
  IX86_BUILTIN_MOVNTPD,
  IX86_BUILTIN_MOVNTDQ,

  /* SSE2 MMX */
  IX86_BUILTIN_MASKMOVDQU,
  IX86_BUILTIN_MOVMSKPD,
  IX86_BUILTIN_PMOVMSKB128,

  IX86_BUILTIN_PACKSSWB128,
  IX86_BUILTIN_PACKSSDW128,
  IX86_BUILTIN_PACKUSWB128,

  IX86_BUILTIN_PADDB128,
  IX86_BUILTIN_PADDW128,
  IX86_BUILTIN_PADDD128,
  IX86_BUILTIN_PADDQ128,
  IX86_BUILTIN_PADDSB128,
  IX86_BUILTIN_PADDSW128,
  IX86_BUILTIN_PADDUSB128,
  IX86_BUILTIN_PADDUSW128,
  IX86_BUILTIN_PSUBB128,
  IX86_BUILTIN_PSUBW128,
  IX86_BUILTIN_PSUBD128,
  IX86_BUILTIN_PSUBQ128,
  IX86_BUILTIN_PSUBSB128,
  IX86_BUILTIN_PSUBSW128,
  IX86_BUILTIN_PSUBUSB128,
  IX86_BUILTIN_PSUBUSW128,

  IX86_BUILTIN_PAND128,
  IX86_BUILTIN_PANDN128,
  IX86_BUILTIN_POR128,
  IX86_BUILTIN_PXOR128,

  IX86_BUILTIN_PAVGB128,
  IX86_BUILTIN_PAVGW128,

  IX86_BUILTIN_PCMPEQB128,
  IX86_BUILTIN_PCMPEQW128,
  IX86_BUILTIN_PCMPEQD128,
  IX86_BUILTIN_PCMPGTB128,
  IX86_BUILTIN_PCMPGTW128,
  IX86_BUILTIN_PCMPGTD128,

  IX86_BUILTIN_PMADDWD128,

  IX86_BUILTIN_PMAXSW128,
  IX86_BUILTIN_PMAXUB128,
  IX86_BUILTIN_PMINSW128,
  IX86_BUILTIN_PMINUB128,

  IX86_BUILTIN_PMULUDQ,
  IX86_BUILTIN_PMULUDQ128,
  IX86_BUILTIN_PMULHUW128,
  IX86_BUILTIN_PMULHW128,
  IX86_BUILTIN_PMULLW128,

  IX86_BUILTIN_PSADBW128,
  IX86_BUILTIN_PSHUFHW,
  IX86_BUILTIN_PSHUFLW,
  IX86_BUILTIN_PSHUFD,

  IX86_BUILTIN_PSLLW128,
  IX86_BUILTIN_PSLLD128,
  IX86_BUILTIN_PSLLQ128,
  IX86_BUILTIN_PSRAW128,
  IX86_BUILTIN_PSRAD128,
  IX86_BUILTIN_PSRLW128,
  IX86_BUILTIN_PSRLD128,
  IX86_BUILTIN_PSRLQ128,
  IX86_BUILTIN_PSLLDQI128,
  IX86_BUILTIN_PSLLWI128,
  IX86_BUILTIN_PSLLDI128,
  IX86_BUILTIN_PSLLQI128,
  IX86_BUILTIN_PSRAWI128,
  IX86_BUILTIN_PSRADI128,
  IX86_BUILTIN_PSRLDQI128,
  IX86_BUILTIN_PSRLWI128,
  IX86_BUILTIN_PSRLDI128,
  IX86_BUILTIN_PSRLQI128,

  IX86_BUILTIN_PUNPCKHBW128,
  IX86_BUILTIN_PUNPCKHWD128,
  IX86_BUILTIN_PUNPCKHDQ128,
  IX86_BUILTIN_PUNPCKHQDQ128,
  IX86_BUILTIN_PUNPCKLBW128,
  IX86_BUILTIN_PUNPCKLWD128,
  IX86_BUILTIN_PUNPCKLDQ128,
  IX86_BUILTIN_PUNPCKLQDQ128,

  IX86_BUILTIN_CLFLUSH,
  IX86_BUILTIN_MFENCE,
  IX86_BUILTIN_LFENCE,

  /* Prescott New Instructions.  */
  IX86_BUILTIN_ADDSUBPS,
  IX86_BUILTIN_HADDPS,
  IX86_BUILTIN_HSUBPS,
  IX86_BUILTIN_MOVSHDUP,
  IX86_BUILTIN_MOVSLDUP,
  IX86_BUILTIN_ADDSUBPD,
  IX86_BUILTIN_HADDPD,
  IX86_BUILTIN_HSUBPD,
  IX86_BUILTIN_LDDQU,

  IX86_BUILTIN_MONITOR,
  IX86_BUILTIN_MWAIT,

  IX86_BUILTIN_VEC_INIT_V2SI,
  IX86_BUILTIN_VEC_INIT_V4HI,
  IX86_BUILTIN_VEC_INIT_V8QI,
  IX86_BUILTIN_VEC_EXT_V2DF,
  IX86_BUILTIN_VEC_EXT_V2DI,
  IX86_BUILTIN_VEC_EXT_V4SF,
  IX86_BUILTIN_VEC_EXT_V4SI,
  IX86_BUILTIN_VEC_EXT_V8HI,
  IX86_BUILTIN_VEC_EXT_V16QI,
  IX86_BUILTIN_VEC_EXT_V2SI,
  IX86_BUILTIN_VEC_EXT_V4HI,
  IX86_BUILTIN_VEC_SET_V8HI,
  IX86_BUILTIN_VEC_SET_V4HI,

  IX86_BUILTIN_MAX
};

#define def_builtin(MASK, NAME, TYPE, CODE)				\
do {									\
  if ((MASK) & target_flags						\
      && (!((MASK) & MASK_64BIT) || TARGET_64BIT))			\
    lang_hooks.builtin_function ((NAME), (TYPE), (CODE), BUILT_IN_MD,	\
				 NULL, NULL_TREE);			\
} while (0)

/* Bits for builtin_description.flag.  */

/* Set when we don't support the comparison natively, and should
   swap_comparison in order to support it.  */
#define BUILTIN_DESC_SWAP_OPERANDS	1

struct builtin_description
{
  const unsigned int mask;
  const enum insn_code icode;
  const char *const name;
  const enum ix86_builtins code;
  const enum rtx_code comparison;
  const unsigned int flag;
};

static const struct builtin_description bdesc_comi[] =
{
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comieq", IX86_BUILTIN_COMIEQSS, UNEQ, 0 },
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comilt", IX86_BUILTIN_COMILTSS, UNLT, 0 },
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comile", IX86_BUILTIN_COMILESS, UNLE, 0 },
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comigt", IX86_BUILTIN_COMIGTSS, GT, 0 },
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comige", IX86_BUILTIN_COMIGESS, GE, 0 },
  { MASK_SSE, CODE_FOR_sse_comi, "__builtin_ia32_comineq", IX86_BUILTIN_COMINEQSS, LTGT, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomieq", IX86_BUILTIN_UCOMIEQSS, UNEQ, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomilt", IX86_BUILTIN_UCOMILTSS, UNLT, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomile", IX86_BUILTIN_UCOMILESS, UNLE, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomigt", IX86_BUILTIN_UCOMIGTSS, GT, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomige", IX86_BUILTIN_UCOMIGESS, GE, 0 },
  { MASK_SSE, CODE_FOR_sse_ucomi, "__builtin_ia32_ucomineq", IX86_BUILTIN_UCOMINEQSS, LTGT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdeq", IX86_BUILTIN_COMIEQSD, UNEQ, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdlt", IX86_BUILTIN_COMILTSD, UNLT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdle", IX86_BUILTIN_COMILESD, UNLE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdgt", IX86_BUILTIN_COMIGTSD, GT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdge", IX86_BUILTIN_COMIGESD, GE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_comi, "__builtin_ia32_comisdneq", IX86_BUILTIN_COMINEQSD, LTGT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdeq", IX86_BUILTIN_UCOMIEQSD, UNEQ, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdlt", IX86_BUILTIN_UCOMILTSD, UNLT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdle", IX86_BUILTIN_UCOMILESD, UNLE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdgt", IX86_BUILTIN_UCOMIGTSD, GT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdge", IX86_BUILTIN_UCOMIGESD, GE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_ucomi, "__builtin_ia32_ucomisdneq", IX86_BUILTIN_UCOMINEQSD, LTGT, 0 },
};

static const struct builtin_description bdesc_2arg[] =
{
  /* SSE */
  { MASK_SSE, CODE_FOR_addv4sf3, "__builtin_ia32_addps", IX86_BUILTIN_ADDPS, 0, 0 },
  { MASK_SSE, CODE_FOR_subv4sf3, "__builtin_ia32_subps", IX86_BUILTIN_SUBPS, 0, 0 },
  { MASK_SSE, CODE_FOR_mulv4sf3, "__builtin_ia32_mulps", IX86_BUILTIN_MULPS, 0, 0 },
  { MASK_SSE, CODE_FOR_divv4sf3, "__builtin_ia32_divps", IX86_BUILTIN_DIVPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmaddv4sf3,  "__builtin_ia32_addss", IX86_BUILTIN_ADDSS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmsubv4sf3,  "__builtin_ia32_subss", IX86_BUILTIN_SUBSS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmulv4sf3,  "__builtin_ia32_mulss", IX86_BUILTIN_MULSS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmdivv4sf3,  "__builtin_ia32_divss", IX86_BUILTIN_DIVSS, 0, 0 },

  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpeqps", IX86_BUILTIN_CMPEQPS, EQ, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpltps", IX86_BUILTIN_CMPLTPS, LT, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpleps", IX86_BUILTIN_CMPLEPS, LE, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpgtps", IX86_BUILTIN_CMPGTPS, LT,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpgeps", IX86_BUILTIN_CMPGEPS, LE,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpunordps", IX86_BUILTIN_CMPUNORDPS, UNORDERED, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpneqps", IX86_BUILTIN_CMPNEQPS, NE, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpnltps", IX86_BUILTIN_CMPNLTPS, UNGE, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpnleps", IX86_BUILTIN_CMPNLEPS, UNGT, 0 },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpngtps", IX86_BUILTIN_CMPNGTPS, UNGE,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpngeps", IX86_BUILTIN_CMPNGEPS, UNGT,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_maskcmpv4sf3, "__builtin_ia32_cmpordps", IX86_BUILTIN_CMPORDPS, ORDERED, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpeqss", IX86_BUILTIN_CMPEQSS, EQ, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpltss", IX86_BUILTIN_CMPLTSS, LT, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpless", IX86_BUILTIN_CMPLESS, LE, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpunordss", IX86_BUILTIN_CMPUNORDSS, UNORDERED, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpneqss", IX86_BUILTIN_CMPNEQSS, NE, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpnltss", IX86_BUILTIN_CMPNLTSS, UNGE, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpnless", IX86_BUILTIN_CMPNLESS, UNGT, 0 },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpngtss", IX86_BUILTIN_CMPNGTSS, UNGE,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpngess", IX86_BUILTIN_CMPNGESS, UNGT,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE, CODE_FOR_sse_vmmaskcmpv4sf3, "__builtin_ia32_cmpordss", IX86_BUILTIN_CMPORDSS, ORDERED, 0 },

  { MASK_SSE, CODE_FOR_sminv4sf3, "__builtin_ia32_minps", IX86_BUILTIN_MINPS, 0, 0 },
  { MASK_SSE, CODE_FOR_smaxv4sf3, "__builtin_ia32_maxps", IX86_BUILTIN_MAXPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmsminv4sf3, "__builtin_ia32_minss", IX86_BUILTIN_MINSS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_vmsmaxv4sf3, "__builtin_ia32_maxss", IX86_BUILTIN_MAXSS, 0, 0 },

  { MASK_SSE, CODE_FOR_andv4sf3, "__builtin_ia32_andps", IX86_BUILTIN_ANDPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_nandv4sf3,  "__builtin_ia32_andnps", IX86_BUILTIN_ANDNPS, 0, 0 },
  { MASK_SSE, CODE_FOR_iorv4sf3, "__builtin_ia32_orps", IX86_BUILTIN_ORPS, 0, 0 },
  { MASK_SSE, CODE_FOR_xorv4sf3,  "__builtin_ia32_xorps", IX86_BUILTIN_XORPS, 0, 0 },

  { MASK_SSE, CODE_FOR_sse_movss,  "__builtin_ia32_movss", IX86_BUILTIN_MOVSS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_movhlps,  "__builtin_ia32_movhlps", IX86_BUILTIN_MOVHLPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_movlhps,  "__builtin_ia32_movlhps", IX86_BUILTIN_MOVLHPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_unpckhps, "__builtin_ia32_unpckhps", IX86_BUILTIN_UNPCKHPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_unpcklps, "__builtin_ia32_unpcklps", IX86_BUILTIN_UNPCKLPS, 0, 0 },

  /* MMX */
  { MASK_MMX, CODE_FOR_mmx_addv8qi3, "__builtin_ia32_paddb", IX86_BUILTIN_PADDB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_addv4hi3, "__builtin_ia32_paddw", IX86_BUILTIN_PADDW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_addv2si3, "__builtin_ia32_paddd", IX86_BUILTIN_PADDD, 0, 0 },
  { MASK_SSE2, CODE_FOR_mmx_adddi3, "__builtin_ia32_paddq", IX86_BUILTIN_PADDQ, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_subv8qi3, "__builtin_ia32_psubb", IX86_BUILTIN_PSUBB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_subv4hi3, "__builtin_ia32_psubw", IX86_BUILTIN_PSUBW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_subv2si3, "__builtin_ia32_psubd", IX86_BUILTIN_PSUBD, 0, 0 },
  { MASK_SSE2, CODE_FOR_mmx_subdi3, "__builtin_ia32_psubq", IX86_BUILTIN_PSUBQ, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_ssaddv8qi3, "__builtin_ia32_paddsb", IX86_BUILTIN_PADDSB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ssaddv4hi3, "__builtin_ia32_paddsw", IX86_BUILTIN_PADDSW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_sssubv8qi3, "__builtin_ia32_psubsb", IX86_BUILTIN_PSUBSB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_sssubv4hi3, "__builtin_ia32_psubsw", IX86_BUILTIN_PSUBSW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_usaddv8qi3, "__builtin_ia32_paddusb", IX86_BUILTIN_PADDUSB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_usaddv4hi3, "__builtin_ia32_paddusw", IX86_BUILTIN_PADDUSW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ussubv8qi3, "__builtin_ia32_psubusb", IX86_BUILTIN_PSUBUSB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ussubv4hi3, "__builtin_ia32_psubusw", IX86_BUILTIN_PSUBUSW, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_mulv4hi3, "__builtin_ia32_pmullw", IX86_BUILTIN_PMULLW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_smulv4hi3_highpart, "__builtin_ia32_pmulhw", IX86_BUILTIN_PMULHW, 0, 0 },
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_umulv4hi3_highpart, "__builtin_ia32_pmulhuw", IX86_BUILTIN_PMULHUW, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_andv2si3, "__builtin_ia32_pand", IX86_BUILTIN_PAND, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_nandv2si3, "__builtin_ia32_pandn", IX86_BUILTIN_PANDN, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_iorv2si3, "__builtin_ia32_por", IX86_BUILTIN_POR, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_xorv2si3, "__builtin_ia32_pxor", IX86_BUILTIN_PXOR, 0, 0 },

  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_uavgv8qi3, "__builtin_ia32_pavgb", IX86_BUILTIN_PAVGB, 0, 0 },
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_uavgv4hi3, "__builtin_ia32_pavgw", IX86_BUILTIN_PAVGW, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_eqv8qi3, "__builtin_ia32_pcmpeqb", IX86_BUILTIN_PCMPEQB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_eqv4hi3, "__builtin_ia32_pcmpeqw", IX86_BUILTIN_PCMPEQW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_eqv2si3, "__builtin_ia32_pcmpeqd", IX86_BUILTIN_PCMPEQD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_gtv8qi3, "__builtin_ia32_pcmpgtb", IX86_BUILTIN_PCMPGTB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_gtv4hi3, "__builtin_ia32_pcmpgtw", IX86_BUILTIN_PCMPGTW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_gtv2si3, "__builtin_ia32_pcmpgtd", IX86_BUILTIN_PCMPGTD, 0, 0 },

  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_umaxv8qi3, "__builtin_ia32_pmaxub", IX86_BUILTIN_PMAXUB, 0, 0 },
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_smaxv4hi3, "__builtin_ia32_pmaxsw", IX86_BUILTIN_PMAXSW, 0, 0 },
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_uminv8qi3, "__builtin_ia32_pminub", IX86_BUILTIN_PMINUB, 0, 0 },
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_sminv4hi3, "__builtin_ia32_pminsw", IX86_BUILTIN_PMINSW, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_punpckhbw, "__builtin_ia32_punpckhbw", IX86_BUILTIN_PUNPCKHBW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_punpckhwd, "__builtin_ia32_punpckhwd", IX86_BUILTIN_PUNPCKHWD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_punpckhdq, "__builtin_ia32_punpckhdq", IX86_BUILTIN_PUNPCKHDQ, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_punpcklbw, "__builtin_ia32_punpcklbw", IX86_BUILTIN_PUNPCKLBW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_punpcklwd, "__builtin_ia32_punpcklwd", IX86_BUILTIN_PUNPCKLWD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_punpckldq, "__builtin_ia32_punpckldq", IX86_BUILTIN_PUNPCKLDQ, 0, 0 },

  /* Special.  */
  { MASK_MMX, CODE_FOR_mmx_packsswb, 0, IX86_BUILTIN_PACKSSWB, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_packssdw, 0, IX86_BUILTIN_PACKSSDW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_packuswb, 0, IX86_BUILTIN_PACKUSWB, 0, 0 },

  { MASK_SSE, CODE_FOR_sse_cvtpi2ps, 0, IX86_BUILTIN_CVTPI2PS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_cvtsi2ss, 0, IX86_BUILTIN_CVTSI2SS, 0, 0 },
  { MASK_SSE | MASK_64BIT, CODE_FOR_sse_cvtsi2ssq, 0, IX86_BUILTIN_CVTSI642SS, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_ashlv4hi3, 0, IX86_BUILTIN_PSLLW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashlv4hi3, 0, IX86_BUILTIN_PSLLWI, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashlv2si3, 0, IX86_BUILTIN_PSLLD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashlv2si3, 0, IX86_BUILTIN_PSLLDI, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashldi3, 0, IX86_BUILTIN_PSLLQ, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashldi3, 0, IX86_BUILTIN_PSLLQI, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_lshrv4hi3, 0, IX86_BUILTIN_PSRLW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_lshrv4hi3, 0, IX86_BUILTIN_PSRLWI, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_lshrv2si3, 0, IX86_BUILTIN_PSRLD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_lshrv2si3, 0, IX86_BUILTIN_PSRLDI, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_lshrdi3, 0, IX86_BUILTIN_PSRLQ, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_lshrdi3, 0, IX86_BUILTIN_PSRLQI, 0, 0 },

  { MASK_MMX, CODE_FOR_mmx_ashrv4hi3, 0, IX86_BUILTIN_PSRAW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashrv4hi3, 0, IX86_BUILTIN_PSRAWI, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashrv2si3, 0, IX86_BUILTIN_PSRAD, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_ashrv2si3, 0, IX86_BUILTIN_PSRADI, 0, 0 },

  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_psadbw, 0, IX86_BUILTIN_PSADBW, 0, 0 },
  { MASK_MMX, CODE_FOR_mmx_pmaddwd, 0, IX86_BUILTIN_PMADDWD, 0, 0 },

  /* SSE2 */
  { MASK_SSE2, CODE_FOR_addv2df3, "__builtin_ia32_addpd", IX86_BUILTIN_ADDPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_subv2df3, "__builtin_ia32_subpd", IX86_BUILTIN_SUBPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_mulv2df3, "__builtin_ia32_mulpd", IX86_BUILTIN_MULPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_divv2df3, "__builtin_ia32_divpd", IX86_BUILTIN_DIVPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmaddv2df3,  "__builtin_ia32_addsd", IX86_BUILTIN_ADDSD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmsubv2df3,  "__builtin_ia32_subsd", IX86_BUILTIN_SUBSD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmulv2df3,  "__builtin_ia32_mulsd", IX86_BUILTIN_MULSD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmdivv2df3,  "__builtin_ia32_divsd", IX86_BUILTIN_DIVSD, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpeqpd", IX86_BUILTIN_CMPEQPD, EQ, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpltpd", IX86_BUILTIN_CMPLTPD, LT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmplepd", IX86_BUILTIN_CMPLEPD, LE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpgtpd", IX86_BUILTIN_CMPGTPD, LT,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpgepd", IX86_BUILTIN_CMPGEPD, LE,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpunordpd", IX86_BUILTIN_CMPUNORDPD, UNORDERED, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpneqpd", IX86_BUILTIN_CMPNEQPD, NE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpnltpd", IX86_BUILTIN_CMPNLTPD, UNGE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpnlepd", IX86_BUILTIN_CMPNLEPD, UNGT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpngtpd", IX86_BUILTIN_CMPNGTPD, UNGE,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpngepd", IX86_BUILTIN_CMPNGEPD, UNGT,
    BUILTIN_DESC_SWAP_OPERANDS },
  { MASK_SSE2, CODE_FOR_sse2_maskcmpv2df3, "__builtin_ia32_cmpordpd", IX86_BUILTIN_CMPORDPD, ORDERED, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpeqsd", IX86_BUILTIN_CMPEQSD, EQ, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpltsd", IX86_BUILTIN_CMPLTSD, LT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmplesd", IX86_BUILTIN_CMPLESD, LE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpunordsd", IX86_BUILTIN_CMPUNORDSD, UNORDERED, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpneqsd", IX86_BUILTIN_CMPNEQSD, NE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpnltsd", IX86_BUILTIN_CMPNLTSD, UNGE, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpnlesd", IX86_BUILTIN_CMPNLESD, UNGT, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmmaskcmpv2df3, "__builtin_ia32_cmpordsd", IX86_BUILTIN_CMPORDSD, ORDERED, 0 },

  { MASK_SSE2, CODE_FOR_sminv2df3, "__builtin_ia32_minpd", IX86_BUILTIN_MINPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_smaxv2df3, "__builtin_ia32_maxpd", IX86_BUILTIN_MAXPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmsminv2df3, "__builtin_ia32_minsd", IX86_BUILTIN_MINSD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_vmsmaxv2df3, "__builtin_ia32_maxsd", IX86_BUILTIN_MAXSD, 0, 0 },

  { MASK_SSE2, CODE_FOR_andv2df3, "__builtin_ia32_andpd", IX86_BUILTIN_ANDPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_nandv2df3,  "__builtin_ia32_andnpd", IX86_BUILTIN_ANDNPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_iorv2df3, "__builtin_ia32_orpd", IX86_BUILTIN_ORPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_xorv2df3,  "__builtin_ia32_xorpd", IX86_BUILTIN_XORPD, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_movsd,  "__builtin_ia32_movsd", IX86_BUILTIN_MOVSD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_unpckhpd, "__builtin_ia32_unpckhpd", IX86_BUILTIN_UNPCKHPD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_unpcklpd, "__builtin_ia32_unpcklpd", IX86_BUILTIN_UNPCKLPD, 0, 0 },

  /* SSE2 MMX */
  { MASK_SSE2, CODE_FOR_addv16qi3, "__builtin_ia32_paddb128", IX86_BUILTIN_PADDB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_addv8hi3, "__builtin_ia32_paddw128", IX86_BUILTIN_PADDW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_addv4si3, "__builtin_ia32_paddd128", IX86_BUILTIN_PADDD128, 0, 0 },
  { MASK_SSE2, CODE_FOR_addv2di3, "__builtin_ia32_paddq128", IX86_BUILTIN_PADDQ128, 0, 0 },
  { MASK_SSE2, CODE_FOR_subv16qi3, "__builtin_ia32_psubb128", IX86_BUILTIN_PSUBB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_subv8hi3, "__builtin_ia32_psubw128", IX86_BUILTIN_PSUBW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_subv4si3, "__builtin_ia32_psubd128", IX86_BUILTIN_PSUBD128, 0, 0 },
  { MASK_SSE2, CODE_FOR_subv2di3, "__builtin_ia32_psubq128", IX86_BUILTIN_PSUBQ128, 0, 0 },

  { MASK_MMX, CODE_FOR_sse2_ssaddv16qi3, "__builtin_ia32_paddsb128", IX86_BUILTIN_PADDSB128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_ssaddv8hi3, "__builtin_ia32_paddsw128", IX86_BUILTIN_PADDSW128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_sssubv16qi3, "__builtin_ia32_psubsb128", IX86_BUILTIN_PSUBSB128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_sssubv8hi3, "__builtin_ia32_psubsw128", IX86_BUILTIN_PSUBSW128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_usaddv16qi3, "__builtin_ia32_paddusb128", IX86_BUILTIN_PADDUSB128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_usaddv8hi3, "__builtin_ia32_paddusw128", IX86_BUILTIN_PADDUSW128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_ussubv16qi3, "__builtin_ia32_psubusb128", IX86_BUILTIN_PSUBUSB128, 0, 0 },
  { MASK_MMX, CODE_FOR_sse2_ussubv8hi3, "__builtin_ia32_psubusw128", IX86_BUILTIN_PSUBUSW128, 0, 0 },

  { MASK_SSE2, CODE_FOR_mulv8hi3, "__builtin_ia32_pmullw128", IX86_BUILTIN_PMULLW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_smulv8hi3_highpart, "__builtin_ia32_pmulhw128", IX86_BUILTIN_PMULHW128, 0, 0 },

  { MASK_SSE2, CODE_FOR_andv2di3, "__builtin_ia32_pand128", IX86_BUILTIN_PAND128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_nandv2di3, "__builtin_ia32_pandn128", IX86_BUILTIN_PANDN128, 0, 0 },
  { MASK_SSE2, CODE_FOR_iorv2di3, "__builtin_ia32_por128", IX86_BUILTIN_POR128, 0, 0 },
  { MASK_SSE2, CODE_FOR_xorv2di3, "__builtin_ia32_pxor128", IX86_BUILTIN_PXOR128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_uavgv16qi3, "__builtin_ia32_pavgb128", IX86_BUILTIN_PAVGB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_uavgv8hi3, "__builtin_ia32_pavgw128", IX86_BUILTIN_PAVGW128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_eqv16qi3, "__builtin_ia32_pcmpeqb128", IX86_BUILTIN_PCMPEQB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_eqv8hi3, "__builtin_ia32_pcmpeqw128", IX86_BUILTIN_PCMPEQW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_eqv4si3, "__builtin_ia32_pcmpeqd128", IX86_BUILTIN_PCMPEQD128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_gtv16qi3, "__builtin_ia32_pcmpgtb128", IX86_BUILTIN_PCMPGTB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_gtv8hi3, "__builtin_ia32_pcmpgtw128", IX86_BUILTIN_PCMPGTW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_gtv4si3, "__builtin_ia32_pcmpgtd128", IX86_BUILTIN_PCMPGTD128, 0, 0 },

  { MASK_SSE2, CODE_FOR_umaxv16qi3, "__builtin_ia32_pmaxub128", IX86_BUILTIN_PMAXUB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_smaxv8hi3, "__builtin_ia32_pmaxsw128", IX86_BUILTIN_PMAXSW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_uminv16qi3, "__builtin_ia32_pminub128", IX86_BUILTIN_PMINUB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sminv8hi3, "__builtin_ia32_pminsw128", IX86_BUILTIN_PMINSW128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_punpckhbw, "__builtin_ia32_punpckhbw128", IX86_BUILTIN_PUNPCKHBW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpckhwd, "__builtin_ia32_punpckhwd128", IX86_BUILTIN_PUNPCKHWD128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpckhdq, "__builtin_ia32_punpckhdq128", IX86_BUILTIN_PUNPCKHDQ128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpckhqdq, "__builtin_ia32_punpckhqdq128", IX86_BUILTIN_PUNPCKHQDQ128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpcklbw, "__builtin_ia32_punpcklbw128", IX86_BUILTIN_PUNPCKLBW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpcklwd, "__builtin_ia32_punpcklwd128", IX86_BUILTIN_PUNPCKLWD128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpckldq, "__builtin_ia32_punpckldq128", IX86_BUILTIN_PUNPCKLDQ128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_punpcklqdq, "__builtin_ia32_punpcklqdq128", IX86_BUILTIN_PUNPCKLQDQ128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_packsswb, "__builtin_ia32_packsswb128", IX86_BUILTIN_PACKSSWB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_packssdw, "__builtin_ia32_packssdw128", IX86_BUILTIN_PACKSSDW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_packuswb, "__builtin_ia32_packuswb128", IX86_BUILTIN_PACKUSWB128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_umulv8hi3_highpart, "__builtin_ia32_pmulhuw128", IX86_BUILTIN_PMULHUW128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_psadbw, 0, IX86_BUILTIN_PSADBW128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_umulsidi3, 0, IX86_BUILTIN_PMULUDQ, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_umulv2siv2di3, 0, IX86_BUILTIN_PMULUDQ128, 0, 0 },

  { MASK_SSE2, CODE_FOR_ashlv8hi3, 0, IX86_BUILTIN_PSLLWI128, 0, 0 },
  { MASK_SSE2, CODE_FOR_ashlv4si3, 0, IX86_BUILTIN_PSLLDI128, 0, 0 },
  { MASK_SSE2, CODE_FOR_ashlv2di3, 0, IX86_BUILTIN_PSLLQI128, 0, 0 },

  { MASK_SSE2, CODE_FOR_lshrv8hi3, 0, IX86_BUILTIN_PSRLWI128, 0, 0 },
  { MASK_SSE2, CODE_FOR_lshrv4si3, 0, IX86_BUILTIN_PSRLDI128, 0, 0 },
  { MASK_SSE2, CODE_FOR_lshrv2di3, 0, IX86_BUILTIN_PSRLQI128, 0, 0 },

  { MASK_SSE2, CODE_FOR_ashrv8hi3, 0, IX86_BUILTIN_PSRAWI128, 0, 0 },
  { MASK_SSE2, CODE_FOR_ashrv4si3, 0, IX86_BUILTIN_PSRADI128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_pmaddwd, 0, IX86_BUILTIN_PMADDWD128, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtsi2sd, 0, IX86_BUILTIN_CVTSI2SD, 0, 0 },
  { MASK_SSE2 | MASK_64BIT, CODE_FOR_sse2_cvtsi2sdq, 0, IX86_BUILTIN_CVTSI642SD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtsd2ss, 0, IX86_BUILTIN_CVTSD2SS, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtss2sd, 0, IX86_BUILTIN_CVTSS2SD, 0, 0 },

  /* SSE3 MMX */
  { MASK_SSE3, CODE_FOR_sse3_addsubv4sf3, "__builtin_ia32_addsubps", IX86_BUILTIN_ADDSUBPS, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_addsubv2df3, "__builtin_ia32_addsubpd", IX86_BUILTIN_ADDSUBPD, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_haddv4sf3, "__builtin_ia32_haddps", IX86_BUILTIN_HADDPS, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_haddv2df3, "__builtin_ia32_haddpd", IX86_BUILTIN_HADDPD, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_hsubv4sf3, "__builtin_ia32_hsubps", IX86_BUILTIN_HSUBPS, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_hsubv2df3, "__builtin_ia32_hsubpd", IX86_BUILTIN_HSUBPD, 0, 0 }
};

static const struct builtin_description bdesc_1arg[] =
{
  { MASK_SSE | MASK_3DNOW_A, CODE_FOR_mmx_pmovmskb, 0, IX86_BUILTIN_PMOVMSKB, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_movmskps, 0, IX86_BUILTIN_MOVMSKPS, 0, 0 },

  { MASK_SSE, CODE_FOR_sqrtv4sf2, 0, IX86_BUILTIN_SQRTPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_rsqrtv4sf2, 0, IX86_BUILTIN_RSQRTPS, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_rcpv4sf2, 0, IX86_BUILTIN_RCPPS, 0, 0 },

  { MASK_SSE, CODE_FOR_sse_cvtps2pi, 0, IX86_BUILTIN_CVTPS2PI, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_cvtss2si, 0, IX86_BUILTIN_CVTSS2SI, 0, 0 },
  { MASK_SSE | MASK_64BIT, CODE_FOR_sse_cvtss2siq, 0, IX86_BUILTIN_CVTSS2SI64, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_cvttps2pi, 0, IX86_BUILTIN_CVTTPS2PI, 0, 0 },
  { MASK_SSE, CODE_FOR_sse_cvttss2si, 0, IX86_BUILTIN_CVTTSS2SI, 0, 0 },
  { MASK_SSE | MASK_64BIT, CODE_FOR_sse_cvttss2siq, 0, IX86_BUILTIN_CVTTSS2SI64, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_pmovmskb, 0, IX86_BUILTIN_PMOVMSKB128, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_movmskpd, 0, IX86_BUILTIN_MOVMSKPD, 0, 0 },

  { MASK_SSE2, CODE_FOR_sqrtv2df2, 0, IX86_BUILTIN_SQRTPD, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtdq2pd, 0, IX86_BUILTIN_CVTDQ2PD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtdq2ps, 0, IX86_BUILTIN_CVTDQ2PS, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtpd2dq, 0, IX86_BUILTIN_CVTPD2DQ, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtpd2pi, 0, IX86_BUILTIN_CVTPD2PI, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtpd2ps, 0, IX86_BUILTIN_CVTPD2PS, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvttpd2dq, 0, IX86_BUILTIN_CVTTPD2DQ, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvttpd2pi, 0, IX86_BUILTIN_CVTTPD2PI, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtpi2pd, 0, IX86_BUILTIN_CVTPI2PD, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtsd2si, 0, IX86_BUILTIN_CVTSD2SI, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvttsd2si, 0, IX86_BUILTIN_CVTTSD2SI, 0, 0 },
  { MASK_SSE2 | MASK_64BIT, CODE_FOR_sse2_cvtsd2siq, 0, IX86_BUILTIN_CVTSD2SI64, 0, 0 },
  { MASK_SSE2 | MASK_64BIT, CODE_FOR_sse2_cvttsd2siq, 0, IX86_BUILTIN_CVTTSD2SI64, 0, 0 },

  { MASK_SSE2, CODE_FOR_sse2_cvtps2dq, 0, IX86_BUILTIN_CVTPS2DQ, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvtps2pd, 0, IX86_BUILTIN_CVTPS2PD, 0, 0 },
  { MASK_SSE2, CODE_FOR_sse2_cvttps2dq, 0, IX86_BUILTIN_CVTTPS2DQ, 0, 0 },

  /* SSE3 */
  { MASK_SSE3, CODE_FOR_sse3_movshdup, 0, IX86_BUILTIN_MOVSHDUP, 0, 0 },
  { MASK_SSE3, CODE_FOR_sse3_movsldup, 0, IX86_BUILTIN_MOVSLDUP, 0, 0 },
};

static void
ix86_init_builtins (void)
{
  if (TARGET_MMX)
    ix86_init_mmx_sse_builtins ();
}

/* Set up all the MMX/SSE builtins.  This is not called if TARGET_MMX
   is zero.  Otherwise, if TARGET_SSE is not set, only expand the MMX
   builtins.  */
static void
ix86_init_mmx_sse_builtins (void)
{
  const struct builtin_description * d;
  size_t i;

  tree V16QI_type_node = build_vector_type_for_mode (intQI_type_node, V16QImode);
  tree V2SI_type_node = build_vector_type_for_mode (intSI_type_node, V2SImode);
  tree V2SF_type_node = build_vector_type_for_mode (float_type_node, V2SFmode);
  tree V2DI_type_node
    = build_vector_type_for_mode (long_long_integer_type_node, V2DImode);
  tree V2DF_type_node = build_vector_type_for_mode (double_type_node, V2DFmode);
  tree V4SF_type_node = build_vector_type_for_mode (float_type_node, V4SFmode);
  tree V4SI_type_node = build_vector_type_for_mode (intSI_type_node, V4SImode);
  tree V4HI_type_node = build_vector_type_for_mode (intHI_type_node, V4HImode);
  tree V8QI_type_node = build_vector_type_for_mode (intQI_type_node, V8QImode);
  tree V8HI_type_node = build_vector_type_for_mode (intHI_type_node, V8HImode);

  tree pchar_type_node = build_pointer_type (char_type_node);
  tree pcchar_type_node = build_pointer_type (
			     build_type_variant (char_type_node, 1, 0));
  tree pfloat_type_node = build_pointer_type (float_type_node);
  tree pcfloat_type_node = build_pointer_type (
			     build_type_variant (float_type_node, 1, 0));
  tree pv2si_type_node = build_pointer_type (V2SI_type_node);
  tree pv2di_type_node = build_pointer_type (V2DI_type_node);
  tree pdi_type_node = build_pointer_type (long_long_unsigned_type_node);

  /* Comparisons.  */
  tree int_ftype_v4sf_v4sf
    = build_function_type_list (integer_type_node,
				V4SF_type_node, V4SF_type_node, NULL_TREE);
  tree v4si_ftype_v4sf_v4sf
    = build_function_type_list (V4SI_type_node,
				V4SF_type_node, V4SF_type_node, NULL_TREE);
  /* MMX/SSE/integer conversions.  */
  tree int_ftype_v4sf
    = build_function_type_list (integer_type_node,
				V4SF_type_node, NULL_TREE);
  tree int64_ftype_v4sf
    = build_function_type_list (long_long_integer_type_node,
				V4SF_type_node, NULL_TREE);
  tree int_ftype_v8qi
    = build_function_type_list (integer_type_node, V8QI_type_node, NULL_TREE);
  tree v4sf_ftype_v4sf_int
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, integer_type_node, NULL_TREE);
  tree v4sf_ftype_v4sf_int64
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, long_long_integer_type_node,
				NULL_TREE);
  tree v4sf_ftype_v4sf_v2si
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, V2SI_type_node, NULL_TREE);

  /* Miscellaneous.  */
  tree v8qi_ftype_v4hi_v4hi
    = build_function_type_list (V8QI_type_node,
				V4HI_type_node, V4HI_type_node, NULL_TREE);
  tree v4hi_ftype_v2si_v2si
    = build_function_type_list (V4HI_type_node,
				V2SI_type_node, V2SI_type_node, NULL_TREE);
  tree v4sf_ftype_v4sf_v4sf_int
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, V4SF_type_node,
				integer_type_node, NULL_TREE);
  tree v2si_ftype_v4hi_v4hi
    = build_function_type_list (V2SI_type_node,
				V4HI_type_node, V4HI_type_node, NULL_TREE);
  tree v4hi_ftype_v4hi_int
    = build_function_type_list (V4HI_type_node,
				V4HI_type_node, integer_type_node, NULL_TREE);
  tree v4hi_ftype_v4hi_di
    = build_function_type_list (V4HI_type_node,
				V4HI_type_node, long_long_unsigned_type_node,
				NULL_TREE);
  tree v2si_ftype_v2si_di
    = build_function_type_list (V2SI_type_node,
				V2SI_type_node, long_long_unsigned_type_node,
				NULL_TREE);
  tree void_ftype_void
    = build_function_type (void_type_node, void_list_node);
  tree void_ftype_unsigned
    = build_function_type_list (void_type_node, unsigned_type_node, NULL_TREE);
  tree void_ftype_unsigned_unsigned
    = build_function_type_list (void_type_node, unsigned_type_node,
				unsigned_type_node, NULL_TREE);
  tree void_ftype_pcvoid_unsigned_unsigned
    = build_function_type_list (void_type_node, const_ptr_type_node,
				unsigned_type_node, unsigned_type_node,
				NULL_TREE);
  tree unsigned_ftype_void
    = build_function_type (unsigned_type_node, void_list_node);
  tree v2si_ftype_v4sf
    = build_function_type_list (V2SI_type_node, V4SF_type_node, NULL_TREE);
  /* Loads/stores.  */
  tree void_ftype_v8qi_v8qi_pchar
    = build_function_type_list (void_type_node,
				V8QI_type_node, V8QI_type_node,
				pchar_type_node, NULL_TREE);
  tree v4sf_ftype_pcfloat
    = build_function_type_list (V4SF_type_node, pcfloat_type_node, NULL_TREE);
  /* @@@ the type is bogus */
  tree v4sf_ftype_v4sf_pv2si
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, pv2si_type_node, NULL_TREE);
  tree void_ftype_pv2si_v4sf
    = build_function_type_list (void_type_node,
				pv2si_type_node, V4SF_type_node, NULL_TREE);
  tree void_ftype_pfloat_v4sf
    = build_function_type_list (void_type_node,
				pfloat_type_node, V4SF_type_node, NULL_TREE);
  tree void_ftype_pdi_di
    = build_function_type_list (void_type_node,
				pdi_type_node, long_long_unsigned_type_node,
				NULL_TREE);
  tree void_ftype_pv2di_v2di
    = build_function_type_list (void_type_node,
				pv2di_type_node, V2DI_type_node, NULL_TREE);
  /* Normal vector unops.  */
  tree v4sf_ftype_v4sf
    = build_function_type_list (V4SF_type_node, V4SF_type_node, NULL_TREE);

  /* Normal vector binops.  */
  tree v4sf_ftype_v4sf_v4sf
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, V4SF_type_node, NULL_TREE);
  tree v8qi_ftype_v8qi_v8qi
    = build_function_type_list (V8QI_type_node,
				V8QI_type_node, V8QI_type_node, NULL_TREE);
  tree v4hi_ftype_v4hi_v4hi
    = build_function_type_list (V4HI_type_node,
				V4HI_type_node, V4HI_type_node, NULL_TREE);
  tree v2si_ftype_v2si_v2si
    = build_function_type_list (V2SI_type_node,
				V2SI_type_node, V2SI_type_node, NULL_TREE);
  tree di_ftype_di_di
    = build_function_type_list (long_long_unsigned_type_node,
				long_long_unsigned_type_node,
				long_long_unsigned_type_node, NULL_TREE);

  tree v2si_ftype_v2sf
    = build_function_type_list (V2SI_type_node, V2SF_type_node, NULL_TREE);
  tree v2sf_ftype_v2si
    = build_function_type_list (V2SF_type_node, V2SI_type_node, NULL_TREE);
  tree v2si_ftype_v2si
    = build_function_type_list (V2SI_type_node, V2SI_type_node, NULL_TREE);
  tree v2sf_ftype_v2sf
    = build_function_type_list (V2SF_type_node, V2SF_type_node, NULL_TREE);
  tree v2sf_ftype_v2sf_v2sf
    = build_function_type_list (V2SF_type_node,
				V2SF_type_node, V2SF_type_node, NULL_TREE);
  tree v2si_ftype_v2sf_v2sf
    = build_function_type_list (V2SI_type_node,
				V2SF_type_node, V2SF_type_node, NULL_TREE);
  tree pint_type_node    = build_pointer_type (integer_type_node);
  tree pdouble_type_node = build_pointer_type (double_type_node);
  tree pcdouble_type_node = build_pointer_type (
				build_type_variant (double_type_node, 1, 0));
  tree int_ftype_v2df_v2df
    = build_function_type_list (integer_type_node,
				V2DF_type_node, V2DF_type_node, NULL_TREE);

  tree void_ftype_pcvoid
    = build_function_type_list (void_type_node, const_ptr_type_node, NULL_TREE);
  tree v4sf_ftype_v4si
    = build_function_type_list (V4SF_type_node, V4SI_type_node, NULL_TREE);
  tree v4si_ftype_v4sf
    = build_function_type_list (V4SI_type_node, V4SF_type_node, NULL_TREE);
  tree v2df_ftype_v4si
    = build_function_type_list (V2DF_type_node, V4SI_type_node, NULL_TREE);
  tree v4si_ftype_v2df
    = build_function_type_list (V4SI_type_node, V2DF_type_node, NULL_TREE);
  tree v2si_ftype_v2df
    = build_function_type_list (V2SI_type_node, V2DF_type_node, NULL_TREE);
  tree v4sf_ftype_v2df
    = build_function_type_list (V4SF_type_node, V2DF_type_node, NULL_TREE);
  tree v2df_ftype_v2si
    = build_function_type_list (V2DF_type_node, V2SI_type_node, NULL_TREE);
  tree v2df_ftype_v4sf
    = build_function_type_list (V2DF_type_node, V4SF_type_node, NULL_TREE);
  tree int_ftype_v2df
    = build_function_type_list (integer_type_node, V2DF_type_node, NULL_TREE);
  tree int64_ftype_v2df
    = build_function_type_list (long_long_integer_type_node,
				V2DF_type_node, NULL_TREE);
  tree v2df_ftype_v2df_int
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, integer_type_node, NULL_TREE);
  tree v2df_ftype_v2df_int64
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, long_long_integer_type_node,
				NULL_TREE);
  tree v4sf_ftype_v4sf_v2df
    = build_function_type_list (V4SF_type_node,
				V4SF_type_node, V2DF_type_node, NULL_TREE);
  tree v2df_ftype_v2df_v4sf
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, V4SF_type_node, NULL_TREE);
  tree v2df_ftype_v2df_v2df_int
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, V2DF_type_node,
				integer_type_node,
				NULL_TREE);
  tree v2df_ftype_v2df_pcdouble
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, pcdouble_type_node, NULL_TREE);
  tree void_ftype_pdouble_v2df
    = build_function_type_list (void_type_node,
				pdouble_type_node, V2DF_type_node, NULL_TREE);
  tree void_ftype_pint_int
    = build_function_type_list (void_type_node,
				pint_type_node, integer_type_node, NULL_TREE);
  tree void_ftype_v16qi_v16qi_pchar
    = build_function_type_list (void_type_node,
				V16QI_type_node, V16QI_type_node,
				pchar_type_node, NULL_TREE);
  tree v2df_ftype_pcdouble
    = build_function_type_list (V2DF_type_node, pcdouble_type_node, NULL_TREE);
  tree v2df_ftype_v2df_v2df
    = build_function_type_list (V2DF_type_node,
				V2DF_type_node, V2DF_type_node, NULL_TREE);
  tree v16qi_ftype_v16qi_v16qi
    = build_function_type_list (V16QI_type_node,
				V16QI_type_node, V16QI_type_node, NULL_TREE);
  tree v8hi_ftype_v8hi_v8hi
    = build_function_type_list (V8HI_type_node,
				V8HI_type_node, V8HI_type_node, NULL_TREE);
  tree v4si_ftype_v4si_v4si
    = build_function_type_list (V4SI_type_node,
				V4SI_type_node, V4SI_type_node, NULL_TREE);
  tree v2di_ftype_v2di_v2di
    = build_function_type_list (V2DI_type_node,
				V2DI_type_node, V2DI_type_node, NULL_TREE);
  tree v2di_ftype_v2df_v2df
    = build_function_type_list (V2DI_type_node,
				V2DF_type_node, V2DF_type_node, NULL_TREE);
  tree v2df_ftype_v2df
    = build_function_type_list (V2DF_type_node, V2DF_type_node, NULL_TREE);
  tree v2di_ftype_v2di_int
    = build_function_type_list (V2DI_type_node,
				V2DI_type_node, integer_type_node, NULL_TREE);
  tree v4si_ftype_v4si_int
    = build_function_type_list (V4SI_type_node,
				V4SI_type_node, integer_type_node, NULL_TREE);
  tree v8hi_ftype_v8hi_int
    = build_function_type_list (V8HI_type_node,
				V8HI_type_node, integer_type_node, NULL_TREE);
  tree v4si_ftype_v8hi_v8hi
    = build_function_type_list (V4SI_type_node,
				V8HI_type_node, V8HI_type_node, NULL_TREE);
  tree di_ftype_v8qi_v8qi
    = build_function_type_list (long_long_unsigned_type_node,
				V8QI_type_node, V8QI_type_node, NULL_TREE);
  tree di_ftype_v2si_v2si
    = build_function_type_list (long_long_unsigned_type_node,
				V2SI_type_node, V2SI_type_node, NULL_TREE);
  tree v2di_ftype_v16qi_v16qi
    = build_function_type_list (V2DI_type_node,
				V16QI_type_node, V16QI_type_node, NULL_TREE);
  tree v2di_ftype_v4si_v4si
    = build_function_type_list (V2DI_type_node,
				V4SI_type_node, V4SI_type_node, NULL_TREE);
  tree int_ftype_v16qi
    = build_function_type_list (integer_type_node, V16QI_type_node, NULL_TREE);
  tree v16qi_ftype_pcchar
    = build_function_type_list (V16QI_type_node, pcchar_type_node, NULL_TREE);
  tree void_ftype_pchar_v16qi
    = build_function_type_list (void_type_node,
			        pchar_type_node, V16QI_type_node, NULL_TREE);

  tree float80_type;
  tree float128_type;
  tree ftype;

  /* The __float80 type.  */
  if (TYPE_MODE (long_double_type_node) == XFmode)
    (*lang_hooks.types.register_builtin_type) (long_double_type_node,
					       "__float80");
  else
    {
      /* The __float80 type.  */
      float80_type = make_node (REAL_TYPE);
      TYPE_PRECISION (float80_type) = 80;
      layout_type (float80_type);
      (*lang_hooks.types.register_builtin_type) (float80_type, "__float80");
    }

  if (TARGET_64BIT)
    {
      float128_type = make_node (REAL_TYPE);
      TYPE_PRECISION (float128_type) = 128;
      layout_type (float128_type);
      (*lang_hooks.types.register_builtin_type) (float128_type, "__float128");
    }

  /* Add all builtins that are more or less simple operations on two
     operands.  */
  for (i = 0, d = bdesc_2arg; i < ARRAY_SIZE (bdesc_2arg); i++, d++)
    {
      /* Use one of the operands; the target can have a different mode for
	 mask-generating compares.  */
      enum machine_mode mode;
      tree type;

      if (d->name == 0)
	continue;
      mode = insn_data[d->icode].operand[1].mode;

      switch (mode)
	{
	case V16QImode:
	  type = v16qi_ftype_v16qi_v16qi;
	  break;
	case V8HImode:
	  type = v8hi_ftype_v8hi_v8hi;
	  break;
	case V4SImode:
	  type = v4si_ftype_v4si_v4si;
	  break;
	case V2DImode:
	  type = v2di_ftype_v2di_v2di;
	  break;
	case V2DFmode:
	  type = v2df_ftype_v2df_v2df;
	  break;
	case V4SFmode:
	  type = v4sf_ftype_v4sf_v4sf;
	  break;
	case V8QImode:
	  type = v8qi_ftype_v8qi_v8qi;
	  break;
	case V4HImode:
	  type = v4hi_ftype_v4hi_v4hi;
	  break;
	case V2SImode:
	  type = v2si_ftype_v2si_v2si;
	  break;
	case DImode:
	  type = di_ftype_di_di;
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Override for comparisons.  */
      if (d->icode == CODE_FOR_sse_maskcmpv4sf3
	  || d->icode == CODE_FOR_sse_vmmaskcmpv4sf3)
	type = v4si_ftype_v4sf_v4sf;

      if (d->icode == CODE_FOR_sse2_maskcmpv2df3
	  || d->icode == CODE_FOR_sse2_vmmaskcmpv2df3)
	type = v2di_ftype_v2df_v2df;

      def_builtin (d->mask, d->name, type, d->code);
    }

  /* Add the remaining MMX insns with somewhat more complicated types.  */
  def_builtin (MASK_MMX, "__builtin_ia32_emms", void_ftype_void, IX86_BUILTIN_EMMS);
  def_builtin (MASK_MMX, "__builtin_ia32_psllw", v4hi_ftype_v4hi_di, IX86_BUILTIN_PSLLW);
  def_builtin (MASK_MMX, "__builtin_ia32_pslld", v2si_ftype_v2si_di, IX86_BUILTIN_PSLLD);
  def_builtin (MASK_MMX, "__builtin_ia32_psllq", di_ftype_di_di, IX86_BUILTIN_PSLLQ);

  def_builtin (MASK_MMX, "__builtin_ia32_psrlw", v4hi_ftype_v4hi_di, IX86_BUILTIN_PSRLW);
  def_builtin (MASK_MMX, "__builtin_ia32_psrld", v2si_ftype_v2si_di, IX86_BUILTIN_PSRLD);
  def_builtin (MASK_MMX, "__builtin_ia32_psrlq", di_ftype_di_di, IX86_BUILTIN_PSRLQ);

  def_builtin (MASK_MMX, "__builtin_ia32_psraw", v4hi_ftype_v4hi_di, IX86_BUILTIN_PSRAW);
  def_builtin (MASK_MMX, "__builtin_ia32_psrad", v2si_ftype_v2si_di, IX86_BUILTIN_PSRAD);

  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_pshufw", v4hi_ftype_v4hi_int, IX86_BUILTIN_PSHUFW);
  def_builtin (MASK_MMX, "__builtin_ia32_pmaddwd", v2si_ftype_v4hi_v4hi, IX86_BUILTIN_PMADDWD);

  /* comi/ucomi insns.  */
  for (i = 0, d = bdesc_comi; i < ARRAY_SIZE (bdesc_comi); i++, d++)
    if (d->mask == MASK_SSE2)
      def_builtin (d->mask, d->name, int_ftype_v2df_v2df, d->code);
    else
      def_builtin (d->mask, d->name, int_ftype_v4sf_v4sf, d->code);

  def_builtin (MASK_MMX, "__builtin_ia32_packsswb", v8qi_ftype_v4hi_v4hi, IX86_BUILTIN_PACKSSWB);
  def_builtin (MASK_MMX, "__builtin_ia32_packssdw", v4hi_ftype_v2si_v2si, IX86_BUILTIN_PACKSSDW);
  def_builtin (MASK_MMX, "__builtin_ia32_packuswb", v8qi_ftype_v4hi_v4hi, IX86_BUILTIN_PACKUSWB);

  def_builtin (MASK_SSE, "__builtin_ia32_ldmxcsr", void_ftype_unsigned, IX86_BUILTIN_LDMXCSR);
  def_builtin (MASK_SSE, "__builtin_ia32_stmxcsr", unsigned_ftype_void, IX86_BUILTIN_STMXCSR);
  def_builtin (MASK_SSE, "__builtin_ia32_cvtpi2ps", v4sf_ftype_v4sf_v2si, IX86_BUILTIN_CVTPI2PS);
  def_builtin (MASK_SSE, "__builtin_ia32_cvtps2pi", v2si_ftype_v4sf, IX86_BUILTIN_CVTPS2PI);
  def_builtin (MASK_SSE, "__builtin_ia32_cvtsi2ss", v4sf_ftype_v4sf_int, IX86_BUILTIN_CVTSI2SS);
  def_builtin (MASK_SSE | MASK_64BIT, "__builtin_ia32_cvtsi642ss", v4sf_ftype_v4sf_int64, IX86_BUILTIN_CVTSI642SS);
  def_builtin (MASK_SSE, "__builtin_ia32_cvtss2si", int_ftype_v4sf, IX86_BUILTIN_CVTSS2SI);
  def_builtin (MASK_SSE | MASK_64BIT, "__builtin_ia32_cvtss2si64", int64_ftype_v4sf, IX86_BUILTIN_CVTSS2SI64);
  def_builtin (MASK_SSE, "__builtin_ia32_cvttps2pi", v2si_ftype_v4sf, IX86_BUILTIN_CVTTPS2PI);
  def_builtin (MASK_SSE, "__builtin_ia32_cvttss2si", int_ftype_v4sf, IX86_BUILTIN_CVTTSS2SI);
  def_builtin (MASK_SSE | MASK_64BIT, "__builtin_ia32_cvttss2si64", int64_ftype_v4sf, IX86_BUILTIN_CVTTSS2SI64);

  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_maskmovq", void_ftype_v8qi_v8qi_pchar, IX86_BUILTIN_MASKMOVQ);

  def_builtin (MASK_SSE, "__builtin_ia32_loadups", v4sf_ftype_pcfloat, IX86_BUILTIN_LOADUPS);
  def_builtin (MASK_SSE, "__builtin_ia32_storeups", void_ftype_pfloat_v4sf, IX86_BUILTIN_STOREUPS);

  def_builtin (MASK_SSE, "__builtin_ia32_loadhps", v4sf_ftype_v4sf_pv2si, IX86_BUILTIN_LOADHPS);
  def_builtin (MASK_SSE, "__builtin_ia32_loadlps", v4sf_ftype_v4sf_pv2si, IX86_BUILTIN_LOADLPS);
  def_builtin (MASK_SSE, "__builtin_ia32_storehps", void_ftype_pv2si_v4sf, IX86_BUILTIN_STOREHPS);
  def_builtin (MASK_SSE, "__builtin_ia32_storelps", void_ftype_pv2si_v4sf, IX86_BUILTIN_STORELPS);

  def_builtin (MASK_SSE, "__builtin_ia32_movmskps", int_ftype_v4sf, IX86_BUILTIN_MOVMSKPS);
  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_pmovmskb", int_ftype_v8qi, IX86_BUILTIN_PMOVMSKB);
  def_builtin (MASK_SSE, "__builtin_ia32_movntps", void_ftype_pfloat_v4sf, IX86_BUILTIN_MOVNTPS);
  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_movntq", void_ftype_pdi_di, IX86_BUILTIN_MOVNTQ);

  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_sfence", void_ftype_void, IX86_BUILTIN_SFENCE);

  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_psadbw", di_ftype_v8qi_v8qi, IX86_BUILTIN_PSADBW);

  def_builtin (MASK_SSE, "__builtin_ia32_rcpps", v4sf_ftype_v4sf, IX86_BUILTIN_RCPPS);
  def_builtin (MASK_SSE, "__builtin_ia32_rcpss", v4sf_ftype_v4sf, IX86_BUILTIN_RCPSS);
  def_builtin (MASK_SSE, "__builtin_ia32_rsqrtps", v4sf_ftype_v4sf, IX86_BUILTIN_RSQRTPS);
  def_builtin (MASK_SSE, "__builtin_ia32_rsqrtss", v4sf_ftype_v4sf, IX86_BUILTIN_RSQRTSS);
  def_builtin (MASK_SSE, "__builtin_ia32_sqrtps", v4sf_ftype_v4sf, IX86_BUILTIN_SQRTPS);
  def_builtin (MASK_SSE, "__builtin_ia32_sqrtss", v4sf_ftype_v4sf, IX86_BUILTIN_SQRTSS);

  def_builtin (MASK_SSE, "__builtin_ia32_shufps", v4sf_ftype_v4sf_v4sf_int, IX86_BUILTIN_SHUFPS);

  /* Original 3DNow!  */
  def_builtin (MASK_3DNOW, "__builtin_ia32_femms", void_ftype_void, IX86_BUILTIN_FEMMS);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pavgusb", v8qi_ftype_v8qi_v8qi, IX86_BUILTIN_PAVGUSB);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pf2id", v2si_ftype_v2sf, IX86_BUILTIN_PF2ID);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfacc", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFACC);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfadd", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFADD);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfcmpeq", v2si_ftype_v2sf_v2sf, IX86_BUILTIN_PFCMPEQ);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfcmpge", v2si_ftype_v2sf_v2sf, IX86_BUILTIN_PFCMPGE);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfcmpgt", v2si_ftype_v2sf_v2sf, IX86_BUILTIN_PFCMPGT);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfmax", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFMAX);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfmin", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFMIN);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfmul", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFMUL);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfrcp", v2sf_ftype_v2sf, IX86_BUILTIN_PFRCP);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfrcpit1", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFRCPIT1);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfrcpit2", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFRCPIT2);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfrsqrt", v2sf_ftype_v2sf, IX86_BUILTIN_PFRSQRT);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfrsqit1", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFRSQIT1);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfsub", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFSUB);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pfsubr", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFSUBR);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pi2fd", v2sf_ftype_v2si, IX86_BUILTIN_PI2FD);
  def_builtin (MASK_3DNOW, "__builtin_ia32_pmulhrw", v4hi_ftype_v4hi_v4hi, IX86_BUILTIN_PMULHRW);

  /* 3DNow! extension as used in the Athlon CPU.  */
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pf2iw", v2si_ftype_v2sf, IX86_BUILTIN_PF2IW);
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pfnacc", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFNACC);
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pfpnacc", v2sf_ftype_v2sf_v2sf, IX86_BUILTIN_PFPNACC);
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pi2fw", v2sf_ftype_v2si, IX86_BUILTIN_PI2FW);
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pswapdsf", v2sf_ftype_v2sf, IX86_BUILTIN_PSWAPDSF);
  def_builtin (MASK_3DNOW_A, "__builtin_ia32_pswapdsi", v2si_ftype_v2si, IX86_BUILTIN_PSWAPDSI);

  /* SSE2 */
  def_builtin (MASK_SSE2, "__builtin_ia32_maskmovdqu", void_ftype_v16qi_v16qi_pchar, IX86_BUILTIN_MASKMOVDQU);

  def_builtin (MASK_SSE2, "__builtin_ia32_loadupd", v2df_ftype_pcdouble, IX86_BUILTIN_LOADUPD);
  def_builtin (MASK_SSE2, "__builtin_ia32_storeupd", void_ftype_pdouble_v2df, IX86_BUILTIN_STOREUPD);

  def_builtin (MASK_SSE2, "__builtin_ia32_loadhpd", v2df_ftype_v2df_pcdouble, IX86_BUILTIN_LOADHPD);
  def_builtin (MASK_SSE2, "__builtin_ia32_loadlpd", v2df_ftype_v2df_pcdouble, IX86_BUILTIN_LOADLPD);

  def_builtin (MASK_SSE2, "__builtin_ia32_movmskpd", int_ftype_v2df, IX86_BUILTIN_MOVMSKPD);
  def_builtin (MASK_SSE2, "__builtin_ia32_pmovmskb128", int_ftype_v16qi, IX86_BUILTIN_PMOVMSKB128);
  def_builtin (MASK_SSE2, "__builtin_ia32_movnti", void_ftype_pint_int, IX86_BUILTIN_MOVNTI);
  def_builtin (MASK_SSE2, "__builtin_ia32_movntpd", void_ftype_pdouble_v2df, IX86_BUILTIN_MOVNTPD);
  def_builtin (MASK_SSE2, "__builtin_ia32_movntdq", void_ftype_pv2di_v2di, IX86_BUILTIN_MOVNTDQ);

  def_builtin (MASK_SSE2, "__builtin_ia32_pshufd", v4si_ftype_v4si_int, IX86_BUILTIN_PSHUFD);
  def_builtin (MASK_SSE2, "__builtin_ia32_pshuflw", v8hi_ftype_v8hi_int, IX86_BUILTIN_PSHUFLW);
  def_builtin (MASK_SSE2, "__builtin_ia32_pshufhw", v8hi_ftype_v8hi_int, IX86_BUILTIN_PSHUFHW);
  def_builtin (MASK_SSE2, "__builtin_ia32_psadbw128", v2di_ftype_v16qi_v16qi, IX86_BUILTIN_PSADBW128);

  def_builtin (MASK_SSE2, "__builtin_ia32_sqrtpd", v2df_ftype_v2df, IX86_BUILTIN_SQRTPD);
  def_builtin (MASK_SSE2, "__builtin_ia32_sqrtsd", v2df_ftype_v2df, IX86_BUILTIN_SQRTSD);

  def_builtin (MASK_SSE2, "__builtin_ia32_shufpd", v2df_ftype_v2df_v2df_int, IX86_BUILTIN_SHUFPD);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtdq2pd", v2df_ftype_v4si, IX86_BUILTIN_CVTDQ2PD);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtdq2ps", v4sf_ftype_v4si, IX86_BUILTIN_CVTDQ2PS);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtpd2dq", v4si_ftype_v2df, IX86_BUILTIN_CVTPD2DQ);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtpd2pi", v2si_ftype_v2df, IX86_BUILTIN_CVTPD2PI);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtpd2ps", v4sf_ftype_v2df, IX86_BUILTIN_CVTPD2PS);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvttpd2dq", v4si_ftype_v2df, IX86_BUILTIN_CVTTPD2DQ);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvttpd2pi", v2si_ftype_v2df, IX86_BUILTIN_CVTTPD2PI);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtpi2pd", v2df_ftype_v2si, IX86_BUILTIN_CVTPI2PD);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtsd2si", int_ftype_v2df, IX86_BUILTIN_CVTSD2SI);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvttsd2si", int_ftype_v2df, IX86_BUILTIN_CVTTSD2SI);
  def_builtin (MASK_SSE2 | MASK_64BIT, "__builtin_ia32_cvtsd2si64", int64_ftype_v2df, IX86_BUILTIN_CVTSD2SI64);
  def_builtin (MASK_SSE2 | MASK_64BIT, "__builtin_ia32_cvttsd2si64", int64_ftype_v2df, IX86_BUILTIN_CVTTSD2SI64);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtps2dq", v4si_ftype_v4sf, IX86_BUILTIN_CVTPS2DQ);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtps2pd", v2df_ftype_v4sf, IX86_BUILTIN_CVTPS2PD);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvttps2dq", v4si_ftype_v4sf, IX86_BUILTIN_CVTTPS2DQ);

  def_builtin (MASK_SSE2, "__builtin_ia32_cvtsi2sd", v2df_ftype_v2df_int, IX86_BUILTIN_CVTSI2SD);
  def_builtin (MASK_SSE2 | MASK_64BIT, "__builtin_ia32_cvtsi642sd", v2df_ftype_v2df_int64, IX86_BUILTIN_CVTSI642SD);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtsd2ss", v4sf_ftype_v4sf_v2df, IX86_BUILTIN_CVTSD2SS);
  def_builtin (MASK_SSE2, "__builtin_ia32_cvtss2sd", v2df_ftype_v2df_v4sf, IX86_BUILTIN_CVTSS2SD);

  def_builtin (MASK_SSE2, "__builtin_ia32_clflush", void_ftype_pcvoid, IX86_BUILTIN_CLFLUSH);
  def_builtin (MASK_SSE2, "__builtin_ia32_lfence", void_ftype_void, IX86_BUILTIN_LFENCE);
  def_builtin (MASK_SSE2, "__builtin_ia32_mfence", void_ftype_void, IX86_BUILTIN_MFENCE);

  def_builtin (MASK_SSE2, "__builtin_ia32_loaddqu", v16qi_ftype_pcchar, IX86_BUILTIN_LOADDQU);
  def_builtin (MASK_SSE2, "__builtin_ia32_storedqu", void_ftype_pchar_v16qi, IX86_BUILTIN_STOREDQU);

  def_builtin (MASK_SSE2, "__builtin_ia32_pmuludq", di_ftype_v2si_v2si, IX86_BUILTIN_PMULUDQ);
  def_builtin (MASK_SSE2, "__builtin_ia32_pmuludq128", v2di_ftype_v4si_v4si, IX86_BUILTIN_PMULUDQ128);

  def_builtin (MASK_SSE2, "__builtin_ia32_psllw128", v8hi_ftype_v8hi_v8hi, IX86_BUILTIN_PSLLW128);
  def_builtin (MASK_SSE2, "__builtin_ia32_pslld128", v4si_ftype_v4si_v4si, IX86_BUILTIN_PSLLD128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psllq128", v2di_ftype_v2di_v2di, IX86_BUILTIN_PSLLQ128);

  def_builtin (MASK_SSE2, "__builtin_ia32_psrlw128", v8hi_ftype_v8hi_v8hi, IX86_BUILTIN_PSRLW128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrld128", v4si_ftype_v4si_v4si, IX86_BUILTIN_PSRLD128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrlq128", v2di_ftype_v2di_v2di, IX86_BUILTIN_PSRLQ128);

  def_builtin (MASK_SSE2, "__builtin_ia32_psraw128", v8hi_ftype_v8hi_v8hi, IX86_BUILTIN_PSRAW128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrad128", v4si_ftype_v4si_v4si, IX86_BUILTIN_PSRAD128);

  def_builtin (MASK_SSE2, "__builtin_ia32_pslldqi128", v2di_ftype_v2di_int, IX86_BUILTIN_PSLLDQI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psllwi128", v8hi_ftype_v8hi_int, IX86_BUILTIN_PSLLWI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_pslldi128", v4si_ftype_v4si_int, IX86_BUILTIN_PSLLDI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psllqi128", v2di_ftype_v2di_int, IX86_BUILTIN_PSLLQI128);

  def_builtin (MASK_SSE2, "__builtin_ia32_psrldqi128", v2di_ftype_v2di_int, IX86_BUILTIN_PSRLDQI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrlwi128", v8hi_ftype_v8hi_int, IX86_BUILTIN_PSRLWI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrldi128", v4si_ftype_v4si_int, IX86_BUILTIN_PSRLDI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psrlqi128", v2di_ftype_v2di_int, IX86_BUILTIN_PSRLQI128);

  def_builtin (MASK_SSE2, "__builtin_ia32_psrawi128", v8hi_ftype_v8hi_int, IX86_BUILTIN_PSRAWI128);
  def_builtin (MASK_SSE2, "__builtin_ia32_psradi128", v4si_ftype_v4si_int, IX86_BUILTIN_PSRADI128);

  def_builtin (MASK_SSE2, "__builtin_ia32_pmaddwd128", v4si_ftype_v8hi_v8hi, IX86_BUILTIN_PMADDWD128);

  /* Prescott New Instructions.  */
  def_builtin (MASK_SSE3, "__builtin_ia32_monitor",
	       void_ftype_pcvoid_unsigned_unsigned,
	       IX86_BUILTIN_MONITOR);
  def_builtin (MASK_SSE3, "__builtin_ia32_mwait",
	       void_ftype_unsigned_unsigned,
	       IX86_BUILTIN_MWAIT);
  def_builtin (MASK_SSE3, "__builtin_ia32_movshdup",
	       v4sf_ftype_v4sf,
	       IX86_BUILTIN_MOVSHDUP);
  def_builtin (MASK_SSE3, "__builtin_ia32_movsldup",
	       v4sf_ftype_v4sf,
	       IX86_BUILTIN_MOVSLDUP);
  def_builtin (MASK_SSE3, "__builtin_ia32_lddqu",
	       v16qi_ftype_pcchar, IX86_BUILTIN_LDDQU);

  /* Access to the vec_init patterns.  */
  ftype = build_function_type_list (V2SI_type_node, integer_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_MMX, "__builtin_ia32_vec_init_v2si",
	       ftype, IX86_BUILTIN_VEC_INIT_V2SI);

  ftype = build_function_type_list (V4HI_type_node, short_integer_type_node,
				    short_integer_type_node,
				    short_integer_type_node,
				    short_integer_type_node, NULL_TREE);
  def_builtin (MASK_MMX, "__builtin_ia32_vec_init_v4hi",
	       ftype, IX86_BUILTIN_VEC_INIT_V4HI);

  ftype = build_function_type_list (V8QI_type_node, char_type_node,
				    char_type_node, char_type_node,
				    char_type_node, char_type_node,
				    char_type_node, char_type_node,
				    char_type_node, NULL_TREE);
  def_builtin (MASK_MMX, "__builtin_ia32_vec_init_v8qi",
	       ftype, IX86_BUILTIN_VEC_INIT_V8QI);

  /* Access to the vec_extract patterns.  */
  ftype = build_function_type_list (double_type_node, V2DF_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_ext_v2df",
	       ftype, IX86_BUILTIN_VEC_EXT_V2DF);

  ftype = build_function_type_list (long_long_integer_type_node,
				    V2DI_type_node, integer_type_node,
				    NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_ext_v2di",
	       ftype, IX86_BUILTIN_VEC_EXT_V2DI);

  ftype = build_function_type_list (float_type_node, V4SF_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE, "__builtin_ia32_vec_ext_v4sf",
	       ftype, IX86_BUILTIN_VEC_EXT_V4SF);

  ftype = build_function_type_list (intSI_type_node, V4SI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_ext_v4si",
	       ftype, IX86_BUILTIN_VEC_EXT_V4SI);

  ftype = build_function_type_list (intHI_type_node, V8HI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_ext_v8hi",
	       ftype, IX86_BUILTIN_VEC_EXT_V8HI);

  ftype = build_function_type_list (intHI_type_node, V4HI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_vec_ext_v4hi",
	       ftype, IX86_BUILTIN_VEC_EXT_V4HI);

  ftype = build_function_type_list (intSI_type_node, V2SI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_MMX, "__builtin_ia32_vec_ext_v2si",
	       ftype, IX86_BUILTIN_VEC_EXT_V2SI);

  ftype = build_function_type_list (intQI_type_node, V16QI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_ext_v16qi", ftype, IX86_BUILTIN_VEC_EXT_V16QI);

  /* Access to the vec_set patterns.  */
  ftype = build_function_type_list (V8HI_type_node, V8HI_type_node,
				    intHI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE2, "__builtin_ia32_vec_set_v8hi",
	       ftype, IX86_BUILTIN_VEC_SET_V8HI);

  ftype = build_function_type_list (V4HI_type_node, V4HI_type_node,
				    intHI_type_node,
				    integer_type_node, NULL_TREE);
  def_builtin (MASK_SSE | MASK_3DNOW_A, "__builtin_ia32_vec_set_v4hi",
	       ftype, IX86_BUILTIN_VEC_SET_V4HI);
}

/* Errors in the source file can cause expand_expr to return const0_rtx
   where we expect a vector.  To avoid crashing, use one of the vector
   clear instructions.  */
static rtx
safe_vector_operand (rtx x, enum machine_mode mode)
{
  if (x == const0_rtx)
    x = CONST0_RTX (mode);
  return x;
}

/* Subroutine of ix86_expand_builtin to take care of binop insns.  */

static rtx
ix86_expand_binop_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat, xops[3];
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx op0 = expand_normal (arg0);
  rtx op1 = expand_normal (arg1);
  enum machine_mode tmode = insn_data[icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;
  enum machine_mode mode1 = insn_data[icode].operand[2].mode;

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);
  if (VECTOR_MODE_P (mode1))
    op1 = safe_vector_operand (op1, mode1);

  if (optimize || !target
      || GET_MODE (target) != tmode
      || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);

  if (GET_MODE (op1) == SImode && mode1 == TImode)
    {
      rtx x = gen_reg_rtx (V4SImode);
      emit_insn (gen_sse2_loadd (x, op1));
      op1 = gen_lowpart (TImode, x);
    }

  /* The insn must want input operands in the same modes as the
     result.  */
  gcc_assert ((GET_MODE (op0) == mode0 || GET_MODE (op0) == VOIDmode)
	      && (GET_MODE (op1) == mode1 || GET_MODE (op1) == VOIDmode));

  if (!(*insn_data[icode].operand[1].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);
  if (!(*insn_data[icode].operand[2].predicate) (op1, mode1))
    op1 = copy_to_mode_reg (mode1, op1);

  /* ??? Using ix86_fixup_binary_operands is problematic when
     we've got mismatched modes.  Fake it.  */

  xops[0] = target;
  xops[1] = op0;
  xops[2] = op1;

  if (tmode == mode0 && tmode == mode1)
    {
      target = ix86_fixup_binary_operands (UNKNOWN, tmode, xops);
      op0 = xops[1];
      op1 = xops[2];
    }
  else if (optimize || !ix86_binary_operator_ok (UNKNOWN, tmode, xops))
    {
      op0 = force_reg (mode0, op0);
      op1 = force_reg (mode1, op1);
      target = gen_reg_rtx (tmode);
    }

  pat = GEN_FCN (icode) (target, op0, op1);
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

/* Subroutine of ix86_expand_builtin to take care of stores.  */

static rtx
ix86_expand_store_builtin (enum insn_code icode, tree arglist)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx op0 = expand_normal (arg0);
  rtx op1 = expand_normal (arg1);
  enum machine_mode mode0 = insn_data[icode].operand[0].mode;
  enum machine_mode mode1 = insn_data[icode].operand[1].mode;

  if (VECTOR_MODE_P (mode1))
    op1 = safe_vector_operand (op1, mode1);

  op0 = gen_rtx_MEM (mode0, copy_to_mode_reg (Pmode, op0));
  op1 = copy_to_mode_reg (mode1, op1);

  pat = GEN_FCN (icode) (op0, op1);
  if (pat)
    emit_insn (pat);
  return 0;
}

/* Subroutine of ix86_expand_builtin to take care of unop insns.  */

static rtx
ix86_expand_unop_builtin (enum insn_code icode, tree arglist,
			  rtx target, int do_load)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  rtx op0 = expand_normal (arg0);
  enum machine_mode tmode = insn_data[icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;

  if (optimize || !target
      || GET_MODE (target) != tmode
      || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);
  if (do_load)
    op0 = gen_rtx_MEM (mode0, copy_to_mode_reg (Pmode, op0));
  else
    {
      if (VECTOR_MODE_P (mode0))
	op0 = safe_vector_operand (op0, mode0);

      if ((optimize && !register_operand (op0, mode0))
	  || ! (*insn_data[icode].operand[1].predicate) (op0, mode0))
	op0 = copy_to_mode_reg (mode0, op0);
    }

  pat = GEN_FCN (icode) (target, op0);
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

/* Subroutine of ix86_expand_builtin to take care of three special unop insns:
   sqrtss, rsqrtss, rcpss.  */

static rtx
ix86_expand_unop1_builtin (enum insn_code icode, tree arglist, rtx target)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  rtx op1, op0 = expand_normal (arg0);
  enum machine_mode tmode = insn_data[icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;

  if (optimize || !target
      || GET_MODE (target) != tmode
      || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);

  if ((optimize && !register_operand (op0, mode0))
      || ! (*insn_data[icode].operand[1].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);

  op1 = op0;
  if (! (*insn_data[icode].operand[2].predicate) (op1, mode0))
    op1 = copy_to_mode_reg (mode0, op1);

  pat = GEN_FCN (icode) (target, op0, op1);
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

/* Subroutine of ix86_expand_builtin to take care of comparison insns.  */

static rtx
ix86_expand_sse_compare (const struct builtin_description *d, tree arglist,
			 rtx target)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx op0 = expand_normal (arg0);
  rtx op1 = expand_normal (arg1);
  rtx op2;
  enum machine_mode tmode = insn_data[d->icode].operand[0].mode;
  enum machine_mode mode0 = insn_data[d->icode].operand[1].mode;
  enum machine_mode mode1 = insn_data[d->icode].operand[2].mode;
  enum rtx_code comparison = d->comparison;

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);
  if (VECTOR_MODE_P (mode1))
    op1 = safe_vector_operand (op1, mode1);

  /* Swap operands if we have a comparison that isn't available in
     hardware.  */
  if (d->flag & BUILTIN_DESC_SWAP_OPERANDS)
    {
      rtx tmp = gen_reg_rtx (mode1);
      emit_move_insn (tmp, op1);
      op1 = op0;
      op0 = tmp;
    }

  if (optimize || !target
      || GET_MODE (target) != tmode
      || ! (*insn_data[d->icode].operand[0].predicate) (target, tmode))
    target = gen_reg_rtx (tmode);

  if ((optimize && !register_operand (op0, mode0))
      || ! (*insn_data[d->icode].operand[1].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);
  if ((optimize && !register_operand (op1, mode1))
      || ! (*insn_data[d->icode].operand[2].predicate) (op1, mode1))
    op1 = copy_to_mode_reg (mode1, op1);

  op2 = gen_rtx_fmt_ee (comparison, mode0, op0, op1);
  pat = GEN_FCN (d->icode) (target, op0, op1, op2);
  if (! pat)
    return 0;
  emit_insn (pat);
  return target;
}

/* Subroutine of ix86_expand_builtin to take care of comi insns.  */

static rtx
ix86_expand_sse_comi (const struct builtin_description *d, tree arglist,
		      rtx target)
{
  rtx pat;
  tree arg0 = TREE_VALUE (arglist);
  tree arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  rtx op0 = expand_normal (arg0);
  rtx op1 = expand_normal (arg1);
  rtx op2;
  enum machine_mode mode0 = insn_data[d->icode].operand[0].mode;
  enum machine_mode mode1 = insn_data[d->icode].operand[1].mode;
  enum rtx_code comparison = d->comparison;

  if (VECTOR_MODE_P (mode0))
    op0 = safe_vector_operand (op0, mode0);
  if (VECTOR_MODE_P (mode1))
    op1 = safe_vector_operand (op1, mode1);

  /* Swap operands if we have a comparison that isn't available in
     hardware.  */
  if (d->flag & BUILTIN_DESC_SWAP_OPERANDS)
    {
      rtx tmp = op1;
      op1 = op0;
      op0 = tmp;
    }

  target = gen_reg_rtx (SImode);
  emit_move_insn (target, const0_rtx);
  target = gen_rtx_SUBREG (QImode, target, 0);

  if ((optimize && !register_operand (op0, mode0))
      || !(*insn_data[d->icode].operand[0].predicate) (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);
  if ((optimize && !register_operand (op1, mode1))
      || !(*insn_data[d->icode].operand[1].predicate) (op1, mode1))
    op1 = copy_to_mode_reg (mode1, op1);

  op2 = gen_rtx_fmt_ee (comparison, mode0, op0, op1);
  pat = GEN_FCN (d->icode) (op0, op1);
  if (! pat)
    return 0;
  emit_insn (pat);
  emit_insn (gen_rtx_SET (VOIDmode,
			  gen_rtx_STRICT_LOW_PART (VOIDmode, target),
			  gen_rtx_fmt_ee (comparison, QImode,
					  SET_DEST (pat),
					  const0_rtx)));

  return SUBREG_REG (target);
}

/* Return the integer constant in ARG.  Constrain it to be in the range
   of the subparts of VEC_TYPE; issue an error if not.  */

static int
get_element_number (tree vec_type, tree arg)
{
  unsigned HOST_WIDE_INT elt, max = TYPE_VECTOR_SUBPARTS (vec_type) - 1;

  if (!host_integerp (arg, 1)
      || (elt = tree_low_cst (arg, 1), elt > max))
    {
      error ("selector must be an integer constant in the range 0..%wi", max);
      return 0;
    }

  return elt;
}

/* A subroutine of ix86_expand_builtin.  These builtins are a wrapper around
   ix86_expand_vector_init.  We DO have language-level syntax for this, in
   the form of  (type){ init-list }.  Except that since we can't place emms
   instructions from inside the compiler, we can't allow the use of MMX
   registers unless the user explicitly asks for it.  So we do *not* define
   vec_set/vec_extract/vec_init patterns for MMX modes in mmx.md.  Instead
   we have builtins invoked by mmintrin.h that gives us license to emit
   these sorts of instructions.  */

static rtx
ix86_expand_vec_init_builtin (tree type, tree arglist, rtx target)
{
  enum machine_mode tmode = TYPE_MODE (type);
  enum machine_mode inner_mode = GET_MODE_INNER (tmode);
  int i, n_elt = GET_MODE_NUNITS (tmode);
  rtvec v = rtvec_alloc (n_elt);

  gcc_assert (VECTOR_MODE_P (tmode));

  for (i = 0; i < n_elt; ++i, arglist = TREE_CHAIN (arglist))
    {
      rtx x = expand_normal (TREE_VALUE (arglist));
      RTVEC_ELT (v, i) = gen_lowpart (inner_mode, x);
    }

  gcc_assert (arglist == NULL);

  if (!target || !register_operand (target, tmode))
    target = gen_reg_rtx (tmode);

  ix86_expand_vector_init (true, target, gen_rtx_PARALLEL (tmode, v));
  return target;
}

/* A subroutine of ix86_expand_builtin.  These builtins are a wrapper around
   ix86_expand_vector_extract.  They would be redundant (for non-MMX) if we
   had a language-level syntax for referencing vector elements.  */

static rtx
ix86_expand_vec_ext_builtin (tree arglist, rtx target)
{
  enum machine_mode tmode, mode0;
  tree arg0, arg1;
  int elt;
  rtx op0;

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));

  op0 = expand_normal (arg0);
  elt = get_element_number (TREE_TYPE (arg0), arg1);

  tmode = TYPE_MODE (TREE_TYPE (TREE_TYPE (arg0)));
  mode0 = TYPE_MODE (TREE_TYPE (arg0));
  gcc_assert (VECTOR_MODE_P (mode0));

  op0 = force_reg (mode0, op0);

  if (optimize || !target || !register_operand (target, tmode))
    target = gen_reg_rtx (tmode);

  ix86_expand_vector_extract (true, target, op0, elt);

  return target;
}

/* A subroutine of ix86_expand_builtin.  These builtins are a wrapper around
   ix86_expand_vector_set.  They would be redundant (for non-MMX) if we had
   a language-level syntax for referencing vector elements.  */

static rtx
ix86_expand_vec_set_builtin (tree arglist)
{
  enum machine_mode tmode, mode1;
  tree arg0, arg1, arg2;
  int elt;
  rtx op0, op1, target;

  arg0 = TREE_VALUE (arglist);
  arg1 = TREE_VALUE (TREE_CHAIN (arglist));
  arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));

  tmode = TYPE_MODE (TREE_TYPE (arg0));
  mode1 = TYPE_MODE (TREE_TYPE (TREE_TYPE (arg0)));
  gcc_assert (VECTOR_MODE_P (tmode));

  op0 = expand_expr (arg0, NULL_RTX, tmode, 0);
  op1 = expand_expr (arg1, NULL_RTX, mode1, 0);
  elt = get_element_number (TREE_TYPE (arg0), arg2);

  if (GET_MODE (op1) != mode1 && GET_MODE (op1) != VOIDmode)
    op1 = convert_modes (mode1, GET_MODE (op1), op1, true);

  op0 = force_reg (tmode, op0);
  op1 = force_reg (mode1, op1);

  /* OP0 is the source of these builtin functions and shouldn't be
     modified.  Create a copy, use it and return it as target.  */
  target = gen_reg_rtx (tmode);
  emit_move_insn (target, op0);
  ix86_expand_vector_set (true, target, op1, elt);

  return target;
}

/* Expand an expression EXP that calls a built-in function,
   with result going to TARGET if that's convenient
   (and in mode MODE if that's convenient).
   SUBTARGET may be used as the target for computing one of EXP's operands.
   IGNORE is nonzero if the value is to be ignored.  */

static rtx
ix86_expand_builtin (tree exp, rtx target, rtx subtarget ATTRIBUTE_UNUSED,
		     enum machine_mode mode ATTRIBUTE_UNUSED,
		     int ignore ATTRIBUTE_UNUSED)
{
  const struct builtin_description *d;
  size_t i;
  enum insn_code icode;
  tree fndecl = TREE_OPERAND (TREE_OPERAND (exp, 0), 0);
  tree arglist = TREE_OPERAND (exp, 1);
  tree arg0, arg1, arg2;
  rtx op0, op1, op2, pat;
  enum machine_mode tmode, mode0, mode1, mode2;
  unsigned int fcode = DECL_FUNCTION_CODE (fndecl);

  switch (fcode)
    {
    case IX86_BUILTIN_EMMS:
      emit_insn (gen_mmx_emms ());
      return 0;

    case IX86_BUILTIN_SFENCE:
      emit_insn (gen_sse_sfence ());
      return 0;

    case IX86_BUILTIN_MASKMOVQ:
    case IX86_BUILTIN_MASKMOVDQU:
      icode = (fcode == IX86_BUILTIN_MASKMOVQ
	       ? CODE_FOR_mmx_maskmovq
	       : CODE_FOR_sse2_maskmovdqu);
      /* Note the arg order is different from the operand order.  */
      arg1 = TREE_VALUE (arglist);
      arg2 = TREE_VALUE (TREE_CHAIN (arglist));
      arg0 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      op2 = expand_normal (arg2);
      mode0 = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;
      mode2 = insn_data[icode].operand[2].mode;

      op0 = force_reg (Pmode, op0);
      op0 = gen_rtx_MEM (mode1, op0);

      if (! (*insn_data[icode].operand[0].predicate) (op0, mode0))
	op0 = copy_to_mode_reg (mode0, op0);
      if (! (*insn_data[icode].operand[1].predicate) (op1, mode1))
	op1 = copy_to_mode_reg (mode1, op1);
      if (! (*insn_data[icode].operand[2].predicate) (op2, mode2))
	op2 = copy_to_mode_reg (mode2, op2);
      pat = GEN_FCN (icode) (op0, op1, op2);
      if (! pat)
	return 0;
      emit_insn (pat);
      return 0;

    case IX86_BUILTIN_SQRTSS:
      return ix86_expand_unop1_builtin (CODE_FOR_sse_vmsqrtv4sf2, arglist, target);
    case IX86_BUILTIN_RSQRTSS:
      return ix86_expand_unop1_builtin (CODE_FOR_sse_vmrsqrtv4sf2, arglist, target);
    case IX86_BUILTIN_RCPSS:
      return ix86_expand_unop1_builtin (CODE_FOR_sse_vmrcpv4sf2, arglist, target);

    case IX86_BUILTIN_LOADUPS:
      return ix86_expand_unop_builtin (CODE_FOR_sse_movups, arglist, target, 1);

    case IX86_BUILTIN_STOREUPS:
      return ix86_expand_store_builtin (CODE_FOR_sse_movups, arglist);

    case IX86_BUILTIN_LOADHPS:
    case IX86_BUILTIN_LOADLPS:
    case IX86_BUILTIN_LOADHPD:
    case IX86_BUILTIN_LOADLPD:
      icode = (fcode == IX86_BUILTIN_LOADHPS ? CODE_FOR_sse_loadhps
	       : fcode == IX86_BUILTIN_LOADLPS ? CODE_FOR_sse_loadlps
	       : fcode == IX86_BUILTIN_LOADHPD ? CODE_FOR_sse2_loadhpd
	       : CODE_FOR_sse2_loadlpd);
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      tmode = insn_data[icode].operand[0].mode;
      mode0 = insn_data[icode].operand[1].mode;
      mode1 = insn_data[icode].operand[2].mode;

      op0 = force_reg (mode0, op0);
      op1 = gen_rtx_MEM (mode1, copy_to_mode_reg (Pmode, op1));
      if (optimize || target == 0
	  || GET_MODE (target) != tmode
	  || !register_operand (target, tmode))
	target = gen_reg_rtx (tmode);
      pat = GEN_FCN (icode) (target, op0, op1);
      if (! pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_STOREHPS:
    case IX86_BUILTIN_STORELPS:
      icode = (fcode == IX86_BUILTIN_STOREHPS ? CODE_FOR_sse_storehps
	       : CODE_FOR_sse_storelps);
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      mode0 = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;

      op0 = gen_rtx_MEM (mode0, copy_to_mode_reg (Pmode, op0));
      op1 = force_reg (mode1, op1);

      pat = GEN_FCN (icode) (op0, op1);
      if (! pat)
	return 0;
      emit_insn (pat);
      return const0_rtx;

    case IX86_BUILTIN_MOVNTPS:
      return ix86_expand_store_builtin (CODE_FOR_sse_movntv4sf, arglist);
    case IX86_BUILTIN_MOVNTQ:
      return ix86_expand_store_builtin (CODE_FOR_sse_movntdi, arglist);

    case IX86_BUILTIN_LDMXCSR:
      op0 = expand_normal (TREE_VALUE (arglist));
      target = assign_386_stack_local (SImode, SLOT_VIRTUAL);
      emit_move_insn (target, op0);
      emit_insn (gen_sse_ldmxcsr (target));
      return 0;

    case IX86_BUILTIN_STMXCSR:
      target = assign_386_stack_local (SImode, SLOT_VIRTUAL);
      emit_insn (gen_sse_stmxcsr (target));
      return copy_to_mode_reg (SImode, target);

    case IX86_BUILTIN_SHUFPS:
    case IX86_BUILTIN_SHUFPD:
      icode = (fcode == IX86_BUILTIN_SHUFPS
	       ? CODE_FOR_sse_shufps
	       : CODE_FOR_sse2_shufpd);
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      op2 = expand_normal (arg2);
      tmode = insn_data[icode].operand[0].mode;
      mode0 = insn_data[icode].operand[1].mode;
      mode1 = insn_data[icode].operand[2].mode;
      mode2 = insn_data[icode].operand[3].mode;

      if (! (*insn_data[icode].operand[1].predicate) (op0, mode0))
	op0 = copy_to_mode_reg (mode0, op0);
      if ((optimize && !register_operand (op1, mode1))
	  || !(*insn_data[icode].operand[2].predicate) (op1, mode1))
	op1 = copy_to_mode_reg (mode1, op1);
      if (! (*insn_data[icode].operand[3].predicate) (op2, mode2))
	{
	  /* @@@ better error message */
	  error ("mask must be an immediate");
	  return gen_reg_rtx (tmode);
	}
      if (optimize || target == 0
	  || GET_MODE (target) != tmode
	  || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
	target = gen_reg_rtx (tmode);
      pat = GEN_FCN (icode) (target, op0, op1, op2);
      if (! pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_PSHUFW:
    case IX86_BUILTIN_PSHUFD:
    case IX86_BUILTIN_PSHUFHW:
    case IX86_BUILTIN_PSHUFLW:
      icode = (  fcode == IX86_BUILTIN_PSHUFHW ? CODE_FOR_sse2_pshufhw
	       : fcode == IX86_BUILTIN_PSHUFLW ? CODE_FOR_sse2_pshuflw
	       : fcode == IX86_BUILTIN_PSHUFD ? CODE_FOR_sse2_pshufd
	       : CODE_FOR_mmx_pshufw);
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      tmode = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;
      mode2 = insn_data[icode].operand[2].mode;

      if (! (*insn_data[icode].operand[1].predicate) (op0, mode1))
	op0 = copy_to_mode_reg (mode1, op0);
      if (! (*insn_data[icode].operand[2].predicate) (op1, mode2))
	{
	  /* @@@ better error message */
	  error ("mask must be an immediate");
	  return const0_rtx;
	}
      if (target == 0
	  || GET_MODE (target) != tmode
	  || ! (*insn_data[icode].operand[0].predicate) (target, tmode))
	target = gen_reg_rtx (tmode);
      pat = GEN_FCN (icode) (target, op0, op1);
      if (! pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_PSLLWI128:
      icode = CODE_FOR_ashlv8hi3;
      goto do_pshifti;
    case IX86_BUILTIN_PSLLDI128:
      icode = CODE_FOR_ashlv4si3;
      goto do_pshifti;
    case IX86_BUILTIN_PSLLQI128:
      icode = CODE_FOR_ashlv2di3;
      goto do_pshifti;
    case IX86_BUILTIN_PSRAWI128:
      icode = CODE_FOR_ashrv8hi3;
      goto do_pshifti;
    case IX86_BUILTIN_PSRADI128:
      icode = CODE_FOR_ashrv4si3;
      goto do_pshifti;
    case IX86_BUILTIN_PSRLWI128:
      icode = CODE_FOR_lshrv8hi3;
      goto do_pshifti;
    case IX86_BUILTIN_PSRLDI128:
      icode = CODE_FOR_lshrv4si3;
      goto do_pshifti;
    case IX86_BUILTIN_PSRLQI128:
      icode = CODE_FOR_lshrv2di3;
      goto do_pshifti;
    do_pshifti:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
      op1 = expand_expr (arg1, NULL_RTX, VOIDmode, 0);

      if (GET_CODE (op1) != CONST_INT)
	{
	  error ("shift must be an immediate");
	  return const0_rtx;
	}
      if (INTVAL (op1) < 0 || INTVAL (op1) > 255)
	op1 = GEN_INT (255);

      tmode = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;
      if (! (*insn_data[icode].operand[1].predicate) (op0, mode1))
	op0 = copy_to_reg (op0);

      target = gen_reg_rtx (tmode);
      pat = GEN_FCN (icode) (target, op0, op1);
      if (!pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_PSLLW128:
      icode = CODE_FOR_ashlv8hi3;
      goto do_pshift;
    case IX86_BUILTIN_PSLLD128:
      icode = CODE_FOR_ashlv4si3;
      goto do_pshift;
    case IX86_BUILTIN_PSLLQ128:
      icode = CODE_FOR_ashlv2di3;
      goto do_pshift;
    case IX86_BUILTIN_PSRAW128:
      icode = CODE_FOR_ashrv8hi3;
      goto do_pshift;
    case IX86_BUILTIN_PSRAD128:
      icode = CODE_FOR_ashrv4si3;
      goto do_pshift;
    case IX86_BUILTIN_PSRLW128:
      icode = CODE_FOR_lshrv8hi3;
      goto do_pshift;
    case IX86_BUILTIN_PSRLD128:
      icode = CODE_FOR_lshrv4si3;
      goto do_pshift;
    case IX86_BUILTIN_PSRLQ128:
      icode = CODE_FOR_lshrv2di3;
      goto do_pshift;
    do_pshift:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_expr (arg0, NULL_RTX, VOIDmode, 0);
      op1 = expand_expr (arg1, NULL_RTX, VOIDmode, 0);

      tmode = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;

      if (! (*insn_data[icode].operand[1].predicate) (op0, mode1))
	op0 = copy_to_reg (op0);

      op1 = simplify_gen_subreg (TImode, op1, GET_MODE (op1), 0);
      if (! (*insn_data[icode].operand[2].predicate) (op1, TImode))
	op1 = copy_to_reg (op1);

      target = gen_reg_rtx (tmode);
      pat = GEN_FCN (icode) (target, op0, op1);
      if (!pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_PSLLDQI128:
    case IX86_BUILTIN_PSRLDQI128:
      icode = (fcode == IX86_BUILTIN_PSLLDQI128 ? CODE_FOR_sse2_ashlti3
	       : CODE_FOR_sse2_lshrti3);
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      tmode = insn_data[icode].operand[0].mode;
      mode1 = insn_data[icode].operand[1].mode;
      mode2 = insn_data[icode].operand[2].mode;

      if (! (*insn_data[icode].operand[1].predicate) (op0, mode1))
	{
	  op0 = copy_to_reg (op0);
	  op0 = simplify_gen_subreg (mode1, op0, GET_MODE (op0), 0);
	}
      if (! (*insn_data[icode].operand[2].predicate) (op1, mode2))
	{
	  error ("shift must be an immediate");
	  return const0_rtx;
	}
      target = gen_reg_rtx (V2DImode);
      pat = GEN_FCN (icode) (simplify_gen_subreg (tmode, target, V2DImode, 0),
			     op0, op1);
      if (! pat)
	return 0;
      emit_insn (pat);
      return target;

    case IX86_BUILTIN_FEMMS:
      emit_insn (gen_mmx_femms ());
      return NULL_RTX;

    case IX86_BUILTIN_PAVGUSB:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_uavgv8qi3, arglist, target);

    case IX86_BUILTIN_PF2ID:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_pf2id, arglist, target, 0);

    case IX86_BUILTIN_PFACC:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_haddv2sf3, arglist, target);

    case IX86_BUILTIN_PFADD:
     return ix86_expand_binop_builtin (CODE_FOR_mmx_addv2sf3, arglist, target);

    case IX86_BUILTIN_PFCMPEQ:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_eqv2sf3, arglist, target);

    case IX86_BUILTIN_PFCMPGE:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_gev2sf3, arglist, target);

    case IX86_BUILTIN_PFCMPGT:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_gtv2sf3, arglist, target);

    case IX86_BUILTIN_PFMAX:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_smaxv2sf3, arglist, target);

    case IX86_BUILTIN_PFMIN:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_sminv2sf3, arglist, target);

    case IX86_BUILTIN_PFMUL:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_mulv2sf3, arglist, target);

    case IX86_BUILTIN_PFRCP:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_rcpv2sf2, arglist, target, 0);

    case IX86_BUILTIN_PFRCPIT1:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_rcpit1v2sf3, arglist, target);

    case IX86_BUILTIN_PFRCPIT2:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_rcpit2v2sf3, arglist, target);

    case IX86_BUILTIN_PFRSQIT1:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_rsqit1v2sf3, arglist, target);

    case IX86_BUILTIN_PFRSQRT:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_rsqrtv2sf2, arglist, target, 0);

    case IX86_BUILTIN_PFSUB:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_subv2sf3, arglist, target);

    case IX86_BUILTIN_PFSUBR:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_subrv2sf3, arglist, target);

    case IX86_BUILTIN_PI2FD:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_floatv2si2, arglist, target, 0);

    case IX86_BUILTIN_PMULHRW:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_pmulhrwv4hi3, arglist, target);

    case IX86_BUILTIN_PF2IW:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_pf2iw, arglist, target, 0);

    case IX86_BUILTIN_PFNACC:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_hsubv2sf3, arglist, target);

    case IX86_BUILTIN_PFPNACC:
      return ix86_expand_binop_builtin (CODE_FOR_mmx_addsubv2sf3, arglist, target);

    case IX86_BUILTIN_PI2FW:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_pi2fw, arglist, target, 0);

    case IX86_BUILTIN_PSWAPDSI:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_pswapdv2si2, arglist, target, 0);

    case IX86_BUILTIN_PSWAPDSF:
      return ix86_expand_unop_builtin (CODE_FOR_mmx_pswapdv2sf2, arglist, target, 0);

    case IX86_BUILTIN_SQRTSD:
      return ix86_expand_unop1_builtin (CODE_FOR_sse2_vmsqrtv2df2, arglist, target);
    case IX86_BUILTIN_LOADUPD:
      return ix86_expand_unop_builtin (CODE_FOR_sse2_movupd, arglist, target, 1);
    case IX86_BUILTIN_STOREUPD:
      return ix86_expand_store_builtin (CODE_FOR_sse2_movupd, arglist);

    case IX86_BUILTIN_MFENCE:
	emit_insn (gen_sse2_mfence ());
	return 0;
    case IX86_BUILTIN_LFENCE:
	emit_insn (gen_sse2_lfence ());
	return 0;

    case IX86_BUILTIN_CLFLUSH:
	arg0 = TREE_VALUE (arglist);
	op0 = expand_normal (arg0);
	icode = CODE_FOR_sse2_clflush;
	if (! (*insn_data[icode].operand[0].predicate) (op0, Pmode))
	    op0 = copy_to_mode_reg (Pmode, op0);

	emit_insn (gen_sse2_clflush (op0));
	return 0;

    case IX86_BUILTIN_MOVNTPD:
      return ix86_expand_store_builtin (CODE_FOR_sse2_movntv2df, arglist);
    case IX86_BUILTIN_MOVNTDQ:
      return ix86_expand_store_builtin (CODE_FOR_sse2_movntv2di, arglist);
    case IX86_BUILTIN_MOVNTI:
      return ix86_expand_store_builtin (CODE_FOR_sse2_movntsi, arglist);

    case IX86_BUILTIN_LOADDQU:
      return ix86_expand_unop_builtin (CODE_FOR_sse2_movdqu, arglist, target, 1);
    case IX86_BUILTIN_STOREDQU:
      return ix86_expand_store_builtin (CODE_FOR_sse2_movdqu, arglist);

    case IX86_BUILTIN_MONITOR:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      arg2 = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (arglist)));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      op2 = expand_normal (arg2);
      if (!REG_P (op0))
	op0 = copy_to_mode_reg (Pmode, op0);
      if (!REG_P (op1))
	op1 = copy_to_mode_reg (SImode, op1);
      if (!REG_P (op2))
	op2 = copy_to_mode_reg (SImode, op2);
      if (!TARGET_64BIT)
	emit_insn (gen_sse3_monitor (op0, op1, op2));
      else
	emit_insn (gen_sse3_monitor64 (op0, op1, op2));
      return 0;

    case IX86_BUILTIN_MWAIT:
      arg0 = TREE_VALUE (arglist);
      arg1 = TREE_VALUE (TREE_CHAIN (arglist));
      op0 = expand_normal (arg0);
      op1 = expand_normal (arg1);
      if (!REG_P (op0))
	op0 = copy_to_mode_reg (SImode, op0);
      if (!REG_P (op1))
	op1 = copy_to_mode_reg (SImode, op1);
      emit_insn (gen_sse3_mwait (op0, op1));
      return 0;

    case IX86_BUILTIN_LDDQU:
      return ix86_expand_unop_builtin (CODE_FOR_sse3_lddqu, arglist,
				       target, 1);

    case IX86_BUILTIN_VEC_INIT_V2SI:
    case IX86_BUILTIN_VEC_INIT_V4HI:
    case IX86_BUILTIN_VEC_INIT_V8QI:
      return ix86_expand_vec_init_builtin (TREE_TYPE (exp), arglist, target);

    case IX86_BUILTIN_VEC_EXT_V2DF:
    case IX86_BUILTIN_VEC_EXT_V2DI:
    case IX86_BUILTIN_VEC_EXT_V4SF:
    case IX86_BUILTIN_VEC_EXT_V4SI:
    case IX86_BUILTIN_VEC_EXT_V8HI:
    case IX86_BUILTIN_VEC_EXT_V16QI:
    case IX86_BUILTIN_VEC_EXT_V2SI:
    case IX86_BUILTIN_VEC_EXT_V4HI:
      return ix86_expand_vec_ext_builtin (arglist, target);

    case IX86_BUILTIN_VEC_SET_V8HI:
    case IX86_BUILTIN_VEC_SET_V4HI:
      return ix86_expand_vec_set_builtin (arglist);

    default:
      break;
    }

  for (i = 0, d = bdesc_2arg; i < ARRAY_SIZE (bdesc_2arg); i++, d++)
    if (d->code == fcode)
      {
	/* Compares are treated specially.  */
	if (d->icode == CODE_FOR_sse_maskcmpv4sf3
	    || d->icode == CODE_FOR_sse_vmmaskcmpv4sf3
	    || d->icode == CODE_FOR_sse2_maskcmpv2df3
	    || d->icode == CODE_FOR_sse2_vmmaskcmpv2df3)
	  return ix86_expand_sse_compare (d, arglist, target);

	return ix86_expand_binop_builtin (d->icode, arglist, target);
      }

  for (i = 0, d = bdesc_1arg; i < ARRAY_SIZE (bdesc_1arg); i++, d++)
    if (d->code == fcode)
      return ix86_expand_unop_builtin (d->icode, arglist, target, 0);

  for (i = 0, d = bdesc_comi; i < ARRAY_SIZE (bdesc_comi); i++, d++)
    if (d->code == fcode)
      return ix86_expand_sse_comi (d, arglist, target);

  gcc_unreachable ();
}

/* Store OPERAND to the memory after reload is completed.  This means
   that we can't easily use assign_stack_local.  */
rtx
ix86_force_to_memory (enum machine_mode mode, rtx operand)
{
  rtx result;

  gcc_assert (reload_completed);
  if (TARGET_RED_ZONE)
    {
      result = gen_rtx_MEM (mode,
			    gen_rtx_PLUS (Pmode,
					  stack_pointer_rtx,
					  GEN_INT (-RED_ZONE_SIZE)));
      emit_move_insn (result, operand);
    }
  else if (!TARGET_RED_ZONE && TARGET_64BIT)
    {
      switch (mode)
	{
	case HImode:
	case SImode:
	  operand = gen_lowpart (DImode, operand);
	  /* FALLTHRU */
	case DImode:
	  emit_insn (
		      gen_rtx_SET (VOIDmode,
				   gen_rtx_MEM (DImode,
						gen_rtx_PRE_DEC (DImode,
							stack_pointer_rtx)),
				   operand));
	  break;
	default:
	  gcc_unreachable ();
	}
      result = gen_rtx_MEM (mode, stack_pointer_rtx);
    }
  else
    {
      switch (mode)
	{
	case DImode:
	  {
	    rtx operands[2];
	    split_di (&operand, 1, operands, operands + 1);
	    emit_insn (
			gen_rtx_SET (VOIDmode,
				     gen_rtx_MEM (SImode,
						  gen_rtx_PRE_DEC (Pmode,
							stack_pointer_rtx)),
				     operands[1]));
	    emit_insn (
			gen_rtx_SET (VOIDmode,
				     gen_rtx_MEM (SImode,
						  gen_rtx_PRE_DEC (Pmode,
							stack_pointer_rtx)),
				     operands[0]));
	  }
	  break;
	case HImode:
	  /* Store HImodes as SImodes.  */
	  operand = gen_lowpart (SImode, operand);
	  /* FALLTHRU */
	case SImode:
	  emit_insn (
		      gen_rtx_SET (VOIDmode,
				   gen_rtx_MEM (GET_MODE (operand),
						gen_rtx_PRE_DEC (SImode,
							stack_pointer_rtx)),
				   operand));
	  break;
	default:
	  gcc_unreachable ();
	}
      result = gen_rtx_MEM (mode, stack_pointer_rtx);
    }
  return result;
}

/* Free operand from the memory.  */
void
ix86_free_from_memory (enum machine_mode mode)
{
  if (!TARGET_RED_ZONE)
    {
      int size;

      if (mode == DImode || TARGET_64BIT)
	size = 8;
      else
	size = 4;
      /* Use LEA to deallocate stack space.  In peephole2 it will be converted
         to pop or add instruction if registers are available.  */
      emit_insn (gen_rtx_SET (VOIDmode, stack_pointer_rtx,
			      gen_rtx_PLUS (Pmode, stack_pointer_rtx,
					    GEN_INT (size))));
    }
}

/* Put float CONST_DOUBLE in the constant pool instead of fp regs.
   QImode must go into class Q_REGS.
   Narrow ALL_REGS to GENERAL_REGS.  This supports allowing movsf and
   movdf to do mem-to-mem moves through integer regs.  */
enum reg_class
ix86_preferred_reload_class (rtx x, enum reg_class class)
{
  enum machine_mode mode = GET_MODE (x);

  /* We're only allowed to return a subclass of CLASS.  Many of the
     following checks fail for NO_REGS, so eliminate that early.  */
  if (class == NO_REGS)
    return NO_REGS;

  /* All classes can load zeros.  */
  if (x == CONST0_RTX (mode))
    return class;

  /* Force constants into memory if we are loading a (nonzero) constant into
     an MMX or SSE register.  This is because there are no MMX/SSE instructions
     to load from a constant.  */
  if (CONSTANT_P (x)
      && (MAYBE_MMX_CLASS_P (class) || MAYBE_SSE_CLASS_P (class)))
    return NO_REGS;

  /* Prefer SSE regs only, if we can use them for math.  */
  if (TARGET_SSE_MATH && !TARGET_MIX_SSE_I387 && SSE_FLOAT_MODE_P (mode))
    return SSE_CLASS_P (class) ? class : NO_REGS;

  /* Floating-point constants need more complex checks.  */
  if (GET_CODE (x) == CONST_DOUBLE && GET_MODE (x) != VOIDmode)
    {
      /* General regs can load everything.  */
      if (reg_class_subset_p (class, GENERAL_REGS))
        return class;

      /* Floats can load 0 and 1 plus some others.  Note that we eliminated
	 zero above.  We only want to wind up preferring 80387 registers if
	 we plan on doing computation with them.  */
      if (TARGET_80387
	  && standard_80387_constant_p (x))
	{
	  /* Limit class to non-sse.  */
	  if (class == FLOAT_SSE_REGS)
	    return FLOAT_REGS;
	  if (class == FP_TOP_SSE_REGS)
	    return FP_TOP_REG;
	  if (class == FP_SECOND_SSE_REGS)
	    return FP_SECOND_REG;
	  if (class == FLOAT_INT_REGS || class == FLOAT_REGS)
	    return class;
	}

      return NO_REGS;
    }

  /* Generally when we see PLUS here, it's the function invariant
     (plus soft-fp const_int).  Which can only be computed into general
     regs.  */
  if (GET_CODE (x) == PLUS)
    return reg_class_subset_p (class, GENERAL_REGS) ? class : NO_REGS;

  /* QImode constants are easy to load, but non-constant QImode data
     must go into Q_REGS.  */
  if (GET_MODE (x) == QImode && !CONSTANT_P (x))
    {
      if (reg_class_subset_p (class, Q_REGS))
	return class;
      if (reg_class_subset_p (Q_REGS, class))
	return Q_REGS;
      return NO_REGS;
    }

  return class;
}

/* Discourage putting floating-point values in SSE registers unless
   SSE math is being used, and likewise for the 387 registers.  */
enum reg_class
ix86_preferred_output_reload_class (rtx x, enum reg_class class)
{
  enum machine_mode mode = GET_MODE (x);

  /* Restrict the output reload class to the register bank that we are doing
     math on.  If we would like not to return a subset of CLASS, reject this
     alternative: if reload cannot do this, it will still use its choice.  */
  mode = GET_MODE (x);
  if (TARGET_SSE_MATH && SSE_FLOAT_MODE_P (mode))
    return MAYBE_SSE_CLASS_P (class) ? SSE_REGS : NO_REGS;

  if (TARGET_80387 && SCALAR_FLOAT_MODE_P (mode))
    {
      if (class == FP_TOP_SSE_REGS)
	return FP_TOP_REG;
      else if (class == FP_SECOND_SSE_REGS)
	return FP_SECOND_REG;
      else
	return FLOAT_CLASS_P (class) ? class : NO_REGS;
    }

  return class;
}

/* If we are copying between general and FP registers, we need a memory
   location. The same is true for SSE and MMX registers.

   The macro can't work reliably when one of the CLASSES is class containing
   registers from multiple units (SSE, MMX, integer).  We avoid this by never
   combining those units in single alternative in the machine description.
   Ensure that this constraint holds to avoid unexpected surprises.

   When STRICT is false, we are being called from REGISTER_MOVE_COST, so do not
   enforce these sanity checks.  */

int
ix86_secondary_memory_needed (enum reg_class class1, enum reg_class class2,
			      enum machine_mode mode, int strict)
{
  if (MAYBE_FLOAT_CLASS_P (class1) != FLOAT_CLASS_P (class1)
      || MAYBE_FLOAT_CLASS_P (class2) != FLOAT_CLASS_P (class2)
      || MAYBE_SSE_CLASS_P (class1) != SSE_CLASS_P (class1)
      || MAYBE_SSE_CLASS_P (class2) != SSE_CLASS_P (class2)
      || MAYBE_MMX_CLASS_P (class1) != MMX_CLASS_P (class1)
      || MAYBE_MMX_CLASS_P (class2) != MMX_CLASS_P (class2))
    {
      gcc_assert (!strict);
      return true;
    }

  if (FLOAT_CLASS_P (class1) != FLOAT_CLASS_P (class2))
    return true;

  /* ??? This is a lie.  We do have moves between mmx/general, and for
     mmx/sse2.  But by saying we need secondary memory we discourage the
     register allocator from using the mmx registers unless needed.  */
  if (MMX_CLASS_P (class1) != MMX_CLASS_P (class2))
    return true;

  if (SSE_CLASS_P (class1) != SSE_CLASS_P (class2))
    {
      /* SSE1 doesn't have any direct moves from other classes.  */
      if (!TARGET_SSE2)
	return true;

      /* If the target says that inter-unit moves are more expensive
	 than moving through memory, then don't generate them.  */
      if (!TARGET_INTER_UNIT_MOVES && !optimize_size)
	return true;

      /* Between SSE and general, we have moves no larger than word size.  */
      if (GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	return true;

      /* ??? For the cost of one register reformat penalty, we could use
	 the same instructions to move SFmode and DFmode data, but the
	 relevant move patterns don't support those alternatives.  */
      if (mode == SFmode || mode == DFmode)
	return true;
    }

  return false;
}

/* Return true if the registers in CLASS cannot represent the change from
   modes FROM to TO.  */

bool
ix86_cannot_change_mode_class (enum machine_mode from, enum machine_mode to,
			       enum reg_class class)
{
  if (from == to)
    return false;

  /* x87 registers can't do subreg at all, as all values are reformatted
     to extended precision.  */
  if (MAYBE_FLOAT_CLASS_P (class))
    return true;

  if (MAYBE_SSE_CLASS_P (class) || MAYBE_MMX_CLASS_P (class))
    {
      /* Vector registers do not support QI or HImode loads.  If we don't
	 disallow a change to these modes, reload will assume it's ok to
	 drop the subreg from (subreg:SI (reg:HI 100) 0).  This affects
	 the vec_dupv4hi pattern.  */
      if (GET_MODE_SIZE (from) < 4)
	return true;

      /* Vector registers do not support subreg with nonzero offsets, which
	 are otherwise valid for integer registers.  Since we can't see
	 whether we have a nonzero offset from here, prohibit all
         nonparadoxical subregs changing size.  */
      if (GET_MODE_SIZE (to) < GET_MODE_SIZE (from))
	return true;
    }

  return false;
}

/* Return the cost of moving data from a register in class CLASS1 to
   one in class CLASS2.

   It is not required that the cost always equal 2 when FROM is the same as TO;
   on some machines it is expensive to move between registers if they are not
   general registers.  */

int
ix86_register_move_cost (enum machine_mode mode, enum reg_class class1,
			 enum reg_class class2)
{
  /* In case we require secondary memory, compute cost of the store followed
     by load.  In order to avoid bad register allocation choices, we need
     for this to be *at least* as high as the symmetric MEMORY_MOVE_COST.  */

  if (ix86_secondary_memory_needed (class1, class2, mode, 0))
    {
      int cost = 1;

      cost += MAX (MEMORY_MOVE_COST (mode, class1, 0),
		   MEMORY_MOVE_COST (mode, class1, 1));
      cost += MAX (MEMORY_MOVE_COST (mode, class2, 0),
		   MEMORY_MOVE_COST (mode, class2, 1));

      /* In case of copying from general_purpose_register we may emit multiple
         stores followed by single load causing memory size mismatch stall.
         Count this as arbitrarily high cost of 20.  */
      if (CLASS_MAX_NREGS (class1, mode) > CLASS_MAX_NREGS (class2, mode))
	cost += 20;

      /* In the case of FP/MMX moves, the registers actually overlap, and we
	 have to switch modes in order to treat them differently.  */
      if ((MMX_CLASS_P (class1) && MAYBE_FLOAT_CLASS_P (class2))
          || (MMX_CLASS_P (class2) && MAYBE_FLOAT_CLASS_P (class1)))
	cost += 20;

      return cost;
    }

  /* Moves between SSE/MMX and integer unit are expensive.  */
  if (MMX_CLASS_P (class1) != MMX_CLASS_P (class2)
      || SSE_CLASS_P (class1) != SSE_CLASS_P (class2))
    return ix86_cost->mmxsse_to_integer;
  if (MAYBE_FLOAT_CLASS_P (class1))
    return ix86_cost->fp_move;
  if (MAYBE_SSE_CLASS_P (class1))
    return ix86_cost->sse_move;
  if (MAYBE_MMX_CLASS_P (class1))
    return ix86_cost->mmx_move;
  return 2;
}

/* Return 1 if hard register REGNO can hold a value of machine-mode MODE.  */

bool
ix86_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  /* Flags and only flags can only hold CCmode values.  */
  if (CC_REGNO_P (regno))
    return GET_MODE_CLASS (mode) == MODE_CC;
  if (GET_MODE_CLASS (mode) == MODE_CC
      || GET_MODE_CLASS (mode) == MODE_RANDOM
      || GET_MODE_CLASS (mode) == MODE_PARTIAL_INT)
    return 0;
  if (FP_REGNO_P (regno))
    return VALID_FP_MODE_P (mode);
  if (SSE_REGNO_P (regno))
    {
      /* We implement the move patterns for all vector modes into and
	 out of SSE registers, even when no operation instructions
	 are available.  */
      return (VALID_SSE_REG_MODE (mode)
	      || VALID_SSE2_REG_MODE (mode)
	      || VALID_MMX_REG_MODE (mode)
	      || VALID_MMX_REG_MODE_3DNOW (mode));
    }
  if (MMX_REGNO_P (regno))
    {
      /* We implement the move patterns for 3DNOW modes even in MMX mode,
	 so if the register is available at all, then we can move data of
	 the given mode into or out of it.  */
      return (VALID_MMX_REG_MODE (mode)
	      || VALID_MMX_REG_MODE_3DNOW (mode));
    }

  if (mode == QImode)
    {
      /* Take care for QImode values - they can be in non-QI regs,
	 but then they do cause partial register stalls.  */
      if (regno < 4 || TARGET_64BIT)
	return 1;
      if (!TARGET_PARTIAL_REG_STALL)
	return 1;
      return reload_in_progress || reload_completed;
    }
  /* We handle both integer and floats in the general purpose registers.  */
  else if (VALID_INT_MODE_P (mode))
    return 1;
  else if (VALID_FP_MODE_P (mode))
    return 1;
  /* Lots of MMX code casts 8 byte vector modes to DImode.  If we then go
     on to use that value in smaller contexts, this can easily force a
     pseudo to be allocated to GENERAL_REGS.  Since this is no worse than
     supporting DImode, allow it.  */
  else if (VALID_MMX_REG_MODE_3DNOW (mode) || VALID_MMX_REG_MODE (mode))
    return 1;

  return 0;
}

/* A subroutine of ix86_modes_tieable_p.  Return true if MODE is a
   tieable integer mode.  */

static bool
ix86_tieable_integer_mode_p (enum machine_mode mode)
{
  switch (mode)
    {
    case HImode:
    case SImode:
      return true;

    case QImode:
      return TARGET_64BIT || !TARGET_PARTIAL_REG_STALL;

    case DImode:
      return TARGET_64BIT;

    default:
      return false;
    }
}

/* Return true if MODE1 is accessible in a register that can hold MODE2
   without copying.  That is, all register classes that can hold MODE2
   can also hold MODE1.  */

bool
ix86_modes_tieable_p (enum machine_mode mode1, enum machine_mode mode2)
{
  if (mode1 == mode2)
    return true;

  if (ix86_tieable_integer_mode_p (mode1)
      && ix86_tieable_integer_mode_p (mode2))
    return true;

  /* MODE2 being XFmode implies fp stack or general regs, which means we
     can tie any smaller floating point modes to it.  Note that we do not
     tie this with TFmode.  */
  if (mode2 == XFmode)
    return mode1 == SFmode || mode1 == DFmode;

  /* MODE2 being DFmode implies fp stack, general or sse regs, which means
     that we can tie it with SFmode.  */
  if (mode2 == DFmode)
    return mode1 == SFmode;

  /* If MODE2 is only appropriate for an SSE register, then tie with
     any other mode acceptable to SSE registers.  */
  if (GET_MODE_SIZE (mode2) >= 8
      && ix86_hard_regno_mode_ok (FIRST_SSE_REG, mode2))
    return ix86_hard_regno_mode_ok (FIRST_SSE_REG, mode1);

  /* If MODE2 is appropriate for an MMX (or SSE) register, then tie
     with any other mode acceptable to MMX registers.  */
  if (GET_MODE_SIZE (mode2) == 8
      && ix86_hard_regno_mode_ok (FIRST_MMX_REG, mode2))
    return ix86_hard_regno_mode_ok (FIRST_MMX_REG, mode1);

  return false;
}

/* Return the cost of moving data of mode M between a
   register and memory.  A value of 2 is the default; this cost is
   relative to those in `REGISTER_MOVE_COST'.

   If moving between registers and memory is more expensive than
   between two registers, you should define this macro to express the
   relative cost.

   Model also increased moving costs of QImode registers in non
   Q_REGS classes.
 */
int
ix86_memory_move_cost (enum machine_mode mode, enum reg_class class, int in)
{
  if (FLOAT_CLASS_P (class))
    {
      int index;
      switch (mode)
	{
	  case SFmode:
	    index = 0;
	    break;
	  case DFmode:
	    index = 1;
	    break;
	  case XFmode:
	    index = 2;
	    break;
	  default:
	    return 100;
	}
      return in ? ix86_cost->fp_load [index] : ix86_cost->fp_store [index];
    }
  if (SSE_CLASS_P (class))
    {
      int index;
      switch (GET_MODE_SIZE (mode))
	{
	  case 4:
	    index = 0;
	    break;
	  case 8:
	    index = 1;
	    break;
	  case 16:
	    index = 2;
	    break;
	  default:
	    return 100;
	}
      return in ? ix86_cost->sse_load [index] : ix86_cost->sse_store [index];
    }
  if (MMX_CLASS_P (class))
    {
      int index;
      switch (GET_MODE_SIZE (mode))
	{
	  case 4:
	    index = 0;
	    break;
	  case 8:
	    index = 1;
	    break;
	  default:
	    return 100;
	}
      return in ? ix86_cost->mmx_load [index] : ix86_cost->mmx_store [index];
    }
  switch (GET_MODE_SIZE (mode))
    {
      case 1:
	if (in)
	  return (Q_CLASS_P (class) ? ix86_cost->int_load[0]
		  : ix86_cost->movzbl_load);
	else
	  return (Q_CLASS_P (class) ? ix86_cost->int_store[0]
		  : ix86_cost->int_store[0] + 4);
	break;
      case 2:
	return in ? ix86_cost->int_load[1] : ix86_cost->int_store[1];
      default:
	/* Compute number of 32bit moves needed.  TFmode is moved as XFmode.  */
	if (mode == TFmode)
	  mode = XFmode;
	return ((in ? ix86_cost->int_load[2] : ix86_cost->int_store[2])
		* (((int) GET_MODE_SIZE (mode)
		    + UNITS_PER_WORD - 1) / UNITS_PER_WORD));
    }
}

/* Compute a (partial) cost for rtx X.  Return true if the complete
   cost has been computed, and false if subexpressions should be
   scanned.  In either case, *TOTAL contains the cost result.  */

static bool
ix86_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  enum machine_mode mode = GET_MODE (x);

  switch (code)
    {
    case CONST_INT:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      if (TARGET_64BIT && !x86_64_immediate_operand (x, VOIDmode))
	*total = 3;
      else if (TARGET_64BIT && !x86_64_zext_immediate_operand (x, VOIDmode))
	*total = 2;
      else if (flag_pic && SYMBOLIC_CONST (x)
	       && (!TARGET_64BIT
		   || (!GET_CODE (x) != LABEL_REF
		       && (GET_CODE (x) != SYMBOL_REF
		           || !SYMBOL_REF_LOCAL_P (x)))))
	*total = 1;
      else
	*total = 0;
      return true;

    case CONST_DOUBLE:
      if (mode == VOIDmode)
	*total = 0;
      else
	switch (standard_80387_constant_p (x))
	  {
	  case 1: /* 0.0 */
	    *total = 1;
	    break;
	  default: /* Other constants */
	    *total = 2;
	    break;
	  case 0:
	  case -1:
	    /* Start with (MEM (SYMBOL_REF)), since that's where
	       it'll probably end up.  Add a penalty for size.  */
	    *total = (COSTS_N_INSNS (1)
		      + (flag_pic != 0 && !TARGET_64BIT)
		      + (mode == SFmode ? 0 : mode == DFmode ? 1 : 2));
	    break;
	  }
      return true;

    case ZERO_EXTEND:
      /* The zero extensions is often completely free on x86_64, so make
	 it as cheap as possible.  */
      if (TARGET_64BIT && mode == DImode
	  && GET_MODE (XEXP (x, 0)) == SImode)
	*total = 1;
      else if (TARGET_ZERO_EXTEND_WITH_AND)
	*total = ix86_cost->add;
      else
	*total = ix86_cost->movzx;
      return false;

    case SIGN_EXTEND:
      *total = ix86_cost->movsx;
      return false;

    case ASHIFT:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && (GET_MODE (XEXP (x, 0)) != DImode || TARGET_64BIT))
	{
	  HOST_WIDE_INT value = INTVAL (XEXP (x, 1));
	  if (value == 1)
	    {
	      *total = ix86_cost->add;
	      return false;
	    }
	  if ((value == 2 || value == 3)
	      && ix86_cost->lea <= ix86_cost->shift_const)
	    {
	      *total = ix86_cost->lea;
	      return false;
	    }
	}
      /* FALLTHRU */

    case ROTATE:
    case ASHIFTRT:
    case LSHIFTRT:
    case ROTATERT:
      if (!TARGET_64BIT && GET_MODE (XEXP (x, 0)) == DImode)
	{
	  if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    {
	      if (INTVAL (XEXP (x, 1)) > 32)
		*total = ix86_cost->shift_const + COSTS_N_INSNS (2);
	      else
		*total = ix86_cost->shift_const * 2;
	    }
	  else
	    {
	      if (GET_CODE (XEXP (x, 1)) == AND)
		*total = ix86_cost->shift_var * 2;
	      else
		*total = ix86_cost->shift_var * 6 + COSTS_N_INSNS (2);
	    }
	}
      else
	{
	  if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    *total = ix86_cost->shift_const;
	  else
	    *total = ix86_cost->shift_var;
	}
      return false;

    case MULT:
      if (FLOAT_MODE_P (mode))
	{
	  *total = ix86_cost->fmul;
	  return false;
	}
      else
	{
	  rtx op0 = XEXP (x, 0);
	  rtx op1 = XEXP (x, 1);
	  int nbits;
	  if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    {
	      unsigned HOST_WIDE_INT value = INTVAL (XEXP (x, 1));
	      for (nbits = 0; value != 0; value &= value - 1)
	        nbits++;
	    }
	  else
	    /* This is arbitrary.  */
	    nbits = 7;

	  /* Compute costs correctly for widening multiplication.  */
	  if ((GET_CODE (op0) == SIGN_EXTEND || GET_CODE (op1) == ZERO_EXTEND)
	      && GET_MODE_SIZE (GET_MODE (XEXP (op0, 0))) * 2
	         == GET_MODE_SIZE (mode))
	    {
	      int is_mulwiden = 0;
	      enum machine_mode inner_mode = GET_MODE (op0);

	      if (GET_CODE (op0) == GET_CODE (op1))
		is_mulwiden = 1, op1 = XEXP (op1, 0);
	      else if (GET_CODE (op1) == CONST_INT)
		{
		  if (GET_CODE (op0) == SIGN_EXTEND)
		    is_mulwiden = trunc_int_for_mode (INTVAL (op1), inner_mode)
			          == INTVAL (op1);
		  else
		    is_mulwiden = !(INTVAL (op1) & ~GET_MODE_MASK (inner_mode));
	        }

	      if (is_mulwiden)
	        op0 = XEXP (op0, 0), mode = GET_MODE (op0);
	    }

  	  *total = (ix86_cost->mult_init[MODE_INDEX (mode)]
		    + nbits * ix86_cost->mult_bit
	            + rtx_cost (op0, outer_code) + rtx_cost (op1, outer_code));

          return true;
	}

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      if (FLOAT_MODE_P (mode))
	*total = ix86_cost->fdiv;
      else
	*total = ix86_cost->divide[MODE_INDEX (mode)];
      return false;

    case PLUS:
      if (FLOAT_MODE_P (mode))
	*total = ix86_cost->fadd;
      else if (GET_MODE_CLASS (mode) == MODE_INT
	       && GET_MODE_BITSIZE (mode) <= GET_MODE_BITSIZE (Pmode))
	{
	  if (GET_CODE (XEXP (x, 0)) == PLUS
	      && GET_CODE (XEXP (XEXP (x, 0), 0)) == MULT
	      && GET_CODE (XEXP (XEXP (XEXP (x, 0), 0), 1)) == CONST_INT
	      && CONSTANT_P (XEXP (x, 1)))
	    {
	      HOST_WIDE_INT val = INTVAL (XEXP (XEXP (XEXP (x, 0), 0), 1));
	      if (val == 2 || val == 4 || val == 8)
		{
		  *total = ix86_cost->lea;
		  *total += rtx_cost (XEXP (XEXP (x, 0), 1), outer_code);
		  *total += rtx_cost (XEXP (XEXP (XEXP (x, 0), 0), 0),
				      outer_code);
		  *total += rtx_cost (XEXP (x, 1), outer_code);
		  return true;
		}
	    }
	  else if (GET_CODE (XEXP (x, 0)) == MULT
		   && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT)
	    {
	      HOST_WIDE_INT val = INTVAL (XEXP (XEXP (x, 0), 1));
	      if (val == 2 || val == 4 || val == 8)
		{
		  *total = ix86_cost->lea;
		  *total += rtx_cost (XEXP (XEXP (x, 0), 0), outer_code);
		  *total += rtx_cost (XEXP (x, 1), outer_code);
		  return true;
		}
	    }
	  else if (GET_CODE (XEXP (x, 0)) == PLUS)
	    {
	      *total = ix86_cost->lea;
	      *total += rtx_cost (XEXP (XEXP (x, 0), 0), outer_code);
	      *total += rtx_cost (XEXP (XEXP (x, 0), 1), outer_code);
	      *total += rtx_cost (XEXP (x, 1), outer_code);
	      return true;
	    }
	}
      /* FALLTHRU */

    case MINUS:
      if (FLOAT_MODE_P (mode))
	{
	  *total = ix86_cost->fadd;
	  return false;
	}
      /* FALLTHRU */

    case AND:
    case IOR:
    case XOR:
      if (!TARGET_64BIT && mode == DImode)
	{
	  *total = (ix86_cost->add * 2
		    + (rtx_cost (XEXP (x, 0), outer_code)
		       << (GET_MODE (XEXP (x, 0)) != DImode))
		    + (rtx_cost (XEXP (x, 1), outer_code)
	               << (GET_MODE (XEXP (x, 1)) != DImode)));
	  return true;
	}
      /* FALLTHRU */

    case NEG:
      if (FLOAT_MODE_P (mode))
	{
	  *total = ix86_cost->fchs;
	  return false;
	}
      /* FALLTHRU */

    case NOT:
      if (!TARGET_64BIT && mode == DImode)
	*total = ix86_cost->add * 2;
      else
	*total = ix86_cost->add;
      return false;

    case COMPARE:
      if (GET_CODE (XEXP (x, 0)) == ZERO_EXTRACT
	  && XEXP (XEXP (x, 0), 1) == const1_rtx
	  && GET_CODE (XEXP (XEXP (x, 0), 2)) == CONST_INT
	  && XEXP (x, 1) == const0_rtx)
	{
	  /* This kind of construct is implemented using test[bwl].
	     Treat it as if we had an AND.  */
	  *total = (ix86_cost->add
		    + rtx_cost (XEXP (XEXP (x, 0), 0), outer_code)
		    + rtx_cost (const1_rtx, outer_code));
	  return true;
	}
      return false;

    case FLOAT_EXTEND:
      if (!TARGET_SSE_MATH
	  || mode == XFmode
	  || (mode == DFmode && !TARGET_SSE2))
	/* For standard 80387 constants, raise the cost to prevent
	   compress_float_constant() to generate load from memory.  */
	switch (standard_80387_constant_p (XEXP (x, 0)))
	  {
	  case -1:
	  case 0:
	    *total = 0;
	    break;
	  case 1: /* 0.0 */
	    *total = 1;
	    break;
	  default:
	    *total = (x86_ext_80387_constants & TUNEMASK
		      || optimize_size
		      ? 1 : 0);
	  }
      return false;

    case ABS:
      if (FLOAT_MODE_P (mode))
	*total = ix86_cost->fabs;
      return false;

    case SQRT:
      if (FLOAT_MODE_P (mode))
	*total = ix86_cost->fsqrt;
      return false;

    case UNSPEC:
      if (XINT (x, 1) == UNSPEC_TP)
	*total = 0;
      return false;

    default:
      return false;
    }
}

#if TARGET_MACHO

static int current_machopic_label_num;

/* Given a symbol name and its associated stub, write out the
   definition of the stub.  */

void
machopic_output_stub (FILE *file, const char *symb, const char *stub)
{
  unsigned int length;
  char *binder_name, *symbol_name, lazy_ptr_name[32];
  int label = ++current_machopic_label_num;

  /* For 64-bit we shouldn't get here.  */
  gcc_assert (!TARGET_64BIT);

  /* Lose our funky encoding stuff so it doesn't contaminate the stub.  */
  symb = (*targetm.strip_name_encoding) (symb);

  length = strlen (stub);
  binder_name = alloca (length + 32);
  GEN_BINDER_NAME_FOR_STUB (binder_name, stub, length);

  length = strlen (symb);
  symbol_name = alloca (length + 32);
  GEN_SYMBOL_NAME_FOR_SYMBOL (symbol_name, symb, length);

  sprintf (lazy_ptr_name, "L%d$lz", label);

  if (MACHOPIC_PURE)
    switch_to_section (darwin_sections[machopic_picsymbol_stub_section]);
  else
    switch_to_section (darwin_sections[machopic_symbol_stub_section]);

  fprintf (file, "%s:\n", stub);
  fprintf (file, "\t.indirect_symbol %s\n", symbol_name);

  if (MACHOPIC_PURE)
    {
      fprintf (file, "\tcall\tLPC$%d\nLPC$%d:\tpopl\t%%eax\n", label, label);
      fprintf (file, "\tmovl\t%s-LPC$%d(%%eax),%%edx\n", lazy_ptr_name, label);
      fprintf (file, "\tjmp\t*%%edx\n");
    }
  else
    fprintf (file, "\tjmp\t*%s\n", lazy_ptr_name);

  fprintf (file, "%s:\n", binder_name);

  if (MACHOPIC_PURE)
    {
      fprintf (file, "\tlea\t%s-LPC$%d(%%eax),%%eax\n", lazy_ptr_name, label);
      fprintf (file, "\tpushl\t%%eax\n");
    }
  else
    fprintf (file, "\tpushl\t$%s\n", lazy_ptr_name);

  fprintf (file, "\tjmp\tdyld_stub_binding_helper\n");

  switch_to_section (darwin_sections[machopic_lazy_symbol_ptr_section]);
  fprintf (file, "%s:\n", lazy_ptr_name);
  fprintf (file, "\t.indirect_symbol %s\n", symbol_name);
  fprintf (file, "\t.long %s\n", binder_name);
}

void
darwin_x86_file_end (void)
{
  darwin_file_end ();
  ix86_file_end ();
}
#endif /* TARGET_MACHO */

/* Order the registers for register allocator.  */

void
x86_order_regs_for_local_alloc (void)
{
   int pos = 0;
   int i;

   /* First allocate the local general purpose registers.  */
   for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
     if (GENERAL_REGNO_P (i) && call_used_regs[i])
	reg_alloc_order [pos++] = i;

   /* Global general purpose registers.  */
   for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
     if (GENERAL_REGNO_P (i) && !call_used_regs[i])
	reg_alloc_order [pos++] = i;

   /* x87 registers come first in case we are doing FP math
      using them.  */
   if (!TARGET_SSE_MATH)
     for (i = FIRST_STACK_REG; i <= LAST_STACK_REG; i++)
       reg_alloc_order [pos++] = i;

   /* SSE registers.  */
   for (i = FIRST_SSE_REG; i <= LAST_SSE_REG; i++)
     reg_alloc_order [pos++] = i;
   for (i = FIRST_REX_SSE_REG; i <= LAST_REX_SSE_REG; i++)
     reg_alloc_order [pos++] = i;

   /* x87 registers.  */
   if (TARGET_SSE_MATH)
     for (i = FIRST_STACK_REG; i <= LAST_STACK_REG; i++)
       reg_alloc_order [pos++] = i;

   for (i = FIRST_MMX_REG; i <= LAST_MMX_REG; i++)
     reg_alloc_order [pos++] = i;

   /* Initialize the rest of array as we do not allocate some registers
      at all.  */
   while (pos < FIRST_PSEUDO_REGISTER)
     reg_alloc_order [pos++] = 0;
}

/* Handle a "ms_struct" or "gcc_struct" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
ix86_handle_struct_attribute (tree *node, tree name,
			      tree args ATTRIBUTE_UNUSED,
			      int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  tree *type = NULL;
  if (DECL_P (*node))
    {
      if (TREE_CODE (*node) == TYPE_DECL)
	type = &TREE_TYPE (*node);
    }
  else
    type = node;

  if (!(type && (TREE_CODE (*type) == RECORD_TYPE
		 || TREE_CODE (*type) == UNION_TYPE)))
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  else if ((is_attribute_p ("ms_struct", name)
	    && lookup_attribute ("gcc_struct", TYPE_ATTRIBUTES (*type)))
	   || ((is_attribute_p ("gcc_struct", name)
		&& lookup_attribute ("ms_struct", TYPE_ATTRIBUTES (*type)))))
    {
      warning (OPT_Wattributes, "%qs incompatible attribute ignored",
               IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

static bool
ix86_ms_bitfield_layout_p (tree record_type)
{
  return (TARGET_MS_BITFIELD_LAYOUT &&
	  !lookup_attribute ("gcc_struct", TYPE_ATTRIBUTES (record_type)))
    || lookup_attribute ("ms_struct", TYPE_ATTRIBUTES (record_type));
}

/* Returns an expression indicating where the this parameter is
   located on entry to the FUNCTION.  */

static rtx
x86_this_parameter (tree function)
{
  tree type = TREE_TYPE (function);

  if (TARGET_64BIT)
    {
      int n = aggregate_value_p (TREE_TYPE (type), type) != 0;
      return gen_rtx_REG (DImode, x86_64_int_parameter_registers[n]);
    }

  if (ix86_function_regparm (type, function) > 0)
    {
      tree parm;

      parm = TYPE_ARG_TYPES (type);
      /* Figure out whether or not the function has a variable number of
	 arguments.  */
      for (; parm; parm = TREE_CHAIN (parm))
	if (TREE_VALUE (parm) == void_type_node)
	  break;
      /* If not, the this parameter is in the first argument.  */
      if (parm)
	{
	  int regno = 0;
	  if (lookup_attribute ("fastcall", TYPE_ATTRIBUTES (type)))
	    regno = 2;
	  return gen_rtx_REG (SImode, regno);
	}
    }

  if (aggregate_value_p (TREE_TYPE (type), type))
    return gen_rtx_MEM (SImode, plus_constant (stack_pointer_rtx, 8));
  else
    return gen_rtx_MEM (SImode, plus_constant (stack_pointer_rtx, 4));
}

/* Determine whether x86_output_mi_thunk can succeed.  */

static bool
x86_can_output_mi_thunk (tree thunk ATTRIBUTE_UNUSED,
			 HOST_WIDE_INT delta ATTRIBUTE_UNUSED,
			 HOST_WIDE_INT vcall_offset, tree function)
{
  /* 64-bit can handle anything.  */
  if (TARGET_64BIT)
    return true;

  /* For 32-bit, everything's fine if we have one free register.  */
  if (ix86_function_regparm (TREE_TYPE (function), function) < 3)
    return true;

  /* Need a free register for vcall_offset.  */
  if (vcall_offset)
    return false;

  /* Need a free register for GOT references.  */
  if (flag_pic && !(*targetm.binds_local_p) (function))
    return false;

  /* Otherwise ok.  */
  return true;
}

/* Output the assembler code for a thunk function.  THUNK_DECL is the
   declaration for the thunk function itself, FUNCTION is the decl for
   the target function.  DELTA is an immediate constant offset to be
   added to THIS.  If VCALL_OFFSET is nonzero, the word at
   *(*this + vcall_offset) should be added to THIS.  */

static void
x86_output_mi_thunk (FILE *file ATTRIBUTE_UNUSED,
		     tree thunk ATTRIBUTE_UNUSED, HOST_WIDE_INT delta,
		     HOST_WIDE_INT vcall_offset, tree function)
{
  rtx xops[3];
  rtx this = x86_this_parameter (function);
  rtx this_reg, tmp;

  /* If VCALL_OFFSET, we'll need THIS in a register.  Might as well
     pull it in now and let DELTA benefit.  */
  if (REG_P (this))
    this_reg = this;
  else if (vcall_offset)
    {
      /* Put the this parameter into %eax.  */
      xops[0] = this;
      xops[1] = this_reg = gen_rtx_REG (Pmode, 0);
      output_asm_insn ("mov{l}\t{%0, %1|%1, %0}", xops);
    }
  else
    this_reg = NULL_RTX;

  /* Adjust the this parameter by a fixed constant.  */
  if (delta)
    {
      xops[0] = GEN_INT (delta);
      xops[1] = this_reg ? this_reg : this;
      if (TARGET_64BIT)
	{
	  if (!x86_64_general_operand (xops[0], DImode))
	    {
	      tmp = gen_rtx_REG (DImode, FIRST_REX_INT_REG + 2 /* R10 */);
	      xops[1] = tmp;
	      output_asm_insn ("mov{q}\t{%1, %0|%0, %1}", xops);
	      xops[0] = tmp;
	      xops[1] = this;
	    }
	  output_asm_insn ("add{q}\t{%0, %1|%1, %0}", xops);
	}
      else
	output_asm_insn ("add{l}\t{%0, %1|%1, %0}", xops);
    }

  /* Adjust the this parameter by a value stored in the vtable.  */
  if (vcall_offset)
    {
      if (TARGET_64BIT)
	tmp = gen_rtx_REG (DImode, FIRST_REX_INT_REG + 2 /* R10 */);
      else
	{
	  int tmp_regno = 2 /* ECX */;
	  if (lookup_attribute ("fastcall",
	      TYPE_ATTRIBUTES (TREE_TYPE (function))))
	    tmp_regno = 0 /* EAX */;
	  tmp = gen_rtx_REG (SImode, tmp_regno);
	}

      xops[0] = gen_rtx_MEM (Pmode, this_reg);
      xops[1] = tmp;
      if (TARGET_64BIT)
	output_asm_insn ("mov{q}\t{%0, %1|%1, %0}", xops);
      else
	output_asm_insn ("mov{l}\t{%0, %1|%1, %0}", xops);

      /* Adjust the this parameter.  */
      xops[0] = gen_rtx_MEM (Pmode, plus_constant (tmp, vcall_offset));
      if (TARGET_64BIT && !memory_operand (xops[0], Pmode))
	{
	  rtx tmp2 = gen_rtx_REG (DImode, FIRST_REX_INT_REG + 3 /* R11 */);
	  xops[0] = GEN_INT (vcall_offset);
	  xops[1] = tmp2;
	  output_asm_insn ("mov{q}\t{%0, %1|%1, %0}", xops);
	  xops[0] = gen_rtx_MEM (Pmode, gen_rtx_PLUS (Pmode, tmp, tmp2));
	}
      xops[1] = this_reg;
      if (TARGET_64BIT)
	output_asm_insn ("add{q}\t{%0, %1|%1, %0}", xops);
      else
	output_asm_insn ("add{l}\t{%0, %1|%1, %0}", xops);
    }

  /* If necessary, drop THIS back to its stack slot.  */
  if (this_reg && this_reg != this)
    {
      xops[0] = this_reg;
      xops[1] = this;
      output_asm_insn ("mov{l}\t{%0, %1|%1, %0}", xops);
    }

  xops[0] = XEXP (DECL_RTL (function), 0);
  if (TARGET_64BIT)
    {
      if (!flag_pic || (*targetm.binds_local_p) (function))
	output_asm_insn ("jmp\t%P0", xops);
      else
	{
	  tmp = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, xops[0]), UNSPEC_GOTPCREL);
	  tmp = gen_rtx_CONST (Pmode, tmp);
	  tmp = gen_rtx_MEM (QImode, tmp);
	  xops[0] = tmp;
	  output_asm_insn ("jmp\t%A0", xops);
	}
    }
  else
    {
      if (!flag_pic || (*targetm.binds_local_p) (function))
	output_asm_insn ("jmp\t%P0", xops);
      else
#if TARGET_MACHO
	if (TARGET_MACHO)
	  {
	    rtx sym_ref = XEXP (DECL_RTL (function), 0);
	    tmp = (gen_rtx_SYMBOL_REF
		   (Pmode,
		    machopic_indirection_name (sym_ref, /*stub_p=*/true)));
	    tmp = gen_rtx_MEM (QImode, tmp);
	    xops[0] = tmp;
	    output_asm_insn ("jmp\t%0", xops);
	  }
	else
#endif /* TARGET_MACHO */
	{
	  tmp = gen_rtx_REG (SImode, 2 /* ECX */);
	  output_set_got (tmp, NULL_RTX);

	  xops[1] = tmp;
	  output_asm_insn ("mov{l}\t{%0@GOT(%1), %1|%1, %0@GOT[%1]}", xops);
	  output_asm_insn ("jmp\t{*}%1", xops);
	}
    }
}

static void
x86_file_start (void)
{
  default_file_start ();
#if TARGET_MACHO
  darwin_file_start ();
#endif
  if (X86_FILE_START_VERSION_DIRECTIVE)
    fputs ("\t.version\t\"01.01\"\n", asm_out_file);
  if (X86_FILE_START_FLTUSED)
    fputs ("\t.global\t__fltused\n", asm_out_file);
  if (ix86_asm_dialect == ASM_INTEL)
    fputs ("\t.intel_syntax\n", asm_out_file);
}

int
x86_field_alignment (tree field, int computed)
{
  enum machine_mode mode;
  tree type = TREE_TYPE (field);

  if (TARGET_64BIT || TARGET_ALIGN_DOUBLE)
    return computed;
  mode = TYPE_MODE (TREE_CODE (type) == ARRAY_TYPE
		    ? get_inner_array_type (type) : type);
  if (mode == DFmode || mode == DCmode
      || GET_MODE_CLASS (mode) == MODE_INT
      || GET_MODE_CLASS (mode) == MODE_COMPLEX_INT)
    return MIN (32, computed);
  return computed;
}

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */
void
x86_function_profiler (FILE *file, int labelno ATTRIBUTE_UNUSED)
{
  if (TARGET_64BIT)
    if (flag_pic)
      {
#ifndef NO_PROFILE_COUNTERS
	fprintf (file, "\tleaq\t%sP%d@(%%rip),%%r11\n", LPREFIX, labelno);
#endif
	fprintf (file, "\tcall\t*%s@GOTPCREL(%%rip)\n", MCOUNT_NAME);
      }
    else
      {
#ifndef NO_PROFILE_COUNTERS
	fprintf (file, "\tmovq\t$%sP%d,%%r11\n", LPREFIX, labelno);
#endif
	fprintf (file, "\tcall\t%s\n", MCOUNT_NAME);
      }
  else if (flag_pic)
    {
#ifndef NO_PROFILE_COUNTERS
      fprintf (file, "\tleal\t%sP%d@GOTOFF(%%ebx),%%%s\n",
	       LPREFIX, labelno, PROFILE_COUNT_REGISTER);
#endif
      fprintf (file, "\tcall\t*%s@GOT(%%ebx)\n", MCOUNT_NAME);
    }
  else
    {
#ifndef NO_PROFILE_COUNTERS
      fprintf (file, "\tmovl\t$%sP%d,%%%s\n", LPREFIX, labelno,
	       PROFILE_COUNT_REGISTER);
#endif
      fprintf (file, "\tcall\t%s\n", MCOUNT_NAME);
    }
}

/* We don't have exact information about the insn sizes, but we may assume
   quite safely that we are informed about all 1 byte insns and memory
   address sizes.  This is enough to eliminate unnecessary padding in
   99% of cases.  */

static int
min_insn_size (rtx insn)
{
  int l = 0;

  if (!INSN_P (insn) || !active_insn_p (insn))
    return 0;

  /* Discard alignments we've emit and jump instructions.  */
  if (GET_CODE (PATTERN (insn)) == UNSPEC_VOLATILE
      && XINT (PATTERN (insn), 1) == UNSPECV_ALIGN)
    return 0;
  if (GET_CODE (insn) == JUMP_INSN
      && (GET_CODE (PATTERN (insn)) == ADDR_VEC
	  || GET_CODE (PATTERN (insn)) == ADDR_DIFF_VEC))
    return 0;

  /* Important case - calls are always 5 bytes.
     It is common to have many calls in the row.  */
  if (GET_CODE (insn) == CALL_INSN
      && symbolic_reference_mentioned_p (PATTERN (insn))
      && !SIBLING_CALL_P (insn))
    return 5;
  if (get_attr_length (insn) <= 1)
    return 1;

  /* For normal instructions we may rely on the sizes of addresses
     and the presence of symbol to require 4 bytes of encoding.
     This is not the case for jumps where references are PC relative.  */
  if (GET_CODE (insn) != JUMP_INSN)
    {
      l = get_attr_length_address (insn);
      if (l < 4 && symbolic_reference_mentioned_p (PATTERN (insn)))
	l = 4;
    }
  if (l)
    return 1+l;
  else
    return 2;
}

/* AMD K8 core mispredicts jumps when there are more than 3 jumps in 16 byte
   window.  */

static void
ix86_avoid_jump_misspredicts (void)
{
  rtx insn, start = get_insns ();
  int nbytes = 0, njumps = 0;
  int isjump = 0;

  /* Look for all minimal intervals of instructions containing 4 jumps.
     The intervals are bounded by START and INSN.  NBYTES is the total
     size of instructions in the interval including INSN and not including
     START.  When the NBYTES is smaller than 16 bytes, it is possible
     that the end of START and INSN ends up in the same 16byte page.

     The smallest offset in the page INSN can start is the case where START
     ends on the offset 0.  Offset of INSN is then NBYTES - sizeof (INSN).
     We add p2align to 16byte window with maxskip 17 - NBYTES + sizeof (INSN).
     */
  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    {

      nbytes += min_insn_size (insn);
      if (dump_file)
        fprintf(dump_file, "Insn %i estimated to %i bytes\n",
		INSN_UID (insn), min_insn_size (insn));
      if ((GET_CODE (insn) == JUMP_INSN
	   && GET_CODE (PATTERN (insn)) != ADDR_VEC
	   && GET_CODE (PATTERN (insn)) != ADDR_DIFF_VEC)
	  || GET_CODE (insn) == CALL_INSN)
	njumps++;
      else
	continue;

      while (njumps > 3)
	{
	  start = NEXT_INSN (start);
	  if ((GET_CODE (start) == JUMP_INSN
	       && GET_CODE (PATTERN (start)) != ADDR_VEC
	       && GET_CODE (PATTERN (start)) != ADDR_DIFF_VEC)
	      || GET_CODE (start) == CALL_INSN)
	    njumps--, isjump = 1;
	  else
	    isjump = 0;
	  nbytes -= min_insn_size (start);
	}
      gcc_assert (njumps >= 0);
      if (dump_file)
        fprintf (dump_file, "Interval %i to %i has %i bytes\n",
		INSN_UID (start), INSN_UID (insn), nbytes);

      if (njumps == 3 && isjump && nbytes < 16)
	{
	  int padsize = 15 - nbytes + min_insn_size (insn);

	  if (dump_file)
	    fprintf (dump_file, "Padding insn %i by %i bytes!\n",
		     INSN_UID (insn), padsize);
          emit_insn_before (gen_align (GEN_INT (padsize)), insn);
	}
    }
}

/* AMD Athlon works faster
   when RET is not destination of conditional jump or directly preceded
   by other jump instruction.  We avoid the penalty by inserting NOP just
   before the RET instructions in such cases.  */
static void
ix86_pad_returns (void)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    {
      basic_block bb = e->src;
      rtx ret = BB_END (bb);
      rtx prev;
      bool replace = false;

      if (GET_CODE (ret) != JUMP_INSN || GET_CODE (PATTERN (ret)) != RETURN
	  || !maybe_hot_bb_p (bb))
	continue;
      for (prev = PREV_INSN (ret); prev; prev = PREV_INSN (prev))
	if (active_insn_p (prev) || GET_CODE (prev) == CODE_LABEL)
	  break;
      if (prev && GET_CODE (prev) == CODE_LABEL)
	{
	  edge e;
	  edge_iterator ei;

	  FOR_EACH_EDGE (e, ei, bb->preds)
	    if (EDGE_FREQUENCY (e) && e->src->index >= 0
		&& !(e->flags & EDGE_FALLTHRU))
	      replace = true;
	}
      if (!replace)
	{
	  prev = prev_active_insn (ret);
	  if (prev
	      && ((GET_CODE (prev) == JUMP_INSN && any_condjump_p (prev))
		  || GET_CODE (prev) == CALL_INSN))
	    replace = true;
	  /* Empty functions get branch mispredict even when the jump destination
	     is not visible to us.  */
	  if (!prev && cfun->function_frequency > FUNCTION_FREQUENCY_UNLIKELY_EXECUTED)
	    replace = true;
	}
      if (replace)
	{
	  emit_insn_before (gen_return_internal_long (), ret);
	  delete_insn (ret);
	}
    }
}

/* Implement machine specific optimizations.  We implement padding of returns
   for K8 CPUs and pass to avoid 4 jumps in the single 16 byte window.  */
static void
ix86_reorg (void)
{
  if (TARGET_PAD_RETURNS && optimize && !optimize_size)
    ix86_pad_returns ();
  if (TARGET_FOUR_JUMP_LIMIT && optimize && !optimize_size)
    ix86_avoid_jump_misspredicts ();
}

/* Return nonzero when QImode register that must be represented via REX prefix
   is used.  */
bool
x86_extended_QIreg_mentioned_p (rtx insn)
{
  int i;
  extract_insn_cached (insn);
  for (i = 0; i < recog_data.n_operands; i++)
    if (REG_P (recog_data.operand[i])
	&& REGNO (recog_data.operand[i]) >= 4)
       return true;
  return false;
}

/* Return nonzero when P points to register encoded via REX prefix.
   Called via for_each_rtx.  */
static int
extended_reg_mentioned_1 (rtx *p, void *data ATTRIBUTE_UNUSED)
{
   unsigned int regno;
   if (!REG_P (*p))
     return 0;
   regno = REGNO (*p);
   return REX_INT_REGNO_P (regno) || REX_SSE_REGNO_P (regno);
}

/* Return true when INSN mentions register that must be encoded using REX
   prefix.  */
bool
x86_extended_reg_mentioned_p (rtx insn)
{
  return for_each_rtx (&PATTERN (insn), extended_reg_mentioned_1, NULL);
}

/* Generate an unsigned DImode/SImode to FP conversion.  This is the same code
   optabs would emit if we didn't have TFmode patterns.  */

void
x86_emit_floatuns (rtx operands[2])
{
  rtx neglab, donelab, i0, i1, f0, in, out;
  enum machine_mode mode, inmode;

  inmode = GET_MODE (operands[1]);
  gcc_assert (inmode == SImode || inmode == DImode);

  out = operands[0];
  in = force_reg (inmode, operands[1]);
  mode = GET_MODE (out);
  neglab = gen_label_rtx ();
  donelab = gen_label_rtx ();
  i1 = gen_reg_rtx (Pmode);
  f0 = gen_reg_rtx (mode);

  emit_cmp_and_jump_insns (in, const0_rtx, LT, const0_rtx, Pmode, 0, neglab);

  emit_insn (gen_rtx_SET (VOIDmode, out, gen_rtx_FLOAT (mode, in)));
  emit_jump_insn (gen_jump (donelab));
  emit_barrier ();

  emit_label (neglab);

  i0 = expand_simple_binop (Pmode, LSHIFTRT, in, const1_rtx, NULL, 1, OPTAB_DIRECT);
  i1 = expand_simple_binop (Pmode, AND, in, const1_rtx, NULL, 1, OPTAB_DIRECT);
  i0 = expand_simple_binop (Pmode, IOR, i0, i1, i0, 1, OPTAB_DIRECT);
  expand_float (f0, i0, 0);
  emit_insn (gen_rtx_SET (VOIDmode, out, gen_rtx_PLUS (mode, f0, f0)));

  emit_label (donelab);
}

/* A subroutine of ix86_expand_vector_init.  Store into TARGET a vector
   with all elements equal to VAR.  Return true if successful.  */

static bool
ix86_expand_vector_init_duplicate (bool mmx_ok, enum machine_mode mode,
				   rtx target, rtx val)
{
  enum machine_mode smode, wsmode, wvmode;
  rtx x;

  switch (mode)
    {
    case V2SImode:
    case V2SFmode:
      if (!mmx_ok)
	return false;
      /* FALLTHRU */

    case V2DFmode:
    case V2DImode:
    case V4SFmode:
    case V4SImode:
      val = force_reg (GET_MODE_INNER (mode), val);
      x = gen_rtx_VEC_DUPLICATE (mode, val);
      emit_insn (gen_rtx_SET (VOIDmode, target, x));
      return true;

    case V4HImode:
      if (!mmx_ok)
	return false;
      if (TARGET_SSE || TARGET_3DNOW_A)
	{
	  val = gen_lowpart (SImode, val);
	  x = gen_rtx_TRUNCATE (HImode, val);
	  x = gen_rtx_VEC_DUPLICATE (mode, x);
	  emit_insn (gen_rtx_SET (VOIDmode, target, x));
	  return true;
	}
      else
	{
	  smode = HImode;
	  wsmode = SImode;
	  wvmode = V2SImode;
	  goto widen;
	}

    case V8QImode:
      if (!mmx_ok)
	return false;
      smode = QImode;
      wsmode = HImode;
      wvmode = V4HImode;
      goto widen;
    case V8HImode:
      if (TARGET_SSE2)
	{
	  rtx tmp1, tmp2;
	  /* Extend HImode to SImode using a paradoxical SUBREG.  */
	  tmp1 = gen_reg_rtx (SImode);
	  emit_move_insn (tmp1, gen_lowpart (SImode, val));
	  /* Insert the SImode value as low element of V4SImode vector. */
	  tmp2 = gen_reg_rtx (V4SImode);
	  tmp1 = gen_rtx_VEC_MERGE (V4SImode,
				    gen_rtx_VEC_DUPLICATE (V4SImode, tmp1),
				    CONST0_RTX (V4SImode),
				    const1_rtx);
	  emit_insn (gen_rtx_SET (VOIDmode, tmp2, tmp1));
	  /* Cast the V4SImode vector back to a V8HImode vector.  */
	  tmp1 = gen_reg_rtx (V8HImode);
	  emit_move_insn (tmp1, gen_lowpart (V8HImode, tmp2));
	  /* Duplicate the low short through the whole low SImode word.  */
	  emit_insn (gen_sse2_punpcklwd (tmp1, tmp1, tmp1));
	  /* Cast the V8HImode vector back to a V4SImode vector.  */
	  tmp2 = gen_reg_rtx (V4SImode);
	  emit_move_insn (tmp2, gen_lowpart (V4SImode, tmp1));
	  /* Replicate the low element of the V4SImode vector.  */
	  emit_insn (gen_sse2_pshufd (tmp2, tmp2, const0_rtx));
	  /* Cast the V2SImode back to V8HImode, and store in target.  */
	  emit_move_insn (target, gen_lowpart (V8HImode, tmp2));
	  return true;
	}
      smode = HImode;
      wsmode = SImode;
      wvmode = V4SImode;
      goto widen;
    case V16QImode:
      if (TARGET_SSE2)
	{
	  rtx tmp1, tmp2;
	  /* Extend QImode to SImode using a paradoxical SUBREG.  */
	  tmp1 = gen_reg_rtx (SImode);
	  emit_move_insn (tmp1, gen_lowpart (SImode, val));
	  /* Insert the SImode value as low element of V4SImode vector. */
	  tmp2 = gen_reg_rtx (V4SImode);
	  tmp1 = gen_rtx_VEC_MERGE (V4SImode,
				    gen_rtx_VEC_DUPLICATE (V4SImode, tmp1),
				    CONST0_RTX (V4SImode),
				    const1_rtx);
	  emit_insn (gen_rtx_SET (VOIDmode, tmp2, tmp1));
	  /* Cast the V4SImode vector back to a V16QImode vector.  */
	  tmp1 = gen_reg_rtx (V16QImode);
	  emit_move_insn (tmp1, gen_lowpart (V16QImode, tmp2));
	  /* Duplicate the low byte through the whole low SImode word.  */
	  emit_insn (gen_sse2_punpcklbw (tmp1, tmp1, tmp1));
	  emit_insn (gen_sse2_punpcklbw (tmp1, tmp1, tmp1));
	  /* Cast the V16QImode vector back to a V4SImode vector.  */
	  tmp2 = gen_reg_rtx (V4SImode);
	  emit_move_insn (tmp2, gen_lowpart (V4SImode, tmp1));
	  /* Replicate the low element of the V4SImode vector.  */
	  emit_insn (gen_sse2_pshufd (tmp2, tmp2, const0_rtx));
	  /* Cast the V2SImode back to V16QImode, and store in target.  */
	  emit_move_insn (target, gen_lowpart (V16QImode, tmp2));
	  return true;
	}
      smode = QImode;
      wsmode = HImode;
      wvmode = V8HImode;
      goto widen;
    widen:
      /* Replicate the value once into the next wider mode and recurse.  */
      val = convert_modes (wsmode, smode, val, true);
      x = expand_simple_binop (wsmode, ASHIFT, val,
			       GEN_INT (GET_MODE_BITSIZE (smode)),
			       NULL_RTX, 1, OPTAB_LIB_WIDEN);
      val = expand_simple_binop (wsmode, IOR, val, x, x, 1, OPTAB_LIB_WIDEN);

      x = gen_reg_rtx (wvmode);
      if (!ix86_expand_vector_init_duplicate (mmx_ok, wvmode, x, val))
	gcc_unreachable ();
      emit_move_insn (target, gen_lowpart (mode, x));
      return true;

    default:
      return false;
    }
}

/* A subroutine of ix86_expand_vector_init.  Store into TARGET a vector
   whose ONE_VAR element is VAR, and other elements are zero.  Return true
   if successful.  */

static bool
ix86_expand_vector_init_one_nonzero (bool mmx_ok, enum machine_mode mode,
				     rtx target, rtx var, int one_var)
{
  enum machine_mode vsimode;
  rtx new_target;
  rtx x, tmp;

  switch (mode)
    {
    case V2SFmode:
    case V2SImode:
      if (!mmx_ok)
	return false;
      /* FALLTHRU */

    case V2DFmode:
    case V2DImode:
      if (one_var != 0)
	return false;
      var = force_reg (GET_MODE_INNER (mode), var);
      x = gen_rtx_VEC_CONCAT (mode, var, CONST0_RTX (GET_MODE_INNER (mode)));
      emit_insn (gen_rtx_SET (VOIDmode, target, x));
      return true;

    case V4SFmode:
    case V4SImode:
      if (!REG_P (target) || REGNO (target) < FIRST_PSEUDO_REGISTER)
	new_target = gen_reg_rtx (mode);
      else
	new_target = target;
      var = force_reg (GET_MODE_INNER (mode), var);
      x = gen_rtx_VEC_DUPLICATE (mode, var);
      x = gen_rtx_VEC_MERGE (mode, x, CONST0_RTX (mode), const1_rtx);
      emit_insn (gen_rtx_SET (VOIDmode, new_target, x));
      if (one_var != 0)
	{
	  /* We need to shuffle the value to the correct position, so
	     create a new pseudo to store the intermediate result.  */

	  /* With SSE2, we can use the integer shuffle insns.  */
	  if (mode != V4SFmode && TARGET_SSE2)
	    {
	      emit_insn (gen_sse2_pshufd_1 (new_target, new_target,
					    GEN_INT (1),
					    GEN_INT (one_var == 1 ? 0 : 1),
					    GEN_INT (one_var == 2 ? 0 : 1),
					    GEN_INT (one_var == 3 ? 0 : 1)));
	      if (target != new_target)
		emit_move_insn (target, new_target);
	      return true;
	    }

	  /* Otherwise convert the intermediate result to V4SFmode and
	     use the SSE1 shuffle instructions.  */
	  if (mode != V4SFmode)
	    {
	      tmp = gen_reg_rtx (V4SFmode);
	      emit_move_insn (tmp, gen_lowpart (V4SFmode, new_target));
	    }
	  else
	    tmp = new_target;

	  emit_insn (gen_sse_shufps_1 (tmp, tmp, tmp,
				       GEN_INT (1),
				       GEN_INT (one_var == 1 ? 0 : 1),
				       GEN_INT (one_var == 2 ? 0+4 : 1+4),
				       GEN_INT (one_var == 3 ? 0+4 : 1+4)));

	  if (mode != V4SFmode)
	    emit_move_insn (target, gen_lowpart (V4SImode, tmp));
	  else if (tmp != target)
	    emit_move_insn (target, tmp);
	}
      else if (target != new_target)
	emit_move_insn (target, new_target);
      return true;

    case V8HImode:
    case V16QImode:
      vsimode = V4SImode;
      goto widen;
    case V4HImode:
    case V8QImode:
      if (!mmx_ok)
	return false;
      vsimode = V2SImode;
      goto widen;
    widen:
      if (one_var != 0)
	return false;

      /* Zero extend the variable element to SImode and recurse.  */
      var = convert_modes (SImode, GET_MODE_INNER (mode), var, true);

      x = gen_reg_rtx (vsimode);
      if (!ix86_expand_vector_init_one_nonzero (mmx_ok, vsimode, x,
						var, one_var))
	gcc_unreachable ();

      emit_move_insn (target, gen_lowpart (mode, x));
      return true;

    default:
      return false;
    }
}

/* A subroutine of ix86_expand_vector_init.  Store into TARGET a vector
   consisting of the values in VALS.  It is known that all elements
   except ONE_VAR are constants.  Return true if successful.  */

static bool
ix86_expand_vector_init_one_var (bool mmx_ok, enum machine_mode mode,
				 rtx target, rtx vals, int one_var)
{
  rtx var = XVECEXP (vals, 0, one_var);
  enum machine_mode wmode;
  rtx const_vec, x;

  const_vec = copy_rtx (vals);
  XVECEXP (const_vec, 0, one_var) = CONST0_RTX (GET_MODE_INNER (mode));
  const_vec = gen_rtx_CONST_VECTOR (mode, XVEC (const_vec, 0));

  switch (mode)
    {
    case V2DFmode:
    case V2DImode:
    case V2SFmode:
    case V2SImode:
      /* For the two element vectors, it's just as easy to use
	 the general case.  */
      return false;

    case V4SFmode:
    case V4SImode:
    case V8HImode:
    case V4HImode:
      break;

    case V16QImode:
      wmode = V8HImode;
      goto widen;
    case V8QImode:
      wmode = V4HImode;
      goto widen;
    widen:
      /* There's no way to set one QImode entry easily.  Combine
	 the variable value with its adjacent constant value, and
	 promote to an HImode set.  */
      x = XVECEXP (vals, 0, one_var ^ 1);
      if (one_var & 1)
	{
	  var = convert_modes (HImode, QImode, var, true);
	  var = expand_simple_binop (HImode, ASHIFT, var, GEN_INT (8),
				     NULL_RTX, 1, OPTAB_LIB_WIDEN);
	  x = GEN_INT (INTVAL (x) & 0xff);
	}
      else
	{
	  var = convert_modes (HImode, QImode, var, true);
	  x = gen_int_mode (INTVAL (x) << 8, HImode);
	}
      if (x != const0_rtx)
	var = expand_simple_binop (HImode, IOR, var, x, var,
				   1, OPTAB_LIB_WIDEN);

      x = gen_reg_rtx (wmode);
      emit_move_insn (x, gen_lowpart (wmode, const_vec));
      ix86_expand_vector_set (mmx_ok, x, var, one_var >> 1);

      emit_move_insn (target, gen_lowpart (mode, x));
      return true;

    default:
      return false;
    }

  emit_move_insn (target, const_vec);
  ix86_expand_vector_set (mmx_ok, target, var, one_var);
  return true;
}

/* A subroutine of ix86_expand_vector_init.  Handle the most general case:
   all values variable, and none identical.  */

static void
ix86_expand_vector_init_general (bool mmx_ok, enum machine_mode mode,
				 rtx target, rtx vals)
{
  enum machine_mode half_mode = GET_MODE_INNER (mode);
  rtx op0 = NULL, op1 = NULL;
  bool use_vec_concat = false;

  switch (mode)
    {
    case V2SFmode:
    case V2SImode:
      if (!mmx_ok && !TARGET_SSE)
	break;
      /* FALLTHRU */

    case V2DFmode:
    case V2DImode:
      /* For the two element vectors, we always implement VEC_CONCAT.  */
      op0 = XVECEXP (vals, 0, 0);
      op1 = XVECEXP (vals, 0, 1);
      use_vec_concat = true;
      break;

    case V4SFmode:
      half_mode = V2SFmode;
      goto half;
    case V4SImode:
      half_mode = V2SImode;
      goto half;
    half:
      {
	rtvec v;

	/* For V4SF and V4SI, we implement a concat of two V2 vectors.
	   Recurse to load the two halves.  */

	op0 = gen_reg_rtx (half_mode);
	v = gen_rtvec (2, XVECEXP (vals, 0, 0), XVECEXP (vals, 0, 1));
	ix86_expand_vector_init (false, op0, gen_rtx_PARALLEL (half_mode, v));

	op1 = gen_reg_rtx (half_mode);
	v = gen_rtvec (2, XVECEXP (vals, 0, 2), XVECEXP (vals, 0, 3));
	ix86_expand_vector_init (false, op1, gen_rtx_PARALLEL (half_mode, v));

	use_vec_concat = true;
      }
      break;

    case V8HImode:
    case V16QImode:
    case V4HImode:
    case V8QImode:
      break;

    default:
      gcc_unreachable ();
    }

  if (use_vec_concat)
    {
      if (!register_operand (op0, half_mode))
	op0 = force_reg (half_mode, op0);
      if (!register_operand (op1, half_mode))
	op1 = force_reg (half_mode, op1);

      emit_insn (gen_rtx_SET (VOIDmode, target,
			      gen_rtx_VEC_CONCAT (mode, op0, op1)));
    }
  else
    {
      int i, j, n_elts, n_words, n_elt_per_word;
      enum machine_mode inner_mode;
      rtx words[4], shift;

      inner_mode = GET_MODE_INNER (mode);
      n_elts = GET_MODE_NUNITS (mode);
      n_words = GET_MODE_SIZE (mode) / UNITS_PER_WORD;
      n_elt_per_word = n_elts / n_words;
      shift = GEN_INT (GET_MODE_BITSIZE (inner_mode));

      for (i = 0; i < n_words; ++i)
	{
	  rtx word = NULL_RTX;

	  for (j = 0; j < n_elt_per_word; ++j)
	    {
	      rtx elt = XVECEXP (vals, 0, (i+1)*n_elt_per_word - j - 1);
	      elt = convert_modes (word_mode, inner_mode, elt, true);

	      if (j == 0)
		word = elt;
	      else
		{
		  word = expand_simple_binop (word_mode, ASHIFT, word, shift,
					      word, 1, OPTAB_LIB_WIDEN);
		  word = expand_simple_binop (word_mode, IOR, word, elt,
					      word, 1, OPTAB_LIB_WIDEN);
		}
	    }

	  words[i] = word;
	}

      if (n_words == 1)
	emit_move_insn (target, gen_lowpart (mode, words[0]));
      else if (n_words == 2)
	{
	  rtx tmp = gen_reg_rtx (mode);
	  emit_insn (gen_rtx_CLOBBER (VOIDmode, tmp));
	  emit_move_insn (gen_lowpart (word_mode, tmp), words[0]);
	  emit_move_insn (gen_highpart (word_mode, tmp), words[1]);
	  emit_move_insn (target, tmp);
	}
      else if (n_words == 4)
	{
	  rtx tmp = gen_reg_rtx (V4SImode);
	  vals = gen_rtx_PARALLEL (V4SImode, gen_rtvec_v (4, words));
	  ix86_expand_vector_init_general (false, V4SImode, tmp, vals);
	  emit_move_insn (target, gen_lowpart (mode, tmp));
	}
      else
	gcc_unreachable ();
    }
}

/* Initialize vector TARGET via VALS.  Suppress the use of MMX
   instructions unless MMX_OK is true.  */

void
ix86_expand_vector_init (bool mmx_ok, rtx target, rtx vals)
{
  enum machine_mode mode = GET_MODE (target);
  enum machine_mode inner_mode = GET_MODE_INNER (mode);
  int n_elts = GET_MODE_NUNITS (mode);
  int n_var = 0, one_var = -1;
  bool all_same = true, all_const_zero = true;
  int i;
  rtx x;

  for (i = 0; i < n_elts; ++i)
    {
      x = XVECEXP (vals, 0, i);
      if (!CONSTANT_P (x))
	n_var++, one_var = i;
      else if (x != CONST0_RTX (inner_mode))
	all_const_zero = false;
      if (i > 0 && !rtx_equal_p (x, XVECEXP (vals, 0, 0)))
	all_same = false;
    }

  /* Constants are best loaded from the constant pool.  */
  if (n_var == 0)
    {
      emit_move_insn (target, gen_rtx_CONST_VECTOR (mode, XVEC (vals, 0)));
      return;
    }

  /* If all values are identical, broadcast the value.  */
  if (all_same
      && ix86_expand_vector_init_duplicate (mmx_ok, mode, target,
					    XVECEXP (vals, 0, 0)))
    return;

  /* Values where only one field is non-constant are best loaded from
     the pool and overwritten via move later.  */
  if (n_var == 1)
    {
      if (all_const_zero
	  && ix86_expand_vector_init_one_nonzero (mmx_ok, mode, target,
						  XVECEXP (vals, 0, one_var),
						  one_var))
	return;

      if (ix86_expand_vector_init_one_var (mmx_ok, mode, target, vals, one_var))
	return;
    }

  ix86_expand_vector_init_general (mmx_ok, mode, target, vals);
}

void
ix86_expand_vector_set (bool mmx_ok, rtx target, rtx val, int elt)
{
  enum machine_mode mode = GET_MODE (target);
  enum machine_mode inner_mode = GET_MODE_INNER (mode);
  bool use_vec_merge = false;
  rtx tmp;

  switch (mode)
    {
    case V2SFmode:
    case V2SImode:
      if (mmx_ok)
	{
	  tmp = gen_reg_rtx (GET_MODE_INNER (mode));
	  ix86_expand_vector_extract (true, tmp, target, 1 - elt);
	  if (elt == 0)
	    tmp = gen_rtx_VEC_CONCAT (mode, tmp, val);
	  else
	    tmp = gen_rtx_VEC_CONCAT (mode, val, tmp);
	  emit_insn (gen_rtx_SET (VOIDmode, target, tmp));
	  return;
	}
      break;

    case V2DFmode:
    case V2DImode:
      {
	rtx op0, op1;

	/* For the two element vectors, we implement a VEC_CONCAT with
	   the extraction of the other element.  */

	tmp = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (1, GEN_INT (1 - elt)));
	tmp = gen_rtx_VEC_SELECT (inner_mode, target, tmp);

	if (elt == 0)
	  op0 = val, op1 = tmp;
	else
	  op0 = tmp, op1 = val;

	tmp = gen_rtx_VEC_CONCAT (mode, op0, op1);
	emit_insn (gen_rtx_SET (VOIDmode, target, tmp));
      }
      return;

    case V4SFmode:
      switch (elt)
	{
	case 0:
	  use_vec_merge = true;
	  break;

	case 1:
	  /* tmp = target = A B C D */
	  tmp = copy_to_reg (target);
	  /* target = A A B B */
	  emit_insn (gen_sse_unpcklps (target, target, target));
	  /* target = X A B B */
	  ix86_expand_vector_set (false, target, val, 0);
	  /* target = A X C D  */
	  emit_insn (gen_sse_shufps_1 (target, target, tmp,
				       GEN_INT (1), GEN_INT (0),
				       GEN_INT (2+4), GEN_INT (3+4)));
	  return;

	case 2:
	  /* tmp = target = A B C D */
	  tmp = copy_to_reg (target);
	  /* tmp = X B C D */
	  ix86_expand_vector_set (false, tmp, val, 0);
	  /* target = A B X D */
	  emit_insn (gen_sse_shufps_1 (target, target, tmp,
				       GEN_INT (0), GEN_INT (1),
				       GEN_INT (0+4), GEN_INT (3+4)));
	  return;

	case 3:
	  /* tmp = target = A B C D */
	  tmp = copy_to_reg (target);
	  /* tmp = X B C D */
	  ix86_expand_vector_set (false, tmp, val, 0);
	  /* target = A B X D */
	  emit_insn (gen_sse_shufps_1 (target, target, tmp,
				       GEN_INT (0), GEN_INT (1),
				       GEN_INT (2+4), GEN_INT (0+4)));
	  return;

	default:
	  gcc_unreachable ();
	}
      break;

    case V4SImode:
      /* Element 0 handled by vec_merge below.  */
      if (elt == 0)
	{
	  use_vec_merge = true;
	  break;
	}

      if (TARGET_SSE2)
	{
	  /* With SSE2, use integer shuffles to swap element 0 and ELT,
	     store into element 0, then shuffle them back.  */

	  rtx order[4];

	  order[0] = GEN_INT (elt);
	  order[1] = const1_rtx;
	  order[2] = const2_rtx;
	  order[3] = GEN_INT (3);
	  order[elt] = const0_rtx;

	  emit_insn (gen_sse2_pshufd_1 (target, target, order[0],
					order[1], order[2], order[3]));

	  ix86_expand_vector_set (false, target, val, 0);

	  emit_insn (gen_sse2_pshufd_1 (target, target, order[0],
					order[1], order[2], order[3]));
	}
      else
	{
	  /* For SSE1, we have to reuse the V4SF code.  */
	  ix86_expand_vector_set (false, gen_lowpart (V4SFmode, target),
				  gen_lowpart (SFmode, val), elt);
	}
      return;

    case V8HImode:
      use_vec_merge = TARGET_SSE2;
      break;
    case V4HImode:
      use_vec_merge = mmx_ok && (TARGET_SSE || TARGET_3DNOW_A);
      break;

    case V16QImode:
    case V8QImode:
    default:
      break;
    }

  if (use_vec_merge)
    {
      tmp = gen_rtx_VEC_DUPLICATE (mode, val);
      tmp = gen_rtx_VEC_MERGE (mode, tmp, target, GEN_INT (1 << elt));
      emit_insn (gen_rtx_SET (VOIDmode, target, tmp));
    }
  else
    {
      rtx mem = assign_stack_temp (mode, GET_MODE_SIZE (mode), false);

      emit_move_insn (mem, target);

      tmp = adjust_address (mem, inner_mode, elt*GET_MODE_SIZE (inner_mode));
      emit_move_insn (tmp, val);

      emit_move_insn (target, mem);
    }
}

void
ix86_expand_vector_extract (bool mmx_ok, rtx target, rtx vec, int elt)
{
  enum machine_mode mode = GET_MODE (vec);
  enum machine_mode inner_mode = GET_MODE_INNER (mode);
  bool use_vec_extr = false;
  rtx tmp;

  switch (mode)
    {
    case V2SImode:
    case V2SFmode:
      if (!mmx_ok)
	break;
      /* FALLTHRU */

    case V2DFmode:
    case V2DImode:
      use_vec_extr = true;
      break;

    case V4SFmode:
      switch (elt)
	{
	case 0:
	  tmp = vec;
	  break;

	case 1:
	case 3:
	  tmp = gen_reg_rtx (mode);
	  emit_insn (gen_sse_shufps_1 (tmp, vec, vec,
				       GEN_INT (elt), GEN_INT (elt),
				       GEN_INT (elt+4), GEN_INT (elt+4)));
	  break;

	case 2:
	  tmp = gen_reg_rtx (mode);
	  emit_insn (gen_sse_unpckhps (tmp, vec, vec));
	  break;

	default:
	  gcc_unreachable ();
	}
      vec = tmp;
      use_vec_extr = true;
      elt = 0;
      break;

    case V4SImode:
      if (TARGET_SSE2)
	{
	  switch (elt)
	    {
	    case 0:
	      tmp = vec;
	      break;

	    case 1:
	    case 3:
	      tmp = gen_reg_rtx (mode);
	      emit_insn (gen_sse2_pshufd_1 (tmp, vec,
					    GEN_INT (elt), GEN_INT (elt),
					    GEN_INT (elt), GEN_INT (elt)));
	      break;

	    case 2:
	      tmp = gen_reg_rtx (mode);
	      emit_insn (gen_sse2_punpckhdq (tmp, vec, vec));
	      break;

	    default:
	      gcc_unreachable ();
	    }
	  vec = tmp;
	  use_vec_extr = true;
	  elt = 0;
	}
      else
	{
	  /* For SSE1, we have to reuse the V4SF code.  */
	  ix86_expand_vector_extract (false, gen_lowpart (SFmode, target),
				      gen_lowpart (V4SFmode, vec), elt);
	  return;
	}
      break;

    case V8HImode:
      use_vec_extr = TARGET_SSE2;
      break;
    case V4HImode:
      use_vec_extr = mmx_ok && (TARGET_SSE || TARGET_3DNOW_A);
      break;

    case V16QImode:
    case V8QImode:
      /* ??? Could extract the appropriate HImode element and shift.  */
    default:
      break;
    }

  if (use_vec_extr)
    {
      tmp = gen_rtx_PARALLEL (VOIDmode, gen_rtvec (1, GEN_INT (elt)));
      tmp = gen_rtx_VEC_SELECT (inner_mode, vec, tmp);

      /* Let the rtl optimizers know about the zero extension performed.  */
      if (inner_mode == HImode)
	{
	  tmp = gen_rtx_ZERO_EXTEND (SImode, tmp);
	  target = gen_lowpart (SImode, target);
	}

      emit_insn (gen_rtx_SET (VOIDmode, target, tmp));
    }
  else
    {
      rtx mem = assign_stack_temp (mode, GET_MODE_SIZE (mode), false);

      emit_move_insn (mem, vec);

      tmp = adjust_address (mem, inner_mode, elt*GET_MODE_SIZE (inner_mode));
      emit_move_insn (target, tmp);
    }
}

/* Expand a vector reduction on V4SFmode for SSE1.  FN is the binary
   pattern to reduce; DEST is the destination; IN is the input vector.  */

void
ix86_expand_reduc_v4sf (rtx (*fn) (rtx, rtx, rtx), rtx dest, rtx in)
{
  rtx tmp1, tmp2, tmp3;

  tmp1 = gen_reg_rtx (V4SFmode);
  tmp2 = gen_reg_rtx (V4SFmode);
  tmp3 = gen_reg_rtx (V4SFmode);

  emit_insn (gen_sse_movhlps (tmp1, in, in));
  emit_insn (fn (tmp2, tmp1, in));

  emit_insn (gen_sse_shufps_1 (tmp3, tmp2, tmp2,
			       GEN_INT (1), GEN_INT (1),
			       GEN_INT (1+4), GEN_INT (1+4)));
  emit_insn (fn (dest, tmp2, tmp3));
}

/* Target hook for scalar_mode_supported_p.  */
static bool
ix86_scalar_mode_supported_p (enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode))
    return true;
  else
    return default_scalar_mode_supported_p (mode);
}

/* Implements target hook vector_mode_supported_p.  */
static bool
ix86_vector_mode_supported_p (enum machine_mode mode)
{
  if (TARGET_SSE && VALID_SSE_REG_MODE (mode))
    return true;
  if (TARGET_SSE2 && VALID_SSE2_REG_MODE (mode))
    return true;
  if (TARGET_MMX && VALID_MMX_REG_MODE (mode))
    return true;
  if (TARGET_3DNOW && VALID_MMX_REG_MODE_3DNOW (mode))
    return true;
  return false;
}

/* Worker function for TARGET_MD_ASM_CLOBBERS.

   We do this in the new i386 backend to maintain source compatibility
   with the old cc0-based compiler.  */

static tree
ix86_md_asm_clobbers (tree outputs ATTRIBUTE_UNUSED,
		      tree inputs ATTRIBUTE_UNUSED,
		      tree clobbers)
{
  clobbers = tree_cons (NULL_TREE, build_string (5, "flags"),
			clobbers);
  clobbers = tree_cons (NULL_TREE, build_string (4, "fpsr"),
			clobbers);
  clobbers = tree_cons (NULL_TREE, build_string (7, "dirflag"),
			clobbers);
  return clobbers;
}

/* Return true if this goes in small data/bss.  */

static bool
ix86_in_large_data_p (tree exp)
{
  if (ix86_cmodel != CM_MEDIUM && ix86_cmodel != CM_MEDIUM_PIC)
    return false;

  /* Functions are never large data.  */
  if (TREE_CODE (exp) == FUNCTION_DECL)
    return false;

  if (TREE_CODE (exp) == VAR_DECL && DECL_SECTION_NAME (exp))
    {
      const char *section = TREE_STRING_POINTER (DECL_SECTION_NAME (exp));
      if (strcmp (section, ".ldata") == 0
	  || strcmp (section, ".lbss") == 0)
	return true;
      return false;
    }
  else
    {
      HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (exp));

      /* If this is an incomplete type with size 0, then we can't put it
	 in data because it might be too big when completed.  */
      if (!size || size > ix86_section_threshold)
	return true;
    }

  return false;
}
static void
ix86_encode_section_info (tree decl, rtx rtl, int first)
{
  default_encode_section_info (decl, rtl, first);

  if (TREE_CODE (decl) == VAR_DECL
      && (TREE_STATIC (decl) || DECL_EXTERNAL (decl))
      && ix86_in_large_data_p (decl))
    SYMBOL_REF_FLAGS (XEXP (rtl, 0)) |= SYMBOL_FLAG_FAR_ADDR;
}

/* Worker function for REVERSE_CONDITION.  */

enum rtx_code
ix86_reverse_condition (enum rtx_code code, enum machine_mode mode)
{
  return (mode != CCFPmode && mode != CCFPUmode
	  ? reverse_condition (code)
	  : reverse_condition_maybe_unordered (code));
}

/* Output code to perform an x87 FP register move, from OPERANDS[1]
   to OPERANDS[0].  */

const char *
output_387_reg_move (rtx insn, rtx *operands)
{
  if (REG_P (operands[1])
      && find_regno_note (insn, REG_DEAD, REGNO (operands[1])))
    {
      if (REGNO (operands[0]) == FIRST_STACK_REG)
	return output_387_ffreep (operands, 0);
      return "fstp\t%y0";
    }
  if (STACK_TOP_P (operands[0]))
    return "fld%z1\t%y1";
  return "fst\t%y0";
}

/* Output code to perform a conditional jump to LABEL, if C2 flag in
   FP status register is set.  */

void
ix86_emit_fp_unordered_jump (rtx label)
{
  rtx reg = gen_reg_rtx (HImode);
  rtx temp;

  emit_insn (gen_x86_fnstsw_1 (reg));

  if (TARGET_USE_SAHF)
    {
      emit_insn (gen_x86_sahf_1 (reg));

      temp = gen_rtx_REG (CCmode, FLAGS_REG);
      temp = gen_rtx_UNORDERED (VOIDmode, temp, const0_rtx);
    }
  else
    {
      emit_insn (gen_testqi_ext_ccno_0 (reg, GEN_INT (0x04)));

      temp = gen_rtx_REG (CCNOmode, FLAGS_REG);
      temp = gen_rtx_NE (VOIDmode, temp, const0_rtx);
    }

  temp = gen_rtx_IF_THEN_ELSE (VOIDmode, temp,
			      gen_rtx_LABEL_REF (VOIDmode, label),
			      pc_rtx);
  temp = gen_rtx_SET (VOIDmode, pc_rtx, temp);
  emit_jump_insn (temp);
}

/* Output code to perform a log1p XFmode calculation.  */

void ix86_emit_i387_log1p (rtx op0, rtx op1)
{
  rtx label1 = gen_label_rtx ();
  rtx label2 = gen_label_rtx ();

  rtx tmp = gen_reg_rtx (XFmode);
  rtx tmp2 = gen_reg_rtx (XFmode);

  emit_insn (gen_absxf2 (tmp, op1));
  emit_insn (gen_cmpxf (tmp,
    CONST_DOUBLE_FROM_REAL_VALUE (
       REAL_VALUE_ATOF ("0.29289321881345247561810596348408353", XFmode),
       XFmode)));
  emit_jump_insn (gen_bge (label1));

  emit_move_insn (tmp2, standard_80387_constant_rtx (4)); /* fldln2 */
  emit_insn (gen_fyl2xp1_xf3 (op0, tmp2, op1));
  emit_jump (label2);

  emit_label (label1);
  emit_move_insn (tmp, CONST1_RTX (XFmode));
  emit_insn (gen_addxf3 (tmp, op1, tmp));
  emit_move_insn (tmp2, standard_80387_constant_rtx (4)); /* fldln2 */
  emit_insn (gen_fyl2x_xf3 (op0, tmp2, tmp));

  emit_label (label2);
}

/* Solaris implementation of TARGET_ASM_NAMED_SECTION.  */

static void
i386_solaris_elf_named_section (const char *name, unsigned int flags,
				tree decl)
{
  /* With Binutils 2.15, the "@unwind" marker must be specified on
     every occurrence of the ".eh_frame" section, not just the first
     one.  */
  if (TARGET_64BIT
      && strcmp (name, ".eh_frame") == 0)
    {
      fprintf (asm_out_file, "\t.section\t%s,\"%s\",@unwind\n", name,
	       flags & SECTION_WRITE ? "aw" : "a");
      return;
    }
  default_elf_asm_named_section (name, flags, decl);
}

/* Return the mangling of TYPE if it is an extended fundamental type.  */

static const char *
ix86_mangle_fundamental_type (tree type)
{
  switch (TYPE_MODE (type))
    {
    case TFmode:
      /* __float128 is "g".  */
      return "g";
    case XFmode:
      /* "long double" or __float80 is "e".  */
      return "e";
    default:
      return NULL;
    }
}

/* For 32-bit code we can save PIC register setup by using
   __stack_chk_fail_local hidden function instead of calling
   __stack_chk_fail directly.  64-bit code doesn't need to setup any PIC
   register, so it is better to call __stack_chk_fail directly.  */

static tree
ix86_stack_protect_fail (void)
{
  return TARGET_64BIT
	 ? default_external_stack_protect_fail ()
	 : default_hidden_stack_protect_fail ();
}

/* Select a format to encode pointers in exception handling data.  CODE
   is 0 for data, 1 for code labels, 2 for function pointers.  GLOBAL is
   true if the symbol may be affected by dynamic relocations.

   ??? All x86 object file formats are capable of representing this.
   After all, the relocation needed is the same as for the call insn.
   Whether or not a particular assembler allows us to enter such, I
   guess we'll have to see.  */
int
asm_preferred_eh_data_format (int code, int global)
{
  if (flag_pic)
    {
      int type = DW_EH_PE_sdata8;
      if (!TARGET_64BIT
	  || ix86_cmodel == CM_SMALL_PIC
	  || (ix86_cmodel == CM_MEDIUM_PIC && (global || code)))
	type = DW_EH_PE_sdata4;
      return (global ? DW_EH_PE_indirect : 0) | DW_EH_PE_pcrel | type;
    }
  if (ix86_cmodel == CM_SMALL
      || (ix86_cmodel == CM_MEDIUM && code))
    return DW_EH_PE_udata4;
  return DW_EH_PE_absptr;
}

#include "gt-i386.h"
