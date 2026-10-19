// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "cpu/x86_ops.h"
#include "xdk/hle.h"
#if !defined(_WIN32)
#include <cmath>

#include "app/platform.h"  // app::HostAspect
#include "core/log.h"
#endif

// the frame step the engine is working with, seconds, published by sub_001232F0 below
namespace {
std::atomic<double> g_dt{1.0 / 60.0};

// Each frame-rate wrapper below can be switched off for the run, so a defect can be bisected
// against the stock behaviour without a rebuild: SIMSBO_NO_STEP (the frame step itself).
// Off means the recompiled function runs exactly as the image wrote it.
bool HookOff(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && std::strcmp(v, "0") != 0;
}
}  // namespace

// D3DDevice_InsertCallback(Flags, pCallback, Context) at 0x24D030.
//
// Confirmed against the guest body, not assumed: 0x24D04B writes the push token 0x00081D8C,
// 0x24D043 loads the second stack argument ([esp+0xC] at entry, after its push esi) and
// 0x24D051 stores it at token+4, 0x24D047 loads the third ([esp+0x10]) and 0x24D05A stores
// it at token+8, and the first argument ([esp+8], loaded at 0x24D054 and tested at 0x24D058)
// only selects the longer token form. So a[1] is the callback and a[2] its context, and the
// function is retn 0xC (0x24D073 / 0x24D098). Guest .text reaches it through the thunk at
// 0x16E6B0 (jmp 0x24D030 at 0x16E6B0).
//
// On the console this writes a callback token into the push buffer; the D3D interrupt handler
// runs pCallback(Context) once the GPU reaches it. The SDK has no push-buffer callback path -
// the tokens land in a scratch buffer that the renderer walks for draws only - so the callback
// would never run and an engine thread waiting on the semaphore that callback releases would
// idle-wait forever.
//
// The host renderer has no GPU timeline the guest can be behind, so the callback is run
// immediately: by the time this returns, everything queued before it has been handed to the
// render thread.
HLE_STDCALL(D3DDevice_InsertCallback, 3) {
    uint32_t callback = a[1], context = a[2];
    if (callback != 0) ::kernel::CallGuest(ctx, callback, {context});
    return 0;
}

