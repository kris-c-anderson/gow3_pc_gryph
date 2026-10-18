/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GOW3_SWITCH_HOOK_H
#define GOW3_SWITCH_HOOK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "signature.h"

/* Switch attacks (runtime_pad.c, GOW3_SWITCH_ATTACKS). Offsets as found in a v01.02 build; builds
 * are found by signature.
 *
 * Kratos' weapons are items 0x10 Nemean Cestus, 0x11 Nemesis Whip, 0x12 Blades of Exile and 0x13
 * Claws of Hades. His player stats keep the weapon the d-pad selected at +0x20a (the d-pad selects
 * it with or without L1). Each weapon's switch attack (MOV_CestusTransition and the like) is
 * reached through ordinary branches of his move data, which carry the weapon they go to at +0x2f,
 * the weapon he must hold in flags at +0x10 and the button that takes them at +0x2c: in the game,
 * L1 + Cross from each weapon to the next one he owns. The switch attack itself selects its weapon
 * (SCR_EnableWeapon, 0x6b5b0).
 *
 * The hooks change which input takes those switch branches:
 * - weapon check (0xeb5b0, the branch's held-weapon flags; rdi stats, rsi branch, edx the weapon in
 *   his hands): a switch branch no longer asks for a weapon in his hands. The stub also notes the
 *   stats and that weapon for the button test, and counts its calls (heartbeat);
 * - button test (inside 0xec0e0, where the branch's button is compared with the one pressed): a
 *   switch branch to the weapon the d-pad selected is taken once while the pad has a request
 *   pending (L1 + d-pad), whatever was pressed; otherwise L1 + Cross only takes the branch to the
 *   weapon in his hands (its switch attack again, no cycling);
 * - select (0x6b5b0): the switch attack started, the request is done.
 * Which branches a move has, their other conditions (owning the weapon, L1 held, timing) and their
 * priorities stay the game's. */
enum { GOW3_SWITCH_WEAPON_HOOK_LENGTH = 6, GOW3_SWITCH_BUTTON_HOOK_LENGTH = 9, GOW3_SWITCH_SELECT_HOOK_LENGTH = 6,
       /* From the button test site: same button, other button, the held-buttons test, return 0. */
       GOW3_SWITCH_BUTTON_SAME = 0x09, GOW3_SWITCH_BUTTON_OTHER = 0x19, GOW3_SWITCH_BUTTON_HELD = 0x32,
       GOW3_SWITCH_BUTTON_OUT = 0x73 };
static const unsigned char gow3_switch_weapon_original[GOW3_SWITCH_WEAPON_HOOK_LENGTH] = {
    0x55, 0x48,0x89,0xe5, 0x41,0x56        /* push rbp; mov rbp,rsp; push r14 */
};
static const unsigned char gow3_switch_button_original[GOW3_SWITCH_BUTTON_HOOK_LENGTH] = {
    0x0f,0xbf,0x43,0x2c,                   /* movsx eax, word [rbx+0x2c] */
    0x3b,0x45,0x18,                        /* cmp eax, [rbp+0x18] */
    0x75,0x10                              /* jne other */
};
static const unsigned char gow3_switch_select_original[GOW3_SWITCH_SELECT_HOOK_LENGTH] = {
    0x55, 0x48,0x89,0xe5, 0x41,0x57        /* push rbp; mov rbp,rsp; push r15 */
};
/* The return of 0xec0e0 (add rsp,8; pop rbx; pop r12; pop r13; pop r14; pop r15; pop rbp; ret). */
static const unsigned char gow3_switch_button_out[] = {
    0x48,0x83,0xc4,0x08, 0x5b, 0x41,0x5c, 0x41,0x5d, 0x41,0x5e, 0x41,0x5f, 0x5d, 0xc3
};
static const Gow3Signature gow3_switch_weapon_site = {
    "55 48 89 e5 41 56 53 48 83 ec 10 49 89 f6 be 00 00 00 e0 48 23 b7 68 01 00 00 41 0f b7 4e 10 f6 c1 80", 0};
static const Gow3Signature gow3_switch_button_site = {
    "80 7b 2f 00 75 03 83 c9 02 0f bf 43 2c 3b 45 18 75 10 83 7d 18 ff 74 23 31 c0 f6 43 0a 80 75 5c eb 19 "
    "83 7d 18 ff 74 06 66 83 f8 ff 75 05 83 c9 04 eb 08 31 c0 f6 43 0a 80 74 41 0f b7 73 12 8b", 9};
static const Gow3Signature gow3_switch_select_site = {
    "55 48 89 e5 41 57 41 56 41 54 53 49 89 d7 41 89 f4 49 89 fe 41 8d 5c 24 ff 83 fb 1c 77 3d", 0};

/* Shared by the stubs (game thread) and the pad (runtime_pad.c). */
typedef struct {
    uint8_t pending;     /* set by the pad after L1 + d-pad; the button test takes it once */
    uint8_t weapon;      /* the weapon in his hands, as the last weapon check saw it */
    uint8_t pad[2];
    uint32_t heartbeat;  /* weapon checks: the move system is asking */
    uint32_t granted;    /* switch branches taken for a request */
    uint32_t reserved;
    uint64_t stats;      /* his player stats, from the last weapon check */
} Gow3SwitchState;
#ifdef __cplusplus
static_assert(offsetof(Gow3SwitchState, weapon) == 1 && offsetof(Gow3SwitchState, heartbeat) == 4 &&
              offsetof(Gow3SwitchState, granted) == 8 && offsetof(Gow3SwitchState, stats) == 16,
              "stubs address the fields directly");
#else
_Static_assert(offsetof(Gow3SwitchState, weapon) == 1 && offsetof(Gow3SwitchState, heartbeat) == 4 &&
               offsetof(Gow3SwitchState, granted) == 8 && offsetof(Gow3SwitchState, stats) == 16,
               "stubs address the fields directly");
