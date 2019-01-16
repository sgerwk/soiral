/*
 * irblast.c
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
 * emit IR codes for an address and function 
 *
 * since soundcards cannot produce the required frequency, output a square wave
 * at 1/3 of the frequency instead: square waves have a component at 3 times
 * their frequency
 *
 * this program does not raise volume by itself, do that with alsamixer
 *
 * todo:
 * - sony12 and sony15 protocols
 * - rc5 toggle
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

/*
 * carrier
 */
int16_t hold =        10;
int16_t left_even =   INT16_MAX;
int16_t left_odd =   -INT16_MAX;
int16_t right_even = -INT16_MAX;
int16_t right_odd =   INT16_MAX;

int ensurelength = 0;
int dutycycle = 50;
double timefactor = 1.0, ontimefactor = 1.0, offtimefactor = 1.0;
int timebalancing = 1, valuetimebalancing = 0;
int markend = 0;
int multiplier = 100; // time granularity: 10 means 1/10 microsecond
int startup = 0;
int textout = 0;

/*
 * timing statistics
 */
int minovertime;
int maxovertime;
int debugtiming = 0;

/*
 * protocols
 */
enum protocol {
	protocol_nec,
	protocol_nec2,
	protocol_sharp,
	protocol_rc5,
	protocol_sony12,
	protocol_sony15,
	protocol_sony20,
	protocol_hold,
	protocol_test,
	protocol_none
};

/*
 * open and configure sound output
 */
snd_pcm_t *audio(char *name, unsigned int *rate) {
	int res;
	snd_pcm_t *handle;
	snd_pcm_info_t *info;
	snd_pcm_hw_params_t *params;
	unsigned int num, c;
	snd_pcm_access_t a;
	int den;
	snd_pcm_uframes_t frames = 32;

	res = snd_pcm_open(&handle, name, SND_PCM_STREAM_PLAYBACK, 0);
	if (res < 0) {
		printf("snd_pcm_open: %s\n", strerror(-res));
		return NULL;
	}

	snd_pcm_info_malloc(&info);
	res = snd_pcm_info(handle, info);
	if (res < 0) {
		printf("snd_pcm_info: %s\n", strerror(-res));
		return NULL;
	}
	printf("name: %s\n", snd_pcm_info_get_name(info));
	snd_pcm_info_free(info);

	snd_pcm_hw_params_malloc(&params);
	snd_pcm_hw_params_any(handle, params);
	printf("requested sample rate: %d\n", *rate);
	res = snd_pcm_hw_params_set_rate_near(handle, params, rate, NULL);
	if (res < 0)
		printf("set sample rate: %s\n", strerror(-res));
	snd_pcm_hw_params_set_access(handle, params,
					SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16);
	res = snd_pcm_hw_params_set_period_size_near(handle, params,
			&frames, NULL);
	if (res < 0)
		fprintf(stderr, "set period size: %s\n", strerror(-res));
	res = snd_pcm_hw_params_set_channels(handle, params, 2);
	if (res < 0)
		printf("set channels: %s\n", strerror(-res));

	res = snd_pcm_hw_params(handle, params);
	if (res < 0) {
		printf("set hw parameters: %s\n", strerror(-res));
		return NULL;
	}

	snd_pcm_hw_params_current(handle, params);

	snd_pcm_hw_params_get_rate(params, &num, &den);
	printf("sample rate: %d/%d\n", num, den);
	if (num != (unsigned) *rate) {
		printf("ERROR: actual sample rate %d, ", num);
		printf("requested %d\n", *rate);
		*rate = num;
	}

	snd_pcm_hw_params_get_channels(params, &c);
	printf("channels: %d\n", c);
	if (c != 2)
		printf("ERROR: %d channels, requested 2\n", c);

	snd_pcm_hw_params_get_access(params, &a);
	if (a != SND_PCM_ACCESS_RW_INTERLEAVED)
		printf("ERROR: interleaved access not allowed (%d)\n", a);

	snd_pcm_hw_params_free(params);

	res = snd_pcm_prepare(handle);
	if (res < 0) {
		printf("prepare: %s\n", strerror(-res));
		return NULL;
	}

	return handle;
}

/*
 * maximal buffer size
 */
#define MAXLEN 80000

