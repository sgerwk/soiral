/*
 * filters.h
 *
 * some filters for sequences of integers
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

#ifdef _FILTERS_H
#else
#define _FILTERS_H

/*
 * outut status of filters
 */
struct status {
	int ended;
	int hasout;
	int flush;
};

/*
 * the filters
 */

void *read_init(char *filename, int ascii, struct status *status);
void *log_init(char *filename, int ascii, struct status *status);
void *scale_init(struct status *status);
void *diff_init(struct status *status);
void *amplify_init(double factor, struct status *status);
void *stabilize_init(struct status *status);
void *maximal_init(int size, struct status *status);
void *trigger_init(int bound, struct status *status);
void *background_init(struct status *status);
void *positive_init(struct status *status);
void *boost_init(int size, struct status *status);
void *valley_init(int size, struct status *status);
void *runlength_init(struct status *status);
void *collapse_init(struct status *status);
void *best_init(char *logfile, struct status *status);

int read_value(int value, void *internal, struct status *status);
int log_value(int value, void *internal, struct status *status);
int scale_value(int value, void *internal, struct status *status);
int diff_value(int value, void *internal, struct status *status);
int amplify_value(int value, void *internal, struct status *status);
int stabilize_value(int value, void *internal, struct status *status);
int maximal_value(int value, void *internal, struct status *status);
int trigger_value(int value, void *internal, struct status *status);
int background_value(int value, void *internal, struct status *status);
int positive_value(int value, void *internal, struct status *status);
int boost_value(int value, void *internal, struct status *status);
int valley_value(int value, void *internal, struct status *status);
int runlength_value(int value, void *internal, struct status *status);
int collapse_value(int value, void *internal, struct status *status);
int best_value(int value, void *internal, struct status *status);

int read_end(void *internal, struct status *status);
int log_end(void *internal, struct status *status);
int scale_end(void *internal, struct status *status);
int diff_end(void *internal, struct status *status);
int amplify_end(void *internal, struct status *status);
int stabilize_end(void *internal, struct status *status);
int maximal_end(void *internal, struct status *status);
int trigger_end(void *internal, struct status *status);
int background_end(void *internal, struct status *status);
int positive_end(void *internal, struct status *status);
int boost_end(void *internal, struct status *status);
int valley_end(void *internal, struct status *status);
int runlength_end(void *internal, struct status *status);
int collapse_end(void *internal, struct status *status);
int best_end(void *internal, struct status *status);

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

#endif
