# Phase 4 test guide: first real run on the Arch PC

Everything below has only been checked by review and compile so far: the real-PAM success path of `AuthVerifier`, `Display`'s replay into a real session, the autologin skip, the greeter's real-mode wiring, `nullok` with empty passwords, and the `pam_access` interplay. Work through it top to bottom on the PC that has the package installed ([INSTALL-ARCH.md](INSTALL-ARCH.md)). Keep a second machine or phone to read this, and know how to reach a text console (Ctrl+Alt+F3, as a parent) or how to roll back.

**Capture throughout:** after each numbered step where it matters, `journalctl -b --no-pager | grep -iE 'edu-lm|sddm' > step-NN.log` (the greeter's messages may be logged outside the `sddm` unit, so `-u sddm` alone can miss them; also keep `journalctl -b -u sddm`), and a phone photo or screenshot of the screen. Edu-LM lines start with `Edu-LM:`. If a step fails, stop and send the log of that step first.

**Setup used below:** child `alice` (groups `edu-students`, `edu-year4`) with a password; child `bob` (`edu-students`) with **no** password (used in step 11 only); parent `dad`, not in any quiz group. Config: defaults (`QuestionCount=5`, `RequiredCorrect=4`, `FailCooldownSeconds=60`, `StudentPasswords=true`). Write down which of the two PCs (LightDM, or sddm and Plasma Login Manager) you are on. **Reboot (or `sudo systemctl restart sddm` from a text console) after installing or upgrading edu-lm**: pacman does not restart the running daemon, and an old daemon with the new greeter wedges the quiz.

## A. Before rebooting

1. **Static check.** `sudo edu-lm-check`. Expect no `FAIL`. Warnings for a not-yet-locked console are `INFO`. Capture the whole output. If it says the display manager is not `sddm.service`, do section 2 of INSTALL-ARCH first.
2. **Files.** `ls -ld /etc/edu-lm /etc/edu-lm/edu-lm.conf.d /etc/edu-lm/questions.d /usr/share/edu-lm/questions` and `sudo ls -l /usr/share/edu-lm/questions`. Expect `/etc/edu-lm` (no `/usr/etc`), `questions.d` and `questions` mode `drwx------` root, packs `-rw-------` root.
3. **Reboot** (`sudo reboot`). Expect the **edu** theme login screen (cheerful, avatar grid or "Other…"). If you see a Breeze-style or other screen: `sudo edu-lm-check` names the file overriding the theme; this is the `kde_settings.conf` gotcha.

## B. Password and quiz, real PAM

4. **Wrong password.** Pick `alice`, type a wrong password, **Next/Log in**. Expect: back on the password page with "That password didn't work. Please try again.", no quiz, no cooldown. Journal (`journalctl -b | grep -i edu-lm`): no `quiz started`. This exercises `AuthVerifier` failure through real PAM.
5. **Right password, Wayland session (primary).** Pick `alice`, choose the **Plasma (Wayland)** entry in the session selector at the bottom of the login page, and enter the right password. Expect the quiz starts ("Question 1 of 5"), journal `Edu-LM: quiz started for "alice"`. Answer at least 4 correctly (use Swap once to check it works). Expect "Well done!" then "Logging you in…" and a **real Plasma session** for alice. Confirm: `loginctl list-sessions` shows alice, and `loginctl show-session <id> -p Type -p Class` says `Type=wayland`; journal `Edu-LM: quiz passed by …`. This is the first real run of `AuthVerifier` success and of `Display`'s replay (`startAuth`). Log out. Capture: screenshot of the quiz and the desktop, the journal. If the session fails to start, capture the journal and `~/.local/share/sddm/wayland-session.log`.
6. **X11 Plasma session (only if `plasma-x11-session` is installed; on Arch it is a separate package, and Plasma 6.8 drops X11).** Repeat step 5 choosing the Plasma (X11) entry. Expect the same with `Type=x11`. If there is no X11 entry, skip this step.
7. **Fail and cooldown.** As `alice` with the right password, answer so that fewer than 4 are right (press Next after each miss). Expect "Not this time" with each missed question, your answer and the right answer, then "Time for a little break / Try again in 1:00" counting down and a return to the login screen at 0. Journal: `quiz failed by "alice"`.
    - **Answer shown on the spot, last question wrong but passing.** As `alice` with the right password, answer the first four questions correctly and the fifth wrongly (4 of 5 passes). Expect "Not quite", then "The answer is: …" (and an explanation if the question has one) and a **Next** button, and nothing else: the screen stays as long as you like and the session does **not** start. Press Next (or Enter): "Well done!" then the session starts. Repeat with a wrong answer on an earlier question: Next shows the following question. A quick second Enter right after your answer must not skip it: Next ignores presses for about half a second. Journal: `quiz passed by` appears only after Next.
    - **Answers hidden.** `printf '[Quiz]\nShowCorrectAnswers=false\n' | sudo tee /etc/edu-lm/edu-lm.conf.d/30-hide.conf`, then miss a question as `alice`: expect a brief "Not quite" with no answer and no Next button, moving on by itself after a second. Remove `30-hide.conf` afterwards.
8. **Cooldown survives leaving and a greeter restart.** Fail again as in step 7 and press Back on the break screen straight away, then pick `alice` again and enter the right password. Expect the break screen with the **remaining** time, not a new quiz. Then kill the greeter only: note `systemctl show -p MainPID sddm`, run `sudo pkill -f /usr/bin/sddm-greeter-qt6`, wait for the login screen to come back (whether SDDM restarts the greeter that way is not verified; if it does not, skip this sub-step), check `MainPID` is unchanged, pick alice again and expect the remaining time again. The cooldown lives in the daemon's memory, so a daemon restart or reboot clears it by design.
9. **Non-quiz user unaffected.** Log in as `dad`. Expect no quiz screen, straight into the session, the same as stock SDDM. `journalctl -b | grep -i edu-lm` has no `quiz started` for dad.
10. **Abandon.** As `alice` right password, answer one question (if it was wrong, press Next first: Back is ignored while the answer is shown), press Back and confirm. Expect it to count as a failure (cooldown). Then wait it out. As a check, start a quiz and press Back **before** answering anything: no cooldown.

## C. No-password students

11. **`StudentPasswords=false` with nullok.**
    ```sh
    sudo passwd -d bob
    echo -e '[Quiz]\nStudentPasswords=false' | sudo tee /etc/edu-lm/edu-lm.conf.d/20-passwords.conf
    sudo edu-lm-check        # expect OK for nullok; alice WARNs (she has a password but StudentPasswords=false), so the OK line for matching passwords is not printed
    ```
    The config is re-read on every login, no restart needed. On the login screen pick `bob`: expect **no password field**, straight to the quiz, and after a pass a real session. If bob is refused with a wrong-password message, capture `journalctl -b | grep -iE 'edu-lm|sddm'` and `journalctl -b | grep -i pam_unix`; expected cause is a missing `nullok` (`edu-lm-check` would have said so). Afterwards set it back if you want passwords: remove `20-passwords.conf`.

## D. Guard rails

12. **Stock theme fails closed.** Temporarily set another theme: `sudo mkdir -p /etc/sddm.conf.d` (it may not exist on a LightDM PC) and `printf '[Theme]\nCurrent=breeze\n' | sudo tee /etc/sddm.conf.d/zz-test.conf` (or `maldives`), restart the greeter (`sudo systemctl restart sddm` from a console). Expect: `alice` cannot log in (an error, or the login just fails; the journal has `Edu-LM: refusing login of "alice" - the greeter does not support the quiz`), while `dad` logs in normally. Remove `zz-test.conf` and restart sddm afterwards; confirm `edu-lm-check` is clean.
13. **Autologin skip.** `sudo mkdir -p /etc/sddm.conf.d` and `printf '[Autologin]\nUser=alice\nSession=plasma\n' | sudo tee /etc/sddm.conf.d/zz-autologin.conf` (in Plasma 6 `plasma` is the Wayland session and `plasmax11` the X11 one; the file names are in `/usr/share/wayland-sessions` and `/usr/share/xsessions`), restart sddm. Expect: the login screen appears (no automatic login) and the journal has `Edu-LM: autologin disabled, the account needs the quiz`. `sudo edu-lm-check` shows a WARN about it. Then set `User=dad`: expect dad to log in automatically. Remove the file.

## E. Text console lock (pam_access)

14. **Before enabling.** As alice (after passing the quiz, or with a password) nothing to check; just note that on Ctrl+Alt+F3 the `login:` prompt accepts alice (with `StudentPasswords=true`, her password).
15. **Enable and confirm the origin.** `sudo install -Dm644 /usr/share/edu-lm/examples/edu-lm-access.conf /etc/security/access.d/50-edu-lm.conf`, then `sudo edu-lm-check` (expect an `INFO` line saying a console rule for the quiz group was found; the tool never claims the console is locked, the login below is the real proof).
    - Ctrl+Alt+F3, log in as `alice`: expect refusal ("Permission denied" after the password), and `journalctl -b | grep pam_access` shows ``access denied for user `alice' from `tty3'``.
    - Log in as `dad` on the console: works.
16. **SDDM still works with the lock on.** Back on the graphical login (Ctrl+Alt+F1 or F2): repeat step 5 and step 9 (alice through the quiz to a session, dad directly). Expect both to work. This proves the account stage sees an origin that is not `ttyN`. To see the origin, temporarily add the `debug` argument to the `pam_access.so` line of `/etc/pam.d/system-login`, log in via SDDM, then `journalctl -b | grep pam_access`. Expected: `from=sddm` (or `sddm-autologin`), never `tty…`. If it shows `tty1`..`tty12`, SDDM's helper has a tty on stdin: stop, remove `50-edu-lm.conf`, and report (that would need a daemon change, which this phase does not include). Remove `debug` afterwards.
17. **Screen unlock (sanity).** Log in as alice, lock the screen (Meta+L) and unlock with the password. Expect it works. (KDE's lock screen only calls `pam_authenticate`, so `pam_access`, an account-stage module, is not involved.)
18. **Rollback of the lock.** `sudo rm /etc/security/access.d/50-edu-lm.conf` and confirm alice can log in on tty3 again.

## F. Greeter wiring

19. **Real mode vs test mode.** Steps 5 and 7 already prove the real path: the greeter (`sddm-greeter-qt6`, user `sddm`) opened the second socket to the live daemon, got `QuizReady`, and questions came from the daemon (they are from `/usr/share/edu-lm/questions`, not the built-in sample of test mode). To double-check that no mock is in use: `ps aux | grep sddm-greeter` shows no `--test-mode`, and `journalctl -b | grep -i "quiz connection"` shows no `too much` or `unreadable` warnings.
20. **Go back after a stuck login.** On the "One moment…" screen "Taking too long? Go back" appears after about 10 s. Not expected to be seen in normal use; if you see it during steps 4 to 11, capture the journal.

## Report back

For each step: pass, fail, or "not as expected" with the journal and a photo. Especially: 5 and 6 (first real session start), 11 (nullok) and 16 (pam_access origin).
