# macOS port: first test on a real Mac

Tested on 2026-11-25/26 by a Claude Code session on the repository owner's Mac, with the owner doing the Finder and Gatekeeper steps that need a person. No code was changed; this report is for the sessions that fix what it finds. Screenshots are in [`docs/mac-test/`](mac-test/).

## Environment and build tested

| | |
|---|---|
| Commit | `5b2dbe124ac746b124b3d67a2708f7e45742806f` (`main`, "Multi-platform port M3: FreeFileSync and RealTimeSync on macOS (#13)") |
| Build tested | built locally on this Mac (not the CI artifact), arm64, with one build workaround (`CXXFLAGS="-include sys/types.h"`, see F1); installed to `/Applications` from a DMG made by `wrap-mac-dmg.sh` |
| Mac | Apple M3 Pro (arm64), 12 cores, 18 GB RAM |
| macOS | 27.2.1 (build 26B131) |
| Xcode | 27.2 (27B52), Apple clang 21.0.0 (clang-2100.4.1.1), SDK 27.2. **CI uses Xcode 26.x**; this is a newer toolchain than CI's |
| Free disk | 13 GiB at the start (11 GiB after all builds) |
| Homebrew tools | bash 5, coreutils, flock, pkgconf, make (gmake), create-dmg 1.3.0 |
| Appearance | Dark mode (system), Dock at the bottom, a third-party menu bar manager hides most status items |

## Summary

| # | Item | Result | Severity |
|---|---|---|---|
| P1 | Libraries (`build.sh`) | pass after a manual re-download (F12) | minor |
| P1 | Apps (`gmake`) | **fail** with Xcode 27: `ino_t` unknown in `base/db_file.cpp` (F1); pass with a `CXXFLAGS` workaround | blocker (Xcode 27 only) |
| P2 | `Tests/zen/run.sh` | pass: 39 cases, 1771 checks; `from_chars` fallback 1 case, 128 checks | |
| P2 | `Tests/sync/run.sh` | **fail** to build (F1); with the workaround pass: 22 cases, 167 checks | blocker (Xcode 27 only) |
| P2 | `Tests/zip_afs/run.sh` | pass | |
| P2 | `check-mac-app-starts.sh` (3 runs as in CI) | pass; but its full-screen screenshot can't show the app on a busy desktop (F15) | minor |
| P2 | `wrap-mac-dmg.sh` | pass (create-dmg layout); `hdiutil` prints deprecation warnings on macOS 27 (F16) | cosmetic |
| 1 | First launch from DMG copy, Gatekeeper | expected block for an ad-hoc signed app; right-click > Open no longer helps (F3); RealTimeSync then runs translocated (F2) | major |
| 2 | `.ffs_batch` from Finder while the app runs (idle, modal dialog open, sync running) | pass, all three | |
| 3 | Cmd+Q, Cmd+, and About with sync settings, options, Save As and folder picker open | pass: items disabled, nothing closes; Quit works afterwards | |
| 4 | `.ffs_gui` from Finder with sync settings open | pass: loaded after the dialog closed (with the normal "save changes?" prompt) | |
| 5 | Dock progress bar | pass: gray while comparing, blue filling during sync, full while the result dialog is open, cleared after it closes | |
| 6 | Main window layout | pass with remarks: narrow path boxes at the default size (F10), dark-mode colors to compare with Linux (F11) | minor |
| 7 | Dark/light mode, switching while open, language change | **fail** on live switch: stale button and panel colors (F5); fresh starts and the language change are fine; standard macOS menu items stay English (F9) | major |
| 8 | Every dialog | pass with remarks; **detail text missing in some message boxes** (F4); Windows/Linux texts (F8) | major |
| 9 | Menus, Cmd shortcuts, Check for updates | pass (notice, no network), but the notice's text is blank (F4); **no Edit menu, Cmd+V fails in dialogs** (F6) | major |
| 10 | Drag folder from Finder; Browse | pass, both boxes | |
| 11 | Real syncs: mirror, two-way twice, update, Trash, versioning, tags, symlink, NFC/NFD, case-only rename | pass, all; default exclude filter is Linux-only (F7) | minor |
| 12 | exFAT disk image | pass: no `._*` files, second compare "No files to synchronize" | |
| 13 | Show in Finder, open with default app, sounds | pass; end-of-sync sound only indirectly confirmed (no one listening) | |
| 14 | RealTimeSync: watch, status item and menu, `.ffs_real` from Finder, Start label | pass, except FreeFileSync not found when RealTimeSync is translocated (F2) | major |
| 15 | Batch job: save, double-click, terminal, exit code, log location | pass: exit code 0, JSON summary on stdout, log in `~/Library/Application Support/FreeFileSync/Logs` | |
| 16 | App icon in Finder, Dock, About | acceptable in the Dock; blurry when large and drawn on a gray plate (F13) | cosmetic |
| 17 | Window size/position and config restored after Cmd+Q and after closing the window | pass; unsaved changes lost after a language change (F14, probably upstream) | minor |
| 18 | Leftover processes after quitting | pass: none, also for the Sentry build's `crashpad_handler` | |
| P4 | Sentry build and `Tests/sentry/run.sh` | pass: 11 scenarios ok, 2 skipped on macOS (xdotool) | |
| P4 | First-start crash-report notice | **fail**: first paragraph of the explanation missing (F4) | major |
| P4 | "Send crash reports" checkbox | pass | |
| P4 | Release name in reports | `freefilesync@14.13+dev`, not the commit hash CONTRIBUTING promises (F17) | minor |
| – | Accessibility | buttons without names, very slow AX tree for big logs (F18) | minor |

