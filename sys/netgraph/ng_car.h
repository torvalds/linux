/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Nuno Antunes <nuno.antunes@gmail.com>
 * Copyright (c) 2007 Alexander Motin <mav@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_CAR_H_
#define _NETGRAPH_NG_CAR_H_

#define NG_CAR_NODE_TYPE	"car"
#define NGM_CAR_COOKIE		1173648034

/* Hook names */
#define NG_CAR_HOOK_UPPER	"upper"
#define NG_CAR_HOOK_LOWER	"lower"

/* Per hook statistics counters */
struct ng_car_hookstats {
	u_int64_t passed_pkts;	/* Counter for passed packets */
	u_int64_t droped_pkts;	/* Counter for droped packets */
	u_int64_t green_pkts;	/* Counter for green packets */
	u_int64_t yellow_pkts;	/* Counter for yellow packets */
	u_int64_t red_pkts;	/* Counter for red packets */
	u_int64_t errors;	/* Counter for operation errors */
};
#define NG_CAR_HOOKSTATS	{				\
	  { "passed",		&ng_parse_uint64_type	},	\
	  { "droped",		&ng_parse_uint64_type	},	\
	  { "green",		&ng_parse_uint64_type	},	\
	  { "yellow",		&ng_parse_uint64_type	},	\
	  { "red",		&ng_parse_uint64_type	},	\
	  { "errors",		&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Bulk statistics */
struct ng_car_bulkstats {
	struct ng_car_hookstats upstream;
	struct ng_car_hookstats downstream;
};
#define NG_CAR_BULKSTATS(hstatstype) {				\
	  { "upstream",		(hstatstype)		},	\
	  { "downstream",	(hstatstype)		},	\
	  { NULL }						\
}

/* Per hook configuration */
struct ng_car_hookconf {
	u_int64_t cbs;		/* Committed burst size (bytes) */
	u_int64_t ebs;		/* Exceeded/Peak burst size (bytes) */
	u_int64_t cir;		/* Committed information rate (bits/s) */
	u_int64_t pir;		/* Peak information rate (bits/s) */
	u_int8_t green_action;	/* Action for green packets */
	u_int8_t yellow_action;	/* Action for yellow packets */
	u_int8_t red_action;	/* Action for red packets */
	u_int8_t mode;		/* single/double rate, ... */
	u_int8_t opt;		/* color-aware or color-blind */
};
/* Keep this definition in sync with the above structure */
#define NG_CAR_HOOKCONF	{					\
	  { "cbs",		&ng_parse_uint64_type	},	\
	  { "ebs",		&ng_parse_uint64_type	},	\
	  { "cir",		&ng_parse_uint64_type	},	\
	  { "pir",		&ng_parse_uint64_type	},	\
	  { "greenAction",	&ng_parse_uint8_type	},	\
	  { "yellowAction",	&ng_parse_uint8_type	},	\
	  { "redAction",	&ng_parse_uint8_type	},	\
	  { "mode",		&ng_parse_uint8_type	},	\
	  { "opt",		&ng_parse_uint8_type	},	\
	  { NULL }						\
}

#define NG_CAR_CBS_MIN		8192
#define NG_CAR_EBS_MIN		8192
#define NG_CAR_CIR_DFLT		10240

/* possible actions (...Action) */
enum {
    NG_CAR_ACTION_FORWARD = 1,
    NG_CAR_ACTION_DROP,
    NG_CAR_ACTION_MARK,
    NG_CAR_ACTION_SET_TOS
};

/* operation modes (mode) */
enum {
    NG_CAR_SINGLE_RATE = 0,
    NG_CAR_DOUBLE_RATE,
    NG_CAR_RED,
    NG_CAR_SHAPE
};

/* mode options (opt) */
#define NG_CAR_COLOR_AWARE	1
#define NG_CAR_COUNT_PACKETS	2

/* Bulk config */
struct ng_car_bulkconf {
	struct ng_car_hookconf upstream;
	struct ng_car_hookconf downstream;
};
#define NG_CAR_BULKCONF(hconftype) {				\
	  { "upstream",		(hconftype)		},	\
	  { "downstream",	(hconftype)		},	\
	  { NULL }						\
}

/* Commands */
enum {
	NGM_CAR_GET_STATS = 1,		/* Get statistics */
	NGM_CAR_CLR_STATS,		/* Clear statistics */
	NGM_CAR_GETCLR_STATS,		/* Get and clear statistics */
	NGM_CAR_GET_CONF,		/* Get bulk configuration */
	NGM_CAR_SET_CONF,		/* Set bulk configuration */
};

#endif /* _NETGRAPH_NG_CAR_H_ */
