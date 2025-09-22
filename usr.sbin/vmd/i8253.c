/* $OpenBSD: i8253.c,v 1.44 2025/06/17 16:01:41 dv Exp $ */
/*
 * Copyright (c) 2016 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/time.h>
#include <sys/types.h>

#include <dev/ic/i8253reg.h>
#include <dev/vmm/vmm.h>

#include <event.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "i8253.h"
#include "vmd.h"
#include "atomicio.h"

extern char *__progname;

/*
 * Channel 0 is used to generate the legacy hardclock interrupt (HZ).
 * Channels 1 and 2 can be used by the guest OS as regular timers,
 * but channel 2 is not connected to any pcppi(4)-like device. Like
 * a regular PC, channel 2 status can also be read from port 0x61.
 */
struct i8253_channel i8253_channel[3];

static struct vm_dev_pipe dev_pipe;

/*
 * i8253_pipe_dispatch
 *
 * Reads a message off the pipe, expecting one that corresponds to a
 * reset request for a specific channel.
 */
static void
i8253_pipe_dispatch(int fd, short event, void *arg)
{
	enum pipe_msg_type msg;

	msg = vm_pipe_recv(&dev_pipe);
	switch (msg) {
	case I8253_RESET_CHAN_0:
		i8253_reset(0);
		break;
	case I8253_RESET_CHAN_1:
		i8253_reset(1);
		break;
	case I8253_RESET_CHAN_2:
		i8253_reset(2);
		break;
	default:
		fatalx("%s: unexpected pipe message %d", __func__, msg);
	}
}

/*
 * i8253_init
 *
 * Initialize the emulated i8253 PIT.
 *
 * Parameters:
 *  vm_id: vmm(4)-assigned ID of the VM
 */
void
i8253_init(uint32_t vm_id)
{
	memset(&i8253_channel, 0, sizeof(struct i8253_channel));
	clock_gettime(CLOCK_MONOTONIC, &i8253_channel[0].ts);
	i8253_channel[0].start = 0xFFFF;
	i8253_channel[0].mode = TIMER_INTTC;
	i8253_channel[0].last_r = 1;
	i8253_channel[0].vm_id = vm_id;
	i8253_channel[0].state = 0;

	i8253_channel[1].start = 0xFFFF;
	i8253_channel[1].mode = TIMER_INTTC;
	i8253_channel[1].last_r = 1;
	i8253_channel[1].vm_id = vm_id;
	i8253_channel[1].state = 0;

	i8253_channel[2].start = 0xFFFF;
	i8253_channel[2].mode = TIMER_INTTC;
	i8253_channel[2].last_r = 1;
	i8253_channel[2].vm_id = vm_id;
	i8253_channel[2].state = 0;

	evtimer_set(&i8253_channel[0].timer, i8253_fire, &i8253_channel[0]);
	evtimer_set(&i8253_channel[1].timer, i8253_fire, &i8253_channel[1]);
	evtimer_set(&i8253_channel[2].timer, i8253_fire, &i8253_channel[2]);

	vm_pipe_init(&dev_pipe, i8253_pipe_dispatch);
	event_add(&dev_pipe.read_ev, NULL);
}

/*
 * i8253_do_readback
 *
 * Handles the readback status command. The readback status command latches
 * the current counter value plus various status bits.
 *
 * Parameters:
 *  data: The command word written by the guest VM
 */
void
i8253_do_readback(uint32_t data)
{
	struct timespec now, delta;
	uint64_t ns, ticks;
	int readback_channel[3] = { TIMER_RB_C0, TIMER_RB_C1, TIMER_RB_C2 };
	int i;

	/* bits are inverted here - !TIMER_RB_STATUS == enable chan readback */
	if (data & ~TIMER_RB_STATUS) {
		i8253_channel[0].rbs = (data & TIMER_RB_C0) ? 1 : 0;
		i8253_channel[1].rbs = (data & TIMER_RB_C1) ? 1 : 0;
		i8253_channel[2].rbs = (data & TIMER_RB_C2) ? 1 : 0;
	}

	/* !TIMER_RB_COUNT == enable counter readback */
	if (data & ~TIMER_RB_COUNT) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		for (i = 0; i < 3; i++) {
			if (data & readback_channel[i]) {
				timespecsub(&now, &i8253_channel[i].ts, &delta);
				ns = delta.tv_sec * 1000000000 + delta.tv_nsec;
				ticks = ns / NS_PER_TICK;
				if (i8253_channel[i].start)
					i8253_channel[i].olatch =
					    i8253_channel[i].start -
					    ticks % i8253_channel[i].start;
				else
					i8253_channel[i].olatch = 0;
			}
		}
	}
}

