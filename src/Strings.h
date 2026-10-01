#ifndef RIW_STRINGS_H
#define RIW_STRINGS_H

#include <string>

// The app's own translations: English, Italian, Korean and Japanese, picked
// from the Locale preferences' language list (RIW_LANG=it|ko|ja|en
// overrides it). Haiku's catalogs would be the usual way, but linkcatkeys
// computes the catalog fingerprint with plain char, which is signed on x86
// and unsigned on arm64, so one set of catkeys does not link on every
// architecture this app is built for. A table in the binary has no such
// problem.

// The translation of an English string (the string itself if there is none).
const char* T(const char* english);

// T() with %1, %2 and %3 replaced.
std::string TF(const char* english, const std::string& a1,
	const std::string& a2 = std::string(),
	const std::string& a3 = std::string());

// "en", "it", "ko" or "ja".
const char* CurrentLanguage();

#endif
