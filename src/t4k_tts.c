/*
   t4k_tts.c:

   Text-To-Speach-related functions.

   Copyright 2013.
Author: Nalin.x.Linux <Nalin.x.Linux@gmail.com>
Project email: <tuxmath-devel@lists.sourceforge.net>
Project website: http://tux4kids.alioth.debian.org

t4k_tts.c is part of the t4k_common library.

t4k_common is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

t4k_common is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include "t4k_globals.h"
#include "t4k_common.h"


#include <stdlib.h>
#include <SDL3/SDL_thread.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#endif

SDL_Thread *tts_thread;
int text_to_speech_status;

#if WITH_ESPEAK == 1

#if WITH_ESPEAK_NG
# include <espeak-ng/speak_lib.h>
#else
# include <speak_lib.h>
#endif

void T4K_Tts_cancel(void);
void T4K_Tts_wait(void);
void T4K_Tts_stop(void);

typedef struct tts_node {
	int mode;
	int rate;
	int pitch;
	char text[10000];
	struct tts_node *next;
} tts_node;

static tts_node *tts_queue_head = NULL;
static tts_node *tts_queue_tail = NULL;
static SDL_Thread *tts_worker_thread = NULL;
static SDL_Mutex *tts_mutex = NULL;
static SDL_Condition *tts_cond = NULL;
static SDL_AudioStream *tts_audio_stream = NULL;
static int tts_worker_active = 0;
static volatile int tts_cancel_requested = 0;
static volatile int tts_is_synthesizing = 0;

/* Callback called by eSpeak-NG when a buffer of speech PCM samples is generated */
static int SDLCALL tts_synth_callback(short *wav, int numsamples, espeak_EVENT *events)
{
	(void)events;
	if (tts_cancel_requested || !tts_worker_active)
		return 1; /* 1 = abort synthesis */

	if (wav && numsamples > 0 && tts_audio_stream)
	{
		SDL_PutAudioStreamData(tts_audio_stream, wav, numsamples * sizeof(short));
	}
	return 0; /* 0 = continue synthesis */
}

/* Dedicated TTS worker thread function — runs asynchronously so the main UI loop is never blocked */
static int SDLCALL tts_worker_func(void *arg)
{
	(void)arg;
#ifdef _WIN32
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

	espeak_POSITION_TYPE position_type = POS_CHARACTER;

	while (1)
	{
		tts_node *node = NULL;

		SDL_LockMutex(tts_mutex);
		while (tts_worker_active && tts_queue_head == NULL)
		{
			SDL_WaitCondition(tts_cond, tts_mutex);
		}

		if (!tts_worker_active && tts_queue_head == NULL)
		{
			SDL_UnlockMutex(tts_mutex);
			break;
		}

		node = tts_queue_head;
		if (node)
		{
			tts_queue_head = node->next;
			if (!tts_queue_head)
				tts_queue_tail = NULL;
		}

		if (!tts_worker_active || tts_cancel_requested)
		{
			SDL_UnlockMutex(tts_mutex);
			if (node) free(node);
			continue;
		}

		tts_is_synthesizing = 1;
		SDL_UnlockMutex(tts_mutex);

		if (!node)
			continue;

		if (node->rate != DEFAULT_VALUE)
			espeak_SetParameter(espeakRATE, (3.7 * node->rate) + 80, 0);
		if (node->pitch != DEFAULT_VALUE)
			espeak_SetParameter(espeakPITCH, node->pitch, 0);

		int size = strlen(node->text) + 1;
		/* espeakPHONEMES (0x100): text inside [[ ]] is treated as Kirshenbaum phoneme codes */
		espeak_Synth(node->text, size, 0, position_type, 0,
		             espeakCHARS_AUTO | espeakPHONEMES, 0, NULL);
		espeak_Synchronize();

		free(node);

		SDL_LockMutex(tts_mutex);
		tts_is_synthesizing = 0;
		SDL_UnlockMutex(tts_mutex);
	}

#ifdef _WIN32
	CoUninitialize();
#endif
	return 0;
}

//terminate the current speech
void T4K_Tts_cancel(void)
{
	T4K_Tts_stop();
}

//wait till current text is spoken
void T4K_Tts_wait(void)
{
	while (1)
	{
		if (tts_cancel_requested || !tts_worker_active)
			break;

		int busy = 0;
		if (tts_mutex)
		{
			SDL_LockMutex(tts_mutex);
			busy = (tts_queue_head != NULL || tts_is_synthesizing);
			SDL_UnlockMutex(tts_mutex);
		}
		if (tts_audio_stream && SDL_GetAudioStreamAvailable(tts_audio_stream) > 0)
			busy = 1;

		if (!busy)
			break;
		SDL_Delay(20);
	}
	SDL_Delay(40);
}

