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
 * Interface to the CN31XX, CN38XX, and CN58XX hardware DFA engine.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_DFA_H__
#define __CVMX_DFA_H__
#include "cvmx-llm.h"
#include "cvmx-wqe.h"
#include "cvmx-fpa.h"

#include "executive-config.h"
#ifdef CVMX_ENABLE_DFA_FUNCTIONS
#include "cvmx-config.h"
#endif

#define ENABLE_DEPRECATED   /* Set to enable the old 18/36 bit names */

#ifdef	__cplusplus
extern "C" {
#endif


/* Maximum nodes available in a small encoding */
#define CVMX_DFA_NODESM_MAX_NODES       ((OCTEON_IS_MODEL(OCTEON_CN31XX)) ? 0x8000 : 0x20000)
#define CVMX_DFA_NODESM_SIZE            512     /* Size of each node for small encoding */
#define CVMX_DFA_NODELG_SIZE            1024    /* Size of each node for large encoding */
#define CVMX_DFA_NODESM_LAST_TERMINAL  (CVMX_DFA_NODESM_MAX_NODES-1)

#ifdef ENABLE_DEPRECATED
/* These defines are for compatability with old code. They are deprecated */
#define CVMX_DFA_NODE18_SIZE            CVMX_DFA_NODESM_SIZE
#define CVMX_DFA_NODE36_SIZE            CVMX_DFA_NODELG_SIZE
#define CVMX_DFA_NODE18_MAX_NODES       CVMX_DFA_NODESM_MAX_NODES
#define CVMX_DFA_NODE18_LAST_TERMINAL   CVMX_DFA_NODESM_LAST_TERMINAL
#endif

/**
 * Which type of memory encoding is this graph using. Make sure you setup
 * the LLM to match.
 */
typedef enum
{
    CVMX_DFA_GRAPH_TYPE_SM              = 0,
    CVMX_DFA_GRAPH_TYPE_LG              = 1,
#ifdef ENABLE_DEPRECATED
    CVMX_DFA_GRAPH_TYPE_18b             = 0,    /* Deprecated */
    CVMX_DFA_GRAPH_TYPE_36b             = 1     /* Deprecated */
#endif
} cvmx_dfa_graph_type_t;

/**
 * The possible node types.
 */
typedef enum
{
    CVMX_DFA_NODE_TYPE_NORMAL           = 0,    /**< Node is a branch */
    CVMX_DFA_NODE_TYPE_MARKED           = 1,    /**< Node is marked special */
    CVMX_DFA_NODE_TYPE_TERMINAL         = 2     /**< Node is a terminal leaf */
} cvmx_dfa_node_type_t;

/**
 * The possible reasons the DFA stopped processing.
 */
typedef enum
{
    CVMX_DFA_STOP_REASON_DATA_GONE      = 0,    /**< DFA ran out of data */
    CVMX_DFA_STOP_REASON_PARITY_ERROR   = 1,    /**< DFA encountered a memory error */
    CVMX_DFA_STOP_REASON_FULL           = 2,    /**< DFA is full */
    CVMX_DFA_STOP_REASON_TERMINAL       = 3     /**< DFA hit a terminal */
} cvmx_dfa_stop_reason_t;

/**
 * This format describes the DFA pointers in small mode
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                next_node1  :15;/**< Next node if an odd character match */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                next_node0  :15;/**< Next node if an even character match */
    } w32;
    struct
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                next_node1  :17;/**< Next node if an odd character match */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                next_node0  :17;/**< Next node if an even character match */
    } w36;
    struct /**< @ this structure only applies starting in CN58XX and if DFA_CFG[NRPL_ENA] == 1 and IWORD0[NREPLEN] == 1.  */
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                per_node_repl1  : 1;/**< enable for extra replicaiton for next node (CN58XX) */
        uint64_t                next_node_repl1 : 2;/**< extra replicaiton for next node (CN58XX) (if per_node_repl1 is set) */
        uint64_t                next_node1  :14;/**< Next node if an odd character match - IWORD3[Msize], if per_node_repl1==1. */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                per_node_repl0  : 1;/**< enable for extra replicaiton for next node (CN58XX) */
        uint64_t                next_node_repl0 : 2;/**< extra replicaiton for next node (CN58XX) (if per_node_repl0 is set) */
        uint64_t                next_node0  :14;/**< Next node if an odd character match - IWORD3[Msize], if per_node_repl0==1. */
    } w36nrepl_en; /**< use when next_node_repl[01] is 1. */
    struct /**< this structure only applies starting in CN58XX and if DFA_CFG[NRPL_ENA] == 1 and IWORD0[NREPLEN] == 1.  */
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                per_node_repl1  : 1;/**< enable for extra replicaiton for next node (CN58XX) */
        uint64_t                next_node1  :16;/**< Next node if an odd character match, if per_node_repl1==0. */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                per_node_repl0  : 1;/**< enable for extra replicaiton for next node (CN58XX) */
        uint64_t                next_node0  :16;/**< Next node if an odd character match, if per_node_repl0==0. */
    } w36nrepl_dis; /**< use when next_node_repl[01] is 0. */
