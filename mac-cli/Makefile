CC      = cc
CFLAGS  = -O2 -Wall -Wextra -arch arm64
LDFLAGS = -arch arm64 -framework IOKit -framework CoreFoundation
TARGET  = s5-unlocker

all: $(TARGET)

$(TARGET): s5_unlocker.c optb_image.h
	$(CC) $(CFLAGS) -o $(TARGET) s5_unlocker.c $(LDFLAGS)

clean:
	rm -f $(TARGET)

check: $(TARGET)
	file $(TARGET)
	./$(TARGET) --self-test

.PHONY: all clean check