#ifdef _WIN32
#include <io.h>
#ifndef F_OK
#define F_OK 0
#endif
#ifndef access
#define access _access
#endif
#else
#include <unistd.h>
#endif


//This function should be called at beginning
int T4K_Tts_init(void)
{
    /* If already initialized, do not re-initialize! */
    if (tts_worker_thread != NULL && tts_worker_active)
    {
        return 1;
    }

    /* Ensure SDL audio subsystem is initialized before querying devices */
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "T4K_Tts_init(): Failed to initialize SDL audio: %s\n", SDL_GetError());
            text_to_speech_status = 0;
            return 0;
        }
    }

    /* Check if any audio playback device is present before attempting playback.
     * Prevents crash in libpcaudio when running in headless environments / VMs without audio. */
    int num_audio_devices = 0;
    SDL_AudioDeviceID *devs = SDL_GetAudioPlaybackDevices(&num_audio_devices);
    if (devs)
    {
        SDL_free(devs);
    }
    if (num_audio_devices <= 0)
    {
        fprintf(stderr, "T4K_Tts_init(): No audio playback device available; disabling TTS.\n");
        text_to_speech_status = 0;
        return 0;
    }

    const char* path = NULL;
    static char bundle_dir[1024];
    static char test_data[1024];
    const char* base = SDL_GetBasePath();
    if (base) {
        /* Check base/espeak-ng-data (Windows / portable packaging) */
        snprintf(test_data, sizeof(test_data), "%sespeak-ng-data", base);
#if defined(WIN32) || defined(_WIN32) || defined(BUILD_MINGW32)
        DWORD attrs = GetFileAttributesA(test_data);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            path = base;
        }
#else
        if (access(test_data, F_OK) == 0) {
            path = base;
        }
#endif
        /* Check base/../Resources/espeak-ng-data (macOS app bundle: path points to Resources) */
        if (!path) {
            snprintf(test_data, sizeof(test_data), "%s../Resources/espeak-ng-data", base);
            if (access(test_data, F_OK) == 0) {
                snprintf(bundle_dir, sizeof(bundle_dir), "%s../Resources", base);
                path = bundle_dir;
            }
        }
    }
    /* Check current directory ./espeak-ng-data */
    if (!path && access("espeak-ng-data", F_OK) == 0) {
        path = ".";
    }
    /* If path is still NULL, espeak_Initialize uses compiled-in system default path
       (e.g., /usr/share/espeak-ng-data on Ubuntu / Linux or Homebrew prefix on macOS) */

	/* Initialize eSpeak in AUDIO_OUTPUT_RETRIEVAL mode: eSpeak generates PCM samples
	   directly and passes them to our callback, bypassing buggy third-party audio drivers
	   (such as libpcaudio CoreAudio crashes on macOS) while routing all sound smoothly
	   through SDL3's audio stream across macOS, Linux, and Windows. */
	int sample_rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 100, path, 0);
	if (sample_rate <= 0)
	{
		text_to_speech_status = 0;
		return 0;
	}

	espeak_SetSynthCallback(tts_synth_callback);

	/* Set up SDL3 Audio Stream for direct playback */
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_S16LE;
	spec.channels = 1;
	spec.freq = sample_rate > 0 ? sample_rate : 22050;

	if (tts_audio_stream)
		SDL_DestroyAudioStream(tts_audio_stream);

	tts_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
	if (!tts_audio_stream)
	{
		fprintf(stderr, "T4K_Tts_init(): Failed to open SDL audio stream: %s\n", SDL_GetError());
		espeak_Terminate();
		text_to_speech_status = 0;
		return 0;
	}
	SDL_ResumeAudioStreamDevice(tts_audio_stream);

	if (!tts_mutex)
		tts_mutex = SDL_CreateMutex();
	if (!tts_cond)
		tts_cond = SDL_CreateCondition();

	tts_worker_active = 1;
	if (!tts_worker_thread)
		tts_worker_thread = SDL_CreateThread(tts_worker_func, "tts_worker", NULL);

	text_to_speech_status = 1;
	T4K_Tts_set_volume(100);
	return 1;
}

void T4K_Tts_set_status(int status)
{
	text_to_speech_status = status;
	if (!status)
		T4K_Tts_stop();
}

/*Used to set person in TTS. in the case of espeak we will set 
 * language by this. return False if language is not available  */
