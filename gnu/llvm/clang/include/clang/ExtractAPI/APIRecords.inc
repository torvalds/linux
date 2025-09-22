//===- ExtractAPI/APIRecords.inc --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the classes defined from ExtractAPI's APIRecord
///
//===----------------------------------------------------------------------===//

#ifndef ABSTRACT_RECORD
#define ABSTRACT_RECORD(CLASS, BASE) RECORD(CLASS, BASE)
#endif
#ifndef CONCRETE_RECORD
#define CONCRETE_RECORD(CLASS, BASE, KIND) RECORD(CLASS, BASE)
#endif
#ifndef RECORD
#define RECORD(CLASS, BASE)
#endif

CONCRETE_RECORD(NamespaceRecord, APIRecord, RK_Namespace)
CONCRETE_RECORD(GlobalFunctionRecord, APIRecord, RK_GlobalFunction)
CONCRETE_RECORD(GlobalFunctionTemplateRecord, GlobalFunctionRecord,
                RK_GlobalFunctionTemplate)
CONCRETE_RECORD(GlobalFunctionTemplateSpecializationRecord,
                GlobalFunctionRecord, RK_GlobalFunctionTemplateSpecialization)
CONCRETE_RECORD(GlobalVariableRecord, APIRecord, RK_GlobalVariable)
CONCRETE_RECORD(GlobalVariableTemplateRecord, GlobalVariableRecord,
                RK_GlobalVariableTemplate)
CONCRETE_RECORD(GlobalVariableTemplateSpecializationRecord,
                GlobalVariableRecord, RK_GlobalVariableTemplateSpecialization)
CONCRETE_RECORD(GlobalVariableTemplatePartialSpecializationRecord,
                GlobalVariableRecord,
                RK_GlobalVariableTemplatePartialSpecialization)
ABSTRACT_RECORD(TagRecord, APIRecord)
CONCRETE_RECORD(EnumConstantRecord, APIRecord, RK_EnumConstant)
CONCRETE_RECORD(EnumRecord, TagRecord, RK_Enum)
ABSTRACT_RECORD(RecordFieldRecord, APIRecord)
ABSTRACT_RECORD(RecordRecord, TagRecord)
CONCRETE_RECORD(StructFieldRecord, RecordFieldRecord, RK_StructField)
CONCRETE_RECORD(StructRecord, APIRecord, RK_Struct)
CONCRETE_RECORD(UnionFieldRecord, RecordFieldRecord, RK_UnionField)
CONCRETE_RECORD(UnionRecord, APIRecord, RK_Union)
CONCRETE_RECORD(CXXFieldRecord, APIRecord, RK_CXXField)
CONCRETE_RECORD(CXXFieldTemplateRecord, CXXFieldRecord, RK_CXXFieldTemplate)
ABSTRACT_RECORD(CXXMethodRecord, APIRecord)
CONCRETE_RECORD(CXXConstructorRecord, CXXMethodRecord, RK_CXXConstructorMethod)
CONCRETE_RECORD(CXXDestructorRecord, CXXMethodRecord, RK_CXXDestructorMethod)
CONCRETE_RECORD(CXXStaticMethodRecord, CXXMethodRecord, RK_CXXStaticMethod)
CONCRETE_RECORD(CXXInstanceMethodRecord, CXXMethodRecord, RK_CXXInstanceMethod)
CONCRETE_RECORD(CXXMethodTemplateRecord, CXXMethodRecord, RK_CXXMethodTemplate)
CONCRETE_RECORD(CXXMethodTemplateSpecializationRecord, CXXMethodRecord,
                RK_CXXMethodTemplateSpecialization)
ABSTRACT_RECORD(ObjCPropertyRecord, APIRecord)
CONCRETE_RECORD(ObjCInstancePropertyRecord, ObjCPropertyRecord,
                RK_ObjCInstanceProperty)
CONCRETE_RECORD(ObjCClassPropertyRecord, ObjCPropertyRecord,
                RK_ObjCClassProperty)
