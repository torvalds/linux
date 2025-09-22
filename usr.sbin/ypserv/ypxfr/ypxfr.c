/*	$OpenBSD: ypxfr.c,v 1.39 2015/02/09 23:00:15 deraadt Exp $ */

/*
 * Copyright (c) 1994 Mats O Jansson <moj@stacken.kth.se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#include "yplib_host.h"
#include "yplog.h"
#include "ypdb.h"
#include "ypdef.h"

DBM	*db;

static int
ypxfr_foreach(u_long status, char *keystr, int keylen, char *valstr, int vallen,
    void *data)
{
	datum key, val;

	if (status == YP_NOMORE)
		return(0);

	keystr[keylen] = '\0';
	valstr[vallen] = '\0';

	key.dptr = keystr;
	key.dsize = strlen(keystr);

	val.dptr = valstr;
	val.dsize = strlen(valstr);

	ypdb_store(db, key, val, YPDB_INSERT);
	return 0;
}

static int
get_local_ordernum(char *domain, char *map, u_int32_t *lordernum)
{
	char map_path[PATH_MAX], order[MAX_LAST_LEN+1];
	char order_key[] = YP_LAST_KEY;
	struct stat finfo;
	datum k, v;
	int status;
	DBM *db;

	/* This routine returns YPPUSH_SUCC or YPPUSH_NODOM */

	status = YPPUSH_SUCC;

	snprintf(map_path, sizeof map_path, "%s/%s", YP_DB_PATH, domain);
	if (!((stat(map_path, &finfo) == 0) && S_ISDIR(finfo.st_mode))) {
		fprintf(stderr, "ypxfr: domain %s not found locally\n",
		    domain);
		status = YPPUSH_NODOM;
		goto bail;
	}

	snprintf(map_path, sizeof map_path, "%s/%s/%s%s",
	    YP_DB_PATH, domain, map, YPDB_SUFFIX);
	if (!(stat(map_path, &finfo) == 0)) {
		status = YPPUSH_NOMAP;
		goto bail;
	}

	snprintf(map_path, sizeof map_path, "%s/%s/%s",
	    YP_DB_PATH, domain, map);
	db = ypdb_open(map_path, O_RDONLY, 0444);
	if (db == NULL) {
		status = YPPUSH_DBM;
		goto bail;
	}

	k.dptr = (char *)&order_key;
	k.dsize = YP_LAST_LEN;

	v = ypdb_fetch(db, k);

	if (v.dptr == NULL) {
		*lordernum = 0;
	} else {
		strlcpy(order, v.dptr, sizeof order);
		*lordernum = (u_int32_t)atol(order);
	}

	ypdb_close(db);
bail:
	if (status == YPPUSH_NOMAP || status == YPPUSH_DBM) {
		*lordernum = 0;
		status = YPPUSH_SUCC;
	}
	return (status);

}

static int
get_remote_ordernum(CLIENT *client, char *domain, char *map,
    u_int32_t lordernum, u_int32_t *rordernum)
{
	int status;

	status = yp_order_host(client, domain, map, rordernum);

	if (status == 0) {
		if (*rordernum <= lordernum)
			status = YPPUSH_AGE;
		else
			status = YPPUSH_SUCC;
	}
	return status;
}

static int
get_map(CLIENT *client, char *domain, char *map,
    struct ypall_callback *incallback)
{
	int	status;

	status = yp_all_host(client, domain, map, incallback);
	if (status == 0 || status == YPERR_NOMORE)
		status = YPPUSH_SUCC;
	else
		status = YPPUSH_YPERR;
	return (status);
}

static DBM *
create_db(char *domain, char *map, char *temp_map)
{
	return ypdb_open_suf(temp_map, O_RDWR, 0444);
}

static int
install_db(char *domain, char *map, char *temp_map)
{
	char	db_name[PATH_MAX];

	snprintf(db_name, sizeof db_name, "%s/%s/%s%s",
	    YP_DB_PATH, domain, map, YPDB_SUFFIX);
	rename(temp_map, db_name);
	return YPPUSH_SUCC;
}

