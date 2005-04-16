/*
 *	DMA buffer calls
 */

int DMAbuf_open(int dev, int mode);
int DMAbuf_release(int dev, int mode);
int DMAbuf_getwrbuffer(int dev, char **buf, int *size, int dontblock);
int DMAbuf_getrdbuffer(int dev, char **buf, int *len, int dontblock);
int DMAbuf_rmchars(int dev, int buff_no, int c);
int DMAbuf_start_output(int dev, int buff_no, int l);
int DMAbuf_move_wrpointer(int dev, int l);
/* int DMAbuf_ioctl(int dev, unsigned int cmd, void __user *arg, int local); */
void DMAbuf_init(int dev, int dma1, int dma2);
void DMAbuf_deinit(int dev);
int DMAbuf_start_dma (int dev, unsigned long physaddr, int count, int dma_mode);
int DMAbuf_open_dma (int dev);
void DMAbuf_close_dma (int dev);
void DMAbuf_inputintr(int dev);
void DMAbuf_outputintr(int dev, int underflow_flag);
struct dma_buffparms;
int DMAbuf_space_in_queue (int dev);
int DMAbuf_activate_recording (int dev, struct dma_buffparms *dmap);
int DMAbuf_get_buffer_pointer (int dev, struct dma_buffparms *dmap, int direction);
void DMAbuf_launch_output(int dev, struct dma_buffparms *dmap);
unsigned int DMAbuf_poll(struct file *file, int dev, poll_table *wait);
void DMAbuf_start_devices(unsigned int devmask);
void DMAbuf_reset (int dev);
int DMAbuf_sync (int dev);

/*
 *	System calls for /dev/dsp and /dev/audio (audio.c)
 */

int audio_read (int dev, struct file *file, char __user *buf, int count);
int audio_write (int dev, struct file *file, const char __user *buf, int count);
int audio_open (int dev, struct file *file);
void audio_release (int dev, struct file *file);
int audio_ioctl (int dev, struct file *file,
	   unsigned int cmd, void __user *arg);
void audio_init_devices (void);
void reorganize_buffers (int dev, struct dma_buffparms *dmap, int recording);

/*
 *	System calls for the /dev/sequencer
 */

int sequencer_read (int dev, struct file *file, char __user *buf, int count);
int sequencer_write (int dev, struct file *file, const char __user *buf, int count);
int sequencer_open (int dev, struct file *file);
void sequencer_release (int dev, struct file *file);
int sequencer_ioctl (int dev, struct file *file, unsigned int cmd, void __user *arg);
unsigned int sequencer_poll(int dev, struct file *file, poll_table * wait);

void sequencer_init (void);
void sequencer_unload (void);
void sequencer_timer(unsigned long dummy);
int note_to_freq(int note_num);
unsigned long compute_finetune(unsigned long base_freq, int bend, int range,
			       int vibrato_bend);
void seq_input_event(unsigned char *event, int len);
void seq_copy_to_input (unsigned char *event, int len);

/*
 *	System calls for the /dev/midi
 */

int MIDIbuf_read (int dev, struct file *file, char __user *buf, int count);
int MIDIbuf_write (int dev, struct file *file, const char __user *buf, int count);
int MIDIbuf_open (int dev, struct file *file);
void MIDIbuf_release (int dev, struct file *file);
int MIDIbuf_ioctl (int dev, struct file *file, unsigned int cmd, void __user *arg);
unsigned int MIDIbuf_poll(int dev, struct file *file, poll_table * wait);
int MIDIbuf_avail(int dev);

void MIDIbuf_bytes_received(int dev, unsigned char *buf, int count);
void MIDIbuf_init(void);


/*	From soundcard.c	*/
void request_sound_timer (int count);
void sound_stop_timer(void);
void conf_printf(char *name, struct address_info *hw_config);
void conf_printf2(char *name, int base, int irq, int dma, int dma2);

/*	From sound_timer.c */
void sound_timer_interrupt(void);
void sound_timer_syncinterval(unsigned int new_usecs);

/*      From midi_synth.c       */
void do_midi_msg (int synthno, unsigned char *msg, int mlen);
