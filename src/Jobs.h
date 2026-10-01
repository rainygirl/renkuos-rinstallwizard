#ifndef RIW_JOBS_H
#define RIW_JOBS_H

#include "PackageTools.h"

#include <Messenger.h>

#include <string>
#include <vector>

// Each job runs on its own thread, reports to the main window (kMsgLog,
// kMsgProgress) and ends with kMsgJobFinished. Questions are asked with
// BAlert::Go(), which blocks only the job's thread.

// Install .hpkg files that are already on disk.
void StartInstallFiles(const BMessenger& window,
	const std::vector<std::string>& paths);

// Download from a GitHub repository / release / .hpkg link, then install.
void StartInstallUrl(const BMessenger& window, const std::string& input);

// Make a package from a dropped app; install it if asked to.
void StartBuild(const BMessenger& window, const BuildRequest& request,
	bool install, InstallTarget target);

#endif
