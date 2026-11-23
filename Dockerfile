# syntax=docker/dockerfile:1

# Linux build environment for FreeFileSync and RealTimeSync.
# Builds libcurl and wxWidgets at the versions named in Bugs.txt and the latest libssh2 release from source.
# wxWidgets gets the wx/settings.h color hook patch FreeFileSync needs to compile. libssh2 is not patched: afs/sftp.cpp
# falls back to libssh2's MAX_SFTP_* values. The other Bugs.txt patches only fix runtime behavior and are not applied.
#
# Build the image once, then compile the mounted source tree (binaries land in FreeFileSync/Build/):
#   docker build -t freefilesync-build .
#   docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" freefilesync-build
#   docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" freefilesync-build make -C FreeFileSync/Source BUILD=debug -j"$(nproc)"
#
# The makefiles keep object files in /tmp. Add "-v freefilesync-objs:/tmp" to keep them between runs for incremental builds.
#
# libssh2 and libcurl are installed as shared libraries under /usr/local, so the binaries run inside this image.

FROM ubuntu:26.04

ARG LIBSSH2_VERSION=1.11.1
ARG CURL_VERSION=8.21.0
ARG WX_VERSION=3.3.4

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	pkg-config \
	ca-certificates \
	wget \
	bzip2 \
	patch \
	libgtk-3-dev \
	libssl-dev \
	zlib1g-dev \
	libpsl-dev \
	libidn2-dev \
	libselinux1-dev \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /build

# --- libssh2 ---
RUN wget -q https://libssh2.org/download/libssh2-${LIBSSH2_VERSION}.tar.gz \
	&& tar xf libssh2-${LIBSSH2_VERSION}.tar.gz \
	&& cd libssh2-${LIBSSH2_VERSION} \
	&& ./configure --quiet --with-crypto=openssl --disable-static --disable-examples-build \
	&& make -j"$(nproc)" \
	&& make install \
	&& ldconfig \
	&& cd /build && rm -rf libssh2-${LIBSSH2_VERSION}*

# --- libcurl ---
RUN wget -q https://curl.se/download/curl-${CURL_VERSION}.tar.gz \
	&& tar xf curl-${CURL_VERSION}.tar.gz \
	&& cd curl-${CURL_VERSION} \
	&& ./configure --quiet --with-openssl --with-libssh2 --disable-static --disable-manual --disable-docs --enable-versioned-symbols \
	&& make -j"$(nproc)" \
	&& make install \
	&& ldconfig \
	&& cd /build && rm -rf curl-${CURL_VERSION}*

# --- wxWidgets ---
# Bugs.txt: support changing system colors for proper dark mode support
# (plus the <memory> include the patched header needs when wxWidgets itself is compiled)
COPY <<'EOF' /build/wx-settings.patch
--- a/include/wx/settings.h
+++ b/include/wx/settings.h
@@ -12,6 +12,7 @@
 
 #include "wx/colour.h"
 #include "wx/font.h"
+#include <memory>
 
 class WXDLLIMPEXP_FWD_CORE wxWindow;
 
@@ -248,10 +249,29 @@
 // ----------------------------------------------------------------------------
 // include the declaration of the real platform-dependent class
 // ----------------------------------------------------------------------------
+
+struct wxColorHook
+{
+    virtual ~wxColorHook() {}
+    virtual wxColor getColor(wxSystemColour index) const = 0;
+};
+WXDLLIMPEXP_CORE inline std::unique_ptr<wxColorHook>& refGlobalColorHook()
+{
+    static std::unique_ptr<wxColorHook> globalColorHook;
+    return globalColorHook;
+}
 
 class WXDLLIMPEXP_CORE wxSystemSettings : public wxSystemSettingsNative
 {
 public:
+    static wxColour GetColour(wxSystemColour index)
+    {
+        if (refGlobalColorHook())
+            return refGlobalColorHook()->getColor(index);
+
+        return wxSystemSettingsNative::GetColour(index);
+    }
+
 #ifdef __WXUNIVERSAL__
     // in wxUniversal we want to use the theme standard colours instead of the
     // system ones, otherwise wxSystemSettings is just the same as
EOF

# FreeFileSync refuses to compile against a wxWidgets with C++ exception handling enabled (see application.cpp),
# so build it with --disable-exceptions.
RUN wget -q https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2 \
	&& tar xf wxWidgets-${WX_VERSION}.tar.bz2 \
	&& cd wxWidgets-${WX_VERSION} \
	&& patch -p1 < /build/wx-settings.patch \
	&& mkdir gtk-build && cd gtk-build \
	&& ../configure --quiet --with-gtk=3 --disable-shared --disable-exceptions \
	&& make -j"$(nproc)" \
	&& make install \
	&& cd /build && rm -rf wxWidgets-${WX_VERSION}* wx-settings.patch

WORKDIR /src

CMD ["sh", "-c", "make -C FreeFileSync/Source -j\"$(nproc)\" && make -C FreeFileSync/Source/RealTimeSync -j\"$(nproc)\""]
