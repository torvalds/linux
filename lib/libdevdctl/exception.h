/*-
 * Copyright (c) 2011, 2012, 2013 Spectra Logic Corporation
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
 * \file zfsd_exception.h
 *
 * Definition of the ZfsdException class hierarchy.  All exceptions
 * explicitly thrown by Zfsd are defined here.
 */
#ifndef	_DEVDCTL_EXCEPTION_H_
#define	_DEVDCTL_EXCEPTION_H_

/*============================ Namespace Control =============================*/
namespace DevdCtl
{

/*============================= Class Definitions ============================*/

/*--------------------------------- Exception --------------------------------*/
/**
 * \brief Class allowing unified reporting/logging of exceptional events.
 */
class Exception
{
public:
	/**
	 * \brief Exception constructor allowing arbitrary string
	 *        data to be reported.
	 *
	 * \param fmt  Printf-like string format specifier.
	 */
	Exception(const char *fmt, ...);

	/**
	 * \brief Augment/Modify a Exception's string data.
	 */
	std::string& GetString();

	/**
	 * \brief Emit exception data to syslog(3).
	 */
	virtual void Log() const;

protected:
	Exception();

	/**
	 * \brief Convert exception string format and arguments provided
	 *        in event constructors into a linear string.
	 */
	void FormatLog(const char *fmt, va_list ap);

	std::string   m_log;
};

inline std::string &
Exception::GetString()
{
	return (m_log);
}

/*------------------------------ ParseException ------------------------------*/
/**
 * Exception thrown when an event string is not converted to an actionable
 * Event object.
 */
class ParseException : public Exception
{
public:
	enum Type
	{
		/** Improperly formatted event string encountered. */
		INVALID_FORMAT,

		/** No handlers for this event type. */
		DISCARDED_EVENT_TYPE,

		/** Unhandled event type. */
		UNKNOWN_EVENT_TYPE
	};

	/**
	 * Constructor
	 *
	 * \param type          The type of this exception.
	 * \param parsedBuffer  The parsing buffer active at the time of
	 *                      the exception.
	 * \param offset        The location in the parse buffer where this
	 *                      exception occurred.
	 */
	ParseException(Type type, const std::string &parsedBuffer,
		       size_t offset = 0);

	/**
	 * Accessor
	 *
	 * \return  The classification for this exception.
	 */
	Type        GetType()   const;

	/**
	 * Accessor
	 *
	 * \return  The offset into the event string where this exception
	 *          occurred.
	 */
	size_t      GetOffset() const;

private:
	/** The type of this exception. */
	Type              m_type;

	/** The parsing buffer that was active at the time of the exception. */
	const std::string m_parsedBuffer;

	/**
	 * The offset into the event string buffer from where this
	 * exception was triggered.
	 */
	size_t            m_offset;
};

//- ParseException Inline Const Public Methods ---------------------------------
inline ParseException::Type
ParseException::GetType() const
{
	return (m_type);
}

inline size_t
ParseException::GetOffset() const
{
	return (m_offset);
}

} // namespace DevdCtl
#endif /* _DEVDCTL_EXCEPTION_H_ */
