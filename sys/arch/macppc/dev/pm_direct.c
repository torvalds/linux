/*	$OpenBSD: pm_direct.c,v 1.35 2023/11/22 18:14:35 tobhe Exp $	*/
/*	$NetBSD: pm_direct.c,v 1.9 2000/06/08 22:10:46 tsubai Exp $	*/

/*
 * Copyright (C) 1997 Takashi Hamada
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Takashi Hamada
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef DEBUG
#ifndef ADB_DEBUG
#define ADB_DEBUG
#endif
#endif

/* #define	PM_GRAB_SI	1 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/sensors.h>

#include <machine/cpu.h>

#include <dev/adb/adb.h>
#include <macppc/dev/adbvar.h>
#include <macppc/dev/pm_direct.h>
#include <macppc/dev/viareg.h>

/* hardware dependent values */
#define ADBDelay 100		/* XXX */

/* useful macros */
#define PM_SR()			read_via_reg(VIA1, vSR)
#define PM_VIA_INTR_ENABLE()	write_via_reg(VIA1, vIER, 0x90)
#define PM_VIA_INTR_DISABLE()	write_via_reg(VIA1, vIER, 0x10)
#define PM_VIA_CLR_INTR()	write_via_reg(VIA1, vIFR, 0x90)
#if 0
#define PM_SET_STATE_ACKON()	via_reg_or(VIA2, vBufB, 0x04)
#define PM_SET_STATE_ACKOFF()	via_reg_and(VIA2, vBufB, ~0x04)
#define PM_IS_ON		(0x02 == (read_via_reg(VIA2, vBufB) & 0x02))
#define PM_IS_OFF		(0x00 == (read_via_reg(VIA2, vBufB) & 0x02))
#else
#define PM_SET_STATE_ACKON()	via_reg_or(VIA2, vBufB, 0x10)
#define PM_SET_STATE_ACKOFF()	via_reg_and(VIA2, vBufB, ~0x10)
#define PM_IS_ON		(0x08 == (read_via_reg(VIA2, vBufB) & 0x08))
#define PM_IS_OFF		(0x00 == (read_via_reg(VIA2, vBufB) & 0x08))
#endif

/* these values shows that number of data returned after 'send' cmd is sent */
const signed char pm_send_cmd_type[] = {
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1,   -1,   -1, 0x00,
	  -1, 0x00, 0x02, 0x01, 0x01,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x04, 0x14,   -1, 0x03,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x02, 0x02,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1, 0x01,   -1,   -1,   -1,
	0x01, 0x00, 0x02, 0x02,   -1, 0x01, 0x03, 0x01,
	0x00, 0x01, 0x00, 0x00, 0x00,   -1,   -1,   -1,
	0x02,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   -1,   -1,
	0x01, 0x01, 0x01,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1,   -1, 0x04, 0x04,
	0x04,   -1, 0x00,   -1,   -1,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1,   -1,   -1,   -1,
	0x02, 0x02, 0x02, 0x04,   -1, 0x00,   -1,   -1,
	0x01, 0x01, 0x03, 0x02,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1, 0x00, 0x00,   -1,   -1,
	  -1, 0x04, 0x00,   -1,   -1,   -1,   -1,   -1,
	0x03,   -1, 0x00,   -1, 0x00,   -1,   -1, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1
};

/* these values shows that number of data returned after 'receive' cmd is sent */
const signed char pm_receive_cmd_type[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x05, 0x15,   -1, 0x02,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x03,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x04, 0x03, 0x09,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1, 0x01, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x06,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1, 0x02,   -1,   -1,   -1,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1, 0x02,   -1,   -1,   -1,   -1, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
};

int pm_old_env;
struct ksensor pm_lid_sens;
struct ksensordev pm_sensdev;

/*
 * Define the private functions
 */

/* for debugging */
#ifdef ADB_DEBUG
void	pm_printerr(char *, int, int, char *);
#endif

int	pm_wait_busy(int);
int	pm_wait_free(int);
int	pm_receive(u_char *);
int	pm_send(u_char);

