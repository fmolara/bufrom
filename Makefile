CXX ?= g++
PKG_CONFIG ?= pkg-config
WREPORT_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libwreport)
WREPORT_LIBS ?= $(shell $(PKG_CONFIG) --libs libwreport)
CPPFLAGS += -Iinclude $(WREPORT_CFLAGS)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -MMD -MP
LDLIBS += $(WREPORT_LIBS)

CORE_SOURCES = src/bufr.cpp src/observations.cpp
APP_SOURCES = $(CORE_SOURCES) src/render.cpp src/main.cpp
APP_OBJECTS = $(APP_SOURCES:src/%.cpp=build/%.o)
TEST_OBJECTS = $(CORE_SOURCES:src/%.cpp=build/%.o) build/native_tests.o

.PHONY: all check clean
all: build/diario-meteo

build/diario-meteo: $(APP_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/native-tests: $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

build/native_tests.o: tests/native_tests.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

check: build/diario-meteo build/native-tests
	./build/native-tests tests/data
	./tests/smoke.sh

clean:
	rm -rf build

-include $(APP_OBJECTS:.o=.d) build/native_tests.d

