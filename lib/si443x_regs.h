/*
 * FHT heating valve comms example with RFM22/23 for AVR
 *
 * Copyright (C) 2013 Mike Stirling
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * \file si443x_regs.h
 * \brief Register definitions for Si443x
 *
 */

#ifndef SI443X_REGS_H_
#define SI443X_REGS_H_

/* Registers */

#define WRITE				0x80
#define READ				0x00

#define R_DEVICE_TYPE		0x00
#define R_DEVICE_VERSION	0x01
#define R_DEVICE_STATUS		0x02
#define R_INT_STATUS1		0x03
#define R_INT_STATUS2		0x04
#define R_INT_STATUS		R_INT_STATUS1
#define R_INT_ENABLE1		0x05
#define R_INT_ENABLE2		0x06
#define R_INT_ENABLE		R_INT_ENABLE1
#define R_OP_CTRL1			0x07
#define R_OP_CTRL2			0x08
#define R_XTAL_LOAD			0x09
#define R_CLOCK_OUT			0x0a
#define R_GPIO0_CFG			0x0b
#define R_GPIO1_CFG			0x0c
#define R_GPIO2_CFG			0x0d
#define R_IO_CFG			0x0e
#define R_ADC_CFG			0x0f
#define R_ADC_OFFSET		0x10
#define R_ADC_VAL			0x11
#define R_TEMP_CTRL			0x12
#define R_TEMP_VAL			0x13
#define R_WU_PERIOD1		0x14
#define R_WU_PERIOD2		0x15
#define R_WU_PERIOD3		0x16
#define R_WU_TIMER1			0x17
#define R_WU_TIMER2			0x18
// big-endian
#define R_WU_TIMER			R_WU_TIMER1
#define R_LDCM_DURATION		0x19
#define R_LOW_BATT_THRESH	0x1a
#define R_BATT_VAL			0x1b
#define R_IF_BANDWIDTH		0x1c
#define R_AFC_OVERRIDE		0x1d
#define R_AFC_CTRL			0x1e
#define R_CR_OVERRIDE		0x1f
#define R_CR_OVERSAMPLING	0x20
#define R_CR_OFFSET2		0x21
#define R_CR_OFFSET1		0x22
#define R_CR_OFFSET0		0x23
#define R_CR_GAIN1			0x24
#define R_CR_GAIN0			0x25
#define R_RSSI				0x26
#define R_RSSI_CCI_THRESH	0x27
#define R_DIVERSITY1		0x28
#define R_DIVERSITY2		0x29
#define R_AFC_LIMITER		0x2a
#define R_AFC_CORRECTION	0x2b
#define R_OOK_VAL1			0x2c
#define R_OOK_VAL2			0x2d
#define R_SLICER_PEAK_HOLD	0x2e
#define R_DATA_ACCESS_CTRL	0x30
#define R_EZMAC_STATUS		0x31
#define R_HEADER_CTRL1		0x32
#define R_HEADER_CTRL2		0x33
#define R_PREAMBLE_LENGTH	0x34
#define R_PREAMBLE_CTRL		0x35
#define R_SYNC_WORD3		0x36
#define R_SYNC_WORD2		0x37
#define R_SYNC_WORD1		0x38
#define R_SYNC_WORD0		0x39
// big-endian
#define R_SYNC_WORD			R_SYNC_WORD3
#define R_TX_HEADER3		0x3a
#define R_TX_HEADER2		0x3b
#define R_TX_HEADER1		0x3c
#define R_TX_HEADER0		0x3d
// big-endian
#define R_TX_HEADER			R_TX_HEADER3
#define R_TX_LENGTH			0x3e
#define R_CHECK_HEADER3		0x3f
#define R_CHECK_HEADER2		0x40
#define R_CHECK_HEADER1		0x41
#define R_CHECK_HEADER0		0x42
// big-endian
#define R_CHECK_HEADER		R_CHECK_HEADER3
#define R_HEADER_ENABLE3	0x43
#define R_HEADER_ENABLE2	0x44
#define R_HEADER_ENABLE1	0x45
#define R_HEADER_ENABLE0	0x46
// big-endian
#define R_HEADER_ENABLE		R_HEADER_ENABLE3
#define R_RX_HEADER3		0x47
#define R_RX_HEADER2		0x48
#define R_RX_HEADER1		0x49
#define R_RX_HEADER0		0x4a
// big-endian
#define R_RX_HEADER			R_RX_HEADER3
#define R_RX_LENGTH			0x4b
#define R_ADC8_CTRL			0x4f
#define R_FILTER_ADDR		0x60
#define R_XTAL_CTRL			0x62
#define R_AGC_OVERRIDE1		0x69
#define R_TX_POWER			0x6d
#define R_TX_RATE1			0x6e
#define R_TX_RATE0			0x6f
// big-endian
#define R_TX_RATE			R_TX_RATE1
#define R_MOD_CTRL1			0x70
#define R_MOD_CTRL2			0x71
#define R_DEVIATION			0x72
#define R_FREQ_OFFSET1		0x73
#define R_FREQ_OFFSET2		0x74
// little-endian!
#define R_FREQ_OFFSET		R_FREQ_OFFSET1
#define R_BAND_SELECT		0x75
#define R_CARRIER_FREQ1		0x76
#define R_CARRIER_FREQ0		0x77
// big-endian
#define R_CARRIER_FREQ		R_CARRIER_FREQ1
#define R_CHANNEL			0x79
#define R_STEP				0x7a
#define R_TX_FIFO_CTRL1		0x7c
#define R_TX_FIFO_CTRL2		0x7d
#define R_RX_FIFO_CTRL		0x7e
#define R_FIFO				0x7f