/* these functions also use the variables of adb_direct.c */
void	pm_adb_get_TALK_result(PMData *);
void	pm_adb_get_ADB_data(PMData *);

void	pm_env_intr(PMData *);


extern int	hw_power;

/*
 * These variables are in adb_direct.c.
 */
extern u_char	*adbBuffer;	/* pointer to user data area */
extern void	*adbCompRout;	/* pointer to the completion routine */
extern void	*adbCompData;	/* pointer to the completion routine data */
extern int	adbWaiting;	/* waiting for return data from the device */
extern int	adbWaitingCmd;	/* ADB command we are waiting for */
extern int	adbStarting;	/* doing ADB reinit, so do "polling" differently */

#define	ADB_MAX_MSG_LENGTH	16
#define	ADB_MAX_HDR_LENGTH	8
struct adbCommand {
	u_char	header[ADB_MAX_HDR_LENGTH];	/* not used yet */
	u_char	data[ADB_MAX_MSG_LENGTH];	/* packet data only */
	u_char	*saveBuf;	/* where to save result */
	u_char	*compRout;	/* completion routine pointer */
	u_char	*compData;	/* completion routine data pointer */
	u_int	cmd;		/* the original command for this data */
	u_int	unsol;		/* 1 if packet was unsolicited */
	u_int	ack_only;	/* 1 for no special processing */
};
extern	void	adb_pass_up(struct adbCommand *);


#ifdef ADB_DEBUG
/*
 * This function dumps contents of the PMData
 */
void
pm_printerr(char *ttl, int rval, int num, char *data)
{
	int i;

	printf("pm: %s:%04x %02x ", ttl, rval, num);
	for (i = 0; i < num; i++)
		printf("%02x ", data[i]);
	printf("\n");
}
#endif

/*
 * Wait until PM IC is busy
 */
int
pm_wait_busy(int delay)
{
	while (PM_IS_ON) {
#ifdef PM_GRAB_SI
		(void)intr_dispatch(0x70);
#endif
		if ((--delay) < 0)
			return 1;	/* timeout */
	}
	return 0;
}


/*
 * Wait until PM IC is free
 */
int
pm_wait_free(int delay)
{
	while (PM_IS_OFF) {
#ifdef PM_GRAB_SI
		(void)intr_dispatch(0x70);
#endif
		if ((--delay) < 0)
			return 0;	/* timeout */
	}
	return 1;
}

/*
 * Functions for the PB Duo series and the PB 5XX series
 */

/*
 * Receive data from PM for the PB Duo series and the PB 5XX series
 */
int
pm_receive(u_char *data)
{
	int i;
	int rval;

	rval = 0xffffcd34;

	switch (1) {
	default:
		/* set VIA SR to input mode */
		via_reg_or(VIA1, vACR, 0x0c);
		via_reg_and(VIA1, vACR, ~0x10);
		i = PM_SR();

		PM_SET_STATE_ACKOFF();
		if (pm_wait_busy((int)ADBDelay*32) != 0)
			break;		/* timeout */

		PM_SET_STATE_ACKON();
		rval = 0xffffcd33;
		if (pm_wait_free((int)ADBDelay*32) == 0)
			break;		/* timeout */

		*data = PM_SR();
		rval = 0;

		break;
	}

	PM_SET_STATE_ACKON();
	via_reg_or(VIA1, vACR, 0x1c);

	return rval;
}

/*
 * Send data to PM for the PB Duo series and the PB 5XX series
 */
int
pm_send(u_char data)
{
	int rval;

	via_reg_or(VIA1, vACR, 0x1c);
	write_via_reg(VIA1, vSR, data);	/* PM_SR() = data; */

	PM_SET_STATE_ACKOFF();
	rval = 0xffffcd36;
	if (pm_wait_busy((int)ADBDelay*32) != 0) {
		PM_SET_STATE_ACKON();
		via_reg_or(VIA1, vACR, 0x1c);
		return rval;
	}

	PM_SET_STATE_ACKON();
	rval = 0xffffcd35;
	if (pm_wait_free((int)ADBDelay*32) != 0)
		rval = 0;

	PM_SET_STATE_ACKON();
	via_reg_or(VIA1, vACR, 0x1c);

	return rval;
}



