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
 *  This software was developed by David A. Hayes and Jason But
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
/** @mainpage
 * Alias_sctp is part of the SONATA (http://caia.swin.edu.au/urp/sonata) project
 * to develop and release a BSD licensed implementation of a Network Address
 * Translation (NAT) module that supports the Stream Control Transmission
 * Protocol (SCTP).
 *
 * Traditional address and port number look ups are inadequate for SCTP's
 * operation due to both processing requirements and issues with multi-homing.
 * Alias_sctp integrates with FreeBSD's ipfw/libalias NAT system.
 *
 * Version 0.2 features include:
 * - Support for global multi-homing
 * - Support for ASCONF modification from Internet Draft
 *   (draft-stewart-behave-sctpnat-04, R. Stewart and M. Tuexen, "Stream control
 *   transmission protocol (SCTP) network address translation," Jul. 2008) to
 *   provide support for multi-homed privately addressed hosts
 * - Support for forwarding of T-flagged packets
 * - Generation and delivery of AbortM/ErrorM packets upon detection of NAT
 *   collisions
 * - Per-port forwarding rules
 * - Dynamically controllable logging and statistics
 * - Dynamic management of timers
 * - Dynamic control of hash-table size
 */

/* $FreeBSD$ */

#ifdef _KERNEL
#include <machine/stdarg.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/syslog.h>
#include <netinet/libalias/alias_sctp.h>
#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>
#include <netinet/sctp_crc32.h>
#include <machine/in_cksum.h>
#else
#include "alias_sctp.h"
#include <arpa/inet.h>
#include "alias.h"
#include "alias_local.h"
#include <machine/in_cksum.h>
#include <sys/libkern.h>
#endif //#ifdef _KERNEL

/* ----------------------------------------------------------------------
 *                          FUNCTION PROTOTYPES
 * ----------------------------------------------------------------------
 */
/* Packet Parsing Functions */
static int sctp_PktParser(struct libalias *la, int direction, struct ip *pip,
    struct sctp_nat_msg *sm, struct sctp_nat_assoc **passoc);
static int GetAsconfVtags(struct libalias *la, struct sctp_nat_msg *sm,
    uint32_t *l_vtag, uint32_t *g_vtag, int direction);
static int IsASCONFack(struct libalias *la, struct sctp_nat_msg *sm, int direction);

static void AddGlobalIPAddresses(struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc, int direction);
static int  Add_Global_Address_to_List(struct sctp_nat_assoc *assoc,  struct sctp_GlobalAddress *G_addr);
static void RmGlobalIPAddresses(struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc, int direction);
static int IsADDorDEL(struct libalias *la, struct sctp_nat_msg *sm, int direction);

/* State Machine Functions */
static int ProcessSctpMsg(struct libalias *la, int direction, \
    struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc);

static int ID_process(struct libalias *la, int direction,\
    struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm);
static int INi_process(struct libalias *la, int direction,\
    struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm);
static int INa_process(struct libalias *la, int direction,\
    struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm);
static int UP_process(struct libalias *la, int direction,\
    struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm);
static int CL_process(struct libalias *la, int direction,\
    struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm);
static void TxAbortErrorM(struct libalias *la,  struct sctp_nat_msg *sm,\
    struct sctp_nat_assoc *assoc, int sndrply, int direction);

/* Hash Table Functions */
static struct sctp_nat_assoc*
FindSctpLocal(struct libalias *la, struct in_addr l_addr, struct in_addr g_addr, uint32_t l_vtag, uint16_t l_port, uint16_t g_port);
static struct sctp_nat_assoc*
FindSctpGlobal(struct libalias *la, struct in_addr g_addr, uint32_t g_vtag, uint16_t g_port, uint16_t l_port, int *partial_match);
static struct sctp_nat_assoc*
FindSctpGlobalClash(struct libalias *la,  struct sctp_nat_assoc *Cassoc);
static struct sctp_nat_assoc*
FindSctpLocalT(struct libalias *la,  struct in_addr g_addr, uint32_t l_vtag, uint16_t g_port, uint16_t l_port);
static struct sctp_nat_assoc*
FindSctpGlobalT(struct libalias *la, struct in_addr g_addr, uint32_t g_vtag, uint16_t l_port, uint16_t g_port);

static int AddSctpAssocLocal(struct libalias *la, struct sctp_nat_assoc *assoc, struct in_addr g_addr);
static int AddSctpAssocGlobal(struct libalias *la, struct sctp_nat_assoc *assoc);
static void RmSctpAssoc(struct libalias *la, struct sctp_nat_assoc *assoc);
static void freeGlobalAddressList(struct sctp_nat_assoc *assoc);

/* Timer Queue Functions */
static void sctp_AddTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc);
static void sctp_RmTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc);
static void sctp_ResetTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc, int newexp);
void sctp_CheckTimers(struct libalias *la);


/* Logging Functions */
static void logsctperror(char* errormsg, uint32_t vtag, int error, int direction);
static void logsctpparse(int direction, struct sctp_nat_msg *sm);
static void logsctpassoc(struct sctp_nat_assoc *assoc, char *s);
static void logTimerQ(struct libalias *la);
static void logSctpGlobal(struct libalias *la);
static void logSctpLocal(struct libalias *la);
#ifdef _KERNEL
static void SctpAliasLog(const char *format, ...);
#endif

/** @defgroup external External code changes and modifications
 *
 * Some changes have been made to files external to alias_sctp.(c|h). These
 * changes are primarily due to code needing to call static functions within
 * those files or to perform extra functionality that can only be performed
 * within these files.
 */
/** @ingroup external
 * @brief Log current statistics for the libalias instance
 *
 * This function is defined in alias_db.c, since it calls static functions in
 * this file
 *
 * Calls the higher level ShowAliasStats() in alias_db.c which logs all current
 * statistics about the libalias instance - including SCTP statistics
 *
 * @param la Pointer to the libalias instance
 */
void SctpShowAliasStats(struct libalias *la);

#ifdef	_KERNEL

static MALLOC_DEFINE(M_SCTPNAT, "sctpnat", "sctp nat dbs");
/* Use kernel allocator. */
#ifdef _SYS_MALLOC_H_
#define	sn_malloc(x)	malloc(x, M_SCTPNAT, M_NOWAIT|M_ZERO)
#define	sn_calloc(n,x)	mallocarray((n), (x), M_SCTPNAT, M_NOWAIT|M_ZERO)
#define	sn_free(x)	free(x, M_SCTPNAT)
#endif// #ifdef _SYS_MALLOC_H_

#else //#ifdef	_KERNEL
#define	sn_malloc(x)	malloc(x)
#define	sn_calloc(n, x)	calloc(n, x)
#define	sn_free(x)	free(x)

#endif //#ifdef	_KERNEL

/** @defgroup packet_parser SCTP Packet Parsing
 *
 * Macros to:
 * - Return pointers to the first and next SCTP chunks within an SCTP Packet
 * - Define possible return values of the packet parsing process
 * - SCTP message types for storing in the sctp_nat_msg structure @{
 */

#define SN_SCTP_FIRSTCHUNK(sctphead)	(struct sctp_chunkhdr *)(((char *)sctphead) + sizeof(struct sctphdr))
/**< Returns a pointer to the first chunk in an SCTP packet given a pointer to the SCTP header */

#define SN_SCTP_NEXTCHUNK(chunkhead)	(struct sctp_chunkhdr *)(((char *)chunkhead) + SCTP_SIZE32(ntohs(chunkhead->chunk_length)))
/**< Returns a pointer to the next chunk in an SCTP packet given a pointer to the current chunk */

#define SN_SCTP_NEXTPARAM(param)	(struct sctp_paramhdr *)(((char *)param) + SCTP_SIZE32(ntohs(param->param_length)))
/**< Returns a pointer to the next parameter in an SCTP packet given a pointer to the current parameter */

#define SN_MIN_CHUNK_SIZE        4    /**< Smallest possible SCTP chunk size in bytes */
#define SN_MIN_PARAM_SIZE        4    /**< Smallest possible SCTP param size in bytes */
#define SN_VTAG_PARAM_SIZE      12    /**< Size of  SCTP ASCONF vtag param in bytes */
#define SN_ASCONFACK_PARAM_SIZE  8    /**< Size of  SCTP ASCONF ACK param in bytes */

/* Packet parsing return codes */
#define SN_PARSE_OK                  0    /**< Packet parsed for SCTP messages */
#define SN_PARSE_ERROR_IPSHL         1    /**< Packet parsing error - IP and SCTP common header len */
#define SN_PARSE_ERROR_AS_MALLOC     2    /**< Packet parsing error - assoc malloc */
#define SN_PARSE_ERROR_CHHL          3    /**< Packet parsing error - Chunk header len */
#define SN_PARSE_ERROR_DIR           4    /**< Packet parsing error - Direction */
#define SN_PARSE_ERROR_VTAG          5    /**< Packet parsing error - Vtag */
#define SN_PARSE_ERROR_CHUNK         6    /**< Packet parsing error - Chunk */
#define SN_PARSE_ERROR_PORT          7    /**< Packet parsing error - Port=0 */
#define SN_PARSE_ERROR_LOOKUP        8    /**< Packet parsing error - Lookup */
#define SN_PARSE_ERROR_PARTIALLOOKUP 9    /**< Packet parsing error - partial lookup only found */
#define SN_PARSE_ERROR_LOOKUP_ABORT  10   /**< Packet parsing error - Lookup - but abort packet */

/* Alias_sctp performs its processing based on a number of key messages */
#define SN_SCTP_ABORT       0x0000    /**< a packet containing an ABORT chunk */
#define SN_SCTP_INIT        0x0001    /**< a packet containing an INIT chunk */
#define SN_SCTP_INITACK     0x0002    /**< a packet containing an INIT-ACK chunk */
#define SN_SCTP_SHUTCOMP    0x0010    /**< a packet containing a SHUTDOWN-COMPLETE chunk */
#define SN_SCTP_SHUTACK     0x0020    /**< a packet containing a SHUTDOWN-ACK chunk */
#define SN_SCTP_ASCONF      0x0100    /**< a packet containing an ASCONF chunk */
#define SN_SCTP_ASCONFACK   0x0200    /**< a packet containing an ASCONF-ACK chunk */
#define SN_SCTP_OTHER       0xFFFF    /**< a packet containing a chunk that is not of interest */

/** @}
 * @defgroup state_machine SCTP NAT State Machine
 *
 * Defines the various states an association can be within the NAT @{
 */
#define SN_ID  0x0000		/**< Idle state */
#define SN_INi 0x0010		/**< Initialising, waiting for InitAck state */
#define SN_INa 0x0020		/**< Initialising, waiting for AddIpAck state */
#define SN_UP  0x0100		/**< Association in UP state */
#define SN_CL  0x1000		/**< Closing state */
#define SN_RM  0x2000		/**< Removing state */

/** @}
 * @defgroup Logging Logging Functionality
 *
 * Define various log levels and a macro to call specified log functions only if
 * the current log level (sysctl_log_level) matches the specified level @{
 */
#define	SN_LOG_LOW	  0
#define SN_LOG_EVENT      1
#define	SN_LOG_INFO	  2
#define	SN_LOG_DETAIL	  3
#define	SN_LOG_DEBUG	  4
#define	SN_LOG_DEBUG_MAX  5

#define	SN_LOG(level, action)	if (sysctl_log_level >= level) { action; } /**< Perform log action ONLY if the current log level meets the specified log level */

/** @}
 * @defgroup Hash Hash Table Macros and Functions
 *
 * Defines minimum/maximum/default values for the hash table size @{
 */
#define SN_MIN_HASH_SIZE        101   /**< Minimum hash table size (set to stop users choosing stupid values) */
#define SN_MAX_HASH_SIZE    1000001   /**< Maximum hash table size (NB must be less than max int) */
#define SN_DEFAULT_HASH_SIZE   2003   /**< A reasonable default size for the hash tables */

#define SN_LOCAL_TBL           0x01   /**< assoc in local table */
#define SN_GLOBAL_TBL          0x02   /**< assoc in global table */
#define SN_BOTH_TBL            0x03   /**< assoc in both tables */
#define SN_WAIT_TOLOCAL        0x10   /**< assoc waiting for TOLOCAL asconf ACK*/
#define SN_WAIT_TOGLOBAL       0x20   /**< assoc waiting for TOLOCAL asconf ACK*/
#define SN_NULL_TBL            0x00   /**< assoc in No table */
#define SN_MAX_GLOBAL_ADDRESSES 100   /**< absolute maximum global address count*/

#define SN_ADD_OK                 0   /**< Association added to the table */
#define SN_ADD_CLASH              1   /**< Clash when trying to add the assoc. info to the table */

#define SN_TABLE_HASH(vtag, port, size) (((u_int) vtag + (u_int) port) % (u_int) size) /**< Calculate the hash table lookup position */

/** @}
 * @defgroup Timer Timer Queue Macros and Functions
 *
 * Timer macros set minimum/maximum timeout values and calculate timer expiry
 * times for the provided libalias instance @{
 */
#define SN_MIN_TIMER 1
#define SN_MAX_TIMER 600
#define SN_TIMER_QUEUE_SIZE SN_MAX_TIMER+2

#define SN_I_T(la) (la->timeStamp + sysctl_init_timer)       /**< INIT State expiration time in seconds */
#define SN_U_T(la) (la->timeStamp + sysctl_up_timer)         /**< UP State expiration time in seconds */
#define SN_C_T(la) (la->timeStamp + sysctl_shutdown_timer)   /**< CL State expiration time in seconds */
#define SN_X_T(la) (la->timeStamp + sysctl_holddown_timer)   /**< Wait after a shutdown complete in seconds */

/** @}
 * @defgroup sysctl SysCtl Variable and callback function declarations
 *
 * Sysctl variables to modify NAT functionality in real-time along with associated functions
 * to manage modifications to the sysctl variables @{
 */

