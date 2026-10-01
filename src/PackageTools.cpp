#include "PackageTools.h"
#include "Strings.h"

#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Node.h>
#include <Path.h>
#include <fs_attr.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

namespace {

std::string
Trim(const std::string& text)
{
	size_t start = text.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return std::string();
	size_t end = text.find_last_not_of(" \t\r\n");
	return text.substr(start, end - start + 1);
}


std::string
BaseName(const std::string& path)
{
	std::string p = path;
	while (p.size() > 1 && p[p.size() - 1] == '/')
		p.erase(p.size() - 1);
	size_t slash = p.rfind('/');
	return slash == std::string::npos ? p : p.substr(slash + 1);
}


bool
IsDirectory(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}


bool
IsExecutableFile(const std::string& path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)
		&& (st.st_mode & S_IXUSR) != 0;
}


bool
HasAttribute(const std::string& path, const char* name)
{
	BNode node(path.c_str());
	attr_info info;
	return node.InitCheck() == B_OK && node.GetAttrInfo(name, &info) == B_OK;
}


// e_machine of an ELF file, or 0.
int
ElfMachine(const std::string& path)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return 0;
	unsigned char header[20];
	size_t got = fread(header, 1, sizeof(header), file);
	fclose(file);
	if (got < sizeof(header) || memcmp(header, "\177ELF", 4) != 0)
		return 0;
	// Every Haiku target is little-endian.
	return header[18] | (header[19] << 8);
}


// The first ELF machine found under path (depth-limited).
int
FindElfMachine(const std::string& path, int depth)
{
	if (!IsDirectory(path))
		return ElfMachine(path);
	if (depth <= 0)
		return 0;
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return 0;
	int machine = 0;
	while (machine == 0) {
		dirent* entry = readdir(dir);
		if (entry == NULL)
			break;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		machine = FindElfMachine(path + "/" + entry->d_name, depth - 1);
	}
	closedir(dir);
	return machine;
}


std::string
ArchitectureFor(int machine)
{
	switch (machine) {
		case 0:
			return "any";
		case 3: {	// EM_386
			// On a gcc2 hybrid, a modern x86 build is the secondary arch.
			std::string primary = PrimaryArchitecture();
			if (primary == "x86_gcc2" || primary == "x86")
				return primary;
			return "x86";
		}
		case 62:
			return "x86_64";
		case 183:
			return "arm64";
		case 243:
			return "riscv64";
		case 40:
			return "arm";
		case 8:
			return "mipsel";
		case 21:
			return "ppc";
		case 2:
			return "sparc";
	}
	return PrimaryArchitecture();
}


// A double-quoted PackageInfo string.
std::string
Quote(const std::string& text)
{
	std::string out = "\"";
	for (size_t i = 0; i < text.size(); i++) {
		char c = text[i];
		if (c == '"' || c == '\\')
			out += '\\';
		if (c == '\n' || c == '\r' || c == '\t')
			c = ' ';
		out += c;
	}
	return out + "\"";
}


std::string
CurrentYear()
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char year[16];
	snprintf(year, sizeof(year), "%d", tm.tm_year + 1900);
	return year;
}


bool
MakeDirs(const std::string& path)
{
	return create_directory(path.c_str(), 0755) == B_OK;
}


// Copies a file or folder, attributes included, into the directory parent.
bool
CopyInto(const std::string& source, const std::string& parent,
	const LogFunc& log)
{
	if (!MakeDirs(parent))
		return false;
	std::string output;
	int status = RunCommand("copyattr -d -r " + ShellQuote(source) + " "
		+ ShellQuote(parent + "/"), &output);
	if (status != 0 && log)
		log("copyattr: " + Trim(output));
	return status == 0;
}


void
RemoveTree(const std::string& path)
{
	if (path.size() > 5 && path.compare(0, 5, "/tmp/") == 0)
		RunCommand("rm -rf " + ShellQuote(path), NULL);
}


