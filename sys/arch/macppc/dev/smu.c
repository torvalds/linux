/*	$OpenBSD: smu.c,v 1.35 2022/03/13 12:33:01 mpi Exp $	*/

/*
 * Copyright (c) 2005 Mark Kettenis
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/proc.h>
#include <sys/sensors.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>

#include <dev/clock_subr.h>
#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>

#include <macppc/dev/maci2cvar.h>
#include <macppc/dev/thermal.h>
#include <macppc/pci/macobio.h>

int     smu_match(struct device *, void *, void *);
void    smu_attach(struct device *, struct device *, void *);

/* Target and Max. temperature in muK. */
#define TEMP_TRG	38 * 1000000 + 273150000
#define TEMP_MAX	70 * 1000000 + 273150000

#define SMU_MAXFANS	8

struct smu_fan {
	struct thermal_fan fan;
	u_int8_t	reg;
	u_int16_t	min_rpm;
	u_int16_t	max_rpm;
	u_int16_t	unmanaged_rpm;
	u_int16_t	min_pwm;
	u_int16_t	max_pwm;
	u_int16_t	unmanaged_pwm;
	struct ksensor	sensor;
};

#define SMU_MAXSENSORS	4

struct smu_sensor {
	struct thermal_temp therm;
	u_int8_t	reg;
	struct ksensor	sensor;
};

struct smu_softc {
        struct device   sc_dev;

	/* SMU command buffer. */
        bus_dma_tag_t   sc_dmat;
        bus_dmamap_t    sc_cmdmap;
        bus_dma_segment_t sc_cmdseg[1];
        caddr_t         sc_cmd;
	struct rwlock	sc_lock;

	/* Doorbell and mailbox. */
	struct ppc_bus_space sc_mem_bus_space;
	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_gpioh;
	bus_space_handle_t sc_buffh;

	uint8_t		sc_firmware_old;

	struct smu_fan	sc_fans[SMU_MAXFANS];
	int		sc_num_fans;

	struct smu_sensor sc_sensors[SMU_MAXSENSORS];
	int		sc_num_sensors;

	struct ksensordev sc_sensordev;

	u_int16_t	sc_cpu_diode_scale;
	int16_t		sc_cpu_diode_offset;
	u_int16_t	sc_cpu_volt_scale;
	int16_t		sc_cpu_volt_offset;
	u_int16_t	sc_cpu_curr_scale;
	int16_t		sc_cpu_curr_offset;

	u_int16_t	sc_slots_pow_scale;
	int16_t		sc_slots_pow_offset;

	struct i2c_controller sc_i2c_tag;
};

const struct cfattach smu_ca = {
        sizeof(struct smu_softc), smu_match, smu_attach
};

struct cfdriver smu_cd = {
        NULL, "smu", DV_DULL,
};

/* SMU command */
struct smu_cmd {
        u_int8_t        cmd;
        u_int8_t        len;
        u_int8_t        data[254];
};
#define SMU_CMDSZ       sizeof(struct smu_cmd)

/* RTC */
#define SMU_RTC			0x8e
#define SMU_RTC_SET_DATETIME	0x80
#define SMU_RTC_GET_DATETIME	0x81

/* ADC */
#define SMU_ADC			0xd8

/* Fan control */
#define SMU_FAN			0x4a

/* Data partitions */
#define SMU_PARTITION		0x3e
#define SMU_PARTITION_LATEST	0x01
#define SMU_PARTITION_BASE	0x02
#define SMU_PARTITION_UPDATE	0x03

/* I2C */
#define SMU_I2C			0x9a
#define SMU_I2C_SIMPLE		0x00
#define SMU_I2C_NORMAL		0x01
#define SMU_I2C_COMBINED	0x02

/* Power Management */
#define SMU_POWER		0xaa

/* Miscellaneous */
#define SMU_MISC		0xee
#define SMU_MISC_GET_DATA	0x02

int	smu_intr(void *);

int	smu_do_cmd(struct smu_softc *, int);
int	smu_time_read(time_t *);
int	smu_time_write(time_t);
int	smu_get_datablock(struct smu_softc *sc, u_int8_t, u_int8_t *, size_t);
void	smu_firmware_probe(struct smu_softc *, struct smu_fan *);
int	smu_fan_set_rpm(struct smu_softc *, struct smu_fan *, u_int16_t);
int	smu_fan_set_pwm(struct smu_softc *, struct smu_fan *, u_int16_t);
int	smu_fan_read_rpm(struct smu_softc *, struct smu_fan *, u_int16_t *);
int	smu_fan_read_pwm(struct smu_softc *, struct smu_fan *, u_int16_t *,
	    u_int16_t *);
