# Docker Desktop for air/OS: native Haiku build (like airShot).
#   make              builds build-haiku/DockerDesktop
#   make package      builds artifacts/docker-desktop-<version>-<arch>.hpkg
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
# BColumnListView's headers are private ones of the Haiku SDK; "=" is the
# compiler's sysroot (empty on Haiku itself, the SDK in cross builds).
PRIVATE_HEADERS ?= =/boot/system/develop/headers/private
CPPFLAGS += -Isrc $(CROSS_CPPFLAGS) -isystem $(PRIVATE_HEADERS)/interface
CXXFLAGS ?= -O2 -g
RC ?= rc
XRES ?= xres
MIMESET ?= mimeset
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter -Wno-sign-compare

APP_SRC = $(wildcard src/*.cpp)
APP_OBJ = $(APP_SRC:%.cpp=$(BUILD)/%.o)
# --export-dynamic: Deskbar loads the executable as an add-on for its
# replicant (DeskbarView.cpp), and finds instantiate_deskbar_item in it.
APP_LIBS = -lcolumnlistview -ltracker -lbe -lnetwork -lpthread

.PHONY: all package clean check

all: $(BUILD)/DockerDesktop

$(BUILD)/DockerDesktop: $(APP_OBJ) resources/docker-desktop.rdef resources/docker-desktop.hvif
	$(CXX) $(APP_LDFLAGS) -Wl,--export-dynamic -o $@.new $(APP_OBJ) $(APP_LIBS) $(APP_LDEND)
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
