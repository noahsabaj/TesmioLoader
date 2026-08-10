# Day and night, and the calendar

The base game keeps two clocks and runs them at different speeds. The date in
the corner advances once per minute of game time; the sun does not. A full
day/night cycle takes **thirteen calendar days**, and the night part of it lasts
two of them, so a fortnight of dates goes by between one sunrise and the next.

The `daynight` plugin compresses that thirteen-step cycle into one calendar day.
It patches nothing and reimplements nothing: the same `weathers/*.ini` files,
the same fades, the same proportions — the game's own state machine, handed a
date derived from the position inside the current day instead of from the
calendar.

## The calendar

Three fields of the world object, and one function that owns them:

| Field | What |
|---|---|
| `game+0x590` | `int`, day of the year, `0..364` |
| `game+0x594` | `int`, the year |
| `game+0x59C` | `float`, how far into the current day we are, `0..60` |

`0x3346E0` is the day tick. Every frame it adds `C3D_TIMER::PowerTime` to
`+0x59C`, and the moment that passes the `60.0f` at `0x90AA90` it zeroes the
field, increments `+0x590`, and wraps at `365` (`0x16D`) into `+0x594`:

```c
time = PowerTime(timer, 0.001f, false, false) + game[0x59C];
game[0x59C] = time;
if (time <= 60.0f) return;
game[0x59C] = 0.0f;
...
game[0x590] += 1;
if (game[0x590] == 365) { game[0x594] += 1; game[0x590] = 0; }
```

**One calendar day is 60 units of scaled game time and nothing else.** That the
day counter really is a day of the year rather than a tick counter is settled by
`0x334340`, which compares it against `25`, `50`, `52`, `128`, `245`, `247` and
`326` — the winter boundaries, per climate.

## The other clock

The day and night themselves come from a separate state machine, `0x333B30`,
called once per tick from the same frame function as the day tick. It steps once
per **calendar day**:

```c
phase = game[0x590] % 13;
```

| `phase` | Lighting | `game+0xE74`, the night factor |
|---|---|---|
| 0–5, 11, 12 | `day1.ini`, or `overcast1.ini` if the roll said so | 0 |
| 6 | `sunset2.ini` | 0 |
| 7 | `night.ini` | ramps 0 → 1 over the first quarter |
| 8 | `night.ini` | 1 |
| 9 | `sunset2.ini` | ramps 1 → 0 over the first quarter |
| 10 | `day1.ini`, forced clear | 0 |

"the first quarter" is `game+0x59C` against the `15.0f` at `0x90A8E4` — a
quarter of a calendar day.

The two outputs are:

| Field | What |
|---|---|
| `game+0xE70` | `int`, which `weathers/*.ini` is current — 0 day, 1 sunset, 2 night, 3 overcast |
| `game+0xE74` | `float`, the night factor. Building windows, street lights and **their electricity consumption** are switched on by it |

Changing `+0xE70` calls `0x333F80`, which loads the two lighting files and sets
up a cross-fade:

| Field | What |
|---|---|
| `game+0xEC0`, `+0xEC8` | the two `C3D_LIGHTING` being blended |
| `game+0xED0` | `float`, how far into the blend |
| `game+0xED4` | `float`, how long the blend takes — a fixed `15.0f` |

`0x8F50` advances `+0xED0` by the same scaled clock and finishes the blend when
it passes `+0xED4`.

## Exactly three places divide by thirteen

Scanning `.text` for `0x4EC4EC4F` — what MSVC emits for a signed `% 13` — finds
three sites and no more:

| Site | In | Reads |
|---|---|---|
| `0x333BA5` | the state machine at `0x333B30` | `game+0x590` |
| `0x5CFBB1` | the light factor at `0x5CFB70` | `game+0x590` |
| `0x9795` | the cross-fade at `0x8F50` | `0x9D54A0` — **not** the world object |

The third is a separate clock with its own day at `0x9D54A0` and its own
time-in-day at `0x9D54AC`, used where there is no calendar at all. It is left
alone.

**`0x5CFB70` is the same thirteen-phase logic written out a second time** — same
`% 13`, same cases 6…10, same `15.0f` quarter — returning a float: how lit the
world is. It is read by the renderer at `0x317D10` and by two of the electricity
functions at `0x1CB8E0` and `0x1CF520`. Patching only the state machine would
have moved the sky and left the street lights on the old thirteen-day schedule.
A second copy of the same logic is normal in this executable; see
[07-pitfalls.md](07-pitfalls.md).