#if defined(ENABLE_DEPRECATED) && !OCTEON_IS_COMMON_BINARY()
#if CVMX_COMPILED_FOR(OCTEON_CN31XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                next_node1  :15;/**< Next node if an odd character match */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                next_node0  :15;/**< Next node if an even character match */
    };
#elif CVMX_COMPILED_FOR(OCTEON_CN38XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                p1          : 1;/**< Set if next_node1 is odd parity */
        uint64_t                next_node1  :17;/**< Next node if an odd character match */
        uint64_t                p0          : 1;/**< Set if next_node0 is odd parity */
        uint64_t                next_node0  :17;/**< Next node if an even character match */
    };
#else
    /* Other chips don't support the deprecated unnamed unions */
#endif
#endif
} cvmx_dfa_node_next_sm_t;

/**
 * This format describes the DFA pointers in large mode
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        cvmx_dfa_node_type_t    type        : 2;/**< Node type */
        uint64_t                mbz2        : 3;/**< Must be zero */
        uint64_t                next_node   :20;/**< Next node */
    } w32;
    struct
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        cvmx_dfa_node_type_t    type        : 2;/**< Node type */
        uint64_t                extra_bits     : 5;/**< bits copied to report (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node_repl : 2;/**< extra replicaiton for next node (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node   :20;/**< Next node ID,  Note, combine with next_node_repl to use as start_node
                                                     for continuation, as in cvmx_dfa_node_next_lgb_t. */
    } w36;
#if defined(ENABLE_DEPRECATED) && !OCTEON_IS_COMMON_BINARY()
#if CVMX_COMPILED_FOR(OCTEON_CN31XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        cvmx_dfa_node_type_t    type        : 2;/**< Node type */
        uint64_t                mbz2        : 3;/**< Must be zero */
        uint64_t                next_node   :20;/**< Next node */
    };
#elif CVMX_COMPILED_FOR(OCTEON_CN38XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        cvmx_dfa_node_type_t    type        : 2;/**< Node type */
        uint64_t                extra_bits     : 5;/**< bits copied to report (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node_repl : 2;/**< extra replicaiton for next node (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node   :20;/**< Next node ID,  Note, combine with next_node_repl to use as start_node
                                                     for continuation, as in cvmx_dfa_node_next_lgb_t. */
    };
#else
    /* Other chips don't support the deprecated unnamed unions */
#endif
#endif
} cvmx_dfa_node_next_lg_t;

/**
 * This format describes the DFA pointers in large mode, another way
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        uint64_t  		type_terminal : 1;/**< Node type */
        uint64_t	        type_marked   : 1;/**< Node type */
        uint64_t                mbz2        : 3;/**< Must be zero */
        uint64_t                next_node   :20;/**< Next node */
    } w32;
    struct
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        uint64_t                type_terminal : 1;/**< Node type */
        uint64_t                type_marked   : 1;/**< Node type */
        uint64_t                extra_bits     : 5;/**< bits copied to report (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node_id_and_repl   :22;/**< Next node ID (and repl for PASS3/CN58XX or repl=0 if not),
                                                                 use this as start node for continuation. */
    } w36;
