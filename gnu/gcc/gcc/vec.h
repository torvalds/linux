/* Vector API for GNU compiler.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Nathan Sidwell <nathan@codesourcery.com>

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

#ifndef GCC_VEC_H
#define GCC_VEC_H

/* The macros here implement a set of templated vector types and
   associated interfaces.  These templates are implemented with
   macros, as we're not in C++ land.  The interface functions are
   typesafe and use static inline functions, sometimes backed by
   out-of-line generic functions.  The vectors are designed to
   interoperate with the GTY machinery.

   Because of the different behavior of structure objects, scalar
   objects and of pointers, there are three flavors, one for each of
   these variants.  Both the structure object and pointer variants
   pass pointers to objects around -- in the former case the pointers
   are stored into the vector and in the latter case the pointers are
   dereferenced and the objects copied into the vector.  The scalar
   object variant is suitable for int-like objects, and the vector
   elements are returned by value.

   There are both 'index' and 'iterate' accessors.  The iterator
   returns a boolean iteration condition and updates the iteration
   variable passed by reference.  Because the iterator will be
   inlined, the address-of can be optimized away.

   The vectors are implemented using the trailing array idiom, thus
   they are not resizeable without changing the address of the vector
   object itself.  This means you cannot have variables or fields of
   vector type -- always use a pointer to a vector.  The one exception
   is the final field of a structure, which could be a vector type.
   You will have to use the embedded_size & embedded_init calls to
   create such objects, and they will probably not be resizeable (so
   don't use the 'safe' allocation variants).  The trailing array
   idiom is used (rather than a pointer to an array of data), because,
   if we allow NULL to also represent an empty vector, empty vectors
   occupy minimal space in the structure containing them.

   Each operation that increases the number of active elements is
   available in 'quick' and 'safe' variants.  The former presumes that
   there is sufficient allocated space for the operation to succeed
   (it dies if there is not).  The latter will reallocate the
   vector, if needed.  Reallocation causes an exponential increase in
   vector size.  If you know you will be adding N elements, it would
   be more efficient to use the reserve operation before adding the
   elements with the 'quick' operation.  This will ensure there are at
   least as many elements as you ask for, it will exponentially
   increase if there are too few spare slots.  If you want reserve a
   specific number of slots, but do not want the exponential increase
   (for instance, you know this is the last allocation), use the
   reserve_exact operation.  You can also create a vector of a
   specific size from the get go.

   You should prefer the push and pop operations, as they append and
   remove from the end of the vector. If you need to remove several
   items in one go, use the truncate operation.  The insert and remove
   operations allow you to change elements in the middle of the
   vector.  There are two remove operations, one which preserves the
   element ordering 'ordered_remove', and one which does not
   'unordered_remove'.  The latter function copies the end element
   into the removed slot, rather than invoke a memmove operation.  The
   'lower_bound' function will determine where to place an item in the
   array using insert that will maintain sorted order.

   When a vector type is defined, first a non-memory managed version
   is created.  You can then define either or both garbage collected
   and heap allocated versions.  The allocation mechanism is specified
   when the type is defined, and is therefore part of the type.  If
   you need both gc'd and heap allocated versions, you still must have
   *exactly* one definition of the common non-memory managed base vector.
   
   If you need to directly manipulate a vector, then the 'address'
   accessor will return the address of the start of the vector.  Also
   the 'space' predicate will tell you whether there is spare capacity
   in the vector.  You will not normally need to use these two functions.
   
   Vector types are defined using a DEF_VEC_{O,P,I}(TYPEDEF) macro, to
   get the non-memory allocation version, and then a
   DEF_VEC_ALLOC_{O,P,I}(TYPEDEF,ALLOC) macro to get memory managed
   vectors.  Variables of vector type are declared using a
   VEC(TYPEDEF,ALLOC) macro.  The ALLOC argument specifies the
   allocation strategy, and can be either 'gc' or 'heap' for garbage
   collected and heap allocated respectively.  It can be 'none' to get
   a vector that must be explicitly allocated (for instance as a
   trailing array of another structure).  The characters O, P and I
   indicate whether TYPEDEF is a pointer (P), object (O) or integral
   (I) type.  Be careful to pick the correct one, as you'll get an
   awkward and inefficient API if you use the wrong one.  There is a
   check, which results in a compile-time warning, for the P and I
   versions, but there is no check for the O versions, as that is not
   possible in plain C.  Due to the way GTY works, you must annotate
   any structures you wish to insert or reference from a vector with a
   GTY(()) tag.  You need to do this even if you never declare the GC
   allocated variants.

   An example of their use would be,

   DEF_VEC_P(tree);   // non-managed tree vector.
   DEF_VEC_ALLOC_P(tree,gc);	// gc'd vector of tree pointers.  This must
   			        // appear at file scope.

   struct my_struct {
     VEC(tree,gc) *v;      // A (pointer to) a vector of tree pointers.
   };

   struct my_struct *s;

   if (VEC_length(tree,s->v)) { we have some contents }
   VEC_safe_push(tree,gc,s->v,decl); // append some decl onto the end
   for (ix = 0; VEC_iterate(tree,s->v,ix,elt); ix++)
     { do something with elt }

*/

/* Macros to invoke API calls.  A single macro works for both pointer
   and object vectors, but the argument and return types might well be
   different.  In each macro, T is the typedef of the vector elements,
   and A is the allocation strategy.  The allocation strategy is only
   present when it is required.  Some of these macros pass the vector,
   V, by reference (by taking its address), this is noted in the
   descriptions.  */

/* Length of vector
   unsigned VEC_T_length(const VEC(T) *v);

   Return the number of active elements in V.  V can be NULL, in which
   case zero is returned.  */

#define VEC_length(T,V)	(VEC_OP(T,base,length)(VEC_BASE(V)))


/* Check if vector is empty
   int VEC_T_empty(const VEC(T) *v);

   Return nonzero if V is an empty vector (or V is NULL), zero otherwise.  */

