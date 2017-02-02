/*
  This file is a part of unixcw project.

  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef H_LIBCW2
#define H_LIBCW2





#include <sys/time.h>  /* struct timeval */
#include <stdbool.h>




/*
  'Mark' means either dot or dash.
  'Symbol' means either Mark or Space.
*/




enum cw_return_values {
	CW_FAILURE = false,
	CW_SUCCESS = true
};

/* Supported audio systems. */
enum cw_audio_systems {
	CW_AUDIO_NONE = 0,  /* initial value; this is not the same as CW_AUDIO_NULL */
	CW_AUDIO_NULL,      /* empty audio output (no sound, just timing); this is not the same as CW_AUDIO_NONE */
	CW_AUDIO_CONSOLE,   /* console buzzer */
	CW_AUDIO_OSS,
	CW_AUDIO_ALSA,
	CW_AUDIO_PA,        /* PulseAudio */
	CW_AUDIO_SOUNDCARD  /* OSS, ALSA or PulseAudio (PA) */
};

enum {
	CW_KEY_STATE_OPEN = 0,  /* key is open, no electrical contact in key, no sound */
	CW_KEY_STATE_CLOSED     /* key is closed, there is an electrical contact in key, a sound is generated */
};





#define CW_AUDIO_CHANNELS  1  /* Sound in mono. */

/* Default outputs for audio systems. Used by libcw unless
   client code decides otherwise. */
#define CW_DEFAULT_NULL_DEVICE      ""
#define CW_DEFAULT_CONSOLE_DEVICE   "/dev/console"
#define CW_DEFAULT_OSS_DEVICE       "/dev/audio"
#define CW_DEFAULT_ALSA_DEVICE      "default"
#define CW_DEFAULT_PA_DEVICE        "( default )"


/* Limits on values of generator's and receiver's main parameters. */
#define CW_SPEED_MIN             4   /* Lowest speed supported by libcw, [wpm]. */
#define CW_SPEED_MAX            60   /* Highest speed supported by libcw, [wpm]. */
#define CW_SPEED_INITIAL        12   /* Initial generator's speed, [wpm]. */
#define CW_FREQUENCY_MIN         0   /* Lowest frequency supported, [Hz]. */
#define CW_FREQUENCY_MAX      4000   /* Highest frequency supported, [Hz]. */
#define CW_FREQUENCY_INITIAL   800   /* Initial frequency, [Hz]. */
#define CW_VOLUME_MIN            0   /* Lowest volume supported, [%] (0 == silent). */
#define CW_VOLUME_MAX          100   /* Highest volume supported, [%]. */
#define CW_VOLUME_INITIAL       70   /* Initial volume, [%]. */
#define CW_GAP_MIN               0   /* Lowest extra gap supported. */
#define CW_GAP_MAX              60   /* Highest extra gap supported. */
#define CW_GAP_INITIAL           0   /* Initial gap. */
#define CW_WEIGHTING_MIN        20   /* Lowest weighting supported. */
#define CW_WEIGHTING_MAX        80   /* Highest weighting supported. */
#define CW_WEIGHTING_INITIAL    50   /* Initial weighting. */
#define CW_TOLERANCE_MIN         0   /* Lowest receive tolerance supported. */
#define CW_TOLERANCE_MAX        90   /* Highest receive tolerance supported. */
#define CW_TOLERANCE_INITIAL    50   /* Initial tolerance. */





/* Representation characters for Dot and Dash.  Only these two
   characters are permitted in Morse representation strings. */
enum {
	CW_DOT_REPRESENTATION  = '.',
	CW_DASH_REPRESENTATION = '-'
};





/* Values deciding the shape of slopes of tones generated by
   generator.

   If a generated tone is declared to have one or two slopes,
   generator has to know what shape of the slope(s) should be. Since
   the shape of tones is common for all tones generated by generator,
   shape of tone is a property of generator rather than of tone.

   These names are to be used as values of argument 'slope_shape' of
   cw_gen_set_tone_slope() function.  */
enum {
	CW_TONE_SLOPE_SHAPE_LINEAR,          /* Ramp/linearly raising slope. */
	CW_TONE_SLOPE_SHAPE_RAISED_COSINE,   /* Shape of cosine function in range <-pi - zero).  */
	CW_TONE_SLOPE_SHAPE_SINE,            /* Shape of sine function in range <zero - pi/2). */
	CW_TONE_SLOPE_SHAPE_RECTANGULAR      /* Slope changes from zero for sample n, to full amplitude of tone in sample n+1. */
};





