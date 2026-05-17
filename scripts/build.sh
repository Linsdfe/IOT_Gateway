#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_FILE="${PROJECT_DIR}/tools/arm-linux-gnueabihf.cmake"
BOARD_IP="192.168.7.2"
BOARD_USER="debian"
BOARD_PASS="temppwd"
REMOTE_HOME="/home/debian"
REMOTE_PLUGIN_DIR="${REMOTE_HOME}/iot_plugins"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }
step()  { echo -e "${CYAN}[STEP]${NC} $1"; }

build_cross() {
    local build_type="${1:-Release}"
    local build_dir="${PROJECT_DIR}/build"
    local mqtt_flag="${2:-ON}"

    step "交叉编译 ARM ${build_type} 版本..."
    info "工具链: ${TOOLCHAIN_FILE}"
    info "构建目录: ${build_dir}"

    if [ ! -f "${TOOLCHAIN_FILE}" ]; then
        error "工具链文件不存在: ${TOOLCHAIN_FILE}"
        return 1
    fi

    cmake -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DUSE_MOSQUITTO="${mqtt_flag}" \
        -DENABLE_DISPLAY=ON \
        -S "${PROJECT_DIR}"

    cmake --build "${build_dir}" -j$(nproc)

    local bin="${build_dir}/iot_gateway"
    if [ -f "${bin}" ]; then
        info "编译成功: ${bin}"
        file "${bin}" | head -1
        ls -lh "${bin}"
    else
        error "编译失败: 未生成可执行文件"
        return 1
    fi

    local plugin_dir="${build_dir}/plugins"
    if [ -d "${plugin_dir}" ]; then
        info "插件编译成功:"
        ls -lh "${plugin_dir}/"*.so 2>/dev/null || warn "未找到插件 .so 文件"
    fi
}

