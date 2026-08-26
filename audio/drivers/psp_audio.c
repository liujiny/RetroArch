/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2014-2017 - Ali Bouhlel
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdarg.h>
#if defined(VITA) || defined(PSP)
#include <malloc.h>
#endif
#include <stdio.h>
#include <string.h>

#include <rthreads/rthreads.h>
#include <queues/fifo_queue.h>

#if defined(VITA)
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/audioout.h>
#elif defined(PSP)
#include <pspkernel.h>
#include <pspaudio.h>
#elif defined(ORBIS)
#include <libSceAudioOut.h>
#include <libSceUserService.h>
#include <defines/ps4_defines.h>
#include <verbosity.h>

typedef struct SceUserServiceLoginUserIdList
{
   int32_t userId[SCE_USER_SERVICE_MAX_LOGIN_USERS];
} SceUserServiceLoginUserIdList;

int sceUserServiceGetLoginUserIdList(
      SceUserServiceLoginUserIdList *userIdList);
#endif

#include "../audio_driver.h"

#if defined(ORBIS)
int sceKernelDebugOutText(int channel, const char *text);

static void psp_audio_klogf(const char *format, ...)
{
   char message[512];
   va_list args;
   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args);
   va_end(args);
   sceKernelDebugOutText(0, message);
}
#endif

typedef struct psp_audio
{
   uint32_t* buffer;
   uint32_t* zeroBuffer;

   sthread_t *worker_thread;
   slock_t *fifo_lock;
   scond_t *cond;
   slock_t *cond_lock;

   SceUID thread;

   int port;
   int rate;

   volatile uint16_t read_pos;
   volatile uint16_t write_pos;

   volatile bool running;
   bool nonblock;
} psp_audio_t;

#define AUDIO_OUT_COUNT 512u
#if defined(ORBIS)
#undef AUDIO_OUT_COUNT
#define AUDIO_OUT_COUNT 256u
#define PS4_AUDIO_SYSTEM_USER_ID 0xff
#define SCE_AUDIO_OUT_ERROR_ALREADY_INIT ((int)0x8026000e)
#endif
#define AUDIO_BUFFER_SIZE (1u<<13u)
#define AUDIO_BUFFER_SIZE_MASK (AUDIO_BUFFER_SIZE-1)

/* Return port used */
static int psp_configure_audio(unsigned rate, int *user_id)
{
#if defined(VITA)
   return sceAudioOutOpenPort(
         SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_OUT_COUNT,
         rate, SCE_AUDIO_OUT_MODE_STEREO);
#elif defined(ORBIS)
   SceUserServiceLoginUserIdList user_id_list;
   int result = sceUserServiceGetLoginUserIdList(&user_id_list);

   /* AudioOut MAIN is opened for the system user (0xff). A logged-in
    * UserService ID is valid for Pad, but AudioOut rejects it with
    * SCE_DEVICE_SERVICE_ERROR_INVALID_USER (0x809b0001). */
   *user_id = PS4_AUDIO_SYSTEM_USER_ID;
   psp_audio_klogf("[PS4 AUDIO] login-users result=0x%08x audioUserId=0x%08x\n",
         result, *user_id);

   return sceAudioOutOpen(*user_id,
         SCE_AUDIO_OUT_PORT_TYPE_MAIN, 0, AUDIO_OUT_COUNT,
         rate, SCE_AUDIO_OUT_MODE_STEREO);
#else
   return sceAudioSRCChReserve(AUDIO_OUT_COUNT, rate, 2);
#endif
}

