/*
 * layout.c
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
 * fill a layout of remote keys with their codes
 *
 * see layout.1 for details
 *
 * testing:
 *	# set up hw:1 to be a loopback device
 *	serial2sound - | AUDIODEV=hw:1,1 play - -r 44100
 *	layout file.txt hw:1,0
 *	serial -d ttySS0
 *
 * todo:
 * - if no keys have codes, do not print codes
 * - if keys have different addresses/protocols, print them as well
 */

/*
 * the layout
 * ----------
 *
 * the layout file is a text file with key definitions and arbitrary spaces and
 * newlines; each key definition is either NAME or NAME|CODE; the idea is that
 * the layout file is initially filled with NAMEs only, and this program fills
 * the CODEs
 *
 * internally, a layout is an array of namedkeys, each being:
 * - the name of the key, or a string of spaces or a single newline
 * - the key, or NULL if the key is unknown or a filler
 *
 * the program does not touch fillers (spaces and newlines), so that they are
 * saved exactly as they were in the input file; it instead updates the keys by
 * adding or changing their codes
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <termios.h>
#include <pthread.h>
#include <signal.h>
#include "microphone.h"
#include "filters.h"
#include "protocols.h"

/*
 * maximum length of string
 */
int maxlen(char *a, char *b) {
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return strlen(b);
	if (b == NULL)
		return strlen(a);
	return strlen(a) > strlen(b) ? strlen(a) : strlen(b);
}

/*
 * keyboard input thread
 */
void *keyboard(void *arg) {
	char *command;
	char c;

	command = (char *) arg;

	do {
		read(STDIN_FILENO, &c, 1);
		*command = c;
	} while (c != 'q' && c != 'x');

	return NULL;
}

/*
 * a key with a name
 */
struct namedkey {
	char *name;
	struct key *key;
};

/*
 * replace a code in namedkey
 */
void namedkeyreplace(struct namedkey *namedkey, struct key *key) {
	free(namedkey->key);
	namedkey->key = key;
}

/*
 * from string to named key
 */
struct namedkey *stringtonamedkey(char *string) {
	char *copy, *name, *key;
	struct namedkey *nk;

	if (string == NULL)
		return NULL;
	nk = malloc(sizeof(struct namedkey));

	copy = strdup(string);
	key = copy;
	name = strsep(&key, "|");
	nk->name = strdup(name);
	nk->key = key == NULL ? NULL : stringtokey(key, ',', '-');
	free(copy);

	return nk;
}

/*
 * from named key to string
 */
char *namedkeytostring(struct namedkey *nk) {
	char *key, *all;
	if (nk->key == NULL)
		return strdup(nk->name);
	key = keytostring(nk->key, ',', '-');
	all = malloc(strlen(nk->name) + 1 + strlen(key) + 1);
	all[0] = '\0';
	strcat(all, nk->name);
	strcat(all, "|");
	strcat(all, key);
	free(key);
	return all;
}

/*
 * print a named key
 */
void printnamedkey(struct namedkey *nk) {
	printf("%s", nk->name);
	if (nk->key == NULL) {
		printf("\n");
		return;
	}
	printf("|");
	printkey(nk->key);
}

/*
 * layout of a remote
 */
struct layout {
	struct namedkey **namedkey;
	int num;
	int size;
};

/*
 * initialize a layout
 */
struct layout *layoutnew() {
	struct layout *layout;
	layout = malloc(sizeof(struct layout));
	layout->namedkey = NULL;
	layout->num = 0;
	layout->size = 0;
	return layout;
}

/*
 * deallocate a layout
 */
void layoutfree(struct layout *layout) {
	int pos;
	for (pos = 0; pos < layout->num; pos++) {
		free(layout->namedkey[pos]->name);
		free(layout->namedkey[pos]->key);
	}
	free(layout->namedkey);
	free(layout);
}

/*
 * add a new key to a layout
 */
int layoutadd(struct layout *layout, struct namedkey *namedkey) {
	layout->num++;
	if (layout->num > layout->size) {
		layout->size += 10;
		layout->namedkey = realloc(layout->namedkey,
			layout->size * sizeof(struct namedkey *));
	}
	layout->namedkey[layout->num - 1] = namedkey;
	return 0;
}

/*
 * read a layout from file
 */
struct layout *layoutread(FILE *fd) {
	struct layout *layout;
	struct namedkey *nk;
	int c, len;
	char key[100];

	layout = layoutnew();
	fseek(fd, 0, SEEK_SET);

