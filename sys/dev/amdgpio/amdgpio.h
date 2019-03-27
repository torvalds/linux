/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Advanced Micro Devices
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

#ifdef	DEBUG
#define	dprintf(fmt, args...) do {	\
	printf("%s(): ", __func__);	\
	printf(fmt,##args);		\
} while (0)
#else
#define	dprintf(fmt, args...)
#endif

#define	AMD_GPIO_PREFIX			"AMDGPIO"

#define	AMD_GPIO_NUM_PIN_BANK		4
#define	AMD_GPIO_PINS_PER_BANK		64
#define	AMD_GPIO_PINS_MAX		256 /* 4 banks * 64 pins */

/* Number of pins in each bank */
#define	AMD_GPIO_PINS_BANK0		63
#define	AMD_GPIO_PINS_BANK1		64
#define	AMD_GPIO_PINS_BANK2		56
#define	AMD_GPIO_PINS_BANK3		32
#define	AMD_GPIO_PIN_PRESENT		(AMD_GPIO_PINS_BANK0 + \
					AMD_GPIO_PINS_BANK1 + \
					AMD_GPIO_PINS_BANK2 + \
					AMD_GPIO_PINS_BANK3)
#define	AMDGPIO_DEFAULT_CAPS		(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)

/* Register related macros */
#define	AMDGPIO_PIN_REGISTER(pin)	(pin * 4)

#define	WAKE_INT_MASTER_REG		0xfc
#define	EOI_MASK			(1 << 29)
#define	WAKE_INT_STATUS_REG0		0x2f8
#define	WAKE_INT_STATUS_REG1		0x2fc

/* Bit definition of 32 bits of each pin register */
#define	DB_TMR_OUT_OFF			0
#define	DB_TMR_OUT_UNIT_OFF		4
#define	DB_CNTRL_OFF			5
#define	DB_TMR_LARGE_OFF		7
#define	LEVEL_TRIG_OFF			8
#define	ACTIVE_LEVEL_OFF		9
#define	INTERRUPT_ENABLE_OFF		11
#define	INTERRUPT_MASK_OFF		12
#define	WAKE_CNTRL_OFF_S0I3		13
#define	WAKE_CNTRL_OFF_S3		14
#define	WAKE_CNTRL_OFF_S4		15
#define	PIN_STS_OFF			16
#define	DRV_STRENGTH_SEL_OFF		17
#define	PULL_UP_SEL_OFF			19
#define	PULL_UP_ENABLE_OFF		20
#define	PULL_DOWN_ENABLE_OFF		21
#define	OUTPUT_VALUE_OFF		22
#define	OUTPUT_ENABLE_OFF		23
#define	SW_CNTRL_IN_OFF			24
#define	SW_CNTRL_EN_OFF			25
#define	INTERRUPT_STS_OFF		28
#define	WAKE_STS_OFF			29

#define	DB_TMR_OUT_MASK			0xFUL
#define	DB_CNTRL_MASK			0x3UL
#define	ACTIVE_LEVEL_MASK		0x3UL
#define	DRV_STRENGTH_SEL_MASK		0x3UL

#define	DB_TYPE_NO_DEBOUNCE		0x0UL
#define	DB_TYPE_PRESERVE_LOW_GLITCH	0x1UL
#define	DB_TYPE_PRESERVE_HIGH_GLITCH	0x2UL
#define	DB_TYPE_REMOVE_GLITCH		0x3UL

#define	EDGE_TRIGGER			0x0UL
#define	LEVEL_TRIGGER			0x1UL

#define	ACTIVE_HIGH			0x0UL
#define	ACTIVE_LOW			0x1UL
#define	BOTH_EDGE			0x2UL

#define	ENABLE_INTERRUPT		0x1UL
#define	DISABLE_INTERRUPT		0x0UL

#define	ENABLE_INTERRUPT_MASK		0x0UL
#define	DISABLE_INTERRUPT_MASK		0x1UL
#define	CLR_INTR_STAT			0x1UL

#define	BIT(bit)			(1 << bit)
#define	GPIO_PIN_INFO(p, n)		{ .pin_num = (p), .pin_name = (n) }

struct pin_info {
	int pin_num;
	char *pin_name;
};

