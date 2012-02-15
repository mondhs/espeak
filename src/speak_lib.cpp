/***************************************************************************
 *   Copyright (C) 2005 to 2011 by Jonathan Duddington                     *
 *   email: jonsd@users.sourceforge.net                                    *
 *                                                                         *
 *   Copyright (C) 2012 Reece H. Dunn                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write see:                           *
 *               <http://www.gnu.org/licenses/>.                           *
 ***************************************************************************/

#include "stdio.h"
#include "ctype.h"
#include "string.h"
#include "stdlib.h"
#include "wchar.h"
#include "locale.h"
#include <assert.h>
#include <time.h>

#include "speech.h"

#include <sys/stat.h>
#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#endif

#include "speak_lib.h"
#include "phoneme.h"
#include "synthesize.h"
#include "voice.h"
#include "translate.h"

unsigned char *outbuf=NULL;

espeak_EVENT *event_list=NULL;
int event_list_ix=0;
int n_event_list;
long count_samples;
void* my_audio=NULL;

static void* my_user_data=NULL;
static espeak_AUDIO_OUTPUT my_mode=AUDIO_OUTPUT_SYNCHRONOUS;
static int synchronous_mode = 1;
static int out_samplerate = 0;
static int voice_samplerate = 22050;

t_espeak_callback* synth_callback = NULL;
int (* uri_callback)(int, const char *, const char *) = NULL;
int (* phoneme_callback)(const char *) = NULL;

char path_home[N_PATH_HOME];   // this is the espeak-data directory
extern int saved_parameters[N_SPEECH_PARAM]; //Parameters saved on synthesis start


void WVoiceChanged(voice_t *wvoice)
{//=================================
// Voice change in wavegen
	voice_samplerate = wvoice->samplerate;
}


static void select_output(espeak_AUDIO_OUTPUT output_type)
{//=======================================================
	my_mode = output_type;
	my_audio = NULL;
	synchronous_mode = 1;
 	option_waveout = 1;   // inhibit portaudio callback from wavegen.cpp
	out_samplerate = 0;

	switch(my_mode)
	{
	case AUDIO_OUTPUT_PLAYBACK:
		// wave_init() is now called just before the first wave_write()
		synchronous_mode = 0;
		break;

	case AUDIO_OUTPUT_RETRIEVAL:
		synchronous_mode = 0;
		break;

	case AUDIO_OUTPUT_SYNCHRONOUS:
		break;

	case AUDIO_OUTPUT_SYNCH_PLAYBACK:
		option_waveout = 0;
		WavegenInitSound();
		break;
	}
}   // end of select_output




int GetFileLength(const char *filename)
{//====================================
	struct stat statbuf;
	
	if(stat(filename,&statbuf) != 0)
		return(0);
	
	if((statbuf.st_mode & S_IFMT) == S_IFDIR)
		//	if(S_ISDIR(statbuf.st_mode))
		return(-2);  // a directory
	
	return(statbuf.st_size);
}  // end of GetFileLength


char *Alloc(int size)
{//==================
	char *p;
	if((p = (char *)malloc(size)) == NULL)
		fprintf(stderr,"Can't allocate memory\n");  // I was told that size+1 fixes a crash on 64-bit systems
	return(p);
}

void Free(void *ptr)
{//=================
	if(ptr != NULL)
		free(ptr);
}



static void init_path(const char *path)
{//====================================
#ifdef PLATFORM_WINDOWS
	HKEY RegKey;
	unsigned long size;
	unsigned long var_type;
	char *env;
	unsigned char buf[sizeof(path_home)-13];

	if(path != NULL)
	{
		sprintf(path_home,"%s/espeak-data",path);
		return;
	}

	if((env = getenv("ESPEAK_DATA_PATH")) != NULL)
	{
		sprintf(path_home,"%s/espeak-data",env);
		if(GetFileLength(path_home) == -2)
			return;   // an espeak-data directory exists 
	}

	buf[0] = 0;
	RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Speech\\Voices\\Tokens\\eSpeak", 0, KEY_READ, &RegKey);
	size = sizeof(buf);
	var_type = REG_SZ;
	RegQueryValueExA(RegKey, "path", 0, &var_type, buf, &size);

	sprintf(path_home,"%s\\espeak-data",buf);

#else
	char *env;

	if(path != NULL)
	{
		snprintf(path_home,sizeof(path_home),"%s/espeak-data",path);
		return;
	}

	// check for environment variable
	if((env = getenv("ESPEAK_DATA_PATH")) != NULL)
	{
		snprintf(path_home,sizeof(path_home),"%s/espeak-data",env);
		if(GetFileLength(path_home) == -2)
			return;   // an espeak-data directory exists 
	}

	snprintf(path_home,sizeof(path_home),"%s/espeak-data",getenv("HOME"));
	if(access(path_home,R_OK) != 0)
	{
		strcpy(path_home,PATH_ESPEAK_DATA);
	}
#endif
}

