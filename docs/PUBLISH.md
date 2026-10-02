# Publish checklist

For the person creating the public repository. Nothing here has been done for you: there is no `origin` remote (only `upstream`, SDDM's repository), and nothing has been pushed. The phase 6 work should already be on `main`; check with `git log --oneline main`.

## 1. Create the repository

1. Create an empty public repository on any host (no README, no licence file, so the first push is clean).
2. Add it and push, for example `git remote add origin <url>` and `git push -u origin main`. An `upstream` remote (`https://github.com/sddm/sddm.git`) is already configured, for merging future SDDM releases (PLAN.md section 9).
3. Fill in the URL (it was not known when the files were written):
   - `Website=` in `data/themes/edu/metadata.desktop.in` (it still points at `https://github.com/sddm/sddm`).
   - The README's links that need a full URL, if you want any (the relative links work on any host as they are).
   - Optionally `url=` in `packaging/arch/PKGBUILD` (not set today).

## 2. Check what gets published

- Files that stay local, not tracked: `TODO.md` (listed in `.git/info/exclude`), `build-rel/` (`.git/info/exclude`), `build/` (`.gitignore`) and everything in `tools/qgen/work/` except its own `.gitignore` (`tools/qgen/work/.gitignore` ignores the rest). `git check-ignore -v TODO.md build build-rel tools/qgen/work/x` shows each rule. `.git/info/exclude` is not part of the repository, so on a fresh clone these are not ignored; they simply do not exist.
- `.claude/skills/qgen/` is tracked on purpose (the question-writing skill; README mentions it).
- `.claude/settings.local.json` is untracked and not published today, but only your global git ignore hides it, so a clone elsewhere would not ignore it. `.claude/settings.local.json` is now in the repo's `.gitignore`.
- Tracked-file scan for secrets, local paths, host names and personal data (`git grep` on 2026-10-02): no secrets, keys, tokens or passwords were found (matches were code identifiers and test dummies like `"secret"`). The only personal data in files written for Edu-LM:
  - `docs/INSTALL-ARCH.md` uses `pi` as a stand-in host name in an `rsync` example, and `docs/PROGRESS.md`, `docs/NEXT-SESSION.md` and `docs/PLAN.md` mention the Raspberry Pi dev machine. No real host names or addresses.
  - No `/home/<you>` paths. (`README.sddm.md` has `/home/username`, upstream's placeholder.)
  - Upstream's `CONTRIBUTORS` and file headers carry upstream authors' emails; leave those.
  The git name and email were removed from the tracked docs (a `git grep` for them finds nothing).

## 3. Commit history exposes your name and email

Every Edu-LM commit is authored and committed with your git name and email (see `git log`), and carries `Co-Authored-By: Claude ...` and `Claude-Session: https://claude.ai/code/session_...` lines. Pushing the history publishes these. The history was not rewritten. If you want a different identity or no session links, that means rewriting history (for example a fresh squashed commit on top of the upstream base) before the first push; it cannot be undone after.

## 4. Upstream's GitHub Actions

Done: `.github/workflows/` (SDDM's `build.yml` and `checks.yml`) was deleted from this repository. They only ran on `master` and `develop` pushes, and their matrix is mostly Qt 5, which the Qt 6-only Edu-LM theme and greeter glue would not build with (not tried), so on a fork they would have been dead or red. `checks.yml` also ran `qmllint` on pull requests to any branch.

Optional later: add one Qt 6 CI job (Ubuntu 24.04, `-DBUILD_WITH_QT6=ON`, `ctest`) once you have run it locally.

## 5. Licences, in short

See the README's licence table. Files: `LICENSE` (GPL-2.0-or-later, upstream's), `LICENSE.MIT` (edu theme QML), `LICENSE.CC-BY-3.0` (upstream's images), `LICENSE.CC-BY-SA-4.0` (question packs). ACARA's attribution is in the README; its terms were read on 2026-10-02 (`tools/qgen/spec-sources.md`).