static void psp_audio_mainloop(void *data)
{
   psp_audio_t* psp = (psp_audio_t*)data;

   while (psp->running)
   {
      bool cond           = false;
      uint16_t read_pos   = psp->read_pos;
      uint16_t read_pos_2 = psp->read_pos;

      slock_lock(psp->fifo_lock);

      cond                = ((uint16_t)(psp->write_pos - read_pos) & AUDIO_BUFFER_SIZE_MASK)
            < (AUDIO_OUT_COUNT * 2);

      if (!cond)
      {
         read_pos      += AUDIO_OUT_COUNT;
         read_pos      &= AUDIO_BUFFER_SIZE_MASK;
         psp->read_pos  = read_pos;
      }

      slock_unlock(psp->fifo_lock);
      slock_lock(psp->cond_lock);
      scond_signal(psp->cond);
      slock_unlock(psp->cond_lock);

#if defined(VITA) || defined(ORBIS)
      {
         int output_result = sceAudioOutOutput(psp->port,
        cond ? (psp->zeroBuffer)
              : (psp->buffer + read_pos_2));
#if defined(ORBIS)
         static unsigned output_logs;
         if (output_logs++ < 10)
         {
            psp_audio_klogf("[PS4 AUDIO] output[%u] handle=%d frames=%u silence=%d result=0x%08x\n",
                  output_logs, psp->port, AUDIO_OUT_COUNT, cond, output_result);
         }
#endif
      }
#else
      sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX,
              cond
            ? (psp->zeroBuffer)
            : (psp->buffer + read_pos));
#endif
   }

   return;
}

static void *psp_audio_init(const char *device,
      unsigned rate, unsigned latency,
      unsigned block_frames,
      unsigned *new_rate)
{
   int port;
#if defined(ORBIS)
   int user_id;
   int init_result;
#endif
   psp_audio_t *psp = (psp_audio_t*)calloc(1, sizeof(psp_audio_t));

   if (!psp)
      return NULL;

#if defined(ORBIS)
   init_result = sceAudioOutInit();
   psp_audio_klogf("[PS4 AUDIO] psp_audio_init entered; init result=0x%08x rate=%u channels=2 format=stereo_s16 "
         "frames=%u latency=%u block_frames=%u\n", init_result, rate,
         AUDIO_OUT_COUNT, latency, block_frames);
   if (init_result < 0 && init_result != SCE_AUDIO_OUT_ERROR_ALREADY_INIT)
   {
      free(psp);
      return NULL;
   }
#endif

   if ((port = psp_configure_audio(rate,
#if defined(ORBIS)
               &user_id
#else
               NULL
#endif
               )) < 0)
   {
#if defined(ORBIS)
      psp_audio_klogf("[PS4 AUDIO] open userId=0x%08x port=MAIN "
            "index=0 frames=%u rate=%u format=stereo_s16 result=0x%08x\n",
            user_id, AUDIO_OUT_COUNT, rate, port);
#endif
      free(psp);
      return NULL;
   }

#if defined(ORBIS)
   psp_audio_klogf("[PS4 AUDIO] open userId=0x%08x handle=%d error=0x%08x port=MAIN "
         "index=0 frames=%u rate=%u format=stereo_s16\n", user_id, port,
         port < 0 ? port : 0, AUDIO_OUT_COUNT, rate);
#endif
   /* Cache aligned, not necessary but helpful. */
   psp->buffer        = (uint32_t*)malloc(AUDIO_BUFFER_SIZE * sizeof(uint32_t));
   memset(psp->buffer, 0, AUDIO_BUFFER_SIZE * sizeof(uint32_t));

   psp->zeroBuffer    = (uint32_t*)malloc(AUDIO_OUT_COUNT   * sizeof(uint32_t));
   memset(psp->zeroBuffer, 0, AUDIO_OUT_COUNT * sizeof(uint32_t));

   psp->read_pos      = 0;
   psp->write_pos     = 0;
   psp->port          = port;

   psp->fifo_lock     = slock_new();
   psp->cond_lock     = slock_new();
   psp->cond          = scond_new();

   psp->nonblock      = false;
   psp->running       = true;
   psp->worker_thread = sthread_create(psp_audio_mainloop, psp);

   return psp;
}

static void psp_audio_free(void *data)
{
   psp_audio_t* psp = (psp_audio_t*)data;
   if (!psp)
      return;

   if (psp->running)
   {
      if (psp->worker_thread)
      {
         psp->running = false;
         sthread_join(psp->worker_thread);
      }

      if (psp->cond)
         scond_free(psp->cond);
      if (psp->fifo_lock)
         slock_free(psp->fifo_lock);
      if (psp->cond_lock)
         slock_free(psp->cond_lock);
   }
   free(psp->buffer);
   psp->worker_thread = NULL;
   free(psp->zeroBuffer);

#if defined(VITA)
      sceAudioOutReleasePort(psp->port);
#elif defined(ORBIS)
      sceAudioOutClose(psp->port);
#else
      sceAudioSRCChRelease();
#endif

   free(psp);

}

