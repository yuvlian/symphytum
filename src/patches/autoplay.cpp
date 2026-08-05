// Autoplay engine patch.
//
// "Real" autoplay in this game is two coordinated pieces:
//
//   1. Chart load. MusicScoreController::CreateChartData() bakes a
//      JudgeResult(Auto=100, None, diff=0) into every non-damage note when the
//      controller's _isAuto field is set (that field comes from the isAuto
//      argument of MusicScoreController::SetUpAsync, which the live screen
//      passes from the pre-live autoplay switch / start-request flag).
//
//   2. Per-frame judgement. LiveLogicControllerBase::Judgment() runs the
//      autoplay branch when the IsAutoPlay() virtual returns true. The branch
//      calls LiveLogicControllerBase::<Judgment>g__AutoPlay|NN_0(), which
//      auto-judges every note at its judge time (result Auto, note consumed),
//      and the scoring flows through Judge() — combo/score/life/SE/effects all
//      process like a manual hit, except the judge type is Auto and the combo
//      is gated on IsAutoPlay().
//
// Starting a live with autoplay is gated (item consumption, isAutoPlayAllowed,
// and the isAutoPlay flag in the start API request), so instead of fighting
// the gating we force the engine from inside and fix the presentation:
//
//   - LiveGameLogicControllerBase::IsAutoPlay()        -> always true
//     (activates the autoplay branch for Single/Multi/ParkQuest controllers)
//   - the CreateChartData() _isAuto gate               -> always bake
//   - UpdateJudgeResult()                              -> remap Auto(100) to
//     PerfectPlus(6) so judge labels, score and the reported judge counts look
//     like a perfect manual play
//   - UpdateCombo(LiveNoteDataBase)                    -> bypass the IsAutoPlay
//     gate so the combo counter counts
//
// The bake gate is forced with a static byte patch, not a hook: CreateChartData
// starts with IL2CPP's shrink-wrap prologue (`mov rax, rsp; ...`), which can't
// be relocated into a trampoline (the resumed code dereferences [rax+..] with
// rax = trampoline rsp -> instant AV). Instead we NOP the `jz` that skips the
// bake block, so the Auto results are always pre-baked at chart build time.
//
// The live then plays exactly like a real autoplay run while the start
// request and server state remain "manual". Damage notes (NoteType=10) are
// never auto-judged (they get an Unknown result baked), so life is never
// drained by them.
//
// All targets are resolved through the il2cpp runtime metadata (class/method
// names from dump.cs); the bake-gate patch is located by scanning the method
// body for the _isAuto byte access (field offset resolved from metadata), so
// nothing depends on hardcoded addresses and it survives game updates.

#include "patches/autoplay.hpp"
#include "patches/bake_gate.hpp"
#include "il2cpp.hpp"
#include "hook.hpp"
#include "config.hpp"
#include "log.hpp"
#include <windows.h>
#include <cstring>

