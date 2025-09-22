/*	$OpenBSD: trap.c,v 1.43 2024/02/06 15:36:11 martijn Exp $	*/

/*
 * Copyright (c) 2008 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>

#include <ber.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "mib.h"
#include "smi.h"
#include "snmp.h"
#include "snmpd.h"

void
trap_init(void)
{
	/*
	 * Send a coldStart to notify that the daemon has been
	 * started and re-initialized.
	 */
	trap_send(&OID(MIB_coldStart), NULL);
}

int
trap_send(struct ber_oid *oid, struct ber_element *elm)
{
	struct trap_address	*tr;
	struct ber_element	*vblist, *trap;
	struct			 ber_oid uptime = OID(MIB_sysUpTime, 0);
	struct			 ber_oid trapoid = OID(MIB_snmpTrapOID, 0);
	char			 ostr[SNMP_MAX_OID_STRLEN];
	struct snmp_message	*msg;
	int			 r;

	if (TAILQ_EMPTY(&snmpd_env->sc_trapreceivers))
		return (0);

	mib_oid2string(oid, ostr, sizeof(ostr), snmpd_env->sc_oidfmt);
	log_debug("trap_send: oid %s", ostr);

	/* Add mandatory varbind elements */
	trap = ober_add_sequence(NULL);
	vblist = ober_printf_elements(trap, "{Odt}{OO}",
	    &uptime, smi_getticks(),
	    BER_CLASS_APPLICATION, SNMP_T_TIMETICKS,
	    &trapoid, oid);
	if (elm != NULL)
		ober_link_elements(vblist, elm);

	TAILQ_FOREACH(tr, &snmpd_env->sc_trapreceivers, entry) {
		if (tr->ta_oid.bo_n) {
			/* The trap receiver may want only a specified MIB */
			r = ober_oid_cmp(oid, &tr->ta_oid);
			if (r != 0 && r != 2)
				continue;
		}

		if ((msg = calloc(1, sizeof(*msg))) == NULL)
			fatal("malloc");
		msg->sm_sock = snmpd_socket_af(&tr->ta_ss, SOCK_DGRAM);
		if (msg->sm_sock == -1) {
			log_warn("socket");
			free(msg);
			continue;
		}
		memcpy(&(msg->sm_ss), &(tr->ta_ss), sizeof(msg->sm_ss));
		msg->sm_slen = tr->ta_ss.ss_len;
		if (tr->ta_sslocal.ss_family != 0) {
			memcpy(&(msg->sm_local_ss), &(tr->ta_sslocal),
			    sizeof(msg->sm_local_ss));
			msg->sm_local_slen = tr->ta_sslocal.ss_len;
		}
		msg->sm_version = tr->ta_version;
		msg->sm_pdutype = SNMP_C_TRAPV2;
		ober_set_application(&msg->sm_ber, smi_application);
		msg->sm_request = arc4random();
		if ((msg->sm_varbindresp = ober_dup(trap->be_sub)) == NULL)
			fatal("malloc");

		switch (msg->sm_version) {
		case SNMP_V2:
			(void)strlcpy(msg->sm_community, tr->ta_community,
			    sizeof(msg->sm_community));
			break;
		case SNMP_V3:
			msg->sm_msgid = msg->sm_request & INT32_MAX;
			msg->sm_max_msg_size = READ_BUF_SIZE;
			msg->sm_flags = tr->ta_seclevel != -1 ?
			    tr->ta_seclevel : snmpd_env->sc_min_seclevel;
			msg->sm_secmodel = SNMP_SEC_USM;
			msg->sm_engine_time = snmpd_engine_time();
			msg->sm_engine_boots = snmpd_env->sc_engine_boots;
			memcpy(msg->sm_ctxengineid, snmpd_env->sc_engineid,
			    snmpd_env->sc_engineid_len);
			msg->sm_ctxengineid_len =
			    snmpd_env->sc_engineid_len;
			(void)strlcpy(msg->sm_username, tr->ta_usmusername,
			    sizeof(msg->sm_username));
			msg->sm_user = tr->ta_usmuser;
			arc4random_buf(msg->sm_salt, sizeof(msg->sm_salt));
			break;
		}

		snmpe_response(msg);
	}
	ober_free_elements(trap);

	return 0;
}
