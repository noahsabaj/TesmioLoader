// construction - how far a construction office reaches for work, as a
// tesmioloader plugin.
//
// THE FEATURE: a construction office only picks up jobs near itself.
// `office_range` in the ini - 10 km by default - makes that reach yours, so an
// office serves a whole district instead of a few streets.
//
// STATE: THE RANGE IS FOUND AND THE FEATURE WORKS. It turned out to be a plain
// int on the office object, at +0xFC8 (B_OFFICE_RANGE below) - the very number
// the office's own window prints as "<3,500m". So there is no spliced code and
// no repointed constant in the feature at all: WriteRanges simply sets the
// field, which is as robust as a patch in this project gets. Confirmed in a
// running game - a site ~9 km out is picked up and the road overlay matches -
// and the simulation does not clamp the value back.
//
// Two smaller pieces sit either side of it:
//
//   the window ceiling  the office window's + button stopped at 3500 because
//                       of imm32 clamps and a fixed rung ladder baked into the
//                       button handlers. PatchClamp raises the top rung, so
//                       the range stays adjustable by hand past the default.
//                       Seven sites, all verified before any is written.
//   the probe           still here, still useful, and now purely diagnostic:
//                       it is what found +0xFC8 and it is what would find the
//                       field again after a game update moved it.
//
// The kRangeSites/kLimitSites tables below are still empty, and deliberately:
// they are the *code-patch* route, which the field write made unnecessary. A
// table of zeroes logs "no address yet" rather than refusing, so `patch = 1`
// on an empty table does nothing instead of complaining about a build that is
// fine. They stay because a future game version might make the field read-only
// and force the issue back to a code patch.
//
// A count cap is the other thing the limit could have been - an office that
// stops at N jobs however close they are - so the probe measures both and
// keeps them in separate tables, because finding one must not block patching
// the other.
//
//
// WHAT AUTOMATIC ASSIGNMENT IS
//
// A construction office services a set of construction sites, and offers to
// pick them up without being told to. The game's own script API names both
// halves of the relationship - media_soviet/scripts/SOVIETInstructions.txt:
//
//     defineStructVariable(Building, int, nConstructionBuildingNum);
//     defineStructVariable(Building, int, nConstructionOfficeNum);
//     defineInstruction(Building_ConstructionBuilding_GetID, 11015, ...);
//     defineInstruction(Building_ConstructionOffice_GetID,   11016, ...);
//     BUILDINGTYPE_CONSTRUCTION_OFFICE      = 12;
//     BUILDINGTYPE_CONSTRUCTION_OFFICE_RAIL = 28;
//
// so an office holds a list of sites and a site holds a list of offices - the
// same reciprocal shape as nWalkingBuildingNum and building+0xCA8. Two lists
// to find, and each one validates the other.
//
// Those declarations give the fields NAMES and not OFFSETS. The script API's
// declaration order is not the struct's layout order - nStorageNum is declared
// thirty lines after nWalkingBuildingNum while +0x970 sits far before +0xCA8 -
// so betting on it would be a guess written down as a finding, which is the
// one mistake docs/07-pitfalls.md singles out as having cost a feature.
//
//
// WHERE THE CAP IS NOT
//
// It is not in building.ini. All six stock construction offices plus
// rail_construction_office declare $TYPE_CONSTRUCTION_OFFICE, storages,
// workers and $WORKING_VEHICLES_NEEDED, and not one of them carries a radius
// or a site count. So the limit is global, and that leaves exactly two shapes,
// which want different patches:
//
//   a literal in .text/.rdata      -> repoint the read, walking.cpp's Repoint
//   a field of the game object     -> repoint the comparisons, the way
//                                     easystart does for game+0x5DC
//
// The probe tells those apart for free by reporting WHERE the number lives: on
// the office object or the type descriptor for the first, at a fixed offset in
// the game object for the second.
//
//
// WHY A PROBE AND NOT A PATCH
//
// Because nothing about this subsystem has ever been located, and
// docs/03-reverse-engineering.md ranks runtime observation above static
// analysis for exactly this case: one game session answers "which offset is
// the list" and "what value does it stop at" together, and the second number
// is the whole of what a .text scan would then be looking for.
//
//
// WHAT THIS DELIBERATELY DOES NOT DO
//
// Walking distance and personal-car distance. plugins/walking (v1.3) owns
// those and is published on the Steam Workshop as "Increase Walking Distance";
// `distance` and `car_distance` in walking.ini are the supported way to get
// them. Nothing here touches query+0x3C for feet or for cars.
//
// It also does not ship a `trip_limit` setting. Whether this game times a long
// journey out at all is unestablished - docs/12-walking.md records that nothing
// downstream re-checks a distance once a pair is in the connection list - so
// the probe reports on it and no key pretends to control it. A key that
// provably does nothing is worse than no key.
//
//
// WHERE IT RUNS, AND THE ONE RACE IT HAS
//
// An import swap on C3D_TERRAIN::Render, exactly as plugins/aging does, so it
// chains with depletion's hook on the same slot and needs no address of its
// own. The building dispatcher would be the natural home and is not available:
// plugins/accumulator already owns an inline hook on it at 0x139A70, and a
// second InstallInlineHook on that target would compare its expected bytes
// against accumulator's jump and refuse.
//
// The cost is that this reads simulation-thread structures from the render
// thread. Three things answer that, and the third is new relative to aging:
// nothing is cached across samples, everything is guarded, and a candidate is
// only accepted when it has held across two consecutive samples - a torn read
// of a vector mid-push_back otherwise looks exactly like a field that moved
// and moved back.
//
// Everything here is addresses for SOVIET64.exe v1.1.1.9, verified before
// anything is read. See docs/17-construction.md.

#include "../../src/tesmio_plugin.h"
#include <math.h>

// ---------------------------------------------------------------- the sites

// The game object is a STATIC, inline in .data - not a pointer to one. It is
// the object carrying the building vector, the date and the resource vector.
//
// Do not confuse it with the pointer at 0x9941F0, which plugins/depletion and
// plugins/deposits use: that is a different object, the one holding the
// terrain at +0xED8 and the resource maps at +0xF00/+0xF08. Both are called
// "the game object" in places and they are not the same thing.
#define P_GAMEOBJ            0x9D4F10

// The lea the save loader itself uses to address it, two instructions before
// it hands a building's position to the city search. Verified before anything
// below runs, the same way plugins/cities does it - if this no longer resolves
// to P_GAMEOBJ then .data has moved and nothing here is safe.
#define RVA_GAMEOBJ_LEA      0x43970A   // v1.1.1.9; was 0x43966A

#define G_BUILDINGS          0x11B08    // game -> vector<Building*> begin
#define G_BUILDINGS_END      0x11B10    // ...and end
#define G_DAY                0x590      // int, day of the year
#define G_YEAR               0x594      // int

#define B_TYPEDESC           0x318      // the type record; null on scenery
#define B_NODE               0x320      // C3D_NODE, for GetPosition
#define B_BBOX               0x5D0      // ->+0x20 min, ->+0x28 max
#define B_FINISHED           0x604      // float; >= 1.0 means built
#define B_DEMOLISHED         0xEA8      // non-zero means going away
#define B_PARKING            0xCC0      // vector<ParkingConnection>, stride 0xF0
#define T_BUILDING_TYPE      0x360      // the BUILDINGTYPE_* number

// THE AUTO-SEARCH RANGE, in whole metres, as an int.
//
// Found by the probe rather than by disassembly, and the evidence is about as
// good as this kind of thing gets: on a map carrying sixteen construction
// offices, the value the office window prints - 3500 - occurs at exactly ONE
// offset in the office object, the same offset in all sixteen, and nowhere in
// the type descriptor. A coincidence does not survive sixteen independent
// objects agreeing.
//
// It is an int and not a float, which is worth knowing: the UI prints whole
// metres and the field stores them.
#define B_OFFICE_RANGE       0xFC8

// What the game's own interface will wind it to, and the number the whole
// plugin exists to get past.
#define VANILLA_RANGE_MAX    3500

// The largest range this plugin will set or accept as plausible. The same
// ceiling plugins/walking uses, for the same reason: these searches are
// breadth-first over the road graph, so the work grows with the square of the
// limit. It bounds `office_range`, and it is also the upper half of the
// "does +0xFC8 still hold something that looks like a range" invariant in
// WriteRanges - one constant so the two can never drift apart.
#define RANGE_MAX            20000

// --- the ceiling, and where it is written down ----------------------------
//
// 3500 is not read from anywhere. It is an imm32 baked into the office
// window's own button handler, twice, as a plain clamp:
//
//   0074E084  8B 86 C8 0F 00 00     mov  eax,[rsi+0xFC8]
//   0074E08A  41 3B C4              cmp  eax,r12d
//   0074E08D  7D 09                 jge  +9
//   0074E08F  44 89 A6 C8 0F 00 00  mov  [rsi+0xFC8],r12d      ; up to a minimum
//   0074E096  EB 11                 jmp  +0x11
//   0074E098  3D AC 0D 00 00        cmp  eax,3500              ; <- and down to
//   0074E09D  7E 0A                 jle  +0xA
//   0074E09F  C7 86 C8 0F 00 00 AC 0D 00 00
//                                   mov  [rsi+0xFC8],3500      ; <- 3500
//
// which is the whole reason the plus button stops there, and the reason
// pressing minus on an office this plugin had set to 10000 snapped it to 3500
// rather than stepping down: the click ran this, and the clamp fired.
//
// It also confirms +0xFC8 independently of the probe that found it. The probe
// inferred the offset from one value matching across sixteen offices; this
// instruction names the offset outright while clamping it to the number the
// window prints. Two unrelated methods agreeing is worth more than either.
//
// A second site does the same comparison against a copy of the field in
// another register, at 0x751A5A. A second implementation of one rule is
// routine in this executable - walking found the same distance written out in
// four separate functions - so both are patched, and neither is patched
// unless both verify. Patching one of two is how that plugin shipped two
// versions where the simulation and the window disagreed.
//
// These are immediates inside instructions, not constants in .rdata, so there
// is nothing shared to disturb: rewriting them touches these two sites and
// nothing else in the game.