int T4K_Tts_set_voice(char voice_name[]){
	if (!voice_name || voice_name[0] == '\0')
		return 0;
	if (espeak_SetVoiceByName(voice_name) == EE_OK)
		return 1;
	char lang2[3] = { voice_name[0], voice_name[1], '\0' };
	if (espeak_SetVoiceByName(lang2) == EE_OK)
		return 1;
	return 0;
}

//Stop the speech if it is speaking
void T4K_Tts_stop(void)
{
	tts_cancel_requested = 1;
	espeak_Cancel();
	if (tts_audio_stream)
		SDL_ClearAudioStream(tts_audio_stream);

	if (tts_mutex)
	{
		SDL_LockMutex(tts_mutex);
		tts_node *curr = tts_queue_head;
		while (curr)
		{
			tts_node *next = curr->next;
			free(curr);
			curr = next;
		}
		tts_queue_head = NULL;
		tts_queue_tail = NULL;
		SDL_UnlockMutex(tts_mutex);
	}
}

//TTS Parameters
void T4K_Tts_set_volume(int volume){
	if (volume < 0) volume = 0;
	if (volume > 100) volume = 100;
	if (tts_audio_stream)
		SDL_SetAudioStreamGain(tts_audio_stream, (float)volume / 100.0f);
	espeak_SetParameter(espeakVOLUME, 2 * volume, 0);
}

/* Set the rate of TTS.
 * Here in case of espeak the rate is ranging
 * from 80 to 450. So we multiply the given
 * rate with 3.7 and add 80 to get exact
 * rate for espeak  */
void T4K_Tts_set_rate(int rate){
	if (rate != DEFAULT_VALUE)
		espeak_SetParameter(espeakRATE, (3.7 * rate) + 80, 0);
}

void T4K_Tts_set_pitch(int pitch){
	if (pitch != DEFAULT_VALUE)
		espeak_SetParameter(espeakPITCH, pitch, 0);
}

/* Function used to read a text
 * 
 * DEFAULT_VALUE (30) can be passed for rate and pitch
 * 
 * if mode = INTERRUPT then terminate the currently speaking 
 * text and read the new text.
 * 
 * if mode = APPEND then wait till speaking is finished 
 * then read the new text */
void T4K_Tts_say(int rate, int pitch, int mode, const char* text, ...){
	if (!text_to_speech_status || !text || !tts_worker_active)
		return;

	tts_node *node = malloc(sizeof(tts_node));
	if (!node)
		return;

	node->mode = mode;
	node->rate = rate;
	node->pitch = pitch;
	node->next = NULL;

	//Getting the formatted text
	va_list list;
	va_start(list, text);
	vsnprintf(node->text, sizeof(node->text), text, list);
	va_end(list);

	if (tts_mutex)
		SDL_LockMutex(tts_mutex);

	if (mode == INTERRUPT)
	{
		tts_cancel_requested = 1;
		espeak_Cancel();
		if (tts_audio_stream)
			SDL_ClearAudioStream(tts_audio_stream);

		/* Discard any pending queued messages */
		tts_node *curr = tts_queue_head;
		while (curr)
		{
			tts_node *next = curr->next;
			free(curr);
			curr = next;
		}
		tts_queue_head = NULL;
		tts_queue_tail = NULL;
	}

	if (tts_queue_tail)
	{
		tts_queue_tail->next = node;
		tts_queue_tail = node;
	}
	else
	{
		tts_queue_head = node;
		tts_queue_tail = node;
	}

	tts_cancel_requested = 0;

	if (tts_cond)
		SDL_SignalCondition(tts_cond);

	if (tts_mutex)
		SDL_UnlockMutex(tts_mutex);
}	

void T4K_Tts_cleanup(void)
{
	text_to_speech_status = 0;
	tts_cancel_requested = 1;
	if (tts_audio_stream)
		SDL_ClearAudioStream(tts_audio_stream);

	if (tts_mutex)
		SDL_LockMutex(tts_mutex);

	tts_worker_active = 0;

	tts_node *curr = tts_queue_head;
	while (curr)
	{
		tts_node *next = curr->next;
		free(curr);
		curr = next;
	}
	tts_queue_head = NULL;
	tts_queue_tail = NULL;

	if (tts_cond)
		SDL_SignalCondition(tts_cond);

	if (tts_mutex)
		SDL_UnlockMutex(tts_mutex);

	if (tts_worker_thread)
	{
		SDL_WaitThread(tts_worker_thread, NULL);
		tts_worker_thread = NULL;
	}

	if (tts_audio_stream)
	{
		SDL_DestroyAudioStream(tts_audio_stream);
		tts_audio_stream = NULL;
	}

	if (tts_cond)
	{
		SDL_DestroyCondition(tts_cond);
		tts_cond = NULL;
	}

	if (tts_mutex)
	{
		SDL_DestroyMutex(tts_mutex);
		tts_mutex = NULL;
	}

	espeak_Terminate();
}	





