# Is it safe?

You are replacing the firmware of a device you own, so this page says what protects it and what could go wrong.

## Your copy comes first

The original firmware of the EleksTube IPS Gen2 is tied to the address (MAC) of each clock and cannot be downloaded again. The installer **reads the whole 4 MB flash twice**, compares the two copies, saves them outside the project and does not go on without them. Keep the file: it is the only way back.

## One command back

The copy is written back with one command (`tools/install.py restore` will do it, with `esptool` underneath), and the clock is as it was. This has been tried on a real clock: the copy was written, the digest matched, and the original firmware worked.

## If an update fails

- A new firmware that does not start is dropped, and the clock goes back to the previous one by itself.
- A firmware is only accepted with a **password** set on the clock, and every file is checked by the page and by the clock before it is used.
- A file for the clock that is damaged is refused before anything is erased. A damaged theme or face that only shows at the end of the upload is also refused, which is why every dialog offers a **backup first**.

::: tip After an update, do not reset the clock for a minute
The firmware is confirmed as good a little after it starts. A reset or a USB flash right after an over-the-air update counts as a failed start and the clock goes back to the old firmware without an error.
:::

## What leaves your network

Nothing, from the clock. The page of the clock asks this site for a static `versions.json` and nothing else, and the creator runs in your browser and sends nothing anywhere.

## Open

The code is under the [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) or later, and themes and faces under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). Read it, change it, and share your changes the same way.

Dekatron is compatible with the EleksTube IPS clocks and has no affiliation with EleksTube. You use it at your own risk, as with any firmware you flash yourself.
