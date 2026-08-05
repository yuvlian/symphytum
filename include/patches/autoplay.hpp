#pragma once

namespace symphytum::patches {

// Force the in-live autoplay judgement engine on, even when the live was
// started as a normal (non-autoplay) run. See autoplay.cpp for the full
// mechanism breakdown. Gated by config::g.force_autoplay; sub-option
// fake_manual_result remaps Auto judges to PerfectPlus so the run reports
// like a perfect manual play.
bool install_autoplay();

}  // namespace symphytum::patches
