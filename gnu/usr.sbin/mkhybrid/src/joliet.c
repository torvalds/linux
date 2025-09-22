/*
 * File joliet.c - handle Win95/WinNT long file/unicode extensions for iso9660.

   Copyright 1997 Eric Youngdale.

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

/* APPLE_HYB James Pearson j.pearson@ge.ucl.ac.uk 12/3/99 */

/*
 * Joliet extensions for ISO9660.  These are spottily documented by
 * Microsoft.  In their infinite stupidity, they completely ignored
 * the possibility of using an SUSP record with the long filename
 * in it, and instead wrote out a duplicate directory tree with the
 * long filenames in it.
 *
 * I am not sure why they did this.  One reason is that they get the path
 * tables with the long filenames in them.
 *
 * There are two basic principles to Joliet, and the non-Unicode variant
 * known as Romeo.  Long filenames seem to be the main one, and the second
 * is that the character set and a few other things is substantially relaxed.
 *
 * The SVD is identical to the PVD, except:
 *
 *	Id is 2, not 1 (indicates SVD).
 *	escape_sequences contains UCS-2 indicator (levels 1, 2 or 3).
 *	The root directory record points to a different extent (with different
 *		size).
 *	There are different path tables for the two sets of directory trees.
 *
 * The following fields are recorded in Unicode:
 *	system_id
 *	volume_id
 *	volume_set_id
 *	publisher_id
 *	preparer_id
 *	application_id
 *	copyright_file_id
 *	abstract_file_id
 *	bibliographic_file_id
 *
 * Unicode strings are always encoded in big-endian format.
 *
 * In a directory record, everything is the same as with iso9660, except
 * that the name is recorded in unicode.  The name length is specified in
 * total bytes, not in number of unicode characters.
 *
 * The character set used for the names is different with UCS - the
 * restrictions are that the following are not allowed:
 *
 *	Characters (00)(00) through (00)(1f) (control chars)
 *	(00)(2a) '*'
 *	(00)(2f) '/'
 *	(00)(3a) ':'
 *	(00)(3b) ';'
 *	(00)(3f) '?'
 *	(00)(5c) '\'
 */
#include "config.h"
#include "mkisofs.h"
#include "iso9660.h"


#include <stdlib.h>
#include <time.h>

static int jpath_table_index;
static struct directory ** jpathlist;
static int next_jpath_index  = 1;
static int sort_goof;

static int generate_joliet_path_tables	__PR((void));
static int DECL(joliet_sort_directory, (struct directory_entry ** sort_dir));
static void DECL(assign_joliet_directory_addresses, (struct directory * node));
static int jroot_gen	__PR((void));

/* 
 * Function:		convert_to_unicode
 *
 * Purpose:		Perform a 1/2 assed unicode conversion on a text
 *			string.
 *
 * Notes:		
 */
static void FDECL3(convert_to_unicode, unsigned char *, buffer, int, size, char *, source )
{
  unsigned char * tmpbuf;
  int i;
  int j;

  /*
   * If we get a NULL pointer for the source, it means we have an inplace
   * copy, and we need to make a temporary working copy first.
   */
  if( source == NULL )
    {
      tmpbuf = (u_char *) e_malloc(size);
      memcpy( tmpbuf, buffer, size);
    }
  else
    {
      tmpbuf = (u_char *)source;
    }

  /*
   * Now start copying characters.  If the size was specified to be 0, then
   * assume the input was 0 terminated.
   */
  j = 0;
  for(i=0; i < size ; i += 2, j++)
    {
      buffer[i]       = 0;
	/*
	 * JS integrated from: Achim_Kaiser@t-online.de
	 *
	 * Let all valid unicode characters pass through (assuming ISO-8859-1).
	 * Others are set to '_' . 
	 */ 
	if( tmpbuf[j] != 0 &&  
	   (tmpbuf[j] <= 0x1f || (tmpbuf[j] >= 0x7F && tmpbuf[j] <= 0xA0)) )
	{
	  buffer[i+1]     = '_';
	}
      else
	{
	  switch(tmpbuf[j])
	    {
	    case '*':
	    case '/':
	    case ':':
	    case ';':
	    case '?':
	    case '\\':
	      /*
	       * Even Joliet has some standards as to what is allowed in a pathname.
	       * Pretty tame in comparison to what DOS restricts you to.
	       */
	      buffer[i+1]     = '_';
	      break;
	    default:
	      buffer[i+1]     = tmpbuf[j];
	      break;
	    }
	}
    }

  if( source == NULL )
    {
      free(tmpbuf);
    }
}

