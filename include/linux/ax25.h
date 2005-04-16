/*
 * These are the public elements of the Linux kernel AX.25 code. A similar
 * file netrom.h exists for the NET/ROM protocol.
 */

#ifndef	AX25_KERNEL_H
#define	AX25_KERNEL_H

#include <linux/socket.h>

#define AX25_MTU	256
#define AX25_MAX_DIGIS  8

#define AX25_WINDOW	1
#define AX25_T1		2
#define AX25_N2		3
#define AX25_T3		4
#define AX25_T2		5
#define	AX25_BACKOFF	6
#define	AX25_EXTSEQ	7
#define	AX25_PIDINCL	8
#define AX25_IDLE	9
#define AX25_PACLEN	10
#define AX25_IAMDIGI	12

#define AX25_KILL	99

#define SIOCAX25GETUID		(SIOCPROTOPRIVATE+0)
#define SIOCAX25ADDUID		(SIOCPROTOPRIVATE+1)
#define SIOCAX25DELUID		(SIOCPROTOPRIVATE+2)
#define SIOCAX25NOUID		(SIOCPROTOPRIVATE+3)
#define SIOCAX25OPTRT		(SIOCPROTOPRIVATE+7)
#define SIOCAX25CTLCON		(SIOCPROTOPRIVATE+8)
#define SIOCAX25GETINFOOLD	(SIOCPROTOPRIVATE+9)
#define SIOCAX25ADDFWD		(SIOCPROTOPRIVATE+10)
#define SIOCAX25DELFWD		(SIOCPROTOPRIVATE+11)
#define SIOCAX25DEVCTL          (SIOCPROTOPRIVATE+12)
#define SIOCAX25GETINFO         (SIOCPROTOPRIVATE+13)

#define AX25_SET_RT_IPMODE	2

#define AX25_NOUID_DEFAULT	0
#define AX25_NOUID_BLOCK	1

typedef struct {
	char		ax25_call[7];	/* 6 call + SSID (shifted ascii!) */
} ax25_address;

struct sockaddr_ax25 {
	sa_family_t	sax25_family;
	ax25_address	sax25_call;
	int		sax25_ndigis;
	/* Digipeater ax25_address sets follow */
};

#define sax25_uid	sax25_ndigis

struct full_sockaddr_ax25 {
	struct sockaddr_ax25 fsa_ax25;
	ax25_address	fsa_digipeater[AX25_MAX_DIGIS];
};

struct ax25_routes_struct {
	ax25_address	port_addr;
	ax25_address	dest_addr;
	unsigned char	digi_count;
	ax25_address	digi_addr[AX25_MAX_DIGIS];
};

struct ax25_route_opt_struct {
	ax25_address	port_addr;
	ax25_address	dest_addr;
	int		cmd;
	int		arg;
};

struct ax25_ctl_struct {
        ax25_address            port_addr;
        ax25_address            source_addr;
        ax25_address            dest_addr;
        unsigned int            cmd;
        unsigned long           arg;
        unsigned char           digi_count;
        ax25_address            digi_addr[AX25_MAX_DIGIS];
};

/* this will go away. Please do not export to user land */
struct ax25_info_struct_deprecated {
	unsigned int	n2, n2count;
	unsigned int	t1, t1timer;
	unsigned int	t2, t2timer;
	unsigned int	t3, t3timer;
	unsigned int	idle, idletimer;
	unsigned int	state;
	unsigned int	rcv_q, snd_q;
};

struct ax25_info_struct {
	unsigned int	n2, n2count;
	unsigned int	t1, t1timer;
	unsigned int	t2, t2timer;
	unsigned int	t3, t3timer;
	unsigned int	idle, idletimer;
	unsigned int	state;
	unsigned int	rcv_q, snd_q;
	unsigned int	vs, vr, va, vs_max;
	unsigned int	paclen;
	unsigned int	window;
};

struct ax25_fwd_struct {
	ax25_address	port_from;
	ax25_address	port_to;
};

#endif
