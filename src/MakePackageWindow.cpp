#include "MakePackageWindow.h"
#include "Strings.h"

#include "Common.h"
#include "PackageTools.h"
#include "Settings.h"

#include <Alert.h>
#include <Button.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>

#include <unistd.h>

namespace {

const char* kKindLabels[] = {
	"Application (in the Deskbar menu)",
	"Preference (in the Preferences menu)",
	"Command-line tool"
};

const char* kTargetLabels[] = {
	"Everyone (system)",
	"Only me (home)"
};


BMenuField*
MakeMenuField(const char* name, const char* label, const char* const* items,
	int count, int marked)
{
	BPopUpMenu* menu = new BPopUpMenu(name);
	for (int i = 0; i < count; i++) {
		BMenuItem* item = new BMenuItem(T(items[i]), NULL);
		menu->AddItem(item);
		if (i == marked)
			item->SetMarked(true);
	}
	return new BMenuField(name, label, menu);
}


std::string
StripExtension(const std::string& name)
{
	size_t dot = name.rfind('.');
	if (dot == std::string::npos || dot == 0)
		return name;
	return name.substr(0, dot);
}


bool
IsWritableDirectory(const std::string& path)
{
	return access(path.c_str(), W_OK) == 0;
}

} // namespace


MakePackageWindow::MakePackageWindow(const BMessenger& mainWindow,
	const entry_ref& app, const entry_ref* source)
	:
	BWindow(BRect(0, 0, 480, 420), T("Make a package"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_NOT_ZOOMABLE),
	fMainWindow(mainWindow),
	fSourcePanel(NULL),
	fPackageNameEdited(false)
{
	BPath appPath(&app);
	fAppPath = appPath.Path();
	std::string itemName = appPath.Leaf();
	std::string displayName = BEntry(&app).IsDirectory() ? itemName
		: StripExtension(itemName);

	Settings settings;
	settings.Load();

	BStringView* title = new BStringView("title",
		TF("Package \"%1\"", itemName).c_str());
	BFont bold(be_bold_font);
	title->SetFont(&bold);
	BStringView* where = new BStringView("where", appPath.Path());
	where->SetTruncation(B_TRUNCATE_MIDDLE);

	fName = new BTextControl("name", T("Name:"), displayName.c_str(),
		new BMessage(kMsgNameChanged));
	fName->SetModificationMessage(new BMessage(kMsgNameChanged));
	fPackageName = new BTextControl("package", T("Package name:"),
		SanitizePackageName(displayName).c_str(), NULL);
	fPackageName->SetModificationMessage(new BMessage(kMsgPackageNameChanged));
	fVersion = new BTextControl("version", T("Version:"), "1.0", NULL);
	fSummary = new BTextControl("summary", T("Summary:"), displayName.c_str(),
		NULL);
	fAuthor = new BTextControl("author", T("Your name:"),
		settings.authorName.c_str(), NULL);
	fEmail = new BTextControl("email", T("E-mail:"), settings.email.c_str(), NULL);
	fUrl = new BTextControl("url", T("Source link:"), "", NULL);
	fUrl->SetToolTip(T("Where the source code is, e.g. a GitHub repository"));

	std::vector<std::string> licenses = KnownLicenses();
	BPopUpMenu* licenseMenu = new BPopUpMenu("license");
	bool marked = false;
	for (size_t i = 0; i < licenses.size(); i++) {
		BMenuItem* item = new BMenuItem(licenses[i].c_str(), NULL);
		licenseMenu->AddItem(item);
		if (licenses[i] == settings.license) {
			item->SetMarked(true);
			marked = true;
		}
	}
	if (!marked && licenseMenu->CountItems() > 0) {
		BMenuItem* mit = licenseMenu->FindItem("MIT");
		(mit != NULL ? mit : licenseMenu->ItemAt(0))->SetMarked(true);
	}
	fLicense = new BMenuField("license", T("License:"), licenseMenu);

	fKind = MakeMenuField("kind", T("Install as:"), kKindLabels, 3, 0);
	fTarget = MakeMenuField("target", T("Install for:"), kTargetLabels, 2, 0);

	fSourceLabel = new BStringView("source", "");
	fSourceLabel->SetTruncation(B_TRUNCATE_MIDDLE);
	fSourceLabel->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
	BButton* chooseSource = new BButton("choose source", T("Choose" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChooseSource));
	fClearSource = new BButton("clear source", T("Remove"),
		new BMessage(kMsgClearSource));

	BPath desktop;
	find_directory(B_DESKTOP_DIRECTORY, &desktop);
	std::string outputDir = desktop.Path();
	BPath parent;
	if (appPath.GetParent(&parent) == B_OK
		&& IsWritableDirectory(parent.Path())
		&& std::string(parent.Path()).compare(0, 13, "/boot/system/") != 0)
		outputDir = parent.Path();
	fOutput = new BTextControl("output", T("Save to:"), outputDir.c_str(), NULL);

	BButton* cancel = new BButton("cancel", T("Cancel"), new BMessage(kMsgCancel));
	BButton* create = new BButton("create", T("Make package"),
		new BMessage(kMsgCreate));
	BButton* createInstall = new BButton("create install",
		T("Make and install"), new BMessage(kMsgCreateInstall));

	BStringView* sourceHint = new BStringView("source hint",
		T("Drop the source folder on this window to add it as a _source package."));
	BFont small(be_plain_font);
	small.SetSize(small.Size() * 0.85f);
	sourceHint->SetFont(&small);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(title)
		.Add(where)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.AddTextControl(fName, 0, 0)
			.AddTextControl(fPackageName, 0, 1)
			.AddTextControl(fVersion, 0, 2)
			.AddTextControl(fSummary, 0, 3)
			.AddTextControl(fAuthor, 0, 4)
			.AddTextControl(fEmail, 0, 5)
			.AddTextControl(fUrl, 0, 6)
			.AddMenuField(fLicense, 0, 7)
			.AddMenuField(fKind, 0, 8)
			.AddMenuField(fTarget, 0, 9)
			.Add(new BStringView("source title", T("Source folder:")), 0, 10)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 1, 10)
				.Add(fSourceLabel, 1)
				.Add(chooseSource)
				.Add(fClearSource)
			.End()
			.Add(sourceHint, 1, 11)
			.AddTextControl(fOutput, 0, 12)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(create)
			.Add(createInstall)
		.End();

	_SetSource(source);
	SetDefaultButton(createInstall);
	fAuthor->MakeFocus(settings.authorName.empty());
	if (!settings.authorName.empty())
		fVersion->MakeFocus(true);
	CenterOnScreen();
}