#define RVA_CLAMP_CMP_A   0x74E08F   // the store, the jmp, then cmp eax,3500
#define RVA_CLAMP_SET_A   0x74E09F   // mov [rsi+0xFC8],3500
#define RVA_CLAMP_CMP_B   0x751A5A   // mov ecx,[r13+0xFC8] then cmp ecx,3500
#define RVA_CLAMP_SET_B   0x751A69   // mov eax,3500

static const BYTE kClampCmpA[] = {
    0x44, 0x89, 0xA6, 0xC8, 0x0F, 0x00, 0x00,   // mov [rsi+0xFC8],r12d
    0xEB, 0x11,                                  // jmp +0x11
    0x3D, 0xAC, 0x0D, 0x00, 0x00                 // cmp eax,3500   <- imm at +10
};
static const BYTE kClampSetA[] = {
    0xC7, 0x86, 0xC8, 0x0F, 0x00, 0x00,          // mov dword [rsi+0xFC8],
    0xAC, 0x0D, 0x00, 0x00                       //      3500      <- imm at +6
};
static const BYTE kClampCmpB[] = {
    0x41, 0x8B, 0x8D, 0xC8, 0x0F, 0x00, 0x00,   // mov ecx,[r13+0xFC8]
    0x81, 0xF9, 0xAC, 0x0D, 0x00, 0x00           // cmp ecx,3500   <- imm at +9
};
static const BYTE kClampSetB[] = {
    0xB8, 0xAC, 0x0D, 0x00, 0x00                 // mov eax,3500   <- imm at +1
};

struct ClampSite
{
    DWORD       rva;
    const BYTE* expect;
    int         len;
    int         immOff;
    const char* label;
};

// --- and the ladder, which is what the buttons actually walk ---------------
//
// The range is not a slider. The minus and plus buttons step through a fixed
// menu of rungs, and each button has its own copy of it:
//
//   0076C792  B9 B8 0B 00 00        mov  ecx,3000
//   0076C797  B8 AC 0D 00 00        mov  eax,3500        ; the top rung
//   0076C79C  44 3B C0              cmp  r8d,eax
//   0076C79F  7F 1E                 jg   -> take 3500    ; anything above snaps here
//   ... then 3000, then 2000, then r9d, then 100
//
// which is why pressing minus on an office holding 10000 gave 3500 rather than
// stepping down: 10000 is above the top rung, so the ladder snapped to it.
// The step-up ladder at 0x76C7C5 is the same menu read the other way, and its
// own copy of 3500 is the reason plus stops there.
//
// So the four sites above were real - the game does clamp the field in a
// validator - but they are not what the buttons obey. These three are.
//
// Raising the top rung is all that is done here. Adding rungs would mean
// emitting code; changing the top one is four bytes each and leaves the menu
// otherwise exactly as the game shipped it.

#define RVA_LADDER_DOWN   0x76C792   // mov ecx,3000 / mov eax,3500
#define RVA_LADDER_UP_CMP 0x76C7C5   // mov ecx,[r12] / cmp ecx,3500
#define RVA_LADDER_UP_SET 0x76C7D1   // mov eax,3500 / jmp / mov eax,3000

static const BYTE kLadderDown[] = {
    0xB9, 0xB8, 0x0B, 0x00, 0x00,                // mov ecx,3000
    0xB8, 0xAC, 0x0D, 0x00, 0x00                 // mov eax,3500   <- imm at +6
};
static const BYTE kLadderUpCmp[] = {
    0x41, 0x8B, 0x0C, 0x24,                      // mov ecx,[r12]
    0x81, 0xF9, 0xAC, 0x0D, 0x00, 0x00           // cmp ecx,3500   <- imm at +6
};
static const BYTE kLadderUpSet[] = {
    0xB8, 0xAC, 0x0D, 0x00, 0x00,                // mov eax,3500   <- imm at +1
    0xEB, 0x22,                                  // jmp +0x22
    0xB8, 0xB8, 0x0B, 0x00, 0x00                 // mov eax,3000
};

static const ClampSite kClampSites[] = {
    { RVA_CLAMP_CMP_A,   kClampCmpA,   sizeof(kClampCmpA),  10, "validator compare" },
    { RVA_CLAMP_SET_A,   kClampSetA,   sizeof(kClampSetA),   6, "validator store"   },
    { RVA_CLAMP_CMP_B,   kClampCmpB,   sizeof(kClampCmpB),   9, "second compare"    },
    { RVA_CLAMP_SET_B,   kClampSetB,   sizeof(kClampSetB),   1, "second store"      },
    { RVA_LADDER_DOWN,   kLadderDown,  sizeof(kLadderDown),  6, "minus top rung"    },
    { RVA_LADDER_UP_CMP, kLadderUpCmp, sizeof(kLadderUpCmp), 6, "plus compare"      },
    { RVA_LADDER_UP_SET, kLadderUpSet, sizeof(kLadderUpSet), 1, "plus top rung"     },
};
#define CLAMP_SITE_COUNT (sizeof(kClampSites) / sizeof(kClampSites[0]))

// SOVIETInstructions.txt:2125-2126.
#define BT_OFFICE            12
#define BT_OFFICE_RAIL       28

// Offsets that are already known to be vectors on a building, and so are not
// candidates. Excluding them by name is cheaper and more honest than
// re-deriving that they are not the one we want.
#define OFF_STORAGES         0x970
#define OFF_WIRES            0xA10
#define OFF_OCCUPANTS        0xBD8
#define OFF_WALKING          0xCA8
#define OFF_PARKING          0xCC0
#define OFF_CATCHMENT        0x10C8

// void C3D_TERRAIN::Render(bool, C3D_CAMERA*, C3D_CAMERA*, int, int), out of
// the engine's export table rather than written out by hand.
#define SYM_TERRAIN_RENDER "?Render@C3D_TERRAIN@@QEAAX_NPEAVC3D_CAMERA@@0HH@Z"
#define SYM_NODE_GETPOS    "?GetPosition@C3D_NODE@@QEAA?AVC3DVECTOR3@@XZ"

// --- the patch table ------------------------------------------------------
//
// Empty until the probe has named an address. `rva == 0` means "not found
// yet", which makes the group UNAVAILABLE rather than FAILED - it logs and is
// skipped, and `patch = 1` on an empty table does nothing rather than
// refusing loudly about a build that is fine.

enum SiteKind
{
    SITE_IMM32,       // mov dword ptr [mem], imm32   - replace the immediate
    SITE_RIP_FLOAT,   // movss/mulss xmm,[rip+disp32] - repoint the read
    SITE_RIP_INT32,   // cmp/mov r32,[rip+disp32]     - repoint, integer cap
    SITE_CMP_IMM8     // cmp dword[reg+disp32],imm8   - rewrite rip-relative
};

#define SLOT_OFFICE_RANGE  0
#define SLOT_OFFICE_LIMIT  1
#define SLOT_COUNT         2

struct Site
{
    DWORD       rva;        // 0 = unknown; the group is skipped, not refused
    SiteKind    kind;
    const BYTE* op;         // the opcode bytes this build must carry
    int         opLen;
    DWORD       constRva;   // what the displacement must resolve to
    double      vanilla;    // and the value that must be sitting in there
    int         slot;
    const char* label;
};

struct Group
{
    const Site* sites;
    int         count;
    int*        ok;         // one flag per site, easystart's g_ratesOk shape
    const char* label;
};

// v1.1.1.9, and both tables are empty until the probe fills them in.
//
// THE RANGE is the one this plugin exists for: how far from itself an office
// will pick up a job. Budget for more than one row - if this behaves like every
// other distance in this game there is the simulation's copy and the office
// panel's own copy of it, and patching one while the other draws the old number
// is the bug plugins/walking took four versions to stop shipping.
static const Site kRangeSites[] = {
    { 0, SITE_RIP_FLOAT, NULL, 0, 0, 0.0, SLOT_OFFICE_RANGE, "office pickup range" },
};

// THE COUNT is the other thing the limit could turn out to be, kept separate so
// that finding one does not block patching the other.
static const Site kLimitSites[] = {
    { 0, SITE_RIP_INT32, NULL, 0, 0, 0.0, SLOT_OFFICE_LIMIT, "office assign cap" },
};

static int g_rangeOk[sizeof(kRangeSites) / sizeof(kRangeSites[0])];
static int g_limitOk[sizeof(kLimitSites) / sizeof(kLimitSites[0])];

