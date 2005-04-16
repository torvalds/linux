/*
 * Exported symbols for audio driver.
 */

#include <linux/module.h>

char audio_syms_symbol;

#include "sound_config.h"
#include "sound_calls.h"

EXPORT_SYMBOL(DMAbuf_start_dma);
EXPORT_SYMBOL(DMAbuf_open_dma);
EXPORT_SYMBOL(DMAbuf_close_dma);
EXPORT_SYMBOL(DMAbuf_inputintr);
EXPORT_SYMBOL(DMAbuf_outputintr);
