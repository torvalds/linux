/*-
 * Copyright 2016 Michal Meloun <mmel@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_EXTRES_REGULATOR_H_
#define _DEV_EXTRES_REGULATOR_H_
#include "opt_platform.h"

#include <sys/kobj.h>
#include <sys/sysctl.h>
#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#endif
#include "regnode_if.h"

SYSCTL_DECL(_hw_regulator);

#define REGULATOR_FLAGS_STATIC		0x00000001  /* Static strings */
#define REGULATOR_FLAGS_NOT_DISABLE	0x00000002  /* Cannot be disabled */

#define REGULATOR_STATUS_ENABLED	0x00000001
#define REGULATOR_STATUS_OVERCURRENT	0x00000002

typedef struct regulator *regulator_t;

/* Standard regulator parameters. */
struct regnode_std_param {
	int 			min_uvolt;	/* In uV */
	int 			max_uvolt;	/* In uV */
	int 			min_uamp;	/* In uA */
	int 			max_uamp;	/* In uA */
	int 			ramp_delay;	/* In uV/usec */
	int 			enable_delay;	/* In usec */
	bool 			boot_on;	/* Is enabled on boot */
	bool 			always_on;	/* Must be enabled */
	int			enable_active_high;
};

/* Initialization parameters. */
struct regnode_init_def {
	char			*name;		/* Regulator name */
	char			*parent_name;	/* Name of parent regulator */
	struct regnode_std_param std_param;	/* Standard parameters */
	intptr_t		id;		/* Regulator ID */
	int			flags;		/* Flags */
#ifdef FDT
	 phandle_t 		ofw_node;	/* OFW node of regulator */
#endif
};

struct regulator_range {
	int		min_uvolt;
	int		step_uvolt;
	uint8_t		min_sel;
	uint8_t		max_sel;
};

#define	REG_RANGE_INIT(_min_sel, _max_sel, _min_uvolt, _step_uvolt) {	\
	.min_sel	= _min_sel,					\
	.max_sel	= _max_sel,					\
	.min_uvolt	= _min_uvolt,					\
	.step_uvolt	= _step_uvolt,					\
}

/*
 * Shorthands for constructing method tables.
 */
#define	REGNODEMETHOD		KOBJMETHOD
#define	REGNODEMETHOD_END	KOBJMETHOD_END
#define regnode_method_t	kobj_method_t
#define regnode_class_t		kobj_class_t
DECLARE_CLASS(regnode_class);

/* Providers interface. */
struct regnode *regnode_create(device_t pdev, regnode_class_t regnode_class,
    struct regnode_init_def *def);
struct regnode *regnode_register(struct regnode *regnode);
const char *regnode_get_name(struct regnode *regnode);
const char *regnode_get_parent_name(struct regnode *regnode);
struct regnode *regnode_get_parent(struct regnode *regnode);
int regnode_get_flags(struct regnode *regnode);
void *regnode_get_softc(struct regnode *regnode);
device_t regnode_get_device(struct regnode *regnode);
struct regnode_std_param *regnode_get_stdparam(struct regnode *regnode);
void regnode_topo_unlock(void);
void regnode_topo_xlock(void);
void regnode_topo_slock(void);

int regnode_enable(struct regnode *regnode);
int regnode_disable(struct regnode *regnode);
int regnode_stop(struct regnode *regnode, int depth);
int regnode_status(struct regnode *regnode, int *status);
int regnode_get_voltage(struct regnode *regnode, int *uvolt);
int regnode_set_voltage(struct regnode *regnode, int min_uvolt, int max_uvolt);
#ifdef FDT
phandle_t regnode_get_ofw_node(struct regnode *regnode);
#endif

/* Consumers interface. */
int regulator_get_by_name(device_t cdev, const char *name,
     regulator_t *regulator);
int regulator_get_by_id(device_t cdev, device_t pdev, intptr_t id,
    regulator_t *regulator);
int regulator_release(regulator_t regulator);
const char *regulator_get_name(regulator_t regulator);
int regulator_enable(regulator_t reg);
int regulator_disable(regulator_t reg);
int regulator_stop(regulator_t reg);
int regulator_status(regulator_t reg, int *status);
int regulator_get_voltage(regulator_t reg, int *uvolt);
int regulator_set_voltage(regulator_t reg, int min_uvolt, int max_uvolt);

#ifdef FDT
int regulator_get_by_ofw_property(device_t dev, phandle_t node, char *name,
    regulator_t *reg);
int regulator_parse_ofw_stdparam(device_t dev, phandle_t node,
    struct regnode_init_def *def);
#endif

/* Utility functions */
int regulator_range_volt_to_sel8(struct regulator_range *ranges, int nranges,
    int min_uvolt, int max_uvolt, uint8_t *out_sel);
int regulator_range_sel8_to_volt(struct regulator_range *ranges, int nranges,
   uint8_t sel, int *volt);

#endif /* _DEV_EXTRES_REGULATOR_H_ */
