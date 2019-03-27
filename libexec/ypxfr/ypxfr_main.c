/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/ypxfrd.h>
#include "ypxfr_extern.h"

char *progname = "ypxfr";
char *yp_dir = _PATH_YP;
int _rpcpmstart = 0;
static int ypxfr_use_yplib = 0; /* Assume the worst. */
static int ypxfr_clear = 1;
static int ypxfr_prognum = 0;
static struct sockaddr_in ypxfr_callback_addr;
static struct yppushresp_xfr ypxfr_resp;
static DB *dbp;

static void
ypxfr_exit(ypxfrstat retval, char *temp)
{
	CLIENT *clnt;
	int sock = RPC_ANYSOCK;
	struct timeval timeout;

	/* Clean up no matter what happened previously. */
	if (temp != NULL) {
		if (dbp != NULL)
			(void)(dbp->close)(dbp);
		if (unlink(temp) == -1) {
			yp_error("failed to unlink %s",strerror(errno));
		}
	}

	if (ypxfr_prognum) {
		timeout.tv_sec = 20;
		timeout.tv_usec = 0;

		if ((clnt = clntudp_create(&ypxfr_callback_addr, ypxfr_prognum,
					1, timeout, &sock)) == NULL) {
			yp_error("%s", clnt_spcreateerror("failed to "
			    "establish callback handle"));
			exit(1);
		}

		ypxfr_resp.status = (yppush_status)retval;

		if (yppushproc_xfrresp_1(&ypxfr_resp, clnt) == NULL) {
			yp_error("%s", clnt_sperror(clnt, "callback failed"));
			clnt_destroy(clnt);
			exit(1);
		}
		clnt_destroy(clnt);
	} else {
		yp_error("Exiting: %s", ypxfrerr_string(retval));
	}

	exit(0);
}

static void
usage(void)
{
	if (_rpcpmstart) {
		ypxfr_exit(YPXFR_BADARGS,NULL);
	} else {
		fprintf(stderr, "%s\n%s\n%s\n",
	"usage: ypxfr [-f] [-c] [-d target domain] [-h source host]",
	"             [-s source domain] [-p path]",
	"             [-C taskid program-number ipaddr port] mapname");
		exit(1);
	}
}

int
ypxfr_foreach(int status, char *key, int keylen, char *val, int vallen,
    char *data)
{
	DBT dbkey, dbval;

	if (status != YP_TRUE)
		return (status);

	/*
	 * XXX Do not attempt to write zero-length keys or
	 * data into a Berkeley DB hash database. It causes a
	 * strange failure mode where sequential searches get
	 * caught in an infinite loop.
	 */
	if (keylen) {
		dbkey.data = key;
		dbkey.size = keylen;
	} else {
		dbkey.data = "";
		dbkey.size = 1;
	}
	if (vallen) {
		dbval.data = val;
		dbval.size = vallen;
	} else {
		dbval.data = "";
		dbval.size = 1;
	}

	if (yp_put_record(dbp, &dbkey, &dbval, 0) != YP_TRUE)
		return(yp_errno);

	return (0);
}

