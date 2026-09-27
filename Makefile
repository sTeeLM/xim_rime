# 定义编译器和编译选项
CC      = gcc
CFLAGS  = -g -I/opt/local/include -I/opt/local/include/freetype2
LDFLAGS = -L/opt/local/lib
LIBS    = -lX11 -lxcb -lxcb-keysyms -lxcb-util -lxcb-imdkit -lrime

# 定义目标文件和源文件
TARGET  = xim_rime
SRCS    = xim_rime.c
OBJS    = $(SRCS:.c=.o)

# 默认规则：编译目标程序
all: $(TARGET)

# 链接规则
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)

# 编译源文件为对象文件的规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理编译产物的规则
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean

