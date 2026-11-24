#Target platform for the FreeFileSync and RealTimeSync makefiles (docs/PORTING_PLAN.md, "Build system"), included by both right
#after CXX is set. The Linux build stays as upstream wrote it; this file only decides which platform block the makefiles use.
#
#   TARGET_OS  "linux" or "mac" (default: the build host's, from uname -s). Cross builds are not supported yet: macOS needs
#              Apple's SDK, so it is built on a Mac; Windows ("win") comes with M4.
#   ARCH       macOS only: "arm64" or "x86_64" (default: the host's). The other one is cross-compiled with -arch on the same Mac.
#
#macOS (TARGET_OS=mac) also takes:
#   FFS_DEPS_PREFIX  the static libraries built by .github/build/build.sh with FFS_TARGET=macos-<arch>
#                    (default: <repo>/.build/deps/macos-<arch>); wx-config and pkg-config look only there, never at Homebrew
#and needs GNU make 4 or newer (Homebrew's "gmake"): /usr/bin/make 3.81 has no .SHELLFLAGS.

platformMkDir := $(dir $(lastword $(MAKEFILE_LIST)))
hostOs := $(shell uname -s)

#"make clean" works anywhere: it needs no compiler and no libraries
platformOnlyClean := $(if $(filter-out clean,$(or $(MAKECMDGOALS),all)),,yes)

ifeq ($(hostOs),Darwin)
    TARGET_OS ?= mac
else
    TARGET_OS ?= linux
endif

ifeq ($(filter Linux Darwin,$(hostOs))$(platformOnlyClean),)
    $(error unsupported build host "$(hostOs)": the makefiles build on Linux and on macOS (Darwin); Windows builds come with M4 of docs/PORTING_PLAN.md)
endif

ifeq ($(TARGET_OS),linux)
    ifeq ($(filter Linux,$(hostOs))$(platformOnlyClean),)
        $(error TARGET_OS=linux: building for Linux needs a Linux host (this is $(hostOs)))
    endif
else ifeq ($(TARGET_OS),mac)
    ifeq ($(filter Darwin,$(hostOs))$(platformOnlyClean),)
        $(error TARGET_OS=mac: building for macOS needs a Mac with Xcode (Apple's SDK cannot be used elsewhere); this is $(hostOs))
    endif
    ARCH ?= $(shell uname -m)
    ifeq ($(filter arm64 x86_64,$(ARCH)),)
        $(error ARCH must be "arm64" or "x86_64", not "$(ARCH)")
    endif

    #oldest macOS the apps support: one value for libraries, apps and tests
    macDeploymentTarget := $(shell sed -nE 's/^FFS_MACOS_DEPLOYMENT_TARGET=([0-9.]+).*/\1/p' $(platformMkDir)../../.github/build/common.sh)
    ifeq ($(macDeploymentTarget),)
        $(error no FFS_MACOS_DEPLOYMENT_TARGET in .github/build/common.sh)
    endif
    macTargetFlags := -arch $(ARCH) -mmacosx-version-min=$(macDeploymentTarget)

    FFS_DEPS_PREFIX ?= $(abspath $(platformMkDir)../../.build/deps/macos-$(ARCH))
    ifeq ($(wildcard $(FFS_DEPS_PREFIX)/bin/wx-config),)
        ifneq ($(platformOnlyClean),yes)
            $(error TARGET_OS=mac: no wxWidgets in $(FFS_DEPS_PREFIX); build the libraries with "FFS_TARGET=macos-$(ARCH) .github/build/build.sh" or set FFS_DEPS_PREFIX)
        endif
    endif
    macWxConfig  := $(FFS_DEPS_PREFIX)/bin/wx-config
    #$(call macWxLibs,<wx libraries>): "wx-config --libs" for the prefix's static wxWidgets. build-wxwidgets.sh configures it with
    #--host, so its libraries are installed as lib<name>-3.3-<host>.a; but wx-config adds the "-<host>" only when configure decided
    #it was cross-compiling, which it doesn't where the target's test programs run (arm64 natively, x86_64 under Rosetta 2).
    #A library file (or -l name) that doesn't exist is mapped to the one installed with the host suffix; anything else is kept.
    macWxLibFile = $(if $(wildcard $1),$1,$(or $(wildcard $(1:.a=)-*-apple-darwin*.a),$1))
    macWxLibName = $(if $(wildcard $(FFS_DEPS_PREFIX)/lib/lib$(1:-l%=%).a),$1,$(or $(wildcard $(FFS_DEPS_PREFIX)/lib/lib$(1:-l%=%)-*-apple-darwin*.a),$1))
    macWxLibs    = $(foreach f,$(shell $(macWxConfig) --libs $1 --debug=no),$(if $(filter %.a,$f),$(call macWxLibFile,$f),$(if $(filter -lwx%,$f),$(call macWxLibName,$f),$f)))
    #the prefix's .pc files only (Homebrew's describe libraries for the host architecture), plus one for the SDK's zlib, which
    #the prefix's libcurl and libssh2 require (without it, pkg-config fails for them and prints nothing on stdout)
    macPkgConfig := PKG_CONFIG_LIBDIR=$(FFS_DEPS_PREFIX)/lib/pkgconfig:$(abspath $(platformMkDir)../../.github/build/pkgconfig-macos) pkg-config
else
    $(error TARGET_OS must be "linux" or "mac", not "$(TARGET_OS)")
endif
