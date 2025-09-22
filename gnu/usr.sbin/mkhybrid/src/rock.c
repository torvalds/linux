/*
 * File rock.c - generate RRIP  records for iso9660 filesystems.

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

#ifndef VMS
#if defined(MAJOR_IN_SYSMACROS)
#include <sys/sysmacros.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#endif
#if defined(MAJOR_IN_MKDEV)
#include <sys/types.h>
#include <sys/mkdev.h>
#endif

#include "mkisofs.h"
#include "iso9660.h"
#include <string.h>

#ifdef	DOESNT_WORK

#ifdef NON_UNIXFS
#define S_ISLNK(m)	(0)
#else
#ifndef S_ISLNK
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
#endif

#else
#include <statdefs.h>
#endif

#define SU_VERSION 1

#define SL_ROOT    8
#define SL_PARENT  4
#define SL_CURRENT 2
#define SL_CONTINUE 1

#define CE_SIZE 28
#define CL_SIZE 12
#define ER_SIZE 8
#define NM_SIZE 5
#define PL_SIZE 12
#define PN_SIZE 20
#define PX_SIZE 36
#define RE_SIZE 4
#define SL_SIZE 20
#define ZZ_SIZE 15
#ifdef APPLE_HYB
#define AA_SIZE 14		/* size of Apple extension */
#endif /* APPLE_HYB */
#ifdef __QNX__
#define TF_SIZE (5 + 4 * 7)
#else
#define TF_SIZE (5 + 3 * 7)
#endif

/* If we need to store this number of bytes, make sure we
   do not box ourselves in so that we do not have room for
   a CE entry for the continuation record */

#define MAYBE_ADD_CE_ENTRY(BYTES) \
    (BYTES + CE_SIZE + currlen + (ipnt - recstart) > reclimit ? 1 : 0) 

/*
 * Buffer to build RR attributes
 */

static unsigned char Rock[16384];
static unsigned char symlink_buff[256];
static int ipnt = 0;
static int recstart = 0;
static int currlen = 0;
static int mainrec = 0;
static int reclimit;

#ifdef APPLE_HYB
/* if we are using the HFS name, we don't want the '/' character */
static void
rstrncpy(char *t, char *f, int c)
{
	while (c-- && *f) {
	    switch (*f) {
		case '/':
		    *t = '_';
		    break;
		default:
		    *t = *f;
		    break;
	    }
	    t++; f++;
	}
}
#endif /* APPLE HYB */

static void add_CE_entry	__PR((void));

static void add_CE_entry(){
          if(recstart)
	    set_733((char*)Rock + recstart - 8, ipnt + 28 - recstart);
	  Rock[ipnt++] ='C';
	  Rock[ipnt++] ='E';
	  Rock[ipnt++] = CE_SIZE;
	  Rock[ipnt++] = SU_VERSION;
	  set_733((char*)Rock + ipnt, 0);
	  ipnt += 8;
	  set_733((char*)Rock + ipnt, 0);
	  ipnt += 8;
	  set_733((char*)Rock + ipnt, 0);
	  ipnt += 8;
	  recstart = ipnt;
	  currlen = 0;
	  if(!mainrec) mainrec = ipnt;
	  reclimit = SECTOR_SIZE - 8; /* Limit to one sector */
}

#ifdef __STDC__
int generate_rock_ridge_attributes (char * whole_name, char * name,
				    struct directory_entry * s_entry,
				    struct stat * statbuf,
				    struct stat * lstatbuf,
				    int deep_opt)
#else
int generate_rock_ridge_attributes (whole_name, name,
				    s_entry,
				    statbuf,
				    lstatbuf,
				    deep_opt)
