/*
 * host.h - Tranport driver unsing Linux SDIO stack.
 *
 */

#include <linux/types.h>

/* This functions must be defined only when KSDIO_HOST_RESET_PIN is defined.
 * They allocate and control the host GPIO pin driving the SHUTDOWN_N pin
 * of NRX600.
 */
int host_gpio_allocate(int gpio_num);
int host_gpio_free(int gpio_num);
int host_gpio_set_level(int gpio_num, int level);
