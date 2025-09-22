#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <endian.h>
#include <err.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "regress.h"

#define AGENTX_SOCKET "/tmp/agentx"

#define SUBAGENT_DESCR(name) name, strlen(name)

#define AGENTX_OPEN_PDU 1
#define AGENTX_CLOSE_PDU 2
#define AGENTX_REGISTER_PDU 3
#define AGENTX_UNREGISTER_PDU 4
#define AGENTX_GET_PDU 5
#define AGENTX_GETNEXT_PDU 6
#define AGENTX_GETBULK_PDU 7
#define AGENTX_TESTSET_PDU 8
#define AGENTX_COMMITSET_PDU 9
#define AGENTX_UNDOSET_PDU 10
#define AGENTX_CLEANUPSET_PDU 11
#define AGENTX_NOTIFY_PDU 12
#define AGENTX_PING_PDU 13
#define AGENTX_INDEXALLOCATE_PDU 14
#define AGENTX_INDEXDEALLOCATE_PDU 15
#define AGENTX_ADDAGENTCAPS_PDU 16
#define AGENTX_REMOVEAGENTCAPS_PDU 17
#define AGENTX_RESPONSE_PDU 18

#define NOAGENTXERROR 0
#define OPENFAILED 256
#define NOTOPEN 257
#define INDEXWRONGTYPE 258
#define INDEXALREADYALLOCATED 259
#define INDEXNONEAVAILABLE 260
#define INDEXNOTALLOCATED 261
#define UNSUPPORTEDCONTEXT 262
#define DUPLICATEREGISTRATION 263
#define UNKNOWNREGISTRATION 264
#define UNKNOWNAGENTCAPS 265
#define PARSEERROR 266
#define REQUESTDENIED 267
#define PROCESSINGERROR 268

#define INSTANCE_REGISTRATION (1 << 0)
#define NEW_INDEX (1 << 1)
#define ANY_INDEX (1 << 2)
#define NON_DEFAULT_CONTEXT (1 << 3)
#define NETWORK_BYTE_ORDER (1 << 4)

#define INTEGER 2
#define OCTETSTRING 4
#define AXNULL 5
#define OBJECTIDENTIFIER 6
#define IPADDRESS 64
#define COUNTER32 65
#define GAUGE32 66
#define TIMETICKS 67
#define OPAQUE 68
#define COUNTER64 70
#define NOSUCHOBJECT 128
#define NOSUCHINSTANCE 129
#define ENDOFMIBVIEW 130

#define MESSAGE_NBO(msg) (((const char *)msg->buf)[2] & NETWORK_BYTE_ORDER)

#define p16toh(header, value) (uint16_t)(header->flags & NETWORK_BYTE_ORDER ? \
    be16toh(value) : le16toh(value))
#define p32toh(header, value) (uint32_t)(header->flags & NETWORK_BYTE_ORDER ? \
    be32toh(value) : le32toh(value))
#define p64toh(header, value) (uint64_t)(header->flags & NETWORK_BYTE_ORDER ? \
    be64toh(value) : le64toh(value))

struct header {
	uint8_t		 version;
	uint8_t		 type;
	uint8_t		 flags;
	uint8_t		 reserved;
	uint32_t	 sessionid;
	uint32_t	 transactionid;
	uint32_t	 packetid;
	uint32_t	 payload_length;
} __attribute__ ((packed));

struct message {
	void		*buf;
	size_t		 len;
	size_t		 size;
};

void agentx_close_validate(const char *, const void *,
    size_t, uint8_t, uint8_t);
static void agentx_response_validate(const char *, const void *, size_t,
    uint8_t, uint32_t, enum error, uint16_t, struct varbind *, size_t);
static void message_add_uint8(struct message *, uint8_t);
static void message_add_uint16(struct message *, uint16_t);
static void message_add_uint32(struct message *, uint32_t);
static void message_add_uint64(struct message *, uint64_t);
static void message_add_nstring(struct message *, const void *, uint32_t);
static void message_add_oid(struct message *, const uint32_t[], uint8_t,
    uint8_t);
static void message_add_varbind(struct message *, struct varbind *);
static void message_add_header(struct message *, uint8_t, uint8_t, uint8_t,
    uint32_t, uint32_t, uint32_t);
static void message_add(struct message *, const void *, size_t);
static void message_release(struct message *);
static size_t poid(const char *, const struct header *, const uint8_t *, size_t,
    struct oid *);

void agentx_write(int, struct message *);

void
agentx_open_nnbo(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 1), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_nbo(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, NETWORK_BYTE_ORDER, 0, 0,
	    packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 2), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, NETWORK_BYTE_ORDER,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

/*
 * The version byte is the only identifier we have to determine if we can
 * "trust" the rest of the packet structure. If this doesn't match the server
 * can't return a parseError message, because it doesn't know the packetid.
 */
void
agentx_open_invalidversion(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 0xFF, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 3), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	if (agentx_read(s, repl, sizeof(repl), 1000) != 0)
		errx(1, "%s: Unexpected reply", __func__);
	close(s);
}

void
agentx_open_ignore_sessionid(void)
{
	int s1, s2;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	struct header *header = (struct header *)repl;

	s1 = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 4, 1), 0);
	message_add_nstring(&msg,
	    SUBAGENT_DESCR("agentx_open_ignore_sessionid.1"));

	agentx_write(s1, &msg);
	
	n = agentx_read(s1, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);

	sessionid = le32toh(header->sessionid);

	s2 = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, sessionid, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 4, 2), 0);
	message_add_nstring(&msg,
	    SUBAGENT_DESCR("agentx_open_ignore_sessionid.2"));

	agentx_write(s2, &msg);
	
	n = agentx_read(s2, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	if (sessionid == le32toh(header->sessionid))
		errx(1, "%s: sessionid not ignored", __func__);

	message_release(&msg);
	close(s1);
	close(s2);
}

