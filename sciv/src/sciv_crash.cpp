// sciv - ReXGlue Recompiled Project
//
// See sciv_crash.h for rationale.

#include "sciv_crash.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// dbghelp must follow windows.h
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>

#include <rex/logging/api.h>
#include <rex/logging/macros.h>
#include <rex/memory/utils.h>
#include <rex/system/kernel_state.h>

#pragma comment(lib, "dbghelp.lib")

namespace sciv {

namespace {

// Recompiled functions are named `sub_<guestaddr>` (alias of `__imp__sub_...`),
// so a symbolized frame embeds the guest address directly -- that string is the
// whole point of this handler.
constexpr int kMaxFrames = 48;

// Module base of the main exe, captured at install. Frames inside the exe are
// also reported as base-relative RVAs so a frame can be cross-referenced even
// when DbgHelp can't name it.
uintptr_t g_module_base = 0;

// Both the rex logger and stderr -- the logger lands in logs/, stderr in the
// console the user is watching. The logger flush in the caller guarantees the
// line survives the imminent termination.
void EmitLine(const char* line) {
  REXLOG_CRITICAL("{}", line);
  std::fprintf(stderr, "%s\n", line);
}

void SymbolizeFrame(int index, DWORD64 addr) {
  char line[1024];

  // RVA relative to the exe image (matches the recompiled .map / IDA math).
  char rva[64] = "";
  if (g_module_base && addr >= g_module_base) {
    std::snprintf(rva, sizeof(rva), "  exe+0x%llX",
                  static_cast<unsigned long long>(addr - g_module_base));
  }

  alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 512];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 511;

  DWORD64 disp = 0;
  if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym)) {
    IMAGEHLP_LINE64 li = {};
    li.SizeOfStruct = sizeof(li);
    DWORD line_disp = 0;
    if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &line_disp, &li)) {
      std::snprintf(line, sizeof(line), "  #%02d 0x%016llX  %s+0x%llX  (%s:%lu)%s",
                    index, static_cast<unsigned long long>(addr), sym->Name,
                    static_cast<unsigned long long>(disp), li.FileName, li.LineNumber, rva);
    } else {
      std::snprintf(line, sizeof(line), "  #%02d 0x%016llX  %s+0x%llX%s", index,
                    static_cast<unsigned long long>(addr), sym->Name,
                    static_cast<unsigned long long>(disp), rva);
    }
  } else {
    std::snprintf(line, sizeof(line), "  #%02d 0x%016llX  <no symbol>%s", index,
                  static_cast<unsigned long long>(addr), rva);
  }
  EmitLine(line);
}

