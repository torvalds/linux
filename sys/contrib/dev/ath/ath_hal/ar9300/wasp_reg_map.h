/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __REG_WASP_REG_MAP_H__

struct host_intf_reg_ar9340 {
  volatile char pad__0[0x4000];                   /*        0x0 - 0x4000     */
  volatile u_int32_t HOST_INTF_RESET_CONTROL;     /*     0x4000 - 0x4004     */
  volatile u_int32_t HOST_INTF_PM_CTRL;           /*     0x4004 - 0x4008     */
  volatile u_int32_t HOST_INTF_TIMEOUT;           /*     0x4008 - 0x400c     */
  volatile u_int32_t HOST_INTF_SREV;              /*     0x400c - 0x4010     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_CAUSE;   /*     0x4010 - 0x4014     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_ENABLE;  /*     0x4014 - 0x4018     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_MASK;   /*     0x4018 - 0x401c     */
  volatile u_int32_t HOST_INTF_INTR_SYNC_MASK;    /*     0x401c - 0x4020     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_CAUSE;  /*     0x4020 - 0x4024     */
  volatile u_int32_t HOST_INTF_INTR_ASYNC_ENABLE; /*     0x4024 - 0x4028     */
  volatile u_int32_t HOST_INTF_GPIO_OUT;          /*     0x4028 - 0x402c     */
  volatile u_int32_t HOST_INTF_GPIO_IN;           /*     0x402c - 0x4030     */
  volatile u_int32_t HOST_INTF_GPIO_OE;           /*     0x4030 - 0x4034     */
  volatile u_int32_t HOST_INTF_GPIO_OE1;          /*     0x4034 - 0x4038     */
  volatile u_int32_t HOST_INTF_GPIO_INTR_POLAR;   /*     0x4038 - 0x403c     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_VALUE;  /*     0x403c - 0x4040     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_MUX1;   /*     0x4040 - 0x4044     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_MUX2;   /*     0x4044 - 0x4048     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX1;  /*     0x4048 - 0x404c     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX2;  /*     0x404c - 0x4050     */
  volatile u_int32_t HOST_INTF_GPIO_OUTPUT_MUX3;  /*     0x4050 - 0x4054     */
  volatile u_int32_t HOST_INTF_GPIO_INPUT_STATE;  /*     0x4054 - 0x4058     */
  volatile u_int32_t HOST_INTF_CLKRUN;            /*     0x4058 - 0x405c     */
  volatile u_int32_t HOST_INTF_OBS_CTRL;          /*     0x405c - 0x4060     */
  volatile u_int32_t HOST_INTF_RFSILENT;          /*     0x4060 - 0x4064     */
  volatile char pad__3[0x10];                     /*     0x4064 - 0x4074     */
  volatile u_int32_t HOST_INTF_MISC;              /*     0x4074 - 0x4078     */
  volatile u_int32_t HOST_INTF_MAC_TDMA_CCA_CNTL; /*     0x4078 - 0x407c     */
  volatile u_int32_t HOST_INTF_MAC_TXAPSYNC;      /*     0x407c - 0x4080     */
  volatile u_int32_t HOST_INTF_MAC_TXSYNC_INITIAL_SYNC_TMR;      
                                                  /*     0x4080 - 0x4084     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_CAUSE;  
                                                  /*     0x4084 - 0x4088     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_ENABLE;
                                                  /*     0x4088 - 0x408c     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_MASK;
                                                  /*     0x408c - 0x4090     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_SYNC_MASK;
                                                  /*     0x4090 - 0x4094     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_CAUSE; 
                                                  /*     0x4094 - 0x4098     */
  volatile u_int32_t HOST_INTF_INTR_PRIORITY_ASYNC_ENABLE; 
                                                  /*     0x4098 - 0x409c     */
  volatile u_int32_t HOST_INTF_AXI_BYTE_SWAP;     /*     0x409c - 0x40a0     */
  volatile char pad__4[0x20];                     /*     0x40a4 - 0x40c4     */
  volatile u_int32_t HOST_INTF_WORK_AROUND;       /*     0x40c4 - 0x40c8     */
  volatile u_int32_t HOST_INTF_EEPROM_STS;        /*     0x40c8 - 0x40cc     */
  volatile u_int32_t HOST_INTF_PCIE_MSI;          /*     0x40d8 - 0x40dc     */
};

#endif /* __REG_WASP_REG_MAP_H__ */
