// The console cheats, entered the game's own way (common/cheats.h, offered in the F1 panel).
//
// Recogniser. sub_00011F20 runs from the per-frame update sub_00012280 (0x12393: ecx = the
// recogniser object at [app+0x1E4], one stack argument = the active pad) whenever a pad reports a
// fresh press. It keeps the last six single-button presses in a ring at [this+0x10], eight bytes a
// press (the button word, then its expiry as a float: now * 1000.0 + 1500.0, fmul dbl_2E2290 at
// 0x11F77, fadd dword_2E2288 = 1500.0f at 0x11F8B; sub_000119D0 clears a press once now reaches its
// expiry), insists a press carry exactly one bit (sub_00011990), and matches the ring against an
// eight-row table. On a match it calls the setter (0x12048: mov ecx, 0x310B98; 0x1204D: push row;
// 0x1204E: call sub_00072010) and empties the ring.
//
// Table. sub_000720A0, called by the options constructor sub_00072A80 (0x730FA; its caller
// sub_000F76C0 passes ecx = 0x310B98 at 0xF76C0), writes the table into the options object at
// 0x310B98: six button words a row from [ecx+6] (0x720E3: lea edx, [ecx+6]), rows twelve bytes
// apart, then one combined-mask word a row at [ecx+0x66] (0x720E0: lea eax, [ecx+66h]; the OR loop
// at 0x721B0-0x721E6) and the OR of every row at [ecx+4] (0x721EB), the word the recogniser tests a
// fresh press against (0x11F47: and eax, [0x310B9C]). The table is built in code, which is why no
// byte search of the XBE finds the sequences. The masks are in the game's own pad layout, built by
// sub_00126270 from the XInput state at [pad+0x98]: the analog button bytes give 0x40 A (0x1262C0),
// 0x20 B (0x1262E1), 0x80 X (0x1262D6), 0x10 Y (0x1262CB), 0x2 Black (0x1262B5), 0x1 White
// (0x1262AA), 0x4 L (0x126294) and 0x8 R (0x12629F); the D-pad bits of wButtons give 0x1000 Up
// (0x1262F0), 0x4000 Down (0x1262E8), 0x8000 Left (0x1262F8) and 0x2000 Right (0x126300). So:
//
//   row 0   R L Down Black Left B   0008 0004 4000 0002 8000 0020
//   row 1   L Black Right X Left    0004 0002 2000 0080 8000
//   row 2   R Left B Down White     0008 8000 0020 4000 0001
//   row 3   Black Up Y Down R       0002 1000 0010 4000 0008
//   row 4   Black Down R L Down Y   0002 4000 0008 0004 4000 0010
//   row 5   L R A Down Black        0004 0008 0040 4000 0002
//   row 6   L B A Black Down Y      0004 0020 0040 0002 4000 0010
//   row 7   R Black Down            0008 0002 4000
//
// (each row written word by word in sub_000720A0: row 0 at 0x72108 [ecx+6] = 8, 0x720F0 [ecx+8] =
// 4, 0x720C0 [ecx+0xA] = 0x4000, 0x7210B [ecx+0xC] = 2, 0x720E6 [ecx+0xE] = 0x8000, 0x7210F
// [ecx+0x10] = 0x20; rows 1-7 likewise at [ecx+0x12] onward, in the order the header lists.)
//
// Setter and effect word. sub_00072010 (ecx = options, one stack argument = row, retn 4 at
// 0x72097) flips bit `row` of the word at [eax+0x76] (0x72028: xor [eax+76h], dx) - the word at
// 0x310C0E - and plays the cheat-on sound 0x350B962D (push at 0x72078) or the cheat-off sound
// 0x4A7EF64D (push at 0x7207F) through sub_0006B050. sub_000F9AC0, a method in the same
// front-end vtable cluster (slot at 0x2EADE4), clears the word (word_310C0E = 0 is its first
// write). The word's SimAntics reader is the global getter sub_001E9680: cases 46 and 47 both
// hand it out (0x1E969F and 0x1E96BD call the
// mask getter sub_00071FC0, which returns [ecx+76h] at 0x71FCD), so the cheat gnome's Edith trees
// see row n as flag n+1 of globals 46 and 47. Rows 0-2 have no native reader but that getter: the
// gnome appears and offers Give Money and Fill Motives. The other rows are also read natively
// through the bit test sub_00071FE0 ([ecx+76h] & (1 << row) at 0x71FEE-0x71FFD): row 3 in the
// build/buy item code (sub_0009A910 returns an item's locked byte unless row 3 is set), row 4 in
// the sixteen-entry list sub_00077440/sub_00076DD0, row 5 in the SimAntics primitive dispatcher
// sub_001CF3C0 (cases 40 and 41 return 0x7FFF for entries 19-22 and 29-32 while it is set), row 6
// in the unlockables lock test sub_0002BD90 (called from the Create-a-Family item browsers
// sub_0002D2D0/sub_0002D330), and row 7 in sub_000F7CA0, which mirrors the bit into a flag of the
// object it updates every frame.
//
// The latch. Every one of those functions first checks the byte at 0x3DCF70 (recogniser's caller
// at 0x122CB, the setter at 0x72010, the getter at 0x71FC0, the bit test at 0x71FE0): while it is
// set, codes are neither recognised nor entered and the mask reads as zero. The state entries
// sub_000E5370 (0xE5388), sub_000E74A0 (0xE7506), sub_000F7630 (0xF76AC) and sub_000F8770
// (0xF87CC) all clear it; no instruction in this image sets it.
//
// So the host enters a code the way a matched sequence does: the same call, from the same place in
// the frame. The override of sub_00012280 below calls sub_00072010 for every code the F1 panel
// asked for, on the title thread, before the frame's own input pass, and only under the same
// conditions the game itself recognises codes under: the latch clear (0x122CB), the pad manager
// present (0x122DE: dword_35D3F0 != 0) and the recogniser object present (0x12372:
// [app+0x1E4] != 0).
#include <atomic>
#include <cstdint>

