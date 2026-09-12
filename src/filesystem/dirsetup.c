#include "filesystem/path.h"
#include "filesystem/dirsetup.h"
#include "Nebu_filesystem.h"

#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

const char* getHome() {
  const char *home = getenv("HOME");
  struct passwd *user;
  if(home != NULL && home[0] != '\0') return home;
  user = getpwuid(getuid());
  return user != NULL ? user->pw_dir : ".";
}

void dirSetup(const char *executable) {
  setExecutablePath(executable);
  initDirectories();
}

