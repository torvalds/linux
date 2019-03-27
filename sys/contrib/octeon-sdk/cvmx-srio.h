/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/



/**
 * @file
 *
 * Interface to SRIO
 *
 * <hr>$Revision: 41586 $<hr>
 */

#ifndef __CVMX_SRIO_H__
#define __CVMX_SRIO_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Enumeration of the type of operations that can be performed
 * by a mapped write operation.
 */
typedef enum
{
    CVMX_SRIO_WRITE_MODE_NWRITE = 0,        /**< Only create NWrite operations */
    CVMX_SRIO_WRITE_MODE_NWRITE_RESP = 1,   /**< Create NWrite with response */
    CVMX_SRIO_WRITE_MODE_AUTO = 2,          /**< Intelligently breaks writes into multiple transactions based on alignment */
    CVMX_SRIO_WRITE_MODE_AUTO_RESP = 3,     /**< CVMX_SRIO_WRITE_MODE_WRITE followed with a response */
    CVMX_SRIO_WRITE_MODE_MAINTENANCE = 6,   /**< Create a MAINTENANCE transaction. Use cvmx_srio_config_write32() instead */
    CVMX_SRIO_WRITE_MODE_PORT = 7           /**< Port Write? */
} cvmx_srio_write_mode_t;

/**
 * Enumeration of the type of operations that can be performed
 * by a mapped read operation.
 */
typedef enum
{
    CVMX_SRIO_READ_MODE_NORMAL = 0,         /**< Perform a normal read */
    CVMX_SRIO_READ_MODE_ATOMIC_SET = 2,     /**< Atomically sets bits in data on remote device */
    CVMX_SRIO_READ_MODE_ATOMIC_CLEAR = 3,   /**< Atomically clears bits in data on remote device */
    CVMX_SRIO_READ_MODE_ATOMIC_INCREMENT = 4,/**< Atomically increments data on remote device */
    CVMX_SRIO_READ_MODE_ATOMIC_DECREMENT = 5,/**< Atomically decrements data on remote device */
    CVMX_SRIO_READ_MODE_MAINTENANCE = 6     /**< Create a MAINTENANCE transaction. Use cvmx_srio_config_read32() instead */
} cvmx_srio_read_mode_t;

/**
 * Initialization flags for SRIO
 */
typedef enum
{
    CVMX_SRIO_INITIALIZE_DEBUG = 1,
} cvmx_srio_initialize_flags_t;

/**
 * The possible results from a doorbell operation
 */
typedef enum
{
    CVMX_SRIO_DOORBELL_DONE,    /**< The doorbell is complete */
    CVMX_SRIO_DOORBELL_NONE,    /**< There wasn't an outstanding doorbell */
    CVMX_SRIO_DOORBELL_BUSY,    /**< The doorbell is still processing */
    CVMX_SRIO_DOORBELL_RETRY,   /**< The doorbell needs to be retried */
    CVMX_SRIO_DOORBELL_ERROR,   /**< The doorbell failed with an error */
    CVMX_SRIO_DOORBELL_TMOUT    /**< The doorbell failed due to timeout */
} cvmx_srio_doorbell_status_t;

/**
 * This structure represents the SRIO header received from SRIO on
 * the top of every received message. This header passes through
 * IPD/PIP unmodified.
 */
