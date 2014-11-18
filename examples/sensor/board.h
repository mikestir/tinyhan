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
 * board.h
 *
 * Simple sensor example, board definition header
 *
 */

#ifndef BOARD_H_
#define BOARD_H_

#include <avr/io.h>

/* Pin definitions */
#define TRX_SDN					B,1,1
#define nTRX_SEL				B,2,1
#define TRX_MOSI				B,3,1
#define TRX_MISO				B,4,1
#define TRX_SCK					B,5,1

#define nTRX_IRQ				D,2,1
#define LED						D,7,1

/* Analogue MUX inputs */
#define ADC_MUX_VREF_1V1		14

/* ADC prescaler configuration
 * ADC clock = F_CPU / (1 << ADC_PRESCALER)
 * 50 kHz <= ADC clock <= 200 kHz
 */
#define ADC_PRESCALER			6

/*
 * Input definitions for each hardware port
 */

#define PORTB_INS				(MASK(TRX_MISO))
#define PORTC_INS				(0)
#define PORTD_INS				(MASK(nTRX_IRQ))

#define PORTB_PULLUP			(MASK(TRX_MISO))
#define PORTC_PULLUP			(0)
#define PORTD_PULLUP			(MASK(nTRX_IRQ))

/*
 * Output definitions for each hardware port
 */

#define PORTB_OUTS				(MASK(TRX_SDN) | MASK(nTRX_SEL) | MASK(TRX_MOSI) | MASK(TRX_SCK))
#define PORTC_OUTS				(0)
#define PORTD_OUTS				(MASK(LED))

#define PORTB_INITIAL			(MASK(TRX_SDN) | MASK(nTRX_SEL))
#define PORTC_INITIAL			(0)
#define PORTD_INITIAL			(0)

/*
 * Initial DDR values - unused pins are set to outputs to save power
 */

#define DDRB_VAL				(0xff & ~PORTB_INS)
#define DDRC_VAL				(0xff & ~PORTC_INS)
#define DDRD_VAL				(0xff & ~PORTD_INS)

/*
 * Initial port values
 */

#define PORTB_VAL				(PORTB_PULLUP | PORTB_INITIAL)
#define PORTC_VAL				(PORTC_PULLUP | PORTC_INITIAL)
#define PORTD_VAL				(PORTD_PULLUP | PORTD_INITIAL)

/*
 * Helper macros
 */

#define MASK_(port,pin,sz)		(((1 << (sz)) - 1) << (pin))
#define INP_(port,pin,sz)		((PIN##port >> (pin)) & ((1 << (sz)) - 1))
#define OUTP_(port,pin,sz,val)	(PORT##port = (PORT##port & ~MASK_(port,pin,sz)) | (((val) << (pin)) & MASK_(port,pin,sz)))
#define SETP_(port,pin,sz)		(PORT##port |= MASK_(port,pin,sz))
#define CLEARP_(port,pin,sz)	(PORT##port &= ~MASK_(port,pin,sz))
#define TOGGLEP_(port,pin,sz)	(PORT##port ^= MASK_(port,pin,sz))

/*! Return bit mask for named IO port */
#define MASK(id)				MASK_(id)
/*! Return the current value of the named IO port */
#define INP(id)					INP_(id)
/*! Sets the named IO port to the specified value */
#define OUTP(id,val)			OUTP_(id,val)
/*! Turns on the named IO port */
#define SETP(id)				SETP_(id)
/*! Turns off the named IO port */
#define CLEARP(id)				CLEARP_(id)
/*! Toggled the named IO port */
#define TOGGLEP(id)				TOGGLEP_(id)

#define TRX_OFF()				SETP(TRX_SDN)
#define TRX_ON()				CLEARP(TRX_SDN)

#endif /*BOARD_H_*/
