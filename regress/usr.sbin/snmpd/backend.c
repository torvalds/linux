#include <sys/socket.h>
#include <sys/time.h>

#include <ber.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "regress.h"

#define MIB_BACKEND_GET MIB_BACKEND, 1
#define MIB_BACKEND_GETNEXT MIB_BACKEND, 2
#define MIB_BACKEND_GETBULK MIB_BACKEND, 3
#define MIB_BACKEND_ERROR MIB_BACKEND, 4

#define MIB_SUBAGENT_BACKEND_GET MIB_SUBAGENT_BACKEND, 1
#define MIB_SUBAGENT_BACKEND_GETNEXT MIB_SUBAGENT_BACKEND, 2
#define MIB_SUBAGENT_BACKEND_GETBULK MIB_SUBAGENT_BACKEND, 3
#define MIB_SUBAGENT_BACKEND_ERROR MIB_SUBAGENT_BACKEND, 4

#define nitems(_a)     (sizeof((_a)) / sizeof((_a)[0]))

void
backend_get_integer(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 1, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 1), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_octetstring(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 2, 0),
		.data.octetstring.string = "test",
		.data.octetstring.len = 4
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 2), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_OCTETSTRING;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_objectidentifier(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 3, 0),
		.data.oid = OID_STRUCT(MIB_BACKEND_GET, 3, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 3), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 3), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_OBJECTIDENTIFIER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_ipaddress(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 4, 0),
		.data.octetstring.string = "\0\0\0\0",
		.data.octetstring.len = 4
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 4), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 4), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_IPADDRESS;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_counter32(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 5, 0),
		.data.uint32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 5), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 5), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_COUNTER32;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_gauge32(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 6, 0),
		.data.uint32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 6), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 6), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_GAUGE32;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_timeticks(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 7, 0),
		.data.uint32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 7), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 7), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_TIMETICKS;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_opaque(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 8, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct ber ber = {};
	struct ber_element *elm;
	ssize_t len;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 8), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 8), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	if ((elm = ober_add_integer(NULL, 1)) == NULL)
		err(1, "ober_add_integer");
	if (ober_write_elements(&ber, elm) == -1)
		err(1, "ober_write_elements");
	varbind.data.octetstring.len = ober_get_writebuf(
	    &ber, (void **)&varbind.data.octetstring.string);
	ober_free_elements(elm);

	varbind.type = TYPE_OPAQUE;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
	ober_free(&ber);
}

void
backend_get_counter64(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 9, 0),
		.data.uint64 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 9), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 9), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_COUNTER64;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_nosuchobject(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 10, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 10), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 10), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_NOSUCHOBJECT;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_nosuchinstance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 11, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 11), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 11), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_NOSUCHINSTANCE;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_endofmibview(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 12, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 12), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 12), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_ENDOFMIBVIEW;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_get_two_single_backend(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 13, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 13, 2),
			.data.int32 = 2
		}
	};
	struct varbind varbind_ax[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 13, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 13, 2),
			.data.int32 = 2
		}
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 13), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 13), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, varbind, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, varbind_ax, 2);

	agentx_response(ax_s, buf, NOERROR, 0, varbind_ax, 2);

	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 2);
}

void
backend_get_two_double_backend(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s1, ax_s2;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 14, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 14, 2),
			.data.int32 = 2
		}
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s1 = agentx_connect(axsocket);
	ax_s2 = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 14, 1),
	    "backend_get_two_double_backend.1");
	sessionid2 = agentx_open(ax_s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 14, 2),
	    "backend_get_two_double_backend.2");
	agentx_register(ax_s1, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 14, 1), 0);
	agentx_register(ax_s2, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 14, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, varbind, 2);

	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	n = agentx_read(ax_s1, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid1, varbind, 1);
	agentx_response(ax_s1, buf, NOERROR, 0, varbind, 1);

	n = agentx_read(ax_s2, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, varbind + 1, 1);
	agentx_response(ax_s2, buf, NOERROR, 0, varbind + 1, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 2);
}

void
backend_get_wrongorder(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 15, 1),
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 15, 2),
		}
	};
	struct varbind varbind_ax[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 15, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 15, 2),
			.data.int32 = 2
		}
	}, tmpvarbind;
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 15), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 15), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, varbind, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, varbind_ax, 2);
	tmpvarbind = varbind_ax[0];
	varbind_ax[0] = varbind_ax[1];
	varbind_ax[1] = tmpvarbind;

	agentx_response(ax_s, buf, NOERROR, 0, varbind_ax, 2);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 2,
	    varbind, 2);
}