// ---------------------------------------------------------------------------
// TEMPORARY Step-11 diagnostic: SoulCalibur engine "type registry" dump.
//
// Boot faults in sub_82130AA8 reading a null secondary pointer from the runtime
// registry at guest 0x830F5198 (32-byte entries indexed by type-id; count at
// 0x82B56F24, set by sub_82138428). The query path (sub_8222D370 ->
// sub_8222D1F8) walks type-ids 5,7,4,8,6,10,9,2,11,12,13 and dereferences
// entry[id]+0x14. The open question is whether the registry is *totally* empty
// (the whole registration phase inside sub_82138a00 was skipped/mis-lifted) or
// only *partially* filled (one type's registration is missing). This dump,
// emitted on the fault, answers that directly. Remove/gate per Step 13.
//
// See SCIV_RECOMP_PLAN.md "Current blocker" + SCIV_RECOMP_HISTORY.md.
//
// `label` tags the dump so the bracketing checkpoint hooks (sciv_hooks.cpp) can
// be told apart in the log.
void DumpTypeRegistryImpl(const char* label) {
  auto* ks = ::rex::system::kernel_state();
  if (!ks || !ks->memory() || !ks->memory()->virtual_membase()) {
    EmitLine("[registry] guest memory unavailable; skipping registry dump");
    return;
  }
  uint8_t* membase = ks->memory()->virtual_membase();
  auto u32 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint32_t>(membase + va); };
  auto u16 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint16_t>(membase + va); };

  constexpr uint32_t kRegBase = 0x830F5198;    // 32-byte entries, indexed by type-id
  constexpr uint32_t kCountAddr = 0x82B56F24;  // entry count (set in sub_82138428 @ 0x82138734)
  constexpr uint32_t kDescPtr = 0x82B56F20;    // static descriptor table pointer (-> 0x82878CD0)
  constexpr uint32_t kSingleton = 0x830F4200;  // "current registry object" pointer
  constexpr uint32_t kProgress = 0x830F421C;   // sub_82138a00 init progress (halfword)
  constexpr uint32_t kModeAddr = 0x829A6604;   // current mode/region index (set by sub_82115F00)

  uint32_t count = u32(kCountAddr);
  char line[256];
  std::snprintf(line, sizeof(line), "---- SCIV type registry (0x830F5198) dump [%s] ----",
                label ? label : "fault");
  EmitLine(line);
  std::snprintf(line, sizeof(line),
                "  count=%u  descptr=0x%08X  singleton=0x%08X  mode=0x%08X  progress=0x%04X",
                count, u32(kDescPtr), u32(kSingleton), u32(kModeAddr),
                static_cast<unsigned>(u16(kProgress)));
  EmitLine(line);

  uint32_t cap = (count == 0 || count > 32) ? 32 : count;  // guard against garbage count
  int populated = 0;
  for (uint32_t i = 0; i < cap; ++i) {
    uint32_t e = kRegBase + i * 32;
    uint32_t f14 = u32(e + 0x14);
    if (f14 != 0) ++populated;
    std::snprintf(line, sizeof(line),
                  "  [%2u] key=0x%08X  +0C=0x%08X +10=0x%08X +14=0x%08X +18=0x%08X%s", i,
                  u32(e + 0x00), u32(e + 0x0C), u32(e + 0x10), f14, u32(e + 0x18),
                  f14 == 0 ? "  <EMPTY>" : "");
    EmitLine(line);
  }
  std::snprintf(line, sizeof(line), "  populated (+14 != 0): %d / %u entries", populated, cap);
  EmitLine(line);
  EmitLine("----------------------------------------------");
}

// ---------------------------------------------------------------------------
// TEMPORARY Step-11 diagnostic: hardware write-watchpoint on a registry field.
//
// The engine type registry at guest 0x830F5198 is written by some deep callee
// of the engine bootstrap; the writer cannot be pinned by static reading (the
// store goes through an indirect base, not a `lis 0x830F` literal). A HW data
// breakpoint on the slot field traps the instant the byte-swapped store lands
// and names the writing recompiled function (sub_<guestaddr>) directly.
//
// The SDK installs its own VEH at priority 1 that handles only AV / illegal-
// instruction and returns CONTINUE_SEARCH for everything else, so our handler
// (added last) is the one that sees the EXCEPTION_SINGLE_STEP a data breakpoint
// raises. Remove/gate per Step 13.
namespace {
void* g_watch_veh = nullptr;
uint8_t* g_watch_host_addr = nullptr;
uint32_t g_watch_guest_va = 0;
volatile LONG g_watch_hits = 0;
constexpr LONG kWatchMaxHits = 12;

// Resolve a host RIP to "sym+0xNN (file:line)" the same way SymbolizeFrame does,
// but into a caller buffer (no logging side effects), for the watch handler.
void NameHostAddr(DWORD64 addr, char* out, size_t out_sz) {
  alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + 512];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 511;
  DWORD64 disp = 0;
  if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym)) {
    IMAGEHLP_LINE64 li = {};
    li.SizeOfStruct = sizeof(li);
    DWORD line_disp = 0;
    if (SymGetLineFromAddr64(GetCurrentProcess(), addr, &line_disp, &li)) {
      std::snprintf(out, out_sz, "%s+0x%llX (%s:%lu)", sym->Name,
                    static_cast<unsigned long long>(disp), li.FileName, li.LineNumber);
    } else {
      std::snprintf(out, out_sz, "%s+0x%llX", sym->Name,
                    static_cast<unsigned long long>(disp));
    }
  } else {
    std::snprintf(out, out_sz, "<no symbol> 0x%016llX",
                  static_cast<unsigned long long>(addr));
  }
}

