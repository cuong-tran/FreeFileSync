SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c
#-e  exit script when last command fails
#-u  treat undefined variables as error
#-o pipefail  consider all individual calls when using pipes
#-c  execute the subsequent command string passed by Make
CXX ?= g++

#TARGET_OS ("linux", "mac") and ARCH: see platform.mk
include platform.mk

#compiler family, detected positively from the compiler's predefined macros: "gcc", "clang" or empty (unknown: no family-specific flags)
compilerMacros := $(shell $(CXX) -dM -E -x c++ /dev/null 2>/dev/null)
compilerFamily := $(if $(findstring __clang__,$(compilerMacros)),clang,$(if $(findstring __GNUC__,$(compilerMacros)),gcc))

#toolchains other than the default g++ + libstdc++ get their own binary name and object folder (exeName is part of both),
#so switching a checkout between toolchains never links objects of another C++ library or keeps another toolchain's binary
toolchainSuffix := $(if $(filter clang,$(compilerFamily)),-clang)$(if $(STDLIB),-$(STDLIB))
ifeq ($(TARGET_OS),linux)
exeName := FreeFileSync_$(shell arch)$(toolchainSuffix)
else ifeq ($(TARGET_OS),mac) #Apple clang + libc++ is the default toolchain there: no suffix for it
exeName := FreeFileSync_$(ARCH)$(if $(STDLIB),-$(STDLIB))
endif

CXXFLAGS += -std=c++26 -pipe -I../.. -I../../zenXml -include "zen/i18n.h" \
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

#crash reporting (zen/crash_report.h): "make FFS_SENTRY=1 SENTRY_DSN=<dsn>" builds with sentry-native (crash_report.mk), the default
#build without any reporting code (zen/crash_report_none.cpp). Same binary name, but its own object folder, so switching never mixes objects
ifeq ($(FFS_SENTRY),1)
    ifeq ($(strip $(SENTRY_DSN))$(if $(filter-out clean,$(MAKECMDGOALS)),,$(filter clean,$(MAKECMDGOALS))),) #"make clean" alone needs no DSN
        $(error FFS_SENTRY=1 needs a DSN: make FFS_SENTRY=1 SENTRY_DSN=<dsn>, or SENTRY_DSN in the environment)
    endif
    crashReportSuffix := -sentry
else ifneq ($(FFS_SENTRY),)
    $(error FFS_SENTRY must be empty or 1, not "$(FFS_SENTRY)")
endif

CXX += -pthread

#build mode: "make" (default) for an optimized build into ../Build/Bin,
#"make BUILD=debug" (or "make debug") for a debug build into ../Build/Debug/Bin
BUILD ?= release
#the debug build keeps -DNDEBUG like the release build, so assert() stays disabled
ifeq ($(BUILD),debug)
    CXXFLAGS += -g3 -O0 #the later -O0 overrides -O3 above
    LDFLAGS  += -g
    buildRoot := ../Build/Debug
else ifeq ($(BUILD),release)
    buildRoot := ../Build
else
    $(error BUILD must be "release" or "debug", not "$(BUILD)")
endif
#object files stay inside this checkout instead of a fixed folder in the shared temp directory:
#builds of different checkouts never mix their objects, and other users can't plant files or symlinks there
objPath := $(buildRoot)/Obj/$(exeName)$(crashReportSuffix)

ifeq ($(TARGET_OS),linux)
CXXFLAGS += $(shell wx-config --cxxflags --debug=no)
LDFLAGS  += $(shell wx-config --libs std, aui, richtext --debug=no)

CXXFLAGS += $(shell pkg-config --cflags openssl)
LDFLAGS  += $(shell pkg-config --libs openssl)

CXXFLAGS += $(shell pkg-config --cflags libcurl)
LDFLAGS  += $(shell pkg-config --libs libcurl)

CXXFLAGS += $(shell pkg-config --cflags libidn2)
LDFLAGS  += $(shell pkg-config --libs libidn2)

CXXFLAGS += $(shell pkg-config --cflags libssh2)
LDFLAGS  += $(shell pkg-config --libs libssh2)

