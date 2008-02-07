/* platform data for the DS1WM driver */

struct ds1wm_platform_data {
	int bus_shift;	    /* number of shifts needed to calculate the
			     * offset between DS1WM registers;
			     * e.g. on h5xxx and h2200 this is 2
			     * (registers aligned to 4-byte boundaries),
			     * while on hx4700 this is 1 */
	int active_high;
	void (*enable)(struct platform_device *pdev);
	void (*disable)(struct platform_device *pdev);
};
