/*-
 * Copyright (c) 2013 Spectra Logic Corporation
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
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 */

/**
 * \file event_factory.cc
 */
#include <sys/cdefs.h>
#include <sys/time.h>

#include <list>
#include <map>
#include <string>

#include "guid.h"
#include "event.h"
#include "event_factory.h"

__FBSDID("$FreeBSD$");

/*================================== Macros ==================================*/
#define NUM_ELEMENTS(x) (sizeof(x) / sizeof(*x))

/*============================ Namespace Control =============================*/
namespace DevdCtl
{

/*=========================== Class Implementations ==========================*/
/*------------------------------- EventFactory -------------------------------*/
//- Event Public Methods -------------------------------------------------------
EventFactory::EventFactory(Event::BuildMethod *defaultBuildMethod)
 : m_defaultBuildMethod(defaultBuildMethod)
{
}

void
EventFactory::UpdateRegistry(Record regEntries[], size_t numEntries)
{
	EventFactory::Record *rec(regEntries);
	EventFactory::Record *lastRec(rec + numEntries - 1);

	for (; rec <= lastRec; rec++) {
		Key key(rec->m_type, rec->m_subsystem);

		if (rec->m_buildMethod == NULL)
			m_registry.erase(key);
		else
			m_registry[key] = rec->m_buildMethod;
	}
}

Event *
EventFactory::Build(Event::Type type, NVPairMap &nvpairs,
		    const std::string eventString) const
{
	Key key(type, nvpairs["system"]);
	Event::BuildMethod *buildMethod(m_defaultBuildMethod);

	Registry::const_iterator foundMethod(m_registry.find(key));
	if (foundMethod != m_registry.end())
		buildMethod = foundMethod->second;
	
	if (buildMethod == NULL) {
		delete &nvpairs;
		return (NULL);
	}

	return (buildMethod(type, nvpairs, eventString));
}

} // namespace DevdCtl
