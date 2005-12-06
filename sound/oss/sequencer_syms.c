/*
 * Exported symbols for sequencer driver.
 */

#include <linux/module.h>

char sequencer_syms_symbol;

#include "sound_config.h"
#include "sound_calls.h"

EXPORT_SYMBOL(note_to_freq);
EXPORT_SYMBOL(compute_finetune);
EXPORT_SYMBOL(seq_copy_to_input);
EXPORT_SYMBOL(seq_input_event);
EXPORT_SYMBOL(sequencer_init);
EXPORT_SYMBOL(sequencer_timer);

EXPORT_SYMBOL(sound_timer_init);
EXPORT_SYMBOL(sound_timer_interrupt);
EXPORT_SYMBOL(sound_timer_syncinterval);

/* Tuning */

#define _SEQUENCER_C_
#include "tuning.h"

EXPORT_SYMBOL(cent_tuning);
EXPORT_SYMBOL(semitone_tuning);
