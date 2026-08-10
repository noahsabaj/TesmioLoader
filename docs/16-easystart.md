# easystart — needs that arrive with the century

A map that starts pre-populated hands the player a town whose citizens want
electricity, running water, central heating and a cinema on the first day. They
lived without all of it the day before. `easystart` holds each need back until a
year in `plugins/easystart.ini`.

It holds back the **consequences only**. A cinema built in 1925 is still walked
to, a shop still sells clothes, a pub still serves; those things simply do not
feed happiness yet, and their absence costs none. That distinction is the whole
design, and it is what decided every mechanism below.

## What a need actually is

There is no list of needs in the game. There is something better: the game keeps
an itemised ledger of *why* happiness and health moved, and the statistics window
draws it. Finding that ledger is what turned "which needs are there" from a
guess into an enumeration.

Both drawers walk a float array immediately followed by an int array of the same
length, with a base language id incremented once per entry — so one `mov
r12d, 0xD30F` (54031) at `0x8DB95` and a `lea r14,[rdi+0x10B74]` give the whole
table:

| Ledger | Drawer | Reasons | Floats | Counts |
|---|---|---|---|---|
| happiness | `0x8D490` | 23, ids 54031…54053 | `0x9E5970` | `0x9E59CC` |
| health | `0x8C3C0` | 12, ids 54003…54014 | `0x9E58B0` | `0x9E58E0` |

Scanning `.text` for `inc dword ptr [rip+X]` landing in either count array maps
every reason to the code that raises it. That scan is the map of the feature:

| # | Happiness reason | Raised by | Gated as |
|---|---|---|---|
| 0 | Starving | `0x83A4F0` | `food` |
| 1 | Lack of meat | `0x83A4F0` | `meat` |
| 2 | Unable to get clothes | `0x83A4F0`, `0x836960` | `clothes` |
| 3 | Unable to get electronics | `0x83A4F0` | `electronics` |
| 4 | Job | **nobody** — dead in this build | — |
| 5 | No electricity | `0x1BC1A0` | `electricity` |
| 6 | No water | `0x1BC1A0` | `water` |
| 7 | Crime | `0x256010`, `0x257170` | — |
| 8 | Interior temperature | `0x488AE0` | `heating` |
| 9 | TV or radio broadcast | `0x4C2150` | — (a bonus only) |
| 10…14 | prison, orphanage, a child's death, relocation, expulsion | events | — |
| 15 | Low government loyalty | `0x83A4F0` | — |
| 16 | Unable to visit a hospital | `0x83A4F0` | `health` |
| 17 | Unable to drink alcohol | `0x83A4F0` | `alcohol` |
| 18 | Unable to practice sports | `0x83A4F0` | `sport` |
| 19 | Unable to pray in church | `0x83A4F0` | `religion` |
| 20 | Unable to enjoy culture | `0x83A4F0` | `culture` |
| 21, 22 | prison / orphanage upbringing | events | — |

and on the health side: Starving, Pollution, Stroke, Unable to visit doctor,
Drinking alcohol, Burns from fire, **Insufficient or polluted drinking water**
(`0x1B08E0`), Crime, **Low interior temperature** (`0x488AE0`), Ambulance delay,
Global pandemic, Lack of meat.

Two things fall out of the table and both matter:

- **There is no "no sewage" reason and no "no school" reason.** Sewage acts
  indirectly — a full sewage store stops the building drawing water and the
  player sees *No water* — so it rides on the `water` gate and needs nothing of
  its own. School and kindergarten are not needs at all; see below.
- **`Job` has no writer.** The reason is drawn and can never be non-zero.

## Nine needs, one function, no code patch

```
rva 0x83A4F0   FUN(game, person)
```

One caller, `0x8338E0`, inside the per-person tick `0x832CB0` and immediately
after that tick has run the daily planner. It walks the demand array the planner
has just rebuilt, moves `person+0xD8` (happiness) and `person+0xE0` (health), and
attributes each change. Which reason is decided by two fields and nothing else:

