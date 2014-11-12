/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include <stdbool.h>
#include <sys/time.h>

#include "libcw_gen.h"





typedef struct cw_key_struct {
	/* Straight key and iambic keyer needs a generator to produce
	   a sound on "Key Down" events. Maybe we don't always need a
	   sound, but sometimes we do want to have it.

	   Additionally iambic keyer needs a generator for timing
	   purposes.

	   In any case - a key needs to have access to a generator
	   (but a generator doesn't need a key). This is why the key
	   data type has a "generator" field, not the other way
	   around. */
	cw_gen_t *gen;


	/* External "on key state change" callback function and its
	   argument.

	   It may be useful for a client to have this library control
	   an external keying device, for example, an oscillator, or a
	   transmitter.  Here is where we keep the address of a
	   function that is passed to us for this purpose, and a void*
	   argument for it. */
	void (*key_callback)(void*, int);
	void *key_callback_arg;


	/* Straight key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} sk;


	/* Iambic keyer.  The keyer functions maintain the current
	   known state of the paddles, and latch false-to-true
	   transitions while busy, to form the iambic effect.  For
	   Curtis mode B, the keyer also latches any point where both
	   paddle states are true at the same time. */
	struct {
		int graph_state;       /* State of iambic keyer state machine. */
		int key_value;         /* Open/Closed, Space/Mark, NoSound/Sound. */

		bool dot_paddle;       /* Dot paddle state */
		bool dash_paddle;      /* Dash paddle state */

		bool dot_latch;        /* Dot false->true latch */
		bool dash_latch;       /* Dash false->true latch */

		/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
		   differ slightly, and some people have a preference for one or the other.
		   Mode A is a bit less timing-critical, so we'll make that the default. */
		bool curtis_mode_b;

		bool curtis_b_latch;   /* Curtis Dot&Dash latch */

		bool lock;             /* FIXME: describe why we need this flag. */

		struct timeval *timer; /* Timer for receiving of iambic keying, owned by client code. */

		/* Generator associated with the keyer. Should never
		   be NULL as iambic keyer *needs* a generator to
		   function properly (and to generate audible tones).
		   Set using
		   cw_key_register_generator_internal(). */
		/* No separate generator, use cw_key_t->gen. */
	} ik;


	/* Tone-queue key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} tk;
} cw_key_t;





int  cw_key_ik_update_graph_state_internal(volatile cw_key_t *keyer);
void cw_key_ik_increment_timer_internal(volatile cw_key_t *keyer, int usecs);

void cw_key_tk_set_value_internal(volatile cw_key_t *key, int key_state);

void cw_key_register_generator_internal(volatile cw_key_t *key, cw_gen_t *gen);





#endif // #ifndef H_LIBCW_KEY
