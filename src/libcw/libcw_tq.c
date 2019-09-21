/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)

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


/**
   \file libcw_tq.c

   \brief Queue of tones to be converted by generator to pcm data and
   sent to audio sink.

   Tone queue - a circular list of tone durations and frequencies
   pending, with a pair of indexes: tail (enqueue) and head (dequeue).
   The indexes are used to manage addition and removal of tones from
   queue.


   The tone queue (the circular list) is implemented using constant
   size table.


   Explanation of "forever" tone:

   If a "forever" flag is set in a tone that is a last one on a tone
   queue, the tone should be constantly returned by dequeue function,
   without removing the tone - as long as it is a last tone on queue.

   Adding new, "non-forever" tone to the queue results in permanent
   dequeuing "forever" tone and proceeding to newly added tone.
   Adding the new "non-forever" tone ends generation of "forever" tone.

   The "forever" tone is useful for generating tones of length unknown
   in advance.

   dequeue() function recognizes the "forever" tone and acts as
   described above; there is no visible difference between dequeuing N
   separate "non-forever" tones of length L [us], and dequeuing a
   "forever" tone of length L [us] N times in a row.

   Because of some corner cases related to "forever" tones it is very
   strongly advised to set "low water mark" level to no less than 2
   tones.


   Tone queue data type is not visible to user of library's API. Tone
   queue is an integral part of a generator. Generator data type is
   visible to user of library's API.
*/




#include <inttypes.h> /* "PRIu32" */
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h> /* SIGALRM */
#include <unistd.h> /* sleep() */




#include "libcw.h"
#include "libcw_tq.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_signal.h"
#include "libcw2.h"




#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif




#define MSG_PREFIX "libcw/tq: "




/*
   The CW tone queue functions implement the following state graph:

                              (queue empty)
            +-----------------------------------------------------+
            |                                                     |
            |                                                     |
            |        (tone(s) added to queue,                     |
            v        dequeueing process started)                  |
   ----> CW_TQ_IDLE -------------------------------> CW_TQ_BUSY --+
                                                 ^        |
                                                 |        |
                                                 +--------+
                                             (queue not empty)


   Above diagram shows two states of a queue, but dequeue function
   returns three distinct values: CW_TQ_DEQUEUED,
   CW_TQ_NDEQUEUED_EMPTY, CW_TQ_NDEQUEUED_IDLE. Having these three
   values is important for the function that calls the dequeue
   function. If you ever intend to limit number of return values of
   dequeue function to two, you will also have to re-think how
   cw_gen_dequeue_and_generate_internal() operates.

   Future libcw API should (completely) hide tone queue from client
   code. The client code should only operate on a generator - enqueue
   tones to generator, flush a generator, register low water callback
   with generator etc. There is very little (or even no) need to
   explicitly reveal to client code this implementation detail called
   "tone queue".
*/





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;




static int    cw_tq_set_capacity_internal(cw_tone_queue_t *tq, size_t capacity, size_t high_water_mark);
static size_t cw_tq_get_high_water_mark_internal(const cw_tone_queue_t *tq) __attribute__((unused));
static size_t cw_tq_prev_index_internal(const cw_tone_queue_t *tq, size_t current) __attribute__((unused));
static size_t cw_tq_next_index_internal(const cw_tone_queue_t *tq, size_t current);
static int    cw_tq_dequeue_sub_internal(cw_tone_queue_t *tq, cw_tone_t *tone);




/* Not used anymore. 2015.02.22. */
#if 0
/* Remember that tail and head are of unsigned type.  Make sure that
   order of calculations is correct when tail < head. */
#define CW_TONE_QUEUE_LENGTH(m_tq)				\
	( m_tq->tail >= m_tq->head				\
	  ? m_tq->tail - m_tq->head				\
	  : m_tq->capacity - m_tq->head + m_tq->tail)		\

#endif





/**
   \brief Create new tone queue

   Allocate and initialize new tone queue structure.

   testedin::test_cw_tone_queue_init_internal()

   \return pointer to new tone queue on success
   \return NULL pointer on failure
*/
cw_tone_queue_t *cw_tq_new_internal(void)
{
	cw_tone_queue_t *tq = (cw_tone_queue_t *) malloc(sizeof (cw_tone_queue_t));
	if (!tq) {
		cw_debug_msg (&cw_debug_object, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
				      MSG_PREFIX "new: failed to malloc() tone queue");
		return (cw_tone_queue_t *) NULL;
	}

	int rv = pthread_mutex_init(&tq->mutex, NULL);
	cw_assert (!rv, MSG_PREFIX "new: failed to initialize mutex");

	pthread_mutex_lock(&tq->mutex);

	/* This function operates on cw_tq_t::wait_var and
	   cdw_tq_t::wait_mutex. Therefore it needs to be called
	   after pthread_X_init(). */
	cw_tq_make_empty_internal(tq);

	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;
	tq->call_callback = false;

	tq->gen = (cw_gen_t *) NULL; /* This field will be set by generator code. */

	rv = cw_tq_set_capacity_internal(tq, CW_TONE_QUEUE_CAPACITY_MAX, CW_TONE_QUEUE_HIGH_WATER_MARK_MAX);
	cw_assert (rv, MSG_PREFIX "new: failed to set initial capacity of tq");

	pthread_mutex_unlock(&tq->mutex);

	return tq;
}





void cw_tq_delete_internal(cw_tone_queue_t **tq)
{
	cw_assert (tq, MSG_PREFIX "delete: pointer to tq is NULL");

	if (!tq || !*tq) {
		return;
	}

	free(*tq);
	*tq = (cw_tone_queue_t *) NULL;

	return;
}




/**
   \brief Reset state of given tone queue

   This makes the \p tq empty, but without calling low water mark callback.

*/
void cw_tq_make_empty_internal(cw_tone_queue_t * tq)
{
#if 0
	int rv = pthread_mutex_trylock(&tq->mutex);
	cw_assert (rv == EBUSY, MSG_PREFIX "make empty: resetting tq state outside of mutex!");

	pthread_mutex_lock(&tq->wait_mutex);
#endif

	tq->head = 0;
	tq->tail = 0;
	tq->len = 0;
	tq->state = CW_TQ_IDLE;

#if 0
	//fprintf(stderr, MSG_PREFIX "make empty: broadcast on tq->len = 0\n");
	pthread_cond_broadcast(&tq->wait_var);
	pthread_mutex_unlock(&tq->wait_mutex);
#endif

	return;
}





