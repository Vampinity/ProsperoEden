/*
 * ProsperoEden game tile launcher.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game tile's starter runs this payload through websrv (GET /hbldr?daemon=1&path=...) and
 * then closes itself. This waits until the starter is gone, then starts the tile's emulator
 * ("app=<title ID>", ProsperoEden PPSA99008 when not given) with the tile's launch arguments (one
 * "arg=<value>" each). A tile with only "rom=<file name>" starts ProsperoEden with
 * --rom <file name> --exit-after-game.
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


/* The tile's settings, one per "key=value" argument: websrv splits its args at spaces and keeps the
   backslash that escapes one, so a value is put back together up to the next known key. */
#define MAX_ARGS 16

static const char* KEYS[] = {"app=", "arg=", "title=", "rom=", 0};

static int
key_length(const char* token) {
  for(int k=0; KEYS[k]; k++) {
    if(!strncmp(token, KEYS[k], strlen(KEYS[k]))) {
      return strlen(KEYS[k]);
    }
  }
  return 0;
}


static int
read_fields(int argc, char** argv, char keys[][8], char values[][1024], int max) {
  int count = -1;
  size_t length = 0;

  for(int i=0; i<argc && argv[i]; i++) {
    const char* c = argv[i];
    int k = key_length(c);
    if(k) {
      if(count + 1 == max) {
        break;
      }
      count++;
      snprintf(keys[count], 8, "%.*s", k - 1, c);
      values[count][0] = 0;
      length = 0;
      c += k;
    } else if(count < 0) {
      continue;
    } else if(length + 1 < 1024) {
      values[count][length++] = ' ';
    }
    for(; *c && length + 1 < 1024; c++) {
      if(*c == '\\' && c[1]) {
        c++;
      }
      values[count][length++] = *c;
    }
    values[count][length] = 0;
  }
  return count + 1;
}


int
main(int argc, char** argv) {
  static char keys[MAX_ARGS][8];
  static char values[MAX_ARGS][1024];
  app_launch_ctx_t ctx = {0};
  const char* app = EMULATOR;
  const char* rom = 0;
  char* args[MAX_ARGS + 3] = {0};
  int count = 0;
  char message[160];
  int err;

  int fields = read_fields(argc, argv, keys, values, MAX_ARGS);
  for(int i=0; i<fields; i++) {
    if(!strcmp(keys[i], "app")) {
      app = values[i];
    } else if(!strcmp(keys[i], "arg")) {
      args[count++] = values[i];
    } else if(!strcmp(keys[i], "rom")) {
      rom = values[i];
    }
  }
  /* A tile made before "app=" and "arg=": its game, the way ProsperoEden takes it since v1.000.080. */
  if(!count && rom) {
    args[count++] = "--rom";
    args[count++] = (char*)rom;
    args[count++] = "--exit-after-game";
  }
  if(!count) {
    notify("Game tile: no game given");
    return EXIT_FAILURE;
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