	while (1) {
		len = 0;
		do {
			c = fgetc(fd);
			len++;
		} while (c == ' ' && c != EOF);

		if (len > 1) {
			nk = malloc(sizeof(struct namedkey));
			nk->name = malloc(len);
			memset(nk->name, ' ', len - 1);
			nk->name[len - 1] = '\0';
			nk->key = NULL;
			layoutadd(layout, nk);
		}

		if (c == EOF)
			break;

		if (c == '\n') {
			nk = malloc(sizeof(struct namedkey));
			nk->name = strdup("\n");
			nk->key = NULL;
			layoutadd(layout, nk);
		}
		else {
			ungetc(c, fd);
			fscanf(fd, "%80s", key);
			nk = stringtonamedkey(key);
			if (nk == NULL) {
				printf("error parsing key: %s\n", key);
				return NULL;
			}
			layoutadd(layout, nk);
		}
	}

	return layout;
}

/*
 * write layout to file
 */
int layoutwrite(FILE *fd, struct layout *layout) {
	int pos;
	char *key;
	fseek(fd, 0, SEEK_SET);
	for (pos = 0; pos < layout->num; pos++) {
		key = namedkeytostring(layout->namedkey[pos]);
		fprintf(fd, "%s", key);
		free(key);
	}
	return 0;
}

/*
 * find position of a name and/or code in a layout
 */
int layoutfind(struct layout *layout, char *name, struct key *key) {
	struct namedkey *nk;
	int pos;

	for (pos = 0; pos < layout->num; pos++) {
		nk = layout->namedkey[pos];
		if (name != NULL && ! ! strcmp(name, nk->name))
			continue;
		if (key != NULL && ! keyequal(key, nk->key, 0))
			continue;
		return pos;
	}

	return -1;
}

/*
 * print the protocol(s) and device(s) of a remote
 */
