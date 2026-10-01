#!/bin/sh
# Cross-compiles R InstallWizard for Haiku and leaves
# dist/<arch>/RInstallWizard, resources attached (rc + xres from the Haiku
# build tools). Nothing has to run on Haiku itself.
#
#   tools/cross-build.sh arm64    container with a Haiku arm64 build tree
#   tools/cross-build.sh x86      gcc2 hybrid's modern x86 compiler, on the
#                                 build host that has an x86_gcc2h tree
#   tools/cross-build.sh x86_64   haiku/cross-compiler:x86_64-r1beta4 image
#
#   ARM64_CONTAINER   default haiku-builder
#   ARM64_GENERATED   default /root/renku-arm64-work/generated.arm64
#   X86_HOST          default rainygirl@MacMiniM4 (ssh; Docker Desktop there)
#   X86_CONTAINER     default vaio-p-builder
#   X86_GENERATED     default /vaio-p/work/generated.x86_gcc2h
set -e

ARCH="$1"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HERE/dist/$ARCH"
mkdir -p "$OUT"

# One line: it is expanded inside the remote sh -c script.
SRCS="App DropZone GitHub HttpClient Jobs JsonValue MainWindow MakePackageWindow PackageTools Settings Strings"
CXXFLAGS="-O2 -Wall -Wno-multichar -std=c++17"

# Compile, link, then attach the resources with the build tree's host rc/xres.
# $CXX, $INC, $LIBS, $RC and $XRES are set by the caller inside the container.
BUILD='
	for s in '"$SRCS"'; do
		$CXX -c src/$s.cpp '"$CXXFLAGS"' -Isrc $INC -o $s.o >&2
	done
	$CXX -o RInstallWizard *.o -Xlinker -soname=_APP_ $LIBS >&2
	$RC -o app.rsrc src/app.rdef >&2
	$XRES -o RInstallWizard app.rsrc >&2
	tar cf - RInstallWizard'

case "$ARCH" in
arm64)
	CONTAINER="${ARM64_CONTAINER:-haiku-builder}"
	GEN="${ARM64_GENERATED:-/root/renku-arm64-work/generated.arm64}"
	P="$GEN/objects/haiku/arm64/packaging/packages_build/minimum"
	WORK="/tmp/riw-cross-$$"
	( cd "$HERE" && tar cf - src ) \
		| docker exec -i "$CONTAINER" sh -c "
			set -e
			mkdir -p $WORK/sysroot/boot/system $WORK/lib && cd $WORK && tar xf -
			ln -sfn $P/hpkg_-haiku_devel.hpkg/contents/develop $WORK/sysroot/boot/system/develop
			SYSLIBS=\$(ls -d $GEN/build_packages/gcc_syslibs-*-arm64/lib | head -1)
			ln -sfn \$SYSLIBS/libstdc++.so.6 $WORK/lib/libstdc++.so
			ln -sfn \$SYSLIBS/libgcc_s.so.1 $WORK/lib/libgcc_s.so
			SSL=\$(ls -d $GEN/build_packages/openssl3-*-arm64 | head -1)
			CXX=\"$GEN/cross-tools-arm64/bin/aarch64-unknown-haiku-g++ --sysroot=$WORK/sysroot\"
			INC=\"-I\$SSL/develop/headers\"
			LIBS=\"-L$P/hpkg_-haiku.hpkg/contents/lib -L\$SSL/develop/lib -L$WORK/lib -lbe -ltracker -lbnetapi -lnetwork -lssl -lcrypto -lstdc++\"
			TOOLS=$GEN/objects/linux/arm64/release/tools
			RC=\$TOOLS/rc/rc; [ -x \$RC ] || RC=\$TOOLS/rc
			XRES=\$TOOLS/xres
			export LD_LIBRARY_PATH=$GEN/objects/linux/lib
			$BUILD
			rm -rf $WORK" \
		| tar xf - -C "$OUT"
	;;
x86)
	# Runs on another machine over ssh, so the build steps travel as a script
	# file inside the tar stream rather than through two layers of quoting.
	HOST="${X86_HOST:-rainygirl@MacMiniM4}"
	CONTAINER="${X86_CONTAINER:-vaio-p-builder}"
	GEN="${X86_GENERATED:-/vaio-p/work/generated.x86_gcc2h}"
	P="$GEN/objects/haiku/x86_gcc2/packaging/packages_build/regular"
	STAGE="$(mktemp -d)"
	cp -R "$HERE/src" "$STAGE/"
	cat > "$STAGE/build.sh" <<SCRIPT
