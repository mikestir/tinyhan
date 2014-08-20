#ifndef PHY_H_
#define PHY_H_

typedef struct {
	char *buf;
	size_t size;
} phy_buf_t;

typedef void(*phy_recv_cb_t)(const char *buf, size_t size);

int phy_init(void);

int phy_suspend(void);
int phy_resume(void);
int phy_listen(void);
int phy_standby(void);

void phy_register_recv_cb(phy_recv_cb_t cb);
void phy_process(void);

int phy_send(const phy_buf_t *bufs, unsigned int nbufs);

unsigned int phy_get_mtu(void);
/* For polling if running on an OS */
int phy_get_fd(void);

#endif
