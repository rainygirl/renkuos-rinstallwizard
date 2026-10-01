#include "App.h"
#include "Strings.h"

#include "Common.h"
#include "MainWindow.h"

#include <Alert.h>
#include <Entry.h>

#include <string.h>


App::App()
	:
	BApplication(APP_SIGNATURE)
{
	// Made here, not in ReadyToRun: files dropped on the app icon arrive as
	// RefsReceived before that.
	fWindow = new MainWindow();
	fWindow->Show();
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	BMessage refs(B_REFS_RECEIVED);
	for (int32 i = 1; i < argc; i++) {
		if (strstr(argv[i], "://") != NULL || strncmp(argv[i], "github.com/", 11) == 0) {
			if (fWindow->Lock()) {
				fWindow->InstallFromText(argv[i]);
				fWindow->Unlock();
			}
			continue;
		}
		entry_ref ref;
		if (get_ref_for_path(argv[i], &ref) == B_OK)
			refs.AddRef("refs", &ref);
	}
	if (refs.HasRef("refs"))
		fWindow->PostMessage(&refs);
}


void
App::RefsReceived(BMessage* message)
{
	fWindow->PostMessage(message);
}


void
App::AboutRequested()
{
	std::string text = std::string(APP_NAME "\n\n")
		+ T("Installs Haiku packages: drop an .hpkg, or give a GitHub address.\n"
			"Drop a program instead and it makes the package for you.")
		+ "\n\nCopyright 2026 Lee JunHaeng. MIT License.";
	BAlert* alert = new BAlert(APP_NAME, text.c_str(), T("OK"));
	alert->Go(NULL);
}


int
main()
{
	App app;
	app.Run();
	return 0;
}