void DisarmWatchDr() {
  CONTEXT ctx = {};
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  if (GetThreadContext(GetCurrentThread(), &ctx)) {
    ctx.Dr0 = 0;
    ctx.Dr7 &= ~static_cast<DWORD64>(0x1);  // clear L0 enable
    SetThreadContext(GetCurrentThread(), &ctx);
  }
}

// Walk + symbolize a stack from a copied CONTEXT (StackWalk64 mutates it).
// Shared by the crash filter and the watchpoint handler to recover the
// recompiled sub_<guestaddr> caller chain.
void WalkAndLogStack(const CONTEXT& src) {
  CONTEXT ctx = src;
  STACKFRAME64 frame = {};
  const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;
  HANDLE proc = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  for (int i = 1; i < kMaxFrames; ++i) {
    if (!StackWalk64(machine, proc, thread, &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
      break;
    }
    if (frame.AddrPC.Offset == 0) break;
    SymbolizeFrame(i, frame.AddrPC.Offset);
  }
}

LONG CALLBACK WatchVeh(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  // Was it our data breakpoint (DR0 / Dr6 bit 0)?
  if ((info->ContextRecord->Dr6 & 0x1) == 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  info->ContextRecord->Dr6 = 0;  // ack

  DWORD64 rip = info->ContextRecord->Rip;
  char name[640];
  NameHostAddr(rip, name, sizeof(name));
  uint32_t cur = g_watch_host_addr
                     ? ::rex::memory::load_and_swap<uint32_t>(g_watch_host_addr)
                     : 0;
  char line[1024];
  LONG n = InterlockedIncrement(&g_watch_hits);
  std::snprintf(line, sizeof(line),
                "[regwatch] hit %ld: guest 0x%08X written by %s -> now 0x%08X",
                n, g_watch_guest_va, name, cur);
  EmitLine(line);

  // On the first hit, dump the caller chain -- sub_8237AE70 (the field writer)
  // is reached by an indirect/virtual call with no static `bl`, so the driving
  // registration loop is only visible from a runtime backtrace.
  if (n == 1) {
    EmitLine("[regwatch] writer backtrace (caller chain):");
    WalkAndLogStack(*info->ContextRecord);
  }

  if (n >= kWatchMaxHits) {
    DisarmWatchDr();
    EmitLine("[regwatch] max hits reached; watchpoint disarmed");
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}
}  // namespace

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  // Re-entrancy guard: if symbolization itself faults, don't recurse.
  static volatile LONG in_handler = 0;
  if (InterlockedExchange(&in_handler, 1) != 0) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  const EXCEPTION_RECORD* rec = info->ExceptionRecord;
  char header[512];

  const char* kind = "exception";
  switch (rec->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: kind = "access violation"; break;
    case EXCEPTION_IN_PAGE_ERROR: kind = "in-page error"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION: kind = "illegal instruction"; break;
    case EXCEPTION_STACK_OVERFLOW: kind = "stack overflow"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO: kind = "integer divide-by-zero"; break;
    case EXCEPTION_PRIV_INSTRUCTION: kind = "privileged instruction"; break;
    default: break;
  }

  EmitLine("==================== NATIVE CRASH ====================");
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
      rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) {
    const char* op = rec->ExceptionInformation[0] == 1   ? "write"
                     : rec->ExceptionInformation[0] == 8 ? "execute"
                                                         : "read";
    std::snprintf(header, sizeof(header),
                  "%s (code 0x%08lX): %s of address 0x%016llX  on thread %lu",
                  kind, rec->ExceptionCode, op,
                  static_cast<unsigned long long>(rec->ExceptionInformation[1]),
                  GetCurrentThreadId());
  } else {
    std::snprintf(header, sizeof(header), "%s (code 0x%08lX)  on thread %lu", kind,
                  rec->ExceptionCode, GetCurrentThreadId());
  }
  EmitLine(header);

  // Faulting instruction first -- this is the recompiled function that crashed.
  EmitLine("backtrace (innermost first):");
  SymbolizeFrame(0, reinterpret_cast<DWORD64>(rec->ExceptionAddress));

  // Walk the rest of the stack. StackWalk64 mutates the CONTEXT, so copy it.
  CONTEXT ctx = *info->ContextRecord;
  STACKFRAME64 frame = {};
#if defined(_M_X64)
  const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrStack.Offset = ctx.Rsp;
#else
  const DWORD machine = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = ctx.Eip;
  frame.AddrFrame.Offset = ctx.Ebp;
  frame.AddrStack.Offset = ctx.Esp;
#endif
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;

  HANDLE proc = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  for (int i = 1; i < kMaxFrames; ++i) {
    if (!StackWalk64(machine, proc, thread, &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
      break;
    }
    if (frame.AddrPC.Offset == 0) {
      break;
    }
    SymbolizeFrame(i, frame.AddrPC.Offset);
  }
  EmitLine("======================================================");

  // TEMPORARY Step-11 diagnostic (see DumpTypeRegistryImpl). Only meaningful for
  // the 0x830F5198 static-init fault, but harmless on any crash; remove/gate per
  // Step 13.
  DumpTypeRegistryImpl("fault");

  if (auto logger = ::rex::GetLogger()) {
    logger->flush();
  }
  std::fflush(stderr);

  // Terminate -- the process state is unrecoverable.
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

// Public entry for the mid-ASM checkpoint hooks (sciv_hooks.cpp).
void DumpTypeRegistry(const char* label) { DumpTypeRegistryImpl(label); }

void CountRegistrarCall() {
  static volatile LONG calls = 0;
  LONG n = InterlockedIncrement(&calls);
  // Log every call for the first 48, then every 256th -- enough to see whether
  // it stalls (small N) or spins (large N) without flooding the log.
  if (n > 48 && (n % 256) != 0) return;

  // Dump the parser's input-stream object so we can tell producer-underfeed
  // (stream empty/short) from parser-misread (stream full but read wrong).
  // sub_8236DA38 takes r24 = [0x830F4210]; it gates the whole parse on the
  // stream length [r24+0x0C] (beq->return) and advances position [r24+0x04].
  char line[256];
  auto* ks = ::rex::system::kernel_state();
  if (!(ks && ks->memory() && ks->memory()->virtual_membase())) {
    REXLOG_CRITICAL("[regcount] sub_8236DA38 call #{} (no guest mem)", n);
    return;
  }
  uint8_t* membase = ks->memory()->virtual_membase();
  auto u32 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint32_t>(membase + va); };
  auto u16 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint16_t>(membase + va); };
  auto u8 = [&](uint32_t va) { return *(membase + va); };
  auto valid = [](uint32_t va) { return va >= 0x10000 && va != 0xFFFFFFFF; };

  uint32_t r24 = u32(0x830F4210);
  if (!valid(r24)) {
    std::snprintf(line, sizeof(line),
                  "[regcount] sub_8236DA38 call #%ld  stream ptr [0x830F4210]=0x%08X (bad)", n, r24);
    REXLOG_CRITICAL("{}", line);
    return;
  }
  std::snprintf(line, sizeof(line),
                "[regcount] sub_8236DA38 call #%ld  stream@0x%08X pos[+04]=0x%08X "
                "buf[+08]=0x%08X node[+0C]=0x%08X",
                n, r24, u32(r24 + 0x04), u32(r24 + 0x08), u32(r24 + 0x0C));
  REXLOG_CRITICAL("{}", line);

  // Per-token dispatch view (settles under-fed vs mis-route). sub_8236DA38
  // re-reads the head node `r30 = [stream+0x0C]` each call and dispatches on the
  // node STATE byte `[node+0x19]` (cmpwi r29,2/3/4/5/6 @ 0x8236dbdc): state==2
  // takes the `node->vtable[+0x0C](node)` path (0x8236dc3c) which is the registry
  // metadata writer sub_8237AE70. The engine-wide serialization op-mode lives at
  // global [0x82BFE68C]. Logging, per token, the node + its vtable + state + the
  // resolved vtable[+0x0C] target answers directly whether tokens 2..17 are
  // registration-shaped nodes routed away from the writer (mis-route) or simply
  // not registration nodes at all (under-fed: producer enqueued only ~1 type).
  uint32_t mode = u32(0x82BFE68C);
  uint32_t node = u32(r24 + 0x0C);
  if (!valid(node)) {
    REXLOG_CRITICAL("[regtok]   #{}  mode=0x{:08X}  node=0x{:08X} (no node)", n, mode, node);
    return;
  }
  uint32_t vtbl = u32(node + 0x00);
  uint32_t vt0c = valid(vtbl) ? u32(vtbl + 0x0C) : 0;
  std::snprintf(line, sizeof(line),
                "[regtok]   #%ld  mode=0x%08X  node=0x%08X vtbl=0x%08X "
                "state[+19]=%u idx[+18]=%u flags[+14]=0x%04X vt[+0C]=0x%08X%s",
                n, mode, node, vtbl, u8(node + 0x19), u8(node + 0x18), u16(node + 0x14), vt0c,
                vt0c == 0x8237AE70u ? "  <WRITER>" : "");
  REXLOG_CRITICAL("{}", line);
}