void
agentx_open_invalid_oid(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t oid[129];

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, oid, 129, 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	if ((n = agentx_read(s, repl, sizeof(repl), 1000)) != 0)
		errx(1, "unexpected reply");

	message_release(&msg);
	close(s);
}

void
agentx_open_descr_too_long(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	char descr[256];

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 6), 0);
	memset(descr, 'a', sizeof(descr));
	message_add_nstring(&msg, descr, sizeof(descr));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_descr_invalid(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, 0, 0, 0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 7), 0);
	/* Too long start-byte (5 ones instead of max 4) followed by ascii */
	message_add_nstring(&msg, SUBAGENT_DESCR("a\373a"));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_context(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, NON_DEFAULT_CONTEXT, 0, 0,
	    packetid);
	message_add_nstring(&msg, "ctx", 3);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 8), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_instance_registration(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, INSTANCE_REGISTRATION, 0,
	    0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 9), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_new_index(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, NEW_INDEX, 0,
	    0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 10), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_open_any_index(void)
{
	int s;
	struct message msg = {};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_OPEN_PDU, ANY_INDEX, 0,
	    0, packetid);
	message_add_uint8(&msg, 0);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, OID_ARG(MIB_SUBAGENT_OPEN, 11), 0);
	message_add_nstring(&msg, SUBAGENT_DESCR(__func__));

	agentx_write(s, &msg);
	
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_notopen(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0, 0, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_invalid_sessionid(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 2), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0,
	    sessionid + 1, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_default(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 3), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_context(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 4), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, NON_DEFAULT_CONTEXT,
	    sessionid, 0, packetid);
	message_add_nstring(&msg, "ctx", 3);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, UNSUPPORTEDCONTEXT, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_invalid_version(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 5), __func__);
	message_add_header(&msg, 0xFF, AGENTX_PING_PDU, INSTANCE_REGISTRATION,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_close_validate(__func__, repl, n, 0, REASONPROTOCOLERROR);
	message_release(&msg);
	close(s);
}

void
agentx_ping_instance_registration(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 6), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, INSTANCE_REGISTRATION,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_new_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 7), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, NEW_INDEX,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_any_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 8), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, ANY_INDEX,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_nbo_nnbo(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 9), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_ping_nnbo_nbo(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 9), __func__);
	message_add_header(&msg, 1, AGENTX_PING_PDU, NETWORK_BYTE_ORDER,
	    sessionid, 0, packetid);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, NETWORK_BYTE_ORDER,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

/*
 * Test that everything continues running in double exception condition
 */
void
agentx_ping_invalid_version_close(void)
{
	struct sockaddr_storage ss;
	struct message msg = {};
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t salen;
	int snmp_s, ax_s;
	uint32_t sessionid, packetid;
	struct varbind varbind = {
		.type = TYPE_NULL,
		.name = OID_STRUCT(MIB_SUBAGENT_PING, 10, 0),
	};
	int32_t requestid;
	char buf[1024];
	size_t n;

	ax_s = agentx_connect(axsocket);
	sessionid = agentx_open(ax_s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_PING, 10), __func__);
	message_add_header(&msg, 0xFF, AGENTX_PING_PDU, INSTANCE_REGISTRATION,
	    sessionid, 0, packetid);

	agentx_write(ax_s, &msg);
	close(ax_s);

	salen = snmp_resolve(SOCK_DGRAM, hostname, servname, sa);
	snmp_s = snmp_connect(SOCK_DGRAM, sa, salen);
	requestid = snmpv2_get(snmp_s, community, 0, &varbind, 1);

	varbind.type = TYPE_NOSUCHOBJECT;

	snmpv2_response_validate(snmp_s, 1000, community, requestid, 0, 0,
	    &varbind, 1);
}

void
agentx_close_notopen(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, 0, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasonother(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 2), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasonparseerror(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONPARSEERROR, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 3), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasonprotocolerror(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONPROTOCOLERROR, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 4), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasontimouts(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONTIMEOUTS, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 5), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasonshutdown(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONSHUTDOWN, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 6), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasonbymanager(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONBYMANAGER, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 7), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR,
	    0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_reasoninvalid(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = 0xFF, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 8), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_close_validate(__func__, repl, n, 0, REASONPROTOCOLERROR);
	message_release(&msg);
	close(s);
}

void
agentx_close_single(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid1, sessionid2;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	
	sessionid1 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 9, 1), "agentx_close_single.1");
	sessionid2 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 9, 2), "agentx_close_single.2");
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid1, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR,
	    0, NULL, 0);

	/* Test that only the second session is still open */
	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0, sessionid2, 0, packetid);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0, sessionid1, 0, packetid);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOTOPEN, 0, NULL, 0);
	
	message_release(&msg);
	close(s);
}

void
agentx_close_notowned(void)
{
	int s1, s2;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid1, sessionid2;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s1 = agentx_connect(axsocket);
	s2 = agentx_connect(axsocket);
	
	sessionid1 = agentx_open(s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 10, 1), "agentx_close_notowned.1");
	sessionid2 = agentx_open(s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 10, 2), "agentx_close_notowned.2");
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid1, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s2, &msg);

	n = agentx_read(s2, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN,
	    0, NULL, 0);

	/* Test that both sessions are still open */
	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0, sessionid1, 0, packetid);
	agentx_write(s1, &msg);
	n = agentx_read(s1, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_PING_PDU, 0, sessionid2, 0, packetid);
	agentx_write(s2, &msg);
	n = agentx_read(s2, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	
	message_release(&msg);
	close(s1);
	close(s2);
}

