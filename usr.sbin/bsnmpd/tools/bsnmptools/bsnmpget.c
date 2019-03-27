/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Shteryana Shopova <syrinx@FreeBSD.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Bsnmpget and bsnmpwalk are simple tools for querying SNMP agents,
 * bsnmpset can be used to set MIB objects in an agent.
 *
 * $FreeBSD$
 */

#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <bsnmp/asn1.h>
#include <bsnmp/snmp.h>
#include <bsnmp/snmpclient.h>
#include "bsnmptc.h"
#include "bsnmptools.h"

static const char *program_name = NULL;
static enum program_e {
	BSNMPGET,
	BSNMPWALK,
	BSNMPSET
} program;

/* *****************************************************************************
 * Common bsnmptools functions.
 */
static void
usage(void)
{
	fprintf(stderr,
"Usage:\n"
"%s %s [-A options] [-b buffersize] [-C options] [-I options]\n"
"\t[-i filelist] [-l filename]%s [-o output] [-P options]\n"
"\t%s[-r retries] [-s [trans::][community@][server][:port]]\n"
"\t[-t timeout] [-U options] [-v version]%s\n",
	program_name,
	(program == BSNMPGET) ? "[-aDdehnK]" :
	    (program == BSNMPWALK) ? "[-dhnK]" :
	    (program == BSNMPSET) ? "[-adehnK]" :
	    "",
	(program == BSNMPGET || program == BSNMPWALK) ?
	" [-M max-repetitions] [-N non-repeaters]" : "",
	(program == BSNMPGET || program == BSNMPWALK) ? "[-p pdu] " : "",
	(program == BSNMPGET) ? " OID [OID ...]" :
	    (program == BSNMPWALK || program == BSNMPSET) ? " [OID ...]" :
	    ""
	);
}

static int32_t
parse_max_repetitions(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	uint32_t v;

	assert(opt_arg != NULL);

	v = strtoul(opt_arg, (void *) NULL, 10);

	if (v > SNMP_MAX_BINDINGS) {
		warnx("Max repetitions value greater than %d maximum allowed.",
		    SNMP_MAX_BINDINGS);
		return (-1);
	}

	SET_MAXREP(snmptoolctx, v);
	return (2);
}

static int32_t
parse_non_repeaters(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	uint32_t v;

	assert(opt_arg != NULL);

	v = strtoul(opt_arg, (void *) NULL, 10);

	if (v > SNMP_MAX_BINDINGS) {
		warnx("Non repeaters value greater than %d maximum allowed.",
		    SNMP_MAX_BINDINGS);
		return (-1);
	}

	SET_NONREP(snmptoolctx, v);
	return (2);
}

static int32_t
parse_pdu_type(struct snmp_toolinfo *snmptoolctx, char *opt_arg)
{
	assert(opt_arg != NULL);

	if (strcasecmp(opt_arg, "getbulk") == 0)
		SET_PDUTYPE(snmptoolctx, SNMP_PDU_GETBULK);
	else if (strcasecmp(opt_arg, "getnext") == 0)
		SET_PDUTYPE(snmptoolctx, SNMP_PDU_GETNEXT);
	else if (strcasecmp(opt_arg, "get") == 0)
		SET_PDUTYPE(snmptoolctx, SNMP_PDU_GET);
	else {
		warnx("PDU type '%s' not supported.", opt_arg);
		return (-1);
	}

	return (2);
}

static int32_t
snmptool_parse_options(struct snmp_toolinfo *snmptoolctx, int argc, char **argv)
{
	int32_t count, optnum = 0;
	int ch;
	const char *opts;

	switch (program) {
		case BSNMPWALK:
			opts = "dhnKA:b:C:I:i:l:M:N:o:P:p:r:s:t:U:v:";
			break;
		case BSNMPGET:
			opts = "aDdehnKA:b:C:I:i:l:M:N:o:P:p:r:s:t:U:v:";
			break;
		case BSNMPSET:
			opts = "adehnKA:b:C:I:i:l:o:P:r:s:t:U:v:";
			break;
		default:
			return (-1);
	}

	while ((ch = getopt(argc, argv, opts)) != EOF) {
		switch (ch) {
		case 'A':
			count = parse_authentication(snmptoolctx, optarg);
			break;
		case 'a':
			count = parse_skip_access(snmptoolctx);
			break;
		case 'b':
			count = parse_buflen(optarg);
			break;
		case 'D':
			count = parse_discovery(snmptoolctx);
			break;
		case 'd':
			count = parse_debug();
			break;
		case 'e':
			count = parse_errors(snmptoolctx);
			break;
		case 'h':
			usage();
			return (-2);
		case 'C':
			count = parse_context(snmptoolctx, optarg);
			break;
		case 'I':
			count = parse_include(snmptoolctx, optarg);
			break;
		case 'i':
			count = parse_file(snmptoolctx, optarg);
			break;
		case 'K':
			count = parse_local_key(snmptoolctx);
			break;
		case 'l':
			count = parse_local_path(optarg);
			break;
		case 'M':
			count = parse_max_repetitions(snmptoolctx, optarg);
			break;
		case 'N':
			count = parse_non_repeaters(snmptoolctx, optarg);
			break;
		case 'n':
			count = parse_num_oids(snmptoolctx);
			break;
		case 'o':
			count = parse_output(snmptoolctx, optarg);
			break;
		case 'P':
			count = parse_privacy(snmptoolctx, optarg);
			break;
		case 'p':
			count = parse_pdu_type(snmptoolctx, optarg);
			break;
		case 'r':
			count = parse_retry(optarg);
			break;
		case 's':
			count = parse_server(optarg);
			break;
		case 't':
			count = parse_timeout(optarg);
			break;
		case 'U':
			count = parse_user_security(snmptoolctx, optarg);
			break;
		case 'v':
			count = parse_version(optarg);
			break;
		case '?':
		default:
			usage();
			return (-1);
		}
		if (count < 0)
			return (-1);
	    optnum += count;
	}

	return (optnum);
}