static const Group kRangeGroup = {
    kRangeSites, sizeof(kRangeSites) / sizeof(kRangeSites[0]), g_rangeOk, "range"
};
static const Group kLimitGroup = {
    kLimitSites, sizeof(kLimitSites) / sizeof(kLimitSites[0]), g_limitOk, "cap"
};

// ---------------------------------------------------------------- settings

static int g_enabled  = 1;
static int g_patch    = 0;      // the second half; off until the table is filled
static int g_probe    = 1;      // and the first half, which is what 0.1 is for

static int g_officeRange = 10000;  // metres of reach. The point of the plugin
static int g_officeLimit = 0;      // 0 = no cap at all

// Raise the office window's own ceiling so the plus button works past 3500.
//
// The default matches g_officeRange deliberately, and construction.ini says to
// keep them equal: then the highest the + button reaches is the value a newly
// built office already starts at, and the menu and the config tell one story.
static int g_raiseCeiling = 1;
static int g_ceiling      = 10000;

// The value to hunt for in a construction office, as an int and as a float.
//
// This is the shortcut the office's own window handed us: it shows the
// auto-search range as "<3,500m" with a - and a + beside it, so the range is
// a per-office number the player already sets, and its current value is known
// before the search starts. Searching for a value you can read off the screen
// beats inferring one from behaviour by a wide margin.
//
// 0 turns the hunt off.
// Set office_range into a construction office. This is the feature, and it is
// the one thing in this plugin that writes to the game.
static int g_writeRange = 1;

// What a construction office starts life at, in the base game. An office still
// carrying this has not been touched by the player, and is the only kind this
// plugin raises - which is what makes office_range a DEFAULT rather than a
// value the plugin holds you to.
//
// 0 means "no such test": every office is held at office_range every frame,
// and the minus button in its window then cannot lower it, because the next
// frame puts the value straight back. That was version 0.1's behaviour and it
// was wrong - it was insurance against a re-clamp the game turns out not to
// do.
static int g_gameDefault = 1000;

static int g_probeExpect = 3500;

// Report every 4-byte slot of an office that changed since the last report,
// which is what turns a list of candidates into an answer: click the + or the
// - in the office window and exactly one of them moves.
static int g_probeDiff = 1;

static int g_probeFrom   = 0x300;
static int g_probeTo     = 0x1800;
static int g_probePeriod = 5;   // seconds between reports
static int g_probeSites  = 8;   // how many list members to print per office

// The .rdata window searched for the range once it has been bracketed. The
// default spans the pool the game keeps its layout constants and rates in -
// where walking's 480 and 530, cities' 1000 and the deposit radii all live.
static int g_rdataFrom = 0x909000;
static int g_rdataTo   = 0x90C000;

// ---------------------------------------------------------------- state

#define MAX_OFFICES   16
#define OFFICE_SNAP   0x1800    // bytes of an office kept for the field diff
#define MAX_LIVE      2048      // live construction sites tracked in one report
#define MAX_ELEMS     256       // most members a candidate list may hold
#define MAX_TYPE      128       // histogram buckets
#define EXTENT_CHUNK  0x400

typedef void   (*t_TerrainRender)(void*, bool, void*, void*, int, int);
typedef float* (*t_VecGetter)(void*, float*);

static t_TerrainRender o_TerrainRender;
static t_VecGetter     o_NodeGetPosition;

struct Office
{
    BYTE* building;
    int   type;

    DWORD listOff;      // the accepted offset, 0 while unknown
    int   stride;
    int   elemPtrOff;   // 0 or 8 - where the Building* sits inside an element

    DWORD pendOff;      // a candidate awaiting a second confirming sample
    int   pendStride;
    int   pendPtrOff;

    int   held;
    int   heldPrev;
    int   frozen;       // consecutive reports held did not move while work waited
    int   maxHeld;
    int   growable;     // cap > end, i.e. a real vector rather than a fixed array

    // The range evidence. Everything inside claimedMax was picked up; the
    // nearest thing it refused is unclaimedMin. When those two do not overlap
    // the range is bracketed between them, and that bracket is the whole point
    // of this plugin.
    float claimedMin;
    float claimedMax;
    float unclaimedMin;
    int   scanned;      // the .rdata hunt is done once per bracket, not per report

    int   raised;       // decided about once, and never reconsidered
    int   hunted;       // the value hunt is done once per office, not per report
    int   haveSnap;
    DWORD snapLen;      // how much of `snap` the last snapshot actually filled
    BYTE  snap[OFFICE_SNAP];
};

static Office g_office[MAX_OFFICES];
static int    g_offices;

static bool  g_armed;
static bool  g_typedOnce;       // the histogram is printed once
static int   g_reports;
static DWORD g_lastReport;

static float* g_slot;

// ---------------------------------------------------------------- reading

// The lea check plugins/cities does. If .data has moved, everything below is
// pointed at nothing and the honest thing is to say so and stop.
static bool VerifyGameObject(void)
{
    BYTE* site = g_exeBase + RVA_GAMEOBJ_LEA;
    if (!ReadablePtr(site, 7)) return false;
    if (site[0] != 0x48 || site[1] != 0x8D || site[2] != 0x0D) return false;

    DWORD resolved = (DWORD)(RVA_GAMEOBJ_LEA + 7 + *(const int*)(site + 3));
    if (resolved != P_GAMEOBJ)
    {
        Logf("construct  game object: rva 0x%X points at 0x%X, expected 0x%X - refusing",
             RVA_GAMEOBJ_LEA, resolved, P_GAMEOBJ);
        return false;
    }
    return true;
}

static BYTE* GameObject(void) { return g_exeBase + P_GAMEOBJ; }

// begin/end of the building vector, with the sanity checks that cost nothing.
// A count of half a million is a wrong address, not a big republic.
static int Buildings(BYTE* game, BYTE*** outBegin)
{
    if (!ReadablePtr(game + G_BUILDINGS, 16)) return 0;

    BYTE** begin = *(BYTE***)(game + G_BUILDINGS);
    BYTE** end   = *(BYTE***)(game + G_BUILDINGS_END);
    if (!begin || !end || end < begin) return 0;

    size_t bytes = (size_t)((BYTE*)end - (BYTE*)begin);
    if (bytes % sizeof(void*)) return 0;

    size_t n = bytes / sizeof(void*);
    if (n == 0 || n > 500000) return 0;
    if (!ReadablePtr(begin, bytes < 4096 ? bytes : 4096)) return 0;

    *outBegin = begin;
    return (int)n;
}

static int BuildingType(BYTE* b)
{
    if (!b || !ReadablePtr(b + B_TYPEDESC, 8)) return -1;
    BYTE* td = *(BYTE**)(b + B_TYPEDESC);
    if (!td || !ReadablePtr(td + T_BUILDING_TYPE, 4)) return -1;
    return *(const int*)(td + T_BUILDING_TYPE);
}

// The dispatcher's own two tests for "this is a live construction site".
static bool IsUnfinished(BYTE* b)
{
    if (!ReadablePtr(b + B_FINISHED, 4) || !ReadablePtr(b + B_DEMOLISHED, 1))
        return false;
    return *(const float*)(b + B_FINISHED) < 1.0f &&
           *(const char*)(b + B_DEMOLISHED) == 0;
}

// A cheap pre-filter before the expensive membership scan: anything that is
// really a Building has a readable type descriptor carrying a plausible type
// number. This is what keeps the candidate scan from running a linear search
// over several thousand pointers for every one of several hundred offsets.
static bool LooksLikeBuilding(BYTE* p)
{
    int t = BuildingType(p);
    return t >= 0 && t < MAX_TYPE;
}

static bool InBuildingVector(BYTE* p, BYTE** begin, int count)
{
    for (int i = 0; i < count; i++)
        if (begin[i] == p) return true;
    return false;
}

// How far into this object it is safe to read, asked in chunks rather than as
// one span. sizeof(Building) is not known, and asking ReadablePtr about a
// fixed large span is the mistake docs/07-pitfalls.md records under "One
// VirtualQuery is not a range check" - it starts answering no partway through
// a session with every byte still perfectly readable.
//
// What this returns is a READABILITY BOUND, not an object size. A heap object
// sits inside a much larger committed region; anything past the real end of
// the building belongs to somebody else and is reported as such.
static DWORD ReadableExtent(BYTE* b, DWORD want)
{
    DWORD got = 0;
    while (got < want)
    {
        DWORD step = want - got < EXTENT_CHUNK ? want - got : EXTENT_CHUNK;
        if (!ReadablePtr(b + got, step)) break;
        got += step;
    }
    return got;
}

// ---------------------------------------------------------------- geometry
//
// The node position, overwritten by the bounding-box midpoint when the
// building has no type record - which is what the game's own reassignment
// pass computes, and copied from plugins/cities rather than re-derived.

static bool BuildingCentre(BYTE* b, float* out)
{
    if (!o_NodeGetPosition) return false;
    if (!ReadablePtr(b + B_BBOX, 8)) return false;

    float tmp[4] = { 0, 0, 0, 0 };
    const float* p = o_NodeGetPosition(b + B_NODE, tmp);
    if (!p) return false;
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];

    // B_TYPEDESC is a lower offset than B_BBOX but that does not make it
    // readable: a heap object can end anywhere, and the check above asked only
    // about +0x5D0. Ask about this one too rather than inferring it.
    if (!ReadablePtr(b + B_TYPEDESC, 8)) return true;
    if (*(void**)(b + B_TYPEDESC) != NULL) return true;

    BYTE* bb = *(BYTE**)(b + B_BBOX);
    if (!bb || !ReadablePtr(bb + 0x20, 16)) return true;
    const float* mn = *(const float**)(bb + 0x20);
    const float* mx = *(const float**)(bb + 0x28);
    if (!mn || !mx) return true;
    if (!ReadablePtr(mn, 16) || !ReadablePtr(mx, 16)) return true;

    out[0] = (mn[1] + mx[1]) * 0.5f;
    out[1] = (mn[2] + mx[2]) * 0.5f;
    out[2] = (mn[3] + mx[3]) * 0.5f;
    return true;
}

