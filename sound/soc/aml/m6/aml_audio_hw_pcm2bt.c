#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <mach/am_regs.h>
#include <linux/clk.h>

#include "aml_audio_hw_pcm2bt.h"

//#define PCM_DEBUG

#ifdef PCM_DEBUG
#define pcm_debug           pr_info
#else
#define pcm_debug(fmt, ...) \
        do {} while(0)
#endif

static unsigned int pcmin_buffer_addr = 0;
static unsigned int pcmin_buffer_size = 0;

static unsigned int pcmout_buffer_addr = 0;
static unsigned int pcmout_buffer_size = 0;

static void pcm_in_register_show(void)
{
    pcm_debug("PCMIN registers show:\n");
    pcm_debug("\tAUDIN_FIFO1_START(0x%04x): 0x%08x\n", AUDIN_FIFO1_START, READ_MPEG_REG(AUDIN_FIFO1_START));
    pcm_debug("\tAUDIN_FIFO1_END(0x%04x):   0x%08x\n", AUDIN_FIFO1_END, READ_MPEG_REG(AUDIN_FIFO1_END));
    pcm_debug("\tAUDIN_FIFO1_PTR(0x%04x):   0x%08x\n", AUDIN_FIFO1_PTR, READ_MPEG_REG(AUDIN_FIFO1_PTR));
    pcm_debug("\tAUDIN_FIFO1_RDPTR(0x%04x): 0x%08x\n", AUDIN_FIFO1_RDPTR, READ_MPEG_REG(AUDIN_FIFO1_RDPTR));
    pcm_debug("\tAUDIN_FIFO1_CTRL(0x%04x):  0x%08x\n", AUDIN_FIFO1_CTRL, READ_MPEG_REG(AUDIN_FIFO1_CTRL));
    pcm_debug("\tAUDIN_FIFO1_CTRL1(0x%04x): 0x%08x\n", AUDIN_FIFO1_CTRL1, READ_MPEG_REG(AUDIN_FIFO1_CTRL1));
    pcm_debug("\tPCMIN_CTRL0(0x%04x):       0x%08x\n", PCMIN_CTRL0, READ_MPEG_REG(PCMIN_CTRL0));
    pcm_debug("\tPCMIN_CTRL1(0x%04x):       0x%08x\n", PCMIN_CTRL1, READ_MPEG_REG(PCMIN_CTRL1));
}

