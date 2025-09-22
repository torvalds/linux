/*	$OpenBSD: sensors.h,v 1.37 2020/07/15 07:13:57 kettenis Exp $	*/

/*
 * Copyright (c) 2003, 2004 Alexander Yurchenko <grange@openbsd.org>
 * Copyright (c) 2006 Constantine A. Murenin <cnst+openbsd@bugmail.mojo.ru>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_SENSORS_H_
#define _SYS_SENSORS_H_

/* Sensor types */
enum sensor_type {
	SENSOR_TEMP,			/* temperature (uK) */
	SENSOR_FANRPM,			/* fan revolution speed */
	SENSOR_VOLTS_DC,		/* voltage (uV DC) */
	SENSOR_VOLTS_AC,		/* voltage (uV AC) */
	SENSOR_OHMS,			/* resistance */
	SENSOR_WATTS,			/* power (uW) */
	SENSOR_AMPS,			/* current (uA) */
	SENSOR_WATTHOUR,		/* power capacity (uWh) */
	SENSOR_AMPHOUR,			/* power capacity (uAh) */
	SENSOR_INDICATOR,		/* boolean indicator */
	SENSOR_INTEGER,			/* generic integer value */
	SENSOR_PERCENT,			/* percent (m%) */
	SENSOR_LUX,			/* illuminance (ulx) */
	SENSOR_DRIVE,			/* disk */
	SENSOR_TIMEDELTA,		/* system time error (nSec) */
	SENSOR_HUMIDITY,		/* humidity (m%RH) */
	SENSOR_FREQ,			/* frequency (uHz) */
	SENSOR_ANGLE,			/* angle (uDegrees) */
	SENSOR_DISTANCE,		/* distance (uMeter) */
	SENSOR_PRESSURE,		/* pressure (mPa) */
	SENSOR_ACCEL,			/* acceleration (u m/s^2) */
	SENSOR_VELOCITY,		/* velocity (u m/s) */
	SENSOR_ENERGY,			/* energy (uJ) */
	SENSOR_MAX_TYPES
};

#ifndef _KERNEL
static const char * const sensor_type_s[SENSOR_MAX_TYPES + 1] = {
	"temp",
	"fan",
	"volt",
	"acvolt",
	"resistance",
	"power",
	"current",
	"watthour",
	"amphour",
	"indicator",
	"raw",
	"percent",
	"illuminance",
	"drive",
	"timedelta",
	"humidity",
	"frequency",
	"angle",
	"distance",
	"pressure",
	"acceleration",
	"velocity",
	"energy",
	"undefined"
};
#endif	/* !_KERNEL */

#define SENSOR_DRIVE_EMPTY	1
#define SENSOR_DRIVE_READY	2
#define SENSOR_DRIVE_POWERUP	3
#define SENSOR_DRIVE_ONLINE	4
#define SENSOR_DRIVE_IDLE	5
#define SENSOR_DRIVE_ACTIVE	6
#define SENSOR_DRIVE_REBUILD	7
#define SENSOR_DRIVE_POWERDOWN	8
#define SENSOR_DRIVE_FAIL	9
#define SENSOR_DRIVE_PFAIL	10

/* Sensor states */
enum sensor_status {
	SENSOR_S_UNSPEC,		/* status is unspecified */
	SENSOR_S_OK,			/* status is ok */
	SENSOR_S_WARN,			/* status is warning */
	SENSOR_S_CRIT,			/* status is critical */
	SENSOR_S_UNKNOWN		/* status is unknown */
};

/* Sensor data:
 * New fields should be added at the end to encourage backwards compat
 */
struct sensor {
	char desc[32];			/* sensor description, may be empty */
	struct timeval tv;		/* sensor value last change time */
	int64_t value;			/* current value */
	enum sensor_type type;		/* sensor type */
	enum sensor_status status;	/* sensor status */
	int numt;			/* sensor number of .type type */
	int flags;			/* sensor flags */
#define SENSOR_FINVALID		0x0001	/* sensor is invalid */
#define SENSOR_FUNKNOWN		0x0002	/* sensor value is unknown */
};

/* Sensor device data:
 * New fields should be added at the end to encourage backwards compat
 */
struct sensordev {
	int num;			/* sensordev number */
	char xname[16];			/* unix device name */
	int maxnumt[SENSOR_MAX_TYPES];
	int sensors_count;
};

#ifdef _KERNEL

/* Sensor data */
struct ksensor {
	SLIST_ENTRY(ksensor) list;	/* device-scope list */
	char desc[32];			/* sensor description, may be empty */
	struct timeval tv;		/* sensor value last change time */
	int64_t value;			/* current value */
	enum sensor_type type;		/* sensor type */
	enum sensor_status status;	/* sensor status */
	int numt;			/* sensor number of .type type */
	int flags;			/* sensor flags, ie. SENSOR_FINVALID */
};
SLIST_HEAD(ksensors_head, ksensor);

/* Sensor device data */
struct ksensordev {
	SLIST_ENTRY(ksensordev)	list;
	int num;			/* sensordev number */
	char xname[16];			/* unix device name */
	int maxnumt[SENSOR_MAX_TYPES];
	int sensors_count;
	struct ksensors_head sensors_list;
};

/* struct ksensordev */
void			 sensordev_install(struct ksensordev *);
void			 sensordev_deinstall(struct ksensordev *);
int			 sensordev_get(int, struct ksensordev **);

/* struct ksensor */
void			 sensor_attach(struct ksensordev *, struct ksensor *);
void			 sensor_detach(struct ksensordev *, struct ksensor *);
int			 sensor_find(int, enum sensor_type, int, struct ksensor **);

/* task scheduling */
struct sensor_task;
struct sensor_task	*sensor_task_register(void *, void (*)(void *),
			     unsigned int);
void			 sensor_task_unregister(struct sensor_task *);
void			 sensor_quiesce(void);
void			 sensor_restart(void);

#endif	/* _KERNEL */

#endif	/* !_SYS_SENSORS_H_ */
