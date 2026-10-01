#ifndef RIW_GITHUB_H
#define RIW_GITHUB_H

#include <string>
#include <vector>

struct RemotePackage {
	std::string fileName;
	std::string url;
	long long size = -1;
};

// Turns what the user typed into package downloads:
//   https://github.com/owner/repo           newest release with a fitting .hpkg
//   github.com/owner/repo/releases/tag/v1   that release
//   owner/repo                              the same as the first
//   https://.../something.hpkg              that file
// "Fitting" means built for this machine's architecture (or "any"); source,
// debug-info and development packages are left out.
bool ResolvePackageUrl(const std::string& input,
	std::vector<RemotePackage>& packages, std::string& description,
	std::string& error);

#endif
