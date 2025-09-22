/*
**	Unix-HFS file interface including maping file extensions to TYPE/CREATOR
**
**	Adapted from mkhfs routines for mkhybrid
**
**	James Pearson 1/5/97
**	Bug fix JCP 4/12/97
**	Updated for 1.12 and added more Unix HFS filetypes. JCP 21/1/98
**
**	Things still to de done:
**
**		Check SGI (XINET) structs
**		Check file size = finder + rsrc [+ data] is needed
**		AppleSingle/Double version 2?
**		Clean up byte order swapping.
*/

#ifdef APPLE_HYB

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>
#include <config.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <apple.h>
#include "apple_proto.h"
#include <mkisofs.h>

/* tidy up mkisofs definition ... */
typedef struct directory_entry dir_ent;

/* routines for getting HFS names and info */
static int get_none_dir(char *, char *, dir_ent *, int);
static int get_none_info(char *, char *, dir_ent *, int);
static int get_cap_dir(char *, char *, dir_ent *, int);
static int get_cap_info(char *, char *, dir_ent *, int);
static int get_es_info(char *, char *, dir_ent *, int);
static int get_dbl_info(char *, char *, dir_ent *, int);
static int get_mb_info(char *, char *, dir_ent *, int);
static int get_sgl_info(char *, char *, dir_ent *, int);
static int get_fe_dir(char *, char *, dir_ent *, int);
static int get_fe_info(char *, char *, dir_ent *, int);
static int get_sgi_dir(char *, char *, dir_ent *, int);
static int get_sgi_info(char *, char *, dir_ent *, int);

void map_ext(char *, char **, char **, unsigned short *, char *);
static afpmap	**map;				/* list of mappings */
static afpmap	*defmap;			/* the default mapping */
static int	last_ent;			/* previous mapped entry */
static int	map_num;			/* number of mappings */
static int	mlen;				/* min extension length */
static char	tmp[MAXPATHLEN];		/* tmp working buffer */
static int	hfs_num;			/* number of file types */
static char	p_buf[MAXPATHLEN];		/* info working buffer */
static FILE	*p_fp = NULL;			/* probe File pointer */
static int	p_num = 0;			/* probe bytes read */
static unsigned int	hselect;		/* type of HFS file selected */

struct hfs_type {			/* Types of various HFS Unix files */
  int	type;					/* type of file */
  int	flags;					/* special flags */
  char *info;           			/* finderinfo name */
  char *rsrc;           			/* resource fork name */
  int  (*get_info)(char*, char*, dir_ent*,int);	/* finderinfo function */
  int  (*get_dir)(char*, char*,dir_ent*,int);	/* directory name function */
  char *desc;					/* description */
};

/* Above filled in */
static struct hfs_type hfs_types[] = {
    {TYPE_NONE,0,"", "", get_none_info, get_none_dir,"None"},
    {TYPE_CAP,0,".finderinfo/", ".resource/", get_cap_info, get_cap_dir,"CAP"},
    {TYPE_NETA,0,".AppleDouble/", ".AppleDouble/", get_dbl_info, get_none_dir,"Netatalk"},
    {TYPE_DBL,0,"%", "%", get_dbl_info, get_none_dir,"AppleDouble"},
    {TYPE_ESH, 0,".rsrc/", ".rsrc/", get_es_info, get_none_dir,"EtherShare/UShare"},
    {TYPE_FEU,2,"FINDER.DAT", "RESOURCE.FRK/", get_fe_info, get_fe_dir,"Exchange"},
    {TYPE_FEL,2,"finder.dat", "resource.frk/", get_fe_info, get_fe_dir,"Exchange"},
    {TYPE_SGI,2,".HSancillary", ".HSResource/", get_sgi_info, get_sgi_dir,"XINET/SGI"},
    {TYPE_MBIN,1,"", "", get_mb_info, get_none_dir,"MacBinary"},
    {TYPE_SGL,1,"", "", get_sgl_info, get_none_dir,"AppleSingle"}
};

/* used by get_magic_match() return */
static char tmp_type[CT_SIZE+1], tmp_creator[CT_SIZE+1];

/*
**	cstrncopy: Cap Unix name to HFS name
**
**	':' is replaced by '%' and string is terminated with '\0'
*/
void
cstrncpy(char *t, char *f, int c)
{
	while (c-- && *f)
	{
	    switch (*f)
	    {
		case ':':
		    *t = '%';
		    break;
		default:
		    *t = *f;
		    break;
	    }
	    t++; f++;
	}

	*t = '\0';
}
/*
** dehex()
**
** Given a hexadecimal digit in ASCII, return the integer representation.
**
**	Taken from linux/fs/hfs/trans.c by Paul H. Hargrove
*/
static unsigned char
dehex(char c)
{
	if ((c>='0')&&(c<='9')) {
	    return c-'0';
	}
	if ((c>='a')&&(c<='f')) {
	    return c-'a'+10;
	}
	if ((c>='A')&&(c<='F')) {
	    return c-'A'+10;
	}
/*	return 0xff; */
	return (0);
}

static unsigned char
hex2char(char *s)
{
	unsigned char o;

	if(strlen(++s) < 2)
	    return(0);

	if (!isxdigit(*s) || !isxdigit(*(s+1)))
	    return(0);

	o = (dehex(*s) << 4) & 0xf0;
	s++;
	o |= (dehex(*s) & 0xf);

	return (o);
}

/*
**	hstrncpy: Unix name to HFS name with special character
**	translation.
**
**	"%xx" or ":xx" is assumed to be a "special" character and
**	replaced by character code given by the hex characters "xx"
**
**	if "xx" is not a hex number, then it is left alone - except
**	that ":" is replaced by "%"
**	
*/
void
hstrncpy(unsigned char *t, char *f, int c)
{
	unsigned char	o;
	while (c-- && *f)
	{
	    switch (*f)
	    {
		case ':':
		case '%':
		    if ((o = hex2char(f)) == 0) {
			*t = '%';
		    }
		    else {
			*t = o;
			f += 2;
		    }
		    break;
		default:
		    *t = *f;
		    break;
	    }
	    t++; f++;
	}

	*t = '\0';
}

/*
**	basename: find just the filename with any directory component
*/
/* not used at the moment ...
static char
basename(char *a)
{
	char *b;

	if((b = strchr(a, '/')))
	    return(++b);
	else
	    return(a);
}
*/

/*
**	get_none_dir: ordinary Unix directory
*/
int
get_none_dir(char *hname, char *dname, dir_ent *s_entry, int ret)
{
	/* just copy the given name */
	hstrncpy(s_entry->hfs_ent->name, dname, HFS_MAX_FLEN);

	return(ret);
}

