#include "MainWindow.h"
#include "Strings.h"

#include "Common.h"
#include "DropZone.h"
#include "Jobs.h"
#include "MakePackageWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <string.h>
#include <strings.h>

#include <string>
#include <vector>

namespace {

bool
IsPackageFile(const BEntry& entry)
{
	char name[B_FILE_NAME_LENGTH];
	if (entry.GetName(name) != B_OK || entry.IsDirectory())
		return false;
	size_t length = strlen(name);
	if (length > 5 && strcasecmp(name + length - 5, ".hpkg") == 0)
		return true;
	BNode node(&entry);
	BNodeInfo info(&node);
	char type[B_MIME_TYPE_LENGTH];
	return info.GetType(type) == B_OK
		&& strcmp(type, "application/x-vnd.haiku-package") == 0;
}

} // namespace


MainWindow::MainWindow()
	:
	BWindow(BRect(0, 0, 520, 480), APP_NAME, B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE),
	fBusy(false)
{
	fDropZone = new DropZone();

	fUrlControl = new BTextControl("url", "GitHub:", "",
		new BMessage(kMsgInstallUrl));
	fUrlControl->SetModificationMessage(new BMessage(kMsgUrlChanged));
	fUrlControl->SetToolTip(T("A repository (https://github.com/owner/name), "
		"one of its releases, or a link to an .hpkg file"));

	fInstallButton = new BButton("install", T("Install"),
		new BMessage(kMsgInstallUrl));
	fInstallButton->SetEnabled(false);

	BStringView* hint = new BStringView("hint",
		T("The newest release with a package for this computer is installed."));
	BFont small(be_plain_font);
	small.SetSize(small.Size() * 0.85f);
	hint->SetFont(&small);
	hint->SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR),
		B_LIGHTEN_1_TINT));

	fProgress = new BStatusBar("progress");
	fProgress->SetBarHeight(12);
	fProgress->Hide();

	fLogView = new BTextView("log");
	fLogView->MakeEditable(false);
	fLogView->SetStylable(false);
	fLogView->SetWordWrap(true);
	BScrollView* logScroll = new BScrollView("log scroll", fLogView, 0, false,
		true);
	logScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 90));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fDropZone, 3)
		.AddGroup(B_HORIZONTAL)
			.Add(fUrlControl, 1)
			.Add(fInstallButton)
		.End()
		.Add(hint)
		.Add(fProgress)
		.Add(logScroll, 2);

	fUrlControl->MakeFocus(true);
	SetDefaultButton(fInstallButton);
	CenterOnScreen();
}


