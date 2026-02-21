// NANOSAUR ENTRY POINT
// (C) 2025 Iliyas Jorio
// This file is part of Nanosaur. https://github.com/jorio/nanosaur

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Pomme.h"
#include "PommeGraphics.h"
#include "PommeInit.h"
#include "PommeFiles.h"

#ifdef __ANDROID__
#include <android/log.h>
#include "AndroidAssets.h"
#include "GLESBridge.h"
#include "TouchControls.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#endif

extern "C"
{
	#include "game.h"

	SDL_Window* gSDLWindow = nullptr;
	WindowPtr gCoverWindow = nullptr;
	UInt32* gBackdropPixels = nullptr;
	FSSpec gDataSpec;
	int gCurrentAntialiasingLevel;
}

static fs::path FindGameData(const char* executablePath)
{
	fs::path dataPath;

	int attemptNum = 0;

#if !(__APPLE__)
	attemptNum++;		// skip macOS special case #0
#endif

	if (!executablePath)
		attemptNum = 2;

tryAgain:
	switch (attemptNum)
	{
		case 0:			// special case for macOS app bundles
			dataPath = executablePath;
			dataPath = dataPath.parent_path().parent_path() / "Resources";
			break;

		case 1:
			dataPath = executablePath;
			dataPath = dataPath.parent_path() / "Data";
			break;

		case 2:
			dataPath = "Data";
			break;

		default:
			throw std::runtime_error("Couldn't find the Data folder.");
	}

	attemptNum++;

	dataPath = dataPath.lexically_normal();

	// Set data spec -- Lets the game know where to find its asset files
	gDataSpec = Pomme::Files::HostPathToFSSpec(dataPath / "System");

	FSSpec someDataFileSpec;
	OSErr iErr = FSMakeFSSpec(gDataSpec.vRefNum, gDataSpec.parID, ":System:gamecontrollerdb.txt", &someDataFileSpec);
	if (iErr)
	{
		goto tryAgain;
	}

	return dataPath;
}

static void Boot(int argc, char** argv)
{
	SDL_SetAppMetadata(GAME_FULL_NAME, GAME_VERSION, GAME_IDENTIFIER);
#if _DEBUG
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
#else
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
#endif

#ifdef __ANDROID__
	// Set HOME so that Pomme can find the prefs directory
	{
		const char *internalPath = SDL_GetAndroidInternalStoragePath();
		if (internalPath)
		{
			setenv("HOME", internalPath, 1);
			// Create ~/.config directory
			char configDir[1024];
			snprintf(configDir, sizeof(configDir), "%s/.config", internalPath);
			mkdir(configDir, 0755);
		}
	}
#endif

	// Start our "machine"
	Pomme::Init();

#ifdef __ANDROID__
	// Extract APK assets to internal storage on first run
	{
		const char *internalPath = SDL_GetAndroidInternalStoragePath();
		if (internalPath)
		{
			char dataDir[1024];
			snprintf(dataDir, sizeof(dataDir), "%s/Data", internalPath);
			if (!Android_ExtractAssets(dataDir))
			{
				SDL_Log("WARNING: Asset extraction incomplete; some files may be missing.");
			}
			// Tell Pomme where the data files are
			setenv("NANOSAUR_DATA_DIR", dataDir, 1);
		}
	}
#endif

	// Find path to game data folder
	const char* executablePath = argc > 0 ? argv[0] : NULL;
#ifdef __ANDROID__
	// On Android, data is always in internal storage
	{
		const char *internalPath = SDL_GetAndroidInternalStoragePath();
		char dataDir[1024];
		if (internalPath)
		{
			snprintf(dataDir, sizeof(dataDir), "%s/Data", internalPath);
			executablePath = NULL;  // force FindGameData to use "Data" path
		}
		// Override current directory to where data is
		if (internalPath)
		{
			chdir(internalPath);
		}
	}
#endif
	fs::path dataPath = FindGameData(executablePath);

	// Load game prefs before starting
	LoadPrefs();

retryVideo:
	// Initialize SDL video subsystem
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		throw std::runtime_error("Couldn't initialize SDL video subsystem.");
	}