void pcm_in_enable(int flag)
{
    /* reset fifo */
RESET_FIFO:    
    WRITE_MPEG_REG_BITS(AUDIN_FIFO1_CTRL, 1, 1, 1);
    WRITE_MPEG_REG(AUDIN_FIFO1_PTR, 0);
    if (READ_MPEG_REG(AUDIN_FIFO1_PTR) != READ_MPEG_REG(AUDIN_FIFO1_START))
        goto RESET_FIFO;
    WRITE_MPEG_REG_BITS(AUDIN_FIFO1_CTRL, 0, 1, 1);

    /* reset pcmin */
    WRITE_MPEG_REG_BITS(PCMIN_CTRL0, 1, 30, 1);
    WRITE_MPEG_REG_BITS(PCMIN_CTRL0, 0, 30, 1);

    /* disable fifo */
    WRITE_MPEG_REG_BITS(AUDIN_FIFO1_CTRL, 0, 0, 1);

    /* disable pcmin */
    WRITE_MPEG_REG_BITS(PCMIN_CTRL0, 0, 31, 1);

    if (flag) {
        /* set buffer start ptr end */
    	WRITE_MPEG_REG(AUDIN_FIFO1_START, pcmin_buffer_addr);
    	WRITE_MPEG_REG(AUDIN_FIFO1_PTR, pcmin_buffer_addr);
    	WRITE_MPEG_REG(AUDIN_FIFO1_END, pcmin_buffer_addr + pcmin_buffer_size - 8);

        /* fifo control */
        WRITE_MPEG_REG(AUDIN_FIFO1_CTRL, (1 << 15) |    // urgent request
                                         (1 << 11) |    // channel
                                         (6 << 8) |     // endian
                                         //(0 << 8) |     // endian
                                         (2 << 3) |     // PCMIN input selection
                                         (1 << 2) |     // load address
                                         (0 << 1) |     // reset fifo
                                         (1 << 0)       // fifo enable
                        );

        /* fifo control1 */
        WRITE_MPEG_REG(AUDIN_FIFO1_CTRL1, (0 << 4) |    // data destination DDR
                                          (1 << 2) |    // 16bits
                                          (0 << 0)      // data position
                        );

        /* pcmin control1 */
        WRITE_MPEG_REG(PCMIN_CTRL1, (0 << 29) |         // external chip
                                    (0 << 28) |         // external chip
                                    (1 << 27) |         // using negedge of PCM clock to latch the input data
                                    (15 << 21) |        // slot bit msb 16 clocks per slot
                                    (15 << 16) |        // data msb 16bits data
                                    (1 << 0)            // slot valid
                        );

        /* pcmin control0 */
        WRITE_MPEG_REG(PCMIN_CTRL0, (1 << 31) |         // pcmin enable
                                    (1 << 29) |         // sync on clock posedge
                                    (0 << 16) |         // FS SKEW
                                    (0 << 4) |          // waithing 1 system clock cycles then sample the PCMIN singals
                                    (0 << 3) |          // use clock counter to do the sample
                                    (0 << 2) |          // fs not inverted. H = left, L = right
                                    (1 << 1) |          // msb first
                                    (1 << 0)            // left justified
                        );
    }

    pcm_debug("PCMIN %s\n", flag ? "enable" : "disable");
    pcm_in_register_show();
}

void pcm_in_set_buf(unsigned int addr, unsigned int size)
{
    pcmin_buffer_addr = addr;
    pcmin_buffer_size = size;

    pcm_debug("PCMIN buffer start: 0x%08x size: 0x%08x\n", pcmin_buffer_addr, pcmin_buffer_size);
}

int pcm_in_is_enable(void)
{
    int value = (READ_MPEG_REG_BITS(PCMIN_CTRL0, 31, 1) & 0x01);

    return value;
}

unsigned int pcm_in_rd_ptr(void)
{
    unsigned int value = READ_MPEG_REG(AUDIN_FIFO1_RDPTR);
    pcm_debug("PCMIN AUDIN_FIFO1_RDPTR: 0x%08x\n", value);

    return value;
}

unsigned int pcm_in_set_rd_ptr(unsigned int value)
{
    unsigned int old = READ_MPEG_REG(AUDIN_FIFO1_RDPTR);
    WRITE_MPEG_REG(AUDIN_FIFO1_RDPTR, value);
    pcm_debug("PCMIN AUDIN_FIFO1_RDPTR: 0x%08x -> 0x%08x\n", old, value);

    return old;
}

unsigned int pcm_in_wr_ptr(void)
{
//	unsigned int writing = READ_MPEG_REG(AUDIN_FIFO1_PTR);
    unsigned int written = 0;
    unsigned int value = 0;

    WRITE_MPEG_REG(AUDIN_FIFO1_PTR, 1);
	written = READ_MPEG_REG(AUDIN_FIFO1_PTR);
    pcm_debug("PCMIN AUDIN_FIFO1_PTR: 0x%08x (0x%08x)\n", written, writing);

    //value = written;
    value = written & (~0x07);
    return value;
}

unsigned int pcm_in_fifo_int(void)
{
    unsigned int value = 0;
    value = READ_MPEG_REG(AUDIN_FIFO_INT);
    pcm_debug("PCMIN AUDIN_FIFO_INT: 0x%08x\n", value);

    return value;
}

