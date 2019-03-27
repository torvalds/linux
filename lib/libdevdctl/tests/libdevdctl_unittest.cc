/*-
 * Copyright (c) 2016 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Alan Somers         (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

#include <gtest/gtest.h>

#include <list>
#include <map>
#include <string>

#include <devdctl/guid.h>
#include <devdctl/event.h>
#include <devdctl/event_factory.h>

using namespace DevdCtl;
using namespace std;
using namespace testing;

#define REGISTRY_SIZE 2

struct DevNameTestParams
{
	const char* evs;
	bool is_disk;
	const char* devname;
};

class DevNameTest : public TestWithParam<DevNameTestParams>{
protected:
	virtual void SetUp()
	{
		m_factory = new EventFactory();
		m_factory->UpdateRegistry(s_registry, REGISTRY_SIZE);
	}

	virtual void TearDown()
	{
		if (m_ev) delete m_ev;
		if (m_factory) delete m_factory;
	}

	EventFactory *m_factory;
	Event *m_ev;
	static EventFactory::Record s_registry[REGISTRY_SIZE];
};

DevdCtl::EventFactory::Record DevNameTest::s_registry[REGISTRY_SIZE] = {
	{ Event::NOTIFY, "DEVFS", &DevfsEvent::Builder },
	{ Event::NOTIFY, "GEOM", &GeomEvent::Builder }
};

TEST_P(DevNameTest, TestDevname) {
	std::string devname;
	DevNameTestParams param = GetParam();
	
	string evString(param.evs);
	m_ev = Event::CreateEvent(*m_factory, evString);
	m_ev->DevName(devname);
	EXPECT_STREQ(param.devname, devname.c_str());
}

TEST_P(DevNameTest, TestIsDiskDev) {
	DevNameTestParams param = GetParam();

	string evString(param.evs);
	m_ev = Event::CreateEvent(*m_factory, evString);
	EXPECT_EQ(param.is_disk, m_ev->IsDiskDev());
}

/* TODO: clean this up using C++-11 uniform initializers */
INSTANTIATE_TEST_CASE_P(IsDiskDevTestInstantiation, DevNameTest, Values(
	(DevNameTestParams){
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=da6\n",
		.is_disk = true, .devname = "da6"},
	(DevNameTestParams){.is_disk = false, .devname = "cuau0",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=cuau0\n"},
	(DevNameTestParams){.is_disk = true, .devname = "ada6",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=ada6\n"},
	(DevNameTestParams){.is_disk = true, .devname = "da6p1",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=da6p1\n"},
	(DevNameTestParams){.is_disk = true, .devname = "ada6p1",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=ada6p1\n"},
	(DevNameTestParams){.is_disk = true, .devname = "da6s0p1",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=da6s0p1\n"},
	(DevNameTestParams){.is_disk = true, .devname = "ada6s0p1",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=ada6s0p1\n"},
	/* 
	 * Test physical path nodes.  These are currently all set to false since
	 * physical path nodes are implemented with symlinks, and most CAM and
	 * ZFS operations can't use symlinked device nodes
	 */
	/* A SpectraBSD-style physical path node*/
	(DevNameTestParams){.is_disk = false, .devname = "enc@50030480019f53fd/elmtype@array_device/slot@18/da",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=enc@50030480019f53fd/elmtype@array_device/slot@18/da\n"},
	(DevNameTestParams){.is_disk = false, .devname = "enc@50030480019f53fd/elmtype@array_device/slot@18/pass",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=enc@50030480019f53fd/elmtype@array_device/slot@18/pass\n"},
	/* A FreeBSD-style physical path node */
	(DevNameTestParams){.is_disk = true, .devname = "enc@n50030480019f53fd/type@0/slot@18/elmdesc@ArrayDevice18/da6",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=enc@n50030480019f53fd/type@0/slot@18/elmdesc@ArrayDevice18/da6\n"},
	(DevNameTestParams){.is_disk = false, .devname = "enc@n50030480019f53fd/type@0/slot@18/elmdesc@ArrayDevice18/pass6",
		.evs = "!system=DEVFS subsystem=CDEV type=CREATE cdev=enc@n50030480019f53fd/type@0/slot@18/elmdesc@ArrayDevice18/pass6\n"},
	
	/*
	 * Test some GEOM events
	 */
	(DevNameTestParams){.is_disk = true, .devname = "da5",
		.evs = "!system=GEOM subsystem=disk type=GEOM::physpath devname=da5\n"})
);