// The frame step, sub_001232F0(this in ecx, float elapsed_seconds on the stack, retn 4 at
// 0x12338B / 0x1233A4 / 0x1233E7), called once a frame from the frame tick sub_00123430 at
// 0x1234A7.
//
// The engine measures real time and then throws the precision away. The clock object is built
// by sub_001216A0: it installs the vtable at 0x2EC1C8 (mov [esi], 0x2EC1C8 at 0x1216AA),
// publishes itself at [0x35D1A0] (0x1216B0) and calls QueryPerformanceFrequency once
// (push 0x35D198 at 0x1216B8, call at 0x1216C4). That XAPI pair is in the image at
// 0x22AC60 / 0x22AC71: the counter does rdtsc at 0x22AC64 and stores eax / edx to its
// argument at 0x22AC66 / 0x22AC68, and the frequency writes the fixed 733,333,333
// (mov dword ptr [eax], 0x2BB5C755 at 0x22AC79, its high dword cleared at 0x22AC75) - the
// clock is the console CPU clock, so its seconds are real. The elapsed vfuncs
// sub_00121710 (vtable+0x10) and sub_00121750 (vtable+0x14) both call that counter
// (0x121717 / 0x121757) and divide by the frequency at [0x35D198] (fildll at
// 0x121736 / 0x121776, fdivrp at 0x12173C / 0x12177C).
//
// The frame tick sub_00123430 stamps its +0x10 timestamp through clock vfunc+0xC
// (push edx at 0x123453, call at 0x123457), takes elapsed through vfunc+0x10
// (push edi at 0x123475, call at 0x12347D, fstp to a float at 0x123480), restamps +8
// (call at 0x12348D) and passes that float to the quantiser (push ecx at 0x12349E,
// call sub_001232F0 at 0x1234A7). The quantiser divides by the tick-size float at
// [0x35D218] (fdiv at 0x123343 and at 0x1233AE), truncates with fistp to the tick count
// at [0x30AAD0] (0x123366 / 0x1233D1), clamps that count to 1..15 on its fixed-step path
// (cmp eax, 1 at 0x123371, cmp eax, 0xF at 0x12338E) and adds it to the running total at
// [0x35D230] (0x12337F / 0x123398 / 0x1233E0). The tick size itself is set once by the
// frame object's constructor sub_00123A00: 0.02 s from [0x2E2428] (movss at 0x123A5B)
// when [0x2E3D78] is 1 (cmp at 0x123A70), else 1/60 s from [0x2E8838] (movss at
// 0x123A7E), stored to [0x35D218] at 0x123A8B.
//
// The caller then makes the one delta the rest of the engine multiplies by, at
// [0x30AAD4] (movss at 0x123542). That global is the confirmation: 143 read xrefs in
// the image, 126 of them in 70 recognised functions, against 3 writes (the tick at
// 0x123542 and a scoped save / restore pair in sub_001EA360 at 0x1EA69A and
// sub_001E9190 at 0x1E9198). Its reciprocal is stored beside it at [0x30AAD8]
// (0x12355D) and appears to have no other reader.
//
// Two caller paths make that delta, and both floor a frame at one whole tick:
//   - fixed-step (byte [this+5] set, tested at 0x1234AC): dt = tick count * tick size
//     (cvtsi2ss of the tick count at [0x30AAD0] at 0x1234B4, mulss [0x35D218] at
//     0x1234BC). The quantiser's own fixed path (cmp byte ptr [ecx+5], 0 at 0x1232F1)
//     feeds it not the caller's elapsed but the largest of four history floats at
//     [0x30AAE0] / [0x30AAE4] / [0x30AAEC] / [0x30AAF0] (movss at 0x1232FB / 0x123303 /
//     0x123313 / 0x12331B).
//   - the default path (that byte clear): the elapsed is clamped to
//     [tick size, tick size * 15.0] (movss [0x35D218] at 0x1234C6, mulss [0x2E3A50]
//     at 0x1234E0, the 15.0 at [0x2E3A50]) and averaged with the two previous clamped
//     values (addss at 0x123518 / 0x12351C, mulss by the 1/3 at [0x2E873C] at
//     0x123520, history stored at 0x123528 / 0x123530).
// Either way a frame shorter than 16.7 ms still advances the world by a whole 1/60 s:
// at 144 fps that is 144 * 1/60 = 2.4 s of game time per real second, and uncapped it
// grows without bound.
//
// That byte is clear in play as far as the image shows: the constructor clears it
// (mov [esi+5], bl at 0x123A69, bl zeroed by xor ebx, ebx at 0x123A35) and the game's
// frame subclass clears it again on init (mov byte ptr [esi+5], 0 at 0x165ED5 in
// sub_00165EC0), and no instruction in the image sets it. It is a fixed-step switch
// nobody flips.
//
// The fix therefore keeps the engine's measurement and replaces only the quantiser,
// and lets the frame tick take its own product path while it runs (see
// sub_00123430 below): ticks of 1 ms, the engine's own maximum-step ceiling kept
// (its 15 ticks of 1/60 s = 0.25 s), and a seconds-per-tick of dt / ticks written
// back into [0x35D218], so the caller's `ticks * [0x35D218]` at 0x1234B4 / 0x1234BC
// is exactly the time that really passed. dt is then real seconds at any frame
// rate - 60, 144, 240 or uncapped.
//
// Nothing else depends on the unit: [0x30AAD0] (the tick count) has three other
// readers - the caller itself at 0x1234B4 and the getter sub_001233F0 (byte cmp at
// 0x1233F0, tick count read at 0x1233F4), which returns 1 whenever that byte is
// clear, and whose only caller (sub_001651A0 at 0x1651C8) runs outside the tick,
// after the byte has been put back; and the running total [0x35D230] has one other
// reader, at 0x129E49.
X86_FUNC(__imp__sub_001232F0);
X86_FUNC(sub_001232F0) {
    if (HookOff("SIMSBO_NO_STEP")) { __imp__sub_001232F0(ctx, base); return; }
    constexpr double kTick = 0.001;  // seconds per tick, 1/60 before this
    constexpr int kMaxTicks = 250;   // 0.25 s, the engine's own ceiling of 15 ticks of 1/60 s
    const float elapsed = X86_MEMF32(ctx.esp + 0x4u);
    // not (elapsed > 0): a NaN or a negative reading takes the smallest step, as the clamp did
    const double dt = elapsed > 0.0f ? std::min(double(elapsed), kMaxTicks * kTick) : kTick;
    const int ticks = std::clamp(int(dt / kTick + 0.5), 1, kMaxTicks);
    X86_MEMF32(0x35D218u) = float(dt / ticks);  // ticks * this == the time that really passed
    X86_MEM32(0x30AAD0u) = uint32_t(ticks);
    X86_MEM32(0x35D230u) = X86_MEM32(0x35D230u) + uint32_t(ticks);
    ctx.eax = uint32_t(ticks);
    g_dt.store(dt, std::memory_order_relaxed);
    ctx.esp += 8;  // retn 4
}