/*
 * My PMgrOp routine for the PB Duo series and the PB 5XX series
 */
int
pmgrop(PMData *pmdata)
{
	int i;
	int s;
	u_char via1_vIER;
	int rval = 0;
	int num_pm_data = 0;
	u_char pm_cmd;
	short pm_num_rx_data;
	u_char pm_data;
	u_char *pm_buf;

	s = splhigh();

	/* disable all interrupts but PM */
	via1_vIER = 0x10;
	via1_vIER &= read_via_reg(VIA1, vIER);
	write_via_reg(VIA1, vIER, via1_vIER);
	if (via1_vIER != 0x0)
		via1_vIER |= 0x80;

	switch (pmdata->command) {
	default:
		/* wait until PM is free */
		pm_cmd = (u_char)(pmdata->command & 0xff);
		rval = 0xcd38;
		if (pm_wait_free(ADBDelay * 4) == 0)
			break;			/* timeout */

		/* send PM command */
		if ((rval = pm_send((u_char)(pm_cmd & 0xff))))
			break;				/* timeout */

		/* send number of PM data */
		num_pm_data = pmdata->num_data;
		if (pm_send_cmd_type[pm_cmd] < 0) {
			if ((rval = pm_send((u_char)(num_pm_data & 0xff))) != 0)
				break;		/* timeout */
			pmdata->command = 0;
		}
		/* send PM data */
		pm_buf = (u_char *)pmdata->s_buf;
		for (i = 0 ; i < num_pm_data; i++)
			if ((rval = pm_send(pm_buf[i])) != 0)
				break;			/* timeout */
		if (i != num_pm_data)
			break;				/* timeout */


		/* check if PM will send me data  */
		pm_num_rx_data = pm_receive_cmd_type[pm_cmd];
		pmdata->num_data = pm_num_rx_data;
		if (pm_num_rx_data == 0) {
			rval = 0;
			break;				/* no return data */
		}

		/* receive PM command */
		pm_data = pmdata->command;
		pm_num_rx_data--;
		if (pm_num_rx_data == 0)
			if ((rval = pm_receive(&pm_data)) != 0) {
				rval = 0xffffcd37;
				break;
			}
		pmdata->command = pm_data;

		/* receive number of PM data */
		if (pm_num_rx_data < 0) {
			if ((rval = pm_receive(&pm_data)) != 0)
				break;		/* timeout */
			num_pm_data = pm_data;
		} else
			num_pm_data = pm_num_rx_data;
		pmdata->num_data = num_pm_data;

		/* receive PM data */
		pm_buf = (u_char *)pmdata->r_buf;
		for (i = 0; i < num_pm_data; i++) {
			if ((rval = pm_receive(&pm_data)) != 0)
				break;			/* timeout */
			pm_buf[i] = pm_data;
		}

		rval = 0;
	}

	/* restore former value */
	write_via_reg(VIA1, vIER, via1_vIER);
	splx(s);

	return rval;
}

void
pm_in_adbattach(const char *devname)
{
	/* A PowerBook (including iBook) has a lid. */
	if (strncmp(hw_prod, "PowerBook", 9) == 0) {
		strlcpy(pm_sensdev.xname, devname,
		    sizeof(pm_sensdev.xname));
		strlcpy(pm_lid_sens.desc, "lid open",
		    sizeof(pm_lid_sens.desc));
		pm_lid_sens.type = SENSOR_INDICATOR;
		sensor_attach(&pm_sensdev, &pm_lid_sens);
		sensordev_install(&pm_sensdev);
		pm_lid_sens.value = 1; /* This is a guess. */
	}
}

/*
 * My PM interrupt routine for the PB Duo series and the PB 5XX series
 */
