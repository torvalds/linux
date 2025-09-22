/*
 * Program mkisofs.c - generate iso9660 filesystem  based upon directory
 * tree on hard disk.

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

/* APPLE_HYB James Pearson j.pearson@ge.ucl.ac.uk 12/3/99 */

#include <errno.h>
#include "config.h"
#include "mkisofs.h"
#include "match.h"
#include "apple_proto.h"

#ifdef linux
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include "iso9660.h"
#include <ctype.h>

#ifndef VMS
#include <time.h>
#else
#include <sys/time.h>
#include "vms.h"
#endif

#include <stdlib.h>
#include <sys/stat.h>

#ifndef VMS
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif
#include <fctldefs.h>

#if defined(__NetBSD__) || defined(__OpenBSD__) 
#include <sys/time.h>
#include <sys/resource.h>
#endif

struct directory * root = NULL;

#ifdef APPLE_HYB
static char version_string[] = "mkhybrid 1.12b5.1";
#else
static char version_string[] = "mkisofs 1.12b5";
#endif /* APPLE_HYB */

char * outfile;
FILE * discimage;
unsigned int next_extent = 0;
unsigned int last_extent = 0;
unsigned int session_start = 0;
unsigned int path_table_size = 0;
unsigned int path_table[4] = {0,};
unsigned int path_blocks = 0;


unsigned int jpath_table_size = 0;
unsigned int jpath_table[4] = {0,};
unsigned int jpath_blocks = 0;

struct iso_directory_record root_record;
struct iso_directory_record jroot_record;

char * extension_record = NULL;
int extension_record_extent = 0;
int extension_record_size = 0;

/* These variables are associated with command line options */
int use_eltorito = 0;
int use_RockRidge = 0;
int use_Joliet = 0;
int verbose = 0;
int all_files  = 0;
int follow_links = 0;
int rationalize = 0;
int generate_tables = 0;
int print_size = 0;
int split_output = 0;
char * preparer = PREPARER_DEFAULT;
char * publisher = PUBLISHER_DEFAULT;
char * appid = APPID_DEFAULT;
char * copyright = COPYRIGHT_DEFAULT;
char * biblio = BIBLIO_DEFAULT;
char * abstract = ABSTRACT_DEFAULT;
char * volset_id = VOLSET_ID_DEFAULT;
char * volume_id = VOLUME_ID_DEFAULT;
char * system_id = SYSTEM_ID_DEFAULT;
char * boot_catalog = BOOT_CATALOG_DEFAULT;
char * boot_image = BOOT_IMAGE_DEFAULT;
char * efi_boot_image = EFI_BOOT_IMAGE_DEFAULT;
int volume_set_size = 1;
int volume_sequence_number = 1;

int omit_period = 0;             /* Violates iso9660, but these are a pain */
int transparent_compression = 0; /* So far only works with linux */
int omit_version_number = 0;     /* May violate iso9660, but noone uses vers*/
int RR_relocation_depth = 6;     /* Violates iso9660, but most systems work */
int full_iso9660_filenames = 0;  /* Used with Amiga.  Disc will not work with
				  DOS */
int allow_leading_dots = 0;	 /* DOS cannot read names with leading dots */
int split_SL_component = 1;    /* circumvent a bug in the SunOS driver */
int split_SL_field = 1;                /* circumvent a bug in the SunOS */

#ifdef APPLE_HYB
int	apple_hyb = 0;		/* create HFS hybrid flag */
int	apple_ext = 0;		/* create HFS extensions flag */
int	apple_both = 0;		/* common flag (for above) */
int	hfs_extra = 0;		/* extra HFS blocks added to end of ISO vol */
int	mac_name = 0;		/* use Mac name for ISO/Joliet/RR flag */
hce_mem *hce;			/* libhfs/mkisofs extras */
char	*hfs_boot_file = 0;	/* name of HFS boot file */
int	gen_pt = 0;		/* generate HFS partition table */
char	*autoname = 0;		/* AutoStart filename */
char	*magic_file = 0;	/* name of magic file */
int	probe = 0;		/* search files for HFS/Unix type */
int	nomacfiles = 0;		/* don't look for Mac/Unix files */
int	hfs_select = 0;		/* Mac/Unix types to select */
int	create_dt = 1;		/* create the Desktp files */
int	bsize = 0;		/* Apple File Exchange block size */
int	hfs_last = MAG_LAST;	/* process magic file after map file */
char	*deftype = DEFTYPE;	/* default Apple TYPE */
char	*defcreator = DEFCREATOR; /* default Apple CREATOR */
char	*trans_tbl = "TRANS.TBL"; /* default name for translation table */
char	*hfs_volume_id = NULL;	/* HFS volume ID */
char	*hfs_bless = NULL;	/* name of folder to 'bless' (System Folder) */

#endif /* APPLE_HYB */

struct rcopts{
  char * tag;
  char ** variable;
};

struct rcopts rcopt[] = {
  {"PREP", &preparer},
  {"PUBL", &publisher},
  {"APPI", &appid},
  {"COPY", &copyright},
  {"BIBL", &biblio},
  {"ABST", &abstract},
  {"VOLS", &volset_id},
  {"VOLI", &volume_id},
  {"SYSI", &system_id},
#ifdef APPLE_HYB
  {"TYPE", &deftype},
  {"CREATOR", &defcreator},
#endif /* APPLE_HYB */
  {NULL, NULL}
};

/*
 * In case it isn't obvious, the option handling code was ripped off from GNU-ld.
 */
struct ld_option
{
  /* The long option information.  */
  struct option opt;
  /* The short option with the same meaning ('\0' if none).  */
  char shortopt;
  /* The name of the argument (NULL if none).  */
  const char *arg;
  /* The documentation string.  If this is NULL, this is a synonym for
     the previous option.  */
  const char *doc;
  enum
    {
      /* Use one dash before long option name.  */
      ONE_DASH,
      /* Use two dashes before long option name.  */
      TWO_DASHES,
      /* Don't mention this option in --help output.  */
      NO_HELP
    } control;
};

/* Codes used for the long options with no short synonyms.  150 isn't
   special; it's just an arbitrary non-ASCII char value.  */
#define OPTION_HELP			150
#define OPTION_QUIET			151
#define OPTION_NOSPLIT_SL_COMPONENT	152
#define OPTION_NOSPLIT_SL_FIELD		153
#define OPTION_PRINT_SIZE		154
#define OPTION_SPLIT_OUTPUT		155
#define OPTION_ABSTRACT			156
#define OPTION_BIBLIO			157
#define OPTION_COPYRIGHT		158
#define OPTION_SYSID			159
#define OPTION_VOLSET			160
#define OPTION_VOLSET_SIZE		161
#define OPTION_VOLSET_SEQ_NUM		162
#define OPTION_I_HIDE			163
#define OPTION_J_HIDE			164
#define OPTION_LOG_FILE			165
#ifdef APPLE_HYB
#define OPTION_CAP			200
#define OPTION_NETA			201
#define OPTION_DBL			202
#define OPTION_ESH			203
#define OPTION_FE			204
#define OPTION_SGI			205
#define OPTION_MBIN			206
#define OPTION_SGL			207
/* aliases */
#define OPTION_USH			208
#define OPTION_XIN			209

