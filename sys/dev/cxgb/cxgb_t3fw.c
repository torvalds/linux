/*
 * from: FreeBSD: src/sys/tools/fw_stub.awk,v 1.6 2007/03/02 11:42:53 flz
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/systm.h>
#include <cxgb_t3fw.h>
#include <t3b_protocol_sram.h>
#include <t3b_tp_eeprom.h>
#include <t3c_protocol_sram.h>
#include <t3c_tp_eeprom.h>

static int
cxgb_t3fw_modevent(module_t mod, int type, void *unused)
{
	const struct firmware *fp, *parent;
	int error;
	switch (type) {
	case MOD_LOAD:

		fp = firmware_register("cxgb_t3fw", t3fw, 
				       (size_t)t3fw_length,
				       0, NULL);
		if (fp == NULL)
			goto fail_0;
		parent = fp;
		return (0);
	fail_0:
		return (ENXIO);
	case MOD_UNLOAD:
		error = firmware_unregister("cxgb_t3fw");
		return (error);
	}
	return (EINVAL);
}

static moduledata_t cxgb_t3fw_mod = {
        "cxgb_t3fw",
        cxgb_t3fw_modevent,
        0
};
DECLARE_MODULE(cxgb_t3fw, cxgb_t3fw_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(cxgb_t3fw, 1);
MODULE_DEPEND(cxgb_t3fw, firmware, 1, 1, 1);

static int
cxgb_t3b_protocol_sram_modevent(module_t mod, int type, void *unused)
{
	const struct firmware *fp, *parent;
	int error;
	switch (type) {
	case MOD_LOAD:

		fp = firmware_register("cxgb_t3b_protocol_sram", t3b_protocol_sram, 
				       (size_t)t3b_protocol_sram_length,
				       0, NULL);
		if (fp == NULL)
			goto fail_0;
		parent = fp;
		return (0);
	fail_0:
		return (ENXIO);
	case MOD_UNLOAD:
		error = firmware_unregister("cxgb_t3b_protocol_sram");
		return (error);
	}
	return (EINVAL);
}

static moduledata_t cxgb_t3b_protocol_sram_mod = {
        "cxgb_t3b_protocol_sram",
        cxgb_t3b_protocol_sram_modevent,
        0
};
DECLARE_MODULE(cxgb_t3b_protocol_sram, cxgb_t3b_protocol_sram_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(cxgb_t3b_protocol_sram, 1);
MODULE_DEPEND(cxgb_t3b_protocol_sram, firmware, 1, 1, 1);

static int
cxgb_t3b_tp_eeprom_modevent(module_t mod, int type, void *unused)
{
	const struct firmware *fp, *parent;
	int error;
	switch (type) {
	case MOD_LOAD:

		fp = firmware_register("cxgb_t3b_tp_eeprom", t3b_tp_eeprom, 
				       (size_t)t3b_tp_eeprom_length,
				       0, NULL);
		if (fp == NULL)
			goto fail_0;
		parent = fp;
		return (0);
	fail_0:
		return (ENXIO);
	case MOD_UNLOAD:
		error = firmware_unregister("cxgb_t3b_tp_eeprom");
		return (error);
	}
	return (EINVAL);
}

static moduledata_t cxgb_t3b_tp_eeprom_mod = {
        "cxgb_t3b_tp_eeprom",
        cxgb_t3b_tp_eeprom_modevent,
        0
};
DECLARE_MODULE(cxgb_t3b_tp_eeprom, cxgb_t3b_tp_eeprom_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(cxgb_t3b_tp_eeprom, 1);
MODULE_DEPEND(cxgb_t3b_tp_eeprom, firmware, 1, 1, 1);

static int
cxgb_t3c_protocol_sram_modevent(module_t mod, int type, void *unused)
{
	const struct firmware *fp, *parent;
	int error;
	switch (type) {
	case MOD_LOAD:

		fp = firmware_register("cxgb_t3c_protocol_sram", t3c_protocol_sram, 
				       (size_t)t3c_protocol_sram_length,
				       0, NULL);
		if (fp == NULL)
			goto fail_0;
		parent = fp;
		return (0);
	fail_0:
		return (ENXIO);
	case MOD_UNLOAD:
		error = firmware_unregister("cxgb_t3c_protocol_sram");
		return (error);
	}
	return (EINVAL);
}

static moduledata_t cxgb_t3c_protocol_sram_mod = {
        "cxgb_t3c_protocol_sram",
        cxgb_t3c_protocol_sram_modevent,
        0
};
DECLARE_MODULE(cxgb_t3c_protocol_sram, cxgb_t3c_protocol_sram_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(cxgb_t3c_protocol_sram, 1);
MODULE_DEPEND(cxgb_t3c_protocol_sram, firmware, 1, 1, 1);

static int
cxgb_t3c_tp_eeprom_modevent(module_t mod, int type, void *unused)
{
	const struct firmware *fp, *parent;
	int error;
	switch (type) {
	case MOD_LOAD:

		fp = firmware_register("cxgb_t3c_tp_eeprom", t3c_tp_eeprom, 
				       (size_t)t3c_tp_eeprom_length,
				       0, NULL);
		if (fp == NULL)
			goto fail_0;
		parent = fp;
		return (0);
	fail_0:
		return (ENXIO);
	case MOD_UNLOAD:
		error = firmware_unregister("cxgb_t3c_tp_eeprom");
		return (error);
	}
	return (EINVAL);
}

static moduledata_t cxgb_t3c_tp_eeprom_mod = {
        "cxgb_t3c_tp_eeprom",
        cxgb_t3c_tp_eeprom_modevent,
        0
};
DECLARE_MODULE(cxgb_t3c_tp_eeprom, cxgb_t3c_tp_eeprom_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(cxgb_t3c_tp_eeprom, 1);
MODULE_DEPEND(cxgb_t3c_tp_eeprom, firmware, 1, 1, 1);