/*
 * Read user input OID - one of following formats:
 * 1) 1.2.1.1.2.1.0 - that is if option numeric was given;
 * 2) string - in such case append .0 to the asn_oid subs;
 * 3) string.1 - no additional processing required in such case.
 */
static char *
snmptools_parse_stroid(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *obj, char *argv)
{
	char string[MAXSTR], *str;
	int32_t i = 0;
	struct asn_oid in_oid;

	str = argv;

	if (*str == '.')
		str++;

	while (isalpha(*str) || *str == '_' || (i != 0 && isdigit(*str))) {
		str++;
		i++;
	}

	if (i <= 0 || i >= MAXSTR)
		return (NULL);

	memset(&in_oid, 0, sizeof(struct asn_oid));
	if ((str = snmp_parse_suboid((argv + i), &in_oid)) == NULL) {
		warnx("Invalid OID - %s", argv);
		return (NULL);
	}

	strlcpy(string, argv, i + 1);
	if (snmp_lookup_oidall(snmptoolctx, obj, string) < 0) {
		warnx("No entry for %s in mapping lists", string);
		return (NULL);
	}

	/* If OID given on command line append it. */
	if (in_oid.len > 0)
		asn_append_oid(&(obj->val.var), &in_oid);
	else if (*str == '[') {
		if ((str = snmp_parse_index(snmptoolctx, str + 1, obj)) == NULL)
			return (NULL);
	} else if (obj->val.syntax > 0 && GET_PDUTYPE(snmptoolctx) ==
	    SNMP_PDU_GET) {
		if (snmp_suboid_append(&(obj->val.var), (asn_subid_t) 0) < 0)
			return (NULL);
	}

	return (str);
}

static int32_t
snmptools_parse_oid(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *obj, char *argv)
{
	if (argv == NULL)
		return (-1);

	if (ISSET_NUMERIC(snmptoolctx)) {
		if (snmp_parse_numoid(argv, &(obj->val.var)) < 0)
			return (-1);
	} else {
		if (snmptools_parse_stroid(snmptoolctx, obj, argv) == NULL &&
		    snmp_parse_numoid(argv, &(obj->val.var)) < 0)
			return (-1);
	}

	return (1);
}

static int32_t
snmptool_add_vbind(struct snmp_pdu *pdu, struct snmp_object *obj)
{
	if (obj->error > 0)
		return (0);

	asn_append_oid(&(pdu->bindings[pdu->nbindings].var), &(obj->val.var));
	pdu->nbindings++;

	return (pdu->nbindings);
}

/* *****************************************************************************
 * bsnmpget private functions.
 */
static int32_t
snmpget_verify_vbind(struct snmp_toolinfo *snmptoolctx, struct snmp_pdu *pdu,
    struct snmp_object *obj)
{
	if (pdu->version == SNMP_V1 && obj->val.syntax ==
	    SNMP_SYNTAX_COUNTER64) {
		warnx("64-bit counters are not supported in SNMPv1 PDU");
		return (-1);
	}

	if (ISSET_NUMERIC(snmptoolctx) || pdu->type == SNMP_PDU_GETNEXT ||
	    pdu->type == SNMP_PDU_GETBULK)
		return (1);

	if (pdu->type == SNMP_PDU_GET && obj->val.syntax == SNMP_SYNTAX_NULL) {
		warnx("Only leaf object values can be added to GET PDU");
		return (-1);
	}

	return (1);
}

