/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "config.h"




#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */


#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>



#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_tq.h"
#include "libcw_utils.h"
#include "libcw_gen.h"




#define MSG_PREFIX "libcw/legacy: "



extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;



static cw_test_stats_t cw_stats_indep   = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };


static void cw_test_setup(void);
static int  cw_test_modules_with_sound_systems(const char * modules, const char * sound_systems);
static int  cw_test_modules_with_one_sound_system(const char * modules, int audio_system, cw_test_t * tests);
static void cw_test_print_stats(void);




static void cw_test_helper_tq_callback(void *data);





/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
static int test_audio_system = CW_AUDIO_NONE;





/* Tone queue module. */
static void test_cw_wait_for_tone(cw_test_t * tests);
static void test_cw_wait_for_tone_queue(cw_test_t * tests);
static void test_cw_queue_tone(cw_test_t * tests);

static void test_empty_tone_queue(cw_test_t * tests);
static void test_full_tone_queue(cw_test_t * tests);

static void test_tone_queue_callback(cw_test_t * tests);

/* Generator module. */
static void test_volume_functions(cw_test_t * tests);
static void test_send_primitives(cw_test_t * tests);
static void test_send_character_and_string(cw_test_t * tests);
static void test_representations(cw_test_t * tests);
/* Morse key module. */
static void test_keyer(cw_test_t * tests);
static void test_straight_key(cw_test_t * tests);
/* Other functions. */
static void test_parameter_ranges(cw_test_t * tests);
//static void test_cw_gen_forever_public(cw_test_t * tests);

// static void cw_test_delayed_release(cw_test_t * tests);





/*---------------------------------------------------------------------*/
/*  Unit tests                                                         */
/*---------------------------------------------------------------------*/





/**
   Notice that getters of parameter limits are tested in test_cw_get_x_limits()

   tests::cw_set_send_speed()
   tests::cw_get_send_speed()
   tests::cw_set_receive_speed()
   tests::cw_get_receive_speed()
   tests::cw_set_frequency()
   tests::cw_get_frequency()
   tests::cw_set_volume()
   tests::cw_get_volume()
   tests::cw_set_gap()
   tests::cw_get_gap()
   tests::cw_set_tolerance()
   tests::cw_get_tolerance()
   tests::cw_set_weighting()
   tests::cw_get_weighting()
*/
void test_parameter_ranges(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	int txdot_usecs, txdash_usecs, end_of_element_usecs, end_of_character_usecs,
		end_of_word_usecs, additional_usecs, adjustment_usecs;

	/* Print default low level timing values. */
	cw_reset_send_receive_parameters();
	cw_get_send_parameters(&txdot_usecs, &txdash_usecs,
			       &end_of_element_usecs, &end_of_character_usecs,
			       &end_of_word_usecs, &additional_usecs,
			       &adjustment_usecs);
	printf(MSG_PREFIX "cw_get_send_parameters():\n"
	       MSG_PREFIX "    %d, %d, %d, %d, %d, %d, %d\n",
	       txdot_usecs, txdash_usecs, end_of_element_usecs,
	       end_of_character_usecs,end_of_word_usecs, additional_usecs,
	       adjustment_usecs);


	/* Test setting and getting of some basic parameters. */

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(int new_value);
		int (* get_value)(void);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_set_send_speed,     cw_get_send_speed,     10000,  -10000,  "send_speed"    },
		{ cw_get_speed_limits,      cw_set_receive_speed,  cw_get_receive_speed,  10000,  -10000,  "receive_speed" },
		{ cw_get_frequency_limits,  cw_set_frequency,      cw_get_frequency,      10000,  -10000,  "frequency"     },
		{ cw_get_volume_limits,     cw_set_volume,         cw_get_volume,         10000,  -10000,  "volume"        },
		{ cw_get_gap_limits,        cw_set_gap,            cw_get_gap,            10000,  -10000,  "gap"           },
		{ cw_get_tolerance_limits,  cw_set_tolerance,      cw_get_tolerance,      10000,  -10000,  "tolerance"     },
		{ cw_get_weighting_limits,  cw_set_weighting,      cw_get_weighting,      10000,  -10000,  "weighting"     },
		{ NULL,                     NULL,                  NULL,                      0,       0,  NULL            }
	};


	for (int i = 0; test_data[i].get_limits; i++) {
		int cwret;

		/* Get limits of values to be tested. */
		/* Notice that getters of parameter limits are tested
		   in test_cw_get_x_limits(). */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);


		/* Test out-of-range value lower than minimum. */
		errno = 0;
		cwret = test_data[i].set_new_value(test_data[i].min - 1);
		tests->expect_eq_int(tests, EINVAL, errno, "cw_set_%s(min - 1):", test_data[i].name);
		tests->expect_eq_int(tests, CW_FAILURE, cwret, "cw_set_%s(min - 1):", test_data[i].name);


		/* Test out-of-range value higher than maximum. */
		errno = 0;
		cwret = test_data[i].set_new_value(test_data[i].max + 1);
		tests->expect_eq_int(tests, EINVAL, errno, "cw_set_%s(max + 1):", test_data[i].name);
		tests->expect_eq_int(tests, CW_FAILURE, cwret, "cw_set_%s(max + 1):", test_data[i].name);


		/*
		  Test setting and reading back of in-range values.
		  There will be many, many iterations, so use ::expect_eq_int_errors_only().
		*/
		bool success = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			const int value_set = j;
			test_data[i].set_new_value(value_set);
			const int value_readback = test_data[i].get_value();

			success = tests->expect_eq_int_errors_only(tests, value_set, value_readback, "cw_get/set_%s(%d):", test_data[i].name, value_set);
			if (!success) {
				break;
			}
		}
		tests->expect_eq_int(tests, true, success, "cw_get/set_%s():", test_data[i].name);
	}


	tests->print_test_footer(tests, __func__);

	return;
}