void
agentx_close_invalid_sessionid(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 11), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid + 1, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_context(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 12), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, NON_DEFAULT_CONTEXT,
	    sessionid, 0, packetid);
	message_add_nstring(&msg, "ctx", 3);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, PARSEERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_invalid_version(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 13), __func__);
	message_add_header(&msg, 0xFF, AGENTX_CLOSE_PDU, 0, sessionid, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_close_validate(__func__, repl, n, 0, REASONPROTOCOLERROR);

	message_release(&msg);
	close(s);
}

void
agentx_close_instance_registration(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 14), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, INSTANCE_REGISTRATION,
	    sessionid, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_new_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 15), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, NEW_INDEX,
	    sessionid, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_any_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 16), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, ANY_INDEX,
	    sessionid, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0,
	    packetid, PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_close_nnbo_nbo(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	const uint8_t reason = REASONOTHER, zero[3] = {0};

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_CLOSE, 17), __func__);
	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, NETWORK_BYTE_ORDER,
	    sessionid, 0, packetid);
	message_add(&msg, &reason, sizeof(reason));
	message_add(&msg, zero, sizeof(zero));

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, NETWORK_BYTE_ORDER,
	    packetid, NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_notopen(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, 0, 0, packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 1, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_invalid_sessionid(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 2), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid + 1, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 2, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_default(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 3), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 3, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_context(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 4), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, NON_DEFAULT_CONTEXT,
	    sessionid, 0, packetid);
	message_add_nstring(&msg, "ctx", 3);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 4, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNSUPPORTEDCONTEXT, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_invalid_version(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 5), __func__);
	message_add_header(&msg, 0xFF, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 5, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_close_validate(__func__, repl, n, 0, REASONPROTOCOLERROR);
	message_release(&msg);
	close(s);
}

void
agentx_register_instance_registration(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 6), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, INSTANCE_REGISTRATION,
	    sessionid, 0, packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 6, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_new_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 7), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, NEW_INDEX,
	    sessionid, 0, packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 7, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_any_index(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 8), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, NEW_INDEX,
	    sessionid, 0, packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 8, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    PARSEERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_register_duplicate_self(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 9), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 9, 0), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 9, 0), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    DUPLICATEREGISTRATION, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_duplicate_twocon(void)
{
	int s1, s2;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid1, sessionid2;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s1 = agentx_connect(axsocket);
	s2 = agentx_connect(axsocket);
	sessionid1 = agentx_open(s1, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 10, 1),
	    "agentx_register_duplicate_twocon.1");
	sessionid2 = agentx_open(s2, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 10, 2),
	    "agentx_register_duplicate_twocon.2");
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid1, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 10, 0), 0);
	agentx_write(s1, &msg);
	n = agentx_read(s1, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid2, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 10, 0), 0);
	agentx_write(s2, &msg);
	n = agentx_read(s2, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    DUPLICATEREGISTRATION, 0, NULL, 0);

	message_release(&msg);
	close(s1);
	close(s2);
}

void
agentx_register_duplicate_priority(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 11), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 11, 0), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	priority = 1;
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 11, 0), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_range(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 10;
	uint8_t timeout = 0, priority = 127, range_subid = 11, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 12), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 12, 1), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_range_invalidupperbound(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 1;
	uint8_t timeout = 0, priority = 127, range_subid = 11, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 13), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 13, 2), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    PARSEERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_range_single(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 1;
	uint8_t timeout = 0, priority = 127, range_subid = 11, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 14), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 14, 1), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_range_overlap_single(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 10;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 15), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 15, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	range_subid = 11;
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 15, 1), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    DUPLICATEREGISTRATION, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_single_overlap_range(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 10;
	uint8_t timeout = 0, priority = 127, range_subid = 11, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 16), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 16, 1), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	range_subid = 0;
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 16, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    DUPLICATEREGISTRATION, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_range_overlap_range(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid, upperbound = 10;
	uint8_t timeout = 0, priority = 127, range_subid = 11, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 17), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 17, 1), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	upperbound = 15;
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 17, 6), 0);
	message_add_uint32(&msg, upperbound);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    DUPLICATEREGISTRATION, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_below(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 18), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 18, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 18, 1, 0), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_above(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 19), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 19, 1, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	packetid = arc4random();
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_REGISTER, 19, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_register_restricted(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t timeout = 0, priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_REGISTER, 20), __func__);
	message_add_header(&msg, 1, AGENTX_REGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(SYSORTABLE, 1, 1), 0);
	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);
	agentx_response_validate(__func__, repl, n, 0, packetid,
	    REQUESTDENIED, 0, NULL, 0);

	message_release(&msg);
	close(s);
}