set -e
W=\$(pwd)
TOOLS=\$(ls -d $GEN/objects/linux/*/release/tools | head -1)
RC=\$TOOLS/rc/rc; [ -x \$RC ] || RC=\$TOOLS/rc
XRES=\$TOOLS/xres
export LD_LIBRARY_PATH=$GEN/objects/linux/lib
mkdir -p \$W/sysroot/boot/system \$W/ssl
for p in openssl3_x86_devel openssl3_x86 gcc_x86_syslibs_devel gcc_x86_syslibs; do
	PKG=\$TOOLS/package/package; [ -x \$PKG ] || PKG=\$TOOLS/package
	\$PKG extract -C \$W/ssl \$(ls $GEN/download/\$p-*.hpkg | head -1) >&2
done
# develop/lib/x86 holds relative links into lib/x86 of another package, so
# the sysroot is real directories with both in their usual places.
S=\$W/sysroot/boot/system
mkdir -p \$S/develop/lib
ln -sfn $P/hpkg_-haiku_devel.hpkg/contents/develop/headers \$S/develop/headers
cp -a $P/hpkg_-haiku_x86_devel.hpkg/contents/develop/lib/x86 \$S/develop/lib/
ln -sfn $P/hpkg_-haiku_x86.hpkg/contents/lib \$S/lib
X86DEV=$P/hpkg_-haiku_x86_devel.hpkg/contents/develop
CXX="$GEN/cross-tools-x86/bin/i586-pc-haiku-g++ --sysroot=\$W/sysroot"
INC="-I\$X86DEV/headers -I\$W/ssl/develop/headers/x86"
LIBS="-B\$S/develop/lib/x86/ -L\$S/develop/lib/x86 -L\$W/ssl/develop/lib/x86 -L\$W/ssl/lib/x86 -lbe -ltracker -lbnetapi -lnetwork -lssl -lcrypto -lstdc++ -lgcc_s"
$BUILD
SCRIPT
	( cd "$STAGE" && tar cf - src build.sh ) \
		| ssh "$HOST" "/usr/local/bin/docker --context desktop-linux exec -i $CONTAINER sh -c 'W=/tmp/riw-cross-\$\$; mkdir -p \$W && cd \$W && tar xf - && sh build.sh; rc=\$?; rm -rf \$W; exit \$rc'" \
		| tar xf - -C "$OUT"
	rm -rf "$STAGE"
	;;
x86_64)
	# The image has Haiku's headers and kits but not OpenSSL (HaikuPorts'
	# openssl3 packages are fetched, as rworldradio does) and no rc/xres:
	# the resources are attached afterwards with the arm64 tree's host tools,
	# which handle any ELF.
	SSL_URL=https://eu.hpkg.haiku-os.org/haikuports/master/x86_64/current/packages
	SSL_VERSION="${SSL_VERSION:-3.5.8-2}"
	( cd "$HERE" && tar cf - src ) \
		| docker run --rm -i --platform linux/amd64 haiku/cross-compiler:x86_64-r1beta4 sh -c "
			set -e
			mkdir -p /work/ssl && cd /work && tar xf -
			for p in openssl3 openssl3_devel; do
				wget -q -O \$p.hpkg $SSL_URL/\$p-$SSL_VERSION-x86_64.hpkg
				package extract -C /work/ssl \$p.hpkg >&2
			done
			for s in $SRCS; do
				x86_64-unknown-haiku-g++ -c src/\$s.cpp $CXXFLAGS -Isrc \
					-I/work/ssl/develop/headers -o \$s.o >&2
			done
			x86_64-unknown-haiku-g++ -o RInstallWizard *.o -Xlinker -soname=_APP_ \
				-L/work/ssl/develop/lib -L/work/ssl/lib \
				-lbe -ltracker -lbnetapi -lnetwork -lssl -lcrypto -lstdc++ >&2
			tar cf - RInstallWizard" \
		| tar xf - -C "$OUT"
	CONTAINER="${ARM64_CONTAINER:-haiku-builder}"
	GEN="${ARM64_GENERATED:-/root/renku-arm64-work/generated.arm64}"
	( cd "$HERE" && tar cf - src/app.rdef -C "$OUT" RInstallWizard ) \
		| docker exec -i "$CONTAINER" sh -c "
			set -e
			W=/tmp/riw-res-\$\$; mkdir -p \$W && cd \$W && tar xf -
			T=$GEN/objects/linux/arm64/release/tools
			export LD_LIBRARY_PATH=$GEN/objects/linux/lib
			\$T/rc/rc -o app.rsrc src/app.rdef >&2
			\$T/xres -o RInstallWizard app.rsrc >&2
			tar cf - RInstallWizard; rm -rf \$W" \
		| tar xf - -C "$OUT"
	;;
*)
	echo "usage: $0 arm64|x86|x86_64" >&2
	exit 1
	;;
esac

file "$OUT/RInstallWizard"