static int
add_order(DBM *db, u_int32_t ordernum)
{
	char	datestr[11];
	datum	key, val;
	char	keystr[] = YP_LAST_KEY;
	int	status;

	snprintf(datestr, sizeof datestr, "%010u", ordernum);

	key.dptr = keystr;
	key.dsize = strlen(keystr);

	val.dptr = datestr;
	val.dsize = strlen(datestr);

	status = ypdb_store(db, key, val, YPDB_INSERT);
	if (status >= 0)
		status = YPPUSH_SUCC;
	else
		status = YPPUSH_DBM;
	return (status);
}

static int
add_master(CLIENT *client, char *domain, char *map, DBM *db)
{
	char	keystr[] = YP_MASTER_KEY, *master = NULL;
	datum	key, val;
	int	status;

	/* Get MASTER */
	status = yp_master_host(client, domain, map, &master);

	if (master != NULL) {
		key.dptr = keystr;
		key.dsize = strlen(keystr);

		val.dptr = master;
		val.dsize = strlen(master);

		status = ypdb_store(db, key, val, YPDB_INSERT);
		if (status >= 0)
			status = YPPUSH_SUCC;
		else
			status = YPPUSH_DBM;
	}
	return (status);
}

static int
add_interdomain(CLIENT *client, char *domain, char *map, DBM *db)
{
	char	keystr[] = YP_INTERDOMAIN_KEY, *value;
	int	vallen, status;
	datum	k, v;

	/* Get INTERDOMAIN */

	k.dptr = keystr;
	k.dsize = strlen(keystr);

	status = yp_match_host(client, domain, map,
	    k.dptr, k.dsize, &value, &vallen);
	if (status == 0 && value) {
		v.dptr = value;
		v.dsize = vallen;

		if (v.dptr != NULL) {
			status = ypdb_store(db, k, v, YPDB_INSERT);
			if (status >= 0)
				status = YPPUSH_SUCC;
			else
				status = YPPUSH_DBM;
		}
	}
	return 1;
}

static int
add_secure(CLIENT *client, char *domain, char *map, DBM *db)
{
	char	keystr[] = YP_SECURE_KEY, *value;
	int	vallen, status;
	datum	k, v;

	/* Get SECURE */

	k.dptr = keystr;
	k.dsize = strlen(keystr);

	status = yp_match_host(client, domain, map,
	    k.dptr, k.dsize, &value, &vallen);
	if (status == 0 && value) {
		v.dptr = value;
		v.dsize = vallen;

		if (v.dptr != NULL) {
			status = ypdb_store(db, k, v, YPDB_INSERT);
			if (status >= 0)
				status = YPPUSH_SUCC;
			else
				status = YPPUSH_DBM;
		}
	}
	return status;
}

