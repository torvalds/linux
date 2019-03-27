/* @(#)rquota.x	2.1 88/08/01 4.0 RPCSRC */
/* @(#)rquota.x 1.2 87/09/20 Copyr 1987 Sun Micro */

/*
 * Remote quota protocol
 * Requires unix authentication
 */

#ifndef RPC_HDR
%#include <sys/cdefs.h>
%__FBSDID("$FreeBSD$");
#endif

const RQ_PATHLEN = 1024;

struct sq_dqblk {
	unsigned int rq_bhardlimit;	/* absolute limit on disk blks alloc */
	unsigned int rq_bsoftlimit;	/* preferred limit on disk blks */
	unsigned int rq_curblocks;	/* current block count */
	unsigned int rq_fhardlimit;	/* absolute limit on allocated files */
	unsigned int rq_fsoftlimit;	/* preferred file limit */
	unsigned int rq_curfiles;	/* current # allocated files */
	unsigned int rq_btimeleft;	/* time left for excessive disk use */
	unsigned int rq_ftimeleft;	/* time left for excessive files */
};

struct getquota_args {
	string gqa_pathp<RQ_PATHLEN>;  	/* path to filesystem of interest */
	int gqa_uid;			/* Inquire about quota for uid */
};

struct setquota_args {
	int sqa_qcmd;
	string sqa_pathp<RQ_PATHLEN>;  	/* path to filesystem of interest */
	int sqa_id;			/* Set quota for uid */
	sq_dqblk sqa_dqblk;
};

struct ext_getquota_args {
	string gqa_pathp<RQ_PATHLEN>;  	/* path to filesystem of interest */
	int gqa_type;			/* Type of quota info is needed about */
	int gqa_id;			/* Inquire about quota for id */
};

struct ext_setquota_args {
	int sqa_qcmd;
	string sqa_pathp<RQ_PATHLEN>;  	/* path to filesystem of interest */
	int sqa_id;			/* Set quota for id */
	int sqa_type;			/* Type of quota to set */
	sq_dqblk sqa_dqblk;
};

/*
 * remote quota structure
 */
struct rquota {
	int rq_bsize;			/* block size for block counts */
	bool rq_active;  		/* indicates whether quota is active */
	unsigned int rq_bhardlimit;	/* absolute limit on disk blks alloc */
	unsigned int rq_bsoftlimit;	/* preferred limit on disk blks */
	unsigned int rq_curblocks;	/* current block count */
	unsigned int rq_fhardlimit;	/* absolute limit on allocated files */
	unsigned int rq_fsoftlimit;	/* preferred file limit */
	unsigned int rq_curfiles;	/* current # allocated files */
	unsigned int rq_btimeleft;	/* time left for excessive disk use */
	unsigned int rq_ftimeleft;	/* time left for excessive files */
};	

enum gqr_status {
	Q_OK = 1,		/* quota returned */
	Q_NOQUOTA = 2,		/* noquota for uid */
	Q_EPERM = 3		/* no permission to access quota */
};

union getquota_rslt switch (gqr_status status) {
case Q_OK:
	rquota gqr_rquota;	/* valid if status == Q_OK */
case Q_NOQUOTA:
	void;
case Q_EPERM:
	void;
};

union setquota_rslt switch (gqr_status status) {
case Q_OK:
	rquota sqr_rquota;	/* valid if status == Q_OK */
case Q_NOQUOTA:
	void;
case Q_EPERM:
	void;
};

program RQUOTAPROG {
	version RQUOTAVERS {
		/*
		 * Get all quotas
		 */
		getquota_rslt
		RQUOTAPROC_GETQUOTA(getquota_args) = 1;

		/*
	 	 * Get active quotas only
		 */
		getquota_rslt
		RQUOTAPROC_GETACTIVEQUOTA(getquota_args) = 2;

		/*
		 * Set all quotas
		 */
		setquota_rslt
		RQUOTAPROC_SETQUOTA(setquota_args) = 3;

		/*
	 	 * Get active quotas only
		 */
		setquota_rslt
		RQUOTAPROC_SETACTIVEQUOTA(setquota_args) = 4;
	} = 1;
	version EXT_RQUOTAVERS {
		/*
		 * Get all quotas
		 */
		getquota_rslt
		RQUOTAPROC_GETQUOTA(ext_getquota_args) = 1;

		/*
	 	 * Get active quotas only
		 */
		getquota_rslt
		RQUOTAPROC_GETACTIVEQUOTA(ext_getquota_args) = 2;

		/*
		 * Set all quotas
		 */
		setquota_rslt
		RQUOTAPROC_SETQUOTA(ext_setquota_args) = 3;

		/*
	 	 * Set active quotas only
		 */
		setquota_rslt
		RQUOTAPROC_SETACTIVEQUOTA(ext_setquota_args) = 4;
	} = 2;
} = 100011;