#if defined(ENABLE_DEPRECATED) && !OCTEON_IS_COMMON_BINARY()
#if CVMX_COMPILED_FOR(OCTEON_CN31XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :32;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        uint64_t  		type_terminal : 1;/**< Node type */
        uint64_t	        type_marked   : 1;/**< Node type */
        uint64_t                mbz2        : 3;/**< Must be zero */
        uint64_t                next_node   :20;/**< Next node */
    };
#elif CVMX_COMPILED_FOR(OCTEON_CN38XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :28;/**< Must be zero */
        uint64_t                ecc         : 7;/**< ECC checksum on the rest of the bits */
        uint64_t                type_terminal : 1;/**< Node type */
        uint64_t                type_marked   : 1;/**< Node type */
        uint64_t                extra_bits     : 5;/**< bits copied to report (PASS3/CN58XX), Must be zero previously */
        uint64_t                next_node_id_and_repl   :22;/**< Next node ID (and repl for PASS3/CN58XX or repl=0 if not),
                                                                 use this as start node for continuation. */
    };
#else
    /* Other chips don't support the deprecated unnamed unions */
#endif
#endif
} cvmx_dfa_node_next_lgb_t;

/**
 * This format describes the DFA pointers in large mode
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t                mbz         :27;/**< Must be zero */
        uint64_t                x0          : 1;/**< XOR of the rest of the bits */
        uint64_t                reserved    : 4;/**< Must be zero */
        uint64_t                data        :32;/**< LLM Data */
    } w32;
    struct
    {
        uint64_t                mbz         :27;/**< Must be zero */
        uint64_t                x0          : 1;/**< XOR of the rest of the bits */
        uint64_t                data        :36;/**< LLM Data */
    } w36;
#if defined(ENABLE_DEPRECATED) && !OCTEON_IS_COMMON_BINARY()
#if CVMX_COMPILED_FOR(OCTEON_CN31XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :27;/**< Must be zero */
        uint64_t                x0          : 1;/**< XOR of the rest of the bits */
        uint64_t                reserved    : 4;/**< Must be zero */
        uint64_t                data        :32;/**< LLM Data */
    };
#elif CVMX_COMPILED_FOR(OCTEON_CN38XX)
    struct /**< @deprecated unnamed reference to members */
    {
        uint64_t                mbz         :27;/**< Must be zero */
        uint64_t                x0          : 1;/**< XOR of the rest of the bits */
        uint64_t                data        :36;/**< LLM Data */
    };
#else
    /* Other chips don't support the deprecated unnamed unions */
#endif
#endif
} cvmx_dfa_node_next_read_t;

/**
 * This structure defines the data format in the low-latency memory
 */
typedef union
{
    uint64_t u64;
    cvmx_dfa_node_next_sm_t     sm;     /**< This format describes the DFA pointers in small mode */
    cvmx_dfa_node_next_lg_t     lg;     /**< This format describes the DFA pointers in large mode */
    cvmx_dfa_node_next_lgb_t    lgb;    /**< This format describes the DFA pointers in large mode, another way */
    cvmx_dfa_node_next_read_t   read;   /**< This format describes the DFA pointers in large mode */
#ifdef ENABLE_DEPRECATED
    cvmx_dfa_node_next_sm_t     s18;    /**< Deprecated */
    cvmx_dfa_node_next_lg_t     s36;    /**< Deprecated */
    cvmx_dfa_node_next_lgb_t    s36b;   /**< Deprecated */
#endif
} cvmx_dfa_node_next_t;

/**
 * These structures define a DFA instruction
 */
