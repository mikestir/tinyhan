#ifndef PHY_H_
#define PHY_H_

#include <stdint.h>

/*! Buffer list fragment */
typedef struct {
	char *buf;		/*< Pointer to this buffer fragment */
	size_t size;	/*< Size of this buffer fragment */
} phy_buf_t;

/*! This packet is to be sent immediately (no clear channel assessment) */
#define PHY_FLAG_IMMEDIATE		(1 << 0)

#define PHY_RSSI_NONE			0

/*!
 * Definition of function to be called when a packet is received.
 * \param buf		Pointer to buffer containing received packet
 * \param size		Size of received packet (bytes)
 * \param rssi		Received signal strength (dBm), or 0 if not supported
 */
typedef void(*phy_recv_cb_t)(const char *buf, size_t size, int rssi);

/*!
 * Initialise the PHY
 * \return			Zero on success or -ve error code
 */
int phy_init(void);

/*!
 * Power down the PHY (for extended periods of sleep)
 * \return			Zero on success
 */
int phy_suspend(void);

/*!
 * Power up the PHY from suspended state
 * \return			Zero on success or -ve error code
 */
int phy_resume(void);

/*!
 * Places the PHY in receive mode.  Received packets are returned
 * asynchronously to the callback registered with \see phy_register_recv_cb
 *
 * \return			Zero on success or -ve error code
 */
int phy_listen(void);

/*!
 * Places the PHY in standby mode (fast entry to receive or transmit states
 * from this mode).  No packets will be received.  Lower power consumption.
 *
 * \return			Zero on success or -ve error code
 */
int phy_standby(void);

/*!
 * Register callback function for received packets (\see phy_recv_cb_t)
 *
 * \param cb		Pointer to callback function
 */
void phy_register_recv_cb(phy_recv_cb_t cb);

/*!
 * Handler function to be called when the PHY has signalled that data
 * is available.  This should normally not be called from an ISR.
 */
void phy_recv_handler(void);

/*!
 * Sends a packet with collision avoidance (clear channel assessment),
 * unless explicitly disabled.
 *
 * \param bufs		Pointer to buffer list
 * \param nbufs		Number of buffers to be sent
 * \param flags		Options (PHY_FLAG_IMMEDIATE = send now with no CCA)
 * \return			0 on success or -ve error code (e.g. CCA timeout)
 */
int phy_send(phy_buf_t *bufs, unsigned int nbufs, uint8_t flags);

/*!
 * Attempt to set the transmitter output power to the specified value
 * \param dbm		Desired output power in dBm
 * \return			Actual output power in dBm
 */
int phy_set_power(int dbm);

/*!
 * Attempt to set the radio channel to the specified value
 * \param n			Channel to select
 * \return			Channel number selected or -ve error
 */
int phy_set_channel(unsigned int n);

/*!
 * Returns the maximum packet size that may be transmitted by this PHY
 * \return			MTU in bytes
 */
unsigned int phy_get_mtu(void);

/*! For polling if running on an OS */
int phy_get_fd(void);

#endif