```asm
cmp dword [rsi+8],3      ; alcohol      cmp [rsi+0x10], [rbp+0xC300]  ; food
cmp dword [rsi+8],4      ; church       cmp [rsi+0x10], [rbp+0xC310]  ; meat
cmp dword [rsi+8],5      ; culture      cmp [rsi+0x10], [rbp+0xC318]  ; clothes
cmp dword [rsi+8],6      ; sport        cmp [rsi+0x10], [rbp+0xC320]  ; electronics
cmp dword [rsi+8],0xA    ; hospital
```

`rsi` is the demand, `rbp` the world. The four resource comparisons are reached
from kinds 1 and 2, the two a shop serves; `game+0xC300`… are the base game's
four cached shop-goods records — the same four `needs` had to repair a vector
for in `0x198670`.

So **a demand that is not in the array when this function runs affects
nothing**, and one that is in the array everywhere else still routes the citizen
to the pub, the church and the shop, because the movement and shopping code runs
in other functions on other ticks. The demand half is therefore a pre/post
bracket and no patch at all:

1. scan the array; if nothing is locked, do nothing — this runs for every
   citizen on every tick;
2. otherwise copy all `n × 0x80` bytes, compact the locked entries out, write
   the shortened count to `person+0x110`;
3. call the original;
4. put the array and the count back byte for byte.

`savedCount` is set *before* the edit, so a fault mid-compaction still restores.
The evaluator's own scratch lives at `person+0x498`…`+0x4B8` — the eighth demand
slot, which the array never reaches — so the restore cannot tread on it.

**Removing the entry removes the bonus with it.** That is deliberate: it is what
"the early cinema changes nothing" means. There is no arrangement that keeps the
reward and drops the penalty, because the function attributes only the downward
half and computes both from the same demand.

### The status floats have to be held

`person+0xD8` is eleven floats in the script VM's order — happiness, food,
health, soviet, alcohol, culture, sport, religion, clothing, electronic, crime —
and it is the **planner** that decays them, not the evaluator. Left alone,
twenty years of locked culture would rot the culture status to zero and the whole
republic would turn miserable on the day the cinema starts counting. So a locked
need's own status is written back to `locked_status` after every evaluation.

Two are deliberately never written:

- **health** is the citizen's real health, fed by pollution, alcohol, water and
  cold as well. Pinning it at 1.0 would make everybody unkillable.
- **happiness** is the thing being protected, not a per-need status.

`food`'s status is satiety, so pinning it means nobody starves while food is
locked — which is exactly what the grace year is for.

### The nagging message

`person+0x4F0` is the unsatisfied-demand list the planner fills from whatever the
last cycle left over, and it is what raises *"N Citizen(s) were unable to get
X"*. `clear_unsatisfied` compacts locked needs out of it in the same pre-hook,
matching on the same kind and record the demand carries — the entries are
`{ float amount, int kind, Resource* }`, 16 bytes, capped at ten.

## The building side is twelve rates in `.rdata`

Electricity, water and interior temperature never reach a demand array. Three
building functions subtract from every inhabitant directly:

| Function | Root | What |
|---|---|---|
| `0x1BC1A0` | living tick | happiness: *No electricity*, *No water* |
| `0x488AE0` | interior temperature | happiness **and** health |
| `0x1B08E0` | drinking water | health |

Every one has the same shape. The amount is computed **once, before the loop over
the inhabitants**, scaled by the "Unsatisfied citizens reaction" setting at
`game+0x5C8` (×0.8 / ×1.0 / ×1.2), and then per person:

```asm
movss   xmm0,[rcx+0xD8]
subss   xmm0,<amount>
movss   [rcx+0xD8],xmm0
...
movss   xmm0,[rsi+0x59C]     ; once per calendar day only
ucomiss <amount*k>,0
je      skip                 ; <- zero means the statistics are not touched either
float[reason] += it ; count[reason]++
```

**An amount of zero is a complete no-op** — no subtraction, no clamp at zero, and
the counter is skipped, which is exactly what a need that does not exist yet
should look like in the statistics window.

Each amount is finalised by one rip-relative load of a `.rdata` constant, so the
whole patch is twelve rewritten displacements — no hook, no code cave, and the
same technique `walking` uses:

| Site | Instruction | Constant | Value | Need |
|---|---|---|---|---|
| `0x1BC32B` | `mulsd xmm1,[rip]` | `0x909E50` | 0.000216667 (double) | electricity |
| `0x1BC505` | `mulsd xmm1,[rip]` | `0x909E78` | 0.000333333 (double) | electricity |
| `0x1BC6DB` | `mulss xmm3,[rip]` | `0x909B10` | 0.001 | water |
| `0x488B4A` | `movss xmm7,[rip]` | `0x909B30` | 0.0015 | heating, happiness |
| `0x488B7C` | `movss xmm7,[rip]` | `0x909B1C` | 0.00117 | heating, happiness |
| `0x488B66` | `movss xmm8,[rip]` | `0x909B38` | 0.0017 | heating, health |
| `0x488B84` | `movss xmm8,[rip]` | `0x909B28` | 0.001326 | heating, health |
| `0x1B0AC7` | `mulss xmm2,[rip]` | `0x909AD4` | 0.00025 | water, health |
| `0x1B0AD4` | `mulss xmm0,[rip]` | `0x909ADC` | 0.0003125 | water, health |
| `0x1B1193` | `mulss xmm1,[rip]` | `0x909AC0` | 0.0001875 | water, health |
| `0x1B11A3` | `mulss xmm1,[rip]` | `0x909AD4` | 0.00025 | water, health |
| `0x1B11B3` | `mulss xmm1,[rip]` | `0x909ADC` | 0.0003125 | water, health |

Two per need where the game has two branches: heating splits warm/cold, and
electricity splits on how the shortfall is measured. `0x1B0AD4` is a *bonus* term
for clean water rather than a penalty, and it is zeroed too — a need that does
not exist must not pay either way.

**Overwriting the constants in place is the obvious alternative and is wrong for
the usual reason:** `0x909AD4` and `0x909ADC` are each read by two different
blocks, and the literal pool is shared. Rewriting one displacement touches
exactly one read.

Each slot holds the vanilla value until the first tick reports a year, so a
plugin that has not decided anything yet is bit for bit the base game.

## School and kindergarten

Neither raises a happiness or a health reason, so neither is a need in the sense
above. What they are is the game's own **Education simulation** option (language
ids 710 Simple / 711 / 712 Complex), whose help text is precisely the two
behaviours wanted:

> id 713 — children automatically reach basic education (no elementary schools
> needed for new born citizens); parents can work even while their children are
> under 6 years old (no kindergarten needed).

It lives at **`game+0x5BC`**, `> 0` meaning Complex, and it is written into the
save header by `0x42CBD0` alongside the date and the other options.

Setting that field to 0 is not an option: the interface reads it too, and a
kindergarten that cannot be built is worse than one that is not needed. So the
plugin rewrites the twelve `cmp dword ptr [reg+0x5BC], 0` sites that belong to
the *simulation* into `cmp dword ptr [rip+ours], 0` — the same seven bytes in
the no-REX form, one byte shorter plus a `nop` with REX.B — and leaves every
interface site (`0x76A7B0`, `0x775180`, `0x7A4870`, `0x76B2F0`) alone:

```
0x1A9CF0 0x1A9D60 0x1A9DD0   a workplace requires basic education
0x1AA03F                     the wrapper that picks between them
0x1A772A                     a workplace's own education demand
0x1AD827                     productivity from education
0x3BB29A                     the same test, from a person
0x831A23                     a school/university workplace check
0x822FDE 0x823129            a new citizen's education at birth
0x825FD4 0x8260C6            the job search's education filter
```

The flag mirrors the player's own choice once the year arrives, and is 0 before
it; it starts at 0, so the one tick before the first year is read behaves as
Simple rather than as Complex.

**`education` is one key and it is off by default**, for two honest reasons:

- **School and kindergarten cannot yet be given different years.** The site that
  blocks a parent of a young child has not been told apart from the ones that
  test a workplace's required education level; all twelve above test
  `building+0x604`, the required education, or a person's own. Splitting the two
  needs that site found first.
- **The claim that the build menu stays available has not been watched in
  game.** It follows from the site list, not from observation.

## Settings

`plugins/easystart.ini`:

