/* Copyright (C) 2026 - SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio/audio_driver.h"

#define AUDIO_PIPE_WAIT_MAX_US 100000
static bool legacy, stalled;
static double now, next_publish, next_output, end_time;
static double device_frames, fractional_output;
static unsigned scenario, gaps, later_gaps, recoveries, episodes;
static bool was_gap;
static size_t hardware_underruns, published, rendered;
static audio_driver_state_t *state;
static uint32_t scratch[4096];
static void advance_event(void);
static size_t fake_underruns(void *data) { return hardware_underruns; }
static size_t fake_avail(void *data)
{
   double free_frames = state->buffer_size / 4 - 1 - device_frames;
   return (size_t)(free_frames > 0 ? free_frames : 0) * 4;
}
static size_t fake_wait(void *data, size_t need)
{
   if (fake_avail(data) < need) advance_event();
   return fake_avail(data) >= need ? fake_avail(data) : 0;
}
static audio_driver_t driver;
#include "policy.h"
#include "sample_fill.h"

/* Timing/event plumbing is fake; policy, rate adjustment, source queue and
 * the ordinary consumer body below are compiled directly from the source. */
static void audio_driver_pipeline_retry(audio_driver_state_t *st) { assert(0); }
static void audio_driver_pipe_note_held(audio_driver_state_t *st, size_t held) {}
static void audio_driver_pipe_note_wait(audio_driver_state_t *st, retro_time_t t) {}
static float audio_driver_snapshot_slowmotion(audio_driver_state_t *st) { return 1.0f; }
static double audio_driver_ff_mult(audio_driver_state_t *st, size_t n) { return 1.0; }
static double audio_driver_effective_ratio(audio_driver_state_t *st,
      bool slow, float ratio, double ff)
{
   return 1.005; /* The real preflight's worst case at the default DRC bound. */
}
static bool audio_driver_pipe_fade_pending(audio_driver_state_t *st,
      size_t *at, unsigned *seq) { return false; }
static void audio_driver_pipeline_pass_done(audio_driver_state_t *st, bool done) {}
static void audio_driver_reset_resamplers(audio_driver_state_t *st) {}
void audio_driver_state_lock(void) {}
void audio_driver_state_unlock(void) {}
retro_time_t cpu_features_get_time_usec(void) { return (retro_time_t)(now * 1e6); }
int retro_eventcount_prepare_wait(retro_eventcount_t *ec) { return 0; }
void retro_eventcount_cancel_wait(retro_eventcount_t *ec) {}
bool retro_eventcount_commit_wait_timeout(retro_eventcount_t *ec, int key,
      int64_t timeout_us) { advance_event(); return true; }
static void audio_driver_pipeline_render(audio_driver_state_t *st,
      const void *source, size_t frames, uint32_t layout, int snap)
{
   const uint32_t *pcm = (const uint32_t*)source;
   size_t i;
   double ratio = 1.0, output;
   for (i = 0; i < frames; i++)
      assert(pcm[i] == ++rendered); /* No dropped, reordered or repeated source. */
   if (AUDIO_FLAGS_GET(st) & AUDIO_FLAG_CONTROL)
      ratio = audio_driver_compute_rate_adjust(st);
   output = frames * ratio + fractional_output;
   fractional_output = output - floor(output);
   device_frames += floor(output);
   assert(device_frames < st->buffer_size / 4);
}
/* The old consumer is reproduced only by suppressing the newly added hook. */
static void check_underrun(audio_driver_state_t *st, int snap)
{
   bool was = st->pipe_priming;
   if (!legacy) audio_driver_ps4_check_underrun(st, snap);
   if (!was && st->pipe_priming) recoveries++;
}
#define audio_driver_ps4_check_underrun check_underrun
#include "consumer.h"
#undef audio_driver_ps4_check_underrun

