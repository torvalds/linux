/*
dsp_register.h
*/
#ifndef DSP_REGISTER_H
#define DSP_REGISTER_H
#include <linux/dma-mapping.h>
#include <mach/cpu.h>

#ifdef CONFIG_ARCH_MESON8
#define SYS_MEM_START	0x00000000
#else
#define SYS_MEM_START	0x80000000
#endif
#define SYS_MEM_SIZE	0x8000000

#define S_1K					(1024)
#define S_1M					(S_1K*S_1K)

#define AUDIO_DSP_MEM_SIZE		 S_1M
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define AUDIO_DSP_START_PHY_ADDR 0x06000000
#else
#define AUDIO_DSP_START_PHY_ADDR 0x85000000
#endif
#define AUDIO_DSP_START_ADDR	((unsigned)phys_to_virt(AUDIO_DSP_START_PHY_ADDR))//((SYS_MEM_START+SYS_MEM_SIZE)-AUDIO_DSP_MEM_SIZE)
#define AUDIO_DSP_END_ADDR		((AUDIO_DSP_START_ADDR+AUDIO_DSP_MEM_SIZE))
#define REG_MEM_SIZE					(S_1K*4)




#define DSP_REG_OFFSET	(AUDIO_DSP_START_ADDR+ AUDIO_DSP_MEM_SIZE-REG_MEM_SIZE)
#define DSP_REG_END			(AUDIO_DSP_START_ADDR+ AUDIO_DSP_MEM_SIZE-4)
#define DSP_REG(x)			(DSP_REG_OFFSET | ((x)<<5))

#define DSP_STATUS_HALT		('H'<<24 | 'a'<<16|'l'<<8 |'t')
#define DSP_STATUS_RUNING		('R'<<16|'u'<<8 |'n')
#define DSP_STATUS_SLEEP    ('S'<<24 | 'L'<<16|'E'<<8 |'P')
#define DSP_STATUS_WAKEUP   ('W'<<24 | 'A'<<16|'K'<<8 |'E')
#define DSP_AUDIOINFO_READY ('A'<<24 | 'I'<<16|'R'<<8 |'D')

#define DSP_STATUS				DSP_REG(0)
#define DSP_JIFFIES				DSP_REG(1)
#define DSP_STACK_START   DSP_REG(3)
#define DSP_STACK_END   	DSP_REG(4)
#define DSP_GP_STACK_START   DSP_REG(5)
#define DSP_GP_STACK_END   	DSP_REG(6)

#define DSP_MEM_START  		DSP_REG(7)
#define DSP_MEM_END 	 		DSP_REG(8)


#define DSP_DECODE_OUT_START_ADDR  	DSP_REG(9)
#define DSP_DECODE_OUT_END_ADDR   	DSP_REG(10)
#define DSP_DECODE_OUT_RD_ADDR  	  DSP_REG(11)
#define DSP_DECODE_OUT_WD_ADDR  		DSP_REG(12)
#define DSP_AUDIOINFO_STATUS              DSP_REG(13)
#define DSP_SLEEP_STATUS                DSP_REG(14)
//#define DEBUG_TIME_TEST
#ifdef DEBUG_TIME_TEST
#define DSP_TEST_TIME_VALUE                DSP_REG(15)
#endif
#define DSP_ARM_REF_CLK_VAL                DSP_REG(16)

#define DSP_DECODE_OUT_WD_PTR       DSP_REG(18)
#define DSP_BUFFERED_LEN  		DSP_REG(19)
#define DSP_AFIFO_RD_OFFSET1  		DSP_REG(20)

#define DSP_DECODE_OPTION       DSP_REG(21)
#define DSP_AUDIO_FORMAT_INFO  DSP_REG(22)

#define DSP_GET_EXTRA_INFO_FINISH    DSP_REG(23)
#define DSP_HDMI_SR                  DSP_REG(24)

#define DSP_LOG_START_ADDR  	DSP_REG(28)
#define DSP_LOG_END_ADDR   	    DSP_REG(29)
#define DSP_LOG_RD_ADDR  	    DSP_REG(30)
#define DSP_LOG_WD_ADDR  		DSP_REG(31)

#define DSP_CHIP_SUBID          DSP_REG(32)
// other definations

// for wifi-display 
#define DSP_SKIP_BYTES          DSP_REG(38)

#define MAILBOX1_REG(n)	DSP_REG(40+n)
#define MAILBOX2_REG(n)	DSP_REG(40+32+n)


