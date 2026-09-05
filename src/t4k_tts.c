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

/* TTS annoncement should be in thread otherwise 
 * it will freez the game till announcemrnt finishes */
int SDLCALL tts_thread_func(void *arg)
{
	espeak_POSITION_TYPE position_type = POS_CHARACTER;
	tts_argument* recived = (tts_argument*)arg;
	if (!recived) return 0;

	fprintf(stderr,"\nSpeaking : %s - %d\n", recived->text, recived->mode);
	fflush(stderr);

	if (recived->mode == INTERRUPT)
		T4K_Tts_cancel();
	else
		T4K_Tts_wait();
	
	int Size = strlen(recived->text)+1;
	espeak_ERROR err = espeak_Synth(recived->text, Size, 0, position_type, 0, espeakCHARS_AUTO, 0, NULL);	
	fprintf(stderr, "espeak_Synth returned %d for '%s'\n", (int)err, recived->text);
	fflush(stderr);
	espeak_Synchronize();
	free(recived);
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
	while (espeak_IsPlaying())
	{
		SDL_Delay(30);
	}
}

//This function should be called at begining 
int T4K_Tts_init()
{
    const char* path = NULL;
#if defined(WIN32) || defined(_WIN32) || defined(BUILD_MINGW32)
    path = ".";
#endif
	if(espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 500, path, 0 ) == -1)
	{
		text_to_speech_status = 0;
		return 0;
	}
	else
	{
		text_to_speech_status = 1;
		T4K_Tts_set_volume(100);
		return 1;
	}
}

void T4K_Tts_set_status(int status)
{
	text_to_speech_status = status;
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
void T4K_Tts_stop(){
	espeak_Cancel();
}


//TTS Parameters
void T4K_Tts_set_volume(int volume){
espeak_SetParameter(espeakVOLUME,2*volume,0);
}

/* Set the rate of TTS.
 * Hear in case of espeak the rate is ranging
 * from 80 to 450. So we multiply the given
 * rate with 3.7 and add 80 to get exact
 * rate for espeak  */
void T4K_Tts_set_rate(int rate){
espeak_SetParameter(espeakRATE,(3.7*rate)+80,0);
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
void T4K_Tts_say(int rate,int pitch,int mode, const char* text, ...){
	if (!text_to_speech_status || !text)
		return;

	tts_argument *data_to_pass = malloc(sizeof(tts_argument));
	if (!data_to_pass)
		return;

	T4K_Tts_set_rate(rate);
	T4K_Tts_set_pitch(pitch);
	T4K_Tts_set_volume(100);

	//Getting the formated text
	va_list list;
	va_start(list,text);
	vsnprintf(data_to_pass->text, sizeof(data_to_pass->text), text, list);
	va_end(list);
	
	//Passing mode
	data_to_pass->mode = mode;
	
	if (mode == INTERRUPT)
	{
		T4K_Tts_cancel();
	}

	//Calling threaded function to say.	
	SDL_Thread *th = SDL_CreateThread(tts_thread_func, "tts_worker", data_to_pass);
	if (th)
	{
		SDL_DetachThread(th);
	}
	else
	{
		free(data_to_pass);
	}
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

#endif
