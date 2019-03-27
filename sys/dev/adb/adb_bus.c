/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Nathan Whitehorn
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
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "adb.h"
#include "adbvar.h"

static int adb_bus_probe(device_t dev);
static int adb_bus_attach(device_t dev);
static int adb_bus_detach(device_t dev);
static void adb_bus_enumerate(void *xdev);
static void adb_probe_nomatch(device_t dev, device_t child);
static int adb_print_child(device_t dev, device_t child);

static int adb_send_raw_packet_sync(device_t dev, uint8_t to, uint8_t command, uint8_t reg, int len, u_char *data, u_char *reply);

static char *adb_device_string[] = {
	"HOST", "dongle", "keyboard", "mouse", "tablet", "modem", "RESERVED", "misc"
};

static device_method_t adb_bus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		adb_bus_probe),
	DEVMETHOD(device_attach,	adb_bus_attach),
	DEVMETHOD(device_detach,        adb_bus_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),

	/* Bus Interface */
        DEVMETHOD(bus_probe_nomatch,    adb_probe_nomatch),
        DEVMETHOD(bus_print_child,	adb_print_child),

	{ 0, 0 },
};

driver_t adb_driver = {
	"adb",
	adb_bus_methods,
	sizeof(struct adb_softc),
};

devclass_t adb_devclass;

static int
adb_bus_probe(device_t dev)
{
	device_set_desc(dev, "Apple Desktop Bus");
	return (0);
}

static int
adb_bus_attach(device_t dev)
{
	struct adb_softc *sc = device_get_softc(dev);
	sc->enum_hook.ich_func = adb_bus_enumerate;
	sc->enum_hook.ich_arg = dev;

	/*
	 * We should wait until interrupts are enabled to try to probe
	 * the bus. Enumerating the ADB involves receiving packets,
	 * which works best with interrupts enabled.
	 */
	
	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	return (0);
}
	
static void
adb_bus_enumerate(void *xdev)
{
	device_t dev = (device_t)xdev;

	struct adb_softc *sc = device_get_softc(dev);
	uint8_t i, next_free;
	uint16_t r3;

	sc->sc_dev = dev;
	sc->parent = device_get_parent(dev);

	sc->packet_reply = 0;
	sc->autopoll_mask = 0;
	sc->sync_packet = 0xffff;

	/* Initialize devinfo */
	for (i = 0; i < 16; i++) {
		sc->devinfo[i].address = i;
		sc->devinfo[i].default_address = 0;
	}
	
	/* Reset ADB bus */
	adb_send_raw_packet_sync(dev,0,ADB_COMMAND_BUS_RESET,0,0,NULL,NULL);
	DELAY(1500);

	/* Enumerate bus */
	next_free = 8;

	for (i = 1; i <= 7; i++) {
	    int8_t first_relocated = -1;
	    int reply = 0;

	    do {
		reply = adb_send_raw_packet_sync(dev,i,
			    ADB_COMMAND_TALK,3,0,NULL,NULL);
	
		if (reply) {
			/* If we got a response, relocate to next_free */
			r3 = sc->devinfo[i].register3;
			r3 &= 0xf000;
			r3 |= ((uint16_t)(next_free) & 0x000f) << 8;
			r3 |= 0x00fe;

			adb_send_raw_packet_sync(dev,i, ADB_COMMAND_LISTEN,3,
			    sizeof(uint16_t),(u_char *)(&r3),NULL);

			adb_send_raw_packet_sync(dev,next_free,
			    ADB_COMMAND_TALK,3,0,NULL,NULL);

			sc->devinfo[next_free].default_address = i;
			if (first_relocated < 0)
				first_relocated = next_free;

			next_free++;
		} else if (first_relocated > 0) {
			/* Collisions removed, relocate first device back */

			r3 = sc->devinfo[i].register3;
			r3 &= 0xf000;
			r3 |= ((uint16_t)(i) & 0x000f) << 8;
			
			adb_send_raw_packet_sync(dev,first_relocated,
			    ADB_COMMAND_LISTEN,3,
			    sizeof(uint16_t),(u_char *)(&r3),NULL);
			adb_send_raw_packet_sync(dev,i,
			    ADB_COMMAND_TALK,3,0,NULL,NULL);

			sc->devinfo[i].default_address = i;
			sc->devinfo[(int)(first_relocated)].default_address = 0;
			break;
		}
	    } while (reply);
	}

	for (i = 0; i < 16; i++) {
		if (sc->devinfo[i].default_address) {
			sc->children[i] = device_add_child(dev, NULL, -1);
			device_set_ivars(sc->children[i], &sc->devinfo[i]);
		}
	}

	bus_generic_attach(dev);

	config_intrhook_disestablish(&sc->enum_hook);
}

static int adb_bus_detach(device_t dev)
{
	return (bus_generic_detach(dev));
}
	

static void
adb_probe_nomatch(device_t dev, device_t child)
{
	struct adb_devinfo *dinfo;

	if (bootverbose) {
		dinfo = device_get_ivars(child);

		device_printf(dev,"ADB %s at device %d (no driver attached)\n",
		    adb_device_string[dinfo->default_address],dinfo->address);
	}
}

u_int
adb_receive_raw_packet(device_t dev, u_char status, u_char command, int len, 
    u_char *data) 
{
	struct adb_softc *sc = device_get_softc(dev);
	u_char addr = command >> 4;

	if (len > 0 && (command & 0x0f) == ((ADB_COMMAND_TALK << 2) | 3)) {
		memcpy(&sc->devinfo[addr].register3,data,2);
		sc->devinfo[addr].handler_id = data[1];
	}

	if (sc->sync_packet == command)  {
		memcpy(sc->syncreg,data,(len > 8) ? 8 : len);
		atomic_store_rel_int(&sc->packet_reply,len + 1);
		wakeup(sc);
	}

	if (sc->children[addr] != NULL) {
		ADB_RECEIVE_PACKET(sc->children[addr],status,
			(command & 0x0f) >> 2,command & 0x03,len,data);
	}
	
	return (0);
}

