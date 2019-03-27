/**************************************************************************
SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


$FreeBSD$

***************************************************************************/

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ctype.h>
#include <sys/endian.h>
#include <sys/bus.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/kdb.h>

#include <dev/mii/mii.h>

#ifndef _CXGB_OSDEP_H_
#define _CXGB_OSDEP_H_

typedef struct adapter adapter_t;
typedef struct port_info pinfo_t;
struct sge_rspq;

enum {
	TP_TMR_RES = 200,	/* TP timer resolution in usec */
	MAX_NPORTS = 4,		/* max # of ports */
	TP_SRAM_OFFSET = 4096,	/* TP SRAM content offset in eeprom */
	TP_SRAM_LEN = 2112,	/* TP SRAM content offset in eeprom */
};

struct t3_mbuf_hdr {
	struct mbuf *mh_head;
	struct mbuf *mh_tail;
};

#ifndef PANIC_IF
#define PANIC_IF(exp) do {                  \
	if (exp)                            \
		panic("BUG: %s", #exp);      \
} while (0)
#endif

#if __FreeBSD_version < 800054
#if defined (__GNUC__)
  #if #cpu(i386) || defined __i386 || defined i386 || defined __i386__ || #cpu(x86_64) || defined __x86_64__
    #define mb()  __asm__ __volatile__ ("mfence;": : :"memory")
    #define wmb()  __asm__ __volatile__ ("sfence;": : :"memory")
    #define rmb()  __asm__ __volatile__ ("lfence;": : :"memory")
  #elif #cpu(sparc64) || defined sparc64 || defined __sparcv9 
    #define mb()  __asm__ __volatile__ ("membar #MemIssue": : :"memory")
    #define wmb() mb()
    #define rmb() mb()
  #elif #cpu(sparc) || defined sparc || defined __sparc__
    #define mb()  __asm__ __volatile__ ("stbar;": : :"memory")
    #define wmb() mb()
    #define rmb() mb()
#else
    #define wmb() mb()
    #define rmb() mb()
    #define mb() 	/* XXX just to make this compile */
  #endif
#else
  #error "unknown compiler"
#endif
#endif

/*
 * Workaround for weird Chelsio issue
 */
#if __FreeBSD_version > 700029
#define PRIV_SUPPORTED
#endif

#define CXGB_TX_CLEANUP_THRESHOLD        32

#define TX_MAX_SIZE                (1 << 16)    /* 64KB                          */
#define TX_MAX_SEGS                      36     /* maximum supported by card     */

#define TX_MAX_DESC                       4     /* max descriptors per packet    */


#define TX_START_MAX_DESC (TX_MAX_DESC << 2)    /* maximum number of descriptors
						 * call to start used per 	 */

#define TX_CLEAN_MAX_DESC (TX_MAX_DESC << 4)    /* maximum tx descriptors
						 * to clean per iteration        */
#define TX_WR_SIZE_MAX    11*1024              /* the maximum total size of packets aggregated into a single
						* TX WR
						*/
#define TX_WR_COUNT_MAX         7              /* the maximum total number of packets that can be
						* aggregated into a single TX WR
						*/
#if defined(__i386__) || defined(__amd64__)  

static __inline
void prefetch(void *x) 
{ 
        __asm volatile("prefetcht0 %0" :: "m" (*(unsigned long *)x));
}

#define smp_mb() mb()

#define L1_CACHE_BYTES 128
#define WARN_ON(condition) do { \
	if (__predict_false((condition)!=0)) {  \
                log(LOG_WARNING, "BUG: warning at %s:%d/%s()\n", __FILE__, __LINE__, __FUNCTION__); \
                kdb_backtrace(); \
        } \
} while (0)

#else 
#define smp_mb()
#define prefetch(x)
#define L1_CACHE_BYTES 32
#endif

#define DBG_RX          (1 << 0)
static const int debug_flags = DBG_RX;

#ifdef DEBUG_PRINT
#define DBG(flag, msg) do {	\
	if ((flag & debug_flags))	\
		printf msg; \
} while (0)
#else
#define DBG(...)
#endif

#include <sys/syslog.h>

#define promisc_rx_mode(rm)  ((rm)->port->ifp->if_flags & IFF_PROMISC) 
#define allmulti_rx_mode(rm) ((rm)->port->ifp->if_flags & IFF_ALLMULTI) 

#define CH_ERR(adap, fmt, ...) log(LOG_ERR, fmt, ##__VA_ARGS__)
#define CH_WARN(adap, fmt, ...)	log(LOG_WARNING, fmt, ##__VA_ARGS__)
#define CH_ALERT(adap, fmt, ...) log(LOG_ALERT, fmt, ##__VA_ARGS__)

#define t3_os_sleep(x) DELAY((x) * 1000)

#define test_and_clear_bit(bit, p) atomic_cmpset_int((p), ((*(p)) | (1<<bit)), ((*(p)) & ~(1<<bit)))

#define max_t(type, a, b) (type)max((a), (b))
#define cpu_to_be32(x)		htobe32(x)

/* Standard PHY definitions */
#define BMCR_LOOPBACK		BMCR_LOOP
#define BMCR_ISOLATE		BMCR_ISO
#define BMCR_ANENABLE		BMCR_AUTOEN
#define BMCR_SPEED1000		BMCR_SPEED1
#define BMCR_SPEED100		BMCR_SPEED0
#define BMCR_ANRESTART		BMCR_STARTNEG
#define BMCR_FULLDPLX		BMCR_FDX
#define BMSR_LSTATUS		BMSR_LINK
#define BMSR_ANEGCOMPLETE	BMSR_ACOMP

