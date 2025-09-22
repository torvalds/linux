/* Virtual array support.
   Copyright (C) 1998, 1999, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Cygnus Solutions.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef GCC_VARRAY_H
#define GCC_VARRAY_H

#ifndef HOST_WIDE_INT
#include "machmode.h"
#endif

#ifndef GCC_SYSTEM_H
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#endif

/* Enum indicating what the varray contains.
   If this is changed, `element' in varray.c needs to be updated.  */

enum varray_data_enum {
  VARRAY_DATA_C,
  VARRAY_DATA_UC,
  VARRAY_DATA_S,
  VARRAY_DATA_US,
  VARRAY_DATA_I,
  VARRAY_DATA_U,
  VARRAY_DATA_L,
  VARRAY_DATA_UL,
  VARRAY_DATA_HINT,
  VARRAY_DATA_UHINT,
  VARRAY_DATA_GENERIC,
  VARRAY_DATA_GENERIC_NOGC,
  VARRAY_DATA_CPTR,
  VARRAY_DATA_RTX,
  VARRAY_DATA_RTVEC,
  VARRAY_DATA_TREE,
  VARRAY_DATA_BITMAP,
  VARRAY_DATA_REG,
  VARRAY_DATA_BB,
  VARRAY_DATA_TE,
  VARRAY_DATA_EDGE,
  VARRAY_DATA_TREE_PTR,
  NUM_VARRAY_DATA
};

/* Union of various array types that are used.  */
typedef union varray_data_tag GTY (()) {
  char			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_C")))		vdt_c[1];
  unsigned char		  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_UC")))	vdt_uc[1];
  short			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_S")))		vdt_s[1];
  unsigned short	  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_US")))	vdt_us[1];
  int			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_I")))		vdt_i[1];
  unsigned int		  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_U")))		vdt_u[1];
  long			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_L")))		vdt_l[1];
  unsigned long		  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_UL")))	vdt_ul[1];
  HOST_WIDE_INT		  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_HINT")))	vdt_hint[1];
  unsigned HOST_WIDE_INT  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_UHINT")))	vdt_uhint[1];
  PTR			  GTY ((length ("%0.num_elements"), use_param,
				tag ("VARRAY_DATA_GENERIC")))	vdt_generic[1];
  PTR			  GTY ((length ("%0.num_elements"), skip (""),
				tag ("VARRAY_DATA_GENERIC_NOGC")))	vdt_generic_nogc[1];
  char			 *GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_CPTR")))	vdt_cptr[1];
  rtx			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_RTX")))	vdt_rtx[1];
  rtvec			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_RTVEC")))	vdt_rtvec[1];
  tree			  GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_TREE")))	vdt_tree[1];
  struct bitmap_head_def *GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_BITMAP")))	vdt_bitmap[1];
  struct reg_info_def	 *GTY ((length ("%0.num_elements"), skip,
				tag ("VARRAY_DATA_REG")))	vdt_reg[1];
  struct basic_block_def *GTY ((length ("%0.num_elements"), skip,
				tag ("VARRAY_DATA_BB")))	vdt_bb[1];
  struct elt_list	 *GTY ((length ("%0.num_elements"),
				tag ("VARRAY_DATA_TE")))	vdt_te[1];
  struct edge_def        *GTY ((length ("%0.num_elements"),
	                        tag ("VARRAY_DATA_EDGE")))	vdt_e[1];
  tree                   *GTY ((length ("%0.num_elements"), skip (""),
	                        tag ("VARRAY_DATA_TREE_PTR")))	vdt_tp[1];
} varray_data;

/* Virtual array of pointers header.  */
struct varray_head_tag GTY(()) {
  size_t	num_elements;	/* Maximum element number allocated.  */
  size_t        elements_used;  /* The number of elements used, if
				   using VARRAY_PUSH/VARRAY_POP.  */
  enum varray_data_enum type;	/* The kind of elements in the varray.  */
  const char   *name;		/* name of the varray for reporting errors */
  varray_data	GTY ((desc ("%0.type"))) data;	/* The data elements follow,
						   must be last.  */
};
typedef struct varray_head_tag *varray_type;

