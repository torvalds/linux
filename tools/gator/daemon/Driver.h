/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRIVER_H
#define DRIVER_H

#include "mxml/mxml.h"

class Counter;

class Driver {
public:
	static Driver *getHead() { return head; }

	virtual ~Driver() {}

	// Returns true if this driver can manage the counter
	virtual bool claimCounter(const Counter &counter) const = 0;
	// Clears and disables all counters
	virtual void resetCounters() = 0;
	// Enables and prepares the counter for capture
	virtual void setupCounter(Counter &counter) = 0;

	// Emits available counters
	virtual int writeCounters(mxml_node_t *root) const = 0;
	// Emits possible dynamically generated events/counters
	virtual void writeEvents(mxml_node_t *) const {}

	Driver *getNext() const { return next; }

protected:
	Driver ();

private:
	static Driver *head;
	Driver *next;

	// Intentionally unimplemented
	Driver(const Driver &);
	Driver &operator=(const Driver &);
};

#endif // DRIVER_H
