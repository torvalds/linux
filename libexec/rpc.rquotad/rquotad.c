/*	$OpenBSD: rquotad.c,v 1.26 2024/05/21 05:00:47 jsg Exp $	*/

/*
 * by Manuel Bouyer (bouyer@ensta.fr). Public domain.
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <ufs/ufs/quota.h>
#include <rpc/rpc.h>
#include <rpcsvc/rquota.h>
#include <arpa/inet.h>

void rquota_service(struct svc_req *request, SVCXPRT *transp);
void sendquota(struct svc_req *request, SVCXPRT *transp);
void initfs(void);
int getfsquota(long id, char *path, struct dqblk *dqblk);
int hasquota(struct fstab *fs, char **qfnamep);

/*
 * structure containing information about ufs filesystems
 * initialised by initfs()
 */
struct fs_stat {
	struct fs_stat *fs_next;	/* next element */
	char   *fs_file;		/* mount point of the filesystem */
	char   *qfpathname;		/* pathname of the quota file */
	dev_t   st_dev;			/* device of the filesystem */
};
struct fs_stat *fs_begin = NULL;

int from_inetd = 1;

static void
cleanup(int signo)
{
	(void) pmap_unset(RQUOTAPROG, RQUOTAVERS);	/* XXX signal races */
	_exit(0);
}

int
main(int argc, char *argv[])
{
	SVCXPRT *transp;
	int sock = 0;
	int proto = 0;
	struct sockaddr_storage from;
	socklen_t fromlen;

	fromlen = sizeof(from);
	if (getsockname(0, (struct sockaddr *)&from, &fromlen) == -1) {
		from_inetd = 0;
		sock = RPC_ANYSOCK;
		proto = IPPROTO_UDP;
	}

	if (!from_inetd) {
		daemon(0, 0);

		(void) pmap_unset(RQUOTAPROG, RQUOTAVERS);

		(void) signal(SIGINT, cleanup);
		(void) signal(SIGTERM, cleanup);
		(void) signal(SIGHUP, cleanup);
	}

	openlog("rpc.rquotad", LOG_CONS|LOG_PID, LOG_DAEMON);

	/* create and register the service */
	transp = svcudp_create(sock);
	if (transp == NULL) {
		syslog(LOG_ERR, "couldn't create udp service.");
		exit(1);
	}
	if (!svc_register(transp, RQUOTAPROG, RQUOTAVERS, rquota_service, proto)) {
		syslog(LOG_ERR, "unable to register (RQUOTAPROG, RQUOTAVERS, %s).",
		    proto ? "udp" : "(inetd)");
		exit(1);
	}

	initfs();		/* init the fs_stat list */
	svc_run();
	syslog(LOG_ERR, "svc_run returned");
	exit(1);
}

void
rquota_service(struct svc_req *request, SVCXPRT *transp)
{
	switch (request->rq_proc) {
	case NULLPROC:
		(void)svc_sendreply(transp, xdr_void, (char *)NULL);
		break;

	case RQUOTAPROC_GETQUOTA:
	case RQUOTAPROC_GETACTIVEQUOTA:
		sendquota(request, transp);
		break;

	default:
		svcerr_noproc(transp);
		break;
	}
	if (from_inetd)
		exit(0);
}

/* read quota for the specified id, and send it */
void
sendquota(struct svc_req *request, SVCXPRT *transp)
{
	struct getquota_args getq_args;
	struct getquota_rslt getq_rslt;
	struct dqblk dqblk;
	struct timeval timev;

	bzero((char *)&getq_args, sizeof(getq_args));
	if (!svc_getargs(transp, xdr_getquota_args, (caddr_t)&getq_args)) {
		svcerr_decode(transp);
		return;
	}
	if (request->rq_cred.oa_flavor != AUTH_UNIX) {
		/* bad auth */
		getq_rslt.status = Q_EPERM;
	} else if (!getfsquota(getq_args.gqa_uid, getq_args.gqa_pathp, &dqblk)) {
		/* failed, return noquota */
		getq_rslt.status = Q_NOQUOTA;
	} else {
		gettimeofday(&timev, NULL);
		getq_rslt.status = Q_OK;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_active = TRUE;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_bsize = DEV_BSIZE;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_bhardlimit =
		    dqblk.dqb_bhardlimit;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_bsoftlimit =
		    dqblk.dqb_bsoftlimit;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_curblocks =
		    dqblk.dqb_curblocks;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_fhardlimit =
		    dqblk.dqb_ihardlimit;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_fsoftlimit =
		    dqblk.dqb_isoftlimit;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_curfiles =
		    dqblk.dqb_curinodes;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_btimeleft =
		    dqblk.dqb_btime - timev.tv_sec;
		getq_rslt.getquota_rslt_u.gqr_rquota.rq_ftimeleft =
		    dqblk.dqb_itime - timev.tv_sec;
	}
	if (!svc_sendreply(transp, xdr_getquota_rslt, (char *)&getq_rslt)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_getquota_args, (caddr_t)&getq_args)) {
		syslog(LOG_ERR, "unable to free arguments");
		exit(1);
	}
}

