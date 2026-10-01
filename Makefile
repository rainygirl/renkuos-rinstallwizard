## Haiku Generic Build Makefile ##
# Native build on Haiku: `make`. On a gcc2 hybrid (x86_gcc2) build with the
# modern compiler: `setarch x86 make`. Cross builds: tools/cross-build.sh.
NAME = RInstallWizard
TYPE = APP
APP_MIME_SIG = application/x-vnd.rainygirl-RInstallWizard

SRCS = \
	src/App.cpp \
	src/DropZone.cpp \
	src/GitHub.cpp \
	src/HttpClient.cpp \
	src/Jobs.cpp \
	src/JsonValue.cpp \
	src/MainWindow.cpp \
	src/MakePackageWindow.cpp \
	src/PackageTools.cpp \
	src/Settings.cpp \
	src/Strings.cpp

RDEFS = src/app.rdef
RSRCS =

# be: interface kit; tracker: BFilePanel; bnetapi: BSocket/BNetworkAddress;
# network: the resolver; ssl/crypto: https (openssl3_devel).
LIBS = be tracker bnetapi network ssl crypto stdc++
LIBPATHS =
SYSTEM_INCLUDE_PATHS =
LOCAL_INCLUDE_PATHS =
OPTIMIZE := FULL
LOCALES =
DEFINES =
WARNINGS = ALL
SYMBOLS =
DEBUGGER =
COMPILER_FLAGS = -std=c++17
LINKER_FLAGS =
APP_VERSION =
DRIVER_PATH =

include /boot/system/develop/etc/makefile-engine