void
backend_get_toofew(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 16, 1),
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 16, 2),
		}
	};
	struct varbind varbind_ax[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 16, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 16, 2),
			.data.int32 = 2
		}
	}, tmpvarbind;
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 16), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 16), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, varbind, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, varbind_ax, 2);

	agentx_response(ax_s, buf, NOERROR, 0, varbind_ax, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 2,
	    varbind, 2);
}

void
backend_get_toomany(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 17, 1),
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GET, 17, 2),
		}
	};
	struct varbind varbind_ax[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 17, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 17, 2),
			.data.int32 = 2
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GET, 17, 3),
			.data.int32 = 3
		}
	}, tmpvarbind;
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 17), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 17), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, varbind, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, varbind_ax, 2);

	agentx_response(ax_s, buf, NOERROR, 0, varbind_ax, 3);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 2,
	    varbind, 2);
}

void
backend_get_instance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 18, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 18), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 18, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_instance_below(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 19, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 19), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 19), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_NOSUCHINSTANCE;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_timeout_default(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 20, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct timespec start, end, diff;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 20), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 20), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(1, "clock_gettime");
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 6000, community, requestid, GENERR, 1,
	    &varbind, 1);
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
		err(1, "clock_gettime");
	timespecsub(&end, &start, &diff);
	if (diff.tv_sec != 5)
		errx(1, "%s: unexpected timeout (%lld.%09ld/5)", __func__,
		    diff.tv_sec, diff.tv_nsec);
}

void
backend_get_timeout_session_lower(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 21, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct timespec start, end, diff;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 1,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 21), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 21), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(1, "clock_gettime");
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 6000, community, requestid, GENERR, 1,
	    &varbind, 1);
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
		err(1, "clock_gettime");
	timespecsub(&end, &start, &diff);
	if (diff.tv_sec != 1)
		errx(1, "%s: unexpected timeout (%lld.%09ld/1)", __func__,
		    diff.tv_sec, diff.tv_nsec);
}

void
backend_get_timeout_session_higher(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 22, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct timespec start, end, diff;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 6,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 22), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 22), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(1, "clock_gettime");
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 7000, community, requestid, GENERR, 1,
	    &varbind, 1);
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
		err(1, "clock_gettime");
	timespecsub(&end, &start, &diff);
	if (diff.tv_sec != 6)
		errx(1, "%s: unexpected timeout (%lld.%09ld/6)", __func__,
		    diff.tv_sec, diff.tv_nsec);
}

void
backend_get_timeout_region_lower(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 23, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct timespec start, end, diff;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 4,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 23), __func__);
	agentx_register(ax_s, sessionid, 0, 1, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 23), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(1, "clock_gettime");
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 6000, community, requestid, GENERR, 1,
	    &varbind, 1);
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
		err(1, "clock_gettime");
	timespecsub(&end, &start, &diff);
	if (diff.tv_sec != 1)
		errx(1, "%s: unexpected timeout (%lld.%09ld/1)", __func__,
		    diff.tv_sec, diff.tv_nsec);
}

void
backend_get_timeout_region_higher(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 24, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	struct timespec start, end, diff;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 7,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 24), __func__);
	agentx_register(ax_s, sessionid, 0, 6, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 24), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
		err(1, "clock_gettime");
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 8000, community, requestid, GENERR, 1,
	    &varbind, 1);
	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1)
		err(1, "clock_gettime");
	timespecsub(&end, &start, &diff);
	if (diff.tv_sec != 6)
		errx(1, "%s: unexpected timeout (%lld.%09ld/6)", __func__,
		    diff.tv_sec, diff.tv_nsec);
}

