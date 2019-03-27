/*
 * Copyright (c) 2000 Alfred Perlstein <alfred@freebsd.org>
 * All rights reserved.
 * Copyright (c) 2000 Paul Saab <ps@freebsd.org>
 * All rights reserved.
 * Copyright (c) 2000 John Baldwin <jhb@freebsd.org>
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
 * $FreeBSD$
 */

/*
 * The typedefs and structures declared in this file
 * clearly violate style(9), the reason for this is to conform to the
 * typedefs/structure-names used in the Intel literature to avoid confusion.
 *
 * It's for your own good. :)
 */

/* It seems that intel didn't think about ABI,
 * either that or 16bit ABI != 32bit ABI (which seems reasonable)
 * I have to thank Intel for the hair loss I incurred trying to figure
 * out why PXE was mis-reading structures I was passing it (at least
 * from my point of view)
 *
 * Solution: use gcc's '__packed' to correctly align
 * structures passed into PXE
 * Question: does this really work for PXE's expected ABI?
 */
#define	PACKED		__packed

#define	S_SIZE(s)	s, sizeof(s) - 1

#define	PXENFSROOTPATH	"/pxeroot"

typedef struct {
	uint16_t		offset;
	uint16_t		segment;
} SEGOFF16_t;

typedef struct {
	uint16_t		Seg_Addr;
	uint32_t		Phy_Addr;
	uint16_t		Seg_Size;
} SEGDESC_t;

typedef	uint16_t		SEGSEL_t;
typedef	uint16_t		PXENV_STATUS_t;
typedef	uint32_t		IP4_t;
typedef	uint32_t		ADDR32_t;
typedef	uint16_t		UDP_PORT_t;

#define	MAC_ADDR_LEN		16
typedef	uint8_t			MAC_ADDR[MAC_ADDR_LEN];

/* PXENV+ */
typedef struct {
	uint8_t		Signature[6];	/* 'PXENV+' */
	uint16_t	Version;	/* MSB = major, LSB = minor */
	uint8_t		Length;		/* structure length */
	uint8_t		Checksum;	/* checksum pad */
	SEGOFF16_t	RMEntry;	/* SEG:OFF to PXE entry point */
	/* don't use PMOffset and PMSelector (from the 2.1 PXE manual) */
	uint32_t	PMOffset;	/* Protected mode entry */
	SEGSEL_t	PMSelector;	/* Protected mode selector */
	SEGSEL_t	StackSeg;	/* Stack segment address */
	uint16_t	StackSize;	/* Stack segment size (bytes) */
	SEGSEL_t	BC_CodeSeg;	/* BC Code segment address */
	uint16_t	BC_CodeSize;	/* BC Code segment size (bytes) */
	SEGSEL_t	BC_DataSeg;	/* BC Data segment address */
	uint16_t	BC_DataSize;	/* BC Data segment size (bytes) */
	SEGSEL_t	UNDIDataSeg;	/* UNDI Data segment address */
	uint16_t	UNDIDataSize;	/* UNDI Data segment size (bytes) */
	SEGSEL_t	UNDICodeSeg;	/* UNDI Code segment address */
	uint16_t	UNDICodeSize;	/* UNDI Code segment size (bytes) */
	SEGOFF16_t	PXEPtr;		/* SEG:OFF to !PXE struct, 
					   only present when Version > 2.1 */
} PACKED pxenv_t;

/* !PXE */
typedef struct {
	uint8_t		Signature[4];
	uint8_t		StructLength;
	uint8_t		StructCksum;
	uint8_t		StructRev;
	uint8_t		reserved_1;
	SEGOFF16_t	UNDIROMID;
	SEGOFF16_t	BaseROMID;
	SEGOFF16_t	EntryPointSP;
	SEGOFF16_t	EntryPointESP;
	SEGOFF16_t	StatusCallout;
	uint8_t		reserved_2;
	uint8_t		SegDescCn;
	SEGSEL_t	FirstSelector;
	SEGDESC_t	Stack;
	SEGDESC_t	UNDIData;
	SEGDESC_t	UNDICode;
	SEGDESC_t	UNDICodeWrite;
	SEGDESC_t	BC_Data;
	SEGDESC_t	BC_Code;
	SEGDESC_t	BC_CodeWrite;
} PACKED pxe_t;

#define	PXENV_START_UNDI		0x0000
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	ax;
	uint16_t	bx;
	uint16_t	dx;
	uint16_t	di;
	uint16_t	es;
} PACKED t_PXENV_START_UNDI;

#define	PXENV_UNDI_STARTUP		0x0001
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_STARTUP;

#define	PXENV_UNDI_CLEANUP		0x0002
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_CLEANUP;

