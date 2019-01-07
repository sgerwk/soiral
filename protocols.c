/*
 * protocols.c
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
 * parse the runlength encoding of a signal from a remote
 *
 * a protocol (nec, sharp, sony,...) is described by an array:
 *	{ pair, pair, pair, ... }
 *	each pair is:
 *		min,max		value is between min and max
 *		BIT		start parsing a bit
 *				bits 0 and 1 are each a similar array of pairs
 *		END		end of sequence
 *
 * an index in this array is increased by two for each value parsed; if the
 * value is not between min and max, parsing fails and restarts from the head
 * of the array; if the index points to BIT, parsing switches to the two arrays
 * for bit 0 and 1 in parallel; if the index points to END, parsing is
 * completed successfully
 *
 * if the value is over max, only (min+max)/2 of it is consumed, the rest is
 * left for the next step; this is not nondeterministic parsing, and has some
 * limitations:
 *	- only one current value is maintained; therefore, parsing for bit 0
 *	  and 1 cannot consume different amounts of value; the one leaving the
 *	  smallest part (possibly 0) continues, the other fails
 *	- the maximum length of a period of the same sign in the protocol is
 *	  required to avoid repeatedly consuming a small part of a period that
 *	  is too long to be possibly part of the protocol; this check is not to
 *	  be done at the end of the sequence, which may be the initial part of
 *	  a long sequence; to simplify, the check is only done at the start of
 *	  the sequence; this is not a problem since a failure in the middle
 *	  restarts the sequence, which means that no more than two unnecessary
 *	  checks are done
 *
 * another limitation is that every time a protocol fails a sequence, parsing
 * is restarted from the head of the array; only the current value is checked
 * for matching the start of the protocol and not the previous values
 *
 * the easiest way to parse all protocols at the same time is by calling
 * protocols_init(), protocols_value() and maybe printkey() and protocols_end()
 * for all input value; these simplified functions have the restriction that
 * once part of a value complete the matching of a protocol, the key is
 * returned even if other protocols matches the value of the rest of the value;
 * this is usually not a problem in practice
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include "protocols.h"

/*
 * reverse bits in a 32-bit number
 */
uint32_t bitreverse(uint32_t val) {
	int rev = 0, i;
	for (i = 0; i < 32; i++) {
		rev = (rev << 1) | (val & 1);
		val = val >> 1;
	}
	return rev;
}

/*
 * check whether value is within a and b
 */
int within(int value, int a, int b) {
	if (a < b && a <= value && value <= b)
		return 1;
	if (b < a && b <= value && value <= a)
		return 1;
	return 0;
}

/*
 * check whether value is over a and b
 */
int over(int value, int a, int b) {
	if (a > 0 && value < a)
		return 0;
	if (a < 0 && value > a)
		return 0;
	if (b > 0 && value < b)
		return 0;
	if (b < 0 && value > b)
		return 0;
	return 1;
}

/*
 * minimum and maximum in absolute value
 */
int absmin(int a, int b) {
	return abs(a) < abs(b) ? a : b;
}
int absmax(int a, int b) {
	return abs(a) > abs(b) ? a : b;
}

/*
 * first and second, used with BIT and END
 */
int first(int a, int b) {
	(void) b;
	return a;
}
int second(int a, int b) {
	(void) a;
	return b;
}

/*
 * status of sequence parsing
 */
#define complete 0
#define proceed 1
#define fail -1

/*
 * check whether value is consistent with a given position of the sequence;
 * consume the part of value that is valid for that sequence position
 */
int seqwithin(int *value, int *seq, int pos, int max) {
	if (within(*value, seq[pos], seq[pos + 1])) {
		*value = 0;
		return complete;
	}
	if (over(*value, seq[pos], seq[pos + 1]) &&
	   (pos > 0 || abs(*value) < max)) {
		*value -= (seq[pos] + seq[pos + 1]) / 2;
		return proceed;
	}
	*value = 0;
	return fail;
}

/*
 * check whether a sequence is complete (END)
 */