/*
 * switch carrier on or off for the given duration
 *
 * the device has a simple ir led between left(+) and right (-); therefore,
 * left=+INT16_MAX,right=-INT16_MAX is maximum power; zero power could be
 * realized by 0 on both channels, but this would make the signal average
 * greater than zero, and this dc component would be progressively filtered
 * out, with a consequent decrease of power
 *
 * variable overtime keeps track of how long the last sample took over the
 * requested duration
 */
void carrier(int value, int duration, int *overtime,
		int period, int sample,
		int16_t *buffer, int *pos) {
	int t, equaltarget, target, boundary, start, o;

	start = *overtime;
	equaltarget = multiplier * duration * timefactor - sample / 2;
	target = multiplier * duration * timefactor *
		(value == 0 ? offtimefactor : ontimefactor) - sample / 2;
	boundary = period * dutycycle / 100;
	if (dutycycle != 0 && boundary < sample)
		boundary = sample;
	if (dutycycle != 100 && boundary > period - sample)
		boundary = period - sample;

	for (t = 0;
	     t < startup * multiplier && value && t < target - *overtime;
	     t += sample) {
		if (textout && t % (20 * sample) == 0)
			printf("*");
		buffer[(*pos)++] = left_even;
		buffer[(*pos)++] = right_even;
	}

	for (t = t;
	     t < target - *overtime ||
		(ensurelength && value && t % period < boundary);
	     t += sample) {
		if (textout && t % (20 * sample) == 0)
			printf("%s", value ? "*" : "_");

		// left channel
		buffer[(*pos)++] =
			value == 0 ?
				hold :
				t % period < boundary ? left_even : left_odd;

		// right channel
		buffer[(*pos)++] =
			value == 0 ?
				hold :
				t % period < boundary ? right_even : right_odd;

		// check overflow
		if (*pos >= MAXLEN - 10) {
			printf("buffer overflow, ignored carrier switch\n");
			return;
		}
	}

	o = t - (valuetimebalancing ? equaltarget : target);
	if (o < minovertime)
		minovertime = o;
	if (o > maxovertime)
		maxovertime = o;

	if (timebalancing)
		*overtime = o;

	if (debugtiming) {
		printf("start: %-4d ", start);
		printf("target: %-6d ", (int) (duration * timefactor));
		printf("produced: %-6d ", t - start);
		printf("overtime: %-4d\n", o);
	}
}

/*
 * nec protocol
 *
 *	lead:		9000   usecs 1
 *	separator:	4500   usecs 0
 *	mark:		 562.5 usecs 1
 *	zero:		 562.5 usecs 0
 *	one:		1687.5 usecs 0
 *
 *	code:
 *		lead - separator - address - function - ~function - mark
 *		each bit is mark-zero or mark-one, from lsb to msb
 *
 *	repeat:
 *		lead - separator/2 - mark
 */
int nec_frequency = 38000;
int nec2_frequency = 38000;

int necxcode(int subprot, int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	uint32_t encoding;
	int pos;
	int overtime;
	int i, bit;

	encoding = 0;
	encoding |= (~function & 0xFF) << 24;
	encoding |= (function & 0xFF) << 16;
	encoding |= ((subdevice == -1 ? ~device : subdevice) & 0xFF) << 8;
	encoding |= (device & 0xFF) << 0;

	pos = 0;
	overtime = 0;

	carrier(1, subprot == 2 ? 4500 : 9000, &overtime,
		period, sample, buffer, &pos);
	carrier(0, 4500, &overtime, period, sample, buffer, &pos);

	for (i = 0; i < 32; i++) {
		bit = (encoding & (1 << i)) ? 2250 : 1125;
		carrier(1, 562, &overtime, period, sample, buffer, &pos);
		carrier(0, bit - 562, &overtime, period, sample, buffer, &pos);
	}

	carrier(1, 562, &overtime, period, sample, buffer, &pos);
	carrier(0, 108000 - sample * pos / multiplier / 2,
		&overtime, period, sample, buffer, &pos);

	return pos / 2;
}
int nec_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return necxcode(1, device, subdevice, function,
		period, sample, buffer);
}
int nec2_code(int device, int subdevice, uint32_t function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return necxcode(2, device, subdevice, function,
		period, sample, buffer);
}

