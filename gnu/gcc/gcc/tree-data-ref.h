/* Data references and dependences detectors. 
   Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <pop@cri.ensmp.fr>

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

#ifndef GCC_TREE_DATA_REF_H
#define GCC_TREE_DATA_REF_H

#include "lambda.h"

/** {base_address + offset + init} is the first location accessed by data-ref 
      in the loop, and step is the stride of data-ref in the loop in bytes;
      e.g.:
    
                       Example 1                      Example 2
      data-ref         a[j].b[i][j]                   a + x + 16B (a is int*)
      
First location info:
      base_address     &a                             a
      offset           j_0*D_j + i_0*D_i + C_a        x
      init             C_b                            16
      step             D_j                            4
      access_fn        NULL                           {16, +, 1}

Base object info:
      base_object      a                              NULL
      access_fn        <access_fns of indexes of b>   NULL

  **/
struct first_location_in_loop
{
  tree base_address;
  tree offset;
  tree init;
  tree step;
  /* Access function related to first location in the loop.  */
  VEC(tree,heap) *access_fns;

};

struct base_object_info
{
  /* The object.  */
  tree base_object;
  
  /* A list of chrecs.  Access functions related to BASE_OBJECT.  */
  VEC(tree,heap) *access_fns;
};

enum data_ref_type {
  ARRAY_REF_TYPE,
  POINTER_REF_TYPE
};

struct data_reference
{
  /* A pointer to the statement that contains this DR.  */
  tree stmt;
  
  /* A pointer to the ARRAY_REF node.  */
  tree ref;

  /* Auxiliary info specific to a pass.  */
  int aux;

  /* True when the data reference is in RHS of a stmt.  */
  bool is_read;

  /* First location accessed by the data-ref in the loop.  */
  struct first_location_in_loop first_location;

  /* Base object related info.  */
  struct base_object_info object_info;

  /* Aliasing information.  This field represents the symbol that
     should be aliased by a pointer holding the address of this data
     reference.  If the original data reference was a pointer
     dereference, then this field contains the memory tag that should
     be used by the new vector-pointer.  */
  tree memtag;
  struct ptr_info_def *ptr_info;
  subvar_t subvars;

  /* Alignment information.  */ 
  /* The offset of the data-reference from its base in bytes.  */
  tree misalignment;
  /* The maximum data-ref's alignment.  */
  tree aligned_to;

  /* The type of the data-ref.  */
  enum data_ref_type type;
};

typedef struct data_reference *data_reference_p;
DEF_VEC_P(data_reference_p);
DEF_VEC_ALLOC_P (data_reference_p, heap);

#define DR_STMT(DR)                (DR)->stmt
#define DR_REF(DR)                 (DR)->ref
#define DR_BASE_OBJECT(DR)         (DR)->object_info.base_object
#define DR_TYPE(DR)                (DR)->type
#define DR_ACCESS_FNS(DR)\
  (DR_TYPE(DR) == ARRAY_REF_TYPE ?  \
   (DR)->object_info.access_fns : (DR)->first_location.access_fns)
#define DR_ACCESS_FN(DR, I)        VEC_index (tree, DR_ACCESS_FNS (DR), I)
#define DR_NUM_DIMENSIONS(DR)      VEC_length (tree, DR_ACCESS_FNS (DR))  
#define DR_IS_READ(DR)             (DR)->is_read
#define DR_BASE_ADDRESS(DR)        (DR)->first_location.base_address
#define DR_OFFSET(DR)              (DR)->first_location.offset
#define DR_INIT(DR)                (DR)->first_location.init
#define DR_STEP(DR)                (DR)->first_location.step
#define DR_MEMTAG(DR)              (DR)->memtag
#define DR_ALIGNED_TO(DR)          (DR)->aligned_to
#define DR_OFFSET_MISALIGNMENT(DR) (DR)->misalignment
#define DR_PTR_INFO(DR)            (DR)->ptr_info
#define DR_SUBVARS(DR)             (DR)->subvars