static int
send_clear(CLIENT *client)
{
	struct	timeval tv;
	int	status, r;

	status = YPPUSH_SUCC;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	/* Send CLEAR */
	r = clnt_call(client, YPPROC_CLEAR, xdr_void, 0, xdr_void, 0, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yp_clear: clnt_call");
	return status;

}

static int
send_reply(CLIENT *client, u_long status, u_long tid)
{
	struct	ypresp_xfr resp;
	struct	timeval tv;
	int	r;

	tv.tv_sec = 10;
	tv.tv_usec = 0;

	resp.transid = tid;
	resp.xfrstat = status;

	/* Send CLEAR */
	r = clnt_call(client, 1, xdr_ypresp_xfr, &resp, xdr_void, 0, tv);
	if (r != RPC_SUCCESS)
		clnt_perror(client, "yppushresp_xdr: clnt_call");
	return status;

}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: ypxfr [-cf] [-C tid prog ipadd port] [-d domain] "
	    "[-h host] [-s domain]\n"
	    "             mapname\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int	 cflag = 0, fflag = 0, Cflag = 0;
	char	 *domain, *host = NULL, *srcdomain = NULL;
	char	 *tid = NULL, *prog = NULL, *ipadd = NULL;
	char	 *port = NULL, *map = NULL;
	int	 status, xfr_status, ch, srvport;
	u_int32_t ordernum, new_ordernum;
	struct	 ypall_callback callback;
	CLIENT   *client = NULL;
	extern	 char *optarg;

	yp_get_default_domain(&domain);

	while ((ch = getopt(argc, argv, "cd:fh:s:C:")) != -1)
		switch (ch) {
		case 'c':
			cflag = 1;
			break;
		case 'd':
			if (strchr(optarg, '/')) /* Ha ha, we are not listening */
				break;
			domain = optarg;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'h':
			host = optarg;
			break;
		case 's':
			if (strchr(optarg, '/')) /* Ha ha, we are not listening */
				break;
			srcdomain = optarg;
			break;
		case 'C':
			if (optind + 3 >= argc)
				usage();
			Cflag = 1;
			tid = optarg;
			prog = argv[optind++];
			ipadd = argv[optind++];
			port = argv[optind++];
			break;
		default:
			usage();
			break;
		}

	status = YPPUSH_SUCC;

	if (optind + 1 != argc)
		usage();

	map = argv[optind];

	if (status > 0) {
		ypopenlog();

		yplog("ypxfr: Arguments:");
		yplog("YP clear to local: %s", (cflag) ? "no" : "yes");
		yplog("   Force transfer: %s", (fflag) ? "yes" : "no");
		yplog("           domain: %s", domain);
		yplog("             host: %s", host);
		yplog("    source domain: %s", srcdomain);
		yplog("          transid: %s", tid);
		yplog("             prog: %s", prog);
		yplog("             port: %s", port);
		yplog("            ipadd: %s", ipadd);
		yplog("              map: %s", map);

		if (fflag != 0) {
			ordernum = 0;
		} else {
			status = get_local_ordernum(domain, map, &ordernum);
		}
	}

	if (status > 0) {
		yplog("Get Master");

		if (host == NULL) {
			if (srcdomain == NULL) {
				status = yp_master(domain, map, &host);
			} else {
				status = yp_master(srcdomain, map, &host);
			}
			if (status == 0) {
				status = YPPUSH_SUCC;
			} else {
				status = -status;
			}
		}
	}

	/* XXX this is raceable if portmap has holes! */
	if (status > 0) {
		yplog("Check for reserved port on host: %s", host);

		srvport = getrpcport(host, YPPROG, YPVERS, IPPROTO_TCP);
		if (srvport >= IPPORT_RESERVED)
			status = YPPUSH_REFUSED;
	}

	if (status > 0) {
		yplog("Connect host: %s", host);

		client = yp_bind_host(host, YPPROG, YPVERS, 0, 1);

		status = get_remote_ordernum(client, domain, map,
		    ordernum, &new_ordernum);
	}

	if (status == YPPUSH_SUCC) {
		char	tmpmapname[PATH_MAX];
		int	fd;

		/* Create temporary db */
		snprintf(tmpmapname, sizeof tmpmapname,
		    "%s/%s/ypdbXXXXXXXXXX", YP_DB_PATH, domain);
		fd = mkstemp(tmpmapname);
		if (fd == -1)
			status = YPPUSH_DBM;
		else
			close(fd);

		if (status > 0) {
			db = create_db(domain, map, tmpmapname);
			if (db == NULL)
				status = YPPUSH_DBM;
		}

		/* Add ORDER */
		if (status > 0)
			status = add_order(db, new_ordernum);

		/* Add MASTER */
		if (status > 0)
			status = add_master(client, domain, map, db);

		/* Add INTERDOMAIN */
		if (status > 0)
			status = add_interdomain(client, domain, map, db);

		/* Add SECURE */
		if (status > 0)
			status = add_secure(client, domain, map, db);

		if (status > 0) {
			callback.foreach = ypxfr_foreach;
			status = get_map(client, domain, map, &callback);
		}

		/* Close db */
		if (db != NULL)
			ypdb_close(db);

		/* Rename db */
		if (status > 0) {
			status = install_db(domain, map, tmpmapname);
		} else {
			unlink(tmpmapname);
			status = YPPUSH_SUCC;
		}
	}

	xfr_status = status;

	if (client != NULL)
		clnt_destroy(client);

	/* YP_CLEAR */

	if (!cflag) {
		client = yp_bind_local(YPPROG, YPVERS);
		status = send_clear(client);
		clnt_destroy(client);
	}

	if (Cflag > 0) {
		/* Send Response */
		client = yp_bind_host(ipadd, atoi(prog), 1, atoi(port), 0);
		status = send_reply(client, xfr_status, atoi(tid));
		clnt_destroy(client);
	}
	return (0);
}
