# PS4 audio buffering and AudioOut lifetime

The PS4 pipeline reserves enough source-ring space to prime the selected device latency, including audio-sync mode. New PS4 configurations use 128 ms output latency and ordinary 0.005 dynamic rate control. Existing saved settings remain authoritative. After an actual synchronized-stream underrun with rate control disabled (or delta zero), the pipeline rebuilds its reserve without discarding source samples; silence generated while priming is acknowledged to avoid repeated priming. Both the ordinary and pitch-preserving consumer paths use the policy. Active rate control refills gradually; it does not trigger this silence-producing re-prime, so a small gap cannot be extended by forced recovery.

The AudioOut worker drains its last queued window on stop, publishes the retired read position, and closes the port before freeing ring storage. This addresses an asynchronous-buffer lifetime defect during pause/restart and driver reinitialization. It is not yet a confirmed explanation of the user's hardware silence after a latency change.

Run outside the source tree:

```
python3 tests/ps4_audio_buffer/run.py --sanitize --output /tmp/ps4-audio-policy
python3 tests/ps4_audio_buffer/lifecycle.py --baseline --output /tmp/ps4-audio-lifetime-before
python3 tests/ps4_audio_buffer/lifecycle.py --output /tmp/ps4-audio-lifetime-after
```

The policy test extracts the real ordinary consumer, source-fill measurement, rate controller and buffering helpers and uses the real SPSC queue. A deterministic 48 kHz/512-frame fake device drives 60 Hz, a one-second 55 Hz interval, repeated short slow intervals and a 250 ms producer stall. Rendered source samples must remain ordered and accounted for. Device scheduling and resampling output counts are modeled: these are not PS4 playback measurements, waveform quality tests, or a full threaded frontend run. Both consumer call sites are checked; the stretch engine itself is not exercised here.

In the deterministic schedules, Y has two silent periods during the one-second slow interval; the 128 ms candidate has none. Repeated short slow intervals also have none. A long 250 ms stall still causes silence. With legacy 64 ms/no-rate-control settings, recovery reduces separate gap episodes (9 to 4) but increases total silent periods (24 to 30) while rebuilding the reserve. This is a recovery tradeoff, not a promise to hide sustained under-speed playback. The source code does not increase the configured DRC limit or implement automatic pitch-preserving slowdown.

The lifetime test compiles the actual PS4 driver with the repository's asynchronous AudioOut mock. It asserts the device has released the queued window before freeing storage and after stop, and checks sample continuity over stop/start. The W/Y baseline fails the first stop's retained-window assertion; the candidate passes 40 latency-change lifetimes (64/128/96/160 ms) under ASan/UBSan. The earlier exploratory mock allowed a stale pointer to survive port close; its UAF trace is not evidence of the console's exact close behavior. The checked lifetime contract is the authoritative regression test.

All native outputs remain outside the production object tree. PS4 hardware confirmation is required for continuity, lip-sync, menu transitions and the reported intermittent silence. A saved 64 ms configuration will still need to be changed to 128 ms; the default change does not overwrite user configuration.