#define VEC_empty(T,V)	(VEC_length (T,V) == 0)


/* Get the final element of the vector.
   T VEC_T_last(VEC(T) *v); // Integer
   T VEC_T_last(VEC(T) *v); // Pointer
   T *VEC_T_last(VEC(T) *v); // Object

   Return the final element.  V must not be empty.  */

#define VEC_last(T,V)	(VEC_OP(T,base,last)(VEC_BASE(V) VEC_CHECK_INFO))

/* Index into vector
   T VEC_T_index(VEC(T) *v, unsigned ix); // Integer
   T VEC_T_index(VEC(T) *v, unsigned ix); // Pointer
   T *VEC_T_index(VEC(T) *v, unsigned ix); // Object

   Return the IX'th element.  If IX must be in the domain of V.  */

#define VEC_index(T,V,I) (VEC_OP(T,base,index)(VEC_BASE(V),I VEC_CHECK_INFO))

/* Iterate over vector
   int VEC_T_iterate(VEC(T) *v, unsigned ix, T &ptr); // Integer
   int VEC_T_iterate(VEC(T) *v, unsigned ix, T &ptr); // Pointer
   int VEC_T_iterate(VEC(T) *v, unsigned ix, T *&ptr); // Object

   Return iteration condition and update PTR to point to the IX'th
   element.  At the end of iteration, sets PTR to NULL.  Use this to
   iterate over the elements of a vector as follows,

     for (ix = 0; VEC_iterate(T,v,ix,ptr); ix++)
       continue;  */

#define VEC_iterate(T,V,I,P)	(VEC_OP(T,base,iterate)(VEC_BASE(V),I,&(P)))

/* Allocate new vector.
   VEC(T,A) *VEC_T_A_alloc(int reserve);

   Allocate a new vector with space for RESERVE objects.  If RESERVE
   is zero, NO vector is created.  */

#define VEC_alloc(T,A,N)	(VEC_OP(T,A,alloc)(N MEM_STAT_INFO))

/* Free a vector.
   void VEC_T_A_free(VEC(T,A) *&);

   Free a vector and set it to NULL.  */

#define VEC_free(T,A,V)	(VEC_OP(T,A,free)(&V))

/* Use these to determine the required size and initialization of a
   vector embedded within another structure (as the final member).
   
   size_t VEC_T_embedded_size(int reserve);
   void VEC_T_embedded_init(VEC(T) *v, int reserve);
   
   These allow the caller to perform the memory allocation.  */

#define VEC_embedded_size(T,N)	 (VEC_OP(T,base,embedded_size)(N))
#define VEC_embedded_init(T,O,N) (VEC_OP(T,base,embedded_init)(VEC_BASE(O),N))

/* Copy a vector.
   VEC(T,A) *VEC_T_A_copy(VEC(T) *);

   Copy the live elements of a vector into a new vector.  The new and
   old vectors need not be allocated by the same mechanism.  */

#define VEC_copy(T,A,V) (VEC_OP(T,A,copy)(VEC_BASE(V) MEM_STAT_INFO))

/* Determine if a vector has additional capacity.
   
   int VEC_T_space (VEC(T) *v,int reserve)

   If V has space for RESERVE additional entries, return nonzero.  You
   usually only need to use this if you are doing your own vector
   reallocation, for instance on an embedded vector.  This returns
   nonzero in exactly the same circumstances that VEC_T_reserve
   will.  */

#define VEC_space(T,V,R) \
	(VEC_OP(T,base,space)(VEC_BASE(V),R VEC_CHECK_INFO))

/* Reserve space.
   int VEC_T_A_reserve(VEC(T,A) *&v, int reserve);

   Ensure that V has at least RESERVE slots available.  This will
   create additional headroom.  Note this can cause V to be
   reallocated.  Returns nonzero iff reallocation actually
   occurred.  */

#define VEC_reserve(T,A,V,R)	\
	(VEC_OP(T,A,reserve)(&(V),R VEC_CHECK_INFO MEM_STAT_INFO))

/* Reserve space exactly.
   int VEC_T_A_reserve_exact(VEC(T,A) *&v, int reserve);

   Ensure that V has at least RESERVE slots available.  This will not
   create additional headroom.  Note this can cause V to be
   reallocated.  Returns nonzero iff reallocation actually
   occurred.  */

#define VEC_reserve_exact(T,A,V,R)	\
	(VEC_OP(T,A,reserve_exact)(&(V),R VEC_CHECK_INFO MEM_STAT_INFO))

/* Push object with no reallocation
   T *VEC_T_quick_push (VEC(T) *v, T obj); // Integer
   T *VEC_T_quick_push (VEC(T) *v, T obj); // Pointer
   T *VEC_T_quick_push (VEC(T) *v, T *obj); // Object
   
   Push a new element onto the end, returns a pointer to the slot
   filled in. For object vectors, the new value can be NULL, in which
   case NO initialization is performed.  There must
   be sufficient space in the vector.  */

#define VEC_quick_push(T,V,O)	\
	(VEC_OP(T,base,quick_push)(VEC_BASE(V),O VEC_CHECK_INFO))

/* Push object with reallocation
   T *VEC_T_A_safe_push (VEC(T,A) *&v, T obj); // Integer
   T *VEC_T_A_safe_push (VEC(T,A) *&v, T obj); // Pointer
   T *VEC_T_A_safe_push (VEC(T,A) *&v, T *obj); // Object
   
   Push a new element onto the end, returns a pointer to the slot
   filled in. For object vectors, the new value can be NULL, in which
   case NO initialization is performed.  Reallocates V, if needed.  */

#define VEC_safe_push(T,A,V,O)		\
	(VEC_OP(T,A,safe_push)(&(V),O VEC_CHECK_INFO MEM_STAT_INFO))