/* Pins exposed to drivers */
static const struct pin_info kernzp_pins[] = {
	GPIO_PIN_INFO(0, "PIN_0"),
	GPIO_PIN_INFO(1, "PIN_1"),
	GPIO_PIN_INFO(2, "PIN_2"),
	GPIO_PIN_INFO(3, "PIN_3"),
	GPIO_PIN_INFO(4, "PIN_4"),
	GPIO_PIN_INFO(5, "PIN_5"),
	GPIO_PIN_INFO(6, "PIN_6"),
	GPIO_PIN_INFO(7, "PIN_7"),
	GPIO_PIN_INFO(8, "PIN_8"),
	GPIO_PIN_INFO(9, "PIN_9"),
	GPIO_PIN_INFO(10, "PIN_10"),
	GPIO_PIN_INFO(11, "PIN_11"),
	GPIO_PIN_INFO(12, "PIN_12"),
	GPIO_PIN_INFO(13, "PIN_13"),
	GPIO_PIN_INFO(14, "PIN_14"),
	GPIO_PIN_INFO(15, "PIN_15"),
	GPIO_PIN_INFO(16, "PIN_16"),
	GPIO_PIN_INFO(17, "PIN_17"),
	GPIO_PIN_INFO(18, "PIN_18"),
	GPIO_PIN_INFO(19, "PIN_19"),
	GPIO_PIN_INFO(20, "PIN_20"),
	GPIO_PIN_INFO(23, "PIN_23"),
	GPIO_PIN_INFO(24, "PIN_24"),
	GPIO_PIN_INFO(25, "PIN_25"),
	GPIO_PIN_INFO(26, "PIN_26"),
	GPIO_PIN_INFO(39, "PIN_39"),
	GPIO_PIN_INFO(40, "PIN_40"),
	GPIO_PIN_INFO(43, "PIN_43"),
	GPIO_PIN_INFO(46, "PIN_46"),
	GPIO_PIN_INFO(47, "PIN_47"),
	GPIO_PIN_INFO(48, "PIN_48"),
	GPIO_PIN_INFO(49, "PIN_49"),
	GPIO_PIN_INFO(50, "PIN_50"),
	GPIO_PIN_INFO(51, "PIN_51"),
	GPIO_PIN_INFO(52, "PIN_52"),
	GPIO_PIN_INFO(53, "PIN_53"),
	GPIO_PIN_INFO(54, "PIN_54"),
	GPIO_PIN_INFO(55, "PIN_55"),
	GPIO_PIN_INFO(56, "PIN_56"),
	GPIO_PIN_INFO(57, "PIN_57"),
	GPIO_PIN_INFO(58, "PIN_58"),
	GPIO_PIN_INFO(59, "PIN_59"),
	GPIO_PIN_INFO(60, "PIN_60"),
	GPIO_PIN_INFO(61, "PIN_61"),
	GPIO_PIN_INFO(62, "PIN_62"),
	GPIO_PIN_INFO(64, "PIN_64"),
	GPIO_PIN_INFO(65, "PIN_65"),
	GPIO_PIN_INFO(66, "PIN_66"),
	GPIO_PIN_INFO(68, "PIN_68"),
	GPIO_PIN_INFO(69, "PIN_69"),
	GPIO_PIN_INFO(70, "PIN_70"),
	GPIO_PIN_INFO(71, "PIN_71"),
	GPIO_PIN_INFO(72, "PIN_72"),
	GPIO_PIN_INFO(74, "PIN_74"),
	GPIO_PIN_INFO(75, "PIN_75"),
	GPIO_PIN_INFO(76, "PIN_76"),
	GPIO_PIN_INFO(84, "PIN_84"),
	GPIO_PIN_INFO(85, "PIN_85"),
	GPIO_PIN_INFO(86, "PIN_86"),
	GPIO_PIN_INFO(87, "PIN_87"),
	GPIO_PIN_INFO(88, "PIN_88"),
	GPIO_PIN_INFO(89, "PIN_89"),
	GPIO_PIN_INFO(90, "PIN_90"),
	GPIO_PIN_INFO(91, "PIN_91"),
	GPIO_PIN_INFO(92, "PIN_92"),
	GPIO_PIN_INFO(93, "PIN_93"),
	GPIO_PIN_INFO(95, "PIN_95"),
	GPIO_PIN_INFO(96, "PIN_96"),
	GPIO_PIN_INFO(97, "PIN_97"),
	GPIO_PIN_INFO(98, "PIN_98"),
	GPIO_PIN_INFO(99, "PIN_99"),
	GPIO_PIN_INFO(100, "PIN_100"),
	GPIO_PIN_INFO(101, "PIN_101"),
	GPIO_PIN_INFO(102, "PIN_102"),
	GPIO_PIN_INFO(113, "PIN_113"),
	GPIO_PIN_INFO(114, "PIN_114"),
	GPIO_PIN_INFO(115, "PIN_115"),
	GPIO_PIN_INFO(116, "PIN_116"),
	GPIO_PIN_INFO(117, "PIN_117"),
	GPIO_PIN_INFO(118, "PIN_118"),
	GPIO_PIN_INFO(119, "PIN_119"),
	GPIO_PIN_INFO(120, "PIN_120"),
	GPIO_PIN_INFO(121, "PIN_121"),
	GPIO_PIN_INFO(122, "PIN_122"),
	GPIO_PIN_INFO(126, "PIN_126"),
	GPIO_PIN_INFO(129, "PIN_129"),
	GPIO_PIN_INFO(130, "PIN_130"),
	GPIO_PIN_INFO(131, "PIN_131"),
	GPIO_PIN_INFO(132, "PIN_132"),
	GPIO_PIN_INFO(133, "PIN_133"),
	GPIO_PIN_INFO(135, "PIN_135"),
	GPIO_PIN_INFO(136, "PIN_136"),
	GPIO_PIN_INFO(137, "PIN_137"),
	GPIO_PIN_INFO(138, "PIN_138"),
	GPIO_PIN_INFO(139, "PIN_139"),
	GPIO_PIN_INFO(140, "PIN_140"),
	GPIO_PIN_INFO(141, "PIN_141"),
	GPIO_PIN_INFO(142, "PIN_142"),
	GPIO_PIN_INFO(143, "PIN_143"),
	GPIO_PIN_INFO(144, "PIN_144"),
	GPIO_PIN_INFO(145, "PIN_145"),
	GPIO_PIN_INFO(146, "PIN_146"),
	GPIO_PIN_INFO(147, "PIN_147"),
	GPIO_PIN_INFO(148, "PIN_148"),
	GPIO_PIN_INFO(166, "PIN_166"),
	GPIO_PIN_INFO(167, "PIN_167"),
	GPIO_PIN_INFO(168, "PIN_168"),
	GPIO_PIN_INFO(169, "PIN_169"),
	GPIO_PIN_INFO(170, "PIN_170"),
	GPIO_PIN_INFO(171, "PIN_171"),
	GPIO_PIN_INFO(172, "PIN_172"),
	GPIO_PIN_INFO(173, "PIN_173"),
	GPIO_PIN_INFO(174, "PIN_174"),
	GPIO_PIN_INFO(175, "PIN_175"),
	GPIO_PIN_INFO(176, "PIN_176"),
	GPIO_PIN_INFO(177, "PIN_177"),
};