/*************************/
/* 0x00 Device Type Code */
/*************************/

#define DT_MASK				(31 << 0)

/****************************/
/* 0x01 Device Version Code */
/****************************/

#define VC_MASK				(31 << 0)

/**********************/
/* 0x02 Device Status */
/**********************/

#define FFOVFL				(1 << 7)
#define FFUNFL				(1 << 6)
#define RXFFEM				(1 << 5)
#define HEADERR				(1 << 4)
#define FREQERR				(1 << 3)
#define CPS_IDLE			(0 << 0)
#define CPS_RX				(1 << 0)
#define CPS_TX				(2 << 0)

/******************************/
/* 0x03/0x04 Interrupt Status */
/******************************/

#define IFFERR				(1 << 15)
#define ITXFFAFULL			(1 << 14)
#define ITXFFAEM			(1 << 13)
#define IRXFFAFULL			(1 << 12)
#define IEXT				(1 << 11)
#define IPKSENT				(1 << 10)
#define IPKVALID			(1 << 9)
#define ICRCERROR			(1 << 8)

#define ISWDET				(1 << 7)
#define IPREAVAL			(1 << 6)
#define IPREAINVAL			(1 << 5)
#define IRSSI				(1 << 4)
#define IWUT				(1 << 3)
#define ILBD				(1 << 2)
#define ICHIPRDY			(1 << 1)
#define IPOR				(1 << 0)

/******************************/
/* 0x05/0x06 Interrupt Enable */
/******************************/

#define ENFFERR				(1 << 15)
#define ENTXFFAFULL			(1 << 14)
#define ENTXFFAEM			(1 << 13)
#define ENRXFFAFULL			(1 << 12)
#define ENEXT				(1 << 11)
#define ENPKSENT			(1 << 10)
#define ENPKVALID			(1 << 9)
#define ENCRCERROR			(1 << 8)

#define ENSWDET				(1 << 7)
#define ENPREAVAL			(1 << 6)
#define ENPREAINVAL			(1 << 5)
#define ENRSSI				(1 << 4)
#define ENWUT				(1 << 3)
#define ENLBD				(1 << 2)
#define ENCHIPRDY			(1 << 1)
#define ENPOR				(1 << 0)

/**********************************************/
/* 0x07 Operating Mode and Function Control 1 */
/**********************************************/

#define SWRES				(1 << 7)
//#define ENLBD				(1 << 6)
#define ENWT				(1 << 5)
#define X32KSEL				(1 << 4)
#define TXON				(1 << 3)
#define RXON				(1 << 2)
#define PLLON				(1 << 1)
#define XTON				(1 << 0)

/**********************************************/
/* 0x08 Operating Mode and Function Control 2 */
/**********************************************/

#define ANTDIV000			(0 << 5)
#define ANTDIV001			(1 << 5)
#define ANTDIV010			(2 << 5)
#define ANTDIV011			(3 << 5)
#define ANTDIV100			(4 << 5)
#define ANTDIV101			(5 << 5)
#define ANTDIV110			(6 << 5)
#define ANTDIV111			(7 << 5)
#define RXMPK				(1 << 4)
#define AUTOTX				(1 << 3)
#define ENLDM				(1 << 2)
#define FFCLRRX				(1 << 1)
#define FFCLRTX				(1 << 0)

