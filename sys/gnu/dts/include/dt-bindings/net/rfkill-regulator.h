/*
 * This header provides macros for rfkill-regulator bindings.
 *
 * Copyright (C) 2014 Marek Belisko <marek@goldelico.com>
 *
 * GPLv2 only
 */

#ifndef __DT_BINDINGS_RFKILL_REGULATOR_H__
#define __DT_BINDINGS_RFKILL_REGULATOR_H__


#define RFKILL_TYPE_ALL		(0)
#define RFKILL_TYPE_WLAN	(1)
#define RFKILL_TYPE_BLUETOOTH	(2)
#define RFKILL_TYPE_UWB		(3)
#define RFKILL_TYPE_WIMAX	(4)
#define RFKILL_TYPE_WWAN	(5)
#define RFKILL_TYPE_GPS		(6)
#define RFKILL_TYPE_FM		(7)
#define RFKILL_TYPE_NFC		(8)

#endif /* __DT_BINDINGS_RFKILL_REGULATOR_H__ */