std::string
TempDir(const std::string& tag)
{
	static int counter = 0;
	char name[128];
	snprintf(name, sizeof(name), "/tmp/RInstallWizard-%d-%d-%s",
		(int)getpid(), ++counter, tag.c_str());
	RemoveTree(name);
	MakeDirs(name);
	return name;
}


// Deskbar and Tracker read the icon and signature from attributes. Cross-built
// binaries often carry them only as resources; resattr copies them over.
void
PromoteResources(const std::string& executable)
{
	if (ElfMachine(executable) == 0)
		return;
	if (!HasAttribute(executable, "BEOS:APP_SIG")
		&& !HasAttribute(executable, "BEOS:ICON")) {
		RunCommand("resattr -O -o " + ShellQuote(executable) + " "
			+ ShellQuote(executable), NULL);
	}
	if (!HasAttribute(executable, "BEOS:TYPE")) {
		RunCommand("addattr -t mime BEOS:TYPE application/x-vnd.be-elfexecutable "
			+ ShellQuote(executable), NULL);
	}
}


bool
CreatePackage(const std::string& stage, const std::string& output,
	const LogFunc& log, std::string& error)
{
	unlink(output.c_str());
	std::string out;
	int status = RunCommand("package create -C " + ShellQuote(stage) + " "
		+ ShellQuote(output), &out, log);
	if (status != 0) {
		error = TF("Making the package failed: %1", Trim(out));
		return false;
	}
	return true;
}

} // namespace


std::string
ShellQuote(const std::string& text)
{
	std::string out = "'";
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\'')
			out += "'\\''";
		else
			out += text[i];
	}
	return out + "'";
}


namespace {

// pkgman's download progress ("repocache-2 [ 12 KiB]") is noise in a log.
bool
IsWorthLogging(const std::string& rawLine)
{
	std::string line = Trim(rawLine);
	if (line.empty())
		return false;
	if (line[line.size() - 1] == ']' && line.find(" [") != std::string::npos)
		return false;
	// Lines that are only leftovers of a redrawn progress bar.
	bool hasLetter = false;
	for (size_t i = 0; i < line.size() && !hasLetter; i++)
		hasLetter = isalpha((unsigned char)line[i]);
	return hasLetter;
}

} // namespace


int
RunCommand(const std::string& command, std::string* output, const LogFunc& log)
{
	FILE* pipe = popen((command + " 2>&1").c_str(), "r");
	if (pipe == NULL)
		return -1;
	// Progress bars redraw one line with '\r' and terminal escapes; only
	// finished lines are kept, without the escapes.
	std::string line;
	int c;
	while ((c = fgetc(pipe)) != EOF) {
		if (c == '\r') {
			line.clear();
		} else if (c == '\n') {
			if (output != NULL)
				*output += line + "\n";
			if (log && IsWorthLogging(line))
				log(Trim(line));
			line.clear();
		} else if (c == 0x1b) {
			// ESC [ parameters final-byte
			int next = fgetc(pipe);
			if (next == '[') {
				while ((next = fgetc(pipe)) != EOF && !(next >= 0x40 && next <= 0x7e))
					;
			}
		} else if (c >= 0x20 || c == '\t')
			line += (char)c;
	}
	if (!line.empty()) {
		if (output != NULL)
			*output += line;
		if (log)
			log(line);
	}
	int status = pclose(pipe);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}


std::string
PrimaryArchitecture()
{
	std::string out;
	if (RunCommand("getarch", &out) == 0 && !Trim(out).empty())
		return Trim(out);
#if defined(__x86_64__)
	return "x86_64";
#elif defined(__aarch64__)
	return "arm64";
#elif defined(__riscv)
	return "riscv64";
#else
	return "x86_gcc2";
#endif
}


std::vector<std::string>
SecondaryArchitectures()
{
	std::vector<std::string> result;
	std::string out;
	if (RunCommand("getarch -s", &out) != 0)
		return result;
	size_t pos = 0;
	while (pos < out.size()) {
		size_t end = out.find_first_of(" \n", pos);
		if (end == std::string::npos)
			end = out.size();
		std::string word = Trim(out.substr(pos, end - pos));
		if (!word.empty())
			result.push_back(word);
		pos = end + 1;
	}
	return result;
}


