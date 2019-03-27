/*

This code is not copyright, and is placed in the public domain. Feel free to
use and modify. Please send modifications and/or suggestions + bug fixes to

        Klas Heggemann <klas@nada.kth.se>

*/

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include "bootparam_prot.h"
#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
extern int debug, dolog;
extern in_addr_t route_addr;
extern char *bootpfile;

#define MAXLEN 800

struct hostent *he;
static char buffer[MAXLEN];
static char hostname[MAX_MACHINE_NAME];
static char askname[MAX_MACHINE_NAME];
static char path[MAX_PATH_LEN];
static char domain_name[MAX_MACHINE_NAME];

int getthefile(char *, char *, char *, int);
int checkhost(char *, char *, int);

bp_whoami_res *
bootparamproc_whoami_1_svc(whoami, req)
bp_whoami_arg *whoami;
struct svc_req *req;
{
  in_addr_t haddr;
  static bp_whoami_res res;
  if (debug)
    fprintf(stderr,"whoami got question for %d.%d.%d.%d\n",
	    255 &  whoami->client_address.bp_address_u.ip_addr.net,
	    255 & whoami->client_address.bp_address_u.ip_addr.host,
	    255 &  whoami->client_address.bp_address_u.ip_addr.lh,
	    255 &  whoami->client_address.bp_address_u.ip_addr.impno);
  if (dolog)
    syslog(LOG_NOTICE, "whoami got question for %d.%d.%d.%d\n",
	    255 &  whoami->client_address.bp_address_u.ip_addr.net,
	    255 & whoami->client_address.bp_address_u.ip_addr.host,
	    255 &  whoami->client_address.bp_address_u.ip_addr.lh,
	    255 &  whoami->client_address.bp_address_u.ip_addr.impno);

  bcopy((char *)&whoami->client_address.bp_address_u.ip_addr, (char *)&haddr,
	sizeof(haddr));
  he = gethostbyaddr((char *)&haddr,sizeof(haddr),AF_INET);
  if ( ! he ) goto failed;

  if (debug) warnx("this is host %s", he->h_name);
  if (dolog) syslog(LOG_NOTICE,"This is host %s\n", he->h_name);

  strncpy(askname, he->h_name, sizeof(askname));
  askname[sizeof(askname)-1] = 0;

  if (checkhost(askname, hostname, sizeof hostname) ) {
    res.client_name = hostname;
    getdomainname(domain_name, MAX_MACHINE_NAME);
    res.domain_name = domain_name;

    if (  res.router_address.address_type != IP_ADDR_TYPE ) {
      res.router_address.address_type = IP_ADDR_TYPE;
      bcopy( &route_addr, &res.router_address.bp_address_u.ip_addr, sizeof(in_addr_t));
    }
    if (debug) fprintf(stderr,
		       "Returning %s   %s    %d.%d.%d.%d\n",
		       res.client_name,
		       res.domain_name,
		       255 &  res.router_address.bp_address_u.ip_addr.net,
		       255 & res.router_address.bp_address_u.ip_addr.host,
		       255 &  res.router_address.bp_address_u.ip_addr.lh,
		       255 & res.router_address.bp_address_u.ip_addr.impno);
    if (dolog) syslog(LOG_NOTICE,
		       "Returning %s   %s    %d.%d.%d.%d\n",
		       res.client_name,
		       res.domain_name,
		       255 &  res.router_address.bp_address_u.ip_addr.net,
		       255 & res.router_address.bp_address_u.ip_addr.host,
		       255 &  res.router_address.bp_address_u.ip_addr.lh,
		       255 & res.router_address.bp_address_u.ip_addr.impno);

    return(&res);
  }
 failed:
  if (debug) warnx("whoami failed");
  if (dolog) syslog(LOG_NOTICE,"whoami failed\n");
  return(NULL);
}


bp_getfile_res *
  bootparamproc_getfile_1_svc(getfile, req)
bp_getfile_arg *getfile;
struct svc_req *req;
{
  char *where;
  static bp_getfile_res res;

  if (debug)
    warnx("getfile got question for \"%s\" and file \"%s\"",
	    getfile->client_name, getfile->file_id);

  if (dolog)
    syslog(LOG_NOTICE,"getfile got question for \"%s\" and file \"%s\"\n",
	    getfile->client_name, getfile->file_id);