int necxrepeat(int subprot, int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	int pos;
	int overtime;

	(void) device;
	(void) subdevice;
	(void) function;

	pos = 0;
	overtime = 0;

	carrier(1, subprot == 2 ? 4500 : 9000, &overtime,
		period, sample, buffer, &pos);
	carrier(0, 4500 / 2, &overtime, period, sample, buffer, &pos);
	carrier(1, 562, &overtime, period, sample, buffer, &pos);
	carrier(0, 108000 - sample * pos / multiplier / 2,
		&overtime, period, sample, buffer, &pos);

	return pos / 2;
}
int nec_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return necxrepeat(1, device, subdevice, function,
		period, sample, buffer);
}
int nec2_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return necxcode(2, device, subdevice, function,
		period, sample, buffer);
}

/*
 * sharp protocol
 *
 *	separator:	40000 usecs 0
 *	mark:		  320 usecs 1
 *	one:		 1680 usecs 0
 *	zero:		  680 usecs 0
 *
 *	code:
 *		address - function - 1 - 0 - mark - separator -
 *		address - ~function - 0 - 1 - mark
 */
int sharp_frequency = 38000;

int sharp_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	int i, pos, bit, overtime;

	(void) subdevice;

	pos = 0;
	overtime = 0;

	for (i = 0; i < 5; i++) {
		carrier(1, 320, &overtime, period, sample, buffer, &pos);
		bit = (device & (1 << i)) ? 1680 : 680;
		carrier(0, bit, &overtime, period, sample, buffer, &pos);
	}
	for (i = 0; i < 8; i++) {
		carrier(1, 320, &overtime, period, sample, buffer, &pos);
		bit = (function & (1 << i)) ? 1680 : 680;
		carrier(0, bit, &overtime, period, sample, buffer, &pos);
	}
	carrier(1, 320, &overtime, period, sample, buffer, &pos);
	carrier(0, 1680, &overtime, period, sample, buffer, &pos);
	carrier(1, 320, &overtime, period, sample, buffer, &pos);
	carrier(0, 680, &overtime, period, sample, buffer, &pos);
	carrier(1, 320, &overtime, period, sample, buffer, &pos);

	carrier(0, 40000, &overtime, period, sample, buffer, &pos);

	for (i = 0; i < 5; i++) {
		carrier(1, 320, &overtime, period, sample, buffer, &pos);
		bit = (device & (1 << i)) ? 1680 : 680;
		carrier(0, bit, &overtime, period, sample, buffer, &pos);
	}
	for (i = 0; i < 8; i++) {
		carrier(1, 320, &overtime, period, sample, buffer, &pos);
		bit = (~function & (1 << i)) ? 1680 : 680;
		carrier(0, bit, &overtime, period, sample, buffer, &pos);
	}
	carrier(1, 320, &overtime, period, sample, buffer, &pos);
	carrier(0, 680, &overtime, period, sample, buffer, &pos);
	carrier(1, 320, &overtime, period, sample, buffer, &pos);
	carrier(0, 1680, &overtime, period, sample, buffer, &pos);
	carrier(1, 320, &overtime, period, sample, buffer, &pos);

	carrier(0, 40000, &overtime, period, sample, buffer, &pos);

	return pos / 2;
}

int sharp_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return sharp_code(device, subdevice, function, period, sample, buffer);
}

/*
 * sony protocol
 *
 *	lead:		2400 usecs 1
 *	space:		 600 usecs 0
 *	zero:		 600 usecs 1
 *	one:		1200 usecs 1
 *
 *	code:
 *		lead - space - function - address
 */
int sony_frequency = 40000;

