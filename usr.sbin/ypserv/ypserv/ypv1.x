/*
 * Copyright (c) 2003 Mats O Jansson <moj@stacken.kth.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * YP v1 access protocol
 *
 * Created by looking at ypv1_prot.h on AIX 5.1
 *
 */
#ifdef RPC_HDR
%#include <rpcsvc/yp.h>
#endif

enum ypreqtype {
        YPREQ_KEY = 1,
        YPREQ_NOKEY = 2,
        YPREQ_MAP_PARMS = 3
};

enum ypresptype {
	YPRESP_VAL = 1,
	YPRESP_KEY_VAL = 2,
	YPRESP_MAP_PARMS = 3
};

union yprequest switch (ypreqtype yp_reqtype) {
case YPREQ_KEY:
	ypreq_key yp_req_keytype;
case YPREQ_NOKEY:
	ypreq_nokey yp_req_nokeytype;
case YPREQ_MAP_PARMS:
	ypmap_parms yp_req_map_parmstype;
};

union ypresponse switch (ypresptype yp_resptype) {
case YPRESP_VAL:
	ypresp_val yp_resp_valtype;
case YPRESP_KEY_VAL:
	ypresp_key_val yp_resp_key_valtype;
case YPRESP_MAP_PARMS:
	ypmap_parms yp_resp_map_parmstype;
};

program YPPROG {
	version YPOLDVERS {

		void
		YPOLDPROC_NULL(void) = 0;

		bool_t
		YPOLDPROC_DOMAIN(domainname) = 1;

		bool_t
		YPOLDPROC_DOMAIN_NONACK(domainname) = 2;

		ypresponse
		YPOLDPROC_MATCH(yprequest) = 3;

		ypresponse
		YPOLDPROC_FIRST(yprequest) = 4;

		ypresponse
		YPOLDPROC_NEXT(yprequest) = 5;

		ypresponse
		YPOLDPROC_POLL(yprequest) = 6;

		void
		YPOLDPROC_PUSH(yprequest) = 7;

		void
		YPOLDPROC_PULL(yprequest) = 8;

		void
		YPOLDPROC_GET(yprequest) = 9;
	} = 1;
} = 100004;

#ifdef RPC_HDR
%#define YPMATCH_REQTYPE YPREQ_KEY
%#define ypmatch_req_domain yprequest_u.yp_req_keytype.domain
%#define ypmatch_req_map yprequest_u.yp_req_keytype.map
%#define ypmatch_req_keydat yprequest_u.yp_req_keytype.key
%#define ypmatch_req_keyptr yprequest_u.yp_req_keytype.key.keydat_val
%#define ypmatch_req_keysize yprequest_u.yp_req_keytype.key.keydat_len
%
%#define YPMATCH_RESPTYPE YPRESP_VAL
%#define ypmatch_resp_status ypresponse_u.yp_resp_valtype.stat
%#define ypmatch_resp_val ypresponse_u.yp_resp_valtype
%#define ypmatch_resp_valdat ypresponse_u.yp_resp_valtype.val
%#define ypmatch_resp_valptr ypresponse_u.yp_resp_valtype.val.valdat_val
%#define ypmatch_resp_valsize ypresponse_u.yp_resp_valtype.val.valdat_len
%
%#define YPFIRST_REQTYPE YPREQ_NOKEY
%#define ypfirst_req_domain yprequest_u.yp_req_nokeytype.domain
%#define ypfirst_req_map yprequest_u.yp_req_nokeytype.map
%
%#define YPFIRST_RESPTYPE YPRESP_KEY_VAL
%#define ypfirst_resp_status ypresponse_u.yp_resp_key_valtype.stat
%#define ypfirst_resp_keydat ypresponse_u.yp_resp_key_valtype.key
%#define ypfirst_resp_keyptr ypresponse_u.yp_resp_key_valtype.key.keydat_val
%#define ypfirst_resp_keysize ypresponse_u.yp_resp_key_valtype.key.keydat_len
%#define ypfirst_resp_val ypresponse_u.yp_resp_key_valtype
%#define ypfirst_resp_valdat ypresponse_u.yp_resp_key_valtype.val
%#define ypfirst_resp_valptr ypresponse_u.yp_resp_key_valtype.val.valdat_val
%#define ypfirst_resp_valsize ypresponse_u.yp_resp_key_valtype.val.valdat_len
%
%#define YPNEXT_REQTYPE YPREQ_KEY
%#define ypnext_req_domain yprequest_u.yp_req_keytype.domain
%#define ypnext_req_map yprequest_u.yp_req_keytype.map
%#define ypnext_req_keydat yprequest_u.yp_req_keytype.key
%#define ypnext_req_keyptr yprequest_u.yp_req_keytype.key.keydat_val
%#define ypnext_req_keysize yprequest_u.yp_req_keytype.key.keydat_len
%
%#define YPNEXT_RESPTYPE YPRESP_KEY_VAL
%#define ypnext_resp_status ypresponse_u.yp_resp_key_valtype.stat
%#define ypnext_resp_keydat ypresponse_u.yp_resp_key_valtype.key
%#define ypnext_resp_keyptr ypresponse_u.yp_resp_key_valtype.key.keydat_val
%#define ypnext_resp_keysize ypresponse_u.yp_resp_key_valtype.key.keydat_len
%#define ypnext_resp_val ypresponse_u.yp_resp_key_valtype
%#define ypnext_resp_valdat ypresponse_u.yp_resp_key_valtype.val
%#define ypnext_resp_valptr ypresponse_u.yp_resp_key_valtype.val.valdat_val
%#define ypnext_resp_valsize ypresponse_u.yp_resp_key_valtype.val.valdat_len
%
%#define YPPUSH_REQTYPE YPREQ_NOKEY
%#define yppush_req_domain yprequest_u.yp_req_nokeytype.domain
%#define yppush_req_map yprequest_u.yp_req_nokeytype.map
%
%#define YPPULL_REQTYPE YPREQ_NOKEY
%#define yppull_req_domain yprequest_u.yp_req_nokeytype.domain
%#define yppull_req_map yprequest_u.yp_req_nokeytype.map
%
%#define YPPOLL_REQTYPE YPREQ_NOKEY
%#define yppoll_req_domain yprequest_u.yp_req_nokeytype.domain
%#define yppoll_req_map yprequest_u.yp_req_nokeytype.map
%
%#define YPPOLL_RESPTYPE YPRESP_MAP_PARMS
%#define yppoll_resp_domain ypresponse_u.yp_resp_map_parmstype.domain
%#define yppoll_resp_map ypresponse_u.yp_resp_map_parmstype.map
%#define yppoll_resp_ordernum ypresponse_u.yp_resp_map_parmstype.ordernum
%#define yppoll_resp_owner ypresponse_u.yp_resp_map_parmstype.peer
%
%#define YPGET_REQTYPE YPREQ_MAP_PARMS
%#define ypget_req_domain yprequest_u.yp_req_map_parmstype.domain
%#define ypget_req_map yprequest_u.yp_req_map_parmstype.map
%#define ypget_req_ordernum yprequest_u.yp_req_map_parmstype.ordernum
%#define ypget_req_owner yprequest_u.yp_req_map_parmstype.peer
#endif
