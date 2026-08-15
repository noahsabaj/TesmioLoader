## Changelog

---

*Update! - v. b0.4.0*
A tenth plugin, `construction`. A construction office only picks up jobs within a fixed distance of itself, so a site across town is never started however idle the office is - `office_range` in `plugins\construction.ini` makes that reach yours, 10 km by default. It gets there by two quite different routes, and it is worth knowing which is which. The range itself turned out to be a plain integer on the office object, at `+0xFC8` - the very number the office's own window shows as "<3,500m" - so `write_range` simply sets that field: no patched code anywhere in it, and no address for a game update to move. The office window's own `+` button is the other route and it does need code, because the button stopped at 3500 on account of hard-coded clamps and a fixed ladder of values baked into the handlers. `raise_ceiling` lifts that with seven patched instructions - all seven or none of them, each verified byte for byte against this build before a single one is written, and a game update that moves them gets a refusal in the log rather than seven writes into whatever now lives at those addresses. It is on by default because the button disagreeing with the range is worse than the patch; `raise_ceiling = 0` leaves the executable's own bytes alone and `write_range` still works, since the two have nothing to do with each other.
An office you have adjusted by hand is left alone, on purpose: only one still sitting at the game's own starting value of 3500, or still holding the value this plugin wrote last, is set. Changing `office_range` therefore moves the offices the plugin already owns and none of yours - it notes what it wrote in the ini so it still recognises them next session. And the range lives in the save, so a raise is permanent: an office set to 10 km stays at 10 km with the plugin, without it, and after you delete the DLL. The way back down is the office's own `-` button, or lowering `office_range` and letting the plugin follow its own writes the other way.
Fixes found in an audit of the whole tree, none of which needed a game update to matter:
- `construction`: `write_range` did nothing at all unless `probe` was also on, because both hung off one hook that only the probe installed - and the ini told you to turn the probe off. `game_default = 0`, documented as holding every office on every frame, quietly behaved the same as the default mode instead.
- `construction`: a demolished office was still written to, because the plugin kept the pointer it had found and never asked again. Every write now re-checks that the address is still readable, still a construction office of the type it matched, and not already flagged as going away, and drops it until the building list hands it back. `MAX_OFFICES` went from 16 to 64 on the evidence of a map with 17 of them, whose seventeenth office silently never got its range set. The log also now says, once per office, what the range was and what it became - the absence of that line is how an argument about the game's own default ended up being settled by git archaeology. It is 3500: that is what the probe read out of every office it ever looked at, and what the executable's own validator writes into the field. 1000 sat in the ini for a while and was never sourced from anything: it was a guess at one rung of the `+`/`−` button ladder - the one the handler computes at run time instead of carrying as an immediate, so the disassembly does not name it - and it is not that rung either.
- `tesmiolauncher`: `--game <path>` consumed one argument too many, so `--game C:\...\SOVIET64.exe --nogui` ate the `--nogui`, and a `--game` at the very end of the command line read one entry past the end of the list.
- `tesmiolauncher`: on a system set to "Use Unicode UTF-8 worldwide", the launcher could not build the path to its own ini at all, so every plugin tick box was read from and written to nowhere and your choices did not survive closing it.
- `tesmiolauncher`: a `LoadLibraryW` that took longer than thirty seconds was read as success, so the game was resumed and the injected DLL's path buffer was freed while the loader was probably still reading it.
- `tesmiolauncher`: plugin DLLs were loaded - running their `DllMain` in the launcher - before their signature was checked, which made the signature mark decorative. The signature is now verified first, and a file whose signature does not verify is never loaded. A plugin with no signature at all still loads, as it always has: a third-party build is correctly unsigned and is not accused of anything.
- `tesmiolauncher`: a malformed `SOVIET64.exe` could overflow a 32-bit bounds check in the version reader and be read far past the end of the file. A `--game` path longer than 260 characters terminated the launcher outright instead of reporting anything.
- `walking`: the eight patch sites were verified and written one at a time, so a game update that moved one of them would leave the other seven patched - the exact simulation-disagrees-with-overlay bug this plugin took four versions to fix. All eight are now verified before any is written, and all eight pages are made writable before any byte is copied, so a run that cannot take one of them writes nothing at all instead of the seven it could. Two of the eight were also being dropped from the tally the summary line printed, so a fully successful run could report six.
- `tesmioloader`: the save-needs-mods warning was a modal message box shown from inside the game's own file-open call, which blocked the game thread until it was dismissed. It now appears on a thread of its own - and once per save, which is what it always claimed to do: the table remembering which saves had already been warned about held sixteen entries, and any real save folder passes sixteen, so every save past that warned again on every single load.
- `accumulator`: `building_types = 18 19` silently kept only the first type. Now the whole list is read, up to 32 of them; a duplicate is dropped with a line saying so, an entry past the end of the list is dropped loudly rather than quietly, and a typo is named - `"transformator"` used to become building type 0, which is a real type, so the plugin went hunting for it and there was nothing in the log to say why.
- `aging`: a probe that could not read a single citizen re-armed and announced itself once a game day, for ever. It now gives up after five days without one and stops. `aging` is parked - `build.bat` skips the folder - so this lands in the source and no DLL ships with it either way.
- `resources`: the audit reported the hex dump overrunning its line buffer. Traced byte by byte, it cannot: the widest row the only caller can hand it is 75 bytes into a 128-byte buffer, and the truncation fallback meant to catch the overflow is unreachable for the same reason. Nothing was changed except that the arithmetic is now written down, so the next reader does not have to derive it again to reach the same answer.
- Everything `build.bat` compiles is built at `/W4` now rather than `/W3`, and the tree is clean at it. That is what keeps the roughly seventy spurious warnings the audit cleared - the ones that were hiding the real ones - from quietly coming back.
- Smaller ones: a missing upper bound on the inline hooker's trampoline, two data races in the texture probe, and a `WaitForSingleObject` that failed outright being reported as a timeout.