## What the plugin does

Both functions are hooked, and each is handed a day and a time-in-day
re-derived from the position inside the current calendar day:

```
u  = ((day mod cycle_days) * 60 + time) / (cycle_days * 60)   0 .. 1
k  = floor(u * 13)                                            0 .. 12
t' = (u * 13 - k) * 60                                        0 .. 60
d' = the smallest day >= day with d' % 13 == k
```

`t'` is rescaled back onto the game's own `0..60`, so the `15.0f` the fade
compares against still means *the first quarter of this phase* and every
proportion in the vanilla machine survives.

### `d'` has to land in the same season as the real date

`0x333B30` does not only take `day % 13` off the field. It also hands it to the
snow-season test at `0x334340`, and compares it against `284`, `326` and `328`
for the overcast season. So the day we invent has to be believable as a date,
not merely as a residue.

A residue class modulo 13 has a representative every thirteen days. Version 1.x
took **the next one at or after the real date**, so as the phase advanced
through a single calendar day `d'` swept the whole window `day … day+12` — and
near a winter boundary that window straddles it, so the weather flipped in and
out of winter thirteen times a day. That is the "problems at the change of
seasons" the plugin was reported with.

The plugin now walks the representatives outward from the real date — `day+m`,
`day+m−13`, `day+m+13`, … ordered by distance — and takes the first one the
game's own `0x334340` puts in the **same season** as the real date. Every season
in that function is at least a hundred days long and the representatives are
thirteen apart, so a matching one always exists; the answer is within seven days
of the real date almost always and within thirteen at worst.

The test is asked on a **scratch object of the plugin's own**, the same trick
the light factor uses. `0x334340` is 120 bytes and references its argument
exactly twice, both `[arg+0x590]`; everything else it reads is an absolute
global. So the world object is never written to answer a question about it. If
the function's bytes do not match at startup the plugin says so and falls back
to the nearest representative — version 1.x's behaviour minus the systematic
forward bias.

**With `cycle_days = 13` the mapping is the identity**: `k` is `day % 13`, `t'`
is `time`, `d'` is `day`. The plugin short-circuits that case rather than
computing it, so 13 is bit for bit the base game and is the control to test
against when something looks wrong.

### The two hooks deliver the lie differently, deliberately

| Site | Bytes stolen | How |
|---|---|---|
| `0x333B30` | 15 | **Writes** to the world object — the weather index, the night factor, two flags — so it gets the real object with `+0x590` and `+0x59C` temporarily rewritten and put back afterwards |
| `0x5CFB70` | 16 | **Reads** only. Handed a scratch object of the plugin's own |

The scratch is safe because the function's 482 bytes were read instruction by
instruction: it touches `+0x5C4`, `+0x590`, `+0x59C`, `+0xE70` and `+0xE28` of
the object and nothing else, everything else it reads being an absolute global.
The scratch is `__declspec(thread)` and zero-initialised, so the render thread
and the simulation thread cannot be handed the same one, and the world object is
never written from the render side at all.

The write-and-restore on `0x333B30` is safe because it runs on the simulation
tick immediately after the day tick that owns those two fields — `0x30D100`
calls `0x3346E0` and then `0x333B30`, back to back — so nothing else is looking
at them in between.

The restore is unconditional, and that includes overriding the one write the
original makes to `+0x59C` itself, at `0x333C03`. It zeroes the field when the
weather changes under the editor flag at `+0x1090` — which was harmless while a
phase *was* a calendar day, and would now throw away most of a day of the
calendar every time the weather turned.

### The phase boundary has to look like a day boundary

The game gets that for free: a phase *is* a day, and the day tick leaves
`+0x59C` at exactly `0.0f`. Two things key on it — the random weather roll
(`if (time == 0.0f && weather == 0)`) and the "clear the overcast" edge at
`time` crossing `40.0f`. So the plugin forces `t' = 0.0f` on the first tick of
each new phase rather than letting it be whatever the float arithmetic produced.

The very first tick of a session is **not** a boundary and passes the real
figure through — otherwise `cycle_days = 13` would differ from vanilla by one
tick after every load, which is exactly the kind of "almost" the short circuit
exists to avoid. The state machine's own second argument is the signal: it is
non-zero only on the world-load path from `0x294318`, and the remembered phase
is forgotten there.

