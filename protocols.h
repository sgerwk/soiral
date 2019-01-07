/*
 * protocols.h
 *
 * parse the runlength encoding of a signal from a remote
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
 * description of a protocol
 */
struct protocol {
	int main[100];
	int zero[20];
	int one[20];
	int max;
};

/*
 * status of parsing according to a protocol
 */
struct protocol_status {
	int main;
	int zero;
	int one;
	uint32_t encoding;
};

/*
 * special values for a sequence: a bit, the end
 */
#define BIT 1,1
#define END 0,0

/*
 * initialize a protocol status
 */
void protocol_init(struct protocol_status *status);

/*
 * process a value according to a protocol
 */
int protocol_value(int value,
		struct protocol *protocol, struct protocol_status *status,
		int debug, void callback(uint32_t encoding));

/*
 * status variables for a protocol
 */
#define PROTOCOL_VARS(protocol)						\
	struct protocol_status protocol ## _status;			\
	struct protocol_status protocol ## inverted_status;

/*
 * initialize a protocol and its inverse
 */
#define PROTOCOL_START(protocol)					\
	protocol_init(& protocol ## _status);				\
	protocol_init(& protocol ## inverted_status);

/*
 * process a value and its inverse according to a protocol
 */
#define PROTOCOL_PROCESS(value, protocol)				\
	protocol_value(value,						\
		& protocol ## _protocol,				\
		& protocol ## _status,					\
		debug == protocol ## _debug,				\
		protocol ## _print);					\
	if (inversion)							\
		protocol_value(-value,					\
			& protocol ## _protocol,			\
			& protocol ## inverted_status,			\
			debug == protocol ## inverted_debug,		\
			protocol ## _print);


/*
 * values for debugging each protocol and its inversion
 */
enum protocol_debug {
	nec_debug = 1,
	necinverted_debug,
	necrepeat_debug,
	necrepeatinverted_debug,
	nec2_debug,
	nec2inverted_debug,
	nec2repeat_debug,
	nec2repeatinverted_debug,
	sharp_debug,
	sharpinverted_debug,
	sony12_debug,
	sony12inverted_debug,
	sony20_debug,
	sony20inverted_debug,
	rc5_debug,
	rc5inverted_debug,
};

/*
 * the protocols
 */
enum protocols {
	nec,
	necrepeat,
	nec2,
	nec2repeat,
	sharp,
	sony12,
	sony20,
	rc5,
};
struct protocol nec_protocol;
struct protocol necrepeat_protocol;
struct protocol nec2_protocol;
struct protocol nec2repeat_protocol;
struct protocol sharp_protocol;
struct protocol sony12_protocol;
struct protocol sony20_protocol;
struct protocol rc5_protocol;

/*
 * print the encoding for each protocol
 */
void nec_print(uint32_t encoding);
void nec2_print(uint32_t encoding);
void necrepeat_print(uint32_t encoding);
void nec2repeat_print(uint32_t encoding);
void sharp_print(uint32_t encoding);
void sony12_print(uint32_t encoding);
void sony20_print(uint32_t encoding);
void rc5_print(uint32_t encoding);

/*
 * a key
 */
struct key {
	int protocol;
	int device;
	int subdevice;
	int function;
	int subfunction;
	int repeat;
};
struct key *stringtokey(char *string, char sep, char subsep);
void appendprotocol(char *string, int protocol);
void appendcode(char *string, int code, int sub, char subsep);
char *keytostring(struct key *key, char sep, char subsep);
void printkey(struct key *key);
int keyequal(struct key *a, struct key *b, int comparerepeat);

/*
 * parse all protocols and their inverse at the same time
 */
void *protocols_init(int debug);
struct key *protocols_value(int value, void *internal);
int protocols_end(void *internal);

