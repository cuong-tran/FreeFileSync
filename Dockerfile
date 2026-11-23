# syntax=docker/dockerfile:1

# Linux build environment for FreeFileSync and RealTimeSync.
# Builds libcurl and wxWidgets at the versions named in Bugs.txt from source, and libssh2 from a pinned development
# commit (Bugs.txt: "1.11.2_DEV"; libssh2/libssh2_wrap.h needs LIBSSH2_ERROR_STORE_OVERFLOW, which 1.11.1 lacks).
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

# last libssh2 master commit before the FreeFileSync 14.13 release (2026-10-16)
# .github/workflows/linux-multiarch-build.yml pins the same libssh2 commit and wxWidgets version: update both together
ARG LIBSSH2_COMMIT=83284a05a1add34722c1fa36fa706fe7aced87b6
ARG LIBSSH2_SHA256=03e70390ceafcde9ceb98654d9e55f05c705c2e62c05863d5284e4e11f51b8a7
# curl publishes signatures, not checksums: this SHA-256 is of the tarball verified against its .asc signature
# (Daniel Stenberg, key 27EDEAF22F3ABCEB50DB9A125CC908FDB71E12C2)
ARG CURL_VERSION=8.21.0
ARG CURL_SHA256=d9b327997999045a24cda50f3983e69e51c516bd8be6ef9842fc7f99135e33bb
ARG WX_VERSION=3.3.4
ARG WX_SHA256=815c16cafa4fcbfcf25c5c0a3418309c62257b8a3b6f91d85a1367a134f98c98

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	cmake \
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
RUN wget -q -O libssh2.tar.gz https://github.com/libssh2/libssh2/archive/${LIBSSH2_COMMIT}.tar.gz \
	&& echo "${LIBSSH2_SHA256}  libssh2.tar.gz" | sha256sum -c - \
	&& tar xf libssh2.tar.gz \
	&& cd libssh2-${LIBSSH2_COMMIT} \
	&& cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
		-DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DCRYPTO_BACKEND=OpenSSL -DENABLE_ZLIB_COMPRESSION=ON \
		-DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
	&& cmake --build build -j"$(nproc)" \
	&& cmake --install build \
	&& ldconfig \
	&& cd /build && rm -rf libssh2*

# --- libcurl ---
RUN wget -q https://curl.se/download/curl-${CURL_VERSION}.tar.gz \
	&& echo "${CURL_SHA256}  curl-${CURL_VERSION}.tar.gz" | sha256sum -c - \
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
	&& echo "${WX_SHA256}  wxWidgets-${WX_VERSION}.tar.bz2" | sha256sum -c - \
	&& tar xf wxWidgets-${WX_VERSION}.tar.bz2 \
	&& cd wxWidgets-${WX_VERSION} \
	&& patch -p1 < /build/wx-settings.patch \
	&& mkdir gtk-build && cd gtk-build \
	&& ../configure --quiet --with-gtk=3 --disable-shared --disable-exceptions \
	&& make -j"$(nproc)" \
	&& make install \
	&& cd /build && rm -rf wxWidgets-${WX_VERSION}* wx-settings.patch

# glib 2.78+ (Ubuntu 26.04 ships 2.88) turns g_free() into a function-like macro, which breaks the "::g_free(...)" calls
# in afs/ftp.cpp, base/icon_loader.cpp and zen/zstring.cpp. Capping the glib API at 2.76 keeps it a plain function;
# the makefiles append their own flags to this CXXFLAGS.
ENV CXXFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76"

WORKDIR /src

CMD ["sh", "-c", "make -C FreeFileSync/Source -j\"$(nproc)\" && make -C FreeFileSync/Source/RealTimeSync -j\"$(nproc)\""]
