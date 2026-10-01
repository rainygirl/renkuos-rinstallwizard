#include "Settings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

namespace {

bool
SettingsPath(BPath& path)
{
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return false;
	return path.Append("RInstallWizard") == B_OK;
}

} // namespace


void
Settings::Load()
{
	BPath path;
	if (!SettingsPath(path))
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage message;
	if (file.InitCheck() != B_OK || message.Unflatten(&file) != B_OK)
		return;
	const char* value;
	if (message.FindString("author", &value) == B_OK)
		authorName = value;
	if (message.FindString("email", &value) == B_OK)
		email = value;
	if (message.FindString("license", &value) == B_OK)
		license = value;
}


void
Settings::Save() const
{
	BPath path;
	if (!SettingsPath(path))
		return;
	BMessage message;
	message.AddString("author", authorName.c_str());
	message.AddString("email", email.c_str());
	message.AddString("license", license.c_str());
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		message.Flatten(&file);
}
