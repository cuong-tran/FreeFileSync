#!/usr/bin/env bash
# Build a static wxWidgets for FreeFileSync into $WX_PREFIX (used by .github/workflows/build-arm64.yml).
# Needs WX_VERSION, WX_SHA256 and WX_PREFIX; CC/CXX, if set, come from the environment.
# FreeFileSync needs wxWidgets 3.3 with the wxColorHook patch from Bugs.txt and without
# wxWidgets' own exception handling (application.cpp refuses to build if wxUSE_EXCEPTIONS is set).
set -euo pipefail
: "${WX_VERSION:?}" "${WX_SHA256:?}" "${WX_PREFIX:?}"

workDir=$(mktemp -d)
cd "${workDir}"
curl -fsSL --retry 3 --retry-all-errors -o wx.tar.bz2 "https://github.com/wxWidgets/wxWidgets/releases/download/v${WX_VERSION}/wxWidgets-${WX_VERSION}.tar.bz2"
echo "${WX_SHA256}  wx.tar.bz2" | sha256sum -c -
tar xf wx.tar.bz2
cd "wxWidgets-${WX_VERSION}"

echo '=== Applying the wxColorHook patch from Bugs.txt to include/wx/settings.h ==='
python3 -I - include/wx/settings.h <<'EOF'
import sys
path = sys.argv[1]
src = open(path).read()

def insert(anchor, before='', after=''):
    global src
    if src.count(anchor) != 1:
        sys.exit(f'patch anchor not found exactly once: {anchor!r}')
    src = src.replace(anchor, before + anchor + after)

insert('#include "wx/font.h"\n', after='#include <memory>\n')
insert('class WXDLLIMPEXP_CORE wxSystemSettings : public wxSystemSettingsNative\n{\npublic:\n',
       before='struct wxColorHook\n'
              '{\n'
              '    virtual ~wxColorHook() {}\n'
              '    virtual wxColor getColor(wxSystemColour index) const = 0;\n'
              '};\n'
              'WXDLLIMPEXP_CORE inline std::unique_ptr<wxColorHook>& refGlobalColorHook()\n'
              '{\n'
              '    static std::unique_ptr<wxColorHook> globalColorHook;\n'
              '    return globalColorHook;\n'
              '}\n\n',
       after='    static wxColour GetColour(wxSystemColour index)\n'
             '    {\n'
             '        if (refGlobalColorHook())\n'
             '            return refGlobalColorHook()->getColor(index);\n'
             '\n'
             '        return wxSystemSettingsNative::GetColour(index);\n'
             '    }\n\n')
open(path, 'w').write(src)
EOF

./configure \
  --prefix="${WX_PREFIX}" \
  --disable-shared \
  --with-gtk=3 \
  --disable-exceptions \
  --disable-stc \
  --disable-webview \
  --disable-mediactrl \
  --without-opengl \
  --without-libnotify \
  --with-libpng=builtin \
  --with-libjpeg=builtin \
  --with-libtiff=builtin \
  --with-libwebp=builtin \
  --with-zlib=builtin \
  --with-expat=builtin \
  --with-regex=builtin
make -j"$(nproc)"
make install

# FreeFileSync needs the color hook and no wxWidgets exception handling (application.cpp has an #error for it)
grep -rq 'refGlobalColorHook' "${WX_PREFIX}/include"
grep -rqE '#define wxUSE_EXCEPTIONS +0' "${WX_PREFIX}/lib/wx/include"
