/* Definitions for Econet sockets. */

#ifndef __LINUX_IF_EC
#define __LINUX_IF_EC

/* User visible stuff. Glibc provides its own but libc5 folk will use these */

struct ec_addr
{
  unsigned char station;		/* Station number.  */
  unsigned char net;			/* Network number.  */
};

struct sockaddr_ec
{
  unsigned short sec_family;
  unsigned char port;			/* Port number.  */
  unsigned char cb;			/* Control/flag byte.  */
  unsigned char type;			/* Type of message.  */
  struct ec_addr addr;
  unsigned long cookie;
};

#define ECTYPE_PACKET_RECEIVED		0	/* Packet received */
#define ECTYPE_TRANSMIT_STATUS		0x10	/* Transmit completed, 
						   low nibble holds status */

#define ECTYPE_TRANSMIT_OK		1
#define ECTYPE_TRANSMIT_NOT_LISTENING	2
#define ECTYPE_TRANSMIT_NET_ERROR	3
#define ECTYPE_TRANSMIT_NO_CLOCK	4
#define ECTYPE_TRANSMIT_LINE_JAMMED	5
#define ECTYPE_TRANSMIT_NOT_PRESENT	6

#ifdef __KERNEL__

#define EC_HLEN				6

/* This is what an Econet frame looks like on the wire. */
struct ec_framehdr 
{
  unsigned char dst_stn;
  unsigned char dst_net;
  unsigned char src_stn;
  unsigned char src_net;
  unsigned char cb;
  unsigned char port;
};

struct econet_sock {
  /* struct sock has to be the first member of econet_sock */
  struct sock	sk;
  unsigned char cb;
  unsigned char port;
  unsigned char station;
  unsigned char net;
  unsigned short num;
};

static inline struct econet_sock *ec_sk(const struct sock *sk)
{
	return (struct econet_sock *)sk;
}

struct ec_device
{
  unsigned char station, net;		/* Econet protocol address */
};

#endif

#endif
