# Changelog
There are no prebuilt NROs in any release: each release holds the source and the build scripts. You build the NRO
yourself, from your own copy of the game ([docs/HOW_TO_BUILD.md](docs/HOW_TO_BUILD.md)).

scpt-nx starts from [scct-nx](https://github.com/itouchdennis/scct-nx) v0.3.0, which starts from
[scda-nx](https://github.com/itouchdennis/scda-nx) v0.4.9; their changelogs have the history of the shared code. The
entries below v0.3.4 are scct-nx's.
## v0.3.5

### Changed
- Rename project references from scda to scpt.
- Update builder image references to scpt-nx-builder.
- Align GitHub Actions workflows with the scpt-nx project.

## v0.3.4
The first release of Pandora Tomorrow's campaign (`Offline.xbe`, XDK 1.0.5788) on Linux and the Switch, into the first mission.
- The XDK functions found anew for the older SDK; the launcher relaunches `Offline.xbe` for each mission.
- Rendering: the D3D getters the game reads back (viewport, vertex shader, projection-viewport matrix), visibility
  tests on occlusion queries, the glow and thermal corner passes, stages without a texture as the console reads
  them, vertex registers keeping their last value, and the light passes over walls (depth EQUAL) steady.
- Sound: the stream thread's 10 ms rounds, so speech, music and ambience play at their speed.
- The settings in the game's own menu: RECOMP OPTIONS in the pause menu opens a page in the game's frame with the
  display, graphics and controller settings and gyro aiming. Gyro aiming and the mouse turn the view through the
  right stick, the gyro only while aiming or zooming if asked; draws wait up to 50 ms a frame for their shaders
  instead of vanishing.
- German and Italian: the console reports PAL (with 60 Hz) for them, which the game needs to play them. The fps
  counter shows in Pandora Tomorrow too.
- The builders, the README and the build guide for Pandora Tomorrow; the Docker builder was run from an ISO to the
  NRO.
- F9 on Linux traces a frame with every draw's target, textures and shaders, for debugging.

## v0.3.0
The port's settings in the game's own options menu, and gyro aiming.
- The settings in the game's options menu, as in Double Agent: Options > Recomp Options and Options > Controller,
  each with its sections, A lists a row's values (B goes back), Left and Right step them, a button row rebinds.
- The overlay's rows change with the D-pad's left and right, and A lists their values. The overlay is off now that
  the game's menu has the settings: `settings_overlay = true` in `scct.toml` turns it on.
- Gyro aiming and the stick's horizontal and vertical sensitivity, on the player camera as in Double Agent, and
  while aiming (the gun raised, the sniper scope) their own sensitivities and, if asked for, the gyro alone.
- The options list's entry is RECOMP OPTIONS, one line in each language.

## v0.1.1
The first release (v0.1.0 was never released). On the Switch the game runs from the menus through the briefing into
the first mission.
- `sc3.xbe`, the campaign, recompiled: the XDK functions and data the host replaces or reads found by their code in
  Double Agent's, which uses the same XDK.
- Boots on Linux with Vulkan to the title screen.
- Kernel: a set auto-reset event goes to the thread waiting for it, as on the Xbox; Chaos Theory's loading screen
  otherwise kept the level from ever loading.
- Its own folder on the Switch, `sdmc:/switch/scct/` (`scct.nro`, `scct.toml`), beside Double Agent's.
- The builders (Docker for Windows and Linux, `build-nro.sh` for Arch) and the guides for Chaos Theory.
- Files: the SD card's open files no longer run out: the handles of one file share one stream (the autosave after
  the briefing crashed).
- The settings panel as an overlay: L + R + D-pad right on a controller (F10 on a keyboard) opens it, B closes it.
  Display, graphics, general and controller settings as Double Agent's, the frame rate counter, and the controller's
  buttons rebound by the Xbox controls; what needs Double Agent's game patches (field of view, gyro look, stick
  sensitivity) waits for Chaos Theory's own.
