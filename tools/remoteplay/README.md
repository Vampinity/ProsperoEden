# Remote Play pairing tools for a 12.40 console (built with ps5-payload-sdk v0.43)

Payloads for elfldr on port 9021. Their output prints in the terminal that sends them.

- offact-list.elf: lists the console's accounts with their ID, type and flags, and changes nothing.
- offact-vampeyro.elf: activates the account named Vampeyro offline with ID 0x2a8405d77e846473. It only writes if that account has no ID yet or already has this one. Restart afterwards.
- rp-get-pin.elf: idlesauce/ps5-remoteplay-get-pin, rebuilt. Shows a Remote Play pairing PIN for the signed-in user.

Built from ps5-payload-dev/offact (cli.c reuses offact.c) and idlesauce/ps5-remoteplay-get-pin. GPLv3+.