void
backend_get_priority_lower(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 25, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 25, 1), "backend_get_priority.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 25, 2), "backend_get_priority.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 25), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 126, 0,
	    OID_ARG(MIB_BACKEND_GET, 25), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_priority_higher(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 26, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 26, 1),
	    "backend_get_priority_higher.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 26, 2),
	    "backend_get_priority_higher.2");
	agentx_register(ax_s, sessionid1, 0, 0, 126, 0,
	    OID_ARG(MIB_BACKEND_GET, 26), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 26), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid1, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_priority_below_lower(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 27, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 27, 1),
	    "backend_get_priority_below_lower.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 27, 2),
	    "backend_get_priority_below_lower.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 27), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 126, 0,
	    OID_ARG(MIB_BACKEND_GET, 27, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_priority_below_higher(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 28, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 28, 1),
	    "backend_get_priority_below_higher.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 28, 2),
	    "backend_get_priority_below_higher.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 28), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GET, 28, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_close(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 29, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 29), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 29), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_close(ax_s, sessionid, REASONOTHER);

	varbind.type = TYPE_NOSUCHOBJECT;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_close_overlap(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s1, ax_s2;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 30, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s1 = agentx_connect(axsocket);
	ax_s2 = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 30, 1),
	    "backend_get_close_overlap.1");
	sessionid2 = agentx_open(ax_s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 30, 1),
	    "backend_get_close_overlap.2");
	agentx_register(ax_s1, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 30), 0);
	agentx_register(ax_s2, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GET, 30), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s1, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid1, &varbind, 1);
	agentx_close(ax_s1, sessionid1, REASONOTHER);

	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s2, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, &varbind, 1);
	agentx_response(ax_s2, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_disappear(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 31, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 31), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 31), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);
	close(ax_s);

	varbind.type = TYPE_NOSUCHOBJECT;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_disappear_overlap(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s1, ax_s2;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 32, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s1 = agentx_connect(axsocket);
	ax_s2 = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 32, 1),
	    "backend_get_close_overlap.1");
	sessionid2 = agentx_open(ax_s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 32, 1),
	    "backend_get_close_overlap.2");
	agentx_register(ax_s1, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 32), 0);
	agentx_register(ax_s2, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GET, 32), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s1, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid1, &varbind, 1);
	close(ax_s1);

	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s2, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid2, &varbind, 1);
	agentx_response(ax_s2, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_disappear_doublesession(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 33, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 33, 1),
	    "backend_get_disappear_doublesession.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 33, 2),
	    "backend_get_disappear_doublesession.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 33), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GET, 33), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid1, &varbind, 1);
	close(ax_s);

	varbind.type = TYPE_NOSUCHOBJECT;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_octetstring_max(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	char vbbuf[65535] = {};
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 34, 0),
		.data.octetstring.string = vbbuf,
		.data.octetstring.len = sizeof(vbbuf)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	memset(vbbuf, 'a', sizeof(vbbuf));
	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 34), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 34), 0);

	/* Too big for SOCK_DGRAM */
	salen = snmp_resolve(SOCK_STREAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_STREAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_OCTETSTRING;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_get_octetstring_too_long(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	char vbbuf[65536];
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 35, 0),
		.data.octetstring.string = vbbuf,
		.data.octetstring.len = sizeof(vbbuf)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	memset(vbbuf, 'a', sizeof(vbbuf));
	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 35), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 35), 0);

	salen = snmp_resolve(SOCK_STREAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_STREAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_OCTETSTRING;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_get_ipaddress_too_short(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 36, 0),
		.data.octetstring.string = "\0\0\0",
		.data.octetstring.len = 3
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 36), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 36), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_IPADDRESS;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_get_ipaddress_too_long(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 37, 0),
		.data.octetstring.string = "\0\0\0\0\0",
		.data.octetstring.len = 5
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 37), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 37), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_IPADDRESS;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_get_opaque_non_ber(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 38, 0),
		.data.octetstring.string = "\1",
		.data.octetstring.len = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;
	ssize_t len;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 38), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 38), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_OPAQUE;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_get_opaque_double_value(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	char vbbuf[1024];
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GET, 39, 0),
		.data.octetstring.string = vbbuf
	};
	int32_t requestid;
	void *berdata;
	char buf[1024];
	size_t n;
	struct ber ber = {};
	struct ber_element *elm;
	ssize_t len;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GET, 39), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GET, 39), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	if ((elm = ober_add_integer(NULL, 1)) == NULL)
		err(1, "ober_add_integer");
	if (ober_write_elements(&ber, elm) == -1)
		err(1, "ober_write_elements");
	len = ober_get_writebuf(&ber, &berdata);
	ober_free_elements(elm);

	memcpy(vbbuf, berdata, len);
	memcpy(vbbuf + len, berdata, len);
	varbind.data.octetstring.len = 2 * len;

	varbind.type = TYPE_OPAQUE;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	varbind.type = TYPE_NULL;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
	ober_free(&ber);
}

void
backend_getnext_selfbound(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 1),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 1), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_lowerbound(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 2),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 2),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 2, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 2, 1),
	    "backend_getnext_lowerbound.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 2, 2),
	    "backend_getnext_lowerbound.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 2), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 2, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_lowerbound_self(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 3),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 3),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 4)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 3), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 3), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_lowerbound_highprio(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 4),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 4),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 4, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 4, 1),
	    "backend_getnext_lowerbound_highprio.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 4, 2),
	    "backend_getnext_lowerbound_highprio.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 4), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 4, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_lowerbound_lowprio(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 5),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 5),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 5, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 5, 1),
	    "backend_getnext_lowerbound_lowprio.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 5, 2),
	    "backend_getnext_lowerbound_lowprio.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 5), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 128, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 5, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_sibling(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 6),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 6),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 8)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 6), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 6), 0);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 7), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_child_gap(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 7),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 7),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 7, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 6, 1),
	    "backend_getnext_child_gap.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 6, 2),
	    "backend_getnext_child_gap.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 7), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 7, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_nosuchobject(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 8),
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 8),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 9)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 8), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 8), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_NOSUCHOBJECT;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name.n_subid--;
	varbind.type = TYPE_NULL;

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_getnext_nosuchinstance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 9),
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 9),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 10)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 9), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 9), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_NOSUCHINSTANCE;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name.n_subid--;
	varbind.type = TYPE_NULL;

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