### And then the cycle runs thirteen times too fast

Compressing the cycle into one calendar day does not slow the calendar. The
cycle therefore comes out **thirteen times faster than the base game's**, and a
calendar day is only a few seconds of real time — measured at 2 to 6 seconds in
the first session this ran in.

That measurement does not need a probe. The day tick calls `0x2FB130`, the price
recompute, on days 5, 10, 15, 20, 25 of the month and at every month change, and
`resources` prints the whole table when `price_report = 1`. Four blocks
9.96 s, 30.1 s and 18.35 s apart is five in-game days each: 2 s, 6 s, 3.7 s per
day.

So the whole thirteen-step cycle — sunset, night, sunrise — goes by in a couple
of seconds, and it reads as *night tried to start and changed its mind*, because
that is exactly what it looks like.

`day_scale` is what makes a calendar day last longer. **What it stretches is the
simulation clock, not the calendar**, and the difference between those two is
the whole of version 2.0.

### Version 1.x stretched the calendar alone, and that broke the economy

The first implementation gave back most of what the day tick had just added to
`+0x59C`:

```c
raw = game[0x59C];                                    // what the day tick left
if (raw >= written) out = written + (raw - written) / scale;
else                out = raw;                        // it rolled over
game[0x59C] = written = out;
```

It works, in the narrow sense that the calendar really does advance thirteen
times slower. The problem is that **nothing else does**. Every rate in this game
is integrated from `C3D_TIMER::PowerTime` per frame, and the game is full of
things counted the other way — once per calendar day — so the two halves came
apart by a factor of thirteen. Three of them were found in the disassembly after
they were reported from a real session:

| Symptom | Where it comes from |
|---|---|
| Pollution runs away and never settles | `0x4D5E80` subtracts a flat `0.06` and `0.005` from every cell of the grid at `game+0x120A8`, **once per calendar day** — its only xref is `0x334A64`, inside the day tick. Emission is per frame. Thirteen times the emission between two subtractions |
| Winter kills the population | the same daily pass accumulates each residential building's exposure at `building+0x11B0` from a sample capped at `3.0`, clamped to `1.0`. With the grid thirteen times too high every home pins at full exposure and citizens sicken. Winter is where it peaks, because buildings with no district heating burn fuel locally and that is one of the emitters. The season boundaries are days of the year (`25`/`50`/`52`/`128` and `245`/`247`/`326` by climate, in `0x334340`), so each winter also lasts thirteen times longer on the wall clock |
| A loan never repays | the loan list is the `0x28`-byte vector at `game+0x10BD0`; `0x4B93F0` decrements the **term in days** at `record+0x10`, compounds the interest and pays `balance / days-left`. `0x4B95C0` runs it once per calendar day, only xref `0x334C48`. Thirteen times fewer payments out of an income that did not slow |
| Used-vehicle offers, notifications and random events dry up | same shape. Expiring notifications are `0x4CD2B0`, the random-event roll is `0x483880`, the fire roll is `0x2552A0` — all once per calendar day, all from the day tick |

There is no fixing these one at a time. There are dozens of per-day systems and
the list above is the ones that were noticed.

### So the simulation clock is scaled instead

**The whole simulation reads one `C3D_TIMER`, the one at `0x9D4EE0`** — 337
sites reference it, the shop tick at `0x171DA0` and the mine tick at `0x1B3690`
among them. `C3D_TIMER::PowerTime` is four instructions (C3DDLL64 rva
`0xFD670`):

```c
if (this[0xC] && !ignorePause) return 0;
if (this[0xD] && !realTime)    return 0;
return v * K / (realTime ? this[4] : this[0]);
```

`this[0]` is the frame rate game time is divided by; `this[4]` is the real one,
which is why a `realTime` caller must never be scaled — that argument is how the
engine asks for wall-clock time on purpose.

And **the game's own speed control is a multiplication of exactly that field.**
`0x105A90` calls `C3D_TIMER::Start` on the timer every frame and then multiplies
`this[0]` by `0.35`, `0.05` or `0.01` for the three speeds, or by `3`, `5` or
`1000` for the slow modes:

```c
C3D_TIMER::Start(&timer);
if (slowmode) { timer[0] *= 3.0f / 5.0f / 1000.0f; }
else          { timer[0] *= 0.35f / 0.05f / 0.01f; }
```