/********************************************/
/* 0x09 Crystal Oscillator Load Capacitance */
/********************************************/

#define XTALSHFT			(1 << 7)
#define XLC_MASK			(127 << 0)

#define XLC_MIN				1800
#define XLC_STEP			85
#define XLC_MAX				(XLC_MIN + (XLC_STEP * 127))

/*! Calculate value to load into register 0x09 given desired load capacitance in fF.
 * XTALSHFT is assumed to be disabled. */
#define XLC(cint)			((((cint) - XLC_MIN) / XLC_STEP) & XLC_MASK)
/*! Calculate current crystal load capacitance in fF given value of register 0x09 */
#define CINT(xlc)			((((xlc) & XLC_MASK) * XLC_STEP) + XLC_MIN + ((xlc) / XTALSHFT) * 3700)

/*************************************/
/* 0x0A Microcontroller Clock Output */
/*************************************/

#define CLKT_0				(0 << 4)
#define CLKT_128			(1 << 4)
#define CLKT_256			(2 << 4)
#define CLKT_512			(3 << 4)

#define ENLFC				(1 << 3)

#define MCLK_30MHZ			(0 << 0)
#define MCLK_15MHZ			(1 << 0)
#define MCLK_10MHZ			(2 << 0)
#define MCLK_4MHZ			(3 << 0)
#define MCLK_3MHZ			(4 << 0)
#define MCLK_2MHZ			(5 << 0)
#define MCLK_1MHZ			(6 << 0)
#define MCLK_32KHZ			(7 << 0)

/*************************************/
/* 0x0B/0x0C/0x0D GPIO Configuration */
/*************************************/

#define GPIODRV_0			(0 << 6)
#define GPIODRV_1			(1 << 6)
#define GPIODRV_2			(2 << 6)
#define GPIODRV_3			(3 << 6)
#define PUP					(1 << 5)

#define GPIO_DEFAULT		(0 << 0)
#define GPIO_WUT			(1 << 0)
#define GPIO_LBD			(2 << 0)
#define GPIO_DIGI_IN		(3 << 0)
#define GPIO_INT_FALLING	(4 << 0)
#define GPIO_INT_RISING		(5 << 0)
#define GPIO_INT_BOTH		(6 << 0)
#define GPIO_ADC_IN			(7 << 0)
#define GPIO_DIGI_OUT		(10 << 0)
#define GPIO_VREF_OUT		(14 << 0)
#define GPIO_DCLK_OUT		(15 << 0)
#define GPIO_TX_IN			(16 << 0)
#define GPIO_RTX_REQ		(17 << 0)
#define GPIO_TX_STATE		(18 << 0)
#define GPIO_TX_FAF			(19 << 0)
#define GPIO_RX_OUT			(20 << 0)
#define GPIO_RX_STATE		(21 << 0)
#define GPIO_RX_FAF			(22 << 0)
#define GPIO_ANTENNA1		(23 << 0)
#define GPIO_ANTENNA2		(24 << 0)
#define GPIO_VALID_PREAMBLE	(25 << 0)
#define GPIO_BAD_PREAMBLE	(26 << 0)
#define GPIO_SYNC			(27 << 0)
#define GPIO_CCA			(28 << 0)
#define GPIO_VDD			(29 << 0)

/******************************/
/* 0x0E IO Port Configuration */
/******************************/

#define EXTITST2			(1 << 6)
#define EXTITST1			(1 << 5)
#define EXTITST0			(1 << 4)
#define ITSDO				(1 << 3)
#define DIO2				(1 << 2)
#define DIO1				(1 << 1)
#define DIO0				(1 << 0)

/**************************/
/* 0x0F ADC Configuration */
/**************************/

#define ADCSTART			(1 << 7)

#define ADCSEL_TEMP			(0 << 4)
#define ADCSEL_GPIO0		(1 << 4)
#define ADCSEL_GPIO1		(2 << 4)
#define ADCSEL_GPIO2		(3 << 4)
#define ADCSEL_GPIO0_1		(4 << 4)
#define ADCSEL_GPIO1_2		(5 << 4)
#define ADCSEL_GPIO0_2		(6 << 4)
#define ADCSEL_GND			(7 << 4)

#define ADCREF_1V2			(0 << 2)
#define ADCREF_VDD_3		(2 << 2)
#define ADCREF_VDD_2		(3 << 2)

#define ADCGAIN_MASK		(3 << 0)

