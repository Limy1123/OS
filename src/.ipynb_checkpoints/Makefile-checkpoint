# 定义编译器
CC = gcc

# 定义编译选项
CFLAGS = -Wall -g -std=c99

# 目标文件
TARGET = fibo

# 源文件
SRCS = fibo.c

# 目标规则
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# 清理规则
clean:
	rm -f $(TARGET) *.o