char * whole_name; char * name; struct directory_entry * s_entry;
struct stat * statbuf, *lstatbuf;
int deep_opt;
#endif
{
  int flagpos, flagval;
  int need_ce;

  statbuf = statbuf;        /* this shuts up unreferenced compiler warnings */
  mainrec = recstart = ipnt = 0;
  reclimit = 0xf8;

  /* no need to fill in the RR stuff if we won't see the file */
  if (s_entry->de_flags & INHIBIT_ISO9660_ENTRY)
    return 0;

  /* Obtain the amount of space that is currently used for the directory
     record.  Assume max for name, since name conflicts may cause us
     to rename the file later on */
  currlen = sizeof(s_entry->isorec);

#ifdef APPLE_HYB
  /* if we have regular file, then add Apple extensions */
  if (S_ISREG(lstatbuf->st_mode) && apple_ext && s_entry->hfs_ent) {
    Rock[ipnt++] ='A';		/* AppleSignature */
    Rock[ipnt++] ='A';
    Rock[ipnt++] = AA_SIZE;	/* includes AppleSignature bytes */
    Rock[ipnt++] = 0x02;	/* SystemUseID */
    Rock[ipnt++] = s_entry->hfs_ent->type[0];
    Rock[ipnt++] = s_entry->hfs_ent->type[1];
    Rock[ipnt++] = s_entry->hfs_ent->type[2];
    Rock[ipnt++] = s_entry->hfs_ent->type[3];
    Rock[ipnt++] = s_entry->hfs_ent->creator[0];
    Rock[ipnt++] = s_entry->hfs_ent->creator[1];
    Rock[ipnt++] = s_entry->hfs_ent->creator[2];
    Rock[ipnt++] = s_entry->hfs_ent->creator[3];
    Rock[ipnt++] = (s_entry->hfs_ent->fdflags >> 8) & 0xff;
    Rock[ipnt++] = s_entry->hfs_ent->fdflags & 0xff;
  }
#endif /* APPLE_HYB */

  /* Identify that we are using the SUSP protocol */
  if(deep_opt & NEED_SP){
	  Rock[ipnt++] ='S';
	  Rock[ipnt++] ='P';
	  Rock[ipnt++] = 7;
	  Rock[ipnt++] = SU_VERSION;
	  Rock[ipnt++] = 0xbe;
	  Rock[ipnt++] = 0xef;
	  Rock[ipnt++] = 0;
  };

  /* First build the posix name field */
  Rock[ipnt++] ='R';
  Rock[ipnt++] ='R';
  Rock[ipnt++] = 5;
  Rock[ipnt++] = SU_VERSION;
  flagpos = ipnt;
  flagval = 0;
  Rock[ipnt++] = 0;   /* We go back and fix this later */

  if(strcmp(name,".")  && strcmp(name,"..")){
    char * npnt;
    int remain, use;

#ifdef APPLE_HYB 
    /* use the HFS name if it exists */
    if (USE_MAC_NAME(mac_name, s_entry)) {
	remain = strlen(s_entry->hfs_ent->name);
	npnt = s_entry->hfs_ent->name;
    }
    else {
#endif

	remain = strlen(name);
	npnt = name;
#ifdef APPLE_HYB
    }
#endif /* APPLE_HYB */

    while(remain){
          use = remain;
	  need_ce = 0;
	  /* Can we fit this SUSP and a CE entry? */
	  if(use + currlen + CE_SIZE + (ipnt - recstart) > reclimit) {
	    use = reclimit - currlen - CE_SIZE - (ipnt - recstart);
	    need_ce++;
	  }

	  /* Only room for 256 per SUSP field */
	  if(use > 0xf8) use = 0xf8;

	  /* First build the posix name field */
	  Rock[ipnt++] ='N';
	  Rock[ipnt++] ='M';
	  Rock[ipnt++] = NM_SIZE + use;
	  Rock[ipnt++] = SU_VERSION;
	  Rock[ipnt++] = (remain != use ? 1 : 0);
	  flagval |= (1<<3);
#ifdef APPLE_HYB
	  /* filter out any '/' character in HFS filename */
	  if (USE_MAC_NAME(mac_name, s_entry))
	    rstrncpy((char *)&Rock[ipnt], npnt, use);
	  else
#endif /* APPLE_HYB */
	    strncpy((char *)&Rock[ipnt], npnt, use);
	  npnt += use;
	  ipnt += use;
	  remain -= use;
	  if(remain && need_ce) add_CE_entry();
	};
  };

  /*
   * Add the posix modes 
   */
  if(MAYBE_ADD_CE_ENTRY(PX_SIZE)) add_CE_entry();
  Rock[ipnt++] ='P';
  Rock[ipnt++] ='X';
  Rock[ipnt++] = PX_SIZE;
  Rock[ipnt++] = SU_VERSION;  
  flagval |= (1<<0);
  set_733((char*)Rock + ipnt, lstatbuf->st_mode);
  ipnt += 8;
  set_733((char*)Rock + ipnt, lstatbuf->st_nlink);
  ipnt += 8;
  set_733((char*)Rock + ipnt, lstatbuf->st_uid);
  ipnt += 8;
  set_733((char*)Rock + ipnt, lstatbuf->st_gid);
  ipnt += 8;

  /*
   * Check for special devices
   */
#ifndef NON_UNIXFS
  if (S_ISCHR(lstatbuf->st_mode) || S_ISBLK(lstatbuf->st_mode)) {
    if(MAYBE_ADD_CE_ENTRY(PN_SIZE)) add_CE_entry();
    Rock[ipnt++] ='P';
    Rock[ipnt++] ='N';
    Rock[ipnt++] = PN_SIZE;
    Rock[ipnt++] = SU_VERSION;  
    flagval |= (1<<1);
#if defined(MAJOR_IN_SYSMACROS) || defined(MAJOR_IN_MKDEV)
    set_733((char*)Rock + ipnt, major(lstatbuf->st_rdev ));
    ipnt += 8;
    set_733((char*)Rock + ipnt, minor(lstatbuf->st_rdev));
    ipnt += 8;
#else
    /*
     * If we don't have sysmacros.h, then we have to guess as to how
     * best to pick apart the device number for major/minor.
     * Note: this may very well be wrong for many systems, so
     * it is always best to use the major/minor macros if the
     * system supports it.
     */
    if(sizeof(dev_t) <= 2) {
        set_733((char*)Rock + ipnt, (lstatbuf->st_rdev >> 8));
        ipnt += 8;
        set_733((char*)Rock + ipnt, lstatbuf->st_rdev & 0xff);
        ipnt += 8;
    }
    else if(sizeof(dev_t) <= 4) {
        set_733((char*)Rock + ipnt, (lstatbuf->st_rdev >> 8) >> 8);
        ipnt += 8;
        set_733((char*)Rock + ipnt, lstatbuf->st_rdev & 0xffff);
        ipnt += 8;
    }
    else {
        set_733((char*)Rock + ipnt, (lstatbuf->st_rdev >> 16) >> 16);
        ipnt += 8;
        set_733((char*)Rock + ipnt, lstatbuf->st_rdev);
        ipnt += 8;
    }
#endif
  };
#endif
  /*
   * Check for and symbolic links.  VMS does not have these.
   */
  if (S_ISLNK(lstatbuf->st_mode)){
    int lenpos, lenval, j0, j1;
    int nchar;
    unsigned char * cpnt, *cpnt1;
    nchar = readlink(whole_name, (char *)symlink_buff, sizeof(symlink_buff)-1);
    symlink_buff[nchar < 0 ? 0 : nchar] = 0;
    nchar = strlen((char *) symlink_buff);
    set_733(s_entry->isorec.size, 0);
    cpnt = &symlink_buff[0];
    flagval |= (1<<2);

    if (! split_SL_field) 
      {
	int sl_bytes = 0;
	for (cpnt1 = cpnt; *cpnt1 != '\0'; cpnt1++) 
	  {
	    if (*cpnt1 == '/') 
	      {
		sl_bytes += 4;
	      } 
	    else 
	      {
		sl_bytes += 1;
	      }
	  }
	if (sl_bytes > 250) 
	  {
	    /* 
	     * the symbolic link won't fit into one SL System Use Field
	     * print an error message and continue with splited one 
	     */
	    fprintf(stderr,"symbolic link ``%s'' to long for one SL System Use Field, splitting", cpnt);
	  }
       if(MAYBE_ADD_CE_ENTRY(SL_SIZE + sl_bytes)) add_CE_entry();
     }

    while(nchar){
      if(MAYBE_ADD_CE_ENTRY(SL_SIZE)) add_CE_entry();
      Rock[ipnt++] ='S';
      Rock[ipnt++] ='L';
      lenpos = ipnt;
      Rock[ipnt++] = SL_SIZE;
      Rock[ipnt++] = SU_VERSION;  
      Rock[ipnt++] = 0; /* Flags */
      lenval = 5;
      while(*cpnt){
	cpnt1 = (unsigned char *) strchr((char *) cpnt, '/');
	if(cpnt1) {
	  nchar--;
	  *cpnt1 = 0;
	};
	
	/* We treat certain components in a special way.  */
	if(cpnt[0] == '.' && cpnt[1] == '.' && cpnt[2] == 0){
	  if(MAYBE_ADD_CE_ENTRY(2)) add_CE_entry();
	  Rock[ipnt++] = SL_PARENT;
	  Rock[ipnt++] = 0;  /* length is zero */
	  lenval += 2;
	  nchar -= 2;
	} else if(cpnt[0] == '.' && cpnt[1] == 0){
	  if(MAYBE_ADD_CE_ENTRY(2)) add_CE_entry();
	  Rock[ipnt++] = SL_CURRENT;
	  Rock[ipnt++] = 0;  /* length is zero */
	  lenval += 2;
	  nchar -= 1;
	} else if(cpnt[0] == 0){
	  if(MAYBE_ADD_CE_ENTRY(2)) add_CE_entry();
	  Rock[ipnt++] = SL_ROOT;
	  Rock[ipnt++] = 0;  /* length is zero */
	  lenval += 2;
	} else {
	  /* If we do not have enough room for a component, start
	     a new continuations segment now */
         if(split_SL_component ? MAYBE_ADD_CE_ENTRY(6) :
                                 MAYBE_ADD_CE_ENTRY(6 + strlen ((char *) cpnt))) 
	   {
	     add_CE_entry();
	     if(cpnt1)
	       {
		 *cpnt1 = '/';
		 nchar++;
		 cpnt1 = NULL; /* A kluge so that we can restart properly */
	       }
	     break;
	   }
	  j0 = strlen((char *) cpnt);
	  while(j0) {
	    j1 = j0;
	    if(j1 > 0xf8) j1 = 0xf8;
	    need_ce = 0;
	    if(j1 + currlen + CE_SIZE + (ipnt - recstart) > reclimit) {
	      j1 = reclimit - currlen - CE_SIZE - (ipnt - recstart);
	      need_ce++;
	    }
	    Rock[ipnt++] = (j1 != j0 ? SL_CONTINUE : 0);
	    Rock[ipnt++] = j1;
	    strncpy((char *) Rock + ipnt, (char *) cpnt, j1);
	    ipnt += j1;
	    lenval += j1 + 2;
	    cpnt += j1;
	    nchar -= j1;  /* Number we processed this time */
	    j0 -= j1;
	    if(need_ce) {
	      add_CE_entry();
	      if(cpnt1) {
		*cpnt1 = '/';
                nchar++;
		cpnt1 = NULL; /* A kluge so that we can restart properly */
	      }
	      break;
	    }
	  }
	};
	if(cpnt1) {
	  cpnt = cpnt1 + 1;
	} else
	  break;
      }
      Rock[lenpos] = lenval;
      if(nchar) Rock[lenpos + 2] = SL_CONTINUE; /* We need another SL entry */
    } /* while nchar */
  } /* Is a symbolic link */
  /* 
   * Add in the Rock Ridge TF time field
   */
  if(MAYBE_ADD_CE_ENTRY(TF_SIZE)) add_CE_entry();
  Rock[ipnt++] ='T';
  Rock[ipnt++] ='F';
  Rock[ipnt++] = TF_SIZE;
  Rock[ipnt++] = SU_VERSION;
#ifdef __QNX__
  Rock[ipnt++] = 0x0f;
#else
  Rock[ipnt++] = 0x0e;
#endif
  flagval |= (1<<7);
#ifdef __QNX__
  iso9660_date((char *) &Rock[ipnt], lstatbuf->st_ftime);
  ipnt += 7;
#endif
  iso9660_date((char *) &Rock[ipnt], lstatbuf->st_mtime);
  ipnt += 7;
  iso9660_date((char *) &Rock[ipnt], lstatbuf->st_atime);
  ipnt += 7;
  iso9660_date((char *) &Rock[ipnt], lstatbuf->st_ctime);
  ipnt += 7;

  /* 
   * Add in the Rock Ridge RE time field
   */
  if(deep_opt & NEED_RE){
          if(MAYBE_ADD_CE_ENTRY(RE_SIZE)) add_CE_entry();
	  Rock[ipnt++] ='R';
	  Rock[ipnt++] ='E';
	  Rock[ipnt++] = RE_SIZE;
	  Rock[ipnt++] = SU_VERSION;
	  flagval |= (1<<6);
  };
  /* 
   * Add in the Rock Ridge PL record, if required.
   */
  if(deep_opt & NEED_PL){
          if(MAYBE_ADD_CE_ENTRY(PL_SIZE)) add_CE_entry();
	  Rock[ipnt++] ='P';
	  Rock[ipnt++] ='L';
	  Rock[ipnt++] = PL_SIZE;
	  Rock[ipnt++] = SU_VERSION;
	  set_733((char*)Rock + ipnt, 0);
	  ipnt += 8;
	  flagval |= (1<<5);
  };

  /* 
   * Add in the Rock Ridge CL field, if required.
   */
  if(deep_opt & NEED_CL){
          if(MAYBE_ADD_CE_ENTRY(CL_SIZE)) add_CE_entry();
	  Rock[ipnt++] ='C';
	  Rock[ipnt++] ='L';
	  Rock[ipnt++] = CL_SIZE;
	  Rock[ipnt++] = SU_VERSION;
	  set_733((char*)Rock + ipnt, 0);
	  ipnt += 8;
	  flagval |= (1<<4);
  };

#ifndef VMS
  /* If transparent compression was requested, fill in the correct
     field for this file */
  if(transparent_compression && 
     S_ISREG(lstatbuf->st_mode) &&
     strlen(name) > 3 &&
     strcmp(name + strlen(name) - 3,".gZ") == 0){
    FILE * zipfile;
    char * checkname;
    unsigned int file_size;
    unsigned char header[8];
    int OK_flag;

    /* First open file and verify that the correct algorithm was used */
    file_size = 0;
    OK_flag = 1;

    zipfile = fopen(whole_name, "rb");
    fread(header, 1, sizeof(header), zipfile);

    /* Check some magic numbers from gzip. */
    if(header[0] != 0x1f || header[1] != 0x8b || header[2] != 8) OK_flag = 0;
    /* Make sure file was blocksized. */
    if(((header[3] & 0x40) == 0)) OK_flag = 0;
    /* OK, now go to the end of the file and get some more info */
    if(OK_flag){
      int status;
      status = (long)lseek(fileno(zipfile), (off_t)(-8), SEEK_END);
      if(status == -1) OK_flag = 0;
    }
    if(OK_flag){
      if(read(fileno(zipfile), (char*)header, sizeof(header)) != sizeof(header))
	OK_flag = 0;
      else {
	int blocksize;
	blocksize = (header[3] << 8) | header[2];
	file_size = ((unsigned int)header[7] << 24) | 
		    ((unsigned int)header[6] << 16) | 
		    ((unsigned int)header[5] << 8)  | header[4];
#if 0
	fprintf(stderr,"Blocksize = %d %d\n", blocksize, file_size);
#endif
	if(blocksize != SECTOR_SIZE) OK_flag = 0;
      }
    }
    fclose(zipfile);

    checkname = strdup(whole_name);
    checkname[strlen(whole_name)-3] = 0;
    zipfile = fopen(checkname, "rb");
    if(zipfile) {
      OK_flag = 0;
      fprintf(stderr,"Unable to insert transparent compressed file - name conflict\n");
      fclose(zipfile);
    }

    free(checkname);

    if(OK_flag){
      if(MAYBE_ADD_CE_ENTRY(ZZ_SIZE)) add_CE_entry();
      Rock[ipnt++] ='Z';
      Rock[ipnt++] ='Z';
      Rock[ipnt++] = ZZ_SIZE;
      Rock[ipnt++] = SU_VERSION;
      Rock[ipnt++] = 'g'; /* Identify compression technique used */
      Rock[ipnt++] = 'z';
      Rock[ipnt++] = 3;
      set_733((char*)Rock + ipnt, file_size); /* Real file size */
      ipnt += 8;
    };
  }
#endif
  /* 
   * Add in the Rock Ridge CE field, if required.  We use  this for the
   * extension record that is stored in the root directory.
   */
  if(deep_opt & NEED_CE) add_CE_entry();
  /*
   * Done filling in all of the fields.  Now copy it back to a buffer for the
   * file in question.
   */

  /* Now copy this back to the buffer for the file */
  Rock[flagpos] = flagval;

  /* If there was a CE, fill in the size field */
  if(recstart)
    set_733((char*)Rock + recstart - 8, ipnt - recstart);

  s_entry->rr_attributes = (unsigned char *) e_malloc(ipnt);
  s_entry->total_rr_attr_size = ipnt;
  s_entry->rr_attr_size = (mainrec ? mainrec : ipnt);
  memcpy(s_entry->rr_attributes, Rock, ipnt);
  return ipnt;
}