typedef union
{
    uint64_t u64[4];
    uint32_t u32;
    struct
    {
        // WORD 0
        uint64_t gxor                   : 8;   /**< Graph XOR value (PASS3/CN58XX), Must be zero for other chips
                                                     or if DFA_CFG[GXOR_ENA] == 0.  */
        uint64_t nxoren                 : 1;   /**< Node XOR enable (PASS3/CN58XX), Must be zero for other chips
                                                     or if DFA_CFG[NXOR_ENA] == 0.  */
        uint64_t nreplen                : 1;   /**< Node Replication mode enable (PASS3/CN58XX), Must be zero for other chips
                                                     or if DFA_CFG[NRPL_ENA] == 0 or IWORD0[Ty] == 0.  */
#if 0
        uint64_t snrepl                 : 2;   /**< Start_Node Replication (PASS3/CN58XX), Must be zero for other chips
                                                     or if DFA_CFG[NRPL_ENA] == 0 or IWORD0[Ty] == 0 or IWORD0[NREPLEN] == 0.  */
        uint64_t start_node_id          : 20;   /**< Node to start the walk from */
#else
        uint64_t start_node             : 22;   /**< Node to start the walk from, includes ID and snrepl, see notes above. */
#endif

        uint64_t unused02               :  2;   /**< Must be zero */
        cvmx_llm_replication_t replication : 2; /**< Type of memory replication to use */
        uint64_t unused03               :  3;   /**< Must be zero */
        cvmx_dfa_graph_type_t type      :  1;   /**< Type of graph */
        uint64_t unused04               :  4;   /**< Must be zero */
        uint64_t base                   : 20;   /**< All tables start on 1KB boundary */

        // WORD 1
        uint64_t input_length           : 16;   /**< In bytes, # pointers in gather case */
        uint64_t use_gather             :  1;   /**< Set to use gather */
        uint64_t no_L2_alloc            :  1;   /**< Set to disable loading of the L2 cache by the DFA */
        uint64_t full_block_write       :  1;   /**< If set, HW can write entire cache blocks @ result_ptr */
        uint64_t little_endian          :  1;   /**< Affects only packet data, not instruction, gather list, or result */
        uint64_t unused1                :  8;   /**< Must be zero */
        uint64_t data_ptr               : 36;   /**< Either directly points to data or the gather list. If gather list,
                                                    data_ptr<2:0> must be zero (i.e. 8B aligned) */
        // WORD 2
        uint64_t max_results            : 16;   /**< in 64-bit quantities, mbz for store */
        uint64_t unused2                : 12;   /**< Must be zero */
        uint64_t result_ptr             : 36;   /**< must be 128 byte aligned */

        // WORD 3
        uint64_t tsize                  :  8;   /**< tsize*256 is the number of terminal nodes for GRAPH_TYPE_SM */
        uint64_t msize                  : 16;   /**< msize is the number of marked nodes for GRAPH_TYPE_SM */
        uint64_t unused3                :  4;   /**< Must be zero */
        uint64_t wq_ptr                 : 36;   /**< 0 for no work queue entry creation */
    } s;
} cvmx_dfa_command_t;

/**
 * Format of the first result word written by the hardware.
 */
typedef union
{
    uint64_t u64;
    struct
    {
        cvmx_dfa_stop_reason_t  reas        : 2;/**< Reason the DFA stopped */
        uint64_t                mbz         :44;/**< Zero */
        uint64_t                last_marked : 1;/**< Set if the last entry written is marked */
        uint64_t                done        : 1;/**< Set to 1 when the DFA completes */
        uint64_t                num_entries :16;/**< Number of result words written */
    } s;
} cvmx_dfa_result0_t;