/*
**	get_none_info: ordinary Unix file - try to map extension
*/
int
get_none_info(char *hname, char *dname, dir_ent *s_entry, int ret)
{
	char	*t, *c;
	hfsdirent *hfs_ent = s_entry->hfs_ent;

	map_ext(dname, &t, &c, &s_entry->hfs_ent->fdflags, s_entry->whole_name);

	/* just copy the given name */
	hstrncpy(hfs_ent->name, dname, HFS_MAX_FLEN);

	strncpy(hfs_ent->type, t, CT_SIZE);
	strncpy(hfs_ent->creator, c, CT_SIZE);
	hfs_ent->type[CT_SIZE] = '\0';
	hfs_ent->creator[CT_SIZE] = '\0';

	return(ret);
}
/*
**	read_info_file:	open and read a finderinfo file for an HFS file
**			or directory
*/
int
read_info_file(char *name, void *info, int len)
/* char		*name;				finderinfo filename */
/* void	 	*info;				info buffer */
/* int		len;				length of above */
{
	FILE	*fp;
	int	num;

	/* clear out any old finderinfo stuf */
	memset(info, 0, len);

	if ((fp = fopen(name,"rb")) == NULL)
	    return(-1);

	/* read and ignore if the file is short - checked later */
	num = fread(info,1,len,fp);

	fclose(fp);

	return(num);
}
/*
**	get_cap_dir: get the CAP name for a directory
*/
int
get_cap_dir(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	FileInfo	info;			/* finderinfo struct */
	int		num = -1;		/* bytes read */

	num = read_info_file(hname, &info, sizeof(FileInfo));

	/* check finder info is OK */
	if (num > 0
		&& info.fi_magic1 == FI_MAGIC1
		&& info.fi_magic == FI_MAGIC
		&& info.fi_bitmap & FI_BM_MACINTOSHFILENAME) {
	    /* use the finderinfo name if it exists */
	    cstrncpy(s_entry->hfs_ent->name, info.fi_macfilename, HFS_MAX_FLEN);
	    return (ret);
	}
	else {
	    /* otherwise give it it's Unix name */
	    hstrncpy(s_entry->hfs_ent->name, dname, HFS_MAX_FLEN);
	    return (TYPE_NONE);
	}
}

/*
**	get_cap_info:	get CAP finderinfo for a file
*/
int
get_cap_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	FileInfo 	info;			/* finderinfo struct */
	int		num = -1;		/* bytes read */
	char		*c;
	char		*t;
	hfsdirent	*hfs_ent = s_entry->hfs_ent;

	num = read_info_file(hname, &info, sizeof(info));

	/* check finder info is OK */
	if (num > 0 
		&& info.fi_magic1 == FI_MAGIC1
		&& info.fi_magic == FI_MAGIC) {

	    if (info.fi_bitmap & FI_BM_MACINTOSHFILENAME) {
		/* use the finderinfo name if it exists */
		cstrncpy(hfs_ent->name, info.fi_macfilename, HFS_MAX_FLEN);
	    }
	    else {
		/* use Unix name */
		hstrncpy(hfs_ent->name, dname, HFS_MAX_FLEN);
	    }

	    /* type and creator from finder info */
	    t = info.fdType;
	    c = info.fdCreator;

	    strncpy(hfs_ent->type, t, CT_SIZE);
	    strncpy(hfs_ent->creator, c, CT_SIZE);
	    hfs_ent->type[CT_SIZE] = '\0';
	    hfs_ent->creator[CT_SIZE] = '\0';

	    /* finder flags */
	    hfs_ent->fdflags = d_getw((unsigned char *)&info.fdFlags);
	    /* clear HFS_FNDR_HASBEENINITED to have tidy desktop ?? */
	    hfs_ent->fdflags &= 0xfeff;

#ifdef USE_MAC_DATES
	    /* set created/modified dates - these date should have already
	       been set from the Unix data fork dates. The finderinfo dates
	       are in Mac format - but we have to convert them back to Unix
	       for the time being  */
	    if ((info.fi_datemagic & FI_CDATE)) {
		/* use libhfs routines to get correct byte order */
		hfs_ent->crdate = d_toutime(d_getl(info.fi_ctime));
	    }
	    if (info.fi_datemagic & FI_MDATE) {
		hfs_ent->mddate = d_toutime(d_getl(info.fi_mtime));
	    }
#endif /* USE_MAC_DATES */
	}
	else {
	    /* failed to open/read finderinfo - so try afpfile mapping */
	    if (verbose > 2) {
		fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
	    	s_entry->whole_name, hfs_types[ret].desc);
	    }

	    ret = get_none_info(hname, dname, s_entry, TYPE_NONE);
	}

	return (ret);
}

/*
**	get_es_info:	get EtherShare/UShare finderinfo for a file
**
**	based on code from Jens-Uwe Mager (jum@helios.de) and Phil Sylvester
**	<psylvstr@interaccess.com>
*/
int
get_es_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	es_FileInfo 	*einfo;			/* EtherShare info struct */
	us_FileInfo 	*uinfo;			/* UShare info struct */
	char		info[ES_INFO_SIZE];	/* finderinfo buffer */
	int		num = -1;		/* bytes read */
	char		*c;
	char		*t;
	hfsdirent	*hfs_ent = s_entry->hfs_ent;
	dir_ent		*s_entry1;

	/* the EtherShare and UShare file layout is the same, but they
	   store finderinfo differently */
	einfo = (es_FileInfo *)info;
	uinfo = (us_FileInfo *)info;

	num = read_info_file(hname, info, sizeof(info));

	/* check finder info for EtherShare finderinfo */
	if (num >= sizeof(es_FileInfo) &&
		ntohl(einfo->magic) == ES_MAGIC &&
		ntohs(einfo->version) == ES_VERSION) {

	    /* type and creator from finder info */
	    t = einfo->fdType;
	    c = einfo->fdCreator;

	    /* finder flags */
	    hfs_ent->fdflags = d_getw((unsigned char *)&einfo->fdFlags);

	    /* set create date - modified date set from the Unix data
	       fork date */
	    hfs_ent->crdate = d_getl((unsigned char *)&einfo->createTime);
	}
	else if (num >= sizeof(us_FileInfo)) {
	    /* UShare has no magic number, so we assume that this is
	       a valid info/resource file ... */

	    /* type and creator from finder info */
	    t = uinfo->fdType;
	    c = uinfo->fdCreator;

	    /* finder flags */
	    hfs_ent->fdflags = d_getw((unsigned char *)&uinfo->fdFlags);

	    /* set create and modified date - if they exist */
	    if (uinfo->ctime)
		hfs_ent->crdate = d_getl((unsigned char *)&uinfo->ctime);

	    if (uinfo->mtime)
		hfs_ent->mddate = d_getl((unsigned char *)&uinfo->mtime);
	}
	else {
	    /* failed to open/read finderinfo - so try afpfile mapping */
	    if (verbose > 2) {
		fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
	    	s_entry->whole_name, hfs_types[ret].desc);
	    }

	    ret = get_none_info(hname, dname, s_entry, TYPE_NONE);
	    return (ret);
	}

	/* this should exist ... */
	if ((s_entry1 = s_entry->assoc) == NULL)
	    errx(1, "TYPE_ESH error - shouldn't happen!");

	/* fill in the HFS info stuff */
	strncpy(hfs_ent->type, t, CT_SIZE);
	strncpy(hfs_ent->creator, c, CT_SIZE);
	hfs_ent->type[CT_SIZE] = '\0';
	hfs_ent->creator[CT_SIZE] = '\0';

	/* clear HFS_FNDR_HASBEENINITED to have tidy desktop ?? */
	hfs_ent->fdflags &= 0xfeff;

	/* set name */
	hstrncpy(hfs_ent->name, dname, HFS_MAX_FLEN);

	/* real rsrc file starts ES_INFO_SIZE bytes into the file */
	if(s_entry1->size <= ES_INFO_SIZE) {
	    s_entry1->size = 0;
	    hfs_ent->rsize = 0;
	}
	else {
	    s_entry1->size -= ES_INFO_SIZE; 
	    hfs_ent->rsize = s_entry1->size;
	    s_entry1->hfs_off = ES_INFO_SIZE;
	}

	set_733((char *) s_entry1->isorec.size, s_entry1->size);

	return (ret);
}

