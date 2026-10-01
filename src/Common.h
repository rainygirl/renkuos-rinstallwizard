#ifndef RIW_COMMON_H
#define RIW_COMMON_H

#include <SupportDefs.h>

#define APP_SIGNATURE "application/x-vnd.rainygirl-RInstallWizard"
#define APP_NAME "R InstallWizard"

enum {
	// worker -> main window
	kMsgLog = 'Rlog',			// "text"
	kMsgProgress = 'Rprg',		// "done", "total" (int64), "label"
	kMsgJobFinished = 'Rfin',	// "ok" (bool)

	// main window
	kMsgInstallUrl = 'Rurl',
	kMsgUrlChanged = 'Rutx',
	kMsgScrollLog = 'Rscl',

	// make-package window -> main window
	kMsgBuild = 'Rbld',

	// make-package window
	kMsgNameChanged = 'Rnam',
	kMsgPackageNameChanged = 'Rpkn',
	kMsgChooseSource = 'Rsrc',
	kMsgClearSource = 'Rsrx',
	kMsgCreate = 'Rcrt',
	kMsgCreateInstall = 'Rcri',
	kMsgCancel = 'Rcan'
};

#endif