// Public entry for the subtree-walk mid-ASM hook (sciv_hooks.cpp). sub_8236DA38
// walks a node tree iteratively: the loop top at 0x8236db04 is re-entered for
// every node visited (the head container node + each child reached via the
// 0x8236de28 child-array walk), with r30 = the current node. Logging each
// visited node's vtable + dispatch state + the +0x0C virtual slot settles
// whether the head container's subtree actually contains ~14 type-leaf children
// (each a node whose vtable[+0x0C] is a real deserialize method, not the shared
// empty stub 0x8228B0C8) -- i.e. the registrations are present but the leaf
// dispatch/state is wrong (parse drop) -- or only ~1 leaf exists (producer
// under-fed the stream). Bounded so a deep/looping tree can't flood the log.
void DumpNodeVisit(unsigned int node_va) {
  static volatile LONG calls = 0;
  LONG n = InterlockedIncrement(&calls);
  if (n > 200 && (n % 512) != 0) return;

  auto* ks = ::rex::system::kernel_state();
  if (!ks || !ks->memory() || !ks->memory()->virtual_membase()) return;
  uint8_t* membase = ks->memory()->virtual_membase();
  auto u32 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint32_t>(membase + va); };
  auto u16 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint16_t>(membase + va); };
  auto u8 = [&](uint32_t va) { return *(membase + va); };
  auto valid = [](uint32_t va) { return va >= 0x10000 && va != 0xFFFFFFFF; };

  if (!valid(node_va)) return;
  uint32_t vtbl = u32(node_va + 0x00);
  uint32_t vt0c = valid(vtbl) ? u32(vtbl + 0x0C) : 0;
  // 0x8228B0C8 is the shared empty virtual stub (a bare `blr`); a real leaf
  // deserialize method (the one that calls the registry writer) is anything else.
  bool leaf = valid(vt0c) && vt0c != 0x8228B0C8u;
  char line[256];
  std::snprintf(line, sizeof(line),
                "[regnode] visit #%ld  node=0x%08X vtbl=0x%08X state[+19]=%u idx[+18]=%u "
                "flags[+14]=0x%04X child[+0C]=0x%08X sib[+08]=0x%08X vt[+0C]=0x%08X%s",
                n, node_va, vtbl, u8(node_va + 0x19), u8(node_va + 0x18),
                u16(node_va + 0x14), u32(node_va + 0x0C), u32(node_va + 0x08), vt0c,
                leaf ? "  <LEAF-DESER>" : "");
  REXLOG_CRITICAL("{}", line);
}

