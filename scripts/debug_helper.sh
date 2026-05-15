#!/bin/bash
# debug_helper.sh - IoT 网关远程调试辅助脚本
#
# 用法：
#   ./scripts/debug_helper.sh install   首次使用，安装gdbserver
#   ./scripts/debug_helper.sh build     构建ARM Debug版本 (-g -O0)
#   ./scripts/debug_helper.sh deploy    部署调试可执行文件到开发板
#   ./scripts/debug_helper.sh start     在开发板上启动gdbserver
#   ./scripts/debug_helper.sh kill      停止开发板上的gdbserver
#   ./scripts/debug_helper.sh all       构建+部署+启动gdbserver
#
# 远程调试流程：
#   1. ./scripts/debug_helper.sh all    # 构建+部署+启动gdbserver
#   2. VS Code 按 F5                   # 选择 "(远程调试) ARM开发板 - gdbserver"
#
# 命令行调试：
#   gdb-multiarch build-debug/iot_gateway
#   (gdb) target remote 192.168.7.2:2345
#   (gdb) break main
#   (gdb) continue

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_FILE="${PROJECT_DIR}/tools/arm-linux-gnueabihf.cmake"
BOARD_IP="192.168.7.2"
BOARD_USER="debian"
BOARD_PASS="temppwd"
BOARD_GDB_PORT="2345"
REMOTE_BIN="/home/debian/iot_gateway"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }
step()  { echo -e "${CYAN}[STEP]${NC} $1"; }

check_board() {
    if ping -c 1 -W 2 "${BOARD_IP}" &>/dev/null; then
        info "开发板 ${BOARD_IP} 可达"
        return 0
    else
        error "开发板 ${BOARD_IP} 不可达，请检查USB连接"
        return 1
    fi
}

install_gdbserver() {
    check_board || return 1
    step "在开发板上安装 gdbserver..."
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" \
        "sudo apt update -qq && sudo apt install -y -qq gdbserver" 2>&1
    info "gdbserver 安装完成"
}

build_debug() {
    step "构建 ARM Debug 版本 (-g -O0, LVGL)..."
    if [ ! -f "${TOOLCHAIN_FILE}" ]; then
        error "工具链文件不存在: ${TOOLCHAIN_FILE}"
        return 1
    fi

    cmake -B "${PROJECT_DIR}/build-debug" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DUSE_MOSQUITTO=ON \
        -DUSE_LVGL=ON \
        -S "${PROJECT_DIR}"

    cmake --build "${PROJECT_DIR}/build-debug" -j$(nproc)

    if [ -f "${PROJECT_DIR}/build-debug/iot_gateway" ]; then
        info "ARM Debug 构建成功: build-debug/iot_gateway"
        file "${PROJECT_DIR}/build-debug/iot_gateway" | head -1
    else
        error "ARM Debug 构建失败"
        return 1
    fi
}

deploy_debug() {
    check_board || return 1
    local bin="${PROJECT_DIR}/build-debug/iot_gateway"
    if [ ! -f "${bin}" ]; then
        error "调试可执行文件不存在: ${bin}"
        error "请先执行: $0 build"
        return 1
    fi
    step "部署调试可执行文件到开发板..."
    sshpass -p "${BOARD_PASS}" scp -o StrictHostKeyChecking=no \
        "${bin}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_BIN}"
    info "部署完成: ${REMOTE_BIN}"
}

start_gdbserver() {
    check_board || return 1
    step "在开发板上启动 gdbserver (端口 ${BOARD_GDB_PORT})..."
    echo ""
    info "VS Code 调试方式:"
    echo "  按 F5 → 选择 '(远程调试) ARM开发板 - gdbserver'"
    echo ""
    info "命令行调试方式:"
    echo "  gdb-multiarch build-debug/iot_gateway"
    echo "  (gdb) target remote ${BOARD_IP}:${BOARD_GDB_PORT}"
    echo ""
    warn "此进程将持续运行，按 Ctrl+C 停止"
    echo ""
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" \
        "sudo gdbserver :${BOARD_GDB_PORT} ${REMOTE_BIN} --no-mqtt --interval 2000"
}

kill_gdbserver() {
    info "停止开发板上的 gdbserver..."
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" \
        "sudo killall gdbserver 2>/dev/null; echo done"
}

case "${1:-help}" in
    install)
        install_gdbserver
        ;;
    build)
        build_debug
        ;;
    deploy)
        deploy_debug
        ;;
    start)
        start_gdbserver
        ;;
    kill)
        kill_gdbserver
        ;;
    all)
        build_debug && deploy_debug && start_gdbserver
        ;;
    help|*)
        echo "IoT Gateway 远程调试辅助脚本"
        echo ""
        echo "用法: $0 <命令>"
        echo ""
        echo "命令:"
        echo "  install   在开发板上安装 gdbserver (仅首次)"
        echo "  build     构建 ARM Debug 版本 (-g -O0, LVGL)"
        echo "  deploy    部署调试可执行文件到开发板"
        echo "  start     在开发板上启动 gdbserver"
        echo "  kill      停止开发板上的 gdbserver"
        echo "  all       构建 + 部署 + 启动 gdbserver"
        echo ""
        echo "典型远程调试流程:"
        echo "  1. $0 install          # 首次使用，安装gdbserver"
        echo "  2. $0 all              # 构建+部署+启动gdbserver"
        echo "  3. VS Code 按 F5       # 选择 '(远程调试) ARM开发板' 开始调试"
        ;;
esac
