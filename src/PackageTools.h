#ifndef RIW_PACKAGE_TOOLS_H
#define RIW_PACKAGE_TOOLS_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Everything that talks to Haiku's package tools (package, pkgman) and to
// the file system on the app's behalf. All of it blocks; it is called from
// the worker threads in Jobs.cpp, never from a window thread.

typedef std::function<void(const std::string& line)> LogFunc;

std::string ShellQuote(const std::string& text);

// Runs a shell command. Each output line goes to log (if given) and, when
// output is non-NULL, is collected there. Returns the exit status.
int RunCommand(const std::string& command, std::string* output,
	const LogFunc& log = LogFunc());

// "x86_gcc2", "x86_64", "arm64", ... as getarch prints them.
std::string PrimaryArchitecture();
std::vector<std::string> SecondaryArchitectures();

struct PackageInfo {
	std::string path;
	std::string name;
	std::string version;
	std::string summary;
	std::string architecture;
	// Paths inside the package ("apps/Foo/Foo", "data/deskbar/menu/...").
	std::vector<std::string> entries;
};

bool ReadPackageInfo(const std::string& path, PackageInfo& info,
	std::string& error);

// Empty if the package can run here, otherwise why not.
std::string ArchitectureProblem(const std::string& architecture);

enum InstallTarget {
	kInstallSystem,
	kInstallHome
};

// /boot/system or ~/config: where a package's entries end up.
std::string InstallRoot(InstallTarget target);

// The package file of that name is already active there.
bool IsInstalled(const PackageInfo& info, InstallTarget target);

bool InstallPackages(const std::vector<std::string>& paths,
	InstallTarget target, const LogFunc& log, std::string& error);

// What an installed package can be launched from: its Deskbar menu entries,
// or failing those its top-level applications. (label, absolute path).
typedef std::vector<std::pair<std::string, std::string> > LaunchList;
LaunchList LaunchTargets(const PackageInfo& info, InstallTarget target);

// Symlinks on the Desktop. Returns how many were made; an existing entry of
// the same name is left alone.
int AddDesktopLinks(const LaunchList& targets, const LogFunc& log);

// #pragma mark - making packages

enum PackageKind {
	kKindApplication,
	kKindPreference,
	kKindCommandLine
};

struct BuildRequest {
	std::string appPath;		// a file or a folder
	std::string sourcePath;		// optional source folder
	std::string displayName;	// Deskbar name
	std::string packageName;
	std::string version;		// "1.0"
	std::string summary;
	std::string authorName;
	std::string email;
	std::string sourceUrl;
	std::string license;
	std::string outputDir;
	PackageKind kind = kKindApplication;
};

// Lower-case letters, digits and '_', starting with a letter.
std::string SanitizePackageName(const std::string& name);
bool IsValidPackageName(const std::string& name);
bool IsValidVersion(const std::string& version);

// The licenses the system knows (/boot/system/data/licenses).
std::vector<std::string> KnownLicenses();

// Builds the package (and <name>_source when a source folder is given).
// outFiles gets the main package first.
bool BuildPackages(const BuildRequest& request,
	std::vector<std::string>& outFiles, const LogFunc& log,
	std::string& error);

// Main executable of a dropped item: the file itself, or inside a folder the
// executable named like the folder, else one that is an application.
std::string FindMainExecutable(const std::string& path);

#endif