//----------------------------------------------
#define DSP_DECODE_51PCM_OUT_START_ADDR  	DSP_REG(106)
#define DSP_DECODE_51PCM_OUT_END_ADDR   	DSP_REG(107)
#define DSP_DECODE_51PCM_OUT_RD_ADDR  	    DSP_REG(108)
#define DSP_DECODE_51PCM_OUT_WD_ADDR  		DSP_REG(109)
//------------------------------------------------
/*
as iec958 ouput maybe be enabled before the spdif module inititation finish in the spdif driver,
in this case,it will cause 958 module not work.so add a protocal register to co-work for that
*/
#define DSP_IEC958_INIT_READY_INFO  		DSP_REG(110) 
#define DSP_AC3_DRC_INFO 					DSP_IEC958_INIT_READY_INFO
#define DSP_DTS_DEC_INFO					DSP_REG(111)
#define DSP_WORK_INFO (AUDIO_DSP_END_ADDR - 128)



#ifndef __ASSEMBLY__
/*mailbox struct*/
struct mail_msg{
int cmd;
int status;//0x1:pending.0:free
char *data;
int len;
};
#endif

#define M1B_IRQ0_PRINT							(0+16)
#define M1B_IRQ1_BUF_OVERFLOW					(1+16)
#define M1B_IRQ2_BUF_UNDERFLOW				(2+16)
#define M1B_IRQ3_DECODE_ERROR					(3+16)
#define M1B_IRQ4_DECODE_FINISH_FRAME 		(4+16)
#define M1B_IRQ5_STREAM_FMT_CHANGED 			(5+16)
#define M1B_IRQ5_STREAM_RD_WD_TEST 			(6+16)
#define M1B_IRQ7_DECODE_FATAL_ERR			(7+16)
#define M1B_IRQ8_IEC958_INFO                (8+16)


#define M2B_IRQ0_DSP_HALT							(0)
#define M2B_IRQ1_DSP_RESET						(1)
#define M2B_IRQ2_DECODE_START					(2)
#define M2B_IRQ3_DECODE_STOP					(3)
#define M2B_IRQ4_AUDIO_INFO					(4)
#define M2B_IRQ0_DSP_SLEEP					(5)
#define M2B_IRQ0_DSP_WAKEUP					(6)
#define M2B_IRQ0_DSP_AUDIO_EFFECT			(7) //audio post process cmd 

#define CMD_PRINT_LOG					(1234<<8 |1)

#if  MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 //>= m8
#define MB0_REG EE_ASSIST_MBOX0_IRQ_REG
#define MB0_SEL EE_ASSIST_MBOX0_FIQ_SEL
#define MB0_CLR EE_ASSIST_MBOX0_CLR_REG
#define MB0_MSK EE_ASSIST_MBOX0_MASK
#define MB1_REG EE_ASSIST_MBOX1_IRQ_REG
#define MB1_SEL EE_ASSIST_MBOX1_FIQ_SEL
#define MB1_CLR EE_ASSIST_MBOX1_CLR_REG
#define MB1_MSK EE_ASSIST_MBOX1_MASK
#define MB2_REG EE_ASSIST_MBOX2_IRQ_REG
#define MB2_SEL EE_ASSIST_MBOX2_FIQ_SEL
#define MB2_CLR EE_ASSIST_MBOX2_CLR_REG
#define MB2_MSK EE_ASSIST_MBOX2_MASK

#define READ_VREG(r) READ_CBUS_REG(r)
#define WRITE_VREG(r, val) WRITE_CBUS_REG(r, val)
#define WRITE_VREG_BITS(r, val, start, len) WRITE_CBUS_REG_BITS(r, val, start, len)
#define SET_VREG_MASK(r, mask) SET_CBUS_REG_MASK(r, mask)
#define CLEAR_VREG_MASK(r, mask) CLEAR_CBUS_REG_MASK(r, mask)
#else  //m6, m6tv
#define MB0_REG VDEC_ASSIST_MBOX0_IRQ_REG
#define MB0_SEL VDEC_ASSIST_MBOX0_FIQ_SEL
#define MB0_CLR VDEC_ASSIST_MBOX0_CLR_REG
#define MB0_MSK VDEC_ASSIST_MBOX0_MASK
#define MB1_REG VDEC_ASSIST_MBOX1_IRQ_REG
#define MB1_SEL VDEC_ASSIST_MBOX1_FIQ_SEL
#define MB1_CLR VDEC_ASSIST_MBOX1_CLR_REG
#define MB1_MSK VDEC_ASSIST_MBOX1_MASK
#define MB2_REG VDEC_ASSIST_MBOX2_IRQ_REG
#define MB2_SEL VDEC_ASSIST_MBOX2_FIQ_SEL
#define MB2_CLR VDEC_ASSIST_MBOX2_CLR_REG
#define MB2_MSK VDEC_ASSIST_MBOX2_MASK