/************************************/
/* 0x10 ADC Sensor Amplifier Offset */
/************************************/

#define ADCOFFS_MASK		(15 << 0)

/******************/
/* 0x11 ADC Value */
/******************/

/***************************************/
/* 0x12 Temperature Sensor Calibration */
/***************************************/

#define TSRANGE00			(0 << 6)
#define TSRANGE01			(1 << 6)
#define TSRANGE10			(2 << 6)
#define TSRANGE11			(3 << 6)

#define ENTSOFFS			(1 << 5)
#define ENTSTRIM			(1 << 4)

#define TSTRIM_MASK			(15 << 0)

/*********************************/
/* 0x13 Temperature Value Offset */
/*********************************/

/***************************************/
/* 0x14/0x15/0x16 Wake-Up Timer Period */
/***************************************/

#define WTR_MASK			(31 << 0)

/*********************************/
/* 0x17/0x18 Wake-Up Timer Value */
/*********************************/

// TODO:

/*************************************/
/* 0x19 Low-Duty Cycle Mode Duration */
/*************************************/

// TODO:

/***************************************/
/* 0x1A Low-Battery Detector Threshold */
/***************************************/

#define LBDT_MASK			(31 << 0)

#define LBDT(mv)			(((mv) - 1700) / 50) & LBDT_MASK)
#define LBDT_MV(lbdt)		(((lbdt) & LBDT_MASK) * 50 + 1700)

/******************************/
/* 0x1B Battery Voltage Level */
/******************************/

#define VBAT_MASK			(31 << 0)

#define VBAT(mv)			(((mv) - 1700) / 50) & VBAT_MASK)
#define VBAT_MV(vbat)		(((vbat) & VBAT_MASK) * 50 + 1700)

/***********************************************************************/
/* 0x1C/0x1D/0x1E/0x1F/0x20/0x21/0x22/0x23/0x24/0x25                   */
/* Demodulator parameters must be defined with reference to the SiLabs */
/* Excel spreadsheet.  Runtime configuration is not support except via */
/* lookup table.                                                       */
/***********************************************************************/

/*******************************************/
/* 0x26 Received Signal Strength Indicator */
/*******************************************/

#define RSSI_DBM(rssi)		((rssi) - 241) * 100 / 191

/***************************/
/* 0x27 RSSI CCI Threshold */
/***************************/

#define RSSITH(dbm)			(241 + (((dbm) * 191) / 100))

/*******************************/
/* 0x28/0x29 Antenna Diversity */
/*******************************/

/***********************************************************************/
/* 0x2A/0x2C/0x2D/0x2E                                                 */
/* Demodulator parameters must be defined with reference to the SiLabs */
/* Excel spreadsheet.  Runtime configuration is not support except via */
/* lookup table.                                                       */
/***********************************************************************/

/****************************/
/* 0x30 Data Access Control */
/****************************/

/*! Enables packet handling for receive */
#define ENPACRX				(1 << 7)
/*! Data is sent/received LSb first */
#define LSBFRST				(1 << 6)
/*! CRC only calculated over the data field */
#define CRCDONLY			(1 << 5)
/*! Skip 2nd phase of preamble detection */
#define SKIP2PH				(1 << 4)
/*! Enables packet handling for transmit */
#define ENPACTX				(1 << 3)
/*! Enables CRC generation in packet handler mode */
#define ENCRC				(1 << 2)
/*! Selects CRC polynomial */
#define CRC_CCITT			(0 << 0)
#define CRC_16				(1 << 0)
#define CRC_IEC16			(2 << 0)
#define CRC_BIACHEVA		(3 << 0)

/*********************/
/* 0x31 EZMAC Status */
/*********************/

/*! Received CRC was all 1s */
#define RXCRC1				(1 << 6)
/*! Packet search is in progress */
#define PKSRCH				(1 << 5)
/*! Packet reception is in progress */
#define PKRX				(1 << 4)
/*! A valid packet was received */
#define PKVALID				(1 << 3)
/*! A packet with a bad CRC was received */
#define CRCERROR			(1 << 2)
/*! Packet transmission is in progress */
#define PKTX				(1 << 1)
/*! Packet transmission is complete */
#define PKSENT				(1 << 0)

/*************************/
/* 0x32 Header Control 1 */
/*************************/

#define BCEN_SHIFT			4
#define BCEN_MASK			15

/*! Enable header bytes for broadcast address checking */
#define BCEN(bcen)			(((bcen) & BCEN_MASK) << BCEN_SHIFT)

