/*
 * 27-Mar-96: Jan-Piet Mens <jpm@mens.de>
 * added 'match' option (-m) to specify regular expressions NOT to be included
 * in the CD image.
 */

#ifdef APPLE_HYB
/*
 * Added a number of routines to create lists of files to hidden from
 * the ISO9660 and/or Joliet trees. James Pearson (j.pearson@ge.ucl.ac.uk)
 * January 1999 (these will probably appear in mkisofs in the future)
 */
#endif /* APPLE_HYB */

#include "config.h"
#include <prototyp.h>
#include <stdio.h>
#include <fnmatch.h>
#ifndef VMS
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#else
#include <stdlib.h>
#endif
#endif
#include <inttypes.h>
#include <string.h>
#include "match.h"

#define MAXMATCH 1000
static char *mat[MAXMATCH];

int  add_match(fn)
char * fn;
{
  register int i;

  for (i=0; mat[i] && i<MAXMATCH; i++);
  if (i == MAXMATCH) {
    fprintf(stderr,"Can't exclude RE '%s' - too many entries in table\n",fn);
    return 1;
  }

 
  mat[i] = (char *) malloc(strlen(fn)+1);
  if (! mat[i]) {
    fprintf(stderr,"Can't allocate memory for excluded filename\n");
    return 1;
  }

  strcpy(mat[i],fn);

  return 0;
}

int matches(fn)
char * fn;
{
  /* very dumb search method ... */
  register int i;

  for (i=0; mat[i] && i<MAXMATCH; i++) {
    if (fnmatch(mat[i], fn, FNM_PATHNAME) != FNM_NOMATCH) {
      return 1; /* found -> excluded filenmae */
    }
  }
  return 0; /* not found -> not excluded */
}

/* ISO9660/RR hide */

static char *i_mat[MAXMATCH];

int  i_add_match(fn)
char * fn;
{
  register int i;

  for (i=0; i_mat[i] && i<MAXMATCH; i++);
  if (i == MAXMATCH) {
    fprintf(stderr,"Can't exclude RE '%s' - too many entries in table\n",fn);
    return 1;
  }

 
  i_mat[i] = (char *) malloc(strlen(fn)+1);
  if (! i_mat[i]) {
    fprintf(stderr,"Can't allocate memory for excluded filename\n");
    return 1;
  }

  strcpy(i_mat[i],fn);

  return 0;
}

int i_matches(fn)
char * fn;
{
  /* very dumb search method ... */
  register int i;

  for (i=0; i_mat[i] && i<MAXMATCH; i++) {
    if (fnmatch(i_mat[i], fn, FNM_PATHNAME) != FNM_NOMATCH) {
      return 1; /* found -> excluded filenmae */
    }
  }
  return 0; /* not found -> not excluded */
}

intptr_t i_ishidden()
{
  return((intptr_t)i_mat[0]);
}

/* Joliet hide */

static char *j_mat[MAXMATCH];

int  j_add_match(fn)
char * fn;
{
  register int i;

  for (i=0; j_mat[i] && i<MAXMATCH; i++);
  if (i == MAXMATCH) {
    fprintf(stderr,"Can't exclude RE '%s' - too many entries in table\n",fn);
    return 1;
  }

 
  j_mat[i] = (char *) malloc(strlen(fn)+1);
  if (! j_mat[i]) {
    fprintf(stderr,"Can't allocate memory for excluded filename\n");
    return 1;
  }

  strcpy(j_mat[i],fn);

  return 0;
}

int j_matches(fn)
char * fn;
{
  /* very dumb search method ... */
  register int i;

  for (i=0; j_mat[i] && i<MAXMATCH; i++) {
    if (fnmatch(j_mat[i], fn, FNM_PATHNAME) != FNM_NOMATCH) {
      return 1; /* found -> excluded filenmae */
    }
  }
  return 0; /* not found -> not excluded */
}

intptr_t j_ishidden()
{
  return((intptr_t)j_mat[0]);
}

#ifdef APPLE_HYB

/* HFS hide */

static char *hfs_mat[MAXMATCH];

int hfs_add_match(fn)
char * fn;
{
  register int i;

  for (i=0; hfs_mat[i] && i<MAXMATCH; i++);
  if (i == MAXMATCH) {
    fprintf(stderr,"Can't exclude RE '%s' - too many entries in table\n",fn);
    return 1;
  }

 
  hfs_mat[i] = (char *) malloc(strlen(fn)+1);
  if (! hfs_mat[i]) {
    fprintf(stderr,"Can't allocate memory for excluded filename\n");
    return 1;
  }

  strcpy(hfs_mat[i],fn);

  return 0;
}

void hfs_add_list(file)
char *file;
{
  FILE *fp;
  char name[1024];

  if ((fp = fopen(file, "r")) == NULL) {
    fprintf(stderr,"Can't open hidden file list %s\n", file);
    exit (1);
  }

  while (fscanf(fp, "%s", name) != EOF) {
    if (hfs_add_match(name)) {
      fclose(fp);
      return;
    }
  }

  fclose(fp);
}


int hfs_matches(fn)
char * fn;
{
  /* very dumb search method ... */
  register int i;

  for (i=0; hfs_mat[i] && i<MAXMATCH; i++) {
    if (fnmatch(hfs_mat[i], fn, FNM_PATHNAME) != FNM_NOMATCH) {
      return 1; /* found -> excluded filenmae */
    }
  }
  return 0; /* not found -> not excluded */
}

intptr_t hfs_ishidden()
{
  return((intptr_t)hfs_mat[0]);
}

/* These will probably appear in mkisofs in the future */

void add_list(file)
char *file;
{
  FILE *fp;
  char name[1024];

  if ((fp = fopen(file, "r")) == NULL) {
    fprintf(stderr,"Can't open exclude file list %s\n", file);
    exit (1);
  }

  while (fscanf(fp, "%s", name) != EOF) {
    if (add_match(name)) {
      fclose(fp);
      return;
    }
  }

  fclose(fp);
}

void i_add_list(file)
char *file;
{
  FILE *fp;
  char name[1024];

  if ((fp = fopen(file, "r")) == NULL) {
    fprintf(stderr,"Can't open hidden file list %s\n", file);
    exit (1);
  }

  while (fscanf(fp, "%s", name) != EOF) {
    if (i_add_match(name)) {
      fclose(fp);
      return;
    }
  }

  fclose(fp);
}

void j_add_list(file)
char *file;
{
  FILE *fp;
  char name[1024];

  if ((fp = fopen(file, "r")) == NULL) {
    fprintf(stderr,"Can't open hidden file list %s\n", file);
    exit (1);
  }

  while (fscanf(fp, "%s", name) != EOF) {
    if (j_add_match(name)) {
      fclose(fp);
      return;
    }
  }

  fclose(fp);
}

#endif /* APPLE_HYB */
