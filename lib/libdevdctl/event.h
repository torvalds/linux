/*-
 * Copyright (c) 2011, 2012, 2013, 2016 Spectra Logic Corporation
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
 * \file devdctl_event.h
 *
 * \brief Class hierarchy used to express events received via
 *        the devdctl API.
 */

#ifndef _DEVDCTL_EVENT_H_
#define	_DEVDCTL_EVENT_H_

/*============================ Namespace Control =============================*/
namespace DevdCtl
{

/*=========================== Forward Declarations ===========================*/
class EventFactory;

/*============================= Class Definitions ============================*/
/*-------------------------------- NVPairMap ---------------------------------*/
/**
 * NVPairMap is a specialization of the standard map STL container.
 */
typedef std::map<std::string, std::string> NVPairMap;

/*----------------------------------- Event ----------------------------------*/
/**
 * \brief Container for the name => value pairs that comprise the content of
 *        a device control event.
 *
 * All name => value data for events can be accessed via the Contains()
 * and Value() methods.  name => value pairs for data not explicitly
 * received as a name => value pair are synthesized during parsing.  For
 * example, ATTACH and DETACH events have "device-name" and "parent"
 * name => value pairs added.
 */
class Event
{
	friend class EventFactory;

public:
	/** Event type */
	enum Type {
		/** Generic event notification. */
		NOTIFY  = '!',

		/** A driver was not found for this device. */
		NOMATCH = '?',

		/** A bus device instance has been added. */
		ATTACH  = '+',

		/** A bus device instance has been removed. */
		DETACH  = '-'
	};

	/**
	 * Factory method type to construct an Event given
	 * the type of event and an NVPairMap populated from
	 * the event string received from devd.
	 */
	typedef Event* (BuildMethod)(Type, NVPairMap &, const std::string &);

	/** Generic Event object factory. */
	static BuildMethod Builder;

	static Event *CreateEvent(const EventFactory &factory,
				  const std::string &eventString);

	/**
	 * Returns the devname, if any, associated with the event
	 *
	 * \param name	Devname, returned by reference
	 * \return	True iff the event contained a devname
	 */
	virtual bool DevName(std::string &name)	const;

	/**
	 * Returns the absolute pathname of the device associated with this
	 * event.
	 *
	 * \param name	Devname, returned by reference
	 * \return	True iff the event contained a devname
	 */
	bool DevPath(std::string &path)		const;

	/**
	 * Returns true iff this event refers to a disk device
	 */
	bool IsDiskDev()			const;

	/** Returns the physical path of the device, if any
	 *
	 * \param path	Physical path, returned by reference
	 * \return	True iff the event contains a device with a physical
	 * 		path
	 */
	bool PhysicalPath(std::string &path)	const;

	/**
	 * Provide a user friendly string representation of an
	 * event type.
	 *
	 * \param type  The type of event to map to a string.
	 *
	 * \return  A user friendly string representing the input type.
	 */
	static const char  *TypeToString(Type type);

	/**
	 * Determine the availability of a name => value pair by name.
	 *
	 * \param name  The key name to search for in this event instance.
	 *
	 * \return  true if the specified key is available in this
	 *          event, otherwise false.
	 */
	bool Contains(const std::string &name)		 const;

	/**
	 * \param key  The name of the key for which to retrieve its
	 *             associated value.
	 *
	 * \return  A const reference to the string representing the
	 *          value associated with key.
	 *
	 * \note  For key's with no registered value, the empty string
	 *        is returned.
	 */
	const std::string &Value(const std::string &key) const;

	/**
	 * Get the type of this event instance.
	 *
	 * \return  The type of this event instance.
	 */
	Type GetType()					 const;

	/**
	 * Get the original DevdCtl event string for this event.
	 *
	 * \return  The DevdCtl event string.
	 */
	const std::string &GetEventString()		 const;

	/**
	 * Convert the event instance into a string suitable for
	 * printing to the console or emitting to syslog.
	 *
	 * \return  A string of formatted event data.
	 */
	std::string ToString()				 const;

	/**
	 * Pretty-print this event instance to cout.
	 */
	void Print()					 const;

	/**
	 * Pretty-print this event instance to syslog.
	 *
	 * \param priority  The logging priority/facility.
	 *                  See syslog(3).
	 */
	void Log(int priority)				 const;

	/**
	 * Create and return a fully independent clone
	 * of this event.
	 */
	virtual Event *DeepCopy()			 const;

	/** Destructor */
	virtual ~Event();

	/**
	 * Interpret and perform any actions necessary to
	 * consume the event.
	 *
	 * \return True if this event should be queued for later reevaluation
	 */
	virtual bool Process()				 const;

	/**
	 * Get the time that the event was created
	 */
	timeval GetTimestamp()				 const;