/* Pop element off end
   T VEC_T_pop (VEC(T) *v);		// Integer
   T VEC_T_pop (VEC(T) *v);		// Pointer
   void VEC_T_pop (VEC(T) *v);		// Object

   Pop the last element off the end. Returns the element popped, for
   pointer vectors.  */

#define VEC_pop(T,V)	(VEC_OP(T,base,pop)(VEC_BASE(V) VEC_CHECK_INFO))

/* Truncate to specific length
   void VEC_T_truncate (VEC(T) *v, unsigned len);
   
   Set the length as specified.  The new length must be less than or
   equal to the current length.  This is an O(1) operation.  */

#define VEC_truncate(T,V,I)		\
	(VEC_OP(T,base,truncate)(VEC_BASE(V),I VEC_CHECK_INFO))

/* Grow to a specific length.
   void VEC_T_A_safe_grow (VEC(T,A) *&v, int len);

   Grow the vector to a specific length.  The LEN must be as
   long or longer than the current length.  The new elements are
   uninitialized.  */

#define VEC_safe_grow(T,A,V,I)		\
	(VEC_OP(T,A,safe_grow)(&(V),I VEC_CHECK_INFO MEM_STAT_INFO))

/* Replace element
   T VEC_T_replace (VEC(T) *v, unsigned ix, T val); // Integer
   T VEC_T_replace (VEC(T) *v, unsigned ix, T val); // Pointer
   T *VEC_T_replace (VEC(T) *v, unsigned ix, T *val);  // Object
   
   Replace the IXth element of V with a new value, VAL.  For pointer
   vectors returns the original value. For object vectors returns a
   pointer to the new value.  For object vectors the new value can be
   NULL, in which case no overwriting of the slot is actually
   performed.  */

#define VEC_replace(T,V,I,O)		\
	(VEC_OP(T,base,replace)(VEC_BASE(V),I,O VEC_CHECK_INFO))

/* Insert object with no reallocation
   T *VEC_T_quick_insert (VEC(T) *v, unsigned ix, T val); // Integer
   T *VEC_T_quick_insert (VEC(T) *v, unsigned ix, T val); // Pointer
   T *VEC_T_quick_insert (VEC(T) *v, unsigned ix, T *val); // Object
   
   Insert an element, VAL, at the IXth position of V. Return a pointer
   to the slot created.  For vectors of object, the new value can be
   NULL, in which case no initialization of the inserted slot takes
   place. There must be sufficient space.  */

#define VEC_quick_insert(T,V,I,O)	\
	(VEC_OP(T,base,quick_insert)(VEC_BASE(V),I,O VEC_CHECK_INFO))

/* Insert object with reallocation
   T *VEC_T_A_safe_insert (VEC(T,A) *&v, unsigned ix, T val); // Integer
   T *VEC_T_A_safe_insert (VEC(T,A) *&v, unsigned ix, T val); // Pointer
   T *VEC_T_A_safe_insert (VEC(T,A) *&v, unsigned ix, T *val); // Object
   
   Insert an element, VAL, at the IXth position of V. Return a pointer
   to the slot created.  For vectors of object, the new value can be
   NULL, in which case no initialization of the inserted slot takes
   place. Reallocate V, if necessary.  */

#define VEC_safe_insert(T,A,V,I,O)	\
	(VEC_OP(T,A,safe_insert)(&(V),I,O VEC_CHECK_INFO MEM_STAT_INFO))
     
/* Remove element retaining order
   T VEC_T_ordered_remove (VEC(T) *v, unsigned ix); // Integer
   T VEC_T_ordered_remove (VEC(T) *v, unsigned ix); // Pointer
   void VEC_T_ordered_remove (VEC(T) *v, unsigned ix); // Object
   
   Remove an element from the IXth position of V. Ordering of
   remaining elements is preserved.  For pointer vectors returns the
   removed object.  This is an O(N) operation due to a memmove.  */

#define VEC_ordered_remove(T,V,I)	\
	(VEC_OP(T,base,ordered_remove)(VEC_BASE(V),I VEC_CHECK_INFO))

/* Remove element destroying order
   T VEC_T_unordered_remove (VEC(T) *v, unsigned ix); // Integer
   T VEC_T_unordered_remove (VEC(T) *v, unsigned ix); // Pointer
   void VEC_T_unordered_remove (VEC(T) *v, unsigned ix); // Object
   
   Remove an element from the IXth position of V. Ordering of
   remaining elements is destroyed.  For pointer vectors returns the
   removed object.  This is an O(1) operation.  */

#define VEC_unordered_remove(T,V,I)	\
	(VEC_OP(T,base,unordered_remove)(VEC_BASE(V),I VEC_CHECK_INFO))

/* Remove a block of elements
   void VEC_T_block_remove (VEC(T) *v, unsigned ix, unsigned len);
   
   Remove LEN elements starting at the IXth.  Ordering is retained.
   This is an O(1) operation.  */

#define VEC_block_remove(T,V,I,L)	\
	(VEC_OP(T,base,block_remove)(VEC_BASE(V),I,L VEC_CHECK_INFO))

/* Get the address of the array of elements
   T *VEC_T_address (VEC(T) v)

   If you need to directly manipulate the array (for instance, you
   want to feed it to qsort), use this accessor.  */

#define VEC_address(T,V)		(VEC_OP(T,base,address)(VEC_BASE(V)))

/* Find the first index in the vector not less than the object.
   unsigned VEC_T_lower_bound (VEC(T) *v, const T val, 
                               bool (*lessthan) (const T, const T)); // Integer
   unsigned VEC_T_lower_bound (VEC(T) *v, const T val, 
                               bool (*lessthan) (const T, const T)); // Pointer
   unsigned VEC_T_lower_bound (VEC(T) *v, const T *val,
                               bool (*lessthan) (const T*, const T*)); // Object
   
   Find the first position in which VAL could be inserted without
   changing the ordering of V.  LESSTHAN is a function that returns
   true if the first argument is strictly less than the second.  */
   