void
pm_intr(void)
{
	int s;
	int rval;
	PMData pmdata;

	s = splhigh();

	PM_VIA_CLR_INTR();			/* clear VIA1 interrupt */
						/* ask PM what happened */
	pmdata.command = PMU_INT_ACK;
	pmdata.num_data = 0;
	pmdata.s_buf = &pmdata.data[2];
	pmdata.r_buf = &pmdata.data[2];
	rval = pmgrop(&pmdata);
	if (rval != 0) {
#ifdef ADB_DEBUG
		if (adb_debug)
			printf("pm: PM is not ready. error code: %08x\n", rval);
#endif
		splx(s);
		return;
	}

	switch ((u_int)(pmdata.data[2] & 0xff)) {
	case 0x00:		/* 1 sec interrupt? */
		break;
	case PMU_INT_TICK:	/* 1 sec interrupt? */
		break;
	case PMU_INT_SNDBRT:	/* Brightness/Contrast button on LCD panel */
		break;
	case PMU_INT_ADB:	/* ADB data requested by TALK command */
	case PMU_INT_ADB|PMU_INT_ADB_AUTO:
		pm_adb_get_TALK_result(&pmdata);
		break;
	case 0x16:		/* ADB device event */
	case 0x18:
	case 0x1e:
		pm_adb_get_ADB_data(&pmdata);
		break;
	case PMU_INT_ENVIRONMENT:
		pm_env_intr(&pmdata);
		break;
	default:
#ifdef ADB_DEBUG
		if (adb_debug)
			pm_printerr("driver does not support this event.",
			    pmdata.data[2], pmdata.num_data,
			    pmdata.data);
#endif
		break;
	}

	splx(s);
}

/*
 * Synchronous ADBOp routine for the Power Manager
 */
int
pm_adb_op(u_char *buffer, void *compRout, void *data, int command)
{
	int i;
	int s;
	int rval;
	int ndelay;
	int waitfor;	/* interrupts to poll for */
	int ifr;
#ifdef ADB_DEBUG
	int oldifr;
#endif
	PMData pmdata;
	struct adbCommand packet;
	extern int adbempty;

	if (adbWaiting == 1)
		return 1;

	s = splhigh();
	write_via_reg(VIA1, vIER, 0x10);

	adbBuffer = buffer;
	adbCompRout = compRout;
	adbCompData = data;

	pmdata.command = 0x20;
	pmdata.s_buf = pmdata.data;
	pmdata.r_buf = pmdata.data;

	/*
	 * if the command is LISTEN,
	 * add number of ADB data to number of PM data
	 */
	if ((command & 0xc) == 0x8) {
		if (buffer != NULL)
			pmdata.num_data = buffer[0] + 3;
	} else
		pmdata.num_data = 3;

	/*
	 * Resetting adb on several models, such as
	 * - PowerBook3,*
	 * - PowerBook5,*
	 * - PowerMac10,1
	 * causes several pmu interrupts with ifr set to PMU_INT_SNDBRT.
	 * Not processing them prevents us from seeing the adb devices
	 * afterwards, so we have to expect it unless we know the adb
	 * bus is empty.
	 */
	if (command == PMU_RESET_ADB) {
		waitfor = PMU_INT_ADB_AUTO | PMU_INT_ADB;
		if (adbempty == 0)
			waitfor |= PMU_INT_SNDBRT;
	} else
		waitfor = PMU_INT_ALL;

	pmdata.data[0] = (u_char)(command & 0xff);
	pmdata.data[1] = 0;
	/* if the command is LISTEN, copy ADB data to PM buffer */
	if ((command & 0xc) == 0x8) {
		if (buffer != NULL && buffer[0] <= 24) {
			pmdata.data[2] = buffer[0];	/* number of data */
			for (i = 0; i < buffer[0]; i++)
				pmdata.data[3 + i] = buffer[1 + i];
		} else
			pmdata.data[2] = 0;
	} else
		pmdata.data[2] = 0;

	if ((command & 0xc) != 0xc) {	/* if the command is not TALK */
		/* set up stuff for adb_pass_up */
		packet.data[0] = 1 + pmdata.data[2];
		packet.data[1] = command;
		for (i = 0; i < pmdata.data[2]; i++)
			packet.data[i+2] = pmdata.data[i+3];
		packet.saveBuf = adbBuffer;
		packet.compRout = adbCompRout;
		packet.compData = adbCompData;
		packet.cmd = command;
		packet.unsol = 0;
		packet.ack_only = 1;
		adb_polling = 1;
		adb_pass_up(&packet);
		adb_polling = 0;
	}

	rval = pmgrop(&pmdata);
	if (rval != 0) {
		splx(s);
		return 1;
	}

	delay (1000);

	adbWaiting = 1;
	adbWaitingCmd = command;

	PM_VIA_INTR_ENABLE();

	/* wait until the PM interrupt is occurred */
	ndelay = 0x8000;
#ifdef ADB_DEBUG
	oldifr = 0;
#endif
	while (adbWaiting == 1) {
		ifr = read_via_reg(VIA1, vIFR);
		if (ifr & waitfor) {
			pm_intr();
#ifdef PM_GRAB_SI
			(void)intr_dispatch(0x70);
#endif
#ifdef ADB_DEBUG
		} else if (ifr != oldifr) {
			if (adb_debug)
				printf("pm_adb_op: ignoring ifr %02x"
				    ", expecting %02x\n",
				    (u_int)ifr, (u_int)waitfor);
			oldifr = ifr;
#endif
		}
		if ((--ndelay) < 0) {
			splx(s);
			return 1;
		}
		delay(10);
	}

	/* this command enables the interrupt by operating ADB devices */
	pmdata.command = PMU_ADB_CMD;
	pmdata.num_data = 4;
	pmdata.s_buf = pmdata.data;
	pmdata.r_buf = pmdata.data;
	pmdata.data[0] = 0x00;
	pmdata.data[1] = 0x86;	/* magic spell for awaking the PM */
	pmdata.data[2] = 0x00;
	pmdata.data[3] = 0x0c;	/* each bit may express the existent ADB device */
	rval = pmgrop(&pmdata);

	splx(s);
	return rval;
}