/*
 * calc_crc() --
 *   Compute the MacBinary II-style CRC for the data pointed to by p, with the
 *   crc seeded to seed.
 *
 *   Modified by Jim Van Verth to use the magic array for efficiency.
 */
static unsigned short
calc_mb_crc(unsigned char *p, long len, unsigned short seed)
{
  unsigned short hold;	/* crc computed so far */
  long  i;		/* index into data */

  hold = seed;     /* start with seed */
  for (i = 0; i < len; i++, p++) {
    hold ^= (*p << 8);
    hold = (hold << 8) ^ mb_magic[(unsigned char)(hold >> 8)];
  }

  return (hold);
} /* calc_mb_crc() */

int
get_mb_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	mb_info 	*info;			/* finderinfo struct */
	char		*c;
	char		*t;
	hfsdirent	*hfs_ent;
	dir_ent		*s_entry1;
	int		i;
#ifdef TEST_CODE
	unsigned short	crc_file, crc_calc;
#endif

	info = (mb_info *)p_buf;

	/* routine called twice for each file - first to check that
	   it is a valid MacBinary file, second to fill in the HFS
	   info. p_buf holds the required raw data and it *should*
	   remain the same between the two calls */

	if (s_entry == 0) {
	    /* test that the CRC is OK - not set for MacBinary I files
	       (and incorrect in some MacBinary II files!). If this
	       fails, then perform some other checks */

#ifdef TEST_CODE
	    /* leave this out for the time being ... */
	    if (p_num >= MB_SIZE && info->version == 0 && info->zero1 == 0) {
		crc_calc = calc_mb_crc((unsigned char *)info, 124, 0);
		crc_file = d_getw(info->crc);
#ifdef DEBUG
		fprintf(stderr,"%s: file %d, calc %d\n",hname,crc_file,crc_calc);
#endif /* DEBUG */
		if (crc_file == crc_calc)
		    return (ret);
	    }
#endif /* TEST_CODE */

	    /* check some of the fields for a valid MacBinary file
	       not zero1 and zero2 SHOULD be zero - but some files incorrect */

/*	    if (p_num < MB_SIZE || info->nlen > 63 || info->zero2 || */
	    if (p_num < MB_SIZE || info->zero1 ||
		info->zero2 || info->nlen > 63 ||
			info->version || info->nlen == 0 || *info->name == 0)
		return (TYPE_NONE);

	    /* check that the filename is OKish */
	    for (i=0;i<info->nlen;i++)
		if(info->name[i] == 0)
		    return (TYPE_NONE);

	    /* check CREATOR and TYPE are valid */
	    for (i=0;i<4;i++)
		if(info->type[i] == 0 || info->auth[i] == 0)
		    return (TYPE_NONE);
	}
	else {
	    /* we have a vaild MacBinary file, so fill in the bits */

	    /* this should exist ... */
	    if((s_entry1 = s_entry->assoc) == NULL)
		errx(1, "TYPE_MBIN error - shouldn't happen!");

	    hfs_ent = s_entry->hfs_ent;

	    /* type and creator from finder info */
	    t = info->type;
	    c = info->auth;

	    strncpy(hfs_ent->type, t, CT_SIZE);
	    strncpy(hfs_ent->creator, c, CT_SIZE);
	    hfs_ent->type[CT_SIZE] = '\0';
	    hfs_ent->creator[CT_SIZE] = '\0';

	    /* finder flags */
	    hfs_ent->fdflags = ((info->flags << 8) & 0xff00) | info->flags2; 
	    /* clear HFS_FNDR_HASBEENINITED to have tidy desktop ?? */
	    hfs_ent->fdflags &= 0xfeff;

	    /* set created/modified dates - these date should have already
	       been set from the Unix data fork dates. The finderinfo dates
	       are in Mac format - but we have to convert them back to Unix
	       for the time being  */

	    hfs_ent->crdate = d_toutime(d_getl(info->cdate));
	    hfs_ent->mddate = d_toutime(d_getl(info->mdate));

	    /* set name */
/*	    hstrncpy(hfs_ent->name, info->name, HFS_MAX_FLEN); */
	    hstrncpy(hfs_ent->name, info->name, MIN(HFS_MAX_FLEN, info->nlen));

	    /* set correct fork sizes */
	    hfs_ent->dsize = d_getl(info->dflen);
	    hfs_ent->rsize = d_getl(info->rflen);

	    /* update directory entries for data fork */
	    s_entry->size = hfs_ent->dsize;
	    s_entry->hfs_off = MB_SIZE;
	    set_733((char *) s_entry->isorec.size, s_entry->size);

	    /* real rsrc file starts after data fork (must be a multiple
	       of MB_SIZE) */
	    s_entry1->size = hfs_ent->rsize;
	    s_entry1->hfs_off = MB_SIZE + V_ROUND_UP(hfs_ent->dsize, MB_SIZE);
	    set_733((char *) s_entry1->isorec.size, s_entry1->size);
	}

	return (ret);
}

