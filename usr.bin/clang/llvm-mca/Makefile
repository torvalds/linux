# $FreeBSD$

PROG_CXX=	llvm-mca

SRCDIR=		tools/llvm-mca
SRCS+=		CodeRegion.cpp
SRCS+=		CodeRegionGenerator.cpp
SRCS+=		PipelinePrinter.cpp
SRCS+=		Views/DispatchStatistics.cpp
SRCS+=		Views/InstructionInfoView.cpp
SRCS+=		Views/RegisterFileStatistics.cpp
SRCS+=		Views/ResourcePressureView.cpp
SRCS+=		Views/RetireControlUnitStatistics.cpp
SRCS+=		Views/SchedulerStatistics.cpp
SRCS+=		Views/SummaryView.cpp
SRCS+=		Views/TimelineView.cpp
SRCS+=		Views/View.cpp
SRCS+=		llvm-mca.cpp

CFLAGS+=	-I${LLVM_SRCS}/${SRCDIR}

LIBADD+=	z

.include "../llvm.prog.mk"
