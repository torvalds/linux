/* KeyMgr backwards-compatibility support for Darwin.
   Copyright (C) 2001, 2002, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* It is incorrect to include config.h here, because this file is being
   compiled for the target, and hence definitions concerning only the host
   do not apply.  */

#include "tconfig.h"
#include "tsystem.h"

/* This file doesn't do anything useful on non-powerpc targets, since they
   don't have backwards compatibility anyway.  */

#ifdef __ppc__

/* Homemade decls substituting for getsect.h and dyld.h, so cross
   compilation works.  */
struct mach_header;
extern char *getsectdatafromheader (struct mach_header *, const char *,
				    const char *, unsigned long *);
extern void _dyld_register_func_for_add_image
  (void (*) (struct mach_header *, unsigned long));
extern void _dyld_register_func_for_remove_image
  (void (*) (struct mach_header *, unsigned long));

extern void __darwin_gcc3_preregister_frame_info (void);

/* These are from "keymgr.h".  */
extern void _init_keymgr (void);
extern void *_keymgr_get_and_lock_processwide_ptr (unsigned key);
extern void _keymgr_set_and_unlock_processwide_ptr (unsigned key, void *ptr);

extern void *__keymgr_global[];
typedef struct _Sinfo_Node {
        unsigned int size ;             /*size of this node*/
        unsigned short major_version ;  /*API major version.*/
        unsigned short minor_version ;  /*API minor version.*/
        } _Tinfo_Node ;

/* KeyMgr 3.x is the first one supporting GCC3 stuff natively.  */
#define KEYMGR_API_MAJOR_GCC3           3       
/* ... with these keys.  */
#define KEYMGR_GCC3_LIVE_IMAGE_LIST	301     /* loaded images  */
#define KEYMGR_GCC3_DW2_OBJ_LIST	302     /* Dwarf2 object list  */   

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


/* These routines are used only on Darwin versions before 10.2.
   Later versions have equivalent code in the system.  
   Eventually, they might go away, although it might be a long time...  */

static void darwin_unwind_dyld_remove_image_hook 
  (struct mach_header *m, unsigned long s);
static void darwin_unwind_dyld_remove_image_hook 
  (struct mach_header *m, unsigned long s);
extern void __darwin_gcc3_preregister_frame_info (void);
     
static void
darwin_unwind_dyld_add_image_hook (struct mach_header *mh, unsigned long slide)
{
  struct live_images *l = (struct live_images *)calloc (1, sizeof (*l));
  l->mh = mh;
  l->vm_slide = slide;
  l->this_size = sizeof (*l);
  l->next = (struct live_images *)
	_keymgr_get_and_lock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST);
  _keymgr_set_and_unlock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST, l);
}

static void
darwin_unwind_dyld_remove_image_hook (struct mach_header *m, unsigned long s)
{
  struct live_images *top, **lip, *destroy = NULL;

  /* Look for it in the list of live images and delete it.  */

  top = (struct live_images *)
	   _keymgr_get_and_lock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST);
  for (lip = &top; *lip != NULL; lip = &(*lip)->next)
    {
      if ((*lip)->mh == m && (*lip)->vm_slide == s)
        {
          destroy = *lip;
          *lip = destroy->next;			/* unlink DESTROY  */

          if (destroy->this_size != sizeof (*destroy))	/* sanity check  */
            abort ();

          break;
        }
    }
  _keymgr_set_and_unlock_processwide_ptr (KEYMGR_GCC3_LIVE_IMAGE_LIST, top);

  /* Now that we have unlinked this from the image list, toss it.  */
  if (destroy != NULL)
    {
      if (destroy->destructor != NULL)
	(*destroy->destructor) (destroy);
      free (destroy);
    }
}

void
__darwin_gcc3_preregister_frame_info (void)
{
  const _Tinfo_Node *info;
  _init_keymgr ();
  info = (_Tinfo_Node *)__keymgr_global[2];
  if (info != NULL)
    {
      if (info->major_version >= KEYMGR_API_MAJOR_GCC3)
	return;
      /* Otherwise, use our own add_image_hooks.  */
    }

  _dyld_register_func_for_add_image (darwin_unwind_dyld_add_image_hook);
  _dyld_register_func_for_remove_image (darwin_unwind_dyld_remove_image_hook);
}

#endif  /* __ppc__ */
