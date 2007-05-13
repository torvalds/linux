/*
 * linux/include/asm-arm/arch-ks8695/irqs.h
 *
 * Copyright (C) 2006 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H


#define NR_IRQS				32

/*
 * IRQ definitions
 */
#define KS8695_IRQ_COMM_RX		0
#define KS8695_IRQ_COMM_TX		1
#define KS8695_IRQ_EXTERN0		2
#define KS8695_IRQ_EXTERN1		3
#define KS8695_IRQ_EXTERN2		4
#define KS8695_IRQ_EXTERN3		5
#define KS8695_IRQ_TIMER0		6
#define KS8695_IRQ_TIMER1		7
#define KS8695_IRQ_UART_TX		8
#define KS8695_IRQ_UART_RX		9
#define KS8695_IRQ_UART_LINE_STATUS	10
#define KS8695_IRQ_UART_MODEM_STATUS	11
#define KS8695_IRQ_LAN_RX_STOP		12
#define KS8695_IRQ_LAN_TX_STOP		13
#define KS8695_IRQ_LAN_RX_BUF		14
#define KS8695_IRQ_LAN_TX_BUF		15
#define KS8695_IRQ_LAN_RX_STATUS	16
#define KS8695_IRQ_LAN_TX_STATUS	17
#define KS8695_IRQ_HPNA_RX_STOP		18
#define KS8695_IRQ_HPNA_TX_STOP		19
#define KS8695_IRQ_HPNA_RX_BUF		20
#define KS8695_IRQ_HPNA_TX_BUF		21
#define KS8695_IRQ_HPNA_RX_STATUS	22
#define KS8695_IRQ_HPNA_TX_STATUS	23
#define KS8695_IRQ_BUS_ERROR		24
#define KS8695_IRQ_WAN_RX_STOP		25
#define KS8695_IRQ_WAN_TX_STOP		26
#define KS8695_IRQ_WAN_RX_BUF		27
#define KS8695_IRQ_WAN_TX_BUF		28
#define KS8695_IRQ_WAN_RX_STATUS	29
#define KS8695_IRQ_WAN_TX_STATUS	30
#define KS8695_IRQ_WAN_LINK		31

#endif
