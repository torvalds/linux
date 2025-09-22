/* score-mdaux.h for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Sunnorth

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

#ifndef SCORE_MDAUX_0621
#define SCORE_MDAUX_0621

/* Machine Auxiliary Functions.  */
enum score_address_type
{
  ADD_REG,
  ADD_CONST_INT,
  ADD_SYMBOLIC
};
#ifdef RTX_CODE
struct score_address_info
{
  enum score_address_type type;
  rtx reg;
  rtx offset;
  enum rtx_code code;
  enum score_symbol_type symbol_type;
};
#endif

struct score_frame_info
{
  HOST_WIDE_INT total_size;       /* bytes that the entire frame takes up  */
  HOST_WIDE_INT var_size;         /* bytes that variables take up  */
  HOST_WIDE_INT args_size;        /* bytes that outgoing arguments take up  */
  HOST_WIDE_INT gp_reg_size;      /* bytes needed to store gp regs  */
  HOST_WIDE_INT gp_sp_offset;     /* offset from new sp to store gp registers  */
  HOST_WIDE_INT cprestore_size;   /* # bytes that the .cprestore slot takes up  */
  unsigned int  mask;             /* mask of saved gp registers  */
  int num_gp;                     /* number of gp registers saved  */
};

typedef void (*score_save_restore_fn) (rtx, rtx);

int mda_valid_base_register_p (rtx x, int strict);

#ifdef RTX_CODE
int mda_classify_address (struct score_address_info *info,
                          enum machine_mode mode, rtx x, int strict);

struct score_frame_info *mda_compute_frame_size (HOST_WIDE_INT size);

struct score_frame_info *mda_cached_frame (void);

void mda_gen_cmp (enum machine_mode mode);
#endif

int mda_symbolic_constant_p (rtx x, enum score_symbol_type *symbol_type);

int mda_bp (void);

/* Machine Expand.  */
void mdx_prologue (void);

void mdx_epilogue (int sibcall_p);

void mdx_movsicc (rtx *ops);

void mdx_call (rtx *ops, bool sibcall);

void mdx_call_value (rtx *ops, bool sibcall);

/* Machine Split.  */
void mds_movdi (rtx *ops);

void mds_zero_extract_andi (rtx *ops);

/* Machine Print.  */
enum mda_mem_unit {MDA_BYTE = 0, MDA_HWORD = 1, MDA_WORD = 2};

#define MDA_ALIGN_UNIT(V, UNIT)   !(V & ((1 << UNIT) - 1))

const char * mdp_linsn (rtx *ops, enum mda_mem_unit unit, bool sign);

const char * mdp_sinsn (rtx *ops, enum mda_mem_unit unit);

const char * mdp_select_add_imm (rtx *ops, bool set_cc);

const char * mdp_select (rtx *ops, const char *inst_pre,
                        bool commu, const char *letter, bool set_cc);

const char * mdp_limm (rtx *ops);

const char * mdp_move (rtx *ops);

/* Machine unaligned memory load/store. */
bool mdx_unaligned_load (rtx* ops);

bool mdx_unaligned_store (rtx* ops);

bool mdx_block_move (rtx* ops);

#endif

