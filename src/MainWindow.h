#ifndef RIW_MAIN_WINDOW_H
#define RIW_MAIN_WINDOW_H

#include <Window.h>

class BButton;
class BStatusBar;
class BTextControl;
class BTextView;
class DropZone;

class MainWindow : public BWindow {
public:
	MainWindow();

	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;
	void WindowActivated(bool active) override;

	// B_REFS_RECEIVED or a drop: packages are installed, anything else opens
	// the make-a-package window.
	void HandleRefs(const BMessage* message);
	void InstallFromText(const char* text);

private:
	void _SetBusy(bool busy);
	void _Log(const char* text);
	void _ScrollLogToEnd();
	void _StartBuild(const BMessage* message);

	DropZone* fDropZone;
	BTextControl* fUrlControl;
	BButton* fInstallButton;
	BStatusBar* fProgress;
	BTextView* fLogView;
	bool fBusy;
};

#endif