int seqcomplete(int *seq, int pos) {
	return seq[pos] == first(END) && seq[pos + 1] == second(END);
}

/*
 * check whether a position of a sequence is a bit to be recognized (BIT)
 */
int seqbit(int *seq, int pos) {
	return seq[pos] == first(BIT) && seq[pos + 1] == second(BIT);
}

/*
 * initialize a protocol status
 */
void protocol_init(struct protocol_status *status) {
	status->main = 0;
	status->zero = 0;
	status->one = 0;
	status->encoding = 0;
}

/*
 * match a value with a sequence 
 */
int protocol_step(int *value,
		struct protocol *protocol, struct protocol_status *status) {
	int zero_value, one_value;
	int iszero, isone, bit;

	/* reset encoding when parsing starts */
	if (status->main == 0)
		status->encoding = 0;
	
	/* protocol requires a bit at this point: suspend parsing the main
	 * sequence and parse bit 0 and bit 1 in parallel until one succedes or
	 * both fail */
	if (seqbit(protocol->main, status->main)) {

		/* parse the value according to the bit 0 array */
		zero_value = *value;
		if (status->zero != fail) {
			iszero = seqwithin(&zero_value,
				protocol->zero, status->zero, protocol->max);
			if (iszero == fail)
				status->zero = fail;
			else {
				status->zero += 2;
				if (seqcomplete(protocol->zero, status->zero))
					status->zero = complete;
			}
		}

		/* parse the value according to the bit 1 array */
		one_value = *value;
		if (status->one != fail) {
			isone = seqwithin(&one_value, protocol->one,
				status->one, protocol->max);
			if (isone == fail)
				status->one = fail;
			else {
				status->one += 2;
				if (seqcomplete(protocol->one, status->one))
					status->one = complete;
			}
		}

		/* only one leftover of value may come out of this function,
		 * not one for parsing bit 0 and one for parsing bit 1; the bit
		 * that consumes the most of value wins, the other fails */
		*value = absmin(zero_value, one_value);
		if (zero_value != *value)
			status->zero = fail;
		if (one_value != *value)
			status->one = fail;

		/* continue only if either bit 0 or bit 1 may complete */
		if (status->zero == fail && status->one == fail) {
			*value = 0;
			status->zero = 0;
			status->one = 0;
			status->main = 0;
			return fail;
		}

		/* neither bit 0 nor bit 1 is complete: proceed parsing */
		if (status->zero != complete && status->one != complete)
			return proceed;

		/* either bit 0 or bit 1 is complete; if both of them are, but
		 * one left some of value for the next step, the other wins */
		bit = 1; // should not matter; this is for testing
		if (status->zero == complete)
			if (iszero == complete || isone != complete)
				bit = 0;
		if (status->one == complete)
			if (isone == complete || iszero != complete)
				bit = 1;
		status->encoding = (status->encoding << 1) | bit;
		status->zero = 0;
		status->one = 0;
		status->main += 2;
	}

	/* protocol requires a value at this point, not a bit */
	else if (seqwithin(value, protocol->main, status->main, protocol->max)
			!= fail)
		status->main += 2;
	else {
		status->main = 0;
		return fail;
	}

	/* check whether the sequence is complete; this has to be done before
	 * the next value; also, a protocol may end with a bit or may end with
	 * a value in the main sequence: the check is done in both cases */
	if (seqcomplete(protocol->main, status->main)) {
		status->main = 0;
		return complete;
	}
	return proceed;
}

/*
 * completely process a value according to a protocol
 */
int protocol_value(int value,
		struct protocol *protocol, struct protocol_status *status,
		int debug, void callback(uint32_t encoding)) {
	int res, orig, origpos;

	orig = value;
	origpos = status->main;

	/* consume the whole value to match the protocol */
	do {
		if (debug) {
			printf("%s %8d\t", value == orig ? " " : ">", value);
			printf("%d %d %d\t",
				status->main, status->zero, status->one);
		}
		res = protocol_step(&value, protocol, status);
		if (debug)
			printf("%d %d %d\n",
				status->main, status->zero, status->one);
		if (res == complete)
			callback(status->encoding);
	} while (value != 0 && res != fail);

	/* restart: check whether the value is the start of a sequence */
	if (res == fail && origpos != 0)
		return protocol_value(orig, protocol, status, debug, callback);

	return res;
}

