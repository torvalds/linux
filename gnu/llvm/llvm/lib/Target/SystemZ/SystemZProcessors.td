//===-- SystemZ.td - SystemZ processors and features ---------*- tblgen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Processor definitions.
//
// For compatibility with other compilers on the platform, each model can
// be identified either by the system name (e.g. z10) or the level of the
// architecture the model supports, as identified by the edition level
// of the z/Architecture Principles of Operation document (e.g. arch8).
//
// The minimum architecture level supported by LLVM is as defined in
// the Eighth Edition of the PoP (i.e. as implemented on z10).
//
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;

def : ProcessorModel<"arch8", NoSchedModel, Arch8SupportedFeatures.List>;
def : ProcessorModel<"z10", NoSchedModel, Arch8SupportedFeatures.List>;

def : ProcessorModel<"arch9", Z196Model, Arch9SupportedFeatures.List>;
def : ProcessorModel<"z196", Z196Model, Arch9SupportedFeatures.List>;

def : ProcessorModel<"arch10", ZEC12Model, Arch10SupportedFeatures.List>;
def : ProcessorModel<"zEC12", ZEC12Model, Arch10SupportedFeatures.List>;

def : ProcessorModel<"arch11", Z13Model, Arch11SupportedFeatures.List>;
def : ProcessorModel<"z13", Z13Model, Arch11SupportedFeatures.List>;

def : ProcessorModel<"arch12", Z14Model, Arch12SupportedFeatures.List>;
def : ProcessorModel<"z14", Z14Model, Arch12SupportedFeatures.List>;

def : ProcessorModel<"arch13", Z15Model, Arch13SupportedFeatures.List>;
def : ProcessorModel<"z15", Z15Model, Arch13SupportedFeatures.List>;

def : ProcessorModel<"arch14", Z16Model, Arch14SupportedFeatures.List>;
def : ProcessorModel<"z16", Z16Model, Arch14SupportedFeatures.List>;

