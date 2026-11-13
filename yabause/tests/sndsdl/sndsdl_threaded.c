/* Stress buffer replacement while SDL's dummy audio thread is running. */
#include <stdio.h>
#include <SDL.h>
#include "scsp.h"
#include "sndsdl.h"

static s32 left[16384], right[16384];
static int errors;

void YabSetError(int type, const void *extra)
{
    fprintf(stderr, "Audio error %d\n", type);
    errors++;
}

int main(void)
{
    int cycle, i;
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    for (i = 0; i < 16384; i++)
    {
        left[i] = (i & 255) * 32;
        right[i] = -left[i];
    }
    for (cycle = 0; cycle < 3; cycle++)
    {
        u32 total, free_frames;
        if (SNDSDL.Init() != 0) return 1;
        total = SNDSDL.GetAudioSpace();
        if (!total || total > 16384) return 2;
        SNDSDL.UpdateAudio((u32 *)left, (u32 *)right, total);
        /* More than two callbacks must consume the initial queue. */
        SDL_Delay(250);
        if (SNDSDL.GetAudioSpace() != total) return 3;
        for (i = 0; i < 100; i++)
        {
            if (i % 5 == 0 && SNDSDL.ChangeVideoFormat(i & 1 ? 50 : 60) != 0) return 4;
            if (i % 7 == 0) SNDSDL.Reset();
            SNDSDL.SetVolume(i % 101);
            if (i & 1) SNDSDL.MuteAudio();
            else SNDSDL.UnMuteAudio();
            free_frames = SNDSDL.GetAudioSpace();
            if (free_frames > 16384) return 5;
            SNDSDL.UpdateAudio((u32 *)left, (u32 *)right, free_frames);
            SDL_Delay(3);
        }
        SNDSDL.DeInit();
        SNDSDL.DeInit(); /* Shutdown is safe after an already closed device. */
        if (SNDSDL.GetAudioSpace() != 0) return 6;
    }
    puts("SDL audio thread, format changes, resets and reinitialization passed");
    return errors ? 1 : 0;
}