void
agentx_unregister_notopen(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, 0, 0, packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 1, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_invalid_sessionid(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 2), __func__);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid + 1, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 2, 0), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOTOPEN, 0,
	    NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_notregistered(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 3), __func__);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 3), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_single(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 4), __func__);
	agentx_register(s, sessionid, 0, 0, 127, 0,
	    OID_ARG(MIB_UNREGISTER, 4), 0);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 4), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_single_notowned(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid1, sessionid2;
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid1 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 5, 1),
	    "agentx_unregister_single_notowned.1");
	sessionid2 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 5, 2),
	    "agentx_unregister_single_notowned.2");
	agentx_register(s, sessionid1, 0, 0, 127, 0,
	    OID_ARG(MIB_UNREGISTER, 5, 1), 0);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid2, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 5, 1), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 11, reserved = 0;
	uint32_t upperbound = 10;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 6), __func__);
	agentx_register(s, sessionid, 0, 0, 127, 11,
	    OID_ARG(MIB_UNREGISTER, 6, 1), 10);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 6, 1), 0);
	message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    NOAGENTXERROR, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range_single(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 0, reserved = 0;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 7), __func__);
	agentx_register(s, sessionid, 0, 0, 127, 11,
	    OID_ARG(MIB_UNREGISTER, 6, 1), 10);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 7, 1), 0);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range_subset(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 11, reserved = 0;
	uint32_t upperbound = 5;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 8), __func__);
	agentx_register(s, sessionid, 0, 0, 127, 11,
	    OID_ARG(MIB_UNREGISTER, 8, 1), 10);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 8, 1), 0);
	message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range_extra(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 11, reserved = 0;
	uint32_t upperbound = 10;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 9), __func__);
	agentx_register(s, sessionid, 0, 0, 127, 11,
	    OID_ARG(MIB_UNREGISTER, 9, 1), upperbound);
	upperbound = 15;
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 9, 1), 0);
	message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range_priority(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid;
	uint8_t priority = 127, range_subid = 11, reserved = 0;
	uint32_t upperbound = 10;

	s = agentx_connect(axsocket);
	sessionid = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 10), __func__);
	agentx_register(s, sessionid, 0, 0, priority, 11,
	    OID_ARG(MIB_UNREGISTER, 10, 1), upperbound);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid, 0,
	    packetid);
	priority = 128;
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 10, 1), 0);
	message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_unregister_range_notowned(void)
{
	int s;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint32_t sessionid1, sessionid2;
	uint8_t priority = 127, range_subid = 11, reserved = 0;
	uint32_t upperbound = 10;

	s = agentx_connect(axsocket);
	sessionid1 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 11, 1),
	    "agentx_unregister_range_notowned.1");
	sessionid2 = agentx_open(s, 0, 0,
	    OID_ARG(MIB_SUBAGENT_UNREGISTER, 11, 2),
	    "agentx_unregister_range_notowned.2");
	agentx_register(s, sessionid1, 0, 0, priority, 11,
	    OID_ARG(MIB_UNREGISTER, 11, 1), upperbound);
	message_add_header(&msg, 1, AGENTX_UNREGISTER_PDU, 0, sessionid2, 0,
	    packetid);
	priority = 128;
	message_add(&msg, &reserved, sizeof(reserved));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, OID_ARG(MIB_UNREGISTER, 10, 1), 0);
	message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid,
	    UNKNOWNREGISTRATION, 0, NULL, 0);
	message_release(&msg);
	close(s);
}

void
agentx_get_handle(const char *test, const void *buf, size_t len,
    uint8_t flags, uint32_t sessionid, struct varbind *varbind,
    size_t nvarbind)
{
	const struct header *header = buf;
	struct oid start, end, zero = {0};
	const uint8_t *u8;
	uint32_t u32;
	uint16_t u16;
	size_t sublen, i, j;
	char oid1[512], oid2[512];
	struct varbind *pool;

	if (len < sizeof(*header))
		errx(1, "%s: unexpected pdu message size received: %zu", test, len);
	if (header->version != 1)
		errx(1, "%s: invalid pdu version", test);
	if (header->type != AGENTX_GET_PDU)
		errx(1, "%s: invalid pdu type received (%hhu/5)", test, header->type);
	if (header->flags != flags)
		errx(1, "%s: invalid get pdu flags received (%hhu/%hhu)",
		    test, header->flags, flags);
	if (header->reserved != 0)
		errx(1, "%s: invalid get pdu reserved received", test);
	if (p32toh(header, header->sessionid) != sessionid)
		errx(1, "%s: unexpected get pdu sessionid (%u/%u)", test,
		    p32toh(header, header->sessionid), sessionid);
	if (p32toh(header, header->payload_length) > len - sizeof(*header))
		errx(1, "%s: unexpected get pdu payload length received",
		    test);

	buf += sizeof(*header);
	len = p32toh(header, header->payload_length);

	if ((pool = calloc(nvarbind, sizeof(*pool))) == NULL)
		err(1, NULL);
	memcpy(pool, varbind, nvarbind * sizeof(*pool));

	for (i = 0; len > 0; i++) {
		sublen = poid(test, header, buf, len, &start);
		buf += sublen;
		len -= sublen;
		sublen = poid(test, header, buf, len, &end);
		buf += sublen;
		len -= sublen;

		if (oid_cmp(&end, &zero) != 0)
			errx(1, "%s: unexpected searchrange end: (%s/%s)", test,
			    oid_print(&end, oid1, sizeof(oid1)),
			    oid_print(&zero, oid2, sizeof(oid2)));
		if (start.include != 0 || end.include != 0)
			errx(1, "%s: unexpected searchrange include: (%s)",
			    test, oid_print(&start, oid1, sizeof(oid1)));
		for (j = 0; j < nvarbind; j++) {
			if (oid_cmp(&pool[j].name, &start) == 0)
				break;
		}
		if (j == nvarbind)
			warnx("%s: unexpected searchrange start: " "(%s)", test,
			    oid_print(&start, oid1, sizeof(oid1)));

		varbind[i] = pool[j];
		pool[j].name.n_subid = 0;
	}
	free(pool);
}

void
agentx_getnext_handle(const char *test, const void *buf, size_t len,
    uint8_t flags, uint32_t sessionid, struct searchrange *searchrange,
    struct varbind *varbind, size_t nvarbind)
{
	const struct header *header = buf;
	struct oid start, end, zero = {0};
	const uint8_t *u8;
	uint32_t u32;
	uint16_t u16;
	size_t sublen, i, j, match;
	char oid1[512], oid2[512], oid3[512], oid4[512];
	struct varbind *pool;

