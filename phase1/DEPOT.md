# Turiya depot: install Turiya's components on Sculpt OS

Turiya publishes its Genode packages as a signed depot, served straight from this repository.

| Item | Value |
|---|---|
| Depot user | `turiya` |
| Download location | `https://raw.githubusercontent.com/ppathak3383/turiya/main/depot` |
| Signing key | Ed25519 PGP key "Turiya depot signing key", fingerprint `AC7A96E0AFEF30283B1B770130786A9DC81E53E5` (`depot/turiya/pubkey`) |
| Index | `depot/turiya/index/26.10.xz` (Sculpt OS 26.10), signed |
| Packages | `turiya/pkg/turiya_hello/2026-11-13`, `turiya/pkg/kartr_gate/2026-11-13` |

Every archive has a `.sig` file. Sculpt checks each download against the public key before installing it.
Check the fingerprint above against the one in `depot/turiya/pubkey` before trusting the key
(`gpg --show-keys depot/turiya/pubkey`).

## Adding the depot to Sculpt OS 26.10

In Sculpt's **+** menu, enter `https://raw.githubusercontent.com/ppathak3383/turiya/main/depot/turiya` and confirm.
A **Turiya** entry then lists both packages.

If your Sculpt version doesn't accept a URL there, add the depot user by hand on the Sculpt partition:
`depot/turiya/download` (one line: the download location above) and `depot/turiya/pubkey` (the key file).

## The packages

- **turiya_hello**: logs one line and holds no capability except the log.
- **kartr_gate**: holds an irreversible action (pay, transfer, delete, send_as_user, publish, share_file) unless it carries the owner's HMAC-SHA256 approval over `type|target|amount`. Its test configuration uses a **test key**, not a secret.

Built with Goa and the Genode tool chain 25.05; see `BUILD.md`.

## kartr_gate 2026-11-13b (current)
The index now points to `turiya/pkg/kartr_gate/2026-11-13b`. The package runs a small init with two parts:
- **kartr_gate:** reads the actions an agent proposes from an `actions` ROM. In this package that ROM is a test file with 12 actions (`turiya/raw/kartr_gate`).
- **report_rom:** prints the gate's verdicts to the log.

The owner's key and mandate are in the package's config. Expected log: `kartr_gate: 3 allowed, 9 held`.
Checked on Genode's Linux base, and the same code on seL4 in QEMU and VirtualBox (`phase1/runs/2026-11-13-1d/`).

The older package `2026-11-13` stays in the depot because it is signed and historical. It had two holes:
- no mandate check, so an unknown type such as `Pay` was allowed;
- `|` was accepted inside fields, so one approval could fit a second action.

It only logged its decisions, so nothing was ever executed. Don't install it.
