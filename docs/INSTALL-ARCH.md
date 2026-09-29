# Installing Edu-LM on Arch Linux (KDE)

Edu-LM is SDDM plus the quiz gate. The package is called `edu-lm`; it `provides` `sddm=0.21.0` and `display-manager` and `conflicts` with `sddm`. Binaries, `sddm.conf`, the `sddm` PAM service and the `sddm` user keep their upstream names.

For other distros see [the generic section](#other-distros-plain-cmake-install) at the end. Test plan for the first real run: [PHASE4-TEST.md](PHASE4-TEST.md).

## What the package installs

| Path | What |
|---|---|
| `/usr/bin/sddm`, `sddm-greeter*`, `edu-lm-validate`, `edu-lm-check` | daemon, greeter, pack validator, admin diagnostics |
| `/usr/share/sddm/themes/edu` | the child-friendly theme |
| `/usr/lib/sddm/sddm.conf.d/default.conf` | Arch's generated defaults (as the stock package) |
| `/usr/lib/sddm/sddm.conf.d/edu-lm.conf` | `[Theme] Current=edu` |
| `/etc/edu-lm/edu-lm.conf` | **comments only**: defaults and descriptions, never edit |
| `/etc/edu-lm/edu-lm.conf.d/` | your settings go here (`*.conf`) |
| `/etc/edu-lm/questions.d/` | your own question packs (mode 0700: they hold answer keys) |
| `/usr/share/edu-lm/questions/` | sample packs (mode 0700) |
| `/usr/lib/sysusers.d/edu-lm.conf` | groups `edu-students`, `edu-year3`, `edu-year4`, `edu-year5` |
| `/etc/pam.d/sddm`, `sddm-autologin`, `sddm-greeter` | PAM services, copies of Arch's sddm 0.21.0-7 files (upstream develop no longer ships them) |
| `/usr/share/edu-lm/examples/edu-lm-access.conf` | opt-in text-console lock, **not active** |

## 1. Copy the source and build

On the Arch PC, pull from the Pi (replace `pi` with its host name; the trailing slash matters; `build*/` are aarch64 build trees and must not travel):

```sh
rsync -a --exclude 'build*/' pi:projects/git/edu-lm/ ~/edu-lm/
```

On the Arch PC (never as root; `makepkg` asks for sudo itself):

```sh
cd ~/edu-lm/packaging/arch
makepkg -si
```

- The build is out of tree in `packaging/arch/src/` and never touches `build*/`. It runs the test suite (`check()`); add `--nocheck` to skip it.
- `.git` is used, if present, only to make the version (`0.21.0.r<commits>.g<hash>`). `makepkg` rewrites the `pkgver=` line in `PKGBUILD` when it does that.
- pacman asks to remove `sddm` (the conflict). Say yes. If you had edited `/etc/pam.d/sddm` (or the other two PAM files), pacman may keep your copy as a `.pacsave` or `.pacnew`; compare them with ours.
- Packages that depend on `sddm` (`sddm-kcm` does) are satisfied by the `sddm=0.21.0` provides.
- **Reboot (or `sudo systemctl restart sddm` from a text console) after installing or upgrading edu-lm.** pacman does not restart the running daemon, and an old daemon with the new greeter (or the reverse) leaves the quiz stuck.

## 2. Switch the display manager

`sddm.service` and `plasmalogin.service` both declare `Alias=display-manager.service` (read from the Arch packages' units), so only one of them can be enabled; LightDM's unit is expected to do the same. Check the current one:

```sh
readlink /etc/systemd/system/display-manager.service
```

**From LightDM:**

```sh
sudo systemctl disable lightdm.service
sudo systemctl enable --force sddm.service
```

**From Plasma Login Manager** (`plasmalogin.service`; `plasma-login-manager` does not conflict with or depend on `sddm`, so both can stay installed, but only one service can be enabled):

```sh
sudo systemctl disable plasmalogin.service
sudo systemctl enable --force sddm.service
```

`--force` replaces an existing `display-manager.service` link, which is what `enable` otherwise complains about. Then reboot (restarting the display manager from inside a graphical session would kill that session). From a text console you can instead run `sudo systemctl stop <old>.service && sudo systemctl start sddm.service`.

## 3. Children, groups, passwords

The groups are created when the package is installed (`getent group edu-students`; if empty, run `sudo systemd-sysusers`).

```sh
sudo usermod -aG edu-students,edu-year4 alice        # year 4
sudo usermod -aG edu-students,edu-year4,edu-year5 bob # works above their year: questions from both
```

Parents stay out of `edu-students` (or are listed in `ExemptUsers`). Settings live in a drop-in, never in `edu-lm.conf`:

```sh
sudo tee /etc/edu-lm/edu-lm.conf.d/10-site.conf <<'EOT'
[Quiz]
QuestionCount=5
RequiredCorrect=4
EOT
```

`/etc/edu-lm/edu-lm.conf` lists every key with its default (`#Key=value`). The main file is read **after** the drop-ins, so a key uncommented there silently beats them; `edu-lm-check` warns if you do.

**Passwords.** `StudentPasswords=true` (default): the child needs a password (`sudo passwd alice`); it is checked first, then the quiz.

`StudentPasswords=false`: the child has no password.

```sh
sudo passwd -d alice
echo -e '[Quiz]\nStudentPasswords=false' | sudo tee /etc/edu-lm/edu-lm.conf.d/20-passwords.conf
```

This needs `nullok` on `pam_unix` in the `sddm` auth chain. Arch's `pambase` ships `auth [success=1 default=bad] pam_unix.so try_first_pass nullok` in `system-auth`, which `sddm` reaches through `system-login`; `edu-lm-check` verifies your machine. Note that a blank password also works for the text console and `su`-style services that use `system-auth`, which is why the console lock below is worth enabling.

## 4. The theme drop-in and the override gotcha

Files are read in this order and the last one wins: `/usr/lib/sddm/sddm.conf.d/*`, `/etc/sddm.conf.d/*`, `/etc/sddm.conf`. Inside a directory the order is by file name. Our `edu-lm.conf` sorts after Arch's `default.conf` (which sets `Current=` empty), so it wins there.

It does **not** win over `/etc/sddm.conf.d/` or `/etc/sddm.conf`. Plasma's **System Settings, Login Screen (SDDM)** writes `/etc/sddm.conf.d/kde_settings.conf` (the `sddm-kcm` helper contains that path) and that beats us. If a theme there is not `edu`, quiz users are refused (the greeter cannot run the quiz: fail closed). Fix by editing or removing the `[Theme] Current=` line in that file. `edu-lm-check` names the file that overrides the theme.

## 5. Optional: lock text consoles (pam_access)

Without this a quiz user can press Ctrl+Alt+F3 and log in on a text console without a quiz.

```sh
sudo install -Dm644 /usr/share/edu-lm/examples/edu-lm-access.conf /etc/security/access.d/50-edu-lm.conf
```

The rule is `-:(edu-students):tty1 … tty12`. If you changed `Groups`, edit the group name. It only covers members of that group, not users listed in `Users`.

How it works, from Arch's packages (pambase 20260616-1, pam 1.7.2-2, util-linux 2.42.4-1, all read from the package files):

- `/etc/pam.d/login` (agetty text logins) includes `system-local-login`, which includes `system-login`, which has `account required pam_access.so`. So the rule applies to `login`.
- `sddm` and `sddm-autologin` reach the same `pam_access.so` line too (KDE's screen unlock does not: kscreenlocker only calls `pam_authenticate`, and `pam_access` is on the `account` line). SDDM must not be locked out, hence **never** use `ALL` or `LOCAL` as the origin. `pam_access` only knows a tty when `PAM_TTY` is set; SDDM's helper sets it only when opening the session (after the account stage), so at the account stage `pam_access` falls back to `ttyname(stdin)` and then to the service name (`sddm`), which no `ttyN` token matches. (pam 1.7.2 `pam_access.c`; whether the helper's stdin is a tty on the real system is checked in PHASE4-TEST.)
- `pam_access` reads `access.conf` first and `access.d/*.conf` (sorted by name) only when `access.conf` has no matching line. Arch's `access.conf` is all comments.
- Remote (ssh) logins are not covered; their origin is a host name.

## 6. Check it

```sh
sudo edu-lm-check
```

One line per check (`OK`, `INFO`, `WARN`, `FAIL`), exit status 1 on any `FAIL`. Without root it skips what needs root and says so. It checks: the enabled display manager, the effective SDDM theme and what overrides it, that the config loads and is valid, the groups, the quiz users, the question pools per year, question file ownership and modes, the `nullok` chain and password state, autologin, and the console lock. `EDU_LM_CHECK_ROOT=/some/copy` runs the checks against a copy of the system files (used by the tests).

## Rollback

From a text console (Ctrl+Alt+F3, log in as a parent):

```sh
sudo systemctl disable sddm.service
sudo systemctl enable --force lightdm.service       # or plasmalogin.service
sudo rm -f /etc/security/access.d/50-edu-lm.conf    # if you enabled the console lock
sudo pacman -S sddm                                  # answer y to remove the conflicting edu-lm
sudo reboot                                          # or: systemctl stop sddm, then start the old one
```

Do not start the old display manager while `sddm.service` is still running; stop sddm first or just reboot. Installing `sddm` swaps the packages in one step, so packages that depend on `sddm` (`sddm-kcm`) stay satisfied (removing `edu-lm` on its own would fail while they are installed).

- Config files you edited are kept by pacman as `.pacsave` next to the original path when `edu-lm` is removed; `/etc/edu-lm/` is yours to delete afterwards. Edu-LM changes no other system file; the groups it created stay (`sudo groupdel edu-students` to remove them).
- **Passwords.** Children who had `sudo passwd -d <child>` (`StudentPasswords=false`) still have empty passwords after rollback, and the console lock is gone. Set their passwords again (`sudo passwd <child>`) or lock the accounts (`sudo passwd -l <child>`).

## Other distros (plain cmake install)

Anything that ships SDDM with Qt 6 can build it. Nothing here needs Arch.

```sh
cmake -B build -S . -DBUILD_WITH_QT6=ON -DCMAKE_INSTALL_PREFIX=/usr \
      -DCMAKE_INSTALL_LIBEXECDIR=/usr/lib/sddm -DBUILD_MAN_PAGES=ON
cmake --build build && ctest --test-dir build
sudo cmake --install build
```

- Config, the theme drop-in, the sysusers file (when systemd is found) and the example install by themselves, at `<sysconfdir>/edu-lm`, `<prefix>/lib/sddm/sddm.conf.d/edu-lm.conf` and `<prefix>/share/edu-lm/examples`. `<sysconfdir>` is `/etc` with prefix `/usr`, but `/usr/local/etc` with prefix `/usr/local` (add `-DCMAKE_INSTALL_SYSCONFDIR=/etc` to force `/etc`). The paths come from `EDU_CONFIG_FILE`, `EDU_CONFIG_DIR`, `EDU_QUESTIONS_DIR` and `EDU_EXTRA_QUESTIONS_DIR` (cmake cache variables) and are absolute.
- Upstream SDDM no longer installs PAM services. Copy your distro's `sddm`, `sddm-autologin` and `sddm-greeter` (for Arch, `packaging/arch/pam/`) to `/etc/pam.d/`. For `StudentPasswords=false` the `sddm` auth chain needs `nullok` on `pam_unix`; `edu-lm-check` tells you whether it is there.
- Create the groups if no sysusers support: `groupadd edu-students; groupadd edu-year3; …`.
- Then enable `sddm.service` as in section 2. After any upgrade, reboot or `sudo systemctl restart sddm` from a text console.