/*
 * completely process a value by a protocol; return instead of callback
 */
int protocol_value_return(int value,
		struct protocol *protocol, struct protocol_status *status,
		int debug) {
	int res, orig, origpos;

	orig = value;
	origpos = status->main;

	/* consume the whole value to match the protocol */
	do {
		if (debug) {
			printf("%s %8d\t", value == orig ? " " : ">", value);
			printf("%8d\t", value);
			printf("%d %d %d\t",
				status->main, status->zero, status->one);
		}
		res = protocol_step(&value, protocol, status);
		if (debug)
			printf("%d %d %d\n",
				status->main, status->zero, status->one);
		if (res == complete)
			return 1;
	} while (value != 0 && res != fail);

	/* restart: check whether the value is the start of a sequence */
	if (res == fail && origpos != 0)
		return protocol_value_return(orig, protocol, status, debug);

	return 0;
}

/*
 * print functions
 */
void necxprint(int subprotocol, uint32_t encoding) {
	int device, subdevice, function, subfunction;

	encoding = bitreverse(encoding);

	device =      (encoding >> 0)  & 0xFF;
	subdevice =   (encoding >> 8)  & 0xFF;
	function =    (encoding >> 16) & 0xFF;
	subfunction = (encoding >> 24) & 0xFF;

	printf("\nnec%d", subprotocol);

	if (device == (~subdevice & 0xFF))
		printf(" 0x%02X", device);
	else
		printf(" 0x%02X-0x%02X", device, subdevice);

	if (function == (~subfunction & 0xFF))
		printf(" 0x%02X", function);
	else
		printf(" 0x%02X-0x%02X",
			function, subfunction);
	
	printf("\n");
}
void nec_print(uint32_t encoding) {
	necxprint(1, encoding);
}
void nec2_print(uint32_t encoding) {
	necxprint(2, encoding);
}

void necrepeat_print(uint32_t encoding) {
	(void) encoding;
	printf("\nnec [repeat]\n");
}
void nec2repeat_print(uint32_t encoding) {
	(void) encoding;
	printf("\nnec2 [repeat]\n");
}

void sharp_print(uint32_t encoding) {
	uint32_t reverse, address, function;

	printf("\nsharp 0x%08X ", encoding);
	reverse = bitreverse(encoding);
	address = (reverse >> 18) & 0x1F;
	function = (reverse >> 23) & 0xFF;
	if (! (encoding & 0x1))
		function = ~function & 0xFF;
	printf("0x%02X-0x%02X", address, function);
	if ((encoding & 0x1) == 0)
		printf(" [reversed]");
	printf("\n");
}

void sony12_print(uint32_t encoding) {
	printf("\nsony12 0x%08X\n", bitreverse(encoding) >> (12 + 8));
}

void sony20_print(uint32_t encoding) {
	printf("\nsony20 0x%08X\n", bitreverse(encoding) >> 12);
}

void rc5_print(uint32_t encoding) {
	printf("\nrc5 0x%08X ", encoding);
	printf("0x%02X-", (encoding >> 6) & 0x1F);
	printf("0x%02X\n", (encoding >> 0) & 0x3F);
}

/*
 * the protocols
 */
struct protocol nec_protocol = {
	{ 380, 430, -180, -220,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  20, 30, END },
	{ 20, 30, -20, -30, END },
	{ 20, 30, -70, -80, END },
	430
};

struct protocol necrepeat_protocol = {
	{ 380, 430, -90, -110, 20, 30, END },
	{},
	{},
	430
};

struct protocol nec2_protocol = {
	{ 180, 220, -180, -220,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  20, 30, END },
	{ 20, 30, -20, -30, END },
	{ 20, 30, -70, -80, END },
	220
};