/* Allocate a virtual array with NUM elements, each of which is SIZE bytes
   long, named NAME.  Array elements are zeroed.  */
extern varray_type varray_init (size_t, enum varray_data_enum, const char *);

#define VARRAY_CHAR_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_C, name)

#define VARRAY_UCHAR_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_UC, name)

#define VARRAY_SHORT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_S, name)

#define VARRAY_USHORT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_US, name)

#define VARRAY_INT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_I, name)

#define VARRAY_UINT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_U, name)

#define VARRAY_LONG_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_L, name)

#define VARRAY_ULONG_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_UL, name)

#define VARRAY_WIDE_INT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_HINT, name)

#define VARRAY_UWIDE_INT_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_UHINT, name)

#define VARRAY_GENERIC_PTR_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_GENERIC, name)

#define VARRAY_GENERIC_PTR_NOGC_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_GENERIC_NOGC, name)

#define VARRAY_CHAR_PTR_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_CPTR, name)

#define VARRAY_RTX_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_RTX, name)

#define VARRAY_RTVEC_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_RTVEC, name)

#define VARRAY_TREE_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_TREE, name)

#define VARRAY_BITMAP_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_BITMAP, name)

#define VARRAY_REG_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_REG, name)

#define VARRAY_BB_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_BB, name)

#define VARRAY_ELT_LIST_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_TE, name)

#define VARRAY_EDGE_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_EDGE, name)

#define VARRAY_TREE_PTR_INIT(va, num, name) \
  va = varray_init (num, VARRAY_DATA_TREE_PTR, name)

/* Free up memory allocated by the virtual array, but do not free any of the
   elements involved.  */
#define VARRAY_FREE(vp) \
  do { if (vp) { free (vp); vp = (varray_type) 0; } } while (0)

/* Grow/shrink the virtual array VA to N elements.  */
extern varray_type varray_grow (varray_type, size_t);

#define VARRAY_GROW(VA, N) ((VA) = varray_grow (VA, N))

#define VARRAY_SIZE(VA)	((VA)->num_elements)

#define VARRAY_ACTIVE_SIZE(VA)	((VA)->elements_used)
#define VARRAY_POP_ALL(VA)	((VA)->elements_used = 0)

#define VARRAY_CLEAR(VA) varray_clear(VA)

extern void varray_clear (varray_type);
extern void dump_varray_statistics (void);

/* Check for VARRAY_xxx macros being in bound.  */
#if defined ENABLE_CHECKING && (GCC_VERSION >= 2007)
extern void varray_check_failed (varray_type, size_t, const char *, int,
				 const char *) ATTRIBUTE_NORETURN;
extern void varray_underflow (varray_type, const char *, int, const char *)
     ATTRIBUTE_NORETURN;
#define VARRAY_CHECK(VA, N, T) __extension__			\
(*({ varray_type const _va = (VA);				\
     const size_t _n = (N);					\
     if (_n >= _va->num_elements)				\
       varray_check_failed (_va, _n, __FILE__, __LINE__, __FUNCTION__);	\
     &_va->data.T[_n]; }))

#define VARRAY_POP(VA) do {					\
  varray_type const _va = (VA);					\
  if (_va->elements_used == 0)					\
    varray_underflow (_va, __FILE__, __LINE__, __FUNCTION__);	\
  else								\
    _va->elements_used--;					\
} while (0)

#else
#define VARRAY_CHECK(VA, N, T) ((VA)->data.T[N])
/* Pop the top element of VA.  */
#define VARRAY_POP(VA) do { ((VA)->elements_used--); } while (0)
#endif

/* Push X onto VA.  T is the name of the field in varray_data
   corresponding to the type of X.  */
