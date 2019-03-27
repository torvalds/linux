/*
 * Copyright (c) 2013-2016 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * File: ql_minidump.h
 *
 * $FreeBSD$
 */
#ifndef _QL_MINIDUMP_H_
#define _QL_MINIDUMP_H_

#define QL_DBG_STATE_ARRAY_LEN          16
#define QL_DBG_CAP_SIZE_ARRAY_LEN       8
#define QL_NO_OF_OCM_WINDOWS            16


typedef struct ql_mdump_tmplt_hdr {
        uint32_t  entry_type ;
        uint32_t  first_entry_offset ;
        uint32_t  size_of_template ;
        uint32_t  recommended_capture_mask;

        uint32_t  num_of_entries ;
        uint32_t  version ;
        uint32_t  driver_timestamp ;
        uint32_t  checksum ;

        uint32_t  driver_capture_mask ;
        uint32_t  driver_info_word2 ;
        uint32_t  driver_info_word3 ;
        uint32_t  driver_info_word4 ;

        uint32_t  saved_state_array[QL_DBG_STATE_ARRAY_LEN] ;
        uint32_t  capture_size_array[QL_DBG_CAP_SIZE_ARRAY_LEN] ;

        uint32_t ocm_window_array[QL_NO_OF_OCM_WINDOWS] ;
} ql_minidump_template_hdr_t ;

/*
 * MIU AGENT ADDRESSES.
 */

#define MD_TA_CTL_ENABLE                0x2
#define MD_TA_CTL_START                 0x1
#define MD_TA_CTL_BUSY                  0x8
#define MD_TA_CTL_CHECK                 1000

#define MD_MIU_TEST_AGT_CTRL            0x41000090
#define MD_MIU_TEST_AGT_ADDR_LO         0x41000094
#define MD_MIU_TEST_AGT_ADDR_HI         0x41000098

#define MD_MIU_TEST_AGT_RDDATA_0_31     0x410000A8
#define MD_MIU_TEST_AGT_RDDATA_32_63    0x410000AC
#define MD_MIU_TEST_AGT_RDDATA_64_95    0x410000B8
#define MD_MIU_TEST_AGT_RDDATA_96_127   0x410000BC

#define MD_MIU_TEST_AGT_WRDATA_0_31     0x410000A0
#define MD_MIU_TEST_AGT_WRDATA_32_63    0x410000A4
#define MD_MIU_TEST_AGT_WRDATA_64_95    0x410000B0
#define MD_MIU_TEST_AGT_WRDATA_96_127   0x410000B4

/*
 * ROM Read Address
 */

#define MD_DIRECT_ROM_WINDOW            0x42110030
#define MD_DIRECT_ROM_READ_BASE         0x42150000

/*
 * Entry Type Defines
 */

#define RDNOP			0
#define RDCRB			1
#define	RDMUX			2
#define QUEUE			3
#define BOARD			4
#define RDOCM			6
#define L1DAT			11
#define L1INS			12
#define L2DTG                  	21
#define L2ITG                  	22
#define L2DAT                  	23
#define L2INS                  	24
#define POLLRD                  35
#define RDMUX2                  36
#define POLLRDMWR               37
#define RDROM                  	71
#define RDMEM                  	72
#define CNTRL                  	98
#define TLHDR                  	99
#define RDEND			255

/*
 * Index of State Table.  The Template header maintains
 * an array of 8 (0..7) words that is used to store some
 * "State Information" from the board.
 */

#define QL_PCIE_FUNC_INDX       0
#define QL_CLK_STATE_INDX       1
#define QL_SRE_STATE_INDX       2
#define QL_OCM0_ADDR_INDX       3

#define QL_REVID_STATE_INDX     4
#define QL_MAJVER_STATE_INDX    5
#define QL_MINVER_STATE_INDX    6
#define QL_SUBVER_STATE_INDX    7

/*
 * Opcodes for Control Entries.
 * These Flags are bit fields.
 */