	if (len < sizeof(*header))
		errx(1, "%s: unexpected pdu message size received: %zu", test, len);
	if (header->version != 1)
		errx(1, "%s: invalid pdu version", test);
	if (header->type != AGENTX_GETNEXT_PDU)
		errx(1, "%s: invalid pdu type received (%hhu/6)", test, header->type);
	if (header->flags != flags)
		errx(1, "%s: invalid get pdu flags received (%hhu/%hhu)",
		    test, header->flags, flags);
	if (header->reserved != 0)
		errx(1, "%s: invalid get pdu reserved received", test);
	if (p32toh(header, header->sessionid) != sessionid)
		errx(1, "%s: unexpected get pdu sessionid (%u/%u)", test,
		    p32toh(header, header->sessionid), sessionid);
	if (p32toh(header, header->payload_length) > len - sizeof(*header))
		errx(1, "%s: unexpected get pdu payload length received",
		    test);

	buf += sizeof(*header);
	len = p32toh(header, header->payload_length);

	if ((pool = calloc(nvarbind, sizeof(*pool))) == NULL)
		err(1, NULL);
	memcpy(pool, varbind, nvarbind * sizeof(*pool));

	for (i = 0; len > 0; i++) {
		sublen = poid(test, header, buf, len, &start);
		buf += sublen;
		len -= sublen;
		sublen = poid(test, header, buf, len, &end);
		buf += sublen;
		len -= sublen;

		if ((start.include != 1 && start.include != 0) ||
		    end.include != 0)
			errx(1, "%s: unexpected searchrange include: (%s-%s)",
			    test, oid_print(&start, oid1, sizeof(oid1)),
			    oid_print(&end, oid2, sizeof(oid2)));

		match = (size_t)-1;
		for (j = 0; j < nvarbind; j++) {
			if (oid_cmp(&pool[j].name, &start) == 0 &&
			    pool[j].type == TYPE_ENDOFMIBVIEW) {
				match = j;
			} else if (oid_cmp(&pool[j].name, &start) < 0 ||
			    (!start.include &&
			    oid_cmp(&pool[j].name, &start) == 0) ||
			    oid_cmp(&pool[j].name, &end) >= 0)
				continue;
			if (match == (size_t)-1)
				match = j;
			else {
				if (oid_cmp(&pool[j].name,
				    &pool[match].name) < 0)
					match = j;
			}
		}
		if (match == (size_t)-1)
			errx(1, "%s: unexpected searchrange start: " "(%s-%s)",
			    test,
			    oid_print(&start, oid1, sizeof(oid1)),
			    oid_print(&end, oid2, sizeof(oid2)));
		if (searchrange != NULL) {
			if (oid_cmp(&searchrange[match].start, &start) != 0 ||
			    oid_cmp(&searchrange[match].end, &end) != 0 ||
			    searchrange[match].end.include != end.include ||
			    searchrange[match].start.include != start.include)
				errx(1, "%s: searchrange did not match "
				    "(%s-%s/%s-%s)", test,
				    oid_print(&start, oid1, sizeof(oid1)),
				    oid_print(&end, oid2, sizeof(oid2)),
				    oid_print(&searchrange[match].start, oid3,
				    sizeof(oid3)),
				    oid_print(&searchrange[match].end, oid4,
				    sizeof(oid4)));
		}

		varbind[i] = pool[match];
		pool[match].name.n_subid = 0;
	}
	free(pool);
}

/*
 * Don't assume a specific sequence of requests here so we can more easily
 * migrate to getbulk in agentx.
 */
size_t
agentx_getbulk_handle(const char *test, const void *buf, size_t len,
    uint8_t flags, int32_t sessionid, struct varbind *varbind, size_t nvarbind,
    struct varbind *outvarbind)
{
	const struct header *header = buf;
	struct oid start, end, zero = {0};
	const uint8_t *u8;
	uint16_t nonrep, maxrep;
	uint32_t u32;
	uint16_t u16;
	size_t sublen, i, j, match;
	size_t nout = 0;
	char oid1[512], oid2[512], oid3[512], oid4[512];

	if (len < sizeof(*header))
		errx(1, "%s: unexpected pdu message size received: %zu", test, len);
	if (header->version != 1)
		errx(1, "%s: invalid pdu version", test);
	if (header->type != AGENTX_GETNEXT_PDU &&
	    header->type != AGENTX_GETBULK_PDU)
		errx(1, "%s: invalid pdu type received (%hhu/[67])",
		    test, header->type);
	if (header->flags != flags)
		errx(1, "%s: invalid get pdu flags received (%hhu/%hhu)",
		    test, header->flags, flags);
	if (header->reserved != 0)
		errx(1, "%s: invalid get pdu reserved received", test);
	if (p32toh(header, header->sessionid) != sessionid)
		errx(1, "%s: unexpected get pdu sessionid (%u/%u)", test,
		    p32toh(header, header->sessionid), sessionid);
	if (p32toh(header, header->payload_length) > len - sizeof(*header))
		errx(1, "%s: unexpected get pdu payload length received",
		    test);

	buf += sizeof(*header);
	len = p32toh(header, header->payload_length);

