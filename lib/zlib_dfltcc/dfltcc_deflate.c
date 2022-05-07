// SPDX-License-Identifier: Zlib

#include "../zlib_deflate/defutil.h"
#include "dfltcc_util.h"
#include "dfltcc.h"
#include <asm/setup.h>
#include <linux/zutil.h>

/*
 * Compress.
 */
int dfltcc_can_deflate(
    z_streamp strm
)
{
    deflate_state *state = (deflate_state *)strm->state;
    struct dfltcc_state *dfltcc_state = GET_DFLTCC_STATE(state);

    /* Check for kernel dfltcc command line parameter */
    if (zlib_dfltcc_support == ZLIB_DFLTCC_DISABLED ||
            zlib_dfltcc_support == ZLIB_DFLTCC_INFLATE_ONLY)
        return 0;

    /* Unsupported compression settings */
    if (!dfltcc_are_params_ok(state->level, state->w_bits, state->strategy,
                              dfltcc_state->level_mask))
        return 0;

    /* Unsupported hardware */
    if (!is_bit_set(dfltcc_state->af.fns, DFLTCC_GDHT) ||
            !is_bit_set(dfltcc_state->af.fns, DFLTCC_CMPR) ||
            !is_bit_set(dfltcc_state->af.fmts, DFLTCC_FMT0))
        return 0;

    return 1;
}

static void dfltcc_gdht(
    z_streamp strm
)
{
    deflate_state *state = (deflate_state *)strm->state;
    struct dfltcc_param_v0 *param = &GET_DFLTCC_STATE(state)->param;
    size_t avail_in = avail_in = strm->avail_in;

    dfltcc(DFLTCC_GDHT,
           param, NULL, NULL,
           &strm->next_in, &avail_in, NULL);
}

static dfltcc_cc dfltcc_cmpr(
    z_streamp strm
)
{
    deflate_state *state = (deflate_state *)strm->state;
    struct dfltcc_param_v0 *param = &GET_DFLTCC_STATE(state)->param;
    size_t avail_in = strm->avail_in;
    size_t avail_out = strm->avail_out;
    dfltcc_cc cc;

    cc = dfltcc(DFLTCC_CMPR | HBT_CIRCULAR,
                param, &strm->next_out, &avail_out,
                &strm->next_in, &avail_in, state->window);
    strm->total_in += (strm->avail_in - avail_in);
    strm->total_out += (strm->avail_out - avail_out);
    strm->avail_in = avail_in;
    strm->avail_out = avail_out;
    return cc;
}

static void send_eobs(
    z_streamp strm,
    const struct dfltcc_param_v0 *param
)
{
    deflate_state *state = (deflate_state *)strm->state;

    zlib_tr_send_bits(
          state,
          bi_reverse(param->eobs >> (15 - param->eobl), param->eobl),
          param->eobl);
    flush_pending(strm);
    if (state->pending != 0) {
        /* The remaining data is located in pending_out[0:pending]. If someone
         * calls put_byte() - this might happen in deflate() - the byte will be
         * placed into pending_buf[pending], which is incorrect. Move the
         * remaining data to the beginning of pending_buf so that put_byte() is
         * usable again.
         */
        memmove(state->pending_buf, state->pending_out, state->pending);
        state->pending_out = state->pending_buf;
    }
#ifdef ZLIB_DEBUG
    state->compressed_len += param->eobl;
#endif
}