struct protocol nec2repeat_protocol = {
	{ 180, 220, -90, -110, 20, 30, END },
	{},
	{},
	220
};

struct protocol sharp_protocol = {
	{
	  BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, 8, 18, END },
	{ 8, 18, -28, -38, END },
	{ 8, 18, -73, -82, END },
	73
};

struct protocol sony12_protocol = {
	{ 90, 120,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT,
	  -900, -1200, END },
	{ -20, -32, 20, 32, END },
	{ -20, -32, 48, 58, END },
	120
};

struct protocol sony20_protocol = {
	{ 90, 120,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT, BIT, BIT,
	  END },
	{ -20, -32, 20, 32, END },
	{ -20, -32, 48, 58, END },
	120
};

struct protocol rc5_protocol = {
	{ 35, 45,
	  BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT,
	  BIT, BIT, BIT, BIT, BIT, BIT,
	  END },
	{ 35, 45, -35, -45, END },
	{ -35, -45, 35, 45, END },
	45 * 2
};

/*
 * from string to key
 */
struct key *stringtokey(char *string, char sep, char subsep) {
	char sepstring[2], subsepstring[2];
	char *copy, *comma, *token, *sub, *invalid;
	struct key *key;

	sepstring[0] = sep;
	sepstring[1] = '\0';
	subsepstring[0] = subsep;
	subsepstring[1] = '\0';

	copy = strdup(string);
	key = malloc(sizeof(struct key));

	comma = copy;

	token = strsep(&comma, sepstring);

	if (! strcmp(token, "nec"))
		key->protocol = nec;
	else if (! strcmp(token, "nec2"))
		key->protocol = nec2;
	else if (! strcmp(token, "sharp"))
		key->protocol = sharp;
	else if (! strcmp(token, "sony12"))
		key->protocol = sony12;
	else if (! strcmp(token, "sony20"))
		key->protocol = sony20;
	else if (! strcmp(token, "rc5"))
		key->protocol = rc5;

	token = strsep(&comma, sepstring);

	sub = strsep(&token, subsepstring);
	if (sub == NULL) {
		free(key);
		free(copy);
		return NULL;
	}
	key->device = strtol(sub, &invalid, 0);
	if (*invalid != '\0') {
		free(key);
		free(copy);
		return NULL;
	}

	sub = strsep(&token, subsepstring);
	if (sub == NULL)
		key->subdevice = -1;
	else {
		key->subdevice = strtol(sub, &invalid, 0);
		if (*invalid != '\0') {
			free(key);
			free(copy);
			return NULL;
		}
	}

	token = strsep(&comma, sepstring);

	sub = strsep(&token, subsepstring);
	if (sub == NULL) {
		free(key);
		free(copy);
		return NULL;
	}
	key->function = strtol(sub, &invalid, 0);
	if (*invalid != '\0') {
		free(key);
		free(copy);
		return NULL;
	}

	sub = strsep(&token, subsepstring);
	if (sub == NULL)
		key->subfunction = -1;
	else {
		key->subfunction = strtol(sub, &invalid, 0);
		if (*invalid != '\0') {
			free(key);
			free(copy);
			return NULL;
		}
	}

	key->repeat = comma != NULL && ! strcmp(comma, "[repeat]");

	free(copy);
	return key;
}

/*
 * append protocol to string
 */
void appendprotocol(char *string, int protocol) {
	switch (protocol) {
	case nec:
		strcat(string, "nec");
		break;
	case necrepeat:
		strcat(string, "necrepeat");
		break;
	case nec2:
		strcat(string, "nec2");
		break;
	case nec2repeat:
		strcat(string, "nec2repeat");
		break;
	case sharp:
		strcat(string, "sharp");
		break;
	case sony12:
		strcat(string, "sony12");
		break;
	case sony20:
		strcat(string, "sony20");
		break;
	case rc5:
		strcat(string, "rc5");
		break;
	}
}

/*
 * append code and subcode to string
 */