#define OPTION_PROBE			220
#define OPTION_MACNAME			221
#define OPTION_NOMACFILES		222
#define OPTION_BOOT_HFS_FILE		223
#define OPTION_MAGIC_FILE		224

#define OPTION_TRANS_TBL		225

#define OPTION_GEN_PT			226

#define OPTION_CREATE_DT		227
#define OPTION_HFS_HIDE			228

#define OPTION_AUTOSTART		229
#define OPTION_BSIZE			230
#define OPTION_HFS_VOLID		231

#define OPTION_H_LIST			240
#define OPTION_P_LIST			241
#define OPTION_I_LIST			242
#define OPTION_J_LIST			243
#define OPTION_X_LIST			244

#define OPTION_HFS_BLESS		245
#endif /* APPLE_HYB */

static const struct ld_option ld_options[] =
{
  { {"all-files", no_argument, NULL, 'a'},
      'a', NULL, "Process all files (don't skip backup files)", ONE_DASH },
  { {"abstract", required_argument, NULL, OPTION_ABSTRACT},
      '\0', "FILE", "Set Abstract filename" , ONE_DASH },
  { {"appid", required_argument, NULL, 'A'},
      'A', "ID", "Set Application ID" , ONE_DASH },
  { {"biblio", required_argument, NULL, OPTION_BIBLIO},
      '\0', "FILE", "Set Bibliographic filename" , ONE_DASH },
  { {"copyright", required_argument, NULL, OPTION_COPYRIGHT},
      '\0', "FILE", "Set Copyright filename" , ONE_DASH },
  { {"eltorito-boot", required_argument, NULL, 'b'},
      'b', "FILE", "Set El Torito boot image name" , ONE_DASH },
  { {"eltorito-catalog", required_argument, NULL, 'c'},
      'c', "FILE", "Set El Torito boot catalog name" , ONE_DASH },
  { {"cdwrite-params", required_argument, NULL, 'C'},
      'C', "PARAMS", "Magic paramters from cdrecord" , ONE_DASH },
  { {"omit-period", no_argument, NULL, 'd'},
      'd', NULL, "Omit trailing periods from filenames", ONE_DASH },
  { {"disable-deep-relocation", no_argument, NULL, 'D'},
      'D', NULL, "Disable deep directory relocation", ONE_DASH },
  { {"eltorito-boot-efi", required_argument, NULL, 'e' },
      'e', "FILE", "Set El Torito EFI boot image name" , ONE_DASH },
  { {"follow-links", no_argument, NULL, 'f'},
      'f', NULL, "Follow symbolic links", ONE_DASH },
  { {"help", no_argument, NULL, OPTION_HELP},
      '\0', NULL, "Print option help", ONE_DASH },
  { {"hide", required_argument, NULL, OPTION_I_HIDE},
      '\0', "GLOBFILE", "Hide ISO9660/RR file" , ONE_DASH },
#ifdef APPLE_HYB
  /* NON-HFS change */
  { {"hide-list", required_argument, NULL, OPTION_I_LIST},
      '\0', "FILE", "list of ISO9660/RR files to hide" , ONE_DASH },
#endif /* APPLE_HYB */
  { {"hide-joliet", required_argument, NULL, OPTION_J_HIDE},
      '\0', "GLOBFILE", "Hide Joliet file" , ONE_DASH },
#ifdef APPLE_HYB
  /* NON-HFS change */
  { {"hide-joliet-list", required_argument, NULL, OPTION_J_LIST},
      '\0', "FILE", "List of Joliet files to hide" , ONE_DASH },
#endif /* APPLE_HYB */
  { {NULL, required_argument, NULL, 'i'},
      'i', "ADD_FILES", "No longer supported" , TWO_DASHES },
  { {"joliet", no_argument, NULL, 'J'},
      'J', NULL, "Generate Joliet directory information", ONE_DASH },
  { {"full-iso9660-filenames", no_argument, NULL, 'l'},
      'l', NULL, "Allow full 32 character filenames for iso9660 names", ONE_DASH },
  { {"allow-leading-dots", no_argument, NULL, 'L'},
      'L', NULL, "Allow iso9660 filenames to start with '.'", ONE_DASH },
  { {"log-file", required_argument, NULL, OPTION_LOG_FILE},
      '\0', "LOG_FILE", "Re-direct messages to LOG_FILE", ONE_DASH },
  { {"exclude", required_argument, NULL, 'm'},
      'm', "GLOBFILE", "Exclude file name" , ONE_DASH },
#ifdef APPLE_HYB
  { {"exclude-list", required_argument, NULL, OPTION_X_LIST},
      'm', "FILE", "List of file names to exclude" , ONE_DASH },
#endif /* APPLE_HYB */
  { {"prev-session", required_argument, NULL, 'M'},
      'M', "FILE", "Set path to previous session to merge" , ONE_DASH },
  { {"omit-version-number", no_argument, NULL, 'N'},
      'N', NULL, "Omit version number from iso9660 filename", ONE_DASH },
  { {"no-split-symlink-components", no_argument, NULL, 0},
      0, NULL, "Inhibit splitting symlink components" , ONE_DASH },
  { {"no-split-symlink-fields", no_argument, NULL, 0},
      0, NULL, "Inhibit splitting symlink fields" , ONE_DASH },
  { {"output", required_argument, NULL, 'o'},
      'o', "FILE", "Set output file name" , ONE_DASH },
#ifdef APPLE_HYB
  { {"path-list", required_argument, NULL, OPTION_P_LIST},
      '\0', "FILE", "list of pathnames to process" , ONE_DASH },
#endif /* APPLE_HYB */
  { {"preparer", required_argument, NULL, 'p'},
      'p', "PREP", "Set Volume preparer" , ONE_DASH },
  { {"print-size", no_argument, NULL, OPTION_PRINT_SIZE},
      '\0', NULL, "Print estimated filesystem size and exit", ONE_DASH },
  { {"publisher", required_argument, NULL, 'P'},
      'P', "PUB", "Set Volume publisher" , ONE_DASH },
  { {"quiet", no_argument, NULL, OPTION_QUIET},
      '\0', NULL, "Run quietly", ONE_DASH },
  { {"rational-rock", no_argument, NULL, 'r'},
      'r', NULL, "Generate rationalized Rock Ridge directory information", ONE_DASH },
  { {"rock", no_argument, NULL, 'R'},
      'R', NULL, "Generate Rock Ridge directory information", ONE_DASH },
  { {"split-output", no_argument, NULL, OPTION_SPLIT_OUTPUT},
      '\0', NULL, "Split output into files of approx. 1GB size", ONE_DASH },
  { {"sysid", required_argument, NULL, OPTION_SYSID},
      '\0', "ID", "Set System ID" , ONE_DASH },
  { {"translation-table", no_argument, NULL, 'T'},
      'T', NULL, "Generate translation tables for systems that don't understand long filenames", ONE_DASH },
  { {"verbose", no_argument, NULL, 'v'},
      'v', NULL, "Verbose", ONE_DASH },
  { {"volid", required_argument, NULL, 'V'},
      'V', "ID", "Set Volume ID" , ONE_DASH },
  { {"volset", required_argument, NULL, OPTION_VOLSET},
      '\0', "ID", "Set Volume set ID" , ONE_DASH },
  { {"volset-size", required_argument, NULL, OPTION_VOLSET_SIZE},
      '\0', "#", "Set Volume set size" , ONE_DASH },
  { {"volset-seqno", required_argument, NULL, OPTION_VOLSET_SEQ_NUM},
      '\0', "#", "Set Volume set sequence number" , ONE_DASH },
  { {"old-exclude", required_argument, NULL, 'x'},
      'x', "FILE", "Exclude file name(depreciated)" , ONE_DASH },
#ifdef ERIC_neverdef
  { {"transparent-compression", no_argument, NULL, 'z'},
      'z', NULL, "Enable transparent compression of files", ONE_DASH },
#endif
#ifdef APPLE_HYB
  { {"apple", no_argument, NULL, 'g'},
      'g', NULL, "Add Apple ISO9660 extensions", ONE_DASH },
  { {"hfs", no_argument, NULL, 'h'},
      'h', NULL, "Create ISO9660/HFS hybrid", ONE_DASH },
  { {"map", required_argument, NULL, 'H'},
      'H', "MAPPING_FILE", "Map file extensions to HFS TYPE/CREATOR", ONE_DASH},
  { {"magic", required_argument, NULL, OPTION_MAGIC_FILE},
      '\0', "FILE", "Magic file for HFS TYPE/CREATOR", ONE_DASH},
  { {"probe", no_argument, NULL, OPTION_PROBE},
      '\0', NULL, "Probe all files for Unix/HFS file type", ONE_DASH },
  { {"mac-name", no_argument, NULL, OPTION_MACNAME},
      '\0', NULL, "Use Macintosh name for ISO9660/Joliet/RockRidge file name",
	ONE_DASH },
  { {"no-mac-files", no_argument, NULL, OPTION_NOMACFILES},
      '\0', NULL, "Do not look for Unix/Mac files", ONE_DASH },
  { {"boot-hfs-file", required_argument, NULL, OPTION_BOOT_HFS_FILE},
      '\0', "FILE", "Set HFS boot image name", ONE_DASH},
  { {"part", no_argument, NULL, OPTION_GEN_PT},
      '\0', NULL, "Generate HFS partition table", ONE_DASH },
  { {"cluster-size", required_argument, NULL, OPTION_BSIZE},
      '\0', "SIZE", "Cluster size for PC Exchange Macintosh files", ONE_DASH},
  { {"auto", required_argument, NULL, OPTION_AUTOSTART},
      '\0', "FILE", "Set HFS AutoStart file name", ONE_DASH},
  { {"no-desktop", no_argument, NULL, OPTION_CREATE_DT},
      '\0', NULL, "Do not create the HFS (empty) Desktop files", ONE_DASH },
  { {"hide-hfs", required_argument, NULL, OPTION_HFS_HIDE},
      '\0', "GLOBFILE", "Hide HFS file" , ONE_DASH },
  { {"hide-hfs-list", required_argument, NULL, OPTION_H_LIST},
      '\0', "GLOBFILE", "List of HFS files to hide" , ONE_DASH },
  { {"table-name", required_argument, NULL, OPTION_TRANS_TBL},
      '\0', "TABLE_NAME", "translation table file name", ONE_DASH },
  { {"hfs-volid", required_argument, NULL, OPTION_HFS_VOLID},
      '\0', "HFS_VOLID", "Volume name for the HFS partition", ONE_DASH },
  {{"hfs-bless", required_argument, NULL, OPTION_HFS_BLESS},
      '\0', "FOLDER_NAME", "Name of Folder to be blessed", ONE_DASH},
  { {"cap", no_argument, NULL, OPTION_CAP},
      '\0', NULL, "Look for AUFS CAP Macintosh files", TWO_DASHES },
  { {"netatalk", no_argument, NULL, OPTION_NETA},
      '\0', NULL, "Look for NETATALK Macintosh files", TWO_DASHES },
  { {"double", no_argument, NULL, OPTION_DBL},
      '\0', NULL, "Look for AppleDouble Macintosh files", TWO_DASHES },
  { {"ethershare", no_argument, NULL, OPTION_ESH},
      '\0', NULL, "Look for Helios EtherShare Macintosh files", TWO_DASHES },
  { {"exchange", no_argument, NULL, OPTION_FE},
      '\0', NULL, "Look for PC Exchange Macintosh files", TWO_DASHES },
  { {"sgi", no_argument, NULL, OPTION_SGI},
      '\0', NULL, "Look for SGI Macintosh files", TWO_DASHES },
  { {"macbin", no_argument, NULL, OPTION_MBIN},
      '\0', NULL, "Look for MacBinary Macintosh files", TWO_DASHES },
  { {"single", no_argument, NULL, OPTION_SGL},
      '\0', NULL, "Look for AppleSingle Macintosh files", TWO_DASHES },
  { {"ushare", no_argument, NULL, OPTION_USH},
      '\0', NULL, "Look for IPT UShare Macintosh files", TWO_DASHES },
  { {"xinet", no_argument, NULL, OPTION_XIN},
      '\0', NULL, "Look for XINET Macintosh files", TWO_DASHES },
#endif /* APPLE_HYB */
};