int	smu_fan_refresh(struct smu_softc *, struct smu_fan *);
int	smu_sensor_refresh(struct smu_softc *, struct smu_sensor *, int);
void	smu_refresh_sensors(void *);

int	smu_fan_set_rpm_thermal(struct smu_fan *, int);
int	smu_fan_set_pwm_thermal(struct smu_fan *, int);
int	smu_sensor_refresh_thermal(struct smu_sensor *);

int	smu_i2c_acquire_bus(void *, int);
void	smu_i2c_release_bus(void *, int);
int	smu_i2c_exec(void *, i2c_op_t, i2c_addr_t,
	    const void *, size_t, void *buf, size_t, int);

void	smu_slew_voltage(u_int);

int
smu_match(struct device *parent, void *cf, void *aux)
{
        struct confargs *ca = aux;

        if (strcmp(ca->ca_name, "smu") == 0)
                return (1);
        return (0);
}

/* XXX */
extern struct powerpc_bus_dma_tag pci_bus_dma_tag;

void
smu_attach(struct device *parent, struct device *self, void *aux)
{
        struct smu_softc *sc = (struct smu_softc *)self;
	struct confargs *ca = aux;
	struct i2cbus_attach_args iba;
	struct smu_fan *fan;
	struct smu_sensor *sensor;
	int nseg, node;
	char type[32], loc[32];
	u_int32_t reg, intr, gpio, val;
#ifndef SMALL_KERNEL
	u_int8_t data[12];
#endif

	/* XXX */
	sc->sc_mem_bus_space.bus_base = 0x80000000;
	sc->sc_mem_bus_space.bus_size = 0;
	sc->sc_mem_bus_space.bus_io = 0;
	sc->sc_memt = &sc->sc_mem_bus_space;

	/* Map smu-doorbell gpio. */
	if (OF_getprop(ca->ca_node, "platform-doorbell-ack",
	        &node, sizeof node) <= 0 ||
	    OF_getprop(node, "reg", &reg, sizeof reg) <= 0 ||
	    OF_getprop(node, "interrupts", &intr, sizeof intr) <= 0 ||
	    OF_getprop(OF_parent(node), "reg", &gpio, sizeof gpio) <= 0) {
		printf(": cannot find smu-doorbell gpio\n");
		return;
	}
	if (bus_space_map(sc->sc_memt, gpio + reg, 1, 0, &sc->sc_gpioh)) {
		printf(": cannot map smu-doorbell gpio\n");
		return;
	}

	/* XXX Should get this from OF. */
	if (bus_space_map(sc->sc_memt, 0x860c, 4, 0, &sc->sc_buffh)) {
		printf(": cannot map smu-doorbell buffer\n");
		return;
	}

	/* XXX */
        sc->sc_dmat = &pci_bus_dma_tag;

	/* Allocate and map SMU command buffer.  */
	if (bus_dmamem_alloc(sc->sc_dmat, SMU_CMDSZ, 0, 0,
            sc->sc_cmdseg, 1, &nseg, BUS_DMA_NOWAIT)) {
                printf(": cannot allocate cmd buffer\n");
                return;
        }
        if (bus_dmamem_map(sc->sc_dmat, sc->sc_cmdseg, nseg,
            SMU_CMDSZ, &sc->sc_cmd, BUS_DMA_NOWAIT)) {
                printf(": cannot map cmd buffer\n");
                bus_dmamem_free(sc->sc_dmat, sc->sc_cmdseg, 1);
                return;
        }
        if (bus_dmamap_create(sc->sc_dmat, SMU_CMDSZ, 1, SMU_CMDSZ, 0,
            BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_cmdmap)) {
                printf(": cannot create cmd dmamap\n");
                bus_dmamem_unmap(sc->sc_dmat, sc->sc_cmd, SMU_CMDSZ);
                bus_dmamem_free(sc->sc_dmat, sc->sc_cmdseg, 1);
                return;
        }
        if (bus_dmamap_load(sc->sc_dmat, sc->sc_cmdmap, sc->sc_cmd,
            SMU_CMDSZ, NULL, BUS_DMA_NOWAIT)) {
                printf(": cannot load cmd dmamap\n");
                bus_dmamap_destroy(sc->sc_dmat, sc->sc_cmdmap);
                bus_dmamem_unmap(sc->sc_dmat, sc->sc_cmd, SMU_CMDSZ);
                bus_dmamem_free(sc->sc_dmat, sc->sc_cmdseg, nseg);
                return;
        }

	rw_init(&sc->sc_lock, sc->sc_dev.dv_xname);

	/* Establish smu-doorbell interrupt. */
	mac_intr_establish(parent, intr, IST_EDGE, IPL_BIO,
	    smu_intr, sc, sc->sc_dev.dv_xname);

	/* Initialize global variables that control RTC functionality. */
	time_read = smu_time_read;
	time_write = smu_time_write;

	/* RPM Fans */
	node = OF_getnodebyname(ca->ca_node, "rpm-fans");
	if (node == 0)
		node = OF_getnodebyname(ca->ca_node, "fans");
	for (node = OF_child(node); node; node = OF_peer(node)) {
		if (OF_getprop(node, "reg", &reg, sizeof reg) <= 0 ||
		    OF_getprop(node, "device_type", type, sizeof type) <= 0)
			continue;

		if (strcmp(type, "fan-rpm-control") != 0) {
			printf(": unsupported rpm-fan type: %s\n", type);
			return;
		}

		if (sc->sc_num_fans >= SMU_MAXFANS) {
			printf(": too many fans\n");
			return;
		}

		fan = &sc->sc_fans[sc->sc_num_fans++];
		fan->sensor.type = SENSOR_FANRPM;
		fan->sensor.flags = SENSOR_FINVALID;
		fan->reg = reg;

		if (OF_getprop(node, "min-value", &val, sizeof val) <= 0)
			val = 0;
		fan->min_rpm = val;
		if (OF_getprop(node, "max-value", &val, sizeof val) <= 0)
			val = 0xffff;
		fan->max_rpm = val;
		if (OF_getprop(node, "unmanage-value", &val, sizeof val) > 0)
			fan->unmanaged_rpm = val;
		else if (OF_getprop(node, "safe-value", &val, sizeof val) > 0)
			fan->unmanaged_rpm = val;
		else
			fan->unmanaged_rpm = fan->max_rpm;

		if (OF_getprop(node, "location", loc, sizeof loc) <= 0)
			strlcpy(loc, "Unknown", sizeof loc);
		strlcpy(fan->sensor.desc, loc, sizeof sensor->sensor.desc);

		/* Start running fans at their "unmanaged" speed. */
		smu_fan_set_rpm(sc, fan, fan->unmanaged_rpm);

		/* Register fan at thermal management framework. */
		fan->fan.min_rpm = fan->min_rpm;
		fan->fan.max_rpm = fan->max_rpm;
		fan->fan.default_rpm = fan->unmanaged_rpm;
		strlcpy(fan->fan.name, loc, sizeof fan->fan.name);
		OF_getprop(node, "zone", &fan->fan.zone, sizeof fan->fan.zone);
		fan->fan.set = (int (*)(struct thermal_fan *, int))
		    smu_fan_set_rpm_thermal;
		thermal_fan_register(&fan->fan);
#ifndef SMALL_KERNEL
		sensor_attach(&sc->sc_sensordev, &fan->sensor);
#endif
	}

	/* PWM Fans */
	node = OF_getnodebyname(ca->ca_node, "pwm-fans");
	for (node = OF_child(node); node; node = OF_peer(node)) {
		if (OF_getprop(node, "reg", &reg, sizeof reg) <= 0 ||
		    OF_getprop(node, "device_type", type, sizeof type) <= 0)
			continue;

		if (strcmp(type, "fan-pwm-control") != 0) {
			printf(": unsupported pwm-fan type: %s\n", type);
			return;
		}

		if (sc->sc_num_fans >= SMU_MAXFANS) {
			printf(": too many fans\n");
			return;
		}

		fan = &sc->sc_fans[sc->sc_num_fans++];
		fan->sensor.type = SENSOR_PERCENT;
		fan->sensor.flags = SENSOR_FINVALID;
		fan->reg = reg;

		if (OF_getprop(node, "min-value", &val, sizeof val) <= 0)
			val = 0;
		fan->min_pwm = val;
		if (OF_getprop(node, "max-value", &val, sizeof val) <= 0)
			val = 0xffff;
		fan->max_pwm = val;
		if (OF_getprop(node, "unmanage-value", &val, sizeof val) > 0)
			fan->unmanaged_pwm = val;
		else if (OF_getprop(node, "safe-value", &val, sizeof val) > 0)
			fan->unmanaged_pwm = val;
		else
			fan->unmanaged_pwm = fan->min_pwm;

		if (OF_getprop(node, "location", loc, sizeof loc) <= 0)
			strlcpy(loc, "Unknown", sizeof loc);
		strlcpy(fan->sensor.desc, loc, sizeof sensor->sensor.desc);

		/* Start running fans at their "unmanaged" speed. */
		smu_fan_set_pwm(sc, fan, fan->unmanaged_pwm);

		/* Register fan at thermal management framework. */
		fan->fan.min_rpm = fan->min_pwm;
		fan->fan.max_rpm = fan->max_pwm;
		fan->fan.default_rpm = fan->unmanaged_pwm;
		strlcpy(fan->fan.name, loc, sizeof fan->fan.name);
		OF_getprop(node, "zone", &fan->fan.zone, sizeof fan->fan.zone);
		fan->fan.set = (int (*)(struct thermal_fan *, int))
		    smu_fan_set_pwm_thermal;
		thermal_fan_register(&fan->fan);
#ifndef SMALL_KERNEL
		sensor_attach(&sc->sc_sensordev, &fan->sensor);
#endif
	}

	/*
	 * Bail out if we didn't find any fans.  If we don't set the
	 * fans to a safe speed, but tickle the SMU periodically by
	 * reading sensors, the fans will never spin up and the
	 * machine might overheat.
	 */
	if (sc->sc_num_fans == 0) {
		printf(": no fans\n");
		return;
	}

	/* Probe the smu firmware version */
	smu_firmware_probe(sc, &sc->sc_fans[0]);

#ifndef SMALL_KERNEL
	/* Sensors */
	node = OF_getnodebyname(ca->ca_node, "sensors");
	for (node = OF_child(node); node; node = OF_peer(node)) {
		if (OF_getprop(node, "reg", &val, sizeof val) <= 0 ||
		    OF_getprop(node, "device_type", type, sizeof type) <= 0)
			continue;

		if (sc->sc_num_sensors >= SMU_MAXSENSORS) {
			printf(": too many sensors\n");
			return;
		}

		sensor = &sc->sc_sensors[sc->sc_num_sensors++];
		sensor->sensor.flags = SENSOR_FINVALID;
		sensor->reg = val;

		if (strcmp(type, "current-sensor") == 0) {
			sensor->sensor.type = SENSOR_AMPS;
		} else if (strcmp(type, "temp-sensor") == 0) {
			sensor->sensor.type = SENSOR_TEMP;
		} else if (strcmp(type, "voltage-sensor") == 0) {
			sensor->sensor.type = SENSOR_VOLTS_DC;
		} else if (strcmp(type, "power-sensor") == 0) {
			sensor->sensor.type = SENSOR_WATTS;
		} else {
			sensor->sensor.type = SENSOR_INTEGER;
		}

		if (OF_getprop(node, "location", loc, sizeof loc) <= 0)
			strlcpy(loc, "Unknown", sizeof loc);
		strlcpy(sensor->sensor.desc, loc, sizeof sensor->sensor.desc);

		/* Register temp. sensor at thermal management framework. */
		if (sensor->sensor.type == SENSOR_TEMP) {
			sensor->therm.target_temp = TEMP_TRG;
			sensor->therm.max_temp = TEMP_MAX;
			strlcpy(sensor->therm.name, loc,
			    sizeof sensor->therm.name);
			OF_getprop(node, "zone", &sensor->therm.zone,
			    sizeof sensor->therm.zone);
			sensor->therm.read = (int (*)
			    (struct thermal_temp *))smu_sensor_refresh_thermal;
			thermal_sensor_register(&sensor->therm);
		}

		sensor_attach(&sc->sc_sensordev, &sensor->sensor);
	}

	/* Register sensor device with sysctl */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);

	/* CPU temperature diode calibration */
	smu_get_datablock(sc, 0x18, data, sizeof data);
	sc->sc_cpu_diode_scale = (data[4] << 8) + data[5];
	sc->sc_cpu_diode_offset = (data[6] << 8) + data[7];

	/* CPU power (voltage and current) calibration */
	smu_get_datablock(sc, 0x21, data, sizeof data);
	sc->sc_cpu_volt_scale = (data[4] << 8) + data[5];
	sc->sc_cpu_volt_offset = (data[6] << 8) + data[7];
	sc->sc_cpu_curr_scale = (data[8] << 8) + data[9];
	sc->sc_cpu_curr_offset = (data[10] << 8) + data[11];

	/* Slots power calibration */
	smu_get_datablock(sc, 0x78, data, sizeof data);
	sc->sc_slots_pow_scale = (data[4] << 8) + data[5];
	sc->sc_slots_pow_offset = (data[6] << 8) + data[7];

	sensor_task_register(sc, smu_refresh_sensors, 5);
