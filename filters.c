/*
 * filters.c
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
 * some filters for sequences of integers
 *
 * todo:
 * - stabilize: decrease bound by 1% every 100 steps or similar, rather than
 *   continuously at each step
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "filters.h"

/*
 * an audio file, either AU or ascii
 */
struct audiofile {
	FILE *fd;
	int ascii;
};

/*
 * a buffer, with a bound
 */
struct buffer {
	int size;
	int pos;
	int *data;
	int bound;
};

struct buffer *balloc(int size) {
	struct buffer *b;
	b = malloc(sizeof(struct buffer));
	b->size = size;
	b->data = malloc(b->size * sizeof(int));
	memset(b->data, 0, b->size);
	b->pos = 0;
	return b;
}

void bfree(struct buffer *b) {
	free(b->data);
	free(b);
}

/*
 * functions of general use
 */
int abs(int value) {
	return value > 0 ? value : -value;
}

int average(struct buffer *b) {
	int tot, i;
	tot = 0;
	for (i = 0; i < b->size; i++)
		tot += b->data[i];
	return tot / b->size;
}

int maximal(struct buffer *b) {
	int max, i;
	max = abs(b->data[0]);
	for (i = 0; i < b->size; i++)
		if (max < abs(b->data[i]))
			max = abs(b->data[i]);
	return max;
}

/*
 * readinput filter
 */
void *read_init(char *filename, int ascii, struct status *status) {
	struct audiofile *read;
	uint32_t header[6];
	int i;

	read = malloc(sizeof(struct audiofile));
	read->ascii = ascii;

	if (! strcmp(filename, "-"))
		read->fd = stdin;
	else {
		read->fd = fopen(filename, "r");
		if (read->fd == NULL) {
			perror(filename);
			return 0;
		}
	}

	if (! ascii) {
		fread(header, 4, 6, read->fd);
		for (i = 0; i < 6; i++)
			header[i] = be32toh(header[i]);

		if (header[0] != 0x2E736E64) {
			printf("%s: not an AU file\n", filename);
			exit(EXIT_FAILURE);
		}
		if (header[3] != 3) {
			printf("%s: not 16-bit linear PCM\n", filename);
			exit(EXIT_FAILURE);
		}
		if (header[4] != 44100)
			fprintf(stderr, "WARNING: sample rate is not 44100\n");
		if (header[5] != 1) {
			printf("%s: number of channel is not 1\n", filename);
			exit(EXIT_FAILURE);
		}

		fseek(read->fd, header[1], SEEK_SET);
	}

	status->ended = 0;
	return read;
}

int read_value(int value, void *internal, struct status *status) {
	struct audiofile *read;
	int out;
	int16_t v;

	(void) value;
	read = (struct audiofile *) internal;

	if (read->ascii) {
		if (1 == fscanf(read->fd, "%d", &out))
			return out;
	}
	else {
		if (1 == fread(&v, 2, 1, read->fd)) {
			v = be16toh(v);
			return v;
		}
	}

	status->ended = 1;
	return 0;
}

int read_end(void *internal, struct status *status) {
	struct audiofile *read;
	(void) status;
	read = (struct audiofile *) internal;
	fclose(read->fd);
	free(read);
	return 0;
}

/*
 * log filter (save values to file)
 */
void *log_init(char *filename, int ascii, struct status *status) {
	struct audiofile *log;
	uint32_t header[6] = { 0x2E736E64, 24, 0XFFFFFFFF, 3, 44100, 1 };
	int i;

	(void) status;

	if (filename == NULL)
		return NULL;

	log = malloc(sizeof(struct audiofile));
	log->ascii = ascii;
	log->fd = fopen(filename, "w");
	if (log->fd == NULL) {
		perror(filename);
		return NULL;
	}

	if (! ascii) {
		for (i = 0; i < 6; i++)
			header[i] = htobe32(header[i]);
		fwrite(header, 4, 6, log->fd);
	}

	return log;
}