int
main(int argc, char *argv[])
{
	int ch;
	int ypxfr_force = 0;
	char *ypxfr_dest_domain = NULL;
	char *ypxfr_source_host = NULL;
	char *ypxfr_source_domain = NULL;
	char *ypxfr_local_domain = NULL;
	char *ypxfr_master = NULL;
	unsigned long ypxfr_order = -1, ypxfr_skew_check = -1;
	char *ypxfr_mapname = NULL;
	int ypxfr_args = 0;
	char ypxfr_temp_map[MAXPATHLEN + 2];
	char tempmap[MAXPATHLEN + 2];
	char buf[MAXPATHLEN + 2];
	DBT key, data;
	int remoteport;
	int interdom = 0;
	int secure = 0;

	debug = 1;

	if (!isatty(fileno(stderr))) {
		openlog("ypxfr", LOG_PID, LOG_DAEMON);
		_rpcpmstart = 1;
	}

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv, "fcd:h:s:p:C:")) != -1) {
		int my_optind;
		switch (ch) {
		case 'f':
			ypxfr_force++;
			ypxfr_args++;
			break;
		case 'c':
			ypxfr_clear = 0;
			ypxfr_args++;
			break;
		case 'd':
			ypxfr_dest_domain = optarg;
			ypxfr_args += 2;
			break;
		case 'h':
			ypxfr_source_host = optarg;
			ypxfr_args += 2;
			break;
		case 's':
			ypxfr_source_domain = optarg;
			ypxfr_args += 2;
			break;
		case 'p':
			yp_dir = optarg;
			ypxfr_args += 2;
			break;
		case 'C':
			/*
			 * Whoever decided that the -C flag should take
			 * four arguments is a twit.
			 */
			my_optind = optind - 1;
			if (argv[my_optind] == NULL || !strlen(argv[my_optind])) {
				yp_error("transaction ID not specified");
				usage();
			}
			ypxfr_resp.transid = atol(argv[my_optind]);
			my_optind++;
			if (argv[my_optind] == NULL || !strlen(argv[my_optind])) {
				yp_error("RPC program number not specified");
				usage();
			}
			ypxfr_prognum = atol(argv[my_optind]);
			my_optind++;
			if (argv[my_optind] == NULL || !strlen(argv[my_optind])) {
				yp_error("address not specified");
				usage();
			}
			if (!inet_aton(argv[my_optind], &ypxfr_callback_addr.sin_addr)) {
				yp_error("failed to convert '%s' to IP addr",
					argv[my_optind]);
				exit(1);
			}
			my_optind++;
			if (argv[my_optind] == NULL || !strlen(argv[my_optind])) {
				yp_error("port not specified");
				usage();
			}
			ypxfr_callback_addr.sin_port = htons((u_short)atoi(argv[my_optind]));
			ypxfr_args += 5;
			break;
		default:
			usage();
			break;
		}
	}

	ypxfr_mapname = argv[ypxfr_args + 1];

	if (ypxfr_mapname == NULL) {
		yp_error("no map name specified");
		usage();
	}

	/* Always the case. */
	ypxfr_callback_addr.sin_family = AF_INET;

	/* Determine if local NIS client facilities are turned on. */
	if (!yp_get_default_domain(&ypxfr_local_domain) &&
	    _yp_check(&ypxfr_local_domain))
		ypxfr_use_yplib = 1;

	/*
	 * If no destination domain is specified, assume that the
	 * local default domain is to be used and try to obtain it.
	 * Fails if NIS client facilities are turned off.
	 */
	if (ypxfr_dest_domain == NULL) {
		if (ypxfr_use_yplib) {
			yp_get_default_domain(&ypxfr_dest_domain);
		} else {
			yp_error("no destination domain specified and \
the local domain name isn't set");
			ypxfr_exit(YPXFR_BADARGS,NULL);
		}
	}

	/*
	 * If a source domain is not specified, assume it to
	 * be the same as the destination domain.
	 */
	if (ypxfr_source_domain == NULL) {
		ypxfr_source_domain = ypxfr_dest_domain;
	}

	/*
	 * If the source host is not specified, assume it to be the
	 * master for the specified map. If local NIS client facilities
	 * are turned on, we can figure this out using yp_master().
	 * If not, we have to see if a local copy of the map exists
	 * and extract its YP_MASTER_NAME record. If _that_ fails,
	 * we are stuck and must ask the user for more information.
	 */
	if (ypxfr_source_host == NULL) {
		if (!ypxfr_use_yplib) {
		/*
		 * Double whammy: NIS isn't turned on and the user
		 * didn't specify a source host.
		 */
			char *dptr;
			key.data = "YP_MASTER_NAME";
			key.size = sizeof("YP_MASTER_NAME") - 1;

			if (yp_get_record(ypxfr_dest_domain, ypxfr_mapname,
					 &key, &data, 1) != YP_TRUE) {
				yp_error("no source host specified");
				ypxfr_exit(YPXFR_BADARGS,NULL);
			}
			dptr = data.data;
			dptr[data.size] = '\0';
			ypxfr_master = ypxfr_source_host = strdup(dptr);
		}
	} else {
		if (ypxfr_use_yplib)
			ypxfr_use_yplib = 0;
	}

	if (ypxfr_master == NULL) {
		if ((ypxfr_master = ypxfr_get_master(ypxfr_source_domain,
					    	 ypxfr_mapname,
					     	ypxfr_source_host,
					     	ypxfr_use_yplib)) == NULL) {
			yp_error("failed to find master of %s in domain %s: %s",
				  ypxfr_mapname, ypxfr_source_domain,
				  ypxfrerr_string((ypxfrstat)yp_errno));
			ypxfr_exit(YPXFR_MADDR,NULL);
		}
	}

	/*
	 * If we got here and ypxfr_source_host is still undefined,
	 * it means we had to resort to using yp_master() to find the
	 * master server for the map. The source host and master should
	 * be identical.
	 */
	if (ypxfr_source_host == NULL)
		ypxfr_source_host = ypxfr_master;

	/*
	 * Don't talk to ypservs on unprivileged ports.
	 */
	remoteport = getrpcport(ypxfr_source_host, YPPROG, YPVERS, IPPROTO_UDP);
	if (remoteport >= IPPORT_RESERVED) {
		yp_error("ypserv on %s not running on reserved port",
						ypxfr_source_host);
		ypxfr_exit(YPXFR_REFUSED, NULL);
	}

	if ((ypxfr_order = ypxfr_get_order(ypxfr_source_domain,
					     ypxfr_mapname,
					     ypxfr_master, 0)) == 0) {
		yp_error("failed to get order number of %s: %s",
				ypxfr_mapname, yp_errno == YP_TRUE ?
				"map has order 0" :
				ypxfrerr_string((ypxfrstat)yp_errno));
		ypxfr_exit(YPXFR_YPERR,NULL);
	}

	if (ypxfr_match(ypxfr_master, ypxfr_source_domain, ypxfr_mapname,
			"YP_INTERDOMAIN", sizeof("YP_INTERDOMAIN") - 1))
		interdom++;

	if (ypxfr_match(ypxfr_master, ypxfr_source_domain, ypxfr_mapname,
			"YP_SECURE", sizeof("YP_SECURE") - 1))
		secure++;

	key.data = "YP_LAST_MODIFIED";
	key.size = sizeof("YP_LAST_MODIFIED") - 1;

	/* The order number is immaterial when the 'force' flag is set. */

	if (!ypxfr_force) {
		int ignore = 0;
		if (yp_get_record(ypxfr_dest_domain,ypxfr_mapname,&key,&data,1) != YP_TRUE) {
			switch (yp_errno) {
			case YP_NOKEY:
				ypxfr_exit(YPXFR_FORCE,NULL);
				break;
			case YP_NOMAP:
				/*
				 * If the map doesn't exist, we're
				 * creating it. Ignore the error.
				 */
				ignore++;
				break;
			case YP_BADDB:
			default:
				ypxfr_exit(YPXFR_DBM,NULL);
				break;
			}
		}
		if (!ignore && ypxfr_order <= atoi(data.data))
			ypxfr_exit(YPXFR_AGE, NULL);

	}

	/* Construct a temporary map file name */
	snprintf(tempmap, sizeof(tempmap), "%s.%d",ypxfr_mapname, getpid());
	snprintf(ypxfr_temp_map, sizeof(ypxfr_temp_map), "%s/%s/%s", yp_dir,
		 ypxfr_dest_domain, tempmap);

	if ((remoteport = getrpcport(ypxfr_source_host, YPXFRD_FREEBSD_PROG,
					YPXFRD_FREEBSD_VERS, IPPROTO_TCP))) {

		/* Don't talk to rpc.ypxfrds on unprovileged ports. */
		if (remoteport >= IPPORT_RESERVED) {
			yp_error("rpc.ypxfrd on %s not using privileged port",
							ypxfr_source_host);
			ypxfr_exit(YPXFR_REFUSED, NULL);
		}

		/* Try to send using ypxfrd. If it fails, use old method. */
		if (!ypxfrd_get_map(ypxfr_source_host, ypxfr_mapname,
					ypxfr_source_domain, ypxfr_temp_map))
			goto leave;
	}

	/* Open the temporary map read/write. */
	if ((dbp = yp_open_db_rw(ypxfr_dest_domain, tempmap, 0)) == NULL) {
		yp_error("failed to open temporary map file");
		ypxfr_exit(YPXFR_DBM,NULL);
	}

	/*
	 * Fill in the keys we already know, such as the order number,
	 * master name, input file name (we actually make up a bogus
	 * name for that) and output file name.
	 */
	snprintf(buf, sizeof(buf), "%lu", ypxfr_order);
	data.data = buf;
	data.size = strlen(buf);

	if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
		yp_error("failed to write order number to database");
		ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
	}

	key.data = "YP_MASTER_NAME";
	key.size = sizeof("YP_MASTER_NAME") - 1;
	data.data = ypxfr_master;
	data.size = strlen(ypxfr_master);

	if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
		yp_error("failed to write master name to database");
		ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
	}

	key.data = "YP_DOMAIN_NAME";
	key.size = sizeof("YP_DOMAIN_NAME") - 1;
	data.data = ypxfr_dest_domain;
	data.size = strlen(ypxfr_dest_domain);

	if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
		yp_error("failed to write domain name to database");
		ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
	}

	snprintf (buf, sizeof(buf), "%s:%s", ypxfr_source_host, ypxfr_mapname);

	key.data = "YP_INPUT_NAME";
	key.size = sizeof("YP_INPUT_NAME") - 1;
	data.data = &buf;
	data.size = strlen(buf);

	if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
		yp_error("failed to write input name to database");
		ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);

	}

	snprintf(buf, sizeof(buf), "%s/%s/%s", yp_dir, ypxfr_dest_domain,
							ypxfr_mapname);

	key.data = "YP_OUTPUT_NAME";
	key.size = sizeof("YP_OUTPUT_NAME") - 1;
	data.data = &buf;
	data.size = strlen(buf);

	if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
		yp_error("failed to write output name to database");
		ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
	}

	if (interdom) {
		key.data = "YP_INTERDOMAIN";
		key.size = sizeof("YP_INTERDOMAIN") - 1;
		data.data = "";
		data.size = 0;

		if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
			yp_error("failed to add interdomain flag to database");
			ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
		}
	}

	if (secure) {
		key.data = "YP_SECURE";
		key.size = sizeof("YP_SECURE") - 1;
		data.data = "";
		data.size = 0;

		if (yp_put_record(dbp, &key, &data, 0) != YP_TRUE) {
			yp_error("failed to add secure flag to database");
			ypxfr_exit(YPXFR_DBM,ypxfr_temp_map);
		}
	}

	/* Now suck over the contents of the map from the master. */

	if (ypxfr_get_map(ypxfr_mapname,ypxfr_source_domain,
			  ypxfr_source_host, ypxfr_foreach)){
		yp_error("failed to retrieve map from source host");
		ypxfr_exit(YPXFR_YPERR,ypxfr_temp_map);
	}

	(void)(dbp->close)(dbp);
	dbp = NULL; /* <- yes, it seems this is necessary. */