void appendcode(char *string, int code, int sub, char subsep) {
	char number[100];
	if (code != -1) {
		if (code < 0x100)
			sprintf(number, "0x%02X", code);
		else
			sprintf(number, "0x%04X", code);
		strcat(string, number);
	}
	if (sub != -1) {
		sprintf(number, "%c0x%02X", subsep, sub);
		strcat(string, number);
	}
}

/*
 * from key to string
 */
char *keytostring(struct key *key, char sep, char subsep) {
	char *string;
	char sepstring[2];

	if (key == NULL)
		return NULL;

	sepstring[0] = sep;
	sepstring[1] = '\0';

	string = malloc(100);
	string[0] = '\0';
	appendprotocol(string, key->protocol);
	strcat(string, sepstring);
	appendcode(string, key->device, key->subdevice, subsep);
	strcat(string, sepstring);
	appendcode(string, key->function, key->subfunction, subsep);
	if (key->repeat) {
		strcat(string, sepstring);
		strcat(string, "[repeat]");
	}
	return string;
}

/*
 * print a key
 */
void printkey(struct key *key) {
	char *string;
	string = keytostring(key, ' ', '-');
	printf("%s", string);
	free(string);
	return;
}

/*
 * comparison of keys
 */
int keyequal(struct key *a, struct key *b, int comparerepeat) {
	if (a == NULL && b == NULL)
		return 1;
	if (a == NULL || b == NULL)
		return 0;

	if (a->protocol != b->protocol)
		return 0;
	if (a->device != b->device)
		return 0;
	if (a->subdevice != b->subdevice)
		return 0;
	if (a->function != b->function)
		return 0;
	if (a->subfunction != b->subfunction)
		return 0;
	if (a->repeat != b->repeat && comparerepeat)
		return 0;

	return 1;
}

/*
 * from encoding to key
 */
void necsub(uint32_t encoding, int offset, int *code, int *sub) {
	*code = (bitreverse(encoding) >> (0 + offset))  & 0xFF;
	*sub =  (bitreverse(encoding) >> (8 + offset))  & 0xFF;
	if (*code == (~*sub & 0xFF))
		*sub = -1;
}

struct key *necxkey(uint32_t encoding, int protocol) {
	struct key *key;
	key = malloc(sizeof(struct key));
	key->protocol = protocol;
	necsub(encoding, 0, &key->device, &key->subdevice);
	necsub(encoding, 16, &key->function, &key->subfunction);
	key->repeat = 0;
	return key;
}

struct key *necxrepeatkey(uint32_t encoding, int protocol) {
	struct key *key;
	(void) encoding;
	key = malloc(sizeof(struct key));
	key->protocol = protocol;
	key->device =      -1;
	key->subdevice =   -1;
	key->function =    -1;
	key->subfunction = -1;
	key->repeat =      1;
	return key;
}

struct key *neckey(uint32_t encoding) {
	return necxkey(encoding, nec);
}
struct key *necrepeatkey(uint32_t encoding) {
	return necxrepeatkey(encoding, nec);
}
struct key *nec2key(uint32_t encoding) {
	return necxkey(encoding, nec2);
}
struct key *nec2repeatkey(uint32_t encoding) {
	return necxrepeatkey(encoding, nec2);
}

struct key *sharpkey(uint32_t encoding) {
	struct key *key;

	key = malloc(sizeof(struct key));
	key->protocol =    sharp;
	key->device =      (bitreverse(encoding) >> 18) & 0x1F;
	key->subdevice =   -1;
	key->function =    (bitreverse(encoding) >> 23) & 0xFF;
	key->subfunction = -1;
	if (! (encoding & 0x1))
		key->function = ~key->function & 0xFF;
	key->repeat = (encoding & 0x1) == 0;
	return key;
}

struct key *sonykey(int protocol, uint32_t reversed) {
	struct key *key;

