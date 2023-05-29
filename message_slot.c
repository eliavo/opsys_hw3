// Declare what kind of code we want
// from the header files. Defining __KERNEL__
// and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE


#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/kdev_t.h>
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>     /* for kmalloc */
#include <linux/ioctl.h>

MODULE_LICENSE("GPL");

//Our custom definitions of IOCTL operations
#include "message_slot.h"


struct channel_list {
  unsigned long channel_id;
  int write_index; // smaller than 128 so no worries
  int size[2]; //smaller than 128 so no worries
  char message[2][BUF_LEN]; // for atomic write operations, we write to the other buffer, and when we're done we switch the pointers
  struct channel_list* next;
};

struct slot_list {
  unsigned long minor_number;
  struct channel_list* channel;
  struct slot_list* next;
};

static struct slot_list* slot_list_head;

struct private_data {
  int channel_id;
  struct channel_list* current_channel;
  struct slot_list* current_slot; // the slot doesn't change after openning the file
};


struct slot_list* assign_slot(int minor_number) {
  /*
  This function retrieves the slot list that corresponds to the given minor number.
  If the slot list doesn't exist, it creates a new one and returns it.
  For simplicity, the end of the slot list is a malloced slot with a NULL next pointer.
  */
  struct slot_list* current_slot = slot_list_head;

  while (current_slot->next != NULL) {
    if (current_slot->minor_number == minor_number) {
      return current_slot;
    }

    current_slot = current_slot->next;
  }

  current_slot->minor_number = minor_number;

  current_slot->channel = (struct channel_list*)kmalloc(sizeof(struct channel_list), GFP_KERNEL);
  current_slot->channel->next = NULL;

  current_slot->next = (struct slot_list*)kmalloc(sizeof(struct slot_list), GFP_KERNEL);
  current_slot->next->next = NULL;

  return current_slot;
}

void free_slot(struct slot_list* slot) {
  /*
  This function frees the slot list that corresponds to the given slot.
  */
  struct channel_list* current_channel = slot->channel;
  struct channel_list* next_channel;

  struct slot_list* current_slot = slot_list_head;
  struct slot_list* next_slot;

  while (current_channel != NULL) {
    next_channel = current_channel->next;
    kfree(current_channel);
    current_channel = next_channel;
  }
  
  // now we need to remove the slot from the slot list and fix the pointers

  if (slot == slot_list_head) {
    slot_list_head = current_slot->next;
    kfree(slot);
    return;
  }

  while (current_slot->next != NULL) {
    next_slot = current_slot->next;

    if (next_slot == slot) {
      current_slot->next = next_slot->next;
      kfree(slot);
      return;
    }
  }
}

struct channel_list* assign_channel(unsigned long channel_id, struct slot_list* current_slot) {
  /*
  Similarly to assign_slot, this function retrieves the struct channel_list that
  corresponds to the given channel_id.
  If the channel does not exist, it creates a new one and returns it.
  For simplicity, the end of the channel list is a malloced channel with a NULL next pointer
  */
  struct channel_list* current_channel = current_slot->channel;

  while (current_channel->next != NULL) {
    if (current_channel->channel_id == channel_id) {
      return current_channel;
    }

    current_channel = current_channel->next;
  }

  current_channel->channel_id = channel_id;
  current_channel->write_index = 0;
  current_channel->size[0] = 0;
  current_channel->size[1] = 0;

  current_channel->next = (struct channel_list*)kmalloc(sizeof(struct channel_list), GFP_KERNEL);
  current_channel->next->next = NULL;

  return current_channel;
}

