/*
 *
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

#include <stdlib.h>

#include "cs-etm-decoder.h"
#include "../util.h"

#include "c_api/rctdl_c_api.h"
#include "rctdl_if_types.h"
#include "etmv4/trc_pkt_types_etmv4.h"

#define MAX_BUFFER 1024 



struct cs_etm_decoder
{
        struct cs_etm_state     state;
        dcd_tree_handle_t       dcd_tree;
        void (*packet_printer)(const char *);
        cs_etm_mem_cb_type      mem_access;
        rctdl_datapath_resp_t   prev_return;
        size_t                  prev_processed;
        bool                    trace_on;
        bool                    discontinuity;
        struct cs_etm_packet    packet_buffer[MAX_BUFFER];
        uint32_t                packet_count;
        uint32_t                head;
        uint32_t                tail;
        uint32_t                end_tail;
};

static uint32_t cs_etm_decoder__mem_access(const void *context,
                                           const rctdl_vaddr_t address,
                                           const rctdl_mem_space_acc_t mem_space,
                                           const uint32_t req_size,
                                           uint8_t *buffer)
{
        struct cs_etm_decoder *decoder = (struct cs_etm_decoder *) context;
        (void) mem_space;

        return decoder->mem_access(decoder->state.data,address,req_size,buffer);
}

static int cs_etm_decoder__gen_etmv4_config(struct cs_etm_trace_params *params,
                                     rctdl_etmv4_cfg *config)
{
        config->reg_configr = params->reg_configr;
        config->reg_traceidr = params->reg_traceidr;
        config->reg_idr0 = params->reg_idr0;
        config->reg_idr1 = params->reg_idr1;
        config->reg_idr2 = params->reg_idr2;
        config->reg_idr8 = params->reg_idr8;

        config->reg_idr9 = 0;
        config->reg_idr10 = 0;
        config->reg_idr11 = 0;
        config->reg_idr12 = 0;
        config->reg_idr13 = 0;
        config->arch_ver = ARCH_V8;
        config->core_prof = profile_CortexA;

        return 0;
}

static int cs_etm_decoder__flush_packet(struct cs_etm_decoder *decoder)
{
        int err = 0;

        if (decoder == NULL) return -1;

        if (decoder->packet_count >= 31) return -1;

        if (decoder->tail != decoder->end_tail) {
                decoder->tail = (decoder->tail + 1) & (MAX_BUFFER - 1);
                decoder->packet_count++;
        }

        return err;
}

int cs_etm_decoder__flush(struct cs_etm_decoder *decoder)
{
        return cs_etm_decoder__flush_packet(decoder);
}

static int cs_etm_decoder__buffer_packet(struct cs_etm_decoder *decoder, const rctdl_generic_trace_elem *elem, enum cs_etm_sample_type sample_type)
{
        int err = 0;
        uint32_t et = 0;

        if (decoder == NULL) return -1;

        if (decoder->packet_count >= 31) return -1;

        err = cs_etm_decoder__flush_packet(decoder);

        if (err) return err;

        et = decoder->end_tail;

        decoder->packet_buffer[et].sample_type = sample_type;
        decoder->packet_buffer[et].start_addr = elem->st_addr;
        decoder->packet_buffer[et].end_addr   = elem->en_addr;
        decoder->packet_buffer[et].exc        = false;
        decoder->packet_buffer[et].exc_ret    = false;
        et = (et + 1) & (MAX_BUFFER - 1);

        decoder->end_tail = et;

        return err;
}

static int cs_etm_decoder__mark_exception(struct cs_etm_decoder *decoder)
{
        int err = 0;

        if (decoder == NULL) return -1;
  
        decoder->packet_buffer[decoder->end_tail].exc = true;

        return err;
}

static int cs_etm_decoder__mark_exception_return(struct cs_etm_decoder *decoder)
{
        int err = 0;

        if (decoder == NULL) return -1;
  
        decoder->packet_buffer[decoder->end_tail].exc_ret = true;
        
        return err;
}

static rctdl_datapath_resp_t cs_etm_decoder__gen_trace_elem_printer(
                        const void *context,
                        const rctdl_trc_index_t indx, 
                        const uint8_t trace_chan_id,
                        const rctdl_generic_trace_elem *elem)
{
        rctdl_datapath_resp_t resp = RCTDL_RESP_CONT;
        struct cs_etm_decoder *decoder = (struct cs_etm_decoder *) context;

        (void) indx;
        (void) trace_chan_id;

        switch (elem->elem_type) {
        case RCTDL_GEN_TRC_ELEM_UNKNOWN:
                break;
        case RCTDL_GEN_TRC_ELEM_NO_SYNC:
                decoder->trace_on = false;
                break;
        case RCTDL_GEN_TRC_ELEM_TRACE_ON:
                decoder->trace_on = true;
                break;
        //case RCTDL_GEN_TRC_ELEM_TRACE_OVERFLOW:
                //decoder->trace_on = false;
                //decoder->discontinuity = true;
                //break;
        case RCTDL_GEN_TRC_ELEM_INSTR_RANGE:
                cs_etm_decoder__buffer_packet(decoder,elem, CS_ETM_RANGE);
                resp = RCTDL_RESP_WAIT;
                break; 
        case RCTDL_GEN_TRC_ELEM_EXCEPTION:
                cs_etm_decoder__mark_exception(decoder);
                break;
        case RCTDL_GEN_TRC_ELEM_EXCEPTION_RET:
                cs_etm_decoder__mark_exception_return(decoder);
                break;
        case RCTDL_GEN_TRC_ELEM_PE_CONTEXT:
        case RCTDL_GEN_TRC_ELEM_EO_TRACE:
        case RCTDL_GEN_TRC_ELEM_ADDR_NACC:
        case RCTDL_GEN_TRC_ELEM_TIMESTAMP:
        case RCTDL_GEN_TRC_ELEM_CYCLE_COUNT:
        //case RCTDL_GEN_TRC_ELEM_TS_WITH_CC:
        case RCTDL_GEN_TRC_ELEM_EVENT:
        default:
            break;
        }

        decoder->state.err = 0;

        return resp;
}

static rctdl_datapath_resp_t cs_etm_decoder__etmv4i_packet_printer(
        const void *context,
        const rctdl_datapath_op_t op,
        const rctdl_trc_index_t indx, 
        const rctdl_etmv4_i_pkt *pkt)
{
        const size_t PACKET_STR_LEN = 1024;
        rctdl_datapath_resp_t ret = RCTDL_RESP_CONT;
        char packet_str[PACKET_STR_LEN];
        size_t offset;
        struct cs_etm_decoder *decoder = (struct cs_etm_decoder *) context;

        sprintf(packet_str,"%ld: ", (long int) indx);
        offset = strlen(packet_str);

        switch(op) {
        case RCTDL_OP_DATA:
                if (rctdl_pkt_str(RCTDL_PROTOCOL_ETMV4I,
                                  (void *)pkt,
                                  packet_str+offset,
                                  PACKET_STR_LEN-offset) != RCTDL_OK)
                        ret = RCTDL_RESP_FATAL_INVALID_PARAM;
                break;
        case RCTDL_OP_EOT:
                sprintf(packet_str,"**** END OF TRACE ****\n");
                break;
        case RCTDL_OP_FLUSH:
        case RCTDL_OP_RESET:
        default:
                break;
        }

        decoder->packet_printer(packet_str);

        return ret;
}
                                            
static int cs_etm_decoder__create_etmv4i_packet_printer(struct cs_etm_decoder_params *d_params, struct cs_etm_trace_params *t_params,

                                                 struct cs_etm_decoder *decoder)
{
        rctdl_etmv4_cfg trace_config;
        int ret = 0;

        if (d_params->packet_printer == NULL) 
                return -1;
 
        ret = cs_etm_decoder__gen_etmv4_config(t_params,&trace_config);

        if (ret != 0) 
                return -1;

        decoder->packet_printer = d_params->packet_printer;

        ret = rctdl_dt_create_etmv4i_pkt_proc(decoder->dcd_tree,
                                              &trace_config,
                                              cs_etm_decoder__etmv4i_packet_printer,
                                              decoder);

        return ret;
}

static int cs_etm_decoder__create_etmv4i_packet_decoder(struct cs_etm_decoder_params *d_params, struct cs_etm_trace_params *t_params, 
                                                 struct cs_etm_decoder *decoder)
{
        rctdl_etmv4_cfg trace_config;
        int ret = 0;
        decoder->packet_printer = d_params->packet_printer;

        ret = cs_etm_decoder__gen_etmv4_config(t_params,&trace_config);

        if (ret != 0)
                return -1;

        ret = rctdl_dt_create_etmv4i_decoder(decoder->dcd_tree,&trace_config);

        if (ret != RCTDL_OK) 
                return -1;

        ret = rctdl_dt_set_gen_elem_outfn(decoder->dcd_tree,
                                              cs_etm_decoder__gen_trace_elem_printer, decoder);
        return ret;
}

int cs_etm_decoder__add_mem_access_cb(struct cs_etm_decoder *decoder, uint64_t address, uint64_t len, cs_etm_mem_cb_type cb_func)
{
        int err;

        decoder->mem_access = cb_func;
        err = rctdl_dt_add_callback_mem_acc(decoder->dcd_tree,
                                            address,
                                            address+len-1,
                                            RCTDL_MEM_SPACE_ANY,
                                            cs_etm_decoder__mem_access,
                                            decoder);
        return err;
}


int cs_etm_decoder__add_bin_file(struct cs_etm_decoder *decoder, uint64_t offset, uint64_t address, uint64_t len, const char *fname)
{
        int err = 0;
        file_mem_region_t region;

        (void) len;
        if (NULL == decoder)
                return -1;

        if (NULL == decoder->dcd_tree)
                return -1;

        region.file_offset = offset;
        region.start_address = address;
        region.region_size = len;
        err = rctdl_dt_add_binfile_region_mem_acc(decoder->dcd_tree,
                                           &region,
                                           1,
                                           RCTDL_MEM_SPACE_ANY,
                                           fname);

        return err;
}

const struct cs_etm_state *cs_etm_decoder__process_data_block(struct cs_etm_decoder *decoder,
                                       uint64_t indx,
                                       const uint8_t *buf,
                                       size_t len,
                                       size_t *consumed)
{
        int ret = 0;
        rctdl_datapath_resp_t dp_ret = decoder->prev_return;
        size_t processed = 0;

        if (decoder->packet_count > 0) {
                decoder->state.err = ret;
                *consumed = processed;
                return &(decoder->state);
        }

        while ((processed < len) && (0 == ret)) {
                
                if (RCTDL_DATA_RESP_IS_CONT(dp_ret)) {
                        uint32_t count;
                        dp_ret = rctdl_dt_process_data(decoder->dcd_tree,
                                                       RCTDL_OP_DATA,
                                                       indx+processed,
                                                       len - processed,
                                                       &buf[processed],
                                                       &count);
                        processed += count;

                } else if (RCTDL_DATA_RESP_IS_WAIT(dp_ret)) {
                        dp_ret = rctdl_dt_process_data(decoder->dcd_tree,
                                                       RCTDL_OP_FLUSH,
                                                       0,
                                                       0,
                                                       NULL,
                                                       NULL);
                        break;
                } else {
                        ret = -1;
                }
        }
        if (RCTDL_DATA_RESP_IS_WAIT(dp_ret)) {
                if (RCTDL_DATA_RESP_IS_CONT(decoder->prev_return)) {
                        decoder->prev_processed = processed;
                }
                processed = 0;
        } else if (RCTDL_DATA_RESP_IS_WAIT(decoder->prev_return)) {
                processed = decoder->prev_processed;
                decoder->prev_processed = 0;
        }
        *consumed = processed;
        decoder->prev_return = dp_ret;
        decoder->state.err = ret;
        return &(decoder->state);
}

int cs_etm_decoder__get_packet(struct cs_etm_decoder *decoder, 
                               struct cs_etm_packet *packet)
{
        if (decoder->packet_count == 0) return -1;

        if (packet == NULL) return -1;

        *packet = decoder->packet_buffer[decoder->head];

        decoder->head = (decoder->head + 1) & (MAX_BUFFER - 1);

        decoder->packet_count--;

        return 0;
}

static void cs_etm_decoder__clear_buffer(struct cs_etm_decoder *decoder)
{
        unsigned i;

        decoder->head = 0;
        decoder->tail = 0;
        decoder->end_tail = 0;
        decoder->packet_count = 0;
        for (i = 0; i < MAX_BUFFER; i++) {
                decoder->packet_buffer[i].start_addr = 0xdeadbeefdeadbeefUL;
                decoder->packet_buffer[i].end_addr   = 0xdeadbeefdeadbeefUL;
                decoder->packet_buffer[i].exc        = false;
                decoder->packet_buffer[i].exc_ret    = false;
        }
}

struct cs_etm_decoder *cs_etm_decoder__new(uint32_t num_cpu, struct cs_etm_decoder_params *d_params, struct cs_etm_trace_params t_params[])
{
        struct cs_etm_decoder *decoder;
        rctdl_dcd_tree_src_t format;
        uint32_t flags;
        int ret;
        size_t i;

        if ((t_params == NULL) || (d_params == 0)) {
                return NULL;
        }

        decoder = zalloc(sizeof(struct cs_etm_decoder));

        if (decoder == NULL) {
                return NULL;
        }

        decoder->state.data = d_params->data;
        decoder->prev_return = RCTDL_RESP_CONT;
        cs_etm_decoder__clear_buffer(decoder);
        format = (d_params->formatted ? RCTDL_TRC_SRC_FRAME_FORMATTED :
                                         RCTDL_TRC_SRC_SINGLE);
        flags = 0;
        flags |= (d_params->fsyncs ? RCTDL_DFRMTR_HAS_FSYNCS : 0);
        flags |= (d_params->hsyncs ? RCTDL_DFRMTR_HAS_HSYNCS : 0);
        flags |= (d_params->frame_aligned ? RCTDL_DFRMTR_FRAME_MEM_ALIGN : 0);

        /* Create decode tree for the data source */
        decoder->dcd_tree = rctdl_create_dcd_tree(format,flags);

        if (decoder->dcd_tree == 0) {
                goto err_free_decoder;
        }

        for (i = 0; i < num_cpu; ++i) {
                switch (t_params[i].protocol)
                {
                        case CS_ETM_PROTO_ETMV4i: 
                                if (d_params->operation == CS_ETM_OPERATION_PRINT) {
                                        ret = cs_etm_decoder__create_etmv4i_packet_printer(d_params,&t_params[i],decoder);
                                } else if (d_params->operation == CS_ETM_OPERATION_DECODE) {
                                        ret = cs_etm_decoder__create_etmv4i_packet_decoder(d_params,&t_params[i],decoder); 
                                } else {
                                        ret = -CS_ETM_ERR_PARAM;
                                }
                                if (ret != 0) {
                                        goto err_free_decoder_tree;
                                }
                                break;
                        default:
                                goto err_free_decoder_tree;
                                break;
                }
        }


        return decoder;

err_free_decoder_tree:
        rctdl_destroy_dcd_tree(decoder->dcd_tree);
err_free_decoder:
        free(decoder);
        return NULL;
}


void cs_etm_decoder__free(struct cs_etm_decoder *decoder)
{
        if (decoder == NULL) return;

        rctdl_destroy_dcd_tree(decoder->dcd_tree);
        decoder->dcd_tree = NULL;

        free(decoder);
}
