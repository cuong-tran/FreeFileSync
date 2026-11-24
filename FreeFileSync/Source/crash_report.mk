#Crash reporting with sentry-native ("make FFS_SENTRY=1 SENTRY_DSN=<dsn>", see zen/crash_report.h), included by the
#FreeFileSync and RealTimeSync makefiles for that build only, and not for "make clean". Expects objPath, objFiles and binDir
#(the folder of the binary).
#
#sentry-native comes from pkg-config (sentry-native.pc, written by .github/build/build-sentry-native.sh). Build variables:
#   SENTRY_DSN          required; compiled in through a generated header, so it is on no command line (keep it out of logs)
#   SENTRY_ENVIRONMENT  "dev" (default), "ci" or "release"
#   FFS_GIT_SHA         short commit hash for the release name freefilesync@<version>+<sha> (default: none => "+dev")

crashReportDir := $(dir $(lastword $(MAKEFILE_LIST)))

#macOS: sentry-native from .build/sentry/macos-<arch> (build-sentry-native.sh with FFS_TARGET=macos-<arch>) unless FFS_SENTRY_PREFIX names
#another prefix, plus the .pc files the app's libraries come from (platform.mk's macPkgConfig), never Homebrew's
ifeq ($(TARGET_OS),mac)
    FFS_SENTRY_PREFIX ?= $(abspath $(crashReportDir)../../.build/sentry/macos-$(ARCH))
    sentryPkgConfig := PKG_CONFIG_LIBDIR=$(FFS_SENTRY_PREFIX)/lib/pkgconfig:$(FFS_DEPS_PREFIX)/lib/pkgconfig:$(abspath $(crashReportDir)../../.github/build/pkgconfig-macos) pkg-config
else
    sentryPkgConfig := pkg-config
endif

ifneq ($(shell $(sentryPkgConfig) --exists sentry-native && echo YES),YES)
    $(error FFS_SENTRY=1: sentry-native not found by pkg-config. Build it with .github/build/build-sentry-native.sh and add its lib/pkgconfig folder to PKG_CONFIG_PATH)
endif

#debug info for symbolicating crash reports: .github/build/package.sh splits it off into separate files for the upload with
#sentry-cli, so the shipped binaries stay as large as without it (the default build has none)
CXXFLAGS += -g
LDFLAGS  += -g

SENTRY_ENVIRONMENT ?= dev
#the recipe below reads them from its environment: their values never appear in make's command echo
export SENTRY_DSN SENTRY_ENVIRONMENT FFS_GIT_SHA

crashReportObj    := $(filter %/zen/crash_report_sentry.cpp.o,$(objFiles))
crashReportHeader := $(objPath)/generated/crash_report_build.h

$(crashReportObj): CXXFLAGS += $(shell $(sentryPkgConfig) --cflags sentry-native) -I$(dir $(crashReportHeader))
$(crashReportObj): $(crashReportHeader)
LDFLAGS += $(shell $(sentryPkgConfig) --libs sentry-native)

#rewritten only when a value changes, so the object is recompiled exactly then
$(crashReportHeader): FORCE
	mkdir -p $(dir $@)
	$(SHELL) $(crashReportDir)crash_report_header.sh $@

#the crash handler runs next to the binary (zen/crash_report.h: CrashReportConfig::handlerPath)
crashHandlerSrc := $(shell $(sentryPkgConfig) --variable=handler sentry-native)
ifeq ($(crashHandlerSrc),)
    $(error FFS_SENTRY=1: sentry-native.pc names no crash handler (variable "handler"). Rebuild sentry-native with .github/build/build-sentry-native.sh)
endif
crashHandler    := $(binDir)/crashpad_handler

all: $(crashHandler)

$(crashHandler): $(crashHandlerSrc)
	mkdir -p $(dir $@)
	cp -f $< $@

.PHONY: FORCE