//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
  int minor_number = iminor(inode);
  struct private_data* private_data = (struct private_data*)kmalloc(sizeof(struct private_data), GFP_KERNEL);
  struct slot_list* current_slot = assign_slot(minor_number);

  private_data->current_slot = current_slot;
  private_data->channel_id = 0;
  private_data->current_channel = NULL;

  file->private_data = (void*)private_data;
  return SUCCESS;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
  struct private_data* private_data = (struct private_data*)file->private_data;

  kfree(private_data);
  return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
  struct private_data* private_data = (struct private_data*)file->private_data;
  struct channel_list* current_channel = private_data->current_channel;
  int channel_id = private_data->channel_id;
  char test_char, *message;
  int i, write_index, size;

  if (channel_id == 0) {
    printk("Invalid device_read and failed on current_channel(%p,%p,%ld)\n",
           file, buffer, length);
    return -EINVAL;
  }

  write_index = current_channel->write_index;
  size = current_channel->size[write_index];

  if (size <= 0) {
    printk("Invalid device_read and failed on size(%p,%p,%ld)\n",
           file, buffer, length);
    return -EWOULDBLOCK;
  }

  if (length < size) {
    printk("Invalid read and failed on length too small\n");
    return -ENOSPC;
  }

  if (length > BUF_LEN) {
    printk("Invalid length for device_read and failed on length (user)(%p,%p,%ld)\n",
           file, buffer, length);
    return -EMSGSIZE;
  }

  message = current_channel->message[write_index];

  for (i=0; i < length; ++i) {
    if (get_user(test_char, &buffer[i]) != 0)
      return -1;
  }

  for (i = 0; i < length; ++i) {
    put_user(message[i], &buffer[i]);
  }

  return size;
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
  struct channel_list* current_channel;
  int write_index, i, channel_id;
  char* message;

  struct private_data* private_data = (struct private_data*)file->private_data;
  if (private_data == NULL) {
    printk("Invalid device_write in private_data(%p, %p, %ld)\n",
           file, buffer, length);
    return -EINVAL;
  }

  current_channel = private_data->current_channel;
  channel_id = private_data->channel_id;

  if (channel_id == 0) {
    printk("Invalid device_write(%p,%p,%ld)\n",
           file, buffer, length);
    return -EINVAL;
  }

  if (length == 0 || length > BUF_LEN) {
    printk("Invalid length for device_write(%p,%p,%ld)\n",
           file, buffer, length);
    return -EMSGSIZE;
  }

  write_index = 1 - current_channel->write_index;
  current_channel->size[write_index] = length;

  message = current_channel->message[write_index];

  for (i=0; i < BUF_LEN; ++i)
    message[i] = '\0';

  for(i = 0; i < length; ++i) {
    if (get_user(message[i], &buffer[i]) != 0)
      return -EINVAL;
  }

  current_channel->write_index = write_index; // atomic write operation

  return length;
}

//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  ioctl_param )
{
  struct private_data* private_data = (struct private_data*)file->private_data;

  if (ioctl_command_id != IOCTL_MSG_SLOT_CHANNEL || ioctl_param == 0) {
    printk("Invalid device_ioctl(%p,%u,%ld)\n",
           file, ioctl_command_id, ioctl_param);
    return -EINVAL;
  }

  private_data->channel_id = ioctl_param;
  private_data->current_channel = assign_channel(private_data->channel_id, private_data->current_slot);

  return SUCCESS;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
  .owner	  = THIS_MODULE, 
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .unlocked_ioctl = device_ioctl,
  .release        = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
  int rc = -1;

  // Register driver capabilities. Obtain major num
  rc = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

  // Negative values signify an error
  if( rc < 0 ) {
    printk( KERN_ALERT "%s registraion failed for  %d\n",
                       DEVICE_FILE_NAME, MAJOR_NUM );
    return rc;
  }

  slot_list_head = (struct slot_list*)kmalloc(sizeof(struct slot_list), GFP_KERNEL);
  if (slot_list_head == NULL) {
    printk( KERN_ALERT "Failed to allocate memory for slot list\n");
    return -ENOMEM;
  }
  slot_list_head->next = NULL;

  printk( "Registeration is successful. ");
  printk( "If you want to talk to the device driver,\n" );
  printk( "you have to create a device file:\n" );
  printk( "mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM );
  printk( "You can echo/cat to/from the device file.\n" );
  printk( "Dont forget to rm the device file and "
          "rmmod when you're done\n" );

  return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
  // Unregister the device
  struct slot_list* current_slot = slot_list_head;
  struct slot_list* next_slot;

  while (current_slot != NULL) {
    next_slot = current_slot->next;
    free_slot(current_slot);
    current_slot = next_slot;
  }

  // Should always succeed
  unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