#define OPTION_COUNT (sizeof ld_options / sizeof ld_options[0])

#if defined(ultrix) || defined(_AUX_SOURCE)
char *strdup(s)
char *s;{char *c;if(c=(char *)malloc(strlen(s)+1))strcpy(c,s);return c;}
#endif

	void read_rcfile	__PR((char * appname));
	void usage		__PR((void));
static	void hide_reloc_dir	__PR((void));

void FDECL1(read_rcfile, char *, appname)
{
  FILE * rcfile;
  struct rcopts * rco;
  char * pnt, *pnt1;
  char linebuffer[256];
  static char rcfn[] = ".mkisofsrc";
  char filename[1000];
  int linum;

  strcpy(filename, rcfn);
  rcfile = fopen(filename, "r");
  if (!rcfile && errno != ENOENT)
    perror(filename);

  if (!rcfile)
    {
      pnt = getenv("MKISOFSRC");
      if (pnt && strlen(pnt) <= sizeof(filename))
	{
	  strcpy(filename, pnt);
	  rcfile = fopen(filename, "r");
	  if (!rcfile && errno != ENOENT)
	    perror(filename);
	}
    }

  if (!rcfile)
    {
      pnt = getenv("HOME");
      if (pnt && strlen(pnt) + strlen(rcfn) + 2 <= sizeof(filename))
	{
	  strcpy(filename, pnt);
	  strcat(filename, "/");
	  strcat(filename, rcfn);
	  rcfile = fopen(filename, "r");
	  if (!rcfile && errno != ENOENT)
	    perror(filename);
	}
    }
  if (!rcfile && strlen(appname)+sizeof(rcfn)+2 <= sizeof(filename))
    {
      strcpy(filename, appname);
      pnt = strrchr(filename, '/');
      if (pnt)
	{
	  strcpy(pnt + 1, rcfn);
	  rcfile = fopen(filename, "r");
	  if (!rcfile && errno != ENOENT)
	    perror(filename);
	}
    }
  if (!rcfile)
    return;
  if ( verbose > 0 )
    {
      fprintf(stderr, "Using \"%s\"\n", filename);
    }

  /* OK, we got it.  Now read in the lines and parse them */
  linum = 0;
  while (fgets(linebuffer, sizeof(linebuffer), rcfile))
    {
      char *name;
      char *name_end;
      ++linum;
      /* skip any leading white space */
	pnt = linebuffer;
      while (*pnt == ' ' || *pnt == '\t')
	++pnt;
      /* If we are looking at a # character, this line is a comment. */
	if (*pnt == '#')
	  continue;
      /* The name should begin in the left margin.  Make sure it is in
	 upper case.  Stop when we see white space or a comment. */
	name = pnt;
      while (*pnt && isalpha((unsigned char)*pnt))
	{
	  if(islower((unsigned char)*pnt))
	    *pnt = toupper((unsigned char)*pnt);
	  pnt++;
	}
      if (name == pnt)
	{
	  fprintf(stderr, "%s:%d: name required\n", filename, linum);
	  continue;
	}
      name_end = pnt;
      /* Skip past white space after the name */
      while (*pnt == ' ' || *pnt == '\t')
	pnt++;
      /* silently ignore errors in the rc file. */
      if (*pnt != '=')
	{
	  fprintf(stderr, "%s:%d: equals sign required\n", filename, linum);
	  continue;
	}
      /* Skip pas the = sign, and any white space following it */
      pnt++; /* Skip past '=' sign */
      while (*pnt == ' ' || *pnt == '\t')
	pnt++;

      /* now it is safe to NUL terminate the name */

	*name_end = 0;

      /* Now get rid of trailing newline */

      pnt1 = pnt;
      while (*pnt1)
	{
	  if (*pnt1 == '\n')
	    {
	      *pnt1 = 0;
	      break;
	    }
	  pnt1++;
	};
      /* OK, now figure out which option we have */
      for(rco = rcopt; rco->tag; rco++) {
	if(strcmp(rco->tag, name) == 0)
	  {
	    *rco->variable = strdup(pnt);
	    break;
	  };
      }
      if (rco->tag == NULL)
	{
	  fprintf(stderr, "%s:%d: field name \"%s\" unknown\n", filename, linum,
		  name);
	}
     }
  if (ferror(rcfile))
    perror(filename);
  fclose(rcfile);
}