static int
adb_print_child(device_t dev, device_t child)
{
	struct adb_devinfo *dinfo;
	int retval = 0;
	
	dinfo = device_get_ivars(child);
	
	retval += bus_print_child_header(dev,child);
	printf(" at device %d",dinfo->address);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

u_int 
adb_send_packet(device_t dev, u_char command, u_char reg, int len, u_char *data)
{
	u_char command_byte = 0;
	struct adb_devinfo *dinfo;
	struct adb_softc *sc;

	sc = device_get_softc(device_get_parent(dev));
	dinfo = device_get_ivars(dev);
	
	command_byte |= dinfo->address << 4;
	command_byte |= command << 2;
	command_byte |= reg;

	ADB_HB_SEND_RAW_PACKET(sc->parent, command_byte, len, data, 1);

	return (0);
}

u_int
adb_set_autopoll(device_t dev, u_char enable) 
{
	struct adb_devinfo *dinfo;
	struct adb_softc *sc;
	uint16_t mod = 0;

	sc = device_get_softc(device_get_parent(dev));
	dinfo = device_get_ivars(dev);
	
	mod = enable << dinfo->address;
	if (enable) {
		sc->autopoll_mask |= mod;
	} else {
		mod = ~mod;
		sc->autopoll_mask &= mod;
	}

	ADB_HB_SET_AUTOPOLL_MASK(sc->parent,sc->autopoll_mask);

	return (0);
}

uint8_t 
adb_get_device_type(device_t dev) 
{
	struct adb_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	return (dinfo->default_address);
}

uint8_t 
adb_get_device_handler(device_t dev) 
{
	struct adb_devinfo *dinfo;

	dinfo = device_get_ivars(dev);
	return (dinfo->handler_id);
}

static int 
adb_send_raw_packet_sync(device_t dev, uint8_t to, uint8_t command, 
    uint8_t reg, int len, u_char *data, u_char *reply) 
{
	u_char command_byte = 0;
	struct adb_softc *sc;
	int result = -1;
	int i = 1;

	sc = device_get_softc(dev);
	
	command_byte |= to << 4;
	command_byte |= command << 2;
	command_byte |= reg;

	/* Wait if someone else has a synchronous request pending */
	while (!atomic_cmpset_int(&sc->sync_packet, 0xffff, command_byte))
		tsleep(sc, 0, "ADB sync", hz/10);

	sc->packet_reply = 0;
	sc->sync_packet = command_byte;

	ADB_HB_SEND_RAW_PACKET(sc->parent, command_byte, len, data, 1);

	while (!atomic_fetchadd_int(&sc->packet_reply,0)) {
		/*
		 * Maybe the command got lost? Try resending and polling the 
		 * controller.
		 */
		if (i % 40 == 0)
			ADB_HB_SEND_RAW_PACKET(sc->parent, command_byte, 
			    len, data, 1);

		tsleep(sc, 0, "ADB sync", hz/10);
		i++;
	}

	result = sc->packet_reply - 1;

	if (reply != NULL && result > 0)
		memcpy(reply,sc->syncreg,result);

	/* Clear packet sync */
	sc->packet_reply = 0;

	/*
	 * We can't match a value beyond 8 bits, so set sync_packet to 
	 * 0xffff to avoid collisions.
	 */
	atomic_set_int(&sc->sync_packet, 0xffff); 

	return (result);
}

uint8_t 
adb_set_device_handler(device_t dev, uint8_t newhandler) 
{
	struct adb_softc *sc;
	struct adb_devinfo *dinfo;
	uint16_t newr3;

	dinfo = device_get_ivars(dev);
	sc = device_get_softc(device_get_parent(dev));

	newr3 = dinfo->register3 & 0xff00;
	newr3 |= (uint16_t)(newhandler);

	adb_send_raw_packet_sync(sc->sc_dev,dinfo->address, ADB_COMMAND_LISTEN, 
	    3, sizeof(uint16_t), (u_char *)(&newr3), NULL);
	adb_send_raw_packet_sync(sc->sc_dev,dinfo->address, 
	    ADB_COMMAND_TALK, 3, 0, NULL, NULL);

	return (dinfo->handler_id);
}

size_t 
adb_read_register(device_t dev, u_char reg, void *data) 
{
	struct adb_softc *sc;
	struct adb_devinfo *dinfo;
	size_t result;

	dinfo = device_get_ivars(dev);
	sc = device_get_softc(device_get_parent(dev));

	result = adb_send_raw_packet_sync(sc->sc_dev,dinfo->address,
	           ADB_COMMAND_TALK, reg, 0, NULL, data);

	return (result);
}

size_t 
adb_write_register(device_t dev, u_char reg, size_t len, void *data) 
{
	struct adb_softc *sc;
	struct adb_devinfo *dinfo;
	size_t result;
	
	dinfo = device_get_ivars(dev);
	sc = device_get_softc(device_get_parent(dev));
	
	result = adb_send_raw_packet_sync(sc->sc_dev,dinfo->address,
		   ADB_COMMAND_LISTEN, reg, len, (u_char *)data, NULL);
	
	result = adb_send_raw_packet_sync(sc->sc_dev,dinfo->address,
	           ADB_COMMAND_TALK, reg, 0, NULL, NULL);

	return (result);
}
