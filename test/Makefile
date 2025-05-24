CC   := gcc
CFLAGS := -Wall -O2
TARGET := test_dict

all: $(TARGET)

$(TARGET): test_dict.c
	$(CC) $(CFLAGS) -o $@ test_dict.c

clean:
	rm -f $(TARGET)