/* Callbacks */
int sysctl_chg_loglevel(SYSCTL_HANDLER_ARGS);
int sysctl_chg_timer(SYSCTL_HANDLER_ARGS);
int sysctl_chg_hashtable_size(SYSCTL_HANDLER_ARGS);
int sysctl_chg_error_on_ootb(SYSCTL_HANDLER_ARGS);
int sysctl_chg_accept_global_ootb_addip(SYSCTL_HANDLER_ARGS);
int sysctl_chg_initialising_chunk_proc_limit(SYSCTL_HANDLER_ARGS);
int sysctl_chg_chunk_proc_limit(SYSCTL_HANDLER_ARGS);
int sysctl_chg_param_proc_limit(SYSCTL_HANDLER_ARGS);
int sysctl_chg_track_global_addresses(SYSCTL_HANDLER_ARGS);

/* Sysctl variables */
/** @brief net.inet.ip.alias.sctp.log_level */
static u_int sysctl_log_level = 0; /**< Stores the current level of logging */
/** @brief net.inet.ip.alias.sctp.init_timer */
static u_int sysctl_init_timer = 15; /**< Seconds to hold an association in the table waiting for an INIT-ACK or AddIP-ACK */
/** @brief net.inet.ip.alias.sctp.up_timer */
static u_int sysctl_up_timer = 300; /**< Seconds to hold an association in the table while no packets are transmitted */
/** @brief net.inet.ip.alias.sctp.shutdown_timer */
static u_int sysctl_shutdown_timer = 15; /**< Seconds to hold an association in the table waiting for a SHUTDOWN-COMPLETE */
/** @brief net.inet.ip.alias.sctp.holddown_timer */
static u_int sysctl_holddown_timer = 0; /**< Seconds to hold an association in the table after it has been shutdown (to allow for lost SHUTDOWN-COMPLETEs) */
/** @brief net.inet.ip.alias.sctp.hashtable_size */
static u_int sysctl_hashtable_size = SN_DEFAULT_HASH_SIZE; /**< Sets the hash table size for any NEW NAT instances (existing instances retain their existing Hash Table */
/** @brief net.inet.ip.alias.sctp.error_on_ootb */
static u_int sysctl_error_on_ootb = 1; /**< NAT response  to receipt of OOTB packet
					  (0 - No response, 1 - NAT will send ErrorM only to local side,
					  2 -  NAT will send local ErrorM and global ErrorM if there was a partial association match
					  3 - NAT will send ErrorM to both local and global) */
/** @brief net.inet.ip.alias.sctp.accept_global_ootb_addip */
static u_int sysctl_accept_global_ootb_addip = 0; /**<NAT responset to receipt of global OOTB AddIP (0 - No response, 1 - NAT will accept OOTB global AddIP messages for processing (Security risk)) */
/** @brief net.inet.ip.alias.sctp.initialising_chunk_proc_limit */
static u_int sysctl_initialising_chunk_proc_limit = 2; /**< A limit on the number of chunks that should be searched if there is no matching association (DoS prevention) */
/** @brief net.inet.ip.alias.sctp.param_proc_limit */
static u_int sysctl_chunk_proc_limit = 5; /**< A limit on the number of chunks that should be searched (DoS prevention) */
/** @brief net.inet.ip.alias.sctp.param_proc_limit */
static u_int sysctl_param_proc_limit = 25; /**< A limit on the number of parameters (in chunks) that should be searched (DoS prevention) */
/** @brief net.inet.ip.alias.sctp.track_global_addresses */
static u_int sysctl_track_global_addresses = 0; /**< Configures the global address tracking option within the NAT (0 - Global tracking is disabled, > 0 - enables tracking but limits the number of global IP addresses to this value)
						   If set to >=1 the NAT will track that many global IP addresses. This may reduce look up table conflicts, but increases processing */

#define SN_NO_ERROR_ON_OOTB              0 /**< Send no errorM on out of the blue packets */
#define SN_LOCAL_ERROR_ON_OOTB           1 /**< Send only local errorM on out of the blue packets */
#define SN_LOCALandPARTIAL_ERROR_ON_OOTB 2 /**< Send local errorM and global errorM for out of the blue packets only if partial match found */
#define SN_ERROR_ON_OOTB                 3 /**< Send errorM on out of the blue packets */

#ifdef SYSCTL_NODE

SYSCTL_DECL(_net_inet);
SYSCTL_DECL(_net_inet_ip);
SYSCTL_DECL(_net_inet_ip_alias);

static SYSCTL_NODE(_net_inet_ip_alias, OID_AUTO, sctp, CTLFLAG_RW, NULL,
    "SCTP NAT");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, log_level, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_log_level, 0, sysctl_chg_loglevel, "IU",
    "Level of detail (0 - default, 1 - event, 2 - info, 3 - detail, 4 - debug, 5 - max debug)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, init_timer, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_init_timer, 0, sysctl_chg_timer, "IU",
    "Timeout value (s) while waiting for (INIT-ACK|AddIP-ACK)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, up_timer, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_up_timer, 0, sysctl_chg_timer, "IU",
    "Timeout value (s) to keep an association up with no traffic");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, shutdown_timer, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_shutdown_timer, 0, sysctl_chg_timer, "IU",
    "Timeout value (s) while waiting for SHUTDOWN-COMPLETE");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, holddown_timer, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_holddown_timer, 0, sysctl_chg_timer, "IU",
    "Hold association in table for this many seconds after receiving a SHUTDOWN-COMPLETE");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, hashtable_size, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_hashtable_size, 0, sysctl_chg_hashtable_size, "IU",
    "Size of hash tables used for NAT lookups (100 < prime_number > 1000001)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, error_on_ootb, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_error_on_ootb, 0, sysctl_chg_error_on_ootb, "IU",
    "ErrorM sent on receipt of ootb packet:\n\t0 - none,\n\t1 - to local only,\n\t2 - to local and global if a partial association match,\n\t3 - to local and global (DoS risk)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, accept_global_ootb_addip, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_accept_global_ootb_addip, 0, sysctl_chg_accept_global_ootb_addip, "IU",
    "NAT response to receipt of global OOTB AddIP:\n\t0 - No response,\n\t1 - NAT will accept OOTB global AddIP messages for processing (Security risk)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, initialising_chunk_proc_limit, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_initialising_chunk_proc_limit, 0, sysctl_chg_initialising_chunk_proc_limit, "IU",
    "Number of chunks that should be processed if there is no current association found:\n\t > 0 (A high value is a DoS risk)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, chunk_proc_limit, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_chunk_proc_limit, 0, sysctl_chg_chunk_proc_limit, "IU",
    "Number of chunks that should be processed to find key chunk:\n\t>= initialising_chunk_proc_limit (A high value is a DoS risk)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, param_proc_limit, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_param_proc_limit, 0, sysctl_chg_param_proc_limit, "IU",
    "Number of parameters (in a chunk) that should be processed to find key parameters:\n\t> 1 (A high value is a DoS risk)");
SYSCTL_PROC(_net_inet_ip_alias_sctp, OID_AUTO, track_global_addresses, CTLTYPE_UINT | CTLFLAG_RW,
    &sysctl_track_global_addresses, 0, sysctl_chg_track_global_addresses, "IU",
    "Configures the global address tracking option within the NAT:\n\t0 - Global tracking is disabled,\n\t> 0 - enables tracking but limits the number of global IP addresses to this value");

#endif /* SYSCTL_NODE */

/** @}
 * @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.fw.sctp.log_level
 *
 * Updates the variable sysctl_log_level to the provided value and ensures
 * it is in the valid range (SN_LOG_LOW -> SN_LOG_DEBUG)
 */
int sysctl_chg_loglevel(SYSCTL_HANDLER_ARGS)
{
	u_int level = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &level, 0, req);
	if (error) return (error);

	level = (level > SN_LOG_DEBUG_MAX) ? (SN_LOG_DEBUG_MAX) : (level);
	level = (level < SN_LOG_LOW) ? (SN_LOG_LOW) : (level);
	sysctl_log_level = level;
	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.fw.sctp.(init_timer|up_timer|shutdown_timer)
 *
 * Updates the timer-based sysctl variables. The new values are sanity-checked
 * to make sure that they are within the range SN_MIN_TIMER-SN_MAX_TIMER. The
 * holddown timer is allowed to be 0
 */
int sysctl_chg_timer(SYSCTL_HANDLER_ARGS)
{
	u_int timer = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &timer, 0, req);
	if (error) return (error);

	timer = (timer > SN_MAX_TIMER) ? (SN_MAX_TIMER) : (timer);

	if (((u_int *)arg1) != &sysctl_holddown_timer) {
		timer = (timer < SN_MIN_TIMER) ? (SN_MIN_TIMER) : (timer);
	}

	*(u_int *)arg1 = timer;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.hashtable_size
 *
 * Updates the hashtable_size sysctl variable. The new value should be a prime
 * number.  We sanity check to ensure that the size is within the range
 * SN_MIN_HASH_SIZE-SN_MAX_HASH_SIZE. We then check the provided number to see
 * if it is prime. We approximate by checking that (2,3,5,7,11) are not factors,
 * incrementing the user provided value until we find a suitable number.
 */
int sysctl_chg_hashtable_size(SYSCTL_HANDLER_ARGS)
{
	u_int size = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &size, 0, req);
	if (error) return (error);

	size = (size < SN_MIN_HASH_SIZE) ? (SN_MIN_HASH_SIZE) : ((size > SN_MAX_HASH_SIZE) ? (SN_MAX_HASH_SIZE) : (size));

	size |= 0x00000001; /* make odd */

	for (;(((size % 3) == 0) || ((size % 5) == 0) || ((size % 7) == 0) || ((size % 11) == 0)); size+=2);
	sysctl_hashtable_size = size;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.error_on_ootb
 *
 * Updates the error_on_clash sysctl variable.
 * If set to 0, no ErrorM will be sent if there is a look up table clash
 * If set to 1, an ErrorM is sent only to the local side
 * If set to 2, an ErrorM is sent to the local side and global side if there is
 *                                                  a partial association match
 * If set to 3, an ErrorM is sent to both local and global sides (DoS) risk.
 */
int sysctl_chg_error_on_ootb(SYSCTL_HANDLER_ARGS)
{
	u_int flag = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &flag, 0, req);
	if (error) return (error);

	sysctl_error_on_ootb = (flag > SN_ERROR_ON_OOTB) ? SN_ERROR_ON_OOTB: flag;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.accept_global_ootb_addip
 *
 * If set to 1 the NAT will accept ootb global addip messages for processing (Security risk)
 * Default is 0, only responding to local ootb AddIP messages
 */
int sysctl_chg_accept_global_ootb_addip(SYSCTL_HANDLER_ARGS)
{
	u_int flag = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &flag, 0, req);
	if (error) return (error);

	sysctl_accept_global_ootb_addip = (flag == 1) ? 1: 0;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.initialising_chunk_proc_limit
 *
 * Updates the initialising_chunk_proc_limit sysctl variable.  Number of chunks
 * that should be processed if there is no current association found: > 0 (A
 * high value is a DoS risk)
 */
int sysctl_chg_initialising_chunk_proc_limit(SYSCTL_HANDLER_ARGS)
{
	u_int proclimit = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &proclimit, 0, req);
	if (error) return (error);

	sysctl_initialising_chunk_proc_limit = (proclimit < 1) ? 1: proclimit;
	sysctl_chunk_proc_limit =
		(sysctl_chunk_proc_limit < sysctl_initialising_chunk_proc_limit) ? sysctl_initialising_chunk_proc_limit : sysctl_chunk_proc_limit;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.chunk_proc_limit
 *
 * Updates the chunk_proc_limit sysctl variable.
 * Number of chunks that should be processed to find key chunk:
 *  >= initialising_chunk_proc_limit (A high value is a DoS risk)
 */
int sysctl_chg_chunk_proc_limit(SYSCTL_HANDLER_ARGS)
{
	u_int proclimit = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &proclimit, 0, req);
	if (error) return (error);

	sysctl_chunk_proc_limit =
		(proclimit < sysctl_initialising_chunk_proc_limit) ? sysctl_initialising_chunk_proc_limit : proclimit;

	return (0);
}


/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.param_proc_limit
 *
 * Updates the param_proc_limit sysctl variable.
 * Number of parameters that should be processed to find key parameters:
 *  > 1 (A high value is a DoS risk)
 */
int sysctl_chg_param_proc_limit(SYSCTL_HANDLER_ARGS)
{
	u_int proclimit = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &proclimit, 0, req);
	if (error) return (error);

	sysctl_param_proc_limit =
		(proclimit < 2) ? 2 : proclimit;

	return (0);
}

/** @ingroup sysctl
 * @brief sysctl callback for changing net.inet.ip.alias.sctp.track_global_addresses
 *
 *Configures the global address tracking option within the NAT (0 - Global
 *tracking is disabled, > 0 - enables tracking but limits the number of global
 *IP addresses to this value)
 */
int sysctl_chg_track_global_addresses(SYSCTL_HANDLER_ARGS)
{
	u_int num_to_track = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &num_to_track, 0, req);
	if (error) return (error);

	sysctl_track_global_addresses = (num_to_track > SN_MAX_GLOBAL_ADDRESSES) ? SN_MAX_GLOBAL_ADDRESSES : num_to_track;

	return (0);
}


/* ----------------------------------------------------------------------
 *                            CODE BEGINS HERE
 * ----------------------------------------------------------------------
 */
/**
 * @brief Initialises the SCTP NAT Implementation
 *
 * Creates the look-up tables and the timer queue and initialises all state
 * variables
 *
 * @param la Pointer to the relevant libalias instance
 */