static void setup(audio_driver_state_t *st, unsigned latency, bool control)
{
   memset(st, 0, sizeof(*st));
   memset(&driver, 0, sizeof(driver));
   driver.underruns = fake_underruns;
   driver.write_avail = fake_avail;
   driver.wait_writable = fake_wait;
   st->current_audio = &driver;
   st->context_audio_data = st;
   st->pipe_frame_bytes = 4;
   st->pipe_pass_frames = 800;
   st->pipe_channels = 2;
   st->pipe_scratch = (uint8_t*)scratch;
   st->output_samples_buf = (float*)scratch;
   st->src_ratio_orig = 1.0;
   st->rate_control_delta = 0.005;
   st->pipe_threaded = true;
   st->buffer_size = ((48000 * latency / 1000 + 511) / 512) * 512 * 4;
   st->pipe_priming = true;
   retro_atomic_int_init(&st->flags, AUDIO_FLAG_ACTIVE | (control ? AUDIO_FLAG_CONTROL : 0));
   retro_atomic_int_init(&st->runloop_snapshot, AUDIO_SNAP_SYNC);
   retro_atomic_int_init(&st->pipe_ctrl_avail, -1);
   assert(retro_spsc_init(&st->pipe_ring,
         4 * (2400 + (legacy ? 0 : 48000 * latency / 1000))));
   state = st;
   now = next_publish = next_output = device_frames = fractional_output = 0;
   gaps = later_gaps = recoveries = episodes = 0;
   was_gap = false;
   hardware_underruns = published = rendered = 0;
   stalled = false;
}
static void publish_frame(void)
{
   uint32_t samples[800];
   unsigned i;
   sample_fill(state);
   for (i = 0; i < 800; i++) samples[i] = ++published;
   assert(retro_spsc_write(&state->pipe_ring, samples, sizeof(samples)) == sizeof(samples));
}
static void advance_event(void)
{
   if (next_output <= next_publish)
   {
      now = next_output;
      next_output += 512.0 / 48000.0;
      if (device_frames >= 512)
      {
         device_frames -= 512;
         was_gap = false;
      }
      else
      {
         hardware_underruns++;
         if (now > 1) { gaps++; if (!was_gap) episodes++; }
         was_gap = true;
         if (now > 4) later_gaps++;
      }
   }
   else
   {
      double interval = 1.0 / 60.0;
      now = next_publish;
      publish_frame();
      if (scenario == 1 && now >= 2 && now < 3) interval = 1.0 / 55.0;
      if (scenario == 2 && now >= 2 && !stalled) { interval = 0.250; stalled = true; }
      if (scenario == 2 && now >= 5 && fmod(now - 5, 1.0) < 0.017) interval = 0.028;
      if (scenario == 3 && now >= 2 && fmod(now - 2, 4.0) < 0.350) interval = 1.0 / 55.0;
      next_publish += interval;
   }
   if (now >= end_time)
      retro_atomic_store_release_int(&state->pipe_wake, 1);
}
static unsigned run_case(const char *name, unsigned which, unsigned latency,
      bool control, bool old, double duration)
{
   audio_driver_state_t st;
   legacy = old;
   setup(&st, latency, control);
   scenario = which; end_time = duration;
   while (now < end_time)
      audio_driver_pipeline_consume(&st);
   printf("%s: silent_periods=%u late_silent_periods=%u recoveries=%u episodes=%u source_frames=%lu\n",
         name, gaps, later_gaps, recoveries, episodes, (unsigned long)rendered);
   assert(published >= rendered);
   assert((published - rendered) * 4 == retro_spsc_read_avail(&st.pipe_ring));
   retro_spsc_free(&st.pipe_ring);
   return gaps;
}
static void unit_cases(void)
{
   audio_driver_state_t st;
   uint32_t samples[800];
   unsigned i, mask;
   size_t head, tail;
   setup(&st, 64, false);
   for (i = 0; i < 800; i++) samples[i] = i + 1;
   assert(retro_spsc_write(&st.pipe_ring, samples, sizeof(samples)) == sizeof(samples));
   head = retro_atomic_load_acquire_size(&st.pipe_ring.head);
   tail = retro_atomic_load_acquire_size(&st.pipe_ring.tail);
   st.pipe_priming = false;
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   assert(!st.pipe_priming);
   hardware_underruns++;
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   assert(st.pipe_priming);
   assert(head == retro_atomic_load_acquire_size(&st.pipe_ring.head));
   assert(tail == retro_atomic_load_acquire_size(&st.pipe_ring.tail));
   hardware_underruns += 10; /* Silence while waiting for the prime. */
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   st.pipe_priming = false;
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   assert(!st.pipe_priming);
   for (mask = AUDIO_SNAP_PAUSED; mask <= AUDIO_SNAP_FASTMOTION; mask <<= 1)
   {
      hardware_underruns++;
      audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC | mask);
      assert(!st.pipe_priming);
      audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
      assert(!st.pipe_priming);
   }
   retro_atomic_store_release_int(&st.core_silenced, 1);
   hardware_underruns++;
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   assert(!st.pipe_priming);
   retro_atomic_store_release_int(&st.core_silenced, 0);
   hardware_underruns++;
   audio_driver_ps4_check_underrun(&st, 0); /* Non-sync owner retains its event. */
   assert(!st.pipe_priming && st.pipe_underruns_seen != hardware_underruns);
   st.context_audio_data = NULL;
   audio_driver_ps4_check_underrun(&st, AUDIO_SNAP_SYNC);
   assert(!st.pipe_priming);
   retro_spsc_free(&st.pipe_ring);
   puts("PASS: startup, lossless recovery, prime silence, pause/menu, fast/slow, non-sync, teardown");
}
int main(void)
{
   unsigned before, after;
   setvbuf(stdout, NULL, _IONBF, 0);
   unit_cases();
   assert(run_case("steady_Y", 0, 64, false, true, 12) == 0);
   assert(run_case("steady_candidate", 0, 128, true, false, 12) == 0);
   before = run_case("55fps_burst_Y", 1, 64, false, true, 12);
   after = run_case("55fps_burst_candidate", 1, 128, true, false, 12);
   assert(before > after && after == 0);
   before = run_case("hard_stall_Y", 2, 64, false, true, 12);
   before = episodes;
   run_case("hard_stall_candidate_old_settings", 2, 64, false, false, 12);
   assert(before > episodes);
   run_case("repeated_bursts_candidate", 3, 128, true, false, 24);
   puts("PASS: source order/count and bounded queues throughout deterministic schedules");
   return 0;
}