CXXFLAGS += $(shell pkg-config --cflags zlib)
LDFLAGS  += $(shell pkg-config --libs zlib)

CXXFLAGS += $(shell pkg-config --cflags gtk+-3.0)
#treat as system headers so that warnings are hidden:
CXXFLAGS += -isystem/usr/include/gtk-3.0

else ifeq ($(TARGET_OS),mac)
#static libraries from FFS_DEPS_PREFIX (platform.mk) for ARCH and the deployment target; zlib (.github/build/pkgconfig-macos) and the
#frameworks come from the SDK
CXXFLAGS += $(macTargetFlags)
LDFLAGS  += $(macTargetFlags)

CXXFLAGS += $(shell $(macWxConfig) --cxxflags --debug=no)
LDFLAGS  += $(shell $(macWxConfig) --libs std, aui, richtext --debug=no)

#pkg-config prints nothing when a package or one it requires is missing: stop instead of building without the flags
ifeq ($(filter-out clean,$(or $(MAKECMDGOALS),all)),)
else ifneq ($(shell $(macPkgConfig) --exists openssl libcurl libidn2 libssh2 && echo YES),YES)
    $(error TARGET_OS=mac: pkg-config does not find openssl, libcurl, libidn2 and libssh2 in $(FFS_DEPS_PREFIX): $(shell $(macPkgConfig) --print-errors --exists openssl libcurl libidn2 libssh2 2>&1))
endif
CXXFLAGS += $(shell $(macPkgConfig) --cflags openssl libcurl libidn2 libssh2)
LDFLAGS  += $(shell $(macPkgConfig) --static --libs libcurl libssh2 libidn2 openssl)

#CoreServices: FSEvents; Foundation: NSFileManager, NSError; AppKit, ImageIO, UniformTypeIdentifiers: file icons and thumbnails
#(base/icon_loader_mac.mm); Security, SystemConfiguration: libcurl (macOS trust store, proxy settings)
LDFLAGS  += -framework CoreFoundation -framework CoreServices -framework Foundation -framework AppKit -framework ImageIO \
            -framework UniformTypeIdentifiers -framework Security -framework SystemConfiguration
endif

#wxWidgets without the wxColorHook patch from Bugs.txt (e.g. stock release or distro packages): let wx+/darkmode.cpp declare the hook itself
#(YES/NO, or empty if the preprocessor run itself fails => leave it to the real compile to report the error)
haveWxColorHook := $(shell if wxSettings=$$($(CXX) $(CXXFLAGS) -E -x c++ -include wx/settings.h /dev/null 2>/dev/null); then grep -q refGlobalColorHook <<< "$$wxSettings" && echo YES || echo NO; fi)
ifeq ($(haveWxColorHook),NO)
    $(warning wxWidgets lacks the wxColorHook patch from Bugs.txt => building without the enhanced dark mode text contrast)
    CXXFLAGS += -DZEN_WX_NO_COLOR_HOOK
endif

ifeq ($(TARGET_OS),linux)
#support for SELinux (optional)
haveSelinux := $(shell pkg-config --exists libselinux && echo YES)
ifeq ($(haveSelinux),YES)
    CXXFLAGS += $(shell pkg-config --cflags libselinux) -DHAVE_SELINUX
    LDFLAGS  += $(shell pkg-config --libs libselinux)
endif
endif

