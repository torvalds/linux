add_lldb_library(lldbDataFormatters NO_PLUGIN_DEPENDENCIES
  CXXFunctionPointer.cpp
  DataVisualization.cpp
  DumpValueObjectOptions.cpp
  FormatCache.cpp
  FormatClasses.cpp
  FormatManager.cpp
  FormattersHelpers.cpp
  LanguageCategory.cpp
  StringPrinter.cpp
  TypeCategory.cpp
  TypeCategoryMap.cpp
  TypeFormat.cpp
  TypeSummary.cpp
  TypeSynthetic.cpp
  ValueObjectPrinter.cpp
  VectorType.cpp

  LINK_LIBS
    lldbCore
    lldbInterpreter
    lldbSymbol
    lldbTarget
    lldbUtility
  
  LINK_COMPONENTS
    Support
  )
