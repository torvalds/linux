/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SATI_DESIGN_H_
#define _SATI_DESIGN_H_

/**
@page sati_design_page SATI High Level Design

<b>Authors:</b>
- Nathan Marushak

@section scif_sas_scope_and_audience Scope and Audience

This document provides design information relating to the SCSI to ATA
Translation Implementation (SATI).  Driver developers are the primary
audience for this document.  The reader is expected to have an understanding
of SCSI (Simple Computer Storage Interface), ATA (Advanced Technology
Attachment), and SAT (SCSI-to-ATA Translation).

Please refer to www.t10.org for specifications relating to SCSI and SAT.
Please refer to www.t13.org for specifications relating to ATA.

@section overview Overview

SATI provides environment agnostic functionality for translating SCSI
commands, data, and responses into ATA commands, data, and responses.  As
a result, in some instances the user must fill out callbacks to set data.
This ensures that user isn't forced to have to copy the data an additional
time due to memory access restrictions.

SATI complies with the t10 SAT specification where possible.  In cases where
there are variances the design and implementation will make note.
Additionally, for parameters, pages, functionality, or commands for which
SATI is unable to translate, SATI will return sense data indicating
INVALID FIELD IN CDB.

SATI has two primary entry points from which the user can enter:
- sati_translate_command()
- sati_translate_response() (this method performs data translation).

Additionally, SATI provides a means through which the user can query to
determine the t10 specification revision with which SATI is compliant.  For
more information please refer to:
- sati_get_sat_compliance_version()
- sati_get_sat_compliance_version_revision()

@section sati_definitions Definitions

- scsi_io: The SCSI IO is considered to be the user's SCSI IO request object
(e.g. the windows driver IO request object and SRB).  It is passed back to
the user via callback methods to retrieve required SCSI information (e.g. CDB,
response IU address, etc.).  The SCSI IO is just a cookie and can represent
any value the caller desires, but the user must be able to utilize this value
when it is passed back through callback methods during translation.
- ata_io: The ATA IO is considered to be the user's ATA IO request object.  If
you are utilizing the SCI Framework, then the SCI Framework is the ATA IO.
The ATA IO is just a cookie and can represent any value the caller desires,
but the user must be able to utilize this value when it is passed back
through callback methods during translation.

@section sati_use_cases Use Cases

The SCSI Primary Command (SPC) set is comprised of commands that are valid
for all device types defined in SCSI.  Some of these commands have
sub-commands or parameter data defined in another specification (e.g. SBC, SAT).
These separate sub-commands or parameter data are captured in the SPC use
case diagram for simplicity.

@note
- For simplicify the association between the actor and the use cases
has not been drawn, but is assumed.
- The use cases in green indicate the use case has been implemented in
  source.

@image html Use_Case_Diagram__SATI__SATI_-_SPC.jpg "SCSI Primary Command Translation Use Cases"

The SCSI Block Command (SBC) set is comprised of commands that are valid for
block devices (e.g. disks).

@image html Use_Case_Diagram__SATI__SATI_-_SBC.jpg "SCSI Block Command Translation Use Cases"

The SCSI-to-ATA Translation (SAT) specification defines a few of its own
commands, parameter data, and log pages.  This use case diagram, however, only
captures the SAT specific commands being translated.

@image html Use_Case_Diagram__SATI__SATI_-_SAT_Specific.jpg "SCSI-to-ATA Translation Specific Use Cases"

@section sati_class_hierarchy Class Hierarchy

@image html Class_Diagram__SATI__Class_Diagram.jpg "SATI Class Diagram"

@section sati_sequences Sequence Diagrams

@note These sequence diagrams are currently a little out of date.  An
      update is required.

This sequence diagram simply depicts the high-level translation sequence to
be followed for command translations.

@image html Sequence_Diagram__General_Cmd_Translation_Sequence__General_Cmd_Translation_Sequence.jpg "General Command Translation Sequence"

This sequence diagram simply depicts the high-level translation sequence to
be followed for response translations.

@image html Sequence_Diagram__General_Rsp_Translation_Sequence__General_Rsp_Translation_Sequence.jpg "General Response Translation Sequence"

This sequence diagram simply depicts the high-level translation sequence to
be followed for data translations.  Some SCSI commands such as READ CAPACITY,
INQUIRY, etc. have payload data associated with them.  As a result, it is
necessary for the ATA payload data to be translated to meet the expected SCSI
output.

@image html Sequence_Diagram__General_Data_Translation_Sequence__General_Data_Translation_Sequence.jpg "General Data Translation Sequence"

*/

#endif // _SATI_DESIGN_H_