static float Distance(const float* a, const float* b)
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------- finding a number
//
// Once the probe has bracketed the range - everything inside R is picked up,
// everything outside it is not - the number R has to be somewhere. This looks
// for it, as a float, in the three places it could live, and reports every hit
// with its offset.
//
// A range is far more likely to be a literal in .rdata than a field of an
// office, because building.ini declares no such thing and every office behaves
// the same. .rdata is therefore scanned too, over the window the game keeps
// its layout constants and rates in - the same pool walking's 480 and 530 and
// cities' 1000 come out of.
//
// Hits are candidates and nothing more. The value 2500 appears in .rdata for
// several unrelated reasons, which is exactly why walking repoints one
// instruction rather than overwriting the constant.
static int ScanFloatRange(BYTE* base, DWORD from, DWORD to,
                          float lo, float hi, const char* what, int maxHits)
{
    int hits = 0;
    for (DWORD off = from; off + 4 <= to && hits < maxHits; off += 4)
    {
        if (!ReadablePtr(base + off, 4)) continue;
        float v = *(const float*)(base + off);
        if (v != v) continue;                       // NaN
        if (v < lo || v > hi) continue;

        Logf("construct      %s +0x%06X = %.3f", what, off, v);
        hits++;
    }
    // The same bytes as an int, for a range stored in whole metres.
    for (DWORD off = from; off + 4 <= to && hits < maxHits; off += 4)
    {
        if (!ReadablePtr(base + off, 4)) continue;
        int v = *(const int*)(base + off);
        if ((float)v < lo || (float)v > hi) continue;

        Logf("construct      %s +0x%06X = %d (int)", what, off, v);
        hits++;
    }
    return hits;
}

// Is this building in the office's personal-car connection list? If every
// claimed site is and every unclaimed one is not, the office's reach IS the
// vehicle path ceiling, plugins/walking already owns it, and this plugin has
// nothing left to do. That is a cheap thing to rule out and an expensive thing
// to discover late.
static bool InParkingList(BYTE* office, BYTE* site)
{
    if (!ReadablePtr(office + B_PARKING, 16)) return false;
    BYTE* begin = *(BYTE**)(office + B_PARKING);
    BYTE* end   = *(BYTE**)(office + B_PARKING + 8);
    if (!begin || !end || end <= begin) return false;

    size_t span = (size_t)(end - begin);
    if (span % 0xF0 || span > 4096 * 0xF0) return false;
    if (!ReadablePtr(begin, span < 4096 ? span : 4096)) return false;

    size_t n = span / 0xF0;
    if (n > 4096) return false;
    for (size_t i = 0; i < n; i++)
        if (*(BYTE**)(begin + i * 0xF0 + 8) == site) return true;
    return false;
}

// ---------------------------------------------------------------- the scan
//
// A {begin, end, cap} triple whose members are all live construction sites.
//
// The shape alone proves nothing - docs/07-pitfalls.md, "A heuristic find
// without an invariant is a write into a stranger's struct": a structure this
// long satisfies "three plausible pointers" by accident. What a lookalike
// cannot fake is where the pointers point, and the decisive test here is that
// EVERY member is an unfinished building. Walking connections, parking
// connections, the electric catchment and the occupant list all carry finished
// buildings; no other list on a building is all-unfinished.

struct Candidate
{
    DWORD off;
    int   stride;
    int   ptrOff;
    int   count;
    int   growable;
};

static const int kStrides[] = { 8, 0x10, 0x60, 0xF0 };
#define STRIDE_COUNT (sizeof(kStrides) / sizeof(kStrides[0]))

static bool KnownOffset(DWORD off)
{
    return off == OFF_STORAGES || off == OFF_WIRES || off == OFF_OCCUPANTS ||
           off == OFF_WALKING  || off == OFF_PARKING || off == OFF_CATCHMENT;
}

// One triple, one stride, one element-pointer position. Returns the member
// count, or -1.
static int TryCandidate(BYTE* b, DWORD off, int stride, int ptrOff,
                        BYTE** bvBegin, int bvCount, int* growable)
{
    if (!ReadablePtr(b + off, 24)) return -1;

    BYTE* begin = *(BYTE**)(b + off);
    BYTE* end   = *(BYTE**)(b + off + 8);
    BYTE* cap   = *(BYTE**)(b + off + 16);

    // I1 - the shape.
    if (!begin || end <= begin || cap < end) return -1;

    size_t span = (size_t)(end - begin);
    if (span % (size_t)stride) return -1;

    size_t n = span / (size_t)stride;
    if (n == 0 || n > MAX_ELEMS) return -1;
    if (!ReadablePtr(begin, span)) return -1;

    // I2 and I3 - every member is a building the game knows about, and every
    // one of them is a live construction site. The cheap filter runs first so
    // the linear membership scan is only ever reached by a handful.
    for (size_t i = 0; i < n; i++)
    {
        BYTE* m = *(BYTE**)(begin + i * (size_t)stride + ptrOff);
        if (!m || !LooksLikeBuilding(m)) return -1;
        if (!IsUnfinished(m))            return -1;
    }
    for (size_t i = 0; i < n; i++)
    {
        BYTE* m = *(BYTE**)(begin + i * (size_t)stride + ptrOff);
        if (!InBuildingVector(m, bvBegin, bvCount)) return -1;
    }

    // Whether raising a cap would be a feature or a heap overrun. A real
    // vector has room past `end`; a fixed inline array does not.
    *growable = cap > end ? 1 : 0;
    return (int)n;
}

static int ScanOffice(BYTE* b, DWORD extent, BYTE** bvBegin, int bvCount,
                      Candidate* out, int max)
{
    int found = 0;
    DWORD to = extent < (DWORD)g_probeTo ? extent : (DWORD)g_probeTo;

    for (DWORD off = (DWORD)g_probeFrom; off + 24 <= to && found < max; off += 8)
    {
        if (KnownOffset(off)) continue;                       // I5
        if (!ReadablePtr(b + off, 24)) continue;

        for (size_t s = 0; s < STRIDE_COUNT && found < max; s++)
        {
            for (int ptrOff = 0; ptrOff <= 8 && found < max; ptrOff += 8)
            {
                if (ptrOff + 8 > kStrides[s]) continue;

                int grow = 0;
                int n = TryCandidate(b, off, kStrides[s], ptrOff,
                                     bvBegin, bvCount, &grow);
                if (n < 0) continue;

                out[found].off      = off;
                out[found].stride   = kStrides[s];
                out[found].ptrOff   = ptrOff;
                out[found].count    = n;
                out[found].growable = grow;
                found++;
            }
        }
    }
    return found;
}

// ---------------------------------------------------------------- writing it
//
// The range is a plain int on the office, so the first thing to try is simply
// setting it - no spliced code, no repointed constant, nothing that a game
// update can move. `cities` does the same kind of thing for a city's radius.
//
// Whether it STICKS is the open question, and this is also the experiment that
// answers it: the value is written, read back on the next pass, and any
// disagreement is reported. If the simulation clamps it to 3500 the log says
// so in as many words, and the answer is then a code patch on whatever does
// the clamping.
//
// This is the one place in the plugin that writes to the game. It is off
// unless `write_range` says otherwise, and it refuses an office whose current
// value is not a plausible range - if 0xFC8 ever stops being this field, that
// guard is what stops the plugin writing into a stranger's structure.

static int  g_wroteOnce;
static int  g_clampReported;
static int  g_refusedReported;