/**
   \brief Set capacity and high water mark for queue

   Set two parameters of queue: total capacity of the queue, and high
   water mark. When calling the function, client code must provide
   valid values of both parameters.

   Calling the function *by a client code* for a queue is optional, as
   a queue has these parameters always set to default values
   (CW_TONE_QUEUE_CAPACITY_MAX and CW_TONE_QUEUE_HIGH_WATER_MARK_MAX)
   by internal call to cw_tq_new_internal().

   \p capacity must be no larger than CW_TONE_QUEUE_CAPACITY_MAX.
   \p high_water_mark must be no larger than CW_TONE_QUEUE_HIGH_WATER_MARK_MAX.

   Both values must be larger than zero (this condition is subject to
   changes in future revisions of the library).

   \p high_water_mark must be no larger than \p capacity.

   \errno EINVAL - any of the two parameters (\p capacity or \p high_water_mark) is invalid.

   testedin::test_cw_tq_capacity_test_init()

   \param tq - tone queue to configure
   \param capacity - new capacity of queue
   \param high_water_mark - high water mark for the queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_set_capacity_internal(cw_tone_queue_t *tq, size_t capacity, size_t high_water_mark)
{
	cw_assert (tq, MSG_PREFIX "set capacity: tq is NULL");
	if (!tq) {
		return CW_FAILURE;
	}

	if (high_water_mark == 0 || high_water_mark > CW_TONE_QUEUE_HIGH_WATER_MARK_MAX) {
		/* If we allowed high water mark to be zero, the queue
		   would not accept any new tones: it would constantly
		   be full. Any attempt to enqueue any tone would
		   result in "sorry, new tones would reach above
		   high_water_mark of the queue". */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (capacity == 0 || capacity > CW_TONE_QUEUE_CAPACITY_MAX) {
		/* Tone queue of capacity zero doesn't make much
		   sense, so capacity == 0 is not allowed. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (high_water_mark > capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	tq->capacity = capacity;
	tq->high_water_mark = high_water_mark;

	return CW_SUCCESS;
}




/**
   \brief Return capacity of a queue

   testedin::test_cw_tq_get_capacity_internal()

   \param tq - tone queue, for which you want to get capacity

   \return capacity of tone queue
*/
size_t cw_tq_get_capacity_internal(cw_tone_queue_t *tq)
{
	cw_assert (tq, MSG_PREFIX "get capacity: tone queue is NULL");
	return tq->capacity;
}




/**
   \brief Return high water mark of a queue

   \reviewed on 2017-01-30

   \param tq - tone queue, for which you want to get high water mark

   \return high water mark of tone queue
*/
size_t cw_tq_get_high_water_mark_internal(const cw_tone_queue_t *tq)
{
	cw_assert (tq, MSG_PREFIX "get high water mark: tone queue is NULL");

	return tq->high_water_mark;
}




/**
   \brief Return number of items (tones) on tone queue

   testedin::test_cw_tq_length_internal()

   \param tq - tone queue

   \return the count of tones currently held in the tone queue
*/
size_t cw_tq_length_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);
	size_t len = tq->len;
	pthread_mutex_unlock(&tq->mutex);

	return len;
}




/**
   \brief Get previous index to queue

   Calculate index of previous element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tq_prev_index_internal()

   \param tq - tone queue for which to calculate previous index
   \param ind - index in relation to which to calculate index of previous element in queue

   \return index of previous element in queue
*/
size_t cw_tq_prev_index_internal(const cw_tone_queue_t *tq, size_t ind)
{
	return ind == 0 ? tq->capacity - 1 : ind - 1;
}




/**
   \brief Get next index to queue

   Calculate index of next element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tq_next_index_internal()

   \param tq - tone queue for which to calculate next index
   \param ind - index in relation to which to calculate index of next element in queue

   \return index of next element in queue
*/
size_t cw_tq_next_index_internal(const cw_tone_queue_t *tq, size_t ind)
{
	return ind == tq->capacity - 1 ? 0 : ind + 1;
}




/**
   \brief Dequeue a tone from tone queue

   Dequeue a tone from tone queue.

   The queue returns three distinct values. This may seem overly
   complicated for a tone queue, but it actually works. The way the
   generator interacts with the tone queue, and the way the enqueueing
   works, depend on the dequeue function to return three values. If
   you ever try to make the dequeue function return two values, you
   would also have to redesign parts of generator and of enqueueing
   code.

   Look in cw_gen_write_to_soundcard_internal(). The function makes
   decision based on two distinct tone queue states (described by
   CW_TQ_DEQUEUED or CW_TQ_NDEQUEUED_EMPTY). So the _write() function
   must be executed by generator for both return values. But we also
   need a third return value that will tell the generator not to
   execute _write() *at all*, but just wait for signal. This third
   value is CW_TQ_NDEQUEUED_IDLE.

   These three return values are:

   \li CW_TQ_DEQUEUED - dequeue() function successfully dequeues and
       returns through \p tone a valid tone. dequeue() understands how
       "forever" tone should be handled: if such tone is the last tone
       on the queue, the function actually both returns the "forever"
       tone, and keeps it in queue (until next tone is enqueued).

   \li CW_TQ_NDEQUEUED_EMPTY - dequeue() function can't dequeue a tone
       from tone queue, because the queue has been just emptied, i.e.
       previous call to dequeue() was successful and returned
       CW_TQ_DEQUEUED, but that was the last tone on queue. This
       return value is a way of telling client code "I've had tones,
       but no more, you should probably stop playing any sounds and
       become silent". If no new tones are enqueued, the next call to
       dequeue() will return CW_TQ_NDEQUEUED_IDLE.

   \li CW_TQ_NDEQUEUED_IDLE - dequeue() function can't dequeue a tone
       from tone queue, because the queue is empty, and the tone queue
       has no memory of being non-empty before. This is the value that
       dequeue() would return for brand new tone queue. This is also
       value returned by dequeue() when its previous return value was
       CW_TQ_NDEQUEUED_EMPTY, and no new tones were enqueued since
       then.

   Notice that returned value does not describe internal state of tone
   queue.

   Successfully dequeued tone is returned through function's argument
   \p tone. The function does not modify the arguments if there are no
   tones to dequeue (CW_TQ_NDEQUEUED_EMPTY, CW_TQ_NDEQUEUED_IDLE).

   As mentioned above, dequeue() understands how "forever" tone works.
   If the last tone in queue has "forever" flag set, the function
   won't permanently dequeue it. Instead, it will keep returning
   (through \p tone) the tone on every call, until a new tone is added
   to the queue after the "forever" tone.

   testedin::test_cw_tq_dequeue_internal()
   testedin::test_cw_tq_test_capacity_2()

   \param tq - tone queue
   \param tone - dequeued tone

   \return CW_TQ_DEQUEUED (see information above)
   \return CW_TQ_NDEQUEUED_EMPTY (see information above)
   \return CW_TQ_NDEQUEUED_IDLE (see information above)
*/
int cw_tq_dequeue_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	pthread_mutex_lock(&(tq->mutex));

	/* Decide what to do based on the current state. */
	switch (tq->state) {

	case CW_TQ_IDLE:
		pthread_mutex_unlock(&(tq->mutex));
		/* Ignore calls if our state is idle. */
		return CW_TQ_NDEQUEUED_IDLE;

	case CW_TQ_BUSY:
		/* If there are some tones in queue, dequeue the next
		   tone. If there are no more tones, go to the idle state. */
		if (tq->len) {
			bool call_callback = cw_tq_dequeue_sub_internal(tq, tone);

			/* Notify the key control function about current tone. */
			if (tq->gen && tq->gen->key) {
				cw_key_tk_set_value_internal(tq->gen->key, tone->frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN);
			}

			pthread_mutex_unlock(&(tq->mutex));

			/* Since client's callback can use functions
			   that call libcw's pthread_mutex_lock(), we
			   should put the callback *after* we release
			   pthread_mutex_unlock() in this function. */

			if (call_callback) {
				(*(tq->low_water_callback))(tq->low_water_callback_arg);
			}

			return CW_TQ_DEQUEUED;
		} else { /* tq->len == 0 */
			/* State of tone queue is still "busy", but
			   there are no tones left on the queue.

			   Time to bring tq->state in sync with
			   tq->len. Set state to idle, indicating that
			   dequeuing has finished for the moment. */
			tq->state = CW_TQ_IDLE;

			/* There is no tone to dequeue, so don't
			   modify function's arguments. Client code
			   will learn about "no valid tone returned
			   through function argument" state through
			   return value. */

			/* Notify the key control function about current tone. */
			if (tq->gen && tq->gen->key) {
				cw_key_tk_set_value_internal(tq->gen->key, CW_KEY_STATE_OPEN);
			}

			pthread_mutex_unlock(&(tq->mutex));
			return CW_TQ_NDEQUEUED_EMPTY;
		}
	}

	pthread_mutex_unlock(&(tq->mutex));
	/* will never get here as "queue state" enum has only two values */
	cw_assert(0, "reached unreachable place");
	return CW_TQ_NDEQUEUED_IDLE;
}




