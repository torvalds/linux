#!/bin/bash

# 检查参数
if [ $# -ne 2 ]; then
    echo "用法: $0 <patch目录路径> <git仓库路径>"
    exit 1
fi

PATCH_DIR="$1"
GIT_REPO="$2"

# 检查目录是否存在
if [ ! -d "$PATCH_DIR" ]; then
    echo "错误: patch目录 '$PATCH_DIR' 不存在"
    exit 1
fi

if [ ! -d "$GIT_REPO/.git" ]; then
    echo "错误: '$GIT_REPO' 不是一个有效的Git仓库"
    exit 1
fi

# 计数器
total=0
success=0
failed=0

# 记录日志文件
LOG_FILE="git_patch_apply_$(date +%Y%m%d_%H%M%S).log"

# 切换到Git仓库目录
cd "$GIT_REPO" || exit 1

# 确保工作目录干净

# 遍历所有.patch文件
echo "开始应用patch文件..."
for patch_file in "$PATCH_DIR"/*.patch; do
    if [ -f "$patch_file" ]; then
        ((total++))
        patch_name=$(basename "$patch_file")
        echo "正在应用: $patch_name"
        
        # 尝试应用patch
        if git apply --3way "$patch_file" >> "$LOG_FILE" 2>&1; then
            echo "✓ 成功应用: $patch_name"
            # 自动提交更改
            git add . >> "$LOG_FILE" 2>&1
            git commit -m "Applied patch: $patch_name" >> "$LOG_FILE" 2>&1
            ((success++))
        else
            echo "✗ 失败: $patch_name"
            # 发生冲突时回滚更改
            ##git reset --hard HEAD >> "$LOG_FILE" 2>&1
            ((failed++))
        fi
        echo "----------------------------------------"
    fi
done

# 输出统计信息
echo
echo "patch应用完成!"
echo "总数: $total"
echo "成功: $success"
echo "失败: $failed"
echo "详细日志已保存到: $LOG_FILE"