cppFiles :=
cppFiles += application.cpp
cppFiles += base_tools.cpp
cppFiles += config.cpp
cppFiles += ffs_paths.cpp
cppFiles += icon_buffer.cpp
cppFiles += localization.cpp
cppFiles += log_file.cpp
cppFiles += status_handler.cpp
cppFiles += base/algorithm.cpp
cppFiles += base/binary.cpp
cppFiles += base/comparison.cpp
cppFiles += base/db_file.cpp
cppFiles += base/dir_lock.cpp
cppFiles += base/file_hierarchy.cpp
cppFiles += base/icon_loader.cpp
cppFiles += base/multi_rename.cpp
cppFiles += base/parallel_scan.cpp
cppFiles += base/path_filter.cpp
cppFiles += base/speed_test.cpp
cppFiles += base/structures.cpp
cppFiles += base/synchronization.cpp
cppFiles += base/versioning.cpp
cppFiles += afs/abstract.cpp
cppFiles += afs/concrete.cpp
cppFiles += afs/ftp.cpp
cppFiles += afs/gdrive.cpp
cppFiles += afs/init_curl_libssh2.cpp
cppFiles += afs/native.cpp
cppFiles += afs/sftp.cpp
cppFiles += afs/zip.cpp
cppFiles += ui/batch_config.cpp
cppFiles += ui/abstract_folder_picker.cpp
cppFiles += ui/batch_status_handler.cpp
cppFiles += ui/cfg_grid.cpp
cppFiles += ui/command_box.cpp
cppFiles += ui/folder_history_box.cpp
cppFiles += ui/folder_selector.cpp
cppFiles += ui/file_grid.cpp
cppFiles += ui/file_view.cpp
cppFiles += ui/log_panel.cpp
cppFiles += ui/tree_grid.cpp
cppFiles += ui/gui_generated.cpp
cppFiles += ui/gui_status_handler.cpp
cppFiles += ui/main_dlg.cpp
cppFiles += ui/progress_indicator.cpp
cppFiles += ui/rename_dlg.cpp
cppFiles += ui/search_grid.cpp
cppFiles += ui/small_dlgs.cpp
cppFiles += ui/sync_cfg.cpp
cppFiles += ui/tray_icon.cpp
cppFiles += ui/triple_splitter.cpp
cppFiles += ui/version_check.cpp
cppFiles += ../../libcurl/curl_wrap.cpp
cppFiles += ../../zen/argon2.cpp
cppFiles += ../../zen/file_access.cpp
cppFiles += ../../zen/file_io.cpp
cppFiles += ../../zen/file_path.cpp
cppFiles += ../../zen/file_traverser.cpp
cppFiles += ../../zen/http.cpp
cppFiles += ../../zen/zstring.cpp
cppFiles += ../../zen/format_unit.cpp
cppFiles += ../../zen/legacy_compiler.cpp
cppFiles += ../../zen/open_ssl.cpp
cppFiles += ../../zen/process_priority.cpp
cppFiles += ../../zen/recycler.cpp
cppFiles += ../../zen/resolve_path.cpp
cppFiles += ../../zen/process_exec.cpp
cppFiles += ../../zen/shutdown.cpp
cppFiles += ../../zen/sys_error.cpp
cppFiles += ../../zen/sys_info.cpp
cppFiles += ../../zen/sys_version.cpp
cppFiles += ../../zen/thread.cpp
cppFiles += ../../zen/zlib_wrap.cpp
cppFiles += ../../wx+/darkmode.cpp
cppFiles += ../../wx+/file_drop.cpp
cppFiles += ../../wx+/grid.cpp
cppFiles += ../../wx+/image_tools.cpp
cppFiles += ../../wx+/graph.cpp
cppFiles += ../../wx+/taskbar.cpp
cppFiles += ../../wx+/tooltip.cpp
cppFiles += ../../wx+/image_resources.cpp
cppFiles += ../../wx+/popup_dlg.cpp
cppFiles += ../../wx+/popup_dlg_generated.cpp
cppFiles += ../../xBRZ/src/xbrz.cpp
ifeq ($(FFS_SENTRY),1)
cppFiles += ../../zen/crash_report_sentry.cpp
cppFiles += ../../zen/crash_scrub.cpp
else
cppFiles += ../../zen/crash_report_none.cpp
endif

#macOS: the platform's own implementations (*_mac.*) in place of the Linux-only files
ifeq ($(TARGET_OS),mac)
cppFiles := $(filter-out ../../zen/recycler.cpp base/icon_loader.cpp,$(cppFiles))
cppFiles += ../../zen/recycler_mac.mm
cppFiles += ../../zen/sys_error_mac.mm
cppFiles += base/icon_loader_mac.mm
endif

objFiles := $(cppFiles:%=$(objPath)/ffs/src/%.o)
target   := $(buildRoot)/Bin/$(exeName)