/* (Forward) declarations of data types. */
struct cw_gen_struct;
typedef struct cw_gen_struct cw_gen_t;

struct cw_rec_struct;
typedef struct cw_rec_struct cw_rec_t;

struct cw_key_struct;
typedef struct cw_key_struct cw_key_t;

#ifdef WITH_EXPERIMENTAL_RECEIVER
typedef bool (* cw_rec_push_callback_t)(int *, void *);
#endif

typedef void (* cw_key_callback_t)(struct timeval *timestamp, int key_state, void* arg);


typedef void (* cw_queue_low_callback_t)(void*);


/* generator module: basic functions. */
cw_gen_t * cw_gen_new(int audio_system, char const * device);
void       cw_gen_delete(cw_gen_t ** gen);
int        cw_gen_start(cw_gen_t * gen);
int        cw_gen_stop(cw_gen_t * gen);


/* generator module: getters of generator's basic parameters. */
int cw_gen_get_speed(cw_gen_t const * gen);
int cw_gen_get_frequency(cw_gen_t const * gen);
int cw_gen_get_volume(cw_gen_t const * gen);
int cw_gen_get_gap(cw_gen_t const * gen);
int cw_gen_get_weighting(cw_gen_t const * gen);


/* generator module: setters of generator's basic parameters. */
int cw_gen_set_speed(cw_gen_t *gen, int new_value);
int cw_gen_set_frequency(cw_gen_t *gen, int new_value);
int cw_gen_set_volume(cw_gen_t *gen, int new_value);
int cw_gen_set_gap(cw_gen_t *gen, int new_value);
int cw_gen_set_weighting(cw_gen_t *gen, int new_value);


/* generator module: queue functions. */
int    cw_gen_enqueue_character(cw_gen_t * gen, char c);
int    cw_gen_enqueue_character_partial(cw_gen_t * gen, char c);
int    cw_gen_enqueue_string(cw_gen_t * gen, char const * string);
int    cw_gen_wait_for_queue_level(cw_gen_t * gen, size_t level);
int    cw_gen_wait_for_tone(cw_gen_t * gen);
void   cw_gen_flush_queue(cw_gen_t * gen);
size_t cw_gen_get_queue_length(cw_gen_t const * gen);
int    cw_gen_register_low_level_callback(cw_gen_t * gen, cw_queue_low_callback_t callback_func, void * callback_arg, size_t level);


/* generator module: misc functions. */
int          cw_gen_set_tone_slope(cw_gen_t * gen, int slope_shape, int slope_usecs);
char const * cw_gen_get_audio_device(cw_gen_t const * gen);
int          cw_gen_get_audio_system(cw_gen_t const * gen);
bool         cw_gen_is_queue_full(cw_gen_t const * gen);


/* receiver module: receiver's main functions. */
cw_rec_t *cw_rec_new(void);
void      cw_rec_delete(cw_rec_t **rec);
int       cw_rec_mark_begin(cw_rec_t *rec, const struct timeval *timestamp);
int       cw_rec_mark_end(cw_rec_t *rec, const struct timeval *timestamp);
int       cw_rec_add_mark(cw_rec_t *rec, const struct timeval *timestamp, char mark);

void cw_rec_reset_state(cw_rec_t * rec);
void cw_rec_reset_statistics(cw_rec_t * rec);

#ifdef WITH_EXPERIMENTAL_RECEIVER
void      cw_rec_register_push_callback(cw_rec_t *rec, cw_rec_push_callback_t *callback);
#endif


/* receiver module: receiver's helper functions. */
int  cw_rec_poll_representation(cw_rec_t *rec, const struct timeval *timestamp, char *representation, bool *is_end_of_word, bool *is_error);
int  cw_rec_poll_character(cw_rec_t *rec, const struct timeval *timestamp, char *c, bool *is_end_of_word, bool *is_error);
bool cw_rec_poll_is_pending_inter_word_space(cw_rec_t const * rec);