int sony_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	int i, pos, bit, overtime;

	pos = 0;
	overtime = 0;

	carrier(1, 2400, &overtime, period, sample, buffer, &pos);
	carrier(0, 600, &overtime, period, sample, buffer, &pos);

	for (i = 0; i < 7; i++) {
		bit = (function & (1 << i)) ? 1200 : 600;
		carrier(1, bit, &overtime, period, sample, buffer, &pos);
		carrier(0, 600, &overtime, period, sample, buffer, &pos);
	}

	for (i = 0; i < 5; i++) {
		bit = (device & (1 << i)) ? 1200 : 600;
		carrier(1, bit, &overtime, period, sample, buffer, &pos);
		carrier(0, 600, &overtime, period, sample, buffer, &pos);
	}

	for (i = 0; i < 8; i++) {
		bit = (subdevice & (1 << i)) ? 1200 : 600;
		carrier(1, bit, &overtime, period, sample, buffer, &pos);
		carrier(0, 600, &overtime, period, sample, buffer, &pos);
	}

	carrier(0, 45000 - sample * pos / multiplier / 2,
		&overtime, period, sample, buffer, &pos);

	return pos / 2;
}

int sony_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return sony_code(device, subdevice , function, period, sample, buffer);
}

/*
 * rc5 protocol
 *
 *	mark:  889 usecs 1
 *	space: 889 usecs 0
 *
 *	bits:
 *		0 = mark - space
 *		1 = space - mark
 *
 *	code:
 *		1 - 1 - toggle - address (msb first) - function (msb first)
 *		toggle changes every time a function is released
 */
int rc5_frequency = 36000;
int rc5_toggle = 0;

int rc5_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	int i, pos, overtime;

	(void) subdevice;

	pos = 0;
	overtime = 0;

	carrier(0, 889, &overtime, period, sample, buffer, &pos);
	carrier(1, 889, &overtime, period, sample, buffer, &pos);
	carrier(0, 889, &overtime, period, sample, buffer, &pos);
	carrier(1, 889, &overtime, period, sample, buffer, &pos);
	carrier(rc5_toggle ? 0 : 1, 889, &overtime,
		period, sample, buffer, &pos);
	carrier(rc5_toggle ? 1 : 0, 889, &overtime,
		period, sample, buffer, &pos);
	rc5_toggle = 1 - rc5_toggle;

	for (i = 4; i >= 0; i--) {
		carrier((device & (1 << i)) ? 0 : 1, 889, &overtime,
			period, sample, buffer, &pos);
		carrier((device & (1 << i)) ? 1 : 0, 889, &overtime,
			period, sample, buffer, &pos);
	}

	for (i = 5; i >= 0; i--) {
		carrier((function & (1 << i)) ? 0 : 1, 889, &overtime,
			period, sample, buffer, &pos);
		carrier((function & (1 << i)) ? 1 : 0, 889, &overtime,
			period, sample, buffer, &pos);
	}

	carrier(0, 114000 - sample * pos / multiplier / 2,
		&overtime, period, sample, buffer, &pos);

	return pos / 2;
}

int rc5_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	rc5_toggle = 1 - rc5_toggle;
	return rc5_code(device, subdevice, function, period, sample, buffer);
}

/*
 * hold protocol: hold the line (device=duration, function=carrier value)
 */
int hold_frequency = -1;
int hold_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	int pos, overtime;
	(void) subdevice;
	pos = 0;
	overtime = 0;
	carrier(function, device, &overtime, period, sample, buffer, &pos);
	return pos / 2;
}
int hold_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return hold_code(device, subdevice, function, period, sample, buffer);
}

/*
 * test protocol
 *
 * perform some kind of testing
 * which one depends on address, and function is a parameter
 */
int test_frequency = 38000;