#ifdef __ANDROID__
	// Request OpenGL ES 3.0 context on Android
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	// Create window
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

	gCurrentAntialiasingLevel = gGamePrefs.antialiasingLevel;
	if (gCurrentAntialiasingLevel != 0)
	{
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 1 << gCurrentAntialiasingLevel);
	}

#ifdef __ANDROID__
	gSDLWindow = SDL_CreateWindow(
		GAME_FULL_NAME " " GAME_VERSION, 0, 0,
		SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#else
	gSDLWindow = SDL_CreateWindow(
		GAME_FULL_NAME " " GAME_VERSION, 640, 480,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif

	if (!gSDLWindow)
	{
		if (gCurrentAntialiasingLevel != 0)
		{
			SDL_Log("Couldn't create SDL window with the requested MSAA level. Retrying without MSAA...");

			// retry without MSAA
			gGamePrefs.antialiasingLevel = 0;
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			goto retryVideo;
		}
		else
		{
			throw std::runtime_error("Couldn't create SDL window.");
		}
	}

	// Set up globals that the game expects
	gCoverWindow = Pomme::Graphics::GetScreenPort();
	gBackdropPixels = (UInt32*) GetPixBaseAddr(GetGWorldPixMap(gCoverWindow));

	// Init gamepad subsystem
	SDL_Init(SDL_INIT_GAMEPAD);
	auto gamecontrollerdbPath8 = (dataPath / "System" / "gamecontrollerdb.txt").u8string();
	if (-1 == SDL_AddGamepadMappingsFromFile((const char*)gamecontrollerdbPath8.c_str()))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, GAME_FULL_NAME, "Couldn't load gamecontrollerdb.txt!", gSDLWindow);
	}

#ifdef __ANDROID__
	// Initialize the GLES bridge (compiles shaders, sets up VBOs)
	// This must be called after the GL context is created.
	// The GL context is created lazily by QD3D_Boot(), but we can call
	// GLESBridge_Init() after that from GameMain(). We hook in via the
	// existing QD3D_Boot() path by calling it here once the window exists.
	// (Actual init happens in QD3D_Boot → Render_InitState → GLESBridge_Init)
	TouchControls_Init();
#endif
}

static void Shutdown()
{
	// Always restore the user's mouse acceleration before exiting.
	// SetMacLinearMouse(false);

#ifdef __ANDROID__
	TouchControls_Shutdown();
	GLESBridge_Shutdown();
#endif

	Pomme::Shutdown();

	if (gSDLWindow)
	{
		SDL_DestroyWindow(gSDLWindow);
		gSDLWindow = NULL;
	}

	SDL_Quit();
}

int main(int argc, char** argv)
{
	bool success = true;
	std::string uncaught = "";

	try
	{
		Boot(argc, argv);
		GameMain();
	}
	catch (Pomme::QuitRequest&)
	{
		// no-op, the game may throw this exception to shut us down cleanly
	}
#if !(_DEBUG) || defined(__ANDROID__)
	// In release builds (and all Android builds), catch anything that might
	// be thrown by GameMain so we can show an error dialog to the user.
	// On Android, assembleDebug defines _DEBUG, but we still want to catch
	// exceptions rather than propagating them as std::terminate() to SDL's Java layer.
	catch (std::exception& ex)		// Last-resort catch
	{
		success = false;
		uncaught = ex.what();
	}
	catch (...)						// Last-resort catch
	{
		success = false;
		uncaught = "unknown";
	}
#endif

	Shutdown();

	if (!success)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Uncaught exception: %s", uncaught.c_str());
#ifndef __ANDROID__
		SDL_ShowSimpleMessageBox(0, GAME_FULL_NAME, uncaught.c_str(), nullptr);
#else
		__android_log_print(ANDROID_LOG_ERROR, "Nanosaur", "Fatal: %s", uncaught.c_str());
#endif
	}

	return success ? 0 : 1;
}