static void WriteRanges(void)
{
    if (!g_writeRange || g_officeRange <= 0) return;

    int wrote = 0, clamped = 0, refused = 0;

    for (int i = 0; i < g_offices; i++)
    {
        Office* o = &g_office[i];
        BYTE*   b = o->building;
        if (!b) continue;

        // Decided once, and never reconsidered - but ONLY in default mode.
        //
        // This is the fix for a bug that was mine: the rule used to be "raise
        // anything sitting at the game's default", and the minus button's own
        // bottom rung IS that default. So stepping an office down to 1000 by
        // hand looked identical to a freshly built one, and the plugin put it
        // straight back to 10000 - which made the minus button cycle 3500,
        // 3000, 2000, 10000 for ever. "Have we already made a decision about
        // this office" is the honest question, and the value alone could never
        // answer it.
        //
        // With `game_default = 0` there is no such question to ask: the
        // documented behaviour there is that every office is HELD at
        // office_range on every frame, so the flag must not short-circuit it.
        // It used to, unconditionally, which quietly made game_default = 0
        // behave identically to the default mode - and took the re-clamp
        // detection below down with it, since a raised office never reached it.
        if (g_gameDefault > 0 && o->raised) continue;

        // Still an office, and still readable. The building vector is walked
        // fresh every report, but this runs between reports too.
        int t = BuildingType(b);
        if (t != BT_OFFICE && t != BT_OFFICE_RAIL) continue;
        if (!ReadablePtr(b + B_OFFICE_RANGE, 4)) continue;

        int now = *(const int*)(b + B_OFFICE_RANGE);
        if (now == g_officeRange) continue;             // already ours

        // The invariant that keeps a wrong offset from becoming a wild write:
        // whatever is here has to look like a range in metres. The game's own
        // values run to 3500 and the plugin's own ceiling is RANGE_MAX.
        if (now < 100 || now > RANGE_MAX)
        {
            refused++;
            continue;
        }

        // A DEFAULT, NOT A CAGE. An office already carrying something other
        // than the game's starting value was set by the player - on this map
        // or in an earlier session, since the value is saved - so it is theirs.
        // Mark it decided and never look at it again.
        if (g_gameDefault > 0 && now != g_gameDefault)
        {
            o->raised = 1;
            continue;
        }

        // Holding mode only: anything other than the value we last put here is
        // the game putting it back. Reachable now that `raised` no longer skips
        // the office on every pass after the first.
        if (g_wroteOnce && g_gameDefault <= 0) clamped++;

        *(int*)(b + B_OFFICE_RANGE) = g_officeRange;
        o->raised = 1;
        wrote++;
    }

    // In default mode this is one line per office raised, which is once each:
    // the next pass sees our own value and skips it, so a newly built office
    // announces itself and nothing else does.
    //
    // In holding mode the write happens on EVERY frame the player has moved the
    // value, so the same line would run at frame rate. It is said once - the
    // behaviour does not change, and one line describes all of it.
    if (wrote)
    {
        if (g_gameDefault > 0)
            Logf("construct  range: %d office(s) at the game's default of %d raised to "
                 "%d m - lower any of them in its own window and it stays lowered",
                 wrote, g_gameDefault, g_officeRange);
        else if (!g_wroteOnce)
            Logf("construct  range: holding %d office(s) at %d m - `game_default = 0`, "
                 "so the minus button in the office window cannot lower them",
                 wrote, g_officeRange);
        g_wroteOnce = 1;
    }

    // Rate-limited for the same reason: a wrong offset is wrong on every frame,
    // and one line says so as well as ten thousand would.
    if (refused && !g_refusedReported)
    {
        Logf("construct  range: %d office(s) refused - +0x%X did not hold a plausible "
             "range (100..%d), so it is not being written",
             refused, B_OFFICE_RANGE, RANGE_MAX);
        g_refusedReported = 1;
    }

    // Holding mode only, and reported once rather than every frame: whatever is
    // moving the field is moving it constantly, and one line says everything.
    //
    // It does NOT say "the game re-clamped it" any more, because in this mode it
    // cannot tell that from the player pressing the minus button - both show up
    // as a value that is not ours, on the very next frame. Which one it is, is
    // exactly what the reader knows and this code does not.
    if (clamped && !g_clampReported)
    {
        Logf("construct  range: %d office(s) had a value other than %d and were put "
             "back - `game_default = 0`, so every office is held on every frame. "
             "If you did not change it by hand, the game is re-clamping the field "
             "and setting it is not enough.", clamped, g_officeRange);
        g_clampReported = 1;
    }
}

// ---------------------------------------------------------------- the hunt
//
// The office window shows the auto-search range outright - "<3,500m", with a
// minus and a plus - so the range is a per-office number whose current value
// is known before the search begins. That is a far better starting point than
// the list scan below: instead of inferring a limit from which buildings got
// picked up, look for the number itself.
//
// Reported as int and as float because either is plausible: the UI prints
// whole metres, and this game stores distances both ways (walking's 480 is a
// float in .rdata, its twin is an imm32).

static int FindValue(BYTE* obj, DWORD extent, int target, const char* what)
{
    int hits = 0;
    for (DWORD off = 0; off + 4 <= extent && hits < 32; off += 4)
    {
        int   i = *(const int*)(obj + off);
        float f = *(const float*)(obj + off);

        if (i == target)
        {
            Logf("construct      %s +0x%04X = %d (int)", what, off, i);
            hits++;
        }
        else if (f == (float)target)
        {
            Logf("construct      %s +0x%04X = %.1f (float)", what, off, f);
            hits++;
        }
    }
    return hits;
}

// Every slot that moved since the last report. The confirmation step: click
// the + or the - in the office window and exactly one of the offsets FindValue
// listed should appear here, going 3500 -> 3400 or whatever the step is.
static void DiffOffice(Office* o, DWORD extent)
{
    DWORD to = extent < OFFICE_SNAP ? extent : OFFICE_SNAP;

    if (!o->haveSnap)
    {
        memcpy(o->snap, o->building, to);
        o->snapLen  = to;
        o->haveSnap = 1;
        return;
    }

    // Only compare what the PREVIOUS snapshot actually covered. A readability
    // bound can grow between reports, and the bytes past the old bound are
    // still zero - every one of which would be printed as a slot that "moved".
    if (to > o->snapLen) to = o->snapLen;

    int printed = 0;
    for (DWORD off = (DWORD)g_probeFrom; off + 4 <= to && printed < 24; off += 4)
    {
        int iNow = *(const int*)(o->building + off);
        int iWas = *(const int*)(o->snap + off);
        if (iNow == iWas) continue;

        float fNow = *(const float*)(o->building + off);
        float fWas = *(const float*)(o->snap + off);

        // A pointer that moved decodes as a wild float and a huge int, and
        // there are a lot of those. Anything that could be a distance in
        // metres is what we are here for.
        bool plausible = (iNow > 0 && iNow < 100000) ||
                         (fNow == fNow && fNow > 0.0f && fNow < 100000.0f);
        if (!plausible) continue;

        Logf("construct      moved +0x%04X  int %d -> %d   float %.2f -> %.2f",
             off, iWas, iNow, fWas, fNow);
        printed++;
    }
    if (!printed)
        Logf("construct      nothing that looks like a distance moved");

    // Re-snapshot the whole of what is readable NOW, not the clamped compare
    // window - otherwise a bound that grew once could never be caught up with.
    DWORD fresh = extent < OFFICE_SNAP ? extent : OFFICE_SNAP;
    memcpy(o->snap, o->building, fresh);
    o->snapLen = fresh;
}

// ---------------------------------------------------------------- the probe

static void TypeHistogram(BYTE** begin, int count)
{
    int hist[MAX_TYPE];
    memset(hist, 0, sizeof(hist));

    int unknown = 0;
    for (int i = 0; i < count; i++)
    {
        int t = BuildingType(begin[i]);
        if (t >= 0 && t < MAX_TYPE) hist[t]++;
        else unknown++;
    }

    // _snprintf_s returns -1 when _TRUNCATE truncates, so the running offset has
    // to be checked rather than accumulated blind: `o += -1` walks backwards and
    // the next write lands before the start of the buffer.
    char line[1024];
    int  o = 0;
    for (int t = 0; t < MAX_TYPE && o < (int)sizeof(line) - 24; t++)
        if (hist[t])
        {
            int k = _snprintf_s(line + o, sizeof(line) - o, _TRUNCATE,
                                "%d x%d  ", t, hist[t]);
            if (k < 0) break;
            o += k;
        }
    line[o] = 0;

    Logf("construct  %d building(s), %d with no readable type", count, unknown);
    Logf("construct  types on this map: %s", line);

    // The line that decides whether anything below is worth reading. If 12 and
    // 28 are absent, building+0x360 is not the type on this build and every
    // later line is noise - docs/02-findings.md records that a previous claim
    // about that very offset was a guess and was wrong.
    if (!hist[BT_OFFICE] && !hist[BT_OFFICE_RAIL])
        Logf("construct  WARN no type %d or %d on this map - either there are no "
             "construction offices built, or +0x%X is not the type field",
             BT_OFFICE, BT_OFFICE_RAIL, T_BUILDING_TYPE);
}

static int g_overflowReported;

static int CollectOffices(BYTE** begin, int count)
{
    int n = 0, total = 0;
    for (int i = 0; i < count; i++)
    {
        BYTE* b = begin[i];
        int   t = BuildingType(b);
        if (t != BT_OFFICE && t != BT_OFFICE_RAIL) continue;

        // Counted before the cap so the overflow below is the real number, not
        // MAX_OFFICES. A map with more offices than fit used to lose the rest
        // in silence - including for WriteRanges, so those offices simply never
        // got their range set and nothing said why.
        total++;
        if (n >= MAX_OFFICES)
        {
            // Once the overflow has been said, there is nothing left to learn
            // from counting the rest - and BuildingType is two VirtualQuery
            // calls per building on a vector that can hold thousands.
            if (g_overflowReported) break;
            continue;
        }

        // Carry forward what we already learned about this office rather than
        // starting over: the accepted offset and the previous count are the
        // whole of the temporal evidence.
        Office keep;
        memset(&keep, 0, sizeof(keep));
        for (int j = 0; j < g_offices; j++)
            if (g_office[j].building == b) { keep = g_office[j]; break; }

        keep.building = b;
        keep.type     = t;
        g_office[n++] = keep;
    }

    if (total > MAX_OFFICES && !g_overflowReported)
    {
        Logf("construct  WARN this map has %d construction offices and only %d fit - "
             "the other %d are not probed and their range is NOT set. Raise "
             "MAX_OFFICES and rebuild if you need them all.",
             total, MAX_OFFICES, total - MAX_OFFICES);
        g_overflowReported = 1;
    }
    return n;
}