// Public entry for the writer mid-ASM hook (sciv_hooks.cpp). Logs which registry
// slot each sub_8237AE70 call targets + its parse-state, to split under-fed
// stream from premature drain-loop exit. See sciv_crash.h / SCIV_RECOMP_HISTORY.
void DumpWriterCall(unsigned int state_va) {
  static volatile LONG calls = 0;
  LONG n = InterlockedIncrement(&calls);
  // The writer should fire ~3 passes/type * 14 types ~= a few dozen times; log
  // the first 96 in full, then every 256th, so a stuck iterator (slot stays 0)
  // or an under-fed stream (only a couple of slots ever appear) is both visible
  // and bounded.
  if (n > 96 && (n % 256) != 0) return;

  auto* ks = ::rex::system::kernel_state();
  if (!ks || !ks->memory() || !ks->memory()->virtual_membase()) {
    REXLOG_CRITICAL("[regwrite] sub_8237AE70 call #{} (no guest mem)", n);
    return;
  }
  uint8_t* membase = ks->memory()->virtual_membase();
  auto u32 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint32_t>(membase + va); };

  constexpr uint32_t kRegBase = 0x830F5198;
  uint32_t parse_state = u32(state_va + 0x84);
  uint32_t entry = u32(state_va + 0xA8);
  int slot = -1;
  if (entry >= kRegBase && entry < kRegBase + 32u * 32u && ((entry - kRegBase) % 32u) == 0) {
    slot = static_cast<int>((entry - kRegBase) / 32u);
  }
  char line[256];
  std::snprintf(line, sizeof(line),
                "[regwrite] sub_8237AE70 call #%ld  state=0x%08X parse_state=%u "
                "entry=0x%08X slot=%d",
                n, state_va, parse_state, entry, slot);
  REXLOG_CRITICAL("{}", line);
}

