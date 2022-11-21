// SPDX-License-Identifier: Zlib
/* dfltcc.c - SystemZ DEFLATE CONVERSION CALL support. */

#include <linux/export.h>
#include <linux/module.h>
#include "dfltcc_util.h"
#include "dfltcc.h"

char *oesc_msg(
    char *buf,
    int oesc
)
{
    if (oesc == 0x00)
        return NULL; /* Successful completion */
    else {
#ifdef STATIC
        return NULL; /* Ignore for pre-boot decompressor */
#else
        sprintf(buf, "Operation-Ending-Supplemental Code is 0x%.2X", oesc);
        return buf;
#endif
    }
}

void dfltcc_reset(
    z_streamp strm,
    uInt size
)
{
    struct dfltcc_state *dfltcc_state =
        (struct dfltcc_state *)((char *)strm->state + size);
    struct dfltcc_qaf_param *param =
        (struct dfltcc_qaf_param *)&dfltcc_state->param;

    /* Initialize available functions */
    if (is_dfltcc_enabled()) {
        dfltcc(DFLTCC_QAF, param, NULL, NULL, NULL, NULL, NULL);
        memmove(&dfltcc_state->af, param, sizeof(dfltcc_state->af));
    } else
        memset(&dfltcc_state->af, 0, sizeof(dfltcc_state->af));

    /* Initialize parameter block */
    memset(&dfltcc_state->param, 0, sizeof(dfltcc_state->param));
    dfltcc_state->param.nt = 1;

    /* Initialize tuning parameters */
    if (zlib_dfltcc_support == ZLIB_DFLTCC_FULL_DEBUG)
        dfltcc_state->level_mask = DFLTCC_LEVEL_MASK_DEBUG;
    else
        dfltcc_state->level_mask = DFLTCC_LEVEL_MASK;
    dfltcc_state->block_size = DFLTCC_BLOCK_SIZE;
    dfltcc_state->block_threshold = DFLTCC_FIRST_FHT_BLOCK_SIZE;
    dfltcc_state->dht_threshold = DFLTCC_DHT_MIN_SAMPLE_SIZE;
    dfltcc_state->param.ribm = DFLTCC_RIBM;
}
EXPORT_SYMBOL(dfltcc_reset);

MODULE_LICENSE("GPL");
