# Crystalbound showcase seeds

These requested seeds are reference cases from the measured complete-cave
corpus. Generator version 3 produced the metadata below. Every entry passes the
same topology, geometry, collision, reachability, elemental, crystal,
exit-arch, finite-value, budget, and repeatability checks as the automated
corpus.

| Requested seed | Acceptance | Rooms | Exit distance / farthest | Elevation span | Topology fingerprint | Scene fingerprint | Best manual use |
| ---: | --- | ---: | ---: | ---: | --- | --- | --- |
| `42` | Normal, first attempt | 7 | 6 / 6 | 1.650 m | `859e05617365168d` | `1f8517f2c8d6c15a` | Primary regression and manual playthrough |
| `123456789` | Normal, first attempt | 7 | 6 / 6 | 1.650 m | `93d42aeff939f0cb` | `52f9039c6baa9835` | Alternate deterministic material variation |

Both seeds use the same authored Start-to-Exit chamber order, six level tunnel
connections, no bridges, and the five Fire, Water, Earth, Air, and Aether
chambers and crystals. The compact list is intended for manual playthroughs; it
does not replace the 256-seed CI corpus or the opt-in 10,000-seed local Release
audit.