/* Guaranteed to  return a single sector with the relevant info */

char * FDECL4(generate_rr_extension_record, char *, id,  char  *, descriptor,
				    char *, source, int  *, size){
  int lipnt = 0;
  char * pnt;
  int len_id, len_des, len_src;

  len_id = strlen(id);
  len_des =  strlen(descriptor);
  len_src = strlen(source);
  Rock[lipnt++] ='E';
  Rock[lipnt++] ='R';
  Rock[lipnt++] = ER_SIZE + len_id + len_des + len_src;
  Rock[lipnt++] = 1;
  Rock[lipnt++] = len_id;
  Rock[lipnt++] = len_des;
  Rock[lipnt++] = len_src;
  Rock[lipnt++] = 1;

  memcpy(Rock  + lipnt, id, len_id);
  lipnt += len_id;

  memcpy(Rock  + lipnt, descriptor, len_des);
  lipnt += len_des;

  memcpy(Rock  + lipnt, source, len_src);
  lipnt += len_src;

  if(lipnt  > SECTOR_SIZE) {
	  fprintf(stderr,"Extension record too  long\n");
	  exit(1);
  };
  pnt = (char *) e_malloc(SECTOR_SIZE);
  memset(pnt, 0,  SECTOR_SIZE);
  memcpy(pnt, Rock, lipnt);
  *size = lipnt;
  return pnt;
}