int log_value(int value, void *internal, struct status *status) {
	struct audiofile *log;
	int16_t val;

	(void) status;

	if (internal == NULL)
		return value;

	log = (struct audiofile *) internal;
	if (log->ascii)
		fprintf(log->fd, "%d\n", value);
	else {
		val = htobe16(value);
		fwrite(&val, 2, 1, log->fd);
	}
	return value;
}

int log_end(void *internal, struct status *status) {
	struct audiofile *log;
	uint32_t size;

	(void) status;

	if (internal == NULL)
		return 0;

	log = (struct audiofile *) internal;
	size = htobe32(ftell(log->fd) - 24);
	fseek(log->fd, 2 * 4, SEEK_SET);
	fwrite(&size, 4, 1, log->fd);
	fclose(log->fd);
	return 0;
}

/*
 * scale filter (show values on screen)
 */
struct scale {
	int level;
	int nlevel;
};

void *scale_init(struct status *status) {
	struct scale *scale;

	(void) status;

	scale = malloc(sizeof(struct scale));
	scale->level = 0;
	scale->nlevel = 0;

	return scale;
}

int scale_value(int value, void *internal, struct status *status) {
	struct scale *scale;
	int i;

	(void) status;

	scale = (struct scale *) internal;

	if (value > scale->level)
		scale->level = value;
	if (-value > scale->level)
		scale->level = -value;
	scale->nlevel++;
	if (scale->nlevel < 32)
		return value;

	printf("%8d ", scale->level);
	for (i = -30; i < 30; i++) {
		if (i < 0)
			putchar(scale->level < 0 &&
			        scale->level * 80 / INT16_MAX < i ? '<' : ' ');
		else if (i == 0)
			printf("|");
		else if (i > 0)
			putchar(scale->level > 0 &&
			        i < scale->level * 80 / INT16_MAX ? '>' : ' ');
	}
	printf("\r");

	scale->level = 0;
	scale->nlevel = 0;
	return value;
}

int scale_end(void *internal, struct status *status) {
	(void) status;
	free(internal);
	return 0;
}

/*
 * amplify filter
 */
void *amplify_init(double factor, struct status *status) {
	double *internal;
	(void) status;
	internal = malloc(sizeof(double *));
	*internal = factor;
	return internal;
}

int amplify_value(int value, void *internal, struct status *status) {
	double factor;
	(void) status;
	factor = * (double *) internal;
	return value * factor;
}

int amplify_end(void *internal, struct status *status) {
	free(internal);
	status->hasout = 0;
	return 0;
}

/*
 * diff filter
 */
void *diff_init(struct status *status) {
	int **internal;
	(void) status;
	internal = malloc(sizeof(int **));
	*internal = NULL;
	return internal;
}

int diff_value(int value, void *internal, struct status *status) {
	int **prev, out;
	prev = (int **) internal;
	if (*prev == NULL) {
		*prev = malloc(sizeof(int));
		**prev = value;
		status->hasout = 0;
		return 0;
	}
	out = value - **prev;
	**prev = value;
	return out;
}

int diff_end(void *internal, struct status *status) {
	int **prev;
	prev = (int **) internal;
	free(*prev);
	free(internal);
	status->hasout = 0;
	return 0;
}

/*
 * spike filter
 */
void *spike_init(int bound, struct status *status) {
	struct buffer *b;
	(void) status;
	b = balloc(1);
	b->bound = bound;
	b->pos = -1;
	return b;
}

int spike_value(int value, void *internal, struct status *status) {
	struct buffer *b;
	int prev;
	b = (struct buffer *) internal;

	if (b->pos == -1) {
		b->data[0] = value;
		b->pos = 0;
		status->hasout = 0;
		return 0;
	}

	prev = b->data[0];
	b->data[0] = value;
	return value > b->bound && prev > b->bound ? 0 : value - prev;
}

int spike_end(void *internal, struct status *status) {
	bfree((struct buffer *) internal);
	status->hasout = 0;
	return 0;
}

/*
 * stabilize filter
 */
void *stabilize_init(struct status *status) {
	int *bound;
	(void) status;
	bound = malloc(sizeof(int));
	*bound = 0;
	return bound;
}

