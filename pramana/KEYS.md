# Where Turiya's keys live (decided 12 Nov 2026)

| Key | Lives in | Curve | Why |
|---|---|---|---|
| Everyday key (laptop) | Windows TPM 2.0, via the Microsoft Platform Crypto Provider | ECDSA P-256 | The TPM provider offers RSA and ECDSA P-256/384/521, not Ed25519. The key can't be copied out of the chip. |
| Backup key (phone) | Android Keystore, StrongBox secure element where the phone has one | ECDSA P-256 | Newer Android adds Curve25519 to the TEE, but StrongBox (the separate secure chip) is not expected to support it. P-256 works in both. |
| Recovery phrase | Paper at home, or Gmail only after encryption | — | Lets you cancel a lost device's key and issue a new one. A plain copy online would let anyone in your Google account sign as you. |
| Software keys (tests, models, tools) | Software | Ed25519 | Fast, small; fine where no hardware chip is involved. |

**Format change made:** Pramāṇa records now accept both schemes. A P-256 public key is written as `p256:<compressed point hex>`; an Ed25519 key as plain hex. `tests/test_p256.py` 5/5: P-256 records verify, chain to each other and to Ed25519 records, a P-256 impostor using the same name is not trusted, a corrupted P-256 signature fails.

**Family key exchange (design, not built yet):** each phone shows its public key as a QR code; family members scan each other in person once; the keyring stores name + key. Nothing secret is ever shown or sent. A key met only online is never added to the keyring.

**Next (Phase 1+):** call the TPM through Windows CNG (NCryptCreatePersistedKey with the Platform Crypto Provider, ECDSA_P256) instead of software keys, and Android Keystore with `setIsStrongBoxBacked(true)` on the phone.

Sources: nsoftware TPM notes (RSA and ECDSA P-256/384/521 only with TPM); Android framework commit on KeyMint Curve25519 support ("implementation backed by a dedicated secure processor is not expected to implement it"); Microsoft CNG key storage provider docs.
