obj-m    := kernel_dict.o
KDIR     := /lib/modules/$(shell uname -r)/build
PWD      := $(shell pwd)
TEST_DIR := test
TEST_BIN := $(TEST_DIR)/test_dict

.PHONY: all load unload clean test help

help:
	@echo "Доступные цели:"
	@echo "  make load    — собрать модуль и загрузить"
	@echo "  make test    — запустить тесты"
	@echo "  make unload  — выгрузить модуль"
	@echo "  make clean   — очистить проект"

all:
	@make -C $(KDIR) M=$(PWD) modules

# Загрузка
load: all
	@sudo rmmod kernel_dict 2>/dev/null || true
	@sudo insmod kernel_dict.ko
	@sudo chmod 0666 /dev/kernel_dict
	@echo ">>> /dev/kernel_dict device:" && ls -l /dev/kernel_dict || true
	@sudo dmesg | tail -n 3

# Выгрузка
unload:
	@sudo rmmod kernel_dict 2>/dev/null || true
	@sudo dmesg | tail -n 1

$(TEST_BIN): $(TEST_DIR)/test_dict.c
	@$(MAKE) -C $(TEST_DIR)

# Запустить тесты
test: load $(TEST_BIN)
	@echo "\n=== Запускаем тесты ==="
	@$(TEST_BIN)

# Чистка
clean:
	@make -C $(KDIR) M=$(PWD) clean
	@$(MAKE) -C $(TEST_DIR) clean
	@sudo rm -f /dev/kernel_dict