int stabilize_value(int value, void *internal, struct status *status) {
	int *bound;
	(void) status;
	bound = (int *) internal;
	*bound = *bound < abs(value) ? abs(value) : *bound * 9995 / 10000;
	return abs(value) < *bound / 4 ? 0 : value;
}

int stabilize_end(void *internal, struct status *status) {
	free(internal);
	status->hasout = 0;
	return 0;
}

/*
 * maximal filter
 */
void *maximal_init(int size, struct status *status) {
	(void) status;
	return balloc(size);
}

int maximal_value(int value, void *internal, struct status *status) {
	struct buffer *b;
	int out;
	(void) status;
	b = (struct buffer *) internal;
	b->data[b->pos] = value;
	b->pos = (b->pos + 1) % b->size;
	if (abs(b->data[(b->pos + b->size / 2) % b->size]) != maximal(b))
		out = 0;
	else {
		out = b->data[(b->pos + b->size / 2) % b->size];
		b->data[(b->pos + b->size / 2) % b->size] *= 2;
	}
	return out;
}

int maximal_end(void *internal, struct status *status) {
	bfree(internal);
	status->hasout = 0;
	return 0;
}

/*
 * trigger filter
 */
void *trigger_init(int bound, struct status *status) {
	int *b;
	(void) status;
	b = malloc(sizeof(int));
	*b = bound;
	return b;
}

int trigger_value(int value, void *internal, struct status *status) {
	int *bound;
	(void) status;
	bound = (int *) internal;
	return abs(value) < *bound ? 0 : value;
}

int trigger_end(void *internal, struct status *status) {
	free(internal);
	status->hasout = 0;
	return 0;
}

/*
 * background noise canceler filter
 */
struct background {
	int maxpos;
	int maxneg;
	int time;
	int silencetime;
};

void *background_init(struct status *status) {
	struct background *background;
	(void) status;
	background = malloc(sizeof(struct background));
	background->time = 0;
	background->silencetime = 0;
	background->maxpos = -1;
	background->maxneg = 1;
	return background;
}

int background_value(int value, void *internal, struct status *status) {
	struct background *background;
	background = (struct background *) internal;
	if (background->time < 1000) {

		/* total silence is due to the card or recording program, not
		 * to the ir diode; count that as 1/10 time */
		background->silencetime++;
		if (value == 0 && background->silencetime % 10 != 0)
			return 0;
		background->time++;

		status->hasout = 0;
		if (background->time < 10)
			return 0;

		/* under-emphasize rare spikes by averaging the new maximum
		 * with the previous */
		if (background->maxpos < value)
			background->maxpos =
				(3 * background->maxpos + value) / 4;
		if (background->maxneg > value)
			background->maxneg =
				(3 * background->maxneg + value) / 4;
		return 0;
	}
	if (background->time == 1000) {
		background->time = 1001;
		fprintf(stderr, "background bounds: %d %d\n",
			background->maxneg, background->maxpos);
	}
	return 2 * background->maxneg < value &&
	                                value < 2 * background->maxpos ?
			0 : value;
}

int background_end(void *internal, struct status *status) {
	free(internal);
	status->hasout = 0;
	return 0;
}

/*
 * positive filter
 */
void *positive_init(struct status *status) {
	(void) status;
	return status;
}

int positive_value(int value, void *internal, struct status *status) {
	(void) status;
	(void) internal;
	return abs(value);
}

int positive_end(void *internal, struct status *status) {
	(void) internal;
	status->hasout = 0;
	return 0;
}

/*
 * boost filter
 */
void *boost_init(int size, struct status *status) {
	(void) status;
	return balloc(size);
}

int boost_value(int value, void *internal, struct status *status) {
	struct buffer *b;
	(void) status;
	b = (struct buffer *) internal;
	b->data[b->pos] = value;
	b->pos = (b->pos + 1) % b->size;
	return maximal(b);
}

int boost_end(void *internal, struct status *status) {
	bfree(internal);
	status->hasout = 0;
	return 0;
}

/*
 * valley filter
 */
void *valley_init(int size, struct status *status) {
	(void) status;
	return balloc(size);
}