void
pm_adb_get_TALK_result(PMData *pmdata)
{
	int i;
	struct adbCommand packet;

	/* set up data for adb_pass_up */
	packet.data[0] = pmdata->num_data-1;
	packet.data[1] = pmdata->data[3];
	for (i = 0; i <packet.data[0]-1; i++)
		packet.data[i+2] = pmdata->data[i+4];

	packet.saveBuf = adbBuffer;
	packet.compRout = adbCompRout;
	packet.compData = adbCompData;
	packet.unsol = 0;
	packet.ack_only = 0;
	adb_polling = 1;
	adb_pass_up(&packet);
	adb_polling = 0;

	adbWaiting = 0;
	adbBuffer = NULL;
	adbCompRout = NULL;
	adbCompData = NULL;
}


void
pm_adb_get_ADB_data(PMData *pmdata)
{
	int i;
	struct adbCommand packet;

	/* set up data for adb_pass_up */
	packet.data[0] = pmdata->num_data-1;	/* number of raw data */
	packet.data[1] = pmdata->data[3];	/* ADB command */
	for (i = 0; i <packet.data[0]-1; i++)
		packet.data[i+2] = pmdata->data[i+4];
	packet.unsol = 1;
	packet.ack_only = 0;
	adb_pass_up(&packet);
}

void
pm_env_intr(PMData *pmdata)
{
	int env, old;

	/* We might have 3 bytes data[3..5], but use only data[3]. */
	if (pmdata->num_data < 3)
		return;
	env = pmdata->data[3];
	old = pm_old_env;

	pm_lid_sens.value = !(env & PMU_ENV_LID_CLOSED);
	if (!(old & PMU_ENV_LID_CLOSED) && (env & PMU_ENV_LID_CLOSED))
		adb_lid_closed_intr();

	hw_power = !!(env & PMU_ENV_AC_POWER);

	/*
	 * Act if one presses and releases the power button on a Mac
	 * with no ADB keyboard.
	 */
	if ((old & PMU_ENV_POWER_BUTTON) && !(env & PMU_ENV_POWER_BUTTON))
		adb_power_button_intr();

	pm_old_env = env;
}