void AliasSctpInit(struct libalias *la)
{
	/* Initialise association tables*/
	int i;
	la->sctpNatTableSize = sysctl_hashtable_size;
	SN_LOG(SN_LOG_EVENT,
	    SctpAliasLog("Initialising SCTP NAT Instance (hash_table_size:%d)\n", la->sctpNatTableSize));
	la->sctpTableLocal = sn_calloc(la->sctpNatTableSize, sizeof(struct sctpNatTableL));
	la->sctpTableGlobal = sn_calloc(la->sctpNatTableSize, sizeof(struct sctpNatTableG));
	la->sctpNatTimer.TimerQ = sn_calloc(SN_TIMER_QUEUE_SIZE, sizeof(struct sctpTimerQ));
	/* Initialise hash table */
	for (i = 0; i < la->sctpNatTableSize; i++) {
		LIST_INIT(&la->sctpTableLocal[i]);
		LIST_INIT(&la->sctpTableGlobal[i]);
	}

	/* Initialise circular timer Q*/
	for (i = 0; i < SN_TIMER_QUEUE_SIZE; i++)
		LIST_INIT(&la->sctpNatTimer.TimerQ[i]);
#ifdef _KERNEL
	la->sctpNatTimer.loc_time=time_uptime; /* la->timeStamp is not set yet */
#else
	la->sctpNatTimer.loc_time=la->timeStamp;
#endif
	la->sctpNatTimer.cur_loc = 0;
	la->sctpLinkCount = 0;
}

/**
 * @brief Cleans-up the SCTP NAT Implementation prior to unloading
 *
 * Removes all entries from the timer queue, freeing associations as it goes.
 * We then free memory allocated to the look-up tables and the time queue
 *
 * NOTE: We do not need to traverse the look-up tables as each association
 *       will always have an entry in the timer queue, freeing this memory
 *       once will free all memory allocated to entries in the look-up tables
 *
 * @param la Pointer to the relevant libalias instance
 */
void AliasSctpTerm(struct libalias *la)
{
	struct sctp_nat_assoc *assoc1, *assoc2;
	int                   i;

	LIBALIAS_LOCK_ASSERT(la);
	SN_LOG(SN_LOG_EVENT,
	    SctpAliasLog("Removing SCTP NAT Instance\n"));
	for (i = 0; i < SN_TIMER_QUEUE_SIZE; i++) {
		assoc1 = LIST_FIRST(&la->sctpNatTimer.TimerQ[i]);
		while (assoc1 != NULL) {
			freeGlobalAddressList(assoc1);
			assoc2 = LIST_NEXT(assoc1, timer_Q);
			sn_free(assoc1);
			assoc1 = assoc2;
		}
	}

	sn_free(la->sctpTableLocal);
	sn_free(la->sctpTableGlobal);
	sn_free(la->sctpNatTimer.TimerQ);
}

/**
 * @brief Handles SCTP packets passed from libalias
 *
 * This function needs to actually NAT/drop packets and possibly create and
 * send AbortM or ErrorM packets in response. The process involves:
 * - Validating the direction parameter passed by the caller
 * - Checking and handling any expired timers for the NAT
 * - Calling sctp_PktParser() to parse the packet
 * - Call ProcessSctpMsg() to decide the appropriate outcome and to update
 *   the NAT tables
 * - Based on the return code either:
 *   - NAT the packet
 *   - Construct and send an ErrorM|AbortM packet
 *   - Mark the association for removal from the tables
 * - Potentially remove the association from all lookup tables
 * - Return the appropriate result to libalias
 *
 * @param la Pointer to the relevant libalias instance
 * @param pip Pointer to IP packet to process
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 * @return  PKT_ALIAS_OK | PKT_ALIAS_IGNORE | PKT_ALIAS_ERROR
 */
int
SctpAlias(struct libalias *la, struct ip *pip, int direction)
{
	int rtnval;
	struct sctp_nat_msg msg;
	struct sctp_nat_assoc *assoc = NULL;

	if ((direction != SN_TO_LOCAL) && (direction != SN_TO_GLOBAL)) {
		SctpAliasLog("ERROR: Invalid direction\n");
		return (PKT_ALIAS_ERROR);
	}

	sctp_CheckTimers(la); /* Check timers */

	/* Parse the packet */
	rtnval = sctp_PktParser(la, direction, pip, &msg, &assoc); //using *char (change to mbuf when get code from paolo)
	switch (rtnval) {
	case SN_PARSE_OK:
		break;
	case SN_PARSE_ERROR_CHHL:
		/* Not an error if there is a chunk length parsing error and this is a fragmented packet */
		if (ntohs(pip->ip_off) & IP_MF) {
			rtnval = SN_PARSE_OK;
			break;
		}
		SN_LOG(SN_LOG_EVENT,
		    logsctperror("SN_PARSE_ERROR", msg.sctp_hdr->v_tag, rtnval, direction));
		return (PKT_ALIAS_ERROR);
	case SN_PARSE_ERROR_PARTIALLOOKUP:
		if (sysctl_error_on_ootb > SN_LOCALandPARTIAL_ERROR_ON_OOTB) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("SN_PARSE_ERROR", msg.sctp_hdr->v_tag, rtnval, direction));
			return (PKT_ALIAS_ERROR);
		}
	case SN_PARSE_ERROR_LOOKUP:
		if (sysctl_error_on_ootb == SN_ERROR_ON_OOTB ||
		    (sysctl_error_on_ootb == SN_LOCALandPARTIAL_ERROR_ON_OOTB && direction == SN_TO_LOCAL) ||
		    (sysctl_error_on_ootb == SN_LOCAL_ERROR_ON_OOTB && direction == SN_TO_GLOBAL)) {
			TxAbortErrorM(la, &msg, assoc, SN_REFLECT_ERROR, direction); /*NB assoc=NULL */
			return (PKT_ALIAS_RESPOND);
		}
	default:
		SN_LOG(SN_LOG_EVENT,
		    logsctperror("SN_PARSE_ERROR", msg.sctp_hdr->v_tag, rtnval, direction));
		return (PKT_ALIAS_ERROR);
	}

	SN_LOG(SN_LOG_DETAIL,
	    logsctpassoc(assoc, "*");
	    logsctpparse(direction, &msg);
		);

	/* Process the SCTP message */
	rtnval = ProcessSctpMsg(la, direction, &msg, assoc);

	SN_LOG(SN_LOG_DEBUG_MAX,
	    logsctpassoc(assoc, "-");
	    logSctpLocal(la);
	    logSctpGlobal(la);
		);
	SN_LOG(SN_LOG_DEBUG, logTimerQ(la));

	switch (rtnval) {
	case SN_NAT_PKT:
		switch (direction) {
		case SN_TO_LOCAL:
			DifferentialChecksum(&(msg.ip_hdr->ip_sum),
			    &(assoc->l_addr), &(msg.ip_hdr->ip_dst), 2);
			msg.ip_hdr->ip_dst = assoc->l_addr; /* change dst address to local address*/
			break;
		case SN_TO_GLOBAL:
			DifferentialChecksum(&(msg.ip_hdr->ip_sum),
			    &(assoc->a_addr),  &(msg.ip_hdr->ip_src), 2);
			msg.ip_hdr->ip_src = assoc->a_addr; /* change src to alias addr*/
			break;
		default:
			rtnval = SN_DROP_PKT; /* shouldn't get here, but if it does drop packet */
			SN_LOG(SN_LOG_LOW, logsctperror("ERROR: Invalid direction", msg.sctp_hdr->v_tag, rtnval, direction));
			break;
		}
		break;
	case SN_DROP_PKT:
		SN_LOG(SN_LOG_DETAIL, logsctperror("SN_DROP_PKT", msg.sctp_hdr->v_tag, rtnval, direction));
		break;
	case SN_REPLY_ABORT:
	case SN_REPLY_ERROR:
	case SN_SEND_ABORT:
		TxAbortErrorM(la, &msg, assoc, rtnval, direction);
		break;
	default:
		// big error, remove association and go to idle and write log messages
		SN_LOG(SN_LOG_LOW, logsctperror("SN_PROCESSING_ERROR", msg.sctp_hdr->v_tag, rtnval, direction));
		assoc->state=SN_RM;/* Mark for removal*/
		break;
	}

	/* Remove association if tagged for removal */
	if (assoc->state == SN_RM) {
		if (assoc->TableRegister) {
			sctp_RmTimeOut(la, assoc);
			RmSctpAssoc(la, assoc);
		}
		LIBALIAS_LOCK_ASSERT(la);
		freeGlobalAddressList(assoc);
		sn_free(assoc);
	}
	switch (rtnval) {
	case SN_NAT_PKT:
		return (PKT_ALIAS_OK);
	case SN_SEND_ABORT:
		return (PKT_ALIAS_OK);
	case SN_REPLY_ABORT:
	case SN_REPLY_ERROR:
	case SN_REFLECT_ERROR:
		return (PKT_ALIAS_RESPOND);
	case SN_DROP_PKT:
	default:
		return (PKT_ALIAS_ERROR);
	}
}

/**
 * @brief Send an AbortM or ErrorM
 *
 * We construct the new SCTP packet to send in place of the existing packet we
 * have been asked to NAT. This function can only be called if the original
 * packet was successfully parsed as a valid SCTP packet.
 *
 * An AbortM (without cause) packet is the smallest SCTP packet available and as
 * such there is always space in the existing packet buffer to fit the AbortM
 * packet. An ErrorM packet is 4 bytes longer than the (the error cause is not
 * optional). An ErrorM is sent in response to an AddIP when the Vtag/address
 * combination, if added, will produce a conflict in the association look up
 * tables. It may also be used for an unexpected packet - a packet with no
 * matching association in the NAT table and we are requesting an AddIP so we
 * can add it.  The smallest valid SCTP packet while the association is in an
 * up-state is a Heartbeat packet, which is big enough to be transformed to an
 * ErrorM.
 *
 * We create a temporary character array to store the packet as we are constructing
 * it. We then populate the array with appropriate values based on:
 * - Packet type (AbortM | ErrorM)
 * - Initial packet direction (SN_TO_LOCAL | SN_TO_GLOBAL)
 * - NAT response (Send packet | Reply packet)
 *
 * Once complete, we copy the contents of the temporary packet over the original
 * SCTP packet we were asked to NAT
 *
 * @param la Pointer to the relevant libalias instance
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to current association details
 * @param sndrply SN_SEND_ABORT | SN_REPLY_ABORT | SN_REPLY_ERROR
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 */
static uint32_t
local_sctp_finalize_crc32(uint32_t crc32c)
{
	/* This routine is duplicated from SCTP
	 * we need to do that since it MAY be that SCTP
	 * is NOT compiled into the kernel. The CRC32C routines
	 * however are always available in libkern.
	 */
	uint32_t result;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t byte0, byte1, byte2, byte3;

#endif
	/* Complement the result */
	result = ~crc32c;
#if BYTE_ORDER == BIG_ENDIAN
	/*
	 * For BIG-ENDIAN.. aka Motorola byte order the result is in
	 * little-endian form. So we must manually swap the bytes. Then we
	 * can call htonl() which does nothing...
	 */
	byte0 = result & 0x000000ff;
	byte1 = (result >> 8) & 0x000000ff;
	byte2 = (result >> 16) & 0x000000ff;
	byte3 = (result >> 24) & 0x000000ff;
	crc32c = ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3);
#else
	/*
	 * For INTEL platforms the result comes out in network order. No
	 * htonl is required or the swap above. So we optimize out both the
	 * htonl and the manual swap above.
	 */
	crc32c = result;
#endif
	return (crc32c);
}

static void
TxAbortErrorM(struct libalias *la, struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc, int sndrply, int direction)
{
	int sctp_size = sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr) + sizeof(struct sctp_error_cause);
	int ip_size = sizeof(struct ip) + sctp_size;
	int include_error_cause = 1;
	char tmp_ip[ip_size];
	char addrbuf[INET_ADDRSTRLEN];

	if (ntohs(sm->ip_hdr->ip_len) < ip_size) { /* short packet, cannot send error cause */
		include_error_cause = 0;
		ip_size = ip_size -  sizeof(struct sctp_error_cause);
		sctp_size = sctp_size -  sizeof(struct sctp_error_cause);
	}
	/* Assign header pointers packet */
	struct ip* ip = (struct ip *) tmp_ip;
	struct sctphdr* sctp_hdr = (struct sctphdr *) ((char *) ip + sizeof(*ip));
	struct sctp_chunkhdr* chunk_hdr = (struct sctp_chunkhdr *) ((char *) sctp_hdr + sizeof(*sctp_hdr));
	struct sctp_error_cause* error_cause = (struct sctp_error_cause *) ((char *) chunk_hdr + sizeof(*chunk_hdr));

	/* construct ip header */
	ip->ip_v = sm->ip_hdr->ip_v;
	ip->ip_hl = 5; /* 5*32 bit words */
	ip->ip_tos = 0;
	ip->ip_len = htons(ip_size);
	ip->ip_id = sm->ip_hdr->ip_id;
	ip->ip_off = 0;
	ip->ip_ttl = 255;
	ip->ip_p = IPPROTO_SCTP;
	/*
	  The definitions below should be removed when they make it into the SCTP stack
	*/