#define DR_ACCESS_FNS_ADDR(DR)       \
  (DR_TYPE(DR) == ARRAY_REF_TYPE ?   \
   &((DR)->object_info.access_fns) : &((DR)->first_location.access_fns))
#define DR_SET_ACCESS_FNS(DR, ACC_FNS)         \
{                                              \
  if (DR_TYPE(DR) == ARRAY_REF_TYPE)           \
    (DR)->object_info.access_fns = ACC_FNS;    \
  else                                         \
    (DR)->first_location.access_fns = ACC_FNS; \
}
#define DR_FREE_ACCESS_FNS(DR)                              \
{                                                           \
  if (DR_TYPE(DR) == ARRAY_REF_TYPE)                        \
    VEC_free (tree, heap, (DR)->object_info.access_fns);    \
  else                                                      \
    VEC_free (tree, heap, (DR)->first_location.access_fns); \
}

enum data_dependence_direction {
  dir_positive, 
  dir_negative, 
  dir_equal, 
  dir_positive_or_negative,
  dir_positive_or_equal,
  dir_negative_or_equal,
  dir_star,
  dir_independent
};

/* What is a subscript?  Given two array accesses a subscript is the
   tuple composed of the access functions for a given dimension.
   Example: Given A[f1][f2][f3] and B[g1][g2][g3], there are three
   subscripts: (f1, g1), (f2, g2), (f3, g3).  These three subscripts
   are stored in the data_dependence_relation structure under the form
   of an array of subscripts.  */

struct subscript
{
  /* A description of the iterations for which the elements are
     accessed twice.  */
  tree conflicting_iterations_in_a;
  tree conflicting_iterations_in_b;
  
  /* This field stores the information about the iteration domain
     validity of the dependence relation.  */
  tree last_conflict;
  
  /* Distance from the iteration that access a conflicting element in
     A to the iteration that access this same conflicting element in
     B.  The distance is a tree scalar expression, i.e. a constant or a
     symbolic expression, but certainly not a chrec function.  */
  tree distance;
};

typedef struct subscript *subscript_p;
DEF_VEC_P(subscript_p);
DEF_VEC_ALLOC_P (subscript_p, heap);

#define SUB_CONFLICTS_IN_A(SUB) SUB->conflicting_iterations_in_a
#define SUB_CONFLICTS_IN_B(SUB) SUB->conflicting_iterations_in_b
#define SUB_LAST_CONFLICT(SUB) SUB->last_conflict
#define SUB_DISTANCE(SUB) SUB->distance

typedef struct loop *loop_p;
DEF_VEC_P(loop_p);
DEF_VEC_ALLOC_P (loop_p, heap);

/* A data_dependence_relation represents a relation between two
   data_references A and B.  */

struct data_dependence_relation
{
  
  struct data_reference *a;
  struct data_reference *b;

  /* When the dependence relation is affine, it can be represented by
     a distance vector.  */
  bool affine_p;

  /* A "yes/no/maybe" field for the dependence relation:
     
     - when "ARE_DEPENDENT == NULL_TREE", there exist a dependence
       relation between A and B, and the description of this relation
       is given in the SUBSCRIPTS array,
     
     - when "ARE_DEPENDENT == chrec_known", there is no dependence and
       SUBSCRIPTS is empty,
     
     - when "ARE_DEPENDENT == chrec_dont_know", there may be a dependence,
       but the analyzer cannot be more specific.  */
  tree are_dependent;
  
  /* For each subscript in the dependence test, there is an element in
     this array.  This is the attribute that labels the edge A->B of
     the data_dependence_relation.  */
  VEC (subscript_p, heap) *subscripts;

  /* The analyzed loop nest.  */
  VEC (loop_p, heap) *loop_nest;

  /* The classic direction vector.  */
  VEC (lambda_vector, heap) *dir_vects;

  /* The classic distance vector.  */
  VEC (lambda_vector, heap) *dist_vects;
};