#define VEC_lower_bound(T,V,O,LT)    \
       (VEC_OP(T,base,lower_bound)(VEC_BASE(V),O,LT VEC_CHECK_INFO))

#if !IN_GENGTYPE
/* Reallocate an array of elements with prefix.  */
extern void *vec_gc_p_reserve (void *, int MEM_STAT_DECL);
extern void *vec_gc_p_reserve_exact (void *, int MEM_STAT_DECL);
extern void *vec_gc_o_reserve (void *, int, size_t, size_t MEM_STAT_DECL);
extern void *vec_gc_o_reserve_exact (void *, int, size_t, size_t
				     MEM_STAT_DECL);
extern void ggc_free (void *);
#define vec_gc_free(V) ggc_free (V)
extern void *vec_heap_p_reserve (void *, int MEM_STAT_DECL);
extern void *vec_heap_p_reserve_exact (void *, int MEM_STAT_DECL);
extern void *vec_heap_o_reserve (void *, int, size_t, size_t MEM_STAT_DECL);
extern void *vec_heap_o_reserve_exact (void *, int, size_t, size_t
				       MEM_STAT_DECL);
#define vec_heap_free(V) free (V)

#if ENABLE_CHECKING
#define VEC_CHECK_INFO ,__FILE__,__LINE__,__FUNCTION__
#define VEC_CHECK_DECL ,const char *file_,unsigned line_,const char *function_
#define VEC_CHECK_PASS ,file_,line_,function_
     
#define VEC_ASSERT(EXPR,OP,T,A) \
  (void)((EXPR) ? 0 : (VEC_ASSERT_FAIL(OP,VEC(T,A)), 0))

extern void vec_assert_fail (const char *, const char * VEC_CHECK_DECL)
     ATTRIBUTE_NORETURN;
#define VEC_ASSERT_FAIL(OP,VEC) vec_assert_fail (OP,#VEC VEC_CHECK_PASS)
#else
#define VEC_CHECK_INFO
#define VEC_CHECK_DECL
#define VEC_CHECK_PASS
#define VEC_ASSERT(EXPR,OP,T,A) (void)(EXPR)
#endif

#define VEC(T,A) VEC_##T##_##A
#define VEC_OP(T,A,OP) VEC_##T##_##A##_##OP
#else  /* IN_GENGTYPE */
#define VEC(T,A) VEC_ T _ A
#define VEC_STRINGIFY(X) VEC_STRINGIFY_(X)
#define VEC_STRINGIFY_(X) #X
#undef GTY
#endif /* IN_GENGTYPE */

/* Base of vector type, not user visible.  */     
#define VEC_T(T,B)							  \
typedef struct VEC(T,B) 				 		  \
{									  \
  unsigned num;								  \
  unsigned alloc;							  \
  T vec[1];								  \
} VEC(T,B)

#define VEC_T_GTY(T,B)							  \
typedef struct VEC(T,B) GTY(())				 		  \
{									  \
  unsigned num;								  \
  unsigned alloc;							  \
  T GTY ((length ("%h.num"))) vec[1];					  \
} VEC(T,B)

/* Derived vector type, user visible.  */
#define VEC_TA_GTY(T,B,A,GTY)						  \
typedef struct VEC(T,A) GTY						  \
{									  \
  VEC(T,B) base;							  \
} VEC(T,A)

/* Convert to base type.  */
#define VEC_BASE(P)  ((P) ? &(P)->base : 0)