#elif defined(HAVE_LIBSPEECHD)

#include <libspeechd.h>


SPDConnection *spd_connection;
int text_to_speech_speaking;

/* TTS annoncement should be in thread otherwise 
 * it will freez the game till announcemrnt finishes */
int tts_thread_func(void *arg)
{
	tts_argument recived = *((tts_argument*)(arg));
	fprintf(stderr,"\nSPD : %s - %d",recived.text,recived.mode);
	
	text_to_speech_speaking = 0;
	
	if (recived.mode == INTERRUPT)
	{	
		T4K_Tts_cancel();
		spd_say(spd_connection, 1, recived.text);
	}
	else
	{

		
		spd_say(spd_connection, 1, recived.text);
		
	}
	
		printf("\nWaiting");
		T4K_Tts_wait();
		printf("\nWaiting over");	

	return 1;
}


//terminate the current speech
void T4K_Tts_cancel()
{
	spd_cancel(spd_connection);
}

//wait till current text is spoken
void T4K_Tts_wait()
{

	while(text_to_speech_speaking == 0)
	{
		SDL_Delay(1);
	};
	 
	text_to_speech_speaking = 1;
}

/* Callback for Speech Dispatcher notifications */
void end_of_speech(size_t msg_id, size_t client_id, SPDNotificationType type)
{
	text_to_speech_speaking = type;
}


//This function should be called at begining 
int T4K_Tts_init()
{
	if (text_to_speech_status)
	{
		spd_connection = spd_open("linux-client221", "linux connection","user_name", SPD_MODE_THREADED);
		text_to_speech_speaking = 1;
		spd_connection->callback_end = end_of_speech;
		spd_set_notification_on(spd_connection, SPD_END);
		return 1;
	}
	return 0;
}

/*Used to set person in TTS. in the case of espeak we will set 
 * language by this. return False if language is not available  */
int T4K_Tts_set_voice(char voice_name[]){
	if (text_to_speech_status)
	{
		spd_set_synthesis_voice(spd_connection,voice_name);
	}
	return 1;
	
}


//Stop the speech if it is speaking
void T4K_Tts_stop(){
	if (tts_thread)
	{
		SDL_WaitThread(tts_thread, NULL);
		tts_thread = NULL;
		spd_cancel(spd_connection);
		text_to_speech_speaking = 1;
	}
}


//TTS Parameters
void T4K_Tts_set_volume(int volume){
	if (text_to_speech_status)
	{
		spd_set_volume(spd_connection,volume);
	}
}


void T4K_Tts_set_rate(int rate){
	if (text_to_speech_status)
	{
		spd_set_voice_rate(spd_connection,rate);
	}

}

void T4K_Tts_set_pitch(int pitch){
	if (text_to_speech_status)
	{
		spd_set_voice_pitch(spd_connection,pitch);
	}
}

/* Function used to read a text
 * 
 * DEFAULT_VALUE (30) can be passed for rate and pitch
 * 
 * if mode = INTERRUPT then terminate the currently speaking 
 * text and read the new text.
 * 
 * if mode = APPEND then wait till speaking is finished 
 * then read the new text */
void T4K_Tts_say(int rate,int pitch,int mode, const char* text, ...){
	tts_argument data_to_pass;
	
	
	if (text_to_speech_status){
	
	T4K_Tts_set_rate(rate);
    T4K_Tts_set_pitch(pitch);

	//Getting the formated text
	va_list list;
	va_start(list,text);
	vsprintf(data_to_pass.text,text,list);
	va_end(list);
	
	//Passing mode
	data_to_pass.mode = mode;
	
	//Calling threded function to say.	
	tts_thread = SDL_CreateThread(tts_thread_func, "tts_worker", &data_to_pass);
	}
}	

#else

int T4K_Tts_init(void) { return 0; }
int T4K_Tts_set_voice(char voice_name[]) { (void)voice_name; return 0; }
void T4K_Tts_stop(void) {}
void T4K_Tts_set_volume(int volume) { (void)volume; }
void T4K_Tts_set_rate(int rate) { (void)rate; }
void T4K_Tts_set_pitch(int pitch) { (void)pitch; }
void T4K_Tts_say(int rate, int pitch, int mode, const char* text, ...) { (void)rate; (void)pitch; (void)mode; (void)text; }
void T4K_Tts_cancel(void) {}
void T4K_Tts_wait(void) {}
void T4K_Tts_set_status(int status) { (void)status; }
void T4K_Tts_cleanup(void) {}

#endif
