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
#ifndef _SCI_OVERVIEW_H_
#define _SCI_OVERVIEW_H_

/**
@mainpage The Intel Storage Controller Interface (SCI)

SCI provides a common interface across intel storage controller hardware.
This includes abstracting differences between Physical PCI functions and
Virtual PCI functions.  The SCI is comprised of four primary components:
-# SCI Base classes
-# SCI Core
-# SCI Framework

It is important to recognize that no component, object, or functionality in
SCI directly allocates memory from the operating system.  It is expected that
the SCI User (OS specific driver code) allocates and frees all memory from
and to the operating system itself.

The C language is utilized to implement SCI.  Although C is not an object
oriented language the SCI driver components, methods, and structures are
modeled and organized following object oriented principles.

The Unified Modeling Language is utilized to present graphical depictions
of the SCI classes and their relationships.

The following figure denotes the meanings of the colors utilized in UML
diagrams throughout this document.
@image latex object_color_key.eps "Object Color Legend" width=8cm

The following figure denotes the meanings for input and output arrows that
are utilized to define parameters for methods defined in this specification.
@image latex arrow_image.eps "Method Parameter Symbol Definition"

@page abbreviations_section Abbreviations

- ATA: Advanced Technology Attachment
- IAF: Identify Address Frame
- SAS: Serial Attached SCSI
- SAT: SCSI to ATA Translation
- SATA: Serial ATA
- SCI: Storage Controller Interface
- SCIC: SCI Core
- SCIF: SCI Framework
- SCU: Storage Controller Unit
- SDS: SCU Driver Standard (i.e. non-virtualization)
- SDV: SCU Driver Virtualized
- SDVP: SDV Physical (PCI function)
- SDVV: SDV Virtual (PCI function)
- SGE: Scatter-Gather Element
- SGL: Scatter-Gather List
- SGPIO: Serial General Purpose Input/Output
- SSC: Spread Spectrum Clocking

@page definitions_section Definitions

- <b>construct</b> - The term "construct" is utilized throughout the
  interface to indicate when an object is being created.  Typically construct
  methods perform pure memory initialization.  No "construct" method ever
  performs memory allocation.  It is incumbent upon the SCI user to provide
  the necessary memory.
- <b>initialize</b> - The term "initialize" is utilized throughout the
  interface to indicate when an object is performing actions on other objects
  or on physical resources in an attempt to allow these resources to become
  operational.
- <b>protected</b> - The term "protected" is utilized to denote a method
  defined in this standard that MUST NOT be invoked directly by operating
  system specific driver code.
- <b>SCI Component</b> - An SCI component is one of: SCI base classes, Core,
  or Framework.
- <b>SCI User</b> - The user callbacks for each SCI Component represent the
  dependencies that the SCI component implementation has upon the operating
  system/environment specific portion of the driver.  It is essentially a
  set of functions or macro definitions that are specific to a particular
  operating system.
- <b>THIN</b> - A term utilized to describe an SCI Component implementation
  that is built to conserve memory.

@page inheritance SCI Inheritance Hierarchy

This section describes the inheritance (i.e. "is-a") relationships between
the various objects in SCI.  Due to various operating environment requirements
the programming language employed for the SCI driver is C.  As a result, one
might be curious how inheritance shall be applied in such an environment.
The SCI driver source shall maintain generalization relationships by ensuring
that child object structures shall contain an instance of their parent's
structure as the very first member of their structure.  As a result, parent
object methods can be invoked with a child structure parameter.  This works
since casting of the child structure to the parent structure inside the parent
method will yield correct access to the parent structure fields.

Consider the following example:
<pre>
typedef struct SCI_OBJECT
{
   U32 object_type;
};

typedef struct SCI_CONTROLLER
{
   U32 state;

} SCI_CONTROLLER_T;

typedef struct SCIC_CONTROLLER
{
   SCI_CONTROLLER_T parent;
   U32 type;

} SCIC_CONTROLLER_T;
</pre>

With the above structure orientation, a user would be allowed to perform
method invocations in a manner similar to the following:
<pre>
SCIC_CONTROLLER_T scic_controller;
scic_controller_initialize((SCIC_CONTROLLER_T*) &scic_controller);

// OR

sci_object_get_association(&scic_controller);
</pre>
@note The actual interface will not require type casting.

The following diagram graphically depicts the inheritance relationships
between the various objects defined in the Storage Controller Interface.
@image latex inheritance.eps "SCI Inheritance Hierarchy" width=16cm

@page sci_classes SCI Classes

This section depicts the common classes and utility functions across the
entire set of SCI Components.  Descriptions about each of the specific
objects will be found in the header file definition in the File Documentation
section.

The following is a list of features that can be found in the SCI base classes:
-# Logging utility methods, constants, and type definitions
-# Memory Descriptor object methods common to the core and framework.
-# Controller object methods common to SCI derived controller objects.
-# Library object methods common to SCI derived library objects.
-# Storage standard (e.g. SAS, SATA) defined constants, structures, etc.
-# Standard types utilized by SCI sub-components.
-# The ability to associate/link SCI objects together or to user objects.

SCI class methods can be overridden by sub-classes in the SCI Core,
SCI Framework, etc.  SCI class methods that MUST NOT be invoked directly
by operating system specific driver code shall be adorned with a
<code>[protected]</code> keyword.  These <code>[protected]</code> API are still
defined as part of the specification in order to demonstrate commonality across
components as well as provide a common description of related methods.  If
these methods are invoked directly by operating system specific code, the
operation of the driver as a whole is not specified or supported.

The following UML diagram graphically depicts the SCI base classes and their
relationships to one another.
@image latex sci_base_classes.eps "SCI Classes" width=4cm

@page associations_section Associations
The sci_object class provides functionality common to all SCI objects.
An important feature provided by this base class is the means by which to
associate one object to another.  An SCI object can be made to have an
association to another SCI object.  Additionally, an SCI object can be
made to have an association to a non-SCI based object.  For example, an SCI
Framework library can have it's association set to an operating system
specific adapter/device driver structre.

Simply put, the association that an object has is a handle (i.e. a void pointer)
to a user structure.  This enables the user of the SCI object to
easily determine it's own associated structure. This association is useful
because the user is now enabled to easily determine their pertinent information
inside of their SCI user callback methods.

Setting an association within an SCI object is generally optional.  The
primary case in which an association is not optional is in the case of IO
request objects.  These associations are necessary in order  to fill
to fill in appropriate information for an IO request (i.e. CDB address, size,
SGL information, etc.) in an efficient manner.

In the case of other objects, the user is free to not create associations.
When the user chooses not to create an association, the user is responsible for
being able to determine their data structures based on the SCI object handles.
Additionally, the user may be forced to invoke additional functionality in
situations where the SCI Framework is employed.  If the user does not
establish proper associations between objects (i.e. SCIC Library to SCIF Library), then the framework is unable to automate interactions.  Users should
strongly consider establishing associations between SCI Framework objects and
OS Driver related structures.

Example Associations:
- The user might set the scif_controller association to their adapter or
controller object.
- The user might set the scif_domain association to their SCSI bus object.

If SCIF is being utilized, then the framework will set the associations
in the core.  In this situation, the user should only set the associations
in the framework objects, unless otherwise directed.
*/

#endif // _SCI_OVERVIEW_H_