| Key | Default | What |
|---|---|---|
| `enabled` | 1 | |
| `demands` | 1 | the nine demand-carried needs |
| `utilities` | 1 | electricity, water, heating |
| `pin_status` | 1 | hold a locked need's status float |
| `locked_status` | 1.0 | at what level |
| `clear_unsatisfied` | 1 | drop locked needs out of `person+0x4F0` too |
| `probe` | 0 | a line per year change, plus one full citizen dump |

`[unlock]` takes a year, `off` or `always` per need. The shipped schedule
assumes a republic founded in 1920 with the first year as a grace period:

```ini
[unlock]
food = 1921        religion = 1921
meat = 1927        clothes  = 1927
alcohol = 1932     sport    = 1932
culture = 1940     health   = 1940
electricity = 1945
water = 1950       heating  = 1950
electronics = 1955
education = off
```

`[unlock_resource]` does the same for a good `needs` added — `list` names them,
comma separated, and each takes a year. The record is matched by name, cached in
a 16-entry table keyed on the record pointer, because resolving it costs a
`VirtualQuery` and this runs per citizen per tick.

**A base-game start is 1960, 1970 or 1980**, at which point every line above is
already in the past and the plugin correctly does nothing. It is for a map that
starts early — the Early Start content, or a date wound back by other means.

## Testing it

Nothing here has been watched in game. The build is clean and every patch site
is compared against this build's bytes before anything is written, which is not
a result — see [07-pitfalls.md](07-pitfalls.md).

1. Tick `easystart` in the launcher. Set `probe = 1`. Load a save whose year is
   **before** some of the unlock years — winding the date back is the whole
   premise, so a 1970 save proves nothing.
2. The log should carry, at startup, twelve `rate ... now reads ...` lines and
   `ready: demand gate on, 12 of 12 utility rate(s)`. A `refusing` line means
   the game moved and that rate is simply left vanilla.
3. Then, on the first tick, one line per year:
   `easystart  year 1922, still to come: meat clothes electronics alcohol ...`
   If that line never appears the hook is installed but the evaluator is not
   being reached; if it says a year you did not expect, `game+0x594` is not what
   the corner of the screen shows and everything else is moot.
4. The `probe` dump names the first citizen whose list was shortened:
   `4 demand(s), 2 hidden, 2 left for the evaluator`, then a line per demand
   with its kind. Kinds 3/4/5/6/10 are alcohol, church, culture, sport, doctor;
   kinds 1 and 2 are the shop goods. **Two hidden out of four with nothing but
   food and meat in the list is the wrong answer** and means the resource
   comparison missed.
5. **The point of the whole plugin** — open the statistics window's *Happiness
   change* panel. Every locked need's line must read zero. A non-zero
   "Unable to enjoy culture" while `culture` is locked means the bracket is not
   taking effect; a non-zero "No water" while `water` is locked means a rate was
   refused or the slot is not being written.
6. Watch a citizen use a service that is built but locked — a pub in 1925. They
   should still go in, and the *Unable to drink alcohol* line should stay at
   zero either way.
7. Turn a year: play across an unlock boundary (or edit the ini to one year
   ahead) and check the locked status did not arrive at zero. The culture status
   in the citizen window should be at `locked_status`, not at the floor.
8. Check the control: set every `[unlock]` key to `always` and confirm the game
   is exactly as it was. Nothing is patched away in that state — the rates hold
   their vanilla values and the demand bracket short-circuits on `g_anyLocked`.

## What this does not do

- **A locked good is still bought.** Shops still stock and sell food, meat,
  clothes and electronics while those needs are locked, because the sale happens
  in `0x171DA0` and is nothing to do with the ledger. Only the consequences are
  gated. Stopping the purchase would mean editing the demand for real, and then
  the citizen would stop visiting the shop, which is the opposite of the point.
- **No status float of its own for a `needs` good.** `[unlock_resource]` gates
  the ledger entry, but a cloned demand had no status float to begin with — see
  [11-needs.md](11-needs.md).
- **Crime and pollution are left alone.** Both are real reasons in both ledgers
  and both are gateable by the same means, but they are consequences of the town
  rather than needs of a citizen.
- **Loyalty is not need-derived.** *Low government loyalty* is a reason for
  happiness falling, not the other way round; loyalty itself comes from
  monuments, broadcasts, personal cars and the secret police.