/*
 * vcpu_exit_i8253_misc
 *
 * Handles the 0x61 misc i8253 PIT register in/out exits.
 *
 * Parameters:
 *  vrp: vm run parameters containing exit information for the I/O
 *      instruction being performed
 *
 * Return value:
 *  Always 0xFF (no interrupt should be injected)
 */
uint8_t
vcpu_exit_i8253_misc(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;
	uint16_t cur;
	uint64_t ns, ticks;
	struct timespec now, delta;

	if (vei->vei.vei_dir == VEI_DIR_IN) {
		/* Port 0x61[5] = counter channel 2 state */
		if (i8253_channel[2].mode == TIMER_INTTC) {
			if (i8253_channel[2].state) {
				set_return_data(vei, (1 << 5));
				DPRINTF("%s: counter 2 fired, returning "
				    "0x20", __func__);
			} else {
				set_return_data(vei, 0);
				DPRINTF("%s: counter 2 clear, returning 0x0",
				    __func__);
			}
		} else if (i8253_channel[2].mode == TIMER_SQWAVE) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &i8253_channel[2].ts, &delta);
			ns = delta.tv_sec * 1000000000 + delta.tv_nsec;
			ticks = ns / NS_PER_TICK;
			if (i8253_channel[2].start) {
				cur = i8253_channel[2].start -
				    ticks % i8253_channel[2].start;

				if (cur > i8253_channel[2].start / 2)
					set_return_data(vei, 1);
				else
					set_return_data(vei, 0);
			}
		}
	} else {
		DPRINTF("%s: discarding data written to PIT misc port",
		    __func__);
	}

	return 0xFF;
}

/*
 * vcpu_exit_i8253
 *
 * Handles emulated i8253 PIT access (in/out instruction to PIT ports).
 *
 * Parameters:
 *  vrp: vm run parameters containing exit information for the I/O
 *      instruction being performed
 *
 * Return value:
 *  Interrupt to inject to the guest VM, or 0xFF if no interrupt should
 *      be injected.
 */
