# FiiO Echo Nano custom firmware

Not affiliated with or endorsed by FiiO. FiiO and Echo Nano are trademarks of their owners.

Patches for the official FiiO Echo Nano firmware **V1.7.0** that add media-library
features and make the library refresh much faster. This repository contains only
the patches and the tools that apply them. It does **not** contain FiiO's
firmware: you download the official image yourself and build the custom image
on your computer.

> **Use at your own risk.** This is experimental, unofficial firmware. Flashing
> any firmware can leave a device unusable. Read [Recovery](#recovery) before you
> start. Nothing here changes the boot loader, and so far a stock image has always
> been able to replace a custom one, but there is no guarantee.

## Features

| Feature | Status |
|---|---|
| **Faster library refresh**: about 5× faster on a FLAC-heavy card (148 s → 28 s for 2,000 songs) | tested on device |
| **Recent Albums** (Media Library): all albums, newest first, by the date the album's folder was created | tested on device |
| **Shuffle** (main menu, 6th item): shuffle play over the whole card in one press | tested on device; the menu icon is new and not yet confirmed |
| **Shuffle scope**: Shuffle Play covers the whole card instead of the current folder | tested on device |
| **Unsupported tracks** are skipped without a popup; playback stops after 10 failures in a row | tested on device |
| **Album Artists / Artists**: the stock Artists list, which groups by album artist, is renamed "Album Artists"; a new "Artists" list groups by each track's own artist | tested on device |
| `CFWLOG.TXT` in the card's root after each refresh: timings and counters | tested on device |
| **Power saving**: lower CPU clocks during playback (decode core 144-288 MHz instead of 350-450 MHz for MP3, AAC, WMA, WAV, OGG, FLAC and ALAC), the CPU sleeps between events with the screen on, and the scroll wheel is polled every 1 ms instead of every 0.4 ms | new in 6.0.5; battery gain not measured |

About shows the custom version (6.0.x) instead of the stock version.

After flashing, run **Media Library → Media Library Refresh** once: Recent Albums
and Artists need data the stock refresh doesn't write.

## Requirements

1. The official **Echo Nano V1.7.0** firmware file `NANOV170.IMG`, from FiiO's
   website. The build checks its SHA-256 and refuses any other file:
   `54a45d233a23c3ce142d1da9c093cc40c46eba417aa91fe915ee867726f4a116`.
   Other firmware versions are **not** supported: the patches are written for the
   exact code addresses of V1.7.0.
2. GNU make 4.3 or newer, `python3` (3.8 or newer, standard library only) and
   the Arm GNU toolchain (`arm-none-eabi-gcc`, binutils 2.39 or newer):
   - macOS: `brew install make python && brew install --cask gcc-arm-embedded`,
     then use **`gmake`** instead of `make` (the system make is too old).
   - Debian 12+ / Ubuntu 24.04+: `sudo apt install make python3 gcc-arm-none-eabi`
   - Nix: `nix-shell` in this directory provides everything.

## Build

```sh
cp ~/Downloads/NANOV170.IMG .        # the official V1.7.0 image
make check                           # confirms it is the expected stock file
make DATE=20261101                   # writes out/NANOV170.IMG
```

`DATE` is the build date stored in the image. The device boots whichever
firmware copy has the **newest** date, so every image you flash must have a
newer date than the one before it (the stock V1.7.0 image is dated 2026-08-24).
Without `DATE` the build uses today's date.

## Install

1. Connect the Echo Nano by USB and copy `out/NANOV170.IMG` to the root of its
   internal storage.
2. Eject it and restart the player; it installs the update while it boots.
3. Check **About**: it should show `6.0.x`.
4. Run **Media Library Refresh** once.

## Recovery

To go back to the official firmware, build the stock code with a newer date and
flash it the same way:

```sh
make recovery DATE=20261102          # newer than any custom build you flashed
```

This writes `out/recovery/NANOV170.IMG`: FiiO's unmodified code with only the
date changed, because the device would ignore the original file while a
newer-dated custom build is installed.

## Known issues

- Back from the new **Artists** list lands on **Album Artists**, and Back from
  **Recent Albums** lands on **Albums**.
- Both artist lists use the same list type internally. If a song plays from one
  of them and you pick the song at the same position in the other, the player
  reopens Now Playing instead of starting the new song.
- FLAC files tagged only with `ALBUM ARTIST` or `ALBUM_ARTIST` (instead of
  `ALBUMARTIST`) are grouped by track artist in Album Artists. This is stock
  behaviour.
- Playback can freeze on some files that show "Format Not Supported!". The cause
  is not found yet.
- Shuffle order is not kept across power cycles.
- Power saving (6.0.5) is not yet tested on hi-res files (FLAC 24/192): watch for
  dropouts. A very fast spin of the scroll wheel may skip steps.

## How it works

The image is the stock firmware with patched bytes, a recomputed CRC and a new
date. Everything is applied at build time from three kinds of input:

- `patches/*.patch`: data patches, written at a runtime address inside a
  firmware module, or as UI strings by ID and language:
  ```
  32:0307cf14:u"6.0.5"                 # module 32, UTF-16 string + NUL
  22:030794f4:0a21                     # raw hex bytes
  lang:87:en:u"Recent Albums"          # UI string 0x87, English
  ```
- `patches/*.S`: small Thumb-2 code patches, each assembled at its target address
  (`@ patch MOD ADDR`; see `patches/examples/template.S` and `patches/fw.inc`).
- `src/`: C code that runs from free RAM during the media-library refresh
  (faster tag reading and sorting, Recent Albums and Artists tables, the log).
  `src/hooks.txt` lists the firmware calls redirected into it.

`tools/rknano_fw.py` reads and writes the RKNano image format (`info`, `crc`,
`check`, `patch`); `tools/build_blob.sh` builds the C code; `tools/asm2patch.sh`
assembles the `.S` patches.

### Image format

| Offset | Content |
|---|---|
| `0x000` | u16 year, u16 monthday (BCD build date). Loader boots the valid flash slot with the newest date |
| `0x010` | `"Rockchip"`, `0x030` `"RKnano SDK 1.0"` |
| `0x070..0x150` | resource section table (offset/length pairs) |
| `0x1F4` | firmware length, `0x1F8` `"RKnanoFW"` (checked by the loader) |
| `0x200` | module table: `load_base`, `count`, then 8-word entries `code_load, code_exec, code_len, data_load, data_exec, data_len, bss_exec, bss_len` |
| `0xF8C` | module 0 (resident kernel) at `0x03050000`; overlay modules (UI screens, services) are swapped in at `0x03078ab0..` |
| `0x15212C` | sysdefault · fonts · codepage · lang (21 languages) · images |
| `fw_len` | copy of the `0x200` header, compared with sector 0 by the loader |
| `end-4` | Rockchip CRC32 (poly `0x04C10DB7`, MSB-first, init 0) over everything before it, LE |
