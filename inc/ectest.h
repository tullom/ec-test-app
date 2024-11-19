
// Define IOCTL's and structures shared between KMDF and Application
#define IOCTL_GET_NOTIFICATION 0x1

typedef struct {
    UINT64 count;
    UINT64 timestamp;
    UINT32  lastevent;
} NotificationRsp_t;

typedef struct {
    UINT8 type;
} NotificationReq_t;
