/*
 * Home screen dump for the console badge research.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Only reads system files. Lists the home screen app (NPXS40002) and the system's UI pictures
 * into /data/prosperoeden/homeui-dump/listing.txt, copies the pictures whose name mentions a
 * platform or logo, and decrypts the home screen's script bundles (RNPS, the format etaHEN's FTP
 * also decrypts through /dev/rnps) into the same folder, so the code that picks the PS4/PS5 badge
 * in front of a game's name can be found. Other folders can be given as arguments.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define OUT "/data/prosperoeden/homeui-dump"
#define MAX_FILE (64 * 1024 * 1024)
#define DECRYPT_RNPS_BUNDLE 0xC0105203

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

struct rnps_args {
  void* buffer;
  int size;
  int error;
};

static FILE* listing;
static int bundles, decrypted, pictures;


static void
notify(const char* message) {
  notify_request_t req = {0};
  snprintf(req.message, sizeof(req.message), "%s", message);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


static int
contains(const char* name, const char* word) {
  for(const char* p=name; *p; p++) {
    size_t i = 0;
    while(word[i] && tolower((unsigned char)p[i]) == word[i]) i++;
    if(!word[i]) return 1;
  }
  return 0;
}


static int
ends_with(const char* name, const char* end) {
  size_t a = strlen(name), b = strlen(end);
  return a >= b && !strcmp(name + a - b, end);
}


/* The dump's name for a system file: its path with / as _. */
static void
out_path(char* out, size_t size, const char* path, const char* suffix) {
  snprintf(out, size, OUT "/");
  size_t n = strlen(out);
  for(const char* p=path + 1; *p && n + 1 < size; p++) out[n++] = *p == '/' ? '_' : *p;
  out[n] = 0;
  strncat(out, suffix, size - strlen(out) - 1);
}


static unsigned char*
read_all(const char* path, ssize_t* size) {
  int fd = open(path, O_RDONLY);
  if(fd < 0) return NULL;
  unsigned char* data = malloc(MAX_FILE);
  ssize_t total = 0, got;
  while(data && total < MAX_FILE && (got = read(fd, data + total, MAX_FILE - total)) > 0) total += got;
  close(fd);
  *size = total;
  return data;
}


static void
write_all(const char* path, const unsigned char* data, ssize_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return;
  ssize_t done = 0, put;
  while(done < size && (put = write(fd, data + done, size - done)) > 0) done += put;
  close(fd);
}


static void
handle_file(const char* path, const char* name, off_t size) {
  fprintf(listing, "%10lld  %s\n", (long long)size, path);
  int bundle = ends_with(name, ".bundle") || ends_with(name, ".jsbundle") || ends_with(name, ".hbc");
  int picture = (ends_with(name, ".png") || ends_with(name, ".dds") || ends_with(name, ".svg")) &&
                (contains(name, "ps4") || contains(name, "ps5") || contains(name, "platform") ||
                 contains(name, "logo") || contains(name, "badge"));
  if(!bundle && !picture && size > 8) {
    /* Bundles without the usual name still start with RNPSHEDR. */
    int fd = open(path, O_RDONLY);
    char magic[8] = {0};
    if(fd >= 0) {
      bundle = read(fd, magic, 8) == 8 && !memcmp(magic, "RNPSHEDR", 8);
      close(fd);
    }
  }
  if((!bundle && !picture) || size > MAX_FILE) return;

  ssize_t length = 0;
  unsigned char* data = read_all(path, &length);
  if(!data) return;
  char out[1024];
  out_path(out, sizeof(out), path, "");
  write_all(out, data, length);  /* as it is on the console */
  if(picture) {
    pictures++;
  } else {
    bundles++;
    int fd = open("/dev/rnps", O_RDWR);
    struct rnps_args args = {data, (int)length, 0};
    if(fd >= 0 && ioctl(fd, DECRYPT_RNPS_BUNDLE, &args) == 0 && args.error == 0) {
      out_path(out, sizeof(out), path, ".decrypted");
      write_all(out, data, args.size > 0 && args.size <= length ? args.size : length);
      decrypted++;
    } else {
      fprintf(listing, "            could not decrypt (%s, 0x%x)\n", fd < 0 ? strerror(errno) : "ioctl",
              (unsigned)args.error);
    }
    if(fd >= 0) close(fd);
  }
  free(data);
}


static void
walk(const char* path, int depth) {
  DIR* dir = opendir(path);
  if(!dir || depth > 12) {
    if(dir) closedir(dir);
    return;
  }
  struct dirent* entry;
  while((entry = readdir(dir))) {
    if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    char child[1024];
    snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
    struct stat st;
    if(lstat(child, &st)) continue;
    if(S_ISDIR(st.st_mode)) walk(child, depth + 1);
    else if(S_ISREG(st.st_mode)) handle_file(child, entry->d_name, st.st_size);
  }
  closedir(dir);
}


int
main(int argc, char** argv) {
  static const char* defaults[] = {"/system_ex/app/NPXS40002", "/system/vsh/app/NPXS40002",
                                   "/system_ex/vsh_asset", "/system/vsh_asset", NULL};
  mkdir("/data/prosperoeden", 0777);
  mkdir(OUT, 0777);
  listing = fopen(OUT "/listing.txt", "w");
  if(!listing) {
    notify("Home screen dump: cannot write " OUT);
    return 1;
  }
  if(argc > 1) {
    for(int i=1; i<argc && argv[i]; i++) walk(argv[i], 0);
  } else {
    for(int i=0; defaults[i]; i++) walk(defaults[i], 0);
  }
  fprintf(listing, "\n%d bundles (%d decrypted), %d pictures\n", bundles, decrypted, pictures);
  fclose(listing);

  char message[256];
  snprintf(message, sizeof(message), "Home screen dump: %d bundles (%d decrypted), %d pictures in " OUT,
           bundles, decrypted, pictures);
  notify(message);
  return 0;
}
