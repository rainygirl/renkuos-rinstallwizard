#ifndef RIW_SETTINGS_H
#define RIW_SETTINGS_H

#include <string>

// What the user typed once and should not have to type again.
struct Settings {
	std::string authorName;
	std::string email;
	std::string license = "MIT";

	void Load();
	void Save() const;
};

#endif
