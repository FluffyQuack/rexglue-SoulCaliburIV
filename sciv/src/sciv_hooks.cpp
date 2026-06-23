// sciv - ReXGlue Recompiled Project
//
// Mid-ASM hooks and function-level fixups (the GoldenEye `ge_hooks.cpp` /
// MSXX `metalslugxx_hooks.cpp` analogue). See include/rex/hook.h for the
// authoring API and sciv_hooks.toml for the [[midasm_hook]] wiring.
//
// Currently this file holds only TEMPORARY Step-11 bring-up diagnostics; remove
// them (and sciv_hooks.toml) per Step 13 once the type-registry blocker closes.

#include <rex/hook.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <rex/chrono/clock.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

#include "sciv_crash.h"
#include "sciv_settings.h"

// ===========================================================================
// PERMANENT runtime fixes (kept when the temporary Step-11 diagnostics below
// are removed). Wired via [[midasm_hook]] entries in sciv_fixups.toml.
// ===========================================================================

// Null-source filter-read guard in obj_update_optional_source (guest 0x821B1368).
//
// That function takes (r3 = object, r4 = optional *source* object). It is called
// from three sites; the one at 0x822E0B34 passes r4 = 0 (no source). r24 holds
// the source across the whole body and is null-checked three times (0x821B138C,
// 0x821B14DC, 0x821B16B4). On the r24 == 0 branch the gate at 0x821B16B4 falls to
// 0x821B1DF0; when the object's two control floats are momentarily equal
// (60(r27) == 64(r27)) it branches to 0x821B21A0, which -- unlike every other
// r24 access in the function -- reads the source's filter-mode halfword WITHOUT a
// null check:
//
//     821b21f8: lis  r10,1
//     821b2200: ori  r10,r10,60778      ; r10 = 0x1ED6A
//     821b2204: lhzx r10,r24,r10        ; r24 = 0  ->  read of guest 0x1ED6A
//     821b2208: cmplwi cr6,r10,2        ; filter == 2 ? set : clear bits
//
// With r24 == 0 this reads guest 0x1ED6A. On a real console that low address
// reads back benign (the value is != 2, so the "clear source-filter bits" path
// runs); in the recomp guest 0x1ED6A is unmapped, so it faults -- an access
// violation reading host 0x10001ED6A (== guest 0x1ED6A); see logs/sciv_033.log.
// It is intermittent because it needs both r4 == 0 AND the two floats equal.
//
// Fix: when r24 == 0, reproduce the benign hardware read by setting r10 = 0
// (filter mode 0 -> the != 2 / clear-bits path) and skip the faulting load by
// jumping past it to 0x821B2208, where the original compare/clear logic resumes
// unchanged. When r24 != 0 (a real source) we return false and the original
// lhzx executes normally. Verified against Assets/SCIV/default.disasm.txt.
bool sciv_sub_821B1368_null_source_filter_guard(PPCRegister& r24, PPCRegister& r10) {
  if (r24.u32 != 0) {
    return false;  // real source object: execute the original lhzx
  }
  r10.u64 = 0;  // no source: filter mode 0 (matches the benign console read)
  return true;  // skip the faulting load; continue at loc_821B2208
}

