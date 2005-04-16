#ifndef _TCP_DIAG_H_
#define _TCP_DIAG_H_ 1

/* Just some random number */
#define TCPDIAG_GETSOCK 18

/* Socket identity */
struct tcpdiag_sockid
{
	__u16	tcpdiag_sport;
	__u16	tcpdiag_dport;
	__u32	tcpdiag_src[4];
	__u32	tcpdiag_dst[4];
	__u32	tcpdiag_if;
	__u32	tcpdiag_cookie[2];
#define TCPDIAG_NOCOOKIE (~0U)
};

/* Request structure */

struct tcpdiagreq
{
	__u8	tcpdiag_family;		/* Family of addresses. */
	__u8	tcpdiag_src_len;
	__u8	tcpdiag_dst_len;
	__u8	tcpdiag_ext;		/* Query extended information */

	struct tcpdiag_sockid id;

	__u32	tcpdiag_states;		/* States to dump */
	__u32	tcpdiag_dbs;		/* Tables to dump (NI) */
};

enum
{
	TCPDIAG_REQ_NONE,
	TCPDIAG_REQ_BYTECODE,
};

#define TCPDIAG_REQ_MAX TCPDIAG_REQ_BYTECODE

/* Bytecode is sequence of 4 byte commands followed by variable arguments.
 * All the commands identified by "code" are conditional jumps forward:
 * to offset cc+"yes" or to offset cc+"no". "yes" is supposed to be
 * length of the command and its arguments.
 */
 
struct tcpdiag_bc_op
{
	unsigned char	code;
	unsigned char	yes;
	unsigned short	no;
};

enum
{
	TCPDIAG_BC_NOP,
	TCPDIAG_BC_JMP,
	TCPDIAG_BC_S_GE,
	TCPDIAG_BC_S_LE,
	TCPDIAG_BC_D_GE,
	TCPDIAG_BC_D_LE,
	TCPDIAG_BC_AUTO,
	TCPDIAG_BC_S_COND,
	TCPDIAG_BC_D_COND,
};

struct tcpdiag_hostcond
{
	__u8	family;
	__u8	prefix_len;
	int	port;
	__u32	addr[0];
};

/* Base info structure. It contains socket identity (addrs/ports/cookie)
 * and, alas, the information shown by netstat. */
struct tcpdiagmsg
{
	__u8	tcpdiag_family;
	__u8	tcpdiag_state;
	__u8	tcpdiag_timer;
	__u8	tcpdiag_retrans;

	struct tcpdiag_sockid id;

	__u32	tcpdiag_expires;
	__u32	tcpdiag_rqueue;
	__u32	tcpdiag_wqueue;
	__u32	tcpdiag_uid;
	__u32	tcpdiag_inode;
};

/* Extensions */

enum
{
	TCPDIAG_NONE,
	TCPDIAG_MEMINFO,
	TCPDIAG_INFO,
	TCPDIAG_VEGASINFO,
};

#define TCPDIAG_MAX TCPDIAG_VEGASINFO


/* TCPDIAG_MEM */

struct tcpdiag_meminfo
{
	__u32	tcpdiag_rmem;
	__u32	tcpdiag_wmem;
	__u32	tcpdiag_fmem;
	__u32	tcpdiag_tmem;
};

/* TCPDIAG_VEGASINFO */

struct tcpvegas_info {
	__u32	tcpv_enabled;
	__u32	tcpv_rttcnt;
	__u32	tcpv_rtt;
	__u32	tcpv_minrtt;
};


#endif /* _TCP_DIAG_H_ */
