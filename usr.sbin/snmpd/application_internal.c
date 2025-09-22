/*	$OpenBSD: application_internal.c,v 1.12 2024/02/06 12:44:27 martijn Exp $	*/

/*
 * Copyright (c) 2023 Martijn van Duren <martijn@openbsd.org>
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

#include <sys/tree.h>
#include <sys/types.h>

#include <stddef.h>

#include <ber.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "application.h"
#include "log.h"
#include "mib.h"
#include "smi.h"
#include "snmp.h"
#include "snmpd.h"

struct appl_internal_object {
	struct ber_oid			 oid;
	struct ber_element *		(*get)(struct ber_oid *);
	/* No getnext means the object is scalar */
	struct ber_element *		(*getnext)(int8_t, struct ber_oid *);

	int32_t				 intval;
	char				*stringval;

	RB_ENTRY(appl_internal_object)	 entry;
};

void appl_internal_region(struct ber_oid *);
void appl_internal_object(struct ber_oid *,
    struct ber_element *(*)(struct ber_oid *),
    struct ber_element *(*)(int8_t, struct ber_oid *));
void appl_internal_get(struct appl_backend *, int32_t, int32_t, const char *,
    struct appl_varbind *);
void appl_internal_getnext(struct appl_backend *, int32_t, int32_t,
    const char *, struct appl_varbind *);
struct ber_element *appl_internal_snmp(struct ber_oid *);
struct ber_element *appl_internal_engine(struct ber_oid *);
struct ber_element *appl_internal_usmstats(struct ber_oid *);
struct ber_element *appl_internal_system(struct ber_oid *);
struct ber_element *appl_internal_get_int(struct ber_oid *);
struct ber_element *appl_internal_get_string(struct ber_oid *);
struct appl_internal_object *appl_internal_object_parent(struct ber_oid *);
int appl_internal_object_cmp(struct appl_internal_object *,
    struct appl_internal_object *);

struct appl_backend_functions appl_internal_functions = {
	.ab_get = appl_internal_get,
	.ab_getnext = appl_internal_getnext,
	.ab_getbulk = NULL, /* getbulk is too complex */
};

struct appl_backend appl_internal = {
	.ab_name = "internal",
	.ab_cookie = NULL,
	.ab_retries = 0,
	.ab_range = 1,
	.ab_fn = &appl_internal_functions
};

struct appl_backend appl_config = {
	.ab_name = "config",
	.ab_cookie = NULL,
	.ab_retries = 0,
	.ab_range = 1,
	.ab_fn = &appl_internal_functions
};

static RB_HEAD(appl_internal_objects, appl_internal_object)
    appl_internal_objects = RB_INITIALIZER(&appl_internal_objects),
    appl_internal_objects_conf = RB_INITIALIZER(&appl_internal_objects_conf);
RB_PROTOTYPE_STATIC(appl_internal_objects, appl_internal_object, entry,
    appl_internal_object_cmp);