/*
 * In case of a getbulk PDU, the error_status and error_index fields are used by
 * libbsnmp to hold the values of the non-repeaters and max-repetitions fields
 * that are present only in the getbulk - so before sending the PDU make sure
 * these have correct values as well.
 */
static void
snmpget_fix_getbulk(struct snmp_pdu *pdu, uint32_t max_rep, uint32_t non_rep)
{
	assert(pdu != NULL);

	if (pdu->nbindings < non_rep)
		pdu->error_status = pdu->nbindings;
	else
		pdu->error_status = non_rep;

	if (max_rep > 0)
		pdu->error_index = max_rep;
	else
		pdu->error_index = 1;
}

static int
snmptool_get(struct snmp_toolinfo *snmptoolctx)
{
	struct snmp_pdu req, resp;

	snmp_pdu_create(&req, GET_PDUTYPE(snmptoolctx));

	while ((snmp_pdu_add_bindings(snmptoolctx, snmpget_verify_vbind,
	     snmptool_add_vbind, &req, SNMP_MAX_BINDINGS)) > 0) {

		if (GET_PDUTYPE(snmptoolctx) == SNMP_PDU_GETBULK)
			snmpget_fix_getbulk(&req, GET_MAXREP(snmptoolctx),
			    GET_NONREP(snmptoolctx));

		if (snmp_dialog(&req, &resp) == -1) {
			warn("Snmp dialog");
			break;
		}

		if (snmp_parse_resp(&resp, &req) >= 0) {
			snmp_output_resp(snmptoolctx, &resp, NULL);
			snmp_pdu_free(&resp);
			break;
		}

		snmp_output_err_resp(snmptoolctx, &resp);
		if (GET_PDUTYPE(snmptoolctx) == SNMP_PDU_GETBULK ||
		    !ISSET_RETRY(snmptoolctx)) {
			snmp_pdu_free(&resp);
			break;
		}

		/*
		 * Loop through the object list and set object->error to the
		 * varbinding that caused the error.
		 */
		if (snmp_object_seterror(snmptoolctx,
		    &(resp.bindings[resp.error_index - 1]),
		    resp.error_status) <= 0) {
			snmp_pdu_free(&resp);
			break;
		}

		fprintf(stderr, "Retrying...\n");
		snmp_pdu_free(&resp);
		snmp_pdu_create(&req, GET_PDUTYPE(snmptoolctx));
	}

	snmp_pdu_free(&req);

	return (0);
}


/* *****************************************************************************
 * bsnmpwalk private functions.
 */
/* The default tree to walk. */
static const struct asn_oid snmp_mibII_OID = {
	6 , { 1, 3, 6, 1, 2, 1 }
};

static int32_t
snmpwalk_add_default(struct snmp_toolinfo *snmptoolctx __unused,
    struct snmp_object *obj, char *string __unused)
{
	asn_append_oid(&(obj->val.var), &snmp_mibII_OID);
	return (1);
}

/*
 * Prepare the next GetNext/Get PDU to send.
 */
static void
snmpwalk_nextpdu_create(uint32_t op, struct asn_oid *var, struct snmp_pdu *pdu)
{
	snmp_pdu_create(pdu, op);
	asn_append_oid(&(pdu->bindings[0].var), var);
	pdu->nbindings = 1;
}

static int
snmptool_walk(struct snmp_toolinfo *snmptoolctx)
{
	struct snmp_pdu req, resp;
	struct asn_oid root;	/* Keep the initial oid. */
	int32_t outputs, rc;
	uint32_t op;

	if (GET_PDUTYPE(snmptoolctx) == SNMP_PDU_GETBULK)
		op = SNMP_PDU_GETBULK;
	else
		op = SNMP_PDU_GETNEXT;

	snmp_pdu_create(&req, op);

	while ((rc = snmp_pdu_add_bindings(snmptoolctx, NULL,
	    snmptool_add_vbind, &req, 1)) > 0) {

		/* Remember the root where the walk started from. */
		memset(&root, 0, sizeof(struct asn_oid));
		asn_append_oid(&root, &(req.bindings[0].var));

		if (op == SNMP_PDU_GETBULK)
			snmpget_fix_getbulk(&req, GET_MAXREP(snmptoolctx),
			    GET_NONREP(snmptoolctx));

		outputs = 0;
		while (snmp_dialog(&req, &resp) >= 0) {
			if ((snmp_parse_resp(&resp, &req)) < 0) {
				snmp_output_err_resp(snmptoolctx, &resp);
				snmp_pdu_free(&resp);
				outputs = -1;
				break;
			}

			rc = snmp_output_resp(snmptoolctx, &resp, &root);
			if (rc < 0) {
				snmp_pdu_free(&resp);
				outputs = -1;
				break;
			}

			outputs += rc;

			if ((u_int)rc < resp.nbindings) {
				snmp_pdu_free(&resp);
				break;
			}

			snmpwalk_nextpdu_create(op,
			    &(resp.bindings[resp.nbindings - 1].var), &req);
			if (op == SNMP_PDU_GETBULK)
				snmpget_fix_getbulk(&req, GET_MAXREP(snmptoolctx),
				    GET_NONREP(snmptoolctx));
			snmp_pdu_free(&resp);
		}

		/* Just in case our root was a leaf. */
		if (outputs == 0) {
			snmpwalk_nextpdu_create(SNMP_PDU_GET, &root, &req);
			if (snmp_dialog(&req, &resp) == SNMP_CODE_OK) {
				if (snmp_parse_resp(&resp, &req) < 0)
					snmp_output_err_resp(snmptoolctx, &resp);
				else
					snmp_output_resp(snmptoolctx, &resp,
					    NULL);
				snmp_pdu_free(&resp);
			} else
				warn("Snmp dialog");
		}

		if (snmp_object_remove(snmptoolctx, &root) < 0) {
			warnx("snmp_object_remove");
			break;
		}

		snmp_pdu_free(&req);
		snmp_pdu_create(&req, op);
	}

	snmp_pdu_free(&req);

	if (rc == 0)
		return (0);
	else
		return (1);
}

