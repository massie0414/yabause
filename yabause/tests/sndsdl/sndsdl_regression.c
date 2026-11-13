/* Exercise the real SDL backend with a deterministic playback clock. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "scsp.h"
#include "sndsdl.h"

static SDL_AudioSpec device;
static int failures, locked, open_device, subsystem_refs;
static int fail_init, fail_open;
static unsigned int lock_calls;
static s32 left[16384], right[16384];
static s16 output[32768];

static void check(int condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int SDLCALL SDL_InitSubSystem(Uint32 flags)
{
    if (fail_init) return -1;
    subsystem_refs++;
    return 0;
}
void SDLCALL SDL_QuitSubSystem(Uint32 flags) { subsystem_refs--; }
void SDLCALL SDL_SetMainReady(void) { }
const char *SDLCALL SDL_GetError(void) { return "test audio error"; }
int SDLCALL SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    if (fail_open) return -1;
    desired->silence = 0;
    desired->size = desired->samples * 4;
    device = *desired;
    open_device = 1;
    return 0;
}
void SDLCALL SDL_CloseAudio(void) { open_device = 0; }
void SDLCALL SDL_PauseAudio(int pause_on) { }
void SDLCALL SDL_LockAudio(void)
{
    check(!locked, "audio lock is not recursive");
    locked = 1;
    lock_calls++;
}
void SDLCALL SDL_UnlockAudio(void)
{
    check(locked, "audio unlock is balanced");
    locked = 0;
}
void YabSetError(int type, const void *extra) { }

static u32 capacity(void)
{
    return SNDSDL.GetAudioSpace();
}

static void enqueue(u32 frames, int start)
{
    u32 i;
    for (i = 0; i < frames; i++)
    {
        left[i] = start + i;
        right[i] = -(start + (int)i);
    }
    SNDSDL.UpdateAudio((u32 *)left, (u32 *)right, frames);
}

static void consume(u32 frames)
{
    check(open_device && !locked, "callback runs only on an open, unlocked device");
    memset(output, 0x55, frames * 4);
    device.callback(device.userdata, (Uint8 *)output, frames * 4);
}

static int is_silence(u32 first, u32 end)
{
    u32 i;
    for (i = first * 2; i < end * 2; i++)
        if (output[i] != 0) return 0;
    return 1;
}

static void expect_sequence(u32 first, u32 count, int start, const char *message)
{
    u32 i;
    for (i = 0; i < count; i++)
    {
        if (output[(first + i) * 2] != start + (int)i ||
            output[(first + i) * 2 + 1] != -(start + (int)i))
        {
            check(0, message);
            return;
        }
    }
}

static void test_fifo(void)
{
    u32 total;
    check(SNDSDL.Init() == 0, "initialize output");
    total = capacity();
    check(total > 0, "an empty audio queue reports writable space");
    check(total >= (u32)device.samples * 2, "queue holds at least two callback blocks");
    /* Let old implementations advance their read cursor so later checks can
       also demonstrate the stale-audio failure, rather than stopping here. */
    if (!total) { consume(device.samples); total = capacity(); }
    check(total < 16000 && total > 32, "test queue size is supported");
    enqueue(total, 100);
    check(capacity() == 0, "a full queue reports no writable space");
    consume(total - 7);
    expect_sequence(0, total - 7, 100, "playback starts with the first submitted sample");
    enqueue(total - 7, 10000);
    consume(total);
    expect_sequence(0, 7, 100 + total - 7, "wrapped queue retains unread samples");
    expect_sequence(7, total - 7, 10000, "wrapped queue preserves new sample order");
    check(capacity() == total, "empty and full queues remain distinguishable");
    consume(total);
    check(is_silence(0, total), "underrun outputs silence instead of replaying stale audio");
    consume(total);
    check(is_silence(0, total), "continued underrun stays silent");

    enqueue(total, 200);
    consume(total - 1);
    consume(32);
    expect_sequence(0, 1, 200 + total - 1, "partial underrun plays the last valid sample");
    check(is_silence(1, 32), "partial underrun clears the remainder of the callback");
    enqueue(8, 300);
    consume(32);
    check(is_silence(0, 32), "recovery waits for a buffer reserve");
    check(capacity() == total - 8, "recovery does not discard newly submitted audio");
    enqueue(total - 8, 400);
    consume(total);
    expect_sequence(0, 8, 300, "recovery retains the first new samples");
    expect_sequence(8, total - 8, 400, "recovery resumes queued samples in order");
    SNDSDL.DeInit();
}

static void test_reset_and_format(void)
{
    u32 total;
    unsigned int before;
    check(SNDSDL.Init() == 0, "reinitialize output");
    total = capacity();
    check(total > 0, "reinitialization resets both cursors");
    if (!total) { consume(device.samples); total = capacity(); }
    enqueue(total, 500);
    consume(31);
    before = lock_calls;
    check(SNDSDL.ChangeVideoFormat(50) == 0, "switch to PAL");
    check(lock_calls > before, "format changes exclude the audio callback");
    total = capacity();
    check(total > 0, "format change resets queue accounting");
    consume(device.samples);
    check(is_silence(0, device.samples), "format change discards old audio");
    before = lock_calls;
    SNDSDL.Reset();
    check(lock_calls > before, "reset excludes the audio callback");
    check(SNDSDL.ChangeVideoFormat(60) == 0, "switch back to NTSC");
    total = capacity();
    if (total > 32)
    {
        enqueue(total, 600);
        SNDSDL.Reset();
        check(capacity() == total, "reset flushes queued audio");
        consume(device.samples);
        check(is_silence(0, device.samples), "reset does not replay old samples");
    }
    SNDSDL.DeInit();
    check(subsystem_refs == 0, "successful audio initialization is balanced on shutdown");
}

static void test_volume_and_mute(void)
{
    u32 total, i;
    check(SNDSDL.Init() == 0, "initialize volume test");
    total = capacity();
    if (!total) { consume(device.samples); total = capacity(); }
    SNDSDL.SetVolume(50);
    for (i = 0; i < total; i++) { left[i] = 10000; right[i] = -10000; }
    left[1] = INT_MAX;
    right[1] = INT_MIN;
    SNDSDL.UpdateAudio((u32 *)left, (u32 *)right, total);
    check(left[0] == 10000 && right[0] == -10000, "volume conversion leaves source samples unchanged");
    consume(total);
    check(output[0] == 5000 && output[1] == -5000, "volume scales both channels");
    check(output[2] == 32767 && output[3] == -32768, "large samples clip without multiplication overflow");

    SNDSDL.SetVolume(100);
    enqueue(total, 100);
    SNDSDL.MuteAudio();
    consume(total);
    check(is_silence(0, total), "muting silences queued audio");
    SNDSDL.UnMuteAudio();
    enqueue(total, 200);
    consume(total);
    expect_sequence(0, total, 200, "unmute resumes only newly queued audio");
    SNDSDL.DeInit();
}

static void test_failures(void)
{
    fail_init = 1;
    check(SNDSDL.Init() == -1, "audio subsystem failure is reported");
    if (open_device) SNDSDL.DeInit();
    fail_init = 0;
    fail_open = 1;
    check(SNDSDL.Init() == -1, "device open failure is reported");
    fail_open = 0;
    check(subsystem_refs == 0, "failed initialization releases subsystem reference");
}

int main(void)
{
    test_fifo();
    test_reset_and_format();
    test_volume_and_mute();
    test_failures();
    printf("%d audio regression failure(s)\n", failures);
    return failures ? 1 : 0;
}