build_onboard() {
    local build_type="${1:-Release}"

    step "在开发板上本地编译 ${build_type} 版本..."

    if ! ping -c 1 -W 2 "${BOARD_IP}" &>/dev/null; then
        error "开发板 ${BOARD_IP} 不可达"
        return 1
    fi

    info "同步源码到开发板..."
    cd "${PROJECT_DIR}"
    tar czf /tmp/iot_gateway_src.tar.gz \
        --exclude='build' \
        --exclude='build-debug' \
        --exclude='build-arm-debug' \
        --exclude='.vscode' \
        --exclude='.git' \
        --exclude='tools/sysroot' \
        .

    sshpass -p "${BOARD_PASS}" scp -o StrictHostKeyChecking=no \
        /tmp/iot_gateway_src.tar.gz "${BOARD_USER}@${BOARD_IP}:${REMOTE_HOME}/"

    info "在开发板上执行编译..."
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" bash -s << REMOTE_SCRIPT
set -e
cd ${REMOTE_HOME}
rm -rf iot_gateway_build
mkdir -p iot_gateway_build
tar xzf iot_gateway_src.tar.gz -C iot_gateway_build
cd iot_gateway_build

echo "[Onboard] 安装编译依赖..."
sudo apt update -qq
sudo apt install -y -qq cmake g++ make libmosquitto-dev 2>/dev/null

echo "[Onboard] 开始编译..."
cmake -B build \
    -DCMAKE_BUILD_TYPE=${build_type} \
    -DUSE_MOSQUITTO=ON \
    -DENABLE_DISPLAY=ON \
    .
cmake --build build -- -j\$(nproc)

echo "[Onboard] 编译结果:"
file build/iot_gateway
ls -lh build/iot_gateway

echo "[Onboard] 插件编译结果:"
ls -lh build/plugins/*.so 2>/dev/null || echo "无插件"

echo "[Onboard] 复制到主目录..."
cp build/iot_gateway ${REMOTE_HOME}/iot_gateway_onboard

echo "[Onboard] 安装插件..."
sudo mkdir -p /usr/lib/iot/plugins
sudo cp build/plugins/*.so /usr/lib/iot/plugins/ 2>/dev/null || true

echo "[Onboard] 编译完成: ${REMOTE_HOME}/iot_gateway_onboard"
REMOTE_SCRIPT

    info "开发板本地编译完成"
}

deploy() {
    local bin="${PROJECT_DIR}/build/iot_gateway"
    if [ ! -f "${bin}" ]; then
        error "可执行文件不存在: ${bin}，请先编译"
        return 1
    fi

    if ! ping -c 1 -W 2 "${BOARD_IP}" &>/dev/null; then
        error "开发板 ${BOARD_IP} 不可达"
        return 1
    fi

    step "部署到开发板..."
    sshpass -p "${BOARD_PASS}" scp -o StrictHostKeyChecking=no \
        "${bin}" "${BOARD_USER}@${BOARD_IP}:${REMOTE_HOME}/iot_gateway"

    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" bash -s << REMOTE_SCRIPT
sudo mkdir -p /usr/lib/iot/plugins
REMOTE_SCRIPT

    local plugin_dir="${PROJECT_DIR}/build/plugins"
    if [ -d "${plugin_dir}" ]; then
        info "部署插件到开发板..."
        sshpass -p "${BOARD_PASS}" scp -o StrictHostKeyChecking=no \
            ${plugin_dir}/*.so "${BOARD_USER}@${BOARD_IP}:/tmp/"
        sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" bash -s << 'REMOTE_SCRIPT'
sudo cp /tmp/lib*_plugin.so /usr/lib/iot/plugins/
sudo chmod 755 /usr/lib/iot/plugins/*.so
ls -lh /usr/lib/iot/plugins/
REMOTE_SCRIPT
    fi

    info "部署完成: ${REMOTE_HOME}/iot_gateway"
    info "插件目录: /usr/lib/iot/plugins/"
}

verify() {
    if ! ping -c 1 -W 2 "${BOARD_IP}" &>/dev/null; then
        error "开发板 ${BOARD_IP} 不可达"
        return 1
    fi

    step "验证开发板上的可执行文件和插件..."
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" bash -s << 'REMOTE_SCRIPT'
echo "=== 可执行文件 ==="
if [ -f ~/iot_gateway ]; then
    file ~/iot_gateway
    ls -lh ~/iot_gateway
else
    echo "交叉编译版本不存在"
fi

echo ""
echo "=== 插件目录 ==="
if [ -d /usr/lib/iot/plugins ]; then
    ls -lh /usr/lib/iot/plugins/*.so 2>/dev/null || echo "无插件"
else
    echo "插件目录不存在"
fi

echo ""
echo "=== 运行测试 (--help) ==="
if [ -f ~/iot_gateway ]; then
    ~/iot_gateway --help 2>&1 | head -10
fi

echo ""
echo "=== 插件列表测试 ==="
if [ -f ~/iot_gateway ]; then
    ~/iot_gateway --list-plugins 2>&1
fi
REMOTE_SCRIPT
}

case "${1:-help}" in
    cross)
        build_cross "${2:-Release}" "${3:-ON}"
        ;;
    onboard)
        build_onboard "${2:-Release}"
        ;;
    both)
        build_cross "${2:-Release}" "${3:-ON}"
        build_onboard "${2:-Release}"
        ;;
    deploy)
        deploy
        ;;
    verify)
        verify
        ;;
    all)
        build_cross Release ON
        deploy
        verify
        ;;
    help|*)
        echo "IoT Gateway 统一编译脚本（支持动态插件）"
        echo ""
        echo "用法: $0 <命令> [build_type] [mqtt]"
        echo ""
        echo "命令:"
        echo "  cross [Release|Debug] [ON|OFF]  虚拟机交叉编译 (默认: Release ON)"
        echo "  onboard [Release|Debug]          开发板本地编译"
        echo "  both   [Release|Debug]           交叉编译 + 本地编译"
        echo "  deploy                           部署可执行文件+插件到开发板"
        echo "  verify                           验证开发板上的可执行文件和插件"
        echo "  all                              交叉编译 + 部署 + 验证"
        echo ""
        echo "插件目录:"
        echo "  开发板: /usr/lib/iot/plugins/"
        echo "  编译输出: build/plugins/"
        echo ""
        echo "运行时插件选项:"
        echo "  --plugin-dir DIR    指定插件搜索目录"
        echo "  --plugin FILE       指定插件 .so 文件"
        echo "  --plugin-config C   插件配置字符串"
        echo "  --list-plugins      列出可用插件"
        ;;
esac