typedef struct
{
    union
    {
        uint64_t u64;
        struct
        {
#ifdef __BIG_ENDIAN_BITFIELD
            uint64_t prio   : 2; /**< The sRIO prio (priority) field in the
                                    first sRIO message segment received for the
                                    message. */
            uint64_t tt     : 1; /**< When set, indicates that the first sRIO
                                    message segment received for the message had
                                    16-bit source and destination ID's. When
                                    clear, indicates 8-bit ID were present. */
            uint64_t dis    : 1; /**< When set, indicates that the destination
                                    ID in the first sRIO message segment received
                                    for the message matched the 63xx's secondary
                                    ID. When clear, indicates that the destination
                                    ID in the first sRIO message segment
                                    received for the message matched the 63xx's
                                    primary ID. Note that the full destination
                                    ID in the received sRIO message can be
                                    determined via the combination of
                                    WORD0[DIS] in the sRIO inbound message
                                    header and WORD1[iprt] in the work queue
                                    entry created by PIP/IPD. */
            uint64_t ssize  : 4; /**< The RIO ssize (standard message packet data
                                    size) field used for the message. */
            uint64_t sid    : 16; /**< The source ID in the first sRIO message
                                    segment received for the message. When TT is
                                    clear, the most-significant 8 bits are zero. */
            uint64_t xmbox  : 4; /**< The RIO xmbox (recipient mailbox extension)
                                    field in the first sRIO message segment
                                    received for the message. Always zero for
                                    multi-segment messages. */
            uint64_t mbox   : 2; /**< The RIO mbox (recipient mailbox) field in
                                    the first sRIO message segment received for
                                    the message. */
            uint64_t letter : 2; /**< The RIO letter (slot within a mailbox)
                                    field in the first sRIO message segment
                                    received for the message. */
            uint64_t seq    : 32; /**< A sequence number. Whenever the OCTEON
                                    63xx sRIO hardware accepts the first sRIO
                                    segment of either a message or doorbell, it
                                    samples the current value of a counter
                                    register and increments the counter
                                    register. SEQ is the value sampled for the
                                    message. The counter increments once per
                                    message/doorbell. SEQ can be used to
                                    determine the relative order of
                                    packets/doorbells. Note that the SEQ-implied
                                    order may differ from the order that the
                                    WQE's are received by software for a number
                                    of reasons, including the fact that the WQE
                                    is not created until the end of the message,
                                    while SEQ is sampled when the first segment. */
#else
            uint64_t seq    : 32;
            uint64_t letter : 2;
            uint64_t mbox   : 2;
            uint64_t xmbox  : 4;
            uint64_t sid    : 16;
            uint64_t ssize  : 4;
            uint64_t dis    : 1;
            uint64_t tt     : 1;
            uint64_t prio   : 2;
#endif
        } s;
    } word0;
    union
    {
        uint64_t u64;
        struct
        {
#ifdef __BIG_ENDIAN_BITFIELD
            uint64_t r      : 1; /**< When set, WORD1[R]/PKT_INST_HDR[R] selects
                                    either RAWFULL or RAWSCHED special PIP
                                    instruction form. WORD1[R] may commonly be
                                    set so that WORD1[QOS,GRP] will be directly
                                    used by the PIP hardware. */
            uint64_t reserved_62_58 : 5;
            uint64_t pm     : 2; /**< WORD1[PM]/PKT_INST_HDR[PM] selects the PIP
                                    parse mode (uninterpreted, skip-to-L2,
                                    skip-to-IP), and chooses between
                                    RAWFULL/RAWSCHED when WORD1[R] is set. */
            uint64_t reserved_55 : 1;
            uint64_t sl     : 7; /**< WORD1[SL]/PKT_INST_HDR[SL] selects the
                                    skip II length. WORD1[SL] may typically be
                                    set to 8 (or larger) so that PIP skips this
                                    WORD1. */
            uint64_t reserved_47_46 : 2;
            uint64_t nqos   : 1; /**< WORD1[NQOS] must not be set when WORD1[R]
                                    is clear and PIP interprets WORD1 as a
                                    PKT_INST_HDR. When set, WORD1[NQOS]/PKT_INST_HDR[NQOS]
                                    prevents PIP from directly using
                                    WORD1[QOS]/PKT_INST_HDR[QOS] for the QOS
                                    value in the work queue entry created by
                                    PIP. WORD1[NQOS] may commonly be clear so
                                    that WORD1[QOS] will be directly used by the
                                    PIP hardware. PKT_INST_HDR[NQOS] is new to
                                    63xx - this functionality did not exist in
                                    prior OCTEON's. */
            uint64_t ngrp   : 1; /**< WORD1[NGRP] must not be set when WORD1[R]
                                    is clear and PIP interprets WORD1 as a
                                    PKT_INST_HDR. When set, WORD1[NGRP]/PKT_INST_HDR[NGRP]
                                    prevents PIP from directly using
                                    WORD1[GRP]/PKT_INST_HDR[GRP] for the GRP
                                    value in the work queue entry created by
                                    PIP. WORD1[NGRP] may commonly be clear so
                                    that WORD1[GRP] will be directly used by the
                                    PIP hardware. PKT_INST_HDR[NGRP] is new to
                                    63xx - this functionality did not exist in
                                    prior OCTEON's. */
            uint64_t ntt    : 1; /**< WORD1[NTT] must not be set when WORD1[R]
                                    is clear and PIP interprets WORD1 as a
                                    PKT_INST_HDR. When set, WORD1[NTT]/PKT_INST_HDR[NTT]
                                    prevents PIP from directly using
                                    WORD1[TT]/PKT_INST_HDR[TT] for the TT value
                                    in the work queue entry created by PIP.
                                    PKT_INST_HDR[NTT] is new to 63xx - this
                                    functionality did not exist in prior OCTEON's. */
            uint64_t ntag   : 1; /**< WORD1[NTAG] must not be set when WORD1[R]
                                    is clear and PIP interprets WORD1 as a
                                    PKT_INST_HDR. When set, WORD1[NTAG]/PKT_INST_HDR[NTAG]
                                    prevents PIP from directly using
                                    WORD1[TAG]/PKT_INST_HDR[TAG] for the TAG
                                    value in the work queue entry created by PIP.
                                    PKT_INST_HDR[NTAG] is new to 63xx - this
                                    functionality did not exist in prior OCTEON's. */
            uint64_t qos    : 3; /**< Created by the hardware from an entry in a
                                    256-entry table. The 8-bit value
                                    WORD0[PRIO,TT,DIS,MBOX,LETTER] selects the
                                    table entry. When WORD1[R] is set and WORD1[NQOS]
                                    is clear, WORD1[QOS] becomes the QOS value
                                    in the work queue entry created by PIP. The
                                    QOS value in the work queue entry determines
                                    the priority that SSO/POW will schedule the
                                    work, and can also control how/if the sRIO
                                    message gets dropped by PIP/IPD. The 256-entry
                                    table is unique to each sRIO core, but
                                    shared by the two controllers associated
                                    with the sRIO core. */
            uint64_t grp    : 4; /**< Created by the hardware from an entry in a
                                    256-entry table. The 8-bit value
                                    WORD0[PRIO,TT,DIS,MBOX,LETTER] selects the
                                    table entry. When WORD1[R] is set and WORD1[NGRP]
                                    is clear, WORD1[GRP] becomes the GRP value
                                    in the work queue entry created by PIP. The
                                    GRP value in the work queue entry can direct
                                    the work to particular cores or particular
                                    groups of cores. The 256-entry table is
                                    unique to each sRIO core, but shared by the
                                    two controllers associated with the sRIO core. */
            uint64_t rs     : 1; /**< In some configurations, enables the sRIO
                                    message to be buffered solely in the work
                                    queue entry, and not otherwise in L2/DRAM. */
            uint64_t tt     : 2; /**< When WORD1[R] is set and WORD1[NTT] is
                                    clear, WORD1[TT]/PKT_INST_HDR[TT] becomes
                                    the TT value in the work queue entry created
                                    by PIP. The TT and TAG values in the work
                                    queue entry determine the scheduling/synchronization
                                    constraints for the work (no constraints,
                                    tag order, atomic tag order). */
            uint64_t tag    : 32; /**< Created by the hardware from a CSR
                                    associated with the sRIO inbound message
                                    controller. When WORD1[R] is set and WORD1[NTAG]
                                    is clear, WORD1[TAG]/PKT_INST_HDR[TAG]
                                    becomes the TAG value in the work queue
                                    entry created by PIP. The TT and TAG values
                                    in the work queue entry determine the
                                    scheduling/synchronization constraints for
                                    the work (no constraints, tag order, atomic
                                    tag order). */
#else
            uint64_t tag    : 32;
            uint64_t tt     : 2;
            uint64_t rs     : 1;
            uint64_t grp    : 4;
            uint64_t qos    : 3;
            uint64_t ntag   : 1;
            uint64_t ntt    : 1;
            uint64_t ngrp   : 1;
            uint64_t nqos   : 1;
            uint64_t reserved_47_46 : 2;
            uint64_t sl     : 7;
            uint64_t reserved_55 : 1;
            uint64_t pm     : 2;
            uint64_t reserved_62_58 : 5;
            uint64_t r      : 1;
#endif
        } s;
    } word1;
} cvmx_srio_rx_message_header_t;

