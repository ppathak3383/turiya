// SPDX-License-Identifier: AGPL-3.0-or-later
// Turiya - the Verus-proved gate decision, as a C-callable library for Sakshi (no_std, no allocation).
// gate_exec.rs is gate.rs with only the proof annotations removed (strip_verus.py); nothing executable changes.
#![no_std]
#![allow(dead_code, unused_variables)]
include!("gate_exec.rs");

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! { loop {} }

/// flags, one bit each: 0 input_ok, 1 target_ok, 2 target_present, 3 irreversible, 4 in_mandate,
/// 5 thinking, 6 planted, 7 has_key, 8 approval_hex_ok, 9 unknown_target (step 5d). state: 0 jagrat, 1 svapna, 2 susupti.
/// Returns the verdict in the same order as the C++ enum class Verdict.
#[no_mangle]
pub extern "C" fn turiya_proved_decide(flags: u32, state: u32, given: *const u8, expect: *const u8) -> u32 {
    let b = |i: u32| flags & (1u32 << i) != 0;
    let f = Facts { input_ok: b(0), target_ok: b(1), target_present: b(2), irreversible: b(3), in_mandate: b(4),
                    thinking: b(5), planted: b(6), has_key: b(7), approval_hex_ok: b(8),
                    unknown_target: b(9) };
    let st = match state { 0 => Avastha::Jagrat, 1 => Avastha::Svapna, _ => Avastha::Susupti };
    let g: &[u8; 32] = unsafe { &*(given as *const [u8; 32]) };
    let e: &[u8; 32] = unsafe { &*(expect as *const [u8; 32]) };
    decide(&f, &st, g, e) as u32
}