#endif /* !SMALL_KERNEL */
	printf("\n");

	ppc64_slew_voltage = smu_slew_voltage;

	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = smu_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = smu_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = smu_i2c_exec;

	/*
	 * Early versions of the SMU have the i2c bus node directly
	 * below the "smu" node, while later models have an
	 * intermediate "smu-i2c-control" node.
	 */
	node = OF_getnodebyname(ca->ca_node, "smu-i2c-control");
	if (node)
		node = OF_child(node);
	else
		node = OF_getnodebyname(ca->ca_node, "i2c");

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	iba.iba_bus_scan = maciic_scan;
	iba.iba_bus_scan_arg = &node;
	config_found(&sc->sc_dev, &iba, NULL);
}

int
smu_intr(void *arg)
{
	wakeup(arg);
	return 1;
}

int
smu_do_cmd(struct smu_softc *sc, int msecs)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	u_int8_t gpio, ack = ~cmd->cmd;
	int error;

	/* Write to mailbox.  */
	bus_space_write_4(sc->sc_memt, sc->sc_buffh, 0,
	    sc->sc_cmdmap->dm_segs->ds_addr);

	/* Flush to RAM. */
	asm volatile ("dcbst 0,%0; sync" :: "r"(sc->sc_cmd): "memory");

	/* Ring doorbell.  */
	bus_space_write_1(sc->sc_memt, sc->sc_gpioh, 0, GPIO_DDR_OUTPUT);

	do {
		error = tsleep_nsec(sc, PWAIT, "smu", MSEC_TO_NSEC(msecs));
		if (error)
			return (error);
		gpio = bus_space_read_1(sc->sc_memt, sc->sc_gpioh, 0);
	} while (!(gpio & (GPIO_DATA)));

	/* CPU might have brought back the cache line. */
	asm volatile ("dcbf 0,%0; sync" :: "r"(sc->sc_cmd) : "memory");

	if (cmd->cmd != ack)
		return (EIO);
	return (0);
}