static void pcm_out_register_show(void)
{
    pcm_debug("PCMOUT registers show:\n");
    pcm_debug("\tAUDOUT_BUF0_STA(0x%04x):  0x%08x\n", AUDOUT_BUF0_STA, READ_MPEG_REG(AUDOUT_BUF0_STA));
    pcm_debug("\tAUDOUT_BUF0_EDA(0x%04x):  0x%08x\n", AUDOUT_BUF0_EDA, READ_MPEG_REG(AUDOUT_BUF0_EDA));
    pcm_debug("\tAUDOUT_BUF0_WPTR(0x%04x): 0x%08x\n", AUDOUT_BUF0_WPTR, READ_MPEG_REG(AUDOUT_BUF0_WPTR));
    pcm_debug("\tAUDOUT_FIFO_RPTR(0x%04x): 0x%08x\n", AUDOUT_FIFO_RPTR, READ_MPEG_REG(AUDOUT_FIFO_RPTR));
    pcm_debug("\tAUDOUT_CTRL(0x%04x):      0x%08x\n", AUDOUT_CTRL, READ_MPEG_REG(AUDOUT_CTRL));
    pcm_debug("\tAUDOUT_CTRL1(0x%04x):     0x%08x\n", AUDOUT_CTRL1, READ_MPEG_REG(AUDOUT_CTRL1));
    pcm_debug("\tPCMOUT_CTRL0(0x%04x):     0x%08x\n", PCMOUT_CTRL0, READ_MPEG_REG(PCMOUT_CTRL0));
    pcm_debug("\tPCMOUT_CTRL1(0x%04x):     0x%08x\n", PCMOUT_CTRL1, READ_MPEG_REG(PCMOUT_CTRL1));
    pcm_debug("\tPCMOUT_CTRL2(0x%04x):     0x%08x\n", PCMOUT_CTRL2, READ_MPEG_REG(PCMOUT_CTRL2));
    pcm_debug("\tPCMOUT_CTRL3(0x%04x):     0x%08x\n", PCMOUT_CTRL3, READ_MPEG_REG(PCMOUT_CTRL3));
}

