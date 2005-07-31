/*
    i2c-sensor.h - Part of the i2c package
    was originally sensors.h - Part of lm_sensors, Linux kernel modules
                               for hardware monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _LINUX_I2C_SENSOR_H
#define _LINUX_I2C_SENSOR_H

#include <linux/i2c.h>

#define SENSORS_MODULE_PARM_FORCE(name) \
  I2C_CLIENT_MODULE_PARM(force_ ## name, \
                      "List of adapter,address pairs which are unquestionably" \
                      " assumed to contain a `" # name "' chip")


/* This defines several insmod variables, and the addr_data structure */
#define SENSORS_INSMOD \
  I2C_CLIENT_MODULE_PARM(probe, \
                      "List of adapter,address pairs to scan additionally"); \
  I2C_CLIENT_MODULE_PARM(ignore, \
                      "List of adapter,address pairs not to scan"); \
	static struct i2c_client_address_data addr_data = {		\
			.normal_i2c =		normal_i2c,		\
			.probe =		probe,			\
			.ignore =		ignore,			\
			.forces =		forces,			\
		}

/* The following functions create an enum with the chip names as elements. 
   The first element of the enum is any_chip. These are the only macros
   a module will want to use. */

#define SENSORS_INSMOD_0 \
  enum chips { any_chip }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  static unsigned short *forces[] = { force, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_1(chip1) \
  enum chips { any_chip, chip1 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_2(chip1,chip2) \
  enum chips { any_chip, chip1, chip2 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_3(chip1,chip2,chip3) \
  enum chips { any_chip, chip1, chip2, chip3 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_4(chip1,chip2,chip3,chip4) \
  enum chips { any_chip, chip1, chip2, chip3, chip4 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  SENSORS_MODULE_PARM_FORCE(chip4); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      force_##chip4, \
				      NULL}; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_5(chip1,chip2,chip3,chip4,chip5) \
  enum chips { any_chip, chip1, chip2, chip3, chip4, chip5 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  SENSORS_MODULE_PARM_FORCE(chip4); \
  SENSORS_MODULE_PARM_FORCE(chip5); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      force_##chip4, \
				      force_##chip5, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_6(chip1,chip2,chip3,chip4,chip5,chip6) \
  enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  SENSORS_MODULE_PARM_FORCE(chip4); \
  SENSORS_MODULE_PARM_FORCE(chip5); \
  SENSORS_MODULE_PARM_FORCE(chip6); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      force_##chip4, \
				      force_##chip5, \
				      force_##chip6, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_7(chip1,chip2,chip3,chip4,chip5,chip6,chip7) \
  enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6, chip7 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  SENSORS_MODULE_PARM_FORCE(chip4); \
  SENSORS_MODULE_PARM_FORCE(chip5); \
  SENSORS_MODULE_PARM_FORCE(chip6); \
  SENSORS_MODULE_PARM_FORCE(chip7); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      force_##chip4, \
				      force_##chip5, \
				      force_##chip6, \
				      force_##chip7, \
				      NULL }; \
  SENSORS_INSMOD

#define SENSORS_INSMOD_8(chip1,chip2,chip3,chip4,chip5,chip6,chip7,chip8) \
  enum chips { any_chip, chip1, chip2, chip3, chip4, chip5, chip6, chip7, chip8 }; \
  I2C_CLIENT_MODULE_PARM(force, \
                      "List of adapter,address pairs to boldly assume " \
                      "to be present"); \
  SENSORS_MODULE_PARM_FORCE(chip1); \
  SENSORS_MODULE_PARM_FORCE(chip2); \
  SENSORS_MODULE_PARM_FORCE(chip3); \
  SENSORS_MODULE_PARM_FORCE(chip4); \
  SENSORS_MODULE_PARM_FORCE(chip5); \
  SENSORS_MODULE_PARM_FORCE(chip6); \
  SENSORS_MODULE_PARM_FORCE(chip7); \
  SENSORS_MODULE_PARM_FORCE(chip8); \
  static unsigned short *forces[] = { force, \
				      force_##chip1, \
				      force_##chip2, \
				      force_##chip3, \
				      force_##chip4, \
				      force_##chip5, \
				      force_##chip6, \
				      force_##chip7, \
				      force_##chip8, \
				      NULL }; \
  SENSORS_INSMOD

/* Detect function. It iterates over all possible addresses itself. For
   SMBus addresses, it will only call found_proc if some client is connected
   to the SMBus (unless a 'force' matched). */
extern int i2c_detect(struct i2c_adapter *adapter,
		      struct i2c_client_address_data *address_data,
		      int (*found_proc) (struct i2c_adapter *, int, int));

#endif				/* def _LINUX_I2C_SENSOR_H */