#define SCTP_MIDDLEBOX_FLAG 0x02
#define SCTP_NAT_TABLE_COLLISION 0x00b0
#define SCTP_MISSING_NAT 0x00b1
	chunk_hdr->chunk_type = (sndrply & SN_TX_ABORT) ? SCTP_ABORT_ASSOCIATION : SCTP_OPERATION_ERROR;
	chunk_hdr->chunk_flags = SCTP_MIDDLEBOX_FLAG;
	if (include_error_cause) {
		error_cause->code = htons((sndrply & SN_REFLECT_ERROR) ? SCTP_MISSING_NAT : SCTP_NAT_TABLE_COLLISION);
		error_cause->length = htons(sizeof(struct sctp_error_cause));
		chunk_hdr->chunk_length = htons(sizeof(*chunk_hdr) + sizeof(struct sctp_error_cause));
	} else {
		chunk_hdr->chunk_length = htons(sizeof(*chunk_hdr));
	}

	/* set specific values */
	switch (sndrply) {
	case SN_REFLECT_ERROR:
		chunk_hdr->chunk_flags |= SCTP_HAD_NO_TCB; /* set Tbit */
		sctp_hdr->v_tag = sm->sctp_hdr->v_tag;
		break;
	case SN_REPLY_ERROR:
		sctp_hdr->v_tag = (direction == SN_TO_LOCAL) ? assoc->g_vtag : assoc->l_vtag ;
		break;
	case SN_SEND_ABORT:
		sctp_hdr->v_tag = sm->sctp_hdr->v_tag;
		break;
	case SN_REPLY_ABORT:
		sctp_hdr->v_tag = sm->sctpchnk.Init->initiate_tag;
		break;
	}

	/* Set send/reply values */
	if (sndrply == SN_SEND_ABORT) { /*pass through NAT */
		ip->ip_src = (direction == SN_TO_LOCAL) ? sm->ip_hdr->ip_src : assoc->a_addr;
		ip->ip_dst = (direction == SN_TO_LOCAL) ? assoc->l_addr : sm->ip_hdr->ip_dst;
		sctp_hdr->src_port = sm->sctp_hdr->src_port;
		sctp_hdr->dest_port = sm->sctp_hdr->dest_port;
	} else { /* reply and reflect */
		ip->ip_src = sm->ip_hdr->ip_dst;
		ip->ip_dst = sm->ip_hdr->ip_src;
		sctp_hdr->src_port = sm->sctp_hdr->dest_port;
		sctp_hdr->dest_port = sm->sctp_hdr->src_port;
	}

	/* Calculate IP header checksum */
	ip->ip_sum = in_cksum_hdr(ip);

	/* calculate SCTP header CRC32 */
	sctp_hdr->checksum = 0;
	sctp_hdr->checksum = local_sctp_finalize_crc32(calculate_crc32c(0xffffffff, (unsigned char *) sctp_hdr, sctp_size));

	memcpy(sm->ip_hdr, ip, ip_size);

	SN_LOG(SN_LOG_EVENT,SctpAliasLog("%s %s 0x%x (->%s:%u vtag=0x%x crc=0x%x)\n",
		((sndrply == SN_SEND_ABORT) ? "Sending" : "Replying"),
		((sndrply & SN_TX_ERROR) ? "ErrorM" : "AbortM"),
		(include_error_cause ? ntohs(error_cause->code) : 0),
		inet_ntoa_r(ip->ip_dst, INET_NTOA_BUF(addrbuf)),
		ntohs(sctp_hdr->dest_port),
		ntohl(sctp_hdr->v_tag), ntohl(sctp_hdr->checksum)));
}

/* ----------------------------------------------------------------------
 *                           PACKET PARSER CODE
 * ----------------------------------------------------------------------
 */
/** @addtogroup packet_parser
 *
 * These functions parse the SCTP packet and fill a sctp_nat_msg structure
 * with the parsed contents.
 */
/** @ingroup packet_parser
 * @brief Parses SCTP packets for the key SCTP chunk that will be processed
 *
 * This module parses SCTP packets for the key SCTP chunk that will be processed
 * The module completes the sctp_nat_msg structure and either retrieves the
 * relevant (existing) stored association from the Hash Tables or creates a new
 * association entity with state SN_ID
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param pip
 * @param sm Pointer to sctp message information
 * @param passoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_PARSE_OK | SN_PARSE_ERROR_*
 */
static int
sctp_PktParser(struct libalias *la, int direction, struct ip *pip,
    struct sctp_nat_msg *sm, struct sctp_nat_assoc **passoc)
//sctp_PktParser(int direction, struct mbuf *ipak, int ip_hdr_len,struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc)
{
	struct sctphdr *sctp_hdr;
	struct sctp_chunkhdr *chunk_hdr;
	struct sctp_paramhdr *param_hdr;
	struct in_addr ipv4addr;
	int bytes_left; /* bytes left in ip packet */
	int chunk_length;
	int chunk_count;
	int partial_match = 0;
	//  mbuf *mp;
	//  int mlen;

	//  mlen = SCTP_HEADER_LEN(i_pak);
	//  mp = SCTP_HEADER_TO_CHAIN(i_pak); /* does nothing in bsd since header and chain not separate */

	/*
	 * Note, that if the VTag is zero, it must be an INIT
	 * Also, I am only interested in the content of INIT and ADDIP chunks
	 */

	// no mbuf stuff from Paolo yet so ...
	sm->ip_hdr = pip;
	/* remove ip header length from the bytes_left */
	bytes_left = ntohs(pip->ip_len) - (pip->ip_hl << 2);

	/* Check SCTP header length and move to first chunk */
	if (bytes_left < sizeof(struct sctphdr)) {
		sm->sctp_hdr = NULL;
		return (SN_PARSE_ERROR_IPSHL); /* packet not long enough*/
	}

	sm->sctp_hdr = sctp_hdr = (struct sctphdr *) ip_next(pip);
	bytes_left -= sizeof(struct sctphdr);

	/* Check for valid ports (zero valued ports would find partially initialised associations */
	if (sctp_hdr->src_port == 0 || sctp_hdr->dest_port == 0)
		return (SN_PARSE_ERROR_PORT);

	/* Check length of first chunk */
	if (bytes_left < SN_MIN_CHUNK_SIZE) /* malformed chunk - could cause endless loop*/
		return (SN_PARSE_ERROR_CHHL); /* packet not long enough for this chunk */

	/* First chunk */
	chunk_hdr = SN_SCTP_FIRSTCHUNK(sctp_hdr);

	chunk_length = SCTP_SIZE32(ntohs(chunk_hdr->chunk_length));
	if ((chunk_length < SN_MIN_CHUNK_SIZE) || (chunk_length > bytes_left)) /* malformed chunk - could cause endless loop*/
		return (SN_PARSE_ERROR_CHHL);

	if ((chunk_hdr->chunk_flags & SCTP_HAD_NO_TCB) &&
	    ((chunk_hdr->chunk_type == SCTP_ABORT_ASSOCIATION) ||
		(chunk_hdr->chunk_type == SCTP_SHUTDOWN_COMPLETE))) {
		/* T-Bit set */
		if (direction == SN_TO_LOCAL)
			*passoc = FindSctpGlobalT(la,  pip->ip_src, sctp_hdr->v_tag, sctp_hdr->dest_port, sctp_hdr->src_port);
		else
			*passoc = FindSctpLocalT(la, pip->ip_dst, sctp_hdr->v_tag, sctp_hdr->dest_port, sctp_hdr->src_port);
	} else {
		/* Proper v_tag settings */
		if (direction == SN_TO_LOCAL)
			*passoc = FindSctpGlobal(la, pip->ip_src, sctp_hdr->v_tag, sctp_hdr->src_port, sctp_hdr->dest_port, &partial_match);
		else
			*passoc = FindSctpLocal(la, pip->ip_src,  pip->ip_dst, sctp_hdr->v_tag, sctp_hdr->src_port, sctp_hdr->dest_port);
	}

	chunk_count = 1;
	/* Real packet parsing occurs below */
	sm->msg = SN_SCTP_OTHER;/* Initialise to largest value*/
	sm->chunk_length = 0; /* only care about length for key chunks */
	while (IS_SCTP_CONTROL(chunk_hdr)) {
		switch (chunk_hdr->chunk_type) {
		case SCTP_INITIATION:
			if (chunk_length < sizeof(struct sctp_init_chunk)) /* malformed chunk*/
				return (SN_PARSE_ERROR_CHHL);
			sm->msg = SN_SCTP_INIT;
			sm->sctpchnk.Init = (struct sctp_init *) ((char *) chunk_hdr + sizeof(struct sctp_chunkhdr));
			sm->chunk_length = chunk_length;
			/* if no existing association, create a new one */
			if (*passoc == NULL) {
				if (sctp_hdr->v_tag == 0) { //Init requires vtag=0
					*passoc = (struct sctp_nat_assoc *) sn_malloc(sizeof(struct sctp_nat_assoc));
					if (*passoc == NULL) {/* out of resources */
						return (SN_PARSE_ERROR_AS_MALLOC);
					}
					/* Initialize association - sn_malloc initializes memory to zeros */
					(*passoc)->state = SN_ID;
					LIST_INIT(&((*passoc)->Gaddr)); /* always initialise to avoid memory problems */
					(*passoc)->TableRegister = SN_NULL_TBL;
					return (SN_PARSE_OK);
				}
				return (SN_PARSE_ERROR_VTAG);
			}
			return (SN_PARSE_ERROR_LOOKUP);
		case SCTP_INITIATION_ACK:
			if (chunk_length < sizeof(struct sctp_init_ack_chunk)) /* malformed chunk*/
				return (SN_PARSE_ERROR_CHHL);
			sm->msg = SN_SCTP_INITACK;
			sm->sctpchnk.InitAck = (struct sctp_init_ack *) ((char *) chunk_hdr + sizeof(struct sctp_chunkhdr));
			sm->chunk_length = chunk_length;
			return ((*passoc == NULL) ? (SN_PARSE_ERROR_LOOKUP) : (SN_PARSE_OK));
		case SCTP_ABORT_ASSOCIATION: /* access only minimum sized chunk */
			sm->msg = SN_SCTP_ABORT;
			sm->chunk_length = chunk_length;
			return ((*passoc == NULL) ? (SN_PARSE_ERROR_LOOKUP_ABORT) : (SN_PARSE_OK));
		case SCTP_SHUTDOWN_ACK:
			if (chunk_length < sizeof(struct sctp_shutdown_ack_chunk)) /* malformed chunk*/
				return (SN_PARSE_ERROR_CHHL);
			if (sm->msg > SN_SCTP_SHUTACK) {
				sm->msg = SN_SCTP_SHUTACK;
				sm->chunk_length = chunk_length;
			}
			break;
		case SCTP_SHUTDOWN_COMPLETE:  /* minimum sized chunk */
			if (sm->msg > SN_SCTP_SHUTCOMP) {
				sm->msg = SN_SCTP_SHUTCOMP;
				sm->chunk_length = chunk_length;
			}
			return ((*passoc == NULL) ? (SN_PARSE_ERROR_LOOKUP) : (SN_PARSE_OK));
		case SCTP_ASCONF:
			if (sm->msg > SN_SCTP_ASCONF) {
				if (chunk_length < (sizeof(struct  sctp_asconf_chunk) + sizeof(struct  sctp_ipv4addr_param))) /* malformed chunk*/
					return (SN_PARSE_ERROR_CHHL);
				//leave parameter searching to later, if required
				param_hdr = (struct sctp_paramhdr *) ((char *) chunk_hdr + sizeof(struct sctp_asconf_chunk)); /*compulsory IP parameter*/
				if (ntohs(param_hdr->param_type) == SCTP_IPV4_ADDRESS) {
					if ((*passoc == NULL) && (direction == SN_TO_LOCAL)) { /* AddIP with no association */
						/* try look up with the ASCONF packet's alternative address */
						ipv4addr.s_addr = ((struct sctp_ipv4addr_param *) param_hdr)->addr;
						*passoc = FindSctpGlobal(la, ipv4addr, sctp_hdr->v_tag, sctp_hdr->src_port, sctp_hdr->dest_port, &partial_match);
					}
					param_hdr = (struct sctp_paramhdr *)
						((char *) param_hdr + sizeof(struct sctp_ipv4addr_param)); /*asconf's compulsory address parameter */
					sm->chunk_length = chunk_length - sizeof(struct  sctp_asconf_chunk) - sizeof(struct  sctp_ipv4addr_param); /* rest of chunk */
				} else {
					if (chunk_length < (sizeof(struct  sctp_asconf_chunk) + sizeof(struct  sctp_ipv6addr_param))) /* malformed chunk*/
						return (SN_PARSE_ERROR_CHHL);
					param_hdr = (struct sctp_paramhdr *)
						((char *) param_hdr + sizeof(struct sctp_ipv6addr_param)); /*asconf's compulsory address parameter */
					sm->chunk_length = chunk_length - sizeof(struct  sctp_asconf_chunk) - sizeof(struct  sctp_ipv6addr_param); /* rest of chunk */
				}
				sm->msg = SN_SCTP_ASCONF;
				sm->sctpchnk.Asconf = param_hdr;

				if (*passoc == NULL) { /* AddIP with no association */
					*passoc = (struct sctp_nat_assoc *) sn_malloc(sizeof(struct sctp_nat_assoc));
					if (*passoc == NULL) {/* out of resources */
						return (SN_PARSE_ERROR_AS_MALLOC);
					}
					/* Initialize association  - sn_malloc initializes memory to zeros */
					(*passoc)->state = SN_ID;
					LIST_INIT(&((*passoc)->Gaddr)); /* always initialise to avoid memory problems */
					(*passoc)->TableRegister = SN_NULL_TBL;
					return (SN_PARSE_OK);
				}
			}
			break;
		case SCTP_ASCONF_ACK:
			if (sm->msg > SN_SCTP_ASCONFACK) {
				if (chunk_length < sizeof(struct  sctp_asconf_ack_chunk)) /* malformed chunk*/
					return (SN_PARSE_ERROR_CHHL);
				//leave parameter searching to later, if required
				param_hdr = (struct sctp_paramhdr *) ((char *) chunk_hdr
				    + sizeof(struct sctp_asconf_ack_chunk));
				sm->msg = SN_SCTP_ASCONFACK;
				sm->sctpchnk.Asconf = param_hdr;
				sm->chunk_length = chunk_length - sizeof(struct sctp_asconf_ack_chunk);
			}
			break;
		default:
			break; /* do nothing*/
		}

		/* if no association is found exit - we need to find an Init or AddIP within sysctl_initialising_chunk_proc_limit */
		if ((*passoc == NULL) && (chunk_count >= sysctl_initialising_chunk_proc_limit))
			return (SN_PARSE_ERROR_LOOKUP);

		/* finished with this chunk, on to the next chunk*/
		bytes_left-= chunk_length;

		/* Is this the end of the packet ? */
		if (bytes_left == 0)
			return (*passoc == NULL) ? (SN_PARSE_ERROR_LOOKUP) : (SN_PARSE_OK);

		/* Are there enough bytes in packet to at least retrieve length of next chunk ? */
		if (bytes_left < SN_MIN_CHUNK_SIZE)
			return (SN_PARSE_ERROR_CHHL);

		chunk_hdr = SN_SCTP_NEXTCHUNK(chunk_hdr);

		/* Is the chunk long enough to not cause endless look and are there enough bytes in packet to read the chunk ? */
		chunk_length = SCTP_SIZE32(ntohs(chunk_hdr->chunk_length));
		if ((chunk_length < SN_MIN_CHUNK_SIZE) || (chunk_length > bytes_left))
			return (SN_PARSE_ERROR_CHHL);
		if (++chunk_count > sysctl_chunk_proc_limit)
			return (SN_PARSE_OK); /* limit for processing chunks, take what we get */
	}

	if (*passoc == NULL)
		return (partial_match) ? (SN_PARSE_ERROR_PARTIALLOOKUP) : (SN_PARSE_ERROR_LOOKUP);
	else
		return (SN_PARSE_OK);
}