int test_code(int device, int subdevice, int function,
		int period, int rate, int16_t buffer[MAXLEN]) {
	int i, t, pos, overtime;

	(void) subdevice;

	pos = 0;
	overtime = 0;

	switch (device) {
	case 0:
		t = 10 * function;
		carrier(0, t, &overtime, period, rate, buffer, &pos);
		printf("_%d_ ", pos);
		carrier(1, t, &overtime, period, rate, buffer, &pos);
		printf("^%d^ ", pos);
		carrier(0, t, &overtime, period, rate, buffer, &pos);
		printf("_%d_ ", pos);
		carrier(1, t, &overtime, period, rate, buffer, &pos);
		printf("^%d^ ", pos);
		carrier(0, t, &overtime, period, rate, buffer, &pos);
		printf("_%d_ ", pos);
		break;
	case 1:
		for (i = 0; i < 40; i++) {
			t = function;
			carrier(1, t, &overtime, period, rate, buffer, &pos);
			printf("^%d^ ", pos);
			carrier(0, t, &overtime, period, rate, buffer, &pos);
			printf("_%d_ ", pos);
		}
		carrier(0, 400, &overtime, period, rate, buffer, &pos);
		carrier(1, 800, &overtime, period, rate, buffer, &pos);
		carrier(0, 400, &overtime, period, rate, buffer, &pos);
		carrier(1, 800, &overtime, period, rate, buffer, &pos);
		carrier(0, 400, &overtime, period, rate, buffer, &pos);
		carrier(1, 800, &overtime, period, rate, buffer, &pos);
		carrier(0, 400, &overtime, period, rate, buffer, &pos);
		break;
	case 2:
		// check LED polarity: flash twice if left=+ right=-,
		left_even = INT16_MAX;
		left_odd = INT16_MAX;
		right_even = -INT16_MAX;
		right_odd = -INT16_MAX;
		carrier(1, 40000, &overtime, period, rate, buffer, &pos);
		carrier(0, 300000, &overtime, period, rate, buffer, &pos);
		carrier(1, 40000, &overtime, period, rate, buffer, &pos);

		// check LED polarity: flash once if left=- right=+,
		left_even = -INT16_MAX;
		left_odd = -INT16_MAX;
		right_even = INT16_MAX;
		right_odd = INT16_MAX;
		carrier(0, 300000, &overtime, period, rate, buffer, &pos);
		carrier(1, 40000, &overtime, period, rate, buffer, &pos);

		break;
	}

	carrier(0, 1000, &overtime, period, rate, buffer, &pos);

	return pos / 2;
}

int test_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return test_code(device, subdevice , function, period, sample, buffer);
}

/*
 * no protocol
 */
int none_frequency = -1;
int none_code(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	(void) device;
	(void) subdevice;
	(void) function;
	(void) period;
	(void) sample;
	(void) buffer;
	return -1;
}
int none_repeat(int device, int subdevice, int function,
		int period, int sample, int16_t buffer[MAXLEN]) {
	return none_code(device, subdevice, function, period, sample, buffer);
}

/*
 * send device,subdevice,function to the sound card
 */
int sendcode(snd_pcm_t *handle,
		int period, int sample,
		enum protocol protocol,
		int device, int subdevice, int function, int repeat) {
	int res, len;
	int16_t buffer[MAXLEN];

	minovertime = 1000;
	maxovertime = -1000;

	if (markend > 0)
		memset(buffer, -64, MAXLEN * sizeof(int16_t));

	switch (protocol) {
	case protocol_nec:
		len = repeat ?
			nec_repeat(device, subdevice, function,
				period, sample, buffer) :
			nec_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_nec2:
		len = repeat ?
			nec2_repeat(device, subdevice, function,
				period, sample, buffer) :
			nec2_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_sharp:
		len = repeat ?
			sharp_repeat(device, subdevice, function,
				period, sample, buffer) :
			sharp_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_rc5:
		len = repeat ?
			rc5_repeat(device, subdevice, function,
				period, sample, buffer) :
			rc5_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_sony12:	// TBD
	case protocol_sony15:	// TBD
	case protocol_sony20:
		len = repeat ?
			sony_repeat(device, subdevice, function,
				period, sample, buffer) :
			sony_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_hold:
		len = repeat ?
			hold_repeat(device, subdevice, function,
				period, sample, buffer) :
			hold_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_test:
		len = repeat ?
			test_repeat(device, subdevice, function,
				period, sample, buffer) :
			test_code(device, subdevice, function,
				period, sample, buffer);
		break;
	case protocol_none:
		return -1;
	}
	if (textout)
		printf("\n");
	printf("audio frames: %d\t", len);
	printf("%d <= overtime <= %d\n", minovertime, maxovertime);
	if (len <= 0)
		return len;

	res = snd_pcm_writei(handle, buffer, len + markend);
	if (res < 0) {
		printf("writei: %s\n", strerror(-res));
		return -1;
	}
	else if (res < len + markend)
		printf("short write: %d < %d\n", res, len + markend);

	return 0;
}

/*
 * usage
 */
