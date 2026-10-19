// Runs the switch-attack diagnostic stubs (src/switch_hook.h) as the game reaches them: each one
// records and then goes on exactly as the replaced instructions would. Entries are called like
// the game's functions; the two exits are reached from a frame laid out like the function's own.
// With the prepared eboot.elf as argument it also finds the seven sites and the branch loop.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
#include "../src/switch_hook.h"

// Where the stubs resume: undo the replayed prologue and return a marker (entries), or finish the
// function's epilogue (exits; rax is the function's result).
extern "C" void land_entry_r14();   // push rbp; mov rbp,rsp; push r14
extern "C" void land_entry_rbx();   // push rbp; mov rbp,rsp; push rbx; push rax
extern "C" void land_entry_r15();   // push rbp; mov rbp,rsp; push r15
extern "C" void land_queue();       // movzx eax, byte [rdi+0xd2]
extern "C" void land_check_exit();  // pop r14; pop rbp; ret
extern "C" void land_eval_exit();   // pop r12; pop r13; pop r14; pop r15; pop rbp; ret
// The exits: a frame like the weapon check's / the branch test's, entered at the exit site.
extern "C" uint64_t run_check_exit(unsigned char* stub, const void* branch, uint32_t result);
extern "C" uint64_t run_eval_exit(unsigned char* stub, const void* branch, int32_t pressed, uint32_t result,
                                  const void* move_state);
extern "C" char eval_return[];
asm(".globl land_entry_r14\nland_entry_r14:\n    pop %r14\n    pop %rbp\n    mov $0x77, %eax\n    ret\n"
    ".globl land_entry_rbx\nland_entry_rbx:\n    pop %rax\n    pop %rbx\n    pop %rbp\n    mov $0x77, %eax\n    ret\n"
    ".globl land_entry_r15\nland_entry_r15:\n    pop %r15\n    pop %rbp\n    mov $0x77, %eax\n    ret\n"
    ".globl land_queue\nland_queue:\n    ret\n"
    ".globl land_check_exit\nland_check_exit:\n    pop %r14\n    pop %rbp\n    ret\n"
    ".globl land_eval_exit\nland_eval_exit:\n"
    "    pop %r12\n    pop %r13\n    pop %r14\n    pop %r15\n    pop %rbp\n    ret\n"
    // run_check_exit(stub rdi, branch rsi, result edx)
    ".globl run_check_exit\nrun_check_exit:\n"
    "    push %rbx\n    push %r14\n    mov $0x1234, %ebx\n    call 1f\n"
    "    cmp $0x1234, %ebx\n    je 2f\n    mov $99, %eax\n2:\n    pop %r14\n    pop %rbx\n    ret\n"
    "1:\n    push %rbp\n    mov %rsp, %rbp\n    push %r14\n    push %rbx\n    sub $0x10, %rsp\n"
    "    mov %rsi, %r14\n    mov $0x5555, %ebx\n    mov %edx, %eax\n    jmp *%rdi\n"
    // run_eval_exit(stub rdi, branch rsi, pressed edx, result ecx, move_state r8): stack args
    // [rbp+0x10..0x40] of the branch test, the caller's frame with the move state at [rbp-0x1c8].
    ".globl run_eval_exit\nrun_eval_exit:\n"
    "    push %rbp\n    mov %rsp, %rbp\n    push %rbx\n    push %r12\n    push %r13\n    push %r14\n    push %r15\n"
    "    sub $0x1d8, %rsp\n    mov %r8, -0x1c8(%rbp)\n"
    "    sub $0x40, %rsp\n    movq $0x10, 0(%rsp)\n    mov %edx, 8(%rsp)\n    movl $0x20, 0x10(%rsp)\n"
    "    movl $0x28, 0x18(%rsp)\n    movl $0x400, 0x20(%rsp)\n    movl $0x38, 0x28(%rsp)\n    movq $0x40, 0x30(%rsp)\n"
    "    mov $0x1234, %ebx\n    mov $0x4321, %r12d\n    call 1f\n"
    ".globl eval_return\neval_return:\n"
    "    cmp $0x1234, %ebx\n    jne 3f\n    cmp $0x4321, %r12d\n    je 2f\n3:\n    mov $99, %eax\n"
    "2:\n    add $0x40, %rsp\n    add $0x1d8, %rsp\n    pop %r15\n    pop %r14\n    pop %r13\n    pop %r12\n    pop %rbx\n"
    "    pop %rbp\n    ret\n"
    "1:\n    push %rbp\n    mov %rsp, %rbp\n    push %r15\n    push %r14\n    push %r13\n    push %r12\n    push %rbx\n"
    "    push %rax\n    mov %rsi, %rbx\n    mov $0x5555, %r12d\n    mov %ecx, %eax\n    mov $0x8, %edx\n    jmp *%rdi\n");

