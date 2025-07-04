char const* USAGE_FMT = "%s\nPrints the revision of the detected version control system in this directory.";

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  int argi;
  for (argi = 1; argi < argc; argi++) {
    char const* arg = argv[argi];
    if (arg[0] == '-' && arg[1] == '-') {
      if (arg[2]) printf(USAGE_FMT, argv[0]);
      continue;
    } else {
      break;
    }
  }
  system("git rev-parse HEAD");
  return 0;
}