/**
   Fill a queue and then wait for each tone separately - repeat until
   all tones are dequeued.

   tests::cw_queue_tone()
   tests::cw_get_tone_queue_length()
   tests::cw_wait_for_tone()
*/
void test_cw_wait_for_tone(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	int cwret;

	const int n_tones_to_add = 6;     /* This is a simple test, so only a handful of tones. */
	const int tone_duration = 100000;

	/* Test setup. */
	{
		cw_set_volume(70);

		int freq_min, freq_max;
		cw_get_frequency_limits(&freq_min, &freq_max);
		const int delta_freq = ((freq_max - freq_min) / (n_tones_to_add - 1));      /* Delta of frequency in loops. */

		/* Test 1: enqueue n_tones_to_add tones, and wait for each of
		   them separately. Control length of tone queue in the
		   process. */

		/* Enqueue first tone. Don't check queue length yet.

		   The first tone is being dequeued right after enqueueing, so
		   checking the queue length would yield incorrect result.
		   Instead, enqueue the first tone, and during the process of
		   dequeueing it, enqueue rest of the tones in the loop,
		   together with checking length of the tone queue. */
		int freq = freq_min;

		cwret = cw_queue_tone(tone_duration, freq);
		tests->expect_eq_int(tests, CW_SUCCESS, cwret, "setup: cw_queue_tone()");


		/* This is to make sure that rest of tones is enqueued when
		   the first tone is in process of being dequeued (because we
		   wait only a fraction of duration). */
		usleep(tone_duration / 4);

		/* Enqueue rest of n_tones_to_add tones. It is now safe to check length of
		   tone queue before and after queueing each tone: length of
		   the tone queue should increase (there won't be any decrease
		   due to dequeueing of first tone). */
		for (int i = 1; i < n_tones_to_add; i++) {

			int got_tq_len = 0;       /* Measured length of tone queue. */
			int expected_tq_len = 0;  /* Expected length of tone queue. */

			/* Monitor length of a queue as it is filled - before
			   adding a new tone. */
			got_tq_len = cw_get_tone_queue_length();
			expected_tq_len = (i - 1);
			tests->expect_eq_int(tests, expected_tq_len, got_tq_len, "setup: cw_get_tone_queue_length(): before adding tone (#%02d):", i);


			/* Add a tone to queue. All frequencies should be
			   within allowed range, so there should be no
			   error. */
			freq = freq_min + i * delta_freq;
			cwret = cw_queue_tone(tone_duration, freq);
			tests->expect_eq_int(tests, CW_SUCCESS, cwret, "setup: cw_queue_tone() #%02d", i);


			/* Monitor length of a queue as it is filled - after
			   adding a new tone. */
			got_tq_len = cw_get_tone_queue_length();
			expected_tq_len = (i - 1) + 1;
			tests->expect_eq_int(tests, expected_tq_len, got_tq_len, "setup: cw_get_tone_queue_length(): after adding tone (#%02d):", i);
		}
	}

	/* Test. */
	{
		/* Above we have queued n_tones_to_add tones. libcw
		   starts dequeueing first of them before the last one
		   is enqueued. This is why below we should only check
		   for n_tones_to_add-1 of them. Additionally, let's
		   wait a moment till dequeueing of the first tone is
		   without a question in progress. */

		usleep(tone_duration / 4);

		/* And this is the proper test - waiting for dequeueing tones. */
		for (int i = 1; i < n_tones_to_add; i++) {

			int got_tq_len = 0;       /* Measured length of tone queue. */
			int expected_tq_len = 0;  /* Expected length of tone queue. */

			/* Monitor length of a queue as it is emptied - before dequeueing. */
			got_tq_len = cw_get_tone_queue_length();
			expected_tq_len = n_tones_to_add - i;
			tests->expect_eq_int(tests, expected_tq_len, got_tq_len, "test: cw_get_tone_queue_length(): before dequeueing (#%02d):", i);

			/* Wait for each of n_tones_to_add tones to be dequeued. */
			cwret = cw_wait_for_tone();
			tests->expect_eq_int(tests, CW_SUCCESS, cwret, "test: cw_wait_for_tone():");

			/* Monitor length of a queue as it is emptied - after dequeueing single tone. */
			got_tq_len = cw_get_tone_queue_length();
			expected_tq_len = n_tones_to_add - i - 1;
			tests->expect_eq_int(tests, expected_tq_len, got_tq_len, "test: cw_get_tone_queue_length(): after dequeueing (#%02d):", i);
		}
	}

	/* Test tear-down. */
	{
	}

	tests->print_test_footer(tests, __func__);

	return;
}