bool
ReadPackageInfo(const std::string& path, PackageInfo& info, std::string& error)
{
	info = PackageInfo();
	info.path = path;
	std::string out;
	if (RunCommand("package list -i " + ShellQuote(path), &out) != 0) {
		error = TF("%1 is not a package Haiku can read.", BaseName(path));
		return false;
	}
	// "\tname: foo" and so on; the first of each wins (dependencies repeat
	// some of the keys further down).
	size_t pos = 0;
	while (pos < out.size()) {
		size_t end = out.find('\n', pos);
		if (end == std::string::npos)
			end = out.size();
		std::string line = Trim(out.substr(pos, end - pos));
		pos = end + 1;
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = line.substr(0, colon);
		std::string value = Trim(line.substr(colon + 1));
		if (key == "name" && info.name.empty())
			info.name = value;
		else if (key == "version" && info.version.empty())
			info.version = value;
		else if (key == "summary" && info.summary.empty())
			info.summary = value;
		else if (key == "architecture" && info.architecture.empty())
			info.architecture = value;
	}
	if (info.name.empty()) {
		error = TF("%1 has no package name.", BaseName(path));
		return false;
	}

	out.clear();
	if (RunCommand("package list -p " + ShellQuote(path), &out) == 0) {
		pos = 0;
		while (pos < out.size()) {
			size_t end = out.find('\n', pos);
			if (end == std::string::npos)
				end = out.size();
			std::string entry = Trim(out.substr(pos, end - pos));
			pos = end + 1;
			if (entry.compare(0, 2, "./") == 0)
				entry.erase(0, 2);
			while (!entry.empty() && entry[entry.size() - 1] == '/')
				entry.erase(entry.size() - 1);
			if (!entry.empty() && entry != ".PackageInfo")
				info.entries.push_back(entry);
		}
	}
	return true;
}


std::string
ArchitectureProblem(const std::string& architecture)
{
	if (architecture.empty() || architecture == "any")
		return std::string();
	if (architecture == "source")
		return T("This is a source package: it holds source code, not a program.");
	std::string primary = PrimaryArchitecture();
	if (architecture == primary)
		return std::string();
	std::vector<std::string> secondary = SecondaryArchitectures();
	if (std::find(secondary.begin(), secondary.end(), architecture)
			!= secondary.end())
		return std::string();
	return TF("This package is for %1, and this system is %2.", architecture,
		primary);
}


std::string
InstallRoot(InstallTarget target)
{
	directory_which which = target == kInstallHome
		? B_USER_CONFIG_DIRECTORY : B_SYSTEM_DIRECTORY;
	BPath path;
	if (find_directory(which, &path) != B_OK)
		return target == kInstallHome ? "/boot/home/config" : "/boot/system";
	return path.Path();
}


bool
IsInstalled(const PackageInfo& info, InstallTarget target)
{
	std::string active = InstallRoot(target) + "/packages/" + BaseName(info.path);
	struct stat st;
	return stat(active.c_str(), &st) == 0;
}


bool
InstallPackages(const std::vector<std::string>& paths, InstallTarget target,
	const LogFunc& log, std::string& error)
{
	std::string command = "pkgman install -y";
	if (target == kInstallHome)
		command += " -H";
	for (size_t i = 0; i < paths.size(); i++)
		command += " " + ShellQuote(paths[i]);
	if (log)
		log("$ " + command);
	std::string out;
	int status = RunCommand(command, &out, log);
	if (status != 0) {
		// pkgman's last words are its reason.
		std::string reason = Trim(out);
		size_t cut = reason.rfind('\n');
		if (cut != std::string::npos)
			reason = reason.substr(cut + 1);
		error = TF("pkgman could not install it: %1", reason);
		return false;
	}
	return true;
}