/**
 * Format of the second result word and subsequent result words written by the hardware.
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t byte_offset    : 16;   /**< Number of bytes consumed */
        uint64_t extra_bits_high:  4;   /**< If PASS3 or CN58XX and DFA_CFG[NRPL_ENA] == 1 and IWORD0[Ty] == 1,
                                             then set to <27:24> of the last next-node pointer. Else set to 0x0.  */
        uint64_t prev_node      : 20;   /**< Index of the previous node */
        uint64_t extra_bits_low :  2;   /**< If PASS3 or CN58XX and DFA_CFG[NRPL_ENA] == 1 and IWORD0[Ty] == 1,
                                             then set to <23:22> of the last next-node pointer. Else set to 0x0.  */
        uint64_t next_node_repl :  2;   /**< If PASS3 or CN58XX and DFA_CFG[NRPL_ENA] == 1 and IWORD0[Ty] == 1, then set
                                             to next_node_repl (<21:20>) of the last next-node pointer. Else set to 0x0.  */
        uint64_t current_node   : 20;   /**< Index of the current node */
    } s;
    struct
    {
        uint64_t byte_offset    : 16;   /**< Number of bytes consumed */
        uint64_t extra_bits_high:  4;   /**< If PASS3 or CN58XX and DFA_CFG[NRPL_ENA] == 1 and IWORD0[Ty] == 1,
                                             then set to <27:24> of the last next-node pointer. Else set to 0x0.  */
        uint64_t prev_node      : 20;   /**< Index of the previous node */
        uint64_t extra_bits_low :  2;   /**< If PASS3 or CN58XX and DFA_CFG[NRPL_ENA] == 1 and IWORD0[Ty] == 1,
                                             then set to <23:22> of the last next-node pointer. Else set to 0x0.  */
        uint64_t curr_id_and_repl:22;   /**< Use ths as start_node for continuation. */
    } s2;
} cvmx_dfa_result1_t;

/**
 * Abstract DFA graph
 */
typedef struct
{
    cvmx_llm_replication_t      replication;        /**< Level of memory replication to use. Must match the LLM setup */
    cvmx_dfa_graph_type_t       type;               /**< Type of graph */
    uint64_t                    base_address;       /**< LLM start address of the graph */
    union {
        struct {
            uint64_t            gxor         : 8;   /**< Graph XOR value (PASS3/CN58XX), Must be zero for other chips
                                                          or if DFA_CFG[GXOR_ENA] == 0.  */
            uint64_t            nxoren       : 1;   /**< Node XOR enable (PASS3/CN58XX), Must be zero for other chips
                                                          or if DFA_CFG[NXOR_ENA] == 0.  */
            uint64_t            nreplen      : 1;   /**< Node Replication mode enable (PASS3/CN58XX), Must be zero for other chips
                                                          or if DFA_CFG[NRPL_ENA] == 0 or IWORD0[Ty] == 0.  */
            uint64_t            snrepl       : 2;   /**< Start_Node Replication (PASS3/CN58XX), Must be zero for other chips
                                                          or if DFA_CFG[NRPL_ENA] == 0 or IWORD0[Ty] == 0 or IWORD0[NREPLEN] == 0.*/
            uint64_t            start_node_id : 20; /**< Start node index for the root of the graph */
        };
        uint32_t                start_node;         /**< Start node index for the root of the graph, incl. snrepl (PASS3/CN58XX)
                                                           NOTE: for backwards compatibility this name includes the the
                                                                 gxor, nxoren, nreplen, and snrepl fields which will all be
                                                                 zero in applicaitons existing before the introduction of these
                                                                 fields, so that existing applicaiton do not need to change. */
    };
    int                         num_terminal_nodes; /**< Number of terminal nodes in the graph. Only needed for small graphs. */
    int                         num_marked_nodes;   /**< Number of marked nodes in the graph. Only needed for small graphs. */
} cvmx_dfa_graph_t;

/**
 * DFA internal global state -- stored in 8 bytes of FAU
 */