void layoutremoteprint(struct layout *layout) {
	struct layout *remote;
	struct namedkey *k1, *k2;
	int p1, p2;
	int found;
	char device[100];

	remote = layoutnew();

	for (p1 = 0; p1 < layout->num; p1++) {
		k1 = layout->namedkey[p1];
		if (k1->key == NULL)
			continue;
		found = 0;
		for (p2 = 0; p2 < remote->num; p2++) {
			k2 = remote->namedkey[p2];
			if (k1->key->protocol == k2->key->protocol &&
			    k1->key->device == k2->key->device &&
			    k1->key->subdevice == k2->key->subdevice) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;
		device[0] = '\0';
		appendprotocol(device, k1->key->protocol);
		strcat(device, ",");
		appendcode(device, k1->key->device, k1->key->subdevice, '-');
		printf("%s\n", device);
		
		layoutadd(remote, k1);
	}

	free(remote->namedkey);
	free(remote);
}

/*
 * print a layout
 */
void layoutprint(struct layout *layout, int codes, int complete) {
	int pos, lastline, len;
	int codesrow;
	struct key *ks;
	char *key;

	if (codes && ! complete)
		layoutremoteprint(layout);

	codesrow = 0;
	lastline = -1;
	for (pos = 0; pos < layout->num; pos++) {
		ks = layout->namedkey[pos]->key;
		if (ks == NULL || ! codes) {
			key = malloc(1);
			key[0] = '\0';
		}
		else if (complete)
			key = keytostring(ks, ',', '-');
		else {
			key = malloc(20);
			key[0] = '\0';
			appendcode(key, ks->function, ks->subfunction, '-');
		}
		len = maxlen(layout->namedkey[pos]->name, key);

		if (layout->namedkey[pos]->name[0] == '\n') {
			printf("%s", layout->namedkey[pos]->name);
			if (! codesrow && codes) {
				pos = lastline;
				codesrow = 1;
			}
			else {
				lastline = pos;
				codesrow = 0;
			}
		}
		else if (layout->namedkey[pos]->name[0] == ' ')
			printf("%s", layout->namedkey[pos]->name);
		else if (! codesrow)
			printf("%-*s", len, layout->namedkey[pos]->name);
		else if (key == NULL)
			printf("%-*s", len, "");
		else
			printf("%-*s", len, key);

		free(key);
	}
}

/*
 * export a layout to csv
 */
void layoutcsvprint(struct layout *layout) {
	int pos, cur;
	int prev, next;
	struct key *ks;
	char *name, *comma;
	char protocol[100];
	int16_t subdevice;

	prev = -1;

	while (1) {
		next = -1;
		for (pos = 0; pos < layout->num; pos++) {
			name = layout->namedkey[pos]->name;
			if (name[0] == '\n' || name[0] == ' ')
				continue;
			ks = layout->namedkey[pos]->key;
			if (ks->function <= prev)
				continue;
			if (next == -1 || ks->function < next) {
				next = ks->function;
				cur = pos;
			}
		}
		prev = next;
		if (next == -1)
			break;

		name = layout->namedkey[cur]->name;
		if (name[0] == '\n' || name[0] == ' ')
			continue;
		comma = strchr(name, ',');
		if (! comma)
			printf("%s,", name);
		else {
			fwrite(name, 1, comma - name, stdout);
			putchar(',');
		}
		ks = layout->namedkey[cur]->key;
		protocol[0] = '\0';
		appendprotocol(protocol, ks->protocol);
		printf("%s,", protocol);
		subdevice = ks->subdevice == -1 ?
			~ks->device & 0xFF : ks->subdevice;
		printf("%d,%d,%d", ks->device, subdevice, ks->function);
		printf("\n");
	}
}

/*
 * prompt
 */
void prompt(int pos, struct layout *layout) {
	printf("press key: %-10s", layout->namedkey[pos]->name);
	printf("       p=previous n=next v=view\n");
}

/*
 * whether a position is valid for a layout
 */
int iskeyvalid(int pos, struct layout *layout) {
	return pos >= 0 && pos < layout->num;
}

/*
 * whether a position is a filler (' ' or '\n'), not a key
 */
int iskeyfiller(int pos, struct layout *layout) {
	return iskeyvalid(pos, layout) &&
	       (layout->namedkey[pos]->name[0] == ' ' ||
	        layout->namedkey[pos]->name[0] == '\n');
}

/*
 * whether a position is a key with code
 */
int iskeycoded(int pos, struct layout *layout) {
	return iskeyvalid(pos, layout) && layout->namedkey[pos]->key != NULL;
}

/*
 * move to next or previous key
 */
void movekey(int *pos, struct layout *layout, int direction, int skipknown) {
	int lastpos, nextpos, respos;

	lastpos = *pos;
	nextpos = *pos;
	do {
		nextpos += direction;
		if (iskeycoded(nextpos, layout))
			lastpos = nextpos;
	} while (iskeyfiller(nextpos, layout) ||
	         (skipknown && iskeycoded(nextpos, layout)));

	respos = iskeyvalid(nextpos, layout) ? nextpos : lastpos;
	if (respos != *pos)
		prompt(respos, layout);
	*pos = respos;
}

/*
 * usage
 */
void usage() {
	printf("usage:\n");
	printf("\tlayout [-s] [-c] [-k] [-t] [-l [-f]] [-r] [-h]");
	printf(" layout.txt [soundcard]\n");
	printf("\t\t-s\t\tshow the layout of keys and terminate\n");
	printf("\t\t-c\t\tomit codes when showing a layout\n");
	printf("\t\t-k\t\tprint complete codes when showing a layout\n");
	printf("\t\t-t\t\tprint layout in csv and terminate\n");
	printf("\t\t-l\t\tlog input data to log.au\n");
	printf("\t\t-f\t\twith, -f, log input data to log.txt\n");
	printf("\t\t-r\t\tfind key names instead of saving them\n");
	printf("\t\t-h\t\tthis help\n");
	printf("\t\tlayout.txt\tthe file that is read and written\n");
	printf("\t\tsoundcard\tthe soundcard name\n");
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	int showlayout, showcodes, showall, showcsv;
	int ascii, readkeys;
	char *layoutfile, *infile, *logfile;
	FILE *layoutfd;
	struct layout *layout;
	struct status status;
	void *microphone, *read, *filters;
	int value;
	struct protocols_status *protocols_status;
	struct key *key, *lastkey;
	int pos, direction, increase;
	int finish, save, skipknown;
	struct termios original, raw;
	pthread_t input;
	volatile char command; // a mutex doesn't seem necessary

					/* arguments */

	showlayout = 0;
	showcodes = 1;
	showall = 0;
	showcsv = 0;
	logfile = NULL;
	ascii = 0;
	readkeys = 0;
	while (-1 != (opt = getopt(argc, argv, "skctlfrh")))
		switch (opt) {
		case 's':
			showlayout = 1;
			break;
		case 'k':
			showall = 1;
			break;
		case 'c':
			showcodes = 0;
			break;
		case 't':
			showlayout = 1;
			showcsv = 1;
			break;
		case 'l':
			logfile = "log.au";
			break;
		case 'f':
			ascii = 1;
			break;
		case 'r':
			readkeys = 1;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		}
	if (ascii && logfile)
		logfile = "log.txt";

	argc -= optind - 1;
	argv += optind - 1;
	if (argc - 1 < 1) {
		printf("layout file missing\n");
		usage();
		exit(EXIT_FAILURE);
	}
	else
		layoutfile = argv[1];
	if (argc - 1 < 2)
		infile = "default";
	else
		infile = argv[2];

					/* open and read layout file */

	layoutfd = fopen(layoutfile, "r+");
	if (layoutfd == NULL) {
		perror(layoutfile);
		exit(EXIT_FAILURE);
	}
	layout = layoutread(layoutfd);
	if (showcsv) {
		layoutcsvprint(layout);
		return 0;
	}
	layoutprint(layout, showcodes, showall);
	if (showlayout)
		return 0;

					/* init filters and protocols */

	read = read_init(infile, ascii, &status);
	if (read != NULL)
		microphone = NULL;
	else {
		microphone = microphone_init(infile, &status);
		if (microphone == NULL) {
			printf("cannot open input file\n");
			exit(EXIT_FAILURE);
		}
	}
	filters = best_init(logfile, &status);
	protocols_status = protocols_init(0);
	
					/* start reading keyboard */

	tcgetattr(STDIN_FILENO, &original);
	raw = original;
	cfmakeraw(&raw);
	raw.c_oflag |= OPOST | ONLCR;
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);

	command = '\0';
	pthread_create(&input, NULL, keyboard, (void *) &command);

					/* process microphone data */

	pos = -1;
	skipknown = 1;
	finish = 0;
	save = 1;
	direction = 0;
	increase = 1;
	key = NULL;
	lastkey = NULL;
	while (! finish) {

					/* move to next key in layout */

		if (! readkeys) {
			movekey(&pos, layout, increase, skipknown);
			if (iskeycoded(pos, layout) &&
			    key != NULL &&
			    direction == 0)
				break;
		}

		do {

					/* process command, if any */

			switch (command) {
			case 'v':
				layoutprint(layout, showcodes, showall);
				if (! readkeys)
					prompt(pos, layout);
				break;
			case 'p':
			case 'n':
				if (readkeys)
					break;
				skipknown = 0;
				direction = command == 'n' ? 1 : -1;
				movekey(&pos, layout, direction, skipknown);
				lastkey = NULL;
				break;
			case 'w':
				if (readkeys)
					break;
				layoutwrite(layoutfd, layout);
				printf("saved!");
				prompt(pos, layout);
				break;
			case 'x':
				save = 0;
				/* fallthrough */
			case 'q':
				finish = 1;
				break;
			case '\0':
				break;
			default:
				printf("unassigned key: %c\n", command);
				if (! readkeys)
					prompt(pos, layout);
			}
			if (finish)
				break;
			command = '\0';

					/* get remote key from microphone */

			key = NULL; // do not free: already done OR in layout
			value = 0;
			if (read)
				FILTER_VALUE(read, value, read, &status)
			if (microphone)
				FILTER_VALUE(microphone, value, microphone,
					&status)
			FILTER_VALUE(best, value, filters, &status)
			key = protocols_value(value, protocols_status);
			if (key != NULL && key->repeat) {
				free(key);
				key = NULL;
			}
		} while (key == NULL);

		if (finish)
			break;
	

					/* find and print key in layout */

		if (readkeys) {
			pos = layoutfind(layout, NULL, key);
			if (pos == -1) {
				printf("not found: ");
				printkey(key);
				printf("\n");
			}
			else {
				printf("%s: ", layout->namedkey[pos]->name);
				printkey(key);
				printf("\n");
			}
			free(key);
		}

					/* add key to layout */

		else if (! keyequal(key, lastkey, 0)) {
			namedkeyreplace(layout->namedkey[pos], key);
			lastkey = key;
			printnamedkey(layout->namedkey[pos]);
			printf("\n");
			increase = 1;
		}
		else {
			free(key);
			increase = 0;
		}
	}

					/* end filters */

	if (read)
		read_end(read, &status);
	if (microphone)
		microphone_end(microphone, &status);
	value = best_end(filters, &status);
	if (! readkeys) {
		protocols_value(value, protocols_status);
		protocols_end(protocols_status);
	}

					/* terminate */

	layoutprint(layout, showcodes, showall);
	if (save)
		layoutwrite(layoutfd, layout);
	fclose(layoutfd);
	layoutfree(layout);

//	pthread_kill(input, SIGTERM);
//	pthread_join(input, NULL);

	tcsetattr(STDIN_FILENO, TCSANOW, &original);
	return EXIT_SUCCESS;
}

