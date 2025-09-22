/* Copyright (C) 2001, 2002, 2003, 2005 Free Software Foundation, Inc.

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

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

/* Locate the FDE entry for a given address, using Darwin's keymgr support.  */

#include "tconfig.h"
#include "tsystem.h"
#include <string.h>
#include <stdlib.h>
#include "dwarf2.h"
#include "unwind.h"
#define NO_BASE_OF_ENCODED_VALUE
#define DWARF2_OBJECT_END_PTR_EXTENSION
#include "unwind-pe.h"
#include "unwind-dw2-fde.h"
/* Carefully don't include gthr.h.  */

typedef int __gthread_mutex_t;
#define __gthread_mutex_lock(x)  (void)(x)
#define __gthread_mutex_unlock(x) (void)(x)

static const fde * _Unwind_Find_registered_FDE (void *pc,
						struct dwarf_eh_bases *bases);

#define _Unwind_Find_FDE _Unwind_Find_registered_FDE
#include "unwind-dw2-fde.c"
#undef _Unwind_Find_FDE

/* KeyMgr stuff.  */
#define KEYMGR_GCC3_LIVE_IMAGE_LIST     301     /* loaded images  */
#define KEYMGR_GCC3_DW2_OBJ_LIST        302     /* Dwarf2 object list  */

extern void *_keymgr_get_and_lock_processwide_ptr (int);
extern void _keymgr_set_and_unlock_processwide_ptr (int, void *);
extern void _keymgr_unlock_processwide_ptr (int);

struct mach_header;
struct mach_header_64;
extern char *getsectdatafromheader (struct mach_header*, const char*,
				    const char *, unsigned long *);
extern char *getsectdatafromheader_64 (struct mach_header_64*, const char*,
				       const char *, unsigned long *);

/* This is referenced from KEYMGR_GCC3_DW2_OBJ_LIST.  */
struct km_object_info {
  struct object *seen_objects;
  struct object *unseen_objects;
  unsigned spare[2];
};

/* Node of KEYMGR_GCC3_LIVE_IMAGE_LIST.  Info about each resident image.  */
struct live_images {
  unsigned long this_size;                      /* sizeof (live_images)  */
  struct mach_header *mh;                       /* the image info  */
  unsigned long vm_slide;
  void (*destructor)(struct live_images *);     /* destructor for this  */
  struct live_images *next;
  unsigned int examined_p;
  void *fde;
  void *object_info;
  unsigned long info[2];                        /* Future use.  */
};

/* Bits in the examined_p field of struct live_images.  */
enum {
  EXAMINED_IMAGE_MASK = 1,	/* We've seen this one.  */
  ALLOCED_IMAGE_MASK = 2,	/* The FDE entries were allocated by
				   malloc, and must be freed.  This isn't
				   used by newer libgcc versions.  */
  IMAGE_IS_TEXT_MASK = 4,	/* This image is in the TEXT segment.  */
  DESTRUCTOR_MAY_BE_CALLED_LIVE = 8  /* The destructor may be called on an
					object that's part of the live
					image list.  */
};

/* Delete any data we allocated on a live_images structure.  Either
   IMAGE has already been removed from the
   KEYMGR_GCC3_LIVE_IMAGE_LIST and the struct will be deleted
   after we return, or that list is locked and we're being called
   because this object might be about to be unloaded.  Called by
   KeyMgr.  */

static void
live_image_destructor (struct live_images *image)
{
  if (image->object_info)
    {
      struct km_object_info *the_obj_info;

      the_obj_info =
	_keymgr_get_and_lock_processwide_ptr (KEYMGR_GCC3_DW2_OBJ_LIST);
      if (the_obj_info)
	{
	  seen_objects = the_obj_info->seen_objects;
	  unseen_objects = the_obj_info->unseen_objects;

	  /* Free any sorted arrays.  */
	  __deregister_frame_info_bases (image->fde);

	  the_obj_info->seen_objects = seen_objects;
	  the_obj_info->unseen_objects = unseen_objects;
	}
      _keymgr_set_and_unlock_processwide_ptr (KEYMGR_GCC3_DW2_OBJ_LIST,
					      the_obj_info);

      free (image->object_info);
      image->object_info = NULL;
      if (image->examined_p & ALLOCED_IMAGE_MASK)
	free (image->fde);
      image->fde = NULL;
    }
  image->examined_p = 0;
  image->destructor = NULL;
}