MakePackageWindow::~MakePackageWindow()
{
	delete fSourcePanel;
}


void
MakePackageWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->HasRef("refs")) {
		entry_ref ref;
		if (message->FindRef("refs", &ref) == B_OK) {
			BEntry entry(&ref, true);
			if (entry.IsDirectory()) {
				entry.GetRef(&ref);
				_SetSource(&ref);
			}
		}
		return;
	}

	switch (message->what) {
		case kMsgNameChanged:
			if (!fPackageNameEdited)
				fPackageName->SetText(SanitizePackageName(fName->Text()).c_str());
			break;

		case kMsgPackageNameChanged:
			// Modification messages arrive after the fact, also for the text
			// set above, so "edited by hand" means "differs from what the
			// name would give".
			if (SanitizePackageName(fName->Text()) != fPackageName->Text())
				fPackageNameEdited = true;
			break;

		case kMsgChooseSource:
			if (fSourcePanel == NULL) {
				BMessenger self(this);
				fSourcePanel = new BFilePanel(B_OPEN_PANEL, &self, NULL,
					B_DIRECTORY_NODE, false);
				fSourcePanel->Window()->SetTitle(T("Choose the source folder"));
			}
			fSourcePanel->Show();
			break;

		case kMsgClearSource:
			_SetSource(NULL);
			break;

		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_SetSource(&ref);
			break;
		}

		case kMsgCreate:
		case kMsgCreateInstall:
			if (_Submit(message->what == kMsgCreateInstall))
				Quit();
			break;

		case kMsgCancel:
			Quit();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
MakePackageWindow::_SetSource(const entry_ref* ref)
{
	if (ref == NULL) {
		fSourcePath.clear();
		fSourceLabel->SetText(T("(none)"));
		fClearSource->SetEnabled(false);
		return;
	}
	BPath path(ref);
	fSourcePath = path.Path();
	fSourceLabel->SetText(path.Path());
	fClearSource->SetEnabled(true);
}


std::string
MakePackageWindow::_MarkedLabel(BMenuField* field) const
{
	BMenuItem* item = field->Menu()->FindMarked();
	return item != NULL ? item->Label() : std::string();
}


bool
MakePackageWindow::_Submit(bool install)
{
	std::string problem;
	std::string packageName = fPackageName->Text();
	BTextControl* culprit = NULL;
	if (std::string(fName->Text()).empty()) {
		problem = T("Give the program a name.");
		culprit = fName;
	} else if (!IsValidPackageName(packageName)) {
		problem = T("The package name may hold only small letters, digits and "
			"'_', and must start with a letter.");
		culprit = fPackageName;
	} else if (!IsValidVersion(fVersion->Text())) {
		problem = T("The version may hold letters, digits, '.', '_' and '~' "
			"(like 1.0 or 2.3.1), but no '-'.");
		culprit = fVersion;
	} else if (std::string(fAuthor->Text()).empty()) {
		problem = T("Put your name in: a package says who made it.");
		culprit = fAuthor;
	} else if (!IsWritableDirectory(fOutput->Text())) {
		problem = TF("Cannot save into %1.", fOutput->Text());
		culprit = fOutput;
	}
	if (!problem.empty()) {
		BAlert* alert = new BAlert(APP_NAME, problem.c_str(), T("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(NULL);
		if (culprit != NULL)
			culprit->MakeFocus(true);
		return false;
	}

	Settings settings;
	settings.authorName = fAuthor->Text();
	settings.email = fEmail->Text();
	settings.license = _MarkedLabel(fLicense);
	settings.Save();

	int32 kind = fKind->Menu()->IndexOf(fKind->Menu()->FindMarked());
	int32 target = fTarget->Menu()->IndexOf(fTarget->Menu()->FindMarked());
	std::string summary = fSummary->Text();
	if (summary.empty())
		summary = fName->Text();

	BMessage build(kMsgBuild);
	build.AddString("app", fAppPath.c_str());
	build.AddString("source", fSourcePath.c_str());
	build.AddString("display", fName->Text());
	build.AddString("package", packageName.c_str());
	build.AddString("version", fVersion->Text());
	build.AddString("summary", summary.c_str());
	build.AddString("author", fAuthor->Text());
	build.AddString("email", fEmail->Text());
	build.AddString("url", fUrl->Text());
	build.AddString("license", settings.license.c_str());
	build.AddString("output", fOutput->Text());
	build.AddInt32("kind", kind < 0 ? kKindApplication : kind);
	build.AddInt32("target", target == 1 ? kInstallHome : kInstallSystem);
	build.AddBool("install", install);
	fMainWindow.SendMessage(&build);
	return true;
}
