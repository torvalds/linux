/* SSA operand management for trees.
   Copyright (C) 2003, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_TREE_SSA_OPERANDS_H
#define GCC_TREE_SSA_OPERANDS_H

/* Interface to SSA operands.  */


/* This represents a pointer to a DEF operand.  */
typedef tree *def_operand_p;

/* This represents a pointer to a USE operand.  */
typedef ssa_use_operand_t *use_operand_p;

/* NULL operand types.  */
#define NULL_USE_OPERAND_P 		NULL
#define NULL_DEF_OPERAND_P 		NULL

/* This represents the DEF operands of a stmt.  */
struct def_optype_d
{
  struct def_optype_d *next;
  tree *def_ptr;
};
typedef struct def_optype_d *def_optype_p;

/* This represents the USE operands of a stmt.  */
struct use_optype_d 
{
  struct use_optype_d *next;
  struct ssa_use_operand_d use_ptr;
};
typedef struct use_optype_d *use_optype_p;

/* This represents the MAY_DEFS for a stmt.  */
struct maydef_optype_d
{
  struct maydef_optype_d *next;
  tree def_var;
  tree use_var;
  struct ssa_use_operand_d use_ptr;
};
typedef struct maydef_optype_d *maydef_optype_p;

/* This represents the VUSEs for a stmt.  */
struct vuse_optype_d
{
  struct vuse_optype_d *next;
  tree use_var;
  struct ssa_use_operand_d use_ptr;
};
typedef struct vuse_optype_d *vuse_optype_p;
                                                                              
/* This represents the V_MUST_DEFS for a stmt.  */
struct mustdef_optype_d
{
  struct mustdef_optype_d *next;
  tree def_var;
  tree kill_var;
  struct ssa_use_operand_d use_ptr;
};
typedef struct mustdef_optype_d *mustdef_optype_p;


#define SSA_OPERAND_MEMORY_SIZE		(2048 - sizeof (void *))
                                                                              
struct ssa_operand_memory_d GTY((chain_next("%h.next")))
{
  struct ssa_operand_memory_d *next;
  char mem[SSA_OPERAND_MEMORY_SIZE];
};


/* This represents the operand cache for a stmt.  */
struct stmt_operands_d
{
  /* Statement operands.  */
  struct def_optype_d * def_ops;
  struct use_optype_d * use_ops;
                                                                              
  /* Virtual operands (V_MAY_DEF, VUSE, and V_MUST_DEF).  */
  struct maydef_optype_d * maydef_ops;
  struct vuse_optype_d * vuse_ops;
  struct mustdef_optype_d * mustdef_ops;
};
                                                                              
typedef struct stmt_operands_d *stmt_operands_p;
                                                                              
#define USE_FROM_PTR(PTR)	get_use_from_ptr (PTR)
#define DEF_FROM_PTR(PTR)	get_def_from_ptr (PTR)
#define SET_USE(USE, V)		set_ssa_use_from_ptr (USE, V)
#define SET_DEF(DEF, V)		((*(DEF)) = (V))

#define USE_STMT(USE)		(USE)->stmt

#define DEF_OPS(STMT)		(stmt_ann (STMT)->operands.def_ops)
#define USE_OPS(STMT)		(stmt_ann (STMT)->operands.use_ops)
#define VUSE_OPS(STMT)		(stmt_ann (STMT)->operands.vuse_ops)
#define MAYDEF_OPS(STMT)	(stmt_ann (STMT)->operands.maydef_ops)
#define MUSTDEF_OPS(STMT)	(stmt_ann (STMT)->operands.mustdef_ops)

#define USE_OP_PTR(OP)		(&((OP)->use_ptr))
#define USE_OP(OP)		(USE_FROM_PTR (USE_OP_PTR (OP)))

#define DEF_OP_PTR(OP)		((OP)->def_ptr)
#define DEF_OP(OP)		(DEF_FROM_PTR (DEF_OP_PTR (OP)))