	/**
	 * Add a timestamp to the event string, if one does not already exist
	 * TODO: make this an instance method that operates on the std::map
	 * instead of the string.  We must fix zfsd's CaseFile serialization
	 * routines first, so that they don't need the raw event string.
	 *
	 * \param[in,out] eventString The devd event string to modify
	 */
	static void TimestampEventString(std::string &eventString);

	/**
	 * Access all parsed key => value pairs.
	 */
	const NVPairMap &GetMap()			 const;

protected:
	/** Table entries used to map a type to a user friendly string. */
	struct EventTypeRecord
	{
		Type         m_type;
		const char  *m_typeName;
	};

	/**
	 * Constructor
	 *
	 * \param type  The type of event to create.
	 */
	Event(Type type, NVPairMap &map, const std::string &eventString);

	/** Deep copy constructor. */
	Event(const Event &src);

	/** Always empty string returned when NVPairMap lookups fail. */
	static const std::string    s_theEmptyString;

	/** Unsorted table of event types. */
	static EventTypeRecord      s_typeTable[];

	/** The type of this event. */
	const Type                  m_type;

	/**
	 * Event attribute storage.
	 *
	 * \note Although stored by reference (since m_nvPairs can
	 *       never be NULL), the NVPairMap referenced by this field
	 *       is dynamically allocated and owned by this event object.
	 *       m_nvPairs must be deleted at event destruction.
	 */
	NVPairMap                  &m_nvPairs;

	/**
	 * The unaltered event string, as received from devd, used to
	 * create this event object.
	 */
	std::string                 m_eventString;

private:
	/**
	 * Ingest event data from the supplied string.
	 *
	 * \param[in] eventString  The string of devd event data to parse.
	 * \param[out] nvpairs     Returns the parsed data
	 */
	static void ParseEventString(Type type, const std::string &eventString,
				     NVPairMap &nvpairs);
};

inline Event::Type
Event::GetType() const
{
	return (m_type);
}

inline const std::string &
Event::GetEventString() const
{
	return (m_eventString);
}

inline const NVPairMap &
Event::GetMap()	const
{
	return (m_nvPairs);
}

/*--------------------------------- EventList --------------------------------*/
/**
 * EventList is a specialization of the standard list STL container.
 */
typedef std::list<Event *> EventList;

/*-------------------------------- DevfsEvent --------------------------------*/
class DevfsEvent : public Event
{
public:
	/** Specialized Event object factory for Devfs events. */
	static BuildMethod Builder;

	virtual Event *DeepCopy()		const;

	/**
	 * Interpret and perform any actions necessary to
	 * consume the event.
	 * \return True if this event should be queued for later reevaluation
	 */
	virtual bool Process()			const;

	bool IsWholeDev()			const;
	virtual bool DevName(std::string &name)	const;

protected:
	/**
	 * Given the device name of a disk, determine if the device
	 * represents the whole device, not just a partition.
	 *
	 * \param devName  Device name of disk device to test.
	 *
	 * \return  True if the device name represents the whole device.
	 *          Otherwise false.
	 */
	static bool IsWholeDev(const std::string &devName);

	/** DeepCopy Constructor. */
	DevfsEvent(const DevfsEvent &src);

	/** Constructor */
	DevfsEvent(Type, NVPairMap &, const std::string &);
};

/*--------------------------------- GeomEvent --------------------------------*/
class GeomEvent : public Event
{
public:
	/** Specialized Event object factory for GEOM events. */
	static BuildMethod Builder;

	virtual Event *DeepCopy()	const;

	virtual bool DevName(std::string &name)	const;

	const std::string &DeviceName()	const;

protected:
	/** Constructor */
	GeomEvent(Type, NVPairMap &, const std::string &);

	/** Deep copy constructor. */
	GeomEvent(const GeomEvent &src);

	std::string m_devname;
};

/*--------------------------------- ZfsEvent ---------------------------------*/
class ZfsEvent : public Event
{
public:
	/** Specialized Event object factory for ZFS events. */
	static BuildMethod Builder;

	virtual Event *DeepCopy()	const;

	virtual bool DevName(std::string &name)	const;

	const std::string &PoolName()	const;
	Guid		   PoolGUID()	const;
	Guid		   VdevGUID()	const;

protected:
	/** Constructor */
	ZfsEvent(Type, NVPairMap &, const std::string &);

	/** Deep copy constructor. */
	ZfsEvent(const ZfsEvent &src);

	Guid	m_poolGUID;
	Guid	m_vdevGUID;
};

//- ZfsEvent Inline Public Methods --------------------------------------------
inline const std::string&
ZfsEvent::PoolName() const
{
	/* The pool name is reported as the subsystem of ZFS events. */
	return (Value("subsystem"));
}

inline Guid
ZfsEvent::PoolGUID() const
{
	return (m_poolGUID);
}

inline Guid
ZfsEvent::VdevGUID() const
{
	return (m_vdevGUID);
}

} // namespace DevdCtl
#endif /*_DEVDCTL_EVENT_H_ */
