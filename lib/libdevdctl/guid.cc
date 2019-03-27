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
 * \file guid.cc
 *
 * Implementation of the Guid class.
 */
#include <sys/cdefs.h>

#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>

#include <iostream>
#include <string>

#include "guid.h"

__FBSDID("$FreeBSD$");
/*============================ Namespace Control =============================*/
using std::string;
namespace DevdCtl
{

/*=========================== Class Implementations ==========================*/
/*----------------------------------- Guid -----------------------------------*/
Guid::Guid(const string &guidString)
{
	if (guidString.empty()) {
		m_GUID = INVALID_GUID;
	} else {
		/*
		 * strtoumax() returns zero on conversion failure
		 * which nicely matches our choice for INVALID_GUID.
		 */
		m_GUID = (uint64_t)strtoumax(guidString.c_str(), NULL, 0);
	}
}

std::ostream&
operator<< (std::ostream& out, Guid g)
{
	if (g.IsValid())
		out << (uint64_t)g;
	else
		out << "None";
	return (out);
}

} // namespace DevdCtl
