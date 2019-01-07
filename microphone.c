/*
 * microphone.c
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
 * read data from microphone
 *
 * todo:
 * - work on frequency allowed by the soundcard
 * - set digital mixer control to 0dB
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <alsa/asoundlib.h>
#include "filters.h"

/*
 * set mixer to maximal capture level
 */
int maxmixer(char *name) {
	snd_mixer_t *handle;
	snd_mixer_selem_id_t *sid;
	snd_mixer_elem_t *elem;
	long int min, max;
	enum _snd_mixer_selem_channel_id channel;
	int res;

	snd_mixer_open(&handle, 0);
	res = snd_mixer_attach(handle, name);
	if (res < 0) {
		fprintf(stderr, "snd_mixer_attach: %s\n", strerror(-res));
		return -1;
	}
	snd_mixer_selem_register(handle, NULL, NULL);
	snd_mixer_load(handle);

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, "Capture");
	elem = snd_mixer_find_selem(handle, sid);
	if (elem == NULL) {
		snd_mixer_selem_id_set_name(sid, "Mic");
		elem = snd_mixer_find_selem(handle, sid);
		if (elem == NULL) {
			printf("cannot find mixer element\n");
			return -1;
		}
	}

	snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
	snd_mixer_selem_set_capture_volume_all(elem, max);
	for (channel = 0; channel <= SND_MIXER_SCHN_LAST; channel++)
		snd_mixer_selem_set_capture_switch(elem, channel, 1);

	snd_mixer_detach(handle, name);
	snd_mixer_close(handle);
	return 0;
}

/*
 * open and configure sound output
 */
snd_pcm_t *audio(char *name, int frequency) {
	int res;
	snd_pcm_t *handle;
	snd_pcm_info_t *info;
	snd_pcm_hw_params_t *params;
	unsigned int num, c;
	snd_pcm_access_t a;
	int den, dir;
	snd_pcm_uframes_t frames = 32;

	res = snd_pcm_open(&handle, name, SND_PCM_STREAM_CAPTURE, 0);
	if (res < 0) {
		fprintf(stderr, "snd_pcm_open: %s\n", strerror(-res));
		return NULL;
	}

	snd_pcm_info_malloc(&info);
	res = snd_pcm_info(handle, info);
	if (res < 0) {
		fprintf(stderr, "snd_pcm_info: %s\n", strerror(-res));
		return NULL;
	}
	fprintf(stderr, "name: %s\n", snd_pcm_info_get_name(info));
	snd_pcm_info_free(info);

	snd_pcm_hw_params_malloc(&params);
	snd_pcm_hw_params_any(handle, params);
	res = snd_pcm_hw_params_set_rate(handle, params, frequency, 0);
	if (res < 0)
		fprintf(stderr, "set sample rate: %s\n", strerror(-res));
	snd_pcm_hw_params_set_access(handle, params,
					SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16);
	res = snd_pcm_hw_params_set_period_size_near(handle, params,
			&frames, &dir);
	if (res < 0)
		fprintf(stderr, "set period size: %s\n", strerror(-res));
	res = snd_pcm_hw_params_set_channels(handle, params, 1);
	if (res < 0)
		fprintf(stderr, "set channels: %s\n", strerror(-res));

	res = snd_pcm_hw_params(handle, params);
	if (res < 0) {
		fprintf(stderr, "set hw parameters: %s\n", strerror(-res));
		return NULL;
	}

	snd_pcm_hw_params_current(handle, params);

	snd_pcm_hw_params_get_rate(params, &num, &den);
	fprintf(stderr, "sample rate: %d/%d\n", num, den);
	if (num != (unsigned) frequency) {
		fprintf(stderr, "ERROR: actual sample rate %d, ", num);
		fprintf(stderr, "requested %d\n", frequency);
	}

	snd_pcm_hw_params_get_channels(params, &c);
	fprintf(stderr, "channels: %d\n", c);
	if (c != 1)
		fprintf(stderr, "ERROR: %d channels, requested 1\n", c);

	snd_pcm_hw_params_get_access(params, &a);
	if (a != SND_PCM_ACCESS_RW_INTERLEAVED)
		fprintf(stderr,
			"ERROR: interleaved access not allowed (%d)\n", a);

	snd_pcm_hw_params_free(params);

	res = snd_pcm_prepare(handle);
	if (res < 0) {
		fprintf(stderr, "prepare: %s\n", strerror(-res));
		return NULL;
	}

	return handle;
}

/*
 * microphone filter
 */
#define NFRAMES (32*256)
struct audiobuffer {
	snd_pcm_t *handle;
	int16_t buffer[NFRAMES * sizeof(int16_t)];
	int pos;
};

snd_pcm_t *microphone_handle(void *internal) {
	struct audiobuffer *buffer;
	buffer = (struct audiobuffer *) internal;
	return buffer->handle;
}

void *microphone_init(char *device, struct status *status) {
	struct audiobuffer *buffer;
	int frequency;

	status->ended = 1;

	buffer = malloc(sizeof(struct audiobuffer));

				/* set mixer */

	if (maxmixer(device))
		fprintf(stderr, "WARNING: cannot maximize capture volume\n");

				/* set pcm */

	frequency = 44100;
	buffer->handle = audio(device, frequency);
	if (buffer->handle == NULL)
		exit(EXIT_FAILURE);

	buffer->pos = NFRAMES;
	status->ended = 0;
	return buffer;
}

int microphone_value(int value, void *internal, struct status *status) {
	struct audiobuffer *buffer;
	int res;

	(void) value;
	(void) status;

	buffer = (struct audiobuffer *) internal;

	if (buffer->pos < NFRAMES)
		return buffer->buffer[buffer->pos++];

	res = snd_pcm_readi(buffer->handle, buffer->buffer, NFRAMES);
	if (res == -EPIPE) {
		snd_pcm_recover(buffer->handle, res, 0);
		return microphone_value(value, internal, status);
	}
	else if (res < 0) {
		fprintf(stderr, "readi: %s\n", strerror(-res));
		return -1;
	}

	buffer->pos = 0;
	return buffer->buffer[buffer->pos++];
}

int microphone_end(void *internal, struct status *status) {
	struct audiobuffer *buffer;
	int res;

	(void) status;

	buffer = (struct audiobuffer *) internal;

	res = snd_pcm_close(buffer->handle);
	if (res < 0) {
		printf("close: %s\n", strerror(-res));
		exit(EXIT_FAILURE);
	}

	free(buffer);
	return 0;
}

