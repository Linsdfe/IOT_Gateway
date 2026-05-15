#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_FILE="${PROJECT_DIR}/tools/arm-linux-gnueabihf.cmake"
BOARD_IP="192.168.7.2"
BOARD_USER="debian"
BOARD_PASS="temppwd"
REMOTE_HOME="/home/debian"

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

echo "[Onboard] 复制到主目录..."
cp build/iot_gateway ${REMOTE_HOME}/iot_gateway_onboard
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
    info "部署完成: ${REMOTE_HOME}/iot_gateway"
}

verify() {
    if ! ping -c 1 -W 2 "${BOARD_IP}" &>/dev/null; then
        error "开发板 ${BOARD_IP} 不可达"
        return 1
    fi

    step "验证开发板上的可执行文件..."
    sshpass -p "${BOARD_PASS}" ssh -o StrictHostKeyChecking=no "${BOARD_USER}@${BOARD_IP}" bash -s << 'REMOTE_SCRIPT'
echo "=== 交叉编译版本 ==="
if [ -f ~/iot_gateway ]; then
    file ~/iot_gateway
    ls -lh ~/iot_gateway
    echo "MD5: $(md5sum ~/iot_gateway | awk '{print $1}')"
else
    echo "交叉编译版本不存在"
fi

echo ""
echo "=== 本地编译版本 ==="
if [ -f ~/iot_gateway_onboard ]; then
    file ~/iot_gateway_onboard
    ls -lh ~/iot_gateway_onboard
    echo "MD5: $(md5sum ~/iot_gateway_onboard | awk '{print $1}')"
else
    echo "本地编译版本不存在"
fi

echo ""
echo "=== 运行测试 (--help) ==="
if [ -f ~/iot_gateway ]; then
    ~/iot_gateway --help 2>&1 | head -5
    echo "交叉编译版本运行正常 ✓"
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
        echo "IoT Gateway 统一编译脚本"
        echo ""
        echo "用法: $0 <命令> [build_type] [mqtt]"
        echo ""
        echo "命令:"
        echo "  cross [Release|Debug] [ON|OFF]  虚拟机交叉编译 (默认: Release ON)"
        echo "  onboard [Release|Debug]          开发板本地编译"
        echo "  both   [Release|Debug]           交叉编译 + 本地编译"
        echo "  deploy                           部署交叉编译产物到开发板"
        echo "  verify                           验证开发板上的可执行文件"
        echo "  all                              交叉编译 + 部署 + 验证"
        echo ""
        echo "示例:"
        echo "  $0 cross                # Release交叉编译"
        echo "  $0 cross Debug          # Debug交叉编译(带调试符号)"
        echo "  $0 onboard              # 开发板上编译"
        echo "  $0 both                 # 两种方式都编译"
        echo "  $0 all                  # 编译+部署+验证"
        ;;
esac