void pcm_out_enable(int flag)
{
    /* reset fifo */
    WRITE_MPEG_REG_BITS(AUDOUT_CTRL, 1, 30, 1);
    WRITE_MPEG_REG_BITS(AUDOUT_CTRL, 0, 30, 1);

    /* reset pcmout */
    WRITE_MPEG_REG_BITS(PCMOUT_CTRL0, 1, 30, 1);
    WRITE_MPEG_REG_BITS(PCMOUT_CTRL0, 0, 30, 1);

    /* disable fifo */
    WRITE_MPEG_REG_BITS(AUDOUT_CTRL, 0, 31, 1);

    /* disable pcmout */
    WRITE_MPEG_REG_BITS(PCMOUT_CTRL0, 0, 31, 1);

    if (flag) {
        /* set buffer start ptr end */
    	WRITE_MPEG_REG(AUDOUT_BUF0_STA, pcmout_buffer_addr);
    	WRITE_MPEG_REG(AUDOUT_BUF0_WPTR, pcmout_buffer_addr);
    	WRITE_MPEG_REG(AUDOUT_BUF0_EDA, pcmout_buffer_addr + pcmout_buffer_size - 8);

        /* fifo control */
        WRITE_MPEG_REG(AUDOUT_CTRL, (0 << 31) |     // fifo enable
                                    (0 << 30) |     // soft reset
                                    (1 << 29) |     // load address
                                    (0 << 22) |     // use cbus AUDOUT BUFFER0 write pointer as the AUDOUT FIFO write pointer
                                    (52 << 15) |    // data request size
                                    (64 << 8) |     // buffer level to keep
                                    (0 << 7) |      // buffer level control
                                    (1 << 6) |      // DMA mode
                                    (1 << 5) |      // circular buffer
                                    (0 << 4) |      // use register set 0 always
                                    (1 << 3) |       // urgent request
                                    (6 << 0)         //endian
                        );

        WRITE_MPEG_REG(AUDOUT_CTRL, (1 << 31) |     // fifo enable
                                    (0 << 30) |     // soft reset
                                    (0 << 29) |     // load address
                                    (0 << 22) |     // use cbus AUDOUT BUFFER0 write pointer as the AUDOUT FIFO write pointer
                                    (52 << 15) |    // data request size
                                    (64 << 8) |     // buffer level to keep
                                    (0 << 7) |      // buffer level control
                                    (1 << 6) |      // DMA mode
                                    (1 << 5) |      // circular buffer
                                    (0 << 4) |      // use register set 0 always
                                    (1 << 3) |       // urgent request
                                    (6 << 0)         //endian
                                    
                        );
        /* pcmout control3 */
        WRITE_MPEG_REG(PCMOUT_CTRL3, 0);            // mute constant

        /* pcmout control2 */
        WRITE_MPEG_REG(PCMOUT_CTRL2,(0 << 29) |     // underrun use mute constant
                                    (0 << 22) |     // 1 channel per frame
                                    (15 << 16) |    // 16 bits per slot
                                    (1 << 0)        // enable 1 slot
                        );

        /* pcmout control1 */
        WRITE_MPEG_REG(PCMOUT_CTRL1,(1 << 30) |     // data byte numbe n - 1?
                                    (0 << 28) |     // use posedge of PCM clock to output data
                                    (1 << 27)       // use negedge of pcm clock to check the fs
                        );

        /* pcmout control0 */
        WRITE_MPEG_REG(PCMOUT_CTRL0,(1 << 31) |     // enable
                                    (0 << 29) |     // slave
                                    (1 << 28) |     // sync on clock rising edge
                                    (0 << 27) |     // data sample mode
                                    (1 << 15) |     // sync on 4 system clock later ?
                                    (1 << 14) |     // msb first
                                    (1 << 13) |     // left justified
                                    (0 << 12) |     // data position
                                    (3 << 6) |      // sync fs with the slot bit counter.
                                    (0 << 0)        // sync fs with frame slot counter.
                        );
    }

    pcm_debug("PCMOUT %s\n", flag ? "enable" : "disable");
    pcm_out_register_show();
}

void pcm_out_mute(int flag)
{
    int value = flag ? 1: 0;
    WRITE_MPEG_REG_BITS(PCMOUT_CTRL2, value, 31, 1);
}

void pcm_out_set_buf(unsigned int addr, unsigned int size)
{
    pcmout_buffer_addr = addr;
    pcmout_buffer_size = size;

    pcm_debug("PCMOUT buffer addr: 0x%08x end: 0x%08x\n", pcmout_buffer_addr, pcmout_buffer_size);
}

int pcm_out_is_enable(void)
{
    int value = (READ_MPEG_REG_BITS(PCMOUT_CTRL0, 31, 1) & 0x01);

    return value;
}

int  pcm_out_is_mute(void)
{
    int value = (READ_MPEG_REG_BITS(PCMOUT_CTRL2, 31, 1) & 0x01);

    return value;
}

unsigned int pcm_out_rd_ptr(void)
{
    unsigned int value = READ_MPEG_REG(AUDOUT_FIFO_RPTR);
    pcm_debug("PCMOUT read pointer: 0x%08x\n", value);

    return value;
}

unsigned int pcm_out_wr_ptr(void)
{
    unsigned int value = 0;
 	value = READ_MPEG_REG(AUDOUT_BUF0_WPTR);
    pcm_debug("PCMOUT write pointer: 0x%08x\n", value);
    return value;
}

unsigned int pcm_out_set_wr_ptr(unsigned int value)
{
    unsigned int old = READ_MPEG_REG(AUDOUT_BUF0_WPTR);
 	WRITE_MPEG_REG(AUDOUT_BUF0_WPTR, value);
    pcm_debug("PCMOUT write pointer: 0x%08x -> 0x%08x\n", old, value);

    return old;
}

