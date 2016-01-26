/*
 * Analog Devices ADV7511 HDMI Transmitter Device Driver
 *
 * Copyright 2013 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ADV7511_H
#define ADV7511_H

/* notify events */
#define ADV7511_MONITOR_DETECT 0
#define ADV7511_EDID_DETECT 1


struct adv7511_monitor_detect {
	int present;
};

struct adv7511_edid_detect {
	int present;
	int segment;
};

struct adv7511_cec_arg {
	void *arg;
	u32 f_flags;
};

struct adv7511_platform_data {
	u8 i2c_edid;
	u8 i2c_cec;
	u8 i2c_pktmem;
	u32 cec_clk;
};

#endif