/**
   \brief Handle dequeueing of tone from non-empty tone queue

   Function gets a tone from head of the queue.

   If this was a last tone in queue, and it was a "forever" tone, the
   tone is not removed from the queue (the philosophy of "forever"
   tone), and "low watermark" condition is not checked.

   Otherwise remove the tone from tone queue, check "low watermark"
   condition, and return value of the check (true/false).

   In any case, dequeued tone is returned through \p tone. \p tone
   must be a valid pointer provided by caller.

   TODO: add unit tests

   \param tq - tone queue
   \param tone - dequeued tone (output from the function)

   \return true if a condition for calling "low watermark" callback is true
   \return false otherwise
*/
int cw_tq_dequeue_sub_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	CW_TONE_COPY(tone, &(tq->queue[tq->head]));

	if (tone->is_forever && tq->len == 1) {
		/* Don't permanently remove the last tone that is
		   "forever" tone in queue. Keep it in tq until client
		   code adds next tone (this means possibly waiting
		   forever). Queue's head should not be
		   iterated. "forever" tone should be played by caller
		   code, this is why we return the tone through
		   function's argument. */

		/* Don't call "low watermark" callback for "forever"
		   tone. As the function's top-level comment has
		   stated: avoid endlessly calling the callback if the
		   only queued tone is "forever" tone.*/
		return false;
	}

	/* Used to check if we passed tq's low level watermark. */
	size_t tq_len_before = tq->len;

	/* Dequeue. We already have the tone, now update tq's state. */
	tq->head = cw_tq_next_index_internal(tq, tq->head);
	tq->len--;


	if (tq->len == 0) {
		/* Verify basic property of empty tq. */
		cw_assert (tq->head == tq->tail, MSG_PREFIX "dequeue sub: head: %zu, tail: %zu", tq->head, tq->tail);
	}


#if 0   /* Disabled because these debug messages produce lots of output
	   to console. Enable only when necessary. */
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      MSG_PREFIX "dequeue sub: dequeue tone %d us, %d Hz", tone->len, tone->frequency);
	cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      MSG_PREFIX "dequeue sub: head = %zu, tail = %zu, length = %zu -> %zu",
		      tq->head, tq->tail, tq_len_before, tq->len);
#endif

	/* You can remove this assert in future. It is only temporary,
	   to check that some changes introduced on 2015.03.01 didn't
	   break one assumption. */
	cw_assert (!(tone->is_forever && tq_len_before == 1), MSG_PREFIX "dequeue sub: 'forever' tone appears!");


	bool call_callback = false;
	if (tq->low_water_callback) {
		/* It may seem that the double condition in 'if ()' is
		   redundant, but for some reason it is necessary. Be
		   very, very careful when modifying this. */
		if (tq_len_before > tq->low_water_mark
		    && tq->len <= tq->low_water_mark) {

			call_callback = true;
		}
	}

	return call_callback;
}




/**
   \brief Add tone to tone queue

   Enqueue a tone for specified frequency and number of microseconds.
   This routine adds the new tone to the queue, and if necessary sends
   signal to generator, so that the generator can dequeue the tone.

   The routine returns CW_SUCCESS on success. If the tone queue is
   full, the routine returns CW_FAILURE, with errno set to EAGAIN.  If
   the iambic keyer or straight key are currently busy, the routine
   returns CW_FAILURE, with errno set to EBUSY.

   The function does not accept tones with frequency outside of
   CW_FREQUENCY_MIN-CW_FREQUENCY_MAX range.

   If length of a tone (tone->len) is zero, the function does not
   add it to tone queue and returns CW_SUCCESS.

   The function does not accept tones with negative values of len.

   testedin::test_cw_tq_enqueue_internal_1()
   testedin::test_cw_tq_enqueue_internal_2()
   testedin::test_cw_tq_test_capacity_1()
   testedin::test_cw_tq_test_capacity_2()

   \errno EINVAL - invalid values of \p tone
   \errno EAGAIN - tone not enqueued because tone queue is full

   \param tq - tone queue
   \param tone - tone to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	cw_assert (tq, MSG_PREFIX "enqueue: tone queue is null");
	cw_assert (tone, MSG_PREFIX "enqueue: tone is null");

	/* Check the arguments given for realistic values. */
	if (tone->frequency < CW_FREQUENCY_MIN
	    || tone->frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len < 0) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len == 0) {
		/* Drop empty tone. It won't be played anyway, and for
		   now there are no other good reasons to enqueue
		   it. While it may happen in higher-level code to
		   create such tone, but there is no need to spend
		   time on it here. */
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
			      MSG_PREFIX "enqueue: ignoring tone with len == 0");
		return CW_SUCCESS;
	}

#if 0 /* This part is no longer in use. It seems that it's not necessary. 2015.02.22. */
	/* If the keyer or straight key are busy, return an error.
	   This is because they use the sound card/console tones and key
	   control, and will interfere with us if we try to use them at
	   the same time. */
	if (cw_key_ik_is_busy_internal(tq->gen->key) || cw_key_sk_is_busy(tq->gen->key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	pthread_mutex_lock(&tq->mutex);

	if (tq->len == tq->capacity) {
		/* Tone queue is full. */

		errno = EAGAIN;
		cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      MSG_PREFIX "enqueue: can't enqueue tone, tq is full");
		pthread_mutex_unlock(&tq->mutex);

		return CW_FAILURE;
	}


	// cw_debug_msg (&cw_debug_object_dev, CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG, MSG_PREFIX "enqueue: enqueue tone %d us, %d Hz", tone->len, tone->frequency);

	/* Enqueue the new tone.

	   Notice that tail is incremented after adding a tone. This
	   means that for empty tq new tone is inserted at index
	   tail == head (which should be kind of obvious). */
	CW_TONE_COPY(&(tq->queue[tq->tail]), tone);

	tq->tail = cw_tq_next_index_internal(tq, tq->tail);
	tq->len++;


	if (tq->state == CW_TQ_IDLE) {
		/* A loop in cw_gen_dequeue_and_generate_internal()
		   function may await for the queue to be filled with
		   new tones to dequeue and play.  It waits for a
		   notification from tq that there are some new tones
		   in tone queue. This is a right place and time to
		   send such notification. */
		tq->state = CW_TQ_BUSY;
		pthread_kill(tq->gen->thread.id, SIGALRM);
	}

	pthread_mutex_unlock(&(tq->mutex));
	return CW_SUCCESS;
}