char * path_table_l = NULL;
char * path_table_m = NULL;

char * jpath_table_l = NULL;
char * jpath_table_m = NULL;

int goof = 0;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

void usage(){
#ifdef APPLE_HYB
  const char * program_name = "mkhybrid";
#else
  const char * program_name = "mkisofs";
#endif /* APPLE_HYB */

#if 0
	fprintf(stderr,"Usage:\n");
	fprintf(stderr,
"mkisofs [-o outfile] [-R] [-V volid] [-v] [-a] \
[-T]\n [-l] [-d] [-V] [-D] [-L] [-p preparer]"
"[-P publisher] [ -A app_id ] [-z] \n \
[-b boot_image_name] [-c boot_catalog-name] \
[-x path -x path ...] path\n");
#endif

  int i;
/*  const char **targets, **pp;*/

  fprintf (stderr, "Usage: %s [options] file...\n", program_name);

  fprintf (stderr, "Options:\n");
  for (i = 0; i < OPTION_COUNT; i++)
    {
      if (ld_options[i].doc != NULL)
	{
	  int comma;
	  int len;
	  int j;

	  fprintf (stderr, "  ");

	  comma = FALSE;
	  len = 2;

	  j = i;
	  do
	    {
	      if (ld_options[j].shortopt != '\0'
		  && ld_options[j].control != NO_HELP)
		{
		  fprintf (stderr, "%s-%c", comma ? ", " : "", ld_options[j].shortopt);
		  len += (comma ? 2 : 0) + 2;
		  if (ld_options[j].arg != NULL)
		    {
		      if (ld_options[j].opt.has_arg != optional_argument)
			{
			  fprintf (stderr, " ");
			  ++len;
			}
		      fprintf (stderr, "%s", ld_options[j].arg);
		      len += strlen (ld_options[j].arg);
		    }
		  comma = TRUE;
		}
	      ++j;
	    }
	  while (j < OPTION_COUNT && ld_options[j].doc == NULL);

	  j = i;
	  do
	    {
	      if (ld_options[j].opt.name != NULL
		  && ld_options[j].control != NO_HELP)
		{
		  fprintf (stderr, "%s-%s%s",
			  comma ? ", " : "",
			  ld_options[j].control == TWO_DASHES ? "-" : "",
			  ld_options[j].opt.name);
		  len += ((comma ? 2 : 0)
			  + 1
			  + (ld_options[j].control == TWO_DASHES ? 1 : 0)
			  + strlen (ld_options[j].opt.name));
		  if (ld_options[j].arg != NULL)
		    {
		      fprintf (stderr, " %s", ld_options[j].arg);
		      len += 1 + strlen (ld_options[j].arg);
		    }
		  comma = TRUE;
		}
	      ++j;
	    }
	  while (j < OPTION_COUNT && ld_options[j].doc == NULL);

	  if (len >= 30)
	    {
	      fprintf (stderr, "\n");
	      len = 0;
	    }

	  for (; len < 30; len++)
	    fputc (' ', stderr);

	  fprintf (stderr, "%s\n", ld_options[i].doc);
	}
    }
  exit(1);
}


/* 
 * Fill in date in the iso9660 format 
 *
 * The standards  state that the timezone offset is in multiples of 15
 * minutes, and is what you add to GMT to get the localtime.  The U.S.
 * is always at a negative offset, from -5h to -8h (can vary a little
 * with DST,  I guess).  The Linux iso9660 filesystem has had the sign
 * of this wrong for ages (mkisofs had it wrong too for the longest time).
 */