---

*Update! - v. b0.3.6*
The game's regular branch updated to v1.1.1.9 - the update notes said content only, but the executable itself had in fact reflowed throughout: `.text` grew in steps of up to nearly 500 bytes depending how far into it a given address sat, and the `.rdata` constant pool shifted in two separate clusters. Every hard-coded address across all nine active plugins has been re-derived and re-verified byte for byte against the new build; v1.1.1.7 is no longer supported, and none of its addresses remain in the source.
The first launch on the new build crashed: `resources` was installing an import hook before its main hook, and when the main one refused (address moved), the loader freed the plugin with the import hook still pointing into it. Fixed - the import hook now installs only after the main one is confirmed.
Two plugins, `aging` and `buildings`, are parked for now - `build.bat` skips both folders, so neither ships a DLL until they get a proper look. Nothing about either is deleted.
Some stale comments describing an old, pre-plugin-split version of the loader (a resource-name hook that has lived in `plugins\resources` for a long time) were cleared out of `tesmioloader.cpp` - no behaviour changed, just what the file claims about itself.

---

*Update! - v. b0.3.5*
Game version control. The launcher now reads the version out of SOVIET64.exe before it starts anything, says in its window which version is required and which one is installed, and refuses to launch a game that is not v1.1.1.7 - every address the loader patches belongs to that build, and injecting into another one is what makes the game die on startup with nothing to explain it. `version_check = 0` in tesmioloader.ini, or `--ignore-version`, turns the refusal back into a warning.
The plugin list is now two columns with a scroll bar, so a long list no longer pushes the Launch button off the bottom of the screen. The launcher window carries the tesmioloader logo in its title bar.
Signature marks are quieter: a plugin built by me shows `[tesmio]`, anything unsigned shows nothing at all instead of being labelled "not from Tesmio", and only a signature that exists and does not match its file is called out.
Plugins built against an older API version now keep working where that is possible: the loader accepts a range of API versions rather than one exact number, and a change that does not have to break an old plugin is not allowed to.

---

*Update! - v. b0.3.4*
A signature of my authorship has been added - modified plugins or plugins without a signature will be marked as not signed. This does not affect anything - any other plugins will load. It's just my digital signature that the plugin was built by me and not modified in any way.
Fixed bugs and crashes: issues #8, issues #9, issues #10

---

*Update! - v. b0.3.3*
A cross-reference bug that was used during development and accidentally made it into the release has been fixed. This was the reason why many users often failed to search for icons and resources, even though they were physically located where they should have been.

---

*Update! - v. b0.3.2*
Added version control - the game will no longer crash if the plugin version does not match the launcher version - it will notify you of this during the initialization phase.

---

*Update! - v. b0.3.1*
Minor fixes. Added a launcher logo. Fixed a save error causing crashes due to deposits.dll.

---

*Update! - v. b0.3*
The launcher now has a window. It shows where the game file was found, with a Browse button if it was found incorrectly, and a checkbox for each plugin. Uncheck it, and the plugin will remain on disk but will not load, so you can disable this feature without deleting anything. Your choice is remembered.
Critical bugs in the resource and deposit plugins have also been fixed, and tooltips have been added to custom buttons.

---

*Update! - v. a0.2.1*
Minor fixes and refactoring

---

*Update! - v. a0.2*
The loader has been updated to version a0.2—the code has been completely refactored and the architecture has been changed to ensure greater unification across separate plugins. If you have already installed the loader, please completely delete this folder and install the updated version.
The loader is now separated from the code, allowing you to install functions separately—the resource plugin, the deposit plugin, or the depletion plugin.