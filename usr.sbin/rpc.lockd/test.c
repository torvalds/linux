/*	$NetBSD: test.c,v 1.2 1997/10/18 04:01:21 lukem Exp $	*/

#include <sys/cdefs.h>
#include <rpc/rpc.h>
#include <rpcsvc/nlm_prot.h>
#ifndef lint
#if 0
static char sccsid[] = "from: @(#)nlm_prot.x 1.8 87/09/21 Copyr 1987 Sun Micro";
static char sccsid[] = "from: * @(#)nlm_prot.x	2.1 88/08/01 4.0 RPCSRC";
#else
__RCSID("$NetBSD: test.c,v 1.2 1997/10/18 04:01:21 lukem Exp $");
static const char rcsid[] = "$FreeBSD$";
#endif
#endif				/* not lint */

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 0, 0 };

nlm_testres *
nlm_test_1(argp, clnt)
	struct nlm_testargs *argp;
	CLIENT *clnt;
{
	static nlm_testres res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_TEST, xdr_nlm_testargs, argp, xdr_nlm_testres, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


nlm_res *
nlm_lock_1(argp, clnt)
	struct nlm_lockargs *argp;
	CLIENT *clnt;
{
	enum clnt_stat st;
	static nlm_res res;

	bzero((char *)&res, sizeof(res));
	if (st = clnt_call(clnt, NLM_LOCK, xdr_nlm_lockargs, argp, xdr_nlm_res, &res, TIMEOUT) != RPC_SUCCESS) {
		printf("clnt_call returns %d\n", st);
		clnt_perror(clnt, "humbug");
		return (NULL);
	}
	return (&res);
}


nlm_res *
nlm_cancel_1(argp, clnt)
	struct nlm_cancargs *argp;
	CLIENT *clnt;
{
	static nlm_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_CANCEL, xdr_nlm_cancargs, argp, xdr_nlm_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


nlm_res *
nlm_unlock_1(argp, clnt)
	struct nlm_unlockargs *argp;
	CLIENT *clnt;
{
	static nlm_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_UNLOCK, xdr_nlm_unlockargs, argp, xdr_nlm_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


nlm_res *
nlm_granted_1(argp, clnt)
	struct nlm_testargs *argp;
	CLIENT *clnt;
{
	static nlm_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_GRANTED, xdr_nlm_testargs, argp, xdr_nlm_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


void *
nlm_test_msg_1(argp, clnt)
	struct nlm_testargs *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_TEST_MSG, xdr_nlm_testargs, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_lock_msg_1(argp, clnt)
	struct nlm_lockargs *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_LOCK_MSG, xdr_nlm_lockargs, argp, xdr_void, NULL, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(clnt, "nlm_lock_msg_1");
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_cancel_msg_1(argp, clnt)
	struct nlm_cancargs *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_CANCEL_MSG, xdr_nlm_cancargs, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_unlock_msg_1(argp, clnt)
	struct nlm_unlockargs *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_UNLOCK_MSG, xdr_nlm_unlockargs, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_granted_msg_1(argp, clnt)
	struct nlm_testargs *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_GRANTED_MSG, xdr_nlm_testargs, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_test_res_1(argp, clnt)
	nlm_testres *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_TEST_RES, xdr_nlm_testres, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_lock_res_1(argp, clnt)
	nlm_res *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_LOCK_RES, xdr_nlm_res, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_cancel_res_1(argp, clnt)
	nlm_res *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_CANCEL_RES, xdr_nlm_res, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_unlock_res_1(argp, clnt)
	nlm_res *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_UNLOCK_RES, xdr_nlm_res, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


void *
nlm_granted_res_1(argp, clnt)
	nlm_res *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_GRANTED_RES, xdr_nlm_res, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


nlm_shareres *
nlm_share_3(argp, clnt)
	nlm_shareargs *argp;
	CLIENT *clnt;
{
	static nlm_shareres res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_SHARE, xdr_nlm_shareargs, argp, xdr_nlm_shareres, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


nlm_shareres *
nlm_unshare_3(argp, clnt)
	nlm_shareargs *argp;
	CLIENT *clnt;
{
	static nlm_shareres res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_UNSHARE, xdr_nlm_shareargs, argp, xdr_nlm_shareres, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


nlm_res *
nlm_nm_lock_3(argp, clnt)
	nlm_lockargs *argp;
	CLIENT *clnt;
{
	static nlm_res res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_NM_LOCK, xdr_nlm_lockargs, argp, xdr_nlm_res, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return (&res);
}


void *
nlm_free_all_3(argp, clnt)
	nlm_notify *argp;
	CLIENT *clnt;
{
	static char res;

	bzero((char *)&res, sizeof(res));
	if (clnt_call(clnt, NLM_FREE_ALL, xdr_nlm_notify, argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *)&res);
}


int main(int argc, char **argv)
{
	CLIENT *cli;
	nlm_res res_block;
	nlm_res *out;
	nlm_lockargs arg;
	struct timeval tim;

	printf("Creating client for host %s\n", argv[1]);
	cli = clnt_create(argv[1], NLM_PROG, NLM_VERS, "udp");
	if (!cli) {
		errx(1, "Failed to create client\n");
		/* NOTREACHED */
	}
	clnt_control(cli, CLGET_TIMEOUT, &tim);
	printf("Default timeout was %d.%d\n", tim.tv_sec, tim.tv_usec);
	tim.tv_usec = -1;
	tim.tv_sec = -1;
	clnt_control(cli, CLSET_TIMEOUT, &tim);
	clnt_control(cli, CLGET_TIMEOUT, &tim);
	printf("timeout now %d.%d\n", tim.tv_sec, tim.tv_usec);


	arg.cookie.n_len = 4;
	arg.cookie.n_bytes = "hello";
	arg.block = 0;
	arg.exclusive = 0;
	arg.reclaim = 0;
	arg.state = 0x1234;
	arg.alock.caller_name = "localhost";
	arg.alock.fh.n_len = 32;
	arg.alock.fh.n_bytes = "\x04\x04\x02\x00\x01\x00\x00\x00\x0c\x00\x00\x00\xff\xff\xff\xd0\x16\x00\x00\x5b\x7c\xff\xff\xff\xec\x2f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x19\x54\xef\xbf\xd7\x94";
	arg.alock.oh.n_len = 8;
	arg.alock.oh.n_bytes = "\x00\x00\x02\xff\xff\xff\xd3";
	arg.alock.svid = 0x5678;
	arg.alock.l_offset = 0;
	arg.alock.l_len = 100;

	res_block.stat.stat = nlm_granted;
	res_block.cookie.n_bytes = "hello";
	res_block.cookie.n_len = 5;

#if 0
	if (nlm_lock_res_1(&res_block, cli))
		printf("Success!\n");
	else
		printf("Fail\n");
#else
	if (out = nlm_lock_msg_1(&arg, cli)) {
		printf("Success!\n");
		printf("out->stat = %d", out->stat);
	} else {
		printf("Fail\n");
	}
#endif

	return 0;
}
