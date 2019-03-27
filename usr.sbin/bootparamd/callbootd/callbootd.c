/*

This code is not copyright, and is placed in the public domain. Feel free to
use and modify. Please send modifications and/or suggestions + bug fixes to

        Klas Heggemann <klas@nada.kth.se>

*/

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include "bootparam_prot.h"
#include <rpc/rpc.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <netdb.h>
#include <stdlib.h>


/* #define bp_address_u bp_address */
#include <stdio.h>
#include <string.h>

int broadcast;

char cln[MAX_MACHINE_NAME+1];
char dmn[MAX_MACHINE_NAME+1];
char path[MAX_PATH_LEN+1];
static void usage(void);
int printgetfile(bp_getfile_res *);
int printwhoami(bp_whoami_res *);

static bool_t
eachres_whoami(bp_whoami_res *resultp, struct sockaddr_in *raddr)
{
  struct hostent *he;

  he = gethostbyaddr((char *)&raddr->sin_addr.s_addr,4,AF_INET);
  printf("%s answered:\n", he ? he->h_name : inet_ntoa(raddr->sin_addr));
  printwhoami(resultp);
  printf("\n");
  return(0);
}

static bool_t
eachres_getfile(bp_getfile_res *resultp, struct sockaddr_in *raddr)
{
  struct hostent *he;

  he = gethostbyaddr((char *)&raddr->sin_addr.s_addr,4,AF_INET);
  printf("%s answered:\n", he ? he->h_name : inet_ntoa(raddr->sin_addr));
  printgetfile(resultp);
  printf("\n");
  return(0);
}


int
main(int argc, char **argv)
{
  char *server;

  bp_whoami_arg whoami_arg;
  bp_whoami_res *whoami_res, stat_whoami_res;
  bp_getfile_arg getfile_arg;
  bp_getfile_res *getfile_res, stat_getfile_res;


  long the_inet_addr;
  CLIENT *clnt;

  stat_whoami_res.client_name = cln;
  stat_whoami_res.domain_name = dmn;

  stat_getfile_res.server_name = cln;
  stat_getfile_res.server_path = path;

  if (argc < 3)
    usage();

  server = argv[1];
  if ( ! strcmp(server , "all") ) broadcast = 1;

  if ( ! broadcast ) {
    clnt = clnt_create(server,BOOTPARAMPROG, BOOTPARAMVERS, "udp");
    if ( clnt == NULL )
      errx(1, "could not contact bootparam server on host %s", server);
  }

  switch (argc) {
  case 3:
    whoami_arg.client_address.address_type = IP_ADDR_TYPE;
    the_inet_addr = inet_addr(argv[2]);
    if ( the_inet_addr == INADDR_NONE)
      errx(2, "bogus addr %s", argv[2]);
    bcopy(&the_inet_addr,&whoami_arg.client_address.bp_address_u.ip_addr,4);

    if (! broadcast ) {
      whoami_res = bootparamproc_whoami_1(&whoami_arg, clnt);
      printf("Whoami returning:\n");
      if (printwhoami(whoami_res)) {
	errx(1, "bad answer returned from server %s", server);
      } else
	exit(0);
     } else {
       (void)clnt_broadcast(BOOTPARAMPROG, BOOTPARAMVERS,
			       BOOTPARAMPROC_WHOAMI,
			       (xdrproc_t)xdr_bp_whoami_arg,
			       (char *)&whoami_arg,
			       (xdrproc_t)xdr_bp_whoami_res,
			       (char *)&stat_whoami_res,
			       (resultproc_t)eachres_whoami);
       exit(0);
     }

  case 4:

    getfile_arg.client_name = argv[2];
    getfile_arg.file_id = argv[3];

    if (! broadcast ) {
      getfile_res = bootparamproc_getfile_1(&getfile_arg,clnt);
      printf("getfile returning:\n");
      if (printgetfile(getfile_res)) {
	errx(1, "bad answer returned from server %s", server);
      } else
	exit(0);
    } else {
      (void)clnt_broadcast(BOOTPARAMPROG, BOOTPARAMVERS,
			       BOOTPARAMPROC_GETFILE,
			       (xdrproc_t)xdr_bp_getfile_arg,
			       (char *)&getfile_arg,
			       (xdrproc_t)xdr_bp_getfile_res,
			       (char *)&stat_getfile_res,
			       (resultproc_t)eachres_getfile);
      exit(0);
    }

  default:

    usage();
  }

}


static void
usage()
{
	fprintf(stderr,
		"usage: callbootd server procnum (IP-addr | host fileid)\n");
    exit(1);
}

int
printwhoami(res)
bp_whoami_res *res;
{
      if ( res) {
	printf("client_name:\t%s\ndomain_name:\t%s\n",
	     res->client_name, res->domain_name);
	printf("router:\t%d.%d.%d.%d\n",
	     255 &  res->router_address.bp_address_u.ip_addr.net,
	     255 & res->router_address.bp_address_u.ip_addr.host,
	     255 &  res->router_address.bp_address_u.ip_addr.lh,
	     255 & res->router_address.bp_address_u.ip_addr.impno);
	return(0);
      } else {
	warnx("null answer!!!");
	return(1);
      }
    }




int
printgetfile(res)
bp_getfile_res *res;
{
      if (res) {
	printf("server_name:\t%s\nserver_address:\t%s\npath:\t%s\n",
	       res->server_name,
	       inet_ntoa(*(struct in_addr *)&res->server_address.bp_address_u.ip_addr),
	       res->server_path);
	return(0);
      } else {
	warnx("null answer!!!");
	return(1);
      }
    }
