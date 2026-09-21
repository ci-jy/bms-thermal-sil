#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/can/raw.h>

static void set_nonblocking(int fd)
{
    const int fl = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

bool transport_open_socketcan(transport_t *t, const char *ifname)
{
    const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        return false;
    }
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        close(fd);
        return false;
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    set_nonblocking(fd);
    t->fd = fd;
    return true;
}

void transport_from_fd(transport_t *t, int fd)
{
    set_nonblocking(fd);
    t->fd = fd;
}

int transport_try_read(transport_t *t, struct can_frame *f)
{
    const ssize_t n = read(t->fd, f, sizeof(*f));
    if (n == (ssize_t)sizeof(*f)) {
        return 1;
    }
    if (n == 0) {
        return -1;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    return (n < 0) ? -1 : 0;
}

bool transport_write(transport_t *t, const struct can_frame *f)
{
    for (int tries = 0; tries < 1000; tries++) {
        const ssize_t n = write(t->fd, f, sizeof(*f));
        if (n == (ssize_t)sizeof(*f)) {
            return true;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR && errno != ENOBUFS) {
            return false;
        }
        usleep(50);
    }
    return false;
}
