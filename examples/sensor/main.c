/*
 * Copyright 2013-2014 Mike Stirling
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of the Tiny Home Area Network stack.
 *
 * http://www.tinyhan.co.uk/
 *
 * examples/sensors/main.c
 *
 * Simple sensor example for an ATMEGA328 based board
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "board.h"

#include "phy.h"
#include "tinymac.h"

#define VREF_MV		1100

static volatile uint32_t timer_tick_count = 0;

/* Radio interrupt only so it can wake us from sleep - actual
 * event handling is polled */
ISR(INT0_vect)
{

}

/* Timer interrupt invoked every 250 ms */
ISR(TIMER2_OVF_vect)
{
	timer_tick_count++;
}

static uint16_t vbatt_mv(void)
{
	uint32_t vbatt;

	/* Enable ADC, measure Vref relative to Vbatt */
	ADCSRA = _BV(ADEN) | (ADC_PRESCALER << ADPS0);
	ADMUX = _BV(REFS0) | ADC_MUX_VREF_1V1;
	_delay_ms(1); /* Allow to stabilise */
	ADCSRA |= _BV(ADSC);
	while (ADCSRA & _BV(ADSC)) {}
	vbatt = 1024UL * VREF_MV / (uint32_t)ADC;

	/* Disable ADC */
	ADCSRA = 0;

	return (uint16_t)vbatt;
}

static void sleep(void)
{
	// A whole TOSC cycle must complete before we re-enter sleep.  This
	// syncs with a dummy write to TCCR2A in the ISR and will complete immediately
	// if the required amount of time has already passed
	while (ASSR & _BV(TCR2AUB));
	// Go to sleep until we get an interrupt from timer 2
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);

	/* Disable BOD (PicoPower variants only) - this is time critical */
	sleep_bod_disable();
	/* Sleep */
	sleep_mode();
}

/*!
 * TinyHAN receive handler.  Invoked when a packet is passed up to the
 * application.  Here, we simply turn the LED on or off depending on the
 * value of the first byte in the packet
 */
static void rx_handler(const tinymac_node_t *node, const char *buf, size_t size)
{
	if (size) {
		if (*buf) {
			SETP(LED);
		} else {
			CLEARP(LED);
		}
	}
}

/*!
 * This function is called at regular intervals and simply posts the
 * current battery voltage to the coordinator as a binary value
 */
static void post_message(void)
{
	uint16_t battmv = vbatt_mv();
	tinymac_send(0, (const char*)&battmv, sizeof(battmv), 0, 0, NULL);
}

int main(void)
{
	/* TinyHAN setup */
	tinymac_params_t params = {
			.uuid = 0x123456789abcdef0ull,
			.coordinator = FALSE,
			.flags = TINYMAC_ATTACH_FLAGS_SLEEPY | 5, /*< Heartbeat interval 2^5 = 32 seconds */
	};
	uint32_t next_mac_tick = 0, next_post = 0;

	MCUSR = 0;
	wdt_disable();

	/* Set up system clock prescaler for specified F_CPU */
	clock_prescale_set(clock_div_2); /* 8 MHz / 2 = 4 MHz */

	/* External interrupts to low level sensitive and disabled */
	EIMSK = 0;
	EICRA = 0;

	/* Set up port directions and load initial values/enable pull-ups */
	PORTB = PORTB_VAL;
	PORTC = PORTC_VAL;
	PORTD = PORTD_VAL;
	DDRB = DDRB_VAL;
	DDRC = DDRC_VAL;
	DDRD = DDRD_VAL;

	/* Configure tick interrupt from external 32.768 kHz crystal
	 * for 250 ms wakeup */
	ASSR = _BV(AS2);
	TCNT2 = 0;
	TCCR2A = 0;
	TCCR2B = _BV(CS21) | _BV(CS20); // /32 for 0.25 second wakeup
	while (ASSR & (_BV(TCN2UB) | _BV(TCR2AUB) | _BV(TCR2BUB))); // sync clock domains
	TIFR2 = _BV(TOV2);
	TIMSK2 = _BV(TOIE2);
	sei();

	/* Turn on the transceiver */
	TRX_ON();

	/* Initialise TinyHAN */
	phy_init();
	tinymac_init(&params);
	tinymac_register_recv_cb(rx_handler);

	/* Main loop */
	while (1) {
		uint32_t now = timer_tick_count;

		if ((int32_t)(now - next_mac_tick) >= 0) {
			next_mac_tick = now + 1; /* 250 ms intervals */

			/* TinyMAC tick handler must be called every 250 ms to drive internal timers */
			tinymac_tick_handler(NULL);
		}

		if ((int32_t)(now - next_post) >= 0) {
			next_post = now + 20; /* 5 second intervals */
			post_message();
		}

		/* Call the PHY event handler whenever convenient - the radio needs to
		 * wake us from sleep if it needs to be serviced */
		phy_event_handler();

		/* Enable radio interrupt just so it can wake us from sleep */
		EIMSK |= _BV(INT0);

		/* Sleep until the next interrupt */
		sleep();

		/* Disable radio interrupt during normal operation */
		EIMSK &= ~_BV(INT0);
	}

	return 0;
}
