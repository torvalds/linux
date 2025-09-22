/*
 * File tree.c - scan directory  tree and build memory structures for iso9660
 * filesystem

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

/* APPLE_HYB James Pearson j.pearson@ge.ucl.ac.uk 16/3/1999 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "config.h"
#include "apple_proto.h"

#ifndef VMS
#if defined(MAJOR_IN_SYSMACROS)
#include <sys/sysmacros.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fctldefs.h>

#if defined(MAJOR_IN_MKDEV)
#include <sys/types.h>
#include <sys/mkdev.h>
#endif
#else
#include <sys/file.h>
#include <vms/fabdef.h>
#include "vms.h"
extern char * strdup(const char *);
#endif

/*
 * Autoconf should be able to figure this one out for us and let us know
 * whether the system has memmove or not.
 */
# ifndef HAVE_MEMMOVE
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif

#include "mkisofs.h"
#include "iso9660.h"
#include "match.h"

#include <sys/stat.h>

#ifdef	DOESNT_WORK

#ifdef NON_UNIXFS
#define S_ISLNK(m)	(0)
#define S_ISSOCK(m)	(0)
#define S_ISFIFO(m)	(0)
#else
#ifndef S_ISLNK
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
#ifndef S_ISSOCK
# ifdef S_IFSOCK
#   define S_ISSOCK(m)	(((m) & S_IFMT) == S_IFSOCK)
# else
#   define S_ISSOCK(m)	(0)
# endif
#endif
#endif

#else
#include <statdefs.h>
#endif


#ifdef __SVR4
extern char * strdup(const char *);
#endif

static unsigned char symlink_buff[256];

static void stat_fix	__PR((struct stat * st));
static void generate_reloc_directory __PR((void));

static void DECL(attach_dot_entries, (struct directory * dirnode,
		   struct stat * parent_stat));
static void DECL(delete_directory, (struct directory * parent, struct directory * child));

extern int verbose;

struct stat fstatbuf = {0,};  /* We use this for the artificial entries we create */

struct stat root_statbuf = {0, };  /* Stat buffer for root directory */

struct directory * reloc_dir = NULL;

static void
FDECL1(stat_fix, struct stat *, st)
{
  /* Remove the uid and gid, they will only be useful on the author's
     system.  */
  st->st_uid = 0;
  st->st_gid = 0;

 /*
  * Make sure the file modes make sense.  Turn on all read bits.  Turn
  * on all exec/search bits if any exec/search bit is set.  Turn off
  * all write bits, and all special mode bits (on a r/o fs lock bits
  * are useless, and with uid+gid 0 don't want set-id bits, either).
  */
  st->st_mode |= 0444;
#ifndef _WIN32		/* make all file "executable" */
  if (st->st_mode & 0111)
#endif /* _WIN32 */
    st->st_mode |= 0111;
  st->st_mode &= ~07222;
}

int
FDECL2(stat_filter, char *, path, struct stat *, st)
{
  int result = stat(path, st);
  if (result >= 0 && rationalize)
    stat_fix(st);
  return result;
}

int
FDECL2(lstat_filter, char *, path, struct stat *, st)
{
  int result = lstat(path, st);
  if (result >= 0 && rationalize)
    stat_fix(st);
  return result;
}