#define VARRAY_PUSH(VA, T, X)				\
  do							\
    {							\
      if ((VA)->elements_used >= (VA)->num_elements)	\
        VARRAY_GROW ((VA), 2 * (VA)->num_elements);	\
      (VA)->data.T[(VA)->elements_used++] = (X);	\
    }							\
  while (0)

#define VARRAY_CHAR(VA, N)		VARRAY_CHECK (VA, N, vdt_c)
#define VARRAY_UCHAR(VA, N)		VARRAY_CHECK (VA, N, vdt_uc)
#define VARRAY_SHORT(VA, N)		VARRAY_CHECK (VA, N, vdt_s)
#define VARRAY_USHORT(VA, N)		VARRAY_CHECK (VA, N, vdt_us)
#define VARRAY_INT(VA, N)		VARRAY_CHECK (VA, N, vdt_i)
#define VARRAY_UINT(VA, N)		VARRAY_CHECK (VA, N, vdt_u)
#define VARRAY_LONG(VA, N)		VARRAY_CHECK (VA, N, vdt_l)
#define VARRAY_ULONG(VA, N)		VARRAY_CHECK (VA, N, vdt_ul)
#define VARRAY_WIDE_INT(VA, N)		VARRAY_CHECK (VA, N, vdt_hint)
#define VARRAY_UWIDE_INT(VA, N)		VARRAY_CHECK (VA, N, vdt_uhint)
#define VARRAY_GENERIC_PTR(VA,N)	VARRAY_CHECK (VA, N, vdt_generic)
#define VARRAY_GENERIC_PTR_NOGC(VA,N)	VARRAY_CHECK (VA, N, vdt_generic_nogc)
#define VARRAY_CHAR_PTR(VA,N)		VARRAY_CHECK (VA, N, vdt_cptr)
#define VARRAY_RTX(VA, N)		VARRAY_CHECK (VA, N, vdt_rtx)
#define VARRAY_RTVEC(VA, N)		VARRAY_CHECK (VA, N, vdt_rtvec)
#define VARRAY_TREE(VA, N)		VARRAY_CHECK (VA, N, vdt_tree)
#define VARRAY_BITMAP(VA, N)		VARRAY_CHECK (VA, N, vdt_bitmap)
#define VARRAY_REG(VA, N)		VARRAY_CHECK (VA, N, vdt_reg)
#define VARRAY_BB(VA, N)		VARRAY_CHECK (VA, N, vdt_bb)
#define VARRAY_ELT_LIST(VA, N)		VARRAY_CHECK (VA, N, vdt_te)
#define VARRAY_EDGE(VA, N)		VARRAY_CHECK (VA, N, vdt_e)
#define VARRAY_TREE_PTR(VA, N)		VARRAY_CHECK (VA, N, vdt_tp)