/*
**	get_dbl_info:	get Apple double finderinfo for a file
**
**	Based on code from cvt2cap.c (c) May 1988, Paul Campbell
*/
int
get_dbl_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	FileInfo 	info;			/* finderinfo struct */
	a_hdr		*hp;
	a_entry		*ep;
	int		num = -1;		/* bytes read */
	char		*c;
	char		*t;
	int		nentries;
	FILE		*fp;
	hfsdirent	*hfs_ent = s_entry->hfs_ent;
	dir_ent		*s_entry1;
	char		name[64];
	int		i;
	int		fail = 0;

	hp = (a_hdr *)p_buf;;
	memset(hp, 0, A_HDR_SIZE);

	memset(name, 0, sizeof(name));

	/* get the rsrc file info - should exist ... */
	if ((s_entry1 = s_entry->assoc) == NULL)
	    errx(1, "TYPE_DBL error - shouldn't happen!");

	/* open and read the info/rsrc file (it's the same file) */
	if ((fp = fopen(hname,"rb")) != NULL)
	    num = fread(hp, 1, A_HDR_SIZE, fp);

	/* check finder info is OK - some Netatalk files don't have
	   magic or version set - ignore if it's a netatalk file */
	if (num == A_HDR_SIZE && ((ret == TYPE_NETA) ||
		(ntohl(hp->magic) == APPLE_DOUBLE &&
		ntohl(hp->version) == A_VERSION))) {

	    /* read TOC of the AppleDouble file */
	    nentries = (int)ntohs(hp->nentries);
	    if(fread(hp->entries, A_ENTRY_SIZE, nentries, fp) < 1) {
		fail = 1;
		nentries = 0;
	    }

	    /* extract what is needed */
	    for (i=0, ep=hp->entries; i<nentries; i++,ep++) {
		switch(ntohl(ep->id)) {
		    case ID_FINDER:
			/* get the finder info */
			fseek(fp, ntohl(ep->offset), 0);
			if (fread(&info, ntohl(ep->length), 1, fp) < 1) {
			    fail = 1;
			}
			break;
		    case ID_RESOURCE:
			/* set the offset and correct rsrc fork size */
			s_entry1->size = ntohl(ep->length); 
			hfs_ent->rsize = s_entry1->size;
			/* offset to start of real rsrc fork */
			s_entry1->hfs_off = ntohl(ep->offset);
			set_733((char *) s_entry1->isorec.size, s_entry1->size);
			break;
		    case ID_NAME:
			/* get Mac file name */
			fseek(fp, ntohl(ep->offset), 0);
			if(fread(name, ntohl(ep->length), 1, fp) < 1)
			    *name = '\0';
			break;
		    default:
			break;
		}
	    }

	    fclose(fp);

	    /* skip this if we had a problem */
	    if (!fail) {
		/* type and creator from finder info */
		t = info.fdType;
		c = info.fdCreator;

		strncpy(hfs_ent->type, t, CT_SIZE);
		strncpy(hfs_ent->creator, c, CT_SIZE);
		hfs_ent->type[CT_SIZE] = '\0';
		hfs_ent->creator[CT_SIZE] = '\0';

		/* finder flags */
		hfs_ent->fdflags = d_getw((unsigned char *)&info.fdFlags);
		/* clear HFS_FNDR_HASBEENINITED to have tidy desktop ?? */
		hfs_ent->fdflags &= 0xfeff;

		/* use stored name if it exists */
		if (*name)
		    cstrncpy(hfs_ent->name, name, HFS_MAX_FLEN);
		else
		    hstrncpy(hfs_ent->name, dname, HFS_MAX_FLEN);
	    }
	}
	else {
	    /* failed to open/read finderinfo */
	    fail = 1;
	    if (fp)
		fclose(fp);
	}

	if (fail) {
	    /* problem with the file - try mapping/magic */
	    if (verbose > 2) {
		fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
		    s_entry->whole_name, hfs_types[ret].desc);
	    }
	    ret = get_none_info(hname, dname, s_entry, TYPE_NONE);
	}

	return (ret);
}
/*
**	get_sgl_info:	get Apple single finderinfo for a file
**
**	Based on code from cvt2cap.c (c) May 1988, Paul Campbell
*/
int
get_sgl_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	FileInfo 	*info = 0;		/* finderinfo struct */
	a_hdr		*hp;
	static a_entry	*entries;
	a_entry		*ep;
	char		*c;
	char		*t;
	int		nentries;
	hfsdirent	*hfs_ent;
	dir_ent		*s_entry1;
	char		name[64];
	int		i;

	/* routine called twice for each file - first to check that
	   it is a valid MacBinary file, second to fill in the HFS
	   info. p_buf holds the required raw data and it *should*
	   remain the same between the two calls */

	hp = (a_hdr *)p_buf;

	if (s_entry == 0) {
	    if (p_num < A_HDR_SIZE ||
		    ntohl(hp->magic) != APPLE_SINGLE ||
		    ntohl(hp->version) != A_VERSION)
		return (TYPE_NONE);

	    /* check we have TOC for the AppleSingle file */
	    nentries = (int)ntohs(hp->nentries);
	    if (p_num < (A_HDR_SIZE + nentries*A_ENTRY_SIZE))
		return (TYPE_NONE);

	    /* save the TOC */
	    entries = (a_entry *)e_malloc(nentries*A_ENTRY_SIZE);

	    memcpy(entries, (p_buf+A_HDR_SIZE), nentries*A_ENTRY_SIZE);
	}
	else {
	    /* have a vaild AppleSingle File */
	    memset(name, 0, sizeof(name));

	    /* get the rsrc file info - should exist ... */
	    if ((s_entry1 = s_entry->assoc) == NULL)
		errx(1, "TYPE_SGL error - shouldn't happen!");

	    hfs_ent = s_entry->hfs_ent;

	    nentries = (int)ntohs(hp->nentries);

	    /* extract what is needed */
	    for (i=0, ep=entries; i<nentries; i++,ep++) {
		switch(ntohl(ep->id)) {
		    case ID_FINDER:
			/* get the finder info */
			info = (FileInfo *)(p_buf + ntohl(ep->offset));
			break;
		    case ID_DATA:
			/* set the offset and correct data fork size */
			hfs_ent->dsize = s_entry->size = ntohl(ep->length);
			/* offset to start of real data fork */
			s_entry->hfs_off = ntohl(ep->offset);
			set_733((char *) s_entry->isorec.size, s_entry->size);
			break;
		    case ID_RESOURCE:
			/* set the offset and correct rsrc fork size */
			hfs_ent->rsize = s_entry1->size = ntohl(ep->length);
			/* offset to start of real rsrc fork */
			s_entry1->hfs_off = ntohl(ep->offset);
			set_733((char *) s_entry1->isorec.size, s_entry1->size);
			break;
		    case ID_NAME:
			strncpy(name, (p_buf+ntohl(ep->offset)),
				ntohl(ep->length));
			break;
		    default:
			break;
		}
	    }

	    free(entries);

	    if (info == NULL) {
		/* failed to open/read finderinfo - so try afpfile mapping */
		if (verbose > 2) {
		    fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
		        s_entry->whole_name, hfs_types[ret].desc);
		}
		ret = get_none_info(hname, dname, s_entry, TYPE_NONE);
		return (ret);
	    }

	    /* type and creator from finder info */
	    t = info->fdType;
	    c = info->fdCreator;

	    strncpy(hfs_ent->type, t, CT_SIZE);
	    strncpy(hfs_ent->creator, c, CT_SIZE);
	    hfs_ent->type[CT_SIZE] = '\0';
	    hfs_ent->creator[CT_SIZE] = '\0';

	    /* finder flags */
	    hfs_ent->fdflags = d_getw((unsigned char *)&info->fdFlags);
	    /* clear HFS_FNDR_HASBEENINITED to have tidy desktop ?? */
	    hfs_ent->fdflags &= 0xfeff;

	    /* use stored name if it exists */
	    if (*name)
		cstrncpy(hfs_ent->name, name, HFS_MAX_FLEN);
	    else
		hstrncpy(hfs_ent->name, dname, HFS_MAX_FLEN);
	}

	return (ret);
}