So scaling it is not a new mechanism — it is **one more speed step**. That is
also the safety argument for a factor of thirteen: the engine already ships a
mode that divides the step by a thousand.

It is done as an **import swap on the three `C3D_TIMER::Power*` the executable
imports** — `Power` (`0x86C030`), `PowerTime` (`0x86C028`), `PowerKmh`
(`0x86D5C0`) — rather than as a write to the field. Two reasons:

- **Stateless.** A per-frame write has to be applied exactly once between two
  `Start()` calls; a wrapper cannot compound if a frame runs its site twice and
  cannot be missed if a frame skips it.
- **All three or none.** `PowerKmh` is what vehicle speeds come out of. Scaling
  the time and not the speeds would be a *worse* desynchronisation than the one
  this is here to remove, so the plugin resolves all three slots before patching
  any and puts back whatever it patched if one refuses.

`PowerMs` is not imported. Calls on any timer other than `0x9D4EE0` — the
cross-fade's own at `0xA558A0`, the engine's internals — are passed through
untouched, which is what keeps `C3D_INPUT` and the render side on real time.

The site is verified as a unit before anything is swapped. `0x334A00`, inside
the day tick, is

```
48 8D 0D ........   lea  rcx,[rip+X]         -> must resolve to 0x9D4EE0
45 33 C9            xor  r9d,r9d             -> realTime    = false
45 33 C0            xor  r8d,r8d             -> ignorePause = false
FF 15 ........      call qword ptr [rip+Y]   -> must resolve to the PowerTime slot
```

Both displacements are resolved and compared rather than matched as bytes, so
between them those sixteen bytes prove that `0x9D4EE0` is a `C3D_TIMER` **and**
that it is the one the calendar itself is driven from. If a future build moves
either, the plugin logs and runs unstretched rather than scaling something else.

**Repointing the `60.0f` at `0x90AA90` was the obvious alternative and is wrong
twice over.** It sits in the shared literal pool with dozens of unrelated
readers, so it cannot be overwritten; and one of the readers that genuinely *is*
a day length, `0x97AF`, belongs to the other clock at `0x9D54A0` and must keep
the old figure.

**The day tick cannot be hooked** either. `0x3346E0`'s prologue reaches 14 bytes
only by including a RIP-relative `movss`, and the host's trampoline is a
straight `memcpy` with no fix-up. It no longer needs to be.

`auto` is `13 / cycle_days` — the value that leaves one whole cycle taking
exactly as long in real time as the base game's did — and it is `1.0` at
`cycle_days = 13`, so that case still changes nothing.

**The cost is real time and only real time.** Every rate slows together, so
in-game the economy is the base game's, exactly; on the wall clock the whole
thing runs `day_scale` times slower. Raising the game speed cancels it exactly,
and now that is true rather than merely claimed. **There is no arrangement that
keeps the date rate, the cycle rate and one cycle per date all at once** — that
is what a fixed ratio between two clocks means.

`slow = calendar` is version 1.x, kept because it is the only way to reproduce
the four rows of the table above and confirm they are gone. It is documented as
broken and should not be played with.

### The cross-fade

`game+0xED4` is the one number that does not scale itself: `0x333F80` writes a
fixed `15.0f` into it on every weather change. That was a quarter of a calendar
day, and a phase is no longer a calendar day, so the plugin rewrites it after
every tick to the same fraction of the new phase. `fade = 0` leaves it alone.

The day scale belongs in that figure, and this is the one place the two clocks
show through: `0x8F50` accumulates `+0xED0` from the timer at `0xA558A0`, **not**
the simulation timer at `0x9D4EE0`, so it is not one of the three the import
swap scales. A phase therefore lasts `day_scale` times longer in the units the
fade is measured in, so

```
fade = 15 * cycle_days * day_scale / 13
```

which with `day_scale = auto` comes back to exactly the vanilla `15`. Whether
those two timers really tick at the same rate at game speeds other than 1× has
not been established — if a fade looks wrong at 5×, that is the first thing to
check, and `fade = 0` is the fallback.

## Settings

`plugins/daynight.ini`:

| Key | Default | What |
|---|---|---|
| `enabled` | 1 | |
| `cycle_days` | 1 | calendar days per full day/night cycle. 13 is the base game exactly |
| `day_scale` | auto | how many times longer a calendar day lasts. `auto` is `13 / cycle_days`; `1` leaves the clock alone |
| `slow` | world | what `day_scale` stretches. `world` is the simulation clock — everything, in step. `calendar` is version 1.x, the calendar alone, and is **known broken**. `none` is `day_scale = 1` |
| `offset` | 0.0 | rotates the cycle, as a fraction of one cycle |
| `fade` | 1 | scale the lighting cross-fade to the new phase length |
| `probe` | 0 | a line per phase change, per lighting change and per date, plus a periodic figure |

`offset` exists because the cycle begins with daylight, so with `0` the dark
part lands a little past the middle of the calendar day. There is no clock in
the game's interface for that to disagree with — it only decides where in the
day the night falls. Anything other than `0` also takes `cycle_days = 13` off
its exact-vanilla short circuit.

## What this does not fix

`0x333F80` allocates two `C3D_LIGHTING` objects on every weather change —
`operator new(0x270)` twice, at `0x334194` and `0x3341E7` — and never frees the
previous pair. That is the base game's, and it is verified in the disassembly,
not guessed. Four changes per cycle used to mean four per thirteen days; it now
means four per day. Roughly 5 KB of leak per real minute at 1× speed, and the
textures behind them come from the managed cache and are not duplicated.

Freeing them here was considered and rejected: the memory comes from the game's
own `operator new` and the plugin has its own CRT, `C3D_LIGHTING` has a
destructor that would have to be run first, and it is not established that
nothing in `C3D_MIDDLEPOINT` holds a raw pointer to the outgoing pair for a
frame.

## Testing it

The hooks install and the cycle runs — a first session confirmed both `hook ok`
lines and a day/night that changed several times per date, which is what
produced `day_scale`, and then a second session produced the four rows of the
version-1.x table above. **Nothing in version 2.0 has been watched in game:
neither the scaled simulation clock nor the season-safe date.**

1. Tick `daynight` in the launcher, set `probe = 1` in `plugins/daynight.ini`,
   and load any save.
2. The log should carry **five** `hook ok` lines: `weather tick` and
   `light factor` — if the second is missing the plugin takes itself out on
   purpose, because the sky and the street lights would otherwise disagree —
   and then `C3D_TIMER::PowerTime`, `C3D_TIMER::Power` and
   `C3D_TIMER::PowerKmh`. Then
   `daynight  the whole simulation clock is scaled 13.00x, …`.
   Anything that says *the simulation clock could not be scaled* means the
   verification at `0x334A00` refused: the cycle is still synchronised but the
   day is three seconds long again.
3. **Read the `day N -> M` probe lines first.** They give how long each date
   lasted in real milliseconds and how many phase changes fitted inside it. It
   should say 13 phase changes, and the day should take about thirteen times
   what it took before. If it says 13 phases in 3000 ms, the clock scale is not
   reaching the day tick.
4. Watch the date and the sky together. One sunrise and one sunset per date is
   the whole point. The `lighting a -> b` lines say which `weathers/*.ini` the
   engine was actually told to load and how far apart the changes were.
5. Check the street lights and the lit windows come on with the dark and go off
   with it — that half comes from `0x5CFB70`, the second hook, and is the one
   that would fail silently on its own.
6. **Check that everything slowed together, which is the whole of version 2.0.**
   Watch a truck: it should crawl at the same rate the clock does, not drive at
   full speed through a slow day — that is `PowerKmh`. Then leave a dirty
   factory running for a few in-game days and check the pollution overlay
   settles instead of climbing. Then take a loan and check the days-remaining
   figure falls once per date. Then play through a winter and check the
   population does not collapse.
7. Watch a season boundary. The `phase` probe lines carry `day 40 (as 45)` —
   the real date and the one the state machine was told. The invented one should
   stay within about a week of the real one and, at a boundary, should **stop
   crossing it**: snow should not start and stop several times inside one date.
8. Set `cycle_days = 13` and `day_scale = 1` and confirm the game is exactly as
   it was. That is the control: the mapping is short-circuited to the identity
   there and no clock is touched at all — the `Power*` swaps are not even
   installed — so anything that still looks different is neither.
9. To reproduce what was fixed rather than to play: `slow = calendar` is version
   1.x exactly. The pollution overlay climbing without bound is the quickest of
   the four to see.