/**
 * This structure represents the SRIO header required on the front
 * of PKO packets destine for SRIO message queues.
 */
typedef union
{
    uint64_t u64;
    struct
    {
#ifdef __BIG_ENDIAN_BITFIELD
        uint64_t prio   : 2;    /**< The sRIO prio (priority) field for all
                                    segments in the message. */
        uint64_t tt     : 1;    /**< When set, the sRIO message segments use a
                                    16-bit source and destination ID for all the
                                    segments in the message. When clear, the
                                    message segments use an 8-bit ID. */
        uint64_t sis    : 1;    /**< When set, the sRIO message segments use the
                                    63xx's secondary ID as the source ID. When
                                    clear, the sRIO message segments use the
                                    primary ID as the source ID. */
        uint64_t ssize  : 4;    /**< The RIO ssize (standard message segment
                                    data size) field used for the message. */
        uint64_t did    : 16;   /**< The destination ID in the sRIO message
                                    segments of the message. When TT is clear,
                                    the most-significant 8 bits must be zero. */
        uint64_t xmbox  : 4;    /**< The RIO xmbox (recipient mailbox extension)
                                    field in the sRIO message segment for a
                                    single-segment message. Must be zero for
                                    multi-segment messages. */
        uint64_t mbox   : 2;    /**< The RIO mbox (recipient mailbox) field in
                                    the sRIO message segments of the message. */
        uint64_t letter : 2;    /**< The RIO letter (slot within mailbox) field
                                    in the sRIO message segments of the message
                                    when LNS is clear. When LNS is set, this
                                    LETTER field is not used and must be zero. */
        uint64_t reserved_31_2 : 30;
        uint64_t lns    : 1;    /**< When set, the outbound message controller
                                    will dynamically selects an sRIO letter
                                    field for the message (based on LETTER_SP or
                                    LETTER_MP - see appendix A), and the LETTER
                                    field in this sRIO outbound message
                                    descriptor is unused. When clear, the LETTER
                                    field in this sRIO outbound message
                                    descriptor selects the sRIO letter used for
                                    the message. */
        uint64_t intr   : 1;    /**< When set, the outbound message controller
                                    will set an interrupt bit after all sRIO
                                    segments of the message receive a message
                                    DONE response. If the message transfer has
                                    errors, the interrupt bit is not set (but
                                    others are). */
#else
        uint64_t intr   : 1;
        uint64_t lns    : 1;
        uint64_t reserved_31_2 : 30;
        uint64_t letter : 2;
        uint64_t mbox   : 2;
        uint64_t xmbox  : 4;
        uint64_t did    : 16;
        uint64_t ssize  : 4;
        uint64_t sis    : 1;
        uint64_t tt     : 1;
        uint64_t prio   : 2;
#endif
    } s;
} cvmx_srio_tx_message_header_t;

