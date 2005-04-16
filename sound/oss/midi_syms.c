/*
 * Exported symbols for midi driver.
 */

#include <linux/module.h>

char midi_syms_symbol;

#include "sound_config.h"
#define _MIDI_SYNTH_C_
#include "midi_synth.h"

EXPORT_SYMBOL(do_midi_msg);
EXPORT_SYMBOL(midi_synth_open);
EXPORT_SYMBOL(midi_synth_close);
EXPORT_SYMBOL(midi_synth_ioctl);
EXPORT_SYMBOL(midi_synth_kill_note);
EXPORT_SYMBOL(midi_synth_start_note);
EXPORT_SYMBOL(midi_synth_set_instr);
EXPORT_SYMBOL(midi_synth_reset);
EXPORT_SYMBOL(midi_synth_hw_control);
EXPORT_SYMBOL(midi_synth_aftertouch);
EXPORT_SYMBOL(midi_synth_controller);
EXPORT_SYMBOL(midi_synth_panning);
EXPORT_SYMBOL(midi_synth_setup_voice);
EXPORT_SYMBOL(midi_synth_send_sysex);
EXPORT_SYMBOL(midi_synth_bender);
EXPORT_SYMBOL(midi_synth_load_patch);
EXPORT_SYMBOL(MIDIbuf_avail);