#define HDCH_SHIFT			0
#define HDCH_MASK			15

/*! Enable header bytes for unicast address checking */
#define HDCH(hdch)			(((hdch) & HDCH_MASK) << HDCH_SHIFT)

/*************************/
/* 0x33 Header Control 2 */
/*************************/

/*! Skip sync word search timeout */
#define SKIPSYN				(1 << 7)

#define HDLEN_SHIFT			4
#define HDLEN_MASK			7

/*! Specify header length in bytes (0-4) */
#define HDLEN(hdlen)		(((hdlen) & HDLEN_MASK) << HDLEN_SHIFT)

/*! Use fixed packet length - omits length byte from tx packet, rx
 * obtains length from 0x3E */
#define FIXPKLEN			(1 << 3)

#define SYNCLEN_SHIFT		1
#define SYNCLEN_MASK		3

/*! Program sync word length in bytes (1-4) */
#define SYNCLEN(synclen)	((((synclen) - 1) & SYNCLEN_MASK) << SYNCLEN_SHIFT)

#define PREALEN8			(1 << 0)

/************************/
/* 0x34 Preamble Length */
/************************/

/*! Maximum preamble length in nibbles (ignoring extra MSb in 0x33) */
#define PREALEN_MAX			255

/***********************************/
/* 0x35 Preamble Threshold Control */
/***********************************/

#define PREATH_SHIFT		3
#define PREATH_MASK			31

/*! Maximum preamble threshold in nibbles */
#define PREATH_MAX			31

#define PREATH(th)			(((th) & PREATH_MASK) << PREATH_SHIFT)

/*********************************/
/* 0x36/0x37/0x38/0x39 Sync Word */
/*********************************/

/***************************************/
/* 0x3A/0x3B/0x3C/0x3D Transmit Header */
/***************************************/

/**********************/
/* 0x3E Packet Length */
/**********************/

/************************************/
/* 0x3F/0x40/0x41/0x42 Check Header */
/************************************/

/*************************************/
/* 0x43/0x44/0x45/0x46 Header Enable */
/*************************************/

/***************************************/
/* 0x47/0x48/0x49/0x4A Received Header */
/***************************************/

/*******************************/
/* 0x4B Received Packet Length */
/*******************************/


/***********************/
/* 0x63 AGC Override 1 */
/***********************/

#define SGIN				(1 << 6)
#define AGCEN				(1 << 5)
#define LNAGAIN				(1 << 4)
#define PGA_MASK			(15 << 0)

/*****************/
/* 0x6D TX Power */
/*****************/

#define LNA_SW				(1 << 3)
#define TXPOW_MASK			(7 << 0)

#ifdef SI4432
#define TXPOW_MIN			-1
#define TXPOW_MAX			20
#else
#define TXPOW_MIN			-8
#define TXPOW_MAX			13
#endif
#define TXPOW_STEP			3

/*! Calculate value to load into register 0x6d given desired output power in dBm */
#define TXPOW(dbm)			((((dbm) - (TXPOW_MIN)) / TXPOW_STEP) & TXPOW_MASK)
/*! Calculate current power in dBm given value of register 0x6d */
#define TXDBM(txpow)		((((txpow) & TXPOW_MASK) * TXPOW_STEP) + (TXPOW_MIN))

/**************************/
/* 0x6E/0x6F TX Data Rate */
/**************************/

/*! Calculate values to load into registers 0x6e, 0x6f given data rate in bps up to 30 kbps */
#define TXDR_LOW(bps)		((((unsigned long long)bps) << 21) / 1000000ULL)
/*! Calculate values to load into registers 0x6e, 0x6f given data rate in bps over 30 kbps */
#define TXDR_HIGH(bps)		((((unsigned long long)bps) << 16) / 1000000ULL)

/* Bit rates below this value require TXDTRSCALE in 0x70 */
#define TXDTRT_SCALE_MAX	30000

/**********************************/
/* 0x70 Modulation Mode Control 1 */
/**********************************/

#define TXDTRTSCALE			(1 << 5)
#define ENPHPWDN			(1 << 4)
#define MANPPOL				(1 << 3)
#define ENMANINV			(1 << 2)
#define ENMANCH				(1 << 1)
#define ENWHITE				(1 << 0)

/**********************************/
/* 0x71 Modulation Mode Control 2 */
/**********************************/

