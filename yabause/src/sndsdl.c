/*  Copyright 2005-2006 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file sndsdl.c
    \brief SDL sound interface.
*/

#ifdef HAVE_LIBSDL

#include <stdlib.h>

#if defined(__APPLE__) || defined(GEKKO)
 #ifdef HAVE_LIBSDL2
  #include <SDL2/SDL.h>
 #else
  #include <SDL/SDL.h>
 #endif
#else
 #include "SDL.h"
#endif
#include "error.h"
#include "scsp.h"
#include "sndsdl.h"
#include "debug.h"

static int SNDSDLInit(void);
static void SNDSDLDeInit(void);
static int SNDSDLReset(void);
static int SNDSDLChangeVideoFormat(int vertfreq);
static void sdlConvert32uto16s(const s32 *srcL, const s32 *srcR, s16 *dst, u32 len);
static void SNDSDLUpdateAudio(u32 *leftchanbuffer, u32 *rightchanbuffer, u32 num_samples);
static u32 SNDSDLGetAudioSpace(void);
static void SNDSDLMuteAudio(void);
static void SNDSDLUnMuteAudio(void);
static void SNDSDLSetVolume(int volume);
#ifdef USE_SCSPMIDI
int SNDSDLMidiChangePorts(int inport, int outport);
u8 SNDSDLMidiIn(int *isdata);
int SNDSDLMidiOut(u8 data);
#endif

SoundInterface_struct SNDSDL = {
SNDCORE_SDL,
"SDL Sound Interface",
SNDSDLInit,
SNDSDLDeInit,
SNDSDLReset,
SNDSDLChangeVideoFormat,
SNDSDLUpdateAudio,
SNDSDLGetAudioSpace,
SNDSDLMuteAudio,
SNDSDLUnMuteAudio,
SNDSDLSetVolume,
#ifdef USE_SCSPMIDI
SNDSDLMidiChangePorts,
SNDSDLMidiIn,
SNDSDLMidiOut
#endif
};

#define NUMSOUNDBLOCKS  4
#define SOUND_FRAME_BYTES (sizeof(s16) * 2)

static u16 *stereodata16;
static u32 soundoffset;
static u32 soundpos;
static u32 soundbufsize;
static u32 soundqueued;
static u32 soundprebuffer;
static int soundready;
static SDL_AudioSpec audiofmt;
static u8 soundvolume;
static int muted = 0;
static int audio_initialized;
static int audio_open;

//////////////////////////////////////////////////////////////////////////////

static void MixAudio(UNUSED void *userdata, Uint8 *stream, int len) {
   u32 bytes, first;
   Uint8 *soundbuf = (Uint8 *)stereodata16;

   if (len <= 0)
      return;
   memset(stream, audiofmt.silence, len);
   if (!soundbuf || !soundbufsize)
      return;

   // SDL holds the audio lock while calling us. Build a reserve at startup
   // and after starvation instead of repeatedly playing tiny fragments.
   if (!soundready)
   {
      if (soundqueued < soundprebuffer)
         return;
      soundready = 1;
   }

   bytes = (u32)len - (u32)len % SOUND_FRAME_BYTES;
   if (bytes > soundqueued)
      bytes = soundqueued;
   first = soundbufsize - soundpos;
   if (first > bytes)
      first = bytes;
   if (!muted)
   {
      memcpy(stream, soundbuf + soundpos, first);
      memcpy(stream + first, soundbuf, bytes - first);
   }
   soundpos = (soundpos + bytes) % soundbufsize;
   soundqueued -= bytes;
   if (bytes < (u32)len)
      soundready = 0;
}

//////////////////////////////////////////////////////////////////////////////