void usage() {
	printf("emit remote infrared codes via sound card\n");
	printf("usage:\n");
	printf("\tirblast [-d audiodevice] [-r rate] [-f frequency]\n");
	printf("\t        [-n value] [-s duration] [-c dutycycle]");
	printf(" [-t factor] [-o factor]\n");
	printf("\t        [-v] [-b] [-i] [-l] [-w] [-e] [-a]\n");
	printf("\t        protocol device subdevice function");
	printf(" [times [repetitions]]\n");
	printf("\t\t-d audiodevice\taudio device (e.g., hw:1)\n");
	printf("\t\t-r rate\t\tset audio device at this samplerate\n");
	printf("\t\t-f frequency\toverride protocol frequency\n");
	printf("\t\t-n value\tcarrier off value\n");
	printf("\t\t-s duration\tinitial silence time\n");
	printf("\t\t-c percentage\tduty cycle\n");
	printf("\t\t-g startup\tfirst pulse length\n");
	printf("\t\t-t factor\ttime scaling\n");
	printf("\t\t-o factor\tcarrier-on time scaling\n");
	printf("\t\t-v\t\tcompensate carrier-on time scaling\n");
	printf("\t\t-b\t\tdisable time quantization error balancing\n");
	printf("\t\t-l\t\tensure carrier-on interval length\n");
	printf("\t\t-i\t\tinverted adapter\n");
	printf("\t\t-w\t\tstart with a 3-seconds pause (for loopback)\n");
	printf("\t\t-e\t\tmark the end of the code (for testing)\n");
	printf("\t\t-a\t\tprint an ascii representation of the signal\n");
	printf("\t\tprotocol\tnec, nec2, rc5, sharp, sony20, test\n");
	printf("\t\tdevice\t\taddress of device, e.g., $((0x12))\n");
	printf("\t\tsubdevice\tsecond part of address, or \"none\"\n");
	printf("\t\tfunction\tfunction, e.g., $((0x50))\n");
	printf("\t\ttimes\t\tsend the code this many times\n");
	printf("\t\trepetitions\tsend repetitions codes afterwards\n");
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	int opt;
	int delay = 0, inverted = 0;
	int optrate = -1, optfrequency = -1, silence = 80000;
	char *outdevice = "hw:0";
	snd_pcm_t *handle;
	enum protocol protocol;
	int device, subdevice, nosubdevice, function;
	int times = 1, rtimes = 0;
	int16_t temp;
	unsigned int frequency, divisor, rate, period, sample;
	int i, res;

				/* arguments */

	while (-1 != (opt = getopt(argc, argv, "d:r:f:n:s:c:g:t:o:vbliweah")))
		switch (opt) {
		case 'd':
			outdevice = optarg;
			break;
		case 'r':
			optrate = atoi(optarg);
			break;
		case 'f':
			optfrequency = atoi(optarg);
			break;
		case 'n':
			hold = atoi(optarg);
			break;
		case 's':
			silence = atoi(optarg);
			break;
		case 'c':
			dutycycle = atoi(optarg);
			break;
		case 'g':
			startup = atoi(optarg);
			break;
		case 't':
			timefactor = atof(optarg);
			break;
		case 'o':
			ontimefactor = atof(optarg);
			if (ontimefactor < 1) {
				offtimefactor = 1 / ontimefactor;
				ontimefactor = 1;
			}
			break;
		case 'v':
			valuetimebalancing = 1;
			break;
		case 'b':
			timebalancing = 0;
			break;
		case 'l':
			ensurelength = 1;
			break;
		case 'i':
			inverted = 1;
			break;
		case 'w':
			delay = 3;
			break;
		case 'e':
			markend = 20;
			break;
		case 'a':
			textout = 1;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		default:
			usage();
			return EXIT_FAILURE;
		}

	if (argc - optind < 4) {
		printf("not enough arguments\n");
		usage();
		return EXIT_FAILURE;
	}
	if (! strcmp(argv[optind], "nec"))
		protocol = protocol_nec;
	else if (! strcmp(argv[optind], "nec2"))
		protocol = protocol_nec2;
	else if (! strcmp(argv[optind], "sharp"))
		protocol = protocol_sharp;
	else if (! strcmp(argv[optind], "rc5"))
		protocol = protocol_rc5;
	else if (! strcmp(argv[optind], "sony20"))
		protocol = protocol_sony20;
	else if (! strcmp(argv[optind], "test"))
		protocol = protocol_test;
	else
		protocol = protocol_none;
	device = atoi(argv[optind + 1]);
	if (! strcmp(argv[optind + 2], "none")) {
		nosubdevice = 1;
		subdevice = -1;
	}
	else {
		nosubdevice = 0;
		subdevice = atoi(argv[optind + 2]);
	}
	function = atoi(argv[optind + 3]);
	if (argc - optind >= 5)
		times = atoi(argv[optind + 4]);
	if (argc - optind >= 6)
		rtimes = atoi(argv[optind + 5]);

	if (protocol == protocol_none) {
		printf("unsuported protocol\n");
		usage();
		exit(EXIT_FAILURE);
	}
	if (nosubdevice)
		printf("device: 0x%02X function: 0x%04X\n", device, function);
	else
		printf("device: 0x%02X-0x%02X function: 0x%04X\n",
			device, subdevice, function);
	printf("times: %d rtimes: %d\n", times, rtimes);

				/* open audio, determine sample rate */

	rate = optrate > 0 ? optrate : 2000000;
	handle = audio(outdevice, &rate);
	if (handle == NULL)
		exit(EXIT_FAILURE);
	sample = 1000000 * multiplier / rate;

				/* carrier frequency */

	if (optfrequency > 0)
		frequency = optfrequency;
	else if (optfrequency == 0) {
		left_even = -INT16_MAX;
		left_odd =  -INT16_MAX;
		right_even = INT16_MAX;
		right_odd =  INT16_MAX;
		frequency = rate / 2;
	}
	else
		switch (protocol) {
		case protocol_nec:
			frequency = nec_frequency;
			break;
		case protocol_nec2:
			frequency = nec2_frequency;
			break;
		case protocol_sharp:
			frequency = sharp_frequency;
			break;
		case protocol_rc5:
			frequency = rc5_frequency;
			break;
		case protocol_sony12:
		case protocol_sony15:
		case protocol_sony20:
			frequency = sony_frequency;
			break;
		case protocol_hold:
			frequency = hold_frequency;
			break;
		case protocol_test:
			frequency = test_frequency;
			break;
		case protocol_none:
			frequency = none_frequency;
			break;
		}

	// if the frequency is close enough to rate/2, aim at that
	// otherwise, use the frequency properties of square waves
	for (divisor = 1;
	     frequency / divisor * 2 > rate * 1.2;
	     divisor += 2) {
	}
	printf("divisor: %d\n", divisor);
	frequency /= divisor;
	if (frequency * 2 > rate)
		frequency = rate / 2;
	period = 1000000 * multiplier / frequency;

	if (inverted) {
		temp = left_even;
		left_even = right_even;
		right_even = temp;
		temp = left_odd;
		left_odd = right_odd;
		right_odd = temp;
	}

				/* print parameters */

	printf("sample rate: %d samples per second\n", rate);
	printf("sample duration: %d.%d microseconds\n",
		sample / multiplier, sample % multiplier);
	printf("carrier frequency: %d Hertz\n", 1000000 * multiplier / period);
	printf("carrier period: %d.%d microseconds\n",
		period / multiplier, period % multiplier);
	printf("timescales: all %g, ", timefactor);
	printf("carrier-on %g, ", ontimefactor);
	printf("carrier-off %g\n", offtimefactor);
	printf("startup interval: %d microseconds\n", startup);
	printf("duty cycle: %d%%\n", dutycycle);
	printf("inverted: %s\n", inverted ? "yes" : "no");

				/* wait, if -l is passed */

	sleep(delay);

				/* send */
	
	sendcode(handle, period, sample, protocol_hold, silence, 0, 0, 0);
	for (i = 0; i < times; i++)
		sendcode(handle, period, sample,
			protocol, device, subdevice, function, 0);
	for (i = 0; i < rtimes; i++)
		sendcode(handle, period, sample,
			protocol, device, subdevice, function, 1);

				/* close */

	res = snd_pcm_drain(handle);
	if (res < 0)
		printf("drain: %s\n", strerror(-res));
	res = snd_pcm_close(handle);
	if (res < 0) {
		printf("close: %s\n", strerror(-res));
		exit(EXIT_FAILURE);
	}

	printf("\n");
	return EXIT_SUCCESS;
}