static int initialise(int control)
{//===============================
	int result;

	LoadConfig();
	WavegenInit(22050,0);   // 22050
	if((result = LoadPhData()) != 1)
	{
		if(result == -1)
		{
			fprintf(stderr,"Failed to load espeak-data\n");
			if((control & espeakINITIALIZE_DONT_EXIT) == 0)
			{
				exit(1);
			}
		}
		else
			fprintf(stderr,"Wrong version of espeak-data 0x%x (expects 0x%x) at %s\n",result,version_phdata,path_home);
	}

	memset(&current_voice_selected,0,sizeof(current_voice_selected));
	SetVoiceStack(NULL, "");
	SynthesizeInit();
	InitNamedata();

	for(int param=0; param<N_SPEECH_PARAM; param++)
		param_stack[0].parameter[param] = param_defaults[param];

	return(0);
}


static espeak_ERROR Synthesize(const void *text, int flags)
{//========================================================================================
	// Fill the buffer with output sound
	int length;
	int finished = 0;
	int count_buffers = 0;

	if((outbuf==NULL) || (event_list==NULL))
		return(EE_INTERNAL_ERROR);  // espeak_Initialize()  has not been called

	option_multibyte = flags & 7;
	option_ssml = flags & espeakSSML;
	option_phoneme_input = flags & espeakPHONEMES;
	option_endpause = flags & espeakENDPAUSE;

	count_samples = 0;

	if(translator == NULL)
	{
		espeak_SetVoiceByName("default");
	}

	SpeakNextClause(NULL,text,0);

	if(my_mode == AUDIO_OUTPUT_SYNCH_PLAYBACK)
	{
		for(;;)
		{
#ifdef PLATFORM_WINDOWS
			Sleep(300);   // 0.3s
#else
#ifdef USE_NANOSLEEP
			struct timespec period;
			struct timespec remaining;
			period.tv_sec = 0;
			period.tv_nsec = 300000000;  // 0.3 sec
			nanosleep(&period,&remaining);
#else
			sleep(1);
#endif
#endif
			if(SynthOnTimer() != 0)
				break;
		}
		return(EE_OK);
	}

	for(;;)
	{
		out_ptr = outbuf;
		out_end = &outbuf[outbuf_size];
		event_list_ix = 0;
		WavegenFill();

		length = (out_ptr - outbuf)/2;
		count_samples += length;
		event_list[event_list_ix].type = espeakEVENT_LIST_TERMINATED; // indicates end of event list
		event_list[event_list_ix].unique_identifier = 0;
		event_list[event_list_ix].user_data = my_user_data;

		count_buffers++;
		if (my_mode!=AUDIO_OUTPUT_PLAYBACK)
		{
			finished = synth_callback((short *)outbuf, length, event_list);
		}
		if(finished)
		{
			SpeakNextClause(NULL,0,2);  // stop
			break;
		}

		if(Generate(phoneme_list,&n_phoneme_list,1)==0)
		{
			if(WcmdqUsed() == 0)
			{
				// don't process the next clause until the previous clause has finished generating speech.
				// This ensures that <audio> tag (which causes end-of-clause) is at a sound buffer boundary

				event_list[0].type = espeakEVENT_LIST_TERMINATED;
				event_list[0].unique_identifier = 0;
				event_list[0].user_data = my_user_data;

				if(SpeakNextClause(NULL,NULL,1)==0)
				{
					synth_callback(NULL, 0, event_list);  // NULL buffer ptr indicates end of data
					break;
				}
			}
		}
	}
	return(EE_OK);
}  //  end of Synthesize


void MarkerEvent(int type, unsigned int char_position, int value, int value2, unsigned char *out_ptr)
{//==================================================================================================
	// type: 1=word, 2=sentence, 3=named mark, 4=play audio, 5=end, 7=phoneme
	
	if((event_list == NULL) || (event_list_ix >= (n_event_list-2)))
		return;
	
	espeak_EVENT *ep = &event_list[event_list_ix++];
	ep->type = (espeak_EVENT_TYPE)type;
	ep->unique_identifier = 0;
	ep->user_data = my_user_data;
	ep->text_position = char_position & 0xffffff;
	ep->length = char_position >> 24;
	
	double time = (double(count_samples + mbrola_delay + (out_ptr - out_start)/2)*1000.0)/samplerate;
	ep->audio_position = int(time);
	ep->sample = (count_samples + mbrola_delay + (out_ptr - out_start)/2);
	
	if((type == espeakEVENT_MARK) || (type == espeakEVENT_PLAY))
		ep->id.name = &namedata[value];
	else
//#ifdef deleted
// temporarily removed, don't introduce until after eSpeak version 1.46.02
	if(type == espeakEVENT_PHONEME)
	{
		int *p;
		p = (int *)(ep->id.string);
		p[0] = value;
		p[1] = value2;
	}
	else
//#endif
	{
		ep->id.number = value;
	}
}  //  end of MarkerEvent




