#ifndef __AML_PCM_H__
#define __AML_PCM_H__

struct aml_pcm_runtime_data {
	spinlock_t			lock;

    dma_addr_t          buffer_start;
    unsigned int        buffer_size;

    unsigned int        buffer_offset;

    unsigned int        data_size;

    unsigned int        running;
    unsigned int        timer_period;
    unsigned int        peroid_elapsed;

    struct timer_list   timer;
    struct snd_pcm_substream *substream;
};


#endif
