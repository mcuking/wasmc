CC = gcc
# gcc 的参数，其中 -I 用来告诉编译器第一个寻找头文件的目录；-Wall 表示输出所有类型的 warning；-g 会创建符号表，方便调试
CFLAGS += -Wall -g -I source -lreadline -lm -ldl
TARGET = wasmc
DIRS = source
# 遍历 DIRS 中所有的文件夹，收集其中的 .c 文件
CFILES = $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c))
# 把 $(CFILES) 中的变量符合后缀是.c的全部替换成.o，即目标文件 TARGET 的依赖是所有的 .o 文件，gcc 会将所有的 .o 文件链接成一个可执行文件
OBJS = $(patsubst %.c, %.o, $(CFILES)) 
$(TARGET):$(OBJS)
	$(CC) $(OBJS) $(CFLAGS) -o $(TARGET)
clean:
	-$(RM) $(TARGET) $(OBJS)