int FDECL2(iso9660_date,char *, result, time_t, crtime){
  struct tm *local;
  local = localtime(&crtime);
  result[0] = local->tm_year;
  result[1] = local->tm_mon + 1;
  result[2] = local->tm_mday;
  result[3] = local->tm_hour;
  result[4] = local->tm_min;
  result[5] = local->tm_sec;

  /* 
   * Must recalculate proper timezone offset each time,
   * as some files use daylight savings time and some don't... 
   */
  result[6] = local->tm_yday;	/* save yday 'cause gmtime zaps it */
  local = gmtime(&crtime);
  local->tm_year -= result[0];
  local->tm_yday -= result[6];
  local->tm_hour -= result[3];
  local->tm_min -= result[4];
  if (local->tm_year < 0) 
    {
      local->tm_yday = -1;
    }
  else 
    {
      if (local->tm_year > 0) local->tm_yday = 1;
    }

  result[6] = -(local->tm_min + 60*(local->tm_hour + 24*local->tm_yday)) / 15;

  return 0;
}

/* hide "./rr_moved" if all its contents are hidden */
static void
hide_reloc_dir()
{
	struct directory_entry * s_entry;

	for (s_entry = reloc_dir->contents; s_entry; s_entry = s_entry->next) {
	    if(strcmp(s_entry->name,".")==0 || strcmp(s_entry->name,"..")==0)
		continue;

	    if((s_entry->de_flags & INHIBIT_ISO9660_ENTRY) == 0)
		return;
	}

	/* all entries are hidden, so hide this directory */
	reloc_dir->dir_flags |= INHIBIT_ISO9660_ENTRY;
	reloc_dir->self->de_flags |= INHIBIT_ISO9660_ENTRY;
}

/* get pathnames from the command line, and then from given file */
static char *
FDECL5(get_pnames, int, argc, char **, argv, int, opt, char *, pname, FILE *, fp)
{
	if (opt < argc)
	    return (argv[opt]);

	if (fp == NULL)
	    return ((char *)0);

	if (fscanf(fp, "%s", pname) != EOF)
	    return (pname);

	return ((char *)0);
}

extern char * cdwrite_data;

