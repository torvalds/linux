#include <sys/socket.h>

#include <ber.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "regress.h"

#define MIB_SUBAGENT_TRANSPORT_TCP MIB_SUBAGENT_TRANSPORT, 1
#define MIB_TRANSPORT_TCP MIB_TRANSPORT, 1

void
transport_tcp_get(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_TRANSPORT_TCP, 1, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_TRANSPORT_TCP, 1), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_TRANSPORT_TCP, 1), 0);

	salen = snmp_resolve(SOCK_STREAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_STREAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
transport_tcp_disconnect(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_TRANSPORT_TCP, 2, 0),
		.data.int32 = 1
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_TRANSPORT_TCP, 2), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_TRANSPORT_TCP, 2), 0);

	salen = snmp_resolve(SOCK_STREAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_STREAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	close(snmp_s);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
}

void
transport_tcp_double_get_disconnect(void)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_TRANSPORT_TCP, 2, 0),
		.data.int32 = 1
	};
	struct ber_element *message;
	struct ber ber = {};
	char buf[1024];
	ssize_t buflen, writelen;
	void *ptr;
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_TRANSPORT_TCP, 2), __func__);
	agentx_register(ax_s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_TRANSPORT_TCP, 2), 0);

	salen = snmp_resolve(SOCK_STREAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_STREAM, sa, salen);

	message = snmpv2_build(community, REQUEST_GET, 0, 0, 0, &varbind, 1);

	if (ober_write_elements(&ber, message) == -1)
		err(1, "ober_write_elements");

	buflen = ober_get_writebuf(&ber, &ptr);
	/* send two copies in a single window */
	memcpy(buf, ptr, buflen);
	memcpy(buf + buflen, ptr, buflen);

	if ((writelen = write(snmp_s, buf, buflen * 2)) == -1)
		err(1, "write");
	if (writelen != buflen * 2)
		errx(1, "write: short write");

	if (verbose) {
		printf("SNMP send(%d):\n", snmp_s);
		smi_debug_elements(message);
	}
	ober_free_elements(message);
	ober_free(&ber);
	
	close(snmp_s);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);

	varbind.type = TYPE_INTEGER;
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);

	n = agentx_read(ax_s, buf, sizeof(buf), 1000);
	agentx_get_handle(__func__, buf, n, 0, sessionid, &varbind, 1);
	agentx_response(ax_s, buf, NOERROR, 0, &varbind, 1);
}