// Public entry for the drain-loop mid-ASM hook (sciv_hooks.cpp). Evaluated at the
// bootstrap pump loop top (0x821387d0) each pass; reads the exit condition and the
// live registry fill so the reason the pump stops with the registry empty is read
// directly. See sciv_crash.h / SCIV_RECOMP_HISTORY.md.
void DumpDrainCond() {
  static volatile LONG calls = 0;
  LONG n = InterlockedIncrement(&calls);
  // The loop runs ~17x then exits; log generously but bound it in case the recomp
  // spins (which would itself be the finding).
  if (n > 64 && (n % 256) != 0) return;

  auto* ks = ::rex::system::kernel_state();
  if (!ks || !ks->memory() || !ks->memory()->virtual_membase()) {
    REXLOG_CRITICAL("[draincond] pass #{} (no guest mem)", n);
    return;
  }
  uint8_t* membase = ks->memory()->virtual_membase();
  auto u32 = [&](uint32_t va) { return ::rex::memory::load_and_swap<uint32_t>(membase + va); };
  auto valid = [](uint32_t va) { return va >= 0x10000 && va != 0xFFFFFFFF; };

  // Loop condition (sub_82138428 @ 0x821387d0): continue while
  //   obj = [0x82C37C14] != 0  &&  [obj+0x30] == [0x82C37C10]
  constexpr uint32_t kListBase = 0x82C37C10;  // table of loaded game-data pointers
  uint32_t target = u32(kListBase + 0x00);    // [0x82C37C10]
  uint32_t obj = u32(kListBase + 0x04);       // [0x82C37C14]
  uint32_t seq = valid(obj) ? u32(obj + 0x30) : 0;
  bool will_exit = (obj == 0) || (seq != target);
  const char* why = (obj == 0) ? "obj==0" : (seq != target) ? "seq!=target" : "continue";

  // Live registry fill (does the pump make progress at all?).
  constexpr uint32_t kRegBase = 0x830F5198;
  constexpr uint32_t kCountAddr = 0x82B56F24;
  uint32_t count = u32(kCountAddr);
  uint32_t cap = (count == 0 || count > 32) ? 32 : count;
  int populated = 0;
  for (uint32_t i = 0; i < cap; ++i) {
    if (u32(kRegBase + i * 32 + 0x14) != 0) ++populated;
  }

  char line[256];
  std::snprintf(line, sizeof(line),
                "[draincond] pass #%ld  obj=[0x82C37C14]=0x%08X seq[+30]=0x%08X "
                "target[0x82C37C10]=0x%08X  %s (%s)  registry %d/%u populated",
                n, obj, seq, target, will_exit ? "WILL EXIT->probe" : "continue", why,
                populated, cap);
  REXLOG_CRITICAL("{}", line);
}