	if (header->type == AGENTX_GETBULK_PDU) {
		if (len < 4)
			errx(1, "%s: missing non_repeaters/max_repititions",
			    __func__);
		memcpy(&u16, buf, sizeof(u16));
		nonrep = p16toh(header, u16);
		memcpy(&u16, buf + sizeof(u16), sizeof(u16));
		maxrep = p16toh(header, u16);
		buf += 4;
	} else {
		nonrep = 0;
		maxrep = 1;
	}
	for (i = 0; len > 0; i++) {
		sublen = poid(test, header, buf, len, &start);
		buf += sublen;
		len -= sublen;
		sublen = poid(test, header, buf, len, &end);
		buf += sublen;
		len -= sublen;

		if ((start.include != 1 && start.include != 0) ||
		    end.include != 0)
			errx(1, "%s: unexpected searchrange include: (%s-%s)",
			    test, oid_print(&start, oid1, sizeof(oid1)),
			    oid_print(&end, oid2, sizeof(oid2)));

		match = (size_t)-1;
		for (j = 0; j < nvarbind; j++) {
			if (oid_cmp(&varbind[j].name, &start) == 0 &&
			    varbind[j].type == TYPE_ENDOFMIBVIEW) {
				match = j;
			} else if (oid_cmp(&varbind[j].name, &start) < 0 ||
			    (!start.include &&
			    oid_cmp(&varbind[j].name, &start) == 0) ||
			    oid_cmp(&varbind[j].name, &end) >= 0)
				continue;
			if (match == (size_t)-1)
				match = j;
			else {
				if (oid_cmp(&varbind[j].name,
				    &varbind[match].name) < 0)
					match = j;
			}
		}
		if (match == (size_t)-1)
			errx(1, "%s: unexpected searchrange start: " "(%s-%s)",
			    test,
			    oid_print(&start, oid1, sizeof(oid1)),
			    oid_print(&end, oid2, sizeof(oid2)));
		outvarbind[nout++] = varbind[match];
		varbind[match] = varbind[--nvarbind];
		varbind[nvarbind] = (struct varbind){};
	}
	return nout;
}

void
agentx_close_validate(const char *test, const void *buf, size_t len,
    uint8_t flags, uint8_t reason)
{
	const struct header *header = buf;
	const uint8_t *u8;
	uint32_t u32;
	uint16_t u16;

	if (len != sizeof(*header) + 4)
		errx(1, "%s: unexpected pdu message size received: %zu", test, len);
	if (header->version != 1)
		errx(1, "%s: invalid pdu version", test);
	if (header->type != AGENTX_CLOSE_PDU)
		errx(1, "%s: invalid pdu type received (%hhu/2)", test, header->type);
	if (header->flags != flags)
		errx(1, "%s: invalid close pdu flags received (%hhu/%hhu)",
		    test, header->flags, flags);
	if (header->reserved != 0)
		errx(1, "%s: invalid close pdu reserved received", test);
	if (p32toh(header, header->payload_length) != 4)
		errx(1, "%s: unexpected response pdu payload length received",
		    test);

	u8 = buf + sizeof(*header);
	if (u8[0] != reason)
		errx(1, "%s: unexpected close reason (%hhu/%hhu)",
		    test, u8[0], reason);
	if (u8[1] != 0 || u8[2] != 0 || u8[3] != 0)
		errx(1, "%s: invalid close pdu reserved received", test);
}

void
agentx_response_validate(const char *test, const void *buf, size_t len,
    uint8_t flags, uint32_t packetid, enum error error, uint16_t index,
    struct varbind *varbindlist, size_t nvarbind)
{
	const struct header *header = buf;
	struct oid name, oid;
	int32_t i32;
	uint32_t u32;
	uint16_t u16, type;
	size_t i, sublen;

	if (len < sizeof(*header) + 8)
		errx(1, "%s: unexpected pdu message size received: %zu", test, len);
	if (header->version != 1)
		errx(1, "%s: invalid pdu version", test);
	if (header->type != AGENTX_RESPONSE_PDU)
		errx(1, "%s: invalid pdu type received (%hhu/18)", test, header->type);
	if (header->flags != flags)
		errx(1, "%s: invalid response pdu flags received (%hhu/%hhu)",
		    test, header->flags, flags);
	if (header->reserved != 0)
		errx(1, "%s: invalid response pdu reserved received", test);
	if (p32toh(header, header->packetid) != packetid)
		errx(1, "%s: invalid response pdu packetid received", test);
	/*
	 * Needs to be changed once we start validating responses with varbinds.
	 */
	if (p32toh(header, header->payload_length) < 8 ||
	    p32toh(header, header->payload_length) > len - sizeof(*header))
		errx(1, "%s: unexpected response pdu payload length received",
		    test);

	buf += sizeof(*header);
	len = p32toh(header, header->payload_length);
	memcpy(&u32, buf, sizeof(u32));
	if ((p32toh(header, u32) & 0xF0000000) != 0)
		errx(1, "%s: unexpected response pdu sysUptime received",
		    test);
	buf += sizeof(u32);
	memcpy(&u16, buf, sizeof(u16));
	if (p16toh(header, u16) != error)
		errx(1, "%s: unexpected response pdu error (%hu/%u)",
		    test, p16toh(header, u16), error);
	buf += sizeof(u16);
	memcpy(&u16, buf, sizeof(u16));
	if (p16toh(header, u16) != index)
		errx(1, "%s: unexpected response pdu index (%hu/%hu)",
		    test, p16toh(header, u16), index);
	buf += sizeof(u16);
	len -= 8;

	for (i = 0; len > 0; i++) {
		if (len < 4)
			errx(1,
			    "%s: invalid response pdu varbind length", test);
		memcpy(&type, buf, sizeof(type));
		type = p16toh(header, type);
		if (i < nvarbind && type != varbindlist[i].type % 1000)
			errx(1, "%s: invalid response pdu varbind type", test);
		buf += sizeof(type);
		len -= sizeof(type);
		memcpy(&u16, buf, sizeof(u16));
		if (u16 != 0)
			errx(1, "%s: invalid response pdu varbind reserved",
			    test);
		buf += sizeof(u16);
		len -= sizeof(u16);
		sublen = poid(test, header, buf, len, &name);
		if (i < nvarbind && oid_cmp(&varbindlist[i].name, &name) != 0)
			errx(1, "%s: invalid response pdu varbind name", test);
		buf += sublen;
		len -= sublen;
		switch (type % 1000) {
		case TYPE_INTEGER:
			if (len < sizeof(i32))
				errx(1,
				    "%s: invalid response pdu varbind length",
				    test);
			if (i < nvarbind && varbindlist[i].type / 1000 == 0) {
				memcpy(&i32, buf, sizeof(i32));
				i32 = p32toh(header, i32);
				if (i32 != varbindlist[i].data.int32)
					errx(1, "%s: invalid response pdu "
					    "varbind integer", test);
			}
			break;
		default:
			errx(1, "%s: Regress test not implemented: %d", test, type);
		}
	}