/**
   Fill a queue, don't wait for each tone separately, but wait for a
   whole queue to become empty.

   tests::cw_queue_tone()
   tests::cw_get_tone_queue_length()
   tests::cw_wait_for_tone_queue()
*/
void test_cw_wait_for_tone_queue(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	const int n_tones_to_add = 6;     /* This is a simple test, so only a handful of tones. */

	/*
	  Test setup:
	  Add tones to tone queue.
	*/
	{
		cw_set_volume(70);

		int freq_min, freq_max;
		cw_get_frequency_limits(&freq_min, &freq_max);
		const int delta_freq = ((freq_max - freq_min) / (n_tones_to_add - 1));

		const int tone_duration = 100000;

		for (int i = 0; i < n_tones_to_add; i++) {
			const int freq = freq_min + i * delta_freq;
			int cwret = cw_queue_tone(tone_duration, freq);
			const bool success = tests->expect_eq_int(tests, CW_SUCCESS, cwret, "setup: cw_queue_tone(%d, %d):", tone_duration, freq);
			if (!success) {
				break;
			}
		}
	}

	/*
	  Test 1 (supplementary):
	  Queue with enqueued tones should have some specific length.
	*/
	{
		const int len = cw_get_tone_queue_length();
		tests->expect_eq_int(tests, n_tones_to_add, len, "test: cw_get_tone_queue_length()");
	}

	/*
	  Test 2 (main):
	  We should be able to wait for emptying of non-empty queue.
	*/
	{
		int cwret = cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, CW_SUCCESS, cwret, "test: cw_wait_for_tone_queue()");
	}

	/* Test tear-down. */
	{
	}

	tests->print_test_footer(tests, __func__);

	return;
}





/**
   Run the complete range of tone generation, at X Hz intervals, first
   up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.

   TODO: this test doesn't really test anything well. It just ensures
   that in some conditions cw_queue_tone() works correctly.

   tests::cw_queue_tone()
*/
void test_cw_queue_tone(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	cw_set_volume(70);
	int duration = 40000;

	int freq_min, freq_max;
	cw_get_frequency_limits(&freq_min, &freq_max);
	const int freq_delta = 100;

	bool wait_success = true;
	bool queue_success = true;

	for (int freq = freq_min; freq < freq_max; freq += freq_delta) {
		while (true == cw_is_tone_queue_full()) {

			/* TODO: we may never get to test
			   cw_wait_for_tone() function because the
			   queue will never be full in this test. */
			int cwret = cw_wait_for_tone();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_wait_for_tone(#1, %d)", freq)) {
				wait_success = false;
				break;
			}
		}

		int cwret = cw_queue_tone(duration, freq);
		if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_queue_tone(#1, %d)", freq)) {
			queue_success = false;
			break;
		}
	}

	for (int freq = freq_max; freq > freq_min; freq -= freq_delta) {
		while (true == cw_is_tone_queue_full()) {

			/* TODO: we may never get to test
			   cw_wait_for_tone() function because the
			   queue will never be full in this test. */
			int cwret = cw_wait_for_tone();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_wait_for_tone(#2, %d)", freq)) {
				wait_success = false;
				break;
			}
		}

		int cwret = cw_queue_tone(duration, freq);
		if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_queue_tone(#2, %d)", freq)) {
			queue_success = false;
			break;
		}
	}

	/* Final expect for 'queue' and 'wait' calls in the loop above. */
	tests->expect_eq_int(tests, true, queue_success, "cw_queue_tone() - enqueueing");
	tests->expect_eq_int(tests, true, wait_success, "cw_queue_tone() - waiting");


	/* We have been adding tones to the queue, so we can test
	   waiting for the queue to be emptied. */
	int cwret = cw_wait_for_tone_queue();
	tests->expect_eq_int(tests, CW_SUCCESS, cwret, "cw_wait_for_tone_queue()");


	tests->print_test_footer(tests, __func__);

	return;
}





/**
   tests::cw_get_tone_queue_capacity()
   tests::cw_get_tone_queue_length()
*/
void test_empty_tone_queue(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	/* Test setup. */
	{
		cw_set_volume(70);

		/* Clear tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();
		cw_wait_for_tone_queue();
	}

	/* Test. */
	{
		const int capacity = cw_get_tone_queue_capacity();
		tests->expect_eq_int(tests, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "cw_get_tone_queue_capacity()");

		const int len_empty = cw_get_tone_queue_length();
		tests->expect_eq_int(tests, 0, len_empty, "cw_get_tone_queue_length() when tq is empty");
	}

	/* Test tear-down. */
	{
	}

	tests->print_test_footer(tests, __func__);
}




/**
   tests::cw_get_tone_queue_capacity()
   tests::cw_get_tone_queue_length()
   tests::cw_queue_tone()
   tests::cw_flush_tone_queue()
   tests::cw_wait_for_tone_queue()
*/
void test_full_tone_queue(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	/* Test setup. */
	{
		cw_set_volume(70);

		/* FIXME: we call cw_queue_tone() until tq is full,
		   and then expect the tq to be full while we perform
		   tests. Doesn't the tq start dequeuing tones right
		   away? Can we expect the tq to be full for some time
		   after adding last tone?  Hint: check when a length
		   of tq is decreased. Probably after playing first
		   tone on tq, which - in this test - is pretty
		   long. Or perhaps not. */

		const int duration = 1000000;
		int i = 0;

		/* FIXME: cw_is_tone_queue_full() is not tested */
		while (!cw_is_tone_queue_full()) {
			const int freq = 100 + (i++ & 1) * 100;
			cw_queue_tone(duration, freq);
		}
	}

	/*
	  Test 1
	  Test properties (capacity and length) of full tq.
	*/
	{
		const int capacity = cw_get_tone_queue_capacity();
		tests->expect_eq_int(tests, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "cw_get_tone_queue_capacity()");

		const int len_full = cw_get_tone_queue_length();
		tests->expect_eq_int(tests, CW_TONE_QUEUE_CAPACITY_MAX, len_full, "cw_get_tone_queue_length() when tq is full");
	}

	/*
	  Test 2
	  Attempt to add tone to full queue.
	*/
	{
		errno = 0;
		int cwret = cw_queue_tone(1000000, 100);
		tests->expect_eq_int(tests, EAGAIN, errno, "cw_queue_tone() for full tq (errno)");
		tests->expect_eq_int(tests, CW_FAILURE, cwret, "cw_queue_tone() for full tq (cwret)");
	}

	/*
	  Test 3

	  Check again properties (capacity and length) of empty tq
	  after it has been in use.
	*/
	{
		int cwret;

		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();

		cwret = cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, CW_SUCCESS, cwret, "cw_wait_for_tone_queue() after flushing");

		const int capacity = cw_get_tone_queue_capacity();
		tests->expect_eq_int(tests, CW_TONE_QUEUE_CAPACITY_MAX, capacity, "cw_get_tone_queue_capacity() after flushing");

		/* Test that the tq is really empty after
		   cw_wait_for_tone_queue() has returned. */
		const int len_empty = cw_get_tone_queue_length();
		tests->expect_eq_int(tests, 0, len_empty, "cw_get_tone_queue_length() after flushing");
	}

	/* Test tear-down. */
	{
	}

	tests->print_test_footer(tests, __func__);

	return;
}





