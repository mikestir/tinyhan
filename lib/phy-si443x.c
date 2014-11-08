/*
 * phy-si443x.c
 *
 *  Created on: 26 Sep 2014
 *      Author: mike
 */

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

//#undef DEBUG

#include "common.h"
#include "phy.h"
#include "tinyhan_platform.h"

#define MAX_PACKET					64

#ifndef MHZ
#define MHZ							1000000
#endif
#ifndef KHZ
#define KHZ							1000
#endif

/* Configuration for TinyHAN PHY spec */

/*! Channel number */
#define DEF_CHANNEL					100
/*! Transmitter power */
#define DEF_TX_POWER				TXPOW_MIN

/* Fixed for given receiver configuration */

/*! Frequency of channel 0 (Hz) */
#define CFG_FREQUENCY_F0			(868 * MHZ)
/*! Channel spacing (kHz) */
#define CFG_CHANNEL_STEP			10
/*! Deviation (for (G)FSK) (Hz) */
#define CFG_DEVIATION				25000
/*! Bitrate (bps) */
#define CFG_BITRATE					50000
/*! Modulation mode */
#define CFG_MODULATION				MODTYP_GFSK
/*! Transmitter preamble length in packet mode (nibbles) */
#define CFG_TX_PREAMBLE				8
/*! Receiver minimum preamble length (nibbles) */
#define CFG_PREAMBLE_THRESH			2
/*! Sync word (left-justified if shorter than 32 bits) */
#define CFG_SYNC_WORD				0x2dd20000
/*! Sync word length (bytes) */
#define CFG_SYNC_WORD_LEN			2

/* Define either LOW_BAND or HIGH_BAND before including si443x_regs.h */
#if (CFG_FREQUENCY_F0) < (480 * MHZ)
	#define LOW_BAND
#else
	#define HIGH_BAND
#endif

#define FIFO_SIZE					64
#define RX_BLOCK_SIZE				(FIFO_SIZE / 2)
#define TX_WAIT_THRESH				(FIFO_SIZE - 6)
#define TX_RESUME_THRESH			(FIFO_SIZE / 2)

#include "si443x_regs.h"

/* Radio configuration:
 *
 * Base frequency = 868 MHz, 10 kHz channels, default channel 100 (869 MHz)
 * TX: GFSK 25 kHz deviation, 50 kbps
 * RX: According to SiLabs spreadsheet for given TX parameters
 */

static const uint8_t phy_si443x_config[] TABLE = {
		/* General setup */
		R_GPIO0_CFG, 3,
		GPIO_RX_STATE, GPIO_TX_STATE, 0,		/* T/R switching for RFM22B */

		R_DATA_ACCESS_CTRL, 1,
		ENPACRX | ENPACTX | ENCRC | CRC_CCITT,	/* R_DATA_ACCESS_CTRL - enable packet engine, CRC CCITT */

		R_TX_FIFO_CTRL1, 3,
		TX_WAIT_THRESH & TXAFTHR_MASK,			/* R_TX_FIFO_CTRL1 - TX almost full threshold */
		TX_RESUME_THRESH & TXFAETHR_MASK,		/* R_TX_FIFO_CTRL2 - TX almost empty threshold */
		(RX_BLOCK_SIZE - 1) & RXAFTHR_MASK,		/* R_RX_FIFO_CTRL - RX almost full threshold */

		R_HEADER_CTRL1, 8,
		BCEN(0) | HDCH(0), 						/* R_HEADER_CTRL1 - no header/broadcast check */
		HDLEN(0) | SYNCLEN(CFG_SYNC_WORD_LEN),	/* R_HEADER_CTRL2 - sync length, no header */
		CFG_TX_PREAMBLE,						/* R_PREAMBLE_LENGTH - tx preamble */
		PREATH(CFG_PREAMBLE_THRESH),			/* R_PREAMBLE_CTRL - rx preamble */
		(uint8_t)((CFG_SYNC_WORD) >> 24),		/* R_SYNC_WORD3 */
		(uint8_t)((CFG_SYNC_WORD) >> 16),
		(uint8_t)((CFG_SYNC_WORD) >> 8),
		(uint8_t)((CFG_SYNC_WORD) >> 0),

		/* Transmitter setup */
		R_TX_POWER, 11,
		TXPOW(DEF_TX_POWER) | LNA_SW,			/* R_TX_POWER - default, configurable */
		(uint8_t)(TXDR_HIGH(CFG_BITRATE) >> 8),	/* R_TX_RATE1 */
		(uint8_t)(TXDR_HIGH(CFG_BITRATE) >> 0),	/* R_TX_RATE0 */
		/*TXDTRTSCALE*/ 0,							/* R_MOD_CTRL1 */
		TRCLK_NONE | DTMOD_FIFO | CFG_MODULATION,	/* R_MOD_CTRL2 */
		FD(CFG_DEVIATION),						/* R_DEVIATION */
		0, 0,									/* R_FREQ_OFFSET1/2 */
		FB(CFG_FREQUENCY_F0),					/* R_BAND_SELECT */
		(uint8_t)(FC(CFG_FREQUENCY_F0) >> 8),	/* R_CARRIER_FREQ1 */
		(uint8_t)(FC(CFG_FREQUENCY_F0) >> 0),	/* R_CARRIER_FREQ0 */

		R_CHANNEL, 2,
		DEF_CHANNEL,							/* R_CHANNEL - default, configurable */
		(CFG_CHANNEL_STEP) / 10,				/* R_STEP */

		/* Receiver setup according to spreadsheet */
		R_IF_BANDWIDTH, 10,
		0x05, 0x40, 0x0a, 0x03, 0x50, 0x01, 0x99, 0x9a, 0x06, 0x68,
		R_AFC_LIMITER, 5,
		0x28, 0x00, 0x28, 0x19, 0x29,

		0, /* END */
};

