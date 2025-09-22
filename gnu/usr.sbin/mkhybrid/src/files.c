/*
 * File files.c - Handle ADD_FILES related stuff.

   Written by Eric Youngdale (1993).

   Copyright 1993 Yggdrasil Computing, Incorporated

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* ADD_FILES changes made by Ross Biro biro@yggdrasil.com 2/23/95 */


#include "config.h"
#include <errno.h>
#include "mkisofs.h"

#include <stdlib.h>
#include <ctype.h>

#ifdef ADD_FILES

struct file_adds {
  char *name;
  struct file_adds *child;
  struct file_adds *next;
  int add_count;
  int used;
  union diru {
	/*
	 * XXX Struct dirent is not guaranteed to be any size on a POSIX
	 * XXX compliant system.
	 * XXX We need to allocate enough space here, to allow the hacky
	 * XXX code in tree.c made by Ross Biro biro@yggdrasil.com
	 * XXX to work on operating systems other than Linux :-(
	 * XXX Changes made by Joerg Schilling joerg@schily.isdn.cs.tu-berlin.de
	 * XXX to prevent core dumps on Solaris.
	 * XXX Space allocated:
	 * XXX		1024 bytes == NAME_MAX
	 * XXX	+	2   bytes for directory record length 
	 * XXX	+	2*8 bytes for inode number & offset (64 for future exp)
	 */
  	struct dirent	de;
  	char		dspace[NAME_MAX+2+2*8];
  } du;
  struct {
    char *path;
    char *name;
  } *adds;
};
extern struct file_adds *root_file_adds;

/*
 * FIXME(eric) - the file adding code really doesn't work very well
 * at all.  We should differentiate between adding directories, and adding
 * single files, as adding a full directory affects how we should be
 * searching for things.  Ideally what we should do is make two passes
 * through the local filesystem - one to figure out what trees we need
 * to scan (and merge in any additions at that point), and the second to
 * actually fill out each structure with the appropriate contents.
 *
 * 
 */

struct file_adds *root_file_adds = NULL;

void
FDECL2(add_one_file, char *, addpath, char *, path )
{
  char *cp;
  char *name;
  struct file_adds *f;
  struct file_adds *tmp;

  f = root_file_adds;
  tmp = NULL;

  name = strrchr (addpath, PATH_SEPARATOR);
  if (name == NULL) {
    name = addpath;
  } else {
    name++;
  }

  cp = strtok (addpath, SPATH_SEPARATOR);

  while (cp != NULL && strcmp (name, cp)) {
     if (f == NULL) {
        root_file_adds = e_malloc (sizeof *root_file_adds);
        f=root_file_adds;
        f->name = NULL;
        f->child = NULL;
        f->next = NULL;
        f->add_count = 0;
        f->adds = NULL;
	f->used = 0;
     }
    if (f->child) {
      for (tmp = f->child; tmp->next != NULL; tmp =tmp->next) {
         if (strcmp (tmp->name, cp) == 0) {
           f = tmp;
           goto next;
         }
      }
      if (strcmp (tmp->name, cp) == 0) {
          f=tmp;
          goto next;
      }
      /* add a new node. */
      tmp->next = e_malloc (sizeof (*tmp->next));
      f=tmp->next;
      f->name = strdup (cp);
      f->child = NULL;
      f->next = NULL;
      f->add_count = 0;
      f->adds = NULL;
      f->used = 0;
    } else {
      /* no children. */
      f->child = e_malloc (sizeof (*f->child));
      f = f->child;
      f->name = strdup (cp);
      f->child = NULL;
      f->next = NULL;
      f->add_count = 0;
      f->adds = NULL;
      f->used = 0;

    }
   next:
     cp = strtok (NULL, SPATH_SEPARATOR);
   }
  /* Now f if non-null points to where we should add things */
  if (f == NULL) {
     root_file_adds = e_malloc (sizeof *root_file_adds);
     f=root_file_adds;
     f->name = NULL;
     f->child = NULL;
     f->next = NULL;
     f->add_count = 0;
     f->adds = NULL;
   }

  /* Now f really points to where we should add this name. */
  f->add_count++;
  f->adds = realloc (f->adds, sizeof (*f->adds)*f->add_count);
  f->adds[f->add_count-1].path = strdup (path);
  f->adds[f->add_count-1].name = strdup (name);
}

/*
 * Function:	add_file_list
 *
 * Purpose:	Register an add-in file.
 *
 * Arguments:
 */
