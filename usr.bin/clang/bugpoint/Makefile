# $FreeBSD$

PROG_CXX=	bugpoint

SRCDIR=		tools/bugpoint
SRCS+=		BugDriver.cpp
SRCS+=		CrashDebugger.cpp
SRCS+=		ExecutionDriver.cpp
SRCS+=		ExtractFunction.cpp
SRCS+=		FindBugs.cpp
SRCS+=		Miscompilation.cpp
SRCS+=		OptimizerDriver.cpp
SRCS+=		ToolRunner.cpp
SRCS+=		bugpoint.cpp

LIBADD+=	z

.include "../llvm.prog.mk"