espeak_ERROR sync_espeak_Synth(const void *text, size_t size, 
		      unsigned int position, espeak_POSITION_TYPE position_type, 
		      unsigned int end_position, unsigned int flags, void* user_data)
{//===========================================================================
	
	InitText(flags);
	my_user_data = user_data;
	
	for (int i=0; i < N_SPEECH_PARAM; i++)
		saved_parameters[i] = param_stack[0].parameter[i];

	switch(position_type)
		{
		case POS_CHARACTER:
			skip_characters = position;
			break;
	
		case POS_WORD:
			skip_words = position;
			break;
	
		case POS_SENTENCE:
			skip_sentences = position;
			break;
	
		}
	if(skip_characters || skip_words || skip_sentences)
		skipping_text = 1;
	
	end_character_position = end_position;
	return Synthesize(text, flags);
}  //  end of sync_espeak_Synth




espeak_ERROR sync_espeak_Synth_Mark(const void *text, size_t size, 
			   const char *index_mark, unsigned int end_position, 
			   unsigned int flags, void* user_data)
{//=========================================================================
	InitText(flags);
	
	my_user_data = user_data;
	
	if(index_mark != NULL)
		{
		strncpy0(skip_marker, index_mark, sizeof(skip_marker));
		skipping_text = 1;
		}
	
	end_character_position = end_position;
	return Synthesize(text, flags | espeakSSML);
}  //  end of sync_espeak_Synth_Mark


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Key(const char *key)
{//==================================
	// symbolic name, symbolicname_character  - is there a system resource of symbolic names per language?
	int letter;

	int ix = utf8_in(&letter,key);
	if(key[ix] == 0)
	{
		// a single character
		return espeak_Char(letter);
	}

	my_user_data = NULL;
	return Synthesize(key,0);   // speak key as a text string
}
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Char(wchar_t character)
{//=====================================
	// is there a system resource of character names per language?
	char buf[80];
	my_user_data = NULL;
	
	sprintf(buf,"<say-as interpret-as=\"tts:char\">&#%d;</say-as>",character);
	return Synthesize(buf,espeakSSML);
}
#pragma GCC visibility pop


void sync_espeak_SetPunctuationList(const wchar_t *punctlist)
{//==========================================================
	// Set the list of punctuation which are spoken for "some".
	my_user_data = NULL;
	
	wcsncpy(option_punctlist, punctlist, N_PUNCTLIST);
	option_punctlist[N_PUNCTLIST-1] = 0;
}  //  end of sync_espeak_SetPunctuationList




#pragma GCC visibility push(default)
ESPEAK_API void espeak_SetSynthCallback(t_espeak_callback* SynthCallback)
{//======================================================================
	synth_callback = SynthCallback;
}
#pragma GCC visibility pop

#pragma GCC visibility push(default)
ESPEAK_API void espeak_SetUriCallback(int (* UriCallback)(int, const char*, const char *))
{//=======================================================================================
	uri_callback = UriCallback;
}
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API void espeak_SetPhonemeCallback(int (* PhonemeCallback)(const char*))
{//===========================================================================
	phoneme_callback = PhonemeCallback;
}
#pragma GCC visibility pop

