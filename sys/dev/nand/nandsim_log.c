/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/alq.h>
#include <sys/time.h>

#include <machine/stdarg.h>

#include <dev/nand/nandsim_log.h>

int  nandsim_log_level;
int  nandsim_log_output;
int  log_size = NANDSIM_RAM_LOG_SIZE;

static int  nandsim_entry_size = NANDSIM_ENTRY_SIZE;
static int  nandsim_entry_count = NANDSIM_ENTRY_COUNT;
static int  str_index = 0;
static char string[NANDSIM_ENTRY_SIZE + 1] = {0};

int
nandsim_log_init(struct nandsim_softc *sc, char *filename)
{
	int error = 0;

	if (nandsim_log_output == NANDSIM_OUTPUT_FILE) {
		error = alq_open(&sc->alq, filename,
		    curthread->td_ucred, 0644,
		    nandsim_entry_size, nandsim_entry_count);
	} else if (nandsim_log_output == NANDSIM_OUTPUT_RAM) {
		sc->log_buff = malloc(log_size, M_NANDSIM, M_WAITOK | M_ZERO);
		if (!sc->log_buff)
			error = ENOMEM;
	}

	return (error);
}

void
nandsim_log_close(struct nandsim_softc *sc)
{

	if (nandsim_log_output == NANDSIM_OUTPUT_FILE) {
		memset(&string[str_index], 0, NANDSIM_ENTRY_SIZE - str_index);
		alq_write(sc->alq, (void *) string, ALQ_NOWAIT);
		str_index = 0;
		string[0] = '\0';
		alq_close(sc->alq);
	} else if (nandsim_log_output == NANDSIM_OUTPUT_RAM) {
		free(sc->log_buff, M_NANDSIM);
		sc->log_buff = NULL;
	}
}

void
nandsim_log(struct nandsim_chip *chip, int level, const char *fmt, ...)
{
	char hdr[TIME_STR_SIZE];
	char tmp[NANDSIM_ENTRY_SIZE];
	struct nandsim_softc *sc;
	struct timeval currtime;
	va_list ap;
	int hdr_len, len, rest;

	if (nandsim_log_output == NANDSIM_OUTPUT_NONE)
		return;

	if (chip == NULL)
		return;

	sc = chip->sc;
	if (!sc->alq && nandsim_log_output == NANDSIM_OUTPUT_FILE)
		return;

	if (level <= nandsim_log_level) {
		microtime(&currtime);
		hdr_len = sprintf(hdr, "%08jd.%08li [chip:%d, ctrl:%d]: ",
		    (intmax_t)currtime.tv_sec, currtime.tv_usec,
		    chip->chip_num, chip->ctrl_num);

		switch(nandsim_log_output) {
		case NANDSIM_OUTPUT_CONSOLE:
			printf("%s", hdr);
			va_start(ap, fmt);
			vprintf(fmt, ap);
			va_end(ap);
			break;
		case NANDSIM_OUTPUT_RAM:
			va_start(ap, fmt);
			len = vsnprintf(tmp, NANDSIM_ENTRY_SIZE - 1, fmt, ap);
			tmp[NANDSIM_ENTRY_SIZE - 1] = 0;
			va_end(ap);

			rest = log_size - sc->log_idx - 1;
			if (rest >= hdr_len) {
				bcopy(hdr, &sc->log_buff[sc->log_idx],
				    hdr_len);
				sc->log_idx += hdr_len;
				sc->log_buff[sc->log_idx] = 0;
			} else {
				bcopy(hdr, &sc->log_buff[sc->log_idx], rest);
				bcopy(&hdr[rest], sc->log_buff,
				    hdr_len - rest);
				sc->log_idx = hdr_len - rest;
				sc->log_buff[sc->log_idx] = 0;
			}

			rest = log_size - sc->log_idx - 1;
			if (rest >= len) {
				bcopy(tmp, &sc->log_buff[sc->log_idx], len);
				sc->log_idx += len;
				sc->log_buff[sc->log_idx] = 0;
			} else {
				bcopy(tmp, &sc->log_buff[sc->log_idx], rest);
				bcopy(&tmp[rest], sc->log_buff, len - rest);
				sc->log_idx = len - rest;
				sc->log_buff[sc->log_idx] = 0;
			}

			break;

		case NANDSIM_OUTPUT_FILE:
			va_start(ap, fmt);
			len = vsnprintf(tmp, NANDSIM_ENTRY_SIZE - 1, fmt, ap);
			tmp[NANDSIM_ENTRY_SIZE - 1] = 0;
			va_end(ap);

			rest = NANDSIM_ENTRY_SIZE - str_index;
			if (rest >= hdr_len) {
				strcat(string, hdr);
				str_index += hdr_len;
			} else {
				strlcat(string, hdr, NANDSIM_ENTRY_SIZE + 1);
				alq_write(sc->alq, (void *) string,
				    ALQ_NOWAIT);
				strcpy(string, &hdr[rest]);
				str_index = hdr_len - rest;
			}
			rest = NANDSIM_ENTRY_SIZE - str_index;
			if (rest >= len) {
				strcat(string, tmp);
				str_index += len;
			} else {
				strlcat(string, tmp, NANDSIM_ENTRY_SIZE + 1);
				alq_write(sc->alq, (void *) string,
				    ALQ_NOWAIT);
				strcpy(string, &tmp[rest]);
				str_index = len - rest;
			}
			break;
		default:
			break;
		}
	}
}