  he = NULL;
  he = gethostbyname(getfile->client_name);
  if (! he ) goto failed;

  strncpy(askname, he->h_name, sizeof(askname));
  askname[sizeof(askname)-1] = 0;

  if (getthefile(askname, getfile->file_id,buffer,sizeof(buffer))) {
    if ( (where = strchr(buffer,':')) ) {
      /* buffer is re-written to contain the name of the info of file */
      strncpy(hostname, buffer, where - buffer);
      hostname[where - buffer] = '\0';
      where++;
      strcpy(path, where);
      he = gethostbyname(hostname);
      if ( !he ) goto failed;
      bcopy( he->h_addr, &res.server_address.bp_address_u.ip_addr, 4);
      res.server_name = hostname;
      res.server_path = path;
      res.server_address.address_type = IP_ADDR_TYPE;
    }
    else { /* special for dump, answer with null strings */
      if (!strcmp(getfile->file_id, "dump")) {
	res.server_name = "";
	res.server_path = "";
        res.server_address.address_type = IP_ADDR_TYPE;
	bzero(&res.server_address.bp_address_u.ip_addr,4);
      } else goto failed;
    }
    if (debug)
      fprintf(stderr, "returning server:%s path:%s address: %d.%d.%d.%d\n",
	     res.server_name, res.server_path,
	     255 &  res.server_address.bp_address_u.ip_addr.net,
	     255 & res.server_address.bp_address_u.ip_addr.host,
	     255 &  res.server_address.bp_address_u.ip_addr.lh,
	     255 & res.server_address.bp_address_u.ip_addr.impno);
    if (dolog)
      syslog(LOG_NOTICE, "returning server:%s path:%s address: %d.%d.%d.%d\n",
	     res.server_name, res.server_path,
	     255 &  res.server_address.bp_address_u.ip_addr.net,
	     255 & res.server_address.bp_address_u.ip_addr.host,
	     255 &  res.server_address.bp_address_u.ip_addr.lh,
	     255 & res.server_address.bp_address_u.ip_addr.impno);
    return(&res);
  }
  failed:
  if (debug) warnx("getfile failed for %s", getfile->client_name);
  if (dolog) syslog(LOG_NOTICE,
		    "getfile failed for %s\n", getfile->client_name);
  return(NULL);
}

/*    getthefile return 1 and fills the buffer with the information
      of the file, e g "host:/export/root/client" if it can be found.
      If the host is in the database, but the file is not, the buffer
      will be empty. (This makes it possible to give the special
      empty answer for the file "dump")   */