void
appl_internal_init(void)
{
	struct appl_internal_object *obj;
	struct ber_oid oid;

	appl_internal_region(&OID(MIB_system));
	appl_internal_object(&OID(MIB_sysDescr), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysOID), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysUpTime), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysContact), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysName), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysLocation), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysServices), appl_internal_system, NULL);
	appl_internal_object(&OID(MIB_sysORLastChange), appl_sysorlastchange,
	    NULL);

	appl_internal_object(&OID(MIB_sysORID), appl_sysortable,
	    appl_sysortable_getnext);
	appl_internal_object(&OID(MIB_sysORDescr), appl_sysortable,
	    appl_sysortable_getnext);
	appl_internal_object(&OID(MIB_sysORUpTime), appl_sysortable,
	    appl_sysortable_getnext);

	appl_internal_region(&OID(MIB_snmp));
	appl_internal_object(&OID(MIB_snmpInPkts), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpOutPkts), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpInBadVersions), appl_internal_snmp,
	   NULL);
	appl_internal_object(&OID(MIB_snmpInBadCommunityNames),
	   appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpInBadCommunityUses),
	   appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpInASNParseErrs), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInTooBigs), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpInNoSuchNames), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInBadValues), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInReadOnlys), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInGenErrs), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpInTotalReqVars), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInTotalSetVars), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInGetRequests), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInGetNexts), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInSetRequests), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInGetResponses), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpInTraps), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpOutTooBigs), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutNoSuchNames), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutBadValues), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutGenErrs), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutGetRequests), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutGetNexts), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutSetRequests), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutGetResponses), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpOutTraps), appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpEnableAuthenTraps),
	    appl_internal_snmp, NULL);
	appl_internal_object(&OID(MIB_snmpSilentDrops), appl_internal_snmp,
	    NULL);
	appl_internal_object(&OID(MIB_snmpProxyDrops), appl_internal_snmp,
	    NULL);

	appl_internal_region(&OID(MIB_snmpV2));
	appl_internal_object(&OID(MIB_snmpEngineID), appl_internal_engine,
	    NULL);
	appl_internal_object(&OID(MIB_snmpEngineBoots), appl_internal_engine,
	    NULL);
	appl_internal_object(&OID(MIB_snmpEngineTime), appl_internal_engine,
	    NULL);
	appl_internal_object(&OID(MIB_snmpEngineMaxMsgSize),
	    appl_internal_engine, NULL);

	appl_internal_object(&OID(MIB_snmpUnavailableContexts),
	    appl_targetmib, NULL);
	appl_internal_object(&OID(MIB_snmpUnknownContexts),
	    appl_targetmib, NULL);

	appl_internal_object(&OID(MIB_usmStatsUnsupportedSecLevels),
	    appl_internal_usmstats, NULL);
	appl_internal_object(&OID(MIB_usmStatsNotInTimeWindow),
	    appl_internal_usmstats, NULL);
	appl_internal_object(&OID(MIB_usmStatsUnknownUserNames),
	    appl_internal_usmstats, NULL);
	appl_internal_object(&OID(MIB_usmStatsUnknownEngineId),
	    appl_internal_usmstats, NULL);
	appl_internal_object(&OID(MIB_usmStatsWrongDigests),
	    appl_internal_usmstats, NULL);
	appl_internal_object(&OID(MIB_usmStatsDecryptionErrors),
	    appl_internal_usmstats, NULL);

	while ((obj = RB_MIN(appl_internal_objects,
	    &appl_internal_objects_conf)) != NULL) {
		RB_REMOVE(appl_internal_objects,
		    &appl_internal_objects_conf, obj);
		oid = obj->oid;
		oid.bo_id[oid.bo_n++] = 0;
		if (appl_register(NULL, 150, 1, &oid,
		    1, 1, 0, 0, &appl_config) != APPL_ERROR_NOERROR) {
			free(obj->stringval);
			free(obj);
		} else
			RB_INSERT(appl_internal_objects, &appl_internal_objects,
			    obj);
	}
}

void
appl_internal_shutdown(void)
{
	struct appl_internal_object *object;

	while ((object = RB_ROOT(&appl_internal_objects)) != NULL) {
		RB_REMOVE(appl_internal_objects, &appl_internal_objects,
		    object);
		free(object->stringval);
		free(object);
	}

	appl_close(&appl_internal);
	appl_close(&appl_config);
}

void
appl_internal_region(struct ber_oid *oid)
{
	enum appl_error error;
	char oidbuf[1024];

	error = appl_register(NULL, 150, 1, oid, 0, 1, 0, 0, &appl_internal);
	/*
	 * Ignore requestDenied, duplicateRegistration, and unsupportedContext
	 */
	if (error == APPL_ERROR_PROCESSINGERROR ||
	    error == APPL_ERROR_PARSEERROR)
		fatalx("internal: Failed to register %s", mib_oid2string(oid,
		    oidbuf, sizeof(oidbuf), snmpd_env->sc_oidfmt));
}

void
appl_internal_object(struct ber_oid *oid,
    struct ber_element *(*get)(struct ber_oid *),
    struct ber_element *(*getnext)(int8_t, struct ber_oid *))
{
	struct appl_internal_object *obj;
	char buf[1024];

	if ((obj = calloc(1, sizeof(*obj))) == NULL)
		fatal(NULL);
	obj->oid = *oid;
	obj->get = get;
	obj->getnext = getnext;
	obj->stringval = NULL;

	if (RB_INSERT(appl_internal_objects,
	    &appl_internal_objects, obj) != NULL)
		fatalx("%s: %s already registered", __func__,
		    mib_oid2string(oid, buf, sizeof(buf),
		    snmpd_env->sc_oidfmt));
}

const char *
appl_internal_object_int(struct ber_oid *oid, int32_t val)
{
	struct appl_internal_object *obj;

	if ((obj = calloc(1, sizeof(*obj))) == NULL)
		return strerror(errno);
	obj->oid = *oid;
	obj->get = appl_internal_get_int;
	obj->getnext = NULL;
	obj->intval = val;
	obj->stringval = NULL;

	if (RB_INSERT(appl_internal_objects,
	    &appl_internal_objects_conf, obj) != NULL) {
		free(obj);
		return "OID already defined";
	}
	return NULL;
}

