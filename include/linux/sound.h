
/*
 * Minor numbers for the sound driver.
 */

#include <linux/fs.h>

#define SND_DEV_CTL		0	/* Control port /dev/mixer */
#define SND_DEV_SEQ		1	/* Sequencer output /dev/sequencer (FM
						synthesizer and MIDI output) */
#define SND_DEV_MIDIN		2	/* Raw midi access */
#define SND_DEV_DSP		3	/* Digitized voice /dev/dsp */
#define SND_DEV_AUDIO		4	/* Sparc compatible /dev/audio */
#define SND_DEV_DSP16		5	/* Like /dev/dsp but 16 bits/sample */
/* #define SND_DEV_STATUS	6 */	/* /dev/sndstat (obsolete) */
#define SND_DEV_UNUSED		6
#define SND_DEV_AWFM		7	/* Reserved */
#define SND_DEV_SEQ2		8	/* /dev/sequencer, level 2 interface */
/* #define SND_DEV_SNDPROC	9 */	/* /dev/sndproc for programmable devices (not used) */
/* #define SND_DEV_DMMIDI	9 */
#define SND_DEV_SYNTH		9	/* Raw synth access /dev/synth (same as /dev/dmfm) */
#define SND_DEV_DMFM		10	/* Raw synth access /dev/dmfm */
#define SND_DEV_UNKNOWN11	11
#define SND_DEV_ADSP		12	/* Like /dev/dsp (obsolete) */
#define SND_DEV_AMIDI		13	/* Like /dev/midi (obsolete) */
#define SND_DEV_ADMMIDI		14	/* Like /dev/dmmidi (onsolete) */

#ifdef __KERNEL__
/*
 *	Sound core interface functions
 */
 
struct device;
extern int register_sound_special(const struct file_operations *fops, int unit);
extern int register_sound_special_device(const struct file_operations *fops, int unit, struct device *dev);
extern int register_sound_mixer(const struct file_operations *fops, int dev);
extern int register_sound_midi(const struct file_operations *fops, int dev);
extern int register_sound_dsp(const struct file_operations *fops, int dev);

extern void unregister_sound_special(int unit);
extern void unregister_sound_mixer(int unit);
extern void unregister_sound_midi(int unit);
extern void unregister_sound_dsp(int unit);
#endif /* __KERNEL__ */