	if (i != nvarbind)
		errx(1, "%s: unexpected response pdu nvarbind: (%zu/%zu)",
		    test, i, nvarbind);
}

int
agentx_connect(const char *path)
{
	struct sockaddr_un sun;
	int s;

	if (path == NULL)
		path = AGENTX_SOCKET;

	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, path, sizeof(sun.sun_path));

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "%s: socket", __func__);
	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "%s: connect", __func__);

	return s;
}

uint32_t
agentx_open(int s, int nbo, uint8_t timeout, uint32_t oid[], size_t oidlen,
     const char *descr)
{
	struct message msg ={};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	struct header *header = (struct header *)repl;

	message_add_header(&msg, 1, AGENTX_OPEN_PDU,
	    nbo ? NETWORK_BYTE_ORDER : 0, 0, 0, packetid);
	message_add_uint8(&msg, timeout);
	message_add(&msg, zero, 3);
	message_add_oid(&msg, oid, oidlen, 0);
	message_add_nstring(&msg, descr, strlen(descr));

	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n,
	    nbo ? NETWORK_BYTE_ORDER : 0, packetid, 0, 0, NULL, 0);

	message_release(&msg);

	return p32toh(header, header->sessionid);
}

void
agentx_close(int s, uint32_t sessionid, enum close_reason reason)
{
	struct message msg ={};
	char zero[3] = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	struct header *header = (struct header *)repl;

	message_add_header(&msg, 1, AGENTX_CLOSE_PDU, 0, sessionid, 0,
	    packetid);
	message_add_uint8(&msg, reason);
	message_add(&msg, zero, 3);

	agentx_write(s, &msg);
	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, 0, 0, NULL, 0);

	message_release(&msg);
}

void
agentx_register(int s, uint32_t sessionid, uint8_t instance, uint8_t timeout,
    uint8_t priority, uint8_t range_subid, uint32_t oid[], size_t oidlen,
    uint32_t upperbound)
{
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t packetid = arc4random();
	uint8_t reserved = 0;

	message_add_header(&msg, 1, AGENTX_REGISTER_PDU,
	    instance ? INSTANCE_REGISTRATION : 0, sessionid, 0, packetid);
	message_add(&msg, &timeout, sizeof(timeout));
	message_add(&msg, &priority, sizeof(priority));
	message_add(&msg, &range_subid, sizeof(range_subid));
	message_add(&msg, &reserved, sizeof(reserved));
	message_add_oid(&msg, oid, oidlen, 0);
	if (range_subid != 0)
		message_add_uint32(&msg, upperbound);

	agentx_write(s, &msg);

	n = agentx_read(s, repl, sizeof(repl), 1000);

	agentx_response_validate(__func__, repl, n, 0, packetid, NOAGENTXERROR, 0,
	    NULL, 0);
	message_release(&msg);
}

void
agentx_response(int s, void *buf, enum error error, uint16_t index,
    struct varbind *varbindlist, size_t nvarbind)
{
	struct header *header = buf;
	struct message msg = {};
	char repl[1024];
	size_t n;
	uint32_t sysuptime = 0;
	uint16_t reserved = 0;
	size_t i;

	message_add_header(&msg, 1, AGENTX_RESPONSE_PDU, 0,
	    p32toh(header, header->sessionid),
	    p32toh(header, header->transactionid),
	    p32toh(header, header->packetid));
	message_add_uint32(&msg, sysuptime);
	message_add_uint16(&msg, error);
	message_add_uint16(&msg, index);

	for (i = 0; i < nvarbind; i++)
		message_add_varbind(&msg, varbindlist + i);

	agentx_write(s, &msg);
	message_release(&msg);
}

static void
message_add_uint8(struct message *msg, uint8_t n)
{
	message_add(msg, &n, 1);
}

static void
message_add_uint16(struct message *msg, uint16_t n)
{
	if (MESSAGE_NBO(msg))
		n = htobe16(n);
	else
		n = htole16(n);

	message_add(msg, &n, sizeof(n));
}

static void
message_add_uint32(struct message *msg, uint32_t n)
{
	if (MESSAGE_NBO(msg))
		n = htobe32(n);
	else
		n = htole32(n);

	message_add(msg, &n, sizeof(n));
}

static void
message_add_uint64(struct message *msg, uint64_t n)
{
	if (MESSAGE_NBO(msg))
		n = htobe64(n);
	else
		n = htole64(n);

	message_add(msg, &n, sizeof(n));
}

static void
message_add_nstring(struct message *msg, const void *src, uint32_t len)
{
	char zero[3] = {};

	message_add_uint32(msg, len);
	if (len == 0)
		return;
	message_add(msg, src, len);
	message_add(msg, zero, (4 - (len % 4)) % 4);
}

static void
message_add_oid(struct message *msg, const uint32_t oid[], uint8_t n_subid,
    uint8_t include)
{
	uint8_t prefix = 0;
	uint8_t i;

	if (n_subid > 5 && oid[0] == 1 && oid[1] == 3 && oid[2] == 6 &&
	    oid[3] == 1 && oid[4] < UINT8_MAX) {
		prefix = oid[4];
		oid += 5;
		n_subid -= 5;
	}

	message_add_uint8(msg, n_subid);
	message_add_uint8(msg, prefix);
	message_add_uint8(msg, include);
	message_add_uint8(msg, 0);
	for (i = 0; i < n_subid; i++)
		message_add_uint32(msg, oid[i]);
}

