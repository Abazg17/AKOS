#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/jhash.h>
#include <linux/string.h>

#define DEVICE_NAME    "kernel_dict"
#define CLASS_NAME     "kdict"
#define DICT_HASH_BITS      8               /* 2^8 = 256 buckets */
#define KEY_MAX_LEN    64
#define VAL_MAX_LEN    256

/* ioctl magic и команды */
#define KDICT_IOC_MAGIC  'k'
#define KDICT_IOC_SET    _IOW(KDICT_IOC_MAGIC, 1, struct kdict_pair)
#define KDICT_IOC_GET    _IOWR(KDICT_IOC_MAGIC, 2, struct kdict_pair)

struct kdict_pair {
    char key[KEY_MAX_LEN];
    char value[VAL_MAX_LEN];
};

/* Структура элемента словаря */
struct dict_entry {
    struct hlist_node node;
    char key[KEY_MAX_LEN];
    char value[VAL_MAX_LEN];
};

static dev_t dev_number;
static struct cdev kdict_cdev;
static struct class *kdict_class;
static DEFINE_HASHTABLE(dict_table, DICT_HASH_BITS);
static DEFINE_MUTEX(dict_mutex);

/* Ищет запись по ключу, возвращает NULL или указатель */
static struct dict_entry *dict_find(const char *key)
{
    struct dict_entry *e;
    u32 h = jhash(key, strnlen(key, KEY_MAX_LEN), 0);

    hash_for_each_possible(dict_table, e, node, h) {
        if (strncmp(e->key, key, KEY_MAX_LEN) == 0)
            return e;
    }
    return NULL;
}

/* Обработчик ioctl */
static long kdict_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
    struct kdict_pair user_kv;
    struct dict_entry *e;
    u32 h;

    if (_IOC_TYPE(cmd) != KDICT_IOC_MAGIC)
        return -ENOTTY;

    if (cmd == KDICT_IOC_SET) {
        if (copy_from_user(&user_kv, (void __user *)arg, sizeof(user_kv)))
            return -EFAULT;

        /* ограничить длины строк */
        user_kv.key[KEY_MAX_LEN-1] = 0;
        user_kv.value[VAL_MAX_LEN-1] = 0;

        mutex_lock(&dict_mutex);

        /* обновление или вставка */
        e = dict_find(user_kv.key);
        if (e) {
            /* перезаписать значение */
            strncpy(e->value, user_kv.value, VAL_MAX_LEN);
            e->value[VAL_MAX_LEN-1]= '\0';
        } else {
            /* новая запись */
            e = kmalloc(sizeof(*e), GFP_KERNEL);
            if (!e) {
                mutex_unlock(&dict_mutex);
                return -ENOMEM;
            }
            strncpy(e->key, user_kv.key, KEY_MAX_LEN);
            e->key[KEY_MAX_LEN-1] = '\0';
            strncpy(e->value, user_kv.value, VAL_MAX_LEN);
            e->value[VAL_MAX_LEN-1] = '\0';
            h = jhash(e->key, strnlen(e->key, KEY_MAX_LEN), 0);
            hash_add(dict_table, &e->node, h);
        }

        mutex_unlock(&dict_mutex);
        return 0;

    } else if (cmd == KDICT_IOC_GET) {
        if (copy_from_user(&user_kv, (void __user *)arg, sizeof(user_kv)))
            return -EFAULT;

        /* гарантируем строку с нуль-терминатором */
        user_kv.key[KEY_MAX_LEN-1] = 0;

        mutex_lock(&dict_mutex);

        e = dict_find(user_kv.key);
        if (!e) {
            mutex_unlock(&dict_mutex);
            return -ENOENT;
        }
        /* копируем значение в user */
        memset(user_kv.value, 0, VAL_MAX_LEN);
        strncpy(user_kv.value, e->value, VAL_MAX_LEN);
        user_kv.value[VAL_MAX_LEN-1] = '\0';

        mutex_unlock(&dict_mutex);

        if (copy_to_user((void __user *)arg, &user_kv, sizeof(user_kv)))
            return -EFAULT;

        return 0;

    } else {
        return -ENOTTY;
    }
}

static int kdict_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int kdict_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations kdict_fops = {
    .owner          = THIS_MODULE,
    .open           = kdict_open,
    .release        = kdict_release,
    .unlocked_ioctl = kdict_ioctl,
};

static int __init kernel_dict_init(void)
{
    int ret;

    /* выделяем номер устройства */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("kdict: не удалось alloc_chrdev_region: %d\n", ret);
        return ret;
    }

    /* регистрируем cdev */
    cdev_init(&kdict_cdev, &kdict_fops);
    kdict_cdev.owner = THIS_MODULE;
    ret = cdev_add(&kdict_cdev, dev_number, 1);
    if (ret) {
        pr_err("kdict: не удалось cdev_add: %d\n", ret);
        unregister_chrdev_region(dev_number, 1);
        return ret;
    }

    /* создаём класс и устройство в /dev */
    kdict_class = class_create(CLASS_NAME);
    if (IS_ERR(kdict_class)) {
        pr_err("kdict: class_create не удался\n");
        cdev_del(&kdict_cdev);
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(kdict_class);
    }
    device_create(kdict_class, NULL, dev_number, NULL, DEVICE_NAME);

    /* инициализация хеш‑таблицы и мьютекса */
    hash_init(dict_table);
    mutex_init(&dict_mutex);

    pr_info("kdict: модуль загружен, /dev/%s (major=%d, minor=%d)\n",
            DEVICE_NAME, MAJOR(dev_number), MINOR(dev_number));
    return 0;
}

static void __exit kernel_dict_exit(void)
{
    struct dict_entry *e;
    struct hlist_node *tmp;
    int bkt;

    /* очистка всех записей */
    mutex_lock(&dict_mutex);
    hash_for_each_safe(dict_table, bkt, tmp, e, node) {
        hash_del(&e->node);
        kfree(e);
    }
    mutex_unlock(&dict_mutex);

    device_destroy(kdict_class, dev_number);
    class_destroy(kdict_class);
    cdev_del(&kdict_cdev);
    unregister_chrdev_region(dev_number, 1);

    pr_info("kdict: модуль выгружен\n");
}

module_init(kernel_dict_init);
module_exit(kernel_dict_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michail_Vataman");
MODULE_DESCRIPTION("Словарь в ядре с хешированием ключей и доступом по чтению и записи");