typedef union
{
    uint64_t u64;
    struct {
#define CVMX_DFA_STATE_TICKET_BIT_POS 16
#ifdef __BIG_ENDIAN_BITFIELD
	// NOTE:  must clear LSB of base_address_div16 due to ticket overflow
	uint32_t		base_address_div16;  /**< Current DFA instruction queue chunck base address/16 (clear LSB). */
	uint8_t			ticket_loops;	     /**< bits [15:8] of total number of tickets requested. */
	uint8_t			ticket;		     /**< bits [7:0] of total number of tickets requested (current ticket held). */
	// NOTE: index and now_serving are written together
	uint8_t			now_serving;	     /**< current ticket being served (or ready to be served). */
	uint8_t			index;		     /**< index into current chunk: (base_address_div16*16)[index] = next entry. */
#else	// NOTE: little endian mode probably won't work
	uint8_t			index;
	uint8_t			now_serving;
	uint8_t			ticket;
	uint8_t			ticket_loops;
	uint32_t		base_address_div16;
#endif
    } s;
    struct {	// a bitfield version of the same thing to extract base address while clearing carry.
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t		base_address_div32	: 31;	/**< Current DFA instruction queue chunck base address/32. */
	uint64_t		carry			: 1;	/**< Carry out from total_tickets. */
	uint64_t		total_tickets		: 16;	/**< Total tickets. */
	uint64_t		now_serving		: 8 ;	/**< current ticket being served (or ready to be served). */
	uint64_t		index			: 8 ;   /**< index into current chunk. */
#else	// NOTE: little endian mode probably won't work
	uint64_t		index			: 8 ;
	uint64_t		now_serving		: 8 ;
	uint64_t		total_tickets		: 16;
	uint64_t		carry			: 1;
	uint64_t		base_address_div32	: 31;
#endif
    } s2;
} cvmx_dfa_state_t;

/* CSR typedefs have been moved to cvmx-dfa-defs.h */

/**
 * Write a small node edge to LLM.
 *
 * @param graph  Graph to modify
 * @param source_node
 *               Source node for this edge
 * @param match_index
 *               Index into the node edge table. This is the match character/2.
 * @param destination_node0
 *               Destination if the character matches (match_index*2).
 * @param destination_node1
 *               Destination if the character matches (match_index*2+1).
 */
static inline void cvmx_dfa_write_edge_sm(const cvmx_dfa_graph_t *graph,
                                         uint64_t source_node, uint64_t match_index,
                                         uint64_t destination_node0, uint64_t destination_node1)
{
    cvmx_llm_address_t address;
    cvmx_dfa_node_next_t    next_ptr;

    address.u64 = graph->base_address + source_node * CVMX_DFA_NODESM_SIZE + match_index * 4;

    next_ptr.u64 = 0;
    if (OCTEON_IS_MODEL(OCTEON_CN31XX))
    {
        next_ptr.sm.w32.next_node0 = destination_node0;
        next_ptr.sm.w32.p0 = cvmx_llm_parity(destination_node0);

        next_ptr.sm.w32.next_node1 = destination_node1;
        next_ptr.sm.w32.p1 = cvmx_llm_parity(destination_node1);
    }
    else
    {
        next_ptr.sm.w36.next_node0 = destination_node0;
        next_ptr.sm.w36.p0 = cvmx_llm_parity(destination_node0);

        next_ptr.sm.w36.next_node1 = destination_node1;
        next_ptr.sm.w36.p1 = cvmx_llm_parity(destination_node1);
    }

    cvmx_llm_write36(address, next_ptr.u64, 0);
}
#ifdef ENABLE_DEPRECATED
#define cvmx_dfa_write_edge18 cvmx_dfa_write_edge_sm
#endif


/**
 * Write a large node edge to LLM.
 *
 * @param graph  Graph to modify
 * @param source_node
 *               Source node for this edge
 * @param match  Character to match before taking this edge.
 * @param destination_node
 *               Destination node of the edge.
 * @param destination_type
 *               Node type at the end of this edge.
 */
static inline void cvmx_dfa_write_node_lg(const cvmx_dfa_graph_t *graph,
                                         uint64_t source_node, unsigned char match,
                                         uint64_t destination_node, cvmx_dfa_node_type_t destination_type)
{
    cvmx_llm_address_t      address;
    cvmx_dfa_node_next_t    next_ptr;

    address.u64 = graph->base_address + source_node * CVMX_DFA_NODELG_SIZE + (uint64_t)match * 4;

    next_ptr.u64 = 0;
    if (OCTEON_IS_MODEL(OCTEON_CN31XX))
    {
        next_ptr.lg.w32.type = destination_type;
        next_ptr.lg.w32.next_node = destination_node;
        next_ptr.lg.w32.ecc = cvmx_llm_ecc(next_ptr.u64);
    }
    else
    {
        next_ptr.lg.w36.type = destination_type;
        next_ptr.lg.w36.next_node = destination_node;
        next_ptr.lg.w36.ecc = cvmx_llm_ecc(next_ptr.u64);
    }

    cvmx_llm_write36(address, next_ptr.u64, 0);
}
#ifdef ENABLE_DEPRECATED
#define cvmx_dfa_write_node36 cvmx_dfa_write_node_lg
#endif

