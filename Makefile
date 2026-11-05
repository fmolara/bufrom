CXX ?= g++
PKG_CONFIG ?= pkg-config
WREPORT_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libwreport)
WREPORT_LIBS ?= $(shell $(PKG_CONFIG) --libs libwreport)
CPPFLAGS += -Iinclude $(WREPORT_CFLAGS)
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -MMD -MP
LDLIBS += $(WREPORT_LIBS) $(shell $(PKG_CONFIG) --libs sqlite3 openssl)

CORE_SOURCES = src/bufr.cpp src/observations.cpp src/meteohub.cpp src/archive.cpp
APP_SOURCES = $(CORE_SOURCES) src/render.cpp src/main.cpp
APP_OBJECTS = $(APP_SOURCES:src/%.cpp=build/%.o)
TEST_OBJECTS = $(CORE_SOURCES:src/%.cpp=build/%.o) build/native_tests.o

.PHONY: all check clean
all: build/bufrom

build/bufrom: $(APP_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/native-tests: $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

build/native_tests.o: tests/native_tests.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

build/archive-tests: $(CORE_SOURCES:src/%.cpp=build/%.o) build/archive_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/archive_tests.o: tests/archive_tests.cpp
	@mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

check: build/bufrom build/native-tests build/archive-tests
	WREPORT_EXTRA_TABLES="$(CURDIR)/tables" ./build/native-tests tests/data
	WREPORT_EXTRA_TABLES="$(CURDIR)/tables" ./build/archive-tests tests/data/meteohub-simnbo.bufr
	./tests/smoke.sh
	./tests/pipeline.sh

clean:
	rm -rf build

-include $(APP_OBJECTS:.o=.d) build/native_tests.d build/archive_tests.d

