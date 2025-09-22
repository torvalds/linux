/* score-conv.h for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef SCORE_CONV_0601
#define SCORE_CONV_0601

extern int target_flags;

#define GP_REG_FIRST                    0U
#define GP_REG_LAST                     31U
#define GP_REG_NUM                      (GP_REG_LAST - GP_REG_FIRST + 1U)
#define GP_DBX_FIRST                    0U

#define CE_REG_FIRST                    48U
#define CE_REG_LAST                     49U
#define CE_REG_NUM                      (CE_REG_LAST - CE_REG_FIRST + 1U)

#define ARG_REG_FIRST                   4U
#define ARG_REG_LAST                    7U
#define ARG_REG_NUM                     (ARG_REG_LAST - ARG_REG_FIRST + 1U)

#define REG_CONTAIN(REGNO, FIRST, NUM) \
  ((unsigned int)((int) (REGNO) - (FIRST)) < (NUM))

#define GP_REG_P(REGNO)        REG_CONTAIN (REGNO, GP_REG_FIRST, GP_REG_NUM)

#define G16_REG_P(REGNO)       REG_CONTAIN (REGNO, GP_REG_FIRST, 16)

#define CE_REG_P(REGNO)        REG_CONTAIN (REGNO, CE_REG_FIRST, CE_REG_NUM)

#define UIMM_IN_RANGE(V, W)  ((V) >= 0 && (V) < ((HOST_WIDE_INT) 1 << (W)))

#define SIMM_IN_RANGE(V, W)                            \
  ((V) >= (-1 * ((HOST_WIDE_INT) 1 << ((W) - 1)))      \
   && (V) < (1 * ((HOST_WIDE_INT) 1 << ((W) - 1))))

#define IMM_IN_RANGE(V, W, S) \
  ((S) ? SIMM_IN_RANGE (V, W) : UIMM_IN_RANGE (V, W))

#define IMM_IS_POW_OF_2(V, E1, E2)                \
  ((V) >= ((unsigned HOST_WIDE_INT) 1 << (E1))     \
   && (V) <= ((unsigned HOST_WIDE_INT) 1 << (E2))  \
   && ((V) & ((V) - 1)) == 0)

#define SCORE_STACK_ALIGN(LOC)          (((LOC) + 3) & ~3)

#define SCORE_MAX_FIRST_STACK_STEP      (0x3ff0)

#define SCORE_SDATA_MAX                 score_sdata_max ()

#define DEFAULT_SDATA_MAX               8

#define CONST_HIGH_PART(VALUE) \
  (((VALUE) + 0x8000) & ~(unsigned HOST_WIDE_INT) 0xffff)

#define CONST_LOW_PART(VALUE)           ((VALUE) - CONST_HIGH_PART (VALUE))

#define PROLOGUE_TEMP_REGNUM            (GP_REG_FIRST + 8)

#define EPILOGUE_TEMP_REGNUM            (GP_REG_FIRST + 8)

enum score_symbol_type
{
  SYMBOL_GENERAL,
  SYMBOL_SMALL_DATA     /* The symbol refers to something in a small data section.  */
};

int score_sdata_max (void);

#endif
