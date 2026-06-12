/*
   t4k_tts.c:

   Text-To-Speech-related functions.

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
#include <stdarg.h>
#include "SDL_thread.h"
#include <wchar.h>

SDL_Thread *tts_thread = NULL;
int text_to_speech_status = 0;
volatile int text_to_speech_speaking = 1;

#if WITH_ESPEAK == 1

#if WITH_ESPEAK_NG
# include <espeak-ng/speak_lib.h>
#else
# include <speak_lib.h>
#endif

void T4K_Tts_cancel(void);
void T4K_Tts_wait(void);

/* TTS announcement should be in thread otherwise 
 * it will freeze the game till announcement finishes */
int tts_thread_func(void *arg)
{
	espeak_POSITION_TYPE position_type = POS_CHARACTER;
	tts_argument recived = *((tts_argument*)(arg));
	free(arg);
	fprintf(stderr,"\nSpeaking : %ls - %d\n", recived.text, recived.mode);
	if (recived.mode == INTERRUPT)
		T4K_Tts_cancel();
	else
		T4K_Tts_wait();
	
	espeak_Synth(recived.text, 0, 0, position_type, 0, espeakCHARS_WCHAR, 0, NULL);	
	espeak_Synchronize();
	return 1;
}


//terminate the current speech
void T4K_Tts_cancel()
{
	espeak_Cancel();
}

//wait till current text is spoken
void T4K_Tts_wait()
{
	while (espeak_IsPlaying() && tts_thread)
	{
		SDL_Delay(1);
	}
	SDL_Delay(30); 
}

//This function should be called at beginning 
int T4K_Tts_init()
{
	if (text_to_speech_status)
	{
		if(espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 500, NULL, 0 ) == -1)
			return 0;
		else
			return 1;
	}
	return 0;
}

/*Used to set person in TTS. in the case of espeak we will set 
 * language by this. return False if language is not available  */
int T4K_Tts_set_voice(char voice_name[]){
	if (espeak_SetVoiceByName(voice_name) == EE_OK)
		return 1;
	else
		return 0;
}


//Stop the speech if it is speaking
void T4K_Tts_stop(){
	espeak_Cancel();
	if (tts_thread)
	{
		SDL_WaitThread(tts_thread, NULL);
		tts_thread = NULL;
	}
}


//TTS Parameters
void T4K_Tts_set_volume(int volume){
	espeak_SetParameter(espeakVOLUME,2*volume,0);
}

/* Set the rate of TTS.
 * Here in case of espeak the rate is ranging
 * from 80 to 450. So we multiply the given
 * rate with 3.7 and add 80 to get exact
 * rate for espeak  */
void T4K_Tts_set_rate(int rate){
	espeak_SetParameter(espeakVOLUME,(3.7*rate)+80,0);
}

