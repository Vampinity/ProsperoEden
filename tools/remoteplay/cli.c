/* Offline account activation without a screen: offact.c's registry calls, run as a payload
   through elfldr (port 9021), printing to the sender's terminal. GPLv3+, from ps5-payload-dev/offact.
   Built with -DACTIVATE it activates the one account named TARGET_NAME with TARGET_ID, and only if
   that account has no ID yet or already has this one. Without it, it only lists the accounts. */
#include <stdio.h>
#include <string.h>
#include "offact.h"

#define TARGET_NAME "Vampeyro"
#define TARGET_ID   0x2a8405d77e846473UL

static void list(void) {
    for (int n = 1; n <= ACCOUNT_NUMB_MAX; n++) {
        char name[ACCOUNT_NAME_MAX] = {0}, type[ACCOUNT_TYPE_MAX] = {0};
        uint64_t id = 0;
        int flags = 0;
        if (OffAct_GetAccountName(n, name) || !*name) continue;
        OffAct_GetAccountId(n, &id);
        OffAct_GetAccountType(n, type);
        OffAct_GetAccountFlags(n, &flags);
        printf("account %2d  name: %-16s id: 0x%016lx  type: %-2s  flags: 0x%04x\n", n, name, id, type, flags);
    }
}

int main(void) {
    printf("offact-cli: accounts on this console\n");
    list();
#ifdef ACTIVATE
    for (int n = 1; n <= ACCOUNT_NUMB_MAX; n++) {
        char name[ACCOUNT_NAME_MAX] = {0};
        uint64_t id = 0;
        if (OffAct_GetAccountName(n, name) || strcmp(name, TARGET_NAME)) continue;
        OffAct_GetAccountId(n, &id);
        if (id && id != TARGET_ID) {
            printf("%s has a different ID (0x%016lx); left unchanged.\n", name, id);
            return 1;
        }
        char type[ACCOUNT_TYPE_MAX] = "np";
        OffAct_SetAccountId(n, TARGET_ID);
        OffAct_SetAccountType(n, type);
        OffAct_SetAccountFlags(n, 4098);
        printf("Activated %s (account %d) with 0x%016lx. Restart the PS5 now.\n", name, n, TARGET_ID);
        list();
        return 0;
    }
    printf("No account named %s found; nothing changed.\n", TARGET_NAME);
    return 1;
#else
    return 0;
#endif
}