/** @ingroup packet_parser
 * @brief Extract Vtags from Asconf Chunk
 *
 * GetAsconfVtags scans an Asconf Chunk for the vtags parameter, and then
 * extracts the vtags.
 *
 * GetAsconfVtags is not called from within sctp_PktParser. It is called only
 * from within ID_process when an AddIP has been received.
 *
 * @param la Pointer to the relevant libalias instance
 * @param sm Pointer to sctp message information
 * @param l_vtag Pointer to the local vtag in the association this SCTP Message belongs to
 * @param g_vtag Pointer to the local vtag in the association this SCTP Message belongs to
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 * @return 1 - success | 0 - fail
 */
static int
GetAsconfVtags(struct libalias *la, struct sctp_nat_msg *sm, uint32_t *l_vtag, uint32_t *g_vtag, int direction)
{
	/* To be removed when information is in the sctp headers */
#define SCTP_VTAG_PARAM 0xC007
	struct sctp_vtag_param {
		struct sctp_paramhdr ph;/* type=SCTP_VTAG_PARAM */
		uint32_t local_vtag;
		uint32_t remote_vtag;
	}                    __attribute__((packed));

	struct sctp_vtag_param *vtag_param;
	struct sctp_paramhdr *param;
	int bytes_left;
	int param_size;
	int param_count;

	param_count = 1;
	param = sm->sctpchnk.Asconf;
	param_size = SCTP_SIZE32(ntohs(param->param_length));
	bytes_left = sm->chunk_length;
	/* step through Asconf parameters */
	while((bytes_left >= param_size) && (bytes_left >= SN_VTAG_PARAM_SIZE)) {
		if (ntohs(param->param_type) == SCTP_VTAG_PARAM) {
			vtag_param = (struct sctp_vtag_param *) param;
			switch (direction) {
				/* The Internet draft is a little ambigious as to order of these vtags.
				   We think it is this way around. If we are wrong, the order will need
				   to be changed. */
			case SN_TO_GLOBAL:
				*g_vtag = vtag_param->local_vtag;
				*l_vtag = vtag_param->remote_vtag;
				break;
			case SN_TO_LOCAL:
				*g_vtag = vtag_param->remote_vtag;
				*l_vtag = vtag_param->local_vtag;
				break;
			}
			return (1); /* found */
		}

		bytes_left -= param_size;
		if (bytes_left < SN_MIN_PARAM_SIZE) return (0);

		param = SN_SCTP_NEXTPARAM(param);
		param_size = SCTP_SIZE32(ntohs(param->param_length));
		if (++param_count > sysctl_param_proc_limit) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("Parameter parse limit exceeded (GetAsconfVtags)",
				sm->sctp_hdr->v_tag, sysctl_param_proc_limit, direction));
			return (0); /* not found limit exceeded*/
		}
	}
	return (0); /* not found */
}

/** @ingroup packet_parser
 * @brief AddGlobalIPAddresses from Init,InitAck,or AddIP packets
 *
 * AddGlobalIPAddresses scans an SCTP chunk (in sm) for Global IP addresses, and
 * adds them.
 *
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 */
static void
AddGlobalIPAddresses(struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc, int direction)
{
	struct sctp_ipv4addr_param *ipv4_param;
	struct sctp_paramhdr *param = NULL;
	struct sctp_GlobalAddress *G_Addr;
	struct in_addr g_addr = {0};
	int bytes_left = 0;
	int param_size;
	int param_count, addr_param_count = 0;

	switch (direction) {
	case SN_TO_GLOBAL: /* does not contain global addresses */
		g_addr = sm->ip_hdr->ip_dst;
		bytes_left = 0; /* force exit */
		break;
	case SN_TO_LOCAL:
		g_addr = sm->ip_hdr->ip_src;
		param_count = 1;
		switch (sm->msg) {
		case SN_SCTP_INIT:
			bytes_left = sm->chunk_length - sizeof(struct sctp_init_chunk);
			param = (struct sctp_paramhdr *)((char *)sm->sctpchnk.Init + sizeof(struct sctp_init));
			break;
		case SN_SCTP_INITACK:
			bytes_left = sm->chunk_length - sizeof(struct sctp_init_ack_chunk);
			param = (struct sctp_paramhdr *)((char *)sm->sctpchnk.InitAck + sizeof(struct sctp_init_ack));
			break;
		case SN_SCTP_ASCONF:
			bytes_left = sm->chunk_length;
			param = sm->sctpchnk.Asconf;
			break;
		}
	}
	if (bytes_left >= SN_MIN_PARAM_SIZE)
		param_size = SCTP_SIZE32(ntohs(param->param_length));
	else
		param_size = bytes_left+1; /* force skip loop */

	if ((assoc->state == SN_ID) && ((sm->msg == SN_SCTP_INIT) || (bytes_left < SN_MIN_PARAM_SIZE))) {/* add pkt address */
		G_Addr = (struct sctp_GlobalAddress *) sn_malloc(sizeof(struct sctp_GlobalAddress));
		if (G_Addr == NULL) {/* out of resources */
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("AddGlobalIPAddress: No resources for adding global address - revert to no tracking",
				sm->sctp_hdr->v_tag,  0, direction));
			assoc->num_Gaddr = 0; /* don't track any more for this assoc*/
			sysctl_track_global_addresses=0;
			return;
		}
		G_Addr->g_addr = g_addr;
		if (!Add_Global_Address_to_List(assoc, G_Addr))
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("AddGlobalIPAddress: Address already in list",
				sm->sctp_hdr->v_tag,  assoc->num_Gaddr, direction));
	}

	/* step through parameters */
	while((bytes_left >= param_size) && (bytes_left >= sizeof(struct sctp_ipv4addr_param))) {
		if (assoc->num_Gaddr >= sysctl_track_global_addresses) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("AddGlobalIPAddress: Maximum Number of addresses reached",
				sm->sctp_hdr->v_tag,  sysctl_track_global_addresses, direction));
			return;
		}
		switch (ntohs(param->param_type)) {
		case SCTP_ADD_IP_ADDRESS:
			/* skip to address parameter - leave param_size so bytes left will be calculated properly*/
			param = (struct sctp_paramhdr *) &((struct sctp_asconf_addrv4_param *) param)->addrp;
			/* FALLTHROUGH */
		case SCTP_IPV4_ADDRESS:
			ipv4_param = (struct sctp_ipv4addr_param *) param;
			/* add addresses to association */
			G_Addr = (struct sctp_GlobalAddress *) sn_malloc(sizeof(struct sctp_GlobalAddress));
			if (G_Addr == NULL) {/* out of resources */
				SN_LOG(SN_LOG_EVENT,
				    logsctperror("AddGlobalIPAddress: No resources for adding global address - revert to no tracking",
					sm->sctp_hdr->v_tag,  0, direction));
				assoc->num_Gaddr = 0; /* don't track any more for this assoc*/
				sysctl_track_global_addresses=0;
				return;
			}
			/* add address */
			addr_param_count++;
			if ((sm->msg == SN_SCTP_ASCONF) && (ipv4_param->addr == INADDR_ANY)) { /* use packet address */
				G_Addr->g_addr = g_addr;
				if (!Add_Global_Address_to_List(assoc, G_Addr))
					SN_LOG(SN_LOG_EVENT,
					    logsctperror("AddGlobalIPAddress: Address already in list",
						sm->sctp_hdr->v_tag,  assoc->num_Gaddr, direction));
				return; /*shouldn't be any other addresses if the zero address is given*/
			} else {
				G_Addr->g_addr.s_addr = ipv4_param->addr;
				if (!Add_Global_Address_to_List(assoc, G_Addr))
					SN_LOG(SN_LOG_EVENT,
					    logsctperror("AddGlobalIPAddress: Address already in list",
						sm->sctp_hdr->v_tag,  assoc->num_Gaddr, direction));
			}
		}

		bytes_left -= param_size;
		if (bytes_left < SN_MIN_PARAM_SIZE)
			break;

		param = SN_SCTP_NEXTPARAM(param);
		param_size = SCTP_SIZE32(ntohs(param->param_length));
		if (++param_count > sysctl_param_proc_limit) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("Parameter parse limit exceeded (AddGlobalIPAddress)",
				sm->sctp_hdr->v_tag, sysctl_param_proc_limit, direction));
			break; /* limit exceeded*/
		}
	}
	if (addr_param_count == 0) {
		SN_LOG(SN_LOG_DETAIL,
		    logsctperror("AddGlobalIPAddress: no address parameters to add",
			sm->sctp_hdr->v_tag, assoc->num_Gaddr, direction));
	}
}

/**
 * @brief Add_Global_Address_to_List
 *
 * Adds a global IP address to an associations address list, if it is not
 * already there.  The first address added us usually the packet's address, and
 * is most likely to be used, so it is added at the beginning. Subsequent
 * addresses are added after this one.
 *
 * @param assoc Pointer to the association this SCTP Message belongs to
 * @param G_addr Pointer to the global address to add
 *
 * @return 1 - success | 0 - fail
 */
static int  Add_Global_Address_to_List(struct sctp_nat_assoc *assoc,  struct sctp_GlobalAddress *G_addr)
{
	struct sctp_GlobalAddress *iter_G_Addr = NULL, *first_G_Addr = NULL;
	first_G_Addr = LIST_FIRST(&(assoc->Gaddr));
	if (first_G_Addr == NULL) {
		LIST_INSERT_HEAD(&(assoc->Gaddr), G_addr, list_Gaddr); /* add new address to beginning of list*/
	} else {
		LIST_FOREACH(iter_G_Addr, &(assoc->Gaddr), list_Gaddr) {
			if (G_addr->g_addr.s_addr == iter_G_Addr->g_addr.s_addr)
				return (0); /* already exists, so don't add */
		}
		LIST_INSERT_AFTER(first_G_Addr, G_addr, list_Gaddr); /* add address to end of list*/
	}
	assoc->num_Gaddr++;
	return (1); /* success */
}

/** @ingroup packet_parser
 * @brief RmGlobalIPAddresses from DelIP packets
 *
 * RmGlobalIPAddresses scans an ASCONF chunk for DelIP parameters to remove the
 * given Global IP addresses from the association. It will not delete the
 * the address if it is a list of one address.
 *
 *
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 */
static void
RmGlobalIPAddresses(struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc, int direction)
{
	struct sctp_asconf_addrv4_param *asconf_ipv4_param;
	struct sctp_paramhdr *param;
	struct sctp_GlobalAddress *G_Addr, *G_Addr_tmp;
	struct in_addr g_addr;
	int bytes_left;
	int param_size;
	int param_count;

	if (direction == SN_TO_GLOBAL)
		g_addr = sm->ip_hdr->ip_dst;
	else
		g_addr = sm->ip_hdr->ip_src;

	bytes_left = sm->chunk_length;
	param_count = 1;
	param = sm->sctpchnk.Asconf;
	if (bytes_left >= SN_MIN_PARAM_SIZE) {
		param_size = SCTP_SIZE32(ntohs(param->param_length));
	} else {
		SN_LOG(SN_LOG_EVENT,
		    logsctperror("RmGlobalIPAddress: truncated packet - cannot remove IP addresses",
			sm->sctp_hdr->v_tag, sysctl_track_global_addresses, direction));
		return;
	}

	/* step through Asconf parameters */
	while((bytes_left >= param_size) && (bytes_left >= sizeof(struct sctp_ipv4addr_param))) {
		if (ntohs(param->param_type) == SCTP_DEL_IP_ADDRESS) {
			asconf_ipv4_param = (struct sctp_asconf_addrv4_param *) param;
			if (asconf_ipv4_param->addrp.addr == INADDR_ANY) { /* remove all bar pkt address */
				LIST_FOREACH_SAFE(G_Addr, &(assoc->Gaddr), list_Gaddr, G_Addr_tmp) {
					if (G_Addr->g_addr.s_addr != sm->ip_hdr->ip_src.s_addr) {
						if (assoc->num_Gaddr > 1) { /* only delete if more than one */
							LIST_REMOVE(G_Addr, list_Gaddr);
							sn_free(G_Addr);
							assoc->num_Gaddr--;
						} else {
							SN_LOG(SN_LOG_EVENT,
							    logsctperror("RmGlobalIPAddress: Request to remove last IP address (didn't)",
								sm->sctp_hdr->v_tag, assoc->num_Gaddr, direction));
						}
					}
				}
				return; /*shouldn't be any other addresses if the zero address is given*/
			} else {
				LIST_FOREACH_SAFE(G_Addr, &(assoc->Gaddr), list_Gaddr, G_Addr_tmp) {
					if (G_Addr->g_addr.s_addr == asconf_ipv4_param->addrp.addr) {
						if (assoc->num_Gaddr > 1) { /* only delete if more than one */
							LIST_REMOVE(G_Addr, list_Gaddr);
							sn_free(G_Addr);
							assoc->num_Gaddr--;
							break; /* Since add only adds new addresses, there should be no double entries */
						} else {
							SN_LOG(SN_LOG_EVENT,
							    logsctperror("RmGlobalIPAddress: Request to remove last IP address (didn't)",
								sm->sctp_hdr->v_tag, assoc->num_Gaddr, direction));
						}
					}
				}
			}
		}
		bytes_left -= param_size;
		if (bytes_left == 0) return;
		else if (bytes_left < SN_MIN_PARAM_SIZE) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("RmGlobalIPAddress: truncated packet - may not have removed all IP addresses",
				sm->sctp_hdr->v_tag, sysctl_track_global_addresses, direction));
			return;
		}

		param = SN_SCTP_NEXTPARAM(param);
		param_size = SCTP_SIZE32(ntohs(param->param_length));
		if (++param_count > sysctl_param_proc_limit) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("Parameter parse limit exceeded (RmGlobalIPAddress)",
				sm->sctp_hdr->v_tag, sysctl_param_proc_limit, direction));
			return; /* limit exceeded*/
		}
	}
}