// Everything about one office, once per report.
static void ReportOffice(Office* o, BYTE** bvBegin, int bvCount, int* claimedBy)
{
    BYTE* b = o->building;

    DWORD extent = ReadableExtent(b, (DWORD)g_probeTo);
    float centre[3] = { 0, 0, 0 };
    bool  havePos = BuildingCentre(b, centre);

    Logf("construct  office %p  type %d  %s  readable to +0x%X",
         b, o->type,
         havePos ? "placed" : "position unknown",
         extent);

    // The range first, because the office's own window tells us what it is.
    // Once per office: the value does not move unless the player moves it, and
    // the diff below is what catches that.
    if (g_probeExpect > 0 && !o->hunted)
    {
        Logf("construct    hunting %d in this office", g_probeExpect);
        int hits = FindValue(b, extent, g_probeExpect, "office");

        // extent can stop short of B_TYPEDESC, so the slot holding the type
        // record has to be checked before it is read, not just the record.
        BYTE* td = ReadablePtr(b + B_TYPEDESC, 8) ? *(BYTE**)(b + B_TYPEDESC) : NULL;
        if (td && ReadablePtr(td, 0x900))
            hits += FindValue(td, 0x900, g_probeExpect, "type  ");

        if (!hits)
            Logf("construct    %d is nowhere in this office - is that the number the "
                 "window shows? Set probe_expect to what it says.", g_probeExpect);
        else
            Logf("construct    %d candidate(s). Now click - or + on the office's "
                 "auto-search range and watch which one moves.", hits);
        o->hunted = 1;
    }

    if (g_probeDiff) DiffOffice(o, extent);

    // --- the candidate scan, until it settles -----------------------------
    if (!o->listOff)
    {
        Candidate cand[16];
        int n = ScanOffice(b, extent, bvBegin, bvCount, cand, 16);

        for (int i = 0; i < n; i++)
            Logf("construct    candidate +0x%04X  stride 0x%X  ptr+%d  %d member(s)  %s",
                 cand[i].off, cand[i].stride, cand[i].ptrOff, cand[i].count,
                 cand[i].growable ? "growable" : "FIXED ARRAY - a raised cap would overrun");

        if (n == 0)
        {
            Logf("construct    no candidate list on this office - it may simply have "
                 "taken nothing on yet; place a construction site near it");
        }
        else if (n == 1)
        {
            // One candidate, and it has to survive a second sample before it
            // is believed: we read this from the render thread and a vector
            // caught mid-push_back is not evidence of anything.
            if (o->pendOff == cand[0].off && o->pendStride == cand[0].stride &&
                o->pendPtrOff == cand[0].ptrOff)
            {
                o->listOff    = cand[0].off;
                o->stride     = cand[0].stride;
                o->elemPtrOff = cand[0].ptrOff;
                o->growable   = cand[0].growable;
                Logf("construct    ACCEPTED +0x%04X after two consecutive samples - "
                     "open this office's window and check it lists %d site(s)",
                     o->listOff, cand[0].count);
            }
            else
            {
                o->pendOff    = cand[0].off;
                o->pendStride = cand[0].stride;
                o->pendPtrOff = cand[0].ptrOff;
                Logf("construct    +0x%04X held once - waiting for a second sample",
                     cand[0].off);
            }
        }
        else
        {
            Logf("construct    %d candidates survived every invariant - place or "
                 "cancel a site and take whichever one moves", n);
        }
        if (!o->listOff) return;
    }

    // --- the numbers that answer the question -----------------------------
    int   grow  = 0;
    int   held  = TryCandidate(b, o->listOff, o->stride, o->elemPtrOff,
                               bvBegin, bvCount, &grow);
    if (held < 0)
    {
        // The list stopped looking like one. That is information: either the
        // office dropped everything, or the offset was wrong all along.
        Logf("construct    +0x%04X no longer validates - dropping it and rescanning",
             o->listOff);
        o->listOff = 0;
        o->pendOff = 0;
        return;
    }

    o->heldPrev = o->held;
    o->held     = held;
    o->growable = grow;
    if (held > o->maxHeld) o->maxHeld = held;

    // Claimed members, with distances, and whether the car list explains them.
    BYTE* begin = *(BYTE**)(b + o->listOff);
    float claimedMax = -1.0f;
    float claimedMin = -1.0f;
    int   parkingAll = 1;

    for (int i = 0; i < held; i++)
    {
        BYTE* m = *(BYTE**)(begin + (size_t)i * (size_t)o->stride + o->elemPtrOff);
        for (int j = 0; j < bvCount; j++)
            if (bvBegin[j] == m) { claimedBy[j] = 1; break; }

        float mc[3];
        float d = -1.0f;
        if (havePos && BuildingCentre(m, mc)) d = Distance(centre, mc);
        if (d >= 0.0f)
        {
            if (d > claimedMax) claimedMax = d;
            if (claimedMin < 0.0f || d < claimedMin) claimedMin = d;
        }

        bool park = InParkingList(b, m);
        if (!park) parkingAll = 0;

        if (i < g_probeSites)
            Logf("construct      site %2d  %p  type %3d  %7.1f m  car list %s  progress %.2f",
                 i, m, BuildingType(m), d, park ? "yes" : "no",
                 ReadablePtr(m + B_FINISHED, 4) ? *(const float*)(m + B_FINISHED) : -1.0f);
    }

    o->claimedMax = claimedMax;
    o->claimedMin = claimedMin;

    Logf("construct    holds %d site(s)  from %.1f m to %.1f m  %s  %s",
         held, claimedMin, claimedMax,
         o->growable ? "growable" : "FIXED ARRAY",
         parkingAll && held ? "every one is in the car list" : "not all in the car list");
}

static void Report(BYTE* game, BYTE** bvBegin, int bvCount)
{
    int day  = ReadablePtr(game + G_DAY, 8) ? *(const int*)(game + G_DAY)  : -1;
    int year = ReadablePtr(game + G_DAY, 8) ? *(const int*)(game + G_YEAR) : -1;

    static int   claimedBy[8192];
    static int   liveIdx[MAX_LIVE];
    static float liveCentre[MAX_LIVE][3];
    static int   liveHasPos[MAX_LIVE];

    int track = bvCount < 8192 ? bvCount : 8192;
    memset(claimedBy, 0, sizeof(int) * (size_t)track);

    // Every live construction site on the map, gathered in ONE pass, with its
    // position taken once while we are here.
    //
    // IsUnfinished is two VirtualQuery calls and BuildingCentre is an engine
    // call. An earlier version of this ran both over the whole building vector
    // for the header, again for the unclaimed count, and again per office -
    // twenty thousand syscalls in a single frame on a decent city, every
    // report, on the render thread. docs/07-pitfalls.md already records that
    // shape once, as "A failed lookup once a frame is a frame-rate leak"; this
    // is the same mistake one step out, and it is cheaper to not make it than
    // to have it reported as the plugin making the game stutter.
    int live = 0;
    for (int i = 0; i < track && live < MAX_LIVE; i++)
    {
        BYTE* b = bvBegin[i];
        if (!b || !IsUnfinished(b)) continue;

        liveIdx[live]    = i;
        liveHasPos[live] = BuildingCentre(b, liveCentre[live]) ? 1 : 0;
        live++;
    }

    Logf("construct  ---- day %d of year %d: %d office(s), %d live site(s) ----",
         day, year, g_offices, live);

    for (int i = 0; i < g_offices; i++)
        ReportOffice(&g_office[i], bvBegin, track, claimedBy);

    // Unclaimed = a live site in nobody's list. Without this number nothing
    // below means anything: an office that stopped taking work on because
    // there was none left has demonstrated no limit of any kind.
    int unclaimed = 0;
    for (int k = 0; k < live; k++)
        if (!claimedBy[liveIdx[k]]) unclaimed++;

    Logf("construct  %d live site(s), %d of them unclaimed", live, unclaimed);

    for (int i = 0; i < g_offices; i++)
    {
        Office* o = &g_office[i];
        if (!o->listOff) continue;

        // The nearest thing THIS office refused. Per office, because two
        // offices in different towns have completely different neighbourhoods
        // and averaging them would erase the boundary we are looking for.
        float oc[3];
        o->unclaimedMin = -1.0f;
        if (BuildingCentre(o->building, oc))
        {
            for (int k = 0; k < live; k++)
            {
                if (claimedBy[liveIdx[k]] || !liveHasPos[k]) continue;
                float d = Distance(oc, liveCentre[k]);
                if (o->unclaimedMin < 0.0f || d < o->unclaimedMin) o->unclaimedMin = d;
            }
        }

        if (!unclaimed)
        {
            Logf("construct  office %p: holds everything there is - place more sites, "
                 "further out, before this can show a range", o->building);
            continue;
        }

        Logf("construct  office %p: claimed out to %.1f m, nearest refused %.1f m",
             o->building, o->claimedMax, o->unclaimedMin);

        // --- THE RANGE, which is the whole question -----------------------
        //
        // A refused site NEARER than one already taken on means distance is
        // not what is being filtered on. A clean gap the other way brackets
        // the range between the two numbers, and that bracket is what the
        // .rdata hunt below is given.
        if (o->held > 0 && o->unclaimedMin >= 0.0f && o->claimedMax >= 0.0f)
        {
            if (o->unclaimedMin < o->claimedMax)
            {
                Logf("construct  office %p: a refused site at %.1f m sits INSIDE a taken "
                     "one at %.1f m - distance is not the gate here, a count is",
                     o->building, o->unclaimedMin, o->claimedMax);
            }
            else if (!o->scanned)
            {
                float lo = o->claimedMax;
                float hi = o->unclaimedMin;
                Logf("construct  office %p: RANGE IS BETWEEN %.1f m AND %.1f m - hunting "
                     "for it", o->building, lo, hi);

                // Widened a little at both ends: the game measures a path along
                // roads and this measures a straight line, so the constant is
                // at or above the straight-line bracket rather than inside it.
                float wlo = lo * 0.9f;
                float whi = hi * 2.0f;

                int hits = 0;
                DWORD extent = ReadableExtent(o->building, (DWORD)g_probeTo);
                hits += ScanFloatRange(o->building, 0, extent, wlo, whi, "office ", 12);

                BYTE* td = ReadablePtr(o->building + B_TYPEDESC, 8)
                         ? *(BYTE**)(o->building + B_TYPEDESC) : NULL;
                if (td) hits += ScanFloatRange(td, 0, 0x900, wlo, whi, "type   ", 12);

                hits += ScanFloatRange(game, 0, 0x14000, wlo, whi, "game   ", 12);

                // The likeliest home by far. building.ini declares no range and
                // every office behaves alike, so it is a global - and a global
                // float in this executable lives in the shared literal pool.
                hits += ScanFloatRange(g_exeBase, (DWORD)g_rdataFrom, (DWORD)g_rdataTo,
                                       wlo, whi, ".rdata ", 24);

                Logf("construct  office %p: %d candidate(s) in %.0f..%.0f. The .rdata "
                     "ones are the ones to try first - repoint the read, never "
                     "overwrite the constant.", o->building, hits, wlo, whi);
                o->scanned = 1;
            }
        }

        // --- and the count, which is the other thing it could be ----------
        if (o->held == o->heldPrev)
        {
            o->frozen++;
            if (o->frozen == 3)
                Logf("construct  office %p: held %d over 3 report(s) with %d site(s) "
                     "unclaimed - %d may also be a count cap",
                     o->building, o->held, unclaimed, o->held);
        }
        else
        {
            o->frozen  = 0;
            o->scanned = 0;      // the bracket moved; hunt again when it settles
        }
    }

    g_reports++;
}