static int FDECL1(sort_n_finish, struct directory *, this_dir)
{
  struct directory_entry  * s_entry;
  struct directory_entry  * s_entry1;
  struct directory_entry  * table;
  int			    count;
  int			    d1;
  int			    d2;
  int			    d3;
  int			    new_reclen;
  char			 *  c;
  int			    status = 0;
  int			    tablesize = 0;
  char			    newname[34];
  char			    rootname[34];

  /* Here we can take the opportunity to toss duplicate entries from the
     directory.  */

  /* ignore if it's hidden */
  if(this_dir->dir_flags & INHIBIT_ISO9660_ENTRY)
    {
      return 0;
    }

  table = NULL;

  init_fstatbuf();

  /*
   * If we had artificially created this directory, then we might be
   * missing the required '.' entries.  Create these now if we need
   * them.
   */
  if( (this_dir->dir_flags & (DIR_HAS_DOT | DIR_HAS_DOTDOT)) != 
      (DIR_HAS_DOT | DIR_HAS_DOTDOT) )
    {
      attach_dot_entries(this_dir, &fstatbuf);
    }

  flush_file_hash();
  s_entry = this_dir->contents;
  while(s_entry)
    {
    /* ignore if it's hidden */
    if (s_entry->de_flags & INHIBIT_ISO9660_ENTRY)
      {
	s_entry = s_entry->next;
	continue;
      }
	  
      /*
       * First assume no conflict, and handle this case 
       */
      if(!(s_entry1 = find_file_hash(s_entry->isorec.name)))
	{
	  add_file_hash(s_entry);
	  s_entry = s_entry->next;
	  continue;
	}
	  
#ifdef APPLE_HYB
      /* if the pair are associated, then skip (as they have the same name!) */
      if(apple_both && s_entry1->assoc && s_entry1->assoc == s_entry)
	{
	  s_entry = s_entry->next;
	  continue;
	}
#endif /* APPLE_HYB */

      if(s_entry1 == s_entry)
	{
	  fprintf(stderr,"Fatal goof\n");
	  exit(1);
	}
      
      /* 
       * OK, handle the conflicts.  Try substitute names until we come
       * up with a winner 
       */
      strcpy(rootname, s_entry->isorec.name);
      if(full_iso9660_filenames) 
	{
	  if(strlen(rootname) > 27) rootname[27] = 0;
	}

      /*
       * Strip off the non-significant part of the name so that we are left
       * with a sensible root filename.  If we don't find a '.', then try
       * a ';'.
       */
      c  = strchr(rootname, '.');
      if (c) 
	*c = 0;
      else
	{
	  c  = strchr(rootname, ';');
	  if (c) *c = 0;
	}
      for(d1 = 0; d1 < 36; d1++)
	{
	  for(d2 = 0; d2 < 36; d2++)
	    {
	      for(d3 = 0; d3 < 36; d3++)
		{
		  snprintf(newname, sizeof newname, "%s.%c%c%c%s", rootname,  
			  (d1 <= 9 ? '0' + d1 : 'A' + d1 - 10),
			  (d2 <= 9 ? '0' + d2 : 'A' + d2 - 10),
			  (d3 <= 9 ? '0' + d3 : 'A' + d3 - 10),
			  (s_entry->isorec.flags[0] == 2 || 
			   omit_version_number ? "" : ";1"));
		  
#ifdef VMS
		  /* Sigh.  VAXCRTL seems to be broken here */
		  {
		    int ijk = 0;
		    while(newname[ijk]) 
		      {
			if(newname[ijk] == ' ') newname[ijk] = '0';
			ijk++;
		      }
		  }
#endif
		  
		  if(!find_file_hash(newname)) goto got_valid_name;
		}
	    }
	}

      /*
       * If we fell off the bottom here, we were in real trouble.
       */
      fprintf(stderr,"Unable to  generate unique  name for file %s\n", s_entry->name);
      exit(1);

got_valid_name:      
      /* 
       * OK, now we have a good replacement name.  Now decide which one
       * of these two beasts should get the name changed 
       */
      if(s_entry->priority < s_entry1->priority) 
	{
	  if( verbose > 0 )
	    {
	      fprintf(stderr,"Using %s for  %s%s%s (%s)\n", newname,  
		      this_dir->whole_name, SPATH_SEPARATOR, 
		      s_entry->name, s_entry1->name);
	    }
	  s_entry->isorec.name_len[0] =  strlen(newname);
	  new_reclen =  sizeof(struct iso_directory_record) -
	    sizeof(s_entry->isorec.name) +
	    strlen(newname);
	  if(use_RockRidge) 
	    {
	      if (new_reclen & 1) new_reclen++;  /* Pad to an even byte */
	      new_reclen += s_entry->rr_attr_size;
	    }
	  if (new_reclen & 1) new_reclen++;  /* Pad to an even byte */
	  s_entry->isorec.length[0] = new_reclen;
	  strcpy(s_entry->isorec.name, newname);
#ifdef APPLE_HYB
	  /* has resource fork - needs new name */
	  if (apple_both && s_entry->assoc) {
	    struct directory_entry  *s_entry2 = s_entry->assoc;

	    /* resource fork name *should* be the same as the data fork */
	    s_entry2->isorec.name_len[0] = s_entry->isorec.name_len[0];
	    strcpy(s_entry2->isorec.name, s_entry->isorec.name);
	    s_entry2->isorec.length[0] = new_reclen;
	  }
#endif /* APPLE_HYB */
	}
      else 
	{
	  delete_file_hash(s_entry1);
	  if( verbose > 0 )
	    {
	      fprintf(stderr,"Using %s for  %s%s%s (%s)\n", newname,  
		      this_dir->whole_name, SPATH_SEPARATOR, 
		      s_entry1->name, s_entry->name);
	    }
	  s_entry1->isorec.name_len[0] =  strlen(newname);
	  new_reclen =  sizeof(struct iso_directory_record) -
	    sizeof(s_entry1->isorec.name) +
	    strlen(newname);
	  if(use_RockRidge) 
	    {
	      if (new_reclen & 1) new_reclen++;  /* Pad to an even byte */
	      new_reclen += s_entry1->rr_attr_size;
	    }
	  if (new_reclen & 1) new_reclen++;  /* Pad to an even byte */
	  s_entry1->isorec.length[0] = new_reclen;
	  strcpy(s_entry1->isorec.name, newname);
	  add_file_hash(s_entry1);
#ifdef APPLE_HYB
	  /* has resource fork - needs new name */
	  if (apple_both && s_entry1->assoc) {
	    struct directory_entry  *s_entry2 = s_entry1->assoc;

	    /* resource fork name *should* be the same as the data fork */
	    s_entry2->isorec.name_len[0] = s_entry1->isorec.name_len[0];
	    strcpy(s_entry2->isorec.name, s_entry1->isorec.name);
	    s_entry2->isorec.length[0] = new_reclen;
	  }
#endif /* APPLE_HYB */
	}
      add_file_hash(s_entry);
      s_entry = s_entry->next;
    }
  
  if(generate_tables 
#ifdef APPLE_HYB
     && !find_file_hash(trans_tbl)
#else
     && !find_file_hash("TRANS.TBL") 
#endif /* APPLE_HYB */
     && (reloc_dir != this_dir)
     && (this_dir->extent == 0) )
    {
      /* 
       * First we need to figure out how big this table is 
       */
      for (s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
	{
	  if(strcmp(s_entry->name, ".") == 0  ||
	     strcmp(s_entry->name, "..") == 0) continue; 
#ifdef APPLE_HYB
	  /* skip table entry for the resource fork */
	  if(apple_both && (s_entry->isorec.flags[0] & ASSOC_FLAG))
	     continue;
#endif /* APPLE_HYB */
	  if(s_entry->de_flags & INHIBIT_ISO9660_ENTRY) continue;
	  if(s_entry->table) tablesize += 35 + strlen(s_entry->table);
	}
    }

  if( tablesize > 0 )
    {
      table = (struct directory_entry *) 
	e_malloc(sizeof (struct directory_entry));
      memset(table, 0, sizeof(struct directory_entry));
      table->table = NULL;
      table->next = this_dir->contents;
      this_dir->contents = table;
      
      table->filedir = root;
      table->isorec.flags[0] = 0;
      table->priority  = 32768;
      iso9660_date(table->isorec.date, fstatbuf.st_mtime);
      table->inode = TABLE_INODE;
      table->dev = (dev_t) UNCACHED_DEVICE;
      set_723(table->isorec.volume_sequence_number, volume_sequence_number);
      set_733((char *) table->isorec.size, tablesize);
      table->size = tablesize;
      table->filedir = this_dir;
      table->de_flags    |= INHIBIT_JOLIET_ENTRY;
      table->name = strdup("<translation table>");
      table->table = (char *) e_malloc(ROUND_UP(tablesize));
      memset(table->table, 0, ROUND_UP(tablesize));
#ifdef APPLE_HYB
      iso9660_file_length  (trans_tbl, table, 0);
#else
      iso9660_file_length  ("TRANS.TBL", table, 0);
#endif /* APPLE_HYB */
      
      if(use_RockRidge)
	{
	  fstatbuf.st_mode = 0444 | S_IFREG;
	  fstatbuf.st_nlink = 1;
	  generate_rock_ridge_attributes("",
#ifdef APPLE_HYB
					 trans_tbl, table,
#else
					 "TRANS.TBL", table,
#endif /* APPLE_HYB */
					 &fstatbuf, &fstatbuf, 0);
	}
    }
  
  /*
   * We have now chosen the 8.3 names and we should now know the length
   * of every entry in the directory.
   */
  for(s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
    {
      /* skip if it's hidden */
      if (s_entry->de_flags & INHIBIT_ISO9660_ENTRY)
	{
	  continue;
	}

      new_reclen = strlen(s_entry->isorec.name);
	  
      /*
       * First update the path table sizes for directories.
       */
      if(s_entry->isorec.flags[0] ==  2)
	{
	  if (strcmp(s_entry->name,".") && strcmp(s_entry->name,"..")) 
	    {
	      path_table_size += new_reclen + sizeof(struct iso_path_table) - 1;
	      if (new_reclen & 1) path_table_size++;
	    }
	  else 
	    {
	      new_reclen = 1;
	      if (this_dir == root && strlen(s_entry->name) == 1)
		{
		  path_table_size += sizeof(struct iso_path_table);
		}
	    }
	}
      if(path_table_size & 1) path_table_size++;  /* For odd lengths we pad */
      s_entry->isorec.name_len[0] = new_reclen;
      
      new_reclen += 
	sizeof(struct iso_directory_record) -
	sizeof(s_entry->isorec.name);
      
      if (new_reclen & 1)	
	new_reclen++;
      
      new_reclen += s_entry->rr_attr_size;
      
      if (new_reclen & 1) new_reclen++;
      
      if(new_reclen > 0xff) 
	{
	  fprintf(stderr,"Fatal error - RR overflow for file %s\n",
		  s_entry->name);
	  exit(1);
	}
      s_entry->isorec.length[0] = new_reclen;
    }

  status = sort_directory(&this_dir->contents);
  if( status > 0 )
    {
      fprintf(stderr, "Unable to sort directory %s\n", 
	      this_dir->whole_name);
    }

  /*
   * If we are filling out a TRANS.TBL, generate the entries that will
   * go in the thing.
   */
  if(table)
    {
      count = 0;
      for (s_entry = this_dir->contents; s_entry; s_entry = s_entry->next){
	if(s_entry == table) continue;
	if(!s_entry->table) continue;
	if(strcmp(s_entry->name, ".") == 0  ||
	   strcmp(s_entry->name, "..") == 0) continue;
#ifdef APPLE_HYB
	/* skip table entry for the resource fork */
	if(apple_both && (s_entry->isorec.flags[0] & ASSOC_FLAG))
	   continue;
#endif /* APPLE_HYB */
	if(s_entry->de_flags & INHIBIT_ISO9660_ENTRY) continue;
	/*
	 * Warning: we cannot use the return value of sprintf because
	 * old BSD based sprintf() implementations will return
	 * a pointer to the result instead of a count.
	 */
	sprintf(table->table + count, "%c %-34s%s",
		s_entry->table[0],
		s_entry->isorec.name, s_entry->table+1);
	count += strlen(table->table + count);
	free(s_entry->table);
	s_entry->table = NULL;
      }

      if(count !=  tablesize) 
	{
	  fprintf(stderr,"Translation table size mismatch %d %d\n",
		  count, tablesize);
	  exit(1);
	}
    }

  /* 
   * Now go through the directory and figure out how large this one will be.
   * Do not split a directory entry across a sector boundary 
   */
  s_entry = this_dir->contents;
  this_dir->ce_bytes = 0;
  while(s_entry)
    {
      /* skip if it's hidden */
      if (s_entry->de_flags & INHIBIT_ISO9660_ENTRY) {
	s_entry = s_entry->next;
	continue;
      }

      new_reclen = s_entry->isorec.length[0];
      if ((this_dir->size & (SECTOR_SIZE - 1)) + new_reclen >= SECTOR_SIZE)
	this_dir->size = (this_dir->size + (SECTOR_SIZE - 1)) & 
	~(SECTOR_SIZE - 1);
      this_dir->size += new_reclen;

      /* See if continuation entries were used on disc */
      if(use_RockRidge && 
	 s_entry->rr_attr_size != s_entry->total_rr_attr_size) 
	{
	  unsigned char * pnt;
	  int len;
	  int nbytes;
	  
	  pnt = s_entry->rr_attributes;
	  len = s_entry->total_rr_attr_size;
	  
	  /*
	   * We make sure that each continuation entry record is not
	   * split across sectors, but each file could in theory have more
	   * than one CE, so we scan through and figure out what we need. 
	   */
	  while(len > 3)
	    {
	      if(pnt[0] == 'C' && pnt[1] == 'E') 
		{
		  nbytes = get_733((char *) pnt+20);
		  
		  if((this_dir->ce_bytes & (SECTOR_SIZE - 1)) + nbytes >=
		     SECTOR_SIZE) this_dir->ce_bytes = 
				    ROUND_UP(this_dir->ce_bytes);
		  /* Now store the block in the ce buffer */
		  this_dir->ce_bytes += nbytes;
		  if(this_dir->ce_bytes & 1) this_dir->ce_bytes++;
		}
	      len -= pnt[2];
	      pnt += pnt[2];
	    }
	}
      s_entry = s_entry->next;
    }
  return status;
}

static void generate_reloc_directory()
{
	time_t current_time;
	struct directory_entry  *s_entry;

	/* Create an  entry for our internal tree */
	time (&current_time);
	reloc_dir = (struct directory *) 
		e_malloc(sizeof(struct directory));
	memset(reloc_dir, 0, sizeof(struct directory));
	reloc_dir->parent = root;
	reloc_dir->next = root->subdir;
	root->subdir = reloc_dir;
	reloc_dir->depth = 1;
	reloc_dir->whole_name = strdup("./rr_moved");
	reloc_dir->de_name =  strdup("rr_moved");
	reloc_dir->extent = 0;
	
	
	/* Now create an actual directory  entry */
	s_entry = (struct directory_entry *) 
		e_malloc(sizeof (struct directory_entry));
	memset(s_entry, 0, sizeof(struct directory_entry));
	s_entry->next = root->contents;
	reloc_dir->self = s_entry;

	/*
	 * The rr_moved entry will not appear in the Joliet tree.
	 */
	reloc_dir->dir_flags |= INHIBIT_JOLIET_ENTRY;
	s_entry->de_flags    |= INHIBIT_JOLIET_ENTRY;

	root->contents = s_entry;
	root->contents->name = strdup(reloc_dir->de_name);
	root->contents->filedir = root;
	root->contents->isorec.flags[0] = 2;
	root->contents->priority  = 32768;
	iso9660_date(root->contents->isorec.date, current_time);
	root->contents->inode = UNCACHED_INODE;
	root->contents->dev = (dev_t) UNCACHED_DEVICE;
	set_723(root->contents->isorec.volume_sequence_number, volume_sequence_number);
	iso9660_file_length (reloc_dir->de_name, root->contents, 1);

	if(use_RockRidge){
		fstatbuf.st_mode = 0555 | S_IFDIR;
		fstatbuf.st_nlink = 2;
		generate_rock_ridge_attributes("",
					       "rr_moved", s_entry,
					       &fstatbuf, &fstatbuf, 0);
	};
	
	/* Now create the . and .. entries in rr_moved */
	/* Now create an actual directory  entry */
	attach_dot_entries(reloc_dir, &root_statbuf);
}

/* 
 * Function:		attach_dot_entries
 *
 * Purpose:		Create . and .. entries for a new directory.
 *
 * Notes:		Only used for artificial directories that
 *			we are creating.
 */
static void FDECL2(attach_dot_entries, struct directory *, dirnode,
		   struct stat *, parent_stat)
{
	struct directory_entry  *s_entry;
	struct directory_entry  *orig_contents;
	int deep_flag = 0;

	init_fstatbuf();

	orig_contents = dirnode->contents;

	if( (dirnode->dir_flags & DIR_HAS_DOTDOT) == 0 )
	  {
	    s_entry = (struct directory_entry *) 
	      e_malloc(sizeof (struct directory_entry));
	    memcpy(s_entry, dirnode->self, 
		   sizeof(struct directory_entry));
	    s_entry->name = strdup("..");
	    s_entry->whole_name = NULL;
	    s_entry->isorec.name_len[0] = 1;
	    s_entry->isorec.flags[0] = 2; /* Mark as a directory */
	    iso9660_file_length ("..", s_entry, 1);
	    iso9660_date(s_entry->isorec.date, fstatbuf.st_mtime);
	    s_entry->filedir = dirnode->parent;

	    dirnode->contents = s_entry;
	    dirnode->contents->next = orig_contents;
	    orig_contents = s_entry;

	    if(use_RockRidge)
	      {
		if( parent_stat == NULL )
		  {
		    parent_stat = &fstatbuf;
		  }
		generate_rock_ridge_attributes("",
					       "..", s_entry,
					       parent_stat, 
					       parent_stat, 0);
	      }
	    dirnode->dir_flags |= DIR_HAS_DOTDOT;
	  }

	if( (dirnode->dir_flags & DIR_HAS_DOT) == 0 )
	  {
	    s_entry = (struct directory_entry *) 
	      e_malloc(sizeof (struct directory_entry));
	    memcpy(s_entry, dirnode->self, 
		   sizeof(struct directory_entry));
	    s_entry->name = strdup(".");
	    s_entry->whole_name = NULL;
	    s_entry->isorec.name_len[0] = 1;
	    s_entry->isorec.flags[0] = 2; /* Mark as a directory */
	    iso9660_file_length (".", s_entry, 1);
	    iso9660_date(s_entry->isorec.date, fstatbuf.st_mtime);
	    s_entry->filedir = dirnode;
	    
	    dirnode->contents = s_entry;
	    dirnode->contents->next = orig_contents;

	    if(use_RockRidge)
	      {
		fstatbuf.st_mode = 0555 | S_IFDIR;
		fstatbuf.st_nlink = 2;
		
		if( dirnode == root )
		  {
		    deep_flag |= NEED_CE | NEED_SP;  /* For extension record */
		  }
		
		generate_rock_ridge_attributes("",
					       ".", s_entry,
					       &fstatbuf, &fstatbuf, deep_flag);
	      }
	    
	    dirnode->dir_flags |= DIR_HAS_DOT;
	  }

}

static void FDECL2(update_nlink, struct directory_entry *, s_entry, int, value)
{
    unsigned char * pnt;
    int len;
    
    pnt = s_entry->rr_attributes;
    len = s_entry->total_rr_attr_size;
    while(len)
    {
	if(pnt[0] == 'P' && pnt[1] == 'X') 
	{
	    set_733((char *) pnt+12, value);
	    break;
	}
	len -= pnt[2];
	pnt += pnt[2];
    }
}

static void FDECL1(increment_nlink, struct directory_entry *, s_entry)
{
    unsigned char * pnt;
    int len, nlink;
    
    pnt = s_entry->rr_attributes;
    len = s_entry->total_rr_attr_size;
    while(len)
    {
	if(pnt[0] == 'P' && pnt[1] == 'X') 
	{
	    nlink =  get_733((char *) pnt+12);
	    set_733((char *) pnt+12, nlink+1);
	    break;
	}
	len -= pnt[2];
	pnt += pnt[2];
    }
}

void finish_cl_pl_entries(){
  struct directory_entry  *s_entry, *s_entry1;
  struct directory *  d_entry;

  /* if the reloc_dir is hidden (empty), then return */
  if (reloc_dir->dir_flags & INHIBIT_ISO9660_ENTRY)
    return;

  s_entry = reloc_dir->contents;
   s_entry  = s_entry->next->next;  /* Skip past . and .. */
  for(; s_entry; s_entry = s_entry->next){
	  /* skip if it's hidden */
	  if(s_entry->de_flags & INHIBIT_ISO9660_ENTRY) {
	    continue;
	  }
	  d_entry = reloc_dir->subdir;
	  while(d_entry){
		  if(d_entry->self == s_entry) break;
		  d_entry = d_entry->next;
	  };
	  if(!d_entry){
		  fprintf(stderr,"Unable to locate directory parent\n");
		  exit(1);
	  };

	  /* First fix the PL pointer in the directory in the rr_reloc dir */
	  s_entry1 = d_entry->contents->next;
	  set_733((char *) s_entry1->rr_attributes +  s_entry1->total_rr_attr_size - 8,
		  s_entry->filedir->extent);

	  /* Now fix the CL pointer */
	  s_entry1 = s_entry->parent_rec;

	  set_733((char *) s_entry1->rr_attributes +  s_entry1->total_rr_attr_size - 8,
		  d_entry->extent);

	  s_entry->filedir = reloc_dir;  /* Now we can fix this */
  }
  /* Next we need to modify the NLINK terms in the assorted root directory records
     to account for the presence of the RR_MOVED directory */

  increment_nlink(root->self);
  increment_nlink(root->self->next);
  d_entry = root->subdir;
  while(d_entry){
    increment_nlink(d_entry->contents->next);
    d_entry = d_entry->next;
  };
}

/* 
 * Function:		scan_directory_tree
 *
 * Purpose:		Walk through a directory on the local machine
 *			filter those things we don't want to include
 *			and build our representation of a dir.
 *
 * Notes:
 */
int
FDECL3(scan_directory_tree,struct directory *, this_dir,
       char *, path, 
       struct directory_entry *, de)
{
  DIR				* current_dir;
  char				  whole_path[1024];
  struct dirent			* d_entry;
  struct directory		* parent;
  int				  dflag;
  char				* old_path;

    if (verbose > 1)
    {
      fprintf(stderr, "Scanning %s\n", path);
    }

  current_dir = opendir(path);
  d_entry = NULL;

  /* Apparently NFS sometimes allows you to open the directory, but
     then refuses to allow you to read the contents.  Allow for this */

  old_path = path;

  if(current_dir) d_entry = readdir(current_dir);

  if(!current_dir || !d_entry) 
    {
      fprintf(stderr,"Unable to open directory %s\n", path);
      de->isorec.flags[0] &= ~2; /* Mark as not a directory */
      if(current_dir) closedir(current_dir);
      return 0;
    }

  parent = de->filedir;
  /* Set up the struct for the current directory, and insert it into the
     tree */

#ifdef VMS
  vms_path_fixup(path);
#endif

  /*
   * if entry for this sub-directory is hidden, then hide this directory
   */
  if (de->de_flags & INHIBIT_ISO9660_ENTRY)
    this_dir->dir_flags |= INHIBIT_ISO9660_ENTRY;
  
  if (de->de_flags & INHIBIT_JOLIET_ENTRY)
    this_dir->dir_flags |= INHIBIT_JOLIET_ENTRY;
  
  /* 
   * Now we scan the directory itself, and look at what is inside of it. 
   */
  dflag = 0;
  while(1==1){

    /* The first time through, skip this, since we already asked for
       the first entry when we opened the directory. */
    if(dflag) d_entry = readdir(current_dir);
    dflag++;

    if(!d_entry) break;

    /* OK, got a valid entry */

    /* If we do not want all files, then pitch the backups. */
    if(!all_files){
	    if(   strchr(d_entry->d_name,'~')
	       || strchr(d_entry->d_name,'#'))
	      {
		if( verbose > 0 )
		  {
		    fprintf(stderr, "Ignoring file %s\n", d_entry->d_name);
		  }
		continue;
	      }
    }

#ifdef APPLE_HYB
    if (apple_both) {
      /* exclude certain HFS type files/directories for the time being */
      if (hfs_exclude(d_entry->d_name))
	continue;
    }
#endif /* APPLE_HYB */

    if(strlen(path)+strlen(d_entry->d_name) + 2 > sizeof(whole_path)){
      fprintf(stderr, "Overflow of stat buffer\n");
      exit(1);
    };

    /* Generate the complete ASCII path for this file */
    strcpy(whole_path, path);
#ifndef VMS
    if(whole_path[strlen(whole_path)-1] != '/')
      strcat(whole_path, "/");
#endif
    strcat(whole_path, d_entry->d_name);

    /** Should we exclude this file ? */
    if (matches(d_entry->d_name) || matches(whole_path)) {
      if (verbose > 1) {
	fprintf(stderr, "Excluded by match: %s\n", whole_path);
      }
      continue;
    }

    if(    generate_tables 
#ifdef APPLE_HYB
	&& strcmp(d_entry->d_name, trans_tbl) == 0 )
#else
	&& strcmp(d_entry->d_name, "TRANS.TBL") == 0 )
#endif /* APPLE_HYB */
      {
	/*
	 * Ignore this entry.  We are going to be generating new
	 * versions of these files, and we need to ignore any
	 * originals that we might have found.
	 */
	if (verbose > 1) 
	  {
	    fprintf(stderr, "Excluded: %s\n",whole_path);
	  }
	continue;
      }

    /*
     * If we already have a '.' or a '..' entry, then don't
     * insert new ones.
     */
    if( strcmp(d_entry->d_name, ".") == 0 
	&& this_dir->dir_flags & DIR_HAS_DOT )
      {
	continue;
      }

    if( strcmp(d_entry->d_name, "..") == 0 
	&& this_dir->dir_flags & DIR_HAS_DOTDOT )
      {
	continue;
      }

#if 0
    if (verbose > 1)  fprintf(stderr, "%s\n",whole_path);
#endif
    /*
     * This actually adds the entry to the directory in question.
     */
#ifdef APPLE_HYB
    insert_file_entry(this_dir, whole_path, d_entry->d_name, 0);
#else
    insert_file_entry(this_dir, whole_path, d_entry->d_name);
#endif /* APPLE_HYB */
  }
  closedir(current_dir);
  
#ifdef APPLE_HYB
  /* if we cached the HFS info stuff for this directory, then delete it */
  if (this_dir->hfs_info) {
    del_hfs_info(this_dir->hfs_info);
    this_dir->hfs_info = 0;
  }
#endif /* APPLE_HYB */

  return 1;
}


/* 
 * Function:		insert_file_entry
 *
 * Purpose:		Insert one entry into our directory node.
 *
 * Note:
 * This function inserts a single entry into the directory.  It
 * is assumed that all filtering and decision making regarding what
 * we want to include has already been made, so the purpose of this
 * is to insert one entry (file, link, dir, etc), into this directory.
 * Note that if the entry is a dir (or if we are following links,
 * and the thing it points to is a dir), then we will scan those
 * trees before we return.
 */
#ifdef APPLE_HYB
int
FDECL4(insert_file_entry,struct directory *, this_dir,
       char *, whole_path,
       char *, short_name,
       int, have_rsrc)
#else
int
FDECL3(insert_file_entry,struct directory *, this_dir,
       char *, whole_path,
       char *, short_name)
#endif /* APPLE_HYB */
{
  struct stat			  statbuf, lstatbuf;
  struct directory_entry	* s_entry, *s_entry1;
  int				  lstatus;
  int				  status;
  int				  deep_flag;
#ifdef APPLE_HYB
  int				  x_hfs = 0;
  int				  htype = 0;
#endif /* APPLE_HYB */

  status = stat_filter(whole_path, &statbuf);

  lstatus = lstat_filter(whole_path, &lstatbuf);

  if( (status == -1) && (lstatus == -1) )
    {
      /*
       * This means that the file doesn't exist, or isn't accessible.
       * Sometimes this is because of NFS permissions problems.
       */
      fprintf(stderr, "Non-existant or inaccessible: %s\n",whole_path);
      return 0;
    }
  
  if(this_dir == root && strcmp(short_name, ".") == 0)
    root_statbuf = statbuf;  /* Save this for later on */
  
  /* We do this to make sure that the root entries are consistent */
  if(this_dir == root && strcmp(short_name, "..") == 0) 
    {
      statbuf = root_statbuf;
      lstatbuf = root_statbuf;
    }

  if(S_ISLNK(lstatbuf.st_mode))
    {
      
      /* Here we decide how to handle the symbolic links.  Here
	 we handle the general case - if we are not following
	 links or there is an error, then we must change
	 something.  If RR is in use, it is easy, we let RR
	 describe the file.  If not, then we punt the file. */
      
      if((status || !follow_links))
	{
	  if(use_RockRidge)
	    {
	      status = 0;
	      statbuf.st_size = 0;
	      STAT_INODE(statbuf) = UNCACHED_INODE;
	      statbuf.st_dev = (dev_t) UNCACHED_DEVICE;
	      statbuf.st_mode = (statbuf.st_mode & ~S_IFMT) | S_IFREG;
	    } else {
	      if(follow_links) 
		{
		  fprintf(stderr,
			  "Unable to stat file %s - ignoring and continuing.\n",
			  whole_path);
		}
	      else
		{
		  fprintf(stderr,
			  "Symlink %s ignored - continuing.\n",
			  whole_path);
		  return 0;  /* Non Rock Ridge discs - ignore all symlinks */
		}
	    }
	}
      
      /* Here we handle a different kind of case.  Here we have
	 a symlink, but we want to follow symlinks.  If we run
	 across a directory loop, then we need to pretend that
	 we are not following symlinks for this file.  If this
	 is the first time we have seen this, then make this
	 seem as if there was no symlink there in the first
	 place */
      
      if( follow_links
	  && S_ISDIR(statbuf.st_mode) ) 
	{
	  if(   strcmp(short_name, ".")
		&& strcmp(short_name, "..") )
	    {
	      if(find_directory_hash(statbuf.st_dev, STAT_INODE(statbuf)))
		{
		  if(!use_RockRidge) 
		    {
		      fprintf(stderr, "Already cached directory seen (%s)\n", 
			      whole_path);
		      return 0;
		    }
		  statbuf.st_size = 0;
		  STAT_INODE(statbuf) = UNCACHED_INODE;
		  statbuf.st_dev = (dev_t) UNCACHED_DEVICE;
		  statbuf.st_mode = (statbuf.st_mode & ~S_IFMT) | S_IFREG;
		}
	      else 
		{
		  lstatbuf = statbuf;
		  add_directory_hash(statbuf.st_dev, STAT_INODE(statbuf));
		}
	    }
	}
      
      /*
       * For non-directories, we just copy the stat information over
       * so we correctly include this file.
       */
      if( follow_links
	  && !S_ISDIR(statbuf.st_mode) ) 
	{
	  lstatbuf = statbuf;
	}
    }
  
  /*
   * Add directories to the cache so that we don't waste space even
   * if we are supposed to be following symlinks.
   */
  if( follow_links
      && strcmp(short_name, ".")
      && strcmp(short_name, "..")
      && S_ISDIR(statbuf.st_mode) ) 
    {
      add_directory_hash(statbuf.st_dev, STAT_INODE(statbuf));
    }
#ifdef VMS
  if(!S_ISDIR(lstatbuf.st_mode) && (statbuf.st_fab_rfm != FAB$C_FIX && 
				    statbuf.st_fab_rfm != FAB$C_STMLF)) {
    fprintf(stderr,"Warning - file %s has an unsupported VMS record"
	    " format (%d)\n",
	    whole_path, statbuf.st_fab_rfm);
  }
#endif
  
  if(S_ISREG(lstatbuf.st_mode) && (status = access(whole_path, R_OK)))
    {
      fprintf(stderr, "File %s is not readable (errno = %d) - ignoring\n", 
	      whole_path, errno);
      return 0;
    }
  
  /* Add this so that we can detect directory loops with hard links.
     If we are set up to follow symlinks, then we skip this checking. */
  if(   !follow_links 
	&& S_ISDIR(lstatbuf.st_mode) 
	&& strcmp(short_name, ".") 
	&& strcmp(short_name, "..") ) 
    {
      if(find_directory_hash(statbuf.st_dev, STAT_INODE(statbuf))) {
#ifdef APPLE_HYB
	/* NON-HFS change - print just a warning *if* this ever happens */
	fprintf(stderr,"Warning: Directory loop (%s)\n", whole_path);
#else
	fprintf(stderr,"Directory loop - fatal goof (%s %lx %llu).\n",
		whole_path, (unsigned long) statbuf.st_dev,
		(unsigned long long) STAT_INODE(statbuf));
	exit(1);
#endif
      }
      add_directory_hash(statbuf.st_dev, STAT_INODE(statbuf));
    }
  
  if (!S_ISCHR(lstatbuf.st_mode) && !S_ISBLK(lstatbuf.st_mode) &&
      !S_ISFIFO(lstatbuf.st_mode) && !S_ISSOCK(lstatbuf.st_mode)
      && !S_ISLNK(lstatbuf.st_mode) && !S_ISREG(lstatbuf.st_mode) &&
      !S_ISDIR(lstatbuf.st_mode)) {
    fprintf(stderr,"Unknown file type %s - ignoring and continuing.\n",
	    whole_path);
    return 0;
  }
  
  /* Who knows what trash this is - ignore and continue */
  
  if(status) 
    {
      fprintf(stderr,
	      "Unable to stat file %s - ignoring and continuing.\n",
	      whole_path);
      return 0; 
    }
  
  /*
   * Check to see if we have already seen this directory node.
   * If so, then we don't create a new entry for it, but we do want
   * to recurse beneath it and add any new files we do find.
   */
  if (S_ISDIR(statbuf.st_mode)) 
    {
      int dflag;
      
      for( s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
	{
	  if( strcmp(s_entry->name, short_name) == 0 )
	    {
	      break;
	    }
	}
      if ( s_entry != NULL 
	   && strcmp(short_name,".") 
	   && strcmp(short_name,"..")) 
	{
	  struct directory * child;
	  
	  if ( (s_entry->de_flags & RELOCATED_DIRECTORY) != 0)
	    {
	      for( s_entry = reloc_dir->contents; s_entry; s_entry = s_entry->next)
		{
		  if( strcmp(s_entry->name, short_name) == 0 )
		    {
		      break;
		    }
		}
	      child = find_or_create_directory(reloc_dir, whole_path, 
					       s_entry, 1);
	    }
	  else
	    {
	      child = find_or_create_directory(this_dir, whole_path, 
					       s_entry, 1);
	      /* If unable to scan directory, mark this as a non-directory */
	    }
	  dflag = scan_directory_tree(child, whole_path, s_entry);
	  if(!dflag)
	    {
	      lstatbuf.st_mode = (lstatbuf.st_mode & ~S_IFMT) | S_IFREG;
	    }
	  return 0;
	}
    }
#ifdef APPLE_HYB 
    /* Should we exclude this HFS file ? - only works with -hfs */
    if (!have_rsrc && apple_hyb && strcmp(short_name,".") && strcmp(short_name,"..")) {
      x_hfs = hfs_matches(short_name) || hfs_matches(whole_path);
      if (x_hfs) {
	if (verbose > 1) {
	  fprintf(stderr, "Hidden from HFS tree: %s\n", whole_path);
	}
      }
    }

    /* check we are a file, using Apple extensions and have a .resource part
	and not excluded */
    if (S_ISREG(lstatbuf.st_mode) && !have_rsrc && apple_both && !x_hfs) {
      char	rsrc_path[1024];	/* rsrc fork filename */

      /* construct the resource full path */
      htype = get_hfs_rname(whole_path, short_name, rsrc_path);
      /* check we can read the resouce fork */
      if (htype) {
	struct stat	rstatbuf, rlstatbuf;

	/* some further checks on the file */
	status = stat_filter(rsrc_path, &rstatbuf);

	lstatus = lstat_filter(rsrc_path, &rlstatbuf);

	if(!status && !lstatus && S_ISREG(rstatbuf.st_mode) && rstatbuf.st_size > 0) {
	  /* have a resource file - insert it into the current directory
	     but flag that we have a resource fork */
	  insert_file_entry(this_dir, rsrc_path, short_name, htype);
	}
      }
    }
#endif /* APPLE_HYB */
  
  s_entry = (struct directory_entry *) 
    e_malloc(sizeof (struct directory_entry));
  /* memset the whole struct, not just the isorec.extent part JCP */
  memset(s_entry, 0, sizeof (struct directory_entry));
  s_entry->next = this_dir->contents;
/*memset(s_entry->isorec.extent, 0, 8); */
  this_dir->contents = s_entry;
  deep_flag = 0;
  s_entry->table = NULL;
  
  s_entry->name = strdup(short_name);
  s_entry->whole_name = strdup (whole_path);

  s_entry->de_flags = 0;

  /*
   * If the current directory is hidden, then hide all it's members
   * otherwise check if this entry needs to be hidden as well */
  if (this_dir->dir_flags & INHIBIT_ISO9660_ENTRY) {
    s_entry->de_flags |= INHIBIT_ISO9660_ENTRY;
  }
  else if (strcmp(short_name,".") && strcmp(short_name,"..")) {
    if (i_matches(short_name) || i_matches(whole_path)) {
      if (verbose > 1) {
	fprintf(stderr, "Hidden from ISO9660 tree: %s\n", whole_path);
      }
      s_entry->de_flags |= INHIBIT_ISO9660_ENTRY;
    }
  }

  if (this_dir != reloc_dir && this_dir->dir_flags & INHIBIT_JOLIET_ENTRY) {
    s_entry->de_flags |= INHIBIT_JOLIET_ENTRY;
  }
  else if (strcmp(short_name,".") && strcmp(short_name,"..")) {
    if (j_matches(short_name) || j_matches(whole_path)) {
      if (verbose > 1) {
	fprintf(stderr, "Hidden from Joliet tree: %s\n", whole_path);
      }
      s_entry->de_flags |= INHIBIT_JOLIET_ENTRY;
    }
  }

  s_entry->filedir = this_dir;
  s_entry->isorec.flags[0] = 0;
  s_entry->isorec.ext_attr_length[0] = 0;
  iso9660_date(s_entry->isorec.date, statbuf.st_mtime);
  s_entry->isorec.file_unit_size[0] = 0;
  s_entry->isorec.interleave[0] = 0;

#ifdef APPLE_HYB
  if (apple_both && !x_hfs) {
    s_entry->hfs_ent = NULL;
    s_entry->assoc = NULL;
    s_entry->hfs_off = 0;
    s_entry->hfs_type = htype;
    if (have_rsrc) {
      s_entry->isorec.flags[0] = ASSOC_FLAG;	/* associated (rsrc) file */
      /* set the type of HFS file */
      s_entry->hfs_type = have_rsrc;
      /* don't want the rsrc file to be included in any Joliet tree */
      s_entry->de_flags |= INHIBIT_JOLIET_ENTRY;
    }
    else if (s_entry->next) {
      /* if previous entry is an associated file, then "link" it
	 to this file i.e. we have a data/resource pair */
      if (s_entry->next->isorec.flags[0] & ASSOC_FLAG) {
	s_entry->assoc = s_entry->next;
	/* share the same HFS parameters */
	s_entry->hfs_ent = s_entry->next->hfs_ent;
	s_entry->hfs_type = s_entry->next->hfs_type;
      }
    }
    /* allocate HFS entry if required */
    if (apple_both && strcmp(short_name, ".") && strcmp(short_name, "..")) {
      if (!s_entry->hfs_ent) {
	hfsdirent *hfs_ent;
	hfs_ent = (hfsdirent *)e_malloc(sizeof(hfsdirent));

	/* fill in the defaults */
	hfs_ent->flags = hfs_ent->fdflags = 0;
	hfs_ent->crdate = lstatbuf.st_ctime;
	hfs_ent->mddate = lstatbuf.st_mtime;
	hfs_ent->dsize = hfs_ent->rsize = 0;
	s_entry->hfs_ent = hfs_ent;
      }
      if (have_rsrc)
	/* set rsrc size */
	s_entry->hfs_ent->rsize = lstatbuf.st_size;
      else
	/* set data size */
	s_entry->hfs_ent->dsize = lstatbuf.st_size;
    }
  }
#endif /* APPLE_HYB */

  if( strcmp(short_name,  ".") == 0)
    {
      this_dir->dir_flags |= DIR_HAS_DOT;
    } 

  if( strcmp(short_name,  "..") == 0)
    {
      this_dir->dir_flags |= DIR_HAS_DOTDOT;
    } 

  if(   this_dir->parent 
     && this_dir->parent == reloc_dir 
     && strcmp(short_name,  "..") == 0)
    {
      s_entry->inode = UNCACHED_INODE;
      s_entry->dev = (dev_t) UNCACHED_DEVICE;
      deep_flag  = NEED_PL;
    } 
  else
#ifdef APPLE_HYB
    if (have_rsrc) {
      /* don't want rsrc files to be cached */
      s_entry->inode = UNCACHED_INODE;
      s_entry->dev = (dev_t) UNCACHED_DEVICE;
    }
  else
#endif /* APPLE_HYB */
    {
      s_entry->inode = STAT_INODE(statbuf);
      s_entry->dev = statbuf.st_dev;
    }
  set_723(s_entry->isorec.volume_sequence_number, volume_sequence_number);
  iso9660_file_length(short_name, s_entry, S_ISDIR(statbuf.st_mode));
  s_entry->rr_attr_size = 0;
  s_entry->total_rr_attr_size = 0;
  s_entry->rr_attributes = NULL;
  
  /* Directories are assigned sizes later on */
  if (!S_ISDIR(statbuf.st_mode)) 
    {
      if (S_ISCHR(lstatbuf.st_mode) || S_ISBLK(lstatbuf.st_mode) || 
	  S_ISFIFO(lstatbuf.st_mode) || S_ISSOCK(lstatbuf.st_mode)
	  || S_ISLNK(lstatbuf.st_mode))
	{
	  s_entry->size = 0; 
	  statbuf.st_size = 0; 
	}
      else
	{
	  s_entry->size = statbuf.st_size; 
	}

      set_733((char *) s_entry->isorec.size, statbuf.st_size); 
    } 
  else
    {
      s_entry->isorec.flags[0] = 2;
    }
#ifdef APPLE_HYB
  /* if the directory is HFS excluded, then we don't have an hfs_ent */
  if (apple_both && s_entry->hfs_ent && s_entry->isorec.flags[0] & 2) {
    /* get the Mac directory name */
    get_hfs_dir(whole_path, short_name, s_entry);

    /* if required, set ISO directory name from HFS name */
    if (mac_name)
      iso9660_file_length(s_entry->hfs_ent->name, s_entry, 1);
  }
#endif /* APPLE_HYB */
  
  if (strcmp(short_name,".") && strcmp(short_name,"..") && 
      S_ISDIR(statbuf.st_mode) && this_dir->depth >  RR_relocation_depth)
    {
      struct directory * child;

      if(!reloc_dir) generate_reloc_directory();
      
      /*
       * Replicate the entry for this directory.  The old one will stay where it
       * is, and it will be neutered so that it no longer looks like a directory.
       * The new one will look like a directory, and it will be put in the reloc_dir.
       */
      s_entry1 = (struct directory_entry *) 
	e_malloc(sizeof (struct directory_entry));
      memcpy(s_entry1, s_entry,  sizeof(struct directory_entry));
      s_entry1->table = NULL;
      s_entry1->name = strdup(this_dir->contents->name);
      s_entry1->whole_name = strdup(this_dir->contents->whole_name);
      s_entry1->next = reloc_dir->contents;
      reloc_dir->contents = s_entry1;
      s_entry1->priority  =  32768;
      s_entry1->parent_rec = this_dir->contents;
      
      deep_flag = NEED_RE;
      
      if(use_RockRidge) 
	{
	  generate_rock_ridge_attributes(whole_path,
					 short_name, s_entry1,
					 &statbuf, &lstatbuf, deep_flag);
	}
      
      deep_flag = 0;
      
      /* We need to set this temporarily so that the parent to this
	 is correctly determined. */
      s_entry1->filedir = reloc_dir;
      child = find_or_create_directory(reloc_dir, whole_path, 
				       s_entry1, 0);
      scan_directory_tree(child, whole_path, s_entry1);
      s_entry1->filedir = this_dir;
      
      statbuf.st_size = 0;
      statbuf.st_mode &= 0777;
      set_733((char *) s_entry->isorec.size, 0);
      s_entry->size = 0;
      s_entry->isorec.flags[0] = 0;
      s_entry->inode = UNCACHED_INODE;
      s_entry->de_flags |= RELOCATED_DIRECTORY;
      deep_flag = NEED_CL;
    }
  
  if(generate_tables 
     && strcmp(s_entry->name, ".") 
     && strcmp(s_entry->name, "..")) 
    {
      char  buffer[2048];
      int nchar;
      switch(lstatbuf.st_mode & S_IFMT)
	{
	case S_IFDIR:
	  snprintf(buffer, sizeof buffer, "D\t%s\n",
		  s_entry->name);
	  break;
#ifdef S_IFBLK
/* extra for WIN32 - if it doesn't have the major/minor defined, then
   S_IFBLK and S_IFCHR type files are unlikely to exist anyway ...
   code similar to that in rock.c */

/* for some reason, MAJOR_IN_SYSMACROS isn't defined on a SunOS when
   it should be, so see if major() is defined instead  */
/*
#if !(defined(MAJOR_IN_SYSMACROS) || defined(MAJOR_IN_MKDEV))
*/
#ifndef major
#define major(dev) (sizeof(dev_t) <= 2 ? ((dev) >> 8) : \
	(sizeof(dev_t) <= 4 ? (((dev) >> 8) >> 8) : \
	(((dev) >> 16) >> 16)))
#define minor(dev) (sizeof(dev_t) <= 2 ? (dev) & 0xff : \
	(sizeof(dev_t) <= 4 ? (dev) & 0xffff : \
	(dev) & 0xffffffff))
#endif
	case S_IFBLK:
	  snprintf(buffer, sizeof buffer, "B\t%s\t%lu %lu\n",
		  s_entry->name,
		  (unsigned long) major(statbuf.st_rdev),
		  (unsigned long) minor(statbuf.st_rdev));
	  break;
#endif
#ifdef S_IFIFO
	case S_IFIFO:
	  snprintf(buffer, sizeof buffer, "P\t%s\n",
		  s_entry->name);
	  break;
#endif
#ifdef S_IFCHR
	case S_IFCHR:
	  snprintf(buffer, sizeof buffer, "C\t%s\t%lu %lu\n",
		  s_entry->name,
		  (unsigned long) major(statbuf.st_rdev),
		  (unsigned long) minor(statbuf.st_rdev));
	  break;
#endif
#ifdef S_IFLNK
	case S_IFLNK:
	  nchar = readlink(whole_path, 
			   (char *)symlink_buff, 
			   sizeof(symlink_buff)-1);
	  symlink_buff[nchar < 0 ? 0 : nchar] = 0;
	  snprintf(buffer, sizeof buffer, "L\t%s\t%s\n",
		  s_entry->name, symlink_buff);
	  break;
#endif
#ifdef S_IFSOCK
	case S_IFSOCK:
	  snprintf(buffer, sizeof buffer, "S\t%s\n",
		  s_entry->name);
	  break;
#endif
	case S_IFREG:
	default:
	  snprintf(buffer, sizeof buffer, "F\t%s\n",
		  s_entry->name);
	  break;
	};
      s_entry->table = strdup(buffer);
    }
  
  if(S_ISDIR(statbuf.st_mode))
    {
      int dflag;
      if (strcmp(short_name,".") && strcmp(short_name,"..")) 
	{
	  struct directory * child;
	  
	  child = find_or_create_directory(this_dir, whole_path, 
					   s_entry, 1);
	  dflag = scan_directory_tree(child, whole_path, s_entry);
	  
	  if(!dflag)
	    {
	      lstatbuf.st_mode = (lstatbuf.st_mode & ~S_IFMT) | S_IFREG;
	      if( child->contents == NULL )
		{
		  delete_directory(this_dir, child);
		}
	    }
	}
      /* If unable to scan directory, mark this as a non-directory */
    }
  
  if(use_RockRidge && this_dir == root && strcmp(s_entry->name, ".")  == 0)
    {
      deep_flag |= NEED_CE | NEED_SP;  /* For extension record */
    }
  
  /* Now figure out how much room this file will take in the
     directory */
  
#ifdef APPLE_HYB
    /* if the file is HFS excluded, then we don't have an hfs_ent */
    if (apple_both && !have_rsrc && s_entry->hfs_ent) {
      if (S_ISREG(lstatbuf.st_mode)) {	/* it's a regular file */

	/* fill in the rest of the HFS entry */
	get_hfs_info(whole_path, short_name, s_entry);

	/* if required, set ISO directory name from HFS name */ 
	if (mac_name)
	  iso9660_file_length(s_entry->hfs_ent->name, s_entry, 0); 

	/* print details about the HFS file */
	if (verbose > 2)
	  print_hfs_info(s_entry);

	/* copy the new ISO9660 name to the rsrc fork - if it exists */
	if (s_entry->assoc)
	  strcpy(s_entry->assoc->isorec.name, s_entry->isorec.name);

	/* we can't handle hard links in the hybrid case, so we "uncache"
	   the file. The downside to this is that hard linked files
	   are added to the output image more than once (we've already
	   done this for rsrc files) */
	if (apple_hyb) {
	  s_entry->inode = UNCACHED_INODE;
	  s_entry->dev = (dev_t) UNCACHED_DEVICE;
	}
      }
      else if (!(s_entry->isorec.flags[0] & 2)) { /* not a directory .. */

	/* no mac equivalent, so ignore - have to be careful here, the
	   hfs_ent may be also be for a relocated directory */
	if (s_entry->hfs_ent && !(s_entry->de_flags & RELOCATED_DIRECTORY))
	    free(s_entry->hfs_ent);
	s_entry->hfs_ent = NULL;
      }

      /* if the rsrc size is zero, then we don't need the entry, so we
	 might as well delete it - this will only happen if we didn't
	 know the rsrc size from the rsrc file size */
      if(s_entry->assoc && s_entry->assoc->size == 0)
	 delete_rsrc_ent(s_entry);
    }

    if(apple_ext && s_entry->assoc) {
	/* need Apple extensions for the resource fork as well */
	generate_rock_ridge_attributes(whole_path,
					   short_name, s_entry->assoc,
					   &statbuf, &lstatbuf, deep_flag);
    }
    /* leave out resource fork for the time being */
    if (use_RockRidge && !have_rsrc) {
#else
    if(use_RockRidge) 
    {
#endif /* APPLE_HYB */
      generate_rock_ridge_attributes(whole_path,
				     short_name, s_entry,
				     &statbuf, &lstatbuf, deep_flag);
      
    }
  
  return 1;
}


void FDECL2(generate_iso9660_directories, struct directory *, node, FILE*, outfile){
  struct directory * dpnt;

  dpnt = node;

  while (dpnt){
    if( dpnt->extent > session_start )
      {
	generate_one_directory(dpnt, outfile);
      }
    if(dpnt->subdir) generate_iso9660_directories(dpnt->subdir, outfile);
    dpnt = dpnt->next;
  }
}

/*
 * Function:	find_or_create_directory
 *
 * Purpose:	Locate a directory entry in the tree, create if needed.
 *
 * Arguments:
 */
struct directory * FDECL4(find_or_create_directory, struct directory *, parent,
			  const char *, path,
			  struct directory_entry *, de, int, flag)
{
  struct directory		* dpnt;
  struct directory_entry	* orig_de;
  struct directory		* next_brother;
  const char                    * cpnt;
  const char			* pnt;

  orig_de = de;

  pnt = strrchr(path, PATH_SEPARATOR);
  if( pnt == NULL )
    {
      pnt = path;
    }
  else
    {
      pnt++;
    }

  if( parent != NULL )
    {
      dpnt = parent->subdir;

      while (dpnt)
	{
	  /*
	   * Weird hack time - if there are two directories by the
	   * same name in the reloc_dir, they are not treated as the
	   * same thing unless the entire path matches completely.
	   */
	  if( flag && strcmp(dpnt->de_name, pnt) == 0 )
	    {
	      return dpnt;
	    }
	  dpnt = dpnt->next;
	}
    }

  /*
   * We don't know if we have a valid directory entry for this one
   * yet.  If not, we need to create one.
   */
  if( de == NULL )
    {
      de = (struct directory_entry *) 
	e_malloc(sizeof (struct directory_entry));
      memset(de, 0, sizeof(struct directory_entry));
      de->next            = parent->contents;
      parent->contents    = de;
      de->name            = strdup(pnt);
      de->filedir         = parent;
      de->isorec.flags[0] = 2;
      de->priority        = 32768;
      de->inode           = UNCACHED_INODE;
      de->dev             = (dev_t) UNCACHED_DEVICE;
      set_723(de->isorec.volume_sequence_number, volume_sequence_number);
      iso9660_file_length (pnt, de, 1);

      init_fstatbuf();
      /*
       * It doesn't exist for real, so we cannot add any Rock Ridge.
       */
      if(use_RockRidge)
	{
	  fstatbuf.st_mode = 0555 | S_IFDIR;
	  fstatbuf.st_nlink = 2;
	  generate_rock_ridge_attributes("",
					 (char *) pnt, de,
					 &fstatbuf, 
					 &fstatbuf, 0);
	}
      iso9660_date(de->isorec.date, fstatbuf.st_mtime);
#ifdef APPLE_HYB
      if (apple_both) {
	/* give the directory an HFS entry */
	hfsdirent *hfs_ent;
	hfs_ent = (hfsdirent *)e_malloc(sizeof(hfsdirent));

	/* fill in the defaults */
	hfs_ent->flags = hfs_ent->fdflags = 0;
	hfs_ent->crdate = fstatbuf.st_ctime;
	hfs_ent->mddate = fstatbuf.st_mtime;
	hfs_ent->dsize = hfs_ent->rsize = 0;

	de->hfs_ent = hfs_ent;

	/* get the Mac directory name */
	get_hfs_dir(path, pnt, de);
      }
#endif /* APPLE_HYB */
    }

  /*
   * If we don't have a directory for this one yet, then allocate it
   * now, and patch it into the tree in the appropriate place.
   */
  dpnt             = (struct directory *) e_malloc(sizeof(struct directory));
  memset(dpnt, 0, sizeof(struct directory));
  dpnt->next       = NULL;
  dpnt->subdir     = NULL;
  dpnt->self       = de;
  dpnt->contents   = NULL;
  dpnt->whole_name = strdup(path);
  cpnt             = strrchr(path, PATH_SEPARATOR);
  if(cpnt)
    cpnt++;
  else
    cpnt = path;
  dpnt->de_name    = strdup(cpnt);
  dpnt->size       = 0;
  dpnt->extent     = 0;
  dpnt->jextent    = 0;
  dpnt->jsize      = 0;
#ifdef APPLE_HYB
  dpnt->hfs_ent	   = de->hfs_ent;
#endif /*APPLE_HYB */

  if( orig_de == NULL )
    {
      struct stat xstatbuf;
      int sts;

      /*
       * Now add a . and .. entry in the directory itself.
       * This is a little tricky - if the real directory
       * exists, we need to stat it first.  Otherwise, we
       * use the fictitious fstatbuf which points to the time
       * at which mkisofs was started.
       */
      sts = stat_filter(parent->whole_name, &xstatbuf);
      if( sts == 0 )
	{
	  attach_dot_entries(dpnt, &xstatbuf);
	}
      else
	{
	  attach_dot_entries(dpnt, &fstatbuf);
	}
    }

  if(!parent || parent == root)
    {
      if (!root) 
	{
	  root = dpnt;  /* First time through for root directory only */
	  root->depth = 0;
	  root->parent = root;
	} else {
	  dpnt->depth = 1;
	  if(!root->subdir)
	    {
	      root->subdir = dpnt;
	    }
	  else 
	    {
	      next_brother = root->subdir;
	      while(next_brother->next) next_brother = next_brother->next;
	      next_brother->next = dpnt;
	    }
	  dpnt->parent = parent;
	}
    } 
  else 
    {
      /* Come through here for  normal traversal of  tree */
#ifdef DEBUG
      fprintf(stderr,"%s(%d) ", path, dpnt->depth);
#endif
      if(parent->depth >  RR_relocation_depth) 
	{
	  fprintf(stderr,"Directories too deep  %s\n", path);
	  exit(1);
	}
      
      dpnt->parent = parent; 
      dpnt->depth = parent->depth + 1;
      
      if(!parent->subdir)
	{
	  parent->subdir = dpnt;
	}
      else 
	{
	  next_brother = parent->subdir;
	  while(next_brother->next) next_brother = next_brother->next;
	  next_brother->next = dpnt;
	}
    }

  return dpnt;
}

/*
 * Function:	delete_directory
 *
 * Purpose:	Locate a directory entry in the tree, create if needed.
 *
 * Arguments:
 */
static void FDECL2(delete_directory, struct directory *, parent, struct directory *, child)
{
  struct directory		* tdir;

  if( child->contents != NULL )
    {
      fprintf(stderr, "Unable to delete non-empty directory\n");
      exit(1);
    }

  free(child->whole_name);
  child->whole_name = NULL;

  free(child->de_name);
  child->de_name = NULL;

#ifdef APPLE_HYB
  if (apple_both && child->hfs_ent)
    free(child->hfs_ent);
#endif /* APPLE_HYB */

  if( parent->subdir == child )
    {
      parent->subdir = child->next;
    }
  else
    {
      for( tdir = parent->subdir; tdir->next != NULL; tdir = tdir->next )
	{
	  if( tdir->next == child )
	    {
	      tdir->next = child->next;
	      break;
	    }
	}
      if( tdir == NULL )
	{
	  fprintf(stderr, "Unable to locate child directory in parent list\n");
	  exit(1);
	}
    }
  free(child);
  return;
}

int FDECL1(sort_tree, struct directory *, node){
  struct directory * dpnt;
  int ret = 0;

  dpnt = node;

  while (dpnt){
    ret = sort_n_finish(dpnt);
    if( ret )
      {
	break;
      }
    
    if(dpnt->subdir) sort_tree(dpnt->subdir);
    dpnt = dpnt->next;
  }
  return ret;
}

void FDECL1(dump_tree, struct directory *, node){
  struct directory * dpnt;

  dpnt = node;

  while (dpnt){
    fprintf(stderr,"%4d %5d %s\n",dpnt->extent, dpnt->size, dpnt->de_name);
    if(dpnt->subdir) dump_tree(dpnt->subdir);
    dpnt = dpnt->next;
  }
}

void FDECL1(update_nlink_field, struct directory *, node)
{
    struct directory		* dpnt;
    struct directory		* xpnt;
    struct directory_entry	* s_entry;
    int				  i;

    dpnt = node;
    
    while (dpnt)
    {
	if (dpnt->dir_flags & INHIBIT_ISO9660_ENTRY) {
	    dpnt = dpnt->next;
	    continue;
	}

	/*
	 * First, count up the number of subdirectories this guy has.
	 */
        for(i=0, xpnt = dpnt->subdir; xpnt; xpnt = xpnt->next)
            if ((xpnt->dir_flags & INHIBIT_ISO9660_ENTRY) == 0)
                i++;
	/*
	 * Next check to see if we have any relocated directories
	 * in this directory.   The nlink field will include these
	 * as real directories when they are properly relocated.
	 *
	 * In the non-rockridge disk, the relocated entries appear
	 * as zero length files.
	 */
	for(s_entry = dpnt->contents; s_entry; s_entry = s_entry->next)
	{
		if( (s_entry->de_flags  & RELOCATED_DIRECTORY) != 0 &&
			(s_entry->de_flags  & INHIBIT_ISO9660_ENTRY) == 0)
		{
			i++;
		}
	}
	/*
	 * Now update the field in the Rock Ridge entry.
	 */
	update_nlink(dpnt->self, i + 2);

	/*
	 * Update the '.' entry for this directory.
	 */
	update_nlink(dpnt->contents, i + 2);

	/*
	 * Update all of the '..' entries that point to this guy.
	 */
	for(xpnt = dpnt->subdir; xpnt; xpnt = xpnt->next)
	    update_nlink(xpnt->contents->next, i + 2);

	if(dpnt->subdir) update_nlink_field(dpnt->subdir);
	dpnt = dpnt->next;
    }
}

/*
 * something quick and dirty to locate a file given a path
 * recursively walks down path in filename until it finds the
 * directory entry for the desired file 
 */
struct directory_entry * FDECL2(search_tree_file, struct directory *, 
				node,char *, filename)
{
  struct directory_entry * depnt;
  struct directory	 * dpnt;
  char			 * p1;
  char			 * rest;
  char			 * subdir;

  /*
   * strip off next directory name from filename 
   */
  subdir = strdup(filename);

  if( (p1=strchr(subdir, '/')) == subdir )
    {
      fprintf(stderr,"call to search_tree_file with an absolute path, stripping\n");
      fprintf(stderr,"initial path separator. Hope this was intended...\n");
      memmove(subdir, subdir+1, strlen(subdir)-1);
      p1 = strchr(subdir, '/');
    }

  /*
   * do we need to find a subdirectory 
   */
  if (p1) 
    {
      *p1 = '\0';

#ifdef DEBUG_TORITO
      fprintf(stderr,"Looking for subdir called %s\n",p1); 
#endif

      rest = p1+1;

#ifdef DEBUG_TORITO
      fprintf(stderr,"Remainder of path name is now %s\n", rest); 
#endif
      
      dpnt = node->subdir;
     while( dpnt )
       {
#ifdef DEBUG_TORITO
	 fprintf(stderr,"%4d %5d %s\n", dpnt->extent, dpnt->size, 
		 dpnt->de_name); 
#endif
	 if (!strcmp(subdir, dpnt->de_name)) 
	   {
#ifdef DEBUG_TORITO
	     fprintf(stderr,"Calling next level with filename = %s", rest); 
#endif
	     return(search_tree_file( dpnt, rest ));
	   }
	 dpnt = dpnt->next;
       }
      
     /* if we got here means we couldnt find the subdir */
     return (NULL);
    }    
  else 
    {
      /* 
       * look for a normal file now 
       */
      depnt = node->contents;
      while (depnt)
	{
#ifdef DEBUG_TORITO
	  fprintf(stderr,"%4d %5d %s\n",depnt->isorec.extent, 
		  depnt->size, depnt->name); 
#endif
	  if (!strcmp(filename, depnt->name)) 
	    {
#ifdef DEBUG_TORITO
	      fprintf(stderr,"Found our file %s", filename); 
#endif
	      return(depnt);
	    }
	  depnt = depnt->next;
	}
      /* 
       * if we got here means we couldnt find the subdir 
       */
      return (NULL);
    }
  fprintf(stderr,"We cant get here in search_tree_file :-/ \n");
}

void init_fstatbuf()
{
  time_t		    current_time;

  if(fstatbuf.st_ctime == 0)
    {
      time (&current_time);
      if( rationalize )
	{
	  fstatbuf.st_uid = 0;
	  fstatbuf.st_gid = 0;
	}
      else
	{
	  fstatbuf.st_uid = getuid();
	  fstatbuf.st_gid = getgid();
	}
      fstatbuf.st_ctime = current_time;
      fstatbuf.st_mtime = current_time;
      fstatbuf.st_atime = current_time;
    }
}