/* NOTE: Order is important here */
typedef enum {
	/* Idle states */
	stateStandby = 0,			/*< Radio off.  To \see stateListen, stateTxStart */
	stateListen,				/*< Rx on (or WoR).  To \see stateStandby, stateRx, stateTxStart */

	/* Receive states */
	stateRx,					/*< Rx synced.  To \see stateRxReady, stateRxValid, stateRxInvalid, stateRxFull */
	stateRxReady,				/*< Rx FIFO ready for read.  To \see stateRx, stateRxValid, stateRxInvalid, stateRxFull */
	stateRxValid,				/*< Rx packet ok.  To \see stateStandby, stateListen */
	stateRxInvalid,				/*< Rx packet CRC bad.  To \see stateStandby, stateListen */
	stateFifoError,				/*< Rx FIFO has overflowed.  To \see stateStandby, stateListen */

	/* Transmit states */
	stateTx,					/*< Tx FIFO filling, FIFO not full.  To \see stateTxWait, stateStandby, stateListen */
	stateTxBusy,				/*< Tx FIFO draining.  To \see stateTx */
} si443x_state_t;

/*! Current transceiver state */
static volatile si443x_state_t phy_si443x_state;
/*! RSSI value sampled after achieving sync, /dBm */
static volatile int phy_si443x_rssi;
/*! Callback function invoked when a packet is received */
static phy_recv_cb_t phy_si443x_recv_cb;

#ifdef PLATFORM_STORE
/*! Platform specific storage */
PLATFORM_STORE
#endif

/**********************************************/
/* Macros for accessing transceiver registers */
/**********************************************/

/* Device identification */

#define SUPPORTED_DEVICE_TYPE			0x08
#define SI443X_DEVICE_TYPE()			(si443x_read8(R_DEVICE_TYPE) & DT_MASK)

#define SUPPORTED_DEVICE_VERSION		0x06
#define SI443X_DEVICE_VERSION()			(si443x_read8(R_DEVICE_VERSION) & VC_MASK)

/* Interrupt status read/clear */

#define SI443X_STATUS()					(si443x_read16(R_INT_STATUS))

/* Operating mode selection */

#define SI443X_SWRESET()				si443x_write8(R_OP_CTRL1, SWRES)
#define SI443X_MODE_STANDBY()			si443x_write8(R_OP_CTRL1, 0)
#define SI443X_MODE_SLEEP()				si443x_write8(R_OP_CTRL1, ENWT)
#define SI443X_MODE_SENSOR()			si443x_write8(R_OP_CTRL1, ENLBD)
#define SI443X_MODE_READY()				si443x_write8(R_OP_CTRL1, XTON)
#define SI443X_MODE_TUNE()				si443x_write8(R_OP_CTRL1, PLLON)
#define SI443X_MODE_TX()				si443x_write8(R_OP_CTRL1, TXON | XTON)
#define SI443X_MODE_RX()				si443x_write8(R_OP_CTRL1, RXON | XTON)

#define SI443X_CLEAR_TX_FIFO()			{ \
										si443x_write8(R_OP_CTRL2, FFCLRTX); \
										si443x_write8(R_OP_CTRL2, 0); \
										}
#define SI443X_CLEAR_RX_FIFO()			{ \
										si443x_write8(R_OP_CTRL2, FFCLRRX); \
										si443x_write8(R_OP_CTRL2, 0); \
										}
#define SI443X_CLEAR_FIFOS()			{ \
										si443x_write8(R_OP_CTRL2, FFCLRTX | FFCLRRX); \
										si443x_write8(R_OP_CTRL2, 0); \
										}

/*********************/
/* Private functions */
/*********************/