/* Run through the list of live images.  If we can allocate memory,
   give each unseen image a new `struct object'.  Even if we can't,
   check whether the PC is inside the FDE of each unseen image.
 */

static inline const fde *
examine_objects (void *pc, struct dwarf_eh_bases *bases, int dont_alloc)
{
  const fde *result = NULL;
  struct live_images *image;

  image = _keymgr_get_and_lock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST);

  for (; image != NULL; image = image->next)
    if ((image->examined_p & EXAMINED_IMAGE_MASK) == 0)
      {
	char *fde = NULL;
	unsigned long sz;

	/* For ppc only check whether or not we have __DATA eh frames.  */
#ifdef __ppc__
	fde = getsectdatafromheader (image->mh, "__DATA", "__eh_frame", &sz);
#endif

	if (fde == NULL)
	  {
#if __LP64__
	    fde = getsectdatafromheader_64 ((struct mach_header_64 *) image->mh,
					    "__TEXT", "__eh_frame", &sz);
#else
	    fde = getsectdatafromheader (image->mh, "__TEXT",
					 "__eh_frame", &sz);
#endif
	    if (fde != NULL)
	      image->examined_p |= IMAGE_IS_TEXT_MASK;
	  }

	/* If .eh_frame is empty, don't register at all.  */
	if (fde != NULL && sz > 0)
	  {
	    char *real_fde = (fde + image->vm_slide);
	    struct object *ob = NULL;
	    struct object panicob;

	    if (! dont_alloc)
	      ob = calloc (1, sizeof (struct object));
	    dont_alloc |= ob == NULL;
	    if (dont_alloc)
	      ob = &panicob;

	    ob->pc_begin = (void *)-1;
	    ob->tbase = 0;
	    ob->dbase = 0;
	    ob->u.single = (struct dwarf_fde *)real_fde;
	    ob->s.i = 0;
	    ob->s.b.encoding = DW_EH_PE_omit;
	    ob->fde_end = real_fde + sz;

	    image->fde = real_fde;

	    result = search_object (ob, pc);

	    if (! dont_alloc)
	      {
		struct object **p;

		image->destructor = live_image_destructor;
		image->object_info = ob;

		image->examined_p |= (EXAMINED_IMAGE_MASK
				      | DESTRUCTOR_MAY_BE_CALLED_LIVE);

		/* Insert the object into the classified list.  */
		for (p = &seen_objects; *p ; p = &(*p)->next)
		  if ((*p)->pc_begin < ob->pc_begin)
		    break;
		ob->next = *p;
		*p = ob;
	      }

	    if (result)
	      {
		int encoding;
		_Unwind_Ptr func;

		bases->tbase = ob->tbase;
		bases->dbase = ob->dbase;

		encoding = ob->s.b.encoding;
		if (ob->s.b.mixed_encoding)
		  encoding = get_fde_encoding (result);
		read_encoded_value_with_base (encoding,
					      base_from_object (encoding, ob),
					      result->pc_begin, &func);
		bases->func = (void *) func;
		break;
	      }
	  }
	else
	  image->examined_p |= EXAMINED_IMAGE_MASK;
      }

  _keymgr_unlock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST);

  return result;
}

const fde *
_Unwind_Find_FDE (void *pc, struct dwarf_eh_bases *bases)
{
  struct km_object_info *the_obj_info;
  const fde *ret = NULL;

  the_obj_info =
    _keymgr_get_and_lock_processwide_ptr (KEYMGR_GCC3_DW2_OBJ_LIST);
  if (! the_obj_info)
    the_obj_info = calloc (1, sizeof (*the_obj_info));

  if (the_obj_info != NULL)
    {
      seen_objects = the_obj_info->seen_objects;
      unseen_objects = the_obj_info->unseen_objects;

      ret = _Unwind_Find_registered_FDE (pc, bases);
    }

  /* OK, didn't find it in the list of FDEs we've seen before,
     so go through and look at the new ones.  */
  if (ret == NULL)
    ret = examine_objects (pc, bases, the_obj_info == NULL);

  if (the_obj_info != NULL)
    {
      the_obj_info->seen_objects = seen_objects;
      the_obj_info->unseen_objects = unseen_objects;
    }
  _keymgr_set_and_unlock_processwide_ptr (KEYMGR_GCC3_DW2_OBJ_LIST,
					  the_obj_info);
  return ret;
}
