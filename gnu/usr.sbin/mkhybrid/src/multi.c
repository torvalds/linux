/*
 * File multi.c - scan existing iso9660 image and merge into 
 * iso9660 filesystem.  Used for multisession support.
 *
 * Written by Eric Youngdale (1996).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"

#ifndef VMS

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#else
#include <sys/file.h>
#include <vms/fabdef.h>
#include "vms.h"
extern char * strdup(const char *);
#endif

#include "mkisofs.h"
#include "iso9660.h"

#define TF_CREATE 1
#define TF_MODIFY 2
#define TF_ACCESS 4
#define TF_ATTRIBUTES 8

static int  isonum_711 __PR((unsigned char * p));
static int  isonum_721 __PR((unsigned char * p));
static int  isonum_723 __PR((unsigned char * p));
static int  isonum_731 __PR((unsigned char * p));

static int  DECL(merge_old_directory_into_tree, (struct directory_entry *,
						 struct directory *));

#ifdef	__STDC__
static int
isonum_711 (unsigned char * p)
#else
static int
isonum_711 (p)
	unsigned char * p;
#endif
{
	return (*p & 0xff);
}

#ifdef	__STDC__
static int
isonum_721 (unsigned char * p)
#else
static int
isonum_721 (p)
	unsigned char * p;
#endif
{
	return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

#ifdef	__STDC__
static int
isonum_723 (unsigned char * p)
#else
static int
isonum_723 (p)
	unsigned char * p;
#endif
{
#if 0
	if (p[0] != p[3] || p[1] != p[2]) {
		fprintf (stderr, "invalid format 7.2.3 number\n");
		exit (1);
	}
#endif
	return (isonum_721 (p));
}

#ifdef	__STDC__
static int
isonum_731 (unsigned char * p)
#else
static int
isonum_731 (p)
	unsigned char * p;
#endif
{
	return ((p[0] & 0xff)
		| ((p[1] & 0xff) << 8)
		| ((p[2] & 0xff) << 16)
		| ((p[3] & 0xff) << 24));
}

#ifdef	__STDC__
int
isonum_733 (unsigned char * p)
#else
int
isonum_733 (p)
	unsigned char * p;
#endif
{
	return (isonum_731 (p));
}

FILE * in_image = NULL;

#ifndef	USE_SCG
/*
 * Don't define readsecs if mkisofs is linked with
 * the SCSI library.
 * readsecs() will be implemented as SCSI command in this case.
 *
 * Use global var in_image directly in readsecs()
 * the SCSI equivalent will not use a FILE* for I/O.
 *
 * The main point of this pointless abstraction is that Solaris won't let
 * you read 2K sectors from the cdrom driver.  The fact that 99.9% of the
 * discs out there have a 2K sectorsize doesn't seem to matter that much.
 * Anyways, this allows the use of a scsi-generics type of interface on
 * Solaris.
 */
#ifdef	__STDC__
static int
readsecs(int startsecno, void *buffer, int sectorcount)
#else
static int
readsecs(startsecno, buffer, sectorcount)
	int	startsecno;
	void	*buffer;
	int	sectorcount;
#endif
{
	int	f = fileno(in_image);

	if (lseek(f, (off_t)startsecno * SECTOR_SIZE, 0) == (off_t)-1) {
		fprintf(stderr," Seek error on old image\n");
		exit(10);
	}
	return (read(f, buffer, sectorcount * SECTOR_SIZE));
}
#endif

/*
 * Parse the RR attributes so we can find the file name.
 */