/* Assume that everything is registered under 1.3.* */
void
backend_getnext_endofmibview(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(2, 0),
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(2, 0),
		.end = OID_STRUCT(2, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 10), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(2, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name.n_subid--;

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    &varbind, 1);
}

void
backend_getnext_inclusive(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 11),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 11, 0),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 11, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	searchrange.start.include = 1;
	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 11), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 11, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
backend_getnext_jumpnext(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind1 = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 12)
	}, varbind2 = {
		.type = TYPE_INTEGER,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 12, 1),
		.data.int32 = 1
	};
	struct searchrange searchrange1 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 12),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 12, 1)
	}, searchrange2 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 12, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 12, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 12, 1),
	    "backend_getnext_jumpnext.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 12, 2),
	    "backend_getnext_jumpnext.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 12), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 12, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind1, 1);

	varbind1.type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange1,
	    &varbind1, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind1, 1);

	searchrange2.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid2, &searchrange2,
	    &varbind2, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind2, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind2, 1);
}

/* Assume that everything is registered under 1.3.* */
void
backend_getnext_jumpnext_endofmibview(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind1 = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(2, 0)
	}, varbind2 = {
		.type = TYPE_ENDOFMIBVIEW,
		.name = OID_STRUCT(2, 1),
	};
	struct searchrange searchrange1 = {
		.start = OID_STRUCT(2, 0),
		.end = OID_STRUCT(2, 1)
	}, searchrange2 = {
		.start = OID_STRUCT(2, 1),
		.end = OID_STRUCT(2, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 13, 1),
	    "backend_getnext_jumpnext_endofmibview.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 13, 2),
	    "backend_getnext_jumpnext_endofmibview.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(2, 0), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(2, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind1, 1);

	varbind1.type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange1,
	    &varbind1, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind1, 1);

	searchrange2.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid2, &searchrange2,
	    &varbind2, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind2, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind1, 1);
}

void
backend_getnext_jump_up(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind1 = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 14, 1)
	}, varbind2 = {
		.type = TYPE_INTEGER,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 14, 2),
		.data.int32 = 1
	};
	struct searchrange searchrange1 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 14, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 14, 2)
	}, searchrange2 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 14, 2),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 15)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 14, 1),
	    "backend_getnext_jump_up.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 14, 2),
	    "backend_getnext_jump_up.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 14), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 14, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind1, 1);

	varbind1.type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid2, &searchrange1,
	    &varbind1, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind1, 1);

	searchrange2.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange2,
	    &varbind2, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind2, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind2, 1);
}

void
backend_getnext_two_single_backend(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 1),
			.data.int32 = 2
		}
	};
	struct varbind varbind_ax[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 2),
			.data.int32 = 2
		}
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 0),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 16)
		},
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 15, 1),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 16)
		}
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 15), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 15), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, searchrange,
	    varbind_ax, 2);

	agentx_response(ax_s, buf, NOERROR, 0, varbind_ax, 2);

	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	varbind[0].name.subid[varbind[0].name.n_subid -1]++;
	varbind[1].name.subid[varbind[1].name.n_subid - 1]++;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 2);
}

void
backend_getnext_two_double_backend(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s1, ax_s2;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 2),
			.data.int32 = 2
		}
	};
	struct searchrange searchrange1 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 2)
	}, searchrange2 = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 2),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 16, 3)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s1 = agentx_connect(axsocket);
	ax_s2 = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 16, 1),
	    "backend_getnext_two_double_backend.1");
	sessionid2 = agentx_open(ax_s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 16, 2),
	    "backend_getnext_two_double_backend.2");
	agentx_register(ax_s1, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 16, 1), 0);
	agentx_register(ax_s2, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 16, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 2);

	varbind[0].name.subid[varbind[0].name.n_subid++] = 0;
	varbind[1].name.subid[varbind[1].name.n_subid++] = 0;
	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	n = agentx_read(ax_s1, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, &searchrange1,
	    varbind + 0, 1);
	agentx_response(ax_s1, buf, NOERROR, 0, varbind + 0, 1);

	n = agentx_read(ax_s2, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid2, &searchrange2,
	    varbind + 1, 1);
	agentx_response(ax_s2, buf, NOERROR, 0, varbind + 1, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 2);
}

