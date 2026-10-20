# R9700 production snapshot (HomeGPU)

What runs in production on the AMD Radeon AI PRO R9700 (gfx1201, 32 GB), so it can be rebuilt.

- `strata-unsloth-ud-iq4_xs.json`: the engine config (`--config` of `serve/server.py`). The API key is redacted;
  set the real one at deploy. MTP is on (`--mtp /srv/LLM/Strata-data/mtp/rt`, `--spec 4 --spec-min-p 0.5`),
  full 262,144-token context, int8 KV.
- `BUILD.json`: the record of `engine/strata` (binary sha256, commit, local patches).
- `systemd/`: the user units (`~/.config/systemd/user/`): the server, its autostart drop-in, and the tailnet
  socket proxy.

Model data is not in git: the Unsloth UD-IQ4_XS GGUF (`/srv/LLM/Strata-data/models/unsloth-UD-IQ4_XS/`), its pack
(`/srv/LLM/Strata-data/packs/unsloth-ud-iq4_xs`), the MTP runtime (`/srv/LLM/Strata-data/mtp/rt`, 787 MB) and the
vision projector (`/srv/LLM/Strata-data/vision/qwen3.8-flash-next/`).

Build (ROCm 10.1): `cd build-r9700-native-rocm10.1 && ninja strata`, then copy `strata` to `engine/strata`.

Measured warm 32K prefill: ~3,375 tok/s (was ~3,190 before the 128-row expert work items and value-major scales).
