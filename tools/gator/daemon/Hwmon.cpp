/**
 * Copyright (C) ARM Limited 2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Hwmon.h"

#include "libsensors/sensors.h"

#include "Buffer.h"
#include "Counter.h"
#include "Logging.h"
#include "SessionData.h"

class HwmonCounter {
public:
	HwmonCounter(HwmonCounter *next, int key, const sensors_chip_name *chip, const sensors_feature *feature);
	~HwmonCounter();

	HwmonCounter *getNext() const { return next; }
	int getKey() const { return key; }
	bool isEnabled() const { return enabled; }
	const char *getName() const { return name; }
	const char *getLabel() const { return label; }
	const char *getTitle() const { return title; }
	bool isDuplicate() const { return duplicate; }
	const char *getDisplay() const { return display; }
	const char *getUnit() const { return unit; }
	int getModifier() const { return modifier; }

	void setEnabled(const bool enabled) {
		this->enabled = enabled;
		// canRead will clear enabled if the counter is not readable
		canRead();
	}

	double read();
	bool canRead();

private:
	void init(const sensors_chip_name *chip, const sensors_feature *feature);

	HwmonCounter *const next;
	const int key;
	int polled : 1,
		readable : 1,
		enabled : 1,
		monotonic: 1,
		duplicate : 1;

	const sensors_chip_name *chip;
	const sensors_feature *feature;

	char *name;
	char *label;
	const char *title;
	const char *display;
	const char *unit;
	int modifier;
	double previous_value;

	sensors_subfeature_type input;
};

HwmonCounter::HwmonCounter(HwmonCounter *next, int key, const sensors_chip_name *chip, const sensors_feature *feature) : next(next), key(key), polled(false), readable(false), enabled(false), duplicate(false), chip(chip), feature(feature) {

	int len = sensors_snprintf_chip_name(NULL, 0, chip) + 1;
	char *chip_name = new char[len];
	sensors_snprintf_chip_name(chip_name, len, chip);

	len = snprintf(NULL, 0, "hwmon_%s_%d", chip_name, feature->number) + 1;
	name = new char[len];
	len = snprintf(name, len, "hwmon_%s_%d", chip_name, feature->number);

	delete [] chip_name;

	label = sensors_get_label(chip, feature);

	switch (feature->type) {
	case SENSORS_FEATURE_IN:
		title = "Voltage";
		input = SENSORS_SUBFEATURE_IN_INPUT;
		display = "average";
		unit = "V";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_FAN:
		title = "Fan";
		input = SENSORS_SUBFEATURE_FAN_INPUT;
		display = "average";
		unit = "RPM";
		modifier = 1;
		monotonic = false;
		break;
	case SENSORS_FEATURE_TEMP:
		title = "Temperature";
		input = SENSORS_SUBFEATURE_TEMP_INPUT;
		display = "maximum";
		unit = "°C";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_POWER:
		title = "Power";
		input = SENSORS_SUBFEATURE_POWER_INPUT;
		display = "average";
		unit = "W";
		modifier = 1000000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_ENERGY:
		title = "Energy";
		input = SENSORS_SUBFEATURE_ENERGY_INPUT;
		display = "accumulate";
		unit = "J";
		modifier = 1000000;
		monotonic = true;
		break;
	case SENSORS_FEATURE_CURR:
		title = "Current";
		input = SENSORS_SUBFEATURE_CURR_INPUT;
		display = "average";
		unit = "A";
		modifier = 1000;
		monotonic = false;
		break;
	case SENSORS_FEATURE_HUMIDITY:
		title = "Humidity";
		input = SENSORS_SUBFEATURE_HUMIDITY_INPUT;
		display = "average";
		unit = "%";
		modifier = 1000;
		monotonic = false;
		break;
	default:
		logg->logError(__FILE__, __LINE__, "Unsupported hwmon feature %i", feature->type);
		handleException();
	}

	for (HwmonCounter * counter = next; counter != NULL; counter = counter->getNext()) {
		if (strcmp(label, counter->getLabel()) == 0 && strcmp(title, counter->getTitle()) == 0) {
			duplicate = true;
			counter->duplicate = true;
			break;
		}
	}
}

HwmonCounter::~HwmonCounter() {
	free((void *)label);
	delete [] name;
}

double HwmonCounter::read() {
	double value;
	double result;
	const sensors_subfeature *subfeature;

	// Keep in sync with canRead
	subfeature = sensors_get_subfeature(chip, feature, input);
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

bool HwmonCounter::canRead() {
	if (!polled) {
		double value;
		const sensors_subfeature *subfeature;
		bool result = true;

		subfeature = sensors_get_subfeature(chip, feature, input);
		if (!subfeature) {
			result = false;
		} else {
			result = sensors_get_value(chip, subfeature->number, &value) == 0;
		}

		polled = true;
		readable = result;
	}

	enabled &= readable;

	return readable;
}

Hwmon::Hwmon() : counters(NULL) {
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
			counters = new HwmonCounter(counters, getEventKey(), chip, feature);
		}
	}
}

Hwmon::~Hwmon() {
	while (counters != NULL) {
		HwmonCounter * counter = counters;
		counters = counter->getNext();
		delete counter;
	}
	sensors_cleanup();
}

HwmonCounter *Hwmon::findCounter(const Counter &counter) const {
	for (HwmonCounter * hwmonCounter = counters; hwmonCounter != NULL; hwmonCounter = hwmonCounter->getNext()) {
		if (hwmonCounter->canRead() && strcmp(hwmonCounter->getName(), counter.getType()) == 0) {
			return hwmonCounter;
		}
	}

	return NULL;
}

bool Hwmon::claimCounter(const Counter &counter) const {
	return findCounter(counter) != NULL;
}

bool Hwmon::countersEnabled() const {
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (counter->isEnabled()) {
			return true;
		}
	}
	return false;
}

void Hwmon::resetCounters() {
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		counter->setEnabled(false);
	}
}

void Hwmon::setupCounter(Counter &counter) {
	HwmonCounter *const hwmonCounter = findCounter(counter);
	if (hwmonCounter == NULL) {
		counter.setEnabled(false);
		return;
	}
	hwmonCounter->setEnabled(true);
	counter.setKey(hwmonCounter->getKey());
}

void Hwmon::writeCounters(mxml_node_t *root) const {
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (!counter->canRead()) {
			continue;
		}
		mxml_node_t *node = mxmlNewElement(root, "counter");
		mxmlElementSetAttr(node, "name", counter->getName());
	}
}

void Hwmon::writeEvents(mxml_node_t *root) const {
	root = mxmlNewElement(root, "category");
	mxmlElementSetAttr(root, "name", "hwmon");

	char buf[1024];
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (!counter->canRead()) {
			continue;
		}
		mxml_node_t *node = mxmlNewElement(root, "event");
		mxmlElementSetAttr(node, "counter", counter->getName());
		mxmlElementSetAttr(node, "title", counter->getTitle());
		if (counter->isDuplicate()) {
			mxmlElementSetAttrf(node, "name", "%s (0x%x)", counter->getLabel(), counter->getKey());
		} else {
			mxmlElementSetAttr(node, "name", counter->getLabel());
		}
		mxmlElementSetAttr(node, "display", counter->getDisplay());
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

void Hwmon::start() {
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->read();
	}
}

void Hwmon::read(Buffer * const buffer) {
	for (HwmonCounter * counter = counters; counter != NULL; counter = counter->getNext()) {
		if (!counter->isEnabled()) {
			continue;
		}
		buffer->event(counter->getKey(), counter->read());
	}
}
