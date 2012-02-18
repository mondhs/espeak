/***************************************************************************
 *   Copyright (C) 2005 to 2011 by Jonathan Duddington                     *
 *   email: jonsd@users.sourceforge.net                                    *
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

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <wchar.h>
#include <math.h>

#include <byteswap.h>

#include "speak_lib.h"
#include "speech.h"
#include "phoneme.h"
#include "synthesize.h"
#include "voice.h"
#include "translate.h"

#include <unistd.h>

#include <locale.h>
#define N_XML_BUF   256


static const char *xmlbase = "";    // base URL from <speak>

static int namedata_ix=0;
static int n_namedata = 0;
char *namedata = NULL;


static FILE *f_input = NULL;
static int ungot_char2 = 0;
unsigned char *p_textinput;
wchar_t *p_wchar_input;
static int ungot_char;
static const char *ungot_word = NULL;
static int end_of_input;

static int ignore_text=0;   // set during <sub> ... </sub>  to ignore text which has been replaced by an alias
static int audio_text=0;    // set during <audio> ... </audio> 
static int clear_skipping_text = 0;  // next clause should clear the skipping_text flag
int count_characters = 0;

// ignore these characters
static const unsigned short chars_ignore[] = {
  0x200c,  // zero width non-joiner
  0x200d,  // zero width joiner
  0 };

// punctuations symbols that can end a clause
static const unsigned short punct_chars[] = {',','.','?','!',':',';',
  0x2013,  // en-dash
  0x2014,  // em-dash
  0x2026,  // elipsis

  0x037e,  // Greek question mark (looks like semicolon)
  0x0387,  // Greek semicolon, ano teleia
  0x0964,  // Devanagari Danda (fullstop)

  0x0589,  // Armenian period
  0x055d,  // Armenian comma
  0x055c,  // Armenian exclamation
  0x055e,  // Armenian question
  0x055b,  // Armenian emphasis mark

  0x0b1b,  // Arabic ;
  0x061f,  // Arabic ?

  0x0f0d,  // Tibet Shad
  0x0f0e,

  0x1362,  // Ethiopic period
  0x1363,
  0x1364,
  0x1365,
  0x1366,
  0x1367,
  0x1368,
  0x10fb,  // Georgian paragraph

  0x3001,  // ideograph comma
  0x3002,  // ideograph period

  0xff01,  // fullwidth exclamation
  0xff0c,  // fullwidth comma
  0xff0e,  // fullwidth period
  0xff1a,  // fullwidth colon
  0xff1b,  // fullwidth semicolon
  0xff1f,  // fullwidth question mark
  
  0};


// indexed by (entry num. in punct_chars) + 1
// bits 0-7 pause x 10mS, bits 12-14 intonation type, bit 15 don't need following space or bracket
static const unsigned int punct_attributes [] = { 0,
  CLAUSE_COMMA, CLAUSE_PERIOD, CLAUSE_QUESTION, CLAUSE_EXCLAMATION, CLAUSE_COLON, CLAUSE_SEMICOLON,
  CLAUSE_SEMICOLON,  // en-dash
  CLAUSE_SEMICOLON,  // em-dash
  CLAUSE_SEMICOLON | PUNCT_SAY_NAME | 0x8000,      // elipsis

  CLAUSE_QUESTION,   // Greek question mark
  CLAUSE_SEMICOLON,  // Greek semicolon
  CLAUSE_PERIOD | 0x8000,     // Devanagari Danda (fullstop)

  CLAUSE_PERIOD | 0x8000,  // Armenian period
  CLAUSE_COMMA,     // Armenian comma
  CLAUSE_EXCLAMATION | PUNCT_IN_WORD,  // Armenian exclamation
  CLAUSE_QUESTION | PUNCT_IN_WORD,  // Armenian question
  CLAUSE_PERIOD | PUNCT_IN_WORD,  // Armenian emphasis mark

  CLAUSE_SEMICOLON,  // Arabic ;
  CLAUSE_QUESTION,   // Arabic question mark

  CLAUSE_PERIOD+0x8000,     // Tibet period
  CLAUSE_PARAGRAPH,

  CLAUSE_PERIOD,     // Ethiopic period
  CLAUSE_COMMA,      // Ethiopic comma
  CLAUSE_SEMICOLON,  // Ethiopic semicolon
  CLAUSE_COLON,      // Ethiopic colon
  CLAUSE_COLON,      // Ethiopic preface colon
  CLAUSE_QUESTION,   // Ethiopic question mark
  CLAUSE_PARAGRAPH,     // Ethiopic paragraph
  CLAUSE_PARAGRAPH,     // Georgian paragraph

  CLAUSE_COMMA+0x8000,      // ideograph comma
  CLAUSE_PERIOD+0x8000,     // ideograph period

  CLAUSE_EXCLAMATION+0x8000, // fullwidth
  CLAUSE_COMMA+0x8000,
  CLAUSE_PERIOD+0x8000,
  CLAUSE_COLON+0x8000,
  CLAUSE_SEMICOLON+0x8000,
  CLAUSE_QUESTION+0x8000,

  CLAUSE_SEMICOLON,  // spare
  0 };


// stack for language and voice properties
// frame 0 is for the defaults, before any ssml tags.
typedef struct {
	int tag_type;
	int voice_variant_number;
	int voice_gender;
	int voice_age;
	char voice_name[40];
	char language[20];
} SSML_STACK;

#define N_SSML_STACK  20
static int n_ssml_stack;
static SSML_STACK ssml_stack[N_SSML_STACK];

static espeak_VOICE base_voice;
static char base_voice_variant_name[40] = {0};
static char current_voice_id[40] = {0};


#define N_PARAM_STACK  20
static int n_param_stack;
PARAM_STACK param_stack[N_PARAM_STACK];

static int speech_parameters[N_SPEECH_PARAM];     // current values, from param_stack
int saved_parameters[N_SPEECH_PARAM];             //Parameters saved on synthesis start

const int param_defaults[N_SPEECH_PARAM] = {
   0,     // silence (internal use)
  175,    // rate wpm
  100,    // volume
   50,    // pitch
   50,    // range
   0,     // punctuation
   0,     // capital letters
   0,     // wordgap
   0,     // options
   0,     // intonation
   0,
   0,
   0,     // emphasis
   0,     // line length
   0,     // voice type
};

int towlower2(unsigned int c)
{
	// check for non-standard upper to lower case conversions
	if(c == 'I')
	{
		if(translator->translator_name == L('t','r'))
		{
			c = 0x131;   // I -> ı
		}
	}
	return(towlower(c));
}


static int IsRomanU(unsigned int c)
{//================================
	if((c=='I') || (c=='V') || (c=='X') || (c=='L'))
		return(1);
	return(0);
}


static void GetC_unget(int c)
{//==========================
// This is only called with UTF8 input, not wchar input
	if(f_input != NULL)
		ungetc(c,f_input);
	else
	{
		p_textinput--;
		*p_textinput = c;
		end_of_input = 0;
	}
}

int Eof(void)
{//==========
	if(ungot_char != 0)
		return(0);

	if(f_input != 0)
		return(feof(f_input));

	return(end_of_input);
}


static int GetC_get(void)
{//======================
	if(f_input != NULL)
	{
		unsigned int c = fgetc(f_input);
		if(feof(f_input)) c = ' ';

		if(option_multibyte == espeakCHARS_16BIT)
		{
			unsigned int c2 = fgetc(f_input);
			if(feof(f_input)) c2 = 0;
			c = c + (c2 << 8);
		}
		return(c);
	}

	if(option_multibyte == espeakCHARS_WCHAR)
	{
		if(*p_wchar_input == 0)
		{
			end_of_input = 1;
			return(0);
		}

		if(!end_of_input)
			return(*p_wchar_input++);
	}
	else
	{
		if(*p_textinput == 0)
		{
			end_of_input = 1;
			return(0);
		}
	
		if(!end_of_input)
		{
			if(option_multibyte == espeakCHARS_16BIT)
			{
				unsigned int c = p_textinput[0] + (p_textinput[1] << 8);
				p_textinput += 2;
				return(c);
			}
			return(*p_textinput++ & 0xff);
		}
	}
	return(0);
}


static int GetC(void)
{//==================
// Returns a unicode wide character
// Performs UTF8 checking and conversion

	int c1;
	int c2;
	int cbuf[4];
	int ix;
	static int ungot2 = 0;
	static const unsigned char mask[4] = {0xff,0x1f,0x0f,0x07};

	if((c1 = ungot_char) != 0)
	{
		ungot_char = 0;
		return(c1);
	}

	if(ungot2 != 0)
	{
		c1 = ungot2;
		ungot2 = 0;
	}
	else
	{
		c1 = GetC_get();
	}

	if((option_multibyte == espeakCHARS_WCHAR) || (option_multibyte == espeakCHARS_16BIT))
	{
		count_characters++;
		return(c1);   // wchar_t  text
	}

	if((option_multibyte < 2) && (c1 & 0x80))
	{
		// multi-byte utf8 encoding, convert to unicode
		int n_bytes = 0;

		if(((c1 & 0xe0) == 0xc0) && ((c1 & 0x1e) != 0))
			n_bytes = 1;
		else
		if((c1 & 0xf0) == 0xe0)
			n_bytes = 2;
		else
		if(((c1 & 0xf8) == 0xf0) && ((c1 & 0x0f) <= 4))
			n_bytes = 3;

		if((ix = n_bytes) > 0)
		{
			int c = c1 & mask[ix];
			while(ix > 0)
			{
				if((c2 = cbuf[ix] = GetC_get()) == 0)
				{
					if(option_multibyte==espeakCHARS_AUTO)
						option_multibyte=espeakCHARS_8BIT;   // change "auto" option to "no"
					GetC_unget(' ');
					break;
				}

				if((c2 & 0xc0) != 0x80)
				{
					// This is not UTF8.  Change to 8-bit characterset.
					if((n_bytes == 2) && (ix == 1))
						ungot2 = cbuf[2];
					GetC_unget(c2);
					break;
				}
				c = (c << 6) + (c2 & 0x3f);
				ix--;
			}
			if(ix == 0)
			{
				count_characters++;
				return(c);
			}
		}
		// top-bit-set character is not utf8, drop through to 8bit charset case
		if((option_multibyte==espeakCHARS_AUTO) && !Eof())
			option_multibyte=espeakCHARS_8BIT;   // change "auto" option to "no"
	}

	// 8 bit character set, convert to unicode if
	count_characters++;
	if(c1 >= 0xa0)
		return(translator->charset_a0[c1-0xa0]);
	return(c1);
}  // end of GetC


static void UngetC(int c)
{//======================
	ungot_char = c;
}


static const char *WordToString2(unsigned int word)
{//================================================
// Convert a language mnemonic word into a string
	static char buf[5];
	char *p = buf;
	for(int ix=3; ix>=0; ix--)
	{
		if((*p = word >> (ix*8)) != 0)
			p++;
	}
	*p = 0;
	return(buf);
}


static const char *LookupSpecial(Translator *tr, const char *string, char* text_out)
{//=================================================================================
	unsigned int flags[2];
	char phonemes[55];
	char phonemes2[55];
	char *string1 = (char *)string;

	flags[0] = flags[1] = 0;
	if(LookupDictList(tr,&string1,phonemes,flags,0,NULL))
	{
		SetWordStress(tr, phonemes, flags, -1, 0);
		DecodePhonemes(phonemes,phonemes2);
		sprintf(text_out,"[\002%s]]",phonemes2);
		return(text_out);
	}
	return(NULL);
}


static const char *LookupCharName(Translator *tr, int c, int only)
{//===============================================================
// Find the phoneme string (in ascii) to speak the name of character c
// Used for punctuation characters and symbols

	unsigned int flags[2];
	char single_letter[24];
	char phonemes[60];
	char phonemes2[60];
	const char *lang_name = NULL;
	static char buf[60];

	buf[0] = 0;
	flags[0] = 0;
	flags[1] = 0;
	single_letter[0] = 0;
	single_letter[1] = '_';
	int ix = utf8_out(c,&single_letter[2]);
	single_letter[2+ix]=0;

	if(only)
	{
		char *string = &single_letter[2];
		LookupDictList(tr, &string, phonemes, flags, 0, NULL);
	}
	else
	{
		char *string = &single_letter[1];
		if(LookupDictList(tr, &string, phonemes, flags, 0, NULL) == 0)
		{
			// try _* then *
			string = &single_letter[2];
			if(LookupDictList(tr, &string, phonemes, flags, 0, NULL) == 0)
			{
				// now try the rules
				single_letter[1] = ' ';
				TranslateRules(tr, &single_letter[2], phonemes, sizeof(phonemes), NULL,0,NULL);
			}
		}
	}

	if((only==0) && (phonemes[0] == 0) && (tr->translator_name != L('e','n')))
	{
		// not found, try English
		SetTranslator2("en");
		char *string = &single_letter[1];
		single_letter[1] = '_';
		if(LookupDictList(translator2, &string, phonemes, flags, 0, NULL) == 0)
		{
			string = &single_letter[2];
			LookupDictList(translator2, &string, phonemes, flags, 0, NULL);
		}
		if(phonemes[0])
		{
			lang_name = "en";
		}
		else
		{
			SelectPhonemeTable(voice->phoneme_tab_ix);  // revert to original phoneme table
		}
	}

	if(phonemes[0])
	{
		if(lang_name)
		{
			SetWordStress(translator2, phonemes, flags, -1, 0);
			DecodePhonemes(phonemes,phonemes2);
			sprintf(buf,"[\002_^_%s %s _^_%s]]","en",phonemes2,WordToString2(tr->translator_name));
			SelectPhonemeTable(voice->phoneme_tab_ix);  // revert to original phoneme table
		}
		else
		{
			SetWordStress(tr, phonemes, flags, -1, 0);
			DecodePhonemes(phonemes,phonemes2);
			sprintf(buf,"[\002%s]] ",phonemes2);
		}
	}
	else
	if(only == 0)
	{
		strcpy(buf,"[\002(X1)(X1)(X1)]]");
	}

	return(buf);
}

int Read4Bytes(FILE *f)
{//====================
// Read 4 bytes (least significant first) into a word
	int acc=0;

	for(int ix=0; ix<4; ix++)
	{
		unsigned char c = fgetc(f) & 0xff;
		acc += (c << (ix*8));
	}
	return(acc);
}


static int LoadSoundFile(const char *fname, int index)
{//===================================================
	char *p;
	char fname_temp[100];
	char fname2[sizeof(path_home)+13+40];

	if(fname == NULL)
	{
		// filename is already in the table
		fname = soundicon_tab[index].filename;
	}

	if(fname==NULL)
		return(1);

	if(fname[0] != '/')
	{
		// a relative path, look in espeak-data/soundicons
		sprintf(fname2,"%s%csoundicons%c%s",path_home,PATHSEP,PATHSEP,fname);
		fname = fname2;
	}

	FILE *f = NULL;
	if((f = fopen(fname,"rb")) != NULL)
	{
		int fd_temp;
		const char *resample;
		int header[3];
		char command[sizeof(fname2)+sizeof(fname2)+40];

		fseek(f,20,SEEK_SET);
		for(int ix=0; ix<3; ix++)
			header[ix] = Read4Bytes(f);

		// if the sound file is not mono, 16 bit signed, at the correct sample rate, then convert it
		if((header[0] != 0x10001) || (header[1] != samplerate) || (header[2] != samplerate*2))
		{
			fclose(f);
			f = NULL;

			if(header[2] == samplerate)
				resample = "";
			else
				resample = "polyphase";

			strcpy(fname_temp,"/tmp/espeakXXXXXX");
			if((fd_temp = mkstemp(fname_temp)) >= 0)
			{
				close(fd_temp);
				sprintf(command,"sox \"%s\" -r %d -w -s -c1 %s %s\n", fname, samplerate, fname_temp, resample);
				if(system(command) == 0)
				{
					fname = fname_temp;
				}
			}
		}
	}

	if(f == NULL)
	{
		f = fopen(fname,"rb");
		if(f == NULL)
		{
			return(3);
		}
	}

	int length = GetFileLength(fname);
	fseek(f,0,SEEK_SET);
	if((p = (char *)realloc(soundicon_tab[index].data, length)) == NULL)
	{
		fclose(f);
		return(4);
	}
	length = fread(p,1,length,f);
	fclose(f);
	remove(fname_temp);

	int *ip = (int *)(&p[40]);
	soundicon_tab[index].length = (*ip) / 2;  // length in samples
	soundicon_tab[index].data = p;
	return(0);
}  //  end of LoadSoundFile


static int LookupSoundicon(int c)
{//==============================
// Find the sound icon number for a punctuation chatacter
	for(int ix=N_SOUNDICON_SLOTS; ix<n_soundicon_tab; ix++)
	{
		if(soundicon_tab[ix].name == c)
		{
			if(soundicon_tab[ix].length == 0)
			{
				if(LoadSoundFile(NULL,ix)!=0)
					return(-1);  // sound file is not available
			}
			return(ix);
		}
	}
	return(-1);
}


static int AnnouncePunctuation(Translator *tr, int c1, int *c2_ptr, char *output, int *bufix, int end_clause)
{//==========================================================================================================
	// announce punctuation names
	// c1:  the punctuation character
	// c2:  the following character

	const char *punctname;
	int found = 0;
	int soundicon;
	char buf[200];
	char buf2[80];

	int c2 = *c2_ptr;
	buf[0] = 0;

	if((soundicon = LookupSoundicon(c1)) >= 0)
	{
		// add an embedded command to play the soundicon
		sprintf(buf,"\001%dI ",soundicon);
		UngetC(c2);
		found = 1;
	}
	else
	if((punctname = LookupCharName(tr, c1, 0)) != NULL)
	{
		found = 1;
		if((*bufix==0) || (end_clause ==0) || (tr->langopts.param[LOPT_ANNOUNCE_PUNCT] & 2))
		{
			int punct_count=1;
			while((c2 == c1) && (c1 != '<'))  // don't eat extra '<', it can miss XML tags
			{
				punct_count++;
				c2 = GetC();
			}
			*c2_ptr = c2;
			if(end_clause)
			{
				UngetC(c2);
			}

			if(punct_count==1)
			{
				sprintf(buf," %s",punctname);   // we need the space before punctname, to ensure it doesn't merge with the previous word  (eg.  "2.-a")
			}
			else
			if(punct_count < 4)
			{
				buf[0] = 0;
				if(embedded_value[EMBED_S] < 300)
					sprintf(buf,"\001+10S");  // Speak punctuation name faster, unless we are already speaking fast.  It would upset Sonic SpeedUp

				while(punct_count-- > 0)
				{
					sprintf(buf2," %s",punctname);
					strcat(buf, buf2);
				}

				if(embedded_value[EMBED_S] < 300)
				{
					sprintf(buf2," \001-10S");
					strcat(buf, buf2);
				}
			}
			else
			{
				sprintf(buf," %s %d %s",
						punctname,punct_count,punctname);
			}
		}
		else
		{
			// end the clause now and pick up the punctuation next time
			UngetC(c2);
			ungot_char2 = c1;
			buf[0] = ' ';
			buf[1] = 0;
		}
	}

	if(found == 0)
		return(-1);

	int bufix1 = *bufix;
	int len = strlen(buf);
	strcpy(&output[*bufix],buf);
	*bufix += len;

	if(end_clause==0)
		return(-1);

	if(c1 == '-')
		return(CLAUSE_NONE);   // no pause

	int attributes = punct_attributes[lookupwchar(punct_chars,c1)];

	int short_pause = CLAUSE_SHORTFALL;
	if((attributes & CLAUSE_BITS_INTONATION) == 0x1000)
		short_pause = CLAUSE_SHORTCOMMA;

	if((bufix1 > 0) && !(tr->langopts.param[LOPT_ANNOUNCE_PUNCT] & 2))
	{
		if((attributes & ~0x8000) == CLAUSE_SEMICOLON)
			return(CLAUSE_SHORTFALL);
		return(short_pause);
	}

	if(attributes & CLAUSE_BIT_SENTENCE)
		return(attributes);
	
	return(short_pause);
}  //  end of AnnouncePunctuation


void SetVoiceStack(espeak_VOICE *v, const char *variant_name)
{//==========================================================
	SSML_STACK *sp = &ssml_stack[0];

	if(v == NULL)
	{
		memset(sp,0,sizeof(ssml_stack[0]));
		return;
	}
	if(v->languages != NULL)
		strcpy(sp->language,v->languages);
	if(v->name != NULL)
		strncpy0(sp->voice_name, v->name, sizeof(sp->voice_name));
	sp->voice_variant_number = v->variant;
	sp->voice_age = v->age;
	sp->voice_gender = v->gender;

	if(memcmp(variant_name, "!v", 2) == 0)
		variant_name += 3;// strip variant directory name, !v plus PATHSEP
	strncpy0(base_voice_variant_name, variant_name, sizeof(base_voice_variant_name));
	memcpy(&base_voice, &current_voice_selected, sizeof(base_voice));
}


static void RemoveChar(char *p)
{//=======================
// Replace a UTF-8 character by spaces
	int c;

	memset(p, ' ', utf8_in(&c, p));
}  // end of RemoveChar


int ReadClause(Translator *tr, FILE *f_in, char *buf, short *charix, int *charix_top, int n_buf, int *tone_type, char *voice_change)
{//=================================================================================================================================
/* Find the end of the current clause.
	Write the clause into  buf

	returns: clause type (bits 0-7: pause x10mS, bits 8-11 intonation type)

	Also checks for blank line (paragraph) as end-of-clause indicator.

	Does not end clause for:
		punctuation immediately followed by alphanumeric  eg.  1.23  !Speak  :path
		repeated punctuation, eg.   ...   !!!
*/
	int c1=' ';  // current character
	int c2;  // next character
	int cprev=' ';  // previous character
	int cprev2=' ';
	int c_next;
	int ix = 0;
	int linelength = 0;
	int phoneme_mode = 0;
	int terminator;
	int punct;
	int any_alnum = 0;
	int is_end_clause;
	int stressed_word = 0;
	int end_clause_after_tag = 0;
	int end_clause_index = 0;

