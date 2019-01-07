/*
 * remote.c
 *
 * Copyright (C) 2019 <sgerwk@aol.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * parse audio data as a remote protocol
 *
 * remote [-f] [-l] [-i] [-d n] (file|dev) -- [amplify_factor [trigger_bound]]
 *	-f	input is a sequence of numbers in ascii, one per line,
 *		instead of an AU file
 *	-c	allow receiving the output of irblast
 *	-l	log input to log.au or log.txt
 *	-d n	debug protocol n, from 1 to 14 so far
 *	amplify_factor
 *		-1 to invert, default 1
 *	trigger_bound
 *		the default is to use the background noise canceler,
 *		which requires a bit of signal absence at the beginning
 *		and derives the bound from it
 *	file|dev
 *		input is read from file if it exists, otherwise from audio
 *		device dev (list available: arecord -L)
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include "microphone.h"
#include "filters.h"
#include "protocols.h"

/*
 * filter debugging: stop the chain of filters at some point and print
 */
#define STOPHERE				\
	printf("%d\n", value);			\
	if (status.flush)			\
		fflush(stdout);			\
	continue;

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	char *filename, *logfile = NULL;
	int debug, ascii, valleyfilter;
	int bound;
	double factor;
	struct status status;
	void *read, *microphone, *log;
	void *valley, *diff, *amplify, *maximal, *stabilize;
	void *background, *trigger, *runlength;
	int value;
	struct protocols_status *protocols_status;
	struct key *key;

					/* arguments */

	ascii = 0;
	valleyfilter = 0;
	debug = 0;
	while (-1 != (opt = getopt(argc, argv, "fcld:")))
		switch (opt) {
		case 'l':
			logfile = "log.au";
			break;
		case 'f':
			ascii = 1;
			break;
		case 'c':
			valleyfilter = 1;
			break;
		case 'd':
			debug = atoi(optarg);
			break;
		}
	if (ascii && logfile)
		logfile = "log.txt";

	argc -= optind - 1;
	argv += optind - 1;
	if (argc - 1 < 1)
		filename = "default";
	else
		filename = argv[1];
	factor = argc - 1 >= 2 ? atof(argv[2]) : 1;
	bound = argc - 1 >= 3 ? atoi(argv[3]) : -1;

					/* init filters and protocols */

	read =             read_init(filename, ascii, &status);
	if (read != NULL)
		microphone = NULL;
	else {
		microphone = microphone_init(filename, &status);
		if (microphone == NULL) {
			printf("cannot open input file\n");
			exit(EXIT_FAILURE);
		}
	}
	log =               log_init(logfile, ascii, &status);
	valley =         valley_init(10, &status);
	diff =             diff_init(&status);
	amplify =       amplify_init(factor, &status);
	maximal =       maximal_init(11, &status);
	stabilize =   stabilize_init(&status);
	trigger =       trigger_init(bound, &status);
	background = background_init(&status);
	runlength =   runlength_init(&status);

	protocols_status = protocols_init(debug);
	
					/* process values */

	while (! status.ended) {

		// filter testing: STOPHERE to cut the pipe of filters short

		value = 0;
		if (read)
			FILTER_VALUE(read, value, read, &status)
		if (microphone)
			FILTER_VALUE(microphone, value, microphone, &status)
		FILTER_VALUE(log, value, log, &status)
		if (valleyfilter)
			FILTER_VALUE(valley, value, valley, &status);
		FILTER_VALUE(diff, value, diff, &status)
		FILTER_VALUE(amplify, value, amplify, &status)
		FILTER_VALUE(stabilize, value, stabilize, &status)
		FILTER_VALUE(maximal, value, maximal, &status)
		if (bound == -1)
			FILTER_VALUE(background, value, background, &status)
		else
			FILTER_VALUE(trigger, value, trigger, &status)
		FILTER_VALUE(runlength, value, runlength, &status)

		if (! debug) {
			printf("*");
			fflush(stdout);
		}

		key = protocols_value(value, protocols_status);
		if (key) {
			printf("\n");
			printkey(key);
			printf("\n");
		}
	}

					/* finish filters */

	if (read)
		read_end(read, &status);
	if (microphone)
		microphone_end(microphone, &status);
	log_end(log, &status);
	valley_end(valley, &status);
	diff_end(diff, &status);
	amplify_end(amplify, &status);
	stabilize_end(stabilize, &status);
	maximal_end(maximal, &status);
	trigger_end(trigger, &status);
	background_end(background, &status);
	value = runlength_end(runlength, &status);
	protocols_value(value, protocols_status);
	protocols_end(protocols_status);

	if (! debug)
		printf("\n");

	return EXIT_SUCCESS;
}

