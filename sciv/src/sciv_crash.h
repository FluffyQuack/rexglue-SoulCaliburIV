// sciv - ReXGlue Recompiled Project
//
// Last-chance crash diagnostics. The SDK's REX_FATAL traps (unregistered
// function, unresolved branch) log + flush before aborting, but a genuine
// native access violation inside recompiled guest code has no logger anywhere
// in the SDK -- the process just dies and the run log ends mid-frame with no
// trace. This installs a SetUnhandledExceptionFilter that, on the first
// unhandled fault, symbolizes the faulting RIP and walks the stack to recover
// the recompiled `sub_<guestaddr>` frames, writes them to the rex log (flushed)
// and stderr, then lets the process terminate.
//
// Project-side glue only -- no SDK fork. Windows-only (matches the build).

#pragma once

namespace sciv {

// Installs the unhandled-exception filter and primes DbgHelp against the
// process PDB. Safe to call once, after logging is up (OnPostInitLogging).
void InstallCrashHandler();

// TEMPORARY Step-11 diagnostic. Dumps the SoulCalibur engine "type registry"
// (guest 0x830F5198) to the log, tagged with `label`. Called both from the
// crash filter (on the static-init fault) and from the mid-ASM checkpoint hooks
// in sciv_hooks.cpp, which fire at fixed points inside the engine bootstrap
// (sub_82138428) to bracket *when* the registry gets populated. Remove/gate per
// Step 13. See SCIV_RECOMP_HISTORY.md.
void DumpTypeRegistry(const char* label);

// TEMPORARY Step-11 diagnostic. Arms a hardware data write-watchpoint (debug
// register DR0) on a guest registry slot field so the *exact* guest function
// that writes the registry can be named, instead of guessing from 86 MB of
// disassembly. `guest_va` is the guest address to watch (4 bytes); it is
// translated to its host address via the kernel membase. Must be called on the
// guest thread that will perform the write (the engine bootstrap runs on the
// `xstart` main thread, so the bootstrap mid-ASM hooks are the right arming
// site). Logs the symbolized writer RIP + value for the first few hits, then
// disarms itself. Remove/gate per Step 13. See SCIV_RECOMP_HISTORY.md.
void ArmRegistryWriteWatch(unsigned int guest_va);

// TEMPORARY Step-11 diagnostic. Counts and logs registrar (sub_8236da38)
// invocations to classify why the type registry stalls after one entry: a
// small final count means the fixed-timestep substep loop in sub_8236f6b8 quit
// driving it (a guest-clock/timing problem); a large count means the parser
// state machine isn't advancing. Logged sparsely. Remove/gate per Step 13.
void CountRegistrarCall();

// TEMPORARY Step-11 diagnostic. Logs each node visited by sub_8236DA38's subtree
// walk (loop top 0x8236db04, r30 = current node). The head container nodes have
// an empty +0x0C virtual slot (the shared `blr` stub 0x8228B0C8); a real
// type-leaf node has a non-stub +0x0C deserialize method (flagged <LEAF-DESER>)
// and is what reaches the registry writer sub_8237AE70. Counting the leaves in
// the head node's subtree settles under-fed stream (~1 leaf) vs leaf parse/state
// drop (~14 leaves but only 1 registers). Remove/gate per Step 13.
void DumpNodeVisit(unsigned int node_va);

// TEMPORARY Step-11 diagnostic. Logs each call to the per-field registry writer
// sub_8237AE70. `state_va` is the guest VA of its state object (r3 at entry);
// the writer's parse-state is [state+0x84] and the target registry entry it
// writes is [state+0xA8]. Logging the target entry's slot index (relative to
// the registry base 0x830F5198, 32-byte stride) per call settles the two live
// hypotheses directly: if the writer only ever targets slot 0/1 the descriptor
// stream is under-fed (few registration tokens); if it walks slots 0..13 but
// the run ends early the drain loop exits prematurely. Remove/gate per Step 13.
// See SCIV_RECOMP_HISTORY.md.
void DumpWriterCall(unsigned int state_va);

// TEMPORARY Step-11 diagnostic. Logs the bootstrap (sub_82138428) frame-pump
// drain-loop *exit condition*, evaluated at the loop top (0x821387d0), once per
// iteration. The loop pumps the engine main-tick sub_82138A00 while
// `[0x82C37C14] != 0 && [[0x82C37C14]+0x30] == [0x82C37C10]`, then probes the
// type registry; the registry is supposed to fill as the engine advances through
// this stage. This logs, each pass, the wait object [0x82C37C14], its sequence
// field [obj+0x30], the target [0x82C37C10], whether the loop will exit, and the
// live registry populated-count -- so the *reason* the pump stops after ~17
// frames with the registry empty is read directly (object torn down vs. sequence
// advanced past target vs. registry simply never grows). This is the decision
// point that controls whether registration runs, vs. the writer/parser which
// prior passes proved correct. Remove/gate per Step 13. See SCIV history doc.
void DumpDrainCond();

}  // namespace sciv
