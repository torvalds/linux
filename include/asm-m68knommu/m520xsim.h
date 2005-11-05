/****************************************************************************/

/*
 *  m520xsim.h -- ColdFire 5207/5208 System Integration Module support.
 *
 *  (C) Copyright 2005, Intec Automation (mike@steroidmicros.com)
 */

/****************************************************************************/
#ifndef m520xsim_h
#define m520xsim_h
/****************************************************************************/

#include <linux/config.h>

/*
 *  Define the 5282 SIM register set addresses.
 */
#define MCFICM_INTC0        0x48000     /* Base for Interrupt Ctrl 0 */
#define MCFINTC_IPRH        0x00        /* Interrupt pending 32-63 */
#define MCFINTC_IPRL        0x04        /* Interrupt pending 1-31 */
#define MCFINTC_IMRH        0x08        /* Interrupt mask 32-63 */
#define MCFINTC_IMRL        0x0c        /* Interrupt mask 1-31 */
#define MCFINTC_INTFRCH     0x10        /* Interrupt force 32-63 */
#define MCFINTC_INTFRCL     0x14        /* Interrupt force 1-31 */
#define MCFINTC_ICR0        0x40        /* Base ICR register */

#define MCFINT_VECBASE      64
#define MCFINT_UART0        26          /* Interrupt number for UART0 */
#define MCFINT_UART1        27          /* Interrupt number for UART1 */
#define MCFINT_UART2        28          /* Interrupt number for UART2 */
#define MCFINT_QSPI         31          /* Interrupt number for QSPI */
#define MCFINT_PIT1         4           /* Interrupt number for PIT1 (PIT0 in processor) */


#define MCF_GPIO_PAR_UART                   (0xA4036)
#define MCF_GPIO_PAR_FECI2C                 (0xA4033)
#define MCF_GPIO_PAR_FEC                    (0xA4038)

#define MCF_GPIO_PAR_UART_PAR_URXD0         (0x0001)
#define MCF_GPIO_PAR_UART_PAR_UTXD0         (0x0002)

#define MCF_GPIO_PAR_UART_PAR_URXD1         (0x0040)
#define MCF_GPIO_PAR_UART_PAR_UTXD1         (0x0080)

#define MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2   (0x02)
#define MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2   (0x04)

#define ICR_INTRCONF		0x05
#define MCFPIT_IMR		MCFINTC_IMRL
#define MCFPIT_IMR_IBIT	(1 << MCFINT_PIT1)

/****************************************************************************/
#endif  /* m520xsim_h */