/* *****************************************************************************
 * bsnmpset private functions.
 */

static int32_t
parse_oid_numeric(struct snmp_value *value, char *val)
{
	char *endptr;
	int32_t saved_errno;
	asn_subid_t suboid;

	do {
		saved_errno = errno;
		errno = 0;
		suboid = strtoul(val, &endptr, 10);
		if (errno != 0) {
			warn("Value %s not supported", val);
			errno = saved_errno;
			return (-1);
		}
		errno = saved_errno;
		if ((asn_subid_t) suboid > ASN_MAXID) {
			warnx("Suboid %u > ASN_MAXID", suboid);
			return (-1);
		}
		if (snmp_suboid_append(&(value->v.oid), suboid) < 0)
			return (-1);
		val = endptr + 1;
	} while (*endptr == '.');

	if (*endptr != '\0')
		warnx("OID value %s not supported", val);

	value->syntax = SNMP_SYNTAX_OID;
	return (0);
}

/*
 * Allow OID leaf in both forms:
 * 1) 1.3.6.1.2... ->  in such case call directly the function reading raw OIDs;
 * 2) begemotSnmpdAgentFreeBSD -> lookup the ASN OID corresponding to that.
 */
static int32_t
parse_oid_string(struct snmp_toolinfo *snmptoolctx,
    struct snmp_value *value, char *string)
{
	struct snmp_object obj;

	if (isdigit(string[0]))
		return (parse_oid_numeric(value, string));

	memset(&obj, 0, sizeof(struct snmp_object));
	if (snmp_lookup_enumoid(snmptoolctx, &obj, string) < 0) {
		warnx("Unknown OID enum string - %s", string);
		return (-1);
	}

	asn_append_oid(&(value->v.oid), &(obj.val.var));
	return (1);
}

static int32_t
parse_ip(struct snmp_value * value, char * val)
{
	char *endptr, *str;
	int32_t i;
	uint32_t v;

	str = val;
	for (i = 0; i < 4; i++) {
		v = strtoul(str, &endptr, 10);
		if (v > 0xff)
			return (-1);
		if (*endptr != '.' && *endptr != '\0' && i != 3)
			break;
		str = endptr + 1;
		value->v.ipaddress[i] = (uint8_t) v;
	}
	value->syntax = SNMP_SYNTAX_IPADDRESS;

	return (0);
}

static int32_t
parse_int(struct snmp_value *value, char *val)
{
	char *endptr;
	int32_t v, saved_errno;

	saved_errno = errno;
	errno = 0;

	v = strtol(val, &endptr, 10);

	if (errno != 0) {
		warn("Value %s not supported", val);
		errno = saved_errno;
		return (-1);
	}

	value->syntax = SNMP_SYNTAX_INTEGER;
	value->v.integer = v;
	errno = saved_errno;

	return (0);
}

static int32_t
parse_int_string(struct snmp_object *object, char *val)
{
	int32_t	v;

	if (isdigit(val[0]))
		return ((parse_int(&(object->val), val)));

	if (object->info == NULL) {
		warnx("Unknown enumerated integer type - %s", val);
		return (-1);
	}
	if ((v = enum_number_lookup(object->info->snmp_enum, val)) < 0)
		warnx("Unknown enumerated integer type - %s", val);

	object->val.v.integer = v;
	return (1);
}

/*
 * Here syntax may be one of SNMP_SYNTAX_COUNTER, SNMP_SYNTAX_GAUGE,
 * SNMP_SYNTAX_TIMETICKS.
 */