static ssize_t psp_audio_write(void *data, const void *s, size_t len)
{
   psp_audio_t* psp      = (psp_audio_t*)data;
   uint16_t write_pos    = psp->write_pos;
   uint16_t sample_count = len / sizeof(uint32_t);

   if (!psp->running)
      return -1;

   if (psp->nonblock)
   {
      if (AUDIO_BUFFER_SIZE - ((uint16_t)
               (psp->write_pos - psp->read_pos) & AUDIO_BUFFER_SIZE_MASK) < len)
         return 0;
   }

   slock_lock(psp->cond_lock);
   while (AUDIO_BUFFER_SIZE - ((uint16_t)
      (psp->write_pos - psp->read_pos) & AUDIO_BUFFER_SIZE_MASK) < len)
      scond_wait(psp->cond, psp->cond_lock);
   slock_unlock(psp->cond_lock);

   slock_lock(psp->fifo_lock);
   if ((write_pos + sample_count) > AUDIO_BUFFER_SIZE)
   {
      memcpy(psp->buffer + write_pos, s,
            (AUDIO_BUFFER_SIZE - write_pos) * sizeof(uint32_t));
      memcpy(psp->buffer, (uint32_t*)s +
            (AUDIO_BUFFER_SIZE - write_pos),
            (write_pos + sample_count - AUDIO_BUFFER_SIZE) * sizeof(uint32_t));
   }
   else
      memcpy(psp->buffer + write_pos, s, len);

   write_pos      += sample_count;
   write_pos      &= AUDIO_BUFFER_SIZE_MASK;
   psp->write_pos  = write_pos;

   slock_unlock(psp->fifo_lock);
   return len;
}

static bool psp_audio_alive(void *data)
{
   psp_audio_t* psp = (psp_audio_t*)data;
   if (!psp)
      return false;
   return psp->running;
}

static bool psp_audio_stop(void *data)
{
   psp_audio_t* psp = (psp_audio_t*)data;

#if defined(ORBIS)
   return false;
#else
   if (psp)
   {
      psp->running = false;

      if (psp->worker_thread)
      {
         sthread_join(psp->worker_thread);
         psp->worker_thread = NULL;
      }
   }
   return true;
#endif
}

static bool psp_audio_start(void *data, bool is_shutdown)
{
   psp_audio_t* psp = (psp_audio_t*)data;

   if (psp && !psp->running)
   {
      if (!psp->worker_thread)
      {
         psp->running       = true;
         psp->worker_thread = sthread_create(psp_audio_mainloop, psp);
      }
   }

   return true;
}

static void psp_audio_set_nonblock_state(void *data, bool toggle)
{
   psp_audio_t* psp = (psp_audio_t*)data;
   if (psp)
      psp->nonblock = toggle;
}

static size_t psp_write_avail(void *data)
{
   size_t _len;
   psp_audio_t* psp = (psp_audio_t*)data;

   if (!psp||!psp->running)
      return 0;
   slock_lock(psp->fifo_lock);
   _len = AUDIO_BUFFER_SIZE - ((uint16_t)
         (psp->write_pos - psp->read_pos) & AUDIO_BUFFER_SIZE_MASK);
   slock_unlock(psp->fifo_lock);
   return _len;
}

/* TODO/FIXME - implement? */
static bool psp_audio_use_float(void *data) { return false; }
static size_t psp_buffer_size(void *data)
{
   return AUDIO_BUFFER_SIZE /** sizeof(uint32_t)*/;
}

audio_driver_t audio_psp = {
   psp_audio_init,
   psp_audio_write,
   psp_audio_stop,
   psp_audio_start,
   psp_audio_alive,
   psp_audio_set_nonblock_state,
   psp_audio_free,
   psp_audio_use_float,
#if defined(VITA)
   "vita",
#elif defined(ORBIS)
   "orbis",
#else
   "psp",
#endif
   NULL,
   NULL,
   psp_write_avail,
   psp_buffer_size,
   NULL /* write_raw */
};