#define	PXENV_UNDI_INITIALIZE		0x0003
typedef struct {
	PXENV_STATUS_t	Status;
	ADDR32_t	ProtocolIni;	/* Phys addr of a copy of the driver module */
	uint8_t		reserved[8];
} PACKED t_PXENV_UNDI_INITIALIZE;


#define	MAXNUM_MCADDR		8
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	MCastAddrCount;
	MAC_ADDR	McastAddr[MAXNUM_MCADDR];
} PACKED t_PXENV_UNDI_MCAST_ADDRESS;

#define	PXENV_UNDI_RESET_ADAPTER	0x0004		
typedef struct {
	PXENV_STATUS_t	Status;
	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} PACKED t_PXENV_UNDI_RESET;

#define	PXENV_UNDI_SHUTDOWN		0x0005
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_SHUTDOWN;

#define	PXENV_UNDI_OPEN			0x0006
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	OpenFlag;
	uint16_t	PktFilter;
#	define FLTR_DIRECTED	0x0001
#	define FLTR_BRDCST	0x0002
#	define FLTR_PRMSCS	0x0004
#	define FLTR_SRC_RTG	0x0008

	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} PACKED t_PXENV_UNDI_OPEN;

#define	PXENV_UNDI_CLOSE		0x0007
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_CLOSE;

#define	PXENV_UNDI_TRANSMIT		0x0008
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		Protocol;
#	define P_UNKNOWN	0
#	define P_IP		1
#	define P_ARP		2
#	define P_RARP		3

	uint8_t		XmitFlag;
#	define XMT_DESTADDR	0x0000
#	define XMT_BROADCAST	0x0001

	SEGOFF16_t	DestAddr;
	SEGOFF16_t	TBD;
	uint32_t	Reserved[2];
} PACKED t_PXENV_UNDI_TRANSMIT;

#define	MAX_DATA_BLKS		8
typedef struct {
	uint16_t	ImmedLength;
	SEGOFF16_t	Xmit;
	uint16_t	DataBlkCount;
	struct	DataBlk {
		uint8_t		TDPtrType;
		uint8_t		TDRsvdByte;
		uint16_t	TDDataLen;
		SEGOFF16_t	TDDataPtr;
	} DataBlock[MAX_DATA_BLKS];
} PACKED t_PXENV_UNDI_TBD;

#define	PXENV_UNDI_SET_MCAST_ADDRESS	0x0009
typedef struct {
	PXENV_STATUS_t	Status;
	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} PACKED t_PXENV_UNDI_SET_MCAST_ADDR;

#define	PXENV_UNDI_SET_STATION_ADDRESS	0x000A
typedef struct {
	PXENV_STATUS_t	Status;
	MAC_ADDR	StationAddress;		/* Temp MAC address to use */
} PACKED t_PXENV_UNDI_SET_STATION_ADDR;

#define	PXENV_UNDI_SET_PACKET_FILTER	0x000B
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		filter;			/* see UNDI_OPEN (0x0006) */
} PACKED t_PXENV_UNDI_SET_PACKET_FILTER;

#define	PXENV_UNDI_GET_INFORMATION	0x000C
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	BaseIo;			/* Adapter base I/O address */
	uint16_t	IntNumber;		/* Adapter IRQ number */
	uint16_t	MaxTranUnit;		/* Adapter maximum transmit unit */
	uint16_t	HwType;			/* Type of protocol at the hardware addr */
#	define ETHER_TYPE	1
#	define EXP_ETHER_TYPE	2
#	define IEEE_TYPE	6

	uint16_t	HwAddrLen;		/* Length of hardware address */
	MAC_ADDR	CurrentNodeAddress;	/* Current hardware address */
	MAC_ADDR	PermNodeAddress;	/* Permanent hardware address */
	SEGSEL_t	ROMAddress;		/* Real mode ROM segment address */
	uint16_t	RxBufCt;		/* Receive queue length */
	uint16_t	TxBufCt;		/* Transmit queue length */
} PACKED t_PXENV_UNDI_GET_INFORMATION;

#define	PXENV_UNDI_GET_STATISTICS	0x000D
typedef struct {
	PXENV_STATUS_t	Status;
	uint32_t	XmitGoodFrames;		/* Number of successful transmissions */
	uint32_t	RcvGoodFrames;		/* Number of good frames received */
	uint32_t	RcvCRCErrors;		/* Number of frames with CRC errors */
	uint32_t	RcvResourceErrors;	/* Number of frames dropped */
} PACKED t_PXENV_UNDI_GET_STATISTICS;

#define	PXENV_UNDI_CLEAR_STATISTICS	0x000E
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_CLEAR_STATISTICS;

#define	PXENV_UNDI_INITIATE_DIAGS	0x000F
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_INITIATE_DIAGS;

#define	PXENV_UNDI_FORCE_INTERRUPT	0x0010
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_UNDI_FORCE_INTERRUPT;

