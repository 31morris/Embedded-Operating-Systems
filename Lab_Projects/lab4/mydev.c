#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h> 

MODULE_LICENSE("GPL");

static const char *seg_for_c_str[27] = {
    "1111001100010001", // A
    "0000011100000101", // b
    "1100111100000000", // C
    "0000011001000101", // d
    "1000011100000001", // E
    "1000001100000001", // F
    "1001111100010000", // G
    "0011001100010001", // H
    "1100110001000100", // I
    "1100010001000100", // J
    "0000000001101100", // K
    "0000111100000000", // L
    "0011001110100000", // M
    "0011001110001000", // N
    "1111111100000000", // O
    "1000001101000001", // P
    "0111000001010000", // Q
    "1110001100011001", // R
    "1101110100010001", // S
    "1100000001000100", // T
    "0011111100000000", // U
    "0000001100100010", // V
    "0011001100001010", // W
    "0000000010101010", // X
    "0000000010100100", // Y
    "1100110000100010", // Z
    "0000000000000000"  // Space
};

//Store the written letter
static char LETTER;

// Convert lowercase letter to uppercase (if it's already uppercase, return as is)
static char convert_to_display_char(char letter) {
    if (letter >= 'a' && letter <= 'z') {
        return letter - 'a' + 'A';  
    }
    return letter;  // If not lowercase, return the original character
}

static ssize_t my_read(struct file *fp, char __user *buf, size_t count, loff_t *fpos)
{
    // Read the current value of LETTER
    char letter = LETTER;

    // Check if the letter is a valid uppercase alphabet (A-Z)
    if (letter < 'A' || letter > 'Z') {
        pr_err("ERROR: Invalid letter '%c'\n", letter);  
        return -EINVAL;  
    }

    // Ensure the requested read size is at least 16 bytes
    if (count < 16) {
        pr_err("ERROR: Requested read size is too small, must be at least 16 bytes\n");
        return -EINVAL;
    }

    // Copy 16 bytes from kernel space to user space
    if (copy_to_user(buf, seg_for_c_str[letter - 'A'], 16) > 0) {
        pr_err("ERROR: Not all the bytes have been copied to user\n");
        return -EFAULT;  
    }

    printk("Read:");
    printk(seg_for_c_str[letter - 'A']);
    printk("\n");

    return count;  
}

static ssize_t my_write(struct file *fp, const char __user *buf, size_t count, loff_t *fpos)
{
    // Initialize LETTER to 0
    LETTER = 0;

    // Copy 1 byte from user space to LETTER
    if (copy_from_user(&LETTER, buf, 1) > 0) {
        pr_err("ERROR: Not all the bytes have been copied from user\n");
        return -EFAULT;  
    }

    // Print debug message when write is called
    printk("call write\n");

    // Convert the input character to uppercase
    LETTER = convert_to_display_char(LETTER);

    printk("Write: Received string = %c\n", LETTER);

    return count;  
}

static int my_open(struct inode *inode, struct file *fp) 
{
  printk("call open\n");
  return 0;
}

static int my_release(struct inode *inode, struct file *fp) 
{
  printk("call release\n");
  return 0;
}

//File operation structure  
static struct file_operations my_fops = 
{
  read: my_read,
  write: my_write,
  open: my_open,
  release: my_release,
};


/* 
** Module Init function 
*/  
#define MAJOR_NUM 456
#define DEVICE_NAME "mydev"

static int my_init(void) {
    printk("call init\n");
    if(register_chrdev(MAJOR_NUM, DEVICE_NAME, &my_fops) < 0) {
        printk("Can not get major %d\n", MAJOR_NUM);
        return (-EBUSY);
    }

    printk("My device is started and the major is %d\n", MAJOR_NUM);
    return 0;
}
static void my_exit(void) 
{
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
    printk("call exit\n");
}

module_init(my_init);
module_exit(my_exit);



