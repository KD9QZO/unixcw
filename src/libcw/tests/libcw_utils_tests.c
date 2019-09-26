#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>




#include "tests/libcw_test_framework.h"
#include "libcw_utils.h"
#include "libcw_utils_tests.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_key.h"
#include "libcw.h"
#include "libcw2.h"




#define MSG_PREFIX "libcw/utils: "




/**
   tests::cw_timestamp_compare_internal()
*/
unsigned int test_cw_timestamp_compare_internal(cw_test_stats_t * stats)
{
	struct timeval earlier_timestamp;
	struct timeval later_timestamp;

	/* TODO: I think that there may be more tests to perform for
	   the function, testing handling of overflow. */

	int expected_deltas[] = { 0,
				  1,
				  1001,
				  CW_USECS_PER_SEC - 1,
				  CW_USECS_PER_SEC,
				  CW_USECS_PER_SEC + 1,
				  2 * CW_USECS_PER_SEC - 1,
				  2 * CW_USECS_PER_SEC,
				  2 * CW_USECS_PER_SEC + 1,
				  -1 }; /* Guard. */


	earlier_timestamp.tv_sec = 3;
	earlier_timestamp.tv_usec = 567;

	bool failure = true;

	int i = 0;
	while (expected_deltas[i] != -1) {

		later_timestamp.tv_sec = earlier_timestamp.tv_sec + (expected_deltas[i] / CW_USECS_PER_SEC);
		later_timestamp.tv_usec = earlier_timestamp.tv_usec + (expected_deltas[i] % CW_USECS_PER_SEC);

		int delta = cw_timestamp_compare_internal(&earlier_timestamp, &later_timestamp);
		failure = (delta != expected_deltas[i]);
		if (failure) {
			fprintf(out_file, "libcw:utils:compare timestamp: test #%d: unexpected delta: %d != %d\n", i, delta, expected_deltas[i]);
			break;
		}

		i++;
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:compare timestamp:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}





/**
   tests::cw_timestamp_validate_internal()
*/
unsigned int test_cw_timestamp_validate_internal(cw_test_stats_t * stats)
{
	struct timeval out_timestamp;
	struct timeval in_timestamp;
	struct timeval ref_timestamp; /* Reference timestamp. */
	int rv = 0;

	bool failure = true;
	int n = 0;




	/* Test 1 - get current time. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;

	cw_assert (!gettimeofday(&ref_timestamp, NULL), "libcw:utils:validate timestamp 1: failed to get reference time");

	rv = cw_timestamp_validate_internal(&out_timestamp, NULL);
	failure = (CW_SUCCESS != rv);
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:current timestamp:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

#if 0
	fprintf(stderr, "\nINFO: delay in getting timestamp is %d microseconds\n",
		cw_timestamp_compare_internal(&ref_timestamp, &out_timestamp));
#endif



	/* Test 2 - validate valid input timestamp and copy it to
	   output timestamp. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 1234;
	in_timestamp.tv_usec = 987;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (CW_SUCCESS != rv)
		|| (out_timestamp.tv_sec != in_timestamp.tv_sec)
		|| (out_timestamp.tv_usec != in_timestamp.tv_usec);

	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:validate and copy:"
			"rv = %d,"
			"%d / %d,"
			"%d / %d",
			rv,
			(int) out_timestamp.tv_sec, (int) in_timestamp.tv_sec,
			(int) out_timestamp.tv_usec, (int) in_timestamp.tv_usec);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:validate and copy:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 3 - detect invalid seconds in input timestamp. */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = -1;
	in_timestamp.tv_usec = 987;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS) || (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:invalid seconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:invalid seconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 4 - detect invalid microseconds in input timestamp (microseconds too large). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = CW_USECS_PER_SEC + 1;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS)
		|| (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:invalid milliseconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:invalid milliseconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);




	/* Test 5 - detect invalid microseconds in input timestamp (microseconds negative). */
	out_timestamp.tv_sec = 0;
	out_timestamp.tv_usec = 0;
	in_timestamp.tv_sec = 123;
	in_timestamp.tv_usec = -1;

	rv = cw_timestamp_validate_internal(&out_timestamp, &in_timestamp);
	failure = (rv == CW_SUCCESS) || (errno != EINVAL);
	if (failure) {
		fprintf(out_file, "libcw:utils:validate timestamp:negative milliseconds: rv==CW_FAILURE = %d, errno==EINVAL = %d\n", rv == CW_FAILURE, errno == EINVAL);
	}
	failure ? stats->failures++ : stats->successes++;
	n = fprintf(out_file, "libcw:utils:validate timestamp:negative milliseconds:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	return 0;
}





/**
   tests::cw_usecs_to_timespec_internal()
*/
unsigned int test_cw_usecs_to_timespec_internal(cw_test_stats_t * stats)
{
	struct {
		int input;
		struct timespec t;
	} input_data[] = {
		/* input in ms    /   expected output seconds : milliseconds */
		{           0,    {   0,             0 }},
		{     1000000,    {   1,             0 }},
		{     1000004,    {   1,          4000 }},
		{    15000350,    {  15,        350000 }},
		{          73,    {   0,         73000 }},
		{          -1,    {   0,             0 }},
	};

	bool failure = true;

	int i = 0;
	while (input_data[i].input != -1) {
		struct timespec result = { .tv_sec = 0, .tv_nsec = 0 };
		cw_usecs_to_timespec_internal(&result, input_data[i].input);
#if 0
		fprintf(stderr, "input = %d usecs, output = %ld.%ld\n",
			input_data[i].input, (long) result.tv_sec, (long) result.tv_nsec);
#endif
		failure = (result.tv_sec != input_data[i].t.tv_sec);
		if (failure) {
			fprintf(out_file, "libcw:utils:usecs to timespec: test %d: %ld [s] != %ld [s]\n", i, result.tv_sec, input_data[i].t.tv_sec);
			break;
		}

		failure = (result.tv_nsec != input_data[i].t.tv_nsec);
		if (failure) {
			fprintf(out_file, "libcw:utils:usecs to timespec: test %d: %ld [ns] != %ld [ns]\n", i, result.tv_nsec, input_data[i].t.tv_nsec);
			break;
		}

		i++;
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:usecs to timespec:");
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}






/**
   tests::cw_version()
*/
unsigned int test_cw_version_internal(cw_test_stats_t * stats)
{
	int current = 77, revision = 88, age = 99; /* Dummy values. */
	cw_get_lib_version(&current, &revision, &age);

	/* Library's version is defined in LIBCW_VERSION. cw_version()
	   uses three calls to strtol() to get three parts of the
	   library version.

	   Let's use a different approach to convert LIBCW_VERSION
	   into numbers. */

#define VERSION_LEN_MAX 30
	cw_assert (strlen(LIBCW_VERSION) <= VERSION_LEN_MAX, "LIBCW_VERSION longer than expected!\n");

	char buffer[VERSION_LEN_MAX + 1];
	strncpy(buffer, LIBCW_VERSION, VERSION_LEN_MAX);
	buffer[VERSION_LEN_MAX] = '\0';
#undef VERSION_LEN_MAX

	char *str = buffer;
	int c = 0, r = 0, a = 0;

	bool failure = true;

	for (int i = 0; ; i++, str = NULL) {

		char *token = strtok(str, ":");
		if (token == NULL) {
			failure = (i != 3);
			if (failure) {
				fprintf(out_file, "libcw:utils:version: stopping at token %d\n", i);
			}
			break;
		}

		if (i == 0) {
			c = atoi(token);
		} else if (i == 1) {
			r = atoi(token);
		} else if (i == 2) {
			a = atoi(token);
		} else {
			failure = true;
			fprintf(out_file, "libcw:utils:version: too many tokens in \"%s\"\n", LIBCW_VERSION);
		}
	}


	failure = (current != c) || (revision != r) || (age != a);
	if (failure) {
		fprintf(out_file, "libcw:utils:version: current: %d / %d; revision: %d / %d; age: %d / %d\n", current, c, revision, r, age, a);
	}

	failure ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:version: %d:%d:%d:", c, r, a);
	CW_TEST_PRINT_TEST_RESULT (failure, n);

	return 0;
}





/**
   tests::cw_license()
*/
unsigned int test_cw_license_internal(cw_test_stats_t * stats)
{
	/* Well, there isn't much to test here. The function just
	   prints the license to stdout, and that's it. */

	cw_license();

	false ? stats->failures++ : stats->successes++;
	int n = fprintf(out_file, "libcw:utils:license:");
	CW_TEST_PRINT_TEST_RESULT (false, n);


	return 0;
}





/**
   \brief Ensure that we can obtain correct values of main parameter limits

   tests::cw_get_speed_limits()
   tests::cw_get_frequency_limits()
   tests::cw_get_volume_limits()
   tests::cw_get_gap_limits()
   tests::cw_get_tolerance_limits()
   tests::cw_get_weighting_limits()
*/
unsigned int test_cw_get_x_limits_internal(cw_test_stats_t * stats)
{
	struct {
		void (* getter)(int *min, int *max);
		int min;     /* Minimum hardwired in library. */
		int max;     /* Maximum hardwired in library. */
		int get_min; /* Minimum received in function call. */
		int get_max; /* Maximum received in function call. */

		const char *name;
	} test_data[] = {
		/*                                                                initial values                */
		{ cw_get_speed_limits,      CW_SPEED_MIN,      CW_SPEED_MAX,      10000,  -10000,  "speed"     },
		{ cw_get_frequency_limits,  CW_FREQUENCY_MIN,  CW_FREQUENCY_MAX,  10000,  -10000,  "frequency" },
		{ cw_get_volume_limits,     CW_VOLUME_MIN,     CW_VOLUME_MAX,     10000,  -10000,  "volume"    },
		{ cw_get_gap_limits,        CW_GAP_MIN,        CW_GAP_MAX,        10000,  -10000,  "gap"       },
		{ cw_get_tolerance_limits,  CW_TOLERANCE_MIN,  CW_TOLERANCE_MAX,  10000,  -10000,  "tolerance" },
		{ cw_get_weighting_limits,  CW_WEIGHTING_MIN,  CW_WEIGHTING_MAX,  10000,  -10000,  "weighting" },
		{ NULL,                     0,                 0,                      0,      0,  NULL        }

	};

	for (int i = 0; test_data[i].getter; i++) {

		bool min_failure = true;
		bool max_failure = true;
		int n = 0;

		/* Get limits of a parameter. */
		test_data[i].getter(&test_data[i].get_min, &test_data[i].get_max);

		/* Test that limits are as expected (values received
		   by function call match those defined in library's
		   header file). */
		min_failure = (test_data[i].get_min != test_data[i].min);
		if (min_failure) {
			fprintf(out_file, "libcw:utils:limits: failed to get correct minimum of %s\n", test_data[i].name);
		}

		max_failure = (test_data[i].get_max != test_data[i].max);
		if (max_failure) {
			fprintf(out_file, "libcw:utils:limits: failed to get correct maximum of %s\n", test_data[i].name);
		}

		min_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:utils:get min %s:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (min_failure, n);

		max_failure ? stats->failures++ : stats->successes++;
		n = fprintf(out_file, "libcw:utils:get max %s:", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (max_failure, n);
	}


	return 0;
}