void T4K_Tts_set_pitch(int pitch){
	espeak_SetParameter(espeakPITCH,pitch,0);
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
void T4K_Tts_say(int rate,int pitch,int mode, const char* text, ...)
{
	tts_argument *data_to_pass;

	if (text_to_speech_status)
	{
		if (mode == INTERRUPT)
		{
			T4K_Tts_stop();
		}
		else
		{
			if (tts_thread)
			{
				SDL_WaitThread(tts_thread, NULL);
				tts_thread = NULL;
			}
		}

		text_to_speech_speaking = 0;

		T4K_Tts_set_rate(rate);
		T4K_Tts_set_pitch(pitch);

		data_to_pass = malloc(sizeof(tts_argument));
		if (!data_to_pass) return;

		char temp[10000];
		va_list list;
		va_start(list, text);
		vsnprintf(temp, sizeof(temp), text, list);
		va_end(list);

		mbstowcs(data_to_pass->text, temp, sizeof(data_to_pass->text)/sizeof(wchar_t) - 1);
		data_to_pass->text[sizeof(data_to_pass->text)/sizeof(wchar_t) - 1] = L'\0';

		data_to_pass->mode = mode;

		tts_thread = SDL_CreateThread(tts_thread_func, data_to_pass);
	}
}


#else

#include <libspeechd.h>

void T4K_Tts_cancel(void);
void T4K_Tts_wait(void);

SPDConnection *spd_connection = NULL;

/* TTS announcement should be in thread otherwise 
 * it will freeze the game till announcement finishes */
int tts_thread_func(void *arg)
{
	tts_argument recived = *((tts_argument*)(arg));
	free(arg);
	
	if (recived.mode == INTERRUPT)
	{	
		T4K_Tts_cancel();
	}
	
	if (spd_connection)
	{
		char utf8[10000];
		wcstombs(utf8, recived.text, sizeof(utf8)-1);
		utf8[sizeof(utf8)-1] = '\0';
		spd_say(spd_connection, SPD_IMPORTANT, utf8);
	}
	
	return 1;
}


//terminate the current speech
void T4K_Tts_cancel()
{
	if (spd_connection)
	{
		spd_cancel(spd_connection);
	}
}

//wait till current text is spoken
void T4K_Tts_wait()
{
	while(text_to_speech_speaking == 0)
	{
		SDL_Delay(1);
	}
	text_to_speech_speaking = 1;
}

/* Callback for Speech Dispatcher notifications */
void end_of_speech(size_t msg_id, size_t client_id, SPDNotificationType type)
{
	text_to_speech_speaking = type;
}


//This function should be called at beginning 
int T4K_Tts_init()
{
	if (text_to_speech_status)
	{
		spd_connection = spd_open("linux-client221", "linux connection", "user_name", SPD_MODE_THREADED);
		if (!spd_connection)
		{
			text_to_speech_status = 0;
			return 0;
		}
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
	if (text_to_speech_status && spd_connection)
	{
		spd_set_synthesis_voice(spd_connection, voice_name);
	}
	return 1;
}


//Stop the speech if it is speaking
void T4K_Tts_stop(){
	if (spd_connection)
	{
		spd_cancel(spd_connection);
	}
	if (tts_thread)
	{
		SDL_WaitThread(tts_thread, NULL);
		tts_thread = NULL;
	}
	text_to_speech_speaking = 1;
}


//TTS Parameters
void T4K_Tts_set_volume(int volume){
	if (text_to_speech_status && spd_connection)
	{
		spd_set_volume(spd_connection, volume);
	}
}


void T4K_Tts_set_rate(int rate){
	if (text_to_speech_status && spd_connection)
	{
		spd_set_voice_rate(spd_connection, rate);
	}
}

void T4K_Tts_set_pitch(int pitch){
	if (text_to_speech_status && spd_connection)
	{
		spd_set_voice_pitch(spd_connection, pitch);
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
	tts_argument *data_to_pass;
	
	if (text_to_speech_status && spd_connection){
		if (mode == INTERRUPT)
		{
			T4K_Tts_stop();
		}
		else
		{
			if (tts_thread)
			{
				SDL_WaitThread(tts_thread, NULL);
				tts_thread = NULL;
			}
		}

		text_to_speech_speaking = 0;

		T4K_Tts_set_rate(rate);
		T4K_Tts_set_pitch(pitch);

		data_to_pass = malloc(sizeof(tts_argument));
		if (!data_to_pass) return;

		char temp[10000];
		va_list list;
		va_start(list, text);
		vsnprintf(temp, sizeof(temp), text, list);
		va_end(list);
		
		mbstowcs(data_to_pass->text, temp, sizeof(data_to_pass->text)/sizeof(wchar_t) - 1);
		data_to_pass->text[sizeof(data_to_pass->text)/sizeof(wchar_t) - 1] = L'\0';
		
		data_to_pass->mode = mode;
		
		tts_thread = SDL_CreateThread(tts_thread_func, data_to_pass);
	}
}	


#endif
