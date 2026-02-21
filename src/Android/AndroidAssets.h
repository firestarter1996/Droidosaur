// ANDROID ASSET EXTRACTION
// Extracts game data from APK assets to internal storage on first run.
#pragma once

#ifdef __ANDROID__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Extract all game data files from the APK asset bundle into destDir.
// Returns true on success, false if any file could not be extracted.
// Should be called once at startup before the game opens any data files.
bool Android_ExtractAssets(const char *destDir);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