/**
 * Ring the DFA doorbell telling it that new commands are
 * available.
 *
 * @param num_commands
 *               Number of new commands
 */
static inline void cvmx_dfa_write_doorbell(uint64_t num_commands)
{
    CVMX_SYNCWS;
    cvmx_write_csr(CVMX_DFA_DBELL, num_commands);
}

/**
 * @INTERNAL
 * Write a new command to the DFA. Calls to this function
 * are internally synchronized across all processors, and
 * the doorbell is rung during this function.
 *
 * @param command Command to write
 */

#ifdef CVMX_ENABLE_DFA_FUNCTIONS
static inline void __cvmx_dfa_write_command(cvmx_dfa_command_t *command)
{
    cvmx_dfa_state_t cvmx_dfa_state;
    uint64_t my_ticket;	// needs to wrap to 8 bits
    uint64_t index;
    cvmx_dfa_command_t *head;

    CVMX_PREFETCH0(command);
    // take a ticket.
    cvmx_dfa_state.u64 = cvmx_fau_fetch_and_add64(CVMX_FAU_DFA_STATE, 1ull<<CVMX_DFA_STATE_TICKET_BIT_POS);
    my_ticket = cvmx_dfa_state.s.ticket;

    // see if it is our turn
    while (my_ticket != cvmx_dfa_state.s.now_serving) {
	int delta = my_ticket - cvmx_dfa_state.s.now_serving;
	if (delta < 0) delta += 256;
	cvmx_wait(10*delta);	// reduce polling load on system
	cvmx_dfa_state.u64 = cvmx_fau_fetch_and_add64(CVMX_FAU_DFA_STATE, 0);		// poll for my_ticket==now_serving
    }

    // compute index and instruction queue head pointer
    index = cvmx_dfa_state.s.index;

    // NOTE: the DFA only supports 36-bit addressing
    head = &((CASTPTR(cvmx_dfa_command_t, (cvmx_dfa_state.s2.base_address_div32 * 32ull))[index]));
    head = (cvmx_dfa_command_t*)cvmx_phys_to_ptr(CAST64(head));	// NOTE: since we are not storing bit 63 of address, we must set it now

    // copy the command to the instruction queue
    *head++ = *command;

    // check if a new chunk is needed
    if (cvmx_unlikely((++index >= ((CVMX_FPA_DFA_POOL_SIZE-8)/sizeof(cvmx_dfa_command_t))))) {
        uint64_t *new_base = (uint64_t*)cvmx_fpa_alloc(CVMX_FPA_DFA_POOL);	// could make this async
        if (new_base) {
	    // put the link into the instruction queue's "Next Chunk Buffer Ptr"
            *(uint64_t *)head = cvmx_ptr_to_phys(new_base);
	    // update our state (note 32-bit write to not disturb other fields)
            cvmx_fau_atomic_write32((cvmx_fau_reg_32_t)(CVMX_FAU_DFA_STATE + (CAST64(&cvmx_dfa_state.s.base_address_div16)-CAST64(&cvmx_dfa_state))),
		    (CAST64(new_base))/16);
        }
        else {
            cvmx_dprintf("__cvmx_dfa_write_command: Out of memory. Expect crashes.\n");
        }
	index=0;
    }

    cvmx_dfa_write_doorbell(1);

    // update index and now_serving in the DFA state FAU location (NOTE: this write16 updates to 8-bit values.)
    // NOTE: my_ticket+1 carry out is lost due to write16 and index has already been wrapped to fit in uint8.
    cvmx_fau_atomic_write16((cvmx_fau_reg_16_t)(CVMX_FAU_DFA_STATE+(CAST64(&cvmx_dfa_state.s.now_serving) - CAST64(&cvmx_dfa_state))),
	    ((my_ticket+1)<<8) | index);
}


