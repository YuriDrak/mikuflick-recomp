#!/bin/sh
# The Makefile runs this script from ios/. Use Android's release checksums.
set -eu
mkdir -p build/deps
stage=$(mktemp -d build/deps/.fetch.XXXXXX)
trap 'rm -rf "$stage"' EXIT
fetch() {
    project=$1
    release=$2
    version=$3
    archive="$release-$version.tar.gz"
    dest="build/deps/$release-$version"
    [ -d "$dest" ] && return 0
    checksum=$(awk -v name="$archive" '$2 == name { print $1 }' ../android/SHA256SUMS)
    if [ -z "$checksum" ]; then echo "fetch-deps: no checksum for $archive in android/SHA256SUMS" >&2; exit 1; fi
    curl -fLsS --retry 2 -o "$stage/$archive" \
        "https://github.com/libsdl-org/$project/releases/download/release-$version/$archive"
    printf '%s  %s\n' "$checksum" "$stage/$archive" | shasum -a 256 -c -
    tar -xzf "$stage/$archive" -C "$stage"
    mv "$stage/$release-$version" build/deps/ || test -d "$dest"
}
fetch SDL SDL2 2.30.10
fetch SDL_image SDL2_image 2.8.2
fetch SDL_ttf SDL2_ttf 2.22.0
