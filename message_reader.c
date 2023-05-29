#include "message_slot.h"    

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int main(int argc, char** argv)
{
  int file_desc;
  int channel_id;
  char buf[BUF_LEN + 1];
  buf[BUF_LEN] = '\0'; // just in case

  if (argc != 3)
  {
    perror("Usage: <device_path> <channel>\n");
    exit(-1);
  }

  file_desc = open( argv[1], O_RDONLY );
  if (file_desc < 0) {
    perror("Can't open device file\n");
    exit(-1);
  }

  errno = 0;
  channel_id = strtol(argv[2], NULL, 10);
  if (errno != 0)
  {
    perror("Channel id invalid");
    exit(-1);
  }

  if (ioctl( file_desc, IOCTL_MSG_SLOT_CHANNEL, channel_id) < 0)
  {
    perror("ioctl failed");
    exit(-1);
  }

  if (read( file_desc, buf, BUF_LEN) < 0)
  {
    perror("read failed");
    exit(-1);
  }

  close(file_desc);

  printf("%s", buf);

  return SUCCESS;
}