void
backend_getnext_instance_below(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 17, 1, 1),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 17, 2),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 17, 3)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 17), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 17, 1), 0);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 17, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 17, 2);
	searchrange.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    &varbind, 1);
}

void
backend_getnext_instance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 18),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 18, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 18, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 18), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 18, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 18, 1);
	searchrange.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    &varbind, 1);
}

void
backend_getnext_instance_exact(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 19, 1),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 19, 2),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 19, 3)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 19), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 19, 1), 0);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 19, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 19, 2);
	searchrange.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    &varbind, 1);
}

void
backend_getnext_instance_ignore(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 20),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 20, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 20, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 20), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 20, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 20, 1, 0);
	searchrange.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
	varbind.type = TYPE_NULL;
	varbind.name.n_subid -= 2;

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_getnext_backwards(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 21),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 21),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 22)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 21), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 21), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 21, 1);
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 20);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
	varbind.type = TYPE_NULL;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 21);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_getnext_stale(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 22),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 22),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 23)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 22), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 22), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 22, 1);
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 22);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
	varbind.type = TYPE_NULL;

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_getnext_inclusive_backwards(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 23),
		.data.int32 = 1
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 23, 1),
		.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 23, 2)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 23), __func__);
	agentx_register(ax_s, sessionid, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 23, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 23, 1);
	searchrange.start.include = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 22);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
	varbind.type = TYPE_NULL;
	varbind.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 23);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_getnext_toofew(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 1),
			.data.int32 = 2
		}
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 0),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 25)
		},
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 1),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 25)
		}
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 24), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 24), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 2);

	varbind[0].name.subid[varbind[0].name.n_subid - 1]++;
	varbind[1].name.subid[varbind[1].name.n_subid - 1]++;
	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, searchrange,
	    varbind, 2);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);
	varbind[0].type = varbind[1].type = TYPE_NULL;
	varbind[0].name = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 0),
	varbind[1].name = OID_STRUCT(MIB_BACKEND_GETNEXT, 24, 1),

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 2,
	    varbind, 2);
}

void
backend_getnext_toomany(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 1),
			.data.int32 = 2
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 3),
			.data.int32 = 3
		}
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 0),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 26)
		},
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 1),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 26)
		}
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 25), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 25), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 2);

	varbind[0].name.subid[varbind[0].name.n_subid - 1]++;
	varbind[1].name.subid[varbind[1].name.n_subid - 1]++;
	varbind[0].type = varbind[1].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, searchrange,
	    varbind, 2);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 3);
	varbind[0].type = varbind[1].type = TYPE_NULL;
	varbind[0].name = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 0),
	varbind[1].name = OID_STRUCT(MIB_BACKEND_GETNEXT, 25, 1),

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 2,
	    varbind, 2);
}

void
backend_getnext_response_equal_end(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 26),
			.data.int32 = 1
		},
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 26),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 26, 1, 1)
		},
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 26, 1),
	    "backend_getnext_end_equal.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 26, 2),
	    "backend_getnext_end_equal.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 26), 0);
	agentx_register(ax_s, sessionid2, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 26, 1, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 1);

	/* Fool agentx_getnext_handle() */
	varbind[0].name.subid[varbind[0].name.n_subid++] = 1;
	varbind[0].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, searchrange,
	    varbind, 1);
	varbind[0].name = searchrange[0].end;
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);
	varbind[0].type = TYPE_NULL;
	varbind[0].name = OID_STRUCT(MIB_BACKEND_GETNEXT, 26),

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    varbind, 1);
}

void
backend_getnext_instance_below_region_before_instance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 27),
			.data.int32 = 1
		},
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 27),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 27, 1, 0)
		},
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 27, 1),
	    "backend_getnext_instance_below_region_before_instance.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 27, 2),
	    "backend_getnext_instance_below_region_before_instance.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 27), 0);
	agentx_register(ax_s, sessionid2, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 27, 1, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	varbind[0].type = TYPE_ENDOFMIBVIEW;
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, searchrange,
	    varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	varbind[0].name = searchrange[0].end;
	varbind[0].type = TYPE_INTEGER;
	searchrange[0].start = searchrange[0].end;
	searchrange[0].start.include = 1;
	searchrange[0].end.subid[searchrange[0].end.n_subid - 1]++;
	agentx_getnext_handle(__func__, buf, n, 0, sessionid2, searchrange,
	    varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 1);
}

void
backend_getnext_instance_below_region_on_instance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 28, 1, 0),
			.data.int32 = 1
		},
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 28, 1, 1),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 29)
		},
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 28, 1),
	    "backend_getnext_instance_below_region_on_instance.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 28, 2),
	    "backend_getnext_instance_below_region_on_instance.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 28), 0);
	agentx_register(ax_s, sessionid2, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 28, 1, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 1);

	searchrange[0].start.include = 1;
	varbind[0].name = searchrange[0].start;
	varbind[0].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, searchrange,
	    varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 1);
}