/* initialise the fs_tab list from entries in /etc/fstab */
void
initfs(void)
{
	struct fs_stat *fs_current = NULL;
	struct fs_stat *fs_next = NULL;
	char *qfpathname;
	struct fstab *fs;
	struct stat st;

	setfsent();
	while ((fs = getfsent())) {
		if (strcmp(fs->fs_vfstype, "ffs"))
			continue;
		if (!hasquota(fs, &qfpathname))
			continue;

		fs_current = malloc(sizeof(struct fs_stat));
		if (fs_current == NULL) {
			syslog(LOG_ERR, "can't malloc: %m");
			exit(1);
		}
		fs_current->fs_next = fs_next;	/* next element */

		fs_current->fs_file = strdup(fs->fs_file);
		if (fs_current->fs_file == NULL) {
			syslog(LOG_ERR, "can't strdup: %m");
			exit(1);
		}

		fs_current->qfpathname = strdup(qfpathname);
		if (fs_current->qfpathname == NULL) {
			syslog(LOG_ERR, "can't strdup: %m");
			exit(1);
		}

		stat(fs_current->fs_file, &st);
		fs_current->st_dev = st.st_dev;

		fs_next = fs_current;
	}
	endfsent();
	fs_begin = fs_current;
}

/*
 * gets the quotas for id, filesystem path.
 * Return 0 if fail, 1 otherwise
 */
int
getfsquota(long id, char *path, struct dqblk *dqblk)
{
	struct stat st_path;
	struct fs_stat *fs;
	int	qcmd, fd, ret = 0;

	if (stat(path, &st_path) == -1)
		return (0);

	qcmd = QCMD(Q_GETQUOTA, USRQUOTA);

	for (fs = fs_begin; fs != NULL; fs = fs->fs_next) {
		/* where the device is the same as path */
		if (fs->st_dev != st_path.st_dev)
			continue;

		/* find the specified filesystem. get and return quota */
		if (quotactl(fs->fs_file, qcmd, id, (char *)dqblk) == 0)
			return (1);

		if ((fd = open(fs->qfpathname, O_RDONLY)) == -1) {
			syslog(LOG_ERR, "open error: %s: %m", fs->qfpathname);
			return (0);
		}
		if (lseek(fd, (off_t)(id * sizeof(struct dqblk)), SEEK_SET) ==
		    (off_t)-1) {
			close(fd);
			return (1);
		}
		switch (read(fd, dqblk, sizeof(struct dqblk))) {
		case 0:
			/*
			 * Convert implicit 0 quota (EOF)
			 * into an explicit one (zero'ed dqblk)
			 */
			bzero((caddr_t) dqblk, sizeof(struct dqblk));
			ret = 1;
			break;
		case sizeof(struct dqblk):	/* OK */
			ret = 1;
			break;
		default:	/* ERROR */
			syslog(LOG_ERR, "read error: %s: %m", fs->qfpathname);
			close(fd);
			return (0);
		}
		close(fd);
	}
	return (ret);
}

/*
 * Check to see if a particular quota is to be enabled.
 * Comes from quota.c, NetBSD 0.9
 */
int
hasquota(struct fstab *fs, char **qfnamep)
{
	static char initname, usrname[100];
	static char buf[BUFSIZ];
	char	*opt, *cp;
	char	*qfextension[] = INITQFNAMES;

	cp = NULL;
	if (!initname) {
		(void)snprintf(usrname, sizeof usrname, "%s%s",
		    qfextension[USRQUOTA], QUOTAFILENAME);
		initname = 1;
	}
	strlcpy(buf, fs->fs_mntops, sizeof buf);
	for (opt = strtok(buf, ","); opt; opt = strtok(NULL, ",")) {
		if ((cp = strchr(opt, '=')))
			*cp++ = '\0';
		if (strcmp(opt, usrname) == 0)
			break;
	}
	if (!opt)
		return (0);
	if (cp) {
		*qfnamep = cp;
		return (1);
	}
	(void)snprintf(buf, sizeof buf, "%s/%s.%s", fs->fs_file,
	    QUOTAFILENAME, qfextension[USRQUOTA]);
	*qfnamep = buf;
	return (1);
}