No crash reports (`~/Library/Logs/DiagnosticReports`) and no fault messages from FreeFileSync or RealTimeSync during the whole session.

## Failures and findings

Ordered by severity. "Source" is the best guess at where the fix belongs.

### F1: build fails with Xcode 27: `ino_t` undeclared in `base/db_file.cpp` (blocker with Xcode 27)

- Steps: `gmake -C FreeFileSync/Source -j12` on macOS 27.2.1 with Xcode 27.2 (SDK 27.2). Same failure for `bash Tests/sync/run.sh` (engine build) and for `FFS_SENTRY=1`.
- Expected: builds as in CI (Xcode 26.x).
- Actual (complete error output; `gmake -k` shows it is the only failing file):

  ```
  base/db_file.cpp:575:17: fatal error: unknown type name 'ino_t'
    575 |                 ino_t fileIndex = 0;
        |                 ^
  1 error generated.
  gmake: *** [Makefile:326: ../Build/Obj/FreeFileSync_arm64/ffs/src/base/db_file.cpp.o] Error 1
  ```
- Source: `FreeFileSync/Source/base/db_file.cpp:575` uses `ino_t` and `dev_t` (line 576) and relied on a transitive include of `<sys/types.h>` that the macOS 27 SDK's libc++/headers no longer provide. Fix: `#include <sys/types.h>` in `db_file.cpp` (upstream file, but CLAUDE.md asks to include what you use). Consider adding an Xcode 27 job or checking with `-isysroot` of the newest SDK in CI.
- Workaround used for everything in this report: `CXXFLAGS="-include sys/types.h"` (the makefiles append the environment's `CXXFLAGS`).
- Timings with the workaround: FreeFileSync 25 s, RealTimeSync 8 s (`-j12`). Without it FreeFileSync stops after 7 s.

### F2: RealTimeSync cannot find FreeFileSync when macOS runs it translocated (major)

- Steps: install both apps from the DMG by dragging (Finder copy), so they keep the quarantine flag; open RealTimeSync through "Open Anyway" (F3); open `job.ffs_batch` with RealTimeSync (`open -a RealTimeSync job.ffs_batch`, or import it in the window).
- Expected: the command line points to `/Applications/FreeFileSync.app/Contents/MacOS/FreeFileSync`.
- Actual: RealTimeSync runs from `/private/var/folders/…/T/AppTranslocation/<uuid>/d/RealTimeSync.app` (App Translocation, a read-only random mount of that one bundle). The generated command is `/private/var/folders/…/AppTranslocation/<uuid>/d/FreeFileSync.app/Contents/MacOS/FreeFileSync /private/tmp/ffs-mac-test/cfg/job.ffs_batch`, and that `FreeFileSync.app` does not exist (only `RealTimeSync.app` is in the mount), so the job can never start. The translocation stayed after "Open Anyway" and after relaunching. Screenshot: [`14-rts-imported-batch-translocated.jpg`](mac-test/14-rts-imported-batch-translocated.jpg).
- Source: `FreeFileSync/Source/ffs_paths.cpp:97` `getFreeFileSyncLauncherPath()` takes the sibling of its own bundle. Fix ideas: detect translocation (`SecTranslocateIsTranslocatedURL`, or the path containing `/AppTranslocation/`) and map back with `SecTranslocateCreateOriginalPathForURL` (Security framework, private-ish but widely used), or look up `io.github.cuong-tran.freefilesync` with `NSWorkspace URLForApplicationWithBundleIdentifier:`, keeping the sibling as the first choice. Also tell users in the release notes to remove the quarantine flag or move the apps with Finder.

### F3: Gatekeeper: unsigned app is blocked, right-click > Open no longer bypasses it (major, expected)

- Steps: DMG with a Safari-style quarantine flag, both apps copied to `/Applications` by Finder, double-click RealTimeSync. (The owner had removed the flag from FreeFileSync with a "Clear Extended Attributes" tool before trying it, so FreeFileSync then simply started; its first-start screenshots are [`01-ffs-first-start-main.jpg`](mac-test/01-ffs-first-start-main.jpg) and [`01-ffs-first-start-about.jpg`](mac-test/01-ffs-first-start-about.jpg).)
- Actual: "“RealTimeSync” Not Opened. Apple could not verify “RealTimeSync” is free of malware…" with only *Move to Trash* and *Done* ([`01-gatekeeper-realtimesync.jpg`](mac-test/01-gatekeeper-realtimesync.jpg)). Right-click > Open shows the same dialog (macOS 15+ removed that bypass). The only way: System Settings > Privacy & Security > *Open Anyway*, then a second dialog with *Open Anyway* ([`01-gatekeeper-open-anyway.jpg`](mac-test/01-gatekeeper-open-anyway.jpg)), then the account password. `open /Applications/FreeFileSync.app` from a shell failed with `_LSOpenURLsWithCompletionHandler() failed with error -128` and no dialog.
- This is the documented consequence of ad-hoc signing (PROGRESS.md decision of 2026-11-23). Needed: README/release-note instructions (Open Anyway, or `xattr -dr com.apple.quarantine /Applications/FreeFileSync.app /Applications/RealTimeSync.app`, which also avoids F2), and eventually Developer ID signing plus notarization.

### F4: message boxes drop the detail text's wrapped paragraphs (major)

- Seen three times:
  - Help > Check for updates now: only the headline "FreeFileSync 14.13 (macOS)"; the explanation "This macOS build does not check online for updates. New builds are published at https://github.com/…" is not drawn, in dark and light mode ([`09-check-for-updates.jpg`](mac-test/09-check-for-updates.jpg), [`09-check-for-updates-light.jpg`](mac-test/09-check-for-updates-light.jpg)).
  - Sentry build, first-start notice: the first paragraph ("A crash report contains the crash dump…", long, would wrap over several lines) is missing, with its space reserved; the second paragraph after the blank line, "You can change this later in the options.", is drawn ([`P4-crash-report-notice.jpg`](mac-test/P4-crash-report-notice.jpg)). This is the "blank area" seen in CI.
  - Counter-example: the warning "The following folders do not yet exist: … The folders are created automatically when needed." shows all its text ([`08-warning-missing-folder.jpg`](mac-test/08-warning-missing-folder.jpg)); none of its lines wraps, and it has no headline.
- Expected: the whole detail text.
- Best guess: paragraphs that need line wrapping are laid out (height reserved) but not drawn by the `wxRichTextCtrl` on wxOSX; unwrapped paragraphs draw fine. A headline (`setMainInstructions`) or a URL is the other difference, so isolate first (e.g. a long detail text without headline and URL).
- Source: `wx+/popup_dlg.cpp:184-190` (`setBestInitialSize` + `setTextWithUrls` on `m_richTextDetail`), `wx+/window_tools.h:156` (`setTextWithUrls`: `SetMargins`, `BeginParagraphSpacing`, `WriteText`), control created in `wx+/popup_dlg_generated.cpp:40` (`wxRichTextCtrl`, `wxTE_READONLY|wxBORDER_NONE|wxVSCROLL|wxWANTS_CHARS`). Possibly a layout before the control has its final width (wrap width 0); try `LayoutContent()`/`Refresh()` after the dialog's `Fit()`, or a `wxTextCtrl` on macOS. Every error/warning/question box with a long detail text is affected, not just these two.

### F5: switching between light and dark mode while the app runs leaves stale colors (major)

- Steps: start FreeFileSync in dark mode, compare, then switch System Settings > Appearance to Light (also tested Light → Dark).
- Expected: everything follows the new appearance, as after a restart ([`07-main-light-fresh-start.jpg`](mac-test/07-main-light-fresh-start.jpg)).
- Actual, dark → light: the texts drawn into the Compare and Synchronize buttons, "Select view:" and the item count stay white and become nearly invisible; the Configuration/Overview panel captions are washed out ([`07-main-light-after-switch.jpg`](mac-test/07-main-light-after-switch.jpg)). Light → dark: the Compare button keeps its light background, "Synchronisieren" (German UI at the time) is dark on dark ([`07-main-dark-after-switch.jpg`](mac-test/07-main-dark-after-switch.jpg)). Rebuilding the window (changing the language) or restarting fixes it ([`07-main-dark-after-language-change.jpg`](mac-test/07-main-dark-after-language-change.jpg)). Users with Appearance set to Auto hit this every evening and morning.
- Source: `FreeFileSync/Source/ui/main_dlg.cpp:439` `updateTopButton()` and `:738` ("Select view:" label) render text into bitmaps with `wxSystemSettings` colors once; nothing in the code handles `wxEVT_SYS_COLOUR_CHANGED`. Fix: on that event, re-run the button image updates, refresh the AUI pane art colors and refresh the grids (`wx+/grid.cpp`, `ui/file_grid.cpp`); same for the open dialogs and RealTimeSync's Start button.

### F6: no Edit menu; Cmd+V does not paste in dialogs (major)

- Steps: Actions > Synchronization settings > Versioning; click the versioning folder box; copy a path anywhere; press Cmd+V.
- Expected: the path is pasted.
- Actual: nothing is pasted, even when the clipboard was filled by FreeFileSync itself (Cmd+A, Cmd+C in that same box). Cmd+A, Cmd+C and Cmd+X work there, typing works. In the main window's folder boxes Cmd+V works; Cmd+Z does nothing anywhere.
- Source: the menu bar has FreeFileSync, File, Actions, Tools, Window, Help but no Edit menu (`ui/gui_generated.cpp:16` and on). Cocoa delivers Paste/Undo to text fields through the Edit menu's key equivalents. Fix: on macOS add a standard Edit menu (`wxID_UNDO`, `wxID_REDO`, `wxID_CUT`, `wxID_COPY`, `wxID_PASTE`, `wxID_SELECTALL`) to both apps' menu bars, in code, not in the generated files.

### F7: default exclude filter has no macOS entries (minor)

- Actual: the default filter excludes `*/.Trash-*/` and `*/.recycle/` only ([`08-sync-settings-filter.jpg`](mac-test/08-sync-settings-filter.jpg)), so `.DS_Store` files that Finder creates are synchronized and show up as differences.
- Source: `FreeFileSync/Source/config.h:218-226`. Suggest a `ZEN_MAC` branch with `*/.DS_Store`, `*/._*`, `*/.Trashes/`, `*/.fseventsd/`, `*/.Spotlight-V100/`, `*/.DocumentRevisions-V100/`, `*/.TemporaryItems/`.

### F8: Windows and Linux texts on macOS (cosmetic)

- Save as batch job: "To start, double-click this file or schedule in a task planner: FreeFileSync.exe <configuration file>.ffs_batch" ([`08-save-as-batch-job.jpg`](mac-test/08-save-as-batch-job.jpg)). Source: `ui/batch_config.cpp:75`. On macOS: `/Applications/FreeFileSync.app/Contents/MacOS/FreeFileSync <configuration file>.ffs_batch`.
- Sync settings, "Run a command": hint "Example: systemctl poweroff" ([`08-sync-settings-synchronization.jpg`](mac-test/08-sync-settings-synchronization.jpg)). Source: `ui/sync_cfg.cpp:670`. A macOS example: `osascript -e 'tell app "System Events" to shut down'` or `pmset sleepnow`.

### F9: standard macOS menu items stay English; Mac menu wording (cosmetic)

- With Tools > Language > Deutsch the app's own menus and the window are German, with every button label once and fitting ([`07-language-german-light.jpg`](mac-test/07-language-german-light.jpg)), but Hide FreeFileSync, Hide Others, Show All, Quit FreeFileSync and the whole Window menu stay English; the bundle declares only `CFBundleDevelopmentRegion` en (`FreeFileSync/Source/mac_bundle.sh:66`), no `CFBundleLocalizations` or `.lproj` folders. Also "Preferences" instead of "Settings…" (macOS 13+) and "About" instead of "About FreeFileSync".
- The F-key shortcuts (F5 compare, F6–F8 settings, F9 sync, F2 rename) need the fn key on Mac keyboards; Cmd-based alternatives would help.

### F10: main window too small by default; path boxes narrow (minor)

- Default window 900×600 points, placed low (top left at 42,520 on a 1800×1169 screen). The folder path boxes are 138 and 159 points wide (left/right differ), paths and file names are cut ([`06-main-compared-default-size.jpg`](mac-test/06-main-compared-default-size.jpg)). At 1500×950 everything fits ([`06-main-compared-1500x950.jpg`](mac-test/06-main-compared-1500x950.jpg)).
- Source: the initial size/position of the main dialog (`ui/main_dlg.cpp`, generated sizes in `ui/gui_generated.cpp`); a larger default and centering on macOS.

### F11: dark mode details to compare with the Linux dark theme (cosmetic)

- Overview panel: the percentage cells ("4%") have a white background in dark mode ([`06-main-compared-1500x950.jpg`](mac-test/06-main-compared-1500x950.jpg)).
- Configuration list: the "[Last session]" row has a light gradient in dark mode, also on a fresh dark start ([`06-main-dark-fresh-start-restored.jpg`](mac-test/06-main-dark-fresh-start-restored.jpg)).
- File grid: items missing on one side are drawn as pale placeholder icons in a narrow light column ([`06-main-compared-1500x950.jpg`](mac-test/06-main-compared-1500x950.jpg), right grid). May be upstream's design.
- Dialogs from `showNotificationDialog` show only their own title ("Warning"), and the sync confirmation and progress dialogs have an empty title; check against Linux (`wx+/popup_dlg.cpp:165` prefixes `GetAppDisplayName()`).

### F12: libidn2 download fails on an expired mirror certificate (minor, tooling)

- `FFS_TARGET=macos-arm64 .github/build/build.sh` stopped after 45 s: `https://ftpmirror.gnu.org/libidn/libidn2-2.3.8.tar.gz` redirected to `https://gnu.mirror.constant.com//libidn/…`, and curl failed with `curl: (60) SSL certificate problem: certificate has expired`.
- Workaround: downloaded `https://ftp.gnu.org/gnu/libidn/libidn2-2.3.8.tar.gz` into `.build/archives/` (SHA-256 matched `common.sh`), then the rerun took 191 s.
- Source: `.github/build/build-libidn2.sh:23`. Fix: try `ftp.gnu.org` when the mirror redirector fails (`download_archive` in `common.sh` could take several URLs).

### F13: app icon blurry at large sizes, gray plate on macOS 26+ (cosmetic)

- The `.icns` holds all sizes up to 1024 px, upscaled from a 128 px image, so 512/1024 px are soft ([`16-icns-1024.jpg`](mac-test/16-icns-1024.jpg)). The artwork fills the canvas edge to edge, so macOS 26+ draws it on a gray rounded plate (Dock, DMG window: [`16-dock-icon.jpg`](mac-test/16-dock-icon.jpg), [`P2-dmg-window.jpg`](mac-test/P2-dmg-window.jpg)). In the Dock it looks acceptable.
- Source: `FreeFileSync/Source/mac_bundle.sh` (icns from the 128 px PNG). A larger source image with Apple's margins (about 10 %) would fix both.

### F14: unsaved configuration lost after a language change and restart (minor, probably upstream)

- Steps: load `job.ffs_batch`, change both folders (title shows `*job`), Tools > Language > Deutsch, then English again; the title has lost its `*`. Close the window and start again.
- Expected: the last session's folders.
- Actual: `job.ffs_batch` is reloaded from disk with its saved folders; the changes are gone. The language change recreates the main window and drops the "modified" state. Likely the same on Linux; check there before fixing in the port.

### F15: start check screenshot is a full-screen capture (minor, tooling)

- An app started from a shell by `check-mac-app-starts.sh` does not come to the front; on a desktop with other windows the full-screen `screencapture -x` (line 70) shows other apps, not FreeFileSync (and could capture private content when run on a developer's Mac). CI runners have empty desktops, so CI is unaffected.
- Fix: capture the app's window by id (`screencapture -l <kCGWindowNumber>`, the script already reads the window list), or activate the app first.
- Related: the start check sets `HOME`, but `NSUserDefaults` ignores it, so runs write `~/Library/Preferences/io.github.cuong-tran.{freefilesync,realtimesync}.plist` (only `NSTreatUnknownArgumentsAsOpen = NO`) into the real home.

### F16: `hdiutil` deprecation warnings on macOS 27 (cosmetic, tooling)

- `wrap-mac-dmg.sh` (and create-dmg) print `hdiutil: WARNING: 'hdiutil create/resize/attach/detach/convert …' is deprecated. Please use 'diskutil image …' instead.` The DMG is still made with create-dmg's full layout. Watch this for a future macOS.

### F17: Sentry release name ends in `+dev` (minor)

- With `SENTRY_ENVIRONMENT=ci` and no `FFS_GIT_SHA`, events carry `"release": "freefilesync@14.13+dev"`. CONTRIBUTING.md (Crash reporting) says `FFS_GIT_SHA` defaults to the short hash of `HEAD`, but `FreeFileSync/Source/crash_report_header.sh:10` leaves it empty and the Makefile doesn't set it. Fix the code or the documentation.

### F18: accessibility (minor)

- Bitmap buttons (Compare, Synchronize, the settings buttons, New/Open/Save…) have no accessibility title or description; VoiceOver can only say "button".
- Reading the accessibility tree of the sync result dialog after a 23,161-item sync took over 2 minutes (the request was stopped), probably the log list; VoiceOver users may see a hang.

### Smaller remarks

- `ld: warning: ignoring duplicate libraries: '-lcrypto', '-lidn2', '-lssh2', '-lssl', '-lz'` when linking FreeFileSync.
- In the sync settings, after clicking Mirror the keyboard focus ring stays on the Two way button while Mirror is highlighted as selected.
- The Overview panel stays empty when all differences are files in the base folder ([`13-grid-one-item.jpg`](mac-test/13-grid-one-item.jpg)); compare with Linux.

## What passed, in detail

- Bundles: `codesign --verify --strict` valid, `lipo` arm64, `minos 13.3`, `sdk 27.2`, `otool -L` only `/usr/lib` and `/System/Library` (both apps).
- Start checks as in `macos-build.yml`: FreeFileSync first start (About over the main window), FreeFileSync with a `sync_test --write-gui` configuration, RealTimeSync: all `rc=0` in 10 s each.
- DMG: 18 MB, made with create-dmg, both signatures verified inside; window with both apps and the Applications link ([`P2-dmg-window.jpg`](mac-test/P2-dmg-window.jpg)).
- Item 2: three times a second process `FreeFileSync job.ffs_batch` started and showed its progress window, warning and result ([`02-batch-progress-second-process.jpg`](mac-test/02-batch-progress-second-process.jpg), [`02-batch-warning-second-process.jpg`](mac-test/02-batch-warning-second-process.jpg), [`02-batch-result-second-process.jpg`](mac-test/02-batch-result-second-process.jpg)): with the app idle, with the Options dialog open, and during a running sync of 23,161 items.
- Item 3: with the sync settings, Options, Save panel ([`03-save-as-panel.jpg`](mac-test/03-save-as-panel.jpg)) and folder picker ([`10-folder-picker.jpg`](mac-test/10-folder-picker.jpg)) open, the application menu's About, Preferences and Quit are disabled; Cmd+Q and Cmd+, change nothing. Quit works after closing them. (The beep could not be heard by the tester.)
- Item 4: [`04-save-changes-prompt.jpg`](mac-test/04-save-changes-prompt.jpg) appeared only after the settings dialog was closed; then the file loaded.
- Item 5: [`05-dock-progress-during-compare.jpg`](mac-test/05-dock-progress-during-compare.jpg) (gray, indeterminate), [`05-dock-progress-during-sync.jpg`](mac-test/05-dock-progress-during-sync.jpg) (blue, frames left to right; the last one is after completion with the result dialog still open). The icon was identical to the plain icon after the result dialog closed.
- Item 8: sync settings tabs ([`08-sync-settings-comparison.jpg`](mac-test/08-sync-settings-comparison.jpg), [`08-sync-settings-filter.jpg`](mac-test/08-sync-settings-filter.jpg), [`08-sync-settings-synchronization.jpg`](mac-test/08-sync-settings-synchronization.jpg)), Options ([`08-options.jpg`](mac-test/08-options.jpg)), confirmation ([`08-sync-confirmation.jpg`](mac-test/08-sync-confirmation.jpg)), progress ([`08-sync-progress.jpg`](mac-test/08-sync-progress.jpg)), result ([`08-sync-result.jpg`](mac-test/08-sync-result.jpg)), batch save ([`08-save-as-batch-job.jpg`](mac-test/08-save-as-batch-job.jpg)), About ([`01-ffs-first-start-about.jpg`](mac-test/01-ffs-first-start-about.jpg)). Not shown: copy/delete/rename confirmations (not opened).
- Item 9: Help > Check for updates now shows a local notice and contacts no server (text missing, F4).
- Item 10: dragging `left` from Finder onto the right path box and `right` onto the left box set both paths; Browse opens the native folder panel in the current folder and returns the chosen folder.
- Item 11 (`/private/tmp/ffs-mac-test/s`, ls and Finder: [`11-finder-right-folder.jpg`](mac-test/11-finder-right-folder.jpg)):
  - Mirror: all files copied with times; `tagged.txt` kept its Finder tags (`com.apple.metadata:_kMDItemUserTags`, red dot in Finder); `link-to-a` copied as a symlink with "Include symbolic links: As link" ([`11-mirror-compare.jpg`](mac-test/11-mirror-compare.jpg)).
  - NFC/NFD: R held `café-nfc.txt` spelled in NFD, L in NFC; FreeFileSync paired them as one equal item (R keeps its spelling). `résumé-nfd.txt` (NFD) copied unchanged in NFD.
  - Case-only rename `Case.txt` → `CASE.txt`: shown as equal content, R renamed, not copied ([`11-case-rename-and-delete-compare.jpg`](mac-test/11-case-rename-and-delete-compare.jpg)).
  - Recycle bin: R's `a.txt` landed in `~/.Trash/a.txt` (moved back out after the test).
  - Versioning: the old `b.txt` moved to the versioning folder; also the two-way deletion below.
  - Two way, twice: first run copied in both directions and created `.sync.ffs_db` on both sides; second run used it and propagated a deletion.
  - Update: a new file copied, a file deleted on the left kept on the right ([`11-update-compare.jpg`](mac-test/11-update-compare.jpg)).
- Item 12: `hdiutil create -size 64m -fs ExFAT`: mirror to it, no `._*` files anywhere, no extended attributes on the tagged copy (as designed), the symlink was accepted, second compare shows "No files to synchronize" ([`12-exfat-second-compare.jpg`](mac-test/12-exfat-second-compare.jpg)).
- Item 13: context menu ([`13-grid-context-menu.jpg`](mac-test/13-grid-context-menu.jpg)): "Show in file manager" opened a Finder window with `hello.txt` selected; "Open with default application" opened it in TextEdit. Sounds: the settings point to `bell.wav`, which exists in the bundle; at the end of a sync `pmset -g assertions` showed a new Core Audio speaker assertion start, so the bell most likely played; nobody listened.
- Item 14: RealTimeSync watching two folders ran `date >> ran.log` after a change in each ([`14-rts-configured.jpg`](mac-test/14-rts-configured.jpg)); after Start the window hides and a menu bar item with *Configure* and *Quit* appears (owner checked; it was behind their menu bar manager); *Configure* brings the window back ([`14-rts-after-configure.jpg`](mac-test/14-rts-after-configure.jpg)), *Quit* quits; opening `watch-test.ffs_real` from Finder starts watching directly; the Start label appears once ([`14-rts-main.jpg`](mac-test/14-rts-main.jpg)).
- Item 15: `FreeFileSync.app/Contents/MacOS/FreeFileSync job.ffs_batch` from the terminal: exit code 0 and on stdout `{"syncResult": "success", …, "logFile": "/Users/<user>/Library/Application Support/FreeFileSync/Logs/job 2026-11-26 002759.694.html"}`; log files named `job <date> <time>[ [Warning]].html`.
- Item 17: after Cmd+Q and after closing the window with its close button, the next start restored position and size (80,60 1500×950 and 150,90 1200×760) and the last configuration (see F14 for the exception).
- Part 4: `build-sentry-native.sh` 23 s, FreeFileSync with `FFS_SENTRY=1` 37 s (needs the F1 workaround too), RealTimeSync 9 s, `crashpad_handler` in both bundles; `Tests/sentry/run.sh` 41 s, all 11 scenarios ok (`a-segv`, `a-abort`, `b-internal`, `c-setting-FreeFileSync`, `c-setting-RealTimeSync`, `c-revoked-then-send`, `c-env`, `f-kept-then-sent`, `f-kept-then-deleted`, `g-rts-follows`, `d-no-handler`; `h` and `o` skipped on macOS as designed). Crash capture of real SIGSEGV/abort works on arm64. Options shows *Send crash reports* ticked after the notice was confirmed, and `GlobalSettings.xml` has `<CrashReports Enabled="true"/>` ([`P4-options-send-crash-reports.jpg`](mac-test/P4-options-send-crash-reports.jpg)).

## Commands and timings

| Step | Command | Time | Result |
|---|---|---|---|
| Tools | `brew install bash coreutils flock pkgconf make create-dmg` | 11 s | ok |
| Libraries | `FFS_TARGET=macos-arm64 .github/build/build.sh` | 45 s | failed (F12) |
| Libraries, rerun | same, after fetching libidn2 from ftp.gnu.org | 191 s | ok (OpenSSL, libssh2, libidn2, curl, wxWidgets, all arm64, macOS 13.3) |
| FreeFileSync | `gmake -C FreeFileSync/Source -j"$(sysctl -n hw.ncpu)"` | 7 s | failed (F1) |
| FreeFileSync | `CXXFLAGS="-include sys/types.h" gmake -C FreeFileSync/Source -j12` | 25 s | ok |
| RealTimeSync | `gmake -C FreeFileSync/Source/RealTimeSync -j12` | 8 s | ok |
| zen tests | `bash Tests/zen/run.sh` | 26 s | 39 cases, 1771 checks, 0 failures; fallback run 1 case, 128 checks |
| sync tests | `bash Tests/sync/run.sh` | 14 s | engine build failed (F1) |
| sync tests | `CXXFLAGS="-include sys/types.h" bash Tests/sync/run.sh` | 21 s | 22 cases, 167 checks, 0 failures |
| ZIP test | `bash Tests/zip_afs/run.sh` | 13 s | all passed |
| Start checks | `check-mac-app-starts.sh` ×3 as in `macos-build.yml` | 10 s each | ok |
| DMG | `wrap-mac-dmg.sh <folder> /tmp/test.dmg` | 41 s | ok, made with create-dmg |
| sentry-native | `FFS_TARGET=macos-arm64 .github/build/build-sentry-native.sh` | 23 s | ok |
| Sentry apps | `FFS_SENTRY=1 SENTRY_DSN=http://testkey@127.0.0.1:38917/1 SENTRY_ENVIRONMENT=ci` + both `gmake` (with the F1 workaround) | 37 s + 9 s | ok |
| Sentry tests | `bash Tests/sentry/run.sh FreeFileSync/Build/Bin/macos-arm64` | 41 s | all passed |

## Not tested, and why

- x86_64 builds and Rosetta 2: this is an arm64 Mac; only arm64 was built.
- The CI artifact `FreeFileSync-14.13-macos-arm64.dmg`: not needed, the local build worked with a flag; CI builds with Xcode 26, so F1 does not affect it.
- Gatekeeper for FreeFileSync itself: the owner removed its quarantine flag before the first launch; RealTimeSync showed the behavior instead (F3).
- Shut down, sleep, Google Drive, SFTP, FTP: excluded by the test instructions.
- Hearing the beeps and the end-of-sync sound: nobody was at the Mac; the sound has indirect evidence only.
- Copy, delete and rename confirmation dialogs from the grid's context menu: not opened.
- Sentry scenarios `h` (two processes) and `o` (options dialog): skipped by the script on macOS (they need xdotool).
- "Color theme" setting in the Options dialog (app-level dark/light override): not tried; only the system appearance was switched.

## Test data and clean-up

All syncs used `/private/tmp/ffs-mac-test` (and an exFAT image there). `~/Library/Application Support/FreeFileSync` did not exist before the test; the one the test created was moved to `/private/tmp/ffs-mac-test/settings-from-test-run`. The two preference plists the apps created were removed. The test file put in the Trash was moved back out. `/Applications/FreeFileSync.app` and `/Applications/RealTimeSync.app` (this build) are still installed. The system appearance is back to dark.
