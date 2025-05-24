#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#define KEY_MAX   64
#define VALUE_MAX 256

#define KDICT_IOC_MAGIC  'k'
#define KDICT_IOC_SET    _IOW(KDICT_IOC_MAGIC, 1, struct kdict_pair)
#define KDICT_IOC_GET    _IOWR(KDICT_IOC_MAGIC, 2, struct kdict_pair)

struct kdict_pair {
    char key[KEY_MAX];
    char value[VALUE_MAX];
};

static int fd;

int set_kv(const char *key, const char *val) {
    struct kdict_pair p;
    memset(&p, 0, sizeof(p));
    strncpy(p.key, key, sizeof(p.key)-1);
    strncpy(p.value, val, sizeof(p.value)-1);
    if (ioctl(fd, KDICT_IOC_SET, &p) < 0) {
        perror("SET ioctl");
        return -1;
    }
    return 0;
}

int get_kv(const char *key, char *out, size_t out_len) {
    struct kdict_pair p;
    memset(&p, 0, sizeof(p));
    strncpy(p.key, key, sizeof(p.key)-1);
    if (ioctl(fd, KDICT_IOC_GET, &p) < 0) {
        if (errno == ENOENT) {
            printf("GET '%s': not found (expected for missing)\n", key);
            return 1;
        }
        perror("GET ioctl");
        return -1;
    }
    printf("GET '%s' -> '%s'\n", key, p.value);
    if (out) {
        strncpy(out, p.value, out_len-1);
        out[out_len-1] = 0;
    }
    return 0;
}

int main(void) {
    char buf[VALUE_MAX];

    printf("Opening /dev/kernel_dict…\n");
    fd = open("/dev/kernel_dict", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Тест 1: запись и чтение */
    printf("\n-- Тест 1: SET foo=bar, GET foo\n");
    if (set_kv("foo", "bar") != 0) return 2;
    if (get_kv("foo", buf, sizeof(buf)) != 0) return 3;

    /* Тест 2: чтение несуществующего */
    printf("\n-- Тест 2: GET missing\n");
    if (get_kv("missing", buf, sizeof(buf)) != 1) return 4;

    /* Тест 3: перезапись */
    printf("\n-- Тест 3: SET foo=baz, GET foo\n");
    if (set_kv("foo", "baz") != 0) return 5;
    if (get_kv("foo", buf, sizeof(buf)) != 0) return 6;

    /* Тест 4: несколько ключей */
    printf("\n-- Тест 4: SET a=1, SET b=2, GET a, GET b\n");
    if (set_kv("a", "1") != 0) return 7;
    if (set_kv("b", "2") != 0) return 8;
    if (get_kv("a", buf, sizeof(buf)) != 0) return 9;
    if (get_kv("b", buf, sizeof(buf)) != 0) return 10;

    printf("\nВсе тесты пройдены ✔\n");
    close(fd);
    return 0;
}