#include "common/cheats.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

// A guest address the host may read: inside one of the regions the guest maps (sdk/runtime/kernel/
// memory.h) and on committed pages, since a stale pointer into the reserved but uncommitted user
// range faults the host on the first read.
bool GuestPtr(uint32_t va, uint32_t size) {
    if (size == 0 || va + size < va) return false;
    const uint32_t end = va + size;
    const bool in_range =
        (va >= mem::kUserLow && end <= mem::kUserHigh) ||
        (va >= mem::kContiguousBase && end <= uint32_t(mem::kContiguousBase + mem::kPhysicalSize)) ||
        (va >= mem::kSystemBase && end <= uint32_t(mem::kSystemBase + mem::kSystemSize));
    if (!in_range) return false;
    for (uint32_t page = va & ~(mem::kPage - 1u); page < end; page += mem::kPage)
        if (!mem::IsCommitted(page)) return false;
    return true;
}

// The options object the code table and the mask live in: sub_000F76C0 constructs it in place with
// ecx = 0x310B98 (0xF76C0) and the recogniser passes the same address to the setter (0x12048).
constexpr uint32_t kOptions = 0x310B98u;
// [options+4]: the OR of every table row, written at 0x721EB; zero until sub_000720A0 has run
constexpr uint32_t kValidMask = kOptions + 4u;
// [options+0x76]: the cheat mask itself (0x72028: xor [eax+76h], dx)
constexpr uint32_t kMask = kOptions + 0x76u;
// the cheat latch: while set, the getter sub_00071FC0 returns zero (0x71FC0-0x71FCC)
constexpr uint32_t kLatch = 0x3DCF70u;
// the pad manager the frame update checks before it recognises codes (0x122DE)
constexpr uint32_t kPadManager = 0x35D3F0u;

// sub_00072010: the options object in ecx, the table row as its one stack argument (retn 4)
constexpr uint32_t kEnterCode = 0x72010u;
constexpr hle::Map kEnterAbi = hle::Make({hle::ecx, hle::stack});