const char *
appl_internal_object_string(struct ber_oid *oid, char *val)
{
	struct appl_internal_object *obj;

	if ((obj = calloc(1, sizeof(*obj))) == NULL)
		return strerror(errno);
	obj->oid = *oid;
	obj->get = appl_internal_get_string;
	obj->getnext = NULL;
	obj->stringval = val;

	if (RB_INSERT(appl_internal_objects,
	    &appl_internal_objects_conf, obj) != NULL) {
		free(obj);
		return "OID already defined";
	}
	return NULL;
}

void
appl_internal_get(struct appl_backend *backend, __unused int32_t transactionid,
    int32_t requestid, __unused const char *ctx, struct appl_varbind *vblist)
{
	struct ber_oid oid;
	struct appl_internal_object *object;
	struct appl_varbind *vb, *resp;
	size_t i;
	int r;

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next, i++)
		continue;

	if ((resp = calloc(i, sizeof(*resp))) == NULL) {
		log_warn("%s", backend->ab_name);
		appl_response(backend, requestid, APPL_ERROR_GENERR, 1, vblist);
		return;
	}

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next, i++) {
		resp[i].av_oid = vb->av_oid;
		if ((object = appl_internal_object_parent(&vb->av_oid)) == NULL)
			resp[i].av_value =
			    appl_exception(APPL_EXC_NOSUCHOBJECT);
		else {
			oid = object->oid;
			/* Add 0 element for scalar */
			if (object->getnext == NULL)
				oid.bo_id[oid.bo_n++] = 0;
			r = ober_oid_cmp(&vb->av_oid, &oid);
			if ((r == 0 && object->getnext == NULL) ||
			    (r == 2 && object->getnext != NULL))
				resp[i].av_value = object->get(&resp[i].av_oid);
			else
				resp[i].av_value =
				    appl_exception(APPL_EXC_NOSUCHINSTANCE);
		}
		if (resp[i].av_value == NULL) {
			log_warnx("%s: Failed to get value", backend->ab_name);
			goto fail;
		}
		resp[i].av_next = &resp[i + 1];
	}
	resp[i - 1].av_next = NULL;

	appl_response(backend, requestid, APPL_ERROR_NOERROR, 0, resp);

	free(resp);
	return;

 fail:
	for (vb = resp; vb != NULL; vb = vb->av_next)
		ober_free_elements(vb->av_value);
	free(resp);
	appl_response(backend, requestid, APPL_ERROR_GENERR, i + 1, vblist);
}

void
appl_internal_getnext(struct appl_backend *backend,
    __unused int32_t transactionid, int32_t requestid, __unused const char *ctx,
    struct appl_varbind *vblist)
{
	struct ber_oid oid;
	struct appl_internal_object *object, search;
	struct appl_varbind *vb, *resp;
	size_t i;
	int r;
	int8_t include;

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next, i++)
		continue;

	if ((resp = calloc(i, sizeof(*resp))) == NULL) {
		log_warn("%s", backend->ab_name);
		appl_response(backend, requestid, APPL_ERROR_GENERR, 1, vblist);
		return;
	}

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next, i++) {
		resp[i].av_oid = vb->av_oid;
		object = appl_internal_object_parent(&vb->av_oid);
		if (object == NULL) {
			search.oid = vb->av_oid;
			object = RB_NFIND(appl_internal_objects,
			    &appl_internal_objects, &search);
		}

		include = vb->av_include;
		for (; object != NULL; object = RB_NEXT(appl_internal_objects,
		    &appl_internal_objects, object), include = 1) {
			if (object->getnext == NULL) {
				oid = object->oid;
				oid.bo_id[oid.bo_n++] = 0;
				r = ober_oid_cmp(&resp[i].av_oid, &oid);
				if (r > 0 || (r == 0 && !include))
					continue;
				resp[i].av_oid = oid;
				resp[i].av_value = object->get(&oid);
				break;
			}
			/* non-scalar */
			if (ober_oid_cmp(&object->oid, &resp[i].av_oid) > 0) {
				include = 1;
				resp[i].av_oid = object->oid;
			}

			resp[i].av_value =
			    object->getnext(include, &resp[i].av_oid);
			if (resp[i].av_value == NULL ||
			    resp[i].av_value->be_class != BER_CLASS_CONTEXT)
				break;
			/* endOfMibView */
			ober_free_elements(resp[i].av_value);
			resp[i].av_value = NULL;
		}
		if (ober_oid_cmp(&resp[i].av_oid, &vb->av_oid_end) >= 0 ||
		    object == NULL) {
			resp[i].av_oid = vb->av_oid;
			ober_free_elements(resp[i].av_value);
			resp[i].av_value =
			    appl_exception(APPL_EXC_ENDOFMIBVIEW);
		}
		if (resp[i].av_value == NULL) {
			log_warnx("%s: Failed to get value", backend->ab_name);
			goto fail;
		}
		resp[i].av_next = &resp[i + 1];
	}
	resp[i - 1].av_next = NULL;

	appl_response(backend, requestid, APPL_ERROR_NOERROR, 0, resp);

	free(resp);
	return;

 fail:
	for (vb = resp; vb != NULL; vb = vb->av_next)
		ober_free_elements(vb->av_value);
	free(resp);
	appl_response(backend, requestid, APPL_ERROR_GENERR, i + 1, vblist);
}