static int 
FDECL3(parse_rr, unsigned char *, pnt, int, len, struct directory_entry *,dpnt)
{
	int cont_extent, cont_offset, cont_size;
	char name_buf[256];

	cont_extent = cont_offset = cont_size = 0;

	while(len >= 4){
		if(pnt[3] != 1) {
		  fprintf(stderr,"**BAD RRVERSION");
		  return -1;
		};
		if(strncmp((char *) pnt, "NM", 2) == 0) {
		  strncpy(name_buf, (char *) pnt+5, pnt[2] - 5);
		  name_buf[pnt[2] - 5] = 0;
		  dpnt->name = strdup(name_buf);
		  dpnt->got_rr_name = 1;
		  return 0;
		}

		if(strncmp((char *) pnt, "CE", 2) == 0) {
			cont_extent = isonum_733(pnt+4);
			cont_offset = isonum_733(pnt+12);
			cont_size = isonum_733(pnt+20);
		};

		len -= pnt[2];
		pnt += pnt[2];
		if(len <= 3 && cont_extent) {
		  unsigned char sector[SECTOR_SIZE];
		  readsecs(cont_extent, sector, 1);
		  parse_rr(&sector[cont_offset], cont_size, dpnt);
		};
	};

	/* Fall back to the iso name if no RR name found */
	if (dpnt->name == NULL) {
	  char *cp;

	  strcpy(name_buf, dpnt->isorec.name);
	  cp = strchr(name_buf, ';');
	  if (cp != NULL) {
	    *cp = '\0';
	  }

	  dpnt->name = strdup(name_buf);
	}

	return 0;
} /* parse_rr */


static int 
FDECL4(check_rr_dates, struct directory_entry *, dpnt, 
       struct directory_entry *, current, 
       struct stat *, statbuf, 
       struct stat *,lstatbuf)
{
	int cont_extent, cont_offset, cont_size;
	int offset;
	unsigned char * pnt;
	int len;
	int same_file;
	int same_file_type;
	mode_t mode;
	char time_buf[7];
	
	
	cont_extent = cont_offset = cont_size = 0;
	same_file = 1;
	same_file_type = 1;

	pnt = dpnt->rr_attributes;
	len = dpnt->rr_attr_size;
	/*
	 * We basically need to parse the rr attributes again, and
	 * dig out the dates and file types.
	 */
	while(len >= 4){
		if(pnt[3] != 1) {
		  fprintf(stderr,"**BAD RRVERSION");
		  return -1;
		};

		/*
		 * If we have POSIX file modes, make sure that the file type
		 * is the same.  If it isn't, then we must always
		 * write the new file.
		 */
		if(strncmp((char *) pnt, "PX", 2) == 0) {
		  mode = isonum_733(pnt + 4);
		  if( (lstatbuf->st_mode & S_IFMT) != (mode & S_IFMT) )
		    {
		      same_file_type = 0;
		      same_file = 0;
		    }
		}

		if(strncmp((char *) pnt, "TF", 2) == 0) {
		  offset = 5;
		  if( pnt[4] & TF_CREATE )
		    {
		      iso9660_date((char *) time_buf, lstatbuf->st_ctime);
		      if(memcmp(time_buf, pnt+offset, 7) == 0) 
			same_file = 0;
		      offset += 7;
		    }
		  if( pnt[4] & TF_MODIFY )
		    {
		      iso9660_date((char *) time_buf, lstatbuf->st_mtime);
		      if(memcmp(time_buf, pnt+offset, 7) == 0) 
			same_file = 0;
		      offset += 7;
		    }
		}

		if(strncmp((char *) pnt, "CE", 2) == 0) {
			cont_extent = isonum_733(pnt+4);
			cont_offset = isonum_733(pnt+12);
			cont_size = isonum_733(pnt+20);
		};

		len -= pnt[2];
		pnt += pnt[2];
		if(len <= 3 && cont_extent) {
		  unsigned char sector[SECTOR_SIZE];

		  readsecs(cont_extent, sector, 1);
		  parse_rr(&sector[cont_offset], cont_size, dpnt);
		};
	};

	/*
	 * If we have the same fundamental file type, then it is clearly
	 * safe to reuse the TRANS.TBL entry.
	 */
	if( same_file_type )
	  {
	    current->de_flags |= SAFE_TO_REUSE_TABLE_ENTRY;
	  }

	return same_file;
}