namespace symphytum::patches {

namespace {

// Vision::Common::Proto::Enums::LiveNoteJudgementType values we rely on.
constexpr uint32_t kJudgeAuto = 100;        // autoplay-baked judge type
constexpr uint32_t kJudgePerfectPlus = 6;   // what we remap Auto to

// Vision::LiveCore::JudgeResult layout: { Judge; TimingType; DiffFrame }.
struct JudgeResultRaw {
    uint32_t judge;
    uint32_t timing;
    float diff;
};
static_assert(sizeof(JudgeResultRaw) == 12);

// Read MethodInfo->methodPointer (offset 0), the native body address.
void* method_pointer(il2cpp::Il2CppMethod* m) {
    return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(m) + 0);
}

// Resolve a field's offset from the class metadata.
size_t find_field_offset(il2cpp::Il2CppClass* k, const char* name) {
    void* iter = nullptr;
    while (il2cpp::Il2CppField* f = il2cpp::class_get_fields(k, &iter)) {
        if (strcmp(il2cpp::field_get_name(f), name) == 0) {
            return il2cpp::field_get_offset(f);
        }
    }
    return 0;
}

// Field names vary between builds (auto-property backing fields show up as
// `<X>k__BackingField` in some metadata dumps, `_X` in others). Try each.
size_t find_field_offset_any(il2cpp::Il2CppClass* k, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        size_t o = find_field_offset(k, n);
        if (o) return o;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Hook 1: LiveGameLogicControllerBase::IsAutoPlay() -> true.
//
// Jump-only (never calls the original). IL2CPP x64 ABI: rcx = this,
// rax = MethodInfo*; we ignore both and return true in al.
// ---------------------------------------------------------------------------
bool hk_is_autoplay(void* /*self*/, void* /*method*/) {
    return true;
}

// ---------------------------------------------------------------------------
// Hook 2: LiveLogicControllerBase::UpdateCombo(LiveNoteDataBase) replaced.
//
// The original is gated on `if (!IsAutoPlay())` — with autoplay forced it
// never updates the combo. We bypass the gate and call the un-gated
// UpdateCombo(NoteType, LiveNoteJudgementType) directly.
// ---------------------------------------------------------------------------

// Field offsets for the combo hook (resolved from metadata).
size_t g_off_note_type = 0;      // LiveNoteDataBase.NoteType (int)
size_t g_off_judge_result = 0;   // LiveNoteDataBase.JudgeResult (12-byte struct)

// UpdateCombo(NoteType, LiveNoteJudgementType) — the un-gated combo updater.
il2cpp::Il2CppMethod* g_update_combo_2 = nullptr;

void hk_update_combo(void* self, void* note, void* /*method*/) {
    if (!self || !note || !g_update_combo_2) return;
    int32_t type = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(note) + g_off_note_type);
    int32_t judge = *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(note) + g_off_judge_result);
    // The bake now writes PerfectPlus(6), but keep Auto->PerfectPlus as
    // defense so IsAddCombo always counts.
    if (judge == static_cast<int32_t>(kJudgeAuto)) judge = static_cast<int32_t>(kJudgePerfectPlus);
    // Direct native dispatch (rcx=self, edx=type, r8d=judge) — no marshalling.
    using uc_fn = void (*)(void* self, int32_t type, int32_t judge);
    uc_fn uc = reinterpret_cast<uc_fn>(method_pointer(g_update_combo_2));
    if (uc) uc(self, type, judge);
}

// ---------------------------------------------------------------------------
// Static patch: force the CreateChartData _isAuto bake (see bake_gate.hpp).
// ---------------------------------------------------------------------------

// NOP the 6-byte `jz rel32` at `at` inside `body` (executable memory).
void nop_jz(uint8_t* body, size_t at) {
    DWORD old_prot = 0;
    if (!VirtualProtect(body + at, 6, PAGE_EXECUTE_READWRITE, &old_prot)) return;
    std::memset(body + at, 0x90, 6);
    DWORD dummy = 0;
    VirtualProtect(body + at, 6, old_prot, &dummy);
    FlushInstructionCache(GetCurrentProcess(), body + at, 6);
}

}  // namespace