#pragma GCC visibility push(default)
ESPEAK_API int espeak_Initialize(espeak_AUDIO_OUTPUT output_type, int buf_length, const char *path, int options)
{//=============================================================================================================
	// It seems that the wctype functions don't work until the locale has been set
	// to something other than the default "C".  Then, not only Latin1 but also the
	// other characters give the correct results with iswalpha() etc.
#ifdef PLATFORM_RISCOS
	setlocale(LC_CTYPE,"ISO8859-1");
#else
	if(setlocale(LC_CTYPE,"en_US.UTF-8") == NULL)
	{
		if(setlocale(LC_CTYPE,"UTF-8") == NULL)
			setlocale(LC_CTYPE,"");
	}
#endif
	
	init_path(path);
	initialise(options);
	select_output(output_type);

	// buflength is in mS, allocate 2 bytes per sample
	if(buf_length == 0)
		buf_length = 200;
	outbuf_size = (buf_length * samplerate)/500;
	outbuf = (unsigned char*)realloc(outbuf,outbuf_size);
	if((out_start = outbuf) == NULL)
		return(EE_INTERNAL_ERROR);
	
	// allocate space for event list.  Allow 200 events per second.
	// Add a constant to allow for very small buf_length
	n_event_list = (buf_length*200)/1000 + 20;
	if((event_list = (espeak_EVENT *)realloc(event_list,sizeof(espeak_EVENT) * n_event_list)) == NULL)
		return(EE_INTERNAL_ERROR);
	
	option_phonemes = 0;
	option_mbrola_phonemes = 0;
	option_phoneme_events = (options & (espeakINITIALIZE_PHONEME_EVENTS | espeakINITIALIZE_PHONEME_IPA));

	VoiceReset(0);
	
	for(int param=0; param<N_SPEECH_PARAM; param++)
		param_stack[0].parameter[param] = saved_parameters[param] = param_defaults[param];
	
	espeak_SetParameter(espeakRATE,175,0);
	espeak_SetParameter(espeakVOLUME,100,0);
	espeak_SetParameter(espeakCAPITALS,option_capitals,0);
	espeak_SetParameter(espeakPUNCTUATION,option_punctuation,0);
	espeak_SetParameter(espeakWORDGAP,0,0);
//	DoVoiceChange(voice);
	
  return(samplerate);
}
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Synth(const void *text, size_t size, 
				     unsigned int position, 
				     espeak_POSITION_TYPE position_type,
				     unsigned int end_position, unsigned int flags, 
				     unsigned int* unique_identifier, void* user_data)
{//=====================================================================================

	if (unique_identifier != NULL)
		*unique_identifier = 0;

	if(synchronous_mode)
	{
		return(sync_espeak_Synth(text,size,position,position_type,end_position,flags,user_data));
	}

	return EE_INTERNAL_ERROR;
}  //  end of espeak_Synth
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Synth_Mark(const void *text, size_t size, 
					  const char *index_mark, 
					  unsigned int end_position, 
					  unsigned int flags, 
					  unsigned int* unique_identifier,
					  void* user_data)
{//=========================================================================

	if (unique_identifier != NULL)
		*unique_identifier = 0;

	if(synchronous_mode)
	{
		return(sync_espeak_Synth_Mark(text,size,index_mark,end_position,flags,user_data));
	}

	return EE_OK;
}  //  end of espeak_Synth_Mark
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API int espeak_GetParameter(espeak_PARAMETER parameter, int current)
{//========================================================================
	// current: 0=default value, 1=current value
	if(current)
	{
		return(param_stack[0].parameter[parameter]);
	}
	else
	{
		return(param_defaults[parameter]);
	}
}  //  end of espeak_GetParameter
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_SetPunctuationList(const wchar_t *punctlist)
{//================================================================
  // Set the list of punctuation which are spoken for "some".

	sync_espeak_SetPunctuationList(punctlist);
	return(EE_OK);
}  //  end of espeak_SetPunctuationList
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API void espeak_SetPhonemeTrace(int value, FILE *stream)
{//============================================================
	/* Controls the output of phoneme symbols for the text
		bits 0-3:
		 value=0  No phoneme output (default)
		 value=1  Output the translated phoneme symbols for the text
		 value=2  as (1), but also output a trace of how the translation was done (matching rules and list entries)
		 value=3  as (1), but produces IPA phoneme names rather than ascii 
		bit 4:   produce mbrola pho data
	*/
	option_phonemes = value & 3;
	option_mbrola_phonemes = value & 16;
	f_trans = stream;
	if(stream == NULL)
		f_trans = stderr;
	
}   //  end of espeak_SetPhonemes
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API void espeak_CompileDictionary(const char *path, FILE *log, int flags)
{//=============================================================================
	CompileDictionary(path, dictionary_name, log, NULL, flags);
}   //  end of espeak_CompileDirectory
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Cancel(void)
{//===============================
	embedded_value[EMBED_T] = 0;    // reset echo for pronunciation announcements

	for (int i=0; i < N_SPEECH_PARAM; i++)
		espeak_SetParameter((espeak_PARAMETER)i, saved_parameters[i], 0);

	return EE_OK;
}   //  end of espeak_Cancel
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API int espeak_IsPlaying(void)
{//==================================
	return(0);
}   //  end of espeak_IsPlaying
#pragma GCC visibility pop


#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Synchronize(void)
{//=============================================
	return EE_OK;
}   //  end of espeak_Synchronize
#pragma GCC visibility pop


extern void FreePhData(void);
extern void FreeVoiceList(void);

#pragma GCC visibility push(default)
ESPEAK_API espeak_ERROR espeak_Terminate(void)
{//===========================================
	Free(event_list);
	event_list = NULL;
	Free(outbuf);
	outbuf = NULL;
	FreePhData();
	FreeVoiceList();

	return EE_OK;
}   //  end of espeak_Terminate
#pragma GCC visibility pop

#pragma GCC visibility push(default)
ESPEAK_API const char *espeak_Info(const char **ptr)
{//=================================================
	if(ptr != NULL)
	{
		*ptr = path_home;
	}
	return(version_string);
}
#pragma GCC visibility pop