static void si443x_read(uint8_t addr, uint8_t *data, uint8_t size)
{
	unsigned int n;

	SELECT();
	SPI_IO(READ | (addr & 0x7f));
	for (n = 0; n < size; n++) {
		*data++ = SPI_IO(0);
	}
	DESELECT();
}

static void si433x_write(uint8_t addr, const uint8_t *data, uint8_t size)
{
	unsigned int n;

	SELECT();
	SPI_IO(WRITE | (addr & 0x7f));
	for (n = 0; n < size; n++) {
		uint8_t c = *data++;
		SPI_IO(c);
	}
	DESELECT();
}

static uint8_t si443x_read8(uint8_t addr)
{
	uint8_t val;
	si443x_read(addr, &val, 1);
	return val;
}

static uint16_t si443x_read16(uint8_t addr)
{
	uint16_t val;
	si443x_read(addr, (uint8_t*)&val, 2);
	return (val >> 8) | (val << 8);
}

static void si443x_write8(uint8_t addr, uint8_t val)
{
	si433x_write(addr, &val, 1);
}

static void si443x_write16(uint8_t addr, uint16_t val)
{
	uint16_t val1 = (val >> 8) | (val << 8);
	si433x_write(addr, (uint8_t*)&val1, 2);
}

static void si443x_device_init(void)
{
	const uint8_t *ptr = phy_si443x_config;

	/* Radio configuration is stored as
	 * <address>, <n registers>, <data> ...,
	 * with a terminating null after the last group.
	 */
	while (TABLE_READ(ptr)) {
		uint8_t address = TABLE_READ(ptr++);
		uint8_t block_size = TABLE_READ(ptr++);

		SELECT();
		SPI_IO(WRITE | address);
		while (block_size--) {
			SPI_IO(TABLE_READ(ptr++));
		}
		DESELECT();
	}
}

/*! Updates driver state according to radio's event flags */
static void si443x_event_handler(void)
{
	uint16_t status;

	while (!INP(nIRQ)) {
		status = SI443X_STATUS(); /* Reading clears bits */
//		TRACE("%04X\n", status);

		/* Receive operations */
		if (phy_si443x_state == stateListen && (status & ISWDET)) {
			/* Sync valid */
			phy_si443x_state = stateRx;
			phy_si443x_rssi = RSSI_DBM(si443x_read8(R_RSSI)); /* Sample RSSI */
		}
		if (status & IRXFFAFULL) {
			/* FIFO almost full - release mainline to start flushing the FIFO */
			phy_si443x_state = stateRxReady;
		}
		if (status & IPKVALID) {
			/* Packet received successfully */
			phy_si443x_state = stateRxValid;
		}
		if (status & ICRCERROR) {
			/* Packet received unsuccessfully */
			phy_si443x_state = stateRxInvalid;
		}
		if (status & IFFERR) {
			/* FIFO full */
			phy_si443x_state = stateFifoError;
		}

		/* Transmit operations */
		if (status & ITXFFAFULL) {
			/* TX FIFO almost full */
			/* Stall FIFO filling */
			phy_si443x_state = stateTxBusy;
		}
		if (status & ITXFFAEM) {
			/* TX FIFO almost empty */
			/* Resume FIFO filling */
			phy_si443x_state = stateTx;
		}
		if (status & IPKSENT) {
			/* End of transmission */
			phy_si443x_state = stateStandby;
		}
	}
}

int phy_init(void)
{
#ifdef PLATFORM_INIT
	/* Configure platform */
	PLATFORM_INIT();
#endif

	/* Configure comms */
	SPI_INIT();

	return phy_resume();
}

int phy_suspend(void)
{
	FUNCTION_TRACE;
	POWER_DOWN();
	return 0;
}

int phy_resume(void)
{
	uint8_t device, version;

	FUNCTION_TRACE;

	/* Power up */
	POWER_UP();
	DELAY_MS(50);

	/* Check for supported device */
	device = SI443X_DEVICE_TYPE();
	version = SI443X_DEVICE_VERSION();
	INFO("Found device type %d version %d\n", device, version);
	if (device != SUPPORTED_DEVICE_TYPE || version != SUPPORTED_DEVICE_VERSION) {
		ERROR("Si443x not found\n");
		return -1;
	}

	/* Software reset - poll for completion */
	INFO("Resetting radio...\n");
	SI443X_SWRESET();
	while (INP(nIRQ)) {}
	INFO("Done\n");
	DELAY_MS(1); /* FIXME: What are we really waiting for here - presence of the INFO above seems enough */

	/* Configure radio */
	si443x_device_init();

	/* Initialise state machine and enable interrupts/events */
	phy_standby();
	si443x_write16(R_INT_ENABLE, 0); /* Clear first to ensure an edge */
	si443x_write16(R_INT_ENABLE,
			ENTXFFAFULL | ENTXFFAEM | ENRXFFAFULL |
			ENPKSENT |
			ENSWDET | ENPKVALID | ENCRCERROR);
	return 0;
}

