/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008
 * 	Swinburne University of Technology, Melbourne, Australia.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 */

/*
 * Alias_sctp forms part of the libalias kernel module to handle 
 * Network Address Translation (NAT) for the SCTP protocol.
 *
 *  This software was developed by David A. Hayes
 *  with leadership and advice from Jason But
 *
 * The design is outlined in CAIA technical report number  080618A
 * (D. Hayes and J. But, "Alias_sctp Version 0.1: SCTP NAT implementation in IPFW")
 *
 * Development is part of the CAIA SONATA project,
 * proposed by Jason But and Grenville Armitage:
 * http://caia.swin.edu.au/urp/sonata/
 *
 * 
 * This project has been made possible in part by a grant from
 * the Cisco University Research Program Fund at Community
 * Foundation Silicon Valley.
 *
 */

/* $FreeBSD$ */

#ifndef _ALIAS_SCTP_H_
#define _ALIAS_SCTP_H_

#include <sys/param.h>
#ifdef	_KERNEL 
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#endif // #ifdef	_KERNEL 
#include <sys/types.h>

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/time.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

/**
 * These are defined in sctp_os_bsd.h, but it can't be included due to its local file
 * inclusion, so I'm defining them here.
 * 
 */
#include <machine/cpufunc.h>
/* The packed define for 64 bit platforms */
#ifndef SCTP_PACKED
#define SCTP_PACKED __attribute__((packed))
#endif //#ifndef SCTP_PACKED
#ifndef SCTP_UNUSED
#define SCTP_UNUSED __attribute__((unused))
#endif //#ifndef SCTP_UNUSED


#include <netinet/sctp.h>
//#include <netinet/sctp_os_bsd.h> --might be needed later for mbuf stuff
#include <netinet/sctp_header.h>

#ifndef _KERNEL
#include <stdlib.h>
#include <stdio.h>
#endif //#ifdef _KERNEL


#define LINK_SCTP                      IPPROTO_SCTP


#define SN_TO_LOCAL              0   /**< packet traveling from global to local */
#define SN_TO_GLOBAL             1   /**< packet traveling from local to global */
#define SN_TO_NODIR             99   /**< used where direction is not important */

#define SN_NAT_PKT          0x0000   /**< Network Address Translate packet */
#define SN_DROP_PKT         0x0001   /**< drop packet (don't forward it) */
#define SN_PROCESSING_ERROR 0x0003   /**< Packet processing error */
#define SN_REPLY_ABORT      0x0010   /**< Reply with ABORT to sender (don't forward it) */
#define SN_SEND_ABORT       0x0020   /**< Send ABORT to destination */
#define SN_TX_ABORT         0x0030   /**< mask for transmitting abort */
#define SN_REFLECT_ERROR    0x0100   /**< Reply with ERROR to sender on OOTB packet Tbit set */
#define SN_REPLY_ERROR      0x0200   /**< Reply with ERROR to sender on ASCONF clash */
#define SN_TX_ERROR         0x0300   /**< mask for transmitting error */


#define PKT_ALIAS_RESPOND   0x1000   /**< Signal to libalias that there is a response packet to send */
/*
 * Data structures
 */

/**
 * @brief sctp association information
 *
 * Structure that contains information about a particular sctp association
 * currently under Network Address Translation.
 * Information is stored in network byte order (as is libalias)***
 */
struct sctp_nat_assoc {
	uint32_t l_vtag;		/**< local side verification tag */
	uint16_t l_port;		/**< local side port number */
	uint32_t g_vtag;		/**< global side verification tag */
	uint16_t g_port;		/**< global side port number */
	struct in_addr l_addr;	/**< local ip address */
	struct in_addr a_addr;	/**< alias ip address */
	int state;			/**< current state of NAT association */
	int TableRegister;		/**< stores which look up tables association is registered in */
	int exp;			/**< timer expiration in seconds from uptime */
	int exp_loc;			/**< current location in timer_Q */
	int num_Gaddr;		/**< number of global IP addresses in the list */
	LIST_HEAD(sctpGlobalAddresshead,sctp_GlobalAddress) Gaddr; /**< List of global addresses */
	LIST_ENTRY (sctp_nat_assoc) list_L; /**< Linked list of pointers for Local table*/
	LIST_ENTRY (sctp_nat_assoc) list_G; /**< Linked list of pointers for Global table */
	LIST_ENTRY (sctp_nat_assoc) timer_Q; /**< Linked list of pointers for timer Q */
//Using libalias locking
};

struct sctp_GlobalAddress {
	struct in_addr g_addr;
	LIST_ENTRY (sctp_GlobalAddress) list_Gaddr; /**< Linked list of pointers for Global table */
};

/**
 * @brief SCTP chunk of interest
 *
 * The only chunks whose contents are of any interest are the INIT and ASCONF_AddIP
 */
union sctpChunkOfInt {
	struct sctp_init *Init;	/**< Pointer to Init Chunk */
	struct sctp_init_ack *InitAck;	/**< Pointer to Init Chunk */
	struct sctp_paramhdr *Asconf; /**< Pointer to ASCONF chunk */
};


/**
 * @brief SCTP message
 * 
 * Structure containing the relevant information from the SCTP message
 */
struct sctp_nat_msg {
	uint16_t msg;			/**< one of the key messages defined above */
#ifdef INET6
	//  struct ip6_hdr *ip_hdr;	/**< pointer to ip packet header */ /*no inet6 support yet*/
#else
	struct ip *ip_hdr;		/**< pointer to ip packet header */
#endif //#ifdef INET6
	struct sctphdr *sctp_hdr;	/**< pointer to sctp common header */
	union sctpChunkOfInt sctpchnk; /**< union of pointers to the chunk of interest */
	int chunk_length;		/**< length of chunk of interest */
};


/**
 * @brief sctp nat timer queue structure
 * 
 */

struct sctp_nat_timer {
	int loc_time;			/**< time in seconds for the current location in the queue */
	int cur_loc;			/**< index of the current location in the circular queue */
	LIST_HEAD(sctpTimerQ,sctp_nat_assoc) *TimerQ; /**< List of associations at this position in the timer Q */
};



#endif //#ifndef _ALIAS_SCTP_H