void
FDECL3(add_file_list, int, argc, char **,argv, int, ind)
{
  char *ptr;
  char *dup_arg;

  while (ind < argc) {
     dup_arg = strdup (argv[ind]);
     ptr = strchr (dup_arg,'=');
     if (ptr == NULL) {
        free (dup_arg);
        return;
     }
     *ptr = 0;
     ptr++;
     add_one_file (dup_arg, ptr);
     free (dup_arg);
     ind++;
  }
}
void
FDECL1(add_file, char *, filename)
{
  char buff[1024];
  FILE *f;
  char *ptr;
  char *p2;
  int count=0;

  if (strcmp (filename, "-") == 0) {
    f = stdin;
  } else {
    f = fopen (filename, "r");
    if (f == NULL) {
      perror ("fopen");
      exit (1);
    }
  }
  while (fgets (buff, 1024, f)) {
    count++;
    ptr = buff;
    while (isspace (*ptr)) ptr++;
    if (*ptr==0) continue;
    if (*ptr=='#') continue;

    if (ptr[strlen(ptr)-1]== '\n') ptr[strlen(ptr)-1]=0;
    p2 = strchr (ptr, '=');
    if (p2 == NULL) {
      fprintf (stderr, "Error in line %d: %s\n", count, buff);
      exit (1);
    }
    *p2 = 0;
    p2++;
    add_one_file (ptr, p2);
  }
  if (f != stdin) fclose (f);
}

/* This function looks up additions. */
char *
FDECL3(look_up_addition,char **, newpath, char *,path, struct dirent **,de)
{
  char *dup_path;
  char *cp;
  struct file_adds *f;
  struct file_adds *tmp = NULL;

  f=root_file_adds;
  if (!f) return NULL;

  /* I don't trust strtok */
  dup_path = strdup (path);

  cp = strtok (dup_path, SPATH_SEPARATOR);
  while (cp != NULL) {
    for (tmp = f->child; tmp != NULL; tmp=tmp->next) {
      if (strcmp (tmp->name, cp) == 0) break;
    }
    if (tmp == NULL) {
      /* no match */
      free (dup_path);
      return (NULL);
    }
    f = tmp;
    cp = strtok(NULL, SPATH_SEPARATOR);
  }
  free (dup_path);

  /*
   * If nothing, then return.
   */
  if (tmp == NULL) 
    {
      /* no match */
      return (NULL);
    }

  /* looks like we found something. */
  if (tmp->used >= tmp->add_count) return (NULL);

  *newpath = tmp->adds[tmp->used].path;
  tmp->used++;
  *de = &(tmp->du.de);
  return (tmp->adds[tmp->used-1].name);
  
}

/* This function looks up additions. */
void
FDECL2(nuke_duplicates, char *, path, struct dirent **,de) 
{
  char *dup_path;
  char *cp;
  struct file_adds *f;
  struct file_adds *tmp;

  f=root_file_adds;
  if (!f) return;

  /* I don't trust strtok */
  dup_path = strdup (path);

  cp = strtok (dup_path, SPATH_SEPARATOR);
  while (cp != NULL) {
    for (tmp = f->child; tmp != NULL; tmp=tmp->next) {
      if (strcmp (tmp->name, cp) == 0) break;
    }
    if (tmp == NULL) {
      /* no match */
      free (dup_path);
      return;
    }
    f = tmp;
    cp = strtok(NULL, SPATH_SEPARATOR);
  }
  free (dup_path);

#if 0
  /* looks like we found something. */
  if (tmp->used >= tmp->add_count) return;

  *newpath = tmp->adds[tmp->used].path;
  tmp->used++;
  *de = &(tmp->du.de);
  return (tmp->adds[tmp->used-1].name);
#endif
  return;
}

/* This function lets us add files from outside the standard file tree.
   It is useful if we want to duplicate a cd, but add/replace things.
   We should note that the real path will be used for exclusions. */

struct dirent *
FDECL3(readdir_add_files, char **, pathp, char *,path, DIR *, dir){
  struct dirent *de;

  char *addpath;
  char *name;

  de = readdir (dir);
  if (de) {
    nuke_duplicates(path, &de);
    return (de);
  }

  name=look_up_addition (&addpath, path, &de);

  if (!name) {
    return NULL;
  }

  *pathp=addpath;
  
  /* Now we must create the directory entry. */
  /* fortuneately only the name seems to matter. */
  /*
  de->d_ino = -1;
  de->d_off = 0;
  de->d_reclen = strlen (name);
  */
  strncpy (de->d_name, name, NAME_MAX);
  de->d_name[NAME_MAX]=0;
  nuke_duplicates(path, &de);
  return (de);

}

#else
struct dirent *
FDECL3(readdir_add_files, char **, pathp, char *,path, DIR *, dir){
  return (readdir (dir));
}
#endif