static int32_t
parse_uint(struct snmp_value *value, char *val)
{
	char *endptr;
	uint32_t v = 0;
	int32_t saved_errno;

	saved_errno = errno;
	errno = 0;

	v = strtoul(val, &endptr, 10);

	if (errno != 0) {
		warn("Value %s not supported", val);
		errno = saved_errno;
		return (-1);
	}

	value->v.uint32 = v;
	errno = saved_errno;

	return (0);
}

static int32_t
parse_ticks(struct snmp_value *value, char *val)
{
	if (parse_uint(value, val) < 0)
		return (-1);

	value->syntax = SNMP_SYNTAX_TIMETICKS;
	return (0);
}

static int32_t
parse_gauge(struct snmp_value *value, char *val)
{
	if (parse_uint(value, val) < 0)
		return (-1);

	value->syntax = SNMP_SYNTAX_GAUGE;
	return (0);
}

static int32_t
parse_counter(struct snmp_value *value, char *val)
{
	if (parse_uint(value, val) < 0)
		return (-1);

	value->syntax = SNMP_SYNTAX_COUNTER;
	return (0);
}

static int32_t
parse_uint64(struct snmp_value *value, char *val)
{
	char *endptr;
	int32_t saved_errno;
	uint64_t v;

	saved_errno = errno;
	errno = 0;

	v = strtoull(val, &endptr, 10);

	if (errno != 0) {
		warnx("Value %s not supported", val);
		errno = saved_errno;
		return (-1);
	}

	value->syntax = SNMP_SYNTAX_COUNTER64;
	value->v.counter64 = v;
	errno = saved_errno;

	return (0);
}

static int32_t
parse_syntax_val(struct snmp_value *value, enum snmp_syntax syntax, char *val)
{
	switch (syntax) {
		case SNMP_SYNTAX_INTEGER:
			return (parse_int(value, val));
		case SNMP_SYNTAX_IPADDRESS:
			return (parse_ip(value, val));
		case SNMP_SYNTAX_COUNTER:
			return (parse_counter(value, val));
		case SNMP_SYNTAX_GAUGE:
			return (parse_gauge(value, val));
		case SNMP_SYNTAX_TIMETICKS:
			return (parse_ticks(value, val));
		case SNMP_SYNTAX_COUNTER64:
			return (parse_uint64(value, val));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_tc2oct(SNMP_STRING, value, val));
		case SNMP_SYNTAX_OID:
			return (parse_oid_numeric(value, val));
		default:
			/* NOTREACHED */
			break;
	}

	return (-1);
}

/*
 * Parse a command line argument of type OID=syntax:value and fill in whatever
 * fields can be derived from the input into snmp_value structure. Reads numeric
 * OIDs.
 */
static int32_t
parse_pair_numoid_val(char *str, struct snmp_value *snmp_val)
{
	int32_t cnt;
	char *ptr;
	enum snmp_syntax syntax;
	char oid_str[ASN_OIDSTRLEN];

	ptr = str;
	for (cnt = 0; cnt < ASN_OIDSTRLEN; cnt++)
		if (ptr[cnt] == '=')
			break;

	if (cnt >= ASN_OIDSTRLEN) {
		warnx("OID too long - %s", str);
		return (-1);
	}
	strlcpy(oid_str, ptr, (size_t) (cnt + 1));

	ptr = str + cnt + 1;
	for (cnt = 0; cnt < MAX_CMD_SYNTAX_LEN; cnt++)
		if(ptr[cnt] == ':')
			break;

	if (cnt >= MAX_CMD_SYNTAX_LEN) {
		warnx("Unknown syntax in OID - %s", str);
		return (-1);
	}

	if ((syntax = parse_syntax(ptr)) <= SNMP_SYNTAX_NULL) {
		warnx("Unknown syntax in OID - %s", ptr);
		return (-1);
	}

	ptr = ptr + cnt + 1;
	for (cnt = 0; cnt < MAX_OCTSTRING_LEN; cnt++)
		if (ptr[cnt] == '\0')
			break;

	if (ptr[cnt] != '\0') {
		warnx("Value string too long - %s", ptr);
		return (-1);
	}

	/*
	 * Here try parsing the OIDs and syntaxes and then check values - have
	 * to know syntax to check value boundaries.
	 */
	if (snmp_parse_numoid(oid_str, &(snmp_val->var)) < 0) {
		warnx("Error parsing OID %s", oid_str);
		return (-1);
	}

	if (parse_syntax_val(snmp_val, syntax, ptr) < 0)
		return (-1);

	return (1);
}