uint8_t
vcpu_exit_i8253(struct vm_run_params *vrp)
{
	uint32_t out_data = 0;
	uint8_t sel, rw, data;
	uint64_t ns, ticks;
	struct timespec now, delta;
	struct vm_exit *vei = vrp->vrp_exit;

	get_input_data(vei, &out_data);

	if (vei->vei.vei_port == TIMER_CTRL) {
		if (vei->vei.vei_dir == VEI_DIR_OUT) { /* OUT instruction */
			sel = out_data &
			    (TIMER_SEL0 | TIMER_SEL1 | TIMER_SEL2);
			sel = sel >> 6;

			if (sel == 3) {
				i8253_do_readback(out_data);
				return (0xFF);
			}

			rw = out_data & (TIMER_LATCH | TIMER_16BIT);

			/*
			 * Since we don't truly emulate each tick of the PIT
			 * counter, when the guest asks for the timer to be
			 * latched, simulate what the counter would have been
			 * had we performed full emulation. We do this by
			 * calculating when the counter was reset vs how much
			 * time has elapsed, then bias by the counter tick
			 * rate.
			 */
			if (rw == TIMER_LATCH) {
				clock_gettime(CLOCK_MONOTONIC, &now);
				timespecsub(&now, &i8253_channel[sel].ts,
				    &delta);
				ns = delta.tv_sec * 1000000000 + delta.tv_nsec;
				ticks = ns / NS_PER_TICK;
				if (i8253_channel[sel].start) {
					i8253_channel[sel].olatch =
					    i8253_channel[sel].start -
					    ticks % i8253_channel[sel].start;
				} else
					i8253_channel[sel].olatch = 0;
				goto ret;
			} else if (rw != TIMER_16BIT) {
				log_warnx("%s: i8253 PIT: unsupported counter "
				    "%d rw mode 0x%x selected", __func__,
				    sel, (rw & TIMER_16BIT));
			}
			i8253_channel[sel].mode = (out_data & 0xe) >> 1;

			goto ret;
		} else {
			log_warnx("%s: i8253 PIT: read from control port "
			    "unsupported", __progname);
			set_return_data(vei, 0);
		}
	} else {
		sel = vei->vei.vei_port - (TIMER_CNTR0 + TIMER_BASE);

		if (vei->vei.vei_dir == VEI_DIR_OUT) { /* OUT instruction */
			if (i8253_channel[sel].last_w == 0) {
				i8253_channel[sel].ilatch |= (out_data & 0xff);
				i8253_channel[sel].last_w = 1;
			} else {
				i8253_channel[sel].ilatch |=
				    ((out_data & 0xff) << 8);
				i8253_channel[sel].start =
				    i8253_channel[sel].ilatch;
				i8253_channel[sel].last_w = 0;

				if (i8253_channel[sel].start == 0)
					i8253_channel[sel].start = 0xffff;

				DPRINTF("%s: channel %d reset, mode=%d, "
				    "start=%d\n", __func__,
				    sel, i8253_channel[sel].mode,
				    i8253_channel[sel].start);

				vm_pipe_send(&dev_pipe, sel);
			}
		} else {
			if (i8253_channel[sel].rbs) {
				i8253_channel[sel].rbs = 0;
				data = i8253_channel[sel].mode << 1;
				data |= TIMER_16BIT;
				set_return_data(vei, data);
				goto ret;
			}

			if (i8253_channel[sel].last_r == 0) {
				data = i8253_channel[sel].olatch >> 8;
				set_return_data(vei, data);
				i8253_channel[sel].last_r = 1;
			} else {
				data = i8253_channel[sel].olatch & 0xFF;
				set_return_data(vei, data);
				i8253_channel[sel].last_r = 0;
			}
		}
	}

ret:
	return (0xFF);
}

/*
 * i8253_reset
 *
 * Resets the i8253's counter timer
 *
 * Parameters:
 *  chn: counter ID. Only channel ID 0 is presently emulated.
 */
void
i8253_reset(uint8_t chn)
{
	struct timeval tv;

	evtimer_del(&i8253_channel[chn].timer);
	timerclear(&tv);

	i8253_channel[chn].in_use = 1;
	i8253_channel[chn].state = 0;
	tv.tv_usec = (i8253_channel[chn].start * NS_PER_TICK) / 1000;
	clock_gettime(CLOCK_MONOTONIC, &i8253_channel[chn].ts);
	evtimer_add(&i8253_channel[chn].timer, &tv);
}

/*
 * i8253_fire
 *
 * Callback invoked when the 8253 PIT timer fires. This will assert
 * IRQ0 on the legacy PIC attached to VCPU0.
 *
 * Parameters:
 *  fd: unused
 *  type: unused
 *  arg: VM ID
 */
void
i8253_fire(int fd, short type, void *arg)
{
	struct timeval tv;
	struct i8253_channel *ctr = (struct i8253_channel *)arg;

	vcpu_assert_irq(ctr->vm_id, 0, 0);

	if (ctr->mode != TIMER_INTTC) {
		timerclear(&tv);
		tv.tv_usec = (ctr->start * NS_PER_TICK) / 1000;
		evtimer_add(&ctr->timer, &tv);
	} else
		ctr->state = 1;
}

void
i8253_stop(void)
{
	int i;
	for (i = 0; i < 3; i++)
		evtimer_del(&i8253_channel[i].timer);
	event_del(&dev_pipe.read_ev);
}

void
i8253_start(void)
{
	int i;
	for (i = 0; i < 3; i++)
		if (i8253_channel[i].in_use)
			i8253_reset(i);
	event_add(&dev_pipe.read_ev, NULL);
}
