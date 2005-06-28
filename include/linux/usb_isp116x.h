
/*
 * Board initialization code should put one of these into dev->platform_data
 * and place the isp116x onto platform_bus.
 */

struct isp116x_platform_data {
	/* Enable internal resistors on downstream ports */
	unsigned sel15Kres:1;
	/* Chip's internal clock won't be stopped in suspended state.
	   Setting/unsetting this bit takes effect only if
	   'remote_wakeup_enable' below is not set. */
	unsigned clknotstop:1;
	/* On-chip overcurrent protection */
	unsigned oc_enable:1;
	/* INT output polarity */
	unsigned int_act_high:1;
	/* INT edge or level triggered */
	unsigned int_edge_triggered:1;
	/* WAKEUP pin connected - NOT SUPPORTED  */
	/* unsigned remote_wakeup_connected:1; */
	/* Wakeup by devices on usb bus enabled */
	unsigned remote_wakeup_enable:1;
	/* Switch or not to switch (keep always powered) */
	unsigned no_power_switching:1;
	/* Ganged port power switching (0) or individual port
	   power switching (1) */
	unsigned power_switching_mode:1;
	/* Given port_power, msec/2 after power on till power good */
	u8 potpg;
	/* Hardware reset set/clear. If implemented, this function must:
	   if set == 0,   deassert chip's HW reset pin
	   otherwise,     assert chip's HW reset pin       */
	void (*reset) (struct device * dev, int set);
	/* Hardware clock start/stop. If implemented, this function must:
	   if start == 0,    stop the external clock
	   otherwise,        start the external clock
	 */
	void (*clock) (struct device * dev, int start);
	/* Inter-io delay (ns). The chip is picky about access timings; it
	   expects at least:
	   150ns delay between consecutive accesses to DATA_REG,
	   300ns delay between access to ADDR_REG and DATA_REG
	   OE, WE MUST NOT be changed during these intervals
	 */
	void (*delay) (struct device * dev, int delay);
};