/* Vector of integer-like object.  */
#if IN_GENGTYPE
{"DEF_VEC_I", VEC_STRINGIFY (VEC_T(#0,#1)) ";", "none"},
{"DEF_VEC_ALLOC_I", VEC_STRINGIFY (VEC_TA (#0,#1,#2,#3)) ";", NULL},
#else
#define DEF_VEC_I(T)							  \
static inline void VEC_OP (T,must_be,integral_type) (void) 		  \
{									  \
  (void)~(T)0;								  \
}									  \
									  \
VEC_T(T,base);								  \
VEC_TA_GTY(T,base,none,);						  \
DEF_VEC_FUNC_P(T)							  \
struct vec_swallow_trailing_semi
#define DEF_VEC_ALLOC_I(T,A)						  \
VEC_TA_GTY(T,base,A,);							  \
DEF_VEC_ALLOC_FUNC_I(T,A)						  \
struct vec_swallow_trailing_semi
#endif

/* Vector of pointer to object.  */
#if IN_GENGTYPE
{"DEF_VEC_P", VEC_STRINGIFY (VEC_T_GTY(#0,#1)) ";", "none"},
{"DEF_VEC_ALLOC_P", VEC_STRINGIFY (VEC_TA_GTY (#0,#1,#2,#3)) ";", NULL},
#else
#define DEF_VEC_P(T) 							  \
static inline void VEC_OP (T,must_be,pointer_type) (void) 		  \
{									  \
  (void)((T)1 == (void *)1);						  \
}									  \
									  \
VEC_T_GTY(T,base);							  \
VEC_TA_GTY(T,base,none,);						  \
DEF_VEC_FUNC_P(T)							  \
struct vec_swallow_trailing_semi
#define DEF_VEC_ALLOC_P(T,A)						  \
VEC_TA_GTY(T,base,A,);							  \
DEF_VEC_ALLOC_FUNC_P(T,A)						  \
struct vec_swallow_trailing_semi
#endif

#define DEF_VEC_FUNC_P(T)						  \
static inline unsigned VEC_OP (T,base,length) (const VEC(T,base) *vec_)   \
{									  \
  return vec_ ? vec_->num : 0;						  \
}									  \
									  \
static inline T VEC_OP (T,base,last)					  \
     (const VEC(T,base) *vec_ VEC_CHECK_DECL)				  \
{									  \
  VEC_ASSERT (vec_ && vec_->num, "last", T, base);			  \
  									  \
  return vec_->vec[vec_->num - 1];					  \
}									  \
									  \
static inline T VEC_OP (T,base,index)					  \
     (const VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)		  \
{									  \
  VEC_ASSERT (vec_ && ix_ < vec_->num, "index", T, base);		  \
  									  \
  return vec_->vec[ix_];						  \
}									  \
									  \
static inline int VEC_OP (T,base,iterate)			  	  \
     (const VEC(T,base) *vec_, unsigned ix_, T *ptr)			  \
{									  \
  if (vec_ && ix_ < vec_->num)						  \
    {									  \
      *ptr = vec_->vec[ix_];						  \
      return 1;								  \
    }									  \
  else									  \
    {									  \
      *ptr = 0;								  \
      return 0;								  \
    }									  \
}									  \
									  \
static inline size_t VEC_OP (T,base,embedded_size)			  \
     (int alloc_)							  \
{									  \
  return offsetof (VEC(T,base),vec) + alloc_ * sizeof(T);		  \
}									  \
									  \
static inline void VEC_OP (T,base,embedded_init)			  \
     (VEC(T,base) *vec_, int alloc_)					  \
{									  \
  vec_->num = 0;							  \
  vec_->alloc = alloc_;							  \
}									  \
									  \
static inline int VEC_OP (T,base,space)	       				  \
     (VEC(T,base) *vec_, int alloc_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (alloc_ >= 0, "space", T, base);				  \
  return vec_ ? vec_->alloc - vec_->num >= (unsigned)alloc_ : !alloc_;	  \
}									  \
									  \
static inline T *VEC_OP (T,base,quick_push)				  \
     (VEC(T,base) *vec_, T obj_ VEC_CHECK_DECL)				  \
{									  \
  T *slot_;								  \
  									  \
  VEC_ASSERT (vec_->num < vec_->alloc, "push", T, base);		  \
  slot_ = &vec_->vec[vec_->num++];					  \
  *slot_ = obj_;							  \
  									  \
  return slot_;								  \
}									  \
									  \
static inline T VEC_OP (T,base,pop) (VEC(T,base) *vec_ VEC_CHECK_DECL)	  \
{									  \
  T obj_;								  \
									  \
  VEC_ASSERT (vec_->num, "pop", T, base);				  \
  obj_ = vec_->vec[--vec_->num];					  \
									  \
  return obj_;								  \
}									  \
									  \
static inline void VEC_OP (T,base,truncate)				  \
     (VEC(T,base) *vec_, unsigned size_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (vec_ ? vec_->num >= size_ : !size_, "truncate", T, base);	  \
  if (vec_)								  \
    vec_->num = size_;							  \
}									  \
									  \
static inline T VEC_OP (T,base,replace)		  	     		  \
     (VEC(T,base) *vec_, unsigned ix_, T obj_ VEC_CHECK_DECL)		  \
{									  \
  T old_obj_;								  \
									  \
  VEC_ASSERT (ix_ < vec_->num, "replace", T, base);			  \
  old_obj_ = vec_->vec[ix_];						  \
  vec_->vec[ix_] = obj_;						  \
									  \
  return old_obj_;							  \
}									  \
									  \
static inline T *VEC_OP (T,base,quick_insert)				  \
     (VEC(T,base) *vec_, unsigned ix_, T obj_ VEC_CHECK_DECL)		  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (vec_->num < vec_->alloc, "insert", T, base);		  \
  VEC_ASSERT (ix_ <= vec_->num, "insert", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  memmove (slot_ + 1, slot_, (vec_->num++ - ix_) * sizeof (T));		  \
  *slot_ = obj_;							  \
  									  \
  return slot_;								  \
}									  \
									  \
static inline T VEC_OP (T,base,ordered_remove)				  \
     (VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)			  \
{									  \
  T *slot_;								  \
  T obj_;								  \
									  \
  VEC_ASSERT (ix_ < vec_->num, "remove", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  obj_ = *slot_;							  \
  memmove (slot_, slot_ + 1, (--vec_->num - ix_) * sizeof (T));     	  \
									  \
  return obj_;								  \
}									  \
									  \
static inline T VEC_OP (T,base,unordered_remove)			  \
     (VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)			  \
{									  \
  T *slot_;								  \
  T obj_;								  \
									  \
  VEC_ASSERT (ix_ < vec_->num, "remove", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  obj_ = *slot_;							  \
  *slot_ = vec_->vec[--vec_->num];					  \
									  \
  return obj_;								  \
}									  \
									  \
static inline void VEC_OP (T,base,block_remove)				  \
     (VEC(T,base) *vec_, unsigned ix_, unsigned len_ VEC_CHECK_DECL)	  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (ix_ + len_ <= vec_->num, "block_remove", T, base);	  \
  slot_ = &vec_->vec[ix_];						  \
  vec_->num -= len_;							  \
  memmove (slot_, slot_ + len_, (vec_->num - ix_) * sizeof (T));	  \
}									  \
									  \
static inline T *VEC_OP (T,base,address)				  \
     (VEC(T,base) *vec_)						  \
{									  \
  return vec_ ? vec_->vec : 0;						  \
}									  \
									  \
static inline unsigned VEC_OP (T,base,lower_bound)			  \
     (VEC(T,base) *vec_, const T obj_,					  \
      bool (*lessthan_)(const T, const T) VEC_CHECK_DECL)		  \
{									  \
   unsigned int len_ = VEC_OP (T,base, length) (vec_);			  \
   unsigned int half_, middle_;						  \
   unsigned int first_ = 0;						  \
   while (len_ > 0)							  \
     {									  \
        T middle_elem_;							  \
        half_ = len_ >> 1;						  \
        middle_ = first_;						  \
        middle_ += half_;						  \
        middle_elem_ = VEC_OP (T,base,index) (vec_, middle_ VEC_CHECK_PASS); \
        if (lessthan_ (middle_elem_, obj_))				  \
          {								  \
             first_ = middle_;						  \
             ++first_;							  \
             len_ = len_ - half_ - 1;					  \
          }								  \
        else								  \
          len_ = half_;							  \
     }									  \
   return first_;							  \
}

#define DEF_VEC_ALLOC_FUNC_P(T,A)					  \
static inline VEC(T,A) *VEC_OP (T,A,alloc)				  \
     (int alloc_ MEM_STAT_DECL)						  \
{									  \
  return (VEC(T,A) *) vec_##A##_p_reserve_exact (NULL, alloc_		  \
						 PASS_MEM_STAT);	  \
}									  \
									  \
static inline void VEC_OP (T,A,free)					  \
     (VEC(T,A) **vec_)							  \
{									  \
  if (*vec_)								  \
    vec_##A##_free (*vec_);						  \
  *vec_ = NULL;								  \
}									  \
									  \
static inline VEC(T,A) *VEC_OP (T,A,copy) (VEC(T,base) *vec_ MEM_STAT_DECL) \
{									  \
  size_t len_ = vec_ ? vec_->num : 0;					  \
  VEC (T,A) *new_vec_ = NULL;						  \
									  \
  if (len_)								  \
    {									  \
      new_vec_ = (VEC (T,A) *)(vec_##A##_p_reserve_exact		  \
			       (NULL, len_ PASS_MEM_STAT));		  \
									  \
      new_vec_->base.num = len_;					  \
      memcpy (new_vec_->base.vec, vec_->vec, sizeof (T) * len_);	  \
    }									  \
  return new_vec_;							  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve)	       				  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
		  							  \
  if (extend)	  							  \
    *vec_ = (VEC(T,A) *) vec_##A##_p_reserve (*vec_, alloc_ PASS_MEM_STAT); \
		  							  \
  return extend;							  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve_exact)  				  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
		  							  \
  if (extend)	  							  \
    *vec_ = (VEC(T,A) *) vec_##A##_p_reserve_exact (*vec_, alloc_	  \
						    PASS_MEM_STAT);	  \
		  							  \
  return extend;							  \
}									  \
									  \
static inline void VEC_OP (T,A,safe_grow)				  \
     (VEC(T,A) **vec_, int size_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  VEC_ASSERT (size_ >= 0						  \
	      && VEC_OP(T,base,length) VEC_BASE(*vec_) <= (unsigned)size_, \
						 "grow", T, A);		  \
  VEC_OP (T,A,reserve_exact) (vec_,					  \
			      size_ - (int)(*vec_ ? VEC_BASE(*vec_)->num : 0) \
			      VEC_CHECK_PASS PASS_MEM_STAT);		  \
  VEC_BASE (*vec_)->num = size_;					  \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_push)					  \
     (VEC(T,A) **vec_, T obj_ VEC_CHECK_DECL MEM_STAT_DECL)       	  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_push) (VEC_BASE(*vec_), obj_ VEC_CHECK_PASS); \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_insert)		     	  	  \
     (VEC(T,A) **vec_, unsigned ix_, T obj_ VEC_CHECK_DECL MEM_STAT_DECL)  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_insert) (VEC_BASE(*vec_), ix_, obj_	  \
 				       VEC_CHECK_PASS);			  \
}

/* Vector of object.  */
#if IN_GENGTYPE
{"DEF_VEC_O", VEC_STRINGIFY (VEC_T_GTY(#0,#1)) ";", "none"},
{"DEF_VEC_ALLOC_O", VEC_STRINGIFY (VEC_TA_GTY(#0,#1,#2,#3)) ";", NULL},
#else
#define DEF_VEC_O(T)							  \
VEC_T_GTY(T,base);							  \
VEC_TA_GTY(T,base,none,);						  \
DEF_VEC_FUNC_O(T)							  \
struct vec_swallow_trailing_semi
#define DEF_VEC_ALLOC_O(T,A)						  \
VEC_TA_GTY(T,base,A,);							  \
DEF_VEC_ALLOC_FUNC_O(T,A)						  \
struct vec_swallow_trailing_semi
#endif

#define DEF_VEC_FUNC_O(T)						  \
static inline unsigned VEC_OP (T,base,length) (const VEC(T,base) *vec_)	  \
{									  \
  return vec_ ? vec_->num : 0;						  \
}									  \
									  \
static inline T *VEC_OP (T,base,last) (VEC(T,base) *vec_ VEC_CHECK_DECL)  \
{									  \
  VEC_ASSERT (vec_ && vec_->num, "last", T, base);			  \
  									  \
  return &vec_->vec[vec_->num - 1];					  \
}									  \
									  \
static inline T *VEC_OP (T,base,index)					  \
     (VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (vec_ && ix_ < vec_->num, "index", T, base);		  \
  									  \
  return &vec_->vec[ix_];						  \
}									  \
									  \
static inline int VEC_OP (T,base,iterate)			     	  \
     (VEC(T,base) *vec_, unsigned ix_, T **ptr)				  \
{									  \
  if (vec_ && ix_ < vec_->num)						  \
    {									  \
      *ptr = &vec_->vec[ix_];						  \
      return 1;								  \
    }									  \
  else									  \
    {									  \
      *ptr = 0;								  \
      return 0;								  \
    }									  \
}									  \
									  \
static inline size_t VEC_OP (T,base,embedded_size)			  \
     (int alloc_)							  \
{									  \
  return offsetof (VEC(T,base),vec) + alloc_ * sizeof(T);		  \
}									  \
									  \
static inline void VEC_OP (T,base,embedded_init)			  \
     (VEC(T,base) *vec_, int alloc_)					  \
{									  \
  vec_->num = 0;							  \
  vec_->alloc = alloc_;							  \
}									  \
									  \
static inline int VEC_OP (T,base,space)	       				  \
     (VEC(T,base) *vec_, int alloc_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (alloc_ >= 0, "space", T, base);				  \
  return vec_ ? vec_->alloc - vec_->num >= (unsigned)alloc_ : !alloc_;	  \
}									  \
									  \
static inline T *VEC_OP (T,base,quick_push)				  \
     (VEC(T,base) *vec_, const T *obj_ VEC_CHECK_DECL)			  \
{									  \
  T *slot_;								  \
  									  \
  VEC_ASSERT (vec_->num < vec_->alloc, "push", T, base);		  \
  slot_ = &vec_->vec[vec_->num++];					  \
  if (obj_)								  \
    *slot_ = *obj_;							  \
  									  \
  return slot_;								  \
}									  \
									  \
static inline void VEC_OP (T,base,pop) (VEC(T,base) *vec_ VEC_CHECK_DECL) \
{									  \
  VEC_ASSERT (vec_->num, "pop", T, base);				  \
  --vec_->num;								  \
}									  \
									  \
static inline void VEC_OP (T,base,truncate)				  \
     (VEC(T,base) *vec_, unsigned size_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (vec_ ? vec_->num >= size_ : !size_, "truncate", T, base);	  \
  if (vec_)								  \
    vec_->num = size_;							  \
}									  \
									  \
static inline T *VEC_OP (T,base,replace)				  \
     (VEC(T,base) *vec_, unsigned ix_, const T *obj_ VEC_CHECK_DECL)	  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (ix_ < vec_->num, "replace", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  if (obj_)								  \
    *slot_ = *obj_;							  \
									  \
  return slot_;								  \
}									  \
									  \
static inline T *VEC_OP (T,base,quick_insert)				  \
     (VEC(T,base) *vec_, unsigned ix_, const T *obj_ VEC_CHECK_DECL)	  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (vec_->num < vec_->alloc, "insert", T, base);		  \
  VEC_ASSERT (ix_ <= vec_->num, "insert", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  memmove (slot_ + 1, slot_, (vec_->num++ - ix_) * sizeof (T));		  \
  if (obj_)								  \
    *slot_ = *obj_;							  \
  									  \
  return slot_;								  \
}									  \
									  \
static inline void VEC_OP (T,base,ordered_remove)			  \
     (VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)			  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (ix_ < vec_->num, "remove", T, base);			  \
  slot_ = &vec_->vec[ix_];						  \
  memmove (slot_, slot_ + 1, (--vec_->num - ix_) * sizeof (T));		  \
}									  \
									  \
static inline void VEC_OP (T,base,unordered_remove)			  \
     (VEC(T,base) *vec_, unsigned ix_ VEC_CHECK_DECL)			  \
{									  \
  VEC_ASSERT (ix_ < vec_->num, "remove", T, base);			  \
  vec_->vec[ix_] = vec_->vec[--vec_->num];				  \
}									  \
									  \
static inline void VEC_OP (T,base,block_remove)				  \
     (VEC(T,base) *vec_, unsigned ix_, unsigned len_ VEC_CHECK_DECL)	  \
{									  \
  T *slot_;								  \
									  \
  VEC_ASSERT (ix_ + len_ <= vec_->num, "block_remove", T, base);	  \
  slot_ = &vec_->vec[ix_];						  \
  vec_->num -= len_;							  \
  memmove (slot_, slot_ + len_, (vec_->num - ix_) * sizeof (T));	  \
}									  \
									  \
static inline T *VEC_OP (T,base,address)				  \
     (VEC(T,base) *vec_)						  \
{									  \
  return vec_ ? vec_->vec : 0;						  \
}									  \
									  \
static inline unsigned VEC_OP (T,base,lower_bound)			  \
     (VEC(T,base) *vec_, const T *obj_,					  \
      bool (*lessthan_)(const T *, const T *) VEC_CHECK_DECL)		  \
{									  \
   unsigned int len_ = VEC_OP (T, base, length) (vec_);			  \
   unsigned int half_, middle_;						  \
   unsigned int first_ = 0;						  \
   while (len_ > 0)							  \
     {									  \
        T *middle_elem_;						  \
        half_ = len_ >> 1;						  \
        middle_ = first_;						  \
        middle_ += half_;						  \
        middle_elem_ = VEC_OP (T,base,index) (vec_, middle_ VEC_CHECK_PASS); \
        if (lessthan_ (middle_elem_, obj_))				  \
          {								  \
             first_ = middle_;						  \
             ++first_;							  \
             len_ = len_ - half_ - 1;					  \
          }								  \
        else								  \
          len_ = half_;							  \
     }									  \
   return first_;							  \
}

#define DEF_VEC_ALLOC_FUNC_O(T,A)					  \
static inline VEC(T,A) *VEC_OP (T,A,alloc)      			  \
     (int alloc_ MEM_STAT_DECL)						  \
{									  \
  return (VEC(T,A) *) vec_##A##_o_reserve_exact (NULL, alloc_,		  \
						 offsetof (VEC(T,A),base.vec), \
						 sizeof (T)		  \
						 PASS_MEM_STAT);	  \
}									  \
									  \
static inline VEC(T,A) *VEC_OP (T,A,copy) (VEC(T,base) *vec_ MEM_STAT_DECL) \
{									  \
  size_t len_ = vec_ ? vec_->num : 0;					  \
  VEC (T,A) *new_vec_ = NULL;						  \
									  \
  if (len_)								  \
    {									  \
      new_vec_ = (VEC (T,A) *)(vec_##A##_o_reserve_exact		  \
			       (NULL, len_,				  \
				offsetof (VEC(T,A),base.vec), sizeof (T)  \
				PASS_MEM_STAT));			  \
									  \
      new_vec_->base.num = len_;					  \
      memcpy (new_vec_->base.vec, vec_->vec, sizeof (T) * len_);	  \
    }									  \
  return new_vec_;							  \
}									  \
									  \
static inline void VEC_OP (T,A,free)					  \
     (VEC(T,A) **vec_)							  \
{									  \
  if (*vec_)								  \
    vec_##A##_free (*vec_);						  \
  *vec_ = NULL;								  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve)	   	    			  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
									  \
  if (extend)								  \
    *vec_ = (VEC(T,A) *) vec_##A##_o_reserve (*vec_, alloc_,		  \
			   		      offsetof (VEC(T,A),base.vec),\
 					      sizeof (T)		  \
			   		      PASS_MEM_STAT);		  \
									  \
  return extend;							  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve_exact)   	    			  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
									  \
  if (extend)								  \
    *vec_ = (VEC(T,A) *) vec_##A##_o_reserve_exact			  \
			 (*vec_, alloc_,				  \
			  offsetof (VEC(T,A),base.vec),			  \
			  sizeof (T) PASS_MEM_STAT);			  \
									  \
  return extend;							  \
}									  \
									  \
static inline void VEC_OP (T,A,safe_grow)				  \
     (VEC(T,A) **vec_, int size_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  VEC_ASSERT (size_ >= 0						  \
	      && VEC_OP(T,base,length) VEC_BASE(*vec_) <= (unsigned)size_, \
						 "grow", T, A);		  \
  VEC_OP (T,A,reserve_exact) (vec_,					  \
			      size_ - (int)(*vec_ ? VEC_BASE(*vec_)->num : 0) \
			      VEC_CHECK_PASS PASS_MEM_STAT);		  \
  VEC_BASE (*vec_)->num = size_;					  \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_push)					  \
     (VEC(T,A) **vec_, const T *obj_ VEC_CHECK_DECL MEM_STAT_DECL)	  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_push) (VEC_BASE(*vec_), obj_ VEC_CHECK_PASS);  \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_insert)		     	  	  \
     (VEC(T,A) **vec_, unsigned ix_, const T *obj_			  \
 		VEC_CHECK_DECL MEM_STAT_DECL)				  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_insert) (VEC_BASE(*vec_), ix_, obj_	  \
				       VEC_CHECK_PASS);			  \
}

#define DEF_VEC_ALLOC_FUNC_I(T,A)					  \
static inline VEC(T,A) *VEC_OP (T,A,alloc)      			  \
     (int alloc_ MEM_STAT_DECL)						  \
{									  \
  return (VEC(T,A) *) vec_##A##_o_reserve_exact				  \
		      (NULL, alloc_, offsetof (VEC(T,A),base.vec),	  \
		       sizeof (T) PASS_MEM_STAT);			  \
}									  \
									  \
static inline VEC(T,A) *VEC_OP (T,A,copy) (VEC(T,base) *vec_ MEM_STAT_DECL) \
{									  \
  size_t len_ = vec_ ? vec_->num : 0;					  \
  VEC (T,A) *new_vec_ = NULL;						  \
									  \
  if (len_)								  \
    {									  \
      new_vec_ = (VEC (T,A) *)(vec_##A##_o_reserve_exact		  \
			       (NULL, len_,				  \
				offsetof (VEC(T,A),base.vec), sizeof (T)  \
				PASS_MEM_STAT));			  \
									  \
      new_vec_->base.num = len_;					  \
      memcpy (new_vec_->base.vec, vec_->vec, sizeof (T) * len_);	  \
    }									  \
  return new_vec_;							  \
}									  \
									  \
static inline void VEC_OP (T,A,free)					  \
     (VEC(T,A) **vec_)							  \
{									  \
  if (*vec_)								  \
    vec_##A##_free (*vec_);						  \
  *vec_ = NULL;								  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve)	   	    			  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
									  \
  if (extend)								  \
    *vec_ = (VEC(T,A) *) vec_##A##_o_reserve (*vec_, alloc_,		  \
			   		      offsetof (VEC(T,A),base.vec),\
 					      sizeof (T)		  \
			   		      PASS_MEM_STAT);		  \
									  \
  return extend;							  \
}									  \
									  \