#the engine (comparison, synchronization, file systems, configuration, zen/) without the GUI: everything except application.cpp, ui/,
#wx+/ and xBRZ/ (used by wx+/image_tools.cpp only), and without a crash reporting implementation (the program linking it picks one).
#"make engine" builds it as a static library from the same objects as the app, for headless programs such as Tests/sync;
#"make engine-flags" prints the compiler and linker flags such a program needs (CXX=, CXXFLAGS=, LDFLAGS=, ENGINE_LIB=, one per line).
#Some of its files still need wxWidgets: see CONTRIBUTING.md, "The engine library".
engineFiles := $(filter-out application.cpp ui/% ../../wx+/% ../../xBRZ/% ../../zen/crash_report_% ../../zen/crash_scrub.cpp,$(cppFiles))
engineLib   := $(objPath)/libffs_engine.a

.PHONY: all clean debug debugResources engine engine-flags

ifeq ($(TARGET_OS),linux)
all: $(target)
else
#the GUI is not ported yet (M3 step 4 of docs/PORTING_PLAN.md): only "make engine" works on macOS
all:
	@echo 'FreeFileSync does not build for TARGET_OS=$(TARGET_OS) yet (the wxWidgets UI is ported in M3 step 4); "make engine" builds the engine library' >&2
	@exit 1
endif

engine: $(engineLib)

$(engineLib): $(engineFiles:%=$(objPath)/ffs/src/%.o)
	rm -f $@
	$(AR) rcs $@ $^

engine-flags:
	$(info CXX=$(CXX))
	$(info CXXFLAGS=$(CXXFLAGS))
	$(info LDFLAGS=$(LDFLAGS))
	$(info ENGINE_LIB=$(abspath $(engineLib)))
	@:

#relink after switching between the default and the FFS_SENTRY=1 build: same binary, objects from different folders
#(a stamp shared by both object folders, rewritten only when the build switches; written while parsing, so make -n shows no command)
crashReportStamp := $(patsubst %-sentry,%,$(objPath)).crashreport
$(shell mkdir -p $(dir $(crashReportStamp)) && { echo '$(FFS_SENTRY)' | cmp -s - $(crashReportStamp) || echo '$(FFS_SENTRY)' > $(crashReportStamp); })

$(target): $(objFiles) $(crashReportStamp)
	mkdir -p $(dir $@)
	$(CXX) -o $@ $(objFiles) $(LDFLAGS)

#not for "make clean" alone: crash_report.mk checks that sentry-native is installed ("make clean all" still includes it)
ifeq ($(FFS_SENTRY)$(if $(filter-out clean,$(MAKECMDGOALS)),,$(filter clean,$(MAKECMDGOALS))),1)
binDir := $(buildRoot)/Bin
include crash_report.mk
endif

ifeq ($(BUILD),debug)
#the binary looks for its Resources folder next to its Bin folder: refresh the copy on every debug build
all: debugResources

debugResources:
	mkdir -p $(buildRoot)
	rm -rf $(buildRoot)/Resources
	cp -r ../Build/Resources $(buildRoot)/
endif

#header dependencies generated by -MMD: rebuild objects when an included header changes
#an object without its .d file (e.g. left over from a build without -MMD) is rebuilt, which recreates the .d file
depFiles := $(objFiles:.o=.d)
-include $(wildcard $(depFiles))
#explicit targets, so make doesn't treat a missing .d as an intermediate file
$(depFiles):

$(objPath)/ffs/src/%.o : % $(objPath)/ffs/src/%.d
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

#Objective-C++ (macOS *_mac.mm files) with automatic reference counting
$(filter %.mm.o,$(objFiles)): CXXFLAGS += -fobjc-arc

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
	rm -f $(target) $(crashReportStamp)
ifeq ($(FFS_SENTRY),1)
	if ! compgen -G '$(buildRoot)/Bin/FreeFileSync_*' > /dev/null && ! compgen -G '$(buildRoot)/Bin/RealTimeSync_*' > /dev/null; then rm -f $(buildRoot)/Bin/crashpad_handler; fi #still needed by another binary?
endif
ifeq ($(BUILD),debug)
	rm -rf $(buildRoot)
endif

debug:
	$(MAKE) BUILD=debug
