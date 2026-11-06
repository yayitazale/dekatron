# The file format of the clock

The clock takes four kinds of file. Three of them are one format, the **store** (`EKST`, version 2), and the fourth is a firmware. The clock, the page of the clock, the creator on the site and `tools/check_bin.py` tell them apart by what is inside, never by the name.

| File | What is inside | Name by convention |
|---|---|---|
| Theme | A theme and its four animations, no faces | `name.theme.bin` |
| Face | One clock face, nothing else | `name.face.bin` |
| Backup | Anything else: up to four faces, a theme, other clips | `dekatron-backup-YYYYMMDD.bin` |
| Firmware | An ESP32 app image (`firmware.ota.bin`) | `dekatron-X.Y.Z-full.ota.bin` |

A new store replaces the whole store of the clock. The page of the clock reads the current store first, adds or replaces the part you upload, and sends the result, so a theme keeps the faces and a face keeps the theme.

## The store

All numbers are little endian. The store lives in a 1,152 KB (`0x120000`) region of the flash at `0x2E0000`, and a file may not be larger. In order:

**Header, 20 bytes**

| Offset | Size | |
|---|---|---|
| 0 | 4 | `EKST` |
| 4 | 4 | Version, `2` |
| 8 | 4 | Size of the whole file |
| 12 | 4 | CRC-32 (the one of zlib) of everything after the header |
| 16 | 4 | Number of entries, 1 to 255 |

**Entries, 32 bytes each**, one per face, clip or theme:

| Offset | Size | |
|---|---|---|
| 0 | 16 | Name in UTF-8, padded with zeros. 1 to 15 bytes |
| 16 | 4 | Offset of its table of images |
| 20 | 4 | Frames |
| 24 | 2 | Frames per second, 1 to 60 |
| 26 | 1 | Images per frame (`0` for a scene) |
| 27 | 1 | Kind: `0` clip, `1` face, `2` theme |
| 28 | 2 | Number of blobs of a scene |
| 30 | 2 | Flags. Bit 0: a scene has a track of lights |

**Tables of images**, 8 bytes each: the offset from the start of the file, and the size. An entry has `frames × images per frame` of them, or for a scene `1 + frames + blobs (+ 1 with lights)`. **The images** follow, and an image that appears twice is stored once and pointed to twice.

### Faces

One entry of kind `1` with one frame and **ten JPEG images, one per digit 0 to 9**, of 135 × 240 pixels, **baseline** (not progressive), best at 4:2:0. It may have an **eleventh image**, JSON of at most 512 bytes in UTF-8, with the information of the face: `author`, `license`, `description`, `version`. A store holds at most **four** faces, besides the one built into the firmware (Nixie).

### Clips

Kind `0`. Either **one JPEG per frame** shown on every tube (`images per frame` = 1), or **six JPEG per frame**, one for each tube, listed from the leftmost tube, which is tube 5 (`images per frame` = 6). The clock plays about 3 to 4 fps of six images; for animations that cross the tubes use a scene.

### Scenes

A scene (`images per frame` = 0, kind `0`) stores only what changes between frames, like a GIF. A scene crosses the six tubes as one strip of **1370 × 240 pixels**: the tubes are 135 pixels wide and start 247 pixels apart, tube 5 first, and the 112 pixels between them are never shown. The table holds, in this order:

1. **The palette**: 512 bytes, 256 colors of 16 bits (RGB565), where index 0 is black, the color of the tubes at the start.
2. **One record per frame**: a count (u16) and then that many patches of 8 bytes: `tube`, `x`, `y`, `width`, `height` (one byte each), a zero byte, and the number of the `blob` (u16). A patch must fit in its tube (135 × 240).
3. **The blobs**: the pixels of a patch, as palette indices row by row, compressed with a simple run-length code. A byte below 128 repeats the next byte that many times plus one; a byte of 128 or more is followed by that many minus 127 bytes, copied as they are.
4. **The lights** (flag bit 0): 18 bytes per frame, six colors of red, green and blue, one for each LED. The LED of a tube has the number of the tube.

Each frame starts from what the tubes showed after the one before; the first one starts from black.

### Themes

A theme is one entry of kind `2` with its name and one image, JSON of at most 512 bytes with `author`, `license` and `description`, and **exactly four clips named `boot`, `connecting`, `online` and `countdown_end`**, usually scenes, with lights. A theme may take at most **800 KB** (counting each distinct image once), so that the faces of the user still fit. It plays `boot` when the clock starts, `connecting` while it looks for the Wi-Fi, `online` when it connects, and `countdown_end` when a countdown ends.

## Firmware

The image that ESPHome writes as `firmware.ota.bin`: it starts with the byte `0xE9`, is for the ESP32 (chip number 0 in its header) and takes at most `0x160000` bytes, the size of the app partition.

## Checking a file

```bash
python tools/check_bin.py my-theme.theme.bin
python tools/check_bin.py my-theme.theme.bin --gallery   # also what the gallery of the project asks
```

The checker applies the rules of the clock and says what is wrong and how to fix it, one line per problem. The pages use the same rules (`checkBin` in `web/store.js`), and a test keeps the two the same.

The gallery of the project takes themes and faces with an `author` and the license **`CC-BY-SA-4.0`**.

## Changing the format

`tools/make_clips.py` writes it, `web/store.js` reads and writes it for the pages, and `components/elekstube_ips/store.cpp` reads it in the clock. `tests/` compares the three.
