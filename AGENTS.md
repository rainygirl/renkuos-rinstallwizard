# R InstallWizard -- development notes

A GUI installer for Haiku packages. Drop an `.hpkg` (or type a GitHub address)
and it installs it with pkgman, asking system or home first and offering a
Desktop icon after; drop a program or its folder and it makes the `.hpkg`.
The README is installation only; everything else lives here.

## Layout

```
src/App.*                BApplication; argv and Tracker "Open with" refs
src/MainWindow.*         drop zone, GitHub field, progress bar, log
src/DropZone.*           the drop target (drawing and drag highlight only)
src/MakePackageWindow.*  the form for making a package of a dropped program
src/Jobs.*               worker threads: install files, install from URL, build
src/PackageTools.*       package/pkgman/copyattr/resattr calls, .PackageInfo
src/GitHub.*             URL parsing and the releases API
src/HttpClient.*         HTTP/1.1 GET over BSocket + OpenSSL, streamed to file
src/JsonValue.*          the JSON parser from rworldradio
src/Strings.*            en/it/ko/ja translations
src/Settings.*           author name/e-mail/license remembered between runs
src/app.rdef             signature, version, file_types, vector icon
tools/make_icon.py       writes the vector_icon block (rworldradio's box)
tools/cross-build.sh     arm64 / x86 / x86_64 cross builds
```

## How things are done, and why

- **Everything blocking runs on a job thread** (`std::thread`, detached), and
  questions are `BAlert::Go()` from that thread, which blocks only it. The
  main window gets `kMsgLog`, `kMsgProgress`, `kMsgJobFinished`.
- **Installing is `pkgman install -y [-H] <file>`.** pkgman resolves the
  package's dependencies from the configured repositories, which copying into
  `packages/` would not. `-H` is the home install. Its output is streamed into
  the log with `\r`-redrawn progress lines and ANSI escapes dropped.
- **"Already installed"** is a file of the same name in `<root>/packages`;
  pkgman refuses a same-version reinstall with "Name in use", so it is skipped
  rather than attempted.
- **Desktop icons** come from the package's `data/deskbar/menu/...` symlinks
  (`package list -p`), resolved after install; failing those, its top-level
  `apps/` and `preferences/` programs. Symlinks are made in `~/Desktop`.
- **GitHub**: `api.github.com/repos/o/r/releases?per_page=30` (or
  `/releases/tags/T`), newest first; the first release with an `.hpkg` asset
  whose file-name architecture is the primary one or `any` wins (secondary
  architectures as a fallback). `_source`, `_debuginfo` and `_devel` assets are
  never chosen. Downloads go to `~/config/cache/RInstallWizard` and are
  deleted after pkgman has its copy.
- **HTTPS is OpenSSL directly**, not BUrlRequest: the gcc2 x86 libbnetapi has
  no TLS and older arm64 images neither. Certificates are verified against
  `/boot/system/data/ssl/CARootCertificates.pem` with `SSL_set1_host`; a
  failure says to check the clock (a 1970 clock is the usual cause).
- **Making a package** stages a tree under `/tmp/RInstallWizard-*`:
  `apps/<item>` (or `preferences/`, or `bin/`), a Deskbar symlink
  `../../../../<main>` (relative, so the same package works in system and
  home), and a `.PackageInfo`; then `package create -C`. Copies use
  `copyattr -d -r` to keep attributes. The main executable gets `resattr` if
  it has resources but no icon/signature attributes (cross-built binaries),
  because Deskbar and Tracker read attributes. The architecture comes from the
  ELF `e_machine` of the first binary found; no ELF means `any`. A dropped
  source folder becomes `<name>_source` (`develop/sources/<name>-<version>`,
  architecture `source`), the way Haiku's own source packages are laid out.
