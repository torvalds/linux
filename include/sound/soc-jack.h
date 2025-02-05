/* SPDX-License-Identifier: GPL-2.0
 *
 * soc-jack.h
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __SOC_JACK_H
#define __SOC_JACK_H

/**
 * struct snd_soc_jack_pin - Describes a pin to update based on jack detection
 *
 * @pin:    name of the pin to update
 * @mask:   bits to check for in reported jack status
 * @invert: if non-zero then pin is enabled when status is not reported
 * @list:   internal list entry
 */
struct snd_soc_jack_pin {
	struct list_head list;
	const char *pin;
	int mask;
	bool invert;
};

/**
 * struct snd_soc_jack_zone - Describes voltage zones of jack detection
 *
 * @min_mv: start voltage in mv
 * @max_mv: end voltage in mv
 * @jack_type: type of jack that is expected for this voltage
 * @debounce_time: debounce_time for jack, codec driver should wait for this
 *		duration before reading the adc for voltages
 * @list:   internal list entry
 */
struct snd_soc_jack_zone {
	unsigned int min_mv;
	unsigned int max_mv;
	unsigned int jack_type;
	unsigned int debounce_time;
	struct list_head list;
};

/**
 * struct snd_soc_jack_gpio - Describes a gpio pin for jack detection
 *
 * @idx:          gpio descriptor index within the function of the GPIO
 *                consumer device
 * @gpiod_dev:    GPIO consumer device
 * @name:         gpio name. Also as connection ID for the GPIO consumer
 *                device function name lookup
 * @report:       value to report when jack detected
 * @invert:       report presence in low state
 * @debounce_time: debounce time in ms
 * @wake:	  enable as wake source
 * @jack_status_check: callback function which overrides the detection
 *		       to provide more complex checks (eg, reading an
 *		       ADC).
 */
struct snd_soc_jack_gpio {
	unsigned int idx;
	struct device *gpiod_dev;
	const char *name;
	int report;
	int invert;
	int debounce_time;
	bool wake;

	/* private: */
	struct snd_soc_jack *jack;
	struct delayed_work work;
	struct notifier_block pm_notifier;
	struct gpio_desc *desc;

	void *data;
	/* public: */
	int (*jack_status_check)(void *data);
};

struct snd_soc_jack {
	struct mutex mutex;
	struct snd_jack *jack;
	struct snd_soc_card *card;
	struct list_head pins;
	int status;
	struct blocking_notifier_head notifier;
	struct list_head jack_zones;
};

/* Jack reporting */
void snd_soc_jack_report(struct snd_soc_jack *jack, int status, int mask);
int snd_soc_jack_add_pins(struct snd_soc_jack *jack, int count,
			  struct snd_soc_jack_pin *pins);
void snd_soc_jack_notifier_register(struct snd_soc_jack *jack,
				    struct notifier_block *nb);
void snd_soc_jack_notifier_unregister(struct snd_soc_jack *jack,
				      struct notifier_block *nb);
int snd_soc_jack_add_zones(struct snd_soc_jack *jack, int count,
			   struct snd_soc_jack_zone *zones);
int snd_soc_jack_get_type(struct snd_soc_jack *jack, int micbias_voltage);
#ifdef CONFIG_GPIOLIB
int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
			   struct snd_soc_jack_gpio *gpios);
int snd_soc_jack_add_gpiods(struct device *gpiod_dev,
			    struct snd_soc_jack *jack,
			    int count, struct snd_soc_jack_gpio *gpios);
void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
			     struct snd_soc_jack_gpio *gpios);
#else
static inline int snd_soc_jack_add_gpios(struct snd_soc_jack *jack, int count,
					 struct snd_soc_jack_gpio *gpios)
{
	return 0;
}

static inline int snd_soc_jack_add_gpiods(struct device *gpiod_dev,
					  struct snd_soc_jack *jack,
					  int count,
					  struct snd_soc_jack_gpio *gpios)
{
	return 0;
}

static inline void snd_soc_jack_free_gpios(struct snd_soc_jack *jack, int count,
					   struct snd_soc_jack_gpio *gpios)
{
}
#endif

#endif /* __SOC_JACK_H */