typedef struct data_dependence_relation *ddr_p;
DEF_VEC_P(ddr_p);
DEF_VEC_ALLOC_P(ddr_p,heap);

#define DDR_A(DDR) DDR->a
#define DDR_B(DDR) DDR->b
#define DDR_AFFINE_P(DDR) DDR->affine_p
#define DDR_ARE_DEPENDENT(DDR) DDR->are_dependent
#define DDR_SUBSCRIPTS(DDR) DDR->subscripts
#define DDR_SUBSCRIPT(DDR, I) VEC_index (subscript_p, DDR_SUBSCRIPTS (DDR), I)
#define DDR_NUM_SUBSCRIPTS(DDR) VEC_length (subscript_p, DDR_SUBSCRIPTS (DDR))

#define DDR_LOOP_NEST(DDR) DDR->loop_nest
/* The size of the direction/distance vectors: the number of loops in
   the loop nest.  */
#define DDR_NB_LOOPS(DDR) (VEC_length (loop_p, DDR_LOOP_NEST (DDR)))

#define DDR_DIST_VECTS(DDR) ((DDR)->dist_vects)
#define DDR_DIR_VECTS(DDR) ((DDR)->dir_vects)
#define DDR_NUM_DIST_VECTS(DDR) \
  (VEC_length (lambda_vector, DDR_DIST_VECTS (DDR)))
#define DDR_NUM_DIR_VECTS(DDR) \
  (VEC_length (lambda_vector, DDR_DIR_VECTS (DDR)))
#define DDR_DIR_VECT(DDR, I) \
  VEC_index (lambda_vector, DDR_DIR_VECTS (DDR), I)
#define DDR_DIST_VECT(DDR, I) \
  VEC_index (lambda_vector, DDR_DIST_VECTS (DDR), I)



extern tree find_data_references_in_loop (struct loop *,
					  VEC (data_reference_p, heap) **);
extern void compute_data_dependences_for_loop (struct loop *, bool,
					       VEC (data_reference_p, heap) **,
					       VEC (ddr_p, heap) **);
extern void print_direction_vector (FILE *, lambda_vector, int);
extern void print_dir_vectors (FILE *, VEC (lambda_vector, heap) *, int);
extern void print_dist_vectors (FILE *, VEC (lambda_vector, heap) *, int);
extern void dump_subscript (FILE *, struct subscript *);
extern void dump_ddrs (FILE *, VEC (ddr_p, heap) *);
extern void dump_dist_dir_vectors (FILE *, VEC (ddr_p, heap) *);
extern void dump_data_reference (FILE *, struct data_reference *);
extern void dump_data_references (FILE *, VEC (data_reference_p, heap) *);
extern void debug_data_dependence_relation (struct data_dependence_relation *);
extern void dump_data_dependence_relation (FILE *, 
					   struct data_dependence_relation *);
extern void dump_data_dependence_relations (FILE *, VEC (ddr_p, heap) *);
extern void dump_data_dependence_direction (FILE *, 
					    enum data_dependence_direction);
extern void free_dependence_relation (struct data_dependence_relation *);
extern void free_dependence_relations (VEC (ddr_p, heap) *);
extern void free_data_refs (VEC (data_reference_p, heap) *);
extern struct data_reference *analyze_array (tree, tree, bool);
extern void estimate_iters_using_array (tree, tree);


/* Return the index of the variable VAR in the LOOP_NEST array.  */

static inline int
index_in_loop_nest (int var, VEC (loop_p, heap) *loop_nest)
{
  struct loop *loopi;
  int var_index;

  for (var_index = 0; VEC_iterate (loop_p, loop_nest, var_index, loopi);
       var_index++)
    if (loopi->num == var)
      break;

  return var_index;
}

/* In lambda-code.c  */
bool lambda_transform_legal_p (lambda_trans_matrix, int, VEC (ddr_p, heap) *);

#endif  /* GCC_TREE_DATA_REF_H  */
