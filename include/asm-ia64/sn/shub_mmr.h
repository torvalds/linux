/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2004 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_SHUB_MMR_H
#define _ASM_IA64_SN_SHUB_MMR_H

/* ==================================================================== */
/*                        Register "SH_IPI_INT"                         */
/*               SHub Inter-Processor Interrupt Registers               */
/* ==================================================================== */
#define SH1_IPI_INT                               0x0000000110000380
#define SH2_IPI_INT                               0x0000000010000380

/*   SH_IPI_INT_TYPE                                                    */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_IPI_INT_TYPE_SHFT                     0
#define SH_IPI_INT_TYPE_MASK                     0x0000000000000007

/*   SH_IPI_INT_AGT                                                     */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_IPI_INT_AGT_SHFT                      3
#define SH_IPI_INT_AGT_MASK                      0x0000000000000008

/*   SH_IPI_INT_PID                                                     */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_IPI_INT_PID_SHFT                      4
#define SH_IPI_INT_PID_MASK                      0x00000000000ffff0

/*   SH_IPI_INT_BASE                                                    */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_IPI_INT_BASE_SHFT                     21
#define SH_IPI_INT_BASE_MASK                     0x0003ffffffe00000

/*   SH_IPI_INT_IDX                                                     */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_IPI_INT_IDX_SHFT                      52
#define SH_IPI_INT_IDX_MASK                      0x0ff0000000000000

/*   SH_IPI_INT_SEND                                                    */
/*   Description:  Send Interrupt Message to PI, This generates a puls  */
#define SH_IPI_INT_SEND_SHFT                     63
#define SH_IPI_INT_SEND_MASK                     0x8000000000000000

/* ==================================================================== */
/*                     Register "SH_EVENT_OCCURRED"                     */
/*                    SHub Interrupt Event Occurred                     */
/* ==================================================================== */
#define SH1_EVENT_OCCURRED                        0x0000000110010000
#define SH1_EVENT_OCCURRED_ALIAS                  0x0000000110010008
#define SH2_EVENT_OCCURRED                        0x0000000010010000
#define SH2_EVENT_OCCURRED_ALIAS                  0x0000000010010008

/* ==================================================================== */
/*                     Register "SH_PI_CAM_CONTROL"                     */
/*                      CRB CAM MMR Access Control                      */
/* ==================================================================== */
#define SH1_PI_CAM_CONTROL                        0x0000000120050300

/* ==================================================================== */
/*                        Register "SH_SHUB_ID"                         */
/*                            SHub ID Number                            */
/* ==================================================================== */
#define SH1_SHUB_ID                               0x0000000110060580
#define SH1_SHUB_ID_REVISION_SHFT                 28
#define SH1_SHUB_ID_REVISION_MASK                 0x00000000f0000000

/* ==================================================================== */
/*                          Register "SH_RTC"                           */
/*                           Real-time Clock                            */
/* ==================================================================== */
#define SH1_RTC                                   0x00000001101c0000
#define SH2_RTC					  0x00000002101c0000
#define SH_RTC_MASK                               0x007fffffffffffff

/* ==================================================================== */
/*                   Register "SH_PIO_WRITE_STATUS_0|1"                 */
/*                      PIO Write Status for CPU 0 & 1                  */
/* ==================================================================== */
#define SH1_PIO_WRITE_STATUS_0                    0x0000000120070200
#define SH1_PIO_WRITE_STATUS_1                    0x0000000120070280
#define SH2_PIO_WRITE_STATUS_0                    0x0000000020070200
#define SH2_PIO_WRITE_STATUS_1                    0x0000000020070280
#define SH2_PIO_WRITE_STATUS_2                    0x0000000020070300
#define SH2_PIO_WRITE_STATUS_3                    0x0000000020070380

/*   SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK                               */
/*   Description:  Deadlock response detected                           */
#define SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_SHFT 1
#define SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_MASK 0x0000000000000002

/*   SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT                          */
/*   Description:  Count of currently pending PIO writes                */
#define SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_SHFT 56
#define SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK 0x3f00000000000000

/* ==================================================================== */
/*                Register "SH_PIO_WRITE_STATUS_0_ALIAS"                */
/* ==================================================================== */
#define SH1_PIO_WRITE_STATUS_0_ALIAS              0x0000000120070208
#define SH2_PIO_WRITE_STATUS_0_ALIAS              0x0000000020070208

/* ==================================================================== */
/*                     Register "SH_EVENT_OCCURRED"                     */
/*                    SHub Interrupt Event Occurred                     */
/* ==================================================================== */
/*   SH_EVENT_OCCURRED_UART_INT                                         */
/*   Description:  Pending Junk Bus UART Interrupt                      */
#define SH_EVENT_OCCURRED_UART_INT_SHFT          20
#define SH_EVENT_OCCURRED_UART_INT_MASK          0x0000000000100000