static int32_t
parse_syntax_strval(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *object, char *str)
{
	uint32_t len;
	enum snmp_syntax syn;

	/*
	 * Syntax string here not required  - still may be present.
	 */

	if (GET_OUTPUT(snmptoolctx) == OUTPUT_VERBOSE) {
		for (len = 0 ; *(str + len) != ':'; len++) {
			if (*(str + len) == '\0') {
				warnx("Syntax missing in value - %s", str);
				return (-1);
			}
		}
		if ((syn = parse_syntax(str)) <= SNMP_SYNTAX_NULL) {
			warnx("Unknown syntax in - %s", str);
			return (-1);
		}
		if (syn != object->val.syntax) {
			if (!ISSET_ERRIGNORE(snmptoolctx)) {
				warnx("Bad syntax in - %s", str);
				return (-1);
			} else
				object->val.syntax = syn;
		}
		len++;
	} else
		len = 0;

	switch (object->val.syntax) {
		case SNMP_SYNTAX_INTEGER:
			return (parse_int_string(object, str + len));
		case SNMP_SYNTAX_IPADDRESS:
			return (parse_ip(&(object->val), str + len));
		case SNMP_SYNTAX_COUNTER:
			return (parse_counter(&(object->val), str + len));
		case SNMP_SYNTAX_GAUGE:
			return (parse_gauge(&(object->val), str + len));
		case SNMP_SYNTAX_TIMETICKS:
			return (parse_ticks(&(object->val), str + len));
		case SNMP_SYNTAX_COUNTER64:
			return (parse_uint64(&(object->val), str + len));
		case SNMP_SYNTAX_OCTETSTRING:
			return (snmp_tc2oct(object->info->tc, &(object->val),
			    str + len));
		case SNMP_SYNTAX_OID:
			return (parse_oid_string(snmptoolctx, &(object->val),
			    str + len));
		default:
			/* NOTREACHED */
			break;
	}

	return (-1);
}

static int32_t
parse_pair_stroid_val(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *obj, char *argv)
{
	char *ptr;

	if ((ptr = snmptools_parse_stroid(snmptoolctx, obj, argv)) == NULL)
		return (-1);

	if (*ptr != '=') {
		warnx("Value to set expected after OID");
		return (-1);
	}

	if (parse_syntax_strval(snmptoolctx, obj, ptr + 1) < 0)
		return (-1);

	return (1);
}


static int32_t
snmpset_parse_oid(struct snmp_toolinfo *snmptoolctx,
    struct snmp_object *obj, char *argv)
{
	if (argv == NULL)
		return (-1);

	if (ISSET_NUMERIC(snmptoolctx)) {
		if (parse_pair_numoid_val(argv, &(obj->val)) < 0)
			return (-1);
	} else {
		if (parse_pair_stroid_val(snmptoolctx, obj, argv) < 0)
			return (-1);
	}

	return (1);
}

static int32_t
add_ip_syntax(struct snmp_value *dst, struct snmp_value *src)
{
	int8_t i;

	dst->syntax = SNMP_SYNTAX_IPADDRESS;
	for (i = 0; i < 4; i++)
		dst->v.ipaddress[i] = src->v.ipaddress[i];

	return (1);
}

static int32_t
add_octstring_syntax(struct snmp_value *dst, struct snmp_value *src)
{
	if (src->v.octetstring.len > ASN_MAXOCTETSTRING) {
		warnx("OctetString len too big - %u", src->v.octetstring.len);
		return (-1);
	}

	if ((dst->v.octetstring.octets = malloc(src->v.octetstring.len)) ==
	    NULL) {
		syslog(LOG_ERR, "malloc() failed - %s", strerror(errno));
		return (-1);
	}

	memcpy(dst->v.octetstring.octets, src->v.octetstring.octets,
	    src->v.octetstring.len);
	dst->syntax = SNMP_SYNTAX_OCTETSTRING;
	dst->v.octetstring.len = src->v.octetstring.len;

	return(0);
}

static int32_t
add_oid_syntax(struct snmp_value *dst, struct snmp_value *src)
{
	asn_append_oid(&(dst->v.oid), &(src->v.oid));
	dst->syntax = SNMP_SYNTAX_OID;
	return (0);
}

/*
 * Check syntax - if one of SNMP_SYNTAX_NULL, SNMP_SYNTAX_NOSUCHOBJECT,
 * SNMP_SYNTAX_NOSUCHINSTANCE, SNMP_SYNTAX_ENDOFMIBVIEW or anything not known -
 * return error.
 */