#define N_XML_BUF2   20
	static char ungot_string[N_XML_BUF2+4];
	static int ungot_string_ix = -1;

	if(clear_skipping_text)
	{
		skipping_text = 0;
		clear_skipping_text = 0;
	}

	tr->phonemes_repeat_count = 0;
	tr->clause_upper_count = 0;
	tr->clause_lower_count = 0;
	end_of_input = 0;
	*tone_type = 0;
	*voice_change = 0;

f_input = f_in;  // for GetC etc

	if(ungot_word != NULL)
	{
		strcpy(buf,ungot_word);
		ix += strlen(ungot_word);
		ungot_word = NULL;
	}

	if(ungot_char2 != 0)
	{
		c2 = ungot_char2;
	}
	else
	{
		c2 = GetC();
	}

	while(!Eof() || (ungot_char != 0) || (ungot_char2 != 0) || (ungot_string_ix >= 0))
	{
		if(!iswalnum(c1))
		{
			if((end_character_position > 0) && (count_characters > end_character_position))
			{
				end_of_input = 1;
				return(CLAUSE_EOF);
			}

			if((skip_characters > 0) && (count_characters > skip_characters))
			{
				// reached the specified start position
				// don't break a word
				clear_skipping_text = 1;
				skip_characters = 0;
				UngetC(c2);
				return(CLAUSE_NONE);
			}
		}

		cprev2 = cprev;
		cprev = c1;
		c1 = c2;

		if(ungot_string_ix >= 0)
		{
			if(ungot_string[ungot_string_ix] == 0)
				ungot_string_ix = -1;
		}

		if((ungot_string_ix == 0) && (ungot_char2 == 0))
		{
			c1 = ungot_string[ungot_string_ix++];
		}
		if(ungot_string_ix >= 0)
		{
			c2 = ungot_string[ungot_string_ix++];
		}
		else
		{
			c2 = GetC();

			if(Eof())
			{
				c2 = ' ';
			}
		}
		ungot_char2 = 0;

		if(ignore_text)
			continue;

		if((c2=='\n') && (option_linelength == -1))
		{
			// single-line mode, return immediately on NL
			if((punct = lookupwchar(punct_chars,c1)) == 0)
			{
				charix[ix] = count_characters - clause_start_char;
				*charix_top = ix;
				ix += utf8_out(c1,&buf[ix]);
				terminator = CLAUSE_PERIOD;  // line doesn't end in punctuation, assume period
			}
			else
			{
				terminator = punct_attributes[punct];
			}
			buf[ix] = ' ';
			buf[ix+1] = 0;
			return(terminator);
		}

		if((c1 == CTRL_EMBEDDED) || (c1 == ctrl_embedded))
		{
			// an embedded command. If it's a voice change, end the clause
			if(c2 == 'V')
			{
				buf[ix++] = 0;      // end the clause at this point
				while(!iswspace(c1 = GetC()) && !Eof() && (ix < (n_buf-1)))
					buf[ix++] = c1;  // add voice name to end of buffer, after the text
				buf[ix++] = 0;
				return(CLAUSE_VOICE);
			}
			else
			if(c2 == 'B')
			{
				// set the punctuation option from an embedded command
				//  B0     B1     B<punct list><space>
				strcpy(&buf[ix],"   ");
				ix += 3;

				if((c2 = GetC()) == '0')
					option_punctuation = 0;
				else
				{
					option_punctuation = 1;
					option_punctlist[0] = 0;
					if(c2 != '1')
					{
						// a list of punctuation characters to be spoken, terminated by space
						int j = 0;
						while(!iswspace(c2) && !Eof())
						{
							option_punctlist[j++] = c2;
							c2 = GetC();
							buf[ix++] = ' ';
						}
						option_punctlist[j] = 0;  // terminate punctuation list
						option_punctuation = 2;
					}
				}
				c2 = GetC();
				continue;
			}
		}

		linelength++;

		if(iswalnum(c1))
			any_alnum = 1;
		else
		{
			if(stressed_word)
			{
				stressed_word = 0;
				c1 = CHAR_EMPHASIS;   // indicate this word is stressed
				UngetC(c2);
				c2 = ' ';
			}

			if(lookupwchar(chars_ignore,c1))
			{
				// ignore this character (eg. zero-width-non-joiner U+200C)
				continue;
			}

			if(c1 == 0xf0b)
				c1 = ' ';    // Tibet inter-syllabic mark, ?? replace by space ??

			if(iswspace(c1))
			{
				if(tr->translator_name == 0x6a626f)
				{
					// language jbo : lojban
					// treat "i" or ".i" as end-of-sentence
					char *p_word = &buf[ix-1];
					if(p_word[0] == 'i')
					{
						if(p_word[-1] == '.')
							p_word--;
						if(p_word[-1] == ' ')
						{
							ungot_word = "i ";
							UngetC(c2);
							p_word[0] = 0;
							return(CLAUSE_PERIOD);
						}
					}
				}
			}

			if(c1 == 0xd4d)
			{
				// Malayalam virama, check if next character is Zero-width-joiner
				if(c2 == 0x200d)
				{
					c1 = 0xd4e;   // use this unofficial code for chillu-virama
				}
			}
		}

		if(iswupper(c1))
		{
			tr->clause_upper_count++;
			if((option_capitals == 2) && !iswupper(cprev))
			{
				char text_buf[40];
				char text_buf2[30];
				if(LookupSpecial(tr, "_cap", text_buf2) != NULL)
				{
					sprintf(text_buf,"%s",text_buf2);
					int j = strlen(text_buf);
					if((ix + j) < n_buf)
					{
						strcpy(&buf[ix],text_buf);
						ix += j;
					}
				}
			}
		}
		else
		if(iswalpha(c1))
			tr->clause_lower_count++;

		if(option_phoneme_input)
		{
			if(phoneme_mode > 0)
				phoneme_mode--;
			else
			if((c1 == '[') && (c2 == '['))
				phoneme_mode = -1;     // input is phoneme mnemonics, so don't look for punctuation
			else
			if((c1 == ']') && (c2 == ']'))
				phoneme_mode = 2;      // set phoneme_mode to zero after the next two characters
		}

		if(c1 == '\n')
		{
			int parag = 0;

			// count consecutive newlines, ignoring other spaces
			while(!Eof() && iswspace(c2))
			{
				if(c2 == '\n')
					parag++;
				c2 = GetC();
			}
			if(parag > 0)
			{
				// 2nd newline, assume paragraph
				UngetC(c2);

				if(end_clause_after_tag)
				{
					RemoveChar(&buf[end_clause_index]);  // delete clause-end punctiation
				}
				buf[ix] = ' ';
				buf[ix+1] = 0;
				if(parag > 3)
					parag = 3;
if(option_ssml) parag=1;
				return((CLAUSE_PARAGRAPH-30) + 30*parag);  // several blank lines, longer pause
			}

			if(linelength <= option_linelength)
			{
				// treat lines shorter than a specified length as end-of-clause
				UngetC(c2);
				buf[ix] = ' ';
				buf[ix+1] = 0;
				return(CLAUSE_COLON);
			}

			linelength = 0;
		}

		int announced_punctuation = 0;

		if(phoneme_mode==0)
		{
			is_end_clause = 0;

			if(end_clause_after_tag)
			{
				// Because of an xml tag, we are waiting for the
				// next non-blank character to decide whether to end the clause
				// i.e. is dot followed by an upper-case letter?
				
				if(!iswspace(c1))
				{
					if(!IsAlpha(c1) || !iswlower(c1))
					{
						UngetC(c2);
						ungot_char2 = c1;
						buf[end_clause_index] = ' ';  // delete the end-clause punctuation
						buf[end_clause_index+1] = 0;
						return(end_clause_after_tag);
					}
					end_clause_after_tag = 0;
				}
			}

			if((c1 == '.') && (c2 == '.'))
			{
				while((c_next = GetC()) == '.')
				{
					// 3 or more dots, replace by elipsis
					c1 = 0x2026;
					c2 = ' ';
				}
				if(c1 == 0x2026)
					c2 = c_next;
				else
					UngetC(c_next);
			}

			int punct_data = 0;
			if((punct = lookupwchar(punct_chars,c1)) != 0)
			{
				punct_data = punct_attributes[punct];
	
				if(punct_data & PUNCT_IN_WORD)
				{
					// Armenian punctuation inside a word
					stressed_word = 1;
					*tone_type = punct_data >> 12 & 0xf;   // override the end-of-sentence type
					continue;
				}

				if((iswspace(c2) || (punct_data & 0x8000) || IsBracket(c2) || (c2=='?') || Eof() || (c2 == ctrl_embedded)))    // don't check for '-' because it prevents recognizing ':-)'
				{
					// note: (c2='?') is for when a smart-quote has been replaced by '?'
					is_end_clause = 1;
				}
			}

			// don't announce punctuation for the alternative text inside inside <audio> ... </audio>
			if(c1 == 0xe000+'<')  c1 = '<';
			if(option_punctuation && iswpunct(c1) && (audio_text == 0))
			{
				// option is set to explicitly speak punctuation characters
				// if a list of allowed punctuation has been set up, check whether the character is in it
				if((option_punctuation == 1) || (wcschr(option_punctlist,c1) != NULL))
				{
					tr->phonemes_repeat_count = 0;
					if((terminator = AnnouncePunctuation(tr, c1, &c2, buf, &ix, is_end_clause)) >= 0)
						return(terminator);
					announced_punctuation = c1;
				}
			}
	
			if((punct_data & PUNCT_SAY_NAME) && (announced_punctuation == 0))
			{
				// used for elipsis (and 3 dots) if a pronunciation for elipsis is given in *_list
				char *p2 = &buf[ix];
				sprintf(p2,"%s",LookupCharName(tr, c1, 1));
				if(p2[0] != 0)
				{
					ix += strlen(p2);
					announced_punctuation = c1;
					punct_data = punct_data & ~CLAUSE_BITS_INTONATION;  // change intonation type to 0 (full-stop)
				}
			}

			if(is_end_clause)
			{
				int nl_count = 0;
				c_next = c2;

				if(iswspace(c_next))
				{
					while(!Eof() && iswspace(c_next))
					{
						if(c_next == '\n')
							nl_count++;
						c_next = GetC();   // skip past space(s)
					}
				}

				if((c1 == '.') && (nl_count < 2))
				{
					punct_data |= CLAUSE_DOT;
				}

				if(nl_count==0)
				{
					if((c1 == ',') && (cprev == '.') && (tr->translator_name == L('h','u')) && iswdigit(cprev2) && (iswdigit(c_next) || (iswlower(c_next))))
					{
						// lang=hu, fix for ordinal numbers, eg:  "december 2., szerda", ignore ',' after ordinal number
						c1 = CHAR_COMMA_BREAK;
						is_end_clause = 0;
					}

					if(c1 == '.')
					{
						if((tr->langopts.numbers & NUM_ORDINAL_DOT) && 
							(iswdigit(cprev) || (IsRomanU(cprev) && (IsRomanU(cprev2) || iswspace(cprev2)))))  // lang=hu
						{
							// dot after a number indicates an ordinal number
							if(!iswdigit(cprev))
							{
								is_end_clause = 0;  // Roman number followed by dot
							}
							else
							{
								if (iswlower(c_next) || (c_next=='-'))     // hyphen is needed for lang-hu (eg. 2.-kal)
									is_end_clause = 0;      // only if followed by lower-case, (or if there is a XML tag)
							}
						}
						else
						if(c_next == '\'')
						{
							is_end_clause = 0;    // eg. u.s.a.'s 
						}
						if(iswlower(c_next))
						{
							// next word has no capital letter, this dot is probably from an abbreviation
							is_end_clause = 0;
						}
						if(any_alnum==0)
						{
							// no letters or digits yet, so probably not a sentence terminator
							// Here, dot is followed by space or bracket
							c1 = ' ';
							is_end_clause = 0;
						}
					}
					else
					{
						if(any_alnum==0)
						{
							// no letters or digits yet, so probably not a sentence terminator
							is_end_clause = 0;
						}
					}

					if(is_end_clause && (c1 == '.') && (c_next == '<') && option_ssml)
					{
						// wait until after the end of the xml tag, then look for upper-case letter
						is_end_clause = 0;
						end_clause_index = ix;
						end_clause_after_tag = punct_data;
					}
				}

				if(is_end_clause)
				{
					UngetC(c_next);
					buf[ix] = ' ';
					buf[ix+1] = 0;

					if(iswdigit(cprev) && !IsAlpha(c_next))   // ????
					{
						punct_data &= ~CLAUSE_DOT;
					}
					if(nl_count > 1)
					{
						if((punct_data == CLAUSE_QUESTION) || (punct_data == CLAUSE_EXCLAMATION))
							return(punct_data + 35);   // with a longer pause
						return(CLAUSE_PARAGRAPH);
					}
					return(punct_data);   // only recognise punctuation if followed by a blank or bracket/quote
				}
				else
				{
					if(!Eof())
					{
						if(iswspace(c2))
							UngetC(c_next);
					}
				}
			}
		}

		if(speech_parameters[espeakSILENCE]==1)
			continue;

		if(c1 == announced_punctuation)
		{
			// This character has already been announced, so delete it so that it isn't spoken a second time.
			// Unless it's a hyphen or apostrophe (which is used by TranslateClause() )
			if(IsBracket(c1))
			{
				c1 = 0xe000 + '(';   // Unicode private useage area.  So TranslateRules() knows the bracket name has been spoken
			}
			else
			if(c1 != '-')
			{
				c1 = ' ';
			}
		}

		int j = ix+1;

		if(c1 == 0xe000 + '<') c1 = '<';

		ix += utf8_out(c1,&buf[ix]);
		if(!iswspace(c1) && !IsBracket(c1))
		{
			charix[ix] = count_characters - clause_start_char;
			while(j < ix)
				charix[j++] = -1;   // subsequent bytes of a multibyte character
		}
		*charix_top = ix;

		if(((ix > (n_buf-75)) && !IsAlpha(c1) && !iswdigit(c1))  ||  (ix >= (n_buf-4)))
		{
			// clause too long, getting near end of buffer, so break here
			// try to break at a word boundary (unless we actually reach the end of buffer).
			// (n_buf-4) is to allow for 3 bytes of multibyte character plus terminator.
			buf[ix] = ' ';
			buf[ix+1] = 0;
			UngetC(c2);
			return(CLAUSE_NONE);
		}
	}

	if(stressed_word)
	{
		ix += utf8_out(CHAR_EMPHASIS, &buf[ix]);
	}
	if(end_clause_after_tag)
	{
		RemoveChar(&buf[end_clause_index]);  // delete clause-end punctiation
	}
	buf[ix] = ' ';
	buf[ix+1] = 0;
	return(CLAUSE_EOF);   //  end of file
}  //  end of ReadClause


void InitNamedata(void)
{//====================
	namedata_ix = 0;
	if(namedata != NULL)
	{
		free(namedata);
		namedata = NULL;
		n_namedata = 0;
	}
}


void InitText2(void)
{//=================
	ungot_char = 0;
	ungot_char2 = 0;

	n_ssml_stack =1;
	n_param_stack = 1;
	ssml_stack[0].tag_type = 0;

	for(int param=0; param<N_SPEECH_PARAM; param++)
		speech_parameters[param] = param_stack[0].parameter[param];   // set all speech parameters to defaults

	option_punctuation = speech_parameters[espeakPUNCTUATION];
	option_capitals = speech_parameters[espeakCAPITALS];

	current_voice_id[0] = 0;

	ignore_text = 0;
	audio_text = 0;
	clear_skipping_text = 0;
	count_characters = -1;

	xmlbase = NULL;
}
