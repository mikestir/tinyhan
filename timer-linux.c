/*
 * timer-linux.c
 *
 *  Created on: 31 Aug 2014
 *      Author: mike
 */

#include <time.h>
#include <assert.h>

#include "common.h"
#include "timer.h"

typedef struct timer {
	timer_cb_t			handler;
	void				*arg;
	uint32_t			reload;
	uint32_t			expiry;
} timer_slot_t;

static timer_slot_t timer_pool[TIMER_MAX_CALLBACKS];

void timer_init(void)
{

}

uint32_t timer_get_tick_count(void)
{
	struct timespec ts;
	uint32_t ticks;

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		perror("clock_gettime");
		return 0;
	}

	/* Convert to 10ms ticks because this is what we are likely to be
	 * using on an embedded implementation.  Don't care about overflow. */
	ticks = (ts.tv_sec * TIMER_TICK_FREQ) + (ts.tv_nsec / (1000000000ul / TIMER_TICK_FREQ));
	return ticks;
}

void timer_delay(uint32_t ticks)
{
	struct timespec ts;

	ts.tv_sec = ticks / TIMER_TICK_FREQ;
	ts.tv_nsec = (ticks % TIMER_TICK_FREQ) * (1000000000ul / TIMER_TICK_FREQ);
	clock_nanosleep(CLOCK_REALTIME, 0, &ts, NULL);
}

timer_handle_t timer_request_callback(timer_cb_t handler, void *arg, uint32_t timeout, unsigned int flags)
{
	timer_slot_t *timer = timer_pool;
	unsigned int n;

	/* Search for free timer */
	for (n = 0; n < TIMER_MAX_CALLBACKS; n++) {
		if (timer->handler == NULL) {
			/* Populate free slot */
			timer->handler = handler;
			timer->arg = arg;
			timer->reload = (flags & TIMER_ONE_SHOT) ? 0 : timeout;
			timer->expiry = timer_get_tick_count() + timeout;
			return (timer_handle_t)timer;
		}
		timer++;
	}

	ERROR("No callback timer available\n");
	return TIMER_HANDLE_FAIL;
}

void timer_cancel_callback(timer_handle_t handle)
{
	timer_slot_t *target = (timer_slot_t*)handle;

	assert(target != NULL);

	/* Clear handler, returns timer to pool*/
	target->handler = NULL;
}

void timer_despatch_callbacks(void)
{
	timer_slot_t *timer = timer_pool;
	uint32_t now = timer_get_tick_count();
	unsigned int n;

	for (n = 0; n < TIMER_MAX_CALLBACKS; n++) {
		if (timer->handler && ((int32_t)(now - timer->expiry) >= 0)) {
			/* This timer has expired - run callback */
			timer->handler(timer->arg);

			/* Calculate next expiry (if any) */
			if (timer->reload) {
				/* Restart */
				timer->expiry += timer->reload;
			} else {
				/* Return one-shot timer to pool automatically */
				timer->handler = NULL;
			}
		}
		timer++;
	}
}
