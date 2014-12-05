LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

XML_H := $(shell cd $(LOCAL_PATH) && make events_xml.h defaults_xml.h)

LOCAL_SRC_FILES := \
	AnnotateListener.cpp \
	Buffer.cpp \
	CCNDriver.cpp \
	CPUFreqDriver.cpp \
	CapturedXML.cpp \
	Child.cpp \
	Command.cpp \
	ConfigurationXML.cpp \
	DiskIODriver.cpp \
	Driver.cpp \
	DriverSource.cpp \
	DynBuf.cpp \
	EventsXML.cpp \
	ExternalSource.cpp \
	FSDriver.cpp \
	Fifo.cpp \
	FtraceDriver.cpp \
	FtraceSource.cpp \
	HwmonDriver.cpp \
	KMod.cpp \
	LocalCapture.cpp \
	Logging.cpp \
	main.cpp \
	MaliVideoDriver.cpp \
	MemInfoDriver.cpp\
	Monitor.cpp \
	NetDriver.cpp \
	OlySocket.cpp \
	OlyUtility.cpp \
	PerfBuffer.cpp \
	PerfDriver.cpp \
	PerfGroup.cpp \
	PerfSource.cpp \
	Proc.cpp \
	Sender.cpp \
	SessionData.cpp \
	SessionXML.cpp \
	Setup.cpp \
	Source.cpp \
	StreamlineSetup.cpp \
	UEvent.cpp \
	UserSpaceSource.cpp \
	libsensors/access.c \
	libsensors/conf-lex.c \
	libsensors/conf-parse.c \
	libsensors/data.c \
	libsensors/error.c \
	libsensors/general.c \
	libsensors/init.c \
	libsensors/sysfs.c \
	mxml/mxml-attr.c \
	mxml/mxml-entity.c \
	mxml/mxml-file.c \
	mxml/mxml-get.c \
	mxml/mxml-index.c \
	mxml/mxml-node.c \
	mxml/mxml-private.c \
	mxml/mxml-search.c \
	mxml/mxml-set.c \
	mxml/mxml-string.c

LOCAL_CFLAGS += -Wall -O3 -fno-exceptions -pthread -DETCDIR=\"/etc\" -Ilibsensors -fPIE
LOCAL_LDFLAGS += -fPIE -pie

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_MODULE := gatord
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