/* 
 * Function:		joliet_strlen
 *
 * Purpose:		Return length in bytes of string after conversion to unicode.
 *
 * Notes:		This is provided mainly as a convenience so that when more intelligent
 *			Unicode conversion for either Multibyte or 8-bit codes is available that
 *			we can easily adapt.
 */
static int FDECL1(joliet_strlen, const char *, string)
{
  int rtn;

  rtn = strlen(string) << 1;

  /* 
   * We do clamp the maximum length of a Joliet string to be the
   * maximum path size.  This helps to ensure that we don't completely
   * bolix things up with very long paths.    The Joliet specs say
   * that the maximum length is 128 bytes, or 64 unicode characters.
   */
  if( rtn > 0x80)
    {
      rtn = 0x80;
    }
  return rtn;
}

/* 
 * Function:		get_joliet_vol_desc
 *
 * Purpose:		generate a Joliet compatible volume desc.
 *
 * Notes:		Assume that we have the non-joliet vol desc
 *			already present in the buffer.  Just modifiy the
 *			appropriate fields.
 */
static void FDECL1(get_joliet_vol_desc, struct iso_primary_descriptor *, jvol_desc)
{
  jvol_desc->type[0] = ISO_VD_SUPPLEMENTARY;

  /*
   * For now, always do Unicode level 3.  I don't really know what 1 and 2
   * are - perhaps a more limited Unicode set.
   *
   * FIXME(eric) - how does Romeo fit in here?  As mkisofs just
   * "expands" 8 bit character codes to 16 bits and does nothing
   * special with the Unicode characters, therefore shouldn't mkisofs
   * really be stating that it's using UCS-2 Level 1, not Level 3 for
   * the Joliet directory tree.  
   */
  strcpy(jvol_desc->escape_sequences, "%/@");

  /*
   * Until we have Unicode path tables, leave these unset.
   */
  set_733((char *) jvol_desc->path_table_size, jpath_table_size);
  set_731(jvol_desc->type_l_path_table,     jpath_table[0]);
  set_731(jvol_desc->opt_type_l_path_table, jpath_table[1]);
  set_732(jvol_desc->type_m_path_table,     jpath_table[2]);
  set_732(jvol_desc->opt_type_m_path_table, jpath_table[3]);

  /*
   * Set this one up.
   */
  memcpy(jvol_desc->root_directory_record, &jroot_record, 
	 sizeof(jvol_desc->root_directory_record));

  /*
   * Finally, we have a bunch of strings to convert to Unicode.
   * FIXME(eric) - I don't know how to do this in general, so we will
   * just be really lazy and do a char -> short conversion.  We probably
   * will want to filter any characters >= 0x80.
   */
  convert_to_unicode((u_char *)jvol_desc->system_id, sizeof(jvol_desc->system_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->volume_id, sizeof(jvol_desc->volume_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->volume_set_id, sizeof(jvol_desc->volume_set_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->publisher_id, sizeof(jvol_desc->publisher_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->preparer_id, sizeof(jvol_desc->preparer_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->application_id, sizeof(jvol_desc->application_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->copyright_file_id, sizeof(jvol_desc->copyright_file_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->abstract_file_id, sizeof(jvol_desc->abstract_file_id), NULL);
  convert_to_unicode((u_char *)jvol_desc->bibliographic_file_id, sizeof(jvol_desc->bibliographic_file_id), NULL);


}

static void FDECL1(assign_joliet_directory_addresses, struct directory *, node)
{
     int		dir_size;
     struct directory * dpnt;

     dpnt = node;
     
     while (dpnt)
     {
	 if( (dpnt->dir_flags & INHIBIT_JOLIET_ENTRY) == 0 )
	 {
	     /*
	      * If we already have an extent for this (i.e. it came from
	      * a multisession disc), then don't reassign a new extent.
	      */
	     dpnt->jpath_index = next_jpath_index++;
	     if( dpnt->jextent == 0 )
	     {
		 dpnt->jextent = last_extent;
		 dir_size = (dpnt->jsize + (SECTOR_SIZE - 1)) >> 11;
		 last_extent += dir_size;
	     }
	 }

	 /* skip if hidden - but not for the rr_moved dir */
	 if(dpnt->subdir && (!(dpnt->dir_flags & INHIBIT_JOLIET_ENTRY) || dpnt == reloc_dir)) 
	 {
	     assign_joliet_directory_addresses(dpnt->subdir);
	 }
	 dpnt = dpnt->next;
     }
}

static 
void FDECL1(build_jpathlist, struct directory *, node)
{
     struct directory * dpnt;
     
     dpnt = node;
     
     while (dpnt)

     {
       if( (dpnt->dir_flags & INHIBIT_JOLIET_ENTRY) == 0 )
	 {
	   jpathlist[dpnt->jpath_index] = dpnt;
	 }
       if(dpnt->subdir) build_jpathlist(dpnt->subdir);
       dpnt = dpnt->next;
     }
} /* build_jpathlist(... */

static int FDECL2(joliet_compare_paths, void const *, r, void const *, l) 
{
  struct directory const *ll = *(struct directory * const *)l;
  struct directory const *rr = *(struct directory * const *)r;
  int rparent, lparent;

  rparent = rr->parent->jpath_index;
  lparent = ll->parent->jpath_index;
  if( rr->parent == reloc_dir )
    {
      rparent = rr->self->parent_rec->filedir->jpath_index;
    }

  if( ll->parent == reloc_dir )
    {
      lparent = ll->self->parent_rec->filedir->jpath_index;
    }

  if (rparent < lparent)
  {
       return -1;
  }

  if (rparent > lparent) 
  {
       return 1;
  }
#ifdef APPLE_HYB
  /* use Mac name for Joliet name */
  if (USE_MAC_NAME(mac_name, rr->self) && USE_MAC_NAME(mac_name, ll->self))
    return strcmp(rr->self->hfs_ent->name, ll->self->hfs_ent->name);
  else
#endif /* APPLE_HYB */
    return strcmp(rr->self->name, ll->self->name);
  
} /* compare_paths(... */

static int generate_joliet_path_tables()
{
  struct directory_entry * de;
  struct directory	 * dpnt;
  int			   fix;
  int			   j;
  int			   namelen;
  char			 * npnt;
  char			 * npnt1;
  int			   tablesize;

  /*
   * First allocate memory for the tables and initialize the memory 
   */
  tablesize = jpath_blocks << 11;
  jpath_table_m = (char *) e_malloc(tablesize);
  jpath_table_l = (char *) e_malloc(tablesize);
  memset(jpath_table_l, 0, tablesize);
  memset(jpath_table_m, 0, tablesize);

  if( next_jpath_index > 0xffff )
  {
      fprintf(stderr, "Unable to generate sane path tables - too many directories (%d)\n",
	      next_jpath_index);
      exit(1);
  }
  /*
   * Now start filling in the path tables.  Start with root directory 
   */
  jpath_table_index = 0;
  jpathlist = (struct directory **) e_malloc(sizeof(struct directory *) 
					    * next_jpath_index);
  memset(jpathlist, 0, sizeof(struct directory *) * next_jpath_index);
  build_jpathlist(root);

  do
  {
       fix = 0;
#ifdef	__STDC__
       qsort(&jpathlist[1], next_jpath_index-1, sizeof(struct directory *), 
	     (int (*)(const void *, const void *))joliet_compare_paths);
#else
       qsort(&jpathlist[1], next_jpath_index-1, sizeof(struct directory *), 
	     joliet_compare_paths);
#endif

       for(j=1; j<next_jpath_index; j++)
       {
	    if(jpathlist[j]->jpath_index != j)
	    {
		 jpathlist[j]->jpath_index = j;
		 fix++;
	    }
       }
  } while(fix);

  for(j=1; j<next_jpath_index; j++)
  {
       dpnt = jpathlist[j];
       if(!dpnt)
       {
	    fprintf(stderr,"Entry %d not in path tables\n", j);
	    exit(1);
       }
       npnt = dpnt->de_name;
       
       npnt1 = strrchr(npnt, PATH_SEPARATOR);
       if(npnt1) 
       { 
	    npnt = npnt1 + 1;
       }
       
       de = dpnt->self;
       if(!de) 
       {
	    fprintf(stderr,"Fatal goof - directory has amnesia\n"); 
	    exit(1);
       }
       
#ifdef APPLE_HYB
       if (USE_MAC_NAME(mac_name, de))
	  namelen = joliet_strlen(de->hfs_ent->name);
       else
#endif /* APPLE_HYB */
          namelen = joliet_strlen(de->name);

       if( dpnt == root )
	 {
	   jpath_table_l[jpath_table_index] = 1;
	   jpath_table_m[jpath_table_index] = 1;
	 }
       else
	 {
	   jpath_table_l[jpath_table_index] = namelen;
	   jpath_table_m[jpath_table_index] = namelen;
	 }
       jpath_table_index += 2;
       
       set_731(jpath_table_l + jpath_table_index, dpnt->jextent); 
       set_732(jpath_table_m + jpath_table_index, dpnt->jextent); 
       jpath_table_index += 4;
       
       if( dpnt->parent != reloc_dir )
	 {
	   set_721(jpath_table_l + jpath_table_index, 
		   dpnt->parent->jpath_index); 
	   set_722(jpath_table_m + jpath_table_index, 
		   dpnt->parent->jpath_index); 
	 }
       else
	 {
	   set_721(jpath_table_l + jpath_table_index, 
		   dpnt->self->parent_rec->filedir->jpath_index); 
	   set_722(jpath_table_m + jpath_table_index, 
		   dpnt->self->parent_rec->filedir->jpath_index); 
	 }

       jpath_table_index += 2;
       
       /*
	* The root directory is still represented in non-unicode fashion.
	*/
       if( dpnt == root )
	 {
	   jpath_table_l[jpath_table_index] = 0;
	   jpath_table_m[jpath_table_index] = 0;
	   jpath_table_index ++;
	 }
       else
	 {
#ifdef APPLE_HYB
	   if (USE_MAC_NAME(mac_name , de)) {
	      convert_to_unicode((u_char *)jpath_table_l + jpath_table_index,  
			      namelen, de->hfs_ent->name);
	      convert_to_unicode((u_char *)jpath_table_m + jpath_table_index, 
			      namelen, de->hfs_ent->name);
	   }
	   else {
#endif /* APPLE_HYB */
	      convert_to_unicode((u_char *)jpath_table_l + jpath_table_index,  
			      namelen, de->name);
	      convert_to_unicode((u_char *)jpath_table_m + jpath_table_index, 
			      namelen, de->name);
#ifdef APPLE_HYB
	   }
#endif /* APPLE_HYB */

	   jpath_table_index += namelen;
	 }

       if(jpath_table_index & 1) 
       {
	    jpath_table_index++;  /* For odd lengths we pad */
       }
  }
  
  free(jpathlist);
  if(jpath_table_index != jpath_table_size)
  {
       fprintf(stderr,"Joliet path table lengths do not match %d %d\n",
	       jpath_table_index,
	       jpath_table_size);
  }
  return 0;
} /* generate_path_tables(... */

static void FDECL2(generate_one_joliet_directory, struct directory *, dpnt, FILE *, outfile)
{
     unsigned int			  dir_index;
     char				* directory_buffer;
     int				  new_reclen;
     struct directory_entry		* s_entry;
     struct directory_entry		* s_entry1;
     struct iso_directory_record	  jrec;
     unsigned int			  total_size;
     int				  cvt_len;
     struct directory			* finddir;
     
     total_size = (dpnt->jsize + (SECTOR_SIZE - 1)) &  ~(SECTOR_SIZE - 1);
     directory_buffer = (char *) e_malloc(total_size);
     memset(directory_buffer, 0, total_size);
     dir_index = 0;
     
     s_entry = dpnt->jcontents;
     while(s_entry) 
     {
	     if(s_entry->de_flags & INHIBIT_JOLIET_ENTRY) {
		s_entry = s_entry->jnext;
		continue;
	     }
		
	     /*
	      * If this entry was a directory that was relocated, we have a bit
	      * of trouble here.  We need to dig out the real thing and put it
	      * back here.  In the Joliet tree, there is no relocated rock
	      * ridge, as there are no depth limits to a directory tree.
	      */
	     if( (s_entry->de_flags & RELOCATED_DIRECTORY) != 0 )
	     {
		 for(s_entry1 = reloc_dir->contents; s_entry1; s_entry1 = s_entry1->next)
		 {
		     if( s_entry1->parent_rec == s_entry )
		     {
			 break;
		     }
		 }
		 if( s_entry1 == NULL )
		 {
		     /*
		      * We got trouble.
		      */
		     fprintf(stderr, "Unable to locate relocated directory\n");
		     exit(1);
		 }
	     }
	     else
	     {
		 s_entry1 = s_entry;
	     }
	      
	     /* 
	      * We do not allow directory entries to cross sector boundaries.  
	      * Simply pad, and then start the next entry at the next sector 
	      */
	     new_reclen = s_entry1->jreclen;
	     if( (dir_index & (SECTOR_SIZE - 1)) + new_reclen >= SECTOR_SIZE )
	     {
		 dir_index = (dir_index + (SECTOR_SIZE - 1)) & 
		     ~(SECTOR_SIZE - 1);
	     }
	     
	     memcpy(&jrec, &s_entry1->isorec, sizeof(struct iso_directory_record) -
		    sizeof(s_entry1->isorec.name));
	     
#ifdef APPLE_HYB
	     /* Use the HFS name if it exists */
	     if (USE_MAC_NAME(mac_name, s_entry1))
	       cvt_len = joliet_strlen(s_entry1->hfs_ent->name);
	     else
#endif /* APPLE_HYB */
	       cvt_len = joliet_strlen(s_entry1->name);
	     
	     /*
	      * Fix the record length - this was the non-Joliet version we
	      * were seeing.
	      */
	     jrec.name_len[0] = cvt_len;
	     jrec.length[0] = s_entry1->jreclen;
	     
	     /*
	      * If this is a directory, fix the correct size and extent
	      * number.
	      */
	     if( (jrec.flags[0] & 2) != 0 )
	     {
		 if(strcmp(s_entry1->name,".") == 0) 
		 {
		     jrec.name_len[0] = 1;
		     set_733((char *) jrec.extent, dpnt->jextent);
		     set_733((char *) jrec.size, ROUND_UP(dpnt->jsize));
		 }
		 else if(strcmp(s_entry1->name,"..") == 0) 
		 {
		     jrec.name_len[0] = 1;
		     if( dpnt->parent == reloc_dir )
		     {
			 set_733((char *) jrec.extent, dpnt->self->parent_rec->filedir->jextent);
			 set_733((char *) jrec.size, ROUND_UP(dpnt->self->parent_rec->filedir->jsize));
		     }
		     else

		     {
			 set_733((char *) jrec.extent, dpnt->parent->jextent);
			 set_733((char *) jrec.size, ROUND_UP(dpnt->parent->jsize));
		     }
		 }
		 else
		 {
		     if( (s_entry->de_flags & RELOCATED_DIRECTORY) != 0 )
		     {
			 finddir = reloc_dir->subdir;
		     }
		     else
		     {
			 finddir = dpnt->subdir;
		     }
		     while(1==1)
		     {
			 if(finddir->self == s_entry1) break;
			 finddir = finddir->next;
			 if(!finddir) 
			 {
			     fprintf(stderr,"Fatal goof - unable to find directory location\n"); exit(1);
			 }
		     }
		     set_733((char *) jrec.extent, finddir->jextent);
		     set_733((char *) jrec.size, ROUND_UP(finddir->jsize));
		 }
	     }
	     
	     memcpy(directory_buffer + dir_index, &jrec, 
		    sizeof(struct iso_directory_record) -
		    sizeof(s_entry1->isorec.name));
	     
	     
	     dir_index += sizeof(struct iso_directory_record) - 
		 sizeof (s_entry1->isorec.name);
	     
	     /*
	      * Finally dump the Unicode version of the filename.
	      * Note - . and .. are the same as with non-Joliet discs.
	      */
	     if( (jrec.flags[0] & 2) != 0 
		 && strcmp(s_entry1->name, ".") == 0 )
	     {
		 directory_buffer[dir_index++] = 0;
	     }
	     else if( (jrec.flags[0] & 2) != 0 
		      && strcmp(s_entry1->name, "..") == 0 )
	     {
		 directory_buffer[dir_index++] = 1;
	     }
	     else
	     {
#ifdef APPLE_HYB
	       if (USE_MAC_NAME(mac_name, s_entry1))
		 /* Use the HFS name if it exists */
		 convert_to_unicode((u_char *)directory_buffer + dir_index,
				    cvt_len,
				    s_entry1->hfs_ent->name);
	       else
#endif /* APPLE_HYB */
		 convert_to_unicode((u_char *)directory_buffer + dir_index,
				    cvt_len,
				    s_entry1->name);
		 dir_index += cvt_len;
	     }
	     
	     if(dir_index & 1)
	     {
		 directory_buffer[dir_index++] = 0;
	     }

	     s_entry = s_entry->jnext;
     }
     
     if(dpnt->jsize != dir_index)
     {
	 fprintf(stderr,"Unexpected joliet directory length %d %d %s\n",dpnt->jsize, 
		 dir_index, dpnt->de_name);
     }
     
     xfwrite(directory_buffer, 1, total_size, outfile);
     last_extent_written += total_size >> 11;
     free(directory_buffer);
} /* generate_one_joliet_directory(... */

static int FDECL1(joliet_sort_n_finish, struct directory *, this_dir)
{
  struct directory_entry  * s_entry;
  int			    status = 0;

  /* don't want to skip this directory if it's the reloc_dir at the moment */
  if(this_dir != reloc_dir && this_dir->dir_flags & INHIBIT_JOLIET_ENTRY)
    {
      return 0;
    }

  for(s_entry = this_dir->contents; s_entry; s_entry = s_entry->next)
    {
      /* skip hidden entries */
      if(  (s_entry->de_flags & INHIBIT_JOLIET_ENTRY) != 0 )
	{
	  continue;
	}
	  
      /*
       * First update the path table sizes for directories.
       *
       * Finally, set the length of the directory entry if Joliet is used.
       * The name is longer, but no Rock Ridge is ever used here, so
       * depending upon the options the entry size might turn out to be about
       * the same.  The Unicode name is always a multiple of 2 bytes, so
       * we always add 1 to make it an even number.
       */
      if(s_entry->isorec.flags[0] ==  2)
	{
	  if (strcmp(s_entry->name,".") && strcmp(s_entry->name,"..")) 
	    {
#ifdef APPLE_HYB
	   if (USE_MAC_NAME(mac_name, s_entry))
	      /* Use the HFS name if it exists */
	      jpath_table_size += joliet_strlen(s_entry->hfs_ent->name) + sizeof(struct iso_path_table) - 1;
	   else
#endif /* APPLE_HYB */
	      jpath_table_size += joliet_strlen(s_entry->name) + sizeof(struct iso_path_table) - 1;
	      if (jpath_table_size & 1) 
		{
		  jpath_table_size++;
		}
	    }
	  else 
	    {
	      if (this_dir == root && strlen(s_entry->name) == 1)
		{
		  jpath_table_size += sizeof(struct iso_path_table);
		  if (jpath_table_size & 1) jpath_table_size++;
		}
	    }
	}

      if (strcmp(s_entry->name,".") && strcmp(s_entry->name,"..")) 
	{
#ifdef APPLE_HYB
	 if (USE_MAC_NAME(mac_name, s_entry))
	  /* Use the HFS name if it exists */
	  s_entry->jreclen = 	sizeof(struct iso_directory_record)
	    - sizeof(s_entry->isorec.name)
	    + joliet_strlen(s_entry->hfs_ent->name) 
	    + 1;
	 else
#endif /* APPLE_HYB */
	  s_entry->jreclen = 	sizeof(struct iso_directory_record)
	    - sizeof(s_entry->isorec.name)
	    + joliet_strlen(s_entry->name) 
	    + 1;
	}
      else
	{
	  /*
	   * Special - for '.' and '..' we generate the same records we
	   * did for non-Joliet discs.
	   */
	  s_entry->jreclen = 	sizeof(struct iso_directory_record)
	    - sizeof(s_entry->isorec.name)
	    + 1;
	}


    }

  if( (this_dir->dir_flags & INHIBIT_JOLIET_ENTRY) != 0 )
    {
      return 0;
    }

  this_dir->jcontents = this_dir->contents;
  status = joliet_sort_directory(&this_dir->jcontents);

  /* 
   * Now go through the directory and figure out how large this one will be.
   * Do not split a directory entry across a sector boundary 
   */
  s_entry = this_dir->jcontents;
/*
 * XXX Is it ok to comment this out?
 */
/*XXX JS  this_dir->ce_bytes = 0;*/
  for(s_entry = this_dir->jcontents; s_entry; s_entry = s_entry->jnext)
    {
      int jreclen;

      if( (s_entry->de_flags & INHIBIT_JOLIET_ENTRY) != 0 )
	{
	  continue;
	}

      jreclen = s_entry->jreclen;
      
      if ((this_dir->jsize & (SECTOR_SIZE - 1)) + jreclen >= SECTOR_SIZE)
	{
	  this_dir->jsize = (this_dir->jsize + (SECTOR_SIZE - 1)) & 
	    ~(SECTOR_SIZE - 1);
	}
      this_dir->jsize += jreclen;
    }
  return status;
}

/*
 * Similar to the iso9660 case, except here we perform a full sort based upon the
 * regular name of the file, not the 8.3 version.
 */
static int FDECL2(joliet_compare_dirs, const void *, rr, const void *, ll) 
{
     char * rpnt, *lpnt;
     struct directory_entry ** r, **l;
     
     r = (struct directory_entry **) rr;
     l = (struct directory_entry **) ll;
     rpnt = (*r)->name;
     lpnt = (*l)->name;

     /*
      * If the entries are the same, this is an error.
      */
     if( strcmp(rpnt, lpnt) == 0 )
       {
	 sort_goof++;
       }
     
     /*
      *  Put the '.' and '..' entries on the head of the sorted list.
      *  For normal ASCII, this always happens to be the case, but out of
      *  band characters cause this not to be the case sometimes.
      */
     if( strcmp(rpnt, ".") == 0 ) return -1;
     if( strcmp(lpnt, ".") == 0 ) return  1;

     if( strcmp(rpnt, "..") == 0 ) return -1;
     if( strcmp(lpnt, "..") == 0 ) return  1;

     while(*rpnt && *lpnt) 
     {
	  if(*rpnt == ';' && *lpnt != ';') return -1;
	  if(*rpnt != ';' && *lpnt == ';') return 1;
	  
	  if(*rpnt == ';' && *lpnt == ';') return 0;
	  
	  /*
	   * Extensions are not special here.  Don't treat the dot as something that
	   * must be bumped to the start of the list.
	   */
#if 0
	  if(*rpnt == '.' && *lpnt != '.') return -1;
	  if(*rpnt != '.' && *lpnt == '.') return 1;
#endif
	  
	  if(*rpnt < *lpnt) return -1;
	  if(*rpnt > *lpnt) return 1;
	  rpnt++;  lpnt++;
     }
     if(*rpnt) return 1;
     if(*lpnt) return -1;
     return 0;
}


/* 
 * Function:		sort_directory
 *
 * Purpose:		Sort the directory in the appropriate ISO9660
 *			order.
 *
 * Notes:		Returns 0 if OK, returns > 0 if an error occurred.
 */
static int FDECL1(joliet_sort_directory, struct directory_entry **, sort_dir)
{
     int dcount = 0;
     int i;
     struct directory_entry * s_entry;
     struct directory_entry ** sortlist;
     
     s_entry = *sort_dir;
     while(s_entry)
     {
	  /* skip hidden entries */
	  if (!(s_entry->de_flags & INHIBIT_JOLIET_ENTRY))
	    dcount++;
	  s_entry = s_entry->next;
     }

     /*
      * OK, now we know how many there are.  Build a vector for sorting. 
      */
     sortlist =   (struct directory_entry **) 
	  e_malloc(sizeof(struct directory_entry *) * dcount);

     dcount = 0;
     s_entry = *sort_dir;
     while(s_entry)
     {
	  /* skip hidden entries */
	  if (!(s_entry->de_flags & INHIBIT_JOLIET_ENTRY)) {
	    sortlist[dcount] = s_entry;
	    dcount++;
	  }
	  s_entry = s_entry->next;
     }
  
     sort_goof = 0;
#ifdef	__STDC__
     qsort(sortlist, dcount, sizeof(struct directory_entry *), 
	   (int (*)(const void *, const void *))joliet_compare_dirs);
#else
     qsort(sortlist, dcount, sizeof(struct directory_entry *), 
	   joliet_compare_dirs);
#endif
     
     /* 
      * Now reassemble the linked list in the proper sorted order 
      */
     for(i=0; i<dcount-1; i++)
     {
	  sortlist[i]->jnext = sortlist[i+1];
     }

     sortlist[dcount-1]->jnext = NULL;
     *sort_dir = sortlist[0];
     
     free(sortlist);
     return sort_goof;
}

int FDECL1(joliet_sort_tree, struct directory *, node)
{
  struct directory * dpnt;
  int ret = 0;

  dpnt = node;

  while (dpnt){
    ret = joliet_sort_n_finish(dpnt);
    if( ret )
      {
	break;
      }
    if(dpnt->subdir) ret = joliet_sort_tree(dpnt->subdir);
    if( ret )
      {
	break;
      }
    dpnt = dpnt->next;
  }
  return ret;
}

static void FDECL2(generate_joliet_directories, struct directory *, node, FILE*, outfile){
  struct directory * dpnt;

  dpnt = node;

  while (dpnt)
    {
      if( (dpnt->dir_flags & INHIBIT_JOLIET_ENTRY) == 0 )
	{
	  /*
	   * In theory we should never reuse a directory, so this doesn't
	   * make much sense.
	   */
	  if( dpnt->jextent > session_start )
	    {
	      generate_one_joliet_directory(dpnt, outfile);
	    }
	}
      /* skip if hidden - but not for the rr_moved dir */
      if(dpnt->subdir && (!(dpnt->dir_flags & INHIBIT_JOLIET_ENTRY) || dpnt == reloc_dir)) 
	generate_joliet_directories(dpnt->subdir, outfile);
      dpnt = dpnt->next;
    }
}


/*
 * Function to write the EVD for the disc.
 */
static int FDECL1(jpathtab_write, FILE *, outfile)
{
  /*
   * Next we write the path tables 
   */
  xfwrite(jpath_table_l, 1, jpath_blocks << 11, outfile);
  xfwrite(jpath_table_m, 1, jpath_blocks << 11, outfile);
  last_extent_written += 2*jpath_blocks;
  free(jpath_table_l);
  free(jpath_table_m);
  jpath_table_l = NULL;
  jpath_table_m = NULL;
  return 0;
}

static int FDECL1(jdirtree_size, int, starting_extent)
{
  assign_joliet_directory_addresses(root);
  return 0;
}

static int jroot_gen()
{
     jroot_record.length[0] = 1 + sizeof(struct iso_directory_record)
	  - sizeof(jroot_record.name);
     jroot_record.ext_attr_length[0] = 0;
     set_733((char *) jroot_record.extent, root->jextent);
     set_733((char *) jroot_record.size, ROUND_UP(root->jsize));
     iso9660_date(jroot_record.date, root_statbuf.st_mtime);
     jroot_record.flags[0] = 2;
     jroot_record.file_unit_size[0] = 0;
     jroot_record.interleave[0] = 0;
     set_723(jroot_record.volume_sequence_number, volume_sequence_number);
     jroot_record.name_len[0] = 1;
     return 0;
}

static int FDECL1(jdirtree_write, FILE *, outfile)
{
  generate_joliet_directories(root, outfile);
  return 0;
}

/*
 * Function to write the EVD for the disc.
 */
static int FDECL1(jvd_write, FILE *, outfile)
{
  struct iso_primary_descriptor jvol_desc;

  /*
   * Next we write out the boot volume descriptor for the disc 
   */
  jvol_desc = vol_desc;
  get_joliet_vol_desc(&jvol_desc);
  xfwrite(&jvol_desc, 1, 2048, outfile);
  last_extent_written ++;
  return 0;
}

/*
 * Functions to describe padding block at the start of the disc.
 */
static int FDECL1(jpathtab_size, int, starting_extent)
{
  jpath_table[0] = starting_extent;
  jpath_table[1] = 0;
  jpath_table[2] = jpath_table[0] + jpath_blocks;
  jpath_table[3] = 0;
  
  last_extent += 2*jpath_blocks;
  return 0;
}

struct output_fragment joliet_desc    = {NULL, oneblock_size, jroot_gen,jvd_write};
struct output_fragment jpathtable_desc= {NULL, jpathtab_size, generate_joliet_path_tables,     jpathtab_write};
struct output_fragment jdirtree_desc  = {NULL, jdirtree_size, NULL,     jdirtree_write};
