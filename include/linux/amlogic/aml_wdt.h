/*
 *  linux/drivers/char/watchdog/omap_wdt.h
 *
 *  BRIEF MODULE DESCRIPTION
 *      OMAP Watchdog timer register definitions
 *
 *  Copyright (C) 2004 Texas Instruments.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _AML_WATCHDOG_H
#define _AML_WATCHDOG_H
#include <mach/watchdog.h>
#include <mach/am_regs.h>
struct aml_wdt_dev {
	unsigned int min_timeout,max_timeout,default_timeout,reset_watchdog_time,shutdown_timeout;
	unsigned int firmware_timeout,suspend_timeout,timeout;
	unsigned int one_second;
	struct device *dev;
	struct mutex	lock;
	unsigned int reset_watchdog_method;
	struct delayed_work boot_queue;
};

#define AML_WDT_ENABLED (aml_read_reg32(P_WATCHDOG_TC)&(1 << WATCHDOG_ENABLE_BIT))
static inline void disable_watchdog(void)
{
	printk(KERN_INFO "** disable watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);
	aml_clr_reg32_mask(P_WATCHDOG_TC,(1 << WATCHDOG_ENABLE_BIT));
}
static inline void enable_watchdog(unsigned int timeout)
{
	printk(KERN_INFO "** enable watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);
	aml_write_reg32(P_WATCHDOG_TC, 1 << WATCHDOG_ENABLE_BIT |(timeout|WATCHDOG_COUNT_MASK));
}
static inline void reset_watchdog(void)
{
	printk(KERN_DEBUG"** reset watchdog\n");
	aml_write_reg32(P_WATCHDOG_RESET, 0);	
}
#ifdef CONFIG_AML_WDT
extern struct aml_wdt_dev *awdtv;
#else
struct aml_wdt_dev *awdtv=NULL;
#endif


#endif				/* _OMAP_WATCHDOG_H */