int
getthefile(askname,fileid,buffer,blen)
char *askname;
char *fileid, *buffer;
int blen;
{
  FILE *bpf;
  char  *where;
#ifdef YP
  static char *result;
  int resultlen;
  static char *yp_domain;
#endif

  int ch, pch, fid_len, res = 0;
  int match = 0;
#define INFOLEN 1343
  _Static_assert(INFOLEN >= MAX_FILEID + MAX_PATH_LEN+MAX_MACHINE_NAME + 3,
		  "INFOLEN isn't large enough");
  char info[INFOLEN + 1];

  bpf = fopen(bootpfile, "r");
  if ( ! bpf )
    errx(1, "no %s", bootpfile);

  /* XXX see comment below */
  while ( fscanf(bpf, "%255s", hostname) > 0  && !match ) {
    if ( *hostname != '#' ) { /* comment */
      if ( ! strcmp(hostname, askname) ) {
	match = 1;
      } else {
	he = gethostbyname(hostname);
	if (he && !strcmp(he->h_name, askname)) match = 1;
      }
    }
    if (*hostname == '+' ) { /* NIS */
#ifdef YP
      if (yp_get_default_domain(&yp_domain)) {
	 if (debug) warn("NIS");
	 return(0);
      }
      if (yp_match(yp_domain, "bootparams", askname, strlen(askname),
		&result, &resultlen))
	return (0);
      if (strstr(result, fileid) == NULL) {
	buffer[0] = '\0';
      } else {
	snprintf(buffer, blen,
		"%s",strchr(strstr(result,fileid), '=') + 1);
	if (strchr(buffer, ' ') != NULL)
	  *(char *)(strchr(buffer, ' ')) = '\0';
      }
      if (fclose(bpf))
        warnx("could not close %s", bootpfile);
      return(1);
#else
      if (fclose(bpf))
        warnx("could not close %s", bootpfile);
      return(0);	/* ENOTSUP */
#endif
    }
    /* skip to next entry */
    if ( match ) break;
    pch = ch = getc(bpf);
    while ( ! ( ch == '\n' && pch != '\\') && ch != EOF) {
      pch = ch; ch = getc(bpf);
    }
  }

  /* if match is true we read the rest of the line to get the
     info of the file */

  if (match) {
    fid_len = strlen(fileid);
#define AS_FORMAT(d)	"%" #d "s"
#define REXPAND(d) AS_FORMAT(d)	/* Force another preprocessor expansion */
    while ( ! res && (fscanf(bpf, REXPAND(INFOLEN), info)) > 0) {
      ch = getc(bpf);                                /* and a character */
      if ( *info != '#' ) {                          /* Comment ? */
	if (! strncmp(info, fileid, fid_len) && *(info + fid_len) == '=') {
	  where = info + fid_len + 1;
	  if ( isprint( *where )) {
	    strcpy(buffer, where);                   /* found file */
	    res = 1; break;
	  }
	} else {
	  while (isspace(ch) && ch != '\n') ch = getc(bpf);
	                                             /* read to end of line */
	  if ( ch == '\n' ) {                        /* didn't find it */
	    res = -1; break;                         /* but host is there */
	  }
	  if ( ch == '\\' ) {                        /* more info */
	    ch = getc(bpf);                          /* maybe on next line */
	    if (ch == '\n') continue;                /* read it in next loop */
	    ungetc(ch, bpf); ungetc('\\',bpf); /* push the character(s) back */
	  } else ungetc(ch, bpf);              /* but who know what a `\` is */
	}                                      /* needed for. */
      } else break;                            /* a commented rest-of-line */
    }
  }
  if (fclose(bpf)) { warnx("could not close %s", bootpfile); }
  if ( res == -1) buffer[0] = '\0';            /* host found, file not */
  return(match);
}

/* checkhost puts the hostname found in the database file in
   the hostname-variable and returns 1, if askname is a valid
   name for a host in the database */

int
checkhost(askname, hostname, len)
char *askname;
char *hostname;
int len;
{
  int ch, pch;
  FILE *bpf;
  int res = 0;
#ifdef YP
  static char *result;
  int resultlen;
  static char *yp_domain;
#endif

/*  struct hostent *cmp_he;*/

  bpf = fopen(bootpfile, "r");
  if ( ! bpf )
    errx(1, "no %s", bootpfile);

  /* XXX there is no way in ISO C to specify the maximal length for a
     conversion in a variable way */
  while ( fscanf(bpf, "%254s", hostname) > 0 ) {
    if ( *hostname != '#' ) { /* comment */
      if ( ! strcmp(hostname, askname) ) {
        /* return true for match of hostname */
        res = 1;
        break;
      } else {
        /* check the alias list */
        he = NULL;
        he = gethostbyname(hostname);
        if (he && !strcmp(askname, he->h_name)) {
  	  res = 1;
	  break;
        }
      }
    }
    if (*hostname == '+' ) { /* NIS */
#ifdef YP
      if (yp_get_default_domain(&yp_domain)) {
	 if (debug) warn("NIS");
	 return(0);
      }
      if (!yp_match(yp_domain, "bootparams", askname, strlen(askname),
		&result, &resultlen)) {
        /* return true for match of hostname */
        he = NULL;
        he = gethostbyname(askname);
        if (he && !strcmp(askname, he->h_name)) {
  	  res = 1;
	  snprintf(hostname, len, "%s", he->h_name);
	}
      }
      if (fclose(bpf))
        warnx("could not close %s", bootpfile);
      return(res);
#else
      return(0);	/* ENOTSUP */
#endif
    }
    /* skip to next entry */
    pch = ch = getc(bpf);
    while ( ! ( ch == '\n' && pch != '\\') && ch != EOF) {
      pch = ch; ch = getc(bpf);
    }
  }
  if (fclose(bpf)) { warnx("could not close %s", bootpfile); }
  return(res);
}
