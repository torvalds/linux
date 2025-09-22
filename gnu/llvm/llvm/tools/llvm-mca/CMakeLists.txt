set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsMCAs          # CustomBehaviour and InstrPostProcess
  AllTargetsDescs
  AllTargetsDisassemblers
  AllTargetsInfos
  MCA
  MC
  MCParser
  Support
  TargetParser
  )

add_llvm_tool(llvm-mca
  llvm-mca.cpp
  CodeRegion.cpp
  CodeRegionGenerator.cpp
  PipelinePrinter.cpp
  Views/BottleneckAnalysis.cpp
  Views/DispatchStatistics.cpp
  Views/InstructionInfoView.cpp
  Views/InstructionView.cpp
  Views/RegisterFileStatistics.cpp
  Views/ResourcePressureView.cpp
  Views/RetireControlUnitStatistics.cpp
  Views/SchedulerStatistics.cpp
  Views/SummaryView.cpp
  Views/TimelineView.cpp
  )

set(LLVM_MCA_SOURCE_DIR ${CURRENT_SOURCE_DIR})
