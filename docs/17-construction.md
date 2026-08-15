# Construction offices — how far one reaches for work

**A plugin**, not part of the loader: `plugins/construction/construction.cpp` builds
to `build/plugins/construction.dll`. Deleting that DLL stops the plugin doing
anything further; it does not undo what it already did, because the range lives in
the save — see [What a save keeps](#what-a-save-keeps). See
[09-plugins.md](09-plugins.md) for the mechanism.

A construction office only picks up jobs near itself, so a site across town is never
started however idle the office is and however many vehicles it has parked. The point
of this plugin is `office_range` — make that reach yours.

**The range is found and the feature works.** It is a plain `int` on the construction
office itself, at `building+0xFC8` — the very number the office's own window prints,
`<1,000m` on a freshly built office. (3500 is where the `+` button *stops*, not where an
office starts; see [Which offices get set, and why 1000](#which-offices-get-set-and-why-1000).)
So there is no spliced code and no repointed constant in the range write at
all — `write_range` sets the field, which is as robust as anything in this project
gets, because there is no address for a game update to move. That claim is about
`write_range` alone; the window ceiling below is a code patch and says so. Confirmed
in a running game: a
site about 9 km from an office is picked up automatically and the yellow road overlay
extends to match, and the simulation does not clamp the value back.

Two smaller pieces sit either side of it:

| | |
|---|---|
| **the window ceiling** | The `+` button stopped at 3500 because of `imm32` clamps and a fixed rung ladder baked into the button handlers — not a constant read from anywhere. `raise_ceiling` lifts the top rung, so the buttons step up the same ladder and stop somewhere higher; they were never freely adjustable and are not now. Seven sites, all verified before any is written, and `ceiling = 3500` verifies all seven and writes none of them. |
| **the probe** | Still here, still useful, now purely diagnostic. It is what found `+0xFC8`, and it is what would find the field again after a game update moved it. |

`patch` and the two site tables under [Where the range lives](#where-the-range-lives)
are the **code-patch route**, which the field write made unnecessary. They are still
empty and deliberately so — a table of zeroes logs "no address yet" rather than
refusing — and they stay because a future game version could make the field read-only
and force the question back to a code patch.

> **`probe` and `write_range` are independent, but they share one hook.** Both are
> driven by the same import swap on `C3D_TERRAIN::Render`, and that hook is installed
> if *either* is on. It used to hang off `probe` alone, which made `probe = 0` with
> `write_range = 1` a silent no-op — and that was the configuration this page and the
> ini both recommended once the range was known.

A **count cap** — an office that stops at N jobs however close they are — was the other
thing the limit could have turned out to be. Both produce the same symptom, an idle
office beside unstarted work, so the probe measures both and keeps them in separate
tables: finding one must not block patching the other.

## What this is not

**Walking distance and personal-car distance.** Those are one number each and they
already have a plugin: [12-walking.md](12-walking.md), `distance` and `car_distance`
in `plugins/walking.ini`, published on the Steam Workshop as *Increase Walking
Distance*. Nothing here touches `query+0x3C` for feet or for cars.

That is the first thing a reader assumes this does, so it is said first — and the
confusion is easy, because all three are "how far something reaches". The difference
is **who is travelling**. `walking`'s two numbers are about a *citizen* getting to
work, a shop or a service on foot or in their own car. This is about a construction
*office* dispatching its own fleet to a job. Different traveller, different list,
different constant.

`car_distance = 10000` will not extend an office's reach, and if it appears to, the
probe will say so outright: it checks whether every job an office took is one already
in that office's personal-car connection list.

## What automatic assignment is, in the engine

A construction office services a set of construction sites, and offers to pick them
up without being told to. The game's own script API names both halves, in
`media_soviet/scripts/SOVIETInstructions.txt`:

| Declaration | What |
|---|---|
| `Building.nConstructionBuildingNum` | how many sites this office is working on |
| `Building.nConstructionOfficeNum` | how many offices are on this site |
| `Building.bConstructionActive` | whether the site is being worked |
| `Building_ConstructionBuilding_GetID` 11015 | walk an office's sites |
| `Building_ConstructionOffice_GetID` 11016 | walk a site's offices |
| `BUILDINGTYPE_CONSTRUCTION_OFFICE` | **12** |
| `BUILDINGTYPE_CONSTRUCTION_OFFICE_RAIL` | **28** |

So an office holds a list of sites and a site holds a list of offices — the same
reciprocal shape as `nWalkingBuildingNum` and `building+0xCA8`, and each list
validates the other.

**Those declarations give names, not offsets.** The script API's declaration order is
not the struct's layout order — `nStorageNum` is declared thirty lines after
`nWalkingBuildingNum` while `+0x970` sits far before `+0xCA8` — so inferring an offset
from position in that file would be a guess written down as a finding, which is the
one mistake [07-pitfalls.md](07-pitfalls.md) singles out as having cost a feature.

The building offsets this plugin does rely on are all established elsewhere:

| Offset | Contents | From |
|---|---|---|
| `game+0x11B08`…`+0x11B10` | `vector<Building*>`, every building | `accumulator`, `easystart` |
| `building+0x318` | the type descriptor; `+0x360` is the `BUILDINGTYPE_*` | [02-findings.md](02-findings.md) |
| `building+0x604` | `>= 1.0f` means finished | the dispatcher's own test |
| `building+0xEA8` | non-zero means going away | the dispatcher's own test |
| `building+0xCC0` | the personal-car connection vector, stride `0xF0` | [12-walking.md](12-walking.md) |

**The game object is the static at rva `0x9D4F10`**, verified against the `lea` at
`0x43970A` before anything is read — `cities`' check, copied. It is not the pointer at
`0x9941F0`, which `depletion` and `deposits` use: that is a different object, the one
carrying the terrain at `+0xED8` and the resource maps at `+0xF00`/`+0xF08`. Both get
called "the game object" in places and they are not the same thing.

## Where the cap is not

**It is not in `building.ini`.** All six stock construction offices plus
`rail_construction_office` declare `$TYPE_CONSTRUCTION_OFFICE`, storages, workers and
`$WORKING_VEHICLES_NEEDED`, and not one of them carries a radius or a site count.

That is a real finding and it narrows the search to two shapes, which want different
patches:

| Shape | Patch |
|---|---|
| a literal in `.text`/`.rdata` | repoint the read — [12-walking.md](12-walking.md) |
| a field of the game object | repoint the *comparisons* — `easystart` on `game+0x5DC` |

The probe tells them apart for free, by reporting **where** the number lives: on the
office object or its type descriptor for the first, at a fixed offset in the game
object for the second.

## Where the range lives

**On the office, at `building+0xFC8`, as a plain `int` in metres.** Three readings say
so, which is worth more than any one of them on its own:

| | Evidence |
|---|---|
| the probe | 3500 occurred at exactly one offset in the office object, the same offset in all sixteen offices on the test map, and nowhere in the type descriptor. Clicking the window's `−` and `+` moved that slot and nothing else |
| the disassembly | the office window's own clamp names the displacement outright while pinning the field to 3500 — `mov [rsi+0xFC8],3500` |
| **the fresh map** (2026-08-15) | with `write_range = 0` and `raise_ceiling = 0`, a newly built office on a brand-new map read **`<1,000m`** in its own window before any button was touched — and the probe, hunting 3500 in that office, found it nowhere |

Read them for what each actually establishes. The first two agree on the **offset**, and
that is the finding: `+0xFC8` is the range. Neither of them establishes the **birth
value**, though both were once read as if they did — the sixteen offices were a save
whose offices had all been ridden to the `+` button's top by hand, and `mov
[rsi+0xFC8],3500` is that button's clamp. The fresh map is the only reading that ever
looked at an untouched office, and it says 1000.

That is what `write_range` sets, and it is why the feature has no address to lose:
a field on an object the game hands us is not a location in `.text` that a rebuild
can move.

The **code-patch route** is a different thing and it is still unbuilt:

| Group | Role | Site | Value |
|---|---|---|---|
| `range` | office pickup range | — | *no address* |
| `cap` | office assignment count | — | *no address* |

`patch = 1` logs `no address for ... yet` and writes nothing. Both tables are kept
deliberately: a future game version could make the field read-only and force the
question back to a code patch. Two groups rather than one because they are
independent findings — a group is verified whole before a single byte of it is
written, so if the simulation's copy of the range verifies and the office panel's
does not, neither is touched.

## How the probe finds the list

A `{begin, end, cap}` triple on the office whose members are all live construction
sites. The shape alone proves nothing — a structure this long satisfies "three
plausible pointers" by accident, which is exactly the false positive
[07-pitfalls.md](07-pitfalls.md) records under *A heuristic find without an invariant
is a write into a stranger's struct*. A candidate is accepted only on all of:

| | Test | Why a lookalike fails it |
|---|---|---|
| I1 | `cap >= end >= begin`, span divides by the stride, span readable, ≤ 256 members | weak alone, and known to be |
| I2 | every member is in the game's own building vector | an accidental triple points anywhere else |
| I3 | **every member is unfinished** (`+0x604 < 1.0`, `+0xEA8 == 0`) | **the decisive one.** Walking, parking, catchment and occupant lists all carry finished buildings; no other list on a building is all-unfinished |
| I4 | not one of `+0x970` `+0xA10` `+0xBD8` `+0xCA8` `+0xCC0` `+0x10C8` | known vectors, excluded by name rather than re-derived |
| I5 | the candidate held across **two consecutive samples** | the probe runs on the render thread; a vector caught mid-`push_back` otherwise looks like a field that moved and moved back |

**The acceptance test is the office's own window.** A candidate passing all five and
disagreeing with what the panel lists is wrong, and everything downstream of it is
worthless. The log says so in as many words when it accepts one.

## Bracketing the range

This is the measurement the plugin exists for. Per office, per report:

| | |
|---|---|
| `claimedMax` | the farthest job this office took on |
| `unclaimedMin` | the nearest job it **refused** |
| `unclaimed` | live sites in no office's list at all |

`unclaimed` has to be non-zero for any of it to mean anything — an office that stopped
taking work because there was none left has demonstrated no limit of any kind, and the
log says exactly that instead of a verdict.

Then:

- **`unclaimedMin > claimedMax`** → the range is bracketed between them. This is the
  answer, and the probe immediately hunts for a number in that bracket.
- **`unclaimedMin < claimedMax`** → a job *nearer* than one already taken was refused,
  so distance is not the gate. It is a count, and `office_limit` is the setting that
  matters rather than `office_range`.

`unclaimedMin` is computed **per office**, not globally: two offices in different towns
have completely different neighbourhoods, and averaging them erases the boundary.

### Hunting the number

Given a bracket, the range is searched for as a float and as an int in four places, and
every hit is logged with its offset:

| Where | Why |
|---|---|
| the office object | a per-office field |
| its type descriptor | a per-type figure, like `$WORKING_VEHICLES_NEEDED` |
| the game object | a global setting, the way `game+0x5DC` is |
| **`.rdata`, `0x909000`–`0x90C000`** | **the likeliest by far** |

The last one is the pool this game keeps its layout constants and rates in — where
`walking`'s 480 and 530, `cities`' 1000 and every deposit search radius live. No
`building.ini` declares a range and every office behaves the same, so it is a global,
and a global float in this executable lives there.

The bracket is widened before searching — down 10% and up to double — because the game
measures a **path along roads** while the probe measures a **straight line**. The real
constant is at or above the straight-line bracket, never inside it.

Hits are candidates, nothing more. 2500 appears in `.rdata` for several unrelated
reasons, which is exactly why the patch repoints one instruction rather than
overwriting the constant.

Two further things the probe reports because each can end the investigation early:

**Is the list growable?** It logs `cap > end` as *growable* or *FIXED ARRAY*. If it is
a fixed inline array, raising the cap is a heap overrun into the next field, not a
feature, and the honest deliverable becomes a bigger fixed cap.

**Is it just the car range?** Every claimed site is checked against the office's own
personal-car connection vector at `+0xCC0`. If claimed ⊆ car-list exactly, the
office's reach *is* the vehicle path ceiling that `plugins/walking` already patches,
and this plugin has nothing left to do.

## The long trip, and whether it exists

Claimed: that a citizen or vehicle abandons a journey that runs too long.

Established: **nothing.** [02-findings.md](02-findings.md) records the opposite —
*"Nothing downstream re-checks the distance. The job, shop and service code only asks
whether the pair is in the list"* — so there may be no such mechanic at all. The one
lead is `query+0x3C`, the generic path ceiling, and
[12-walking.md](12-walking.md)'s own *Where to go next*: *"the same field is filled in
by every other search the game runs. A probe that logged the value at each caller
would map them."*

**No `trip_limit` setting ships.** A key that provably does nothing invites a bug
report. If the probe establishes the mechanic exists, the key arrives with an address
behind it; if it establishes it does not, the negative result is the deliverable. And
if it turns out to be a path-search ceiling, it belongs in `walking`, not here.

A negative result is only worth having with four things in the log: a **positive
control** (demolish a destination mid-journey and watch the detector fire), a
**denominator** (N journeys, N arrivals, N abandonments), a **bound** (the largest
value any growing field reached without an event firing), and honest **scope** — the
verdict is *"not on the `Person`"*, never *"not in the game"*.

## The settings

`plugins/construction.ini`, section `[construction]`.

| Key | Default | What |
|---|---|---|
| `enabled` | 1 | 0 unloads the plugin without reading anything |
| **`write_range`** | **1** | **set the office's range field. This is the feature** |
| **`office_range`** | **10000** | **metres of reach. The point of the plugin** |
| `game_default` | **1000** | what an untouched office starts at — the number a freshly built office's own window prints, not the 3500 its `+` button stops at. Only an office still carrying this — or `written_range` — is set, which is what makes `office_range` a default rather than a cage. 0 holds *every* office on every frame and the `−` button then cannot lower one |
| `written_range` | *absent* | the plugin's own bookkeeping, not a knob. It writes back the value it last set, so an office still holding that is recognised next session as the plugin's work and follows `office_range` when you change it. Anything outside 100…20000 is treated as "no earlier value", which is also what a first run looks like |
| `raise_ceiling` | 1 | lift the 3500 the window's `+` button stops at |
| `ceiling` | 10000 | what it stops at instead. Clamped to 3500…20000 — the same upper bound as `office_range`, because a value above it is one `write_range` would then refuse as implausible |
| `probe` | 1 | bracket the range and hunt for it. Diagnostic now, not a step you have to run |
| `patch` | 0 | the code-patch route. Needs an address in a site table, and both are empty |
| `office_limit` | 0 | jobs one office will take on, if the gate is a count. 0 is no cap |
| `probe_expect` | 1000 | the value to hunt for inside an office |
| `probe_diff` | 1 | report every 4-byte slot of an office that changed since the last report |
| `probe_from`, `probe_to` | 768, 6144 | which part of an office to compare |
| `probe_period` | 5 | seconds between reports |
| `probe_sites` | 8 | list members printed per office |
| `rdata_from`, `rdata_to` | `0x909000`, `0x90C000` | the constant pool searched for the range. Decimal in the file — the profile API does not take `0x` |

`office_range` is a **path length along roads**, not a straight line, and is clamped at
20000 for the reason `walking` clamps at the same figure: these searches are
breadth-first over the road graph, so the work grows with the square of the limit.

`write_range` and `probe` share one hook and either one installs it, so turning the
probe off does not turn the feature off. At most `MAX_OFFICES` (**64**) construction
offices are tracked; a map with more says so in the log rather than ignoring the rest
in silence. It was 16, which is a probe-era number — the author's own map carries 17
offices, and the seventeenth silently never got its range set.

### Which offices get set, and why 1000

Two kinds, and only two:

| | |
|---|---|
| still at `game_default` | the player has never touched this office |
| still at `written_range` | this plugin set it, in this session or an earlier one |

Anything else is a number the player chose in the office window, and it is left
alone. That is the whole of what makes `office_range` a default rather than a cage,
and it is designed behaviour rather than a gap: a plugin that overwrote a
hand-adjusted office every frame would be taking the `−` button away.

The second kind is why `written_range` is written back into the ini. Without it,
every office the plugin has ever touched reads as hand-adjusted the moment you change
`office_range`, and the setting would only ever govern offices that do not exist yet.
With it, lowering `office_range` walks the plugin's own offices back down — nothing
in the write path is raise-only.

**`game_default` ships as 1000**, settled on 2026-08-15 by the only reading that could
settle it: both of this plugin's writes switched off, a brand-new map, one freshly built
office, and its own window read `<1,000m` before anything had touched it. The probe,
running in that same office, hunted 3500 and found it nowhere.

The number reached that value the long way round, and the wrong turns are worth keeping
because each is a way of mistaking a strong reading for the reading you wanted:

- **3500 sat here and was wrong.** It had two pieces of evidence behind it, which is why
  it was convincing. The probe read 3500 out of `+0xFC8` on all sixteen offices of the
  test map — but that was a *save*, and its offices had every one of them been ridden to
  the `+` button's top by hand, so the sixteen agreed about the player's habits and not
  about the game. And the executable's own `mov [rsi+0xFC8],3500` is the **button
  clamp**: where `+` stops, not where an office is born. Two readings agreeing is worth
  more than either — but only about the thing they both actually measured, which was the
  offset.
- **1000's own first stay here was a guess**, off the `+`/`−` button ladder
  (`100 → r9d → 2000 → 3000 → ceiling`, where `r9d` is the one rung the handler computes
  at run time rather than carrying as an immediate, so the disassembly does not name it).
  It is not that rung. It happened to be the right number for the wrong reason, which is
  not the same as having been right.

The check takes a minute and is worth re-running on your own build, or on any build where
the game has changed: set `write_range = 0` and `raise_ceiling = 0`, start a new map,
build one construction office and read the number in its own window before touching
anything. That is `game_default`. If it says something other than 1000, exactly two
literals move — the default in `construction.cpp` and the `game_default` line in
`construction.ini`.

## What a save keeps

**The range is in the save.** `+0xFC8` is a field on the office, the office is in the
save, and the value survives a save and a reload the same way the number you set with
the `−` and `+` buttons does — because it *is* that number.

So a raise is permanent, and this page will not pretend otherwise:

- an office set to 10 km stays at 10 km on the next load, with the plugin;
- it stays at 10 km with the plugin removed and the DLL deleted;
- there is no restore-on-uninstall, because there is nothing to restore *from* — the
  plugin never keeps a copy of what the field was.

The two ways back down are the office's own `−` button, which the plugin respects,
and lowering `office_range` and letting the plugin follow its own writes — an office
still holding `written_range` is set to whatever `office_range` now says, downwards
included.

Which sites an office has taken on is likewise in the save and not derived from the
map, so `office_limit`, if it is ever wired to an address, would have the same shape:
assignments already made are kept. **The save format is unchanged** either way, so a
save stays loadable with or without this plugin — unlike a resource being added.

## What the log says

Startup, on the shipped defaults — the ceiling patch happens here, before a map is
even loaded, and the `ready` line reports what will actually happen rather than what
was asked for:

```
plugin   construction     1.0      from construction.dll
construct  game object 0x9D4F10 confirmed by the lea at 0x43970A
construct  ceiling: 7 of 7 site(s) raised from 3500 to 10000 - the office window's
           + button now goes that far
construct  ready: a construction office starting at the game's 1000 m will be set to
           10000 m (its own window stops at 10000, and lowering one by hand sticks)
construct  612 building(s), 0 with no readable type
construct  types on this map: 2 x381  3 x44  6 x22  12 x3  28 x1  43 x2
```

`12 x3` is the load-bearing line: it proves `building+0x360` really is the type on this
build and that offices were found. `12 x0` means the offset is wrong and every later
line is noise — [02-findings.md](02-findings.md) records that a previous claim about
that very offset was a guess and was wrong.

With `ceiling = 3500`, the seven sites are still verified and none is written — the
executable is left exactly as it shipped, and the line says so rather than reporting
a raise "from 3500 to 3500":

```
construct  ceiling: 3500 is what the game already stops at - all 7 site(s) verified
           and none rewritten, the executable is untouched
```

Then the writes themselves. One line per office, on the first pass that touches it,
and one line for the pass:

```
construct  range: office 0000029F4C8A1200 was at 1000 m, set to 10000 m
construct  range: office 0000029F4C8B7A80 was at 1000 m, set to 10000 m
construct  range: 2 office(s) at the game's default of 1000 raised to 10000 m -
           lower any of them in its own window and it stays lowered
construct  range: written_range = 10000 noted in construction.ini - an office holding
           that is this plugin's own work, and follows office_range the next time you
           change it
```

`was at` is the load-bearing half of the per-office line. Without it the log said what
a range became and never what it had been, which is how a disagreement about the
game's own default became an argument settled by git archaeology.

The second session, after you have edited `office_range` down to 8000, is where
`written_range` earns its keep — the offices this plugin set are still its own, and
follow:

```
construct  ready: an office starting at the game's 1000 m, or still holding the 10000
           this plugin wrote last, will be set to 8000 m (its own window stops at
           10000, and lowering one by hand sticks)
construct  range: office 0000029F4C8A1200 was at 10000 m, set to 8000 m
construct  range: 2 office(s) set to 8000 m - the ones still at the game's 1000, and
           the ones still holding the 10000 this plugin wrote last. Lower any of them
           in its own window and it stays lowered.
```

An office you moved by hand appears in neither list, and no line is printed about it
at all — that is the design, not an omission.

A report from the probe, which is diagnostic now and answers questions the field
write made unnecessary:

```
construct  ---- day 143 of year 1962: 1 office(s), 41 live site(s) ----
construct  office 000002...  type 12  placed  readable to +0x1800
construct    candidate +0x0CE8  stride 0x8  ptr+0  8 member(s)  growable
construct    +0x0CE8 held once - waiting for a second sample
construct    ACCEPTED +0x0CE8 after two consecutive samples - open this office's
             window and check it lists 8 site(s)
construct      site  0  000002...  type   2   118.4 m  car list yes  progress 0.12
construct    holds 8 site(s)  from 44.2 m to 412.7 m  growable  not all in the car list
construct  41 live site(s), 33 of them unclaimed
construct  office 000002...: claimed out to 412.7 m, nearest refused 604.9 m
construct  office 000002...: RANGE IS BETWEEN 412.7 m AND 604.9 m - hunting for it
construct      .rdata  +0x90AD50 = 500.000
construct      .rdata  +0x90B0C4 = 600.000
construct  office 000002...: 2 candidate(s) in 371..1210. The .rdata ones are the ones
           to try first - repoint the read, never overwrite the constant.
```

The two lines that matter are the bracket and the `.rdata` hits under it. Those RVAs are
what phase two goes and verifies.

The other outcome, which is just as useful:

```
construct  office 000002...: a refused site at 265.1 m sits INSIDE a taken one at
           412.7 m - distance is not the gate here, a count is
construct  office 000002...: held 8 over 3 report(s) with 33 site(s) unclaimed - 8 may
           also be a count cap
```

A refusal, in the project's usual voice — the address, what was found, what was
expected, then `refusing`. The first line is the whole plugin standing down; the
second is the ceiling patch standing down on its own, which is what a game update
looks like from here, and it happens before any of the seven is written:

```
construct  game object: rva 0x43970A points at 0x9D5010, expected 0x9D4F10 - refusing
construct  ceiling: plus compare at rva 0x76C7C5 does not match this build - refusing
construct           found: 41 8B 0C 24 81 F9 B8 0B 00 00
construct  range: 1 office(s) refused - +0xFC8 did not hold a plausible range
           (100..20000), so it is not being written
construct  cap: no address for "office assign cap" yet - run with probe = 1
```

And the one that means a building went away between one frame and the next, which the
plugin treats as a reason to stop touching that pointer rather than a reason to
panic:

```
construct  range: 1 cached office pointer(s) no longer validate - demolished since
           the last probe, most likely. They are left alone until the building
           vector hands them back.
```

## Testing it

The answer comes from the game, not the log alone. The range is the goal, so the road
comes first:

1. **Build the road out to about 5 km before placing anything.** This is not optional
   and it is the step people skip: a site with no road is refused for a completely
   different reason, and "refused for no road" and "refused for distance" are
   indistinguishable in the log.
2. **One `construction_office` on that road**, its vehicles bought.
3. **Place sites along the road at roughly 200, 600, 1200, 2500 and 5000 m**, one at a
   time, letting a report land between each. Watch `claimed out to` climb and
   `nearest refused` appear. When the refused one is further than everything claimed,
   the bracket is the range and the `.rdata` hunt fires by itself.
4. **Then test the count**, separately: place several sites all within 200 m and watch
   whether the office stops taking them even though they are close. If it does, there
   is a cap as well as a range.
5. **Repeat on the 6-, 12- and 24-vehicle offices.** The six stock offices declare six
   different `$WORKING_VEHICLES_NEEDED` (6, 8, 12, 14, 16, 24; rail 4). If either
   number tracks that, it is per-type data and may need no code patch at all; if it is
   identical on all of them, it is a literal.
6. **Open each office's window** and count its entries against `holds N`. A mismatch
   invalidates everything above.

## State

**The feature is confirmed in game.** `building+0xFC8` was found by the probe — the
value 3500 occurred at exactly one offset in the office object, the same offset in all
sixteen offices on the test map, and nowhere in the type descriptor — and then
confirmed independently by disassembly, where the office window's own clamp names that
displacement outright while pinning it to 3500. Two unrelated methods agreeing is worth
more than either. An office set through `write_range` really does reach that far: a site
about 9 km out is picked up automatically and the road overlay extends to match.

What those two agreed about was the *offset*. The fresh-map check on 2026-08-15 later
showed that the sixteen offices had all been ridden to the `+` button's top by hand and
that 3500 is that button's clamp: an office is born at **1000**, which is what
`game_default` now ships as.

The seven ceiling sites are verified byte for byte against this build before any is
written, and the `+` button reaches `ceiling` afterwards.

**What is still illustrative** is everything about the *list*: the histogram, the
candidate scan for the office's site vector, and the cap-versus-radius verdicts under
[Bracketing the range](#bracketing-the-range). Those answer a question the field write
made unnecessary, and the numbers in those log samples are made up.

## Where to go next

- **The cap's enforcement site.** The probe produces the number, never the compare.
  That is a Ghidra step: `DisasmMany.py` over the type-12 branch of the dispatcher at
  `0x139A70`, plus a `.text` scan for the value **in every form** — `imm8`, `imm32`
  and `(float)`, because [07-pitfalls.md](07-pitfalls.md) records what a single-form
  scan costs.
- **Whether it is written out twice.** A second implementation of the same thing is
  common in this executable; walking had four. The probe sees one state, not four
  constants, and the office panel's own copy is invisible to it.
- **`<` vs `<=` vs an array bound**, which decides whether "unlimited" is a feature.
- **The rail office**, type 28, and whether it shares 12's number.