/*
**	get_hfs_fe_info: read in the whole finderinfo for a PC Exchange
**		directory - saves on reading this many times for each file.
**
**	Based of information provided by Mark Weinstein <mrwesq@earthlink.net>
**
**	Note: the FINDER.DAT file layout depends on the FAT cluster size
**	therefore, files should only be read directly from the FAT media
**
**	Only tested with PC Exchange v2.1 - don't know if it will work
**	with v2.2 and above.
*/
struct hfs_info *
get_hfs_fe_info(struct hfs_info *hfs_info, char *name)
{
	FILE	*fp;
#ifdef __svr4__
	struct statvfs fsbuf;
#else
	struct statfs fsbuf;
#endif /* __svr4__ */
	int	fe_num, fe_pad;
	fe_info info;
	int	c = 0;
	struct hfs_info *hfs_info1 = NULL;
	hfsdirent *hfs_ent;
	char	keyname[12];
	char	*s, *e, *k;
	int	i;

	if ((fp = fopen(name, "rb")) == NULL)
	    return(NULL);

	/* The FAT cluster size may have been given on the command line
	   - if not they try and find *guess* it */
	if (!bsize) {
	    /* FINDER.DAT layout depends on the FAT cluster size - assume
	       this is mapped to the "fundamental file system block size"
	       For SVR4 we use statvfs(), others use statfs() */
#ifdef __svr4__
	    if (statvfs(name, &fsbuf) < 0)
		return(NULL);

	    bsize = fsbuf.f_frsize;
#else
	    if (statfs(name, &fsbuf) < 0)
		return(NULL);

	    bsize = fsbuf.f_bsize;
#endif /* __svr4__ */
	}

	if (bsize <= 0)
	    return(NULL);

	fe_num = bsize/FE_SIZE;
	fe_pad = bsize%FE_SIZE;

	while(fread(&info, 1, FE_SIZE, fp) != 0) {

	    /* the Mac name may be NULL - so ignore this entry */
	    if (info.nlen != 0) {

		hfs_info1 = (struct hfs_info *)e_malloc(sizeof(struct hfs_info));
		/* add this entry to the list */
		hfs_info1->next = hfs_info;
		hfs_info = hfs_info1;

		hfs_ent = &hfs_info1->hfs_ent;

		/* get the bits we need - ignore [cm]time for the moment */
		cstrncpy(hfs_ent->name, info.name, info.nlen);

		strncpy(hfs_ent->type, info.type, CT_SIZE);
		strncpy(hfs_ent->creator, info.creator, CT_SIZE);

		hfs_ent->type[CT_SIZE] = hfs_ent->creator[CT_SIZE] = '\0';

		hfs_ent->fdflags = d_getw(info.flags);

		s = info.sname;
		e = info.ext;
		k = keyname;

		/* short (Unix) name is stored in PC format, so needs
		   to be mangled a bit */

		/* name part */
		for(i=0;i<8;i++,s++,k++) {
		    if(*s == ' ')
			break;
		    else
			*k = *s;
		}

		/* extension - if it exists */
		if (strncmp(info.ext, "   ", 3)) {
		    *k = '.';
		    k++;
		    for(i=0;i<3;i++,e++,k++) {
			if(*e == ' ')
			    break;
			else
			    *k = *e;
		    }
		}
		*k = '\0';

		hfs_info1->keyname = strdup(keyname);
	    }

	    /* each record is FE_SIZE long, and there are FE_NUM
		per each "cluster size", so we may need to skip the padding */
	    if (++c == fe_num) {
		c = 0;
	        fseek(fp, fe_pad, 1);
	    }
	}
	fclose (fp);

	return (hfs_info);
}

/*
**	get_hfs_sgi_info: read in the whole finderinfo for a SGI (XINET)
**		directory - saves on reading this many times for each
**		file.
*/
struct hfs_info *
get_hfs_sgi_info(struct hfs_info *hfs_info, char *name)
{
	FILE	*fp;
	sgi_info info;
	struct hfs_info *hfs_info1 = NULL;
	hfsdirent *hfs_ent;

	if ((fp = fopen(name, "rb")) == NULL)
	    return(NULL);

	while(fread(&info, 1, SGI_SIZE, fp) != 0) {

	    hfs_info1 = (struct hfs_info *)e_malloc(sizeof(struct hfs_info));
	    /* add thsi entry to the list */
	    hfs_info1->next = hfs_info;
	    hfs_info = hfs_info1;

	    hfs_ent = &hfs_info1->hfs_ent;

	    /* get the bits we need - ignore [cm]time for the moment */
	    cstrncpy(hfs_ent->name, info.name, HFS_MAX_FLEN);

	    strncpy(hfs_ent->type, info.type, CT_SIZE);
	    strncpy(hfs_ent->creator, info.creator, CT_SIZE);

	    hfs_ent->type[CT_SIZE] = hfs_ent->creator[CT_SIZE] = '\0';

	    /* don't know about flags at the moment */
	/*  hfs_ent->fdflags = d_getw((unsigned char *)&info.flags); */

	    /* use the HFS name as the key */
	    hfs_info1->keyname = hfs_ent->name;

	}
	fclose (fp);

	return (hfs_info);
}

/*
**	del_hfs_info: delete the info list and recover memory
*/
void
del_hfs_info(struct hfs_info *hfs_info)
{
	struct hfs_info	*hfs_info1;

	while (hfs_info) {
	    hfs_info1 = hfs_info;
	    hfs_info = hfs_info->next;

	    /* key may be the same as the HFS name - so don't free it */
	    *hfs_info1->hfs_ent.name = '\0';
	    if (*hfs_info1->keyname)
		free(hfs_info1->keyname);
	    free(hfs_info1);
	}
}

/*
**	match_key: find the correct hfs_ent using the Unix filename
**		as the key
*/
hfsdirent *
match_key(struct hfs_info *hfs_info, char *key)
{
	while (hfs_info) {
	    if (!strcasecmp(key, hfs_info->keyname))
		return (&hfs_info->hfs_ent);
	    hfs_info = hfs_info->next;
	}

	return (NULL);
}

/*
**	get_fe_dir: get PC Exchange directory name
**
**	base on probing with od ...
*/
int
get_fe_dir(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	struct hfs_info *hfs_info;
	hfsdirent	*hfs_ent;

	/* cached finderinfo stored with parent directory */
	hfs_info = s_entry->filedir->hfs_info;

	/* if we have no cache, then make one and store it */
	if (hfs_info == NULL) {
	    if ((hfs_info = get_hfs_fe_info(hfs_info, hname)) == NULL)
		ret = TYPE_NONE;
	    else
		s_entry->filedir->hfs_info = hfs_info;
	}

	if (ret != TYPE_NONE) {
	    /* see if we can find the details of this file */
	    if ((hfs_ent = match_key(hfs_info, dname)) != NULL) {
		strcpy(s_entry->hfs_ent->name, hfs_ent->name);
		return (ret);
	    }
	}

	/* can't find the entry, so use the Unix name */
	hstrncpy(s_entry->hfs_ent->name, dname, HFS_MAX_FLEN);

	return(TYPE_NONE);
}

