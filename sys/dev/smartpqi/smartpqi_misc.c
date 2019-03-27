/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

#include "smartpqi_includes.h"

/*
 * Populate hostwell time variables in bcd format from FreeBSD format 
 */
void os_get_time(struct bmic_host_wellness_time *host_wellness_time)
{
	struct timespec ts;
	struct clocktime ct;

	getnanotime(&ts);
	clock_ts_to_ct(&ts, &ct);


	/* Fill the time In BCD Format */
	host_wellness_time->hour= (uint8_t)bin2bcd(ct.hour);
	host_wellness_time->min = (uint8_t)bin2bcd(ct.min);
	host_wellness_time->sec= (uint8_t)bin2bcd(ct.sec);
	host_wellness_time->reserved = 0;
	host_wellness_time->month = (uint8_t)bin2bcd(ct.mon);
	host_wellness_time->day = (uint8_t)bin2bcd(ct.day);
	host_wellness_time->century = (uint8_t)bin2bcd(ct.year / 100);
	host_wellness_time->year = (uint8_t)bin2bcd(ct.year % 100);

}	

/*
 * Update host time to f/w every 24 hours in a periodic timer.
 */

void os_wellness_periodic(void *data)
{
	struct pqisrc_softstate *softs = (struct pqisrc_softstate *)data;
	int ret = 0;


	/* update time to FW */
	if (!pqisrc_ctrl_offline(softs)){
		if( (ret = pqisrc_write_current_time_to_host_wellness(softs)) != 0 )
			DBG_ERR("Failed to update time to FW in periodic ret = %d\n", ret);
	}

	/* reschedule ourselves */
	softs->os_specific.wellness_periodic = timeout(os_wellness_periodic, 
					softs, OS_HOST_WELLNESS_TIMEOUT * hz);
}

/*
 * Routine used to stop the heart-beat timer
 */
void os_stop_heartbeat_timer(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN\n");

	/* Kill the heart beat event */
	untimeout(os_start_heartbeat_timer, softs, 
			softs->os_specific.heartbeat_timeout_id);

	DBG_FUNC("OUT\n");
}

/*
 * Routine used to start the heart-beat timer
 */
void os_start_heartbeat_timer(void *data)
{
	struct pqisrc_softstate *softs = (struct pqisrc_softstate *)data;
	DBG_FUNC("IN\n");

	pqisrc_heartbeat_timer_handler(softs);
	if (!pqisrc_ctrl_offline(softs)) {
		softs->os_specific.heartbeat_timeout_id =
		timeout(os_start_heartbeat_timer, softs,
		OS_FW_HEARTBEAT_TIMER_INTERVAL * hz);
	}

       DBG_FUNC("OUT\n");
}

/*
 * Mutex initialization function
 */
int os_init_spinlock(struct pqisrc_softstate *softs, struct mtx *lock, 
			char *lockname)
{
    mtx_init(lock, lockname, NULL, MTX_SPIN);
    return 0;

}

/*
 * Mutex uninitialization function
 */
void os_uninit_spinlock(struct mtx *lock)
{
    mtx_destroy(lock);
    return;

}

/*
 * Semaphore initialization function
 */
int os_create_semaphore(const char *name, int value, struct sema *sema)
{
    sema_init(sema, value, name);
    return PQI_STATUS_SUCCESS;

}

/*
 * Semaphore uninitialization function
 */
int os_destroy_semaphore(struct sema *sema)
{
    sema_destroy(sema);
    return PQI_STATUS_SUCCESS;

}

/*
 * Semaphore grab function
 */
void inline os_sema_lock(struct sema *sema)
{
	sema_post(sema);
}

/*
 * Semaphore release function
 */
void inline os_sema_unlock(struct sema *sema)
{
	sema_wait(sema);
}


/*
 * string copy wrapper function
 */
int os_strlcpy(char *dst, char *src, int size)
{
	return strlcpy(dst, src, size);
}