int phy_listen(void)
{
	/* Enter receive mode */
	SI443X_CLEAR_FIFOS();
	SI443X_MODE_RX();
	phy_si443x_state = stateListen;
	return 0;
}

int phy_standby(void)
{
	/* No checks for current state - we can abort any ongoing
	 * process at any time */
	SI443X_CLEAR_FIFOS();
	SI443X_MODE_STANDBY();
	phy_si443x_state = stateStandby;
	return 0;
}

void phy_register_recv_cb(phy_recv_cb_t cb)
{
	phy_si443x_recv_cb = cb;
}

void phy_event_handler(void)
{
	static char payload[MAX_PACKET];
	unsigned int rxsize;

	si443x_event_handler();
	switch (phy_si443x_state) {
	case stateRxValid:
		/* Valid packet received */
		/* TODO: No handling of packets > transceiver FIFO size */
		rxsize = si443x_read8(R_RX_LENGTH);
		TRACE("rxsize = %u, rssi = %d\n", rxsize, phy_si443x_rssi);
		if (rxsize > MAX_PACKET) {
			ERROR("packet truncated\n");
			rxsize = MAX_PACKET;
		}
		si443x_read(R_FIFO, payload, rxsize);

		/* FIXME: Relying on the Si443x CRC here, which may not be suitable
		 * for interoperability - insert software calculation here */

		/* Back to idle state otherwise if the callback tries to send something it won't work */
		phy_listen();

		/* Despatch to callback */
		if (phy_si443x_recv_cb) {
			phy_si443x_recv_cb(payload, (size_t)rxsize, phy_si443x_rssi);
		}

		break;
	case stateRxInvalid:
	case stateFifoError:
		ERROR("Rx error\n");
		phy_listen();
		break;
	case stateRx:
	case stateRxReady:
	default:
		/* Nothing to do this time */
		break;
	}
}

int phy_send(phy_buf_t *bufs, unsigned int nbufs, uint8_t flags)
{
	unsigned int size, n;
	uint8_t tx_started = 0;
	phy_buf_t *ptr = bufs;
	phy_buf_t buf;

	assert(bufs != NULL);
	assert(nbufs > 0);

	/* Calculate total size for all fragments */
	size = 0;
	for (n = 0; n < nbufs; n++) {
		size += bufs[n].size;
	}

	/* Make sure receiver isn't already busy */
	si443x_event_handler();
	if (phy_si443x_state > stateListen) {
		ERROR("send: busy (%d)\n",(int)phy_si443x_state);
		return -1;
	}
	phy_si443x_state = stateTx;

	/* Start pushing data to FIFO */
	SI443X_CLEAR_TX_FIFO();
	si443x_write8(R_TX_LENGTH, size);
	buf = *ptr++;
	do {
		if (phy_si443x_state == stateTx) {
			TRACE("tx fill (%u left)\n", size);

			/* Copy (more) data to FIFO */
			SELECT();
			SPI_IO(R_FIFO | WRITE);
			while (size) {
				/* Walk through buffer list. We already know the
				 * total size so no need to check again for end of list. */
				while (buf.size == 0) {
					buf = *ptr++;
				}
				SPI_IO(*(buf.buf++));
				buf.size--;
				size--;

				/* Drop out if an event is pending */
				if (!INP(nIRQ)) {
					break;
				}
			}
			DESELECT();
		} else {
			/* Wait for FIFO to drain (or send to complete) */
			WAIT_EVENT();
		}

		/* Event handler may place us into TxBusy.  If this occurs or all data has
		 * been sent then enter transmit mode. */
		si443x_event_handler();

		if (!tx_started && (phy_si443x_state == stateTxBusy || size == 0)) {
			/* Enable transmitter */
			TRACE("tx start\n");
			SI443X_MODE_TX();
			tx_started = 1;
		}
	} while (size);

	/* wait for completion */
	while (phy_si443x_state != stateStandby) {
		WAIT_EVENT();
		si443x_event_handler();
	}
	TRACE("tx done\n");
	phy_listen();

	return 0;
}

int phy_set_power(int dbm)
{
	FUNCTION_TRACE;
	si443x_write8(R_TX_POWER, TXPOW(dbm) | LNA_SW);
	return TXDBM(TXPOW(dbm));
}

int phy_set_channel(unsigned int n)
{
	FUNCTION_TRACE;
	si443x_write8(R_CHANNEL, n);
	return (int)n;
}

unsigned int phy_get_mtu(void)
{
	return MAX_PACKET - 2; /* CRC takes up two bytes */
}

int phy_get_fd(void)
{
	return 0;
}