int dfltcc_deflate(
    z_streamp strm,
    int flush,
    block_state *result
)
{
    deflate_state *state = (deflate_state *)strm->state;
    struct dfltcc_state *dfltcc_state = GET_DFLTCC_STATE(state);
    struct dfltcc_param_v0 *param = &dfltcc_state->param;
    uInt masked_avail_in;
    dfltcc_cc cc;
    int need_empty_block;
    int soft_bcc;
    int no_flush;

    if (!dfltcc_can_deflate(strm))
        return 0;

again:
    masked_avail_in = 0;
    soft_bcc = 0;
    no_flush = flush == Z_NO_FLUSH;

    /* Trailing empty block. Switch to software, except when Continuation Flag
     * is set, which means that DFLTCC has buffered some output in the
     * parameter block and needs to be called again in order to flush it.
     */
    if (flush == Z_FINISH && strm->avail_in == 0 && !param->cf) {
        if (param->bcf) {
            /* A block is still open, and the hardware does not support closing
             * blocks without adding data. Thus, close it manually.
             */
            send_eobs(strm, param);
            param->bcf = 0;
        }
        return 0;
    }

    if (strm->avail_in == 0 && !param->cf) {
        *result = need_more;
        return 1;
    }

    /* There is an open non-BFINAL block, we are not going to close it just
     * yet, we have compressed more than DFLTCC_BLOCK_SIZE bytes and we see
     * more than DFLTCC_DHT_MIN_SAMPLE_SIZE bytes. Open a new block with a new
     * DHT in order to adapt to a possibly changed input data distribution.
     */
    if (param->bcf && no_flush &&
            strm->total_in > dfltcc_state->block_threshold &&
            strm->avail_in >= dfltcc_state->dht_threshold) {
        if (param->cf) {
            /* We need to flush the DFLTCC buffer before writing the
             * End-of-block Symbol. Mask the input data and proceed as usual.
             */
            masked_avail_in += strm->avail_in;
            strm->avail_in = 0;
            no_flush = 0;
        } else {
            /* DFLTCC buffer is empty, so we can manually write the
             * End-of-block Symbol right away.
             */
            send_eobs(strm, param);
            param->bcf = 0;
            dfltcc_state->block_threshold =
                strm->total_in + dfltcc_state->block_size;
            if (strm->avail_out == 0) {
                *result = need_more;
                return 1;
            }
        }
    }

    /* The caller gave us too much data. Pass only one block worth of
     * uncompressed data to DFLTCC and mask the rest, so that on the next
     * iteration we start a new block.
     */
    if (no_flush && strm->avail_in > dfltcc_state->block_size) {
        masked_avail_in += (strm->avail_in - dfltcc_state->block_size);
        strm->avail_in = dfltcc_state->block_size;
    }

    /* When we have an open non-BFINAL deflate block and caller indicates that
     * the stream is ending, we need to close an open deflate block and open a
     * BFINAL one.
     */
    need_empty_block = flush == Z_FINISH && param->bcf && !param->bhf;

    /* Translate stream to parameter block */
    param->cvt = CVT_ADLER32;
    if (!no_flush)
        /* We need to close a block. Always do this in software - when there is
         * no input data, the hardware will not nohor BCC. */
        soft_bcc = 1;
    if (flush == Z_FINISH && !param->bcf)
        /* We are about to open a BFINAL block, set Block Header Final bit
         * until the stream ends.
         */
        param->bhf = 1;
    /* DFLTCC-CMPR will write to next_out, so make sure that buffers with
     * higher precedence are empty.
     */
    Assert(state->pending == 0, "There must be no pending bytes");
    Assert(state->bi_valid < 8, "There must be less than 8 pending bits");
    param->sbb = (unsigned int)state->bi_valid;
    if (param->sbb > 0)
        *strm->next_out = (Byte)state->bi_buf;
    if (param->hl)
        param->nt = 0; /* Honor history */
    param->cv = strm->adler;

    /* When opening a block, choose a Huffman-Table Type */
    if (!param->bcf) {
        if (strm->total_in == 0 && dfltcc_state->block_threshold > 0) {
            param->htt = HTT_FIXED;
        }
        else {
            param->htt = HTT_DYNAMIC;
            dfltcc_gdht(strm);
        }
    }

    /* Deflate */
    do {
        cc = dfltcc_cmpr(strm);
        if (strm->avail_in < 4096 && masked_avail_in > 0)
            /* We are about to call DFLTCC with a small input buffer, which is
             * inefficient. Since there is masked data, there will be at least
             * one more DFLTCC call, so skip the current one and make the next
             * one handle more data.
             */
            break;
    } while (cc == DFLTCC_CC_AGAIN);

    /* Translate parameter block to stream */
    strm->msg = oesc_msg(dfltcc_state->msg, param->oesc);
    state->bi_valid = param->sbb;
    if (state->bi_valid == 0)
        state->bi_buf = 0; /* Avoid accessing next_out */
    else
        state->bi_buf = *strm->next_out & ((1 << state->bi_valid) - 1);
    strm->adler = param->cv;

    /* Unmask the input data */
    strm->avail_in += masked_avail_in;
    masked_avail_in = 0;

    /* If we encounter an error, it means there is a bug in DFLTCC call */
    Assert(cc != DFLTCC_CC_OP2_CORRUPT || param->oesc == 0, "BUG");

    /* Update Block-Continuation Flag. It will be used to check whether to call
     * GDHT the next time.
     */
    if (cc == DFLTCC_CC_OK) {
        if (soft_bcc) {
            send_eobs(strm, param);
            param->bcf = 0;
            dfltcc_state->block_threshold =
                strm->total_in + dfltcc_state->block_size;
        } else
            param->bcf = 1;
        if (flush == Z_FINISH) {
            if (need_empty_block)
                /* Make the current deflate() call also close the stream */
                return 0;
            else {
                bi_windup(state);
                *result = finish_done;
            }
        } else {
            if (flush == Z_FULL_FLUSH)
                param->hl = 0; /* Clear history */
            *result = flush == Z_NO_FLUSH ? need_more : block_done;
        }
    } else {
        param->bcf = 1;
        *result = need_more;
    }
    if (strm->avail_in != 0 && strm->avail_out != 0)
        goto again; /* deflate() must use all input or all output */
    return 1;
}