#define MII_LPA			MII_ANLPAR
#define MII_ADVERTISE		MII_ANAR
#define MII_CTRL1000		MII_100T2CR

#define ADVERTISE_PAUSE_CAP	ANAR_FC
#define ADVERTISE_PAUSE_ASYM	0x800
#define ADVERTISE_PAUSE		ANAR_FC
#define ADVERTISE_1000HALF	0x100
#define ADVERTISE_1000FULL	0x200
#define ADVERTISE_10FULL	ANAR_10_FD
#define ADVERTISE_10HALF	ANAR_10
#define ADVERTISE_100FULL	ANAR_TX_FD
#define ADVERTISE_100HALF	ANAR_TX


#define ADVERTISE_1000XHALF	ANAR_X_HD
#define ADVERTISE_1000XFULL	ANAR_X_FD
#define ADVERTISE_1000XPSE_ASYM	ANAR_X_PAUSE_ASYM
#define ADVERTISE_1000XPAUSE	ANAR_X_PAUSE_SYM

#define ADVERTISE_CSMA		ANAR_CSMA
#define ADVERTISE_NPAGE		ANAR_NP


/* Standard PCI Extended Capabilities definitions */
#define PCI_CAP_ID_VPD	PCIY_VPD
#define PCI_VPD_ADDR	PCIR_VPD_ADDR
#define PCI_VPD_ADDR_F	0x8000
#define PCI_VPD_DATA	PCIR_VPD_DATA

#define PCI_CAP_ID_EXP		PCIY_EXPRESS
#define PCI_EXP_DEVCTL		PCIER_DEVICE_CTL
#define PCI_EXP_DEVCTL_PAYLOAD	PCIEM_CTL_MAX_PAYLOAD
#define PCI_EXP_DEVCTL_READRQ	PCIEM_CTL_MAX_READ_REQUEST
#define PCI_EXP_LNKCTL		PCIER_LINK_CTL
#define PCI_EXP_LNKSTA		PCIER_LINK_STA

/*
 * Linux compatibility macros
 */

/* Some simple translations */
#define __devinit
#define udelay(x) DELAY(x)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define le32_to_cpu(x) le32toh(x)
#define le16_to_cpu(x) le16toh(x)
#define cpu_to_le32(x) htole32(x)
#define swab32(x) bswap32(x)
#ifndef simple_strtoul
#define simple_strtoul(...) strtoul(__VA_ARGS__)
#endif


#ifndef LINUX_TYPES_DEFINED
typedef uint8_t 	u8;
typedef uint16_t 	u16;
typedef uint32_t 	u32;
typedef uint64_t 	u64;
 
typedef uint8_t		__u8;
typedef uint16_t	__u16;
typedef uint32_t	__u32;
typedef uint8_t		__be8;
typedef uint16_t	__be16;
typedef uint32_t	__be32;
typedef uint64_t	__be64;
#endif


#if BYTE_ORDER == BIG_ENDIAN
#define __BIG_ENDIAN_BITFIELD
#elif BYTE_ORDER == LITTLE_ENDIAN
#define __LITTLE_ENDIAN_BITFIELD
#else
#error "Must set BYTE_ORDER"
#endif

/* Indicates what features are supported by the interface. */
#define SUPPORTED_10baseT_Half          (1 << 0)
#define SUPPORTED_10baseT_Full          (1 << 1)
#define SUPPORTED_100baseT_Half         (1 << 2)
#define SUPPORTED_100baseT_Full         (1 << 3)
#define SUPPORTED_1000baseT_Half        (1 << 4)
#define SUPPORTED_1000baseT_Full        (1 << 5)
#define SUPPORTED_Autoneg               (1 << 6)
#define SUPPORTED_TP                    (1 << 7)
#define SUPPORTED_AUI                   (1 << 8)
#define SUPPORTED_MII                   (1 << 9) 
#define SUPPORTED_FIBRE                 (1 << 10)
#define SUPPORTED_BNC                   (1 << 11)
#define SUPPORTED_10000baseT_Full       (1 << 12)
#define SUPPORTED_Pause                 (1 << 13)
#define SUPPORTED_Asym_Pause            (1 << 14)

/* Indicates what features are advertised by the interface. */
#define ADVERTISED_10baseT_Half         (1 << 0)
#define ADVERTISED_10baseT_Full         (1 << 1)
#define ADVERTISED_100baseT_Half        (1 << 2)
#define ADVERTISED_100baseT_Full        (1 << 3)
#define ADVERTISED_1000baseT_Half       (1 << 4)
#define ADVERTISED_1000baseT_Full       (1 << 5)
#define ADVERTISED_Autoneg              (1 << 6)
#define ADVERTISED_TP                   (1 << 7)
#define ADVERTISED_AUI                  (1 << 8)
#define ADVERTISED_MII                  (1 << 9)
#define ADVERTISED_FIBRE                (1 << 10) 
#define ADVERTISED_BNC                  (1 << 11)
#define ADVERTISED_10000baseT_Full      (1 << 12)
#define ADVERTISED_Pause                (1 << 13)
#define ADVERTISED_Asym_Pause           (1 << 14)

/* Enable or disable autonegotiation.  If this is set to enable,
 * the forced link modes above are completely ignored.
 */
#define AUTONEG_DISABLE         0x00
#define AUTONEG_ENABLE          0x01

#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000
#define SPEED_10000		10000
#define DUPLEX_HALF		0
#define DUPLEX_FULL		1

#endif
