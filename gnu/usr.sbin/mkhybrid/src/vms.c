/*
 * File vms.c - assorted bletcherous hacks for VMS.

   Written by Eric Youngdale (1993).
 */

#ifdef VMS
#include <rms.h>
#include <descrip.h>
#include <ssdef.h>
#include <sys/types.h>
#include <sys/stat.h>
#define opendir fake_opendir
#include "mkisofs.h"
#undef opendir
#include <stdio.h>

static struct RAB *rab;	/* used for external mailfiles */
static int rms_status;

static error_exit(char * text){
	fprintf(stderr,"%s\n", text);
	exit(33);
}


char * strrchr(const char *, char);

char * strdup(char * source){
  char * pnt;
  pnt = (char *) e_malloc(strlen(source) + 1);
  strcpy(pnt, source);
  return pnt;
}

int VMS_stat(char * path, struct stat * spnt){
  char * spath;
  char sbuffer[255];
  char * pnt, *ppnt;
  char * pnt1;

  ppnt = strrchr(path,']');
  if(ppnt) ppnt++;
  else ppnt = path;

  spath = path;

  if(strcmp(ppnt,".") == 0 || strcmp(ppnt,"..") == 0){
    strcpy(sbuffer, path);

    /* Find end of actual name */
    pnt = strrchr(sbuffer,']');
    if(!pnt) return 0;

    pnt1 = pnt;
    while(*pnt1 != '[' && *pnt1 != '.') pnt1--;

    if(*pnt1 != '[' && strcmp(ppnt,"..") == 0) {
      pnt1--;
      while(*pnt1 != '[' && *pnt1 != '.') pnt1--;
    };

    if(*pnt1 == '.') {
      *pnt1 = ']';
      pnt = pnt1;
      while(*pnt != '.' && *pnt != ']') pnt++;
      *pnt++ = ']';
      while(*pnt != '.' && *pnt != ']') pnt++;
      *pnt = 0;
      strcat(sbuffer,".DIR;1");
    };

    if(*pnt1 == '[') {
      pnt1++;
      *pnt1 = 0;
      strcat(pnt1,"000000]");
      pnt1 = strrchr(path,'[') + 1;
      pnt = sbuffer + strlen(sbuffer);
      while(*pnt1 && *pnt1 != '.' && *pnt1 != ']') *pnt++ = *pnt1++;
      *pnt = 0;
      strcat(sbuffer,".DIR;1");
    };

    spath = sbuffer;
  };
  return stat_filter(spath, spnt);
}

static int dircontext[32] = {0,};
static char * searchpath[32];
static struct direct d_entry[32];

int optind = 0;
char * optarg;

int  getopt(int argc, char *argv[], char * flags){
  char * pnt;
  char c;
  optind++;
  if(*argv[optind] != '-') return EOF;
  optarg = 0;

  c = *(argv[optind]+1);
  pnt = (char *) strchr(flags, c);
  if(!pnt) return c;  /* Not found */
  if(pnt[1] == ':') {
    optind++;
    optarg = argv[optind];
  };
  return c;
}

void vms_path_fixup(char * name){
  char * pnt1;
  pnt1 = name + strlen(name) - 6;

  /* First strip the .DIR;1 */
  if(strcmp(pnt1, ".DIR;1") == 0) *pnt1 = 0;

  pnt1 = (char*) strrchr(name, ']');
  if(pnt1) {
    if(pnt1[1] == 0) return;
    *pnt1 = '.';
    strcat(name,"]");
    return;
  };
  pnt1 = (char*) strrchr(name, '>');
  if(pnt1) {
    if(pnt1[1] == 0) return;
    *pnt1 = '.';
    strcat(name,">");
    return;
  };
}

int opendir(char * path){
  int i;
  for(i=1; i<32; i++) {
    if(dircontext[i] == 0){
      dircontext[i] = -1;
      searchpath[i] = (char *) e_malloc(strlen(path) + 6);
      strcpy(searchpath[i], path);
      vms_path_fixup(searchpath[i]);
      strcat(searchpath[i],"*.*.*");
      return i;
    };
  };
  exit(0);
}

