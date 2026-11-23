# Changelog

## 0.3.2 — 2026-11-23

- Lowered the minimum OnAir version to 4.3.0. The official 4.3.0 and 4.5.0 sources use the same plugin ABI and `lora.packet` event fields required by this plugin.
- Updated the installation guidance and package version. No packet filtering or map behavior changed.

## 0.3.1 — 2026-11-22

- Added an explicit switch to use a fixed receiver position instead of GPS.
- Fixed coordinates can come from OnAir's Linux station settings or from manual latitude and longitude fields.
- Labeled the position source in live map points and new CSV sessions; older CSV sessions still open in the archive view.
- Added a German quickstart, a deterministic single-file package build, and Linux tests.

## Before 0.3.1

The prototype added a local live browser map, fictional simulation, per-activation CSV sessions, archive CSV import, and status counts. Older test packages remain local and are not part of the public repository.
