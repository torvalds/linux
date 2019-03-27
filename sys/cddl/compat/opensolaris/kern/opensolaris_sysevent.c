/*-
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kmem.h>
#include <sys/sbuf.h>
#include <sys/bus.h>
#include <sys/nvpair.h>
#include <sys/sunddi.h>
#include <sys/sysevent.h>
#include <sys/fm/protocol.h>

struct sysevent {
	nvlist_t	*se_nvl;
	char		 se_class[128];
	char		 se_subclass[128];
	char		 se_pub[128];
};

sysevent_t *
sysevent_alloc(char *class, char *subclass, char *pub, int flag)
{
	struct sysevent *ev;

	ASSERT(class != NULL);
	ASSERT(subclass != NULL);
	ASSERT(pub != NULL);
	ASSERT(flag == SE_SLEEP);

	ev = kmem_alloc(sizeof(*ev), KM_SLEEP);
	ev->se_nvl = NULL;
	strlcpy(ev->se_class, class, sizeof(ev->se_class));
	strlcpy(ev->se_subclass, subclass, sizeof(ev->se_subclass));
	strlcpy(ev->se_pub, pub, sizeof(ev->se_pub));

	return ((sysevent_t *)ev);
}

void
sysevent_free(sysevent_t *evp)
{
	struct sysevent *ev = (struct sysevent *)evp;

	ASSERT(evp != NULL);

	if (ev->se_nvl != NULL)
		sysevent_free_attr(ev->se_nvl);
	kmem_free(ev, sizeof(*ev));
}

int
sysevent_add_attr(sysevent_attr_list_t **ev_attr_list, char *name,
    sysevent_value_t *se_value, int flag)
{
	nvlist_t *nvl;
	int error;

	ASSERT(ev_attr_list != NULL);
	ASSERT(name != NULL);
	ASSERT(se_value != NULL);
	ASSERT(flag == SE_SLEEP);

	if (strlen(name) >= MAX_ATTR_NAME)
		return (SE_EINVAL);

	nvl = *ev_attr_list;
	if (nvl == NULL) {
		if (nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, KM_SLEEP) != 0)
			return (SE_ENOMEM);
	}

	error = 0;

	switch (se_value->value_type) {
	case SE_DATA_TYPE_UINT64:
		error = nvlist_add_uint64(nvl, name, se_value->value.sv_uint64);
		break;
	case SE_DATA_TYPE_STRING:
		if (strlen(se_value->value.sv_string) >= MAX_STRING_SZ)
			error = SE_EINVAL;
		if (error == 0) {
			error = nvlist_add_string(nvl, name,
			    se_value->value.sv_string);
		}
		break;
	default:
#if 0
		printf("%s: type %d is not implemented\n", __func__,
		    se_value->value_type);
#endif
		break;
	}

	if (error != 0) {
		nvlist_free(nvl);
		return (error);
	}

	*ev_attr_list = nvl;

	return (0);
}

void
sysevent_free_attr(sysevent_attr_list_t *ev_attr_list)
{

	nvlist_free(ev_attr_list);
}

int
sysevent_attach_attributes(sysevent_t *evp, sysevent_attr_list_t *ev_attr_list)
{
	struct sysevent *ev = (struct sysevent *)evp;

	ASSERT(ev->se_nvl == NULL);

	ev->se_nvl = ev_attr_list;

	return (0);
}

void
sysevent_detach_attributes(sysevent_t *evp)
{
	struct sysevent *ev = (struct sysevent *)evp;

	ASSERT(ev->se_nvl != NULL);

	ev->se_nvl = NULL;
}

int
log_sysevent(sysevent_t *evp, int flag, sysevent_id_t *eid)
{
	struct sysevent *ev = (struct sysevent *)evp;
	struct sbuf *sb;
	const char *type;
	char typestr[128];
	nvpair_t *elem = NULL;

	ASSERT(evp != NULL);
	ASSERT(ev->se_nvl != NULL);
	ASSERT(flag == SE_SLEEP);
	ASSERT(eid != NULL);

	sb = sbuf_new_auto();
	if (sb == NULL)
		return (SE_ENOMEM);
	type = NULL;

	while ((elem = nvlist_next_nvpair(ev->se_nvl, elem)) != NULL) {
		switch (nvpair_type(elem)) {
		case DATA_TYPE_BOOLEAN:
		    {
			boolean_t value;

			(void) nvpair_value_boolean_value(elem, &value);
			sbuf_printf(sb, " %s=%s", nvpair_name(elem),
			    value ? "true" : "false");
			break;
		    }
		case DATA_TYPE_UINT8:
		    {
			uint8_t value;

			(void) nvpair_value_uint8(elem, &value);
			sbuf_printf(sb, " %s=%hhu", nvpair_name(elem), value);
			break;
		    }
		case DATA_TYPE_INT32:
		    {
			int32_t value;

			(void) nvpair_value_int32(elem, &value);
			sbuf_printf(sb, " %s=%jd", nvpair_name(elem),
			    (intmax_t)value);
			break;
		    }
		case DATA_TYPE_UINT32:
		    {
			uint32_t value;

			(void) nvpair_value_uint32(elem, &value);
			sbuf_printf(sb, " %s=%ju", nvpair_name(elem),
			    (uintmax_t)value);
			break;
		    }
		case DATA_TYPE_INT64:
		    {
			int64_t value;

			(void) nvpair_value_int64(elem, &value);
			sbuf_printf(sb, " %s=%jd", nvpair_name(elem),
			    (intmax_t)value);
			break;
		    }
		case DATA_TYPE_UINT64:
		    {
			uint64_t value;

			(void) nvpair_value_uint64(elem, &value);
			sbuf_printf(sb, " %s=%ju", nvpair_name(elem),
			    (uintmax_t)value);
			break;
		    }
		case DATA_TYPE_STRING:
		    {
			char *value;

			(void) nvpair_value_string(elem, &value);
			sbuf_printf(sb, " %s=%s", nvpair_name(elem), value);
			if (strcmp(FM_CLASS, nvpair_name(elem)) == 0)
				type = value;
			break;
		    }
		case DATA_TYPE_UINT8_ARRAY:
		    {
		    	uint8_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint8_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%02hhx", value[ii]);
			break;
		    }
		case DATA_TYPE_UINT16_ARRAY:
		    {
		    	uint16_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint16_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%04hx", value[ii]);
			break;
		    }
		case DATA_TYPE_UINT32_ARRAY:
		    {
		    	uint32_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint32_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%08jx", (uintmax_t)value[ii]);
			break;
		    }
		case DATA_TYPE_UINT64_ARRAY:
		    {
		    	uint64_t *value;
			uint_t ii, nelem;

			(void) nvpair_value_uint64_array(elem, &value, &nelem);
			sbuf_printf(sb, " %s=", nvpair_name(elem));
			for (ii = 0; ii < nelem; ii++)
				sbuf_printf(sb, "%016jx", (uintmax_t)value[ii]);
			break;
		    }
		default:
#if 0
			printf("%s: type %d is not implemented\n", __func__,
			    nvpair_type(elem));
#endif
			break;
		}
	}

	if (sbuf_finish(sb) != 0) {
		sbuf_delete(sb);
		return (SE_ENOMEM);
	}

	if (type == NULL)
		type = ev->se_subclass;
	if (strncmp(type, "ESC_ZFS_", 8) == 0) {
		snprintf(typestr, sizeof(typestr), "misc.fs.zfs.%s", type + 8);
		type = typestr;
	}
	devctl_notify("ZFS", "ZFS", type, sbuf_data(sb));
	sbuf_delete(sb);

	return (0);
}

int
_ddi_log_sysevent(char *vendor, char *class, char *subclass,
    nvlist_t *attr_list, sysevent_id_t *eidp, int flag)
{
	sysevent_t *ev;
	int ret;

	ASSERT(vendor != NULL);
	ASSERT(class != NULL);
	ASSERT(subclass != NULL);
	ASSERT(attr_list != NULL);
	ASSERT(eidp != NULL);
	ASSERT(flag == DDI_SLEEP);

	ev = sysevent_alloc(class, subclass, vendor, SE_SLEEP);
	ASSERT(ev != NULL);
	(void)sysevent_attach_attributes(ev, attr_list);
        ret = log_sysevent(ev, SE_SLEEP, eidp);
	sysevent_detach_attributes(ev);
	sysevent_free(ev);

	return (ret);
}
