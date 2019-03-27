/* $FreeBSD$ */
/*-
 * Copyright (c) 2013 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

extern int usleep(int);
extern void callout_process(int);
extern void usb_idle(void);
extern void usb_init(void);
extern void usb_uninit(void);

#define	hz 1000

#ifdef HAVE_MALLOC
void *
usb_malloc(size_t size)
{
	return (malloc(size));
}

void
usb_free(void *ptr)
{
	free(ptr);
}
#endif

void
DELAY(unsigned int delay)
{
	usleep(delay);
}

void
delay(unsigned int delay)
{
	usleep(delay);
}

int
pause(const char *what, int timeout)
{
	if (timeout == 0)
		timeout = 1;

	usleep((1000000 / hz) * timeout);

	return (0);
}

int
main(int argc, char **argv)
{
	uint32_t time;

	usb_init();

	time = 0;

	while (1) {

		usb_idle();

		usleep(1000);

		if (++time >= (1000 / hz)) {
			time = 0;
			callout_process(1);
		}
	}

	usb_uninit();

	return (0);
}