/**
   \brief Register callback for low queue state

   Register a function to be called automatically by the dequeue routine
   whenever the tone queue falls to a given \p level. To be more precise:
   the callback is called by queue's dequeue function if, after dequeueing a tone,
   the function notices that tone queue length has become equal or less
   than \p level.

   \p callback_arg may be used to give a value passed back on callback
   calls.  A NULL function pointer suppresses callbacks.

   \errno EINVAL - \p level is invalid

   \reviewed on 2017-01-30

   \param tq - tone queue
   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_tq_register_low_level_callback_internal(cw_tone_queue_t *tq, void (*callback_func)(void*), void *callback_arg, int level)
{
	if (level < 0 || (size_t) level >= tq->capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Store the function and low water mark level. */
	tq->low_water_mark = level;
	tq->low_water_callback = callback_func;
	tq->low_water_callback_arg = callback_arg;

	return CW_SUCCESS;
}




/**
   \brief Check if tone sender is busy

   Indicate if the tone sender is busy.

   \param tq - tone queue

   \return true if there are still entries in the tone queue
   \return false if the queue is empty
*/
bool cw_tq_is_busy_internal(cw_tone_queue_t *tq)
{
	return tq->state != CW_TQ_IDLE;
}





/**
   \brief Wait for the current tone to complete

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns CW_FAILURE, with errno set to EDEADLK,
   to avoid indefinite waits.

   \param tq - tone queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_tone_internal(cw_tone_queue_t *tq)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the head index to change or the dequeue to go idle. */
	size_t check_tq_head = tq->head;
	while (tq->head == check_tq_head && tq->state != CW_TQ_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain

   The routine returns CW_SUCCESS on success. If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK,
   to avoid indefinite waits.

   \param tq - tone queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_tone_queue_internal(cw_tone_queue_t *tq)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the dequeue indicates it has hit the end of the queue. */
	while (tq->state != CW_TQ_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This function is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The function returns when queue's level is equal or lower than \p
   level.  If at the time of function call the level of queue is
   already equal or lower than \p level, function returns immediately.

   testedin::test_cw_tq_wait_for_level_internal()
   testedin::test_cw_tq_operations_2()

   \reviewed on 2017-01-30

   \param tq - tone queue
   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_level_internal(cw_tone_queue_t *tq, size_t level)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the queue length is at or below critical level. */
	while (cw_tq_length_internal(tq) > level) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief See if the tone queue is full

   This is a helper subroutine created so that I can pass a test tone
   queue in unit tests. The 'cw_is_tone_queue_full() works only on
   default tone queue object.

   testedin::test_cw_tq_is_full_internal()

   \reviewed on 2017-01-30

   \param tq - tone queue to check

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_tq_is_full_internal(const cw_tone_queue_t *tq)
{
	return tq->len == tq->capacity;
}





void cw_tq_reset_internal(cw_tone_queue_t *tq)
{
	/* Empty and reset the queue, and force state to idle. */
	tq->len = 0;
	tq->head = tq->tail;
	tq->state = CW_TQ_IDLE;

	/* Reset low water mark details to their initial values. */
	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;

	return;
}





void cw_tq_flush_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&(tq->mutex));

	/* Empty and reset the queue. */
	tq->len = 0;
	tq->head = tq->tail;
	tq->state = CW_TQ_IDLE;

	pthread_mutex_unlock(&(tq->mutex));

	/* If we can, wait until the dequeue goes idle. */
	if (!cw_sigalrm_is_blocked_internal()) {
		cw_tq_wait_for_tone_queue_internal(tq);
	}

	return;
}




/**
   \brief Attempt to remove all tones constituting full, single character

   Try to remove all tones until and including first tone with ->is_first tone flag set.

   The function removes character's tones only if all the tones,
   including the first tone in the character, are still in tone queue.

   \param tq - tone queue
*/
void cw_tq_handle_backspace_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);

	size_t len = tq->len;
	size_t idx = tq->tail;
	bool is_found = false;

	while (len > 0) {
		--len;
		idx = cw_tq_prev_index_internal(tq, idx);
		if (tq->queue[idx].is_first) {
			is_found = true;
			break;
		}
	}

	if (is_found) {
		tq->len = len;
		tq->tail = idx;
	}

	pthread_mutex_unlock(&tq->mutex);
}




/* *** Unit tests *** */





#ifdef LIBCW_UNIT_TESTS


#include "libcw_test.h"

static cw_tone_queue_t *test_cw_tq_capacity_test_init(size_t capacity, size_t high_water_mark, int head_shift);
static unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq, cw_test_stats_t * stats);
static unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq, cw_test_stats_t * stats);





