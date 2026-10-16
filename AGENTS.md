# Instructions for AI agents working on mnKord

These rules apply to every change an agent makes in this repository (Claude Code, Codex, Cursor, ...).
The project's owner doesn't manage versions by hand: **keeping the version number and the changelog
right is the agent's job**, as part of every change.

## Build and test

```bash
cmake -S . -B build -DMNKORD_BUILD_TESTS=ON
cmake --build build --config Release
build/mnKordTests        # Linux / single-config generators; Visual Studio: build\Release\mnKordTests.exe
```

Build and run the tests before every commit; they must end with `0 failures`. The configure step prints
`mnKord version: ...` and warns if the version and the changelog disagree (see below).

## Versioning (Semantic Versioning: MAJOR.MINOR.PATCH)

- The version lives in **one place**: `project(mnKord VERSION x.y.z)` in `CMakeLists.txt`.
  Don't write version numbers anywhere else in the code; `MnKordVersion.h` is generated from it.
- Between releases that number is **the next release**, and `CHANGELOG.md` has a section
  `## [Unreleased] - x.y.z` with the same number.
- The last release is the newest `## [x.y.z] - YYYY-MM-DD` section in `CHANGELOG.md` (and the git tag
  `vx.y.z`, once tags exist).

### With every change

1. Add a line for it under `## [Unreleased] - x.y.z` in `CHANGELOG.md`, in the right group: **Added**,
   **Changed**, **Fixed** or **Removed**. Write it for a user of the plugin (what they will notice), not
   about the code. Purely internal work (refactoring, tests, build scripts) gets no line unless users notice it.
2. Set the version to **the last release + the largest kind of change in the Unreleased section**.

   - **MAJOR** (`1.4.2` -> `2.0.0`): projects or preset files saved by an earlier version don't load or
     don't play the same, or a feature users rely on is removed.
     **Ask the owner before making a MAJOR bump**, and say what breaks.
   - **MINOR** (`1.4.2` -> `1.5.0`): a feature is added, or one changes how it behaves, and older
     projects and presets still work.
   - **PATCH** (`1.4.2` -> `1.4.3`): only bug fixes.

   The version moves at most once per kind of change in a release cycle: three fixes after `1.0.0` are
   still `1.0.1`; a feature after them makes it `1.1.0` (not `1.0.2`).
   The first release was `1.0.0`; going to `2.0.0` needs the owner's say-so (see MAJOR above).
3. Update the number in **both** `CMakeLists.txt` and the `## [Unreleased] - x.y.z` heading.
4. Say in your reply to the owner which version the change led to, and why.

### Releasing (only when the owner asks)

1. In `CHANGELOG.md`, turn `## [Unreleased] - x.y.z` into `## [x.y.z] - YYYY-MM-DD` (today's date).
2. Merge to `master`. Then tag that commit and push the tag:
   `git tag -a vx.y.z -m "mnKord x.y.z"` and `git push origin vx.y.z`.
3. The next change after the release starts a new `## [Unreleased] - x.y.z` section at the top.

Never create, move or delete tags, or push them, unless the owner asks.

## Where the version shows

- The plugin window, next to the title: `v1.0.0` for a release build, `v1.0.0-dev+<commit>` for any
  other build (the suffix follows the commit by itself; see `cmake/Version.cmake`).
- The plugin's metadata (VST3 version, `JucePlugin_VersionString`), from the same CMake number.

## Documentation

- The user guide is in `docs/USER_GUIDE.md` (English) and `docs/USER_GUIDE.hu.md` (Hungarian), with the
  screenshots in `docs/images/`. When a change alters what the guide says (a control, a menu, how something
  behaves), update **both** languages.
- The PDFs (`docs/mnKord-User-Guide.pdf`, `docs/mnKord-Felhasznaloi-utmutato.pdf`) are generated from the
  Markdown and go into the installer: after changing a guide, run `python docs/build-pdf.py` (needs Python 3
  and Microsoft Edge or Chrome) and commit the PDFs with it.
- A screenshot that no longer matches the window (a changed look, labels or menus) is retaken, not left.