int FDECL2(main, int, argc, char **, argv){
  struct directory_entry de;
#ifdef HAVE_SBRK
  unsigned long mem_start;
#endif
  struct stat statbuf;
  char * scan_tree;
  char * merge_image = NULL;
  struct iso_directory_record * mrootp = NULL;
  struct output_fragment * opnt;
  int longind;
  char shortopts[OPTION_COUNT * 3 + 2];
  struct option longopts[OPTION_COUNT + 1];
  int c;
  char *log_file = 0;
#ifdef APPLE_HYB
  char *afpfile = "";		/* mapping file for TYPE/CREATOR */
  char *pathnames = 0;
  FILE *pfp = NULL;
  char pname[1024], *arg;
  int no_path_names = 0;
#endif /* APPLE_HYB */

  if (argc < 2)
    usage();

  /* Get the defaults from the .mkisofsrc file */
  read_rcfile(argv[0]);

  outfile = NULL;

  /*
   * Copy long option initialization from GNU-ld.
   */
  /* Starting the short option string with '-' is for programs that
     expect options and other ARGV-elements in any order and that care about
     the ordering of the two.  We describe each non-option ARGV-element
     as if it were the argument of an option with character code 1.  */
  {
    int i, is, il;
    shortopts[0] = '-';
    is = 1;
    il = 0;
    for (i = 0; i < OPTION_COUNT; i++)
      {
	if (ld_options[i].shortopt != '\0')
	  {
	    shortopts[is] = ld_options[i].shortopt;
	    ++is;
	    if (ld_options[i].opt.has_arg == required_argument
		|| ld_options[i].opt.has_arg == optional_argument)
	      {
		shortopts[is] = ':';
		++is;
		if (ld_options[i].opt.has_arg == optional_argument)
		  {
		    shortopts[is] = ':';
		    ++is;
		  }
	      }
	  }
	if (ld_options[i].opt.name != NULL)
	  {
	    longopts[il] = ld_options[i].opt;
	    ++il;
	  }
      }
    shortopts[is] = '\0';
    longopts[il].name = NULL;
  }

  while ((c = getopt_long_only (argc, argv, shortopts, longopts, &longind)) != EOF)
    switch (c)
      {
      case 1:
	/*
	 * A filename that we take as input.
	 */
	optind--;
	goto parse_input_files;
      case 'C':
	/*
	 * This is a temporary hack until cdwrite gets the proper hooks in
	 * it.
	 */
	cdwrite_data = optarg;
	break;
      case 'i':
	fprintf(stderr, "-i option no longer supported.\n");
	exit(1);
	break;
      case 'J':
	use_Joliet++;
	break;
      case 'a':
	all_files++;
	break;
      case 'b':
	use_eltorito++;
	boot_image = optarg;  /* pathname of the boot image on cd */
	if (boot_image == NULL) {
	        fprintf(stderr,"Required boot image pathname missing\n");
		exit(1);
	}
	break;
      case 'e':
	use_eltorito++;
	efi_boot_image = optarg;
	if (efi_boot_image == NULL) {
		fprintf(stderr,"Required EFI boot image pathname missing\n");
		exit(1);
	}
	break;
      case 'c':
	use_eltorito++;
	boot_catalog = optarg;  /* pathname of the boot image on cd */
	if (boot_catalog == NULL) {
	        fprintf(stderr,"Required boot catalog pathname missing\n");
		exit(1);
	}
	break;
      case OPTION_ABSTRACT:
	abstract = optarg;
	if(strlen(abstract) > 37) {
		fprintf(stderr,"Abstract filename string too long\n");
		exit(1);
	};
	break;
      case 'A':
	appid = optarg;
	if(strlen(appid) > 128) {
		fprintf(stderr,"Application-id string too long\n");
		exit(1);
	};
	break;
      case OPTION_BIBLIO:
	biblio = optarg;
	if(strlen(biblio) > 37) {
		fprintf(stderr,"Bibliographic filename string too long\n");
		exit(1);
	};
	break;
      case OPTION_COPYRIGHT:
	copyright = optarg;
	if(strlen(copyright) > 37) {
		fprintf(stderr,"Copyright filename string too long\n");
		exit(1);
	};
	break;
      case 'd':
	omit_period++;
	break;
      case 'D':
	RR_relocation_depth = 32767;
	break;
      case 'f':
	follow_links++;
	break;
      case 'l':
	full_iso9660_filenames++;
	break;
      case 'L':
        allow_leading_dots++;
        break;
     case OPTION_LOG_FILE:
	log_file = optarg;
	break;
      case 'M':
	merge_image = optarg;
	break;
      case 'N':
	omit_version_number++;
	break;
      case 'o':
	outfile = optarg;
	break;
      case 'p':
	preparer = optarg;
	if(strlen(preparer) > 128) {
		fprintf(stderr,"Preparer string too long\n");
		exit(1);
	};
	break;
      case OPTION_PRINT_SIZE:
	print_size++;
	break;
      case 'P':
	publisher = optarg;
	if(strlen(publisher) > 128) {
		fprintf(stderr,"Publisher string too long\n");
		exit(1);
	};
	break;
      case OPTION_QUIET:
	verbose = 0;
	break;
      case 'R':
	use_RockRidge++;
	break;
      case 'r':
	rationalize++;
	use_RockRidge++;
	break;
      case OPTION_SPLIT_OUTPUT:
	split_output++;
	break;
      case OPTION_SYSID:
	system_id = optarg;
	if(strlen(system_id) > 32) {
		fprintf(stderr,"System ID string too long\n");
		exit(1);
	};
	break;
#ifdef APPLE_HYB
      case OPTION_TRANS_TBL:
	trans_tbl = optarg;
	/* fall through */
#endif /* APPLE_HYB */
      case 'T':
	generate_tables++;
	break;
      case 'V':
	volume_id = optarg;
	if(strlen(volume_id) > 32) {
		fprintf(stderr,"Volume ID string too long\n");
		exit(1);
	};
	break;
      case OPTION_VOLSET:
	volset_id = optarg;
	if(strlen(volset_id) > 128) {
		fprintf(stderr,"Volume set ID string too long\n");
		exit(1);
	};
	break;
      case OPTION_VOLSET_SIZE:
	volume_set_size = atoi(optarg);
	break;
      case OPTION_VOLSET_SEQ_NUM:
	volume_sequence_number = atoi(optarg);
	if (volume_sequence_number > volume_set_size) {
		fprintf(stderr,"Volume set sequence number too big\n");
		exit(1);
	}
	break;
      case 'v':
	verbose++;
	break;
      case 'z':
#ifdef VMS
	fprintf(stderr,"Transparent compression not supported with VMS\n");
	exit(1);
#else
	transparent_compression++;
#endif
	break;
      case 'x':
      case 'm':
	/*
	 * Somehow two options to do basically the same thing got added somewhere along
	 * the way.  The 'match' code supports limited globbing, so this is the one
	 * that got selected.  Unfortunately the 'x' switch is probably more intuitive.
	 */
        add_match(optarg);
	break;
      case OPTION_I_HIDE:
	i_add_match(optarg);
	break;
      case OPTION_J_HIDE:
	j_add_match(optarg);
	break;
      case OPTION_HELP:
	usage ();
	exit (0);
	break;
      case OPTION_NOSPLIT_SL_COMPONENT:
	split_SL_component = 0;
	break;
      case OPTION_NOSPLIT_SL_FIELD:
	split_SL_field = 0;
	break;
#ifdef APPLE_HYB
      case 'H':
	afpfile = optarg;
	hfs_last = MAP_LAST;
	break;
      case 'h':
	apple_hyb = 1;
	break;
      case 'g':
	apple_ext = 1;
	break;
      case OPTION_PROBE:
	probe = 1;
	break;
      case OPTION_MACNAME:
	mac_name = 1;
	break;
      case OPTION_NOMACFILES:
	nomacfiles = 1;
	break;
      case OPTION_BOOT_HFS_FILE:
	hfs_boot_file = optarg;
	/* fall through */
      case OPTION_GEN_PT:
	gen_pt = 1;
	break;
      case OPTION_MAGIC_FILE:
	magic_file = optarg;
	hfs_last = MAG_LAST;
	break;
      case OPTION_AUTOSTART:
	autoname = optarg;
	/* gen_pt = 1; */
	break;
      case OPTION_BSIZE:
	bsize = atoi(optarg);
	break;
      case OPTION_HFS_VOLID:
	hfs_volume_id = optarg;
	break;
      case OPTION_HFS_BLESS:
	hfs_bless = optarg;
	break;
      /* Mac/Unix types to include */
      case OPTION_CAP:
	hfs_select |= DO_CAP;
	break;
      case OPTION_NETA:
	hfs_select |= DO_NETA;
	break;
      case OPTION_DBL:
	hfs_select |= DO_DBL;
	break;
      case OPTION_ESH:
      case OPTION_USH:
	hfs_select |= DO_ESH;
	break;
      case OPTION_FE:
	hfs_select |= DO_FEU;
	hfs_select |= DO_FEL;
	break;
      case OPTION_SGI:
      case OPTION_XIN:
	hfs_select |= DO_SGI;
	break;
      case OPTION_MBIN:
	hfs_select |= DO_MBIN;
	break;
      case OPTION_SGL:
	hfs_select |= DO_SGL;
	break;
      case OPTION_CREATE_DT:
	create_dt = 0;
	break;
      case OPTION_HFS_HIDE:
        hfs_add_match(optarg);
	break;
      case OPTION_H_LIST:
	hfs_add_list(optarg);
	break;
/* NON-HFS change 
   The next options will probably appear in mkisofs in the future */
      case OPTION_P_LIST:
	pathnames = optarg;
	break;
      case OPTION_X_LIST:
	add_list(optarg);
	break;
      case OPTION_I_LIST:
	i_add_list(optarg);
	break;
      case OPTION_J_LIST:
	j_add_list(optarg);
	break;
#endif /* APPLE_HYB */
      default:
	usage();
	exit(1);
      }

parse_input_files:

#if defined(__NetBSD__) || defined(__OpenBSD__) 
    {
    struct rlimit rlp;
	if (getrlimit(RLIMIT_DATA,&rlp) == -1) 
		perror("Warning: getrlimit");
	else {
		rlp.rlim_cur=33554432;
		if (setrlimit(RLIMIT_DATA,&rlp) == -1)
			perror("Warning: setrlimit");
		}
	}
#endif
#ifdef HAVE_SBRK
  mem_start = (unsigned long) sbrk(0);
#endif

  /* if the -hide-joliet option has been given, set the Joliet option */
  if (!use_Joliet && j_ishidden())
    use_Joliet++;

#ifdef APPLE_HYB
  if (apple_hyb && apple_ext) {
    fprintf(stderr,"can't have both -apple and -hfs options");
    exit (1);
  }

  /* if -probe, -macname, any hfs selection and/or mapping file is given,
     but no HFS option, then select apple_hyb */
  if (!apple_hyb && !apple_ext) {
    if (*afpfile || probe || mac_name || nomacfiles || hfs_select || hfs_boot_file || magic_file || hfs_ishidden() || gen_pt || autoname || bsize)
	apple_hyb = 1;
  }

  if (apple_ext && hfs_boot_file) {
    fprintf(stderr,"can't have -hfs-boot-file with -apple\n");
    exit (1);
  }

  if (hfs_select)
    /* if we have selected certain types of Mac/Unix files, then turn off
       probe and nomacfiles */
    probe = nomacfiles = 0;

  if (apple_hyb || apple_ext)
    apple_both = 1;

  if (apple_both) {
    /* set up the TYPE/CREATOR mappings */
    hfs_init(afpfile, 0, probe, nomacfiles, hfs_select);
  }

  if (apple_ext && !use_RockRidge) {
    /* use RockRidge to set the SystemUse field ... */
    use_RockRidge++;
    rationalize++;
  }

#endif /* APPLE_HYB */

  if(verbose > 1) fprintf(stderr,"%s\n", version_string);

  if(cdwrite_data == NULL && merge_image != NULL)
    {
      fprintf(stderr,"Multisession usage bug: Must specify -C if -M is used.\n");
      exit(0);
    }

  if(cdwrite_data != NULL && merge_image == NULL)
    {
      fprintf(stderr,"Warning: -C specified without -M: old session data will not be merged.\n");
    }

  /*  The first step is to scan the directory tree, and take some notes */

  scan_tree = argv[optind];


  if(!scan_tree){
	  usage();
	  exit(1);
  };

#ifndef VMS
  if(scan_tree[strlen(scan_tree)-1] != '/') {
    scan_tree = (char *) e_malloc(strlen(argv[optind])+2);
    strcpy(scan_tree, argv[optind]);
    strcat(scan_tree, "/");
  };
#endif

  if(use_RockRidge){
#if 1
	extension_record = generate_rr_extension_record("RRIP_1991A",
				       "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS",
				       "PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT INFORMATION.", &extension_record_size);
#else
	extension_record = generate_rr_extension_record("IEEE_P1282",
				       "THE IEEE P1282 PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS",
				       "PLEASE CONTACT THE IEEE STANDARDS DEPARTMENT, PISCATAWAY, NJ, USA FOR THE P1282 SPECIFICATION.", &extension_record_size);
#endif
  }

  if (log_file) {
    FILE *lfp;
    int i;

    /* open log file - test that we can open OK */
    if ((lfp = fopen(log_file, "w")) == NULL) {
      fprintf(stderr,"can't open logfile: %s\n", log_file);
      exit (1);
    }
    fclose(lfp);

    /* redirect all stderr message to log_file */
    fprintf(stderr, "re-directing all messages to %s\n", log_file);
    fflush(stderr);

    /* associate stderr with the log file */
    if (freopen(log_file, "w", stderr) == NULL) {
      fprintf(stderr,"can't open logfile: %s\n", log_file);
      exit (1);
    }
    if(verbose > 1) {
      for (i=0;i<argc;i++)
       fprintf(stderr,"%s ", argv[i]);

      fprintf(stderr,"\n%s\n", version_string);
    }
  }

  /*
   * See if boot catalog file exists in root directory, if not
   * we will create it.
   */
  if (use_eltorito)
    init_boot_catalog(argv[optind]);

  /*
   * Find the device and inode number of the root directory.
   * Record this in the hash table so we don't scan it more than
   * once.
   */
  stat_filter(argv[optind], &statbuf);
  add_directory_hash(statbuf.st_dev, STAT_INODE(statbuf));

  memset(&de, 0, sizeof(de));

  de.filedir = root;  /* We need this to bootstrap */

  if (cdwrite_data != NULL && merge_image == NULL) {
    /* in case we want to add a new session, but don't want to merge old one */
    get_session_start(NULL);
  }

  if( merge_image != NULL )
    {
      mrootp = merge_isofs(merge_image);
      if( mrootp == NULL )
	{
	  /*
	   * Complain and die.
	   */
	  fprintf(stderr,"Unable to open previous session image %s\n",
		  merge_image);
	  exit(1);
	}

      memcpy(&de.isorec.extent, mrootp->extent, 8);      
    }

  /*
   * Create an empty root directory. If we ever scan it for real, we will fill in the
   * contents.
   */
  find_or_create_directory(NULL, "", &de, TRUE);

#ifdef APPLE_HYB
  /* NON-HFS change: see if we have a list of pathnames to process */
  if (pathnames) {
	/* "-" means take list from the standard input */
	if (strcmp(pathnames, "-")) {
	    if ((pfp = fopen(pathnames, "r")) == NULL) {
		fprintf(stderr, "unable to open pathname list %s\n",pathnames);
		exit (1);
	    }
	}
	else
	    pfp = stdin;
  }
#endif /* APPLE_HYB */

  /*
   * Scan the actual directory (and any we find below it)
   * for files to write out to the output image.  Note - we
   * take multiple source directories and keep merging them
   * onto the image.
   */
#if APPLE_HYB
  /* NON-HFS change */
  while((arg = get_pnames(argc, argv, optind, pname, pfp)) != NULL)
#else
  while(optind < argc)
#endif /* APPLE_HYB */
    {
      char * node;
      struct directory * graft_dir;
      struct stat        st;
      char             * short_name;
      int                status;
      char   graft_point[1024];

      /*
       * We would like a syntax like:
       *
       * /tmp=/usr/tmp/xxx
       *
       * where the user can specify a place to graft each
       * component of the tree.  To do this, we may have to create
       * directories along the way, of course.
       * Secondly, I would like to allow the user to do something
       * like:
       *
       * /home/baz/RMAIL=/u3/users/baz/RMAIL
       *
       * so that normal files could also be injected into the tree
       * at an arbitrary point.
       *
       * The idea is that the last component of whatever is being
       * entered would take the name from the last component of
       * whatever the user specifies.
       *
       * The default will be that the file is injected at the
       * root of the image tree.
       */
#ifdef APPLE_HYB
      /* NON-HFS change */
      node = strchr(arg, '=');
#else
      node = strchr(argv[optind], '=');
#endif /* APPLE_HYB */
      short_name = NULL;

      if( node != NULL )
	{
	  char * pnt;
	  char * xpnt;

	  *node = '\0';
#ifdef APPLE_HYB
	  /* NON-HFS change */
	  strcpy(graft_point, arg);
#else
	  strcpy(graft_point, argv[optind]);
#endif /* APPLE_HYB */
	  *node = '=';
	  node++;

	  graft_dir = root;
	  xpnt = graft_point;
	  if( *xpnt == PATH_SEPARATOR )
	    {
	      xpnt++;
	    }

	  /*
	   * Loop down deeper and deeper until we
	   * find the correct insertion spot.
	   */
	  while(1==1)
	    {
	      pnt = strchr(xpnt, PATH_SEPARATOR);
	      if( pnt == NULL )
		{
		  if( *xpnt != '\0' )
		    {
		      short_name = xpnt;
		    }
		  break;
		}
	      *pnt = '\0';
	      graft_dir = find_or_create_directory(graft_dir, 
						   graft_point, 
						   NULL, TRUE);
	      *pnt = PATH_SEPARATOR;
	      xpnt = pnt + 1;
	    }
	}
      else
	{
	  graft_dir = root;
#ifdef APPLE_HYB
	  /* NON-HFS change */
	  node = arg;
#else
	  node = argv[optind];
#endif /* APPLE_HYB */
	}

      /*
       * Now see whether the user wants to add a regular file,
       * or a directory at this point.
       */
      status = stat_filter(node, &st);
      if( status != 0 )
	{
	  /*
	   * This is a fatal error - the user won't be getting what
	   * they want if we were to proceed.
	   */
	  fprintf(stderr, "Invalid node - %s\n", node);
	  exit(1);
	}
      else
	{
	  if( S_ISDIR(st.st_mode) )
	    {
	      if (!scan_directory_tree(graft_dir, node, &de))
		{
		  exit(1);
		}
	    }
	  else
	    {
	      if( short_name == NULL )
		{
		  short_name = strrchr(node, PATH_SEPARATOR);
		  if( short_name == NULL || short_name < node )
		    {
		      short_name = node;
		    }
		  else
		    {
		      short_name++;
		    }
		}
#ifdef APPLE_HYB
	      if( !insert_file_entry(graft_dir, node, short_name, 0) )
#else
	      if( !insert_file_entry(graft_dir, node, short_name) )
#endif /* APPLE_HYB */
		{
		 exit(1);
		}
	    }
	}

      optind++;
#ifdef APPLE_HYB
      /* NON-HFS change */
      no_path_names = 0;
#endif /* APPLE_HYB */
    }

#ifdef APPLE_HYB
  /* NON-HFS change */
  if (pfp && pfp != stdin)
    fclose(pfp);

  /* exit if we don't have any pathnames to process - not going to happen 
     at the moment as we have to have at least one path on the command line */
  if(no_path_names){
          usage();
          exit(1);
  };
#endif /* APPLE_HYB */

  /*
   * Now merge in any previous sessions.  This is driven on the source
   * side, since we may need to create some additional directories.
   */
  if( merge_image != NULL )
    {
      merge_previous_session(root, mrootp);
    }
#ifdef APPLE_HYB
  /* free up any HFS filename mapping memory */
  if (apple_both)
    clean_hfs();
#endif /* APPLE_HYB */

  /* hide "./rr_moved" if all its contents have been hidden */
  if (reloc_dir && i_ishidden())
    hide_reloc_dir();

  /*
   * Sort the directories in the required order (by ISO9660).  Also,
   * choose the names for the 8.3 filesystem if required, and do
   * any other post-scan work.
   */
  goof += sort_tree(root);

  if( use_Joliet )
    {
      goof += joliet_sort_tree(root);
    }

  if (goof)
    {
      fprintf(stderr, "Joliet tree sort failed.\n");
      exit(1);
    }
  
  /*
   * Fix a couple of things in the root directory so that everything
   * is self consistent.
   */
  root->self = root->contents;  /* Fix this up so that the path 
				   tables get done right */

  /*
   * OK, ready to write the file.  Open it up, and generate the thing.
   */
  if (print_size){
	  discimage = fopen("/dev/null", "wb");
	  if (!discimage){
		  fprintf(stderr,"Unable to open /dev/null\n");
		  exit(1);
	  }
  } else if (outfile){
	  discimage = fopen(outfile, "wb");
	  if (!discimage){
		  fprintf(stderr,"Unable to open disc image file\n");
		  exit(1);

	  };
  } else {
	  discimage =  stdout;

#if	defined(__CYGWIN32__)
	setmode(fileno(stdout), O_BINARY);
#endif
  }

  /* Now assign addresses on the disc for the path table. */

  path_blocks = (path_table_size + (SECTOR_SIZE - 1)) >> 11;
  if (path_blocks & 1) path_blocks++;

  jpath_blocks = (jpath_table_size + (SECTOR_SIZE - 1)) >> 11;
  if (jpath_blocks & 1) jpath_blocks++;

  /*
   * Start to set up the linked list that we use to track the
   * contents of the disc.
   */
  outputlist_insert(&padblock_desc);

  /*
   * PVD for disc.
   */
  outputlist_insert(&voldesc_desc);

  /*
   * SVD for El Torito. MUST be immediately after the PVD!
   */
  if( use_eltorito)
    {
      outputlist_insert(&torito_desc);
    }

  /*
   * SVD for Joliet.
   */
  if( use_Joliet)
    {
      outputlist_insert(&joliet_desc);
    }

  /*
   * Finally the last volume desctiptor.
   */
  outputlist_insert(&end_vol);


  outputlist_insert(&pathtable_desc);
  if( use_Joliet)
    {
      outputlist_insert(&jpathtable_desc);
    }

  outputlist_insert(&dirtree_desc);
  if( use_Joliet)
    {
      outputlist_insert(&jdirtree_desc);
    }

  outputlist_insert(&dirtree_clean);

  if(extension_record) 
    { 
      outputlist_insert(&extension_desc);
    }

  outputlist_insert(&files_desc);

  /*
   * Allow room for the various headers we will be writing.  There
   * will always be a primary and an end volume descriptor.
   */
  last_extent = session_start;
  
  /*
   * Calculate the size of all of the components of the disc, and assign
   * extent numbers.
   */
  for(opnt = out_list; opnt; opnt = opnt->of_next )
    {
      if( opnt->of_size != NULL )
	{
	  (*opnt->of_size)(last_extent);
	}
    }

  /*
   * Generate the contents of any of the sections that we want to generate.
   * Not all of the fragments will do anything here - most will generate the
   * data on the fly when we get to the write pass.
   */
  for(opnt = out_list; opnt; opnt = opnt->of_next )
    {
      if( opnt->of_generate != NULL )
	{
	  (*opnt->of_generate)();
	}
    }

  if( in_image != NULL )
    {
      fclose(in_image);
    }

  /*
   * Now go through the list of fragments and write the data that corresponds to
   * each one.
   */
  for(opnt = out_list; opnt; opnt = opnt->of_next )
    {
      if( opnt->of_write != NULL )
	{
	  (*opnt->of_write)(discimage);
	}
    }

  if( verbose > 0 )
    {
#ifdef HAVE_SBRK
      fprintf(stderr,"Max brk space used %x\n", 
	      (unsigned int)(((unsigned long)sbrk(0)) - mem_start));
#endif
      fprintf(stderr,"%d extents written (%d Mb)\n", last_extent, last_extent >> 9);
    }
#ifdef APPLE_HYB
  last_extent += hfs_extra;
#endif /* APPLE_HYB */

#ifdef VMS
  return 1;
#else
  return 0;
#endif
}

void *
FDECL1(e_malloc, size_t, size)
{
void* pt = 0;
	if( (size > 0) && ((pt=malloc(size))==NULL) ) {
		fprintf(stderr, "Not enough memory\n");
		exit (1);
		}
return pt;
}