int
smu_time_read(time_t *secs)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	struct clock_ymdhms dt;
	int error;

	rw_enter_write(&sc->sc_lock);

	cmd->cmd = SMU_RTC;
	cmd->len = 1;
	cmd->data[0] = SMU_RTC_GET_DATETIME;
	error = smu_do_cmd(sc, 800);
	if (error) {
		rw_exit_write(&sc->sc_lock);

		*secs = 0;
		return (error);
	}

	dt.dt_year = 2000 + FROMBCD(cmd->data[6]);
	dt.dt_mon = FROMBCD(cmd->data[5]);
	dt.dt_day = FROMBCD(cmd->data[4]);
	dt.dt_hour = FROMBCD(cmd->data[2]);
	dt.dt_min = FROMBCD(cmd->data[1]);
	dt.dt_sec = FROMBCD(cmd->data[0]);

	rw_exit_write(&sc->sc_lock);

	*secs = clock_ymdhms_to_secs(&dt);
	return (0);
}

int
smu_time_write(time_t secs)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	struct clock_ymdhms dt;
	int error;

	clock_secs_to_ymdhms(secs, &dt);

	rw_enter_write(&sc->sc_lock);

	cmd->cmd = SMU_RTC;
	cmd->len = 8;
	cmd->data[0] = SMU_RTC_SET_DATETIME;
	cmd->data[1] = TOBCD(dt.dt_sec);
	cmd->data[2] = TOBCD(dt.dt_min);
	cmd->data[3] = TOBCD(dt.dt_hour);
	cmd->data[4] = TOBCD(dt.dt_wday);
	cmd->data[5] = TOBCD(dt.dt_day);
	cmd->data[6] = TOBCD(dt.dt_mon);
	cmd->data[7] = TOBCD(dt.dt_year - 2000);
	error = smu_do_cmd(sc, 800);

	rw_exit_write(&sc->sc_lock);

	return (error);
}