/*
**	get_fe_info: get PC Exchange file details.
**
**	base on probing with od and details from Mark Weinstein
**	<mrwesq@earthlink.net>
*/
int
get_fe_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	struct hfs_info *hfs_info;
	hfsdirent	*hfs_ent;

	/* cached finderinfo stored with parent directory */
	hfs_info = s_entry->filedir->hfs_info;

	/* if we have no cache, then make one and store it */
	if (hfs_info == NULL) {
	    if ((hfs_info = get_hfs_fe_info(hfs_info, hname)) == NULL)
		ret = TYPE_NONE;
	    else
		s_entry->filedir->hfs_info = hfs_info;
	}

	if (ret != TYPE_NONE) {
	    char  *dn = dname;
#ifdef _WIN32_TEST
	    /* may have a problem here - v2.2 has long filenames,
	       but we need to key on the short filename, so we need
	       do go a bit of win32 stuff ... */

	    char  sname[1024], lname[1024];

	    cygwin32_conv_to_full_win32_path(s_entry->whole_name, lname);

	    if (GetShortPathName(lname, sname, sizeof(sname))) {
		if (dn = strrchr(sname, '\\'))
		    dn++;
		else
		    dn = sname;
	    }
#endif /* _WIN32 */

	    /* see if we can find the details of this file */
	    if ((hfs_ent = match_key(hfs_info, dn)) != NULL) {
		strcpy(s_entry->hfs_ent->name, hfs_ent->name);
		strcpy(s_entry->hfs_ent->type, hfs_ent->type);
		strcpy(s_entry->hfs_ent->creator, hfs_ent->creator);
		/* clear HFS_FNDR_HASBEENINITED flag */
		s_entry->hfs_ent->fdflags = hfs_ent->fdflags & 0xfeff;
		return (ret);
	    }
	}

	/* no entry found - use extension mapping */
	if (verbose > 2) {
	    fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
	        s_entry->whole_name, hfs_types[ret].desc);
	}

	ret = get_none_info(hname, dname, s_entry, TYPE_NONE);

	return(TYPE_NONE);
}

/*
**	get_sgi_dir: get SGI (XINET) HFS directory name
**
**	base on probing with od ...
*/
int
get_sgi_dir(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	struct hfs_info *hfs_info;
	hfsdirent	*hfs_ent;

	/* cached finderinfo stored with parent directory */
	hfs_info = s_entry->filedir->hfs_info;

	/* if we haven't got a cache, then make one */
	if (hfs_info == NULL) {
	    if ((hfs_info = get_hfs_sgi_info(hfs_info, hname)) == NULL)
		ret = TYPE_NONE;
	    else
		s_entry->filedir->hfs_info = hfs_info;
	}

	/* find the matching entry in the cache */
	if (ret != TYPE_NONE) {
	    /* key is (hopefully) the real Mac name */
	    cstrncpy(tmp, dname, strlen(dname));
	    if ((hfs_ent = match_key(hfs_info, tmp)) != NULL) {
		strcpy(s_entry->hfs_ent->name, hfs_ent->name);
		return (ret);
	    }
	}

	/* no entry found - use Unix name */
	hstrncpy(s_entry->hfs_ent->name, dname, HFS_MAX_FLEN);

	return(TYPE_NONE);
}

/*
**	get_sgi_info: get SGI (XINET) HFS finder info
**
**	base on probing with od ...
*/
int
get_sgi_info(char *hname, char *dname, dir_ent *s_entry, int ret)
/* char		*hname				whole path */
/* char		*dname				this dir name */
/* dir_ent	*s_entry			directory entry */
{
	struct hfs_info *hfs_info;
	hfsdirent	*hfs_ent;

	/* cached finderinfo stored with parent directory */
	hfs_info = s_entry->filedir->hfs_info;

	/* if we haven't got a cache, then make one */
	if (hfs_info == NULL) {
	    if ((hfs_info = get_hfs_sgi_info(hfs_info, hname)) == NULL)
		ret = TYPE_NONE;
	    else
		s_entry->filedir->hfs_info = hfs_info;
	}

	if (ret != TYPE_NONE) {
	    /* tmp is the same as hname here, but we don't need hname
	       anymore in this function  ...  see if we can find the
	       details of this file using the Unix name as the key */
	    cstrncpy(tmp, dname, strlen(dname));
	    if ((hfs_ent = match_key(hfs_info, tmp)) != NULL) {
		strcpy(s_entry->hfs_ent->name, hfs_ent->name);
		strcpy(s_entry->hfs_ent->type, hfs_ent->type);
		strcpy(s_entry->hfs_ent->creator, hfs_ent->creator);
	/*	s_entry->hfs_ent->fdflags = hfs_ent->fdflags; */
		return (ret);
	    }
	}

	/* no entry found, so try file extension */
	if (verbose > 2) {
	    fprintf(stderr, "warning: %s doesn't appear to be a %s file\n",
	        s_entry->whole_name, hfs_types[ret].desc);
	}

	ret = get_none_info(hname, dname, s_entry, TYPE_NONE);

	return(TYPE_NONE);
}

/*
**	get_hfs_itype: get the type of HFS info for a file
*/
int
get_hfs_itype(char *wname, char *dname, char *htmp)
{
	int	wlen, i;

	wlen = strlen(wname) - strlen(dname);

	/* search through the known types looking for matches */
	for (i=1;i<hfs_num;i++) {
	    /* skip the ones that we don't care about */
	    if ((hfs_types[i].flags & 0x1) || *(hfs_types[i].info) == TYPE_NONE)
		continue;

	    strcpy(htmp, wname);

	    sprintf(htmp+wlen, "%s%s", hfs_types[i].info, 
		(hfs_types[i].flags & 0x2) ? "" : dname);
	    if (!access(tmp, R_OK))
		return (hfs_types[i].type);
	}

	return (TYPE_NONE);
}

/*
**	get_hfs_dir: set the HFS directory name
*/
int
get_hfs_dir(char *wname, char *dname, dir_ent *s_entry)
{
	int	type;

	/* get the HFS file type from the info file (if it exists) */
	type = get_hfs_itype(wname, dname, tmp);

	/* try to get the required info */
	type = (*(hfs_types[type].get_dir))(tmp, dname, s_entry, type);

	return (type);
}

