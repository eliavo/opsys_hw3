#include "message_slot.h"    

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int main(int argc, char** argv)
{
  int file_desc;
  int channel_id;
  int len;

  if (argc != 4)
  {
    perror("Usage: <device_path> <channel> <message>");
    exit(-1);
  }

  file_desc = open( argv[1], O_WRONLY );
  if (file_desc < 0) {
    perror("Can't open device file");
    exit(-1);
  }

  errno = 0;
  channel_id = strtol(argv[2], NULL, 10);
  if (errno != 0)
  {
    perror("Channel id invalid");
    exit(-1);
  }

  if (ioctl( file_desc, MSG_SLOT_CHANNEL, channel_id) < 0)
  {
    perror("ioctl failed");
    exit(-1);
  }

  len = strlen(argv[3]); // error handling is in the message slot driver code

  if (write( file_desc, argv[3], len) < 0)
  {
    perror("write failed");
    exit(-1);
  }

  close(file_desc);

  return SUCCESS;
}
