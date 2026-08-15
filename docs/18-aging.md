# Citizen age — finding the field

**A parked plugin.** `plugins/aging/aging.cpp` is in the tree and compiles, but
`build.bat` skips the folder by name, so no `aging.dll` is built or shipped. Deleting
that skip block is the whole of turning it back on. See
[09-plugins.md](09-plugins.md) for the mechanism a plugin uses.

**It is the measuring half of a feature that does not exist yet.** It reads citizens
and changes nothing whatsoever — no hook on anything the simulation writes, no patched
byte, no setting that alters the game. What it is for is finding two numbers that
static analysis could not settle:

- which field of a `Person` is the age
- how much of it a citizen gains per calendar day

The second is the whole of the correction that would follow: one year of age per 365 of
those steps.

## Why a probe and not a patch

Because the field is not where the notes said it was, and **both** candidates static
analysis offered turned out to be something else. This is the case
[07-pitfalls.md](07-pitfalls.md) files under *a guess written down as a finding*, and
it is why this plugin measures rather than assumes.

| Candidate | What it actually is |
|---|---|
| `person+0x70` | A **state timer**. [02-findings.md](02-findings.md) called it the age. The per-person tick at `0x832CB0` resets it to zero at `0x8337C0` and `0x8338F9` and accumulates frame time into it in between. What reads it is `0x8368B0`, which turns it into an eight-step factor through the thresholds 30/60/90/120/150/180/210 at `0x90A9B8`. |
| `person+0x65C` | The **walk animation**. It grows as `dt * person[0xA4] * 30.0 * k` with `k` in {1, 2, 5}, which reads like an age — but it *wraps back to zero* past `record[0x9C] - 1`, and it is seeded at birth with `C3DRandom_Float(0, record[0x9C] * 0.5)` so no two citizens move in lockstep. A quantity that loops over a per-object frame count, randomly phased, running 5× while moving, is not a life. |

The game's own script API declares `Person.fAge` as a float and `Person_SetAge` as
instruction 31004, but **neither the names nor the id appear anywhere in the
executable** — the API file is documentation, not data — so there is no string to
cross-reference. The person constructor at `0x823290` never writes an age, and neither
does the per-frame tick: every `dt *` in it lands in `+0x10`, `+0x28`, `+0x48`, `+0x70`,
`+0x90`, `+0x65C` or `+0x680`. So the age is advanced somewhere else, and reading
outward from a wrong guess is exactly how the two rows above were nearly recorded as
facts.

[03-reverse-engineering.md](03-reverse-engineering.md) ranks runtime observation above
static analysis for this shape of question, and it is right here: one in-game week of
watching real citizens answers *which offset* and *how fast* together.

## How it works

| | |
|---|---|
| the people | the engine's live `vector<Person*>`, at `0x9E75B8`…`0x9E75C0` |
| the date | `game+0x590`, day of the year, through the world object pointer at `0x9941F0` |
| the clock | an import swap on `C3D_TERRAIN::Render`. It needs a per-frame call and nothing else here does; it **chains**, so `depletion` and `construction` hooking the same import all survive |

`Person` is `0x750` bytes — `operator new(0x750)` in the constructor at `0x823290`.

A handful of citizens are snapshotted whole, taken spread across the live vector rather
than off the front of it (the front of a loaded save is whoever happens to sit there).
Every time the calendar day changes, each tracked person is re-read and every 4-byte
slot that moved is reported — **as a float and as an int**, because guessing the type is
how the last two candidates were got wrong. The line says which reading passed.

The age is the offset that goes up by the same small amount every day, out of a value
that looks like a human age.

### Re-arming

Loading a save frees every `Person` in the world, so every tracked pointer goes stale at
once. The probe notices — a pointer that no longer reads is dropped, and when the
tracked set empties it re-arms from the new world, at most once per calendar day. It
used to arm once per session and simply follow nobody afterwards, reporting nothing and
never saying why.

## The settings

`plugins/aging.ini`, section `[aging]`.

| Key | Default | What |
|---|---|---|
| `enabled` | 1 | 0 unloads the plugin without reading anything |
| `people` | 4 | citizens to follow, 1…16 |
| `days` | 14 | calendar days to report before the probe stops itself |
| `from`, `to` | 168, 1872 | the byte range of the `Person` object compared. Below `0xA8` is pointers and handles, which would bury the answer |
| `strict` | 1 | only print a slot that went **up** into the `min`…`max` window |
| `min`, `max` | 0, 200 | what counts as a plausible human age |

`strict = 0` prints everything that moved at all. The log gets long, but if nothing has
shown up after a few in-game days the answer is somewhere in it.

## State

**Nothing is confirmed.** The plugin compiles and its guards and bounds are in place,
but it has not been run against a real session for long enough to name the field. That
is the entire remaining work: run it, play a couple of in-game weeks, read the log.

Once the offset and the per-day delta are known, the correction is arithmetic, and the
plugin stops being a probe and becomes a feature — at which point it comes out of
`build.bat`'s skip list.

## Where to go next

- **Run it.** Everything above is preparation; the answer only exists in a log.
- **The writer.** The probe names the field, never the instruction that advances it.
  That is the Ghidra step afterwards: a `.text` scan for a write to that offset, in
  every form, for the reason [07-pitfalls.md](07-pitfalls.md) gives about single-form
  searches.
- **What else reads it.** An age field is read by retirement, by school and
  kindergarten eligibility and by death. Those readers are what a correction has to not
  break, and they are found from the offset once it is known.
