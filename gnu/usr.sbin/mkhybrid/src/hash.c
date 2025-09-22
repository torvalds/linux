/*
 * File hash.c - generate hash tables for iso9660 filesystem.

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


#include <stdlib.h>
#include "config.h"
#include "mkisofs.h"

#define NR_HASH 1024

#define HASH_FN(DEV, INO) ((DEV + INO + (INO >> 2) + (INO << 8)) % NR_HASH)

static struct file_hash * hash_table[NR_HASH] = {0,};

void FDECL1(add_hash, struct directory_entry *, spnt){
  struct file_hash * s_hash;
  unsigned int hash_number;

  if(spnt->size == 0 || spnt->starting_block == 0) 
    if(spnt->size != 0 || spnt->starting_block != 0) {
      fprintf(stderr,"Non zero-length file assigned zero extent.\n");
      exit(1);
    };

  if (spnt->dev == (dev_t) UNCACHED_DEVICE || spnt->inode == UNCACHED_INODE) return;
  hash_number = HASH_FN((unsigned int) spnt->dev, (unsigned int) spnt->inode);

#if 0
  if (verbose > 1) fprintf(stderr,"%s ",spnt->name);
#endif
  s_hash = (struct file_hash *) e_malloc(sizeof(struct file_hash));
  s_hash->next = hash_table[hash_number];
  s_hash->inode = spnt->inode;
  s_hash->dev = spnt->dev;
  s_hash->starting_block = spnt->starting_block;
  s_hash->size = spnt->size;
  hash_table[hash_number] = s_hash;
}

struct file_hash * FDECL2(find_hash, dev_t, dev, ino_t, inode){
  unsigned int hash_number;
  struct file_hash * spnt;
  hash_number = HASH_FN((unsigned int) dev, (unsigned int) inode);
  if (dev == (dev_t) UNCACHED_DEVICE || inode == UNCACHED_INODE) return NULL;

  spnt = hash_table[hash_number];
  while(spnt){
    if(spnt->inode == inode && spnt->dev == dev) return spnt;
    spnt = spnt->next;
  };
  return NULL;
}

#ifdef APPLE_HYB
/* based on flush_file_hash() below - needed as we wnat to re-use the
   file hash table */
void flush_hash(){
	struct file_hash  * fh, *fh1;
	int i;

	for(i=0; i<NR_HASH; i++) {
		fh = hash_table[i];
		while(fh) {
			fh1 =  fh->next;
			free(fh);
			fh = fh1;
		}
		hash_table[i] =  NULL;
	}
}
#endif /* APPLE_HYB */

static struct file_hash * directory_hash_table[NR_HASH] = {0,};

void FDECL2(add_directory_hash, dev_t, dev, ino_t, inode){
  struct file_hash * s_hash;
  unsigned int hash_number;

  if (dev == (dev_t) UNCACHED_DEVICE || inode == UNCACHED_INODE) return;
  hash_number = HASH_FN((unsigned int) dev, (unsigned int) inode);

  s_hash = (struct file_hash *) e_malloc(sizeof(struct file_hash));
  s_hash->next = directory_hash_table[hash_number];
  s_hash->inode = inode;
  s_hash->dev = dev;
  directory_hash_table[hash_number] = s_hash;
}

struct file_hash * FDECL2(find_directory_hash, dev_t, dev, ino_t, inode){
  unsigned int hash_number;
  struct file_hash * spnt;
  hash_number = HASH_FN((unsigned int) dev, (unsigned int) inode);
  if (dev == (dev_t) UNCACHED_DEVICE || inode == UNCACHED_INODE) return NULL;

  spnt = directory_hash_table[hash_number];
  while(spnt){
    if(spnt->inode == inode && spnt->dev == dev) return spnt;
    spnt = spnt->next;
  };
  return NULL;
}

struct  name_hash
{
  struct name_hash * next;
  struct directory_entry * de;
};

#define NR_NAME_HASH 128

static struct name_hash * name_hash_table[NR_NAME_HASH] = {0,};

/*
 * Find the hash bucket for this name.
 */
static  unsigned int FDECL1(name_hash, const char *, name)
{
  unsigned int hash = 0;
  const char * p;
  
  p = name;
  
  while (*p) 
    {
      /*
       * Don't hash the  iso9660 version number.  This way
       * we can detect duplicates in cases where we have
       * directories (i.e. foo) and non-directories
       * (i.e. foo;1).
       */
      if( *p == ';' )
	{
	  break;
	}
      hash = (hash << 15) + (hash << 3) + (hash >> 3) + *p++;
    }
  return hash % NR_NAME_HASH;
}

void FDECL1(add_file_hash, struct directory_entry *, de){
	struct name_hash  * new;
	int hash;

	new = (struct  name_hash *) e_malloc(sizeof(struct name_hash));
	new->de = de;
	new->next = NULL;
	hash = name_hash(de->isorec.name);

	/* Now insert into the hash table */
	new->next = name_hash_table[hash];
	name_hash_table[hash] = new;
}

struct directory_entry * FDECL1(find_file_hash, char *, name)
{
  struct name_hash  * nh;
  char		    * p1;
  char		    * p2;
  
  for(nh = name_hash_table[name_hash(name)]; nh; nh = nh->next)
    {
      p1 = name;
      p2 = nh->de->isorec.name;

      /*
       * Look for end of string, or a mismatch.
       */
      while(1==1)
	{
	  if(    (*p1 == '\0' || *p1 == ';')
	      || (*p2 == '\0' || *p2 == ';')
	      || (*p1 != *p2) )
	    {
	      break;
	    }
	  p1++;
	  p2++;
	}

      /*
       * If we are at the end of both strings, then
       * we have a match.
       */
      if(    (*p1 == '\0' || *p1 == ';')
	  && (*p2 == '\0' || *p2 == ';') )
	{
	  return nh->de;
	}
    }
  return NULL;
}

int FDECL1(delete_file_hash, struct directory_entry *, de){
	struct name_hash  * nh, *prev;
	int hash;

	prev = NULL;
	hash = name_hash(de->isorec.name);
	for(nh = name_hash_table[hash]; nh; nh = nh->next) {
		if(nh->de == de) break;
		prev = nh;
	}
	if(!nh) return 1;
	if(!prev)
		name_hash_table[hash] = nh->next;
	else
		prev->next = nh->next;
	free(nh);
	return 0;
}

void flush_file_hash(){
	struct name_hash  * nh, *nh1;
	int i;

	for(i=0; i<NR_NAME_HASH; i++) {
		nh = name_hash_table[i];
		while(nh) {
			nh1 =  nh->next;
			free(nh);
			nh = nh1;
		}
		name_hash_table[i] =  NULL;
		
	}
}