#define	PXENV_UNDI_GET_MCAST_ADDRESS	0x0011
typedef struct {
	PXENV_STATUS_t	Status;
	IP4_t		InetAddr;		/* IP mulicast address */
	MAC_ADDR	MediaAddr;		/* MAC multicast address */
} PACKED t_PXENV_UNDI_GET_MCAST_ADDR;

#define	PXENV_UNDI_GET_NIC_TYPE		0x0012
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		NicType;		/* Type of NIC */
#	define PCI_NIC		2
#	define PnP_NIC		3
#	define CardBus_NIC	4

	union {
		struct {
			uint16_t	Vendor_ID;
			uint16_t	Dev_ID;
			uint8_t		Base_Class;
			uint8_t		Sub_Class;
			uint8_t		Prog_Intf;
			uint8_t		Rev;
			uint16_t	BusDevFunc;
			uint16_t	SubVendor_ID;
			uint16_t	SubDevice_ID;
		} pci, cardbus;
		struct {
			uint32_t	EISA_Dev_ID;
			uint8_t		Base_Class;
			uint8_t		Sub_Class;
			uint8_t		Prog_Intf;
			uint16_t	CardSelNum;
		} pnp;
	} info;
} PACKED t_PXENV_UNDI_GET_NIC_TYPE;

#define	PXENV_UNDI_GET_IFACE_INFO	0x0013
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		IfaceType[16];		/* Name of MAC type in ASCII. */
	uint32_t	LinkSpeed;		/* Defined in NDIS 2.0 spec */
	uint32_t	ServiceFlags;		/* Defined in NDIS 2.0 spec */
	uint32_t	Reserved[4];		/* must be 0 */
} PACKED t_PXENV_UNDI_GET_NDIS_INFO;

#define	PXENV_UNDI_ISR			0x0014
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	FuncFlag;		/* PXENV_UNDI_ISR_OUT_xxx */
	uint16_t	BufferLength;		/* Length of Frame */
	uint16_t	FrameLength;		/* Total length of receiver frame */
	uint16_t	FrameHeaderLength;	/* Length of the media header in Frame */
	SEGOFF16_t	Frame;			/* receive buffer */
	uint8_t		ProtType;		/* Protocol type */
	uint8_t		PktType;		/* Packet Type */
#	define PXENV_UNDI_ISR_IN_START		1
#	define PXENV_UNDI_ISR_IN_PROCESS	2
#	define PXENV_UNDI_ISR_IN_GET_NEXT	3

	/* one of these will be returned for PXENV_UNDI_ISR_IN_START */
#	define PXENV_UNDI_ISR_OUT_OURS		0
#	define PXENV_UNDI_ISR_OUT_NOT_OUTS	1

	/*
	 * one of these will bre returned for PXEND_UNDI_ISR_IN_PROCESS
	 * and PXENV_UNDI_ISR_IN_GET_NEXT
	 */
#	define PXENV_UNDI_ISR_OUT_DONE		0
#	define PXENV_UNDI_ISR_OUT_TRANSMIT	2
#	define PXENV_UNDI_ISR_OUT_RECEIVE	3
#	define PXENV_UNDI_ISR_OUT_BUSY		4
} PACKED t_PXENV_UNDI_ISR;

#define	PXENV_STOP_UNDI			0x0015
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_STOP_UNDI;

#define	PXENV_TFTP_OPEN			0x0020
typedef struct {
	PXENV_STATUS_t	Status;
	IP4_t		ServerIPAddress;
	IP4_t		GatewayIPAddress;
	uint8_t		FileName[128];
	UDP_PORT_t	TFTPPort;
	uint16_t	PacketSize;
} PACKED t_PXENV_TFTP_OPEN;

#define	PXENV_TFTP_CLOSE		0x0021
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_TFTP_CLOSE;

#define	PXENV_TFTP_READ			0x0022
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	PacketNumber;
	uint16_t	BufferSize;
	SEGOFF16_t	Buffer;
} PACKED t_PXENV_TFTP_READ;

#define	PXENV_TFTP_READ_FILE		0x0023
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		FileName[128];
	uint32_t	BufferSize;
	ADDR32_t	Buffer;
	IP4_t		ServerIPAddress;
	IP4_t		GatewayIPAdress;
	IP4_t		McastIPAdress;
	UDP_PORT_t	TFTPClntPort;
	UDP_PORT_t	TFTPSrvPort;
	uint16_t	TFTPOpenTimeOut;
	uint16_t	TFTPReopenDelay;
} PACKED t_PXENV_TFTP_READ_FILE;

#define	PXENV_TFTP_GET_FSIZE		0x0025
typedef struct {
	PXENV_STATUS_t	Status;
	IP4_t		ServerIPAddress;
	IP4_t		GatewayIPAdress;
	uint8_t		FileName[128];
	uint32_t	FileSize;
} PACKED t_PXENV_TFTP_GET_FSIZE;

