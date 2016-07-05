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


struct cs_etm_decoder
{
        void *state;
        int dummy;
};

int cs_etm_decoder__flush(struct cs_etm_decoder *decoder)
{
        (void) decoder;
        return -1;
}

int cs_etm_decoder__add_bin_file(struct cs_etm_decoder *decoder, uint64_t offset, uint64_t address, uint64_t len, const char *fname)
{
        (void) decoder;
        (void) offset;
        (void) address;
        (void) len;
        (void) fname;
        return -1;
}

const struct cs_etm_state *cs_etm_decoder__process_data_block(struct cs_etm_decoder *decoder,
                                       uint64_t indx,
                                       const uint8_t *buf,
                                       size_t len,
                                       size_t *consumed)
{
        (void) decoder;
        (void) indx;
        (void) buf;
        (void) len;
        (void) consumed;
        return NULL;
}

int cs_etm_decoder__add_mem_access_cb(struct cs_etm_decoder *decoder, uint64_t address, uint64_t len, cs_etm_mem_cb_type cb_func)
{
        (void) decoder;
        (void) address;
        (void) len;
        (void) cb_func;
        return -1;
}

int cs_etm_decoder__get_packet(struct cs_etm_decoder *decoder, 
                               struct cs_etm_packet *packet)
{
        (void) decoder;
        (void) packet;
        return -1;
}

struct cs_etm_decoder *cs_etm_decoder__new(uint32_t num_cpu, struct cs_etm_decoder_params *d_params, struct cs_etm_trace_params t_params[])
{
        (void) num_cpu;
        (void) d_params;
        (void) t_params;
        return NULL;
}


void cs_etm_decoder__free(struct cs_etm_decoder *decoder)
{
        (void) decoder;
        return;
}
