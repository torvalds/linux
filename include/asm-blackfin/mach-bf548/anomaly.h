
/*
 * File:         include/asm-blackfin/mach-bf548/anomaly.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_
#define ANOMALY_05000074 /* A multi issue instruction with dsp32shiftimm in
			    slot1 and store of a P register in slot 2 is not
			    supported */
#define ANOMALY_05000119 /* DMA_RUN bit is not valid after a Peripheral Receive
			    Channel DMA stops */
#define ANOMALY_05000122 /* Rx.H can not be used to access 16-bit System MMR
			    registers. */
#define ANOMALY_05000245 /* Spurious Hardware Error from an Access in the
			    Shadow of a Conditional Branch */
#define ANOMALY_05000255 /* Entering Hibernate Mode with RTC Seconds event
			    interrupt not functional */
#define ANOMALY_05000265 /* Sensitivity to noise with slow input edge rates on
			    SPORT external receive and transmit clocks. */
#define ANOMALY_05000272 /* Certain data cache write through modes fail for
			    VDDint <=0.9V */
#define ANOMALY_05000281 /* False Hardware Error Exception when ISR context is
			    not restored */
#define ANOMALY_05000310 /* False Hardware Errors Caused by Fetches at the
			    Boundary of Reserved Memory */
#define ANOMALY_05000312 /* Errors When SSYNC, CSYNC, or Loads to LT, LB and
			    LC Registers Are Interrupted */
#define ANOMALY_05000324 /* TWI Slave Boot Mode Is Not Functional */
#define ANOMALY_05000325 /* External FIFO Boot Mode Is Not Functional */
#define ANOMALY_05000327 /* Data Lost When Core and DMA Accesses Are Made to
			    the USB FIFO Simultaneously */
#define ANOMALY_05000328 /* Incorrect Access of OTP_STATUS During otp_write()
			    function */
#define ANOMALY_05000329 /* Synchronous Burst Flash Boot Mode Is Not Functional
			    */
#define ANOMALY_05000330 /* Host DMA Boot Mode Is Not Functional */
#define ANOMALY_05000334 /* Inadequate Timing Margins on DDR DQS to DQ and DQM
			    Skew */
#define ANOMALY_05000335 /* Inadequate Rotary Debounce Logic Duration */
#define ANOMALY_05000336 /* Phantom Interrupt Occurs After First Configuration
			    of Host DMA Port */
#define ANOMALY_05000337 /* Disallowed Configuration Prevents Subsequent
			    Allowed Configuration on Host DMA Port */
#define ANOMALY_05000338 /* Slave-Mode SPI0 MISO Failure With CPHA = 0 */

#endif /* _MACH_ANOMALY_H_ */
