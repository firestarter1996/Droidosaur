// ANDROID ASSET EXTRACTION IMPLEMENTATION
// Copies game data from APK assets to the app's internal storage.
//
// SDL_EnumerateDirectory uses POSIX opendir() on Android and therefore
// CANNOT enumerate APK asset paths.  Instead we keep a complete, explicit
// list of every game data file.  SDL_IOFromFile() with a relative path
// DOES read from the APK asset bundle on Android, so we use that for the
// actual byte-for-byte copy.

#ifdef __ANDROID__

#include "AndroidAssets.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "Nanosaur", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "Nanosaur", __VA_ARGS__)

// Version file: if this file exists and contains our version, skip extraction.
// Bump this string whenever the Data/ directory contents change.
#define EXTRACT_VERSION_FILE  ".extract_version"
#define EXTRACT_VERSION       "1.4.5"

// -------------------------------------------------------------------------
// Complete list of all game data files, relative to the Data/ root.
// These are the exact paths that end up at the APK asset bundle root
// (because build.gradle.kts uses  assets.srcDirs("../../Data")).
// -------------------------------------------------------------------------
static const char *kAllDataFiles[] = {
    "Audio/GameSong.aiff",
    "Audio/Song_Pangea.aiff",
    "Audio/SoundBank/Alarm.aiff",
    "Audio/SoundBank/Ambient.aiff",
    "Audio/SoundBank/Blaster.aiff",
    "Audio/SoundBank/Bubbles.aiff",
    "Audio/SoundBank/Crunch.aiff",
    "Audio/SoundBank/Crystal.aiff",
    "Audio/SoundBank/DiloAttack.aiff",
    "Audio/SoundBank/EnemyDie.aiff",
    "Audio/SoundBank/Explode.aiff",
    "Audio/SoundBank/Footstep.aiff",
    "Audio/SoundBank/HeatSeek.aiff",
    "Audio/SoundBank/JetLoop.aiff",
    "Audio/SoundBank/Jump.aiff",
    "Audio/SoundBank/MenuChange.aiff",
    "Audio/SoundBank/POWPickup.aiff",
    "Audio/SoundBank/Portal.aiff",
    "Audio/SoundBank/Roar.aiff",
    "Audio/SoundBank/RockSlam.aiff",
    "Audio/SoundBank/Select.aiff",
    "Audio/SoundBank/Shield.aiff",
    "Audio/SoundBank/Sonic.aiff",
    "Audio/SoundBank/Steam.aiff",
    "Audio/SoundBank/WingFlap.aiff",
    "Audio/TitleSong.aiff",
    "Images/Boot1.tga",
    "Images/Boot1Pro.tga",
    "Images/Boot2.tga",
    "Images/Help1.tga",
    "Images/Infobar.tga",
    "Images/Map.tga",
    "Images/Shadow.tga",
    "Models/Global_Models.3dmf",
    "Models/HighScores.3dmf",
    "Models/Infobar_Models.3dmf",
    "Models/Level1_Models.3dmf",
    "Models/MenuInterface.3dmf",
    "Models/Title.3dmf",
    "Movies/Lose.mov",
    "Movies/Win.mov",
    "Skeletons/Deinon.3dmf",
    "Skeletons/Deinon.skeleton.rsrc",
    "Skeletons/DeinonTeethFix.3dmf",
    "Skeletons/Diloph.3dmf",
    "Skeletons/Diloph.skeleton.rsrc",
    "Skeletons/Ptera.3dmf",
    "Skeletons/Ptera.skeleton.rsrc",
    "Skeletons/Rex.3dmf",
    "Skeletons/Rex.skeleton.rsrc",
    "Skeletons/Stego.3dmf",
    "Skeletons/Stego.skeleton.rsrc",
    "Skeletons/Tricer.3dmf",
    "Skeletons/Tricer.skeleton.rsrc",
    "Sprites/Infobar1000.tga",
    "Sprites/Infobar1001.tga",
    "Sprites/Infobar1002.tga",
    "Sprites/Infobar1003.tga",
    "Sprites/Infobar1004.tga",
    "Sprites/Infobar1005.tga",
    "Sprites/Infobar1006.tga",
    "Sprites/Infobar1007.tga",
    "Sprites/Infobar1008.tga",
    "Sprites/Infobar1009.tga",
    "Sprites/Infobar1010.tga",
    "Sprites/Infobar1011.tga",
    "Sprites/Infobar1012.tga",
    "Sprites/Infobar1013.tga",
    "Sprites/Infobar1014.tga",
    "Sprites/Infobar1015.tga",
    "Sprites/Infobar1016.tga",
    "Sprites/Infobar1017.tga",
    "Sprites/Infobar1018.tga",
    "Sprites/Infobar1019.tga",
    "Sprites/Infobar1020.tga",
    "Sprites/Infobar1021.tga",
    "Sprites/Infobar1022.tga",
    "Sprites/Infobar1023.tga",
    "Sprites/Infobar1024.tga",
    "Sprites/Infobar1025.tga",
    "Sprites/Infobar1026.tga",
    "Sprites/Infobar1027.tga",
    "Sprites/Infobar1028.tga",
    "Sprites/Infobar1029.tga",
    "Sprites/Infobar1030.tga",
    "Sprites/Infobar1031.tga",
    "Sprites/Infobar1032.tga",
    "Sprites/Infobar1033.tga",
    "Sprites/Infobar1034.tga",
    "Sprites/Infobar1035.tga",
    "Sprites/Infobar1036.tga",
    "Sprites/Infobar1037.tga",
    "Sprites/Infobar1038.tga",
    "Sprites/Infobar1039.tga",
    "Sprites/Infobar1040.tga",
    "Sprites/Infobar1041.tga",
    "Sprites/Infobar1042.tga",
    "Sprites/Infobar1043.tga",
    "Sprites/Infobar1044.tga",
    "Sprites/Infobar1045.tga",
    "Sprites/Infobar1046.tga",
    "Sprites/Infobar1047.tga",
    "Sprites/Infobar1048.tga",
    "Sprites/Infobar1049.tga",
    "System/gamecontrollerdb.txt",
    "Terrain/Level1.ter",
    "Terrain/Level1.trt",
    "Terrain/Level1Pro.ter",
    NULL
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Create every directory component of a file path.
static void MakeDirsFor(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

// Copy one file from the APK asset bundle to the filesystem.
// assetPath  – relative to the APK asset root (no leading /)
// destPath   – absolute filesystem destination
static bool ExtractOneFile(const char *assetPath, const char *destPath)
{
    SDL_IOStream *src = SDL_IOFromFile(assetPath, "rb");
    if (!src)
    {
        LOGE("Cannot open asset %s: %s", assetPath, SDL_GetError());
        return false;
    }

    MakeDirsFor(destPath);

    FILE *dst = fopen(destPath, "wb");
    if (!dst)
    {
        SDL_CloseIO(src);
        LOGE("Cannot create %s: %s", destPath, strerror(errno));
        return false;
    }

    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = SDL_ReadIO(src, buf, sizeof(buf))) > 0)
    {
        if (fwrite(buf, 1, n, dst) != n)
        {
            LOGE("Write error for %s", destPath);
            ok = false;
            break;
        }
    }

    fclose(dst);
    SDL_CloseIO(src);
    return ok;
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

bool Android_ExtractAssets(const char *destDir)
{
    // Check if already extracted with the current version.
    char versionFile[1024];
    snprintf(versionFile, sizeof(versionFile), "%s/%s", destDir, EXTRACT_VERSION_FILE);

    FILE *vf = fopen(versionFile, "r");
    if (vf)
    {
        char ver[64] = "";
        if (fgets(ver, sizeof(ver), vf))
        {
            size_t len = strlen(ver);
            while (len > 0 && (ver[len-1] == '\n' || ver[len-1] == '\r'))
                ver[--len] = '\0';

            if (strcmp(ver, EXTRACT_VERSION) == 0)
            {
                fclose(vf);
                LOGI("Assets already extracted (version %s)", EXTRACT_VERSION);
                return true;
            }
        }
        fclose(vf);
    }

    LOGI("Extracting game assets to %s ...", destDir);
    mkdir(destDir, 0755);

    int totalFiles = 0;
    int failedFiles = 0;

    for (int i = 0; kAllDataFiles[i] != NULL; i++)
    {
        char destPath[1024];
        snprintf(destPath, sizeof(destPath), "%s/%s", destDir, kAllDataFiles[i]);

        if (!ExtractOneFile(kAllDataFiles[i], destPath))
        {
            LOGE("Failed to extract %s", kAllDataFiles[i]);
            failedFiles++;
        }

        totalFiles++;
    }

    if (failedFiles > 0)
    {
        LOGE("Asset extraction: %d/%d files failed", failedFiles, totalFiles);
        return false;
    }

    // Write version stamp only after all files succeeded.
    vf = fopen(versionFile, "w");
    if (vf)
    {
        fputs(EXTRACT_VERSION "\n", vf);
        fclose(vf);
    }

    LOGI("Asset extraction complete: %d files extracted", totalFiles);
    return true;
}

#endif // __ANDROID__
