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
 *
 * $FreeBSD$
 */

/**
 * \file devdctl_event_factory.h
 */

#ifndef _DEVDCTL_EVENT_FACTORY_H_
#define	_DEVDCTL_EVENT_FACTORY_H_

/*============================ Namespace Control =============================*/
namespace DevdCtl
{

/*============================= Class Definitions ============================*/
/*------------------------------- EventFactory -------------------------------*/
/**
 * \brief Container for "event type" => "event object" creation methods.
 */
class EventFactory
{
public:
	/**
	 * Event creation handlers are matched by event type and a
	 * string representing the system emitting the event.
	 */
	typedef std::pair<Event::Type, std::string> Key;

	/** Map type for Factory method lookups. */
	typedef std::map<Key, Event::BuildMethod *> Registry;

	/** Table record of factory methods to add to our registry. */
	struct Record
	{
		Event::Type         m_type;
		const char         *m_subsystem;
		Event::BuildMethod *m_buildMethod;
	};

	const Registry &GetRegistry()				const;
	Event *Build(Event::Type type, NVPairMap &nvpairs,
		     const std::string eventString)		const;

	EventFactory(Event::BuildMethod *defaultBuildMethod = NULL);

	void UpdateRegistry(Record regEntries[], size_t numEntries);


protected:
	/** Registry of event factory methods providing O(log(n)) lookup. */
	Registry	    m_registry;

	Event::BuildMethod *m_defaultBuildMethod;
};

inline const EventFactory::Registry &
EventFactory::GetRegistry() const
{
	return (m_registry);
}

} // namespace DevdCtl
#endif /*_DEVDCTL_EVENT_FACTORY_H_ */
