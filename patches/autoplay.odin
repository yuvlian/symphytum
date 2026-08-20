package patches

import "base:runtime"
import "core:log"
import "shared:il2cure/2eff70d/hook"
import "shared:il2cure/2eff70d/il2cpp"
import "shared:il2cure/2eff70d/scan"
import "../cfg"

JUDGE_AUTO         :: 100 // LiveNoteJudgementType.Auto
JUDGE_PERFECT_PLUS :: 6   // remap target

field_note_type:  il2cpp.Il2CppField  // LiveNoteDataBase.NoteType
field_note_judge: il2cpp.Il2CppField  // LiveNoteDataBase.JudgeResult
update_combo_2:   il2cpp.Il2CppMethod // UpdateCombo(NoteType, JudgeType)

// rewrite Auto(100) to PerfectPlus(6)
install_force_fp :: proc() -> bool {
	if !cfg.cfg.autoplay {
		log.debug("autoplay disabled. install_force_fp skipped.")
		return false
	}

	msc, cok := il2cpp.find_class("Vision.Common.MusicScoreController")
	if !cok {
		log.error("MusicScoreController not found. install_force_fp failed.")
		return false
	}

	is_auto_off: u32 = 0x58 // MusicScoreController._isAuto
	if f := il2cpp.vm_il2cpp_class_get_field_from_name(msc, "_isAuto"); f != 0 {
		is_auto_off = u32(il2cpp.vm_il2cpp_field_get_offset(f))
	}

	ccd, mok := il2cpp.class_method(msc, "CreateChartData", 1)
	if !mok {
		log.error("CreateChartData not found. install_force_fp failed.")
		return false
	}

	body := il2cpp.method_pointer(ccd)
	if body == nil {
		log.error("CreateChartData has no method pointer. install_force_fp failed.")
		return false
	}

	body_b := cast([^]u8)body

	jz, jok := scan.find_gate_jz(body_b, 0x4000, is_auto_off)
	if !jok {
		log.errorf(
			"CreateChartData _isAuto gate not found at 0x%x. install_force_fp failed.",
			is_auto_off,
		)
		return false
	}

	if !hook.nop_rel32(body_b, jz) {
		log.errorf("could not NOP the bake gate at +0x%x. install_force_fp failed.", jz)
		return false
	}

	log.debugf(
		"CreateChartData bake gate forced at +0x%x (_isAuto off 0x%x)",
		jz, is_auto_off,
	)

	if imm, iok := scan.find_mov_imm32(body_b, 0x4000, jz, JUDGE_AUTO); iok {
		if hook.patch_byte(body_b, imm, u8(JUDGE_PERFECT_PLUS)) {
			log.debugf(
				"bake judge immediate 0x64 -> 0x%02x at +0x%x (Auto -> PerfectPlus)",
				JUDGE_PERFECT_PLUS,
				imm,
			)
		} else {
			log.errorf(
				"couldnt rewrite bake judge immediate at +0x%x. install_force_fp failed.",
				imm,
			)
			return false
		}
	} else {
		log.error("Auto(100) bake immediate not found. install_force_fp failed.")
		return false
	}

	log.info("install_force_fp finished.")
	return true
}

// make LiveGameLogicControllerBase.IsAutoPlay() always true
install_force_auto :: proc() -> bool {
	if !cfg.cfg.autoplay {
		log.debug("autoplay disabled. install_force_auto skipped.")
		return false
	}

	m, find_ok := il2cpp.find_method(
		"Vision.Rhythm.LiveGameLogicControllerBase::IsAutoPlay", 0)

	if !find_ok {
		log.error(
			"LiveGameLogicControllerBase::IsAutoPlay not found. install_force_auto failed.",
		)
		return false
	}

	trampoline, og := hook.hook_inline(m,
		rawptr(is_autoplay_detour), il2cpp.default_offsets())

	if trampoline == nil && og == nil {
		log.error("could not hook IsAutoPlay. install_force_auto failed.")
		return false
	}

	log.info("install_force_auto finished.")
	return true
}

is_autoplay_detour :: proc "c" (this: il2cpp.Il2CppObject, method: il2cpp.Il2CppMethod) -> bool {
	return true
}

// redirect UpdateCombo(LiveNoteDataBase) to the
// un-gated UpdateCombo(NoteType, LiveNoteJudgementType)
// so combo counts w/ autoplay
install_force_count :: proc() -> bool {
	if !cfg.cfg.autoplay {
		log.debug("autoplay disabled. install_force_count skipped.")
		return false
	}

	ncls, cok := il2cpp.find_class("Vision.Common.LiveNoteDataBase")
	if !cok {
		log.error("LiveNoteDataBase not found. install_force_count failed.")
		return false
	}

	field_note_type = il2cpp.vm_il2cpp_class_get_field_from_name(ncls,
		"<NoteType>k__BackingField")
	field_note_judge = il2cpp.vm_il2cpp_class_get_field_from_name(ncls,
		"<JudgeResult>k__BackingField")

	lbc, lok := il2cpp.find_class("Vision.Common.LiveLogicControllerBase")
	if !lok {
		log.error("LiveLogicControllerBase not found. install_force_count failed.")
		return false
	}

	update_combo_2, _ = il2cpp.class_method(lbc, "UpdateCombo", 2)
	if update_combo_2 == 0 {
		log.error("UpdateCombo(NoteType,JudgeType) not found. install_force_count failed.")
		return false
	}

	m1, m1ok := il2cpp.class_method(lbc, "UpdateCombo", 1)
	if !m1ok {
		log.error("UpdateCombo(LiveNoteDataBase) not found. install_force_count failed.")
		return false
	}

	tramp, orig := hook.hook_inline(m1,
		rawptr(update_combo_detour), il2cpp.default_offsets())

	if tramp == nil && orig == nil {
		log.error(
			"could not hook UpdateCombo(LiveNoteDataBase). install_force_count failed.")
		return false
	}

	log.infof("install_force_count finished.")
	return true
}

update_combo_detour :: proc "c" (
	this:   il2cpp.Il2CppObject,
	note:   il2cpp.Il2CppObject,
	method: il2cpp.Il2CppMethod,
) {
	context = runtime.default_context()
	context.logger = detour_logger

	if this == 0 || note == 0 || update_combo_2 == 0 ||
		field_note_type == 0 || field_note_judge == 0 {
		return
	}

	note_type := i32(0)
	judge := i32(0)

	il2cpp.vm_il2cpp_field_get_value(note, field_note_type, &note_type)
	il2cpp.vm_il2cpp_field_get_value(note, field_note_judge, &judge)

	if judge == JUDGE_AUTO {
		judge = JUDGE_PERFECT_PLUS
	}

	uc := il2cpp.method_proc(
		update_combo_2,
		proc "c" (il2cpp.Il2CppObject, i32, i32, il2cpp.Il2CppMethod),
	)
	if uc == nil {
		return
	}

	uc(this, note_type, judge, update_combo_2)
	// log.debugf("combo type=%v judge=%v", note_type, judge)
}