/*
**	get_hfs_info: set the HFS info for a file
*/
int
get_hfs_info(char *wname, char *dname, dir_ent *s_entry)
{
	int	type, wlen, i;

	wlen = strlen(wname) - strlen(dname);

	/* we may already know the type of Unix/HFS file - so process */
	if (s_entry->hfs_type != TYPE_NONE) {

/*
	    i = 0;
	    for(type=1;i<hfs_num;type++) {
		if (s_entry->hfs_type == hfs_types[type].type) {
		    i = type;
		    break;
		}
	    }
*/
	    type = s_entry->hfs_type;

	    strcpy(tmp, wname);
	    sprintf(tmp+wlen, "%s%s", hfs_types[type].info,
		(hfs_types[type].flags & 0x2) ? "" : dname);
	    type = (*(hfs_types[type].get_info))(tmp, dname, s_entry, type);

	    /* if everything is as expected, then return */
	    if (s_entry->hfs_type == type)
		return(type);
	}

	/* we don't know what type we have so, find out */
	for (i=1;i<hfs_num;i++) {
	    if ((hfs_types[i].flags & 0x1) || *(hfs_types[i].info) == TYPE_NONE) 
		continue;

	    strcpy(tmp, wname);

	    sprintf(tmp+wlen, "%s%s", hfs_types[i].info, 
		(hfs_types[i].flags & 0x2) ? "" : dname);

	    /* if the file exists - and not a type we've already tried */
	    if (!access(tmp, R_OK) && i != s_entry->hfs_type) {
		type = (*(hfs_types[i].get_info))(tmp, dname, s_entry, i);
		s_entry->hfs_type = type;
		return (type);
	    }
	}

	/* nothing found, so just a Unix file */
	type = (*(hfs_types[TYPE_NONE].get_info))(wname, dname, s_entry, TYPE_NONE);

	return (type);
}

/*
**	get_hfs_rname: set the name of the Unix rsrc file for a file
*/
int
get_hfs_rname(char *wname, char *dname, char *rname)
{
	int	wlen, type, i;
	int	p_fd = -1;

	wlen = strlen(wname) - strlen(dname);

	/* try to find what sort of Unix HFS file type we have */
	for (i=1;i<hfs_num;i++) {
	    /* skip if don't want to probe the files - (default) */
	    if (hfs_types[i].flags & 0x1)
		continue;

	    strcpy(rname, wname);

	    /* if we have a different info file, the find out it's type */
	    if (*(hfs_types[i].rsrc)) {
		sprintf(rname+wlen, "%s%s", hfs_types[i].rsrc, dname);
		if (!access(rname, R_OK))
		    return (hfs_types[i].type);
	    }
	    else {
		/* if we are probing, then have a look at the contents to
		   find type */
		if (p_fd < 0) {
		    /* open file, if not already open */
		    if((p_fd = open(wname, O_RDONLY | O_BINARY)) < 0) {
			/* can't open it, then give up */
			return (TYPE_NONE);
		    } else {
			if((p_num = read(p_fd, p_buf, sizeof(p_buf))) <= 0) {
			/* can't read, or zero length - give up */
			    close(p_fd);
			    return(TYPE_NONE);
			}
			/* get file pointer and close file */
			p_fp = fdopen(p_fd, "rb");
			close(p_fd);
			if(p_fp == NULL)
			    return(TYPE_NONE);
		    }
		}
		/* call routine to do the work - use the given dname as
		   this is the name we may use on the CD */
		type = (*(hfs_types[i].get_info))(rname, dname, 0, i);
		if (type != 0) {
		    fclose(p_fp);
		    return (type);
		}
		if (p_fp) {
		    /* close file - just use contents of buffer next time */
		    fclose(p_fp);
		    p_fp = NULL;
		}
	    }
	}

	return (0);
}

/*
**	hfs_exclude: file/directory names that hold finder/resource
**		     information that we want to exclude from the tree.
**		     These files/directories are processed later ...
*/
int
hfs_exclude(char *d_name)
{
    /* we don't exclude "." and ".." */
    if (!strcmp(d_name,"."))
      return 0;
    if (!strcmp(d_name,".."))
      return 0;

    /* do not add the following to our list of dir entries */
    if (DO_CAP & hselect) {
      /* CAP */
      if(!strcmp(d_name,".finderinfo"))
	return 1;
      if(!strcmp(d_name,".resource"))
	return 1;
      if(!strcmp(d_name,".ADeskTop"))
	return 1;
      if(!strcmp(d_name,".IDeskTop"))
	return 1;
      if(!strcmp(d_name,"Network Trash Folder"))
	return 1;
      /* special case when HFS volume is mounted using Linux's hfs_fs
	 Brad Midgley <brad@pht.com> */
      if(!strcmp(d_name,".rootinfo"))
	return 1;
    }

    if (DO_ESH & hselect) {
      /* Helios EtherShare files */
      if(!strcmp(d_name,".rsrc"))
	return 1;
      if(!strcmp(d_name,".Desktop"))
	return 1;
      if(!strcmp(d_name,".DeskServer"))
	return 1;
      if(!strcmp(d_name,".Label"))
	return 1;
    }

    if (DO_DBL & hselect) {
      /* Apple Double */
      if (*d_name == '%')
	return 1;
    }

    if (DO_NETA & hselect) {
      if(!strcmp(d_name,".AppleDouble"))
	return 1;
      if(!strcmp(d_name,".AppleDesktop"))
	return 1;
    }

    if ((DO_FEU & hselect) || ( DO_FEL & hselect)) {
	/* PC Exchange */
      if(!strcmp(d_name,"RESOURCE.FRK"))
	return 1;
      if(!strcmp(d_name,"FINDER.DAT"))
	return 1;
      if(!strcmp(d_name,"DESKTOP"))
	return 1;
      if(!strcmp(d_name,"FILEID.DAT"))
	return 1;
      if(!strcmp(d_name,"resource.frk"))
	return 1;
      if(!strcmp(d_name,"finder.dat"))
	return 1;
      if(!strcmp(d_name,"desktop"))
	return 1;
      if(!strcmp(d_name,"fileid.dat"))
	return 1;
    }

    if (DO_SGI | hselect) {
      /* SGI */
      if(!strcmp(d_name,".HSResource"))
	return 1;
      if(!strcmp(d_name,".HSancillary"))
	return 1;
    }

    return 0;
}
/*
**	print_hfs_info: print info about the HFS files.
**
*/
void
print_hfs_info(dir_ent *s_entry)
{
	fprintf(stderr,"Name: %s\n",s_entry->whole_name);
	fprintf(stderr,"\tFile type: %s\n",hfs_types[s_entry->hfs_type].desc);
	fprintf(stderr,"\tHFS Name: %s\n",s_entry->hfs_ent->name);
	fprintf(stderr,"\tISO Name: %s\n",s_entry->isorec.name);
	fprintf(stderr,"\tCREATOR: %s\n",s_entry->hfs_ent->creator);
	fprintf(stderr,"\tTYPE:	%s\n", s_entry->hfs_ent->type);
}


/*
**	hfs_init: sets up the mapping list from the afpfile as well
**		 the default mapping (with or without) an afpfile
*/
void
hfs_init(char *name, unsigned short fdflags, int probe, int nomacfiles,
	unsigned int hfs_select)
#if 0
   char		*name;				/* afpfile name */
   u_short	*fdflags;			/* default finder flags */
   int		probe;				/* probe flag */
   int		nomacfiles;			/* don't look for mac files */
   u_int	hfs_select			/* select certain mac files */