#endif

static inline unsigned gow3_switch_resume(unsigned char *out, unsigned n, const unsigned char *original,
                                          unsigned length, uintptr_t resume) {
    static const unsigned char jump[] = {0xff,0x25,0,0,0,0}; /* jmp [rip+0] */
    memcpy(out + n, original, length);
    n += length;
    memcpy(out + n, jump, sizeof(jump));
    n += sizeof(jump);
    memcpy(out + n, &resume, 8);
    return n + 8;
}

/* 0xeb5b0, the branch's held-weapon flags. Function entry: rax, rcx and the flags are free. */
static inline unsigned gow3_switch_build_weapon_stub(unsigned char *out, uintptr_t state, uintptr_t resume) {
    static const unsigned char body[] = {
        0x48,0xb8, 0,0,0,0,0,0,0,0,              /* movabs rax, state */
        0xf0,0xff,0x40,0x04,                     /* lock inc dword [rax+4]: heartbeat */
        0x48,0x89,0x78,0x10,                     /* mov [rax+16], rdi: stats */
        0x88,0x50,0x01,                          /* mov [rax+1], dl: the weapon in his hands */
        0x0f,0xb6,0x4e,0x2f,                     /* movzx ecx, byte [rsi+0x2f]: where the branch goes */
        0x83,0xe9,0x10,                          /* sub ecx, 0x10 */
        0x83,0xf9,0x03,                          /* cmp ecx, 3 */
        0x77,0x06,                               /* ja original: not a switch branch */
        0xb8,0x01,0x00,0x00,0x00,                /* mov eax, 1: any weapon in his hands */
        0xc3,                                    /* ret */
    };
    memcpy(out, body, sizeof(body));
    memcpy(out + 2, &state, 8);
    return gow3_switch_resume(out, sizeof(body), gow3_switch_weapon_original, GOW3_SWITCH_WEAPON_HOOK_LENGTH,
                              resume);
}

/* The button test in 0xec0e0 (rbx branch, rbp frame, [rbp+0x18] the button pressed). ecx holds
 * the match flags; eax, edx and esi are free. targets: same, other, held, out (absolute). */
static inline unsigned gow3_switch_build_button_stub(unsigned char *out, uintptr_t state, const uintptr_t targets[4]) {
    static const unsigned char body[] = {
        0x0f,0xb6,0x43,0x2f,                     /* movzx eax, byte [rbx+0x2f]: where the branch goes */
        0x8d,0x70,0xf0,                          /* lea esi, [rax-0x10] */
        0x83,0xfe,0x03,                          /* cmp esi, 3 */
        0x77,0x38,                               /* ja press: not a switch branch */
        0x48,0xba, 0,0,0,0,0,0,0,0,              /* movabs rdx, state */
        0x80,0x3a,0x00,                          /* cmp byte [rdx], 0: request pending? */
        0x74,0x21,                               /* je inhand */
        0x48,0x8b,0x72,0x10,                     /* mov rsi, [rdx+16]: stats */
        0x48,0x85,0xf6,                          /* test rsi, rsi */
        0x74,0x18,                               /* je inhand */
        0x0f,0xb6,0xb6,0x0a,0x02,0x00,0x00,      /* movzx esi, byte [rsi+0x20a]: the d-pad's weapon */
        0x39,0xf0,                               /* cmp eax, esi */
        0x75,0x0d,                               /* jne inhand */
        0xc6,0x02,0x00,                          /* mov byte [rdx], 0: taken once */
        0xf0,0xff,0x42,0x08,                     /* lock inc dword [rdx+8]: granted */
        0xff,0x25,0x25,0x00,0x00,0x00,           /* jmp [held]: on to the held-buttons test */
        0x0f,0xb6,0x72,0x01,                     /* inhand: movzx esi, byte [rdx+1] */
        0x39,0xf0,                               /* cmp eax, esi */
        0x75,0x15,                               /* jne fail: no cycling */
        0x0f,0xbf,0x43,0x2c,                     /* press: movsx eax, word [rbx+0x2c] */
        0x3b,0x45,0x18,                          /* cmp eax, [rbp+0x18] */
        0x75,0x06,                               /* jne other */
        0xff,0x25,0x16,0x00,0x00,0x00,           /* jmp [same] */
        0xff,0x25,0x18,0x00,0x00,0x00,           /* other: jmp [other] */
        0x31,0xc0,                               /* fail: xor eax, eax */
        0xff,0x25,0x18,0x00,0x00,0x00,           /* jmp [out] */
    };
    memcpy(out, body, sizeof(body));
    memcpy(out + 14, &state, 8);
    unsigned n = sizeof(body);
    memcpy(out + n, &targets[2], 8);  /* held */
    memcpy(out + n + 8, &targets[0], 8);  /* same */
    memcpy(out + n + 16, &targets[1], 8);  /* other */
    memcpy(out + n + 24, &targets[3], 8);  /* out */
    return n + 32;
}

/* 0x6b5b0, a weapon selected by SCR_EnableWeapon: a switch attack started. */
static inline unsigned gow3_switch_build_select_stub(unsigned char *out, uintptr_t state, uintptr_t resume) {
    static const unsigned char body[] = {
        0x48,0xb8, 0,0,0,0,0,0,0,0,              /* movabs rax, state */
        0xc6,0x00,0x00,                          /* mov byte [rax], 0: request done */
    };
    memcpy(out, body, sizeof(body));
    memcpy(out + 2, &state, 8);
    return gow3_switch_resume(out, sizeof(body), gow3_switch_select_original, GOW3_SWITCH_SELECT_HOOK_LENGTH,
                              resume);
}
#endif
