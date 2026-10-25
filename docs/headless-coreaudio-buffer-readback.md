# CoreAudio buffer readback and AOO receive overruns

Verified on macOS with the local Headless SonoBus build, 2026-10-25.

## Cause

The AirPods output driver accepted a request for 128 frames but reported and
delivered 256 frames per callback. `CoreAudioInternal::reopen()` correctly read
back 256 and allocated its temporary buffers, then overwrote `bufferSize` with
the requested 128. The callback subsequently rendered and copied only half of
each hardware output buffer.

The main music sink (sink 1, source 3, stereo Opus, 960 frames/block at 48 kHz)
received approximately 47,890 PCM frames/s but processed only 24,000 frames/s.
In a 27.3-second observation, it overflowed 63 times, clearing 11 queued network
blocks per overflow. The old "dropped 0 blocks" message counted inserted
concealment blocks, not the queued network blocks discarded before that log.

## Fix

Keep the frame size returned by `updateDetailsFromDevice()` instead of replacing
it with the request. The existing sample-rate workaround is preserved.

No requested device buffer, jitter buffer, resampling mode, codec, network target,
input-device choice, Bach Tap code, or Bach signature was changed. On this tested
route the effective device buffer is 256 frames (5.33 ms at 48 kHz); the persisted
request remains 128.

The diagnostic build confirmed `configured_frames=256 actual_frames=256`.
Temporary logging inside the hardware callback was removed from the final build.
Low-frequency `AOO_RX_FLOW` reports run on the network thread; the audio thread
only increments an atomic frame counter. Overrun/underrun logs identify the sink
and source, and overruns report the queue size before it is cleared. Existing
loss-counter and automatic buffer behavior are otherwise unchanged.

## Verification

- A real Windows music stream was used throughout; no test tone was generated.
- A 30-second fixed-build observation recorded zero main-stream overruns and
  zero underruns. Received rate: 47,964 frames/s; processed rate: 48,001 frames/s.
  The network queue stayed at or below 3 blocks instead of reaching its 11-block limit.
- After removing the temporary callback prints, the final deployed build passed
  another 20-second observation: zero overruns/underruns and 48,001 processed frames/s.
- The user confirmed Headless direct output to AirPods was normal, with no
  stuttering or crackling, after releasing Bach's existing Tap through its Stop button.
- Local builds and stable Apple Development signing passed. Bundle ID, Team,
  authority chain, designated requirement, entitlements, installation path,
  LaunchAgent plist, and setup XML were preserved. TCC was not reset.
- Original user changes in the standalone app, processor, and GUI module remain
  outside this change.

Raw local evidence: `/private/tmp/sonobus-crackle-20261025/`.
