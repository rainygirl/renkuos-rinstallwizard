#ifndef RIW_MAKE_PACKAGE_WINDOW_H
#define RIW_MAKE_PACKAGE_WINDOW_H

#include <Entry.h>
#include <Messenger.h>
#include <Window.h>

#include <string>

class BButton;
class BFilePanel;
class BMenuField;
class BStringView;
class BTextControl;

// Asks the few things a package needs (name, version, who made it, where the
// source lives, how to install it), then hands them to the main window,
// which builds the package and shows the progress.
class MakePackageWindow : public BWindow {
public:
	MakePackageWindow(const BMessenger& mainWindow, const entry_ref& app,
		const entry_ref* source);
	~MakePackageWindow() override;

	void MessageReceived(BMessage* message) override;

private:
	void _SetSource(const entry_ref* ref);
	bool _Submit(bool install);
	std::string _MarkedLabel(BMenuField* field) const;

	BMessenger fMainWindow;
	std::string fAppPath;
	std::string fSourcePath;

	BTextControl* fName;
	BTextControl* fPackageName;
	BTextControl* fVersion;
	BTextControl* fSummary;
	BTextControl* fAuthor;
	BTextControl* fEmail;
	BTextControl* fUrl;
	BMenuField* fLicense;
	BMenuField* fKind;
	BMenuField* fTarget;
	BStringView* fSourceLabel;
	BButton* fClearSource;
	BTextControl* fOutput;
	BFilePanel* fSourcePanel;

	bool fPackageNameEdited;
};

#endif