static int cw_test_tone_queue_callback_data = 999999;
static int cw_test_helper_tq_callback_capture = false;



void test_tone_queue_callback(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);
#if 0
	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		int rv = cw_register_tone_queue_low_callback(cw_test_helper_tq_callback, (void *) &cw_test_tone_queue_callback_data, level);
		tests->expect_eq_int(tests, );
		bool failure = rv == CW_FAILURE;
		sleep(1);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_register_tone_queue_low_callback(): threshold = %d:", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			int duration = 10000;
			int f = 440;
			rv = cw_queue_tone(duration, f);
			assert (rv);
		}

		/* Allow the callback to work only after initial
		   filling of queue. */
		cw_test_helper_tq_callback_capture = true;

		/* Wait for the queue to be drained to zero. While the
		   tq is drained, and level of tq reaches trigger
		   level, a callback will be called. Its only task is
		   to copy the current level (tq level at time of
		   calling the callback) value into
		   cw_test_tone_queue_callback_data.

		   Since the value of trigger level is different in
		   consecutive iterations of loop, we can test the
		   callback for different values of trigger level. */
		cw_wait_for_tone_queue();

		/* Because of order of calling callback and decreasing
		   length of queue, I think that it's safe to assume
		   that there may be a difference of 1 between these
		   two values. */
		int diff = level - cw_test_tone_queue_callback_data;
		tests->expect_eq_int(tests, );
		failure = diff > 1;

		failure ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "tone queue callback:           level at callback = %d:", cw_test_tone_queue_callback_data);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_reset_tone_queue();
	}

#endif
	tests->print_test_footer(tests, __func__);

	return;
}





static void cw_test_helper_tq_callback(void *data)
{
	if (cw_test_helper_tq_callback_capture) {
	int *d = (int *) data;
	*d = cw_get_tone_queue_length();

		cw_test_helper_tq_callback_capture = false;
	}

	return;
}





/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.

   tests::cw_get_volume_limits()
*/
void test_volume_functions(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	int vol_min = -1;
	int vol_max = -1;

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&vol_min, &vol_max);

		tests->expect_eq_int(tests, CW_VOLUME_MIN, vol_min, "cw_get_volume_limits() - min = %d%%", vol_min);
		tests->expect_eq_int(tests, CW_VOLUME_MAX, vol_max, "cw_get_volume_limits() - max = %d%%", vol_max);
	}


	/*
	  Test setup.
	  Fill the tone queue with valid tones.
	*/
	{
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}
	}

	/* Test: decrease volume from max to min. */
	{
		bool set_failure = false;
		bool get_failure = false;

		for (int volume = vol_max; volume >= vol_min; volume -= 10) {

			/* We wait here for next tone so that changes
			   in volume happen once per tone - not more
			   often and not less. */
			cw_wait_for_tone();

			const int cwret = cw_set_volume(volume);
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_set_volume(%d) (down)", volume)) {
				set_failure = true;
				break;
			}

			const int readback = cw_get_volume();
			if (!tests->expect_eq_int_errors_only(tests, volume, readback, "cw_get_volume() (down) -> %d", readback)) {
				get_failure = true;
				break;
			}
		}

		tests->expect_eq_int(tests, false, set_failure, "cw_set_volume() (down)");
		tests->expect_eq_int(tests, false, get_failure, "cw_get_volume() (down)");
	}

	/* Test tear-down. */
	{
		cw_flush_tone_queue();
	}


	/* ---------------- */


	/*
	  Test setup.
	  Fill the tone queue with valid tones.
	*/
	{
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}
	}

	/* Test: increase volume from min to max. */
	{
		bool set_failure = false;
		bool get_failure = false;

		for (int volume = vol_min; volume <= vol_max; volume += 10) {

			/* We wait here for next tone so that changes
			   in volume happen once per tone - not more
			   often and not less. */
			cw_wait_for_tone();

			const int cwret = cw_set_volume(volume);
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_set_volume(%d) (up)", volume)) {
				set_failure = true;
				break;
			}

			const int readback = cw_get_volume();
			if (!tests->expect_eq_int_errors_only(tests, volume, readback, "cw_get_volume() (up) -> %d", readback)) {
				get_failure = true;
				break;
			}
		}

		tests->expect_eq_int(tests, false, set_failure, "cw_set_volume() (up)");
		tests->expect_eq_int(tests, false, get_failure, "cw_get_volume() (up)");
	}

	/* Test tear-down. */
	{
		cw_flush_tone_queue();
	}

	tests->print_test_footer(tests, __func__);

	return;
}




