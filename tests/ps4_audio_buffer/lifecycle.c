/* PS4 AudioOut retains the last submitted window until drained. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "audio/audio_driver.h"
#include "samples/audio/psp_ring/device_mock.h"
int ps4_lifetime_pending(void);
int main(void)
{
   unsigned i, j, rate;
   static const unsigned latency[] = {64, 128, 96, 160};
   uint32_t samples[1536];
   void *context;
   const audio_driver_t *driver = &audio_ps4;
   setvbuf(stdout, NULL, _IONBF, 0);
   mock_device_reset();
   for (j = 0; j < 1536; j++) samples[j] = j + 1;
   for (i = 0; i < 40; i++)
   {
      rate = 0;
      context = driver->init(NULL, 48000, latency[i % 4], &rate);
      assert(context);
      assert(driver->write(context, samples, sizeof(samples)) == sizeof(samples));
      usleep(3000);
      assert(driver->stop(context));
      assert(!ps4_lifetime_pending());
      /* Stop must retire the in-flight window before restart. */
      assert(driver->start(context, false));
      usleep(3000);
      driver->free(context);
      assert(!ps4_lifetime_pending());
      assert(MOCK_READ(mock_breaks) == 0);
      mock_device_reset();
      printf("reinit %u, latency %u: closed\n", i + 1, latency[i % 4]);
      /* Reset only after proving that no DMA pointer remains. */
   }
   puts("PASS: 40 latency reinitializations and stop/start lifetimes");
   return 0;
}