#define VUSE_OP_PTR(OP)		USE_OP_PTR(OP)
#define VUSE_OP(OP)		((OP)->use_var)

#define MAYDEF_RESULT_PTR(OP)	(&((OP)->def_var))
#define MAYDEF_RESULT(OP)	((OP)->def_var)
#define MAYDEF_OP_PTR(OP)	USE_OP_PTR (OP)
#define MAYDEF_OP(OP)		((OP)->use_var)

#define MUSTDEF_RESULT_PTR(OP)	(&((OP)->def_var))
#define MUSTDEF_RESULT(OP)	((OP)->def_var)
#define MUSTDEF_KILL_PTR(OP)	USE_OP_PTR (OP)
#define MUSTDEF_KILL(OP)	((OP)->kill_var)

#define PHI_RESULT_PTR(PHI)	get_phi_result_ptr (PHI)
#define PHI_RESULT(PHI)		DEF_FROM_PTR (PHI_RESULT_PTR (PHI))
#define SET_PHI_RESULT(PHI, V)	SET_DEF (PHI_RESULT_PTR (PHI), (V))

#define PHI_ARG_DEF_PTR(PHI, I)	get_phi_arg_def_ptr ((PHI), (I))
#define PHI_ARG_DEF(PHI, I)	USE_FROM_PTR (PHI_ARG_DEF_PTR ((PHI), (I)))
#define SET_PHI_ARG_DEF(PHI, I, V)					\
				SET_USE (PHI_ARG_DEF_PTR ((PHI), (I)), (V))
#define PHI_ARG_DEF_FROM_EDGE(PHI, E)					\
				PHI_ARG_DEF ((PHI), (E)->dest_idx)
#define PHI_ARG_DEF_PTR_FROM_EDGE(PHI, E)				\
				PHI_ARG_DEF_PTR ((PHI), (E)->dest_idx)
#define PHI_ARG_INDEX_FROM_USE(USE)   phi_arg_index_from_use (USE)


extern void init_ssa_operands (void);
extern void fini_ssa_operands (void);
extern void free_ssa_operands (stmt_operands_p);
extern void update_stmt_operands (tree);
extern bool verify_imm_links (FILE *f, tree var);

extern void copy_virtual_operands (tree, tree);
extern void create_ssa_artficial_load_stmt (tree, tree);

extern void dump_immediate_uses (FILE *file);
extern void dump_immediate_uses_for (FILE *file, tree var);
extern void debug_immediate_uses (void);
extern void debug_immediate_uses_for (tree var);

extern bool ssa_operands_active (void);

extern void add_to_addressable_set (tree, bitmap *);

enum ssa_op_iter_type {
  ssa_op_iter_none = 0,
  ssa_op_iter_tree,
  ssa_op_iter_use,
  ssa_op_iter_def,
  ssa_op_iter_maymustdef
};
/* This structure is used in the operand iterator loops.  It contains the 
   items required to determine which operand is retrieved next.  During
   optimization, this structure is scalarized, and any unused fields are 
   optimized away, resulting in little overhead.  */

typedef struct ssa_operand_iterator_d
{
  def_optype_p defs;
  use_optype_p uses;
  vuse_optype_p vuses;
  maydef_optype_p maydefs;
  maydef_optype_p mayuses;
  mustdef_optype_p mustdefs;
  mustdef_optype_p mustkills;
  enum ssa_op_iter_type iter_type;
  int phi_i;
  int num_phi;
  tree phi_stmt;
  bool done;
} ssa_op_iter;

/* These flags are used to determine which operands are returned during 
   execution of the loop.  */
#define SSA_OP_USE		0x01	/* Real USE operands.  */
#define SSA_OP_DEF		0x02	/* Real DEF operands.  */
#define SSA_OP_VUSE		0x04	/* VUSE operands.  */
#define SSA_OP_VMAYUSE		0x08	/* USE portion of V_MAY_DEFS.  */
#define SSA_OP_VMAYDEF		0x10	/* DEF portion of V_MAY_DEFS.  */
#define SSA_OP_VMUSTDEF		0x20	/* V_MUST_DEF definitions.  */
#define SSA_OP_VMUSTKILL     	0x40    /* V_MUST_DEF kills.  */

