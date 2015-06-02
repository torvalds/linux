# -g produces debugging information
# -O3 maximum optimization
# -O0 no optimization, used for debugging
# -Wall enables most warnings
# -Werror treats warnings as errors
# -std=c++0x is the planned new c++ standard
# -std=c++98 is the 1998 c++ standard
CPPFLAGS += -O3 -Wall -fno-exceptions -pthread -MMD -DETCDIR=\"/etc\" -Ilibsensors
CXXFLAGS += -fno-rtti -Wextra -Wshadow # -Weffc++
ifeq ($(WERROR),1)
	CPPFLAGS += -Werror
endif
# -s strips the binary of debug info
LDFLAGS += -s
LDLIBS += -lrt -lm -pthread
TARGET = gatord
C_SRC = $(wildcard mxml/*.c) $(wildcard libsensors/*.c)
CXX_SRC = $(wildcard *.cpp)

all: $(TARGET)

events.xml: events_header.xml $(wildcard events-*.xml) events_footer.xml
	cat $^ > $@

include $(wildcard *.d)
include $(wildcard mxml/*.d)

EventsXML.cpp: events_xml.h
ConfigurationXML.cpp: defaults_xml.h

# Don't regenerate conf-lex.c or conf-parse.c
libsensors/conf-lex.c: ;
libsensors/conf-parse.c: ;

%_xml.h: %.xml escape
	./escape $< > $@

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

SrcMd5.cpp: $(wildcard *.cpp *.h mxml/*.c mxml/*.h libsensors/*.c libsensors/*.h)
	echo 'extern const char *const gSrcMd5 = "'`ls $^ | grep -Ev '^(.*_xml\.h|$@)$$' | LC_ALL=C sort | xargs cat | md5sum | cut -b 1-32`'";' > $@

$(TARGET): $(CXX_SRC:%.cpp=%.o) $(C_SRC:%.c=%.o) SrcMd5.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

# Intentionally ignore CC as a native binary is required
escape: escape.c
	gcc $^ -o $@

clean:
	rm -f *.d *.o mxml/*.d mxml/*.o libsensors/*.d libsensors/*.o $(TARGET) escape events.xml events_xml.h defaults_xml.h SrcMd5.cpp