#define TRCLK_NONE			(0 << 6)
#define TRCLK_GPIO			(1 << 6)
#define TRCLK_SDO			(2 << 6)
#define TRCLK_nIRQ			(3 << 6)
#define DTMOD_GPIO			(0 << 4)
#define DTMOD_SDI			(1 << 4)
#define DTMOD_FIFO			(2 << 4)
#define DTMOD_PN9			(3 << 4)
#define ENINV				(1 << 3)
/*! MSB of FD value if required.  See 0x72 Deviation */
#define FD8					(1 << 2)
#define MODTYP_NONE			(0 << 0)
#define MODTYP_OOK			(1 << 0)
#define MODTYP_FSK			(2 << 0)
#define MODTYP_GFSK			(3 << 0)
#define MODTYP_MASK			(3 << 0)

/******************/
/* 0x72 Deviation */
/******************/

#define FD_MASK				(255 << 0)

/*! Calculate value to load into register 0x72 given desired FM deviation in Hz */
#define FD(deviation_hz)	((deviation_hz) / 625UL)
/*! Calculate deviation in Hz given value of register 0x72 */
#define DEVIATION(fd)		((fd) * 625UL)

/******************************/
/* 0x73/0x74 Frequency Offset */
/******************************/

#define FO_MASK				(1023 << 0)

#ifdef HIGH_BAND
#define FO_STEP				(312.5)
#else
#define FO_STEP				(156.25)
#endif

/*! Carrier frequency trim in steps of 156.25 Hz (low band) or 312.5 Hz (high band).
 *  Desired frequency specified in signed Hz.  Note this register is little-endian,
 *  unlike most of the other register pairs. */
#define _FO(offset_hz)		(((offset_hz) / FO_STEP) & FO_MASK)
/*! Calculate offset in Hz given value of registers 0x73/0x74 */
#define _OFFSET(fo)			(FO_STEP * ((fo) & FO_MASK))

/*! Wrapper for _FO to swap endianness */
#define FO(offset_hz)		((uint16_t)((_FO(offset_hz) >> 8) | _FO(offset_hz) << 8))
/*! Wrapper for _OFFSET to swap endianness */
#define	OFFSET(fo)			(_OFFSET(((fo) >> 8) | ((fo) << 8)))

/********************/
/* 0x75 Band select */
/********************/
#define SBSEL				(1 << 6)
#define HBSEL				(1 << 5)
#define FB_MASK				(31 << 0)

#if !defined(HIGH_BAND) && !defined(LOW_BAND)
#error Must define either LOW_BAND (240-480 MHz) or HIGH_BAND(480-960 MHz)
#endif

#ifndef MHZ
#define MHZ					1000000
#endif
#ifndef KHZ
#define KHZ					1000
#endif

#ifdef HIGH_BAND
#define BAND_OFFSET			(480 * MHZ)
#define BAND_STEP			(20 * MHZ)
#define BAND_FLAGS			(SBSEL | HBSEL)
#else
#define BAND_OFFSET			(240 * MHZ)
#define BAND_STEP			(10 * MHZ)
#define BAND_FLAGS			(SBSEL)
#endif

#define _BAND(ch0_hz)		((((ch0_hz) - BAND_OFFSET) / BAND_STEP) & FB_MASK)

/*! Calculate value to load into register 0x75 given channel 0 frequency in Hz */
#define FB(ch0_hz)			(BAND_FLAGS | _BAND(ch0_hz))

/*******************************************************/
/* 0x76/0x77 Nominal carrier frequency (for channel 0) */
/*******************************************************/

/*! Calculate values to load into registers 0x76, 0x77 given channel 0 frequency in Hz */
#define FC(ch0_hz)			(((ch0_hz) - (BAND_OFFSET + BAND_STEP * _BAND(ch0_hz))) * 64UL / (BAND_STEP / KHZ))

/***********************/
/* 0x79 Channel select */
/***********************/

/*****************************************/
/* 0x7A Channel step (10 kHz increments) */
/*****************************************/

/**************************/
/* 0x7C TX FIFO Control 1 */
/**************************/

/*! TX FIFO almost full threshold - default 55 */
#define TXAFTHR_MASK		(63 << 0)

/**************************/
/* 0x7D TX FIFO Control 2 */
/**************************/

/*! TX FIFO almost empty threshold - default 4 */
#define TXFAETHR_MASK		(63 << 0)

/************************/
/* 0x7E RX FIFO Control */
/************************/

/*! RX FIFO almost full threshold - default 55 */
#define RXAFTHR_MASK		(63 << 0)


#endif /* SI443X_REGS_H_ */