/**
   \brief Test enqueueing most basic elements of Morse code

   tests::cw_send_dot()
   tests::cw_send_dash()
   tests::cw_send_character_space()
   tests::cw_send_word_space()
*/
void test_send_primitives(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	int N = 20;

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_send_dot();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_dot() #%d", i)) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, false, failure, "cw_send_dot()");
	}

	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_send_dash();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_dash() #%d", i)) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, false, failure, "cw_send_dash()");
	}

	/* Test: sending character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_send_character_space();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_character_space() #%d", i)) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, false, failure, "cw_send_character_space()");
	}

	/* Test: sending word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			const int cwret = cw_send_word_space();
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_word_space() #%d", i)) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();
		tests->expect_eq_int(tests, false, failure, "cw_send_word_space()");
	}

	tests->print_test_footer(tests, __func__);

	return;
}





/**
   \brief Enqueueing representations of characters

   tests::cw_send_representation()
   tests::cw_send_representation_partial()
*/
void test_representations(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	const char * valid_representations[] = {
		".-.-.-",
		".-",
		"---",
		"...-",

		NULL,      /* Guard. */
	};

	const char * invalid_representations[] = {
		"INVALID", /* Not a representation at all (no dots/dashes). */
		"_._",     /* There is no character that would be represented like this. */
		"-_-",     /* There is no character that would be represented like this. */

		NULL,      /* Guard. */
	};

	/* Test: sending valid representations. */
	{
		bool failure = false;
		int i = 0;
		while (NULL != valid_representations[i]) {
			const int cwret = cw_send_representation(valid_representations[i]);
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_representation(valid #%d)", i)) {
				failure = true;
				break;
			}
			i++;
		}
		tests->expect_eq_int(tests, false, failure, "cw_send_representation(valid)");
		cw_wait_for_tone_queue();
	}

	/* Test: sending invalid representations. */
	{
		bool failure = false;
		int i = 0;
		while (NULL != invalid_representations[i]) {
			const int cwret = cw_send_representation(invalid_representations[i]);
			if (!tests->expect_eq_int_errors_only(tests, CW_FAILURE, cwret, "cw_send_representation(invalid #%d)", i)) {
				failure = true;
				break;
			}
			i++;
		}
		tests->expect_eq_int(tests, false, failure, "cw_send_representation(invalid)");
		cw_wait_for_tone_queue();
	}

	/* Test: sending partial representation of a valid string. */
	{
		bool failure = false;
		int i = 0;
		while (NULL != valid_representations[i]) {
			const int cwret = cw_send_representation_partial(valid_representations[i]);
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_representation_partial(valid #%d)", i)) {
				failure = true;
				break;
			}
			i++;
		}
		tests->expect_eq_int(tests, false, failure, "cw_send_representation_partial(valid)");
		cw_wait_for_tone_queue();
	}

	/* Test: sending partial representation of a invalid string. */
	{
		bool failure = false;
		int i = 0;
		while (NULL != invalid_representations[i]) {
			const int cwret = cw_send_representation_partial(invalid_representations[i]);
			if (!tests->expect_eq_int_errors_only(tests, CW_FAILURE, cwret, "cw_send_representation_partial(invalid #%d)", i)) {
				failure = true;
				break;
			}
			i++;
		}
		tests->expect_eq_int(tests, false, failure, "cw_send_representation_partial(invalid)");
		cw_wait_for_tone_queue();
	}

	cw_wait_for_tone_queue();

	tests->print_test_footer(tests, __func__);

	return;
}




/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_list_characters()
   tests::cw_send_character()
   tests::cw_send_string()
*/
void test_send_character_and_string(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

	/* Test: sending all supported characters as individual characters. */
	{
		char charlist[UCHAR_MAX + 1]; /* TODO: get size of this buffer through cw_get_character_count(). */
		cw_list_characters(charlist);

		bool failure = false;

		/* Send all the characters from the charlist individually. */

		fprintf(tests->stdout,
			MSG_PREFIX "cw_send_character(<valid>):\n"
			MSG_PREFIX "    ");

		for (int i = 0; charlist[i] != '\0'; i++) {

			const char character = charlist[i];
			fprintf(tests->stdout, "%c", character);
			fflush(tests->stdout);

			const int cwret = cw_send_character(character);
			if (!tests->expect_eq_int_errors_only(tests, CW_SUCCESS, cwret, "cw_send_character(%c)", character)) {
				failure = true;
				break;
			}
			cw_wait_for_tone_queue();
		}

		fprintf(tests->stdout, "\n");
		fflush(tests->stdout);

		tests->expect_eq_int(tests, false, failure, "cw_send_character(<valid>)");
	}

	/* Test: sending invalid character. */
	{
		const int cwret = cw_send_character(0);
		tests->expect_eq_int(tests, CW_FAILURE, cwret, "cw_send_character(<invalid>)");
	}

	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1]; /* TODO: get size of this buffer through cw_get_character_count(). */
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		fprintf(tests->stdout, MSG_PREFIX "cw_send_string(<valid>):\n"
		       MSG_PREFIX "    %s\n", charlist);

		const int cwret = cw_send_string(charlist);
		tests->expect_eq_int(tests, CW_SUCCESS, cwret, "cw_send_string(<valid>)");


		while (cw_get_tone_queue_length() > 0) {
			fprintf(tests->stdout, MSG_PREFIX "tone queue length %-6d\r", cw_get_tone_queue_length());
			fflush(tests->stdout);
			cw_wait_for_tone();
		}
		fprintf(tests->stdout, MSG_PREFIX "tone queue length %-6d\n", cw_get_tone_queue_length());
	}


	/* Test: sending invalid string. */
	{
		const int cwret = cw_send_string("%INVALID%");
		tests->expect_eq_int(tests, CW_FAILURE, cwret, "cw_send_string(<invalid>)");
	}


	tests->print_test_footer(tests, __func__);

	return;
}