/*   SH_EVENT_OCCURRED_IPI_INT                                          */
/*   Description:  Pending IPI Interrupt                                */
#define SH_EVENT_OCCURRED_IPI_INT_SHFT           28
#define SH_EVENT_OCCURRED_IPI_INT_MASK           0x0000000010000000

/*   SH_EVENT_OCCURRED_II_INT0                                          */
/*   Description:  Pending II 0 Interrupt                               */
#define SH_EVENT_OCCURRED_II_INT0_SHFT           29
#define SH_EVENT_OCCURRED_II_INT0_MASK           0x0000000020000000

/*   SH_EVENT_OCCURRED_II_INT1                                          */
/*   Description:  Pending II 1 Interrupt                               */
#define SH_EVENT_OCCURRED_II_INT1_SHFT           30
#define SH_EVENT_OCCURRED_II_INT1_MASK           0x0000000040000000

/* ==================================================================== */
/*                         LEDS                                         */
/* ==================================================================== */
#define SH1_REAL_JUNK_BUS_LED0			 0x7fed00000UL
#define SH1_REAL_JUNK_BUS_LED1			 0x7fed10000UL
#define SH1_REAL_JUNK_BUS_LED2			 0x7fed20000UL
#define SH1_REAL_JUNK_BUS_LED3			 0x7fed30000UL

#define SH2_REAL_JUNK_BUS_LED0			 0xf0000000UL
#define SH2_REAL_JUNK_BUS_LED1			 0xf0010000UL
#define SH2_REAL_JUNK_BUS_LED2			 0xf0020000UL
#define SH2_REAL_JUNK_BUS_LED3			 0xf0030000UL

/* ==================================================================== */
/*                         Register "SH1_PTC_0"                         */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH1_PTC_0                                 0x00000001101a0000

/*   SH1_PTC_0_A                                                        */
/*   Description:  Type                                                 */
#define SH1_PTC_0_A_SHFT                          0

/*   SH1_PTC_0_PS                                                       */
/*   Description:  Page Size                                            */
#define SH1_PTC_0_PS_SHFT                         2

/*   SH1_PTC_0_RID                                                      */
/*   Description:  Region ID                                            */
#define SH1_PTC_0_RID_SHFT                        8

/*   SH1_PTC_0_START                                                    */
/*   Description:  Start                                                */
#define SH1_PTC_0_START_SHFT                      63

/* ==================================================================== */
/*                         Register "SH1_PTC_1"                         */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH1_PTC_1                                 0x00000001101a0080

/*   SH1_PTC_1_START                                                    */
/*   Description:  PTC_1 Start                                          */
#define SH1_PTC_1_START_SHFT                      63


/* ==================================================================== */
/*                         Register "SH2_PTC"                           */
/*       Puge Translation Cache Message Configuration Information       */
/* ==================================================================== */
#define SH2_PTC                                   0x0000000170000000

/*   SH2_PTC_A                                                          */
/*   Description:  Type                                                 */
#define SH2_PTC_A_SHFT                            0

/*   SH2_PTC_PS                                                         */
/*   Description:  Page Size                                            */
#define SH2_PTC_PS_SHFT                           2

/*   SH2_PTC_RID                                                      */
/*   Description:  Region ID                                            */
#define SH2_PTC_RID_SHFT                          4

/*   SH2_PTC_START                                                      */
/*   Description:  Start                                                */
#define SH2_PTC_START_SHFT                        63

/*   SH2_PTC_ADDR_RID                                                   */
/*   Description:  Region ID                                            */
#define SH2_PTC_ADDR_SHFT                         4
#define SH2_PTC_ADDR_MASK                         0x1ffffffffffff000

/* ==================================================================== */
/*                    Register "SH_RTC1_INT_CONFIG"                     */
/*                SHub RTC 1 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH1_RTC1_INT_CONFIG                      0x0000000110001480
#define SH2_RTC1_INT_CONFIG                      0x0000000010001480
#define SH_RTC1_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC1_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC1_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC1_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC1_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC1_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC1_INT_CONFIG_AGT_SHFT              3
#define SH_RTC1_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC1_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC1_INT_CONFIG_PID_SHFT              4
#define SH_RTC1_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC1_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC1_INT_CONFIG_BASE_SHFT             21
#define SH_RTC1_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC1_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC1_INT_CONFIG_IDX_SHFT              52
#define SH_RTC1_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC1_INT_ENABLE"                     */
/*                SHub RTC 1 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH1_RTC1_INT_ENABLE                      0x0000000110001500
#define SH2_RTC1_INT_ENABLE                      0x0000000010001500
#define SH_RTC1_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC1_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC1_INT_ENABLE_RTC1_ENABLE                                     */
/*   Description:  Enable RTC 1 Interrupt                               */
#define SH_RTC1_INT_ENABLE_RTC1_ENABLE_SHFT      0
#define SH_RTC1_INT_ENABLE_RTC1_ENABLE_MASK      0x0000000000000001

