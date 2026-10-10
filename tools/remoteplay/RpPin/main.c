// RpPin: LinkDev without the screen. Started as a homebrew app from websrv, it asks the
// console's own Remote Play library for a pairing PIN (no ptrace, ShellUI is left alone) and
// shows the PIN and account ID as notifications until a device registers or 2 minutes pass.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#define REMOTEPLAY_rp_enable 1098973184

int sceRemoteplayInitialize(void*, size_t);
int sceRemoteplayGeneratePinCode(uint32_t*);
int sceRemoteplayConfirmDeviceRegist(int*, int*);
int sceRemoteplayNotifyPinCodeError(int);
int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(int*);
int sceRegMgrGetInt(int, int*);
int sceRegMgrSetInt(int, int);
int sceRegMgrGetBin(int, void*, size_t);

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static void say(const char* fmt, ...) {
    notify_request_t req;
    va_list args;
    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);
    printf("%s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static uint32_t user_key(uint32_t n, uint32_t base, uint32_t fallback) {
    return (n < 1 || n > 16) ? fallback : (n - 1) * 65536u + base;
}

static void base64(const uint8_t d[8], char* out) {
    static const char cs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < 8; i += 3) {
        uint32_t v = d[i] << 16 | (i + 1 < 8 ? d[i + 1] << 8 : 0) | (i + 2 < 8 ? d[i + 2] : 0);
        out[o++] = cs[(v >> 18) & 63];
        out[o++] = cs[(v >> 12) & 63];
        out[o++] = i + 1 < 8 ? cs[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < 8 ? cs[v & 63] : '=';
    }
    out[o] = 0;
}

static int account_id(char* out) {
    int user = 0;
    sceUserServiceInitialize(0);
    if (sceUserServiceGetForegroundUser(&user)) return -1;
    for (uint32_t i = 1; i <= 16; i++) {
        int id = 0;
        if (sceRegMgrGetInt(user_key(i, 125829376u, 127140096u), &id) == 0 && id == user) {
            uint8_t raw[8] = {0};
            if (sceRegMgrGetBin(user_key(i, 125830400u, 127141120u), raw, sizeof(raw))) return -1;
            base64(raw, out);
            return 0;
        }
    }
    return -1;
}

static void __attribute__((constructor)) init_remoteplay(void) {
    sceRemoteplayInitialize(0, 0);
}

int main(void) {
    int enabled = 0;
    if (sceRegMgrGetInt(REMOTEPLAY_rp_enable, &enabled) == 0 && enabled != 1)
        sceRegMgrSetInt(REMOTEPLAY_rp_enable, 1);

    char id[16] = "?";
    if (account_id(id)) say("RpPin: couldn't read the signed-in account ID");

    uint32_t pin = 0;
    sceRemoteplayNotifyPinCodeError(1);
    int err = sceRemoteplayGeneratePinCode(&pin);
    if (err) {
        say("RpPin: no PIN (0x%08x)", err);
        return -1;
    }

    time_t end = time(0) + 120, last = 0;
    while (time(0) < end) {
        if (time(0) - last >= 6) {
            say("PIN: %04u %04u\nAccount ID: %s\n%ld s left", pin / 10000, pin % 10000, id, (long)(end - time(0)));
            last = time(0);
        }
        int status = 0, code = 0;
        if ((err = sceRemoteplayConfirmDeviceRegist(&status, &code))) {
            say("RpPin: pairing check failed (0x%08x)", err);
            return -1;
        }
        if (status == 2) { say("RpPin: pairing complete"); return 0; }
        if (status == 3) {
            if (code == (int)0x80FC1047) say("RpPin: wrong PIN");
            else if (code == (int)0x80FC1040) say("RpPin: wrong account ID");
            else say("RpPin: pairing failed (0x%08x)", code);
            return -1;
        }
        usleep(250 * 1000);
    }
    sceRemoteplayNotifyPinCodeError(1);
    say("RpPin: timed out, start it again for a new PIN");
    return 0;
}