struct directory_entry **
FDECL2(read_merging_directory, struct iso_directory_record *, mrootp,
       int *, nent)
{
  unsigned char			* cpnt;
  unsigned char			* cpnt1;
  char				* dirbuff;
  int				  i;
  struct iso_directory_record	* idr;
  int				  len;
  struct directory_entry	**pnt;
  int				  rlen;
  struct directory_entry	**rtn;
  int				  seen_rockridge;
  unsigned char			* tt_buf;
  int				  tt_extent;
  int				  tt_size;

  static int warning_given = 0;

  /*
   * First, allocate a buffer large enough to read in the entire
   * directory.
   */
  dirbuff = (char *) e_malloc(isonum_733((unsigned char *)mrootp->size));

  readsecs(isonum_733((unsigned char *)mrootp->extent), dirbuff,
	   isonum_733((unsigned char *)mrootp->size)/SECTOR_SIZE);

  /*
   * Next look over the directory, and count up how many entries we
   * have.
   */
  len = isonum_733((unsigned char *)mrootp->size);
  i = 0;
  *nent = 0;
  while(i < len )
    {
      idr = (struct iso_directory_record *) &dirbuff[i];
      if(idr->length[0] == 0) 
	{
	  i = (i + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
	  continue;
	}
      (*nent)++;
      i += idr->length[0];
    }

  /*
   * Now allocate the buffer which will hold the array we are
   * about to return.
   */
  rtn = (struct directory_entry **) e_malloc(*nent * sizeof(*rtn));

  /*
   * Finally, scan the directory one last time, and pick out the
   * relevant bits of information, and store it in the relevant
   * bits of the structure.
   */
  i = 0;
  pnt = rtn;
  tt_extent = 0;
  seen_rockridge = 0;
  tt_size = 0;
  while(i < len )
    {
      idr = (struct iso_directory_record *) &dirbuff[i];
      if(idr->length[0] == 0) 
	{
	  i = (i + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
	  continue;
	}
      *pnt = (struct directory_entry *) e_malloc(sizeof(**rtn));
      (*pnt)->next = NULL;
      (*pnt)->isorec = *idr;
      (*pnt)->starting_block = isonum_733((unsigned char *)idr->extent);
      (*pnt)->size = isonum_733((unsigned char *)idr->size);
      (*pnt)->priority = 0;
      (*pnt)->name = NULL;
      (*pnt)->got_rr_name = 0;
      (*pnt)->table = NULL;
      (*pnt)->whole_name = NULL;
      (*pnt)->filedir = NULL;
      (*pnt)->parent_rec = NULL;
      /*
       * Set this information so that we correctly cache previous
       * session bits of information.
       */
      (*pnt)->inode = (*pnt)->starting_block;
      (*pnt)->dev = PREV_SESS_DEV;
      (*pnt)->rr_attributes = NULL;
      (*pnt)->rr_attr_size = 0;
      (*pnt)->total_rr_attr_size = 0;
      (*pnt)->de_flags = SAFE_TO_REUSE_TABLE_ENTRY;

      /*
       * Check for and parse any RR attributes for the file.
       * All we are really looking for here is the original name
       * of the file.
       */
      rlen = idr->length[0] & 0xff;
      cpnt = (unsigned char *) idr;
      
      rlen -= sizeof(struct iso_directory_record);
      cpnt += sizeof(struct iso_directory_record);
      
      rlen += sizeof(idr->name);
      cpnt -= sizeof(idr->name);
      
      rlen -= idr->name_len[0];
      cpnt += idr->name_len[0];
      
      if((idr->name_len[0] & 1) == 0){
	cpnt++;
	rlen--;
      };

      if( rlen != 0 )
	{
	  (*pnt)->total_rr_attr_size =  (*pnt)->rr_attr_size = rlen;
	  (*pnt)->rr_attributes = e_malloc(rlen);
	  memcpy((*pnt)->rr_attributes,  cpnt, rlen);
	  seen_rockridge = 1;
	}

      /*
       * Now zero out the remainder of the name field.
       */
      cpnt = (unsigned char *) &(*pnt)->isorec.name;
      cpnt += idr->name_len[0];
      memset(cpnt, 0, sizeof((*pnt)->isorec.name) - idr->name_len[0]);

      parse_rr((*pnt)->rr_attributes, rlen, *pnt);
      
      if(    ((*pnt)->isorec.name_len[0] == 1)
	  && (    ((*pnt)->isorec.name[0] == 0)
	       || ((*pnt)->isorec.name[0] == 1)) )
	{
	  if( (*pnt)->name != NULL )
	    {
	      free((*pnt)->name);
	    }
	  if( (*pnt)->whole_name != NULL )
	    {
	      free((*pnt)->whole_name);
	    }
	  if( (*pnt)->isorec.name[0] == 0 )
	    {
	      (*pnt)->name = strdup(".");
	    }
	  else
	    {
	      (*pnt)->name = strdup("..");
	    }
	}

#ifdef DEBUG
      fprintf(stderr, "got DE name: %s\n", (*pnt)->name);
#endif

#ifdef APPLE_HYB
      if( strncmp(idr->name, trans_tbl, strlen(trans_tbl)) == 0)
#else
      if( strncmp(idr->name, "TRANS.TBL", 9) == 0)
#endif /* APPLE_HYB */
	{
	  if( (*pnt)->name != NULL )
	    {
	      free((*pnt)->name);
	    }
	  if( (*pnt)->whole_name != NULL )
	    {
	      free((*pnt)->whole_name);
	    }
	  (*pnt)->name = strdup("<translation table>");
	  tt_extent = isonum_733((unsigned char *)idr->extent);
	  tt_size = isonum_733((unsigned char *)idr->size);
	}
      
      pnt++;
      i += idr->length[0];
    }

  /*
   * If there was a TRANS.TBL;1 entry, then grab it, read it, and use it
   * to get the filenames of the files.  Also, save the table info, just
   * in case we need to use it.
   */
  if( tt_extent != 0 && tt_size != 0 )
    {
      tt_buf = (unsigned char *) e_malloc(tt_size);
      readsecs(tt_extent, tt_buf, tt_size/SECTOR_SIZE);

      /*
       * Loop through the file, examine each entry, and attempt to
       * attach it to the correct entry.
       */
      cpnt = tt_buf;
      cpnt1 = tt_buf;
      while( cpnt - tt_buf < tt_size )
	{
	  while(*cpnt1 != '\n' && *cpnt1 != '\0')  cpnt1++;
	  *cpnt1 = '\0';

	  for(pnt = rtn, i = 0; i <*nent; i++, pnt++)
	    {
	      rlen = isonum_711((*pnt)->isorec.name_len);
	      if( strncmp((char *) cpnt + 2, (*pnt)->isorec.name,
			  rlen) == 0 
		  && cpnt[2+rlen] == ' ')
		{
		  (*pnt)->table = e_malloc(strlen((char*)cpnt) - 33);
		  sprintf((*pnt)->table, "%c\t%s\n",
			  *cpnt, cpnt+37);
		  if( !(*pnt)->got_rr_name )
		    {
		      if ((*pnt)->name != NULL) {
			free((*pnt)->name);
		      }
		      (*pnt)->name = strdup((char *) cpnt+37);
		    }
		  break;
		}
	    }
	  cpnt = cpnt1 + 1;
	  cpnt1 = cpnt;
	}
      
      free(tt_buf);
    }
  else if( !seen_rockridge && !warning_given )
    {
      /*
       * Warn the user that iso (8.3) names were used because neither
       * Rock Ridge (-R) nor TRANS.TBL (-T) name translations were found.
       */
      fprintf(stderr,"Warning: Neither Rock Ridge (-R) nor TRANS.TBL (-T) \n");
      fprintf(stderr,"name translations were found on previous session.\n");
      fprintf(stderr,"ISO (8.3) file names have been used instead.\n");
      warning_given = 1;
    }

  if( dirbuff != NULL )
    {
      free(dirbuff);
    }
  
  return rtn;
} /* read_merging_directory */

/*
 * Free any associated data related to the structures.
 */
int 
FDECL2(free_mdinfo, struct directory_entry **  , ptr, int, len )
{
  int		i;
  struct directory_entry **p;

  p = ptr;
  for(i=0; i<len; i++, p++)
    {
      /*
       * If the tree-handling code decided that it needed an entry,
       * it will have removed it from the list.  Thus we must allow
       * for null pointers here.
       */
      if( *p == NULL )
	{
	  continue;
	}

      if( (*p)->name != NULL )
	{
	  free((*p)->name);
	}

      if( (*p)->whole_name != NULL )
	{
	  free((*p)->whole_name);
	}

      if( (*p)->rr_attributes != NULL )
	{
	  free((*p)->rr_attributes);
	}

      if( (*p)->table != NULL )
	{
	  free((*p)->table);
	}

      free(*p);

    }

  free(ptr);
  return 0;
}

/*
 * Search the list to see if we have any entries from the previous
 * session that match this entry.  If so, copy the extent number
 * over so we don't bother to write it out to the new session.
 */

int
FDECL6(check_prev_session, struct directory_entry **  , ptr, int, len,
       struct directory_entry *, curr_entry,
       struct stat *, statbuf, struct stat *, lstatbuf,
       struct directory_entry **, odpnt)
{
  int		i;

  for( i=0; i < len; i++ )
    {
      if( ptr[i] == NULL )
	{
	  continue;
	}

#if 0
      if( ptr[i]->name != NULL && ptr[i]->isorec.name_len[0] == 1
	  && ptr[i]->name[0] == '\0' )
	{
	  continue;
	}
      if( ptr[i]->name != NULL && ptr[i]->isorec.name_len[0] == 1
	  && ptr[i]->name[0] == 1)
	{
	  continue;
	}
#else
      if( ptr[i]->name != NULL && strcmp(ptr[i]->name, ".") == 0 )
	{
	  continue;
	}
      if( ptr[i]->name != NULL  && strcmp(ptr[i]->name, "..") == 0 )
	{
	  continue;
	}
#endif

      if(    ptr[i]->name != NULL
	  && strcmp(ptr[i]->name, curr_entry->name) != 0 )
	{
	  continue;
	}

      /*
       * We know that the files have the same name.  If they also have
       * the same file type (i.e. file, dir, block, etc), then we
       * can safely reuse the TRANS.TBL entry for this file.
       * The check_rr_dates function will do this for us.
       *
       * Verify that the file type and dates are consistent.
       * If not, we probably have a different file, and we need
       * to write it out again.
       */
      if(    (ptr[i]->rr_attributes != NULL)
	  && (check_rr_dates(ptr[i], curr_entry, statbuf, lstatbuf)) )
	{
	  goto found_it;
	}


      /*
       * Verify size and timestamp.  If rock ridge is in use, we need
       * to compare dates from RR too.  Directories are special, we
       * calculate their size later.
       */
      if(     (curr_entry->isorec.flags[0] & 2) == 0
	  &&  ptr[i]->size != curr_entry->size )
	{
	  goto found_it;
	}

      if( memcmp(ptr[i]->isorec.date, curr_entry->isorec.date,7) != 0 )
	{
	  goto found_it;
	}

      /*
       * Never ever reuse directory extents.  See comments in
       * tree.c for an explaination of why this must be the case.
       */
      if( (curr_entry->isorec.flags[0] & 2) != 0 )
	{
	  goto found_it;
	}

      memcpy(curr_entry->isorec.extent, ptr[i]->isorec.extent, 8);
      curr_entry->de_flags |= SAFE_TO_REUSE_TABLE_ENTRY;
      goto found_it;
    }
  return 0;

found_it:
  if( odpnt != NULL )
    {
      *odpnt = ptr[i];
    }
  else
    {
      free(ptr[i]);
    }
  ptr[i] = NULL;
  return 0;
}

/*
 * merge_isofs:  Scan an existing image, and return a pointer
 * to the root directory for this image.
 */
struct iso_directory_record * FDECL1(merge_isofs, char *, path)
{
  char				  buffer[SECTOR_SIZE];
  int				  file_addr;
  int				  i;
  struct iso_primary_descriptor * pri = NULL;
  struct iso_directory_record   * rootp;
  struct iso_volume_descriptor  * vdp;

  /*
   * Start by opening up the image and searching for the volume header.
   * Ultimately, we need to search for volume headers in multiple places
   * because we might be starting with a multisession image.
   * FIXME(eric).
   */

#ifndef	USE_SCG
  in_image = fopen(path, "rb");
  if( in_image == NULL )
    {
      return NULL;
    }
#else
  if (strchr(path, '/')) {
	in_image = fopen(path, "rb");
	if( in_image == NULL ) {
		return NULL;
	}
  } else {
	if (scsidev_open(path) < 0)
		return NULL;
  }
#endif

  get_session_start(&file_addr);

  for(i = 0; i< 100; i++)
    {
      if (readsecs(file_addr/SECTOR_SIZE, &buffer,
		   sizeof(buffer)/SECTOR_SIZE) != sizeof(buffer))
	{
	  fprintf(stderr," Read error on old image %s\n", path);
	  exit(10);
	}

      vdp = (struct iso_volume_descriptor *)buffer;

      if(    (strncmp(vdp->id, ISO_STANDARD_ID, sizeof vdp->id) == 0)
	  && (isonum_711((unsigned char *) vdp->type) == ISO_VD_PRIMARY) )
	{
	  break;
	}
      file_addr += SECTOR_SIZE;
    }

  if( i == 100 )
    {
      return NULL;
    }

  pri = (struct iso_primary_descriptor *)vdp;

  /*
   * Check the blocksize of the image to make sure it is compatible.
   */
  if(    (isonum_723 ((unsigned char *) pri->logical_block_size) != SECTOR_SIZE)
      || (isonum_723 ((unsigned char *) pri->volume_set_size) != 1) )
    {
      return NULL;
    }

  /*
   * Get the location and size of the root directory.
   */
  rootp = calloc(1, sizeof(struct iso_directory_record));

  memcpy(rootp, pri->root_directory_record, sizeof(pri->root_directory_record));

  return rootp;
}

void FDECL3(merge_remaining_entries, struct directory *, this_dir,
	    struct directory_entry **, pnt,
	    int, n_orig)
{
  int i;
  struct directory_entry * s_entry;
  unsigned int ttbl_extent = 0;
  unsigned int ttbl_index  = 0;
  char whole_path[1024];

  /*
   * Whatever is leftover in the list needs to get merged back
   * into the directory.
   */
  for( i=0; i < n_orig; i++ )
    {
      if( pnt[i] == NULL )
	{
	  continue;
	}
      
      if( pnt[i]->name != NULL && pnt[i]->whole_name == NULL)
       {
         /*
          * Set the name for this directory.
          */
         strcpy(whole_path, this_dir->de_name);
         strcat(whole_path, SPATH_SEPARATOR);
         strcat(whole_path, pnt[i]->name);

         pnt[i]->whole_name = strdup(whole_path);
       }

      if( pnt[i]->name != NULL
	  && strcmp(pnt[i]->name, "<translation table>") == 0 )
	{
	  ttbl_extent = isonum_733((unsigned char *) pnt[i]->isorec.extent);
	  ttbl_index = i;
	  continue;
	}
      /*
       * Skip directories for now - these need to be treated
       * differently.
       */
      if( (pnt[i]->isorec.flags[0] & 2) != 0 )
	{
	  /*
	   * FIXME - we need to insert this directory into the
	   * tree, so that the path tables we generate will
	   * be correct.
	   */
	  if(    (strcmp(pnt[i]->name, ".") == 0)
	      || (strcmp(pnt[i]->name, "..") == 0) )
	    {
	      free(pnt[i]);
	      pnt[i] = NULL;
	      continue;
	    }
	  else
	    {
	      merge_old_directory_into_tree(pnt[i], this_dir);
	    }
	}
      pnt[i]->next = this_dir->contents;
      pnt[i]->filedir = this_dir;
      this_dir->contents = pnt[i];
      pnt[i] = NULL;
    }
  

  /*
   * If we don't have an entry for the translation table, then
   * don't bother trying to copy the starting extent over.
   * Note that it is possible that if we are copying the entire
   * directory, the entry for the translation table will have already
   * been inserted into the linked list and removed from the old
   * entries list, in which case we want to leave the extent number
   * as it was before.
   */
  if( ttbl_extent == 0 )
    {
      return;
    }

  /*
   * Finally, check the directory we are creating to see whether
   * there are any new entries in it.  If there are not, we can
   * reuse the same translation table.
   */
  for(s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
    {
      /*
       * Don't care about '.' or '..'.  They are never in the table
       * anyways.
       */
      if( s_entry->name != NULL && strcmp(s_entry->name, ".") == 0 )
	{
	  continue;
	}
      if( s_entry->name != NULL && strcmp(s_entry->name, "..") == 0 )
	{
	  continue;
	}
      if( strcmp(s_entry->name, "<translation table>") == 0)
	{
	  continue;
	}
      if( (s_entry->de_flags & SAFE_TO_REUSE_TABLE_ENTRY) == 0 )
	{
	  return;
	}
    }

  /*
   * Locate the translation table, and re-use the same extent.
   * It isn't clear that there should ever be one in there already
   * so for now we try and muddle through the best we can.
   */
  for(s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
    {
      if( strcmp(s_entry->name, "<translation table>") == 0)
	{
	  fprintf(stderr,"Should never get here\n");
	  set_733(s_entry->isorec.extent, ttbl_extent);
	  return;
	}
    }

  pnt[ttbl_index]->next = this_dir->contents;
  pnt[ttbl_index]->filedir = this_dir;
  this_dir->contents = pnt[ttbl_index];
  pnt[ttbl_index] = NULL;
}


/*
 * Here we have a case of a directory that has completely disappeared from
 * the face of the earth on the tree we are mastering from.  Go through and
 * merge it into the tree, as well as everything beneath it.
 *
 * Note that if a directory has been moved for some reason, this will
 * incorrectly pick it up and attempt to merge it back into the old
 * location.  FIXME(eric).
 */
static int
FDECL2(merge_old_directory_into_tree, struct directory_entry *, dpnt, 
       struct directory *, parent)
{
  struct directory_entry	**contents = NULL;
  int				  i;
  int				  n_orig;
  struct directory		* this_dir, *next_brother;
  char				  whole_path[1024];

  this_dir = (struct directory *) e_malloc(sizeof(struct directory));
  memset(this_dir, 0, sizeof(struct directory));
  this_dir->next = NULL;
  this_dir->subdir = NULL;
  this_dir->self = dpnt;
  this_dir->contents = NULL;
  this_dir->size = 0;
  this_dir->extent = 0;
  this_dir->depth = parent->depth + 1;
  this_dir->parent = parent;
  if(!parent->subdir)
    parent->subdir = this_dir;
  else {
    next_brother = parent->subdir;
    while(next_brother->next) next_brother = next_brother->next;
    next_brother->next = this_dir;
  }

  /*
   * Set the name for this directory.
   */
  strcpy(whole_path, parent->de_name);
  strcat(whole_path, SPATH_SEPARATOR);
  strcat(whole_path, dpnt->name);
  this_dir->de_name = strdup(whole_path);
  this_dir->whole_name = strdup(whole_path);

  /*
   * Now fill this directory using information from the previous
   * session.
   */
  contents = read_merging_directory(&dpnt->isorec, &n_orig);
  /*
   * Start by simply copying the '.', '..' and non-directory
   * entries to this directory.  Technically we could let
   * merge_remaining_entries handle this, but it gets rather confused
   * by the '.' and '..' entries.
   */
  for(i=0; i < n_orig; i ++ )
    {
      /*
       * We can always reuse the TRANS.TBL in this particular case.
       */
      contents[i]->de_flags |= SAFE_TO_REUSE_TABLE_ENTRY;	

      if(    ((contents[i]->isorec.flags[0] & 2) != 0)
	  && (i >= 2) )
	{
	  continue;
	}

      /*
       * If we have a directory, don't reuse the extent number.
       */
      if( (contents[i]->isorec.flags[0] & 2) != 0 )
	{
	  memset(contents[i]->isorec.extent, 0, 8);

	  if( strcmp(contents[i]->name, ".") == 0 )
	      this_dir->dir_flags |= DIR_HAS_DOT;

	  if( strcmp(contents[i]->name, "..") == 0 )
	      this_dir->dir_flags |= DIR_HAS_DOTDOT;
	}

      /*
       * Set the whole name for this file.
       */
      strcpy(whole_path, this_dir->whole_name);
      strcat(whole_path, SPATH_SEPARATOR);
      strcat(whole_path, contents[i]->name);

      contents[i]->whole_name = strdup(whole_path);

      contents[i]->next = this_dir->contents;
      contents[i]->filedir = this_dir;
      this_dir->contents = contents[i];
      contents[i] = NULL;
    }

  /*
   * Zero the extent number for ourselves.
   */
  memset(dpnt->isorec.extent, 0, 8);

  /*
   * Anything that is left are other subdirectories that need to be merged.
   */
  merge_remaining_entries(this_dir, contents, n_orig);
  free_mdinfo(contents, n_orig);
#if 0
  /*
   * This is no longer required.  The post-scan sort will handle
   * all of this for us.
   */
  sort_n_finish(this_dir);
#endif

  return 0;
}


char * cdwrite_data = NULL;

int
FDECL1(get_session_start, int *, file_addr) 
{
  char * pnt;

#ifdef CDWRITE_DETERMINES_FIRST_WRITABLE_ADDRESS
  /*
   * FIXME(eric).  We need to coordinate with cdwrite to obtain
   * the parameters.  For now, we assume we are writing the 2nd session,
   * so we start from the session that starts at 0.
   */

  *file_addr = (16 << 11);

  /*
   * We need to coordinate with cdwrite to get the next writable address
   * from the device.  Here is where we use it.
   */
  session_start = last_extent = last_extent_written = cdwrite_result();

#else

  if( cdwrite_data == NULL )
    {
      fprintf(stderr,"Special parameters for cdwrite not specified with -C\n");
      exit(1);
    }

  /*
   * Next try and find the ',' in there which delimits the two numbers.
   */
  pnt = strchr(cdwrite_data, ',');
  if( pnt == NULL )
    {
      fprintf(stderr, "Malformed cdwrite parameters\n");
      exit(1);
    }

  *pnt = '\0';
  if (file_addr != NULL) {
    *file_addr = atol(cdwrite_data) * SECTOR_SIZE;
  }
  pnt++;

  session_start = last_extent = last_extent_written = atol(pnt);

  pnt--;
  *pnt = ',';

#endif
  return 0;
}

/*
 * This function scans the directory tree, looking for files, and it makes
 * note of everything that is found.  We also begin to construct the ISO9660
 * directory entries, so that we can determine how large each directory is.
 */

int
FDECL2(merge_previous_session,struct directory *, this_dir,
       struct iso_directory_record *, mrootp)
{
  struct directory_entry	**orig_contents = NULL;
  struct directory_entry        * odpnt = NULL;
  int				  n_orig;
  struct directory_entry	* s_entry;
  int				  status, lstatus;
  struct stat			  statbuf, lstatbuf;

  /*
   * Parse the same directory in the image that we are merging
   * for multisession stuff.
   */
  orig_contents = read_merging_directory(mrootp, &n_orig);
  if( orig_contents == NULL )
    {
      return 0;
    }


/* Now we scan the directory itself, and look at what is inside of it. */

  for(s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
    {
      status  =  stat_filter(s_entry->whole_name, &statbuf);
      lstatus = lstat_filter(s_entry->whole_name, &lstatbuf);

      /*
       * We always should create an entirely new directory tree whenever
       * we generate a new session, unless there were *no* changes whatsoever
       * to any of the directories, in which case it would be kind of pointless
       * to generate a new session.
       *
       * I believe it is possible to rigorously prove that any change anywhere
       * in the filesystem will force the entire tree to be regenerated
       * because the modified directory will get a new extent number.  Since
       * each subdirectory of the changed directory has a '..' entry, all of
       * them will need to be rewritten too, and since the parent directory
       * of the modified directory will have an extent pointer to the directory
       * it too will need to be rewritten.  Thus we will never be able to reuse
       * any directory information when writing new sessions.
       *
       * We still check the previous session so we can mark off the equivalent
       * entry in the list we got from the original disc, however.
       */

      /*
       * The check_prev_session function looks for an identical entry in
       * the previous session.  If we see it, then we copy the extent
       * number to s_entry, and cross it off the list.
       */
      check_prev_session(orig_contents, n_orig, s_entry,
			 &statbuf, &lstatbuf, &odpnt);

      if(S_ISDIR(statbuf.st_mode) && odpnt != NULL)
	{
	  int dflag;

	  if (strcmp(s_entry->name,".") && strcmp(s_entry->name,"..")) 
	    {
	      struct directory * child;

	      child = find_or_create_directory(this_dir, 
					       s_entry->whole_name, 
					       s_entry, 1);
	      dflag = merge_previous_session(child, 
					     &odpnt->isorec);
	      /* If unable to scan directory, mark this as a non-directory */
	      if(!dflag)
		lstatbuf.st_mode = (lstatbuf.st_mode & ~S_IFMT) | S_IFREG;
	      free(odpnt);
	      odpnt = NULL;
	    }
	}
    }
  
  /*
   * Whatever is left over, are things which are no longer in the tree
   * on disk.  We need to also merge these into the tree.
   */
   merge_remaining_entries(this_dir, orig_contents, n_orig);
   free_mdinfo(orig_contents, n_orig);
  
  return 1;
}