/**
 * Submit work to the DFA units for processing
 *
 * @param graph   Graph to process
 * @param start_node
 *                The node to start (or continue) walking from
 *                includes. start_node_id and snrepl (PASS3/CN58XX), but gxor,
 *                nxoren, and nreplen are taken from the graph structure
 * @param input   The input to match against
 * @param input_length
 *                The length of the input in bytes
 * @param use_gather
 *		  The input and input_length are of a gather list
 * @param is_little_endian
 *                Set to 1 if the input is in little endian format and must
 *                be swapped before compare.
 * @param result  Location the DFA should put the results in. This must be
 *                an area sized in multiples of a cache line.
 * @param max_results
 *                The maximum number of 64-bit result1 words after result0.
 *                That is, "size of the result area in 64-bit words" - 1.
 *                max_results must be at least 1.
 * @param work    Work queue entry to submit when DFA completes. Can be NULL.
 */
static inline void cvmx_dfa_submit(const cvmx_dfa_graph_t *graph, int start_node,
                                  void *input, int input_length, int use_gather, int is_little_endian,
                                  cvmx_dfa_result0_t *result, int max_results, cvmx_wqe_t *work)
{
    cvmx_dfa_command_t command;

    /* Make sure the result's first 64bit word is zero so we can tell when the
        DFA is done. */
    result->u64 = 0;

    // WORD 0
    command.u64[0] = 0;
    command.s.gxor          = graph->gxor;      // (PASS3/CN58XX)
    command.s.nxoren        = graph->nxoren;    // (PASS3/CN58XX)
    command.s.nreplen       = graph->nreplen;   // (PASS3/CN58XX)
    command.s.start_node    = start_node;       // includes snrepl (PASS3/CN58XX)
    command.s.replication   = graph->replication;
    command.s.type          = graph->type;
    command.s.base          = graph->base_address>>10;

    // WORD 1
    command.u64[1] = 0;
    command.s.input_length  = input_length;
    command.s.use_gather   = use_gather;
    command.s.no_L2_alloc   = 0;
    command.s.full_block_write = 1;
    command.s.little_endian = is_little_endian;
    command.s.data_ptr      = cvmx_ptr_to_phys(input);

    // WORD 2
    command.u64[2] = 0;
    command.s.max_results   = max_results;
    command.s.result_ptr    = cvmx_ptr_to_phys(result);

    // WORD 3
    command.u64[3] = 0;
    if (graph->type == CVMX_DFA_GRAPH_TYPE_SM)
    {
        command.s.tsize     = (graph->num_terminal_nodes + 255) / 256;
        command.s.msize     = graph->num_marked_nodes;
    }
    command.s.wq_ptr        = cvmx_ptr_to_phys(work);

    __cvmx_dfa_write_command(&command);	// NOTE: this does synchronization and rings doorbell
}
#endif

/**
 * DFA gather list element
 */
typedef struct {
    uint64_t length         : 16;   /**< length of piece of data at addr */
    uint64_t reserved       : 12;   /**< reserved, set to 0 */
    uint64_t addr           : 36;   /**< pointer to piece of data */
} cvmx_dfa_gather_entry_t;


/**
 * Check if a DFA has completed processing
 *
 * @param result_ptr Result area the DFA is using
 * @return Non zero if the DFA is done
 */
static inline uint64_t cvmx_dfa_is_done(cvmx_dfa_result0_t *result_ptr)
{
    /* DFA sets the first result 64bit word to non zero when it's done */
    return ((volatile cvmx_dfa_result0_t *)result_ptr)->s.done;
}


#ifdef CVMX_ENABLE_DFA_FUNCTIONS
/**
 * Initialize the DFA hardware before use
 * Returns 0 on success, -1 on failure
 */
int cvmx_dfa_initialize(void);


/**
 * Shutdown and cleanup resources used by the DFA
 */
void cvmx_dfa_shutdown(void);
#endif

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_DFA_H__ */
