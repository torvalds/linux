/*
 * linux/include/asm-arm/arch-sa1100/dma.h
 *
 * Generic SA1100 DMA support
 *
 * Copyright (C) 2000 Nicolas Pitre
 *
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/config.h>
#include "hardware.h"


/*
 * This is the maximum DMA address that can be DMAd to.
 */
#define MAX_DMA_ADDRESS		0xffffffff


/*
 * The regular generic DMA interface is inappropriate for the
 * SA1100 DMA model.  None of the SA1100 specific drivers using
 * DMA are portable anyway so it's pointless to try to twist the
 * regular DMA API to accommodate them.
 */
#define MAX_DMA_CHANNELS	0

/*
 * The SA1100 has six internal DMA channels.
 */
#define SA1100_DMA_CHANNELS	6

/*
 * Maximum physical DMA buffer size
 */
#define MAX_DMA_SIZE		0x1fff
#define CUT_DMA_SIZE		0x1000

/*
 * All possible SA1100 devices a DMA channel can be attached to.
 */
typedef enum {
	DMA_Ser0UDCWr  = DDAR_Ser0UDCWr,   /* Ser. port 0 UDC Write */
	DMA_Ser0UDCRd  = DDAR_Ser0UDCRd,   /* Ser. port 0 UDC Read */
	DMA_Ser1UARTWr = DDAR_Ser1UARTWr,  /* Ser. port 1 UART Write */
	DMA_Ser1UARTRd = DDAR_Ser1UARTRd,  /* Ser. port 1 UART Read */
	DMA_Ser1SDLCWr = DDAR_Ser1SDLCWr,  /* Ser. port 1 SDLC Write */
	DMA_Ser1SDLCRd = DDAR_Ser1SDLCRd,  /* Ser. port 1 SDLC Read */
	DMA_Ser2UARTWr = DDAR_Ser2UARTWr,  /* Ser. port 2 UART Write */
	DMA_Ser2UARTRd = DDAR_Ser2UARTRd,  /* Ser. port 2 UART Read */
	DMA_Ser2HSSPWr = DDAR_Ser2HSSPWr,  /* Ser. port 2 HSSP Write */
	DMA_Ser2HSSPRd = DDAR_Ser2HSSPRd,  /* Ser. port 2 HSSP Read */
	DMA_Ser3UARTWr = DDAR_Ser3UARTWr,  /* Ser. port 3 UART Write */
	DMA_Ser3UARTRd = DDAR_Ser3UARTRd,  /* Ser. port 3 UART Read */
	DMA_Ser4MCP0Wr = DDAR_Ser4MCP0Wr,  /* Ser. port 4 MCP 0 Write (audio) */
	DMA_Ser4MCP0Rd = DDAR_Ser4MCP0Rd,  /* Ser. port 4 MCP 0 Read (audio) */
	DMA_Ser4MCP1Wr = DDAR_Ser4MCP1Wr,  /* Ser. port 4 MCP 1 Write */
	DMA_Ser4MCP1Rd = DDAR_Ser4MCP1Rd,  /* Ser. port 4 MCP 1 Read */
	DMA_Ser4SSPWr  = DDAR_Ser4SSPWr,   /* Ser. port 4 SSP Write (16 bits) */
	DMA_Ser4SSPRd  = DDAR_Ser4SSPRd    /* Ser. port 4 SSP Read (16 bits) */
} dma_device_t;

typedef struct {
	volatile u_long DDAR;
	volatile u_long SetDCSR;
	volatile u_long ClrDCSR;
	volatile u_long RdDCSR;
	volatile dma_addr_t DBSA;
	volatile u_long DBTA;
	volatile dma_addr_t DBSB;
	volatile u_long DBTB;
} dma_regs_t;

typedef void (*dma_callback_t)(void *data);

/*
 * DMA function prototypes
 */

extern int sa1100_request_dma( dma_device_t device, const char *device_id,
			       dma_callback_t callback, void *data,
			       dma_regs_t **regs );
extern void sa1100_free_dma( dma_regs_t *regs );
extern int sa1100_start_dma( dma_regs_t *regs, dma_addr_t dma_ptr, u_int size );
extern dma_addr_t sa1100_get_dma_pos(dma_regs_t *regs);
extern void sa1100_reset_dma(dma_regs_t *regs);

/**
 * 	sa1100_stop_dma - stop DMA in progress
 * 	@regs: identifier for the channel to use
 *
 * 	This stops DMA without clearing buffer pointers. Unlike
 * 	sa1100_clear_dma() this allows subsequent use of sa1100_resume_dma()
 * 	or sa1100_get_dma_pos().
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 **/

#define sa1100_stop_dma(regs)	((regs)->ClrDCSR = DCSR_IE|DCSR_RUN)

/**
 * 	sa1100_resume_dma - resume DMA on a stopped channel
 * 	@regs: identifier for the channel to use
 *
 * 	This resumes DMA on a channel previously stopped with
 * 	sa1100_stop_dma().
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 **/

#define sa1100_resume_dma(regs)	((regs)->SetDCSR = DCSR_IE|DCSR_RUN)

/**
 * 	sa1100_clear_dma - clear DMA pointers
 * 	@regs: identifier for the channel to use
 *
 * 	This clear any DMA state so the DMA engine is ready to restart
 * 	with new buffers through sa1100_start_dma(). Any buffers in flight
 * 	are discarded.
 *
 * 	The @regs identifier is provided by a successful call to
 * 	sa1100_request_dma().
 **/

#define sa1100_clear_dma(regs)	((regs)->ClrDCSR = DCSR_IE|DCSR_RUN|DCSR_STRTA|DCSR_STRTB)

#endif /* _ASM_ARCH_DMA_H */
