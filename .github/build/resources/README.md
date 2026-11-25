# Build resources

Files the upstream source zip lacks, made for this repository and copied into the builds:

- `Animal.dat`: the picture of FreeFileSync's first-start welcome dialog (`ui/small_dlgs.cpp` loads `Resources/Animal.dat` as a JPEG). Upstream ships its own picture only in the official binary packages, under an unclear license, so this is an original image (a cartoon cat drawn with ImageMagick primitives, 256 × 256), released under the repository's license. The macOS app bundles (`FreeFileSync/Source/mac_bundle.sh`) and the Linux packages (`package.sh`) copy it into `Resources/` when the build tree has none.