LaunchList
LaunchTargets(const PackageInfo& info, InstallTarget target)
{
	LaunchList result;
	std::string root = InstallRoot(target);

	// Deskbar menu entries are what the package itself chose to show.
	for (size_t i = 0; i < info.entries.size(); i++) {
		const std::string& entry = info.entries[i];
		if (entry.compare(0, 18, "data/deskbar/menu/") != 0)
			continue;
		std::string installed = root + "/" + entry;
		BEntry link(installed.c_str());
		if (!link.IsSymLink())
			continue;
		BEntry resolved(installed.c_str(), true);
		BPath path;
		if (!resolved.Exists() || resolved.IsDirectory()
			|| resolved.GetPath(&path) != B_OK)
			continue;
		result.push_back(std::make_pair(BaseName(entry), std::string(path.Path())));
	}
	if (!result.empty())
		return result;

	// Otherwise its applications and preferences, one level down.
	for (size_t i = 0; i < info.entries.size(); i++) {
		const std::string& entry = info.entries[i];
		bool app = entry.compare(0, 5, "apps/") == 0;
		bool pref = entry.compare(0, 12, "preferences/") == 0;
		if (!app && !pref)
			continue;
		std::string rest = entry.substr(app ? 5 : 12);
		if (rest.empty() || rest.find('/') != std::string::npos)
			continue;
		std::string installed = root + "/" + entry;
		std::string executable = FindMainExecutable(installed);
		if (!executable.empty())
			result.push_back(std::make_pair(rest, executable));
	}
	return result;
}


int
AddDesktopLinks(const LaunchList& targets, const LogFunc& log)
{
	BPath desktopPath;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktopPath) != B_OK)
		return 0;
	BDirectory desktop(desktopPath.Path());
	int made = 0;
	for (size_t i = 0; i < targets.size(); i++) {
		const std::string& name = targets[i].first;
		if (desktop.Contains(name.c_str())) {
			if (log)
				log(TF("Desktop already has \"%1\"; left as it is.", name));
			continue;
		}
		if (desktop.CreateSymLink(name.c_str(), targets[i].second.c_str(),
				NULL) == B_OK) {
			made++;
			if (log)
				log(TF("Added \"%1\" to the Desktop.", name));
		}
	}
	return made;
}


// #pragma mark - making packages


std::string
SanitizePackageName(const std::string& name)
{
	std::string out;
	for (size_t i = 0; i < name.size(); i++) {
		unsigned char c = name[i];
		if (c >= 'A' && c <= 'Z')
			out += (char)(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			out += (char)c;
		else if (!out.empty() && out[out.size() - 1] != '_')
			out += '_';
	}
	while (!out.empty() && out[out.size() - 1] == '_')
		out.erase(out.size() - 1);
	while (!out.empty() && !(out[0] >= 'a' && out[0] <= 'z'))
		out.erase(0, 1);
	return out;
}


bool
IsValidPackageName(const std::string& name)
{
	return !name.empty() && SanitizePackageName(name) == name;
}


bool
IsValidVersion(const std::string& version)
{
	if (version.empty() || !isalnum((unsigned char)version[0]))
		return false;
	for (size_t i = 0; i < version.size(); i++) {
		unsigned char c = version[i];
		if (!isalnum(c) && c != '.' && c != '_' && c != '~')
			return false;
	}
	return true;
}


std::vector<std::string>
KnownLicenses()
{
	std::vector<std::string> result;
	BPath path;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &path) == B_OK) {
		path.Append("licenses");
		DIR* dir = opendir(path.Path());
		if (dir != NULL) {
			while (dirent* entry = readdir(dir)) {
				if (entry->d_name[0] != '.')
					result.push_back(entry->d_name);
			}
			closedir(dir);
		}
	}
	if (result.empty()) {
		result.push_back("MIT");
		result.push_back("GNU GPL v2");
		result.push_back("GNU GPL v3");
		result.push_back("GNU LGPL v2.1");
		result.push_back("BSD (3-clause)");
		result.push_back("Apache v2");
	}
	std::sort(result.begin(), result.end());
	return result;
}