/**
 * Reset SRIO to link partner
 *
 * @param srio_port  SRIO port to initialize
 *
 * @return Zero on success
 */
int cvmx_srio_link_rst(int srio_port);

/**
 * Initialize a SRIO port for use.
 *
 * @param srio_port SRIO port to initialize
 * @param flags     Optional flags
 *
 * @return Zero on success
 */
int cvmx_srio_initialize(int srio_port, cvmx_srio_initialize_flags_t flags);

/**
 * Read 32bits from a Device's config space
 *
 * @param srio_port SRIO port the device is on
 * @param srcid_index
 *                  Which SRIO source ID to use. 0 = Primary, 1 = Secondary
 * @param destid    RapidIO device ID, or -1 for the local Octeon.
 * @param is16bit   Non zero if the transactions should use 16bit device IDs. Zero
 *                  if transactions should use 8bit device IDs.
 * @param hopcount  Number of hops to the remote device. Use 0 for the local Octeon.
 * @param offset    Offset in config space. This must be a multiple of 32 bits.
 * @param result    Result of the read. This will be unmodified on failure.
 *
 * @return Zero on success, negative on failure.
 */
int cvmx_srio_config_read32(int srio_port, int srcid_index, int destid,
                            int is16bit, uint8_t hopcount, uint32_t offset,
                            uint32_t *result);

/**
 * Write 32bits to a Device's config space
 *
 * @param srio_port SRIO port the device is on
 * @param srcid_index
 *                  Which SRIO source ID to use. 0 = Primary, 1 = Secondary
 * @param destid    RapidIO device ID, or -1 for the local Octeon.
 * @param is16bit   Non zero if the transactions should use 16bit device IDs. Zero
 *                  if transactions should use 8bit device IDs.
 * @param hopcount  Number of hops to the remote device. Use 0 for the local Octeon.
 * @param offset    Offset in config space. This must be a multiple of 32 bits.
 * @param data      Data to write.
 *
 * @return Zero on success, negative on failure.
 */
int cvmx_srio_config_write32(int srio_port, int srcid_index, int destid,
                             int is16bit, uint8_t hopcount, uint32_t offset,
                             uint32_t data);

/**
 * Send a RapidIO doorbell to a remote device
 *
 * @param srio_port SRIO port the device is on
 * @param srcid_index
 *                  Which SRIO source ID to use. 0 = Primary, 1 = Secondary
 * @param destid    RapidIO device ID.
 * @param is16bit   Non zero if the transactions should use 16bit device IDs. Zero
 *                  if transactions should use 8bit device IDs.
 * @param priority  Doorbell priority (0-3)
 * @param data      Data for doorbell.
 *
 * @return Zero on success, negative on failure.
 */