static void
message_add_varbind(struct message *msg, struct varbind *varbind)
{
	uint64_t u64;
	uint32_t u32;
	size_t len;
	va_list ap;

	message_add_uint16(msg, varbind->type);
	message_add_uint16(msg, 0);
	message_add_oid(msg, varbind->name.subid, varbind->name.n_subid, 0);
	switch (varbind->type) {
	case INTEGER:
		message_add_uint32(msg, varbind->data.int32);
		break;
	case COUNTER32:
	case GAUGE32:
	case TIMETICKS:
		message_add_uint32(msg, varbind->data.uint32);
		break;
	case OCTETSTRING:
	case IPADDRESS:
	case OPAQUE:
		message_add_nstring(msg, varbind->data.octetstring.string,
		    varbind->data.octetstring.len);
		break;
	case OBJECTIDENTIFIER:
		message_add_oid(msg, varbind->data.oid.subid,
		    varbind->data.oid.n_subid, 0);
		break;
	case COUNTER64:
		message_add_uint64(msg, varbind->data.uint64);
		break;
	case AXNULL:
	case NOSUCHOBJECT:
	case NOSUCHINSTANCE:
	case ENDOFMIBVIEW:
		break;
	default:
		errx(1, "%s: unsupported data type: %d", __func__,
		    varbind->type);
	}
}

static void
message_add_header(struct message *msg, uint8_t version, uint8_t type,
    uint8_t flags, uint32_t sessionid, uint32_t transactionid,
    uint32_t packetid)
{
	if (msg->len != 0)
		errx(1, "%s: message not new", __func__);
	message_add_uint8(msg, version);
	message_add_uint8(msg, type);
	message_add_uint8(msg, flags);
	message_add_uint8(msg, 0);
	message_add_uint32(msg, sessionid);
	message_add_uint32(msg, transactionid);
	message_add_uint32(msg, packetid);
	message_add_uint32(msg, 0); /* payload_length tbt */
}

static void
message_add(struct message *msg, const void *src, size_t len)
{
	if (len == 0)
		return;
	if (msg->len + len > msg->size) {
		if ((msg->buf = recallocarray(msg->buf, msg->size,
		    (((msg->len + len) % 4096) + 1) * 4096, 1)) == NULL)
			err(1, NULL);
		msg->size = (((msg->len + len) % 4096) + 1) * 4096;
	}
	memcpy(msg->buf + msg->len, src, len);
	msg->len += len;
}

static void
message_release(struct message *msg)
{
	free(msg->buf);
}

static size_t
poid(const char *test, const struct header *header, const uint8_t *buf, size_t len,
    struct oid *oid)
{
	uint8_t n_subid, i;
	uint32_t subid;

	if (len < 4)
		errx(1, "%s: incomplete oid", test);
	n_subid = buf[0];
	if (buf[1] != 0) {
		*oid = OID_STRUCT(1, 3, 6, 1, buf[1]);
	} else
		*oid = OID_STRUCT();
	oid->include = buf[2];
	if (buf[3] != 0)
		errx(1, "%s: invalid oid reserved (%hhx)", test, buf[3]);
	buf += 4;
	len -= 4;
	if (oid->n_subid + n_subid > 128)
		errx(1, "%s: too many n_subid in oid", test);
	if (len < n_subid * sizeof(oid->subid[0]))
		errx(1, "%s: incomplete oid: (%zu/%zu)", test,
		    n_subid * sizeof(oid->subid[0]), len);
	for (i = 0; i < n_subid; i++) {
		memcpy(&subid, buf, sizeof(subid));
		buf += 4;
		oid->subid[oid->n_subid++] = p32toh(header, subid);
	}

	return 4 * (n_subid + 1);
}

size_t
agentx_read(int s, void *buf, size_t len, int timeout)
{
	ssize_t n;
	size_t i;
	int ret;
	struct pollfd pfd = {
		.fd = s,
		.events = POLLIN
	};

	if ((ret = poll(&pfd, 1, timeout)) == -1)
		err(1, "poll");
	if (ret == 0)
		errx(1, "%s: timeout", __func__);
	if ((n = read(s, buf, len)) == -1)
		err(1, "agentx read");

	if (verbose && n != 0) {
		printf("AgentX received(%d):\n", s);
		for (i = 0; i < n; i++) {
			printf("%s%02hhx", i % 4 == 0 ? "" : " ",
			    ((char *)buf)[i]);
			if (i % 4 == 3)
				printf("\n");
		}
		if (i % 4 != 0)
			printf("\n");
	}
	return n;
}

void
agentx_timeout(int s, int timeout)
{
	int ret;
	struct pollfd pfd = {
		.fd = s,
		.events = POLLIN
	};
	
	if ((ret = poll(&pfd, 1, timeout)) == -1)
		err(1, "poll");
	if (ret != 0)
		errx(1, "%s: unexpected agentx data", __func__);
}

void
agentx_write(int s, struct message *msg)
{
	ssize_t n;
	char *buf = msg->buf;
	size_t len = msg->len;
	struct header *header = msg->buf;
	size_t i;

	msg->len = 16;
	message_add_uint32(msg, len - sizeof(*header));

	if (verbose) {
		printf("AgentX sending(%d):\n", s);
		for (i = 0; i < len; i++) {
			printf("%s%02hhx", i % 4 == 0 ? "" : " ", buf[i]);
			if (i % 4 == 3)
				printf("\n");
		}
		if (i % 4 != 0)
			printf("\n");
	}

	while (len > 0) {
		if ((n = write(s, buf, len)) == -1)
			err(1, "agentx write");
		buf += n;
		len -= n;
	}
	msg->len = 0;
}
