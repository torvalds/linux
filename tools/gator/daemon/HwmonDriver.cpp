/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "HwmonDriver.h"

#include "libsensors/sensors.h"

#include "Logging.h"

// feature->type to input map
static sensors_subfeature_type getInput(const sensors_feature_type type) {
	switch (type) {
	case SENSORS_FEATURE_IN: return SENSORS_SUBFEATURE_IN_INPUT;
	case SENSORS_FEATURE_FAN: return SENSORS_SUBFEATURE_FAN_INPUT;
	case SENSORS_FEATURE_TEMP: return SENSORS_SUBFEATURE_TEMP_INPUT;
	case SENSORS_FEATURE_POWER: return SENSORS_SUBFEATURE_POWER_INPUT;
	case SENSORS_FEATURE_ENERGY: return SENSORS_SUBFEATURE_ENERGY_INPUT;
	case SENSORS_FEATURE_CURR: return SENSORS_SUBFEATURE_CURR_INPUT;
	case SENSORS_FEATURE_HUMIDITY: return SENSORS_SUBFEATURE_HUMIDITY_INPUT;
	default:
		logg->logError(__FILE__, __LINE__, "Unsupported hwmon feature %i", type);
		handleException();
	}
};

class HwmonCounter : public DriverCounter {
public:
	HwmonCounter(DriverCounter *next, char *const name, const sensors_chip_name *chip, const sensors_feature *feature);
	~HwmonCounter();

	const char *getLabel() const { return label; }
	const char *getTitle() const { return title; }
	bool isDuplicate() const { return duplicate; }
	const char *getDisplay() const { return display; }
	const char *getCounterClass() const { return counter_class; }
	const char *getUnit() const { return unit; }
	int getModifier() const { return modifier; }

	int64_t read();

private:
	void init(const sensors_chip_name *chip, const sensors_feature *feature);

	const sensors_chip_name *chip;
	const sensors_feature *feature;
	char *label;
	const char *title;
	const char *display;
	const char *counter_class;
	const char *unit;
	double previous_value;
	int modifier;
	int monotonic: 1,
		duplicate : 1;

	// Intentionally unimplemented
	HwmonCounter(const HwmonCounter &);
	HwmonCounter &operator=(const HwmonCounter &);
};

HwmonCounter::HwmonCounter(DriverCounter *next, char *const name, const sensors_chip_name *chip, const sensors_feature *feature) : DriverCounter(next, name), chip(chip), feature(feature), duplicate(false) {
	label = sensors_get_label(chip, feature);

	switch (feature->type) {
	case SENSORS_FEATURE_IN:
		title = "Voltage";
		display = "maximum";
		counter_class = "absolute";
		unit = "V";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_FAN:
		title = "Fan";
		display = "average";
		counter_class = "absolute";
		unit = "RPM";
		modifier = 1;
		monotonic = false;
		break;
	case SENSORS_FEATURE_TEMP:
		title = "Temperature";
		display = "maximum";
		counter_class = "absolute";
		unit = "°C";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_POWER:
		title = "Power";
		display = "maximum";
		counter_class = "absolute";
		unit = "W";
		modifier = 1000000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_ENERGY:
		title = "Energy";
		display = "accumulate";
		counter_class = "delta";
		unit = "J";
		modifier = 1000000;
		monotonic = true;
		break;
	case SENSORS_FEATURE_CURR:
		title = "Current";
		display = "maximum";
		counter_class = "absolute";
		unit = "A";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_HUMIDITY:
		title = "Humidity";
		display = "average";
		counter_class = "absolute";
		unit = "%";
		modifier = 1000;
		monotonic = false;
		break;
	default:
		logg->logError(__FILE__, __LINE__, "Unsupported hwmon feature %i", feature->type);
		handleException();
	}

	for (HwmonCounter * counter = static_cast<HwmonCounter *>(next); counter != NULL; counter = static_cast<HwmonCounter *>(counter->getNext())) {
		if (strcmp(label, counter->getLabel()) == 0 && strcmp(title, counter->getTitle()) == 0) {
			duplicate = true;
			counter->duplicate = true;
			break;
		}
	}
}