void
backend_getnext_instance_below_region_below_instance(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid1, sessionid2;
	struct varbind varbind[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETNEXT, 29, 1, 0, 1),
			.data.int32 = 1
		},
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(MIB_BACKEND_GETNEXT, 29, 1, 1),
			.end = OID_STRUCT(MIB_BACKEND_GETNEXT, 30)
		},
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid1 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 29, 1),
	    "backend_getnext_instance_below_region_below_instance.1");
	sessionid2 = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETNEXT, 29, 2),
	    "backend_getnext_instance_below_region_below_instance.2");
	agentx_register(ax_s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 29), 0);
	agentx_register(ax_s, sessionid2, 1, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETNEXT, 29, 1, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, varbind, 1);

	searchrange[0].start.include = 1;
	varbind[0].name = searchrange[0].start;
	varbind[0].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid1, searchrange,
	    varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    varbind, 1);
}

void
backend_getbulk_nonrep_zero_maxrep_one(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETBULK, 1)
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 1, 0),
			.data.int32 = 1
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 1), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 1, &request, 1);

	memcpy(response, ax_request, sizeof(ax_request));
	while (nvarbind > 0) {
		n = agentx_read(ax_s, buf, sizeof(buf), 1000);
		nout = agentx_getbulk_handle(__func__, buf, n, 0,
		    sessionid, ax_request, nvarbind, ax_response);
		agentx_response(ax_s, buf, NOERROR, 0, ax_response, nout);
		nvarbind -= nout;
	}

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    response, nitems(response));
}

void
backend_getbulk_nonrep_zero_maxrep_two(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETBULK, 2)
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 2, 1),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 2, 2),
			.data.int32 = 2
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 2), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, &request, 1);

	memcpy(response, ax_request, sizeof(ax_request));
	while (nvarbind > 0) {
		n = agentx_read(ax_s, buf, sizeof(buf), 1000);
		nout = agentx_getbulk_handle(__func__, buf, n, 0,
		    sessionid, ax_request, nvarbind, ax_response);
		agentx_response(ax_s, buf, NOERROR, 0, ax_response, nout);
		nvarbind -= nout;
	}

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    response, nitems(response));
}

void
backend_getbulk_nonrep_one_maxrep_one(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 3, 1)
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 3, 2)
		}
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 3, 1, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 3, 2, 0),
			.data.int32 = 2
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 3), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 3), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 1, 1, request, 2);

	memcpy(response, ax_request, sizeof(ax_request));
	while (nvarbind > 0) {
		n = agentx_read(ax_s, buf, sizeof(buf), 1000);
		nout = agentx_getbulk_handle(__func__, buf, n, 0,
		    sessionid, ax_request, nvarbind, ax_response);
		agentx_response(ax_s, buf, NOERROR, 0, ax_response, nout);
		nvarbind -= nout;
	}

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    response, nitems(response));
}

void
backend_getbulk_nonrep_one_maxrep_two(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 4, 1)
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 4, 2)
		}
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 4, 1, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 4, 2, 2),
			.data.int32 = 2
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 4, 2, 3),
			.data.int32 = 3
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 4), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 4), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 1, 2, request, 2);

	memcpy(response, ax_request, sizeof(ax_request));
	while (nvarbind > 0) {
		n = agentx_read(ax_s, buf, sizeof(buf), 1000);
		nout = agentx_getbulk_handle(__func__, buf, n, 0,
		    sessionid, ax_request, nvarbind, ax_response);
		agentx_response(ax_s, buf, NOERROR, 0, ax_response, nout);
		nvarbind -= nout;
	}

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    response, nitems(response));
}

void
backend_getbulk_nonrep_two_maxrep_two(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 1)
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 2)
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 3)
		}
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 1, 0),
			.data.int32 = 1
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 2, 0),
			.data.int32 = 2
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 3, 3),
			.data.int32 = 3
		},
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 5, 3, 4),
			.data.int32 = 4
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 5), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 5), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 2, 2, request, 3);

	memcpy(response, ax_request, sizeof(ax_request));
	while (nvarbind > 0) {
		n = agentx_read(ax_s, buf, sizeof(buf), 1000);
		nout = agentx_getbulk_handle(__func__, buf, n, 0,
		    sessionid, ax_request, nvarbind, ax_response);
		agentx_response(ax_s, buf, NOERROR, 0, ax_response, nout);
		nvarbind -= nout;
	}

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    response, nitems(response));
}