#endif
{
	FILE	*fp;				/* File pointer */
	int	count = NUMMAP;			/* max number of entries */
	char	buf[MAXPATHLEN];		/* working buffer */
	afpmap	*amap;				/* mapping entry */
	char	*c, *t, *e;
	int	i;

	/* setup number of Unix/HFS filetype - we may wish to not bother */
	if (nomacfiles)
	    hfs_num = 0;
	else
	    hfs_num = sizeof(hfs_types)/sizeof(struct hfs_type);

	/* we may want to probe all files */
	if (probe || hfs_select)
	    for(i=0;i<hfs_num;i++)
		hfs_types[i].flags &= ~1;	/* 0xfffffffe */

	/* if we have selected certain types of Mac/Unix files, then
	   turn off the filetype */
	if (hfs_select)
	    for(i=1;i<hfs_num;i++)
		if (!((1 << i) & hfs_select))
		    hfs_types[i].flags |= 0x1;

	/* save what types have been selected (set all if none have) */
	if (hfs_select)
	    hselect = hfs_select;
	else
	    hselect = ~0;

#ifdef DEBUG
	for(i=0;i<hfs_num;i++)
	    fprintf(stderr,"type = %d flags = %d\n", i, hfs_types[i].flags);
#endif /* DEBUG */

	/* min length set to max to start with */
	mlen = MAXPATHLEN;

	/* initialise magic file */
	if(magic_file && init_magic(magic_file) != 0)
	    errx(1, "unable to open magic file");

	/* set defaults */
	map_num = last_ent = 0;

	/* allocate memory for the default entry */
	if((defmap = (afpmap *)malloc(sizeof(afpmap))) == NULL)
	    errx(1, "not enough memory");

	/* set default values */
	defmap->extn = DEFMATCH;

	/* make sure creator and type are 4 chars long */
	strcpy(defmap->type, BLANK);
	strcpy(defmap->creator, BLANK);
	
	e = deftype;
	t = defmap->type;

	while(*e && (e - deftype) < CT_SIZE)
	    *t++ = *e++;

	e = defcreator;
	c = defmap->creator;

	while(*e && (e - defcreator) < CT_SIZE)
	    *c++ = *e++;

	/* length is not important here */
	defmap->elen = 0;

	/* no flags */
	defmap->fdflags = fdflags;

	/* no afpfile - no mappings */
	if (*name == '\0') {
	    map = NULL;
	    return;
	}

	if((fp = fopen(name,"r")) == NULL)
	    err(1, "unable to open mapping file: %s", name);

	if((map = (afpmap **)malloc(NUMMAP * sizeof(afpmap *))) == NULL)
	    errx(1, "not enough memory");

	/* read afpfile line by line */
	while(fgets(buf, MAXPATHLEN, fp) != NULL) {
	    /* ignore any comment lines */
	    c = tmp;
	    *c = '\0';
	    if (sscanf(buf,"%1s", c) == EOF || *c == '#')
		continue;

	    /* increase list size if needed */
	    if (map_num == count) {
		count += NUMMAP;
		map = (afpmap **)realloc(map, count * sizeof(afpmap *));
		if (map == NULL)
		    errx(1, "not enough memory");
	    }

	    /* allocate memory for this entry */
	    if((amap = (afpmap *)malloc(sizeof(afpmap))) == NULL)
		errx(1, "not enough memory");

	    t = amap->type;
	    c = amap->creator;

	    /* extract the info */
	    if(sscanf(buf, "%s%*s%*1s%c%c%c%c%*1s%*1s%c%c%c%c%*1s",
		    tmp, c, c+1, c+2, c+3, t, t+1, t+2, t+3) != 9) {
  		fprintf(stderr,"error scanning afpfile %s - continuing", name);
		free(amap);
		continue;
	    }

	    /* copy the extension found */
	    if ((amap->extn = (char *)strdup(tmp)) == NULL)
		errx(1, "not enough memory");

	    /* set end-of-string */
	    *(t+4) = *(c+4) = '\0';

	    /* find the length of the extension */
	    amap->elen = strlen(amap->extn);

	    /* set flags */
	    amap->fdflags = fdflags;

	    /* see if we have the default creator/type */
	    if(!strcmp(amap->extn, DEFMATCH)) {
		/* get rid of the old default */
		free(defmap);
		/* make this the default */
		defmap = amap;
		continue;
	    }

	    /* update the smallest extension length */
	    mlen = MIN(mlen, amap->elen);

	    /* add entry to the list */
	    map[map_num++] = amap;

	}

	/* free up some memory */
	if (map_num != count) {
	    map = (afpmap **)realloc(map, map_num * sizeof(afpmap *));
	    if (map == NULL)
		errx(1, "not enough memory");
	}

}

/*
**	map_ext: map a files extension with the list to get type/creator
*/
void
map_ext(char *name, char **type, char **creator, unsigned short *fdflags,
	char *whole_name)
#if 0
   char		*name;				/* filename */
   char		**type;				/* set type */
   char		**creator;			/* set creator */
   u_short	*fdflags;			/* set finder flags */
#endif
{
	int	i;				/* loop counter */
	int	len;				/* filename length */
	afpmap	*amap;				/* mapping entry */
	char	*ret;

	/* we don't take fdflags from the map or magic file */
	*fdflags = defmap->fdflags;

	/* if we have a magic file and we want to search it first, then
	   try to get a match */
	if (magic_file && hfs_last == MAP_LAST) {
	    ret = get_magic_match(whole_name);

	    if (ret) {
		if (sscanf(ret, "%4s%4s", tmp_creator, tmp_type) == 2) {
		    *type = tmp_type;
		    *creator = tmp_creator;
		    return;
		}
	    }
	}

	len = strlen(name);

	/* have an afpfile and filename if long enough */
	if(map && len >= mlen) {
	    /* search through the list - we start where we left
	       off last time in case this file is of the same type
	       as the last one */
	    for(i=0;i<map_num;i++) {
		amap = map[last_ent];

		/* compare the end of the filename */
/*		if (!strcmp((name + len - amap->elen), amap->extn)) { */
		if (!strcasecmp((name + len - amap->elen), amap->extn)) {
		    /* set the required info */
		    *type = amap->type;
		    *creator = amap->creator;
		    *fdflags = amap->fdflags;
		    return;
		}
		/* move on to the next entry - wrapping round if neccessary */
		last_ent++;
		last_ent %= map_num;
	    }
	}

	/* if no matches are found, file name too short, or no 
	   afpfile, then take defaults */
	*type = defmap->type;
	*creator = defmap->creator;

	/* if we have a magic file and we haven't searched yet, then try
	   to get a match */
	if (magic_file && hfs_last == MAG_LAST) {
	    ret = get_magic_match(whole_name);

	    if (ret) {
		if (sscanf(ret, "%4s%4s", tmp_creator, tmp_type) == 2) {
		    *type = tmp_type;
		    *creator = tmp_creator;
		}
	    }
	}
}

void
delete_rsrc_ent(dir_ent *s_entry)
{
	dir_ent	*s_entry1 = s_entry->next;

	if (s_entry1 == NULL)
	    return;

	s_entry->next = s_entry1->next;
	s_entry->assoc = NULL;

	free(s_entry1->name);
	free(s_entry1->whole_name);

	free(s_entry1);
}

void
clean_hfs()
{
	if (map)
	    free(map);

	if (defmap)
	    free(defmap);

	if (magic_file)
	    clean_magic();
}

#else
#include <stdio.h>
#endif /* APPLE_HYB */
