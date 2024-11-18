
// Define IOCTL's and structures shared between KMDF and Application
#define IOCTL_GET_NOTIFICATION 0x1

typedef struct {
    uint64_t count;
    uint64_t timestamp;
    uint32_t  lastevent;
} NotificationRsp_t;

typedef struct {
    uint8_t type;
} NotificationReq_t;
