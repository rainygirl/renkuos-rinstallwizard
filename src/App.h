#ifndef RIW_APP_H
#define RIW_APP_H

#include <Application.h>

class MainWindow;

class App : public BApplication {
public:
	App();

	void ArgvReceived(int32 argc, char** argv) override;
	void RefsReceived(BMessage* message) override;
	void AboutRequested() override;

private:
	MainWindow* fWindow;
};

#endif