/* These are commonly grouped operand flags.  */
#define SSA_OP_VIRTUAL_USES	(SSA_OP_VUSE | SSA_OP_VMAYUSE)
#define SSA_OP_VIRTUAL_DEFS	(SSA_OP_VMAYDEF | SSA_OP_VMUSTDEF)
#define SSA_OP_VIRTUAL_KILLS    (SSA_OP_VMUSTKILL)
#define SSA_OP_ALL_VIRTUALS     (SSA_OP_VIRTUAL_USES | SSA_OP_VIRTUAL_KILLS \
				 | SSA_OP_VIRTUAL_DEFS)
#define SSA_OP_ALL_USES		(SSA_OP_VIRTUAL_USES | SSA_OP_USE)
#define SSA_OP_ALL_DEFS		(SSA_OP_VIRTUAL_DEFS | SSA_OP_DEF)
#define SSA_OP_ALL_KILLS        (SSA_OP_VIRTUAL_KILLS)
#define SSA_OP_ALL_OPERANDS	(SSA_OP_ALL_USES | SSA_OP_ALL_DEFS	\
				 | SSA_OP_ALL_KILLS)

/* This macro executes a loop over the operands of STMT specified in FLAG, 
   returning each operand as a 'tree' in the variable TREEVAR.  ITER is an
   ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_TREE_OPERAND(TREEVAR, STMT, ITER, FLAGS)	\
  for (TREEVAR = op_iter_init_tree (&(ITER), STMT, FLAGS);	\
       !op_iter_done (&(ITER));					\
       TREEVAR = op_iter_next_tree (&(ITER)))

/* This macro executes a loop over the operands of STMT specified in FLAG, 
   returning each operand as a 'use_operand_p' in the variable USEVAR.  
   ITER is an ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_USE_OPERAND(USEVAR, STMT, ITER, FLAGS)	\
  for (USEVAR = op_iter_init_use (&(ITER), STMT, FLAGS);	\
       !op_iter_done (&(ITER));					\
       USEVAR = op_iter_next_use (&(ITER)))

/* This macro executes a loop over the operands of STMT specified in FLAG, 
   returning each operand as a 'def_operand_p' in the variable DEFVAR.  
   ITER is an ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_DEF_OPERAND(DEFVAR, STMT, ITER, FLAGS)	\
  for (DEFVAR = op_iter_init_def (&(ITER), STMT, FLAGS);	\
       !op_iter_done (&(ITER));					\
       DEFVAR = op_iter_next_def (&(ITER)))

/* This macro executes a loop over the V_MAY_DEF operands of STMT.  The def
   and use for each V_MAY_DEF is returned in DEFVAR and USEVAR. 
   ITER is an ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_MAYDEF_OPERAND(DEFVAR, USEVAR, STMT, ITER)	\
  for (op_iter_init_maydef (&(ITER), STMT, &(USEVAR), &(DEFVAR));	\
       !op_iter_done (&(ITER));					\
       op_iter_next_maymustdef (&(USEVAR), &(DEFVAR), &(ITER)))

/* This macro executes a loop over the V_MUST_DEF operands of STMT.  The def
   and kill for each V_MUST_DEF is returned in DEFVAR and KILLVAR. 
   ITER is an ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_MUSTDEF_OPERAND(DEFVAR, KILLVAR, STMT, ITER)	\
  for (op_iter_init_mustdef (&(ITER), STMT, &(KILLVAR), &(DEFVAR));	\
       !op_iter_done (&(ITER));					\
       op_iter_next_maymustdef (&(KILLVAR), &(DEFVAR), &(ITER)))

/* This macro executes a loop over the V_{MUST,MAY}_DEF of STMT.  The def
   and kill for each V_{MUST,MAY}_DEF is returned in DEFVAR and KILLVAR. 
   ITER is an ssa_op_iter structure used to control the loop.  */