static uint64_t call_entry(unsigned char* stub, void* rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx, uint64_t r8,
                           uint64_t* rsi_after, uint64_t* rdx_after, uint64_t* rcx_after, uint64_t* r8_after) {
    uint64_t result;
    register uint64_t r8r asm("r8") = r8;
    asm volatile("call *%[stub]"
                 : "=a"(result), "+D"(rdi), "+S"(rsi), "+d"(rdx), "+c"(rcx), "+r"(r8r)
                 : [stub] "r"(stub)
                 : "r9", "r10", "r11", "memory", "cc");
    *rsi_after = rsi; *rdx_after = rdx; *rcx_after = rcx; *r8_after = r8r;
    return result;
}

static const Gow3SwitchRecord* last(const Gow3SwitchState& s) {
    return &s.records[(s.head - 1) & (GOW3_SWITCH_RING - 1)];
}

int main(int argc, char** argv) {
    alignas(64) static Gow3SwitchState state;
    auto* code = static_cast<unsigned char*>(mmap(nullptr, GOW3_SWITCH_SITES * GOW3_SWITCH_SLOT,
                                                  PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(code != MAP_FAILED);
    const uintptr_t lands[GOW3_SWITCH_SITES] = {
        reinterpret_cast<uintptr_t>(&land_entry_r14), reinterpret_cast<uintptr_t>(&land_check_exit),
        reinterpret_cast<uintptr_t>(&land_eval_exit), reinterpret_cast<uintptr_t>(&land_queue),
        reinterpret_cast<uintptr_t>(&land_entry_rbx), reinterpret_cast<uintptr_t>(&land_entry_r15),
        reinterpret_cast<uintptr_t>(&land_entry_r15)};
    unsigned char* stub[GOW3_SWITCH_SITES];
    for (unsigned i = 0; i < GOW3_SWITCH_SITES; ++i) {
        stub[i] = code + i * GOW3_SWITCH_SLOT;
        assert(gow3_switch_build_stub(stub[i], &gow3_switch_sites[i], reinterpret_cast<uintptr_t>(&state), lands[i]) <=
               GOW3_SWITCH_SLOT);
    }
    state.eval_return = reinterpret_cast<uintptr_t>(eval_return);

    alignas(16) static unsigned char stats[0x300], to_blades[0x40], other[0x40], move_state[0x100];
    stats[0x20a] = 0x12;
    stats[0x20b] = 0x10;
    stats[0x168] = 0xab;
    to_blades[0x2f] = 0x12;
    to_blades[0x2c] = 5;
    to_blades[0x10] = 0x40;
    other[0x2c] = 3;
    memset(move_state + 0xa8, 0x6e, 8);
    uint64_t rsi, rdx, rcx, r8;

    // Weapon check: the entry notes stats and weapon, the exit records branches to a weapon.
    assert(call_entry(stub[0], stats, reinterpret_cast<uintptr_t>(to_blades), 0x10, 0xc0, 0x80, &rsi, &rdx, &rcx, &r8) == 0x77);
    assert(rsi == reinterpret_cast<uintptr_t>(to_blades) && rdx == 0x10 && rcx == 0xc0 && r8 == 0x80);
    assert(state.check_stats == reinterpret_cast<uintptr_t>(stats) && state.check_weapon == 0x10 && state.head == 0);
    assert(run_check_exit(stub[1], to_blades, 1) == 1);
    assert(state.head == 1);
    const Gow3SwitchRecord* r = last(state);
    assert(r->seq == 1 && r->kind == GOW3_SWITCH_CHECK && r->q[0] == reinterpret_cast<uintptr_t>(to_blades));
    assert(r->q[1] == (1 | 0x10ull << 32) && r->q[2] == reinterpret_cast<uintptr_t>(stats) && (r->q[3] & 0xff) == 0xab);
    assert((r->q[4] >> 16 & 0xffff) == 0x1012 && (r->q[5] & 0xff) == 0x40);
    assert(!memcmp(&r->q[7], to_blades, 56));
    assert(run_check_exit(stub[1], other, 0) == 0 && state.head == 1);  // not a switch branch

    // Branch test exit: branches to a weapon, and branches a pressed button passes.
    assert(run_eval_exit(stub[2], to_blades, -1, 0, move_state) == 0 && state.head == 2);
    r = last(state);
    assert(r->kind == GOW3_SWITCH_EVAL && r->q[0] == reinterpret_cast<uintptr_t>(to_blades) && r->q[1] == (8ull << 32));
    assert(r->q[2] == (0xffffffffull | 0x20ull << 32) && r->q[3] == (0x28 | 0x400ull << 32) && r->q[4] == 0x40 &&
           r->q[5] == 0x10 && r->q[6] == reinterpret_cast<uintptr_t>(eval_return) && r->q[7] == 0x6e6e6e6e6e6e6e6eull);
    assert(!memcmp(&r->q[8], to_blades, 56));
    assert(run_eval_exit(stub[2], other, -1, 1, move_state) == 1 && state.head == 2);  // nothing pressed
    assert(run_eval_exit(stub[2], other, 3, 0, move_state) == 0 && state.head == 2);   // pressed, not passed
    assert(run_eval_exit(stub[2], other, 3, 1, move_state) == 1 && state.head == 3);   // pressed and passed
    assert(last(state)->seq == 3 && (int32_t)last(state)->q[2] == 3);
    state.eval_return = 0;  // another caller: no move read
    assert(run_eval_exit(stub[2], to_blades, 5, 1, nullptr) == 1 && state.head == 4 && last(state)->q[7] == 0);
    state.eval_return = reinterpret_cast<uintptr_t>(eval_return);

    // Input queue: the branch buffered, and the replaced movzx.
    move_state[0xd2] = 3;
    float when = 0.5f;
    uint64_t result;
    {
        register void* rdi_r asm("rdi") = move_state;
        register void* rsi_r asm("rsi") = to_blades;
        asm volatile("movd %[w], %%xmm0\n    call *%[s]" : "=a"(result), "+r"(rdi_r), "+r"(rsi_r)
                     : [s] "r"(stub[3]), [w] "r"(when) : "rcx", "rdx", "r8", "r9", "r10", "r11", "xmm0", "memory", "cc");
        assert(rdi_r == move_state && rsi_r == to_blades);
    }
    assert(result == 3 && state.head == 5);
    r = last(state);
    uint32_t bits;
    memcpy(&bits, &when, 4);
    assert(r->kind == GOW3_SWITCH_QUEUE && r->q[0] == reinterpret_cast<uintptr_t>(move_state) &&
           r->q[1] == reinterpret_cast<uintptr_t>(to_blades) && (uint32_t)r->q[2] == bits &&
           r->q[4] == 0x6e6e6e6e6e6e6e6eull && !memcmp(&r->q[6], to_blades, 56));

    // d-pad, weapon applied, select: arguments and stats, registers untouched.
    for (unsigned i = 4; i < 7; ++i) {
        assert(call_entry(stub[i], stats, 0x13, 0x22, 0x33, 0x44, &rsi, &rdx, &rcx, &r8) == 0x77);
        assert(rsi == 0x13 && rdx == 0x22 && rcx == 0x33 && r8 == 0x44);
        r = last(state);
        assert(r->kind == GOW3_SWITCH_DPAD + (i - 4) && r->seq == state.head && r->q[0] == reinterpret_cast<uintptr_t>(stats));
        assert(r->q[1] == 0x13 && r->q[2] == 0x22 && r->q[3] == 0x33 && (r->q[5] & 0xff) == 0xab &&
               (r->q[6] >> 16 & 0xffff) == 0x1012);
    }

    if (argc > 1) {
        // The code segment starts at file offset 0x4000 (image offset 0).
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
        assert(data.size() > 0x4000);
        const unsigned char* image = data.data() + 0x4000;
        const uint64_t size = data.size() - 0x4000;
        for (const Gow3SwitchSite& site : gow3_switch_sites) {
            const uint64_t at = gow3_signature_find(image, size, &site.site);
            assert(at != UINT64_MAX && !memcmp(image + at, site.original, site.length));
            std::printf("Switch site %s 0x%llx\n", site.name, static_cast<unsigned long long>(at));
        }
        const uint64_t loop = gow3_signature_find(image, size, &gow3_switch_eval_return);
        assert(loop != UINT64_MAX);
        std::printf("Branch loop return 0x%llx\n", static_cast<unsigned long long>(loop));
    }
    munmap(code, GOW3_SWITCH_SITES * GOW3_SWITCH_SLOT);
    std::puts("PASS: switch attack diagnostic hooks");
}
