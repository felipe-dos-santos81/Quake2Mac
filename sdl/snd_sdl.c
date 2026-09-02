// snd_sdl.c -- SNDDMA_* on an SDL3 audio stream. Replaces
// linux/snd_linux.c (OSS mmap). The engine paints into dma.buffer
// (a plain malloc ring); Submit pushes freshly painted regions to SDL.
//
// UNITS (critical): paintedtime/soundtime are FRAMES ("sample pairs",
// see snd_dma.c). dma.samples and ring offsets are MONO-EQUIVALENT
// samples (frames * channels). last_pushed tracks paintedtime in
// frames; every ring access converts by dma.channels.

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "client/client.h"
#include "client/snd_loc.h"

static SDL_AudioStream *audio_stream;
static SDL_AudioDeviceID audio_device;
static int snd_inited;
static int last_pushed;      /* absolute frame cursor handed to SDL (paintedtime units) */
static int out_frame_bytes;  /* device output format, fixed for its lifetime */
static int out_freq;

cvar_t *sndbits;
cvar_t *sndspeed;
cvar_t *sndchannels;

/* Unwinds whatever subset of the audio stack SNDDMA_Init already set up;
 * all steps tolerate the not-yet-initialized state. */
static void SNDDMA_Teardown(void)
{
	if (audio_stream)
	{
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
	}
	if (audio_device)
	{
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
	}
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

/* The stream is bound to the device, so queued bytes are reported in the
 * device's (possibly resampled) output format; cache it once to convert
 * them back to input frames in SNDDMA_GetDMAPos. */
static void SNDDMA_CacheDeviceFormat(void)
{
	SDL_AudioSpec dspec;

	SDL_GetAudioDeviceFormat(audio_device, &dspec, NULL);
	out_frame_bytes = SDL_AUDIO_FRAMESIZE(dspec);
	out_freq = dspec.freq;
	if (out_frame_bytes <= 0 || out_freq <= 0)
	{
		out_frame_bytes = dma.channels * (dma.samplebits / 8);
		out_freq = dma.speed;
	}
}

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec spec;

	if (snd_inited)
		return true;

	sndbits     = Cvar_Get("sndbits", "16", CVAR_ARCHIVE);
	sndspeed    = Cvar_Get("sndspeed", "22050", CVAR_ARCHIVE);
	sndchannels = Cvar_Get("sndchannels", "2", CVAR_ARCHIVE);

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		Com_Printf("SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
		return false;
	}

	dma.samplebits = ((int)sndbits->value == 8) ? 8 : 16;
	dma.speed = (int)sndspeed->value;
	if (dma.speed <= 0)
		dma.speed = 22050;
	dma.channels = (int)sndchannels->value;
	if (dma.channels != 1 && dma.channels != 2)
		dma.channels = 2;

	spec.format   = (dma.samplebits == 16) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
	spec.channels = dma.channels;
	spec.freq     = dma.speed;

	audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
	if (!audio_device)
	{
		Com_Printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		SNDDMA_Teardown();
		return false;
	}

	audio_stream = SDL_CreateAudioStream(&spec, &spec);
	if (!audio_stream || !SDL_BindAudioStream(audio_device, audio_stream))
	{
		Com_Printf("SDL audio stream setup failed: %s\n", SDL_GetError());
		SNDDMA_Teardown();
		return false;
	}

	SNDDMA_CacheDeviceFormat();

	/* MUST stay a power of two: the mixer masks indices with dma.samples-1 */
	dma.samples = 32768;           /* mono-equivalent samples in the ring */
	dma.submission_chunk = 1;
	dma.buffer = (byte *)malloc(dma.samples * (dma.samplebits / 8));
	if (!dma.buffer)
	{
		Com_Printf("SNDDMA_Init: could not allocate dma buffer\n");
		SNDDMA_Teardown();
		return false;
	}
	memset(dma.buffer, 0, dma.samples * (dma.samplebits / 8));

	dma.samplepos = 0;
	last_pushed = 0;

	SDL_ResumeAudioStreamDevice(audio_stream);

	snd_inited = 1;
	Com_Printf("SDL3 audio: %d Hz, %d channels, %d bit\n",
		dma.speed, dma.channels, dma.samplebits);
	return true;
}

/*
** SNDDMA_GetDMAPos
**
** Returns the consumed position in MONO-EQUIVALENT samples modulo
** dma.samples (the engine divides by dma.channels to get frames).
** Estimated as pushed-minus-still-queued.
*/
int SNDDMA_GetDMAPos(void)
{
	int avail;
	long long queued_in_frames, played_in_frames;

	if (!snd_inited)
		return 0;

	avail = SDL_GetAudioStreamAvailable(audio_stream);
	if (avail < 0)
		avail = 0;

	// the stream is bound to the device, so queued bytes are reported in
	// the device's (possibly resampled) output format; convert them back
	// to input frames before comparing against last_pushed (format cached
	// at init, it is fixed for the device's lifetime)
	queued_in_frames = ((long long)avail / out_frame_bytes)
		* dma.speed / out_freq;

	played_in_frames = (long long)last_pushed - queued_in_frames;
	if (played_in_frames < 0)
		played_in_frames = 0;

	dma.samplepos = (int)((played_in_frames * dma.channels) % dma.samples);
	return dma.samplepos;
}

void SNDDMA_BeginPainting(void)
{
}

/*
==============
SNDDMA_Submit

Push the region painted since the last submit into the SDL stream.
paintedtime is in frames; the ring is indexed in mono-equivalent samples.
==============
*/
void SNDDMA_Submit(void)
{
	int bytes_per_sample, bytes_per_frame, frames_in_ring;
	int pos, chunk;

	if (!snd_inited)
		return;

	bytes_per_sample = dma.samplebits / 8;
	bytes_per_frame  = bytes_per_sample * dma.channels;
	frames_in_ring   = dma.samples / dma.channels;

	/* the engine chops paintedtime after very long sessions and snd_restart
	** can regress it; resync if it ever moves backwards */
	if (paintedtime < last_pushed)
		last_pushed = paintedtime;

	/* backpressure: never hold more than half the ring queued in SDL */
	if (SDL_GetAudioStreamAvailable(audio_stream) / bytes_per_frame
		>= frames_in_ring / 2)
		return;

	/* the ring only holds frames_in_ring frames; anything older was
	** overwritten and would be pushed as garbage */
	if (paintedtime - last_pushed > frames_in_ring)
		last_pushed = paintedtime - frames_in_ring;

	while (last_pushed < paintedtime)
	{
		pos = last_pushed % frames_in_ring;  /* frame position in ring */
		chunk = paintedtime - last_pushed;   /* frames */
		if (chunk > frames_in_ring - pos)
			chunk = frames_in_ring - pos;
		if (!SDL_PutAudioStreamData(audio_stream,
			dma.buffer + (pos * dma.channels) * bytes_per_sample,
			chunk * bytes_per_frame))
		{
			Com_Printf("SNDDMA_Submit: SDL_PutAudioStreamData failed: %s\n",
				SDL_GetError());
			return;
		}
		last_pushed += chunk;
	}
}

void SNDDMA_Shutdown(void)
{
	if (!snd_inited)
		return;

	SNDDMA_Teardown();

	if (dma.buffer)
	{
		free(dma.buffer);
		dma.buffer = NULL;
	}
	snd_inited = 0;
}