void
MainWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && message->HasRef("refs")) {
		HandleRefs(message);
		return;
	}

	switch (message->what) {
		case B_REFS_RECEIVED:
		case B_SIMPLE_DATA:
			HandleRefs(message);
			break;

		case kMsgUrlChanged:
			fInstallButton->SetEnabled(!fBusy
				&& fUrlControl->Text()[0] != '\0');
			break;

		case kMsgInstallUrl:
			InstallFromText(fUrlControl->Text());
			break;

		case kMsgBuild:
			_StartBuild(message);
			break;

		case kMsgLog:
		{
			const char* text;
			if (message->FindString("text", &text) == B_OK)
				_Log(text);
			break;
		}

		case kMsgProgress:
		{
			int64 done = message->GetInt64("done", 0);
			int64 total = message->GetInt64("total", 0);
			const char* label = message->GetString("label", "");
			if (total == 0 && label[0] == '\0') {
				if (!fProgress->IsHidden()) {
					fProgress->Hide();
					// The log view grows into the space and its relayout
					// scrolls it back to the top; scroll again after it.
					PostMessage(kMsgScrollLog);
				}
				break;
			}
			if (fProgress->IsHidden()) {
				fProgress->Show();
				PostMessage(kMsgScrollLog);
			}
			if (total > 0) {
				fProgress->SetMaxValue(100);
				fProgress->SetTo(100.0f * done / total, label);
			} else {
				// No size known: the label alone says what is going on.
				fProgress->SetMaxValue(100);
				fProgress->SetTo(0, label);
			}
			break;
		}

		case kMsgScrollLog:
			_ScrollLogToEnd();
			break;

		case kMsgJobFinished:
			_SetBusy(false);
			_ScrollLogToEnd();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
MainWindow::QuitRequested()
{
	if (fBusy) {
		BAlert* alert = new BAlert(APP_NAME,
			T("Something is still being installed. Quit anyway?"),
			T("Keep working"), T("Quit"), NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0)
			return false;
	}
	return true;
}


void
MainWindow::HandleRefs(const BMessage* message)
{
	if (fBusy) {
		_Log(T("Busy with the last one; drop it again when this is done."));
		return;
	}

	std::vector<std::string> packages;
	std::vector<entry_ref> others;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref, true);
		BPath path;
		if (entry.GetPath(&path) != B_OK)
			continue;
		if (IsPackageFile(entry))
			packages.push_back(path.Path());
		else {
			entry_ref resolved;
			entry.GetRef(&resolved);
			others.push_back(resolved);
		}
	}

	if (!packages.empty()) {
		if (!others.empty())
			_Log(T("Only the packages are installed; the other items were left."));
		_SetBusy(true);
		StartInstallFiles(BMessenger(this), packages);
		return;
	}
	if (others.empty())
		return;

	// One program, and optionally a folder with its source.
	entry_ref app = others[0];
	entry_ref source;
	bool hasSource = false;
	if (others.size() >= 2) {
		BPath first(&others[0]);
		BPath second(&others[1]);
		bool firstRuns = !FindMainExecutable(first.Path()).empty();
		bool secondRuns = !FindMainExecutable(second.Path()).empty();
		if (!firstRuns && secondRuns) {
			app = others[1];
			source = others[0];
		} else
			source = others[1];
		hasSource = BEntry(&source).IsDirectory();
	}

	MakePackageWindow* window = new MakePackageWindow(BMessenger(this), app,
		hasSource ? &source : NULL);
	window->Show();
}


void
MainWindow::InstallFromText(const char* text)
{
	if (fBusy || text == NULL || text[0] == '\0')
		return;
	_SetBusy(true);
	StartInstallUrl(BMessenger(this), text);
}


void
MainWindow::_StartBuild(const BMessage* message)
{
	if (fBusy) {
		BAlert* alert = new BAlert(APP_NAME,
			T("Something else is being installed. Try again when it is done."),
			T("OK"));
		alert->Go(NULL);
		return;
	}
	BuildRequest request;
	request.appPath = message->GetString("app", "");
	request.sourcePath = message->GetString("source", "");
	request.displayName = message->GetString("display", "");
	request.packageName = message->GetString("package", "");
	request.version = message->GetString("version", "1.0");
	request.summary = message->GetString("summary", "");
	request.authorName = message->GetString("author", "");
	request.email = message->GetString("email", "");
	request.sourceUrl = message->GetString("url", "");
	request.license = message->GetString("license", "MIT");
	request.outputDir = message->GetString("output", "/boot/home/Desktop");
	request.kind = (PackageKind)message->GetInt32("kind", kKindApplication);
	bool install = message->GetBool("install", false);
	InstallTarget target = (InstallTarget)message->GetInt32("target",
		kInstallSystem);

	_SetBusy(true);
	StartBuild(BMessenger(this), request, install, target);
}


void
MainWindow::_SetBusy(bool busy)
{
	fBusy = busy;
	fDropZone->SetBusy(busy);
	fUrlControl->SetEnabled(!busy);
	fInstallButton->SetEnabled(!busy && fUrlControl->Text()[0] != '\0');
	if (!busy && !fProgress->IsHidden()) {
		fProgress->Hide();
		PostMessage(kMsgScrollLog);
	}
}


void
MainWindow::_Log(const char* text)
{
	// BTextView scrolls to its selection after every insert; with the caret
	// left at offset 0 that is the top. Inserting at a caret kept at the end
	// makes it follow the log instead.
	int32 end = fLogView->TextLength();
	fLogView->Select(end, end);
	if (end > 0)
		fLogView->Insert("\n");
	fLogView->Insert(text);
	_ScrollLogToEnd();
}


void
MainWindow::_ScrollLogToEnd()
{
	int32 end = fLogView->TextLength();
	fLogView->Select(end, end);
	fLogView->ScrollToSelection();
}


void
MainWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active)
		_ScrollLogToEnd();
}