// [Game] SkipLogos -- IMPLEMENTED via scoped guest-clock acceleration.
//
// RE + runtime probing established that the boot intro (a "now loading" phase
// then the five logos BANDAI NAMCO / Havok / CRI / SoulCalibur / Project Soul)
// is one statically-allocated modal (state 0x82891D50) driven by the generic
// modal_display_loop (sub_824307F0) on the boot thread. Its per-logo fades AND
// holds are paced by a WALL-CLOCK time source the guest reads via the PowerPC
// time-base register (mftb), NOT by loop-iteration or vblank rate (both proven
// not to matter -- forcing a non-blocking wait spun the loop ~1e6/s and
// vsync=false gave ~1000 Hz vblank, yet the holds stayed 4 s). Synthetic START
// injection only early-outs each logo's middle "wait" (the fades are not
// Start-interruptible), so input alone is a speed-up, not a true skip.
//
// In the recomp, mftb -> REX_QUERY_TIMEBASE() -> Clock::QueryGuestTickCount(),
// which honours the runtime's guest_time_scalar. So accelerating that scalar
// while the intro modal is the one running collapses the fades AND the holds
// together -- a true (near-instant) skip -- without bailing the loop's exit flag
// (which strands the game on a white screen by skipping the title hand-off).
//
// Scoping is the whole game here: the scalar is global, so it must be raised
// ONLY while modal_display_loop is iterating for the boot-intro modal (r31 ==
// 0x82891D50) and restored to 1x the instant that modal completes, so it can
// never leak into menus/gameplay. Two midasm hooks (sciv_fixups.toml) bracket
// the loop body: a per-iteration "tick" hook raises the scalar (and defensively
// restores it if the loop ever runs for a *different* modal while still scaled),
// and a "done" hook on the loop's exit path restores it and latches a one-shot
// so a later reuse of state 0x82891D50 is never re-accelerated. The vblank wait
// is event-driven (not duration-based), so it is unaffected and the loop still
// renders at ~60 Hz -- the logos simply fade/hold in fast-forward.
namespace {
constexpr uint32_t kBootIntroModalState = 0x82891D50u;

// How much faster the guest clock runs during the boot intro. Overridable at
// runtime via SCIV_LOGO_TIMESCALE for tuning (clamped to a sane range).
double ScivLogoTimeScale() {
  static const double scale = [] {
    if (const char* v = std::getenv("SCIV_LOGO_TIMESCALE")) {
      double d = std::atof(v);
      if (d >= 1.0 && d <= 1000.0) {
        return d;
      }
    }
    return 12.0;
  }();
  return scale;
}

bool g_sciv_intro_scaled = false;  // scalar currently raised?
bool g_sciv_intro_done = false;    // first boot intro already handled (one-shot)
}  // namespace

// Asset-phase watcher on asset_loader (0x82420D90). r3 = descriptor; the filename
// pointer is at [r3+0]. While the guest clock is accelerated for the boot intro,
// this watches for the first opening-cinematic asset and restores the clock to 1x
// so the cinematic plays at normal speed (the user's bug: it ran too fast).
//
// Boot opens, in order: the bulk "now loading" archives (bgm/movie/voice/se.afs,
// *.olt, effects, fonts) interspersed with the five logos (logo_bng, logo_namco,
// logo_havok, logo_cri, logo_soul), and ONLY AFTER the last logo the intro demo's
// own assets: D:\event\scene\scene02b.bin and D:\event\segment\scene02b_segment*.
// So "event\scene\" / "event\segment\" is the precise logo->cinematic boundary.
// (Match the subpath, not bare "event": D:\event\event.olt loads during the bulk
// phase, long before the logos finish.) Env SCIV_LOG_ASSETS additionally logs
// every open, for re-investigation.
void sciv_intro_asset_watch(PPCRegister& r3) {
  const bool log_all = std::getenv("SCIV_LOG_ASSETS") != nullptr;
  if (g_sciv_intro_done && !log_all) {
    return;  // boundary already handled; nothing to watch
  }
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) {
    return;
  }
  uint8_t* base = ks->memory()->virtual_membase();
  uint32_t name_ptr = rex::memory::load_and_swap<uint32_t>(base + r3.u32);
  if (!name_ptr) {
    return;
  }
  std::string name = rex::memory::load_and_swap<std::string>(base + name_ptr);
  if (log_all) {
    REXKRNL_INFO("[sciv] asset_open: {}", name);
  }
  if (g_sciv_intro_done || !g_sciv_intro_scaled) {
    return;
  }
  if (name.find("event\\scene") != std::string::npos ||
      name.find("event\\segment") != std::string::npos) {
    rex::chrono::Clock::set_guest_time_scalar(1.0);
    g_sciv_intro_scaled = false;
    g_sciv_intro_done = true;
    REXKRNL_INFO("[sciv] intro cinematic ({}) loading; guest clock restored to 1x", name);
  }
}

// Per-iteration hook at the top of modal_display_loop's body (guest 0x82430818).
// r31 is the active modal's state object. Raise the guest clock while the
// boot-intro modal is the one iterating; if some other modal ever runs while we
// are still scaled (i.e. our exit hook somehow did not fire), restore 1x.
void sciv_intro_clock_tick(PPCRegister& r31) {
  if (g_sciv_intro_done) {
    return;
  }
  const bool is_intro = (r31.u32 == kBootIntroModalState) && sciv::config().skip_logos;
  if (is_intro) {
    if (!g_sciv_intro_scaled) {
      rex::chrono::Clock::set_guest_time_scalar(ScivLogoTimeScale());
      g_sciv_intro_scaled = true;
      REXKRNL_INFO("[sciv] boot-intro: guest clock accelerated {}x (skip logos)",
                   ScivLogoTimeScale());
    }
  } else if (g_sciv_intro_scaled) {
    rex::chrono::Clock::set_guest_time_scalar(1.0);
    g_sciv_intro_scaled = false;
  }
}

