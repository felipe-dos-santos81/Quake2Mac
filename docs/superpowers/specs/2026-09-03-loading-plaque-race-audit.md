# Loading-Plaque Race Audit — 2026-09-03

Status: **audit + precautionary hardening**. One user occurrence
(2026-09-02, retry worked); not reproducible in 19 constructed headless runs.
Two latent vanilla-inherited fragilities documented below, plus a diagnostic
recipe for the next recurrence. The F1 hardening (per-frame re-check of the
plaque end condition in `CL_Frame`, `client/main.c`) landed the same day as a
precaution, although the bug was never reproduced.

## Symptom

Changing the resolution from the video menu during the attract loop resized
the window correctly but left the UI showing only the "Loading" plaque.
Boot log showed the attract transition starting (`Changing map...`,
`reconnecting...`) and then nothing further.

## Mechanics (load-bearing facts)

- Attract aliases `d1`–`d4` ship inside `pak0.pak` (`default.cfg`); the game
  always enters the attract loop unless explicit `+` commands are given.
- Menu apply only sets cvars; the ref reload comes from
  `ref_gl/rmain.c` `R_BeginFrame` (~line 1215): `gl_mode->modified ||
  vid_fullscreen->modified` → `vid_ref->modified = true`, then
  `VID_CheckChanges` (`platform/sdl/vid.c`) reloads the renderer and sets
  `cl.refresh_prepped = false`.
- Attract transitions: `SV_Map` (`server/init.c`) broadcasts `changing`,
  spawns, broadcasts `reconnect`; client `CL_Changing_f`/`CL_Reconnect_f`
  (`client/main.c`) show the plaque and send a single `new`.
- `ss_demo`: the server streams the recorded handshake from the `.dm2` (one
  message per `SV_Frame`, ~10 Hz); `SV_New_f` merely reopens the file and
  `SV_Begin_f` (`server/user.c`) skips the spawncount check and rewinds to
  `sv.demo_frame_pos`. `ss_cinematic`: `SV_New_f` sends live serverdata
  (`playernum -1`), protected by reliable-netchan retransmission.
- Plaque: `SCR_BeginLoadingPlaque` stores a timestamp in `cls.disable_screen`
  plus `cls.disable_servercount`; `SCR_UpdateScreen`
  (`client/screen/scrn.c` ~1365) draws nothing while `disable_screen` is set,
  with a 120 s timeout ("Loading plaque timed out.").

## Latent fragilities found (vanilla-inherited, untouched)

1. **One-shot plaque end vs `refresh_prepped`.** The plaque ends only at the
   first valid frame (`client/net/ents.c` ~586), and only if
   `cls.disable_servercount != cl.servercount && cl.refresh_prepped`. A ref
   reload landing between precache completion (`CL_PrepRefresh` at the end of
   the precache machine, `client/main.c` ~1352) and the first frame leaves
   the plaque up over a *live* game for the full 120 s timeout. The only
   mechanism found that produces the symptom. Hardened 2026-09-03:
   `CL_Frame` re-evaluates the end condition every frame, so a late re-prep
   still ends the plaque.
2. **`CL_ClearState` eats an unsent `new`.** `CL_ClearState`
   (`client/main.c` ~597) clears the outgoing reliable buffer; a streamed
   demo serverdata arriving between `CL_Reconnect_f` queueing `new` and
   `CL_SendCmd` transmitting it silently discards the `new`. Benign — the
   demo handshake is stream-driven.

## Reproduction attempts (all clean)

19 headless runs with `+set developer 1 +set logfile 2`: reloads at boot
offsets, reloads hooked exactly at the cin↔demo attract transitions (temp
`d2`/`d3` alias overrides: same-batch, post-spawn, reload-then-transition,
mid-handshake), 135 s each. Every run completed the handshake
(`Begin() from Player` in the log), zero plaque timeouts, screenshots showed
live play. Note: a healthy demo transition prints almost nothing for 10–20 s
after `reconnecting...`, so a live log can read as "stuck" while progressing.

## Diagnostic recipe for a recurrence

```sh
./build/quake2 +set developer 1 +set logfile 2   # reproduce the change
```

In `baseq2/qconsole.log`, after `reconnecting...`:

- `Begin() from Player` present but plaque stuck → fragility 1 confirmed; a
  hardening patch (end the plaque whenever `ca_active && refresh_prepped &&
  disable_screen`, not only on the one-shot frame) is then justified.
- `Begin()` absent → genuine handshake stall; the developer log shows which
  end stopped.