/**
   tests::cw_tq_new_internal()
   tests::cw_tq_delete_internal()
*/
unsigned int test_cw_tq_new_delete_internal(cw_test_stats_t * stats)
{
	/* Arbitrary number of calls to new()/delete() pair. */
	const int max = 40;
	bool failure = false;

	for (int i = 0; i < max; i++) {
		cw_tone_queue_t * tq = cw_tq_new_internal();

		failure = (tq == NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "failed to create new tone queue\n");
			break;
		}

		/* Try to access some fields in cw_tone_queue_t just
		   to be sure that the tq has been allocated properly. */
		failure = (tq->head != 0);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "head in new tone queue is not at zero\n");
			break;
		}

		tq->tail = tq->head + 10;
		failure = (tq->tail != 10);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "tail didn't store correct new value\n");
			break;
		}

		cw_tq_delete_internal(&tq);
		failure = (tq != NULL);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "delete function didn't set the pointer to NULL\n");
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "new/delete:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_get_capacity_internal()
*/
unsigned int test_cw_tq_get_capacity_internal(cw_test_stats_t * stats)
{
	bool failure = false;

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");
	for (size_t i = 10; i < 40; i++) {
		/* This is a silly test, but let's have any test of
		   the getter. */

		tq->capacity = i;
		size_t capacity = cw_tq_get_capacity_internal(tq);
		failure = (capacity != i);

		if (failure) {
			fprintf(out_file, MSG_PREFIX "incorrect capacity: %zu != %zu", capacity, i);
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "get capacity:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_prev_index_internal()
*/
unsigned int test_cw_tq_prev_index_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	struct {
		int arg;
		size_t expected;
		bool guard;
	} input[] = {
		{ tq->capacity - 4, tq->capacity - 5, false },
		{ tq->capacity - 3, tq->capacity - 4, false },
		{ tq->capacity - 2, tq->capacity - 3, false },
		{ tq->capacity - 1, tq->capacity - 2, false },

		/* This one should never happen. We can't pass index
		   equal "capacity" because it's out of range. */
		/*
		{ tq->capacity - 0, tq->capacity - 1, false },
		*/

		{                0, tq->capacity - 1, false },
		{                1,                0, false },
		{                2,                1, false },
		{                3,                2, false },
		{                4,                3, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	bool failure = false;
	while (!input[i].guard) {
		size_t prev = cw_tq_prev_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) prev, input[i].expected);
		failure = (prev != input[i].expected);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "calculated \"prev\" != expected \"prev\": %zu != %zu", prev, input[i].expected);
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "prev index:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_tq_next_index_internal()
*/
unsigned int test_cw_tq_next_index_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	struct {
		int arg;
		size_t expected;
		bool guard;
	} input[] = {
		{ tq->capacity - 5, tq->capacity - 4, false },
		{ tq->capacity - 4, tq->capacity - 3, false },
		{ tq->capacity - 3, tq->capacity - 2, false },
		{ tq->capacity - 2, tq->capacity - 1, false },
		{ tq->capacity - 1,                0, false },
		{                0,                1, false },
		{                1,                2, false },
		{                2,                3, false },
		{                3,                4, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	bool failure = false;
	while (!input[i].guard) {
		size_t next = cw_tq_next_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) next, input[i].expected);
		failure = (next != input[i].expected);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "calculated \"next\" != expected \"next\": %zu != %zu",  next, input[i].expected);
			break;
		}
		i++;
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "next index:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_length_internal()
   tests::cw_get_tone_queue_length()
*/
unsigned int test_cw_tq_length_internal(cw_test_stats_t * stats)
{
	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	bool failure = false;

	for (size_t i = 0; i < tq->capacity; i++) {

		/* This block of code pretends to be enqueue function.
		   The most important functionality of enqueue
		   function is done here manually. We don't do any
		   checks of boundaries of tq, we trust that this is
		   enforced by for loop's conditions. */
		{
			/* Notice that this is *before* enqueueing the tone. */
			cw_assert (tq->len < tq->capacity,
				   "length before enqueue reached capacity: %zu / %zu",
				   tq->len, tq->capacity);

			/* Enqueue the new tone and set the new tail index. */
			tq->queue[tq->tail] = tone;
			tq->tail = cw_tq_next_index_internal(tq, tq->tail);
			tq->len++;

			cw_assert (tq->len <= tq->capacity,
				   "length after enqueue exceeded capacity: %zu / %zu",
				   tq->len, tq->capacity);
		}


		/* OK, added a tone, ready to measure length of the queue. */
		size_t len = cw_tq_length_internal(tq);
		failure = (len != i + 1);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "length: after adding tone #%zu length is incorrect (%zu)\n", i, len);
			break;
		}

		failure = (len != tq->len);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "length: after adding tone #%zu lengths don't match: %zu != %zu", i, len, tq->len);
			break;
		}
	}

	cw_tq_delete_internal(&tq);

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "length:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
  \brief Wrapper for tests of enqueue() and dequeue() function

  First we fill a tone queue when testing enqueue(), and then use the
  tone queue to test dequeue().
*/
unsigned int test_cw_tq_enqueue_dequeue_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tone queue");
	tq->state = CW_TQ_BUSY; /* TODO: why this assignment? */

	/* Fill the tone queue with tones. */
	test_cw_tq_enqueue_internal_1(tq, stats);

	/* Use the same (now filled) tone queue to test dequeue()
	   function. */
	test_cw_tq_dequeue_internal(tq, stats);

	cw_tq_delete_internal(&tq);

	return 0;
}





/**
   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq, cw_test_stats_t * stats)
{
	/* At this point cw_tq_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);
	bool enqueue_failure = false;
	bool length_failure = false;
	bool failure = false;
	int n = 0;

	for (size_t i = 0; i < tq->capacity; i++) {

		/* This tests for potential problems with function call. */
		int rv = cw_tq_enqueue_internal(tq, &tone);
		if (rv != CW_SUCCESS) {
			fprintf(out_file, MSG_PREFIX "enqueue: failed to enqueue tone #%zu/%zu", i, tq->capacity);
			enqueue_failure = true;
			break;
		}

		/* This tests for correctness of working of the 'enqueue' function. */
		size_t len = cw_tq_length_internal(tq);
		if (len != i + 1) {
			fprintf(out_file, MSG_PREFIX "enqueue: incorrect tone queue length: %zu != %zu", len, i + 1);
			length_failure = true;
			break;
		}
	}

	enqueue_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: enqueueing tones to queue:");
	CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

	length_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: length of tq during enqueueing:");
	CW_TEST_PRINT_TEST_RESULT (length_failure, n);



	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	fprintf(out_file, MSG_PREFIX "you may now see \"EE:" MSG_PREFIX "can't enqueue tone, tq is full\" message:\n");
	fflush(out_file);
	int rv = cw_tq_enqueue_internal(tq, &tone);
	failure = rv != CW_FAILURE;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: attempting to enqueue tone to full queue:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	failure = (tq->len != tq->capacity);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "enqueue: length of full queue == capacity (%zd == %zd):", tq->len, tq->capacity);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   tests::cw_tq_dequeue_internal()
*/
unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq, cw_test_stats_t * stats)
{
	/* tq should be completely filled after tests of enqueue()
	   function. */

	/* Test some assertions about full tq, just to be sure. */
	cw_assert (tq->capacity == tq->len,
		   MSG_PREFIX "enqueue: capacity != len of full queue: %zu != %zu",
		   tq->capacity, tq->len);

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);
	int n = 0;

	bool dequeue_failure = false;
	bool length_failure = false;
	bool failure = false;

	for (size_t i = tq->capacity; i > 0; i--) {

		/* Length of tone queue before dequeue. */
		if (i != tq->len) {
			fprintf(out_file, MSG_PREFIX "dequeue: iteration before dequeue doesn't match len: %zu != %zu", i, tq->len);
			length_failure = true;
			break;
		}

		/* This tests for potential problems with function call. */
		int rv = cw_tq_dequeue_internal(tq, &tone);
		if (rv != CW_SUCCESS) {
			fprintf(out_file, MSG_PREFIX "dequeue: can't dequeue tone %zd/%zd", i, tq->capacity);
			dequeue_failure = true;
			break;
		}

		/* Length of tone queue after dequeue. */
		if (i - 1 != tq->len) {
			fprintf(out_file, "libcw_tq: dequeue: iteration after dequeue doesn't match len: %zu != %zu",  i - 1, tq->len);
			length_failure = true;
			break;
		}
	}

	dequeue_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: dequeueing tones from queue:");
	CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

	length_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: length of tq during dequeueing:");
	CW_TEST_PRINT_TEST_RESULT (length_failure, n);



	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call. */
	int rv = cw_tq_dequeue_internal(tq, &tone);
	failure = rv != CW_FAILURE;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: attempting to dequeue tone from empty queue:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This tests for correctness of working of the dequeue()
	   function.  Empty tq should stay empty.

	   At this point cw_tq_length_internal() should be tested, so
	   we can use it to verify correctness of dequeue()
	   function. */
	size_t len = cw_tq_length_internal(tq);
	failure = (len != 0) || (tq->len != 0);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "dequeue: length of empty queue == zero (%zd == %zd):", len, tq->len);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	/* Try removing a tone from empty queue. */
	/* This time we should get CW_TQ_NDEQUEUED_IDLE return value. */
	rv = cw_tq_dequeue_internal(tq, &tone);
	cw_assert (rv == CW_TQ_NDEQUEUED_IDLE, "unexpected return value from \"dequeue\" on empty tone queue: %d", rv);

	CW_TEST_PRINT_TEST_RESULT(failure, n);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_is_full_internal()
   tests::cw_is_tone_queue_full()
