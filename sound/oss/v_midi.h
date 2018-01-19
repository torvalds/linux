/* SPDX-License-Identifier: GPL-2.0 */
typedef struct vmidi_devc {
	   int dev;

	/* State variables */
	   int opened;
	   spinlock_t lock;

	/* MIDI fields */
	   int my_mididev;
	   int pair_mididev;
	   int input_opened;
	   int intr_active;
	   void (*midi_input_intr) (int dev, unsigned char data);
	} vmidi_devc;
