<p align="center">
  <a href="https://ps2homebrew-arcade.github.io/pcsx2x6/">
    <img src="./bin/resources/icons/AppIconLarge.png" alt="Logo" width="25%" height="auto">
  </a>

  <p align="center">
    A fork of PCSX2 to emulate NAMCO System 246 and System 256 arcade units
    <br />
  </p>
</p>

# pcsx2x6: Steam Deck / Sinden lightgun fork

A Steam Deck-focused fork of [`pcsx2x6`](https://github.com/PS2Homebrew-arcade/pcsx2x6) (itself a fork
of [PCSX2](https://github.com/PCSX2/pcsx2)). Upstream pcsx2x6 already boots and runs NAMCO System 246/256
arcade games; this fork makes a **Sinden / absolute-pointer lightgun** usable on the Deck, including in
**Game Mode (gamescope)**, where stock PCSX2 lightgun aim does not work.

## What this fork adds (over upstream pcsx2x6)

- **Direct evdev lightgun source for gamescope / Game Mode.**
  gamescope delivers only *relative* pointer motion, so a lightgun's absolute on-screen position never
  reaches the emulator and aim is broken in Game Mode. This fork reads the gun's absolute `ABS_X`/`ABS_Y`
  off `/dev/input` and drives the emulator's pointer directly, bypassing the compositor (similar to
  RetroArch's `udev` input driver). Works in **both Desktop and Game Mode.**
  Enable it with the environment variable `PCSX2_EVDEV_LIGHTGUN=auto`.

- **Per-game JVS gun mappings.**
  The arcade JVS I/O switch bits (trigger, foot pedal / reload, on-screen sensor) are mapped per game ID
  so each title reads the gun on the bits it expects.

## Using a lightgun on the Deck

1. Launch the emulator with `PCSX2_EVDEV_LIGHTGUN=auto` in the environment.
2. Bind the gun's trigger to the GunCon2 **Trigger** input in the emulator's settings.
3. Run the game's in-arcade **gun calibration** once.

For base emulator setup (BIOS, security dongles, game discs), see the
[upstream pcsx2x6 site](https://ps2homebrew-arcade.github.io/pcsx2x6/).

## Status & limitations

- **Single-gun.** Two-player dual-gun support is a work in progress on a separate branch and is **not
  included or functional in this build.** Vampire Night (the only System 246/256 two-guns-on-one-screen
  lightgun title) drives its guns through an undocumented CCD/IR I/O board that no emulator currently
  reproduces, so its second gun does not fire in-game anywhere.

## Relationship to upstream

This fork tracks upstream pcsx2x6 on `master`; the Deck/Sinden changes live on the `deck-patches` branch.

## Special Thanks to:
- The [PCSX2](https://github.com/PCSX2/pcsx2) team and upstream
  [pcsx2x6](https://github.com/PS2Homebrew-arcade/pcsx2x6): Tovarichtch, DiscoStarSlayer, Uyjulian,
  krHACKen, and many more for all their help
- Berion for the app icon
- The Sinden Lightgun community
