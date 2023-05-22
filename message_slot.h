#ifndef MSG_SLOT_H
#define MSG_SLOT_H

// The major device number.
// We don't rely on dynamic registration
// any more. We want ioctls to know this
// number at compile time.
//#define MAJOR_NUM 244
#define MAJOR_NUM 235

// Set the message of the device driver
#define IOCTL_MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)

#define DEVICE_RANGE_NAME "char_dev"
#define BUF_LEN 128
#define DEVICE_FILE_NAME "simple_char_dev"
#define SUCCESS 0

#endif