#define QL_DBG_OPCODE_WR        0x01
#define QL_DBG_OPCODE_RW        0x02
#define QL_DBG_OPCODE_AND       0x04
#define QL_DBG_OPCODE_OR        0x08
#define QL_DBG_OPCODE_POLL      0x10
#define QL_DBG_OPCODE_RDSTATE   0x20
#define QL_DBG_OPCODE_WRSTATE   0x40
#define QL_DBG_OPCODE_MDSTATE   0x80

typedef struct ql_minidump_entry_hdr_s {
        uint32_t      entry_type ;
        uint32_t      entry_size ;
        uint32_t      entry_capture_size ;
    	union {
        	struct {
            		uint8_t   entry_capture_mask ;
            		uint8_t   entry_code ;
            		uint8_t   driver_code ;
            		uint8_t   driver_flags ;
        	};
        	uint32_t entry_ctrl_word ;
    	};
} ql_minidump_entry_hdr_t ;

/*
 * Driver Flags
 */
#define QL_DBG_SKIPPED_FLAG	0x80 /*  driver skipped this entry  */
#define QL_DBG_SIZE_ERR_FLAG    0x40 /*  entry size vs capture size mismatch*/

/*
 * Generic Entry Including Header
 */

typedef struct ql_minidump_entry_s {
        ql_minidump_entry_hdr_t hdr ;

    uint32_t entry_data00 ;
    uint32_t entry_data01 ;
    uint32_t entry_data02 ;
    uint32_t entry_data03 ;

    uint32_t entry_data04 ;
    uint32_t entry_data05 ;
    uint32_t entry_data06 ;
    uint32_t entry_data07 ;
} ql_minidump_entry_t;

/*
 *  Read CRB Entry Header
 */

typedef struct ql_minidump_entry_rdcrb_s {
        ql_minidump_entry_hdr_t h;

        uint32_t addr ;
    union {
        struct {
            uint8_t  addr_stride ;
            uint8_t  rsvd_0;
            uint16_t rsvd_1 ;
        } ;
            uint32_t addr_cntrl  ;
    } ;

        uint32_t data_size ;
        uint32_t op_count;

    uint32_t    rsvd_2 ;
    uint32_t    rsvd_3 ;
    uint32_t    rsvd_4 ;
    uint32_t    rsvd_5 ;

} ql_minidump_entry_rdcrb_t ;

/*
 * Cache Entry Header
 */

typedef struct ql_minidump_entry_cache_s {
        ql_minidump_entry_hdr_t h;

        uint32_t tag_reg_addr ;
    	union {
        	struct {
            		uint16_t   tag_value_stride ;
            		uint16_t  init_tag_value ;
        	} ;
            	uint32_t select_addr_cntrl  ;
    	} ;

        uint32_t data_size ;
        uint32_t op_count;

    	uint32_t control_addr ;
    	union {
        	struct {
            		uint16_t  write_value ;
            		uint8_t   poll_mask ;
            		uint8_t   poll_wait ;
        	};
        	uint32_t control_value ;
    	} ;

    	uint32_t read_addr ;
    	union {
        	struct {
            		uint8_t   read_addr_stride ;
            		uint8_t   read_addr_cnt ;
            		uint16_t  rsvd_1 ;
        	} ;
            	uint32_t read_addr_cntrl  ;
    	} ;
} ql_minidump_entry_cache_t ;


/*
 * Read OCM Entry Header
 */

typedef struct ql_minidump_entry_rdocm_s {
        ql_minidump_entry_hdr_t h;

        uint32_t rsvd_0 ;
        uint32_t rsvd_1 ;

        uint32_t data_size ;
        uint32_t op_count;

    uint32_t rsvd_2 ;
    uint32_t rsvd_3 ;

    uint32_t read_addr ;
    uint32_t read_addr_stride ;

} ql_minidump_entry_rdocm_t ;

/*
 * Read MEM Entry Header
 */

typedef struct ql_minidump_entry_rdmem_s {
        ql_minidump_entry_hdr_t h;

    uint32_t rsvd_0[6] ;

    uint32_t read_addr ;
    uint32_t read_data_size ;

} ql_minidump_entry_rdmem_t ;

/*
 * Read ROM Entry Header
 */