// Exit hook on modal_display_loop's completion path (guest 0x8243089C, where r31
// still holds the modal state). When the boot-intro modal finishes, restore the
// guest clock to 1x and latch the one-shot so a later reuse of state 0x82891D50
// is never re-accelerated.
void sciv_intro_clock_done(PPCRegister& r31) {
  if (!g_sciv_intro_scaled || r31.u32 != kBootIntroModalState) {
    return;
  }
  rex::chrono::Clock::set_guest_time_scalar(1.0);
  g_sciv_intro_scaled = false;
  g_sciv_intro_done = true;
  REXKRNL_INFO("[sciv] boot-intro complete; guest clock restored to 1x");
}

// ---------------------------------------------------------------------------
// TEMPORARY Step-11 diagnostic: engine type-registry population checkpoints.
//
// Boot faults in sub_82130AA8 reading a null entry[id]+0x14 from the runtime
// type registry at guest 0x830F5198: count is set to 14 but only slot 0 ever
// gets its metadata, so the per-type registration runs once instead of 14x.
//
// These [[midasm_hook]]s (sciv_hooks.toml) dump the registry at fixed points so
// the window where a slot first appears pins the registering call. Current state
// (see SCIV_RECOMP_HISTORY.md):
//
//   pre_init    (bootstrap entry 82138434)  : count=0, empty  -> NOT static-init
//   post_subsys (82138744, after subsys+cnt): count=14, 0/14  -> NOT sub_823814e0
//   a00_w2      (82138b64, before sub_8236f6b8) : NEVER FIRES
//   a00_w3      (82138ba4, before sub_82372090) : NEVER FIRES
//   pre_probe   (82138818, before the probe)  : 1/14 (+ slot 1 key only)
//
// IMPORTANT: a00_w2/a00_w3 never fire, which DISPROVES the earlier "registration
// via sub_82138a00 -> sub_8236f6b8" chain (0x82138b64 is sub_8236f6b8's only
// static caller and it is never executed). The real writer sub_8237AE70 is
// reached by INDIRECT dispatch: sub_8236da38's address lives in a function-
// pointer table at 0x820e4010 (data word 82 36 da 38) and is bctrl'd ~17x by a
// stream-pump loop. Registration is therefore a stream/dispatch path, not the
// substep loop. The sciv_dump_writer hook below measures it directly.
//
// The hooks take no registers and return void, so codegen emits a plain
// `name();` call that does not perturb guest state. They must be global-scope,
// non-namespaced, non-extern-"C" definitions whose names match the hooks'
// `name` fields (the generated `extern void <name>();` declarations).
void sciv_regdump_pre_init() {
  sciv::DumpTypeRegistry("pre_init    @82138434");
  // Arm a HW write-watchpoint on registry slot 0 field +0x0C (guest 0x830F51A4)
  // -- the first real metadata pointer written during registration -- to name
  // the exact guest function that populates the registry. Armed here because
  // this hook runs on the xstart guest thread that performs the write.
  sciv::ArmRegistryWriteWatch(0x830F51A4u);
}
void sciv_regdump_post_subsys() { sciv::DumpTypeRegistry("post_subsys @82138744"); }
void sciv_regdump_a00_w2() { sciv::DumpTypeRegistry("a00_w2 (pre 8236f6b8) @82138b64"); }
void sciv_regdump_a00_w3() { sciv::DumpTypeRegistry("a00_w3 (pre 82372090) @82138ba4"); }
void sciv_regdump_pre_probe() { sciv::DumpTypeRegistry("pre_probe   @82138818"); }

// Bootstrap frame-pump drain-loop top (sub_82138428 @ 0x821387d0). The loop pumps
// the engine main-tick sub_82138A00 while `[0x82C37C14] && [obj+0x30]==[0x82C37C10]`
// then probes the type registry, so the registry is meant to fill as the engine
// advances through this init stage. This logs the exit condition + live registry
// fill each pass -- the decision point that controls whether registration runs.
// (The deep writer/parser hooks from prior passes were removed: those paths were
// proven correct; the bug is that the pump exits this stage with the registry
// empty.) See sciv_hooks.toml + SCIV_RECOMP_HISTORY.md.
void sciv_dump_drain() { sciv::DumpDrainCond(); }