- **Translations are a table in the binary**, not catkeys: linkcatkeys hashes
  with plain `char` (signed on x86, unsigned on arm64) so one catkeys file does
  not link everywhere (see haiku-korean-pack's tools/catfp.cpp). The language
  is the first of en/it/ko/ja in `~/config/settings/Locale settings`
  (`"language"` strings), read directly: `BLocaleRoster` is in liblocale on
  r1beta4 and in libbe later, and the x86_64 build is linked against r1beta4.
  `RIW_LANG=it|ko|ja|en` overrides it (used for screenshots and tests).
- **The log keeps its caret at the end.** BTextView scrolls to the selection
  after every insert, so with the caret at 0 every new line jumped the view
  back to the top. Hiding or showing the progress bar relayouts the log and
  scrolls it to the top too, hence kMsgScrollLog after each.
- **Known:** while a question (BAlert) is up, the log shows its top again;
  it comes back to the end when the window is activated. Waiting for the
  window to scroll before showing the alert (a synchronous message) did not
  change it: the jump happens as the window loses activation.
- **Package-name field follows the name field** until edited by hand. The
  text control's modification message arrives asynchronously, also for text
  set by code, so "edited" means "differs from what the name gives", not a
  flag set around `SetText()`.

## Building

`make` on Haiku (`setarch x86 make` on a gcc2 hybrid) with `openssl3_devel`.
Cross builds, each leaving `dist/<arch>/RInstallWizard` with resources:

```sh
tools/cross-build.sh arm64    # this Mac's haiku-builder container
tools/cross-build.sh x86      # MacMiniM4, Docker Desktop vaio-p-builder,
                              # generated.x86_gcc2h (secondary x86 gcc13)
tools/cross-build.sh x86_64   # haiku/cross-compiler:x86_64-r1beta4 here,
                              # resources added with the arm64 tree's rc/xres
```

Traps met on the way:
- The x86 secondary `develop/lib/x86/*.so` are relative links into
  `lib/x86` of *another* package (`haiku_x86`), so the sysroot is built as
  real directories, with `-B` pointing at the x86 crt files and `-lgcc_s`
  for `_Unwind_Resume`.
- The r1beta4 x86_64 image has no `rc`/`xres`; the arm64 tree's host tools
  handle any ELF.
- Several Haiku host tools are directories (`rc/rc`), others files (`xres`).

## Publishing

- pkgman: `pkgman-repo/recipes/rinstallwizard.recipe`. Every architecture
  only copies `dist/<arch>/RInstallWizard` and runs `resattr`/`addattr`, so
  one Haiku machine packages all three (`scripts/build.sh --arch <a>`; arm64
  with `REPO_VENDOR=RENKU PACKAGER="RENKU <renku@localhost>"`). x86_gcc2 is
  the secondary-arch package `rinstallwizard_x86` (requires `haiku_x86`,
  `lib:libssl_x86`, `lib:libcrypto_x86`). 1.0.0-1 was packaged in the RENKU
  arm64 live VM, indexed with the container's `package_repo` and deployed
  with `scripts/deploy.sh x86_gcc2 x86_64 arm64`.
- GitHub: each release carries the three `.hpkg` files, which is also what
  the app's own GitHub install looks for.

## Testing

Done in QEMU on live ISOs, never on the user's machines:
- arm64: the RENKU `haiku-minimum-anyboot.iso` from the container, hvf; the
  test files on a read-only `fat:` USB drive (Deskbar > Mount > "qemu vvfat").
- x86_64: `haiku-master-hrev60177-x86_64-anyboot.iso`, TCG. Picking a
  language in the welcome window also switches the keymap.
- x86: a gcc2 hybrid ISO, `qemu-system-i386`, TCG.

Verified on arm64: drag install (system and home), Desktop icon offer and
link, GitHub API over TLS, direct .hpkg URL download with progress, making a
package of a dropped binary (Deskbar entry and Desktop icon show the app's
icon), `_source` package, the four languages. On x86_64: drag install, GitHub
over TLS, language from the Locale settings.

## AI disclosure

Parts of this program and these notes were produced with Claude.
