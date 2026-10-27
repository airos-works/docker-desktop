# Docker Desktop for air/OS: native Haiku build (like airShot).
#   make              builds build-haiku/DockerDesktop
#   make package      builds artifacts/docker-desktop-<version>-<arch>.hpkg
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
CPPFLAGS += -Isrc $(CROSS_CPPFLAGS)
CXXFLAGS ?= -O2 -g
RC ?= rc
XRES ?= xres
MIMESET ?= mimeset
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter -Wno-sign-compare

APP_SRC = $(wildcard src/*.cpp)
APP_OBJ = $(APP_SRC:%.cpp=$(BUILD)/%.o)
APP_LIBS = -lbe -lnetwork -lpthread

.PHONY: all package clean check

all: $(BUILD)/DockerDesktop

$(BUILD)/DockerDesktop: $(APP_OBJ) resources/docker-desktop.rdef
	$(CXX) $(APP_LDFLAGS) -o $@.new $(APP_OBJ) $(APP_LIBS) $(APP_LDEND)
	$(RC) -o $(BUILD)/docker-desktop.rsrc resources/docker-desktop.rdef
	$(XRES) -o $@.new $(BUILD)/docker-desktop.rsrc
	$(MIMESET) -f $@.new
	mv $@.new $@

$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(APP_CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

package: all
	bash tools/package-haiku.sh

clean:
	rm -rf $(BUILD)

-include $(APP_OBJ:.o=.d)