*/
unsigned int test_cw_tq_is_full_internal(cw_test_stats_t * stats)
{
	cw_tone_queue_t * tq = cw_tq_new_internal();
	cw_assert (tq, MSG_PREFIX "failed to create new tq");
	tq->state = CW_TQ_BUSY;
	bool failure = true;
	int n = 0;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	/* Notice the "capacity - 1" in loop condition: we leave one
	   place in tq free so that is_full() called in the loop
	   always returns false. */
	for (size_t i = 0; i < tq->capacity - 1; i++) {
		int rv = cw_tq_enqueue_internal(tq, &tone);
		/* The 'enqueue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		failure = (rv != CW_SUCCESS);
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: failed to enqueue tone #%zu", i);
			break;
		}

		bool is_full = cw_tq_is_full_internal(tq);
		failure = is_full;
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: tone queue is full after enqueueing tone #%zu", i);
			break;
		}
	}


	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: 'full' state during enqueueing:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* At this point there is still place in tq for one more
	   tone. Enqueue it and verify that the tq is now full. */
	int rv = cw_tq_enqueue_internal(tq, &tone);
	failure = rv != CW_SUCCESS;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: adding last element:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	bool is_full = cw_tq_is_full_internal(tq);
	failure = !is_full;
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: queue is full after adding last element:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* Now test the function as we dequeue tones. */

	for (size_t i = tq->capacity; i > 0; i--) {
		/* The 'dequeue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		failure = (CW_FAILURE == cw_tq_dequeue_internal(tq, &tone));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: failed to dequeue tone %zd\n", i);
			break;
		}

		/* Here is the proper test of tested function. */
		failure = (true == cw_tq_is_full_internal(tq));
		if (failure) {
			fprintf(out_file, MSG_PREFIX "is_full: queue is full after dequeueing tone %zd\n", i);
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "is_full: 'full' state during dequeueing:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	cw_tq_delete_internal(&tq);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   Just like in test_cw_tq_test_capacity_2(), enqueueing is done with
   cw_tq_enqueue_internal().

   Unlike test_cw_tq_test_capacity_2(), this function dequeues tones
   using "manual" method.

   After every dequeue we check that dequeued tone is the one that we
   were expecting to get.

   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_test_capacity_1(cw_test_stats_t * stats)
{
	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	size_t capacity = 30;
	size_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1".

	   TODO: allow negative head shifts in the test. */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		bool enqueue_failure = true;
		bool dequeue_failure = true;

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t * tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (size_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			int rv = cw_tq_enqueue_internal(tq, &tone);
			enqueue_failure = (rv != CW_SUCCESS);
			if (enqueue_failure) {
				fprintf(out_file, MSG_PREFIX "capacity1: failed to enqueue tone #%zu", i);
				break;
			}
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected. Let's do the readback N times, just for
		   fun. Every time the results should be the same. */

		for (int l = 0; l < 3; l++) {
			for (size_t i = 0; i < tq->capacity; i++) {

				/* When shift of head == 0, tone with
				   frequency 'i' is at index 'i'. But with
				   non-zero shift of head, tone with frequency
				   'i' is at index 'shifted_i'. */


				size_t shifted_i = (i + head_shifts[s]) % (tq->capacity);
				// fprintf(stderr, "Readback %d: position %zu: checking tone %zu, expected %zu, got %d\n",
				// 	l, shifted_i, i, i, tq->queue[shifted_i].frequency);

				/* This is the "manual" dequeue. We
				   don't really remove the tone from
				   tq, just checking that tone at
				   shifted_i has correct, expected
				   properties. */
				dequeue_failure = tq->queue[shifted_i].frequency != (int) i;
				if (dequeue_failure) {
					fprintf(out_file, MSG_PREFIX "capacity1: frequency of dequeued tone is incorrect: %d != %d", tq->queue[shifted_i].frequency, (int) i);
					break;
				}
			}
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);

		enqueue_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "capacity1: enqueue @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

		dequeue_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity1: dequeue @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

		s++;
	}

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   Just like in test_cw_tq_test_capacity_1(), enqueueing is done with
   cw_tq_enqueue_internal().

   Unlike test_cw_tq_test_capacity_1(), this function dequeues tones
   using cw_tq_dequeue_internal().

   After every dequeue we check that dequeued tone is the one that we
   were expecting to get.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_dequeue_internal()
*/
unsigned int test_cw_tq_test_capacity_2(cw_test_stats_t * stats)
{
	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	size_t capacity = 30;
	size_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1".

	   TODO: allow negative head shifts in the test. */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		bool enqueue_failure = true;
		bool dequeue_failure = true;
		bool capacity_failure = true;

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t *tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (size_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			int rv = cw_tq_enqueue_internal(tq, &tone);
			enqueue_failure = (rv != CW_SUCCESS);
			if (enqueue_failure) {
				fprintf(out_file, MSG_PREFIX "capacity2: failed to enqueue tone #%zu", i);
				break;
			}
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected.

		   In test_cw_tq_test_capacity_1() we did the
		   readback "manually", this time let's use "dequeue"
		   function to do the job.

		   Since the "dequeue" function moves queue pointers,
		   we can do this test only once (we can't repeat the
		   readback N times with calls to dequeue() expecting
		   the same results). */

		size_t i = 0;
		cw_tone_t tone; /* For output only, so no need to initialize. */

		int rv = 0;
		while ((rv = cw_tq_dequeue_internal(tq, &tone))
		       && rv == CW_TQ_DEQUEUED) {

			size_t shifted = (i + head_shifts[s]) % (tq->capacity);

			cw_assert (tq->queue[shifted].frequency == (int) i,
				   "position %zd: checking tone %zd, expected %zd, got %d\n",
				   shifted, i, i, tq->queue[shifted].frequency);

			i++;
		}

		capacity_failure = (i != tq->capacity);
		if (capacity_failure) {
			fprintf(out_file, MSG_PREFIX "capacity2: number of dequeues (%zu) is different than capacity (%zu)\n", i, tq->capacity);
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);


		enqueue_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "capacity2: enqueue  @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (enqueue_failure, n);

		dequeue_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity2: dequeue  @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (dequeue_failure, n);

		capacity_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "capacity2: capacity @ shift=%d:", head_shifts[s]);
		CW_TEST_PRINT_TEST_RESULT (capacity_failure, n);


		s++;
	}

	return 0;
}





/**
   \brief Create and initialize tone queue for tests of capacity

   Create new tone queue for tests using three given parameters: \p
   capacity, \p high_water_mark, \p head_shift. The function is used
   to create a new tone queue in tests of "capacity" parameter of a
   tone queue.

   First two function parameters are rather boring. What is
   interesting is the third parameter: \p head_shift.

   In general the behaviour of tone queue (a circular list) should be
   independent of initial position of queue's head (i.e. from which
   position in the queue we start adding new elements to the queue).

   By initializing the queue with different initial positions of head
   pointer, we can test this assertion about irrelevance of initial
   head position.

   The "initialize" word may be misleading. The function does not
   enqueue any tones, it just initializes (resets) every slot in queue
   to non-random value.

   Returned pointer is owned by caller.

   tests::cw_tq_set_capacity_internal()

   \param capacity - intended capacity of tone queue
   \param high_water_mark - high water mark to be set for tone queue
   \param head_shift - position of first element that will be inserted in empty queue

   \return newly allocated and initialized tone queue
*/
cw_tone_queue_t *test_cw_tq_capacity_test_init(size_t capacity, size_t high_water_mark, int head_shift)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");
	tq->state = CW_TQ_BUSY;

	int rv = cw_tq_set_capacity_internal(tq, capacity, high_water_mark);
	cw_assert (rv == CW_SUCCESS, "failed to set capacity/high water mark");
	cw_assert (tq->capacity == capacity, "incorrect capacity: %zu != %zu", tq->capacity, capacity);
	cw_assert (tq->high_water_mark == high_water_mark, "incorrect high water mark: %zu != %zu", tq->high_water_mark, high_water_mark);

	/* Initialize *all* tones with known value. Do this manually,
	   to be 100% sure that all tones in queue table have been
	   initialized. */
	for (int i = 0; i < CW_TONE_QUEUE_CAPACITY_MAX; i++) {
		CW_TONE_INIT(&(tq->queue[i]), 10000 + i, 1, CW_SLOPE_MODE_STANDARD_SLOPES);
	}

	/* Move head and tail of empty queue to initial position. The
	   queue is empty - the initialization of fields done above is not
	   considered as real enqueueing of valid tones. */
	tq->tail = head_shift;
	tq->head = tq->tail;
	tq->len = 0;

	/* TODO: why do this here? */
	tq->state = CW_TQ_BUSY;

	return tq;
}





/**
   \brief Test the limits of the parameters to the tone queue routine

   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_2(void)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create a tone queue\n");
	cw_tone_t tone;


	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);


	/* Test 1: invalid length of tone. */
	errno = 0;
	tone.len = -1;            /* Invalid length. */
	tone.frequency = f_min;   /* Valid frequency. */
	int status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with invalid length.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 2: tone's frequency too low. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_min - 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too low frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 3: tone's frequency too high. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_max + 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too high frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));

	cw_tq_delete_internal(&tq);
	cw_assert (!tq, "tone queue not deleted properly\n");

	int n = printf(MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (false, n);

	return 0;
}





/**
   This function creates a generator that internally uses a tone
   queue. The generator is needed to perform automatic dequeueing
   operations, so that cw_tq_wait_for_level_internal() can detect
   expected level.

  tests::cw_tq_wait_for_level_internal()
*/
unsigned int test_cw_tq_wait_for_level_internal(cw_test_stats_t * stats)
{
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 20, 10000, CW_SLOPE_MODE_STANDARD_SLOPES);

	for (int i = 0; i < 10; i++) {
		cw_gen_t * gen = cw_gen_new(CW_AUDIO_NULL, CW_DEFAULT_NULL_DEVICE);
		cw_assert (gen, "failed to create a tone queue\n");
		cw_gen_start(gen);

		/* Test the function for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			int rv = cw_tq_enqueue_internal(gen->tq, &tone);
			cw_assert (rv, MSG_PREFIX "wait for level: failed to enqueue tone #%d", j);
		}

		int rv = cw_tq_wait_for_level_internal(gen->tq, level);
		bool wait_failure = (rv != CW_SUCCESS);
		if (wait_failure) {
			fprintf(out_file, MSG_PREFIX "wait failed for level = %d", level);
		}

		size_t len = cw_tq_length_internal(gen->tq);

		/* cw_tq_length_internal() is called after return of
		   tested function, so 'len' can be smaller by one,
		   but never larger, than 'level'.

		   During initial tests, for function implemented with
		   signals and with alternative IPC, diff was always
		   zero on my primary Linux box. */
		int diff = level - len;
		bool diff_failure = (abs(diff) > 1);
		if (diff_failure) {
			fprintf(out_file, MSG_PREFIX "wait for level: difference is too large: level = %d, len = %zu, diff = %d\n", level, len, diff);
		}

		fprintf(stderr, "          level = %d, len = %zu, diff = %d\n", level, len, diff);

		cw_gen_stop(gen);
		cw_gen_delete(&gen);

		wait_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "wait for level: wait @ level=%d:", level);
		CW_TEST_PRINT_TEST_RESULT (wait_failure, n);

		diff_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "wait for level: diff @ level=%d:", level);
		CW_TEST_PRINT_TEST_RESULT (diff_failure, n);
	}


	return 0;
}