#define FOR_EACH_SSA_MUST_AND_MAY_DEF_OPERAND(DEFVAR, KILLVAR, STMT, ITER)\
  for (op_iter_init_must_and_may_def (&(ITER), STMT, &(KILLVAR), &(DEFVAR));\
       !op_iter_done (&(ITER));					\
       op_iter_next_maymustdef (&(KILLVAR), &(DEFVAR), &(ITER)))

/* This macro will execute a loop over all the arguments of a PHI which
   match FLAGS.   A use_operand_p is always returned via USEVAR.  FLAGS
   can be either SSA_OP_USE or SSA_OP_VIRTUAL_USES or SSA_OP_ALL_USES.  */
#define FOR_EACH_PHI_ARG(USEVAR, STMT, ITER, FLAGS)		\
  for ((USEVAR) = op_iter_init_phiuse (&(ITER), STMT, FLAGS);	\
       !op_iter_done (&(ITER));					\
       (USEVAR) = op_iter_next_use (&(ITER)))


/* This macro will execute a loop over a stmt, regardless of whether it is
   a real stmt or a PHI node, looking at the USE nodes matching FLAGS.  */
#define FOR_EACH_PHI_OR_STMT_USE(USEVAR, STMT, ITER, FLAGS)	\
  for ((USEVAR) = (TREE_CODE (STMT) == PHI_NODE 		\
		   ? op_iter_init_phiuse (&(ITER), STMT, FLAGS)	\
		   : op_iter_init_use (&(ITER), STMT, FLAGS));	\
       !op_iter_done (&(ITER));					\
       (USEVAR) = op_iter_next_use (&(ITER)))

/* This macro will execute a loop over a stmt, regardless of whether it is
   a real stmt or a PHI node, looking at the DEF nodes matching FLAGS.  */
#define FOR_EACH_PHI_OR_STMT_DEF(DEFVAR, STMT, ITER, FLAGS)	\
  for ((DEFVAR) = (TREE_CODE (STMT) == PHI_NODE 		\
		   ? op_iter_init_phidef (&(ITER), STMT, FLAGS)	\
		   : op_iter_init_def (&(ITER), STMT, FLAGS));	\
       !op_iter_done (&(ITER));					\
       (DEFVAR) = op_iter_next_def (&(ITER)))
  
/* This macro returns an operand in STMT as a tree if it is the ONLY
   operand matching FLAGS.  If there are 0 or more than 1 operand matching
   FLAGS, then NULL_TREE is returned.  */
#define SINGLE_SSA_TREE_OPERAND(STMT, FLAGS)			\
  single_ssa_tree_operand (STMT, FLAGS)
                                                                                
/* This macro returns an operand in STMT as a use_operand_p if it is the ONLY
   operand matching FLAGS.  If there are 0 or more than 1 operand matching
   FLAGS, then NULL_USE_OPERAND_P is returned.  */
#define SINGLE_SSA_USE_OPERAND(STMT, FLAGS)			\
  single_ssa_use_operand (STMT, FLAGS)
                                                                                
/* This macro returns an operand in STMT as a def_operand_p if it is the ONLY
   operand matching FLAGS.  If there are 0 or more than 1 operand matching
   FLAGS, then NULL_DEF_OPERAND_P is returned.  */
#define SINGLE_SSA_DEF_OPERAND(STMT, FLAGS)			\
  single_ssa_def_operand (STMT, FLAGS)

/* This macro returns TRUE if there are no operands matching FLAGS in STMT.  */
#define ZERO_SSA_OPERANDS(STMT, FLAGS) 	zero_ssa_operands (STMT, FLAGS)

/* This macro counts the number of operands in STMT matching FLAGS.  */
#define NUM_SSA_OPERANDS(STMT, FLAGS)	num_ssa_operands (STMT, FLAGS)

#endif  /* GCC_TREE_SSA_OPERANDS_H  */