leave:

	snprintf(buf, sizeof(buf), "%s/%s/%s", yp_dir, ypxfr_dest_domain,
							ypxfr_mapname);

	/* Peek at the order number again and check for skew. */
	if ((ypxfr_skew_check = ypxfr_get_order(ypxfr_source_domain,
					     ypxfr_mapname,
					     ypxfr_master, 0)) == 0) {
		yp_error("failed to get order number of %s: %s",
				ypxfr_mapname, yp_errno == YP_TRUE ?
				"map has order 0" :
				ypxfrerr_string((ypxfrstat)yp_errno));
		ypxfr_exit(YPXFR_YPERR,ypxfr_temp_map);
	}

	if (ypxfr_order != ypxfr_skew_check)
		ypxfr_exit(YPXFR_SKEW,ypxfr_temp_map);

	/*
	 * Send a YPPROC_CLEAR to the local ypserv.
	 */
	if (ypxfr_clear) {
		char in = 0;
		char *out = NULL;
		int stat;
		if ((stat = callrpc("localhost",YPPROG,YPVERS,YPPROC_CLEAR,
			(xdrproc_t)xdr_void, (void *)&in,
			(xdrproc_t)xdr_void, (void *)out)) != RPC_SUCCESS) {
			yp_error("failed to send 'clear' to local ypserv: %s",
				 clnt_sperrno((enum clnt_stat) stat));
			ypxfr_exit(YPXFR_CLEAR, ypxfr_temp_map);
		}
	}

	/*
	 * Put the new map in place immediately. I'm not sure if the
	 * kernel does an unlink() and rename() atomically in the event
	 * that we move a new copy of a map over the top of an existing
	 * one, but there's less chance of a race condition happening
	 * than if we were to do the unlink() ourselves.
	 */
	if (rename(ypxfr_temp_map, buf) == -1) {
		yp_error("rename(%s,%s) failed: %s", ypxfr_temp_map, buf,
							strerror(errno));
		ypxfr_exit(YPXFR_FILE,NULL);
	}

	ypxfr_exit(YPXFR_SUCC,NULL);

	return(1);
}
