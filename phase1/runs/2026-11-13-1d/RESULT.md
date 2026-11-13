# Phase 1, step 1d: kartr_gate reads an agent-written ROM (13 Nov 2026)

## 16:43 IST cover run (from the Project mirror, host only)
- `kartr_rule.h`: the whole decision with no Genode dependency, so the component and the host test run the same code.
- `kartr_gate_1d.cc`: actions come from an **`actions` ROM written by the agent** (re-read on every update); verdicts go to a **`verdicts` report** for the owner's screen. Key and mandate only from `config`, which the agent cannot write.
- `tests/test_kartr_rule.cc` + `vectors.h`: **24/24** on the host (g++ -std=c++20 -Wall -Wextra -Werror). Output: `test_output.txt`.
- Two holes found in the 13 Nov component (also in the published depot package), both closed:
  1. **No mandate check**: `Pay`, `pay ` or any unknown type (`open_link`) was allowed, because only the irreversible list was checked.
  2. **Field-boundary forgery**: an approval over `type|target|amount` for target `r@ybl|x`, amount `5` also fitted target `r@ybl`, amount `x|5`. Now `|`, control characters, an empty target on an irreversible act and fields over 63 characters are held.

## ~17:15 IST live session: built and run on seL4
- Built with Genode's own build system (no depot download needed) in Claude's cloud; one fix (Main made non-copyable for -Weffc++).
- Run script `phase2/genode-repo/turiya_1d.run`: kartr_gate + report_rom; the agent's ROM is a boot module standing in for an agent component; 12 actions (the original 8 + `Pay`, `open_link`, a 70-char target, a pay approval reused as transfer).
- **seL4 in QEMU: 12/12 as expected, "kartr_gate: 3 allowed, 9 held"**, verdicts report received by report_rom (`boot_log_sel4_qemu.txt`).
- **seL4 in VirtualBox on the laptop: the same 12/12** (`serial_log_sel4_vbox.txt`, image `phase2/images/turiya-sel4-1d.iso`, SHA-256 290924d9…4c7d84).

## Notes
- A URL value `http://…` in the HID actions file stopped earlier test runs from showing actions (not yet confirmed whether the colon or the old build caused it); the run uses `kyc.example/verify`. If an agent writes something unparsable, the gate sees no actions and allows nothing: fails closed. To check with the real agent writer.
- The Goa project still builds the old `src/kartr_gate.cc` (the one in the published depot). Republishing kartr_gate in the Turiya depot with the 1d code needs the owner's yes.
- Not yet: routes from a real agent component, and Phase 0's third rule (target taken from outside content), which needs the outside content.