/* Receiver module: getters of receiver's essential parameters. */
float cw_rec_get_speed(const cw_rec_t * rec);
int   cw_rec_get_tolerance(const cw_rec_t * rec);
int   cw_rec_get_noise_spike_threshold(const cw_rec_t * rec);
bool  cw_rec_get_adaptive_mode(const cw_rec_t * rec);


/* receiver module: setters of receiver's essential parameters. */
int  cw_rec_set_speed(cw_rec_t *rec, int new_value);
int  cw_rec_set_tolerance(cw_rec_t *rec, int new_value);
int  cw_rec_set_noise_spike_threshold(cw_rec_t *rec, int new_value);
void cw_rec_enable_adaptive_mode(cw_rec_t *rec);
void cw_rec_disable_adaptive_mode(cw_rec_t *rec);


/* key module: main functions. */
cw_key_t *cw_key_new(void);
void      cw_key_delete(cw_key_t **key);


/* key module. */
void cw_key_ik_enable_curtis_mode_b(cw_key_t * key);
void cw_key_ik_disable_curtis_mode_b(cw_key_t * key);
bool cw_key_ik_get_curtis_mode_b(const cw_key_t * key);
int  cw_key_ik_notify_paddle_event(cw_key_t *key, int dot_paddle_state, int dash_paddle_state);
int  cw_key_ik_notify_dot_paddle_event(cw_key_t *key, int dot_paddle_state);
int  cw_key_ik_notify_dash_paddle_event(cw_key_t *key, int dash_paddle_state);

int  cw_key_ik_wait_for_element(cw_key_t *key);
int  cw_key_ik_wait_for_keyer(cw_key_t *key);
void cw_key_ik_get_paddles(cw_key_t * key, /* out */ int * dot_paddle_state, /* out */ int * dash_paddle_state);

int  cw_key_sk_notify_event(cw_key_t * key, int key_value);
int  cw_key_sk_get_value(const cw_key_t * key);
bool cw_key_sk_is_busy(const cw_key_t * key);

void cw_key_register_keying_callback(cw_key_t *key, cw_key_callback_t callback_func, void *callback_arg);
void cw_key_register_generator(cw_key_t *key, cw_gen_t *gen);
void cw_key_register_receiver(cw_key_t *key, cw_rec_t *rec);


/* General functions: audio systems. */
extern bool cw_is_null_possible(const char *device);
extern bool cw_is_console_possible(const char *device);
extern bool cw_is_oss_possible(const char *device);
extern bool cw_is_alsa_possible(const char *device);
extern bool cw_is_pa_possible(const char *device);
extern const char *cw_get_audio_system_label(int audio_system);


/* General functions: library metadata. */
extern void cw_version(int *current, int *revision, int *age);
extern void cw_license(void);


/* General functions: limits. */
extern void cw_get_speed_limits(int *min_speed, int *max_speed);
extern void cw_get_frequency_limits(int *min_frequency, int *max_frequency);
extern void cw_get_volume_limits(int *min_volume, int *max_volume);
extern void cw_get_gap_limits(int *min_gap, int *max_gap);
extern void cw_get_tolerance_limits(int *min_tolerance, int *max_tolerance);
extern void cw_get_weighting_limits(int *min_weighting, int *max_weighting);


#if 0 /* FIXME: what to do with this function? */
/* General functions: other. */
extern void cw_reset_send_receive_parameters(void);
#endif


/* data module: phonetic alphabet. */
extern int cw_get_maximum_phonetic_length(void);
extern int cw_lookup_phonetic(char c, char *phonetic);


/* data module: procedural characters (procedural signals). */
extern int  cw_get_procedural_character_count(void);
extern void cw_list_procedural_characters(char *list);
extern int  cw_get_maximum_procedural_expansion_length(void);
extern int  cw_lookup_procedural_character(char c, char *representation, int *is_usually_expanded);


/* data module: regular characters. */
extern int   cw_get_character_count(void);
extern void  cw_list_characters(char *list);
extern int   cw_get_maximum_representation_length(void);
extern char *cw_character_to_representation(int c);
extern int   cw_representation_to_character(const char *representation);


/* data module: validators. */
extern bool cw_character_is_valid(char c);
extern bool cw_string_is_valid(const char *string);
extern bool cw_representation_is_valid(const char *representation);





#endif /* #ifndef H_LIBCW2 */
