#include "GitHub.h"
#include "Strings.h"

#include "HttpClient.h"
#include "JsonValue.h"
#include "PackageTools.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <stdexcept>

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


bool
EndsWith(const std::string& text, const char* suffix)
{
	size_t n = strlen(suffix);
	return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}


std::vector<std::string>
Split(const std::string& text, char separator)
{
	std::vector<std::string> parts;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t end = text.find(separator, pos);
		if (end == std::string::npos)
			end = text.size();
		if (end > pos)
			parts.push_back(text.substr(pos, end - pos));
		pos = end + 1;
	}
	return parts;
}


// The architecture an .hpkg file name ends with: "foo-1.0-1-x86_64.hpkg".
std::string
FileArchitecture(const std::string& fileName)
{
	std::string stem = fileName.substr(0, fileName.size() - 5);
	size_t dash = stem.rfind('-');
	return dash == std::string::npos ? std::string() : stem.substr(dash + 1);
}


bool
IsAuxiliary(const std::string& fileName)
{
	return fileName.find("_source-") != std::string::npos
		|| fileName.find("_debuginfo-") != std::string::npos
		|| fileName.find("_devel-") != std::string::npos;
}


// The .hpkg assets of one release that fit this machine, best first: the
// primary architecture, then "any", then secondary architectures.
std::vector<RemotePackage>
FittingAssets(const JsonValue& release)
{
	std::string primary = PrimaryArchitecture();
	std::vector<std::string> secondary = SecondaryArchitectures();
	std::vector<RemotePackage> preferred;
	std::vector<RemotePackage> fallback;

	const JsonValue* assets = release.Find("assets");
	if (assets == NULL || !assets->IsArray())
		return preferred;
	for (size_t i = 0; i < assets->arrayValue.size(); i++) {
		const JsonValue& asset = assets->arrayValue[i];
		const JsonValue* name = asset.Find("name");
		const JsonValue* url = asset.Find("browser_download_url");
		if (name == NULL || url == NULL)
			continue;
		RemotePackage package;
		package.fileName = name->AsString();
		package.url = url->AsString();
		const JsonValue* size = asset.Find("size");
		if (size != NULL)
			package.size = (long long)size->AsDouble(-1);
		if (!EndsWith(package.fileName, ".hpkg") || IsAuxiliary(package.fileName))
			continue;
		std::string arch = FileArchitecture(package.fileName);
		if (arch == primary || arch == "any")
			preferred.push_back(package);
		else if (std::find(secondary.begin(), secondary.end(), arch)
				!= secondary.end())
			fallback.push_back(package);
	}
	return preferred.empty() ? fallback : preferred;
}


bool
FetchJson(const std::string& url, JsonValue& value, int& status,
	std::string& error)
{
	std::vector<std::string> headers;
	headers.push_back("Accept: application/vnd.github+json");
	headers.push_back("X-GitHub-Api-Version: 2022-11-28");
	std::string body;
	if (!Http::GetString(url, headers, body, status, error))
		return false;
	try {
		value = JsonValue::Parse(body);
	} catch (const std::exception& e) {
		error = TF("GitHub sent something unreadable: %1", e.what());
		return false;
	}
	return true;
}


std::string
ApiMessage(const JsonValue& value)
{
	const JsonValue* message = value.Find("message");
	return message != NULL ? message->AsString() : std::string();
}

} // namespace


bool
ResolvePackageUrl(const std::string& rawInput,
	std::vector<RemotePackage>& packages, std::string& description,
	std::string& error)
{
	packages.clear();
	std::string input = Trim(rawInput);
	if (input.empty()) {
		error = T("Type a GitHub address first.");
		return false;
	}

	// A direct link to a package file.
	if ((input.compare(0, 8, "https://") == 0
			|| input.compare(0, 7, "http://") == 0)
		&& EndsWith(input.substr(0, input.find('?')), ".hpkg")) {
		RemotePackage package;
		package.url = input;
		std::string path = input.substr(0, input.find('?'));
		package.fileName = path.substr(path.rfind('/') + 1);
		packages.push_back(package);
		description = package.fileName;
		return true;
	}

	std::string rest = input;
	const char* prefixes[] = { "https://", "http://", "www.", "github.com/" };
	for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		size_t n = strlen(prefixes[i]);
		if (rest.compare(0, n, prefixes[i]) == 0)
			rest.erase(0, n);
	}
	std::vector<std::string> parts = Split(rest.substr(0, rest.find('?')), '/');
	bool otherSite = input.find("://") != std::string::npos
		&& input.find("github.com/") == std::string::npos;
	if (parts.size() < 2 || otherSite) {
		error = T("That is not a GitHub repository address. Use "
			"https://github.com/owner/repository, or a link to an .hpkg file.");
		return false;
	}
	std::string owner = parts[0];
	std::string repo = parts[1];
	if (EndsWith(repo, ".git"))
		repo.erase(repo.size() - 4);
	std::string tag;
	if (parts.size() >= 5 && parts[2] == "releases" && parts[3] == "tag")
		tag = parts[4];

	std::string api = "https://api.github.com/repos/" + owner + "/" + repo;
	JsonValue value;
	int status = 0;
	if (!tag.empty()) {
		if (!FetchJson(api + "/releases/tags/" + tag, value, status, error))
			return false;
	} else if (!FetchJson(api + "/releases?per_page=30", value, status, error))
		return false;

	if (status == 404) {
		error = TF("GitHub does not know %1.", owner + "/" + repo
			+ (tag.empty() ? std::string() : " " + tag));
		return false;
	}
	if (status == 403 || status == 429) {
		error = TF("GitHub refused for now (%1). Try again in a while.",
			ApiMessage(value));
		return false;
	}
	if (status != 200) {
		char s[32];
		snprintf(s, sizeof(s), "%d", status);
		error = TF("GitHub answered %1: %2", s, ApiMessage(value));
		return false;
	}

	std::vector<const JsonValue*> releases;
	if (value.IsArray()) {
		for (size_t i = 0; i < value.arrayValue.size(); i++)
			releases.push_back(&value.arrayValue[i]);
	} else
		releases.push_back(&value);

	for (size_t i = 0; i < releases.size(); i++) {
		const JsonValue& release = *releases[i];
		const JsonValue* draft = release.Find("draft");
		if (draft != NULL && draft->type == JsonValue::Boolean && draft->boolValue)
			continue;
		packages = FittingAssets(release);
		if (packages.empty())
			continue;
		const JsonValue* name = release.Find("name");
		const JsonValue* tagName = release.Find("tag_name");
		description = owner + "/" + repo + " "
			+ (name != NULL && !name->AsString().empty() ? name->AsString()
				: tagName != NULL ? tagName->AsString() : std::string());
		return true;
	}

	if (releases.empty())
		error = TF("%1 has no releases on GitHub.", owner + "/" + repo);
	else {
		error = TF("No release of %1 has a package (.hpkg) for %2.",
			owner + "/" + repo, PrimaryArchitecture());
	}
	return false;
}