int
smu_get_datablock(struct smu_softc *sc, u_int8_t id, u_int8_t *buf, size_t len)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	u_int8_t addr[4];
	int error;

	cmd->cmd = SMU_PARTITION;
	cmd->len = 2;
	cmd->data[0] = SMU_PARTITION_LATEST;
	cmd->data[1] = id;
	error = smu_do_cmd(sc, 800);
	if (error)
		return (error);

	addr[0] = 0x00;
	addr[1] = 0x00;
	addr[2] = cmd->data[0];
	addr[3] = cmd->data[1];

	cmd->cmd = SMU_MISC;
	cmd->len = 7;
	cmd->data[0] = SMU_MISC_GET_DATA;
	cmd->data[1] = sizeof(u_int32_t);
	cmd->data[2] = addr[0];
	cmd->data[3] = addr[1];
	cmd->data[4] = addr[2];
	cmd->data[5] = addr[3];
	cmd->data[6] = len;
	error = smu_do_cmd(sc, 800);
	if (error)
		return (error);

	memcpy(buf, cmd->data, len);
	return (0);
}

void
smu_firmware_probe(struct smu_softc *sc, struct smu_fan *fan)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	int error;

	/*
	 * Find out if the smu runs an old or new firmware version
	 * by sending a new firmware command to read the fan speed.
	 * If it fails we assume to have an old firmware version.
	 */
	cmd->cmd = SMU_FAN;
	cmd->len = 2;
	cmd->data[0] = 0x31;
	cmd->data[1] = fan->reg;
	error = smu_do_cmd(sc, 800);
	if (error)
		sc->sc_firmware_old = 1;
}

