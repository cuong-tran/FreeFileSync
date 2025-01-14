# Build instructions

## Requirements

- `sudo apt install pkgconf`
- `sudo apt install libssl-dev`
- `sudo apt install libcurl4-openssl-dev`
- `sudo apt install libssh2-1-dev`
- `sudo apt install libgtk2.0-dev`
- `sudo apt install libgtk-3-dev`

## Build wxWidgets from Source

Follow installation instructions from <https://github.com/jeffli678/build-FreeFileSync>
and <https://github.com/wxWidgets/wxWidgets/blob/master/docs/gtk/install.md>.

Install wxWidgets 3.2.6.

```bash
mkdir buildgtk
cd buildgtk
../configure --with-gtk=3 --enable-no_exceptions  # TODO: Check if wxWidgets should be compiled against gtk3 or gtk2
make -j4
sudo make install
```

## Build FreeFileSync

```bash
cd FreeFileSync/Source
make
```