/**  @ingroup packet_parser
 * @brief Check that ASCONF was successful
 *
 * Each ASCONF configuration parameter carries a correlation ID which should be
 * matched with an ASCONFack. This is difficult for a NAT, since every
 * association could potentially have a number of outstanding ASCONF
 * configuration parameters, which should only be activated on receipt of the
 * ACK.
 *
 * Currently we only look for an ACK when the NAT is setting up a new
 * association (ie AddIP for a connection that the NAT does not know about
 * because the original Init went through a public interface or another NAT)
 * Since there is currently no connection on this path, there should be no other
 * ASCONF configuration parameters outstanding, so we presume that if there is
 * an ACK that it is responding to the AddIP and activate the new association.
 *
 * @param la Pointer to the relevant libalias instance
 * @param sm Pointer to sctp message information
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 * @return 1 - success | 0 - fail
 */
static int
IsASCONFack(struct libalias *la, struct sctp_nat_msg *sm, int direction)
{
	struct sctp_paramhdr *param;
	int bytes_left;
	int param_size;
	int param_count;

	param_count = 1;
	param = sm->sctpchnk.Asconf;
	param_size = SCTP_SIZE32(ntohs(param->param_length));
	if (param_size == 8)
		return (1); /*success - default acknowledgement of everything */

	bytes_left = sm->chunk_length;
	if (bytes_left < param_size)
		return (0); /* not found */
	/* step through Asconf parameters */
	while(bytes_left >= SN_ASCONFACK_PARAM_SIZE) {
		if (ntohs(param->param_type) == SCTP_SUCCESS_REPORT)
			return (1); /* success - but can't match correlation IDs - should only be one */
		/* check others just in case */
		bytes_left -= param_size;
		if (bytes_left >= SN_MIN_PARAM_SIZE) {
			param = SN_SCTP_NEXTPARAM(param);
		} else {
			return (0);
		}
		param_size = SCTP_SIZE32(ntohs(param->param_length));
		if (bytes_left < param_size) return (0);

		if (++param_count > sysctl_param_proc_limit) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("Parameter parse limit exceeded (IsASCONFack)",
				sm->sctp_hdr->v_tag, sysctl_param_proc_limit, direction));
			return (0); /* not found limit exceeded*/
		}
	}
	return (0); /* not success */
}

/**  @ingroup packet_parser
 * @brief Check to see if ASCONF contains an Add IP or Del IP parameter
 *
 * IsADDorDEL scans an ASCONF packet to see if it contains an AddIP or DelIP
 * parameter
 *
 * @param la Pointer to the relevant libalias instance
 * @param sm Pointer to sctp message information
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 *
 * @return SCTP_ADD_IP_ADDRESS | SCTP_DEL_IP_ADDRESS | 0 - fail
 */
static int
IsADDorDEL(struct libalias *la, struct sctp_nat_msg *sm, int direction)
{
	struct sctp_paramhdr *param;
	int bytes_left;
	int param_size;
	int param_count;

	param_count = 1;
	param = sm->sctpchnk.Asconf;
	param_size = SCTP_SIZE32(ntohs(param->param_length));

	bytes_left = sm->chunk_length;
	if (bytes_left < param_size)
		return (0); /* not found */
	/* step through Asconf parameters */
	while(bytes_left >= SN_ASCONFACK_PARAM_SIZE) {
		if (ntohs(param->param_type) == SCTP_ADD_IP_ADDRESS)
			return (SCTP_ADD_IP_ADDRESS);
		else if (ntohs(param->param_type) == SCTP_DEL_IP_ADDRESS)
			return (SCTP_DEL_IP_ADDRESS);
		/* check others just in case */
		bytes_left -= param_size;
		if (bytes_left >= SN_MIN_PARAM_SIZE) {
			param = SN_SCTP_NEXTPARAM(param);
		} else {
			return (0); /*Neither found */
		}
		param_size = SCTP_SIZE32(ntohs(param->param_length));
		if (bytes_left < param_size) return (0);

		if (++param_count > sysctl_param_proc_limit) {
			SN_LOG(SN_LOG_EVENT,
			    logsctperror("Parameter parse limit exceeded IsADDorDEL)",
				sm->sctp_hdr->v_tag, sysctl_param_proc_limit, direction));
			return (0); /* not found limit exceeded*/
		}
	}
	return (0);  /*Neither found */
}

/* ----------------------------------------------------------------------
 *                            STATE MACHINE CODE
 * ----------------------------------------------------------------------
 */
/** @addtogroup state_machine
 *
 * The SCTP NAT State Machine functions will:
 * - Process an already parsed packet
 * - Use the existing NAT Hash Tables
 * - Determine the next state for the association
 * - Update the NAT Hash Tables and Timer Queues
 * - Return the appropriate action to take with the packet
 */
/** @ingroup state_machine
 * @brief Process SCTP message
 *
 * This function is the base state machine. It calls the processing engine for
 * each state.
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_DROP_PKT | SN_NAT_PKT | SN_REPLY_ABORT | SN_REPLY_ERROR | SN_PROCESSING_ERROR
 */
static int
ProcessSctpMsg(struct libalias *la, int direction, struct sctp_nat_msg *sm, struct sctp_nat_assoc *assoc)
{
	int rtnval;

	switch (assoc->state) {
	case SN_ID: /* Idle */
		rtnval = ID_process(la, direction, assoc, sm);
		if (rtnval != SN_NAT_PKT) {
			assoc->state = SN_RM;/* Mark for removal*/
		}
		return (rtnval);
	case SN_INi: /* Initialising - Init */
		return (INi_process(la, direction, assoc, sm));
	case SN_INa: /* Initialising - AddIP */
		return (INa_process(la, direction, assoc, sm));
	case SN_UP:  /* Association UP */
		return (UP_process(la, direction, assoc, sm));
	case SN_CL:  /* Association Closing */
		return (CL_process(la, direction, assoc, sm));
	}
	return (SN_PROCESSING_ERROR);
}

/** @ingroup state_machine
 * @brief Process SCTP message while in the Idle state
 *
 * This function looks for an Incoming INIT or AddIP message.
 *
 * All other SCTP messages are invalid when in SN_ID, and are dropped.
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_NAT_PKT | SN_DROP_PKT | SN_REPLY_ABORT | SN_REPLY_ERROR
 */
static int
ID_process(struct libalias *la, int direction, struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm)
{
	switch (sm->msg) {
	case SN_SCTP_ASCONF:           /* a packet containing an ASCONF chunk with ADDIP */
		if (!sysctl_accept_global_ootb_addip && (direction == SN_TO_LOCAL))
			return (SN_DROP_PKT);
		/* if this Asconf packet does not contain the Vtag parameters it is of no use in Idle state */
		if (!GetAsconfVtags(la, sm, &(assoc->l_vtag), &(assoc->g_vtag), direction))
			return (SN_DROP_PKT);
		/* FALLTHROUGH */
	case SN_SCTP_INIT:            /* a packet containing an INIT chunk or an ASCONF AddIP */
		if (sysctl_track_global_addresses)
			AddGlobalIPAddresses(sm, assoc, direction);
		switch (direction) {
		case SN_TO_GLOBAL:
			assoc->l_addr = sm->ip_hdr->ip_src;
			assoc->a_addr = FindAliasAddress(la, assoc->l_addr);
			assoc->l_port = sm->sctp_hdr->src_port;
			assoc->g_port = sm->sctp_hdr->dest_port;
			if (sm->msg == SN_SCTP_INIT)
				assoc->g_vtag = sm->sctpchnk.Init->initiate_tag;
			if (AddSctpAssocGlobal(la, assoc)) /* DB clash *///**** need to add dst address
				return ((sm->msg == SN_SCTP_INIT) ? SN_REPLY_ABORT : SN_REPLY_ERROR);
			if (sm->msg == SN_SCTP_ASCONF) {
				if (AddSctpAssocLocal(la, assoc, sm->ip_hdr->ip_dst)) /* DB clash */
					return (SN_REPLY_ERROR);
				assoc->TableRegister |= SN_WAIT_TOLOCAL; /* wait for tolocal ack */
			}
		break;
		case SN_TO_LOCAL:
			assoc->l_addr = FindSctpRedirectAddress(la, sm);
			assoc->a_addr = sm->ip_hdr->ip_dst;
			assoc->l_port = sm->sctp_hdr->dest_port;
			assoc->g_port = sm->sctp_hdr->src_port;
			if (sm->msg == SN_SCTP_INIT)
				assoc->l_vtag = sm->sctpchnk.Init->initiate_tag;
			if (AddSctpAssocLocal(la, assoc, sm->ip_hdr->ip_src)) /* DB clash */
				return ((sm->msg == SN_SCTP_INIT) ? SN_REPLY_ABORT : SN_REPLY_ERROR);
			if (sm->msg == SN_SCTP_ASCONF) {
				if (AddSctpAssocGlobal(la, assoc)) /* DB clash */ //**** need to add src address
					return (SN_REPLY_ERROR);
				assoc->TableRegister |= SN_WAIT_TOGLOBAL; /* wait for toglobal ack */
					}
			break;
		}
		assoc->state = (sm->msg == SN_SCTP_INIT) ? SN_INi : SN_INa;
		assoc->exp = SN_I_T(la);
		sctp_AddTimeOut(la,assoc);
		return (SN_NAT_PKT);
	default: /* Any other type of SCTP message is not valid in Idle */
		return (SN_DROP_PKT);
	}
	return (SN_DROP_PKT);/* shouldn't get here very bad: log, drop and hope for the best */
}

/** @ingroup state_machine
 * @brief Process SCTP message while waiting for an INIT-ACK message
 *
 * Only an INIT-ACK, resent INIT, or an ABORT SCTP packet are valid in this
 * state, all other packets are dropped.
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_NAT_PKT | SN_DROP_PKT | SN_REPLY_ABORT
 */
static int
INi_process(struct libalias *la, int direction, struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm)
{
	switch (sm->msg) {
	case SN_SCTP_INIT:            /* a packet containing a retransmitted INIT chunk */
		sctp_ResetTimeOut(la, assoc, SN_I_T(la));
		return (SN_NAT_PKT);
	case SN_SCTP_INITACK:         /* a packet containing an INIT-ACK chunk */
		switch (direction) {
		case SN_TO_LOCAL:
			if (assoc->num_Gaddr) /*If tracking global addresses for this association */
				AddGlobalIPAddresses(sm, assoc, direction);
			assoc->l_vtag = sm->sctpchnk.Init->initiate_tag;
			if (AddSctpAssocLocal(la, assoc, sm->ip_hdr->ip_src)) { /* DB clash */
				assoc->state = SN_RM;/* Mark for removal*/
				return (SN_SEND_ABORT);
			}
			break;
		case SN_TO_GLOBAL:
			assoc->l_addr = sm->ip_hdr->ip_src; // Only if not set in Init! *
			assoc->g_vtag = sm->sctpchnk.Init->initiate_tag;
			if (AddSctpAssocGlobal(la, assoc)) { /* DB clash */
				assoc->state = SN_RM;/* Mark for removal*/
				return (SN_SEND_ABORT);
			}
			break;
		}
		assoc->state = SN_UP;/* association established for NAT */
		sctp_ResetTimeOut(la,assoc, SN_U_T(la));
		return (SN_NAT_PKT);
	case SN_SCTP_ABORT:           /* a packet containing an ABORT chunk */
		assoc->state = SN_RM;/* Mark for removal*/
		return (SN_NAT_PKT);
	default:
		return (SN_DROP_PKT);
	}
	return (SN_DROP_PKT);/* shouldn't get here very bad: log, drop and hope for the best */
}

/** @ingroup state_machine
 * @brief Process SCTP message while waiting for an AddIp-ACK message
 *
 * Only an AddIP-ACK, resent AddIP, or an ABORT message are valid, all other
 * SCTP packets are dropped
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_NAT_PKT | SN_DROP_PKT
 */
static int
INa_process(struct libalias *la, int direction,struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm)
{
	switch (sm->msg) {
	case SN_SCTP_ASCONF:           /* a packet containing an ASCONF chunk*/
		sctp_ResetTimeOut(la,assoc, SN_I_T(la));
		return (SN_NAT_PKT);
	case SN_SCTP_ASCONFACK:        /* a packet containing an ASCONF chunk with a ADDIP-ACK */
		switch (direction) {
		case SN_TO_LOCAL:
			if (!(assoc->TableRegister & SN_WAIT_TOLOCAL)) /* wrong direction */
				return (SN_DROP_PKT);
			break;
		case SN_TO_GLOBAL:
			if (!(assoc->TableRegister & SN_WAIT_TOGLOBAL)) /* wrong direction */
				return (SN_DROP_PKT);
		}
		if (IsASCONFack(la,sm,direction)) {
			assoc->TableRegister &= SN_BOTH_TBL; /* remove wait flags */
			assoc->state = SN_UP; /* association established for NAT */
			sctp_ResetTimeOut(la,assoc, SN_U_T(la));
			return (SN_NAT_PKT);
		} else {
			assoc->state = SN_RM;/* Mark for removal*/
			return (SN_NAT_PKT);
		}
	case SN_SCTP_ABORT:           /* a packet containing an ABORT chunk */
		assoc->state = SN_RM;/* Mark for removal*/
		return (SN_NAT_PKT);
	default:
		return (SN_DROP_PKT);
	}
	return (SN_DROP_PKT);/* shouldn't get here very bad: log, drop and hope for the best */
}