// Public entry for the bootstrap mid-ASM hook (sciv_hooks.cpp). See the
// anonymous-namespace watch machinery above and sciv_crash.h.
void ArmRegistryWriteWatch(unsigned int guest_va) {
  static volatile LONG armed = 0;
  if (InterlockedExchange(&armed, 1) != 0) {
    return;  // arm once
  }
  auto* ks = ::rex::system::kernel_state();
  if (!ks || !ks->memory() || !ks->memory()->virtual_membase()) {
    EmitLine("[regwatch] guest memory unavailable; cannot arm watchpoint");
    return;
  }
  g_watch_guest_va = guest_va;
  g_watch_host_addr = ks->memory()->virtual_membase() + guest_va;

  if (!g_watch_veh) {
    // Priority 0 = last, so the SDK's AV/illegal-instruction VEH runs first and
    // ours only fields the single-step a data breakpoint raises.
    g_watch_veh = AddVectoredExceptionHandler(0, WatchVeh);
  }

  CONTEXT ctx = {};
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  if (!GetThreadContext(GetCurrentThread(), &ctx)) {
    EmitLine("[regwatch] GetThreadContext failed; cannot arm watchpoint");
    return;
  }
  ctx.Dr0 = reinterpret_cast<DWORD64>(g_watch_host_addr);
  // DR7: L0 (bit 0) = local enable; RW0 (bits 16-17) = 01 (write); LEN0
  // (bits 18-19) = 11 (4 bytes) -> 0x000D0001.
  ctx.Dr7 = (ctx.Dr7 & ~static_cast<DWORD64>(0x000F0003)) | 0x000D0001;
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  char line[256];
  if (SetThreadContext(GetCurrentThread(), &ctx)) {
    std::snprintf(line, sizeof(line),
                  "[regwatch] armed write-watch on guest 0x%08X (host %p), DR7=0x%llX",
                  guest_va, static_cast<void*>(g_watch_host_addr),
                  static_cast<unsigned long long>(ctx.Dr7));
  } else {
    std::snprintf(line, sizeof(line),
                  "[regwatch] SetThreadContext failed (err %lu); watchpoint not armed",
                  GetLastError());
  }
  EmitLine(line);
}

void InstallCrashHandler() {
  g_module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));

  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME |
                SYMOPT_FAIL_CRITICAL_ERRORS);
  // Prime symbols now, off the crash path: the PDB load and DbgHelp init can
  // allocate, which is risky to do for the first time inside a faulted process.
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);

  SetUnhandledExceptionFilter(CrashFilter);
  REXLOG_INFO("sciv crash handler installed (symbolized backtraces on native fault)");
}

}  // namespace sciv

#else  // !_WIN32

namespace sciv {
void InstallCrashHandler() {}
}  // namespace sciv

#endif