/* ==================================================================== */
/*                    Register "SH_RTC2_INT_CONFIG"                     */
/*                SHub RTC 2 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH1_RTC2_INT_CONFIG                      0x0000000110001580
#define SH2_RTC2_INT_CONFIG                      0x0000000010001580
#define SH_RTC2_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC2_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC2_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC2_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC2_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC2_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC2_INT_CONFIG_AGT_SHFT              3
#define SH_RTC2_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC2_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC2_INT_CONFIG_PID_SHFT              4
#define SH_RTC2_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC2_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC2_INT_CONFIG_BASE_SHFT             21
#define SH_RTC2_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC2_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC2_INT_CONFIG_IDX_SHFT              52
#define SH_RTC2_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC2_INT_ENABLE"                     */
/*                SHub RTC 2 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH1_RTC2_INT_ENABLE                      0x0000000110001600
#define SH2_RTC2_INT_ENABLE                      0x0000000010001600
#define SH_RTC2_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC2_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC2_INT_ENABLE_RTC2_ENABLE                                     */
/*   Description:  Enable RTC 2 Interrupt                               */
#define SH_RTC2_INT_ENABLE_RTC2_ENABLE_SHFT      0
#define SH_RTC2_INT_ENABLE_RTC2_ENABLE_MASK      0x0000000000000001

/* ==================================================================== */
/*                    Register "SH_RTC3_INT_CONFIG"                     */
/*                SHub RTC 3 Interrupt Config Registers                 */
/* ==================================================================== */

#define SH1_RTC3_INT_CONFIG                      0x0000000110001680
#define SH2_RTC3_INT_CONFIG                      0x0000000010001680
#define SH_RTC3_INT_CONFIG_MASK                  0x0ff3ffffffefffff
#define SH_RTC3_INT_CONFIG_INIT                  0x0000000000000000

/*   SH_RTC3_INT_CONFIG_TYPE                                            */
/*   Description:  Type of Interrupt: 0=INT, 2=PMI, 4=NMI, 5=INIT       */
#define SH_RTC3_INT_CONFIG_TYPE_SHFT             0
#define SH_RTC3_INT_CONFIG_TYPE_MASK             0x0000000000000007

/*   SH_RTC3_INT_CONFIG_AGT                                             */
/*   Description:  Agent, must be 0 for SHub                            */
#define SH_RTC3_INT_CONFIG_AGT_SHFT              3
#define SH_RTC3_INT_CONFIG_AGT_MASK              0x0000000000000008

/*   SH_RTC3_INT_CONFIG_PID                                             */
/*   Description:  Processor ID, same setting as on targeted McKinley  */
#define SH_RTC3_INT_CONFIG_PID_SHFT              4
#define SH_RTC3_INT_CONFIG_PID_MASK              0x00000000000ffff0

/*   SH_RTC3_INT_CONFIG_BASE                                            */
/*   Description:  Optional interrupt vector area, 2MB aligned          */
#define SH_RTC3_INT_CONFIG_BASE_SHFT             21
#define SH_RTC3_INT_CONFIG_BASE_MASK             0x0003ffffffe00000

/*   SH_RTC3_INT_CONFIG_IDX                                             */
/*   Description:  Targeted McKinley interrupt vector                   */
#define SH_RTC3_INT_CONFIG_IDX_SHFT              52
#define SH_RTC3_INT_CONFIG_IDX_MASK              0x0ff0000000000000

/* ==================================================================== */
/*                    Register "SH_RTC3_INT_ENABLE"                     */
/*                SHub RTC 3 Interrupt Enable Registers                 */
/* ==================================================================== */

#define SH1_RTC3_INT_ENABLE                      0x0000000110001700
#define SH2_RTC3_INT_ENABLE                      0x0000000010001700
#define SH_RTC3_INT_ENABLE_MASK                  0x0000000000000001
#define SH_RTC3_INT_ENABLE_INIT                  0x0000000000000000

/*   SH_RTC3_INT_ENABLE_RTC3_ENABLE                                     */
/*   Description:  Enable RTC 3 Interrupt                               */
#define SH_RTC3_INT_ENABLE_RTC3_ENABLE_SHFT      0
#define SH_RTC3_INT_ENABLE_RTC3_ENABLE_MASK      0x0000000000000001