// The frame tick itself, sub_00123430(this in ecx), reached through the game frame
// subclass's thunk sub_00165E30 at vtable+0x18 of 0x2EED28. It takes no stack arguments.
//
// This wrapper writes nothing of its own. It sets the frame object's fixed-step byte
// at this+5 for the duration of the original call and puts the saved value back
// afterwards, so the tick's own product path (cmp byte ptr [esi+5], 0 at 0x1234AC,
// then cvtsi2ss at 0x1234B4 and mulss [0x35D218] at 0x1234BC) makes the delta at
// [0x30AAD4] (movss at 0x123542) out of the host quantiser's tick count and written
// back tick size, instead of the default path's three-frame average of an elapsed
// clamped to a whole tick at the bottom (0x1234C6-0x123530). Without it, replacing
// the quantiser alone would leave that average in charge: it would cap a 60 fps
// frame's delta at 15 of the new 1 ms ticks and floor a fast frame at one of them.
//
// The byte is not state anything else reads during the tick: inside the image its
// only readers are the quantiser (cmp at 0x1232F1, whose host replacement above
// does not consult it), this caller test at 0x1234AC, and the tick-count getter
// sub_001233F0 (byte cmp at 0x1233F0), whose sole call site at 0x1651C8 is in
// sub_001651A0, outside this call, where it sees the restored value. The saved
// byte is restored whatever it was, so a build that does flip the switch keeps
// the behaviour it asked for.
X86_FUNC(__imp__sub_00123430);
X86_FUNC(sub_00123430) {
    if (HookOff("SIMSBO_NO_STEP")) { __imp__sub_00123430(ctx, base); return; }
    const uint32_t frame = ctx.ecx;
    const uint8_t fixed_step = X86_MEM8(frame + 0x5u);
    X86_MEM8(frame + 0x5u) = 1;
    __imp__sub_00123430(ctx, base);  // pops its own return
    X86_MEM8(frame + 0x5u) = fixed_step;
}

#if !defined(_WIN32)
// The aspect getter, sub_001287C0 (video vtable off_2ECA78 slot +0x90, this in
// ecx, float returned on the x87 stack, plain ret). It is the title's one
// aspect source: docs/guest-projection.md proves every projection builder
// (sub_00130BC0 via sub_001341F0: m[0] = cot / aspect, m[5] = cot) takes its
// aspect from here, and no 2-D, HUD, text or movie path reads it or the
// widescreen byte behind it. The dashboard byte already reports widescreen on
// this host (the kernel's ExQueryNonVolatileSetting 8 answer), so the authored
// return is already 1.7777778; wider than 16:9 has no title mode.
//
// Host Hor+: when app::HostAspect() is active (ultrawide on, aspect wider than
// 16:9), calls that reach the getter from the projection path get the widened
// frame's aspect instead - the same number the Vulkan frame target is built
// at (lround(height * aspect) / height over the guest's present height), so
// the projection, the target and the present agree exactly and only the
// horizontal field of view grows; m[5] (vertical) is bit-identical. The gate
// is the guest return address: only the proven projection call sites below
// (docs/widescreen-flag.md) see the widened aspect, every other caller - and
// every call with widening off - runs the recompiled getter untouched.
// SIMSBO_NO_WIDE=1 bisects the override off without a rebuild.
X86_FUNC(__imp__sub_001287C0);
X86_FUNC(sub_001287C0) {
    const float aspect = HookOff("SIMSBO_NO_WIDE") ? 0.0f : app::HostAspect();
    if (aspect <= 0.0f) { __imp__sub_001287C0(ctx, base); return; }
    // Return addresses of the `call [reg+0x90]` sites that feed a projection
    // builder: the four camera setters and sub_0001D870's second view.
    static constexpr uint32_t kProjectionReturns[] = {0x15488u, 0x164B6u, 0x16723u, 0x16C96u,
                                                      0x1DBA4u, 0x1DEECu, 0x1E424u, 0x1E44Bu,
                                                      0x1E5A4u, 0x1E5CBu};
    const uint32_t ret = X86_MEM32(ctx.esp);
    bool projection = false;
    for (uint32_t site : kProjectionReturns) projection |= ret == site;
    LOG_FIRST(16, "simsbo aspect: getter called from %08X (%s), host aspect %.4f", ret,
              projection ? "projection path" : "authored", double(aspect));
    __imp__sub_001287C0(ctx, base);  // authored value on the x87 stack, esp past the return
    if (!projection) return;
    const uint32_t height = X86_MEM32(ctx.ecx + 0x248u);  // present height ([this+146], getter +0x98's field)
    if (!height) return;
    const uint32_t wide_w = uint32_t(std::lround(float(height) * aspect));
    X87_ST(0) = double(float(wide_w) / float(height));
}
#endif