	key = malloc(sizeof(struct key));
	key->protocol = protocol;
	key->device = (reversed >> 7) & 0x1F;
	key->subdevice = reversed >> (7 + 5);
	key->function = reversed & 0x7F;
	key->subfunction = 0;
	key->repeat = 0;

	return key;
}
struct key *sony12key(uint32_t encoding) {
	return sonykey(sony12, bitreverse(encoding) >> (12 + 8));
}
struct key *sony20key(uint32_t encoding) {
	return sonykey(sony20, bitreverse(encoding) >> 12);
}

struct key *rc5key(uint32_t encoding) {
	struct key *key;

	key = malloc(sizeof(struct key));
	key->protocol =    rc5;
	key->device =      (encoding >> 6) & 0x1F;
	key->subdevice =   -1;
	key->function =    (encoding >> 0) & 0x3F;
	key->subfunction = -1;
	key->repeat =      (encoding >> 11) & 0x01;		// FIXME
	return key;
}

/*
 * status of all protocols
 */
struct protocols_status {
	struct protocol_status nec;
	struct protocol_status necinverted;
	struct protocol_status necrepeat;
	struct protocol_status necrepeatinverted;
	struct protocol_status nec2;
	struct protocol_status nec2inverted;
	struct protocol_status nec2repeat;
	struct protocol_status nec2repeatinverted;
	struct protocol_status sharp;
	struct protocol_status sharpinverted;
	struct protocol_status sony12;
	struct protocol_status sony12inverted;
	struct protocol_status sony20;
	struct protocol_status sony20inverted;
	struct protocol_status rc5;
	struct protocol_status rc5inverted;
	int debug;
};

/*
 * init all protocols
 */
void *protocols_init(int debug) {
	struct protocols_status *status;
	status = malloc(sizeof(struct protocols_status));
	status->debug = debug;
	protocol_init(&status->nec);
	protocol_init(&status->necinverted);
	protocol_init(&status->necrepeat);
	protocol_init(&status->necrepeatinverted);
	protocol_init(&status->nec2);
	protocol_init(&status->nec2inverted);
	protocol_init(&status->nec2repeat);
	protocol_init(&status->nec2repeatinverted);
	protocol_init(&status->sharp);
	protocol_init(&status->sharpinverted);
	protocol_init(&status->sony12);
	protocol_init(&status->sony12inverted);
	protocol_init(&status->sony20);
	protocol_init(&status->sony20inverted);
	protocol_init(&status->rc5);
	protocol_init(&status->rc5inverted);
	return status;
}

/*
 * from protocol to key
 */
#define PROTOCOL_KEY(protocol, protocolstatus, status, conversion)	\
	res = protocol_value_return(value,				\
		& protocol ## _protocol,				\
		&status->protocolstatus,				\
		status->debug == protocol ## _debug);			\
	if (res)							\
		return conversion(status->protocolstatus.encoding);	\
	res = protocol_value_return(-value,				\
		& protocol ## _protocol,				\
		&status->protocolstatus ## inverted,			\
		status->debug == protocol ## inverted_debug);		\
	if (res) 							\
		return conversion(status->protocolstatus ## inverted.encoding);

/*
 * parse a value according to a protocol or inverse protocol
 */
struct key *protocols_value(int value, void *internal) {
	struct protocols_status *status;
	int res;

	status = (struct protocols_status *) internal;

	PROTOCOL_KEY(nec, nec, status, neckey)
	PROTOCOL_KEY(necrepeat, necrepeat, status, necrepeatkey)
	PROTOCOL_KEY(nec2, nec2, status, nec2key)
	PROTOCOL_KEY(nec2repeat, nec2repeat, status, nec2repeatkey)
	PROTOCOL_KEY(sharp, sharp, status, sharpkey)
	PROTOCOL_KEY(sony12, sony12, status, sony12key)
	PROTOCOL_KEY(sony20, sony20, status, sony20key)
	PROTOCOL_KEY(rc5, rc5, status, rc5key)

	return NULL;
}

/*
 * finish parsing all protocols
 */
int protocols_end(void *internal) {
	free(internal);
	return 0;
}

