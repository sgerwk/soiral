/*
 * signal2pbm.c
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
 * convert a sequence of values to an image
 * for example, the integers from microphone.c
 *
 * see man page
 */

#include <stdlib.h>
#include <string.h>
#include <endian.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

/*
 * integer option
 */
#define INTOPT(variable,min,error)				\
	variable = atoi(optarg);				\
	if (variable < (min)) {					\
		printf("error parsing " error "\n");		\
		exit(EXIT_FAILURE);				\
	}

/*
 * newline, maybe
 */
void newline(FILE *out, int *l) {
	(*l)++;
	if (*l == 69) {
		fprintf(out, "\n");
		*l = 0;
	}
}

/*
 * read an input value
 */
int input(FILE *in, int ascii, int *value) {
	int res;
	char line[100];
	int16_t v;

	if (ascii) {
		res = fscanf(in, "%d", value);
		if (res == 1)
			return res;
		fgets(line, 100, in);
		return EOF;
	}

	res = fread(&v, 2, 1, in);
	v = be16toh(v);
	*value = v;
	return res == 0 ? EOF : res;
}

/*
 * display height of a value
 */
#define HEIGHT(x) ((x) * interline * expansion / maxvalue)

/*
 * usage
 */
void usage() {
	printf("plot a sequence of integers\n");
	printf("usage:\n");
	printf("\tsignal2pbm [options] infile [outfile.pbm]\n");
	printf("\t\t-a\t\tread data in ascii format, rather than au\n");
	printf("\t\t-w width\twidth of image (height depends on samples)\n");
	printf("\t\t-t timeslot\tsamples squeezed in a horizontal pixel\n");
	printf("\t\t-m maxvalue\tthe maximal value for the samples\n");
	printf("\t\t-i interline\tspace between lines of signal\n");
	printf("\t\t-e factor\texpand every line by this factor\n");
	printf("\t\t-s threshold\tless than this is like zero\n");
	printf("\t\t-n height\theight for a line without signal\n");
	printf("\t\t-j\t\tdo not connect jumps in the signal line\n");
	printf("\t\t-a\t\talso show average of signal in the timeslot\n");
	printf("\t\t-0\t\tdraw the level of 0 signal\n");
	printf("\t\t-c\t\tconvert output to png (requires netpbm)\n");
	printf("\t\t-v\t\tshow image with an external viewer (fbi)\n");
	printf("\t\t-h\t\tthis help\n");
	printf("\t\toutfile.pbm\tdefault is output.pbm\n");
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	char *inname, *outname = "output.pbm";
	FILE *in, *out;
	uint32_t header[6];
	int ascii = 0;
	int maxvalue = INT16_MAX, width = 640, interline = 10;
	int expansion = 1, timeslot = 1, significant = 0, nosignalheight = 6;
	int jump = 0, displayaverage = 0, zero = 0;
	int convert = 0, view = 0;
	char *vargv[4];

	size_t pos;
	int res;
	int val;
	int *value, *minimal, *maximal, *average;
	int emax, emin, eavg;
	int firstmax, firstmin, firstavg;
	int prevmax, prevmin, prevavg;
	int r, t, x, y, ys, l;
	int hassignal;
	char c;

					/* arguments */

	while (-1 != (opt = getopt(argc, argv, "w:t:m:i:e:s:n:fja0cvh")))
		switch (opt) {
		case 'w':
			INTOPT(width, 1, "width");
			break;
		case 't':
			INTOPT(timeslot, -10000, "timeslot");
			break;
		case 'm':
			INTOPT(maxvalue, 1, "maxvalue");
			break;
		case 'i':
			INTOPT(interline, 4, "interline");
			break;
		case 'e':
			INTOPT(expansion, 1, "expansion");
			break;
		case 's':
			INTOPT(significant, 1, "significant");
			break;
		case 'n':
			INTOPT(nosignalheight, 1, "no-signal height");
			break;
		case 'j':
			jump = 1;
			break;
		case 'a':
			displayaverage = 1;
			break;
		case '0':
			zero = 1;
			break;
		case 'f':
			ascii = 1;
			break;
		case 'c':
			convert = 1;
			break;
		case 'v':
			view = 1;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}

	if (optind > argc - 1) {
		printf("file name missing\n");
		usage();
		exit(EXIT_FAILURE);
	}
	inname = argv[optind];
	if (optind < argc - 1)
		outname = argv[optind + 1];

					/* input file */

	if (! strcmp(inname, "-"))
		in = stdin;
	else {
		in = fopen(inname, "r");
		if (in == NULL) {
			perror(inname);
			exit(EXIT_FAILURE);
		}
	}

	if (! ascii) {
		fread(header, 4, 6, in);
		fprintf(stderr, "magic:    %08X\n", be32toh(header[0]));
		fprintf(stderr, "offset:   %d\n",   be32toh(header[1]));
		fprintf(stderr, "size:     %d\n",   be32toh(header[2]));
		fprintf(stderr, "encoding: %d\n",   be32toh(header[3]));
		fprintf(stderr, "rate:     %d\n",   be32toh(header[4]));
		fprintf(stderr, "channels: %d\n",   be32toh(header[5]));

		if (be32toh(header[3]) != 3) {
			printf("wrong encoding, 16-bit PCM required\n");
			exit(EXIT_FAILURE);
		}
		if (0 && be32toh(header[4]) != 44100) {
			printf("wrong sample rate, 44100 required\n");
			exit(EXIT_FAILURE);
		}
		if (be32toh(header[5]) != 1) {
			printf("wrong number of channels, 1 required\n");
			exit(EXIT_FAILURE);
		}

		fseek(in, be32toh(header[1]), SEEK_SET);
	}

					/* output file */

	out = fopen(outname, "w");
	if (out == NULL) {
		perror(outname);
		exit(EXIT_FAILURE);
	}

	fprintf(out, "P1\n");
	fprintf(out, "# converted by signal2pbm\n");
	fprintf(out, "# data from %s\n", inname);
	pos = ftell(out);
	fprintf(out, "w h                \n");

					/* arrays */

	value = malloc(width * sizeof(int));
	minimal = malloc(width * sizeof(int));
	maximal = malloc(width * sizeof(int));
	average = malloc(width * sizeof(int));

					/* loop over output lines */

	x = 0;
	y = 0;
	l = 0;
	firstmin = 0;
	firstmax = 0;
	firstavg = 0;
	for (r = width; r == width; ) {

					/* read and analyze signal */

		hassignal = 0;
		t = 0;
		for (r = 0; r < width; r++) {
			minimal[r] = INT16_MAX;
			maximal[r] = -INT16_MAX;
			average[r] = 0;
			if (timeslot <= 0) {
				if (t == 0)
					res = input(in, ascii, &val);
				value[r] = val;
				minimal[r] = val;
				maximal[r] = val;
				average[r] = val;
				t = (t + 1) % (-timeslot + 2);
			}
			else {
				for (t = 0; t < timeslot; t++) {
					res = input(in, ascii, &(value[r]));
					if (res == -1)
						continue;
					if (res == EOF)
						break;
					if (value[r] < minimal[r])
						minimal[r] = value[r];
					if (value[r] > maximal[r])
						maximal[r] = value[r];
					average[r] += value[r];
				}
				if (t == 0)
					break;
				average[r] /= t;
			}
			if (res == EOF)
				break;

			/* nothing to be seen anyway */
			if (HEIGHT(minimal[r]) == 0 && HEIGHT(maximal[r]) == 0)
				continue;

			/* value too low to be significant */
			if (value[r] < significant && value[r] > -significant)
				continue;

			hassignal = 1;
		}

					/* no significant signal */

		if (! hassignal) {
			for (ys = 0; ys < nosignalheight; ys++)
				for (x = 0; x < width; x++) {
					fprintf(out, ys == nosignalheight / 2 ?
						     	"1" : "0"); 
					newline(out, &l);
				}
			firstmin = 0;
			firstmax = 0;
			firstavg = 0;
			y += nosignalheight;
			continue;
		}

					/* draw signal */

		for (ys = interline; ys >= -interline - 1; ys--) {
			prevmin = firstmin;
			prevmax = firstmax;
			prevavg = firstavg;
			
			for (x = 0; x < width; x++) {
				/* file ends before line is complete */
				if (x >= r) {
					fprintf(out, "0");
					continue;
				}

				emin = HEIGHT(minimal[x]);
				emax = HEIGHT(maximal[x]);
				eavg = HEIGHT(average[x]);
				c = (! displayaverage &&
					((ys >= emin ||
						(! jump && ys >= prevmin)) &&
					 (ys <= emax ||
					 	(! jump && ys <= prevmax)))) ||
				    (displayaverage &&
				    	(ys == eavg ||
					 (! jump && (
					 	(eavg <= ys && ys <= prevavg) ||
						(prevavg <= ys && ys <= eavg)))
					 )
					) ||
				    (zero && ys == 0) ? '1' : '0';
				fputc(c, out);
				newline(out, &l);

				prevmin = emin;
				prevmax = emax;
				prevavg = eavg;
			}
		}

		firstmin = prevmin;
		firstmax = prevmax;
		firstavg = prevavg;
		y += interline * 2;
	}

					/* go back and write size of image */

	fseek(out, pos, SEEK_SET);
	fprintf(out, "%d %d\n", width, y);

					/* close, free, etc */

	fclose(out);
	fclose(in);

	free(value);
	free(minimal);
	free(maximal);

					/* run external viewer */

	if (view || convert)
		system("pnmtopng output.pbm > output.png");
	if (view) {
		vargv[0] = "fbi";
		vargv[1] = "--fitwidth";
		vargv[2] = "output.png";
		vargv[3] = NULL;
		execvp("fbi", vargv);
	}

	return EXIT_SUCCESS;
}