typedef struct ql_minidump_entry_rdrom_s {
        ql_minidump_entry_hdr_t h;

    uint32_t rsvd_0[6] ;

    uint32_t read_addr ;
    uint32_t read_data_size ;

} ql_minidump_entry_rdrom_t ;

/*
 * Read MUX Entry Header
 */

typedef struct ql_minidump_entry_mux_s {
        ql_minidump_entry_hdr_t h;

        uint32_t select_addr ;
    union {
        struct {
            uint32_t rsvd_0 ;
        } ;
            uint32_t select_addr_cntrl  ;
    } ;

        uint32_t data_size ;
        uint32_t op_count;

    uint32_t select_value ;
    uint32_t select_value_stride ;

    uint32_t read_addr ;
    uint32_t rsvd_1 ;

} ql_minidump_entry_mux_t ;

/*
 * Read MUX2 Entry Header
 */

typedef struct ql_minidump_entry_mux2_s {
        ql_minidump_entry_hdr_t h;

        uint32_t select_addr_1;
        uint32_t select_addr_2;
        uint32_t select_value_1;
        uint32_t select_value_2;
        uint32_t select_value_count;
        uint32_t select_value_mask;
        uint32_t read_addr;
        union {
                struct {
                        uint8_t select_value_stride;
                        uint8_t data_size;
                        uint8_t reserved_0;
                        uint8_t reserved_1;
                };
                uint32_t select_addr_value_cntrl;
        };

} ql_minidump_entry_mux2_t;

/*
 * Read QUEUE Entry Header
 */

typedef struct ql_minidump_entry_queue_s {
        ql_minidump_entry_hdr_t h;

        uint32_t select_addr ;
    union {
        struct {
            uint16_t  queue_id_stride ;
            uint16_t  rsvd_0 ;
        } ;
            uint32_t select_addr_cntrl  ;
    } ;

        uint32_t data_size ;
        uint32_t op_count ;

    uint32_t rsvd_1 ;
    uint32_t rsvd_2 ;

    uint32_t read_addr ;
    union {
        struct {
            uint8_t   read_addr_stride ;
            uint8_t   read_addr_cnt ;
            uint16_t  rsvd_3 ;
        } ;
            uint32_t read_addr_cntrl  ;
    } ;

} ql_minidump_entry_queue_t ;

/*
 * Control Entry Header
 */

typedef struct ql_minidump_entry_cntrl_s {
        ql_minidump_entry_hdr_t h;

        uint32_t addr ;
    union {
        struct {
            uint8_t  addr_stride ;
            uint8_t  state_index_a ;
            uint16_t poll_timeout ;
        } ;
            uint32_t addr_cntrl  ;
    } ;

        uint32_t data_size ;
        uint32_t op_count;

    union {
        struct {
            uint8_t opcode ;
            uint8_t state_index_v ;
            uint8_t shl ;
            uint8_t shr ;
        } ;
        uint32_t control_value ;
    } ;

    uint32_t value_1 ;
    uint32_t value_2 ;
    uint32_t value_3 ;
} ql_minidump_entry_cntrl_t ;

/*
 * Read with poll.
 */

typedef struct ql_minidump_entry_rdcrb_with_poll_s {
        ql_minidump_entry_hdr_t h;

        uint32_t select_addr;
        uint32_t read_addr;
        uint32_t select_value;
        union {
                struct {
                        uint16_t select_value_stride;
                        uint16_t op_count;
                };
                uint32_t select_value_cntrl;
        };

        uint32_t poll;
        uint32_t mask;

        uint32_t data_size;
        uint32_t rsvd_0;

} ql_minidump_entry_pollrd_t;

/*
 * Read_Modify_Write with poll.
 */

typedef struct ql_minidump_entry_rd_modify_wr_with_poll_s {
        ql_minidump_entry_hdr_t h;

        uint32_t addr_1;
        uint32_t addr_2;
        uint32_t value_1;
        uint32_t value_2;
        uint32_t poll;
        uint32_t mask;
        uint32_t modify_mask;
        uint32_t data_size;

} ql_minidump_entry_rd_modify_wr_with_poll_t;

#endif /* #ifndef _QL_MINIDUMP_H_ */