static void Tick(void)
{
    BYTE* game = GameObject();

    BYTE** bvBegin = NULL;
    int    bvCount = Buildings(game, &bvBegin);
    if (bvCount <= 0) return;                   // no world loaded yet

    // Every frame, and deliberately not once per report: if the game re-clamps
    // this field then holding it only every five seconds would leave it at the
    // game's own value almost all the time, and the auto-search would hardly
    // ever run with ours in place. It works off the office list the last report
    // gathered, so it costs one type check and one compare per office.
    WriteRanges();

    DWORD now = GetTickCount();
    if (g_armed && now - g_lastReport < (DWORD)g_probePeriod * 1000) return;
    g_lastReport = now;

    g_offices = CollectOffices(bvBegin, bvCount);

    // After collecting, not before. The first version printed the histogram on
    // the first tick of the session - which on a fresh map is before any
    // office exists - so it warned that type 12 was absent and, being a
    // once-only line, never corrected itself when one was built.
    if (!g_typedOnce && g_offices)
    {
        TypeHistogram(bvBegin, bvCount);
        g_typedOnce = true;
    }

    if (!g_offices)
    {
        if (!g_armed)
            Logf("construct  no construction office on the map yet - build one");
        g_armed = true;
        return;
    }

    g_armed = true;
    Report(game, bvBegin, bvCount);
}

// ---------------------------------------------------------------- patching
//
// Nothing here runs while the site table is empty. It is written now rather
// than later so that the shape the probe is aiming at is on the record: what
// the probe has to produce is an rva, an opcode, and the value that must be
// sitting at the address the displacement resolves to.

