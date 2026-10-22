# R9700 production snapshot (HomeGPU)

What runs in production on the AMD Radeon AI PRO R9700 (gfx1201, 32 GB), so it can be rebuilt.

- `strata-unsloth-ud-iq4_xs.json`: the engine config (`--config` of `serve/server.py`). The API key is redacted;
  set the real one privately before deploying. MTP is on (`--mtp /srv/LLM/Strata-data/mtp/rt`,
  `--spec 4 --spec-min-p 0.5`), with full 262,144-token context and int8 KV.
- `BUILD.json`: the record of `engine/strata` (binary sha256, commit, local patches).
- `systemd/`: the user units (`~/.config/systemd/user/`): the server, its autostart drop-in, and the tailnet
  socket proxy.

Model data is not in git: the Unsloth UD-IQ4_XS GGUF (`/srv/LLM/Strata-data/models/unsloth-UD-IQ4_XS/`), its pack
(`/srv/LLM/Strata-data/packs/unsloth-ud-iq4_xs`), the MTP runtime (`/srv/LLM/Strata-data/mtp/rt`, 787 MB) and the
vision projector (`/srv/LLM/Strata-data/vision/qwen3.8-flash-next/`).

Build (ROCm 10.1): `cd build-r9700-native-rocm10.1 && ninja strata`, then copy `strata` to `engine/strata`.
For a fresh build directory, follow [AMD HIP setup](../../docs/AMD_HIP.md).

## Production profile, 2026-10-21

- `parallel: 2` becomes native `--batch 2`. `--batch-mtp` enables a private drafter for each slot; normal
  solo MTP remains available when only one request is active.
- QSA selection uses `STRATA_SELECT_WMMA=1`. This is separate from the existing `STRATA_HIP_WMMA` setting.
- KV streaming uses a 20,480-token GPU-resident window (`--kv-resident 20480`) with host backing for the
  full context. Both `--no-kv-grow` and `STRATA_KV_GROW=0` keep the window fixed. Conversation parking
  remains disabled.
- The service limit is 59 GiB, with service swap disabled. The nominal 50 GiB expert budget is not a
  total-process limit; startup clamps the actual expert arena after allocating session state.
- The API remains bound to `127.0.0.1`; the existing private tailnet socket proxy is unchanged.

The unit snapshot declares the effective `MemoryMax=59G` directly. On the running host this limit was
persisted with a systemd user control drop-in overriding the original unit's 58G. Do not copy runtime
control files or credentials into this directory.

The native binary is unchanged from `BUILD.json`; these are runtime settings, not a new engine build.
The root runtime config, backups, profiler captures, full benchmark responses, and model assets are
not part of this snapshot.

## Measurements and the rejected third slot

Earlier warm 32K prefill measured about 3,375 tok/s (about 3,190 before the 128-row expert work items and
value-major scales). The following newer measurements used the same R9700 binary recorded in
`BUILD.json`, the production context capacity, and uncached synthetic evidence-audit inputs.

An identical 29,946-token first-admission input, before any other stream decoded, showed:

| Configured slots | Prefill time | Prefill tokens/s |
| --- | ---: | ---: |
| 2, restored control | 9.4742 s | 3,160.8 |
| 3 | 20.8682 s | 1,435.0 |

Three slots cut the host expert arena from 43,013 to 39,558 MiB. RAM copies backing GPU expert slots
borrowed during prefill fell from 9.06 to 4.82 GiB: startup reported 4,022 of 4,388 borrowed experts
retained in RAM with two slots, versus 2,146 of 4,387 with three. The engine explicitly warns that the
remainder are read from model files when those slots are lent. Physical storage I/O was not separately
traced in this comparison. Three slots were rolled back to preserve prefill performance.

The concurrent decode measurements were taken over matched intervals where all requested slots were
decoding, including intervals without token progress:

| Workload | Decode tokens/s per stream | Aggregate tokens/s | Batch MTP accepted/offered |
| --- | ---: | ---: | ---: |
| Two inputs of 127,978 / 127,979 tokens | 33.49 / 33.62 | 67.11 | 3,127 / 3,334 (93.8%) |
| Three inputs of 29,946 / 29,947 / 29,949 tokens | 28.80 / 28.81 / 28.83 | 86.43 | 5,094 / 5,425 (93.9%) |

These are different context lengths, not an isolated two-versus-three-slot decode speedup. In the
two-stream 128K run, the first prefill measured 3,311 tokens/s. The second reported 2,470 tokens/s
because its 51.8117 s prompt timer included 14.034 s decoding the other request between prefill chunks;
excluding that logged interval gives about 3,388 tokens/s.

Each request allowed up to 31,000 output tokens and stopped naturally at roughly 3,400-3,600 tokens.
All 32 factual values per response were correct, with no observed cross-request mixing in the complete
responses. The same minor citation omission remained: Q08's evidence array omitted R10 even though
the explanation noted the approved change was not applied. This fixture is not a general quality
guarantee. No OOM or service swap occurred; minimum host-available RAM was 3.31 GiB in the two-stream
128K run and 3.67 GiB in the three-stream 30K run.

Batch proposal acceptance comes from native batch summaries, not the API's solo-drafter counters.
Future production adoption must compare uncached first-admission prefill on identical inputs, not just
functional correctness, memory safety, or aggregate decode throughput.

LAN listener (valhalla-core only): `systemd/strata-lan.socket` + `strata-lan.service` proxy 192.168.50.75:8080 to
Strata's loopback port for LiteLLM on valhalla-core. Access control is a root nftables table (`root/strata-lan.nft`)
that drops port 8080 on 192.168.50.75 from any source except 192.168.50.215, loaded at boot by
`root/strata-lan-firewall.service` (install: `sudo sh root/install-strata-lan-firewall.sh`). The socket only listens
when `/run/strata-lan-firewall.ok` exists, so it fails closed. systemd IPAddressAllow/Deny is not enforced in user units.
