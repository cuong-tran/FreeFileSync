#Crash reporting with sentry-native ("make FFS_SENTRY=1 SENTRY_DSN=<dsn>", see zen/crash_report.h), included by the
#FreeFileSync and RealTimeSync makefiles for that build only. Expects objPath, objFiles and binDir (the folder of the binary).
#
#sentry-native comes from pkg-config (sentry-native.pc, written by .github/build/build-sentry-native.sh). Build variables:
#   SENTRY_DSN          required; compiled in through a generated header, so it is on no command line (keep it out of logs)
#   SENTRY_ENVIRONMENT  "dev" (default), "ci" or "release"
#   FFS_GIT_SHA         short commit hash for the release name freefilesync@<version>+<sha> (default: none => "+dev")

crashReportDir := $(dir $(lastword $(MAKEFILE_LIST)))

ifneq ($(shell pkg-config --exists sentry-native && echo YES),YES)
    $(error FFS_SENTRY=1: sentry-native not found by pkg-config. Build it with .github/build/build-sentry-native.sh and add its lib/pkgconfig folder to PKG_CONFIG_PATH)
endif

SENTRY_ENVIRONMENT ?= dev
#the recipe below reads them from its environment: their values never appear in make's command echo
export SENTRY_DSN SENTRY_ENVIRONMENT FFS_GIT_SHA

crashReportObj    := $(filter %/zen/crash_report_sentry.cpp.o,$(objFiles))
crashReportHeader := $(objPath)/generated/crash_report_build.h

$(crashReportObj): CXXFLAGS += $(shell pkg-config --cflags sentry-native) -I$(dir $(crashReportHeader))
$(crashReportObj): $(crashReportHeader)
LDFLAGS += $(shell pkg-config --libs sentry-native)

#rewritten only when a value changes, so the object is recompiled exactly then
$(crashReportHeader): FORCE
	mkdir -p $(dir $@)
	$(SHELL) $(crashReportDir)crash_report_header.sh $@

#the crash handler runs next to the binary (zen/crash_report.h: CrashReportConfig::handlerPath)
crashHandlerSrc := $(shell pkg-config --variable=handler sentry-native)
crashHandler    := $(binDir)/crashpad_handler

all: $(crashHandler)

$(crashHandler): $(crashHandlerSrc)
	mkdir -p $(dir $@)
	cp -f $< $@

.PHONY: FORCE
