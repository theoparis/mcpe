#include "OptionsFile.h"
#include "../world/level/storage/FolderMethods.h"
#include <cstdlib>
#include <stdio.h>
#include <string.h>
#if defined(SDL3)
#include <SDL3/SDL.h>
#endif

OptionsFile::OptionsFile() {
#if defined(SDL3)
  char *prefPath = SDL_GetPrefPath("minecraftcpp", "minecraftcpp");
  if (prefPath && *prefPath) {
    settingsPath = std::string(prefPath) + "options.txt";
    SDL_free(prefPath);
  } else {
    if (prefPath)
      SDL_free(prefPath);
    const char *home = getenv("HOME");
    std::string base = (home && *home) ? (std::string(home) + "/.minecraft")
                                       : std::string(".");
    createFolderIfNotExists(base.c_str());
    settingsPath = base + "/options.txt";
  }
#elif defined(__APPLE__)
  settingsPath = "./Documents/options.txt";
#elif defined(ANDROID)
  settingsPath = "options.txt";
#else
  settingsPath = "options.txt";
#endif
}

void OptionsFile::save(const StringVector &settings) {
  FILE *pFile = fopen(settingsPath.c_str(), "w");
  if (pFile != NULL) {
    for (StringVector::const_iterator it = settings.begin();
         it != settings.end(); ++it) {
      fprintf(pFile, "%s\n", it->c_str());
    }
    fclose(pFile);
  }
}

StringVector OptionsFile::getOptionStrings() {
  StringVector returnVector;
  FILE *pFile = fopen(settingsPath.c_str(), "r");
  if (pFile != NULL) {
    char lineBuff[256];
    while (fgets(lineBuff, sizeof lineBuff, pFile)) {
      size_t len = strlen(lineBuff);
      while (len > 0 &&
             (lineBuff[len - 1] == '\n' || lineBuff[len - 1] == '\r')) {
        lineBuff[--len] = '\0';
      }
      if (len == 0)
        continue;

      char *sep = strchr(lineBuff, ':');
      if (!sep)
        continue;

      *sep = '\0';
      const char *key = lineBuff;
      const char *value = sep + 1;
      if (strlen(key) == 0)
        continue;

      returnVector.push_back(std::string(key));
      returnVector.push_back(std::string(value));
    }
    fclose(pFile);
  }
  return returnVector;
}
