SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c
#-e  exit script when last command fails
#-u  treat undefined variables as error
#-o pipefail  consider all individual calls when using pipes
#-c  execute the subsequent command string passed by Make
CXX ?= g++

#compiler family, detected positively from the compiler's predefined macros: "gcc", "clang" or empty (unknown: no family-specific flags)
compilerMacros := $(shell $(CXX) -dM -E -x c++ /dev/null 2>/dev/null)
compilerFamily := $(if $(findstring __clang__,$(compilerMacros)),clang,$(if $(findstring __GNUC__,$(compilerMacros)),gcc))

#toolchains other than the default g++ + libstdc++ get their own binary name and object folder (exeName is part of both),
#so switching a checkout between toolchains never links objects of another C++ library or keeps another toolchain's binary
toolchainSuffix := $(if $(filter clang,$(compilerFamily)),-clang)$(if $(STDLIB),-$(STDLIB))
exeName := RealTimeSync_$(shell arch)$(toolchainSuffix)

CXXFLAGS += -std=c++26 -pipe -I../../.. -I../../../zenXml -include "zen/i18n.h" \
           -DWXINTL_NO_GETTEXT_MACRO -DwxNO_IMPLICIT_WXSTRING_ENCODING -DwxNO_IMPLICIT_WXSTRING_CONV_TO_PTR \
           -Wall -Wfatal-errors -Wshadow -Wmissing-include-dirs -Wswitch-enum -Wcast-align -Wnon-virtual-dtor -Wimplicit-fallthrough -Wundef -Wpedantic -Wredundant-decls \
           -Wno-unused-function -Wno-deprecated-enum-enum-conversion \
           -O3 -DNDEBUG
#GCC-only warning flags for g++, their closest clang counterparts for clang
ifeq ($(compilerFamily),gcc)
CXXFLAGS += -Wno-maybe-uninitialized -Wno-free-nonheap-object -Wno-restrict -Wduplicated-cond -Wlogical-op
else ifeq ($(compilerFamily),clang)
#-Wlogical-op, -Wduplicated-cond: clang has no direct equivalent; -Wtautological-overlap-compare catches contradictory or redundant comparisons
#-Wno-potentially-evaluated-expression: "typeid(path.afsDevice.ref())" evaluates its operand on purpose, to get the dynamic type
#-Wno-unused-const-variable: constants kept for features this edition doesn't compile in (e.g. the EMAIL_* limits in log_file.cpp)
#-Wno-unused-lambda-capture: captures of constants clang can use without capturing (GCC and MSVC may still require them)
CXXFLAGS += -Wtautological-overlap-compare -Wno-potentially-evaluated-expression -Wno-unused-const-variable -Wno-unused-lambda-capture
endif

#optional: "make STDLIB=libc++" compiles and links against LLVM's libc++ instead of the compiler's default C++ library
#(libstdc++ on Linux); macOS and the Windows cross build use libc++, so this reproduces their library errors on Linux
ifeq ($(STDLIB),libc++)
    CXXFLAGS += -stdlib=libc++
    LDFLAGS  += -stdlib=libc++
else ifneq ($(STDLIB),)
    $(error STDLIB must be empty or "libc++", not "$(STDLIB)")
endif

#crash reporting: see ../Makefile
ifeq ($(FFS_SENTRY),1)
    ifeq ($(strip $(SENTRY_DSN))$(if $(filter-out clean,$(MAKECMDGOALS)),,$(filter clean,$(MAKECMDGOALS))),) #"make clean" alone needs no DSN
        $(error FFS_SENTRY=1 needs a DSN: make FFS_SENTRY=1 SENTRY_DSN=<dsn>, or SENTRY_DSN in the environment)
    endif
    crashReportSuffix := -sentry
else ifneq ($(FFS_SENTRY),)
    $(error FFS_SENTRY must be empty or 1, not "$(FFS_SENTRY)")
endif

CXX += -pthread

CXXFLAGS += $(shell wx-config --cxxflags --debug=no)
LDFLAGS  += $(shell wx-config --libs std, aui, richtext --debug=no)

CXXFLAGS += $(shell pkg-config --cflags gtk+-3.0)
#treat as system headers so that warnings are hidden:
CXXFLAGS += -isystem/usr/include/gtk-3.0

#wxWidgets without the wxColorHook patch from Bugs.txt (e.g. stock release or distro packages): let wx+/darkmode.cpp declare the hook itself
#(YES/NO, or empty if the preprocessor run itself fails => leave it to the real compile to report the error)
haveWxColorHook := $(shell if wxSettings=$$($(CXX) $(CXXFLAGS) -E -x c++ -include wx/settings.h /dev/null 2>/dev/null); then grep -q refGlobalColorHook <<< "$$wxSettings" && echo YES || echo NO; fi)
ifeq ($(haveWxColorHook),NO)
    $(warning wxWidgets lacks the wxColorHook patch from Bugs.txt => building without the enhanced dark mode text contrast)
    CXXFLAGS += -DZEN_WX_NO_COLOR_HOOK