#define READ_VREG(r) (aml_read_reg32(DOS_REG_ADDR(r)))
#define WRITE_VREG(r, val) aml_write_reg32( DOS_REG_ADDR(r),val)
#define WRITE_VREG_BITS(r, val, start, len) \
    WRITE_VREG(r, (READ_VREG(r) & ~(((1L<<(len))-1)<<(start)))|((unsigned)((val)&((1L<<(len))-1)) << (start)))
#define SET_VREG_MASK(r, mask) WRITE_VREG(r, READ_VREG(r) | (mask))
#define CLEAR_VREG_MASK(r, mask) WRITE_VREG(r, READ_VREG(r) & ~(mask))
#endif
#else // < m6
#define MB0_REG ASSIST_MBOX0_IRQ_REG
#define MB0_SEL ASSIST_MBOX0_FIQ_SEL
#define MB0_CLR ASSIST_MBOX0_CLR_REG
#define MB0_MSK ASSIST_MBOX0_MASK
#define MB1_REG ASSIST_MBOX1_IRQ_REG
#define MB1_SEL ASSIST_MBOX1_FIQ_SEL
#define MB1_CLR ASSIST_MBOX1_CLR_REG
#define MB1_MSK ASSIST_MBOX1_MASK
#define MB2_REG ASSIST_MBOX2_IRQ_REG
#define MB2_SEL ASSIST_MBOX2_FIQ_SEL
#define MB2_CLR ASSIST_MBOX2_CLR_REG
#define MB2_MSK ASSIST_MBOX2_MASK

#define READ_VREG(r) READ_CBUS_REG(r)
#define WRITE_VREG(r, val) WRITE_CBUS_REG(r, val)
#define WRITE_VREG_BITS(r, val, start, len) WRITE_CBUS_REG_BITS(r, val, start, len)
#define SET_VREG_MASK(r, mask) SET_CBUS_REG_MASK(r, mask)
#define CLEAR_VREG_MASK(r, mask) CLEAR_CBUS_REG_MASK(r, mask)

#endif

#define MDEC_TRIGGER_IRQ(irq)	{SET_VREG_MASK(MB0_SEL,1<<irq);\
								WRITE_VREG(MB0_REG,1<<irq);}
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8 //>= m8
#define SYS_TRIGGER_IRQ(irq)		{/*SET_VREG_MASK(MB1_SEL,1<<irq);*/ \
								WRITE_VREG(MB1_REG,1<<irq);}
#define SYS_CLEAR_IRQ(irq)		{/*CLEAR_VREG_MASK(MB1_SEL,1<<irq); */\
								WRITE_VREG(MB1_CLR,1<<irq);}
#define DSP_TRIGGER_IRQ(irq)		{/*SET_VREG_MASK(MB2_SEL,1<<irq); */\
								WRITE_VREG(MB2_REG,1<<irq);}
#else
#define SYS_TRIGGER_IRQ(irq)		{SET_VREG_MASK(MB1_SEL,1<<irq); \
								WRITE_VREG(MB1_REG,1<<irq);}
#define SYS_CLEAR_IRQ(irq)		{CLEAR_VREG_MASK(MB1_SEL,1<<irq); \
								WRITE_VREG(MB1_CLR,1<<irq);}
#define DSP_TRIGGER_IRQ(irq)		{SET_VREG_MASK(MB2_SEL,1<<irq); \
								WRITE_VREG(MB2_REG,1<<irq);}
#endif


#define MDEC_CLEAR_IRQ(irq)		{CLEAR_VREG_MASK(MB0_SEL,1<<irq); \
								WRITE_VREG(MB0_CLR,1<<irq);}



#define DSP_CLEAR_IRQ(irq)		{CLEAR_VREG_MASK(MB2_SEL,1<<irq); \
								WRITE_VREG(MB2_CLR,1<<irq);}


#define MAIBOX0_IRQ_ENABLE(irq)		SET_VREG_MASK(MB0_MSK,1<<irq)
#define MAIBOX1_IRQ_ENABLE(irq)		SET_VREG_MASK(MB1_MSK,1<<irq)
#define MAIBOX2_IRQ_ENABLE(irq)		SET_VREG_MASK(MB2_MSK,1<<irq)

#define ARC_2_ARM_ADDR_SWAP(addr)  ((unsigned long)(phys_to_virt(addr)))
#define ARM_2_ARC_ADDR_SWAP(addr)  (virt_to_phys((void*)addr))

#define DSP_RD(reg)	  (*((volatile unsigned long *)reg))
#define DSP_WD(reg,val)	({(*((volatile unsigned long *)(reg)))=val;})

#endif