/**
*/
void test_keyer(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);

#if 0

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */

	int dot_paddle, dash_paddle;

	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		tests->expect_eq_int(tests, );
		bool failure = !cw_notify_keyer_paddle_event(true, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_notify_keyer_paddle_event(true, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		printf(MSG_PREFIX "testing iambic keyer dots   ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('.');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		tests->expect_eq_int(tests, );
		bool failure = !dot_paddle || dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_keyer_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		tests->expect_eq_int(tests, );
		bool failure = !cw_notify_keyer_paddle_event(false, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_notify_keyer_paddle_event(false, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		printf(MSG_PREFIX "testing iambic keyer dashes ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('-');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		tests->expect_eq_int(tests, );
		bool failure = dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying alternate dit/dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Both
		   arguments are true, so both paddles are pressed at
		   the same time.*/
		tests->expect_eq_int(tests, );
		bool failure = !cw_notify_keyer_paddle_event(true, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_notify_keyer_paddle_event(true, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		bool success = true;
		printf(MSG_PREFIX "testing iambic alternating  ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('#');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "cw_wait_for_keyer_element:");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		tests->expect_eq_int(tests, );
		bool failure = !dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		tests->expect_eq_int(tests, );
		bool failure = !cw_notify_keyer_paddle_event(false, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_notify_keyer_paddle_event(false, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_wait_for_keyer();

#endif

	tests->print_test_footer(tests, __func__);

	return;
}





/**
*/
void test_straight_key(cw_test_t * tests)
{
	printf(MSG_PREFIX "%s():  ", __func__);

#if 0

	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		struct timespec t;
		int usecs = CW_USECS_PER_SEC;
		cw_usecs_to_timespec_internal(&t, usecs);


		/* Alternate between open and closed. */
		for (int i = 0; i < 5; i++) {
			if (CW_SUCCESS != cw_notify_straight_key_event(CW_KEY_STATE_OPEN)) {
				tests->expect_eq_int(tests, );
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_OPEN != cw_get_straight_key_state()) {
				tests->expect_eq_int(tests, );
				state_failure = true;
				break;
			}

			if (cw_is_straight_key_busy()) {
				tests->expect_eq_int(tests, );
				busy_failure = true;
				break;
			}

			printf("0");
			fflush(stdout);
#ifdef __FreeBSD__
			/* There is a problem with nanosleep() and
			   signals on FreeBSD. */
			sleep(1);
#else
			cw_nanosleep_internal(&t);
#endif

			if (CW_SUCCESS != cw_notify_straight_key_event(CW_KEY_STATE_CLOSED)) {
				tests->expect_eq_int(tests, );
				event_failure = true;
				break;
			}

			if (CW_KEY_STATE_CLOSED != cw_get_straight_key_state()) {
				tests->expect_eq_int(tests, );
				state_failure = true;
				break;
			}

			if (!cw_is_straight_key_busy()) {
				tests->expect_eq_int(tests, );
				busy_failure = true;
				break;
			}

			printf("1");
			fflush(stdout);
#ifdef __FreeBSD__
			/* There is a problem with nanosleep() and
			   signals on FreeBSD. */
			sleep(1);
#else
			cw_nanosleep_internal(&t);
#endif
		}

		/* Whatever happens during tests, keep the key open after the tests. */
		cw_notify_straight_key_event(CW_KEY_STATE_OPEN);

		printf("\n");
		fflush(stdout);

		event_failure ? stats->failures++ : stats->successes++;
		int n = printf(MSG_PREFIX "cw_notify_straight_key_event(<key open/closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = printf(MSG_PREFIX "cw_straight_key_busy():");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}


	sleep(1);

#endif

	tests->print_test_footer(tests, __func__);

	return;
}





# if 0
/*
 * cw_test_delayed_release()
 */
void cw_test_delayed_release(cw_test_t * tests)
{
	tests->print_test_header(tests, __func__);


	int failures = 0;
	struct timeval start, finish;
	int is_released, delay;

	/* This is slightly tricky to detect, but circumstantial
	   evidence is provided by SIGALRM disposition returning to SIG_DFL. */
	if (!cw_send_character_space()) {
		printf(MSG_PREFIX "ERROR: cw_send_character_space()\n");
		failures++;
	}

	if (gettimeofday(&start, NULL) != 0) {
		printf(MSG_PREFIX "WARNING: gettimeofday failed, test incomplete\n");
		return;
	}
	printf(MSG_PREFIX "waiting for cw_finalization delayed release");
	fflush(stdout);
	do {
		struct sigaction disposition;

		sleep(1);
		if (sigaction(SIGALRM, NULL, &disposition) != 0) {
			printf(MSG_PREFIX "WARNING: sigaction failed, test incomplete\n");
			return;
		}
		is_released = disposition.sa_handler == SIG_DFL;

		if (gettimeofday(&finish, NULL) != 0) {
			printf(MSG_PREFIX "WARNING: gettimeofday failed, test incomplete\n");
			return;
		}

		delay = (finish.tv_sec - start.tv_sec) * 1000000 + finish.tv_usec
			- start.tv_usec;
		putchar('.');
		fflush(stdout);
	}
	while (!is_released && delay < 20000000);
	putchar('\n');

	/* The release should be around 10 seconds after the end of
	   the sent space.  A timeout or two might leak in, reducing
	   it by a bit; we'll be ecstatic with more than five
	   seconds. */
	if (is_released) {
		printf(MSG_PREFIX "cw_finalization delayed release after %d usecs\n", delay);
		if (delay < 5000000) {
			printf(MSG_PREFIX "ERROR: cw_finalization release too quick\n");
			failures++;
		}
	} else {
		printf(MSG_PREFIX "ERROR: cw_finalization release wait timed out\n");
		failures++;
	}


	tests->print_test_footer(tests, __func__);

	return;
}





/*
 * cw_test_signal_handling_callback()
 * cw_test_signal_handling()
 */
static int cw_test_signal_handling_callback_called = false;
void cw_test_signal_handling_callback(int signal_number)
{
	signal_number = 0;
	cw_test_signal_handling_callback_called = true;
}





void cw_test_signal_handling(cw_test_t * tests)
{
	int failures = 0;
	struct sigaction action, disposition;

	/* Test registering, unregistering, and raising SIGUSR1.
	   SIG_IGN and handlers are tested, but not SIG_DFL, because
	   that stops the process. */
	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf(MSG_PREFIX "ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1,
                                   cw_test_signal_handling_callback)) {
		printf(MSG_PREFIX "ERROR: cw_register_signal_handler failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (!cw_test_signal_handling_callback_called) {
		printf(MSG_PREFIX "ERROR: cw_test_signal_handling_callback missed\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf(MSG_PREFIX "ERROR: cw_register_signal_handler (overwrite) failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (cw_test_signal_handling_callback_called) {
		printf(MSG_PREFIX "ERROR: cw_test_signal_handling_callback called\n");
		failures++;
	}

	if (!cw_unregister_signal_handler(SIGUSR1)) {
		printf(MSG_PREFIX "ERROR: cw_unregister_signal_handler failed\n");
		failures++;
	}

	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf(MSG_PREFIX "ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	action.sa_handler = cw_test_signal_handling_callback;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGUSR1, &action, &disposition) != 0) {
		printf(MSG_PREFIX "WARNING: sigaction failed, test incomplete\n");
		return failures;
	}
	if (cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf(MSG_PREFIX "ERROR: cw_register_signal_handler clobbered\n");
		failures++;
	}
	if (sigaction(SIGUSR1, &disposition, NULL) != 0) {
		printf(MSG_PREFIX "WARNING: sigaction failed, test incomplete\n");
		return failures;
	}

	printf(MSG_PREFIX "cw_[un]register_signal_handler tests complete\n");
	return;
}
#endif




#if 0
/* "Forever" functionality is not exactly part of public
   interface. The functionality will be tested only as part of
   internal test. */

/*
  Version of test_cw_gen_forever() to be used in libcw_test_internal
  test executable.

  Because the function calls cw_generator_delete(), it should be
  executed as last test in test suite (unless you want to call
  cw_generator_new/start() again). */
void test_cw_gen_forever_public(cw_test_t * tests)
{
	/* Make sure that an audio sink is closed. If we try to open
	   an OSS sink that is already open, we may end up with
	   "resource busy" error in libcw_oss.c module (that's what
	   happened on Alpine Linux).

	   Because of this call this test should be executed as last
	   one. */
	cw_generator_delete();

	int seconds = 5;
	printf(MSG_PREFIX "%s() (%d seconds):\n", __func__, seconds);

	unsigned int rv = test_cw_gen_forever_sub(seconds, test_audio_system, NULL);
	rv == 0 ? stats->successes++ : stats->failures++;

	tests->print_test_footer(tests, __func__);

	return;
}
#endif




/*---------------------------------------------------------------------*/
/*  Unit tests drivers                                                 */
/*---------------------------------------------------------------------*/





/**
   \brief Set up common test conditions

   Run before each individual test, to handle setup of common test conditions.
*/
void cw_test_setup(void)
{
	cw_reset_send_receive_parameters();
	cw_set_send_speed(30);
	cw_set_receive_speed(30);
	cw_disable_adaptive_receive();
	cw_reset_receive_statistics();
	cw_unregister_signal_handler(SIGUSR1);
	errno = 0;

	return;
}




/* Tests that are dependent on a sound system being configured.
   Tone queue module functions */
static void (*const CW_TEST_FUNCTIONS_DEP_TQ[])(cw_test_t *) = {
	test_cw_wait_for_tone,
	test_cw_wait_for_tone_queue,
	test_cw_queue_tone,

	test_empty_tone_queue,
	test_full_tone_queue,

	test_tone_queue_callback,

	NULL
};




/* Tests that are dependent on a sound system being configured.
   Generator module functions. */
static void (*const CW_TEST_FUNCTIONS_DEP_GEN[])(cw_test_t *) = {
	test_volume_functions,
	test_send_primitives,
	test_send_character_and_string,
	test_representations,

	NULL
};


/* Tests that are dependent on a sound system being configured.
   Morse key module functions */
static void (*const CW_TEST_FUNCTIONS_DEP_K[])(cw_test_t *) = {
	test_keyer,
	test_straight_key,

	NULL
};


/* Tests that are dependent on a sound system being configured.
   Other modules' functions. */
static void (*const CW_TEST_FUNCTIONS_DEP_O[])(cw_test_t *) = {
	test_parameter_ranges,
	//test_cw_gen_forever_public,

	//cw_test_delayed_release,
	//cw_test_signal_handling, /* FIXME - not sure why this test fails :( */

	NULL
};





/**
   \brief Run tests for given audio system.

   Perform a series of self-tests on library public interfaces, using
   audio system specified with \p audio_system. Range of tests is specified
   with \p testset.

   \param audio_system - audio system to use for tests
   \param modules - libcw modules to be tested
   \param stats - test statistics

   \return -1 on failure to set up tests
   \return 0 if tests were run, and no errors occurred
   \return 1 if tests were run, and some errors occurred
*/
int cw_test_modules_with_one_sound_system(const char * modules, int audio_system, cw_test_t * tests)
{
	test_audio_system = audio_system;

	int rv = cw_generator_new(audio_system, NULL);
	if (rv != 1) {
		fprintf(stderr, MSG_PREFIX "can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(stderr, MSG_PREFIX "can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}


	if (strstr(modules, "t")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_TQ[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_TQ[test])(tests);
		}
	}


	if (strstr(modules, "g")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_GEN[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_GEN[test])(tests);
		}
	}


	if (strstr(modules, "k")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_K[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_K[test])(tests);
		}
	}


	if (strstr(modules, "o")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_O[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_O[test])(tests);
		}
	}


	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return tests->stats->failures ? 1 : 0;
}





/**
   \brief Run a series of tests for specified audio systems

   Function attempts to run a set of testcases for every audio system
   specified in \p sound_systems. These testcases require some kind of
   audio system configured. The function calls
   cw_test_modules_with_one_sound_system() to do the configuration and
   run the tests.

   \p sound_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param sound_systems - list of audio systems to be tested
*/
int cw_test_modules_with_sound_systems(const char * modules, const char * sound_systems)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;

	cw_test_t test_set;
	cw_test_init(&test_set, stdout, stderr, MSG_PREFIX);


	if (!sound_systems || strstr(sound_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with null output\n");
			test_set.stats = &cw_stats_null;
			n = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_NULL, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "null output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with console output\n");
			test_set.stats = &cw_stats_console;
			c = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_CONSOLE, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "console output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with OSS output\n");
			test_set.stats = &cw_stats_oss;
			o = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_OSS, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "OSS output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with ALSA output\n");
			test_set.stats = &cw_stats_alsa;
			a = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_ALSA, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "Alsa output not available\n");
		}
	}

	if (!sound_systems || strstr(sound_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, MSG_PREFIX "testing with PulseAudio output\n");
			test_set.stats = &cw_stats_pa;
			p = cw_test_modules_with_one_sound_system(modules, CW_AUDIO_PA, &test_set);
		} else {
			fprintf(stderr, MSG_PREFIX "PulseAudio output not available\n");
		}
	}

	if (!n && !c && !o && !a && !p) {
		return 0;
	} else {
		return -1;
	}
}





void cw_test_print_stats(void)
{
	printf("\n\n"MSG_PREFIX "Statistics of tests:\n\n");

	printf(MSG_PREFIX "Tests not requiring any audio system:            ");
	if (cw_stats_indep.failures + cw_stats_indep.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_indep.failures, cw_stats_indep.failures + cw_stats_indep.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with NULL audio system:          ");
	if (cw_stats_null.failures + cw_stats_null.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_null.failures, cw_stats_null.failures + cw_stats_null.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with console audio system:       ");
	if (cw_stats_console.failures + cw_stats_console.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_console.failures, cw_stats_console.failures + cw_stats_console.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with OSS audio system:           ");
	if (cw_stats_oss.failures + cw_stats_oss.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_oss.failures, cw_stats_oss.failures + cw_stats_oss.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with ALSA audio system:          ");
	if (cw_stats_alsa.failures + cw_stats_alsa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_alsa.failures, cw_stats_alsa.failures + cw_stats_alsa.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf(MSG_PREFIX "Tests performed with PulseAudio audio system:    ");
	if (cw_stats_pa.failures + cw_stats_pa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_pa.failures, cw_stats_pa.failures + cw_stats_pa.successes);
	} else {
		printf("no tests were performed\n");
	}

	return;
}





/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	int rv = 0;

	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

	//cw_debug_set_flags(&cw_debug_object_dev, CW_DEBUG_RECEIVE_STATES | CW_DEBUG_TONE_QUEUE | CW_DEBUG_GENERATOR | CW_DEBUG_KEYING);
	//cw_debug_object_dev.level = CW_DEBUG_DEBUG;

	unsigned int testset = 0;

	struct timeval seed;
	gettimeofday(&seed, NULL);
	// fprintf(stderr, "seed: %d\n", (int) seed.tv_usec);
	srand(seed.tv_usec);

	/* Obtain a bitmask of the tests to run from the command line
	   arguments. If none, then default to ~0, which effectively
	   requests all tests. */
	if (argc > 1) {
		testset = 0;
		for (int arg = 1; arg < argc; arg++) {
			unsigned int test = strtoul(argv[arg], NULL, 0);
			testset |= 1 << test;
		}
	} else {
		testset = ~0;
	}

#define CW_SYSTEMS_MAX 5
	char sound_systems[CW_SYSTEMS_MAX + 1];
#define CW_MODULES_MAX 4  /* g, t, k, o */
	char modules[CW_MODULES_MAX + 1];
	modules[0] = '\0';

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats);

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, MSG_PREFIX "ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}

	rv = cw_test_modules_with_sound_systems(modules, sound_systems);

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