void
backend_getbulk_nonrep_negative(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_GETBULK, 6)
	}, ax_request[] = {
		{
			.type = TYPE_INTEGER,
			.name = OID_STRUCT(MIB_BACKEND_GETBULK, 6),
			.data.int32 = 1
		}
	}, ax_response[nitems(ax_request)], response[nitems(ax_request)];
	int32_t requestid;
	char buf[1024];
	size_t n, nvarbind = nitems(ax_request), nout;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 6), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_GETBULK, 6), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, -1, 1, &request, 1);

	agentx_timeout(ax_s, 1000);
	snmp_timeout(snmp_s, 1);
}

/* Assume that everything is registered under 1.3.* */
void
backend_getbulk_endofmibview(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(2, 0),
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(2, 0),
		.end = OID_STRUCT(2, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 7), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(2, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	varbind.type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &varbind, 1);
	varbind.name.n_subid--;

	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    &varbind, 1);
}

void
backend_getbulk_endofmibview_second_rep(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(2 ,0),
			.data.int32 = 1
		},
		{
			.type = TYPE_ENDOFMIBVIEW,
			.name = OID_STRUCT(2, 0, 0),
		}
	};
	struct searchrange searchrange = {
		.start = OID_STRUCT(2, 0),
		.end = OID_STRUCT(2, 1)
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 8), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(2, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, request, 1);

	request[0].name.subid[request[0].name.n_subid++] = 0;
	request[0].type = TYPE_INTEGER;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    request, 1);
	agentx_response(ax_s, buf, NOERROR, 0, request, 1);

	searchrange.start = request[0].name;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange,
	    &request[1], 1);
	agentx_response(ax_s, buf, NOERROR, 0, &request[1], 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    request, 2);
}

void
backend_getbulk_endofmibview_two_varbinds(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind request[] = {
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(2 ,0),
			.data.int32 = 1
		},
		{
			.type = TYPE_NULL,
			.name = OID_STRUCT(2, 0, 0),
		},
		{
			.type = TYPE_ENDOFMIBVIEW,
			.name = OID_STRUCT(2, 0, 0),
		},
		{
			.type = TYPE_ENDOFMIBVIEW,
			.name = OID_STRUCT(2, 0, 0),
		}
	};
	struct searchrange searchrange[] = {
		{
			.start = OID_STRUCT(2, 0),
			.end = OID_STRUCT(2, 1)
		},
		{
			.start = OID_STRUCT(2, 0, 0),
			.end = OID_STRUCT(2, 1)
		},
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_GETBULK, 9), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(2, 0), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, request, 2);

	request[0].name.subid[request[0].name.n_subid++] = 0;
	request[0].type = TYPE_INTEGER;
	request[1].type = TYPE_ENDOFMIBVIEW;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, searchrange,
	    request, 2);
	agentx_response(ax_s, buf, NOERROR, 0, request, 2);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, &searchrange[1],
	    &request[1], 1);
	agentx_response(ax_s, buf, NOERROR, 0, &request[1], 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOERROR, 0,
	    request, 4);
}

void
backend_error_get_toobig(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 1, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 1), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 1), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, TOOBIG, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, TOOBIG, 1,
	    &varbind, 1);
}

void
backend_error_get_nosuchname(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 2, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 2), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 2), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, NOSUCHNAME, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOSUCHNAME, 1,
	    &varbind, 1);
}

void
backend_error_get_badvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 3, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 3), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 3), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, BADVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_readonly(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 4, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 4), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 4), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, READONLY, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_generr(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 5, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 5), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 5), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, GENERR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_noaccess(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 6, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 6), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 5), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, NOACCESS, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_wrongtype(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 7, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 7), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 7), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, WRONGTYPE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_wronglength(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 8, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 8), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 8), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, WRONGLENGTH, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_wrongencoding(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 9, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 9), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 9), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, WRONGENCODING, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_wrongvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 10, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 10), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 10), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, WRONGVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_nocreation(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 11, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 11), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 11), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, NOCREATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_inconsistentvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 12, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 12), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 12), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INCONSISTENTVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_resourceunavailable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 13, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 13), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 13), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, WRONGVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_commitfailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 14, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 14), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 14), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, COMMITFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_undofailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 15, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 15), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 15), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, UNDOFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_authorizationerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 16, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 16), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 16), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, AUTHORIZATIONERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, AUTHORIZATIONERROR, 1,
	    &varbind, 1);
}