static int32_t
snmpset_add_value(struct snmp_value *dst, struct snmp_value *src)
{
	if (dst == NULL || src == NULL)
		return (-1);

	switch (src->syntax) {
		case SNMP_SYNTAX_INTEGER:
			dst->v.integer = src->v.integer;
			dst->syntax = SNMP_SYNTAX_INTEGER;
			break;
		case SNMP_SYNTAX_TIMETICKS:
			dst->v.uint32 = src->v.uint32;
			dst->syntax = SNMP_SYNTAX_TIMETICKS;
			break;
		case SNMP_SYNTAX_GAUGE:
			dst->v.uint32 = src->v.uint32;
			dst->syntax = SNMP_SYNTAX_GAUGE;
			break;
		case SNMP_SYNTAX_COUNTER:
			dst->v.uint32 = src->v.uint32;
			dst->syntax = SNMP_SYNTAX_COUNTER;
			break;
		case SNMP_SYNTAX_COUNTER64:
			dst->v.counter64 = src->v.counter64;
			dst->syntax = SNMP_SYNTAX_COUNTER64;
			break;
		case SNMP_SYNTAX_IPADDRESS:
			add_ip_syntax(dst, src);
			break;
		case SNMP_SYNTAX_OCTETSTRING:
			add_octstring_syntax(dst, src);
			break;
		case SNMP_SYNTAX_OID:
			add_oid_syntax(dst, src);
			break;
		default:
			warnx("Unknown syntax %d", src->syntax);
			return (-1);
	}

	return (0);
}

static int32_t
snmpset_verify_vbind(struct snmp_toolinfo *snmptoolctx, struct snmp_pdu *pdu,
    struct snmp_object *obj)
{
	if (pdu->version == SNMP_V1 && obj->val.syntax ==
	    SNMP_SYNTAX_COUNTER64) {
		warnx("64-bit counters are not supported in SNMPv1 PDU");
		return (-1);
	}

	if (ISSET_NUMERIC(snmptoolctx) || ISSET_ERRIGNORE(snmptoolctx))
		return (1);

	if (obj->info->access < SNMP_ACCESS_SET) {
		warnx("Object %s not accessible for set - try 'bsnmpset -a'",
		    obj->info->string);
		return (-1);
	}

	return (1);
}

static int32_t
snmpset_add_vbind(struct snmp_pdu *pdu, struct snmp_object *obj)
{
	if (pdu->nbindings > SNMP_MAX_BINDINGS) {
		warnx("Too many OIDs for one PDU");
		return (-1);
	}

	if (obj->error > 0)
		return (0);

	if (snmpset_add_value(&(pdu->bindings[pdu->nbindings]), &(obj->val))
	    < 0)
		return (-1);

	asn_append_oid(&(pdu->bindings[pdu->nbindings].var), &(obj->val.var));
	pdu->nbindings++;

	return (pdu->nbindings);
}

static int
snmptool_set(struct snmp_toolinfo *snmptoolctx)
{
	struct snmp_pdu req, resp;

	snmp_pdu_create(&req, SNMP_PDU_SET);

	while ((snmp_pdu_add_bindings(snmptoolctx, snmpset_verify_vbind,
	    snmpset_add_vbind, &req, SNMP_MAX_BINDINGS)) > 0) {
		if (snmp_dialog(&req, &resp)) {
			warn("Snmp dialog");
			break;
		}

		if (snmp_pdu_check(&req, &resp) > 0) {
			if (GET_OUTPUT(snmptoolctx) != OUTPUT_QUIET)
				snmp_output_resp(snmptoolctx, &resp, NULL);
			snmp_pdu_free(&resp);
			break;
		}

		snmp_output_err_resp(snmptoolctx, &resp);
		if (!ISSET_RETRY(snmptoolctx)) {
			snmp_pdu_free(&resp);
			break;
		}

		if (snmp_object_seterror(snmptoolctx,
		    &(resp.bindings[resp.error_index - 1]),
		    resp.error_status) <= 0) {
			snmp_pdu_free(&resp);
			break;
		}

		fprintf(stderr, "Retrying...\n");
		snmp_pdu_free(&req);
		snmp_pdu_create(&req, SNMP_PDU_SET);
	}

	snmp_pdu_free(&req);

	return (0);
}

/* *****************************************************************************
 * main
 */
/*
 * According to command line options prepare SNMP Get | GetNext | GetBulk PDU.
 * Wait for a response and print it.
 */
/*
 * Do a 'snmp walk' - according to command line options request for values
 * lexicographically subsequent and subrooted at a common node. Send a GetNext
 * PDU requesting the value for each next variable and print the response. Stop
 * when a Response PDU is received that contains the value of a variable not
 * subrooted at the variable the walk started.
 */