int
smu_fan_set_rpm(struct smu_softc *sc, struct smu_fan *fan, u_int16_t rpm)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;

	/*
	 * On the PowerMac8,2 this command expects the requested fan
	 * speed at a different location in the command block than on
	 * the PowerMac8,1.  We simply store the value at both
	 * locations.
	 */
	if (sc->sc_firmware_old) {
		cmd->cmd = SMU_FAN;
		cmd->len = 14;
		cmd->data[0] = 0x00;	/* fan-rpm-control */
		cmd->data[1] = 0x01 << fan->reg;
		cmd->data[2] = cmd->data[2 + fan->reg * 2] = (rpm >> 8) & 0xff;
		cmd->data[3] = cmd->data[3 + fan->reg * 2] = (rpm & 0xff);
	} else {
		cmd->cmd = SMU_FAN;
		cmd->len = 4;
		cmd->data[0] = 0x30;
		cmd->data[1] = fan->reg;
		cmd->data[2] = (rpm >> 8) & 0xff;
		cmd->data[3] = rpm & 0xff;
	}
	return smu_do_cmd(sc, 800);
}

int
smu_fan_set_pwm(struct smu_softc *sc, struct smu_fan *fan, u_int16_t pwm)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;

	if (sc->sc_firmware_old) {
		cmd->cmd = SMU_FAN;
		cmd->len = 14;
		cmd->data[0] = 0x10;	/* fan-pwm-control */
		cmd->data[1] = 0x01 << fan->reg;
		cmd->data[2] = cmd->data[2 + fan->reg * 2] = (pwm >> 8) & 0xff;
		cmd->data[3] = cmd->data[3 + fan->reg * 2] = (pwm & 0xff);
	} else {
		cmd->cmd = SMU_FAN;
		cmd->len = 4;
		cmd->data[0] = 0x30;
		cmd->data[1] = fan->reg;
		cmd->data[2] = (pwm >> 8) & 0xff;
		cmd->data[3] = pwm & 0xff;
	}
	return smu_do_cmd(sc, 800);
}

int
smu_fan_read_rpm(struct smu_softc *sc, struct smu_fan *fan, u_int16_t *rpm)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	int error;

	if (sc->sc_firmware_old) {
		cmd->cmd = SMU_FAN;
		cmd->len = 1;
		cmd->data[0] = 0x01;	/* fan-rpm-control */
		error = smu_do_cmd(sc, 800);
		if (error)
			return (error);
		*rpm = (cmd->data[fan->reg * 2 + 1] << 8) |
		        cmd->data[fan->reg * 2 + 2];
	} else {
		cmd->cmd = SMU_FAN;
		cmd->len = 2;
		cmd->data[0] = 0x31;
		cmd->data[1] = fan->reg;
		error = smu_do_cmd(sc, 800);
		if (error)
			return (error);
		*rpm = (cmd->data[0] << 8) | cmd->data[1];
	}

	return (0);
}

