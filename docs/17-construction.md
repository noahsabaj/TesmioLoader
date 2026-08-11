# Construction offices — automatic assignment without the cap

**A plugin**, not part of the loader: `plugins/construction/construction.cpp` builds
to `build/plugins/construction.dll`, and deleting that DLL restores the base game
exactly. See [09-plugins.md](09-plugins.md) for the mechanism.

**This is the measuring half.** The site table is empty, `patch` is 0 by default, and
in this state the plugin reads construction offices and changes nothing at all about
the game. Everything under [Where the cap lives](#where-the-cap-lives) is what the
probe is for, not what it has found.

## What this is not

**Walking distance and personal-car distance.** Those are one number each and they
already have a plugin: [12-walking.md](12-walking.md), `distance` and `car_distance`
in `plugins/walking.ini`, published on the Steam Workshop as *Increase Walking
Distance*. Nothing here touches `query+0x3C` for feet or for cars.

That is the first thing a reader assumes this does, so it is said first. The two
features look alike from outside — both are "a limit on how far something reaches" —
and they share no code, no address and no mechanism.

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

## Where the cap lives

| Role | Function | Site | Value |
|---|---|---|---|
| — | — | — | *not found yet* |

Filled in by the probe. Until then `patch = 1` logs `no address for ... yet` and
writes nothing.

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

## Cap or radius

Three numbers per report, and they only mean anything together:

| | |
|---|---|
| `held` | how many sites this office has taken on |
| `live` | unfinished buildings on the whole map |
| `unclaimed` | live sites in no office's list at all |

- `held` frozen over three reports **while `unclaimed` climbs** → `held` is the cap.
  Without the second half this is just an office that has taken on everything there is.
- an unclaimed site **nearer** than one already claimed → the gate is a count, not a
  radius.
- a clean gap between farthest-claimed and nearest-unclaimed → a radius lies in it.

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
| `probe` | 1 | follow the offices and report what moves |
| `patch` | 0 | write the patches. Needs an address in the site table |
| `office_limit` | 0 | sites one office will take on. 0 is no cap |
| `probe_from`, `probe_to` | 768, 6144 | which part of an office to compare |
| `probe_period` | 5 | seconds between reports |
| `probe_sites` | 8 | list members printed per office |

## What a save keeps

Which sites an office has taken on is in the save, not derived from the map. Removing
the plugin restores the cap for every assignment made from then on, while offices that
already took more keep them until those sites finish. **The save format is unchanged**,
so a save stays loadable with or without this plugin — unlike a resource being added.

## What the log says

Armed, before anything has been found:

```
plugin   construction     0.1 (probe) from construction.dll
construct  game object 0x9D4F10 confirmed by the lea at 0x43970A
construct  ready: probe armed - it reads the offices and changes nothing
construct  612 building(s), 0 with no readable type
construct  types on this map: 2 x381  3 x44  6 x22  12 x3  28 x1  43 x2
```

`12 x3` is the load-bearing line: it proves `building+0x360` really is the type on this
build and that offices were found. `12 x0` means the offset is wrong and every later
line is noise — [02-findings.md](02-findings.md) records that a previous claim about
that very offset was a guess and was wrong.

A report:

```
construct  ---- day 143 of year 1962: 1 office(s), 41 live site(s) ----
construct  office 000002...  type 12  placed  readable to +0x1800
construct    candidate +0x0CE8  stride 0x8  ptr+0  8 member(s)  growable
construct    +0x0CE8 held once - waiting for a second sample
construct    ACCEPTED +0x0CE8 after two consecutive samples - open this office's
             window and check it lists 8 site(s)
construct      site  0  000002...  type   2   118.4 m  car list yes  progress 0.12
construct    holds 8 site(s)  farthest 412.7 m  growable  every one is in the car list
construct  41 live site(s), 33 unclaimed, nearest unclaimed 265.1 m from office 0
construct  office 000002...: held 8 over 3 report(s) with 33 site(s) unclaimed - 8 is
           the cap. Search for it as an int and a float.
construct  office 000002...: an unclaimed site at 265.1 m sits inside a claimed one at
           412.7 m - the gate is a count, not a radius
```

A refusal, in the project's usual voice — the address, what was found, what was
expected, then `refusing`:

```
construct  game object: rva 0x43970A points at 0x9D5010, expected 0x9D4F10 - refusing
construct  office assign cap: 0x... holds 20, expected 12 - refusing
construct  office: 2 site(s) did not verify - nothing written
```

## Testing it

The answer comes from the game, not the log alone. In order:

1. **One 8-vehicle `construction_office`**, its vehicles bought, on a road. Place
   construction sites one at a time within 200 m, pausing for a report. Watch `held`
   climb, then freeze while `unclaimed` climbs. **The cap is where it freezes.**
2. **Cancel one assigned site.** `held` must drop and an unclaimed site must be picked
   up next report. If it does not, the gate is on something other than the list.
3. **Build the road out to 5 km first**, then place sites at roughly 600, 1200, 2500
   and 5000 m. Road first is not optional: "refused for no road" and "refused for
   distance" look identical in the log otherwise.
4. **Repeat on the 6-, 12- and 24-vehicle offices.** The six stock offices declare six
   different `$WORKING_VEHICLES_NEEDED` (6, 8, 12, 14, 16, 24; rail 4). If the cap
   tracks that number it is per-type data and may need no code patch at all. If it is
   identical on all of them, it is a literal in `.text`.
5. **Open each office's window** and count its entries against `holds N`. A mismatch
   invalidates everything above.

## State

**Nothing has been confirmed in game.** The plugin compiles, loads, and its
probe is armed; the histogram, the candidate scan and the cap/radius verdicts have not
been watched against a real construction office. Every number in the log samples above
is illustrative.

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