CONCRETE_RECORD(ObjCInstanceVariableRecord, APIRecord, RK_ObjCIvar)
ABSTRACT_RECORD(ObjCMethodRecord, APIRecord)
CONCRETE_RECORD(ObjCInstanceMethodRecord, ObjCMethodRecord,
                RK_ObjCInstanceMethod)
CONCRETE_RECORD(ObjCClassMethodRecord, ObjCMethodRecord, RK_ObjCClassMethod)
CONCRETE_RECORD(StaticFieldRecord, CXXFieldRecord, RK_StaticField)
ABSTRACT_RECORD(ObjCContainerRecord, APIRecord)
CONCRETE_RECORD(CXXClassRecord, APIRecord, RK_CXXClass)
CONCRETE_RECORD(ClassTemplateRecord, CXXClassRecord, RK_ClassTemplate)
CONCRETE_RECORD(ClassTemplateSpecializationRecord, CXXClassRecord,
                RK_ClassTemplateSpecialization)
CONCRETE_RECORD(ClassTemplatePartialSpecializationRecord, CXXClassRecord,
                RK_ClassTemplatePartialSpecialization)
CONCRETE_RECORD(ConceptRecord, APIRecord, RK_Concept)
CONCRETE_RECORD(ObjCCategoryRecord, ObjCContainerRecord, RK_ObjCCategory)
CONCRETE_RECORD(ObjCInterfaceRecord, ObjCContainerRecord, RK_ObjCInterface)
CONCRETE_RECORD(ObjCProtocolRecord, ObjCContainerRecord, RK_ObjCProtocol)
CONCRETE_RECORD(MacroDefinitionRecord, APIRecord, RK_MacroDefinition)
CONCRETE_RECORD(TypedefRecord, APIRecord, RK_Typedef)

#undef CONCRETE_RECORD
#undef ABSTRACT_RECORD
#undef RECORD

#ifndef RECORD_CONTEXT
#define RECORD_CONTEXT(CLASS, KIND)
#endif

RECORD_CONTEXT(NamespaceRecord, RK_Namespace)
RECORD_CONTEXT(EnumRecord, RK_Enum)
RECORD_CONTEXT(StructRecord, RK_Struct)
RECORD_CONTEXT(UnionRecord, RK_Union)
RECORD_CONTEXT(ObjCCategoryRecord, RK_ObjCCategory)
RECORD_CONTEXT(ObjCInterfaceRecord, RK_ObjCInterface)
RECORD_CONTEXT(ObjCProtocolRecord, RK_ObjCProtocol)
RECORD_CONTEXT(CXXClassRecord, RK_CXXClass)
RECORD_CONTEXT(ClassTemplateRecord, RK_ClassTemplate)
RECORD_CONTEXT(ClassTemplateSpecializationRecord,
               RK_ClassTemplateSpecialization)
RECORD_CONTEXT(ClassTemplatePartialSpecializationRecord,
               RK_ClassTemplatePartialSpecialization)
RECORD_CONTEXT(StructFieldRecord, RK_StructField)
RECORD_CONTEXT(UnionFieldRecord, RK_UnionField)
RECORD_CONTEXT(CXXFieldRecord, RK_CXXField)
RECORD_CONTEXT(StaticFieldRecord, RK_StaticField)
RECORD_CONTEXT(CXXFieldTemplateRecord, RK_CXXFieldTemplate)
RECORD_CONTEXT(GlobalVariableRecord, RK_GlobalVariable)
RECORD_CONTEXT(GlobalVariableTemplateRecord, RK_GlobalVariableTemplate)
RECORD_CONTEXT(GlobalVariableTemplateSpecializationRecord,
               RK_GlobalVariableTemplateSpecialization)
RECORD_CONTEXT(GlobalVariableTemplatePartialSpecializationRecord,
               RK_GlobalVariableTemplatePartialSpecialization)

#undef RECORD_CONTEXT
