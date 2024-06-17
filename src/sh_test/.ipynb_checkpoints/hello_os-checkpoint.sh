#!/bin/bash

# 检查参数是否正确
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 source_file target_file"
    exit 1
fi

# 获取参数
SOURCE_FILE=$1
TARGET_FILE=$2

# 创建/覆盖目标文件
> $TARGET_FILE

# 提取指定行并写入目标文件
awk 'NR==8 || NR==32 || NR==128 || NR==512 || NR==1024' $SOURCE_FILE > $TARGET_FILE