#define	PXENV_UDP_OPEN			0x0030
typedef struct {
	PXENV_STATUS_t	status;
	IP4_t		src_ip;		/* IP address of this station */
} PACKED t_PXENV_UDP_OPEN;

#define	PXENV_UDP_CLOSE			0x0031
typedef struct {
	PXENV_STATUS_t	status;
} PACKED t_PXENV_UDP_CLOSE;

#define	PXENV_UDP_READ			0x0032
typedef struct {
	PXENV_STATUS_t	status;
	IP4_t		src_ip;		/* IP of sender */
	IP4_t		dest_ip;	/* Only accept packets sent to this IP */
	UDP_PORT_t	s_port;		/* UDP source port of sender */
	UDP_PORT_t	d_port;		/* Only accept packets sent to this port */
	uint16_t	buffer_size;	/* Size of the packet buffer */
	SEGOFF16_t	buffer;		/* SEG:OFF to the packet buffer */
} PACKED t_PXENV_UDP_READ;

#define	PXENV_UDP_WRITE			0x0033
typedef struct {
	PXENV_STATUS_t	status;
	IP4_t		ip;		/* dest ip addr */
	IP4_t		gw;		/* ip gateway */
	UDP_PORT_t	src_port;	/* source udp port */
	UDP_PORT_t	dst_port;	/* destination udp port */
	uint16_t	buffer_size;	/* Size of the packet buffer */
	SEGOFF16_t	buffer;		/* SEG:OFF to the packet buffer */
} PACKED t_PXENV_UDP_WRITE;

#define	PXENV_UNLOAD_STACK		0x0070
typedef struct {
	PXENV_STATUS_t	Status;
	uint8_t		reserved[10];
} PACKED t_PXENV_UNLOAD_STACK;


#define	PXENV_GET_CACHED_INFO		0x0071
typedef struct {
	PXENV_STATUS_t	Status;
	uint16_t	PacketType;	/* type (defined right here) */
#	define PXENV_PACKET_TYPE_DHCP_DISCOVER  1
#	define PXENV_PACKET_TYPE_DHCP_ACK       2
#	define PXENV_PACKET_TYPE_BINL_REPLY     3
	uint16_t	BufferSize;	/* max to copy, leave at 0 for pointer */
	SEGOFF16_t	Buffer;		/* copy to, leave at 0 for pointer */
	uint16_t	BufferLimit;	/* max size of buffer in BC dataseg ? */
} PACKED t_PXENV_GET_CACHED_INFO;


/* structure filled in by PXENV_GET_CACHED_INFO 
 * (how we determine which IP we downloaded the initial bootstrap from)
 * words can't describe...
 */
typedef struct {
	uint8_t		opcode;
#	define BOOTP_REQ	1
#	define BOOTP_REP	2
	uint8_t		Hardware;	/* hardware type */
	uint8_t		Hardlen;	/* hardware addr len */
	uint8_t		Gatehops;	/* zero it */
	uint32_t	ident;		/* random number chosen by client */
	uint16_t	seconds;	/* seconds since did initial bootstrap */
	uint16_t	Flags;		/* seconds since did initial bootstrap */
#	define BOOTP_BCAST	0x8000		/* ? */
	IP4_t		cip;		/* Client IP */
	IP4_t		yip;		/* Your IP */
	IP4_t		sip;		/* IP to use for next boot stage */
	IP4_t		gip;		/* Relay IP ? */
	MAC_ADDR	CAddr;		/* Client hardware address */
	uint8_t		Sname[64];	/* Server's hostname (Optional) */
	uint8_t		bootfile[128];	/* boot filename */
	union {
#		if 1
#		define BOOTP_DHCPVEND  1024    /* DHCP extended vendor field size */
#		else
#		define BOOTP_DHCPVEND  312	/* DHCP standard vendor field size */
#		endif
		uint8_t		d[BOOTP_DHCPVEND];	/* raw array of vendor/dhcp options */
		struct {
			uint8_t		magic[4];	/* DHCP magic cookie */
#			ifndef		VM_RFC1048
#			define		VM_RFC1048	0x63825363L	/* ? */
#			endif
			uint32_t	flags;		/* bootp flags/opcodes */
			uint8_t		pad[56];	/* I don't think intel knows what a
							   union does... */
		} v;
	} vendor;
} PACKED BOOTPLAYER;

#define	PXENV_RESTART_TFTP		0x0073
#define	t_PXENV_RESTART_TFTP		t_PXENV_TFTP_READ_FILE

#define	PXENV_START_BASE		0x0075
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_START_BASE;

#define	PXENV_STOP_BASE			0x0076
typedef struct {
	PXENV_STATUS_t	Status;
} PACKED t_PXENV_STOP_BASE;
