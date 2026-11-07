/* PSP audio output: one hardware channel fed by the portable mixer. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <string.h>
#include "psp_common.h"
#include "../platform.h"
#include "../../audio/mixer.h"

#define SAMPLES 1024
static int16_t __attribute__((aligned(64))) buffers[2][SAMPLES * 2];
static SceUID sema = -1;
static volatile int running;
static int channel = -1;
static SceUID thid = -1;
volatile int g_audioMute;

static void lock(void) { if (sema >= 0) sceKernelWaitSema(sema, 1, NULL); }
static void unlock(void) { if (sema >= 0) sceKernelSignalSema(sema, 1); }

static int audioThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cur = 0;
    while (running) {
        if (g_audioMute) memset(buffers[cur], 0, sizeof buffers[cur]);
        else {
            extern volatile int64_t g_perfAudioUs;
            SceUInt32 t0 = sceKernelGetSystemTimeLow();
            mix_render(buffers[cur], SAMPLES);
            g_perfAudioUs += (SceUInt32)(sceKernelGetSystemTimeLow() - t0);
        }
        sceAudioOutputPannedBlocking(channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, buffers[cur]);
        cur ^= 1;
    }
    return 0;
}

void audio_init(void) {
    sema = sceKernelCreateSema("mixsema", 0, 1, 1, NULL);
    mix_init(lock, unlock);
    channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SAMPLES, PSP_AUDIO_FORMAT_STEREO);
    if (channel < 0) return;
    running = 1;
    thid = sceKernelCreateThread("audio", audioThread, 0x12, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

void audio_shutdown(void) {
    if (!running) return;
    mix_stop_all();
    running = 0;
    if (thid >= 0) { sceKernelWaitThreadEnd(thid, NULL); sceKernelDeleteThread(thid); }
    if (channel >= 0) sceAudioChRelease(channel);
}

int plat_audio_available(void) { return channel >= 0; }
void plat_audio_poll(void) {}
