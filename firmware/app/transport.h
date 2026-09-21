#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <linux/can.h>
#include <stdbool.h>

/* A CAN transport is a file descriptor carrying raw `struct can_frame`
 * records: either a SocketCAN raw socket (vcan0) or one end of an
 * AF_UNIX SOCK_SEQPACKET socketpair inherited from the SIL harness. */
typedef struct {
    int fd;
} transport_t;

bool transport_open_socketcan(transport_t *t, const char *ifname);
void transport_from_fd(transport_t *t, int fd);
/* Returns 1 when a frame was read, 0 when none is pending, -1 when closed. */
int transport_try_read(transport_t *t, struct can_frame *f);
bool transport_write(transport_t *t, const struct can_frame *f);

#endif