struct direct * readdir(int context){
  int i;
  char cresult[100];
  char * pnt;
  int status;
  $DESCRIPTOR(dpath,searchpath[context]);
  $DESCRIPTOR(result,cresult);

  if(dircontext[context] == -1) {
    dircontext[context] = -2;
    strcpy(d_entry[context].d_name, ".");
    return &d_entry[context];
  };

  if(dircontext[context] == -2) {
    dircontext[context] = -3;
    strcpy(d_entry[context].d_name, "..");
    return &d_entry[context];
  };

  if(dircontext[context] == -3) dircontext[context] = 0;

  dpath.dsc$w_length = strlen(searchpath[context]);
  lib$find_file(&dpath, &result, &dircontext[context], 
		0, 0, &status, 0);

  if(status == SS$_NOMOREFILES) return 0;

  /* Now trim trailing spaces from the name */
  i = result.dsc$w_length - 1;
  while(i && cresult[i] == ' ') i--;
  cresult[i+1] = 0;

  /* Now locate the actual portion of the file we want */

  pnt = (char *) strrchr(cresult,']');
  if(pnt) pnt++;
  else
    pnt = cresult;

  strcpy(d_entry[context].d_name, pnt);
  return &d_entry[context];
}

void closedir(int context){
  lib$find_file_end(&dircontext[context]);
  free(searchpath[context]);
  searchpath[context] = (char *) 0;
  dircontext[context] = 0;
}

static open_file(char* fn){
/* this routine initializes a rab and  fab required to get the
   correct definition of the external data file used by mail */
	struct FAB * fab;

	rab = (struct RAB*) e_malloc(sizeof(struct RAB));
	fab = (struct FAB*) e_malloc(sizeof(struct FAB));

	*rab = cc$rms_rab;	/* initialize RAB*/
	rab->rab$l_fab = fab;

	*fab = cc$rms_fab;	/* initialize FAB*/
	fab->fab$l_fna = fn;
	fab->fab$b_fns = strlen(fn);
	fab->fab$w_mrs = 512;
	fab->fab$b_fac = FAB$M_BIO | FAB$M_GET;
	fab->fab$b_org = FAB$C_SEQ;
	fab->fab$b_rfm = FAB$C_FIX;
	fab->fab$l_xab = (char*) 0;

	rms_status = sys$open(rab->rab$l_fab);
	if(rms_status != RMS$_NORMAL && rms_status != RMS$_CREATED)
		error_exit("$OPEN");
	rms_status = sys$connect(rab);
	if(rms_status != RMS$_NORMAL)
		error_exit("$CONNECT");
	return 1;
}

static close_file(struct RAB * prab){
	rms_status = sys$close(prab->rab$l_fab);
	free(prab->rab$l_fab);
	free(prab);
	if(rms_status != RMS$_NORMAL)
		error_exit("$CLOSE");
}

#define NSECT 16
extern unsigned int last_extent_written;

int vms_write_one_file(char * filename, int size, FILE * outfile){
	int status, i;
	char buffer[SECTOR_SIZE * NSECT];
	int count;
	int use;
	int remain;

	open_file(filename);

	remain = size;
	
	while(remain > 0){
	  use =  (remain >  SECTOR_SIZE * NSECT - 1 ? NSECT*SECTOR_SIZE : remain);
	  use = ROUND_UP(use); /* Round up to nearest sector boundary */
	  memset(buffer, 0, use);
	  rab->rab$l_ubf = buffer;
	  rab->rab$w_usz = sizeof(buffer);
	  status = sys$read(rab);
	  fwrite(buffer, 1, use, outfile);
	  last_extent_written += use/SECTOR_SIZE;
	  if((last_extent_written % 1000) < use/SECTOR_SIZE) fprintf(stderr,"%d..", last_extent_written);
	  remain -= use;
	};
	
	close_file(rab);
}
#endif