static inline int VEC_OP (T,A,reserve_exact)   	    			  \
     (VEC(T,A) **vec_, int alloc_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  int extend = !VEC_OP (T,base,space) (VEC_BASE(*vec_), alloc_		  \
				       VEC_CHECK_PASS);			  \
									  \
  if (extend)								  \
    *vec_ = (VEC(T,A) *) vec_##A##_o_reserve_exact			  \
			 (*vec_, alloc_, offsetof (VEC(T,A),base.vec),	  \
			  sizeof (T) PASS_MEM_STAT);			  \
									  \
  return extend;							  \
}									  \
									  \
static inline void VEC_OP (T,A,safe_grow)				  \
     (VEC(T,A) **vec_, int size_ VEC_CHECK_DECL MEM_STAT_DECL)		  \
{									  \
  VEC_ASSERT (size_ >= 0						  \
	      && VEC_OP(T,base,length) VEC_BASE(*vec_) <= (unsigned)size_, \
						 "grow", T, A);		  \
  VEC_OP (T,A,reserve_exact) (vec_,					  \
			      size_ - (int)(*vec_ ? VEC_BASE(*vec_)->num : 0) \
			      VEC_CHECK_PASS PASS_MEM_STAT);		  \
  VEC_BASE (*vec_)->num = size_;					  \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_push)					  \
     (VEC(T,A) **vec_, const T obj_ VEC_CHECK_DECL MEM_STAT_DECL)	  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_push) (VEC_BASE(*vec_), obj_ VEC_CHECK_PASS);  \
}									  \
									  \
static inline T *VEC_OP (T,A,safe_insert)		     	  	  \
     (VEC(T,A) **vec_, unsigned ix_, const T obj_			  \
 		VEC_CHECK_DECL MEM_STAT_DECL)				  \
{									  \
  VEC_OP (T,A,reserve) (vec_, 1 VEC_CHECK_PASS PASS_MEM_STAT);		  \
									  \
  return VEC_OP (T,base,quick_insert) (VEC_BASE(*vec_), ix_, obj_	  \
				       VEC_CHECK_PASS);			  \
}

#endif /* GCC_VEC_H */
