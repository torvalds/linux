macro(add_lv_impl_folder group)
  list(APPEND LV_IMPL_SOURCES ${ARGN})
  source_group(${group} FILES ${ARGN})
endmacro()

add_lv_impl_folder(Core
  Core/LVCompare.cpp
  Core/LVElement.cpp
  Core/LVLine.cpp
  Core/LVLocation.cpp
  Core/LVObject.cpp
  Core/LVOptions.cpp
  Core/LVRange.cpp
  Core/LVReader.cpp
  Core/LVScope.cpp
  Core/LVSort.cpp
  Core/LVSupport.cpp
  Core/LVSymbol.cpp
  Core/LVType.cpp
  )

add_lv_impl_folder(Readers
  LVReaderHandler.cpp
  Readers/LVBinaryReader.cpp
  Readers/LVCodeViewReader.cpp
  Readers/LVCodeViewVisitor.cpp
  Readers/LVDWARFReader.cpp
  )

list(APPEND LIBLV_ADDITIONAL_HEADER_DIRS
  "${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/LogicalView"
  "${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/LogicalView/Core"
  "${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/LogicalView/Readers"
  )

add_llvm_component_library(LLVMDebugInfoLogicalView
  ${LV_IMPL_SOURCES}

  ADDITIONAL_HEADER_DIRS
  ${LIBLV_ADDITIONAL_HEADER_DIRS}

  LINK_COMPONENTS
  BinaryFormat
  Demangle
  Object
  MC
  Support
  TargetParser
  DebugInfoDWARF
  DebugInfoCodeView
  DebugInfoPDB
  )