int valley_value(int value, void *internal, struct status *status) {
	int before, after, i, c;
	struct buffer *b;

	(void) status;

	b = (struct buffer *) internal;
	b->data[b->pos] = value;
	b->pos = (b->pos + 1) % b->size;

	before = 0;
	after = 0;
	for (i = 0; i < b->size; i++) {
		c = abs(b->data[(b->pos + i) % b->size]);
		if (i < b->size / 2 && before < c)
			before = c;
		if (i >= b->size / 2 && after < c)
			after = c;
	}

	return before < after ? before : after;
}

int valley_end(void *internal, struct status *status) {
	bfree(internal);
	status->hasout = 0;
	return 0;
}

/*
 * runlength filter
 */
void *runlength_init(struct status *status) {
	int *time;
	(void) status;
	time = malloc(sizeof(int));
	*time = -1;
	return time;
}

int runlength_value(int value, void *internal, struct status *status) {
	int *time;
	time = (int *) internal;
	int out;
	if (value != 0 || abs(*time) > 10000) {
		out = *time;
		*time = value < 0 ? -1 : value > 0 ? 1 : *time < 0 ? -1 : 1;
		status->flush = 1;
	}
	else {
		*time = *time < 0 ? *time - 1 : *time + 1;
		status->hasout = 0;
	}
	return out;
}

int runlength_end(void *internal, struct status *status) {
	int time;
	(void) status;
	time = * (int *) internal;
	free(internal);
	return time;
}

/*
 * collapse filter
 */
void *collapse_init(struct status *status) {
	int *prev;
	(void) status;
	prev = malloc(sizeof(int));
	*prev = 0;
	return prev;
}

int collapse_value(int value, void *internal, struct status *status) {
	int prev;
	prev = * (int *) internal;
	if ((prev < 0 && value < 0) || (prev > 0 && value > 0)) {
		* (int *) internal += value;
		status->hasout = 0;
		return 0;
	}
	else {
		* (int *) internal = value;
		status->flush = 1;
		return prev;
	}
}

int collapse_end(void *internal, struct status *status) {
	int prev;
	(void) status;
	prev = * (int *) internal;
	free(internal);
	return prev;
}

/*
 * best sequence of filters found so far
 */
struct bestfilters {
	void *log;
	void *diff;
	void *maximal;
	void *stabilize;
	void *background;
	void *runlength;
};

void *best_init(char *logfile, struct status *status) {
	struct bestfilters *bestfilters;

	(void) status;

	bestfilters = malloc(sizeof(struct bestfilters));
	bestfilters->log = log_init(logfile, 0, status);
	bestfilters->diff = diff_init(status);
	bestfilters->maximal = maximal_init(11, status);
	bestfilters->stabilize = stabilize_init(status);
	bestfilters->background = background_init(status);
	bestfilters->runlength = runlength_init(status);

	return bestfilters;
}

int best_value(int value, void *internal, struct status *status) {
	struct bestfilters *bestfilters;

	bestfilters = (struct bestfilters *) internal;

	value = log_value(value, bestfilters->log, status);
	value = diff_value(value, bestfilters->diff, status);
	value = maximal_value(value, bestfilters->maximal, status);
	value = stabilize_value(value, bestfilters->stabilize, status);
	value = background_value(value, bestfilters->background, status);
	value = runlength_value(value, bestfilters->runlength, status);
	
	return value;
}

int best_end(void *internal, struct status *status) {
	struct bestfilters *bestfilters;
	int value;

	bestfilters = (struct bestfilters *) internal;

	value = log_end(bestfilters->log, status);
	value = diff_end(bestfilters->diff, status);
	value = maximal_end(bestfilters->maximal, status);
	value = stabilize_end(bestfilters->stabilize, status);
	value = background_end(bestfilters->background, status);
	value = runlength_end(bestfilters->runlength, status);

	free(internal);
	return value;
}

/*
 * apply a filter
 */
#define FILTER_VALUE(filter, value, internal, status) {		\
 	(status)->ended = 0;					\
	(status)->hasout = 1;					\
	(status)->flush = 0;					\
 	value = filter ## _value (value, internal, status);	\
 	if ((status)->ended) break;				\
	if (! (status)->hasout) continue;			\
}