/*   SH_EVENT_OCCURRED_RTC1_INT                                         */
/*   Description:  Pending RTC 1 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC1_INT_SHFT          24
#define SH_EVENT_OCCURRED_RTC1_INT_MASK          0x0000000001000000

/*   SH_EVENT_OCCURRED_RTC2_INT                                         */
/*   Description:  Pending RTC 2 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC2_INT_SHFT          25
#define SH_EVENT_OCCURRED_RTC2_INT_MASK          0x0000000002000000

/*   SH_EVENT_OCCURRED_RTC3_INT                                         */
/*   Description:  Pending RTC 3 Interrupt                              */
#define SH_EVENT_OCCURRED_RTC3_INT_SHFT          26
#define SH_EVENT_OCCURRED_RTC3_INT_MASK          0x0000000004000000

/* ==================================================================== */
/*                        Register "SH_INT_CMPB"                        */
/*                  RTC Compare Value for Processor B                   */
/* ==================================================================== */

#define SH1_INT_CMPB                             0x00000001101b0080
#define SH2_INT_CMPB                             0x00000000101b0080
#define SH_INT_CMPB_MASK                         0x007fffffffffffff
#define SH_INT_CMPB_INIT                         0x0000000000000000

/*   SH_INT_CMPB_REAL_TIME_CMPB                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPB_REAL_TIME_CMPB_SHFT          0
#define SH_INT_CMPB_REAL_TIME_CMPB_MASK          0x007fffffffffffff

/* ==================================================================== */
/*                        Register "SH_INT_CMPC"                        */
/*                  RTC Compare Value for Processor C                   */
/* ==================================================================== */

#define SH1_INT_CMPC                             0x00000001101b0100
#define SH2_INT_CMPC                             0x00000000101b0100
#define SH_INT_CMPC_MASK                         0x007fffffffffffff
#define SH_INT_CMPC_INIT                         0x0000000000000000

/*   SH_INT_CMPC_REAL_TIME_CMPC                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPC_REAL_TIME_CMPC_SHFT          0
#define SH_INT_CMPC_REAL_TIME_CMPC_MASK          0x007fffffffffffff

/* ==================================================================== */
/*                        Register "SH_INT_CMPD"                        */
/*                  RTC Compare Value for Processor D                   */
/* ==================================================================== */

#define SH1_INT_CMPD                             0x00000001101b0180
#define SH2_INT_CMPD                             0x00000000101b0180
#define SH_INT_CMPD_MASK                         0x007fffffffffffff
#define SH_INT_CMPD_INIT                         0x0000000000000000

/*   SH_INT_CMPD_REAL_TIME_CMPD                                         */
/*   Description:  Real Time Clock Compare                              */
#define SH_INT_CMPD_REAL_TIME_CMPD_SHFT          0
#define SH_INT_CMPD_REAL_TIME_CMPD_MASK          0x007fffffffffffff


/* ==================================================================== */
/* Some MMRs are functionally identical (or close enough) on both SHUB1 */
/* and SHUB2 that it makes sense to define a geberic name for the MMR.  */
/* It is acceptible to use (for example) SH_IPI_INT to reference the    */
/* the IPI MMR. The value of SH_IPI_INT is determined at runtime based  */
/* on the type of the SHUB. Do not use these #defines in performance    */
/* critical code  or loops - there is a small performance penalty.      */
/* ==================================================================== */
#define shubmmr(a,b) 		(is_shub2() ? a##2_##b : a##1_##b)

#define SH_REAL_JUNK_BUS_LED0	shubmmr(SH, REAL_JUNK_BUS_LED0)
#define SH_IPI_INT		shubmmr(SH, IPI_INT)
#define SH_EVENT_OCCURRED	shubmmr(SH, EVENT_OCCURRED)
#define SH_EVENT_OCCURRED_ALIAS	shubmmr(SH, EVENT_OCCURRED_ALIAS)
#define SH_RTC			shubmmr(SH, RTC)
#define SH_RTC1_INT_CONFIG	shubmmr(SH, RTC1_INT_CONFIG)
#define SH_RTC1_INT_ENABLE	shubmmr(SH, RTC1_INT_ENABLE)
#define SH_RTC2_INT_CONFIG	shubmmr(SH, RTC2_INT_CONFIG)
#define SH_RTC2_INT_ENABLE	shubmmr(SH, RTC2_INT_ENABLE)
#define SH_RTC3_INT_CONFIG	shubmmr(SH, RTC3_INT_CONFIG)
#define SH_RTC3_INT_ENABLE	shubmmr(SH, RTC3_INT_ENABLE)
#define SH_INT_CMPB		shubmmr(SH, INT_CMPB)
#define SH_INT_CMPC		shubmmr(SH, INT_CMPC)
#define SH_INT_CMPD		shubmmr(SH, INT_CMPD)

#endif /* _ASM_IA64_SN_SHUB_MMR_H */
