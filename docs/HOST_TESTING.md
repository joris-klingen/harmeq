# Host testing checklist

Things the automated tests can't cover: run these in Ableton Live (and any other host you use) after
installing a new build. Tick each one; note anything odd with the Live version and macOS version.

## Loading and state
- [ ] HarmEQ appears once in Live's browser (VST3 only; no leftover Audio Unit).
- [ ] Insert on an audio track, change a few bands, save the set, close and reopen it: everything comes
      back, including window size, display range and analyzer mode.
- [ ] Load a set saved with an older HarmEQ build: it opens, and MIDI Notes is *Last*.
- [ ] Duplicate the track: the copy sounds and looks identical.

## Presets
- [ ] Each factory preset loads from the menu under the HarmEQ name and the name shows there.
- [ ] *Save Preset...* writes a file to ~/Library/Audio/Presets/HarmEQ; it appears under *Yours* and
      loads back on another instance.
- [ ] Loading a preset doesn't change the window size.

## MIDI
- [ ] A MIDI track routed to HarmEQ (MIDI To → track → HarmEQ) moves the bands; notes and the readout match.
- [ ] Last / Lowest / Poly behave as described; Poly shows the chord in the top bar.
- [ ] Pitch bend moves the bands by the Bend range.
- [ ] Stopping playback with notes held: bands follow *No pitch* (Hold keeps them).

## Audio detection and sidechain
- [ ] Source = Main on a bass or vocal: the readout follows the notes, confidence meter goes green.
- [ ] Source = Sidechain with another track routed into HarmEQ's sidechain (device title bar → sidechain):
      bands follow that track. With nothing routed, *No sidechain signal* shows.
- [ ] Lookahead on: Live's track delay/latency readout shows the delay (≈ 40 ms at Detect 50 Hz); the
      track stays in time with the others (e.g. a click on both tracks lines up).
- [ ] Toggle Lookahead or change Detect low while playing: at most a small blip; timing re-aligns.
- [ ] Bypass the device (its power button) with Lookahead on: the track stays in time (no jump).

## GUI
- [ ] Dragging nodes, Shift-drag, mouse wheel, double-click and right-click work as in the README.
- [ ] Automation: moving a node while Live records automation writes the parameter lanes.
- [ ] Resizing the window is smooth; reopening keeps the size.
- [ ] The display keeps up while notes change quickly; no stutter in Live's own UI.

## Performance and stability
- [ ] CPU meter: a few instances on tracks with Poly and the analyzer on stay reasonable.
- [ ] Changing the audio interface sample rate (44.1 ↔ 48 ↔ 96 kHz) while the set is open: HarmEQ keeps
      working and Lookahead latency updates.
- [ ] Freeze and flatten a track using HarmEQ: the rendered audio matches what you heard.