std::string
FindMainExecutable(const std::string& path)
{
	if (!IsDirectory(path))
		return IsExecutableFile(path) ? path : std::string();

	std::string folder = BaseName(path);
	std::string sameName;
	std::string application;
	std::string anyExecutable;
	DIR* dir = opendir(path.c_str());
	if (dir == NULL)
		return std::string();
	while (dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.')
			continue;
		std::string child = path + "/" + entry->d_name;
		if (!IsExecutableFile(child))
			continue;
		if (strcasecmp(entry->d_name, folder.c_str()) == 0)
			sameName = child;
		else if (application.empty() && HasAttribute(child, "BEOS:APP_SIG"))
			application = child;
		else if (anyExecutable.empty() && ElfMachine(child) != 0)
			anyExecutable = child;
	}
	closedir(dir);
	if (!sameName.empty())
		return sameName;
	if (!application.empty())
		return application;
	return anyExecutable;
}


bool
BuildPackages(const BuildRequest& request, std::vector<std::string>& outFiles,
	const LogFunc& log, std::string& error)
{
	outFiles.clear();
	bool isFolder = IsDirectory(request.appPath);
	std::string itemName = BaseName(request.appPath);
	std::string main = FindMainExecutable(request.appPath);
	if (main.empty() && request.kind != kKindCommandLine) {
		error = TF("There is no program in \"%1\": no file in it is marked "
			"executable.", itemName);
		return false;
	}
	std::string architecture
		= ArchitectureFor(FindElfMachine(request.appPath, 3));
	if (log)
		log(TF("Architecture: %1", architecture));

	std::string stage = TempDir("package");

	// Where the program goes, and what Deskbar points at.
	std::string base;
	std::string menu;
	switch (request.kind) {
		case kKindApplication:
			base = "apps";
			menu = "Applications";
			break;
		case kKindPreference:
			base = "preferences";
			menu = "Preferences";
			break;
		case kKindCommandLine:
			base = isFolder ? "apps" : "bin";
			break;
	}
	if (log)
		log(TF("Copying %1 ...", itemName));
	if (!CopyInto(request.appPath, stage + "/" + base, log)) {
		error = TF("Could not copy \"%1\".", itemName);
		RemoveTree(stage);
		return false;
	}

	std::string mainRelative = base + "/" + itemName;
	if (isFolder && !main.empty())
		mainRelative += "/" + BaseName(main);
	if (!main.empty()) {
		std::string staged = stage + "/" + mainRelative;
		chmod(staged.c_str(), 0755);
		PromoteResources(staged);
	}

	std::string provides = "\t" + request.packageName + " = " + request.version
		+ "\n";
	if (!menu.empty() && !main.empty()) {
		std::string menuDir = stage + "/data/deskbar/menu/" + menu;
		MakeDirs(menuDir);
		std::string link = "../../../../" + mainRelative;
		if (symlink(link.c_str(),
				(menuDir + "/" + request.displayName).c_str()) != 0) {
			error = T("Could not make the Deskbar entry.");
			RemoveTree(stage);
			return false;
		}
		provides += "\tapp:" + request.packageName + " = " + request.version
			+ "\n";
	} else if (request.kind == kKindCommandLine && !main.empty()) {
		std::string command = BaseName(main);
		if (isFolder) {
			MakeDirs(stage + "/bin");
			std::string link = "../" + mainRelative;
			symlink(link.c_str(), (stage + "/bin/" + command).c_str());
		}
		provides += "\tcmd:" + SanitizePackageName(command) + " = "
			+ request.version + "\n";
	}

	std::string packager = request.authorName;
	if (!request.email.empty())
		packager += " <" + request.email + ">";
	std::string fullVersion = request.version + "-1";

	std::string info;
	info += "name\t\t\t" + request.packageName + "\n";
	info += "version\t\t\t" + fullVersion + "\n";
	info += "architecture\t" + architecture + "\n";
	info += "summary\t\t\t" + Quote(request.summary) + "\n";
	info += "description\t\t" + Quote(request.summary) + "\n";
	info += "packager\t\t" + Quote(packager) + "\n";
	info += "vendor\t\t\t" + Quote(request.authorName) + "\n";
	info += "copyrights\t\t{ " + Quote(CurrentYear() + " " + request.authorName)
		+ " }\n";
	info += "licenses\t\t{ " + Quote(request.license) + " }\n";
	if (!request.sourceUrl.empty()) {
		info += "urls\t\t\t{ " + Quote(request.sourceUrl) + " }\n";
		info += "source-urls\t\t{ " + Quote(request.sourceUrl) + " }\n";
	}
	info += "provides {\n" + provides + "}\n";
	info += "requires {\n\thaiku\n}\n";

	FILE* file = fopen((stage + "/.PackageInfo").c_str(), "w");
	if (file == NULL || fputs(info.c_str(), file) < 0) {
		if (file != NULL)
			fclose(file);
		error = T("Could not write the package description.");
		RemoveTree(stage);
		return false;
	}
	fclose(file);

	std::string output = request.outputDir + "/" + request.packageName + "-"
		+ fullVersion + "-" + architecture + ".hpkg";
	if (log)
		log(TF("Creating %1 ...", BaseName(output)));
	bool ok = CreatePackage(stage, output, log, error);
	RemoveTree(stage);
	if (!ok)
		return false;
	outFiles.push_back(output);

	if (request.sourcePath.empty())
		return true;

	// The source goes in its own package, as Haiku's own _source packages do:
	// installing the program should not install its source.
	std::string sourceName = request.packageName + "_source";
	std::string sourceStage = TempDir("source");
	std::string sources = sourceStage + "/develop/sources";
	std::string folder = request.packageName + "-" + request.version;
	if (log)
		log(T("Copying the source folder ..."));
	if (!CopyInto(request.sourcePath, sources, log)
		|| rename((sources + "/" + BaseName(request.sourcePath)).c_str(),
			(sources + "/" + folder).c_str()) != 0) {
		error = T("Could not copy the source folder.");
		RemoveTree(sourceStage);
		return false;
	}
	std::string sourceInfo;
	sourceInfo += "name\t\t\t" + sourceName + "\n";
	sourceInfo += "version\t\t\t" + fullVersion + "\n";
	sourceInfo += "architecture\tsource\n";
	sourceInfo += "summary\t\t\t" + Quote("Source code of "
		+ request.displayName) + "\n";
	sourceInfo += "description\t\t" + Quote("The source code "
		+ request.packageName + " " + request.version + " was built from.")
		+ "\n";
	sourceInfo += "packager\t\t" + Quote(packager) + "\n";
	sourceInfo += "vendor\t\t\t" + Quote(request.authorName) + "\n";
	sourceInfo += "copyrights\t\t{ " + Quote(CurrentYear() + " "
		+ request.authorName) + " }\n";
	sourceInfo += "licenses\t\t{ " + Quote(request.license) + " }\n";
	if (!request.sourceUrl.empty())
		sourceInfo += "source-urls\t\t{ " + Quote(request.sourceUrl) + " }\n";
	sourceInfo += "provides {\n\t" + sourceName + " = " + request.version
		+ "\n}\n";
	file = fopen((sourceStage + "/.PackageInfo").c_str(), "w");
	if (file == NULL) {
		error = T("Could not write the source package description.");
		RemoveTree(sourceStage);
		return false;
	}
	fputs(sourceInfo.c_str(), file);
	fclose(file);

	std::string sourceOutput = request.outputDir + "/" + sourceName + "-"
		+ fullVersion + "-source.hpkg";
	if (log)
		log(TF("Creating %1 ...", BaseName(sourceOutput)));
	ok = CreatePackage(sourceStage, sourceOutput, log, error);
	RemoveTree(sourceStage);
	if (ok)
		outFiles.push_back(sourceOutput);
	return ok;
}