HwmonCounter::~HwmonCounter() {
	free((void *)label);
}

int64_t HwmonCounter::read() {
	double value;
	double result;
	const sensors_subfeature *subfeature;

	// Keep in sync with the read check in HwmonDriver::readEvents
	subfeature = sensors_get_subfeature(chip, feature, getInput(feature->type));
	if (!subfeature) {
		logg->logError(__FILE__, __LINE__, "No input value for hwmon sensor %s", label);
		handleException();
	}

	if (sensors_get_value(chip, subfeature->number, &value) != 0) {
		logg->logError(__FILE__, __LINE__, "Can't get input value for hwmon sensor %s", label);
		handleException();
	}

	result = (monotonic ? value - previous_value : value);
	previous_value = value;

	return result;
}

HwmonDriver::HwmonDriver() {
}

HwmonDriver::~HwmonDriver() {
	sensors_cleanup();
}

void HwmonDriver::readEvents(mxml_node_t *const) {
	int err = sensors_init(NULL);
	if (err) {
		logg->logMessage("Failed to initialize libsensors! (%d)", err);
		return;
	}
	sensors_sysfs_no_scaling = 1;

	int chip_nr = 0;
	const sensors_chip_name *chip;
	while ((chip = sensors_get_detected_chips(NULL, &chip_nr))) {
		int feature_nr = 0;
		const sensors_feature *feature;
		while ((feature = sensors_get_features(chip, &feature_nr))) {
			// Keep in sync with HwmonCounter::read
			// Can this counter be read?
			double value;
			const sensors_subfeature *const subfeature = sensors_get_subfeature(chip, feature, getInput(feature->type));
			if ((subfeature == NULL) || (sensors_get_value(chip, subfeature->number, &value) != 0)) {
				continue;
			}

			// Get the name of the counter
			int len = sensors_snprintf_chip_name(NULL, 0, chip) + 1;
			char *chip_name = new char[len];
			sensors_snprintf_chip_name(chip_name, len, chip);
			len = snprintf(NULL, 0, "hwmon_%s_%d_%d", chip_name, chip_nr, feature->number) + 1;
			char *const name = new char[len];
			snprintf(name, len, "hwmon_%s_%d_%d", chip_name, chip_nr, feature->number);
			delete [] chip_name;

			setCounters(new HwmonCounter(getCounters(), name, chip, feature));
		}
	}
}

void HwmonDriver::writeEvents(mxml_node_t *root) const {
	root = mxmlNewElement(root, "category");
	mxmlElementSetAttr(root, "name", "hwmon");

	char buf[1024];
	for (HwmonCounter *counter = static_cast<HwmonCounter *>(getCounters()); counter != NULL; counter = static_cast<HwmonCounter *>(counter->getNext())) {
		mxml_node_t *node = mxmlNewElement(root, "event");
		mxmlElementSetAttr(node, "counter", counter->getName());
		mxmlElementSetAttr(node, "title", counter->getTitle());
		if (counter->isDuplicate()) {
			mxmlElementSetAttrf(node, "name", "%s (0x%x)", counter->getLabel(), counter->getKey());
		} else {
			mxmlElementSetAttr(node, "name", counter->getLabel());
		}
		mxmlElementSetAttr(node, "display", counter->getDisplay());
		mxmlElementSetAttr(node, "class", counter->getCounterClass());
		mxmlElementSetAttr(node, "units", counter->getUnit());
		if (counter->getModifier() != 1) {
			mxmlElementSetAttrf(node, "modifier", "%d", counter->getModifier());
		}
		if (strcmp(counter->getDisplay(), "average") == 0 || strcmp(counter->getDisplay(), "maximum") == 0) {
			mxmlElementSetAttr(node, "average_selection", "yes");
		}
		snprintf(buf, sizeof(buf), "libsensors %s sensor %s (%s)", counter->getTitle(), counter->getLabel(), counter->getName());
		mxmlElementSetAttr(node, "description", buf);
	}
}

void HwmonDriver::start() {
	for (DriverCounter *counter = getCounters(); counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->read();
	}
}