bool install_autoplay() {
    // --- Step 0: resolve the bake gate and force it (static patch, no hook) ---
    il2cpp::Il2CppClass* msc = il2cpp::find_class("Vision.Common", "MusicScoreController");
    if (!msc) {
        SYM_LOG_ERROR("autoplay", "MusicScoreController not found");
        return false;
    }
    uint32_t is_auto_off = static_cast<uint32_t>(find_field_offset_any(msc, {"_isAuto"}));
    if (!is_auto_off) {
        // Fallback: field offset from dump.cs (stable across the known builds).
        is_auto_off = 0x58;
        SYM_LOG("autoplay", "_isAuto field not found in metadata, using 0x58 fallback");
    }
    il2cpp::Il2CppMethod* ccd = il2cpp::class_get_method_from_name(msc, "CreateChartData", 1);
    if (!ccd) {
        SYM_LOG_ERROR("autoplay", "CreateChartData not found");
        return false;
    }
    uint8_t* body = static_cast<uint8_t*>(method_pointer(ccd));
    if (!body) {
        SYM_LOG_ERROR("autoplay", "CreateChartData has no method pointer");
        return false;
    }
    // The gate sits near the end of the bake block; scan a generous window.
    size_t jz = bake_gate::find_jz(body, 0x4000, is_auto_off);
    if (jz == SIZE_MAX) {
        SYM_LOG_ERROR("autoplay", "CreateChartData _isAuto gate not found (field off 0x{:x}) "
                     "— autoplay bake NOT forced", is_auto_off);
        return false;  // don't activate the autoplay branch without the bake
    }
    nop_jz(body, jz);
    SYM_LOG("autoplay", "CreateChartData bake gate forced at +0x{:x} (_isAuto off 0x{:x})",
            jz, is_auto_off);

    // Step 0b: flip the baked judge type from Auto(100) to PerfectPlus(6).
    // The bake writes the judge type from a single `mov r32, 0x64` immediate;
    // every note then carries PerfectPlus from chart load, so labels, score
    // and the reported counts all look manual — no result hook needed.
    if (config::g.fake_manual_result) {
        size_t imm = bake_gate::find_auto_judge_imm(body, 0x4000, jz);
        if (imm == SIZE_MAX) {
            SYM_LOG_ERROR("autoplay", "Auto(100) bake immediate not found — results stay Auto");
        } else {
            uint8_t* p = body + imm;
            DWORD old_prot = 0;
            if (VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old_prot)) {
                *p = static_cast<uint8_t>(kJudgePerfectPlus);  // 0x64 -> 0x06
                DWORD dummy = 0;
                VirtualProtect(p, 1, old_prot, &dummy);
                FlushInstructionCache(GetCurrentProcess(), p, 1);
                SYM_LOG("autoplay", "bake judge immediate 0x64 -> 0x06 at +0x{:x} (Auto -> PerfectPlus)", imm);
            } else {
                SYM_LOG_ERROR("autoplay", "bake judge immediate VirtualProtect failed");
            }
        }
    }

    // --- Step 1: IsAutoPlay() -> true (jump-only) ---
    il2cpp::Il2CppClass* logic = il2cpp::find_class("Vision.Rhythm", "LiveGameLogicControllerBase");
    if (!logic) {
        SYM_LOG_ERROR("autoplay", "LiveGameLogicControllerBase not found");
        return false;
    }
    il2cpp::Il2CppMethod* is_autoplay = il2cpp::class_get_method_from_name(logic, "IsAutoPlay", 0);
    if (!is_autoplay) {
        SYM_LOG_ERROR("autoplay", "IsAutoPlay() not found");
        return false;
    }
    void* old1 = hook::install_jump(is_autoplay, reinterpret_cast<void*>(&hk_is_autoplay));
    if (!old1) {
        SYM_LOG_ERROR("autoplay", "IsAutoPlay jump hook failed");
        return false;
    }
    SYM_LOG("autoplay", "IsAutoPlay -> always true (body at {})", old1);

    // --- Step 2: note field offsets for the combo hook ---
    il2cpp::Il2CppClass* note_cls = il2cpp::find_class("Vision.Common", "LiveNoteDataBase");
    if (!note_cls) {
        SYM_LOG_ERROR("autoplay", "LiveNoteDataBase not found");
        return false;
    }
    g_off_note_type = find_field_offset_any(note_cls, {
        "<NoteType>k__BackingField", "_NoteType", "NoteType"});
    g_off_judge_result = find_field_offset_any(note_cls, {
        "<JudgeResult>k__BackingField", "_JudgeResult", "JudgeResult"});
    if (!g_off_judge_result) {
        g_off_judge_result = 0x34;  // from the runtime dump (stable layout)
        SYM_LOG("autoplay", "JudgeResult field not found, using 0x34 fallback");
    }
    SYM_LOG("autoplay", "note offsets: noteType=0x{:x} judgeResult=0x{:x}",
            g_off_note_type, g_off_judge_result);

    // --- Step 3: combo — bypass the IsAutoPlay gate in UpdateCombo(note) ---
    // UpdateCombo is declared on the BASE class (Vision.Common).
    il2cpp::Il2CppClass* logic_base = il2cpp::find_class("Vision.Common", "LiveLogicControllerBase");
    if (!logic_base) {
        SYM_LOG_ERROR("autoplay", "LiveLogicControllerBase (base) not found");
        return false;
    }
    g_update_combo_2 = il2cpp::class_get_method_from_name(logic_base, "UpdateCombo", 2);
    il2cpp::Il2CppMethod* combo1 = il2cpp::class_get_method_from_name(logic_base, "UpdateCombo", 1);
    if (!g_update_combo_2 || !combo1) {
        SYM_LOG_ERROR("autoplay", "UpdateCombo methods not found — combo bypass skipped");
    } else {
        void* old3 = hook::install_jump(combo1, reinterpret_cast<void*>(&hk_update_combo));
        if (!old3) {
            SYM_LOG_ERROR("autoplay", "UpdateCombo jump hook failed — combo bypass skipped");
        } else {
            SYM_LOG("autoplay", "UpdateCombo autoplay gate bypassed (body at {})", old3);
        }
    }

    SYM_LOG("autoplay", "autoplay engine patch installed");
    return true;
}

}  // namespace symphytum::patches
