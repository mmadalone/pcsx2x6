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
  Enable it with `PCSX2_EVDEV_LIGHTGUN=auto`, which finds a Sinden gun automatically (USB vendor `16c0`,
  or "sinden" in the device name). See **Setup** below.

That source (and this auto-discovery) is the fork's one functional addition; the System 246/256 emulation
and its per-game gun handling are upstream pcsx2x6's.

## Setup (new users)

**What you provide** (this fork is just the emulator):
- The System 246/256 **BIOS** and your **games** (security dongles, discs/CHDs, `.acgame` launchers).
  Follow the [upstream pcsx2x6 site](https://ps2homebrew-arcade.github.io/pcsx2x6/) for that; this fork
  does not change it.
- A **Sinden lightgun with its own Linux driver running**, so the gun appears as an input device with
  absolute axes. The Sinden software (LightgunMono) is closed-source and download-only from
  sindenlightgun.com; it is not bundled here.

**Steps:**
1. Download the AppImage from the [Releases](https://github.com/mmadalone/pcsx2x6/releases) page and
   `chmod +x` it.
2. Set up your BIOS and games per upstream.
3. Launch with `PCSX2_EVDEV_LIGHTGUN=auto` in the environment. It finds a Sinden gun automatically (USB
   vendor `16c0`, or "sinden" in the device name); or point it at a specific device with
   `PCSX2_EVDEV_LIGHTGUN=/dev/input/eventN`.
4. Bind the gun's trigger to the GunCon2 **Trigger** input in the emulator's input settings.
5. Run the game's in-arcade **gun calibration** once.

**Updating:** use **Help > Check for Updates** in the emulator, or download the latest AppImage from the
[Releases](https://github.com/mmadalone/pcsx2x6/releases) page.

## Status & limitations

- **Single-gun only. Two-player does not work.** The only System 246/256 two-guns-on-one-screen lightgun
  title, Vampire Night, drives its guns through an undocumented CCD/IR I/O board that no emulator
  reproduces, so its second gun never fires in-game. Some 2-gun input plumbing exists in the code, but it
  yields no working second gun.

## Relationship to upstream

This fork tracks upstream pcsx2x6 on `master`; the Deck/Sinden changes live on the `deck-patches` branch.

## Special Thanks to:
- The [PCSX2](https://github.com/PCSX2/pcsx2) team and upstream
  [pcsx2x6](https://github.com/PS2Homebrew-arcade/pcsx2x6): Tovarichtch, DiscoStarSlayer, Uyjulian,
  krHACKen, and many more for all their help
- Berion for the app icon
- The Sinden Lightgun community
