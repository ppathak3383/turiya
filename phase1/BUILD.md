# Building Turiya's Genode components (Phase 1)

Two components so far, both built with Genode's Goa tool and run on Genode's Linux base (13 Nov 2026):

| Component | What it does | Result |
|---|---|---|
| `goa/turiya_hello` | Logs one line; holds no capability except the log | runs: `[init -> turiya_hello] Turiya: Sakshi is awake…` |
| `goa/kartr_gate` | Phase 0's core rule as a component: irreversible actions held unless they carry the owner's HMAC-SHA256 approval over `type\|target\|amount` | 8/8 (`runs/2026-11-13-goa/kartr_gate_log.txt`) |

## One file to create by hand

The laptop bridge refuses to write files named `Makefile`, so each project needs its one-line `src/Makefile` created by hand (or by the build machine):

- `goa/turiya_hello/src/Makefile`: `turiya_hello: turiya_hello.cc`
- `goa/kartr_gate/src/Makefile`: `kartr_gate: kartr_gate.cc`

(`turiya_hello` already has it on the laptop.)

## Build machine

Ubuntu 24.04 or newer (the tool chain needs glibc 2.38; the laptop's Claude sandbox is 22.04 and can't run it).

```
sudo apt install expect bubblewrap tcl git make xmllint
git clone https://codeberg.org/genodelabs/goa.git           # GitHub copy is retired
curl -LO https://github.com/genodelabs/genode/releases/download/25.05/genode-toolchain-25.05-ubuntu-24.04.tar.xz
echo "6cab8e5f4490399a296a73b01695d5fb055a40f6ac04d61a3f7a7344003eb739  genode-toolchain-25.05-ubuntu-24.04.tar.xz" | sha256sum -c
mkdir tool && tar xJf genode-toolchain-25.05-ubuntu-24.04.tar.xz -C tool
echo "lappend allowed_tools $PWD/tool" > ~/goarc
# in each project directory:
echo "set cross_dev_prefix \"$PWD/../tool/usr/local/genode/tool/25.05/bin/genode-x86-\"" > goarc
goa run
```

Goa downloads the Genode depot archives it needs (with signatures) from depot.genode.org.

## Reading the log

On some machines `goa run` prints nothing although the components run. `cd var/run && ./core` starts the same system; its log goes to stdout (one write per character). The 13 Nov logs were captured with `strace -f -e trace=write`.

## Approvals in the test config

`kartr_gate/pkg/runtime` holds a **test key**, not a secret. The approvals in it were made with Python:
`hmac.new(key, b"pay|ramesh.kumar@ybl|15000", hashlib.sha256).hexdigest()`. In Turiya the key will live in the Sākṣī core (TPM-backed) and reach the gate through a session no agent can open.

## Next

1. Run `turiya_hello` and `kartr_gate` inside Sculpt (needs the Sculpt screen: storage = RAM fs, network on, add the package).
2. Give `kartr_gate` a real input: a ROM written by the agent component, and a report the owner's screen reads.
3. Run the same packages on the seL4-based Sculpt image (see `SEL4.md`) once the Sculpt step works. Goa packages are kernel-independent; only the base system changes.