static int SNDSDLInit(void)
{
   //samples should be a power of 2 according to SDL-doc
   //so normalize it to the nearest power of 2 here
   u32 normSamples = 512;

#if defined (_MSC_VER) && SDL_VERSION_ATLEAST(2,0,0)
   SDL_SetMainReady();
#endif
   if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
   {
      YabSetError(YAB_ERR_SDL, (void *)SDL_GetError());
      return -1;
   }
   audio_initialized = 1;

   memset(&audiofmt, 0, sizeof(audiofmt));
   audiofmt.freq = 44100;
   audiofmt.format = AUDIO_S16SYS;
   audiofmt.channels = 2;
   audiofmt.samples = (audiofmt.freq / 60) * 2;
   audiofmt.callback = MixAudio;
   audiofmt.userdata = NULL;

   while (normSamples < audiofmt.samples) 
      normSamples <<= 1;

   audiofmt.samples = normSamples;
   
   soundvolume = SDL_MIX_MAXVOLUME;
   muted = 0;

   if (SDL_OpenAudio(&audiofmt, NULL) != 0)
   {
      YabSetError(YAB_ERR_SDL, (void *)SDL_GetError());
      SNDSDLDeInit();
      return -1;
   }
   audio_open = 1;

   if (SNDSDLChangeVideoFormat(60) != 0)
   {
      SNDSDLDeInit();
      return -1;
   }

   SDL_PauseAudio(0);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void SNDSDLDeInit(void)
{
   if (audio_open)
      SDL_CloseAudio();
   audio_open = 0;
   free(stereodata16);
   stereodata16 = NULL;
   soundoffset = soundpos = soundqueued = soundbufsize = soundprebuffer = 0;
   soundready = 0;
   if (audio_initialized)
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
   audio_initialized = 0;
}

//////////////////////////////////////////////////////////////////////////////

static int SNDSDLReset(void)
{
   if (audio_open)
   {
      SDL_LockAudio();
      soundoffset = soundpos = soundqueued = 0;
      soundready = 0;
      SDL_UnlockAudio();
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static int SNDSDLChangeVideoFormat(int vertfreq)
{
   u32 newsize, prebuffer;
   u16 *newbuffer;
   if (!audio_open || vertfreq <= 0)
      return -1;

   newsize = (audiofmt.freq / vertfreq) * NUMSOUNDBLOCKS * SOUND_FRAME_BYTES;
   prebuffer = audiofmt.samples * 2 * SOUND_FRAME_BYTES;
   // The old four-frame buffer could be smaller than two SDL callbacks.
   if (newsize < prebuffer)
      newsize = prebuffer;
   if (!(newbuffer = (u16 *)malloc(newsize)))
      return -1;
   memset(newbuffer, audiofmt.silence, newsize);

   SDL_LockAudio();
   free(stereodata16);
   stereodata16 = newbuffer;
   soundbufsize = newsize;
   soundprebuffer = prebuffer;
   soundoffset = soundpos = soundqueued = 0;
   soundready = 0;
   SDL_UnlockAudio();
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void sdlConvert32uto16s(const s32 *srcL, const s32 *srcR, s16 *dst, u32 len) {
   u32 i;

   for (i = 0; i < len; i++)
   {
      s64 sample;
      // Left Channel
      sample = ((s64)*srcL * soundvolume) / SDL_MIX_MAXVOLUME;
      if (sample > 0x7FFF) *dst = 0x7FFF;
      else if (sample < -0x8000) *dst = -0x8000;
      else *dst = (s16)sample;
      srcL++;
      dst++;
      // Right Channel
      sample = ((s64)*srcR * soundvolume) / SDL_MIX_MAXVOLUME;
      if (sample > 0x7FFF) *dst = 0x7FFF;
      else if (sample < -0x8000) *dst = -0x8000;
      else *dst = (s16)sample;
      srcR++;
      dst++;
   } 
}

static void SNDSDLUpdateAudio(u32 *leftchanbuffer, u32 *rightchanbuffer, u32 num_samples)
{
   u32 copy1size=0, copy2size=0;
   if (!audio_open)
      return;
   SDL_LockAudio();

   if (num_samples > (soundbufsize - soundqueued) / SOUND_FRAME_BYTES)
      num_samples = (soundbufsize - soundqueued) / SOUND_FRAME_BYTES;

   if ((soundbufsize - soundoffset) < (num_samples * sizeof(s16) * 2))
   {
      copy1size = (soundbufsize - soundoffset);
      copy2size = (num_samples * sizeof(s16) * 2) - copy1size;
   }
   else
   {
      copy1size = (num_samples * sizeof(s16) * 2);
      copy2size = 0;
   }

   sdlConvert32uto16s((s32 *)leftchanbuffer, (s32 *)rightchanbuffer, (s16 *)(((u8 *)stereodata16)+soundoffset), copy1size / sizeof(s16) / 2);

   if (copy2size)
      sdlConvert32uto16s((s32 *)leftchanbuffer + (copy1size / sizeof(s16) / 2), (s32 *)rightchanbuffer + (copy1size / sizeof(s16) / 2), (s16 *)stereodata16, copy2size / sizeof(s16) / 2);

   soundoffset += copy1size + copy2size;   
   soundoffset %= soundbufsize;
   soundqueued += copy1size + copy2size;

   SDL_UnlockAudio();
}

//////////////////////////////////////////////////////////////////////////////

static u32 SNDSDLGetAudioSpace(void)
{
   u32 freespace;
   if (!audio_open)
      return 0;
   SDL_LockAudio();
   freespace = (soundbufsize - soundqueued) / SOUND_FRAME_BYTES;
   SDL_UnlockAudio();
   return freespace;
}

//////////////////////////////////////////////////////////////////////////////

static void SNDSDLMuteAudio(void)
{
   if (audio_open) SDL_LockAudio();
   muted = 1;
   if (audio_open) SDL_UnlockAudio();
}

//////////////////////////////////////////////////////////////////////////////

static void SNDSDLUnMuteAudio(void)
{
   if (audio_open) SDL_LockAudio();
   muted = 0;
   if (audio_open) SDL_UnlockAudio();
}

//////////////////////////////////////////////////////////////////////////////

static void SNDSDLSetVolume(int volume)
{
   if (volume < 0) volume = 0;
   if (volume > 100) volume = 100;
   if (audio_open) SDL_LockAudio();
   soundvolume = (SDL_MIX_MAXVOLUME * volume) / 100;
   if (audio_open) SDL_UnlockAudio();
}

//////////////////////////////////////////////////////////////////////////////

#ifdef USE_SCSPMIDI
int SNDSDLMidiChangePorts(int inport, int outport)
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

u8 SNDSDLMidiIn(int *isdata)
{
	*isdata = 0;
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

int SNDSDLMidiOut(u8 data)
{
	return 1;
}

//////////////////////////////////////////////////////////////////////////////
#endif
#endif