void
pm_adb_restart(void)
{
	PMData p;

	p.command = PMU_RESET_CPU;
	p.num_data = 0;
	p.s_buf = p.data;
	p.r_buf = p.data;
	pmgrop(&p);
}

void
pm_adb_poweroff(void)
{
	PMData p;

	bzero(&p, sizeof p);
	p.command = PMU_POWER_OFF;
	p.num_data = 4;
	p.s_buf = p.data;
	p.r_buf = p.data;
	strlcpy(p.data, "MATT", sizeof p.data);
	pmgrop(&p);
}

void
pm_read_date_time(time_t *time)
{
	PMData p;
	u_int32_t t;

	p.command = PMU_READ_RTC;
	p.num_data = 0;
	p.s_buf = p.data;
	p.r_buf = p.data;
	pmgrop(&p);

	bcopy(p.data, &t, sizeof(t));
	*time = (time_t)t;
}

void
pm_set_date_time(time_t time)
{
	PMData p;
	u_int32_t t = time;		/* XXX eventually truncates */

	p.command = PMU_SET_RTC;
	p.num_data = sizeof(t);
	p.s_buf = p.r_buf = p.data;
	bcopy(&t, p.data, sizeof(t));
	pmgrop(&p);
}

#if 0
void
pm_eject_pcmcia(int slot)
{
	PMData p;

	if (slot != 0 && slot != 1)
		return;

	p.command = PMU_EJECT_PCMCIA;
	p.num_data = 1;
	p.s_buf = p.r_buf = p.data;
	p.data[0] = 5 + slot;	/* XXX */
	pmgrop(&p);
}
#endif


/*
 * Thanks to Paul Mackerras and Fabio Riccardi's Linux implementation
 * for a clear description of the PMU results.
 */

int
pm_battery_info(int battery, struct pmu_battery_info *info)
{
	PMData p;

	p.command = PMU_SMART_BATTERY_STATE;
	p.num_data = 1;
	p.s_buf = p.r_buf = p.data;
	p.data[0] = battery + 1;
	pmgrop(&p);

	info->flags = p.data[1];
	hw_power = !!(info->flags & PMU_PWR_AC_PRESENT);

	switch (p.data[0]) {
	case 3:
	case 4:
		info->cur_charge = p.data[2];
		info->max_charge = p.data[3];
		info->draw = *((signed char *)&p.data[4]);
		info->voltage = p.data[5];
		break;
	case 5:
		info->cur_charge = ((p.data[2] << 8) | (p.data[3]));
		info->max_charge = ((p.data[4] << 8) | (p.data[5]));
		info->draw = *((signed short *)&p.data[6]);
		info->voltage = ((p.data[8] << 8) | (p.data[7]));
		break;
	default:
		/* XXX - Error condition */
		info->cur_charge = 0;
		info->max_charge = 0;
		info->draw = 0;
		info->voltage = 0;
		break;
	}

	return 1;
}

void
pmu_fileserver_mode(int on)
{
	PMData p;

	p.command = PMU_POWER_EVENTS;
	p.num_data = 1;
	p.s_buf = p.r_buf = p.data;
	p.data[0] = PMU_PWR_GET_POWERUP_EVENTS;
	pmgrop(&p);

	p.command = PMU_POWER_EVENTS;
	p.num_data = 3;
	p.s_buf = p.r_buf = p.data;
	p.data[1] = p.data[0];   /* result from the get */
	if (on) {
		p.data[0] = PMU_PWR_SET_POWERUP_EVENTS;
		p.data[2] = PMU_WAKE_AC_LOSS;
	} else {
		p.data[0] = PMU_PWR_CLR_POWERUP_EVENTS;
		p.data[2] = PMU_WAKE_AC_LOSS;
	}
	pmgrop(&p);
}

int
pmu_set_kbl(unsigned int level)
{
	if (level > 0xff)
		return (EINVAL);

	PMData p;

	p.command = 0x4F;
	p.num_data = 3;
	p.s_buf = p.r_buf = p.data;
	p.data[0] = 0;
	p.data[1] = 0;
	p.data[2] = level;
	pmgrop(&p);
	return (0);
}