int
main(int argc, char ** argv)
{
	struct snmp_toolinfo snmptoolctx;
	int32_t oid_cnt, last_oid, opt_num;
	int rc = 0;

	/* Make sure program_name is set and valid. */
	if (*argv == NULL)
		program_name = "snmptool";
	else {
		program_name = strrchr(*argv, '/');
		if (program_name != NULL)
			program_name++;
		else
			program_name = *argv;
	}

	if (program_name == NULL) {
		fprintf(stderr, "Error: No program name?\n");
		exit (1);
	} else if (strcmp(program_name, "bsnmpget") == 0)
		program = BSNMPGET;
	else if (strcmp(program_name, "bsnmpwalk") == 0)
		program = BSNMPWALK;
	else if (strcmp(program_name, "bsnmpset") == 0)
		program = BSNMPSET;
	else {
		fprintf(stderr, "Unknown snmp tool name '%s'.\n", program_name);
		exit (1);
	}

	/* Initialize. */
	if (snmptool_init(&snmptoolctx) < 0)
		exit (1);

	if ((opt_num = snmptool_parse_options(&snmptoolctx, argc, argv)) < 0) {
		snmp_tool_freeall(&snmptoolctx);
		/* On -h (help) exit without error. */
		if (opt_num == -2)
			exit(0);
		else
			exit(1);
	}

	oid_cnt = argc - opt_num - 1;
	if (oid_cnt == 0) {
		switch (program) {
		case BSNMPGET:
			if (!ISSET_EDISCOVER(&snmptoolctx) &&
			    !ISSET_LOCALKEY(&snmptoolctx)) {
				fprintf(stderr, "No OID given.\n");
				usage();
				snmp_tool_freeall(&snmptoolctx);
				exit(1);
			}
			break;

		case BSNMPWALK:
			if (snmp_object_add(&snmptoolctx, snmpwalk_add_default,
			    NULL) < 0) {
				fprintf(stderr,
				    "Error setting default subtree.\n");
				snmp_tool_freeall(&snmptoolctx);
				exit(1);
			}
			break;

		case BSNMPSET:
			fprintf(stderr, "No OID given.\n");
			usage();
			snmp_tool_freeall(&snmptoolctx);
			exit(1);
		}
	}

	if (snmp_import_all(&snmptoolctx) < 0) {
		snmp_tool_freeall(&snmptoolctx);
		exit(1);
	}

	/* A simple sanity check - can not send GETBULK when using SNMPv1. */
	if (program == BSNMPGET && snmp_client.version == SNMP_V1 &&
	    GET_PDUTYPE(&snmptoolctx) == SNMP_PDU_GETBULK) {
		fprintf(stderr, "Cannot send GETBULK PDU with SNMPv1.\n");
		snmp_tool_freeall(&snmptoolctx);
		exit(1);
	}

	for (last_oid = argc - 1; oid_cnt > 0; last_oid--, oid_cnt--) {
		if ((snmp_object_add(&snmptoolctx, (program == BSNMPSET) ?
		    snmpset_parse_oid : snmptools_parse_oid,
		    argv[last_oid])) < 0) {
			fprintf(stderr, "Error parsing OID string '%s'.\n",
			    argv[last_oid]);
			snmp_tool_freeall(&snmptoolctx);
			exit(1);
		}
	}

	if (snmp_open(NULL, NULL, NULL, NULL)) {
		warn("Failed to open snmp session");
		snmp_tool_freeall(&snmptoolctx);
		exit(1);
	}

	if (snmp_client.version == SNMP_V3 && snmp_client.engine.engine_len == 0)
		SET_EDISCOVER(&snmptoolctx);

	if (ISSET_EDISCOVER(&snmptoolctx) &&
	    snmp_discover_engine(snmptoolctx.passwd) < 0) {
		warn("Unknown SNMP Engine ID");
		rc = 1;
		goto cleanup;
	}

	if (GET_OUTPUT(&snmptoolctx) == OUTPUT_VERBOSE ||
	    ISSET_EDISCOVER(&snmptoolctx))
		snmp_output_engine();

	if (snmp_client.version == SNMP_V3 && ISSET_LOCALKEY(&snmptoolctx) &&
	    !ISSET_EDISCOVER(&snmptoolctx)) {
		if (snmp_passwd_to_keys(&snmp_client.user,
		    snmptoolctx.passwd) != SNMP_CODE_OK ||
		    snmp_get_local_keys(&snmp_client.user,
		    snmp_client.engine.engine_id,
		    snmp_client.engine.engine_len) != SNMP_CODE_OK) {
		    	warn("Failed to get keys");
			rc = 1;
			goto cleanup;
		}
	}

	if (GET_OUTPUT(&snmptoolctx) == OUTPUT_VERBOSE ||
	    ISSET_EDISCOVER(&snmptoolctx))
		snmp_output_keys();

	if (ISSET_EDISCOVER(&snmptoolctx) && snmptoolctx.objects == 0)
		goto cleanup;

	switch (program) {
	case BSNMPGET:
		rc = snmptool_get(&snmptoolctx);
		break;
	case BSNMPWALK:
		rc = snmptool_walk(&snmptoolctx);
		break;
	case BSNMPSET:
		rc = snmptool_set(&snmptoolctx);
		break;
	}


cleanup:
	snmp_tool_freeall(&snmptoolctx);
	snmp_close();

	exit(rc);
}
