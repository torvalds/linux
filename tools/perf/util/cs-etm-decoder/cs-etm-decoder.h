/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Tor Jeremiassen <tor.jeremiassen@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU GEneral Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDE__CS_ETM_DECODER_H__
#define INCLUDE__CS_ETM_DECODER_H__

#include <linux/types.h>
#include <stdio.h>

struct cs_etm_decoder;

struct cs_etm_buffer {
        const unsigned char *buf;
        size_t  len;
        uint64_t offset;
        //bool    consecutive;
        uint64_t        ref_timestamp;
        //uint64_t        trace_nr;
};

enum cs_etm_sample_type {
        CS_ETM_RANGE      = 1 << 0,
};

struct cs_etm_state {
        int err;
        void *data;
        unsigned isa;
        uint64_t start;
        uint64_t end;
        uint64_t timestamp;
};

struct cs_etm_packet {
        enum cs_etm_sample_type sample_type;
        uint64_t start_addr;
        uint64_t end_addr;
        bool     exc;
        bool     exc_ret;
        int cpu;
};


struct cs_etm_queue;
typedef uint32_t (*cs_etm_mem_cb_type)(struct cs_etm_queue *, uint64_t, size_t, uint8_t *);

struct cs_etm_trace_params {
        void *etmv4i_packet_handler;
        uint32_t reg_idr0;
        uint32_t reg_idr1;
        uint32_t reg_idr2;
        uint32_t reg_idr8;
        uint32_t reg_configr;
        uint32_t reg_traceidr;
        int  protocol;
};

struct cs_etm_decoder_params {
        int  operation;
        void (*packet_printer)(const char *);
        cs_etm_mem_cb_type  mem_acc_cb;
        bool formatted;
        bool fsyncs;
        bool hsyncs;
        bool frame_aligned;
        void *data;
};

enum {
        CS_ETM_PROTO_ETMV3 = 1,
        CS_ETM_PROTO_ETMV4i,
        CS_ETM_PROTO_ETMV4d,
};

enum {
        CS_ETM_OPERATION_PRINT = 1,
        CS_ETM_OPERATION_DECODE,
};

enum {
        CS_ETM_ERR_NOMEM = 1,
        CS_ETM_ERR_NODATA,
        CS_ETM_ERR_PARAM,
};


struct cs_etm_decoder *cs_etm_decoder__new(uint32_t num_cpu, struct cs_etm_decoder_params *,struct cs_etm_trace_params []);

int cs_etm_decoder__add_mem_access_cb(struct cs_etm_decoder *, uint64_t, uint64_t, cs_etm_mem_cb_type);

int cs_etm_decoder__flush(struct cs_etm_decoder *);
void cs_etm_decoder__free(struct cs_etm_decoder *);
int cs_etm_decoder__get_packet(struct cs_etm_decoder *, struct cs_etm_packet *);

int cs_etm_decoder__add_bin_file(struct cs_etm_decoder *, uint64_t, uint64_t, uint64_t, const char *);

const struct cs_etm_state *cs_etm_decoder__process_data_block(struct cs_etm_decoder *,
                                       uint64_t,
                                       const uint8_t *,
                                       size_t,
                                       size_t *);

#endif /* INCLUDE__CS_ETM_DECODER_H__ */