/** @ingroup state_machine
 * @brief Process SCTP messages while association is UP redirecting packets
 *
 * While in the SN_UP state, all packets for the particular association
 * are passed. Only a SHUT-ACK or an ABORT will cause a change of state.
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_NAT_PKT | SN_DROP_PKT
 */
static int
UP_process(struct libalias *la, int direction, struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm)
{
	switch (sm->msg) {
	case SN_SCTP_SHUTACK:         /* a packet containing a SHUTDOWN-ACK chunk */
		assoc->state = SN_CL;
		sctp_ResetTimeOut(la,assoc, SN_C_T(la));
		return (SN_NAT_PKT);
	case SN_SCTP_ABORT:           /* a packet containing an ABORT chunk */
		assoc->state = SN_RM;/* Mark for removal*/
		return (SN_NAT_PKT);
	case SN_SCTP_ASCONF:           /* a packet containing an ASCONF chunk*/
		if ((direction == SN_TO_LOCAL) && assoc->num_Gaddr) /*If tracking global addresses for this association & from global side */
			switch (IsADDorDEL(la,sm,direction)) {
			case SCTP_ADD_IP_ADDRESS:
				AddGlobalIPAddresses(sm, assoc, direction);
				break;
			case SCTP_DEL_IP_ADDRESS:
				RmGlobalIPAddresses(sm, assoc, direction);
				break;
			} /* fall through to default */
	default:
		sctp_ResetTimeOut(la,assoc, SN_U_T(la));
		return (SN_NAT_PKT);  /* forward packet */
	}
	return (SN_DROP_PKT);/* shouldn't get here very bad: log, drop and hope for the best */
}

/** @ingroup state_machine
 * @brief Process SCTP message while association is in the process of closing
 *
 * This function waits for a SHUT-COMP to close the association. Depending on
 * the setting of sysctl_holddown_timer it may not remove the association
 * immediately, but leave it up until SN_X_T(la). Only SHUT-COMP, SHUT-ACK, and
 * ABORT packets are permitted in this state. All other packets are dropped.
 *
 * @param la Pointer to the relevant libalias instance
 * @param direction SN_TO_LOCAL | SN_TO_GLOBAL
 * @param sm Pointer to sctp message information
 * @param assoc Pointer to the association this SCTP Message belongs to
 *
 * @return SN_NAT_PKT | SN_DROP_PKT
 */
static int
CL_process(struct libalias *la, int direction,struct sctp_nat_assoc *assoc, struct sctp_nat_msg *sm)
{
	switch (sm->msg) {
	case SN_SCTP_SHUTCOMP:        /* a packet containing a SHUTDOWN-COMPLETE chunk */
		assoc->state = SN_CL;  /* Stay in Close state until timeout */
		if (sysctl_holddown_timer > 0)
			sctp_ResetTimeOut(la, assoc, SN_X_T(la));/* allow to stay open for Tbit packets*/
		else
			assoc->state = SN_RM;/* Mark for removal*/
		return (SN_NAT_PKT);
	case SN_SCTP_SHUTACK:         /* a packet containing a SHUTDOWN-ACK chunk */
		assoc->state = SN_CL;  /* Stay in Close state until timeout */
		sctp_ResetTimeOut(la, assoc, SN_C_T(la));
		return (SN_NAT_PKT);
	case SN_SCTP_ABORT:           /* a packet containing an ABORT chunk */
		assoc->state = SN_RM;/* Mark for removal*/
		return (SN_NAT_PKT);
	default:
		return (SN_DROP_PKT);
	}
	return (SN_DROP_PKT);/* shouldn't get here very bad: log, drop and hope for the best */
}

/* ----------------------------------------------------------------------
 *                           HASH TABLE CODE
 * ----------------------------------------------------------------------
 */
/** @addtogroup Hash
 *
 * The Hash functions facilitate searching the NAT Hash Tables for associations
 * as well as adding/removing associations from the table(s).
 */
/** @ingroup Hash
 * @brief Find the SCTP association given the local address, port and vtag
 *
 * Searches the local look-up table for the association entry matching the
 * provided local <address:ports:vtag> tuple
 *
 * @param la Pointer to the relevant libalias instance
 * @param l_addr local address
 * @param g_addr global address
 * @param l_vtag local Vtag
 * @param l_port local Port
 * @param g_port global Port
 *
 * @return pointer to association or NULL
 */
static struct sctp_nat_assoc*
FindSctpLocal(struct libalias *la, struct in_addr l_addr, struct in_addr g_addr, uint32_t l_vtag, uint16_t l_port, uint16_t g_port)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;
	struct sctp_GlobalAddress *G_Addr = NULL;

	if (l_vtag != 0) { /* an init packet, vtag==0 */
		i = SN_TABLE_HASH(l_vtag, l_port, la->sctpNatTableSize);
		LIST_FOREACH(assoc, &la->sctpTableLocal[i], list_L) {
			if ((assoc->l_vtag == l_vtag) && (assoc->l_port == l_port) && (assoc->g_port == g_port)\
			    && (assoc->l_addr.s_addr == l_addr.s_addr)) {
				if (assoc->num_Gaddr) {
					LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
						if (G_Addr->g_addr.s_addr == g_addr.s_addr)
							return (assoc);
					}
				} else {
					return (assoc);
				}
			}
		}
	}
	return (NULL);
}

/** @ingroup Hash
 * @brief Check for Global Clash
 *
 * Searches the global look-up table for the association entry matching the
 * provided global <(addresses):ports:vtag> tuple
 *
 * @param la Pointer to the relevant libalias instance
 * @param Cassoc association being checked for a clash
 *
 * @return pointer to association or NULL
 */
static struct sctp_nat_assoc*
FindSctpGlobalClash(struct libalias *la,  struct sctp_nat_assoc *Cassoc)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;
	struct sctp_GlobalAddress *G_Addr = NULL;
	struct sctp_GlobalAddress *G_AddrC = NULL;

	if (Cassoc->g_vtag != 0) { /* an init packet, vtag==0 */
		i = SN_TABLE_HASH(Cassoc->g_vtag, Cassoc->g_port, la->sctpNatTableSize);
		LIST_FOREACH(assoc, &la->sctpTableGlobal[i], list_G) {
			if ((assoc->g_vtag == Cassoc->g_vtag) && (assoc->g_port == Cassoc->g_port) && (assoc->l_port == Cassoc->l_port)) {
				if (assoc->num_Gaddr) {
					LIST_FOREACH(G_AddrC, &(Cassoc->Gaddr), list_Gaddr) {
						LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
							if (G_Addr->g_addr.s_addr == G_AddrC->g_addr.s_addr)
								return (assoc);
						}
					}
				} else {
					return (assoc);
				}
			}
		}
	}
	return (NULL);
}

/** @ingroup Hash
 * @brief Find the SCTP association given the global port and vtag
 *
 * Searches the global look-up table for the association entry matching the
 * provided global <address:ports:vtag> tuple
 *
 * If all but the global address match it sets partial_match to 1 to indicate a
 * partial match. If the NAT is tracking global IP addresses for this
 * association, the NAT may respond with an ERRORM to request the missing
 * address to be added.
 *
 * @param la Pointer to the relevant libalias instance
 * @param g_addr global address
 * @param g_vtag global vtag
 * @param g_port global port
 * @param l_port local port
 *
 * @return pointer to association or NULL
 */
static struct sctp_nat_assoc*
FindSctpGlobal(struct libalias *la, struct in_addr g_addr, uint32_t g_vtag, uint16_t g_port, uint16_t l_port, int *partial_match)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;
	struct sctp_GlobalAddress *G_Addr = NULL;

	*partial_match = 0;
	if (g_vtag != 0) { /* an init packet, vtag==0 */
		i = SN_TABLE_HASH(g_vtag, g_port, la->sctpNatTableSize);
		LIST_FOREACH(assoc, &la->sctpTableGlobal[i], list_G) {
			if ((assoc->g_vtag == g_vtag) && (assoc->g_port == g_port) && (assoc->l_port == l_port)) {
				*partial_match = 1;
				if (assoc->num_Gaddr) {
					LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
						if (G_Addr->g_addr.s_addr == g_addr.s_addr)
							return (assoc);
					}
				} else {
					return (assoc);
				}
			}
		}
	}
	return (NULL);
}

/** @ingroup Hash
 * @brief Find the SCTP association for a T-Flag message (given the global port and local vtag)
 *
 * Searches the local look-up table for a unique association entry matching the
 * provided global port and local vtag information
 *
 * @param la Pointer to the relevant libalias instance
 * @param g_addr global address
 * @param l_vtag local Vtag
 * @param g_port global Port
 * @param l_port local Port
 *
 * @return pointer to association or NULL
 */
static struct sctp_nat_assoc*
FindSctpLocalT(struct libalias *la, struct in_addr g_addr, uint32_t l_vtag, uint16_t g_port, uint16_t l_port)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL, *lastmatch = NULL;
	struct sctp_GlobalAddress *G_Addr = NULL;
	int cnt = 0;

	if (l_vtag != 0) { /* an init packet, vtag==0 */
		i = SN_TABLE_HASH(l_vtag, g_port, la->sctpNatTableSize);
		LIST_FOREACH(assoc, &la->sctpTableGlobal[i], list_G) {
			if ((assoc->g_vtag == l_vtag) && (assoc->g_port == g_port) && (assoc->l_port == l_port)) {
				if (assoc->num_Gaddr) {
					LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
						if (G_Addr->g_addr.s_addr == g_addr.s_addr)
							return (assoc); /* full match */
					}
				} else {
					if (++cnt > 1) return (NULL);
					lastmatch = assoc;
				}
			}
		}
	}
	/* If there is more than one match we do not know which local address to send to */
	return (cnt ? lastmatch : NULL);
}

/** @ingroup Hash
 * @brief Find the SCTP association for a T-Flag message (given the local port and global vtag)
 *
 * Searches the global look-up table for a unique association entry matching the
 * provided local port and global vtag information
 *
 * @param la Pointer to the relevant libalias instance
 * @param g_addr global address
 * @param g_vtag global vtag
 * @param l_port local port
 * @param g_port global port
 *
 * @return pointer to association or NULL
 */
static struct sctp_nat_assoc*
FindSctpGlobalT(struct libalias *la, struct in_addr g_addr, uint32_t g_vtag, uint16_t l_port, uint16_t g_port)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;
	struct sctp_GlobalAddress *G_Addr = NULL;

	if (g_vtag != 0) { /* an init packet, vtag==0 */
		i = SN_TABLE_HASH(g_vtag, l_port, la->sctpNatTableSize);
		LIST_FOREACH(assoc, &la->sctpTableLocal[i], list_L) {
			if ((assoc->l_vtag == g_vtag) && (assoc->l_port == l_port) && (assoc->g_port == g_port)) {
				if (assoc->num_Gaddr) {
					LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
						if (G_Addr->g_addr.s_addr == g_addr.s_addr)
							return (assoc);
					}
				} else {
					return (assoc);
				}
			}
		}
	}
	return (NULL);
}

/** @ingroup Hash
 * @brief  Add the sctp association information to the local look up table
 *
 * Searches the local look-up table for an existing association with the same
 * details. If a match exists and is ONLY in the local look-up table then this
 * is a repeated INIT packet, we need to remove this association from the
 * look-up table and add the new association
 *
 * The new association is added to the head of the list and state is updated
 *
 * @param la Pointer to the relevant libalias instance
 * @param assoc pointer to sctp association
 * @param g_addr global address
 *
 * @return SN_ADD_OK | SN_ADD_CLASH
 */
static int
AddSctpAssocLocal(struct libalias *la, struct sctp_nat_assoc *assoc, struct in_addr g_addr)
{
	struct sctp_nat_assoc *found;

	LIBALIAS_LOCK_ASSERT(la);
	found = FindSctpLocal(la, assoc->l_addr, g_addr, assoc->l_vtag, assoc->l_port, assoc->g_port);
	/*
	 * Note that if a different global address initiated this Init,
	 * ie it wasn't resent as presumed:
	 *  - the local receiver if receiving it for the first time will establish
	 *    an association with the new global host
	 *  - if receiving an init from a different global address after sending a
	 *    lost initack it will send an initack to the new global host, the first
	 *    association attempt will then be blocked if retried.
	 */
	if (found != NULL) {
		if ((found->TableRegister == SN_LOCAL_TBL) && (found->g_port == assoc->g_port)) { /* resent message */
			RmSctpAssoc(la, found);
			sctp_RmTimeOut(la, found);
			freeGlobalAddressList(found);
			sn_free(found);
		} else
			return (SN_ADD_CLASH);
	}

	LIST_INSERT_HEAD(&la->sctpTableLocal[SN_TABLE_HASH(assoc->l_vtag, assoc->l_port, la->sctpNatTableSize)],
	    assoc, list_L);
	assoc->TableRegister |= SN_LOCAL_TBL;
	la->sctpLinkCount++; //increment link count

	if (assoc->TableRegister == SN_BOTH_TBL) {
		/* libalias log -- controlled by libalias */
		if (la->packetAliasMode & PKT_ALIAS_LOG)
			SctpShowAliasStats(la);

		SN_LOG(SN_LOG_INFO, logsctpassoc(assoc, "^"));
	}

	return (SN_ADD_OK);
}

/** @ingroup Hash
 * @brief  Add the sctp association information to the global look up table
 *
 * Searches the global look-up table for an existing association with the same
 * details. If a match exists and is ONLY in the global look-up table then this
 * is a repeated INIT packet, we need to remove this association from the
 * look-up table and add the new association
 *
 * The new association is added to the head of the list and state is updated
 *
 * @param la Pointer to the relevant libalias instance
 * @param assoc pointer to sctp association
 *
 * @return SN_ADD_OK | SN_ADD_CLASH
 */
