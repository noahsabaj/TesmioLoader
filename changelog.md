## Changelog

---

*Unreleased*
A tenth plugin, `construction`. A construction office only picks up jobs within a fixed distance of itself, so a site across town is never started however idle the office is - `office_range` in `plugins\construction.ini` makes that reach yours, 10 km by default. The range turned out to be a plain integer on the office object, the same number its own window shows as "<3,500m", so the plugin simply sets it: no patched code, and nothing for a game update to move. The office window's own + button stopped at 3500 because of a hard-coded clamp and a fixed menu of values in the button handlers, so seven instructions are patched to lift that too - all seven or none of them, verified byte for byte first. An office you have adjusted by hand is left alone; only one still sitting at the game's starting value is raised.
Fixes found in an audit of the whole tree, none of which needed a game update to matter:
- `construction`: `write_range` did nothing at all unless `probe` was also on, because both hung off one hook that only the probe installed - and the ini told you to turn the probe off. `game_default = 0`, documented as holding every office on every frame, quietly behaved the same as the default mode instead.
- `tesmiolauncher`: a `LoadLibraryW` that took longer than thirty seconds was read as success, so the game was resumed and the injected DLL's path buffer was freed while the loader was probably still reading it.
- `tesmiolauncher`: plugin DLLs were loaded - running their `DllMain` in the launcher - before their signature was checked, which made the signature mark decorative. The signature is now verified first, and a file whose signature does not verify is never loaded.
- `tesmiolauncher`: a malformed `SOVIET64.exe` could overflow a 32-bit bounds check in the version reader and be read far past the end of the file. A `--game` path longer than 260 characters terminated the launcher outright instead of reporting anything.
- `walking`: the eight patch sites were verified and written one at a time, so a game update that moved one of them would leave the other seven patched - the exact simulation-disagrees-with-overlay bug this plugin took four versions to fix. All eight are now verified before any is written.
- `tesmioloader`: the save-needs-mods warning was a modal message box shown from inside the game's own file-open call, which blocked the game thread until it was dismissed. It now appears on a thread of its own.
- `accumulator`: `building_types = 18 19` silently kept only the first type.
- Smaller ones: an out-of-bounds read in the resource hex dump, a missing upper bound on the inline hooker's trampoline, two data races in the texture probe, a per-day log spam in `aging`, and about seventy spurious compiler warnings that were hiding the real ones.

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