void
backend_error_get_notwritable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 17, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 17), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 17), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, NOTWRITABLE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_inconsistentname(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 18, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 18), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 18), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INCONSISTENTNAME, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_openfailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 19, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 19), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 19), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, OPENFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_notopen(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 20, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 20), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 20), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, NOTOPEN, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_indexwrongtype(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 21, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 21), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 21), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INDEXWRONGTYPE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_indexalreadyallocated(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 22, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 22), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 22), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INDEXALREADYALLOCATED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_indexnonavailable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 23, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 23), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 23), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INDEXNONEAVAILABLE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_indexnotallocated(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 24, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 24), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 24), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, INDEXNOTALLOCATED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_unsupportedcontext(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 25, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 25), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 25), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, UNSUPPORTEDCONTEXT, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_duplicateregistration(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 26, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 26), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 26), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, DUPLICATEREGISTRATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_unknownregistration(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 27, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 27), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 27), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, UNKNOWNREGISTRATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_parseerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 28, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 28), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 28), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, PARSEERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_requestdenied(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 29, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 29), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 29), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, REQUESTDENIED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_processingerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 30, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 30), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 30), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, PROCESSINGERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_get_nonstandard(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 31, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 31), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 31), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	agentx_response(ax_s, buf, 0xFFFF, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_toobig(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 32),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 32), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 32), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, TOOBIG, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, TOOBIG, 1,
	    &varbind, 1);
}

void
backend_error_getnext_nosuchname(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 33),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 33), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 33), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, NOSUCHNAME, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, NOSUCHNAME, 1,
	    &varbind, 1);
}

void
backend_error_getnext_badvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 34),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 34), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 34), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, BADVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_readonly(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 35),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 35), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 35), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, READONLY, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_generr(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 36),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 36), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 36), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, GENERR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_noaccess(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 37),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 37), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 37), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, NOACCESS, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_wrongtype(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 38),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 38), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 38), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, WRONGTYPE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_wronglength(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 39),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 39), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 39), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, WRONGLENGTH, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_wrongencoding(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 40),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 40), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 40), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, WRONGENCODING, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_wrongvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 41),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 41), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 41), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, WRONGVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_nocreation(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 42),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 42), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 42), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, NOCREATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_inconsistentvalue(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 43),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 43), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 43), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INCONSISTENTVALUE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_resourceunavailable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 44),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 44), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 44), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, RESOURCEUNAVAILABLE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_commitfailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 45),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 45), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 45), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, COMMITFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_undofailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 46),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 46), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 46), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, UNDOFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_authorizationerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 47),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 47), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 47), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, AUTHORIZATIONERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_notwritable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 48),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 48), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 48), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, NOTWRITABLE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_inconsistentname(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 49),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 49), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 49), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INCONSISTENTNAME, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_openfailed(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 50),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 50), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 50), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, OPENFAILED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_notopen(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 51),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 51), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 51), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, NOTOPEN, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_indexwrongtype(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 52),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 52), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 52), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INDEXWRONGTYPE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_indexalreadyallocated(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 53),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 53), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 53), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INDEXALREADYALLOCATED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_indexnonavailable(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 54),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 54), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 54), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INDEXNONEAVAILABLE, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_indexnotallocated(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 55),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 55), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 55), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, INDEXNOTALLOCATED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_unsupportedcontext(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 56),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 56), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 56), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, UNSUPPORTEDCONTEXT, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_duplicateregistration(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 57),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 57), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 57), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, DUPLICATEREGISTRATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_unknownregistration(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 58),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 58), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 58), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, UNKNOWNREGISTRATION, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_parseerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 59),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 59), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 59), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, PARSEERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_requestdenied(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 60),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 60), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 60), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, REQUESTDENIED, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_processingerror(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 61),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 61), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 61), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, PROCESSINGERROR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getnext_nonstandard(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 62),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 62), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 62), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getnext(snmp_s, community, 0, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, 0xFFFF, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getbulk_firstrepetition(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 63),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 63), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 63), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.n_subid--;
	agentx_response(ax_s, buf, GENERR, 1, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}

void
backend_error_getbulk_secondrepetition(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_BACKEND_ERROR, 64),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_BACKEND_ERROR, 64), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_BACKEND_ERROR, 64), 0);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_getbulk(snmp_s, community, 0, 0, 2, &varbind, 1);

	varbind.name.subid[varbind.name.n_subid++] = 0;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, 0, NOERROR, &varbind, 1);
	varbind.name.subid[varbind.name.n_subid - 1] = 1;
	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_getnext_handle(__func__, buf, n, 0, sessionid, NULL, &varbind, 1);
	varbind.name.subid[varbind.name.n_subid - 1] = 0;
	varbind.type = TYPE_NULL;
	agentx_response(ax_s, buf, 0, GENERR, &varbind, 1);

	varbind.name.n_subid--;
	snmpv2_response_validate(snmp_s, 1000, community, requestid, GENERR, 1,
	    &varbind, 1);
}