// the cheat mask as the gnome sees it: the latch hides it, exactly as sub_00071FC0 does
uint32_t GameMask() {
    if (!GuestPtr(kOptions, 0x78)) return 0;
    if (*static_cast<const uint8_t*>(kernel::HostPtr(kLatch)) != 0) return 0;
    return kernel::R16(kMask);
}

const char* Name(int code) {
    switch (code) {
        case cheats::kGnome: return "Cheat gnome";
        case cheats::kMoney: return "Give Money";
        case cheats::kMotives: return "Fill Motives";
        case cheats::kObjects: return "Unlock All Objects";
        case cheats::kLocations: return "Unlock All Locations";
        case cheats::kSocials: return "Unlock All Socials";
        case cheats::kSkins: return "Unlock All Skins";
        case cheats::kTeamPhoto: return "Team picture";
    }
    return "unknown";
}

// codes the panel asked to enter, one bit each; a second click before the title thread takes the
// first cancels it, as entering the code twice would
std::atomic<uint32_t> g_toggle{0};

// The title thread, at the top of its frame: enter what the panel asked for. The conditions are
// checked again here, in the game's own form (the addresses in the file header), because the
// click came from the render thread a frame ago.
void EnterPending(X86Context& ctx) {
    const uint32_t pending = g_toggle.exchange(0, std::memory_order_relaxed);
    if (!pending) return;
    const bool latch_clear = *static_cast<const uint8_t*>(kernel::HostPtr(kLatch)) == 0;  // 0x122CB
    const bool pads = kernel::R32(kPadManager) != 0;                                      // 0x122DE
    const bool recogniser = kernel::R32(ctx.ecx + 0x1E4u) != 0;                           // 0x12372
    if (!latch_clear || !pads || !recogniser) {
        LOGI("cheats: codes %02X not entered, the game was not in a state to take them", pending);
        return;
    }
    // flags are live only across tail calls, but the update reads them on entry: keep them as found
    const uint32_t cf = ctx.cf, zf = ctx.zf, sf = ctx.sf, of = ctx.of, pf = ctx.pf, df = ctx.df;
    for (int code = 0; code < 8; code++) {
        if (!(pending & (1u << code))) continue;
        hle::Call(ctx, kEnterCode, kEnterAbi, {kOptions, uint32_t(code)});
        const uint32_t mask = GameMask();
        LOGI("cheats: entered %s (code %d): %s, the game's cheat mask is now %02X", Name(code), code,
             (mask >> code) & 1u ? "on" : "off", mask);
    }
    ctx.cf = cf;
    ctx.zf = zf;
    ctx.sf = sf;
    ctx.of = of;
    ctx.pf = pf;
    ctx.df = df;
}

}  // namespace

namespace cheats {

bool InGame() {
    if (!GuestPtr(kOptions, 0x78)) return false;
    if (*static_cast<const uint8_t*>(kernel::HostPtr(kLatch)) != 0) return false;
    // the table is built in code at startup: its combined mask is zero until sub_000720A0 has run
    return kernel::R16(kValidMask) != 0;
}

bool On(Code c) {
    const uint32_t bit = 1u << c;
    return ((GameMask() ^ g_toggle.load(std::memory_order_relaxed)) & bit) != 0;
}

void Toggle(Code c) {
    if (InGame()) g_toggle.fetch_xor(1u << c, std::memory_order_relaxed);
}

}  // namespace cheats

X86_FUNC(__imp__sub_00012280);

// sub_00012280: the application's per-frame update (vtable slot at 0x2E2234), whose first act on
// a fresh press is the game's own cheat-code recogniser; pending codes are entered just ahead of
// it, under the conditions the update itself checks at 0x122CB, 0x122DE and 0x12372
X86_FUNC(sub_00012280) {
    LOG_FIRST(1, "cheats: frame hook reached");
    if (g_toggle.load(std::memory_order_relaxed)) EnterPending(ctx);
    __imp__sub_00012280(ctx, base);
}
