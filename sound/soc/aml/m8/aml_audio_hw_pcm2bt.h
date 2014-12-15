#ifndef __AML_PCM_HW_H__
#define __AML_PCM_HW_H__

void pcm_in_enable(int flag);
void pcm_in_set_buf(unsigned int addr, unsigned int size);
int  pcm_in_is_enable(void);
unsigned int pcm_in_rd_ptr(void);
unsigned int pcm_in_wr_ptr(void);
unsigned int pcm_in_set_rd_ptr(unsigned int value);
unsigned int pcm_in_fifo_int(void);

void pcm_out_enable(int flag);
void pcm_out_mute(int flag);
void pcm_out_set_buf(unsigned int addr, unsigned int size);
int  pcm_out_is_enable(void);
int  pcm_out_is_mute(void);
unsigned int pcm_out_rd_ptr(void);
unsigned int pcm_out_wr_ptr(void);
unsigned int pcm_out_set_wr_ptr(unsigned int value);

#endif
