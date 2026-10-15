/*
 * ProsperoEden game tile launcher.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game tile's starter runs this payload through websrv (GET /hbldr?daemon=1&path=...) and
 * then closes itself. This waits until the starter is gone, then starts ProsperoEden (PPSA99008)
 * with the tile's game ("title=<16 hex digits>" or "rom=<file name>" in its arguments), or, with
 * "app=<title ID>" and "path=<ROM path>", that emulator with the ROM's path as its argument.
 *
 * It never closes an app: websrv's /launch closes the running app and starts the next one at once,
 * which fails while the app being closed is the one asking (PS5 error CE-105773-3). If the
 * starter has not closed after a while, or the launch fails, this says so in a notification and
 * leaves the console as it is.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char* title_id, char** argv, app_launch_ctx_t* ctx);
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

#define EMULATOR "PPSA99008"
#define WAIT_STEPS 100 /* 100 ms each */


static void
notify(const char* message) {
  notify_request_t req = {0};
  snprintf(req.message, sizeof(req.message), "%s", message);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


/* websrv splits its args at spaces and keeps the backslash that escapes one: put the game back
   together as one argument. */
static char*
game_argument(int argc, char** argv) {
  static char game[1024];
  size_t length = 0;
  int found = 0;

  for(int i=0; i<argc && argv[i]; i++) {
    if(!found) {
      if(strncmp(argv[i], "title=", 6) && strncmp(argv[i], "rom=", 4) && strncmp(argv[i], "path=", 5)) {
        continue;
      }
      found = 1;
    } else if(!strncmp(argv[i], "title=", 6) || !strncmp(argv[i], "rom=", 4) ||
              !strncmp(argv[i], "path=", 5) || !strncmp(argv[i], "app=", 4)) {
      break;
    } else if(length + 1 < sizeof(game)) {
      game[length++] = ' ';
    }
    for(const char* c=argv[i]; *c && length + 1 < sizeof(game); c++) {
      if(*c == '\\' && c[1]) {
        c++;
      }
      game[length++] = *c;
    }
  }
  game[length] = 0;
  return found ? game : 0;
}


/* The emulator named by an "app=<title ID>" argument, else ProsperoEden. */
static const char*
emulator(int argc, char** argv) {
  static char app[10];

  for(int i=0; i<argc && argv[i]; i++) {
    if(!strncmp(argv[i], "app=", 4) && strlen(argv[i]) == 13 && !strncmp(argv[i] + 4, "PPSA", 4)) {
      snprintf(app, sizeof(app), "%s", argv[i] + 4);
      return app;
    }
  }
  return EMULATOR;
}


int
main(int argc, char** argv) {
  app_launch_ctx_t ctx = {0};
  char* args[3] = {0, 0, 0};
  const char* app = emulator(argc, argv);
  char message[160];
  int err;

  if(!(args[0]=game_argument(argc, argv))) {
    notify("Game tile: no game given");
    return EXIT_FAILURE;
  }
  if(!strncmp(args[0], "path=", 5)) {
    /* Other emulators take the ROM's path as their first argument (argv[1]); given twice, it is
       argv[1] whether or not the console puts the app's own path first. */
    args[0] += 5;
    args[1] = args[0];
  }

  for(int i=0; sceSystemServiceGetAppIdOfRunningBigApp() > 0; i++) {
    if(i == WAIT_STEPS) {
      notify("Game tile: the tile did not close, so the game was not started");
      return EXIT_FAILURE;
    }
    usleep(100 * 1000);
  }
  usleep(500 * 1000); /* let the home screen settle after the tile closed */

  sceUserServiceInitialize(0);
  if((err=sceUserServiceGetForegroundUser(&ctx.user_id))) {
    snprintf(message, sizeof(message), "Game tile: no signed-in user (0x%08x)", err);
    notify(message);
    return EXIT_FAILURE;
  }
  if((err=sceSystemServiceLaunchApp(app, args, &ctx)) < 0) {
    snprintf(message, sizeof(message), "Game tile: %s did not start (0x%08x)", app, err);
    notify(message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
