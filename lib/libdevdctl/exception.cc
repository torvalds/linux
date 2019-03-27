/*-
 * Copyright (c) 2011, 2012, 2013, 2014 Spectra Logic Corporation
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
 * \file exception.cc
 */
#include <sys/cdefs.h>

#include <syslog.h>

#include <cstdio>
#include <cstdarg>
#include <sstream>
#include <string>

#include "exception.h"

__FBSDID("$FreeBSD$");

/*============================ Namespace Control =============================*/
using std::string;
using std::stringstream;
using std::endl;
namespace DevdCtl
{

/*=========================== Class Implementations ==========================*/
/*--------------------------------- Exception --------------------------------*/
void
Exception::FormatLog(const char *fmt, va_list ap)
{
	char buf[256];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	m_log = buf;
}

Exception::Exception(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	FormatLog(fmt, ap);
	va_end(ap);
}

Exception::Exception()
{
}

void
Exception::Log() const
{
	syslog(LOG_ERR, "%s", m_log.c_str());
}

/*------------------------------ ParseException ------------------------------*/
//- ParseException Inline Public Methods ---------------------------------------
ParseException::ParseException(Type type, const std::string &parsedBuffer,
			       size_t offset)
 : Exception(),
   m_type(type),
   m_parsedBuffer(parsedBuffer),
   m_offset(offset)
{
        stringstream logstream;

        logstream << "Parsing ";

        switch (Type()) {
        case INVALID_FORMAT:
                logstream << "invalid format ";
                break;
        case DISCARDED_EVENT_TYPE:
                logstream << "discarded event ";
                break;
        case UNKNOWN_EVENT_TYPE:
                logstream << "unknown event ";
                break;
        default:
                break;
        }
        logstream << "exception on buffer: \'";
        if (GetOffset() == 0) {
                logstream << m_parsedBuffer << '\'' << endl;
        } else {
                string markedBuffer(m_parsedBuffer);

                markedBuffer.insert(GetOffset(), "<HERE-->");
                logstream << markedBuffer << '\'' << endl;
        }

	GetString() = logstream.str();
}

} // namespace DevdCtl
