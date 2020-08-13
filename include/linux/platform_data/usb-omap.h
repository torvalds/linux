/*
 * usb-omap.h - Platform data for the various OMAP USB IPs
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 2, as published by the Free Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define OMAP3_HS_USB_PORTS	3

enum usbhs_omap_port_mode {
	OMAP_USBHS_PORT_MODE_UNUSED,
	OMAP_EHCI_PORT_MODE_PHY,
	OMAP_EHCI_PORT_MODE_TLL,
	OMAP_EHCI_PORT_MODE_HSIC,
	OMAP_OHCI_PORT_MODE_PHY_6PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_PHY_6PIN_DPDM,
	OMAP_OHCI_PORT_MODE_PHY_3PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_PHY_4PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_6PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_6PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_3PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_4PIN_DPDM,
	OMAP_OHCI_PORT_MODE_TLL_2PIN_DATSE0,
	OMAP_OHCI_PORT_MODE_TLL_2PIN_DPDM
};

struct usbtll_omap_platform_data {
	enum usbhs_omap_port_mode		port_mode[OMAP3_HS_USB_PORTS];
};

struct ehci_hcd_omap_platform_data {
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	int				reset_gpio_port[OMAP3_HS_USB_PORTS];
	struct regulator		*regulator[OMAP3_HS_USB_PORTS];
	unsigned			phy_reset:1;
};

struct ohci_hcd_omap_platform_data {
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	unsigned			es2_compatibility:1;
};

struct usbhs_omap_platform_data {
	int				nports;
	enum usbhs_omap_port_mode	port_mode[OMAP3_HS_USB_PORTS];
	int				reset_gpio_port[OMAP3_HS_USB_PORTS];
	struct regulator		*regulator[OMAP3_HS_USB_PORTS];

	struct ehci_hcd_omap_platform_data	*ehci_data;
	struct ohci_hcd_omap_platform_data	*ohci_data;

	/* OMAP3 <= ES2.1 have a single ulpi bypass control bit */
	unsigned single_ulpi_bypass:1;
	unsigned es2_compatibility:1;
	unsigned phy_reset:1;
};

/*-------------------------------------------------------------------------*/

struct omap_musb_board_data {
	u8	interface_type;
	u8	mode;
	u16	power;
	unsigned extvbus:1;
	void	(*set_phy_power)(u8 on);
	void	(*clear_irq)(void);
	void	(*set_mode)(u8 mode);
	void	(*reset)(void);
};

enum musb_interface {
	MUSB_INTERFACE_ULPI,
	MUSB_INTERFACE_UTMI
};