#define	AMD_GPIO_PINS_EXPOSED	nitems(kernzp_pins)

static const unsigned i2c0_pins[] = {145, 146};
static const unsigned i2c1_pins[] = {147, 148};
static const unsigned i2c2_pins[] = {113, 114};
static const unsigned i2c3_pins[] = {19, 20};
static const unsigned i2c4_pins[] = {149, 150};
static const unsigned i2c5_pins[] = {151, 152};

static const unsigned uart0_pins[] = {135, 136, 137, 138, 139};
static const unsigned uart1_pins[] = {140, 141, 142, 143, 144};

struct amd_pingroup {
	const char *name;
	const unsigned *pins;
	unsigned npins;
};

static const struct amd_pingroup kernzp_groups[] = {
	{
		.name = "i2c0",
		.pins = i2c0_pins,
		.npins = 2,
	},
	{
		.name = "i2c1",
		.pins = i2c1_pins,
		.npins = 2,
	},
	{
		.name = "i2c2",
		.pins = i2c2_pins,
		.npins = 2,
	},
	{
		.name = "i2c3",
		.pins = i2c3_pins,
		.npins = 2,
	},
	{
		.name = "i2c4",
		.pins = i2c4_pins,
		.npins = 2,
	},
	{
		.name = "i2c5",
		.pins = i2c5_pins,
		.npins = 2,
	},
	{
		.name = "uart0",
		.pins = uart0_pins,
		.npins = 5,
	},
	{
		.name = "uart1",
		.pins = uart1_pins,
		.npins = 5,
	},
};

/* Macros for driver mutex locking */
#define	AMDGPIO_LOCK_INIT(_sc)	\
	mtx_init(&_sc->sc_mtx, device_get_nameunit((_sc)->sc_dev),	\
		"amdgpio", MTX_SPIN)
#define	AMDGPIO_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	AMDGPIO_LOCK(_sc)		mtx_lock_spin(&(_sc)->sc_mtx)
#define	AMDGPIO_UNLOCK(_sc)		mtx_unlock_spin(&(_sc)->sc_mtx)
#define	AMDGPIO_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define	AMDGPIO_ASSERT_UNLOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)

struct amdgpio_softc {
	ACPI_HANDLE		sc_handle;
	device_t		sc_dev;
	device_t		sc_busdev;
	const char*		sc_bank_prefix;
	int			sc_nbanks;
	int			sc_npins;
	int			sc_ngroups;
	struct mtx		sc_mtx;
	struct resource		*sc_res[AMD_GPIO_NUM_PIN_BANK + 1];
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	struct gpio_pin		sc_gpio_pins[AMD_GPIO_PINS_MAX];
	const struct pin_info	*sc_pin_info;
	const struct amd_pingroup *sc_groups;
};

struct amdgpio_sysctl {
	struct amdgpio_softc	*sc;
	uint32_t		pin;
};