static bool WriteCode(void* at, const void* bytes, size_t n, const char* what)
{
    DWORD prot = 0;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &prot))
    {
        Logf("construct  VirtualProtect failed at %p (%s)", at, what);
        return false;
    }
    memcpy(at, bytes, n);
    VirtualProtect(at, n, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

// The four checks walking.cpp's Repoint does, with the write removed. Split
// apart on purpose: a group is verified whole before a single byte of it is
// written, because a cap the simulation honours while the office window still
// draws the old one is worse than neither, and is the bug plugins/walking took
// four versions to stop shipping.
static bool CheckSite(const Site* s, const float* slot)
{
    if (!s->rva) return false;

    BYTE* site   = g_exeBase + s->rva;
    int   insLen = s->opLen + 4;

    if (!ReadablePtr(site, (size_t)insLen) ||
        (s->op && memcmp(site, s->op, (size_t)s->opLen) != 0))
    {
        Logf("construct  %s: rva 0x%X is not the expected instruction - refusing",
             s->label, s->rva);
        return false;
    }

    if (s->kind == SITE_IMM32)
    {
        double now = (double)*(const float*)(site + s->opLen);
        if (now != s->vanilla)
        {
            Logf("construct  %s: rva 0x%X holds %.6g, expected %.6g - refusing",
                 s->label, s->rva, now, s->vanilla);
            return false;
        }
        return true;
    }

    DWORD resolved = (DWORD)(s->rva + insLen + *(const int*)(site + s->opLen));
    if (resolved != s->constRva)
    {
        Logf("construct  %s: rva 0x%X reads 0x%X, expected 0x%X - refusing",
             s->label, s->rva, resolved, s->constRva);
        return false;
    }

    double now = (s->kind == SITE_RIP_INT32)
               ? (double)*(const int*)(g_exeBase + s->constRva)
               : (double)*(const float*)(g_exeBase + s->constRva);
    if (now != s->vanilla)
    {
        Logf("construct  %s: 0x%X holds %.6g, expected %.6g - refusing",
             s->label, s->constRva, now, s->vanilla);
        return false;
    }

    __int64 rel = (BYTE*)slot - (site + insLen);
    if (rel < (__int64)INT_MIN || rel > (__int64)INT_MAX)
    {
        Logf("construct  %s: slot %p is out of rel32 range of %p - refusing",
             s->label, slot, site);
        return false;
    }
    return true;
}

static bool WriteSite(const Site* s, float* slot, double value)
{
    BYTE* site   = g_exeBase + s->rva;
    int   insLen = s->opLen + 4;

    if (s->kind == SITE_IMM32)
    {
        float v = (float)value;
        if (!WriteCode(site + s->opLen, &v, sizeof(v), s->label)) return false;
        Logf("construct  %-20s %.6g -> %.6g  (rva 0x%X, immediate)",
             s->label, s->vanilla, value, s->rva);
        return true;
    }

    if (s->kind == SITE_RIP_INT32) *(int*)slot   = (int)value;
    else                           *(float*)slot = (float)value;

    int disp = (int)((BYTE*)slot - (site + insLen));
    if (!WriteCode(site + s->opLen, &disp, sizeof(disp), s->label)) return false;

    Logf("construct  %-20s %.6g -> %.6g  (rva 0x%X now reads %p)",
         s->label, s->vanilla, value, s->rva, slot);
    return true;
}

// All seven or none - four validator sites and the three rungs the buttons
// actually walk. Each site individually is a short byte run that could in
// principle occur elsewhere; what makes the set trustworthy is that four of
// the seven carry the 0xFC8 displacement and all of them sit at fixed
// addresses in this build. Verifying them together and writing only on a clean
// sweep is also what stops the half-patched state where the validator honours
// one ceiling and the button ladder stops at another.
//
// The write loop is deliberately after the whole verify loop. A VirtualProtect
// that fails partway still leaves earlier sites written, which is why the
// failure is logged loudly rather than returned quietly.
static bool PatchClamp(void)
{
    for (int i = 0; i < (int)CLAMP_SITE_COUNT; i++)
    {
        const ClampSite* s = &kClampSites[i];
        BYTE* at = g_exeBase + s->rva;

        if (!ReadablePtr(at, (size_t)s->len))
        {
            Logf("construct  ceiling: rva 0x%X is not readable - refusing", s->rva);
            return false;
        }
        if (memcmp(at, s->expect, (size_t)s->len) != 0)
        {
            char hex[96]; int o = 0;
            for (int k = 0; k < s->len && o < (int)sizeof(hex) - 4; k++)
            {
                int w = _snprintf_s(hex + o, sizeof(hex) - o, _TRUNCATE, "%02X ", at[k]);
                if (w < 0) break;              // _TRUNCATE returns -1, never add it
                o += w;
            }
            hex[o] = 0;
            Logf("construct  ceiling: %s at rva 0x%X does not match this build - refusing",
                 s->label, s->rva);
            Logf("construct           found: %s", hex);
            return false;
        }
    }

    for (int i = 0; i < (int)CLAMP_SITE_COUNT; i++)
    {
        const ClampSite* s = &kClampSites[i];
        BYTE* imm = g_exeBase + s->rva + s->immOff;
        if (!WriteCode(imm, &g_ceiling, sizeof(g_ceiling), s->label))
        {
            // Every site verified, so this is a VirtualProtect failure and not
            // a wrong build - and the ones before it are already written. Say
            // so outright: a half-raised ceiling is a state worth knowing about
            // rather than a quiet "did not patch".
            Logf("construct  ceiling: FAILED writing \"%s\" (site %d of %d) - %d "
                 "earlier site(s) ARE already raised to %d. The office window may "
                 "now disagree with itself; restart the game to undo it.",
                 s->label, i + 1, (int)CLAMP_SITE_COUNT, i, g_ceiling);
            return false;
        }
    }

    Logf("construct  ceiling: %d of %d site(s) raised from %d to %d - the office "
         "window's + button now goes that far",
         (int)CLAMP_SITE_COUNT, (int)CLAMP_SITE_COUNT, VANILLA_RANGE_MAX, g_ceiling);
    return true;
}

static int PatchGroup(const Group* g, double value)
{
    for (int i = 0; i < g->count; i++)
        if (!g->sites[i].rva)
        {
            Logf("construct  %s: no address for \"%s\" yet - run with probe = 1",
                 g->label, g->sites[i].label);
            return 0;
        }

    for (int i = 0; i < g->count; i++)
        if (!CheckSite(&g->sites[i], &g_slot[g->sites[i].slot]))
        {
            Logf("construct  %s: %d site(s) did not verify - nothing written",
                 g->label, g->count);
            return 0;
        }

    int done = 0;
    for (int i = 0; i < g->count; i++)
    {
        g->ok[i] = WriteSite(&g->sites[i], &g_slot[g->sites[i].slot], value) ? 1 : 0;
        done += g->ok[i];
    }
    Logf("construct  %s: %d of %d site(s) written", g->label, done, g->count);
    return done;
}

// ---------------------------------------------------------------- the hook

static void h_TerrainRender(void* self, bool a, void* cam1, void* cam2, int b, int c)
{
    o_TerrainRender(self, a, cam1, cam2, b, c);

    if (!g_probe && !g_writeRange) return;
    __try { Tick(); }
    __except (FaultFilter("construction probe", GetExceptionInformation()))
    {
        Logf("construct  disabled after a fault - the range is left as the game had it");
        g_probe      = 0;
        g_writeRange = 0;
    }
}

// ---------------------------------------------------------------- setup

static void ReadSettings(void)
{
    const char* ini = "plugins\\construction.ini";

    g_enabled     = H->configInt(ini, "construction", "enabled",      g_enabled);
    g_patch       = H->configInt(ini, "construction", "patch",        g_patch);
    g_probe       = H->configInt(ini, "construction", "probe",        g_probe);
    g_officeRange = H->configInt(ini, "construction", "office_range", g_officeRange);
    g_officeLimit = H->configInt(ini, "construction", "office_limit", g_officeLimit);

    g_probeFrom   = H->configInt(ini, "construction", "probe_from",   g_probeFrom);
    g_probeTo     = H->configInt(ini, "construction", "probe_to",     g_probeTo);
    g_probePeriod = H->configInt(ini, "construction", "probe_period", g_probePeriod);
    g_probeSites  = H->configInt(ini, "construction", "probe_sites",  g_probeSites);
    g_probeExpect = H->configInt(ini, "construction", "probe_expect", g_probeExpect);
    g_probeDiff   = H->configInt(ini, "construction", "probe_diff",   g_probeDiff);
    g_writeRange  = H->configInt(ini, "construction", "write_range",  g_writeRange);
    g_gameDefault = H->configInt(ini, "construction", "game_default", g_gameDefault);
    g_raiseCeiling = H->configInt(ini, "construction", "raise_ceiling", g_raiseCeiling);
    g_ceiling      = H->configInt(ini, "construction", "ceiling",       g_ceiling);
    g_rdataFrom   = H->configInt(ini, "construction", "rdata_from",   g_rdataFrom);
    g_rdataTo     = H->configInt(ini, "construction", "rdata_to",     g_rdataTo);

    if (g_probeFrom < 0)          g_probeFrom = 0;
    if (g_probeTo <= g_probeFrom) g_probeTo = g_probeFrom + 0x800;
    if (g_probeTo > 0x8000)       g_probeTo = 0x8000;
    if (g_probePeriod < 1)        g_probePeriod = 1;
    if (g_probeSites < 0)         g_probeSites = 0;
    if (g_officeLimit < 0)        g_officeLimit = 0;
    // Below 3500 the "ceiling" would be a restriction rather than a lift.
    // Above RANGE_MAX it would be a trap: the player could wind an office up
    // past the value WriteRanges accepts as a plausible range, and the plugin
    // would then start refusing that office as if +0xFC8 had stopped being the
    // field. One bound for both, so that cannot happen.
    if (g_ceiling < VANILLA_RANGE_MAX) g_ceiling = VANILLA_RANGE_MAX;
    if (g_ceiling > RANGE_MAX)
    {
        Logf("construct  ceiling %d is past the %d limit - clamped",
             g_ceiling, RANGE_MAX);
        g_ceiling = RANGE_MAX;
    }

    if (g_officeRange < 0)        g_officeRange = 0;
    if (g_officeRange > RANGE_MAX)
    {
        Logf("construct  office_range %d is past the %d ceiling - clamped",
             g_officeRange, RANGE_MAX);
        g_officeRange = RANGE_MAX;
    }
    if (g_rdataFrom < 0)          g_rdataFrom = 0;
    if (g_rdataTo <= g_rdataFrom) g_rdataTo = g_rdataFrom + 0x1000;
}

extern "C" __declspec(dllexport) unsigned TsmPluginApiVersion(void)
{
    return TSM_API_VERSION;
}

extern "C" __declspec(dllexport) int TsmPluginInit(const TsmHost* host, TsmPluginInfo* info)
{
    TsmBind(host);
    info->name    = "construction";
    info->version = "1.0";

    ReadSettings();
    if (!g_enabled)
    {
        Logf("construct  disabled");
        return 1;
    }
    return 0;
}

extern "C" __declspec(dllexport) int TsmPluginStart(void)
{
    if (!g_enabled) return 1;

    if (!VerifyGameObject())
    {
        Logf("construct  the game object is not where this build puts it - inactive");
        return 1;
    }
    Logf("construct  game object 0x%X confirmed by the lea at 0x%X",
         P_GAMEOBJ, RVA_GAMEOBJ_LEA);

    // The render hook drives BOTH halves - the probe's reports and, through
    // Tick, WriteRanges - so it is installed whenever either of them is wanted.
    //
    // It used to hang off `g_probe` alone, which made `probe = 0` with
    // `write_range = 1` a silent no-op: the hook was never installed, Tick was
    // never called, and the plugin still logged "ready". construction.ini tells
    // the reader to set probe = 0 once the range is known - and it is known -
    // so that was the configuration the documentation actually recommended.
    bool armed = false;
    if (g_probe || g_writeRange)
    {
        // An import swap, so it chains with depletion's and aging's hooks on
        // the same function rather than fighting them, and needs no address of
        // its own. The building dispatcher is not available: accumulator owns
        // an inline hook on it.
        if (PatchIat(g_exe, DLL_ENGINE, SYM_TERRAIN_RENDER, (void*)h_TerrainRender,
                     (void**)&o_TerrainRender, "C3D_TERRAIN::Render"))
            armed = true;
        else
            Logf("construct  no import slot for C3D_TERRAIN::Render - neither the "
                 "probe nor write_range can run");
    }

    // Positions are a probe-only concern: WriteRanges needs no geometry.
    if (g_probe && armed)
    {
        if (void** s = FindIatSlot(g_exe, DLL_ENGINE, SYM_NODE_GETPOS))
            o_NodeGetPosition = (t_VecGetter)*s;
        else
            Logf("construct  no import slot for C3D_NODE::GetPosition - the report "
                 "will carry lists but no distances");
    }

    int written = 0;

    // The office window's own ceiling. Independent of the site tables below -
    // this one has real addresses and they are verified byte for byte.
    if (g_raiseCeiling && PatchClamp()) written++;

    if (g_patch)
    {
        g_slot = (float*)AllocNear(g_exeBase, 64);
        if (!g_slot)
            Logf("construct  no allocation within reach of the executable - not patching");
        else
        {
            written += PatchGroup(&kRangeGroup, (double)g_officeRange);
            written += PatchGroup(&kLimitGroup, (double)g_officeLimit);
        }
    }

    if (!armed && !written)
    {
        Logf("construct  nothing installed - inactive");
        return 1;
    }

    // Say what will actually happen, not what was asked for. Without the render
    // hook there is nothing to run WriteRanges from, and claiming otherwise is
    // how the gating bug above stayed invisible.
    if (g_writeRange && !armed)
        Logf("construct  WARN write_range is on but the render hook could not be "
             "installed - no office range will be set. Only the window ceiling, "
             "if it was raised, is in effect.");
    else if (g_writeRange && g_gameDefault > 0)
        Logf("construct  ready: a construction office starting at the game's %d m will "
             "be set to %d m (its own window stops at %d, and lowering one by hand "
             "sticks)", g_gameDefault, g_officeRange, g_raiseCeiling ? g_ceiling
                                                                    : VANILLA_RANGE_MAX);
    else if (g_writeRange)
        Logf("construct  ready: every office held at %d m - `game_default = 0`, so they "
             "cannot be lowered by hand", g_officeRange);
    else
        Logf("construct  ready: probe %s, writing nothing",
             armed ? "armed" : "off");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
