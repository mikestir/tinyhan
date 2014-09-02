/*
 * timer.h
 *
 *  Created on: 31 Aug 2014
 *      Author: mike
 */

#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>

/*! Tick frequency in Hz - defines timer resolution */
#define TIMER_TICK_FREQ			100

/*! Maximum number of callbacks we support */
#define TIMER_MAX_CALLBACKS		32

/* Flags for timer_register_callback */
#define TIMER_ONE_SHOT			(1 << 0)

/*! Helper macro calculates number of ticks in specified ms */
#define TIMER_MILLIS(ms)		((uint32_t)(ms) * (TIMER_TICK_FREQ) / 1000ul)

/*! Helper macro calculates number of ticks in specified s */
#define TIMER_SECONDS(s)		((uint32_t)(s) * (TIMER_TICK_FREQ))

typedef void (*timer_cb_t)(void *arg);

typedef void* timer_handle_t;
#define TIMER_HANDLE_FAIL		NULL

/*!
 * Initialise timers (enables SysTick)
 * Must be called from SystemCoreClockUpdate
 */
void timer_init(void);

/*!
 * Returns current 32-bit tick counter in
 * increments of TIMER_TICK_FREQ
 *
 * \return				Current tick count
 */
uint32_t timer_get_tick_count(void);

/*!
 * Busy-wait for the specified number of ticks
 *
 * \param ticks			Number of ticks delay
 */
void timer_delay(uint32_t ticks);

/*!
 * Register a timed callback
 *
 * \param handler		Pointer to callback function
 * \param arg			Function argument
 * \param timeout		Timeout in ticks
 * \param flags			TIMER_ONE_SHOT
 * \return				Timer handle or TIMER_HANDLE_FAIL on error
 */
timer_handle_t timer_request_callback(timer_cb_t handler, void *arg, uint32_t timeout, unsigned int flags);

/*!
 * Cancel a timed callback
 *
 * \param id			Handle returned by \see timer_register_callback
 */
void timer_cancel_callback(timer_handle_t handle);

/*!
 * Callback despatcher function.  Must be called at regular intervals.
 */
void timer_despatch_callbacks(void);

#endif /* TIMER_H_ */