int
smu_fan_read_pwm(struct smu_softc *sc, struct smu_fan *fan, u_int16_t *pwm,
    u_int16_t *rpm)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	int error;

	if (sc->sc_firmware_old) {
		/* read PWM value */
		cmd->cmd = SMU_FAN;
		cmd->len = 14;
		cmd->data[0] = 0x12;
		cmd->data[1] = 0x01 << fan->reg;
		error = smu_do_cmd(sc, 800);
		if (error)
			return (error);
		*pwm = cmd->data[fan->reg * 2 + 2];

		/* read RPM value */
		cmd->cmd = SMU_FAN;
		cmd->len = 1;
		cmd->data[0] = 0x11;
		error = smu_do_cmd(sc, 800);
		if (error)
			return (error);
		*rpm = (cmd->data[fan->reg * 2 + 1] << 8) |
		        cmd->data[fan->reg * 2 + 2];
	} else {
		cmd->cmd = SMU_FAN;
		cmd->len = 2;
		cmd->data[0] = 0x31;
		cmd->data[1] = fan->reg;
		error = smu_do_cmd(sc, 800);
		if (error)
			return (error);
		*rpm = (cmd->data[0] << 8) | cmd->data[1];

		/* XXX
		 * We don't know currently if there is a pwm read command
		 * for the new firmware as well.  Therefore lets calculate
		 * the pwm value for now based on the rpm.
		 */
		*pwm = *rpm * 100 / fan->max_rpm;
	}

	return (0);
}

int
smu_fan_refresh(struct smu_softc *sc, struct smu_fan *fan)
{
	int error;
	u_int16_t rpm, pwm;

	if (fan->sensor.type == SENSOR_PERCENT) {
		error = smu_fan_read_pwm(sc, fan, &pwm, &rpm);
		if (error) {
			fan->sensor.flags = SENSOR_FINVALID;
			return (error);
		}
		fan->sensor.value = pwm * 1000;
		fan->sensor.flags = 0;
	} else {
		error = smu_fan_read_rpm(sc, fan, &rpm);
		if (error) {
			fan->sensor.flags = SENSOR_FINVALID;
			return (error);
		}
		fan->sensor.value = rpm;
		fan->sensor.flags = 0;
	}

	return (0);
}

int
smu_sensor_refresh(struct smu_softc *sc, struct smu_sensor *sensor,
    int update_sysctl)
{
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	int64_t value;
	int error;

	cmd->cmd = SMU_ADC;
	cmd->len = 1;
	cmd->data[0] = sensor->reg;
	error = smu_do_cmd(sc, 800);
	if (error) {
		sensor->sensor.flags = SENSOR_FINVALID;
		return (error);
	}
	value = (cmd->data[0] << 8) + cmd->data[1];
	switch (sensor->sensor.type) {
	case SENSOR_TEMP:
		value *= sc->sc_cpu_diode_scale;
		value >>= 3;
		value += ((int64_t)sc->sc_cpu_diode_offset) << 9;
		value <<= 1;

		/* Convert from 16.16 fixed point degC into muK. */
		value *= 15625;
		value /= 1024;
		value += 273150000;
		break;

	case SENSOR_VOLTS_DC:
		value *= sc->sc_cpu_volt_scale;
		value += sc->sc_cpu_volt_offset;
		value <<= 4;

		/* Convert from 16.16 fixed point V into muV. */
		value *= 15625;
		value /= 1024;
		break;

	case SENSOR_AMPS:
		value *= sc->sc_cpu_curr_scale;
		value += sc->sc_cpu_curr_offset;
		value <<= 4;

		/* Convert from 16.16 fixed point A into muA. */
		value *= 15625;
		value /= 1024;
		break;

	case SENSOR_WATTS:
		value *= sc->sc_slots_pow_scale;
		value += sc->sc_slots_pow_offset;
		value <<= 4;

		/* Convert from 16.16 fixed point W into muW. */
		value *= 15625;
		value /= 1024;
		break;

	default:
		break;
	}
	if (update_sysctl) {
		sensor->sensor.value = value;
		sensor->sensor.flags = 0;
	}
	return (value);
}

