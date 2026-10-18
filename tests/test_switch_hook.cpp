// Runs the switch-attack stubs (src/switch_hook.h) as the game would reach them: the weapon check
// lets switch branches pass and notes the weapon in hand, the button test takes a switch branch to
// the selected weapon once for a request and otherwise L1 + Cross only to the weapon in hand, and
// select ends the request. With the prepared eboot.elf as argument it also finds the three sites.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../src/switch_hook.h"

// The stubs replay the functions' first instructions, then jump here: undo them and return what
// the game's own function would (7 from the weapon check, 0 from select). The button test is
// entered with rbx the branch and [rbp+0x18] the button pressed, and leaves through four targets.
extern "C" void switch_weapon_return();
extern "C" void switch_select_return();
extern "C" uint32_t run_button(unsigned char* stub, const void* branch, int32_t pressed);
extern "C" void land_same();
extern "C" void land_other();
extern "C" void land_held();
extern "C" void land_out();
asm(".globl switch_weapon_return\nswitch_weapon_return:\n    pop %r14\n    pop %rbp\n    mov $7, %eax\n    ret\n"
    ".globl switch_select_return\nswitch_select_return:\n    pop %r15\n    pop %rbp\n    xor %eax, %eax\n    ret\n"
    ".globl run_button\nrun_button:\n"
    "    push %rbp\n    push %rbx\n    push %r12\n    sub $0x28, %rsp\n    mov %rsp, %rbp\n"
    "    mov %edx, 0x18(%rbp)\n    mov %rsi, %rbx\n    mov $0x55, %ecx\n    jmp *%rdi\n"
    ".globl land_same\nland_same:\n    mov $1, %eax\n    jmp 1f\n"
    ".globl land_other\nland_other:\n    mov $2, %eax\n    jmp 1f\n"
    ".globl land_held\nland_held:\n    mov $3, %eax\n    jmp 1f\n"
    ".globl land_out\nland_out:\n    test %eax, %eax\n    jnz 2f\n    mov $4, %eax\n    jmp 1f\n"
    "2:\n    mov $98, %eax\n"
    "1:\n    cmp $0x55, %ecx\n    je 3f\n    mov $99, %eax\n"  // the match flags must survive
    "3:\n    add $0x28, %rsp\n    pop %r12\n    pop %rbx\n    pop %rbp\n    ret\n");

static uint32_t call_weapon(unsigned char* stub, void* stats, const void* branch, uint32_t weapon) {
    uint32_t result;
    asm volatile("call *%4"
                 : "=a"(result), "+D"(stats), "+S"(branch), "+d"(weapon)
                 : "r"(stub)
                 : "rcx", "r8", "r9", "r10", "r11", "memory", "cc");
    return result;
}

static uint32_t call_select(unsigned char* stub) {
    uint32_t result;
    asm volatile("call *%1" : "=a"(result) : "r"(stub) : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory", "cc");
    return result;
}

int main(int argc, char** argv) {
    alignas(64) static Gow3SwitchState state;
#ifdef _WIN32
    auto* code = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#else
    auto* code = static_cast<unsigned char*>(
        mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
#endif
    assert(code);
    unsigned char* weapon = code;
    unsigned char* button = code + 256;
    unsigned char* select = code + 512;
    const uintptr_t s = reinterpret_cast<uintptr_t>(&state);
    const uintptr_t targets[4] = {reinterpret_cast<uintptr_t>(&land_same), reinterpret_cast<uintptr_t>(&land_other),
                                  reinterpret_cast<uintptr_t>(&land_held), reinterpret_cast<uintptr_t>(&land_out)};
    assert(gow3_switch_build_weapon_stub(weapon, s, reinterpret_cast<uintptr_t>(&switch_weapon_return)) <= 256);
    assert(gow3_switch_build_button_stub(button, s, targets) <= 256);
    assert(gow3_switch_build_select_stub(select, s, reinterpret_cast<uintptr_t>(&switch_select_return)) <= 256);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
#endif

    alignas(16) static unsigned char stats[0x300];
    alignas(16) static unsigned char attack[0x40], other[0x40];
    attack[0x2f] = 0x12;                     // a switch branch to the Blades, L1 + Cross (button 5)
    attack[0x2c] = 5;
    other[0x2f] = 0;                         // any other branch, button 3
    other[0x2c] = 3;

    // Weapon check: switch branches pass whatever is in his hands; others are the game's.
    assert(call_weapon(weapon, stats, other, 0x12) == 7);
    assert(call_weapon(weapon, stats, attack, 0x10) == 1);
    assert(state.weapon == 0x10 && state.stats == reinterpret_cast<uintptr_t>(stats) && state.heartbeat == 2);

    // Button test, other branches: the game's own comparison.
    assert(run_button(button, other, 3) == 1);
    assert(run_button(button, other, 5) == 2);
    // L1 + Cross only to the weapon in his hands: Cestus in hand, the Blades branch fails...
    assert(run_button(button, attack, 5) == 4);
    // ...Blades in hand: the game's comparison (Cross takes it, another button does not).
    state.weapon = 0x12;
    assert(run_button(button, attack, 5) == 1);
    assert(run_button(button, attack, 3) == 2);

    // A request: the branch to the selected weapon is taken once, whatever was pressed.
    state.weapon = 0x10;
    stats[0x20a] = 0x12;
    state.pending = 1;
    assert(run_button(button, attack, -1) == 3);
    assert(!state.pending && state.granted == 1);
    assert(run_button(button, attack, -1) == 4);
    // A request for another weapon leaves this branch to the in-hand rule.
    stats[0x20a] = 0x13;
    state.pending = 1;
    assert(run_button(button, attack, 5) == 4 && state.pending);

    // Select: the switch attack started, the request is done.
    assert(call_select(select) == 0);
    assert(!state.pending);

    if (argc > 1) {
        // The code segment starts at file offset 0x4000 (image offset 0).
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
        assert(data.size() > 0x4000);
        const unsigned char* image = data.data() + 0x4000;
        const uint64_t size = data.size() - 0x4000;
        const uint64_t weapon_at = gow3_signature_find(image, size, &gow3_switch_weapon_site);
        const uint64_t button_at = gow3_signature_find(image, size, &gow3_switch_button_site);
        const uint64_t select_at = gow3_signature_find(image, size, &gow3_switch_select_site);
        assert(weapon_at != UINT64_MAX && button_at != UINT64_MAX && select_at != UINT64_MAX);
        assert(!std::memcmp(image + weapon_at, gow3_switch_weapon_original, GOW3_SWITCH_WEAPON_HOOK_LENGTH));
        assert(!std::memcmp(image + button_at, gow3_switch_button_original, GOW3_SWITCH_BUTTON_HOOK_LENGTH));
        assert(!std::memcmp(image + select_at, gow3_switch_select_original, GOW3_SWITCH_SELECT_HOOK_LENGTH));
        assert(!std::memcmp(image + button_at + GOW3_SWITCH_BUTTON_OUT, gow3_switch_button_out,
                            sizeof(gow3_switch_button_out)));
        std::printf("Switch sites: weapon check 0x%llx, button test 0x%llx, select 0x%llx\n",
                    static_cast<unsigned long long>(weapon_at), static_cast<unsigned long long>(button_at),
                    static_cast<unsigned long long>(select_at));
    }
#ifdef _WIN32
    VirtualFree(code, 0, MEM_RELEASE);
#else
    munmap(code, 4096);
#endif
    std::puts("PASS: switch attack hooks");
}
