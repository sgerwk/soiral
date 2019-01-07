/*
 * serial2sound.c
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
 * make sound out of serial stream
 *
 * read from the ttySS0 pipe, and translate the serial bytes some other program
 * writes to it to a sound file, adding some random noise; this allows for
 * testing programs that produce infrared signals via uart and programs that
 * read infrared signals from a soundcard
 *
 * test:
 *	serial2sound - | sox - -r 44100 -b 16 -t au - | remote -
 *	serial ttySS0
 *
 * to visualize the actual waveform:
 *	serial2sound &
 *	serial ttySS0
 *	sox output.au -r 44100 resampled.au
 *	signal2pbm -t 4 -e 2 -i 20 -c resampled.au ; fbi output.png
 *	remote resampled.au
 *
 * todo:
 * - optionally write output to a sound device (aloop) instead of stdout
 * - make a distinction between reading from a pipe (which this program creates
 *   if it does not exist) and a serial device like /dev/ttyS0, /dev/ttyUSB0 or
 *   /dev/tnt1 (which must exist); maybe: create the pipe only if no argument
 *   is passed, otherwise ask to create the pipe before calling
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>

/*
 * create an AU file
 */
FILE *aucreate(char *filename, int baudrate) {
	uint32_t header[6] = { 0x2E736E64, 24, 0XFFFFFFFF, 3, baudrate, 1 };
	FILE *out;
	int i;

	if (filename == NULL)
		return NULL;

	out = ! strcmp(filename, "-") ? stdout : fopen(filename, "w");
	if (out == NULL) {
		perror(filename);
		return NULL;
	}

	for (i = 0; i < 6; i++)
		header[i] = htobe32(header[i]);
	fwrite(header, 4, 6, out);

	return out;
}

/*
 * add a value to an AU file
 */
int auwrite(FILE *out, int val) {
	val = htobe16(val + random() % 200 - 100);
	return fwrite(&val, 2, 1, out);
}

/*
 * bit number of value
 */
int bit(int value, int number) {
	return (value >> number) & 0x01;
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	char *pipename = "ttySS0";
	char *outfile = "output.au";
	int in;
	FILE *out;
	uint8_t c;
	int repeat = 0;
	int i, res;

	if (argc - 1 >= 1)
		outfile = argv[1];
	if (! strcmp(outfile, "-"))
		repeat = 1;

	mkfifo(pipename, 0666);
	out = aucreate(outfile, 460800);

	do {
		for (i = 0; i < 100000; i++)
			auwrite(out, 0);

		// FIXME: if pipename does not exists, exit
		in = open(pipename, O_RDONLY);
		if (in == -1) {
			perror(pipename);
			exit(EXIT_FAILURE);
		}

		while (1 == (res = read(in, &c, 1))) {
			auwrite(out, INT16_MAX / 2);
			for (i = 0; i < 8; i++)
				auwrite(out, bit(c, i) ? 0 : INT16_MAX / 2);
			auwrite(out, 0);
			auwrite(out, 0);
			auwrite(out, 0);
		}

		fflush(out);
		close(in);
	} while (repeat);

	return EXIT_SUCCESS;
}

