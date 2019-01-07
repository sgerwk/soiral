#include <alsa/asoundlib.h>
#include "filters.h"

/*
 * microphone filter
 */
void *microphone_init(char *device, struct status *status);
int microphone_value(int value, void *internal, struct status *status);
int microphone_end(void *internal, struct status *status);

/*
 * this is intended to be used only for select() or poll()
 */
snd_pcm_t *microphone_handle(void *internal);

