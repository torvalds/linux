
#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpcsvc/sm_inter.h>


/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

struct sm_stat_res *
sm_stat_1(argp, clnt)
	struct sm_name *argp;
	CLIENT *clnt;
{
	static struct sm_stat_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, SM_STAT, xdr_sm_name, argp, xdr_sm_stat_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


struct sm_stat_res *
sm_mon_1(argp, clnt)
	struct mon *argp;
	CLIENT *clnt;
{
	static struct sm_stat_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, SM_MON, xdr_mon, argp, xdr_sm_stat_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


struct sm_stat *
sm_unmon_1(argp, clnt)
	struct mon_id *argp;
	CLIENT *clnt;
{
	static struct sm_stat res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, SM_UNMON, xdr_mon_id, argp, xdr_sm_stat, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


struct sm_stat *
sm_unmon_all_1(argp, clnt)
	struct my_id *argp;
	CLIENT *clnt;
{
	static struct sm_stat res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, SM_UNMON_ALL, xdr_my_id, argp, xdr_sm_stat, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


void *
sm_simu_crash_1(argp, clnt)
	void *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, SM_SIMU_CRASH, xdr_void, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


int main(int argc, char **argv)
{
  CLIENT *cli;
  char dummy;
  void *out;
  struct mon mon;

  if (argc < 2)
  {
    fprintf(stderr, "usage: test <hostname> | crash\n");
    fprintf(stderr, "always talks to statd at localhost\n");
    exit(1);
  }

  printf("Creating client for localhost\n" );
  cli = clnt_create("localhost", SM_PROG, SM_VERS, "udp");
  if (!cli)
  {
    printf("Failed to create client\n");
    exit(1);
  }

  mon.mon_id.mon_name = argv[1];
  mon.mon_id.my_id.my_name = argv[1];
  mon.mon_id.my_id.my_prog = SM_PROG;
  mon.mon_id.my_id.my_vers = SM_VERS;
  mon.mon_id.my_id.my_proc = 1;	/* have it call sm_stat() !!!	*/

  if (strcmp(argv[1], "crash"))
  {
    /* Hostname given		*/
    struct sm_stat_res *res;
    if (res = sm_mon_1(&mon, cli))
    {
      printf("Success!\n");
    }
    else
    {
      printf("Fail\n");  
    }
  }
  else
  {
    if (out = sm_simu_crash_1(&dummy, cli))
    {
      printf("Success!\n");
    }
    else
    {
      printf("Fail\n");  
    }
  }

  return 0;
}