struct ber_element *
appl_internal_snmp(struct ber_oid *oid)
{
	struct snmp_stats *stats = &snmpd_env->sc_stats;
	struct ber_element *value = NULL;

	if (ober_oid_cmp(oid, &OID(MIB_snmpEnableAuthenTraps, 0)) == 0)
		return ober_add_integer(NULL,
		    stats->snmp_enableauthentraps ? 1 : 2);
	if (ober_oid_cmp(&OID(MIB_snmpInPkts, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inpkts);
	else if (ober_oid_cmp(&OID(MIB_snmpOutPkts, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outpkts);
	else if (ober_oid_cmp(&OID(MIB_snmpInBadVersions, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inbadversions);
	else if (ober_oid_cmp(&OID(MIB_snmpInBadCommunityNames, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inbadcommunitynames);
	else if (ober_oid_cmp(&OID(MIB_snmpInBadCommunityUses, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inbadcommunityuses);
	else if (ober_oid_cmp(&OID(MIB_snmpInASNParseErrs, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inasnparseerrs);
	else if (ober_oid_cmp(&OID(MIB_snmpInTooBigs, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_intoobigs);
	else if (ober_oid_cmp(&OID(MIB_snmpInNoSuchNames, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_innosuchnames);
	else if (ober_oid_cmp(&OID(MIB_snmpInBadValues, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inbadvalues);
	else if (ober_oid_cmp(&OID(MIB_snmpInReadOnlys, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_inreadonlys);
	else if (ober_oid_cmp(&OID(MIB_snmpInGenErrs, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_ingenerrs);
	else if (ober_oid_cmp(&OID(MIB_snmpInTotalReqVars, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_intotalreqvars);
	else if (ober_oid_cmp(&OID(MIB_snmpInTotalSetVars, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_intotalsetvars);
	else if (ober_oid_cmp(&OID(MIB_snmpInGetRequests, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_ingetrequests);
	else if (ober_oid_cmp(&OID(MIB_snmpInGetNexts, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_ingetnexts);
	else if (ober_oid_cmp(&OID(MIB_snmpInSetRequests, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_insetrequests);
	else if (ober_oid_cmp(&OID(MIB_snmpInGetResponses, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_ingetresponses);
	else if (ober_oid_cmp(&OID(MIB_snmpInTraps, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_intraps);
	else if (ober_oid_cmp(&OID(MIB_snmpOutTooBigs, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outtoobigs);
	else if (ober_oid_cmp(&OID(MIB_snmpOutNoSuchNames, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outnosuchnames);
	else if (ober_oid_cmp(&OID(MIB_snmpOutBadValues, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outbadvalues);
	else if (ober_oid_cmp(&OID(MIB_snmpOutGenErrs, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outgenerrs);
	else if (ober_oid_cmp(&OID(MIB_snmpOutGetRequests, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outgetrequests);
	else if (ober_oid_cmp(&OID(MIB_snmpOutGetNexts, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outgetnexts);
	else if (ober_oid_cmp(&OID(MIB_snmpOutSetRequests, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outsetrequests);
	else if (ober_oid_cmp(&OID(MIB_snmpOutGetResponses, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outgetresponses);
	else if (ober_oid_cmp(&OID(MIB_snmpOutTraps, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_outtraps);
	else if (ober_oid_cmp(&OID(MIB_snmpSilentDrops, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_silentdrops);
	else if (ober_oid_cmp(&OID(MIB_snmpProxyDrops, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_proxydrops);

	if (value != NULL)
		ober_set_header(value, BER_CLASS_APPLICATION, SNMP_T_COUNTER32);
	return value;
}

struct ber_element *
appl_internal_engine(struct ber_oid *oid)
{
	if (ober_oid_cmp(&OID(MIB_snmpEngineID, 0), oid) == 0)
		return ober_add_nstring(NULL, snmpd_env->sc_engineid,
		    snmpd_env->sc_engineid_len);
	else if (ober_oid_cmp(&OID(MIB_snmpEngineBoots, 0), oid) == 0)
		return ober_add_integer(NULL, snmpd_env->sc_engine_boots);
	else if (ober_oid_cmp(&OID(MIB_snmpEngineTime, 0), oid) == 0)
		return ober_add_integer(NULL, snmpd_engine_time());
	else if (ober_oid_cmp(&OID(MIB_snmpEngineMaxMsgSize, 0), oid) == 0)
		return ober_add_integer(NULL, READ_BUF_SIZE);
	return NULL;
}

struct ber_element *
appl_internal_usmstats(struct ber_oid *oid)
{
	struct snmp_stats *stats = &snmpd_env->sc_stats;
	struct ber_element *value = NULL;

	if (ober_oid_cmp(&OID(MIB_usmStatsUnsupportedSecLevels, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmbadseclevel);
	else if (ober_oid_cmp(&OID(MIB_usmStatsNotInTimeWindow, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmtimewindow);
	else if (ober_oid_cmp(&OID(MIB_usmStatsUnknownUserNames, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmnosuchuser);
	else if (ober_oid_cmp(&OID(MIB_usmStatsUnknownEngineId, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmnosuchengine);
	else if (ober_oid_cmp(&OID(MIB_usmStatsWrongDigests, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmwrongdigest);
	else if (ober_oid_cmp(&OID(MIB_usmStatsDecryptionErrors, 0), oid) == 0)
		value = ober_add_integer(NULL, stats->snmp_usmdecrypterr);

	if (value != NULL)
		ober_set_header(value, BER_CLASS_APPLICATION, SNMP_T_COUNTER32);

	return value;
}

struct ber_element *
appl_internal_system(struct ber_oid *oid)
{
	struct snmp_system *s = &snmpd_env->sc_system;
	struct ber_element *value = NULL;

	if (ober_oid_cmp(&OID(MIB_sysDescr, 0), oid) == 0)
		return ober_add_string(NULL, s->sys_descr);
	else if (ober_oid_cmp(&OID(MIB_sysOID, 0), oid) == 0)
		return ober_add_oid(NULL, &s->sys_oid);
	else if (ober_oid_cmp(&OID(MIB_sysUpTime, 0), oid) == 0) {
		value = ober_add_integer(NULL, smi_getticks());
		ober_set_header(value, BER_CLASS_APPLICATION, SNMP_T_TIMETICKS);
	} else if (ober_oid_cmp(&OID(MIB_sysContact, 0), oid) == 0)
		return ober_add_string(NULL, s->sys_contact);
	else if (ober_oid_cmp(&OID(MIB_sysName, 0), oid) == 0)
		return ober_add_string(NULL, s->sys_name);
	else if (ober_oid_cmp(&OID(MIB_sysLocation, 0), oid) == 0)
		return ober_add_string(NULL, s->sys_location);
	else if (ober_oid_cmp(&OID(MIB_sysServices, 0), oid) == 0)
		return ober_add_integer(NULL, s->sys_services);
	return value;
}

struct ber_element *
appl_internal_get_int(struct ber_oid *oid)
{
	struct appl_internal_object *obj;

	obj = appl_internal_object_parent(oid);
	return ober_add_integer(NULL, obj->intval);
}

struct ber_element *
appl_internal_get_string(struct ber_oid *oid)
{
	struct appl_internal_object *obj;

	obj = appl_internal_object_parent(oid);
	return ober_add_string(NULL, obj->stringval);
}

struct appl_internal_object *
appl_internal_object_parent(struct ber_oid *oid)
{
	struct appl_internal_object *object, search;

	search.oid = *oid;
	do {
		if ((object = RB_FIND(appl_internal_objects,
		    &appl_internal_objects, &search)) != NULL)
			return object;
	} while (--search.oid.bo_n > 0);

	return NULL;
}

int
appl_internal_object_cmp(struct appl_internal_object *o1,
    struct appl_internal_object *o2)
{
	return ober_oid_cmp(&o1->oid, &o2->oid);
}

RB_GENERATE_STATIC(appl_internal_objects, appl_internal_object, entry,
    appl_internal_object_cmp);
