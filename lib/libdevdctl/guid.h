/*-
 * Copyright (c) 2012, 2013 Spectra Logic Corporation
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

/**
 * \file devdctl_guid.h
 *
 * Definition of the Guid class.
 */
#ifndef	_DEVDCTL_GUID_H_
#define	_DEVDCTL_GUID_H_

/*============================ Namespace Control =============================*/
namespace DevdCtl
{

/*============================= Class Definitions ============================*/
/*----------------------------------- Guid -----------------------------------*/
/**
 * \brief Object that represents guids.
 *
 * It can generally be manipulated as a uint64_t, but with a special
 * value INVALID_GUID that does not equal any valid guid.
 *
 * As of this writing, this class is only used to represent ZFS
 * guids in events and spa_generate_guid() in spa_misc.c explicitly
 * refuses to return a guid of 0.  So this class uses 0 as the value
 * for INVALID_GUID.  In the future, if 0 is allowed to be a valid
 * guid, the implementation of this class must change.
 */
class Guid
{
public:
	/* Constructors */
	/* Default constructor: an Invalid guid */
	Guid();
	/* Construct a guid from a provided integer */
	Guid(uint64_t guid);
	/* Construct a guid from a string in base 8, 10, or 16 */
	Guid(const std::string &guid);
	static Guid InvalidGuid();

	/* Assignment */
	Guid& operator=(const Guid& rhs);

	/* Test the validity of this guid. */
	bool IsValid()			 const;

	/* Comparison to other Guid operators */
	bool operator==(const Guid& rhs) const;
	bool operator!=(const Guid& rhs) const;

	/* Integer conversion operators */
	operator uint64_t()		 const;
	operator bool()			 const;

protected:
	static const uint64_t INVALID_GUID = 0;

	/* The integer value of the GUID. */
	uint64_t  m_GUID;
};

//- Guid Inline Public Methods ------------------------------------------------
inline
Guid::Guid()
  : m_GUID(INVALID_GUID)
{
}

inline
Guid::Guid(uint64_t guid)
  : m_GUID(guid)
{
}

inline Guid
Guid::InvalidGuid()
{
	return (Guid(INVALID_GUID));
}

inline Guid&
Guid::operator=(const Guid &rhs)
{
	m_GUID = rhs.m_GUID;
	return (*this);
}

inline bool
Guid::IsValid() const
{
	return (m_GUID != INVALID_GUID);
}

inline bool
Guid::operator==(const Guid& rhs) const
{
	return (m_GUID == rhs.m_GUID);
}

inline bool
Guid::operator!=(const Guid& rhs) const
{
	return (m_GUID != rhs.m_GUID);
}

inline
Guid::operator uint64_t() const
{
	return (m_GUID);
}

inline
Guid::operator bool() const
{
	return (m_GUID != INVALID_GUID);
}

/** Convert the GUID into its string representation */
std::ostream& operator<< (std::ostream& out, Guid g);

} // namespace DevdCtl
#endif /* _DEVDCTL_GUID_H_ */