/**
   \brief Simple tests of queueing and dequeueing of tones

   Ensure we can generate a few simple tones, and wait for them to end.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_length_internal()
   tests::cw_wait_for_tone()
   tests::cw_tq_wait_for_level_internal()
*/
unsigned int test_cw_tq_operations_1(cw_gen_t * gen, cw_test_stats_t * stats)
{
	int l = 0;         /* Measured length of tone queue. */
	int expected = 0;  /* Expected length of tone queue. */

	int f_min, f_max;

	cw_gen_set_volume(gen, 70);
	cw_get_frequency_limits(&f_min, &f_max);

	int N = 6;              /* Number of test tones put in queue. */
	int duration = 100000;  /* Duration of tone. */
	int delta_f = ((f_max - f_min) / (N - 1));      /* Delta of frequency in loops. */


	/* Test 1: enqueue N tones, and wait for each of them
	   separately. Control length of tone queue in the process. */

	/* Enqueue first tone. Don't check queue length yet.

	   The first tone is being dequeued right after enqueueing, so
	   checking the queue length would yield incorrect result.
	   Instead, enqueue the first tone, and during the process of
	   dequeueing it, enqueue rest of the tones in the loop,
	   together with checking length of the tone queue. */
	int f = f_min;
	cw_tone_t tone;
	CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
	bool failure = !cw_tq_enqueue_internal(gen->tq, &tone);
	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This is to make sure that rest of tones is enqueued when
	   the first tone is being dequeued. */
	usleep(duration / 4);

	/* Enqueue rest of N tones. It is now safe to check length of
	   tone queue before and after queueing each tone: length of
	   the tone queue should increase (there won't be any decrease
	   due to dequeueing of first tone). */
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is filled - before
		   adding a new tone. */
		l = cw_tq_length_internal(gen->tq);
		expected = (i - 1);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): pre (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): pre-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a tone to queue. All frequencies should be
		   within allowed range, so there should be no
		   error. */
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		failure = !cw_tq_enqueue_internal(gen->tq, &tone);

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is filled - after
		   adding a new tone. */
		l = cw_tq_length_internal(gen->tq);
		expected = (i - 1) + 1;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): post (#%02d):", i);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Above we have queued N tones. libcw starts dequeueing first
	   of them before the last one is enqueued. This is why below
	   we should only check for N-1 of them. Additionally, let's
	   wait a moment till dequeueing of the first tone is without
	   a question in progress. */

	usleep(duration / 4);

	/* And this is the proper test - waiting for dequeueing tones. */
	for (int i = 1; i < N; i++) {
#if 0
		/* Monitor length of a queue as it is emptied - before dequeueing. */
		l = cw_tq_length_internal(gen->tq);
		expected = N - i;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "cw_tq_length_internal(): pre (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): pre-dequeue:  expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Wait for each of N tones to be dequeued. */
		failure = !cw_wait_for_tone();

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw: cw_wait_for_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Monitor length of a queue as it is emptied - after dequeueing. */
		l = cw_tq_length_internal();
		expected = N - i - 1;
		//printf("libcw: cw_tq_length_internal(): post: l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_tq_length_internal(): post (#%02d):", i);
		// n = printf("libcw: cw_tq_length_internal(): post-dequeue: expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
#endif
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	failure = false;
	f = 0;
	for (int i = 0; i < N; i++) {
		f = f_min + i * delta_f;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			failure = true;
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal(%08d, %04d):", duration, f);
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	failure = !cw_tq_wait_for_level_internal(gen->tq, 0);

	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}




/**
   Run the complete range of tone generation, at 100Hz intervals,
   first up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_wait_for_level_internal()
*/
unsigned int test_cw_tq_operations_2(cw_gen_t * gen, cw_test_stats_t * stats)
{
	cw_gen_set_volume(gen, 70);
	int duration = 40000;

	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);

	bool wait_failure = false;
	bool queue_failure = false;

	for (int f = f_min; f < f_max; f += 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			if (!cw_tq_wait_for_level_internal(gen->tq, 0)) {
				wait_failure = true;
				break;
			}
		}

		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			queue_failure = true;
			break;
		}
	}

	for (int f = f_max; f > f_min; f -= 100) {
		while (cw_tq_is_full_internal(gen->tq)) {
			if (!cw_tq_wait_for_level_internal(gen->tq, 0)) {
				wait_failure = true;
				break;
			}
		}
		cw_tone_t tone;
		CW_TONE_INIT(&tone, f, duration, CW_SLOPE_MODE_NO_SLOPES);
		if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
			queue_failure = true;
			break;
		}
	}


	queue_failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, MSG_PREFIX "cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (queue_failure, n);


	wait_failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal(A):");
	CW_TEST_PRINT_TEST_RESULT (wait_failure, n);


	n = fprintf(out_file, MSG_PREFIX "cw_tq_wait_for_level_internal(B):");
	fflush(out_file);
	bool wait_tq_failure = !cw_tq_wait_for_level_internal(gen->tq, 0);
	wait_tq_failure ? stats->failures++ : stats->successes++;
	CW_TEST_PRINT_TEST_RESULT (wait_tq_failure, n);


	/* Silence the generator before next test. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 100, CW_SLOPE_MODE_NO_SLOPES);
	cw_tq_enqueue_internal(gen->tq, &tone);
	cw_tq_wait_for_level_internal(gen->tq, 0);

	return 0;
}





/**
   Test the tone queue manipulations, ensuring that we can fill the
   queue, that it looks full when it is, and that we can flush it all
   again afterwards, and recover.

   tests::cw_tq_get_capacity_internal()
   tests::cw_tq_length_internal()
   tests::cw_tq_enqueue_internal()
   tests::cw_tq_wait_for_level_internal()
*/
unsigned int test_cw_tq_operations_3(cw_gen_t * gen, cw_test_stats_t * stats)
{
	/* Small setup. */
	cw_gen_set_volume(gen, 70);



	/* Test: properties (capacity and length) of empty tq. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "empty queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_empty = cw_tq_length_internal(gen->tq);
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "empty queue's length: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: properties (capacity and length) of full tq. */

	/* FIXME: we call cw_tq_enqueue_internal() until tq is full, and then
	   expect the tq to be full while we perform tests. Doesn't
	   the tq start dequeuing tones right away? Can we expect the
	   tq to be full for some time after adding last tone?
	   Hint: check when a length of tq is decreased. Probably
	   after playing first tone on tq, which - in this test - is
	   pretty long. Or perhaps not. */
	{
		int i = 0;
		/* FIXME: cw_tq_is_full_internal() is not tested */
		while (!cw_tq_is_full_internal(gen->tq)) {
			cw_tone_t tone;
			int f = 5; /* I don't want to hear the tone during tests. */
			CW_TONE_INIT(&tone, f + (i++ & 1) * f, 1000000, CW_SLOPE_MODE_NO_SLOPES);
			cw_tq_enqueue_internal(gen->tq, &tone);
		}


		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "full queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_full = cw_tq_length_internal(gen->tq);
		failure = len_full != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "full queue's length: %d %s %d:",
			    len_full, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: attempt to add tone to full queue. */
	{
		fprintf(out_file, MSG_PREFIX "you may now see \"EE:" MSG_PREFIX "can't enqueue tone, tq is full\" message:\n");
		fflush(out_file);

		cw_tone_t tone;
		CW_TONE_INIT(&tone, 100, 1000000, CW_SLOPE_MODE_NO_SLOPES);
		errno = 0;
		int status = cw_tq_enqueue_internal(gen->tq, &tone);
		bool failure = status || errno != EAGAIN;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "trying to enqueue tone to full queue:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: check again properties (capacity and length) of empty
	   tq after it has been in use.

	   Empty the tq, ensure that it is empty, and do the test. */
	{
		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_tq_flush_internal(gen->tq);
		cw_tq_wait_for_level_internal(gen->tq, 0);

		int capacity = cw_tq_get_capacity_internal(gen->tq);
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(out_file, MSG_PREFIX "empty queue's capacity: %d %s %d:",
				capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test that the tq is really empty after
		   cw_tq_wait_for_level_internal() has returned. */
		int len_empty = cw_tq_length_internal(gen->tq);
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, MSG_PREFIX "empty queue's length: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	return 0;
}




static void cw_test_helper_tq_callback(void * data);
static size_t cw_test_tone_queue_callback_data = 999999;
static int cw_test_helper_tq_callback_capture = false;

struct cw_test_struct{
	cw_gen_t *gen;
	size_t *data;
};


/**
   tests::cw_register_tone_queue_low_callback()
*/
unsigned int test_cw_tq_callback(cw_gen_t *gen, cw_test_stats_t *stats)
{
	struct cw_test_struct s;
	s.gen = gen;
	s.data = &cw_test_tone_queue_callback_data;

	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 3 * i;

		int rv = cw_tq_register_low_level_callback_internal(gen->tq, cw_test_helper_tq_callback, (void *) &s, level);
		bool failure = rv == CW_FAILURE;
		sleep(1);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_register_tone_queue_low_callback(): threshold = %d:", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a lot of tones to tone queue. "a lot" means two
		   times more than a value of trigger level. */
		for (int j = 0; j < 2 * level; j++) {
			rv = cw_gen_enqueue_character_partial(gen, 'e');
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
		cw_tq_wait_for_level_internal(gen->tq, 0);

		/* Because of order of calling callback and decreasing
		   length of queue, I think that it's safe to assume
		   that there may be a difference of 1 between these
		   two values. */
		int diff = level - cw_test_tone_queue_callback_data;
		failure = abs(diff) > 1;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: tone queue callback:           level at callback = %zd:", cw_test_tone_queue_callback_data);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_tq_flush_internal(gen->tq);
	}

	return 0;
}





static void cw_test_helper_tq_callback(void *data)
{
	if (cw_test_helper_tq_callback_capture) {
		struct cw_test_struct *s = (struct cw_test_struct *) data;

		*(s->data) = cw_tq_length_internal(s->gen->tq);

		cw_test_helper_tq_callback_capture = false;
		fprintf(stderr, MSG_PREFIX "cw_test_helper_tq_callback:    captured level = %zd\n", *(s->data));
	}

	return;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
