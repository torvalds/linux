/*****************************************************************************
*
*        BF-533/2/1 Specific Declarations
*
****************************************************************************/

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_BLACKFIN_DMA_CHANNEL 36

#define CH_PPI0			0
#define CH_PPI			(CH_PPI0)
#define CH_PPI1			1
#define CH_SPORT0_RX		12
#define CH_SPORT0_TX		13
#define CH_SPORT1_RX		14
#define CH_SPORT1_TX		15
#define CH_SPI			16
#define CH_UART_RX		17
#define CH_UART_TX		18
#define CH_MEM_STREAM0_DEST     24	 /* TX */
#define CH_MEM_STREAM0_SRC      25	 /* RX */
#define CH_MEM_STREAM1_DEST     26	 /* TX */
#define CH_MEM_STREAM1_SRC      27	 /* RX */
#define CH_MEM_STREAM2_DEST	28
#define CH_MEM_STREAM2_SRC	29
#define CH_MEM_STREAM3_SRC	30
#define CH_MEM_STREAM3_DEST	31
#define CH_IMEM_STREAM0_DEST	32
#define CH_IMEM_STREAM0_SRC	33
#define CH_IMEM_STREAM1_SRC	34
#define CH_IMEM_STREAM1_DEST	35

#endif
