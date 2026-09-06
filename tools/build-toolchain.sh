#!/bin/bash
# Build the GNU cross toolchain used by jornada-gpib on macOS (Apple Silicon or Intel).
#
#   sh-pe   binutils : assembler, PE/COFF linker (Windows CE subsystem), dlltool, objdump
#   sh-elf  binutils : needed only so that GCC's own build can run its target assembler
#   sh-elf  GCC (C)  : used with -S to turn C into SH-3 assembly, which sh-pe-as assembles
#
# Everything lands under ~/.cache/jornada-gpib (never inside the iCloud-synced Desktop).
# Re-running is safe: finished stages are skipped.
set -euo pipefail

CACHE="${JORNADA_GPIB_CACHE:-$HOME/.cache/jornada-gpib}"
SRC="$CACHE/src"
BUILD="$CACHE/build"
PREFIX="${JORNADA_GPIB_XTOOLS:-$CACHE/xtools}"
BINUTILS_VER="${BINUTILS_VER:-2.47}"
GCC_VER="${GCC_VER:-15.3.0}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
die() { log "ERROR: $*" >&2; exit 1; }

mkdir -p "$SRC" "$BUILD" "$PREFIX"

fetch() {  # fetch <relative gnu path> <file>
    local rel="$1" file="$2"
    [ -s "$SRC/$file" ] && return 0
    log "downloading $file"
    curl -sS -L --max-time 1800 -o "$SRC/$file.part" "https://mirrors.kernel.org/gnu/$rel" \
        || curl -sS -L --max-time 1800 -o "$SRC/$file.part" "https://ftp.gnu.org/gnu/$rel"
    xz -t "$SRC/$file.part" || die "corrupt download: $file"
    mv "$SRC/$file.part" "$SRC/$file"
}

extract() {  # extract <tarball> <dir name>
    [ -d "$SRC/$2" ] && return 0
    log "extracting $1"
    tar -C "$SRC" -xf "$SRC/$1"
}

build_binutils() {  # build_binutils <target>
    local target="$1" bdir="$BUILD/binutils-$1"
    if [ -x "$PREFIX/bin/$target-ld" ] && [ -x "$PREFIX/bin/$target-as" ]; then
        log "binutils $target already installed, skipping"; return 0
    fi
    log "configuring binutils $BINUTILS_VER for $target"
    rm -rf "$bdir"; mkdir -p "$bdir"; cd "$bdir"
    "$SRC/binutils-$BINUTILS_VER/configure" --target="$target" --prefix="$PREFIX" \
        --disable-nls --disable-werror --disable-gprofng --disable-gdb --disable-sim \
        --with-system-zlib MAKEINFO=true > configure.log 2>&1 || die "configure failed, see $bdir/configure.log"
    log "building binutils $target (jobs=$JOBS)"
    make -j"$JOBS" MAKEINFO=true > make.log 2>&1 || die "make failed, see $bdir/make.log"
    make install MAKEINFO=true > install.log 2>&1 || die "install failed, see $bdir/install.log"
    log "binutils $target installed"
}

build_gcc() {
    local bdir="$BUILD/gcc-sh-elf"
    if [ -x "$PREFIX/bin/sh-elf-gcc" ]; then
        log "gcc sh-elf already installed, skipping"; return 0
    fi
    log "configuring gcc $GCC_VER for sh-elf"
    rm -rf "$bdir"; mkdir -p "$bdir"; cd "$bdir"
    "$SRC/gcc-$GCC_VER/configure" --target=sh-elf --prefix="$PREFIX" \
        --enable-languages=c --disable-nls --disable-shared --disable-threads \
        --disable-libssp --disable-libquadmath --disable-libgomp --disable-libatomic \
        --disable-multilib --with-endian=little --with-cpu=sh3 \
        --without-headers --with-newlib --without-isl --with-system-zlib \
        --with-gmp="$BREW_PREFIX" --with-mpfr="$BREW_PREFIX" --with-mpc="$BREW_PREFIX" \
        --with-as="$PREFIX/bin/sh-elf-as" --with-ld="$PREFIX/bin/sh-elf-ld" \
        MAKEINFO=true > configure.log 2>&1 || die "configure failed, see $bdir/configure.log"
    log "building gcc (all-gcc, jobs=$JOBS)"
    make -j"$JOBS" all-gcc MAKEINFO=true > make-gcc.log 2>&1 || die "make all-gcc failed, see $bdir/make-gcc.log"
    make install-gcc MAKEINFO=true > install-gcc.log 2>&1 || die "install-gcc failed"
    log "building libgcc (best effort, used only as a source of helper routines)"
    if make -j"$JOBS" all-target-libgcc MAKEINFO=true > make-libgcc.log 2>&1; then
        make install-target-libgcc MAKEINFO=true > install-libgcc.log 2>&1 || log "libgcc install failed (non-fatal)"
    else
        log "libgcc build failed (non-fatal), see $bdir/make-libgcc.log"
    fi
    log "gcc sh-elf installed"
}

fetch "binutils/binutils-$BINUTILS_VER.tar.xz" "binutils-$BINUTILS_VER.tar.xz"
fetch "gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" "gcc-$GCC_VER.tar.xz"
extract "binutils-$BINUTILS_VER.tar.xz" "binutils-$BINUTILS_VER"
extract "gcc-$GCC_VER.tar.xz" "gcc-$GCC_VER"

build_binutils sh-pe
build_binutils sh-elf
build_gcc

log "done. tools in $PREFIX/bin:"
ls "$PREFIX/bin"