static int
AddSctpAssocGlobal(struct libalias *la, struct sctp_nat_assoc *assoc)
{
	struct sctp_nat_assoc *found;

	LIBALIAS_LOCK_ASSERT(la);
	found = FindSctpGlobalClash(la, assoc);
	if (found != NULL) {
		if ((found->TableRegister == SN_GLOBAL_TBL) &&			\
		    (found->l_addr.s_addr == assoc->l_addr.s_addr) && (found->l_port == assoc->l_port)) { /* resent message */
			RmSctpAssoc(la, found);
			sctp_RmTimeOut(la, found);
			freeGlobalAddressList(found);
			sn_free(found);
		} else
			return (SN_ADD_CLASH);
	}

	LIST_INSERT_HEAD(&la->sctpTableGlobal[SN_TABLE_HASH(assoc->g_vtag, assoc->g_port, la->sctpNatTableSize)],
	    assoc, list_G);
	assoc->TableRegister |= SN_GLOBAL_TBL;
	la->sctpLinkCount++; //increment link count

	if (assoc->TableRegister == SN_BOTH_TBL) {
		/* libalias log -- controlled by libalias */
		if (la->packetAliasMode & PKT_ALIAS_LOG)
			SctpShowAliasStats(la);

		SN_LOG(SN_LOG_INFO, logsctpassoc(assoc, "^"));
	}

	return (SN_ADD_OK);
}

/** @ingroup Hash
 * @brief Remove the sctp association information from the look up table
 *
 * For each of the two (local/global) look-up tables, remove the association
 * from that table IF it has been registered in that table.
 *
 * NOTE: The calling code is responsible for freeing memory allocated to the
 *       association structure itself
 *
 * NOTE: The association is NOT removed from the timer queue
 *
 * @param la Pointer to the relevant libalias instance
 * @param assoc pointer to sctp association
 */
static void
RmSctpAssoc(struct libalias *la, struct sctp_nat_assoc *assoc)
{
	//  struct sctp_nat_assoc *found;
	if (assoc == NULL) {
		/* very bad, log and die*/
		SN_LOG(SN_LOG_LOW,
		    logsctperror("ERROR: alias_sctp:RmSctpAssoc(NULL)\n", 0, 0, SN_TO_NODIR));
		return;
	}
	/* log if association is fully up and now closing */
	if (assoc->TableRegister == SN_BOTH_TBL) {
		SN_LOG(SN_LOG_INFO, logsctpassoc(assoc, "$"));
	}
	LIBALIAS_LOCK_ASSERT(la);
	if (assoc->TableRegister & SN_LOCAL_TBL) {
		assoc->TableRegister ^= SN_LOCAL_TBL;
		la->sctpLinkCount--; //decrement link count
		LIST_REMOVE(assoc, list_L);
	}

	if (assoc->TableRegister & SN_GLOBAL_TBL) {
		assoc->TableRegister ^= SN_GLOBAL_TBL;
		la->sctpLinkCount--; //decrement link count
		LIST_REMOVE(assoc, list_G);
	}
	//  sn_free(assoc); //Don't remove now, remove if needed later
	/* libalias logging -- controlled by libalias log definition */
	if (la->packetAliasMode & PKT_ALIAS_LOG)
		SctpShowAliasStats(la);
}

/**
 * @ingroup Hash
 * @brief  free the Global Address List memory
 *
 * freeGlobalAddressList deletes all global IP addresses in an associations
 * global IP address list.
 *
 * @param assoc
 */
static void freeGlobalAddressList(struct sctp_nat_assoc *assoc)
{
	struct sctp_GlobalAddress *gaddr1=NULL,*gaddr2=NULL;
	/*free global address list*/
	gaddr1 = LIST_FIRST(&(assoc->Gaddr));
	while (gaddr1 != NULL) {
		gaddr2 = LIST_NEXT(gaddr1, list_Gaddr);
		sn_free(gaddr1);
		gaddr1 = gaddr2;
	}
}
/* ----------------------------------------------------------------------
 *                            TIMER QUEUE CODE
 * ----------------------------------------------------------------------
 */
/** @addtogroup Timer
 *
 * The timer queue management functions are designed to operate efficiently with
 * a minimum of interaction with the queues.
 *
 * Once a timeout is set in the queue it will not be altered in the queue unless
 * it has to be changed to a shorter time (usually only for aborts and closing).
 * On a queue timeout, the real expiry time is checked, and if not leq than the
 * timeout it is requeued (O(1)) at its later time. This is especially important
 * for normal packets sent during an association. When a timer expires, it is
 * updated to its new expiration time if necessary, or processed as a
 * timeout. This means that while in UP state, the timing queue is only altered
 * every U_T (every few minutes) for a particular association.
 */
/** @ingroup Timer
 * @brief Add an association timeout to the timer queue
 *
 * Determine the location in the queue to add the timeout and insert the
 * association into the list at that queue position
 *
 * @param la
 * @param assoc
 */
static void
sctp_AddTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc)
{
	int add_loc;
	LIBALIAS_LOCK_ASSERT(la);
	add_loc = assoc->exp - la->sctpNatTimer.loc_time + la->sctpNatTimer.cur_loc;
	if (add_loc >= SN_TIMER_QUEUE_SIZE)
		add_loc -= SN_TIMER_QUEUE_SIZE;
	LIST_INSERT_HEAD(&la->sctpNatTimer.TimerQ[add_loc], assoc, timer_Q);
	assoc->exp_loc = add_loc;
}

/** @ingroup Timer
 * @brief Remove an association from timer queue
 *
 * This is an O(1) operation to remove the association pointer from its
 * current position in the timer queue
 *
 * @param la Pointer to the relevant libalias instance
 * @param assoc pointer to sctp association
 */
static void
sctp_RmTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc)
{
	LIBALIAS_LOCK_ASSERT(la);
	LIST_REMOVE(assoc, timer_Q);/* Note this is O(1) */
}


/** @ingroup Timer
 * @brief Reset timer in timer queue
 *
 * Reset the actual timeout for the specified association. If it is earlier than
 * the existing timeout, then remove and re-install the association into the
 * queue
 *
 * @param la Pointer to the relevant libalias instance
 * @param assoc pointer to sctp association
 * @param newexp New expiration time
 */
static void
sctp_ResetTimeOut(struct libalias *la, struct sctp_nat_assoc *assoc, int newexp)
{
	if (newexp < assoc->exp) {
		sctp_RmTimeOut(la, assoc);
		assoc->exp = newexp;
		sctp_AddTimeOut(la, assoc);
	} else {
		assoc->exp = newexp;
	}
}

/** @ingroup Timer
 * @brief Check timer Q against current time
 *
 * Loop through each entry in the timer queue since the last time we processed
 * the timer queue until now (the current time). For each association in the
 * event list, we remove it from that position in the timer queue and check if
 * it has really expired. If so we:
 * - Log the timer expiry
 * - Remove the association from the NAT tables
 * - Release the memory used by the association
 *
 * If the timer hasn't really expired we place the association into its new
 * correct position in the timer queue.
 *
 * @param la  Pointer to the relevant libalias instance
 */
void
sctp_CheckTimers(struct libalias *la)
{
	struct sctp_nat_assoc *assoc;

	LIBALIAS_LOCK_ASSERT(la);
	while(la->timeStamp >= la->sctpNatTimer.loc_time) {
		while (!LIST_EMPTY(&la->sctpNatTimer.TimerQ[la->sctpNatTimer.cur_loc])) {
			assoc = LIST_FIRST(&la->sctpNatTimer.TimerQ[la->sctpNatTimer.cur_loc]);
			//SLIST_REMOVE_HEAD(&la->sctpNatTimer.TimerQ[la->sctpNatTimer.cur_loc], timer_Q);
			LIST_REMOVE(assoc, timer_Q);
			if (la->timeStamp >= assoc->exp) { /* state expired */
				SN_LOG(((assoc->state == SN_CL) ? (SN_LOG_DEBUG) : (SN_LOG_INFO)),
				    logsctperror("Timer Expired", assoc->g_vtag, assoc->state, SN_TO_NODIR));
				RmSctpAssoc(la, assoc);
				freeGlobalAddressList(assoc);
				sn_free(assoc);
			} else {/* state not expired, reschedule timer*/
				sctp_AddTimeOut(la, assoc);
			}
		}
		/* Goto next location in the timer queue*/
		++la->sctpNatTimer.loc_time;
		if (++la->sctpNatTimer.cur_loc >= SN_TIMER_QUEUE_SIZE)
			la->sctpNatTimer.cur_loc = 0;
	}
}

/* ----------------------------------------------------------------------
 *                              LOGGING CODE
 * ----------------------------------------------------------------------
 */
/** @addtogroup Logging
 *
 * The logging functions provide logging of different items ranging from logging
 * a simple message, through logging an association details to logging the
 * current state of the NAT tables
 */
/** @ingroup Logging
 * @brief Log sctp nat errors
 *
 * @param errormsg Error message to be logged
 * @param vtag Current Vtag
 * @param error Error number
 * @param direction Direction of packet
 */
static void
logsctperror(char* errormsg, uint32_t vtag, int error, int direction)
{
	char dir;
	switch (direction) {
	case SN_TO_LOCAL:
		dir = 'L';
		break;
	case SN_TO_GLOBAL:
		dir = 'G';
		break;
	default:
		dir = '*';
		break;
	}
	SctpAliasLog("->%c %s (vt=%u) %d\n", dir, errormsg, ntohl(vtag), error);
}

/** @ingroup Logging
 * @brief Log what the parser parsed
 *
 * @param direction Direction of packet
 * @param sm Pointer to sctp message information
 */
static void
logsctpparse(int direction, struct sctp_nat_msg *sm)
{
	char *ploc, *pstate;
	switch (direction) {
	case SN_TO_LOCAL:
		ploc = "TO_LOCAL -";
		break;
	case SN_TO_GLOBAL:
		ploc = "TO_GLOBAL -";
		break;
	default:
		ploc = "";
	}
	switch (sm->msg) {
	case SN_SCTP_INIT:
		pstate = "Init";
		break;
	case SN_SCTP_INITACK:
		pstate = "InitAck";
		break;
	case SN_SCTP_ABORT:
		pstate = "Abort";
		break;
	case SN_SCTP_SHUTACK:
		pstate = "ShutAck";
		break;
	case SN_SCTP_SHUTCOMP:
		pstate = "ShutComp";
		break;
	case SN_SCTP_ASCONF:
		pstate = "Asconf";
		break;
	case SN_SCTP_ASCONFACK:
		pstate = "AsconfAck";
		break;
	case SN_SCTP_OTHER:
		pstate = "Other";
		break;
	default:
		pstate = "***ERROR***";
		break;
	}
	SctpAliasLog("Parsed: %s %s\n", ploc, pstate);
}

/** @ingroup Logging
 * @brief Log an SCTP association's details
 *
 * @param assoc pointer to sctp association
 * @param s Character that indicates the state of processing for this packet
 */
static void logsctpassoc(struct sctp_nat_assoc *assoc, char* s)
{
	struct sctp_GlobalAddress *G_Addr = NULL;
	char *sp;
	char addrbuf[INET_ADDRSTRLEN];

	switch (assoc->state) {
	case SN_ID:
		sp = "ID ";
		break;
	case SN_INi:
		sp = "INi ";
		break;
	case SN_INa:
		sp = "INa ";
		break;
	case SN_UP:
		sp = "UP ";
		break;
	case SN_CL:
		sp = "CL ";
		break;
	case SN_RM:
		sp = "RM ";
		break;
	default:
		sp = "***ERROR***";
		break;
	}
	SctpAliasLog("%sAssoc: %s exp=%u la=%s lv=%u lp=%u gv=%u gp=%u tbl=%d\n",
	    s, sp, assoc->exp, inet_ntoa_r(assoc->l_addr, addrbuf),
	    ntohl(assoc->l_vtag), ntohs(assoc->l_port),
	    ntohl(assoc->g_vtag), ntohs(assoc->g_port),
	    assoc->TableRegister);
	/* list global addresses */
	LIST_FOREACH(G_Addr, &(assoc->Gaddr), list_Gaddr) {
		SctpAliasLog("\t\tga=%s\n",
		    inet_ntoa_r(G_Addr->g_addr, addrbuf));
	}
}

/** @ingroup Logging
 * @brief Output Global table to log
 *
 * @param la Pointer to the relevant libalias instance
 */
static void logSctpGlobal(struct libalias *la)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;

	SctpAliasLog("G->\n");
	for (i=0; i < la->sctpNatTableSize; i++) {
		LIST_FOREACH(assoc, &la->sctpTableGlobal[i], list_G) {
			logsctpassoc(assoc, " ");
		}
	}
}

/** @ingroup Logging
 * @brief  Output Local table to log
 *
 * @param la Pointer to the relevant libalias instance
 */
static void logSctpLocal(struct libalias *la)
{
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;

	SctpAliasLog("L->\n");
	for (i=0; i < la->sctpNatTableSize; i++) {
		LIST_FOREACH(assoc, &la->sctpTableLocal[i], list_L) {
			logsctpassoc(assoc, " ");
		}
	}
}

/** @ingroup Logging
 * @brief Output timer queue to log
 *
 * @param la Pointer to the relevant libalias instance
 */
static void logTimerQ(struct libalias *la)
{
	static char buf[50];
	u_int i;
	struct sctp_nat_assoc *assoc = NULL;

	SctpAliasLog("t->\n");
	for (i=0; i < SN_TIMER_QUEUE_SIZE; i++) {
		LIST_FOREACH(assoc, &la->sctpNatTimer.TimerQ[i], timer_Q) {
			snprintf(buf, 50, " l=%u ",i);
			//SctpAliasLog(la->logDesc," l=%d ",i);
			logsctpassoc(assoc, buf);
		}
	}
}

/** @ingroup Logging
 * @brief Sctp NAT logging function
 *
 * This function is based on a similar function in alias_db.c
 *
 * @param str/stream logging descriptor
 * @param format printf type string
 */
#ifdef _KERNEL
static void
SctpAliasLog(const char *format, ...)
{
	char buffer[LIBALIAS_BUF_SIZE];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, LIBALIAS_BUF_SIZE, format, ap);
	va_end(ap);
	log(LOG_SECURITY | LOG_INFO,
	    "alias_sctp: %s", buffer);
}
#else
static void
SctpAliasLog(FILE *stream, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vfprintf(stream, format, ap);
	va_end(ap);
	fflush(stream);
}
#endif