endif

cppFiles :=
cppFiles += application.cpp
cppFiles += config.cpp
cppFiles += gui_generated.cpp
cppFiles += main_dlg.cpp
cppFiles += tray_menu.cpp
cppFiles += monitor.cpp
cppFiles += folder_selector2.cpp
cppFiles += ../afs/abstract.cpp
cppFiles += ../base/icon_loader.cpp
cppFiles += ../ffs_paths.cpp
cppFiles += ../icon_buffer.cpp
cppFiles += ../localization.cpp
cppFiles += ../../../wx+/darkmode.cpp
cppFiles += ../../../wx+/file_drop.cpp
cppFiles += ../../../wx+/image_tools.cpp
cppFiles += ../../../wx+/image_resources.cpp
cppFiles += ../../../wx+/popup_dlg.cpp
cppFiles += ../../../wx+/popup_dlg_generated.cpp
cppFiles += ../../../wx+/taskbar.cpp
cppFiles += ../../../xBRZ/src/xbrz.cpp
cppFiles += ../../../zen/dir_watcher.cpp
cppFiles += ../../../zen/file_access.cpp
cppFiles += ../../../zen/file_io.cpp
cppFiles += ../../../zen/file_path.cpp
cppFiles += ../../../zen/file_traverser.cpp
cppFiles += ../../../zen/format_unit.cpp
cppFiles += ../../../zen/legacy_compiler.cpp
cppFiles += ../../../zen/resolve_path.cpp
cppFiles += ../../../zen/process_exec.cpp
cppFiles += ../../../zen/shutdown.cpp
cppFiles += ../../../zen/sys_error.cpp
cppFiles += ../../../zen/sys_info.cpp
cppFiles += ../../../zen/sys_version.cpp
cppFiles += ../../../zen/thread.cpp
cppFiles += ../../../zen/zstring.cpp
ifeq ($(FFS_SENTRY),1)
cppFiles += ../../../zen/crash_report_sentry.cpp
cppFiles += ../../../zen/crash_scrub.cpp
else
cppFiles += ../../../zen/crash_report_none.cpp
endif

#object files stay inside this checkout instead of a fixed folder in the shared temp directory:
#builds of different checkouts never mix their objects, and other users can't plant files or symlinks there
objPath := ../../Build/Obj/$(exeName)$(crashReportSuffix)

objFiles := $(cppFiles:%=$(objPath)/ffs/src/rts/%.o)

all: ../../Build/Bin/$(exeName)

#relink after switching between the default and the FFS_SENTRY=1 build (see ../Makefile)
crashReportStamp := $(patsubst %-sentry,%,$(objPath)).crashreport
$(shell mkdir -p $(dir $(crashReportStamp)) && { echo '$(FFS_SENTRY)' | cmp -s - $(crashReportStamp) || echo '$(FFS_SENTRY)' > $(crashReportStamp); })

../../Build/Bin/$(exeName): $(objFiles) $(crashReportStamp)
	mkdir -p $(dir $@)
	$(CXX) -o $@ $(objFiles) $(LDFLAGS)

#not for "make clean" alone: crash_report.mk checks that sentry-native is installed ("make clean all" still includes it)
ifeq ($(FFS_SENTRY)$(if $(filter-out clean,$(MAKECMDGOALS)),,$(filter clean,$(MAKECMDGOALS))),1)
binDir := ../../Build/Bin
include ../crash_report.mk
endif

$(objPath)/ffs/src/rts/%.o : %
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

#recompile wx+/darkmode.cpp when the wxColorHook check above changes its result (e.g. after switching between a patched and a stock wxWidgets):
#make doesn't track compiler flags, so keep the result in a stamp file that is rewritten only when the result changes
wxColorHookStamp := $(objPath)/wxColorHook.stamp

$(filter %/wx+/darkmode.cpp.o,$(objFiles)): $(wxColorHookStamp)

$(wxColorHookStamp): FORCE
	mkdir -p $(dir $@)
	echo '$(haveWxColorHook)' | cmp -s - $@ || echo '$(haveWxColorHook)' > $@

.PHONY: FORCE

clean:
	rm -rf $(objPath)
	rm -f ../../Build/Bin/$(exeName) $(crashReportStamp)
ifeq ($(FFS_SENTRY),1)
	if ! compgen -G '../../Build/Bin/FreeFileSync_*' > /dev/null && ! compgen -G '../../Build/Bin/RealTimeSync_*' > /dev/null; then rm -f ../../Build/Bin/crashpad_handler; fi #still needed by another binary?
endif