void
smu_refresh_sensors(void *arg)
{
	struct smu_softc *sc = arg;
	int i;

	rw_enter_write(&sc->sc_lock);
	for (i = 0; i < sc->sc_num_sensors; i++)
		smu_sensor_refresh(sc, &sc->sc_sensors[i], 1);
	for (i = 0; i < sc->sc_num_fans; i++)
		smu_fan_refresh(sc, &sc->sc_fans[i]);
	rw_exit_write(&sc->sc_lock);
}

/*
 * Wrapper functions for the thermal management framework.
 */
int
smu_fan_set_rpm_thermal(struct smu_fan *fan, int rpm)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];

	rw_enter_write(&sc->sc_lock);
	(void)smu_fan_set_rpm(sc, fan, rpm);
	rw_exit_write(&sc->sc_lock);

	return (0);
}

int
smu_fan_set_pwm_thermal(struct smu_fan *fan, int pwm)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];

	rw_enter_write(&sc->sc_lock);
	(void)smu_fan_set_pwm(sc, fan, pwm);
	rw_exit_write(&sc->sc_lock);

	return (0);
}

int
smu_sensor_refresh_thermal(struct smu_sensor *sensor)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];
	int value;

	rw_enter_write(&sc->sc_lock);
	value = smu_sensor_refresh(sc, sensor, 0);
	rw_exit_write(&sc->sc_lock);

	return (value);
}

int
smu_i2c_acquire_bus(void *cookie, int flags)
{
	struct smu_softc *sc = cookie;

	if (flags & I2C_F_POLL)
		return (0);

	return (rw_enter(&sc->sc_lock, RW_WRITE));
}

void
smu_i2c_release_bus(void *cookie, int flags)
{
	struct smu_softc *sc = cookie;

        if (flags & I2C_F_POLL)
                return;

	rw_exit(&sc->sc_lock);
}

int
smu_i2c_exec(void *cookie, i2c_op_t op, i2c_addr_t addr,
    const void *cmdbuf, size_t cmdlen, void *buf, size_t len, int flags)
{
	struct smu_softc *sc = cookie;
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;
	u_int8_t smu_op = SMU_I2C_NORMAL;
	int error, retries = 10;

	if (!I2C_OP_STOP_P(op) || cmdlen > 3 || len > 5)
		return (EINVAL);

	if(cmdlen == 0)
		smu_op = SMU_I2C_SIMPLE;
	else if (I2C_OP_READ_P(op))
		smu_op = SMU_I2C_COMBINED;

	cmd->cmd = SMU_I2C;
	cmd->len = 9 + len;
	cmd->data[0] = 0xb;
	cmd->data[1] = smu_op;
	cmd->data[2] = addr << 1;
	cmd->data[3] = cmdlen;
	memcpy (&cmd->data[4], cmdbuf, cmdlen);
	cmd->data[7] = addr << 1 | I2C_OP_READ_P(op);
	cmd->data[8] = len;
	memcpy(&cmd->data[9], buf, len);

	error = smu_do_cmd(sc, 250);
	if (error)
		return error;

	while (retries--) {
		cmd->cmd = SMU_I2C;
		cmd->len = 1;
		cmd->data[0] = 0;
		memset(&cmd->data[1], 0xff, len);

		error = smu_do_cmd(sc, 250);
		if (error)
			return error;

		if ((cmd->data[0] & 0x80) == 0)
			break;
		if (cmd->data[0] == 0xfd)
			break;

		DELAY(15 * 1000);
	}

	if (cmd->data[0] & 0x80)
		return (EIO);

	if (I2C_OP_READ_P(op))
		memcpy(buf, &cmd->data[1], len);
	return (0);
}

void
smu_slew_voltage(u_int freq_scale)
{
	struct smu_softc *sc = smu_cd.cd_devs[0];
	struct smu_cmd *cmd = (struct smu_cmd *)sc->sc_cmd;

	rw_enter_write(&sc->sc_lock);

	cmd->cmd = SMU_POWER;
	cmd->len = 8;
	memcpy(cmd->data, "VSLEW", 5);
	cmd->data[5] = 0xff;
	cmd->data[6] = 1;
	cmd->data[7] = freq_scale;

	smu_do_cmd(sc, 250);

	rw_exit_write(&sc->sc_lock);
}