/* Push a new element on the end of VA, extending it if necessary.  */
#define VARRAY_PUSH_CHAR(VA, X)		VARRAY_PUSH (VA, vdt_c, X)
#define VARRAY_PUSH_UCHAR(VA, X)	VARRAY_PUSH (VA, vdt_uc, X)
#define VARRAY_PUSH_SHORT(VA, X)	VARRAY_PUSH (VA, vdt_s, X)
#define VARRAY_PUSH_USHORT(VA, X)	VARRAY_PUSH (VA, vdt_us, X)
#define VARRAY_PUSH_INT(VA, X)		VARRAY_PUSH (VA, vdt_i, X)
#define VARRAY_PUSH_UINT(VA, X)		VARRAY_PUSH (VA, vdt_u, X)
#define VARRAY_PUSH_LONG(VA, X)		VARRAY_PUSH (VA, vdt_l, X)
#define VARRAY_PUSH_ULONG(VA, X)	VARRAY_PUSH (VA, vdt_ul, X)
#define VARRAY_PUSH_WIDE_INT(VA, X)	VARRAY_PUSH (VA, vdt_hint, X)
#define VARRAY_PUSH_UWIDE_INT(VA, X)	VARRAY_PUSH (VA, vdt_uhint, X)
#define VARRAY_PUSH_GENERIC_PTR(VA, X)	VARRAY_PUSH (VA, vdt_generic, X)
#define VARRAY_PUSH_GENERIC_PTR_NOGC(VA, X)	VARRAY_PUSH (VA, vdt_generic_nogc, X)
#define VARRAY_PUSH_CHAR_PTR(VA, X)	VARRAY_PUSH (VA, vdt_cptr, X)
#define VARRAY_PUSH_RTX(VA, X)		VARRAY_PUSH (VA, vdt_rtx, X)
#define VARRAY_PUSH_RTVEC(VA, X)	VARRAY_PUSH (VA, vdt_rtvec, X)
#define VARRAY_PUSH_TREE(VA, X)		VARRAY_PUSH (VA, vdt_tree, X)
#define VARRAY_PUSH_BITMAP(VA, X)	VARRAY_PUSH (VA, vdt_bitmap, X)
#define VARRAY_PUSH_REG(VA, X)		VARRAY_PUSH (VA, vdt_reg, X)
#define VARRAY_PUSH_BB(VA, X)		VARRAY_PUSH (VA, vdt_bb, X)
#define VARRAY_PUSH_EDGE(VA, X)		VARRAY_PUSH (VA, vdt_e, X)
#define VARRAY_PUSH_TREE_PTR(VA, X)	VARRAY_PUSH (VA, vdt_tp, X)

/* Return the last element of VA.  */
#define VARRAY_TOP(VA, T) VARRAY_CHECK(VA, (VA)->elements_used - 1, T)

#define VARRAY_TOP_CHAR(VA)		VARRAY_TOP (VA, vdt_c)
#define VARRAY_TOP_UCHAR(VA)	        VARRAY_TOP (VA, vdt_uc)
#define VARRAY_TOP_SHORT(VA)	        VARRAY_TOP (VA, vdt_s)
#define VARRAY_TOP_USHORT(VA)	        VARRAY_TOP (VA, vdt_us)
#define VARRAY_TOP_INT(VA)		VARRAY_TOP (VA, vdt_i)
#define VARRAY_TOP_UINT(VA)		VARRAY_TOP (VA, vdt_u)
#define VARRAY_TOP_LONG(VA)		VARRAY_TOP (VA, vdt_l)
#define VARRAY_TOP_ULONG(VA)	        VARRAY_TOP (VA, vdt_ul)
#define VARRAY_TOP_WIDE_INT(VA)	        VARRAY_TOP (VA, vdt_hint)
#define VARRAY_TOP_UWIDE_INT(VA)	VARRAY_TOP (VA, vdt_uhint)
#define VARRAY_TOP_GENERIC_PTR(VA)	VARRAY_TOP (VA, vdt_generic)
#define VARRAY_TOP_GENERIC_PTR_NOGC(VA)	VARRAY_TOP (VA, vdt_generic_nogc)
#define VARRAY_TOP_CHAR_PTR(VA)		VARRAY_TOP (VA, vdt_cptr)
#define VARRAY_TOP_RTX(VA)		VARRAY_TOP (VA, vdt_rtx)
#define VARRAY_TOP_RTVEC(VA)	        VARRAY_TOP (VA, vdt_rtvec)
#define VARRAY_TOP_TREE(VA)		VARRAY_TOP (VA, vdt_tree)
#define VARRAY_TOP_BITMAP(VA)	        VARRAY_TOP (VA, vdt_bitmap)
#define VARRAY_TOP_REG(VA)		VARRAY_TOP (VA, vdt_reg)
#define VARRAY_TOP_BB(VA)		VARRAY_TOP (VA, vdt_bb)
#define VARRAY_TOP_EDGE(VA)		VARRAY_TOP (VA, vdt_e)
#define VARRAY_TOP_TREE_PTR(VA)		VARRAY_TOP (VA, vdt_tp)

#endif /* ! GCC_VARRAY_H */