int cvmx_srio_send_doorbell(int srio_port, int srcid_index, int destid,
                            int is16bit, int priority, uint16_t data);

/**
 * Get the status of the last doorbell sent. If the dooorbell
 * hardware is done, then the status is cleared to get ready for
 * the next doorbell (or retry).
 *
 * @param srio_port SRIO port to check doorbell on
 *
 * @return Doorbell status
 */
cvmx_srio_doorbell_status_t cvmx_srio_send_doorbell_status(int srio_port);

/**
 * Read a received doorbell and report data about it.
 *
 * @param srio_port SRIO port to check for the received doorbell
 * @param destid_index
 *                  Which Octeon destination ID was the doorbell for
 * @param sequence_num
 *                  Sequence number of doorbell (32bits)
 * @param srcid     RapidIO source ID of the doorbell sender
 * @param priority  Priority of the doorbell (0-3)
 * @param is16bit   Non zero if the transactions should use 16bit device IDs. Zero
 *                  if transactions should use 8bit device IDs.
 * @param data      Data in the doorbell (16 bits)
 *
 * @return Doorbell status. Either DONE, NONE, or ERROR.
 */
cvmx_srio_doorbell_status_t cvmx_srio_receive_doorbell(int srio_port,
        int *destid_index, uint32_t *sequence_num, int *srcid, int *priority,
        int *is16bit, uint16_t *data);

/**
 * Receive a packet from the Soft Packet FIFO (SPF).
 *
 * @param srio_port SRIO port to read the packet from.
 * @param buffer    Buffer to receive the packet.
 * @param buffer_length
 *                  Length of the buffer in bytes.
 *
 * @return Returns the length of the packet read. Negative on failure.
 *         Zero if no packets are available.
 */
int cvmx_srio_receive_spf(int srio_port, void *buffer, int buffer_length);

/**
 * Map a remote device's memory region into Octeon's physical
 * address area. The caller can then map this into a core using
 * the TLB or XKPHYS.
 *
 * @param srio_port SRIO port to map the device on
 * @param write_op  Type of operation to perform on a write to the device.
 *                  Normally should be CVMX_SRIO_WRITE_MODE_AUTO.
 * @param write_priority
 *                  SRIO priority of writes (0-3)
 * @param read_op   Type of operation to perform on reads to the device.
 *                  Normally should be CVMX_SRIO_READ_MODE_NORMAL.
 * @param read_priority
 *                  SRIO priority of reads (0-3)
 * @param srcid_index
 *                  Which SRIO source ID to use. 0 = Primary, 1 = Secondary
 * @param destid    RapidIO device ID.
 * @param is16bit   Non zero if the transactions should use 16bit device IDs. Zero
 *                  if transactions should use 8bit device IDs.
 * @param base      Device base address to start the mapping
 * @param size      Size of the mapping in bytes
 *
 * @return Octeon 64bit physical address that accesses the remote device,
 *         or zero on failure.
 */
uint64_t cvmx_srio_physical_map(int srio_port, cvmx_srio_write_mode_t write_op,
        int write_priority, cvmx_srio_read_mode_t read_op, int read_priority,
        int srcid_index, int destid, int is16bit, uint64_t base, uint64_t size);

/**
 * Unmap a physical address window created by cvmx_srio_phys_map().
 *
 * @param physical_address
 *               Physical address returned by cvmx_srio_phys_map().
 * @param size   Size used on original call.
 *
 * @return Zero on success, negative on failure.
 */
int cvmx_srio_physical_unmap(uint64_t physical_address, uint64_t size);

#ifdef CVMX_ENABLE_PKO_FUNCTIONS
/**
 * fill out outbound message descriptor
 *
 * @param buf_ptr     pointer to a buffer pointer. the buffer pointer points
 *                    to a chain of buffers that hold an outbound srio packet.
 *                    the packet can take the format of (1) a pip/ipd inbound
 *                    message or (2) an application-generated outbound message
 * @param desc_ptr    pointer to an outbound message descriptor. should be null
 *                    if *buf_ptr is in the format (1)
 *
 * @return           0 on success; negative of failure.
 */
int cvmx_srio_omsg_desc (uint64_t port, cvmx_buf_ptr_t *buf_ptr,
                         cvmx_srio_tx_message_header_t *desc_ptr);
#endif


#ifdef	__cplusplus
}
#endif

#endif
