#include "Jobs.h"
#include "Strings.h"

#include "Common.h"
#include "GitHub.h"
#include "HttpClient.h"

#include <Alert.h>
#include <FindDirectory.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>
#include <Directory.h>

#include <stdio.h>
#include <unistd.h>

#include <set>
#include <thread>

namespace {

class Reporter {
public:
	explicit Reporter(const BMessenger& window) : fWindow(window) {}

	void Log(const std::string& text) const
	{
		BMessage message(kMsgLog);
		message.AddString("text", text.c_str());
		fWindow.SendMessage(&message);
	}

	void Progress(off_t done, off_t total, const std::string& label) const
	{
		BMessage message(kMsgProgress);
		message.AddInt64("done", done);
		message.AddInt64("total", total);
		message.AddString("label", label.c_str());
		fWindow.SendMessage(&message);
	}

	void Finish(bool ok) const
	{
		BMessage message(kMsgJobFinished);
		message.AddBool("ok", ok);
		fWindow.SendMessage(&message);
	}

	LogFunc Logger() const
	{
		Reporter copy = *this;
		return [copy](const std::string& line) { copy.Log(line); };
	}

private:
	BMessenger fWindow;
};


int32
Ask(const std::string& text, const char* button0, const char* button1,
	const char* button2 = NULL, alert_type type = B_IDEA_ALERT)
{
	BAlert* alert = new BAlert(APP_NAME, text.c_str(), button0, button1,
		button2, B_WIDTH_AS_USUAL, B_OFFSET_SPACING, type);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go();
}


void
Tell(const std::string& text, alert_type type = B_INFO_ALERT)
{
	BAlert* alert = new BAlert(APP_NAME, text.c_str(), T("OK"), NULL, NULL,
		B_WIDTH_AS_USUAL, type);
	alert->Go();
}


std::string
BaseName(const std::string& path)
{
	size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


std::string
DescribePackages(const std::vector<PackageInfo>& infos)
{
	std::string text;
	for (size_t i = 0; i < infos.size(); i++) {
		text += infos[i].name + " " + infos[i].version;
		if (!infos[i].summary.empty())
			text += "\n" + infos[i].summary;
		text += "\n";
	}
	return text;
}


// Installs, then offers Desktop icons. ask: let the user pick system or home
// (otherwise `target` is used).
bool
InstallFlow(const Reporter& reporter, const std::vector<std::string>& paths,
	bool ask, InstallTarget target)
{
	std::vector<PackageInfo> infos;
	for (size_t i = 0; i < paths.size(); i++) {
		PackageInfo info;
		std::string error;
		if (!ReadPackageInfo(paths[i], info, error)) {
			reporter.Log(error);
			Tell(error, B_STOP_ALERT);
			return false;
		}
		std::string problem = ArchitectureProblem(info.architecture);
		if (!problem.empty()) {
			reporter.Log(info.name + ": " + problem);
			Tell(TF("%1 cannot be installed here.\n\n%2", info.name, problem),
				B_STOP_ALERT);
			return false;
		}
		infos.push_back(info);
	}
	if (infos.empty())
		return false;

	std::string what = DescribePackages(infos);
	reporter.Progress(0, 0, "");
	if (ask) {
		int32 choice = Ask(TF("Install this?\n\n%1\nWho should get it?", what),
			T("Cancel"), T("Only me"), T("Everyone"));
		if (choice == 0) {
			reporter.Log(T("Cancelled."));
			return false;
		}
		target = choice == 1 ? kInstallHome : kInstallSystem;
	}

	std::vector<std::string> pending;
	for (size_t i = 0; i < infos.size(); i++) {
		if (IsInstalled(infos[i], target))
			reporter.Log(TF("%1 is already installed.",
				infos[i].name + " " + infos[i].version));
		else
			pending.push_back(infos[i].path);
	}

	if (!pending.empty()) {
		reporter.Progress(0, -1, T("Installing ..."));
		std::string error;
		if (!InstallPackages(pending, target, reporter.Logger(), error)) {
			reporter.Log(error);
			Tell(error, B_STOP_ALERT);
			return false;
		}
		reporter.Log(T("Installed."));
	}

	LaunchList targets;
	std::set<std::string> seen;
	for (size_t i = 0; i < infos.size(); i++) {
		LaunchList list = LaunchTargets(infos[i], target);
		for (size_t j = 0; j < list.size(); j++) {
			if (seen.insert(list[j].second).second)
				targets.push_back(list[j]);
		}
	}
	if (targets.empty()) {
		reporter.Log(T("Nothing in it to put on the Desktop."));
		return true;
	}

	std::string names;
	for (size_t i = 0; i < targets.size(); i++)
		names += "  " + targets[i].first + "\n";
	reporter.Progress(0, 0, "");
	int32 choice = Ask(TF("Installed.\n\nAdd an icon to the Desktop as well?\n\n%1",
		names), T("No"), T("Add to Desktop"));
	if (choice == 1)
		AddDesktopLinks(targets, reporter.Logger());
	return true;
}


std::string
DownloadDirectory()
{
	BPath path;
	if (find_directory(B_USER_CACHE_DIRECTORY, &path, true) != B_OK)
		return "/tmp";
	path.Append("RInstallWizard");
	create_directory(path.Path(), 0755);
	return path.Path();
}

} // namespace


void
StartInstallFiles(const BMessenger& window,
	const std::vector<std::string>& paths)
{
	std::thread([window, paths]() {
		Reporter reporter(window);
		bool ok = InstallFlow(reporter, paths, true, kInstallSystem);
		reporter.Progress(0, 0, "");
		reporter.Finish(ok);
	}).detach();
}


void
StartInstallUrl(const BMessenger& window, const std::string& input)
{
	std::thread([window, input]() {
		Reporter reporter(window);
		reporter.Log(TF("Looking up %1 ...", input));
		reporter.Progress(0, -1, T("Asking GitHub ..."));

		std::vector<RemotePackage> packages;
		std::string description;
		std::string error;
		if (!ResolvePackageUrl(input, packages, description, error)) {
			reporter.Log(error);
			reporter.Progress(0, 0, "");
			Tell(error, B_STOP_ALERT);
			reporter.Finish(false);
			return;
		}
		reporter.Log(TF("Found %1", description));

		std::string directory = DownloadDirectory();
		std::vector<std::string> files;
		for (size_t i = 0; i < packages.size(); i++) {
			const RemotePackage& package = packages[i];
			std::string path = directory + "/" + package.fileName;
			std::string label = TF("Downloading %1", package.fileName);
			reporter.Log(label + " ...");
			reporter.Progress(0, package.size, label);
			off_t lastReport = -1;
			bool ok = Http::GetFile(package.url, path,
				[&reporter, &label, &lastReport](off_t done, off_t total) {
					// Every 256 KB is plenty for a progress bar.
					if (done - lastReport >= 262144 || done == total) {
						lastReport = done;
						reporter.Progress(done, total, label);
					}
				},
				error);
			if (!ok) {
				reporter.Log(TF("Download failed: %1", error));
				reporter.Progress(0, 0, "");
				Tell(TF("Could not download %1.\n\n%2", package.fileName, error),
					B_STOP_ALERT);
				for (size_t j = 0; j < files.size(); j++)
					unlink(files[j].c_str());
				reporter.Finish(false);
				return;
			}
			files.push_back(path);
		}

		bool ok = InstallFlow(reporter, files, true, kInstallSystem);
		// pkgman has its own copy now.
		for (size_t i = 0; i < files.size(); i++)
			unlink(files[i].c_str());
		reporter.Progress(0, 0, "");
		reporter.Finish(ok);
	}).detach();
}


void
StartBuild(const BMessenger& window, const BuildRequest& request,
	bool install, InstallTarget target)
{
	std::thread([window, request, install, target]() {
		Reporter reporter(window);
		reporter.Log(TF("Making a package of %1 ...", BaseName(request.appPath)));
		reporter.Progress(0, -1, T("Making the package ..."));

		std::vector<std::string> files;
		std::string error;
		if (!BuildPackages(request, files, reporter.Logger(), error)) {
			reporter.Log(error);
			reporter.Progress(0, 0, "");
			Tell(error, B_STOP_ALERT);
			reporter.Finish(false);
			return;
		}
		std::string made;
		for (size_t i = 0; i < files.size(); i++) {
			reporter.Log(TF("Saved %1", files[i]));
			made += "  " + BaseName(files[i]) + "\n";
		}

		bool ok = true;
		if (install) {
			std::vector<std::string> main(1, files[0]);
			ok = InstallFlow(reporter, main, false, target);
		} else {
			reporter.Progress(0, 0, "");
			Tell(TF("Made in %1:\n\n%2", request.outputDir, made));
		}
		reporter.Progress(0, 0, "");
		reporter.Finish(ok);
	}).detach();
}
